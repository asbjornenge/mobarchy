#include "notifications.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QRegularExpression>
#include <QTextDocumentFragment>

// The D-Bus face of Notifications; method names and signatures are the spec's.
class NotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    explicit NotificationsAdaptor(Notifications *parent)
        : QDBusAbstractAdaptor(parent)
        , m_n(parent)
    {
    }

public slots:
    QStringList GetCapabilities()
    {
        return {QStringLiteral("body"), QStringLiteral("body-markup"), QStringLiteral("actions"),
                QStringLiteral("persistence"), QStringLiteral("icon-static")};
    }
    uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int expire_timeout)
    {
        return m_n->notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout);
    }
    void CloseNotification(uint id) { m_n->closeFromApp(id); }
    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version)
    {
        vendor = QStringLiteral("Mobarchy");
        version = QStringLiteral("0.1");
        spec_version = QStringLiteral("1.2");
        return QStringLiteral("mobarchy-shell");
    }

signals:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);

private:
    Notifications *m_n;
    friend class Notifications;
};

namespace {

// Bodies may carry a little markup (<b>, <i>, <a>, <img>): the shell shows plain text.
QString plain(const QString &text)
{
    if (!text.contains(u'<') && !text.contains(u'&'))
        return text;
    return QTextDocumentFragment::fromHtml(text).toPlainText();
}

}  // namespace

Notifications::Notifications(QObject *parent)
    : QAbstractListModel(parent)
    , m_adaptor(new NotificationsAdaptor(this))
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_registered = bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), this)
        && bus.registerService(QStringLiteral("org.freedesktop.Notifications"));
    if (!m_registered)
        qWarning() << "notifications: could not take org.freedesktop.Notifications (another server?)";
}

uint Notifications::notify(const QString &app, uint replacesId, const QString &icon, const QString &summary,
                           const QString &body, const QStringList &actions, const QVariantMap &hints,
                           int timeout)
{
    Item item;
    item.id = replacesId && rowOf(replacesId) >= 0 ? replacesId : m_nextId++;
    item.app = app;
    item.summary = plain(summary);
    item.body = plain(body);
    item.actions = actions;
    item.urgency = hints.value(QStringLiteral("urgency"), 1).toInt();
    item.resident = hints.value(QStringLiteral("resident")).toBool();
    item.time = QDateTime::currentDateTime();
    // An icon: the app's own, a file path hint, or the icon of its desktop entry.
    item.icon = icon;
    if (item.icon.isEmpty())
        item.icon = hints.value(QStringLiteral("image-path")).toString();
    if (item.icon.startsWith(QStringLiteral("file://")))
        item.icon = item.icon.mid(7);
    if (item.icon.isEmpty())
        item.icon = hints.value(QStringLiteral("desktop-entry")).toString();

    if (const int row = rowOf(item.id); row >= 0) {
        m_items[row] = item;
        emit dataChanged(index(row), index(row));
    } else {
        beginInsertRows({}, 0, 0);
        m_items.prepend(item);
        endInsertRows();
        emit countChanged();
    }

    // -1: the server decides; the shell keeps them until dismissed. 0: never expires.
    if (QTimer *old = m_expiry.take(item.id))
        old->deleteLater();
    if (timeout > 0) {
        auto *t = new QTimer(this);
        t->setSingleShot(true);
        t->setInterval(timeout);
        const uint id = item.id;
        connect(t, &QTimer::timeout, this, [this, id] { remove(id, Expired); });
        t->start();
        m_expiry.insert(item.id, t);
    }
    emit posted(item.id);
    return item.id;
}

void Notifications::closeFromApp(uint id)
{
    remove(id, Closed);
}

void Notifications::dismiss(uint id)
{
    remove(id, Dismissed);
}

void Notifications::invoke(uint id, const QString &action)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    emit m_adaptor->ActionInvoked(id, action);
    if (!m_items.at(row).resident)
        remove(id, Dismissed);
}

void Notifications::clear()
{
    const QList<Item> items = m_items;
    for (const Item &i : items)
        remove(i.id, Dismissed);
}

void Notifications::remove(uint id, CloseReason reason)
{
    const int row = rowOf(id);
    // deleteLater: this may run from that timer's own timeout.
    if (QTimer *t = m_expiry.take(id))
        t->deleteLater();
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    emit m_adaptor->NotificationClosed(id, uint(reason));
}

int Notifications::rowOf(uint id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

QVariantMap Notifications::get(uint id) const
{
    const int row = rowOf(id);
    if (row < 0)
        return {};
    QVariantMap m;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return m;
}

int Notifications::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant Notifications::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Item &i = m_items.at(index.row());
    switch (role) {
    case IdRole: return i.id;
    case AppRole: return i.app;
    case IconRole: return i.icon;
    case SummaryRole: return i.summary;
    case BodyRole: return i.body;
    case ActionsRole: {
        // [{key, label}], without "default" (that one is a tap on the notification)
        QVariantList out;
        for (int k = 0; k + 1 < i.actions.size(); k += 2) {
            if (i.actions.at(k) != u"default")
                out << QVariantMap{{QStringLiteral("key"), i.actions.at(k)},
                                   {QStringLiteral("label"), i.actions.at(k + 1)}};
        }
        return out;
    }
    case UrgencyRole: return i.urgency;
    case TimeRole: return i.time;
    default: return {};
    }
}

QHash<int, QByteArray> Notifications::roleNames() const
{
    return {{IdRole, "notificationId"}, {AppRole, "app"}, {IconRole, "icon"}, {SummaryRole, "summary"},
            {BodyRole, "body"}, {ActionsRole, "actions"}, {UrgencyRole, "urgency"}, {TimeRole, "time"}};
}

#include "notifications.moc"
