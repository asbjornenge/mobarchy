// The installed apps (desktop entries) and how to start them.
#pragma once

#include "desktopentry.h"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class AppModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, IconRole };

    explicit AppModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Starts the app in its own systemd scope, so it outlives a restart of the shell.
    Q_INVOKABLE bool launch(const QString &id);
    // The icon for a window's app_id (matched against desktop IDs and StartupWMClass).
    Q_INVOKABLE QString iconForAppId(const QString &appId) const;
    // The app IDs an app's windows can have: its desktop ID (without .desktop) and its
    // StartupWMClass.
    Q_INVOKABLE QStringList windowIds(const QString &appId) const;
    Q_INVOKABLE QString nameForAppId(const QString &appId) const;
    // A web app put on the home screen (Mobarchy's browser's or Chromium's "Add to home
    // screen", the agent's add_web_app): one the owner can remove.
    Q_INVOKABLE bool isWebApp(const QString &appId) const;
    // Removes it: its entry, its icon, and (Mobarchy's browser's) its own profile.
    Q_INVOKABLE bool removeWebApp(const QString &appId);

private:
    void reload();
    const DesktopEntry *forAppId(const QString &appId) const;

    QList<DesktopEntry> m_apps;     // visible ones, sorted by name
    QList<DesktopEntry> m_all;      // including hidden ones, for window -> app lookups
    QFileSystemWatcher m_watcher;
    QTimer m_reload;                // installs touch many files: reload once they settle
};
