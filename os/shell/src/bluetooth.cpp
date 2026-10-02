#include "bluetooth.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDebug>

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ObjectMap)

namespace {

const QString Service = QStringLiteral("org.bluez");
const QString AdapterIface = QStringLiteral("org.bluez.Adapter1");
const QString DeviceIface = QStringLiteral("org.bluez.Device1");
const QString BatteryIface = QStringLiteral("org.bluez.Battery1");
const QString PropertiesIface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString AgentPath = QStringLiteral("/org/mobarchy/bluetooth/agent");

QDBusConnection bus() { return QDBusConnection::systemBus(); }

}  // namespace

// The pairing agent BlueZ calls during pairing. It registers as NoInputNoOutput, so pairing
// is "just works"; it accepts what the owner started from the shell, and services of
// devices that are already paired.
class BluetoothAgent : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    BluetoothAgent(Bluetooth *bt, const QSet<QString> *busy) : QObject(bt), m_bt(bt), m_busy(busy) {}

    bool started(const QDBusObjectPath &device) const { return m_busy->contains(device.path()); }
    void reject(const QString &why)
    {
        sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), why);
    }

    Bluetooth *m_bt;
    const QSet<QString> *m_busy;
};

class BluetoothAgentAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    explicit BluetoothAgentAdaptor(BluetoothAgent *agent) : QDBusAbstractAdaptor(agent), m_a(agent) {}

public slots:
    void Release() {}
    QString RequestPinCode(const QDBusObjectPath &) { m_a->reject(QStringLiteral("PIN codes are not supported yet")); return {}; }
    void DisplayPinCode(const QDBusObjectPath &, const QString &) { m_a->reject(QStringLiteral("not supported yet")); }
    uint RequestPasskey(const QDBusObjectPath &) { m_a->reject(QStringLiteral("passkeys are not supported yet")); return 0; }
    void DisplayPasskey(const QDBusObjectPath &, uint, ushort) {}
    void RequestConfirmation(const QDBusObjectPath &device, uint)
    {
        if (!m_a->started(device))
            m_a->reject(QStringLiteral("pairing not started on this phone"));
    }
    void RequestAuthorization(const QDBusObjectPath &device)
    {
        if (!m_a->started(device))
            m_a->reject(QStringLiteral("pairing not started on this phone"));
    }
    void AuthorizeService(const QDBusObjectPath &, const QString &) {}  // paired devices only reach this
    void Cancel() { m_a->m_bt->cancelPairing(); }

private:
    BluetoothAgent *m_a;
};

Bluetooth::Bluetooth(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ObjectMap>();

    auto *agent = new BluetoothAgent(this, &m_busy);
    new BluetoothAgentAdaptor(agent);
    if (!bus().registerObject(AgentPath, agent))
        qWarning() << "bluetooth: cannot register the pairing agent object";

    auto *watcher = new QDBusServiceWatcher(Service, bus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Bluetooth::serviceUp);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &Bluetooth::serviceDown);

    bus().connect(Service, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                  QStringLiteral("InterfacesAdded"), this,
                  SLOT(interfacesAdded(QDBusObjectPath, QMap<QString, QVariantMap>)));
    bus().connect(Service, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                  QStringLiteral("InterfacesRemoved"), this, SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
    // Any object's properties: adapters and devices.
    bus().connect(Service, QString(), PropertiesIface, QStringLiteral("PropertiesChanged"), this,
                  SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    load();
}

void Bluetooth::serviceUp()
{
    load();
}

void Bluetooth::serviceDown()
{
    m_objects.clear();
    m_adapter.clear();
    m_busy.clear();
    rebuild();
}

void Bluetooth::load()
{
    auto msg = QDBusMessage::createMethodCall(Service, QStringLiteral("/"),
                                              QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                              QStringLiteral("GetManagedObjects"));
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<ObjectMap> reply = *w;
        if (reply.isError())
            return;  // bluetoothd not running: the watcher brings us back
        m_objects.clear();
        const ObjectMap objects = reply.value();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            for (auto i = it.value().cbegin(); i != it.value().cend(); ++i)
                m_objects[it.key().path()][i.key()] = i.value();
        }
        const bool hadAdapter = !m_adapter.isEmpty();
        m_adapter.clear();
        for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
            if (it->contains(AdapterIface) && (m_adapter.isEmpty() || it.key() < m_adapter))
                m_adapter = it.key();
        }
        if (!m_adapter.isEmpty() && !hadAdapter)
            registerAgent();
        if (m_wantDiscovery)
            setDiscovery(true);
        rebuild();
    });
}

void Bluetooth::registerAgent()
{
    auto msg = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez"),
                                              QStringLiteral("org.bluez.AgentManager1"),
                                              QStringLiteral("RegisterAgent"));
    msg << QVariant::fromValue(QDBusObjectPath(AgentPath)) << QStringLiteral("NoInputNoOutput");
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError() && w->error().name() != QLatin1String("org.bluez.Error.AlreadyExists")) {
            qWarning() << "bluetooth: RegisterAgent:" << w->error().message();
            return;
        }
        auto def = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez"),
                                                  QStringLiteral("org.bluez.AgentManager1"),
                                                  QStringLiteral("RequestDefaultAgent"));
        def << QVariant::fromValue(QDBusObjectPath(AgentPath));
        bus().asyncCall(def);
    });
}

void Bluetooth::interfacesAdded(const QDBusObjectPath &path, const QMap<QString, QVariantMap> &interfaces)
{
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it)
        m_objects[path.path()][it.key()] = it.value();
    if (interfaces.contains(AdapterIface) && m_adapter.isEmpty()) {
        m_adapter = path.path();
        registerAgent();
        if (m_wantDiscovery)
            setDiscovery(true);
    }
    rebuild();
}

void Bluetooth::interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    auto it = m_objects.find(path.path());
    if (it == m_objects.end())
        return;
    for (const QString &i : interfaces)
        it->remove(i);
    if (it->isEmpty())
        m_objects.erase(it);
    if (path.path() == m_adapter && interfaces.contains(AdapterIface)) {
        m_adapter.clear();
        load();  // another adapter, if any
    }
    m_busy.remove(path.path());
    rebuild();
}

void Bluetooth::propertiesChanged(const QString &interface, const QVariantMap &changed,
                                  const QStringList &invalidated, const QDBusMessage &message)
{
    auto it = m_objects.find(message.path());
    if (it == m_objects.end() || !it->contains(interface))
        return;
    QVariantMap &props = (*it)[interface];
    for (auto c = changed.cbegin(); c != changed.cend(); ++c)
        props.insert(c.key(), c.value());
    for (const QString &k : invalidated)
        props.remove(k);
    if (interface == AdapterIface && message.path() == m_adapter && changed.contains(QStringLiteral("Powered"))
        && changed.value(QStringLiteral("Powered")).toBool() && m_wantDiscovery)
        setDiscovery(true);
    rebuild();
}

bool Bluetooth::powered() const
{
    return m_objects.value(m_adapter).value(AdapterIface).value(QStringLiteral("Powered")).toBool();
}

bool Bluetooth::discovering() const
{
    return m_objects.value(m_adapter).value(AdapterIface).value(QStringLiteral("Discovering")).toBool();
}

QStringList Bluetooth::connectedNames() const
{
    QStringList names;
    for (const QVariant &v : m_devices) {
        const QVariantMap d = v.toMap();
        if (d.value(QStringLiteral("connected")).toBool())
            names << d.value(QStringLiteral("name")).toString();
    }
    return names;
}

QString Bluetooth::nameOf(const QString &path) const
{
    const QVariantMap d = m_objects.value(path).value(DeviceIface);
    return d.value(QStringLiteral("Alias"), d.value(QStringLiteral("Address"))).toString();
}

void Bluetooth::rebuild()
{
    struct Entry {
        QVariantMap map;
        int rank;    // 0 connected, 1 paired, 2 other
        int signal;
    };
    QList<Entry> entries;
    const QString prefix = m_adapter + QLatin1Char('/');
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (m_adapter.isEmpty() || !it.key().startsWith(prefix) || !it->contains(DeviceIface))
            continue;
        const QVariantMap d = it->value(DeviceIface);
        const bool paired = d.value(QStringLiteral("Paired")).toBool();
        // Devices around that don't tell their name (beacons, phones' random addresses) are noise.
        if (!paired && !d.contains(QStringLiteral("Name")))
            continue;
        const bool connected = d.value(QStringLiteral("Connected")).toBool();
        const QVariantMap battery = it->value(BatteryIface);
        QVariantMap m;
        m[QStringLiteral("path")] = it.key();
        m[QStringLiteral("address")] = d.value(QStringLiteral("Address"));
        m[QStringLiteral("name")] = d.value(QStringLiteral("Alias"), d.value(QStringLiteral("Address")));
        m[QStringLiteral("icon")] = d.value(QStringLiteral("Icon"));
        m[QStringLiteral("paired")] = paired;
        m[QStringLiteral("connected")] = connected;
        m[QStringLiteral("busy")] = m_busy.contains(it.key());
        m[QStringLiteral("battery")] = battery.contains(QStringLiteral("Percentage"))
            ? battery.value(QStringLiteral("Percentage")).toInt() : -1;
        entries.append({m, connected ? 0 : paired ? 1 : 2,
                        d.value(QStringLiteral("RSSI"), -1000).toInt()});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (a.rank == 2 && a.signal != b.signal)
            return a.signal > b.signal;
        return a.map.value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.map.value(QStringLiteral("name")).toString()) < 0;
    });
    m_devices.clear();
    for (const Entry &e : std::as_const(entries))
        m_devices << e.map;
    emit changed();
}

void Bluetooth::setDiscovery(bool on)
{
    m_wantDiscovery = on;
    if (m_adapter.isEmpty() || (on && !powered()) || on == discovering())
        return;
    auto msg = QDBusMessage::createMethodCall(Service, m_adapter, AdapterIface,
                                              on ? QStringLiteral("StartDiscovery") : QStringLiteral("StopDiscovery"));
    bus().asyncCall(msg);  // InProgress / NotReady are fine: Discovering tells the truth
}

void Bluetooth::call(const QString &path, const QString &interface, const QString &method,
                     const QString &what, const QVariantList &args)
{
    auto msg = QDBusMessage::createMethodCall(Service, path, interface, method);
    msg.setArguments(args);
    // Pairing and connecting can take a while (the device may need to wake up).
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(msg, 60000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, path, method, what](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            m_busy.remove(path);
            rebuild();
            if (w->error().name() != QLatin1String("org.bluez.Error.AuthenticationCanceled"))
                emit failed(nameOf(path), what + QStringLiteral(": ") + w->error().message());
            return;
        }
        if (method == QLatin1String("Pair")) {
            // Trusted: it may connect by itself next time, without asking.
            auto trust = QDBusMessage::createMethodCall(Service, path, PropertiesIface, QStringLiteral("Set"));
            trust << DeviceIface << QStringLiteral("Trusted") << QVariant::fromValue(QDBusVariant(true));
            bus().asyncCall(trust);
            call(path, DeviceIface, QStringLiteral("Connect"), tr("Connecting failed"));
            return;
        }
        m_busy.remove(path);
        rebuild();
    });
}

void Bluetooth::activate(const QString &path)
{
    const QVariantMap d = m_objects.value(path).value(DeviceIface);
    if (d.isEmpty() || m_busy.contains(path))
        return;
    if (d.value(QStringLiteral("Connected")).toBool()) {
        disconnectDevice(path);
        return;
    }
    m_busy.insert(path);
    rebuild();
    if (d.value(QStringLiteral("Paired")).toBool())
        call(path, DeviceIface, QStringLiteral("Connect"), tr("Connecting failed"));
    else
        call(path, DeviceIface, QStringLiteral("Pair"), tr("Pairing failed"));
}

void Bluetooth::disconnectDevice(const QString &path)
{
    call(path, DeviceIface, QStringLiteral("Disconnect"), tr("Disconnecting failed"));
}

void Bluetooth::forget(const QString &path)
{
    if (m_adapter.isEmpty())
        return;
    call(m_adapter, AdapterIface, QStringLiteral("RemoveDevice"), tr("Forgetting failed"),
         {QVariant::fromValue(QDBusObjectPath(path))});
}

void Bluetooth::cancelPairing()
{
    m_busy.clear();
    rebuild();
}

#include "bluetooth.moc"
