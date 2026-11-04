// SPDX-License-Identifier: MIT
/* Minimal fixed-mode Xorg EXA driver for the TaruGX RGB565 front buffer. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xorg-server.h>
#include <xorgVersion.h>
#include <xf86.h>
#include <xf86Module.h>
#include <xf86_OSproc.h>
#include <fb.h>
#include <micmap.h>
#include <mipointer.h>
#include <exa.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "tarugx_drm.h"

#define TARUGX_VERSION 1000
#define TARUGX_NAME "TaruGX"
#define TARUGX_DRIVER_NAME "tarugx"
#define TARUGX_WIDTH 1920
#define TARUGX_HEIGHT 1080
#define TARUGX_PITCH (TARUGX_WIDTH * 2)
#define TARUGX_FRAME_BYTES ((size_t)TARUGX_PITCH * TARUGX_HEIGHT)

enum { OPTION_DEVICE };
static OptionInfoRec tarugx_options[] = {
    { OPTION_DEVICE, "Device", OPTV_STRING, {0}, FALSE },
    { -1, NULL, OPTV_NONE, {0}, FALSE },
};

typedef struct {
    int fd;
    char *device;
    void *map;
    size_t map_size;
    uint32_t handle;
    uint32_t fb_id;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    drmModeCrtcPtr saved_crtc;
    ExaDriverPtr exa;
    CloseScreenProcPtr close_screen;
    Pixel solid_color;
    uint64_t solid_count;
    uint64_t copy_count;
    uint64_t fallback_count;
    uint64_t submit_failures;
    int last_submit_errno;
} TaruGXRec, *TaruGXPtr;

#define TARUGXPTR(p) ((TaruGXPtr)((p)->driverPrivate))

static Bool tarugx_close_screen(ScreenPtr screen);
static void tarugx_destroy_front(TaruGXPtr tgx);

static Bool tarugx_pixmap_is_front(PixmapPtr pixmap)
{
    if (!pixmap)
        return FALSE;
    return pixmap->drawable.width == TARUGX_WIDTH &&
           pixmap->drawable.height == TARUGX_HEIGHT &&
           pixmap->drawable.bitsPerPixel == 16 &&
           exaGetPixmapOffset(pixmap) == 0 &&
           exaGetPixmapPitch(pixmap) == TARUGX_PITCH;
}

static int tarugx_submit(TaruGXPtr tgx, struct drm_tarugx_command *command)
{
    struct drm_tarugx_submit_gem submit = {
        .handle = tgx->handle,
        .count = 1,
    };

    submit.commands[0] = *command;
    if (ioctl(tgx->fd, DRM_IOCTL_TARUGX_SUBMIT_GEM, &submit)) {
        tgx->submit_failures++;
        tgx->last_submit_errno = errno;
        return -1;
    }
    return 0;
}

static Bool tarugx_prepare_solid(PixmapPtr pixmap, int alu,
                                 Pixel planemask, Pixel color)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    TaruGXPtr tgx = TARUGXPTR(scrn);

    if (!tarugx_pixmap_is_front(pixmap) || alu != GXcopy ||
        (planemask & 0xffff) != 0xffff) {
        tgx->fallback_count++;
        return FALSE;
    }
    tgx->solid_color = color;
    return TRUE;
}

static void tarugx_solid(PixmapPtr pixmap, int x1, int y1, int x2, int y2)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pixmap->drawable.pScreen);
    TaruGXPtr tgx = TARUGXPTR(scrn);
    struct drm_tarugx_command command = {
        .opcode = DRM_TARUGX_OP_FILL,
        .dst_x = x1,
        .dst_y = y1,
        .width = x2 - x1,
        .height = y2 - y1,
        .color = tgx->solid_color,
    };

    if (x2 > x1 && y2 > y1 && !tarugx_submit(tgx, &command))
        tgx->solid_count++;
}

static void tarugx_done_solid(PixmapPtr pixmap)
{
    (void)pixmap;
}

static Bool tarugx_prepare_copy(PixmapPtr source, PixmapPtr destination,
                                int dx, int dy, int alu, Pixel planemask)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(destination->drawable.pScreen);
    TaruGXPtr tgx = TARUGXPTR(scrn);

    (void)dx;
    (void)dy;
    if (!tarugx_pixmap_is_front(source) ||
        !tarugx_pixmap_is_front(destination) || alu != GXcopy ||
        (planemask & 0xffff) != 0xffff) {
        tgx->fallback_count++;
        return FALSE;
    }
    return TRUE;
}

static void tarugx_copy(PixmapPtr destination, int src_x, int src_y,
                        int dst_x, int dst_y, int width, int height)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(destination->drawable.pScreen);
    TaruGXPtr tgx = TARUGXPTR(scrn);
    struct drm_tarugx_command command = {
        .opcode = DRM_TARUGX_OP_BLIT,
        .src_x = src_x,
        .src_y = src_y,
        .dst_x = dst_x,
        .dst_y = dst_y,
        .width = width,
        .height = height,
    };

    if (width > 0 && height > 0 && !tarugx_submit(tgx, &command))
        tgx->copy_count++;
}

static void tarugx_done_copy(PixmapPtr pixmap)
{
    (void)pixmap;
}

static void tarugx_wait_marker(ScreenPtr screen, int marker)
{
    (void)screen;
    (void)marker;
    __sync_synchronize();
}

static Bool tarugx_find_output(TaruGXPtr tgx)
{
    drmModeResPtr resources = drmModeGetResources(tgx->fd);
    int i, j;

    if (!resources)
        return FALSE;
    for (i = 0; i < resources->count_connectors; i++) {
        drmModeConnectorPtr connector =
            drmModeGetConnector(tgx->fd, resources->connectors[i]);
        if (!connector)
            continue;
        if (connector->connection == DRM_MODE_CONNECTED) {
            for (j = 0; j < connector->count_modes; j++) {
                if (connector->modes[j].hdisplay == TARUGX_WIDTH &&
                    connector->modes[j].vdisplay == TARUGX_HEIGHT) {
                    tgx->connector_id = connector->connector_id;
                    tgx->mode = connector->modes[j];
                    if (connector->encoder_id) {
                        drmModeEncoderPtr encoder = drmModeGetEncoder(
                            tgx->fd, connector->encoder_id);
                        if (encoder) {
                            tgx->crtc_id = encoder->crtc_id;
                            drmModeFreeEncoder(encoder);
                        }
                    }
                    drmModeFreeConnector(connector);
                    if (!tgx->crtc_id && resources->count_crtcs)
                        tgx->crtc_id = resources->crtcs[0];
                    drmModeFreeResources(resources);
                    return tgx->crtc_id != 0;
                }
            }
        }
        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(resources);
    return FALSE;
}

static Bool tarugx_create_front(ScrnInfoPtr scrn)
{
    TaruGXPtr tgx = TARUGXPTR(scrn);
    struct drm_tarugx_vram_create create = {
        .width = TARUGX_WIDTH,
        .height = TARUGX_HEIGHT,
        .flags = DRM_TARUGX_VRAM_FRONT,
    };
    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};

    if (drmSetMaster(tgx->fd)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "drmSetMaster failed: %s\n",
                   strerror(errno));
        return FALSE;
    }
    if (!tarugx_find_output(tgx)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "1080p DRM output not found\n");
        return FALSE;
    }
    if (ioctl(tgx->fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &create)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "VRAM_FRONT ioctl failed: %s\n", strerror(errno));
        return FALSE;
    }
    tgx->handle = create.handle;
    tgx->map_size = create.size;
    tgx->map = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    tgx->fd, (off_t)create.mmap_offset);
    if (tgx->map == MAP_FAILED) {
        int error = errno;
        tgx->map = NULL;
        tarugx_destroy_front(tgx);
        errno = error;
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "front mmap failed: %s\n",
                   strerror(errno));
        return FALSE;
    }
    handles[0] = create.handle;
    pitches[0] = create.pitch;
    if (drmModeAddFB2(tgx->fd, TARUGX_WIDTH, TARUGX_HEIGHT,
                      DRM_FORMAT_RGB565, handles, pitches, offsets,
                      &tgx->fb_id, 0)) {
        int error = errno;
        tarugx_destroy_front(tgx);
        errno = error;
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "drmModeAddFB2 failed: %s\n",
                   strerror(errno));
        return FALSE;
    }
    tgx->saved_crtc = drmModeGetCrtc(tgx->fd, tgx->crtc_id);
    memset(tgx->map, 0, create.size);
    if (drmModeSetCrtc(tgx->fd, tgx->crtc_id, tgx->fb_id, 0, 0,
                       &tgx->connector_id, 1, &tgx->mode)) {
        int error = errno;
        tarugx_destroy_front(tgx);
        errno = error;
        xf86DrvMsg(scrn->scrnIndex, X_ERROR, "drmModeSetCrtc failed: %s\n",
                   strerror(errno));
        return FALSE;
    }
    return TRUE;
}

static void tarugx_destroy_front(TaruGXPtr tgx)
{
    struct drm_gem_close close = { .handle = tgx->handle };

    if (tgx->saved_crtc) {
        drmModeSetCrtc(tgx->fd, tgx->saved_crtc->crtc_id,
                       tgx->saved_crtc->buffer_id,
                       tgx->saved_crtc->x, tgx->saved_crtc->y,
                       &tgx->connector_id, 1, &tgx->saved_crtc->mode);
        drmModeFreeCrtc(tgx->saved_crtc);
        tgx->saved_crtc = NULL;
    }
    if (tgx->fb_id)
        drmModeRmFB(tgx->fd, tgx->fb_id);
    if (tgx->map)
        munmap(tgx->map, tgx->map_size);
    if (tgx->handle)
        ioctl(tgx->fd, DRM_IOCTL_GEM_CLOSE, &close);
    tgx->fb_id = tgx->handle = 0;
    tgx->map = NULL;
}

static Bool tarugx_screen_init(ScreenPtr screen, int argc, char **argv)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TaruGXPtr tgx = TARUGXPTR(scrn);
    VisualPtr visual;

    (void)argc;
    (void)argv;
    if (!tarugx_create_front(scrn)) {
        xf86DrvMsg(scrn->scrnIndex, X_ERROR,
                   "could not create accelerated front buffer: %s\n",
                   strerror(errno));
        return FALSE;
    }
    miClearVisualTypes();
    if (!miSetVisualTypes(scrn->depth, miGetDefaultVisualMask(scrn->depth),
                          scrn->rgbBits, scrn->defaultVisual) ||
        !miSetPixmapDepths() ||
        !fbScreenInit(screen, tgx->map, TARUGX_WIDTH, TARUGX_HEIGHT,
                      scrn->xDpi, scrn->yDpi, TARUGX_WIDTH, 16))
        return FALSE;
    for (visual = screen->visuals; visual < screen->visuals + screen->numVisuals;
         visual++) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed = scrn->offset.red;
            visual->offsetGreen = scrn->offset.green;
            visual->offsetBlue = scrn->offset.blue;
            visual->redMask = scrn->mask.red;
            visual->greenMask = scrn->mask.green;
            visual->blueMask = scrn->mask.blue;
        }
    }
    fbPictureInit(screen, NULL, 0);
    xf86SetBlackWhitePixels(screen);
    xf86SetBackingStore(screen);
    miDCInitialize(screen, xf86GetPointerScreenFuncs());
    if (!miCreateDefColormap(screen))
        return FALSE;

    tgx->exa = exaDriverAlloc();
    if (!tgx->exa)
        return FALSE;
    tgx->exa->exa_major = EXA_VERSION_MAJOR;
    tgx->exa->exa_minor = EXA_VERSION_MINOR;
    tgx->exa->flags = EXA_OFFSCREEN_PIXMAPS;
    tgx->exa->memoryBase = tgx->map;
    tgx->exa->memorySize = tgx->map_size;
    /* Keep EXA's allocator non-empty. The GEM is page aligned, leaving a
     * small tail after the visible frame; a zero-sized offscreen arena trips
     * an EXA 2.6 allocator assertion during server startup. */
    tgx->exa->offScreenBase = TARUGX_FRAME_BYTES;
    tgx->exa->pixmapOffsetAlign = 4;
    tgx->exa->pixmapPitchAlign = 4;
    tgx->exa->maxX = TARUGX_WIDTH;
    tgx->exa->maxY = TARUGX_HEIGHT;
    tgx->exa->PrepareSolid = tarugx_prepare_solid;
    tgx->exa->Solid = tarugx_solid;
    tgx->exa->DoneSolid = tarugx_done_solid;
    tgx->exa->PrepareCopy = tarugx_prepare_copy;
    tgx->exa->Copy = tarugx_copy;
    tgx->exa->DoneCopy = tarugx_done_copy;
    tgx->exa->WaitMarker = tarugx_wait_marker;
    if (!exaDriverInit(screen, tgx->exa))
        return FALSE;
    tgx->close_screen = screen->CloseScreen;
    screen->CloseScreen = tarugx_close_screen;
    scrn->vtSema = TRUE;
    xf86DrvMsg(scrn->scrnIndex, X_INFO,
               "TaruGX EXA front buffer active at 1920x1080 RGB565\n");
    return TRUE;
}

static Bool tarugx_close_screen(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    TaruGXPtr tgx = TARUGXPTR(scrn);
    CloseScreenProcPtr close_screen = tgx->close_screen;

    xf86DrvMsg(scrn->scrnIndex, X_INFO,
               "EXA totals: solid=%llu copy=%llu fallback=%llu "
               "submit_failures=%llu last_errno=%d\n",
               (unsigned long long)tgx->solid_count,
               (unsigned long long)tgx->copy_count,
               (unsigned long long)tgx->fallback_count,
               (unsigned long long)tgx->submit_failures,
               tgx->last_submit_errno);
    if (tgx->exa)
        exaDriverFini(screen);
    tgx->exa = NULL;
    tarugx_destroy_front(tgx);
    screen->CloseScreen = close_screen;
    scrn->vtSema = FALSE;
    return close_screen(screen);
}

static Bool tarugx_enter_vt(ScrnInfoPtr scrn)
{
    TaruGXPtr tgx = TARUGXPTR(scrn);

    return drmModeSetCrtc(tgx->fd, tgx->crtc_id, tgx->fb_id, 0, 0,
                          &tgx->connector_id, 1, &tgx->mode) == 0;
}

static void tarugx_leave_vt(ScrnInfoPtr scrn)
{
    (void)scrn;
}

static Bool tarugx_switch_mode(ScrnInfoPtr scrn, DisplayModePtr mode)
{
    (void)scrn;
    return mode->HDisplay == TARUGX_WIDTH && mode->VDisplay == TARUGX_HEIGHT;
}

static void tarugx_adjust_frame(ScrnInfoPtr scrn, int x, int y)
{
    (void)scrn;
    (void)x;
    (void)y;
}

static ModeStatus tarugx_valid_mode(ScrnInfoPtr scrn, DisplayModePtr mode,
                                    Bool verbose, int flags)
{
    (void)scrn;
    (void)verbose;
    (void)flags;
    return mode->HDisplay == TARUGX_WIDTH && mode->VDisplay == TARUGX_HEIGHT ?
        MODE_OK : MODE_BAD;
}

static void tarugx_free_screen(ScrnInfoPtr scrn)
{
    TaruGXPtr tgx = TARUGXPTR(scrn);

    if (!tgx)
        return;
    if (tgx->fd >= 0)
        close(tgx->fd);
    free(tgx->device);
    free(tgx);
    scrn->driverPrivate = NULL;
}

static Bool tarugx_pre_init(ScrnInfoPtr scrn, int flags)
{
    TaruGXPtr tgx;
    EntityInfoPtr entity;
    DisplayModePtr mode;
    rgb weight = {5, 6, 5};
    rgb mask = {0, 0, 0};
    Gamma gamma = {0.0, 0.0, 0.0};
    const char *device;

    if (flags & PROBE_DETECT)
        return TRUE;
    tgx = xnfcalloc(1, sizeof(*tgx));
    tgx->fd = -1;
    scrn->driverPrivate = tgx;
    entity = xf86GetEntityInfo(scrn->entityList[0]);
    scrn->monitor = scrn->confScreen->monitor;
    if (!xf86SetDepthBpp(scrn, 16, 16, 16, 0) ||
        !xf86SetWeight(scrn, weight, mask) ||
        !xf86SetDefaultVisual(scrn, -1) || !xf86SetGamma(scrn, gamma))
        return FALSE;
    xf86CollectOptions(scrn, entity->device->options);
    xf86ProcessOptions(scrn->scrnIndex, scrn->options,
                       (OptionInfoPtr)tarugx_options);
    device = xf86GetOptValString(tarugx_options, OPTION_DEVICE);
    tgx->device = strdup(device ? device : "/dev/dri/card0");
    tgx->fd = open(tgx->device, O_RDWR | O_CLOEXEC);
    if (tgx->fd < 0)
        return FALSE;
    mode = xnfcalloc(1, sizeof(*mode));
    mode->name = strdup("1920x1080");
    mode->status = MODE_OK;
    mode->type = M_T_DRIVER | M_T_PREFERRED;
    mode->Clock = 148500;
    mode->HDisplay = 1920; mode->HSyncStart = 2008;
    mode->HSyncEnd = 2052; mode->HTotal = 2200;
    mode->VDisplay = 1080; mode->VSyncStart = 1084;
    mode->VSyncEnd = 1089; mode->VTotal = 1125;
    mode->Flags = V_PHSYNC | V_PVSYNC;
    mode->next = mode->prev = mode;
    scrn->modes = scrn->currentMode = mode;
    scrn->virtualX = scrn->displayWidth = TARUGX_WIDTH;
    scrn->virtualY = TARUGX_HEIGHT;
    scrn->videoRam = (TARUGX_PITCH * TARUGX_HEIGHT + 1023) / 1024;
    scrn->progClock = TRUE;
    xf86SetDpi(scrn, 0, 0);
    if (!xf86LoadSubModule(scrn, "fb") || !xf86LoadSubModule(scrn, "exa"))
        return FALSE;
    scrn->memPhysBase = 0;
    scrn->fbOffset = 0;
    return TRUE;
}

static const OptionInfoRec *tarugx_available_options(int chipid, int busid)
{
    (void)chipid;
    (void)busid;
    return tarugx_options;
}

static void tarugx_identify(int flags)
{
    (void)flags;
    xf86Msg(X_INFO, "TaruGX fixed-mode EXA driver\n");
}

static Bool tarugx_probe(DriverPtr driver, int flags)
{
    GDevPtr *devices = NULL;
    int count, i;
    Bool found = FALSE;

    if (flags & PROBE_DETECT)
        return FALSE;
    count = xf86MatchDevice(TARUGX_DRIVER_NAME, &devices);
    for (i = 0; i < count; i++) {
        int entity = xf86ClaimNoSlot(driver, 0, devices[i], TRUE);
        ScrnInfoPtr scrn = xf86AllocateScreen(driver, 0);
        if (!scrn)
            continue;
        xf86AddEntityToScreen(scrn, entity);
        scrn->driverVersion = TARUGX_VERSION;
        scrn->driverName = TARUGX_DRIVER_NAME;
        scrn->name = TARUGX_NAME;
        scrn->Probe = tarugx_probe;
        scrn->PreInit = tarugx_pre_init;
        scrn->ScreenInit = tarugx_screen_init;
        scrn->SwitchMode = tarugx_switch_mode;
        scrn->AdjustFrame = tarugx_adjust_frame;
        scrn->EnterVT = tarugx_enter_vt;
        scrn->LeaveVT = tarugx_leave_vt;
        scrn->FreeScreen = tarugx_free_screen;
        scrn->ValidMode = tarugx_valid_mode;
        found = TRUE;
    }
    free(devices);
    return found;
}

_X_EXPORT DriverRec TARUGX = {
    .driverVersion = TARUGX_VERSION,
    .driverName = TARUGX_DRIVER_NAME,
    .Identify = tarugx_identify,
    .Probe = tarugx_probe,
    .AvailableOptions = tarugx_available_options,
};

static XF86ModuleVersionInfo tarugx_version = {
    .modname = TARUGX_DRIVER_NAME,
    .vendor = MODULEVENDORSTRING,
    ._modinfo1_ = MODINFOSTRING1,
    ._modinfo2_ = MODINFOSTRING2,
    .xf86version = XORG_VERSION_CURRENT,
    .majorversion = 0,
    .minorversion = 1,
    .patchlevel = 0,
    .abiclass = ABI_CLASS_VIDEODRV,
    .abiversion = ABI_VIDEODRV_VERSION,
    .moduleclass = MOD_CLASS_VIDEODRV,
};

static pointer tarugx_setup(pointer module, pointer options,
                            int *error_major, int *error_minor)
{
    static Bool done;
    (void)options;
    (void)error_minor;
    if (done) {
        if (error_major)
            *error_major = LDR_ONCEONLY;
        return NULL;
    }
    done = TRUE;
    xf86AddDriver(&TARUGX, module, HaveDriverFuncs);
    return (pointer)1;
}

_X_EXPORT XF86ModuleData tarugxModuleData = {
    &tarugx_version,
    tarugx_setup,
    NULL,
};
