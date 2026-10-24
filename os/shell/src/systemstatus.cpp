#include "systemstatus.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace {

QString readLine(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readLine()).trimmed() : QString();
}

}  // namespace

SystemStatus::SystemStatus(QObject *parent)
    : QObject(parent)
{
    const QDir supplies(QStringLiteral("/sys/class/power_supply"));
    for (const QString &name : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (readLine(supplies.filePath(name + QStringLiteral("/type"))) == u"Battery") {
            m_batteryDir = supplies.filePath(name);
            break;
        }
    }
    const QDir nets(QStringLiteral("/sys/class/net"));
    for (const QString &name : nets.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFile::exists(nets.filePath(name + QStringLiteral("/wireless"))))
            m_wifiIfaces << name;
    }
    m_iw = new QProcess(this);
    connect(m_iw, &QProcess::finished, this, [this] {
        // "\tsignal:  \t-68 dBm"; in managed mode the only station is the access point
        const QString out = QString::fromLatin1(m_iw->readAllStandardOutput());
        static const QRegularExpression signal(QStringLiteral("signal:\\s*(-?\\d+)"));
        const QRegularExpressionMatch m = signal.match(out);
        setWifiStrength(m.hasMatch() && m_wifiConnected ? strengthFromDbm(m.captured(1).toDouble())
                                                        : (m_wifiConnected ? -1 : 0));
    });
    connect(m_iw, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setWifiStrength(m_wifiConnected ? -1 : 0);
    });
    m_iwLink = new QProcess(this);
    connect(m_iwLink, &QProcess::finished, this, [this] {
        // "Connected to cc:f4:... (on wlan0)\n\tSSID: home\n..."
        static const QRegularExpression ssid(QStringLiteral("^\\s*SSID: (.*)$"),
                                             QRegularExpression::MultilineOption);
        const QRegularExpressionMatch m = ssid.match(QString::fromUtf8(m_iwLink->readAllStandardOutput()));
        const QString name = m_wifiConnected && m.hasMatch() ? m.captured(1).trimmed() : QString();
        if (name != m_wifiName) {
            m_wifiName = name;
            emit changed();
        }
    });
    m_poll.setInterval(10000);
    connect(&m_poll, &QTimer::timeout, this, &SystemStatus::refresh);
    m_poll.start();
    refresh();
}

void SystemStatus::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    if (active) {
        refresh();
        m_poll.start();
    } else {
        m_poll.stop();
    }
}

void SystemStatus::refresh()
{
    int battery = -1;
    bool charging = false;
    if (!m_batteryDir.isEmpty()) {
        bool ok;
        battery = readLine(m_batteryDir + QStringLiteral("/capacity")).toInt(&ok);
        if (!ok)
            battery = -1;
        const QString status = readLine(m_batteryDir + QStringLiteral("/status"));
        charging = status == u"Charging" || status == u"Full";
    }

    bool connected = false;
    int strength = 0;
    const auto up = std::find_if(m_wifiIfaces.cbegin(), m_wifiIfaces.cend(), [](const QString &i) {
        return readLine(QStringLiteral("/sys/class/net/%1/operstate").arg(i)) == u"up";
    });
    m_wifiIface = up != m_wifiIfaces.cend() ? *up : QString();
    if (!m_wifiIface.isEmpty()) {
        connected = true;
        strength = m_wifiStrength;
        // /proc/net/wireless: "wlan0: 0000   54.  -56.  -256 ...": link, level (dBm), noise
        bool found = false;
        QFile f(QStringLiteral("/proc/net/wireless"));
        if (f.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : f.readAll().split('\n')) {
                const QList<QByteArray> fields = line.simplified().split(' ');
                if (fields.size() >= 4 && fields[0] == m_wifiIface.toLatin1() + ':') {
                    strength = strengthFromDbm(QByteArray(fields[3]).replace('.', "").toDouble());
                    found = true;
                }
            }
        }
        // The SSID only changes with a new connection (which takes the link down first).
        if (m_wifiName.isEmpty() && m_iwLink->state() == QProcess::NotRunning)
            m_iwLink->start(QStringLiteral("iw"), {QStringLiteral("dev"), m_wifiIface, QStringLiteral("link")});
        if (!found && m_iw->state() == QProcess::NotRunning)
            m_iw->start(QStringLiteral("iw"), {QStringLiteral("dev"), m_wifiIface,
                                               QStringLiteral("station"), QStringLiteral("dump")});
    }

    if (battery != m_battery || charging != m_charging || connected != m_wifiConnected
        || strength != m_wifiStrength) {
        m_battery = battery;
        m_charging = charging;
        m_wifiConnected = connected;
        m_wifiStrength = strength;
        if (!connected)
            m_wifiName.clear();
        emit changed();
    }
}

int SystemStatus::strengthFromDbm(double dbm)
{
    return dbm >= -55 ? 4 : dbm >= -65 ? 3 : dbm >= -75 ? 2 : 1;
}

void SystemStatus::setWifiStrength(int strength)
{
    if (strength != m_wifiStrength) {
        m_wifiStrength = strength;
        emit changed();
    }
}
