#include "applink.h"

#include "launch.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QRandomGenerator>

namespace {
QString pathFor(const QString &name)
{
    return QLatin1Char('/') + QString(name).replace(QLatin1Char('.'), QLatin1Char('/'));
}
}

void AppLink::setName(const QString &name)
{
    if (name == m_name)
        return;
    m_name = name;
    emit changed();
    announce();
}

void AppLink::setService(bool on)
{
    if (on == m_service)
        return;
    m_service = on;
    emit changed();
    announce();
}

void AppLink::announce()
{
    if (!m_service || m_name.isEmpty() || m_announced)
        return;
    m_announced = true;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(pathFor(m_name), m_name, this, QDBusConnection::ExportAllSlots)
        || !bus.registerService(m_name))
        qWarning() << "can't take" << m_name << "on the session bus";
}

void AppLink::Show(const QString &arg)
{
    emit showRequested(arg);
}

void AppLink::open(const QString &name, const QString &flag, const QString &arg)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.interface() && bus.interface()->isServiceRegistered(name)) {
        bus.asyncCall(QDBusMessage::createMethodCall(name, pathFor(name), name, QStringLiteral("Show")) << arg);
        return;
    }
    const QString unit = QStringLiteral("app-mobarchy-%1-%2.scope")
                             .arg(name)
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QChar(u'0'));
    QStringList args{QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"),
                     QStringLiteral("--collect"), QStringLiteral("--slice=app.slice"),
                     QStringLiteral("--unit=") + unit, QStringLiteral("--"), QStringLiteral("mobarchy-shell"), flag};
    if (!arg.isEmpty())
        args << arg;
    launch::detached(QStringLiteral("systemd-run"), args);
}
