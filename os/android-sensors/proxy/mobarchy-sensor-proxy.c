/*
 * mobarchy-sensor-proxy: the sensors on the Linux side (ADR 0020). It is the sensor bridge's
 * client (mobarchy-sensord in the Android container; protocol in ../protocol.h) and offers
 * them as iio-sensor-proxy does, on the system bus (net.hadess.SensorProxy), so what
 * already reads that (desktop shells, Qt Sensors, rotation helpers) works as it is:
 *   - the accelerometer's orientation: how the device is held (normal, left-up, right-up,
 *     bottom-up), from Android's device orientation sensor, which Android rotates the
 *     screen by;
 *   - the ambient light, in lux;
 *   - proximity (near or not);
 *   - and, beside iio-sensor-proxy's interface (it has none), a foldable's hinge angle
 *     (org.mobarchy.SensorProxy.Hinge: ClaimHinge, HingeAngle in degrees, 0 closed, 180 flat).
 * A sensor runs only while some client has claimed it (ClaimAccelerometer, ...), as with
 * iio-sensor-proxy; a client that leaves the bus releases its claims.
 */
#include "../protocol.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BRIDGE "/var/lib/mobarchy/android/data/mobarchy/sensord/socket"
#define RETRY_SECONDS 3
#define BUS_NAME "net.hadess.SensorProxy"
#define BUS_PATH "/net/hadess/SensorProxy"
#define BUS_IFACE "net.hadess.SensorProxy"
#define HINGE_IFACE "org.mobarchy.SensorProxy.Hinge"
#define PROXIMITY_NEAR_CM 1.0       /* binary sensors report 0 (near) or their range (far) */

static const char introspection[] =
    "<node>"
    "  <interface name='net.hadess.SensorProxy'>"
    "    <method name='ClaimAccelerometer'/>"
    "    <method name='ReleaseAccelerometer'/>"
    "    <method name='ClaimLight'/>"
    "    <method name='ReleaseLight'/>"
    "    <method name='ClaimProximity'/>"
    "    <method name='ReleaseProximity'/>"
    "    <property name='HasAccelerometer' type='b' access='read'/>"
    "    <property name='AccelerometerOrientation' type='s' access='read'/>"
    "    <property name='HasAmbientLight' type='b' access='read'/>"
    "    <property name='LightLevelUnit' type='s' access='read'/>"
    "    <property name='LightLevel' type='d' access='read'/>"
    "    <property name='HasProximity' type='b' access='read'/>"
    "    <property name='ProximityNear' type='b' access='read'/>"
    "  </interface>"
    "  <interface name='org.mobarchy.SensorProxy.Hinge'>"
    "    <method name='ClaimHinge'/>"
    "    <method name='ReleaseHinge'/>"
    "    <property name='HasHinge' type='b' access='read'/>"
    "    <property name='HingeAngle' type='d' access='read'/>"
    "  </interface>"
    "</node>";

/* what iio-sensor-proxy calls them, and which bridge sensor each one is */
enum claim { ACCEL, LIGHT, PROXIMITY, HINGE, CLAIMS };
static const char *const claim_name[CLAIMS] = { "Accelerometer", "Light", "Proximity", "Hinge" };
static const char *const has_name[CLAIMS] = { "HasAccelerometer", "HasAmbientLight", "HasProximity",
                                              "HasHinge" };
static const enum sensor_kind claim_sensor[CLAIMS] = {
    SENSOR_ORIENTATION, SENSOR_LIGHT, SENSOR_PROXIMITY, SENSOR_HINGE,
};

static struct {
    GDBusConnection *bus;
    int bridge;
    /* per claim: the clients holding it (unique name -> watch id) */
    GHashTable *claims[CLAIMS];
    /* what the bridge has said; "has" stays TRUE until it says otherwise */
    gboolean has[CLAIMS];
    const char *orientation;
    double light;
    gboolean near;
    double hinge;
} app = { .bridge = -1, .orientation = "undefined", .has = { TRUE, TRUE, TRUE, TRUE }, .hinge = -1 };

static void
bridge_send(uint32_t type, uint32_t sensor)
{
    struct sensor_message m = { .type = type, .sensor = sensor };

    if (app.bridge >= 0)
        send(app.bridge, &m, sizeof m, MSG_NOSIGNAL);
}

static void
changed(const char *name, GVariant *value)
{
    const char *iface = g_str_has_suffix(name, "Hinge") || !strcmp(name, "HingeAngle")
                      ? HINGE_IFACE : BUS_IFACE;
    GVariantBuilder b;

    if (!app.bus) {
        g_variant_unref(g_variant_ref_sink(value));
        return;
    }
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", name, value);
    g_dbus_connection_emit_signal(app.bus, NULL, BUS_PATH, "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}as)", iface, &b, NULL), NULL);
}

/* Android's device orientation (0 upright, 1 turned counter-clockwise, 2 upside down,
 * 3 turned clockwise) as iio-sensor-proxy says it: which edge is up */
static const char *
orientation_name(int android)
{
    switch (android) {
    case 0: return "normal";
    case 1: return "right-up";
    case 2: return "bottom-up";
    case 3: return "left-up";
    }
    return "undefined";
}

static void
on_event(const struct sensor_message *m)
{
    switch (m->sensor) {
    case SENSOR_ORIENTATION: {
        const char *o = orientation_name((int)m->values[0]);
        if (o != app.orientation) {
            app.orientation = o;
            changed("AccelerometerOrientation", g_variant_new_string(o));
        }
        break;
    }
    case SENSOR_LIGHT:
        if (m->values[0] != app.light) {
            app.light = m->values[0];
            changed("LightLevel", g_variant_new_double(app.light));
        }
        break;
    case SENSOR_PROXIMITY: {
        gboolean near = m->values[0] < PROXIMITY_NEAR_CM;
        if (near != app.near) {
            app.near = near;
            changed("ProximityNear", g_variant_new_boolean(near));
        }
        break;
    }
    case SENSOR_HINGE:
        if (m->values[0] != app.hinge) {
            app.hinge = m->values[0];
            changed("HingeAngle", g_variant_new_double(app.hinge));
        }
        break;
    }
}

static void
on_unavailable(uint32_t sensor)
{
    for (int c = 0; c < CLAIMS; c++)
        if (claim_sensor[c] == sensor && app.has[c]) {
            app.has[c] = FALSE;
            changed(has_name[c], g_variant_new_boolean(FALSE));
        }
}

static gboolean bridge_connect(gpointer unused);

static gboolean
on_bridge(gint fd, GIOCondition cond, gpointer unused)
{
    struct sensor_message m;
    ssize_t n = recv(fd, &m, sizeof m, 0);

    if (n <= 0) {
        g_message("sensor bridge gone; trying again every %d s", RETRY_SECONDS);
        close(app.bridge);
        app.bridge = -1;
        if (app.orientation[0] != 'u') {
            app.orientation = "undefined";
            changed("AccelerometerOrientation", g_variant_new_string(app.orientation));
        }
        g_timeout_add_seconds(RETRY_SECONDS, bridge_connect, NULL);
        return G_SOURCE_REMOVE;
    }
    if (n == sizeof m) {
        if (m.type == SENSOR_EVENT)
            on_event(&m);
        else if (m.type == SENSOR_UNAVAILABLE)
            on_unavailable(m.sensor);
    }
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
    g_unix_fd_add(fd, G_IO_IN | G_IO_HUP | G_IO_ERR, on_bridge, NULL);
    g_message("connected to the sensor bridge");
    /* claims made while it was away */
    for (int c = 0; c < CLAIMS; c++)
        if (g_hash_table_size(app.claims[c]))
            bridge_send(SENSOR_ENABLE, claim_sensor[c]);
    return G_SOURCE_REMOVE;
}

/* --- claims ---------------------------------------------------------------------------- */

static void release(int c, const char *client);

static void
on_client_vanished(GDBusConnection *bus, const char *name, gpointer unused)
{
    for (int c = 0; c < CLAIMS; c++)
        release(c, name);
}

static void
claim(int c, const char *client)
{
    if (g_hash_table_contains(app.claims[c], client))
        return;
    guint watch = g_bus_watch_name_on_connection(app.bus, client, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                 NULL, on_client_vanished, NULL, NULL);
    g_hash_table_insert(app.claims[c], g_strdup(client), GUINT_TO_POINTER(watch));
    if (g_hash_table_size(app.claims[c]) == 1)
        bridge_send(SENSOR_ENABLE, claim_sensor[c]);
}

static void
release(int c, const char *client)
{
    gpointer watch;

    if (!g_hash_table_lookup_extended(app.claims[c], client, NULL, &watch))
        return;
    g_bus_unwatch_name(GPOINTER_TO_UINT(watch));
    g_hash_table_remove(app.claims[c], client);
    if (g_hash_table_size(app.claims[c]) == 0) {
        bridge_send(SENSOR_DISABLE, claim_sensor[c]);
        if (c == ACCEL && app.orientation[0] != 'u') {
            app.orientation = "undefined";
            changed("AccelerometerOrientation", g_variant_new_string(app.orientation));
        }
    }
}

/* --- D-Bus ----------------------------------------------------------------------------- */

static void
on_method(GDBusConnection *bus, const char *sender, const char *path, const char *iface,
          const char *method, GVariant *args, GDBusMethodInvocation *inv, gpointer unused)
{
    gboolean is_claim = g_str_has_prefix(method, "Claim");
    const char *what = is_claim ? method + strlen("Claim")
                     : g_str_has_prefix(method, "Release") ? method + strlen("Release") : "";

    for (int c = 0; c < CLAIMS; c++)
        if (!g_strcmp0(what, claim_name[c])) {
            if (is_claim)
                claim(c, sender);
            else
                release(c, sender);
            g_dbus_method_invocation_return_value(inv, NULL);
            return;
        }
    g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "No such method: %s", method);
}

static GVariant *
on_get_property(GDBusConnection *bus, const char *sender, const char *path, const char *iface,
                const char *name, GError **error, gpointer unused)
{
    if (!strcmp(name, "HasAccelerometer"))
        return g_variant_new_boolean(app.has[ACCEL]);
    if (!strcmp(name, "AccelerometerOrientation"))
        return g_variant_new_string(app.orientation);
    if (!strcmp(name, "HasAmbientLight"))
        return g_variant_new_boolean(app.has[LIGHT]);
    if (!strcmp(name, "LightLevelUnit"))
        return g_variant_new_string("lux");
    if (!strcmp(name, "LightLevel"))
        return g_variant_new_double(app.light);
    if (!strcmp(name, "HasProximity"))
        return g_variant_new_boolean(app.has[PROXIMITY]);
    if (!strcmp(name, "ProximityNear"))
        return g_variant_new_boolean(app.near);
    if (!strcmp(name, "HasHinge"))
        return g_variant_new_boolean(app.has[HINGE]);
    if (!strcmp(name, "HingeAngle"))
        return g_variant_new_double(app.hinge);
    return NULL;
}

static const GDBusInterfaceVTable vtable = { .method_call = on_method,
                                             .get_property = on_get_property };

static void
on_bus(GDBusConnection *bus, const char *name, gpointer unused)
{
    g_autoptr(GDBusNodeInfo) node = g_dbus_node_info_new_for_xml(introspection, NULL);
    app.bus = bus;
    for (int i = 0; node->interfaces[i]; i++)
        g_dbus_connection_register_object(bus, BUS_PATH, node->interfaces[i], &vtable, NULL, NULL, NULL);
}

static void
on_name_lost(GDBusConnection *bus, const char *name, gpointer loop)
{
    g_printerr("can't own %s on the system bus (is iio-sensor-proxy running?)\n", name);
    g_main_loop_quit(loop);
}

int
main(void)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    for (int c = 0; c < CLAIMS; c++)
        app.claims[c] = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_bus_own_name(G_BUS_TYPE_SYSTEM, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, on_bus, NULL,
                   on_name_lost, loop, NULL);
    if (bridge_connect(NULL) == G_SOURCE_CONTINUE)
        g_timeout_add_seconds(RETRY_SECONDS, bridge_connect, NULL);
    g_main_loop_run(loop);
    return 1;
}
