// Frigg, a live voice conversation with the agent (ADR 0026): `mobarchy-agent talk`, its
// JSON events (state, heard, said, tool, error) and its commands (stop, interrupt).
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
    // "listening" | "thinking" | "speaking" | "idle" (and "starting" before the first)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // What the owner said last, and what Frigg has said of its answer so far.
    Q_PROPERTY(QString heard READ heard NOTIFY textChanged)
    Q_PROPERTY(QString said READ said NOTIFY textChanged)
    // What Frigg is doing (a tool), and what went wrong last.
    Q_PROPERTY(QString doing READ doing NOTIFY textChanged)
    Q_PROPERTY(QString error READ error NOTIFY textChanged)

public:
    explicit Talk(QObject *parent = nullptr);
    ~Talk() override;

    bool active() const { return m_proc.state() != QProcess::NotRunning; }
    QString state() const { return m_state; }
    QString heard() const { return m_heard; }
    QString said() const { return m_said; }
    QString doing() const { return m_doing; }
    QString error() const { return m_error; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // stop speaking, listen
    Q_INVOKABLE void interrupt();

signals:
    void activeChanged();
    void stateChanged();
    void textChanged();

private:
    void readEvents();
    void send(const QByteArray &command);

    QProcess m_proc;
    QString m_state = QStringLiteral("idle");
    QString m_heard, m_said, m_doing, m_error;
};
