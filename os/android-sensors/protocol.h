/*
 * The sensor bridge's protocol (ADR 0020), between mobarchy-sensord in the Android container
 * and mobarchy-sensor-proxy on the Linux side: messages on a SOCK_SEQPACKET Unix socket,
 * each one the struct below.
 *
 *   client: ENABLE sensor             server: EVENT sensor values... (on each change; the
 *                                     current value first), or UNAVAILABLE sensor
 *   client: DISABLE sensor            server: stops its events
 * The server serves one client at a time; a client that goes disables everything.
 */
#pragma once

#include <stdint.h>

#define SENSOR_SOCKET   "/data/mobarchy/sensord/socket"     /* in the container */

enum sensor_msg {
    SENSOR_ENABLE = 1,      /* client: sensor */
    SENSOR_DISABLE,         /* client: sensor */

    SENSOR_EVENT = 100,     /* server: sensor, values, timestamp */
    SENSOR_UNAVAILABLE,     /* server: sensor (the device has none) */
};

enum sensor_kind {
    /* how the device is turned (Android's device orientation sensor: debounced, as
     * Android rotates the screen by): values[0] = 0 upright (its natural orientation),
     * 1 turned 90° counter-clockwise, 2 upside down, 3 turned 90° clockwise */
    SENSOR_ORIENTATION = 1,
    SENSOR_LIGHT,           /* values[0]: lux */
    SENSOR_PROXIMITY,       /* values[0]: cm; binary sensors (most phones'): 0 near, their
                             * range (e.g. 5) far */
    SENSOR_ACCELEROMETER,   /* values[0..2]: m/s², x y z (Android's axes) */
    SENSOR_HINGE,           /* values[0]: a foldable's hinge angle, degrees (0 closed, 180 flat) */
    SENSOR_KINDS
};

struct sensor_message {
    uint32_t type;              /* enum sensor_msg */
    uint32_t sensor;            /* enum sensor_kind */
    float values[4];            /* EVENT */
    int64_t timestamp;          /* EVENT: ns, CLOCK_BOOTTIME */
};
_Static_assert(sizeof(struct sensor_message) == 32, "sensor_message is 32 bytes");
