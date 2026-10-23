// A foldable's hinge (laptop mode): its angle from mobarchy-sensor-proxy
// (org.mobarchy.SensorProxy.Hinge, beside net.hadess.SensorProxy), claimed only while
// active.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Hinge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Read the sensor (it runs only while something has it claimed).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // Degrees: 0 closed, 180 flat; -1 not known (or not active).
    Q_PROPERTY(qreal angle READ angle NOTIFY angleChanged)

public:
    explicit Hinge(QObject *parent = nullptr);
    ~Hinge() override;

    bool active() const { return m_active; }
    void setActive(bool active);
    bool available() const { return m_available; }
    qreal angle() const { return m_angle; }

signals:
    void activeChanged();
    void availableChanged();
    void angleChanged();

private slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &);

private:
    void claim(bool claim);
    void refresh();
    void setAngle(qreal angle);

    bool m_active = false;
    bool m_available = false;
    qreal m_angle = -1;
};
