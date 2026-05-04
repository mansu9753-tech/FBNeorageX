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
#include <QNetworkInterface>
#include <QDebug>
#include <algorithm>

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
    qDebug() << "NetplayManager: 호스트 대기 port=" << port;
}

// ── 클라이언트 연결 ──────────────────────────────────────────
void NetplayManager::clientConnect(const QString& ip, int port) {
    shutdown();
    m_isHost     = false;
    m_remoteAddr = QHostAddress(ip);
    m_remotePort = static_cast<quint16>(port);

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::Any, 0,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit error("UDP 바인드 실패: " + m_socket->errorString());
        m_socket->deleteLater(); m_socket = nullptr;
        return;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &NetplayManager::onReadyRead);

    m_helloRetry = 0;
    m_helloSentAt.start();   // RTT 측정 시작
    m_helloTimer->start();
    onHelloTimer();
    qDebug() << "NetplayManager: HELLO 시작 →" << ip << ":" << port;
}

// ── 완전 종료 (소켓 닫기) ───────────────────────────────────
void NetplayManager::shutdown() {
    m_helloTimer->stop();
    m_heartbeatTimer->stop();
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
void NetplayManager::sendLoadGame(const QString& romName) {
    QByteArray pkt;
    pkt.append(static_cast<char>(MSG_LOAD_GAME));
    pkt.append(romName.toUtf8());
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
void NetplayManager::sendState(uint32_t frame, const QByteArray& data) {
    int total = static_cast<int>((data.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    if (total < 1) total = 1;

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
        sendRaw(pkt);
    }
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
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QHostAddress sender;
        quint16      senderPort = 0;
        QByteArray   data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);
        if (data.isEmpty()) continue;

        // 마지막 수신 시각 갱신 (heartbeat watchdog)
        m_lastRecvClock.start();

        uint8_t type = static_cast<uint8_t>(data[0]);

        // ── UDP 핸드셰이크 ──────────────────────────────────
        if (type == MSG_HELLO) {
            if (m_isHost) {
                if (m_state == State::Disconnected) {
                    m_remoteAddr = sender;
                    m_remotePort = senderPort;
                    setState(State::Lobby);
                    m_heartbeatTimer->start();
                    m_lastRecvClock.start();
                    emit connected(true);
                    qDebug() << "NetplayManager: 클라이언트 연결됨"
                             << sender.toString() << ":" << senderPort;
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
                setState(State::Lobby);
                m_heartbeatTimer->start();
                m_lastRecvClock.start();
                emit connected(false);
                qDebug() << "NetplayManager: 호스트 연결됨 (ACK) RTT=" << m_rttMs << "ms";
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
        QString romName = QString::fromUtf8(data.mid(1));
        setState(State::Loading);
        emit loadGameReceived(romName);
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

        if (m_chunkBuf.frame != frame || m_chunkBuf.totalChunks != total) {
            m_chunkBuf.frame = frame; m_chunkBuf.totalChunks = total;
            m_chunkBuf.chunks.clear();
        }
        m_chunkBuf.chunks[idx] = data.mid(9);

        if (static_cast<int>(m_chunkBuf.chunks.size()) == total) {
            QByteArray assembled;
            assembled.reserve(total * CHUNK_SIZE);
            for (uint16_t k = 0; k < total; ++k)
                assembled.append(m_chunkBuf.chunks.value(k));
            m_chunkBuf.chunks.clear();
            emit stateReceived(frame, assembled);
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
}
