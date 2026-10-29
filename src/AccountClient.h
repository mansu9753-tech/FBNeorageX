#pragma once
// AccountClient.h — 계정 서버(아이디·비밀번호 로그인, 레벨·아바타, 세트 승리 보고)와 통신
//
//  서버 주소는 설정(server_url)에 있다. 비어 있으면 계정 기능을 쓸 수 없고 게스트로만 논다.
//  비밀번호는 서버로 보낼 때만 쓰고 저장하지 않는다. 로그인에 성공하면 서버가 준 로그인 표(token)만 저장한다.

#include <QObject>
#include <QVector>
#include <functional>
#include <QString>

#include "Avatar.h"

class QNetworkAccessManager;
class QNetworkReply;

class AccountClient : public QObject {
    Q_OBJECT
public:
    struct Profile {
        QString id, nick;
        int exp = 0, level = 1, into = 0, need = 2;
        avatar::Parts parts;
        bool valid() const { return !id.isEmpty(); }
    };

    struct RoomInfo {
        QString code, host, hostId, game, gameName;
        int level = 1, seats = 1, max = 2;
        bool playing = false;
    };

    explicit AccountClient(QObject* parent = nullptr);

    QString serverUrl() const;
    bool    configured() const { return !serverUrl().isEmpty(); }
    bool    loggedIn() const { return m_me.valid(); }
    const Profile& me() const { return m_me; }

    void registerAccount(const QString& id, const QString& pw, const QString& nick);
    void login(const QString& id, const QString& pw);
    void restore();                          // 저장된 로그인 표로 조용히 다시 로그인
    void logout();
    void saveProfile(const QString& nick, const avatar::Parts& parts);
    void setLocalParts(const avatar::Parts& p) { m_me.parts = p; emit profileChanged(); }   // 서버 응답 전에 화면을 먼저 바꾼다
    void lookup(const QString& id);
    void fetchRooms();                                  // 로비의 방 목록
    void openRoom(const QString& code, const QString& game, const QString& gameName, int seats, int maxPlayers, bool playing, bool update);
    void closeRoom(const QString& code);          // 상대 프로필 확인 (공개 정보)
    void reportResult(const QString& room, int seq, const QString& game,
                      const QString& winner, const QString& loser);

signals:
    void authDone(bool ok, const QString& message);       // 가입·로그인·복원 결과
    void profileChanged();                                // 내 프로필이 바뀌었다 (로그인, 경험치 등)
    void userLooked(const AccountClient::Profile& p);     // lookup 결과
    void roomsFetched(bool ok, const QVector<AccountClient::RoomInfo>& rooms);
    void resultReported(int awarded, bool leveledUp);

private:
    void post(const QString& path, const QJsonObject& body, std::function<void(const QJsonObject&, int status)> done,
              bool auth = true);
    void applyProfile(const QJsonObject& o);
    static Profile parseProfile(const QJsonObject& o);

    QNetworkAccessManager* m_nam = nullptr;
    QString m_token;
    Profile m_me;
};

Q_DECLARE_METATYPE(AccountClient::Profile)
Q_DECLARE_METATYPE(QVector<AccountClient::RoomInfo>)
