#include "device.h"

#include <QFile>
#include <QProcess>

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

bool Device::canRotate() const
{
    return !m_device.tables.value(QStringLiteral("rotation")).toMap().value(QStringLiteral("hook")).toString().isEmpty();
}

bool Device::rotationLockedByDefault(const QString &connector) const
{
    return deviceinfo::display(m_device, connector).value(QStringLiteral("rotation_locked")).toBool();
}

void Device::rotate(const QString &orientation) const
{
    QStringList args = QProcess::splitCommand(
        m_device.tables.value(QStringLiteral("rotation")).toMap().value(QStringLiteral("hook")).toString());
    if (args.isEmpty())
        return;
    const QString program = args.takeFirst();
    QProcess::startDetached(program, args << orientation);
}

int Device::inset(const QString &connector, const QString &edge, int turns) const
{
    // the panel's edges, clockwise from its top
    const int panel[4] = {deviceinfo::topInset(m_device, connector), deviceinfo::sideInset(m_device, connector),
                          0, deviceinfo::sideInset(m_device, connector)};
    static const QStringList edges = {QStringLiteral("top"), QStringLiteral("right"),
                                      QStringLiteral("bottom"), QStringLiteral("left")};
    const qsizetype e = edges.indexOf(edge);
    if (e < 0)
        return 0;
    // the device turned clockwise by a quarter: the panel's top is now on the right
    return panel[(e - turns % 4 + 8) % 4];
}
