// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#include "blt.h"

static int rectangle(size_t x, size_t y, size_t w, size_t h)
{
    return x < TGX_WIDTH && y < TGX_HEIGHT &&
           w <= TGX_WIDTH - x && h <= TGX_HEIGHT - y;
}

int tgx_blt(struct tgx_io *io, void *buffer, unsigned op, size_t sx, size_t sy,
            size_t dx, size_t dy, size_t w, size_t h, size_t delta)
{
    size_t bx, by, x, y, row, col, offset = 0;
    unsigned char *b = buffer;
    uint32_t pixel = 0;
    if (!io || !io->read || !io->write || op > 3 || !w || !h)
        return -1;
    if ((op == 1 || op == 3) && !rectangle(sx, sy, w, h)) return -1;
    if ((op == 0 || op == 2 || op == 3) && !rectangle(dx, dy, w, h)) return -1;
    if (op != 3 && !b) return -1;
    if (op == 1 || op == 2) {
        bx = op == 1 ? dx : sx;
        by = op == 1 ? dy : sy;
        if (!delta) delta = w * 4;
        if (delta % 4 || bx > SIZE_MAX / 4 || w > SIZE_MAX / 4 - bx ||
            (bx + w) * 4 > delta || by > SIZE_MAX - (h - 1) ||
            by + h - 1 > (SIZE_MAX - (bx + w) * 4) / delta) return -1;
        offset = by * delta + bx * 4;
        if ((uintptr_t)b > UINTPTR_MAX - (offset + (h - 1) * delta + w * 4))
            return -1;
    }
    if (op == 0)
        pixel = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16;
    for (row = 0; row < h; row++) {
        /* Reverse order is memmove-safe for both vertical and horizontal overlap. */
        y = op == 3 && (dy > sy || (dy == sy && dx > sx)) ? h - 1 - row : row;
        for (col = 0; col < w; col++) {
            x = op == 3 && (dy > sy || (dy == sy && dx > sx)) ? w - 1 - col : col;
            unsigned char *p = (op == 1 || op == 2) ? b + offset + y * delta + x * 4 : b;
            if (op == 1 || op == 3) {
                if (io->read(io->context, ((sy+y)*TGX_WIDTH+sx+x)*4, &pixel)) return -2;
            } else if (op == 2) {
                pixel = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
            }
            if (op == 1) {
                p[0] = pixel; p[1] = pixel >> 8; p[2] = pixel >> 16; p[3] = 0;
            } else if (io->write(io->context, ((dy+y)*TGX_WIDTH+dx+x)*4, pixel & 0xffffff)) {
                return -2;
            }
        }
    }
    /* Drain posted BAR2 writes on the same BAR, not by reading BAR0. */
    if (op != 1 && io->read(io->context, ((dy+h-1)*TGX_WIDTH+dx+w-1)*4, &pixel))
        return -2;
    return 0;
}
