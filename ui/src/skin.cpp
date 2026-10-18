#include "skin.h"

#include <QApplication>
#include <QDateTime>
#include <QPalette>
#include <QDir>
#include <QFontDatabase>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>
#include <QIconEngine>
#include <QPixmapCache>
#include <QStyleOption>
#include <QPainter>
#include <QPolygonF>
#include <QProxyStyle>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSvgRenderer>

namespace skin {

static QString g_ts3, g_gfx, g_fallback, g_pack = "default_mono_2014";
static QHash<QString, QString> g_map; // logical name -> file in pack
static void extractEmbedded();

QString dataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ts3cord";
}

QString ts3Dir() {
    if (!g_ts3.isEmpty()) return g_ts3;
    // shipped ts3/ folder: next to the binary when installed or in the appimage, the repo for dev builds
    const auto app = QCoreApplication::applicationDirPath();
    QStringList c;
    if (qEnvironmentVariableIsSet("TS3_DIR")) c << qEnvironmentVariable("TS3_DIR");
    c << app + "/ts3" << app + "/../share/ts3cord/ts3" << app + "/../../ts3" << app + "/../ts3" << dataDir() + "/ts3";
    for (auto &d : c)
        if (QFile::exists(d + "/styles/default.qss")) return g_ts3 = QDir(d).canonicalPath();
    return {};
}

QString packName() { return g_pack; }

// icon packs ship unpacked: ts3/gfx/<pack>/
static QString gfxDir(const QString &pack) { return ts3Dir() + "/gfx/" + pack; }

// ts3 palette
static QPalette ts3Palette() {
    QPalette p;
    auto set = [&](QPalette::ColorRole r, const char *c) { p.setColor(QPalette::All, r, QColor(c)); };
    set(QPalette::WindowText, "#000000");
    set(QPalette::Button, "#dfdcd9");
    set(QPalette::Light, "#ffffff");
    set(QPalette::Midlight, "#efedec");
    set(QPalette::Dark, "#6f6e6c");
    set(QPalette::Mid, "#959391");
    set(QPalette::Text, "#000000");
    set(QPalette::BrightText, "#ffffff");
    set(QPalette::ButtonText, "#000000");
    set(QPalette::Base, "#ffffff");
    set(QPalette::Window, "#d6d2d0");
    set(QPalette::Shadow, "#000000");
    set(QPalette::Highlight, "#308cc6");
    set(QPalette::HighlightedText, "#ffffff");
    set(QPalette::Link, "#1c82cc");
    set(QPalette::LinkVisited, "#ff00ff");
    set(QPalette::AlternateBase, "#efedec");
    set(QPalette::ToolTipBase, "#ffffdc");
    set(QPalette::ToolTipText, "#000000");
    set(QPalette::PlaceholderText, "#000000");
    return p;
}

QString look() {
    static QString l = [] {
        auto v = qEnvironmentVariable("TS3CORD_LOOK", "win7");
        // win7 needs kvantum with a theme picked (aerothemeplasma's Windows7Aero); else the linux ts3 look
        auto kv = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/Kvantum/kvantum.kvconfig";
        if (v == "win7" && (!QStyleFactory::keys().contains("kvantum", Qt::CaseInsensitive) || !QFile::exists(kv))) return QString("linux");
        return v;
    }();
    return l;
}

QIcon appIcon() {
    QIcon ic;
    auto dir = ts3Dir() + "/icons";
    for (auto f : QDir(dir).entryList({"*.png"})) ic.addFile(dir + "/" + f);
    return ic.isNull() ? QIcon(":/res/ts3cord.png") : ic;
}

// base style plus scrolling menus (long bookmark lists); windows has no icons on dialog buttons
class Ts3Style : public QProxyStyle {
public:
    explicit Ts3Style(QStyle *base) : QProxyStyle(base) {}
    int styleHint(StyleHint h, const QStyleOption *o, const QWidget *w, QStyleHintReturn *r) const override {
        if (h == SH_Menu_Scrollable) return 1;
        if (h == SH_DialogButtonBox_ButtonsHaveIcons && look() == "win7") return 0;
        return QProxyStyle::styleHint(h, o, w, r);
    }
    // windows 7 tree branches: vista triangles, no dotted lines
    void drawPrimitive(PrimitiveElement pe, const QStyleOption *o, QPainter *p, const QWidget *w) const override {
        if (pe == PE_IndicatorBranch && look() == "win7") {
            if (!(o->state & State_Children)) return;
            bool open = o->state & State_Open;
            bool hover = o->state & State_MouseOver;
            auto c = o->rect.center();
            QPolygonF tri;
            if (open) tri << QPointF(c.x() + 3.5, c.y() - 3.5) << QPointF(c.x() + 3.5, c.y() + 2.5) << QPointF(c.x() - 2.5, c.y() + 2.5);
            else tri << QPointF(c.x() - 1.5, c.y() - 4.5) << QPointF(c.x() + 2.5, c.y() - 0.5) << QPointF(c.x() - 1.5, c.y() + 3.5);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            if (open) {
                p->setPen(QColor(hover ? "#1c1c1c" : "#262626"));
                p->setBrush(QColor(hover ? "#1c1c1c" : "#595959"));
            } else {
                p->setPen(QColor(hover ? "#1cc4f7" : "#a6a6a6"));
                p->setBrush(hover ? QColor("#d8f3fd") : Qt::white);
            }
            p->drawPolygon(tri);
            p->restore();
            return;
        }
        QProxyStyle::drawPrimitive(pe, o, p, w);
    }
};

bool apply() {
    if (ts3Dir().isEmpty()) return false;
    QDir::addSearchPath("styles", ts3Dir() + "/styles");
    if (qEnvironmentVariableIsSet("TS3CORD_ICONPACK")) g_pack = qEnvironmentVariable("TS3CORD_ICONPACK");
    g_gfx = gfxDir(g_pack);
    g_fallback = gfxDir("default");
    for (auto f : QDir(ts3Dir() + "/fonts").entryList({"*.ttf"})) QFontDatabase::addApplicationFont(ts3Dir() + "/fonts/" + f);
    QDir::addSearchPath("gfx", g_gfx);
    extractEmbedded();
    // [gfxfiles] NAME = file
    QFile ini(g_gfx + "/settings.ini");
    if (ini.open(QFile::ReadOnly)) {
        bool in = false;
        for (auto l : QString::fromUtf8(ini.readAll()).split('\n')) {
            l = l.trimmed();
            if (l.startsWith('[')) { in = l == "[gfxfiles]"; continue; }
            if (!in || l.startsWith('#') || !l.contains('=')) continue;
            g_map[l.section('=', 0, 0).trimmed()] = l.section('=', 1).trimmed();
        }
    }
    bool win7 = look() == "win7";
    auto base = QStyleFactory::create(win7 ? "kvantum" : "Fusion");
    QApplication::setStyle(new Ts3Style(base));
    // windows 7: palette from the kvantum theme; linux: ts3's own
    auto pal = win7 ? qApp->style()->standardPalette() : ts3Palette();
    if (qEnvironmentVariableIsSet("TS3CORD_PALETTE")) {
        // key=#rrggbb[,group] overrides, for matching runs
        for (auto kv : qEnvironmentVariable("TS3CORD_PALETTE").split(';', Qt::SkipEmptyParts)) {
            auto a = kv.split('=');
            if (a.size() == 2) pal.setColor(QPalette::ColorRole(a[0].toInt()), QColor(a[1]));
        }
    }
    QApplication::setPalette(pal);
    QApplication::setFont(win7 ? QFont("Segoe UI", 9) : QFont("Sans Serif", 9));
    QString qss;
    for (auto f : {"default.qss", win7 ? "default_vista.qss" : "default_linux.qss"}) {
        QFile q(ts3Dir() + "/styles/" + f);
        if (q.open(QFile::ReadOnly)) qss += QString::fromUtf8(q.readAll()) + "\n";
    }
    qss.chop(1);
    qApp->setStyleSheet(qss);
    return true;
}

QString styleFile(const QString &rel) {
    QFile f(ts3Dir() + "/styles/" + rel);
    return f.open(QFile::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// resources compiled into the ts3 binary (rcc: 4 byte size + zlib), pulled out once by content hash
static QHash<QString, QString> g_embedded;
static void extractEmbedded() {
    // pieces of the ts3 binary (server tab logo), shipped as ts3/embedded/<NAME>.svg
    auto dir = ts3Dir() + "/embedded";
    for (auto f : QDir(dir).entryList({"*.svg"})) g_embedded[QFileInfo(f).completeBaseName()] = dir + "/" + f;
}

// logical NAME via settings.ini, else plain file base name; default pack as fallback
static QString file(const QString &name) {
    if (g_embedded.contains(name)) return g_embedded[name];
    QString base = name;
    if (g_map.contains(name)) {
        auto p = g_gfx + "/" + g_map[name];
        if (QFile::exists(p)) return p;
        base = QFileInfo(g_map[name]).completeBaseName();
    } else if (name == name.toUpper()) base = name.toLower();
    for (auto ext : {".svg", ".png"}) {
        auto p = g_gfx + "/" + base + ext;
        if (QFile::exists(p)) return p;
    }
    auto p = g_fallback + "/16x16_" + base + ".png";
    if (QFile::exists(p)) return p;
    return {};
}

QString iconUrl(const QString &name) {
    auto f = file(name);
    return f.isEmpty() ? QString() : "gfx:" + QFileInfo(f).fileName();
}

QPixmap pixmap(const QString &name, int px) {
    return icon(name).pixmap(px, px);
}

// ts3 rasterizes svgs at 128px and smooth-scales down
class Ts3IconEngine : public QIconEngine {
public:
    explicit Ts3IconEngine(const QString &file) : m_file(file) {}
    QIconEngine *clone() const override { return new Ts3IconEngine(m_file); }
    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override { return size; }
    void paint(QPainter *p, const QRect &r, QIcon::Mode mode, QIcon::State state) override {
        p->drawPixmap(r, pixmap(r.size() * p->device()->devicePixelRatioF(), mode, state));
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        auto key = QString("ts3icon:%1:%2x%3:%4").arg(m_file).arg(size.width()).arg(size.height()).arg(int(mode));
        QPixmap pm;
        if (QPixmapCache::find(key, &pm)) return pm;
        if (mode == QIcon::Normal || mode == QIcon::Active) {
            int base = qMax(128, qMax(size.width(), size.height()));
            QImage big(base, base, QImage::Format_ARGB32_Premultiplied);
            big.fill(0);
            {
                QSvgRenderer r(m_file);
                QPainter p(&big);
                p.setRenderHint(QPainter::Antialiasing);
                r.render(&p);
            }
            pm = QPixmap::fromImage(big).scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        } else {
            QStyleOption opt;
            opt.palette = QApplication::palette();
            pm = QApplication::style()->generatedIconPixmap(mode, pixmap(size, QIcon::Normal, state), &opt);
        }
        QPixmapCache::insert(key, pm);
        return pm;
    }
private:
    QString m_file;
};

QIcon icon(const QString &name) {
    static QHash<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return *it;
    auto f = file(name);
    QIcon ic;
    if (f.endsWith(".svg")) ic = QIcon(new Ts3IconEngine(f));
    else if (!f.isEmpty()) ic = QIcon(f);
    cache.insert(name, ic);
    return ic;
}

}
