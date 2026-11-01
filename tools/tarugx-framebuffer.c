#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum { BAR0_SIZE = 65536, BAR2_SIZE = 4194304, WIDTH = 640, HEIGHT = 480 };

static int check_device(const char *bdf)
{
    char path[256];
    uint8_t cfg[8];
    if (snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/config", bdf)
        >= (int)sizeof(path)) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }
    ssize_t got = pread(fd, cfg, sizeof(cfg), 0);
    close(fd);
    if (got != (ssize_t)sizeof(cfg) || cfg[0] != 0xc2 || cfg[1] != 0x22 ||
        cfg[2] != 0x00 || cfg[3] != 0x11 || !(cfg[4] & 2)) {
        fprintf(stderr, "live PCI configuration is not 22c2:1100 with Memory Space enabled\n");
        return -1;
    }
    return 0;
}

static volatile uint32_t *map_bar(const char *bdf, unsigned bar, size_t size)
{
    char path[256];
    struct stat st;
    if (snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource%u", bdf, bar)
        >= (int)sizeof(path)) return NULL;
    int fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) { perror(path); return NULL; }
    if (fstat(fd, &st) || st.st_size < (off_t)size) {
        fprintf(stderr, "%s: missing or undersized PCI BAR\n", path);
        close(fd); return NULL;
    }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { perror("mmap"); return NULL; }
    return p;
}

static uint32_t pixel(unsigned x, unsigned y)
{
    /* Visible RGB gradient, checkerboard blue and a white frame. */
    if (x < 4 || x >= WIDTH-4 || y < 4 || y >= HEIGHT-4) return 0x00ffffff;
    return ((x * 255 / (WIDTH-1)) << 16) |
           ((y * 255 / (HEIGHT-1)) << 8) |
           ((((x / 32) ^ (y / 32)) & 1) ? 255u : 32u);
}

/* Keep scanout enabled while competing for DDR; restore the original frame.
 * UART sticky fault state must be captured separately, starting before this test. */
static int stress_scanout(volatile uint32_t *regs, volatile uint32_t *fb)
{
    if (regs[0x10/4] != 1 || regs[0x1c/4] != 0 || regs[0x20/4] != WIDTH*4) {
        fprintf(stderr, "stress requires enabled framebuffer, base=0 and stride=2560\n");
        return 1;
    }
    uint32_t *saved = malloc(WIDTH * HEIGHT * sizeof(*saved));
    if (!saved) { perror("malloc"); return 1; }
    for (unsigned i=0; i<WIDTH*HEIGHT; i++) saved[i] = fb[i];
    unsigned errors = 0;
    for (unsigned pass=0; pass<8; pass++) {
        uint32_t mask = (pass & 1) ? 0 : UINT32_C(0x00ffffff);
        for (unsigned i=0; i<WIDTH*HEIGHT; i++) fb[i] = saved[i] ^ mask;
        __sync_synchronize();
        (void)regs[0];
        for (unsigned i=0; i<WIDTH*HEIGHT; i++)
            if (fb[i] != (saved[i] ^ mask)) errors++;
    }
    /* Last pass restores the frame even if an earlier comparison failed. */
    printf("Scanout load: 8 full-frame write/read passes, errors=%u; "
           "original frame restoration %s\n", errors, errors ? "not guaranteed" : "verified");
    free(saved);
    return errors || regs[0x10/4] != 1;
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[2], "probe") && strcmp(argv[2], "pattern") &&
                     strcmp(argv[2], "sweep") && strcmp(argv[2], "stress"))) {
        fprintf(stderr, "usage: %s 0000:01:00.0 probe|pattern|sweep|stress\n"
                "probe overwrites 256 bytes; pattern writes 640x480; sweep overwrites all 4 MiB.\n"
                "stress requires an unbound driver and enabled display; temporarily inverts and restores the frame.\n",
                argv[0]);
        return 1;
    }
    if (check_device(argv[1])) return 1;
    if (!strcmp(argv[2], "stress")) {
        char path[256];
        struct stat st;
        if (snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/driver", argv[1]) >= (int)sizeof(path)) return 1;
        if (lstat(path, &st) == 0 || errno != ENOENT) {
            fprintf(stderr, "stress requires an unbound PCI device\n");
            return 1;
        }
    }
    volatile uint32_t *regs = map_bar(argv[1], 0, BAR0_SIZE);
    if (!regs) return 1;
    if (regs[0] != UINT32_C(0x54475055)) {
        fprintf(stderr, "GPU_ID mismatch\n");
        munmap((void *)regs, BAR0_SIZE); return 1;
    }
    volatile uint32_t *fb = map_bar(argv[1], 2, BAR2_SIZE);
    if (!fb) { munmap((void *)regs, BAR0_SIZE); return 1; }
    int rc = 1;
    if (!strcmp(argv[2], "stress")) {
        rc = stress_scanout(regs, fb);
        goto done;
    }
    int pattern = !strcmp(argv[2], "pattern");
    int sweep = !strcmp(argv[2], "sweep");
    unsigned count = sweep ? BAR2_SIZE/4 : (pattern ? WIDTH*HEIGHT : 64);
    /* Leave timing active but blank the display before changing memory.
     * Use the normal resource2 mapping, never resource2_wc; single DWORD
     * transactions are required by this initial TLP bridge. */
    regs[0x10/4] = 0;
    __sync_synchronize();
    (void)regs[0];
    for (unsigned pass=0; pass<(sweep ? 2u : 1u); pass++) {
        uint32_t invert = pass ? UINT32_MAX : 0;
        printf("BAR2: writing %u DWORDs, pass %u\n", count, pass+1);
        fflush(stdout);
        for (unsigned i=0; i<count; i++)
            fb[i] = pattern ? pixel(i%WIDTH, i/WIDTH) : ((UINT32_C(0xa55a0000) ^ i) ^ invert);
        __sync_synchronize();
        (void)regs[0];
        /* Full readback includes a memory read after the posted writes. */
        for (unsigned i=0; i<count; i++) {
            uint32_t expected = pattern ? pixel(i%WIDTH, i/WIDTH) : ((UINT32_C(0xa55a0000)^i)^invert);
            uint32_t actual = fb[i];
            if (actual != expected) {
                fprintf(stderr, "BAR2[%u] expected %08" PRIx32 " actual %08" PRIx32 "\n",
                        i, expected, actual);
                goto done;
            }
        }
        printf("BAR2: %u DWORD writes and readbacks matched\n", count);
    }
    if (pattern) {
        regs[0x1c/4] = 0;
        regs[0x20/4] = WIDTH*4;
        regs[0x24/4] = 1;
        __sync_synchronize();
        (void)regs[0];
        regs[0x10/4] = 1;
        __sync_synchronize();
        printf("Framebuffer display enabled: ctrl=%08" PRIx32 "\n", regs[0x10/4]);
    }
    rc = 0;
done:
    munmap((void *)fb, BAR2_SIZE);
    munmap((void *)regs, BAR0_SIZE);
    return rc;
}
