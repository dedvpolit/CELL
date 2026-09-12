#include "SaveSystem.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <atomic>
#include <memory>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <process.h> // _beginthreadex — см. большой комментарий у SaveSlotAsync()
#endif

namespace SaveSystem {

namespace {

namespace fs = std::filesystem;

// Папка "saves" рядом с .exe (Windows) либо рядом с рабочей директорией
// (запуск не под Windows/из-под IDE) — тот же основной способ найти папку
// исполняемого файла, что и AssetPath::Resolve(), но здесь для ЗАПИСИ, а
// не поиска существующего файла, поэтому папка создаётся сама при первом
// обращении (AssetPath.cpp этого не делает — ему запись не нужна).
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
    fs::create_directories(savesDir, ec); // тихо игнорируем ошибку — SaveSlot() ниже сам проверит, открылся ли файл
    return savesDir;
}

fs::path SlotPath(int slotIndex) {
    return GetSavesDir() / ("slot" + std::to_string(slotIndex + 1) + ".sav");
}

// Туман войны — это массив из 0/1 длиной mapW*mapH (16384 при 128x128),
// почти всегда с длинными однородными пробегами (в начале игры — почти
// весь "0", т.е. не раскрыто) — простой RLE ("значение:длина" через
// запятую) в разы компактнее и всё ещё человекочитаем, в отличие от
// сырых 16 тысяч цифр подряд.
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
            for (long k = 0; k < count; ++k)
                out.push_back((unsigned char)v);
        }

        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

} // namespace

// Простой список чисел через запятую ("0,3,7") — компактнее RLE для
// коротких списков вроде diariesReadIndices (максимум 12 элементов, в
// отличие от explored, там счёт на тысячи и RLE оправдан).
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
        if (!numStr.empty()) out.push_back(std::atoi(numStr.c_str()));
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
    if (!in.is_open()) return data; // файла нет -> valid остаётся false

    bool sawSeed = false;
    std::string exploredToken;
    std::string diariesReadToken;
    std::string line;
    while (std::getline(in, line)) {
        // На всякий случай убираем возможный '\r' (файл мог быть
        // сохранён/скопирован под Windows и прочитан построчно иначе).
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
            data.posX = std::strtof(value.c_str(), nullptr);
        } else if (key == "POSY") {
            data.posY = std::strtof(value.c_str(), nullptr);
        } else if (key == "POSZ") {
            data.posZ = std::strtof(value.c_str(), nullptr);
        } else if (key == "YAW") {
            data.yaw = std::strtof(value.c_str(), nullptr);
        } else if (key == "PITCH") {
            data.pitch = std::strtof(value.c_str(), nullptr);
        } else if (key == "HEALTH") {
            data.healthFraction = std::strtof(value.c_str(), nullptr);
        } else if (key == "STAMINA") {
            data.staminaFraction = std::strtof(value.c_str(), nullptr);
        } else if (key == "EXPLORED") {
            exploredToken = value;
        } else if (key == "DIARIESREAD") {
            diariesReadToken = value;
        } else if (key == "NAME") {
            data.name = value.substr(0, (size_t)kNameMaxLen); // на случай руками испорченного/старого файла
        }
    }

    // SEED — единственное поле, без которого файл считается непригодным
    // (сам лабиринт восстанавливается только через него) — остальные
    // поля при отсутствии просто остаются значениями по умолчанию
    // (спавн стартовой safe-zone, полное здоровье/стамина).
    if (!sawSeed) return data;

    if (!exploredToken.empty())
        data.explored = DecodeExplored(exploredToken);

    // В отличие от exploredToken выше, пустая строка здесь — не "поле
    // отсутствовало", а легитимное "ни один дневник ещё не прочитан";
    // DecodeIntList("") и так возвращает пустой vector, отдельная
    // проверка на .empty() не нужна.
    data.diariesReadIndices = DecodeIntList(diariesReadToken);

    data.valid = true;
    return data;
}

bool SaveSlot(int slotIndex, const SaveData& data) {
    if (slotIndex < 0 || slotIndex >= kSlotCount) return false;

    std::ofstream out(SlotPath(slotIndex), std::ios::trunc);
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

    return out.good();
}

// БАГФИКС ("игра на несколько секунд подвисает каждые ~20 секунд — те же
// самые кадры по 20-45мс подряд видны в perf-логе, независимо от того,
// сколько на экране треугольников/факелов") — SaveSlot() выше — это
// синхронный std::ofstream ПРЯМО на вызывающем потоке, а вызывался он
// ПРЯМО из основного игрового цикла (см. DungeonScene::saveActiveSlot(),
// единственное место в движке, которое пишет сохранения — периодический
// автосейв каждые kAutosaveIntervalSeconds=20с, автосейв на NEW GAME,
// автосейв при выходе в меню, и ручной SAVE из паузы — всё идёт через
// неё). Любая ОС-задержка на запись файла — антивирус, сканирующий налету
// новый/изменённый файл рядом с ещё не подписанным .exe (частый случай
// именно на dev-сборках, см. лог: путь вида "...\bin\Debug\..."),
// медленный диск, индексация — блокирует ВЕСЬ игровой цикл (рендер,
// ввод, звук) на всё время этой задержки: ощущается как случайная
// просадка FPS без видимой причины, ровно с той периодичностью, что и
// автосейв.
//
// НЕ std::thread — этот файл (и весь движок) больше нигде не использует
// std::thread/<thread>, и намеренно: у части сборок MinGW-w64 (threading
// model "win32", а не "posix") std::thread либо не линкуется вообще,
// либо падает в рантайме с std::system_error при первом же запуске
// потока — тот же класс риска, которого этот проект уже избегает в
// audio/ProcessMemory.h, разрешая нужные WinAPI-функции динамически
// вместо линковки против них напрямую. _beginthreadex — обычный
// CRT/WinAPI примитив, доступный в любой сборке MinGW без специальных
// флагов линковки, и корректно (в отличие от голого CreateThread)
// инициализирует per-thread состояние CRT — важно, т.к. тело потока
// ниже использует std::ofstream (см. SaveSlot()).
//
// std::atomic<bool> на слот (не мьютекс/очередь) — потому что нужно
// только "не начинать вторую запись в тот же файл, пока первая ещё не
// закончилась" (что на практике почти невозможно: запись занимает
// миллисекунды, следующий автосейв — через 20 секунд), а не честная
// сериализация произвольного числа отложенных записей; примитивы
// std::atomic для простых типов — это компиляторные интринсики, они не
// зависят от threading model libstdc++ так, как std::thread/std::mutex.
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
        // Предыдущая запись в ЭТОТ ЖЕ слот ещё не закончилась (экстремально
        // маловероятно при интервале в 20с и записи в несколько КБ, но на
        // случай очень медленного диска) — просто пропускаем этот тик,
        // ничего не портим повторной записью поверх незавершённой; следующий
        // автосейв придёт как обычно.
        return;
    }

#ifdef _WIN32
    // job передаёт владение (unique_ptr внутри AsyncSaveThreadProc) —
    // копия data уходит в кучу, поток ничего не разделяет с вызывающим
    // кодом (DungeonScene к этому моменту уже полностью выгрузила нужные
    // поля в SaveData, см. saveActiveSlot()).
    auto* job = new AsyncSaveJob{ slotIndex, data };
    const uintptr_t h = _beginthreadex(nullptr, 0, AsyncSaveThreadProc, job, 0, nullptr);
    if (h == 0) {
        // Не удалось создать поток (крайне маловероятно) — не теряем
        // сохранение, просто пишем синхронно здесь же: короткая просадка
        // сейчас лучше, чем полностью пропавший автосейв.
        delete job;
        SaveSlot(slotIndex, data);
        g_slotSaveInFlight[slotIndex].store(false);
    } else {
        CloseHandle(reinterpret_cast<HANDLE>(h)); // не ждём завершения — только закрываем хэндл потока, сам поток продолжает работать
    }
#else
    // Не-Windows (отладка движка локально) — синхронно, как и раньше;
    // это не та платформа, под которую здесь вообще что-то оптимизируется.
    SaveSlot(slotIndex, data);
    g_slotSaveInFlight[slotIndex].store(false);
#endif
}

int PickSlotForNewGame() {
    for (int i = 0; i < kSlotCount; ++i) {
        if (!SlotExists(i)) return i;
    }

    // Все три слота заняты — перезаписываем тот, что дольше всего не
    // обновлялся (простая ротация истории из последних 3 игр).
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
