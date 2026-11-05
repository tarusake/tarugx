// SPDX-License-Identifier: MIT
#include "tarugx_winsys.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <xf86drm.h>

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
    int duplicate;

    if (!winsys || fd < 0 || !tarugx_winsys_is_device(fd))
        return false;
    duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0)
        return false;
    memset(winsys, 0, sizeof(*winsys));
    winsys->fd = duplicate;
    if (drmSyncobjCreate(duplicate, 0, &winsys->submit_syncobj)) {
        close(duplicate);
        winsys->fd = -1;
        return false;
    }
    return true;
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

bool
tarugx_winsys_surface_create(struct tarugx_winsys *winsys,
                             uint32_t width, uint32_t height,
                             struct tarugx_winsys_surface *surface)
{
    struct drm_tarugx_vram_create create = {
        .width = width,
        .height = height,
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
tarugx_winsys_submit_triangle(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    const struct tarugx_winsys_triangle *triangle)
{
    struct drm_tarugx_submit_gem2 submit;

    if (!winsys || winsys->fd < 0 || !surface || !surface->handle ||
        !triangle || !winsys->submit_syncobj)
        return false;
    submit = (struct drm_tarugx_submit_gem2) {
        .handle = surface->handle,
        .count = 1,
        .flags = DRM_TARUGX_SUBMIT_ASYNC,
        .out_syncobj = winsys->submit_syncobj,
        .commands = {{
            .opcode = DRM_TARUGX_OP_TRIANGLE,
            .x0 = triangle->x0,
            .y0 = triangle->y0,
            .x1 = triangle->x1,
            .y1 = triangle->y1,
            .x2 = triangle->x2,
            .y2 = triangle->y2,
            .color = triangle->color,
        }},
    };
    return drmIoctl(winsys->fd, DRM_IOCTL_TARUGX_SUBMIT_GEM2,
                    &submit) == 0;
}

bool
tarugx_winsys_wait(struct tarugx_winsys *winsys, int64_t timeout_ns)
{
    if (!winsys || winsys->fd < 0 || !winsys->submit_syncobj)
        return false;
    return drmSyncobjWait(winsys->fd, &winsys->submit_syncobj, 1,
                          timeout_ns, 0, NULL) == 0;
}
