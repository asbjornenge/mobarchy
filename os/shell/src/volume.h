// The output volume: the default sink's volume and mute, through the PulseAudio API
// (PipeWire's pipewire-pulse). It follows changes made elsewhere (apps, wpctl).
#pragma once

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

struct pa_threaded_mainloop;
struct pa_context;

class Volume : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY changed)  // 0..1 (100 %)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    explicit Volume(QObject *parent = nullptr);
    ~Volume() override;

    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    bool available() const { return m_available; }

signals:
    void changed();

private:
    // Called on the PulseAudio thread (the loop is locked there).
    static void contextState(pa_context *c, void *self);
    void refresh();

    void connectServer();
    void update(bool available, const QByteArray &sink, int channels, qreal volume, bool muted);

    pa_threaded_mainloop *m_loop = nullptr;
    pa_context *m_context = nullptr;
    QTimer m_reconnect;
    // Qt thread state
    QByteArray m_sink;
    int m_channels = 2;
    qreal m_volume = 0;
    bool m_muted = false;
    bool m_available = false;
};
