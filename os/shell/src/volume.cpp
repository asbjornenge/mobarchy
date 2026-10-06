#include "volume.h"

#include <pulse/pulseaudio.h>

namespace {
// What kind of output a sink is: a phone's own speaker, wired headphones, Bluetooth.
QString kindOf(const pa_sink_info *sink)
{
    const char *bus = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_BUS);
    const char *form = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_FORM_FACTOR);
    if (bus && qstrcmp(bus, "bluetooth") == 0)
        return QStringLiteral("bluetooth");
    if (QByteArray(sink->name).startsWith("bluez_output."))
        return QStringLiteral("bluetooth");
    if (form && (qstrcmp(form, "headphone") == 0 || qstrcmp(form, "headset") == 0))
        return QStringLiteral("headphones");
    if (sink->active_port && sink->active_port->type == PA_DEVICE_PORT_TYPE_HEADPHONES)
        return QStringLiteral("headphones");
    return QStringLiteral("speaker");
}
}  // namespace

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
        pa_context_set_subscribe_callback(c, [](pa_context *, pa_subscription_event_type_t t, uint32_t index, void *self) {
            auto *v = static_cast<Volume *>(self);
            if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK
                && (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW)
                v->sinkAdded(index);
            v->refresh();
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

    // The outputs, gathered here and handed over at the end of the list.
    struct Listing {
        Volume *v;
        QVariantList outputs;
    };
    op = pa_context_get_sink_info_list(m_context, [](pa_context *, const pa_sink_info *sink, int eol, void *data) {
        auto *l = static_cast<Listing *>(data);
        if (eol) {
            Volume *v = l->v;
            QVariantList outputs = std::move(l->outputs);
            delete l;
            QMetaObject::invokeMethod(v, [v, outputs] {
                if (outputs != v->m_outputs) {
                    v->m_outputs = outputs;
                    emit v->outputsChanged();
                }
            }, Qt::QueuedConnection);
            return;
        }
        l->outputs.append(QVariantMap{
            {QStringLiteral("name"), QString::fromUtf8(sink->name)},
            {QStringLiteral("description"), QString::fromUtf8(sink->description)},
            {QStringLiteral("kind"), kindOf(sink)},
        });
    }, new Listing{this, {}});
    if (op)
        pa_operation_unref(op);
}

// PulseAudio thread: a sink came. Headphones and Bluetooth take over (the owner just put them
// on); a speaker coming back doesn't.
void Volume::sinkAdded(uint32_t index)
{
    pa_operation *op = pa_context_get_sink_info_by_index(m_context, index,
        [](pa_context *, const pa_sink_info *sink, int eol, void *self) {
            if (eol || !sink || kindOf(sink) == QLatin1String("speaker"))
                return;
            auto *v = static_cast<Volume *>(self);
            const QString name = QString::fromUtf8(sink->name);
            QMetaObject::invokeMethod(v, [v, name] { v->setOutput(name); }, Qt::QueuedConnection);
        }, this);
    if (op)
        pa_operation_unref(op);
}

void Volume::setOutput(const QString &name)
{
    if (!m_context || name.isEmpty())
        return;
    const QByteArray sink = name.toUtf8();
    qInfo() << "volume: sound goes to" << name;
    pa_threaded_mainloop_lock(m_loop);
    // The default (where new sounds go, remembered by WirePlumber), and what plays now.
    if (pa_operation *op = pa_context_set_default_sink(m_context, sink.constData(), nullptr, nullptr))
        pa_operation_unref(op);
    struct Move {
        QByteArray sink;
    };
    pa_operation *op = pa_context_get_sink_input_info_list(m_context,
        [](pa_context *c, const pa_sink_input_info *input, int eol, void *data) {
            auto *m = static_cast<Move *>(data);
            if (eol) {
                delete m;
                return;
            }
            if (pa_operation *op = pa_context_move_sink_input_by_name(c, input->index, m->sink.constData(), nullptr, nullptr))
                pa_operation_unref(op);
        }, new Move{sink});
    if (op)
        pa_operation_unref(op);
    pa_threaded_mainloop_unlock(m_loop);
}

void Volume::update(bool available, const QByteArray &sink, int channels, qreal volume, bool muted)
{
    if (sink != m_sink) {
        m_sink = sink;
        emit outputsChanged();
    }
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
