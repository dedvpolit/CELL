#pragma once

#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

// ============================================================================
// AudioMixer — ОБЩИЙ на весь процесс пул из нескольких независимых
// "голосов" поверх WinMM (waveOut*), а не PlaySoundW.
//
// ПОЧЕМУ ЭТОТ ФАЙЛ ПОЯВИЛСЯ (баг из живого теста с 7 врагами, см.
// DungeonScene::kEnemyCount): PlaySoundW — это ОДИН системный "канал" на
// весь процесс (см. старые комментарии в FootstepAudio.h/EnemyAudio.h —
// ограничение было известно и заявлено как приемлемое для РЕДКИХ, КОРОТКИХ
// реплик ОДНОГО врага). Как только в игре оказалось 7 врагов + сам игрок,
// все они дёргают ОДИН и тот же общий канал PlaySoundW — с флагом
// SND_NOSTOP это значит, что пока играет чей-то один звук (особенно шаги
// с их 1.2-1.3с эхо-хвостом, см. README_AUDIO.txt), ЛЮБОЙ другой запрос —
// шаги другого врага, шаги игрока, рык, крик обнаружения — просто молча
// НЕ проигрывается. Ровно это и наблюдалось: "шаги только у одного",
// "крика иногда нет вообще" (сработал в момент, когда канал был занят
// чьими-то шагами).
//
// РЕШЕНИЕ — свой пул из kVoiceCount РЕАЛЬНО независимых `HWAVEOUT`
// (открыты один раз, не на каждый Play — открытие устройства не бесплатно
// по времени). Windows позволяет открыть НЕСКОЛЬКО waveOut-хендлов на
// устройство по умолчанию одновременно — микс делает сама ОС, честно и
// одновременно, без ограничения "один канал". Каждый Play() ищет
// свободный голос (или, если все заняты, "крадёт" самый старый по
// круговому счётчику, а не молча отбрасывает новый звук — лучше оборвать
// давно играющий эффект, чем не дать прозвучать новому крику/шагу).
//
// ДИНАМИЧЕСКАЯ ГРОМКОСТЬ ПО ДИСТАНЦИИ — ради этого явно принимается
// float volume (0..1) в play(), а не только имя файла: PlaySoundW вообще
// не даёт регулировать громкость отдельного проигрывания. Значит нужен
// СВОЙ, программный способ регулировать громкость каждого проигрывания
// НЕЗАВИСИМО от остальных — см. большой БАГФИКС ниже про то, почему это
// НЕ waveOutSetVolume.
//
// БАГФИКС ("когда игрок начинает ходить — громкость звука врага скачет
// вверх, хотя расстояние до игрока не менялось") — первая версия этого
// файла регулировала громкость через waveOutSetVolume(hwo, volume) на
// каждый голос отдельно, предполагая, что это per-handle настройка
// (так документировано формально). НА ПРАКТИКЕ на многих реальных
// Windows-драйверах (особенно через WAVE_MAPPER на обычных встроенных
// звуковых картах) waveOutSetVolume регулирует громкость всего
// устройства/аппаратного микшера ЦЕЛИКОМ, а не конкретного открытого
// хэндла — то есть вызов на ОДНОМ голосе (например, шаги игрока на
// полной громкости 1.0) реально задирал общую громкость устройства, и
// ЛЮБОЙ другой звук, УЖЕ игравший в этот момент тише (например, дальний
// враг) — резко становился громче вместе с ним, хотя его собственные
// параметры не менялись.
//
// РЕШЕНИЕ — громкость больше НЕ трогает устройство вообще: каждый
// play() масштабирует САМИ САМПЛЫ (PCM) под нужную громкость программно,
// в свой собственный, привязанный к конкретному голосу буфер (см. Voice::
// scaledBuffer ниже), и отдаёт WinMM уже готовый, отмасштабированный
// звук. Раз устройство никогда не получает команду "стань громче/тише"
// — громкость одного голоса физически не может повлиять на другой.
// Цена — по сравнению с прежним подходом (общий read-only буфер на все
// голоса) каждый Play() один раз проходит по своим сэмплам и умножает
// их (для файла в пару секунд на 48kHz это доли миллисекунды, незаметно
// даже при частых шагах).
//
// ФОРМАТ — фиксированный: моно/16-бит/48000 Гц. Это ровно формат ВСЕХ
// текущих ассетов (assets/audio/footsteps и assets/audio/enemy, см.
// README_AUDIO.txt) — голоса открываются с этим форматом один раз при
// init(), а не пересоздаются под формат каждого файла. WAV-файлы иного
// формата (другая частота/битность/стерео) на лету конвертируются в этот
// же формат при первой загрузке (см. LoadAndConvertWav()) — так что
// новый ассет "неправильного" формата не сломает микшер, просто получит
// небольшую (обычно неразличимую на слух) деградацию качества от
// ресемплинга.
//
// КЭШ PCM — декодированные сэмплы каждого файла держатся в памяти на всё
// время жизни процесса (по пути к файлу как ключ) — иначе каждый шаг
// каждого из 7 врагов заново читал бы и парсил WAV с диска, десятки раз
// в секунду. Данные разделяются МЕЖДУ голосами (WAVEHDR::lpData
// указывает на общий, доступный только для чтения буфер) — это безопасно,
// т.к. WinMM только читает из буфера во время проигрывания, не пишет.
//
// На не-Windows платформах — как и везде в audio/ — безопасный no-op:
// isAvailable()==false, play() ничего не делает, движок собирается без
// платформенной звуковой зависимости.
// ============================================================================
class AudioMixer {
public:
    static AudioMixer& instance()
    {
        static AudioMixer mixer;
        return mixer;
    }

    // Идемпотентно — вызывается из каждого EnemyAudio::init() (по одному
    // на врага, см. EnemyAI::init()) и из FootstepAudio::init() (игрок).
    // Реально открывает голоса только один раз; повторные вызовы, пока
    // уже доступен, сразу возвращают true.
    bool init()
    {
#ifdef _WIN32
        if (m_available)
            return true;

        HMODULE winmm = LoadLibraryW(L"winmm.dll");
        if (!winmm)
            return false;

        m_waveOutOpen = reinterpret_cast<WaveOutOpenFn>(GetProcAddress(winmm, "waveOutOpen"));
        m_waveOutClose = reinterpret_cast<WaveOutCloseFn>(GetProcAddress(winmm, "waveOutClose"));
        m_waveOutPrepareHeader = reinterpret_cast<WaveOutPrepareHeaderFn>(GetProcAddress(winmm, "waveOutPrepareHeader"));
        m_waveOutUnprepareHeader = reinterpret_cast<WaveOutUnprepareHeaderFn>(GetProcAddress(winmm, "waveOutUnprepareHeader"));
        m_waveOutWrite = reinterpret_cast<WaveOutWriteFn>(GetProcAddress(winmm, "waveOutWrite"));
        m_waveOutReset = reinterpret_cast<WaveOutResetFn>(GetProcAddress(winmm, "waveOutReset"));

        if (!m_waveOutOpen || !m_waveOutClose || !m_waveOutPrepareHeader ||
            !m_waveOutUnprepareHeader || !m_waveOutWrite || !m_waveOutReset) {
            FreeLibrary(winmm);
            return false;
        }

        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = kSampleRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = (fmt.nChannels * fmt.wBitsPerSample) / 8;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        fmt.cbSize = 0;

        int openedCount = 0;
        for (auto& v : m_voices) {
            v = Voice{};
            const MMRESULT r = m_waveOutOpen(&v.handle, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
            v.opened = (r == MMSYSERR_NOERROR);
            if (v.opened)
                ++openedCount;
        }

        if (openedCount == 0) {
            FreeLibrary(winmm);
            return false;
        }

        m_winmm = winmm;
        m_available = true;
        return true;
#else
        return false;
#endif
    }

    void shutdown()
    {
#ifdef _WIN32
        for (auto& v : m_voices) {
            if (v.opened) {
                m_waveOutReset(v.handle);
                if (v.busy)
                    m_waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
                m_waveOutClose(v.handle);
            }
            v = Voice{};
        }

        if (m_winmm)
            FreeLibrary(m_winmm);
        m_winmm = nullptr;
        m_available = false;
        // Кэш PCM НЕ чистим — данные маленькие, а следующий init() (новый
        // уровень) избежит повторного чтения/парсинга с диска.
#endif
    }

    // absolutePath — уже разрешённый путь к файлу (см. locateAsset() в
    // FootstepAudio.h/EnemyAudio.h — они по-прежнему сами ищут файл
    // относительно .exe/cwd, сюда передаётся готовый результат).
    // volume — 0..1, вычисляется СНАРУЖИ (обычно по дистанции до
    // слушателя, см. EnemyAI.cpp) — микшер сам ничего не знает ни о
    // позициях, ни о камере.
    // priority — см. большой комментарий класса выше про "некоторые
    // звуки не проигрываются": используется ТОЛЬКО когда все голоса
    // заняты (см. reclaimFinishedVoices()/поиск свободного голоса ниже)
    // — тогда вытесняется занятый голос с НАИМЕНЬШИМ приоритетом (при
    // равенстве — начатый раньше остальных), а не случайный/по кругу.
    // Так частые малозначимые звуки (шаги) уступают место редким важным
    // (крик обнаружения, удар о стену), а не наоборот. Если приоритет
    // НОВОГО звука ниже, чем у ВСЕГО, что сейчас играет (пул забит
    // важными звуками) — жертвуем новым, не обрываем что-то важное ради
    // ещё одного шага. Условные уровни (см. вызывающий код): 0 —
    // частые/фоновые (шаги врага), 1 — редкие атмосферные (шаги игрока,
    // стон), 2 — важные разовые события (крик обнаружения, удар о
    // стену).
    void play(const std::wstring& absolutePath, float volume, int priority)
    {
#ifdef _WIN32
        if (!m_available || absolutePath.empty())
            return;

        if (volume < 0.0f) volume = 0.0f;
        if (volume > 1.0f) volume = 1.0f;
        // Ниже порога слышимости — не тратим голос на звук, который всё
        // равно никто не услышит (см. дистанционное затухание в
        // EnemyAI.cpp — там своя "дальняя" граница, это отдельный,
        // более низкий технический порог на случай крошечных отличных
        // от нуля значений).
        if (volume <= 0.002f)
            return;

        const std::shared_ptr<std::vector<int16_t>> pcm = getOrLoadPcm(absolutePath);
        if (!pcm || pcm->empty())
            return;

        reclaimFinishedVoices();

        int chosen = -1;
        for (int i = 0; i < (int)m_voices.size(); ++i) {
            if (m_voices[i].opened && !m_voices[i].busy) {
                chosen = i;
                break;
            }
        }

        if (chosen < 0) {
            // Все голоса заняты — ищем среди занятых наименее важный
            // (приоритет, при равенстве — самый старый по startOrder),
            // а не первый попавшийся по кругу (см. большой комментарий
            // у параметра priority выше — именно это раньше приводило
            // к тому, что случайные звуки, включая важные, терялись
            // одинаково часто, как и шаги).
            int worstIdx = -1;
            int worstPriority = INT_MAX;
            std::uint64_t worstOrder = UINT64_MAX;
            for (int i = 0; i < (int)m_voices.size(); ++i) {
                const Voice& vv = m_voices[i];
                if (!vv.opened || !vv.busy)
                    continue;
                if (vv.priority < worstPriority ||
                    (vv.priority == worstPriority && vv.startOrder < worstOrder)) {
                    worstPriority = vv.priority;
                    worstOrder = vv.startOrder;
                    worstIdx = i;
                }
            }

            if (worstIdx < 0)
                return; // ни один голос вообще не открылся — не должно происходить

            if (priority < worstPriority)
                return; // новый звук менее важен, чем вообще всё, что сейчас играет — жертвуем им, не обрываем важное

            chosen = worstIdx;
            Voice& stolen = m_voices[chosen];
            m_waveOutReset(stolen.handle);
            if (stolen.busy)
                m_waveOutUnprepareHeader(stolen.handle, &stolen.header, sizeof(WAVEHDR));
            stolen.busy = false;
        }

        Voice& v = m_voices[chosen];
        if (!v.opened)
            return;

        // Громкость — программно, в СВОЙ буфер этого голоса (см. большой
        // БАГФИКС-комментарий класса выше про то, почему НЕ через
        // waveOutSetVolume). pcm — общий, доступный только для чтения
        // оригинал; v.scaledBuffer принадлежит конкретно этому голосу и
        // безопасно перезаписывается здесь же — WinMM в этот момент уже
        // точно не читает из него (голос либо был свободен, либо мы
        // только что сами его остановили выше через waveOutReset).
        v.scaledBuffer.resize(pcm->size());
        for (size_t i = 0; i < pcm->size(); ++i) {
            float sample = (float)(*pcm)[i] * volume;
            if (sample > 32767.0f) sample = 32767.0f;
            if (sample < -32768.0f) sample = -32768.0f;
            v.scaledBuffer[i] = (int16_t)sample;
        }

        v.header = WAVEHDR{};
        v.header.lpData = reinterpret_cast<LPSTR>(v.scaledBuffer.data());
        v.header.dwBufferLength = (DWORD)(v.scaledBuffer.size() * sizeof(int16_t));

        if (m_waveOutPrepareHeader(v.handle, &v.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
            return;

        if (m_waveOutWrite(v.handle, &v.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            m_waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
            return;
        }

        v.priority = priority;
        v.startOrder = m_playCounter++;
        v.busy = true;
#else
        (void)absolutePath;
        (void)volume;
        (void)priority;
#endif
    }

    bool isAvailable() const { return m_available; }

private:
    AudioMixer() = default;
    ~AudioMixer() = default;
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    static constexpr int kVoiceCount = 24; // с запасом: 7 врагов × шаги с длинным эхо-хвостом друг поверх друга + стоны/крики + игрок
    static constexpr unsigned long kSampleRate = 48000;

#ifdef _WIN32
    struct Voice {
        HWAVEOUT handle = nullptr;
        WAVEHDR header{};
        bool opened = false;
        bool busy = false;
        int priority = 0;
        std::uint64_t startOrder = 0;
        // Собственный, отмасштабированный под конкретную громкость этого
        // проигрывания буфер (см. большой БАГФИКС-комментарий класса
        // выше) — НЕ общий с кэшем m_pcmCache.
        std::vector<int16_t> scaledBuffer;
    };

    // БАГФИКС ("игра стала есть на 15-20 МБ ОЗУ больше, чем до аудио-
    // системы") — resize() у std::vector НИКОГДА не освобождает уже
    // выделенную память при уменьшении размера, только растит capacity.
    // Голоса переиспользуются под РАЗНЫЕ файлы (маленький шаг сейчас,
    // большой стон через минуту, снова маленький шаг после) — без явного
    // освобождения буфер каждого голоса рос бы до размера САМОГО
    // большого файла, который он хоть раз проиграл (moan_3.wav — 550КБ),
    // и оставался бы таким навсегда, даже играя дальше только шаги.
    // При 24 голосах, каждый из которых со временем нахватывает хотя бы
    // один стон, этореально ~24×500КБ ≈ 12МБ мёртвого груза. Здесь, как
    // только голос НАВЕРНЯКА закончил играть (и какое-то время будет
    // простаивать — до следующего Play() в него), сразу отдаём память
    // обратно: пиковое потребление теперь отслеживает РЕАЛЬНО играющие
    // прямо сейчас звуки, а не исторический максимум по каждому голосу.
    void reclaimFinishedVoices()
    {
        for (auto& v : m_voices) {
            if (v.opened && v.busy && (v.header.dwFlags & WHDR_DONE)) {
                m_waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
                v.busy = false;
                v.scaledBuffer.clear();
                v.scaledBuffer.shrink_to_fit();
            }
        }
    }

    // Читает WAV-файл (канонический PCM RIFF/WAVE, 8 или 16 бит, моно
    // или стерео, любая частота) и конвертирует в формат голосов
    // (моно/16-бит/48000) — те же приёмы (усреднение каналов, линейная
    // интерполяция при ресемплинге), что использовались при подготовке
    // самих ассетов (см. README_AUDIO.txt), только теперь встроены в
    // движок на случай, если когда-нибудь добавится файл другого формата.
    static bool LoadAndConvertWav(const std::wstring& path, std::vector<int16_t>& outSamples)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file)
            return false;

        char riffHeader[12];
        file.read(riffHeader, 12);
        if (!file || std::strncmp(riffHeader, "RIFF", 4) != 0 || std::strncmp(riffHeader + 8, "WAVE", 4) != 0)
            return false;

        WORD channels = 0, bitsPerSample = 0;
        DWORD sampleRate = 0;
        std::vector<uint8_t> dataChunk;
        bool haveFmt = false;

        while (file) {
            char chunkId[4];
            uint32_t chunkSize = 0;
            file.read(chunkId, 4);
            file.read(reinterpret_cast<char*>(&chunkSize), 4);
            if (!file)
                break;

            if (std::strncmp(chunkId, "fmt ", 4) == 0) {
                std::vector<uint8_t> fmtBuf(chunkSize);
                file.read(reinterpret_cast<char*>(fmtBuf.data()), chunkSize);
                if (fmtBuf.size() >= 16) {
                    channels = *reinterpret_cast<uint16_t*>(&fmtBuf[2]);
                    sampleRate = *reinterpret_cast<uint32_t*>(&fmtBuf[4]);
                    bitsPerSample = *reinterpret_cast<uint16_t*>(&fmtBuf[14]);
                    haveFmt = true;
                }
                if (chunkSize % 2 != 0)
                    file.seekg(1, std::ios::cur);
            } else if (std::strncmp(chunkId, "data", 4) == 0) {
                dataChunk.resize(chunkSize);
                file.read(reinterpret_cast<char*>(dataChunk.data()), chunkSize);
                if (chunkSize % 2 != 0)
                    file.seekg(1, std::ios::cur);
            } else {
                file.seekg(chunkSize + (chunkSize % 2), std::ios::cur);
            }
        }

        if (!haveFmt || dataChunk.empty() || channels == 0 || sampleRate == 0)
            return false;
        if (bitsPerSample != 8 && bitsPerSample != 16)
            return false; // 24/32-бит редкие ассеты уже сконвертированы заранее в pipeline подготовки

        const size_t bytesPerSample = bitsPerSample / 8;
        const size_t frameCount = dataChunk.size() / (bytesPerSample * channels);

        std::vector<float> mono(frameCount);
        for (size_t i = 0; i < frameCount; ++i) {
            float sum = 0.0f;
            for (WORD c = 0; c < channels; ++c) {
                const size_t offset = (i * channels + c) * bytesPerSample;
                float sample;
                if (bitsPerSample == 16) {
                    const int16_t s = *reinterpret_cast<const int16_t*>(&dataChunk[offset]);
                    sample = s / 32768.0f;
                } else {
                    const uint8_t s = dataChunk[offset];
                    sample = (s - 128) / 128.0f;
                }
                sum += sample;
            }
            mono[i] = sum / (float)channels;
        }

        if (sampleRate == kSampleRate) {
            outSamples.resize(mono.size());
            for (size_t i = 0; i < mono.size(); ++i) {
                float v = mono[i];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                outSamples[i] = (int16_t)(v * 32767.0f);
            }
            return true;
        }

        // Линейная интерполяция под фиксированную частоту голосов —
        // достаточно для коротких игровых эффектов (не музыка).
        const size_t outCount = (size_t)((double)mono.size() * (double)kSampleRate / (double)sampleRate);
        outSamples.resize(outCount);
        for (size_t i = 0; i < outCount; ++i) {
            const double srcPos = (double)i * (double)sampleRate / (double)kSampleRate;
            const size_t i0 = (size_t)srcPos;
            const size_t i1 = (i0 + 1 < mono.size()) ? i0 + 1 : i0;
            const float frac = (float)(srcPos - (double)i0);
            float v = mono[i0] * (1.0f - frac) + mono[i1] * frac;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            outSamples[i] = (int16_t)(v * 32767.0f);
        }
        return true;
    }

    std::shared_ptr<std::vector<int16_t>> getOrLoadPcm(const std::wstring& path)
    {
        const auto it = m_pcmCache.find(path);
        if (it != m_pcmCache.end())
            return it->second; // может быть nullptr — файл уже пытались загрузить и не смогли, не повторяем попытку каждый Play()

        auto samples = std::make_shared<std::vector<int16_t>>();
        if (!LoadAndConvertWav(path, *samples) || samples->empty())
            samples.reset();

        m_pcmCache[path] = samples;
        return samples;
    }

    using WaveOutOpenFn = MMRESULT(WINAPI*)(LPHWAVEOUT, UINT_PTR, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    using WaveOutCloseFn = MMRESULT(WINAPI*)(HWAVEOUT);
    using WaveOutPrepareHeaderFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
    using WaveOutUnprepareHeaderFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
    using WaveOutWriteFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
    using WaveOutResetFn = MMRESULT(WINAPI*)(HWAVEOUT);

    bool m_available = false;
    HMODULE m_winmm = nullptr;
    WaveOutOpenFn m_waveOutOpen = nullptr;
    WaveOutCloseFn m_waveOutClose = nullptr;
    WaveOutPrepareHeaderFn m_waveOutPrepareHeader = nullptr;
    WaveOutUnprepareHeaderFn m_waveOutUnprepareHeader = nullptr;
    WaveOutWriteFn m_waveOutWrite = nullptr;
    WaveOutResetFn m_waveOutReset = nullptr;

    std::array<Voice, kVoiceCount> m_voices;
    std::uint64_t m_playCounter = 0;
    std::unordered_map<std::wstring, std::shared_ptr<std::vector<int16_t>>> m_pcmCache;
#else
    bool m_available = false;
#endif
};
