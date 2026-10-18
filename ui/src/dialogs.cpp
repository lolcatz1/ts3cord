#include "dialogs.h"

#include "avatars.h"
#include "corelink.h"
#include "skin.h"
#include "store.h"

#include <QBuffer>
#include <QProcess>
#include <QFileInfo>
#include <QSlider>
#include <QToolButton>
#include <memory>
#include "mpvwidget.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QMovie>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QScreen>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTimer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <qrencode.h>

QImage qrImage(const QString &text, int scale) {
    auto q = QRcode_encodeString(text.toUtf8().constData(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!q) return {};
    int n = q->width, border = 4;
    QImage img((n + 2 * border) * scale, (n + 2 * border) * scale, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (q->data[y * n + x] & 1) p.fillRect((x + border) * scale, (y + border) * scale, scale, scale, Qt::black);
    QRcode_free(q);
    return img;
}

namespace com::teamspeak::ui {

Account_Create_Login_Dialog::Account_Create_Login_Dialog(Store *s, CoreLink *core, QWidget *parent)
    : QDialog(parent), m_store(s), m_core(core) {
    setWindowTitle(tr("Discord Account"));
    resize(438, 290);
    auto v = new QVBoxLayout(this);
    auto banner = new QLabel(tr("Login to your Discord Account"), this);
    banner->setObjectName("accountLoginBanner");
    v->addWidget(banner);
    auto form = new QFormLayout;
    m_email = new QLineEdit(this);
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Email Address:"), m_email);
    form->addRow(tr("Password:"), m_pass);
    v->addLayout(form);
    v->addSpacing(8);
    auto row = new QHBoxLayout;
    auto stay = new QCheckBox(tr("Stay logged in on this computer"), this);
    stay->setChecked(true);
    stay->setEnabled(false);
    auto info = new QLabel(this);
    info->setPixmap(skin::pixmap("ABOUT", 16));
    info->setToolTip(tr("The token is kept in the system keyring."));
    auto qr = new QLabel(QString("<a href=\"qr\">%1</a>").arg(tr("Login with QR Code")), this);
    row->addWidget(stay);
    row->addWidget(info);
    row->addStretch();
    row->addWidget(qr);
    v->addLayout(row);
    v->addSpacing(8);
    v->addWidget(new QLabel(tr("Discord Account Features:"), this));
    auto featRow = new QHBoxLayout;
    featRow->setContentsMargins(16, 0, 0, 0);
    auto voice = new QCheckBox(tr("Voice"), this);
    voice->setChecked(true);
    voice->setEnabled(false);
    auto vinfo = new QLabel(this);
    vinfo->setPixmap(skin::pixmap("ABOUT", 16));
    vinfo->setToolTip(tr("Voice channels use Discord's end-to-end encrypted voice."));
    featRow->addWidget(voice);
    featRow->addWidget(vinfo);
    featRow->addStretch();
    v->addLayout(featRow);
    m_error = new QLabel(this);
    m_error->setStyleSheet("color: #c9070a;");
    m_error->setWordWrap(true);
    v->addWidget(m_error);
    v->addStretch();
    auto buttons = new QHBoxLayout;
    auto token = new QPushButton(tr("Use Token"), this);
    auto qrBtn = new QPushButton(tr("QR Code"), this);
    auto login = new QPushButton(tr("Login"), this);
    login->setDefault(true);
    buttons->addWidget(token);
    buttons->addStretch();
    buttons->addWidget(qrBtn);
    buttons->addWidget(login);
    connect(qrBtn, &QPushButton::clicked, this, [this] { m_core->send("qr_start"); });
    v->addLayout(buttons);
    connect(login, &QPushButton::clicked, this, [this] {
        m_error->clear();
        m_core->send("login_password", {{"login", m_email->text()}, {"password", m_pass->text()}});
    });
    connect(token, &QPushButton::clicked, this, &Account_Create_Login_Dialog::askToken);
    connect(qr, &QLabel::linkActivated, this, [this] { m_core->send("qr_start"); });
    connect(s, &Store::loginError, this, &Account_Create_Login_Dialog::showError);
    connect(s, &Store::qrCode, this, &Account_Create_Login_Dialog::showQr);
    connect(s, &Store::mfaRequired, this, [this](const QString &ticket, const QStringList &) {
        bool ok = false;
        auto code = QInputDialog::getText(this, tr("Two-Factor Authentication"),
                                          tr("This account is protected with two-factor authentication.\nPlease enter the code from your authenticator app.\n\nEnter Code:"),
                                          QLineEdit::Normal, {}, &ok);
        if (ok) m_core->send("login_mfa", {{"ticket", ticket}, {"code", code.trimmed()}, {"method", "totp"}});
    });
    connect(s, &Store::readyChanged, this, &QDialog::accept);
}

void Account_Create_Login_Dialog::askToken() {
    bool ok = false;
    auto t = QInputDialog::getText(this, tr("Use Token"),
                                   tr("Please enter the Discord token of the account you want to use.\nIt is kept in the system keyring.\n\nEnter Token:"),
                                   QLineEdit::Password, {}, &ok);
    if (ok && !t.trimmed().isEmpty()) {
        m_error->clear();
        m_core->send("login_token", {{"token", t.trimmed()}});
    }
}

void Account_Create_Login_Dialog::showError(const QString &msg) {
    m_error->setText(msg.toHtmlEscaped());
}

void Account_Create_Login_Dialog::showQr(const QString &url) {
    auto d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(tr("Login with QR Code"));
    auto v = new QVBoxLayout(d);
    auto l = new QLabel(d);
    l->setPixmap(QPixmap::fromImage(qrImage(url, 5)));
    l->setAlignment(Qt::AlignCenter);
    v->addWidget(new QLabel(tr("Scan this code with the Discord mobile app."), d));
    v->addWidget(l);
    auto who = new QLabel(d);
    v->addWidget(who);
    auto bb = new QDialogButtonBox(QDialogButtonBox::Cancel, d);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, d, &QDialog::reject);
    connect(m_store, &Store::qrUser, d, [who](const QString &name, const QString &) {
        who->setText(QObject::tr("Confirm the login on your phone, <b>%1</b>.").arg(name.toHtmlEscaped()));
    });
    connect(m_store, &Store::readyChanged, d, &QDialog::accept);
    connect(m_store, &Store::loginError, d, &QDialog::reject);
    d->show();
}

}

ConnectDialog::ConnectDialog(Store *s, QWidget *parent) : QDialog(parent), m_store(s) {
    setWindowTitle(tr("Connect"));
    auto v = new QVBoxLayout(this);
    auto g = new QGridLayout;
    m_server = new QComboBox(this);
    m_server->setEditable(true);
    m_server->setInsertPolicy(QComboBox::NoInsert);
    m_server->setMinimumWidth(300);
    for (auto &id : s->guildOrder) {
        auto &gd = s->guilds[id];
        auto pm = Avatars::get()->image(gd.iconUrl);
        m_server->addItem(pm.isNull() ? skin::icon("SERVER_GREEN") : QIcon(pm), gd.name, id);
    }
    m_nick = new QLineEdit(s->me.name(), this);
    m_nick->setEnabled(false);
    g->addWidget(new QLabel(tr("Server Nickname or Address:"), this), 0, 0);
    g->addWidget(m_server, 0, 1);
    g->addWidget(new QLabel(tr("Nickname:"), this), 1, 0);
    g->addWidget(m_nick, 1, 1);
    v->addLayout(g);
    v->addStretch();
    auto row = new QHBoxLayout;
    auto inTab = new QPushButton(tr("In New Tab"), this);
    auto conn = new QPushButton(tr("Connect"), this);
    auto cancel = new QPushButton(tr("Cancel"), this);
    conn->setDefault(true);
    row->addStretch();
    row->addWidget(inTab);
    row->addWidget(conn);
    row->addWidget(cancel);
    v->addLayout(row);
    connect(conn, &QPushButton::clicked, this, &QDialog::accept);
    connect(inTab, &QPushButton::clicked, this, [this] { m_newTab = true; accept(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

QString ConnectDialog::guildId() const {
    int i = m_server->findText(m_server->currentText(), Qt::MatchFixedString);
    if (i < 0) i = m_server->findText(m_server->currentText(), Qt::MatchContains);
    return i >= 0 ? m_server->itemData(i).toString() : QString();
}

ContactsDialog::ContactsDialog(Store *s, CoreLink *, QWidget *parent) : QDialog(parent), m_store(s) {
    setWindowTitle(tr("Contacts"));
    resize(420, 380);
    auto v = new QVBoxLayout(this);
    m_list = new QTreeWidget(this);
    m_list->setObjectName("ContactsList");
    m_list->setHeaderLabels({tr("Nickname"), tr("Status")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setStretchLastSection(true);
    v->addWidget(m_list);
    auto bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // enter in the list opens the chat; close must not be the default button
    for (auto b : bb->buttons())
        if (auto pb = qobject_cast<QPushButton *>(b)) {
            pb->setAutoDefault(false);
            pb->setDefault(false);
        }
    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it) { emit openDm(it->data(0, Qt::UserRole).toString()); });
    connect(s, &Store::presenceChanged, this, &ContactsDialog::reload);
    reload();
}

void ContactsDialog::reload() {
    m_list->clear();
    for (auto &[u, type] : m_store->friends) {
        // 1 friend, 2 blocked, 3 incoming, 4 outgoing
        auto st = m_store->presence.value(u.id, "offline");
        auto status = type == 2 ? tr("Blocked") : type == 3 ? tr("Incoming request") : type == 4 ? tr("Outgoing request") : st;
        if (m_store->ignored.contains(u.id)) status += tr(" (ignored)");
        auto it = new QTreeWidgetItem(m_list, {u.name(), status});
        it->setIcon(0, skin::icon(type == 2 ? "REMOVE_FOE" : st == "offline" ? "PLAYER_OFF" : st == "idle" ? "AWAY" : "PRESENT"));
        it->setData(0, Qt::UserRole, u.id);
    }
}

static QString clock(double s) {
    int t = int(s);
    return t >= 3600 ? QString("%1:%2:%3").arg(t / 3600).arg(t / 60 % 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
}

AttachmentDialog::AttachmentDialog(const Attachment &a, QWidget *parent) : QDialog(parent), m_url(a.url), m_name(a.filename) {
    setWindowTitle(a.filename);
    setAttribute(Qt::WA_DeleteOnClose);
    auto v = new QVBoxLayout(this);
    m_body = new QVBoxLayout;
    v->addLayout(m_body, 1);
    auto info = new QLabel(QString("<b>%1</b> &nbsp; %2%3").arg(a.filename.toHtmlEscaped(),
                                                             a.size >= 1048576 ? QString::number(a.size / 1048576.0, 'f', 1) + " MB" : QString::number(qMax<qint64>(1, a.size / 1024)) + " KB",
                                                             a.width ? QString(" &nbsp; %1x%2").arg(a.width).arg(a.height) : QString()),
                           this);
    v->addWidget(info);
    auto row = new QHBoxLayout;
    auto web = new QPushButton(tr("Open in Webbrowser"), this);
    auto save = new QPushButton(tr("Save As..."), this);
    auto close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    row->addWidget(web);
    row->addStretch();
    row->addWidget(save);
    row->addWidget(close);
    v->addLayout(row);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(web, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_url)); });
    connect(save, &QPushButton::clicked, this, [this] {
        auto dest = QFileDialog::getSaveFileName(this, tr("Save Attachment"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + m_name);
        if (dest.isEmpty()) return;
        fetch([dest](const QByteArray &d) {
            QFile f(dest);
            if (f.open(QFile::WriteOnly)) f.write(d);
        });
    });

    auto type = a.contentType.toLower();
    auto ext = QFileInfo(a.filename).suffix().toLower();
    static const QStringList videoExt{"mp4", "webm", "mov", "mkv", "m4v", "avi", "wmv", "flv", "3gp", "ogv"};
    static const QStringList audioExt{"mp3", "ogg", "oga", "opus", "wav", "flac", "m4a", "aac", "wma", "weba"};
    bool video = type.startsWith("video/") || videoExt.contains(ext);
    bool audio = !video && (type.startsWith("audio/") || audioExt.contains(ext));
    // size: the media or image, capped to most of the screen
    QSize want(a.width ? a.width : 640, a.height ? a.height : 360);
    if (audio) want = QSize(420, 90);
    auto avail = screen() ? screen()->availableGeometry().size() * 0.8 : QSize(1200, 800);
    want = want.scaled(want.boundedTo(avail), Qt::KeepAspectRatio).expandedTo(QSize(360, audio ? 90 : 200));
    resize(want + QSize(30, audio ? 110 : 90));
    if (video || audio) {
        showMedia(audio, false);
        return;
    }
    m_view = new QLabel(tr("Loading..."), this);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setObjectName("AttachmentView");
    m_view->setMinimumSize(1, 1);
    m_view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_body->addWidget(m_view);
    fetch([this](const QByteArray &d) { showImage(d); });
}

void AttachmentDialog::fetch(std::function<void(const QByteArray &)> done) {
    if (!m_data.isEmpty()) { done(m_data); return; }
    QUrl u(m_url);
    if (u.isLocalFile()) {
        QFile f(u.toLocalFile());
        m_data = f.open(QFile::ReadOnly) ? f.readAll() : QByteArray();
        QTimer::singleShot(0, this, [this, done] { done(m_data); });
        return;
    }
    static QNetworkAccessManager net;
    QNetworkRequest req(u);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto r = net.get(req);
    connect(this, &QObject::destroyed, r, &QNetworkReply::abort);
    connect(r, &QNetworkReply::finished, this, [r, done, this] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            if (m_view) m_view->setText(tr("Could not load the attachment: %1").arg(r->errorString().toHtmlEscaped()));
            return;
        }
        m_data = r->readAll();
        done(m_data);
    });
}

void AttachmentDialog::showImage(const QByteArray &data) {
    auto buf = new QBuffer(this);
    buf->setData(data);
    buf->open(QIODevice::ReadOnly);
    QImageReader probe(buf);
    if (probe.canRead() && probe.supportsAnimation() && probe.imageCount() != 1) {
        buf->seek(0);
        auto mv = new QMovie(buf, QByteArray(), this);
        if (mv->isValid()) {
            m_view->setMovie(mv);
            mv->start();
            // fit big animations into the window
            auto fit = [this, mv] {
                auto sz = mv->currentPixmap().size();
                if (sz.isEmpty()) return;
                mv->setScaledSize(sz.scaled(sz.boundedTo(m_view->size()), Qt::KeepAspectRatio));
            };
            connect(mv, &QMovie::started, this, fit);
            return;
        }
    }
    QImage img;
    if (img.loadFromData(data)) {
        auto pm = QPixmap::fromImage(img);
        auto box = m_view->size();
        if (pm.width() > box.width() || pm.height() > box.height()) pm = pm.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_view->setPixmap(pm);
        return;
    }
    // formats qt can't decode (animated webp/avif, odd gifs): let mpv try
    m_view->hide();
    showMedia(false, true);
}

void AttachmentDialog::showMedia(bool audio, bool loop) {
    m_player = new MpvWidget(this);
    if (audio) {
        // audio: no picture area, just a header
        m_player->setAudioOnly(true);
        m_player->hide();
        auto head = new QLabel(QString("<span style=\"font-size:x-large\">&#9835;</span> %1").arg(m_name.toHtmlEscaped()), this);
        head->setAlignment(Qt::AlignCenter);
        m_body->addWidget(head, 1);
    }
    m_body->addWidget(m_player, audio ? 0 : 1);
    auto ctl = new QHBoxLayout;
    auto play = new QToolButton(this);
    play->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    play->setAutoRaise(true);
    auto pos = new QSlider(Qt::Horizontal, this);
    pos->setRange(0, 0);
    auto time = new QLabel("0:00 / 0:00", this);
    auto volIcon = new QLabel(this);
    volIcon->setPixmap(skin::pixmap("PLAYBACK", 16));
    auto vol = new QSlider(Qt::Horizontal, this);
    vol->setRange(0, 100);
    vol->setValue(80);
    vol->setFixedWidth(80);
    ctl->addWidget(play);
    ctl->addWidget(pos, 1);
    ctl->addWidget(time);
    ctl->addWidget(volIcon);
    ctl->addWidget(vol);
    m_body->addLayout(ctl);
    if (loop) for (auto w : std::initializer_list<QWidget *>{play, pos, time, volIcon, vol}) w->hide();
    auto dur = std::make_shared<double>(0);
    connect(play, &QToolButton::clicked, m_player, &MpvWidget::togglePause);
    connect(m_player, &MpvWidget::paused, this, [this, play](bool on) {
        play->setIcon(style()->standardIcon(on ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    });
    connect(m_player, &MpvWidget::duration, this, [pos, dur](double d) {
        *dur = d;
        pos->setRange(0, int(d * 10));
    });
    connect(m_player, &MpvWidget::position, this, [pos, time, dur](double s) {
        if (!pos->isSliderDown()) pos->setValue(int(s * 10));
        time->setText(clock(s) + " / " + clock(*dur));
    });
    connect(pos, &QSlider::sliderReleased, this, [this, pos] { m_player->seek(pos->value() / 10.0); });
    connect(vol, &QSlider::valueChanged, m_player, &MpvWidget::setVolume);
    connect(m_player, &MpvWidget::failed, this, [this](const QString &why) {
        auto l = new QLabel(tr("Could not play the attachment: %1").arg(why.toHtmlEscaped()), this);
        l->setAlignment(Qt::AlignCenter);
        m_body->insertWidget(0, l);
        m_player->hide();
    });
    m_player->setVolume(80);
    m_player->load(m_url, loop);
}
