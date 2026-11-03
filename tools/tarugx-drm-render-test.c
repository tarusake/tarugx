// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "tarugx_drm.h"

#define STRESS_BATCHES 120

static int close_handle(int fd, uint32_t handle)
{
    struct drm_gem_close close_args = { .handle = handle };

    return ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_args);
}

static int expect_errno(int fd, unsigned long request, void *data, int error)
{
    errno = 0;
    if (ioctl(fd, request, data) != -1 || errno != error) {
        fprintf(stderr, "expected errno %d, got %d (%s)\n",
                error, errno, strerror(errno));
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct drm_tarugx_fill fill = {
        .x = 16, .y = 16, .width = 32, .height = 16,
        .color = 0xff2050d0,
    };
    struct drm_tarugx_blit blit = {
        .src_x = 16, .src_y = 16, .dst_x = 64, .dst_y = 16,
        .width = 32, .height = 16,
    };
    struct drm_tarugx_fill invalid = {
        .x = 639, .y = 0, .width = 2, .height = 1,
    };
    struct drm_tarugx_submit batch = {
        .count = 4,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .commands = {
            {
                .opcode = DRM_TARUGX_OP_FILL,
                .dst_x = 0, .dst_y = 100, .width = 320, .height = 180,
                .color = 0xff40c060,
            },
            {
                .opcode = DRM_TARUGX_OP_BLIT,
                .src_x = 0, .src_y = 100, .dst_x = 320, .dst_y = 100,
                .width = 320, .height = 180,
            },
            {
                .opcode = DRM_TARUGX_OP_FILL,
                .dst_x = 0, .dst_y = 300, .width = 320, .height = 180,
                .color = 0xffd08020,
            },
            {
                .opcode = DRM_TARUGX_OP_BLIT,
                .src_x = 0, .src_y = 300, .dst_x = 320, .dst_y = 300,
                .width = 320, .height = 180,
            },
        },
    };
    struct drm_tarugx_wait wait;
    struct drm_tarugx_vram_create vram = { .width = 640, .height = 480 };
    struct drm_tarugx_vram_create too_large = {
        .width = 640, .height = 480,
    };
    struct drm_tarugx_submit_gem gem_batch = {
        .count = 2,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .commands = {
            {
                .opcode = DRM_TARUGX_OP_FILL,
                .dst_x = 10, .dst_y = 10, .width = 64, .height = 32,
                .color = 0xff30b070,
            },
            {
                .opcode = DRM_TARUGX_OP_BLIT,
                .src_x = 10, .src_y = 10, .dst_x = 100, .dst_y = 10,
                .width = 64, .height = 32,
            },
        },
    };
    volatile uint32_t *vram_map;
    uint32_t fb_id = 0;
    uint32_t fb_handles[4] = { 0 };
    uint32_t fb_pitches[4] = { 0 };
    uint32_t fb_offsets[4] = { 0 };
    struct timespec started, returned, stress_started, stress_finished;
    struct timespec cpu_started, cpu_finished;
    double submit_ms, stress_s, cpu_s;
    unsigned int iteration;
    int fd, other, result = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/dri/cardN\n", argv[0]);
        return 2;
    }
    fd = open(argv[1], O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open DRM device");
        return 1;
    }
    if (drmSetMaster(fd)) {
        perror("drmSetMaster");
        goto out_close;
    }

    other = open(argv[1], O_RDWR | O_CLOEXEC);
    if (other < 0) {
        perror("open second DRM file");
        goto out_drop;
    }
    if (expect_errno(other, DRM_IOCTL_TARUGX_FILL, &fill, EACCES))
        goto out_other;
    if (expect_errno(other, DRM_IOCTL_TARUGX_SUBMIT, &batch, EACCES))
        goto out_other;
    if (expect_errno(other, DRM_IOCTL_TARUGX_VRAM_CREATE, &vram, EACCES))
        goto out_other;
    if (expect_errno(fd, DRM_IOCTL_TARUGX_FILL, &invalid, EINVAL))
        goto out_other;
    if (ioctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &vram)) {
        perror("DRM_IOCTL_TARUGX_VRAM_CREATE");
        goto out_other;
    }
    vram_map = mmap(NULL, vram.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, (off_t)vram.mmap_offset);
    if (vram_map == MAP_FAILED) {
        perror("mmap VRAM GEM");
        close_handle(fd, vram.handle);
        goto out_other;
    }
    vram_map[0] = 0xfeed1234;
    vram_map[vram.size / sizeof(*vram_map) - 1] = 0xcafe5678;
    if (vram_map[0] != 0xfeed1234 ||
        vram_map[vram.size / sizeof(*vram_map) - 1] != 0xcafe5678) {
        fputs("VRAM GEM mmap readback mismatch\n", stderr);
        munmap((void *)vram_map, vram.size);
        close_handle(fd, vram.handle);
        goto out_other;
    }
    fb_handles[0] = vram.handle;
    fb_pitches[0] = vram.pitch;
    if (drmModeAddFB2(fd, 640, 480, DRM_FORMAT_XRGB8888,
                      fb_handles, fb_pitches, fb_offsets, &fb_id, 0)) {
        perror("drmModeAddFB2 for VRAM GEM");
        munmap((void *)vram_map, vram.size);
        close_handle(fd, vram.handle);
        goto out_other;
    }
    if (drmModeRmFB(fd, fb_id)) {
        perror("drmModeRmFB for VRAM GEM");
        munmap((void *)vram_map, vram.size);
        close_handle(fd, vram.handle);
        goto out_other;
    }
    fb_id = 0;
    if (expect_errno(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &too_large, ENOSPC)) {
        munmap((void *)vram_map, vram.size);
        close_handle(fd, vram.handle);
        goto out_other;
    }
    gem_batch.handle = vram.handle;
    if (ioctl(fd, DRM_IOCTL_TARUGX_SUBMIT_GEM, &gem_batch)) {
        perror("DRM_IOCTL_TARUGX_SUBMIT_GEM");
        munmap((void *)vram_map, vram.size);
        close_handle(fd, vram.handle);
        goto out_other;
    }
    munmap((void *)vram_map, vram.size);
    if (close_handle(fd, vram.handle)) {
        perror("DRM_IOCTL_GEM_CLOSE");
        goto out_other;
    }
    wait = (struct drm_tarugx_wait) {
        .fence = gem_batch.fence,
        .timeout_ms = 1000,
    };
    if (ioctl(fd, DRM_IOCTL_TARUGX_WAIT, &wait)) {
        perror("DRM_IOCTL_TARUGX_WAIT for GEM");
        goto out_other;
    }
    vram = (struct drm_tarugx_vram_create) {
        .width = 640, .height = 480,
    };
    if (ioctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &vram)) {
        perror("VRAM GEM reallocation");
        goto out_other;
    }
    if (close_handle(fd, vram.handle)) {
        perror("close reallocated VRAM GEM");
        goto out_other;
    }
    puts("VRAM GEM PASS: mmap/readback, DRM framebuffer, async fill/blit, "
         "close lifetime, ENOSPC and reuse");
    if (ioctl(fd, DRM_IOCTL_TARUGX_FILL, &fill)) {
        perror("DRM_IOCTL_TARUGX_FILL");
        goto out_other;
    }
    if (ioctl(fd, DRM_IOCTL_TARUGX_BLIT, &blit)) {
        perror("DRM_IOCTL_TARUGX_BLIT");
        goto out_other;
    }
    clock_gettime(CLOCK_MONOTONIC, &started);
    if (ioctl(fd, DRM_IOCTL_TARUGX_SUBMIT, &batch)) {
        perror("DRM_IOCTL_TARUGX_SUBMIT");
        goto out_other;
    }
    clock_gettime(CLOCK_MONOTONIC, &returned);
    submit_ms = (returned.tv_sec - started.tv_sec) * 1000.0 +
                (returned.tv_nsec - started.tv_nsec) / 1000000.0;
    if (expect_errno(fd, DRM_IOCTL_TARUGX_SUBMIT, &batch, EBUSY))
        goto out_other;
    wait = (struct drm_tarugx_wait) {
        .fence = batch.fence,
        .timeout_ms = 1000,
    };
    if (ioctl(fd, DRM_IOCTL_TARUGX_WAIT, &wait)) {
        perror("DRM_IOCTL_TARUGX_WAIT");
        goto out_other;
    }
    if (!fill.fence || !blit.fence || fill.fence == blit.fence) {
        fprintf(stderr, "invalid fences: fill=%u blit=%u\n",
                fill.fence, blit.fence);
        goto out_other;
    }

    if (!batch.fence || batch.fence == blit.fence) {
        fprintf(stderr, "invalid batch fence: %u\n", batch.fence);
        goto out_other;
    }

    printf("DRM render ioctl PASS: master restriction, bounds, fill fence %u, "
           "blit fence %u, async four-command batch fence %u returned in "
           "%.3f ms\n", fill.fence, blit.fence, batch.fence, submit_ms);
    clock_gettime(CLOCK_MONOTONIC, &stress_started);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_started);
    for (iteration = 0; iteration < STRESS_BATCHES; iteration++) {
        if (ioctl(fd, DRM_IOCTL_TARUGX_SUBMIT, &batch)) {
            perror("DRM_IOCTL_TARUGX_SUBMIT during stress");
            goto out_other;
        }
        wait.fence = batch.fence;
        if (ioctl(fd, DRM_IOCTL_TARUGX_WAIT, &wait)) {
            perror("DRM_IOCTL_TARUGX_WAIT during stress");
            goto out_other;
        }
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_finished);
    clock_gettime(CLOCK_MONOTONIC, &stress_finished);
    stress_s = stress_finished.tv_sec - stress_started.tv_sec +
               (stress_finished.tv_nsec - stress_started.tv_nsec) / 1e9;
    cpu_s = cpu_finished.tv_sec - cpu_started.tv_sec +
            (cpu_finished.tv_nsec - cpu_started.tv_nsec) / 1e9;
    printf("DRM async stress PASS: %u batches, %u commands, %.3f s, "
           "%.2f batches/s, CPU %.1f%%\n", STRESS_BATCHES,
           STRESS_BATCHES * 4, stress_s, STRESS_BATCHES / stress_s,
           cpu_s * 100.0 / stress_s);
    if (ioctl(fd, DRM_IOCTL_TARUGX_SUBMIT, &batch)) {
        perror("DRM_IOCTL_TARUGX_SUBMIT for postclose test");
        goto out_other;
    }
    printf("DRM render postclose cleanup pending fence %u\n", batch.fence);
    result = 0;
out_other:
    close(other);
out_drop:
    drmDropMaster(fd);
out_close:
    close(fd);
    return result;
}
