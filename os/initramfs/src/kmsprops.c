/*
 * kmsprops: dump the KMS properties of every CRTC, plane and connector
 * (bring-up tool, no libdrm). Blob properties are shown with their size and
 * the first bytes of their contents.
 *
 *   kmsprops [/dev/dri/card0]
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

static int fd;

static void dump_blob(uint64_t id) {
    struct drm_mode_get_blob b = { .blob_id = (uint32_t)id };
    if (!id || ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &b)) return;
    uint8_t *data = malloc(b.length ? b.length : 1);
    b.data = (uintptr_t)data;
    if (!ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &b)) {
        printf(" blob[%u bytes]:", b.length);
        for (uint32_t i = 0; i < b.length && i < 24; i++) printf(" %02x", data[i]);
    }
    free(data);
}

static void dump_props(uint32_t obj, uint32_t type, const char *what) {
    struct drm_mode_obj_get_properties op = { .obj_id = obj, .obj_type = type };
    if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op)) return;
    uint32_t *ids = calloc(op.count_props, 4);
    uint64_t *vals = calloc(op.count_props, 8);
    op.props_ptr = (uintptr_t)ids; op.prop_values_ptr = (uintptr_t)vals;
    if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op)) return;
    printf("%s %u:\n", what, obj);
    for (uint32_t i = 0; i < op.count_props; i++) {
        struct drm_mode_get_property p = { .prop_id = ids[i] };
        if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) continue;
        printf("  [%u] %-24s = %llu", ids[i], p.name, (unsigned long long)vals[i]);
        if (p.flags & DRM_MODE_PROP_BLOB) dump_blob(vals[i]);
        if (p.flags & DRM_MODE_PROP_IMMUTABLE) printf(" (immutable)");
        printf("\n");
    }
    free(ids); free(vals);
}

int main(int argc, char **argv) {
    fd = open(argc > 1 ? argv[1] : "/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open"); return 1; }
    struct drm_set_client_cap cap = { .capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES, .value = 1 };
    ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);
    cap.capability = DRM_CLIENT_CAP_ATOMIC;
    ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);

    struct drm_mode_card_res res = {0};
    ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res);
    uint32_t *crtcs = calloc(res.count_crtcs, 4), *conns = calloc(res.count_connectors, 4);
    res.crtc_id_ptr = (uintptr_t)crtcs; res.connector_id_ptr = (uintptr_t)conns;
    res.count_fbs = res.count_encoders = 0;
    ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res);
    for (uint32_t i = 0; i < res.count_crtcs; i++) dump_props(crtcs[i], DRM_MODE_OBJECT_CRTC, "crtc");
    for (uint32_t i = 0; i < res.count_connectors; i++) dump_props(conns[i], DRM_MODE_OBJECT_CONNECTOR, "connector");

    struct drm_mode_get_plane_res pr = {0};
    ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr);
    uint32_t *planes = calloc(pr.count_planes, 4);
    pr.plane_id_ptr = (uintptr_t)planes;
    ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr);
    for (uint32_t i = 0; i < pr.count_planes; i++) dump_props(planes[i], DRM_MODE_OBJECT_PLANE, "plane");
    return 0;
}
