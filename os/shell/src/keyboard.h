// The on-screen keyboard's engine (ADR 0016): the shell is the session's input method.
//
//  - input-method-unstable-v2: the compositor says when a text field in an app has focus
//    (text-input-v3 on the app's side), what it holds around the cursor and what kind of
//    text it wants (email, number, password, ...). Text goes into the field with
//    commit_string, and deletion with delete_surrounding_text.
//  - virtual-keyboard-unstable-v1: keys that are actions, not text (Return, BackSpace when
//    the field doesn't share its text, arrows, Tab), and all typing when no field speaks
//    text-input (some apps, terminals in X): those get key presses of a Norwegian keymap.
//
// The keyboard itself (Keyboard.qml) is QML; this is its connection to the apps.
#pragma once

#include "qwayland-input-method-unstable-v2.h"
#include "qwayland-virtual-keyboard-unstable-v1.h"

#include <QObject>
#include <QHash>
#include <QPair>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtension>

struct xkb_context;
struct xkb_keymap;

class InputMethodManager : public QWaylandClientExtensionTemplate<InputMethodManager>,
                           public QtWayland::zwp_input_method_manager_v2
{
    Q_OBJECT
public:
    InputMethodManager() : QWaylandClientExtensionTemplate<InputMethodManager>(1) {}
    void bind() { initialize(); }
};

class VirtualKeyboardManager : public QWaylandClientExtensionTemplate<VirtualKeyboardManager>,
                               public QtWayland::zwp_virtual_keyboard_manager_v1
{
    Q_OBJECT
public:
    VirtualKeyboardManager() : QWaylandClientExtensionTemplate<VirtualKeyboardManager>(1) {}
    void bind() { initialize(); }
};

class InputMethod;

class Keyboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Shown: a text field has focus (and the owner didn't hide it), or shown by hand.
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    // A text field in an app has focus (through text-input).
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // What the field wants: "text", "number", "phone", "email", "url", "password", ...
    Q_PROPERTY(QString purpose READ purpose NOTIFY contentChanged)
    // The field asks for a capital letter here (sentence start, auto-capitalization on).
    Q_PROPERTY(bool capitalize READ capitalize NOTIFY contentChanged)
    // The text before the cursor, as far as the field shares it.
    Q_PROPERTY(QString textBefore READ textBefore NOTIFY contentChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)

public:
    explicit Keyboard(QObject *parent = nullptr);
    ~Keyboard() override;

    bool visible() const { return m_visible; }
    void setVisible(bool visible);
    Q_INVOKABLE void toggle() { setVisible(!m_visible); }

    bool active() const { return m_active; }
    QString purpose() const { return m_purpose; }
    bool capitalize() const;
    QString textBefore() const;
    bool supported() const;

    // Type text at the cursor.
    Q_INVOKABLE void type(const QString &text);
    // Delete what is before the cursor: one character (a whole emoji, an accented letter).
    Q_INVOKABLE void backspace();
    // An action key: "Return", "Tab", "Left", "Right", "Up", "Down", "Escape".
    Q_INVOKABLE void key(const QString &name);
    // The emoji panel's groups: [{name, emoji: [...]}] (data/emoji.json, tools/emoji-gen)
    Q_INVOKABLE QVariantList emojiGroups() const;

signals:
    void visibleChanged();
    void activeChanged();
    void contentChanged();
    void supportedChanged();
    // A key was pressed (typed, deleted, moved): an app's selection right after is its own
    // doing (Chromium deletes by selecting first), not something to offer for copying.
    void edited();

private:
    friend class InputMethod;
    void setup();
    void onDone();
    bool ensureVirtualKeyboard();
    void pressKeysym(uint32_t keysym);
    void sendKeycode(uint32_t keycode, uint32_t mods);

    InputMethodManager *m_imManager;
    VirtualKeyboardManager *m_vkManager;
    InputMethod *m_im = nullptr;
    QtWayland::zwp_virtual_keyboard_v1 *m_vk = nullptr;

    // input method state, applied on done
    bool m_active = false;
    bool m_activeChanged = false;
    bool m_visible = false;
    bool m_hiddenByHand = false;   // the owner hid it while a field had focus
    bool m_manual = false;         // the owner showed it with no field focused
    QString m_surrounding;
    int m_cursor = 0;              // in bytes of UTF-8, as the protocol counts
    // What the field held before our latest edits (text, cursor). An app may report its
    // text a step late (Chromium tells the state before the edit it was just given): such
    // a report is not taken over our own view of the field.
    QList<QPair<QString, int>> m_before;
    void noteEdit();
    uint32_t m_hint = 0;
    uint32_t m_purposeValue = 0;
    QString m_purpose = QStringLiteral("text");
    uint32_t m_serial = 0;         // done events so far: commits refer to them

    // the virtual keyboard's keymap: keysym -> (evdev keycode, shift)
    xkb_context *m_xkb = nullptr;
    xkb_keymap *m_keymap = nullptr;
    QHash<uint32_t, QPair<uint32_t, bool>> m_keys;
    uint32_t m_shiftMask = 0;
};

class InputMethod : public QtWayland::zwp_input_method_v2
{
public:
    InputMethod(struct ::zwp_input_method_v2 *object, Keyboard *keyboard)
        : QtWayland::zwp_input_method_v2(object), m_keyboard(keyboard) {}
    ~InputMethod() override { destroy(); }

protected:
    void zwp_input_method_v2_activate() override;
    void zwp_input_method_v2_deactivate() override;
    void zwp_input_method_v2_surrounding_text(const QString &text, uint32_t cursor, uint32_t anchor) override;
    void zwp_input_method_v2_content_type(uint32_t hint, uint32_t purpose) override;
    void zwp_input_method_v2_done() override;
    void zwp_input_method_v2_unavailable() override;

private:
    Keyboard *m_keyboard;
    // pending (double-buffered until done)
    bool m_pendingActive = false;
    QString m_pendingText;
    uint32_t m_pendingCursor = 0;
    uint32_t m_pendingHint = 0;
    uint32_t m_pendingPurpose = 0;
};
