// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

enum { W = 640, H = 480, FRAMES = 120 };
struct buffer { uint32_t handle, pitch, fb; uint64_t size; uint8_t *map; };
static void destroy(int fd, struct buffer *b)
{
    if (b->fb) drmModeRmFB(fd, b->fb);
    if (b->map) munmap(b->map, b->size);
    if (b->handle) {
        struct drm_mode_destroy_dumb d = { .handle = b->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
}
static int create(int fd, struct buffer *b)
{
    struct drm_mode_create_dumb d = { .width = W, .height = H, .bpp = 32 };
    struct drm_mode_map_dumb m = {0};
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &d)) return -1;
    b->handle = d.handle; b->pitch = d.pitch; b->size = d.size;
    uint32_t handles[4] = {d.handle}, pitches[4] = {d.pitch}, offsets[4] = {0};
    if (drmModeAddFB2(fd, W, H, DRM_FORMAT_XRGB8888, handles, pitches,
                      offsets, &b->fb, 0)) return -1;
    m.handle = d.handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) return -1;
    void *p = mmap(NULL, d.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (p == MAP_FAILED) return -1;
    b->map = p;
    return 0;
}
static uint32_t pixel(unsigned x, unsigned y, unsigned frame)
{
    unsigned left = (frame * 7) % (W - 80);
    if (x >= left && x < left + 80 && y >= 160 && y < 320) return 0xffffff;
    return (((x + frame * 3) & 255) << 16) |
           (((y + frame * 5) & 255) << 8) | ((x ^ y ^ frame) & 255);
}
static void paint(struct buffer *b, unsigned frame)
{
    for (unsigned y = 0; y < H; y++) {
        uint32_t *row = (uint32_t *)(b->map + y * b->pitch);
        for (unsigned x = 0; x < W; x++) row[x] = pixel(x, y, frame);
    }
}
static int verify(volatile uint32_t *vram, unsigned frame, int full)
{
    for (unsigned i = 0; i < W * H; i += full ? 1 : 997) {
        uint32_t want = pixel(i % W, i / W, frame), got = vram[i];
        if (got != want) {
            fprintf(stderr, "frame %u BAR2[%u]: %08x != %08x\n", frame, i, got, want);
            return -1;
        }
    }
    return 0;
}
static void flipped(int fd, unsigned sequence, unsigned sec, unsigned usec, void *data)
{
    (void)fd; (void)sequence; (void)sec; (void)usec;
    *(int *)data = 1;
}
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/dri/cardN (tarugx only; exclusive test)\n", argv[0]);
        return 1;
    }
    int rc = 1, enabled = 0, fd = open(argv[1], O_RDWR | O_CLOEXEC);
    int memfd = -1;
    volatile uint32_t *vram = MAP_FAILED;
    drmModeRes *res = NULL;
    drmModeConnector *connector = NULL;
    struct buffer b[2] = {{0}};
    uint32_t crtc = 0;
    if (fd < 0) { perror("open DRM"); return 1; }
    drmVersion *version = drmGetVersion(fd);
    int ours = version && version->name_len == 6 && !memcmp(version->name, "tarugx", 6);
    if (version) drmFreeVersion(version);
    if (!ours) { fprintf(stderr, "not a tarugx DRM device\n"); goto out; }
    if (drmSetMaster(fd)) { perror("DRM master (stop modetest first)"); goto out; }
    const char *card = strrchr(argv[1], '/');
    char path[256];
    if (!card || snprintf(path, sizeof(path), "/sys/class/drm/%s/device/resource2", card+1)
                  >= (int)sizeof(path)) goto out;
    /* Read-only observer: do not change device registers behind the driver. */
    memfd = open(path, O_RDONLY | O_SYNC);
    if (memfd < 0) { perror("BAR2 observer"); goto out; }
    vram = mmap(NULL, 4*1024*1024, PROT_READ, MAP_SHARED, memfd, 0);
    if (vram == MAP_FAILED) { perror("BAR2 mmap"); goto out; }
    res = drmModeGetResources(fd);
    if (!res || res->count_crtcs != 1) goto out;
    crtc = res->crtcs[0];
    for (int i = 0; i < res->count_connectors; i++) {
        connector = drmModeGetConnector(fd, res->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes) break;
        if (connector) drmModeFreeConnector(connector);
        connector = NULL;
    }
    if (!connector || create(fd, &b[0]) || create(fd, &b[1])) {
        perror("KMS setup"); goto out;
    }
    paint(&b[0], 0);
    if (drmModeSetCrtc(fd, crtc, b[0].fb, 0, 0, &connector->connector_id, 1,
                       &connector->modes[0])) { perror("modeset"); goto out; }
    enabled = 1;
    drmEventContext events = { .version = DRM_EVENT_CONTEXT_VERSION, .page_flip_handler = flipped };
    for (unsigned phase = 0; phase < 2; phase++) {
        double start = now();
        for (unsigned n = 0; n < FRAMES; n++) {
            unsigned frame = phase * FRAMES + n;
            unsigned target = phase ? ((n + 1) & 1) : 0;
            paint(&b[target], frame);
            if (!phase) {
                drmModeClip clip = {0, 0, W, H};
                if (drmModeDirtyFB(fd, b[target].fb, &clip, 1)) { perror("dirty"); goto out; }
            } else {
                int done = 0;
                if (drmModePageFlip(fd, crtc, b[target].fb, DRM_MODE_PAGE_FLIP_EVENT, &done)) {
                    perror("page flip"); goto out;
                }
                while (!done) {
                    struct pollfd p = { .fd = fd, .events = POLLIN };
                    int r = poll(&p, 1, 5000);
                    if (r <= 0 || !(p.revents & POLLIN) || drmHandleEvent(fd, &events)) {
                        fprintf(stderr, "page flip event failed/timeout\n"); goto out;
                    }
                }
            }
            if (verify(vram, frame, n == FRAMES - 1)) goto out;
        }
        printf("%s: %u frames PASS, %.3f s (includes readback); last frame all pixels matched\n",
               phase ? "flip+event" : "dirty", FRAMES, now()-start);
        fflush(stdout);
    }
    /* Final flip selected b[0]. Poison unannounced pixels in CPU memory:
     * a full-screen copy here must FAIL the readback test. */
    uint32_t *expected = malloc(W * H * sizeof(*expected));
    if (!expected) goto out;
    for (unsigned i = 0; i < W * H; i++)
        expected[i] = pixel(i % W, i / W, 2 * FRAMES - 1);
    const drmModeClip clips[] = {
        {100, 100, 140, 140}, {0, 0, 1, 1}, {639, 479, 640, 480},
        {0, 479, 640, 480}, {639, 0, 640, 480}, {0, 0, 640, 1},
    };
    for (unsigned n = 0; n < sizeof(clips)/sizeof(clips[0]); n++) {
        drmModeClip clip = clips[n];
        memset(b[0].map, 0xa5, b[0].size);
        for (unsigned y = clip.y1; y < clip.y2; y++) {
            uint32_t *row = (uint32_t *)(b[0].map + y * b[0].pitch);
            for (unsigned x = clip.x1; x < clip.x2; x++)
                row[x] = expected[y * W + x] = 0x00102030 + n;
        }
        if (drmModeDirtyFB(fd, b[0].fb, &clip, 1)) {
            perror("partial dirty"); free(expected); goto out;
        }
        for (unsigned i = 0; i < W * H; i++) {
            if (vram[i] != expected[i]) {
                fprintf(stderr, "partial clip %u pixel %u mismatch\n", n, i);
                free(expected); goto out;
            }
        }
    }
    free(expected);
    puts("partial dirty: 6 interior/edge clips PASS; all pixels checked, outside damage preserved");
    rc = 0;
out:
    if (enabled && drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0, NULL)) {
        perror("disable CRTC"); rc = 1;
    }
    destroy(fd, &b[1]); destroy(fd, &b[0]);
    if (connector) drmModeFreeConnector(connector);
    if (res) drmModeFreeResources(res);
    if (vram != MAP_FAILED) munmap((void *)vram, 4*1024*1024);
    if (memfd >= 0) close(memfd);
    close(fd);
    return rc;
}
