#include "keyboard.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

// All calls are asynchronous: the shell must never block on the keyboard process.
namespace {
const QString Service = QStringLiteral("sm.puri.OSK0");
const QString Path = QStringLiteral("/sm/puri/OSK0");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
}

Keyboard::Keyboard(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(Service, Path, Properties, QStringLiteral("PropertiesChanged"), this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    QDBusMessage get = QDBusMessage::createMethodCall(Service, Path, Properties, QStringLiteral("Get"));
    get << Service << QStringLiteral("Visible");
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(get), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isValid() && reply.value().variant().toBool() != m_visible) {
            m_visible = !m_visible;
            emit visibleChanged();
        }
        w->deleteLater();
    });
}

void Keyboard::setVisible(bool visible)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Service, QStringLiteral("SetVisible"));
    call << visible;
    QDBusConnection::sessionBus().asyncCall(call);
    if (visible != m_visible) {  // optimistic; PropertiesChanged confirms it
        m_visible = visible;
        emit visibleChanged();
    }
}

void Keyboard::propertiesChanged(const QString &interface, const QVariantMap &changed,
                                 const QStringList &)
{
    if (interface != Service || !changed.contains(QStringLiteral("Visible")))
        return;
    const bool visible = changed.value(QStringLiteral("Visible")).toBool();
    if (visible != m_visible) {
        m_visible = visible;
        emit visibleChanged();
    }
}
