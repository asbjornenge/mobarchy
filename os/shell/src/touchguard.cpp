#include "touchguard.h"

#include <QDebug>
#include <QGuiApplication>
#include <QPointingDevice>
#include <QSet>
#include <QTouchEvent>
#include <qpa/qwindowsysteminterface.h>

namespace {
// No event about any point for this long: the points still down are lost ones.
constexpr qint64 StaleMs = 15000;
}

TouchGuard::TouchGuard(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    qGuiApp->installEventFilter(this);
    // (only while points are down)
    m_stale.setInterval(5000);
    connect(&m_stale, &QTimer::timeout, this, [this] {
        const qint64 now = m_clock.elapsed();
        if (now - m_lastEvent > StaleMs) {
            reset(QStringLiteral("no touch event for %1 s").arg(StaleMs / 1000));
            return;
        }
        // (other fingers touching meanwhile: a point that hasn't moved in all that time is
        // a lost one, kept by Qt as stationary in every event since)
        for (const Point &p : std::as_const(m_down)) {
            if (now - p.lastMoved > StaleMs) {
                reset(QStringLiteral("a point still for %1 s").arg(StaleMs / 1000));
                return;
            }
        }
    });
    // A fold or an output change takes the shell's surfaces away with any finger on them:
    // no finger can be down across it.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { reset(QStringLiteral("a screen added")); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { reset(QStringLiteral("a screen removed")); });
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
        m_lastEvent = m_clock.elapsed();
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
        return false;
    }
    if (m_down.isEmpty())
        m_stale.stop();
    else if (!m_stale.isActive())
        m_stale.start();
    return false;
}

void TouchGuard::reset(const QString &reason)
{
    m_stale.stop();
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
