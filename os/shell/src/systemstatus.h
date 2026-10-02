// Battery and wifi state for the status bar, read from sysfs and procfs (generic Linux
// interfaces, no device-specific paths). Drivers that leave the signal level out of
// /proc/net/wireless (bcmdhd) get it from `iw station dump`, which asks nl80211.
#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class SystemStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int battery READ battery NOTIFY changed)            // percent, -1 if none
    Q_PROPERTY(bool charging READ charging NOTIFY changed)
    Q_PROPERTY(bool wifiConnected READ wifiConnected NOTIFY changed)
    Q_PROPERTY(int wifiStrength READ wifiStrength NOTIFY changed)  // 0..4, -1 unknown
    Q_PROPERTY(QString wifiName READ wifiName NOTIFY changed)      // SSID

public:
    explicit SystemStatus(QObject *parent = nullptr);

    int battery() const { return m_battery; }
    bool charging() const { return m_charging; }
    bool wifiConnected() const { return m_wifiConnected; }
    int wifiStrength() const { return m_wifiStrength; }
    QString wifiName() const { return m_wifiName; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    void setWifiStrength(int strength);
    static int strengthFromDbm(double dbm);

    QTimer m_poll;
    QProcess *m_iw = nullptr;      // station dump: signal
    QProcess *m_iwLink = nullptr;  // link: SSID
    QString m_batteryDir;
    QStringList m_wifiIfaces;  // e.g. wlan0, but also p2p and NAN interfaces
    QString m_wifiIface;       // the one that is up
    int m_battery = -1;
    bool m_charging = false;
    bool m_wifiConnected = false;
    int m_wifiStrength = 0;
    QString m_wifiName;
};
