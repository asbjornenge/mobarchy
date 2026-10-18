#include "geolocation.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcLocation)

// Every call to GeoClue is asynchronous: GeoClue asks its agent, which is this same shell
// (LocationAgent), whether the shell may know where it is, while it handles them. A blocking
// call would wait for GeoClue while GeoClue waits for us.

namespace {
const QString Service = QStringLiteral("org.freedesktop.GeoClue2");
const QString ManagerPath = QStringLiteral("/org/freedesktop/GeoClue2/Manager");
const QString ManagerIface = QStringLiteral("org.freedesktop.GeoClue2.Manager");
const QString ClientIface = QStringLiteral("org.freedesktop.GeoClue2.Client");
const QString LocationIface = QStringLiteral("org.freedesktop.GeoClue2.Location");
const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

QDBusMessage method(const QString &path, const QString &iface, const QString &name, const QVariantList &args = {})
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, path, iface, name);
    m.setArguments(args);
    return m;
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

// call, then `then` with the reply (or failed() and stop on an error)
void GeoLocation::send(const QDBusMessage &m, std::function<void(const QDBusMessage &)> then)
{
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, then, m] {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcLocation) << m.member() << "failed:" << reply.errorMessage();
            stop();
            emit failed(reply.errorMessage());
            return;
        }
        if (then)
            then(reply);
    });
}

void GeoLocation::setClientProperty(const QString &name, const QVariant &value, std::function<void()> then)
{
    send(method(m_client, PropsIface, QStringLiteral("Set"),
                {ClientIface, name, QVariant::fromValue(QDBusVariant(value))}),
         [then](const QDBusMessage &) { then(); });
}

void GeoLocation::refresh(int timeoutSeconds)
{
    if (busy())
        return;
    m_creating = true;
    m_timeout.start(timeoutSeconds * 1000);
    emit busyChanged();
    // (CreateClient: a client of our own, not the one GetClient shares per connection)
    send(method(ManagerPath, ManagerIface, QStringLiteral("CreateClient")), [this](const QDBusMessage &reply) {
        const QString client = reply.arguments().value(0).value<QDBusObjectPath>().path();
        if (!m_creating) {      // (given up meanwhile)
            QDBusConnection::systemBus().asyncCall(method(ManagerPath, ManagerIface, QStringLiteral("DeleteClient"),
                                                          {QVariant::fromValue(QDBusObjectPath(client))}));
            return;
        }
        m_creating = false;
        m_client = client;
        QDBusConnection::systemBus().connect(Service, m_client, ClientIface, QStringLiteral("LocationUpdated"), this,
                                             SLOT(locationUpdated(QDBusObjectPath, QDBusObjectPath)));
        setClientProperty(QStringLiteral("DesktopId"), QStringLiteral("org.mobarchy.Shell"), [this] {
            setClientProperty(QStringLiteral("RequestedAccuracyLevel"), uint(m_level), [this] {
                send(method(m_client, ClientIface, QStringLiteral("Start")), [this](const QDBusMessage &) {
                    qCInfo(lcLocation) << "looking for where the phone is";
                });
            });
        });
    });
}

void GeoLocation::locationUpdated(const QDBusObjectPath &, const QDBusObjectPath &location)
{
    send(method(location.path(), PropsIface, QStringLiteral("GetAll"), {LocationIface}),
         [this](const QDBusMessage &reply) {
        const QVariantMap p = qdbus_cast<QVariantMap>(reply.arguments().value(0));
        m_latitude = p.value(QStringLiteral("Latitude")).toDouble();
        m_longitude = p.value(QStringLiteral("Longitude")).toDouble();
        m_accuracy = p.value(QStringLiteral("Accuracy")).toDouble();
        m_valid = true;
        emit changed();
        stop();     // one fix is what was asked for
    });
}

void GeoLocation::stop()
{
    m_timeout.stop();
    m_creating = false;
    if (m_client.isEmpty()) {
        emit busyChanged();
        return;
    }
    auto bus = QDBusConnection::systemBus();
    bus.disconnect(Service, m_client, ClientIface, QStringLiteral("LocationUpdated"), this,
                   SLOT(locationUpdated(QDBusObjectPath, QDBusObjectPath)));
    // (no answers wanted)
    bus.asyncCall(method(m_client, ClientIface, QStringLiteral("Stop")));
    bus.asyncCall(method(ManagerPath, ManagerIface, QStringLiteral("DeleteClient"),
                         {QVariant::fromValue(QDBusObjectPath(m_client))}));
    m_client.clear();
    emit busyChanged();
}
