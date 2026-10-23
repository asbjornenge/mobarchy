#include "sleepgate.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

SleepGate::SleepGate(QObject *parent)
    : QObject(parent)
    , m_dir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/mobarchy"))
{
    QDir().mkpath(m_dir);
    // (a shell starting: awake until it says otherwise)
    QFile::remove(m_dir + QStringLiteral("/sleep-ok"));
}

SleepGate::~SleepGate()
{
    QFile::remove(m_dir + QStringLiteral("/sleep-ok"));
}

void SleepGate::setAllowed(bool allowed)
{
    if (allowed == m_allowed)
        return;
    m_allowed = allowed;
    const QString path = m_dir + QStringLiteral("/sleep-ok");
    if (allowed) {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
    } else {
        QFile::remove(path);
    }
    emit allowedChanged();
}

void SleepGate::setNextWake(const QDateTime &at)
{
    if (at == m_nextWake)
        return;
    m_nextWake = at;
    const QString path = m_dir + QStringLiteral("/next-wake");
    if (at.isValid()) {
        QFile f(path + QStringLiteral(".tmp"));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QByteArray::number(at.toSecsSinceEpoch()) + '\n');
            f.close();
            QFile::remove(path);
            f.rename(path);
        }
    } else {
        QFile::remove(path);
    }
    emit nextWakeChanged();
}
