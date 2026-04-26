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
    int    audioBufferMs  = 80;    // 80ms — 지연감과 안정성 균형
    double audioDrcMax    = 0.005;

    // ── 비디오 ──────────────────────────────────────────
    QString videoScaleMode  = "Fit";    // "Fill" / "Fit" / "1:1"
    bool    videoSmooth     = false;
    bool    videoCrtMode    = false;
    double  videoCrtIntensity = 0.4;
    int     videoFrameskip  = 0;        // 0=OFF, -1=AUTO, 1~5
    bool    videoVsync      = true;
    QString videoShaderPath;

    // ── 기타 ────────────────────────────────────────────
    QString region = "USA";

    // ── 넷플레이 ────────────────────────────────────────
    int     netplayPort = 7845;

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
    QHash<int,int> xinputMapping;   // XInput 버튼 비트 → libretro idx (empty=기본값)
    QHash<int,int> winmmMapping;    // WinMM 버튼 인덱스 → libretro idx (empty=기본값)
    QHash<int,int> keyboardMapping; // Qt::Key → libretro idx (empty=기본값)

    // ── 싱글톤 ──────────────────────────────────────────
    static AppSettings& instance();

    void load(const QString& path = {});
    void save(const QString& path = {}) const;

private:
    AppSettings();
    QString defaultConfigPath() const;
};

inline AppSettings& gSettings = AppSettings::instance();
