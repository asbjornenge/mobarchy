#include "maildata.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace {
const QString Command = QStringLiteral("mobarchy-mail");

QString mailDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/mobarchy/mail");
}
}

MailData::MailData(QObject *parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(400);
    connect(&m_settle, &QTimer::timeout, this, &MailData::refresh);
    // (the database and its journal change as the watcher writes: the directory)
    QDir().mkpath(mailDir());
    m_watcher.addPath(mailDir());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    QTimer::singleShot(0, this, &MailData::refresh);
}

void MailData::setCategory(const QString &c)
{
    if (c == m_category)
        return;
    m_category = c;
    emit categoryChanged();
    refresh();
}

void MailData::setSnoozed(bool s)
{
    if (s == m_snoozed)
        return;
    m_snoozed = s;
    emit categoryChanged();
    refresh();
}

void MailData::run(const QStringList &args, std::function<void(const QJsonObject &)> done, const QString &input,
                   std::function<void(const QString &)> failed)
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, done, failed](int code, QProcess::ExitStatus status) {
        const QJsonObject o = QJsonDocument::fromJson(p->readAllStandardOutput().trimmed()).object();
        p->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            const QString why = o.value(QStringLiteral("error")).toString(tr("Mail did not answer."));
            if (failed)
                failed(why);
            m_error = why;
            emit errorChanged();
            return;
        }
        done(o);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, failed](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        m_error = tr("mobarchy-mail is not installed");
        if (failed)
            failed(m_error);
        emit errorChanged();
    });
    p->start(Command, args);
    if (!input.isNull()) {
        p->write(input.toUtf8());
        p->closeWriteChannel();
    }
}

void MailData::refresh()
{
    const int generation = ++m_generation;
    QStringList args{QStringLiteral("list")};
    if (m_snoozed)
        args << QStringLiteral("--snoozed");
    else if (!m_category.isEmpty())
        args << QStringLiteral("--category") << m_category;
    run(args, [this, generation](const QJsonObject &o) {
        if (generation != m_generation)
            return;
        m_messages = o.value(QStringLiteral("messages")).toArray().toVariantList();
        m_unread = o.value(QStringLiteral("unread")).toObject().toVariantMap();
        m_accounts = o.value(QStringLiteral("accounts")).toArray().toVariantList();
        emit messagesChanged();
    });
}

void MailData::sync()
{
    if (m_syncing)
        return;
    m_syncing = true;
    emit syncingChanged();
    auto finish = [this] {
        m_syncing = false;
        emit syncingChanged();
        refresh();
    };
    run({QStringLiteral("sync")}, [this, finish](const QJsonObject &o) {
        // an account that failed says why (the rest synced)
        QString why;
        const QJsonObject accounts = o.value(QStringLiteral("accounts")).toObject();
        for (auto it = accounts.begin(); it != accounts.end(); ++it)
            if (const QString e = it.value().toObject().value(QStringLiteral("error")).toString(); !e.isEmpty())
                why = it.key() + QStringLiteral(": ") + e;
        if (why != m_error) {
            m_error = why;
            emit errorChanged();
        }
        finish();
    }, {}, [finish](const QString &) { finish(); });
}

void MailData::drop(const QString &id)
{
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].toMap().value(QStringLiteral("id")).toString() == id) {
            m_messages.removeAt(i);
            emit messagesChanged();
            return;
        }
    }
}

void MailData::open(const QString &id)
{
    run({QStringLiteral("read"), id}, [this](const QJsonObject &o) { emit opened(o.toVariantMap()); });
    // read: at once in the list
    for (int i = 0; i < m_messages.size(); ++i) {
        QVariantMap m = m_messages[i].toMap();
        if (m.value(QStringLiteral("id")).toString() == id && !m.value(QStringLiteral("seen")).toBool()) {
            m.insert(QStringLiteral("seen"), true);
            m_messages[i] = m;
            emit messagesChanged();
        }
    }
}

void MailData::archive(const QString &id)
{
    drop(id);
    run({QStringLiteral("archive"), id}, [this](const QJsonObject &) { refresh(); },
        {}, [this](const QString &) { refresh(); });
}

void MailData::trash(const QString &id)
{
    drop(id);
    run({QStringLiteral("trash"), id}, [this](const QJsonObject &) { refresh(); },
        {}, [this](const QString &) { refresh(); });
}

void MailData::snooze(const QString &id, qint64 until)
{
    drop(id);
    run({QStringLiteral("snooze"), id, QString::number(until)}, [this](const QJsonObject &) { refresh(); },
        {}, [this](const QString &) { refresh(); });
}

void MailData::unsnooze(const QString &id)
{
    drop(id);
    run({QStringLiteral("unsnooze"), id}, [this](const QJsonObject &) { refresh(); });
}

void MailData::setSeen(const QString &id, bool seen)
{
    // at once in the list (the command keeps it so until the server has it)
    for (int i = 0; i < m_messages.size(); ++i) {
        QVariantMap m = m_messages[i].toMap();
        if (m.value(QStringLiteral("id")).toString() == id) {
            m.insert(QStringLiteral("seen"), seen);
            m_messages[i] = m;
            emit messagesChanged();
            break;
        }
    }
    run({QStringLiteral("seen"), id, seen ? QStringLiteral("on") : QStringLiteral("off")},
        [this](const QJsonObject &) { refresh(); });
}

void MailData::saveAttachment(const QString &id, int index)
{
    run({QStringLiteral("attachment"), id, QString::number(index)},
        [this](const QJsonObject &o) { emit saved(o.value(QStringLiteral("saved")).toString()); });
}

void MailData::send(const QString &account, const QString &to, const QString &cc, const QString &subject,
                    const QString &body, const QString &replyTo)
{
    QStringList args{QStringLiteral("send"), QStringLiteral("--account"), account, QStringLiteral("--to"), to,
                     QStringLiteral("--subject"), subject};
    if (!cc.isEmpty())
        args << QStringLiteral("--cc") << cc;
    if (!replyTo.isEmpty())
        args << QStringLiteral("--reply-to") << replyTo;
    // (the text on stdin: any length, any characters)
    run(args, [this](const QJsonObject &) { emit sent(true, {}); }, body,
        [this](const QString &why) { emit sent(false, why); });
}
