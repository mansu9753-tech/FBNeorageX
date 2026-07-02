#pragma once
// StunClient.h — STUN(RFC 5389) Binding Request/Response 헬퍼
//
//  역할
//  ─────────────────────────────────────────────────────────────
//   외부 STUN 서버(stun.l.google.com:19302 등)에 보낼 Binding Request 를
//   만들고, 응답에서 XOR-MAPPED-ADDRESS(외부 IP:Port) 를 파싱한다.
//   네트워킹은 직접 하지 않는다 — NetplayManager 가 자신의 UDP 소켓을
//   통해 송수신하면서 이 헬퍼만 호출.
//
//  ★ 동일 소켓 사용 이유
//   STUN 으로 알아낸 외부 포트가 실제 게임 트래픽에 사용되려면, STUN
//   요청도 반드시 "게임 소켓"으로 보내야 함. (NAT 매핑은 소켓 단위)
//
//  헤더 전용 정적 메서드 모음 (인스턴스 없음)

#include <QByteArray>
#include <QString>

class StunClient {
public:
    // STUN Binding Request 패킷 생성.
    //   - outTxnId : 12바이트 트랜잭션 ID(랜덤). 응답 매칭에 사용.
    // 반환: 20바이트 STUN 헤더(속성 없음)
    static QByteArray buildBindingRequest(QByteArray& outTxnId);

    // 첫 4바이트와 magic cookie 만으로 STUN 응답 여부 빠르게 판별.
    // (게임 패킷(MSG_* 1바이트)과 충돌 없음 — STUN 응답은 항상 20+ 바이트
    //  이고 첫 비트가 0이며 magic cookie 0x2112A442 가 있음.)
    static bool isStunResponse(const QByteArray& data);

    // Binding Response 파싱 → XOR-MAPPED-ADDRESS(또는 MAPPED-ADDRESS) 추출.
    //   - data    : 수신 패킷 전체
    //   - txnId   : 요청 시 사용한 12바이트 트랜잭션 ID (매칭용)
    //   - outIp   : 외부 IPv4 주소 문자열
    //   - outPort : 외부 포트
    // 반환: 성공 여부
    static bool parseBindingResponse(const QByteArray& data,
                                     const QByteArray& txnId,
                                     QString&          outIp,
                                     int&              outPort);
};
