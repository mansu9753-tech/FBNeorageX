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

    //  ★ 데이터 폴더를 실제로 만든다. (roms/previews/... )
    //    최초 실행 때는 config.json 이 없어 load() 가 곧바로 return 하므로,
    //    폴더 생성을 load() 안에 두면 "처음 켠 사용자"에게만 폴더가 안 생긴다.
    //    그래서 load() 와 분리해 항상 호출한다.
    void ensureDataDirs() const;

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

    // ── 사운드 모드 ─────────────────────────────────────
    //   "original" / "hifi" / "cabinet" / "enhanced"
    //   게임별로 다르게 듣고 싶은 경우가 많아 컨트롤·머신세팅과 같은
    //   3단 범위를 쓴다. 해석 우선순위: 게임별 > 기종별 > 전역
    QString                soundMode = "original";   // 전역 기본
    QHash<QString,QString> soundModeByGame;          // romName  → 모드
    QHash<QString,QString> soundModeByPlatform;      // platform → 모드

    // 실제로 적용할 모드를 골라준다 (romName 이 비면 전역값)
    QString resolvedSoundMode(const QString& romName,
                              const QString& platform) const {
        if (!romName.isEmpty()) {
            auto g = soundModeByGame.constFind(romName);
            if (g != soundModeByGame.constEnd() && !g.value().isEmpty())
                return g.value();
        }
        if (!platform.isEmpty()) {
            auto p = soundModeByPlatform.constFind(platform);
            if (p != soundModeByPlatform.constEnd() && !p.value().isEmpty())
                return p.value();
        }
        return soundMode.isEmpty() ? QString("original") : soundMode;
    }

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

    // 셰이더 파라미터 사용자 값 — { 프리셋 파일이름: { 파라미터이름: 값 } }
    //   RetroArch 의 "셰이더 파라미터" 메뉴에 해당한다. 예를 들어 Mega Bezel 의
    //   HSM_ASPECT_RATIO_MODE 를 6(Full) 으로 두면 화면을 꽉 채우도록 늘어난다.
    //   프리셋 파일 이름으로 묶어 두어, 다른 셰이더로 바꿔도 값이 섞이지 않는다.
    QHash<QString, QHash<QString, float>> shaderParams;

    // ── 베젤(아케이드 프레임) ───────────────────────────
    //   bezels/<롬이름>.png 을 먼저 찾고, 없으면 bezels/default.png 를 쓴다.
    //   가운데가 투명한 PNG 를 게임 화면 위에 덧그린다.
    bool    bezelEnabled = false;

    //  베젤 배정 — 하나의 베젤을 여러 게임에 쓰기 위한 것.
    //    키: "game:<롬>" / "plat:<기종>" / "all"
    //    값: 이미지 경로 (bezels/ 아래면 파일명만 저장한다)
    QHash<QString, QString> bezelAssign;

    //  적용할 베젤을 고른다. 좁은 범위부터 찾는다:
    //    ① 게임 배정  ② bezels/<롬>.png     ③ 기종 배정
    //    ④ bezels/<기종>.png  ⑤ 전체 배정   ⑥ bezels/default.png
    //  whyOut 에는 어디서 왔는지(게임별/기종별/전체)를 담는다.
    QString bezelPathFor(const QString& rom, const QString& platform,
                         QString* whyOut = nullptr) const;

    // ── 기타 ────────────────────────────────────────────
    QString uiLanguage = "ko";          // GUI 표시 언어: "ko" / "en"
    QString videoRenderer = "opengl";   // 게임 화면 그리기: "opengl"(기본, 셰이더 지원) / "software"(CPU, 어떤 PC 에서도 보임)
    bool    showIntro  = true;          // 시작할 때 오프닝 영상을 보여 준다

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
    // 범위별 터보: "game:<롬>" / "plat:<기종>#std|#6btn" → "0,8,1|6" (켠 버튼 인덱스 | 주기).
    //   없으면 위의 전역 값을 쓴다.
    QHash<QString, QString> turboScoped;

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
    QHash<int,int> keyboardMappingSix; // 6버튼 격투 배치용 (약손 등 의미 id)

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
    QHash<QString, QHash<int,int>> padProfiles;   // (구버전 — 마이그레이션 후 미사용)
    QHash<QString, int>            padAssign;

    // ── 게임패드 매핑 (단일 저장소) ─────────────────────────
    //   예전에는 "전역/기종별/게임별"(xinputMapping·xiScoped)과
    //   "장치별"(padProfiles)이 서로 다른 곳에 저장되고 서로를 덮어써서,
    //   무엇이 적용될지 예측할 수 없었다. 이제 저장소는 이것 하나뿐이다.
    //
    //   키 형식:  "game:<롬>"          — 이 게임 전용
    //             "plat:<기종>"        — 이 기종 공통
    //             "dev:<장치명>#std"   — 이 장치, 일반 배치
    //             "dev:<장치명>#6btn"  — 이 장치, 6버튼 격투 배치
    //   해석 우선순위: game > plat > dev > 배치 기본값(코드)
    QHash<QString, QHash<int,int>> padMaps;

    // 구버전 패드 설정을 정리했는가 (실행 중에만 쓰는 표시 — 저장 안 함)
    bool padMapsMigrated = false;

    // ── 머신 세팅 (DIP/BIOS) ────────────────────────────────────
    // 게임별: machineVars[romName][var]=value
    // 기종별: machineVarsByPlatform[platform][var]=value
    // 해석 우선순위: 게임별 > 기종별 > 코어 기본
    QHash<QString, QHash<QString,QString>> machineVars;
    QHash<QString, QHash<QString,QString>> machineVarsByPlatform;

    // ── 싱글톤 ──────────────────────────────────────────
    static AppSettings& instance();

    void load(const QString& path = {});
    void save(const QString& path = {}) const;

    //  ★ 자주 바꾸는 설정을 기본값으로 되돌린다 (경로·소리·화면·넷플레이 포트).
    //    저장은 하지 않는다 — 부르는 쪽이 save() 한다.
    //    사운드 모드는 게임별·기종별 저장분까지 지운다. 전역만 되돌리면 우선순위가
    //    높은 게임별 설정이 남아 "초기화했는데 소리가 그대로" 가 된다.
    //    (키 매핑·핫키·즐겨찾기·DIP·셰이더는 건드리지 않는다: 사용자가 공들인 것들이다)
    void resetToDefaults();

private:
    AppSettings();
    QString defaultConfigPath() const;
};

inline AppSettings& gSettings = AppSettings::instance();
