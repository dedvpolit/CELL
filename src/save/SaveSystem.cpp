#include "SaveSystem.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <atomic>
#include <memory>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <process.h> // _beginthreadex, see SaveSlotAsync()
#endif

namespace SaveSystem {

namespace {

namespace fs = std::filesystem;

// A "saves" folder next to the .exe (Windows) or next to the working directory (non-Windows/IDE
// runs): the same basic approach as AssetPath::Resolve() for finding the executable's folder, but
// for WRITING, so the folder is created on first use (AssetPath.cpp never needs to write).
fs::path GetSavesDir() {
    fs::path baseDir;

#ifdef _WIN32
    wchar_t modulePath[32768]{};
    const DWORD len = GetModuleFileNameW(
        nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (len > 0 && len < std::size(modulePath))
        baseDir = fs::path(modulePath).parent_path();
#endif

    if (baseDir.empty()) {
        std::error_code ec;
        baseDir = fs::current_path(ec);
    }

    const fs::path savesDir = baseDir / "saves";
    std::error_code ec;
    fs::create_directories(savesDir, ec);
    return savesDir;
}

fs::path SlotPath(int slotIndex) {
    return GetSavesDir() / ("slot" + std::to_string(slotIndex + 1) + ".sav");
}

// The fog of war is a 0/1 array of length mapW*mapH (16384 at 128x128), almost always with long
// uniform runs (mostly unexplored at the start of the game), so a simple RLE ("value:length",
// comma-separated) is far more compact than 16 thousand raw digits and still human-readable.
// Loaded values come from a plain text file that may be edited or damaged: out-of-range or
// non-finite numbers fall back to a default instead of reaching the game state.
constexpr size_t kMaxExploredCells = 1u << 20;
constexpr size_t kMaxListEntries   = 1u << 16;
constexpr float  kMaxCoordinate    = 10000.0f;

float ParseFloat(const std::string& s, float fallback, float lo, float hi) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end != s.c_str() && std::isfinite(v) && v >= lo && v <= hi ? v : fallback;
}

int ParseInt(const std::string& s, int fallback, int lo, int hi) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    return end != s.c_str() ? (int)std::clamp(v, (long)lo, (long)hi) : fallback;
}

std::string EncodeExplored(const std::vector<unsigned char>& explored) {
    std::ostringstream oss;
    size_t i = 0;
    bool first = true;
    while (i < explored.size()) {
        const unsigned char v = explored[i];
        size_t j = i;
        while (j < explored.size() && explored[j] == v) ++j;

        if (!first) oss << ',';
        first = false;
        oss << (int)v << ':' << (j - i);

        i = j;
    }
    return oss.str();
}

std::vector<unsigned char> DecodeExplored(const std::string& token) {
    std::vector<unsigned char> out;
    size_t pos = 0;
    while (pos < token.size()) {
        const size_t comma = token.find(',', pos);
        const std::string run = token.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos);

        const size_t colon = run.find(':');
        if (colon != std::string::npos) {
            const int v = std::atoi(run.substr(0, colon).c_str());
            const long count = std::atol(run.substr(colon + 1).c_str());
            if (count > 0) {
                if ((size_t)count > kMaxExploredCells - out.size()) return {};
                out.insert(out.end(), (size_t)count, (unsigned char)(v != 0));
            }
        }

        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

} // namespace

// A plain comma-separated number list ("0,3,7"): more compact than RLE for short lists like
// diariesReadIndices (about a dozen elements, unlike explored where the count is in the thousands
// and RLE pays off).
namespace {
std::string EncodeIntList(const std::vector<int>& values) {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) oss << ',';
        oss << values[i];
    }
    return oss.str();
}

std::vector<int> DecodeIntList(const std::string& token) {
    std::vector<int> out;
    size_t pos = 0;
    while (pos < token.size()) {
        const size_t comma = token.find(',', pos);
        const std::string numStr = token.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!numStr.empty()) {
            const int v = std::atoi(numStr.c_str());
            if (v >= 0) out.push_back(v);
            if (out.size() >= kMaxListEntries) break;
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}
} // namespace

bool SlotExists(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= kSlotCount) return false;
    std::error_code ec;
    const fs::path path = SlotPath(slotIndex);
    if (!fs::exists(path, ec) || ec) return false;
    return fs::file_size(path, ec) > 0 && !ec;
}

SaveData LoadSlot(int slotIndex) {
    SaveData data;
    if (slotIndex < 0 || slotIndex >= kSlotCount) return data;

    std::ifstream in(SlotPath(slotIndex));
    if (!in.is_open()) return data; // no file -> valid stays false

    bool sawSeed = false;
    std::string exploredToken;
    std::string diariesReadToken;
    std::string torchTakenToken;
    std::string stoneTakenToken;
    std::string line;
    while (std::getline(in, line)) {
        // Strip a possible trailing '\r' (the file may have been saved or copied on Windows and
        // read line by line differently).
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "SEED") {
            data.seed = (unsigned int)std::strtoul(value.c_str(), nullptr, 10);
            sawSeed = true;
        } else if (key == "POSX") {
            data.posX = ParseFloat(value, data.posX, -kMaxCoordinate, kMaxCoordinate);
        } else if (key == "POSY") {
            data.posY = ParseFloat(value, data.posY, -kMaxCoordinate, kMaxCoordinate);
        } else if (key == "POSZ") {
            data.posZ = ParseFloat(value, data.posZ, -kMaxCoordinate, kMaxCoordinate);
        } else if (key == "YAW") {
            data.yaw = ParseFloat(value, data.yaw, -100000.0f, 100000.0f);
        } else if (key == "PITCH") {
            data.pitch = ParseFloat(value, data.pitch, -90.0f, 90.0f);
        } else if (key == "HEALTH") {
            data.healthFraction = ParseFloat(value, data.healthFraction, 0.0f, 1.0f);
        } else if (key == "STAMINA") {
            data.staminaFraction = ParseFloat(value, data.staminaFraction, 0.0f, 1.0f);
        } else if (key == "EXPLORED") {
            exploredToken = value;
        } else if (key == "DIARIESREAD") {
            diariesReadToken = value;
        } else if (key == "TORCHFUEL") {
            data.torchFuel = ParseFloat(value, data.torchFuel, 0.0f, 1.0f);
        } else if (key == "TORCHINV") {
            data.torchInventoryCount = ParseInt(value, data.torchInventoryCount, 0, 1024);
        } else if (key == "TORCHTAKEN") {
            torchTakenToken = value;
        } else if (key == "STONECOUNT") {
            data.stoneCount = ParseInt(value, data.stoneCount, 0, 999);
        } else if (key == "STONETAKEN") {
            stoneTakenToken = value;
        } else if (key == "NAME") {
            data.name = value.substr(0, (size_t)kNameMaxLen); // in case of a hand-edited or old file
        }
    }

    // SEED is the only field without which the file is unusable (the maze is reconstructed from
    // it); other fields fall back to defaults if missing (spawn in the starting safe zone, full
    // health/stamina).
    if (!sawSeed) return data;

    if (!exploredToken.empty())
        data.explored = DecodeExplored(exploredToken);

    // Unlike exploredToken above, an empty string here is not "field missing" but a legitimate "no
    // diaries read yet"; DecodeIntList("") already returns an empty vector, so no separate .empty()
    // check is needed.
    data.diariesReadIndices = DecodeIntList(diariesReadToken);
    data.torchTakenIndices = DecodeIntList(torchTakenToken);
    data.stoneTakenIndices = DecodeIntList(stoneTakenToken);

    data.valid = true;
    return data;
}

static bool WriteSlotFile(const fs::path& file, const SaveData& data) {
    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "SEED=" << data.seed << '\n';
    out << "POSX=" << data.posX << '\n';
    out << "POSY=" << data.posY << '\n';
    out << "POSZ=" << data.posZ << '\n';
    out << "YAW=" << data.yaw << '\n';
    out << "PITCH=" << data.pitch << '\n';
    out << "HEALTH=" << data.healthFraction << '\n';
    out << "STAMINA=" << data.staminaFraction << '\n';
    out << "NAME=" << data.name.substr(0, (size_t)kNameMaxLen) << '\n';
    out << "EXPLORED=" << EncodeExplored(data.explored) << '\n';
    out << "DIARIESREAD=" << EncodeIntList(data.diariesReadIndices) << '\n';
    out << "TORCHFUEL=" << data.torchFuel << '\n';
    out << "TORCHINV=" << data.torchInventoryCount << '\n';
    out << "TORCHTAKEN=" << EncodeIntList(data.torchTakenIndices) << '\n';
    out << "STONECOUNT=" << data.stoneCount << '\n';
    out << "STONETAKEN=" << EncodeIntList(data.stoneTakenIndices) << '\n';

    out.flush();
    return out.good();
}

bool SaveSlot(int slotIndex, const SaveData& data) {
    if (slotIndex < 0 || slotIndex >= kSlotCount) return false;

    const fs::path path = SlotPath(slotIndex);
    fs::path tmp = path;
    tmp += ".tmp";

    std::error_code ec;
    if (!WriteSlotFile(tmp, data)) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// Saves are written on a short-lived thread: a synchronous ofstream write on the game loop can
// block the whole frame for as long as the OS delays the file (antivirus, slow disk), which shows
// up as an FPS drop at every autosave. _beginthreadex is used instead of std::thread because some
// MinGW-w64 builds (win32 threading model) fail to link or throw on std::thread; it also
// initializes the per-thread CRT state that std::ofstream needs. One atomic<bool> per slot prevents
// a second concurrent write to the same file. The thread is not joined, so a process exit during a
// write can cut it short.
std::atomic<bool> g_slotSaveInFlight[kSlotCount] = {};

#ifdef _WIN32
struct AsyncSaveJob {
    int slotIndex;
    SaveData data;
};

unsigned __stdcall AsyncSaveThreadProc(void* param)
{
    std::unique_ptr<AsyncSaveJob> job(reinterpret_cast<AsyncSaveJob*>(param));
    SaveSlot(job->slotIndex, job->data);
    g_slotSaveInFlight[job->slotIndex].store(false);
    return 0;
}
#endif

void SaveSlotAsync(int slotIndex, const SaveData& data) {
    if (slotIndex < 0 || slotIndex >= kSlotCount) return;

    bool expected = false;
    if (!g_slotSaveInFlight[slotIndex].compare_exchange_strong(expected, true)) {
        // A previous write to this same slot has not finished yet (extremely unlikely with a 20 s
        // interval and a few KB per write, but possible on a very slow disk): skip this tick rather
        // than write over an unfinished write; the next autosave comes along as usual.
        return;
    }

#ifdef _WIN32
    // job transfers ownership (a unique_ptr inside AsyncSaveThreadProc): a copy of data goes on the
    // heap and the thread shares nothing with the caller (DungeonScene has already copied the
    // needed fields into SaveData, see saveActiveSlot()).
    auto* job = new AsyncSaveJob{ slotIndex, data };
    const uintptr_t h = _beginthreadex(nullptr, 0, AsyncSaveThreadProc, job, 0, nullptr);
    if (h == 0) {
        // Thread creation failed (extremely unlikely): do not lose the save, write synchronously
        // right here. A brief stutter beats a silently dropped autosave.
        delete job;
        SaveSlot(slotIndex, data);
        g_slotSaveInFlight[slotIndex].store(false);
    } else {
        CloseHandle(reinterpret_cast<HANDLE>(h));
    }
#else
    // Non-Windows (local engine debugging): synchronous, since this is not the platform any of this
    // is optimized for.
    SaveSlot(slotIndex, data);
    g_slotSaveInFlight[slotIndex].store(false);
#endif
}

int PickSlotForNewGame() {
    for (int i = 0; i < kSlotCount; ++i) {
        if (!SlotExists(i)) return i;
    }

    // All three slots are full: overwrite the one updated least recently (a simple rotation across
    // the last 3 games).
    int oldest = 0;
    fs::file_time_type oldestTime{};
    bool first = true;
    for (int i = 0; i < kSlotCount; ++i) {
        std::error_code ec;
        const fs::file_time_type t = fs::last_write_time(SlotPath(i), ec);
        if (ec) continue;
        if (first || t < oldestTime) {
            oldestTime = t;
            oldest = i;
            first = false;
        }
    }
    return oldest;
}

} // namespace SaveSystem
