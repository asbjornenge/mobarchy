// QML access to /etc/mobarchy/device.toml.
#pragma once

#include "deviceinfo.h"

#include <QObject>
#include <QRectF>
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
    // The inset at an edge of the screen as it is turned now (edge: "top", "right",
    // "bottom", "left"; turns: quarter turns clockwise of the device, Rotation.turns): the
    // panel's own insets (top_inset at its top, side_inset at its sides, bottom_inset at its
    // bottom) move with it.
    Q_INVOKABLE int inset(const QString &connector, const QString &edge, int turns) const;
    // The display's camera hole (device.toml cutout) on the screen as it is turned now: its
    // bounds in the screen's logical pixels (width x height as it is now); empty if none.
    Q_INVOKABLE QRectF cutout(const QString &connector, int turns, qreal width, qreal height) const;
    // Rotation (ADR 0020): whether the device can turn its screens ([rotation] hook), and
    // whether a display's rotation is locked until the owner unlocks it (rotation_locked).
    Q_INVOKABLE bool canRotate() const;
    Q_INVOKABLE bool rotationLockedByDefault(const QString &connector) const;
    // Turns the screen in use for the device held as orientation (iio-sensor-proxy's
    // names: normal, left-up, bottom-up, right-up), through the device's hook.
    Q_INVOKABLE void rotate(const QString &orientation) const;
    // A foldable's folding panel: where its fold runs on the panel upright ("vertical": a
    // book; "horizontal": a flip phone), from the display's fold_axis; "" for a flat one.
    Q_INVOKABLE QString foldAxis(const QString &connector) const;

private:
    deviceinfo::Device m_device;
};
