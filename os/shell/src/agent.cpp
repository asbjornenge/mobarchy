#include "agent.h"

#include <QJsonArray>
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
    connect(&m_list, &QProcess::finished, this, [this] {
        const QJsonObject o = QJsonDocument::fromJson(m_list.readAllStandardOutput()).object();
        m_agents = o.value(QStringLiteral("agents")).toArray().toVariantList();
        m_active = o.value(QStringLiteral("active")).toString();
        m_models = o.value(QStringLiteral("models")).toObject().toVariantMap();
        m_modelChoices = o.value(QStringLiteral("modelChoices")).toArray().toVariantList();
        emit agentsChanged();
    });
    refresh();
    loadConversation();
}

QString Agent::program()
{
    return qEnvironmentVariable("MOBARCHY_AGENT_BIN", QString::fromLatin1(DefaultAgent));
}

void Agent::refresh()
{
    if (m_list.state() == QProcess::NotRunning)
        m_list.start(program(), {QStringLiteral("agents"), QStringLiteral("--json")});
}

void Agent::start(const QStringList &args)
{
    m_stderr.clear();
    m_sawError = m_stopped = false;
    m_proc.start(program(), args);
}

void Agent::logout(const QString &id)
{
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc] {
        proc->deleteLater();
        refresh();
    });
    proc->start(program(), {QStringLiteral("logout"), id});
}

void Agent::setModel(const QString &which, const QString &model)
{
    // at once in the menu; the file follows
    m_models[which] = model == QLatin1String("default") ? QString() : model;
    emit agentsChanged();
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc] {
        proc->deleteLater();
        refresh();
    });
    proc->start(program(), {QStringLiteral("model"), which, model});
}

void Agent::choose(const QString &id)
{
    if (running())
        return;
    for (const QVariant &a : std::as_const(m_agents)) {
        const QVariantMap m = a.toMap();
        if (m.value(QStringLiteral("id")).toString() != id)
            continue;
        if (m.value(QStringLiteral("ready")).toBool()) {
            auto *use = new QProcess(this);
            connect(use, &QProcess::finished, this, [this, use] {
                use->deleteLater();
                refresh();
            });
            use->start(program(), {QStringLiteral("use"), id});
        } else {
            start({QStringLiteral("setup"), id, QStringLiteral("--json")});
        }
        return;
    }
}

void Agent::command(const QStringList &args, std::function<void(const QByteArray &)> done)
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [p, done] {
        if (done)
            done(p->readAllStandardOutput());
        p->deleteLater();
    });
    p->start(program(), args);
}

void Agent::newConversation()
{
    command({QStringLiteral("conversation"), QStringLiteral("new")}, [this](const QByteArray &) {
        emit conversationLoaded({});
        refreshConversations();
    });
}

void Agent::openConversation(const QString &id)
{
    command({QStringLiteral("conversation"), QStringLiteral("open"), id},
            [this](const QByteArray &) { loadConversation(); refreshConversations(); });
}

void Agent::loadConversation()
{
    command({QStringLiteral("conversation"), QStringLiteral("show"), QStringLiteral("--json")},
            [this](const QByteArray &out) {
                const QJsonObject o = QJsonDocument::fromJson(out).object();
                emit conversationLoaded(o.value(QStringLiteral("turns")).toArray().toVariantList());
            });
}

void Agent::refreshConversations()
{
    command({QStringLiteral("conversations"), QStringLiteral("--json")}, [this](const QByteArray &out) {
        m_conversations = QJsonDocument::fromJson(out).array().toVariantList();
        emit conversationsChanged();
    });
}

void Agent::sendCode(const QString &code)
{
    if (!running() || code.trimmed().isEmpty())
        return;
    m_proc.write(code.trimmed().toUtf8() + '\n');
    m_awaitingCode = false;
    emit awaitingCodeChanged();
}

void Agent::openUrl(const QString &url)
{
    QProcess::startDetached(QStringLiteral("mobarchy-open"), {url});
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
    start({QStringLiteral("--json"), task.trimmed()});
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
        if (kind == u"code" && !m_awaitingCode) {
            m_awaitingCode = true;
            emit awaitingCodeChanged();
        }
        emit event(kind, o.value(QStringLiteral("text")).toString());
    }
}

void Agent::finished(int code, QProcess::ExitStatus status)
{
    readEvents();
    if (m_awaitingCode) {
        m_awaitingCode = false;
        emit awaitingCodeChanged();
    }
    refresh();  // a setup may have logged in, a run may have found the login expired
    if (m_stopped) {
        emit event(QStringLiteral("error"), tr("Stopped."));
    } else if ((code != 0 || status != QProcess::NormalExit) && !m_sawError) {
        const QString detail = QString::fromUtf8(m_stderr).trimmed().split(u'\n').last();
        emit event(QStringLiteral("error"),
                   detail.isEmpty() ? tr("The agent exited with code %1.").arg(code) : detail);
    }
}
