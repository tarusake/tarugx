/* SPDX-License-Identifier: MIT */
#ifndef _UAPI_TARUGX_DRM_H_
#define _UAPI_TARUGX_DRM_H_

#include <drm/drm.h>
#include <linux/types.h>

/* The screen commands address the current scanout page.  Userspace supplies
 * pixels and rectangles, never physical VRAM addresses.
 */
struct drm_tarugx_fill {
	__u32 x;
	__u32 y;
	__u32 width;
	__u32 height;
	__u32 color;
	__u32 flags;
	__u32 fence; /* returned after the command has completed */
	__u32 pad;
};

struct drm_tarugx_blit {
	__u32 src_x;
	__u32 src_y;
	__u32 dst_x;
	__u32 dst_y;
	__u32 width;
	__u32 height;
	__u32 flags;
	__u32 fence; /* returned after the command has completed */
};

#define DRM_TARUGX_OP_FILL 1
#define DRM_TARUGX_OP_BLIT 2
#define DRM_TARUGX_MAX_COMMANDS 4
#define DRM_TARUGX_SUBMIT_ASYNC (1U << 0)

struct drm_tarugx_command {
	__u32 opcode;
	__u32 flags;
	__u32 src_x;
	__u32 src_y;
	__u32 dst_x;
	__u32 dst_y;
	__u32 width;
	__u32 height;
	__u32 color;
	__u32 pad[3];
};

struct drm_tarugx_submit {
	__u32 count;
	__u32 flags;
	__u32 fence; /* final command fence, returned after the batch completes */
	__u32 pad;
	struct drm_tarugx_command commands[DRM_TARUGX_MAX_COMMANDS];
};

struct drm_tarugx_wait {
	__u32 fence;
	__u32 timeout_ms; /* zero performs a non-blocking poll */
	__u32 flags;
	__u32 pad;
};

struct drm_tarugx_vram_create {
	__u32 width;
	__u32 height;
	__u32 flags;
	__u32 handle;
	__u32 pitch;
	__u32 pad;
	__u64 size;
	__u64 mmap_offset;
};

/* Allocate the physical scanout page at BAR2 offset zero.  This is reserved
 * for the DRM master so a display server can combine CPU fallback through the
 * mmap offset with FPGA fill/blit on the same RGB565 front buffer. */
#define DRM_TARUGX_VRAM_FRONT (1U << 0)

struct drm_tarugx_submit_gem {
	__u32 handle;
	__u32 count;
	__u32 flags;
	__u32 fence;
	__u32 out_syncobj; /* optional syncobj signaled by the final fence */
	__u32 pad;
	__u64 out_point; /* zero for binary syncobj, otherwise timeline point */
	struct drm_tarugx_command commands[DRM_TARUGX_MAX_COMMANDS];
};

/* Dependency-aware extension.  The input fence is waited interruptibly before
 * the batch is submitted; output completion uses the same binary/timeline
 * syncobj semantics as drm_tarugx_submit_gem.
 */
struct drm_tarugx_submit_gem2 {
	__u32 handle;
	__u32 count;
	__u32 flags;
	__u32 pad;
	__u32 in_syncobj;
	__u32 out_syncobj;
	__u64 in_point;
	__u64 out_point;
	struct drm_tarugx_command commands[DRM_TARUGX_MAX_COMMANDS];
};

/* Copy a validated RGB565 rectangle from a DMA-coherent host dumb GEM into a
 * TaruGX VRAM GEM.  Coordinates and width must preserve the FPGA DMA engine's
 * 32-bit transfer alignment. */
struct drm_tarugx_upload_gem {
	__u32 source_handle;
	__u32 destination_handle;
	__u32 source_x;
	__u32 source_y;
	__u32 destination_x;
	__u32 destination_y;
	__u32 width;
	__u32 height;
	__u32 flags;
	__u32 pad[3];
};

#define DRM_TARUGX_FILL 0x00
#define DRM_TARUGX_BLIT 0x01
#define DRM_TARUGX_SUBMIT 0x02
#define DRM_TARUGX_WAIT 0x03
#define DRM_TARUGX_VRAM_CREATE 0x04
#define DRM_TARUGX_SUBMIT_GEM 0x05
#define DRM_TARUGX_SUBMIT_GEM2 0x06
#define DRM_TARUGX_UPLOAD_GEM 0x07

#define DRM_IOCTL_TARUGX_FILL \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_FILL, struct drm_tarugx_fill)
#define DRM_IOCTL_TARUGX_BLIT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_BLIT, struct drm_tarugx_blit)
#define DRM_IOCTL_TARUGX_SUBMIT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_SUBMIT, struct drm_tarugx_submit)
#define DRM_IOCTL_TARUGX_WAIT \
	DRM_IOW(DRM_COMMAND_BASE + DRM_TARUGX_WAIT, struct drm_tarugx_wait)
#define DRM_IOCTL_TARUGX_VRAM_CREATE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_VRAM_CREATE, \
		 struct drm_tarugx_vram_create)
#define DRM_IOCTL_TARUGX_SUBMIT_GEM \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_SUBMIT_GEM, \
		 struct drm_tarugx_submit_gem)
#define DRM_IOCTL_TARUGX_SUBMIT_GEM2 \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_SUBMIT_GEM2, \
		 struct drm_tarugx_submit_gem2)
#define DRM_IOCTL_TARUGX_UPLOAD_GEM \
	DRM_IOW(DRM_COMMAND_BASE + DRM_TARUGX_UPLOAD_GEM, \
		struct drm_tarugx_upload_gem)

#endif
