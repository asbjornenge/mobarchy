#include "locationagent.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcLocation, "mobarchy.location")

namespace {
const QString Service = QStringLiteral("org.freedesktop.GeoClue2");
const QString AgentPath = QStringLiteral("/org/freedesktop/GeoClue2/Agent");

// the shell, and the agent layer (mobarchy-mcp's location tool)
const QStringList Trusted = {QStringLiteral("org.mobarchy.Shell"), QStringLiteral("org.mobarchy.Agent")};

bool ownerAllowed(const QString &desktopId)
{
    QFile f(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/mobarchy/location.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QJsonDocument::fromJson(f.readAll()).object().value(desktopId).toBool();
}
}

LocationAgent::LocationAgent(QObject *parent)
    : QObject(parent)
    , m_watcher(Service, QDBusConnection::systemBus(), QDBusServiceWatcher::WatchForRegistration)
{
    auto bus = QDBusConnection::systemBus();
    if (!bus.registerObject(AgentPath, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties))
        qCWarning(lcLocation) << "cannot be GeoClue's agent:" << bus.lastError().message();
    // GeoClue is started when first asked for, and may restart: be its agent each time
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &LocationAgent::addAgent);
    addAgent();
}

void LocationAgent::addAgent()
{
    QDBusMessage add = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/freedesktop/GeoClue2/Manager"),
                                                      QStringLiteral("org.freedesktop.GeoClue2.Manager"),
                                                      QStringLiteral("AddAgent"));
    add << QStringLiteral("org.mobarchy.Shell");
    QDBusConnection::systemBus().callWithCallback(add, this, nullptr, nullptr);
}

void LocationAgent::AuthorizeApp(const QString &desktopId, uint requestedAccuracyLevel)
{
    const bool allowed = Trusted.contains(desktopId) || ownerAllowed(desktopId);
    qCInfo(lcLocation) << (allowed ? "location for" : "no location for") << desktopId;
    // (two answers: a reply of our own)
    setDelayedReply(true);
    connection().send(message().createReply({allowed, allowed ? requestedAccuracyLevel : 0u}));
}
