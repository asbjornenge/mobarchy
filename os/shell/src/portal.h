// Captive portals (a café's or an airport's Wi-Fi login page): after joining a network, ask
// a plain-HTTP address that answers 204 on an open Internet. Any other answer is a portal in
// the way: `portal` and its address, until the check comes through (re-tried while it lasts).
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class Portal : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the network (SSID) to check; "" when not connected
    Q_PROPERTY(QString network READ network WRITE setNetwork NOTIFY networkChanged)
    Q_PROPERTY(bool portal READ portal NOTIFY changed)
    // where to sign in: the portal's redirect, or a plain-HTTP page it will intercept
    Q_PROPERTY(QUrl url READ url NOTIFY changed)

public:
    explicit Portal(QObject *parent = nullptr);

    QString network() const { return m_network; }
    void setNetwork(const QString &network);
    bool portal() const { return m_portal; }
    QUrl url() const { return m_url; }

    Q_INVOKABLE void check();
    // (for tests: another address to ask)
    void setCheckUrl(const QUrl &url) { m_check = url; }

signals:
    void networkChanged();
    void changed();

private:
    void set(bool portal, const QUrl &url);

    QNetworkAccessManager m_net;
    QTimer m_retry;
    QString m_network;
    bool m_portal = false;
    QUrl m_url;
    QUrl m_check;
    int m_generation = 0;
};
