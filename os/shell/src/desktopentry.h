// Desktop entries (freedesktop Desktop Entry Specification 1.5): the parts a launcher
// needs, i.e. reading [Desktop Entry] and turning Exec into an argument list.
#pragma once

#include <QString>
#include <QStringList>

struct DesktopEntry {
    QString id;        // desktop file ID, e.g. "org.gnome.Epiphany.desktop"
    QString path;      // where it was read from
    QString name;      // localized Name
    QString icon;      // icon name or absolute path
    QString exec;      // Exec, string escapes already resolved
    QString workingDir;
    QString wmClass;   // StartupWMClass
    bool terminal = false;
    // An entry that is hidden still shadows entries with the same ID further down the
    // search path (that is how NoDisplay/Hidden overrides in /usr/local or ~ work).
    bool visible = false;
};

namespace desktopentry {

// `languages`: most specific first, e.g. {"nb_NO", "nb"}. `desktops`: XDG_CURRENT_DESKTOP.
DesktopEntry parse(const QString &id, const QString &path, const QByteArray &data,
                   const QStringList &languages, const QStringList &desktops);

// Exec -> argv, with field codes expanded (files and URLs: none). Empty on a syntax error.
QStringList execArgs(const DesktopEntry &entry);

// "nb_NO.UTF-8@x" -> {"nb_NO@x", "nb_NO", "nb@x", "nb"}, as the spec's matching rules.
QStringList languagesFor(const QString &locale);

}  // namespace desktopentry
