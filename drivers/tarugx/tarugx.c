// SPDX-License-Identifier: GPL-2.0-only
/* CPU shadow framebuffer with MSI VBlank and host-to-VRAM DMA. Linux 6.8. */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/iosys-map.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/sizes.h>
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
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_mm.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "tarugx_drm.h"

static bool dma_upload = true;
module_param(dma_upload, bool, 0644);
MODULE_PARM_DESC(dma_upload,
		 "use full-frame DMA uploads when supported by the FPGA (default: true)");

struct tarugx {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
	struct pci_dev *pdev;
	void __iomem *regs, *vram;
	struct mutex dma_lock;
	struct mutex render_lock;
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
	atomic64_t render_commands;
	atomic64_t render_failures;
	atomic64_t render_queue_peak;
	bool dma_available;
	bool render_available;
	bool legacy_single_buffer;
	wait_queue_head_t vblank_wait;
	wait_queue_head_t render_wait;
	u32 render_fence;
	bool render_async_valid;
	bool render_async_completed;
	struct drm_file *render_async_owner;
	u32 render_async_fence;
	u32 render_async_commands;
	int render_async_result;
};

struct tarugx_vram_object {
	struct drm_gem_object base;
	struct drm_mm_node node;
};

#define TGX_WIDTH 640
#define TGX_HEIGHT 480
#define TGX_PITCH (TGX_WIDTH * 4)
#define TGX_FRAME_BYTES (TGX_PITCH * TGX_HEIGHT)
#define TGX_FB_BASE(n) ((n) * TGX_FRAME_BYTES)
#define TGX_VRAM_BYTES SZ_4M
#define TGX_VRAM_GEM_BASE TGX_FB_BASE(2)
#define TGX_REG_DISPLAY_CTRL 0x10
#define TGX_REG_FB_BASE 0x1c
#define TGX_REG_FB_STRIDE 0x20
#define TGX_REG_FB_COMMIT 0x24
#define TGX_REG_ACTIVE_BUFFER 0x28
#define TGX_REG_VBLANK_TOGGLE 0x2c
#define TGX_REG_IRQ_ENABLE 0x30
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
#define TGX_QUEUE_LEVEL GENMASK(2, 0)
#define TGX_QUEUE_READY BIT(3)
#define TGX_QUEUE_ACTIVE BIT(4)
#define TGX_QUEUE_ERROR BIT(5)
#define TGX_RENDER_TIMEOUT_MS 100
#define TGX_REG_DMA_HOST_LO 0xc0
#define TGX_REG_DMA_HOST_HI 0xc4
#define TGX_REG_DMA_VRAM 0xc8
#define TGX_REG_DMA_LENGTH 0xcc
#define TGX_REG_DMA_COMMAND 0xd0
#define TGX_REG_DMA_STATUS 0xd4
#define TGX_DMA_STATUS_BUSY BIT(0)
#define TGX_DMA_STATUS_DONE BIT(1)
#define TGX_DMA_STATUS_ERROR BIT(2)
#define TGX_DMA_STATUS_REJECTED BIT(3)
#define TGX_DMA_STATUS_READY BIT(4)
#define TGX_DMA_TEST_BYTES 64
#define TGX_DMA_TEST_VRAM TGX_FB_BASE(2)

static int tgx_dma_transfer(struct tarugx *tgx, dma_addr_t host_address,
			    u32 vram_address, u32 length_bytes,
			    unsigned int timeout_us)
{
	u32 status;
	int ret;

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
	ret = readl_poll_timeout(tgx->regs + TGX_REG_DMA_STATUS, status,
				 status & (TGX_DMA_STATUS_DONE |
					   TGX_DMA_STATUS_ERROR |
					   TGX_DMA_STATUS_REJECTED),
				 10, timeout_us);
	if (ret)
		return ret;
	if (!(status & TGX_DMA_STATUS_DONE) ||
	    status & (TGX_DMA_STATUS_ERROR | TGX_DMA_STATUS_REJECTED))
		return -EIO;
	return 0;
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
	writel(TGX_PITCH, tgx->regs + TGX_REG_BLIT_SRC_STRIDE);
	writel(TGX_PITCH, tgx->regs + TGX_REG_BLIT_DST_STRIDE);
	writel((TGX_HEIGHT << 16) | TGX_WIDTH, tgx->regs + TGX_REG_BLIT_SIZE);
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

static bool tgx_rect_valid(u32 x, u32 y, u32 width, u32 height)
{
	return width && height && x < TGX_WIDTH && y < TGX_HEIGHT &&
		width <= TGX_WIDTH - x && height <= TGX_HEIGHT - y;
}

static u32 tgx_next_render_fence(struct tarugx *tgx)
{
	tgx->render_fence++;
	if (!tgx->render_fence)
		tgx->render_fence++;
	return tgx->render_fence;
}

static bool tgx_command_valid(const struct drm_tarugx_command *command)
{
	if (command->flags || command->pad[0] || command->pad[1] ||
	    command->pad[2])
		return false;
	if (command->opcode == DRM_TARUGX_OP_FILL)
		return !command->src_x && !command->src_y &&
			tgx_rect_valid(command->dst_x, command->dst_y,
				       command->width, command->height);
	if (command->opcode == DRM_TARUGX_OP_BLIT)
		return !command->color &&
			tgx_rect_valid(command->src_x, command->src_y,
				       command->width, command->height) &&
			tgx_rect_valid(command->dst_x, command->dst_y,
				       command->width, command->height);
	return false;
}

static void tgx_program_render(struct tarugx *tgx, u32 base,
			       const struct drm_tarugx_command *command)
{
	if (command->opcode == DRM_TARUGX_OP_FILL) {
		writel(base + command->dst_y * TGX_PITCH + command->dst_x * 4,
		       tgx->regs + TGX_REG_FILL_ADDR);
		writel(TGX_PITCH, tgx->regs + TGX_REG_FILL_STRIDE);
		writel((command->height << 16) | command->width,
		       tgx->regs + TGX_REG_FILL_SIZE);
		writel(command->color, tgx->regs + TGX_REG_FILL_COLOR);
	} else {
		writel(base + command->src_y * TGX_PITCH + command->src_x * 4,
		       tgx->regs + TGX_REG_BLIT_SRC);
		writel(base + command->dst_y * TGX_PITCH + command->dst_x * 4,
		       tgx->regs + TGX_REG_BLIT_DST);
		writel(TGX_PITCH, tgx->regs + TGX_REG_BLIT_SRC_STRIDE);
		writel(TGX_PITCH, tgx->regs + TGX_REG_BLIT_DST_STRIDE);
		writel((command->height << 16) | command->width,
		       tgx->regs + TGX_REG_BLIT_SIZE);
	}
}

static int tgx_wait_render(struct tarugx *tgx, u32 fence, u32 timeout_ms)
{
	u32 status;
	long waited;

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
		readl(tgx->regs + TGX_REG_QUEUE_COMPLETED) == fence ||
		readl(tgx->regs + TGX_REG_QUEUE_STATUS) & TGX_QUEUE_ERROR,
		msecs_to_jiffies(timeout_ms));
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
	tgx->render_async_valid = false;
	tgx->render_async_completed = false;
	tgx->render_async_owner = NULL;
	tgx->render_async_fence = 0;
	tgx->render_async_commands = 0;
	tgx->render_async_result = 0;
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
				u32 count, struct drm_file *async_owner,
				u32 *fence_out)
{
	u32 active, base, fence = 0, status, submitted = 0;
	int ret = 0, wait_ret;

	if (tgx->render_async_valid) {
		if (tgx->render_async_owner)
			return -EBUSY;
		ret = tgx_reap_async(tgx, 0);
		if (!tgx->render_async_completed)
			return -EBUSY;
		tgx_clear_async(tgx);
		if (ret)
			return ret;
	}
	active = readl(tgx->regs + TGX_REG_ACTIVE_BUFFER);
	if (active > 1)
		return -EIO;
	status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	if (status & TGX_QUEUE_ERROR) {
		writel(TGX_QUEUE_ERROR, tgx->regs + TGX_REG_QUEUE_STATUS);
		status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	}
	if (status & TGX_QUEUE_ERROR)
		return -EIO;
	if (!(status & TGX_QUEUE_READY) ||
	    status & (TGX_QUEUE_ACTIVE | TGX_QUEUE_LEVEL))
		return -EBUSY;

	base = TGX_FB_BASE(active);
	for (submitted = 0; submitted < count; submitted++) {
		status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
		if (!(status & TGX_QUEUE_READY)) {
			ret = -EBUSY;
			break;
		}
		tgx_program_render(tgx, base, &commands[submitted]);
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

	(void)file;
	if (args->flags || args->pad ||
	    !tgx_rect_valid(args->x, args->y, args->width, args->height))
		return -EINVAL;
	if (!tgx->render_available)
		return -EOPNOTSUPP;
	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	mutex_lock(&tgx->render_lock);
	ret = tgx_execute_commands(tgx, &command, 1, NULL, &args->fence);
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

	(void)file;
	if (args->flags ||
	    !tgx_rect_valid(args->src_x, args->src_y,
			    args->width, args->height) ||
	    !tgx_rect_valid(args->dst_x, args->dst_y,
			    args->width, args->height))
		return -EINVAL;
	if (!tgx->render_available)
		return -EOPNOTSUPP;
	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	mutex_lock(&tgx->render_lock);
	ret = tgx_execute_commands(tgx, &command, 1, NULL, &args->fence);
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

	if (!args->count || args->count > DRM_TARUGX_MAX_COMMANDS ||
	    args->flags & ~DRM_TARUGX_SUBMIT_ASYNC || args->pad)
		return -EINVAL;
	for (i = 0; i < args->count; i++)
		if (!tgx_command_valid(&args->commands[i]))
			return -EINVAL;
	if (!tgx->render_available)
		return -EOPNOTSUPP;
	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	mutex_lock(&tgx->render_lock);
	ret = tgx_execute_commands(tgx, args->commands, args->count,
				   args->flags & DRM_TARUGX_SUBMIT_ASYNC ?
				   file : NULL,
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
	if (drm_mm_node_allocated(&object->node))
		drm_mm_remove_node(&object->node);
	mutex_unlock(&tgx->vram_lock);
	drm_gem_object_release(gem);
	kfree(object);
}

static const struct drm_gem_object_funcs tgx_vram_object_funcs = {
	.free = tgx_vram_object_free,
	.mmap = tgx_vram_object_mmap,
};

static int tgx_ioctl_vram_create(struct drm_device *drm, void *data,
				 struct drm_file *file)
{
	struct tarugx *tgx = container_of(drm, struct tarugx, drm);
	struct drm_tarugx_vram_create *args = data;
	struct tarugx_vram_object *object;
	u64 size;
	int ret;

	if (!args->size || args->size > TGX_VRAM_BYTES - TGX_VRAM_GEM_BASE ||
	    args->flags)
		return -EINVAL;
	size = PAGE_ALIGN(args->size);
	object = kzalloc(sizeof(*object), GFP_KERNEL);
	if (!object)
		return -ENOMEM;
	object->base.funcs = &tgx_vram_object_funcs;
	ret = drm_gem_object_init(drm, &object->base, size);
	if (ret) {
		kfree(object);
		return ret;
	}
	mutex_lock(&tgx->vram_lock);
	ret = drm_mm_insert_node_generic(&tgx->vram_mm, &object->node, size,
					 PAGE_SIZE, 0, DRM_MM_INSERT_BEST);
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
	args->mmap_offset = drm_vma_node_offset_addr(&object->base.vma_node);
out_put:
	drm_gem_object_put(&object->base);
	return ret;
}

static void tgx_cpu_copy_frame(struct tarugx *tgx, u32 source, u32 target)
{
	u32 offset;

	for (offset = 0; offset < TGX_FRAME_BYTES; offset += 4)
		writel(readl(tgx->vram + source + offset),
		       tgx->vram + target + offset);
	readl(tgx->vram + target + TGX_FRAME_BYTES - 4);
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
	if (!length || length > TGX_FRAME_BYTES || (length & 3))
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
	&dev_attr_render_commands.attr,
	&dev_attr_render_failures.attr,
	&dev_attr_render_queue_peak.attr,
	&dev_attr_dma_test.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tgx);

static const struct drm_display_mode tgx_mode = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		 25188, 640, 656, 752, 800, 0, 480, 490, 492, 525, 0,
		 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)
};

static irqreturn_t tgx_irq(int irq, void *data)
{
	struct tarugx *tgx = data;

	atomic64_inc(&tgx->msi_interrupts);
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
	struct drm_rect damage, copy;
	unsigned int x, y;
	u64 copied;
	u32 active, target;
	s64 vblank_before;
	long wait_ret;
	int idx, target_index, ret = 0, vblank_ret = 0;
	u64 pixels;
	u32 pixel;
	bool full_damage, did_flip_fallback = false;
	if (!fb || !pipe->crtc.state->active)
		return;
	/* A new FB must replace the single physical scanout buffer in full. */
	if (old->fb != fb || drm_atomic_crtc_needs_modeset(pipe->crtc.state))
		damage = DRM_RECT_INIT(0, 0, 640, 480);
	else if (!drm_atomic_helper_damage_merged(old, state, &damage))
		return;
	if (!drm_dev_enter(&tgx->drm, &idx))
		return;
	mutex_lock(&tgx->render_lock);
	if (tgx->render_async_valid) {
		u32 pending_fence = tgx->render_async_fence;

		ret = tgx_reap_async(tgx, TGX_RENDER_TIMEOUT_MS);
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
	active = readl(tgx->regs + TGX_REG_ACTIVE_BUFFER);
	target_index = tgx->legacy_single_buffer && active <= 1 ?
		active : (active == 0 ? 1 : 0);
	target = TGX_FB_BASE(target_index);
	copy = damage;
	full_damage = copy.x1 == 0 && copy.y1 == 0 &&
		copy.x2 == TGX_WIDTH && copy.y2 == TGX_HEIGHT;
	if (!full_damage && !tgx->legacy_single_buffer) {
		ret = tgx_blit_copy_frame(tgx, TGX_FB_BASE(active), target);
		if (ret) {
			atomic64_inc(&tgx->blit_sync_failures);
			dev_warn_ratelimited(tgx->drm.dev,
					     "frame sync blit failed: %d; using CPU copy\n",
					     ret);
			tgx_cpu_copy_frame(tgx, TGX_FB_BASE(active), target);
		} else {
			atomic64_inc(&tgx->blit_syncs);
		}
		ret = 0;
	}
	copied = (u64)(copy.x2 - copy.x1) * (copy.y2 - copy.y1) * 4;
	if (full_damage && dma_upload && tgx->dma_available) {
		for (y = 0; y < TGX_HEIGHT; y++)
			iosys_map_memcpy_from(tgx->dma_staging + y * TGX_PITCH,
					      &shadow->data[0],
					      y * fb->pitches[0], TGX_PITCH);
		dma_wmb();
		mutex_lock(&tgx->dma_lock);
		ret = tgx_dma_transfer(tgx, tgx->dma_staging_addr, target,
				       TGX_FRAME_BYTES, 2000000);
		mutex_unlock(&tgx->dma_lock);
		if (!ret) {
			atomic64_inc(&tgx->dma_uploads);
			goto upload_complete;
		}
		atomic64_inc(&tgx->dma_upload_failures);
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
			       tgx->vram + target + y * TGX_PITCH + x * 4);
			x++;
		}
		for (; x + 1 < copy.x2; x += 2) {
			iosys_map_memcpy_from(&pixels, &shadow->data[0],
					     y * fb->pitches[0] + x * 4, 8);
			writeq(le64_to_cpu((__force __le64)pixels),
			       tgx->vram + target + y * TGX_PITCH + x * 4);
		}
		if (x < copy.x2) {
			iosys_map_memcpy_from(&pixel, &shadow->data[0],
					     y * fb->pitches[0] + x * 4, 4);
			writel(le32_to_cpu((__force __le32)pixel),
			       tgx->vram + target + y * TGX_PITCH + x * 4);
		}
	}
	/* BAR2 read orders the preceding posted writes through the DDR path. */
	readl(tgx->vram + target + TGX_FRAME_BYTES - 4);
upload_complete:
	if (tgx->legacy_single_buffer) {
		/* Legacy hardware scans a fixed page.  Updating only the damage
		 * rectangle in place is slower than a flip but avoids repeatedly
		 * copying an entire frame while scanout is active. */
		atomic64_inc(&tgx->flip_fallbacks);
		atomic64_add(copied, &tgx->bytes_uploaded);
		mutex_unlock(&tgx->render_lock);
		drm_dev_exit(idx);
		return;
	}
	vblank_before = atomic64_read(&tgx->msi_interrupts);
	writel(target, tgx->regs + TGX_REG_FB_BASE);
	writel(TGX_PITCH, tgx->regs + TGX_REG_FB_STRIDE);
	writel(1, tgx->regs + TGX_REG_FB_COMMIT);
	readl(tgx->regs);
	writel(1, tgx->regs + TGX_REG_DISPLAY_CTRL);
	readl(tgx->regs);
	/* The same VBlank both raises MSI and applies FB_COMMIT.  Wait for those
	 * two facts together instead of polling the flip and then waiting for an
	 * extra frame.  A pre-commit interrupt cannot satisfy both conditions. */
	wait_ret = wait_event_timeout(tgx->vblank_wait,
			atomic64_read(&tgx->msi_interrupts) != vblank_before &&
			readl(tgx->regs + TGX_REG_ACTIVE_BUFFER) == target_index,
			msecs_to_jiffies(100));
	if (!wait_ret) {
		active = readl(tgx->regs + TGX_REG_ACTIVE_BUFFER);
		if (active == target_index)
			vblank_ret = 1;
		else {
			ret = -ETIMEDOUT;
			/* Older Flash images do not implement the current commit
			 * handshake.  Preserve a usable display after a power cycle by
			 * copying the completed inactive page onto the page that legacy
			 * hardware keeps scanning out. */
			if (active <= 1) {
				tgx_cpu_copy_frame(tgx, target,
						   TGX_FB_BASE(active));
				atomic64_inc(&tgx->flip_fallbacks);
				tgx->legacy_single_buffer = true;
				did_flip_fallback = true;
			}
		}
	}
	if (ret)
		atomic64_inc(&tgx->flip_timeouts);
	else if (vblank_ret)
		atomic64_inc(&tgx->vblank_timeouts);
	else
		atomic64_inc(&tgx->page_flips);
	atomic64_add(copied, &tgx->bytes_uploaded);
	if (ret)
		dev_warn_ratelimited(tgx->drm.dev,
				     "page flip to buffer %d timed out (active %u)%s\n",
				     target_index, active,
				     did_flip_fallback ? "; copied to active page" : "");
	else if (vblank_ret)
		dev_warn_ratelimited(tgx->drm.dev,
				     "VBlank wait timed out after flip to buffer %d\n",
				     target_index);
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
	DRM_IOCTL_DEF_DRV(TARUGX_WAIT, tgx_ioctl_wait, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(TARUGX_VRAM_CREATE, tgx_ioctl_vram_create, DRM_MASTER),
};

static struct drm_gem_object *tgx_prime_import(struct drm_device *drm,
					     struct dma_buf *dma_buf)
{
	/* Initial driver accepts its own CPU buffers only; no imported DMA BOs. */
	return ERR_PTR(-EOPNOTSUPP);
}
static const struct drm_driver tgx_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &tgx_fops,
	.postclose = tgx_postclose,
	DRM_GEM_SHMEM_DRIVER_OPS,
	.ioctls = tgx_ioctls,
	.num_ioctls = ARRAY_SIZE(tgx_ioctls),
	.gem_prime_import = tgx_prime_import,
	.name = "tarugx",
	.desc = "TaruGX/100 CPU framebuffer",
	.date = "20261103",
	.major = 0, .minor = 3,
};

static void tgx_vram_mm_takedown(struct drm_device *drm, void *data)
{
	struct tarugx *tgx = data;

	(void)drm;
	WARN_ON(!drm_mm_clean(&tgx->vram_mm));
	drm_mm_takedown(&tgx->vram_mm);
}

static int tgx_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const u32 formats[] = { DRM_FORMAT_XRGB8888 };
	struct tarugx *tgx;
	struct drm_device *drm;
	u32 dma_status, queue_status;
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
	tgx->pdev = pdev;
	mutex_init(&tgx->dma_lock);
	mutex_init(&tgx->render_lock);
	mutex_init(&tgx->vram_lock);
	drm_mm_init(&tgx->vram_mm, TGX_VRAM_GEM_BASE,
		    TGX_VRAM_BYTES - TGX_VRAM_GEM_BASE);
	ret = drmm_add_action_or_reset(drm, tgx_vram_mm_takedown, tgx);
	if (ret)
		return ret;
	tgx->regs = pcim_iomap_table(pdev)[0];
	tgx->vram = pcim_iomap_table(pdev)[2];
	tgx->dma_staging = dmam_alloc_coherent(&pdev->dev, TGX_FRAME_BYTES,
					       &tgx->dma_staging_addr, GFP_KERNEL);
	if (!tgx->dma_staging)
		return -ENOMEM;
	init_waitqueue_head(&tgx->vblank_wait);
	init_waitqueue_head(&tgx->render_wait);
	if (readl(tgx->regs) != 0x54475055)
		return -ENODEV;
	dma_status = readl(tgx->regs + TGX_REG_DMA_STATUS);
	tgx->dma_available = (dma_status & TGX_DMA_STATUS_READY) &&
		!(dma_status & (TGX_DMA_STATUS_BUSY | TGX_DMA_STATUS_ERROR |
			 TGX_DMA_STATUS_REJECTED));
	dev_info(&pdev->dev, "full-frame DMA uploads %s\n",
		 tgx->dma_available ? "available" : "unavailable; using BAR2");
	queue_status = readl(tgx->regs + TGX_REG_QUEUE_STATUS);
	tgx->render_available = queue_status & TGX_QUEUE_READY;
	tgx->render_fence = readl(tgx->regs + TGX_REG_QUEUE_COMPLETED);
	dev_info(&pdev->dev, "2D render queue %s\n",
		 tgx->render_available ? "available" : "unavailable");
	pci_set_drvdata(pdev, tgx);
	ret = devm_device_add_groups(&pdev->dev, tgx_groups);
	if (ret)
		return ret;
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
	return drm_dev_register(drm, 0);
}

static void tgx_remove(struct pci_dev *pdev)
{
	struct tarugx *tgx = pci_get_drvdata(pdev);
	writel(0, tgx->regs + TGX_REG_IRQ_ENABLE);
	readl(tgx->regs);
	synchronize_irq(pci_irq_vector(pdev, 0));
	drm_atomic_helper_shutdown(&tgx->drm);
	drm_dev_unplug(&tgx->drm);
}
static void tgx_shutdown(struct pci_dev *pdev)
{
	struct tarugx *tgx = pci_get_drvdata(pdev);
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
