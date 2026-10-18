#include "serverview.h"

#include "chat.h"
#include "infoframe.h"
#include "store.h"

#include <QScrollBar>
#include <QTabBar>
#include <QVBoxLayout>

ServerView::ServerView(Store *s, CoreLink *core, const QString &guildId, QWidget *parent)
    : QSplitter(Qt::Horizontal, parent), m_store(s), m_core(core), m_guild(guildId) {
    setChildrenCollapsible(false);
    setHandleWidth(4);
    auto left = new QWidget(this);
    auto ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(1);
    m_tree = new TreeView(left);
    m_model = new TreeModel(s, guildId, this);
    m_tree->setTreeModel(m_model);
    ll->addWidget(m_tree);
    auto right = new QWidget(this);
    auto rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(1);
    m_info = new InfoFrame(s, right);
    rl->addWidget(m_info);
    addWidget(left);
    addWidget(right);
    setStretchFactor(0, 1);
    setStretchFactor(1, 1);
    m_chat = new ChatTabWidget;
    m_chat->addChat(ChatTab::ServerTab, {}, s->guilds[guildId].name);
    m_chat->addChat(ChatTab::ChannelTab, {}, QString());
    m_chat->setTabEnabled(1, false);
}

void ServerView::openText(const QString &channelId) {
    m_text = channelId;
    m_model->setSelfChannel(channelId);
    auto tab = m_chat->channelTab();
    tab->setChannelId(channelId);
    tab->clearLog();
    auto c = m_store->channel(channelId);
    int i = m_chat->indexOf(tab);
    m_chat->setTabText(i, c ? c->name : QString());
    m_chat->setTabEnabled(i, true);
}

void ServerView::rebuild() {
    // keep the selection across model resets
    auto cur = m_tree->currentIndex();
    TreeNode::Kind k = TreeNode::Server;
    QString id;
    if (cur.isValid()) {
        k = m_model->node(cur)->kind;
        id = m_model->node(cur)->id;
    }
    auto scroll = m_tree->verticalScrollBar()->value();
    m_model->rebuild();
    if (!id.isEmpty()) {
        auto i = m_model->indexOf(k, id);
        if (i.isValid()) m_tree->setCurrentIndex(i);
    }
    m_tree->verticalScrollBar()->setValue(scroll);
}

ServerViewManager::ServerViewManager(QWidget *parent) : QTabWidget(parent) {
    setDocumentMode(false);
    setMovable(true);
    setUsesScrollButtons(true);
}

ServerView *ServerViewManager::view(const QString &guildId) const {
    for (int i = 0; i < count(); ++i)
        if (auto v = qobject_cast<ServerView *>(widget(i)); v && v->guildId() == guildId) return v;
    return nullptr;
}

void ServerViewManager::updateTabBar() {
    // ts3 hides the server tabs with a single connection
    tabBar()->setVisible(count() > 1);
}
