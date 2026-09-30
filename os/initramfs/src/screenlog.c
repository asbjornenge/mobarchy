/*
 * mobarchy-screenlog: show the tail of a text log on every connected display,
 * using only DRM/KMS dumb buffers (no libdrm, no GPU, no compositor).
 *
 *   mobarchy-screenlog <logfile> [scale]
 *
 * An early-boot console for bring-up: the debug init starts it once the
 * display modules are loaded, and the log becomes readable on the phone's
 * screen, even with a webcam, when nothing else (USB, serial) works yet.
 * The font is a PSF console font compiled in as font_psf[] (see build).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

extern unsigned char font_psf[];
extern unsigned int font_psf_len;

#define MAX_OUT 4
#define MAX_LINES 256
#define LINE_LEN 512

struct buf { uint32_t handle, fb, pitch; uint64_t size; uint8_t *map; };
struct out {
    uint32_t conn, crtc, w, h;
    struct drm_mode_modeinfo mode;
    struct buf b[2];
    int front;
};

static int fd;
static struct out outs[MAX_OUT];
static int nouts;
static const uint8_t *glyphs;
static uint32_t gw, gh, gbytes, gcount;
static int scale = 2;

static int xioctl(unsigned long req, void *arg) {
    int r;
    do r = ioctl(fd, req, arg); while (r == -1 && (errno == EINTR || errno == EAGAIN));
    return r;
}

static int load_font(void) {
    const uint8_t *p = font_psf;
    if (font_psf_len > 32 && p[0] == 0x72 && p[1] == 0xb5 && p[2] == 0x4a && p[3] == 0x86) {  /* PSF2 */
        const uint32_t *h = (const uint32_t *)p;
        glyphs = p + h[2]; gcount = h[4]; gbytes = h[5]; gh = h[6]; gw = h[7];
        return 0;
    }
    if (font_psf_len > 4 && p[0] == 0x36 && p[1] == 0x04) {  /* PSF1 */
        glyphs = p + 4; gcount = (p[2] & 1) ? 512 : 256; gh = gbytes = p[3]; gw = 8;
        return 0;
    }
    return -1;
}

static int reimport;  /* SCREENLOG_REIMPORT=1: scan out via a dma-buf export/import round trip */

static int make_buf(struct out *o, struct buf *b) {
    struct drm_mode_create_dumb cd = { .width = o->w, .height = o->h, .bpp = 32 };
    if (xioctl(DRM_IOCTL_MODE_CREATE_DUMB, &cd)) return -1;
    b->handle = cd.handle; b->pitch = cd.pitch; b->size = cd.size;
    uint32_t fb_handle = cd.handle;
    if (reimport) {  /* what compositors do: export as dma-buf, import, make the FB from that */
        struct drm_prime_handle ph = { .handle = cd.handle, .flags = DRM_CLOEXEC | DRM_RDWR };
        if (xioctl(DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph)) { perror("prime export"); return -1; }
        struct drm_prime_handle ih = { .fd = ph.fd };
        if (xioctl(DRM_IOCTL_PRIME_FD_TO_HANDLE, &ih)) { perror("prime import"); return -1; }
        fb_handle = ih.handle;
        fprintf(stderr, "screenlog: reimported handle %u -> %u\n", cd.handle, ih.handle);
    }
    struct drm_mode_fb_cmd fc = { .width = o->w, .height = o->h, .pitch = cd.pitch,
                                  .bpp = 32, .depth = 24, .handle = fb_handle };
    if (xioctl(DRM_IOCTL_MODE_ADDFB, &fc)) return -1;
    b->fb = fc.fb_id;
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    if (xioctl(DRM_IOCTL_MODE_MAP_DUMB, &md)) return -1;
    b->map = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (b->map == MAP_FAILED) return -1;
    memset(b->map, 0, cd.size);
    return 0;
}

static int setup_outputs(void) {
    struct drm_mode_card_res res = {0};
    if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res)) return -1;
    uint32_t *crtcs = calloc(res.count_crtcs, 4), *conns = calloc(res.count_connectors, 4);
    uint32_t *encs = calloc(res.count_encoders, 4);
    res.crtc_id_ptr = (uintptr_t)crtcs; res.connector_id_ptr = (uintptr_t)conns;
    res.encoder_id_ptr = (uintptr_t)encs; res.fb_id_ptr = 0; res.count_fbs = 0;
    if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res)) return -1;
    uint32_t used = 0;

    for (uint32_t i = 0; i < res.count_connectors && nouts < MAX_OUT; i++) {
        struct drm_mode_get_connector c = { .connector_id = conns[i] };
        if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
        if (c.connection != 1 || c.count_modes == 0) continue;
        struct drm_mode_modeinfo *modes = calloc(c.count_modes, sizeof *modes);
        uint32_t *cencs = calloc(c.count_encoders ? c.count_encoders : 1, 4);
        c.modes_ptr = (uintptr_t)modes; c.encoders_ptr = (uintptr_t)cencs;
        c.count_props = 0; c.props_ptr = c.prop_values_ptr = 0;
        if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;

        /* A CRTC this connector's encoders can drive and that is still free. */
        uint32_t crtc = 0;
        for (uint32_t e = 0; e < c.count_encoders && !crtc; e++) {
            struct drm_mode_get_encoder enc = { .encoder_id = cencs[e] };
            if (xioctl(DRM_IOCTL_MODE_GETENCODER, &enc)) continue;
            for (uint32_t k = 0; k < res.count_crtcs; k++)
                if ((enc.possible_crtcs & (1u << k)) && !(used & (1u << k))) {
                    crtc = crtcs[k]; used |= 1u << k; break;
                }
        }
        if (!crtc) continue;
        struct out *o = &outs[nouts];
        o->conn = conns[i]; o->crtc = crtc; o->mode = modes[0];
        o->w = modes[0].hdisplay; o->h = modes[0].vdisplay;
        if (make_buf(o, &o->b[0]) || make_buf(o, &o->b[1])) continue;
        struct drm_mode_crtc sc = { .crtc_id = crtc, .fb_id = o->b[0].fb, .set_connectors_ptr = (uintptr_t)&o->conn,
                                    .count_connectors = 1, .mode = o->mode, .mode_valid = 1 };
        if (xioctl(DRM_IOCTL_MODE_SETCRTC, &sc)) { perror("setcrtc"); continue; }
        fprintf(stderr, "screenlog: connector %u %ux%u on crtc %u\n", o->conn, o->w, o->h, crtc);
        nouts++;
    }
    return nouts ? 0 : -1;
}

static void draw_char(struct buf *b, struct out *o, int x0, int y0, unsigned char ch, uint32_t color) {
    if (ch >= gcount) ch = '?';
    const uint8_t *g = glyphs + ch * gbytes;
    uint32_t rowbytes = (gw + 7) / 8;
    for (uint32_t y = 0; y < gh; y++)
        for (uint32_t x = 0; x < gw; x++) {
            if (!(g[y * rowbytes + x / 8] & (0x80 >> (x % 8)))) continue;
            for (int sy = 0; sy < scale; sy++) {
                uint32_t py = y0 + y * scale + sy;
                if (py >= o->h) continue;
                uint32_t *row = (uint32_t *)(b->map + py * b->pitch);
                for (int sx = 0; sx < scale; sx++) {
                    uint32_t px = x0 + x * scale + sx;
                    if (px < o->w) row[px] = color;
                }
            }
        }
}

static uint32_t line_color(const char *l) {
    if ((strcasestr(l, "fail") && !strstr(l, " 0 failed")) || strcasestr(l, "error") || strstr(l, " no ")) return 0xff5050;
    if (strstr(l, "diag")) return 0xffd040;
    if (strstr(l, "bound") || strstr(l, "ready") || strstr(l, " ok")) return 0x60ff60;
    return 0xe0e0e0;
}

/* Wrap and render the tail of the log so the newest line is at the bottom. */
static void render(struct out *o, char lines[][LINE_LEN], int n) {
    struct buf *b = &o->b[!o->front];
    memset(b->map, 0, b->size);
    int cols = o->w / (gw * scale), rows = o->h / (gh * scale);
    if (cols < 1 || rows < 1) return;
    int r = rows - 1;
    for (int i = n - 1; i >= 0 && r >= 0; i--) {
        int len = strlen(lines[i]), nrows = len ? (len + cols - 1) / cols : 1;
        uint32_t color = line_color(lines[i]);
        for (int k = nrows - 1; k >= 0 && r >= 0; k--, r--)
            for (int c = 0; c < cols && k * cols + c < len; c++)
                draw_char(b, o, c * gw * scale, r * gh * scale, lines[i][k * cols + c], color);
    }
    struct drm_mode_crtc_page_flip pf = { .crtc_id = o->crtc, .fb_id = b->fb };
    if (ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf)) {  /* fall back to a full modeset */
        struct drm_mode_crtc sc = { .crtc_id = o->crtc, .fb_id = b->fb, .set_connectors_ptr = (uintptr_t)&o->conn,
                                    .count_connectors = 1, .mode = o->mode, .mode_valid = 1 };
        xioctl(DRM_IOCTL_MODE_SETCRTC, &sc);
    }
    struct drm_mode_fb_dirty_cmd d = { .fb_id = b->fb };  /* command-mode panels */
    ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &d);
    o->front = !o->front;
}

static char lines[MAX_LINES][LINE_LEN];

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <logfile> [scale]\n", argv[0]); return 2; }
    if (argc > 2) scale = atoi(argv[2]) > 0 ? atoi(argv[2]) : 2;
    reimport = getenv("SCREENLOG_REIMPORT") != NULL;
    if (load_font()) { fprintf(stderr, "screenlog: bad font\n"); return 1; }
    for (int i = 0; i < 900; i++) {  /* the display driver may still be probing (up to 3 min) */
        fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (fd >= 0 && (ioctl(fd, DRM_IOCTL_SET_MASTER, 0), setup_outputs() == 0)) break;
        if (fd >= 0) close(fd);
        fd = -1;
        usleep(200000);
    }
    if (fd < 0) { fprintf(stderr, "screenlog: no usable display\n"); return 1; }

    off_t last_size = -1;
    for (;;) {
        FILE *f = fopen(argv[1], "r");
        if (f) {
            fseeko(f, 0, SEEK_END);
            off_t size = ftello(f);
            if (size != last_size) {
                last_size = size;
                rewind(f);
                int n = 0;
                char tmp[LINE_LEN];
                while (fgets(tmp, sizeof tmp, f)) {  /* keep the last MAX_LINES lines */
                    tmp[strcspn(tmp, "\n")] = 0;
                    if (n == MAX_LINES) { memmove(lines, lines + 1, sizeof lines[0] * (MAX_LINES - 1)); n--; }
                    strcpy(lines[n++], tmp);
                }
                for (int i = 0; i < nouts; i++) render(&outs[i], lines, n);
            }
            fclose(f);
        }
        usleep(500000);
    }
}
