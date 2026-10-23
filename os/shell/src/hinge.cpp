#include "hinge.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>

namespace {
const QString Service = QStringLiteral("net.hadess.SensorProxy");
const QString Path = QStringLiteral("/net/hadess/SensorProxy");
const QString Interface = QStringLiteral("org.mobarchy.SensorProxy.Hinge");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
}  // namespace

Hinge::Hinge(QObject *parent)
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

Hinge::~Hinge()
{
    if (m_active)
        claim(false);
}

void Hinge::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    claim(active);
    if (!active)
        setAngle(-1);
}

void Hinge::claim(bool claim)
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Interface,
                                              claim ? QStringLiteral("ClaimHinge") : QStringLiteral("ReleaseHinge"));
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

void Hinge::refresh()
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Properties, QStringLiteral("Get"));
    msg << Interface << QStringLiteral("HingeAngle");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (!reply.isError() && m_active)
            setAngle(reply.value().variant().toDouble());
    });
}

void Hinge::propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface == Interface && m_active && changed.contains(QStringLiteral("HingeAngle")))
        setAngle(changed.value(QStringLiteral("HingeAngle")).toDouble());
}

void Hinge::setAngle(qreal angle)
{
    if (qFuzzyCompare(angle + 2, m_angle + 2))
        return;
    m_angle = angle;
    emit angleChanged();
}
