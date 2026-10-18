#include "tree.h"

#include "avatars.h"
#include "skin.h"
#include "store.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QPainter>
#include <algorithm>

int TreeNode::row() const {
    if (!parent) return 0;
    for (size_t i = 0; i < parent->kids.size(); ++i)
        if (parent->kids[i].get() == this) return int(i);
    return 0;
}

TreeModel::TreeModel(Store *s, const QString &guildId, QObject *parent) : QAbstractItemModel(parent), m_store(s), m_guild(guildId) {
    rebuild();
}

static TreeNode *add(TreeNode *p, TreeNode::Kind k, const QString &id, const QString &text, const QString &spacer = {}) {
    auto n = std::make_unique<TreeNode>();
    n->kind = k;
    n->id = id;
    n->text = text;
    n->spacer = spacer;
    n->parent = p;
    p->kids.push_back(std::move(n));
    return p->kids.back().get();
}

void TreeModel::rebuild() {
    beginResetModel();
    m_root = std::make_unique<TreeNode>();
    auto &g = m_store->guilds[m_guild];
    auto server = add(m_root.get(), TreeNode::Server, g.id, g.name);

    // discord order: categories by position; inside, text-like before voice, then position
    QVector<const Channel *> cats, loose;
    QHash<QString, QVector<const Channel *>> byCat;
    for (auto &c : g.channels) {
        if (!c.canView) continue;
        if (c.isCategory()) cats << &c;
        else if (c.parentId.isEmpty() || !g.channels.contains(c.parentId)) loose << &c;
        else byCat[c.parentId] << &c;
    }
    auto order = [](const Channel *a, const Channel *b) {
        if (a->isVoice() != b->isVoice()) return !a->isVoice();
        if (a->position != b->position) return a->position < b->position;
        return a->id.toULongLong() < b->id.toULongLong();
    };
    std::sort(cats.begin(), cats.end(), [](auto a, auto b) { return a->position != b->position ? a->position < b->position : a->id.toULongLong() < b->id.toULongLong(); });
    std::sort(loose.begin(), loose.end(), order);

    // voice members per channel
    QHash<QString, QVector<const VoiceState *>> members;
    for (auto &v : g.voice) members[v.channelId] << &v;

    auto addChannel = [&](const Channel *c) {
        auto n = add(server, TreeNode::Channel, c->id, c->name);
        QVector<const VoiceState *> in = members.value(c->id);
        std::sort(in.begin(), in.end(), [](auto a, auto b) { return a->user.name().compare(b->user.name(), Qt::CaseInsensitive) < 0; });
        for (auto v : in) add(n, TreeNode::Client, v->user.id, v->user.name());
        bool selfInVoice = g.voice.contains(m_store->me.id);
        if (!selfInVoice && c->id == m_self) add(n, TreeNode::Client, m_store->me.id, m_store->myName(m_guild));
    };
    for (auto c : loose) addChannel(c);
    for (auto cat : cats) {
        auto kids = byCat.value(cat->id);
        if (kids.isEmpty()) continue;
        add(server, TreeNode::Spacer, cat->id, cat->name.toUpper(), "c");
        std::sort(kids.begin(), kids.end(), order);
        for (auto c : kids) addChannel(c);
    }
    endResetModel();
}

void TreeModel::setSelfChannel(const QString &channelId) {
    if (m_self == channelId) return;
    m_self = channelId;
    rebuild();
}

TreeNode *TreeModel::node(const QModelIndex &i) const {
    return i.isValid() ? static_cast<TreeNode *>(i.internalPointer()) : m_root.get();
}

QModelIndex TreeModel::index(int row, int col, const QModelIndex &parent) const {
    auto p = node(parent);
    if (!p || row < 0 || row >= int(p->kids.size()) || col != 0) return {};
    return createIndex(row, col, p->kids[row].get());
}

QModelIndex TreeModel::parent(const QModelIndex &i) const {
    if (!i.isValid()) return {};
    auto n = node(i);
    if (!n->parent || n->parent == m_root.get()) return {};
    return createIndex(n->parent->row(), 0, n->parent);
}

int TreeModel::rowCount(const QModelIndex &parent) const {
    auto p = node(parent);
    return p ? int(p->kids.size()) : 0;
}

QModelIndex TreeModel::indexOf(TreeNode::Kind k, const QString &id) const {
    std::function<QModelIndex(const QModelIndex &)> find = [&](const QModelIndex &p) -> QModelIndex {
        for (int r = 0; r < rowCount(p); ++r) {
            auto i = index(r, 0, p);
            auto n = node(i);
            if (n->kind == k && n->id == id) return i;
            auto c = find(i);
            if (c.isValid()) return c;
        }
        return {};
    };
    return find({});
}

void TreeModel::refreshClient(const QString &userId) {
    auto i = indexOf(TreeNode::Client, userId);
    if (i.isValid()) emit dataChanged(i, i);
}

QString TreeModel::clientIcon(const QString &uid) const {
    auto &g = m_store->guilds[m_guild];
    auto it = g.voice.find(uid);
    if (m_store->presence.value(uid) == "idle") return "AWAY";
    if (it != g.voice.end()) {
        if (it->deaf) return "HARDWARE_OUTPUT_MUTED";
        if (it->selfDeaf) return "OUTPUT_MUTED";
        if (it->mute) return "HARDWARE_INPUT_MUTED";
        if (it->selfMute) return "INPUT_MUTED";
        if (m_store->speaking.contains(uid)) return "PLAYER_ON";
    }
    return "PLAYER_OFF";
}

QVariant TreeModel::data(const QModelIndex &i, int role) const {
    auto n = node(i);
    if (!n) return {};
    auto &g = m_store->guilds[m_guild];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case RawNameRole:
        return n->kind == TreeNode::Spacer ? QString("[%1spacer]%2").arg(n->spacer, n->text) : n->text;
    case KindRole: return n->kind == TreeNode::Server ? 0 : n->kind == TreeNode::Client ? 2 : 1;
    case IdRole: return n->id;
    case Qt::DecorationRole:
        switch (n->kind) {
        case TreeNode::Server: return skin::icon("SERVER_GREEN");
        case TreeNode::Spacer: return {};
        case TreeNode::Client: return skin::icon(clientIcon(n->id));
        case TreeNode::Channel: {
            auto &c = g.channels[n->id];
            int in = int(n->kids.size());
            if (c.isVoice() && c.userLimit > 0 && in >= c.userLimit) return skin::icon("CHANNEL_RED_SUBSCRIBED");
            if ((c.isVoice() && !c.canConnect) || (!c.isVoice() && !c.canSend)) return skin::icon("CHANNEL_YELLOW_SUBSCRIBED");
            return skin::icon("CHANNEL_GREEN_SUBSCRIBED");
        }
        }
        return {};
    case Qt::FontRole:
        if (n->kind == TreeNode::Client && n->id == m_store->me.id) {
            QFont f;
            f.setBold(true);
            return f;
        }
        if (n->kind == TreeNode::Channel && g.channels[n->id].unread && !g.channels[n->id].isVoice()) {
            QFont f;
            f.setBold(true);
            return f;
        }
        return {};
    case RightIconsRole: {
        QStringList r;
        // ts3 shows a server's custom icon at the right of the root row
        if (n->kind == TreeNode::Server && !g.iconUrl.isEmpty()) r << "url:" + g.iconUrl;
        if (n->kind == TreeNode::Channel) {
            auto &c = g.channels[n->id];
            if (c.mentions > 0) r << "POKE";
            if (c.nsfw) r << "MODERATED";
            if ((c.isVoice() && !c.canConnect) || (!c.isVoice() && !c.canSend)) r << "REGISTER";
        } else if (n->kind == TreeNode::Client) {
            auto it = g.voice.find(n->id);
            if (it != g.voice.end() && it->streaming) r << "MUSIC";
            bool bot = it != g.voice.end() && it->user.bot;
            if (bot) r << "GROUP_500";
            if (n->id == g.ownerId) r << "GROUP_300";
        }
        return r;
    }
    case SelfRole: return n->kind == TreeNode::Client && n->id == m_store->me.id;
    }
    return {};
}

Qt::ItemFlags TreeModel::flags(const QModelIndex &i) const {
    return i.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

// delegate: rows 20px, spacers 18px
QSize TreeDelegate::sizeHint(const QStyleOptionViewItem &o, const QModelIndex &i) const {
    bool spacer = i.data(Qt::DisplayRole).toString().startsWith('[') && i.data(TreeModel::KindRole).toInt() == 1 &&
                  i.data(Qt::DisplayRole).toString().contains("spacer]");
    return QSize(o.rect.width(), spacer ? 18 : 20);
}

void TreeDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &i) const {
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, i);
    auto text = i.data(Qt::DisplayRole).toString();
    bool selected = o.state & QStyle::State_Selected;
    p->save();
    // background (selection / hover) the way the style draws item panels
    o.text.clear();
    o.icon = QIcon();
    if (auto w = o.widget) {
        auto bg = o;
        if (skin::look() == "win7") bg.state &= ~QStyle::State_Selected;
        w->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &bg, p, w);
    }
    auto pen = selected ? o.palette.color(QPalette::HighlightedText) : o.palette.color(QPalette::Text);
    p->setPen(pen);
    p->setFont(o.font);
    QRect r = o.rect;
    // spacers
    if (text.startsWith('[') && text.contains("spacer]")) {
        auto kind = text.mid(1, text.indexOf("spacer]") - 1);
        auto body = text.mid(text.indexOf("spacer]") + 7);
        int align = kind.startsWith('c') ? Qt::AlignHCenter : kind.startsWith('r') ? Qt::AlignRight : Qt::AlignLeft;
        if (kind.startsWith('*')) {
            QString fill;
            while (p->fontMetrics().horizontalAdvance(fill + body) < r.width() - 4) fill += body;
            body = fill;
        }
        p->drawText(r.adjusted(2, 0, -2, 0), align | Qt::AlignVCenter, body);
        p->restore();
        return;
    }
    auto icon = i.data(Qt::DecorationRole).value<QIcon>();
    int x = r.x() + 4;
    auto mode = selected ? QIcon::Selected : QIcon::Normal;
    if (!icon.isNull()) icon.paint(p, QRect(x, r.y() + (r.height() - 16) / 2, 16, 16), Qt::AlignCenter, mode);
    x += 16 + 5;
    // right side icons
    int right = r.right() - 1;
    auto rights = i.data(TreeModel::RightIconsRole).toStringList();
    for (int k = rights.size() - 1; k >= 0; --k) {
        right -= 16;
        QRect ir(right + 1, r.y() + (r.height() - 16) / 2, 16, 16);
        if (rights[k].startsWith("url:")) {
            auto pm = Avatars::get()->image(rights[k].mid(4));
            if (!pm.isNull()) p->drawPixmap(ir, pm.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else skin::icon(rights[k]).paint(p, ir, Qt::AlignCenter, mode);
        right -= 2;
    }
    QRect tr(x, r.y(), right - x, r.height());
    p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, p->fontMetrics().elidedText(text, Qt::ElideRight, tr.width()));
    p->restore();
}

TreeView::TreeView(QWidget *parent) : QTreeView(parent) {
    setObjectName("ServerTreeView");
    setHeaderHidden(true);
    setIndentation(20);
    setRootIsDecorated(true);
    setUniformRowHeights(false);
    setItemDelegate(new TreeDelegate(this));
    setExpandsOnDoubleClick(false);
    setAnimated(false);
    setEditTriggers(NoEditTriggers);
    setSelectionMode(SingleSelection);
    setMouseTracking(true);
    connect(Avatars::get(), &Avatars::ready, viewport(), [this] { viewport()->update(); });
    connect(this, &QTreeView::doubleClicked, this, [this](const QModelIndex &i) {
        // copies: handlers may rebuild the model and free the node
        if (auto n = m_model ? m_model->node(i) : nullptr) {
            auto kind = n->kind;
            auto id = n->id;
            emit activatedNode(kind, id);
        }
    });
}

void TreeView::setTreeModel(TreeModel *m) {
    m_model = m;
    setModel(m);
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &cur) {
        if (auto n = cur.isValid() ? m_model->node(cur) : nullptr) {
            auto kind = n->kind;
            auto id = n->id;
            emit selectedNode(kind, id);
        }
    });
    connect(m, &QAbstractItemModel::modelReset, this, [this] { expandAll(); });
    expandAll();
}

// ts3 highlights the whole row, branch area included
void TreeView::drawRow(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const {
    if (selectionModel() && selectionModel()->isSelected(i)) {
        QStyleOptionViewItem opt = o;
        opt.rect = QRect(0, o.rect.y(), viewport()->width(), o.rect.height());
        opt.state |= QStyle::State_Selected;
        opt.showDecorationSelected = true;
        opt.viewItemPosition = QStyleOptionViewItem::OnlyOne;
        if (skin::look() == "win7") style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, p, this);
        else p->fillRect(opt.rect, opt.palette.brush(hasFocus() ? QPalette::Active : QPalette::Inactive, QPalette::Highlight));
    }
    QTreeView::drawRow(p, o, i);
}

void TreeView::contextMenuEvent(QContextMenuEvent *e) {
    auto i = indexAt(e->pos());
    if (!i.isValid() || !m_model) return;
    setCurrentIndex(i);
    auto n = m_model->node(i);
    auto kind = n->kind;
    auto id = n->id;
    emit contextMenuNode(kind, id, e->globalPos());
}
