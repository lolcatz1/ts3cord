#pragma once
// ts3 server tree: guild = server, categories = [cspacer], channels, voice members = clients
#include <QAbstractItemModel>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <memory>

class Store;

struct TreeNode {
    enum Kind { Server, Channel, Spacer, Client } kind = Server;
    QString id;      // guild / channel / user id
    QString text;
    QString spacer;  // "c", "r", "", "*" for spacer rows
    TreeNode *parent = nullptr;
    std::vector<std::unique_ptr<TreeNode>> kids;
    int row() const;
};

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles { KindRole = 256, IdRole = 257, RawNameRole = 258, RightIconsRole = 300, TalkingRole, SelfRole };
    TreeModel(Store *s, const QString &guildId, QObject *parent = nullptr);
    void rebuild();
    // the channel the own client is drawn in (voice channel, else the open text channel)
    void setSelfChannel(const QString &channelId);
    QString selfChannel() const { return m_self; }
    QModelIndex indexOf(TreeNode::Kind k, const QString &id) const;
    TreeNode *node(const QModelIndex &i) const;
    void refreshClient(const QString &userId);

    QModelIndex index(int row, int col, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &i) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex & = {}) const override { return 1; }
    QVariant data(const QModelIndex &i, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &i) const override;
private:
    QString clientIcon(const QString &userId) const;
    Store *m_store;
    QString m_guild, m_self;
    std::unique_ptr<TreeNode> m_root;
};

class TreeDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override;
    QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &i) const override;
};

class TreeView : public QTreeView {
    Q_OBJECT
public:
    TreeView(QWidget *parent = nullptr);
    void setTreeModel(TreeModel *m);
    TreeModel *treeModel() const { return m_model; }
signals:
    void activatedNode(TreeNode::Kind k, const QString &id);
    void selectedNode(TreeNode::Kind k, const QString &id);
    void contextMenuNode(TreeNode::Kind k, const QString &id, const QPoint &global);
protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    void drawRow(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override;
private:
    TreeModel *m_model = nullptr;
};
