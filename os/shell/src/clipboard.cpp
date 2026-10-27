#include "clipboard.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <fcntl.h>
#include <memory>
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
    // what is selected (marked) in an app: the shell offers to copy it
    m_clipboard->onPrimarySelection(m_offers.take(id));
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
    m_forget.setSingleShot(true);
    m_forget.setInterval(SecretSeconds * 1000);
    connect(&m_forget, &QTimer::timeout, this, &Clipboard::forget);
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
    // (emptied: a password manager clearing what it put there, or its owner gone)
    if (!offer)
        forget();
    read(offer, m_reading, [this](const QString &text, bool keep) { received(text, keep); });
}

void Clipboard::forget()
{
    m_forget.stop();
    if (m_text.isEmpty() && !m_secret)
        return;
    m_text.clear();
    m_secret = false;
    emit changed();
}

void Clipboard::onPrimarySelection(ClipboardOffer *offer)
{
    read(offer, m_readingPrimary, [this](const QString &text, bool keep) {
        if (!keep || text == m_selected)    // a secret is not offered for copying
            return;
        m_selected = text;
        // the selection there was when the shell started is old news
        if (m_primarySeen)
            emit selectedChanged();
        m_primarySeen = true;
    });
}

void Clipboard::read(ClipboardOffer *offer, QPointer<QSocketNotifier> &reading,
                     std::function<void(const QString &, bool)> done)
{
    if (reading) {      // a read still running is for an older selection
        if (reading->isEnabled())
            ::close(int(reading->socket()));
        delete reading;
    }
    if (!offer)         // cleared (its owner went away)
        return;
    QString type;
    if (!offer->mimeTypes.contains(OwnType)) {
        for (const QString &t : TextTypes) {
            if (offer->mimeTypes.contains(t)) {
                type = t;
                break;
            }
        }
    }
    const bool keep = !offer->mimeTypes.contains(SecretHint);
    int fds[2];
    if (type.isEmpty() || pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {   // ours, or not text
        delete offer;
        return;
    }
    offer->receive(type, fds[1]);
    ::close(fds[1]);
    wl_display_flush(display());
    delete offer;

    auto *notifier = new QSocketNotifier(fds[0], QSocketNotifier::Read, this);
    reading = notifier;
    auto buffer = std::make_shared<QByteArray>();
    connect(notifier, &QSocketNotifier::activated, this, [notifier, buffer, keep, done](QSocketDescriptor fd) {
        char chunk[16384];
        for (;;) {
            const ssize_t n = ::read(int(fd), chunk, sizeof chunk);
            if (n > 0) {
                if (buffer->size() + n > MaxBytes) {   // too big for a text clipboard
                    buffer->clear();
                    break;
                }
                buffer->append(chunk, n);
                continue;
            }
            if (n < 0 && errno == EAGAIN)
                return;     // more to come
            break;          // the end (or an error)
        }
        notifier->setEnabled(false);
        ::close(int(fd));
        done(QString::fromUtf8(*buffer), keep);
        notifier->deleteLater();
    });
}

void Clipboard::received(const QString &text, bool keep)
{
    if (text.isEmpty()) {
        forget();
        return;
    }
    m_text = text;
    m_secret = !keep;
    if (m_secret)
        m_forget.start();
    else
        m_forget.stop();
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
    m_secret = false;
    m_forget.stop();
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
