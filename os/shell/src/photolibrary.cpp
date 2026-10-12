#include "photolibrary.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QProcess>
#include <QRandomGenerator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

namespace {

const QStringList ImageSuffixes = {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                                   QStringLiteral("webp"), QStringLiteral("gif")};
const QStringList VideoSuffixes = {QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv")};
// how deep into ~/Pictures: Pictures/<album>/<one more>
constexpr int MaxDepth = 3;

}  // namespace

PhotoLibrary::PhotoLibrary(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(300);  // a burst of changes (a photo being written) settles first
    connect(&m_rescan, &QTimer::timeout, this, &PhotoLibrary::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_rescan, qOverload<>(&QTimer::start));
    QDir().mkpath(root());
    scan();
}

QString PhotoLibrary::root()
{
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
}

void PhotoLibrary::scan()
{
    const QString base = QDir(root()).absolutePath();
    QList<Picture> all;
    QStringList dirs = {base};
    QDirIterator it(base, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo f = it.nextFileInfo();
        const QString rel = QDir(base).relativeFilePath(f.absoluteFilePath());
        if (rel.startsWith(u'.') || rel.contains(QStringLiteral("/.")) || rel.count(u'/') >= MaxDepth)
            continue;
        if (f.isDir()) {
            dirs << f.absoluteFilePath();
            continue;
        }
        const QString suffix = f.suffix().toLower();
        const bool video = VideoSuffixes.contains(suffix);
        if (!video && !ImageSuffixes.contains(suffix))
            continue;
        const int slash = int(rel.indexOf(u'/'));
        all << Picture{f.absoluteFilePath(), slash < 0 ? QString() : rel.left(slash), f.lastModified(), video};
    }
    std::sort(all.begin(), all.end(), [](const Picture &a, const Picture &b) { return a.time > b.time; });

    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    m_watcher.addPaths(dirs);

    beginResetModel();
    m_all = all;
    m_shown.clear();
    for (int i = 0; i < m_all.size(); ++i)
        if (m_album.isEmpty() || m_all[i].album == m_album)
            m_shown << i;
    endResetModel();
    emit countChanged();
    emit albumsChanged();
}

void PhotoLibrary::filter()
{
    beginResetModel();
    m_shown.clear();
    for (int i = 0; i < m_all.size(); ++i)
        if (m_album.isEmpty() || m_all[i].album == m_album)
            m_shown << i;
    endResetModel();
    emit countChanged();
}

void PhotoLibrary::setAlbum(const QString &album)
{
    if (album == m_album)
        return;
    m_album = album;
    filter();
    emit albumChanged();
}

QVariantList PhotoLibrary::albums() const
{
    QHash<QString, int> counts;
    for (const Picture &p : m_all)
        if (!p.album.isEmpty())
            counts[p.album]++;
    QList<QPair<QString, int>> sorted;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        sorted << qMakePair(it.key(), it.value());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    QVariantList out;
    for (const auto &[name, n] : std::as_const(sorted))
        out << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("count"), n}};
    return out;
}

int PhotoLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

QVariant PhotoLibrary::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Picture &p = m_all.at(m_shown.at(index.row()));
    switch (role) {
    case FilePathRole: return p.path;
    case AlbumRole: return p.album;
    case TimeRole: return p.time;
    case VideoRole: return p.video;
    case DayRole: return p.time.date().toString(Qt::ISODate);
    default: return {};
    }
}

QHash<int, QByteArray> PhotoLibrary::roleNames() const
{
    return {{FilePathRole, "filePath"}, {AlbumRole, "album"}, {TimeRole, "time"}, {VideoRole, "video"},
            {DayRole, "day"}};
}

int PhotoLibrary::indexOf(const QString &path) const
{
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (int row = 0; row < m_shown.size(); ++row)
        if (m_all.at(m_shown.at(row)).path == wanted)
            return row;
    return -1;
}

QString PhotoLibrary::pathAt(int row) const
{
    return row >= 0 && row < m_shown.size() ? m_all.at(m_shown.at(row)).path : QString();
}

QDateTime PhotoLibrary::timeAt(int row) const
{
    return row >= 0 && row < m_shown.size() ? m_all.at(m_shown.at(row)).time : QDateTime();
}

bool PhotoLibrary::trash(const QString &path)
{
    const QString file = QFileInfo(path).absoluteFilePath();
    if (!file.startsWith(QDir(root()).absolutePath() + u'/'))
        return false;
    if (!QFile::moveToTrash(file))
        return false;
    scan();
    return true;
}

void PhotoLibrary::setService(bool on)
{
    if (on == m_service)
        return;
    m_service = on;
    if (on) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.registerObject(QStringLiteral("/org/mobarchy/Photos"), QStringLiteral("org.mobarchy.Photos"),
                                this, QDBusConnection::ExportAllSlots)
            || !bus.registerService(QStringLiteral("org.mobarchy.Photos")))
            qWarning("photos: can't take org.mobarchy.Photos on the session bus");
    }
    emit serviceChanged();
}

void PhotoLibrary::Show(const QString &path)
{
    if (!path.isEmpty())
        setAlbum(QString());
    emit showRequested(QFileInfo(path).absoluteFilePath());
}

void Photos::open(const QString &path)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.interface() && bus.interface()->isServiceRegistered(QStringLiteral("org.mobarchy.Photos"))) {
        bus.asyncCall(QDBusMessage::createMethodCall(QStringLiteral("org.mobarchy.Photos"),
                                                     QStringLiteral("/org/mobarchy/Photos"),
                                                     QStringLiteral("org.mobarchy.Photos"), QStringLiteral("Show"))
                      << path);
        return;
    }
    const QString unit = QStringLiteral("app-mobarchy-org.mobarchy.Photos-%1.scope")
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QChar(u'0'));
    QProcess::startDetached(QStringLiteral("systemd-run"),
                            {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"),
                             QStringLiteral("--collect"), QStringLiteral("--slice=app.slice"),
                             QStringLiteral("--unit=") + unit, QStringLiteral("--"), QStringLiteral("mobarchy-shell"),
                             QStringLiteral("--photos"), path});
}

