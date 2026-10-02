#include "../src/icontheme.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestIconTheme : public QObject
{
    Q_OBJECT

    QTemporaryDir tmp;
    QString base, pixmaps;

    void write(const QString &rel, const QByteArray &data = "x")
    {
        const QString path = base + u'/' + rel;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

private slots:
    void initTestCase()
    {
        base = tmp.path() + QStringLiteral("/icons");
        pixmaps = tmp.path() + QStringLiteral("/pixmaps");
        write("Mine/index.theme", "[Icon Theme]\nName=Mine\nInherits=Parent\nDirectories=48x48/apps,scalable/apps\n\n"
                                  "[48x48/apps]\nSize=48\nType=Fixed\n\n"
                                  "[scalable/apps]\nSize=128\nMinSize=8\nMaxSize=512\nType=Scalable\n");
        write("Mine/48x48/apps/fixed.png");
        write("Mine/scalable/apps/both.svg");
        write("Mine/48x48/apps/both.png");
        write("Parent/index.theme", "[Icon Theme]\nName=Parent\nDirectories=32x32/apps,64x64/apps\n\n"
                                    "[32x32/apps]\nSize=32\n\n[64x64/apps]\nSize=64\n");
        write("Parent/32x32/apps/inherited.png");
        write("Parent/64x64/apps/inherited.png");
        write("hicolor/index.theme", "[Icon Theme]\nName=Hicolor\nDirectories=256x256/apps\n\n[256x256/apps]\nSize=256\n");
        write("hicolor/256x256/apps/firefox.png");
        QDir().mkpath(pixmaps);
        QFile f(pixmaps + QStringLiteral("/old.xpm"));
        QVERIFY(f.open(QIODevice::WriteOnly));
    }

    void lookup()
    {
        IconTheme t(QStringLiteral("Mine"), {base}, {pixmaps});
        QVERIFY(t.lookup("fixed", 48).endsWith("Mine/48x48/apps/fixed.png"));
        // Only a 48px file: still the best there is for 100px.
        QVERIFY(t.lookup("fixed", 100).endsWith("Mine/48x48/apps/fixed.png"));
        // Both fit 48px: the scalable one wins the tie; only scalable fits 100px.
        QVERIFY(t.lookup("both", 48).endsWith("Mine/scalable/apps/both.svg"));
        QVERIFY(t.lookup("both", 100).endsWith("Mine/scalable/apps/both.svg"));
        // Closest size in an inherited theme.
        QVERIFY(t.lookup("inherited", 60).endsWith("Parent/64x64/apps/inherited.png"));
        QVERIFY(t.lookup("inherited", 30).endsWith("Parent/32x32/apps/inherited.png"));
        // hicolor is always searched, then the pixmaps directory.
        QVERIFY(t.lookup("firefox", 64).endsWith("hicolor/256x256/apps/firefox.png"));
        QVERIFY(t.lookup("old", 64).endsWith("pixmaps/old.xpm"));
        QVERIFY(t.lookup("missing", 64).isEmpty());
        QCOMPARE(t.lookup(base + "/Mine/48x48/apps/fixed.png", 64), base + "/Mine/48x48/apps/fixed.png");
    }
};

QTEST_GUILESS_MAIN(TestIconTheme)
#include "tst_icontheme.moc"
