// Icon lookup by the freedesktop Icon Theme Specification: theme, the themes it inherits,
// hicolor, then /usr/share/pixmaps. Thread-safe and without QIcon, so image providers can
// use it from Qt Quick's loader threads.
#pragma once

#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>

class IconTheme
{
public:
    // baseDirs: where themes live, most important first (default: $XDG_DATA_HOME/icons,
    // $XDG_DATA_DIRS/icons). pixmapDirs: the last fallback (default: /usr/share/pixmaps).
    explicit IconTheme(const QString &theme, QStringList baseDirs = {}, QStringList pixmapDirs = {});

    // The file for an icon name at (about) this size in pixels, or an empty string.
    QString lookup(const QString &name, int size);

private:
    struct Dir {
        int size = 0, minSize = 0, maxSize = 0, threshold = 2, scale = 1;  // sizes in pixels
        enum { Fixed, Scalable, Threshold } type = Threshold;
        bool matches(int s) const;
        int distance(int s) const;
    };
    struct Theme {
        QList<Dir> dirs;
        QStringList inherits;
        // icon name -> (index into dirs, file)
        QHash<QString, QList<QPair<int, QString>>> files;
        bool loaded = false;
    };
    Theme &theme(const QString &name);
    QStringList chain();

    QMutex m_mutex;
    QString m_theme;
    QStringList m_bases;
    QStringList m_pixmaps;
    QHash<QString, Theme> m_themes;
    QHash<QString, QString> m_cache;  // "name@size" -> file
    QStringList m_chain;
};
