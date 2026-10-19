// The owner's mail (ADR 0038), through the `mobarchy-mail` command (the agent package's: IMAP
// and SMTP for the accounts, a local copy of the inboxes): the Mail app's backend. It follows
// the local copy (mobarchy-mail watch, the user service, keeps it current) and asks again when
// it changes.
#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class MailData : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // what is shown: "" (all), "people", "notifications", "newsletters"; or the snoozed
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(bool snoozed READ snoozed WRITE setSnoozed NOTIFY categoryChanged)
    // [{id, account, account_name, from, from_addr, to, subject, date (s), snippet, seen,
    //   flagged, category, attachments, snoozed_until}], newest first
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    // unread in the inbox, by category; the accounts with mail [{id, user}]
    Q_PROPERTY(QVariantMap unread READ unread NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY messagesChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit MailData(QObject *parent = nullptr);

    QString category() const { return m_category; }
    void setCategory(const QString &c);
    bool snoozed() const { return m_snoozed; }
    void setSnoozed(bool s);
    QVariantList messages() const { return m_messages; }
    QVariantMap unread() const { return m_unread; }
    QVariantList accounts() const { return m_accounts; }
    bool syncing() const { return m_syncing; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void sync();
    // the message in full: opened(message) when it has come ({from, to, cc, subject, date,
    // text, html, attachments [{index, name, type, size}]})
    Q_INVOKABLE void open(const QString &id);
    // out of the list at once, then on the server
    Q_INVOKABLE void archive(const QString &id);
    Q_INVOKABLE void trash(const QString &id);
    Q_INVOKABLE void snooze(const QString &id, qint64 until);
    Q_INVOKABLE void unsnooze(const QString &id);
    Q_INVOKABLE void setSeen(const QString &id, bool seen);
    Q_INVOKABLE void saveAttachment(const QString &id, int index);
    // sent(ok, error) when done
    Q_INVOKABLE void send(const QString &account, const QString &to, const QString &cc,
                          const QString &subject, const QString &body, const QString &replyTo);

signals:
    void categoryChanged();
    void messagesChanged();
    void syncingChanged();
    void errorChanged();
    void opened(const QVariantMap &message);
    void sent(bool ok, const QString &error);
    void saved(const QString &path);

private:
    void run(const QStringList &args, std::function<void(const QJsonObject &)> done,
             const QString &input = {}, std::function<void(const QString &)> failed = nullptr);
    void drop(const QString &id);

    QString m_category;
    bool m_snoozed = false;
    QVariantList m_messages, m_accounts;
    QVariantMap m_unread;
    bool m_syncing = false;
    QString m_error;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    int m_generation = 0;
};
