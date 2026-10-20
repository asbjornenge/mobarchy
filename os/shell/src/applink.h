// Opening one of the shell's apps at something in it (a notification tapped, a contact's
// message button): the running app answers <name>.Show(arg) on the session bus (an AppLink
// with service on, in the app); otherwise it is started (mobarchy-shell <flag> <arg>, in an
// app scope of its own, as the shell starts apps). From QML: AppLinks.open(name, flag, arg);
// Messaging.open(number) for Messages.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class AppLink : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the app's name on the bus (its desktop ID): "org.mobarchy.Messages"
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    // the app's: answer Show on the bus
    Q_PROPERTY(bool service READ service WRITE setService NOTIFY changed)

public:
    explicit AppLink(QObject *parent = nullptr) : QObject(parent) {}
    QString name() const { return m_name; }
    void setName(const QString &name);
    bool service() const { return m_service; }
    void setService(bool on);
    static void open(const QString &name, const QString &flag, const QString &arg);

public slots:
    void Show(const QString &arg);

signals:
    void changed();
    void showRequested(const QString &arg);

private:
    void announce();
    QString m_name;
    bool m_service = false;
    bool m_announced = false;
};

class AppLinks : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit AppLinks(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE static void open(const QString &name, const QString &flag, const QString &arg)
    {
        AppLink::open(name, flag, arg);
    }
};

class Messaging : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Messaging(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE static void open(const QString &number)
    {
        AppLink::open(QStringLiteral("org.mobarchy.Messages"), QStringLiteral("--messages"), number);
    }
};
