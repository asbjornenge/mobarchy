#include "appfilter.h"

#include "apps.h"

#include <QRegularExpression>

AppFilter::AppFilter(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &AppFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &AppFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &AppFilter::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &AppFilter::countChanged);
}

void AppFilter::setSource(QAbstractItemModel *source)
{
    if (source == sourceModel())
        return;
    setSourceModel(source);
    sort(0);
    emit sourceChanged();
}

void AppFilter::setQuery(const QString &query)
{
    const QString q = query.trimmed();
    if (q == m_query)
        return;
    m_query = q;
    invalidate();
    emit queryChanged();
    emit countChanged();
}

bool AppFilter::matches(const QString &name, const QString &query)
{
    return query.isEmpty() || name.contains(query, Qt::CaseInsensitive);
}

bool AppFilter::wordStart(const QString &name, const QString &query)
{
    static const QRegularExpression words(QStringLiteral("[\\s\\-_.]+"));
    for (const QString &w : name.split(words, Qt::SkipEmptyParts))
        if (w.startsWith(query, Qt::CaseInsensitive))
            return true;
    return false;
}

bool AppFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QString name = sourceModel()->index(row, 0, parent).data(AppModel::NameRole).toString();
    return matches(name, m_query);
}

bool AppFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (!m_query.isEmpty()) {
        const bool l = wordStart(left.data(AppModel::NameRole).toString(), m_query);
        const bool r = wordStart(right.data(AppModel::NameRole).toString(), m_query);
        if (l != r)
            return l;
    }
    return left.row() < right.row();
}

QString AppFilter::first() const
{
    return rowCount() > 0 ? index(0, 0).data(AppModel::IdRole).toString() : QString();
}
