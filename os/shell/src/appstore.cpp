#include "appstore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

AppStore::AppStore(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_list, &QProcess::stateChanged, this, &AppStore::loadingChanged);
    connect(&m_list, &QProcess::finished, this, [this](int code) {
        const QJsonObject reply = QJsonDocument::fromJson(m_list.readAllStandardOutput()).object();
        if (code != 0 || reply.contains(QStringLiteral("error"))) {
            emit failed(QString(), reply.value(QStringLiteral("error")).toString(tr("The store could not be read")));
            return;
        }
        load(reply.value(QStringLiteral("apps")).toArray().toVariantList());
        // The categories that have apps, in the order the catalog lists them.
        const QJsonObject names = reply.value(QStringLiteral("categories")).toObject();
        QVariantList categories;
        QStringList seen;
        for (const QVariantMap &a : std::as_const(m_apps)) {
            const QString c = a.value(QStringLiteral("category")).toString();
            if (seen.contains(c))
                continue;
            seen << c;
            categories << QVariantMap{{QStringLiteral("id"), c}, {QStringLiteral("name"), names.value(c).toString(c)}};
        }
        if (categories != m_categories) {
            m_categories = categories;
            emit categoriesChanged();
        }
    });
    connect(&m_job, &QProcess::finished, this, [this](int code) {
        const QJsonObject reply = QJsonDocument::fromJson(m_job.readAllStandardOutput().trimmed()).object();
        const QString id = m_running;
        m_running.clear();
        if (code != 0 || reply.contains(QStringLiteral("error"))) {
            const int row = rowOf(id);
            emit failed(row >= 0 ? m_apps[row].value(QStringLiteral("name")).toString() : id,
                        reply.value(QStringLiteral("error")).toString(tr("Something went wrong")));
        } else if (reply.value(QStringLiteral("installed")).toBool()) {
            emit installed(id);
        }
        busyChanged(id);
        refresh();
        next();
    });
    refresh();
}

QString AppStore::program() const
{
    return qEnvironmentVariable("MOBARCHY_STORE_BIN",
                                QStringLiteral("/opt/mobarchy/agent/mobarchy-mcp/.venv/bin/mobarchy-store"));
}

void AppStore::load(const QVariantList &apps)
{
    QList<QVariantMap> fresh;
    for (const QVariant &v : apps)
        fresh << v.toMap();
    // The same apps in the same order (the usual case: a status changed): update rows in
    // place. Otherwise the catalog itself changed: start over.
    const bool sameRows = fresh.size() == m_apps.size()
        && std::equal(fresh.cbegin(), fresh.cend(), m_apps.cbegin(), [](const QVariantMap &a, const QVariantMap &b) {
               return a.value(QStringLiteral("id")) == b.value(QStringLiteral("id"));
           });
    if (!sameRows) {
        beginResetModel();
        m_apps = fresh;
        endResetModel();
        return;
    }
    for (int i = 0; i < fresh.size(); ++i) {
        if (fresh[i] == m_apps[i])
            continue;
        m_apps[i] = fresh[i];
        emit dataChanged(index(i), index(i));
        emit appChanged(fresh[i].value(QStringLiteral("id")).toString());
    }
}

int AppStore::rowOf(const QString &id) const
{
    for (int i = 0; i < m_apps.size(); ++i) {
        if (m_apps[i].value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}

bool AppStore::busy(const QString &id) const
{
    return id == m_running
        || std::any_of(m_queue.cbegin(), m_queue.cend(), [&](const Job &j) { return j.id == id; });
}

void AppStore::busyChanged(const QString &id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    emit dataChanged(index(row), index(row), {BusyRole});
    emit appChanged(id);
}

int AppStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_apps.size());
}

QVariant AppStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_apps.size())
        return {};
    const QVariantMap &a = m_apps[index.row()];
    switch (role) {
    case IdRole: return a.value(QStringLiteral("id"));
    case KindRole: return a.value(QStringLiteral("kind"));
    case NameRole: return a.value(QStringLiteral("name"));
    case SummaryRole: return a.value(QStringLiteral("summary"));
    case IconRole: return a.value(QStringLiteral("icon"));
    case CategoryRole: return a.value(QStringLiteral("category"));
    case InstalledRole: return a.value(QStringLiteral("installed"));
    case BusyRole: return busy(a.value(QStringLiteral("id")).toString());
    case AppIdRole: return a.value(QStringLiteral("app_id"));
    case SearchTextRole:
        return QString(a.value(QStringLiteral("name")).toString() + u' ' + a.value(QStringLiteral("summary")).toString()
                       + u' ' + a.value(QStringLiteral("description")).toString()).toLower();
    }
    return {};
}

QHash<int, QByteArray> AppStore::roleNames() const
{
    return {{IdRole, "appId"}, {KindRole, "kind"}, {NameRole, "name"}, {SummaryRole, "summary"},
            {IconRole, "icon"}, {CategoryRole, "category"}, {InstalledRole, "installed"},
            {BusyRole, "busy"}, {AppIdRole, "desktopId"}};
}

QVariantMap AppStore::get(const QString &id) const
{
    const int row = rowOf(id);
    if (row < 0)
        return {};
    QVariantMap a = m_apps[row];
    a[QStringLiteral("busy")] = busy(id);
    return a;
}

void AppStore::refresh()
{
    if (m_list.state() == QProcess::NotRunning)
        m_list.start(program(), {QStringLiteral("list"), QStringLiteral("--json")});
}

void AppStore::install(const QString &id)
{
    m_queue.enqueue({id, true});
    busyChanged(id);
    next();
}

void AppStore::remove(const QString &id)
{
    m_queue.enqueue({id, false});
    busyChanged(id);
    next();
}

void AppStore::next()
{
    if (m_job.state() != QProcess::NotRunning || m_queue.isEmpty())
        return;
    const Job job = m_queue.dequeue();
    m_running = job.id;
    m_job.start(program(), {job.install ? QStringLiteral("install") : QStringLiteral("remove"), job.id});
}

// --- AppStoreFilter ----------------------------------------------------------------------

void AppStoreFilter::setStore(AppStore *store)
{
    if (store == m_store)
        return;
    m_store = store;
    setSourceModel(store);
    emit storeChanged();
}

void AppStoreFilter::setCategory(const QString &category)
{
    if (category == m_category)
        return;
    beginFilterChange();
    m_category = category;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit categoryChanged();
}

void AppStoreFilter::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    beginFilterChange();
    m_query = query;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit queryChanged();
}

bool AppStoreFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (!m_category.isEmpty() && i.data(AppStore::CategoryRole).toString() != m_category)
        return false;
    const QString text = i.data(AppStore::SearchTextRole).toString();
    const QStringList words = m_query.toLower().split(u' ', Qt::SkipEmptyParts);
    return std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return text.contains(w); });
}
