#include "geolocation.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>

namespace {
const QString Service = QStringLiteral("org.freedesktop.GeoClue2");
const QString ManagerPath = QStringLiteral("/org/freedesktop/GeoClue2/Manager");
const QString ManagerIface = QStringLiteral("org.freedesktop.GeoClue2.Manager");
const QString ClientIface = QStringLiteral("org.freedesktop.GeoClue2.Client");
const QString LocationIface = QStringLiteral("org.freedesktop.GeoClue2.Location");
const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

void setClientProperty(const QString &path, const QString &name, const QVariant &value)
{
    QDBusInterface props(Service, path, PropsIface, QDBusConnection::systemBus());
    props.call(QStringLiteral("Set"), ClientIface, name, QVariant::fromValue(QDBusVariant(value)));
}
}

GeoLocation::GeoLocation(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        stop();
        emit failed(tr("No position yet"));
    });
}

GeoLocation::~GeoLocation()
{
    stop();
}

void GeoLocation::refresh(int timeoutSeconds)
{
    if (busy())
        return;
    auto bus = QDBusConnection::systemBus();
    QDBusInterface manager(Service, ManagerPath, ManagerIface, bus);
    // (CreateClient: a client of our own, not the one GetClient shares per connection)
    QDBusReply<QDBusObjectPath> client = manager.call(QStringLiteral("CreateClient"));
    if (!client.isValid()) {
        emit failed(client.error().message());
        return;
    }
    m_client = client.value().path();
    setClientProperty(m_client, QStringLiteral("DesktopId"), QStringLiteral("org.mobarchy.Shell"));
    setClientProperty(m_client, QStringLiteral("RequestedAccuracyLevel"), uint(m_level));
    bus.connect(Service, m_client, ClientIface, QStringLiteral("LocationUpdated"), this,
                SLOT(locationUpdated(QDBusObjectPath, QDBusObjectPath)));
    QDBusInterface iface(Service, m_client, ClientIface, bus);
    const QDBusMessage started = iface.call(QStringLiteral("Start"));
    if (started.type() == QDBusMessage::ErrorMessage) {
        const QString reason = started.errorMessage();
        stop();
        emit failed(reason);
        return;
    }
    m_timeout.start(timeoutSeconds * 1000);
    emit busyChanged();
}

void GeoLocation::locationUpdated(const QDBusObjectPath &, const QDBusObjectPath &location)
{
    QDBusInterface loc(Service, location.path(), LocationIface, QDBusConnection::systemBus());
    m_latitude = loc.property("Latitude").toDouble();
    m_longitude = loc.property("Longitude").toDouble();
    m_accuracy = loc.property("Accuracy").toDouble();
    m_valid = true;
    emit changed();
    stop();     // one fix is what was asked for
}

void GeoLocation::stop()
{
    m_timeout.stop();
    if (m_client.isEmpty())
        return;
    auto bus = QDBusConnection::systemBus();
    bus.disconnect(Service, m_client, ClientIface, QStringLiteral("LocationUpdated"), this,
                   SLOT(locationUpdated(QDBusObjectPath, QDBusObjectPath)));
    QDBusInterface(Service, m_client, ClientIface, bus).call(QStringLiteral("Stop"));
    QDBusInterface(Service, ManagerPath, ManagerIface, bus)
        .call(QStringLiteral("DeleteClient"), QVariant::fromValue(QDBusObjectPath(m_client)));
    m_client.clear();
    emit busyChanged();
}
