// Opening the Messages app at a conversation (an SMS notification tapped): the running app
// answers org.mobarchy.Messages.Show(number) on the session bus; otherwise it is started
// (mobarchy-shell --messages <number>, in an app scope of its own, as the shell starts apps).
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class MessagesThread : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the Messages app's: answer Show on the bus
    Q_PROPERTY(bool service READ service WRITE setService NOTIFY serviceChanged)

public:
    explicit MessagesThread(QObject *parent = nullptr) : QObject(parent) {}
    bool service() const { return m_service; }
    void setService(bool on);
    // From the shell: show the conversation with number in the Messages app.
    Q_INVOKABLE static void open(const QString &number);

public slots:
    void Show(const QString &number);

signals:
    void serviceChanged();
    void showRequested(const QString &number);

private:
    bool m_service = false;
};
