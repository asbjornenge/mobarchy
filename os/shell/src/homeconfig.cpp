#include "homeconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJSValue>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

HomeConfig::HomeConfig(QObject *parent)
    : QObject(parent)
    , m_path(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
             + QStringLiteral("/mobarchy/home.json"))
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(100);
    connect(&m_settle, &QTimer::timeout, this, &HomeConfig::load);
    // the file, and its directory: a file replaced (written to a temporary one and renamed,
    // as the agent and QSaveFile do) is a new file the watcher must be told about again
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    load();
}

void HomeConfig::watch()
{
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
    if (QFile::exists(m_path) && !m_watcher.files().contains(m_path))
        m_watcher.addPath(m_path);
}

void HomeConfig::load()
{
    watch();
    QFile in(m_path);
    QVariantMap config;
    if (in.open(QIODevice::ReadOnly))
        config = QJsonDocument::fromJson(in.readAll()).object().toVariantMap();
    if (config == m_config)
        return;
    m_config = config;
    emit changed();
}

void HomeConfig::set(const QString &key, const QVariant &given)
{
    // (a JavaScript array comes as a QJSValue, which JSON would write as null)
    const QVariant value = given.canConvert<QJSValue>()
        ? given.value<QJSValue>().toVariant(QJSValue::ConvertJSObjects) : given;
    if (value.isNull())
        m_config.remove(key);
    else
        m_config.insert(key, value);
    QSaveFile out(m_path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(QJsonObject::fromVariantMap(m_config)).toJson(QJsonDocument::Indented));
        out.commit();
    }
    emit changed();
}
