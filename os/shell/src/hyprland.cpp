#include "hyprland.h"

#include <QJsonDocument>

Hyprland::Hyprland(QObject *parent)
    : QObject(parent)
{
    m_retry.setInterval(2000);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &Hyprland::listen);
    connect(&m_events, &QLocalSocket::readyRead, this, [this] {
        m_pending += m_events.readAll();
        qsizetype end;
        while ((end = m_pending.indexOf('\n')) >= 0) {
            const QByteArray line = m_pending.left(end);
            m_pending.remove(0, end + 1);
            if (line.startsWith("fullscreen>>"))
                emit fullscreenEvent();
        }
    });
    // (Hyprland restarted, or not up yet: again in a moment)
    connect(&m_events, &QLocalSocket::disconnected, &m_retry, qOverload<>(&QTimer::start));
    connect(&m_events, &QLocalSocket::errorOccurred, &m_retry, qOverload<>(&QTimer::start));
    listen();
}

QString Hyprland::socketPath(const QString &name) const
{
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature.isEmpty())
        return {};
    return qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/hypr/") + signature + QLatin1Char('/') + name;
}

void Hyprland::listen()
{
    const QString path = socketPath(QStringLiteral(".socket2.sock"));
    if (path.isEmpty())
        return;
    m_pending.clear();
    m_events.abort();
    m_events.connectToServer(path);
}

QByteArray Hyprland::request(const QByteArray &command)
{
    const QString path = socketPath(QStringLiteral(".socket.sock"));
    if (path.isEmpty())
        return {};
    // (short and local: answered at once; waited for, as hyprctl does)
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(200))
        return {};
    socket.write(command);
    socket.waitForBytesWritten(200);
    QByteArray answer;
    while (socket.waitForReadyRead(300))
        answer += socket.readAll();
    return answer;
}

QString Hyprland::dispatch(const QString &args)
{
    return QString::fromUtf8(request("dispatch " + args.toUtf8())).trimmed();
}

QVariant Hyprland::query(const QString &what)
{
    const QByteArray answer = request("j/" + what.toUtf8());
    if (answer.isEmpty())
        return {};
    return QJsonDocument::fromJson(answer).toVariant();
}
