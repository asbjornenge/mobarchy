#include "wifi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

bool fillAddress(sockaddr_un &addr, const QString &path)
{
    const QByteArray p = QFile::encodeName(path);
    if (p.size() >= qsizetype(sizeof(addr.sun_path)))
        return false;
    addr = {};
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, p.constData(), p.size());
    return true;
}

}  // namespace

// --- WpaControl --------------------------------------------------------------------------

WpaControl::WpaControl(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        close();
        emit lost();
    });
}

WpaControl::~WpaControl()
{
    close();
}

bool WpaControl::open(const QString &path)
{
    close();
    static int counter = 0;
    // The reply address must be somewhere wpa_supplicant (root) can send to: our runtime dir.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    m_local = QStringLiteral("%1/mobarchy-wpa-%2-%3").arg(dir).arg(getpid()).arg(++counter);
    sockaddr_un local, remote;
    if (!fillAddress(local, m_local) || !fillAddress(remote, path))
        return false;
    m_fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (m_fd < 0)
        return false;
    ::unlink(local.sun_path);
    if (::bind(m_fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0
        || ::connect(m_fd, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) < 0) {
        close();
        return false;
    }
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &WpaControl::readable);
    return true;
}

void WpaControl::close()
{
    m_timeout.stop();
    delete m_notifier;
    m_notifier = nullptr;
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
        ::unlink(QFile::encodeName(m_local).constData());
    }
    m_queue.clear();
    m_waiting = false;
}

void WpaControl::request(const QByteArray &command, Reply reply)
{
    if (m_fd < 0)
        return;
    m_queue.enqueue({command, std::move(reply)});
    if (!m_waiting)
        sendNext();
}

void WpaControl::sendNext()
{
    if (m_queue.isEmpty() || m_fd < 0)
        return;
    const QByteArray &cmd = m_queue.head().command;
    if (::send(m_fd, cmd.constData(), cmd.size(), 0) < 0) {
        close();
        emit lost();
        return;
    }
    m_waiting = true;
    m_timeout.start();
}

void WpaControl::readable()
{
    char buf[8192];
    for (;;) {
        const ssize_t n = ::recv(m_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0)
            return;
        const QByteArray msg(buf, n);
        if (msg.startsWith('<')) {  // "<level>EVENT ..."
            const qsizetype end = msg.indexOf('>');
            emit event(msg.mid(end + 1));
            if (m_fd < 0)
                return;
            continue;
        }
        if (!m_waiting || m_queue.isEmpty())
            continue;
        m_timeout.stop();
        m_waiting = false;
        const Pending p = m_queue.dequeue();
        if (p.reply)
            p.reply(msg);
        if (m_fd < 0)
            return;
        sendNext();
    }
}

// --- Wifi --------------------------------------------------------------------------------

Wifi::Wifi(QObject *parent)
    : QObject(parent)
{
    connect(&m_ctrl, &WpaControl::event, this, &Wifi::onEvent);
    connect(&m_ctrl, &WpaControl::lost, this, [this] {
        m_reconnect.start();
        emit changed();
    });
    m_reconnect.setInterval(5000);
    connect(&m_reconnect, &QTimer::timeout, this, &Wifi::connectControl);
    connectControl();
}

void Wifi::connectControl()
{
    // The station interface's socket (not p2p-dev-*).
    const QDir dir(QStringLiteral("/run/wpa_supplicant"));
    for (const QString &name : dir.entryList(QDir::System)) {
        if (name.startsWith(QStringLiteral("p2p-")) || !m_ctrl.open(dir.filePath(name)))
            continue;
        m_reconnect.stop();
        m_ctrl.request("ATTACH");
        refreshStatus();
        refreshNetworks();
        m_ctrl.request("SCAN_RESULTS", [this](const QByteArray &r) {
            m_scan = wpa::parseScanResults(r);
            rebuild();
        });
        emit changed();
        return;
    }
    m_reconnect.start();
}

QString Wifi::ssid() const
{
    return state() == u"COMPLETED" ? m_status.value(QStringLiteral("ssid")) : QString();
}

void Wifi::refreshStatus()
{
    m_ctrl.request("STATUS", [this](const QByteArray &r) {
        m_status = wpa::parseStatus(r);
        rebuild();
    });
}

void Wifi::refreshNetworks()
{
    m_ctrl.request("LIST_NETWORKS", [this](const QByteArray &r) {
        m_known = wpa::parseNetworks(r);
        rebuild();
    });
}

void Wifi::onEvent(const QByteArray &message)
{
    if (message.startsWith("CTRL-EVENT-SCAN-RESULTS")) {
        m_ctrl.request("SCAN_RESULTS", [this](const QByteArray &r) {
            m_scan = wpa::parseScanResults(r);
            m_scanning = false;
            rebuild();
        });
    } else if (message.startsWith("CTRL-EVENT-SCAN-FAILED")) {
        m_scanning = false;
        emit changed();
    } else if (message.startsWith("CTRL-EVENT-CONNECTED")) {
        if (m_added >= 0) {  // a new network worked: keep it
            m_ctrl.request("SAVE_CONFIG");
            m_added = -1;
        }
        m_connecting.clear();
        refreshStatus();
        refreshNetworks();
    } else if (message.startsWith("CTRL-EVENT-SSID-TEMP-DISABLED")) {
        // "id=1 ssid=\"home\" auth_failures=1 duration=10 reason=WRONG_KEY"
        if (message.contains("reason=WRONG_KEY")) {
            const QString failed = m_connecting;
            if (m_added >= 0) {  // don't keep a network with a wrong passphrase
                m_ctrl.request("REMOVE_NETWORK " + QByteArray::number(m_added));
                m_ctrl.request("ENABLE_NETWORK all");
                m_added = -1;
            }
            m_connecting.clear();
            refreshNetworks();
            emit joinFailed(failed, tr("Wrong password"));
        }
    } else if (message.startsWith("CTRL-EVENT-DISCONNECTED") || message.startsWith("CTRL-EVENT-STATE-CHANGE")
               || message.startsWith("CTRL-EVENT-NETWORK-")) {
        refreshStatus();
        refreshNetworks();
    } else if (message.startsWith("CTRL-EVENT-TERMINATING")) {
        m_ctrl.close();
        m_reconnect.start();
        emit changed();
    }
}

void Wifi::rebuild()
{
    const QString current = ssid();
    QVariantList list;
    for (const wpa::ScanResult &r : std::as_const(m_scan)) {
        list << QVariantMap{
            {QStringLiteral("ssid"), r.ssid},
            {QStringLiteral("bars"), wpa::bars(r.signalDbm)},
            {QStringLiteral("secured"), r.secured},
            {QStringLiteral("known"), knownId(r.ssid) >= 0},
            {QStringLiteral("connected"), r.ssid == current},
            {QStringLiteral("frequency"), r.frequencyMhz},
        };
    }
    // The connected network first, then by signal.
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("connected")).toBool()
            && !b.toMap().value(QStringLiteral("connected")).toBool();
    });
    m_networks = list;
    emit changed();
}

int Wifi::knownId(const QString &ssid) const
{
    for (const wpa::KnownNetwork &n : m_known) {
        if (n.ssid == ssid)
            return n.id;
    }
    return -1;
}

void Wifi::scan()
{
    if (!m_ctrl.isOpen())
        return;
    m_scanning = true;
    emit changed();
    m_ctrl.request("SCAN", [this](const QByteArray &r) {
        // FAIL-BUSY: a scan is already running; its results come all the same.
        if (!r.startsWith("OK") && !r.startsWith("FAIL-BUSY")) {
            m_scanning = false;
            emit changed();
        }
    });
}

bool Wifi::join(const QString &ssid)
{
    const int id = knownId(ssid);
    if (id >= 0) {
        m_connecting = ssid;
        emit changed();
        // select_network disables the others for now; enable them again so they stay
        // candidates for later (it does not touch the current choice).
        m_ctrl.request("SELECT_NETWORK " + QByteArray::number(id));
        m_ctrl.request("ENABLE_NETWORK all");
        return true;
    }
    const auto it = std::find_if(m_scan.cbegin(), m_scan.cend(),
                                 [&](const wpa::ScanResult &r) { return r.ssid == ssid; });
    if (it != m_scan.cend() && it->secured)
        return false;
    addNetwork(ssid, QString());
    return true;
}

void Wifi::joinWithPassphrase(const QString &ssid, const QString &passphrase)
{
    if (!validPassphrase(passphrase)) {
        emit joinFailed(ssid, tr("A Wi-Fi password has 8 to 63 characters"));
        return;
    }
    addNetwork(ssid, passphrase);
}

void Wifi::addNetwork(const QString &ssid, const QString &passphrase)
{
    m_connecting = ssid;
    emit changed();
    m_ctrl.request("ADD_NETWORK", [this, ssid, passphrase](const QByteArray &r) {
        bool ok;
        const int id = r.trimmed().toInt(&ok);
        if (!ok) {
            m_connecting.clear();
            emit joinFailed(ssid, tr("wpa_supplicant refused the network"));
            return;
        }
        m_added = id;
        const QByteArray set = "SET_NETWORK " + QByteArray::number(id) + ' ';
        m_ctrl.request(set + "ssid " + wpa::ssidHex(ssid));
        if (passphrase.isEmpty()) {
            m_ctrl.request(set + "key_mgmt NONE");
        } else {
            // WPA2 and WPA3 (SAE), with management frame protection where offered.
            m_ctrl.request(set + "key_mgmt WPA-PSK WPA-PSK-SHA256 SAE");
            m_ctrl.request(set + "ieee80211w 1");
            m_ctrl.request(set + "psk \"" + passphrase.toUtf8() + '"');
        }
        m_ctrl.request("SELECT_NETWORK " + QByteArray::number(id));
        m_ctrl.request("ENABLE_NETWORK all");
        refreshNetworks();
    });
}

void Wifi::forget(const QString &ssid)
{
    const int id = knownId(ssid);
    if (id < 0)
        return;
    m_ctrl.request("REMOVE_NETWORK " + QByteArray::number(id));
    m_ctrl.request("SAVE_CONFIG");
    refreshNetworks();
    refreshStatus();
}

bool Wifi::validPassphrase(const QString &passphrase)
{
    if (passphrase.size() < 8 || passphrase.size() > 63)
        return false;
    return std::all_of(passphrase.cbegin(), passphrase.cend(),
                       [](QChar c) { return c.unicode() >= 0x20 && c.unicode() < 0x7f; });
}
