/*
 * The fingerprint bridge's protocol (ADR 0021): mobarchy-fingerprintd in the Android
 * container serves it on a SOCK_SEQPACKET socket in the container's /data, one message
 * per packet, to one client at a time (mobarchy-fingerprint, the Linux side).
 *
 * One operation runs at a time: enrol or verify until it ends (FP_DONE, or FP_ERROR), or
 * until FP_CANCEL. Enrolments are the Android user 0's: the phone's owner. Each has an id,
 * which the matcher in the TEE gives it; the Linux side keeps the names.
 */
#pragma once

#include <stdint.h>

#define FP_SOCKET_DIR "/data/mobarchy/fingerprintd"
#define FP_SOCKET FP_SOCKET_DIR "/socket"
#define FP_MAX_IDS 8

enum fp_request {
    FP_LIST = 1,            /* -> FP_ENROLLMENTS */
    FP_ENROLL,              /* -> FP_ACQUIRED..., FP_PROGRESS..., FP_DONE (id) or FP_ERROR */
    FP_VERIFY,              /* -> FP_ACQUIRED..., FP_MATCH (id), FP_NO_MATCH... (keeps
                             *    trying), FP_LOCKOUT, FP_ERROR; until FP_CANCEL */
    FP_CANCEL,              /* the running operation -> FP_ERROR (FP_ERROR_CANCELED) */
    FP_REMOVE,              /* ids -> FP_ENROLLMENTS (those left) */
    FP_RESET_LOCKOUT,       /* after the owner's PIN: fingers work again -> FP_LOCKOUT (0) */
};

enum fp_event {
    FP_ENROLLMENTS = 100,   /* ids, count */
    FP_ACQUIRED,            /* value: what the sensor saw (AcquiredInfo: 1 good, 2 partial,
                             *        3 insufficient, 4 dirty, 5 too slow, 6 too fast,
                             *        14 power key pressed, ...) */
    FP_PROGRESS,            /* value: enrolment id; value2: touches still needed */
    FP_MATCH,               /* value: enrolment id */
    FP_NO_MATCH,
    FP_LOCKOUT,             /* value64: milliseconds (-1: until FP_RESET_LOCKOUT; 0: over) */
    FP_ERROR,               /* value: enum fp_error */
    FP_DONE,                /* value: the new enrolment's id */
    FP_UNAVAILABLE,         /* the HAL or the TEE isn't there: try later */
};

/* the HAL's Error, as it says it */
enum fp_error {
    FP_ERROR_UNKNOWN = 0,
    FP_ERROR_HW_UNAVAILABLE = 1,
    FP_ERROR_UNABLE_TO_PROCESS = 2,
    FP_ERROR_TIMEOUT = 3,
    FP_ERROR_NO_SPACE = 4,
    FP_ERROR_CANCELED = 5,
    FP_ERROR_UNABLE_TO_REMOVE = 6,
    FP_ERROR_VENDOR = 7,
    FP_ERROR_BAD_CALIBRATION = 8,
    FP_ERROR_POWER_PRESS = 9,
    FP_ERROR_BUSY = 100,            /* (the bridge's) another operation is running */
    FP_ERROR_NOT_AUTHORIZED = 101,  /* (the bridge's) Gatekeeper gave no auth token */
};

struct fp_message {
    uint32_t type;
    int32_t value;
    int32_t value2;
    int32_t count;                  /* ids used */
    int64_t value64;
    int32_t ids[FP_MAX_IDS];
};
