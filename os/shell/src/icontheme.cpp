#include "icontheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

const QStringList Extensions = {QStringLiteral("png"), QStringLiteral("svg"), QStringLiteral("xpm")};

// index.theme is an ini file, but values with commas would be lists to QSettings: read it
// by hand (only [Icon Theme] and the directory sections are needed).
QHash<QString, QHash<QString, QString>> readIndex(const QString &path)
{
    QHash<QString, QHash<QString, QString>> sections;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return sections;
    QString section;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (eq > 0)
            sections[section].insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return sections;
}

}  // namespace

bool IconTheme::Dir::matches(int s) const
{
    switch (type) {
    case Fixed: return s == size;
    case Scalable: return s >= minSize && s <= maxSize;
    case Threshold: return s >= size - threshold && s <= size + threshold;
    }
    return false;
}

int IconTheme::Dir::distance(int s) const
{
    switch (type) {
    case Fixed: return qAbs(size - s);
    case Scalable: return s < minSize ? minSize - s : s > maxSize ? s - maxSize : 0;
    case Threshold:
        return s < size - threshold ? size - threshold - s : s > size + threshold ? s - size - threshold : 0;
    }
    return INT_MAX;
}

IconTheme::IconTheme(const QString &theme, QStringList baseDirs, QStringList pixmapDirs)
    : m_theme(theme)
    , m_bases(std::move(baseDirs))
    , m_pixmaps(std::move(pixmapDirs))
{
    if (m_bases.isEmpty()) {
        m_bases << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/icons");
        for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
            m_bases << d + QStringLiteral("/icons");
        m_bases.removeDuplicates();
    }
    if (m_pixmaps.isEmpty())
        m_pixmaps << QStringLiteral("/usr/share/pixmaps");
}

IconTheme::Theme &IconTheme::theme(const QString &name)
{
    Theme &t = m_themes[name];
    if (t.loaded)
        return t;
    t.loaded = true;
    QHash<QString, QHash<QString, QString>> index;
    for (const QString &base : std::as_const(m_bases)) {
        const QString file = base + u'/' + name + QStringLiteral("/index.theme");
        if (QFile::exists(file)) {
            index = readIndex(file);
            break;
        }
    }
    const QHash<QString, QString> head = index.value(QStringLiteral("Icon Theme"));
    t.inherits = head.value(QStringLiteral("Inherits")).split(u',', Qt::SkipEmptyParts);
    QStringList dirNames = head.value(QStringLiteral("Directories")).split(u',', Qt::SkipEmptyParts)
        + head.value(QStringLiteral("ScaledDirectories")).split(u',', Qt::SkipEmptyParts);
    dirNames.removeDuplicates();
    for (const QString &dirName : std::as_const(dirNames)) {
        const QHash<QString, QString> s = index.value(dirName);
        Dir d;
        d.size = s.value(QStringLiteral("Size")).toInt();
        if (d.size <= 0)
            continue;
        d.scale = qMax(1, s.value(QStringLiteral("Scale"), QStringLiteral("1")).toInt());
        d.minSize = s.value(QStringLiteral("MinSize"), QString::number(d.size)).toInt();
        d.maxSize = s.value(QStringLiteral("MaxSize"), QString::number(d.size)).toInt();
        d.threshold = s.value(QStringLiteral("Threshold"), QStringLiteral("2")).toInt();
        const QString type = s.value(QStringLiteral("Type"), QStringLiteral("Threshold"));
        d.type = type == u"Fixed" ? Dir::Fixed : type == u"Scalable" ? Dir::Scalable : Dir::Threshold;
        // Compare in device pixels: a 48x48@2 directory holds 96 pixel icons.
        d.size *= d.scale;
        d.minSize *= d.scale;
        d.maxSize *= d.scale;
        const int dirIndex = int(t.dirs.size());
        t.dirs << d;
        // List each directory once; lookups are then hash hits instead of stat calls.
        for (const QString &base : std::as_const(m_bases)) {
            const QDir dir(base + u'/' + name + u'/' + dirName);
            for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
                if (Extensions.contains(fi.suffix()))
                    t.files[fi.completeBaseName()].append({dirIndex, fi.absoluteFilePath()});
            }
        }
    }
    return t;
}

QStringList IconTheme::chain()
{
    if (!m_chain.isEmpty())
        return m_chain;
    // Breadth-first through Inherits, then hicolor (always the last resort).
    QStringList queue{m_theme};
    while (!queue.isEmpty()) {
        const QString name = queue.takeFirst();
        if (m_chain.contains(name))
            continue;
        m_chain << name;
        queue << theme(name).inherits;
    }
    m_chain.removeAll(QStringLiteral("hicolor"));
    m_chain << QStringLiteral("hicolor");
    return m_chain;
}

QString IconTheme::lookup(const QString &name, int size)
{
    if (name.isEmpty())
        return {};
    if (QFileInfo(name).isAbsolute())
        return QFile::exists(name) ? name : QString();
    QMutexLocker lock(&m_mutex);
    const QString key = name + u'@' + QString::number(size);
    if (const auto it = m_cache.constFind(key); it != m_cache.cend())
        return *it;

    QString found;
    for (const QString &themeName : chain()) {
        const Theme &t = theme(themeName);
        const auto files = t.files.value(name);
        int best = INT_MAX;
        for (const auto &[dirIndex, file] : files) {
            const Dir &d = t.dirs.at(dirIndex);
            const int distance = d.matches(size) ? 0 : d.distance(size);
            // Prefer an exact match, then the closest size; scalable wins a tie.
            if (distance < best || (distance == best && file.endsWith(QStringLiteral(".svg")))) {
                best = distance;
                found = file;
            }
        }
        if (!found.isEmpty())
            break;
    }
    if (found.isEmpty()) {
        for (const QString &dir : std::as_const(m_pixmaps)) {
            for (const QString &ext : Extensions) {
                const QString file = dir + u'/' + name + u'.' + ext;
                if (QFile::exists(file)) {
                    found = file;
                    break;
                }
            }
            if (!found.isEmpty())
                break;
        }
    }
    m_cache.insert(key, found);
    return found;
}
