#include "clipboard.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <fcntl.h>
#include <unistd.h>
#include <wayland-client.h>

namespace {
// The text types, in order of preference.
const QStringList TextTypes = {QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("UTF8_STRING"),
                               QStringLiteral("text/plain"), QStringLiteral("TEXT"), QStringLiteral("STRING")};
// Password managers mark secrets with this (KDE's convention, also used by KeePassXC).
const QString SecretHint = QStringLiteral("x-kde-passwordManagerHint");
// Our own copies carry this, so the shell doesn't read back what it just set.
const QString OwnType = QStringLiteral("application/x-mobarchy-shell");

wl_display *display()
{
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    return app ? app->display() : nullptr;
}
} // namespace

// --- device and source ---------------------------------------------------------------------

ClipboardDevice::~ClipboardDevice()
{
    qDeleteAll(m_offers);
    destroy();
}

void ClipboardDevice::zwlr_data_control_device_v1_data_offer(struct ::zwlr_data_control_offer_v1 *id)
{
    m_offers.insert(id, new ClipboardOffer(id));
}

void ClipboardDevice::zwlr_data_control_device_v1_selection(struct ::zwlr_data_control_offer_v1 *id)
{
    ClipboardOffer *offer = m_offers.take(id);
    // offers that were announced but not selected are stale now
    qDeleteAll(m_offers);
    m_offers.clear();
    m_clipboard->onSelection(offer);
}

void ClipboardDevice::zwlr_data_control_device_v1_primary_selection(struct ::zwlr_data_control_offer_v1 *id)
{
    delete m_offers.take(id);   // the middle-click selection: not ours to keep
}

void ClipboardDevice::zwlr_data_control_device_v1_finished()
{
    qWarning() << "clipboard: data control device finished";
}

void ClipboardSource::zwlr_data_control_source_v1_send(const QString &, int32_t fd)
{
    // Small texts (MaxBytes): written in one go.
    const char *p = m_text.constData();
    qsizetype left = m_text.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, size_t(left));
        if (n <= 0)
            break;
        p += n;
        left -= n;
    }
    ::close(fd);
}

// --- Clipboard -----------------------------------------------------------------------------

Clipboard::Clipboard(QObject *parent)
    : QObject(parent)
    , m_manager(new DataControlManager)
{
    m_manager->setParent(this);
    connect(m_manager, &DataControlManager::activeChanged, this, &Clipboard::setup);
    m_manager->bind();
}

Clipboard::~Clipboard()
{
    delete m_source;
    delete m_device;
}

void Clipboard::setup()
{
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (m_device || !m_manager->isActive() || !app || !app->seat())
        return;
    m_device = new ClipboardDevice(m_manager->get_data_device(app->seat()), this);
    emit supportedChanged();
}

void Clipboard::onSelection(ClipboardOffer *offer)
{
    delete m_reading;   // a read still running is for an older selection
    m_buffer.clear();
    if (!offer) {       // the clipboard was cleared (its owner went away)
        return;
    }
    if (offer->mimeTypes.contains(OwnType)) {
        delete offer;
        return;
    }
    QString type;
    for (const QString &t : TextTypes) {
        if (offer->mimeTypes.contains(t)) {
            type = t;
            break;
        }
    }
    if (type.isEmpty()) {   // not text (an image, files)
        delete offer;
        return;
    }
    const bool keep = !offer->mimeTypes.contains(SecretHint);
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        delete offer;
        return;
    }
    offer->receive(type, fds[1]);
    ::close(fds[1]);
    wl_display_flush(display());
    delete offer;

    auto *notifier = new QSocketNotifier(fds[0], QSocketNotifier::Read, this);
    m_reading = notifier;
    connect(notifier, &QSocketNotifier::activated, this, [this, notifier, keep](QSocketDescriptor fd) {
        char chunk[16384];
        for (;;) {
            const ssize_t n = ::read(int(fd), chunk, sizeof chunk);
            if (n > 0) {
                if (m_buffer.size() + n > MaxBytes) {   // too big for a text clipboard
                    m_buffer.clear();
                    break;
                }
                m_buffer.append(chunk, n);
                continue;
            }
            if (n < 0 && errno == EAGAIN)
                return;     // more to come
            break;          // end (or an error)
        }
        notifier->setEnabled(false);
        ::close(int(fd));
        received(m_buffer, keep);
        m_buffer.clear();
        notifier->deleteLater();
    });
}

void Clipboard::received(const QByteArray &data, bool keep)
{
    const QString text = QString::fromUtf8(data);
    if (text.isEmpty())
        return;
    m_text = text;
    if (keep) {
        m_history.removeAll(text);
        m_history.prepend(text);
        while (m_history.size() > HistorySize)
            m_history.removeLast();
    }
    emit changed();
}

void Clipboard::setText(const QString &text)
{
    if (!m_device || text.isEmpty())
        return;
    delete m_source;
    m_source = new ClipboardSource(m_manager->create_data_source(), text.toUtf8());
    for (const QString &t : TextTypes)
        m_source->offer(t);
    m_source->offer(OwnType);
    m_device->set_selection(m_source->object());
    m_text = text;
    m_history.removeAll(text);
    m_history.prepend(text);
    emit changed();
}

void Clipboard::clearHistory()
{
    m_history.clear();
    if (!m_text.isEmpty())
        m_history << m_text;
    emit changed();
}
