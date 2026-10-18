#include "corelink.h"
#include "mainwindow.h"
#include "skin.h"
#include "store.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QMessageBox>

// everything (qt warnings, core stderr) also goes to ~/.cache/ts3cord/ts3cord.log
static QFile *g_log = nullptr;
void ts3cordLog(const QByteArray &line) {
    if (!g_log) return;
    g_log->write(QDateTime::currentDateTime().toString("hh:mm:ss.zzz ").toUtf8() + line + (line.endsWith('\n') ? "" : "\n"));
    g_log->flush();
}
static void messages(QtMsgType, const QMessageLogContext &, const QString &msg) {
    fprintf(stderr, "%s\n", qPrintable(msg));
    ts3cordLog(msg.toUtf8());
}

int main(int argc, char **argv) {
    // ts3 runs on xcb without a desktop platform theme; same here so qss, palette and fonts match
    if (!qEnvironmentVariableIsSet("TS3CORD_NATIVE")) {
        qputenv("QT_QPA_PLATFORMTHEME", "");
        qputenv("QT_STYLE_OVERRIDE", "");
        // linux look matches ts3 on xcb; win7 look runs native so wayland maps the app id to ts3cord.desktop (taskbar icon)
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && qEnvironmentVariable("TS3CORD_LOOK", "win7") == "linux") qputenv("QT_QPA_PLATFORM", "xcb");
        qunsetenv("KDE_FULL_SESSION");
        qunsetenv("XDG_CURRENT_DESKTOP");
    }
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setDesktopFileName("ts3cord");
    QApplication app(argc, argv);
    app.setOrganizationName("ts3cord");
    app.setApplicationName("ts3cord");
    {
        auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        QFile::remove(dir + "/ts3cord.log.old");
        QFile::rename(dir + "/ts3cord.log", dir + "/ts3cord.log.old");
        g_log = new QFile(dir + "/ts3cord.log");
        g_log->open(QFile::WriteOnly | QFile::Text);
        qInstallMessageHandler(messages);
    }
    QCommandLineParser cl;
    cl.addHelpOption();
    cl.addOption({"demo", "fake data from the core, no network"});
    cl.addOption({"replay", "feed events from a .jsonl file instead of the core", "file"});
    cl.addOption({"size", "window size WxH", "size"});
    cl.process(app);
    app.setDesktopFileName("ts3cord");
    if (!skin::apply()) {
        QMessageBox::critical(nullptr, "ts3cord", "ts3 folder missing (styles, icons, sounds).");
        return 1;
    }
    QApplication::setWindowIcon(skin::appIcon());
    Store store;
    CoreLink core;
    MainWindow w(&store, &core);
    if (cl.isSet("size")) {
        auto wh = cl.value("size").split('x');
        w.resize(wh[0].toInt(), wh[1].toInt());
    }
    w.show();
    core.start(cl.value("replay"), cl.isSet("demo"));
    core.send("start");
    return app.exec();
}
