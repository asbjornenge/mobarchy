// The torch (the camera's flash as a light), through org.mobarchy.Camera
// (mobarchy-camera-source, ADR 0018).
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Torch : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool on READ on NOTIFY onChanged)
    // Something answers (the cameras are up); false: no torch to offer.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit Torch(QObject *parent = nullptr);

    bool on() const { return m_on; }
    bool available() const { return m_available; }

    Q_INVOKABLE void setOn(bool on);
    Q_INVOKABLE void toggle() { setOn(!m_on); }

signals:
    void onChanged();
    void availableChanged();

private slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &);

private:
    void refresh();
    void setState(bool on);

    bool m_on = false;
    bool m_available = false;
};
