#include "store.h"

#include <QJsonArray>

User User::from(const QJsonObject &o) {
    User u;
    u.id = o["id"].toString();
    u.username = o["username"].toString();
    u.globalName = o["global_name"].toString();
    u.display = o["display"].toString();
    u.avatarUrl = o["avatar_url"].toString();
    u.bot = o["bot"].toBool();
    return u;
}

Channel Channel::from(const QJsonObject &o) {
    Channel c;
    c.id = o["id"].toString();
    c.guildId = o["guild_id"].toString();
    c.name = o["name"].toString();
    c.parentId = o["parent_id"].toString();
    c.topic = o["topic"].toString();
    c.lastMessageId = o["last_message_id"].toString();
    c.type = o["type"].toInt();
    c.position = o["position"].toInt();
    c.bitrate = o["bitrate"].toInt();
    c.userLimit = o["user_limit"].toInt();
    c.mentions = o["mentions"].toInt();
    c.nsfw = o["nsfw"].toBool();
    c.canView = o["can_view"].toBool(true);
    c.canSend = o["can_send"].toBool(true);
    c.canConnect = o["can_connect"].toBool(true);
    c.unread = o["unread"].toBool();
    c.muted = o["muted"].toBool();
    for (auto r : o["recipients"].toArray()) c.recipients << User::from(r.toObject());
    return c;
}

VoiceState VoiceState::from(const QJsonObject &o) {
    VoiceState v;
    v.guildId = o["guild_id"].toString();
    v.channelId = o["channel_id"].toString();
    v.user = User::from(o["user"].toObject());
    v.selfMute = o["self_mute"].toBool();
    v.selfDeaf = o["self_deaf"].toBool();
    v.mute = o["mute"].toBool();
    v.deaf = o["deaf"].toBool();
    v.streaming = o["streaming"].toBool();
    v.video = o["video"].toBool();
    v.suppress = o["suppress"].toBool();
    return v;
}

Message Message::from(const QJsonObject &o) {
    Message m;
    m.id = o["id"].toString();
    m.channelId = o["channel_id"].toString();
    m.guildId = o["guild_id"].toString();
    m.content = o["content"].toString();
    m.authorColor = o["author_color"].toString();
    m.type = o["type"].toInt();
    m.author = User::from(o["author"].toObject());
    m.timestamp = QDateTime::fromString(o["timestamp"].toString(), Qt::ISODateWithMs);
    if (!m.timestamp.isValid()) m.timestamp = QDateTime::fromString(o["timestamp"].toString(), Qt::ISODate);
    auto e = o["edited_timestamp"].toString();
    if (!e.isEmpty()) m.edited = QDateTime::fromString(e, Qt::ISODateWithMs);
    m.mentionsMe = o["mentions_me"].toBool();
    for (auto a : o["attachments"].toArray()) {
        auto x = a.toObject();
        m.attachments << Attachment{x["filename"].toString(), x["url"].toString(), x["content_type"].toString(),
                                    (qint64)x["size"].toDouble(), x["width"].toInt(), x["height"].toInt()};
    }
    for (auto a : o["embeds"].toArray()) {
        auto x = a.toObject();
        m.embeds << Embed{x["title"].toString(), x["description"].toString(), x["url"].toString(), x["author"].toString(), x["footer"].toString()};
    }
    auto r = o["reply_to"].toObject();
    if (!r.isEmpty()) {
        m.replyId = r["id"].toString();
        m.replyAuthor = User::from(r["author"].toObject()).name();
        m.replyContent = r["content"].toString();
    }
    return m;
}

const Channel *Store::channel(const QString &id) const {
    for (auto &g : guilds) {
        auto it = g.channels.find(id);
        if (it != g.channels.end()) return &*it;
    }
    auto it = dms.find(id);
    return it != dms.end() ? &*it : nullptr;
}

QString Store::guildOf(const QString &channelId) const {
    auto c = channel(channelId);
    return c ? c->guildId : QString();
}

const VoiceState *Store::myVoice() const {
    if (voiceGuild.isEmpty() || !guilds.contains(voiceGuild)) return nullptr;
    auto &g = guilds[voiceGuild];
    auto it = g.voice.find(me.id);
    return it != g.voice.end() ? &*it : nullptr;
}

static void applyGuildInfo(Guild &g, const QJsonObject &o) {
    g.id = o["id"].toString();
    g.name = o["name"].toString();
    g.iconUrl = o["icon_url"].toString();
    g.ownerId = o["owner_id"].toString();
    if (o.contains("description")) g.description = o["description"].toString();
    if (o.contains("banner_url")) g.bannerUrl = o["banner_url"].toString();
    if (o.contains("member_count")) g.memberCount = o["member_count"].toInt();
}

void Store::handle(const QString &ev, const QJsonObject &o) {
    if (ev == "need_login") emit needLogin();
    else if (ev == "login_error") emit loginError(o["msg"].toString());
    else if (ev == "mfa_required") {
        QStringList m;
        for (auto x : o["methods"].toArray()) m << x.toString();
        emit mfaRequired(o["ticket"].toString(), m);
    } else if (ev == "qr") emit qrCode(o["url"].toString());
    else if (ev == "qr_user") emit qrUser(o["username"].toString(), o["avatar_url"].toString());
    else if (ev == "ready") {
        me = User::from(o["user"].toObject());
        guildOrder.clear();
        for (auto x : o["guilds"].toArray()) {
            auto go = x.toObject();
            auto &g = guilds[go["id"].toString()];
            applyGuildInfo(g, go);
            guildOrder << g.id;
        }
        dms.clear();
        for (auto x : o["dms"].toArray()) {
            auto c = Channel::from(x.toObject());
            dms[c.id] = c;
        }
        setFriends(o["friends"].toArray());
        ready = true;
        emit readyChanged();
    } else if (ev == "relationships") {
        setFriends(o["friends"].toArray());
        emit presenceChanged({});
    } else if (ev == "connection") emit connectionChanged(o["state"].toString(), o["msg"].toString());
    else if (ev == "guild_snapshot") {
        auto go = o["guild"].toObject();
        auto &g = guilds[go["id"].toString()];
        applyGuildInfo(g, go);
        if (!guildOrder.contains(g.id)) guildOrder << g.id;
        g.channels.clear();
        for (auto x : o["channels"].toArray()) {
            auto c = Channel::from(x.toObject());
            g.channels[c.id] = c;
        }
        g.voice.clear();
        for (auto x : o["voice_states"].toArray()) {
            auto v = VoiceState::from(x.toObject());
            if (!v.channelId.isEmpty()) g.voice[v.user.id] = v;
        }
        g.roles.clear();
        for (auto x : o["roles"].toArray()) {
            auto r = x.toObject();
            g.roles << Role{r["id"].toString(), r["name"].toString(), r["color"].toString(), r["position"].toInt(), r["hoist"].toBool()};
        }
        g.myNick = o["me"]["nick"].toString();
        g.myRoles.clear();
        for (auto x : o["me"]["roles"].toArray()) g.myRoles << x.toString();
        g.loaded = true;
        emit guildChanged(g.id);
    } else if (ev == "guild_create" || ev == "guild_update") {
        auto go = o["guild"].toObject();
        auto &g = guilds[go["id"].toString()];
        applyGuildInfo(g, go);
        if (!guildOrder.contains(g.id)) guildOrder << g.id;
        emit guildChanged(g.id);
    } else if (ev == "guild_delete") {
        auto id = o["guild_id"].toString();
        guildOrder.removeAll(id);
        guilds.remove(id);
        emit guildRemoved(id);
    } else if (ev == "channel_create" || ev == "channel_update") {
        auto c = Channel::from(o["channel"].toObject());
        if (c.guildId.isEmpty()) dms[c.id] = c;
        else if (guilds.contains(c.guildId)) guilds[c.guildId].channels[c.id] = c;
        emit guildChanged(c.guildId);
    } else if (ev == "channel_delete") {
        auto gid = o["guild_id"].toString();
        if (gid.isEmpty()) dms.remove(o["channel_id"].toString());
        else if (guilds.contains(gid)) guilds[gid].channels.remove(o["channel_id"].toString());
        emit guildChanged(gid);
    } else if (ev == "messages") {
        auto cid = o["channel_id"].toString();
        QVector<Message> batch;
        for (auto x : o["messages"].toArray()) batch << Message::from(x.toObject());
        auto &list = messages[cid];
        // older page goes in front, skipping ones we already have
        QSet<QString> have;
        for (auto &m : list) have << m.id;
        QVector<Message> merged;
        for (auto &m : batch) if (!have.contains(m.id)) merged << m;
        list = merged + list;
        hasMore[cid] = o["has_more"].toBool();
        emit messagesLoaded(cid);
    } else if (ev == "message_create") {
        auto m = Message::from(o["message"].toObject());
        m.nonce = o["nonce"].toString();
        auto &list = messages[m.channelId];
        // replace our pending echo when the nonce matches
        for (auto &x : list)
            if (x.pending && !m.nonce.isEmpty() && x.nonce == m.nonce) {
                x = m;
                emit messageUpdated(m);
                return;
            }
        list << m;
        emit messageAdded(m);
    } else if (ev == "message_update") {
        auto m = Message::from(o["message"].toObject());
        for (auto &x : messages[m.channelId])
            if (x.id == m.id) x = m;
        emit messageUpdated(m);
    } else if (ev == "message_delete") {
        auto cid = o["channel_id"].toString(), mid = o["message_id"].toString();
        auto &list = messages[cid];
        for (int i = 0; i < list.size(); ++i)
            if (list[i].id == mid) { list.remove(i); break; }
        emit messageRemoved(cid, mid);
    } else if (ev == "voice_state") {
        auto v = VoiceState::from(o["voice_state"].toObject());
        if (!guilds.contains(v.guildId)) return;
        auto &g = guilds[v.guildId];
        QString old = g.voice.contains(v.user.id) ? g.voice[v.user.id].channelId : QString();
        if (v.channelId.isEmpty()) g.voice.remove(v.user.id);
        else g.voice[v.user.id] = v;
        if (v.channelId.isEmpty()) speaking.remove(v.user.id);
        emit voiceChanged(v.guildId, v.user.id, old, v.channelId);
    } else if (ev == "speaking") {
        auto uid = o["user_id"].toString();
        bool on = o["speaking"].toBool();
        if (on) speaking << uid; else speaking.remove(uid);
        emit speakingChanged(uid, on);
    } else if (ev == "voice_status") {
        voiceState = o["state"].toString();
        voiceGuild = voiceState == "disconnected" ? QString() : o["guild_id"].toString();
        voiceChannel = voiceState == "disconnected" ? QString() : o["channel_id"].toString();
        ping = o["ping_ms"].toInt();
        emit voiceStatus(voiceState, o["msg"].toString());
    } else if (ev == "presence") {
        presence[o["user_id"].toString()] = o["status"].toString();
        activity[o["user_id"].toString()] = o["activity"].toString();
        emit presenceChanged(o["user_id"].toString());
    } else if (ev == "typing") emit typing(o["channel_id"].toString(), User::from(o["user"].toObject()));
    else if (ev == "user_profile") emit profile(o);
    else if (ev == "dm_open") {
        auto c = Channel::from(o["channel"].toObject());
        dms[c.id] = c;
        emit dmOpened(c.id);
    } else if (ev == "unread") {
        auto cid = o["channel_id"].toString();
        for (auto &g : guilds)
            if (g.channels.contains(cid)) {
                g.channels[cid].unread = o["unread"].toBool();
                g.channels[cid].mentions = o["mentions"].toInt();
            }
        if (dms.contains(cid)) dms[cid].unread = o["unread"].toBool();
        emit unreadChanged(cid);
    } else if (ev == "error") emit error(o["msg"].toString());
}

QString Store::myName(const QString &guildId) const {
    auto it = guilds.find(guildId);
    if (it != guilds.end() && !it->myNick.isEmpty()) return it->myNick;
    return me.name();
}

void Store::setFriends(const QJsonArray &list) {
    friends.clear();
    blocked.clear();
    ignored.clear();
    for (auto x : list) {
        auto f = x.toObject();
        auto u = User::from(f["user"].toObject());
        int type = f["type"].toInt();
        // ignore-only entries have no relationship type
        if (type) friends << qMakePair(u, type);
        if (type == 2) blocked << u.id;
        if (f["ignored"].toBool()) ignored << u.id;
    }
}

bool Store::silenced(const Message &m) const {
    if (silencedUser(m.author.id)) return true;
    auto c = channel(m.channelId);
    return c && c->guildId.isEmpty() && c->muted;
}
