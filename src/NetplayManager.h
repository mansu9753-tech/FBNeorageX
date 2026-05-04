#pragma once
// NetplayManager.h — Rollback Netcode  (UDP + 상태머신 + 안전 종료)
//
//  상태 전이:
//    Disconnected → (hostListen/clientConnect) → Lobby
//    Lobby        → (sendLoadGame)             → Loading
//    Loading      → (sendReady)                → Ready
//    Ready        → (sendStart / startReceived)→ Playing
//    Playing      → (sendGameOver / received)  → Lobby     ← 소켓 유지!
//    Any          → (shutdown)                 → Disconnected

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QHash>
#include <QHostAddress>

class NetplayManager : public QObject {
    Q_OBJECT
public:
    // ── 상태머신 ─────────────────────────────────────────────
    enum class State {
        Disconnected,  // 소켓 없음
        Lobby,         // 연결됨, 게임 없음
        Loading,       // 게임 로딩 중
        Ready,         // 로딩 완료 (상대 READY 대기)
        Playing        // 게임 진행 중
    };
    Q_ENUM(State)

    // ── 메시지 타입 ──────────────────────────────────────────
    static constexpr uint8_t MSG_INPUT     = 0x00; // 번들 입력
    static constexpr uint8_t MSG_LOAD_GAME = 0x01; // 게임 선택 동기화
    static constexpr uint8_t MSG_READY     = 0x03; // 로딩 완료 선언 (+ RTT 페이로드)
    static constexpr uint8_t MSG_START     = 0x04; // 호스트→클라이언트 시작 신호
    static constexpr uint8_t MSG_STATE     = 0x05; // 스냅샷 청크
    static constexpr uint8_t MSG_HELLO     = 0x06; // UDP 핸드셰이크
    static constexpr uint8_t MSG_ACK       = 0x07; // 핸드셰이크 응답
    static constexpr uint8_t MSG_GAME_OVER = 0x08; // 게임 종료 선언
    static constexpr uint8_t MSG_HEARTBEAT = 0x09; // 연결 유지
    static constexpr uint8_t MSG_START_ACK = 0x0A; // 클라이언트→호스트 시작 확인

    // ── 상수 ────────────────────────────────────────────────
    static constexpr int  MAX_ROLLBACK      = 30;   // 롤백 창 확대 (20→30)
    static constexpr int  MAX_AHEAD         = 8;
    static constexpr int  SYNC_INTERVAL     = 4;    // 싱크 간격 축소 (8→4 프레임)
    static constexpr int  DEFAULT_PORT      = 7845;
    static constexpr int  CHUNK_SIZE        = 1300;
    static constexpr int  INPUT_REDUNDANCY  = 8;    // 입력 중복 전송 증가 (5→8)
    static constexpr int  HELLO_RETRY_MAX   = 50;
    static constexpr int  HELLO_INTERVAL_MS = 200;
    static constexpr int  HEARTBEAT_INTERVAL_MS = 500;
    static constexpr int  DISCONNECT_TIMEOUT_MS = 6000; // 6초 무응답 → 끊김

    explicit NetplayManager(QObject* parent = nullptr);
    ~NetplayManager() override;

    // ── 연결 관리 ────────────────────────────────────────────
    void hostListen(int port = DEFAULT_PORT);
    void clientConnect(const QString& ip, int port = DEFAULT_PORT);

    // shutdown : 소켓까지 완전 종료 (Address-already-in-use 방지 처리 포함)
    void shutdown();

    // cleanupGame : 소켓 유지 + 게임 데이터만 초기화 → Lobby 복귀
    void cleanupGame();

    // ── 상태 조회 ────────────────────────────────────────────
    State   netState() const { return m_state; }
    bool    active()   const { return m_state != State::Disconnected; }
    bool    isHost()   const { return m_isHost; }
    bool    playing()  const { return m_state == State::Playing; }
    QString localIp()  const;
    int     rttMs()    const { return m_rttMs; }

    // ── 게임 흐름 메시지 ─────────────────────────────────────
    void sendLoadGame(const QString& romName);  // 호스트: 게임 선택 알림
    void sendReady();                           // 양쪽: 로딩 완료 선언
    void sendStart();                           // 호스트: 양쪽 동시 시작
    void sendGameOver();                        // 게임 종료 알림

    // ── 입력 / 스냅샷 전송 ──────────────────────────────────
    void sendInput(uint32_t frame, uint16_t bits);
    void sendState(uint32_t frame, const QByteArray& data);

    // ── Rollback API ─────────────────────────────────────────
    int16_t  getRemoteInput(uint32_t frame) const;
    void     recordPrediction(uint32_t frame, uint16_t bits);
    int      getRollbackFrame(uint32_t currentFrame) const;
    void     confirmFramesUpTo(uint32_t frame);
    uint32_t remoteMaxFrame() const;

    // ── 게임 데이터 리셋 (cleanupGame 내부·외부 모두 사용) ──
    void resetGameState();

signals:
    // 연결
    void connected(bool isHost);
    void disconnected();
    void error(const QString& msg);
    void stateChanged(NetplayManager::State s);

    // 게임 흐름
    void loadGameReceived(const QString& romName);
    void readyReceived();
    void startReceived();
    void gameOverReceived();

    // 롤백
    void stateReceived(quint32 frame, QByteArray data);

private:
    // ── 연결 상태 ────────────────────────────────────────────
    State        m_state  = State::Disconnected;
    bool         m_isHost = false;

    QUdpSocket*  m_socket    = nullptr;
    QHostAddress m_remoteAddr;
    quint16      m_remotePort = 0;

    // ── HELLO 재시도 + RTT 측정 ──────────────────────────────
    QTimer*       m_helloTimer   = nullptr;
    int           m_helloRetry   = 0;
    QElapsedTimer m_helloSentAt;  // 첫 HELLO 송신 시각 (RTT 측정용)
    int           m_rttMs        = 100; // 왕복 지연 추정치 (ms, HELLO→ACK 측정)

    // ── ACK 기반 동시 시작 제어 ──────────────────────────────
    bool          m_waitingStartAck = false; // sendStart 후 ACK 대기 플래그

    // ── Heartbeat / Disconnect 감지 ──────────────────────────
    QTimer*       m_heartbeatTimer = nullptr;
    QElapsedTimer m_lastRecvClock;   // 마지막 패킷 수신 시각

    // ── Rollback 데이터 ──────────────────────────────────────
    mutable QMutex            m_mutex;
    QHash<uint32_t, uint16_t> m_remoteInputs;
    QHash<uint32_t, uint16_t> m_predicted;
    uint16_t  m_lastRemote     = 0;
    uint32_t  m_remoteMaxFrame = 0;

    // 입력 번들 (redundancy)
    struct InputEntry { uint32_t frame; uint16_t bits; };
    QList<InputEntry> m_localInputHistory;

    // 상태 청크 재조립
    struct ChunkAssembly {
        uint32_t frame = 0; uint16_t totalChunks = 0;
        QHash<uint16_t, QByteArray> chunks;
    };
    ChunkAssembly m_chunkBuf;

    // ── 내부 헬퍼 ───────────────────────────────────────────
    void setState(State s);
    void sendRaw(const QByteArray& data);
    void sendCtrl(const QByteArray& pkt, int times = 3);
    void processPacket(const QByteArray& data);
    // checkReadyToStart 제거 — 양쪽 준비 판단은 MainWindow에서 관리

private slots:
    void onReadyRead();
    void onHelloTimer();
    void onHeartbeat();
};

// 싱글톤
inline NetplayManager& gNetplay() {
    static NetplayManager nm;
    return nm;
}
