#include "../src/desktopentry.h"

#include <QTest>

using namespace desktopentry;

class TestDesktopEntry : public QObject
{
    Q_OBJECT

private:
    static DesktopEntry entry(const QByteArray &body, const QStringList &languages = {},
                              const QStringList &desktops = {})
    {
        return parse(QStringLiteral("x.desktop"), QStringLiteral("/a/x.desktop"),
                     "[Desktop Entry]\nType=Application\n" + body, languages, desktops);
    }

private slots:
    void basic()
    {
        const DesktopEntry e = entry("Name=Foot\nExec=foot\nIcon=foot\n");
        QCOMPARE(e.name, QStringLiteral("Foot"));
        QCOMPARE(e.icon, QStringLiteral("foot"));
        QVERIFY(e.visible);
        QVERIFY(!e.terminal);
    }

    void onlyMainGroup()
    {
        const DesktopEntry e = entry("Name=Firefox\nExec=firefox %u\n"
                                     "[Desktop Action new-window]\nName=New Window\nExec=firefox --new-window\n");
        QCOMPARE(e.name, QStringLiteral("Firefox"));
        QCOMPARE(e.exec, QStringLiteral("firefox %u"));
    }

    void localizedName()
    {
        const QByteArray body = "Name=Files\nName[nb]=Filer\nName[nb_NO]=Filer (NO)\nExec=x\n";
        QCOMPARE(entry(body, languagesFor("nb_NO.UTF-8")).name, QStringLiteral("Filer (NO)"));
        QCOMPARE(entry(body, languagesFor("nb_SE")).name, QStringLiteral("Filer"));
        QCOMPARE(entry(body, languagesFor("C")).name, QStringLiteral("Files"));
    }

    void languages()
    {
        QCOMPARE(languagesFor("sr_RS.UTF-8@latin"),
                 (QStringList{"sr_RS@latin", "sr_RS", "sr@latin", "sr"}));
        QCOMPARE(languagesFor("de"), QStringList{"de"});
        QVERIFY(languagesFor("POSIX").isEmpty());
    }

    void hidden()
    {
        QVERIFY(!entry("Name=A\nExec=a\nNoDisplay=true\n").visible);
        QVERIFY(!entry("Name=A\nExec=a\nHidden=true\n").visible);
        QVERIFY(!parse("x.desktop", "/x", "[Desktop Entry]\nType=Link\nName=A\nURL=x\n", {}, {}).visible);
        QVERIFY(!entry("Name=A\nExec=a\nTryExec=/nonexistent/mobarchy-test\n").visible);
    }

    void showIn()
    {
        const QByteArray only = "Name=A\nExec=a\nOnlyShowIn=GNOME;KDE;\n";
        QVERIFY(!entry(only, {}, {"Hyprland"}).visible);
        QVERIFY(entry(only, {}, {"Hyprland", "KDE"}).visible);
        QVERIFY(!entry("Name=A\nExec=a\nNotShowIn=Hyprland;\n", {}, {"Hyprland"}).visible);
    }

    void execFieldCodes()
    {
        DesktopEntry e = entry("Name=My App\nExec=app %U --name=%c %% %i --file %k\nIcon=app-icon\n");
        QCOMPARE(execArgs(e), (QStringList{"app", "--name=My App", "%", "--icon", "app-icon",
                                           "--file", "/a/x.desktop"}));
        e.icon.clear();
        QCOMPARE(execArgs(e), (QStringList{"app", "--name=My App", "%", "--file", "/a/x.desktop"}));
    }

    void execQuoting()
    {
        // In the file, a literal backslash inside quotes is written as four of them: string
        // unescaping turns \\\\ into \\, and the Exec quoting rules turn \\ into \.
        const DesktopEntry e = entry(
            "Name=A\nExec=\"/opt/my app/run\" \"say \\\\\"hi\\\\\"\" \"a\\\\\\\\b\" plain\n");
        QCOMPARE(execArgs(e), (QStringList{"/opt/my app/run", "say \"hi\"", "a\\b", "plain"}));
        QVERIFY(execArgs(entry("Name=A\nExec=\"unterminated\n")).isEmpty());
    }

    void stringEscapes()
    {
        QCOMPARE(entry("Name=a\\sb\\\\c\nExec=x\n").name, QStringLiteral("a b\\c"));
    }
};

QTEST_GUILESS_MAIN(TestDesktopEntry)
#include "tst_desktopentry.moc"
