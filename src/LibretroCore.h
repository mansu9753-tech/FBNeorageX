#pragma once
// LibretroCore.h — libretro .dll/.so 동적 로딩 및 콜백 관리

#include <QString>
#include <QObject>
#include "libretro.h"
#include "EmulatorState.h"

class LibretroCore : public QObject {
    Q_OBJECT
public:
    explicit LibretroCore(QObject* parent = nullptr);
    ~LibretroCore() override;

    // ── 코어 로딩 ─────────────────────────────────────────
    bool    load(const QString& libPath);
    void    unload();
    bool    isLoaded() const { return m_loaded; }

    // ── 게임 ──────────────────────────────────────────────
    bool    loadGame(const QString& romPath);
    void    unloadGame();
    bool    gameLoaded() const { return m_gameLoaded; }

    // ── 에뮬레이션 ────────────────────────────────────────
    void    run();
    void    reset();
    double  fps() const { return m_fps; }

    // ── 세이브스테이트 ────────────────────────────────────
    size_t  serializeSize();
    bool    serialize(void* data, size_t size);
    bool    unserialize(const void* data, size_t size);

    // ── 치트 ──────────────────────────────────────────────
    void    cheatReset();
    void    cheatSet(unsigned index, bool enabled, const QString& code);

    // ── 메모리 직접 접근 (치트 RAM 패치용) ────────────────
    void*  getMemoryData(unsigned memId);
    size_t getMemorySize(unsigned memId);

    // ── 시스템 정보 ───────────────────────────────────────
    retro_system_info    sysInfo()   const { return m_sysInfo; }
    retro_system_av_info avInfo()    const { return m_avInfo; }
    retro_pixel_format   pixFmt()    const { return m_pixelFormat; }

    // ── 코어 경로 설정 ────────────────────────────────────
    void setSystemDir(const QString& dir);
    void setSaveDir(const QString& dir);

signals:
    void logMessage(const QString& msg);

private:
    // 동적 라이브러리 핸들
    void* m_handle = nullptr;

    // 상태
    bool  m_loaded     = false;
    bool  m_gameLoaded = false;
    double m_fps       = 60.0;

    // 코어 정보
    retro_system_info    m_sysInfo    = {};
    retro_system_av_info m_avInfo     = {};
    retro_pixel_format   m_pixelFormat = RETRO_PIXEL_FORMAT_RGB565;

    // 경로 (코어에 넘겨줄 C 문자열 수명 보장용)
    QByteArray m_systemDirBa;
    QByteArray m_saveDirBa;

    // ── 함수 포인터 ───────────────────────────────────────
    retro_init_t                m_retro_init                = nullptr;
    retro_deinit_t              m_retro_deinit              = nullptr;
    retro_api_version_t         m_retro_api_version         = nullptr;
    retro_get_system_info_t     m_retro_get_system_info     = nullptr;
    retro_get_system_av_info_t  m_retro_get_system_av_info  = nullptr;
    retro_load_game_t           m_retro_load_game           = nullptr;
    retro_unload_game_t         m_retro_unload_game         = nullptr;
    retro_run_t                 m_retro_run                 = nullptr;
    retro_serialize_size_t      m_retro_serialize_size      = nullptr;
    retro_serialize_t           m_retro_serialize           = nullptr;
    retro_unserialize_t         m_retro_unserialize         = nullptr;
    retro_reset_t               m_retro_reset               = nullptr;
    retro_cheat_reset_t         m_retro_cheat_reset         = nullptr;
    retro_cheat_set_t           m_retro_cheat_set           = nullptr;
    retro_set_environment_t     m_retro_set_environment     = nullptr;
    retro_set_video_refresh_t   m_retro_set_video_refresh   = nullptr;
    retro_set_audio_sample_t    m_retro_set_audio_sample    = nullptr;
    retro_set_audio_sample_batch_t m_retro_set_audio_sample_batch = nullptr;
    retro_set_input_poll_t      m_retro_set_input_poll      = nullptr;
    retro_set_input_state_t     m_retro_set_input_state     = nullptr;

    // 메모리 접근 (선택적)
    using retro_get_memory_data_t = void* (*)(unsigned);
    using retro_get_memory_size_t = size_t (*)(unsigned);
    retro_get_memory_data_t m_retro_get_memory_data = nullptr;
    retro_get_memory_size_t m_retro_get_memory_size = nullptr;

    // ── 내부 헬퍼 ─────────────────────────────────────────
    void* getProcAddress(const char* name);
    bool  loadAllProcs();

    // ── 정적 콜백 (libretro → C 함수 포인터 필요) ─────────
    static bool    environmentCb(unsigned cmd, void* data);
    static void    videoRefreshCb(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    audioSampleCb(int16_t left, int16_t right);
    static size_t  audioSampleBatchCb(const int16_t* data, size_t frames);
    static void    inputPollCb();
    static int16_t inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
    static void    logCb(retro_log_level level, const char* fmt, ...);
};
