// Parsing wpa_supplicant control interface replies (the same text wpa_cli prints).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

namespace wpa {

struct ScanResult {
    QString ssid;
    int signalDbm = -100;
    int frequencyMhz = 0;
    bool secured = false;
};

struct KnownNetwork {
    int id = -1;
    QString ssid;
    bool current = false;
    bool disabled = false;
};

// STATUS: key=value lines.
QHash<QString, QString> parseStatus(const QByteArray &reply);
// SCAN_RESULTS: one entry per SSID (its strongest BSS), strongest first; hidden ones left out.
QList<ScanResult> parseScanResults(const QByteArray &reply);
// LIST_NETWORKS
QList<KnownNetwork> parseNetworks(const QByteArray &reply);

// wpa_supplicant prints SSIDs with printf-style escapes (\xNN, \\, \"). -> UTF-8 text.
QString decodeSsid(const QByteArray &escaped);
// An SSID as SET_NETWORK takes it without any quoting problems: hex.
QByteArray ssidHex(const QString &ssid);

// 0..4 bars from dBm.
int bars(int dbm);

}  // namespace wpa
