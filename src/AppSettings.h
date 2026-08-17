#pragma once
// AppSettings.h — 앱 설정 (Python AppSettings 대응)

#include <QString>
#include <QStringList>
#include <QHash>

struct AppSettings {
    // ── 기준 폴더 (포터블) ──────────────────────────────
    //  모든 사용자 데이터(roms/previews/screenshots/saves/cheats/recordings,
    //  config.json)를 "프로그램이 있는 폴더" 아래 모은다. 폴더 하나만 옮기면
    //  통째로 이동·백업·정리가 되고, 어디에 뭐가 쌓이는지 한눈에 보인다.
    //  ★ 스팀덱(Linux) 번들은 실행파일이 <루트>/bin/ 안에 있으므로,
    //    사용자가 보는 <루트>(FBNeoRageX.sh·names.txt 가 있는 곳)를 기준으로 삼는다.
    static QString baseDir();

    //  ★ 기본 경로 확정 — 반드시 QApplication 생성 후, load() 전에 호출.
    //    gSettings 는 정적 초기화 시점(=QApplication 생성 전)에 만들어지는데
    //    그때 applicationDirPath() 는 빈 문자열이라 경로가 "/roms" 처럼 깨진다.
    void initDefaults();

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
    int     videoFlashStrength = 100;   // 보정 강도 0~100
                                        // 100 = 번쩍임을 주변 밝기에 완전히 맞춤(완전 제거)
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
    QString uiLanguage = "ko";          // GUI 표시 언어: "ko" / "en"

    // ── 넷플레이 ────────────────────────────────────────
    int     netplayPort       = 7845;
    int     netplayInputDelay = 2;   // 입력 지연 프레임 (0=없음, 1~8 / 해외플레이 권장 2~4)
    // 내장 릴레이(Cloudflare Worker) 주소. 계정 ID 를 포함하므로 GUI 에는
    // 절대 실제 값을 노출하지 않는다(육안·스크린샷 차단). builtinRelayUrl() 로 비교.
    static QString builtinRelayUrl() { return "https://fbneoragex-relay.mansu9753.workers.dev"; }
    QString netplayRelayUrl   = builtinRelayUrl();

    // ── 즐겨찾기 ────────────────────────────────────────
    QStringList favorites;
    // 마지막으로 플레이한 게임(롬 이름). 다음 실행 때 그 게임을 선택된 상태로
    // 복원해, 목록 맨 위로 초기화되지 않게 한다.
    QString lastGame;

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

    // ── 장치별 게임패드 프로필 ──────────────────────────
    //   패드마다 버튼 번호 체계가 달라(예: 8BitDo 16버튼 vs Xbox 11버튼)
    //   공용 매핑 하나로는 맞출 수 없다. 장치 이름별로 따로 저장한다.
    //   padProfiles : 장치 이름 → 버튼 매핑
    //   padAssign   : 장치 이름 → 플레이어 (1~4, 0 = 사용 안 함)
    QHash<QString, QHash<int,int>> padProfiles;
    QHash<QString, int>            padAssign;

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
