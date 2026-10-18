#pragma once
// one "server connection" = one open guild: tree + info frame, plus its own chat tabs
#include <QSplitter>
#include <QTabWidget>

#include "tree.h"

class Store;
class CoreLink;
class InfoFrame;
class ChatTabWidget;

class ServerView : public QSplitter {
    Q_OBJECT
public:
    ServerView(Store *s, CoreLink *core, const QString &guildId, QWidget *parent = nullptr);
    QString guildId() const { return m_guild; }
    TreeView *tree() const { return m_tree; }
    TreeModel *model() const { return m_model; }
    InfoFrame *info() const { return m_info; }
    ChatTabWidget *chat() const { return m_chat; }
    // the open text channel (where the own client sits when not in voice)
    QString textChannel() const { return m_text; }
    void openText(const QString &channelId);
    void rebuild();
private:
    Store *m_store;
    CoreLink *m_core;
    QString m_guild, m_text;
    TreeView *m_tree;
    TreeModel *m_model;
    InfoFrame *m_info;
    ChatTabWidget *m_chat;
};

class ServerViewManager : public QTabWidget {
    Q_OBJECT
public:
    explicit ServerViewManager(QWidget *parent = nullptr);
    ServerView *view(const QString &guildId) const;
    ServerView *current() const { return qobject_cast<ServerView *>(currentWidget()); }
    void updateTabBar();
protected:
    void tabInserted(int) override { updateTabBar(); }
    void tabRemoved(int) override { updateTabBar(); }
};
