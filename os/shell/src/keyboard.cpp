#include "keyboard.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QGuiApplication>
#include <QTextBoundaryFinder>
#include <QtGui/qguiapplication_platform.h>

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

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
    m_keyboard->m_serial++;
    const bool activated = m_pendingActive && !m_keyboard->m_active;
    m_keyboard->m_activeChanged = m_pendingActive != m_keyboard->m_active;
    m_keyboard->m_active = m_pendingActive;
    m_keyboard->m_surrounding = m_pendingText;
    m_keyboard->m_cursor = int(m_pendingCursor);
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
}

Keyboard::~Keyboard()
{
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
    if (m_activeChanged)
        emit activeChanged();
    emit contentChanged();
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
    if (!m_active)
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

void Keyboard::type(const QString &text)
{
    if (text.isEmpty())
        return;
    emit edited();
    if (m_active && m_im) {
        m_im->commit_string(text);
        m_im->commit(m_serial);
        // Our own view of the field, until the app sends its new state (it may not).
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

void Keyboard::backspace()
{
    emit edited();
    const QString before = textBefore();
    if (m_active && m_im && !before.isEmpty()) {
        // one grapheme: a whole emoji, a letter with its combining marks
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, before);
        finder.toEnd();
        const int start = finder.toPreviousBoundary();
        const int bytes = int(before.mid(start).toUtf8().size());
        m_im->delete_surrounding_text(uint32_t(bytes), 0);
        m_im->commit(m_serial);
        m_cursor -= bytes;
        m_surrounding = QString::fromUtf8(m_surrounding.toUtf8().remove(m_cursor, bytes));
        emit contentChanged();
        return;
    }
    pressKeysym(XKB_KEY_BackSpace);
}

void Keyboard::key(const QString &name)
{
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

QVariantList Keyboard::emojiGroups() const
{
    QFile file(QStringLiteral(":/qt/qml/Mobarchy/Shell/data/emoji.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("groups")).toArray().toVariantList();
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
