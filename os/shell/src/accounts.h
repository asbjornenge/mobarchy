// The owner's internet accounts (ADR 0032), through the `mobarchy-accounts` command (the agent
// package's): iCloud, Google, Nextcloud and other CardDAV accounts, whose contacts the phone keeps. The
// Accounts app's backend: it runs the command and shows what it prints (JSON); passwords go to
// it on stdin, and from it into the keyring, never into a file or an argument.
#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Accounts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{id, type (icloud/google/nextcloud/carddav), user, last_sync, error, contacts, books: [{url, name, enabled}]}]
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY changed)
    // there is a Google OAuth client (Mobarchy's, or the owner's own): Google accounts can be added
    Q_PROPERTY(bool googleClient READ googleClient NOTIFY changed)
    // something is running (adding, syncing): what, for the screen ("" when idle)
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY busyChanged)

public:
    explicit Accounts(QObject *parent = nullptr);

    QVariantList accounts() const { return m_accounts; }
    bool googleClient() const { return m_googleClient; }
    QString busy() const { return m_busy; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addIcloud(const QString &appleId, const QString &appPassword);
    Q_INVOKABLE void addCarddav(const QString &url, const QString &user, const QString &password);
    Q_INVOKABLE void addGoogle();
    Q_INVOKABLE void addNextcloud(const QString &url);
    // a web page in the browser (where an app password is made)
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE void setGoogleClient(const QString &clientId, const QString &clientSecret);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void setBook(const QString &id, const QString &url, bool enabled);
    Q_INVOKABLE void setCalendar(const QString &id, const QString &url, bool enabled);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void cancel();

signals:
    void changed();
    void busyChanged();
    // an account was added (the app goes back to the list)
    void added();

private:
    void run(const QString &what, const QStringList &args, const QString &input = {}, bool adds = false);
    // a switch turned: no word while it is saved (the switch says it), and none lost if
    // another is turned meanwhile
    void quietly(const QStringList &args);
    void take(const QByteArray &json);

    QProcess *m_proc = nullptr;
    QList<QStringList> m_queue;     // (quietly's, waiting for the one running)
    QVariantList m_accounts;
    bool m_googleClient = false;
    QString m_busy, m_error;
};
