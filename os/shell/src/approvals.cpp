#include "approvals.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>

namespace {
// mobarchy-mcp waits as long (approval.py: TIMEOUT_S).
constexpr int AnswerWithinMs = 120 * 1000;
}

// The D-Bus face of Approvals.
class ApprovalsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mobarchy.Approvals")

public:
    explicit ApprovalsAdaptor(Approvals *parent)
        : QDBusAbstractAdaptor(parent)
        , m_a(parent)
    {
    }

public slots:
    QString Ask(const QString &tool, const QString &title, const QString &detail, const QDBusMessage &msg)
    {
        msg.setDelayedReply(true);
        m_a->ask(tool, title, detail, msg);
        return {};
    }

private:
    Approvals *m_a;
};

Approvals::Approvals(QObject *parent)
    : QObject(parent)
{
    new ApprovalsAdaptor(this);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/mobarchy/Approvals"), this)
        || !bus.registerService(QStringLiteral("org.mobarchy.Shell")))
        qWarning() << "approvals: could not take org.mobarchy.Shell (another shell?)";
    m_expire.setSingleShot(true);
    connect(&m_expire, &QTimer::timeout, this, [this] {
        if (pending()) {
            qInfo() << "approvals: no answer to" << m_queue.first().tool;
            reply(m_queue.takeFirst(), QStringLiteral("deny"));
            next();
        }
    });
}

void Approvals::ask(const QString &tool, const QString &title, const QString &detail, const QDBusMessage &msg)
{
    Request r{tool, title, detail, msg, QDeadlineTimer(AnswerWithinMs)};
    if (m_locked) {
        reply(r, QStringLiteral("deny"));
        return;
    }
    qInfo() << "approvals: the agent asks for" << tool;
    m_queue.append(r);
    if (m_queue.size() == 1)
        next();
}

void Approvals::answer(const QString &answer)
{
    if (!pending())
        return;
    const QString a = answer == QLatin1String("once") || answer == QLatin1String("task") ? answer
                                                                                         : QStringLiteral("deny");
    qInfo() << "approvals:" << m_queue.first().tool << a;
    reply(m_queue.takeFirst(), a);
    next();
}

void Approvals::setLocked(bool locked)
{
    if (m_locked == locked)
        return;
    m_locked = locked;
    emit lockedChanged();
    // What waits is turned down: the owner isn't looking at it.
    if (locked && pending()) {
        while (!m_queue.isEmpty())
            reply(m_queue.takeFirst(), QStringLiteral("deny"));
        next();
    }
}

void Approvals::reply(const Request &r, const QString &answer)
{
    QDBusConnection::sessionBus().send(r.msg.createReply(answer));
}

// The next request up (the ones whose caller has given up go), or none.
void Approvals::next()
{
    while (!m_queue.isEmpty() && m_queue.first().deadline.hasExpired())
        reply(m_queue.takeFirst(), QStringLiteral("deny"));
    if (pending())
        m_expire.start(int(m_queue.first().deadline.remainingTime()));
    else
        m_expire.stop();
    emit changed();
}

#include "approvals.moc"
