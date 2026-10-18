#include "themesource.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

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

QString activeId()
{
    QFile f(activeFile());
    if (f.open(QIODevice::ReadOnly)) {
        const QString id = QString::fromUtf8(f.readAll()).trimmed();
        if (!id.isEmpty())
            return id;
    }
    return DefaultId;
}

// A theme as the shell uses it: every colour role, the font, the wallpaper as a URL ("" if
// none). The owner's (userThemes) win over Mobarchy's. Empty map: no such theme.
QVariantMap readTheme(const QString &id)
{
    for (const QString &dir : {userThemes(), systemThemes()}) {
        QFile f(dir + QLatin1Char('/') + id + QStringLiteral(".json"));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject theme = QJsonDocument::fromJson(f.readAll()).object();
        QVariantMap colors = Defaults;
        const QJsonObject given = theme.value(QStringLiteral("colors")).toObject();
        for (auto it = given.begin(); it != given.end(); ++it) {
            const QColor c = QColor::fromString(it.value().toString());
            if (Defaults.contains(it.key()) && c.isValid())
                colors[it.key()] = c.name(QColor::HexArgb);
        }
        // the wallpaper: a file next to the theme (its name, not a path)
        const QString wallpaper = QFileInfo(theme.value(QStringLiteral("wallpaper")).toString()).fileName();
        const QString wallpaperPath = dir + QLatin1Char('/') + wallpaper;
        const QString font = theme.value(QStringLiteral("font")).toString();
        return {
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), theme.value(QStringLiteral("name")).toString(id)},
            {QStringLiteral("description"), theme.value(QStringLiteral("description")).toString()},
            {QStringLiteral("colors"), colors},
            {QStringLiteral("font"), font.isEmpty() ? DefaultFont : font},
            {QStringLiteral("wallpaper"), !wallpaper.isEmpty() && QFile::exists(wallpaperPath)
                                              ? QUrl::fromLocalFile(wallpaperPath).toString() : QString()},
            {QStringLiteral("own"), dir == userThemes()},
        };
    }
    return {};
}

// The files and directories that decide the themes, for a watcher; created as they appear.
QStringList watchedPaths()
{
    QDir().mkpath(QFileInfo(activeFile()).path());
    QDir().mkpath(userThemes());
    QStringList paths = {QFileInfo(activeFile()).path(), userThemes()};
    if (QFile::exists(activeFile()))
        paths << activeFile();
    if (QDir(systemThemes()).exists())
        paths << systemThemes();
    for (const QString &dir : {userThemes(), systemThemes()})
        for (const QFileInfo &f : QDir(dir).entryInfoList(QDir::Files))
            paths << f.absoluteFilePath();
    return paths;
}

void follow(QFileSystemWatcher &watcher)
{
    const QStringList watched = watcher.files() + watcher.directories();
    for (const QString &p : watchedPaths())
        if (!watched.contains(p))
            watcher.addPath(p);
}

void settleOn(QObject *owner, QFileSystemWatcher &watcher, QTimer &settle)
{
    settle.setSingleShot(true);
    settle.setInterval(100);
    QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, owner, [&settle] { settle.start(); });
    QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, owner, [&settle] { settle.start(); });
}
}  // namespace

ThemeSource::ThemeSource(QObject *parent)
    : QObject(parent)
{
    settleOn(this, m_watcher, m_settle);
    connect(&m_settle, &QTimer::timeout, this, &ThemeSource::load);
    load();
}

void ThemeSource::load()
{
    QString id = activeId();
    QVariantMap theme = readTheme(id);
    if (theme.isEmpty()) {
        if (id != DefaultId)
            qWarning() << "theme:" << id << "not found: Mobarchy's";
        theme = readTheme(DefaultId);
    }
    follow(m_watcher);
    const QVariantMap colors = theme.isEmpty() ? Defaults : theme.value(QStringLiteral("colors")).toMap();
    const QString font = theme.value(QStringLiteral("font"), DefaultFont).toString();
    const QString name = theme.value(QStringLiteral("name"), QStringLiteral("Mobarchy")).toString();
    const QString wallpaper = theme.value(QStringLiteral("wallpaper")).toString();
    const QString cards = theme.value(QStringLiteral("cards")).toString() == QLatin1String("outline")
                          ? QStringLiteral("outline") : QStringLiteral("filled");
    if (id == m_id && colors == m_colors && font == m_font && name == m_name && wallpaper == m_wallpaper
        && cards == m_cards)
        return;
    m_cards = cards;
    m_id = id;
    m_name = name;
    m_colors = colors;
    m_font = font;
    m_wallpaper = wallpaper;
    qInfo() << "theme:" << m_id;
    emit changed();
}

ThemeLibrary::ThemeLibrary(QObject *parent)
    : QObject(parent)
{
    settleOn(this, m_watcher, m_settle);
    connect(&m_settle, &QTimer::timeout, this, &ThemeLibrary::load);
    load();
}

void ThemeLibrary::load()
{
    QStringList ids;
    for (const QString &dir : {systemThemes(), userThemes()})
        for (const QFileInfo &f : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files))
            if (!ids.contains(f.completeBaseName()))
                ids << f.completeBaseName();
    const QString active = activeId();
    QVariantList themes;
    for (const QString &id : ids) {
        QVariantMap t = readTheme(id);
        if (t.isEmpty())
            continue;
        t[QStringLiteral("active")] = id == active;
        themes << t;
    }
    // Mobarchy's own first, then the rest by name
    std::sort(themes.begin(), themes.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        if (x[QStringLiteral("own")] != y[QStringLiteral("own")])
            return !x[QStringLiteral("own")].toBool();
        return x[QStringLiteral("name")].toString().localeAwareCompare(y[QStringLiteral("name")].toString()) < 0;
    });
    follow(m_watcher);
    if (themes != m_themes || active != m_active) {
        m_themes = themes;
        m_active = active;
        emit changed();
    }
}

bool ThemeLibrary::activate(const QString &id)
{
    if (readTheme(id).isEmpty())
        return false;
    QSaveFile f(activeFile());   // (whole, by rename: the shell watches it)
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(id.toUtf8() + '\n');
    return f.commit();
}

bool ThemeLibrary::remove(const QString &id)
{
    const QVariantMap t = readTheme(id);
    if (t.isEmpty() || !t.value(QStringLiteral("own")).toBool())
        return false;   // (Mobarchy's own stay)
    const QString wallpaper = QUrl(t.value(QStringLiteral("wallpaper")).toString()).toLocalFile();
    if (!wallpaper.isEmpty())
        QFile::remove(wallpaper);
    const bool removed = QFile::remove(userThemes() + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (removed && id == activeId())
        activate(DefaultId);
    return removed;
}
