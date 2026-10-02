// The phone's screen and power key, all generic:
//  - ScreenPower: screens on/off through wlr-output-power-management (Hyprland, Sway, Phoc)
//  - Idle: no input for a while, through ext-idle-notify (respects idle inhibitors, e.g.
//    a playing video)
//  - HardwareKeys: the power and volume keys from evdev (the session user is in the input
//    group), KEY_WAKEUP (a touchscreen's wake gesture while the screen is off) and media keys
//    (a headset's buttons: BlueZ turns them into an input device)
#pragma once

#include "qwayland-ext-idle-notify-v1.h"
#include "qwayland-wlr-output-power-management-unstable-v1.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtension>

class OutputPowerManager : public QWaylandClientExtensionTemplate<OutputPowerManager>,
                           public QtWayland::zwlr_output_power_manager_v1
{
    Q_OBJECT
public:
    OutputPowerManager() : QWaylandClientExtensionTemplate<OutputPowerManager>(1) {}
    void bind() { initialize(); }
};

class OutputPower : public QObject, public QtWayland::zwlr_output_power_v1
{
    Q_OBJECT
public:
    explicit OutputPower(struct ::zwlr_output_power_v1 *object)
        : QtWayland::zwlr_output_power_v1(object) {}
    ~OutputPower() override { if (isInitialized()) destroy(); }
signals:
    void failed();
protected:
    void zwlr_output_power_v1_failed() override { emit failed(); }
};

class ScreenPower : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool on READ on WRITE setOn NOTIFY onChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)

public:
    explicit ScreenPower(QObject *parent = nullptr);
    bool on() const { return m_on; }
    void setOn(bool on);
    bool supported() const { return m_manager->isActive(); }

signals:
    void onChanged();
    void supportedChanged();
    // A screen was added (e.g. the other panel after a fold).
    void screensChanged();

private:
    void attach();

    OutputPowerManager *m_manager;
    QHash<QScreen *, QPointer<OutputPower>> m_outputs;
    bool m_on = true;
};

class IdleNotifier : public QWaylandClientExtensionTemplate<IdleNotifier>,
                     public QtWayland::ext_idle_notifier_v1
{
    Q_OBJECT
public:
    IdleNotifier() : QWaylandClientExtensionTemplate<IdleNotifier>(1) {}
    void bind() { initialize(); }
};

class IdleNotification : public QObject, public QtWayland::ext_idle_notification_v1
{
    Q_OBJECT
public:
    explicit IdleNotification(struct ::ext_idle_notification_v1 *object)
        : QtWayland::ext_idle_notification_v1(object) {}
    ~IdleNotification() override { if (isInitialized()) destroy(); }
signals:
    void idled();
    void resumed();
protected:
    void ext_idle_notification_v1_idled() override { emit idled(); }
    void ext_idle_notification_v1_resumed() override { emit resumed(); }
};

class Idle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)  // seconds, 0 = off
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)

public:
    explicit Idle(QObject *parent = nullptr);
    int timeout() const { return m_timeout; }
    void setTimeout(int seconds);
    bool idle() const { return m_idle; }

signals:
    void timeoutChanged();
    void idleChanged();

private:
    void rearm();

    IdleNotifier *m_notifier;
    QPointer<IdleNotification> m_notification;
    int m_timeout = 0;
    bool m_idle = false;
};

class HardwareKeys : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit HardwareKeys(QObject *parent = nullptr);
    ~HardwareKeys() override;

signals:
    void powerPressed();      // a short press, on release
    void powerLongPressed();  // held for a while (emitted while still held)
    void volumeUp();          // on press, then repeated while held
    void volumeDown();
    void wakeGesture();       // KEY_WAKEUP, e.g. a tap on the dark touchscreen
    void mediaPlayPause();
    void mediaPause();
    void mediaNext();
    void mediaPrevious();

private:
    void scan();        // open input devices with the keys we handle, new ones included
    bool read(int fd);  // false: the device is gone

    QHash<QString, int> m_fds;  // device node -> fd
    QFileSystemWatcher m_dirWatcher;
    QTimer m_rescan;
    QTimer m_powerHold;
    QTimer m_volumeRepeat;
    int m_volumeKey = 0;  // the volume key being held
};
