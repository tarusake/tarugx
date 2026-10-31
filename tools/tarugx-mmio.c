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

enum { BAR0_SIZE = 0x10000 };

static void usage(const char *name)
{
    fprintf(stderr,
            "usage: %s BDF read OFFSET\n"
            "       %s BDF write OFFSET VALUE\n"
            "       %s BDF identify\n",
            name, name, name);
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno || !end || *end || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    char path[256];
    uint32_t offset = 0, value = 0;
    int write_access, fd, rc = EXIT_FAILURE;
    volatile uint32_t *bar;

    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    write_access = strcmp(argv[2], "write") == 0;
    if (strcmp(argv[2], "identify") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    } else if (strcmp(argv[2], "read") == 0) {
        if (argc != 4 || parse_u32(argv[3], &offset)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    } else if (write_access) {
        if (argc != 5 || parse_u32(argv[3], &offset) ||
            parse_u32(argv[4], &value)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    } else {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if ((offset & 3) || offset >= BAR0_SIZE) {
        fprintf(stderr, "offset must be aligned and below 0x%x\n", BAR0_SIZE);
        return EXIT_FAILURE;
    }

    if (snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0",
                 argv[1]) >= (int)sizeof(path)) {
        fprintf(stderr, "BDF is too long\n");
        return EXIT_FAILURE;
    }

    fd = open(path, write_access ? O_RDWR | O_SYNC : O_RDONLY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return EXIT_FAILURE;
    }

    bar = mmap(NULL, BAR0_SIZE, write_access ? PROT_READ | PROT_WRITE : PROT_READ,
               MAP_SHARED, fd, 0);
    if (bar == MAP_FAILED) {
        fprintf(stderr, "mmap %s: %s\n", path, strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[2], "identify") == 0) {
        printf("GPU_ID      = 0x%08" PRIx32 "\n", bar[0x0000 / 4]);
        printf("GPU_VERSION = 0x%08" PRIx32 "\n", bar[0x0004 / 4]);
        printf("GPU_STATUS  = 0x%08" PRIx32 "\n", bar[0x0008 / 4]);
        rc = bar[0] == UINT32_C(0x54475055) ? EXIT_SUCCESS : 2;
    } else if (write_access) {
        bar[offset / 4] = value;
        __sync_synchronize();
        printf("[0x%04" PRIx32 "] <- 0x%08" PRIx32 "\n", offset, value);
        rc = EXIT_SUCCESS;
    } else {
        printf("[0x%04" PRIx32 "] = 0x%08" PRIx32 "\n", offset,
               bar[offset / 4]);
        rc = EXIT_SUCCESS;
    }

    munmap((void *)bar, BAR0_SIZE);
    close(fd);
    return rc;
}
