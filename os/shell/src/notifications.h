// The notification server (org.freedesktop.Notifications, Desktop Notifications 1.2) on
// the session bus: apps' notifications become banners and a list in quick settings.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class NotificationsAdaptor;

class Notifications : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool registered READ registered CONSTANT)

public:
    enum Role { IdRole = Qt::UserRole + 1, AppRole, IconRole, SummaryRole, BodyRole, ActionsRole,
                UrgencyRole, TimeRole, DesktopEntryRole, TargetRole };
    enum CloseReason { Expired = 1, Dismissed = 2, Closed = 3 };

    explicit Notifications(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_items.size()); }
    bool registered() const { return m_registered; }

    // The user swiped it away / tapped one of its actions ("default" for the body).
    Q_INVOKABLE void dismiss(uint id);
    Q_INVOKABLE void invoke(uint id, const QString &action);
    Q_INVOKABLE void clear();
    // The shell's own notification (no app behind it): invoked() says when it is tapped.
    Q_INVOKABLE uint post(const QString &summary, const QString &body, const QString &icon = {});
    // For banners: the notification with this id, as a map (empty if gone).
    Q_INVOKABLE QVariantMap get(uint id) const;

    // Called by the D-Bus adaptor.
    uint notify(const QString &app, uint replacesId, const QString &icon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int timeout);
    void closeFromApp(uint id);

signals:
    void countChanged();
    // A new or updated notification, for the banner.
    void posted(uint id);
    // A notification's action was invoked (tapped: "default").
    void invoked(uint id, const QString &action);
    // An app's notification tapped: open the app (its desktop entry), at the target if it
    // gave one (the hint "x-mobarchy-target": a conversation, a message)
    void opened(const QString &desktopEntry, const QString &target);

private:
    struct Item {
        uint id = 0;
        QString app, icon, summary, body, desktopEntry, target;
        QStringList actions;  // key, label, key, label, ...
        int urgency = 1;      // 0 low, 1 normal, 2 critical
        bool resident = false;
        QDateTime time;
    };
    void remove(uint id, CloseReason reason);
    int rowOf(uint id) const;

    QList<Item> m_items;  // newest first
    QHash<uint, QTimer *> m_expiry;
    NotificationsAdaptor *m_adaptor = nullptr;
    uint m_nextId = 1;
    bool m_registered = false;
};
