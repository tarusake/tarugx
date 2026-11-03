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

struct drm_tarugx_submit_gem {
	__u32 handle;
	__u32 count;
	__u32 flags;
	__u32 fence;
	__u32 out_syncobj; /* optional binary syncobj signaled by the final fence */
	__u32 pad[3];
	struct drm_tarugx_command commands[DRM_TARUGX_MAX_COMMANDS];
};

#define DRM_TARUGX_FILL 0x00
#define DRM_TARUGX_BLIT 0x01
#define DRM_TARUGX_SUBMIT 0x02
#define DRM_TARUGX_WAIT 0x03
#define DRM_TARUGX_VRAM_CREATE 0x04
#define DRM_TARUGX_SUBMIT_GEM 0x05

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

#endif
