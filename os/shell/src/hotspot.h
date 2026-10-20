// The hotspot (ADR 0039): the phone's internet shared over Wi-Fi, through the root helper
// /usr/lib/mobarchy/hotspot (sudo, no PIN: sudoers.d/zz-mobarchy-hotspot).
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>

class Hotspot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool on READ on NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString password READ password NOTIFY changed)
    Q_PROPERTY(int clients READ clients NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // follow it (who has joined) while shown
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)
    // a QR code to join (a picture file; "" until made): WIFI:T:WPA;S:<name>;P:<password>;;
    Q_PROPERTY(QString qr READ qr NOTIFY changed)

public:
    explicit Hotspot(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool on() const { return m_on; }
    QString name() const { return m_name; }
    QString password() const { return m_password; }
    int clients() const { return m_clients; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString qr() const { return m_qr; }
    bool watching() const { return m_watch.isActive(); }
    void setWatching(bool on);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setOn(bool on);
    Q_INVOKABLE void configure(const QString &name, const QString &password);

signals:
    void changed();
    void watchingChanged();

private:
    void run(const QStringList &args);
    void take(const QJsonObject &o);
    void makeQr();

    bool m_available = false, m_on = false, m_busy = false;
    QString m_name, m_password, m_error, m_qr;
    int m_qrSerial = 0;
    int m_clients = 0;
    QTimer m_watch;
};
