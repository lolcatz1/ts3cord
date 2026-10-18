#pragma once
// bottom chat: ts3 chat log tabs (server / channel / private) and the input line
#include <QDateTime>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>

struct Message;
class Store;

// discord markdown -> ts3 chat html
QString markdownToHtml(const QString &md);

class ChatTab : public QTextBrowser {
    Q_OBJECT
public:
    enum Kind { ServerTab, ChannelTab, PrivateTab };
    ChatTab(Kind k, const QString &channelId, QWidget *parent = nullptr);
    Kind kind() const { return m_kind; }
    QString channelId() const { return m_channel; }
    void setChannelId(const QString &c) { m_channel = c; }
    // ts3 info line: icon MESSAGE_INFO, <time>, text in class cls
    void info(const QString &html, const QString &cls = "TextMessage_Normal", const QDateTime &when = {});
    void error(const QString &html);
    void message(const Message &m, bool own);
    void history(const QVector<Message> &list, const QString &myId);
    void clearLog();
signals:
    void scrolledToTop();
protected:
    QVariant loadResource(int type, const QUrl &url) override;
private:
    void append_(const QString &p);
    void render();
    Kind m_kind;
    QString m_channel, m_myId;
    // log = messages (from the store) and info lines, merged by time
    struct Entry { QDateTime when; QString html; };
    // follow the bottom unless the user scrolled up; ignore our own scroll changes
    bool m_stick = true, m_rendering = false;
    QVector<Entry> m_infos, m_msgs;
};

class ImprovedTabBar : public QTabBar {
    Q_OBJECT
public:
    using QTabBar::QTabBar;
};

class ChatTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit ChatTabWidget(QWidget *parent = nullptr);
    ChatTab *addChat(ChatTab::Kind k, const QString &channelId, const QString &title);
    ChatTab *find(const QString &channelId) const;
    ChatTab *serverTab() const;
    ChatTab *channelTab() const;
    ChatTab *current() const { return qobject_cast<ChatTab *>(currentWidget()); }
    void markUnread(ChatTab *t, bool on);
signals:
    void privateClosed(const QString &channelId);
};

class ChatLineEdit : public QTextEdit {
    Q_OBJECT
    Q_PROPERTY(bool helpTextVisible READ helpTextVisible)
public:
    explicit ChatLineEdit(QWidget *parent = nullptr);
    bool helpTextVisible() const { return m_help; }
    QString text() const { return m_help ? QString() : toPlainText(); }
    void clearText();
    // files queued by paste / drop, sent with the next message
    QStringList attachments() const { return m_files; }
    void clearAttachments() { m_files.clear(); emit attachmentsChanged(m_files); }
    QSize sizeHint() const override { return QSize(100, 30); }
signals:
    void submitted(const QString &text);
    void typed();
    void attachmentsChanged(const QStringList &files);
protected:
    void keyPressEvent(QKeyEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *m) const override;
    void insertFromMimeData(const QMimeData *m) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
private:
    void setHelp(bool on);
    bool m_help = false;
    QStringList m_history, m_files;
    int m_histPos = -1;
};
