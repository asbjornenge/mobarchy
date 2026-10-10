// Haptic feedback, through the kernel's force-feedback interface (the first input device
// that can play effects): a click when a button acts, and a lighter tick when a gesture
// takes hold (the app switcher). Each the device's own waveform if device.toml names one
// ([haptics] click_bank, click_index; tick_bank, tick_index: a custom effect's data),
// else a short buzz (the tick shorter and weaker).
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
    Q_INVOKABLE void tick();

private:
    bool open();
    void play(int effect);

    int m_fd = -1;
    int m_click = -1;
    int m_tick = -1;
    bool m_tried = false;
};
