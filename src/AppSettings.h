#pragma once
// AppSettings.h — 앱 설정 (Python AppSettings 대응)

#include <QString>
#include <QStringList>
#include <QHash>

struct AppSettings {
    // ── 경로 ────────────────────────────────────────────
    QString romPath;
    QString previewPath;
    QString screenshotPath;
    QString savePath;
    QString cheatPath;

    // ── 오디오 ──────────────────────────────────────────
    int    audioVolume    = 100;
    int    audioSampleRate= 48000;
    // Linux(PipeWire): HW 버퍼 최솟값이 96ms이므로 100ms 기본값
    // Windows(WASAPI): 80ms — 낮은 레이턴시
#ifdef Q_OS_LINUX
    int    audioBufferMs  = 100;
#else
    int    audioBufferMs  = 80;
#endif
    double audioDrcMax    = 0.005;

    // ── 비디오 ──────────────────────────────────────────
    QString videoScaleMode  = "Fit";    // "Fill" / "Fit" / "1:1"
    bool    videoSmooth     = false;
    bool    videoCrtMode    = false;
    double  videoCrtIntensity = 0.4;
    int     videoFrameskip  = 0;        // 0=OFF, -1=AUTO, 1~5
    bool    videoFlashGuard  = false;   // 플래시 감소 (눈 보호) on/off
    int     videoFlashStrength = 80;    // 강도 0~100 (클수록 더 어둡게)
    // Linux(Steam Deck): GameScope가 VSync를 자체 처리 → swapInterval=0이 AFL과 충돌하지 않음
    // Windows: 컴포지터 없이 직접 출력 → VSync ON이 테어링 방지에 필요
#ifdef Q_OS_LINUX
    bool    videoVsync      = false;
#else
    bool    videoVsync      = true;
#endif
    QString videoShaderPath;

    // ── 기타 ────────────────────────────────────────────
    QString region = "USA";

    // ── 넷플레이 ────────────────────────────────────────
    int     netplayPort       = 7845;
    int     netplayInputDelay = 2;   // 입력 지연 프레임 (0=없음, 1~8 / 해외플레이 권장 2~4)
    QString netplayRelayUrl   = "https://fbneoragex-relay.mansu9753.workers.dev";  // Cloudflare Worker 릴레이 URL

    // ── 즐겨찾기 ────────────────────────────────────────
    QStringList favorites;

    // ── 터보 설정 ───────────────────────────────────────
    int     turboPeriod = 6;        // ON/OFF 주기 (프레임)
    // 터보 활성 버튼: "0,1,8,9" 형식 쉼표 구분 문자열
    QString turboButtons;

    // ── 녹화 ────────────────────────────────────────────────
    QString recordPath;         // 녹화 저장 경로 (기본: recordings/)

    // ── 컨트롤러 ────────────────────────────────────────────
    // "auto": XInput 우선, 없으면 WinMM(DirectInput) 자동 전환
    // "xinput": Xbox 컨트롤러 전용
    // "winmm":  아케이드 스틱 / 일반 HID 게임패드 전용
    QString        inputMode       = "auto";
    QHash<int,int> xinputMapping;   // XInput 버튼 비트 → libretro idx (전역 폴백)
    QHash<int,int> winmmMapping;    // WinMM 버튼 인덱스 → libretro idx (전역 폴백)
    QHash<int,int> keyboardMapping; // Qt::Key (전역 폴백)

    // ── 기종별/게임별 컨트롤 매핑 ───────────────────────────────
    //   scope 키: "plat:<기종>" (기종별 전역) 또는 "game:<romName>" (게임별)
    //   해석 우선순위: game > plat > 전역(위 3개) > 기본
    QHash<QString, QHash<int,int>> kbScoped;   // 키보드
    QHash<QString, QHash<int,int>> xiScoped;   // XInput
    QHash<QString, QHash<int,int>> wmScoped;   // WinMM

    // ── 머신 세팅 (DIP/BIOS) ────────────────────────────────────
    // 게임별: machineVars[romName][var]=value
    // 기종별: machineVarsByPlatform[platform][var]=value
    // 해석 우선순위: 게임별 > 기종별 > 코어 기본
    QHash<QString, QHash<QString,QString>> machineVars;
    QHash<QString, QHash<QString,QString>> machineVarsByPlatform;

    // ── 핫키 (action 이름 → 인코딩 키) ──────────────────────────
    //   인코딩: (Qt::Key) | (modifiers << 24)  — modifiers: 1=Shift,2=Ctrl,4=Alt
    //   비어있으면 코드의 기본 핫키 사용. 리셋 시 clear → 기본 복귀.
    QHash<QString,int> hotkeyMap;

    // ── 싱글톤 ──────────────────────────────────────────
    static AppSettings& instance();

    void load(const QString& path = {});
    void save(const QString& path = {}) const;

private:
    AppSettings();
    QString defaultConfigPath() const;
};

inline AppSettings& gSettings = AppSettings::instance();
