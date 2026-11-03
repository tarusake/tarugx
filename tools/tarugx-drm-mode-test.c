// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

static uint32_t color(unsigned band)
{
	static const uint32_t colors[] = {
		0x00ff0000, 0x0000ff00, 0x000000ff,
		0x0000ffff, 0x00ff00ff, 0x00ffff00,
	};

	return colors[band % 6];
}

int main(int argc, char **argv)
{
	struct drm_mode_create_dumb create = { .bpp = 32 };
	struct drm_mode_map_dumb map = {0};
	struct drm_mode_destroy_dumb destroy = {0};
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	uint32_t framebuffer = 0, crtc = 0;
	uint8_t *pixels = MAP_FAILED;
	unsigned hold_seconds = 10;
	int fd = -1, rc = 1;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s /dev/dri/cardN [hold-seconds]\n", argv[0]);
		return 2;
	}
	if (argc == 3)
		hold_seconds = strtoul(argv[2], NULL, 0);
	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0 || drmSetMaster(fd)) {
		perror("open/master");
		goto out;
	}
	resources = drmModeGetResources(fd);
	if (!resources || resources->count_crtcs != 1) {
		fprintf(stderr, "expected exactly one CRTC\n");
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
		fprintf(stderr, "no connected connector with a mode\n");
		goto out;
	}
	create.width = connector->modes[0].hdisplay;
	create.height = connector->modes[0].vdisplay;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create)) {
		perror("create dumb buffer");
		goto out;
	}
	destroy.handle = create.handle;
	handles[0] = create.handle;
	pitches[0] = create.pitch;
	if (drmModeAddFB2(fd, create.width, create.height,
			  DRM_FORMAT_XRGB8888, handles, pitches, offsets,
			  &framebuffer, 0)) {
		perror("add framebuffer");
		goto out;
	}
	map.handle = create.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) {
		perror("map dumb buffer");
		goto out;
	}
	pixels = mmap(NULL, create.size, PROT_READ | PROT_WRITE,
		      MAP_SHARED, fd, map.offset);
	if (pixels == MAP_FAILED) {
		perror("mmap dumb buffer");
		goto out;
	}
	for (unsigned y = 0; y < create.height; y++) {
		uint32_t *row = (uint32_t *)(pixels + y * create.pitch);

		for (unsigned x = 0; x < create.width; x++)
			row[x] = color((uint64_t)y * 6 / create.height);
	}
	if (drmModeSetCrtc(fd, crtc, framebuffer, 0, 0,
			   &connector->connector_id, 1, &connector->modes[0])) {
		perror("modeset");
		goto out;
	}
	printf("DRM mode PASS: %ux%u pitch=%u size=%llu hold=%us\n",
	       create.width, create.height, create.pitch,
	       (unsigned long long)create.size, hold_seconds);
	fflush(stdout);
	{
		struct timespec remaining = { .tv_sec = hold_seconds };

		while (nanosleep(&remaining, &remaining) && errno == EINTR)
			;
	}
	rc = 0;
out:
	if (crtc && fd >= 0)
		drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0, NULL);
	if (pixels != MAP_FAILED)
		munmap(pixels, create.size);
	if (framebuffer)
		drmModeRmFB(fd, framebuffer);
	if (destroy.handle)
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);
	if (fd >= 0)
		close(fd);
	return rc;
}
