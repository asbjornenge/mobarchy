#include "talk.h"

#include "agent.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

Talk::Talk(QObject *parent)
    : QObject(parent)
{
    // its messages (e.g. each turn's timing) to the shell's log, the journal
    m_proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &Talk::readEvents);
    connect(&m_proc, &QProcess::stateChanged, this, [this] {
        if (!active() && m_state != QLatin1String("idle")) {
            m_state = QStringLiteral("idle");
            emit stateChanged();
        }
        emit activeChanged();
    });
    // Waiting for the phrase is meant to go on: if it ends by itself, it starts again (later
    // and later if it keeps failing).
    connect(&m_proc, &QProcess::finished, this, [this](int code) {
        if (m_procEnroll) {
            m_procEnroll = false;
            emit enrollChanged();
        }
        if (m_enrollNext) {
            m_enrollNext = false;
            launch(true);
            return;
        }
        if (!m_wake)
            return;
        m_failures = code == 0 ? 0 : m_failures + 1;
        if (m_failures > 5)
            return;
        QTimer::singleShot(code == 0 ? 0 : 2000 * m_failures, this, [this] {
            if (m_wake && !active())
                launch();
        });
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
    m_heard.clear();
    m_said.clear();
    m_doing.clear();
    m_error.clear();
    m_usingScreen = false;
    emit textChanged();
    if (active()) {
        if (m_state == QLatin1String("waiting"))
            send("wake");
        return;
    }
    m_state = QStringLiteral("starting");
    emit stateChanged();
    launch();
}

void Talk::launch(bool enroll)
{
    QStringList args{QStringLiteral("talk")};
    if (enroll)
        args << QStringLiteral("--enroll");
    else if (m_wake)
        args << QStringLiteral("--wake");
    m_procWake = m_wake && !enroll;
    m_procEnroll = enroll;
    m_proc.start(Agent::program(), args);
    emit enrollChanged();
}

void Talk::enroll()
{
    m_heard.clear();
    m_said.clear();
    m_doing.clear();
    m_error.clear();
    m_enrollText.clear();
    m_enrollIndex = m_enrollCount = 0;
    emit textChanged();
    m_state = QStringLiteral("starting");
    emit stateChanged();
    if (active()) {
        // (the phrase's listener first stops: it would hear the sentences)
        m_enrollNext = true;
        send("stop");
        return;
    }
    launch(true);
}

void Talk::setWake(bool wake)
{
    if (wake == m_wake)
        return;
    m_wake = wake;
    m_failures = 0;
    emit wakeChanged();
    if (wake && !active())
        launch();
    else if (!wake && active() && !talking())
        send("stop");
    // (on, during a conversation without it: the next one waits for the phrase; off during
    // one: it ends with the conversation)
}

void Talk::stop()
{
    if (!active())
        return;
    if (m_procWake) {
        send("sleep");
        return;
    }
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
            // (what Frigg was doing, a tool or a download, is done when its state moves on;
            // "starting" comes with a download)
            // (the answer is done: the overlay is back, to listen or to be closed)
            if (state == QLatin1String("listening") || state == QLatin1String("waiting")) {
                if (m_usingScreen) {
                    m_usingScreen = false;
                    emit textChanged();
                }
            }
            if (state != QLatin1String("starting") && !m_doing.isEmpty()) {
                m_doing.clear();
                emit textChanged();
            }
            if (state != m_state) {
                m_state = state;
                emit stateChanged();
            }
            // (the phrase was turned off during the conversation: it ends with it)
            if (state == QLatin1String("waiting") && !m_wake)
                send("stop");
            continue;
        }
        if (kind == QLatin1String("enroll")) {
            m_enrollText = e.value(QStringLiteral("text")).toString();
            m_enrollIndex = e.value(QStringLiteral("index")).toInt();
            m_enrollCount = e.value(QStringLiteral("count")).toInt();
            emit enrollChanged();
            continue;
        }
        if (kind == QLatin1String("enrolled")) {
            m_enrollText.clear();
            emit enrollChanged();
            continue;
        }
        if (kind == QLatin1String("heard")) {
            m_heard = e.value(QStringLiteral("text")).toString();
            m_guest = e.value(QStringLiteral("voice")).toString() == QLatin1String("guest");
            m_usingScreen = false;
            m_said.clear();
            m_error.clear();
        } else if (kind == QLatin1String("said")) {
            m_said = m_said.isEmpty() ? e.value(QStringLiteral("text")).toString()
                                      : m_said + QLatin1Char(' ') + e.value(QStringLiteral("text")).toString();
        } else if (kind == QLatin1String("tool")) {
            m_doing = e.value(QStringLiteral("text")).toString();
            if (e.value(QStringLiteral("screen")).toBool())
                m_usingScreen = true;
        } else if (kind == QLatin1String("error")) {
            m_error = e.value(QStringLiteral("text")).toString();
        } else {
            continue;
        }
        emit textChanged();
    }
}
