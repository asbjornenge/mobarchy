// Bluetooth through BlueZ on the system bus (org.bluez): the first adapter, its devices,
// discovery, pairing, connecting. Switching the radio on and off is rfkill's (Rfkill), so
// a device's bring-up (power, firmware) follows the same switch as flight mode.
// Pairing goes through the shell's own agent: "just works" pairing (headphones, speakers);
// devices that need a passkey shown or typed are not supported yet.
#pragma once

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QDBusPendingCallWatcher;

class Bluetooth : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // bluetoothd runs and there is an adapter.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    // [{path, address, name, icon, paired, connected, busy, battery (-1: unknown)}]:
    // connected first, then paired, then the rest by signal.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    // Names of the connected devices, for the tile.
    Q_PROPERTY(QStringList connectedNames READ connectedNames NOTIFY changed)

public:
    explicit Bluetooth(QObject *parent = nullptr);

    bool available() const { return !m_adapter.isEmpty(); }
    bool powered() const;
    bool discovering() const;
    QVariantList devices() const { return m_devices; }
    QStringList connectedNames() const;

    // Look for devices while a page shows them.
    Q_INVOKABLE void setDiscovery(bool on);
    // A paired device: connect or disconnect. Not paired: pair, trust, connect.
    Q_INVOKABLE void activate(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    Q_INVOKABLE void forget(const QString &path);

    // Called by the agent (org.bluez.Agent1).
    void cancelPairing();

signals:
    void changed();
    void failed(const QString &name, const QString &reason);

private slots:
    void interfacesAdded(const QDBusObjectPath &path, const QMap<QString, QVariantMap> &interfaces);
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated, const QDBusMessage &message);
    void serviceUp();
    void serviceDown();

private:
    void load();
    void registerAgent();
    void rebuild();
    void call(const QString &path, const QString &interface, const QString &method,
              const QString &what, const QVariantList &args = {});
    QString nameOf(const QString &path) const;

    // object path -> interface -> properties
    QHash<QString, QHash<QString, QVariantMap>> m_objects;
    QString m_adapter;
    QSet<QString> m_busy;  // devices being paired or connected
    QVariantList m_devices;
    bool m_wantDiscovery = false;
};
