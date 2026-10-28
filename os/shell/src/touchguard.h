// Recovers from touch points that never ended. If a finger's "up" is lost (the surface it
// touched went away), Qt keeps that point down for good, and Qt Quick then stops turning
// new touches into presses for MouseAreas: the whole shell ignores taps and swipes. The
// guard follows the points the shell's windows see; reset() cancels any still down, at
// moments when no finger can be (the screen just woke up, a screen came or went: a fold),
// and points that haven't moved for a while are cancelled by themselves (a finger on glass is
// never that still: it jitters, and each jitter is an event), whether or not other fingers
// touch meanwhile.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QPointingDevice;

class TouchGuard : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit TouchGuard(QObject *parent = nullptr);

    // Cancel touch points still down; `reason` goes to the log.
    Q_INVOKABLE void reset(const QString &reason);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Point {
        const QPointingDevice *device = nullptr;
        qint64 lastMoved = 0;  // ms on m_clock
    };
    QHash<int, Point> m_down;  // by point id
    QElapsedTimer m_clock;
    qint64 m_lastEvent = 0;    // ms on m_clock: any touch event at all
    QTimer m_stale;
};
