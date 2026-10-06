#include "themesource.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
// Mobarchy's own: Tokyo Night (Omarchy's default). A theme's missing roles come from here.
const QVariantMap Defaults = {
    {QStringLiteral("bg"), QStringLiteral("#1a1b26")},
    {QStringLiteral("bgDark"), QStringLiteral("#16161e")},
    {QStringLiteral("panel"), QStringLiteral("#24283b")},
    {QStringLiteral("line"), QStringLiteral("#292e42")},
    {QStringLiteral("fg"), QStringLiteral("#c0caf5")},
    {QStringLiteral("muted"), QStringLiteral("#565f89")},
    {QStringLiteral("accent"), QStringLiteral("#7aa2f7")},
    {QStringLiteral("green"), QStringLiteral("#9ece6a")},
    {QStringLiteral("red"), QStringLiteral("#f7768e")},
    {QStringLiteral("magenta"), QStringLiteral("#bb9af7")},
};
const QString DefaultFont = QStringLiteral("JetBrainsMono Nerd Font");
const QString DefaultId = QStringLiteral("mobarchy");

QString home(QStandardPaths::StandardLocation where, const QString &path)
{
    return QStandardPaths::writableLocation(where) + QLatin1Char('/') + path;
}
QString userThemes() { return home(QStandardPaths::GenericDataLocation, QStringLiteral("mobarchy/themes")); }
QString systemThemes() { return QStringLiteral("/usr/share/mobarchy/themes"); }
QString activeFile() { return home(QStandardPaths::GenericConfigLocation, QStringLiteral("mobarchy/theme")); }
}  // namespace

ThemeSource::ThemeSource(QObject *parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(100);
    connect(&m_settle, &QTimer::timeout, this, &ThemeSource::load);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    load();
}

void ThemeSource::load()
{
    QString id = DefaultId;
    QFile active(activeFile());
    if (active.open(QIODevice::ReadOnly)) {
        const QString wanted = QString::fromUtf8(active.readAll()).trimmed();
        if (!wanted.isEmpty())
            id = wanted;
    }

    QJsonObject theme;
    for (const QString &dir : {userThemes(), systemThemes()}) {
        QFile f(dir + QLatin1Char('/') + id + QStringLiteral(".json"));
        if (f.open(QIODevice::ReadOnly)) {
            theme = QJsonDocument::fromJson(f.readAll()).object();
            break;
        }
    }
    if (theme.isEmpty() && id != DefaultId)
        qWarning() << "theme:" << id << "not found (or not JSON): Mobarchy's";

    QVariantMap colors = Defaults;
    const QJsonObject given = theme.value(QStringLiteral("colors")).toObject();
    for (auto it = given.begin(); it != given.end(); ++it) {
        const QColor c = QColor::fromString(it.value().toString());
        if (Defaults.contains(it.key()) && c.isValid())
            colors[it.key()] = c.name(QColor::HexArgb);
    }
    const QString font = theme.value(QStringLiteral("font")).toString(DefaultFont);
    const QString name = theme.value(QStringLiteral("name")).toString(QStringLiteral("Mobarchy"));

    watch();
    if (id == m_id && colors == m_colors && font == m_font && name == m_name)
        return;
    m_id = id;
    m_name = name;
    m_colors = colors;
    m_font = font.isEmpty() ? DefaultFont : font;
    qInfo() << "theme:" << m_id;
    emit changed();
}

// The files and directories that decide the theme; created as they appear.
void ThemeSource::watch()
{
    QDir().mkpath(QFileInfo(activeFile()).path());
    QDir().mkpath(userThemes());
    QStringList paths = {QFileInfo(activeFile()).path(), userThemes()};
    if (QFile::exists(activeFile()))
        paths << activeFile();
    if (QDir(systemThemes()).exists())
        paths << systemThemes();
    for (const QString &dir : {userThemes(), systemThemes()})
        for (const QFileInfo &f : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files))
            paths << f.absoluteFilePath();
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    for (const QString &p : paths)
        if (!watched.contains(p))
            m_watcher.addPath(p);
}
