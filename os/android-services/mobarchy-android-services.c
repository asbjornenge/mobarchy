/*
 * Stand-ins for Android services the container's vendor HALs wait for, where Mobarchy
 * has its own answer on the Linux side (ADRs 0014, 0017). Each is registered with the
 * container's service manager and answers the way Android does when the service has
 * nothing to offer.
 *
 * - android.frameworks.stats.IStats/default: vendor HALs report metrics ("vendor atoms")
 *   to it. On Android it lives in system_server, which the container doesn't run.
 *   Mobarchy sends no metrics anywhere: they are accepted and dropped.
 * - android.hardware.power.IPower/default: HALs send it hints and boosts (the camera
 *   HAL waits for it). On a Pixel the vendor's power HAL acts on them by writing CPU,
 *   GPU and memory frequencies; on Mobarchy the Linux side decides those, so hints are
 *   accepted and dropped, and the optional features (hint sessions, headroom) are
 *   reported unsupported, which HALs handle. Its binder extension is Pixel's IPowerExt
 *   (named modes and boosts, which the camera HAL uses), with the same answers.
 * - android.frameworks.sensorservice.ISensorManager/default: Android's sensor service, for
 *   HALs (the camera HAL's gyro, for stabilisation). On Android it lives in system_server.
 *   Until Mobarchy bridges the sensors (they are behind the container's sensor HAL), it
 *   has none to offer: an empty list, and "no such sensor".
 * - activity, appops, permission, permission_checker, sensor_privacy, media.camera.proxy,
 *   processinfo: system_server's services that cameraserver asks about its clients:
 *   whether they are in the foreground (and their priority, for which client gets a
 *   camera), have the camera permission and app op, whether camera privacy is on, and
 *   whether device policy disables the camera. The container's
 *   clients are Mobarchy's own (its camera service on the Linux side, which decides who
 *   may use the camera), so the answers are: active, top, granted, allowed, privacy off.
 *   (Native protocols: frameworks/native IActivityManager, IAppOpsService,
 *   IPermissionController, IPermissionChecker.aidl and ISensorPrivacyManager.aidl;
 *   frameworks/av ICameraServiceProxy.aidl; frameworks/base IProcessInfoService.aidl; as
 *   of Android 17.)
 * - SurfaceFlingerAIDL: Android's compositor. cameraserver waits for it while creating any
 *   output stream (for preview streams paced by the display's vsync). The container has
 *   no display of its own (Mobarchy's compositor is on the Linux side), so it offers no
 *   display events, and cameraserver doesn't pace to them.
 *
 * Runs on the Linux side, over the binder device the container shares (libgbinder,
 * /etc/gbinder.d), and registers again whenever the container's service manager restarts.
 * A new container brings a new binder device: systemd restarts this with the container.
 */

#include <gbinder.h>
#include <gutil_log.h>

#include <glib-unix.h>

#define DEVICE      "/dev/binder"

#define TX_GET_INTERFACE_HASH       0x00fffffe
#define TX_GET_INTERFACE_VERSION    0x00ffffff
#define TX(n)                       (GBINDER_FIRST_CALL_TRANSACTION + (n) - 1)
#define TX_EXTENSION                GBINDER_FOURCC('_', 'E', 'X', 'T')

#define STATUS_UNKNOWN_TRANSACTION  (-74)   /* -EBADMSG, as libbinder */
#define EX_UNSUPPORTED_OPERATION    (-7)
#define EX_SERVICE_SPECIFIC         (-8)

struct service;
typedef GBinderLocalReply *(*Handler)(struct service *s, GBinderLocalObject *obj,
                                      GBinderRemoteRequest *req, guint code, int *status);

typedef struct service {
    const char *name;
    const char *iface;
    gint32 version;
    const char *hash;
    Handler handle;
    gboolean system;            /* not a VINTF interface: system stability */
    struct service *extension;  /* its binder extension (AIBinder_getExtension) */
    GBinderLocalObject *obj;
    guint64 calls;
} Service;

/* AIDL replies: a status header, then the return value */
static GBinderLocalReply *
reply_ok(GBinderLocalObject *obj)
{
    GBinderLocalReply *reply = gbinder_local_object_new_reply(obj);

    gbinder_local_reply_append_int32(reply, 0);     /* no exception */
    return reply;
}

static GBinderLocalReply *
reply_bool(GBinderLocalObject *obj, gboolean value)
{
    GBinderLocalReply *reply = reply_ok(obj);

    gbinder_local_reply_append_int32(reply, value ? 1 : 0);
    return reply;
}

static GBinderLocalReply *
reply_unsupported(GBinderLocalObject *obj)
{
    GBinderLocalReply *reply = gbinder_local_object_new_reply(obj);

    gbinder_local_reply_append_int32(reply, EX_UNSUPPORTED_OPERATION);
    gbinder_local_reply_append_string16(reply, NULL);   /* no message */
    gbinder_local_reply_append_int32(reply, 0);         /* no remote stack trace */
    return reply;
}

static GBinderLocalReply *
reply_error(GBinderLocalObject *obj, gint32 code)
{
    GBinderLocalReply *reply = gbinder_local_object_new_reply(obj);

    gbinder_local_reply_append_int32(reply, EX_SERVICE_SPECIFIC);
    gbinder_local_reply_append_string16(reply, NULL);   /* no message */
    gbinder_local_reply_append_int32(reply, 0);         /* no remote stack trace */
    gbinder_local_reply_append_int32(reply, code);
    return reply;
}

/* --- IStats V2 (frameworks/hardware/interfaces/stats/aidl) ---------------------------- */

static GBinderLocalReply *
stats(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    switch (code) {
    case TX(1):     /* oneway reportVendorAtom(VendorAtom) */
        if (++s->calls == 1)
            GINFO("IStats: the first vendor atom came (dropped, as all will be)");
        return NULL;
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

/* --- IPower V7 (hardware/interfaces/power/aidl) --------------------------------------- */

static GBinderLocalReply *
power(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    if (++s->calls == 1)
        GINFO("IPower: the first hint came (hints are dropped)");
    switch (code) {
    case TX(1):     /* setMode(Mode, boolean) */
    case TX(3):     /* setBoost(Boost, int) */
        return reply_ok(obj);
    case TX(2):     /* boolean isModeSupported(Mode) */
    case TX(4):     /* boolean isBoostSupported(Boost) */
        return reply_bool(obj, FALSE);
    case TX(5):     /* createHintSession */
    case TX(6):     /* getHintSessionPreferredRate */
    case TX(7):     /* createHintSessionWithConfig */
    case TX(8):     /* getSessionChannel */
    case TX(9):     /* closeSessionChannel */
    case TX(10):    /* getSupportInfo */
    case TX(11):    /* getCpuHeadroom */
    case TX(12):    /* getGpuHeadroom */
        return reply_unsupported(obj);
    case TX(13):    /* oneway sendCompositionData */
    case TX(14):    /* oneway sendCompositionUpdate */
        return NULL;
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

/* --- ISensorManager V1 (frameworks/hardware/interfaces/sensorservice/aidl) ------------ */

#define RESULT_NOT_EXIST 1      /* ISensorManager.RESULT_NOT_EXIST */

static GBinderLocalReply *
sensors(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    GBinderLocalReply *reply;

    if (++s->calls == 1)
        GINFO("ISensorManager: the first request came (no sensors yet)");
    switch (code) {
    case TX(1):     /* createAshmemDirectChannel */
    case TX(2):     /* createEventQueue */
    case TX(3):     /* createGrallocDirectChannel */
        return reply_unsupported(obj);
    case TX(4):     /* SensorInfo getDefaultSensor(SensorType) */
        return reply_error(obj, RESULT_NOT_EXIST);
    case TX(5):     /* SensorInfo[] getSensorList() */
        reply = reply_ok(obj);
        gbinder_local_reply_append_int32(reply, 0);    /* no sensors */
        return reply;
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

/* --- system_server's services, for cameraserver ------------------------------------- */

/* the reply of a call that returns nothing */
#define OK_VOID(obj) reply_ok(obj)

static GBinderLocalReply *
reply_int(GBinderLocalObject *obj, gint32 value)
{
    GBinderLocalReply *reply = reply_ok(obj);

    gbinder_local_reply_append_int32(reply, value);
    return reply;
}

static GBinderLocalObject *observer_token;  /* for registerUidObserverForUids */

#define PROCESS_STATE_TOP   2
#define PERMISSION_GRANTED  0
#define MODE_ALLOWED        0

static GBinderLocalReply *
activity(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    GBinderLocalReply *reply;

    switch (code) {
    case TX(1):     /* openContentUri: none */
        return reply_int(obj, 0);
    case TX(2):     /* registerUidObserver */
    case TX(3):     /* unregisterUidObserver */
    case TX(5):     /* addUidToObserver */
    case TX(6):     /* removeUidFromObserver */
        return OK_VOID(obj);
    case TX(4):     /* registerUidObserverForUids: a token for the observer */
        reply = reply_ok(obj);
        gbinder_local_reply_append_local_object(reply, observer_token);
        gbinder_local_reply_append_int32(reply, 0);
        return reply;
    case TX(7):     /* isUidActive */
        return reply_int(obj, 1);
    case TX(8):     /* getUidProcessState */
        return reply_int(obj, PROCESS_STATE_TOP);
    case TX(9):     /* checkPermission */
        return reply_int(obj, PERMISSION_GRANTED);
    case TX(10):    /* oneway logFgsApiBegin */
    case TX(11):    /* oneway logFgsApiEnd */
    case TX(12):    /* oneway logFgsApiStateChanged */
        return NULL;
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

static GBinderLocalReply *
appops(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    GBinderLocalReply *reply;

    switch (code) {
    case TX(1):     /* checkOperation */
    case TX(8):     /* checkAudioOperation */
    case TX(12):    /* checkOperationForDevice */
        return reply_int(obj, MODE_ALLOWED);
    case TX(2):     /* noteOperation */
    case TX(3):     /* startOperation: a SyncNotedAppOp, its mode last */
        reply = reply_ok(obj);
        gbinder_local_reply_append_int32(reply, 0);
        gbinder_local_reply_append_int32(reply, 0);
        gbinder_local_reply_append_int32(reply, MODE_ALLOWED);
        return reply;
    case TX(4):     /* finishOperation */
    case TX(5):     /* startWatchingMode */
    case TX(6):     /* stopWatchingMode */
    case TX(10):    /* setCameraAudioRestriction */
    case TX(11):    /* startWatchingModeWithFlags */
        return OK_VOID(obj);
    case TX(7):     /* permissionToOpCode: none */
        return reply_int(obj, -1);
    case TX(9):     /* shouldCollectNotes */
        return reply_int(obj, 0);
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

static GBinderLocalReply *
permission(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    switch (code) {
    case TX(1):     /* checkPermission */
        return reply_int(obj, 1);
    case TX(2):     /* noteOp */
        return reply_int(obj, MODE_ALLOWED);
    case TX(3):     /* getPackagesForUid: none */
        return reply_int(obj, 0);
    case TX(4):     /* isRuntimePermission */
        return reply_int(obj, 0);
    case TX(5):     /* getPackageUid: unknown */
        return reply_int(obj, -1);
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

static GBinderLocalReply *
permission_checker(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    switch (code) {
    case TX(1):     /* int checkPermission */
    case TX(3):     /* int checkOp */
        return reply_int(obj, PERMISSION_GRANTED);
    case TX(2):     /* finishDataDelivery */
        return OK_VOID(obj);
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

static GBinderLocalReply *
camera_proxy(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    switch (code) {
    case TX(1):     /* oneway pingForUserUpdate */
    case TX(2):     /* oneway notifyCameraState */
    case TX(3):     /* oneway notifyFeatureCombinationStats */
    case TX(7):     /* oneway notifyWatchdog */
    case TX(8):     /* oneway notifyCameraDistractionRestriction */
        return NULL;
    case TX(4):     /* getRotateAndCropOverride: ROTATE_AND_CROP_NONE */
    case TX(5):     /* getAutoframingOverride: AUTOFRAMING_OFF */
        return reply_int(obj, 0);
    case TX(6):     /* isCameraDisabled */
        return reply_bool(obj, FALSE);
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

/* processinfo: every process is in the foreground, with the best score */
static GBinderLocalReply *
processinfo(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    GBinderReader reader;
    GBinderLocalReply *reply;
    gint32 n = 0;

    if (code != TX(1) && code != TX(2)) {   /* getProcessStatesFromPids, ...AndOomScores... */
        *status = STATUS_UNKNOWN_TRANSACTION;
        return NULL;
    }
    gbinder_remote_request_init_reader(req, &reader);
    if (!gbinder_reader_read_int32(&reader, &n) || n < 0 || n > 4096)
        n = 0;
    reply = reply_ok(obj);
    gbinder_local_reply_append_int32(reply, n);             /* states */
    for (gint32 i = 0; i < n; i++)
        gbinder_local_reply_append_int32(reply, PROCESS_STATE_TOP);
    if (code == TX(2)) {
        gbinder_local_reply_append_int32(reply, n);         /* oom scores */
        for (gint32 i = 0; i < n; i++)
            gbinder_local_reply_append_int32(reply, 0);
    }
    return reply;
}

/* SurfaceFlingerAIDL (frameworks/native libs/gui ISurfaceComposer.aidl): nothing to offer */
static GBinderLocalReply *
surface_composer(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    if (++s->calls == 1)
        GINFO("SurfaceFlingerAIDL: asked for something (there is no display here)");
    return reply_unsupported(obj);
}

#define SENSOR_PRIVACY_DISABLED 2

static GBinderLocalReply *
sensor_privacy(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    switch (code) {
    case TX(1):     /* supportsSensorToggle */
    case TX(6):     /* isSensorPrivacyEnabled */
    case TX(7):     /* isCombinedToggleSensorPrivacyEnabled */
    case TX(8):     /* isToggleSensorPrivacyEnabled */
    case TX(16):    /* isCameraPrivacyEnabled */
        return reply_bool(obj, FALSE);
    case TX(2):     /* addSensorPrivacyListener */
    case TX(3):     /* addToggleSensorPrivacyListener */
    case TX(4):     /* removeSensorPrivacyListener */
    case TX(5):     /* removeToggleSensorPrivacyListener */
        return OK_VOID(obj);
    case TX(12):    /* getCameraPrivacyAllowlist: empty */
        return reply_int(obj, 0);
    case TX(13):    /* getToggleSensorPrivacyState */
        return reply_int(obj, SENSOR_PRIVACY_DISABLED);
    case TX(9):     /* setSensorPrivacy */
    case TX(10):    /* setToggleSensorPrivacy */
    case TX(11):    /* setToggleSensorPrivacyForProfileGroup */
    case TX(14):    /* setToggleSensorPrivacyState */
    case TX(15):    /* setToggleSensorPrivacyStateForProfileGroup */
        return reply_unsupported(obj);
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

/* --- Pixel's IPowerExt V1, IPower's extension (hardware/google/interfaces/power) ------- */

static GBinderLocalReply *
power_ext(Service *s, GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
      int *status)
{
    switch (code) {
    case TX(1):     /* setMode(String, boolean) */
    case TX(3):     /* setBoost(String, int) */
        return reply_ok(obj);
    case TX(2):     /* boolean isModeSupported(String) */
    case TX(4):     /* boolean isBoostSupported(String) */
        return reply_bool(obj, FALSE);
    }
    *status = STATUS_UNKNOWN_TRANSACTION;
    return NULL;
}

static Service power_extension = {
    .name = "google.hardware.power.extension.pixel.IPowerExt (IPower's extension)",
    .iface = "google.hardware.power.extension.pixel.IPowerExt", .version = 1,
    .hash = "3ce8c3ba6e332074ed51fa88088ac4af5d6c08a5", .handle = power_ext,
};

static Service services[] = {
    { .name = "android.frameworks.stats.IStats/default", .iface = "android.frameworks.stats.IStats",
      .version = 2, .hash = "7afdd459ab55ba70fdae6987d3296514822c03a4", .handle = stats },
    { .name = "android.hardware.power.IPower/default", .iface = "android.hardware.power.IPower",
      .version = 7, .hash = "4642211f44ad39c8c592b3611e62d29974b784b5", .handle = power,
      .extension = &power_extension },
    { .name = "android.frameworks.sensorservice.ISensorManager/default",
      .iface = "android.frameworks.sensorservice.ISensorManager",
      .version = 1, .hash = "514f08f7a595501066b94e5de878d240b4d49456", .handle = sensors },
    /* (system interfaces: no version or hash, and the service manager refuses VINTF
     * stability for names the VINTF manifests don't declare) */
    { .name = "activity", .iface = "android.app.IActivityManager", .handle = activity,
      .system = TRUE },
    { .name = "appops", .iface = "com.android.internal.app.IAppOpsService", .handle = appops,
      .system = TRUE },
    { .name = "permission", .iface = "android.os.IPermissionController", .handle = permission,
      .system = TRUE },
    { .name = "permission_checker", .iface = "android.permission.IPermissionChecker",
      .handle = permission_checker, .system = TRUE },
    { .name = "sensor_privacy", .iface = "android.hardware.ISensorPrivacyManager",
      .handle = sensor_privacy, .system = TRUE },
    { .name = "media.camera.proxy", .iface = "android.hardware.ICameraServiceProxy",
      .handle = camera_proxy, .system = TRUE },
    { .name = "processinfo", .iface = "android.os.IProcessInfoService", .handle = processinfo,
      .system = TRUE },
    { .name = "SurfaceFlingerAIDL", .iface = "android.gui.ISurfaceComposer",
      .handle = surface_composer, .system = TRUE },
};
#define N_SERVICES (sizeof services / sizeof *services)

static GBinderLocalReply *
on_transact(GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code, guint flags,
            int *status, void *user_data)
{
    Service *s = user_data;
    GBinderLocalReply *reply;

    *status = GBINDER_STATUS_OK;
    switch (code) {
    case TX_EXTENSION:          /* the binder's extension: just the binder, no status */
        reply = gbinder_local_object_new_reply(obj);
        gbinder_local_reply_append_local_object(reply, s->extension ? s->extension->obj : NULL);
        return reply;
    case TX_GET_INTERFACE_VERSION:
        reply = reply_ok(obj);
        gbinder_local_reply_append_int32(reply, s->version);
        return reply;
    case TX_GET_INTERFACE_HASH:
        reply = reply_ok(obj);
        gbinder_local_reply_append_string16(reply, s->hash);
        return reply;
    }
    return s->handle(s, obj, req, code, status);
}

static void
on_added(GBinderServiceManager *sm, int status, void *user_data)
{
    Service *s = user_data;
    /* "OK" only means the service manager answered: one that refused says so in the reply,
     * which libgbinder doesn't read (it refuses app uids, 10000 and up). So look for it. */
    char **names = status == GBINDER_STATUS_OK ? gbinder_servicemanager_list_sync(sm) : NULL;

    if (names && g_strv_contains((const char *const *)names, s->name))
        GINFO("registered %s", s->name);
    else
        GERR("could not register %s (%d); is this an app uid?", s->name, status);
    g_strfreev(names);
}

static void
on_presence(GBinderServiceManager *sm, void *user_data)
{
    if (!gbinder_servicemanager_is_present(sm)) {
        GINFO("service manager went away; waiting for it");
        return;
    }
    for (guint i = 0; i < N_SERVICES; i++)
        gbinder_servicemanager_add_service(sm, services[i].name, services[i].obj, on_added,
                                           &services[i]);
}

static void
new_object(GBinderServiceManager *sm, Service *s)
{
    const char *const ifaces[] = { s->iface, NULL };

    s->obj = gbinder_servicemanager_new_local_object2(sm, ifaces, on_transact, s);
    /* a vendor HAL only calls a stable interface's binder that says it is stable */
    gbinder_local_object_set_stability(s->obj, s->system ? GBINDER_STABILITY_SYSTEM
                                                         : GBINDER_STABILITY_VINTF);
}

static gboolean
on_signal(gpointer user_data)
{
    g_main_loop_quit(user_data);
    return G_SOURCE_CONTINUE;
}

int
main(void)
{
    GBinderServiceManager *sm;
    GMainLoop *loop;
    gulong presence;
    guint sigterm, sigint;

    gutil_log_func = gutil_log_stderr;     /* to the journal, unbuffered */
    gutil_log_timestamp = FALSE;
    gutil_log_default.name = "mobarchy-android-services";
    gutil_log_default.level = GLOG_LEVEL_INFO;

    /* gbinder waits for the service manager to appear (the container may still be starting) */
    sm = gbinder_servicemanager_new(DEVICE);
    if (!sm) {
        GERR("no binder at " DEVICE);
        return 1;
    }
    for (guint i = 0; i < N_SERVICES; i++) {
        new_object(sm, &services[i]);
        if (services[i].extension)
            new_object(sm, services[i].extension);
    }

    observer_token = gbinder_servicemanager_new_local_object(sm, NULL, NULL, NULL);
    gbinder_local_object_set_stability(observer_token, GBINDER_STABILITY_SYSTEM);

    loop = g_main_loop_new(NULL, FALSE);
    presence = gbinder_servicemanager_add_presence_handler(sm, on_presence, NULL);
    sigterm = g_unix_signal_add(SIGTERM, on_signal, loop);
    sigint = g_unix_signal_add(SIGINT, on_signal, loop);
    if (gbinder_servicemanager_is_present(sm))
        on_presence(sm, NULL);

    g_main_loop_run(loop);

    g_source_remove(sigterm);
    g_source_remove(sigint);
    gbinder_servicemanager_remove_handler(sm, presence);
    for (guint i = 0; i < N_SERVICES; i++) {
        gbinder_local_object_drop(services[i].obj);
        if (services[i].extension)
            gbinder_local_object_drop(services[i].extension->obj);
    }
    gbinder_local_object_drop(observer_token);
    gbinder_servicemanager_unref(sm);
    g_main_loop_unref(loop);
    return 0;
}
