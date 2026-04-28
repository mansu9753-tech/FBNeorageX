#pragma once
// EmulatorState.h — 에뮬레이터 전역 상태 (Python의 EmulatorState 클래스 대응)

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QSet>
#include <QHash>
#include <atomic>
#include <array>

#include "libretro.h"

// ── DRC 이동 평균 (오디오 버퍼 평활화) ────────────────────
struct MovingAvg {
    static constexpr int N = 5;
    double buf[N] = {};
    int    idx    = 0;
    bool   full   = false;

    double update(double v) {
        buf[idx] = v;
        idx = (idx + 1) % N;
        if (idx == 0) full = true;
        int cnt = full ? N : idx;
        double sum = 0;
        for (int i = 0; i < cnt; ++i) sum += buf[i];
        return sum / cnt;
    }
};

// ── 에뮬레이터 전역 상태 ──────────────────────────────────
struct EmulatorState {
    // ── 게임 상태 ──────────────────────────────────────────
    bool    gameLoaded      = false;
    bool    isPaused        = false;
    bool    fastForward     = false;
    double  coreFps         = 60.0;
    double  coreSampleRate  = 44100.0;  // 코어가 실제로 출력하는 샘플레이트
    int     frameCount      = 0;
    int     gameLoadFrame   = 0;  // 게임 로드 시점 프레임 (치트 딜레이 기준)

    // ── 입력 ──────────────────────────────────────────────
    // rawKeys: 물리 키보드/패드 상태 (항상 현재)
    // keys   : P1 논리 입력 (넷플레이 처리 후)
    // p2Keys : P2 논리 입력
    std::array<int, 16> rawKeys  = {};
    std::array<int, 16> keys     = {};
    std::array<int, 16> p2Keys   = {};

    // 연습용 1P↔2P 포트 스왑 (싱글 플레이 연습 모드)
    bool swapPlayers = false;

    // 현재 눌린 키보드 인덱스 집합
    QSet<int>   kbHeld;

    // ── 터보 ──────────────────────────────────────────────
    // turboBtns: libretro 버튼인덱스 → 터보 활성 여부
    QHash<int, bool> turboBtns;
    // 터보 주기 (프레임). 기본 6 = ON 6프레임 / OFF 6프레임
    int  turboPeriod = 6;
    // 내부 카운터 (MainWindow 에서 증가)
    int  turboFrame  = 0;

    // ── 넷플레이 ────────────────────────────────────────────
    bool netplayResim = false;  // 롤백 재시뮬레이션 중 (오디오 억제)

    // ── 비디오 ────────────────────────────────────────────
    QByteArray          videoBuffer;   // retro_video_refresh 에서 복사된 프레임
    unsigned            videoWidth  = 0;
    unsigned            videoHeight = 0;
    size_t              videoPitch  = 0;
    retro_pixel_format  pixelFormat = RETRO_PIXEL_FORMAT_RGB565;
    std::atomic<bool>   frameReady  = false;

    // ── 오디오 ────────────────────────────────────────────
    QByteArray  audioPending;    // retro_audio_batch 로 누적된 PCM 데이터
    MovingAvg   drcFreeAvg;

    // ── 설정 경로 (코어에 전달) ────────────────────────────
    QByteArray  systemDir;
    QByteArray  saveDir;

    // ── DIP 스위치 / 변수 ─────────────────────────────────
    QHash<QString, QString>     variables;          // key → 현재 값
    QHash<QString, QStringList> variableOptions;    // key → ["opt1","opt2",...]
    QHash<QString, QString>     variableDescriptions; // key → 설명 텍스트
    std::atomic<bool>           variablesUpdated = false; // SET_VARIABLE 후 true

    // ── 녹화 ──────────────────────────────────────────────
    std::atomic<bool> isRecording{false};
    QByteArray        audioRecBuf;      // 녹화용 오디오 복사본 (main thread만 접근)
    QString           lastRecordPath;    // 녹화 완료 후 이동할 최종 경로
    QString           lastRecordTemp;    // WMF ASCII 우회용 임시 녹화 경로

    // 싱글톤 접근
    static EmulatorState& instance() {
        static EmulatorState s;
        return s;
    }

private:
    EmulatorState() = default;
};

// 전역 참조 (편의)
inline EmulatorState& gState = EmulatorState::instance();
