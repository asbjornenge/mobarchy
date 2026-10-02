#include "inhibit.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>

// The D-Bus face of Inhibitors: the part of org.freedesktop.ScreenSaver that apps use.
class ScreenSaverAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")

public:
    explicit ScreenSaverAdaptor(Inhibitors *parent)
        : QDBusAbstractAdaptor(parent)
        , m_i(parent)
    {
    }

public slots:
    uint Inhibit(const QString &application_name, const QString &reason_for_inhibit, const QDBusMessage &msg)
    {
        return m_i->inhibit(application_name, reason_for_inhibit, msg.service());
    }
    void UnInhibit(uint cookie, const QDBusMessage &msg) { m_i->uninhibit(cookie, msg.service()); }

private:
    Inhibitors *m_i;
};

Inhibitors::Inhibitors(QObject *parent)
    : QObject(parent)
    , m_watcher(new QDBusServiceWatcher(this))
{
    new ScreenSaverAdaptor(this);
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_watcher->setConnection(bus);
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &Inhibitors::senderGone);
    // Apps use either path.
    m_registered = bus.registerObject(QStringLiteral("/org/freedesktop/ScreenSaver"), this)
        && bus.registerObject(QStringLiteral("/ScreenSaver"), this)
        && bus.registerService(QStringLiteral("org.freedesktop.ScreenSaver"));
    if (!m_registered)
        qWarning() << "inhibitors: could not take org.freedesktop.ScreenSaver (another service?)";
}

uint Inhibitors::inhibit(const QString &app, const QString &reason, const QString &sender)
{
    const bool was = active();
    const uint cookie = m_nextCookie++;
    m_inhibitions.insert(cookie, {app, reason, sender});
    if (!m_watcher->watchedServices().contains(sender))
        m_watcher->addWatchedService(sender);
    qInfo() << "inhibitors:" << app << "keeps the screen on:" << reason;
    if (!was)
        emit activeChanged();
    return cookie;
}

void Inhibitors::uninhibit(uint cookie, const QString &sender)
{
    const auto it = m_inhibitions.constFind(cookie);
    if (it == m_inhibitions.cend() || it->sender != sender)  // only the app that asked
        return;
    m_inhibitions.erase(it);
    if (!active())
        emit activeChanged();
}

void Inhibitors::senderGone(const QString &sender)
{
    m_watcher->removeWatchedService(sender);
    const bool was = active();
    m_inhibitions.removeIf([&](const auto &i) { return i.value().sender == sender; });
    if (was && !active())
        emit activeChanged();
}

#include "inhibit.moc"
