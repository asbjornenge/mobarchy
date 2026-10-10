#include "windowcapture.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTransform>
#include <QUrl>
#include <QWindow>

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

WindowCapture *WindowCapture::s_instance = nullptr;

namespace {
// The width a picture is kept at: a card is a fraction of the screen.
constexpr int PictureWidth = 540;
// A screen's, for a blurred background
constexpr int ScreenWidth = 360;

// The buffer as it is shown: the compositor gave its transform (wl_output.transform: n
// quarter turns counter-clockwise, 4-7 flipped as well); undone.
QImage untransformed(const QImage &image, uint32_t transform)
{
    if (transform == WL_OUTPUT_TRANSFORM_NORMAL)
        return image;
    QImage out = image.transformed(QTransform().rotate(90.0 * (transform % 4)));
    return transform >= WL_OUTPUT_TRANSFORM_FLIPPED ? out.flipped(Qt::Horizontal) : out;
}
}

void ExtToplevelList::ext_foreign_toplevel_list_v1_toplevel(struct ::ext_foreign_toplevel_handle_v1 *handle)
{
    auto *t = new ExtToplevel(handle);
    toplevels << t;
    QObject::connect(t, &ExtToplevel::closed, t, [this, t] {
        toplevels.removeOne(t);
        t->deleteLater();
    });
}

// --- one picture: a session for one window, one frame into shared memory -----------------

class CaptureFrame : public QtWayland::ext_image_copy_capture_frame_v1
{
public:
    std::function<void(bool)> done;
    uint32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
    explicit CaptureFrame(struct ::ext_image_copy_capture_frame_v1 *f) : QtWayland::ext_image_copy_capture_frame_v1(f) {}
    ~CaptureFrame() override { destroy(); }

protected:
    void ext_image_copy_capture_frame_v1_transform(uint32_t t) override { transform = t; }
    void ext_image_copy_capture_frame_v1_ready() override { done(true); }
    void ext_image_copy_capture_frame_v1_failed(uint32_t) override { done(false); }
};

class CaptureJob : public QtWayland::ext_image_copy_capture_session_v1
{
public:
    // width: the picture's (at most); a fast scaling where it will be blurred anyway
    CaptureJob(WindowCapture *owner, const QString &key, struct ::ext_image_copy_capture_session_v1 *s,
               struct ::ext_image_capture_source_v1 *source, int width = PictureWidth, bool fast = false)
        : QtWayland::ext_image_copy_capture_session_v1(s), m_owner(owner), m_key(key), m_source(source),
          m_pictureWidth(width), m_fast(fast) {}

    ~CaptureJob() override
    {
        delete m_frame;
        if (m_buffer)
            wl_buffer_destroy(m_buffer);
        if (m_pool)
            wl_shm_pool_destroy(m_pool);
        if (m_data)
            munmap(m_data, m_bytes);
        destroy();
        ext_image_capture_source_v1_destroy(m_source);
    }

protected:
    void ext_image_copy_capture_session_v1_buffer_size(uint32_t width, uint32_t height) override
    {
        m_width = int(width);
        m_height = int(height);
    }
    void ext_image_copy_capture_session_v1_shm_format(uint32_t format) override
    {
        if (format == WL_SHM_FORMAT_ARGB8888 || (format == WL_SHM_FORMAT_XRGB8888 && m_format < 0))
            m_format = int(format);
    }
    void ext_image_copy_capture_session_v1_done() override
    {
        if (m_frame)
            return;  // (constraints again, while a frame is out: it fails and we start over later)
        if (!start())
            m_owner->finished(this, m_key, {});
    }
    void ext_image_copy_capture_session_v1_stopped() override { m_owner->finished(this, m_key, {}); }

private:
    bool start()
    {
        if (m_format < 0 || m_width <= 0 || m_height <= 0 || !m_owner->shm())
            return false;
        const int stride = m_width * 4;
        m_bytes = size_t(stride) * size_t(m_height);
        const int fd = memfd_create("mobarchy-window", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, off_t(m_bytes)) < 0) {
            if (fd >= 0)
                close(fd);
            return false;
        }
        m_data = mmap(nullptr, m_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m_data == MAP_FAILED) {
            m_data = nullptr;
            close(fd);
            return false;
        }
        m_pool = wl_shm_create_pool(m_owner->shm(), fd, int32_t(m_bytes));
        close(fd);
        m_buffer = wl_shm_pool_create_buffer(m_pool, 0, m_width, m_height, stride, uint32_t(m_format));
        m_frame = new CaptureFrame(create_frame());
        m_frame->done = [this, stride](bool ok) {
            QImage image;
            if (ok) {
                const QImage raw(static_cast<const uchar *>(m_data), m_width, m_height, stride,
                                 m_format == WL_SHM_FORMAT_ARGB8888 ? QImage::Format_ARGB32 : QImage::Format_RGB32);
                image = untransformed(raw.scaledToWidth(qMin(m_pictureWidth, m_width),
                                                        m_fast ? Qt::FastTransformation : Qt::SmoothTransformation),
                                      m_frame->transform);
            }
            m_owner->finished(this, m_key, image);
        };
        m_frame->attach_buffer(m_buffer);
        m_frame->damage_buffer(0, 0, m_width, m_height);
        m_frame->capture();
        return true;
    }

    WindowCapture *m_owner;
    QString m_key;
    struct ::ext_image_capture_source_v1 *m_source;
    int m_pictureWidth;
    bool m_fast;
    int m_width = 0, m_height = 0, m_format = -1;
    size_t m_bytes = 0;
    void *m_data = nullptr;
    wl_shm_pool *m_pool = nullptr;
    wl_buffer *m_buffer = nullptr;
    CaptureFrame *m_frame = nullptr;
};

// --- the manager -----------------------------------------------------------------------

struct RegistryListener {
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t)
    {
        auto *self = static_cast<WindowCapture *>(data);
        if (qstrcmp(interface, wl_shm_interface.name) == 0 && !self->m_shm)
            self->m_shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    }
    static void remove(void *, wl_registry *, uint32_t) {}
};

static const wl_registry_listener registryListener = {RegistryListener::global, RegistryListener::remove};

WindowCapture::WindowCapture(QObject *parent)
    : QObject(parent), m_list(new ExtToplevelList), m_sources(new CaptureSources), m_copy(new CopyCapture),
      m_outputs(new OutputSources)
{
    s_instance = this;
    m_list->setParent(this);
    m_sources->setParent(this);
    m_copy->setParent(this);
    m_outputs->setParent(this);
    for (QWaylandClientExtension *e : {static_cast<QWaylandClientExtension *>(m_list),
                                       static_cast<QWaylandClientExtension *>(m_sources),
                                       static_cast<QWaylandClientExtension *>(m_copy)})
        connect(e, &QWaylandClientExtension::activeChanged, this, &WindowCapture::supportedChanged);
    if (auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        m_registry = wl_display_get_registry(app->display());
        wl_registry_add_listener(m_registry, &registryListener, this);
    }
}

WindowCapture::~WindowCapture()
{
    qDeleteAll(m_jobs);
    delete m_screenJob;
    if (m_shm)
        wl_shm_destroy(m_shm);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (s_instance == this)
        s_instance = nullptr;
}

bool WindowCapture::supported() const
{
    return m_list->isActive() && m_sources->isActive() && m_copy->isActive();
}

void WindowCapture::captureAll()
{
    if (!supported() || !m_jobs.isEmpty())
        return;
    for (ExtToplevel *t : std::as_const(m_list->toplevels)) {
        auto *source = m_sources->create_source(t->object());
        auto *job = new CaptureJob(this, t->key(), m_copy->create_session(source, 0), source);
        m_jobs << job;
    }
}

void WindowCapture::captureScreen(QWindow *window)
{
    if (m_screenJob || !window || !window->screen() || !m_outputs->isActive() || !m_copy->isActive())
        return;
    auto *screen = window->screen()->nativeInterface<QNativeInterface::QWaylandScreen>();
    if (!screen || !screen->output())
        return;
    auto *source = m_outputs->create_source(screen->output());
    m_screenJob = new CaptureJob(this, screenKey(), m_copy->create_session(source, 0), source, ScreenWidth, true);
}

void WindowCapture::finished(CaptureJob *job, const QString &key, const QImage &image)
{
    m_jobs.removeOne(job);
    const bool screen = job == m_screenJob;
    if (screen)
        m_screenJob = nullptr;
    // (not from inside the job's own callback)
    QMetaObject::invokeMethod(this, [job] { delete job; }, Qt::QueuedConnection);
    if (image.isNull())
        return;
    {
        QMutexLocker locker(&m_lock);
        m_pictures.insert(key, image);
    }
    if (screen) {
        m_screenGeneration++;
        emit screenCaptured();
    } else {
        m_generation++;
        emit captured();
    }
}

QImage WindowCapture::picture(const QString &key) const
{
    QMutexLocker locker(&m_lock);
    return m_pictures.value(key);
}

QImage WindowImages::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    // <app ID>/<title>?<generation>, each part percent-encoded
    const QString path = id.section(QLatin1Char('?'), 0, 0);
    const QString key = path == WindowCapture::screenKey() ? path : QUrl::fromPercentEncoding(path.section(QLatin1Char('/'), 0, 0).toUtf8())
                        + QLatin1Char('\n') + QUrl::fromPercentEncoding(path.section(QLatin1Char('/'), 1).toUtf8());
    QImage image = WindowCapture::instance() ? WindowCapture::instance()->picture(key) : QImage();
    if (image.isNull())
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied), image.fill(Qt::transparent);
    if (requested.isValid() && requested.width() > 0)
        image = image.scaledToWidth(requested.width(), Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
