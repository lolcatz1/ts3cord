#pragma once
// right pane: ts3 .tpl templates filled with discord data
#include <QMap>
#include <QTextBrowser>

class Store;

// %%VAR%%, %%?VAR%% (drop line when empty), %%??VAR%% (line prefix condition), list vars repeat their line
QString fillTemplate(const QString &tpl, const QMap<QString, QString> &vars, const QMap<QString, QStringList> &lists = {});

class InfoFrame : public QTextBrowser {
    Q_OBJECT
public:
    InfoFrame(Store *s, QWidget *parent = nullptr);
    void showServer(const QString &guildId);
    void showChannel(const QString &guildId, const QString &channelId);
    void showClient(const QString &guildId, const QString &userId);
    void refresh();
protected:
    QVariant loadResource(int type, const QUrl &url) override;
private:
    Store *m_store;
    enum { None, Server, Channel, Client } m_kind = None;
    QString m_guild, m_id;
};

// "iconpath:NAME?size=WxH" and "avatar:<url>?size=" for QTextDocument
QVariant ts3Resource(const QUrl &url);
