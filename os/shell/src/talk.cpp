#include "talk.h"

#include "agent.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

Talk::Talk(QObject *parent)
    : QObject(parent)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &Talk::readEvents);
    connect(&m_proc, &QProcess::stateChanged, this, [this] {
        if (!active() && m_state != QLatin1String("idle")) {
            m_state = QStringLiteral("idle");
            emit stateChanged();
        }
        emit activeChanged();
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_error = tr("Frigg could not start (%1)").arg(m_proc.errorString());
        emit textChanged();
    });
}

Talk::~Talk()
{
    if (active()) {
        send("stop");
        if (!m_proc.waitForFinished(2000))
            m_proc.kill();
    }
}

void Talk::start()
{
    if (active())
        return;
    m_heard.clear();
    m_said.clear();
    m_doing.clear();
    m_error.clear();
    m_state = QStringLiteral("starting");
    emit textChanged();
    emit stateChanged();
    m_proc.start(Agent::program(), {QStringLiteral("talk")});
}

void Talk::stop()
{
    if (!active())
        return;
    send("stop");
    // (it closes Claude Code and the audio, then exits; if not, end it)
    QTimer *t = new QTimer(this);
    t->setSingleShot(true);
    connect(t, &QTimer::timeout, this, [this, t] {
        if (active())
            m_proc.kill();
        t->deleteLater();
    });
    t->start(5000);
}

void Talk::interrupt()
{
    send("interrupt");
}

void Talk::send(const QByteArray &command)
{
    if (active())
        m_proc.write(command + '\n');
}

void Talk::readEvents()
{
    while (m_proc.canReadLine()) {
        const QJsonObject e = QJsonDocument::fromJson(m_proc.readLine().trimmed()).object();
        const QString kind = e.value(QStringLiteral("event")).toString();
        if (kind == QLatin1String("state")) {
            const QString state = e.value(QStringLiteral("state")).toString();
            if (state == QLatin1String("thinking") || state == QLatin1String("speaking")) {
                m_doing.clear();
                emit textChanged();
            }
            if (state != m_state) {
                m_state = state;
                emit stateChanged();
            }
            continue;
        }
        if (kind == QLatin1String("heard")) {
            m_heard = e.value(QStringLiteral("text")).toString();
            m_said.clear();
            m_error.clear();
        } else if (kind == QLatin1String("said")) {
            m_said = m_said.isEmpty() ? e.value(QStringLiteral("text")).toString()
                                      : m_said + QLatin1Char(' ') + e.value(QStringLiteral("text")).toString();
        } else if (kind == QLatin1String("tool")) {
            m_doing = e.value(QStringLiteral("text")).toString();
        } else if (kind == QLatin1String("error")) {
            m_error = e.value(QStringLiteral("text")).toString();
        } else {
            continue;
        }
        emit textChanged();
    }
}
