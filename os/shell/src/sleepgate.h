// Deep sleep (ADR 0043): the shell says when nothing needs the phone awake, and when it
// must be awake again for an alarm; mobarchy-suspend (root) suspends then. Through files in
// the session's runtime directory: $XDG_RUNTIME_DIR/mobarchy/sleep-ok (there: may sleep) and
// next-wake (seconds since the epoch).
#pragma once

#include <QDateTime>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class SleepGate : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool allowed READ allowed WRITE setAllowed NOTIFY allowedChanged)
    // the next alarm or timer (invalid: none)
    Q_PROPERTY(QDateTime nextWake READ nextWake WRITE setNextWake NOTIFY nextWakeChanged)

public:
    explicit SleepGate(QObject *parent = nullptr);
    ~SleepGate() override;

    bool allowed() const { return m_allowed; }
    void setAllowed(bool allowed);
    QDateTime nextWake() const { return m_nextWake; }
    void setNextWake(const QDateTime &at);

signals:
    void allowedChanged();
    void nextWakeChanged();

private:
    QString m_dir;
    bool m_allowed = false;
    QDateTime m_nextWake;
};
