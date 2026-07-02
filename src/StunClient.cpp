// StunClient.cpp — STUN(RFC 5389) Binding Request/Response 헬퍼 구현
//
//  STUN 패킷 포맷 (RFC 5389)
//  ─────────────────────────────────────────────────────────────
//   [0..1]   Message Type      (0x0001=Binding Request, 0x0101=Binding Response)
//   [2..3]   Message Length    (속성 부분 길이, big-endian)
//   [4..7]   Magic Cookie      (0x2112A442 고정)
//   [8..19]  Transaction ID    (12 bytes, 요청 시 랜덤)
//   [20..]   Attributes (TLV)
//
//  XOR-MAPPED-ADDRESS (0x0020) attribute body:
//   [0]   Reserved (0)
//   [1]   Family   (0x01=IPv4, 0x02=IPv6)
//   [2..3] X-Port  (port XOR (magic_cookie >> 16))
//   [4..7] X-Addr  (IPv4 addr XOR magic_cookie)
//
//  MAPPED-ADDRESS (0x0001) — 구형 호환, XOR 없음.

#include "StunClient.h"
#include <QRandomGenerator>
#include <QHostAddress>

namespace {
    constexpr quint16 STUN_BINDING_REQUEST  = 0x0001;
    constexpr quint16 STUN_BINDING_RESPONSE = 0x0101;
    constexpr quint32 STUN_MAGIC_COOKIE     = 0x2112A442;

    constexpr quint16 STUN_ATTR_MAPPED_ADDRESS     = 0x0001;
    constexpr quint16 STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;

    inline void putU16(QByteArray& b, quint16 v) {
        b.append(static_cast<char>((v >> 8) & 0xFF));
        b.append(static_cast<char>( v       & 0xFF));
    }
    inline void putU32(QByteArray& b, quint32 v) {
        b.append(static_cast<char>((v >> 24) & 0xFF));
        b.append(static_cast<char>((v >> 16) & 0xFF));
        b.append(static_cast<char>((v >>  8) & 0xFF));
        b.append(static_cast<char>( v        & 0xFF));
    }
    inline quint16 readU16(const QByteArray& b, int off) {
        return (static_cast<quint8>(b[off]) << 8) |
                static_cast<quint8>(b[off + 1]);
    }
    inline quint32 readU32(const QByteArray& b, int off) {
        return (static_cast<quint32>(static_cast<quint8>(b[off    ])) << 24) |
               (static_cast<quint32>(static_cast<quint8>(b[off + 1])) << 16) |
               (static_cast<quint32>(static_cast<quint8>(b[off + 2])) <<  8) |
                static_cast<quint32>(static_cast<quint8>(b[off + 3]));
    }
}

QByteArray StunClient::buildBindingRequest(QByteArray& outTxnId) {
    // 12바이트 랜덤 트랜잭션 ID
    outTxnId.resize(12);
    for (int i = 0; i < 12; ++i)
        outTxnId[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));

    QByteArray pkt;
    pkt.reserve(20);
    putU16(pkt, STUN_BINDING_REQUEST);  // type
    putU16(pkt, 0);                     // length = 0 (속성 없음)
    putU32(pkt, STUN_MAGIC_COOKIE);     // magic
    pkt.append(outTxnId);               // txnId
    return pkt;
}

bool StunClient::isStunResponse(const QByteArray& data) {
    if (data.size() < 20) return false;
    // 첫 2비트는 항상 0 (STUN 메시지 마커) → 첫 바이트 < 0x40
    if ((static_cast<quint8>(data[0]) & 0xC0) != 0) return false;
    quint16 type = readU16(data, 0);
    if (type != STUN_BINDING_RESPONSE) return false;
    if (readU32(data, 4) != STUN_MAGIC_COOKIE) return false;
    return true;
}

bool StunClient::parseBindingResponse(const QByteArray& data,
                                       const QByteArray& txnId,
                                       QString&          outIp,
                                       int&              outPort)
{
    if (!isStunResponse(data)) return false;

    // 트랜잭션 ID 매칭
    if (txnId.size() != 12) return false;
    if (data.mid(8, 12) != txnId) return false;

    int msgLen = readU16(data, 2);
    if (20 + msgLen > data.size()) return false;

    // 속성 순회
    int p = 20;
    const int end = 20 + msgLen;
    while (p + 4 <= end) {
        quint16 attrType = readU16(data, p);
        quint16 attrLen  = readU16(data, p + 2);
        int     body     = p + 4;
        if (body + attrLen > end) return false;

        if (attrType == STUN_ATTR_XOR_MAPPED_ADDRESS && attrLen >= 8) {
            // [0]=reserved, [1]=family, [2..3]=xport, [4..7]=xaddr (IPv4)
            quint8 family = static_cast<quint8>(data[body + 1]);
            if (family == 0x01) {  // IPv4
                quint16 xport = readU16(data, body + 2);
                quint32 xaddr = readU32(data, body + 4);
                outPort = xport ^ static_cast<quint16>(STUN_MAGIC_COOKIE >> 16);
                quint32 addr = xaddr ^ STUN_MAGIC_COOKIE;
                outIp = QHostAddress(addr).toString();
                return true;
            }
        }
        else if (attrType == STUN_ATTR_MAPPED_ADDRESS && attrLen >= 8) {
            // 구형 — XOR 없음
            quint8 family = static_cast<quint8>(data[body + 1]);
            if (family == 0x01) {
                outPort = readU16(data, body + 2);
                quint32 addr = readU32(data, body + 4);
                outIp = QHostAddress(addr).toString();
                return true;
            }
        }
        // 4바이트 정렬 패딩
        int padded = (attrLen + 3) & ~3;
        p = body + padded;
    }
    return false;
}
