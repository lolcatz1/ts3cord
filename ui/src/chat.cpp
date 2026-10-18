#include "chat.h"

#include "infoframe.h"
#include "skin.h"
#include "store.h"

#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QTimer>

static QString esc(const QString &s) { return s.toHtmlEscaped(); }

QString markdownToHtml(const QString &md) {
    // code first so its insides stay literal
    QStringList code;
    QString s = md;
    static QRegularExpression block("```(?:[a-zA-Z0-9_+-]*\\n)?([\\s\\S]*?)```"), inl("`([^`\\n]+)`");
    auto stash = [&](QRegularExpression &re, bool isBlock) {
        QString out;
        int last = 0;
        for (auto it = re.globalMatch(s); it.hasNext();) {
            auto m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            auto body = esc(m.captured(1));
            if (isBlock) body.replace("\n", "<br />");
            code << QString("<span style=\"font-family:monospace; background-color:#efedec;\">%1</span>").arg(body);
            out += QString("\x01%1\x02").arg(code.size() - 1);
            last = m.capturedEnd();
        }
        s = out + s.mid(last);
    };
    stash(block, true);
    stash(inl, false);
    s = esc(s);
    // discord cdn files and direct media links become [attachment], the rest stay links
    static QRegularExpression url("(https?://[^\\s<>\"]+)");
    static QRegularExpression media("^https?://((cdn|media)\\.discordapp\\.(com|net)/attachments/|[^?#]+\\.(png|jpe?g|gif|webp|avif|bmp|mp4|webm|mov|mkv|m4v|mp3|ogg|oga|opus|wav|flac|m4a)([?#]|$))",
                                    QRegularExpression::CaseInsensitiveOption);
    {
        QString out;
        int last = 0;
        for (auto it = url.globalMatch(s); it.hasNext();) {
            auto m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            // the text is html-escaped here; links want the raw url
            auto raw = QString(m.captured(1)).replace("&amp;", "&");
            if (media.match(raw).hasMatch())
                out += QString("<a href=\"attachurl:%1\">[attachment]</a>").arg(QString::fromUtf8(QUrl::toPercentEncoding(raw)));
            else
                out += QString("<a href=\"%1\">%1</a>").arg(m.captured(1));
            last = m.capturedEnd();
        }
        s = out + s.mid(last);
    }
    static QRegularExpression bold("\\*\\*(.+?)\\*\\*"), under("__(.+?)__"), ital1("\\*(?!\\s)(.+?)\\*"), ital2("(?<![\\w])_(?!\\s)(.+?)_(?![\\w])"),
        strike("~~(.+?)~~"), spoil("\\|\\|(.+?)\\|\\|"), quote("(^|\\n)&gt; ([^\\n]*)"), head("(^|\\n)#{1,3} ([^\\n]*)");
    s.replace(bold, "<b>\\1</b>");
    s.replace(under, "<u>\\1</u>");
    s.replace(ital1, "<i>\\1</i>");
    s.replace(ital2, "<i>\\1</i>");
    s.replace(strike, "<s>\\1</s>");
    s.replace(spoil, "<span style=\"background-color:#202020; color:#202020;\">\\1</span>");
    s.replace(quote, "\\1<span style=\"color:#707475;\">| \\2</span>");
    s.replace(head, "\\1<b>\\2</b>");
    s.replace("\n", "<br />");
    static QRegularExpression back("\x01(\\d+)\x02");
    QString out;
    int last = 0;
    for (auto it = back.globalMatch(s); it.hasNext();) {
        auto m = it.next();
        out += s.mid(last, m.capturedStart() - last) + code.value(m.captured(1).toInt());
        last = m.capturedEnd();
    }
    return out + s.mid(last);
}

ChatTab::ChatTab(Kind k, const QString &channelId, QWidget *parent) : QTextBrowser(parent), m_kind(k), m_channel(channelId) {
    setObjectName("ChatTab");
    setOpenLinks(false);
    setFrameShape(QFrame::StyledPanel);
    document()->setDefaultStyleSheet(skin::styleFile("default_chat.qss"));
    document()->setDocumentMargin(4);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (m_rendering) return;
        m_stick = v >= verticalScrollBar()->maximum() - 4;
        if (v == 0 && verticalScrollBar()->maximum() > 0) emit scrolledToTop();
    });
}

QVariant ChatTab::loadResource(int type, const QUrl &url) {
    auto r = ts3Resource(url);
    return r.isValid() ? r : QTextBrowser::loadResource(type, url);
}

static QString stamp(const QDateTime &t) {
    auto lt = t.isValid() ? t.toLocalTime() : QDateTime::currentDateTime();
    if (lt.date() == QDate::currentDate()) return "&lt;" + lt.toString("hh:mm:ss") + "&gt;";
    return "&lt;" + lt.toString("yyyy-MM-dd hh:mm:ss") + "&gt;";
}

void ChatTab::render() {
    auto sb = verticalScrollBar();
    int fromBottom = sb->maximum() - sb->value();
    QVector<Entry> all = m_msgs + m_infos;
    std::stable_sort(all.begin(), all.end(), [](const Entry &x, const Entry &y) { return x.when < y.when; });
    QStringList ps;
    for (auto &e : all) ps << e.html;
    m_rendering = true;
    setHtml(ps.join("\n"));
    // layout settles on the next turn of the loop
    auto apply = [this, fromBottom] {
        auto sb = verticalScrollBar();
        sb->setValue(m_stick ? sb->maximum() : sb->maximum() - fromBottom);
    };
    apply();
    QTimer::singleShot(0, this, [this, apply] {
        apply();
        m_rendering = false;
    });
}

void ChatTab::append_(const QString &p) {
    // info lines on an otherwise empty server tab go straight in, like ts3
    m_infos << Entry{QDateTime::currentDateTimeUtc(), p};
    render();
}

void ChatTab::info(const QString &html, const QString &cls, const QDateTime &when) {
    auto t = when.isValid() ? when : QDateTime::currentDateTimeUtc();
    m_infos << Entry{t, QString("<p class=\"%1\"><img src=\"iconpath:MESSAGE_INFO?size=18x18\" width=\"18\" height=\"18\" />"
                                "<span class=\"TextMessage_Time\">%2</span><span class=\"%1\"> %3</span></p>")
                            .arg(cls, stamp(t), html)};
    render();
}

void ChatTab::error(const QString &html) {
    auto t = QDateTime::currentDateTimeUtc();
    m_infos << Entry{t, QString("<p class=\"TextMessage_ServerError\"><img src=\"iconpath:MESSAGE_INFO?size=18x18\" width=\"18\" height=\"18\" />"
                                "<span class=\"TextMessage_Time\">%1</span><span class=\"TextMessage_ServerError\"> %2</span></p>")
                            .arg(stamp(t), html)};
    render();
}

static QString messageHtml(const Message &m, bool own, bool priv) {
    QString body = markdownToHtml(m.content);
    if (m.type != 0 && m.type != 19 && m.content.isEmpty()) body = "<i>(system message)</i>";
    for (int i = 0; i < m.attachments.size(); ++i)
        body += QString("%1<a href=\"attachment://%2/%3/%4\">[attachment]</a>").arg(body.isEmpty() ? "" : " ", m.channelId, m.id).arg(i);
    for (auto &e : m.embeds) {
        if (e.title.isEmpty() && e.description.isEmpty()) continue;
        QString t = e.url.isEmpty() ? esc(e.title) : QString("<a href=\"%1\">%2</a>").arg(esc(e.url), esc(e.title.isEmpty() ? e.url : e.title));
        body += QString("%1<span style=\"color:#707475;\">[%2%3]</span>").arg(body.isEmpty() ? "" : "<br />", t,
                                                                              e.description.isEmpty() ? "" : " " + markdownToHtml(e.description.left(300)));
    }
    if (m.edited.isValid()) body += " <span style=\"color:#909090;\">(edited)</span>";
    QString reply;
    if (!m.replyId.isEmpty())
        reply = QString("<span style=\"color:#707475;\">[Re: %1: %2]</span> ").arg(esc(m.replyAuthor), esc(m.replyContent.left(60).replace('\n', ' ')));
    QString cls = QString("TextMessage_Message %1 %2").arg(own ? "Outgoing" : "Incoming", priv ? "Private" : "Public");
    QString name = esc(m.author.name());
    QString user = QString("<a href=\"client://%1/%2~%3\" class=\"TextMessage_UserLink\">\"%4\"</a>")
                       .arg(m.author.id, m.author.id, QString::fromUtf8(QUrl::toPercentEncoding(m.author.name())), name);
    QString text = m.mentionsMe ? "<span class=\"Important\">" + body + "</span>" : body;
    if (m.pending) text = "<span style=\"color:#909090;\">" + text + "</span>";
    return QString("<p class=\"%1\"><img src=\"iconpath:%2?size=18x18\" width=\"18\" height=\"18\" />"
                   "<span class=\"TextMessage_Time\">%3</span> %4: <span class=\"TextMessage_Text\">%5%6</span></p>")
        .arg(cls, own ? "MESSAGE_OUTGOING" : "MESSAGE_INCOMING", stamp(m.timestamp), user, reply, text);
}

void ChatTab::message(const Message &m, bool own) {
    m_msgs << Entry{m.timestamp, messageHtml(m, own, m_kind == PrivateTab)};
    render();
}

void ChatTab::history(const QVector<Message> &list, const QString &myId) {
    m_myId = myId;
    m_msgs.clear();
    for (auto &m : list) m_msgs << Entry{m.timestamp, messageHtml(m, m.author.id == myId, m_kind == PrivateTab)};
    render();
}

void ChatTab::clearLog() {
    m_msgs.clear();
    m_infos.clear();
    clear();
}

ChatTabWidget::ChatTabWidget(QWidget *parent) : QTabWidget(parent) {
    setObjectName("ChatTabWidget");
    auto bar = new ImprovedTabBar(this);
    bar->setObjectName("ChatTabBar");
    setTabBar(bar);
    setTabPosition(South);
    setIconSize(QSize(16, 16));
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideNone);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int i) {
        auto t = qobject_cast<ChatTab *>(widget(i));
        if (!t || t->kind() != ChatTab::PrivateTab) return;
        auto id = t->channelId();
        removeTab(i);
        t->deleteLater();
        emit privateClosed(id);
    });
    connect(this, &QTabWidget::currentChanged, this, [this](int i) {
        if (auto t = qobject_cast<ChatTab *>(widget(i))) markUnread(t, false);
    });
}

ChatTab *ChatTabWidget::addChat(ChatTab::Kind k, const QString &channelId, const QString &title) {
    auto t = new ChatTab(k, channelId, this);
    auto icon = k == ChatTab::ServerTab ? "SERVER_TAB" : k == ChatTab::ChannelTab ? "CHANNEL_CHAT" : "PLAYER_CHAT";
    int i = addTab(t, skin::icon(icon), title);
    if (k == ChatTab::PrivateTab) {
        auto close = new QToolButton(this);
        close->setObjectName("ServerTabCloseButton");
        close->setIcon(skin::icon("TAB_CLOSE_BUTTON"));
        close->setAutoRaise(true);
        close->setIconSize(QSize(16, 16));
        connect(close, &QToolButton::clicked, this, [this, t] { emit tabCloseRequested(indexOf(t)); });
        tabBar()->setTabButton(i, QTabBar::RightSide, close);
    }
    return t;
}

ChatTab *ChatTabWidget::find(const QString &channelId) const {
    for (int i = 0; i < count(); ++i)
        if (auto t = qobject_cast<ChatTab *>(widget(i)); t && t->kind() == ChatTab::PrivateTab && t->channelId() == channelId) return t;
    return nullptr;
}

ChatTab *ChatTabWidget::serverTab() const {
    for (int i = 0; i < count(); ++i)
        if (auto t = qobject_cast<ChatTab *>(widget(i)); t && t->kind() == ChatTab::ServerTab) return t;
    return nullptr;
}

ChatTab *ChatTabWidget::channelTab() const {
    for (int i = 0; i < count(); ++i)
        if (auto t = qobject_cast<ChatTab *>(widget(i)); t && t->kind() == ChatTab::ChannelTab) return t;
    return nullptr;
}

void ChatTabWidget::markUnread(ChatTab *t, bool on) {
    int i = indexOf(t);
    if (i < 0) return;
    if (on && i == currentIndex()) return;
    // ts3 paints tabs with unread lines in its link blue
    tabBar()->setTabTextColor(i, on ? QColor("#1c82cc") : QColor());
}

ChatLineEdit::ChatLineEdit(QWidget *parent) : QTextEdit(parent) {
    setObjectName("ChatLineEdit");
    setAcceptRichText(false);
    setTabChangesFocus(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setFixedHeight(30);
    setHelp(true);
    connect(this, &QTextEdit::textChanged, this, [this] {
        // text set from outside while the help text shows ends help mode
        if (m_help && toPlainText() != tr("Enter Chat Message...")) {
            m_help = false;
            style()->unpolish(this);
            style()->polish(this);
        }
        if (!m_help && !toPlainText().isEmpty()) emit typed();
    });
}

void ChatLineEdit::setHelp(bool on) {
    if (m_help == on) return;
    m_help = on;
    blockSignals(true);
    setPlainText(on ? tr("Enter Chat Message...") : QString());
    blockSignals(false);
    style()->unpolish(this);
    style()->polish(this);
}

void ChatLineEdit::clearText() {
    blockSignals(true);
    clear();
    blockSignals(false);
    if (!hasFocus()) setHelp(true);
}

void ChatLineEdit::focusInEvent(QFocusEvent *e) {
    setHelp(false);
    QTextEdit::focusInEvent(e);
}

void ChatLineEdit::focusOutEvent(QFocusEvent *e) {
    if (toPlainText().isEmpty()) setHelp(true);
    QTextEdit::focusOutEvent(e);
}

bool ChatLineEdit::canInsertFromMimeData(const QMimeData *m) const {
    return m->hasImage() || m->hasUrls() || QTextEdit::canInsertFromMimeData(m);
}

void ChatLineEdit::insertFromMimeData(const QMimeData *m) {
    QStringList added;
    if (m->hasUrls())
        for (auto &u : m->urls())
            if (u.isLocalFile() && QFileInfo(u.toLocalFile()).isFile()) added << u.toLocalFile();
    if (added.isEmpty() && m->hasImage()) {
        // clipboard image -> png in the cache, uploaded like a file
        auto img = qvariant_cast<QImage>(m->imageData());
        auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/paste";
        QDir().mkpath(dir);
        auto path = dir + "/" + QDateTime::currentDateTime().toString("'image_'yyyyMMdd_hhmmss_zzz'.png'");
        if (!img.isNull() && img.save(path, "PNG")) added << path;
    }
    if (added.isEmpty()) {
        QTextEdit::insertFromMimeData(m);
        return;
    }
    m_files << added;
    emit attachmentsChanged(m_files);
}

void ChatLineEdit::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && !m_files.isEmpty()) {
        clearAttachments();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier)) {
        auto t = text().trimmed();
        if (!t.isEmpty() || !m_files.isEmpty()) {
            m_history << t;
            m_histPos = -1;
            emit submitted(t);
        }
        clearText();
        return;
    }
    if (e->key() == Qt::Key_Up && !m_history.isEmpty() && (toPlainText().isEmpty() || m_histPos >= 0)) {
        m_histPos = m_histPos < 0 ? m_history.size() - 1 : qMax(0, m_histPos - 1);
        setPlainText(m_history[m_histPos]);
        moveCursor(QTextCursor::End);
        return;
    }
    if (e->key() == Qt::Key_Down && m_histPos >= 0) {
        m_histPos++;
        if (m_histPos >= m_history.size()) { m_histPos = -1; clear(); }
        else setPlainText(m_history[m_histPos]);
        moveCursor(QTextCursor::End);
        return;
    }
    QTextEdit::keyPressEvent(e);
}
