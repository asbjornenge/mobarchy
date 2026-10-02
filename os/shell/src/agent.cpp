#include "agent.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {
const char DefaultAgent[] = "/opt/mobarchy/agent/mobarchy-agent/.venv/bin/mobarchy-agent";
}

Agent::Agent(QObject *parent)
    : QObject(parent)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &Agent::readEvents);
    connect(&m_proc, &QProcess::readyReadStandardError, this,
            [this] { m_stderr = (m_stderr + m_proc.readAllStandardError()).right(4096); });
    connect(&m_proc, &QProcess::finished, this, &Agent::finished);
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit event(QStringLiteral("error"),
                       tr("Cannot start the agent (%1): %2").arg(m_proc.program(), m_proc.errorString()));
            emit runningChanged();
        }
    });
    connect(&m_proc, &QProcess::stateChanged, this, &Agent::runningChanged);
}

Agent::~Agent()
{
    if (running()) {
        m_proc.terminate();
        m_proc.waitForFinished(1000);
    }
}

void Agent::ask(const QString &task)
{
    if (running() || task.trimmed().isEmpty())
        return;
    m_stderr.clear();
    m_sawError = m_stopped = false;
    m_proc.start(qEnvironmentVariable("MOBARCHY_AGENT_BIN", QString::fromLatin1(DefaultAgent)),
                 {QStringLiteral("--json"), task.trimmed()});
}

void Agent::stop()
{
    if (!running())
        return;
    m_stopped = true;
    m_proc.terminate();
}

void Agent::readEvents()
{
    while (m_proc.canReadLine()) {
        const QByteArray line = m_proc.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        const QString kind = o.value(QStringLiteral("kind")).toString();
        if (kind.isEmpty()) {  // not an event: pass the line on as text
            emit event(QStringLiteral("text"), QString::fromUtf8(line));
            continue;
        }
        m_sawError |= kind == u"error";
        emit event(kind, o.value(QStringLiteral("text")).toString());
    }
}

void Agent::finished(int code, QProcess::ExitStatus status)
{
    readEvents();
    if (m_stopped) {
        emit event(QStringLiteral("error"), tr("Stopped."));
    } else if ((code != 0 || status != QProcess::NormalExit) && !m_sawError) {
        const QString detail = QString::fromUtf8(m_stderr).trimmed().split(u'\n').last();
        emit event(QStringLiteral("error"),
                   detail.isEmpty() ? tr("The agent exited with code %1.").arg(code) : detail);
    }
}
