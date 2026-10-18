#pragma once
// status bar pieces with ts3's class/object names
#include <QLabel>
#include <QToolButton>
#include <QWidget>

class NewsWidget : public QWidget {
    Q_OBJECT
public:
    explicit NewsWidget(QWidget *parent = nullptr);
    void setNews(const QString &html);
private:
    QLabel *m_left;
    QWidget *m_banner;
    QToolButton *m_close;
    QString m_text;
    int m_offset = 0;
    friend class NewsBanner;
};

class PingWidget : public QWidget {
    Q_OBJECT
public:
    explicit PingWidget(QWidget *parent = nullptr);
    void setPing(int ms, bool connected);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QString m_icon = "PING_DISCONNECTED";
};

class ServerStatusWidget : public QWidget {
    Q_OBJECT
public:
    explicit ServerStatusWidget(QWidget *parent = nullptr);
    void setStatus(const QString &text, int ping, bool connected);
private:
    QLabel *m_label;
    PingWidget *m_ping;
};

class StatusErrorDisplay : public QLabel {
    Q_OBJECT
public:
    explicit StatusErrorDisplay(QWidget *parent = nullptr);
    void addError(const QString &msg);
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent *) override { emit clicked(); }
};

namespace com::teamspeak::ui {
class SyncStatusWidget : public QWidget {
    Q_OBJECT
public:
    explicit SyncStatusWidget(QWidget *parent = nullptr);
};
}
