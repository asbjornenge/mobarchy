#include "accounts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
const QString Command = QStringLiteral("mobarchy-accounts");
}

Accounts::Accounts(QObject *parent)
    : QObject(parent)
{
    refresh();
}

void Accounts::take(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.contains(QStringLiteral("accounts"))) {
        m_accounts = o.value(QStringLiteral("accounts")).toArray().toVariantList();
        m_googleClient = o.value(QStringLiteral("google_client")).toBool();
        emit changed();
    }
}

void Accounts::run(const QString &what, const QStringList &args, const QString &input, bool adds)
{
    if (m_proc)
        return;  // one at a time
    m_proc = new QProcess(this);
    m_busy = what;
    m_error.clear();
    emit busyChanged();
    connect(m_proc, &QProcess::finished, this, [this, adds](int code, QProcess::ExitStatus status) {
        const QByteArray out = m_proc->readAllStandardOutput();
        m_proc->deleteLater();
        m_proc = nullptr;
        m_busy.clear();
        // the last line is the answer (add-google prints the sign-in address first)
        const QList<QByteArray> lines = out.trimmed().split('\n');
        const QJsonObject o = QJsonDocument::fromJson(lines.value(lines.size() - 1)).object();
        if (status != QProcess::NormalExit || code != 0)
            m_error = o.value(QStringLiteral("error")).toString(tr("It did not work."));
        emit busyChanged();
        if (code == 0 && status == QProcess::NormalExit) {
            if (adds)
                emit added();
            if (o.contains(QStringLiteral("accounts")))
                take(lines.last());
            else
                refresh();
        }
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_proc->deleteLater();
        m_proc = nullptr;
        m_busy.clear();
        m_error = tr("mobarchy-accounts is not installed");
        emit busyChanged();
    });
    m_proc->start(Command, args);
    if (!input.isNull()) {
        m_proc->write(input.toUtf8() + '\n');
        m_proc->closeWriteChannel();
    }
}

void Accounts::refresh()
{
    if (m_proc)
        return;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        take(p->readAllStandardOutput().trimmed().split('\n').last());
        p->deleteLater();
    });
    p->start(Command, {QStringLiteral("list")});
}

void Accounts::addIcloud(const QString &appleId, const QString &appPassword)
{
    run(tr("Adding iCloud …"), {QStringLiteral("add-icloud"), appleId.trimmed()}, appPassword, true);
}

void Accounts::addCarddav(const QString &url, const QString &user, const QString &password)
{
    run(tr("Adding the account …"), {QStringLiteral("add-carddav"), url.trimmed(), user.trimmed()}, password, true);
}

void Accounts::addGoogle()
{
    // the command opens the browser at Google's sign-in, and waits for it
    run(tr("Sign in with Google in the browser …"), {QStringLiteral("add-google")}, {}, true);
}

void Accounts::addNextcloud(const QString &url)
{
    // the command opens the browser at the server's login page, and waits for it
    run(tr("Sign in to Nextcloud in the browser …"), {QStringLiteral("add-nextcloud"), url.trimmed()}, {}, true);
}

void Accounts::openUrl(const QString &url)
{
    QProcess::startDetached(QStringLiteral("mobarchy-open"), {url});
}

void Accounts::setGoogleClient(const QString &clientId, const QString &clientSecret)
{
    run(tr("Saving …"), {QStringLiteral("google-client"), clientId.trimmed()}, clientSecret);
}

void Accounts::remove(const QString &id)
{
    run(tr("Removing …"), {QStringLiteral("remove"), id});
}

void Accounts::setBook(const QString &id, const QString &url, bool enabled)
{
    run(tr("Saving …"), {QStringLiteral("book"), id, url, enabled ? QStringLiteral("on") : QStringLiteral("off")});
}

void Accounts::setCalendar(const QString &id, const QString &url, bool enabled)
{
    run(tr("Saving …"), {QStringLiteral("calendar"), id, url, enabled ? QStringLiteral("on") : QStringLiteral("off")});
}

void Accounts::sync()
{
    run(tr("Syncing …"), {QStringLiteral("sync")});
}

void Accounts::cancel()
{
    if (m_proc)
        m_proc->terminate();
}
