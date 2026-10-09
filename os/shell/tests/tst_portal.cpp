// Portal: 204 is the open Internet; a page or a redirect is a login page in the way.
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "../src/portal.h"

class Http : public QTcpServer
{
public:
    QByteArray answer;
    Http()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s] {
                s->readAll();
                s->write(answer);
                s->disconnectFromHost();
            });
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/generate_204").arg(serverPort())); }
};

class TstPortal : public QObject
{
    Q_OBJECT

    bool ask(Portal &p, Http &h, const QByteArray &answer)
    {
        h.answer = answer;
        p.setCheckUrl(h.url());
        QSignalSpy spy(&p, &Portal::changed);
        p.check();
        spy.wait(3000);
        return p.portal();
    }

private slots:
    void openInternet()
    {
        Http h;
        Portal p;
        p.setNetwork(QStringLiteral("cafe"));
        QVERIFY(!ask(p, h, "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n"));
    }
    void pageInTheWay()
    {
        Http h;
        Portal p;
        p.setNetwork(QStringLiteral("airport"));
        QVERIFY(ask(p, h, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nlogin"));
        QCOMPARE(p.url(), QUrl(QStringLiteral("http://neverssl.com/")));
    }
    void redirectToLogin()
    {
        Http h;
        Portal p;
        p.setNetwork(QStringLiteral("hotel"));
        QVERIFY(ask(p, h, "HTTP/1.1 302 Found\r\nLocation: http://login.example/x\r\nContent-Length: 0\r\n\r\n"));
        QCOMPARE(p.url(), QUrl(QStringLiteral("http://login.example/x")));
        // signed in: the next check gets through
        h.answer = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
        QSignalSpy spy(&p, &Portal::changed);
        p.check();
        QVERIFY(spy.wait(3000));
        QVERIFY(!p.portal());
    }
};

QTEST_MAIN(TstPortal)
#include "tst_portal.moc"
