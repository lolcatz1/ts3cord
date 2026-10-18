#pragma once
// discord state as the ui sees it, fed by core events
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVector>

struct User {
    QString id, username, globalName, display, avatarUrl;
    bool bot = false;
    static User from(const QJsonObject &o);
    QString name() const { return !display.isEmpty() ? display : !globalName.isEmpty() ? globalName : username; }
};

enum ChannelType { GuildText = 0, DM = 1, GuildVoice = 2, GroupDM = 3, Category = 4, Announcement = 5, Stage = 13, Forum = 15, Media = 16 };

struct Channel {
    QString id, guildId, name, parentId, topic, lastMessageId;
    int type = 0, position = 0, bitrate = 0, userLimit = 0, mentions = 0;
    bool nsfw = false, canView = true, canSend = true, canConnect = true, unread = false, muted = false;
    QVector<User> recipients;
    static Channel from(const QJsonObject &o);
    bool isVoice() const { return type == GuildVoice || type == Stage; }
    bool isCategory() const { return type == Category; }
    bool isText() const { return type == GuildText || type == Announcement || type == DM || type == GroupDM; }
};

struct VoiceState {
    QString guildId, channelId;
    User user;
    bool selfMute = false, selfDeaf = false, mute = false, deaf = false, streaming = false, video = false, suppress = false;
    static VoiceState from(const QJsonObject &o);
};

struct Attachment { QString filename, url, contentType; qint64 size = 0; int width = 0, height = 0; };
struct Embed { QString title, description, url, author, footer; };

struct Message {
    QString id, channelId, guildId, content, authorColor, nonce;
    int type = 0;
    User author;
    QDateTime timestamp, edited;
    bool mentionsMe = false, pending = false;
    QVector<Attachment> attachments;
    QVector<Embed> embeds;
    QString replyId, replyAuthor, replyContent;
    static Message from(const QJsonObject &o);
};

struct Role { QString id, name, color; int position = 0; bool hoist = false; };

struct Guild {
    QString id, name, iconUrl, ownerId, description, bannerUrl;
    int memberCount = 0;
    bool loaded = false;
    QMap<QString, Channel> channels;
    QMap<QString, VoiceState> voice; // user id -> state
    QVector<Role> roles;
    QString myNick;
    QStringList myRoles;
};

class Store : public QObject {
    Q_OBJECT
public:
    explicit Store(QObject *parent = nullptr) : QObject(parent) {}
    void handle(const QString &ev, const QJsonObject &o);
    void setFriends(const QJsonArray &list);

    User me;
    bool ready = false;
    QVector<QString> guildOrder;
    QHash<QString, Guild> guilds;
    QMap<QString, Channel> dms;
    QVector<QPair<User, int>> friends;
    QSet<QString> blocked, ignored;             // user ids
    QHash<QString, QVector<Message>> messages; // channel id -> oldest first
    QHash<QString, bool> hasMore;
    QSet<QString> speaking;                     // user ids
    QHash<QString, QString> presence;           // user id -> status
    QHash<QString, QString> activity;           // user id -> activity text
    QString voiceGuild, voiceChannel, voiceState = "disconnected";
    int ping = 0;

    const Channel *channel(const QString &id) const;
    QString guildOf(const QString &channelId) const;
    const VoiceState *myVoice() const;
    // own name as shown in a guild (nick first)
    QString myName(const QString &guildId) const;
    // blocked / ignored author or muted dm: no popups, sounds or pokes
    bool silenced(const Message &m) const;
    bool silencedUser(const QString &userId) const { return blocked.contains(userId) || ignored.contains(userId); }

signals:
    void needLogin();
    void loginError(const QString &msg);
    void mfaRequired(const QString &ticket, const QStringList &methods);
    void qrCode(const QString &url);
    void qrUser(const QString &name, const QString &avatar);
    void readyChanged();
    void connectionChanged(const QString &state, const QString &msg);
    void guildChanged(const QString &guildId);    // snapshot, channel list, info
    void guildRemoved(const QString &guildId);
    void voiceChanged(const QString &guildId, const QString &userId, const QString &oldChannel, const QString &newChannel);
    void speakingChanged(const QString &userId, bool on);
    void messagesLoaded(const QString &channelId);
    void messageAdded(const Message &m);
    void messageUpdated(const Message &m);
    void messageRemoved(const QString &channelId, const QString &messageId);
    void typing(const QString &channelId, const User &u);
    void voiceStatus(const QString &state, const QString &msg);
    void presenceChanged(const QString &userId);
    void profile(const QJsonObject &o);
    void dmOpened(const QString &channelId);
    void unreadChanged(const QString &channelId);
    void error(const QString &msg);
};
