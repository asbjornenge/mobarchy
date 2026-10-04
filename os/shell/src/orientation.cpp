#include "orientation.h"

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

DeviceOrientation::DeviceOrientation(QObject *parent)
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

DeviceOrientation::~DeviceOrientation()
{
    if (m_active)
        claim(false);
}

void DeviceOrientation::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    claim(active);
    if (!active)
        setOrientation(QStringLiteral("undefined"));
}

void DeviceOrientation::claim(bool claim)
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Interface,
                                              claim ? QStringLiteral("ClaimAccelerometer")
                                                    : QStringLiteral("ReleaseAccelerometer"));
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

void DeviceOrientation::refresh()
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Properties, QStringLiteral("Get"));
    msg << Interface << QStringLiteral("AccelerometerOrientation");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (!reply.isError() && m_active)
            setOrientation(reply.value().variant().toString());
    });
}

void DeviceOrientation::propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface == Interface && m_active && changed.contains(QStringLiteral("AccelerometerOrientation")))
        setOrientation(changed.value(QStringLiteral("AccelerometerOrientation")).toString());
}

void DeviceOrientation::setOrientation(const QString &orientation)
{
    if (orientation == m_orientation)
        return;
    m_orientation = orientation;
    emit orientationChanged();
}

int DeviceOrientation::degrees() const
{
    if (m_orientation == u"left-up")
        return 90;
    if (m_orientation == u"bottom-up")
        return 180;
    if (m_orientation == u"right-up")
        return 270;
    return 0;
}
