// Radios on and off through /dev/rfkill (the active session's user gets access through
// logind/udev uaccess): flight mode and the Wi-Fi switch. Generic Linux, no driver knowledge.
#pragma once

#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QtQml/qqmlregistration.h>

class Rfkill : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available CONSTANT)
    // Every radio is blocked.
    Q_PROPERTY(bool flightMode READ flightMode WRITE setFlightMode NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled WRITE setWifiEnabled NOTIFY changed)
    Q_PROPERTY(bool hasWifi READ hasWifi NOTIFY changed)

public:
    explicit Rfkill(QObject *parent = nullptr);
    ~Rfkill() override;

    bool available() const { return m_fd >= 0; }
    bool flightMode() const;
    void setFlightMode(bool on);
    bool wifiEnabled() const;
    void setWifiEnabled(bool on);
    bool hasWifi() const;

signals:
    void changed();

private:
    struct Radio {
        quint8 type = 0;
        bool soft = false;
        bool hard = false;
    };
    void readEvents();
    void changeAll(quint8 type, bool block);
    void change(quint32 idx, bool block);

    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QHash<quint32, Radio> m_radios;  // by rfkill index
    // Radios that were on when flight mode started: only those come back on after it.
    QList<quint32> m_beforeFlight;
};
