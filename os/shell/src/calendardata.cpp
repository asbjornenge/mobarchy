#include "calendardata.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <utility>

namespace {
const QString Command = QStringLiteral("mobarchy-calendar");

QString home(QStandardPaths::StandardLocation where, const QString &path)
{
    return QStandardPaths::writableLocation(where) + QLatin1Char('/') + path;
}
// where events are: the phone's calendar, the accounts' synced events
QStringList watchedDirs()
{
    return {home(QStandardPaths::GenericDataLocation, QStringLiteral("mobarchy/calendar")),
            home(QStandardPaths::GenericCacheLocation, QStringLiteral("mobarchy/caldav"))};
}
// and which of the accounts' calendars are on (a file: its directory has much else)
QString accountsFile()
{
    return home(QStandardPaths::GenericConfigLocation, QStringLiteral("mobarchy/accounts.json"));
}
}

CalendarData::CalendarData(QObject *parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(300);
    connect(&m_settle, &QTimer::timeout, this, &CalendarData::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    watch();
    QTimer::singleShot(0, this, &CalendarData::refresh);
}

void CalendarData::watch()
{
    for (const QString &dir : watchedDirs()) {
        QDir().mkpath(dir);
        if (!m_watcher.directories().contains(dir))
            m_watcher.addPath(dir);
    }
    // (replaced by a rename, it is a new file to watch)
    if (QFile::exists(accountsFile()) && !m_watcher.files().contains(accountsFile()))
        m_watcher.addPath(accountsFile());
}

void CalendarData::setFrom(const QDate &d)
{
    if (d == m_from || !d.isValid())
        return;
    m_from = d;
    emit rangeChanged();
    refresh();
}

void CalendarData::setDays(int n)
{
    n = qBound(1, n, 400);
    if (n == m_days)
        return;
    m_days = n;
    emit rangeChanged();
    refresh();
}

void CalendarData::run(const QStringList &args, std::function<void(const QJsonObject &)> done,
                       std::function<void()> after)
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, done, after](int code, QProcess::ExitStatus status) {
        const QJsonObject o = QJsonDocument::fromJson(p->readAllStandardOutput().trimmed()).object();
        p->deleteLater();
        if (after)
            after();
        if (status != QProcess::NormalExit || code != 0) {
            m_error = o.value(QStringLiteral("error")).toString(tr("The calendar did not answer."));
            emit errorChanged();
            return;
        }
        if (!m_error.isEmpty()) {
            m_error.clear();
            emit errorChanged();
        }
        done(o);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, after](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        if (after)
            after();
        m_error = tr("mobarchy-calendar is not installed");
        emit errorChanged();
    });
    p->start(Command, args);
}

void CalendarData::refresh()
{
    if (m_asking) {
        m_askAgain = true;
        return;
    }
    watch();
    m_asking = true;
    const int generation = ++m_generation;
    run({QStringLiteral("events"), QStringLiteral("--from"), m_from.toString(Qt::ISODate),
         QStringLiteral("--days"), QString::number(m_days)},
        [this, generation](const QJsonObject &o) {
            if (generation != m_generation || m_askAgain)
                return;
            m_events = o.value(QStringLiteral("events")).toArray().toVariantList();
            run({QStringLiteral("calendars")}, [this](const QJsonObject &c) {
                m_calendars = c.value(QStringLiteral("calendars")).toArray().toVariantList();
                emit eventsChanged();
            });
        },
        [this] {
            m_asking = false;
            if (std::exchange(m_askAgain, false))
                refresh();
        });
}

void CalendarData::add(const QString &title, const QString &start, const QString &end,
                       const QString &location, const QString &notes, const QString &repeat)
{
    QStringList args = {QStringLiteral("add"), title, start};
    if (!end.isEmpty())
        args << QStringLiteral("--end") << end;
    if (!location.isEmpty())
        args << QStringLiteral("--location") << location;
    if (!notes.isEmpty())
        args << QStringLiteral("--notes") << notes;
    if (!repeat.isEmpty())
        args << QStringLiteral("--repeat") << repeat;
    run(args, [this](const QJsonObject &) { emit changed(); refresh(); });
}

void CalendarData::update(const QString &uid, const QString &title, const QString &start,
                          const QString &end, const QString &location, const QString &notes)
{
    run({QStringLiteral("update"), uid, QStringLiteral("--title"), title, QStringLiteral("--start"), start,
         QStringLiteral("--end"), end, QStringLiteral("--location"), location, QStringLiteral("--notes"), notes},
        [this](const QJsonObject &) { emit changed(); refresh(); });
}

void CalendarData::remove(const QString &uid)
{
    run({QStringLiteral("delete"), uid}, [this](const QJsonObject &) { emit changed(); refresh(); });
}
