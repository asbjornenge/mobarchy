#include "toplevels.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QtGui/qguiapplication_platform.h>

Q_LOGGING_CATEGORY(lcToplevels, "mobarchy.toplevels", QtWarningMsg)

// --- Toplevel ----------------------------------------------------------------------------

Toplevel::Toplevel(struct ::zwlr_foreign_toplevel_handle_v1 *handle)
    : QtWayland::zwlr_foreign_toplevel_handle_v1(handle)
{
}

Toplevel::~Toplevel()
{
    if (isInitialized())
        destroy();
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_state(wl_array *state)
{
    m_pending.activated = false;
    const auto *values = static_cast<const uint32_t *>(state->data);
    for (size_t i = 0; i < state->size / sizeof(uint32_t); ++i) {
        if (values[i] == state_activated)
            m_pending.activated = true;
    }
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_done()
{
    // Hyprland sends one more done after closed; the window is gone by then (and the
    // model has let go of it), so it must not come back.
    if (m_closed)
        return;
    m_title = m_pending.title;
    m_appId = m_pending.appId;
    m_activated = m_pending.activated;
    emit changed();
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_closed()
{
    if (m_closed)
        return;
    m_closed = true;
    emit closed();
}

// --- ToplevelManager ---------------------------------------------------------------------

ToplevelManager::ToplevelManager()
    : QWaylandClientExtensionTemplate<ToplevelManager>(3)
{
}

void ToplevelManager::zwlr_foreign_toplevel_manager_v1_toplevel(struct ::zwlr_foreign_toplevel_handle_v1 *handle)
{
    emit toplevelAdded(new Toplevel(handle));
}

// --- ToplevelModel -----------------------------------------------------------------------

ToplevelModel::ToplevelModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(new ToplevelManager)
{
    m_manager->setParent(this);
    connect(m_manager, &ToplevelManager::activeChanged, this, &ToplevelModel::supportedChanged);
    connect(m_manager, &ToplevelManager::toplevelAdded, this, [this](Toplevel *t) {
        t->setParent(this);
        // Rows appear on the first done event, when the title and app_id are known.
        connect(t, &Toplevel::changed, this, [this, t] {
            m_recent.contains(t) ? updated(t) : announced(t);
        });
        connect(t, &Toplevel::closed, this, [this, t] { removed(t); });
    });
    m_manager->bind();
}

bool ToplevelModel::supported() const
{
    return m_manager->isActive();
}

bool ToplevelModel::isOwn(const Toplevel *t) const
{
    return t->appId() == QGuiApplication::desktopFileName();
}

void ToplevelModel::announced(Toplevel *t)
{
    qCDebug(lcToplevels) << "announced" << t << t->appId() << t->title() << t->activated();
    // A new window: treat it as the most recent one (it normally gets focus right away).
    m_recent.prepend(t);
    if (isOwn(t))
        return;
    beginInsertRows({}, 0, 0);
    m_rows.prepend(t);
    endInsertRows();
    emit countChanged();
}

void ToplevelModel::updated(Toplevel *t)
{
    qCDebug(lcToplevels) << "updated" << t << t->appId() << t->title() << t->activated();
    if (t->activated() && m_recent.first() != t) {
        m_recent.removeOne(t);
        m_recent.prepend(t);
    }
    const int row = int(m_rows.indexOf(t));
    if (row < 0)
        return;
    emit dataChanged(index(row), index(row));
    if (t->activated() && row > 0) {
        beginMoveRows({}, row, row, {}, 0);
        m_rows.move(row, 0);
        endMoveRows();
    }
}

void ToplevelModel::removed(Toplevel *t)
{
    qCDebug(lcToplevels) << "closed" << t << t->appId() << t->activated();
    const bool wasActive = t->activated();
    m_recent.removeOne(t);
    if (const int row = int(m_rows.indexOf(t)); row >= 0) {
        beginRemoveRows({}, row, row);
        m_rows.removeAt(row);
        endRemoveRows();
        emit countChanged();
    }
    t->deleteLater();
    // With one app per screen, closing the active app would leave an empty screen.
    // Give the compositor a moment to focus something itself, then show the most recent.
    if (wasActive)
        QTimer::singleShot(150, this, &ToplevelModel::showRecentIfNothingActive);
}

void ToplevelModel::showRecentIfNothingActive()
{
    if (m_recent.isEmpty())
        return;
    const bool anyActive = std::any_of(m_recent.cbegin(), m_recent.cend(),
                                       [](const Toplevel *t) { return t->activated(); });
    if (!anyActive)
        activateToplevel(m_recent.first());
}

void ToplevelModel::activateToplevel(Toplevel *t)
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (wayland && wayland->seat())
        t->activate(wayland->seat());
}

int ToplevelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ToplevelModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Toplevel *t = m_rows.at(index.row());
    switch (role) {
    case TitleRole: return t->title();
    case AppIdRole: return t->appId();
    case ActivatedRole: return t->activated();
    default: return {};
    }
}

QHash<int, QByteArray> ToplevelModel::roleNames() const
{
    return {{TitleRole, "title"}, {AppIdRole, "appId"}, {ActivatedRole, "activated"}};
}

void ToplevelModel::activate(int row)
{
    if (row >= 0 && row < m_rows.size())
        activateToplevel(m_rows.at(row));
}

void ToplevelModel::close(int row)
{
    qCDebug(lcToplevels) << "close" << row << m_rows.value(row);
    if (row >= 0 && row < m_rows.size())
        m_rows.at(row)->QtWayland::zwlr_foreign_toplevel_handle_v1::close();
}

bool ToplevelModel::activateApp(const QStringList &appIds)
{
    for (Toplevel *t : std::as_const(m_recent)) {
        if (isOwn(t))
            continue;
        for (const QString &id : appIds) {
            if (t->appId().compare(id, Qt::CaseInsensitive) == 0) {
                activateToplevel(t);
                return true;
            }
        }
    }
    return false;
}

bool ToplevelModel::activateOwn(const QString &title)
{
    for (Toplevel *t : std::as_const(m_recent)) {
        if (isOwn(t) && t->title() == title) {
            activateToplevel(t);
            return true;
        }
    }
    return false;
}
