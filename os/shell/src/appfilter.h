// The app drawer's search: the apps whose name has the query in it (any case), the ones
// where a word of the name starts with it first ("cam" finds Camera before "Webcam"), else
// in the apps' own order.
#pragma once

#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>

class AppFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel *source READ sourceModel WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit AppFilter(QObject *parent = nullptr);

    void setSource(QAbstractItemModel *source);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int count() const { return rowCount(); }
    // the first match's app ID ("" if none): Return opens it
    Q_INVOKABLE QString first() const;

    static bool matches(const QString &name, const QString &query);
    static bool wordStart(const QString &name, const QString &query);

signals:
    void sourceChanged();
    void queryChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_query;
};
