#include "device.h"

#include <QFile>

Device::Device(QObject *parent)
    : QObject(parent)
{
    QFile f(qEnvironmentVariable("MOBARCHY_DEVICE_TOML", QStringLiteral("/etc/mobarchy/device.toml")));
    if (f.open(QIODevice::ReadOnly))
        m_device = deviceinfo::parse(f.readAll());
    else
        qWarning() << "no device description:" << f.fileName();
}

int Device::topInset(const QString &connector) const
{
    return deviceinfo::topInset(m_device, connector);
}

int Device::sideInset(const QString &connector) const
{
    return deviceinfo::sideInset(m_device, connector);
}
