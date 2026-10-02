// Runs the on-device agent for one task (mobarchy-agent --json) and passes its events on.
// The shell only depends on that JSON line stream (ADR 0006).
#pragma once

#include <QObject>
#include <QProcess>
#include <QtQml/qqmlregistration.h>

class Agent : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit Agent(QObject *parent = nullptr);
    ~Agent() override;

    bool running() const { return m_proc.state() != QProcess::NotRunning; }

    Q_INVOKABLE void ask(const QString &task);
    Q_INVOKABLE void stop();

signals:
    // kind: "text" | "tool" | "done" | "error"
    void event(const QString &kind, const QString &text);
    void runningChanged();

private:
    void readEvents();
    void finished(int code, QProcess::ExitStatus status);

    QProcess m_proc;
    QByteArray m_stderr;
    bool m_sawError = false;
    bool m_stopped = false;
};
