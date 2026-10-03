// The clipboard, for the keyboard's paste button and its history (ADR 0016), over
// wlr-data-control: the shell sees every copy in the session (text only) and can set the
// clipboard itself. The history is kept in memory only: copies are often passwords and
// codes. Copies a password manager marks as secret are offered once but not kept.
#pragma once

#include "qwayland-wlr-data-control-unstable-v1.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSocketNotifier>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtension>

class DataControlManager : public QWaylandClientExtensionTemplate<DataControlManager>,
                           public QtWayland::zwlr_data_control_manager_v1
{
    Q_OBJECT
public:
    DataControlManager() : QWaylandClientExtensionTemplate<DataControlManager>(2) {}
    void bind() { initialize(); }
};

class ClipboardOffer : public QtWayland::zwlr_data_control_offer_v1
{
public:
    explicit ClipboardOffer(struct ::zwlr_data_control_offer_v1 *object)
        : QtWayland::zwlr_data_control_offer_v1(object) {}
    ~ClipboardOffer() override { destroy(); }
    QStringList mimeTypes;

protected:
    void zwlr_data_control_offer_v1_offer(const QString &mime) override { mimeTypes << mime; }
};

class Clipboard;

class ClipboardDevice : public QtWayland::zwlr_data_control_device_v1
{
public:
    ClipboardDevice(struct ::zwlr_data_control_device_v1 *object, Clipboard *clipboard)
        : QtWayland::zwlr_data_control_device_v1(object), m_clipboard(clipboard) {}
    ~ClipboardDevice() override;

protected:
    void zwlr_data_control_device_v1_data_offer(struct ::zwlr_data_control_offer_v1 *id) override;
    void zwlr_data_control_device_v1_selection(struct ::zwlr_data_control_offer_v1 *id) override;
    void zwlr_data_control_device_v1_primary_selection(struct ::zwlr_data_control_offer_v1 *id) override;
    void zwlr_data_control_device_v1_finished() override;

private:
    Clipboard *m_clipboard;
    QHash<struct ::zwlr_data_control_offer_v1 *, ClipboardOffer *> m_offers;
};

class ClipboardSource : public QtWayland::zwlr_data_control_source_v1
{
public:
    ClipboardSource(struct ::zwlr_data_control_source_v1 *object, const QByteArray &text)
        : QtWayland::zwlr_data_control_source_v1(object), m_text(text) {}
    ~ClipboardSource() override { destroy(); }
    bool cancelled = false;

protected:
    void zwlr_data_control_source_v1_send(const QString &mime, int32_t fd) override;
    void zwlr_data_control_source_v1_cancelled() override { cancelled = true; }

private:
    QByteArray m_text;
};

class Clipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // What is on the clipboard now ("" if no text).
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // Earlier copies, newest first (text includes the current one).
    Q_PROPERTY(QStringList history READ history NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)

public:
    explicit Clipboard(QObject *parent = nullptr);
    ~Clipboard() override;

    QString text() const { return m_text; }
    QStringList history() const { return m_history; }
    bool supported() const { return m_device != nullptr; }

    // Put text on the clipboard (e.g. an earlier copy, to paste it in an app).
    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE void clearHistory();

    static constexpr int HistorySize = 20;
    static constexpr int MaxBytes = 1 << 20;

signals:
    void changed();
    void supportedChanged();

private:
    friend class ClipboardDevice;
    void setup();
    void onSelection(ClipboardOffer *offer);
    void received(const QByteArray &data, bool keep);

    DataControlManager *m_manager;
    ClipboardDevice *m_device = nullptr;
    ClipboardSource *m_source = nullptr;
    QPointer<QSocketNotifier> m_reading;
    QByteArray m_buffer;
    QString m_text;
    QStringList m_history;
};
