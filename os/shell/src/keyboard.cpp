#include "keyboard.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QLoggingCategory>
#include <QGuiApplication>
#include <QTextBoundaryFinder>
#include <QtGui/qguiapplication_platform.h>

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

// QT_LOGGING_RULES=mobarchy.keyboard.debug=true: what the app tells the keyboard, and back
Q_LOGGING_CATEGORY(lcKeyboard, "mobarchy.keyboard", QtWarningMsg)

namespace {
// text-input-v3's content_hint and content_purpose
enum Hint : uint32_t { AutoCapitalization = 0x4, Lowercase = 0x8, Uppercase = 0x10, Titlecase = 0x20 };
const char *const Purposes[] = {"text", "alpha", "digits", "number", "phone", "url", "email",
                                "name", "password", "pin", "date", "time", "datetime", "terminal"};

uint32_t now() { return uint32_t(QDateTime::currentMSecsSinceEpoch()); }
} // namespace

// --- InputMethod: the protocol's events, double-buffered until done -----------------------

void InputMethod::zwp_input_method_v2_activate()
{
    m_pendingActive = true;
    m_pendingText.clear();
    m_pendingCursor = 0;
    m_pendingHint = 0;
    m_pendingPurpose = 0;
}

void InputMethod::zwp_input_method_v2_deactivate() { m_pendingActive = false; }

void InputMethod::zwp_input_method_v2_surrounding_text(const QString &text, uint32_t cursor, uint32_t)
{
    m_pendingText = text;
    m_pendingCursor = cursor;
}

void InputMethod::zwp_input_method_v2_content_type(uint32_t hint, uint32_t purpose)
{
    m_pendingHint = hint;
    m_pendingPurpose = purpose;
}

void InputMethod::zwp_input_method_v2_done()
{
    qCDebug(lcKeyboard) << "done" << m_keyboard->m_serial + 1 << "active" << m_pendingActive << "text" << m_pendingText
                        << "cursor" << m_pendingCursor;
    m_keyboard->m_serial++;
    const bool activated = m_pendingActive && !m_keyboard->m_active;
    m_keyboard->m_activeChanged = m_pendingActive != m_keyboard->m_active;
    m_keyboard->m_active = m_pendingActive;
    const QPair<QString, int> reported(m_pendingText, int(m_pendingCursor));
    if (activated || !m_keyboard->m_before.contains(reported)) {
        // (the app's own news: a new field, or a change it made; ours caught up)
        m_keyboard->m_before.clear();
        m_keyboard->m_surrounding = m_pendingText;
        m_keyboard->m_cursor = int(m_pendingCursor);
    }
    m_keyboard->m_hint = m_pendingHint;
    m_keyboard->m_purposeValue = m_pendingPurpose;
    if (activated)
        m_keyboard->m_hiddenByHand = false;
    m_keyboard->onDone();
}

void InputMethod::zwp_input_method_v2_unavailable()
{
    // Another input method has the seat (e.g. squeekboard still running).
    qWarning() << "input method unavailable: another one is running";
}

// --- Keyboard ------------------------------------------------------------------------------

Keyboard::Keyboard(QObject *parent)
    : QObject(parent)
    , m_imManager(new InputMethodManager)
    , m_vkManager(new VirtualKeyboardManager)
{
    m_imManager->setParent(this);
    m_vkManager->setParent(this);
    connect(m_imManager, &InputMethodManager::activeChanged, this, &Keyboard::setup);
    connect(m_vkManager, &VirtualKeyboardManager::activeChanged, this, &Keyboard::setup);
    m_imManager->bind();
    m_vkManager->bind();
    // emojiSearch, for Hyprland's Alt+Space (on the shell's name, org.mobarchy.Shell)
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/mobarchy/Keyboard"),
                                                 QStringLiteral("org.mobarchy.Keyboard"), this,
                                                 QDBusConnection::ExportScriptableSlots);
}

Keyboard::~Keyboard()
{
    delete m_grab;
    delete m_im;
    if (m_vk) {
        m_vk->destroy();
        delete m_vk;
    }
    xkb_keymap_unref(m_keymap);
    xkb_context_unref(m_xkb);
}

bool Keyboard::supported() const { return m_im != nullptr; }

void Keyboard::setup()
{
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!app || !app->seat())
        return;
    if (!m_im && m_imManager->isActive()) {
        m_im = new InputMethod(m_imManager->get_input_method(app->seat()), this);
        emit supportedChanged();
    }
}

void Keyboard::onDone()
{
    m_purpose = m_purposeValue < std::size(Purposes) ? QString::fromLatin1(Purposes[m_purposeValue])
                                                     : QStringLiteral("text");
    const bool visible = m_active ? !m_hiddenByHand : m_manual;
    if (visible != m_visible) {
        m_visible = visible;
        emit visibleChanged();
    }
    // (the field lost focus: the keys go back with it)
    if (!m_active && m_grab)
        releaseKeys();
    if (m_activeChanged)
        emit activeChanged();
    emit contentChanged();
}

bool Keyboard::grabKeys()
{
    if (m_grab)
        return true;
    if (!m_active || !m_im)
        return false;
    m_grab = new KeyboardGrab(m_im->grab_keyboard(), this);
    emit grabbingChanged();
    return true;
}

void Keyboard::releaseKeys()
{
    if (!m_grab)
        return;
    // (often from one of its own keys, Return: deleted once that is delivered)
    KeyboardGrab *grab = m_grab;
    m_grab = nullptr;
    grab->stop();
    QTimer::singleShot(0, this, [grab] { delete grab; });
    // The modifiers held when the keys were borrowed (the Alt of Alt+Space) were let go while
    // they were ours: the app still thinks them held (Backspace came to it as Alt+Backspace).
    // It is told none are, through the virtual keyboard (a change, or nothing is sent).
    if (ensureVirtualKeyboard()) {
        m_vk->modifiers(m_altMask, 0, 0, 0);
        m_vk->modifiers(0, 0, 0, 0);
    }
    emit grabbingChanged();
}

void Keyboard::setVisible(bool visible)
{
    if (visible == m_visible)
        return;
    m_visible = visible;
    // Hidden while a field has focus: stays hidden until the next field. Shown without a
    // field: by hand (typing goes in as key presses).
    m_hiddenByHand = !visible && m_active;
    m_manual = visible && !m_active;
    emit visibleChanged();
}

QString Keyboard::textBefore() const
{
    const QByteArray utf8 = m_surrounding.toUtf8();
    return QString::fromUtf8(utf8.left(qBound(0, m_cursor, int(utf8.size()))));
}

bool Keyboard::capitalize() const
{
    if (!m_active || m_capturing)
        return false;
    if (m_hint & Uppercase)
        return true;
    if (m_hint & Lowercase)
        return false;
    const QString before = textBefore();
    if (m_hint & Titlecase)
        return before.isEmpty() || before.back().isSpace();
    if (!(m_hint & AutoCapitalization))
        return false;
    // a sentence starts: at the start, or after . ! ? and a space (or a new line)
    const QString t = before.trimmed();
    if (t.isEmpty() || before.endsWith(u'\n'))
        return true;
    return before.back().isSpace() && QStringLiteral(".!?").contains(t.back());
}

void Keyboard::startCapture()
{
    m_capturing = true;
    m_captured.clear();
    emit captureChanged();
    emit contentChanged();      // (no capitals for a search)
}

void Keyboard::stopCapture()
{
    if (!m_capturing)
        return;
    m_capturing = false;
    m_captured.clear();
    emit captureChanged();
    emit contentChanged();
}

void Keyboard::type(const QString &text)
{
    if (text.isEmpty())
        return;
    if (m_capturing) {
        m_captured += text;
        emit captureChanged();
        return;
    }
    emit edited();
    if (m_active && m_im) {
        qCDebug(lcKeyboard) << "commit_string" << text << "serial" << m_serial;
        m_im->commit_string(text);
        m_im->commit(m_serial);
        // Our own view of the field, until the app sends its new state (it may not).
        noteEdit();
        const QByteArray utf8 = m_surrounding.toUtf8();
        const int at = qBound(0, m_cursor, int(utf8.size()));
        m_surrounding = QString::fromUtf8(utf8.left(at) + text.toUtf8() + utf8.mid(at));
        m_cursor = at + int(text.toUtf8().size());
        emit contentChanged();
        return;
    }
    for (const char32_t c : text.toUcs4())
        pressKeysym(xkb_utf32_to_keysym(c));
}

// The BackSpace key, as a hardware keyboard's: every toolkit deletes one grapheme (a whole
// emoji, a letter with its marks) for it. (delete_surrounding_text instead counts on the
// app's report of its text being current: Chromium's is a step late, and it dropped the
// first deletion after typing.)
void Keyboard::backspace()
{
    if (m_capturing) {
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, m_captured);
        finder.toEnd();
        const qsizetype start = finder.toPreviousBoundary();
        m_captured.truncate(start < 0 ? 0 : start);
        emit captureChanged();
        return;
    }
    emit edited();
    const QString before = textBefore();
    if (m_active && !before.isEmpty()) {
        // our own view of the field: one grapheme less
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, before);
        finder.toEnd();
        const int start = finder.toPreviousBoundary();
        const int bytes = int(before.mid(start).toUtf8().size());
        noteEdit();
        m_cursor -= bytes;
        m_surrounding = QString::fromUtf8(m_surrounding.toUtf8().remove(m_cursor, bytes));
        emit contentChanged();
    }
    qCDebug(lcKeyboard) << "BackSpace key";
    pressKeysym(XKB_KEY_BackSpace);
}

void Keyboard::noteEdit()
{
    m_before << qMakePair(m_surrounding, m_cursor);
    while (m_before.size() > 16)
        m_before.removeFirst();
}

void Keyboard::key(const QString &name)
{
    if (m_capturing) {
        if (name == QLatin1String("Return"))
            emit captureAccepted();
        return;
    }
    emit edited();
    static const QHash<QString, uint32_t> keys = {
        {QStringLiteral("Return"), XKB_KEY_Return}, {QStringLiteral("Tab"), XKB_KEY_Tab},
        {QStringLiteral("Left"), XKB_KEY_Left},     {QStringLiteral("Right"), XKB_KEY_Right},
        {QStringLiteral("Up"), XKB_KEY_Up},         {QStringLiteral("Down"), XKB_KEY_Down},
        {QStringLiteral("Escape"), XKB_KEY_Escape}, {QStringLiteral("BackSpace"), XKB_KEY_BackSpace},
    };
    if (const uint32_t sym = keys.value(name))
        pressKeysym(sym);
}

void Keyboard::press(const QString &key, int mods)
{
    if (m_capturing) {
        if (key == QLatin1String("Return"))
            emit captureAccepted();
        return;
    }
    emit edited();
    if (!ensureVirtualKeyboard())
        return;
    const QList<uint> chars = key.toUcs4();
    const uint32_t sym = chars.size() == 1 ? xkb_utf32_to_keysym(QChar::toLower(char32_t(chars.first())))
                                           : xkb_keysym_from_name(key.toUtf8().constData(), XKB_KEYSYM_CASE_INSENSITIVE);
    const auto it = m_keys.constFind(sym);
    if (sym == XKB_KEY_NoSymbol || it == m_keys.constEnd())
        return;
    uint32_t mask = it->second ? m_shiftMask : 0;
    if (mods & 1) mask |= m_ctrlMask;
    if (mods & 2) mask |= m_altMask;
    if (mods & 4) mask |= m_superMask;
    if (mods & 8) mask |= m_shiftMask;
    if (mods & 16) mask |= m_altGrMask;
    sendKeycode(it->first, mask);
}

QVariantList Keyboard::emojiGroups() const
{
    QFile file(QStringLiteral(":/qt/qml/Mobarchy/Shell/data/emoji.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("groups")).toArray().toVariantList();
}

QStringList Keyboard::searchEmoji(const QString &query, int max) const
{
    if (m_emojiWords.isEmpty()) {
        QFile file(QStringLiteral(":/qt/qml/Mobarchy/Shell/data/emoji.json"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QJsonObject all = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject names = all.value(QStringLiteral("names")).toObject();
        for (const QJsonValue &g : all.value(QStringLiteral("groups")).toArray())
            for (const QJsonValue &e : g.toObject().value(QStringLiteral("emoji")).toArray()) {
                QStringList words;
                for (const QString &phrase : names.value(e.toString()).toString().split(u'|'))
                    words << phrase << phrase.split(u' ', Qt::SkipEmptyParts);
                words.removeDuplicates();
                m_emojiWords << qMakePair(e.toString(), words);
            }
    }
    const QStringList wanted = query.toLower().split(u' ', Qt::SkipEmptyParts);
    if (wanted.isEmpty())
        return {};
    QStringList exact, prefix;
    for (const auto &[emoji, words] : std::as_const(m_emojiWords)) {
        bool all = true, named = false;
        for (const QString &w : wanted) {
            bool found = false;
            for (const QString &word : words) {
                if (word.startsWith(w)) {
                    found = true;
                    named = named || word == w || word == query.toLower().trimmed();
                }
            }
            all = all && found;
        }
        if (all)
            (named ? exact : prefix) << emoji;
    }
    return (exact + prefix).mid(0, max);
}

// --- the virtual keyboard: key presses of a Norwegian keymap -------------------------------

bool Keyboard::ensureVirtualKeyboard()
{
    if (m_vk)
        return true;
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!m_vkManager->isActive() || !app || !app->seat())
        return false;

    m_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const xkb_rule_names names = {nullptr, nullptr, "no", nullptr, nullptr};
    m_keymap = xkb_keymap_new_from_names(m_xkb, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!m_keymap)
        return false;
    m_shiftMask = 1u << xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_SHIFT);
    m_ctrlMask = 1u << xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_CTRL);
    m_altMask = 1u << xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_ALT);
    m_superMask = 1u << xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_LOGO);
    m_altGrMask = 1u << xkb_keymap_mod_get_index(m_keymap, "Mod5");
    // keysym -> the key and level that type it (levels 0 and 1: plain and shifted)
    xkb_keymap_key_for_each(m_keymap, [](xkb_keymap *map, xkb_keycode_t code, void *data) {
        auto *keys = static_cast<QHash<uint32_t, QPair<uint32_t, bool>> *>(data);
        for (xkb_level_index_t level = 0; level < 2; ++level) {
            const xkb_keysym_t *syms = nullptr;
            const int n = xkb_keymap_key_get_syms_by_level(map, code, 0, level, &syms);
            for (int i = 0; i < n; ++i) {
                if (!keys->contains(syms[i]))
                    keys->insert(syms[i], qMakePair(uint32_t(code - 8), level == 1));
            }
        }
    }, &m_keys);

    char *text = xkb_keymap_get_as_string(m_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    const size_t size = strlen(text) + 1;
    const int fd = memfd_create("mobarchy-keymap", MFD_CLOEXEC);
    if (fd < 0 || write(fd, text, size) != ssize_t(size)) {
        free(text);
        if (fd >= 0)
            close(fd);
        return false;
    }
    free(text);
    m_vk = new QtWayland::zwp_virtual_keyboard_v1(m_vkManager->create_virtual_keyboard(app->seat()));
    m_vk->keymap(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, uint32_t(size));
    close(fd);
    return true;
}

void Keyboard::pressKeysym(uint32_t keysym)
{
    if (!ensureVirtualKeyboard())
        return;
    const auto it = m_keys.constFind(keysym);
    if (it == m_keys.constEnd())
        return;  // not on the keymap (e.g. an emoji with no text field)
    sendKeycode(it->first, it->second ? m_shiftMask : 0);
}

void Keyboard::sendKeycode(uint32_t keycode, uint32_t mods)
{
    if (mods)
        m_vk->modifiers(mods, 0, 0, 0);
    m_vk->key(now(), keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
    m_vk->key(now(), keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
    if (mods)
        m_vk->modifiers(0, 0, 0, 0);
}

// --- the hardware keyboard's keys, borrowed ---------------------------------------------------

KeyboardGrab::KeyboardGrab(struct ::zwp_input_method_keyboard_grab_v2 *object, Keyboard *keyboard)
    : QtWayland::zwp_input_method_keyboard_grab_v2(object), m_keyboard(keyboard)
{
    m_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    QObject::connect(&m_repeat, &QTimer::timeout, keyboard, [this] {
        m_repeat.setInterval(m_rate > 0 ? 1000 / m_rate : 40);
        deliver(m_repeatKey);
    });
}

KeyboardGrab::~KeyboardGrab()
{
    release();
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);
    xkb_context_unref(m_xkb);
}

void KeyboardGrab::zwp_input_method_keyboard_grab_v2_keymap(uint32_t format, int32_t fd, uint32_t size)
{
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        void *map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            xkb_keymap *keymap = xkb_keymap_new_from_string(m_xkb, static_cast<const char *>(map),
                                                            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (keymap) {
                xkb_state_unref(m_state);
                xkb_keymap_unref(m_keymap);
                m_keymap = keymap;
                m_state = xkb_state_new(m_keymap);
            }
        }
    }
    close(fd);
}

void KeyboardGrab::zwp_input_method_keyboard_grab_v2_modifiers(uint32_t, uint32_t depressed, uint32_t latched,
                                                               uint32_t locked, uint32_t group)
{
    if (m_state)
        xkb_state_update_mask(m_state, depressed, latched, locked, 0, 0, group);
}

void KeyboardGrab::zwp_input_method_keyboard_grab_v2_repeat_info(int32_t rate, int32_t delay)
{
    m_rate = rate;
    m_delay = delay;
}

void KeyboardGrab::zwp_input_method_keyboard_grab_v2_key(uint32_t, uint32_t, uint32_t key, uint32_t state)
{
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (key == m_repeatKey)
            m_repeat.stop();
        return;
    }
    deliver(key);
    // (held: again, after the keyboard's delay, at its rate)
    if (!m_stopped && m_keymap && m_rate > 0 && xkb_keymap_key_repeats(m_keymap, key + 8)) {
        m_repeatKey = key;
        m_repeat.start(m_delay);
    }
}

void KeyboardGrab::deliver(uint32_t key)
{
    if (!m_state || m_stopped)
        return;
    const uint32_t code = key + 8;    // (evdev to xkb)
    static const QHash<uint32_t, QString> named = {
        {XKB_KEY_BackSpace, QStringLiteral("BackSpace")}, {XKB_KEY_Return, QStringLiteral("Return")},
        {XKB_KEY_KP_Enter, QStringLiteral("Return")}, {XKB_KEY_Escape, QStringLiteral("Escape")},
        {XKB_KEY_Tab, QStringLiteral("Tab")}, {XKB_KEY_ISO_Left_Tab, QStringLiteral("Tab")},
        {XKB_KEY_Left, QStringLiteral("Left")}, {XKB_KEY_Right, QStringLiteral("Right")},
        {XKB_KEY_Up, QStringLiteral("Up")}, {XKB_KEY_Down, QStringLiteral("Down")}};
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(m_state, code);
    if (named.contains(sym)) {
        emit m_keyboard->grabbedKey(named.value(sym));
        return;
    }
    // (with Ctrl or Super held it is a shortcut, not text)
    if (xkb_state_mod_name_is_active(m_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0
        || xkb_state_mod_name_is_active(m_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        return;
    char utf8[16];
    if (xkb_state_key_get_utf8(m_state, code, utf8, sizeof utf8) > 0) {
        const QString text = QString::fromUtf8(utf8);
        if (!text.isEmpty() && text.at(0).isPrint())
            emit m_keyboard->grabbedText(text);
    }
}
