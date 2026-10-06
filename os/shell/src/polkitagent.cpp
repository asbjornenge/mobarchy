#include "polkitagent.h"

#include "logind.h"

#include <PolkitQt1/Agent/Listener>
#include <PolkitQt1/Agent/Session>
#include <PolkitQt1/Details>
#include <PolkitQt1/Identity>
#include <PolkitQt1/Subject>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

#include <pwd.h>
#include <unistd.h>

// polkit's side: requests come in here and go to the PolkitAgent.
class AgentListener : public PolkitQt1::Agent::Listener
{
    Q_OBJECT

public:
    explicit AgentListener(PolkitAgent *agent)
        : PolkitQt1::Agent::Listener(agent)
        , m_agent(agent)
    {
    }

public slots:
    void initiateAuthentication(const QString &actionId, const QString &message, const QString &iconName,
                                const PolkitQt1::Details &details, const QString &cookie,
                                const PolkitQt1::Identity::List &identities,
                                PolkitQt1::Agent::AsyncResult *result) override
    {
        Q_UNUSED(iconName)
        // The owner, when they are one who may (as wheel, for auth_admin); else the first.
        const passwd *pw = getpwuid(getuid());
        const QString me = QStringLiteral("unix-user:") + QString::fromLocal8Bit(pw ? pw->pw_name : "");
        QString identity = identities.isEmpty() ? QString() : identities.first().toString();
        for (const PolkitQt1::Identity &i : identities)
            if (i.toString() == me)
                identity = me;
        Q_UNUSED(details)   // (polkitd passes no more than pids: the message names the command)
        m_agent->begin(actionId, message, identity, cookie, result);
    }
    bool initiateAuthenticationFinish() override { return true; }
    void cancelAuthentication() override { m_agent->cancelled(); }

private:
    PolkitAgent *m_agent;
};

PolkitAgent::PolkitAgent(QObject *parent)
    : QObject(parent)
    , m_listener(new AgentListener(this))
{
    // The agent for the owner's display session (the shell runs outside it, as a user
    // service; polkit counts the service's processes to that session too).
    const DisplaySession session = displaySession();
    if (session.id.isEmpty()) {
        qWarning() << "polkit: no display session to be the agent of";
        return;
    }
    m_registered = m_listener->registerListener(PolkitQt1::UnixSessionSubject(session.id),
                                                QStringLiteral("/org/mobarchy/PolicyKit1/AuthenticationAgent"));
    if (!m_registered)
        qWarning() << "polkit: could not register as the authentication agent of session" << session.id;
}

PolkitAgent::~PolkitAgent()
{
    if (m_result) {
        m_cancelling = true;
        stopSession();
        finish();
    }
}

void PolkitAgent::begin(const QString &actionId, const QString &message, const QString &identity,
                        const QString &cookie, PolkitQt1::Agent::AsyncResult *result)
{
    if (m_result) {
        // (one at a time: polkit asks again later)
        qWarning() << "polkit: busy, turning down" << actionId;
        result->setError(QStringLiteral("Another authentication is in progress"));
        result->setCompleted();
        return;
    }
    qInfo() << "polkit:" << actionId << "asks the owner";
    m_result = result;
    m_identity = identity;
    m_cookie = cookie;
    m_message = message;
    m_error.clear();
    m_cancelling = false;
    emit changed();
    if (!m_held)
        start();
}

// A PAM conversation; pin: one already typed, for its password prompt.
void PolkitAgent::start(const QString &pin)
{
    m_pin = pin;
    m_responded = false;
    m_passwordWanted = false;
    m_fingerWanted = false;
    m_busy = !pin.isEmpty();
    auto *session = new PolkitQt1::Agent::Session(PolkitQt1::Identity::fromString(m_identity), m_cookie,
                                                 nullptr, this);
    m_session = session;
    connect(session, &PolkitQt1::Agent::Session::request, this, [this](const QString &, bool) {
        // the password: the PIN, now or when it is typed
        m_passwordWanted = true;
        m_fingerWanted = false;
        emit changed();
        if (!m_pin.isEmpty())
            respond();
    });
    connect(session, &PolkitQt1::Agent::Session::showInfo, this, [this](const QString &) {
        // pam_fprintd: "Place your finger on the fingerprint reader"
        if (!m_passwordWanted) {
            m_fingerWanted = true;
            emit changed();
        }
    });
    connect(session, &PolkitQt1::Agent::Session::showError, this, [this](const QString &text) {
        m_error = text;
        emit changed();
    });
    connect(session, &PolkitQt1::Agent::Session::completed, this, [this, session](bool gained) {
        session->deleteLater();
        if (m_session == session)
            sessionCompleted(gained);
    });
    emit changed();
    session->initiate();
}

void PolkitAgent::sessionCompleted(bool gained)
{
    m_session.clear();
    if (gained) {
        qInfo() << "polkit: authorized";
        emit authorized();
        finish();
    } else if (m_cancelling || !m_result) {
        finish();
    } else if (m_held) {
        // (stopped for the lock: it starts again after the unlock)
    } else if (m_responded) {
        // a wrong PIN: again
        m_error = tr("Wrong PIN");
        emit failed();
        start();
    } else {
        // PAM gave up before any PIN (no helper, or the finger's tries ran out): no new
        // conversation until the owner types one (no loop)
        if (m_error.isEmpty())
            m_error = tr("Could not check: try the PIN");
        m_busy = false;
        emit changed();
    }
}

void PolkitAgent::enterPin(const QString &pin)
{
    if (!m_result || pin.isEmpty())
        return;
    if (!m_session) {
        // (the last conversation ended without asking: a new one, with the PIN ready)
        m_error.clear();
        start(pin);
        return;
    }
    m_pin = pin;
    m_busy = true;
    m_error.clear();
    emit changed();
    if (m_passwordWanted) {
        respond();
        return;
    }
    // pam_fprintd is still waiting for a finger: it steps aside, and the password prompt
    // that follows gets the PIN.
    QDBusConnection::systemBus().asyncCall(QDBusMessage::createMethodCall(
        QStringLiteral("net.reactivated.Fprint"), QStringLiteral("/net/reactivated/Fprint/Device/0"),
        QStringLiteral("org.mobarchy.Fingerprint"), QStringLiteral("SkipVerify")));
}

void PolkitAgent::respond()
{
    m_passwordWanted = false;
    m_responded = true;
    const QString pin = m_pin;
    m_pin.clear();
    m_session->setResponse(pin);
}

void PolkitAgent::cancel()
{
    if (!m_result)
        return;
    qInfo() << "polkit: the owner cancelled";
    m_cancelling = true;
    if (m_session)
        stopSession();
    else
        finish();
}

void PolkitAgent::cancelled()
{
    if (!m_result)
        return;
    qInfo() << "polkit: the request was withdrawn";
    m_cancelling = true;
    if (m_session)
        stopSession();
    else
        finish();
}

void PolkitAgent::setHeld(bool held)
{
    if (m_held == held)
        return;
    m_held = held;
    // A locked phone: the finger belongs to the lock screen, and nobody is looking. The
    // request waits, without a PAM conversation, until the unlock.
    if (held && m_session) {
        stopSession();
    } else if (!held && m_result && !m_session) {
        m_error.clear();
        start();
    }
    emit changed();
}

// Ends the PAM conversation; its completed() follows.
void PolkitAgent::stopSession()
{
    if (m_session)
        m_session->cancel();
}

void PolkitAgent::finish()
{
    if (m_result) {
        m_result->setCompleted();
        m_result = nullptr;
    }
    m_message.clear();
    m_pin.clear();
    m_passwordWanted = m_fingerWanted = m_busy = false;
    m_cancelling = false;
    emit changed();
}

#include "polkitagent.moc"
