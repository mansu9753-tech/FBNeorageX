// UPnpMapper.cpp — UPnP/IGD 자동 포트 매핑
#include "UPnpMapper.h"
#include <QNetworkRequest>
#include <QXmlStreamReader>
#include <QHostAddress>
#include <QDebug>

static const char kSsdpAddr[] = "239.255.255.250";
static const int  kSsdpPort   = 1900;
static const int  kSsdpTimeoutMs = 5000;

UPnpMapper::UPnpMapper(QObject* parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

UPnpMapper::~UPnpMapper() { cancel(); }

// ── 공개 API ──────────────────────────────────────────────────
void UPnpMapper::map(int externalPort, const QString& localIp, int internalPort)
{
    cancel();
    m_done    = false;
    m_extPort = externalPort;
    m_intPort = (internalPort < 0) ? externalPort : internalPort;
    m_localIp = localIp;

    m_ssdpSock = new QUdpSocket(this);
    if (!m_ssdpSock->bind(QHostAddress::Any, 0)) {
        emit failed("UPnP: UDP 소켓 바인드 실패");
        return;
    }
    connect(m_ssdpSock, &QUdpSocket::readyRead,
            this,       &UPnpMapper::onSsdpReadyRead);

    m_ssdpTimer = new QTimer(this);
    m_ssdpTimer->setSingleShot(true);
    m_ssdpTimer->setInterval(kSsdpTimeoutMs);
    connect(m_ssdpTimer, &QTimer::timeout, this, &UPnpMapper::onSsdpTimeout);
    m_ssdpTimer->start();

    // WANIPConnection:1  — 일반 공유기
    // WANPPPConnection:1 — PPPoE 공유기 (KT 등)
    // InternetGatewayDevice:1 — 폴백
    static const char* kSvcs[] = {
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        nullptr
    };
    QHostAddress ssdpGroup(QString::fromLatin1(kSsdpAddr));
    for (int i = 0; kSvcs[i]; ++i) {
        QByteArray pkt =
            QByteArray("M-SEARCH * HTTP/1.1\r\n")
            + "HOST: 239.255.255.250:1900\r\n"
            + "MAN: \"ssdp:discover\"\r\n"
            + "ST: " + kSvcs[i] + "\r\n"
            + "MX: 3\r\n\r\n";
        m_ssdpSock->writeDatagram(pkt, ssdpGroup, static_cast<quint16>(kSsdpPort));
    }
    qDebug() << "UPnpMapper: SSDP M-SEARCH 전송";
}

void UPnpMapper::cancel()
{
    if (m_ssdpTimer) {
        m_ssdpTimer->stop();
        m_ssdpTimer->deleteLater();
        m_ssdpTimer = nullptr;
    }
    if (m_ssdpSock) {
        m_ssdpSock->close();
        m_ssdpSock->deleteLater();
        m_ssdpSock = nullptr;
    }
}

// ── SSDP 응답 수신 ────────────────────────────────────────────
void UPnpMapper::onSsdpReadyRead()
{
    if (m_done || !m_ssdpSock) return;

    while (m_ssdpSock->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_ssdpSock->pendingDatagramSize()));
        m_ssdpSock->readDatagram(data.data(), data.size());

        QString resp = QString::fromLatin1(data);
        QString location;
        for (const QString& line : resp.split("\r\n")) {
            if (line.startsWith("LOCATION:", Qt::CaseInsensitive)) {
                location = line.mid(9).trimmed();
                break;
            }
        }
        if (location.isEmpty()) continue;

        qDebug() << "UPnpMapper: SSDP 응답 LOCATION=" << location;
        m_done = true;
        if (m_ssdpTimer) m_ssdpTimer->stop();
        cancel();
        fetchDescription(location);
        return;
    }
}

void UPnpMapper::onSsdpTimeout()
{
    if (m_done) return;
    m_done = true;
    cancel();
    emit failed(QString("UPnP: 공유기가 UPnP 를 지원하지 않거나 비활성화됨\n"
                        "공유기 설정에서 UDP %1 포트포워딩을 직접 설정해주세요.")
                .arg(m_extPort));
}

// ── HTTP Description 가져오기 ──────────────────────────────────
void UPnpMapper::fetchDescription(const QString& locationUrl)
{
    // vexing-parse 방지: QUrl 변수를 먼저 선언
    QUrl     descUrl(locationUrl);
    QNetworkRequest req;
    req.setUrl(descUrl);
    req.setTransferTimeout(6000);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, locationUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("UPnP: 디바이스 설명 가져오기 실패 — " + reply->errorString());
            return;
        }
        QByteArray xml = reply->readAll();
        QString ctrlUrl, svcType;
        if (!parseDescription(xml, locationUrl, ctrlUrl, svcType)) {
            emit failed("UPnP: WANIPConnection/WANPPPConnection 서비스 없음\n"
                        "공유기 설정에서 수동 포트포워딩을 해주세요.");
            return;
        }
        qDebug() << "UPnpMapper: controlURL=" << ctrlUrl << " svcType=" << svcType;
        sendAddPortMapping(ctrlUrl, svcType);
    });
}

// ── XML 파싱 — WANIPConnection controlURL 추출 ─────────────────
bool UPnpMapper::parseDescription(const QByteArray& xml,
                                   const QString&    locationUrl,
                                   QString&          outCtrlUrl,
                                   QString&          outSvcType)
{
    QUrl loc(locationUrl);
    QString base = loc.scheme() + "://" + loc.host();
    if (loc.port() > 0 && loc.port() != 80)
        base += ":" + QString::number(loc.port());

    QXmlStreamReader xr(xml);
    QString curSvc;
    bool inService = false;

    while (!xr.atEnd() && !xr.hasError()) {
        xr.readNext();
        if (!xr.isStartElement()) continue;

        QString tag = xr.name().toString().toLower();

        if (tag == "urlbase") {
            QString ub = xr.readElementText().trimmed();
            if (!ub.isEmpty())
                base = ub.endsWith('/') ? ub.left(ub.size() - 1) : ub;
        } else if (tag == "service") {
            inService = true;
            curSvc.clear();
        } else if (inService && tag == "servicetype") {
            curSvc = xr.readElementText().trimmed();
        } else if (inService && tag == "controlurl") {
            if (curSvc.contains("WANIPConnection",  Qt::CaseInsensitive) ||
                curSvc.contains("WANPPPConnection", Qt::CaseInsensitive)) {
                QString ctrl = xr.readElementText().trimmed();
                if (!ctrl.isEmpty()) {
                    if (ctrl.startsWith("http://",  Qt::CaseInsensitive) ||
                        ctrl.startsWith("https://", Qt::CaseInsensitive)) {
                        outCtrlUrl = ctrl;
                    } else {
                        outCtrlUrl = base + (ctrl.startsWith('/') ? ctrl : "/" + ctrl);
                    }
                    outSvcType = curSvc;
                    return true;
                }
            }
        }
    }
    return false;
}

// ── SOAP AddPortMapping ──────────────────────────────────────
void UPnpMapper::sendAddPortMapping(const QString& ctrlUrl, const QString& svcType)
{
    QString svcName = svcType.contains("WANPPPConnection", Qt::CaseInsensitive)
                      ? "WANPPPConnection:1"
                      : "WANIPConnection:1";
    QString urn = "urn:schemas-upnp-org:service:" + svcName;

    QString soap = QString(
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
        " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:AddPortMapping xmlns:u=\"%1\">"
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>%2</NewExternalPort>"
        "<NewProtocol>UDP</NewProtocol>"
        "<NewInternalPort>%3</NewInternalPort>"
        "<NewInternalClient>%4</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>FBNeoRageX</NewPortMappingDescription>"
        "<NewLeaseDuration>3600</NewLeaseDuration>"
        "</u:AddPortMapping>"
        "</s:Body>"
        "</s:Envelope>")
        .arg(urn)
        .arg(m_extPort)
        .arg(m_intPort)
        .arg(m_localIp);

    // vexing-parse 방지: QUrl 변수를 먼저 선언
    QUrl     ctrlQUrl(ctrlUrl);
    QNetworkRequest req;
    req.setUrl(ctrlQUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QByteArray("text/xml; charset=\"utf-8\""));
    req.setRawHeader("SOAPAction",
                     ("\"" + urn + "#AddPortMapping\"").toUtf8());
    req.setTransferTimeout(6000);

    QNetworkReply* reply = m_nam->post(req, soap.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int        httpCode = reply->attribute(
                                  QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body     = reply->readAll();

        if (httpCode == 200 || body.contains("AddPortMappingResponse")) {
            qDebug() << "UPnpMapper: 포트 매핑 성공 port=" << m_extPort;
            emit mapped(m_extPort);
        } else {
            QString fault;
            QXmlStreamReader xr(body);
            while (!xr.atEnd()) {
                xr.readNext();
                if (xr.isStartElement() &&
                    xr.name().toString() == "errorDescription") {
                    fault = xr.readElementText();
                    break;
                }
            }
            if (fault.isEmpty()) fault = QString("HTTP %1").arg(httpCode);
            emit failed("UPnP: 포트 매핑 거부됨 — " + fault +
                        QString("\n수동 포트포워딩이 필요합니다 (UDP %1).").arg(m_extPort));
        }
    });
}
