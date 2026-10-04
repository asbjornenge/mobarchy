// The camera app's camera (ADR 0018): a camera's PipeWire source (mobarchy-camera-source) into
// a QVideoSink for VideoOutput, and photos through org.mobarchy.Camera.
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct spa_pod;

class CameraSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // The cameras: [{id, facing ("back", "front", "external"), orientation (degrees)}].
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY camerasChanged)
    // The camera to show (an id from cameras); "" until they are known.
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)
    // Its facing and how its images are rotated (degrees clockwise, to upright).
    Q_PROPERTY(QString facing READ facing NOTIFY currentChanged)
    Q_PROPERTY(int orientation READ orientation NOTIFY currentChanged)
    // Where the frames go (VideoOutput.videoSink).
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    // Showing frames (the app is visible).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)          // taking a photo
    // How far the device is turned, clockwise from its natural orientation (degrees): the
    // photos' own orientation follows it (DeviceOrientation.degrees).
    Q_PROPERTY(int deviceRotation MEMBER m_deviceRotation NOTIFY deviceRotationChanged)
    Q_PROPERTY(QString lastPhoto READ lastPhoto NOTIFY lastPhotoChanged)

public:
    explicit CameraSource(QObject *parent = nullptr);
    ~CameraSource() override;

    QVariantList cameras() const { return m_cameras; }
    QString current() const { return m_current; }
    void setCurrent(const QString &id);
    QString facing() const;
    int orientation() const;
    QVideoSink *videoSink() const { return m_sink; }
    void setVideoSink(QVideoSink *sink);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool busy() const { return m_busy; }
    QString lastPhoto() const { return m_lastPhoto; }

    Q_INVOKABLE void takePhoto();
    Q_INVOKABLE void switchCamera();        // to the next camera
    Q_INVOKABLE void refresh();             // ask for the cameras again

signals:
    void camerasChanged();
    void currentChanged();
    void videoSinkChanged();
    void activeChanged();
    void busyChanged();
    void deviceRotationChanged();
    void lastPhotoChanged();
    void photoTaken(const QString &path);
    void failed(const QString &message);

private:
    void restartStream();
    void stopStream();
    QVariantMap currentInfo() const;
    // the stream's callbacks (PipeWire's thread)
    static void onProcess(void *data);
    static void onParamChanged(void *data, uint32_t id, const struct spa_pod *param);

    QVariantList m_cameras;
    QString m_current;
    QPointer<QVideoSink> m_sink;
    bool m_active = false;
    bool m_busy = false;
    int m_deviceRotation = 0;
    QString m_lastPhoto;

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_stream *m_stream = nullptr;
    void *m_listener = nullptr;             // spa_hook
    int m_width = 0, m_height = 0;
};
