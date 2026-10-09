#include "portal.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
// Android's check: 204 and an empty body when nothing is in the way.
const QUrl Check(QStringLiteral("http://connectivitycheck.gstatic.com/generate_204"));
// For a portal that doesn't redirect: a page that is plain HTTP on purpose.
const QUrl Fallback(QStringLiteral("http://neverssl.com/"));
}

Portal::Portal(QObject *parent)
    : QObject(parent)
{
    m_check = Check;
    m_net.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    m_retry.setInterval(20000);
    connect(&m_retry, &QTimer::timeout, this, &Portal::check);
}

void Portal::setNetwork(const QString &network)
{
    if (network == m_network)
        return;
    m_network = network;
    m_generation++;
    set(false, {});
    m_retry.stop();
    emit networkChanged();
    // (a moment for DHCP and DNS)
    if (!network.isEmpty())
        QTimer::singleShot(3000, this, &Portal::check);
}

void Portal::check()
{
    if (m_network.isEmpty())
        return;
    QNetworkRequest request(m_check);
    request.setTransferTimeout(8000);
    QNetworkReply *reply = m_net.get(request);
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            // no answer at all: no Internet (or not yet), which isn't a portal; ask again
            m_retry.start();
            return;
        }
        if (status == 204) {
            set(false, {});
            m_retry.stop();
            return;
        }
        QUrl url = Fallback;
        if (status >= 300 && status < 400) {
            const QUrl to = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            if (to.isValid())
                url = m_check.resolved(to);
        }
        set(true, url);
        m_retry.start();
    });
}

void Portal::set(bool portal, const QUrl &url)
{
    if (portal == m_portal && url == m_url)
        return;
    m_portal = portal;
    m_url = url;
    emit changed();
}
