#pragma once
// ts3cord-core subprocess, json lines both ways (docs/PROTOCOL.md)
#include <QJsonObject>
#include <QObject>
#include <QProcess>

class CoreLink : public QObject {
    Q_OBJECT
public:
    explicit CoreLink(QObject *parent = nullptr);
    // starts the core binary, or replays a recorded .jsonl when replay is set
    void start(const QString &replay = {}, bool demo = false);
    void send(const QJsonObject &cmd);
    void send(const QString &cmd, QJsonObject fields = {}) { fields["cmd"] = cmd; send(fields); }
signals:
    void event(const QString &ev, const QJsonObject &o);
    void died(const QString &why);
private:
    void line(const QByteArray &l);
    QProcess m_proc;
    QByteArray m_buf;
    bool m_replay = false;
};
