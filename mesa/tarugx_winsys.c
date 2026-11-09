// SPDX-License-Identifier: MIT
#include "tarugx_winsys.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "tarugx_drm.h"

static bool
tarugx_winsys_is_device(int fd)
{
    drmVersionPtr version = drmGetVersion(fd);
    bool matches = version && version->name_len == 6 &&
                   !memcmp(version->name, "tarugx", 6);

    drmFreeVersion(version);
    return matches;
}

bool
tarugx_winsys_open(struct tarugx_winsys *winsys, int fd)
{
    struct drm_tarugx_get_param parameter;
    int duplicate;

    if (!winsys || fd < 0 || !tarugx_winsys_is_device(fd))
        return false;
    duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0)
        return false;
    memset(winsys, 0, sizeof(*winsys));
    winsys->fd = duplicate;
    /* Drivers predating GET_PARAM already support flat triangles.  Keep that
     * path available, but require an explicit capability for interpolation. */
    winsys->caps = DRM_TARUGX_CAP_TRIANGLE;
    parameter = (struct drm_tarugx_get_param) {
        .param = DRM_TARUGX_PARAM_GPU_VERSION,
    };
    if (!drmIoctl(duplicate, DRM_IOCTL_TARUGX_GET_PARAM, &parameter))
        winsys->gpu_version = (uint32_t)parameter.value;
    parameter = (struct drm_tarugx_get_param) {
        .param = DRM_TARUGX_PARAM_CAPS,
    };
    if (!drmIoctl(duplicate, DRM_IOCTL_TARUGX_GET_PARAM, &parameter))
        winsys->caps = parameter.value;
    if (drmSyncobjCreate(duplicate, 0, &winsys->submit_syncobj)) {
        close(duplicate);
        winsys->fd = -1;
        return false;
    }
    return true;
}

bool
tarugx_winsys_has_cap(const struct tarugx_winsys *winsys,
                      uint64_t capability)
{
    return winsys && (winsys->caps & capability) == capability;
}

bool
tarugx_winsys_supports_triangle_interpolation(
    const struct tarugx_winsys *winsys)
{
    return tarugx_winsys_has_cap(
        winsys, DRM_TARUGX_CAP_TRIANGLE_INTERPOLATION);
}

bool
tarugx_winsys_supports_depth(const struct tarugx_winsys *winsys)
{
    return tarugx_winsys_has_cap(winsys, DRM_TARUGX_CAP_Z16);
}

bool
tarugx_winsys_supports_texture(const struct tarugx_winsys *winsys)
{
    return tarugx_winsys_has_cap(winsys, DRM_TARUGX_CAP_TEXTURE_RGB565);
}

void
tarugx_winsys_close(struct tarugx_winsys *winsys)
{
    if (!winsys || winsys->fd < 0)
        return;
    if (winsys->submit_syncobj)
        drmSyncobjDestroy(winsys->fd, winsys->submit_syncobj);
    close(winsys->fd);
    winsys->fd = -1;
    winsys->submit_syncobj = 0;
}

static bool
tarugx_winsys_surface_create_flags(struct tarugx_winsys *winsys,
                                   uint32_t width, uint32_t height,
                                   uint32_t flags,
                                   struct tarugx_winsys_surface *surface)
{
    struct drm_tarugx_vram_create create = {
        .width = width,
        .height = height,
        .flags = flags,
    };

    if (!winsys || winsys->fd < 0 || !surface || !width || !height)
        return false;
    if (drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &create))
        return false;
    *surface = (struct tarugx_winsys_surface) {
        .handle = create.handle,
        .width = width,
        .height = height,
        .pitch = create.pitch,
        .size = create.size,
        .depth_offset = create.depth_offset,
        .depth = create.depth_offset != 0,
    };
    return true;
}

bool
tarugx_winsys_surface_create(struct tarugx_winsys *winsys,
                             uint32_t width, uint32_t height,
                             struct tarugx_winsys_surface *surface)
{
    uint32_t flags = tarugx_winsys_supports_depth(winsys) ?
                     DRM_TARUGX_VRAM_DEPTH : 0;

    return tarugx_winsys_surface_create_flags(winsys, width, height, flags,
                                              surface);
}

bool
tarugx_winsys_texture_create(struct tarugx_winsys *winsys,
                             uint32_t width, uint32_t height,
                             struct tarugx_winsys_surface *surface)
{
    if (!tarugx_winsys_supports_texture(winsys))
        return false;
    return tarugx_winsys_surface_create_flags(winsys, width, height, 0,
                                              surface);
}

bool
tarugx_winsys_surface_query(struct tarugx_winsys *winsys, uint32_t handle,
                            struct tarugx_winsys_surface *surface)
{
    struct drm_tarugx_vram_info info = { .handle = handle };

    if (!winsys || winsys->fd < 0 || !handle || !surface ||
        drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_VRAM_INFO, &info))
        return false;
    *surface = (struct tarugx_winsys_surface) {
        .handle = handle,
        .width = info.width,
        .height = info.height,
        .pitch = info.pitch,
        .size = (uint64_t)info.pitch * info.height,
        .depth_offset = info.depth_offset,
        .depth = (info.flags & DRM_TARUGX_VRAM_DEPTH) != 0,
    };
    return true;
}

void
tarugx_winsys_surface_destroy(struct tarugx_winsys *winsys,
                              struct tarugx_winsys_surface *surface)
{
    struct drm_gem_close close_args;

    if (!winsys || winsys->fd < 0 || !surface || !surface->handle)
        return;
    close_args = (struct drm_gem_close) { .handle = surface->handle };
    drmIoctl(winsys->fd, DRM_IOCTL_GEM_CLOSE, &close_args);
    memset(surface, 0, sizeof(*surface));
}

bool
tarugx_winsys_upload_texture(
    struct tarugx_winsys *winsys, const void *pixels, uint32_t source_stride,
    const struct tarugx_winsys_surface *texture)
{
    struct drm_mode_create_dumb create = {0};
    struct drm_mode_map_dumb map = {0};
    struct drm_mode_destroy_dumb destroy = {0};
    struct drm_tarugx_upload_gem upload = {0};
    uint8_t *destination = MAP_FAILED;
    const uint8_t *source = pixels;
    uint32_t row_bytes;
    uint32_t y;
    bool result = false;

    if (!winsys || winsys->fd < 0 || !pixels || !texture ||
        !texture->handle || !texture->width || !texture->height ||
        (texture->width & 1) || texture->width > UINT32_MAX / 2)
        return false;
    row_bytes = texture->width * 2;
    if (source_stride < row_bytes)
        return false;
    create.width = texture->width;
    create.height = texture->height;
    create.bpp = 16;
    if (drmIoctl(winsys->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create))
        return false;
    map.handle = create.handle;
    if (drmIoctl(winsys->fd, DRM_IOCTL_MODE_MAP_DUMB, &map))
        goto out;
    destination = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       winsys->fd, (off_t)map.offset);
    if (destination == MAP_FAILED)
        goto out;
    for (y = 0; y < texture->height; y++)
        memcpy(destination + (uint64_t)y * create.pitch,
               source + (uint64_t)y * source_stride, row_bytes);
    upload = (struct drm_tarugx_upload_gem) {
        .source_handle = create.handle,
        .destination_handle = texture->handle,
        .width = texture->width,
        .height = texture->height,
    };
    result = drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_UPLOAD_GEM, &upload) == 0;

out:
    if (destination != MAP_FAILED)
        munmap(destination, create.size);
    destroy.handle = create.handle;
    drmIoctl(winsys->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    return result;
}

bool
tarugx_winsys_submit_triangle(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    const struct tarugx_winsys_triangle *triangle)
{
    return tarugx_winsys_submit_triangles(winsys, surface, triangle, 1);
}

bool
tarugx_winsys_submit_triangles(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    const struct tarugx_winsys_triangle *triangles,
    uint32_t count)
{
    struct drm_tarugx_submit_gem2 submit;
    struct drm_tarugx_submit_gem3 submit3;
    bool depth = false;
    uint32_t i;

    if (!winsys || winsys->fd < 0 || !surface || !surface->handle ||
        !triangles || !count || count > TARUGX_MAX_BATCH_TRIANGLES ||
        !winsys->submit_syncobj)
        return false;
    for (i = 0; i < count; i++)
        depth |= triangles[i].depth_flags != 0;
    if (depth) {
        if (!surface->depth ||
            !tarugx_winsys_supports_depth(winsys))
            return false;
        submit3 = (struct drm_tarugx_submit_gem3) {
            .handle = surface->handle,
            .count = count,
            .flags = DRM_TARUGX_SUBMIT_ASYNC,
            .out_syncobj = winsys->submit_syncobj,
        };
        for (i = 0; i < count; i++) {
            struct drm_tarugx_command3d *output = &submit3.commands[i];

            output->command = (struct drm_tarugx_command) {
                .opcode = DRM_TARUGX_OP_TRIANGLE,
                .x0 = triangles[i].x0,
                .y0 = triangles[i].y0,
                .x1 = triangles[i].x1,
                .y1 = triangles[i].y1,
                .x2 = triangles[i].x2,
                .y2 = triangles[i].y2,
                .color = triangles[i].color,
                .gradient_r = triangles[i].gradient_r,
                .gradient_g = triangles[i].gradient_g,
                .gradient_b = triangles[i].gradient_b,
            };
            output->depth_flags = triangles[i].depth_flags;
            output->depth = triangles[i].depth;
            output->gradient_z_x = triangles[i].gradient_z_x;
            output->gradient_z_y = triangles[i].gradient_z_y;
        }
        return drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_SUBMIT_GEM3,
                        &submit3) == 0;
    }
    submit = (struct drm_tarugx_submit_gem2) {
        .handle = surface->handle,
        .count = count,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .out_syncobj = winsys->submit_syncobj,
    };
    for (i = 0; i < count; i++) {
        submit.commands[i] = (struct drm_tarugx_command) {
            .opcode = DRM_TARUGX_OP_TRIANGLE,
            .x0 = triangles[i].x0,
            .y0 = triangles[i].y0,
            .x1 = triangles[i].x1,
            .y1 = triangles[i].y1,
            .x2 = triangles[i].x2,
            .y2 = triangles[i].y2,
            .color = triangles[i].color,
            .gradient_r = triangles[i].gradient_r,
            .gradient_g = triangles[i].gradient_g,
            .gradient_b = triangles[i].gradient_b,
        };
    }
    return drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_SUBMIT_GEM2,
                    &submit) == 0;
}

bool
tarugx_winsys_submit_textured_triangles(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    const struct tarugx_winsys_surface *texture,
    const struct tarugx_winsys_triangle *triangles,
    uint32_t count)
{
    struct drm_tarugx_submit_gem4 submit = {0};
    uint32_t i;

    if (!winsys || winsys->fd < 0 || !surface || !surface->handle ||
        !texture || !texture->handle || !texture->width || !texture->height ||
        !triangles || !count || count > TARUGX_MAX_BATCH_TRIANGLES ||
        !winsys->submit_syncobj || !tarugx_winsys_supports_texture(winsys))
        return false;
    submit.handle = surface->handle;
    submit.texture_handle = texture->handle;
    submit.count = count;
    submit.flags = DRM_TARUGX_SUBMIT_ASYNC;
    submit.out_syncobj = winsys->submit_syncobj;
    for (i = 0; i < count; i++) {
        struct drm_tarugx_command4 *output = &submit.commands[i];

        output->command.command = (struct drm_tarugx_command) {
            .opcode = DRM_TARUGX_OP_TRIANGLE,
            .x0 = triangles[i].x0,
            .y0 = triangles[i].y0,
            .x1 = triangles[i].x1,
            .y1 = triangles[i].y1,
            .x2 = triangles[i].x2,
            .y2 = triangles[i].y2,
            .color = triangles[i].color,
            .gradient_r = triangles[i].gradient_r,
            .gradient_g = triangles[i].gradient_g,
            .gradient_b = triangles[i].gradient_b,
        };
        output->command.depth_flags = triangles[i].depth_flags;
        output->command.depth = triangles[i].depth;
        output->command.gradient_z_x = triangles[i].gradient_z_x;
        output->command.gradient_z_y = triangles[i].gradient_z_y;
        output->texture_flags = DRM_TARUGX_TEXTURE_ENABLE;
        output->u = triangles[i].u;
        output->v = triangles[i].v;
        output->gradient_u_x = triangles[i].gradient_u_x;
        output->gradient_u_y = triangles[i].gradient_u_y;
        output->gradient_v_x = triangles[i].gradient_v_x;
        output->gradient_v_y = triangles[i].gradient_v_y;
    }
    return drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_SUBMIT_GEM4,
                    &submit) == 0;
}

bool
tarugx_winsys_submit_fill(struct tarugx_winsys *winsys,
                          const struct tarugx_winsys_surface *surface,
                          uint32_t color)
{
    struct drm_tarugx_submit_gem2 submit;

    if (!winsys || winsys->fd < 0 || !surface || !surface->handle ||
        !surface->width || !surface->height || !winsys->submit_syncobj)
        return false;
    submit = (struct drm_tarugx_submit_gem2) {
        .handle = surface->handle,
        .count = 1,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .out_syncobj = winsys->submit_syncobj,
        .commands = {{
            .opcode = DRM_TARUGX_OP_FILL,
            .width = surface->width,
            .height = surface->height,
            .color = color,
        }},
    };
    return drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_SUBMIT_GEM2,
                    &submit) == 0;
}

bool
tarugx_winsys_submit_color_depth_clear(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    uint32_t color, uint16_t depth)
{
    struct drm_tarugx_submit_gem3 submit;

    if (!winsys || winsys->fd < 0 || !surface || !surface->handle ||
        !surface->depth || !surface->width || !surface->height ||
        !winsys->submit_syncobj)
        return false;
    if (!tarugx_winsys_submit_fill(winsys, surface, color) ||
        !tarugx_winsys_wait(winsys, INT64_MAX))
        return false;
    submit = (struct drm_tarugx_submit_gem3) {
        .handle = surface->handle,
        .count = 1,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .out_syncobj = winsys->submit_syncobj,
        .commands = {{
            .command = {
                .opcode = DRM_TARUGX_OP_DEPTH_CLEAR,
                .width = surface->width,
                .height = surface->height,
            },
            .depth = depth,
        }},
    };
    return drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_SUBMIT_GEM3, &submit) == 0;
}

bool
tarugx_winsys_wait(struct tarugx_winsys *winsys, int64_t timeout_ns)
{
    if (!winsys || winsys->fd < 0 || !winsys->submit_syncobj)
        return false;
    return drmSyncobjWait(winsys->fd, &winsys->submit_syncobj, 1,
                          timeout_ns, 0, NULL) == 0;
}
