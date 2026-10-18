#include "corelink.h"

void ts3cordLog(const QByteArray &line);

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>

CoreLink::CoreLink(QObject *parent) : QObject(parent) {
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        m_buf += m_proc.readAllStandardOutput();
        int i;
        while ((i = m_buf.indexOf('\n')) >= 0) {
            line(m_buf.left(i));
            m_buf.remove(0, i + 1);
        }
    });
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        auto err = m_proc.readAllStandardError();
        fprintf(stderr, "%s", err.constData());
        ts3cordLog(err);
    });
    connect(&m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        emit died(QString("core exited (%1)").arg(code));
    });
}

static QString coreBinary() {
    if (qEnvironmentVariableIsSet("TS3CORD_CORE")) return qEnvironmentVariable("TS3CORD_CORE");
    auto dir = QCoreApplication::applicationDirPath();
    for (auto p : {dir + "/ts3cord-core", dir + "/../core/ts3cord-core", dir + "/../../core/ts3cord-core"})
        if (QFile::exists(p)) return p;
    return "ts3cord-core";
}

void CoreLink::start(const QString &replay, bool demo) {
    if (!replay.isEmpty()) {
        // replay: events from a file, all at once after the event loop starts; commands are dropped
        m_replay = true;
        QTimer::singleShot(0, this, [this, replay] {
            QFile f(replay);
            if (!f.open(QFile::ReadOnly)) { emit died("cannot open " + replay); return; }
            for (auto l : f.readAll().split('\n'))
                if (!l.trimmed().isEmpty() && !l.startsWith("//")) line(l);
        });
        return;
    }
    m_proc.start(coreBinary(), demo ? QStringList{"--demo"} : QStringList{});
}

void CoreLink::send(const QJsonObject &cmd) {
    if (m_replay) return;
    auto c = cmd.value("cmd").toString();
    if (c.contains("voice") || c == "open_guild") ts3cordLog("ui cmd: " + QJsonDocument(cmd).toJson(QJsonDocument::Compact));
    m_proc.write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
}

void CoreLink::line(const QByteArray &l) {
    QJsonParseError err;
    auto d = QJsonDocument::fromJson(l, &err);
    if (!d.isObject()) {
        fprintf(stderr, "core: bad line: %s\n", l.constData());
        return;
    }
    auto o = d.object();
    auto ev = o.value("ev").toString();
    if (ev == "error" || ev == "voice_status" || ev == "login_error" || ev == "connection") ts3cordLog("core event: " + l);
    emit event(ev, o);
}
