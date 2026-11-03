/* SPDX-License-Identifier: MIT */
#ifndef _UAPI_TARUGX_DRM_H_
#define _UAPI_TARUGX_DRM_H_

#include <drm/drm.h>
#include <linux/types.h>

/*
 * The first 2D API deliberately addresses only the visible 640x480 scanout
 * page.  Userspace supplies pixels, never physical VRAM addresses.
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

#define DRM_TARUGX_FILL 0x00
#define DRM_TARUGX_BLIT 0x01

#define DRM_IOCTL_TARUGX_FILL \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_FILL, struct drm_tarugx_fill)
#define DRM_IOCTL_TARUGX_BLIT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_TARUGX_BLIT, struct drm_tarugx_blit)

#endif
