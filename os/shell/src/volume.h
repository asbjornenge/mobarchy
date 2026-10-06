// The output volume: the default sink's volume and mute, through the PulseAudio API
// (PipeWire's pipewire-pulse). It follows changes made elsewhere (apps, wpctl).
// And where the sound goes: the outputs (the speaker, headphones, Bluetooth), the one in
// use, a choice of another. Headphones that connect take over, as on other phones.
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
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
    // [{name, description, kind: "speaker" | "headphones" | "bluetooth"}], and the one in use
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputsChanged)

public:
    explicit Volume(QObject *parent = nullptr);
    ~Volume() override;

    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    bool available() const { return m_available; }
    QVariantList outputs() const { return m_outputs; }
    QString output() const { return QString::fromUtf8(m_sink); }

    // The sound goes there now, what plays included.
    Q_INVOKABLE void setOutput(const QString &name);

signals:
    void changed();
    void outputsChanged();

private:
    // Called on the PulseAudio thread (the loop is locked there).
    static void contextState(pa_context *c, void *self);
    void refresh();
    void sinkAdded(uint32_t index);

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
    QVariantList m_outputs;
};
