#include "switcherkeys.h"

#include <QDBusConnection>
#include <QGuiApplication>

SwitcherKeys::SwitcherKeys(QObject *parent)
    : QObject(parent)
{
    // (on the shell's name, org.mobarchy.Shell, which Approvals takes)
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/mobarchy/Switcher"),
                                                 QStringLiteral("org.mobarchy.Switcher"), this,
                                                 QDBusConnection::ExportScriptableSlots);
}

bool SwitcherKeys::superHeld() const
{
    return QGuiApplication::queryKeyboardModifiers() & Qt::MetaModifier;
}
