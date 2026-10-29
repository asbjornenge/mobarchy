// The content of the owner's own home widgets (ADR 0046), as mobarchy-widgets last fetched
// it: ~/.local/state/mobarchy/widgets/<id>.json, each followed live.
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QFont>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class WidgetContents : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // id -> {items | value, caption | text | image, updated, error}
    Q_PROPERTY(QVariantMap contents READ contents NOTIFY changed)

public:
    explicit WidgetContents(QObject *parent = nullptr);

    QVariantMap contents() const { return m_contents; }

    // A widget's markdown as rich text in the given font, its links in the given colour
    // (Text's own markdown gives them the palette's blue, whatever its linkColor)
    Q_INVOKABLE QString markdown(const QString &text, const QFont &font, const QColor &link) const;

signals:
    void changed();

private:
    void load();

    QString m_dir;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;    // several files written at once load once
    QVariantMap m_contents;
};
