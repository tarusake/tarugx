// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define WIDTH 1920u
#define HEIGHT 1080u
#define BAR_BYTES (4u * 1024u * 1024u)

int main(int argc, char **argv)
{
	volatile uint16_t *pixels;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /sys/.../resource2\n", argv[0]);
		return 2;
	}
	fd = open(argv[1], O_RDWR | O_SYNC | O_CLOEXEC);
	if (fd < 0) {
		perror("open BAR2");
		return 1;
	}
	pixels = mmap(NULL, BAR_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		perror("mmap BAR2");
		close(fd);
		return 1;
	}
	/* Red uses individual RGB565 stores.  Green packs adjacent RGB565 pixels
	 * into the one-DWORD write already used by the kernel upload fallback. */
	for (unsigned y = 0; y < 100; y++)
		for (unsigned x = 0; x < 512; x++)
			pixels[y * WIDTH + x] = 0xf800;
	for (unsigned y = 100; y < 200; y++) {
		volatile uint32_t *row = (volatile uint32_t *)&pixels[y * WIDTH];
		for (unsigned x = 0; x < 256; x++)
			row[x] = 0x07e007e0;
	}
	__sync_synchronize();
	(void)pixels[199 * WIDTH + 511];
	munmap((void *)pixels, BAR_BYTES);
	close(fd);
	puts("BAR2 store test: red=16-bit, green=32-bit");
	return 0;
}
