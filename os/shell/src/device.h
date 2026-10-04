// QML access to /etc/mobarchy/device.toml.
#pragma once

#include "deviceinfo.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Device : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Device(QObject *parent = nullptr);

    // See deviceinfo::topInset. connector: QScreen::name(), e.g. "DSI-1".
    Q_INVOKABLE int topInset(const QString &connector) const;
    Q_INVOKABLE int sideInset(const QString &connector) const;
    // Rotation (ADR 0020): whether the device can turn its screens ([rotation] hook), and
    // whether a display's rotation is locked until the owner unlocks it (rotation_locked).
    Q_INVOKABLE bool canRotate() const;
    Q_INVOKABLE bool rotationLockedByDefault(const QString &connector) const;
    // Turns the screen in use for the device held as orientation (iio-sensor-proxy's
    // names: normal, left-up, bottom-up, right-up), through the device's hook.
    Q_INVOKABLE void rotate(const QString &orientation) const;

private:
    deviceinfo::Device m_device;
};
