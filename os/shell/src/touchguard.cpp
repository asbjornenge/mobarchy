#include "touchguard.h"

#include <QDebug>
#include <QGuiApplication>
#include <QPointingDevice>
#include <QSet>
#include <QTouchEvent>
#include <qpa/qwindowsysteminterface.h>

TouchGuard::TouchGuard(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    qGuiApp->installEventFilter(this);
}

bool TouchGuard::eventFilter(QObject *watched, QEvent *event)
{
    // Touch events reach a window first, then its items get them too: count them once.
    if (!watched->isWindowType())
        return false;
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd: {
        const auto *touch = static_cast<QTouchEvent *>(event);
        for (const QEventPoint &p : touch->points()) {
            if (p.state() == QEventPoint::Released) {
                m_down.remove(p.id());
                continue;
            }
            Point &point = m_down[p.id()];
            point.device = touch->pointingDevice();
            if (p.state() != QEventPoint::Stationary || !point.lastMoved)
                point.lastMoved = m_clock.elapsed();
        }
        break;
    }
    case QEvent::TouchCancel:
        m_down.clear();
        break;
    default:
        break;
    }
    return false;
}

void TouchGuard::reset(const QString &reason)
{
    if (m_down.isEmpty())
        return;
    QStringList points;
    QSet<const QPointingDevice *> devices;
    for (auto it = m_down.cbegin(); it != m_down.cend(); ++it) {
        points << QStringLiteral("%1 (still for %2 s)").arg(it.key()).arg((m_clock.elapsed() - it->lastMoved) / 1000);
        devices.insert(it->device);
    }
    qWarning().noquote() << "touch:" << m_down.size() << "point(s) still down at" << reason
                         << "- cancelling:" << points.join(QStringLiteral(", "));
    // What the platform does when the compositor cancels a touch sequence: Qt drops the
    // points, and Qt Quick its grabs and its touch-to-mouse state.
    for (const QPointingDevice *device : std::as_const(devices))
        QWindowSystemInterface::handleTouchCancelEvent(nullptr, device);
    m_down.clear();
}
