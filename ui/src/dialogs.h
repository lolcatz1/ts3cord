#pragma once
// dialogs: discord login (ts3 account dialog look), connect (guild picker), contacts, about
#include <QDialog>
#include <functional>

class Store;
class CoreLink;
class QLineEdit;
class QLabel;
class QCheckBox;
class QComboBox;
class QTreeWidget;

namespace com::teamspeak::ui {
class Account_Create_Login_Dialog : public QDialog {
    Q_OBJECT
public:
    Account_Create_Login_Dialog(Store *s, CoreLink *core, QWidget *parent = nullptr);
    void showError(const QString &msg);
    void showQr(const QString &url);
private:
    void askToken();
    Store *m_store;
    CoreLink *m_core;
    QLineEdit *m_email, *m_pass;
    QLabel *m_error;
};
}

class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    ConnectDialog(Store *s, QWidget *parent = nullptr);
    QString guildId() const;
    bool newTab() const { return m_newTab; }
private:
    Store *m_store;
    QComboBox *m_server;
    QLineEdit *m_nick;
    bool m_newTab = false;
};

class ContactsDialog : public QDialog {
    Q_OBJECT
public:
    ContactsDialog(Store *s, CoreLink *core, QWidget *parent = nullptr);
    void reload();
signals:
    void openDm(const QString &userId);
private:
    Store *m_store;
    QTreeWidget *m_list;
};

struct Attachment;

// popup for one message attachment: image (gif animates) or file info, save / open in browser
class MpvWidget;
class QVBoxLayout;
class AttachmentDialog : public QDialog {
    Q_OBJECT
public:
    AttachmentDialog(const Attachment &a, QWidget *parent = nullptr);
private:
    void showImage(const QByteArray &data);
    void showMedia(bool audio, bool loop);
    void fetch(std::function<void(const QByteArray &)> done);
    QVBoxLayout *m_body;
    QLabel *m_view = nullptr;
    MpvWidget *m_player = nullptr;
    QByteArray m_data;
    QString m_url, m_name;
};

// qr code image via libqrencode
QImage qrImage(const QString &text, int scale = 4);
