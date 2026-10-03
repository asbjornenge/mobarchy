// The app store's data and actions (ADR 0012), through mobarchy-store: the curated
// catalog with what is installed, installing and removing (one at a time: the package
// manager takes one transaction at a time).
//
// A list model whose rows change in place: a status change (installing, installed) updates
// its row, so a list showing the store keeps its scroll position.
#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <QQueue>
#include <QSortFilterProxyModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppStore : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    // [{id, name}] of the categories that have apps, in catalog order
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, KindRole, NameRole, SummaryRole, IconRole, CategoryRole,
                InstalledRole, BusyRole, AppIdRole, SearchTextRole };

    explicit AppStore(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList categories() const { return m_categories; }
    bool loading() const { return m_list.state() != QProcess::NotRunning; }

    // Everything about one app (for its page): description, screenshots, note, ...
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void remove(const QString &id);

signals:
    void categoriesChanged();
    void loadingChanged();
    // An app changed (installed, busy): pages showing it re-read it.
    void appChanged(const QString &id);
    void failed(const QString &name, const QString &reason);
    void installed(const QString &id);

private:
    struct Job {
        QString id;
        bool install;
    };
    void load(const QVariantList &apps);
    void next();
    bool busy(const QString &id) const;
    void busyChanged(const QString &id);
    int rowOf(const QString &id) const;
    QString program() const;

    QProcess m_list;
    QProcess m_job;
    QQueue<Job> m_queue;
    QString m_running;  // the app whose job runs
    QList<QVariantMap> m_apps;
    QVariantList m_categories;
};

// The store filtered by category and search words, for the list.
class AppStoreFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AppStore *store READ store WRITE setStore NOTIFY storeChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)  // "": all
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    AppStore *store() const { return m_store; }
    void setStore(AppStore *store);
    QString category() const { return m_category; }
    void setCategory(const QString &category);
    QString query() const { return m_query; }
    void setQuery(const QString &query);

signals:
    void storeChanged();
    void categoryChanged();
    void queryChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    AppStore *m_store = nullptr;
    QString m_category;
    QString m_query;
};
