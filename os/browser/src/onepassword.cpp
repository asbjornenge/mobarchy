#include "onepassword.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

OnePassword::OnePassword(QObject *parent)
    : QObject(parent)
    , m_op(QStandardPaths::findExecutable(QStringLiteral("op")))
{
}

void OnePassword::setBusy(bool busy)
{
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

// op's own words, or what to do about it
static QString explain(const QString &stderrText)
{
    const QString e = stderrText.trimmed();
    if (e.contains(QLatin1String("app integration"), Qt::CaseInsensitive)
        || e.contains(QLatin1String("not currently signed in"), Qt::CaseInsensitive)
        || e.contains(QLatin1String("no accounts"), Qt::CaseInsensitive))
        return OnePassword::tr("Open 1Password and turn on Settings → Developer → "
                               "“Integrate with 1Password CLI”");
    return e.section(QLatin1Char('\n'), -1).remove(QRegularExpression(QStringLiteral("^\\[ERROR\\] [^ ]+ [^ ]+ ")));
}

void OnePassword::run(const QStringList &args, Done done)
{
    m_queue.enqueue({args, done});
    setBusy(true);
    next();
}

// One request at a time, through the session (started on first use, again if it ended).
void OnePassword::next()
{
    if (m_waiting || m_queue.isEmpty()) {
        if (m_queue.isEmpty() && !m_waiting)
            setBusy(false);
        return;
    }
    if (!m_session || m_session->state() == QProcess::NotRunning) {
        delete m_session;
        m_session = new QProcess(this);
        m_buffer.clear();
        // Started by the user's service manager, not as the browser's child: the 1Password
        // app drops op's connection when op's parent is the browser ("InvalidClientInfo").
        connect(m_session, &QProcess::readyReadStandardOutput, this, [this] {
            m_buffer += m_session->readAllStandardOutput();
            int nl;
            while ((nl = m_buffer.indexOf('\n')) >= 0) {
                const QJsonObject r = QJsonDocument::fromJson(m_buffer.left(nl)).object();
                m_buffer.remove(0, nl + 1);
                if (!m_waiting || m_queue.isEmpty())
                    continue;
                m_waiting = false;
                auto [args, done] = m_queue.dequeue();
                done(r.value(QStringLiteral("code")).toInt(-1),
                     r.value(QStringLiteral("out")).toString().toUtf8(),
                     r.value(QStringLiteral("err")).toString());
                next();
            }
        });
        connect(m_session, &QProcess::finished, this, [this] {
            if (m_waiting && !m_queue.isEmpty()) {
                m_waiting = false;
                auto [args, done] = m_queue.dequeue();
                done(-1, {}, tr("1Password's session ended"));
            }
            QMetaObject::invokeMethod(this, &OnePassword::next, Qt::QueuedConnection);
        });
        m_session->start(QStringLiteral("systemd-run"),
                         {QStringLiteral("--user"), QStringLiteral("--pipe"), QStringLiteral("--quiet"),
                          QStringLiteral("--collect"), QStringLiteral(OP_SESSION)});
    }
    m_waiting = true;
    const QJsonObject req{{QStringLiteral("args"), QJsonArray::fromStringList(m_queue.head().first)}};
    m_session->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
}

bool OnePassword::matches(const QString &loginUrl, const QString &host)
{
    QString h = QUrl::fromUserInput(loginUrl).host().toLower();
    QString page = host.toLower();
    if (h.startsWith(QLatin1String("www.")))
        h = h.mid(4);
    if (page.startsWith(QLatin1String("www.")))
        page = page.mid(4);
    if (h.isEmpty() || page.isEmpty())
        return false;
    // the same site, or one of its subdomains either way (accounts.example.com, example.com)
    return h == page || page.endsWith(QLatin1Char('.') + h) || h.endsWith(QLatin1Char('.') + page);
}

void OnePassword::logins(const QString &host)
{
    run({QStringLiteral("item"), QStringLiteral("list"), QStringLiteral("--categories"),
         QStringLiteral("Login"), QStringLiteral("--format"), QStringLiteral("json")},
        [this, host](int code, const QByteArray &out, const QString &err) {
            if (code != 0) {
                emit listed({}, explain(err));
                return;
            }
            QVariantList found;
            for (const QJsonValue &v : QJsonDocument::fromJson(out).array()) {
                const QJsonObject item = v.toObject();
                bool ok = false;
                for (const QJsonValue &u : item.value(QStringLiteral("urls")).toArray())
                    ok = ok || matches(u.toObject().value(QStringLiteral("href")).toString(), host);
                if (ok)
                    found << QVariantMap{
                        {QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
                        {QStringLiteral("title"), item.value(QStringLiteral("title")).toString()},
                        {QStringLiteral("username"),
                         item.value(QStringLiteral("additional_information")).toString()}};
            }
            emit listed(found, QString());
        });
}

void OnePassword::credentials(const QString &id)
{
    run({QStringLiteral("item"), QStringLiteral("get"), id, QStringLiteral("--format"),
         QStringLiteral("json"), QStringLiteral("--reveal")},
        [this](int code, const QByteArray &out, const QString &err) {
            if (code != 0) {
                emit credentialsReady({}, {}, {}, explain(err));
                return;
            }
            QString user, password, otp;
            const QJsonObject item = QJsonDocument::fromJson(out).object();
            for (const QJsonValue &v : item.value(QStringLiteral("fields")).toArray()) {
                const QJsonObject f = v.toObject();
                const QString purpose = f.value(QStringLiteral("purpose")).toString();
                if (purpose == QLatin1String("USERNAME") && user.isEmpty())
                    user = f.value(QStringLiteral("value")).toString();
                else if (purpose == QLatin1String("PASSWORD") && password.isEmpty())
                    password = f.value(QStringLiteral("value")).toString();
                else if (f.value(QStringLiteral("type")).toString() == QLatin1String("OTP") && otp.isEmpty())
                    otp = f.value(QStringLiteral("totp")).toString();
            }
            emit credentialsReady(user, password, otp, QString());
        });
}
