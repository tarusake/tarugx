#include "splash.h"
#include "boot_logo_data.h"

int tgx_splash(struct tgx_io *io, const uint8_t *data, size_t length,
               const uint32_t *palette, size_t colors,
               size_t x, size_t y, size_t w, size_t h)
{
    if (!io || !io->write || !io->read || !data || !palette ||
        !colors || colors > 256 || !w || !h || x >= TGX_WIDTH ||
        y >= TGX_HEIGHT || w > TGX_WIDTH-x || h > TGX_HEIGHT-y) return -1;
    size_t total=w*h, pos=0, pixel=0;
    /* A malformed asset must never cause a partial or out-of-bounds draw. */
    while (pos < length) {
        unsigned tag=data[pos++];
        size_t count=(tag & 127)+1, bytes=tag & 128 ? 1 : count;
        if (count > total-pixel || bytes > length-pos) return -1;
        for (size_t j=0;j<bytes;j++) if (data[pos+j] >= colors) return -1;
        pos+=bytes;pixel+=count;
    }
    if (pixel != total) return -1;
    pos=0;pixel=0;
    while (pos < length) {
        unsigned tag=data[pos++];
        size_t count=(tag & 127)+1;
        for (size_t j=0;j<count;j++,pixel++) {
            unsigned index=data[pos+(tag & 128 ? 0 : j)];
            size_t offset=((y+pixel/w)*TGX_WIDTH+x+pixel%w)*4;
            if (io->write(io->context,offset,palette[index] & 0xffffff)) return -2;
        }
        pos+=tag & 128 ? 1 : count;
    }
    uint32_t drain;
    return io->read(io->context,((y+h-1)*TGX_WIDTH+x+w-1)*4,&drain) ? -2 : 0;
}

int tgx_boot_splash(struct tgx_io *io)
{
    return tgx_splash(io,tgx_logo_data,sizeof(tgx_logo_data),tgx_logo_palette,
        sizeof(tgx_logo_palette)/sizeof(tgx_logo_palette[0]),
        (TGX_WIDTH-TGX_LOGO_WIDTH)/2,(TGX_HEIGHT-TGX_LOGO_HEIGHT)/2,
        TGX_LOGO_WIDTH,TGX_LOGO_HEIGHT);
}
