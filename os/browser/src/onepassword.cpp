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

void OnePassword::run(const QStringList &args, std::function<void(int, const QByteArray &, const QString &)> done)
{
    auto *p = new QProcess(this);
    setBusy(true);
    connect(p, &QProcess::finished, this, [this, p, done](int code) {
        p->deleteLater();
        setBusy(false);
        done(code, p->readAllStandardOutput(), QString::fromUtf8(p->readAllStandardError()));
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        setBusy(false);
        done(-1, {}, tr("1Password's CLI (op) isn't installed"));
    });
    p->start(m_op, args);
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
