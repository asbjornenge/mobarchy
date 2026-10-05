/*
 * mobarchy-sharedsecret: the HMAC key negotiation keystore2 does at every Android boot
 * (hardware/interfaces/security/sharedsecret). KeyMint in Trusty derives the key that
 * signs hardware auth tokens from it: Gatekeeper's (a confirmed PIN) and the fingerprint
 * matcher's (a matched finger), and that checks them. Until it has run, the fingerprint
 * HAL can't start ("hmac_key is not available").
 *
 * The container runs no keystore2, so this does just that part: each ISharedSecret takes
 * part (on comet, Trusty's KeyMint; the Titan's StrongBox, which Mobarchy doesn't use,
 * not), with getSharedSecretParameters, then computeSharedSecret with all of them; their
 * sharing checks must match. Run once per boot, after KeyMint started (oneshot in init).
 */
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define TAG "mobarchy-sharedsecret"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define ERR(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define DESCRIPTOR "android.hardware.security.sharedsecret.ISharedSecret"
#define TX_GET_PARAMETERS   (FIRST_CALL_TRANSACTION + 0)
#define TX_COMPUTE          (FIRST_CALL_TRANSACTION + 1)
#define MAX_PARTIES 4

/* the instances taking part */
static const char *const parties[] = { DESCRIPTOR "/default" };
#define N_PARTIES (sizeof parties / sizeof *parties)

struct bytes { int8_t *data; int32_t len; };
struct parameters { struct bytes seed, nonce; };

static void *on_create(void *args) { return args; }
static void on_destroy(void *data) {}
static binder_status_t
on_transact(AIBinder *b, transaction_code_t code, const AParcel *in, AParcel *out)
{
    return STATUS_UNKNOWN_TRANSACTION;
}

static bool
alloc_bytes(void *data, int32_t len, int8_t **out)
{
    struct bytes *b = data;
    b->len = len;
    b->data = len > 0 ? calloc(len, 1) : NULL;
    *out = b->data;
    return len <= 0 || b->data;
}

/* a stable AIDL reply's status header: OK, or why not */
static binder_status_t
read_status(const AParcel *p, const char *what, const char *who)
{
    AStatus *status = NULL;
    binder_status_t st = AParcel_readStatusHeader(p, &status);
    if (st == STATUS_OK && !AStatus_isOk(status)) {
        const char *desc = AStatus_getDescription(status);
        ERR("%s: %s: %s", who, what, desc);
        AStatus_deleteDescription(desc);
        st = STATUS_FAILED_TRANSACTION;
    }
    AStatus_delete(status);
    return st;
}

/* SharedSecretParameters: non-null, its size, seed, nonce */
static binder_status_t
read_parameters(const AParcel *p, struct parameters *params)
{
    int32_t nonnull, size;
    binder_status_t st = AParcel_readInt32(p, &nonnull);
    if (st == STATUS_OK && !nonnull)
        st = STATUS_UNEXPECTED_NULL;
    if (st == STATUS_OK) st = AParcel_readInt32(p, &size);
    if (st == STATUS_OK) st = AParcel_readByteArray(p, &params->seed, alloc_bytes);
    if (st == STATUS_OK) st = AParcel_readByteArray(p, &params->nonce, alloc_bytes);
    return st;
}

static binder_status_t
write_parameters(AParcel *p, const struct parameters *params)
{
    binder_status_t st = AParcel_writeInt32(p, 1);             /* not null */
    int32_t start = AParcel_getDataPosition(p);
    if (st == STATUS_OK) st = AParcel_writeInt32(p, 0);        /* its size, below */
    if (st == STATUS_OK) st = AParcel_writeByteArray(p, params->seed.data, params->seed.len);
    if (st == STATUS_OK) st = AParcel_writeByteArray(p, params->nonce.data, params->nonce.len);
    if (st != STATUS_OK)
        return st;
    int32_t end = AParcel_getDataPosition(p);
    AParcel_setDataPosition(p, start);
    AParcel_writeInt32(p, end - start);
    return AParcel_setDataPosition(p, end);
}

int
main(void)
{
    AIBinder *(*wait_for_service)(const char *);
    AIBinder_Class *clazz = AIBinder_Class_define(DESCRIPTOR, on_create, on_destroy, on_transact);
    AIBinder *party[MAX_PARTIES];
    struct parameters params[MAX_PARTIES] = { 0 };
    struct bytes check[MAX_PARTIES] = { 0 };
    binder_status_t st = STATUS_OK;

    void *lib = dlopen("libbinder_ndk.so", RTLD_NOW);
    wait_for_service = lib ? dlsym(lib, "AServiceManager_waitForService") : NULL;  /* (not NDK) */
    if (!wait_for_service) {
        ERR("no AServiceManager_waitForService");
        return 1;
    }
    for (size_t i = 0; i < N_PARTIES; i++) {
        party[i] = wait_for_service(parties[i]);
        if (!party[i] || !AIBinder_associateClass(party[i], clazz)) {
            ERR("no %s", parties[i]);
            return 1;
        }
    }

    /* each one's seed and nonce */
    for (size_t i = 0; i < N_PARTIES && st == STATUS_OK; i++) {
        AParcel *in = NULL, *out = NULL;
        st = AIBinder_prepareTransaction(party[i], &in);
        if (st == STATUS_OK) st = AIBinder_transact(party[i], TX_GET_PARAMETERS, &in, &out, 0);
        if (st == STATUS_OK) st = read_status(out, "getSharedSecretParameters", parties[i]);
        if (st == STATUS_OK) st = read_parameters(out, &params[i]);
        AParcel_delete(out);
    }
    /* all of them to each: the key, and a check that they all derived the same */
    for (size_t i = 0; i < N_PARTIES && st == STATUS_OK; i++) {
        AParcel *in = NULL, *out = NULL;
        st = AIBinder_prepareTransaction(party[i], &in);
        if (st == STATUS_OK) st = AParcel_writeInt32(in, N_PARTIES);
        for (size_t j = 0; j < N_PARTIES && st == STATUS_OK; j++)
            st = write_parameters(in, &params[j]);
        if (st == STATUS_OK) st = AIBinder_transact(party[i], TX_COMPUTE, &in, &out, 0);
        else AParcel_delete(in);
        if (st == STATUS_OK) st = read_status(out, "computeSharedSecret", parties[i]);
        if (st == STATUS_OK) st = AParcel_readByteArray(out, &check[i], alloc_bytes);
        AParcel_delete(out);
    }
    if (st != STATUS_OK) {
        ERR("shared secret: failed (%d)", st);
        return 1;
    }
    for (size_t i = 1; i < N_PARTIES; i++)
        if (check[i].len != check[0].len || memcmp(check[i].data, check[0].data, check[0].len)) {
            ERR("shared secret: %s and %s disagree", parties[0], parties[i]);
            return 1;
        }
    LOG("shared secret: computed (%zu taking part)", N_PARTIES);
    return 0;
}
