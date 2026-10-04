/*
 * camshot: takes one JPEG with a camera through cameraserver in the Android container (NDK
 * camera API), after letting auto exposure and focus settle on a few preview frames.
 *
 *     camshot [camera id] [width height] [out.jpg]       (defaults: 0, 1920 1080, photo.jpg)
 *
 * A test client for the camera stack (ADR 0017). Build: see Makefile.
 */
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SETTLE_FRAMES 30

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int preview_frames, jpeg_done, failed;
static const char *out_path = "photo.jpg";

static void on_disconnected(void *ctx, ACameraDevice *dev) { fprintf(stderr, "camera disconnected\n"); }
static void on_error(void *ctx, ACameraDevice *dev, int err)
{
    fprintf(stderr, "camera error %d\n", err);
    pthread_mutex_lock(&lock);
    failed = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

static void on_preview(void *ctx, AImageReader *reader)
{
    AImage *img = NULL;
    if (AImageReader_acquireLatestImage(reader, &img) == AMEDIA_OK && img)
        AImage_delete(img);
    pthread_mutex_lock(&lock);
    preview_frames++;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

static void on_jpeg(void *ctx, AImageReader *reader)
{
    AImage *img = NULL;
    uint8_t *data = NULL;
    int len = 0;

    if (AImageReader_acquireNextImage(reader, &img) != AMEDIA_OK || !img)
        return;
    if (AImage_getPlaneData(img, 0, &data, &len) == AMEDIA_OK) {
        FILE *f = fopen(out_path, "wb");
        if (f) {
            fwrite(data, 1, len, f);
            fclose(f);
            printf("wrote %s (%d bytes)\n", out_path, len);
        }
    }
    AImage_delete(img);
    pthread_mutex_lock(&lock);
    jpeg_done = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

static void session_noop(void *ctx, ACameraCaptureSession *s) {}

/* what the camera chose for the still: exposure, sensitivity, white balance */
static void on_still_completed(void *ctx, ACameraCaptureSession *s, ACaptureRequest *req,
                               const ACameraMetadata *result)
{
    ACameraMetadata_const_entry e;

    if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_EXPOSURE_TIME, &e) == ACAMERA_OK)
        printf("exposure %.2f ms", e.data.i64[0] / 1e6);
    if (ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_SENSITIVITY, &e) == ACAMERA_OK)
        printf(", ISO %d", e.data.i32[0]);
    if (ACameraMetadata_getConstEntry(result, ACAMERA_CONTROL_AE_STATE, &e) == ACAMERA_OK)
        printf(", AE state %d", e.data.u8[0]);
    if (ACameraMetadata_getConstEntry(result, ACAMERA_COLOR_CORRECTION_GAINS, &e) == ACAMERA_OK)
        printf(", WB gains %.2f %.2f %.2f %.2f", e.data.f[0], e.data.f[1], e.data.f[2], e.data.f[3]);
    printf("\n");
}

static int wait_for(int *flag, int value, int seconds)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    pthread_mutex_lock(&lock);
    while (*flag < value && !failed)
        if (pthread_cond_timedwait(&cond, &lock, &ts))
            break;
    int ok = *flag >= value && !failed;
    pthread_mutex_unlock(&lock);
    return ok;
}

static AImageReader *
reader(int w, int h, int format, int max, AImageReader_ImageCallback cb, ACameraOutputTarget **target,
       ACaptureSessionOutput **output)
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

/* The camera's callbacks come over binder: the process needs binder threads, which an app
 * gets from its runtime. libbinder_ndk has the calls, but not the NDK's headers. */
static void start_binder_threads(void)
{
    void *lib = dlopen("libbinder_ndk.so", RTLD_NOW);
    int (*set_max)(uint32_t) = lib ? dlsym(lib, "ABinderProcess_setThreadPoolMaxThreadCount") : NULL;
    void (*start)(void) = lib ? dlsym(lib, "ABinderProcess_startThreadPool") : NULL;

    if (!set_max || !start) {
        fprintf(stderr, "no binder thread pool: callbacks won't come\n");
        return;
    }
    set_max(4);
    start();
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    start_binder_threads();
    const char *id = argc > 1 ? argv[1] : "0";
    int w = argc > 3 ? atoi(argv[2]) : 1920, h = argc > 3 ? atoi(argv[3]) : 1080;
    if (argc > 4)
        out_path = argv[4];

    ACameraManager *mgr = ACameraManager_create();
    ACameraDevice *dev = NULL;
    ACameraDevice_StateCallbacks dev_cb = { .onDisconnected = on_disconnected, .onError = on_error };
    camera_status_t st = ACameraManager_openCamera(mgr, id, &dev_cb, &dev);
    if (st != ACAMERA_OK) {
        fprintf(stderr, "openCamera(%s) failed: %d\n", id, st);
        return 1;
    }
    printf("camera %s open\n", id);

    ACameraOutputTarget *pv_target, *jpg_target;
    ACaptureSessionOutput *pv_out, *jpg_out;
    AImageReader *pv = reader(640, 480, AIMAGE_FORMAT_YUV_420_888, 4, on_preview, &pv_target, &pv_out);
    AImageReader *jpg = reader(w, h, AIMAGE_FORMAT_JPEG, 2, on_jpeg, &jpg_target, &jpg_out);
    if (!pv || !jpg) {
        fprintf(stderr, "image readers failed\n");
        return 1;
    }

    ACaptureSessionOutputContainer *outputs;
    ACaptureSessionOutputContainer_create(&outputs);
    ACaptureSessionOutputContainer_add(outputs, pv_out);
    ACaptureSessionOutputContainer_add(outputs, jpg_out);
    ACameraCaptureSession *session;
    ACameraCaptureSession_stateCallbacks s_cb = { .onClosed = session_noop, .onReady = session_noop,
                                                  .onActive = session_noop };
    st = ACameraDevice_createCaptureSession(dev, outputs, &s_cb, &session);
    if (st != ACAMERA_OK) {
        fprintf(stderr, "createCaptureSession failed: %d\n", st);
        return 1;
    }
    printf("session created\n");

    /* preview frames, so that exposure and focus settle */
    ACaptureRequest *preview;
    ACameraDevice_createCaptureRequest(dev, TEMPLATE_PREVIEW, &preview);
    ACaptureRequest_addTarget(preview, pv_target);
    st = ACameraCaptureSession_setRepeatingRequest(session, NULL, 1, &preview, NULL);
    if (st != ACAMERA_OK) {
        fprintf(stderr, "setRepeatingRequest failed: %d\n", st);
        return 1;
    }
    if (!wait_for(&preview_frames, SETTLE_FRAMES, 20)) {
        fprintf(stderr, "only %d preview frames came\n", preview_frames);
        return 1;
    }
    printf("%d preview frames\n", preview_frames);

    ACaptureRequest *still;
    ACameraDevice_createCaptureRequest(dev, TEMPLATE_STILL_CAPTURE, &still);
    ACaptureRequest_addTarget(still, jpg_target);
    ACameraCaptureSession_captureCallbacks still_cb = { .onCaptureCompleted = on_still_completed };
    st = ACameraCaptureSession_capture(session, &still_cb, 1, &still, NULL);
    if (st != ACAMERA_OK) {
        fprintf(stderr, "capture failed: %d\n", st);
        return 1;
    }
    int ok = wait_for(&jpeg_done, 1, 20);
    if (!ok)
        fprintf(stderr, "no JPEG came\n");

    ACameraCaptureSession_stopRepeating(session);
    ACameraCaptureSession_close(session);
    ACameraDevice_close(dev);
    AImageReader_delete(pv);
    AImageReader_delete(jpg);
    ACameraManager_delete(mgr);
    return ok ? 0 : 1;
}
