#include "appstore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

AppStore::AppStore(QObject *parent)
    : QObject(parent)
{
    connect(&m_list, &QProcess::stateChanged, this, &AppStore::loadingChanged);
    connect(&m_list, &QProcess::finished, this, [this](int code) {
        const QJsonObject reply = QJsonDocument::fromJson(m_list.readAllStandardOutput()).object();
        if (code != 0 || reply.contains(QStringLiteral("error"))) {
            emit failed(QString(), reply.value(QStringLiteral("error")).toString(tr("The store could not be read")));
            return;
        }
        m_apps = reply.value(QStringLiteral("apps")).toArray().toVariantList();
        // The categories that have apps, in the order the catalog lists them.
        const QJsonObject names = reply.value(QStringLiteral("categories")).toObject();
        m_categories.clear();
        QStringList seen;
        for (const QVariant &v : std::as_const(m_apps)) {
            const QString c = v.toMap().value(QStringLiteral("category")).toString();
            if (seen.contains(c))
                continue;
            seen << c;
            m_categories << QVariantMap{{QStringLiteral("id"), c},
                                        {QStringLiteral("name"), names.value(c).toString(c)}};
        }
        emit changed();
    });
    connect(&m_job, &QProcess::finished, this, [this](int code) {
        const QJsonObject reply = QJsonDocument::fromJson(m_job.readAllStandardOutput().trimmed()).object();
        const QString id = m_running;
        m_running.clear();
        if (code != 0 || reply.contains(QStringLiteral("error")))
            emit failed(nameOf(id), reply.value(QStringLiteral("error")).toString(tr("Something went wrong")));
        else if (reply.value(QStringLiteral("installed")).toBool())
            emit installed(id);
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

QVariantList AppStore::apps() const
{
    QVariantList out;
    for (const QVariant &v : m_apps) {
        QVariantMap a = v.toMap();
        const QString id = a.value(QStringLiteral("id")).toString();
        const bool queued = std::any_of(m_queue.cbegin(), m_queue.cend(), [&](const Job &j) { return j.id == id; });
        a[QStringLiteral("busy")] = id == m_running || queued;
        out << a;
    }
    return out;
}

QString AppStore::nameOf(const QString &id) const
{
    for (const QVariant &v : m_apps) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id)
            return v.toMap().value(QStringLiteral("name")).toString();
    }
    return id;
}

void AppStore::refresh()
{
    if (m_list.state() == QProcess::NotRunning)
        m_list.start(program(), {QStringLiteral("list"), QStringLiteral("--json")});
}

void AppStore::install(const QString &id)
{
    m_queue.enqueue({id, true});
    emit changed();
    next();
}

void AppStore::remove(const QString &id)
{
    m_queue.enqueue({id, false});
    emit changed();
    next();
}

void AppStore::next()
{
    if (m_job.state() != QProcess::NotRunning || m_queue.isEmpty())
        return;
    const Job job = m_queue.dequeue();
    m_running = job.id;
    m_job.start(program(), {job.install ? QStringLiteral("install") : QStringLiteral("remove"), job.id});
    emit changed();
}
