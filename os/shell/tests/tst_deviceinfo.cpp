#include "../src/deviceinfo.h"

#include <QTest>

class TestDeviceInfo : public QObject
{
    Q_OBJECT

private slots:
    void comet()
    {
        const auto d = deviceinfo::parse(R"(# Google Pixel 9 Pro Fold.
[device]
codename = "comet"
name = "Google Pixel 9 Pro Fold"  # with a comment
memory = "16G"

[[display]]
id = "outer"
connector = "DSI-2"
width = 1080
top_inset = 32

[[display]]
id = "inner"
connector = "DSI-1"
width = 2152
scale = 2.0
side_inset = 12

[input]
fold_sensor = true
)");
        QCOMPARE(d.tables.value("device").toMap().value("codename").toString(), QStringLiteral("comet"));
        QCOMPARE(d.tables.value("device").toMap().value("name").toString(), QStringLiteral("Google Pixel 9 Pro Fold"));
        QCOMPARE(d.tables.value("input").toMap().value("fold_sensor").toBool(), true);
        QCOMPARE(d.displays.size(), 2);
        QCOMPARE(d.displays[0].value("width").toInt(), 1080);
        QCOMPARE(d.displays[1].value("scale").toDouble(), 2.0);
        QCOMPARE(deviceinfo::topInset(d, "DSI-2"), 32);
        QCOMPARE(deviceinfo::topInset(d, "DSI-1"), 0);
        QCOMPARE(deviceinfo::topInset(d, "HDMI-A-1"), 0);
        QCOMPARE(deviceinfo::sideInset(d, "DSI-1"), 12);
        QCOMPARE(deviceinfo::sideInset(d, "DSI-2"), 0);
    }

    void singleDisplayWithoutConnector()
    {
        const auto d = deviceinfo::parse("[[display]]\nid = \"main\"\ntop_inset = 20\n");
        QCOMPARE(deviceinfo::topInset(d, "Virtual-1"), 20);
    }

    void hashInString()
    {
        const auto d = deviceinfo::parse("[device]\nname = \"Phone #1\" # comment\n");
        QCOMPARE(d.tables.value("device").toMap().value("name").toString(), QStringLiteral("Phone #1"));
    }
};

QTEST_GUILESS_MAIN(TestDeviceInfo)
#include "tst_deviceinfo.moc"
