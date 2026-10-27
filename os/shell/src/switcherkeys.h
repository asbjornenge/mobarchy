// ⌘+Tab from a hardware keyboard: Hyprland's binds (os/config/hypr/hyprland.conf) call these
// on D-Bus (org.mobarchy.Shell /org/mobarchy/Switcher, org.mobarchy.Switcher), and the
// switcher (AppSwitcher.qml) follows: Next and Previous choose an open app. ⌘ let go goes to
// it: the switcher has the keyboard while it is chosen from, and sees that itself (a release
// bind in Hyprland only fires when no other key came between). Commit and Cancel do the same
// from D-Bus.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class SwitcherKeys : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit SwitcherKeys(QObject *parent = nullptr);
    // ⌘ (Super) still held, as the compositor last told the window with the keyboard
    Q_INVOKABLE bool superHeld() const;

public slots:
    Q_SCRIPTABLE void Next() { emit next(); }
    Q_SCRIPTABLE void Previous() { emit previous(); }
    Q_SCRIPTABLE void Commit() { emit commit(); }
    Q_SCRIPTABLE void Cancel() { emit cancel(); }

signals:
    void next();
    void previous();
    void commit();
    void cancel();
};

// ⌘+← and ⌘+→ from a hardware keyboard (Hyprland's binds, on D-Bus: org.mobarchy.Shell
// /org/mobarchy/Home, org.mobarchy.Home): the notifications to the left of what is open (the
// app in front, or home's widgets), the apps to the right (Main.qml's side()). And ⌘+↓:
// home, its widgets (Home).
class HomeKeys : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit HomeKeys(QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE void Left() { emit left(); }
    Q_SCRIPTABLE void Right() { emit right(); }
    Q_SCRIPTABLE void Home() { emit home(); }

signals:
    void left();
    void right();
    void home();
};
