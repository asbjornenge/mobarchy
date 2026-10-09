// Pictures of the open windows, for the app switcher: ext-image-copy-capture-v1 of each
// window's ext-foreign-toplevel-list-v1 handle (ext-image-capture-source-v1), into shared
// memory. Standard protocols (Hyprland, Sway, ...), so no compositor-specific code (ADR 0004).
//
// The switcher lists windows from wlr-foreign-toplevel-management (it can switch and close);
// this list is another protocol's, so a picture is found by the window's app ID and title.
// QML asks for image://windows/<app ID>/<title>?<generation>.
#pragma once

#include "qwayland-ext-foreign-toplevel-list-v1.h"
#include "qwayland-ext-image-capture-source-v1.h"
#include "qwayland-ext-image-copy-capture-v1.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QtQml/qqmlregistration.h>
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
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)

public:
    explicit WindowCapture(QObject *parent = nullptr);
    ~WindowCapture() override;
    static WindowCapture *instance() { return s_instance; }

    int generation() const { return m_generation; }
    bool supported() const;
    // Takes a picture of every open window (each comes in as it's ready).
    Q_INVOKABLE void captureAll();
    QImage picture(const QString &key) const;

    // (CaptureJob)
    wl_shm *shm() const { return m_shm; }
    CopyCapture *copyCapture() const { return m_copy; }
    void finished(CaptureJob *job, const QString &key, const QImage &image);

signals:
    void captured();
    void supportedChanged();

private:
    static WindowCapture *s_instance;
    ExtToplevelList *m_list;
    CaptureSources *m_sources;
    CopyCapture *m_copy;
    wl_registry *m_registry = nullptr;
    wl_shm *m_shm = nullptr;
    QList<CaptureJob *> m_jobs;
    mutable QMutex m_lock;      // (the image provider asks from QML's image thread)
    QHash<QString, QImage> m_pictures;
    int m_generation = 0;
    friend struct RegistryListener;
};

class WindowImages : public QQuickImageProvider
{
public:
    WindowImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};
