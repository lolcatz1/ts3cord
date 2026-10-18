#include "infoframe.h"

#include "avatars.h"
#include "skin.h"
#include "store.h"

#include <QPainter>
#include <QRegularExpression>
#include <QUrlQuery>

QString fillTemplate(const QString &tpl, const QMap<QString, QString> &vars, const QMap<QString, QStringList> &lists) {
    static QRegularExpression var("%%(\\?{0,2})([A-Z0-9_]+)%%");
    auto empty = [](const QString &v) { return v.isEmpty() || v == "0"; };
    QStringList out;
    // the leading comment holds option defaults, not output
    auto body = tpl;
    if (body.trimmed().startsWith("<!--")) body = body.mid(body.indexOf("-->") + 3);
    for (auto line : body.split('\n')) {
        // list vars on a line repeat it once per item
        QString listVar;
        for (auto it = var.globalMatch(line); it.hasNext();) {
            auto m = it.next();
            if (lists.contains(m.captured(2))) { listVar = m.captured(2); break; }
        }
        int reps = listVar.isEmpty() ? 1 : lists[listVar].size();
        for (int rep = 0; rep < reps; ++rep) {
            bool drop = false;
            auto value = [&](const QString &name) -> QString {
                if (lists.contains(name)) {
                    // sibling lists line up by index
                    auto &l = lists[name];
                    return rep < l.size() ? l[rep] : QString();
                }
                return vars.value(name);
            };
            for (auto it = var.globalMatch(line); it.hasNext();) {
                auto m = it.next();
                if (!m.captured(1).isEmpty() && empty(value(m.captured(2)))) drop = true;
            }
            if (drop) continue;
            QString l = line;
            QString res;
            int last = 0;
            for (auto it = var.globalMatch(l); it.hasNext();) {
                auto m = it.next();
                res += l.mid(last, m.capturedStart() - last);
                if (m.captured(1) != "??") res += value(m.captured(2));
                last = m.capturedEnd();
            }
            res += l.mid(last);
            out << res;
        }
    }
    return out.join('\n');
}

QVariant ts3Resource(const QUrl &url) {
    auto s = url.toString();
    QSize size(16, 16);
    // our size suffix is the last one; discord urls carry their own ?size=
    auto q = s.lastIndexOf("?size=");
    if (q >= 0) {
        auto wh = s.mid(q + 6).split('x');
        if (wh.size() == 2) size = QSize(wh[0].toInt(), wh[1].toInt());
        s = s.left(q);
    }
    if (s.startsWith("iconpath:")) return skin::icon(s.mid(9)).pixmap(size).toImage();
    if (s.startsWith("avatar:")) {
        auto p = Avatars::get()->image(s.mid(7));
        if (p.isNull()) return QImage(size, QImage::Format_ARGB32_Premultiplied);
        return p.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
    }
    return {};
}

InfoFrame::InfoFrame(Store *s, QWidget *parent) : QTextBrowser(parent), m_store(s) {
    setObjectName("InfoFrame");
    setOpenLinks(false);
    document()->setDefaultStyleSheet(skin::styleFile("default_chat.qss"));
    connect(Avatars::get(), &Avatars::ready, this, [this] { refresh(); });
}

QVariant InfoFrame::loadResource(int type, const QUrl &url) {
    auto r = ts3Resource(url);
    return r.isValid() ? r : QTextBrowser::loadResource(type, url);
}

static QDateTime snowflakeTime(const QString &id) {
    return QDateTime::fromMSecsSinceEpoch(qint64(id.toULongLong() >> 22) + 1420070400000LL);
}

// "0 hours 22 minutes 3 seconds", "41 seconds", "1 second"
static QString duration(qint64 secs, bool fromHours) {
    auto unit = [](qint64 n, const char *one) { return QString("%1 %2%3").arg(n).arg(one).arg(n == 1 ? "" : "s"); };
    qint64 d = secs / 86400, h = secs / 3600 % 24, m = secs / 60 % 60, s = secs % 60;
    QStringList p;
    if (d) p << unit(d, "day");
    if (d || h || fromHours) p << unit(h, "hour");
    if (d || h || m || fromHours) p << unit(m, "minute");
    p << unit(s, "second");
    return p.join(' ');
}

static QString esc(const QString &s) { return s.toHtmlEscaped(); }

void InfoFrame::showServer(const QString &guildId) {
    m_kind = Server;
    m_guild = guildId;
    m_id = guildId;
    refresh();
}
void InfoFrame::showChannel(const QString &guildId, const QString &channelId) {
    m_kind = Channel;
    m_guild = guildId;
    m_id = channelId;
    refresh();
}
void InfoFrame::showClient(const QString &guildId, const QString &userId) {
    m_kind = Client;
    m_guild = guildId;
    m_id = userId;
    refresh();
}

void InfoFrame::refresh() {
    if (m_kind == None || !m_store->guilds.contains(m_guild)) { clear(); return; }
    auto &g = m_store->guilds[m_guild];
    QMap<QString, QString> v;
    QMap<QString, QStringList> lists;
    QString tpl, tail;
    auto now = QDateTime::currentDateTimeUtc();
    if (m_kind == Server) {
        tpl = skin::styleFile("default/serverinfo.tpl");
        v["SERVER_NAME"] = esc(g.name);
        v["SERVER_ICON_SCALED"] = g.iconUrl.isEmpty() ? "<img src=\"iconpath:SERVER_GREEN?size=24x24\" title=\"Server Icon\" width=\"24\" height=\"24\" />"
                                                      : QString("<img src=\"avatar:%1?size=24x24\" title=\"Server Icon\" width=\"24\" height=\"24\" />").arg(esc(g.iconUrl));
        v["TR_SERVER_NICKNAMES"] = "Nicknames";
        v["TR_SERVER_ADDRESS"] = "Address";
        v["SERVER_ADDRESS"] = "discord.com/channels/" + g.id;
        v["TR_SERVER_VERSION"] = "Version";
        v["SERVER_VERSION_SHORT"] = "Discord";
        v["SERVER_PLATFORM"] = "";
        v["TR_SERVER_LICENSE"] = "Owner";
        QString owner = g.ownerId;
        if (g.voice.contains(g.ownerId)) owner = esc(g.voice[g.ownerId].user.name());
        else if (g.ownerId == m_store->me.id) owner = esc(m_store->me.name());
        v["SERVER_LICENSE"] = owner;
        v["TR_SERVER_UPTIME"] = "Uptime";
        v["SERVER_UPTIME"] = duration(snowflakeTime(g.id).secsTo(now), true);
        v["TR_SERVER_CLIENTS_ONLINE"] = "Current Clients";
        v["SERVER_CLIENTS_ONLINE"] = QString::number(g.voice.size());
        v["SERVER_MAXCLIENTS"] = g.memberCount ? QString::number(g.memberCount) : "Unlimited";
        v["SERVER_NO_RESERVED_SLOTS"] = " ";
        v["TR_SERVER_CHANNELS_ONLINE"] = "Current Channels";
        int n = 0;
        for (auto &c : g.channels) if (!c.isCategory() && c.canView) ++n;
        v["SERVER_CHANNELS_ONLINE"] = QString::number(n);
        v["SERVER_REFRESH_ICON"] = "<img src=\"iconpath:REFRESH?size=18x18\" width=\"18\" height=\"18\" />";
        v["SERVER_REFRESH_ACTIVE"] = "_UpdateServerInfo_";
        v["TR_SERVER_REFRESH_ACTIVE"] = "Refresh";
        if (!g.description.isEmpty()) tail = "<br /><table class=\"InfoFrame_Table\" width=\"100%\"><tr><td class=\"Label\">Description:</td></tr><tr><td><br />" + esc(g.description) + "</td></tr></table>";
    } else if (m_kind == Channel) {
        if (!g.channels.contains(m_id)) { clear(); return; }
        auto &c = g.channels[m_id];
        tpl = skin::styleFile("default/channelinfo.tpl");
        v["CHANNEL_ID"] = c.id;
        v["CHANNEL_NAME"] = esc(c.name);
        v["CHANNEL_ICON_SCALED"] = "<img src=\"iconpath:CHANNEL_GREEN?size=24x24\" title=\"Channel Icon\" width=\"24\" height=\"24\" />";
        v["TR_CHANNEL_TOPIC"] = "Channel Topic";
        v["CHANNEL_TOPIC"] = esc(c.topic.section('\n', 0, 0));
        v["TR_CHANNEL_CODEC"] = "Audio Codec";
        v["CHANNEL_CODEC"] = c.isVoice() ? "Opus Voice" : c.type == Announcement ? "Announcements" : "Text Chat";
        QStringList flags;
        flags << "Permanent";
        if (c.nsfw) flags << "Age-Restricted";
        if (!c.canSend && !c.isVoice()) flags << "Read Only";
        if (!c.canConnect && c.isVoice()) flags << "Locked";
        v["TR_CHANNEL_FLAGS"] = "Properties";
        v["CHANNEL_FLAGS"] = "<span class=\"Highlight\">" + flags.join(", ") + "</span>";
        v["TR_CHANNEL_CLIENTS_COUNT"] = "Current Clients";
        int in = 0;
        for (auto &x : g.voice) if (x.channelId == c.id) ++in;
        v["CHANNEL_CLIENTS_COUNT"] = QString::number(in);
        v["CHANNEL_FLAG_MAXCLIENTS"] = c.userLimit ? QString::number(c.userLimit) : "Unlimited";
        if (c.isVoice() && c.bitrate) {
            v["TR_CHANNEL_MODERATED"] = "";
        }
        v["TR_CHANNEL_DESCRIPTION"] = c.topic.contains('\n') ? "Description" : "";
        v["CHANNEL_DESCRIPTION"] = esc(c.topic).replace("\n", "<br />");
    } else {
        tpl = skin::styleFile("default/clientinfo.tpl");
        User u;
        const VoiceState *vs = nullptr;
        if (g.voice.contains(m_id)) { vs = &g.voice[m_id]; u = vs->user; }
        else if (m_id == m_store->me.id) { u = m_store->me; u.display = m_store->myName(m_guild); }
        else u.id = m_id;
        v["CLIENT_ID"] = u.id;
        v["CLIENT_UNIQUE_ID"] = u.id;
        v["CLIENT_NAME"] = esc(u.name());
        v["CLIENT_NAME_PERCENT_ENCODED"] = QString::fromUtf8(QUrl::toPercentEncoding(u.name()));
        if (!u.username.isEmpty() && u.username != u.name()) v["CLIENT_CUSTOM_NICK_NAME"] = esc(u.username);
        v["TR_CLIENT_VERSION"] = "Version";
        v["CLIENT_VERSION_LONG"] = u.bot ? "Discord Bot" : "Discord";
        v["TR_CLIENT_CONNECTED_SINCE"] = "Account age";
        v["CLIENT_CONNECTED_SINCE"] = duration(snowflakeTime(u.id).secsTo(now), false).section(' ', 0, 1);
        auto st = m_store->presence.value(u.id);
        auto act = m_store->activity.value(u.id);
        if (!act.isEmpty()) {
            v["TR_CLIENT_DESCRIPTION"] = "Description";
            v["CLIENT_DESCRIPTION"] = esc(act);
        }
        v["TR_CLIENT_SERVER_GROUPS"] = "Server Groups";
        QStringList icons, names;
        if (u.id == g.ownerId) { icons << "<img src=\"iconpath:GROUP_300?size=24x24\" width=\"24\" height=\"24\" style=\"vertical-align: middle;\" />"; names << "Server Owner"; }
        if (u.bot) { icons << "<img src=\"iconpath:GROUP_500?size=24x24\" width=\"24\" height=\"24\" style=\"vertical-align: middle;\" />"; names << "Bot"; }
        if (u.id == m_store->me.id) {
            for (auto &r : g.roles)
                if (g.myRoles.contains(r.id)) {
                    icons << "<img src=\"iconpath:GROUP_600?size=24x24\" width=\"24\" height=\"24\" style=\"vertical-align: middle;\" />";
                    names << (r.color.isEmpty() || r.color == "#000000" ? esc(r.name) : QString("<span style=\"color:%1\">%2</span>").arg(r.color, esc(r.name)));
                }
        }
        if (names.isEmpty()) { icons << ""; names << "@everyone"; }
        lists["CLIENT_SERVER_GROUP_ICON"] = icons;
        lists["CLIENT_SERVER_GROUP_NAME"] = names;
        v["TR_CLIENT_CHANNEL_GROUP"] = "Channel Group";
        v["CLIENT_CHANNEL_GROUP_ICON"] = "";
        v["CLIENT_CHANNEL_GROUP_NAME"] = vs ? (vs->suppress ? "Audience" : "Speaker") : "Guest";
        // status rows like ts3 appends them
        QStringList rows;
        auto row = [&](const char *icon, const char *label) {
            rows << QString("<tr valign=\"middle\"><td><img src=\"iconpath:%1?size=48x48\" width=\"48\" height=\"48\" /></td><td>%2</td></tr>").arg(icon, label);
        };
        if (st == "idle") row("AWAY", "Away");
        if (st == "dnd") row("AWAY", "Do Not Disturb");
        if (vs) {
            if (vs->deaf) row("HARDWARE_OUTPUT_MUTED", "Speakers Muted by Server");
            else if (vs->selfDeaf) row("OUTPUT_MUTED", "Speakers Muted");
            if (vs->mute) row("HARDWARE_INPUT_MUTED", "Microphone Muted by Server");
            else if (vs->selfMute) row("INPUT_MUTED", "Microphone Muted");
            if (vs->streaming) row("MUSIC", "Streaming");
        }
        if (!rows.isEmpty()) tail = "<table cellspacing=\"3\">" + rows.join("") + "</table>";
    }
    auto html = fillTemplate(tpl, v, lists);
    auto end = html.lastIndexOf("</div>");
    if (end >= 0) html.insert(end, tail);
    setHtml(html);
}
