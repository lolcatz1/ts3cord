#pragma once
// ts3 look: locates a teamspeak 3 client install and loads its styles, icon pack and palette
#include <QIcon>
#include <QString>

namespace skin {

// "win7" (kvantum windows 7 theme, segoe ui, vista qss) or "linux" (ts3 linux build look)
QString look();
// app icon from the ts3 windows exe (~/.local/share/ts3cord/icons)
QIcon appIcon();
// ~/.local/share/ts3cord
QString dataDir();
// ts3 client dir (has styles/, gfx/, sound/), empty when not found
QString ts3Dir();
// sets style, palette, font, qss, search paths. call after QApplication exists
bool apply();
// icon from the active pack by base name, e.g. "away", "channel_green_subscribed"
QIcon icon(const QString &name);
QPixmap pixmap(const QString &name, int px = 16);
// path of a pack file for <img src> use, as "gfx:name"
QString iconUrl(const QString &name);
QString packName();
// styles/default/<file> contents
QString styleFile(const QString &rel);

}
