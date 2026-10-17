// The home screen's settings: ~/.config/mobarchy/home.json, followed live (the agent edits it
// too). For now {"weather": {"name", "latitude", "longitude"}}; the board's layout and the
// owner's own widgets come here as well.
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class HomeConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap config READ config NOTIFY changed)

public:
    explicit HomeConfig(QObject *parent = nullptr);

    QVariantMap config() const { return m_config; }

    // one top-level key, written at once (a null value removes it)
    Q_INVOKABLE void set(const QString &key, const QVariant &value);

signals:
    void changed();

private:
    void load();
    void watch();

    QString m_path;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;    // a file written as several events loads once
    QVariantMap m_config;
};
