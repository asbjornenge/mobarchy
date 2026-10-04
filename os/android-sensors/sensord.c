/*
 * mobarchy-sensord: the sensor bridge in the Android container (ADR 0020). It reads the
 * device's sensors through Android's sensor service (the NDK sensor API, as an app would)
 * and serves them to the Linux side on a Unix socket, only while asked for each one.
 * The protocol is in protocol.h.
 */
#include "protocol.h"

#include <android/log.h>
#include <android/looper.h>
#include <android/sensor.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "sensord", __VA_ARGS__)
#define ERR(...) __android_log_print(ANDROID_LOG_ERROR, "sensord", __VA_ARGS__)

#define ID_LISTEN 1
#define ID_CLIENT 2
#define ID_SENSORS 3

/* Android's sensor types (hardware/libhardware sensors-base.h) */
#define TYPE_ACCELEROMETER 1
#define TYPE_LIGHT 5
#define TYPE_PROXIMITY 8
#define TYPE_DEVICE_ORIENTATION 27          /* (a system sensor: not in the NDK's list) */

static const int android_type[SENSOR_KINDS] = {
    [SENSOR_ORIENTATION] = TYPE_DEVICE_ORIENTATION,
    [SENSOR_LIGHT] = TYPE_LIGHT,
    [SENSOR_PROXIMITY] = TYPE_PROXIMITY,
    [SENSOR_ACCELEROMETER] = TYPE_ACCELEROMETER,
};
#define ACCELEROMETER_PERIOD_US 100000      /* 10 Hz: enough to tell how it is held */

static ASensorManager *manager;
static ASensorEventQueue *queue;
static const ASensor *sensors[SENSOR_KINDS];
static int enabled[SENSOR_KINDS];
static int client = -1;

static void
send_msg(const struct sensor_message *m)
{
    if (client >= 0)
        send(client, m, sizeof *m, MSG_NOSIGNAL);
}

static int
kind_of(int type)
{
    for (int k = 1; k < SENSOR_KINDS; k++)
        if (android_type[k] == type)
            return k;
    return 0;
}

static void
enable(int kind, int on)
{
    if (kind <= 0 || kind >= SENSOR_KINDS || enabled[kind] == on)
        return;
    if (!sensors[kind]) {
        struct sensor_message m = { .type = SENSOR_UNAVAILABLE, .sensor = kind };
        send_msg(&m);
        return;
    }
    if (on) {
        int32_t period = ASensor_getReportingMode(sensors[kind]) == AREPORTING_MODE_CONTINUOUS
                       ? ACCELEROMETER_PERIOD_US : 0;
        if (ASensorEventQueue_registerSensor(queue, sensors[kind], period, 0) < 0) {
            ERR("can't enable %s", ASensor_getName(sensors[kind]));
            return;
        }
    } else {
        ASensorEventQueue_disableSensor(queue, sensors[kind]);
    }
    enabled[kind] = on;
    LOG("%s: %s", ASensor_getName(sensors[kind]), on ? "on" : "off");
}

static void
client_gone(ALooper *looper)
{
    for (int k = 1; k < SENSOR_KINDS; k++)
        enable(k, 0);
    ALooper_removeFd(looper, client);
    close(client);
    client = -1;
    LOG("client gone");
}

static void
on_client(ALooper *looper)
{
    struct sensor_message m;
    ssize_t n = recv(client, &m, sizeof m, 0);

    if (n <= 0) {
        client_gone(looper);
        return;
    }
    if (n != sizeof m)
        return;
    if (m.type == SENSOR_ENABLE || m.type == SENSOR_DISABLE)
        enable((int)m.sensor, m.type == SENSOR_ENABLE);
}

static void
on_sensors(void)
{
    ASensorEvent ev[16];
    ssize_t n;

    while ((n = ASensorEventQueue_getEvents(queue, ev, 16)) > 0)
        for (ssize_t i = 0; i < n; i++) {
            int kind = kind_of(ev[i].type);
            if (!kind || !enabled[kind])
                continue;
            struct sensor_message m = { .type = SENSOR_EVENT, .sensor = kind,
                                        .timestamp = ev[i].timestamp };
            memcpy(m.values, ev[i].data, sizeof m.values);
            send_msg(&m);
        }
}

int
main(void)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int srv;

    signal(SIGPIPE, SIG_IGN);
    manager = ASensorManager_getInstanceForPackage("mobarchy-sensord");
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    queue = ASensorManager_createEventQueue(manager, looper, ID_SENSORS, NULL, NULL);
    if (!manager || !queue) {
        ERR("no sensor service");
        return 1;
    }
    for (int k = 1; k < SENSOR_KINDS; k++) {
        sensors[k] = ASensorManager_getDefaultSensor(manager, android_type[k]);
        LOG("%d: %s", k, sensors[k] ? ASensor_getName(sensors[k]) : "(none)");
    }

    srv = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", SENSOR_SOCKET);
    unlink(SENSOR_SOCKET);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(srv, 2) < 0) {
        ERR("%s: %s", SENSOR_SOCKET, strerror(errno));
        return 1;
    }
    /* the Linux side's client is another user: who may read the sensors is decided there */
    chmod(SENSOR_SOCKET, 0666);
    ALooper_addFd(looper, srv, ID_LISTEN, ALOOPER_EVENT_INPUT, NULL, NULL);
    LOG("listening on %s", SENSOR_SOCKET);

    for (;;) {
        int fd, events;
        int id = ALooper_pollOnce(-1, &fd, &events, NULL);
        if (id == ID_LISTEN) {
            int c = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
            if (c < 0)
                continue;
            if (client >= 0)
                client_gone(looper);        /* one client: the newest */
            client = c;
            ALooper_addFd(looper, client, ID_CLIENT, ALOOPER_EVENT_INPUT, NULL, NULL);
            LOG("client connected");
        } else if (id == ID_CLIENT) {
            on_client(looper);
        } else if (id == ID_SENSORS) {
            on_sensors();
        }
    }
}
