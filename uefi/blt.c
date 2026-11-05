// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#include "blt.h"

static size_t width(const struct tgx_io *io) { return io->width ? io->width : TGX_WIDTH; }
static size_t height(const struct tgx_io *io) { return io->height ? io->height : TGX_HEIGHT; }
static size_t pitch(const struct tgx_io *io)
{
    return io->pitch ? io->pitch : width(io) * (io->rgb565 ? 2 : 4);
}
static int rectangle(const struct tgx_io *io, size_t x, size_t y, size_t w, size_t h)
{
    return x < width(io) && y < height(io) &&
           w <= width(io) - x && h <= height(io) - y;
}
static uint16_t to_rgb565(uint32_t pixel)
{
    return (uint16_t)(((pixel >> 8) & 0xf800) | ((pixel >> 5) & 0x07e0) |
                      ((pixel >> 3) & 0x001f));
}
static uint32_t from_rgb565(uint16_t pixel)
{
    uint32_t r=(pixel >> 11) & 31, g=(pixel >> 5) & 63, b=pixel & 31;
    return ((r << 3) | (r >> 2)) << 16 |
           ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
}
static size_t video_offset(const struct tgx_io *io, size_t x, size_t y)
{
    return y * pitch(io) + x * (io->rgb565 ? 2 : 4);
}
static int video_read(struct tgx_io *io, size_t x, size_t y, uint32_t *pixel)
{
    size_t offset=video_offset(io,x,y);
    uint32_t word;
    if (io->read(io->context,offset & ~(size_t)3,&word)) return -2;
    *pixel=io->rgb565 ? from_rgb565((uint16_t)(word >> ((offset & 2)*8))) : word & 0xffffff;
    return 0;
}
static int video_write(struct tgx_io *io, size_t x, size_t y, uint32_t pixel)
{
    size_t offset=video_offset(io,x,y);
    if (!io->rgb565)
        return io->write(io->context,offset,pixel & 0xffffff) ? -2 : 0;
    uint32_t word;
    unsigned shift=(unsigned)(offset & 2)*8;
    if (io->read(io->context,offset & ~(size_t)3,&word)) return -2;
    word=(word & ~(0xffffu << shift)) | (uint32_t)to_rgb565(pixel) << shift;
    return io->write(io->context,offset & ~(size_t)3,word) ? -2 : 0;
}

int tgx_blt(struct tgx_io *io, void *buffer, unsigned op, size_t sx, size_t sy,
            size_t dx, size_t dy, size_t w, size_t h, size_t delta)
{
    size_t bx, by, x, y, row, col, offset = 0;
    unsigned char *b = buffer;
    uint32_t pixel = 0, drain;
    if (!io || !io->read || !io->write || op > 3 || !w || !h)
        return -1;
    if ((op == 1 || op == 3) && !rectangle(io, sx, sy, w, h)) return -1;
    if ((op == 0 || op == 2 || op == 3) && !rectangle(io, dx, dy, w, h)) return -1;
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
    if (io->rgb565 && (op == 0 || op == 2)) {
        for (row=0;row<h;row++) {
            col=0;
            if (dx & 1) {
                uint32_t first=pixel;
                if (op==2) {
                    unsigned char *p=b+offset+row*delta;
                    first=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16;
                }
                if (video_write(io,dx,dy+row,first)) return -2;
                col++;
            }
            for (;col+1<w;col+=2) {
                uint32_t first=pixel,second=pixel;
                if (op==2) {
                    unsigned char *p=b+offset+row*delta+col*4;
                    first=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16;
                    second=(uint32_t)p[4]|(uint32_t)p[5]<<8|(uint32_t)p[6]<<16;
                }
                uint32_t packed=to_rgb565(first)|(uint32_t)to_rgb565(second)<<16;
                if (io->write(io->context,video_offset(io,dx+col,dy+row),packed))
                    return -2;
            }
            if (col<w) {
                uint32_t last=pixel;
                if (op==2) {
                    unsigned char *p=b+offset+row*delta+col*4;
                    last=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16;
                }
                if (video_write(io,dx+col,dy+row,last)) return -2;
            }
            /* EFI_PCI_IO_PROTOCOL memory writes are posted.  A firmware can
             * queue a full-screen fill or logo upload much faster than the
             * FPGA's DDR path retires it.  Read back the end of every row so
             * PCIe ordering bounds the outstanding writes instead of waiting
             * until an entire 1080p BLT has been submitted. */
            if (io->read(io->context,
                video_offset(io,dx+w-1,dy+row)&~(size_t)3,&drain))
                return -2;
        }
        return 0;
    }
    for (row = 0; row < h; row++) {
        /* Reverse order is memmove-safe for both vertical and horizontal overlap. */
        y = op == 3 && (dy > sy || (dy == sy && dx > sx)) ? h - 1 - row : row;
        for (col = 0; col < w; col++) {
            x = op == 3 && (dy > sy || (dy == sy && dx > sx)) ? w - 1 - col : col;
            unsigned char *p = (op == 1 || op == 2) ? b + offset + y * delta + x * 4 : b;
            if (op == 1 || op == 3) {
                if (video_read(io,sx+x,sy+y,&pixel)) return -2;
            } else if (op == 2) {
                pixel = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
            }
            if (op == 1) {
                p[0] = pixel; p[1] = pixel >> 8; p[2] = pixel >> 16; p[3] = 0;
            } else if (video_write(io,dx+x,dy+y,pixel)) return -2;
        }
    }
    /* Drain posted BAR2 writes on the same BAR, not by reading BAR0. */
    if (op != 1 && io->read(io->context,
        video_offset(io,dx+w-1,dy+h-1) & ~(size_t)3,&pixel))
        return -2;
    return 0;
}
