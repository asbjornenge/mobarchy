// Talking to the agent: records from the microphone in use (PipeWire's default source:
// the phone's, or a headset's), then turns the speech into text on the phone
// (mobarchy-agent transcribe: whisper.cpp).
#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class Voice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(int seconds READ seconds NOTIFY secondsChanged)  // recorded so far

public:
    enum State { Idle, Listening, Transcribing };
    Q_ENUM(State)

    explicit Voice(QObject *parent = nullptr);
    ~Voice() override;

    State state() const { return m_state; }
    int seconds() const { return m_seconds; }

    Q_INVOKABLE void start();
    // Stop listening and turn what was said into text (transcribed()).
    Q_INVOKABLE void finish();
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void secondsChanged();
    void transcribed(const QString &text);
    void failed(const QString &reason);

private:
    void setState(State state);
    void recorded();
    QString file() const;

    QProcess m_record;
    QProcess m_transcribe;
    QTimer m_clock;
    State m_state = Idle;
    int m_seconds = 0;
    bool m_cancelled = false;
};
