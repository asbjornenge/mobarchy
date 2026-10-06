// The active theme (ADR 0025): its colours and font, from the theme files, followed live.
//   /usr/share/mobarchy/themes/<id>.json       the ones that come with Mobarchy
//   ~/.local/share/mobarchy/themes/<id>.json   the owner's (the agent makes them); win
//   ~/.config/mobarchy/theme                   the active one's id (default: mobarchy)
// A theme file: {"name", "description", "colors": {bg, bgDark, panel, line, fg, muted,
// accent, green, red, magenta}, "font"}. What a theme leaves out is Mobarchy's.
// (A copy of the shell's: os/shell/src/themesource.*.)
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ThemeSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString id READ id NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    // every colour role, always all of them: {"bg": "#1a1b26", ...}
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QString font READ font NOTIFY changed)

public:
    explicit ThemeSource(QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QVariantMap colors() const { return m_colors; }
    QString font() const { return m_font; }

signals:
    void changed();

private:
    void load();
    void watch();

    QFileSystemWatcher m_watcher;
    QTimer m_settle;    // a theme written as several file events loads once
    QString m_id, m_name, m_font;
    QVariantMap m_colors;
};
