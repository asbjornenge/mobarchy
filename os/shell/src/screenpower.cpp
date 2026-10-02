#include "screenpower.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

// --- ScreenPower -------------------------------------------------------------------------

ScreenPower::ScreenPower(QObject *parent)
    : QObject(parent)
    , m_manager(new OutputPowerManager)
{
    m_manager->setParent(this);
    connect(m_manager, &OutputPowerManager::activeChanged, this, [this] {
        attach();
        emit supportedChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] {
        attach();
        emit screensChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *s) {
        delete m_outputs.take(s);
    });
    m_manager->bind();
}

void ScreenPower::attach()
{
    if (!m_manager->isActive())
        return;
    for (QScreen *s : QGuiApplication::screens()) {
        if (m_outputs.value(s))
            continue;
        auto *w = s->nativeInterface<QNativeInterface::QWaylandScreen>();
        if (!w || !w->output())
            continue;
        auto *p = new OutputPower(m_manager->get_output_power(w->output()));
        p->setParent(this);
        connect(p, &OutputPower::failed, p, &QObject::deleteLater);
        m_outputs.insert(s, p);
        // Apply our state: after a restart of the shell (e.g. a crash while the screen was
        // off), this is what turns the screen back on.
        p->set_mode(m_on ? QtWayland::zwlr_output_power_v1::mode_on : QtWayland::zwlr_output_power_v1::mode_off);
    }
}

void ScreenPower::setOn(bool on)
{
    attach();
    for (const QPointer<OutputPower> &p : std::as_const(m_outputs)) {
        if (p)
            p->set_mode(on ? QtWayland::zwlr_output_power_v1::mode_on : QtWayland::zwlr_output_power_v1::mode_off);
    }
    if (on != m_on) {
        m_on = on;
        emit onChanged();
    }
}

// --- Idle --------------------------------------------------------------------------------

Idle::Idle(QObject *parent)
    : QObject(parent)
    , m_notifier(new IdleNotifier)
{
    m_notifier->setParent(this);
    connect(m_notifier, &IdleNotifier::activeChanged, this, &Idle::rearm);
    m_notifier->bind();
}

void Idle::setTimeout(int seconds)
{
    if (seconds == m_timeout)
        return;
    m_timeout = seconds;
    emit timeoutChanged();
    rearm();
}

void Idle::rearm()
{
    delete m_notification;
    if (m_idle) {
        m_idle = false;
        emit idleChanged();
    }
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (m_timeout <= 0 || !m_notifier->isActive() || !wayland || !wayland->seat())
        return;
    m_notification = new IdleNotification(
        m_notifier->get_idle_notification(uint32_t(m_timeout) * 1000, wayland->seat()));
    m_notification->setParent(this);
    connect(m_notification, &IdleNotification::idled, this, [this] {
        m_idle = true;
        emit idleChanged();
    });
    connect(m_notification, &IdleNotification::resumed, this, [this] {
        m_idle = false;
        emit idleChanged();
    });
}

// --- PowerButton -------------------------------------------------------------------------

PowerButton::PowerButton(QObject *parent)
    : QObject(parent)
{
    m_hold.setSingleShot(true);
    m_hold.setInterval(600);
    connect(&m_hold, &QTimer::timeout, this, [this] {
        m_longFired = true;
        emit longPressed();
    });
    const QDir dir(QStringLiteral("/dev/input"));
    for (const QString &name : dir.entryList({QStringLiteral("event*")}, QDir::System)) {
        const int fd = ::open(QFile::encodeName(dir.filePath(name)).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {};
        const bool hasPower = ::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0
            && (keys[KEY_POWER / (8 * sizeof(long))] >> (KEY_POWER % (8 * sizeof(long)))) & 1;
        if (!hasPower) {
            ::close(fd);
            continue;
        }
        m_fds << fd;
        auto *n = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(n, &QSocketNotifier::activated, this, [this, fd, n] {
            if (!read(fd)) {  // the device went away (ENODEV): stop watching it
                n->setEnabled(false);
                n->deleteLater();
                m_fds.removeOne(fd);
                ::close(fd);
            }
        });
    }
    if (m_fds.isEmpty())
        qWarning() << "power button: no input device with KEY_POWER (is the user in the input group?)";
}

PowerButton::~PowerButton()
{
    for (int fd : std::as_const(m_fds))
        ::close(fd);
}

bool PowerButton::read(int fd)
{
    input_event ev;
    ssize_t n;
    while ((n = ::read(fd, &ev, sizeof(ev))) == ssize_t(sizeof(ev))) {
        if (ev.type != EV_KEY || ev.code != KEY_POWER)
            continue;
        if (ev.value == 1) {
            m_longFired = false;
            m_hold.start();
        } else if (ev.value == 0) {
            if (m_hold.isActive()) {
                m_hold.stop();
                emit pressed();
            }
        }
    }
    return n >= 0 || errno == EAGAIN || errno == EINTR;
}
