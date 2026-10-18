/*
 * mobarchy-location: the phone's GNSS (GPS, Galileo, ...) for Linux. It talks to the
 * Android container's GNSS HAL (AIDL android.hardware.gnss.IGnss, version 3, over
 * /dev/binder, with libgbinder as oFono does for the modem) and serves the fixes as NMEA
 * on a Unix socket, which GeoClue reads (its NMEA source: nmea-socket in geoclue.conf).
 * Apps then ask GeoClue, the standard way, and GeoClue asks the owner.
 *
 * The GNSS runs only while someone listens: the first client on the socket starts it, the
 * last one gone stops it.
 *
 *   mobarchy-location [--socket PATH]   serve
 *   mobarchy-location --once            start, print the first fix (or the satellites seen
 *                                       meanwhile), stop: a test
 */
#include <gbinder.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BINDER_DEVICE "/dev/binder"
/* Samsung's HAL is "vendor"; Google's wrapper of it (on with persist.vendor.gps.hal.service.
 * name=vendor, not started here) would be "default" */
static const char *const gnss_services[] = {
    "android.hardware.gnss.IGnss/vendor", "android.hardware.gnss.IGnss/default", NULL };
#define GNSS_IFACE "android.hardware.gnss.IGnss"
#define CALLBACK_IFACE "android.hardware.gnss.IGnssCallback"
#define DEFAULT_SOCKET "/run/mobarchy-location/nmea.sock"

/* the interface version this speaks (the vendor's is V3), and its hash
 * (hardware/interfaces gnss/aidl/aidl_api/android.hardware.gnss/3/.hash) */
#define AIDL_VERSION 3
#define AIDL_HASH "747fa8b8b4bb55d21f2c42446dd1b31de1dfbeaa"

/* IGnss (V3): its methods in declaration order, from FIRST_CALL_TRANSACTION */
enum {
    GNSS_SET_CALLBACK = 1,
    GNSS_CLOSE = 2,
    GNSS_GET_EXTENSION_PSDS = 3,
    GNSS_START = 14,
    GNSS_STOP = 15,
    GNSS_INJECT_TIME = 16,
    GNSS_INJECT_LOCATION = 17,
    GNSS_SET_POSITION_MODE = 20,
    GNSS_START_SV_STATUS = 23,
    GNSS_STOP_SV_STATUS = 24,
};
/* IGnssCallback (V3) */
enum {
    CB_SET_CAPABILITIES = 1,
    CB_STATUS = 2,
    CB_SV_STATUS = 3,
    CB_LOCATION = 4,
    CB_NMEA = 5,
    CB_ACQUIRE_WAKELOCK = 6,
    CB_RELEASE_WAKELOCK = 7,
    CB_SET_SYSTEM_INFO = 8,
    CB_REQUEST_TIME = 9,
    CB_REQUEST_LOCATION = 10,
    CB_SET_SIGNAL_TYPE_CAPABILITIES = 11,
};
/* IGnssPsds (V3) */
#define PSDS_IFACE "android.hardware.gnss.IGnssPsds"
#define PSDS_LONG_TERM 1
/* the predicted orbits (PSDS), fetched by mobarchy-location-psds: the GNSS's first fix in
 * seconds, not the minutes of decoding them from the satellites */
#define PSDS_FILE "/var/cache/mobarchy-location-psds/psds.bin"
#define PSDS_MAX_AGE (7 * 24 * 3600)

/* every AIDL interface's meta transactions */
#define AIDL_GET_INTERFACE_VERSION 0x00fffffe
#define AIDL_GET_INTERFACE_HASH 0x00fffffd

/* GnssLocation.gnssLocationFlags */
#define HAS_LAT_LONG 0x0001
#define HAS_ALTITUDE 0x0002
#define HAS_SPEED 0x0004
#define HAS_BEARING 0x0008
#define HAS_HORIZONTAL_ACCURACY 0x0010

typedef struct {
    int flags;
    double latitude, longitude, altitude, speed, bearing, accuracy;
    gint64 time_ms;
} Fix;

static struct {
    GMainLoop *loop;
    GBinderServiceManager *sm;
    GBinderRemoteObject *remote;
    GBinderClient *client;
    GBinderLocalObject *callback;
    GBinderClient *psds;    /* IGnssPsds, if the HAL has it */
    time_t psds_given;      /* the PSDS file's mtime last given to the GNSS */
    gulong death_id;
    guint retry_id;
    gboolean running;       /* the GNSS is started */
    gboolean once;
    char *last_path;        /* the last fix, kept: a hint for the next start */
    int satellites;         /* seen in the last status, used in a fix */
    GSocketService *service;
    GList *clients;         /* GSocketConnection* */
} self;

static void connect_gnss(void);
static void update_running(void);

/* --- calls into the HAL ------------------------------------------------------------- */

static gboolean
call(guint32 code, GBinderLocalRequest *req, const char *what)
{
    int status = 0;
    GBinderRemoteReply *reply = gbinder_client_transact_sync_reply(self.client, code, req, &status);
    gint32 exception = -1;
    if (reply) {
        GBinderReader reader;
        gbinder_remote_reply_init_reader(reply, &reader);
        gbinder_reader_read_int32(&reader, &exception);
        gbinder_remote_reply_unref(reply);
    }
    if (status != GBINDER_STATUS_OK || exception != 0) {
        g_warning("%s failed (status %d, exception %d)", what, status, exception);
        return FALSE;
    }
    return TRUE;
}

static gboolean
call_simple(guint32 code, const char *what)
{
    GBinderLocalRequest *req = gbinder_client_new_request(self.client);
    const gboolean ok = call(code, req, what);
    gbinder_local_request_unref(req);
    return ok;
}

static gint64
clock_ms(clockid_t clock)
{
    struct timespec ts;
    clock_gettime(clock, &ts);
    return (gint64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The time (the system's, kept by NTP): without it the GNSS starts cold, minutes longer. */
static void
inject_time(void)
{
    GBinderLocalRequest *req = gbinder_client_new_request(self.client);
    GBinderWriter w;
    gbinder_local_request_init_writer(req, &w);
    gbinder_writer_append_int64(&w, clock_ms(CLOCK_REALTIME));    /* timeMs */
    gbinder_writer_append_int64(&w, clock_ms(CLOCK_BOOTTIME));    /* timeReferenceMs: elapsedRealtime */
    gbinder_writer_append_int32(&w, 1000);                        /* uncertaintyMs */
    call(GNSS_INJECT_TIME, req, "injectTime");
    gbinder_local_request_unref(req);
}

/* Where the phone was last (a hint, with the accuracy it has after this long), so the
 * GNSS looks for the right satellites. */
/* Without a last fix: the time zone's city (tzdata's zone.tab: every zone, where zone1970.tab
 * folds some, as Europe/Oslo into Berlin's), a hint good to a
 * thousand kilometres, which the GNSS waits for when it has the time and orbits but no idea
 * where on earth it is. Nothing leaves the phone for it. */
static gboolean
zone_location(double *lat, double *lon)
{
    char *zone = g_file_read_link("/etc/localtime", NULL);
    const char *name = zone ? strstr(zone, "zoneinfo/") : NULL;
    char *tab = NULL;
    gboolean found = FALSE;
    if (name && g_file_get_contents("/usr/share/zoneinfo/zone.tab", &tab, NULL, NULL)) {
        name += strlen("zoneinfo/");
        char **lines = g_strsplit(tab, "\n", -1);
        for (char **l = lines; *l && !found; l++) {
            char **f = g_strsplit(*l, "\t", 4);
            /* country codes, coordinates (+DDMM+DDDMM or with seconds), zone */
            if (g_strv_length(f) >= 3 && *l[0] != '#' && !strcmp(f[2], name)) {
                const char *c = f[1];
                const char *split = strpbrk(c + 1, "+-");
                if (split) {
                    double d[2];
                    const char *parts[2] = { c, split };
                    for (int k = 0; k < 2; k++) {
                        const int degs = k == 0 ? 2 : 3;
                        char deg[4] = { 0 }, min[3] = { 0 };
                        memcpy(deg, parts[k] + 1, degs);
                        memcpy(min, parts[k] + 1 + degs, 2);
                        d[k] = atoi(deg) + atoi(min) / 60.0;
                        if (parts[k][0] == '-')
                            d[k] = -d[k];
                    }
                    *lat = d[0];
                    *lon = d[1];
                    found = TRUE;
                }
            }
            g_strfreev(f);
        }
        g_strfreev(lines);
    }
    g_free(tab);
    g_free(zone);
    return found;
}

static void
inject_last_location(void)
{
    char *text = NULL;
    double lat, lon, acc;
    gint64 when;
    int n = 0;
    if (self.last_path && g_file_get_contents(self.last_path, &text, NULL, NULL)) {
        n = sscanf(text, "%lf %lf %lf %" G_GINT64_FORMAT, &lat, &lon, &acc, &when);
        g_free(text);
    }
    if (n != 4) {
        if (!zone_location(&lat, &lon))
            return;
        acc = 1000000;
        when = clock_ms(CLOCK_REALTIME);
        g_message("no last fix: the time zone's place as a hint (%.1f, %.1f)", lat, lon);
    }
    /* (it may have moved: say so, up to a city's worth) */
    const double age_h = MAX(0, (clock_ms(CLOCK_REALTIME) - when) / 3600000.0);
    acc = MIN(acc + age_h * 2000, MAX(acc, 50000));
    GBinderLocalRequest *req = gbinder_client_new_request(self.client);
    GBinderWriter w;
    gbinder_local_request_init_writer(req, &w);
    /* GnssLocation */
    gbinder_writer_append_int32(&w, 1);
    gbinder_writer_append_int32(&w, 4 + 4 + 9 * 8 + 8 + 4 + 24);
    gbinder_writer_append_int32(&w, HAS_LAT_LONG | HAS_HORIZONTAL_ACCURACY);
    gbinder_writer_append_double(&w, lat);
    gbinder_writer_append_double(&w, lon);
    for (int i = 0; i < 3; i++)                 /* altitude, speed, bearing */
        gbinder_writer_append_double(&w, 0);
    gbinder_writer_append_double(&w, acc);      /* horizontalAccuracyMeters */
    for (int i = 0; i < 3; i++)                 /* the other accuracies */
        gbinder_writer_append_double(&w, 0);
    gbinder_writer_append_int64(&w, when);      /* timestampMillis */
    /* ElapsedRealtime: flags (HAS_TIMESTAMP_NS), timestampNs, timeUncertaintyNs */
    gbinder_writer_append_int32(&w, 1);
    gbinder_writer_append_int32(&w, 24);
    gbinder_writer_append_int32(&w, 1);
    gbinder_writer_append_int64(&w, clock_ms(CLOCK_BOOTTIME) * 1000000);
    gbinder_writer_append_double(&w, 0);
    call(GNSS_INJECT_LOCATION, req, "injectLocation");
    gbinder_local_request_unref(req);
}

static void
keep_last(const Fix *f)
{
    static gint64 kept;
    if (!self.last_path || f->time_ms - kept < 60000)     /* (a minute apart is enough) */
        return;
    kept = f->time_ms;
    char *text = g_strdup_printf("%.6f %.6f %.0f %" G_GINT64_FORMAT "\n", f->latitude, f->longitude,
                                 (f->flags & HAS_HORIZONTAL_ACCURACY) ? f->accuracy : 100.0, f->time_ms);
    g_file_set_contents(self.last_path, text, -1, NULL);
    g_free(text);
}

static void
get_psds(void)
{
    int status = 0;
    GBinderLocalRequest *req = gbinder_client_new_request(self.client);
    GBinderRemoteReply *reply = gbinder_client_transact_sync_reply(self.client, GNSS_GET_EXTENSION_PSDS, req, &status);
    gbinder_local_request_unref(req);
    if (!reply)
        return;
    GBinderReader r;
    gint32 exception = -1;
    gbinder_remote_reply_init_reader(reply, &r);
    if (gbinder_reader_read_int32(&r, &exception) && exception == 0) {
        GBinderRemoteObject *psds = gbinder_reader_read_object(&r);
        if (psds) {
            self.psds = gbinder_client_new(psds, PSDS_IFACE);
            gbinder_remote_object_unref(psds);
        }
    }
    gbinder_remote_reply_unref(reply);
    g_message(self.psds ? "the GNSS takes predicted orbits (PSDS)" : "no PSDS in this GNSS");
}

/* the predicted orbits, when there are fresh ones it has not had */
static void
inject_psds(void)
{
    struct stat st;
    if (!self.psds || stat(PSDS_FILE, &st) != 0 || st.st_mtime == self.psds_given
        || time(NULL) - st.st_mtime > PSDS_MAX_AGE)
        return;
    char *data = NULL;
    gsize len = 0;
    if (!g_file_get_contents(PSDS_FILE, &data, &len, NULL))
        return;
    GBinderLocalRequest *req = gbinder_client_new_request(self.psds);
    GBinderWriter w;
    gbinder_local_request_init_writer(req, &w);
    gbinder_writer_append_int32(&w, PSDS_LONG_TERM);
    gbinder_writer_append_byte_array(&w, data, (gint32)len);
    int status = 0;
    GBinderRemoteReply *reply = gbinder_client_transact_sync_reply(self.psds, 1 /* injectPsdsData */, req, &status);
    gint32 exception = -1;
    if (reply) {
        GBinderReader r;
        gbinder_remote_reply_init_reader(reply, &r);
        gbinder_reader_read_int32(&r, &exception);
        gbinder_remote_reply_unref(reply);
    }
    gbinder_local_request_unref(req);
    g_free(data);
    if (status == GBINDER_STATUS_OK && exception == 0) {
        self.psds_given = st.st_mtime;
        g_message("gave the GNSS predicted orbits (%zu bytes, %ld h old)", (size_t)len,
                  (long)((time(NULL) - st.st_mtime) / 3600));
    } else {
        g_warning("injectPsdsData failed (status %d, exception %d)", status, exception);
    }
}

static gboolean
start_gnss(void)
{
    inject_time();
    inject_last_location();
    inject_psds();

    /* PositionModeOptions: MS_BASED (assisted where it can be, else on its own), periodic,
     * a fix a second */
    GBinderLocalRequest *req = gbinder_client_new_request(self.client);
    GBinderWriter w;
    gbinder_local_request_init_writer(req, &w);
    gbinder_writer_append_int32(&w, 1);         /* (not null) */
    gbinder_writer_append_int32(&w, 4 + 6 * 4); /* the parcelable's size, with itself */
    gbinder_writer_append_int32(&w, 1);         /* mode: MS_BASED */
    gbinder_writer_append_int32(&w, 0);         /* recurrence: RECURRENCE_PERIODIC */
    gbinder_writer_append_int32(&w, 1000);      /* minIntervalMs */
    gbinder_writer_append_int32(&w, 0);         /* preferredAccuracyMeters */
    gbinder_writer_append_int32(&w, 0);         /* preferredTimeMs */
    gbinder_writer_append_bool(&w, FALSE);      /* lowPowerMode */
    const gboolean mode = call(GNSS_SET_POSITION_MODE, req, "setPositionMode");
    gbinder_local_request_unref(req);
    if (!mode)
        return FALSE;
    call_simple(GNSS_START_SV_STATUS, "startSvStatus");
    return call_simple(GNSS_START, "start");
}

/* --- NMEA ------------------------------------------------------------------------------ */

static void
nmea_append(GString *out, const char *body)
{
    guint8 sum = 0;
    for (const char *p = body; *p; p++)
        sum ^= (guint8)*p;
    g_string_append_printf(out, "$%s*%02X\r\n", body, sum);
}

static void
coordinate(GString *s, double degrees, gboolean latitude)
{
    const double a = fabs(degrees);
    const int d = (int)a;
    const double m = (a - d) * 60.0;
    if (latitude)
        g_string_append_printf(s, "%02d%08.5f,%c", d, m, degrees < 0 ? 'S' : 'N');
    else
        g_string_append_printf(s, "%03d%08.5f,%c", d, m, degrees < 0 ? 'W' : 'E');
}

/* GGA (the position, how many satellites, how good) and RMC (with the date, speed and
 * course), as GeoClue reads them. GeoClue makes an accuracy of the HDOP: the HAL's
 * accuracy (metres) is turned into an HDOP that GeoClue turns back into about as much. */
static char *
nmea_for(const Fix *f)
{
    const time_t t = (time_t)(f->time_ms / 1000);
    struct tm tm;
    gmtime_r(&t, &tm);
    const int cs = (int)(f->time_ms % 1000) / 10;
    const double acc = (f->flags & HAS_HORIZONTAL_ACCURACY) ? f->accuracy : 10;
    const double hdop = acc <= 1 ? 1.0 : acc <= 3 ? 2.0 : acc <= 50 ? 5.0 : acc <= 100 ? 10.0 : 20.0;

    GString *out = g_string_new(NULL);
    GString *s = g_string_new(NULL);
    g_string_printf(s, "GPGGA,%02d%02d%02d.%02d,", tm.tm_hour, tm.tm_min, tm.tm_sec, cs);
    coordinate(s, f->latitude, TRUE);
    g_string_append_c(s, ',');
    coordinate(s, f->longitude, FALSE);
    g_string_append_printf(s, ",1,%02d,%.1f,%.1f,M,,M,,", MAX(self.satellites, 4), hdop,
                           (f->flags & HAS_ALTITUDE) ? f->altitude : 0.0);
    nmea_append(out, s->str);

    g_string_printf(s, "GPRMC,%02d%02d%02d.%02d,A,", tm.tm_hour, tm.tm_min, tm.tm_sec, cs);
    coordinate(s, f->latitude, TRUE);
    g_string_append_c(s, ',');
    coordinate(s, f->longitude, FALSE);
    g_string_append_printf(s, ",%.1f,", (f->flags & HAS_SPEED) ? f->speed * 1.943844 : 0.0);  /* knots */
    if (f->flags & HAS_BEARING)
        g_string_append_printf(s, "%.1f", f->bearing);
    g_string_append_printf(s, ",%02d%02d%02d,,,A", tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100);
    nmea_append(out, s->str);
    g_string_free(s, TRUE);
    return g_string_free(out, FALSE);
}

static void
send_to_clients(const char *text)
{
    for (GList *l = self.clients; l; ) {
        GList *next = l->next;
        GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(l->data));
        if (!g_output_stream_write_all(out, text, strlen(text), NULL, NULL, NULL)) {
            g_io_stream_close(G_IO_STREAM(l->data), NULL, NULL);
            g_object_unref(l->data);
            self.clients = g_list_delete_link(self.clients, l);
        }
        l = next;
    }
    if (!self.clients)
        update_running();
}

/* --- the HAL's calls back ----------------------------------------------------------------- */

/* (libgbinder reads, it does not skip, numbers) */
static gboolean
skip32(GBinderReader *r)
{
    gint32 v;
    return gbinder_reader_read_int32(r, &v);
}

static gboolean
skip64(GBinderReader *r)
{
    gint64 v;
    return gbinder_reader_read_int64(r, &v);
}

static gboolean
read_location(GBinderReader *r, Fix *f)
{
    gint32 present = 0, size = 0;
    if (!gbinder_reader_read_int32(r, &present) || !present || !gbinder_reader_read_int32(r, &size))
        return FALSE;
    return gbinder_reader_read_int32(r, &f->flags)
        && gbinder_reader_read_double(r, &f->latitude)
        && gbinder_reader_read_double(r, &f->longitude)
        && gbinder_reader_read_double(r, &f->altitude)
        && gbinder_reader_read_double(r, &f->speed)
        && gbinder_reader_read_double(r, &f->bearing)
        && gbinder_reader_read_double(r, &f->accuracy)
        && skip64(r)        /* verticalAccuracyMeters */
        && skip64(r)        /* speedAccuracyMetersPerSecond */
        && skip64(r)        /* bearingAccuracyDegrees */
        && gbinder_reader_read_int64(r, &f->time_ms);
}

static void
on_location(GBinderReader *r)
{
    Fix f = { 0 };
    if (!read_location(r, &f) || !(f.flags & HAS_LAT_LONG))
        return;
    keep_last(&f);
    static gboolean fixed;
    if (!fixed)
        g_message("first fix: ±%.0f m", f.accuracy);
    fixed = TRUE;
    if (self.once) {
        printf("fix: %.6f, %.6f  ±%.0f m  (%d satellites)\n", f.latitude, f.longitude,
               f.accuracy, self.satellites);
        g_main_loop_quit(self.loop);
        return;
    }
    char *nmea = nmea_for(&f);
    send_to_clients(nmea);
    g_free(nmea);
}

/* GnssSvInfo[]: how many are used in a fix (svFlag USED_IN_FIX = 4) */
static void
on_sv_status(GBinderReader *r)
{
    gint32 n = 0;
    int used = 0, seen = 0, strong = 0, ephemeris = 0;
    if (!gbinder_reader_read_int32(r, &n) || n < 0)
        return;
    for (int i = 0; i < n; i++) {
        gint32 present = 0, size = 0, flags = 0;
        if (!gbinder_reader_read_int32(r, &present) || !present || !gbinder_reader_read_int32(r, &size))
            return;
        /* svid, constellation, cN0Dbhz, basebandCN0DbHz, elevation, azimuth, carrierFrequencyHz, svFlag */
        const gsize start = gbinder_reader_bytes_read(r) - 8;
        gfloat cn0 = 0;
        skip32(r);                      /* svid */
        skip32(r);                      /* constellation */
        gbinder_reader_read_float(r, &cn0);
        for (int k = 0; k < 3; k++)     /* basebandCN0DbHz, elevation, azimuth */
            skip32(r);
        skip64(r);                      /* carrierFrequencyHz */
        gbinder_reader_read_int32(r, &flags);
        if (cn0 >= 30)
            strong++;
        if (flags & 1)                  /* HAS_EPHEMERIS_DATA */
            ephemeris++;
        /* (a newer HAL's parcelable may be longer: on to its end) */
        while (gbinder_reader_bytes_read(r) < start + (gsize)size && skip32(r))
            ;
        seen++;
        if (flags & 4)
            used++;
    }
    if (self.once)
        fprintf(stderr, "satellites: %d seen, %d used\n", seen, used);
    /* (in the log when it changes: how a search is going) */
    static int last_seen = -1, last_used = -1;
    if (seen != last_seen || used != last_used)
        g_message("satellites: %d seen (%d strong, %d with ephemeris), %d used in a fix",
                  seen, strong, ephemeris, used);
    last_seen = seen;
    last_used = used;
    self.satellites = used;
}

static GBinderLocalReply *
on_callback(GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code, guint flags,
            int *status, void *user)
{
    GBinderReader r;
    GBinderLocalReply *reply = gbinder_local_object_new_reply(obj);
    GBinderWriter w;
    gbinder_local_reply_init_writer(reply, &w);
    gbinder_writer_append_int32(&w, 0);     /* no exception */
    *status = GBINDER_STATUS_OK;

    gbinder_remote_request_init_reader(req, &r);
    /* (each kind of call once in the log: what the HAL says at all) */
    static guint32 told;
    if (code < 32 && !(told & (1u << code))) {
        told |= 1u << code;
        g_message("the HAL calls back: %u", code);
    }
    switch (code) {
    case AIDL_GET_INTERFACE_VERSION:
        gbinder_writer_append_int32(&w, AIDL_VERSION);
        break;
    case AIDL_GET_INTERFACE_HASH:
        gbinder_writer_append_string16(&w, AIDL_HASH);
        break;
    case CB_LOCATION:
        on_location(&r);
        break;
    case CB_SV_STATUS:
        on_sv_status(&r);
        break;
    case CB_STATUS: {
        gint32 s = 0;
        gbinder_reader_read_int32(&r, &s);
        g_message("GNSS status %d", s);
        break;
    }
    case CB_SET_CAPABILITIES: {
        gint32 caps = 0;
        gbinder_reader_read_int32(&r, &caps);
        g_message("GNSS capabilities 0x%x", caps);
        break;
    }
    case CB_NMEA: {
        /* the GNSS's own NMEA: GGA and GSV now and then in the log, to see how a search goes */
        gint64 ts = 0;
        gbinder_reader_read_int64(&r, &ts);
        char *nmea = gbinder_reader_read_string16(&r);
        static gint64 shown;
        static int raw;
        if (nmea && raw < 12 && g_getenv("MOBARCHY_LOCATION_NMEA")) {  /* (diagnosis: the first sentences as they come) */
            raw++;
            g_message("NMEA raw %s", g_strstrip(nmea));
        }
        if (nmea && strstr(nmea, "GGA,") && g_get_monotonic_time() - shown > 30000000) {
            g_strstrip(nmea);
            g_message("NMEA %s", nmea);
            shown = g_get_monotonic_time();
        }
        g_free(nmea);
        break;
    }
    case CB_REQUEST_TIME:
        if (self.client)
            g_idle_add_once((GSourceOnceFunc)inject_time, NULL);
        break;
    case CB_REQUEST_LOCATION:
        if (self.client)
            g_idle_add_once((GSourceOnceFunc)inject_last_location, NULL);
        break;
    case CB_ACQUIRE_WAKELOCK:
    case CB_RELEASE_WAKELOCK:
    case CB_SET_SYSTEM_INFO:
    case CB_SET_SIGNAL_TYPE_CAPABILITIES:
        break;
    default:
        g_message("callback transaction %u (unknown)", code);
        break;
    }
    return reply;
}

/* --- the HAL coming and going ------------------------------------------------------------ */

static void
drop_gnss(void)
{
    if (self.remote && self.death_id)
        gbinder_remote_object_remove_handler(self.remote, self.death_id);
    self.death_id = 0;
    g_clear_pointer(&self.psds, gbinder_client_unref);
    self.psds_given = 0;
    g_clear_pointer(&self.client, gbinder_client_unref);
    g_clear_pointer(&self.remote, gbinder_remote_object_unref);
    self.running = FALSE;
}

static gboolean
retry(gpointer data)
{
    self.retry_id = 0;
    connect_gnss();
    return G_SOURCE_REMOVE;
}

static void
on_death(GBinderRemoteObject *obj, void *data)
{
    g_message("the GNSS HAL went away");
    drop_gnss();
    if (!self.retry_id)
        self.retry_id = g_timeout_add_seconds(5, retry, NULL);
}

static void
connect_gnss(void)
{
    int status = 0;
    if (!self.sm)
        self.sm = gbinder_servicemanager_new(BINDER_DEVICE);
    GBinderRemoteObject *remote = NULL;
    for (int i = 0; self.sm && !remote && gnss_services[i]; i++)
        remote = gbinder_servicemanager_get_service_sync(self.sm, gnss_services[i], &status);
    if (!remote) {
        static gboolean told;
        if (!told)
            g_message("no GNSS HAL yet: waiting for it");
        told = TRUE;
        /* (the Android container not up yet) */
        g_clear_pointer(&self.sm, gbinder_servicemanager_unref);
        if (!self.retry_id)
            self.retry_id = g_timeout_add_seconds(5, retry, NULL);
        return;
    }
    self.remote = gbinder_remote_object_ref(remote);
    self.client = gbinder_client_new(self.remote, GNSS_IFACE);
    self.death_id = gbinder_remote_object_add_death_handler(self.remote, on_death, NULL);
    if (!self.callback) {
        self.callback = gbinder_servicemanager_new_local_object(self.sm, CALLBACK_IFACE, on_callback, NULL);
        /* the HAL is @VintfStability: so must what it calls be */
        gbinder_local_object_set_stability(self.callback, GBINDER_STABILITY_VINTF);
    }
    GBinderLocalRequest *req = gbinder_client_new_request(self.client);
    GBinderWriter w;
    gbinder_local_request_init_writer(req, &w);
    gbinder_writer_append_local_object(&w, self.callback);
    const gboolean ok = call(GNSS_SET_CALLBACK, req, "setCallback");
    gbinder_local_request_unref(req);
    if (!ok) {
        drop_gnss();
        self.retry_id = g_timeout_add_seconds(5, retry, NULL);
        return;
    }
    g_message("connected to the GNSS HAL");
    get_psds();
    update_running();
}

/* running while wanted: by a client, or the test */
static void
update_running(void)
{
    const gboolean wanted = self.once || self.clients != NULL;
    if (!self.client || wanted == self.running)
        return;
    if (wanted) {
        self.running = start_gnss();
        g_message(self.running ? "GNSS started" : "GNSS did not start");
    } else {
        call_simple(GNSS_STOP_SV_STATUS, "stopSvStatus");
        call_simple(GNSS_STOP, "stop");
        self.running = FALSE;
        self.satellites = 0;
        g_message("GNSS stopped");
    }
}

/* --- the socket --------------------------------------------------------------------------- */

static gboolean
on_client_input(GPollableInputStream *in, gpointer conn)
{
    char buf[256];
    const gssize n = g_pollable_input_stream_read_nonblocking(in, buf, sizeof buf, NULL, NULL);
    if (n > 0 || n == -1)   /* (what a client says is not listened to) */
        return G_SOURCE_CONTINUE;
    /* end of stream: gone */
    GList *l = g_list_find(self.clients, conn);
    if (l) {
        self.clients = g_list_delete_link(self.clients, l);
        g_object_unref(conn);
    }
    update_running();
    return G_SOURCE_REMOVE;
}

static gboolean
on_incoming(GSocketService *service, GSocketConnection *conn, GObject *source, gpointer data)
{
    self.clients = g_list_prepend(self.clients, g_object_ref(conn));
    GPollableInputStream *in = G_POLLABLE_INPUT_STREAM(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
    GSource *s = g_pollable_input_stream_create_source(in, NULL);
    g_source_set_callback(s, (GSourceFunc)(void (*)(void))on_client_input, conn, NULL);
    g_source_attach(s, NULL);
    g_source_unref(s);
    update_running();
    return TRUE;
}

static gboolean
on_signal(gpointer data)
{
    g_main_loop_quit(self.loop);
    return G_SOURCE_REMOVE;
}

static gboolean
on_timeout(gpointer data)
{
    fprintf(stderr, "no fix in time\n");
    g_main_loop_quit(self.loop);
    return G_SOURCE_REMOVE;
}

int
main(int argc, char **argv)
{
    const char *path = DEFAULT_SOCKET;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--once"))
            self.once = TRUE;
        else if (!strcmp(argv[i], "--socket") && i + 1 < argc)
            path = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--socket PATH] | --once\n", argv[0]);
            return 2;
        }
    }
    /* $STATE_DIRECTORY: systemd's StateDirectory= */
    const char *state = g_getenv("STATE_DIRECTORY");
    self.last_path = g_build_filename(state ? state : g_get_user_state_dir(), "mobarchy-location.last", NULL);
    self.loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal, NULL);
    g_unix_signal_add(SIGINT, on_signal, NULL);

    if (self.once) {
        g_timeout_add_seconds(180, on_timeout, NULL);
    } else {
        GError *error = NULL;
        unlink(path);
        GSocketAddress *addr = g_unix_socket_address_new(path);
        self.service = g_socket_service_new();
        if (!g_socket_listener_add_address(G_SOCKET_LISTENER(self.service), addr, G_SOCKET_TYPE_STREAM,
                                           G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL, &error)) {
            fprintf(stderr, "cannot listen on %s: %s\n", path, error->message);
            return 1;
        }
        g_object_unref(addr);
        chmod(path, 0666);
        g_signal_connect(self.service, "incoming", G_CALLBACK(on_incoming), NULL);
        g_socket_service_start(self.service);
    }

    connect_gnss();
    g_main_loop_run(self.loop);

    if (self.running) {
        call_simple(GNSS_STOP_SV_STATUS, "stopSvStatus");
        call_simple(GNSS_STOP, "stop");
    }
    if (!self.once)
        unlink(path);
    return 0;
}
