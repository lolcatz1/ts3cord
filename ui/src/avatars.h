#pragma once
// cdn image cache (avatars, guild icons, attachments)
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>

class Avatars : public QObject {
    Q_OBJECT
public:
    static Avatars *get();
    // cached image or null pixmap; fetch starts on first miss, ready() fires when it lands
    QPixmap image(const QString &url);
signals:
    void ready(const QString &url);
private:
    Avatars();
    QNetworkAccessManager m_net;
    QHash<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
    QString m_dir;
};
