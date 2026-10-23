#include "friggorb.h"

#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

namespace {
// how much a state moves the orb: turning speed, swirl, ripple, pulse
struct Motion { double spin, swirl, ripple, pulse; };
Motion motionFor(const QString &state)
{
    if (state == QLatin1String("listening"))
        return {0.25, 0.0, 0.0, 1.0};
    if (state == QLatin1String("thinking") || state == QLatin1String("starting"))
        return {0.9, 1.0, 0.0, 0.2};
    if (state == QLatin1String("speaking"))
        return {0.35, 0.0, 1.0, 0.3};
    return {0.12, 0.0, 0.0, 0.0};
}

QSGGeometry::ColoredPoint2D vertex(float x, float y, const QColor &c, float alpha)
{
    QSGGeometry::ColoredPoint2D v;
    // (premultiplied, as the vertex colour material wants)
    v.set(x, y, uchar(c.red() * alpha), uchar(c.green() * alpha), uchar(c.blue() * alpha), uchar(255 * alpha));
    return v;
}
}

FriggOrb::FriggOrb(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    build(160);
    m_clock.start();
    // about a frame apart while running and in sight (and not at all otherwise)
    m_frames.setInterval(16);
    m_frames.setTimerType(Qt::PreciseTimer);
    connect(&m_frames, &QTimer::timeout, this, &FriggOrb::tick);
    connect(this, &QQuickItem::visibleChanged, this, &FriggOrb::schedule);
    schedule();
}

void FriggOrb::schedule()
{
    if (m_running && isVisible())
        m_frames.start();
    else
        m_frames.stop();
}

void FriggOrb::setColor(const QColor &c)
{
    if (c == m_color)
        return;
    m_color = c;
    emit colorChanged();
    update();
}

void FriggOrb::setState(const QString &s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged();
    update();
}

void FriggOrb::setRunning(bool r)
{
    if (r == m_running)
        return;
    m_running = r;
    m_last = 0;
    schedule();
    emit runningChanged();
    update();
}

void FriggOrb::setPoints(int n)
{
    n = qBound(20, n, 600);
    if (n == m_points.size())
        return;
    build(n);
    emit pointsChanged();
    update();
}

void FriggOrb::turnBy(double dx, double dy)
{
    const double perPixel = M_PI / qMax(1.0, qMin(width(), height()));    // half a turn across it
    m_spin -= dx * perPixel;
    m_tilt = qBound(-1.2, m_tilt - dy * perPixel, 1.2);
    m_spinSpeed = m_tiltSpeed = 0;
    update();
}

void FriggOrb::release(double vx, double vy)
{
    const double perPixel = M_PI / qMax(1.0, qMin(width(), height()));
    m_spinSpeed = -vx * perPixel;
    m_tiltSpeed = -vy * perPixel;
}

// points spread evenly on a sphere (a Fibonacci lattice), a little uneven so it looks alive,
// each joined to those near it
void FriggOrb::build(int n)
{
    m_points.clear();
    m_links.clear();
    auto *rng = QRandomGenerator::global();
    const double golden = M_PI * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; i++) {
        const double y = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(1.0 - y * y);
        const double a = golden * i;
        QVector3D p(float(std::cos(a) * r), float(y), float(std::sin(a) * r));
        p += QVector3D(float(rng->bounded(0.12) - 0.06), float(rng->bounded(0.12) - 0.06),
                       float(rng->bounded(0.12) - 0.06));
        // some a little inside: depth, not a shell
        p *= float(0.82 + rng->bounded(0.18));
        m_points.append(p);
    }
    // neighbours: within about two lattice spacings
    const float reach = float(2.3 * std::sqrt(4.0 / n));
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if ((m_points[i] - m_points[j]).length() < reach)
                m_links.append({i, j});
}

void FriggOrb::tick()
{
    const qint64 now = m_clock.elapsed();
    const double dt = m_last ? qMin(0.1, (now - m_last) / 1000.0) : 0.0;
    m_last = now;
    const Motion m = motionFor(m_state);
    m_energy += (m.spin - m_energy) * qMin(1.0, dt * 2.0);    // speeds change smoothly
    m_time += dt;
    m_spin += dt * m_energy * 2.0;
    // a fling spins on, slowing down; the tilt eases back
    m_spin += dt * m_spinSpeed;
    m_tilt = qBound(-1.2, m_tilt + dt * m_tiltSpeed, 1.2);
    const double friction = std::exp(-dt * 1.4);
    m_spinSpeed *= friction;
    m_tiltSpeed *= friction;
    if (std::abs(m_tiltSpeed) < 0.05)
        m_tilt *= std::exp(-dt * 0.5);
    // at rest (no conversation, not flung): half the frames; the slow turn looks the same,
    // and the home screen costs half the battery
    const bool atRest = m.spin < 0.2 && std::abs(m_spinSpeed) < 0.05 && std::abs(m_tiltSpeed) < 0.05;
    const int interval = atRest ? 33 : 16;
    if (m_frames.interval() != interval)
        m_frames.setInterval(interval);
    update();
}

QSGNode *FriggOrb::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGNode *>(old);
    QSGGeometryNode *lines, *dots;
    if (!node) {
        node = new QSGNode;
        lines = new QSGGeometryNode;
        dots = new QSGGeometryNode;
        for (auto *g : {lines, dots}) {
            g->setMaterial(new QSGVertexColorMaterial);
            g->setFlag(QSGNode::OwnsMaterial);
            g->setFlag(QSGNode::OwnsGeometry);
            node->appendChildNode(g);
        }
        auto *lg = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        lg->setDrawingMode(QSGGeometry::DrawLines);
        lg->setLineWidth(1);
        lines->setGeometry(lg);
        auto *dg = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        dg->setDrawingMode(QSGGeometry::DrawTriangles);
        dots->setGeometry(dg);
    } else {
        lines = static_cast<QSGGeometryNode *>(node->firstChild());
        dots = static_cast<QSGGeometryNode *>(node->lastChild());
    }

    const Motion m = motionFor(m_state);
    // calm: smaller movements, slower
    const double amount = 1.0 - 0.6 * m_calm;
    const double t = m_time;
    const double tb = m_time * (1.0 - 0.45 * m_calm);   // the breathing's own time
    const float size = float(qMin(width(), height()));
    const float cx = float(width() / 2), cy = float(height() / 2);
    // breathing; a pulse while listening
    const double breath = 1.0 + amount * (0.025 * std::sin(tb * 1.3) + m.pulse * 0.06 * std::pow(std::sin(tb * 3.2), 2.0));
    const float radius = float(size * 0.42 * breath);
    const double yaw = m_spin, pitch = 0.35 + 0.15 * std::sin(t * 0.21) + m_tilt;
    const float cyaw = float(std::cos(yaw)), syaw = float(std::sin(yaw));
    const float cp = float(std::cos(pitch)), sp = float(std::sin(pitch));

    // turned, rippled and swirled, then projected: x, y on screen, z (-1 far .. 1 near)
    QList<QVector3D> at(m_points.size());
    for (int i = 0; i < m_points.size(); i++) {
        QVector3D p = m_points[i];
        // swirl (thinking): turn more the higher up
        if (m.swirl > 0) {
            const double a = amount * m.swirl * 0.6 * std::sin(tb * 1.7) * p.y();
            const float ca = float(std::cos(a)), sa = float(std::sin(a));
            p = QVector3D(p.x() * ca - p.z() * sa, p.y(), p.x() * sa + p.z() * ca);
        }
        // ripple (speaking): waves outward over the surface
        if (m.ripple > 0)
            p *= float(1.0 + amount * 0.07 * m.ripple * std::sin(tb * 9.0 - p.y() * 6.0 + p.x() * 3.0));
        const float x1 = p.x() * cyaw - p.z() * syaw;
        const float z1 = p.x() * syaw + p.z() * cyaw;
        const float y2 = p.y() * cp - z1 * sp;
        const float z2 = p.y() * sp + z1 * cp;
        const float persp = 1.0f / (1.6f - z2 * 0.35f);
        at[i] = QVector3D(cx + x1 * radius * persp * 1.6f, cy + y2 * radius * persp * 1.6f, z2);
    }

    // lines: fainter the further back
    QSGGeometry *lg = lines->geometry();
    lg->allocate(int(m_links.size()) * 2);
    auto *lv = lg->vertexDataAsColoredPoint2D();
    for (int k = 0; k < m_links.size(); k++) {
        const QVector3D &a = at[m_links[k].first], &b = at[m_links[k].second];
        const float alpha = qBound(0.03f, 0.22f + 0.2f * (a.z() + b.z()) / 2, 0.45f);
        lv[2 * k] = vertex(a.x(), a.y(), m_color, alpha);
        lv[2 * k + 1] = vertex(b.x(), b.y(), m_color, alpha);
    }
    lines->markDirty(QSGNode::DirtyGeometry);

    // points: round, a bright core in a soft halo (the halo fades out to its rim), larger and
    // brighter in front, twinkling a little
    constexpr int Sides = 10;
    constexpr int PerPoint = Sides * 3 * 2;     // two fans of triangles: halo, core
    QSGGeometry *dg = dots->geometry();
    dg->allocate(int(m_points.size()) * PerPoint);
    auto *dv = dg->vertexDataAsColoredPoint2D();
    const QColor bright = m_color.lighter(160);
    const float unit = qMax(1.0f, size / 220.0f);
    static float cosines[Sides + 1], sines[Sides + 1];
    static bool ready = false;
    if (!ready) {
        for (int k = 0; k <= Sides; k++) {
            cosines[k] = float(std::cos(2 * M_PI * k / Sides));
            sines[k] = float(std::sin(2 * M_PI * k / Sides));
        }
        ready = true;
    }
    int n = 0;
    for (int i = 0; i < at.size(); i++) {
        const QVector3D &p = at[i];
        const float near = (p.z() + 1) / 2;
        const float twinkle = float(0.75 + 0.25 * std::sin(t * 2.0 + i * 1.7));
        const float alpha = qBound(0.15f, (0.35f + 0.65f * near) * twinkle, 1.0f);
        const float core = unit * (0.35f + 0.8f * near);
        const float halo = core * 2.6f;
        const QColor &c = near > 0.6f ? bright : m_color;
        for (int k = 0; k < Sides; k++) {
            // the halo: from the middle, fading to nothing at the rim
            dv[n++] = vertex(p.x(), p.y(), c, alpha * 0.45f);
            dv[n++] = vertex(p.x() + halo * cosines[k], p.y() + halo * sines[k], c, 0);
            dv[n++] = vertex(p.x() + halo * cosines[k + 1], p.y() + halo * sines[k + 1], c, 0);
            // the core: solid
            dv[n++] = vertex(p.x(), p.y(), c, alpha);
            dv[n++] = vertex(p.x() + core * cosines[k], p.y() + core * sines[k], c, alpha);
            dv[n++] = vertex(p.x() + core * cosines[k + 1], p.y() + core * sines[k + 1], c, alpha);
        }
    }
    dots->markDirty(QSGNode::DirtyGeometry);
    return node;
}
