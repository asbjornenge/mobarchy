#include "device.h"
#include "launch.h"

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

QString Device::foldAxis(const QString &connector) const
{
    return deviceinfo::display(m_device, connector).value(QStringLiteral("fold_axis")).toString();
}

void Device::rotate(const QString &orientation) const
{
    QStringList args = QProcess::splitCommand(
        m_device.tables.value(QStringLiteral("rotation")).toMap().value(QStringLiteral("hook")).toString());
    if (args.isEmpty())
        return;
    const QString program = args.takeFirst();
    launch::detached(program, args << orientation);
}

QRectF Device::cutout(const QString &connector, int turns, qreal width, qreal height) const
{
    const QList<qreal> c = deviceinfo::cutout(m_device, connector);
    if (c.isEmpty())
        return {};
    // the panel upright is the screen turned back (a quarter or three: sides swapped)
    turns = (turns % 4 + 4) % 4;
    const qreal w = turns % 2 ? height : width, h = turns % 2 ? width : height;
    const qreal x = c[0], y = c[1], r = c[2];
    // the device turned clockwise by a quarter: the panel's top is now on the right
    const QPointF at = turns == 0 ? QPointF(x, y)
                     : turns == 1 ? QPointF(h - y, x)
                     : turns == 2 ? QPointF(w - x, h - y)
                                  : QPointF(y, w - x);
    return {at.x() - r, at.y() - r, 2 * r, 2 * r};
}

int Device::inset(const QString &connector, const QString &edge, int turns) const
{
    // the panel's edges, clockwise from its top
    const int panel[4] = {deviceinfo::topInset(m_device, connector), deviceinfo::sideInset(m_device, connector),
                          deviceinfo::bottomInset(m_device, connector), deviceinfo::sideInset(m_device, connector)};
    static const QStringList edges = {QStringLiteral("top"), QStringLiteral("right"),
                                      QStringLiteral("bottom"), QStringLiteral("left")};
    const qsizetype e = edges.indexOf(edge);
    if (e < 0)
        return 0;
    // the device turned clockwise by a quarter: the panel's top is now on the right
    return panel[(e - turns % 4 + 8) % 4];
}
