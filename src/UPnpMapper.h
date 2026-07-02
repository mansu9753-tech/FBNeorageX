#pragma once
// UPnpMapper.h — UPnP/IGD 자동 포트 매핑 (NAT 홀펀칭)
//
//  흐름:
//   1. SSDP M-SEARCH (UDP 멀티캐스트 239.255.255.250:1900) → 공유기 발견
//   2. HTTP GET description URL → WANIPConnection controlURL 파싱
//   3. SOAP AddPortMapping → 공유기에 UDP 포트 개방 요청
//
//  Qt Core/Network 만 사용 (외부 라이브러리 불필요)

#include <QObject>
#include <QUdpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

class UPnpMapper : public QObject {
    Q_OBJECT
public:
    explicit UPnpMapper(QObject* parent = nullptr);
    ~UPnpMapper() override;

    // 비동기 포트 매핑 요청
    //   externalPort : 공유기 외부 포트
    //   localIp      : 이 PC 의 LAN IP (로컬 IP)
    //   internalPort : 내부 포트 (기본값 = externalPort)
    void map(int externalPort, const QString& localIp, int internalPort = -1);

    void cancel();

signals:
    void mapped(int externalPort);       // 성공
    void failed(const QString& reason);  // 실패

private slots:
    void onSsdpReadyRead();
    void onSsdpTimeout();

private:
    void fetchDescription(const QString& locationUrl);
    bool parseDescription(const QByteArray& xml,
                          const QString&    locationUrl,
                          QString&          outCtrlUrl,
                          QString&          outSvcType);
    void sendAddPortMapping(const QString& ctrlUrl, const QString& svcType);

    QUdpSocket*            m_ssdpSock  = nullptr;
    QTimer*                m_ssdpTimer = nullptr;
    QNetworkAccessManager* m_nam       = nullptr;

    int     m_extPort = 0;
    int     m_intPort = 0;
    QString m_localIp;
    bool    m_done    = false;
};
