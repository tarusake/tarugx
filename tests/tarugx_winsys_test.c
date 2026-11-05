// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "tarugx_winsys.h"

int
main(int argc, char **argv)
{
    struct tarugx_winsys winsys = { .fd = -1 };
    struct tarugx_winsys_surface surface = {0};
    const struct tarugx_winsys_triangle triangles[2] = {{
        .x0 = 0,
        .y0 = 0,
        .x1 = 4 << 8,
        .y1 = 0,
        .x2 = 0,
        .y2 = 4 << 8,
        .color = 0x0000ff00,
    }, {
        .x0 = 4 << 8,
        .y0 = 4 << 8,
        .x1 = 8 << 8,
        .y1 = 4 << 8,
        .x2 = 4 << 8,
        .y2 = 8 << 8,
        .color = 0x00ff0000,
    }};
    int fd;
    int result = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/dri/renderDN\n", argv[0]);
        return 2;
    }
    fd = open(argv[1], O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open render node");
        return 1;
    }
    if (!tarugx_winsys_open(&winsys, fd)) {
        fputs("TaruGX winsys open failed\n", stderr);
        goto out_fd;
    }
    if (!tarugx_winsys_surface_create(&winsys, 64, 64, &surface)) {
        fputs("TaruGX winsys surface create failed\n", stderr);
        goto out_winsys;
    }
    if (!tarugx_winsys_submit_fill(&winsys, &surface, 0x000000ff) ||
        !tarugx_winsys_wait(&winsys, INT64_MAX)) {
        fputs("TaruGX winsys fill submit failed\n", stderr);
        goto out_surface;
    }
    if (!tarugx_winsys_submit_triangles(&winsys, &surface, triangles, 2)) {
        fputs("TaruGX winsys triangle submit failed\n", stderr);
        goto out_surface;
    }
    if (!tarugx_winsys_wait(&winsys, INT64_MAX)) {
        fputs("TaruGX winsys fence wait failed\n", stderr);
        goto out_surface;
    }
    printf("TaruGX Mesa winsys PASS: gpu=%u.%u caps=0x%016llx; "
           "VRAM resource, fill/triangle submit and syncobj fence\n",
           winsys.gpu_version >> 16, winsys.gpu_version & 0xffff,
           (unsigned long long)winsys.caps);
    result = 0;
out_surface:
    tarugx_winsys_surface_destroy(&winsys, &surface);
out_winsys:
    tarugx_winsys_close(&winsys);
out_fd:
    close(fd);
    return result;
}
