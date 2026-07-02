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
    static constexpr uint8_t MSG_CHECKSUM  = 0x0B; // 상태 체크섬 (desync 감지)
    static constexpr uint8_t MSG_RESYNC_REQ= 0x0C; // 클라→호스트 풀스테이트 재동기 요청

    // ── 상수 ────────────────────────────────────────────────
    static constexpr int  MAX_ROLLBACK      = 30;   // 롤백 창
    static constexpr int  MAX_AHEAD         = 8;
    // ★ 순수 GGPO: 상태 상시 전송 안 함. 입력 교환 + 로컬 롤백이 기본.
    //   desync 는 체크섬으로 감지하고, 어긋날 때만 호스트가 풀스테이트 1회 전송.
    //   CHECKSUM_INTERVAL: 확정 프레임에서 N프레임마다 체크섬(8바이트) 교환.
    static constexpr int  CHECKSUM_INTERVAL = 30;
    // (구) SYNC_INTERVAL 은 더 이상 사용 안 함 (상시 스트리밍 제거)
    static constexpr int  DEFAULT_PORT      = 7845;
    static constexpr int  CHUNK_SIZE        = 1300;
    static constexpr int  INPUT_REDUNDANCY  = 8;    // 입력 중복 전송 증가 (5→8)
    static constexpr int  HELLO_RETRY_MAX   = 150;  // 30초 (150 × 200ms)
    static constexpr int  HELLO_INTERVAL_MS = 200;
    static constexpr int  HEARTBEAT_INTERVAL_MS = 500;
    static constexpr int  DISCONNECT_TIMEOUT_MS = 6000; // 6초 무응답 → 끊김

    explicit NetplayManager(QObject* parent = nullptr);
    ~NetplayManager() override;

    // ── 연결 관리 ────────────────────────────────────────────
    void hostListen(int port = DEFAULT_PORT);
    void clientConnect(const QString& ip, int port = DEFAULT_PORT);

    // ── 토큰 기반 JOIN (릴레이 사용 시) ──────────────────────
    // clientPrepare()        : UDP 소켓만 바인드 (HELLO 미발사)
    //                          STUN/릴레이로 호스트 주소 확보 후 핸드셰이크 시작
    // clientStartHandshake() : 호스트 주소 확정되면 HELLO 발사 시작
    bool clientPrepare();
    void clientStartHandshake(const QString& ip, int port);

    // ── STUN: 외부 IP:Port 발견 ──────────────────────────────
    // 현재 UDP 소켓을 사용해 STUN Binding Request 송신.
    // 응답 수신 시 externalAddressDiscovered(ip, port) 신호 발생.
    // 실패/타임아웃 시 stunFailed(reason) 신호.
    void discoverExternalAddress(const QString& stunHost = "stun.l.google.com",
                                  int            stunPort = 19302);

    // shutdown : 소켓까지 완전 종료 (Address-already-in-use 방지 처리 포함)
    void shutdown();

    // cleanupGame : 소켓 유지 + 게임 데이터만 초기화 → Lobby 복귀
    void cleanupGame();

    // ── 상태 조회 ────────────────────────────────────────────
    State   netState()    const { return m_state; }
    bool    active()      const { return m_state != State::Disconnected; }
    bool    isHost()      const { return m_isHost; }
    bool    playing()     const { return m_state == State::Playing; }
    QString localIp()     const;
    quint16 localPort()   const { return m_socket ? m_socket->localPort() : 0; }
    int     rttMs()       const { return m_rttMs; }
    int     inputDelay()  const { return m_inputDelay; }

    // 홀펀칭: 특정 주소로 HELLO 프로브 전송 (NAT 매핑 생성용)
    void sendProbeTo(const QString& ip, int port);

    // ── 게임 흐름 메시지 ─────────────────────────────────────
    void sendLoadGame(const QString& romName, int inputDelay = 0);  // 호스트: 게임 선택 알림 (지연프레임 포함)
    void sendReady();                           // 양쪽: 로딩 완료 선언
    void sendStart();                           // 호스트: 양쪽 동시 시작
    void sendGameOver();                        // 게임 종료 알림

    // ── 입력 / 스냅샷 전송 ──────────────────────────────────
    void sendInput(uint32_t frame, uint16_t bits);
    void sendState(uint32_t frame, const QByteArray& data);

    // ── GGPO desync 감지 ────────────────────────────────────
    void sendChecksum(uint32_t frame, uint32_t crc);  // 확정 프레임 체크섬 전송
    void sendResyncReq(uint32_t frame);               // 클라→호스트 재동기 요청

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
    void loadGameReceived(const QString& romName, int inputDelay);
    void readyReceived();
    void startReceived();
    void gameOverReceived();

    // 롤백
    void stateReceived(quint32 frame, QByteArray data);

    // GGPO desync
    void checksumReceived(quint32 frame, quint32 crc);  // 상대 체크섬 수신
    void resyncRequested(quint32 frame);                // 호스트: 재동기 요청 받음

    // STUN 결과
    void externalAddressDiscovered(const QString& ip, int port);
    void stunFailed(const QString& reason);

private:
    // ── 연결 상태 ────────────────────────────────────────────
    State        m_state      = State::Disconnected;
    bool         m_isHost     = false;
    int          m_inputDelay = 0;  // 입력 지연 프레임 (호스트 설정 → MSG_LOAD_GAME 으로 동기화)

    QUdpSocket*  m_socket    = nullptr;
    QHostAddress m_remoteAddr;
    quint16      m_remotePort = 0;

    // ── 상태 스냅샷 청크 페이싱 ──────────────────────────────
    // 414KB(~319청크)를 한 번에 쏘면 네트워크 버스트 드롭으로 ~84% 유실.
    // 타이머로 나눠 보내 네트워크가 흡수 가능하게 함.
    QList<QByteArray> m_stateTxQueue;   // 전송 대기 청크
    QTimer*           m_stateTxTimer = nullptr;

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

    // ── STUN 상태 ────────────────────────────────────────────
    QTimer*       m_stunTimer    = nullptr;
    QByteArray    m_stunTxnId;          // 현재 진행 중 STUN 트랜잭션 ID (12B)
    QHostAddress  m_stunAddr;           // STUN 서버 IP
    quint16       m_stunPort     = 0;
    int           m_stunRetries  = 0;
    bool          m_stunActive   = false;

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

    // STUN 헬퍼 (Binding Request 송신)
    void sendStunRequest();

private slots:
    void onReadyRead();
    void onHelloTimer();
    void onHeartbeat();
    void onStunRetry();
    void onStateTxTick();   // 상태 청크 페이싱 전송
};

// 싱글톤
inline NetplayManager& gNetplay() {
    static NetplayManager nm;
    return nm;
}
