/*
 * The camera bridge's protocol (ADR 0018), between mobarchy-camerad in the Android container
 * and its client on the Linux side: messages on a SOCK_SEQPACKET Unix socket, each one a
 * struct below, some with file descriptors (SCM_RIGHTS).
 *
 * Session:
 *   client: LIST                      server: CAMERA ... (one per camera), END
 *   client: STREAM camera w h         server: BUFFERS (fds: the frame buffers), then FRAME
 *                                     for each frame; the client gives each back (RELEASE)
 *   client: STILL camera w h rotation server: JPEG (fd: a memfd with the JPEG), or ERROR
 *   client: STOP                      server: stops streaming
 *   client: DEVICE_STATE state        (the device's state, for the cameras it picks: sent on
 *                                     connecting and on every change; a stream goes on)
 *   client: RECORD camera w h rotation server: RECORDING (video from the camera, encoded in
 *                                     hardware, while the stream goes on), or ERROR
 *   client: RECORD_STOP               server: VIDEO (fd: the MP4, video only), or ERROR
 *   client: TORCH camera on           server: TORCH_STATE on (the torch's state now: as asked, or
 *                                     not if it couldn't); also unasked, when a camera
 *                                     session takes the flash
 * The server serves one client at a time; frames are I420 (planar YUV 4:2:0).
 */
#pragma once

#include <stdint.h>

#define CAMERA_SOCKET           "/data/mobarchy/camera"     /* in the container */
#define CAMERA_MAX_BUFFERS      4
#define CAMERA_ID_SIZE          16

enum camera_msg {
    MSG_LIST = 1,       /* client */
    MSG_STREAM,         /* client: camera, width, height */
    MSG_RELEASE,        /* client: index */
    MSG_STILL,          /* client: camera, width, height, orientation (the device's) */
    MSG_STOP,           /* client */
    MSG_DEVICE_STATE,   /* client: code (enum device_state flags) */
    MSG_TORCH,          /* client: camera ("": the first with a flash), code (1: on, 0: off) */
    MSG_RECORD,         /* client: camera, width, height (0: 1920x1080), orientation (the
                         * device's, as STILL) */
    MSG_RECORD_STOP,    /* client */

    MSG_CAMERA = 100,   /* server: one camera */
    MSG_END,            /* server: end of the list */
    MSG_BUFFERS,        /* server: count buffers of size bytes (fds), frame geometry */
    MSG_FRAME,          /* server: index, timestamp */
    MSG_JPEG,           /* server: size bytes (fd) */
    MSG_ERROR,          /* server: code (negative errno or camera_status_t) */
    MSG_TORCH_STATE,    /* server: camera, code (1: on, 0: off) */
    MSG_RECORDING,      /* server: camera, width, height */
    MSG_VIDEO,          /* server: size bytes (fd), timestamp: its length in ns */
};

/* the device's state (as Android's ICameraService.DEVICE_STATE_*): flags */
enum device_state {
    DEVICE_STATE_NORMAL = 0,
    DEVICE_STATE_BACK_COVERED = 1,
    DEVICE_STATE_FRONT_COVERED = 2,
    DEVICE_STATE_FOLDED = 4,
};

enum camera_facing { FACING_BACK = 0, FACING_FRONT = 1, FACING_EXTERNAL = 2 };

struct camera_message {
    uint32_t type;                  /* enum camera_msg */
    char camera[CAMERA_ID_SIZE];    /* camera id, NUL terminated */
    uint32_t width, height;
    uint32_t facing;                /* CAMERA: enum camera_facing */
    uint32_t orientation;           /* CAMERA: degrees the image is rotated from upright;
                                     * STILL: degrees the device is turned clockwise from
                                     * its natural orientation */
    uint32_t max_width, max_height; /* CAMERA: largest output */
    uint32_t count;                 /* BUFFERS: number of buffers (fds) */
    uint32_t size;                  /* BUFFERS: bytes per buffer; JPEG: bytes */
    uint32_t index;                 /* FRAME, RELEASE: buffer */
    int32_t code;                   /* ERROR; DEVICE_STATE: enum device_state flags;
                                     * TORCH, TORCH_STATE: 1 on, 0 off */
    uint32_t reserved;              /* (explicit padding: the layout has no implicit gaps) */
    uint64_t timestamp;             /* FRAME: ns, CLOCK_BOOTTIME (the sensor's) */
};
_Static_assert(sizeof(struct camera_message) == 72, "camera_message is 72 bytes");
