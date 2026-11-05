// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
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
#define DEPENDENCY_PRODUCERS 63

_Static_assert(sizeof(struct drm_tarugx_submit_gem) == 224,
               "submit GEM ABI size changed");
_Static_assert(offsetof(struct drm_tarugx_submit_gem, commands) == 32,
               "submit GEM command offset changed");
_Static_assert(sizeof(struct drm_tarugx_submit_gem2) == 232,
               "submit GEM2 ABI size changed");
_Static_assert(offsetof(struct drm_tarugx_submit_gem2, commands) == 40,
               "submit GEM2 command offset changed");
_Static_assert(sizeof(struct drm_tarugx_upload_gem) == 48,
               "upload GEM ABI size changed");
_Static_assert(sizeof(struct drm_tarugx_copy_gem) == 48,
               "copy GEM ABI size changed");

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
    struct drm_tarugx_vram_create front_vram = {
        .width = 1920, .height = 1080, .flags = DRM_TARUGX_VRAM_FRONT,
    };
    struct drm_tarugx_vram_create foreign_front = {
        .width = 1920, .height = 1080, .flags = DRM_TARUGX_VRAM_FRONT,
    };
    struct drm_mode_create_dumb upload_source = {
        .width = 64, .height = 32, .bpp = 16,
    };
    struct drm_mode_map_dumb upload_map_args;
    struct drm_tarugx_upload_gem upload = {
        .source_x = 4, .source_y = 2,
        .destination_x = 100, .destination_y = 80,
        .width = 32, .height = 16,
    };
    struct drm_tarugx_vram_create copy_source = {
        .width = 64, .height = 32,
    };
    struct drm_tarugx_copy_gem copy = {
        .source_x = 0, .source_y = 0,
        .destination_x = 200, .destination_y = 120,
        .width = 32, .height = 16,
    };
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
    struct drm_tarugx_vram_create dependency_vram = {
        .width = 1920, .height = 1080,
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
    struct drm_tarugx_submit_gem2 dependency_source = {
        .count = 1,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .commands = {
            { .opcode = DRM_TARUGX_OP_BLIT },
        },
    };
    struct drm_tarugx_submit_gem2 dependent_batch = {
        .count = 2,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .commands = {
            { .opcode = DRM_TARUGX_OP_FILL,
              .dst_x = 0, .dst_y = 0, .width = 64, .height = 32,
              .color = 0xff30b070 },
            { .opcode = DRM_TARUGX_OP_BLIT,
              .src_x = 0, .src_y = 0, .dst_x = 64, .dst_y = 0,
              .width = 64, .height = 32 },
        },
    };
    struct drm_tarugx_submit_gem2 invalid_dependency;
    struct drm_tarugx_submit_gem2 overflow_batch;
    volatile uint32_t *vram_map;
    volatile uint16_t *vram_pixels;
    uint16_t *upload_pixels;
    uint32_t fb_id = 0;
    uint32_t syncobj = 0;
    uint32_t timeline_syncobj = 0;
    uint64_t timeline_point = 7;
    uint32_t fb_handles[4] = { 0 };
    uint32_t fb_pitches[4] = { 0 };
    uint32_t fb_offsets[4] = { 0 };
    struct timespec started, returned, stress_started, stress_finished;
    struct timespec cpu_started, cpu_finished;
    double submit_ms, dependency_submit_ms, stress_s, cpu_s;
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
    if (expect_errno(other, DRM_IOCTL_TARUGX_VRAM_CREATE,
                     &foreign_front, EACCES))
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
    if (ioctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &front_vram)) {
        perror("DRM_IOCTL_TARUGX_VRAM_CREATE front buffer");
        goto out_other;
    }
    vram_map = mmap(NULL, front_vram.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, (off_t)front_vram.mmap_offset);
    if (vram_map == MAP_FAILED) {
        perror("mmap front-buffer VRAM GEM");
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    vram_map[0] = 0x12345678;
    if (vram_map[0] != 0x12345678) {
        fputs("front-buffer VRAM GEM mmap readback mismatch\n", stderr);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &upload_source)) {
        perror("create partial-DMA source GEM");
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    upload_map_args = (struct drm_mode_map_dumb) {
        .handle = upload_source.handle,
    };
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &upload_map_args)) {
        perror("map partial-DMA source GEM");
        close_handle(fd, upload_source.handle);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    upload_pixels = mmap(NULL, upload_source.size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, (off_t)upload_map_args.offset);
    if (upload_pixels == MAP_FAILED) {
        perror("mmap partial-DMA source GEM");
        close_handle(fd, upload_source.handle);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    for (uint32_t y = 0; y < upload_source.height; y++)
        for (uint32_t x = 0; x < upload_source.width; x++)
            upload_pixels[y * (upload_source.pitch / 2) + x] =
                (uint16_t)(0x4000 + y * upload_source.width + x);
    vram_pixels = (volatile uint16_t *)vram_map;
    for (uint32_t y = 0; y < upload.height; y++)
        for (uint32_t x = 0; x < upload.width; x++)
            vram_pixels[(upload.destination_y + y) * 1920 +
                        upload.destination_x + x] = 0;
    __sync_synchronize();
    upload.source_handle = upload_source.handle;
    upload.destination_handle = front_vram.handle;
    if (ioctl(fd, DRM_IOCTL_TARUGX_UPLOAD_GEM, &upload)) {
        perror("DRM_IOCTL_TARUGX_UPLOAD_GEM");
        munmap(upload_pixels, upload_source.size);
        close_handle(fd, upload_source.handle);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    for (uint32_t y = 0; y < upload.height; y++) {
        for (uint32_t x = 0; x < upload.width; x++) {
            uint16_t expected = (uint16_t)(0x4000 +
                (upload.source_y + y) * upload_source.width +
                upload.source_x + x);
            uint16_t observed = vram_pixels[
                (upload.destination_y + y) * 1920 +
                upload.destination_x + x];

            if (observed != expected) {
                fprintf(stderr,
                        "partial DMA mismatch at %u,%u: %#x != %#x\n",
                        x, y, observed, expected);
                munmap(upload_pixels, upload_source.size);
                close_handle(fd, upload_source.handle);
                munmap((void *)vram_map, front_vram.size);
                close_handle(fd, front_vram.handle);
                goto out_other;
            }
        }
    }
    {
        struct drm_tarugx_upload_gem invalid_upload = upload;

        invalid_upload.width = 31;
        if (expect_errno(fd, DRM_IOCTL_TARUGX_UPLOAD_GEM,
                         &invalid_upload, EINVAL)) {
            munmap(upload_pixels, upload_source.size);
            close_handle(fd, upload_source.handle);
            munmap((void *)vram_map, front_vram.size);
            close_handle(fd, front_vram.handle);
            goto out_other;
        }
        invalid_upload = upload;
        invalid_upload.destination_handle = upload_source.handle;
        if (expect_errno(fd, DRM_IOCTL_TARUGX_UPLOAD_GEM,
                         &invalid_upload, EINVAL)) {
            munmap(upload_pixels, upload_source.size);
            close_handle(fd, upload_source.handle);
            munmap((void *)vram_map, front_vram.size);
            close_handle(fd, front_vram.handle);
            goto out_other;
        }
    }
    if (ioctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &copy_source)) {
        perror("create cross-GEM blit source");
        munmap(upload_pixels, upload_source.size);
        close_handle(fd, upload_source.handle);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    upload.destination_handle = copy_source.handle;
    upload.destination_x = 0;
    upload.destination_y = 0;
    if (ioctl(fd, DRM_IOCTL_TARUGX_UPLOAD_GEM, &upload)) {
        perror("upload cross-GEM blit source");
        close_handle(fd, copy_source.handle);
        munmap(upload_pixels, upload_source.size);
        close_handle(fd, upload_source.handle);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    copy.source_handle = copy_source.handle;
    copy.destination_handle = front_vram.handle;
    for (uint32_t y = 0; y < copy.height; y++)
        for (uint32_t x = 0; x < copy.width; x++)
            vram_pixels[(copy.destination_y + y) * 1920 +
                        copy.destination_x + x] = 0;
    __sync_synchronize();
    if (ioctl(fd, DRM_IOCTL_TARUGX_COPY_GEM, &copy)) {
        perror("DRM_IOCTL_TARUGX_COPY_GEM");
        close_handle(fd, copy_source.handle);
        munmap(upload_pixels, upload_source.size);
        close_handle(fd, upload_source.handle);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    for (uint32_t y = 0; y < copy.height; y++) {
        for (uint32_t x = 0; x < copy.width; x++) {
            uint16_t expected = (uint16_t)(0x4000 +
                (upload.source_y + y) * upload_source.width +
                upload.source_x + x);
            uint16_t observed = vram_pixels[
                (copy.destination_y + y) * 1920 +
                copy.destination_x + x];

            if (observed != expected) {
                fprintf(stderr,
                        "cross-GEM blit mismatch at %u,%u: %#x != %#x\n",
                        x, y, observed, expected);
                close_handle(fd, copy_source.handle);
                munmap(upload_pixels, upload_source.size);
                close_handle(fd, upload_source.handle);
                munmap((void *)vram_map, front_vram.size);
                close_handle(fd, front_vram.handle);
                goto out_other;
            }
        }
    }
    {
        struct drm_tarugx_copy_gem invalid_copy = copy;

        invalid_copy.source_handle = upload_source.handle;
        if (expect_errno(fd, DRM_IOCTL_TARUGX_COPY_GEM,
                         &invalid_copy, EINVAL)) {
            close_handle(fd, copy_source.handle);
            munmap(upload_pixels, upload_source.size);
            close_handle(fd, upload_source.handle);
            munmap((void *)vram_map, front_vram.size);
            close_handle(fd, front_vram.handle);
            goto out_other;
        }
    }
    if (close_handle(fd, copy_source.handle)) {
        perror("close cross-GEM blit source");
        munmap(upload_pixels, upload_source.size);
        close_handle(fd, upload_source.handle);
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    puts("VRAM cross-GEM blit PASS: offscreen source copied to front buffer");
    munmap(upload_pixels, upload_source.size);
    if (close_handle(fd, upload_source.handle)) {
        perror("close partial-DMA source GEM");
        munmap((void *)vram_map, front_vram.size);
        close_handle(fd, front_vram.handle);
        goto out_other;
    }
    puts("VRAM partial DMA PASS: aligned host GEM rectangle and validation");
    munmap((void *)vram_map, front_vram.size);
    if (close_handle(fd, front_vram.handle)) {
        perror("close front-buffer VRAM GEM");
        goto out_other;
    }
    front_vram.handle = 0;
    puts("VRAM front-buffer GEM PASS: master-only BAR2 mmap and reuse");
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
    invalid_sync_batch = render_batch;
    invalid_sync_batch.handle = render_vram.handle;
    invalid_sync_batch.out_point = timeline_point;
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
    if (drmSyncobjCreate(render, 0, &timeline_syncobj)) {
        perror("drmSyncobjCreate timeline");
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
    /* A standard binary syncobj wait must be sufficient to submit a timeline
     * batch; userspace must not need the private WAIT ioctl to clear state. */
    render_batch.out_syncobj = timeline_syncobj;
    render_batch.out_point = timeline_point;
    if (ioctl(render, DRM_IOCTL_TARUGX_SUBMIT_GEM, &render_batch)) {
        perror("render-node timeline submit after binary syncobj wait");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (drmSyncobjTimelineWait(render, &timeline_syncobj, &timeline_point, 1,
                               INT64_MAX, 0, NULL)) {
        perror("drmSyncobjTimelineWait");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    timeline_point = 0;
    if (drmSyncobjQuery(render, &timeline_syncobj, &timeline_point, 1) ||
        timeline_point < 7) {
        fprintf(stderr, "timeline query did not reach point 7 (point=%llu)\n",
                (unsigned long long)timeline_point);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    timeline_point = 11;
    render_batch.out_point = timeline_point;
    if (ioctl(render, DRM_IOCTL_TARUGX_SUBMIT_GEM, &render_batch)) {
        perror("render-node second timeline submit");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (drmSyncobjTimelineWait(render, &timeline_syncobj, &timeline_point, 1,
                               INT64_MAX, 0, NULL)) {
        perror("drmSyncobjTimelineWait second point");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    timeline_point = 0;
    if (drmSyncobjQuery(render, &timeline_syncobj, &timeline_point, 1) ||
        timeline_point < 11) {
        fprintf(stderr, "timeline query did not reach point 11 (point=%llu)\n",
                (unsigned long long)timeline_point);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (ioctl(render, DRM_IOCTL_TARUGX_VRAM_CREATE, &dependency_vram)) {
        if (errno != EINVAL) {
            perror("dependency VRAM create");
            close_handle(render, render_vram.handle);
            goto out_render;
        }
        dependency_vram = (struct drm_tarugx_vram_create) {
            .width = 640, .height = 480,
        };
        if (ioctl(render, DRM_IOCTL_TARUGX_VRAM_CREATE,
                  &dependency_vram)) {
            perror("fallback dependency VRAM create");
            close_handle(render, render_vram.handle);
            goto out_render;
        }
    }
    invalid_dependency = dependent_batch;
    invalid_dependency.handle = dependency_vram.handle;
    invalid_dependency.in_syncobj = UINT32_MAX;
    if (expect_errno(render, DRM_IOCTL_TARUGX_SUBMIT_GEM2,
                     &invalid_dependency, ENOENT)) {
        close_handle(render, dependency_vram.handle);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    invalid_dependency.in_syncobj = 0;
    invalid_dependency.in_point = 1;
    if (expect_errno(render, DRM_IOCTL_TARUGX_SUBMIT_GEM2,
                     &invalid_dependency, EINVAL)) {
        close_handle(render, dependency_vram.handle);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    dependency_source.handle = dependency_vram.handle;
    dependency_source.commands[0].width = dependency_vram.width;
    dependency_source.commands[0].height = dependency_vram.height;
    dependency_source.out_syncobj = timeline_syncobj;
    dependency_source.out_point = 20;
    for (iteration = 0; iteration < DEPENDENCY_PRODUCERS; iteration++) {
        dependency_source.out_point = 20 + iteration;
        if (ioctl(render, DRM_IOCTL_TARUGX_SUBMIT_GEM2,
                  &dependency_source)) {
            perror("dependency producer submit");
            close_handle(render, dependency_vram.handle);
            close_handle(render, render_vram.handle);
            goto out_render;
        }
    }
    dependent_batch.handle = dependency_vram.handle;
    dependent_batch.in_syncobj = timeline_syncobj;
    dependent_batch.in_point = 20 + DEPENDENCY_PRODUCERS - 1;
    dependent_batch.out_syncobj = timeline_syncobj;
    dependent_batch.out_point = 20 + DEPENDENCY_PRODUCERS;
    clock_gettime(CLOCK_MONOTONIC, &started);
    if (ioctl(render, DRM_IOCTL_TARUGX_SUBMIT_GEM2, &dependent_batch)) {
        perror("dependency-aware GEM2 submit");
        close_handle(render, dependency_vram.handle);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    clock_gettime(CLOCK_MONOTONIC, &returned);
    dependency_submit_ms = (returned.tv_sec - started.tv_sec) * 1000.0 +
                           (returned.tv_nsec - started.tv_nsec) / 1000000.0;
    if (dependency_submit_ms >= 4.0) {
        fprintf(stderr, "scheduler submit blocked for %.3f ms\n",
                dependency_submit_ms);
        close_handle(render, dependency_vram.handle);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    overflow_batch = dependent_batch;
    overflow_batch.in_point = dependent_batch.out_point;
    overflow_batch.out_point = dependent_batch.out_point + 1;
    if (expect_errno(render, DRM_IOCTL_TARUGX_SUBMIT_GEM2,
                     &overflow_batch, EBUSY)) {
        close_handle(render, dependency_vram.handle);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    timeline_point = 20 + DEPENDENCY_PRODUCERS;
    if (drmSyncobjTimelineWait(render, &timeline_syncobj, &timeline_point, 1,
                               INT64_MAX, 0, NULL)) {
        perror("dependency output timeline wait");
        close_handle(render, dependency_vram.handle);
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    if (close_handle(render, dependency_vram.handle)) {
        perror("close dependency VRAM GEM");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    usleep(10000);
    /* Reap the completed timeline-backed record on the next private submit. */
    render_batch.out_syncobj = 0;
    render_batch.out_point = 0;
    if (ioctl(render, DRM_IOCTL_TARUGX_SUBMIT_GEM, &render_batch)) {
        perror("render-node resubmit after timeline wait");
        close_handle(render, render_vram.handle);
        goto out_render;
    }
    wait.fence = render_batch.fence;
    if (ioctl(render, DRM_IOCTL_TARUGX_WAIT, &wait)) {
        perror("render-node DRM_IOCTL_TARUGX_WAIT after resubmit");
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
    if (drmSyncobjDestroy(render, timeline_syncobj)) {
        perror("drmSyncobjDestroy timeline");
        goto out_render;
    }
    timeline_syncobj = 0;
    close(render);
    render = -1;
    puts("DRM render node PASS: non-master GEM create, binary/multi-point timeline "
         "syncobj + sync_file resubmit and private fence fill/blit; "
         "screen fill denied");
    printf("DRM scheduler dependency PASS: GEM2 returned in %.3f ms with "
           "point %u queued; %u producer/dependent batches signaled through "
           "point %u; overflow batch rejected with EBUSY\n", dependency_submit_ms,
           20 + DEPENDENCY_PRODUCERS - 1, DEPENDENCY_PRODUCERS + 1,
           20 + DEPENDENCY_PRODUCERS);
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
    if (timeline_syncobj)
        drmSyncobjDestroy(render, timeline_syncobj);
    close(render);
    close(other);
out_drop:
    drmDropMaster(fd);
out_close:
    close(fd);
    return result;
}
