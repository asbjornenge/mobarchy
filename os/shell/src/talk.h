// Frigg, a live voice conversation with the agent (ADR 0026): `mobarchy-agent talk`, its
// JSON events (state, heard, said, tool, error) and its commands (stop, interrupt, sleep,
// wake). With `wake` on, it runs between conversations too, waiting for "Hei Frigg".
#pragma once

#include <QObject>
#include <QProcess>
#include <QtQml/qqmlregistration.h>

class Talk : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // A conversation is on.
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // "listening" | "thinking" | "speaking" | "idle" (and "starting" before the first);
    // "waiting" for the wake phrase
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // A conversation is on (not just waiting for the phrase).
    Q_PROPERTY(bool talking READ talking NOTIFY stateChanged)
    // Wait for "Hei Frigg" (the microphone stays on).
    Q_PROPERTY(bool wake READ wake WRITE setWake NOTIFY wakeChanged)
    // What the owner said last, and what Frigg has said of its answer so far.
    Q_PROPERTY(QString heard READ heard NOTIFY textChanged)
    Q_PROPERTY(QString said READ said NOTIFY textChanged)
    // Frigg uses the screen in this answer (a screenshot, a tap): the overlay keeps out of
    // the way until she has answered.
    Q_PROPERTY(bool usingScreen READ usingScreen NOTIFY textChanged)
    // On a locked phone: what was heard wasn't the owner's voice (Voice Match)
    Q_PROPERTY(bool guest READ guest NOTIFY textChanged)
    // What Frigg is doing (a tool), and what went wrong last.
    Q_PROPERTY(QString doing READ doing NOTIFY textChanged)
    Q_PROPERTY(QString error READ error NOTIFY textChanged)
    // Learning the owner's voice (Voice Match): the sentence to read, which of how many.
    Q_PROPERTY(bool enrolling READ enrolling NOTIFY enrollChanged)
    Q_PROPERTY(QString enrollText READ enrollText NOTIFY enrollChanged)
    Q_PROPERTY(int enrollIndex READ enrollIndex NOTIFY enrollChanged)
    Q_PROPERTY(int enrollCount READ enrollCount NOTIFY enrollChanged)

public:
    explicit Talk(QObject *parent = nullptr);
    ~Talk() override;

    bool active() const { return m_proc.state() != QProcess::NotRunning; }
    bool talking() const
    {
        return active() && m_state != QLatin1String("waiting") && m_state != QLatin1String("idle");
    }
    bool wake() const { return m_wake; }
    void setWake(bool wake);
    QString state() const { return m_state; }
    QString heard() const { return m_heard; }
    QString said() const { return m_said; }
    bool guest() const { return m_guest; }
    bool usingScreen() const { return m_usingScreen; }
    QString doing() const { return m_doing; }
    QString error() const { return m_error; }
    bool enrolling() const { return m_procEnroll && active(); }
    QString enrollText() const { return m_enrollText; }
    int enrollIndex() const { return m_enrollIndex; }
    int enrollCount() const { return m_enrollCount; }

    // start a conversation
    Q_INVOKABLE void start();
    // end it (with the wake phrase on, wait for it again)
    Q_INVOKABLE void stop();
    // learn the owner's voice: Frigg asks for a few sentences
    Q_INVOKABLE void enroll();
    // stop speaking, listen
    Q_INVOKABLE void interrupt();

signals:
    void activeChanged();
    void wakeChanged();
    void enrollChanged();
    void stateChanged();
    void textChanged();

private:
    void readEvents();
    void send(const QByteArray &command);
    void launch(bool enroll = false);

    QProcess m_proc;
    QString m_state = QStringLiteral("idle");
    QString m_heard, m_said, m_doing, m_error;
    bool m_guest = false;
    bool m_usingScreen = false;
    bool m_wake = false;
    bool m_procWake = false;    // the running process waits for the phrase
    bool m_procEnroll = false;  // it learns the owner's voice
    bool m_enrollNext = false;  // enroll() waits for the running process to end
    bool m_stopping = false;    // told to stop (and killed if it doesn't): its end is no error
    bool m_startNext = false;   // start() while it was stopping: again once it has ended
    QString m_enrollText;
    int m_enrollIndex = 0, m_enrollCount = 0;
    int m_failures = 0;     // quick exits in a row, waiting for the phrase
};
