// Pictures of the open windows, for the app switcher: ext-image-copy-capture-v1 of each
// window's ext-foreign-toplevel-list-v1 handle (ext-image-capture-source-v1), into shared
// memory. Standard protocols (Hyprland, Sway, ...), so no compositor-specific code (ADR 0004).
//
// The switcher lists windows from wlr-foreign-toplevel-management (it can switch and close);
// this list is another protocol's, so a picture is found by the window's app ID and title.
// QML asks for image://windows/<app ID>/<title>?<generation>.
//
// And a picture of a whole screen (ext_output_image_capture_source_manager_v1), for the
// switcher's background, blurred once in the shell: image://windows/screen?<generation>.
// (A compositor's blur behind the switcher is worked out again for every frame it draws,
// which halved the frame rate as the cards moved.)
#pragma once

#include "qwayland-ext-foreign-toplevel-list-v1.h"
#include "qwayland-ext-image-capture-source-v1.h"
#include "qwayland-ext-image-copy-capture-v1.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QQuickImageProvider>
#include <QtQml/qqmlregistration.h>
#include <QWindow>
#include <QtWaylandClient/QWaylandClientExtension>

struct wl_shm;
struct wl_registry;

class ExtToplevel : public QObject, public QtWayland::ext_foreign_toplevel_handle_v1
{
    Q_OBJECT

public:
    explicit ExtToplevel(struct ::ext_foreign_toplevel_handle_v1 *handle)
        : QtWayland::ext_foreign_toplevel_handle_v1(handle) {}
    ~ExtToplevel() override { destroy(); }
    QString key() const { return m_appId + QLatin1Char('\n') + m_title; }

signals:
    void closed();

protected:
    void ext_foreign_toplevel_handle_v1_title(const QString &title) override { m_title = title; }
    void ext_foreign_toplevel_handle_v1_app_id(const QString &appId) override { m_appId = appId; }
    void ext_foreign_toplevel_handle_v1_closed() override { emit closed(); }

private:
    QString m_title, m_appId;
};

class ExtToplevelList : public QWaylandClientExtensionTemplate<ExtToplevelList>,
                        public QtWayland::ext_foreign_toplevel_list_v1
{
    Q_OBJECT

public:
    ExtToplevelList() : QWaylandClientExtensionTemplate(1) {}
    QList<ExtToplevel *> toplevels;

protected:
    void ext_foreign_toplevel_list_v1_toplevel(struct ::ext_foreign_toplevel_handle_v1 *handle) override;
};

class CaptureSources : public QWaylandClientExtensionTemplate<CaptureSources>,
                       public QtWayland::ext_foreign_toplevel_image_capture_source_manager_v1
{
    Q_OBJECT

public:
    CaptureSources() : QWaylandClientExtensionTemplate(1) {}
};

class OutputSources : public QWaylandClientExtensionTemplate<OutputSources>,
                      public QtWayland::ext_output_image_capture_source_manager_v1
{
    Q_OBJECT

public:
    OutputSources() : QWaylandClientExtensionTemplate(1) {}
};

class CopyCapture : public QWaylandClientExtensionTemplate<CopyCapture>,
                    public QtWayland::ext_image_copy_capture_manager_v1
{
    Q_OBJECT

public:
    CopyCapture() : QWaylandClientExtensionTemplate(1) {}
};

class CaptureJob;

class WindowCapture : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Bumped when a picture is new: in an Image's source, it makes QML ask again.
    Q_PROPERTY(int generation READ generation NOTIFY captured)
    // (and the screen's)
    Q_PROPERTY(int screenGeneration READ screenGeneration NOTIFY screenCaptured)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)

public:
    explicit WindowCapture(QObject *parent = nullptr);
    ~WindowCapture() override;
    static WindowCapture *instance() { return s_instance; }

    int generation() const { return m_generation; }
    int screenGeneration() const { return m_screenGeneration; }
    bool supported() const;
    // Takes a picture of a window (it comes in when it's ready). A window out of sight
    // keeps the buffer it last drew, at its old size after a fold (Hyprland then scales it to
    // the new size: squeezed); so windows are pictured as they leave the front
    // (ToplevelModel.deactivated) and when in front, and a picture of another shape is
    // the window as it last looked.
    Q_INVOKABLE void capture(const QString &appId, const QString &title);
    // ... of every window there is no picture of yet
    Q_INVOKABLE void captureMissing();

    // Takes a picture of the screen the window is on (smaller: it is for a blurred
    // background).
    Q_INVOKABLE void captureScreen(QWindow *window);
    static QString screenKey() { return QStringLiteral("screen"); }
    QImage picture(const QString &key) const;

    // (CaptureJob)
    wl_shm *shm() const { return m_shm; }
    CopyCapture *copyCapture() const { return m_copy; }
    void finished(CaptureJob *job, const QString &key, const QImage &image);

signals:
    void captured();
    void screenCaptured();
    void supportedChanged();

private:
    static WindowCapture *s_instance;
    ExtToplevelList *m_list;
    CaptureSources *m_sources;
    CopyCapture *m_copy;
    OutputSources *m_outputs;
    CaptureJob *m_screenJob = nullptr;
    wl_registry *m_registry = nullptr;
    wl_shm *m_shm = nullptr;
    QList<CaptureJob *> m_jobs;
    QSet<QString> m_busy;        // windows a picture is being taken of
    void start(ExtToplevel *t);
    mutable QMutex m_lock;      // (the image provider asks from QML's image thread)
    QHash<QString, QImage> m_pictures;
    int m_generation = 0;
    int m_screenGeneration = 0;
    friend struct RegistryListener;
};

class WindowImages : public QQuickImageProvider
{
public:
    WindowImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};
