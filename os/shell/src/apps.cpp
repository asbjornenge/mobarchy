#include "apps.h"

#include <QDirIterator>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <functional>

namespace {

QStringList applicationDirs()
{
    // $XDG_DATA_HOME/applications first, then $XDG_DATA_DIRS: earlier ones win.
    return QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
}

// systemd unit names allow [A-Za-z0-9:_.-] (and \ for escapes, which we avoid).
QString unitSafe(QString s)
{
    for (QChar &c : s) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != u':' && c != u'_' && c != u'.'
            && c != u'-')
            c = u'_';
    }
    return s;
}

}  // namespace

AppModel::AppModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_reload.setSingleShot(true);
    m_reload.setInterval(500);
    connect(&m_reload, &QTimer::timeout, this, &AppModel::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reload,
            qOverload<>(&QTimer::start));
    reload();
}

void AppModel::reload()
{
    const QStringList languages = desktopentry::languagesFor(
        qEnvironmentVariable("LC_ALL", qEnvironmentVariable("LC_MESSAGES", qEnvironmentVariable("LANG"))));
    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts);

    // The user's own directory exists, so it is watched: apps added later (a web app
    // installed from the browser) show up right away.
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation));

    QSet<QString> seen;
    QList<DesktopEntry> all;
    QStringList watched;
    for (const QString &dir : applicationDirs()) {
        if (!QFileInfo(dir).isDir())
            continue;
        watched << dir;
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString path = it.next();
            // The desktop file ID: the path below applications/, with / replaced by -.
            const QString id = path.mid(dir.size() + 1).replace(u'/', u'-');
            if (seen.contains(id))
                continue;
            seen.insert(id);
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            all << desktopentry::parse(id, path, f.readAll(), languages, desktops);
            const QString sub = QFileInfo(path).absolutePath();
            if (!watched.contains(sub))
                watched << sub;
        }
    }

    QList<DesktopEntry> visible;
    std::copy_if(all.cbegin(), all.cend(), std::back_inserter(visible),
                 [](const DesktopEntry &e) { return e.visible; });
    std::sort(visible.begin(), visible.end(), [](const DesktopEntry &a, const DesktopEntry &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });

    beginResetModel();
    m_all = all;
    m_apps = visible;
    endResetModel();

    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_watcher.addPaths(watched);
}

int AppModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_apps.size());
}

QVariant AppModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const DesktopEntry &e = m_apps.at(index.row());
    switch (role) {
    case IdRole: return e.id;
    case NameRole: return e.name;
    case IconRole: return e.icon;
    default: return {};
    }
}

QHash<int, QByteArray> AppModel::roleNames() const
{
    return {{IdRole, "appId"}, {NameRole, "name"}, {IconRole, "icon"}};
}

bool AppModel::launch(const QString &appId)
{
    // A desktop file ID, with or without ".desktop" (app IDs come without it).
    const QString id = appId.endsWith(QLatin1String(".desktop")) ? appId : appId + QStringLiteral(".desktop");
    // (hidden apps too: the shell opens some from elsewhere, as the camera)
    const auto it = std::find_if(m_all.cbegin(), m_all.cend(),
                                 [&](const DesktopEntry &e) { return e.id == id; });
    if (it == m_all.cend())
        return false;
    QStringList args = desktopentry::execArgs(*it);
    if (args.isEmpty()) {
        qWarning() << "cannot parse Exec of" << id << ":" << it->exec;
        return false;
    }
    if (it->terminal)
        args.prepend(qEnvironmentVariable("MOBARCHY_TERMINAL", QStringLiteral("foot")));

    QString base = id;
    base.chop(int(sizeof(".desktop") - 1));
    const QString unit = QStringLiteral("app-mobarchy-%1-%2.scope")
                             .arg(unitSafe(base))
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QChar(u'0'));
    bool ok;
    if (!QStandardPaths::findExecutable(QStringLiteral("systemd-run")).isEmpty()) {
        ok = QProcess::startDetached(QStringLiteral("systemd-run"),
                                     QStringList{QStringLiteral("--user"), QStringLiteral("--scope"),
                                                 QStringLiteral("--quiet"), QStringLiteral("--collect"),
                                                 QStringLiteral("--slice=app.slice"),
                                                 QStringLiteral("--unit=") + unit, QStringLiteral("--")}
                                         + args,
                                     it->workingDir);
    } else {
        ok = QProcess::startDetached(args.first(), args.mid(1), it->workingDir);
    }
    if (!ok)
        qWarning() << "failed to start" << id << args;
    return ok;
}

const DesktopEntry *AppModel::forAppId(const QString &appId) const
{
    if (appId.isEmpty())
        return nullptr;
    const QString file = appId + QStringLiteral(".desktop");
    // Exact desktop ID, then StartupWMClass, then the ID ignoring case.
    const std::function<bool(const DesktopEntry &)> matchers[] = {
        [&](const DesktopEntry &e) { return e.id == file; },
        [&](const DesktopEntry &e) { return e.wmClass == appId; },
        [&](const DesktopEntry &e) { return e.id.compare(file, Qt::CaseInsensitive) == 0; },
    };
    for (const auto &match : matchers) {
        const auto it = std::find_if(m_all.cbegin(), m_all.cend(), match);
        if (it != m_all.cend())
            return &*it;
    }
    return nullptr;
}

QStringList AppModel::windowIds(const QString &appId) const
{
    QString base = appId.endsWith(QLatin1String(".desktop")) ? appId.chopped(8) : appId;
    QStringList ids{base};
    const auto it = std::find_if(m_all.cbegin(), m_all.cend(),
                                 [&](const DesktopEntry &e) { return e.id == base + QStringLiteral(".desktop"); });
    if (it != m_all.cend() && !it->wmClass.isEmpty() && !ids.contains(it->wmClass))
        ids << it->wmClass;
    return ids;
}

QString AppModel::iconForAppId(const QString &appId) const
{
    const DesktopEntry *e = forAppId(appId);
    return e && !e->icon.isEmpty() ? e->icon : appId;
}

QString AppModel::nameForAppId(const QString &appId) const
{
    const DesktopEntry *e = forAppId(appId);
    return e ? e->name : appId;
}
