// Logins from 1Password (ADR 0023), through its CLI (op), which the 1Password app unlocks:
// the logins for a site, and one's username, password and one-time code to fill in. Nothing
// is kept here.
#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class OnePassword : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // op is installed
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit OnePassword(QObject *parent = nullptr);
    bool available() const { return !m_op.isEmpty(); }
    bool busy() const { return m_busy; }

    // The logins whose websites are this host's (or a parent domain's): listed().
    Q_INVOKABLE void logins(const QString &host);
    // One login's username, password and current one-time code: credentials().
    Q_INVOKABLE void credentials(const QString &id);

    // (for tests) whether a login's URL is for this host
    static bool matches(const QString &loginUrl, const QString &host);

signals:
    void busyChanged();
    // [{id, title, username}]; error: what went wrong (op not connected to the app, ...)
    void listed(const QVariantList &logins, const QString &error);
    void credentialsReady(const QString &username, const QString &password, const QString &otp,
                          const QString &error);

private:
    using Done = std::function<void(int, const QByteArray &, const QString &)>;
    void run(const QStringList &args, Done done);
    void next();
    void setBusy(bool busy);

    QString m_op;
    bool m_busy = false;
    // the session (helper/op-session): one process for the browser's lifetime, so 1Password
    // asks once per session, not at every login
    QProcess *m_session = nullptr;
    QByteArray m_buffer;
    QQueue<QPair<QStringList, Done>> m_queue;
    bool m_waiting = false;
};
