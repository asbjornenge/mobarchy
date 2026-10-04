#include "haptics.h"

#include "deviceinfo.h"

#include <QDir>
#include <QFile>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr int ClickMs = 20;     // the buzz, when the device has no click of its own

bool testBit(const unsigned long *bits, int n)
{
    constexpr int bitsPer = 8 * sizeof(unsigned long);
    return bits[n / bitsPer] & (1UL << (n % bitsPer));
}

}  // namespace

Haptics::Haptics(QObject *parent)
    : QObject(parent)
{
}

Haptics::~Haptics()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool Haptics::open()
{
    if (m_tried)
        return m_effect >= 0;
    m_tried = true;

    QFile f(qEnvironmentVariable("MOBARCHY_DEVICE_TOML", QStringLiteral("/etc/mobarchy/device.toml")));
    const QVariantMap haptics = f.open(QIODevice::ReadOnly)
        ? deviceinfo::parse(f.readAll()).tables.value(QStringLiteral("haptics")).toMap() : QVariantMap();

    const QStringList events = QDir(QStringLiteral("/dev/input")).entryList({QStringLiteral("event*")}, QDir::System);
    for (const QString &name : events) {
        const int fd = ::open(QFile::encodeName(QStringLiteral("/dev/input/") + name).constData(),
                              O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            continue;
        unsigned long ff[(FF_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
        if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) < 0 || !testBit(ff, FF_PERIODIC) && !testBit(ff, FF_RUMBLE)) {
            ::close(fd);
            continue;
        }
        ff_effect effect = {};
        effect.id = -1;
        int16_t custom[2] = {};
        if (testBit(ff, FF_PERIODIC) && testBit(ff, FF_CUSTOM) && haptics.contains(QStringLiteral("click_index"))) {
            custom[0] = int16_t(haptics.value(QStringLiteral("click_bank")).toInt());
            custom[1] = int16_t(haptics.value(QStringLiteral("click_index")).toInt());
            effect.type = FF_PERIODIC;
            effect.u.periodic.waveform = FF_CUSTOM;
            effect.u.periodic.custom_len = 2;
            effect.u.periodic.custom_data = custom;
        } else if (testBit(ff, FF_RUMBLE)) {
            effect.type = FF_RUMBLE;
            effect.u.rumble.strong_magnitude = 0xc000;
            effect.replay.length = ClickMs;
        } else {
            effect.type = FF_PERIODIC;
            effect.u.periodic.waveform = FF_SINE;
            effect.u.periodic.period = 5;           // ms: 200 Hz
            effect.u.periodic.magnitude = 0x7fff;
            effect.replay.length = ClickMs;
        }
        if (ioctl(fd, EVIOCSFF, &effect) < 0) {
            qWarning("haptics: %s can't take the click effect", qPrintable(name));
            ::close(fd);
            continue;
        }
        m_fd = fd;
        m_effect = effect.id;
        return true;
    }
    return false;
}

void Haptics::click()
{
    if (!open())
        return;
    input_event ev = {};
    ev.type = EV_FF;
    ev.code = uint16_t(m_effect);
    ev.value = 1;
    if (::write(m_fd, &ev, sizeof ev) != sizeof ev)
        qWarning("haptics: can't play the click");
}
