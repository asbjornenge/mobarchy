// The owner's yes or no to a step of the agent's (ADR 0024). mobarchy-mcp asks,
// org.mobarchy.Approvals.Ask(tool, title, detail) on the session bus (the shell is
// org.mobarchy.Shell there), and waits; the approval sheet shows the request and answers:
// "once", "task" (this tool for the rest of the agent's task) or "deny". One at a time, in
// order. A locked phone, or two minutes without an answer, is a "deny".
#pragma once

#include <QDBusMessage>
#include <QDeadlineTimer>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class Approvals : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // A request is waiting for the owner, and what it is.
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString tool READ tool NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    // The shell says so while the phone is locked: requests are turned down then.
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)

public:
    explicit Approvals(QObject *parent = nullptr);

    bool pending() const { return !m_queue.isEmpty(); }
    QString tool() const { return pending() ? m_queue.first().tool : QString(); }
    QString title() const { return pending() ? m_queue.first().title : QString(); }
    QString detail() const { return pending() ? m_queue.first().detail : QString(); }
    bool locked() const { return m_locked; }
    void setLocked(bool locked);

    // The owner's answer to the request shown: "once", "task" or "deny".
    Q_INVOKABLE void answer(const QString &answer);

    // From D-Bus: the reply goes out when the owner has answered.
    void ask(const QString &tool, const QString &title, const QString &detail, const QDBusMessage &msg);

signals:
    void changed();
    void lockedChanged();

private:
    struct Request {
        QString tool, title, detail;
        QDBusMessage msg;
        QDeadlineTimer deadline;
    };
    void reply(const Request &r, const QString &answer);
    void next();

    QList<Request> m_queue;
    QTimer m_expire;
    bool m_locked = false;
};
