// NetplayManager.cpp — Rollback Netcode (UDP 상태머신 + 안전 종료)
//
//  흐름 요약
//  ─────────────────────────────────────────────────────────────
//  [연결]
//    호스트: hostListen() → UDP 바인드 → HELLO 대기
//    조인  : clientConnect() → HELLO 송신(200ms 재시도) → ACK 수신 → Lobby
//
//  [게임 시작 Loading Barrier]
//    호스트: sendLoadGame(rom) → 조인에게 전달
//    양쪽  : 로딩 완료 → sendReady() → Ready 상태
//    호스트: 양쪽 Ready 확인 → sendStart() → 양쪽 Playing 진입 Frame 0
//
//  [게임 종료]
//    어느쪽이든 sendGameOver() → 양쪽 Lobby 복귀 (소켓 유지)
//
//  [안전 종료]
//    shutdown() → abort() + deleteLater() + 500ms 후 실제 소켓 삭제
//               → 재연결 시 "Address already in use" 방지

#include "NetplayManager.h"
#include "StunClient.h"
#include <QNetworkInterface>
#include <QHostInfo>
#include <QVariant>
#include <QDebug>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
// ★ Windows UDP 소켓의 SIO_UDP_CONNRESET 동작 비활성화.
//   기본값: 닫힌 포트로 보낸 UDP 에 대해 ICMP port-unreachable 을 받으면
//   다음 recvfrom 이 WSAECONNRESET 에러를 반환 → QUdpSocket 오동작 → 크래시.
//   홀펀칭은 닫힌 포트로 프로브를 마구 보내므로 이 동작을 반드시 꺼야 함.
static void disableUdpConnReset(QUdpSocket* sock) {
    if (!sock) { qDebug("[CONNRESET] sock null"); return; }
    qintptr fd = sock->socketDescriptor();
    if (fd == -1) { qDebug("[CONNRESET] fd 무효 — 적용 실패"); return; }
    BOOL  newBehavior   = FALSE;
    DWORD bytesReturned = 0;
    // SIO_UDP_CONNRESET = _WSAIOW(IOC_VENDOR, 12) = 0x9800000C
    DWORD ioctlCode = 0x9800000C;
    int r = WSAIoctl(static_cast<SOCKET>(fd), ioctlCode,
             &newBehavior, sizeof(newBehavior),
             nullptr, 0, &bytesReturned, nullptr, nullptr);
    qDebug("[CONNRESET] SIO_UDP_CONNRESET 비활성화 결과=%d (0=성공)", r);
}
#else
static void disableUdpConnReset(QUdpSocket*) {}
#endif

// ★ UDP 소켓 버퍼 대폭 확대.
//   상태 스냅샷(KOF98 ~414KB)을 319개 청크로 버스트 전송하면, 기본 소켓
//   버퍼(~64KB ≈ 49패킷)가 즉시 넘쳐 나머지 ~85%가 OS 단에서 버려진다.
//   → 송신/수신 버퍼를 8MB 로 키워 한 상태 전체가 버퍼에 들어가게 함.
static void tuneSocketBuffers(QUdpSocket* sock) {
    if (!sock) return;
    const int bufBytes = 8 * 1024 * 1024;   // 8MB
    sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, QVariant(bufBytes));
    sock->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption,    QVariant(bufBytes));
}

// ── 생성자/소멸자 ────────────────────────────────────────────
NetplayManager::NetplayManager(QObject* parent)
    : QObject(parent)
{
    m_helloTimer = new QTimer(this);
    m_helloTimer->setInterval(HELLO_INTERVAL_MS);
    connect(m_helloTimer, &QTimer::timeout, this, &NetplayManager::onHelloTimer);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetplayManager::onHeartbeat);

    // STUN 재시도 타이머 (500ms 간격, 최대 6회 = 3초)
    m_stunTimer = new QTimer(this);
    m_stunTimer->setInterval(500);
    connect(m_stunTimer, &QTimer::timeout, this, &NetplayManager::onStunRetry);

    // 상태 청크 페이싱 타이머 (1ms 마다 8청크씩 전송)
    m_stateTxTimer = new QTimer(this);
    m_stateTxTimer->setInterval(1);
    connect(m_stateTxTimer, &QTimer::timeout, this, &NetplayManager::onStateTxTick);
}

NetplayManager::~NetplayManager() {
    m_helloTimer->stop();
    m_heartbeatTimer->stop();
    if (m_socket) { m_socket->abort(); delete m_socket; m_socket = nullptr; }
}

// ── 상태 전이 ────────────────────────────────────────────────
void NetplayManager::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

// ── 호스트 바인드 ────────────────────────────────────────────
void NetplayManager::hostListen(int port) {
    shutdown();
    m_isHost = true;

    m_socket = new QUdpSocket(this);
    // ReuseAddressHint: 이전 소켓이 TIME_WAIT 상태여도 즉시 재바인드 가능
    if (!m_socket->bind(QHostAddress::Any, static_cast<quint16>(port),
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit error(QString("UDP 바인드 실패 (port %1): %2")
                   .arg(port).arg(m_socket->errorString()));
        m_socket->deleteLater(); m_socket = nullptr;
        m_isHost = false;
        return;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &NetplayManager::onReadyRead);
    disableUdpConnReset(m_socket);   // Windows ICMP unreachable 크래시 방지
    tuneSocketBuffers(m_socket);     // 대용량 상태 버스트 수용 (버퍼 8MB)
    qDebug() << "NetplayManager: 호스트 대기 port=" << port;
}

// ── 클라이언트 연결 (직접 IP 모드) ───────────────────────────
// clientPrepare() + clientStartHandshake() 의 합성 — 기존 동작 보존.
void NetplayManager::clientConnect(const QString& ip, int port) {
    if (!clientPrepare()) return;
    clientStartHandshake(ip, port);
    qDebug() << "NetplayManager: clientConnect →" << ip << ":" << port;
}

// ── 클라이언트 소켓 바인드만 (HELLO 미발사) ──────────────────
// 토큰 기반 JOIN 흐름: bind → STUN → 릴레이 → 호스트 주소 확보
// → clientStartHandshake() 에서 HELLO 시작.
bool NetplayManager::clientPrepare() {
    shutdown();
    m_isHost     = false;
    m_remoteAddr = QHostAddress();
    m_remotePort = 0;

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::Any, 0,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit error("UDP 바인드 실패: " + m_socket->errorString());
        m_socket->deleteLater(); m_socket = nullptr;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &NetplayManager::onReadyRead);
    disableUdpConnReset(m_socket);   // Windows ICMP unreachable 크래시 방지
    tuneSocketBuffers(m_socket);     // 대용량 상태 버스트 수용 (버퍼 8MB)
    qDebug() << "NetplayManager: clientPrepare localPort=" << m_socket->localPort();
    return true;
}

// ── 호스트 주소 확정 후 HELLO 발사 시작 ──────────────────────
// 중복 호출 안전: 이미 연결 완료 상태(Lobby+)면 무시.
void NetplayManager::clientStartHandshake(const QString& ip, int port) {
    if (!m_socket) {
        // prepare 안 된 상태로 호출되면 안전하게 bind 수행
        if (!clientPrepare()) return;
    }
    // 이미 핸드셰이크 성공/진행 후면 재시작 안 함 (relayPollPeer 가
    // 피어를 여러 번 보고할 수 있으므로 idempotent 동작 보장)
    if (m_state != State::Disconnected) {
        qDebug() << "NetplayManager: clientStartHandshake skipped (state="
                 << (int)m_state << ")";
        return;
    }
    m_isHost     = false;
    m_remoteAddr = QHostAddress(ip);
    m_remotePort = static_cast<quint16>(port);

    m_helloRetry = 0;
    m_helloSentAt.start();   // RTT 측정 시작
    m_helloTimer->start();
    onHelloTimer();
    qDebug() << "NetplayManager: HELLO 시작 →" << ip << ":" << port;
}

// ── STUN: 외부 IP:Port 발견 ──────────────────────────────────
// 1. DNS 로 STUN 서버 주소 해석
// 2. 현재 m_socket 으로 Binding Request 송신
// 3. 응답 수신은 onReadyRead 에서 StunClient::isStunResponse() 로 가로채기
// 4. 응답 파싱 → externalAddressDiscovered(ip,port) 신호
// 실패 시 stunFailed(reason) 신호.
void NetplayManager::discoverExternalAddress(const QString& stunHost, int stunPort) {
    if (!m_socket) {
        emit stunFailed("STUN: 소켓 미바인드");
        return;
    }
    // 이전 STUN 진행 중이면 정리
    if (m_stunTimer) m_stunTimer->stop();
    m_stunActive  = true;
    m_stunRetries = 0;
    m_stunTxnId.clear();
    m_stunPort = static_cast<quint16>(stunPort);

    // DNS 비동기 조회
    QHostInfo::lookupHost(stunHost, this,
        [this](const QHostInfo& info){
            if (!m_stunActive) return;
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                m_stunActive = false;
                emit stunFailed("STUN DNS 실패: " + info.errorString());
                return;
            }
            // IPv4 우선
            m_stunAddr = QHostAddress();
            for (const auto& a : info.addresses()) {
                if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                    m_stunAddr = a; break;
                }
            }
            if (m_stunAddr.isNull()) {
                m_stunActive = false;
                emit stunFailed("STUN: IPv4 주소 없음");
                return;
            }
            qDebug("[STUN] server resolved %s:%u",
                   m_stunAddr.toString().toUtf8().constData(),
                   (unsigned)m_stunPort);
            sendStunRequest();
            m_stunTimer->start();
        });
}

void NetplayManager::sendStunRequest() {
    if (!m_socket || m_stunAddr.isNull() || m_stunPort == 0) return;
    QByteArray pkt = StunClient::buildBindingRequest(m_stunTxnId);
    m_socket->writeDatagram(pkt, m_stunAddr, m_stunPort);
    qDebug("[STUN] binding request sent (retry=%d)", m_stunRetries);
}

void NetplayManager::onStunRetry() {
    if (!m_stunActive) { m_stunTimer->stop(); return; }
    if (++m_stunRetries > 6) {
        m_stunTimer->stop();
        m_stunActive = false;
        emit stunFailed("STUN 타임아웃 (3초 무응답)");
        return;
    }
    sendStunRequest();
}

// ── 완전 종료 (소켓 닫기) ───────────────────────────────────
void NetplayManager::shutdown() {
    m_helloTimer->stop();
    m_heartbeatTimer->stop();
    if (m_stunTimer) m_stunTimer->stop();
    if (m_stateTxTimer) m_stateTxTimer->stop();
    m_stateTxQueue.clear();
    m_stunActive = false;
    setState(State::Disconnected);

    m_isHost     = false;
    m_remoteAddr = QHostAddress();
    m_remotePort = 0;
    m_helloRetry = 0;

    resetGameState();

    if (m_socket) {
        // abort(): 즉시 소켓 닫기 (graceful close 없이)
        // deleteLater(): 현재 이벤트 루프 사이클 완료 후 삭제
        // → 동일 포트를 즉시 재바인드해도 "Address already in use" 없음
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

// ── 게임 데이터만 초기화 (소켓 유지) ────────────────────────
// Lobby로 돌아가되 연결은 유지
void NetplayManager::cleanupGame() {
    resetGameState();
    if (m_state == State::Playing || m_state == State::Loading ||
        m_state == State::Ready) {
        setState(State::Lobby);
    }
}

// ── 로컬 IP ──────────────────────────────────────────────────
QString NetplayManager::localIp() const {
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
            !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            for (const auto& e : iface.addressEntries())
                if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                    return e.ip().toString();
        }
    }
    return "127.0.0.1";
}

// ── 전송 헬퍼 ────────────────────────────────────────────────
void NetplayManager::sendRaw(const QByteArray& data) {
    if (!m_socket || m_remoteAddr.isNull() || m_remotePort == 0) return;
    m_socket->writeDatagram(data, m_remoteAddr, m_remotePort);
}

void NetplayManager::sendCtrl(const QByteArray& pkt, int times) {
    for (int i = 0; i < times; ++i) sendRaw(pkt);
}

// ── 게임 흐름 메시지 ─────────────────────────────────────────

// 호스트: 선택한 게임을 조인에게 알림 → Lobby→Loading 전이
void NetplayManager::sendLoadGame(const QString& romName, int inputDelay) {
    m_inputDelay = qBound(0, inputDelay, 8);
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_LOAD_GAME));
    pkt.append(static_cast<char>(m_inputDelay));  // byte 1: 입력 지연 프레임
    pkt.append(romName.toUtf8());                 // byte 2+: ROM 이름
    sendCtrl(pkt, 5);
    setState(State::Loading);
}

// 로딩 완료 선언 → Loading→Ready 전이
// 클라이언트는 페이로드에 RTT(ms)를 포함 → 호스트가 동시 시작 지연 계산에 사용
void NetplayManager::sendReady() {
    QByteArray pkt(3, Qt::Uninitialized);
    pkt[0] = static_cast<char>(MSG_READY);
    pkt[1] = static_cast<char>( m_rttMs        & 0xFF);
    pkt[2] = static_cast<char>((m_rttMs >>  8) & 0xFF);
    sendCtrl(pkt, 3);
    setState(State::Ready);
}

// 호스트 전용: 양쪽 Ready 확인 후 동시 시작
// ACK 기반 동시 시작: 클라이언트가 MSG_START 수신 후 즉시 Playing 진입 + MSG_START_ACK 전송
// 호스트는 ACK 수신 시 Playing 진입 → 양쪽 시작 시점 차이 ≈ RTT/2
void NetplayManager::sendStart() {
    if (!m_isHost) return;
    sendCtrl(QByteArray(1, static_cast<char>(MSG_START)), 5);
    m_waitingStartAck = true;

    // Fallback: ACK 수신 실패 시 max(300ms, RTT+100ms) 후 자동 시작
    int delay = std::max(300, m_rttMs + 100);
    QTimer::singleShot(delay, this, [this] {
        if (m_waitingStartAck) {
            m_waitingStartAck = false;
            if (m_state == State::Ready)
                setState(State::Playing);
        }
    });
}

// 게임 종료 → Playing→Lobby 전이 (소켓 유지)
void NetplayManager::sendGameOver() {
    sendCtrl(QByteArray(1, static_cast<char>(MSG_GAME_OVER)), 3);
    cleanupGame();
}

// ── 입력 번들 전송 ───────────────────────────────────────────
void NetplayManager::sendInput(uint32_t frame, uint16_t bits) {
    m_localInputHistory.prepend({frame, bits});
    if (m_localInputHistory.size() > INPUT_REDUNDANCY)
        m_localInputHistory.resize(INPUT_REDUNDANCY);

    int count = m_localInputHistory.size();
    QByteArray pkt;
    pkt.reserve(2 + count * 6);
    pkt.append(static_cast<char>(MSG_INPUT));
    pkt.append(static_cast<char>(count));
    for (const auto& e : m_localInputHistory) {
        pkt.append(static_cast<char>( e.frame        & 0xFF));
        pkt.append(static_cast<char>((e.frame >>  8) & 0xFF));
        pkt.append(static_cast<char>((e.frame >> 16) & 0xFF));
        pkt.append(static_cast<char>((e.frame >> 24) & 0xFF));
        pkt.append(static_cast<char>( e.bits         & 0xFF));
        pkt.append(static_cast<char>((e.bits  >>  8) & 0xFF));
    }
    sendRaw(pkt);
}

// ── 상태 청크 전송 ───────────────────────────────────────────
void NetplayManager::sendState(uint32_t frame, const QByteArray& rawData) {
    // ★ 상태 압축 (zlib level 1 = 빠름). 게임 RAM 은 0/반복 패턴이 많아
    //   414KB → ~100~150KB 로 줄어듦 → 청크 수 1/3 → 더 자주 동기화 가능.
    QByteArray data = qCompress(rawData, 1);
    int total = static_cast<int>((data.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    if (total < 1) total = 1;
    {
        static int s_txCount = 0;
        if ((++s_txCount % 5) == 1)
            qDebug("[SYNC-TX] #%d frame=%u raw=%d comp=%d total=%d (queued)",
                   s_txCount, frame, (int)rawData.size(), (int)data.size(), total);
    }

    // ★ 모든 청크를 큐에 넣고 타이머로 페이싱 전송 (버스트 드롭 방지).
    //   이전 상태가 아직 전송 중이면 버리고 최신 상태로 교체 (오래된 상태 무의미).
    m_stateTxQueue.clear();
    for (int i = 0; i < total; ++i) {
        QByteArray pkt(9, Qt::Uninitialized);
        pkt[0] = static_cast<char>(MSG_STATE);
        pkt[1] = static_cast<char>( frame        & 0xFF);
        pkt[2] = static_cast<char>((frame >>  8) & 0xFF);
        pkt[3] = static_cast<char>((frame >> 16) & 0xFF);
        pkt[4] = static_cast<char>((frame >> 24) & 0xFF);
        pkt[5] = static_cast<char>( total        & 0xFF);
        pkt[6] = static_cast<char>((total >>  8) & 0xFF);
        pkt[7] = static_cast<char>( i            & 0xFF);
        pkt[8] = static_cast<char>((i     >>  8) & 0xFF);
        pkt.append(data.mid(i * CHUNK_SIZE, CHUNK_SIZE));
        m_stateTxQueue.append(std::move(pkt));
    }
    if (m_stateTxTimer && !m_stateTxTimer->isActive())
        m_stateTxTimer->start();
}

// 상태 청크 페이싱: 1ms 마다 소량씩 전송 → 네트워크 버스트 드롭 방지.
// 319청크를 8개/tick 로 보내면 ~40tick(=~40ms) 에 걸쳐 분산.
void NetplayManager::onStateTxTick() {
    if (m_stateTxQueue.isEmpty()) {
        if (m_stateTxTimer) m_stateTxTimer->stop();
        return;
    }
    const int perTick = 8;
    for (int i = 0; i < perTick && !m_stateTxQueue.isEmpty(); ++i) {
        sendRaw(m_stateTxQueue.front());
        m_stateTxQueue.pop_front();
    }
}

// ── GGPO desync 감지: 체크섬 전송 ────────────────────────────
// 페이로드: [MSG_CHECKSUM, frame(4), crc(4)]  = 9바이트 (초경량)
void NetplayManager::sendChecksum(uint32_t frame, uint32_t crc) {
    QByteArray pkt(9, Qt::Uninitialized);
    pkt[0] = static_cast<char>(MSG_CHECKSUM);
    pkt[1] = static_cast<char>( frame        & 0xFF);
    pkt[2] = static_cast<char>((frame >>  8) & 0xFF);
    pkt[3] = static_cast<char>((frame >> 16) & 0xFF);
    pkt[4] = static_cast<char>((frame >> 24) & 0xFF);
    pkt[5] = static_cast<char>( crc          & 0xFF);
    pkt[6] = static_cast<char>((crc   >>  8) & 0xFF);
    pkt[7] = static_cast<char>((crc   >> 16) & 0xFF);
    pkt[8] = static_cast<char>((crc   >> 24) & 0xFF);
    sendRaw(pkt);
}

// 클라→호스트 재동기(풀스테이트) 요청 — desync 감지 시 (신뢰성 위해 3회)
void NetplayManager::sendResyncReq(uint32_t frame) {
    QByteArray pkt(5, Qt::Uninitialized);
    pkt[0] = static_cast<char>(MSG_RESYNC_REQ);
    pkt[1] = static_cast<char>( frame        & 0xFF);
    pkt[2] = static_cast<char>((frame >>  8) & 0xFF);
    pkt[3] = static_cast<char>((frame >> 16) & 0xFF);
    pkt[4] = static_cast<char>((frame >> 24) & 0xFF);
    sendCtrl(pkt, 3);
}

// ── 홀펀칭 프로브 ────────────────────────────────────────────
// 호스트가 클라이언트의 외부 IP:Port 에 UDP 패킷을 먼저 보내
// 호스트 공유기 NAT 에 매핑을 생성 → 이후 클라이언트 HELLO 가 통과
void NetplayManager::sendProbeTo(const QString& ip, int port)
{
    if (!m_socket) return;
    QByteArray probe(1, static_cast<char>(MSG_HELLO));
    m_socket->writeDatagram(probe, QHostAddress(ip), static_cast<quint16>(port));
    qDebug() << "NetplayManager: 홀펀칭 프로브 →" << ip << ":" << port;
}

// ── HELLO 재시도 ─────────────────────────────────────────────
void NetplayManager::onHelloTimer() {
    if (m_state != State::Disconnected || !m_socket) { m_helloTimer->stop(); return; }
    if (m_helloRetry++ > HELLO_RETRY_MAX) {
        m_helloTimer->stop();
        emit error(QString("연결 타임아웃 (%1초)")
                   .arg(HELLO_INTERVAL_MS * HELLO_RETRY_MAX / 1000));
        return;
    }
    QByteArray pkt(1, static_cast<char>(MSG_HELLO));
    m_socket->writeDatagram(pkt, m_remoteAddr, m_remotePort);
}

// ── Heartbeat: 연결 유지 + 끊김 감지 ────────────────────────
void NetplayManager::onHeartbeat() {
    // 무응답 → Disconnect 처리
    if (m_lastRecvClock.isValid() &&
        m_lastRecvClock.elapsed() > DISCONNECT_TIMEOUT_MS) {
        qWarning() << "NetplayManager: 연결 타임아웃 (무응답" << DISCONNECT_TIMEOUT_MS << "ms)";
        emit disconnected();
        shutdown();
        return;
    }
    // Heartbeat 전송 (상대도 나를 살아있다고 인식)
    sendRaw(QByteArray(1, static_cast<char>(MSG_HEARTBEAT)));
}

// ── 수신 처리 ────────────────────────────────────────────────
void NetplayManager::onReadyRead() {
    // ★ 에러 데이터그램 가드: Windows ICMP 에러 상태에서 pendingDatagramSize() 가
    //   비정상값을 반복 반환할 때만 탈출. 정상 패킷(MSG_STATE 청크 등)은 제한 없이
    //   모두 처리 — 그렇지 않으면 큰 상태 스냅샷(NeoGeo ~수백 청크)이 잘려서
    //   재조립 실패 → 주기적 동기화 깨짐.
    int errorGuard = 0;
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QHostAddress sender;
        quint16      senderPort = 0;
        QByteArray   data;

        // ★ Windows UDP: 닫힌/도달불가 포트로 프로브 → ICMP port-unreachable →
        //   소켓 에러 상태 → pendingDatagramSize() 가 -1 또는 비정상 거대값 반환.
        //   그대로 resize() 하면 크래시 → 크기를 [1, 65535] 로 엄격히 검증.
        qint64 dgSize = m_socket->pendingDatagramSize();
        if (dgSize <= 0 || dgSize > 65535) {
            // 에러/비정상 데이터그램 → 고정 버퍼로 dequeue 후 건너뜀
            char discard[2048];
            m_socket->readDatagram(discard, sizeof(discard));
            if (++errorGuard > 64) break;   // 연속 에러만 탈출 (정상 패킷은 무제한)
            continue;
        }
        data.resize(static_cast<int>(dgSize));
        qint64 rd = m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);
        if (rd <= 0) continue;
        if (rd < data.size()) data.resize(static_cast<int>(rd));
        if (data.isEmpty()) continue;

        // ── STUN 응답 가로채기 (게임 패킷 dispatch 전에 처리) ──
        // 게임 메시지는 1바이트 type 으로 시작하므로 STUN 응답(20+바이트,
        // magic cookie 검증)과 충돌하지 않음.
        if (StunClient::isStunResponse(data)) {
            QString extIp; int extPort = 0;
            if (StunClient::parseBindingResponse(data, m_stunTxnId, extIp, extPort)) {
                if (m_stunTimer) m_stunTimer->stop();
                m_stunActive = false;
                qDebug("[STUN] external addr %s:%d",
                       extIp.toUtf8().constData(), extPort);
                emit externalAddressDiscovered(extIp, extPort);
                qDebug("[STUN] emit externalAddressDiscovered 반환 — onReadyRead 계속");
            }
            continue;  // 게임 dispatch 로 보내지 않음
        }

        // 마지막 수신 시각 갱신 (heartbeat watchdog)
        m_lastRecvClock.start();

        uint8_t type = static_cast<uint8_t>(data[0]);
        // 핵심 연결 이벤트만 진단: HELLO/ACK/LOAD_GAME/GAME_OVER/RESYNC_REQ.
        // 고빈도(INPUT/STATE/HEARTBEAT/CHECKSUM)·재전송(READY/START) 은 제외(플러드 방지).
        if (type == MSG_HELLO || type == MSG_ACK ||
            type == MSG_LOAD_GAME || type == MSG_GAME_OVER ||
            type == MSG_RESYNC_REQ)
            qDebug("[NM] recv type=%u sz=%d state=%d isHost=%d",
                   (unsigned)type, data.size(), (int)m_state, m_isHost ? 1 : 0);

        // ── UDP 핸드셰이크 ──────────────────────────────────
        if (type == MSG_HELLO) {
            if (m_isHost) {
                if (m_state == State::Disconnected) {
                    m_remoteAddr = sender;
                    m_remotePort = senderPort;
                    qDebug("[NM] HOST: HELLO recv → setState(Lobby)");
                    setState(State::Lobby);
                    qDebug("[NM] HOST: setState done → heartbeat start");
                    m_heartbeatTimer->start();
                    m_lastRecvClock.start();
                    qDebug("[NM] HOST: emit connected(true)");
                    emit connected(true);
                    qDebug("[NM] HOST: emit connected(true) done. addr=%s port=%u",
                           sender.toString().toUtf8().constData(), (unsigned)senderPort);
                }
                QByteArray ack(1, static_cast<char>(MSG_ACK));
                m_socket->writeDatagram(ack, sender, senderPort);
            }
            continue;

        } else if (type == MSG_ACK) {
            if (!m_isHost && m_state == State::Disconnected) {
                // RTT = HELLO 첫 송신~ACK 수신 시간
                if (m_helloSentAt.isValid())
                    m_rttMs = static_cast<int>(std::min<qint64>(1000, m_helloSentAt.elapsed()));
                m_helloTimer->stop();
                qDebug("[NM] CLIENT: ACK recv rtt=%d → setState(Lobby)", m_rttMs);
                setState(State::Lobby);
                qDebug("[NM] CLIENT: setState done → heartbeat start");
                m_heartbeatTimer->start();
                m_lastRecvClock.start();
                qDebug("[NM] CLIENT: emit connected(false)");
                emit connected(false);
                qDebug("[NM] CLIENT: emit connected(false) done");
            }
            continue;
        }

        processPacket(data);
    }
}

void NetplayManager::processPacket(const QByteArray& data) {
    if (data.isEmpty()) return;
    uint8_t type = static_cast<uint8_t>(data[0]);

    switch (type) {

    // ── 입력 번들 ──────────────────────────────────────────
    case MSG_INPUT: {
        if (data.size() < 2) break;
        int count = static_cast<uint8_t>(data[1]);
        if (data.size() < 2 + count * 6) break;

        QMutexLocker lk(&m_mutex);
        for (int i = 0; i < count; ++i) {
            int o = 2 + i * 6;
            uint32_t frame =
                static_cast<uint8_t>(data[o  ])         |
                (static_cast<uint8_t>(data[o+1]) <<  8) |
                (static_cast<uint8_t>(data[o+2]) << 16) |
                (static_cast<uint8_t>(data[o+3]) << 24);
            uint16_t bits =
                static_cast<uint8_t>(data[o+4]) |
                (static_cast<uint8_t>(data[o+5]) << 8);
            if (!m_remoteInputs.contains(frame)) {
                m_remoteInputs[frame] = bits;
                m_lastRemote = bits;
                if (frame > m_remoteMaxFrame) m_remoteMaxFrame = frame;
            }
        }
        break;
    }

    // ── 게임 선택 동기화 ───────────────────────────────────
    case MSG_LOAD_GAME: {
        // byte 1: 입력 지연 프레임, byte 2+: ROM 이름
        int delay = (data.size() >= 2) ? static_cast<uint8_t>(data[1]) : 0;
        m_inputDelay = qBound(0, delay, 8);
        QString romName = QString::fromUtf8(data.mid(2));
        setState(State::Loading);
        emit loadGameReceived(romName, m_inputDelay);
        break;
    }

    // ── 로딩 완료 선언 ─────────────────────────────────────
    // 페이로드: [MSG_READY, rtt_lo, rtt_hi]  (클라이언트 RTT 포함)
    case MSG_READY:
        // 호스트: 클라이언트의 RTT 수신 → 동시 시작 지연 계산에 사용
        if (m_isHost && data.size() >= 3) {
            int peerRtt = static_cast<uint8_t>(data[1]) |
                          (static_cast<uint8_t>(data[2]) << 8);
            if (peerRtt > 0 && peerRtt < 2000)
                m_rttMs = peerRtt;
        }
        emit readyReceived();
        break;

    // ── 시작 신호 (호스트→클라이언트) ─────────────────────
    // 클라이언트: Playing 진입 + ACK 전송
    // 호스트는 ACK 수신 후 Playing 진입 (sendStart 참조)
    case MSG_START:
        setState(State::Playing);
        emit startReceived();
        if (!m_isHost) {
            // 클라이언트: ACK 전송 (호스트가 Playing에 진입하는 트리거)
            sendCtrl(QByteArray(1, static_cast<char>(MSG_START_ACK)), 3);
        }
        break;

    // ── 시작 확인 (클라이언트→호스트) ─────────────────────
    case MSG_START_ACK:
        if (m_isHost && m_waitingStartAck) {
            m_waitingStartAck = false;
            if (m_state == State::Ready)
                setState(State::Playing);
        }
        break;

    // ── 게임 종료 ──────────────────────────────────────────
    case MSG_GAME_OVER:
        cleanupGame();
        emit gameOverReceived();
        break;

    // ── GGPO 체크섬 수신 (desync 감지) ─────────────────────
    case MSG_CHECKSUM: {
        if (data.size() < 9) break;
        uint32_t frame =
            static_cast<uint8_t>(data[1])        |
            (static_cast<uint8_t>(data[2]) <<  8) |
            (static_cast<uint8_t>(data[3]) << 16) |
            (static_cast<uint8_t>(data[4]) << 24);
        uint32_t crc =
            static_cast<uint8_t>(data[5])        |
            (static_cast<uint8_t>(data[6]) <<  8) |
            (static_cast<uint8_t>(data[7]) << 16) |
            (static_cast<uint8_t>(data[8]) << 24);
        emit checksumReceived(frame, crc);
        break;
    }

    // ── 재동기 요청 (클라→호스트) ──────────────────────────
    case MSG_RESYNC_REQ: {
        if (data.size() < 5) break;
        uint32_t frame =
            static_cast<uint8_t>(data[1])        |
            (static_cast<uint8_t>(data[2]) <<  8) |
            (static_cast<uint8_t>(data[3]) << 16) |
            (static_cast<uint8_t>(data[4]) << 24);
        if (m_isHost) emit resyncRequested(frame);
        break;
    }

    // ── 상태 청크 ──────────────────────────────────────────
    case MSG_STATE: {
        if (data.size() < 9) break;
        uint32_t frame =
            static_cast<uint8_t>(data[1])        |
            (static_cast<uint8_t>(data[2]) <<  8) |
            (static_cast<uint8_t>(data[3]) << 16) |
            (static_cast<uint8_t>(data[4]) << 24);
        uint16_t total =
            static_cast<uint8_t>(data[5]) | (static_cast<uint8_t>(data[6]) << 8);
        uint16_t idx =
            static_cast<uint8_t>(data[7]) | (static_cast<uint8_t>(data[8]) << 8);

        // ★ chunkBuf 스래싱 방지: 더 "최신" 프레임이 올 때만 버퍼를 새로 시작.
        //   고지연/재정렬로 늦게 도착한 '이전 프레임' 청크는 무시 → 현재 재조립
        //   중인 최신 프레임이 망가지지 않게 한다. (이전엔 어떤 프레임이든 리셋 →
        //   계속 미완성 → 동기화 불가)
        if (frame > m_chunkBuf.frame || m_chunkBuf.totalChunks == 0) {
            // 이전 프레임이 미완성이었으면 진단 (throttled)
            if (m_chunkBuf.totalChunks > 0 &&
                static_cast<int>(m_chunkBuf.chunks.size()) < m_chunkBuf.totalChunks) {
                static int s_incCount = 0;
                if ((++s_incCount % 5) == 1)
                    qDebug("[SYNC-RX] 미완성 폐기 #%d frame=%u had=%d/%d (새frame=%u)",
                           s_incCount, m_chunkBuf.frame,
                           (int)m_chunkBuf.chunks.size(), (int)m_chunkBuf.totalChunks, frame);
            }
            m_chunkBuf.frame = frame; m_chunkBuf.totalChunks = total;
            m_chunkBuf.chunks.clear();
        } else if (frame < m_chunkBuf.frame) {
            break;   // 오래된 프레임 청크 — 무시 (현재 재조립 보호)
        }
        m_chunkBuf.chunks[idx] = data.mid(9);

        if (static_cast<int>(m_chunkBuf.chunks.size()) == total) {
            QByteArray assembled;
            assembled.reserve(total * CHUNK_SIZE);
            for (uint16_t k = 0; k < total; ++k)
                assembled.append(m_chunkBuf.chunks.value(k));
            m_chunkBuf.chunks.clear();
            // ★ 압축 해제 (송신측 qCompress 대응)
            QByteArray decompressed = qUncompress(assembled);
            {
                static int s_rxCount = 0;
                if ((++s_rxCount % 5) == 1)
                    qDebug("[SYNC-RX] 재조립완료 #%d frame=%u comp=%d raw=%d total=%d",
                           s_rxCount, frame, (int)assembled.size(),
                           (int)decompressed.size(), total);
            }
            if (!decompressed.isEmpty())
                emit stateReceived(frame, decompressed);
        }
        break;
    }

    // ── Heartbeat: 수신만 해도 watchdog 갱신 (onReadyRead에서 처리됨) ─
    case MSG_HEARTBEAT:
        break;

    default:
        qWarning() << "NetplayManager: 알 수 없는 패킷 0x"
                   << Qt::hex << static_cast<int>(type);
        break;
    }
}


// ── Rollback API ─────────────────────────────────────────────
int16_t NetplayManager::getRemoteInput(uint32_t frame) const {
    QMutexLocker lk(&m_mutex);
    auto it = m_remoteInputs.find(frame);
    if (it != m_remoteInputs.end()) return static_cast<int16_t>(it.value());
    return static_cast<int16_t>(m_lastRemote); // hold-last 예측
}

void NetplayManager::recordPrediction(uint32_t frame, uint16_t bits) {
    QMutexLocker lk(&m_mutex);
    m_predicted[frame] = bits;
}

int NetplayManager::getRollbackFrame(uint32_t currentFrame) const {
    QMutexLocker lk(&m_mutex);
    int rollbackTo = -1;
    uint32_t earliest = (currentFrame > static_cast<uint32_t>(MAX_ROLLBACK))
                        ? currentFrame - MAX_ROLLBACK : 0;
    for (uint32_t f = earliest; f < currentFrame; ++f) {
        auto ri = m_remoteInputs.find(f);
        auto pi = m_predicted.find(f);
        if (ri == m_remoteInputs.end() || pi == m_predicted.end()) continue;
        if (ri.value() != pi.value()) { rollbackTo = static_cast<int>(f); break; }
    }
    return rollbackTo;
}

void NetplayManager::confirmFramesUpTo(uint32_t frame) {
    QMutexLocker lk(&m_mutex);
    uint32_t cutoff = (frame > static_cast<uint32_t>(MAX_ROLLBACK + 2))
                      ? frame - MAX_ROLLBACK - 2 : 0;
    QList<uint32_t> toRemove;
    for (auto it = m_remoteInputs.begin(); it != m_remoteInputs.end(); ++it)
        if (it.key() < cutoff) toRemove.append(it.key());
    for (auto k : toRemove) { m_remoteInputs.remove(k); m_predicted.remove(k); }
}

uint32_t NetplayManager::remoteMaxFrame() const {
    QMutexLocker lk(&m_mutex);
    return m_remoteMaxFrame;
}

// ── 게임 데이터 리셋 ─────────────────────────────────────────
void NetplayManager::resetGameState() {
    QMutexLocker lk(&m_mutex);
    m_remoteInputs.clear();
    m_predicted.clear();
    m_localInputHistory.clear();
    m_lastRemote      = 0;
    m_remoteMaxFrame  = 0;
    m_chunkBuf.chunks.clear();
    m_chunkBuf.frame       = 0;
    m_chunkBuf.totalChunks = 0;
    m_waitingStartAck      = false;
    m_stateTxQueue.clear();
    if (m_stateTxTimer) m_stateTxTimer->stop();
}
