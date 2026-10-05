/*
 * mobarchy-fingerprintd: the fingerprint bridge in the Android container (ADR 0021). It is
 * a client of the vendor's fingerprint HAL (IFingerprint, AIDL V3), whose matcher runs in
 * the TEE, and serves enrolment and verification to the Linux side on a Unix socket
 * (protocol.h). Linux never sees a fingerprint: only which enrolment matched.
 *
 * Enrolling needs a hardware auth token for a fresh challenge, from Gatekeeper, as
 * Android's lock screen gets one when the owner confirms the PIN. Here the Linux side has
 * already asked the owner (polkit: the account password, which is the PIN), and the bridge
 * holds its own Gatekeeper credential (a random secret in the container's /data, root's
 * only), which it verifies for the token.
 *
 * Runs as root: Android's system user is uid 1000 on the host too, the owner's; the socket
 * is root's only.
 */
#include "protocol.h"

#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "fingerprintd", __VA_ARGS__)
#define ERR(...) __android_log_print(ANDROID_LOG_ERROR, "fingerprintd", __VA_ARGS__)

#define TX(n) (FIRST_CALL_TRANSACTION + (n) - 1)
#define TX_GET_INTERFACE_HASH    0x00fffffe
#define TX_GET_INTERFACE_VERSION 0x00ffffff

#define FINGERPRINT "android.hardware.biometrics.fingerprint.IFingerprint"
#define SESSION "android.hardware.biometrics.fingerprint.ISession"
#define CALLBACK "android.hardware.biometrics.fingerprint.ISessionCallback"
#define CANCELLATION "android.hardware.biometrics.common.ICancellationSignal"
#define GATEKEEPER "android.hardware.gatekeeper.IGatekeeper"
/* Google's side-FPS extension of IFingerprint (its binder extension): the HAL pauses an
 * enrolment near its end, for the UI to switch to the finger's edges, until resumeEnroll()
 * (oneway; transaction 2, from the vendor's NDK proxy) */
#define FINGERPRINT_EXT "com.google.hardware.biometrics.sidefps.IFingerprintExt"
#define TX_RESUME_ENROLL 2
#define RESUME_AFTER_MS 1200
/* the fingerprint AIDL the HAL serves: V3 (its .hash) */
#define CALLBACK_VERSION 3
#define CALLBACK_HASH "637371b53fb7faf9bd43aa51b72c23852d6e6d96"

/* the phone's owner: Android's user 0, and Gatekeeper's uid for it */
#define USER 0
#define GK_SECRET FP_SOCKET_DIR "/gatekeeper.secret"
#define GK_HANDLE FP_SOCKET_DIR "/gatekeeper.handle"
#define GK_SECRET_SIZE 32
#define GK_STATUS_OK 0
#define GK_STATUS_REENROLL 1

/* AcquiredInfo, Error (byte enums) */
#define ERROR_CANCELED 5
#define ACQUIRED_VENDOR 7           /* (comet: the enrolment paused for the edges) */

static AIBinder *(*wait_for_service)(const char *);
static void (*mark_vintf)(AIBinder *);
static AIBinder_Class *session_class, *callback_class, *cancel_class, *gatekeeper_class,
                      *fingerprint_class;
static AIBinder *session;
static AIBinder *fingerprint_ext;   /* or NULL */
static int64_t resume_at;           /* when to resume a paused enrolment (ms, 0: none) */
static int events[2];           /* the HAL's callbacks (binder threads) -> the main loop */
static int client = -1;

/* --- what runs ----------------------------------------------------------------------- */

enum op { OP_NONE, OP_ENROLL, OP_VERIFY, OP_LIST, OP_REMOVE, OP_RESET_LOCKOUT };
static enum op op;
static AIBinder *cancel_signal;     /* the running enrol or verify */
static int64_t challenge;           /* enrol's, while it runs */

/* a callback, as the binder thread passes it on */
struct event {
    int code;
    int32_t a, b;
    int64_t l;
    int32_t count;
    int32_t ids[FP_MAX_IDS];
};

static void
send_msg(const struct fp_message *m)
{
    if (client >= 0)
        send(client, m, sizeof *m, MSG_NOSIGNAL);
}

static void
send_simple(uint32_t type, int32_t value, int32_t value2, int64_t value64)
{
    struct fp_message m = { .type = type, .value = value, .value2 = value2, .value64 = value64 };
    send_msg(&m);
}

static void
op_end(void)
{
    if (cancel_signal)
        AIBinder_decStrong(cancel_signal);
    cancel_signal = NULL;
    op = OP_NONE;
}

/* --- parcels --------------------------------------------------------------------------- */

static void *on_create(void *args) { return args; }
static void on_destroy(void *data) {}
static binder_status_t
no_transact(AIBinder *b, transaction_code_t code, const AParcel *in, AParcel *out)
{
    return STATUS_UNKNOWN_TRANSACTION;
}

struct bytes { int8_t *data; int32_t len; };

static bool
alloc_bytes(void *data, int32_t len, int8_t **out)
{
    struct bytes *b = data;
    free(b->data);
    b->len = len;
    b->data = len > 0 ? calloc(len, 1) : NULL;
    *out = b->data;
    return len <= 0 || b->data;
}

struct ids { int32_t ids[FP_MAX_IDS]; int32_t count; };

static bool
alloc_ids(void *data, int32_t len, int32_t **out)
{
    struct ids *ids = data;
    ids->count = len < 0 ? 0 : len > FP_MAX_IDS ? FP_MAX_IDS : len;
    *out = len > FP_MAX_IDS ? NULL : ids->ids;
    return len <= FP_MAX_IDS;
}

/* A stable parcelable: not null, then its size (from before the size); *end: past it */
static binder_status_t
parcelable_begin(const AParcel *p, int32_t *end)
{
    int32_t nonnull, size = 0, start;
    binder_status_t st = AParcel_readInt32(p, &nonnull);
    if (st == STATUS_OK && !nonnull)
        return STATUS_UNEXPECTED_NULL;
    start = AParcel_getDataPosition(p);
    if (st == STATUS_OK) st = AParcel_readInt32(p, &size);
    *end = start + size;
    return st;
}

/* HardwareAuthToken (keymint's and keymaster's: the same fields) */
struct hat {
    int64_t challenge, user_id, authenticator_id;
    int32_t authenticator_type;
    int64_t timestamp;
    struct bytes mac;
};

static binder_status_t
read_hat(const AParcel *p, struct hat *hat)
{
    int32_t end, ts_end;
    binder_status_t st = parcelable_begin(p, &end);
    if (st == STATUS_OK) st = AParcel_readInt64(p, &hat->challenge);
    if (st == STATUS_OK) st = AParcel_readInt64(p, &hat->user_id);
    if (st == STATUS_OK) st = AParcel_readInt64(p, &hat->authenticator_id);
    if (st == STATUS_OK) st = AParcel_readInt32(p, &hat->authenticator_type);
    if (st == STATUS_OK) st = parcelable_begin(p, &ts_end);
    if (st == STATUS_OK) st = AParcel_readInt64(p, &hat->timestamp);
    if (st == STATUS_OK) st = AParcel_setDataPosition(p, ts_end);
    if (st == STATUS_OK) st = AParcel_readByteArray(p, &hat->mac, alloc_bytes);
    if (st == STATUS_OK) st = AParcel_setDataPosition(p, end);
    return st;
}

static binder_status_t
write_hat(AParcel *p, const struct hat *hat)
{
    int32_t start, ts_start, end;
    binder_status_t st = AParcel_writeInt32(p, 1);
    start = AParcel_getDataPosition(p);
    if (st == STATUS_OK) st = AParcel_writeInt32(p, 0);
    if (st == STATUS_OK) st = AParcel_writeInt64(p, hat->challenge);
    if (st == STATUS_OK) st = AParcel_writeInt64(p, hat->user_id);
    if (st == STATUS_OK) st = AParcel_writeInt64(p, hat->authenticator_id);
    if (st == STATUS_OK) st = AParcel_writeInt32(p, hat->authenticator_type);
    if (st == STATUS_OK) st = AParcel_writeInt32(p, 1);                 /* Timestamp */
    ts_start = AParcel_getDataPosition(p);
    if (st == STATUS_OK) st = AParcel_writeInt32(p, 0);
    if (st == STATUS_OK) st = AParcel_writeInt64(p, hat->timestamp);
    if (st != STATUS_OK)
        return st;
    end = AParcel_getDataPosition(p);
    AParcel_setDataPosition(p, ts_start);
    AParcel_writeInt32(p, end - ts_start);
    AParcel_setDataPosition(p, end);
    st = AParcel_writeByteArray(p, hat->mac.data, hat->mac.len);
    if (st != STATUS_OK)
        return st;
    end = AParcel_getDataPosition(p);
    AParcel_setDataPosition(p, start);
    AParcel_writeInt32(p, end - start);
    return AParcel_setDataPosition(p, end);
}

/* a reply's status header: OK, or what went wrong */
static binder_status_t
read_status(const AParcel *p, const char *what)
{
    AStatus *status = NULL;
    binder_status_t st = AParcel_readStatusHeader(p, &status);
    if (st == STATUS_OK && !AStatus_isOk(status)) {
        const char *desc = AStatus_getDescription(status);
        ERR("%s: %s", what, desc);
        AStatus_deleteDescription(desc);
        st = STATUS_FAILED_TRANSACTION;
    }
    AStatus_delete(status);
    return st;
}

/* --- the HAL's callbacks (ISessionCallback; on binder threads) ------------------------ */

static binder_status_t
callback_transact(AIBinder *b, transaction_code_t code, const AParcel *in, AParcel *out)
{
    struct event e = { .code = code };
    struct ids ids = { 0 };
    struct hat hat = { 0 };
    int8_t byte;
    binder_status_t st = STATUS_OK;

    switch (code) {
    case TX_GET_INTERFACE_VERSION:
        AParcel_writeStatusHeader(out, AStatus_newOk());
        return AParcel_writeInt32(out, CALLBACK_VERSION);
    case TX_GET_INTERFACE_HASH:
        AParcel_writeStatusHeader(out, AStatus_newOk());
        return AParcel_writeString(out, CALLBACK_HASH, strlen(CALLBACK_HASH));
    case TX(1):     /* onChallengeGenerated(long) */
    case TX(2):     /* onChallengeRevoked(long) */
    case TX(8):     /* onLockoutTimed(long durationMillis) */
    case TX(14):    /* onAuthenticatorIdRetrieved(long) */
    case TX(15):    /* onAuthenticatorIdInvalidated(long) */
        st = AParcel_readInt64(in, &e.l);
        break;
    case TX(3):     /* onAcquired(AcquiredInfo, int vendorCode) */
    case TX(4):     /* onError(Error, int vendorCode) */
        st = AParcel_readByte(in, &byte);
        e.a = byte;
        if (st == STATUS_OK) st = AParcel_readInt32(in, &e.b);
        break;
    case TX(5):     /* onEnrollmentProgress(int enrollmentId, int remaining) */
        st = AParcel_readInt32(in, &e.a);
        if (st == STATUS_OK) st = AParcel_readInt32(in, &e.b);
        break;
    case TX(6):     /* onAuthenticationSucceeded(int enrollmentId, HardwareAuthToken) */
        st = AParcel_readInt32(in, &e.a);
        if (st == STATUS_OK) st = read_hat(in, &hat);     /* (not passed on: for KeyMint) */
        free(hat.mac.data);
        break;
    case TX(7):     /* onAuthenticationFailed() */
    case TX(9):     /* onLockoutPermanent() */
    case TX(10):    /* onLockoutCleared() */
    case TX(11):    /* onInteractionDetected() */
    case TX(16):    /* onSessionClosed() */
        break;
    case TX(12):    /* onEnrollmentsEnumerated(int[]) */
    case TX(13):    /* onEnrollmentsRemoved(int[]) */
        st = AParcel_readInt32Array(in, &ids, alloc_ids);
        e.count = ids.count;
        memcpy(e.ids, ids.ids, sizeof e.ids);
        break;
    default:
        return STATUS_UNKNOWN_TRANSACTION;
    }
    if (st != STATUS_OK)
        return st;
    write(events[1], &e, sizeof e);
    return AParcel_writeStatusHeader(out, AStatus_newOk());
}

/* --- the HAL --------------------------------------------------------------------------- */

static AIBinder *
service(const char *name, AIBinder_Class *clazz)
{
    AIBinder *b = wait_for_service(name);
    if (b && !AIBinder_associateClass(b, clazz)) {
        AIBinder_decStrong(b);
        b = NULL;
    }
    if (!b)
        ERR("no %s", name);
    return b;
}

/* the first sensor's id: SensorProps[0].commonProps.sensorId */
static binder_status_t
sensor_id(AIBinder *fingerprint, int32_t *id)
{
    AParcel *in = NULL, *out = NULL;
    int32_t count, end, common_end;
    binder_status_t st = AIBinder_prepareTransaction(fingerprint, &in);
    if (st == STATUS_OK) st = AIBinder_transact(fingerprint, TX(1), &in, &out, 0);
    if (st == STATUS_OK) st = read_status(out, "getSensorProps");
    if (st == STATUS_OK) st = AParcel_readInt32(out, &count);
    if (st == STATUS_OK && count < 1) st = STATUS_BAD_VALUE;
    if (st == STATUS_OK) st = parcelable_begin(out, &end);              /* SensorProps */
    if (st == STATUS_OK) st = parcelable_begin(out, &common_end);       /* CommonProps */
    if (st == STATUS_OK) st = AParcel_readInt32(out, id);
    AParcel_delete(out);
    return st;
}

static binder_status_t
create_session(void)
{
    AIBinder *fingerprint = service(FINGERPRINT "/default", fingerprint_class);
    AParcel *in = NULL, *out = NULL;
    int32_t id;
    binder_status_t st;

    if (!fingerprint)
        return STATUS_NAME_NOT_FOUND;
    st = sensor_id(fingerprint, &id);
    AIBinder *cb = AIBinder_new(callback_class, NULL);
    if (mark_vintf)
        mark_vintf(cb);         /* the HAL only calls stable (VINTF) binders */
    if (st == STATUS_OK) st = AIBinder_prepareTransaction(fingerprint, &in);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, id);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, USER);
    if (st == STATUS_OK) st = AParcel_writeStrongBinder(in, cb);
    if (st == STATUS_OK) st = AIBinder_transact(fingerprint, TX(2), &in, &out, 0);
    if (st == STATUS_OK) st = read_status(out, "createSession");
    if (st == STATUS_OK) st = AParcel_readStrongBinder(out, &session);
    if (st == STATUS_OK && (!session || !AIBinder_associateClass(session, session_class)))
        st = STATUS_BAD_VALUE;
    AParcel_delete(out);
    AIBinder_decStrong(cb);     /* (the HAL holds it) */
    /* its extension, for resumeEnroll */
    AIBinder *ext = NULL;
    if (st == STATUS_OK && AIBinder_getExtension(fingerprint, &ext) == STATUS_OK && ext) {
        static AIBinder_Class *ext_class;
        if (!ext_class)
            ext_class = AIBinder_Class_define(FINGERPRINT_EXT, on_create, on_destroy, no_transact);
        if (AIBinder_associateClass(ext, ext_class)) {
            if (fingerprint_ext)
                AIBinder_decStrong(fingerprint_ext);
            fingerprint_ext = ext;
        } else {
            AIBinder_decStrong(ext);
        }
    }
    AIBinder_decStrong(fingerprint);
    if (st == STATUS_OK)
        LOG("session for sensor %d", id);
    return st;
}

/* a session call: its arguments by write(), and the ICancellationSignal it may return */
static binder_status_t
session_call(int code, const char *what, binder_status_t (*write)(AParcel *, const void *),
             const void *args, AIBinder **cancel)
{
    AParcel *in = NULL, *out = NULL;
    binder_status_t st = AIBinder_prepareTransaction(session, &in);
    if (st == STATUS_OK && write) st = write(in, args);
    if (st == STATUS_OK) st = AIBinder_transact(session, code, &in, &out, 0);
    else AParcel_delete(in);
    if (st == STATUS_OK) st = read_status(out, what);
    if (st == STATUS_OK && cancel) {
        st = AParcel_readStrongBinder(out, cancel);
        if (st == STATUS_OK && *cancel)
            AIBinder_associateClass(*cancel, cancel_class);
    }
    AParcel_delete(out);
    return st;
}

static binder_status_t w_long(AParcel *p, const void *v) { return AParcel_writeInt64(p, *(const int64_t *)v); }
static binder_status_t w_hat(AParcel *p, const void *v) { return write_hat(p, v); }
static binder_status_t
w_ids(AParcel *p, const void *v)
{
    const struct ids *ids = v;
    return AParcel_writeInt32Array(p, ids->ids, ids->count);
}

/* --- Gatekeeper -------------------------------------------------------------------------- */

static bool
read_file(const char *path, struct bytes *b)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > 4096) {
        if (fd >= 0) close(fd);
        return false;
    }
    b->len = st.st_size;
    b->data = malloc(b->len);
    bool ok = read(fd, b->data, b->len) == b->len;
    close(fd);
    return ok;
}

static bool
write_file(const char *path, const struct bytes *b)
{
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    bool ok = write(fd, b->data, b->len) == b->len && fsync(fd) == 0;
    close(fd);
    return ok && rename(tmp, path) == 0;
}

/* enroll(uid, currentHandle, currentPassword, desiredPassword): the new handle */
static bool
gatekeeper_enroll(AIBinder *gk, const struct bytes *old_handle, const struct bytes *secret,
                  struct bytes *handle)
{
    AParcel *in = NULL, *out = NULL;
    int32_t end, status, timeout;
    int64_t sid;
    binder_status_t st = AIBinder_prepareTransaction(gk, &in);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, USER);
    /* (no current credential: empty, not null; the arrays aren't @nullable) */
    if (st == STATUS_OK) st = AParcel_writeByteArray(in, old_handle ? old_handle->data : NULL,
                                                     old_handle ? old_handle->len : 0);
    if (st == STATUS_OK) st = AParcel_writeByteArray(in, old_handle ? secret->data : NULL,
                                                     old_handle ? secret->len : 0);
    if (st == STATUS_OK) st = AParcel_writeByteArray(in, secret->data, secret->len);
    if (st == STATUS_OK) st = AIBinder_transact(gk, TX(3), &in, &out, 0);
    if (st == STATUS_OK) st = read_status(out, "Gatekeeper enroll");
    if (st == STATUS_OK) st = parcelable_begin(out, &end);
    if (st == STATUS_OK) st = AParcel_readInt32(out, &status);
    if (st == STATUS_OK) st = AParcel_readInt32(out, &timeout);
    if (st == STATUS_OK) st = AParcel_readInt64(out, &sid);
    if (st == STATUS_OK) st = AParcel_readByteArray(out, handle, alloc_bytes);
    AParcel_delete(out);
    if (st != STATUS_OK || status != GK_STATUS_OK || handle->len <= 0) {
        ERR("Gatekeeper enroll: %d, status %d", st, st == STATUS_OK ? status : 0);
        return false;
    }
    LOG("Gatekeeper: enrolled");
    return write_file(GK_HANDLE, handle);
}

/* A hardware auth token for this challenge: Gatekeeper verifies the bridge's credential
 * (enrolling it the first time). */
static bool
auth_token(int64_t for_challenge, struct hat *hat)
{
    struct bytes secret = { 0 }, handle = { 0 };
    bool ok = false;
    AIBinder *gk = service(GATEKEEPER "/default", gatekeeper_class);
    if (!gk)
        return false;

    if (!read_file(GK_SECRET, &secret)) {
        secret.len = GK_SECRET_SIZE;
        secret.data = malloc(secret.len);
        if (getrandom(secret.data, secret.len, 0) != secret.len || !write_file(GK_SECRET, &secret))
            goto out;
        unlink(GK_HANDLE);
    }
    if (!read_file(GK_HANDLE, &handle) && !gatekeeper_enroll(gk, NULL, &secret, &handle))
        goto out;

    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        AParcel *in = NULL, *out = NULL;
        int32_t end, status = -1, timeout;
        binder_status_t st = AIBinder_prepareTransaction(gk, &in);
        if (st == STATUS_OK) st = AParcel_writeInt32(in, USER);
        if (st == STATUS_OK) st = AParcel_writeInt64(in, for_challenge);
        if (st == STATUS_OK) st = AParcel_writeByteArray(in, handle.data, handle.len);
        if (st == STATUS_OK) st = AParcel_writeByteArray(in, secret.data, secret.len);
        if (st == STATUS_OK) st = AIBinder_transact(gk, TX(4), &in, &out, 0);
        if (st == STATUS_OK) st = read_status(out, "Gatekeeper verify");
        if (st == STATUS_OK) st = parcelable_begin(out, &end);
        if (st == STATUS_OK) st = AParcel_readInt32(out, &status);
        if (st == STATUS_OK) st = AParcel_readInt32(out, &timeout);
        if (st == STATUS_OK) st = read_hat(out, hat);
        AParcel_delete(out);
        if (st == STATUS_OK && status == GK_STATUS_OK) {
            ok = true;
        } else if (st == STATUS_OK && status == GK_STATUS_REENROLL) {
            struct bytes fresh = { 0 };
            if (gatekeeper_enroll(gk, &handle, &secret, &fresh)) {
                free(handle.data);
                handle = fresh;
            }
        } else {
            ERR("Gatekeeper verify: %d, status %d, timeout %d ms", st, status, timeout);
            break;
        }
    }
out:
    free(secret.data);
    free(handle.data);
    AIBinder_decStrong(gk);
    return ok;
}

/* --- requests and events ----------------------------------------------------------------- */

static int64_t
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
resume_enroll(void)
{
    AParcel *in = NULL, *out = NULL;
    resume_at = 0;
    if (op != OP_ENROLL || !fingerprint_ext)
        return;
    if (AIBinder_prepareTransaction(fingerprint_ext, &in) == STATUS_OK
        && AIBinder_transact(fingerprint_ext, TX_RESUME_ENROLL, &in, &out, FLAG_ONEWAY) == STATUS_OK)
        LOG("enrolment resumed (the edges)");
    AParcel_delete(out);
}

static void
fail(int error)
{
    send_simple(FP_ERROR, error, 0, 0);
}

static void
on_request(const struct fp_message *m)
{
    struct ids ids = { 0 };
    binder_status_t st;

    if (!session) {
        send_simple(FP_UNAVAILABLE, 0, 0, 0);
        return;
    }
    if (m->type == FP_CANCEL) {
        if (cancel_signal) {
            AParcel *in = NULL, *out = NULL;
            if (AIBinder_prepareTransaction(cancel_signal, &in) == STATUS_OK)
                AIBinder_transact(cancel_signal, TX(1), &in, &out, FLAG_ONEWAY);
            AParcel_delete(out);
        }
        return;
    }
    if (op != OP_NONE) {
        fail(FP_ERROR_BUSY);
        return;
    }
    switch (m->type) {
    case FP_LIST:
        op = OP_LIST;
        st = session_call(TX(6), "enumerateEnrollments", NULL, NULL, NULL);
        break;
    case FP_ENROLL:
        op = OP_ENROLL;                 /* first a challenge (onChallengeGenerated) */
        st = session_call(TX(1), "generateChallenge", NULL, NULL, NULL);
        break;
    case FP_VERIFY: {
        int64_t operation = 0;          /* (no KeyMint operation waits for it) */
        op = OP_VERIFY;
        st = session_call(TX(4), "authenticate", w_long, &operation, &cancel_signal);
        break;
    }
    case FP_REMOVE:
        op = OP_REMOVE;
        ids.count = m->count < 0 ? 0 : m->count > FP_MAX_IDS ? FP_MAX_IDS : m->count;
        memcpy(ids.ids, m->ids, sizeof ids.ids);
        st = session_call(TX(7), "removeEnrollments", w_ids, &ids, NULL);
        break;
    case FP_RESET_LOCKOUT: {
        struct hat hat = { 0 };
        op = OP_RESET_LOCKOUT;
        if (!auth_token(0, &hat)) {
            op_end();
            fail(FP_ERROR_NOT_AUTHORIZED);
            return;
        }
        st = session_call(TX(10), "resetLockout", w_hat, &hat, NULL);
        free(hat.mac.data);
        break;
    }
    default:
        return;
    }
    if (st != STATUS_OK) {
        op_end();
        fail(FP_ERROR_UNABLE_TO_PROCESS);
    }
}

static void
on_event(const struct event *e)
{
    switch (e->code) {
    case TX(1): {   /* onChallengeGenerated: enrol, with Gatekeeper's token for it */
        struct hat hat = { 0 };
        if (op != OP_ENROLL)
            break;
        challenge = e->l;
        if (!auth_token(challenge, &hat)) {
            session_call(TX(2), "revokeChallenge", w_long, &challenge, NULL);
            op_end();
            fail(FP_ERROR_NOT_AUTHORIZED);
            break;
        }
        if (session_call(TX(3), "enroll", w_hat, &hat, &cancel_signal) != STATUS_OK) {
            session_call(TX(2), "revokeChallenge", w_long, &challenge, NULL);
            op_end();
            fail(FP_ERROR_UNABLE_TO_PROCESS);
        }
        free(hat.mac.data);
        break;
    }
    case TX(3):     /* onAcquired */
        /* an enrolment paused for the finger's edges: on, after the Linux side's hint */
        if (op == OP_ENROLL && e->a == ACQUIRED_VENDOR && fingerprint_ext)
            resume_at = now_ms() + RESUME_AFTER_MS;
        send_simple(FP_ACQUIRED, e->a, e->b, 0);
        break;
    case TX(4):     /* onError: the operation is over */
        if (op == OP_ENROLL)
            session_call(TX(2), "revokeChallenge", w_long, &challenge, NULL);
        op_end();
        send_simple(FP_ERROR, e->a, e->b, 0);
        break;
    case TX(5):     /* onEnrollmentProgress */
        send_simple(FP_PROGRESS, e->a, e->b, 0);
        if (e->b == 0) {
            session_call(TX(2), "revokeChallenge", w_long, &challenge, NULL);
            op_end();
            send_simple(FP_DONE, e->a, 0, 0);
        }
        break;
    case TX(6):     /* onAuthenticationSucceeded */
        op_end();
        send_simple(FP_MATCH, e->a, 0, 0);
        break;
    case TX(7):     /* onAuthenticationFailed: it keeps trying */
        send_simple(FP_NO_MATCH, 0, 0, 0);
        break;
    case TX(8):     /* onLockoutTimed */
        op_end();
        send_simple(FP_LOCKOUT, 0, 0, e->l);
        break;
    case TX(9):     /* onLockoutPermanent */
        op_end();
        send_simple(FP_LOCKOUT, 0, 0, -1);
        break;
    case TX(10):    /* onLockoutCleared */
        if (op == OP_RESET_LOCKOUT)
            op_end();
        send_simple(FP_LOCKOUT, 0, 0, 0);
        break;
    case TX(12):    /* onEnrollmentsEnumerated */
    case TX(13): {  /* onEnrollmentsRemoved: then what is left */
        if (e->code == TX(13) && op == OP_REMOVE) {
            op = OP_LIST;
            if (session_call(TX(6), "enumerateEnrollments", NULL, NULL, NULL) != STATUS_OK) {
                op_end();
                fail(FP_ERROR_UNABLE_TO_PROCESS);
            }
            break;
        }
        /* (the FPC HAL says "none" as one enrolment with id 0) */
        struct fp_message m = { .type = FP_ENROLLMENTS };
        for (int i = 0; i < e->count; i++)
            if (e->ids[i])
                m.ids[m.count++] = e->ids[i];
        op_end();
        send_msg(&m);
        break;
    }
    case TX(16):    /* onSessionClosed: the HAL went (it is restarted): a new session */
        ERR("session closed");
        AIBinder_decStrong(session);
        session = NULL;
        break;
    }
}

static void
client_gone(void)
{
    if (op == OP_ENROLL || op == OP_VERIFY) {
        struct fp_message cancel = { .type = FP_CANCEL };
        on_request(&cancel);
    }
    close(client);
    client = -1;
    LOG("client gone");
}

int
main(void)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int srv;

    signal(SIGPIPE, SIG_IGN);
    void *lib = dlopen("libbinder_ndk.so", RTLD_NOW);
    wait_for_service = lib ? dlsym(lib, "AServiceManager_waitForService") : NULL;  /* (not NDK) */
    mark_vintf = lib ? dlsym(lib, "AIBinder_markVintfStability") : NULL;           /* (LL-NDK) */
    /* binder threads, for the HAL's callbacks (not in the NDK's headers) */
    bool (*set_threads)(uint32_t) = lib ? dlsym(lib, "ABinderProcess_setThreadPoolMaxThreadCount") : NULL;
    void (*start_threads)(void) = lib ? dlsym(lib, "ABinderProcess_startThreadPool") : NULL;
    if (!wait_for_service || !mark_vintf || !set_threads || !start_threads) {
        ERR("libbinder_ndk lacks what is needed");
        return 1;
    }
    fingerprint_class = AIBinder_Class_define(FINGERPRINT, on_create, on_destroy, no_transact);
    session_class = AIBinder_Class_define(SESSION, on_create, on_destroy, no_transact);
    cancel_class = AIBinder_Class_define(CANCELLATION, on_create, on_destroy, no_transact);
    gatekeeper_class = AIBinder_Class_define(GATEKEEPER, on_create, on_destroy, no_transact);
    callback_class = AIBinder_Class_define(CALLBACK, on_create, on_destroy, callback_transact);
    if (pipe2(events, O_CLOEXEC) < 0)
        return 1;
    set_threads(2);
    start_threads();

    umask(077);
    mkdir(FP_SOCKET_DIR, 0700);
    srv = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", FP_SOCKET);
    unlink(FP_SOCKET);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(srv, 2) < 0) {
        ERR("%s: %s", FP_SOCKET, strerror(errno));
        return 1;
    }
    LOG("listening on %s", FP_SOCKET);
    if (create_session() != STATUS_OK)
        ERR("no fingerprint session (yet)");

    for (;;) {
        struct pollfd fds[3] = {
            { .fd = srv, .events = POLLIN },
            { .fd = events[0], .events = POLLIN },
            { .fd = client, .events = POLLIN },
        };
        int timeout = resume_at ? (int)(resume_at - now_ms()) : -1;
        if (resume_at && timeout <= 0) {
            resume_enroll();
            continue;
        }
        if (poll(fds, client >= 0 ? 3 : 2, timeout) < 0)
            continue;
        if (fds[1].revents & POLLIN) {
            struct event e;
            if (read(events[0], &e, sizeof e) == sizeof e)
                on_event(&e);
        }
        if (fds[0].revents & POLLIN) {
            int c = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
            if (c >= 0) {
                if (client >= 0)
                    client_gone();          /* one client: the newest */
                client = c;
                LOG("client connected");
            }
        }
        if (client >= 0 && fds[2].revents) {
            struct fp_message m;
            ssize_t n = recv(client, &m, sizeof m, 0);
            if (n <= 0) {
                client_gone();
                continue;
            }
            if (n != sizeof m)
                continue;
            if (!session && create_session() != STATUS_OK) {
                send_simple(FP_UNAVAILABLE, 0, 0, 0);
                continue;
            }
            on_request(&m);
        }
    }
}
