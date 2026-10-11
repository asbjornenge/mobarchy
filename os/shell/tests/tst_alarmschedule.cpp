#include "../src/alarmschedule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace alarmschedule;

namespace {

// Sunday 2026-10-11
const QDateTime Now(QDate(2026, 10, 11), QTime(9, 30));

double epoch(const QDateTime &t)
{
    return t.toMSecsSinceEpoch() / 1000.0;
}

QJsonObject file(const char *json)
{
    return QJsonDocument::fromJson(json).object();
}

QJsonObject alarm(const QString &time, QJsonArray days = {}, QJsonObject extra = {})
{
    QJsonObject a{{"id", "a1"}, {"time", time}, {"days", days}, {"label", "Up"}, {"enabled", true},
                  {"created", epoch(Now.addDays(-1))}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        a.insert(it.key(), it.value());
    return a;
}

QJsonObject with(const QJsonObject &a)
{
    return QJsonObject{{"alarms", QJsonArray{a}}, {"timers", QJsonArray{}}};
}

}  // namespace

class TestAlarmSchedule : public QObject
{
    Q_OBJECT

private slots:
    void nextRingOnceAndOnDays()
    {
        QCOMPARE(*nextRing(alarm("10:00"), Now), QDateTime(QDate(2026, 10, 11), QTime(10, 0)));
        QCOMPARE(*nextRing(alarm("07:00"), Now), QDateTime(QDate(2026, 10, 12), QTime(7, 0)));
        // weekdays: Monday next
        QCOMPARE(*nextRing(alarm("10:00", {1, 2, 3, 4, 5}), Now), QDateTime(QDate(2026, 10, 12), QTime(10, 0)));
        QVERIFY(!nextRing(alarm("10:00", {}, {{"enabled", false}}), Now));
    }

    void dueRightAfterItsTime()
    {
        const auto a = alarm("09:25");
        const auto d = due(with(a), Now);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].kind, QStringLiteral("alarm"));
        QCOMPARE(d[0].at, QDateTime(QDate(2026, 10, 11), QTime(9, 25)));
        QVERIFY(d[0].once);
        // too long ago (the shell was down): not any more
        QVERIFY(due(with(alarm("09:10")), Now).isEmpty());
        // not on a Sunday
        QVERIFY(due(with(alarm("09:25", {1})), Now).isEmpty());
    }

    void notTwiceAndNotBeforeItWasMade()
    {
        QVERIFY(due(with(alarm("09:25", {}, {{"last_rung", epoch(Now.addSecs(-60))}})), Now).isEmpty());
        // made at 09:28 for 09:25: that one has passed, it is for tomorrow
        QVERIFY(due(with(alarm("09:25", {}, {{"created", epoch(Now.addSecs(-120))}})), Now).isEmpty());
    }

    void snoozeRingsAgain()
    {
        // rang at 07:00, snoozed to 09:29
        const auto a = alarm("07:00", {}, {{"last_rung", epoch(Now.addSecs(-9000))},
                                          {"snoozed_until", epoch(Now.addSecs(-60))}});
        const auto d = due(with(a), Now);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].at, Now.addSecs(-60));
        const auto later = alarm("07:00", {}, {{"snoozed_until", epoch(Now.addSecs(300))}});
        QCOMPARE(*nextRing(later, Now), Now.addSecs(300));
    }

    void timers()
    {
        const auto f = file(R"({"alarms": [], "timers": [{"id": "t1", "label": "Pasta", "ends": 0}]})");
        QJsonObject g = f;
        QJsonArray timers{QJsonObject{{"id", "t1"}, {"label", "Pasta"}, {"ends", epoch(Now.addSecs(-5))}}};
        g["timers"] = timers;
        const auto d = due(g, Now);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].kind, QStringLiteral("timer"));
        timers[0] = QJsonObject{{"id", "t1"}, {"ends", epoch(Now.addSecs(90))}};
        g["timers"] = timers;
        QVERIFY(due(g, Now).isEmpty());
        QCOMPARE(nextEvent(g, Now), Now.addSecs(90));
        QVERIFY(!nextAlarm(g, Now).isValid());
    }

    void nextEventIsTheSoonest()
    {
        QJsonObject f = with(alarm("10:00"));
        f["timers"] = QJsonArray{QJsonObject{{"id", "t"}, {"ends", epoch(Now.addSecs(3600))}}};
        QCOMPARE(nextEvent(f, Now), QDateTime(QDate(2026, 10, 11), QTime(10, 0)));
        QCOMPARE(nextAlarm(f, Now), QDateTime(QDate(2026, 10, 11), QTime(10, 0)));
    }
};

QTEST_GUILESS_MAIN(TestAlarmSchedule)
#include "tst_alarmschedule.moc"
