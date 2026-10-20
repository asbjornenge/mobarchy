#include "proximity.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>

namespace {
const QString Service = QStringLiteral("net.hadess.SensorProxy");
const QString Path = QStringLiteral("/net/hadess/SensorProxy");
const QString Interface = QStringLiteral("net.hadess.SensorProxy");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
}  // namespace

Proximity::Proximity(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(Service, Path, Properties, QStringLiteral("PropertiesChanged"), this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    // The proxy restarted: its claims are gone; claim again.
    auto *watcher = new QDBusServiceWatcher(Service, bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        if (m_active)
            claim(true);
    });
}

Proximity::~Proximity()
{
    if (m_active)
        claim(false);
}

void Proximity::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    claim(active);
    if (!active)
        setNear(false);
}

void Proximity::claim(bool claim)
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Interface,
                                              claim ? QStringLiteral("ClaimProximity")
                                                    : QStringLiteral("ReleaseProximity"));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, claim](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const bool available = !w->isError();
        if (available != m_available) {
            m_available = available;
            emit availableChanged();
        }
        if (claim && available)
            refresh();
    });
}

void Proximity::refresh()
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Properties, QStringLiteral("Get"));
    msg << Interface << QStringLiteral("ProximityNear");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (!reply.isError() && m_active)
            setNear(reply.value().variant().toBool());
    });
}

void Proximity::propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface == Interface && m_active && changed.contains(QStringLiteral("ProximityNear")))
        setNear(changed.value(QStringLiteral("ProximityNear")).toBool());
}

void Proximity::setNear(bool near)
{
    if (near == m_near)
        return;
    m_near = near;
    emit nearChanged();
}
