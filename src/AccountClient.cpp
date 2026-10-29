// AccountClient.cpp

#include "AccountClient.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <functional>

#include "AppSettings.h"

AccountClient::AccountClient(QObject* parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    qRegisterMetaType<AccountClient::Profile>();
    qRegisterMetaType<QVector<AccountClient::RoomInfo>>();
    m_token = gSettings.accountToken;
}

QString AccountClient::serverUrl() const {
    QString u = gSettings.effectiveServer();
    while (u.endsWith('/')) u.chop(1);
    return u;
}

AccountClient::Profile AccountClient::parseProfile(const QJsonObject& o) {
    Profile p;
    p.id    = o["id"].toString();
    p.nick  = o["nick"].toString(p.id);
    p.exp   = o["exp"].toInt();
    p.level = qBound(1, o["level"].toInt(1), avatar::kMaxLevel);
    p.into  = o["into"].toInt();
    p.need  = o["need"].toInt();
    const QJsonObject a = o["avatar"].toObject();
    for (auto it = a.begin(); it != a.end(); ++it) p.parts[it.key()] = it.value().toString();
    return p;
}

void AccountClient::applyProfile(const QJsonObject& o) {
    m_me = parseProfile(o);
    gSettings.accountId = m_me.id;
    gSettings.save();
    emit profileChanged();
}

// 서버에 JSON 을 보낸다. done 은 응답(또는 실패 시 빈 객체, status 0)과 함께 불린다.
void AccountClient::post(const QString& path, const QJsonObject& body,
                         std::function<void(const QJsonObject&, int)> done, bool auth) {
    if (!configured()) { if (done) done(QJsonObject{ { "error", "server not set" } }, 0); return; }
    QNetworkRequest req(QUrl(serverUrl() + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(10000);
    if (auth && !m_token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    QNetworkReply* r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [r, done] {
        const int st = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        r->deleteLater();
        if (done) done(o, st);
    });
}

void AccountClient::registerAccount(const QString& id, const QString& pw, const QString& nick) {
    post("/api/register", { { "id", id }, { "pw", pw }, { "nick", nick } }, [this](const QJsonObject& o, int st) {
        if (o["ok"].toBool()) {
            m_token = o["token"].toString();
            gSettings.accountToken = m_token;
            applyProfile(o["profile"].toObject());
            emit authDone(true, QString());
        } else {
            emit authDone(false, o["error"].toString(st == 0 ? QStringLiteral("서버에 연결할 수 없습니다") : QStringLiteral("가입 실패")));
        }
    }, false);
}

void AccountClient::login(const QString& id, const QString& pw) {
    post("/api/login", { { "id", id }, { "pw", pw } }, [this](const QJsonObject& o, int st) {
        if (o["ok"].toBool()) {
            m_token = o["token"].toString();
            gSettings.accountToken = m_token;
            applyProfile(o["profile"].toObject());
            emit authDone(true, QString());
        } else {
            emit authDone(false, o["error"].toString(st == 0 ? QStringLiteral("서버에 연결할 수 없습니다") : QStringLiteral("로그인 실패")));
        }
    }, false);
}

void AccountClient::restore() {
    if (m_token.isEmpty() || !configured()) return;
    post("/api/me", QJsonObject(), [this](const QJsonObject& o, int st) {
        if (o["ok"].toBool()) {
            applyProfile(o["profile"].toObject());
            emit authDone(true, QString());
        } else if (st == 401) {                       // 로그인 표가 만료됐다
            m_token.clear();
            gSettings.accountToken.clear();
            gSettings.save();
            m_me = Profile();
            emit profileChanged();
            emit authDone(false, QStringLiteral("로그인이 만료되었습니다"));
        }                                             // 연결이 안 될 때는 조용히 넘어간다 (게스트로 계속)
    });
}

void AccountClient::logout() {
    if (!m_token.isEmpty()) post("/api/logout", QJsonObject(), nullptr);
    m_token.clear();
    gSettings.accountToken.clear();
    gSettings.accountId.clear();
    gSettings.save();
    m_me = Profile();
    emit profileChanged();
}

void AccountClient::saveProfile(const QString& nick, const avatar::Parts& parts) {
    QJsonObject av;
    for (auto it = parts.begin(); it != parts.end(); ++it) av[it.key()] = it.value();
    post("/api/profile", { { "nick", nick }, { "avatar", av } }, [this](const QJsonObject& o, int) {
        if (o["ok"].toBool()) applyProfile(o["profile"].toObject());
    });
}

void AccountClient::lookup(const QString& id) {
    if (!configured() || id.isEmpty()) return;
    QNetworkRequest req(QUrl(serverUrl() + "/api/user/" + QString::fromLatin1(QUrl::toPercentEncoding(id))));
    req.setTransferTimeout(8000);
    QNetworkReply* r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        r->deleteLater();
        if (o["ok"].toBool()) emit userLooked(parseProfile(o["profile"].toObject()));
    });
}

void AccountClient::reportResult(const QString& room, int seq, const QString& game,
                                 const QString& winner, const QString& loser) {
    if (!loggedIn()) return;
    post("/api/result", { { "room", room }, { "seq", seq }, { "game", game }, { "winner", winner }, { "loser", loser } },
         [this](const QJsonObject& o, int) {
             if (!o["ok"].toBool()) return;
             const int a = o["awarded"].toInt();
             if (o.contains("profile")) applyProfile(o["profile"].toObject());
             if (a > 0) emit resultReported(a, o["leveledUp"].toBool());
         });
}

// ── 방 목록 (로비) ───────────────────────────────────────────
void AccountClient::fetchRooms() {
    if (!configured() || m_token.isEmpty()) { emit roomsFetched(false, {}); return; }
    QNetworkRequest req(QUrl(serverUrl() + "/api/rooms"));
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    req.setTransferTimeout(8000);
    QNetworkReply* r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        r->deleteLater();
        QVector<RoomInfo> out;
        if (o["ok"].toBool()) {
            for (const QJsonValue& v : o["rooms"].toArray()) {
                const QJsonObject ro = v.toObject();
                RoomInfo ri;
                ri.code = ro["code"].toString(); ri.host = ro["host"].toString(); ri.hostId = ro["hostId"].toString();
                ri.game = ro["game"].toString(); ri.gameName = ro["gameName"].toString();
                ri.level = ro["level"].toInt(1); ri.seats = ro["seats"].toInt(1); ri.max = ro["max"].toInt(2);
                ri.playing = ro["playing"].toBool();
                out.append(ri);
            }
        }
        emit roomsFetched(o["ok"].toBool(), out);
    });
}

void AccountClient::openRoom(const QString& code, const QString& game, const QString& gameName, int seats, int maxPlayers, bool playing, bool update) {
    post(update ? "/api/room/update" : "/api/room/open",
         { { "code", code }, { "game", game }, { "gameName", gameName }, { "seats", seats }, { "max", maxPlayers }, { "playing", playing } },
         nullptr);
}

void AccountClient::closeRoom(const QString& code) {
    post("/api/room/close", { { "code", code } }, nullptr);
}
