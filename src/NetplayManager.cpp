// NetplayManager.cpp — Rollback Netcode (UDP 상태머신 + 안전 종료, 호스트 1 + 참가자 2)
//
//  흐름 요약
//  ─────────────────────────────────────────────────────────────
//  [연결]
//    호스트: hostListen() → UDP 바인드 → HELLO 대기 (참가자마다 자리 1, 2 를 준다)
//    참가자: clientConnect() → HELLO 송신(200ms 재시도) → ACK(내 자리 번호) 수신 → Lobby
//
//  [게임 시작 Loading Barrier]
//    호스트: sendLoadGame(rom, 딜레이, 플레이어 자리) → 참가자에게 전달
//    모두  : 로딩 완료 → 참가자는 sendReady()
//    호스트: 모든 참가자 Ready 확인 → sendStart() → 모두 Playing 진입 Frame 0
//
//  [중계]
//    참가자끼리는 직접 연결하지 않는다. 호스트가 입력·프로필·채팅을 다른 참가자에게 전달한다.
//
//  [게임 종료]
//    어느쪽이든 sendGameOver() → 모두 Lobby 복귀 (소켓 유지)

#include "NetplayManager.h"
#include <QTimer>
#include "StunClient.h"
#include <QNetworkInterface>
#include <QHostInfo>
#include <QVariant>
#include <QDebug>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
// ★ Windows UDP 소켓의 SIO_UDP_CONNRESET 동작 비활성화 (ICMP port-unreachable 로 인한 오동작 방지)
static void disableUdpConnReset(QUdpSocket* sock) {
    if (!sock) { qDebug("[CONNRESET] sock null"); return; }
    qintptr fd = sock->socketDescriptor();
    if (fd == -1) { qDebug("[CONNRESET] fd 무효 — 적용 실패"); return; }
    BOOL  newBehavior   = FALSE;
    DWORD bytesReturned = 0;
    DWORD ioctlCode = 0x9800000C;   // SIO_UDP_CONNRESET
    int r = WSAIoctl(static_cast<SOCKET>(fd), ioctlCode,
             &newBehavior, sizeof(newBehavior),
             nullptr, 0, &bytesReturned, nullptr, nullptr);
    qDebug("[CONNRESET] SIO_UDP_CONNRESET 비활성화 결과=%d (0=성공)", r);
}
#else
static void disableUdpConnReset(QUdpSocket*) {}
#endif

// ★ UDP 소켓 버퍼 확대 (상태 스냅샷 버스트 수용)
static void tuneSocketBuffers(QUdpSocket* sock) {
    if (!sock) return;
    const int bufBytes = 8 * 1024 * 1024;
    sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, QVariant(bufBytes));
    sock->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption,    QVariant(bufBytes));
}

static bool sameAddr(const QHostAddress& a, const QHostAddress& b) {
    return a.isEqual(b, QHostAddress::ConvertV4MappedToIPv4);
}

static inline uint32_t rd32(const QByteArray& d, int o) {
    return  static_cast<uint8_t>(d[o])         |
           (static_cast<uint8_t>(d[o+1]) <<  8) |
           (static_cast<uint8_t>(d[o+2]) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d[o+3])) << 24);
}
static inline void wr32(QByteArray& d, uint32_t v) {
    d.append(static_cast<char>( v        & 0xFF));
    d.append(static_cast<char>((v >>  8) & 0xFF));
    d.append(static_cast<char>((v >> 16) & 0xFF));
    d.append(static_cast<char>((v >> 24) & 0xFF));
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

    m_stunTimer = new QTimer(this);
    m_stunTimer->setInterval(500);
    connect(m_stunTimer, &QTimer::timeout, this, &NetplayManager::onStunRetry);

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

bool NetplayManager::seatConnected(int slot) const {
    return slot >= 1 && slot < MAX_SEATS && m_peers[slot].used;
}
int NetplayManager::peerCount() const {
    int n = 0;
    for (int i = 1; i < MAX_SEATS; ++i) if (m_peers[i].used) ++n;
    return n;
}

// ── 호스트 바인드 ────────────────────────────────────────────
void NetplayManager::hostListen(int port) {
    shutdown();
    m_isHost = true;
    m_mySlot = 0;

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::Any, static_cast<quint16>(port),
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit error(QString("UDP 바인드 실패 (port %1): %2")
                   .arg(port).arg(m_socket->errorString()));
        m_socket->deleteLater(); m_socket = nullptr;
        m_isHost = false;
        return;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &NetplayManager::onReadyRead);
    disableUdpConnReset(m_socket);
    tuneSocketBuffers(m_socket);
    qDebug() << "NetplayManager: 호스트 대기 port=" << port;
}

// ── 클라이언트 연결 (직접 IP 모드) ───────────────────────────
void NetplayManager::clientConnect(const QString& ip, int port) {
    if (!clientPrepare()) return;
    clientStartHandshake(ip, port);
    qDebug() << "NetplayManager: clientConnect →" << ip << ":" << port;
}

bool NetplayManager::clientPrepare() {
    shutdown();
    m_isHost     = false;
    m_mySlot     = 1;
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
    disableUdpConnReset(m_socket);
    tuneSocketBuffers(m_socket);
    qDebug() << "NetplayManager: clientPrepare localPort=" << m_socket->localPort();
    return true;
}

void NetplayManager::clientStartHandshake(const QString& ip, int port) {
    if (!m_socket) {
        if (!clientPrepare()) return;
    }
    if (m_state != State::Disconnected) {
        qDebug() << "NetplayManager: clientStartHandshake skipped (state="
                 << (int)m_state << ")";
        return;
    }
    m_isHost     = false;
    m_remoteAddr = QHostAddress(ip);
    m_remotePort = static_cast<quint16>(port);

    m_helloRetry = 0;
    m_helloSentAt.start();
    m_helloTimer->start();
    onHelloTimer();
    qDebug() << "NetplayManager: HELLO 시작 →" << ip << ":" << port;
}

// ── STUN: 외부 IP:Port 발견 ──────────────────────────────────
void NetplayManager::discoverExternalAddress(const QString& stunHost, int stunPort) {
    if (!m_socket) {
        emit stunFailed("STUN: 소켓 미바인드");
        return;
    }
    if (m_stunTimer) m_stunTimer->stop();
    m_stunActive  = true;
    m_stunRetries = 0;
    m_stunTxnId.clear();
    m_stunPort = static_cast<quint16>(stunPort);

    QHostInfo::lookupHost(stunHost, this,
        [this](const QHostInfo& info){
            if (!m_stunActive) return;
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                m_stunActive = false;
                emit stunFailed("STUN DNS 실패: " + info.errorString());
                return;
            }
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
    m_mySlot     = 0;
    m_remoteAddr = QHostAddress();
    m_remotePort = 0;
    m_helloRetry = 0;
    m_myRomPackets.clear();
    m_myVarPackets.clear();
    m_peerVarChunks.clear();
    m_peerVarTotal = 0;
    m_peerVarDone  = true;
    for (Peer& p : m_peers) p = Peer();

    resetGameState();

    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

// ── 게임 데이터만 초기화 (소켓 유지) ────────────────────────
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
//   호스트는 들어와 있는 모든 참가자에게, 참가자는 호스트에게 보낸다.
void NetplayManager::sendRaw(const QByteArray& data) {
    if (!m_socket) return;
    if (m_isHost) {
        for (int i = 1; i < MAX_SEATS; ++i)
            if (m_peers[i].used) m_socket->writeDatagram(data, m_peers[i].addr, m_peers[i].port);
    } else if (!m_remoteAddr.isNull() && m_remotePort != 0) {
        m_socket->writeDatagram(data, m_remoteAddr, m_remotePort);
    }
}

void NetplayManager::sendRawTo(int slot, const QByteArray& data) {
    if (!m_socket || !m_isHost || !seatConnected(slot)) return;
    m_socket->writeDatagram(data, m_peers[slot].addr, m_peers[slot].port);
}

void NetplayManager::sendCtrl(const QByteArray& pkt, int times) {
    for (int i = 0; i < times; ++i) sendRaw(pkt);
}

int NetplayManager::slotOfSender(const QHostAddress& a, quint16 port) const {
    for (int i = 1; i < MAX_SEATS; ++i)
        if (m_peers[i].used && m_peers[i].port == port && sameAddr(m_peers[i].addr, a)) return i;
    return -1;
}

// 호스트: 참가자 하나를 내보낸다
void NetplayManager::dropPeer(int slot) {
    if (slot < 1 || slot >= MAX_SEATS || !m_peers[slot].used) return;
    m_peers[slot] = Peer();
    {
        QMutexLocker lk(&m_mutex);
        m_remoteInputs[slot].clear();
        m_predicted[slot].clear();
        m_remoteMaxFrame[slot] = 0;
        m_lastRemote[slot] = 0;
    }
    emit peerLeft(slot);
}

// ── 게임 흐름 메시지 ─────────────────────────────────────────

// 호스트: 선택한 게임을 참가자에게 알림 → Lobby→Loading 전이
void NetplayManager::sendLoadGame(const QString& romName, int inputDelay, int playerMask) {
    m_inputDelay = qBound(0, inputDelay, 8);
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_LOAD_GAME));
    pkt.append(static_cast<char>(m_inputDelay));
    pkt.append(static_cast<char>(playerMask & 0xFF));
    pkt.append(romName.toUtf8());
    sendCtrl(pkt, 5);
    setState(State::Loading);
}

// ── ROM 목록 교환 ────────────────────────────────────────────
void NetplayManager::sendRomList(const QStringList& roms) {
    m_myRomPackets = qCompress(roms.join(QLatin1Char('\n')).toUtf8(), 9);
    transmitMyRomList();
    QTimer::singleShot(400, this, [this] { transmitMyRomList(); });
}

// ── 머신 세팅 교환 (호스트 → 참가자) ─────────────────────────
void NetplayManager::sendVars(const QStringList& lines) {
    ++m_varGen;
    m_myVarPackets = qCompress(lines.join(QLatin1Char('\n')).toUtf8(), 9);
    transmitMyVars();
    QTimer::singleShot(250, this, [this] { transmitMyVars(); });
    QTimer::singleShot(700, this, [this] { transmitMyVars(); });
}

void NetplayManager::transmitMyVars() {
    if (m_myVarPackets.isEmpty() || !active()) return;
    constexpr int kChunk = 1000;
    const int total = (m_myVarPackets.size() + kChunk - 1) / kChunk;
    for (int i = 0; i < total; ++i) {
        QByteArray pkt;
        pkt.append(static_cast<char>(MSG_VARS));
        pkt.append(static_cast<char>(m_varGen));
        pkt.append(static_cast<char>(total & 0xFF));  pkt.append(static_cast<char>((total >> 8) & 0xFF));
        pkt.append(static_cast<char>(i & 0xFF));      pkt.append(static_cast<char>((i >> 8) & 0xFF));
        pkt.append(m_myVarPackets.mid(i * kChunk, kChunk));
        sendRaw(pkt);
    }
}

// ── 로비 메시지 ───────────────────────────────────────────────
//   UDP 라 하나씩 잃을 수 있어서 3번씩 보낸다. 받는 쪽은 같은 내용을 여러 번 받아도 문제가 없게 처리한다.
static QByteArray buildProfile(int slot, const QString& id, const QString& nick, int level, bool account, const QString& parts) {
    QByteArray pkt;
    const QByteArray idb = id.toUtf8().left(32);
    const QByteArray nb = nick.toUtf8().left(48);
    pkt.append(static_cast<char>(NetplayManager::MSG_PROFILE));
    pkt.append(static_cast<char>(slot));
    pkt.append(static_cast<char>(idb.size()));
    pkt.append(idb);
    pkt.append(static_cast<char>(nb.size()));
    pkt.append(nb);
    pkt.append(static_cast<char>(qBound(1, level, 100)));
    pkt.append(static_cast<char>(account ? 1 : 0));
    pkt.append(parts.toUtf8().left(120));
    return pkt;
}

// 호스트: slot 번 자리의 프로필을 모든 참가자에게 / 참가자: 내 프로필을 호스트에게
void NetplayManager::sendProfile(int slot, const QString& id, const QString& nick, int level, bool account, const QString& parts) {
    sendCtrl(buildProfile(m_isHost ? slot : m_mySlot, id, nick, level, account, parts), 3);
}

void NetplayManager::sendChat(const QString& text) {
    QByteArray pkt;
    ++m_chatSeq;
    pkt.append(static_cast<char>(MSG_CHAT));
    pkt.append(static_cast<char>(m_chatSeq & 0xFF));
    pkt.append(static_cast<char>((m_chatSeq >> 8) & 0xFF));
    pkt.append(static_cast<char>(m_isHost ? 0 : m_mySlot));
    pkt.append(text.toUtf8().left(400));
    sendCtrl(pkt, 3);
}

void NetplayManager::sendLobbyReady(bool ready, int rev) {
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_LREADY));
    pkt.append(static_cast<char>(ready ? 1 : 0));
    pkt.append(static_cast<char>(rev & 0xFF));
    sendCtrl(pkt, 3);
}

void NetplayManager::sendSeatMode(bool wantSpectator) {
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_SEATMODE));
    pkt.append(static_cast<char>(wantSpectator ? 1 : 0));
    sendCtrl(pkt, 3);
}

void NetplayManager::sendSeats(const uint8_t flags[MAX_SEATS]) {
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_SEATS));
    for (int i = 0; i < MAX_SEATS; ++i) pkt.append(static_cast<char>(flags[i]));
    sendCtrl(pkt, 3);
}

void NetplayManager::sendRoomInfo(const QString& romName, int inputDelay, int rev, const QString& roomCode) {
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_ROOMINFO));
    pkt.append(static_cast<char>(qBound(0, inputDelay, 8)));
    pkt.append(static_cast<char>(rev & 0xFF));
    QByteArray code = roomCode.toLatin1().left(6);
    while (code.size() < 6) code.append(' ');
    pkt.append(code);
    pkt.append(romName.toUtf8());
    sendCtrl(pkt, 3);
}

void NetplayManager::requestRomList() {
    for (Peer& p : m_peers) { p.romChunks.clear(); p.romTotal = 0; p.romDone = false; }
    sendCtrl(QByteArray(1, static_cast<char>(MSG_ROMLIST_REQ)), 2);
}

void NetplayManager::transmitMyRomList() {
    if (m_myRomPackets.isEmpty() || !active()) return;
    constexpr int kChunk = 1000;
    const int total = (m_myRomPackets.size() + kChunk - 1) / kChunk;
    for (int i = 0; i < total; ++i) {
        QByteArray pkt;
        pkt.append(static_cast<char>(MSG_ROMLIST));
        pkt.append(static_cast<char>(total & 0xFF));  pkt.append(static_cast<char>((total >> 8) & 0xFF));
        pkt.append(static_cast<char>(i & 0xFF));      pkt.append(static_cast<char>((i >> 8) & 0xFF));
        pkt.append(m_myRomPackets.mid(i * kChunk, kChunk));
        sendRaw(pkt);
    }
}

// 로딩 완료 선언 → Loading→Ready 전이 (참가자는 호스트에게 RTT 와 함께 알린다)
void NetplayManager::sendReady() {
    QByteArray pkt(3, Qt::Uninitialized);
    pkt[0] = static_cast<char>(MSG_READY);
    pkt[1] = static_cast<char>( m_rttMs        & 0xFF);
    pkt[2] = static_cast<char>((m_rttMs >>  8) & 0xFF);
    sendCtrl(pkt, 3);
    setState(State::Ready);
}

// 호스트 전용: 모든 참가자가 Ready 인 뒤 동시 시작.
//   참가자는 MSG_START 수신 즉시 Playing + MSG_START_ACK. 호스트는 모든 ACK 가 모이면 Playing 으로 간다.
void NetplayManager::sendStart() {
    if (!m_isHost) return;
    if (m_state == State::Loading) setState(State::Ready);      // 호스트는 READY 를 보내지 않으므로 여기서 Ready 로 간다
    for (Peer& p : m_peers) p.startAcked = false;
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
//   [MSG_INPUT, 자리, 개수, (프레임, 비트)*]  호스트가 받으면 다른 참가자에게 그대로 전달한다.
void NetplayManager::sendInput(uint32_t frame, uint16_t bits) {
    m_localInputHistory.prepend({frame, bits});
    if (m_localInputHistory.size() > INPUT_REDUNDANCY)
        m_localInputHistory.resize(INPUT_REDUNDANCY);

    int count = m_localInputHistory.size();
    QByteArray pkt;
    pkt.reserve(3 + count * 6);
    pkt.append(static_cast<char>(MSG_INPUT));
    pkt.append(static_cast<char>(m_mySeat));
    pkt.append(static_cast<char>(count));
    for (const auto& e : m_localInputHistory) {
        wr32(pkt, e.frame);
        pkt.append(static_cast<char>( e.bits         & 0xFF));
        pkt.append(static_cast<char>((e.bits  >>  8) & 0xFF));
    }
    sendRaw(pkt);
}

// ── 상태 청크 전송 (호스트 → slot 번 참가자) ──────────────────
void NetplayManager::sendState(int slot, uint32_t frame, const QByteArray& rawData) {
    // ★ 상태 압축 (zlib level 1 = 빠름).
    QByteArray data = qCompress(rawData, 1);
    int total = static_cast<int>((data.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    if (total < 1) total = 1;
    {
        static int s_txCount = 0;
        if ((++s_txCount % 5) == 1)
            qDebug("[SYNC-TX] #%d slot=%d frame=%u raw=%d comp=%d total=%d (queued)",
                   s_txCount, slot, frame, (int)rawData.size(), (int)data.size(), total);
    }

    // 모든 청크를 큐에 넣고 타이머로 페이싱 전송 (버스트 드롭 방지). 같은 참가자의 이전 큐는 버린다.
    for (int i = m_stateTxQueue.size() - 1; i >= 0; --i)
        if (m_stateTxQueue[i].slot == slot) m_stateTxQueue.removeAt(i);
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
        m_stateTxQueue.append({ slot, std::move(pkt) });
    }
    if (m_stateTxTimer && !m_stateTxTimer->isActive())
        m_stateTxTimer->start();
}

void NetplayManager::onStateTxTick() {
    if (m_stateTxQueue.isEmpty()) {
        if (m_stateTxTimer) m_stateTxTimer->stop();
        return;
    }
    const int perTick = 8;
    for (int i = 0; i < perTick && !m_stateTxQueue.isEmpty(); ++i) {
        const TxChunk c = m_stateTxQueue.front();
        m_stateTxQueue.pop_front();
        sendRawTo(c.slot, c.pkt);
    }
}

// ── GGPO desync 감지: 체크섬 전송 ────────────────────────────
void NetplayManager::sendChecksum(uint32_t frame, uint32_t crc) {
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_CHECKSUM));
    wr32(pkt, frame);
    wr32(pkt, crc);
    sendRaw(pkt);
}

// 참가자→호스트 재동기(풀스테이트) 요청 — desync 감지 시 (신뢰성 위해 3회)
void NetplayManager::sendResyncReq(uint32_t frame) {
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_RESYNC_REQ));
    wr32(pkt, frame);
    sendCtrl(pkt, 3);
}

// ── 홀펀칭 프로브 ────────────────────────────────────────────
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
    if (m_isHost) {
        // 호스트: 참가자마다 따로 본다. 한 명이 나가도 방은 그대로 열려 있다.
        for (int i = 1; i < MAX_SEATS; ++i) {
            if (m_peers[i].used && m_peers[i].lastRecv.isValid() &&
                m_peers[i].lastRecv.elapsed() > DISCONNECT_TIMEOUT_MS) {
                qWarning() << "NetplayManager: 참가자" << i << "무응답 → 내보냄";
                dropPeer(i);
            }
        }
    } else if (m_lastRecvClock.isValid() &&
               m_lastRecvClock.elapsed() > DISCONNECT_TIMEOUT_MS) {
        qWarning() << "NetplayManager: 연결 타임아웃 (무응답" << DISCONNECT_TIMEOUT_MS << "ms)";
        emit disconnected();
        shutdown();
        return;
    }
    sendRaw(QByteArray(1, static_cast<char>(MSG_HEARTBEAT)));
}

// ── 수신 처리 ────────────────────────────────────────────────
void NetplayManager::onReadyRead() {
    int errorGuard = 0;
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QHostAddress sender;
        quint16      senderPort = 0;
        QByteArray   data;

        qint64 dgSize = m_socket->pendingDatagramSize();
        if (dgSize <= 0 || dgSize > 65535) {
            char discard[2048];
            m_socket->readDatagram(discard, sizeof(discard));
            if (++errorGuard > 64) break;
            continue;
        }
        data.resize(static_cast<int>(dgSize));
        qint64 rd = m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);
        if (rd <= 0) continue;
        if (rd < data.size()) data.resize(static_cast<int>(rd));
        if (data.isEmpty()) continue;

        // ── STUN 응답 가로채기 ──
        if (StunClient::isStunResponse(data)) {
            QString extIp; int extPort = 0;
            if (StunClient::parseBindingResponse(data, m_stunTxnId, extIp, extPort)) {
                if (m_stunTimer) m_stunTimer->stop();
                m_stunActive = false;
                qDebug("[STUN] external addr %s:%d", extIp.toUtf8().constData(), extPort);
                emit externalAddressDiscovered(extIp, extPort);
            }
            continue;
        }

        uint8_t type = static_cast<uint8_t>(data[0]);
        if (type == MSG_HELLO || type == MSG_ACK ||
            type == MSG_LOAD_GAME || type == MSG_GAME_OVER ||
            type == MSG_RESYNC_REQ)
            qDebug("[NM] recv type=%u sz=%d state=%d isHost=%d",
                   (unsigned)type, data.size(), (int)m_state, m_isHost ? 1 : 0);

        // ── UDP 핸드셰이크: 호스트가 새 참가자에게 자리를 준다 ──────────
        if (type == MSG_HELLO) {
            if (m_isHost) {
                int slot = slotOfSender(sender, senderPort);
                const bool fresh = (slot < 0);
                if (fresh) {
                    // 게임이 진행 중이면 새로 받지 않는다 (중간 입장은 지원하지 않는다)
                    if (m_state == State::Playing || m_state == State::Loading || m_state == State::Ready) continue;
                    for (int i = 1; i < MAX_SEATS; ++i) if (!m_peers[i].used) { slot = i; break; }
                    if (slot < 0) continue;                                 // 방이 가득 찼다
                    m_peers[slot] = Peer();
                    m_peers[slot].addr = sender;
                    m_peers[slot].port = senderPort;
                    m_peers[slot].used = true;
                    m_peers[slot].lastRecv.start();
                }
                QByteArray ack;
                ack.append(static_cast<char>(MSG_ACK));
                ack.append(static_cast<char>(slot));
                m_socket->writeDatagram(ack, sender, senderPort);
                if (fresh) {
                    const bool first = (m_state == State::Disconnected);
                    if (first) {
                        qDebug("[NM] HOST: HELLO recv → setState(Lobby)");
                        setState(State::Lobby);
                        m_heartbeatTimer->start();
                        m_lastRecvClock.start();
                        emit connected(true);
                    }
                    qDebug("[NM] HOST: 참가자 입장 slot=%d addr=%s port=%u", slot,
                           sender.toString().toUtf8().constData(), (unsigned)senderPort);
                    emit peerJoined(slot);
                }
            }
            continue;

        } else if (type == MSG_ACK) {
            if (!m_isHost && m_state == State::Disconnected) {
                m_mySlot = (data.size() >= 2) ? qBound(1, int(static_cast<uint8_t>(data[1])), MAX_SEATS - 1) : 1;
                if (m_helloSentAt.isValid())
                    m_rttMs = static_cast<int>(std::min<qint64>(1000, m_helloSentAt.elapsed()));
                m_helloTimer->stop();
                qDebug("[NM] CLIENT: ACK recv rtt=%d slot=%d → setState(Lobby)", m_rttMs, m_mySlot);
                setState(State::Lobby);
                m_heartbeatTimer->start();
                m_lastRecvClock.start();
                emit connected(false);
            }
            continue;
        }

        // ── 보낸 사람 확인: 호스트는 자기 참가자만, 참가자는 호스트로 본다 ──
        int fromSlot = 0;
        if (m_isHost) {
            fromSlot = slotOfSender(sender, senderPort);
            if (fromSlot < 0) continue;                                      // 모르는 사람의 패킷은 버린다
            m_peers[fromSlot].lastRecv.start();
        } else {
            m_lastRecvClock.start();
        }
        processPacket(data, fromSlot);
    }
}

void NetplayManager::processPacket(const QByteArray& data, int fromSlot) {
    if (data.isEmpty()) return;
    uint8_t type = static_cast<uint8_t>(data[0]);

    switch (type) {

    // ── 입력 번들 ──────────────────────────────────────────
    case MSG_INPUT: {
        if (data.size() < 3) break;
        const int count = static_cast<uint8_t>(data[2]);
        if (data.size() < 3 + count * 6) break;
        // 호스트는 보낸 참가자의 자리를 주소로 안다. 참가자는 패킷에 적힌 자리를 쓴다 (호스트=0, 중계된 다른 참가자).
        const int seat = m_isHost ? fromSlot : static_cast<int>(static_cast<uint8_t>(data[1]));
        if (seat < 0 || seat >= MAX_SEATS || seat == m_mySeat) break;

        {
            QMutexLocker lk(&m_mutex);
            for (int i = 0; i < count; ++i) {
                const int o = 3 + i * 6;
                const uint32_t frame = rd32(data, o);
                const uint16_t bits = static_cast<uint8_t>(data[o+4]) | (static_cast<uint8_t>(data[o+5]) << 8);
                if (!m_remoteInputs[seat].contains(frame)) {
                    static const bool kDbg = qEnvironmentVariableIsSet("FBNRX_NP_DEBUG");
                    static uint16_t sPrev[MAX_SEATS] = { 0, 0, 0 };
                    if (kDbg && bits != sPrev[seat]) { qDebug("[NPRECV] f=%u seat=%d bits=%04x", frame, seat, bits); sPrev[seat] = bits; }
                    m_remoteInputs[seat][frame] = bits;
                    m_lastRemote[seat] = bits;
                    if (frame > m_remoteMaxFrame[seat]) m_remoteMaxFrame[seat] = frame;
                }
            }
        }
        if (m_isHost) {                                    // 다른 참가자에게 그대로 전달 (자리 번호를 보낸 사람으로 고쳐서)
            QByteArray fwd = data;
            fwd[1] = static_cast<char>(fromSlot);
            for (int i = 1; i < MAX_SEATS; ++i)
                if (i != fromSlot && m_peers[i].used) sendRawTo(i, fwd);
        }
        break;
    }

    // ── 게임 선택 동기화 ───────────────────────────────────
    case MSG_LOAD_GAME: {
        if (m_isHost || data.size() < 3) break;
        m_inputDelay = qBound(0, int(static_cast<uint8_t>(data[1])), 8);
        const int mask = static_cast<uint8_t>(data[2]);
        const QString romName = QString::fromUtf8(data.mid(3));
        setState(State::Loading);
        emit loadGameReceived(romName, m_inputDelay, mask);
        break;
    }

    // ── 로딩 완료 선언 (참가자 → 호스트) ───────────────────
    case MSG_READY:
        if (m_isHost && data.size() >= 3) {
            int peerRtt = static_cast<uint8_t>(data[1]) | (static_cast<uint8_t>(data[2]) << 8);
            if (peerRtt > 0 && peerRtt < 2000) m_rttMs = std::max(m_rttMs, peerRtt);
            emit readyReceived(fromSlot);
        }
        break;

    // ── 시작 신호 (호스트→참가자) ──────────────────────────
    case MSG_START:
        if (m_isHost) break;
        setState(State::Playing);
        emit startReceived();
        sendCtrl(QByteArray(1, static_cast<char>(MSG_START_ACK)), 3);
        break;

    // ── 시작 확인 (참가자→호스트): 모두 모이면 Playing ──────
    case MSG_START_ACK:
        if (m_isHost && m_waitingStartAck) {
            m_peers[fromSlot].startAcked = true;
            bool all = true;
            for (int i = 1; i < MAX_SEATS; ++i) if (m_peers[i].used && !m_peers[i].startAcked) all = false;
            if (all) {
                m_waitingStartAck = false;
                if (m_state == State::Ready) setState(State::Playing);
            }
        }
        break;

    // ── ROM 목록 ────────────────────────────────────────────
    case MSG_ROMLIST: {
        if (data.size() < 6) break;
        Peer& pr = m_peers[m_isHost ? fromSlot : 0];
        if (pr.romDone) break;
        const int total = static_cast<uint8_t>(data[1]) | (static_cast<uint8_t>(data[2]) << 8);
        const int index = static_cast<uint8_t>(data[3]) | (static_cast<uint8_t>(data[4]) << 8);
        if (total <= 0 || index >= total) break;
        if (pr.romTotal != total) { pr.romChunks.clear(); pr.romTotal = total; }
        pr.romChunks.insert(index, data.mid(5));
        if (pr.romChunks.size() == total) {
            QByteArray all;
            for (auto it = pr.romChunks.constBegin(); it != pr.romChunks.constEnd(); ++it) all += it.value();
            const QByteArray raw = qUncompress(all);
            pr.romDone = true;
            emit romListReceived(m_isHost ? fromSlot : 0,
                                 QString::fromUtf8(raw).split(QLatin1Char('\n'), Qt::SkipEmptyParts));
        }
        break;
    }
    case MSG_ROMLIST_REQ:
        transmitMyRomList();
        break;

    // ── 로비 ────────────────────────────────────────────────
    case MSG_PROFILE: {
        if (data.size() < 5) break;
        int slot = static_cast<uint8_t>(data[1]);
        const int n = static_cast<uint8_t>(data[2]);
        if (data.size() < 4 + n) break;
        const int nl = static_cast<uint8_t>(data[3 + n]);
        if (data.size() < 6 + n + nl) break;
        if (m_isHost) slot = fromSlot;                      // 참가자가 보낸 프로필의 자리는 주소로 정한다
        if (slot < 0 || slot >= MAX_SEATS) break;
        const QString id = QString::fromUtf8(data.mid(3, n));
        const QString nick = QString::fromUtf8(data.mid(4 + n, nl));
        const int level = static_cast<uint8_t>(data[4 + n + nl]);
        const bool acct = static_cast<uint8_t>(data[5 + n + nl]) & 1;
        const QString parts = QString::fromUtf8(data.mid(6 + n + nl));
        emit profileReceived(slot, id, nick, level, acct, parts);
        break;
    }
    case MSG_CHAT: {
        if (data.size() < 5) break;
        const uint16_t seq = static_cast<uint8_t>(data[1]) | (static_cast<uint8_t>(data[2]) << 8);
        int from = static_cast<uint8_t>(data[3]);
        if (m_isHost) from = fromSlot;
        if (from < 0 || from >= MAX_SEATS) break;
        const uint32_t key = (uint32_t(from) << 16) | seq;
        Peer& pr = m_peers[m_isHost ? fromSlot : 0];
        if (pr.chatSeen.contains(key)) break;
        pr.chatSeen.append(key);
        while (pr.chatSeen.size() > 32) pr.chatSeen.removeFirst();
        emit chatReceived(from, QString::fromUtf8(data.mid(4)));
        if (m_isHost) {                                      // 다른 참가자에게 전달 (보낸 자리를 적어서)
            QByteArray fwd = data;
            fwd[3] = static_cast<char>(fromSlot);
            for (int i = 1; i < MAX_SEATS; ++i)
                if (i != fromSlot && m_peers[i].used) { sendRawTo(i, fwd); sendRawTo(i, fwd); sendRawTo(i, fwd); }
        }
        break;
    }
    case MSG_LREADY:
        if (m_isHost && data.size() >= 3)
            emit lobbyReadyReceived(fromSlot, data[1] != 0, static_cast<uint8_t>(data[2]));
        break;
    case MSG_SEATMODE:
        if (m_isHost && data.size() >= 2) emit seatModeReceived(fromSlot, data[1] != 0);
        break;
    case MSG_SEATS:
        if (!m_isHost && data.size() >= 1 + MAX_SEATS)
            emit seatsReceived(static_cast<uint8_t>(data[1]), static_cast<uint8_t>(data[2]), static_cast<uint8_t>(data[3]));
        break;
    case MSG_ROOMINFO:
        if (!m_isHost && data.size() >= 9)
            emit roomInfoReceived(QString::fromUtf8(data.mid(9)), static_cast<uint8_t>(data[1]), static_cast<uint8_t>(data[2]),
                                  QString::fromLatin1(data.mid(3, 6)).trimmed());
        break;

    // ── 호스트의 머신 세팅 ──────────────────────────────────
    case MSG_VARS: {
        if (data.size() < 7 || m_isHost) break;
        const uint8_t gen = static_cast<uint8_t>(data[1]);
        const int total = static_cast<uint8_t>(data[2]) | (static_cast<uint8_t>(data[3]) << 8);
        const int index = static_cast<uint8_t>(data[4]) | (static_cast<uint8_t>(data[5]) << 8);
        if (total <= 0 || index >= total) break;
        if (gen != m_peerVarGen || m_peerVarTotal != total) {
            m_peerVarGen = gen; m_peerVarChunks.clear(); m_peerVarTotal = total; m_peerVarDone = false;
        }
        if (m_peerVarDone) break;
        m_peerVarChunks.insert(index, data.mid(6));
        if (m_peerVarChunks.size() == total) {
            QByteArray all;
            for (auto it = m_peerVarChunks.constBegin(); it != m_peerVarChunks.constEnd(); ++it) all += it.value();
            const QByteArray raw = qUncompress(all);
            m_peerVarDone = true;
            emit varsReceived(QString::fromUtf8(raw).split(QLatin1Char('\n'), Qt::SkipEmptyParts));
        }
        break;
    }

    // ── 게임 종료 ──────────────────────────────────────────
    case MSG_GAME_OVER:
        if (m_isHost) {                                      // 참가자가 끝냈다 → 다른 참가자에게도 알린다
            for (int i = 1; i < MAX_SEATS; ++i)
                if (i != fromSlot && m_peers[i].used)
                    for (int k = 0; k < 3; ++k) sendRawTo(i, QByteArray(1, static_cast<char>(MSG_GAME_OVER)));
        }
        cleanupGame();
        emit gameOverReceived();
        break;

    // ── GGPO 체크섬 수신 (desync 감지) ─────────────────────
    case MSG_CHECKSUM: {
        if (data.size() < 9) break;
        emit checksumReceived(fromSlot, rd32(data, 1), rd32(data, 5));
        break;
    }

    // ── 재동기 요청 (참가자→호스트) ────────────────────────
    case MSG_RESYNC_REQ: {
        if (data.size() < 5) break;
        if (m_isHost) emit resyncRequested(fromSlot, rd32(data, 1));
        break;
    }

    // ── 상태 청크 ──────────────────────────────────────────
    case MSG_STATE: {
        if (data.size() < 9 || m_isHost) break;
        uint32_t frame = rd32(data, 1);
        uint16_t total = static_cast<uint8_t>(data[5]) | (static_cast<uint8_t>(data[6]) << 8);
        uint16_t idx   = static_cast<uint8_t>(data[7]) | (static_cast<uint8_t>(data[8]) << 8);

        // ★ chunkBuf 스래싱 방지: 더 "최신" 프레임이 올 때만 버퍼를 새로 시작.
        if (frame > m_chunkBuf.frame || m_chunkBuf.totalChunks == 0) {
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
            break;
        }
        m_chunkBuf.chunks[idx] = data.mid(9);

        if (static_cast<int>(m_chunkBuf.chunks.size()) == total) {
            QByteArray assembled;
            assembled.reserve(total * CHUNK_SIZE);
            for (uint16_t k = 0; k < total; ++k)
                assembled.append(m_chunkBuf.chunks.value(k));
            m_chunkBuf.chunks.clear();
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

    case MSG_HEARTBEAT:
        break;

    default:
        qWarning() << "NetplayManager: 알 수 없는 패킷 0x"
                   << Qt::hex << static_cast<int>(type);
        break;
    }
}

// ── Rollback API (자리별) ────────────────────────────────────
void NetplayManager::setPlayers(int playerMask, int mySeat) {
    QMutexLocker lk(&m_mutex);
    m_playerMask = playerMask;
    m_mySeat = mySeat;
}

int16_t NetplayManager::getRemoteInput(uint32_t frame, int seat) const {
    if (seat < 0 || seat >= MAX_SEATS) return 0;
    QMutexLocker lk(&m_mutex);
    auto it = m_remoteInputs[seat].find(frame);
    if (it != m_remoteInputs[seat].end()) return static_cast<int16_t>(it.value());
    return static_cast<int16_t>(m_lastRemote[seat]);      // hold-last 예측
}

void NetplayManager::recordPrediction(uint32_t frame, int seat, uint16_t bits) {
    if (seat < 0 || seat >= MAX_SEATS) return;
    QMutexLocker lk(&m_mutex);
    m_predicted[seat][frame] = bits;
}

int NetplayManager::getRollbackFrame(uint32_t currentFrame) const {
    QMutexLocker lk(&m_mutex);
    int rollbackTo = -1;
    uint32_t earliest = (currentFrame > static_cast<uint32_t>(MAX_ROLLBACK))
                        ? currentFrame - MAX_ROLLBACK : 0;
    for (int s = 0; s < MAX_SEATS; ++s) {
        if (s == m_mySeat || !(m_playerMask & (1 << s))) continue;
        for (uint32_t f = earliest; f < currentFrame; ++f) {
            auto ri = m_remoteInputs[s].find(f);
            auto pi = m_predicted[s].find(f);
            if (ri == m_remoteInputs[s].end() || pi == m_predicted[s].end()) continue;
            if (ri.value() != pi.value()) {
                if (rollbackTo < 0 || static_cast<int>(f) < rollbackTo) rollbackTo = static_cast<int>(f);
                break;
            }
        }
    }
    return rollbackTo;
}

void NetplayManager::confirmFramesUpTo(uint32_t frame) {
    QMutexLocker lk(&m_mutex);
    uint32_t cutoff = (frame > static_cast<uint32_t>(MAX_ROLLBACK + 2))
                      ? frame - MAX_ROLLBACK - 2 : 0;
    for (int s = 0; s < MAX_SEATS; ++s) {
        QList<uint32_t> toRemove;
        for (auto it = m_remoteInputs[s].begin(); it != m_remoteInputs[s].end(); ++it)
            if (it.key() < cutoff) toRemove.append(it.key());
        for (auto k : toRemove) { m_remoteInputs[s].remove(k); m_predicted[s].remove(k); }
    }
}

// 나 말고 입력을 내는 자리들 중 가장 느린 쪽의 프레임
uint32_t NetplayManager::remoteMaxFrame() const {
    QMutexLocker lk(&m_mutex);
    uint32_t m = 0; bool any = false;
    for (int s = 0; s < MAX_SEATS; ++s) {
        if (s == m_mySeat || !(m_playerMask & (1 << s))) continue;
        if (!any || m_remoteMaxFrame[s] < m) m = m_remoteMaxFrame[s];
        any = true;
    }
    return any ? m : 0;
}

// ── 게임 데이터 리셋 ─────────────────────────────────────────
void NetplayManager::resetGameState() {
    QMutexLocker lk(&m_mutex);
    for (int s = 0; s < MAX_SEATS; ++s) {
        m_remoteInputs[s].clear();
        m_predicted[s].clear();
        m_lastRemote[s] = 0;
        m_remoteMaxFrame[s] = 0;
    }
    m_localInputHistory.clear();
    m_chunkBuf.chunks.clear();
    m_chunkBuf.frame       = 0;
    m_chunkBuf.totalChunks = 0;
    m_waitingStartAck      = false;
    m_stateTxQueue.clear();
    if (m_stateTxTimer) m_stateTxTimer->stop();
}
