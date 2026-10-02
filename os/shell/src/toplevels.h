// The open app windows, through wlr-foreign-toplevel-management: list them, switch to
// one, close one. Works on any compositor with the protocol (Hyprland, Phoc, Sway), so the
// shell has no compositor-specific code for this (ADR 0004).
#pragma once

#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtension>

class Toplevel : public QObject, public QtWayland::zwlr_foreign_toplevel_handle_v1
{
    Q_OBJECT

public:
    explicit Toplevel(struct ::zwlr_foreign_toplevel_handle_v1 *handle);
    ~Toplevel() override;

    QString title() const { return m_title; }
    QString appId() const { return m_appId; }
    bool activated() const { return m_activated; }

signals:
    void changed();  // after the first: the window was announced
    void closed();

protected:
    void zwlr_foreign_toplevel_handle_v1_title(const QString &title) override { m_pending.title = title; }
    void zwlr_foreign_toplevel_handle_v1_app_id(const QString &appId) override { m_pending.appId = appId; }
    void zwlr_foreign_toplevel_handle_v1_state(wl_array *state) override;
    void zwlr_foreign_toplevel_handle_v1_done() override;
    void zwlr_foreign_toplevel_handle_v1_closed() override;

private:
    struct {
        QString title, appId;
        bool activated = false;
    } m_pending;
    QString m_title, m_appId;
    bool m_activated = false;
    bool m_closed = false;
};

class ToplevelManager : public QWaylandClientExtensionTemplate<ToplevelManager>,
                        public QtWayland::zwlr_foreign_toplevel_manager_v1
{
    Q_OBJECT

public:
    ToplevelManager();
    void bind() { initialize(); }  // after the signals are connected

signals:
    void toplevelAdded(Toplevel *toplevel);

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(struct ::zwlr_foreign_toplevel_handle_v1 *handle) override;
};

class ToplevelModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)

public:
    enum Role { TitleRole = Qt::UserRole + 1, AppIdRole, ActivatedRole };

    explicit ToplevelModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }
    bool supported() const;

    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void close(int row);
    // Switch to one of the shell's own windows (e.g. home), by title.
    Q_INVOKABLE bool activateOwn(const QString &title);

signals:
    void countChanged();
    void supportedChanged();

private:
    void announced(Toplevel *t);
    void updated(Toplevel *t);
    void removed(Toplevel *t);
    void activateToplevel(Toplevel *t);
    void showRecentIfNothingActive();
    bool isOwn(const Toplevel *t) const;

    ToplevelManager *m_manager;
    QList<Toplevel *> m_rows;    // other apps' windows, most recently used first
    QList<Toplevel *> m_recent;  // all windows, the shell's included, most recently used first
};
