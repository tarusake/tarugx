// SPDX-License-Identifier: MIT
#include "tarugx_screen.h"

#include "pipe/p_screen.h"
#include "frontend/sw_winsys.h"
#include "softpipe/sp_public.h"
#include "sw/kms-dri/kms_dri_sw_winsys.h"

#include "tarugx_winsys.h"
#include "tarugx_context.h"

static const char *
tarugx_screen_name(struct pipe_screen *screen)
{
    (void)screen;
    return "TaruGX (FPGA 3D + softpipe fallback)";
}

static const char *
tarugx_screen_vendor(struct pipe_screen *screen)
{
    (void)screen;
    return "TaruGX";
}

struct pipe_screen *
tarugx_screen_create(int fd, const struct pipe_screen_config *config)
{
    struct tarugx_winsys hardware = { .fd = -1 };
    struct sw_winsys *software;
    struct pipe_screen *screen;

    (void)config;
    if (!tarugx_winsys_open(&hardware, fd))
        return NULL;
    tarugx_winsys_close(&hardware);

    software = kms_dri_create_winsys(fd);
    if (!software)
        return NULL;
    screen = softpipe_create_screen(software);
    if (!screen) {
        software->destroy(software);
        return NULL;
    }
    if (!tarugx_context_enable(screen, fd)) {
        screen->destroy(screen);
        return NULL;
    }
    screen->get_name = tarugx_screen_name;
    screen->get_vendor = tarugx_screen_vendor;
    screen->get_device_vendor = tarugx_screen_vendor;
    return screen;
}
