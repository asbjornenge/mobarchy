// Wi-Fi through wpa_supplicant's control socket (/run/wpa_supplicant/<iface>, group wheel):
// the networks in range, connecting, forgetting. Asynchronous throughout; credentials go
// straight to wpa_supplicant and are never logged.
#pragma once

#include <QObject>
#include <QQueue>
#include <QSocketNotifier>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "wpaparse.h"

// One control-interface connection: requests are answered in order; with ATTACH, events
// ("<3>CTRL-EVENT-...") arrive on the same socket.
class WpaControl : public QObject
{
    Q_OBJECT

public:
    using Reply = std::function<void(const QByteArray &)>;

    explicit WpaControl(QObject *parent = nullptr);
    ~WpaControl() override;

    bool open(const QString &path);
    void close();
    bool isOpen() const { return m_fd >= 0; }
    void request(const QByteArray &command, Reply reply = {});

signals:
    void event(const QByteArray &message);
    void lost();  // no answer: wpa_supplicant went away

private:
    void readable();
    void sendNext();

    struct Pending {
        QByteArray command;
        Reply reply;
    };
    int m_fd = -1;
    QString m_local;
    QSocketNotifier *m_notifier = nullptr;
    QQueue<Pending> m_queue;
    bool m_waiting = false;
    QTimer m_timeout;
};

class Wifi : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)        // wpa_state, e.g. COMPLETED
    Q_PROPERTY(QString ssid READ ssid NOTIFY changed)          // connected network
    Q_PROPERTY(QString connecting READ connecting NOTIFY changed)  // network being joined
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    // [{ssid, bars (1..4), secured, known, connected, frequency}], strongest first
    Q_PROPERTY(QVariantList networks READ networks NOTIFY changed)

public:
    explicit Wifi(QObject *parent = nullptr);

    bool available() const { return m_ctrl.isOpen(); }
    QString state() const { return m_status.value(QStringLiteral("wpa_state")); }
    QString ssid() const;
    QString connecting() const { return m_connecting; }
    bool scanning() const { return m_scanning; }
    QVariantList networks() const { return m_networks; }

    Q_INVOKABLE void scan();
    // A saved or open network. Returns false if it needs a passphrase first.
    Q_INVOKABLE bool join(const QString &ssid);
    Q_INVOKABLE void joinWithPassphrase(const QString &ssid, const QString &passphrase);
    Q_INVOKABLE void forget(const QString &ssid);
    // WPA passphrases are 8 to 63 printable ASCII characters.
    Q_INVOKABLE static bool validPassphrase(const QString &passphrase);

signals:
    void changed();
    void joinFailed(const QString &ssid, const QString &reason);

private:
    void connectControl();
    void onEvent(const QByteArray &message);
    void refreshStatus();
    void refreshNetworks();
    void rebuild();
    void addNetwork(const QString &ssid, const QString &passphrase);
    int knownId(const QString &ssid) const;

    WpaControl m_ctrl;
    QTimer m_reconnect;
    QHash<QString, QString> m_status;
    QList<wpa::ScanResult> m_scan;
    QList<wpa::KnownNetwork> m_known;
    QVariantList m_networks;
    QString m_connecting;
    int m_added = -1;  // a network we added and have not seen connect yet
    bool m_scanning = false;
};
