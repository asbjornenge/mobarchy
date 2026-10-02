#include "wpaparse.h"

#include <algorithm>

namespace wpa {

QHash<QString, QString> parseStatus(const QByteArray &reply)
{
    QHash<QString, QString> out;
    for (const QByteArray &line : reply.split('\n')) {
        const qsizetype eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QString key = QString::fromUtf8(line.left(eq));
        const QByteArray value = line.mid(eq + 1).trimmed();
        out.insert(key, key == u"ssid" ? decodeSsid(value) : QString::fromUtf8(value));
    }
    return out;
}

QList<ScanResult> parseScanResults(const QByteArray &reply)
{
    // bssid / frequency / signal level / flags / ssid
    QHash<QString, ScanResult> best;
    const QList<QByteArray> lines = reply.split('\n');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines[i].split('\t');
        if (f.size() < 5 || f[4].isEmpty())
            continue;
        ScanResult r;
        r.ssid = decodeSsid(f[4]);
        if (r.ssid.isEmpty() || r.ssid.front() == QChar(0))
            continue;
        r.frequencyMhz = f[1].toInt();
        r.signalDbm = f[2].toInt();
        r.secured = f[3].contains("WPA") || f[3].contains("WEP") || f[3].contains("SAE");
        const auto it = best.constFind(r.ssid);
        if (it == best.cend() || r.signalDbm > it->signalDbm)
            best.insert(r.ssid, r);
    }
    QList<ScanResult> out = best.values();
    std::sort(out.begin(), out.end(), [](const ScanResult &a, const ScanResult &b) {
        return a.signalDbm != b.signalDbm ? a.signalDbm > b.signalDbm : a.ssid < b.ssid;
    });
    return out;
}

QList<KnownNetwork> parseNetworks(const QByteArray &reply)
{
    // network id / ssid / bssid / flags
    QList<KnownNetwork> out;
    const QList<QByteArray> lines = reply.split('\n');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines[i].split('\t');
        if (f.size() < 2)
            continue;
        bool ok;
        KnownNetwork n;
        n.id = f[0].toInt(&ok);
        if (!ok)
            continue;
        n.ssid = decodeSsid(f[1]);
        const QByteArray flags = f.size() > 3 ? f[3] : QByteArray();
        n.current = flags.contains("[CURRENT]");
        n.disabled = flags.contains("[DISABLED]");
        out << n;
    }
    return out;
}

QString decodeSsid(const QByteArray &escaped)
{
    QByteArray raw;
    raw.reserve(escaped.size());
    for (qsizetype i = 0; i < escaped.size(); ++i) {
        const char c = escaped[i];
        if (c != '\\' || i + 1 == escaped.size()) {
            raw += c;
            continue;
        }
        const char n = escaped[++i];
        switch (n) {
        case 'x':
            if (i + 2 < escaped.size()) {
                bool ok;
                const int v = escaped.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    raw += char(v);
                    i += 2;
                    break;
                }
            }
            raw += "\\x";
            break;
        case 'n': raw += '\n'; break;
        case 'r': raw += '\r'; break;
        case 't': raw += '\t'; break;
        case 'e': raw += '\033'; break;
        default: raw += n; break;  // \\ \" and the like
        }
    }
    return QString::fromUtf8(raw);
}

QByteArray ssidHex(const QString &ssid)
{
    return ssid.toUtf8().toHex();
}

int bars(int dbm)
{
    return dbm >= -55 ? 4 : dbm >= -65 ? 3 : dbm >= -75 ? 2 : 1;
}

}  // namespace wpa
