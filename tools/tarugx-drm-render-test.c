// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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
        .x = UINT32_MAX, .y = 0, .width = 2, .height = 1,
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
    struct drm_tarugx_vram_create other_vram = { .width = 32, .height = 32 };
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
    struct drm_tarugx_vram_create render_vram = {
        .width = 64, .height = 64,
    };
    struct drm_tarugx_submit_gem render_batch = {
        .count = 2,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .commands = {
            {
                .opcode = DRM_TARUGX_OP_FILL,
                .dst_x = 0, .dst_y = 0, .width = 32, .height = 16,
                .color = 0xff6090d0,
            },
            {
                .opcode = DRM_TARUGX_OP_BLIT,
                .src_x = 0, .src_y = 0, .dst_x = 32, .dst_y = 0,
                .width = 32, .height = 16,
            },
        },
    };
    struct drm_tarugx_submit_gem foreign_batch;
    struct drm_tarugx_submit_gem invalid_sync_batch;
    volatile uint32_t *vram_map;
    uint32_t fb_id = 0;
    uint32_t syncobj = 0;
    uint32_t fb_handles[4] = { 0 };
    uint32_t fb_pitches[4] = { 0 };
    uint32_t fb_offsets[4] = { 0 };
    struct timespec started, returned, stress_started, stress_finished;
    struct timespec cpu_started, cpu_finished;
    double submit_ms, stress_s, cpu_s;
    unsigned int iteration;
    int fd, other, render = -1, render_other = -1, sync_file = -1;
    int result = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s /dev/dri/cardN /dev/dri/renderDN\n",
                argv[0]);
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
    if (ioctl(other, DRM_IOCTL_TARUGX_VRAM_CREATE, &other_vram)) {
        perror("authenticated non-master VRAM create");
        goto out_other;
    }
    if (close_handle(other, other_vram.handle)) {
        perror("close authenticated non-master VRAM GEM");
        goto out_other;
    }
    if (expect_errno(fd, DRM_IOCTL_TARUGX_FILL, &invalid, EINVAL))
        goto out_other;
    if (ioctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &vram)) {
        if (errno != ENOSPC) {
            perror("DRM_IOCTL_TARUGX_VRAM_CREATE");
            goto out_other;
        }
        puts("VRAM GEM SKIP: no CPU-mappable VRAM remains in this mode");
    } else {
    vram_map = mmap(NULL, vram.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, (off_t)vram.mmap_offset);
    if (vram_map == MAP_FAILED) {
        if (errno == EOPNOTSUPP) {
            puts("VRAM GEM CPU mmap SKIP: object is outside the PCIe BAR");
            close_handle(fd, vram.handle);
            goto vram_gem_done;
        }
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
    }
vram_gem_done:
    render = open(argv[2], O_RDWR | O_CLOEXEC);
    if (render < 0) {
        perror("open TaruGX render node");
        goto out_other;
    }
    if (expect_errno(render, DRM_IOCTL_TARUGX_FILL, &fill, EACCES))
        goto out_render;
    if (ioctl(render, DRM_IOCTL_TARUGX_VRAM_CREATE, &render_vram)) {
        perror("render-node DRM_IOCTL_TARUGX_VRAM_CREATE");
        goto out_render;
    }
    invalid_sync_batch = render_batch;
    invalid_sync_batch.handle = render_vram.handle;
    invalid_sync_batch.flags = 0;
    invalid_sync_batch.out_syncobj = 1;
    if (expect_errno(render, DRM_IOCTL_TARUGX_SUBMIT_GEM,
                     &invalid_sync_batch, EINVAL)) {
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (drmSyncobjCreate(render, 0, &syncobj)) {
        perror("drmSyncobjCreate");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    invalid_sync_batch = render_batch;
    invalid_sync_batch.handle = UINT32_MAX;
    invalid_sync_batch.out_syncobj = syncobj;
    if (expect_errno(render, DRM_IOCTL_TARUGX_SUBMIT_GEM,
                     &invalid_sync_batch, ENOENT)) {
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    render_batch.handle = render_vram.handle;
    render_batch.out_syncobj = syncobj;
    if (ioctl(render, DRM_IOCTL_TARUGX_SUBMIT_GEM, &render_batch)) {
        perror("render-node DRM_IOCTL_TARUGX_SUBMIT_GEM");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (drmSyncobjExportSyncFile(render, syncobj, &sync_file)) {
        perror("drmSyncobjExportSyncFile");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    render_other = open(argv[2], O_RDWR | O_CLOEXEC);
    if (render_other < 0) {
        perror("open second TaruGX render node fd");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    foreign_batch = render_batch;
    foreign_batch.out_syncobj = 0;
    if (expect_errno(render_other, DRM_IOCTL_TARUGX_SUBMIT_GEM,
                     &foreign_batch, ENOENT)) {
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    wait = (struct drm_tarugx_wait) {
        .fence = render_batch.fence,
        .timeout_ms = 1000,
    };
    if (expect_errno(render_other, DRM_IOCTL_TARUGX_WAIT, &wait, EPERM)) {
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    close(render_other);
    render_other = -1;
    {
        struct pollfd sync_poll = { .fd = sync_file, .events = POLLIN };

        if (poll(&sync_poll, 1, 1000) != 1 || !(sync_poll.revents & POLLIN)) {
            fputs("exported sync_file did not signal\n", stderr);
            close_handle(render, render_vram.handle);
            goto out_render;
        }
    }
    close(sync_file);
    sync_file = -1;
    if (drmSyncobjWait(render, &syncobj, 1, INT64_MAX, 0, NULL)) {
        perror("drmSyncobjWait");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (ioctl(render, DRM_IOCTL_TARUGX_WAIT, &wait)) {
        perror("render-node DRM_IOCTL_TARUGX_WAIT");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (close_handle(render, render_vram.handle)) {
        perror("close render-node VRAM GEM");
        goto out_render;
    }
    if (drmSyncobjDestroy(render, syncobj)) {
        perror("drmSyncobjDestroy");
        goto out_render;
    }
    syncobj = 0;
    close(render);
    render = -1;
    puts("DRM render node PASS: non-master GEM create, syncobj/sync_file + "
         "private fence fill/blit; screen fill denied");
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
    goto out_drop;
out_render:
    if (sync_file >= 0)
        close(sync_file);
    if (render_other >= 0)
        close(render_other);
    if (syncobj)
        drmSyncobjDestroy(render, syncobj);
    close(render);
    close(other);
out_drop:
    drmDropMaster(fd);
out_close:
    close(fd);
    return result;
}
