#include "avatars.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QStandardPaths>

Avatars *Avatars::get() {
    static auto a = new Avatars;
    return a;
}

Avatars::Avatars() {
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/img";
    QDir().mkpath(m_dir);
}

QPixmap Avatars::image(const QString &url) {
    if (url.isEmpty()) return {};
    auto it = m_cache.find(url);
    if (it != m_cache.end()) return *it;
    auto file = m_dir + "/" + QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    QPixmap p;
    if (QFile::exists(file) && p.load(file)) {
        m_cache.insert(url, p);
        return p;
    }
    if (url.startsWith("file:") || url.startsWith('/')) {
        p.load(QUrl(url).isLocalFile() ? QUrl(url).toLocalFile() : url);
        m_cache.insert(url, p);
        return p;
    }
    if (m_pending.contains(url)) return {};
    m_pending << url;
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, url, file] {
        r->deleteLater();
        m_pending.remove(url);
        auto data = r->readAll();
        QPixmap p;
        if (r->error() == QNetworkReply::NoError && p.loadFromData(data)) {
            QFile f(file);
            if (f.open(QFile::WriteOnly)) f.write(data);
        }
        m_cache.insert(url, p);
        emit ready(url);
    });
    return {};
}
