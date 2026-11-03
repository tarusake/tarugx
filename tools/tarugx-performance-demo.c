// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

enum { W = 640, H = 480, FRAMES = 120 };

enum {
    REG_ACTIVE_BUFFER = 0x28,
    REG_FILL_ADDR = 0x60,
    REG_FILL_STRIDE = 0x64,
    REG_FILL_SIZE = 0x68,
    REG_FILL_COLOR = 0x6c,
    REG_FILL_COMMAND = 0x70,
    REG_FILL_STATUS = 0x74,
    REG_BLIT_SRC = 0x80,
    REG_BLIT_DST = 0x84,
    REG_BLIT_SRC_STRIDE = 0x88,
    REG_BLIT_DST_STRIDE = 0x8c,
    REG_BLIT_SIZE = 0x90,
    REG_BLIT_COMMAND = 0x94,
    REG_BLIT_STATUS = 0x98,
    FRAME_BYTES = W * H * 4,
    SPRITE_ADDRESS = 0x3f0000,
    SPRITE_W = 80,
    SPRITE_H = 160,
};

struct buffer {
    uint32_t handle, pitch, fb;
    uint64_t size;
    uint8_t *map;
};

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static double cpu_now(void)
{
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return r.ru_utime.tv_sec + r.ru_utime.tv_usec / 1e6 +
           r.ru_stime.tv_sec + r.ru_stime.tv_usec / 1e6;
}

static int create_buffer(int fd, struct buffer *b)
{
    struct drm_mode_create_dumb d = { .width = W, .height = H, .bpp = 32 };
    struct drm_mode_map_dumb m = {0};
    uint32_t handles[4], pitches[4], offsets[4] = {0};
    void *p;

    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &d))
        return -1;
    b->handle = d.handle;
    b->pitch = d.pitch;
    b->size = d.size;
    handles[0] = d.handle;
    pitches[0] = d.pitch;
    if (drmModeAddFB2(fd, W, H, DRM_FORMAT_XRGB8888, handles, pitches,
                      offsets, &b->fb, 0))
        return -1;
    m.handle = d.handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m))
        return -1;
    p = mmap(NULL, d.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
    if (p == MAP_FAILED)
        return -1;
    b->map = p;
    return 0;
}

static void destroy_buffer(int fd, struct buffer *b)
{
    if (b->fb)
        drmModeRmFB(fd, b->fb);
    if (b->map)
        munmap(b->map, b->size);
    if (b->handle) {
        struct drm_mode_destroy_dumb d = { .handle = b->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
}

static const uint8_t glyphs[][7] = {
    /* A */ {14, 17, 17, 31, 17, 17, 17},
    /* B */ {30, 17, 17, 30, 17, 17, 30},
    /* D */ {30, 17, 17, 17, 17, 17, 30},
    /* M */ {17, 27, 21, 21, 17, 17, 17},
    /* R */ {30, 17, 17, 30, 20, 18, 17},
};

static int glyph_index(char c)
{
    const char *letters = "ABDMR";
    const char *p = strchr(letters, c);
    return p ? (int)(p - letters) : -1;
}

static void label(struct buffer *b, const char *text)
{
    const unsigned scale = 12, width = 5 * scale, gap = 2 * scale;
    unsigned total = (unsigned)strlen(text) * (width + gap) - gap;
    unsigned x0 = (W - total) / 2, y0 = 54;

    for (unsigned n = 0; text[n]; n++) {
        int gi = glyph_index(text[n]);
        if (gi < 0)
            continue;
        for (unsigned y = 0; y < 7; y++)
            for (unsigned x = 0; x < 5; x++)
                if (glyphs[gi][y] & (1u << (4 - x)))
                    for (unsigned sy = 0; sy < scale; sy++) {
                        uint32_t *row = (uint32_t *)(b->map +
                            (y0 + y * scale + sy) * b->pitch);
                        for (unsigned sx = 0; sx < scale; sx++)
                            row[x0 + n * (width + gap) + x * scale + sx] =
                                0x00ffffff;
                    }
    }
}

static void paint(struct buffer *b, unsigned frame, int dma)
{
    unsigned bar = (frame * 11) % (W + 120);
    uint32_t base = dma ? 0x00102a54 : 0x00502018;

    for (unsigned y = 0; y < H; y++) {
        uint32_t *row = (uint32_t *)(b->map + y * b->pitch);
        for (unsigned x = 0; x < W; x++) {
            unsigned grid = ((x / 32) ^ (y / 32)) & 1;
            uint32_t color = base + (grid ? 0x00101010 : 0);
            if (x + 120 >= bar && x < bar)
                color = 0x00ffffff;
            row[x] = color;
        }
    }
    label(b, dma ? "DMA" : "BAR");
}

static int set_dma(const char *path, int enabled)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    char value = enabled ? '1' : '0';
    int ok;

    if (fd < 0)
        return -1;
    ok = write(fd, &value, 1) == 1 ? 0 : -1;
    close(fd);
    return ok;
}

static int run_phase(int fd, struct buffer *b, int dma)
{
    drmModeClip clip = {0, 0, W, H};
    double wall_start = now(), cpu_start = cpu_now();

    for (unsigned frame = 0; frame < FRAMES; frame++) {
        paint(b, frame, dma);
        if (drmModeDirtyFB(fd, b->fb, &clip, 1)) {
            perror("drmModeDirtyFB");
            return -1;
        }
    }
    double elapsed = now() - wall_start;
    double cpu = cpu_now() - cpu_start;
    printf("%s: %d frames, %.3f s, %.2f fps, %.2f ms/frame, "
           "process CPU %.1f%%\n", dma ? "DMA" : "BAR", FRAMES,
           elapsed, FRAMES / elapsed, elapsed * 1000 / FRAMES,
           elapsed ? cpu * 100 / elapsed : 0);
    fflush(stdout);
    sleep(1);
    return 0;
}

static void write_reg(volatile uint32_t *regs, unsigned offset, uint32_t value)
{
    regs[offset / 4] = value;
    __sync_synchronize();
    (void)regs[offset / 4];
}

static int fill(volatile uint32_t *regs, uint32_t address, unsigned stride,
                unsigned width, unsigned height, uint32_t color)
{
    double deadline = now() + 2.0;
    const struct timespec pause = { .tv_nsec = 50000 };
    uint32_t status;

    do {
        status = regs[REG_FILL_STATUS / 4];
        if (status & 1)
            nanosleep(&pause, NULL);
    } while ((status & 1) && now() < deadline);
    if (status & 1)
        return -1;
    write_reg(regs, REG_FILL_ADDR, address);
    write_reg(regs, REG_FILL_STRIDE, stride);
    write_reg(regs, REG_FILL_SIZE, height << 16 | width);
    write_reg(regs, REG_FILL_COLOR, color);
    write_reg(regs, REG_FILL_COMMAND, 1);
    do {
        status = regs[REG_FILL_STATUS / 4];
        if (!(status & 2))
            nanosleep(&pause, NULL);
    } while (!(status & 2) && now() < deadline);
    return (status & 6) == 2 ? 0 : -1;
}

static int run_fill_phase(volatile uint32_t *regs)
{
    uint32_t base = (regs[REG_ACTIVE_BUFFER / 4] & 1) * FRAME_BYTES;
    double wall_start = now(), cpu_start = cpu_now();

    for (unsigned frame = 0; frame < FRAMES; frame++) {
        unsigned x = frame * 11 % (W - 80);
        uint32_t shade = 0x00001820 + ((frame & 15) << 8);
        if (fill(regs, base, W * 4, W, H, shade) ||
            fill(regs, base + (160 * W + x) * 4, W * 4,
                 SPRITE_W, SPRITE_H, 0x00ffffff)) {
            fprintf(stderr, "FPGA fill failed on frame %u\n", frame);
            return -1;
        }
    }
    double elapsed = now() - wall_start;
    double cpu = cpu_now() - cpu_start;
    printf("FILL: %d frames, %.3f s, %.2f fps, %.2f ms/frame, "
           "process CPU %.1f%%\n", FRAMES, elapsed, FRAMES / elapsed,
           elapsed * 1000 / FRAMES, elapsed ? cpu * 100 / elapsed : 0);
    fflush(stdout);
    sleep(1);
    return 0;
}

static int blit(volatile uint32_t *regs, uint32_t source,
                uint32_t destination, unsigned width, unsigned height)
{
    double deadline = now() + 2.0;
    const struct timespec pause = { .tv_nsec = 50000 };
    uint32_t status;

    do {
        status = regs[REG_BLIT_STATUS / 4];
        if (status & 1)
            nanosleep(&pause, NULL);
    } while ((status & 1) && now() < deadline);
    if (status & 1)
        return -1;
    write_reg(regs, REG_BLIT_SRC, source);
    write_reg(regs, REG_BLIT_DST, destination);
    write_reg(regs, REG_BLIT_SRC_STRIDE, SPRITE_W * 4);
    write_reg(regs, REG_BLIT_DST_STRIDE, W * 4);
    write_reg(regs, REG_BLIT_SIZE, height << 16 | width);
    write_reg(regs, REG_BLIT_COMMAND, 1);
    do {
        status = regs[REG_BLIT_STATUS / 4];
        if (!(status & 2))
            nanosleep(&pause, NULL);
    } while (!(status & 2) && now() < deadline);
    return (status & 6) == 2 ? 0 : -1;
}

static int run_blit_phase(volatile uint32_t *regs)
{
    const uint32_t background = 0x00301050;
    uint32_t base = (regs[REG_ACTIVE_BUFFER / 4] & 1) * FRAME_BYTES;
    unsigned previous = 0;

    if (fill(regs, base, W * 4, W, H, background) ||
        fill(regs, SPRITE_ADDRESS, SPRITE_W * 4, SPRITE_W, SPRITE_H,
             0x0000d8ff)) {
        fputs("FPGA blit setup failed\n", stderr);
        return -1;
    }
    double wall_start = now(), cpu_start = cpu_now();
    for (unsigned frame = 0; frame < FRAMES; frame++) {
        unsigned x = (frame * 8) % (W - SPRITE_W);
        if (fill(regs, base + (160 * W + previous) * 4, W * 4,
                 SPRITE_W, SPRITE_H, background) ||
            blit(regs, SPRITE_ADDRESS, base + (160 * W + x) * 4,
                 SPRITE_W, SPRITE_H)) {
            fprintf(stderr, "FPGA blit failed on frame %u\n", frame);
            return -1;
        }
        previous = x;
    }
    int memfd = open("/sys/bus/pci/devices/0000:01:00.0/resource2",
                     O_RDONLY | O_SYNC | O_CLOEXEC);
    if (memfd < 0) {
        perror("BAR2 verify open");
        return -1;
    }
    volatile uint32_t *vram = mmap(NULL, 4 * 1024 * 1024, PROT_READ,
                                   MAP_SHARED, memfd, 0);
    if (vram == MAP_FAILED) {
        perror("BAR2 verify mmap");
        close(memfd);
        return -1;
    }
    unsigned last = ((FRAMES - 1) * 8) % (W - SPRITE_W);
    int verified = vram[base / 4] == background &&
        vram[(base + (160 * W + last) * 4) / 4] == 0x0000d8ff &&
        vram[(base + ((160 + SPRITE_H - 1) * W +
                      last + SPRITE_W - 1) * 4) / 4] == 0x0000d8ff;
    munmap((void *)vram, 4 * 1024 * 1024);
    close(memfd);
    if (!verified) {
        fputs("FPGA blit readback verification failed\n", stderr);
        return -1;
    }
    double elapsed = now() - wall_start;
    double cpu = cpu_now() - cpu_start;
    printf("BLIT: %d frames + readback PASS, %.3f s, %.2f fps, "
           "%.2f ms/frame, process CPU %.1f%%\n", FRAMES, elapsed, FRAMES / elapsed,
           elapsed * 1000 / FRAMES, elapsed ? cpu * 100 / elapsed : 0);
    fflush(stdout);
    sleep(1);
    return 0;
}

int main(int argc, char **argv)
{
    const char *parameter = "/sys/module/tarugx/parameters/dma_upload";
    int fd = -1, regfd = -1, rc = EXIT_FAILURE, master = 0, enabled = 0;
    char original = '1';
    volatile uint32_t *regs = MAP_FAILED;
    drmModeRes *res = NULL;
    drmModeConnector *connector = NULL;
    struct buffer b = {0};
    uint32_t crtc = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/dri/cardN\n", argv[0]);
        return EXIT_FAILURE;
    }
    int pfd = open(parameter, O_RDONLY | O_CLOEXEC);
    if (pfd < 0 || read(pfd, &original, 1) != 1) {
        perror("dma_upload parameter");
        if (pfd >= 0) close(pfd);
        return EXIT_FAILURE;
    }
    close(pfd);
    fd = open(argv[1], O_RDWR | O_CLOEXEC);
    if (fd < 0 || drmSetMaster(fd)) {
        perror("DRM master");
        goto out;
    }
    master = 1;
    res = drmModeGetResources(fd);
    if (!res || res->count_crtcs != 1)
        goto out;
    crtc = res->crtcs[0];
    for (int i = 0; i < res->count_connectors; i++) {
        connector = drmModeGetConnector(fd, res->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED &&
            connector->count_modes)
            break;
        if (connector)
            drmModeFreeConnector(connector);
        connector = NULL;
    }
    if (!connector || create_buffer(fd, &b)) {
        perror("KMS setup");
        goto out;
    }
    regfd = open("/sys/bus/pci/devices/0000:01:00.0/resource0",
                 O_RDWR | O_SYNC | O_CLOEXEC);
    if (regfd < 0) {
        perror("BAR0 open");
        goto out;
    }
    regs = mmap(NULL, 64 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, regfd, 0);
    if (regs == MAP_FAILED) {
        perror("BAR0 mmap");
        goto out;
    }
    paint(&b, 0, 0);
    if (drmModeSetCrtc(fd, crtc, b.fb, 0, 0, &connector->connector_id, 1,
                       &connector->modes[0])) {
        perror("modeset");
        goto out;
    }
    enabled = 1;
    puts("Watch the monitor: red BAR, blue DMA, green FILL, purple BLIT.");
    if (set_dma(parameter, 0) || run_phase(fd, &b, 0) ||
        set_dma(parameter, 1) || run_phase(fd, &b, 1) ||
        run_fill_phase(regs) || run_blit_phase(regs))
        goto out;
    rc = EXIT_SUCCESS;

out:
    if (set_dma(parameter, original == 'Y' || original == '1'))
        perror("restore dma_upload");
    if (enabled)
        drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0, NULL);
    if (regs != MAP_FAILED)
        munmap((void *)regs, 64 * 1024);
    if (regfd >= 0)
        close(regfd);
    destroy_buffer(fd, &b);
    if (connector)
        drmModeFreeConnector(connector);
    if (res)
        drmModeFreeResources(res);
    if (master)
        drmDropMaster(fd);
    if (fd >= 0)
        close(fd);
    return rc;
}
