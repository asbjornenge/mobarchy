// Themes (ADR 0025): colours, font and wallpaper, from the theme files, followed live.
//   /usr/share/mobarchy/themes/<id>.json       the ones that come with Mobarchy
//   ~/.local/share/mobarchy/themes/<id>.json   the owner's (the agent makes them); win
//   ~/.config/mobarchy/theme                   the active one's id (default: mobarchy)
// A theme file: {"name", "description", "colors": {bg, bgDark, panel, line, fg, muted,
// accent, green, red, magenta}, "font", "wallpaper": a file next to it}. What a theme
// leaves out is Mobarchy's.
// (The browser has a copy of ThemeSource: os/browser/src/themesource.*.)
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// The active theme.
class ThemeSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString id READ id NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    // every colour role, always all of them: {"bg": "#1a1b26", ...}
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QString font READ font NOTIFY changed)
    // the wallpaper's URL, "" without one
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY changed)

public:
    explicit ThemeSource(QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QVariantMap colors() const { return m_colors; }
    QString font() const { return m_font; }
    QString wallpaper() const { return m_wallpaper; }

signals:
    void changed();

private:
    void load();

    QFileSystemWatcher m_watcher;
    QTimer m_settle;    // a theme written as several file events loads once
    QString m_id, m_name, m_font, m_wallpaper;
    QVariantMap m_colors;
};

// All the themes, for the Themes app: switching, and deleting the owner's.
class ThemeLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{id, name, description, colors, font, wallpaper, own, active}], Mobarchy's own first
    Q_PROPERTY(QVariantList themes READ themes NOTIFY changed)
    Q_PROPERTY(QString active READ active NOTIFY changed)

public:
    explicit ThemeLibrary(QObject *parent = nullptr);

    QVariantList themes() const { return m_themes; }
    QString active() const { return m_active; }

    Q_INVOKABLE bool activate(const QString &id);
    // one of the owner's (with its wallpaper); Mobarchy's own stay
    Q_INVOKABLE bool remove(const QString &id);

signals:
    void changed();

private:
    void load();

    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QVariantList m_themes;
    QString m_active;
};
