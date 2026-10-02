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

private:
    deviceinfo::Device m_device;
};
