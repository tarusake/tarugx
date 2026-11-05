// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "tarugx_drm.h"

static int close_handle(int fd, uint32_t handle)
{
	struct drm_gem_close close_args = { .handle = handle };

	return ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_args);
}

int main(int argc, char **argv)
{
	struct drm_tarugx_vram_create vram = {0};
	struct drm_tarugx_submit_gem submit = {
		.count = 4,
		.flags = DRM_TARUGX_SUBMIT_ASYNC,
	};
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	uint32_t framebuffer = 0, crtc = 0, format;
	uint8_t *pixels = MAP_FAILED;
	unsigned hold_seconds = 5;
	int fd = -1, enabled = 0, result = 1;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s /dev/dri/cardN [hold-seconds]\n", argv[0]);
		return 2;
	}
	if (argc == 3)
		hold_seconds = strtoul(argv[2], NULL, 0);
	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0 || drmSetMaster(fd)) {
		perror("open/set DRM master");
		goto out;
	}
	resources = drmModeGetResources(fd);
	if (!resources || resources->count_crtcs != 1) {
		fputs("expected one CRTC\n", stderr);
		goto out;
	}
	crtc = resources->crtcs[0];
	for (int i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector && connector->connection == DRM_MODE_CONNECTED &&
		    connector->count_modes)
			break;
		drmModeFreeConnector(connector);
		connector = NULL;
	}
	if (!connector) {
		fputs("connected output not found\n", stderr);
		goto out;
	}
	vram.width = connector->modes[0].hdisplay;
	vram.height = connector->modes[0].vdisplay;
	if (ioctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &vram)) {
		perror("create VRAM GEM");
		goto out;
	}
	format = vram.pitch / vram.width == 2 ?
		DRM_FORMAT_RGB565 : DRM_FORMAT_XRGB8888;
	pixels = mmap(NULL, vram.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      fd, (off_t)vram.mmap_offset);
	if (pixels != MAP_FAILED) {
		if (format != DRM_FORMAT_XRGB8888) {
			fputs("RGB565 VRAM above BAR unexpectedly mapped\n", stderr);
			goto out;
		}
		for (uint32_t y = 0; y < vram.height; y++) {
			uint32_t *row = (uint32_t *)(pixels + y * vram.pitch);

			for (uint32_t x = 0; x < vram.width; x++)
				row[x] = ((x / 80 + y / 60) & 1) ?
					0x002060c0 : 0x00e0a030;
		}
	} else {
		static const uint16_t colors[4] = {
			0x19f8, 0x04ff, 0xfbe0, 0xffff,
		};

		if (errno != EOPNOTSUPP || format != DRM_FORMAT_RGB565) {
			perror("mmap VRAM GEM");
			goto out;
		}
		for (uint32_t i = 0; i < submit.count; i++) {
			submit.commands[i].opcode = DRM_TARUGX_OP_FILL;
			submit.commands[i].dst_y = vram.height * i / submit.count;
			submit.commands[i].width = vram.width;
			submit.commands[i].height =
				vram.height * (i + 1) / submit.count -
				submit.commands[i].dst_y;
			submit.commands[i].color = colors[i];
		}
		submit.handle = vram.handle;
		if (ioctl(fd, DRM_IOCTL_TARUGX_SUBMIT_GEM, &submit)) {
			perror("fill VRAM GEM");
			goto out;
		}
	}
	handles[0] = vram.handle;
	pitches[0] = vram.pitch;
	if (drmModeAddFB2(fd, vram.width, vram.height, format, handles, pitches,
			  offsets, &framebuffer, 0)) {
		perror("add VRAM framebuffer");
		goto out;
	}
	if (drmModeSetCrtc(fd, crtc, framebuffer, 0, 0,
			   &connector->connector_id, 1, &connector->modes[0])) {
		perror("direct VRAM modeset");
		goto out;
	}
	enabled = 1;
	printf("VRAM GEM direct scanout PASS: %ux%u pitch=%u format=%s, "
	       "implicit fence, no CPU shadow copy\n",
	       vram.width, vram.height, vram.pitch,
	       format == DRM_FORMAT_RGB565 ? "RGB565" : "XRGB8888");
	if (hold_seconds) {
		struct timespec remaining = { .tv_sec = hold_seconds };

		while (nanosleep(&remaining, &remaining) && errno == EINTR)
			;
	}
	result = 0;
out:
	if (enabled && drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0, NULL)) {
		perror("disable CRTC");
		result = 1;
	}
	if (framebuffer)
		drmModeRmFB(fd, framebuffer);
	if (pixels != MAP_FAILED)
		munmap(pixels, vram.size);
	if (vram.handle)
		close_handle(fd, vram.handle);
	if (connector)
		drmModeFreeConnector(connector);
	if (resources)
		drmModeFreeResources(resources);
	if (fd >= 0)
		close(fd);
	return result;
}
