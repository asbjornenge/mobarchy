// Frigg's orb: points on a sphere, joined to their neighbours, turning slowly in 3D (a
// "plexus"), drawn in the scene graph (lines and small quads, coloured by depth). It breathes
// when idle, pulses while listening, swirls faster while thinking and ripples while speaking.
// It moves only while `running`.
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QTimer>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

class FriggOrb : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    // "idle", "listening", "thinking", "speaking" (as Talk's state; anything else is idle)
    Q_PROPERTY(QString state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    // how many points (fewer for a small orb)
    Q_PROPERTY(int points READ points WRITE setPoints NOTIFY pointsChanged)

public:
    explicit FriggOrb(QQuickItem *parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    QString state() const { return m_state; }
    void setState(const QString &s);
    bool running() const { return m_running; }
    void setRunning(bool r);
    int points() const { return int(m_points.size()); }
    void setPoints(int n);

signals:
    void colorChanged();
    void stateChanged();
    void runningChanged();
    void pointsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    void build(int n);
    void tick();
    void schedule();
    QTimer m_frames;

    QColor m_color = QColor(122, 162, 247);
    QString m_state = QStringLiteral("idle");
    bool m_running = true;
    QList<QVector3D> m_points;
    QList<QPair<int, int>> m_links;     // neighbours, found once (turning keeps distances)
    QElapsedTimer m_clock;
    double m_time = 0;                  // seconds the orb has moved
    double m_spin = 0;                  // radians turned
    double m_energy = 0;                // eases towards the state's
    qint64 m_last = 0;
};
