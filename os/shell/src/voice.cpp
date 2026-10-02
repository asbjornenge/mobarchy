#include "voice.h"

#include "agent.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <csignal>

namespace {

constexpr int MaxSeconds = 60;  // a forgotten recording stops by itself

}  // namespace

Voice::Voice(QObject *parent)
    : QObject(parent)
{
    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        ++m_seconds;
        emit secondsChanged();
        if (m_seconds >= MaxSeconds)
            finish();
    });
    connect(&m_record, &QProcess::finished, this, &Voice::recorded);
    connect(&m_record, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            setState(Idle);
            emit failed(tr("Cannot record: pw-record is missing"));
        }
    });
    connect(&m_transcribe, &QProcess::finished, this, [this] {
        QFile::remove(file());
        setState(Idle);
        if (m_cancelled)
            return;
        const QJsonObject reply = QJsonDocument::fromJson(m_transcribe.readAllStandardOutput().trimmed()).object();
        if (reply.contains(QStringLiteral("error")))
            emit failed(reply.value(QStringLiteral("error")).toString());
        else if (!reply.value(QStringLiteral("text")).toString().isEmpty())
            emit transcribed(reply.value(QStringLiteral("text")).toString());
        else
            emit failed(tr("Didn't catch that"));
    });
}

Voice::~Voice()
{
    m_record.kill();
    m_transcribe.kill();
    m_record.waitForFinished(500);
    QFile::remove(file());
}

QString Voice::file() const
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/mobarchy-voice.wav");
}

void Voice::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void Voice::start()
{
    if (m_state != Idle)
        return;
    m_cancelled = false;
    m_seconds = 0;
    emit secondsChanged();
    QFile::remove(file());
    // 16 kHz mono is what Whisper wants; PipeWire resamples.
    m_record.start(QStringLiteral("pw-record"), {QStringLiteral("--rate"), QStringLiteral("16000"),
                                                 QStringLiteral("--channels"), QStringLiteral("1"),
                                                 QStringLiteral("--format"), QStringLiteral("s16"),
                                                 QStringLiteral("--media-role"), QStringLiteral("Communication"),
                                                 file()});
    setState(Listening);
    m_clock.start();
}

void Voice::finish()
{
    if (m_state != Listening)
        return;
    m_clock.stop();
    // pw-record writes the WAV header's sizes when it is interrupted.
    ::kill(pid_t(m_record.processId()), SIGINT);
}

void Voice::cancel()
{
    m_cancelled = true;
    if (m_state == Listening) {
        m_clock.stop();
        ::kill(pid_t(m_record.processId()), SIGINT);
    } else if (m_state == Transcribing) {
        m_transcribe.kill();
    }
}

void Voice::recorded()
{
    if (m_cancelled || !QFile::exists(file())) {
        QFile::remove(file());
        setState(Idle);
        return;
    }
    setState(Transcribing);
    m_transcribe.start(Agent::program(), {QStringLiteral("transcribe"), QStringLiteral("--json"), file()});
}
