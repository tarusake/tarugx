// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <xf86drm.h>

#include "tarugx_drm.h"

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
    if (expect_errno(fd, DRM_IOCTL_TARUGX_FILL, &invalid, EINVAL))
        goto out_other;
    if (ioctl(fd, DRM_IOCTL_TARUGX_FILL, &fill)) {
        perror("DRM_IOCTL_TARUGX_FILL");
        goto out_other;
    }
    if (ioctl(fd, DRM_IOCTL_TARUGX_BLIT, &blit)) {
        perror("DRM_IOCTL_TARUGX_BLIT");
        goto out_other;
    }
    if (ioctl(fd, DRM_IOCTL_TARUGX_SUBMIT, &batch)) {
        perror("DRM_IOCTL_TARUGX_SUBMIT");
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
           "blit fence %u, four-command batch fence %u\n",
           fill.fence, blit.fence, batch.fence);
    result = 0;
out_other:
    close(other);
out_drop:
    drmDropMaster(fd);
out_close:
    close(fd);
    return result;
}
