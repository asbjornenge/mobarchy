// The owner's calendars (ADR 0034), through the `mobarchy-calendar` command (the agent
// package's, which reads the phone's calendar and the accounts' and expands repeating
// events): the Calendar app's and the home board's backend. It follows the files the events
// are in, and asks again when they change (an event added by Frigg, an account synced).
#pragma once

#include <QDate>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class CalendarData : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the days asked for: from `from`, `days` of them
    Q_PROPERTY(QDate from READ from WRITE setFrom NOTIFY rangeChanged)
    Q_PROPERTY(int days READ days WRITE setDays NOTIFY rangeChanged)
    // [{id, uid, calendar, calendar_name, color, title, start, end, all_day, location, notes,
    //   editable}], start and end ISO 8601 with the offset
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    // [{id, name, color, account, enabled, editable}]
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY eventsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit CalendarData(QObject *parent = nullptr);

    QDate from() const { return m_from; }
    void setFrom(const QDate &d);
    int days() const { return m_days; }
    void setDays(int n);
    QVariantList events() const { return m_events; }
    QVariantList calendars() const { return m_calendars; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    // start/end: "YYYY-MM-DD" (all day) or "YYYY-MM-DDTHH:MM"
    Q_INVOKABLE void add(const QString &title, const QString &start, const QString &end,
                         const QString &location, const QString &notes, const QString &repeat);
    Q_INVOKABLE void update(const QString &uid, const QString &title, const QString &start,
                            const QString &end, const QString &location, const QString &notes);
    Q_INVOKABLE void remove(const QString &uid);

signals:
    void rangeChanged();
    void eventsChanged();
    void errorChanged();
    void changed();     // an add, update or remove went through

private:
    void run(const QStringList &args, std::function<void(const QJsonObject &)> done);
    void watch();

    QDate m_from = QDate::currentDate();
    int m_days = 1;
    QVariantList m_events, m_calendars;
    QString m_error;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    int m_generation = 0;   // (an answer to an older question is not taken)
};
