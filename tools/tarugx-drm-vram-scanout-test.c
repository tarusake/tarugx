// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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
	struct drm_tarugx_vram_create vram = { .width = 640, .height = 480 };
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	uint32_t framebuffer = 0, crtc = 0;
	uint32_t *pixels = MAP_FAILED;
	int fd = -1, enabled = 0, result = 1;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/dri/cardN\n", argv[0]);
		return 2;
	}
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
	if (ioctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE, &vram)) {
		perror("create VRAM GEM");
		goto out;
	}
	pixels = mmap(NULL, vram.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      fd, (off_t)vram.mmap_offset);
	if (pixels == MAP_FAILED) {
		perror("mmap VRAM GEM");
		goto out;
	}
	for (uint32_t y = 0; y < 480; y++)
		for (uint32_t x = 0; x < 640; x++)
			pixels[y * 640 + x] = ((x / 80 + y / 60) & 1) ?
				0x002060c0 : 0x00e0a030;
	handles[0] = vram.handle;
	pitches[0] = vram.pitch;
	if (drmModeAddFB2(fd, 640, 480, DRM_FORMAT_XRGB8888, handles, pitches,
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
	puts("VRAM GEM direct scanout PASS: standard DRM framebuffer, no CPU shadow copy");
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
