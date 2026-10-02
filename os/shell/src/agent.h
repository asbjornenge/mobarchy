// The on-device agent (mobarchy-agent): runs tasks and setup with their JSON event stream
// (ADR 0006), and knows which agents exist and which one is active (ADR 0010).
#pragma once

#include <QObject>
#include <QProcess>
#include <functional>
#include <QtQml/qqmlregistration.h>

class Agent : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // [{id, name, installed, ready, detail, available}]
    Q_PROPERTY(QVariantList agents READ agents NOTIFY agentsChanged)
    Q_PROPERTY(QString active READ active NOTIFY agentsChanged)
    // [{id, title, updated, current, agents}], newest first
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    // A login is waiting for a code the owner may paste (event kind "code").
    Q_PROPERTY(bool awaitingCode READ awaitingCode NOTIFY awaitingCodeChanged)

public:
    explicit Agent(QObject *parent = nullptr);
    ~Agent() override;

    bool running() const { return m_proc.state() != QProcess::NotRunning; }

    QVariantList agents() const { return m_agents; }
    QString active() const { return m_active; }
    bool awaitingCode() const { return m_awaitingCode; }
    QVariantList conversations() const { return m_conversations; }

    Q_INVOKABLE void ask(const QString &task);
    Q_INVOKABLE void stop();
    // Re-read which agents are installed and logged in.
    Q_INVOKABLE void refresh();
    // Make this agent the active one; if it is not ready, set it up (install, log in).
    Q_INVOKABLE void choose(const QString &id);
    // Conversations (kept by mobarchy-agent, whichever agent answered).
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void openConversation(const QString &id);
    Q_INVOKABLE void loadConversation();      // -> conversationLoaded(turns)
    Q_INVOKABLE void refreshConversations();
    Q_INVOKABLE void openUrl(const QString &url);
    // Hand a pasted login code to the running setup.
    Q_INVOKABLE void sendCode(const QString &code);

signals:
    // kind: "text" | "tool" | "done" | "error" | "url" (a link to open, e.g. a login page)
    void event(const QString &kind, const QString &text);
    void runningChanged();
    void agentsChanged();
    void awaitingCodeChanged();
    void conversationsChanged();
    // [{role, text, agent}]: role user | assistant | tool | error | note
    void conversationLoaded(const QVariantList &turns);

private:
    void readEvents();
    void finished(int code, QProcess::ExitStatus status);
    void start(const QStringList &args);
    QString program() const;
    // Run a short mobarchy-agent command; `done` gets its stdout.
    void command(const QStringList &args, std::function<void(const QByteArray &)> done = {});

    QProcess m_proc;
    QProcess m_list;
    QVariantList m_agents;
    QString m_active;
    QVariantList m_conversations;
    bool m_awaitingCode = false;
    QByteArray m_stderr;
    bool m_sawError = false;
    bool m_stopped = false;
};
