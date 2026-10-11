#include "alarms.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {

// Checked at least this often, whatever the next ring: the clock may be set, or the zone
// changed, while waiting.
constexpr int MaxWaitMs = 60 * 1000;

double epochNow()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

}  // namespace

Alarms::Alarms(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Alarms::check);
    // The file is replaced whole (a rename): watch the directory, and the file again after.
    QDir().mkpath(QFileInfo(path()).absolutePath());
    m_watcher.addPath(QFileInfo(path()).absolutePath());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Alarms::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Alarms::reload);
    reload();
}

QString Alarms::path() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/mobarchy/alarms.json");
}

QJsonObject Alarms::read() const
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void Alarms::modify(const std::function<void(QJsonObject &)> &change)
{
    const int lock = ::open(QFile::encodeName(path() + QStringLiteral(".lock")).constData(),
                            O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock >= 0)
        ::flock(lock, LOCK_EX);
    QJsonObject file = read();
    change(file);
    QSaveFile out(path());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(file).toJson(QJsonDocument::Indented));
        if (!out.commit())
            qWarning("alarms: can't write %s", qPrintable(path()));
    }
    if (lock >= 0)
        ::close(lock);
    m_file = file;
}

void Alarms::reload()
{
    if (QFile::exists(path()) && !m_watcher.files().contains(path()))
        m_watcher.addPath(path());
    m_file = read();
    check();
}

void Alarms::check()
{
    const QDateTime now = QDateTime::currentDateTime();
    const auto due = alarmschedule::due(m_file, now);
    QList<alarmschedule::Due> fresh;
    for (const auto &d : due) {
        const bool queued = std::any_of(m_queue.cbegin(), m_queue.cend(), [&](const alarmschedule::Due &q) {
            return q.kind == d.kind && q.id == d.id;
        });
        if (!queued)
            fresh << d;
    }
    if (!fresh.isEmpty()) {
        // It has rung: not again after a restart (an alarm), and a snooze is used up.
        modify([&](QJsonObject &file) {
            QJsonArray alarms = file.value(QStringLiteral("alarms")).toArray();
            for (qsizetype i = 0; i < alarms.size(); ++i) {
                QJsonObject a = alarms[i].toObject();
                for (const auto &d : std::as_const(fresh))
                    if (d.kind == QLatin1String("alarm") && a.value(QStringLiteral("id")).toString() == d.id) {
                        a[QStringLiteral("last_rung")] = epochNow();
                        a[QStringLiteral("snoozed_until")] = QJsonValue::Null;
                    }
                alarms[i] = a;
            }
            file[QStringLiteral("alarms")] = alarms;
        });
        const bool was = ringing();
        for (const auto &d : std::as_const(fresh))
            qInfo("alarms: ringing %s %s %s", qPrintable(d.kind), qPrintable(d.id), qPrintable(d.label));
        m_queue << fresh;
        emit ringingChanged();
        if (!was)
            emit started();
    }
    const QDateTime next = alarmschedule::nextAlarm(m_file, now);
    if (next != m_nextAlarm) {
        m_nextAlarm = next;
        emit changed();
    }
    const QDateTime event = alarmschedule::nextEvent(m_file, now);
    const qint64 wait = event.isValid() ? now.msecsTo(event) + 50 : MaxWaitMs;
    m_timer.start(int(std::clamp<qint64>(wait, 50, MaxWaitMs)));
}

void Alarms::pop()
{
    if (!m_queue.isEmpty())
        m_queue.removeFirst();
    emit ringingChanged();
}

void Alarms::snooze()
{
    if (!ringing())
        return;
    const auto d = m_queue.first();
    if (d.kind != QLatin1String("alarm"))
        return dismiss();
    modify([&](QJsonObject &file) {
        QJsonArray alarms = file.value(QStringLiteral("alarms")).toArray();
        for (qsizetype i = 0; i < alarms.size(); ++i) {
            QJsonObject a = alarms[i].toObject();
            if (a.value(QStringLiteral("id")).toString() == d.id)
                a[QStringLiteral("snoozed_until")] = epochNow() + snoozeMinutes() * 60;
            alarms[i] = a;
        }
        file[QStringLiteral("alarms")] = alarms;
    });
    pop();
    check();
}

void Alarms::dismiss()
{
    if (!ringing())
        return;
    const auto d = m_queue.first();
    modify([&](QJsonObject &file) {
        if (d.kind == QLatin1String("timer")) {
            QJsonArray timers = file.value(QStringLiteral("timers")).toArray();
            for (qsizetype i = timers.size() - 1; i >= 0; --i)
                if (timers[i].toObject().value(QStringLiteral("id")).toString() == d.id)
                    timers.removeAt(i);
            file[QStringLiteral("timers")] = timers;
            return;
        }
        QJsonArray alarms = file.value(QStringLiteral("alarms")).toArray();
        for (qsizetype i = 0; i < alarms.size(); ++i) {
            QJsonObject a = alarms[i].toObject();
            if (a.value(QStringLiteral("id")).toString() == d.id) {
                a[QStringLiteral("snoozed_until")] = QJsonValue::Null;
                if (a.value(QStringLiteral("days")).toArray().isEmpty())
                    a[QStringLiteral("enabled")] = false;
            }
            alarms[i] = a;
        }
        file[QStringLiteral("alarms")] = alarms;
    });
    pop();
    check();
}
