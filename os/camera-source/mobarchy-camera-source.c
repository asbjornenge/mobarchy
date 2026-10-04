/*
 * mobarchy-camera-source: the cameras on the Linux side (ADR 0018). It is the camera
 * bridge's client (mobarchy-camerad in the Android container; protocol in
 * os/android-camera/protocol.h) and offers
 *   - a PipeWire video source per camera ("Back camera", "Front camera"), which streams
 *     while something reads it (the camera app, a browser through the camera portal, ...);
 *   - org.mobarchy.Camera on the session bus: the cameras, and photos (the HAL's JPEGs,
 *     saved in ~/Pictures/Camera).
 * One camera streams at a time (the bridge serves one session).
 *
 * It also tells the bridge the device's state, for the cameras the HAL picks: folded (a lid
 * switch, SW_LID, closed: a foldable's hinge sensor) or not. On a foldable, the front
 * camera is then the outer one when folded and the inner one when open, as on Android.
 */
#include "../android-camera/protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define BRIDGE "/var/lib/mobarchy/android/data/mobarchy/camera"
#define MAX_CAMERAS 8
#define RETRY_SECONDS 5
/* A stream that stops and starts again within this keeps the camera's session: PipeWire
 * pauses streams briefly while it sets them up, and each new session costs the HAL a
 * reconfiguration of its whole pipeline, which is slow. */
#define STOP_DELAY_MS 1500

#define BUS_NAME "org.mobarchy.Camera"
#define BUS_PATH "/org/mobarchy/Camera"
static const char introspection[] =
    "<node>"
    "  <interface name='org.mobarchy.Camera'>"
    "    <!-- the cameras: id, facing (back, front, external), orientation (degrees),"
    "         largest photo width and height -->"
    "    <method name='ListCameras'>"
    "      <arg type='a(ssuuu)' name='cameras' direction='out'/>"
    "    </method>"
    "    <!-- takes a photo (0 x 0: the default size) with the device turned rotation"
    "         degrees clockwise from its natural orientation (for the photo's own"
    "         orientation); returns the JPEG's path -->"
    "    <method name='TakePhoto'>"
    "      <arg type='s' name='camera' direction='in'/>"
    "      <arg type='u' name='width' direction='in'/>"
    "      <arg type='u' name='height' direction='in'/>"
    "      <arg type='u' name='rotation' direction='in'/>"
    "      <arg type='s' name='path' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

struct camera {
    struct camera_message info;     /* the bridge's CAMERA message */
    struct pw_stream *stream;
    struct spa_hook listener;
    struct spa_video_info_raw format;
    gboolean streaming;
};

static struct {
    GMainLoop *main;
    struct pw_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    int bridge;
    guint bridge_watch;
    struct camera cameras[MAX_CAMERAS];
    int n_cameras;
    gboolean listed;
    /* the stream the bridge serves */
    struct camera *active;
    uint8_t *maps[CAMERA_MAX_BUFFERS];
    uint32_t size, width, height;
    int pending;                    /* a frame waiting for the stream's process (index) */
    guint stop_timer;               /* the active stream paused: stop the bridge's later */
    uint32_t asked_width, asked_height;    /* what the bridge was asked to stream */
    /* photos: one at a time */
    GDBusMethodInvocation *photo;
    char photo_camera[CAMERA_ID_SIZE];
    GDBusConnection *bus;
    int lid;                        /* the lid switch (SW_LID) device, or -1 */
    int folded;
} app = { .bridge = -1, .pending = -1, .lid = -1 };

/* --- the bridge ----------------------------------------------------------------------- */

static int
bridge_send(uint32_t type, const char *camera, uint32_t w, uint32_t h, uint32_t index)
{
    struct camera_message m = { .type = type, .width = w, .height = h, .index = index };

    if (app.bridge < 0)
        return -1;
    if (camera)
        g_strlcpy(m.camera, camera, sizeof m.camera);
    return send(app.bridge, &m, sizeof m, MSG_NOSIGNAL) == sizeof m ? 0 : -1;
}

static void
unmap_buffers(void)
{
    for (int i = 0; i < CAMERA_MAX_BUFFERS; i++) {
        if (app.maps[i])
            munmap(app.maps[i], app.size);
        app.maps[i] = NULL;
    }
    app.pending = -1;
}

static void photo_failed(const char *why);
static void streams_create(void);
static gboolean bridge_connect(gpointer unused);

static void
bridge_lost(void)
{
    g_message("camera bridge gone; trying again every %d s", RETRY_SECONDS);
    if (app.bridge_watch)
        g_source_remove(app.bridge_watch);
    app.bridge_watch = 0;
    close(app.bridge);
    app.bridge = -1;
    unmap_buffers();
    app.active = NULL;
    if (app.photo)
        photo_failed("the camera bridge went away");
    g_timeout_add_seconds(RETRY_SECONDS, bridge_connect, NULL);
}

static void
photo_done(int fd, uint32_t size)
{
    const char *pictures = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    g_autofree char *fallback = g_build_filename(g_get_home_dir(), "Pictures", NULL);
    g_autofree char *dir = g_build_filename(pictures ?: fallback, "Camera", NULL);
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *name = g_date_time_format(now, "IMG_%Y%m%d_%H%M%S.jpg");
    g_autofree char *path = g_build_filename(dir, name, NULL);
    g_autofree char *data = g_malloc(size);
    g_autoptr(GError) error = NULL;

    g_mkdir_with_parents(dir, 0755);
    if (pread(fd, data, size, 0) != (ssize_t)size ||
        !g_file_set_contents(path, data, size, &error)) {
        photo_failed(error ? error->message : "can't read the photo");
        return;
    }
    g_message("photo: %s (%u bytes)", path, size);
    g_dbus_method_invocation_return_value(app.photo, g_variant_new("(s)", path));
    app.photo = NULL;
}

static void
photo_failed(const char *why)
{
    g_dbus_method_invocation_return_error(app.photo, G_IO_ERROR, G_IO_ERROR_FAILED,
                                          "No photo: %s", why);
    app.photo = NULL;
}

static gboolean
on_bridge(gint fd, GIOCondition cond, gpointer unused)
{
    struct camera_message m;
    char ctrl[CMSG_SPACE(sizeof(int) * CAMERA_MAX_BUFFERS)];
    struct iovec iov = { .iov_base = &m, .iov_len = sizeof m };
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctrl,
                          .msg_controllen = sizeof ctrl };
    int fds[CAMERA_MAX_BUFFERS], nfds = 0;

    ssize_t n = recvmsg(fd, &msg, MSG_CMSG_CLOEXEC);
    if (n <= 0) {
        bridge_lost();
        return G_SOURCE_REMOVE;
    }
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            nfds = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            memcpy(fds, CMSG_DATA(c), nfds * sizeof(int));
        }
    if (n != sizeof m)
        goto out;

    switch (m.type) {
    case MSG_CAMERA:
        if (app.n_cameras < MAX_CAMERAS)
            app.cameras[app.n_cameras++].info = m;
        break;
    case MSG_END:
        app.listed = TRUE;
        streams_create();
        break;
    case MSG_BUFFERS:
        unmap_buffers();
        app.size = m.size, app.width = m.width, app.height = m.height;
        for (int i = 0; i < nfds && i < CAMERA_MAX_BUFFERS; i++) {
            app.maps[i] = mmap(NULL, m.size, PROT_READ, MAP_SHARED, fds[i], 0);
            if (app.maps[i] == MAP_FAILED)
                app.maps[i] = NULL;
        }
        break;
    case MSG_FRAME:
        if (!app.active || !app.active->streaming || app.pending >= 0) {
            /* not wanted (or paused), or still busy: give back */
            bridge_send(MSG_RELEASE, NULL, 0, 0, m.index);
            break;
        }
        app.pending = m.index;
        pw_stream_trigger_process(app.active->stream);
        break;
    case MSG_JPEG:
        if (app.photo && nfds)
            photo_done(fds[0], m.size);
        break;
    case MSG_ERROR:
        g_warning("camera bridge: error %d", m.code);
        app.asked_width = app.asked_height = 0;     /* (a new STREAM, even of the same, retries) */
        if (app.photo)
            photo_failed("the camera refused");
        break;
    }
out:
    for (int i = 0; i < nfds; i++)
        close(fds[i]);
    return G_SOURCE_CONTINUE;
}

static void
send_device_state(void)
{
    struct camera_message m = { .type = MSG_DEVICE_STATE,
                                .code = app.folded ? DEVICE_STATE_FOLDED : DEVICE_STATE_NORMAL };

    if (app.bridge >= 0)
        send(app.bridge, &m, sizeof m, MSG_NOSIGNAL);
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
    app.bridge_watch = g_unix_fd_add(fd, G_IO_IN | G_IO_HUP | G_IO_ERR, on_bridge, NULL);
    g_message("connected to the camera bridge");
    send_device_state();
    if (!app.listed) {
        app.n_cameras = 0;
        bridge_send(MSG_LIST, NULL, 0, 0, 0);
    }
    /* a stream that was running goes on */
    for (int i = 0; i < app.n_cameras; i++)
        if (app.cameras[i].streaming) {
            app.active = &app.cameras[i];
            bridge_send(MSG_STREAM, app.active->info.camera, app.active->format.size.width,
                        app.active->format.size.height, 0);
        }
    return G_SOURCE_REMOVE;
}

/* --- PipeWire ------------------------------------------------------------------------- */

static void
on_process(void *data)
{
    struct camera *c = data;
    struct pw_buffer *b;
    int index = app.pending;

    if (c != app.active || index < 0)
        return;
    app.pending = -1;
    if ((b = pw_stream_dequeue_buffer(c->stream))) {
        struct spa_data *d = &b->buffer->datas[0];
        uint32_t size = SPA_MIN(app.size, d->maxsize);
        if (d->data && app.maps[index])
            memcpy(d->data, app.maps[index], size);
        d->chunk->offset = 0;
        d->chunk->size = size;
        d->chunk->stride = app.width;
        pw_stream_queue_buffer(c->stream, b);
    }
    bridge_send(MSG_RELEASE, NULL, 0, 0, index);
}

static void
start_streaming(struct camera *c)
{
    if (app.stop_timer) {
        g_source_remove(app.stop_timer);
        app.stop_timer = 0;
    }
    if (app.active == c && app.asked_width == c->format.size.width
        && app.asked_height == c->format.size.height) {
        c->streaming = TRUE;                /* the bridge's session goes on as it was */
        return;
    }
    if (app.active && app.active != c)
        app.active->streaming = FALSE;      /* one camera at a time: the newest wins */
    app.active = c;
    c->streaming = TRUE;
    app.asked_width = c->format.size.width, app.asked_height = c->format.size.height;
    bridge_send(MSG_STREAM, c->info.camera, c->format.size.width, c->format.size.height, 0);
    g_message("camera %s: streaming %ux%u", c->info.camera, c->format.size.width,
              c->format.size.height);
}

static gboolean
stop_streaming(gpointer unused)
{
    struct camera *c = app.active;

    app.stop_timer = 0;
    if (c && !c->streaming) {
        app.active = NULL;
        unmap_buffers();
        bridge_send(MSG_STOP, NULL, 0, 0, 0);
        g_message("camera %s: stopped", c->info.camera);
    }
    return G_SOURCE_REMOVE;
}

static void
on_state_changed(void *data, enum pw_stream_state old, enum pw_stream_state state, const char *error)
{
    struct camera *c = data;

    if (state == PW_STREAM_STATE_STREAMING) {
        start_streaming(c);
    } else if (c->streaming) {
        c->streaming = FALSE;
        if (app.active == c && !app.stop_timer)
            app.stop_timer = g_timeout_add(STOP_DELAY_MS, stop_streaming, NULL);
    }
}

static void
on_param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
    struct camera *c = data;
    uint8_t buf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    const struct spa_pod *params[1];

    if (!param || id != SPA_PARAM_Format)
        return;
    if (spa_format_video_raw_parse(param, &c->format) < 0)
        return;
    uint32_t w = c->format.size.width, h = c->format.size.height;
    params[0] = spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_size, SPA_POD_Int(w * h * 3 / 2),
        SPA_PARAM_BUFFERS_stride, SPA_POD_Int(w));
    pw_stream_update_params(c->stream, params, 1);
    if (c->streaming)                       /* a new size while streaming */
        start_streaming(c);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
};

static void
streams_create(void)
{
    for (int i = 0; i < app.n_cameras; i++) {
        struct camera *c = &app.cameras[i];
        const char *desc = c->info.facing == FACING_FRONT ? "Front camera"
                         : c->info.facing == FACING_BACK ? "Back camera" : "Camera";
        g_autofree char *name = g_strdup_printf("mobarchy-camera-%s", c->info.camera);
        uint8_t buf[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        const struct spa_pod *params[1];

        if (c->stream)
            continue;
        c->stream = pw_stream_new(app.core, name, pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Video",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Camera",
            PW_KEY_MEDIA_CLASS, "Video/Source",
            PW_KEY_NODE_DESCRIPTION, desc,
            PW_KEY_NODE_NAME, name,
            "api.mobarchy.camera.id", c->info.camera,
            NULL));
        pw_stream_add_listener(c->stream, &c->listener, &stream_events, c);
        params[0] = spa_pod_builder_add_object(&b,
            SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_I420),
            SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_ENUM_Rectangle(4,  /* the default, then the choices */
                &SPA_RECTANGLE(1280, 720), &SPA_RECTANGLE(1920, 1080),
                &SPA_RECTANGLE(1280, 720), &SPA_RECTANGLE(640, 480)),
            SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(30, 1)));
        pw_stream_connect(c->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                          PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_MAP_BUFFERS, params, 1);
        g_message("camera %s: PipeWire source \"%s\"", c->info.camera, desc);
    }
}

static gboolean
on_pipewire(gint fd, GIOCondition cond, gpointer loop)
{
    pw_loop_iterate(loop, 0);
    return G_SOURCE_CONTINUE;
}

/* --- D-Bus ---------------------------------------------------------------------------- */

static void
on_method(GDBusConnection *bus, const char *sender, const char *path, const char *iface,
          const char *method, GVariant *args, GDBusMethodInvocation *inv, gpointer unused)
{
    if (!g_strcmp0(method, "ListCameras")) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a(ssuuu)"));
        for (int i = 0; i < app.n_cameras; i++) {
            struct camera_message *m = &app.cameras[i].info;
            g_variant_builder_add(&b, "(ssuuu)", m->camera,
                                  m->facing == FACING_FRONT ? "front" : m->facing == FACING_BACK
                                  ? "back" : "external", m->orientation, m->max_width, m->max_height);
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a(ssuuu))", &b));
    } else if (!g_strcmp0(method, "TakePhoto")) {
        const char *camera;
        guint32 w, h, rotation;
        g_variant_get(args, "(&suuu)", &camera, &w, &h, &rotation);
        if (app.photo) {
            g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_BUSY,
                                                  "A photo is being taken");
        } else if (app.bridge < 0) {
            g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                                                  "The camera isn't up (the Android container)");
        } else {
            app.photo = inv;
            g_strlcpy(app.photo_camera, camera, sizeof app.photo_camera);
            struct camera_message m = { .type = MSG_STILL, .width = w, .height = h,
                                        .orientation = rotation % 360 };
            g_strlcpy(m.camera, camera, sizeof m.camera);
            send(app.bridge, &m, sizeof m, MSG_NOSIGNAL);
        }
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = on_method };

static void
on_bus(GDBusConnection *bus, const char *name, gpointer unused)
{
    g_autoptr(GDBusNodeInfo) node = g_dbus_node_info_new_for_xml(introspection, NULL);
    app.bus = bus;
    g_dbus_connection_register_object(bus, BUS_PATH, node->interfaces[0], &vtable, NULL, NULL, NULL);
}

/* --- the lid switch (a foldable's hinge) ---------------------------------------------- */

#define BIT(bits, n) ((bits)[(n) / 8] & (1 << ((n) % 8)))

static gboolean
on_lid(gint fd, GIOCondition cond, gpointer unused)
{
    struct input_event ev[16];
    ssize_t n;
    int folded = app.folded;

    while ((n = read(fd, ev, sizeof ev)) > 0)
        for (size_t i = 0; i < n / sizeof *ev; i++)
            if (ev[i].type == EV_SW && ev[i].code == SW_LID)
                folded = ev[i].value != 0;
    if (n < 0 && errno != EAGAIN) {
        g_warning("lid switch: %s", g_strerror(errno));
        close(fd);
        app.lid = -1;
        return G_SOURCE_REMOVE;
    }
    if (folded != app.folded) {
        app.folded = folded;
        g_message("%s", folded ? "folded" : "unfolded");
        send_device_state();
    }
    return G_SOURCE_CONTINUE;
}

/* the first input device with a lid switch; none: the device doesn't fold */
static void
lid_open(void)
{
    glob_t g;

    if (glob("/dev/input/event*", 0, NULL, &g))
        return;
    for (size_t i = 0; i < g.gl_pathc && app.lid < 0; i++) {
        uint8_t types[EV_MAX / 8 + 1] = { 0 }, switches[SW_MAX / 8 + 1] = { 0 };
        int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (ioctl(fd, EVIOCGBIT(0, sizeof types), types) >= 0 && BIT(types, EV_SW)
            && ioctl(fd, EVIOCGBIT(EV_SW, sizeof switches), switches) >= 0
            && BIT(switches, SW_LID)) {
            uint8_t state[SW_MAX / 8 + 1] = { 0 };
            ioctl(fd, EVIOCGSW(sizeof state), state);
            app.lid = fd;
            app.folded = BIT(state, SW_LID) != 0;
            g_unix_fd_add(fd, G_IO_IN, on_lid, NULL);
            g_message("lid switch %s: %s", g.gl_pathv[i], app.folded ? "folded" : "unfolded");
        } else {
            close(fd);
        }
    }
    globfree(&g);
}

int
main(int argc, char **argv)
{
    pw_init(&argc, &argv);
    app.main = g_main_loop_new(NULL, FALSE);
    app.loop = pw_loop_new(NULL);
    app.context = pw_context_new(app.loop, NULL, 0);
    app.core = pw_context_connect(app.context, NULL, 0);
    if (!app.core) {
        g_printerr("can't connect to PipeWire: %s\n", g_strerror(errno));
        return 1;
    }
    pw_loop_enter(app.loop);
    g_unix_fd_add(pw_loop_get_fd(app.loop), G_IO_IN, on_pipewire, app.loop);

    lid_open();
    g_bus_own_name(G_BUS_TYPE_SESSION, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, on_bus, NULL,
                   NULL, NULL, NULL);
    if (bridge_connect(NULL) == G_SOURCE_CONTINUE)
        g_timeout_add_seconds(RETRY_SECONDS, bridge_connect, NULL);
    g_main_loop_run(app.main);
    return 0;
}
