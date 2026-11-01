// SPDX-License-Identifier: GPL-2.0-only
/* CPU shadow framebuffer; no DMA, interrupts or hardware vblank. Linux 6.8. */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/iosys-map.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

struct tarugx {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
	void __iomem *regs, *vram;
};

static const struct drm_display_mode tgx_mode = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		 25188, 640, 656, 752, 800, 0, 480, 490, 492, 525, 0,
		 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)
};

static int tgx_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode = drm_mode_duplicate(connector->dev, &tgx_mode);
	if (!mode)
		return 0;
	drm_mode_probed_add(connector, mode);
	return 1;
}

static enum drm_connector_status tgx_detect(struct drm_connector *c, bool force)
{
	/* No HPD/EDID interface in this FPGA image. */
	return connector_status_connected;
}

static const struct drm_connector_helper_funcs tgx_connector_helper = {
	.get_modes = tgx_get_modes,
};
static const struct drm_connector_funcs tgx_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = tgx_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status tgx_mode_valid(struct drm_simple_display_pipe *pipe,
					 const struct drm_display_mode *mode)
{
	return drm_mode_equal(mode, &tgx_mode) ? MODE_OK : MODE_BAD;
}

static int tgx_check(struct drm_simple_display_pipe *pipe,
		    struct drm_plane_state *plane, struct drm_crtc_state *crtc)
{
	crtc->no_vblank = true;
	if (plane->fb && (plane->src_x || plane->src_y ||
	    plane->fb->width != 640 || plane->fb->height != 480))
		return -EINVAL;
	return 0;
}

static void tgx_update(struct drm_simple_display_pipe *pipe,
		       struct drm_plane_state *old)
{
	struct tarugx *tgx = container_of(pipe, struct tarugx, pipe);
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow = to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect damage;
	unsigned int x, y;
	int idx;
	u32 pixel;
	if (!fb || !pipe->crtc.state->active)
		return;
	/* A new FB must replace the single physical scanout buffer in full. */
	if (old->fb != fb || drm_atomic_crtc_needs_modeset(pipe->crtc.state))
		damage = DRM_RECT_INIT(0, 0, 640, 480);
	else if (!drm_atomic_helper_damage_merged(old, state, &damage))
		return;
	if (!drm_dev_enter(&tgx->drm, &idx))
		return;
	/* Keep scanout running during CPU updates. Blanking every dirty clip
	 * makes cursors and desktop animations visibly flash. This initial
	 * single-buffer path permits tearing; it does not promise atomic flips. */
	/* Normal uncached BAR mapping and explicit DWORD stores are required.
	 * memcpy_toio/WC can emit unsupported multi-DWORD PCIe requests. */
	for (y = damage.y1; y < damage.y2; y++) {
		for (x = damage.x1; x < damage.x2; x++) {
			iosys_map_memcpy_from(&pixel, &shadow->data[0],
					     y * fb->pitches[0] + x * 4, 4);
			writel(le32_to_cpu((__force __le32)pixel),
			       tgx->vram + (y * 640 + x) * 4);
		}
	}
	/* BAR2 read orders the preceding posted writes through the DDR path. */
	readl(tgx->vram + (640 * 480 - 1) * 4);
	writel(0, tgx->regs + 0x1c);
	writel(2560, tgx->regs + 0x20);
	writel(1, tgx->regs + 0x24);
	readl(tgx->regs);
	writel(1, tgx->regs + 0x10);
	readl(tgx->regs);
	drm_dev_exit(idx);
}

static void tgx_disable(struct drm_simple_display_pipe *pipe)
{
	struct tarugx *tgx = container_of(pipe, struct tarugx, pipe);
	int idx;
	if (!drm_dev_enter(&tgx->drm, &idx))
		return;
	writel(0, tgx->regs + 0x10);
	readl(tgx->regs);
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs tgx_pipe_funcs = {
	.mode_valid = tgx_mode_valid,
	.check = tgx_check,
	.update = tgx_update,
	.disable = tgx_disable,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};
static const struct drm_mode_config_funcs tgx_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};
DEFINE_DRM_GEM_FOPS(tgx_fops);
static struct drm_gem_object *tgx_prime_import(struct drm_device *drm,
					     struct dma_buf *dma_buf)
{
	/* Initial driver accepts its own CPU buffers only; no imported DMA BOs. */
	return ERR_PTR(-EOPNOTSUPP);
}
static const struct drm_driver tgx_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &tgx_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	.gem_prime_import = tgx_prime_import,
	.name = "tarugx",
	.desc = "TaruGX/100 CPU framebuffer",
	.date = "20261031",
	.major = 0, .minor = 1,
};

static int tgx_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const u32 formats[] = { DRM_FORMAT_XRGB8888 };
	struct tarugx *tgx;
	struct drm_device *drm;
	int ret;
	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;
	if (pci_resource_len(pdev, 0) != 65536 ||
	    pci_resource_len(pdev, 2) != 4194304)
		return -ENODEV;
	ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "tarugx");
	if (ret)
		return ret;
	tgx = devm_drm_dev_alloc(&pdev->dev, &tgx_driver, struct tarugx, drm);
	if (IS_ERR(tgx))
		return PTR_ERR(tgx);
	drm = &tgx->drm;
	tgx->regs = pcim_iomap_table(pdev)[0];
	tgx->vram = pcim_iomap_table(pdev)[2];
	if (readl(tgx->regs) != 0x54475055)
		return -ENODEV;
	pci_set_drvdata(pdev, tgx);
	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;
	drm->mode_config.funcs = &tgx_mode_config_funcs;
	drm->mode_config.min_width = drm->mode_config.max_width = 640;
	drm->mode_config.min_height = drm->mode_config.max_height = 480;
	ret = drm_connector_init(drm, &tgx->connector, &tgx_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;
	drm_connector_helper_add(&tgx->connector, &tgx_connector_helper);
	ret = drm_simple_display_pipe_init(drm, &tgx->pipe, &tgx_pipe_funcs,
			formats, ARRAY_SIZE(formats), NULL, &tgx->connector);
	if (ret)
		return ret;
	drm_plane_enable_fb_damage_clips(&tgx->pipe.plane);
	drm_mode_config_reset(drm);
	return drm_dev_register(drm, 0);
}

static void tgx_remove(struct pci_dev *pdev)
{
	struct tarugx *tgx = pci_get_drvdata(pdev);
	drm_atomic_helper_shutdown(&tgx->drm);
	drm_dev_unplug(&tgx->drm);
}
static void tgx_shutdown(struct pci_dev *pdev)
{
	struct tarugx *tgx = pci_get_drvdata(pdev);
	drm_atomic_helper_shutdown(&tgx->drm);
}
static const struct pci_device_id tgx_ids[] = {
	{ PCI_DEVICE(0x22c2, 0x1100) }, { }
};
MODULE_DEVICE_TABLE(pci, tgx_ids);
static struct pci_driver tgx_pci_driver = {
	.name = "tarugx", .id_table = tgx_ids,
	.probe = tgx_probe, .remove = tgx_remove, .shutdown = tgx_shutdown,
};
module_pci_driver(tgx_pci_driver);
MODULE_DESCRIPTION("TaruGX/100 DRM/KMS framebuffer driver");
MODULE_LICENSE("GPL");
