#include "volume.h"

#include <pulse/pulseaudio.h>

Volume::Volume(QObject *parent)
    : QObject(parent)
{
    m_loop = pa_threaded_mainloop_new();
    pa_threaded_mainloop_start(m_loop);
    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(2000);
    connect(&m_reconnect, &QTimer::timeout, this, &Volume::connectServer);
    connectServer();
}

Volume::~Volume()
{
    pa_threaded_mainloop_lock(m_loop);
    if (m_context) {
        pa_context_set_state_callback(m_context, nullptr, nullptr);
        pa_context_set_subscribe_callback(m_context, nullptr, nullptr);
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
        m_context = nullptr;
    }
    pa_threaded_mainloop_unlock(m_loop);
    pa_threaded_mainloop_stop(m_loop);
    pa_threaded_mainloop_free(m_loop);
}

void Volume::connectServer()
{
    pa_threaded_mainloop_lock(m_loop);
    if (m_context) {
        pa_context_set_state_callback(m_context, nullptr, nullptr);
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
    }
    m_context = pa_context_new(pa_threaded_mainloop_get_api(m_loop), "mobarchy-shell");
    pa_context_set_state_callback(m_context, &Volume::contextState, this);
    pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFAIL, nullptr);
    pa_threaded_mainloop_unlock(m_loop);
}

void Volume::contextState(pa_context *c, void *self)
{
    auto *v = static_cast<Volume *>(self);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        pa_context_set_subscribe_callback(c, [](pa_context *, pa_subscription_event_type_t, uint32_t, void *self) {
            static_cast<Volume *>(self)->refresh();
        }, v);
        pa_operation_unref(pa_context_subscribe(
            c, pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER), nullptr, nullptr));
        v->refresh();
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        // pipewire-pulse went away (restart): try again in a moment.
        QMetaObject::invokeMethod(v, [v] {
            v->update(false, {}, 2, 0, false);
            v->m_reconnect.start();
        }, Qt::QueuedConnection);
        break;
    default:
        break;
    }
}

// PulseAudio thread: the default sink's state, then hand it to the Qt thread.
void Volume::refresh()
{
    pa_operation *op = pa_context_get_server_info(m_context, [](pa_context *c, const pa_server_info *info, void *self) {
        if (!info || !info->default_sink_name)
            return;
        pa_operation *op2 = pa_context_get_sink_info_by_name(
            c, info->default_sink_name,
            [](pa_context *, const pa_sink_info *sink, int eol, void *self) {
                if (eol || !sink)
                    return;
                auto *v = static_cast<Volume *>(self);
                const QByteArray name(sink->name);
                const int channels = sink->volume.channels;
                const qreal volume = qreal(pa_cvolume_avg(&sink->volume)) / PA_VOLUME_NORM;
                const bool muted = sink->mute;
                QMetaObject::invokeMethod(v, [v, name, channels, volume, muted] {
                    v->update(true, name, channels, volume, muted);
                }, Qt::QueuedConnection);
            },
            self);
        if (op2)
            pa_operation_unref(op2);
    }, this);
    if (op)
        pa_operation_unref(op);
}

void Volume::update(bool available, const QByteArray &sink, int channels, qreal volume, bool muted)
{
    m_sink = sink;
    m_channels = channels > 0 ? channels : 2;
    if (available != m_available || !qFuzzyCompare(volume + 1, m_volume + 1) || muted != m_muted) {
        m_available = available;
        m_volume = volume;
        m_muted = muted;
        emit changed();
    }
}

void Volume::setVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (!m_available || m_sink.isEmpty())
        return;
    m_volume = volume;
    emit changed();
    pa_cvolume cv;
    pa_cvolume_set(&cv, uint8_t(m_channels), pa_volume_t(volume * PA_VOLUME_NORM));
    pa_threaded_mainloop_lock(m_loop);
    if (pa_operation *op = pa_context_set_sink_volume_by_name(m_context, m_sink.constData(), &cv, nullptr, nullptr))
        pa_operation_unref(op);
    // Moving the slider means "I want to hear it".
    if (m_muted) {
        if (pa_operation *op = pa_context_set_sink_mute_by_name(m_context, m_sink.constData(), 0, nullptr, nullptr))
            pa_operation_unref(op);
    }
    pa_threaded_mainloop_unlock(m_loop);
}

void Volume::setMuted(bool muted)
{
    if (!m_available || m_sink.isEmpty())
        return;
    pa_threaded_mainloop_lock(m_loop);
    if (pa_operation *op = pa_context_set_sink_mute_by_name(m_context, m_sink.constData(), muted, nullptr, nullptr))
        pa_operation_unref(op);
    pa_threaded_mainloop_unlock(m_loop);
}
