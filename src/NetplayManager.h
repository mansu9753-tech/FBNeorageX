#pragma once
// NetplayManager.h — Rollback Netcode  (UDP + 상태머신 + 안전 종료)
//
//  구성: 호스트 1 + 참가자 최대 2 = 3자리(seat). 참가자는 호스트하고만 연결한다 (별 모양).
//        자리 번호: 호스트 0, 참가자 1·2. 호스트가 입장 순서대로 번호를 준다.
//        호스트는 로비 메시지(프로필·채팅)와 게임 입력을 다른 참가자에게 중계한다.
//
//  상태 전이:
//    Disconnected → (hostListen/clientConnect) → Lobby
//    Lobby        → (sendLoadGame)             → Loading
//    Loading      → (sendReady)                → Ready
//    Ready        → (sendStart / startReceived)→ Playing
//    Playing      → (sendGameOver / received)  → Lobby     ← 소켓 유지!
//    Any          → (shutdown)                 → Disconnected

#include <QObject>
#include <QMap>
#include <QStringList>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QHash>
#include <QHostAddress>
#include <QVector>

class NetplayManager : public QObject {
    Q_OBJECT
public:
    // ── 상태머신 ─────────────────────────────────────────────
    enum class State {
        Disconnected,  // 소켓 없음 / 아직 아무도 안 들어옴
        Lobby,         // 연결됨, 게임 없음
        Loading,       // 게임 로딩 중
        Ready,         // 로딩 완료 (상대 READY 대기)
        Playing        // 게임 진행 중
    };
    Q_ENUM(State)

    static constexpr int MAX_SEATS = 3;

    // ── 메시지 타입 ──────────────────────────────────────────
    static constexpr uint8_t MSG_INPUT     = 0x00; // 번들 입력  [type, seat, count, (frame u32, bits u16)*]
    static constexpr uint8_t MSG_LOAD_GAME = 0x01; // 게임 선택 동기화  [type, 딜레이, 플레이어 자리 비트마스크, 롬 이름]
    static constexpr uint8_t MSG_READY     = 0x03; // 로딩 완료 선언 (+ RTT 페이로드)
    static constexpr uint8_t MSG_START     = 0x04; // 호스트→클라이언트 시작 신호
    static constexpr uint8_t MSG_STATE     = 0x05; // 스냅샷 청크
    static constexpr uint8_t MSG_HELLO     = 0x06; // UDP 핸드셰이크
    static constexpr uint8_t MSG_ACK       = 0x07; // 핸드셰이크 응답  [type, 내 자리 번호]
    static constexpr uint8_t MSG_GAME_OVER = 0x08; // 게임 종료 선언
    static constexpr uint8_t MSG_HEARTBEAT = 0x09; // 연결 유지
    static constexpr uint8_t MSG_START_ACK = 0x0A; // 클라이언트→호스트 시작 확인
    static constexpr uint8_t MSG_CHECKSUM  = 0x0B; // 상태 체크섬 (desync 감지)
    static constexpr uint8_t MSG_RESYNC_REQ= 0x0C; // 클라→호스트 풀스테이트 재동기 요청
    static constexpr uint8_t MSG_ROMLIST   = 0x0D; // 내가 가진 ROM 이름 목록 (압축, 조각)  [type, total(u16), index(u16), 데이터]
    static constexpr uint8_t MSG_ROMLIST_REQ=0x0E; // 상대에게 ROM 목록을 다시 보내 달라는 요청
    static constexpr uint8_t MSG_VARS      = 0x0F; // 호스트의 머신 세팅(딥스위치·BIOS 등) 목록 (압축, 조각)  [type, gen, total(u16), index(u16), 데이터]
    static constexpr uint8_t MSG_PROFILE   = 0x10; // 아이디·닉네임·레벨·캐릭터  [type, 자리, idLen, id, nickLen, 닉네임, level, flags(1=계정), 부품 문자열]
    static constexpr uint8_t MSG_CHAT      = 0x11; // 채팅 한 줄  [type, seq(u16), 보낸 자리, 글(utf8)]
    static constexpr uint8_t MSG_LREADY    = 0x12; // 로비 레디 (참가자→호스트)  [type, 0/1, rev]
    static constexpr uint8_t MSG_ROOMINFO  = 0x13; // 방 설정 (호스트→참가자)  [type, 딜레이, rev, 룸코드 6바이트, 롬 이름(utf8)]
    static constexpr uint8_t MSG_SEATS     = 0x14; // 자리 상태 (호스트→참가자)  [type, 자리0 플래그, 자리1 플래그, 자리2 플래그]
    static constexpr uint8_t MSG_SEATMODE  = 0x15; // 플레이어/관전 희망 (참가자→호스트)  [type, 0/1]

    // 자리 플래그 (MSG_SEATS)
    static constexpr uint8_t SEAT_PRESENT = 1, SEAT_READY = 2, SEAT_SPECTATOR = 4, SEAT_WANTSPEC = 8;

    // ── 상수 ────────────────────────────────────────────────
    static constexpr int  MAX_ROLLBACK      = 30;   // 롤백 창
    static constexpr int  MAX_AHEAD         = 8;
    // ★ 순수 GGPO: 상태 상시 전송 안 함. 입력 교환 + 로컬 롤백이 기본.
    //   desync 는 체크섬으로 감지하고, 어긋날 때만 호스트가 풀스테이트 1회 전송.
    static constexpr int  CHECKSUM_INTERVAL = 30;
    static constexpr int  DEFAULT_PORT      = 7845;
    static constexpr int  CHUNK_SIZE        = 1300;
    static constexpr int  INPUT_REDUNDANCY  = 8;
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
    bool clientPrepare();
    void clientStartHandshake(const QString& ip, int port);

    // ── STUN: 외부 IP:Port 발견 ──────────────────────────────
    void discoverExternalAddress(const QString& stunHost = "stun.l.google.com",
                                  int            stunPort = 19302);

    // shutdown : 소켓까지 완전 종료
    void shutdown();
    // cleanupGame : 소켓 유지 + 게임 데이터만 초기화 → Lobby 복귀
    void cleanupGame();

    // ── 상태 조회 ────────────────────────────────────────────
    State   netState()    const { return m_state; }
    bool    active()      const { return m_state != State::Disconnected; }
    bool    isHost()      const { return m_isHost; }
    bool    playing()     const { return m_state == State::Playing; }
    int     mySlot()      const { return m_mySlot; }
    bool    seatConnected(int slot) const;           // 호스트: 그 자리에 사람이 들어와 있는가
    int     peerCount()   const;                     // 호스트: 들어와 있는 참가자 수
    QString localIp()     const;
    quint16 localPort()   const { return m_socket ? m_socket->localPort() : 0; }
    int     rttMs()       const { return m_rttMs; }
    int     inputDelay()  const { return m_inputDelay; }

    // 홀펀칭: 특정 주소로 HELLO 프로브 전송 (NAT 매핑 생성용)
    void sendProbeTo(const QString& ip, int port);

    // ── 게임 흐름 메시지 ─────────────────────────────────────
    void sendLoadGame(const QString& romName, int inputDelay, int playerMask);   // 호스트: 게임 선택 알림
    void sendReady();                           // 참가자: 로딩 완료 선언 (호스트는 자기 로딩만 하면 된다)
    void sendStart();                           // 호스트: 모두 동시 시작
    void sendGameOver();                        // 게임 종료 알림

    // ── 로비 (아이디·닉네임·채팅·레디·방 설정·자리) ──────────────
    void sendProfile(int slot, const QString& id, const QString& nick, int level, bool account, const QString& parts);
    void sendChat(const QString& text);
    void sendLobbyReady(bool ready, int rev);                    // 참가자→호스트. rev: 내가 본 방 설정 번호
    void sendSeatMode(bool wantSpectator);                       // 참가자→호스트
    void sendSeats(const uint8_t flags[MAX_SEATS]);              // 호스트→참가자
    void sendRoomInfo(const QString& romName, int inputDelay, int rev, const QString& roomCode);

    // ── ROM 목록 교환 (모두가 가진 게임만 고르게 하려고) ──────────
    void sendRomList(const QStringList& roms);
    void sendVars(const QStringList& lines);   // 호스트: "키=값" 목록을 참가자에게
    void requestRomList();

    // ── 입력 / 스냅샷 전송 ──────────────────────────────────
    void sendInput(uint32_t frame, uint16_t bits);
    void sendState(int slot, uint32_t frame, const QByteArray& data);   // 호스트: 그 참가자에게 풀스테이트

    // ── GGPO desync 감지 ────────────────────────────────────
    void sendChecksum(uint32_t frame, uint32_t crc);  // 확정 프레임 체크섬 전송
    void sendResyncReq(uint32_t frame);               // 참가자→호스트 재동기 요청

    // ── Rollback API (자리별) ────────────────────────────────
    //   playerMask: 이번 게임에서 입력을 내는 자리들 (관전 자리는 빠진다)
    void     setPlayers(int playerMask, int mySeat);
    int16_t  getRemoteInput(uint32_t frame, int seat) const;
    void     recordPrediction(uint32_t frame, int seat, uint16_t bits);
    int      getRollbackFrame(uint32_t currentFrame) const;
    void     confirmFramesUpTo(uint32_t frame);
    uint32_t remoteMaxFrame() const;                  // 나 말고 입력을 내는 자리들의 가장 느린 쪽

    // ── 게임 데이터 리셋 (cleanupGame 내부·외부 모두 사용) ──
    void resetGameState();

signals:
    // 연결
    void connected(bool isHost);                       // 처음으로 연결이 이루어졌다 (호스트: 첫 참가자 입장)
    void peerJoined(int slot);                         // 호스트: 참가자가 들어왔다
    void peerLeft(int slot);                           // 호스트: 참가자가 나갔다(무응답)
    void disconnected();                               // 참가자: 호스트와 끊겼다
    void error(const QString& msg);
    void stateChanged(NetplayManager::State s);

    // 게임 흐름
    void loadGameReceived(const QString& romName, int inputDelay, int playerMask);
    void readyReceived(int slot);
    void startReceived();
    void gameOverReceived();
    void profileReceived(int slot, const QString& id, const QString& nick, int level, bool account, const QString& parts);
    void chatReceived(int slot, const QString& text);
    void lobbyReadyReceived(int slot, bool ready, int rev);
    void seatModeReceived(int slot, bool wantSpectator);
    void seatsReceived(int f0, int f1, int f2);
    void roomInfoReceived(const QString& romName, int inputDelay, int rev, const QString& roomCode);
    void varsReceived(const QStringList& lines);
    void romListReceived(int slot, const QStringList& roms);

    // 롤백
    void stateReceived(quint32 frame, QByteArray data);

    // GGPO desync
    void checksumReceived(int slot, quint32 frame, quint32 crc);
    void resyncRequested(int slot, quint32 frame);

    // STUN 결과
    void externalAddressDiscovered(const QString& ip, int port);
    void stunFailed(const QString& reason);

private:
    // ── 연결 상태 ────────────────────────────────────────────
    State        m_state      = State::Disconnected;
    bool         m_isHost     = false;
    int          m_mySlot     = 0;

    // 호스트: 자리 1·2 의 참가자 주소. 참가자: 호스트 주소는 m_remoteAddr/m_remotePort
    struct Peer {
        QHostAddress addr; quint16 port = 0; bool used = false;
        QElapsedTimer lastRecv;
        // ROM 목록 조립 (그 참가자가 보낸 것)
        QMap<int, QByteArray> romChunks; int romTotal = 0; bool romDone = false;
        bool startAcked = false;
        QList<uint32_t> chatSeen;
    };
    Peer         m_peers[MAX_SEATS];                   // [0] 은 참가자일 때 호스트 정보의 ROM 조립용으로만 쓴다

    QByteArray   m_myRomPackets;
    void         transmitMyRomList();
    QByteArray   m_myVarPackets;
    uint8_t      m_varGen = 0;
    uint8_t      m_peerVarGen = 0;
    QMap<int, QByteArray> m_peerVarChunks;
    int          m_peerVarTotal = 0;
    bool         m_peerVarDone  = true;
    void         transmitMyVars();
    uint16_t     m_chatSeq = 0;
    int          m_inputDelay = 0;

    QUdpSocket*  m_socket    = nullptr;
    QHostAddress m_remoteAddr;
    quint16      m_remotePort = 0;

    // ── 상태 스냅샷 청크 페이싱 ──────────────────────────────
    struct TxChunk { int slot; QByteArray pkt; };
    QList<TxChunk>    m_stateTxQueue;
    QTimer*           m_stateTxTimer = nullptr;

    // ── HELLO 재시도 + RTT 측정 ──────────────────────────────
    QTimer*       m_helloTimer   = nullptr;
    int           m_helloRetry   = 0;
    QElapsedTimer m_helloSentAt;
    int           m_rttMs        = 100;

    // ── ACK 기반 동시 시작 제어 ──────────────────────────────
    bool          m_waitingStartAck = false;

    // ── Heartbeat / Disconnect 감지 ──────────────────────────
    QTimer*       m_heartbeatTimer = nullptr;
    QElapsedTimer m_lastRecvClock;

    // ── STUN 상태 ────────────────────────────────────────────
    QTimer*       m_stunTimer    = nullptr;
    QByteArray    m_stunTxnId;
    QHostAddress  m_stunAddr;
    quint16       m_stunPort     = 0;
    int           m_stunRetries  = 0;
    bool          m_stunActive   = false;

    // ── Rollback 데이터 (자리별) ─────────────────────────────
    mutable QMutex            m_mutex;
    QHash<uint32_t, uint16_t> m_remoteInputs[MAX_SEATS];
    QHash<uint32_t, uint16_t> m_predicted[MAX_SEATS];
    uint16_t  m_lastRemote[MAX_SEATS]     = { 0, 0, 0 };
    uint32_t  m_remoteMaxFrame[MAX_SEATS] = { 0, 0, 0 };
    int       m_playerMask = 0;                        // 입력을 내는 자리
    int       m_mySeat     = 0;

    struct InputEntry { uint32_t frame; uint16_t bits; };
    QList<InputEntry> m_localInputHistory;

    struct ChunkAssembly {
        uint32_t frame = 0; uint16_t totalChunks = 0;
        QHash<uint16_t, QByteArray> chunks;
    };
    ChunkAssembly m_chunkBuf;

    // ── 내부 헬퍼 ───────────────────────────────────────────
    void setState(State s);
    void sendRaw(const QByteArray& data);               // 호스트: 모든 참가자 / 참가자: 호스트
    void sendRawTo(int slot, const QByteArray& data);   // 호스트: 그 참가자에게
    void sendCtrl(const QByteArray& pkt, int times = 3);
    void processPacket(const QByteArray& data, int fromSlot);
    int  slotOfSender(const QHostAddress& a, quint16 port) const;
    void dropPeer(int slot);
    void sendStunRequest();

private slots:
    void onReadyRead();
    void onHelloTimer();
    void onHeartbeat();
    void onStunRetry();
    void onStateTxTick();
};

// 싱글톤
inline NetplayManager& gNetplay() {
    static NetplayManager nm;
    return nm;
}
