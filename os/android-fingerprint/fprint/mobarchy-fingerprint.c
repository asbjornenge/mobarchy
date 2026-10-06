/*
 * mobarchy-fingerprint: the fingerprint sensor on the Linux side (ADR 0021). It is the
 * fingerprint bridge's client (mobarchy-fingerprintd in the Android container; protocol in
 * ../protocol.h) and offers the sensor as fprintd does, on the system bus
 * (net.reactivated.Fprint), so what already uses fprintd works as it is: pam_fprintd
 * (sudo, polkit), settings, and Mobarchy's lock screen.
 *
 * The matcher is in the TEE, and its enrolments are Android's user 0's: the phone's. Each
 * Linux user's fingers are names for some of them, kept in /var/lib/mobarchy/fingerprint/
 * <user> ("right-thumb 1234567"). A match counts for a user only if it is one of theirs.
 *
 * Who may do what is polkit's, with fprintd's actions: net.reactivated.fprint.device.verify
 * and .enroll for one's own fingers, .setusername for someone else's.
 *
 * And two of its own, org.mobarchy.Fingerprint:
 * - ResetLockout (org.mobarchy.fingerprint.reset-lockout): the matcher locks fingers out
 *   after too many wrong ones; the owner's PIN (the lock screen gives it) lets them in again,
 *   as on Android.
 * - SkipVerify (.verify): the owner typed the PIN instead. The verify running (pam_fprintd's)
 *   ends as "disconnected", and pam_fprintd steps aside for the password (ADR 0024).
 */
#include "../protocol.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <pwd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BRIDGE "/var/lib/mobarchy/android" FP_SOCKET
#define STATE_DIR "/var/lib/mobarchy/fingerprint"
#define RETRY_SECONDS 3
#define ENROLL_STAGES 12            /* what clients show until the matcher says (it varies) */

#define BUS_NAME "net.reactivated.Fprint"
#define MANAGER_PATH "/net/reactivated/Fprint/Manager"
#define DEVICE_PATH "/net/reactivated/Fprint/Device/0"
#define DEVICE_IFACE "net.reactivated.Fprint.Device"
#define ERROR_PREFIX "net.reactivated.Fprint.Error."

static const char introspection[] =
    "<node>"
    "  <interface name='net.reactivated.Fprint.Manager'>"
    "    <method name='GetDevices'><arg type='ao' name='devices' direction='out'/></method>"
    "    <method name='GetDefaultDevice'><arg type='o' name='device' direction='out'/></method>"
    "  </interface>"
    "  <interface name='net.reactivated.Fprint.Device'>"
    "    <method name='ListEnrolledFingers'>"
    "      <arg type='s' name='username' direction='in'/>"
    "      <arg type='as' name='enrolled_fingers' direction='out'/>"
    "    </method>"
    "    <method name='DeleteEnrolledFingers'><arg type='s' name='username' direction='in'/></method>"
    "    <method name='DeleteEnrolledFingers2'/>"
    "    <method name='DeleteEnrolledFinger'><arg type='s' name='finger_name' direction='in'/></method>"
    "    <method name='Claim'><arg type='s' name='username' direction='in'/></method>"
    "    <method name='Release'/>"
    "    <method name='VerifyStart'><arg type='s' name='finger_name' direction='in'/></method>"
    "    <method name='VerifyStop'/>"
    "    <method name='EnrollStart'><arg type='s' name='finger_name' direction='in'/></method>"
    "    <method name='EnrollStop'/>"
    "    <signal name='VerifyFingerSelected'><arg type='s' name='finger_name'/></signal>"
    "    <signal name='VerifyStatus'><arg type='s' name='result'/><arg type='b' name='done'/></signal>"
    "    <signal name='EnrollStatus'><arg type='s' name='result'/><arg type='b' name='done'/></signal>"
    "    <property name='name' type='s' access='read'/>"
    "    <property name='num-enroll-stages' type='i' access='read'/>"
    "    <property name='scan-type' type='s' access='read'/>"
    "    <property name='finger-present' type='b' access='read'/>"
    "    <property name='finger-needed' type='b' access='read'/>"
    "  </interface>"
    "  <interface name='org.mobarchy.Fingerprint'>"
    "    <method name='ResetLockout'/>"
    "    <method name='SkipVerify'/>"
    "  </interface>"
    "</node>";

static const char *const fingers[] = {
    "left-thumb", "left-index-finger", "left-middle-finger", "left-ring-finger",
    "left-little-finger", "right-thumb", "right-index-finger", "right-middle-finger",
    "right-ring-finger", "right-little-finger",
};

enum action { NONE, ENROLL, VERIFY, LIST, REMOVE, RESET };

static struct {
    GDBusConnection *bus;
    int bridge;
    gboolean available;             /* the bridge is there, and has a session */
    /* the claim: one client at a time, for one user */
    char *claimer;                  /* its unique name */
    guint claimer_watch;
    char *user;
    /* what runs */
    enum action action;
    char *finger;                   /* enrolling it, or verifying it ("any") */
    gboolean stopping;              /* EnrollStop/VerifyStop: the bridge's cancel is coming */
    int stages;
    int remaining;                  /* touches the enrolment still needs (-1: not said yet) */
    gboolean finger_needed;
    GQueue waiting;                 /* calls waiting for the bridge's FP_ENROLLMENTS */
    gboolean reset_wanted;          /* a lockout reset, once what runs is over */
} app = { .bridge = -1, .stages = ENROLL_STAGES };

/* a call that waits for the bridge's list of enrolments */
struct pending {
    GDBusMethodInvocation *inv;
    enum { P_LIST, P_DELETE_ALL, P_DELETE_ONE } kind;
    char *user;
    char *finger;
};

/* --- each user's fingers: name -> enrolment id ------------------------------------------ */

static char *
state_path(const char *user)
{
    return g_build_filename(STATE_DIR, user, NULL);
}

static GHashTable *
fingers_load(const char *user)
{
    GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_autofree char *path = state_path(user), *text = NULL;

    if (!g_file_get_contents(path, &text, NULL, NULL))
        return h;
    g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
    for (char **l = lines; *l; l++) {
        g_auto(GStrv) f = g_strsplit(*l, " ", 2);
        if (f[0] && f[1])
            g_hash_table_insert(h, g_strdup(f[0]), GINT_TO_POINTER(atoi(f[1])));
    }
    return h;
}

static void
fingers_save(const char *user, GHashTable *h)
{
    g_autoptr(GString) text = g_string_new(NULL);
    g_autofree char *path = state_path(user);
    GHashTableIter it;
    gpointer k, v;

    g_hash_table_iter_init(&it, h);
    while (g_hash_table_iter_next(&it, &k, &v))
        g_string_append_printf(text, "%s %d\n", (char *)k, GPOINTER_TO_INT(v));
    g_mkdir_with_parents(STATE_DIR, 0700);
    if (!g_hash_table_size(h))
        unlink(path);
    else if (!g_file_set_contents_full(path, text->str, -1, G_FILE_SET_CONTENTS_CONSISTENT,
                                       0600, NULL))
        g_warning("%s: not saved", path);
}

/* the user's name for an enrolment, or NULL */
static const char *
finger_of(GHashTable *h, int id)
{
    GHashTableIter it;
    gpointer k, v;

    g_hash_table_iter_init(&it, h);
    while (g_hash_table_iter_next(&it, &k, &v))
        if (GPOINTER_TO_INT(v) == id)
            return k;
    return NULL;
}

/* --- the bridge ----------------------------------------------------------------------- */

static void
bridge_send(uint32_t type, const int32_t *ids, int count)
{
    struct fp_message m = { .type = type, .count = count };

    if (ids)
        memcpy(m.ids, ids, count * sizeof *ids);
    if (app.bridge >= 0)
        send(app.bridge, &m, sizeof m, MSG_NOSIGNAL);
}

static void
emit(const char *signal, GVariant *args)
{
    g_dbus_connection_emit_signal(app.bus, NULL, DEVICE_PATH, DEVICE_IFACE, signal, args, NULL);
}

static void
property_changed(const char *name, GVariant *value)
{
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", name, value);
    g_dbus_connection_emit_signal(app.bus, NULL, DEVICE_PATH, "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}as)", DEVICE_IFACE, &b, NULL), NULL);
}

static void
set_finger_needed(gboolean needed)
{
    if (needed == app.finger_needed)
        return;
    app.finger_needed = needed;
    property_changed("finger-needed", g_variant_new_boolean(needed));
}

static void
reset_lockout(void)
{
    if (app.action != NONE || app.bridge < 0) {
        app.reset_wanted = TRUE;
        return;
    }
    app.reset_wanted = FALSE;
    app.action = RESET;
    bridge_send(FP_RESET_LOCKOUT, NULL, 0);
}

static void
action_end(void)
{
    app.action = NONE;
    app.stopping = FALSE;
    g_clear_pointer(&app.finger, g_free);
    set_finger_needed(FALSE);
    if (app.reset_wanted)
        reset_lockout();
}

static void
status(const char *result, gboolean done)
{
    const char *signal = app.action == ENROLL ? "EnrollStatus" : "VerifyStatus";
    if (done)
        action_end();
    emit(signal, g_variant_new("(sb)", result, done));
}

/* what the sensor saw, as fprintd says it */
static const char *
acquired_result(int info, gboolean enroll)
{
    switch (info) {
    case 2:  return enroll ? "enroll-finger-not-centered" : "verify-finger-not-centered";
    case 4:  return enroll ? "enroll-remove-and-retry" : "verify-remove-and-retry";
    case 11: return enroll ? "enroll-remove-and-retry" : NULL;     /* immobile: move it */
    case 7:  return enroll ? "enroll-remove-and-retry" : NULL;     /* (comet: now the edges) */
    case 3: case 5: case 6: case 13:
             return enroll ? "enroll-retry-scan" : "verify-retry-scan";
    }
    return NULL;
}

static void pending_answer(const struct fp_message *m);

static void
on_bridge_event(const struct fp_message *m)
{
    switch (m->type) {
    case FP_ENROLLMENTS:
        if (app.action == REMOVE)
            app.action = NONE;              /* (a removal's: what is left, for no one) */
        else
            pending_answer(m);
        break;
    case FP_ACQUIRED: {
        const char *r = app.action == ENROLL || app.action == VERIFY
                      ? acquired_result(m->value, app.action == ENROLL) : NULL;
        if (r && !app.stopping)
            status(r, FALSE);
        break;
    }
    case FP_PROGRESS:
        if (app.action != ENROLL)
            break;
        /* the first progress says how many touches it takes */
        if (app.remaining < 0 && m->value2 > 0 && m->value2 + 1 != app.stages) {
            app.stages = m->value2 + 1;
            property_changed("num-enroll-stages", g_variant_new_int32(app.stages));
        }
        /* a touch that added nothing (the same spot again): move the finger */
        if (m->value2 > 0)
            status(app.remaining < 0 || m->value2 < app.remaining ? "enroll-stage-passed"
                                                                  : "enroll-remove-and-retry", FALSE);
        app.remaining = m->value2;
        break;
    case FP_DONE:
        if (app.action == ENROLL) {
            /* (a finger enrolled again: its old enrolment is no one's now, and goes at
             * the next list: remove_orphans) */
            g_autoptr(GHashTable) h = fingers_load(app.user);
            g_hash_table_insert(h, g_strdup(app.finger), GINT_TO_POINTER(m->value));
            fingers_save(app.user, h);
            g_message("%s: %s enrolled", app.user, app.finger);
            status("enroll-completed", TRUE);
        }
        break;
    case FP_MATCH:
        if (app.action == VERIFY) {
            g_autoptr(GHashTable) h = fingers_load(app.user);
            const char *finger = finger_of(h, m->value);
            gboolean ok = finger && (!strcmp(app.finger, "any") || !strcmp(app.finger, finger));
            status(ok ? "verify-match" : "verify-no-match", TRUE);
        }
        break;
    case FP_NO_MATCH:
        /* fprintd's verify ends at a no-match (its clients start another); the matcher
         * would keep trying */
        if (app.action == VERIFY && !app.stopping) {
            app.stopping = TRUE;
            bridge_send(FP_CANCEL, NULL, 0);
            emit("VerifyStatus", g_variant_new("(sb)", "verify-no-match", TRUE));
        }
        break;
    case FP_LOCKOUT:
        if (app.action == RESET && m->value64 == 0) {
            g_message("lockout reset");
            action_end();
            break;
        }
        if (m->value64 != 0)
            g_message("locked out (%" G_GINT64_FORMAT " ms)", (gint64)m->value64);
        if (app.action == VERIFY && m->value64 != 0)
            status("verify-unknown-error", TRUE);   /* (fprintd has no lockout) */
        break;
    case FP_ERROR:
        if (m->value == FP_ERROR_BUSY) {
            pending_answer(NULL);           /* a list asked while enrolling or verifying */
        } else if (app.action == REMOVE || app.action == RESET) {
            if (app.action == RESET)
                g_warning("lockout reset: error %d", m->value);
            action_end();
        } else if (app.action == ENROLL || app.action == VERIFY) {
            gboolean enroll = app.action == ENROLL;
            if (m->value == FP_ERROR_CANCELED && app.stopping)
                action_end();               /* (asked for: nothing to say) */
            else if (enroll)
                status(m->value == FP_ERROR_NO_SPACE ? "enroll-data-full" : "enroll-failed", TRUE);
            else
                status("verify-unknown-error", TRUE);
        } else {
            pending_answer(NULL);
        }
        break;
    case FP_UNAVAILABLE:
        if (app.action == ENROLL || app.action == VERIFY)
            status(app.action == ENROLL ? "enroll-disconnected" : "verify-disconnected", TRUE);
        pending_answer(NULL);
        break;
    }
}

static gboolean bridge_connect(gpointer unused);

static gboolean
on_bridge(gint fd, GIOCondition cond, gpointer unused)
{
    struct fp_message m;
    ssize_t n = recv(fd, &m, sizeof m, 0);

    if (n <= 0) {
        g_message("fingerprint bridge gone; trying again every %d s", RETRY_SECONDS);
        close(app.bridge);
        app.bridge = -1;
        app.available = FALSE;
        if (app.action == ENROLL || app.action == VERIFY)
            status(app.action == ENROLL ? "enroll-disconnected" : "verify-disconnected", TRUE);
        while (!g_queue_is_empty(&app.waiting))
            pending_answer(NULL);
        g_timeout_add_seconds(RETRY_SECONDS, bridge_connect, NULL);
        return G_SOURCE_REMOVE;
    }
    if (n == sizeof m)
        on_bridge_event(&m);
    return G_SOURCE_CONTINUE;
}

static gboolean
bridge_connect(gpointer unused)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);

    g_strlcpy(addr.sun_path, BRIDGE, sizeof addr.sun_path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return G_SOURCE_CONTINUE;           /* the container isn't up yet: again later */
    }
    app.bridge = fd;
    app.available = TRUE;
    g_unix_fd_add(fd, G_IO_IN | G_IO_HUP | G_IO_ERR, on_bridge, NULL);
    g_message("connected to the fingerprint bridge");
    return G_SOURCE_REMOVE;
}

/* --- polkit ------------------------------------------------------------------------------ */

static gboolean
authorized(const char *sender, const char *action)
{
    g_autoptr(GError) error = NULL;
    GVariantBuilder subject, details;

    g_variant_builder_init(&subject, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&subject, "{sv}", "name", g_variant_new_string(sender));
    g_variant_builder_init(&details, G_VARIANT_TYPE("a{ss}"));
    /* (synchronous: polkit may ask the owner for the password; the bus is ours meanwhile,
     * and the sensor serves one client anyway) */
    g_autoptr(GVariant) r = g_dbus_connection_call_sync(
        app.bus, "org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
        "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization",
        g_variant_new("((sa{sv})sa{ss}us)", "system-bus-name", &subject, action, &details,
                      1 /* AllowUserInteraction */, ""),
        G_VARIANT_TYPE("((bba{ss}))"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);
    if (!r) {
        g_warning("polkit: %s", error->message);
        return FALSE;
    }
    gboolean ok;
    g_variant_get(r, "((bb@a{ss}))", &ok, NULL, NULL);
    return ok;
}

/* the user a call is for: the caller's own, or (with .setusername) the one named */
static char *
user_for(const char *sender, const char *username, GError **error)
{
    g_autoptr(GVariant) r = g_dbus_connection_call_sync(
        app.bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetConnectionUnixUser", g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);
    if (!r)
        return NULL;
    guint32 uid;
    g_variant_get(r, "(u)", &uid);
    struct passwd *pw = getpwuid(uid);
    if (!pw) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "uid %u has no name", uid);
        return NULL;
    }
    if (!username || !*username || !strcmp(username, pw->pw_name))
        return g_strdup(pw->pw_name);
    if (!authorized(sender, "net.reactivated.fprint.device.setusername")) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED, "Not authorized");
        return NULL;
    }
    if (strchr(username, '/') || !getpwnam(username)) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No such user");
        return NULL;
    }
    return g_strdup(username);
}

/* --- D-Bus ------------------------------------------------------------------------------- */

static void
fail(GDBusMethodInvocation *inv, const char *error, const char *message)
{
    g_dbus_method_invocation_return_dbus_error(inv, error, message);
}
#define FAIL(inv, name, msg) fail(inv, ERROR_PREFIX name, msg)

static void
release_claim(void)
{
    if (app.action == ENROLL || app.action == VERIFY) {
        app.stopping = TRUE;
        bridge_send(FP_CANCEL, NULL, 0);
    }
    if (app.claimer_watch)
        g_bus_unwatch_name(app.claimer_watch);
    app.claimer_watch = 0;
    g_clear_pointer(&app.claimer, g_free);
    g_clear_pointer(&app.user, g_free);
}

static void
on_claimer_vanished(GDBusConnection *bus, const char *name, gpointer unused)
{
    g_message("the claiming client went: released");
    release_claim();
}

static gboolean
valid_finger(const char *f)
{
    for (size_t i = 0; i < G_N_ELEMENTS(fingers); i++)
        if (!strcmp(f, fingers[i]))
            return TRUE;
    return FALSE;
}

/* the bridge's list of enrolments answers the oldest waiting call (or, NULL, fails it) */
static void answer(struct pending *p, const struct fp_message *m);

/* Enrolments no user names (left by a re-enrolled finger, or a lost state directory)
 * only take the matcher's room: removed. */
static void
remove_orphans(const struct fp_message *m)
{
    int32_t orphans[FP_MAX_IDS];
    int n = 0;
    g_autoptr(GDir) dir = g_dir_open(STATE_DIR, 0, NULL);
    g_autoptr(GHashTable) named = g_hash_table_new(g_direct_hash, g_direct_equal);
    const char *user;

    while (dir && (user = g_dir_read_name(dir))) {
        g_autoptr(GHashTable) h = fingers_load(user);
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, h);
        while (g_hash_table_iter_next(&it, &k, &v))
            g_hash_table_add(named, v);
    }
    for (int i = 0; i < m->count; i++)
        if (!g_hash_table_contains(named, GINT_TO_POINTER(m->ids[i])))
            orphans[n++] = m->ids[i];
    if (n && app.action == NONE) {
        g_message("removing %d enrolment(s) no one names", n);
        app.action = REMOVE;
        bridge_send(FP_REMOVE, orphans, n);
    }
}

static void
pending_answer(const struct fp_message *m)
{
    struct pending *p = g_queue_pop_head(&app.waiting);
    if (!p)
        return;
    if (m)
        answer(p, m);
    else
        FAIL(p->inv, "Internal", "The fingerprint sensor isn't available");
    g_free(p->user);
    g_free(p->finger);
    g_free(p);
    if (m && g_queue_is_empty(&app.waiting))
        remove_orphans(m);
}

static void
answer(struct pending *p, const struct fp_message *m)
{
    g_autoptr(GHashTable) h = fingers_load(p->user);
    /* names whose enrolment the matcher no longer has are forgotten */
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, h);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        gboolean there = FALSE;
        for (int i = 0; i < m->count; i++)
            there |= m->ids[i] == GPOINTER_TO_INT(v);
        if (!there)
            g_hash_table_iter_remove(&it);
    }
    if (p->kind == P_LIST) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
        for (size_t i = 0; i < G_N_ELEMENTS(fingers); i++)
            if (g_hash_table_contains(h, fingers[i]))
                g_variant_builder_add(&b, "s", fingers[i]);
        fingers_save(p->user, h);
        if (!g_hash_table_size(h))
            FAIL(p->inv, "NoEnrolledPrints", "No fingers enrolled");
        else
            g_dbus_method_invocation_return_value(p->inv, g_variant_new("(as)", &b));
    } else {
        int32_t ids[FP_MAX_IDS];
        int n = 0;
        g_hash_table_iter_init(&it, h);
        while (g_hash_table_iter_next(&it, &k, &v))
            if (p->kind == P_DELETE_ALL || !strcmp(k, p->finger)) {
                if (n < FP_MAX_IDS)
                    ids[n++] = GPOINTER_TO_INT(v);
                g_hash_table_iter_remove(&it);
            }
        fingers_save(p->user, h);
        if (n) {
            app.action = REMOVE;
            bridge_send(FP_REMOVE, ids, n);     /* (its answer: the list, for no one) */
        }
        if (!n && p->kind == P_DELETE_ONE)
            FAIL(p->inv, "NoEnrolledPrints", "That finger isn't enrolled");
        else
            g_dbus_method_invocation_return_value(p->inv, NULL);
    }
}

static void
ask_enrollments(GDBusMethodInvocation *inv, int kind, const char *user, const char *finger)
{
    struct pending *p = g_new0(struct pending, 1);

    if (!app.available) {
        FAIL(inv, "Internal", "The fingerprint sensor isn't available");
        g_free(p);
        return;
    }
    p->inv = inv;
    p->kind = kind;
    p->user = g_strdup(user);
    p->finger = g_strdup(finger);
    if (kind == P_LIST && (app.action == ENROLL || app.action == VERIFY)) {
        /* the matcher is busy (the lock screen verifies): the names as they are */
        g_autoptr(GHashTable) h = fingers_load(user);
        struct fp_message m = { .type = FP_ENROLLMENTS };
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, h);
        while (g_hash_table_iter_next(&it, &k, &v) && m.count < FP_MAX_IDS)
            m.ids[m.count++] = GPOINTER_TO_INT(v);
        answer(p, &m);
        g_free(p->user);
        g_free(p->finger);
        g_free(p);
        return;
    }
    g_queue_push_tail(&app.waiting, p);
    bridge_send(FP_LIST, NULL, 0);
}

static void
on_method(GDBusConnection *bus, const char *sender, const char *path, const char *iface,
          const char *method, GVariant *args, GDBusMethodInvocation *inv, gpointer unused)
{
    g_autoptr(GError) error = NULL;

    if (!strcmp(iface, "org.mobarchy.Fingerprint")) {
        if (!strcmp(method, "SkipVerify")) {
            if (!authorized(sender, "net.reactivated.fprint.device.verify")) {
                FAIL(inv, "PermissionDenied", "Not authorized");
                return;
            }
            /* as at a no-match: the verify's end is said now, the bridge's cancel follows */
            if (app.action == VERIFY && !app.stopping) {
                app.stopping = TRUE;
                bridge_send(FP_CANCEL, NULL, 0);
                emit("VerifyStatus", g_variant_new("(sb)", "verify-disconnected", TRUE));
            }
            g_dbus_method_invocation_return_value(inv, NULL);
            return;
        }
        if (!authorized(sender, "org.mobarchy.fingerprint.reset-lockout")) {
            FAIL(inv, "PermissionDenied", "Not authorized");
            return;
        }
        reset_lockout();
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    if (!strcmp(iface, "net.reactivated.Fprint.Manager")) {
        if (!strcmp(method, "GetDevices")) {
            const char *devices[] = { DEVICE_PATH };
            g_dbus_method_invocation_return_value(
                inv, g_variant_new("(@ao)", g_variant_new_objv(devices, 1)));
        } else {
            g_dbus_method_invocation_return_value(inv, g_variant_new("(o)", DEVICE_PATH));
        }
        return;
    }

    const char *s = NULL;
    if (g_variant_n_children(args))
        g_variant_get_child(args, 0, "&s", &s);
    gboolean claimed = app.claimer && !strcmp(app.claimer, sender);

    if (!strcmp(method, "ListEnrolledFingers")) {
        g_autofree char *user = user_for(sender, s, &error);
        if (!user)
            g_dbus_method_invocation_return_gerror(inv, error);
        else
            ask_enrollments(inv, P_LIST, user, NULL);
    } else if (!strcmp(method, "Claim")) {
        if (app.claimer) {
            FAIL(inv, "AlreadyInUse", "The device is already claimed");
            return;
        }
        g_autofree char *user = user_for(sender, s, &error);
        if (!user) {
            g_dbus_method_invocation_return_gerror(inv, error);
            return;
        }
        app.claimer = g_strdup(sender);
        app.user = g_steal_pointer(&user);
        app.claimer_watch = g_bus_watch_name_on_connection(bus, sender, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                           NULL, on_claimer_vanished, NULL, NULL);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (!claimed) {
        FAIL(inv, "ClaimDevice", "Claim the device first");
    } else if (!strcmp(method, "Release")) {
        release_claim();
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (!strcmp(method, "VerifyStart") || !strcmp(method, "EnrollStart")) {
        gboolean enroll = method[0] == 'E';
        if (app.action != NONE) {
            FAIL(inv, "AlreadyInUse", "Already enrolling or verifying");
            return;
        }
        if (!app.available) {
            FAIL(inv, "Internal", "The fingerprint sensor isn't available");
            return;
        }
        if (enroll ? !valid_finger(s) : strcmp(s, "any") && strcmp(s, "") && !valid_finger(s)) {
            FAIL(inv, "InvalidFingername", "No such finger");
            return;
        }
        if (!authorized(sender, enroll ? "net.reactivated.fprint.device.enroll"
                                       : "net.reactivated.fprint.device.verify")) {
            FAIL(inv, "PermissionDenied", "Not authorized");
            return;
        }
        if (!enroll) {
            g_autoptr(GHashTable) h = fingers_load(app.user);
            if (!g_hash_table_size(h) || (valid_finger(s) && !g_hash_table_contains(h, s))) {
                FAIL(inv, "NoEnrolledPrints", "No fingers enrolled");
                return;
            }
        }
        app.action = enroll ? ENROLL : VERIFY;
        app.remaining = -1;
        app.finger = g_strdup(!*s ? "any" : s);
        bridge_send(enroll ? FP_ENROLL : FP_VERIFY, NULL, 0);
        set_finger_needed(TRUE);
        g_dbus_method_invocation_return_value(inv, NULL);
        if (!enroll)
            emit("VerifyFingerSelected", g_variant_new("(s)", app.finger));
    } else if (!strcmp(method, "VerifyStop") || !strcmp(method, "EnrollStop")) {
        gboolean enroll = method[0] == 'E';
        if (app.action != (enroll ? ENROLL : VERIFY) && !app.stopping) {
            FAIL(inv, "NoActionInProgress", "Nothing to stop");
            return;
        }
        if (app.action == (enroll ? ENROLL : VERIFY)) {
            app.stopping = TRUE;
            bridge_send(FP_CANCEL, NULL, 0);
        }
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (!strcmp(method, "DeleteEnrolledFingers") || !strcmp(method, "DeleteEnrolledFingers2")
               || !strcmp(method, "DeleteEnrolledFinger")) {
        gboolean one = !strcmp(method, "DeleteEnrolledFinger");
        if (!authorized(sender, "net.reactivated.fprint.device.enroll")) {
            FAIL(inv, "PermissionDenied", "Not authorized");
            return;
        }
        if (one && !valid_finger(s)) {
            FAIL(inv, "InvalidFingername", "No such finger");
            return;
        }
        ask_enrollments(inv, one ? P_DELETE_ONE : P_DELETE_ALL, app.user, one ? s : NULL);
    } else {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "No such method: %s", method);
    }
}

static GVariant *
on_get_property(GDBusConnection *bus, const char *sender, const char *path, const char *iface,
                const char *name, GError **error, gpointer unused)
{
    if (!strcmp(name, "name"))
        return g_variant_new_string("Power key fingerprint sensor");
    if (!strcmp(name, "num-enroll-stages"))
        return g_variant_new_int32(app.stages);
    if (!strcmp(name, "scan-type"))
        return g_variant_new_string("press");
    if (!strcmp(name, "finger-present"))
        return g_variant_new_boolean(FALSE);
    if (!strcmp(name, "finger-needed"))
        return g_variant_new_boolean(app.finger_needed);
    return NULL;
}

static const GDBusInterfaceVTable vtable = { .method_call = on_method,
                                             .get_property = on_get_property };

static void
on_bus(GDBusConnection *bus, const char *name, gpointer unused)
{
    g_autoptr(GDBusNodeInfo) node = g_dbus_node_info_new_for_xml(introspection, NULL);
    app.bus = bus;
    g_dbus_connection_register_object(bus, MANAGER_PATH, node->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_connection_register_object(bus, DEVICE_PATH, node->interfaces[1], &vtable, NULL, NULL, NULL);
    g_dbus_connection_register_object(bus, DEVICE_PATH, node->interfaces[2], &vtable, NULL, NULL, NULL);
}

static void
on_name_lost(GDBusConnection *bus, const char *name, gpointer loop)
{
    g_printerr("can't own %s on the system bus (is fprintd running?)\n", name);
    g_main_loop_quit(loop);
}

int
main(void)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    g_queue_init(&app.waiting);
    g_bus_own_name(G_BUS_TYPE_SYSTEM, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, on_bus, NULL,
                   on_name_lost, loop, NULL);
    if (bridge_connect(NULL) == G_SOURCE_CONTINUE)
        g_timeout_add_seconds(RETRY_SECONDS, bridge_connect, NULL);
    g_main_loop_run(loop);
    return 1;
}
