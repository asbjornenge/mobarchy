#include "torch.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>

namespace {
const QString Service = QStringLiteral("org.mobarchy.Camera");
const QString Path = QStringLiteral("/org/mobarchy/Camera");
const QString Interface = QStringLiteral("org.mobarchy.Camera");
}  // namespace

Torch::Torch(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    auto *watcher = new QDBusServiceWatcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &Torch::refresh);
    refresh();
}

void Torch::refresh()
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("Get"));
    msg << Interface << QStringLiteral("Torch");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        const bool available = !reply.isError();
        if (available != m_available) {
            m_available = available;
            emit availableChanged();
        }
        setState(available && reply.value().variant().toBool());
    });
}

void Torch::setOn(bool on)
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("SetTorch"));
    msg << on;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<bool> reply = *w;
        if (reply.isError())
            qWarning() << "torch:" << reply.error().message();
        else
            setState(reply.value());
    });
}

void Torch::propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface == Interface && changed.contains(QStringLiteral("Torch")))
        setState(changed.value(QStringLiteral("Torch")).toBool());
}

void Torch::setState(bool on)
{
    if (on == m_on)
        return;
    m_on = on;
    emit onChanged();
}
