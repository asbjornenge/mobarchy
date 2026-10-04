/*
 * camlist: lists the cameras cameraserver offers in the Android container (NDK camera API,
 * as an app would see them), with each one's facing, sensor size and output sizes.
 * A first client for testing the camera stack (ADR 0017). Build: see Makefile.
 */
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <stdio.h>

int main(void)
{
    ACameraManager *mgr = ACameraManager_create();
    ACameraIdList *ids = NULL;
    camera_status_t st = ACameraManager_getCameraIdList(mgr, &ids);

    if (st != ACAMERA_OK) {
        printf("getCameraIdList failed: %d\n", st);
        return 1;
    }
    printf("%d camera(s)\n", ids->numCameras);
    for (int i = 0; i < ids->numCameras; i++) {
        ACameraMetadata *md = NULL;
        ACameraMetadata_const_entry e;
        const char *id = ids->cameraIds[i];

        if (ACameraManager_getCameraCharacteristics(mgr, id, &md) != ACAMERA_OK) {
            printf("camera %s: no characteristics\n", id);
            continue;
        }
        const char *facing = "?";
        if (ACameraMetadata_getConstEntry(md, ACAMERA_LENS_FACING, &e) == ACAMERA_OK)
            facing = e.data.u8[0] == ACAMERA_LENS_FACING_FRONT ? "front"
                   : e.data.u8[0] == ACAMERA_LENS_FACING_BACK ? "back" : "external";
        printf("camera %s: %s", id, facing);
        if (ACameraMetadata_getConstEntry(md, ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE, &e) == ACAMERA_OK)
            printf(", sensor %dx%d", e.data.i32[0], e.data.i32[1]);
        if (ACameraMetadata_getConstEntry(md, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) == ACAMERA_OK) {
            printf(", outputs:");
            for (uint32_t j = 0; j + 3 < e.count; j += 4)
                if (e.data.i32[j + 3] == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
                    (e.data.i32[j] == 0x23 /* YUV_420_888 */ || e.data.i32[j] == 0x100 /* JPEG */))
                    printf(" %s %dx%d", e.data.i32[j] == 0x100 ? "jpeg" : "yuv",
                           e.data.i32[j + 1], e.data.i32[j + 2]);
        }
        printf("\n");
        ACameraMetadata_free(md);
    }
    ACameraManager_deleteCameraIdList(ids);
    ACameraManager_delete(mgr);
    return 0;
}
