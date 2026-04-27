// LibretroCore.cpp — libretro .dll/.so 동적 로딩 및 콜백

#include "LibretroCore.h"
#include "EmulatorState.h"

#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QStringList>
#include <cstdarg>
#include <cstring>
#include "NetplayManager.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

// ── 전역 코어 포인터 (정적 콜백에서 인스턴스 접근용) ──────
static LibretroCore* g_core = nullptr;

// ── 생성자/소멸자 ─────────────────────────────────────────
LibretroCore::LibretroCore(QObject* parent)
    : QObject(parent)
{
    g_core = this;
}

LibretroCore::~LibretroCore() {
    unloadGame();
    unload();
    if (g_core == this) g_core = nullptr;
}

// ── 라이브러리 로딩 ───────────────────────────────────────
void* LibretroCore::getProcAddress(const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
    return dlsym(m_handle, name);
#endif
}

bool LibretroCore::loadAllProcs() {
#define LOAD(fn) \
    m_##fn = reinterpret_cast<fn##_t>(getProcAddress(#fn)); \
    if (!m_##fn) { emit logMessage(QString("Missing: ") + #fn); return false; }

    LOAD(retro_init)
    LOAD(retro_deinit)
    LOAD(retro_api_version)
    LOAD(retro_get_system_info)
    LOAD(retro_get_system_av_info)
    LOAD(retro_load_game)
    LOAD(retro_unload_game)
    LOAD(retro_run)
    LOAD(retro_serialize_size)
    LOAD(retro_serialize)
    LOAD(retro_unserialize)
    LOAD(retro_reset)
    LOAD(retro_set_environment)
    LOAD(retro_set_video_refresh)
    LOAD(retro_set_audio_sample)
    LOAD(retro_set_audio_sample_batch)
    LOAD(retro_set_input_poll)
    LOAD(retro_set_input_state)
#undef LOAD

    // 선택적 (없어도 동작)
    m_retro_cheat_reset = reinterpret_cast<retro_cheat_reset_t>(getProcAddress("retro_cheat_reset"));
    m_retro_cheat_set   = reinterpret_cast<retro_cheat_set_t>(getProcAddress("retro_cheat_set"));
    m_retro_get_memory_data = reinterpret_cast<retro_get_memory_data_t>(
        getProcAddress("retro_get_memory_data"));
    m_retro_get_memory_size = reinterpret_cast<retro_get_memory_size_t>(
        getProcAddress("retro_get_memory_size"));

    return true;
}

bool LibretroCore::load(const QString& libPath) {
    if (m_loaded) unload();

#ifdef _WIN32
    m_handle = LoadLibraryW(libPath.toStdWString().c_str());
    if (!m_handle) {
        emit logMessage(QString("LoadLibrary 실패: %1 (오류: %2)")
                        .arg(libPath).arg(GetLastError()));
        return false;
    }
#else
    m_handle = dlopen(libPath.toUtf8().constData(), RTLD_LAZY);
    if (!m_handle) {
        emit logMessage(QString("dlopen 실패: %1 (%2)")
                        .arg(libPath).arg(dlerror()));
        return false;
    }
#endif

    if (!loadAllProcs()) {
        unload();
        return false;
    }

    // 콜백 등록
    m_retro_set_environment(environmentCb);
    m_retro_set_video_refresh(videoRefreshCb);
    m_retro_set_audio_sample(audioSampleCb);
    m_retro_set_audio_sample_batch(audioSampleBatchCb);
    m_retro_set_input_poll(inputPollCb);
    m_retro_set_input_state(inputStateCb);

    // API 버전 확인
    unsigned api = m_retro_api_version();
    if (api != RETRO_API_VERSION) {
        emit logMessage(QString("경고: API 버전 불일치 (코어=%1, 기대=%2)")
                        .arg(api).arg(RETRO_API_VERSION));
    }

    // 시스템 정보
    m_retro_get_system_info(&m_sysInfo);

    // 코어 초기화
    m_retro_init();

    m_loaded = true;
    emit logMessage(QString("코어 로드: %1 %2")
                    .arg(m_sysInfo.library_name)
                    .arg(m_sysInfo.library_version));
    return true;
}

void LibretroCore::unload() {
    if (!m_handle) return;
    if (m_loaded && m_retro_deinit) m_retro_deinit();
    m_loaded = false;

#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(m_handle));
#else
    dlclose(m_handle);
#endif
    m_handle = nullptr;
    std::memset(&m_sysInfo, 0, sizeof(m_sysInfo));
    std::memset(&m_avInfo,  0, sizeof(m_avInfo));
}

// ── 게임 로딩 ─────────────────────────────────────────────
bool LibretroCore::loadGame(const QString& romPath) {
    if (!m_loaded) return false;
    if (m_gameLoaded) unloadGame();

    // ROM 파일 읽기
    QFile f(romPath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit logMessage("ROM 파일 열기 실패: " + romPath);
        return false;
    }
    QByteArray romData = f.readAll();
    f.close();

    // need_fullpath 코어: 경로만 전달
    retro_game_info info{};
    QByteArray pathBa = romPath.toUtf8();
    info.path = pathBa.constData();
    if (!m_sysInfo.need_fullpath) {
        info.data = romData.constData();
        info.size = static_cast<size_t>(romData.size());
    }

    if (!m_retro_load_game(&info)) {
        emit logMessage("retro_load_game 실패: " + romPath);
        return false;
    }

    // AV 정보
    m_retro_get_system_av_info(&m_avInfo);
    m_fps = m_avInfo.timing.fps > 0 ? m_avInfo.timing.fps : 60.0;
    gState.coreFps        = m_fps;
    gState.coreSampleRate = m_avInfo.timing.sample_rate > 0
                            ? m_avInfo.timing.sample_rate : 44100.0;

    m_gameLoaded = true;
    gState.gameLoaded = true;
    emit logMessage(QString("ROM 로드: %1 (FPS=%2)")
                    .arg(romPath).arg(m_fps, 0, 'f', 2));
    return true;
}

void LibretroCore::unloadGame() {
    if (!m_gameLoaded || !m_retro_unload_game) return;
    m_retro_unload_game();
    m_gameLoaded = false;
    gState.gameLoaded = false;
}

// ── 에뮬레이션 ────────────────────────────────────────────
void LibretroCore::run() {
    if (m_retro_run) m_retro_run();
}

void LibretroCore::reset() {
    if (m_retro_reset) m_retro_reset();
}

// ── 세이브스테이트 ───────────────────────────────────────
size_t LibretroCore::serializeSize() {
    return m_retro_serialize_size ? m_retro_serialize_size() : 0;
}

bool LibretroCore::serialize(void* data, size_t size) {
    return m_retro_serialize ? m_retro_serialize(data, size) : false;
}

bool LibretroCore::unserialize(const void* data, size_t size) {
    return m_retro_unserialize ? m_retro_unserialize(data, size) : false;
}

// ── 치트 ─────────────────────────────────────────────────
void LibretroCore::cheatReset() {
    if (m_retro_cheat_reset) m_retro_cheat_reset();
}

void LibretroCore::cheatSet(unsigned index, bool enabled, const QString& code) {
    if (m_retro_cheat_set)
        m_retro_cheat_set(index, enabled, code.toUtf8().constData());
}

void* LibretroCore::getMemoryData(unsigned memId) {
    return m_retro_get_memory_data ? m_retro_get_memory_data(memId) : nullptr;
}
size_t LibretroCore::getMemorySize(unsigned memId) {
    return m_retro_get_memory_size ? m_retro_get_memory_size(memId) : 0;
}

// ── 경로 설정 ────────────────────────────────────────────
void LibretroCore::setSystemDir(const QString& dir) {
    m_systemDirBa = dir.toUtf8();
    gState.systemDir = m_systemDirBa;
}

void LibretroCore::setSaveDir(const QString& dir) {
    m_saveDirBa = dir.toUtf8();
    gState.saveDir = m_saveDirBa;
}

// ════════════════════════════════════════════════════════════
//  정적 콜백 구현
// ════════════════════════════════════════════════════════════

bool LibretroCore::environmentCb(unsigned cmd, void* data) {
    if (!g_core) return false;

    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        auto fmt = static_cast<retro_pixel_format*>(data);
        g_core->m_pixelFormat = *fmt;
        gState.pixelFormat = *fmt;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = gState.systemDir.constData();
        return !gState.systemDir.isEmpty();

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = gState.saveDir.constData();
        return !gState.saveDir.isEmpty();

    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        auto cb = static_cast<retro_log_callback*>(data);
        cb->log = logCb;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto var = static_cast<retro_variable*>(data);
        if (!var->key) return false;
        QString key = QString::fromUtf8(var->key);
        if (gState.variables.contains(key)) {
            // static buffer — 생명 주기 주의 (Phase4에서 개선)
            static QHash<QString, QByteArray> valCache;
            valCache[key] = gState.variables[key].toUtf8();
            var->value = valCache[key].constData();
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        auto vars = static_cast<const retro_variable*>(data);
        while (vars && vars->key) {
            QString key = QString::fromUtf8(vars->key);
            QString raw = QString::fromUtf8(vars->value ? vars->value : "");
            // 포맷: "설명 텍스트; opt1|opt2|opt3"
            int semi = raw.indexOf(';');
            QString desc = (semi >= 0) ? raw.left(semi).trimmed() : key;
            QString opts = (semi >= 0) ? raw.mid(semi + 2).trimmed() : raw;
            QStringList choices = opts.split('|');
            for (QString& c : choices) c = c.trimmed();
            choices.removeAll({});

            gState.variableDescriptions[key] = desc;
            gState.variableOptions[key]       = choices;
            // 첫 번째 옵션이 libretro 기본값
            if (!gState.variables.contains(key) && !choices.isEmpty())
                gState.variables[key] = choices.first();
            ++vars;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
        *static_cast<bool*>(data) = gState.variablesUpdated.exchange(false);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        auto geom = static_cast<const retro_game_geometry*>(data);
        if (g_core->m_gameLoaded) {
            g_core->m_avInfo.geometry = *geom;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return false;  // 비트마스크 미지원 — false 반환으로 id-based 쿼리 유지

    default:
        return false;
    }
}

void LibretroCore::videoRefreshCb(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;

    // bpp는 현재 pitch 기반 sz 계산에 불필요 (pitch가 이미 바이트 단위)
    Q_UNUSED(w)
    size_t sz = static_cast<size_t>(h) * pitch;
    if (static_cast<size_t>(gState.videoBuffer.size()) != sz)
        gState.videoBuffer.resize(static_cast<int>(sz));
    std::memcpy(gState.videoBuffer.data(), data, sz);

    gState.videoWidth  = w;
    gState.videoHeight = h;
    gState.videoPitch  = pitch;
    gState.frameReady.store(true, std::memory_order_release);
}

void LibretroCore::audioSampleCb(int16_t left, int16_t right) {
    if (gState.netplayResim) return;
    int16_t buf[2] = {left, right};
    gState.audioPending.append(reinterpret_cast<const char*>(buf), 4);
    if (gState.isRecording)
        gState.audioRecBuf.append(reinterpret_cast<const char*>(buf), 4);
}

size_t LibretroCore::audioSampleBatchCb(const int16_t* data, size_t frames) {
    if (gState.netplayResim) return frames;
    const int bytes = static_cast<int>(frames * 4);
    gState.audioPending.append(reinterpret_cast<const char*>(data), bytes);
    if (gState.isRecording)
        gState.audioRecBuf.append(reinterpret_cast<const char*>(data), bytes);
    return frames;
}

void LibretroCore::inputPollCb() {
    // 게임패드 폴링은 MainWindow의 에뮬 루프에서 처리
}

int16_t LibretroCore::inputStateCb(unsigned port, unsigned device,
                                    unsigned /*index*/, unsigned id) {
    if (device != RETRO_DEVICE_JOYPAD || id >= 16) return 0;
    // 1P↔2P 스왑 모드: port 0↔1 교환 (싱글 플레이 연습용)
    if (gState.swapPlayers) {
        if (port == 0) return gState.p2Keys[id];
        if (port == 1) return gState.keys[id];
    }
    // port 0: 터보/넷플레이 처리된 keys 사용 (rawKeys는 터보 미적용)
    if (port == 0) return gState.keys[id];
    if (port == 1) return gState.p2Keys[id];
    return 0;
}

void LibretroCore::logCb(retro_log_level level, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // 콘솔 출력 (항상)
    if (level >= RETRO_LOG_WARN)
        qDebug() << "[Core]" << buf;

    // UI EVENTS 패널로 전달 (WARN 이상)
    if (level >= RETRO_LOG_WARN && g_core) {
        const char* prefix = (level == RETRO_LOG_ERROR) ? "❌ " : "⚠ ";
        emit g_core->logMessage(QString(prefix) + QString::fromUtf8(buf).trimmed());
    }
}
