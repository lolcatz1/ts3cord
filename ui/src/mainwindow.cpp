#include "mainwindow.h"

#include "avatars.h"
#include "chat.h"
#include "corelink.h"
#include "dialogs.h"
#include "infoframe.h"
#include "serverview.h"
#include "skin.h"
#include "statusbar.h"
#include "store.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSoundEffect>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

// ts3 qss keys off this class name
class ImprovedToolBar : public QToolBar {
    Q_OBJECT
public:
    using QToolBar::QToolBar;
};

MainWindow::MainWindow(Store *s, CoreLink *core, QWidget *parent) : QMainWindow(parent), m_store(s), m_core(core) {
    setWindowTitle("TeamSpeak 3");
    setWindowIcon(skin::appIcon());
    buildMenus();
    buildToolbar();
    buildCentral();
    buildStatus();
    wire();
    QSettings st;
    if (!restoreGeometry(st.value("geometry").toByteArray())) resize(1100, 750);
    updateSelfActions();
    updateStatus();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    QSettings st;
    st.setValue("geometry", saveGeometry());
    QStringList open;
    for (int i = 0; i < m_views->count(); ++i)
        if (auto v = qobject_cast<ServerView *>(m_views->widget(i))) open << v->guildId();
    st.setValue("openGuilds", open);
    QMainWindow::closeEvent(e);
}

static QAction *act(QMenu *m, const QString &text, const char *icon, const QKeySequence &sc = {}) {
    auto a = m->addAction(text);
    if (icon) a->setIcon(skin::icon(icon));
    if (!sc.isEmpty()) a->setShortcut(sc);
    return a;
}

void MainWindow::buildMenus() {
    auto mb = menuBar();
    auto nyi = [this] { QMessageBox::information(this, "TeamSpeak 3", tr("This feature has no Discord counterpart.")); };

    auto con = mb->addMenu(tr("&Connections"));
    connect(act(con, tr("&Connect"), "CONNECT", QKeySequence("Ctrl+S")), &QAction::triggered, this, [this] {
        ConnectDialog d(m_store, this);
        if (d.exec() == QDialog::Accepted && !d.guildId().isEmpty()) openGuild(d.guildId(), d.newTab() || !current());
    });
    connect(act(con, tr("&Disconnect from Current"), "DISCONNECT", QKeySequence("Ctrl+D")), &QAction::triggered, this, [this] {
        if (auto v = current()) {
            playSound("CONNECTION_DISCONNECTED");
            if (m_store->voiceGuild == v->guildId()) m_core->send("leave_voice");
            m_views->removeTab(m_views->indexOf(v));
            v->chat()->deleteLater();
            v->deleteLater();
            QTimer::singleShot(0, this, [this] { showChat(current()); updateStatus(); });
        }
    });
    connect(act(con, tr("&Disconnect from All"), "DISCONNECT_ALL"), &QAction::triggered, this, [this] {
        if (!m_store->voiceGuild.isEmpty()) m_core->send("leave_voice");
        if (m_views->count()) playSound("CONNECTION_DISCONNECTED");
        while (m_views->count()) {
            auto v = qobject_cast<ServerView *>(m_views->widget(0));
            m_views->removeTab(0);
            v->chat()->deleteLater();
            v->deleteLater();
        }
        QTimer::singleShot(0, this, [this] { showChat(nullptr); updateStatus(); });
    });
    con->addSeparator();
    connect(act(con, tr("&Quit"), "QUIT", QKeySequence("Ctrl+Q")), &QAction::triggered, this, &QWidget::close);

    m_bookmarks = mb->addMenu(tr("&Bookmarks"));
    // guild icons arrive after the menu is built
    auto bmTimer = new QTimer(this);
    bmTimer->setSingleShot(true);
    bmTimer->setInterval(300);
    connect(bmTimer, &QTimer::timeout, this, &MainWindow::updateBookmarks);
    connect(Avatars::get(), &Avatars::ready, bmTimer, qOverload<>(&QTimer::start));
    // long guild lists scroll inside a menu of ~20 rows
    m_bookmarks->setMaximumHeight(560);
    updateBookmarks();

    auto self = mb->addMenu(tr("S&elf"));
    connect(self, &QMenu::aboutToShow, this, [this, self, nyi] {
        self->clear();
        auto cap = self->addMenu(skin::icon("CAPTURE"), tr("Capture Profile"));
        auto d1 = cap->addAction(tr("Default"));
        d1->setCheckable(true);
        d1->setChecked(true);
        auto play = self->addMenu(skin::icon("PLAYBACK"), tr("Playback Profile"));
        auto d2 = play->addAction(tr("Default"));
        d2->setCheckable(true);
        d2->setChecked(true);
        auto hk = self->addMenu(skin::icon("HOTKEYS"), tr("Hotkey Profile"));
        auto d3 = hk->addAction(tr("Default"));
        d3->setCheckable(true);
        d3->setChecked(true);
        auto sp = self->addMenu(skin::icon("SOUNDPACK"), tr("Sound Pack"));
        QSettings st;
        auto cur = st.value("soundpack", "default").toString();
        for (auto [dir, name] : {std::pair{"default", "Default Sound Pack (Female)"}, {"default_male", "Default Sound Pack (Male)"},
                                 {"default_speech", "Default Text To Speech"}, {"nosounds", "Sounds deactivated"}}) {
            auto a = sp->addAction(tr(name));
            a->setCheckable(true);
            a->setChecked(cur == dir);
            QString d = dir;
            connect(a, &QAction::triggered, this, [d] { QSettings().setValue("soundpack", d); });
        }
        self->addSeparator();
        auto away = self->addMenu(skin::icon("AWAY"), tr("Set Away"));
        away->addAction(m_actAway);
        away->addAction(m_actOnline);
        self->addAction(m_actMic);
        self->addAction(m_selfMuteMic);
        self->addAction(m_selfMuteSpk);
        self->addSeparator();
        connect(act(self, tr("&Change Nickname"), "CHANGE_NICKNAME"), &QAction::triggered, this, nyi);
        act(self, tr("Request Talk Power"), "TALK_POWER_REQUEST")->setEnabled(false);
        connect(act(self, tr("Set &Phonetic Nickname"), "PHONETICSNICKNAME"), &QAction::triggered, this, nyi);
        connect(act(self, tr("Set Avatar"), "UPLOAD_AVATAR"), &QAction::triggered, this, nyi);
        self->addSeparator();
        connect(act(self, tr("&Connection Info"), "INFO"), &QAction::triggered, this, [this] {
            QMessageBox::information(this, tr("Connection Info"),
                                     tr("Gateway: %1\nVoice: %2\nPing: %3 ms").arg(m_connected ? tr("connected") : tr("disconnected"), m_store->voiceState).arg(m_store->ping));
        });
    });

    auto perm = mb->addMenu(tr("Per&missions"));
    for (auto [t, i, sc] : {std::tuple{"&Server Groups", "PERMISSIONS_SERVER_GROUPS", "Ctrl+F1"}, {"&Channel Groups", "PERMISSIONS_CHANNEL_GROUPS", "Ctrl+F4"}})
        connect(act(perm, tr(t), i, QKeySequence(sc)), &QAction::triggered, this, nyi);
    perm->addSeparator();
    connect(act(perm, tr("Channel &Groups of Client"), "PERMISSIONS_CHANNEL_GROUPS"), &QAction::triggered, this, nyi);
    connect(act(perm, tr("&List All Clients"), "PERMISSIONS_CLIENTS"), &QAction::triggered, this, nyi);
    perm->addSeparator();
    connect(act(perm, tr("&Privilege Keys"), "TOKEN"), &QAction::triggered, this, nyi);
    connect(act(perm, tr("Use &Privilege Key"), "TOKEN_USE"), &QAction::triggered, this, nyi);

    auto tools = mb->addMenu(tr("&Tools"));
    connect(act(tools, tr("&Identities"), "IDENTITY_MANAGER", QKeySequence("Ctrl+I")), &QAction::triggered, this, [this] {
        if (QMessageBox::question(this, tr("Identities"), tr("Logged in as %1.\nLog out of this Discord account?").arg(m_store->me.name())) == QMessageBox::Yes)
            m_core->send("logout");
    });
    m_actContacts = act(tools, tr("Contacts"), "CONTACT", QKeySequence("Ctrl+Shift+O"));
    act(tools, tr("Collected &URLs"), "URLCATCHER", QKeySequence("Ctrl+U"))->setEnabled(false);
    act(tools, tr("File &Transfers"), "FILETRANSFER", QKeySequence("Ctrl+T"))->setEnabled(false);
    connect(act(tools, tr("&Invite Buddy"), "INVITE_BUDDY"), &QAction::triggered, this, nyi);
    connect(act(tools, tr("Offline Messages"), "OFFLINE_MESSAGES", QKeySequence("Ctrl+O")), &QAction::triggered, this, [this] {
        // direct messages
        QMenu m(this);
        QVector<const Channel *> list;
        for (auto &c : m_store->dms) list << &c;
        std::sort(list.begin(), list.end(), [](auto a, auto b) { return a->lastMessageId.toULongLong() > b->lastMessageId.toULongLong(); });
        for (auto c : list) {
            QStringList names;
            for (auto &u : c->recipients) names << u.name();
            auto a = m.addAction(skin::icon(c->unread ? "MESSAGE_INCOMING" : "PLAYER_CHAT"), c->name.isEmpty() ? names.join(", ") : c->name);
            auto id = c->id;
            connect(a, &QAction::triggered, this, [this, id] { openPrivate(id, true); });
        }
        if (list.isEmpty()) m.addAction(tr("No messages"))->setEnabled(false);
        m.exec(QCursor::pos());
    });
    act(tools, tr("&Whisper Lists"), "WHISPERLISTS", QKeySequence("Ctrl+Shift+W"))->setEnabled(false);
    act(tools, tr("&Whisper History"), "WHISPERLISTS", QKeySequence("Ctrl+Shift+H"))->setEnabled(false);
    act(tools, tr("Client &Log"), "LISTVIEW", QKeySequence("Ctrl+L"))->setEnabled(false);
    tools->addSeparator();
    act(tools, tr("Ban List"), "BAN_LIST", QKeySequence("Ctrl+Shift+B"))->setEnabled(false);
    act(tools, tr("Complaints List"), "COMPLAINT_LIST", QKeySequence("Ctrl+Shift+C"))->setEnabled(false);
    act(tools, tr("&ServerQuery Login"), "SERVER_QUERY")->setEnabled(false);
    act(tools, tr("&Server Log"), "SERVER_LOG", QKeySequence("Ctrl+Shift+L"))->setEnabled(false);
    tools->addSeparator();
    act(tools, tr("Start &Recording"), "RECORDING_START", QKeySequence("Ctrl+Shift+R"))->setEnabled(false);
    act(tools, tr("Start Multitrack Recording"), "RECORDING_START")->setEnabled(false);
    act(tools, tr("Stop &Recording"), "RECORDING_STOP", QKeySequence("Ctrl+Shift+T"))->setEnabled(false);
    tools->addSeparator();
    connect(act(tools, tr("&Options"), "SETTINGS", QKeySequence("Alt+P")), &QAction::triggered, this, nyi);

    auto help = mb->addMenu(tr("&Help"));
    connect(act(help, tr("&About TeamSpeak 3"), "ABOUT"), &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About"), tr("<b>ts3cord %1</b><br>a discord client wearing teamspeak 3.<br>by leroy").arg(TS3CORD_VERSION));
    });
    connect(act(help, tr("About &Qt"), "ABOUT"), &QAction::triggered, qApp, &QApplication::aboutQt);
    help->addSeparator();
    connect(act(help, tr("Visit TeamSpeak &Webpage"), "WEBLIST"), &QAction::triggered, this, [] { QDesktopServices::openUrl(QUrl("https://discord.com/app")); });
    connect(act(help, tr("Visit TeamSpeak Community Forum"), "WEBLIST"), &QAction::triggered, this, [] { QDesktopServices::openUrl(QUrl("https://github.com/ayn2op/discordo")); });
    act(help, tr("&Rent own TeamSpeak Server"), "WEBLIST")->setEnabled(false);
    help->addSeparator();
    act(help, tr("&myTeamSpeak Account"), "MYTS_ACCOUNT")->setEnabled(false);
    help->addSeparator();
    act(help, tr("Check for &Update"), "CHECK_UPDATE")->setEnabled(false);
    act(help, tr("View &Changelog"), "CHANGELOG")->setEnabled(false);
    act(help, tr("View &License"), "CHANGELOG")->setEnabled(false);
    act(help, tr("Show News &Ticker"), "CHANGELOG")->setEnabled(false);
}

void MainWindow::updateBookmarks() {
    m_bookmarks->clear();
    act(m_bookmarks, tr("&Add to Bookmarks"), "BOOKMARK_ADD")->setEnabled(false);
    act(m_bookmarks, tr("&Manage Bookmarks"), "BOOKMARK_MANAGER", QKeySequence("Ctrl+B"))->setEnabled(false);
    m_bookmarks->addSeparator();
    for (auto &id : m_store->guildOrder) {
        auto &g = m_store->guilds[id];
        auto pm = Avatars::get()->image(g.iconUrl);
        auto a = m_bookmarks->addAction(pm.isNull() ? skin::icon("SERVER_GREEN") : QIcon(pm), QString(g.name).replace('&', "&&"));
        connect(a, &QAction::triggered, this, [this, id] { openGuild(id, true); });
    }
}

void MainWindow::buildToolbar() {
    auto tb = new ImprovedToolBar(this);
    tb->setIconSize(QSize(25, 25));
    tb->setMovable(true);
    addToolBar(tb);

    m_actAway = new QAction(skin::icon("AWAY"), tr("&Set Away"), this);
    m_actOnline = new QAction(skin::icon("PRESENT"), tr("&Online"), this);
    connect(m_actAway, &QAction::triggered, this, [this] { m_away = true; m_core->send("set_status", {{"status", "idle"}}); playSound("STATUS_SET_AWAY"); updateSelfActions(); });
    connect(m_actOnline, &QAction::triggered, this, [this] { m_away = false; m_core->send("set_status", {{"status", "online"}}); playSound("STATUS_SET_PRESENT"); updateSelfActions(); });
    m_awayBtn = new QToolButton(tb);
    m_awayBtn->setObjectName("AwayButton");
    auto awayToggle = new QAction(skin::icon("AWAY"), tr("&Toggle Away Status"), this);
    connect(awayToggle, &QAction::triggered, this, [this] { (m_away ? m_actOnline : m_actAway)->trigger(); });
    auto awayMenu = new QMenu(m_awayBtn);
    awayMenu->addAction(m_actAway);
    awayMenu->addAction(m_actOnline);
    m_awayBtn->setDefaultAction(awayToggle);
    m_awayBtn->setMenu(awayMenu);
    m_awayBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_awayBtn->setIconSize(QSize(16, 16));
    tb->addWidget(m_awayBtn);

    m_actMic = tb->addAction(skin::icon("ACTIVATE_MICROPHONE"), tr("&Activate Microphone"));
    m_actMuteMic = tb->addAction(skin::icon("INPUT_MUTED"), tr("Mute Microphone"));
    m_actMuteMic->setCheckable(true);
    m_selfMuteMic = m_actMuteMic;
    connect(m_actMic, &QAction::triggered, this, [this] {
        m_actMuteMic->setChecked(false);
        m_core->send("set_self_mute", {{"mute", false}});
        playSound("SOUND_CAPTURE_UNMUTED");
        updateSelfActions();
    });
    connect(m_actMuteMic, &QAction::triggered, this, [this](bool on) {
        m_core->send("set_self_mute", {{"mute", on}});
        playSound(on ? "SOUND_CAPTURE_MUTED" : "SOUND_CAPTURE_UNMUTED");
        updateSelfActions();
    });

    m_spkBtn = new QToolButton(tb);
    m_spkBtn->setObjectName("SpeakerMuteButton");
    m_actMuteSpk = new QAction(skin::icon("OUTPUT_MUTED"), tr("Mute Speakers/Headphones"), this);
    m_actMuteSpk->setCheckable(true);
    m_selfMuteSpk = m_actMuteSpk;
    connect(m_actMuteSpk, &QAction::triggered, this, [this](bool on) {
        m_core->send("set_self_deaf", {{"deaf", on}});
        playSound(on ? "SOUND_PLAYBACK_MUTED" : "SOUND_PLAYBACK_UNMUTED");
        updateSelfActions();
    });
    auto spkMenu = new QMenu(m_spkBtn);
    spkMenu->addAction(m_actMuteSpk);
    auto all = spkMenu->addAction(skin::icon("OUTPUT_MUTED"), tr("Mute Speakers/Headphones on all servers"));
    connect(all, &QAction::triggered, m_actMuteSpk, &QAction::trigger);
    m_spkBtn->setDefaultAction(m_actMuteSpk);
    m_spkBtn->setMenu(spkMenu);
    m_spkBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_spkBtn->setIconSize(QSize(16, 16));
    tb->addWidget(m_spkBtn);
    tb->addSeparator();
    m_actSubscribe = tb->addAction(skin::icon("SUBSCRIBE_MODE"), tr("Toggle Subscribe Mode for this server\nCurrent: Subscribe all"));
    tb->addSeparator();
    tb->addAction(m_actContacts);
    connect(m_actContacts, &QAction::triggered, this, [this] {
        if (!m_contacts) {
            m_contacts = new ContactsDialog(m_store, m_core, this);
            m_contacts->setAttribute(Qt::WA_DeleteOnClose);
            connect(m_contacts, &ContactsDialog::openDm, this, [this](const QString &uid) { m_core->send("open_dm", {{"user_id", uid}}); });
        }
        m_contacts->show();
        m_contacts->raise();
    });
    auto stretch = new QWidget(tb);
    stretch->setObjectName("ToolbarStretcher");
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(stretch);
}

void MainWindow::buildCentral() {
    auto split = new QSplitter(Qt::Vertical, this);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(4);
    // handles take their name from the widget when it is added, so name first
    auto top = new QWidget;
    top->setObjectName("MainWindowServerTabsWidget");
    auto tl = new QHBoxLayout(top);
    tl->setContentsMargins(5, 5, 5, 0);
    tl->setSpacing(6);
    m_views = new ServerViewManager(top);
    tl->addWidget(m_views);
    m_chatWidget = new QWidget;
    m_chatWidget->setObjectName("MainWindowChatWidget");
    m_chatLayout = new QVBoxLayout(m_chatWidget);
    m_chatLayout->setContentsMargins(5, 0, 5, 5);
    m_chatLayout->setSpacing(1);
    m_noServerChat = new ChatTabWidget(m_chatWidget);
    m_noServerChat->addChat(ChatTab::ServerTab, {}, tr("No server"));
    m_chatLayout->addWidget(m_noServerChat);
    m_chatShown = m_noServerChat;
    auto lineRow = new QHBoxLayout;
    lineRow->setContentsMargins(0, 11, 0, 0);
    lineRow->setSpacing(1);
    m_line = new ChatLineEdit(m_chatWidget);
    auto emo = new QToolButton(m_chatWidget);
    emo->setObjectName("EmoticonButton");
    emo->setIcon(skin::icon("EMOTICON"));
    emo->setIconSize(QSize(16, 16));
    emo->setAutoRaise(true);
    emo->setFixedSize(24, 23);
    lineRow->addWidget(m_line);
    lineRow->addWidget(emo);
    m_chatLayout->addLayout(lineRow);
    split->addWidget(top);
    split->addWidget(m_chatWidget);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);
    split->setSizes({436, 218});
    setCentralWidget(split);
    auto emoMenu = new QMenu(emo);
    for (auto e : QStringList{"😀", "😂", "😍", "👍", "👎", "🎉", "😢", "😡", "🤔", "👀", "🔥", "❤️"}) {
        auto a = emoMenu->addAction(e);
        connect(a, &QAction::triggered, this, [this, e] {
            m_line->setFocus();
            m_line->insertPlainText(e);
        });
    }
    connect(emo, &QToolButton::clicked, this, [emo, emoMenu] { emoMenu->exec(emo->mapToGlobal(QPoint(0, -emoMenu->sizeHint().height()))); });
    connect(m_line, &ChatLineEdit::submitted, this, &MainWindow::submit);
    connect(m_line, &ChatLineEdit::attachmentsChanged, this, [this](const QStringList &files) {
        auto t = m_chatShown ? m_chatShown->current() : nullptr;
        if (!t) return;
        if (files.isEmpty()) { t->info(tr("Attachments removed")); return; }
        QStringList names;
        for (auto &f : files) names << "<b>" + QFileInfo(f).fileName().toHtmlEscaped() + "</b>";
        t->info(tr("Attached %1, sent with your next message (Esc removes)").arg(names.join(", ")));
    });
    connect(m_line, &ChatLineEdit::typed, this, [this] {
        auto t = m_chatShown ? m_chatShown->current() : nullptr;
        if (!t || t->channelId().isEmpty()) return;
        auto now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastTyping < 8000) return;
        m_lastTyping = now;
        m_core->send("typing", {{"channel_id", t->channelId()}});
    });
}

void MainWindow::buildStatus() {
    auto sb = statusBar();
    m_news = new NewsWidget(sb);
    sb->addWidget(m_news, 1);
    auto sync = new com::teamspeak::ui::SyncStatusWidget(sb);
    sb->addPermanentWidget(sync);
    m_errors = new StatusErrorDisplay(sb);
    sb->addPermanentWidget(m_errors);
    m_status = new ServerStatusWidget(sb);
    sb->addPermanentWidget(m_status);
}

ServerView *MainWindow::current() const { return m_views->current(); }

void MainWindow::showChat(ServerView *v) {
    auto want = v ? v->chat() : m_noServerChat;
    if (want == m_chatShown) return;
    if (m_chatShown) {
        m_chatLayout->removeWidget(m_chatShown);
        m_chatShown->hide();
    }
    want->setParent(m_chatWidget);
    m_chatLayout->insertWidget(0, want);
    want->show();
    m_chatShown = want;
}

void MainWindow::wire() {
    connect(m_core, &CoreLink::event, m_store, &Store::handle);
    connect(m_core, &CoreLink::died, this, [this](const QString &why) {
        m_connected = false;
        updateStatus();
        if (auto t = m_chatShown->serverTab()) t->error(why.toHtmlEscaped());
        m_errors->addError(why);
    });
    connect(m_store, &Store::needLogin, this, [this] {
        if (m_login) return;
        m_connected = false;
        updateStatus();
        auto d = new com::teamspeak::ui::Account_Create_Login_Dialog(m_store, m_core, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        m_login = d;
        d->show();
    });
    connect(m_store, &Store::readyChanged, this, [this] {
        m_connected = true;
        updateBookmarks();
        updateStatus();
        // reopen the guilds from last time, else the first one
        QStringList open = QSettings().value("openGuilds").toStringList();
        bool any = false;
        for (auto &id : open)
            if (m_store->guilds.contains(id) && !m_views->view(id)) { openGuild(id, true); any = true; }
        if (!any && !m_views->count() && !m_store->guildOrder.isEmpty()) openGuild(m_store->guildOrder.first(), true);
    });
    connect(m_store, &Store::connectionChanged, this, [this](const QString &state, const QString &msg) {
        m_connected = state == "connected";
        updateStatus();
        if (state == "reconnecting" || state == "disconnected") {
            for (int i = 0; i < m_views->count(); ++i)
                if (auto v = qobject_cast<ServerView *>(m_views->widget(i)))
                    v->chat()->serverTab()->info(state == "reconnecting" ? tr("Lost connection, trying to reconnect") : tr("Disconnected from server") + (msg.isEmpty() ? "" : ": " + msg.toHtmlEscaped()),
                                                 "TextMessage_ClientDropped");
            playSound("CONNECTION_LOST_CONNECTION");
        }
    });
    connect(m_store, &Store::guildChanged, this, [this](const QString &gid) {
        updateBookmarks();
        updateStatus();
        if (auto v = m_views->view(gid)) {
            v->rebuild();
            v->info()->refresh();
            m_views->setTabText(m_views->indexOf(v), m_store->guilds[gid].name);
            v->chat()->setTabText(0, m_store->guilds[gid].name);
        }
        // a snapshot we asked for
        if (gid == m_pendingGuild && m_store->guilds[gid].loaded) {
            m_pendingGuild.clear();
            auto v = m_views->view(gid);
            if (!v) return;
            auto st = v->chat()->serverTab();
            auto &g = m_store->guilds[gid];
            st->info(tr("Connected to Server: <a href=\"channelid://0\" class=\"TextMessage_ServerLink\">\"%1\"</a>").arg(g.name.toHtmlEscaped()), "TextMessage_Connected");
            playSound("CONNECTION_CONNECTED");
            // reopen last text channel, else the first one we can write to
            auto last = QSettings().value("lastChannel/" + gid).toString();
            if (!g.channels.contains(last) || !g.channels[last].canView || !g.channels[last].isText()) {
                last.clear();
                QVector<const Channel *> texts;
                for (auto &c : g.channels) if (c.isText() && c.canView) texts << &c;
                std::sort(texts.begin(), texts.end(), [&](auto a, auto b) {
                    auto pa = g.channels.contains(a->parentId) ? g.channels[a->parentId].position : -1;
                    auto pb = g.channels.contains(b->parentId) ? g.channels[b->parentId].position : -1;
                    if (pa != pb) return pa < pb;
                    return a->position < b->position;
                });
                for (auto c : texts) if (c->canSend) { last = c->id; break; }
                if (last.isEmpty() && !texts.isEmpty()) last = texts.first()->id;
            }
            if (!last.isEmpty()) switchText(v, last);
            auto self = v->model()->indexOf(TreeNode::Client, m_store->me.id);
            if (self.isValid()) v->tree()->setCurrentIndex(self);
            else v->info()->showServer(gid);
        }
    });
    connect(m_store, &Store::guildRemoved, this, [this](const QString &gid) {
        updateBookmarks();
        if (auto v = m_views->view(gid)) {
            m_views->removeTab(m_views->indexOf(v));
            v->chat()->deleteLater();
            v->deleteLater();
            QTimer::singleShot(0, this, [this] { showChat(current()); updateStatus(); });
        }
    });
    connect(m_store, &Store::messagesLoaded, this, [this](const QString &cid) {
        for (int i = 0; i < m_views->count(); ++i)
            if (auto v = qobject_cast<ServerView *>(m_views->widget(i)); v && v->textChannel() == cid)
                v->chat()->channelTab()->history(m_store->messages[cid], m_store->me.id);
        if (auto t = privateTab(cid, false)) t->history(m_store->messages[cid], m_store->me.id);
    });
    connect(m_store, &Store::messageAdded, this, &MainWindow::onMessage);
    connect(m_store, &Store::messageUpdated, this, [this](const Message &m) {
        // rebuild the affected log from the store
        for (int i = 0; i < m_views->count(); ++i)
            if (auto v = qobject_cast<ServerView *>(m_views->widget(i)); v && v->textChannel() == m.channelId)
                v->chat()->channelTab()->history(m_store->messages[m.channelId], m_store->me.id);
        if (auto t = privateTab(m.channelId, false)) t->history(m_store->messages[m.channelId], m_store->me.id);
    });
    connect(m_store, &Store::messageRemoved, this, [this](const QString &cid, const QString &) {
        for (int i = 0; i < m_views->count(); ++i)
            if (auto v = qobject_cast<ServerView *>(m_views->widget(i)); v && v->textChannel() == cid)
                v->chat()->channelTab()->history(m_store->messages[cid], m_store->me.id);
        if (auto t = privateTab(cid, false)) t->history(m_store->messages[cid], m_store->me.id);
    });
    connect(m_store, &Store::voiceChanged, this, &MainWindow::onVoice);
    connect(m_store, &Store::speakingChanged, this, [this](const QString &uid, bool) {
        for (int i = 0; i < m_views->count(); ++i)
            if (auto v = qobject_cast<ServerView *>(m_views->widget(i))) v->model()->refreshClient(uid);
    });
    connect(m_store, &Store::presenceChanged, this, [this](const QString &uid) {
        for (int i = 0; i < m_views->count(); ++i)
            if (auto v = qobject_cast<ServerView *>(m_views->widget(i))) v->model()->refreshClient(uid);
    });
    connect(m_store, &Store::voiceStatus, this, [this](const QString &state, const QString &msg) {
        updateStatus();
        auto lastVoice = m_lastVoice;
        Q_UNUSED(lastVoice);
        updateSelfActions();
        if (auto v = m_views->view(m_store->voiceGuild.isEmpty() ? QString() : m_store->voiceGuild)) {
            if (state == "connected" && m_lastVoice != m_store->voiceChannel) {
                auto c = m_store->channel(m_store->voiceChannel);
                v->chat()->serverTab()->info(tr("You switched to channel <a href=\"channelid://%1\" class=\"TextMessage_ChannelLink\">\"%2\"</a>")
                                                 .arg(m_store->voiceChannel, c ? c->name.toHtmlEscaped() : QString()),
                                             "TextMessage_ClientSwitched");
                // switched by us, or moved by someone else
                playSound(m_wantVoice == m_store->voiceChannel ? "YOU_SWITCHED_CHANNEL" : "YOU_WERE_MOVED_TO_DIFFERENT_CHANNEL");
            }
        }
        m_lastVoice = m_store->voiceChannel;
        if (!msg.isEmpty() && state == "disconnected")
            if (auto t = m_chatShown->serverTab()) t->error(msg.toHtmlEscaped());
    });
    connect(m_store, &Store::typing, this, [this](const QString &cid, const User &u) {
        if (u.id == m_store->me.id || m_store->silencedUser(u.id)) return;
        auto c = m_store->channel(cid);
        if (!c || (c->guildId.isEmpty() && c->muted)) return;
        m_news->setNews(QString("<b>%1</b> is typing in %2").arg(u.name().toHtmlEscaped(), c->guildId.isEmpty() ? tr("a private chat") : "#" + c->name.toHtmlEscaped()));
        QTimer::singleShot(10000, this, [this] { m_news->setNews({}); });
    });
    connect(m_store, &Store::dmOpened, this, [this](const QString &cid) { openPrivate(cid, true); });
    connect(m_store, &Store::unreadChanged, this, [this](const QString &cid) {
        auto gid = m_store->guildOf(cid);
        if (auto v = m_views->view(gid)) v->model()->dataChanged(v->model()->indexOf(TreeNode::Channel, cid), v->model()->indexOf(TreeNode::Channel, cid));
    });
    connect(m_store, &Store::error, this, [this](const QString &msg) {
        if (auto t = m_chatShown->serverTab()) t->error(msg.toHtmlEscaped());
        m_errors->addError(msg);
        playSound("SERVER_ERROR");
    });
    connect(m_views, &QTabWidget::currentChanged, this, [this] {
        showChat(current());
        updateStatus();
    });
}

void MainWindow::openGuild(const QString &gid, bool newTab) {
    if (auto v = m_views->view(gid)) {
        m_views->setCurrentWidget(v);
        return;
    }
    if (!newTab && current()) {
        auto old = current();
        m_views->removeTab(m_views->indexOf(old));
        old->chat()->deleteLater();
        old->deleteLater();
    }
    auto v = new ServerView(m_store, m_core, gid);
    auto &g = m_store->guilds[gid];
    int i = m_views->addTab(v, skin::icon("SERVER_GREEN"), g.name);
    m_views->setCurrentIndex(i);
    showChat(v);
    v->setSizes({541, 541});
    auto st = v->chat()->serverTab();
    st->info(tr("Trying to connect to server on <b>%1</b>").arg(g.name.toHtmlEscaped()));
    st->info(g.description.isEmpty() ? tr("Welcome to %1").arg(g.name.toHtmlEscaped()) : g.description.toHtmlEscaped(), "TextMessage_Welcome");
    connect(v->tree(), &TreeView::selectedNode, this, [this, v](TreeNode::Kind k, const QString &id) {
        if (k == TreeNode::Server) v->info()->showServer(v->guildId());
        else if (k == TreeNode::Channel || k == TreeNode::Spacer) v->info()->showChannel(v->guildId(), id);
        else v->info()->showClient(v->guildId(), id);
    });
    connect(v->tree(), &TreeView::activatedNode, this, [this, v](TreeNode::Kind k, const QString &id) {
        if (k == TreeNode::Channel) {
            auto c = m_store->channel(id);
            if (!c) return;
            if (c->isVoice()) {
                if (!c->canConnect) { v->chat()->serverTab()->error(tr("insufficient client permissions")); playSound("SERVER_INSUFFICIENT_PERMISSIONS"); return; }
                m_wantVoice = id;
                m_core->send("join_voice", {{"guild_id", v->guildId()}, {"channel_id", id}});
            } else {
                switchText(v, id);
                v->chat()->setCurrentWidget(v->chat()->channelTab());
            }
        } else if (k == TreeNode::Client && id != m_store->me.id) {
            m_core->send("open_dm", {{"user_id", id}});
        }
    });
    connect(v->tree(), &TreeView::contextMenuNode, this, [this](TreeNode::Kind k, const QString &id, const QPoint &pos) { contextMenu(int(k), id, pos); });
    connect(v->chat(), &ChatTabWidget::privateClosed, this, [](const QString &) {});
    connect(v->chat()->channelTab(), &ChatTab::scrolledToTop, this, [this, v] {
        auto cid = v->textChannel();
        auto &list = m_store->messages[cid];
        if (!m_store->hasMore.value(cid) || list.isEmpty()) return;
        m_store->hasMore[cid] = false;
        m_core->send("fetch_messages", {{"channel_id", cid}, {"before", list.first().id}, {"limit", 50}});
    });
    for (auto t : {v->chat()->serverTab(), v->chat()->channelTab()})
        connect(t, &QTextBrowser::anchorClicked, this, [this, v](const QUrl &u) { onLink(v, u); });
    m_pendingGuild = gid;
    m_core->send("open_guild", {{"guild_id", gid}});
    if (g.loaded) emit m_store->guildChanged(gid);
    updateStatus();
}

void MainWindow::onLink(ServerView *v, const QUrl &u) {
    if (u.scheme() == "attachurl") {
        // a pasted cdn / media link: attachment popup straight from the url
        auto raw = QUrl::fromPercentEncoding(u.toString(QUrl::FullyEncoded).mid(10).toUtf8());
        QUrl target(raw);
        Attachment a;
        a.url = raw;
        a.filename = QFileInfo(target.path()).fileName();
        (new AttachmentDialog(a, this))->show();
        return;
    }
    if (u.scheme() == "attachment") {
        // attachment://<channel>/<message>/<index>
        auto parts = u.path().split('/', Qt::SkipEmptyParts);
        if (parts.size() != 2) return;
        for (auto &m : m_store->messages.value(u.host()))
            if (m.id == parts[0] && parts[1].toInt() < m.attachments.size()) {
                auto d = new AttachmentDialog(m.attachments[parts[1].toInt()], this);
                d->show();
                return;
            }
        return;
    }
    if (!v) {
        if (u.scheme().startsWith("http")) QDesktopServices::openUrl(u);
        return;
    }
    if (u.scheme() == "channelid") {
        if (u.host() == "0" || u.toString() == "channelid://0") v->info()->showServer(v->guildId());
        else {
            auto i = v->model()->indexOf(TreeNode::Channel, u.toString().mid(12));
            if (i.isValid()) v->tree()->setCurrentIndex(i);
        }
    } else if (u.scheme() == "client") {
        auto id = u.toString().mid(9).section('/', 0, 0);
        auto i = v->model()->indexOf(TreeNode::Client, id);
        if (i.isValid()) v->tree()->setCurrentIndex(i);
        else v->info()->showClient(v->guildId(), id);
    } else QDesktopServices::openUrl(u);
}

void MainWindow::switchText(ServerView *v, QString cid) {
    if (v->textChannel() == cid) return;
    auto c = m_store->channel(cid);
    if (!c) return;
    bool first = v->textChannel().isEmpty();
    v->openText(cid);
    if (!first) playSound("YOU_SWITCHED_CHANNEL");
    QSettings().setValue("lastChannel/" + v->guildId(), cid);
    auto ct = v->chat()->channelTab();
    auto wasCurrent = v->chat()->currentWidget();
    Q_UNUSED(wasCurrent);
    if (!m_store->messages[cid].isEmpty()) ct->history(m_store->messages[cid], m_store->me.id);
    m_core->send("fetch_messages", {{"channel_id", cid}, {"limit", 50}});
    if (!c->topic.isEmpty()) ct->info(tr("Channel topic: %1").arg(c->topic.section('\n', 0, 0).toHtmlEscaped()));
    auto i = v->model()->indexOf(TreeNode::Client, m_store->me.id);
    if (i.isValid()) v->tree()->setCurrentIndex(i);
    if (!c->lastMessageId.isEmpty()) m_core->send("ack", {{"channel_id", cid}, {"message_id", c->lastMessageId}});
}

ChatTab *MainWindow::privateTab(const QString &cid, bool create) {
    auto v = current();
    if (!v) return nullptr;
    if (auto t = v->chat()->find(cid)) return t;
    if (!create) return nullptr;
    auto c = m_store->channel(cid);
    QString title;
    if (c) {
        QStringList names;
        for (auto &u : c->recipients) names << u.name();
        title = c->name.isEmpty() ? names.join(", ") : c->name;
    }
    auto t = v->chat()->addChat(ChatTab::PrivateTab, cid, title);
    connect(t, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) { onLink(current(), u); });
    connect(t, &ChatTab::scrolledToTop, this, [this, cid] {
        auto &list = m_store->messages[cid];
        if (!m_store->hasMore.value(cid) || list.isEmpty()) return;
        m_store->hasMore[cid] = false;
        m_core->send("fetch_messages", {{"channel_id", cid}, {"before", list.first().id}, {"limit", 50}});
    });
    if (!m_store->messages[cid].isEmpty()) t->history(m_store->messages[cid], m_store->me.id);
    m_core->send("fetch_messages", {{"channel_id", cid}, {"limit", 50}});
    return t;
}

void MainWindow::openPrivate(const QString &cid, bool focus) {
    auto t = privateTab(cid, true);
    if (t && focus) current()->chat()->setCurrentWidget(t);
}

void MainWindow::submit(const QString &text) {
    auto t = m_chatShown ? m_chatShown->current() : nullptr;
    if (!t) return;
    auto files = m_line->attachments();
    if (t->kind() == ChatTab::ServerTab || t->channelId().isEmpty()) {
        t->error(tr("Select a text channel first: double-click it in the channel tree."));
        return;
    }
    auto c = m_store->channel(t->channelId());
    if (c && !c->canSend) {
        t->error(tr("insufficient client permissions (failed on b_client_channel_textmessage_send)"));
        playSound("SERVER_INSUFFICIENT_PERMISSIONS");
        return;
    }
    Message m;
    m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.channelId = t->channelId();
    m.author = m_store->me;
    if (auto v = current()) m.author.display = m_store->myName(v->guildId());
    m.content = text;
    m.timestamp = QDateTime::currentDateTimeUtc();
    m.pending = true;
    m.nonce = QString::number(QDateTime::currentMSecsSinceEpoch() << 22);
    QJsonArray paths;
    for (auto &f : files) {
        QImageReader r(f);
        auto sz = r.size();
        m.attachments << Attachment{QFileInfo(f).fileName(), QUrl::fromLocalFile(f).toString(), {}, QFileInfo(f).size(), sz.width(), sz.height()};
        paths << f;
    }
    m_store->messages[m.channelId] << m;
    t->message(m, true);
    QJsonObject cmd{{"channel_id", m.channelId}, {"content", text}, {"nonce", m.nonce}};
    if (!paths.isEmpty()) cmd["files"] = paths;
    m_core->send("send_message", cmd);
    m_line->blockSignals(true);
    m_line->clearAttachments();
    m_line->blockSignals(false);
    m_lastTyping = 0;
    playSound(t->kind() == ChatTab::PrivateTab ? "CHAT_SENT_MESSAGE_CLIENT" : "CHAT_SENT_MESSAGE_CHANNEL");
}

void MainWindow::onMessage(const Message &m) {
    bool own = m.author.id == m_store->me.id;
    bool shown = false;
    for (int i = 0; i < m_views->count(); ++i) {
        auto v = qobject_cast<ServerView *>(m_views->widget(i));
        if (!v) continue;
        if (v->textChannel() == m.channelId) {
            v->chat()->channelTab()->message(m, own);
            v->chat()->markUnread(v->chat()->channelTab(), !own);
            shown = true;
            if (v == current() && v->chat()->current() == v->chat()->channelTab() && isActiveWindow())
                m_core->send("ack", {{"channel_id", m.channelId}, {"message_id", m.id}});
        } else if (!m.guildId.isEmpty() && v->guildId() == m.guildId && m.mentionsMe && !m_store->silenced(m)) {
            auto c = m_store->channel(m.channelId);
            v->chat()->serverTab()->info(tr("<a class=\"TextMessage_UserLink\" href=\"client://%1\">\"%2\"</a> mentioned you in <a class=\"TextMessage_ChannelLink\" href=\"channelid://%3\">\"%4\"</a>: %5")
                                             .arg(m.author.id, m.author.name().toHtmlEscaped(), m.channelId, c ? c->name.toHtmlEscaped() : QString(), markdownToHtml(m.content)),
                                         "TextMessage_Poke");
            v->chat()->markUnread(v->chat()->serverTab(), true);
            playSound("OTHER_RECEIVED_POKE");
        }
    }
    bool quiet = !own && m_store->silenced(m);
    if (m.guildId.isEmpty()) {
        // private chat; blocked / ignored people and muted dms never pop a tab open
        auto t = privateTab(m.channelId, !own && !quiet);
        if (t) {
            if (!shown) t->message(m, own);
            current()->chat()->markUnread(t, !own && !quiet);
            shown = true;
        }
    }
    if (shown && !own && !quiet) playSound(m.guildId.isEmpty() ? "CHAT_RECEIVED_MESSAGE_CLIENT" : "CHAT_RECEIVED_MESSAGE_CHANNEL");
    if (!shown && !m.guildId.isEmpty()) {
        // unread bold in the tree
        auto &g = m_store->guilds[m.guildId];
        if (g.channels.contains(m.channelId) && !g.channels[m.channelId].unread) {
            g.channels[m.channelId].unread = true;
            if (m.mentionsMe) g.channels[m.channelId].mentions++;
            if (auto v = m_views->view(m.guildId)) {
                auto i = v->model()->indexOf(TreeNode::Channel, m.channelId);
                emit v->model()->dataChanged(i, i);
            }
        }
    }
}

void MainWindow::onVoice(const QString &gid, const QString &uid, const QString &oldCh, const QString &newCh) {
    auto v = m_views->view(gid);
    if (!v) return;
    v->rebuild();
    v->info()->refresh();
    if (oldCh == newCh) return;
    auto &g = m_store->guilds[gid];
    bool self = uid == m_store->me.id;
    QString name;
    if (g.voice.contains(uid)) name = g.voice[uid].user.name();
    else name = self ? m_store->me.name() : QString();
    auto chan = [&](const QString &id) {
        auto c = m_store->channel(id);
        return QString("<a href=\"channelid://%1\" class=\"TextMessage_ChannelLink\">\"%2\"</a>").arg(id, c ? c->name.toHtmlEscaped() : QString());
    };
    auto who = QString("<a href=\"client://%1\" class=\"TextMessage_UserLink\">\"%2\"</a>").arg(uid, name.toHtmlEscaped());
    auto st = v->chat()->serverTab();
    auto mine = m_store->voiceChannel;
    if (self) {
        if (newCh.isEmpty()) {
            st->info(tr("You left the voice channel %1").arg(chan(oldCh)), "TextMessage_Disconnected");
            if (!m_wantVoice.isEmpty() && m_wantVoice == oldCh) playSound("YOU_WERE_KICKED_FROM_CHANNEL");
            m_wantVoice.clear();
        }
        updateSelfActions();
        return;
    }
    if (oldCh.isEmpty()) {
        st->info(tr("%1 connected to channel %2").arg(who, chan(newCh)), "TextMessage_ClientConnected");
        if (!mine.isEmpty() && newCh == mine) playSound("CLIENT_CONNECTION_CONNECTED_CURRENT_CHANNEL", uid);
    } else if (newCh.isEmpty()) {
        st->info(tr("%1 disconnected").arg(who), "TextMessage_ClientDisconnected");
        if (!mine.isEmpty() && oldCh == mine) playSound("CLIENT_CONNECTION_DISCONNECTED_CURRENT_CHANNEL", uid);
    } else {
        st->info(tr("%1 switched from channel %2 to %3").arg(who, chan(oldCh), chan(newCh)), "TextMessage_ClientSwitched");
        if (!mine.isEmpty() && newCh == mine) playSound("CLIENT_SWITCHED_TO_CURRENT_CHANNEL_APPEARS", uid);
        else if (!mine.isEmpty() && oldCh == mine) playSound("CLIENT_SWITCHED_FROM_CURRENT_CHANNEL_DISAPPEARS", uid);
    }
}

void MainWindow::contextMenu(int kind, const QString &id, const QPoint &pos) {
    auto v = current();
    if (!v) return;
    QMenu m(this);
    if (kind == TreeNode::Channel) {
        auto c = m_store->channel(id);
        if (!c) return;
        if (c->isVoice()) {
            auto a = m.addAction(skin::icon("CHANNEL_SWITCH"), tr("&Switch to Channel"));
            a->setEnabled(c->canConnect && m_store->voiceChannel != id);
            connect(a, &QAction::triggered, this, [this, v, id] {
                m_wantVoice = id;
                m_core->send("join_voice", {{"guild_id", v->guildId()}, {"channel_id", id}});
            });
            if (m_store->voiceChannel == id) connect(m.addAction(skin::icon("DISCONNECT"), tr("&Leave Channel")), &QAction::triggered, this, [this] { m_wantVoice.clear(); m_core->send("leave_voice"); });
        } else {
            connect(m.addAction(skin::icon("CHANNEL_SWITCH"), tr("&Switch to Channel")), &QAction::triggered, this, [this, v, id] { switchText(v, id); });
            connect(m.addAction(skin::icon("APPLY"), tr("Mark as Read")), &QAction::triggered, this, [this, id] {
                auto c = m_store->channel(id);
                if (c && !c->lastMessageId.isEmpty()) m_core->send("ack", {{"channel_id", id}, {"message_id", c->lastMessageId}});
            });
        }
        m.addSeparator();
        connect(m.addAction(skin::icon("COPY_URL"), tr("Copy Channel Link")), &QAction::triggered, this, [v, id] {
            QApplication::clipboard()->setText(QString("https://discord.com/channels/%1/%2").arg(v->guildId(), id));
        });
    } else if (kind == TreeNode::Client) {
        if (id != m_store->me.id)
            connect(m.addAction(skin::icon("NEW_CHAT"), tr("Open Text Chat")), &QAction::triggered, this, [this, id] { m_core->send("open_dm", {{"user_id", id}}); });
        connect(m.addAction(skin::icon("COPY"), tr("Copy User ID")), &QAction::triggered, this, [id] { QApplication::clipboard()->setText(id); });
        if (id == m_store->me.id && !m_store->voiceChannel.isEmpty())
            connect(m.addAction(skin::icon("DISCONNECT"), tr("Disconnect from Voice")), &QAction::triggered, this, [this] { m_wantVoice.clear(); m_core->send("leave_voice"); });
    } else if (kind == TreeNode::Server) {
        connect(m.addAction(skin::icon("COPY_URL"), tr("Copy Server Link")), &QAction::triggered, this, [v] {
            QApplication::clipboard()->setText("https://discord.com/channels/" + v->guildId());
        });
        connect(m.addAction(skin::icon("CHANNEL_COLLAPSE_ALL"), tr("Collapse All")), &QAction::triggered, v->tree(), &QTreeView::collapseAll);
        connect(m.addAction(skin::icon("CHANNEL_EXPAND_ALL"), tr("Expand All")), &QAction::triggered, v->tree(), &QTreeView::expandAll);
    }
    if (!m.isEmpty()) m.exec(pos);
}

void MainWindow::updateStatus() {
    auto v = current();
    QString text = !m_connected ? tr("Disconnected") : v ? tr("Connected as %1").arg(m_store->myName(v->guildId()))
                                                         : tr("Disconnected");
    int ping = m_store->voiceState == "connected" ? m_store->ping : 0;
    m_status->setStatus(text, ping, m_connected && v);
    if (v) setWindowTitle("TeamSpeak 3 - " + m_store->guilds[v->guildId()].name);
    else setWindowTitle("TeamSpeak 3");
}

void MainWindow::updateSelfActions() {
    auto vs = m_store->myVoice();
    bool mute = vs ? vs->selfMute : m_actMuteMic->isChecked();
    bool deaf = vs ? vs->selfDeaf : m_actMuteSpk->isChecked();
    m_actMuteMic->setChecked(mute);
    m_actMuteSpk->setChecked(deaf);
    m_actMic->setEnabled(mute);
    m_actAway->setEnabled(!m_away);
    m_actOnline->setEnabled(m_away);
}

void MainWindow::notify(const QString &html) { m_news->setNews(html); }

void MainWindow::playSound(const QString &event, const QString &userId) {
    static QHash<QString, QSoundEffect *> cache;
    auto pack = QSettings().value("soundpack", "default").toString();
    if (pack == "nosounds") return;
    auto dir = skin::ts3Dir() + "/sound/" + pack + "/";
    QString file;
    {
        static QHash<QString, QHash<QString, QString>> maps;
        auto &map = maps[pack];
        if (map.isEmpty()) {
            QFile ini(dir + "settings.ini");
            if (ini.open(QFile::ReadOnly))
                for (auto l : QString::fromUtf8(ini.readAll()).split('\n')) {
                    auto k = l.section('=', 0, 0).trimmed(), val = l.section('=', 1).trimmed();
                    if (val.startsWith("play(\"")) map[k] = val.mid(6, val.indexOf("\")") - 6);
                }
        }
        file = map.value(event);
        // friend / blocked voices like ts3's contact list
        QString type = "neutral";
        for (auto &[u, t] : m_store->friends)
            if (u.id == userId) type = t == 1 ? "friend" : t == 2 ? "blocked_user" : "neutral";
        file.replace("${clientType}", type);
    }
    if (file.isEmpty() || !QFile::exists(dir + file)) return;
    auto &fx = cache[dir + file];
    if (!fx) {
        fx = new QSoundEffect(this);
        fx->setSource(QUrl::fromLocalFile(dir + file));
        fx->setVolume(0.7);
    }
    fx->play();
}

#include "mainwindow.moc"
