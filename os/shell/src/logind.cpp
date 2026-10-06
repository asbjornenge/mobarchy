#include "logind.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>

#include <unistd.h>

namespace {

const QString Login1 = QStringLiteral("org.freedesktop.login1");
const QString ManagerPath = QStringLiteral("/org/freedesktop/login1");
const QString ManagerIface = QStringLiteral("org.freedesktop.login1.Manager");

int readInt(const QString &path, bool *ok = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return 0;
    }
    return f.readLine().trimmed().toInt(ok);
}

}  // namespace

// --- Brightness --------------------------------------------------------------------------

Brightness::Brightness(QObject *parent)
    : QObject(parent)
{
    m_apply.setSingleShot(true);
    m_apply.setInterval(30);
    connect(&m_apply, &QTimer::timeout, this, &Brightness::apply);
    refresh();
}

QList<Brightness::Backlight> Brightness::activeBacklights()
{
    QList<Backlight> out;
    const QDir dir(QStringLiteral("/sys/class/backlight"));
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        const int power = readInt(dir.filePath(name + QStringLiteral("/bl_power")), &ok);
        const int max = readInt(dir.filePath(name + QStringLiteral("/max_brightness")));
        if (ok && power == 0 && max > 0)
            out << Backlight{name, max};
    }
    return out;
}

void Brightness::refresh()
{
    const QList<Backlight> active = activeBacklights();
    const bool available = !active.isEmpty();
    qreal level = m_level;
    if (available) {
        const Backlight &b = active.first();
        level = qreal(readInt(QStringLiteral("/sys/class/backlight/%1/brightness").arg(b.name))) / b.max;
    }
    if (available != m_available || !qFuzzyCompare(level + 1, m_level + 1)) {
        m_available = available;
        m_level = level;
        emit levelChanged();
    }
}

void Brightness::setLevel(qreal level)
{
    // Never all the way down: a black screen is hard to undo on a phone.
    level = qBound(0.02, level, 1.0);
    if (qFuzzyCompare(level + 1, m_level + 1))
        return;
    m_level = level;
    emit levelChanged();
    if (!m_apply.isActive())
        m_apply.start();
}

DisplaySession displaySession()
{
    // Two quick calls to logind: the user, then its display session.
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage getUser = QDBusMessage::createMethodCall(Login1, ManagerPath, ManagerIface,
                                                          QStringLiteral("GetUser"));
    getUser << uint(getuid());
    const QDBusReply<QDBusObjectPath> user = bus.call(getUser, QDBus::Block, 1000);
    if (!user.isValid())
        return {};
    QDBusMessage get = QDBusMessage::createMethodCall(Login1, user.value().path(),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << QStringLiteral("org.freedesktop.login1.User") << QStringLiteral("Display");
    const QDBusReply<QDBusVariant> display = bus.call(get, QDBus::Block, 1000);
    if (!display.isValid())
        return {};
    // Display is (so): session ID and object path.
    const QDBusArgument arg = display.value().variant().value<QDBusArgument>();
    DisplaySession session;
    QDBusObjectPath path;
    arg.beginStructure();
    arg >> session.id >> path;
    arg.endStructure();
    session.path = path.path();
    return session;
}

QString Brightness::sessionPath()
{
    // The shell runs as a user service, outside any session, so session/auto does not
    // work: the user's display session, asked once.
    if (m_session.isEmpty())
        m_session = displaySession().path;
    return m_session;
}

void Brightness::apply()
{
    const QString session = sessionPath();
    if (session.isEmpty()) {
        qWarning() << "brightness: no logind display session";
        return;
    }
    for (const Backlight &b : activeBacklights()) {
        QDBusMessage set = QDBusMessage::createMethodCall(Login1, session,
                                                          QStringLiteral("org.freedesktop.login1.Session"),
                                                          QStringLiteral("SetBrightness"));
        set << QStringLiteral("backlight") << b.name << uint(qMax(1, qRound(m_level * b.max)));
        QDBusConnection::systemBus().asyncCall(set);
    }
}

// --- Power -------------------------------------------------------------------------------

void Power::reboot()
{
    call(QStringLiteral("Reboot"));
}

void Power::powerOff()
{
    call(QStringLiteral("PowerOff"));
}

void Power::call(const QString &method)
{
    QDBusMessage m = QDBusMessage::createMethodCall(Login1, ManagerPath, ManagerIface, method);
    m << false;  // not interactive: no polkit prompt (there is no agent to answer it)
    QDBusConnection::systemBus().asyncCall(m);
}
