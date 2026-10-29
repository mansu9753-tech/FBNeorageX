#pragma once
// WinCounter.h — 넷플레이 중 게임 결과 자동 집계
//   · 대전 격투게임: 세트 승수 (1P : 2P)
//   · 벨트스크롤 액션(캐딜락·천지를먹다·파이널파이트): 1P / 2P 점수
//
//  넷플레이(Fightcade 처럼)에서 한 세트(3인 팀전이 끝날 때까지)를 이기면 승수가 올라간다.
//  게임 RAM 에 있는 "1P 쪽 / 2P 쪽에서 쓰러진 캐릭터 수" 카운터가 2 → 3 이 되는 순간을 본다.
//    · 1P 쪽 카운터가 3 이 되면 2P 승, 2P 쪽 카운터가 3 이 되면 1P 승. (KOF95 는 팀 전멸 표시가 0→0xFF)
//  양쪽 PC 의 RAM 은 같으므로 따로 주고받지 않아도 같은 숫자가 나온다.
//  롤백 때 같은 세트를 두 번 세지 않도록 상태(State)를 프레임별로 저장/복원할 수 있다.
//
//  지원 목록은 RAM 주소를 실제 게임을 돌려서 확인한 것만 넣는다 (WinCounter.cpp 의 표).

#include <QString>
#include <cstddef>
#include <cstdint>

class WinCounter {
public:
    enum class Mode { None, Fight, Score };

    struct State {
        uint8_t prev1 = 0, prev2 = 0;   // 직전 프레임의 카운터 값
        int     w1 = 0, w2 = 0;         // 1P / 2P 세트 승수
        bool    armed = false;          // 첫 프레임은 값만 기억한다 (게임을 막 켰을 때 가짜 이벤트 방지)
        int     s1 = 0, s2 = 0;         // 1P / 2P 현재 점수 (Score 모드)
        int     max1 = 0, max2 = 0;     // 이번 판에서 나온 최고 점수
    };

    static bool supported(const QString& romName);

    void setGame(const QString& romName);          // 게임을 켤 때: 승수 0, 지원 여부 판단
    bool active() const { return m_mode != Mode::None; }
    Mode mode() const { return m_mode; }
    int  maxScore1() const { return m_s.max1; }
    int  maxScore2() const { return m_s.max2; }
    void reset();                                  // 승수만 0 으로 (직접 초기화)

    // 프레임 하나를 실행한 직후 호출. 새 세트 결과가 나오면 true.
    bool update(const uint8_t* ram, size_t size);
    // 재동기(상태 덮어쓰기) 직후: 값만 다시 기억하고 승수는 그대로 둔다.
    void resync(const uint8_t* ram, size_t size);

    State state() const { return m_s; }
    void  setState(const State& s) { m_s = s; }

    int wins1() const { return m_s.w1; }
    int wins2() const { return m_s.w2; }
    QString text() const;                          // "1P  2 : 1  2P"  /  "1P 12,300   2P 8,400"

private:
    Mode     m_mode = Mode::None;
    uint32_t m_a1 = 0, m_a2 = 0;                   // 1P / 2P 쪽 "쓰러진 수" 카운터(또는 전멸 표시) 주소 (0 = 미지원)
    bool     m_flag = false;                       // true: 값이 0→0xFF 가 되면 전멸 (KOF95)
    State    m_s;
};
