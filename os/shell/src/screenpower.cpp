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
        emit inUseChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *s) {
        delete m_outputs.take(s);
        emit inUseChanged();
    });
    m_manager->bind();
}

QString ScreenPower::inUse() const
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    return screens.isEmpty() ? QString() : screens.first()->name();
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

// --- HardwareKeys ------------------------------------------------------------------------

namespace {

bool hasKey(const unsigned long *bits, int key)
{
    return (bits[key / (8 * sizeof(long))] >> (key % (8 * sizeof(long)))) & 1;
}

}  // namespace

HardwareKeys::HardwareKeys(QObject *parent)
    : QObject(parent)
{
    m_powerHold.setSingleShot(true);
    m_powerHold.setInterval(600);
    connect(&m_powerHold, &QTimer::timeout, this, &HardwareKeys::powerLongPressed);
    // Held volume keys repeat: a pause, then steady steps.
    m_volumeRepeat.setInterval(400);
    connect(&m_volumeRepeat, &QTimer::timeout, this, [this] {
        m_volumeRepeat.setInterval(110);
        m_volumeKey == KEY_VOLUMEUP ? emit volumeUp() : emit volumeDown();
    });

    m_volumeDownDelay.setSingleShot(true);
    m_volumeDownDelay.setInterval(150);
    connect(&m_volumeDownDelay, &QTimer::timeout, this, [this] {
        emit volumeDown();
        m_volumeRepeat.setInterval(250);
        m_volumeRepeat.start();
    });

    // Devices come and go (a Bluetooth headset's buttons appear when it connects).
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(300);  // udev gives the session user access just after the node appears
    connect(&m_rescan, &QTimer::timeout, this, &HardwareKeys::scan);
    m_dirWatcher.addPath(QStringLiteral("/dev/input"));
    connect(&m_dirWatcher, &QFileSystemWatcher::directoryChanged, &m_rescan, qOverload<>(&QTimer::start));
    scan();
    if (m_fds.isEmpty())
        qWarning() << "hardware keys: no input device with power, volume, wake or media keys (is the user in the input group?)";
}

void HardwareKeys::scan()
{
    const QDir dir(QStringLiteral("/dev/input"));
    for (const QString &name : dir.entryList({QStringLiteral("event*")}, QDir::System)) {
        const QString path = dir.filePath(name);
        if (m_fds.contains(path))
            continue;
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {};
        const bool wanted = ::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0
            && (hasKey(keys, KEY_POWER) || hasKey(keys, KEY_VOLUMEUP) || hasKey(keys, KEY_VOLUMEDOWN)
                || hasKey(keys, KEY_WAKEUP) || hasKey(keys, KEY_PLAYPAUSE) || hasKey(keys, KEY_PLAYCD)
                || hasKey(keys, KEY_NEXTSONG));
        if (!wanted) {
            ::close(fd);
            continue;
        }
        m_fds.insert(path, fd);
        auto *n = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(n, &QSocketNotifier::activated, this, [this, fd, n, path] {
            if (!read(fd)) {  // the device went away (ENODEV): stop watching it
                n->setEnabled(false);
                n->deleteLater();
                m_fds.remove(path);
                ::close(fd);
            }
        });
    }
}

HardwareKeys::~HardwareKeys()
{
    for (int fd : std::as_const(m_fds))
        ::close(fd);
}

bool HardwareKeys::read(int fd)
{
    input_event ev;
    ssize_t n;
    while ((n = ::read(fd, &ev, sizeof(ev))) == ssize_t(sizeof(ev))) {
        if (ev.type != EV_KEY)
            continue;
        if (ev.code == KEY_POWER) {
            if (ev.value == 1) {
                m_powerDown = true;
                if (m_volumeDownDown)
                    combo();
                else
                    m_powerHold.start();
            } else if (ev.value == 0) {
                m_powerDown = false;
                if (m_combo) {
                    m_combo = m_volumeDownDown;
                } else if (m_powerHold.isActive()) {
                    m_powerHold.stop();
                    emit powerPressed();
                }
            }
        } else if (ev.value == 1 && (ev.code == KEY_PLAYPAUSE || ev.code == KEY_PLAYCD)) {
            // A headset's "play" is a toggle: it doesn't know the player's state.
            emit mediaPlayPause();
        } else if (ev.value == 1 && (ev.code == KEY_PAUSECD || ev.code == KEY_STOPCD)) {
            emit mediaPause();  // e.g. AirPods taken out of the ears
        } else if (ev.value == 1 && ev.code == KEY_NEXTSONG) {
            emit mediaNext();
        } else if (ev.value == 1 && ev.code == KEY_PREVIOUSSONG) {
            emit mediaPrevious();
        } else if (ev.code == KEY_WAKEUP) {
            if (ev.value == 1)
                emit wakeGesture();
        } else if (ev.code == KEY_VOLUMEDOWN && ev.value == 1 && m_powerDown) {
            m_volumeDownDown = true;
            combo();
        } else if (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN) {
            if (ev.code == KEY_VOLUMEDOWN && ev.value != 2)
                m_volumeDownDown = ev.value == 1;
            if (ev.code == KEY_VOLUMEDOWN && ev.value == 0 && m_combo)
                m_combo = m_powerDown;
            if (ev.value == 1) {
                m_volumeKey = ev.code;
                if (ev.code == KEY_VOLUMEDOWN) {
                    // (a moment first: with power just after, it's a screenshot, not a step)
                    m_volumeDownDelay.start();
                } else {
                    emit volumeUp();
                    m_volumeRepeat.setInterval(400);
                    m_volumeRepeat.start();
                }
            } else if (ev.value == 0 && ev.code == m_volumeKey) {
                if (m_volumeDownDelay.isActive()) {
                    m_volumeDownDelay.stop();
                    emit volumeDown();
                }
                m_volumeRepeat.stop();
                m_volumeKey = 0;
            }
        }
    }
    return n >= 0 || errno == EAGAIN || errno == EINTR;
}

void HardwareKeys::combo()
{
    if (m_combo)
        return;
    m_combo = true;
    m_powerHold.stop();
    m_volumeDownDelay.stop();
    m_volumeRepeat.stop();
    m_volumeKey = 0;
    emit screenshot();
}
