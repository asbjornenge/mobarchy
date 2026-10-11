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
constexpr int TickMs = 10;
constexpr int BuzzMs = 800;     // an alarm's, again and again

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

// One effect, uploaded: the device's waveform <name>_bank/<name>_index, else a buzz of
// <ms> at <strength> (of full). timed: the device's waveform plays for <ms> too (a long
// vibration), not its own length. -1 if it can't take it.
static int upload(int fd, const unsigned long *ff, const QVariantMap &haptics, const QString &name, int ms,
                  double strength, bool timed = false)
{
    ff_effect effect = {};
    effect.id = -1;
    int16_t custom[2] = {};
    if (testBit(ff, FF_PERIODIC) && testBit(ff, FF_CUSTOM) && haptics.contains(name + QStringLiteral("_index"))) {
        custom[0] = int16_t(haptics.value(name + QStringLiteral("_bank")).toInt());
        custom[1] = int16_t(haptics.value(name + QStringLiteral("_index")).toInt());
        effect.type = FF_PERIODIC;
        effect.u.periodic.waveform = FF_CUSTOM;
        effect.u.periodic.custom_len = 2;
        effect.u.periodic.custom_data = custom;
        if (timed)
            effect.replay.length = uint16_t(ms);
    } else if (testBit(ff, FF_RUMBLE)) {
        effect.type = FF_RUMBLE;
        effect.u.rumble.strong_magnitude = uint16_t(0xc000 * strength);
        effect.replay.length = uint16_t(ms);
    } else {
        effect.type = FF_PERIODIC;
        effect.u.periodic.waveform = FF_SINE;
        effect.u.periodic.period = 5;           // ms: 200 Hz
        effect.u.periodic.magnitude = int16_t(0x7fff * strength);
        effect.replay.length = uint16_t(ms);
    }
    return ioctl(fd, EVIOCSFF, &effect) < 0 ? -1 : effect.id;
}

bool Haptics::open()
{
    if (m_tried)
        return m_fd >= 0;
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
        m_click = upload(fd, ff, haptics, QStringLiteral("click"), ClickMs, 1.0);
        if (m_click < 0) {
            qWarning("haptics: %s can't take the click effect", qPrintable(name));
            ::close(fd);
            continue;
        }
        m_tick = upload(fd, ff, haptics, QStringLiteral("tick"), TickMs, 0.5);
        if (m_tick < 0)
            m_tick = m_click;
        m_buzz = upload(fd, ff, haptics, QStringLiteral("buzz"), BuzzMs, 1.0, true);
        m_fd = fd;
        return true;
    }
    return false;
}

void Haptics::click()
{
    if (open())
        play(m_click);
}

void Haptics::tick()
{
    if (open())
        play(m_tick);
}

void Haptics::buzz()
{
    if (open() && m_buzz >= 0)
        play(m_buzz);
}

void Haptics::play(int effect)
{
    input_event ev = {};
    ev.type = EV_FF;
    ev.code = uint16_t(effect);
    ev.value = 1;
    if (::write(m_fd, &ev, sizeof ev) != sizeof ev)
        qWarning("haptics: can't play an effect");
}
