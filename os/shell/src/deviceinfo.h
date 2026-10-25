// The device description, /etc/mobarchy/device.toml (devices/README.md): the shell reads
// what it must know about the hardware from there, never from a codename.
#pragma once

#include <QList>
#include <QVariantMap>

namespace deviceinfo {

struct Device {
    QVariantMap tables;          // "device", "input", ...: key -> value
    QList<QVariantMap> displays; // [[display]] entries, in order
};

// The subset of TOML that device.toml uses: [table], [[display]], and key = string,
// integer, float or boolean, with # comments.
Device parse(const QByteArray &toml);

// The display with this connector (QScreen::name(), e.g. "DSI-1"). A device with a single
// display and no connector names (the simulator) has that one. Empty if none matches.
QVariantMap display(const Device &device, const QString &connector);

// Logical pixels at the top of a display that content keeps clear of (camera cutout,
// rounded corners): `top_inset`. 0 if not set.
int topInset(const Device &device, const QString &connector);
// The same at the left and right edges (panel under the bezel): `side_inset`.
int sideInset(const Device &device, const QString &connector);
// The same at the bottom edge: `bottom_inset`.
int bottomInset(const Device &device, const QString &connector);
// A camera's hole in the panel, upright: [centre x, centre y, radius] in logical pixels
// (`cutout`); empty if none.
QList<qreal> cutout(const Device &device, const QString &connector);

}  // namespace deviceinfo
