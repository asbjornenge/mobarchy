#include "camera.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#include <cstring>

namespace {
const QString Service = QStringLiteral("org.mobarchy.Camera");
const QString Path = QStringLiteral("/org/mobarchy/Camera");
const QString Interface = QStringLiteral("org.mobarchy.Camera");
constexpr int PreviewWidth = 1280;      // the source's sizes: 1920x1080, 1280x720, 640x480
constexpr int PreviewHeight = 720;
} // namespace

CameraSource::CameraSource(QObject *parent)
    : QObject(parent)
{
    pw_init(nullptr, nullptr);
    m_loop = pw_thread_loop_new("camera", nullptr);
    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    pw_thread_loop_start(m_loop);
    pw_thread_loop_lock(m_loop);
    m_core = pw_context_connect(m_context, nullptr, 0);
    pw_thread_loop_unlock(m_loop);
    if (!m_core)
        qWarning("camera: can't connect to PipeWire");
    refresh();
}

CameraSource::~CameraSource()
{
    stopStream();
    pw_thread_loop_stop(m_loop);
    if (m_core)
        pw_core_disconnect(m_core);
    pw_context_destroy(m_context);
    pw_thread_loop_destroy(m_loop);
}

void CameraSource::refresh()
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("ListCameras"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage) {
            emit failed(tr("The cameras aren't available: %1").arg(reply.errorMessage()));
            return;
        }
        m_cameras.clear();
        const QDBusArgument arg = reply.arguments().value(0).value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            QString id, facing;
            uint orientation = 0, maxW = 0, maxH = 0;
            arg.beginStructure();
            arg >> id >> facing >> orientation >> maxW >> maxH;
            arg.endStructure();
            m_cameras << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("facing"), facing},
                                     {QStringLiteral("orientation"), orientation}};
        }
        arg.endArray();
        emit camerasChanged();
        if (m_current.isEmpty() && !m_cameras.isEmpty())
            setCurrent(m_cameras.first().toMap().value(QStringLiteral("id")).toString());
    });
}

QVariantMap CameraSource::currentInfo() const
{
    for (const QVariant &c : m_cameras)
        if (c.toMap().value(QStringLiteral("id")).toString() == m_current)
            return c.toMap();
    return {};
}

QString CameraSource::facing() const { return currentInfo().value(QStringLiteral("facing")).toString(); }
int CameraSource::orientation() const { return currentInfo().value(QStringLiteral("orientation")).toInt(); }

void CameraSource::setCurrent(const QString &id)
{
    if (id == m_current)
        return;
    m_current = id;
    emit currentChanged();
    restartStream();
}

void CameraSource::switchCamera()
{
    for (int i = 0; i < m_cameras.size(); i++)
        if (m_cameras[i].toMap().value(QStringLiteral("id")).toString() == m_current) {
            setCurrent(m_cameras[(i + 1) % m_cameras.size()].toMap().value(QStringLiteral("id")).toString());
            return;
        }
}

void CameraSource::setVideoSink(QVideoSink *sink)
{
    if (sink == m_sink)
        return;
    m_sink = sink;
    emit videoSinkChanged();
}

void CameraSource::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    restartStream();
}

void CameraSource::takePhoto()
{
    if (m_busy || m_current.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();
    auto msg = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("TakePhoto"));
    msg << m_current << 0u << 0u    // the default size
        << uint((m_deviceRotation % 360 + 360) % 360);
    // a photo takes a few seconds (exposure settles first)
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 30000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        m_busy = false;
        emit busyChanged();
        if (reply.isError()) {
            emit failed(reply.error().message());
            return;
        }
        m_lastPhoto = reply.value();
        emit lastPhotoChanged();
        emit photoTaken(m_lastPhoto);
        restartStream();        // the photo may have taken the camera from the preview
    });
}

bool CameraSource::deletePhoto(const QString &path)
{
    // only the camera's own photos (~/Pictures/Camera), whatever the caller asks
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                           + QStringLiteral("/Camera");
    if (QFileInfo(path).absolutePath() != QFileInfo(folder).absoluteFilePath())
        return false;
    if (!QFile::moveToTrash(path)) {
        emit failed(tr("Couldn't delete the photo"));
        return false;
    }
    if (path == m_lastPhoto) {
        m_lastPhoto.clear();
        emit lastPhotoChanged();
    }
    return true;
}

// --- the PipeWire stream --------------------------------------------------------------------

void CameraSource::onParamChanged(void *data, uint32_t id, const struct spa_pod *param)
{
    auto *self = static_cast<CameraSource *>(data);
    spa_video_info_raw info{};

    if (!param || id != SPA_PARAM_Format || spa_format_video_raw_parse(param, &info) < 0)
        return;
    self->m_width = int(info.size.width);
    self->m_height = int(info.size.height);
}

void CameraSource::onProcess(void *data)
{
    auto *self = static_cast<CameraSource *>(data);
    pw_buffer *b = pw_stream_dequeue_buffer(self->m_stream);
    if (!b)
        return;
    const spa_data &d = b->buffer->datas[0];
    const int w = self->m_width, h = self->m_height;
    if (d.data && w > 0 && d.chunk->size >= uint32_t(w * h * 3 / 2)) {
        // I420 (planar, rows of w bytes) into a QVideoFrame: VideoOutput converts it on the GPU
        QVideoFrame frame(QVideoFrameFormat(QSize(w, h), QVideoFrameFormat::Format_YUV420P));
        if (frame.map(QVideoFrame::WriteOnly)) {
            const auto *src = static_cast<const uint8_t *>(d.data) + d.chunk->offset;
            for (int p = 0; p < 3; p++) {
                const int pw = p ? w / 2 : w, ph = p ? h / 2 : h;
                uint8_t *dst = frame.bits(p);
                for (int y = 0; y < ph; y++)
                    memcpy(dst + y * frame.bytesPerLine(p), src + y * pw, pw);
                src += pw * ph;
            }
            frame.unmap();
            QPointer<QVideoSink> sink = self->m_sink;
            QMetaObject::invokeMethod(self, [sink, frame] {
                if (sink)
                    sink->setVideoFrame(frame);
            }, Qt::QueuedConnection);
        }
    }
    pw_stream_queue_buffer(self->m_stream, b);
}

void CameraSource::stopStream()
{
    if (!m_stream)
        return;
    pw_thread_loop_lock(m_loop);
    pw_stream_destroy(m_stream);
    m_stream = nullptr;
    pw_thread_loop_unlock(m_loop);
    delete static_cast<spa_hook *>(m_listener);
    m_listener = nullptr;
}

void CameraSource::restartStream()
{
    static const pw_stream_events events = [] {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.process = &CameraSource::onProcess;
        e.param_changed = &CameraSource::onParamChanged;
        return e;
    }();

    stopStream();
    if (!m_active || m_current.isEmpty() || !m_core)
        return;
    const QByteArray target = "mobarchy-camera-" + m_current.toUtf8();
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    spa_rectangle size = SPA_RECTANGLE(PreviewWidth, PreviewHeight);
    spa_rectangle min = SPA_RECTANGLE(1, 1), max = SPA_RECTANGLE(4096, 4096);
    spa_fraction rate = SPA_FRACTION(30, 1);
    const spa_pod *params[1];
    params[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_I420),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&size, &min, &max),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&rate)));

    m_listener = new spa_hook{};
    pw_thread_loop_lock(m_loop);
    m_stream = pw_stream_new(m_core, "Camera", pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Camera",
        PW_KEY_TARGET_OBJECT, target.constData(), nullptr));
    pw_stream_add_listener(m_stream, static_cast<spa_hook *>(m_listener), &events, this);
    pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                      pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
    pw_thread_loop_unlock(m_loop);
}
