// How the device is held (ADR 0020): the accelerometer's orientation from
// net.hadess.SensorProxy (iio-sensor-proxy, or mobarchy-sensor-proxy on devices whose
// sensors are behind the Android container), claimed only while active.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class DeviceOrientation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Read the sensor (it runs only while something has it claimed).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // normal, left-up, bottom-up, right-up, or undefined (not known, or not active)
    Q_PROPERTY(QString orientation READ orientation NOTIFY orientationChanged)
    // The same as degrees the device is turned clockwise from its natural orientation
    // (left-up: 90); 0 while undefined.
    Q_PROPERTY(int degrees READ degrees NOTIFY orientationChanged)

public:
    explicit DeviceOrientation(QObject *parent = nullptr);
    ~DeviceOrientation() override;

    bool active() const { return m_active; }
    void setActive(bool active);
    bool available() const { return m_available; }
    QString orientation() const { return m_orientation; }
    int degrees() const;

signals:
    void activeChanged();
    void availableChanged();
    void orientationChanged();

private slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &);

private:
    void claim(bool claim);
    void refresh();
    void setOrientation(const QString &orientation);

    bool m_active = false;
    bool m_available = false;
    QString m_orientation = QStringLiteral("undefined");
};
