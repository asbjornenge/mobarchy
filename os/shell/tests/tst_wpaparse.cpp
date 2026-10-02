#include "../src/wpaparse.h"

#include <QTest>

class TestWpaParse : public QObject
{
    Q_OBJECT

private slots:
    void status()
    {
        const auto s = wpa::parseStatus("bssid=cc:f4:11:82:0b:c9\nfreq=2412\nssid=caf\\xc3\\xa9 net\n"
                                        "id=0\nmode=station\nwpa_state=COMPLETED\nip_address=192.168.86.113\n");
        QCOMPARE(s.value("wpa_state"), QStringLiteral("COMPLETED"));
        QCOMPARE(s.value("ssid"), QStringLiteral("café net"));
        QCOMPARE(s.value("freq"), QStringLiteral("2412"));
    }

    void scanResults()
    {
        const auto r = wpa::parseScanResults(
            "bssid / frequency / signal level / flags / ssid\n"
            "aa:aa:aa:aa:aa:01\t2412\t-70\t[WPA2-PSK-CCMP][ESS]\thome\n"
            "aa:aa:aa:aa:aa:02\t5180\t-50\t[WPA2-PSK-CCMP][ESS]\thome\n"
            "aa:aa:aa:aa:aa:03\t2437\t-60\t[ESS]\tCafe \\\"Open\\\"\n"
            "aa:aa:aa:aa:aa:04\t2437\t-40\t[WPA2-PSK-CCMP][ESS]\t\n"
            "aa:aa:aa:aa:aa:05\t2437\t-80\t[RSN-SAE-CCMP][ESS]\t\\x00\\x00\n");
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].ssid, QStringLiteral("home"));
        QCOMPARE(r[0].signalDbm, -50);
        QCOMPARE(r[0].frequencyMhz, 5180);
        QVERIFY(r[0].secured);
        QCOMPARE(r[1].ssid, QStringLiteral("Cafe \"Open\""));
        QVERIFY(!r[1].secured);
    }

    void networks()
    {
        const auto n = wpa::parseNetworks("network id / ssid / bssid / flags\n"
                                          "0\thome\tany\t[CURRENT]\n1\twork\tany\t[DISABLED]\n2\tcafe\tany\t\n");
        QCOMPARE(n.size(), 3);
        QCOMPARE(n[0].id, 0);
        QVERIFY(n[0].current);
        QVERIFY(n[1].disabled);
        QCOMPARE(n[2].ssid, QStringLiteral("cafe"));
    }

    void hex()
    {
        QCOMPARE(wpa::ssidHex("hi"), QByteArray("6869"));
        QCOMPARE(wpa::decodeSsid("\\x"), QStringLiteral("\\x"));
    }

    void bars()
    {
        QCOMPARE(wpa::bars(-50), 4);
        QCOMPARE(wpa::bars(-68), 2);
        QCOMPARE(wpa::bars(-90), 1);
    }
};

QTEST_GUILESS_MAIN(TestWpaParse)
#include "tst_wpaparse.moc"
