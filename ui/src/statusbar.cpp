#include "statusbar.h"

#include "skin.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QTextDocument>
#include <QTimer>

// scrolling rich text ticker
class NewsBanner : public QWidget {
public:
    NewsBanner(NewsWidget *w, QWidget *parent) : QWidget(parent), m_w(w) {
        setObjectName("NewsBanner");
        setFixedHeight(18);
        auto t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this] {
            if (m_doc.idealWidth() <= 0) return;
            m_w->m_offset = (m_w->m_offset + 1) % int(m_doc.idealWidth() + width());
            update();
        });
        t->start(30);
    }
    void setText(const QString &html) {
        m_doc.setDefaultFont(font());
        m_doc.setDocumentMargin(0);
        m_doc.setHtml("<nobr>" + html + "</nobr>");
        m_w->m_offset = 0;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setClipRect(rect());
        p.translate(width() - m_w->m_offset, (height() - m_doc.size().height()) / 2);
        m_doc.drawContents(&p);
    }
private:
    NewsWidget *m_w;
    QTextDocument m_doc;
};

NewsWidget::NewsWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("NewsWidget");
    auto l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 8, 0);
    l->setSpacing(0);
    m_left = new QLabel("<span style=\"font-style:italic\">News:</span>", this);
    m_left->setObjectName("NewsLeftLabel");
    // linux ts3 sizes this label to 43px; with segoe ui it sizes to its text
    if (skin::look() != "win7") m_left->setFixedWidth(43);
    m_left->setIndent(0);
    auto banner = new NewsBanner(this, this);
    m_banner = banner;
    m_close = new QToolButton(this);
    m_close->setObjectName("NewsCloseButton");
    m_close->setIcon(skin::icon("CLOSE_BUTTON"));
    m_close->setIconSize(QSize(16, 16));
    m_close->setAutoRaise(true);
    m_close->setFixedSize(16, 16);
    l->addWidget(m_left);
    l->addWidget(m_banner, 1);
    l->addSpacing(8);
    l->addWidget(m_close);
    connect(m_close, &QToolButton::clicked, this, [this] { setNews({}); });
    m_close->hide();
}

void NewsWidget::setNews(const QString &html) {
    m_text = html;
    static_cast<NewsBanner *>(m_banner)->setText(html);
    m_close->setVisible(!html.isEmpty());
}

PingWidget::PingWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("ServerStatusPingWidget");
    setFixedSize(24, 24);
}

void PingWidget::setPing(int ms, bool connected) {
    m_icon = !connected ? "PING_DISCONNECTED" : ms <= 0 ? "PING_4" : ms < 60 ? "PING_4" : ms < 120 ? "PING_3" : ms < 250 ? "PING_2" : "PING_1";
    setToolTip(connected && ms > 0 ? QString("%1 ms").arg(ms) : QString());
    update();
}

void PingWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    skin::icon(m_icon).paint(&p, rect());
}

ServerStatusWidget::ServerStatusWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("ServerStatusWidget");
    auto l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    m_label = new QLabel(tr("Disconnected"), this);
    m_label->setObjectName("ServerStatusLabel");
    m_ping = new PingWidget(this);
    l->addWidget(m_label);
    l->addWidget(m_ping);
}

void ServerStatusWidget::setStatus(const QString &text, int ping, bool connected) {
    m_label->setText(text);
    m_ping->setPing(ping, connected);
}

StatusErrorDisplay::StatusErrorDisplay(QWidget *parent) : QLabel(parent) {
    setPixmap(skin::pixmap("WARNING", 24));
    setFixedSize(30, 24);
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    hide();
}

void StatusErrorDisplay::addError(const QString &msg) {
    setToolTip(msg);
    show();
}

com::teamspeak::ui::SyncStatusWidget::SyncStatusWidget(QWidget *parent) : QWidget(parent) {
    auto l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    auto lab = new QLabel(this);
    lab->setPixmap(skin::pixmap("SYNC_DISABLE", 24));
    lab->setFixedSize(30, 24);
    lab->setAlignment(Qt::AlignCenter);
    lab->setToolTip(tr("Synchronization disabled"));
    l->addWidget(lab);
}
