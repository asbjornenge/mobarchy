#include "messagesthread.h"

#include "launch.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QRandomGenerator>

namespace {
const QString Service = QStringLiteral("org.mobarchy.Messages");
const QString Path = QStringLiteral("/org/mobarchy/Messages");
}

void MessagesThread::setService(bool on)
{
    if (on == m_service)
        return;
    m_service = on;
    if (on) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.registerObject(Path, Service, this, QDBusConnection::ExportAllSlots)
            || !bus.registerService(Service))
            qWarning("messages: can't take org.mobarchy.Messages on the session bus");
    }
    emit serviceChanged();
}

void MessagesThread::Show(const QString &number)
{
    emit showRequested(number);
}

void MessagesThread::open(const QString &number)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.interface() && bus.interface()->isServiceRegistered(Service)) {
        bus.asyncCall(QDBusMessage::createMethodCall(Service, Path, Service, QStringLiteral("Show")) << number);
        return;
    }
    const QString unit = QStringLiteral("app-mobarchy-org.mobarchy.Messages-%1.scope")
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QChar(u'0'));
    launch::detached(QStringLiteral("systemd-run"),
                     {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"),
                      QStringLiteral("--collect"), QStringLiteral("--slice=app.slice"),
                      QStringLiteral("--unit=") + unit, QStringLiteral("--"), QStringLiteral("mobarchy-shell"),
                      QStringLiteral("--messages"), number});
}
