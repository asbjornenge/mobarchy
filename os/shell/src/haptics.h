// Haptic feedback: a short click when a button acts, through the kernel's force-feedback
// interface (the first input device that can play effects). The click itself: the
// device's own waveform if device.toml names one ([haptics] click_bank, click_index: a
// custom effect's data), else a short buzz.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Haptics : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Haptics(QObject *parent = nullptr);
    ~Haptics() override;

    Q_INVOKABLE void click();

private:
    bool open();

    int m_fd = -1;
    int m_effect = -1;
    bool m_tried = false;
};
