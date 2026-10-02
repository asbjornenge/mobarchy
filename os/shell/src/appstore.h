// The app store's data and actions (ADR 0012), through mobarchy-store: the curated
// catalog with what is installed, installing and removing (one at a time: the package
// manager takes one transaction at a time).
#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{id, kind, name, summary, description, icon, screenshots, category, category_name,
    //   tested, note, installed, app_id, busy}]
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
    // [{id, name}] of the categories that have apps, in catalog order
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit AppStore(QObject *parent = nullptr);

    QVariantList apps() const;
    QVariantList categories() const { return m_categories; }
    bool loading() const { return m_list.state() != QProcess::NotRunning; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void remove(const QString &id);

signals:
    void changed();
    void loadingChanged();
    void failed(const QString &name, const QString &reason);
    void installed(const QString &id);

private:
    struct Job {
        QString id;
        bool install;
    };
    void next();
    QString program() const;
    QString nameOf(const QString &id) const;

    QProcess m_list;
    QProcess m_job;
    QQueue<Job> m_queue;
    QString m_running;  // the app whose job runs
    QVariantList m_apps;
    QVariantList m_categories;
};
