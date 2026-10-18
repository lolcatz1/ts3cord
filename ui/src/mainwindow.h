#pragma once
#include <QMainWindow>
#include <QPointer>

class Store;
class CoreLink;
class ServerViewManager;
class ServerView;
class ChatTab;
class ChatLineEdit;
class NewsWidget;
class ServerStatusWidget;
class StatusErrorDisplay;
class QToolButton;
class QAction;
class QMenu;
class QVBoxLayout;
class QWidget;
struct Message;
class ChatTabWidget;
class ContactsDialog;

// class name matters: ts3 qss uses "MainWindow"
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Store *s, CoreLink *core, QWidget *parent = nullptr);
    void openGuild(const QString &guildId, bool newTab = true);
protected:
    void closeEvent(QCloseEvent *e) override;
private:
    void buildMenus();
    void buildToolbar();
    void buildCentral();
    void buildStatus();
    void wire();
    ServerView *current() const;
    void showChat(ServerView *v);
    void switchText(ServerView *v, QString channelId);
    void openPrivate(const QString &dmChannelId, bool focus);
    ChatTab *privateTab(const QString &dmChannelId, bool create);
    void submit(const QString &text);
    void onMessage(const Message &m);
    void onVoice(const QString &guildId, const QString &userId, const QString &oldCh, const QString &newCh);
    void updateStatus();
    void updateSelfActions();
    void updateBookmarks();
    void contextMenu(int kind, const QString &id, const QPoint &pos);
    void notify(const QString &html);
    void playSound(const QString &event, const QString &userId = {});
    void onLink(ServerView *v, const QUrl &u);

    Store *m_store;
    CoreLink *m_core;
    ServerViewManager *m_views;
    QWidget *m_chatWidget;
    QVBoxLayout *m_chatLayout;
    ChatTabWidget *m_chatShown = nullptr;
    ChatTabWidget *m_noServerChat;
    ChatLineEdit *m_line;
    NewsWidget *m_news;
    ServerStatusWidget *m_status;
    StatusErrorDisplay *m_errors;
    QMenu *m_bookmarks;
    QAction *m_actAway, *m_actOnline, *m_actMic, *m_actMuteMic, *m_actMuteSpk, *m_actSubscribe, *m_actContacts;
    QAction *m_selfMuteMic, *m_selfMuteSpk;
    QToolButton *m_awayBtn, *m_spkBtn;
    QPointer<QDialog> m_login;
    QPointer<ContactsDialog> m_contacts;
    bool m_away = false, m_connected = false;
    QString m_pendingGuild, m_lastVoice, m_wantVoice;
    qint64 m_lastTyping = 0;
};
