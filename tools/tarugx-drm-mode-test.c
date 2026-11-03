// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

#include "tarugx_drm.h"

#ifndef DMA_BUF_IOCTL_IMPORT_SYNC_FILE
struct dma_buf_import_sync_file {
	uint32_t flags;
	int32_t fd;
};
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE \
	_IOW(DMA_BUF_BASE, 3, struct dma_buf_import_sync_file)
#endif

static uint32_t color(unsigned band)
{
	static const uint32_t colors[] = {
		0x00ff0000, 0x0000ff00, 0x000000ff,
		0x0000ffff, 0x00ff00ff, 0x00ffff00,
	};

	return colors[band % 6];
}

static double monotonic_seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec + now.tv_nsec / 1000000000.0;
}

static void fill_rect(uint8_t *pixels, uint32_t pitch, const drmModeClip *clip,
		      uint32_t color, uint32_t bpp)
{
	for (uint32_t y = clip->y1; y < clip->y2; y++) {
		if (bpp == 16) {
			uint16_t *row = (uint16_t *)(pixels + y * pitch);
			uint16_t rgb565 = ((color >> 19) & 0x1f) << 11 |
				((color >> 10) & 0x3f) << 5 | ((color >> 3) & 0x1f);

			for (uint32_t x = clip->x1; x < clip->x2; x++)
				row[x] = rgb565;
		} else {
			uint32_t *row = (uint32_t *)(pixels + y * pitch);

			for (uint32_t x = clip->x1; x < clip->x2; x++)
				row[x] = color;
		}
	}
}

int main(int argc, char **argv)
{
	struct drm_mode_create_dumb create = { .bpp = 32 };
	struct drm_mode_map_dumb map = {0};
	struct drm_mode_destroy_dumb destroy = {0};
	struct drm_gem_close import_close = {0};
	struct dma_heap_allocation_data heap_alloc = {
		.fd_flags = O_RDWR | O_CLOEXEC,
	};
	drmModeRes *resources = NULL;
	drmModeConnector *connector = NULL;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	uint32_t external_framebuffer = 0, external_handle = 0;
	uint32_t render_handle = 0, render_syncobj = 0;
	uint32_t framebuffer = 0, crtc = 0;
	uint8_t *external_pixels = MAP_FAILED;
	uint8_t *pixels = MAP_FAILED;
	unsigned hold_seconds = 10;
	unsigned requested_bpp = 0;
	double modeset_started, modeset_seconds, dirty_started;
	double dirty_seconds[3];
	drmModeClip clips[2] = {
		{120, 120, 440, 200},
		{900, 700, 1100, 701},
	};
	drmModeClip full_clip;
	int fd = -1, heap_buffer_fd = -1, heap_fd = -1, import_fd = -1;
	int prime_fd = -1, render_sync_file = -1, rc = 1;

	if (argc < 2 || argc > 4) {
		fprintf(stderr,
			"usage: %s /dev/dri/cardN [hold-seconds] [bpp]\n",
			argv[0]);
		return 2;
	}
	if (argc >= 3)
		hold_seconds = strtoul(argv[2], NULL, 0);
	if (argc == 4) {
		requested_bpp = strtoul(argv[3], NULL, 0);
		if (requested_bpp != 16 && requested_bpp != 32) {
			fputs("bpp must be 16 or 32\n", stderr);
			return 2;
		}
		create.bpp = requested_bpp;
	}
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
		if (errno != EINVAL || requested_bpp) {
			perror("create dumb buffer");
			goto out;
		}
		create = (struct drm_mode_create_dumb) {
			.width = connector->modes[0].hdisplay,
			.height = connector->modes[0].vdisplay,
			.bpp = 16,
		};
		if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create)) {
			perror("create RGB565 dumb buffer");
			goto out;
		}
	}
	destroy.handle = create.handle;
	heap_alloc.len = create.size;
	if (drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR,
			       &prime_fd)) {
		perror("export dumb buffer to PRIME fd");
		goto out;
	}
	import_fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (import_fd < 0 ||
	    drmPrimeFDToHandle(import_fd, prime_fd, &import_close.handle)) {
		perror("self-import PRIME fd");
		goto out;
	}
	puts("DRM PRIME self-import PASS: DMA-backed dumb buffer shared to a second file");
	heap_fd = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
	if (heap_fd >= 0) {
		uint32_t external_handles[4] = {0};
		uint32_t external_pitches[4] = {0};
		uint32_t external_offsets[4] = {0};

		if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_alloc)) {
			perror("allocate external dma-buf");
			goto out;
		}
		heap_buffer_fd = heap_alloc.fd;
		if (drmPrimeFDToHandle(fd, heap_buffer_fd,
				       &external_handle)) {
			perror("import contiguous external dma-buf");
			goto out;
		}
		external_handles[0] = external_handle;
		external_pitches[0] = create.pitch;
		if (drmModeAddFB2(fd, create.width, create.height,
				  create.bpp == 16 ? DRM_FORMAT_RGB565 :
				  DRM_FORMAT_XRGB8888,
				  external_handles, external_pitches,
				  external_offsets, &external_framebuffer, 0)) {
			perror("add external PRIME framebuffer");
			goto out;
		}
		external_pixels = mmap(NULL, create.size,
				       PROT_READ | PROT_WRITE, MAP_SHARED,
				       heap_buffer_fd, 0);
		if (external_pixels == MAP_FAILED) {
			perror("mmap external PRIME framebuffer");
			goto out;
		}
		{
			struct dma_buf_sync sync = {
				.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE,
			};

			if (ioctl(heap_buffer_fd, DMA_BUF_IOCTL_SYNC, &sync)) {
				perror("begin external dma-buf CPU access");
				goto out;
			}
		}
		for (unsigned band = 0; band < 6; band++) {
			drmModeClip band_clip = {
				0, create.height * band / 6,
				create.width, create.height * (band + 1) / 6,
			};

			fill_rect(external_pixels, create.pitch, &band_clip,
				  color(5 - band), create.bpp);
		}
		{
			struct dma_buf_sync sync = {
				.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
			};

			if (ioctl(heap_buffer_fd, DMA_BUF_IOCTL_SYNC, &sync)) {
				perror("end external dma-buf CPU access");
				goto out;
			}
		}
		{
			struct drm_tarugx_vram_create render_vram = {
				.width = create.width,
				.height = create.height,
			};
			struct drm_tarugx_submit_gem render_submit = {
				.count = 1,
				.flags = DRM_TARUGX_SUBMIT_ASYNC,
				.commands = {{
					.opcode = DRM_TARUGX_OP_BLIT,
					.width = create.width,
					.height = create.height,
				}},
			};
			struct dma_buf_import_sync_file import_sync = {
				.flags = DMA_BUF_SYNC_WRITE,
			};
			struct pollfd pending = { .events = POLLIN };

			if (drmIoctl(fd, DRM_IOCTL_TARUGX_VRAM_CREATE,
				     &render_vram)) {
				perror("create implicit-sync render surface");
				goto out;
			}
			render_handle = render_vram.handle;
			if (drmSyncobjCreate(fd, 0, &render_syncobj)) {
				perror("create implicit-sync syncobj");
				goto out;
			}
			render_submit.handle = render_handle;
			render_submit.out_syncobj = render_syncobj;
			if (drmIoctl(fd, DRM_IOCTL_TARUGX_SUBMIT_GEM,
				     &render_submit)) {
				perror("submit implicit-sync producer");
				goto out;
			}
			if (drmSyncobjExportSyncFile(fd, render_syncobj,
						 &render_sync_file)) {
				perror("export implicit-sync fence");
				goto out;
			}
			pending.fd = render_sync_file;
			if (poll(&pending, 1, 0) != 0) {
				fputs("implicit-sync producer completed too early\n",
				      stderr);
				goto out;
			}
			import_sync.fd = render_sync_file;
			if (ioctl(heap_buffer_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE,
				  &import_sync)) {
				perror("import implicit-sync fence into dma-buf");
				goto out;
			}
		}
		if (drmModeSetCrtc(fd, crtc, external_framebuffer, 0, 0,
				   &connector->connector_id, 1,
				   &connector->modes[0])) {
			perror("external PRIME modeset");
			goto out;
		}
		{
			struct pollfd completed = {
				.fd = render_sync_file,
				.events = POLLIN,
			};

			if (poll(&completed, 1, 0) != 1 ||
			    !(completed.revents & POLLIN)) {
				fputs("external modeset did not wait for dma-buf fence\n",
				      stderr);
				goto out;
			}
		}
		puts("DRM PRIME implicit sync PASS: modeset waited for imported write fence");
		puts("DRM PRIME external scanout PASS: contiguous dma-buf uploaded by PCIe DMA");
	}
	handles[0] = create.handle;
	pitches[0] = create.pitch;
	if (drmModeAddFB2(fd, create.width, create.height,
			  create.bpp == 16 ? DRM_FORMAT_RGB565 : DRM_FORMAT_XRGB8888,
			  handles, pitches, offsets,
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
	full_clip = (drmModeClip){0, 0, create.width, create.height};
	for (unsigned band = 0; band < 6; band++) {
		drmModeClip band_clip = {0, create.height * band / 6,
			create.width, create.height * (band + 1) / 6};
		fill_rect(pixels, create.pitch, &band_clip, color(band), create.bpp);
	}
	modeset_started = monotonic_seconds();
	if (drmModeSetCrtc(fd, crtc, framebuffer, 0, 0,
			   &connector->connector_id, 1, &connector->modes[0])) {
		perror("modeset");
		goto out;
	}
	modeset_seconds = monotonic_seconds() - modeset_started;
	fill_rect(pixels, create.pitch, &clips[0], 0x00ffffff, create.bpp);
	dirty_started = monotonic_seconds();
	if (drmModeDirtyFB(fd, framebuffer, &clips[0], 1)) {
		perror("first dirty framebuffer");
		goto out;
	}
	dirty_seconds[0] = monotonic_seconds() - dirty_started;
	fill_rect(pixels, create.pitch, &clips[1], 0x00ff8000, create.bpp);
	dirty_started = monotonic_seconds();
	if (drmModeDirtyFB(fd, framebuffer, &clips[1], 1)) {
		perror("second dirty framebuffer");
		goto out;
	}
	dirty_seconds[1] = monotonic_seconds() - dirty_started;
	fill_rect(pixels, create.pitch, &full_clip, 0x00204080, create.bpp);
	dirty_started = monotonic_seconds();
	if (drmModeDirtyFB(fd, framebuffer, &full_clip, 1)) {
		perror("active full dirty framebuffer");
		goto out;
	}
	dirty_seconds[2] = monotonic_seconds() - dirty_started;
	printf("DRM mode PASS: %ux%u pitch=%u size=%llu modeset=%.6fs dirty1=%.6fs dirty2=%.6fs active_full=%.6fs hold=%us\n",
	       create.width, create.height, create.pitch,
	       (unsigned long long)create.size, modeset_seconds,
	       dirty_seconds[0], dirty_seconds[1], dirty_seconds[2], hold_seconds);
	fflush(stdout);
	{
		struct timespec remaining = { .tv_sec = hold_seconds };

		while (nanosleep(&remaining, &remaining) && errno == EINTR)
			;
	}
	rc = 0;
out:
	if (import_close.handle && import_fd >= 0)
		drmIoctl(import_fd, DRM_IOCTL_GEM_CLOSE, &import_close);
	if (render_sync_file >= 0)
		close(render_sync_file);
	if (render_syncobj && fd >= 0)
		drmSyncobjDestroy(fd, render_syncobj);
	if (render_handle && fd >= 0) {
		struct drm_gem_close render_close = { .handle = render_handle };

		drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &render_close);
	}
	if (prime_fd >= 0)
		close(prime_fd);
	if (heap_buffer_fd >= 0)
		close(heap_buffer_fd);
	if (heap_fd >= 0)
		close(heap_fd);
	if (import_fd >= 0)
		close(import_fd);
	if (crtc && fd >= 0)
		drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0, NULL);
	if (pixels != MAP_FAILED)
		munmap(pixels, create.size);
	if (external_pixels != MAP_FAILED)
		munmap(external_pixels, create.size);
	if (framebuffer)
		drmModeRmFB(fd, framebuffer);
	if (external_framebuffer)
		drmModeRmFB(fd, external_framebuffer);
	if (external_handle && fd >= 0) {
		struct drm_gem_close external_close = {
			.handle = external_handle,
		};

		drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &external_close);
	}
	if (destroy.handle)
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);
	if (fd >= 0)
		close(fd);
	return rc;
}
