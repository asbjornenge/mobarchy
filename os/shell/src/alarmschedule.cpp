#include "alarmschedule.h"

#include <QJsonArray>

namespace alarmschedule {

namespace {

QDateTime fromEpoch(const QJsonValue &v)
{
    return v.isDouble() ? QDateTime::fromMSecsSinceEpoch(qint64(v.toDouble() * 1000)) : QDateTime();
}

bool enabled(const QJsonObject &alarm)
{
    return alarm.value(QStringLiteral("enabled")).toBool(true);
}

std::optional<QTime> timeOfDay(const QJsonObject &alarm)
{
    const QTime t = QTime::fromString(alarm.value(QStringLiteral("time")).toString(), QStringLiteral("HH:mm"));
    return t.isValid() ? std::optional(t) : std::nullopt;
}

bool onDay(const QJsonObject &alarm, const QDate &day)
{
    const QJsonArray days = alarm.value(QStringLiteral("days")).toArray();
    if (days.isEmpty())
        return true;
    for (const QJsonValue &d : days)
        if (d.toInt() == day.dayOfWeek())
            return true;
    return false;
}

}  // namespace

std::optional<QDateTime> lastRing(const QJsonObject &alarm, const QDateTime &now)
{
    const auto t = timeOfDay(alarm);
    if (!enabled(alarm) || !t)
        return std::nullopt;
    std::optional<QDateTime> last;
    for (int back = 0; back <= 7 && !last; ++back) {
        const QDate day = now.date().addDays(-back);
        const QDateTime at(day, *t);
        if (at <= now && onDay(alarm, day))
            last = at;
    }
    const QDateTime snoozed = fromEpoch(alarm.value(QStringLiteral("snoozed_until")));
    if (snoozed.isValid() && snoozed <= now && (!last || snoozed > *last))
        last = snoozed;
    return last;
}

std::optional<QDateTime> nextRing(const QJsonObject &alarm, const QDateTime &now)
{
    const auto t = timeOfDay(alarm);
    if (!enabled(alarm) || !t)
        return std::nullopt;
    const QDateTime snoozed = fromEpoch(alarm.value(QStringLiteral("snoozed_until")));
    if (snoozed.isValid() && snoozed > now)
        return snoozed;
    for (int ahead = 0; ahead <= 7; ++ahead) {
        const QDate day = now.date().addDays(ahead);
        const QDateTime at(day, *t);
        if (at > now && onDay(alarm, day))
            return at;
    }
    return std::nullopt;
}

QList<Due> due(const QJsonObject &file, const QDateTime &now, int grace)
{
    QList<Due> out;
    for (const QJsonValue &v : file.value(QStringLiteral("alarms")).toArray()) {
        const QJsonObject a = v.toObject();
        const auto last = lastRing(a, now);
        if (!last || last->secsTo(now) > grace)
            continue;
        const QDateTime created = fromEpoch(a.value(QStringLiteral("created")));
        const QDateTime rung = fromEpoch(a.value(QStringLiteral("last_rung")));
        if ((created.isValid() && *last <= created) || (rung.isValid() && *last <= rung))
            continue;
        out << Due{QStringLiteral("alarm"), a.value(QStringLiteral("id")).toString(),
                   a.value(QStringLiteral("label")).toString(), *last,
                   a.value(QStringLiteral("days")).toArray().isEmpty()};
    }
    for (const QJsonValue &v : file.value(QStringLiteral("timers")).toArray()) {
        const QJsonObject t = v.toObject();
        const QDateTime ends = fromEpoch(t.value(QStringLiteral("ends")));
        if (ends.isValid() && ends <= now && ends.secsTo(now) <= grace)
            out << Due{QStringLiteral("timer"), t.value(QStringLiteral("id")).toString(),
                       t.value(QStringLiteral("label")).toString(), ends, true};
    }
    return out;
}

QDateTime nextAlarm(const QJsonObject &file, const QDateTime &now)
{
    QDateTime next;
    for (const QJsonValue &v : file.value(QStringLiteral("alarms")).toArray()) {
        const auto at = nextRing(v.toObject(), now);
        if (at && (!next.isValid() || *at < next))
            next = *at;
    }
    return next;
}

QDateTime nextEvent(const QJsonObject &file, const QDateTime &now)
{
    QDateTime next = nextAlarm(file, now);
    for (const QJsonValue &v : file.value(QStringLiteral("timers")).toArray()) {
        const QDateTime ends = fromEpoch(v.toObject().value(QStringLiteral("ends")));
        if (ends.isValid() && ends > now && (!next.isValid() || ends < next))
            next = ends;
    }
    return next;
}

}  // namespace alarmschedule
