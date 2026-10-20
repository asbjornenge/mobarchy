// Whether something is near the screen (an ear, during a call): the proximity sensor from
// net.hadess.SensorProxy (mobarchy-sensor-proxy on comet; ADR 0020), claimed only while
// active.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Proximity : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool near READ near NOTIFY nearChanged)    // false while not active

public:
    explicit Proximity(QObject *parent = nullptr);
    ~Proximity() override;

    bool active() const { return m_active; }
    void setActive(bool active);
    bool available() const { return m_available; }
    bool near() const { return m_near; }

signals:
    void activeChanged();
    void availableChanged();
    void nearChanged();

private slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &);

private:
    void claim(bool claim);
    void refresh();
    void setNear(bool near);

    bool m_active = false;
    bool m_available = false;
    bool m_near = false;
};
