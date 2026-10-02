// The on-screen keyboard (squeekboard, ADR 0006) over its D-Bus interface sm.puri.OSK0.
// It shows itself when a text field gets focus; this is for showing and hiding it by hand.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Keyboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)

public:
    explicit Keyboard(QObject *parent = nullptr);

    bool visible() const { return m_visible; }
    void setVisible(bool visible);
    Q_INVOKABLE void toggle() { setVisible(!m_visible); }

signals:
    void visibleChanged();

private slots:
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated);

private:
    bool m_visible = false;
};
