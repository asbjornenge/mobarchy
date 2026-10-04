/*
 * mobarchy-camerad: the camera bridge in the Android container (ADR 0018). It opens the
 * cameras through cameraserver (the NDK camera API, as an app would) and serves them to the
 * Linux side on a Unix socket: frames in shared memory, stills as the HAL's JPEGs.
 * The protocol is in protocol.h.
 */
#define _GNU_SOURCE     /* memfd_create */
#include "../protocol.h"

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

#include <android/binder_ibinder.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "camerad", __VA_ARGS__)
#define ERR(...) __android_log_print(ANDROID_LOG_ERROR, "camerad", __VA_ARGS__)

#define PREVIEW_W 640           /* the hidden preview a still converges on */
#define PREVIEW_H 480
#define SETTLE_FRAMES 20        /* frames for exposure and focus to settle before a still */
#define LIST_WAIT 30            /* seconds a LIST waits for the HAL's cameras */
#define STILL_W 1920            /* a still's default size (and the stream's JPEG output) */
#define STILL_H 1080

static ACameraManager *manager;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int client = -1;
/* the stream the client asked for (camera "" if none): kept up through camera failures */
static struct { char camera[CAMERA_ID_SIZE]; int width, height; } wanted;

static void do_device_state(const struct camera_message *req);
static void do_torch(const struct camera_message *req);
static int torch_on;
static void send_torch_state(void);

/* the open camera and its session */
static struct {
    char id[CAMERA_ID_SIZE];
    ACameraDevice *device;
    ACameraCaptureSession *session;
    ACaptureSessionOutputContainer *outputs;
    AImageReader *yuv, *jpeg;
    ACameraOutputTarget *yuv_target, *jpeg_target;
    ACaptureSessionOutput *yuv_output, *jpeg_output;
    ACaptureRequest *repeating;
    int yuv_w, yuv_h, jpeg_w, jpeg_h;
    int streaming;              /* frames go to the client (else: only settling) */
    int frames;                 /* frames since the session started */
    int failed;
    int idle;                   /* the session has no requests in flight (onReady) */
    int closed;                 /* the session closed (onClosed) */
    /* the client's buffers */
    int fds[CAMERA_MAX_BUFFERS];
    uint8_t *maps[CAMERA_MAX_BUFFERS];
    uint32_t size, busy;        /* busy: bit per buffer the client holds */
    /* a still in flight */
    int jpeg_fd;
    uint32_t jpeg_size;
} cam = { .jpeg_fd = -1 };

/* --- messages ------------------------------------------------------------------------- */

static int
send_msg(const struct camera_message *m, const int *fds, int nfds)
{
    struct iovec iov = { .iov_base = (void *)m, .iov_len = sizeof *m };
    char ctrl[CMSG_SPACE(sizeof(int) * CAMERA_MAX_BUFFERS)];
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };

    if (client < 0)
        return -1;
    if (nfds) {
        memset(ctrl, 0, sizeof ctrl);
        msg.msg_control = ctrl;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfds);
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * nfds);
        memcpy(CMSG_DATA(c), fds, sizeof(int) * nfds);
    }
    return sendmsg(client, &msg, MSG_NOSIGNAL) < 0 ? -1 : 0;
}

static void
send_error(int code)
{
    struct camera_message m = { .type = MSG_ERROR, .code = code };
    send_msg(&m, NULL, 0);
}

/* --- the camera ----------------------------------------------------------------------- */

static void on_disconnected(void *ctx, ACameraDevice *d) { LOG("camera disconnected"); }

static void
on_error(void *ctx, ACameraDevice *d, int err)
{
    ERR("camera error %d", err);
    pthread_mutex_lock(&lock);
    cam.failed = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

static void session_noop(void *ctx, ACameraCaptureSession *s) {}

static void
session_flag(int *flag)
{
    pthread_mutex_lock(&lock);
    *flag = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}
static void on_ready(void *ctx, ACameraCaptureSession *s) { session_flag(&cam.idle); }
static void on_closed(void *ctx, ACameraCaptureSession *s) { session_flag(&cam.closed); }

/* waits (with the lock held) until *flag or the deadline; false on timeout */
static int
wait_flag(const int *flag, int seconds)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    while (!*flag)
        if (pthread_cond_timedwait(&cond, &lock, &ts))
            break;
    return *flag;
}

/* YUV_420_888 (any strides) into I420 */
static void
copy_i420(AImage *img, uint8_t *out, int w, int h)
{
    for (int p = 0; p < 3; p++) {
        uint8_t *data;
        int len, row, pixel = 1;
        int pw = p ? w / 2 : w, ph = p ? h / 2 : h;

        if (AImage_getPlaneData(img, p, &data, &len) != AMEDIA_OK)
            return;
        AImage_getPlaneRowStride(img, p, &row);
        if (p)
            AImage_getPlanePixelStride(img, p, &pixel);
        for (int y = 0; y < ph; y++) {
            const uint8_t *src = data + (size_t)y * row;
            if (pixel == 1) {
                memcpy(out, src, pw);
            } else {
                for (int x = 0; x < pw; x++)
                    out[x] = src[x * pixel];
            }
            out += pw;
        }
    }
}

static void
on_yuv(void *ctx, AImageReader *reader)
{
    AImage *img = NULL;
    int64_t ts = 0;

    if (AImageReader_acquireLatestImage(reader, &img) != AMEDIA_OK || !img)
        return;
    AImage_getTimestamp(img, &ts);
    pthread_mutex_lock(&lock);
    cam.frames++;
    pthread_cond_broadcast(&cond);
    if (cam.streaming) {
        int i;
        for (i = 0; i < CAMERA_MAX_BUFFERS && (cam.busy & (1u << i)); i++)
            ;
        if (i < CAMERA_MAX_BUFFERS && cam.maps[i]) {     /* else: the client is behind */
            copy_i420(img, cam.maps[i], cam.yuv_w, cam.yuv_h);
            cam.busy |= 1u << i;
            struct camera_message m = { .type = MSG_FRAME, .index = i, .timestamp = ts };
            send_msg(&m, NULL, 0);
        }
    }
    pthread_mutex_unlock(&lock);
    AImage_delete(img);
}

static void
on_jpeg(void *ctx, AImageReader *reader)
{
    AImage *img = NULL;
    uint8_t *data;
    int len;

    if (AImageReader_acquireNextImage(reader, &img) != AMEDIA_OK || !img)
        return;
    if (AImage_getPlaneData(img, 0, &data, &len) == AMEDIA_OK) {
        /* the JPEG ends at its EOI marker; the buffer is larger */
        int end = len;
        for (int i = len - 2; i > 0; i--)
            if (data[i] == 0xff && data[i + 1] == 0xd9) {
                end = i + 2;
                break;
            }
        int fd = memfd_create("camera-jpeg", MFD_CLOEXEC);
        if (fd >= 0 && write(fd, data, end) == end) {
            pthread_mutex_lock(&lock);
            cam.jpeg_fd = fd;
            cam.jpeg_size = end;
            pthread_cond_broadcast(&cond);
            pthread_mutex_unlock(&lock);
        } else if (fd >= 0) {
            close(fd);
        }
    }
    AImage_delete(img);
}

/* Requests without face results: the client has no use for them. (The HAL's own face
 * detection, for exposure and focus, runs regardless.) */
static void
no_faces(ACaptureRequest *req)
{
    uint8_t off = ACAMERA_STATISTICS_FACE_DETECT_MODE_OFF;
    ACaptureRequest_setEntry_u8(req, ACAMERA_STATISTICS_FACE_DETECT_MODE, 1, &off);
}

/* The JPEG's orientation (EXIF): how far to turn it clockwise to upright, from the sensor's
 * mounting and how far the device is turned (clockwise from its natural orientation), as
 * Android's camera API documents for ACAMERA_JPEG_ORIENTATION. */
static int32_t
jpeg_orientation(const char *id, int device_rotation)
{
    ACameraMetadata *md = NULL;
    ACameraMetadata_const_entry e;
    int32_t sensor = 0;
    int front = 0;

    if (ACameraManager_getCameraCharacteristics(manager, id, &md) != ACAMERA_OK)
        return 0;
    if (ACameraMetadata_getConstEntry(md, ACAMERA_SENSOR_ORIENTATION, &e) == ACAMERA_OK)
        sensor = e.data.i32[0];
    if (ACameraMetadata_getConstEntry(md, ACAMERA_LENS_FACING, &e) == ACAMERA_OK)
        front = e.data.u8[0] == ACAMERA_LENS_FACING_FRONT;
    ACameraMetadata_free(md);
    device_rotation = (device_rotation + 45) / 90 * 90 % 360;
    /* a front camera's image is mirrored: the device's turn counts the other way */
    return (front ? sensor - device_rotation + 360 : sensor + device_rotation) % 360;
}

static AImageReader *
new_reader(int w, int h, int format, int max, AImageReader_ImageCallback cb,
           ACameraOutputTarget **target, ACaptureSessionOutput **output)
{
    AImageReader *r;
    ANativeWindow *win;
    AImageReader_ImageListener l = { .context = NULL, .onImageAvailable = cb };

    if (AImageReader_new(w, h, format, max, &r) != AMEDIA_OK)
        return NULL;
    AImageReader_setImageListener(r, &l);
    AImageReader_getWindow(r, &win);
    ACameraOutputTarget_create(win, target);
    ACaptureSessionOutput_create(win, output);
    return r;
}

static void
free_buffers(void)
{
    for (int i = 0; i < CAMERA_MAX_BUFFERS; i++) {
        if (cam.maps[i])
            munmap(cam.maps[i], cam.size);
        if (cam.fds[i] > 0)
            close(cam.fds[i]);
        cam.maps[i] = NULL;
        cam.fds[i] = -1;
    }
    cam.busy = 0;
}

/* Closes the session (and the camera, if all is true). Called without the lock. */
static void
close_session(int all)
{
    /* A clean stop: no new requests, wait until the ones in flight are done, close, and
     * wait until the session is closed, before the next one is configured. */
    if (cam.session) {
        pthread_mutex_lock(&lock);
        cam.idle = 0;
        pthread_mutex_unlock(&lock);
        if (!cam.failed && ACameraCaptureSession_stopRepeating(cam.session) == ACAMERA_OK) {
            pthread_mutex_lock(&lock);
            if (!wait_flag(&cam.idle, 3))
                ERR("session not idle after 3 s");
            pthread_mutex_unlock(&lock);
        }
        pthread_mutex_lock(&lock);
        cam.closed = 0;
        pthread_mutex_unlock(&lock);
        ACameraCaptureSession_close(cam.session);
        pthread_mutex_lock(&lock);
        if (!cam.failed && !wait_flag(&cam.closed, 3))
            ERR("session not closed after 3 s");
        pthread_mutex_unlock(&lock);
        cam.session = NULL;
    }
    if (cam.repeating) {
        ACaptureRequest_free(cam.repeating);
        cam.repeating = NULL;
    }
    if (cam.outputs) {
        ACaptureSessionOutputContainer_free(cam.outputs);
        cam.outputs = NULL;
    }
    if (cam.yuv) {
        AImageReader_delete(cam.yuv);
        ACameraOutputTarget_free(cam.yuv_target);
        ACaptureSessionOutput_free(cam.yuv_output);
        cam.yuv = NULL;
    }
    if (cam.jpeg) {
        AImageReader_delete(cam.jpeg);
        ACameraOutputTarget_free(cam.jpeg_target);
        ACaptureSessionOutput_free(cam.jpeg_output);
        cam.jpeg = NULL;
    }
    pthread_mutex_lock(&lock);
    cam.streaming = 0;
    free_buffers();
    pthread_mutex_unlock(&lock);
    if (all && cam.device) {
        ACameraDevice_close(cam.device);
        cam.device = NULL;
        cam.id[0] = 0;
    }
}

/* A session on camera id with a YUV stream (w x h) and, if jw, a JPEG output (jw x jh). */
static int
open_session(const char *id, int w, int h, int jw, int jh)
{
    camera_status_t st;

    /* a camera that failed (the HAL crashed, say) is no use: it has to be opened again */
    if (cam.device && (cam.failed || strcmp(cam.id, id))) {
        close_session(1);
    } else {
        close_session(0);
    }
    cam.failed = 0;
    if (!cam.device && torch_on) {
        torch_on = 0;                   /* opening a camera turns its torch off */
        send_torch_state();
    }
    if (!cam.device) {
        ACameraDevice_StateCallbacks cb = { .onDisconnected = on_disconnected, .onError = on_error };
        /* after a crash the provider restarts, and the camera is back a few seconds later */
        for (int tries = 0; tries < 10; tries++) {
            st = ACameraManager_openCamera(manager, id, &cb, &cam.device);
            if (st == ACAMERA_OK || (st != ACAMERA_ERROR_CAMERA_DISCONNECTED
                                     && st != ACAMERA_ERROR_INVALID_PARAMETER
                                     && st != ACAMERA_ERROR_CAMERA_IN_USE))
                break;
            sleep(1);
        }
        if (st != ACAMERA_OK) {
            ERR("openCamera(%s): %d", id, st);
            cam.device = NULL;
            return st;
        }
        snprintf(cam.id, sizeof cam.id, "%s", id);
    }
    cam.yuv_w = w, cam.yuv_h = h, cam.jpeg_w = jw, cam.jpeg_h = jh;
    cam.yuv = new_reader(w, h, AIMAGE_FORMAT_YUV_420_888, 4, on_yuv, &cam.yuv_target, &cam.yuv_output);
    if (jw)
        cam.jpeg = new_reader(jw, jh, AIMAGE_FORMAT_JPEG, 2, on_jpeg, &cam.jpeg_target, &cam.jpeg_output);
    if (!cam.yuv || (jw && !cam.jpeg))
        return -ENOMEM;
    ACaptureSessionOutputContainer_create(&cam.outputs);
    ACaptureSessionOutputContainer_add(cam.outputs, cam.yuv_output);
    if (cam.jpeg)
        ACaptureSessionOutputContainer_add(cam.outputs, cam.jpeg_output);
    ACameraCaptureSession_stateCallbacks scb = { .onClosed = on_closed, .onReady = on_ready,
                                                 .onActive = session_noop };
    st = ACameraDevice_createCaptureSession(cam.device, cam.outputs, &scb, &cam.session);
    if (st != ACAMERA_OK) {
        ERR("createCaptureSession: %d", st);
        cam.session = NULL;
        return st;
    }
    ACameraDevice_createCaptureRequest(cam.device, TEMPLATE_PREVIEW, &cam.repeating);
    ACaptureRequest_addTarget(cam.repeating, cam.yuv_target);
    no_faces(cam.repeating);
    cam.frames = 0;
    st = ACameraCaptureSession_setRepeatingRequest(cam.session, NULL, 1, &cam.repeating, NULL);
    if (st != ACAMERA_OK)
        ERR("setRepeatingRequest: %d", st);
    LOG("camera %s: session %dx%d, stills %dx%d", id, w, h, jw, jh);
    return st;
}

/* waits (with the lock held) until cond() or the deadline; false on timeout or failure */
static int
wait_until(int (*done)(void), int seconds)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    while (!done() && !cam.failed)
        if (pthread_cond_timedwait(&cond, &lock, &ts))
            break;
    return done() && !cam.failed;
}

static int settled(void) { return cam.frames >= SETTLE_FRAMES; }
static int jpeg_ready(void) { return cam.jpeg_fd >= 0; }

/* --- requests ------------------------------------------------------------------------- */

static void
do_list(void)
{
    ACameraIdList *ids = NULL;

    /* the camera HAL registers its cameras a while after cameraserver is up: a client that
     * asks early waits for them (up to LIST_WAIT seconds) rather than getting none */
    for (int waited = 0;; waited++) {
        if (ACameraManager_getCameraIdList(manager, &ids) != ACAMERA_OK) {
            send_error(-EIO);
            return;
        }
        if (ids->numCameras > 0 || waited >= LIST_WAIT)
            break;
        ACameraManager_deleteCameraIdList(ids);
        sleep(1);
    }
    for (int i = 0; i < ids->numCameras; i++) {
        ACameraMetadata *md = NULL;
        ACameraMetadata_const_entry e;
        struct camera_message m = { .type = MSG_CAMERA };

        snprintf(m.camera, sizeof m.camera, "%s", ids->cameraIds[i]);
        if (ACameraManager_getCameraCharacteristics(manager, ids->cameraIds[i], &md) != ACAMERA_OK)
            continue;
        if (ACameraMetadata_getConstEntry(md, ACAMERA_LENS_FACING, &e) == ACAMERA_OK)
            m.facing = e.data.u8[0] == ACAMERA_LENS_FACING_FRONT ? FACING_FRONT
                     : e.data.u8[0] == ACAMERA_LENS_FACING_BACK ? FACING_BACK : FACING_EXTERNAL;
        if (ACameraMetadata_getConstEntry(md, ACAMERA_SENSOR_ORIENTATION, &e) == ACAMERA_OK)
            m.orientation = e.data.i32[0];
        if (ACameraMetadata_getConstEntry(md, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) == ACAMERA_OK)
            for (uint32_t j = 0; j + 3 < e.count; j += 4)
                if (e.data.i32[j + 3] == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
                    e.data.i32[j] == AIMAGE_FORMAT_JPEG &&
                    (uint32_t)(e.data.i32[j + 1] * e.data.i32[j + 2]) > m.max_width * m.max_height) {
                    m.max_width = e.data.i32[j + 1];
                    m.max_height = e.data.i32[j + 2];
                }
        ACameraMetadata_free(md);
        send_msg(&m, NULL, 0);
    }
    ACameraManager_deleteCameraIdList(ids);
    struct camera_message end = { .type = MSG_END };
    send_msg(&end, NULL, 0);
    send_torch_state();
}

/* the client's frame buffers for the session's YUV stream, sent to it (BUFFERS) */
static void
share_buffers(void)
{
    int w = cam.yuv_w, h = cam.yuv_h;
    uint32_t size = (uint32_t)w * h * 3 / 2;
    int fds[CAMERA_MAX_BUFFERS];

    pthread_mutex_lock(&lock);
    free_buffers();
    for (int i = 0; i < CAMERA_MAX_BUFFERS; i++) {
        fds[i] = cam.fds[i] = memfd_create("camera-frame", MFD_CLOEXEC);
        if (fds[i] < 0 || ftruncate(fds[i], size) < 0) {
            pthread_mutex_unlock(&lock);
            send_error(-errno);
            return;
        }
        cam.maps[i] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[i], 0);
    }
    cam.size = size;
    cam.busy = 0;
    struct camera_message m = { .type = MSG_BUFFERS, .width = w, .height = h,
                                .count = CAMERA_MAX_BUFFERS, .size = size };
    snprintf(m.camera, sizeof m.camera, "%s", cam.id);
    send_msg(&m, fds, CAMERA_MAX_BUFFERS);
    cam.streaming = 1;
    pthread_mutex_unlock(&lock);
}

static void
do_stream(const struct camera_message *req)
{
    /* the stream, and a JPEG output for stills: a still while streaming then needs no new
     * session (each one is a slow reconfiguration of the HAL's whole pipeline) */
    snprintf(wanted.camera, sizeof wanted.camera, "%s", req->camera);
    wanted.width = req->width & ~1, wanted.height = req->height & ~1;
    int st = open_session(req->camera, wanted.width, wanted.height, STILL_W, STILL_H);
    if (st)
        send_error(st);
    else
        share_buffers();
}

/* back to the stream there was before a still (or no session) */
static void
restore(int was_streaming, const char *id, int w, int h)
{
    if (!was_streaming) {
        close_session(0);
        return;
    }
    if (open_session(id, w, h, STILL_W, STILL_H) == 0)
        share_buffers();
}

static void
do_still(const struct camera_message *req)
{
    int st = 0;
    int w = req->width ? req->width : STILL_W, h = req->height ? req->height : STILL_H;
    int ok;

    /* From the stream's session, if it has a JPEG output of that size. If not, a session of
     * the still's own: a small preview to settle on, and the JPEG; the stream comes back
     * afterwards. */
    char stream_id[CAMERA_ID_SIZE];
    int was_streaming = cam.streaming, sw = cam.yuv_w, sh = cam.yuv_h;
    snprintf(stream_id, sizeof stream_id, "%s", cam.id);
    int own = !(cam.session && !cam.failed && !strcmp(cam.id, req->camera) && cam.jpeg
                && cam.jpeg_w == w && cam.jpeg_h == h);
    if (own) {
        st = open_session(req->camera, PREVIEW_W, PREVIEW_H, w, h);
        if (st) {
            send_error(st);
            restore(was_streaming, stream_id, sw, sh);
            return;
        }
    }
    pthread_mutex_lock(&lock);
    ok = wait_until(settled, 10);
    pthread_mutex_unlock(&lock);
    if (!ok) {
        send_error(-ETIMEDOUT);
        if (own)
            restore(was_streaming, stream_id, sw, sh);
        return;
    }
    ACaptureRequest *still;
    ACameraDevice_createCaptureRequest(cam.device, TEMPLATE_STILL_CAPTURE, &still);
    /* (the JPEG alone: the stream's frames go on through the repeating request) */
    ACaptureRequest_addTarget(still, cam.jpeg_target);
    no_faces(still);
    int32_t orientation = jpeg_orientation(req->camera, req->orientation);
    ACaptureRequest_setEntry_i32(still, ACAMERA_JPEG_ORIENTATION, 1, &orientation);
    st = ACameraCaptureSession_capture(cam.session, NULL, 1, &still, NULL);
    ACaptureRequest_free(still);
    if (st != ACAMERA_OK) {
        send_error(st);
        if (own)
            restore(was_streaming, stream_id, sw, sh);
        return;
    }
    pthread_mutex_lock(&lock);
    ok = wait_until(jpeg_ready, 10);
    int fd = cam.jpeg_fd;
    struct camera_message m = { .type = MSG_JPEG, .size = cam.jpeg_size };
    cam.jpeg_fd = -1;
    pthread_mutex_unlock(&lock);
    if (!ok) {
        send_error(-ETIMEDOUT);
        if (own)
            restore(was_streaming, stream_id, sw, sh);
        return;
    }
    snprintf(m.camera, sizeof m.camera, "%s", req->camera);
    send_msg(&m, &fd, 1);
    close(fd);
    LOG("still from camera %s: %u bytes", req->camera, m.size);
    if (own)
        restore(was_streaming, stream_id, sw, sh);
}

/* A stream whose camera failed (the HAL crashed, say: it restarts) is opened again, so the
 * client's stream goes on by itself. */
static void
recover(void)
{
    if (!wanted.camera[0])
        return;
    pthread_mutex_lock(&lock);
    int down = cam.failed || !cam.streaming;
    pthread_mutex_unlock(&lock);
    if (!down)
        return;
    LOG("camera %s: the stream is down; opening it again", wanted.camera);
    restore(1, wanted.camera, wanted.width, wanted.height);
}

static void
serve(void)
{
    struct camera_message req;

    for (;;) {
        struct pollfd pfd = { .fd = client, .events = POLLIN };
        if (poll(&pfd, 1, 1000) == 0) {
            recover();
            continue;
        }
        ssize_t n = recv(client, &req, sizeof req, 0);
        if (n <= 0)
            break;
        if (n != sizeof req)
            continue;
        req.camera[CAMERA_ID_SIZE - 1] = 0;
        switch (req.type) {
        case MSG_LIST:
            do_list();
            break;
        case MSG_STREAM:
            do_stream(&req);
            break;
        case MSG_RELEASE:
            pthread_mutex_lock(&lock);
            if (req.index < CAMERA_MAX_BUFFERS)
                cam.busy &= ~(1u << req.index);
            pthread_mutex_unlock(&lock);
            break;
        case MSG_STILL:
            do_still(&req);
            break;
        case MSG_STOP:
            wanted.camera[0] = 0;
            close_session(1);
            break;
        case MSG_DEVICE_STATE:
            do_device_state(&req);
            break;
        case MSG_TORCH:
            do_torch(&req);
            break;
        }
    }
}

/* --- the device's state ---------------------------------------------------------------- */

/* cameraserver's ICameraService (frameworks/av ICameraService.aidl, Android 17):
 * oneway notifyDeviceStateChange(long newState). On Android, system_server tells it the
 * device's state (folded, covered); the HAL picks cameras by it (a foldable's front camera
 * is the outer one when folded). */
#define CAMERA_SERVICE "media.camera"
#define CAMERA_SERVICE_DESCRIPTOR "android.hardware.ICameraService"
#define TX_NOTIFY_DEVICE_STATE_CHANGE (FIRST_CALL_TRANSACTION + 20)

static void *class_on_create(void *args) { return args; }
static void class_on_destroy(void *data) {}
static binder_status_t
class_on_transact(AIBinder *b, transaction_code_t code, const AParcel *in, AParcel *out)
{
    return STATUS_UNKNOWN_TRANSACTION;
}

static int64_t device_state = -1;       /* as last told to cameraserver */

/* cameraserver, to call (a reference: AIBinder_decStrong it), or NULL */
static AIBinder *
camera_service(void)
{
    static AIBinder_Class *clazz;
    static AIBinder *(*get_service)(const char *);

    if (!clazz) {
        void *lib = dlopen("libbinder_ndk.so", RTLD_NOW);
        get_service = lib ? dlsym(lib, "AServiceManager_getService") : NULL;     /* (not NDK) */
        clazz = AIBinder_Class_define(CAMERA_SERVICE_DESCRIPTOR, class_on_create,
                                      class_on_destroy, class_on_transact);
    }
    AIBinder *service = get_service ? get_service(CAMERA_SERVICE) : NULL;
    if (service && !AIBinder_associateClass(service, clazz)) {
        AIBinder_decStrong(service);
        service = NULL;
    }
    if (!service)
        ERR("no %s", CAMERA_SERVICE);
    return service;
}

static int
notify_device_state(int64_t state)
{
    AParcel *in = NULL, *out = NULL;
    binder_status_t st;

    AIBinder *service = camera_service();
    if (!service)
        return -1;
    st = AIBinder_prepareTransaction(service, &in);
    if (st == STATUS_OK)
        st = AParcel_writeInt64(in, state);
    if (st == STATUS_OK)
        st = AIBinder_transact(service, TX_NOTIFY_DEVICE_STATE_CHANGE, &in, &out, FLAG_ONEWAY);
    else
        AParcel_delete(in);
    AParcel_delete(out);
    AIBinder_decStrong(service);
    if (st != STATUS_OK) {
        ERR("notifyDeviceStateChange: %d", st);
        return -1;
    }
    LOG("device state: %#llx", (long long)state);
    return 0;
}

/* DEVICE_STATE: cameraserver hears of it; a stream goes on with the camera that the HAL
 * now picks for the same id */
static void
do_device_state(const struct camera_message *req)
{
    int64_t state = (uint32_t)req->code;

    if (state == device_state || notify_device_state(state))
        return;
    int changed = device_state >= 0;
    device_state = state;
    if (changed && wanted.camera[0]) {
        close_session(1);
        restore(1, wanted.camera, wanted.width, wanted.height);
    }
}

/* --- the torch -------------------------------------------------------------------------- */

/* ICameraService (Android 17): void setTorchMode(String cameraId, boolean enabled,
 * IBinder clientBinder, in AttributionSourceState clientAttribution, int devicePolicy).
 * clientBinder: cameraserver turns the torch off when it dies (this process). */
#define TX_SET_TORCH_MODE (FIRST_CALL_TRANSACTION + 15)

static char torch_camera[CAMERA_ID_SIZE];

static const char *
no_strings(const void *array, size_t index, int32_t *length)
{
    return NULL;
}

/* frameworks/base AttributionSourceState: who asks (this process), as a parcelable */
static binder_status_t
write_attribution(AParcel *p)
{
    binder_status_t st = AParcel_writeInt32(p, 1);             /* not null */
    int32_t start = AParcel_getDataPosition(p);

    if (st == STATUS_OK) st = AParcel_writeInt32(p, 0);        /* its size, below */
    if (st == STATUS_OK) st = AParcel_writeInt32(p, getpid());
    if (st == STATUS_OK) st = AParcel_writeInt32(p, getuid());
    if (st == STATUS_OK) st = AParcel_writeInt32(p, 0);        /* deviceId: the default */
    if (st == STATUS_OK) st = AParcel_writeString(p, "mobarchy-camerad", 16);  /* packageName */
    if (st == STATUS_OK) st = AParcel_writeString(p, NULL, -1);                /* attributionTag */
    if (st == STATUS_OK) st = AParcel_writeStrongBinder(p, NULL);              /* token */
    if (st == STATUS_OK) st = AParcel_writeStringArray(p, NULL, -1, no_strings); /* renouncedPermissions */
    if (st == STATUS_OK) st = AParcel_writeInt32(p, 0);        /* next: none */
    if (st != STATUS_OK)
        return st;
    int32_t end = AParcel_getDataPosition(p);
    AParcel_setDataPosition(p, start);
    AParcel_writeInt32(p, end - start);
    return AParcel_setDataPosition(p, end);
}

static int
set_torch(const char *id, int on)
{
    static AIBinder_Class *token_class;
    static AIBinder *token;
    AParcel *in = NULL, *out = NULL;
    binder_status_t st;
    int32_t exception = -1;

    if (!token_class) {
        token_class = AIBinder_Class_define("org.mobarchy.camerad.Torch", class_on_create,
                                            class_on_destroy, class_on_transact);
        token = AIBinder_new(token_class, NULL);
    }
    AIBinder *service = camera_service();
    if (!service)
        return -1;
    st = AIBinder_prepareTransaction(service, &in);
    if (st == STATUS_OK) st = AParcel_writeString(in, id, strlen(id));
    if (st == STATUS_OK) st = AParcel_writeBool(in, on);
    if (st == STATUS_OK) st = AParcel_writeStrongBinder(in, token);
    if (st == STATUS_OK) st = write_attribution(in);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, 0);       /* devicePolicy: the default */
    if (st == STATUS_OK)
        st = AIBinder_transact(service, TX_SET_TORCH_MODE, &in, &out, 0);
    else
        AParcel_delete(in);
    if (st == STATUS_OK)
        st = AParcel_readInt32(out, &exception);               /* the status header */
    AParcel_delete(out);
    AIBinder_decStrong(service);
    if (st != STATUS_OK || exception != 0) {
        ERR("setTorchMode(%s, %d): %d, exception %d", id, on, st, exception);
        return -1;
    }
    LOG("torch %s: %s", id, on ? "on" : "off");
    return 0;
}

static void
send_torch_state(void)
{
    struct camera_message m = { .type = MSG_TORCH_STATE, .code = torch_on };
    snprintf(m.camera, sizeof m.camera, "%s", torch_camera);
    send_msg(&m, NULL, 0);
}

/* the first camera with a flash (into id), or false */
static bool
flash_camera(char *id, size_t size)
{
    ACameraIdList *ids = NULL;
    bool found = false;

    if (ACameraManager_getCameraIdList(manager, &ids) != ACAMERA_OK)
        return false;
    for (int i = 0; i < ids->numCameras && !found; i++) {
        ACameraMetadata *md = NULL;
        ACameraMetadata_const_entry e;
        if (ACameraManager_getCameraCharacteristics(manager, ids->cameraIds[i], &md) != ACAMERA_OK)
            continue;
        if (ACameraMetadata_getConstEntry(md, ACAMERA_FLASH_INFO_AVAILABLE, &e) == ACAMERA_OK
            && e.data.u8[0] == ACAMERA_FLASH_INFO_AVAILABLE_TRUE) {
            snprintf(id, size, "%s", ids->cameraIds[i]);
            found = true;
        }
        ACameraMetadata_free(md);
    }
    ACameraManager_deleteCameraIdList(ids);
    return found;
}

/* TORCH: camera "" is the first one with a flash */
static void
do_torch(const struct camera_message *req)
{
    char id[CAMERA_ID_SIZE];

    snprintf(id, sizeof id, "%s", req->camera);
    if ((id[0] || flash_camera(id, sizeof id)) && set_torch(id, req->code != 0) == 0) {
        torch_on = req->code != 0;
        snprintf(torch_camera, sizeof torch_camera, "%s", id);
    }
    send_torch_state();
}

/* The camera's callbacks come over binder: the process needs binder threads. libbinder_ndk
 * has the calls, but not the NDK's headers. */
static void
start_binder_threads(void)
{
    void *lib = dlopen("libbinder_ndk.so", RTLD_NOW);
    int (*set_max)(uint32_t) = lib ? dlsym(lib, "ABinderProcess_setThreadPoolMaxThreadCount") : NULL;
    void (*start)(void) = lib ? dlsym(lib, "ABinderProcess_startThreadPool") : NULL;

    if (set_max && start) {
        set_max(4);
        start();
    } else {
        ERR("no binder thread pool: the camera's callbacks won't come");
    }
}

int
main(void)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int srv;

    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i < CAMERA_MAX_BUFFERS; i++)
        cam.fds[i] = -1;
    start_binder_threads();
    manager = ACameraManager_create();

    srv = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", CAMERA_SOCKET);
    unlink(CAMERA_SOCKET);
    if (srv < 0 || bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(srv, 1) < 0) {
        ERR("can't listen on %s: %s", CAMERA_SOCKET, strerror(errno));
        return 1;
    }
    /* the Linux side's client is another user: who may use the camera is decided there */
    chmod(CAMERA_SOCKET, 0666);
    LOG("listening on %s", CAMERA_SOCKET);

    for (;;) {
        int c = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
        if (c < 0)
            continue;
        pthread_mutex_lock(&lock);
        client = c;
        pthread_mutex_unlock(&lock);
        LOG("client connected");
        serve();
        wanted.camera[0] = 0;
        close_session(1);
        pthread_mutex_lock(&lock);
        client = -1;
        pthread_mutex_unlock(&lock);
        close(c);
        LOG("client gone");
    }
}
