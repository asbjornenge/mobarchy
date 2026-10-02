#include "rfkill.h"

#include <QDebug>

#include <fcntl.h>
#include <linux/rfkill.h>
#include <unistd.h>

Rfkill::Rfkill(QObject *parent)
    : QObject(parent)
{
    m_fd = ::open("/dev/rfkill", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        qWarning() << "rfkill: cannot open /dev/rfkill";
        return;
    }
    // Opening it replays an ADD event for every radio, then CHANGE events follow.
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Rfkill::readEvents);
    readEvents();
}

Rfkill::~Rfkill()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

void Rfkill::readEvents()
{
    bool any = false;
    for (;;) {
        // The kernel may send a longer (extended) event; the first 8 bytes are the same.
        unsigned char buf[64];
        const ssize_t n = ::read(m_fd, buf, sizeof(buf));
        if (n < ssize_t(RFKILL_EVENT_SIZE_V1))
            break;
        rfkill_event e;
        memcpy(&e, buf, RFKILL_EVENT_SIZE_V1);
        if (e.op == RFKILL_OP_DEL)
            m_radios.remove(e.idx);
        else if (e.op == RFKILL_OP_ADD || e.op == RFKILL_OP_CHANGE)
            m_radios.insert(e.idx, {e.type, e.soft != 0, e.hard != 0});
        any = true;
    }
    if (any)
        emit changed();
}

void Rfkill::changeAll(quint8 type, bool block)
{
    if (m_fd < 0)
        return;
    rfkill_event e = {};
    e.type = type;
    e.op = RFKILL_OP_CHANGE_ALL;
    e.soft = block ? 1 : 0;
    if (::write(m_fd, &e, RFKILL_EVENT_SIZE_V1) != ssize_t(RFKILL_EVENT_SIZE_V1))
        qWarning() << "rfkill: change failed";
}

void Rfkill::change(quint32 idx, bool block)
{
    if (m_fd < 0)
        return;
    rfkill_event e = {};
    e.idx = idx;
    e.op = RFKILL_OP_CHANGE;
    e.soft = block ? 1 : 0;
    if (::write(m_fd, &e, RFKILL_EVENT_SIZE_V1) != ssize_t(RFKILL_EVENT_SIZE_V1))
        qWarning() << "rfkill: change failed";
}

bool Rfkill::flightMode() const
{
    if (m_radios.isEmpty())
        return false;
    return std::all_of(m_radios.cbegin(), m_radios.cend(), [](const Radio &r) { return r.soft || r.hard; });
}

void Rfkill::setFlightMode(bool on)
{
    if (on) {
        m_beforeFlight.clear();
        for (auto it = m_radios.cbegin(); it != m_radios.cend(); ++it) {
            if (!it->soft)
                m_beforeFlight << it.key();
        }
        changeAll(RFKILL_TYPE_ALL, true);
        return;
    }
    // Restore what was on before. Without that (the shell restarted meanwhile), bring back
    // Wi-Fi and the modem; Bluetooth stays off until it is switched on.
    if (!m_beforeFlight.isEmpty()) {
        for (quint32 idx : std::as_const(m_beforeFlight))
            change(idx, false);
        m_beforeFlight.clear();
    } else {
        changeAll(RFKILL_TYPE_WLAN, false);
        changeAll(RFKILL_TYPE_WWAN, false);
    }
}

bool Rfkill::has(quint8 type) const
{
    return std::any_of(m_radios.cbegin(), m_radios.cend(), [type](const Radio &r) { return r.type == type; });
}

// All switches of the type on (a Bluetooth controller has its own besides its power's).
bool Rfkill::enabled(quint8 type) const
{
    bool any = false;
    for (const Radio &r : m_radios) {
        if (r.type != type)
            continue;
        if (r.soft || r.hard)
            return false;
        any = true;
    }
    return any;
}

bool Rfkill::hasWifi() const { return has(RFKILL_TYPE_WLAN); }
bool Rfkill::wifiEnabled() const { return enabled(RFKILL_TYPE_WLAN); }
void Rfkill::setWifiEnabled(bool on) { changeAll(RFKILL_TYPE_WLAN, !on); }

bool Rfkill::hasBluetooth() const { return has(RFKILL_TYPE_BLUETOOTH); }
bool Rfkill::bluetoothEnabled() const { return enabled(RFKILL_TYPE_BLUETOOTH); }
void Rfkill::setBluetoothEnabled(bool on) { changeAll(RFKILL_TYPE_BLUETOOTH, !on); }
