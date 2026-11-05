// SPDX-License-Identifier: GPL-2.0-only
/* CPU shadow framebuffer with MSI VBlank and host-to-VRAM DMA. Linux 6.8. */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-chain.h>
#include <linux/dma-resv.h>
#include <linux/dma-buf.h>
#include <linux/iosys-map.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_auth.h>
#include <drm/drm_drv.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_mm.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_prime.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_vma_manager.h>

#include "tarugx_drm.h"

static bool dma_upload = true;
module_param(dma_upload, bool, 0644);
MODULE_PARM_DESC(dma_upload,
		 "use full-frame DMA uploads when supported by the FPGA (default: true)");

#define TGX_RENDER_ENTITIES 4
#define TGX_RENDER_JOB_LIMIT 64

struct tarugx {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
	struct pci_dev *pdev;
	void __iomem *regs, *vram;
	struct mutex dma_lock;
	struct mutex render_lock;
	struct mutex render_submit_lock;
	struct mutex vram_lock;
	struct drm_mm vram_mm;
	void *dma_staging;
	dma_addr_t dma_staging_addr;
	atomic64_t page_flips;
	atomic64_t flip_timeouts;
	atomic64_t flip_fallbacks;
	atomic64_t vblank_timeouts;
	atomic64_t bytes_uploaded;
	atomic64_t msi_interrupts;
	atomic64_t blit_syncs;
	atomic64_t blit_sync_failures;
	atomic64_t dma_uploads;
	atomic64_t dma_upload_failures;
	atomic64_t partial_dma_uploads;
	atomic64_t partial_dma_bytes;
	atomic64_t render_commands;
	atomic64_t render_failures;
	atomic64_t render_queue_peak;
	atomic64_t render_sched_peak;
	atomic64_t direct_scanouts;
	atomic64_t last_update_ns;
	atomic64_t last_conversion_ns;
	atomic64_t last_dma_ns;
	atomic64_t last_vblank_wait_ns;
	atomic64_t host_conversion_ns;
	atomic64_t host_conversion_count;
	atomic64_t prime_conversion_ns;
	atomic64_t prime_conversion_count;
	atomic64_t prime_import_count;
	atomic64_t prime_sg_segments;
	atomic64_t prime_sg_segments_peak;
	bool dma_available;
	bool render_available;
	bool triangle_available;
	bool triangle_interpolation_available;
	bool depth_available;
	u32 gpu_version;
	bool active_fb_base_available;
	bool legacy_single_buffer;
	bool physical_pages_synced;
	bool removing;
	wait_queue_head_t vblank_wait;
	wait_queue_head_t render_wait;
	spinlock_t render_fence_lock;
	u64 render_fence_context;
	atomic64_t render_fence_seqno;
	struct dma_fence *render_sync_fence;
	u32 render_sync_hw_fence;
	u32 render_sync_commands;
	u32 render_fence;
	bool render_async_valid;
	bool render_async_completed;
	struct drm_file *render_async_owner;
	u32 render_async_fence;
	u32 render_async_commands;
	int render_async_result;
	struct drm_gem_object *render_async_gem;
	struct drm_gpu_scheduler render_sched;
	struct drm_sched_entity render_entities[TGX_RENDER_ENTITIES];
	struct drm_gpu_scheduler *render_sched_list[1];
	atomic_t render_sched_jobs;
	atomic_t render_entity_next;
	u32 render_entity_count;
	bool render_sched_initialized;
	const struct drm_display_mode *mode;
	u32 width;
	u32 height;
	u32 cpp;
	u32 format;
	u32 pitch;
	u32 frame_bytes;
	u32 vram_bytes;
	u32 vram_gem_base;
	u32 vram_gem_bytes;
	bool dma_only_scanout;
	bool front_allocated;
};

struct tarugx_vram_object {
	struct drm_gem_object base;
	struct drm_mm_node node;
	u32 width;
	u32 height;
	u32 pitch;
	bool front;
	bool depth;
	u32 depth_offset;
};

struct tarugx_depth_state {
	u32 flags;
	u16 depth;
	s32 gradient_x;
	s32 gradient_y;
};

struct tarugx_sched_job {
	struct drm_sched_job base;
	struct tarugx *tgx;
	struct drm_gem_object *gem;
	struct dma_fence *input_fence;
	struct drm_tarugx_command commands[DRM_TARUGX_MAX_COMMANDS];
	u32 count;
	u32 vram_base;
	u32 pitch;
	u32 width;
	u32 height;
	u32 depth_base;
	struct tarugx_depth_state depth[DRM_TARUGX_MAX_COMMANDS];
};

struct tarugx_dma_fence {
	struct dma_fence base;
};

struct tarugx_host_object {
	struct drm_gem_object base;
	void *vaddr;
	dma_addr_t dma_addr;
	u32 width;
	u32 height;
	u32 pitch;
	u32 cpp;
};

struct tarugx_prime_object {
	struct drm_gem_object base;
	struct sg_table *sgt;
	struct iosys_map map;
};

static const struct drm_gem_object_funcs tgx_host_object_funcs;
static const struct drm_gem_object_funcs tgx_prime_object_funcs;

#define TGX_BAR2_BYTES SZ_4M
#define TGX_LEGACY_WIDTH 640
#define TGX_LEGACY_HEIGHT 480
#define TGX_LEGACY_PITCH (TGX_LEGACY_WIDTH * 4)
#define TGX_LEGACY_FRAME_BYTES (TGX_LEGACY_PITCH * TGX_LEGACY_HEIGHT)
#define TGX_1080P_WIDTH 1920
#define TGX_1080P_HEIGHT 1080
#define TGX_1080P_PITCH (TGX_1080P_WIDTH * 4)
#define TGX_1080P_FRAME_BYTES (TGX_1080P_PITCH * TGX_1080P_HEIGHT)
#define TGX_1080P_RGB565_PITCH (TGX_1080P_WIDTH * 2)
#define TGX_1080P_RGB565_FRAME_BYTES \
	(TGX_1080P_RGB565_PITCH * TGX_1080P_HEIGHT)
#define TGX_DDR_BYTES SZ_16M
#define TGX_REG_GPU_VERSION 0x04
#define TGX_REG_DISPLAY_CTRL 0x10
#define TGX_REG_FB_BASE 0x1c
#define TGX_REG_FB_STRIDE 0x20
#define TGX_REG_FB_COMMIT 0x24
#define TGX_REG_ACTIVE_BUFFER 0x28
#define TGX_REG_VBLANK_TOGGLE 0x2c
#define TGX_REG_IRQ_ENABLE 0x30
#define TGX_REG_ACTIVE_FB_BASE 0x34
#define TGX_GPU_VERSION_ACTIVE_FB_BASE 0x00010001
#define TGX_GPU_VERSION_1080P60 0x00010002
#define TGX_GPU_VERSION_RGB565 0x00010003
#define TGX_GPU_VERSION_TRIANGLE 0x00010004
#define TGX_GPU_VERSION_TRIANGLE_INTERPOLATION 0x00010005
#define TGX_GPU_VERSION_Z16 0x00010006
#define TGX_IRQ_VBLANK BIT(0)
#define TGX_IRQ_RENDER BIT(1)
#define TGX_REG_FILL_ADDR 0x60
#define TGX_REG_FILL_STRIDE 0x64
#define TGX_REG_FILL_SIZE 0x68
#define TGX_REG_FILL_COLOR 0x6c
#define TGX_REG_BLIT_SRC 0x80
#define TGX_REG_BLIT_DST 0x84
#define TGX_REG_BLIT_SRC_STRIDE 0x88
#define TGX_REG_BLIT_DST_STRIDE 0x8c
#define TGX_REG_BLIT_SIZE 0x90
#define TGX_REG_BLIT_COMMAND 0x94
#define TGX_REG_BLIT_STATUS 0x98
#define TGX_BLIT_STATUS_BUSY BIT(0)
#define TGX_BLIT_STATUS_DONE BIT(1)
#define TGX_BLIT_STATUS_ERROR BIT(2)
#define TGX_REG_QUEUE_FENCE 0xa0
#define TGX_REG_QUEUE_SUBMIT 0xa4
#define TGX_REG_QUEUE_STATUS 0xa8
#define TGX_REG_QUEUE_COMPLETED 0xac
#define TGX_REG_QUEUE_ERROR_FENCE 0xb0
#define TGX_REG_TRIANGLE_BASE 0x100
#define TGX_REG_TRIANGLE_STRIDE 0x104
#define TGX_REG_TRIANGLE_SIZE 0x108
#define TGX_REG_TRIANGLE_X0 0x10c
#define TGX_REG_TRIANGLE_Y0 0x110
#define TGX_REG_TRIANGLE_X1 0x114
#define TGX_REG_TRIANGLE_Y1 0x118
#define TGX_REG_TRIANGLE_X2 0x11c
#define TGX_REG_TRIANGLE_Y2 0x120
#define TGX_REG_TRIANGLE_COLOR 0x124
#define TGX_REG_TRIANGLE_GRADIENT_R 0x134
#define TGX_REG_TRIANGLE_GRADIENT_G 0x138
#define TGX_REG_TRIANGLE_GRADIENT_B 0x13c
#define TGX_REG_TRIANGLE_DEPTH_BASE 0x140
#define TGX_REG_TRIANGLE_DEPTH_CONFIG 0x144
#define TGX_REG_TRIANGLE_GRADIENT_Z_X 0x148
#define TGX_REG_TRIANGLE_GRADIENT_Z_Y 0x14c
#define TGX_QUEUE_LEVEL GENMASK(2, 0)
#define TGX_QUEUE_READY BIT(3)
#define TGX_QUEUE_ACTIVE BIT(4)
#define TGX_QUEUE_ERROR BIT(5)
/* A large GUI rectangle can be delayed by 1080p scanout arbitration. Keep the
 * kernel bounded, but do not classify a legitimate operation as failed at the
 * old 100 ms synthetic-test timeout. */
#define TGX_RENDER_TIMEOUT_MS 1000
#define TGX_REG_DMA_HOST_LO 0xc0
#define TGX_REG_DMA_HOST_HI 0xc4
#define TGX_REG_DMA_VRAM 0xc8
#define TGX_REG_DMA_LENGTH 0xcc
#define TGX_REG_DMA_COMMAND 0xd0
#define TGX_REG_DMA_STATUS 0xd4
#define TGX_REG_DMA_BUSY_CYCLES 0xd8
#define TGX_REG_DMA_TX_STALL_CYCLES 0xdc
#define TGX_REG_DMA_RX_STALL_CYCLES 0xe0
#define TGX_REG_DMA_MEMORY_STALL_CYCLES 0xe4
#define TGX_REG_DMA_ERROR_CODE 0xe8
#define TGX_REG_DMA_SLOT_FULL_CYCLES 0xec
#define TGX_REG_DMA_COMPLETION_BUSY_CYCLES 0xf0
#define TGX_DMA_STATUS_BUSY BIT(0)
#define TGX_DMA_STATUS_DONE BIT(1)
#define TGX_DMA_STATUS_ERROR BIT(2)
#define TGX_DMA_STATUS_REJECTED BIT(3)
#define TGX_DMA_STATUS_READY BIT(4)
#define TGX_DMA_TEST_BYTES 64
#define TGX_DMA_TEST_VRAM (2 * TGX_LEGACY_FRAME_BYTES)
#define TGX_DMA_TIMEOUT_US 2000000
#define TGX_1080P_DMA_TIMEOUT_US 30000000
#define TGX_CONVERSION_DMA_ROWS 120

static u32 tgx_fb_base(const struct tarugx *tgx, u32 index)
{
	return index * tgx->frame_bytes;
}

static int tgx_dma_start(struct tarugx *tgx, dma_addr_t host_address,
			 u32 vram_address, u32 length_bytes)
{
	u32 status;

	if (!length_bytes || length_bytes > tgx->vram_bytes ||
	    vram_address > tgx->vram_bytes - length_bytes)
		return -EINVAL;
	status = readl(tgx->regs + TGX_REG_DMA_STATUS);
	if (!(status & TGX_DMA_STATUS_READY) ||
	    status & (TGX_DMA_STATUS_BUSY | TGX_DMA_STATUS_REJECTED))
		return -EBUSY;
	writel(lower_32_bits(host_address), tgx->regs + TGX_REG_DMA_HOST_LO);
	writel(upper_32_bits(host_address), tgx->regs + TGX_REG_DMA_HOST_HI);
	writel(vram_address, tgx->regs + TGX_REG_DMA_VRAM);
	writel(length_bytes, tgx->regs + TGX_REG_DMA_LENGTH);
	writel(1, tgx->regs + TGX_REG_DMA_COMMAND);
	readl(tgx->regs + TGX_REG_DMA_COMMAND);
	return 0;
}

static int tgx_dma_wait(struct tarugx *tgx, unsigned int timeout_us)
{
	u32 status;
	int ret;

	ret = readl_poll_timeout(tgx->regs + TGX_REG_DMA_STATUS, status,
				 status & (TGX_DMA_STATUS_DONE |
					   TGX_DMA_STATUS_ERROR |
					   TGX_DMA_STATUS_REJECTED),
				 10, timeout_us);
	if (ret)
		return ret;
	if (!(status & TGX_DMA_STATUS_DONE) ||
	    status & (TGX_DMA_STATUS_ERROR | TGX_DMA_STATUS_REJECTED)) {
		dev_warn_ratelimited(&tgx->pdev->dev,
				     "DMA failed: status=%#x reason=%u\n",
				     status,
				     readl(tgx->regs + TGX_REG_DMA_ERROR_CODE) & 0xf);
		return -EIO;
	}
	return 0;
}

static int tgx_dma_transfer(struct tarugx *tgx, dma_addr_t host_address,
			    u32 vram_address, u32 length_bytes,
			    unsigned int timeout_us)
{
	int ret;

	ret = tgx_dma_start(tgx, host_address, vram_address, length_bytes);
	if (ret)
		return ret;
	return tgx_dma_wait(tgx, timeout_us);
}

static int tgx_dma_transfer_sg(struct tarugx *tgx, struct sg_table *sgt,
			       u32 source_offset, u32 vram_address,
			       u32 length_bytes, unsigned int timeout_us)
{
	struct scatterlist *sg;
	u32 remaining = length_bytes;
	u32 skip = source_offset;
	unsigned int i;

	if (!sgt)
		return -EINVAL;
	for_each_sgtable_dma_sg(sgt, sg, i) {
		u32 segment_length = sg_dma_len(sg);
		u32 transfer_length;
		int ret;

		if (skip >= segment_length) {
			skip -= segment_length;
			continue;
		}
		transfer_length = min(segment_length - skip, remaining);
		ret = tgx_dma_transfer(tgx, sg_dma_address(sg) + skip,
				       vram_address, transfer_length, timeout_us);
		if (ret)
			return ret;
		remaining -= transfer_length;
		vram_address += transfer_length;
		skip = 0;
		if (!remaining)
			return 0;
	}

	return -EINVAL;
}

static int tgx_blit_copy_frame(struct tarugx *tgx, u32 source, u32 target)
{
	u32 status;
	int ret;

	status = readl(tgx->regs + TGX_REG_BLIT_STATUS);
	if (status & TGX_BLIT_STATUS_BUSY)
		return -EBUSY;
	writel(source, tgx->regs + TGX_REG_BLIT_SRC);
	writel(target, tgx->regs + TGX_REG_BLIT_DST);
	writel(tgx->pitch, tgx->regs + TGX_REG_BLIT_SRC_STRIDE);
	writel(tgx->pitch, tgx->regs + TGX_REG_BLIT_DST_STRIDE);
	writel((tgx->height << 16) | tgx->width,
	       tgx->regs + TGX_REG_BLIT_SIZE);
	writel(1, tgx->regs + TGX_REG_BLIT_COMMAND);
	readl(tgx->regs + TGX_REG_BLIT_COMMAND);
	ret = readl_poll_timeout(tgx->regs + TGX_REG_BLIT_STATUS, status,
				 status & (TGX_BLIT_STATUS_DONE |
					   TGX_BLIT_STATUS_ERROR),
				 10, 100000);
	if (ret)
		return ret;
	return status & TGX_BLIT_STATUS_ERROR ? -EIO : 0;
}

static u32 tgx_active_fb_base(struct tarugx *tgx)
{
	u32 active;

	if (tgx->active_fb_base_available)
		return readl(tgx->regs + TGX_REG_ACTIVE_FB_BASE);
	active = readl(tgx->regs + TGX_REG_ACTIVE_BUFFER);
	return active <= 1 ? tgx_fb_base(tgx, active) : U32_MAX;
}

static bool tgx_rect_valid(u32 x, u32 y, u32 width, u32 height,
			   u32 surface_width, u32 surface_height)
{
	return width && height && x < surface_width && y < surface_height &&
		width <= surface_width - x && height <= surface_height - y;
}

static u32 tgx_next_render_fence(struct tarugx *tgx)
{
	tgx->render_fence++;
	if (!tgx->render_fence)
		tgx->render_fence++;
	return tgx->render_fence;
}

static const char *tgx_dma_fence_get_driver_name(struct dma_fence *fence)
{
	return "tarugx";
}

static const char *tgx_dma_fence_get_timeline_name(struct dma_fence *fence)
{
	return "tarugx-render";
}

static void tgx_dma_fence_release(struct dma_fence *fence)
{
	struct tarugx_dma_fence *tgx_fence =
		container_of(fence, struct tarugx_dma_fence, base);

	kfree(tgx_fence);
}

static const struct dma_fence_ops tgx_dma_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = tgx_dma_fence_get_driver_name,
	.get_timeline_name = tgx_dma_fence_get_timeline_name,
	.release = tgx_dma_fence_release,
};

static struct dma_fence *tgx_create_dma_fence(struct tarugx *tgx)
{
	struct tarugx_dma_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;
	dma_fence_init(&fence->base, &tgx_dma_fence_ops,
			&tgx->render_fence_lock, tgx->render_fence_context,
			atomic64_inc_return(&tgx->render_fence_seqno));
	return &fence->base;
}

static void tgx_signal_sync_fence(struct tarugx *tgx, int error)
{
	struct dma_fence *fence;
	u32 commands;
	unsigned long flags;

	spin_lock_irqsave(&tgx->render_fence_lock, flags);
	fence = tgx->render_sync_fence;
	commands = 0;
	if (fence) {
		commands = tgx->render_sync_commands;
		tgx->render_sync_fence = NULL;
		tgx->render_sync_commands = 0;
		if (error)
			dma_fence_set_error(fence, error);
		dma_fence_signal_locked(fence);
	}
	spin_unlock_irqrestore(&tgx->render_fence_lock, flags);
	if (commands) {
		if (error)
			atomic64_inc(&tgx->render_failures);
		else
			atomic64_add(commands, &tgx->render_commands);
	}
	if (fence)
		dma_fence_put(fence);
}

static void tgx_update_sync_fence(struct tarugx *tgx)
{
	u32 status;

	if (!READ_ONCE(tgx->render_sync_fence))
		return;
	if (READ_ONCE(tgx->removing)) {
		tgx_signal_sync_fence(tgx, -ENODEV);
		return;
	}
	status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	if (status & TGX_QUEUE_ERROR)
		tgx_signal_sync_fence(tgx, -EIO);
	else if (readl(tgx->regs + TGX_REG_QUEUE_COMPLETED) ==
		 READ_ONCE(tgx->render_sync_hw_fence))
		tgx_signal_sync_fence(tgx, 0);
}

static bool tgx_command_valid(const struct tarugx *tgx,
			      const struct drm_tarugx_command *command,
			      u32 width, u32 height)
{
	const s32 coord_min = -0x800000;
	const s32 coord_max = 0x7fffff;

	if (command->flags)
		return false;
	if (command->opcode == DRM_TARUGX_OP_FILL)
		return !command->src_x && !command->src_y &&
			!command->gradient_r && !command->gradient_g &&
			!command->gradient_b &&
			tgx_rect_valid(command->dst_x, command->dst_y,
				       command->width, command->height,
				       width, height);
	if (command->opcode == DRM_TARUGX_OP_BLIT)
		return !command->color && !command->gradient_r &&
			!command->gradient_g && !command->gradient_b &&
			tgx_rect_valid(command->src_x, command->src_y,
				       command->width, command->height,
				       width, height) &&
			tgx_rect_valid(command->dst_x, command->dst_y,
				       command->width, command->height,
				       width, height);
	if (command->opcode == DRM_TARUGX_OP_TRIANGLE)
		return tgx->triangle_available &&
			(tgx->triangle_interpolation_available ||
			 (!command->gradient_r && !command->gradient_g &&
			  !command->gradient_b)) &&
			command->x0 >= coord_min && command->x0 <= coord_max &&
			command->y0 >= coord_min && command->y0 <= coord_max &&
			command->x1 >= coord_min && command->x1 <= coord_max &&
			command->y1 >= coord_min && command->y1 <= coord_max &&
			command->x2 >= coord_min && command->x2 <= coord_max &&
			command->y2 >= coord_min && command->y2 <= coord_max;
	return false;
}

static void tgx_program_render(struct tarugx *tgx,
			       u32 source_base, u32 source_pitch,
			       u32 destination_base, u32 destination_pitch,
			       u32 destination_width, u32 destination_height,
			       u32 depth_base,
			       const struct tarugx_depth_state *depth,
			       const struct drm_tarugx_command *command)
{
	if (command->opcode == DRM_TARUGX_OP_FILL) {
		writel(destination_base + command->dst_y * destination_pitch +
		       command->dst_x * tgx->cpp,
		       tgx->regs + TGX_REG_FILL_ADDR);
		writel(destination_pitch, tgx->regs + TGX_REG_FILL_STRIDE);
		writel((command->height << 16) | command->width,
		       tgx->regs + TGX_REG_FILL_SIZE);
		writel(command->color, tgx->regs + TGX_REG_FILL_COLOR);
	} else if (command->opcode == DRM_TARUGX_OP_BLIT) {
		writel(source_base + command->src_y * source_pitch +
		       command->src_x * tgx->cpp,
		       tgx->regs + TGX_REG_BLIT_SRC);
		writel(destination_base + command->dst_y * destination_pitch +
		       command->dst_x * tgx->cpp,
		       tgx->regs + TGX_REG_BLIT_DST);
		writel(source_pitch, tgx->regs + TGX_REG_BLIT_SRC_STRIDE);
		writel(destination_pitch, tgx->regs + TGX_REG_BLIT_DST_STRIDE);
		writel((command->height << 16) | command->width,
		       tgx->regs + TGX_REG_BLIT_SIZE);
	} else {
		writel(destination_base, tgx->regs + TGX_REG_TRIANGLE_BASE);
		writel(destination_pitch, tgx->regs + TGX_REG_TRIANGLE_STRIDE);
		writel((destination_height << 16) | destination_width,
		       tgx->regs + TGX_REG_TRIANGLE_SIZE);
		writel((u32)command->x0, tgx->regs + TGX_REG_TRIANGLE_X0);
		writel((u32)command->y0, tgx->regs + TGX_REG_TRIANGLE_Y0);
		writel((u32)command->x1, tgx->regs + TGX_REG_TRIANGLE_X1);
		writel((u32)command->y1, tgx->regs + TGX_REG_TRIANGLE_Y1);
		writel((u32)command->x2, tgx->regs + TGX_REG_TRIANGLE_X2);
		writel((u32)command->y2, tgx->regs + TGX_REG_TRIANGLE_Y2);
		writel(command->color, tgx->regs + TGX_REG_TRIANGLE_COLOR);
		writel(command->gradient_r,
		       tgx->regs + TGX_REG_TRIANGLE_GRADIENT_R);
		writel(command->gradient_g,
		       tgx->regs + TGX_REG_TRIANGLE_GRADIENT_G);
		writel(command->gradient_b,
		       tgx->regs + TGX_REG_TRIANGLE_GRADIENT_B);
		if (tgx->depth_available) {
			u32 config = 0;

			if (depth && depth->flags & DRM_TARUGX_DEPTH_ENABLE) {
				config = depth->depth | BIT(16) |
					 ((depth->flags & DRM_TARUGX_DEPTH_WRITE) ?
					  BIT(17) : 0) |
					 ((depth->flags & DRM_TARUGX_DEPTH_FUNC_MASK)
					  << (18 - DRM_TARUGX_DEPTH_FUNC_SHIFT));
			}
			writel(config ? depth_base : 0,
			       tgx->regs + TGX_REG_TRIANGLE_DEPTH_BASE);
			writel(config, tgx->regs + TGX_REG_TRIANGLE_DEPTH_CONFIG);
			writel(config ? (u32)depth->gradient_x & GENMASK(23, 0) : 0,
			       tgx->regs + TGX_REG_TRIANGLE_GRADIENT_Z_X);
			writel(config ? (u32)depth->gradient_y & GENMASK(23, 0) : 0,
			       tgx->regs + TGX_REG_TRIANGLE_GRADIENT_Z_Y);
		}
	}
}

static int tgx_wait_render(struct tarugx *tgx, u32 fence, u32 timeout_ms)
{
	u32 status;
	long waited;

	if (READ_ONCE(tgx->removing))
		return -ENODEV;
	status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	if (status & TGX_QUEUE_ERROR) {
		dev_warn_ratelimited(tgx->drm.dev,
			"render command fence %u failed (error fence %u)\n",
			fence, readl(tgx->regs + TGX_REG_QUEUE_ERROR_FENCE));
		return -EIO;
	}
	if (readl(tgx->regs + TGX_REG_QUEUE_COMPLETED) == fence)
		return 0;
	if (!timeout_ms)
		return -EAGAIN;
	waited = wait_event_timeout(tgx->render_wait,
		READ_ONCE(tgx->removing) ||
		readl(tgx->regs + TGX_REG_QUEUE_COMPLETED) == fence ||
		readl(tgx->regs + TGX_REG_QUEUE_STATUS) & TGX_QUEUE_ERROR,
		msecs_to_jiffies(timeout_ms));
	if (READ_ONCE(tgx->removing))
		return -ENODEV;
	status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	if (status & TGX_QUEUE_ERROR) {
		dev_warn_ratelimited(tgx->drm.dev,
			"render command fence %u failed (error fence %u)\n",
			fence, readl(tgx->regs + TGX_REG_QUEUE_ERROR_FENCE));
		return -EIO;
	}
	if (!waited || readl(tgx->regs + TGX_REG_QUEUE_COMPLETED) != fence)
		return -ETIMEDOUT;
	return 0;
}

static void tgx_clear_async(struct tarugx *tgx)
{
	struct drm_gem_object *gem = tgx->render_async_gem;

	tgx->render_async_valid = false;
	tgx->render_async_completed = false;
	tgx->render_async_owner = NULL;
	tgx->render_async_fence = 0;
	tgx->render_async_commands = 0;
	tgx->render_async_result = 0;
	tgx->render_async_gem = NULL;
	drm_gem_object_put(gem);
}

static int tgx_reap_async(struct tarugx *tgx, u32 timeout_ms)
{
	int ret;

	if (!tgx->render_async_valid)
		return 0;
	if (tgx->render_async_completed)
		return tgx->render_async_result;
	ret = tgx_wait_render(tgx, tgx->render_async_fence, timeout_ms);
	if (ret == -EAGAIN || ret == -ETIMEDOUT)
		return ret;
	tgx->render_async_completed = true;
	tgx->render_async_result = ret;
	if (ret)
		atomic64_inc(&tgx->render_failures);
	else
		atomic64_add(tgx->render_async_commands,
			     &tgx->render_commands);
	return ret;
}

static int tgx_execute_commands(struct tarugx *tgx,
				const struct drm_tarugx_command *commands,
				u32 count, u32 source_base, u32 source_pitch,
				u32 destination_base, u32 destination_pitch,
				u32 destination_width, u32 destination_height,
				struct drm_file *async_owner,
				u32 *fence_out)
{
	u32 fence = 0, status, submitted = 0;
	int ret = 0, wait_ret;

	if (async_owner && atomic_read(&tgx->render_sched_jobs))
		return -EBUSY;
	if (tgx->render_async_valid) {
		/* A binary syncobj waiter does not call the private WAIT ioctl.
		 * Let the same owner reap its completed record while submitting the
		 * next asynchronous batch, but never let another file consume it. */
		if (!async_owner || tgx->render_async_owner != async_owner)
			return -EBUSY;
		ret = tgx_reap_async(tgx, 0);
		if (!tgx->render_async_completed)
			return -EBUSY;
		tgx_clear_async(tgx);
		if (ret)
			return ret;
	}
	status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	if (status & TGX_QUEUE_ERROR) {
		writel(TGX_QUEUE_ERROR, tgx->regs + TGX_REG_QUEUE_STATUS);
		status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	}
	if (status & TGX_QUEUE_ERROR)
		return -EIO;
	if (!(status & TGX_QUEUE_READY) ||
	    status & (TGX_QUEUE_ACTIVE | TGX_QUEUE_LEVEL)) {
		/* Fence completion can precede ACTIVE/LEVEL deassertion by a few
		 * device clocks. GUI clients submit the next small rectangle quickly
		 * enough to hit that interval, so a synchronous submit must wait for
		 * the idle transition instead of spuriously dropping the operation. */
		ret = readl_poll_timeout(tgx->regs + TGX_REG_QUEUE_STATUS,
					 status,
					 (status & TGX_QUEUE_READY) &&
					 !(status & (TGX_QUEUE_ACTIVE |
						     TGX_QUEUE_LEVEL |
						     TGX_QUEUE_ERROR)),
					 1, TGX_RENDER_TIMEOUT_MS * 1000);
		if (ret)
			return ret;
	}

	for (submitted = 0; submitted < count; submitted++) {
		status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
		if (!(status & TGX_QUEUE_READY)) {
			ret = -EBUSY;
			break;
		}
		tgx_program_render(tgx, source_base, source_pitch,
				   destination_base, destination_pitch,
				   destination_width, destination_height,
				   0, NULL,
				   &commands[submitted]);
		fence = tgx_next_render_fence(tgx);
		writel(fence, tgx->regs + TGX_REG_QUEUE_FENCE);
		writel(commands[submitted].opcode,
		       tgx->regs + TGX_REG_QUEUE_SUBMIT);
		readl(tgx->regs + TGX_REG_QUEUE_SUBMIT);
		status = readl(tgx->regs + TGX_REG_QUEUE_STATUS) & TGX_QUEUE_LEVEL;
		if (status > atomic64_read(&tgx->render_queue_peak))
			atomic64_set(&tgx->render_queue_peak, status);
	}
	if (!ret && async_owner) {
		tgx->render_async_valid = true;
		tgx->render_async_completed = false;
		tgx->render_async_owner = async_owner;
		tgx->render_async_fence = fence;
		tgx->render_async_commands = submitted;
		tgx->render_async_result = 0;
		*fence_out = fence;
		return 0;
	}

	/* Even an unexpected partial enqueue is drained before releasing the
	 * render lock, so KMS cannot start a direct blit against an active queue. */
	if (submitted) {
		wait_ret = tgx_wait_render(tgx, fence, TGX_RENDER_TIMEOUT_MS);
		if (!ret)
			ret = wait_ret;
	}
	if (ret) {
		atomic64_inc(&tgx->render_failures);
		return ret;
	}

	atomic64_add(count, &tgx->render_commands);
	*fence_out = fence;
	return 0;
}

static struct dma_fence *tgx_sched_run_job(struct drm_sched_job *sched_job)
{
	struct tarugx_sched_job *job =
		container_of(sched_job, struct tarugx_sched_job, base);
	struct tarugx *tgx = job->tgx;
	struct dma_fence *hw_fence = NULL;
	unsigned long flags;
	u32 fence = 0, status, submitted = 0;
	int idx, ret;

	ret = job->input_fence ? dma_fence_get_status(job->input_fence) : 1;
	if (ret < 0) {
		/* A failed producer must not allow dependent writes to execute. */
	} else if (!drm_dev_enter(&tgx->drm, &idx)) {
		ret = -ENODEV;
	} else {
		mutex_lock(&tgx->render_lock);
		status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
		if (status & TGX_QUEUE_ERROR) {
			writel(TGX_QUEUE_ERROR,
			       tgx->regs + TGX_REG_QUEUE_STATUS);
			status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
		}
		if (status & TGX_QUEUE_ERROR) {
			ret = -EIO;
		} else if (!(status & TGX_QUEUE_READY) ||
			   status & (TGX_QUEUE_ACTIVE | TGX_QUEUE_LEVEL)) {
			ret = -EBUSY;
		} else {
			hw_fence = tgx_create_dma_fence(tgx);
			ret = hw_fence ? 0 : -ENOMEM;
		}
		for (; !ret && submitted < job->count; submitted++) {
			status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
			if (!(status & TGX_QUEUE_READY)) {
				ret = -EBUSY;
				break;
			}
			tgx_program_render(tgx, job->vram_base, job->pitch,
					   job->vram_base, job->pitch,
					   job->width, job->height,
					   job->depth_base,
					   &job->depth[submitted],
					   &job->commands[submitted]);
			fence = tgx_next_render_fence(tgx);
			writel(fence, tgx->regs + TGX_REG_QUEUE_FENCE);
			writel(job->commands[submitted].opcode,
			       tgx->regs + TGX_REG_QUEUE_SUBMIT);
			readl(tgx->regs + TGX_REG_QUEUE_SUBMIT);
			status = readl(tgx->regs + TGX_REG_QUEUE_STATUS) &
				 TGX_QUEUE_LEVEL;
			if (status > atomic64_read(&tgx->render_queue_peak))
				atomic64_set(&tgx->render_queue_peak, status);
		}
		if (!ret) {
			spin_lock_irqsave(&tgx->render_fence_lock, flags);
			WARN_ON(tgx->render_sync_fence);
			tgx->render_sync_hw_fence = fence;
			tgx->render_sync_commands = submitted;
			tgx->render_sync_fence = dma_fence_get(hw_fence);
			spin_unlock_irqrestore(&tgx->render_fence_lock, flags);
		} else if (submitted) {
			/* Drain an unexpected partial enqueue before another job can
			 * touch the single hardware queue. */
			tgx_wait_render(tgx, fence, TGX_RENDER_TIMEOUT_MS);
		}
		mutex_unlock(&tgx->render_lock);
		if (!ret)
			tgx_update_sync_fence(tgx);
		drm_dev_exit(idx);
	}
	if (ret) {
		dma_fence_put(hw_fence);
		dma_fence_set_error(&sched_job->s_fence->finished, ret);
		atomic64_inc(&tgx->render_failures);
		return NULL;
	}
	return hw_fence;
}

static enum drm_gpu_sched_stat
tgx_sched_timedout_job(struct drm_sched_job *sched_job)
{
	struct tarugx_sched_job *job =
		container_of(sched_job, struct tarugx_sched_job, base);

	tgx_signal_sync_fence(job->tgx, -ETIMEDOUT);
	return DRM_GPU_SCHED_STAT_NOMINAL;
}

static void tgx_sched_free_job(struct drm_sched_job *sched_job)
{
	struct tarugx_sched_job *job =
		container_of(sched_job, struct tarugx_sched_job, base);

	drm_sched_job_cleanup(sched_job);
	dma_fence_put(job->input_fence);
	drm_gem_object_put(job->gem);
	atomic_dec(&job->tgx->render_sched_jobs);
	kfree(job);
}

static const struct drm_sched_backend_ops tgx_sched_ops = {
	.run_job = tgx_sched_run_job,
	.timedout_job = tgx_sched_timedout_job,
	.free_job = tgx_sched_free_job,
};

static int tgx_execute_screen_commands(struct tarugx *tgx,
				       const struct drm_tarugx_command *commands,
				       u32 count,
				       struct drm_file *async_owner,
				       u32 *fence_out)
{
	u32 active = tgx_active_fb_base(tgx);

	if (active > tgx->vram_bytes - tgx->frame_bytes)
		return -EIO;
	return tgx_execute_commands(tgx, commands, count,
				    active, tgx->pitch, active, tgx->pitch,
				    tgx->width, tgx->height,
				    async_owner, fence_out);
}

static int tgx_ioctl_fill(struct drm_device *drm, void *data,
			  struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_fill *args = data;
	struct drm_tarugx_command command = {
		.opcode = DRM_TARUGX_OP_FILL,
		.dst_x = args->x,
		.dst_y = args->y,
		.width = args->width,
		.height = args->height,
		.color = args->color,
	};
	int idx, ret;

	/* DRM_MASTER is not a render-node access barrier: DRM core deliberately
	 * skips primary-node authentication there.  Screen-addressed commands
	 * therefore need an explicit primary-master check in the driver. */
	if (file->minor->type != DRM_MINOR_PRIMARY ||
	    !drm_is_current_master(file))
		return -EACCES;
	if (args->flags || args->pad ||
	    !tgx_rect_valid(args->x, args->y, args->width, args->height,
			    tgx->width, tgx->height))
		return -EINVAL;
	if (!tgx->render_available)
		return -EOPNOTSUPP;
	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	mutex_lock(&tgx->render_lock);
	ret = tgx_execute_screen_commands(tgx, &command, 1, NULL,
					  &args->fence);
	mutex_unlock(&tgx->render_lock);
	drm_dev_exit(idx);
	return ret;
}

static int tgx_ioctl_blit(struct drm_device *drm, void *data,
			  struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_blit *args = data;
	struct drm_tarugx_command command = {
		.opcode = DRM_TARUGX_OP_BLIT,
		.src_x = args->src_x,
		.src_y = args->src_y,
		.dst_x = args->dst_x,
		.dst_y = args->dst_y,
		.width = args->width,
		.height = args->height,
	};
	int idx, ret;

	if (file->minor->type != DRM_MINOR_PRIMARY ||
	    !drm_is_current_master(file))
		return -EACCES;
	if (args->flags ||
	    !tgx_rect_valid(args->src_x, args->src_y,
			    args->width, args->height,
			    tgx->width, tgx->height) ||
	    !tgx_rect_valid(args->dst_x, args->dst_y,
			    args->width, args->height,
			    tgx->width, tgx->height))
		return -EINVAL;
	if (!tgx->render_available)
		return -EOPNOTSUPP;
	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	mutex_lock(&tgx->render_lock);
	ret = tgx_execute_screen_commands(tgx, &command, 1, NULL,
					  &args->fence);
	mutex_unlock(&tgx->render_lock);
	drm_dev_exit(idx);
	return ret;
}

static int tgx_ioctl_submit(struct drm_device *drm, void *data,
			    struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_submit *args = data;
	u32 i;
	int idx, ret;

	if (file->minor->type != DRM_MINOR_PRIMARY ||
	    !drm_is_current_master(file))
		return -EACCES;
	if (!args->count || args->count > DRM_TARUGX_MAX_COMMANDS ||
	    args->flags & ~DRM_TARUGX_SUBMIT_ASYNC || args->pad)
		return -EINVAL;
	for (i = 0; i < args->count; i++)
		if (!tgx_command_valid(tgx, &args->commands[i],
				       tgx->width, tgx->height))
			return -EINVAL;
	if (!tgx->render_available)
		return -EOPNOTSUPP;
	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	mutex_lock(&tgx->render_lock);
	ret = tgx_execute_screen_commands(tgx, args->commands, args->count,
					  args->flags &
					  DRM_TARUGX_SUBMIT_ASYNC ? file : NULL,
					  &args->fence);
	mutex_unlock(&tgx->render_lock);
	drm_dev_exit(idx);
	return ret;
}

static int tgx_ioctl_wait(struct drm_device *drm, void *data,
			  struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_wait *args = data;
	int idx, ret;

	if (args->flags || args->pad || args->timeout_ms > 1000)
		return -EINVAL;
	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	mutex_lock(&tgx->render_lock);
	if (!tgx->render_async_valid || tgx->render_async_owner != file ||
	    tgx->render_async_fence != args->fence) {
		ret = -EPERM;
		goto out_unlock;
	}
	ret = tgx_reap_async(tgx, args->timeout_ms);
	if (tgx->render_async_completed)
		tgx_clear_async(tgx);
out_unlock:
	mutex_unlock(&tgx->render_lock);
	drm_dev_exit(idx);
	return ret;
}

static void tgx_vram_vm_open(struct vm_area_struct *vma)
{
	drm_gem_vm_open(vma);
}

static void tgx_vram_vm_close(struct vm_area_struct *vma)
{
	drm_gem_vm_close(vma);
}

static const struct vm_operations_struct tgx_vram_vm_ops = {
	.open = tgx_vram_vm_open,
	.close = tgx_vram_vm_close,
};

static int tgx_vram_object_mmap(struct drm_gem_object *gem,
				struct vm_area_struct *vma)
{
	struct tarugx_vram_object *object =
		container_of(gem, struct tarugx_vram_object, base);
	struct tarugx *tgx = container_of(gem->dev, struct tarugx, drm);
	unsigned long length = vma->vm_end - vma->vm_start;
	resource_size_t physical;

	if (length > gem->size)
		return -EINVAL;
	if (gem->size > TGX_BAR2_BYTES ||
	    object->node.start > TGX_BAR2_BYTES - gem->size)
		return -EOPNOTSUPP;
	physical = pci_resource_start(tgx->pdev, 2) + object->node.start;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_ops = &tgx_vram_vm_ops;
	return io_remap_pfn_range(vma, vma->vm_start,
				  physical >> PAGE_SHIFT, length,
				  vma->vm_page_prot);
}

static void tgx_vram_object_free(struct drm_gem_object *gem)
{
	struct tarugx_vram_object *object =
		container_of(gem, struct tarugx_vram_object, base);
	struct tarugx *tgx = container_of(gem->dev, struct tarugx, drm);

	mutex_lock(&tgx->vram_lock);
	if (object->front)
		tgx->front_allocated = false;
	else if (drm_mm_node_allocated(&object->node))
		drm_mm_remove_node(&object->node);
	mutex_unlock(&tgx->vram_lock);
	drm_gem_object_release(gem);
	kfree(object);
}

static int tgx_vram_object_vmap(struct drm_gem_object *gem,
				struct iosys_map *map)
{
	struct tarugx_vram_object *object =
		container_of(gem, struct tarugx_vram_object, base);
	struct tarugx *tgx = container_of(gem->dev, struct tarugx, drm);

	if (gem->size > TGX_BAR2_BYTES ||
	    object->node.start > TGX_BAR2_BYTES - gem->size)
		return -EOPNOTSUPP;
	iosys_map_set_vaddr_iomem(map, tgx->vram + object->node.start);
	return 0;
}

static void tgx_vram_object_vunmap(struct drm_gem_object *gem,
				   struct iosys_map *map)
{
	(void)gem;
	iosys_map_clear(map);
}

static const struct drm_gem_object_funcs tgx_vram_object_funcs = {
	.free = tgx_vram_object_free,
	.vmap = tgx_vram_object_vmap,
	.vunmap = tgx_vram_object_vunmap,
	.mmap = tgx_vram_object_mmap,
};

static int tgx_ioctl_vram_create(struct drm_device *drm, void *data,
				 struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_vram_create *args = data;
	struct tarugx_vram_object *object;
	u64 color_size, size;
	int ret;

	if (!args->width || args->width > tgx->width || !args->height ||
	    args->height > tgx->height ||
	    args->flags & ~(DRM_TARUGX_VRAM_FRONT |
			    DRM_TARUGX_VRAM_DEPTH) || args->depth_offset)
		return -EINVAL;
	if (args->flags & DRM_TARUGX_VRAM_DEPTH) {
		if (!tgx->depth_available || tgx->cpp != 2 ||
		    args->flags & DRM_TARUGX_VRAM_FRONT)
			return -EOPNOTSUPP;
	}
	if (args->flags & DRM_TARUGX_VRAM_FRONT) {
		if (!drm_is_current_master(file))
			return -EACCES;
		if (args->width != tgx->width || args->height != tgx->height ||
		    tgx->format != DRM_FORMAT_RGB565)
			return -EINVAL;
	}
	args->pitch = args->width * tgx->cpp;
	color_size = PAGE_ALIGN((u64)args->pitch * args->height);
	size = color_size;
	if (args->flags & DRM_TARUGX_VRAM_DEPTH) {
		if (color_size > U32_MAX || color_size > U64_MAX / 2)
			return -EOVERFLOW;
		size = color_size * 2;
	}
	if (size > tgx->vram_gem_bytes)
		return -ENOSPC;
	object = kzalloc(sizeof(*object), GFP_KERNEL);
	if (!object)
		return -ENOMEM;
	object->width = args->width;
	object->height = args->height;
	object->pitch = args->pitch;
	object->front = args->flags & DRM_TARUGX_VRAM_FRONT;
	object->depth = args->flags & DRM_TARUGX_VRAM_DEPTH;
	object->depth_offset = object->depth ? (u32)color_size : 0;
	object->base.funcs = &tgx_vram_object_funcs;
	ret = drm_gem_object_init(drm, &object->base, size);
	if (ret) {
		kfree(object);
		return ret;
	}
	mutex_lock(&tgx->vram_lock);
	if (object->front) {
		if (tgx->front_allocated) {
			ret = -EBUSY;
		} else {
			tgx->front_allocated = true;
			object->node.start = 0;
			ret = 0;
		}
	} else {
		ret = drm_mm_insert_node_generic(&tgx->vram_mm, &object->node,
					 size, PAGE_SIZE, 0, DRM_MM_INSERT_BEST);
	}
	mutex_unlock(&tgx->vram_lock);
	if (ret)
		goto out_put;
	ret = drm_gem_create_mmap_offset(&object->base);
	if (ret)
		goto out_put;
	ret = drm_gem_handle_create(file, &object->base, &args->handle);
	if (ret)
		goto out_put;
	args->size = size;
	args->depth_offset = object->depth_offset;
	args->mmap_offset = drm_vma_node_offset_addr(&object->base.vma_node);
out_put:
	drm_gem_object_put(&object->base);
	return ret;
}

static int tgx_ioctl_submit_gem(struct drm_device *drm, void *data,
				struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_submit_gem *args = data;
	struct drm_gem_object *gem;
	struct drm_syncobj *syncobj = NULL;
	struct dma_fence *sync_fence = NULL;
	struct dma_fence_chain *sync_chain = NULL;
	struct tarugx_vram_object *object;
	unsigned long irq_flags;
	bool resv_locked = false;
	bool async;
	u32 i;
	int idx, ret;

	if (!args->count || args->count > DRM_TARUGX_MAX_COMMANDS ||
	    args->flags & ~DRM_TARUGX_SUBMIT_ASYNC || args->pad)
		return -EINVAL;
	async = args->flags & DRM_TARUGX_SUBMIT_ASYNC;
	if ((args->out_syncobj && !async) ||
	    (args->out_point && (!args->out_syncobj || !async)))
		return -EINVAL;
	if (args->out_syncobj) {
		syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!syncobj)
			return -ENOENT;
	}
	if (async) {
		sync_fence = tgx_create_dma_fence(tgx);
		if (!sync_fence) {
			ret = -ENOMEM;
			goto out_syncobj;
		}
		if (args->out_point) {
			sync_chain = dma_fence_chain_alloc();
			if (!sync_chain) {
				ret = -ENOMEM;
				goto out_fence;
			}
		}
	}
	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem) {
		ret = -ENOENT;
		goto out_fence;
	}
	if (gem->funcs != &tgx_vram_object_funcs) {
		ret = -EINVAL;
		goto out_put;
	}
	object = container_of(gem, struct tarugx_vram_object, base);
	for (i = 0; i < args->count; i++) {
		if (!tgx_command_valid(tgx, &args->commands[i], object->width,
				       object->height)) {
			ret = -EINVAL;
			goto out_put;
		}
	}
	if (!tgx->render_available) {
		ret = -EOPNOTSUPP;
		goto out_put;
	}
	if (!drm_dev_enter(drm, &idx)) {
		ret = -ENODEV;
		goto out_put;
	}
	if (sync_fence) {
		ret = dma_resv_lock_interruptible(gem->resv, NULL);
		if (ret)
			goto out_dev;
		resv_locked = true;
		ret = dma_resv_reserve_fences(gem->resv, 1);
		if (ret)
			goto out_resv;
	}
	mutex_lock(&tgx->render_submit_lock);
	mutex_lock(&tgx->render_lock);
	ret = tgx_execute_commands(tgx, args->commands, args->count,
				   object->node.start, object->pitch,
				   object->node.start, object->pitch,
				   object->width, object->height,
				   async ? file : NULL, &args->fence);
	if (!ret && async) {
		tgx->render_async_gem = gem;
		gem = NULL;
		if (sync_fence) {
			/* Reservation and optional syncobj take references.  The original
			 * remains owned by the IRQ path until signal or teardown. */
			dma_resv_add_fence(object->base.resv, sync_fence,
					   DMA_RESV_USAGE_WRITE);
			if (syncobj) {
				if (args->out_point) {
					drm_syncobj_add_point(syncobj, sync_chain,
							      sync_fence,
							      args->out_point);
					sync_chain = NULL;
				} else {
					drm_syncobj_replace_fence(syncobj, sync_fence);
				}
			}
			spin_lock_irqsave(&tgx->render_fence_lock, irq_flags);
			WARN_ON(tgx->render_sync_fence);
			tgx->render_sync_hw_fence = args->fence;
			tgx->render_sync_fence = sync_fence;
			sync_fence = NULL;
			spin_unlock_irqrestore(&tgx->render_fence_lock, irq_flags);
		}
	}
	mutex_unlock(&tgx->render_lock);
	mutex_unlock(&tgx->render_submit_lock);
	if (!ret)
		tgx_update_sync_fence(tgx);
out_resv:
	if (resv_locked)
		dma_resv_unlock(object->base.resv);
out_dev:
	drm_dev_exit(idx);
out_put:
	drm_gem_object_put(gem);
out_fence:
	if (sync_chain)
		dma_fence_chain_free(sync_chain);
	if (sync_fence)
		dma_fence_put(sync_fence);
out_syncobj:
	if (syncobj)
		drm_syncobj_put(syncobj);
	return ret;
}

static int tgx_ioctl_submit_gem2(struct drm_device *drm, void *data,
				 struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_submit_gem2 *args = data;
	struct tarugx_sched_job *job = NULL;
	struct tarugx_vram_object *object;
	struct drm_gem_object *gem = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct dma_fence_chain *out_chain = NULL;
	struct dma_fence *in_fence = NULL;
	struct dma_fence *dependency;
	struct dma_fence *finished;
	bool gate_held = false, job_initialized = false, resv_locked = false;
	u32 entity_index, i;
	int ret;

	if (!args->count || args->count > DRM_TARUGX_MAX_COMMANDS || args->pad ||
	    args->flags != DRM_TARUGX_SUBMIT_ASYNC ||
	    (args->in_point && !args->in_syncobj) ||
	    (args->out_point && !args->out_syncobj))
		return -EINVAL;
	if (!tgx->render_sched_initialized)
		return -EOPNOTSUPP;
	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}
	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem) {
		ret = -ENOENT;
		goto out;
	}
	if (gem->funcs != &tgx_vram_object_funcs) {
		ret = -EINVAL;
		goto out;
	}
	object = container_of(gem, struct tarugx_vram_object, base);
	for (i = 0; i < args->count; i++) {
		if (!tgx_command_valid(tgx, &args->commands[i], object->width,
				       object->height)) {
			ret = -EINVAL;
			goto out;
		}
	}
	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job) {
		ret = -ENOMEM;
		goto out;
	}
	job->tgx = tgx;
	job->gem = gem;
	job->count = args->count;
	job->vram_base = object->node.start;
	job->pitch = object->pitch;
	job->width = object->width;
	job->height = object->height;
	for (i = 0; i < args->count; i++)
		job->commands[i] = args->commands[i];
	entity_index = (u32)atomic_inc_return(&tgx->render_entity_next) - 1;
	entity_index %= tgx->render_entity_count;
	ret = drm_sched_job_init(&job->base,
				 &tgx->render_entities[entity_index], 1, tgx);
	if (ret)
		goto out;
	job_initialized = true;
	if (args->in_syncobj) {
		ret = drm_syncobj_find_fence(file, args->in_syncobj,
					     args->in_point, 0, &in_fence);
		if (ret)
			goto out;
		job->input_fence = dma_fence_get(in_fence);
		/* drm_sched_job_add_dependency consumes its fence reference on
		 * both success and failure. */
		dependency = in_fence;
		in_fence = NULL;
		ret = drm_sched_job_add_dependency(&job->base, dependency);
		if (ret)
			goto out;
	}
	if (args->out_point) {
		out_chain = dma_fence_chain_alloc();
		if (!out_chain) {
			ret = -ENOMEM;
			goto out;
		}
	}
	mutex_lock(&tgx->render_submit_lock);
	if (tgx->render_async_valid && tgx->render_async_owner == file) {
		ret = tgx_reap_async(tgx, 0);
		if (tgx->render_async_completed)
			tgx_clear_async(tgx);
		if (ret && ret != -EAGAIN)
			goto out_unlock_gate;
	}
	if (tgx->removing) {
		ret = -ENODEV;
	} else if (tgx->render_async_valid) {
		ret = -EBUSY;
	} else {
		i = atomic_inc_return(&tgx->render_sched_jobs);
		if (i > TGX_RENDER_JOB_LIMIT) {
			atomic_dec(&tgx->render_sched_jobs);
			ret = -EBUSY;
		} else {
			if (i > atomic64_read(&tgx->render_sched_peak))
				atomic64_set(&tgx->render_sched_peak, i);
			gate_held = true;
			ret = 0;
		}
	}
out_unlock_gate:
	mutex_unlock(&tgx->render_submit_lock);
	if (ret)
		goto out;
	ret = dma_resv_lock_interruptible(gem->resv, NULL);
	if (ret)
		goto out;
	resv_locked = true;
	ret = drm_sched_job_add_resv_dependencies(&job->base, gem->resv,
						  DMA_RESV_USAGE_WRITE);
	if (ret)
		goto out;
	ret = dma_resv_reserve_fences(gem->resv, 1);
	if (ret)
		goto out;
	drm_sched_job_arm(&job->base);
	finished = &job->base.s_fence->finished;
	dma_resv_add_fence(gem->resv, finished, DMA_RESV_USAGE_WRITE);
	if (out_syncobj) {
		if (args->out_point) {
			drm_syncobj_add_point(out_syncobj, out_chain, finished,
					      args->out_point);
			out_chain = NULL;
		} else {
			drm_syncobj_replace_fence(out_syncobj, finished);
		}
	}
	dma_resv_unlock(gem->resv);
	resv_locked = false;
	/* Keep the SPSC entity producer side serialized without waiting for the
	 * hardware execution lock held by the scheduler worker. */
	mutex_lock(&tgx->render_submit_lock);
	drm_sched_entity_push_job(&job->base);
	mutex_unlock(&tgx->render_submit_lock);
	gem = NULL;
	job = NULL;
	job_initialized = false;
	gate_held = false;
	ret = 0;
out:
	if (resv_locked)
		dma_resv_unlock(gem->resv);
	if (job_initialized)
		drm_sched_job_cleanup(&job->base);
	if (job)
		dma_fence_put(job->input_fence);
	if (gate_held)
		atomic_dec(&tgx->render_sched_jobs);
	if (out_chain)
		dma_fence_chain_free(out_chain);
	dma_fence_put(in_fence);
	drm_gem_object_put(gem);
	kfree(job);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	return ret;
}

static bool tgx_command3d_valid(const struct tarugx *tgx,
				const struct drm_tarugx_command3d *command,
				u32 width, u32 height)
{
	const u32 valid_flags = DRM_TARUGX_DEPTH_ENABLE |
		DRM_TARUGX_DEPTH_WRITE | DRM_TARUGX_DEPTH_FUNC_MASK;

	if (!tgx_command_valid(tgx, &command->command, width, height) ||
	    command->depth_flags & ~valid_flags || command->depth > U16_MAX ||
	    command->gradient_z_x < -0x800000 ||
	    command->gradient_z_x > 0x7fffff ||
	    command->gradient_z_y < -0x800000 ||
	    command->gradient_z_y > 0x7fffff)
		return false;
	if (command->command.opcode != DRM_TARUGX_OP_TRIANGLE)
		return !command->depth_flags && !command->depth &&
			!command->gradient_z_x && !command->gradient_z_y;
	if (!(command->depth_flags & DRM_TARUGX_DEPTH_ENABLE))
		return !command->depth_flags && !command->depth &&
			!command->gradient_z_x && !command->gradient_z_y;
	if (!tgx->depth_available ||
	    ((command->depth_flags & DRM_TARUGX_DEPTH_WRITE) &&
	     !(command->depth_flags & DRM_TARUGX_DEPTH_ENABLE)))
		return false;
	return true;
}

static int tgx_ioctl_submit_gem3(struct drm_device *drm, void *data,
				 struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_submit_gem3 *args = data;
	struct tarugx_sched_job *job = NULL;
	struct tarugx_vram_object *object;
	struct drm_gem_object *gem = NULL;
	struct drm_syncobj *out_syncobj = NULL;
	struct dma_fence_chain *out_chain = NULL;
	struct dma_fence *in_fence = NULL;
	struct dma_fence *dependency;
	struct dma_fence *finished;
	bool gate_held = false, job_initialized = false, resv_locked = false;
	u32 entity_index, i;
	int ret;

	if (!args->count || args->count > DRM_TARUGX_MAX_COMMANDS || args->pad ||
	    args->flags != DRM_TARUGX_SUBMIT_ASYNC ||
	    (args->in_point && !args->in_syncobj) ||
	    (args->out_point && !args->out_syncobj))
		return -EINVAL;
	if (!tgx->render_sched_initialized || !tgx->depth_available)
		return -EOPNOTSUPP;
	if (args->out_syncobj) {
		out_syncobj = drm_syncobj_find(file, args->out_syncobj);
		if (!out_syncobj)
			return -ENOENT;
	}
	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem) {
		ret = -ENOENT;
		goto out;
	}
	if (gem->funcs != &tgx_vram_object_funcs) {
		ret = -EINVAL;
		goto out;
	}
	object = container_of(gem, struct tarugx_vram_object, base);
	if (!object->depth) {
		ret = -EINVAL;
		goto out;
	}
	for (i = 0; i < args->count; i++) {
		if (!tgx_command3d_valid(tgx, &args->commands[i],
					 object->width, object->height)) {
			ret = -EINVAL;
			goto out;
		}
	}
	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job) {
		ret = -ENOMEM;
		goto out;
	}
	job->tgx = tgx;
	job->gem = gem;
	job->count = args->count;
	job->vram_base = object->node.start;
	job->pitch = object->pitch;
	job->width = object->width;
	job->height = object->height;
	job->depth_base = object->node.start + object->depth_offset;
	for (i = 0; i < args->count; i++) {
		job->commands[i] = args->commands[i].command;
		job->depth[i].flags = args->commands[i].depth_flags;
		job->depth[i].depth = args->commands[i].depth;
		job->depth[i].gradient_x = args->commands[i].gradient_z_x;
		job->depth[i].gradient_y = args->commands[i].gradient_z_y;
	}
	entity_index = (u32)atomic_inc_return(&tgx->render_entity_next) - 1;
	entity_index %= tgx->render_entity_count;
	ret = drm_sched_job_init(&job->base,
				 &tgx->render_entities[entity_index], 1, tgx);
	if (ret)
		goto out;
	job_initialized = true;
	if (args->in_syncobj) {
		ret = drm_syncobj_find_fence(file, args->in_syncobj,
					     args->in_point, 0, &in_fence);
		if (ret)
			goto out;
		job->input_fence = dma_fence_get(in_fence);
		dependency = in_fence;
		in_fence = NULL;
		ret = drm_sched_job_add_dependency(&job->base, dependency);
		if (ret)
			goto out;
	}
	if (args->out_point) {
		out_chain = dma_fence_chain_alloc();
		if (!out_chain) {
			ret = -ENOMEM;
			goto out;
		}
	}
	mutex_lock(&tgx->render_submit_lock);
	if (tgx->render_async_valid && tgx->render_async_owner == file) {
		ret = tgx_reap_async(tgx, 0);
		if (tgx->render_async_completed)
			tgx_clear_async(tgx);
		if (ret && ret != -EAGAIN)
			goto out_unlock_gate3;
	}
	if (tgx->removing) {
		ret = -ENODEV;
	} else if (tgx->render_async_valid) {
		ret = -EBUSY;
	} else {
		i = atomic_inc_return(&tgx->render_sched_jobs);
		if (i > TGX_RENDER_JOB_LIMIT) {
			atomic_dec(&tgx->render_sched_jobs);
			ret = -EBUSY;
		} else {
			if (i > atomic64_read(&tgx->render_sched_peak))
				atomic64_set(&tgx->render_sched_peak, i);
			gate_held = true;
			ret = 0;
		}
	}
out_unlock_gate3:
	mutex_unlock(&tgx->render_submit_lock);
	if (ret)
		goto out;
	ret = dma_resv_lock_interruptible(gem->resv, NULL);
	if (ret)
		goto out;
	resv_locked = true;
	ret = drm_sched_job_add_resv_dependencies(&job->base, gem->resv,
						  DMA_RESV_USAGE_WRITE);
	if (ret)
		goto out;
	ret = dma_resv_reserve_fences(gem->resv, 1);
	if (ret)
		goto out;
	drm_sched_job_arm(&job->base);
	finished = &job->base.s_fence->finished;
	dma_resv_add_fence(gem->resv, finished, DMA_RESV_USAGE_WRITE);
	if (out_syncobj) {
		if (args->out_point) {
			drm_syncobj_add_point(out_syncobj, out_chain, finished,
					      args->out_point);
			out_chain = NULL;
		} else {
			drm_syncobj_replace_fence(out_syncobj, finished);
		}
	}
	dma_resv_unlock(gem->resv);
	resv_locked = false;
	mutex_lock(&tgx->render_submit_lock);
	drm_sched_entity_push_job(&job->base);
	mutex_unlock(&tgx->render_submit_lock);
	gem = NULL;
	job = NULL;
	job_initialized = false;
	gate_held = false;
	ret = 0;
out:
	if (resv_locked)
		dma_resv_unlock(gem->resv);
	if (job_initialized)
		drm_sched_job_cleanup(&job->base);
	if (job)
		dma_fence_put(job->input_fence);
	if (gate_held)
		atomic_dec(&tgx->render_sched_jobs);
	if (out_chain)
		dma_fence_chain_free(out_chain);
	dma_fence_put(in_fence);
	drm_gem_object_put(gem);
	kfree(job);
	if (out_syncobj)
		drm_syncobj_put(out_syncobj);
	return ret;
}

static int tgx_ioctl_upload_gem(struct drm_device *drm, void *data,
				struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_upload_gem *args = data;
	struct tarugx_host_object *source;
	struct tarugx_vram_object *destination;
	struct drm_gem_object *source_gem = NULL, *destination_gem = NULL;
	u32 source_offset, destination_offset, row_bytes, row;
	long waited;
	int idx, ret = 0;

	if (args->flags || memchr_inv(args->pad, 0, sizeof(args->pad)) ||
	    !args->width || !args->height || (args->source_x & 1) ||
	    (args->destination_x & 1) || (args->width & 1))
		return -EINVAL;
	source_gem = drm_gem_object_lookup(file, args->source_handle);
	if (!source_gem)
		return -ENOENT;
	destination_gem = drm_gem_object_lookup(file,
						args->destination_handle);
	if (!destination_gem) {
		ret = -ENOENT;
		goto out;
	}
	if (source_gem->funcs != &tgx_host_object_funcs ||
	    destination_gem->funcs != &tgx_vram_object_funcs) {
		ret = -EINVAL;
		goto out;
	}
	source = container_of(source_gem, struct tarugx_host_object, base);
	destination = container_of(destination_gem,
				   struct tarugx_vram_object, base);
	if (source->cpp != tgx->cpp || args->source_x >= source->width ||
	    args->source_y >= source->height ||
	    args->destination_x >= destination->width ||
	    args->destination_y >= destination->height ||
	    args->width > source->width - args->source_x ||
	    args->height > source->height - args->source_y ||
	    args->width > destination->width - args->destination_x ||
	    args->height > destination->height - args->destination_y) {
		ret = -EINVAL;
		goto out;
	}
	row_bytes = args->width * tgx->cpp;
	source_offset = args->source_y * source->pitch +
			args->source_x * source->cpp;
	destination_offset = destination->node.start +
			     args->destination_y * destination->pitch +
			     args->destination_x * tgx->cpp;
	if ((source_offset | destination_offset | row_bytes) & 3) {
		ret = -EINVAL;
		goto out;
	}
	waited = dma_resv_wait_timeout(destination_gem->resv,
				       DMA_RESV_USAGE_WRITE, true,
				       msecs_to_jiffies(TGX_RENDER_TIMEOUT_MS));
	if (waited <= 0) {
		ret = waited ? (int)waited : -ETIMEDOUT;
		goto out;
	}
	if (!tgx->dma_available) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (!drm_dev_enter(drm, &idx)) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&tgx->render_submit_lock);
	mutex_lock(&tgx->render_lock);
	if (tgx->render_async_valid) {
		ret = tgx_reap_async(tgx, TGX_RENDER_TIMEOUT_MS);
		if (ret)
			goto out_unlock_render;
	}
	mutex_lock(&tgx->dma_lock);
	dma_wmb();
	if (!args->source_x && !args->destination_x &&
	    row_bytes == source->pitch && row_bytes == destination->pitch) {
		ret = tgx_dma_transfer(tgx, source->dma_addr + source_offset,
				       destination_offset,
				       row_bytes * args->height,
				       TGX_1080P_DMA_TIMEOUT_US);
	} else {
		for (row = 0; row < args->height; row++) {
			ret = tgx_dma_transfer(tgx,
					source->dma_addr + source_offset +
					row * source->pitch,
					destination_offset +
					row * destination->pitch,
					row_bytes, TGX_DMA_TIMEOUT_US);
			if (ret)
				break;
		}
	}
	mutex_unlock(&tgx->dma_lock);
	if (ret) {
		atomic64_inc(&tgx->dma_upload_failures);
	} else {
		atomic64_inc(&tgx->dma_uploads);
		atomic64_inc(&tgx->partial_dma_uploads);
		atomic64_add((u64)row_bytes * args->height,
			     &tgx->partial_dma_bytes);
	}
out_unlock_render:
	mutex_unlock(&tgx->render_lock);
	mutex_unlock(&tgx->render_submit_lock);
	drm_dev_exit(idx);
out:
	drm_gem_object_put(destination_gem);
	drm_gem_object_put(source_gem);
	return ret;
}

static int tgx_ioctl_copy_gem(struct drm_device *drm, void *data,
			      struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_copy_gem *args = data;
	struct drm_gem_object *source_gem = NULL, *destination_gem = NULL;
	struct tarugx_vram_object *source, *destination;
	struct drm_tarugx_command command = {
		.opcode = DRM_TARUGX_OP_BLIT,
	};
	long waited;
	u32 fence;
	int idx, ret = 0;

	if (args->flags || memchr_inv(args->pad, 0, sizeof(args->pad)) ||
	    !args->width || !args->height ||
	    args->source_handle == args->destination_handle)
		return -EINVAL;
	source_gem = drm_gem_object_lookup(file, args->source_handle);
	if (!source_gem)
		return -ENOENT;
	destination_gem = drm_gem_object_lookup(file,
						args->destination_handle);
	if (!destination_gem) {
		ret = -ENOENT;
		goto out;
	}
	if (source_gem->funcs != &tgx_vram_object_funcs ||
	    destination_gem->funcs != &tgx_vram_object_funcs) {
		ret = -EINVAL;
		goto out;
	}
	source = container_of(source_gem, struct tarugx_vram_object, base);
	destination = container_of(destination_gem,
				   struct tarugx_vram_object, base);
	if (args->source_x >= source->width ||
	    args->source_y >= source->height ||
	    args->destination_x >= destination->width ||
	    args->destination_y >= destination->height ||
	    args->width > source->width - args->source_x ||
	    args->height > source->height - args->source_y ||
	    args->width > destination->width - args->destination_x ||
	    args->height > destination->height - args->destination_y) {
		ret = -EINVAL;
		goto out;
	}
	command.src_x = args->source_x;
	command.src_y = args->source_y;
	command.dst_x = args->destination_x;
	command.dst_y = args->destination_y;
	command.width = args->width;
	command.height = args->height;
	waited = dma_resv_wait_timeout(source_gem->resv, DMA_RESV_USAGE_READ,
				       true,
				       msecs_to_jiffies(TGX_RENDER_TIMEOUT_MS));
	if (waited <= 0) {
		ret = waited ? (int)waited : -ETIMEDOUT;
		goto out;
	}
	waited = dma_resv_wait_timeout(destination_gem->resv,
				       DMA_RESV_USAGE_WRITE, true,
				       msecs_to_jiffies(TGX_RENDER_TIMEOUT_MS));
	if (waited <= 0) {
		ret = waited ? (int)waited : -ETIMEDOUT;
		goto out;
	}
	if (!tgx->render_available) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (!drm_dev_enter(drm, &idx)) {
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&tgx->render_submit_lock);
	mutex_lock(&tgx->render_lock);
	if (tgx->render_async_valid) {
		ret = tgx_reap_async(tgx, TGX_RENDER_TIMEOUT_MS);
		if (ret)
			goto out_unlock_render;
	}
	ret = tgx_execute_commands(tgx, &command, 1,
				   source->node.start, source->pitch,
				   destination->node.start, destination->pitch,
				   destination->width, destination->height,
				   NULL, &fence);
out_unlock_render:
	mutex_unlock(&tgx->render_lock);
	mutex_unlock(&tgx->render_submit_lock);
	drm_dev_exit(idx);
out:
	drm_gem_object_put(destination_gem);
	drm_gem_object_put(source_gem);
	return ret;
}

static int tgx_ioctl_get_param(struct drm_device *drm, void *data,
			       struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_get_param *args = data;

	(void)file;
	if (args->pad)
		return -EINVAL;

	switch (args->param) {
	case DRM_TARUGX_PARAM_GPU_VERSION:
		args->value = tgx->gpu_version;
		return 0;
	case DRM_TARUGX_PARAM_CAPS:
		args->value = 0;
		if (tgx->triangle_available)
			args->value |= DRM_TARUGX_CAP_TRIANGLE;
		if (tgx->triangle_interpolation_available)
			args->value |= DRM_TARUGX_CAP_TRIANGLE_INTERPOLATION;
		if (tgx->depth_available)
			args->value |= DRM_TARUGX_CAP_Z16;
		return 0;
	default:
		return -EINVAL;
	}
}

static void tgx_cpu_copy_frame(struct tarugx *tgx, u32 source, u32 target)
{
	u32 offset;

	for (offset = 0; offset < tgx->frame_bytes; offset += 4)
		writel(readl(tgx->vram + source + offset),
		       tgx->vram + target + offset);
	readl(tgx->vram + target + tgx->frame_bytes - 4);
}

static void tgx_record_update_profile(struct tarugx *tgx, u64 started_ns,
				      u64 conversion_ns, u64 dma_ns,
				      u64 vblank_wait_ns)
{
	atomic64_set(&tgx->last_update_ns, ktime_get_ns() - started_ns);
	atomic64_set(&tgx->last_conversion_ns, conversion_ns);
	atomic64_set(&tgx->last_dma_ns, dma_ns);
	atomic64_set(&tgx->last_vblank_wait_ns, vblank_wait_ns);
}

static ssize_t page_flips_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->page_flips));
}
static DEVICE_ATTR_RO(page_flips);

static ssize_t flip_timeouts_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->flip_timeouts));
}
static DEVICE_ATTR_RO(flip_timeouts);

static ssize_t flip_fallbacks_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->flip_fallbacks));
}
static DEVICE_ATTR_RO(flip_fallbacks);

static ssize_t vblank_timeouts_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->vblank_timeouts));
}
static DEVICE_ATTR_RO(vblank_timeouts);

static ssize_t bytes_uploaded_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->bytes_uploaded));
}
static DEVICE_ATTR_RO(bytes_uploaded);

static ssize_t msi_interrupts_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->msi_interrupts));
}
static DEVICE_ATTR_RO(msi_interrupts);

static ssize_t blit_syncs_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->blit_syncs));
}
static DEVICE_ATTR_RO(blit_syncs);

static ssize_t blit_sync_failures_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n",
			  atomic64_read(&tgx->blit_sync_failures));
}
static DEVICE_ATTR_RO(blit_sync_failures);

static ssize_t dma_uploads_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->dma_uploads));
}
static DEVICE_ATTR_RO(dma_uploads);

static ssize_t dma_upload_failures_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n",
			  atomic64_read(&tgx->dma_upload_failures));
}
static DEVICE_ATTR_RO(dma_upload_failures);

static ssize_t partial_dma_profile_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "uploads=%lld bytes=%lld\n",
			  atomic64_read(&tgx->partial_dma_uploads),
			  atomic64_read(&tgx->partial_dma_bytes));
}
static DEVICE_ATTR_RO(partial_dma_profile);

static ssize_t dma_profile_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);
	u32 busy, tx_stall, rx_stall, memory_stall, slot_full;
	u32 completion_busy;

	busy = readl(tgx->regs + TGX_REG_DMA_BUSY_CYCLES);
	tx_stall = readl(tgx->regs + TGX_REG_DMA_TX_STALL_CYCLES);
	rx_stall = readl(tgx->regs + TGX_REG_DMA_RX_STALL_CYCLES);
	memory_stall = readl(tgx->regs + TGX_REG_DMA_MEMORY_STALL_CYCLES);
	slot_full = readl(tgx->regs + TGX_REG_DMA_SLOT_FULL_CYCLES);
	completion_busy = readl(tgx->regs +
				TGX_REG_DMA_COMPLETION_BUSY_CYCLES);

	return sysfs_emit(buf,
			  "busy_cycles=%u tx_stall_cycles=%u rx_stall_cycles=%u memory_stall_cycles=%u slot_full_cycles=%u completion_busy_cycles=%u\n",
			  busy, tx_stall, rx_stall, memory_stall, slot_full,
			  completion_busy);
}
static DEVICE_ATTR_RO(dma_profile);

static ssize_t dma_error_code_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n",
			  readl(tgx->regs + TGX_REG_DMA_ERROR_CODE) & 0xf);
}
static DEVICE_ATTR_RO(dma_error_code);

static ssize_t render_commands_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->render_commands));
}
static DEVICE_ATTR_RO(render_commands);

static ssize_t render_failures_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->render_failures));
}
static DEVICE_ATTR_RO(render_failures);

static ssize_t render_queue_peak_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n",
			  atomic64_read(&tgx->render_queue_peak));
}
static DEVICE_ATTR_RO(render_queue_peak);

static ssize_t render_sched_peak_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n",
			  atomic64_read(&tgx->render_sched_peak));
}
static DEVICE_ATTR_RO(render_sched_peak);

static ssize_t direct_scanouts_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", atomic64_read(&tgx->direct_scanouts));
}
static DEVICE_ATTR_RO(direct_scanouts);

static ssize_t physical_pages_synced_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", tgx->physical_pages_synced);
}
static DEVICE_ATTR_RO(physical_pages_synced);

static ssize_t update_profile_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf,
		"update_ns=%lld conversion_ns=%lld dma_ns=%lld vblank_wait_ns=%lld\n",
		atomic64_read(&tgx->last_update_ns),
		atomic64_read(&tgx->last_conversion_ns),
		atomic64_read(&tgx->last_dma_ns),
		atomic64_read(&tgx->last_vblank_wait_ns));
}
static DEVICE_ATTR_RO(update_profile);

static ssize_t conversion_profile_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);
	s64 host_count = atomic64_read(&tgx->host_conversion_count);
	s64 prime_count = atomic64_read(&tgx->prime_conversion_count);
	s64 host_ns = atomic64_read(&tgx->host_conversion_ns);
	s64 prime_ns = atomic64_read(&tgx->prime_conversion_ns);

	return sysfs_emit(buf,
		"host_count=%lld host_total_ns=%lld host_average_ns=%lld prime_count=%lld prime_total_ns=%lld prime_average_ns=%lld\n",
		host_count, host_ns, host_count ? div64_s64(host_ns, host_count) : 0,
		prime_count, prime_ns,
		prime_count ? div64_s64(prime_ns, prime_count) : 0);
}
static DEVICE_ATTR_RO(conversion_profile);

static ssize_t prime_profile_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tarugx *tgx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "imports=%lld segments=%lld peak_segments=%lld\n",
		atomic64_read(&tgx->prime_import_count),
		atomic64_read(&tgx->prime_sg_segments),
		atomic64_read(&tgx->prime_sg_segments_peak));
}
static DEVICE_ATTR_RO(prime_profile);

static ssize_t dma_test_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct tarugx *tgx = dev_get_drvdata(dev);
	dma_addr_t dma_addr;
	u32 *source;
	u32 length;
	unsigned int i;
	u64 started_ns, elapsed_ns;
	int ret;

	ret = kstrtou32(buf, 0, &length);
	if (ret)
		return ret;
	if (length == 1)
		length = TGX_DMA_TEST_BYTES;
	/* The diagnostic window follows two legacy scanout pages.  A 1080p
	 * page covers that address, so writing there would visibly corrupt the
	 * active framebuffer.  Refuse the test unless both scanout pages end at
	 * or before the fixed BAR2-readable diagnostic window. */
	if (TGX_DMA_TEST_VRAM < 2 * tgx->frame_bytes)
		return -EOPNOTSUPP;
	if (!length || length > tgx->frame_bytes ||
	    length > TGX_BAR2_BYTES - TGX_DMA_TEST_VRAM || (length & 3))
		return -EINVAL;
	if (!mutex_trylock(&tgx->dma_lock))
		return -EBUSY;
	source = dma_alloc_coherent(&tgx->pdev->dev, length,
				    &dma_addr, GFP_KERNEL);
	if (!source) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	for (i = 0; i < length / sizeof(*source); i++)
		source[i] = 0x54470000 | i;
	dma_wmb();
	started_ns = ktime_get_ns();
	ret = tgx_dma_transfer(tgx, dma_addr, TGX_DMA_TEST_VRAM, length,
			       2000000);
	elapsed_ns = ktime_get_ns() - started_ns;
	if (ret)
		goto out_free;
	for (i = 0; i < length / sizeof(*source); i++) {
		if (readl(tgx->vram + TGX_DMA_TEST_VRAM + i * sizeof(*source)) !=
		    source[i]) {
			ret = -EIO;
			goto out_free;
		}
	}
	dev_info(dev, "DMA self-test passed: %u bytes in %llu us at %pad\n",
		 length, div_u64(elapsed_ns, 1000), &dma_addr);
	ret = count;

out_free:
	dma_free_coherent(&tgx->pdev->dev, length, source, dma_addr);
out_unlock:
	mutex_unlock(&tgx->dma_lock);
	return ret;
}
static DEVICE_ATTR_WO(dma_test);

static struct attribute *tgx_attrs[] = {
	&dev_attr_page_flips.attr,
	&dev_attr_flip_timeouts.attr,
	&dev_attr_flip_fallbacks.attr,
	&dev_attr_vblank_timeouts.attr,
	&dev_attr_bytes_uploaded.attr,
	&dev_attr_msi_interrupts.attr,
	&dev_attr_blit_syncs.attr,
	&dev_attr_blit_sync_failures.attr,
	&dev_attr_dma_uploads.attr,
	&dev_attr_dma_upload_failures.attr,
	&dev_attr_partial_dma_profile.attr,
	&dev_attr_dma_profile.attr,
	&dev_attr_dma_error_code.attr,
	&dev_attr_render_commands.attr,
	&dev_attr_render_failures.attr,
	&dev_attr_render_queue_peak.attr,
	&dev_attr_render_sched_peak.attr,
	&dev_attr_direct_scanouts.attr,
	&dev_attr_physical_pages_synced.attr,
	&dev_attr_update_profile.attr,
	&dev_attr_conversion_profile.attr,
	&dev_attr_prime_profile.attr,
	&dev_attr_dma_test.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tgx);

static const struct drm_display_mode tgx_mode_640x480 = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		 25188, 640, 656, 752, 800, 0, 480, 490, 492, 525, 0,
		 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)
};

static const struct drm_display_mode tgx_mode_1920x1080 = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		 148438, 1920, 2008, 2052, 2200, 0,
		 1080, 1084, 1089, 1125, 0,
		 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)
};

static irqreturn_t tgx_irq(int irq, void *data)
{
	struct tarugx *tgx = data;

	atomic64_inc(&tgx->msi_interrupts);
	tgx_update_sync_fence(tgx);
	wake_up_all(&tgx->vblank_wait);
	wake_up_all(&tgx->render_wait);
	return IRQ_HANDLED;
}

static void tgx_free_irq_vectors(void *data)
{
	pci_free_irq_vectors(data);
}

static void tgx_clear_bus_master(void *data)
{
	pci_clear_master(data);
}

static void tgx_disable_msi(void *data)
{
	struct tarugx *tgx = data;

	writel(0, tgx->regs + TGX_REG_IRQ_ENABLE);
	readl(tgx->regs);
}

static int tgx_get_modes(struct drm_connector *connector)
{
	struct tarugx *tgx = container_of(connector, struct tarugx, connector);
	struct drm_display_mode *mode = drm_mode_duplicate(connector->dev,
							  tgx->mode);
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
	struct tarugx *tgx = container_of(pipe, struct tarugx, pipe);

	return drm_mode_equal(mode, tgx->mode) ? MODE_OK : MODE_BAD;
}

static int tgx_check(struct drm_simple_display_pipe *pipe,
		    struct drm_plane_state *plane, struct drm_crtc_state *crtc)
{
	struct tarugx *tgx = container_of(pipe, struct tarugx, pipe);
	struct drm_gem_object *gem;
	struct tarugx_vram_object *object;

	crtc->no_vblank = true;
	if (plane->fb && (plane->src_x || plane->src_y ||
	    plane->fb->width != tgx->width ||
	    plane->fb->height != tgx->height))
		return -EINVAL;
	if (!plane->fb)
		return 0;
	gem = drm_gem_fb_get_obj(plane->fb, 0);
	if (tgx->dma_only_scanout &&
	    (!gem || (gem->funcs != &tgx_host_object_funcs &&
		     gem->funcs != &tgx_vram_object_funcs &&
		     !gem->import_attach)))
		return -EINVAL;
	if (tgx->dma_only_scanout && gem->funcs != &tgx_vram_object_funcs &&
	    plane->fb->format->format == tgx->format &&
	    plane->fb->pitches[0] != tgx->pitch)
		return -EINVAL;
	if (!gem || gem->funcs != &tgx_vram_object_funcs)
		return 0;
	if (!tgx->active_fb_base_available || tgx->legacy_single_buffer)
		return -EOPNOTSUPP;
	object = container_of(gem, struct tarugx_vram_object, base);
	if (object->width != tgx->width || object->height != tgx->height ||
	    object->pitch != tgx->pitch || plane->fb->pitches[0] != tgx->pitch ||
	    plane->fb->offsets[0] || plane->fb->format->format != tgx->format)
		return -EINVAL;
	return 0;
}

static void tgx_host_object_free(struct drm_gem_object *gem)
{
	struct tarugx_host_object *object =
		container_of(gem, struct tarugx_host_object, base);

	if (object->vaddr)
		dma_free_coherent(gem->dev->dev, gem->size, object->vaddr,
				  object->dma_addr);
	drm_gem_object_release(gem);
	kfree(object);
}

static int tgx_host_object_vmap(struct drm_gem_object *gem,
				struct iosys_map *map)
{
	struct tarugx_host_object *object =
		container_of(gem, struct tarugx_host_object, base);

	iosys_map_set_vaddr(map, object->vaddr);
	return 0;
}

static void tgx_host_object_vunmap(struct drm_gem_object *gem,
				   struct iosys_map *map)
{
	(void)gem;
	iosys_map_clear(map);
}

static struct sg_table *
tgx_host_object_get_sg_table(struct drm_gem_object *gem)
{
	struct tarugx_host_object *object =
		container_of(gem, struct tarugx_host_object, base);
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);
	ret = dma_get_sgtable(gem->dev->dev, sgt, object->vaddr,
			      object->dma_addr, gem->size);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(ret);
	}
	return sgt;
}

static int tgx_host_object_mmap(struct drm_gem_object *gem,
				struct vm_area_struct *vma)
{
	struct tarugx_host_object *object =
		container_of(gem, struct tarugx_host_object, base);
	int ret;

	/* drm_gem_mmap() uses vm_pgoff as a fake GEM-object offset.  DMA mmap
	 * expects an offset within this object, so translate it back first. */
	vma->vm_pgoff -= drm_vma_node_start(&gem->vma_node);
	vm_flags_mod(vma, VM_DONTDUMP | VM_DONTEXPAND, VM_PFNMAP);
	ret = dma_mmap_coherent(gem->dev->dev, vma, object->vaddr,
				object->dma_addr, vma->vm_end - vma->vm_start);
	if (ret)
		drm_gem_vm_close(vma);
	return ret;
}

static const struct drm_gem_object_funcs tgx_host_object_funcs = {
	.free = tgx_host_object_free,
	.export = drm_gem_prime_export,
	.get_sg_table = tgx_host_object_get_sg_table,
	.vmap = tgx_host_object_vmap,
	.vunmap = tgx_host_object_vunmap,
	.mmap = tgx_host_object_mmap,
	.vm_ops = &tgx_vram_vm_ops,
};

static void tgx_prime_object_free(struct drm_gem_object *gem)
{
	struct tarugx_prime_object *object =
		container_of(gem, struct tarugx_prime_object, base);

	if (iosys_map_is_set(&object->map))
		dma_buf_vunmap_unlocked(gem->import_attach->dmabuf, &object->map);
	drm_prime_gem_destroy(gem, object->sgt);
	drm_gem_object_release(gem);
	kfree(object);
}

static int tgx_prime_object_vmap(struct drm_gem_object *gem,
				 struct iosys_map *map)
{
	struct tarugx_prime_object *object =
		container_of(gem, struct tarugx_prime_object, base);

	*map = object->map;
	return 0;
}

static void tgx_prime_object_vunmap(struct drm_gem_object *gem,
				    struct iosys_map *map)
{
	(void)gem;
	iosys_map_clear(map);
}

static const struct drm_gem_object_funcs tgx_prime_object_funcs = {
	.free = tgx_prime_object_free,
	.vmap = tgx_prime_object_vmap,
	.vunmap = tgx_prime_object_vunmap,
};

static int tgx_dumb_create(struct drm_file *file, struct drm_device *drm,
			   struct drm_mode_create_dumb *args)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct tarugx_host_object *object;
	u32 max_width, max_height;
	u64 pitch, size, max_size;
	int ret;

	if (!tgx->dma_only_scanout)
		return drm_gem_shmem_dumb_create(file, drm, args);
	/* GBM may pad scanout allocations beyond the visible dimensions (Mesa's
	 * kms_swrast pads 1080 lines to 1088).  The framebuffer and atomic checks
	 * still require the visible mode, while the backing GEM may include this
	 * bounded alignment padding. */
	max_width = ALIGN(tgx->width, 64);
	max_height = ALIGN(tgx->height, 64);
	if (!args->width || args->width > max_width || !args->height ||
	    args->height > max_height ||
	    (args->bpp != tgx->cpp * 8 &&
	     !(tgx->format == DRM_FORMAT_RGB565 && args->bpp == 32)))
		return -EINVAL;
	pitch = DIV_ROUND_UP((u64)args->width * args->bpp, 8);
	if (check_mul_overflow(pitch, (u64)args->height, &size))
		return -EOVERFLOW;
	size = PAGE_ALIGN(size);
	max_size = PAGE_ALIGN((u64)max_width * max_height *
			      max_t(u32, tgx->cpp, args->bpp / 8));
	if (!pitch || pitch > U32_MAX || !size ||
	    size > max_size)
		return -EINVAL;
	object = kzalloc(sizeof(*object), GFP_KERNEL);
	if (!object)
		return -ENOMEM;
	object->base.funcs = &tgx_host_object_funcs;
	object->width = args->width;
	object->height = args->height;
	object->pitch = pitch;
	object->cpp = args->bpp / 8;
	ret = drm_gem_object_init(drm, &object->base, size);
	if (ret) {
		kfree(object);
		return ret;
	}
	object->vaddr = dma_alloc_coherent(drm->dev, size,
					   &object->dma_addr, GFP_KERNEL);
	if (!object->vaddr) {
		ret = -ENOMEM;
		goto out_put;
	}
	ret = drm_gem_handle_create(file, &object->base, &args->handle);
	if (ret)
		goto out_put;
	args->pitch = pitch;
	args->size = size;
out_put:
	drm_gem_object_put(&object->base);
	return ret;
}

static u32 tgx_pack_xrgb8888_pair(u64 pixels)
{
	u64 converted = ((pixels >> 8) & 0x0000f8000000f800ULL) |
			((pixels >> 5) & 0x000007e0000007e0ULL) |
			((pixels >> 3) & 0x0000001f0000001fULL);

	return (u32)converted | (u32)(converted >> 16);
}

static void tgx_xrgb8888_to_rgb565(struct tarugx *tgx,
				    const void *source_vaddr,
				    u32 source_pitch, u32 first_y, u32 last_y)
{
	u32 x, y;

	for (y = first_y; y < last_y; y++) {
		const __le32 *source = source_vaddr + y * source_pitch;
		__le16 *destination = tgx->dma_staging + y * tgx->pitch;

		/* dma_alloc_coherent() aligns the source, and both supported
		 * scanout pitches are multiples of eight.  Convert two independent
		 * pairs of 32-bit pixels, then store four RGB565 results at once. */
		for (x = 0; x + 3 < tgx->width; x += 4) {
			u64 first = le64_to_cpu(*(const __le64 *)&source[x]);
			u64 second = le64_to_cpu(*(const __le64 *)&source[x + 2]);
			u64 packed = tgx_pack_xrgb8888_pair(first) |
				(u64)tgx_pack_xrgb8888_pair(second) << 32;

			*(__le64 *)&destination[x] = cpu_to_le64(packed);
		}
		if (x + 1 < tgx->width) {
			u64 pixels = le64_to_cpu(*(const __le64 *)&source[x]);

			*(__le32 *)&destination[x] =
				cpu_to_le32(tgx_pack_xrgb8888_pair(pixels));
			x += 2;
		}
		if (x < tgx->width) {
			u32 pixel = le32_to_cpu(source[x]);
			u16 rgb565 = ((pixel >> 19) & 0x1f) << 11 |
				     ((pixel >> 10) & 0x3f) << 5 |
				     ((pixel >> 3) & 0x1f);

			destination[x] = cpu_to_le16(rgb565);
		}
	}
}

static int tgx_xrgb8888_dma_pipeline(struct tarugx *tgx,
				      const void *source_vaddr,
				      u32 source_pitch, u32 target,
				      u32 first_y, u32 last_y,
				      u64 *conversion_ns, u64 *dma_ns)
{
	u32 current_y = first_y;
	u32 current_last = min(first_y + TGX_CONVERSION_DMA_ROWS, last_y);
	u64 started_ns;
	int ret;

	started_ns = ktime_get_ns();
	tgx_xrgb8888_to_rgb565(tgx, source_vaddr, source_pitch,
				       current_y, current_last);
	*conversion_ns += ktime_get_ns() - started_ns;

	while (current_y < last_y) {
		u32 next_y = current_last;
		u32 next_last = min(next_y + TGX_CONVERSION_DMA_ROWS, last_y);
		u32 offset = current_y * tgx->pitch;
		u32 length = (current_last - current_y) * tgx->pitch;
		u64 dma_started_ns;

		dma_wmb();
		dma_started_ns = ktime_get_ns();
		ret = tgx_dma_start(tgx, tgx->dma_staging_addr + offset,
				    target + offset, length);
		if (ret)
			return ret;

		/* The next rows occupy a separate part of the coherent staging
		 * buffer, so the CPU can safely fill them while the FPGA reads the
		 * current rows. */
		if (next_y < last_y) {
			started_ns = ktime_get_ns();
			tgx_xrgb8888_to_rgb565(tgx, source_vaddr, source_pitch,
					       next_y, next_last);
			*conversion_ns += ktime_get_ns() - started_ns;
		}

		ret = tgx_dma_wait(tgx, TGX_1080P_DMA_TIMEOUT_US);
		*dma_ns += ktime_get_ns() - dma_started_ns;
		if (ret)
			return ret;
		current_y = next_y;
		current_last = next_last;
	}

	return 0;
}

static void tgx_update(struct drm_simple_display_pipe *pipe,
		       struct drm_plane_state *old)
{
	struct tarugx *tgx = container_of(pipe, struct tarugx, pipe);
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow = to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_object *gem;
	struct tarugx_host_object *host_object = NULL;
	struct tarugx_prime_object *imported_object = NULL;
	struct tarugx_vram_object *vram_object = NULL;
	struct drm_rect damage, copy;
	unsigned int x, y;
	u64 copied;
	u32 active, active_base, target, previous_base;
	u32 dma_offset = 0, dma_length = 0;
	s64 vblank_before;
	long wait_ret;
	int idx, target_index, ret = 0, vblank_ret = 0, mirror_ret;
	u64 pixels;
	u32 pixel;
	bool direct_scanout = false, dma_source_available = false, full_damage;
	bool did_flip_fallback = false;
	bool mirror_damage = false;
	bool convert_xrgb8888;
	dma_addr_t source_dma_addr = 0;
	const void *source_vaddr = NULL;
	u64 update_started_ns, stage_started_ns;
	u64 conversion_ns = 0, dma_ns = 0, vblank_wait_ns = 0;
	if (!fb || !pipe->crtc.state->active)
		return;
	update_started_ns = ktime_get_ns();
	gem = drm_gem_fb_get_obj(fb, 0);
	if (gem && gem->funcs == &tgx_vram_object_funcs)
		vram_object = container_of(gem, struct tarugx_vram_object, base);
	else if (gem && gem->funcs == &tgx_host_object_funcs)
		host_object = container_of(gem, struct tarugx_host_object, base);
	else if (gem && gem->funcs == &tgx_prime_object_funcs)
		imported_object = container_of(gem, struct tarugx_prime_object,
					       base);
	if (host_object) {
		source_dma_addr = host_object->dma_addr;
		source_vaddr = host_object->vaddr;
		dma_source_available = true;
	} else if (imported_object) {
		source_vaddr = imported_object->map.vaddr;
		dma_source_available = true;
	}
	convert_xrgb8888 = tgx->format == DRM_FORMAT_RGB565 &&
		fb->format->format == DRM_FORMAT_XRGB8888;
	/* A new FB must replace the single physical scanout buffer in full. */
	if (old->fb != fb || drm_atomic_crtc_needs_modeset(pipe->crtc.state))
		damage = DRM_RECT_INIT(0, 0, tgx->width, tgx->height);
	else if (!drm_atomic_helper_damage_merged(old, state, &damage))
		return;
	if (!drm_dev_enter(&tgx->drm, &idx))
		return;
	mutex_lock(&tgx->render_lock);
	if (tgx->render_async_valid) {
		u32 pending_fence = tgx->render_async_fence;
		u32 timeout_ms = TGX_RENDER_TIMEOUT_MS;

		/* prepare_fb waits the reservation WRITE fence for this exact GEM.
		 * Reap non-blockingly here: a pending result means implicit sync did
		 * not complete and the new scanout must not be exposed. */
		if (vram_object && tgx->render_async_gem == gem)
			timeout_ms = 0;

		ret = tgx_reap_async(tgx, timeout_ms);
		if (!tgx->render_async_owner && tgx->render_async_completed)
			tgx_clear_async(tgx);
		if (ret) {
			dev_warn_ratelimited(tgx->drm.dev,
				"KMS update deferred by render fence %u: %d\n",
				pending_fence, ret);
			mutex_unlock(&tgx->render_lock);
			drm_dev_exit(idx);
			return;
		}
	}
	/* A partial update starts from an exact copy of the active physical page.
	 * Never repair the inactive page from unannounced CPU-shadow pixels: only
	 * the damage rectangle is guaranteed to contain new valid contents. */
	active_base = tgx_active_fb_base(tgx);
	previous_base = active_base;
	if (active_base > tgx->vram_bytes - tgx->frame_bytes) {
		dev_warn_ratelimited(tgx->drm.dev,
				     "invalid active framebuffer base %#x\n",
				     active_base);
		mutex_unlock(&tgx->render_lock);
		drm_dev_exit(idx);
		return;
	}
	active = active_base == tgx_fb_base(tgx, 0) ? 0 :
		 active_base == tgx_fb_base(tgx, 1) ? 1 : U32_MAX;
	if (vram_object) {
		direct_scanout = true;
		target = vram_object->node.start;
		target_index = -1;
		copied = 0;
		/* Only BAR-visible objects can have userspace write-combine stores.
		 * Objects above BAR2 are produced by fenced FPGA render commands. */
		if (tgx->frame_bytes <= TGX_BAR2_BYTES &&
		    target <= TGX_BAR2_BYTES - tgx->frame_bytes)
			readl(tgx->vram + target + tgx->frame_bytes - 4);
		goto upload_complete;
	}
	target_index = tgx->legacy_single_buffer && active <= 1 ?
		active : (active_base == tgx_fb_base(tgx, 0) ? 1 : 0);
	target = tgx_fb_base(tgx, target_index);
	copy = damage;
	full_damage = copy.x1 == 0 && copy.y1 == 0 &&
		copy.x2 == tgx->width && copy.y2 == tgx->height;
	if (!full_damage && !tgx->legacy_single_buffer &&
	    !tgx->dma_only_scanout) {
		ret = tgx_blit_copy_frame(tgx, active_base, target);
		if (ret) {
			atomic64_inc(&tgx->blit_sync_failures);
			dev_warn_ratelimited(tgx->drm.dev,
					     "frame sync blit failed: %d; using CPU copy\n",
					     ret);
			tgx_cpu_copy_frame(tgx, active_base, target);
		} else {
			atomic64_inc(&tgx->blit_syncs);
		}
		ret = 0;
	}
	copied = (u64)(copy.x2 - copy.x1) * (copy.y2 - copy.y1) * tgx->cpp;
	if ((full_damage || tgx->dma_only_scanout) &&
	    (dma_upload || tgx->dma_only_scanout) &&
	    tgx->dma_available) {
		if (tgx->dma_only_scanout) {
			if (!dma_source_available || !source_vaddr) {
				dev_err_ratelimited(tgx->drm.dev,
					"1080p framebuffer is not DMA-backed\n");
				mutex_unlock(&tgx->render_lock);
				drm_dev_exit(idx);
				return;
			}
			mirror_damage = !full_damage;
			if (full_damage || !tgx->physical_pages_synced) {
				dma_offset = 0;
				dma_length = tgx->frame_bytes;
			} else {
				dma_offset = copy.y1 * tgx->pitch;
				dma_length = (copy.y2 - copy.y1) * tgx->pitch;
			}
		} else {
			dma_length = tgx->frame_bytes;
		}
		if (!dma_source_available)
			for (y = 0; y < tgx->height; y++)
				iosys_map_memcpy_from(
					tgx->dma_staging + y * tgx->pitch,
					&shadow->data[0], y * fb->pitches[0],
					tgx->pitch);
		mutex_lock(&tgx->dma_lock);
		if (convert_xrgb8888) {
			u64 conversion_before = conversion_ns;
			u64 elapsed_ns;

			ret = tgx_xrgb8888_dma_pipeline(tgx, source_vaddr,
					fb->pitches[0], target,
					dma_offset / tgx->pitch,
					(dma_offset + dma_length) / tgx->pitch,
					&conversion_ns, &dma_ns);
			elapsed_ns = conversion_ns - conversion_before;
			if (imported_object) {
				atomic64_add(elapsed_ns, &tgx->prime_conversion_ns);
				atomic64_inc(&tgx->prime_conversion_count);
			} else {
				atomic64_add(elapsed_ns, &tgx->host_conversion_ns);
				atomic64_inc(&tgx->host_conversion_count);
			}
		} else {
			dma_wmb();
			stage_started_ns = ktime_get_ns();
			if (imported_object)
				ret = tgx_dma_transfer_sg(tgx, imported_object->sgt,
						dma_offset, target + dma_offset,
						dma_length,
						tgx->dma_only_scanout ?
						TGX_1080P_DMA_TIMEOUT_US :
						TGX_DMA_TIMEOUT_US);
			else
				ret = tgx_dma_transfer(tgx,
					       (dma_source_available ? source_dma_addr :
					       tgx->dma_staging_addr) + dma_offset,
					       target + dma_offset, dma_length,
					       tgx->dma_only_scanout ?
					       TGX_1080P_DMA_TIMEOUT_US :
					       TGX_DMA_TIMEOUT_US);
			dma_ns += ktime_get_ns() - stage_started_ns;
		}
		mutex_unlock(&tgx->dma_lock);
		if (!ret) {
			atomic64_inc(&tgx->dma_uploads);
			if (tgx->dma_only_scanout && full_damage)
				tgx->physical_pages_synced = false;
			goto upload_complete;
		}
		atomic64_inc(&tgx->dma_upload_failures);
		if (tgx->dma_only_scanout) {
			dev_err_ratelimited(tgx->drm.dev,
				"frame DMA upload failed: %d; no safe BAR fallback\n",
				ret);
			mutex_unlock(&tgx->render_lock);
			drm_dev_exit(idx);
			return;
		}
		dev_warn_ratelimited(tgx->drm.dev,
				     "frame DMA upload failed: %d; using BAR writes\n",
				     ret);
		ret = 0;
	}
	/* The FPGA accepts one- and two-DWORD writes.  Pair adjacent pixels with
	 * writeq(), while retaining a DWORD tail for odd-width damage rectangles. */
	for (y = copy.y1; y < copy.y2; y++) {
		x = copy.x1;
		if (x & 1) {
			iosys_map_memcpy_from(&pixel, &shadow->data[0],
					     y * fb->pitches[0] + x * 4, 4);
			writel(le32_to_cpu((__force __le32)pixel),
			       tgx->vram + target + y * tgx->pitch + x * 4);
			x++;
		}
		for (; x + 1 < copy.x2; x += 2) {
			iosys_map_memcpy_from(&pixels, &shadow->data[0],
					     y * fb->pitches[0] + x * 4, 8);
			writeq(le64_to_cpu((__force __le64)pixels),
			       tgx->vram + target + y * tgx->pitch + x * 4);
		}
		if (x < copy.x2) {
			iosys_map_memcpy_from(&pixel, &shadow->data[0],
					     y * fb->pitches[0] + x * 4, 4);
			writel(le32_to_cpu((__force __le32)pixel),
			       tgx->vram + target + y * tgx->pitch + x * 4);
		}
	}
	/* BAR2 read orders the preceding posted writes through the DDR path. */
	readl(tgx->vram + target + tgx->frame_bytes - 4);
upload_complete:
	if (tgx->legacy_single_buffer) {
		/* Legacy hardware scans a fixed page.  Updating only the damage
		 * rectangle in place is slower than a flip but avoids repeatedly
		 * copying an entire frame while scanout is active. */
		atomic64_inc(&tgx->flip_fallbacks);
		atomic64_add(copied, &tgx->bytes_uploaded);
		tgx_record_update_profile(tgx, update_started_ns, conversion_ns,
					  dma_ns, vblank_wait_ns);
		mutex_unlock(&tgx->render_lock);
		drm_dev_exit(idx);
		return;
	}
	vblank_before = atomic64_read(&tgx->msi_interrupts);
	writel(target, tgx->regs + TGX_REG_FB_BASE);
	writel(tgx->pitch, tgx->regs + TGX_REG_FB_STRIDE);
	writel(1, tgx->regs + TGX_REG_FB_COMMIT);
	readl(tgx->regs);
	writel(1, tgx->regs + TGX_REG_DISPLAY_CTRL);
	readl(tgx->regs);
	/* The same VBlank both raises MSI and applies FB_COMMIT.  Wait for those
	 * two facts together instead of polling the flip and then waiting for an
	 * extra frame.  A pre-commit interrupt cannot satisfy both conditions. */
	stage_started_ns = ktime_get_ns();
	wait_ret = wait_event_timeout(tgx->vblank_wait,
			READ_ONCE(tgx->removing) ||
			(atomic64_read(&tgx->msi_interrupts) != vblank_before &&
			 tgx_active_fb_base(tgx) == target),
			msecs_to_jiffies(100));
	vblank_wait_ns += ktime_get_ns() - stage_started_ns;
	if (READ_ONCE(tgx->removing)) {
		mutex_unlock(&tgx->render_lock);
		drm_dev_exit(idx);
		return;
	}
	if (!wait_ret) {
		active_base = tgx_active_fb_base(tgx);
		active = active_base == tgx_fb_base(tgx, 0) ? 0 :
			 active_base == tgx_fb_base(tgx, 1) ? 1 : U32_MAX;
		if (active_base == target)
			vblank_ret = 1;
		else {
			ret = -ETIMEDOUT;
			/* Older Flash images do not implement the current commit
			 * handshake.  Preserve a usable display after a power cycle by
			 * copying the completed inactive page onto the page that legacy
			 * hardware keeps scanning out. */
			if (active <= 1 && !tgx->dma_only_scanout) {
				tgx_cpu_copy_frame(tgx, target,
						   tgx_fb_base(tgx, active));
				atomic64_inc(&tgx->flip_fallbacks);
				tgx->legacy_single_buffer = true;
				did_flip_fallback = true;
			}
		}
	}
	if (!ret && mirror_damage) {
		u32 mirror_offset = copy.y1 * tgx->pitch;
		u32 mirror_length = (copy.y2 - copy.y1) * tgx->pitch;

		dma_wmb();
		mutex_lock(&tgx->dma_lock);
		stage_started_ns = ktime_get_ns();
		if (imported_object && !convert_xrgb8888)
			mirror_ret = tgx_dma_transfer_sg(tgx,
					imported_object->sgt, mirror_offset,
					previous_base + mirror_offset,
					mirror_length, TGX_1080P_DMA_TIMEOUT_US);
		else
			mirror_ret = tgx_dma_transfer(tgx,
					(convert_xrgb8888 ?
					 tgx->dma_staging_addr : source_dma_addr) +
					 mirror_offset,
					previous_base + mirror_offset, mirror_length,
					TGX_1080P_DMA_TIMEOUT_US);
		dma_ns += ktime_get_ns() - stage_started_ns;
		mutex_unlock(&tgx->dma_lock);
		if (mirror_ret) {
			tgx->physical_pages_synced = false;
			atomic64_inc(&tgx->dma_upload_failures);
			dev_warn_ratelimited(tgx->drm.dev,
				"damage mirror DMA failed: %d\n", mirror_ret);
		} else {
			tgx->physical_pages_synced = true;
			atomic64_inc(&tgx->dma_uploads);
		}
	}
	if (ret)
		atomic64_inc(&tgx->flip_timeouts);
	else if (vblank_ret)
		atomic64_inc(&tgx->vblank_timeouts);
	else
		atomic64_inc(&tgx->page_flips);
	if (!ret && direct_scanout)
		atomic64_inc(&tgx->direct_scanouts);
	atomic64_add(copied, &tgx->bytes_uploaded);
	if (ret)
		dev_warn_ratelimited(tgx->drm.dev,
				     "page flip to base %#x timed out (active base %#x)%s\n",
				     target, active_base,
				     did_flip_fallback ? "; copied to active page" : "");
	else if (vblank_ret)
		dev_warn_ratelimited(tgx->drm.dev,
				     "VBlank wait timed out after flip to base %#x\n",
				     target);
	tgx_record_update_profile(tgx, update_started_ns, conversion_ns, dma_ns,
				  vblank_wait_ns);
	mutex_unlock(&tgx->render_lock);
	drm_dev_exit(idx);
}

static void tgx_disable(struct drm_simple_display_pipe *pipe)
{
	struct tarugx *tgx = container_of(pipe, struct tarugx, pipe);
	int idx;
	if (!drm_dev_enter(&tgx->drm, &idx))
		return;
	writel(0, tgx->regs + TGX_REG_DISPLAY_CTRL);
	readl(tgx->regs);
	drm_dev_exit(idx);
}

static int tgx_begin_fb_access(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *state)
{
	struct drm_gem_object *gem;

	if (!state->fb)
		return 0;
	gem = drm_gem_fb_get_obj(state->fb, 0);
	if (gem && gem->funcs == &tgx_vram_object_funcs)
		return 0;
	return drm_gem_simple_kms_begin_shadow_fb_access(pipe, state);
}

static void tgx_end_fb_access(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *state)
{
	struct drm_gem_object *gem;

	if (!state->fb)
		return;
	gem = drm_gem_fb_get_obj(state->fb, 0);
	if (gem && gem->funcs == &tgx_vram_object_funcs)
		return;
	drm_gem_simple_kms_end_shadow_fb_access(pipe, state);
}

static const struct drm_simple_display_pipe_funcs tgx_pipe_funcs = {
	.mode_valid = tgx_mode_valid,
	.check = tgx_check,
	.update = tgx_update,
	.disable = tgx_disable,
	.begin_fb_access = tgx_begin_fb_access,
	.end_fb_access = tgx_end_fb_access,
	.reset_plane = drm_gem_simple_kms_reset_shadow_plane,
	.duplicate_plane_state = drm_gem_simple_kms_duplicate_shadow_plane_state,
	.destroy_plane_state = drm_gem_simple_kms_destroy_shadow_plane_state,
};
static const struct drm_mode_config_funcs tgx_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void tgx_configure_memory_and_mode(struct tarugx *tgx,
					  u32 gpu_version)
{
	if (gpu_version >= TGX_GPU_VERSION_RGB565) {
		tgx->mode = &tgx_mode_1920x1080;
		tgx->width = TGX_1080P_WIDTH;
		tgx->height = TGX_1080P_HEIGHT;
		tgx->cpp = 2;
		tgx->format = DRM_FORMAT_RGB565;
		tgx->pitch = TGX_1080P_RGB565_PITCH;
		tgx->frame_bytes = TGX_1080P_RGB565_FRAME_BYTES;
		tgx->vram_bytes = TGX_DDR_BYTES;
		tgx->vram_gem_base = 2 * tgx->frame_bytes;
		tgx->vram_gem_bytes = tgx->vram_bytes - tgx->vram_gem_base;
		tgx->dma_only_scanout = true;
		return;
	}
	if (gpu_version >= TGX_GPU_VERSION_1080P60) {
		tgx->mode = &tgx_mode_1920x1080;
		tgx->width = TGX_1080P_WIDTH;
		tgx->height = TGX_1080P_HEIGHT;
		tgx->cpp = 4;
		tgx->format = DRM_FORMAT_XRGB8888;
		tgx->pitch = TGX_1080P_PITCH;
		tgx->frame_bytes = TGX_1080P_FRAME_BYTES;
		tgx->vram_bytes = TGX_DDR_BYTES;
		tgx->vram_gem_base = TGX_BAR2_BYTES;
		tgx->vram_gem_bytes = 0;
		tgx->dma_only_scanout = true;
		return;
	}

	tgx->mode = &tgx_mode_640x480;
	tgx->width = TGX_LEGACY_WIDTH;
	tgx->height = TGX_LEGACY_HEIGHT;
	tgx->cpp = 4;
	tgx->format = DRM_FORMAT_XRGB8888;
	tgx->pitch = TGX_LEGACY_PITCH;
	tgx->frame_bytes = TGX_LEGACY_FRAME_BYTES;
	tgx->vram_bytes = TGX_BAR2_BYTES;
	tgx->vram_gem_base = 2 * TGX_LEGACY_FRAME_BYTES;
	tgx->vram_gem_bytes = TGX_BAR2_BYTES - tgx->vram_gem_base;
}
DEFINE_DRM_GEM_FOPS(tgx_fops);

static void tgx_postclose(struct drm_device *drm, struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	int idx;

	if (!drm_dev_enter(drm, &idx))
		return;
	mutex_lock(&tgx->render_lock);
	if (tgx->render_async_valid && tgx->render_async_owner == file) {
		tgx_reap_async(tgx, TGX_RENDER_TIMEOUT_MS);
		tgx->render_async_owner = NULL;
		if (tgx->render_async_completed)
			tgx_clear_async(tgx);
	}
	mutex_unlock(&tgx->render_lock);
	drm_dev_exit(idx);
}

static const struct drm_ioctl_desc tgx_ioctls[] = {
	DRM_IOCTL_DEF_DRV(TARUGX_FILL, tgx_ioctl_fill, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(TARUGX_BLIT, tgx_ioctl_blit, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(TARUGX_SUBMIT, tgx_ioctl_submit, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(TARUGX_WAIT, tgx_ioctl_wait,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TARUGX_VRAM_CREATE, tgx_ioctl_vram_create,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TARUGX_SUBMIT_GEM, tgx_ioctl_submit_gem,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TARUGX_SUBMIT_GEM2, tgx_ioctl_submit_gem2,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TARUGX_UPLOAD_GEM, tgx_ioctl_upload_gem,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TARUGX_COPY_GEM, tgx_ioctl_copy_gem,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TARUGX_GET_PARAM, tgx_ioctl_get_param,
			  DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TARUGX_SUBMIT_GEM3, tgx_ioctl_submit_gem3,
			  DRM_AUTH | DRM_RENDER_ALLOW),
};

static struct drm_gem_object *
tgx_prime_import_sg_table(struct drm_device *drm,
			  struct dma_buf_attachment *attach,
			  struct sg_table *sgt)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct tarugx_prime_object *object;
	s64 peak;
	int ret;

	object = kzalloc(sizeof(*object), GFP_KERNEL);
	if (!object)
		return ERR_PTR(-ENOMEM);
	ret = dma_buf_vmap_unlocked(attach->dmabuf, &object->map);
	if (ret) {
		kfree(object);
		return ERR_PTR(ret);
	}
	if (object->map.is_iomem) {
		dma_buf_vunmap_unlocked(attach->dmabuf, &object->map);
		kfree(object);
		return ERR_PTR(-EOPNOTSUPP);
	}
	drm_gem_private_object_init(drm, &object->base,
				    PAGE_ALIGN(attach->dmabuf->size));
	object->base.funcs = &tgx_prime_object_funcs;
	object->sgt = sgt;
	atomic64_inc(&tgx->prime_import_count);
	atomic64_set(&tgx->prime_sg_segments, sgt->nents);
	peak = atomic64_read(&tgx->prime_sg_segments_peak);
	while (sgt->nents > peak) {
		s64 previous = atomic64_cmpxchg(&tgx->prime_sg_segments_peak,
						 peak, sgt->nents);

		if (previous == peak)
			break;
		peak = previous;
	}
	return &object->base;
}
static const struct drm_driver tgx_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC |
		DRIVER_RENDER | DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE,
	.fops = &tgx_fops,
	.postclose = tgx_postclose,
	.dumb_create = tgx_dumb_create,
	.gem_prime_import_sg_table = tgx_prime_import_sg_table,
	.ioctls = tgx_ioctls,
	.num_ioctls = ARRAY_SIZE(tgx_ioctls),
	.gem_prime_import = drm_gem_prime_import,
	.name = "tarugx",
	.desc = "TaruGX/100 CPU framebuffer",
	.date = "20261104",
	.major = 0, .minor = 18,
};

static void tgx_vram_mm_takedown(struct drm_device *drm, void *data)
{
	struct tarugx *tgx = data;

	(void)drm;
	WARN_ON(!drm_mm_clean(&tgx->vram_mm));
	drm_mm_takedown(&tgx->vram_mm);
}

static void tgx_scheduler_fini(void *data)
{
	struct tarugx *tgx = data;

	if (!tgx->render_sched_initialized)
		return;
	tgx->render_sched_initialized = false;
	while (tgx->render_entity_count) {
		tgx->render_entity_count--;
		drm_sched_entity_destroy(
			&tgx->render_entities[tgx->render_entity_count]);
	}
	drm_sched_fini(&tgx->render_sched);
}

static int tgx_scheduler_init(struct tarugx *tgx)
{
	u32 i;
	int ret;

	tgx->render_sched_list[0] = &tgx->render_sched;
	ret = drm_sched_init(&tgx->render_sched, &tgx_sched_ops, NULL,
			     DRM_SCHED_PRIORITY_COUNT, 1,
			     0, msecs_to_jiffies(TGX_RENDER_TIMEOUT_MS),
			     NULL, NULL,
			     "tarugx-render", tgx->drm.dev);
	if (ret)
		return ret;
	for (i = 0; i < TGX_RENDER_ENTITIES; i++) {
		ret = drm_sched_entity_init(&tgx->render_entities[i],
					    DRM_SCHED_PRIORITY_NORMAL,
					    tgx->render_sched_list, 1, NULL);
		if (ret)
			goto out_entities;
		tgx->render_entity_count++;
	}
	tgx->render_sched_initialized = true;
	return 0;

out_entities:
	while (tgx->render_entity_count) {
		tgx->render_entity_count--;
		drm_sched_entity_destroy(
			&tgx->render_entities[tgx->render_entity_count]);
	}
	drm_sched_fini(&tgx->render_sched);
	return ret;
}

static int tgx_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const u32 xrgb8888_formats[] = { DRM_FORMAT_XRGB8888 };
	static const u32 rgb565_formats[] = {
		DRM_FORMAT_XRGB8888,
		DRM_FORMAT_RGB565,
	};
	const u32 *formats;
	u32 format_count;
	struct tarugx *tgx;
	struct drm_device *drm;
	u32 dma_status, gpu_version, queue_status;
	int ret;
	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "no usable DMA address mask\n");
		dev_info(&pdev->dev, "using 32-bit DMA addresses\n");
	}
	pci_set_master(pdev);
	ret = devm_add_action_or_reset(&pdev->dev, tgx_clear_bus_master, pdev);
	if (ret)
		return ret;
	if (pci_resource_len(pdev, 0) != SZ_64K ||
	    pci_resource_len(pdev, 2) != TGX_BAR2_BYTES)
		return -ENODEV;
	ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), "tarugx");
	if (ret)
		return ret;
	tgx = devm_drm_dev_alloc(&pdev->dev, &tgx_driver, struct tarugx, drm);
	if (IS_ERR(tgx))
		return PTR_ERR(tgx);
	drm = &tgx->drm;
	tgx->pdev = pdev;
	mutex_init(&tgx->dma_lock);
	mutex_init(&tgx->render_lock);
	mutex_init(&tgx->render_submit_lock);
	mutex_init(&tgx->vram_lock);
	spin_lock_init(&tgx->render_fence_lock);
	tgx->render_fence_context = dma_fence_context_alloc(1);
	tgx->regs = pcim_iomap_table(pdev)[0];
	tgx->vram = pcim_iomap_table(pdev)[2];
	init_waitqueue_head(&tgx->vblank_wait);
	init_waitqueue_head(&tgx->render_wait);
	if (readl(tgx->regs) != 0x54475055)
		return -ENODEV;
	gpu_version = readl(tgx->regs + TGX_REG_GPU_VERSION);
	tgx->gpu_version = gpu_version;
	tgx_configure_memory_and_mode(tgx, gpu_version);
	formats = tgx->format == DRM_FORMAT_RGB565 ?
		rgb565_formats : xrgb8888_formats;
	format_count = tgx->format == DRM_FORMAT_RGB565 ?
		ARRAY_SIZE(rgb565_formats) : ARRAY_SIZE(xrgb8888_formats);
	tgx->active_fb_base_available =
		gpu_version >= TGX_GPU_VERSION_ACTIVE_FB_BASE;
	tgx->triangle_available = gpu_version >= TGX_GPU_VERSION_TRIANGLE;
	tgx->triangle_interpolation_available =
		gpu_version >= TGX_GPU_VERSION_TRIANGLE_INTERPOLATION;
	tgx->depth_available = gpu_version >= TGX_GPU_VERSION_Z16;
	dev_info(&pdev->dev,
		 "GPU version %u.%u, %ux%u, 32-bit active FB base %s\n",
		 gpu_version >> 16, gpu_version & 0xffff,
		 tgx->width, tgx->height,
		 tgx->active_fb_base_available ? "available" : "unavailable");
	dma_status = readl(tgx->regs + TGX_REG_DMA_STATUS);
	tgx->dma_available = (dma_status & TGX_DMA_STATUS_READY) &&
		!(dma_status & (TGX_DMA_STATUS_BUSY | TGX_DMA_STATUS_ERROR |
			 TGX_DMA_STATUS_REJECTED));
	dev_info(&pdev->dev, "full-frame DMA uploads %s\n",
		 tgx->dma_available ? "available" : "unavailable; using BAR2");
	if (tgx->dma_only_scanout && !tgx->dma_available)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "1080p scanout requires host-to-VRAM DMA\n");
	drm_mm_init(&tgx->vram_mm, tgx->vram_gem_base,
		    tgx->vram_gem_bytes);
	ret = drmm_add_action_or_reset(drm, tgx_vram_mm_takedown, tgx);
	if (ret)
		return ret;
	if (!tgx->dma_only_scanout || tgx->format == DRM_FORMAT_RGB565) {
		tgx->dma_staging = dmam_alloc_coherent(&pdev->dev,
						       tgx->frame_bytes,
						       &tgx->dma_staging_addr,
						       GFP_KERNEL);
		if (!tgx->dma_staging)
			return -ENOMEM;
	}
	queue_status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	tgx->render_available = queue_status & TGX_QUEUE_READY;
	tgx->render_fence = readl(tgx->regs + TGX_REG_QUEUE_COMPLETED);
	dev_info(&pdev->dev, "2D render queue %s\n",
		 tgx->render_available ? "available" : "unavailable");
	if (tgx->render_available) {
		ret = tgx_scheduler_init(tgx);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(&pdev->dev, tgx_scheduler_fini,
					       tgx);
		if (ret)
			return ret;
	}
	pci_set_drvdata(pdev, tgx);
	ret = devm_device_add_groups(&pdev->dev, tgx_groups);
	if (ret)
		return ret;
	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;
	drm->mode_config.funcs = &tgx_mode_config_funcs;
	drm->mode_config.min_width = drm->mode_config.max_width = tgx->width;
	drm->mode_config.min_height = drm->mode_config.max_height = tgx->height;
	ret = drm_connector_init(drm, &tgx->connector, &tgx_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;
	drm_connector_helper_add(&tgx->connector, &tgx_connector_helper);
	ret = drm_simple_display_pipe_init(drm, &tgx->pipe, &tgx_pipe_funcs,
			formats, format_count, NULL, &tgx->connector);
	if (ret)
		return ret;
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0)
		return ret;
	ret = devm_add_action_or_reset(&pdev->dev, tgx_free_irq_vectors, pdev);
	if (ret)
		return ret;
	ret = devm_request_irq(&pdev->dev, pci_irq_vector(pdev, 0), tgx_irq, 0,
			       "tarugx-vblank", tgx);
	if (ret)
		return ret;
	writel(TGX_IRQ_VBLANK |
	       (tgx->render_available ? TGX_IRQ_RENDER : 0),
	       tgx->regs + TGX_REG_IRQ_ENABLE);
	readl(tgx->regs);
	ret = devm_add_action_or_reset(&pdev->dev, tgx_disable_msi, tgx);
	if (ret)
		return ret;
	drm_plane_enable_fb_damage_clips(&tgx->pipe.plane);
	drm_mode_config_reset(drm);
	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	/* Establish the fixed scanout mode before userspace starts so the
	 * initramfs Plymouth renderer can acquire a usable DRM head. */
	drm_fbdev_generic_setup(drm, tgx->cpp * 8);
	return 0;
}

static void tgx_stop_waiters(struct tarugx *tgx)
{
	/* No waiter may rely on an IRQ or touch BAR registers after teardown
	 * begins.  Publish removal before masking MSI, then wake both classes of
	 * waiter so they return immediately instead of reaching their timeout. */
	WRITE_ONCE(tgx->removing, true);
	tgx_signal_sync_fence(tgx, -ENODEV);
	wake_up_all(&tgx->render_wait);
	wake_up_all(&tgx->vblank_wait);
	tgx_scheduler_fini(tgx);
}

static void tgx_remove(struct pci_dev *pdev)
{
	struct tarugx *tgx = pci_get_drvdata(pdev);

	tgx_stop_waiters(tgx);
	writel(0, tgx->regs + TGX_REG_IRQ_ENABLE);
	readl(tgx->regs);
	synchronize_irq(pci_irq_vector(pdev, 0));
	drm_atomic_helper_shutdown(&tgx->drm);
	drm_dev_unplug(&tgx->drm);
}
static void tgx_shutdown(struct pci_dev *pdev)
{
	struct tarugx *tgx = pci_get_drvdata(pdev);

	tgx_stop_waiters(tgx);
	writel(0, tgx->regs + TGX_REG_IRQ_ENABLE);
	readl(tgx->regs);
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
MODULE_IMPORT_NS(DMA_BUF);
