// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "splash.h"
#include "boot_logo_data.h"

static uint32_t fb[TGX_WIDTH*TGX_HEIGHT];
static unsigned writes, reads;
static int fail_write, fail_read;
static size_t last_write;
static int write_pixel(void *ctx, size_t offset, uint32_t value)
{
    (void)ctx;
    assert(offset%4==0 && offset<sizeof(fb));
    if (fail_write) return 1;
    fb[offset/4]=value;writes++;last_write=offset;return 0;
}
static int read_pixel(void *ctx, size_t offset, uint32_t *value)
{
    (void)ctx;
    assert(offset==last_write && offset<sizeof(fb));
    reads++;if (fail_read) return 1;
    *value=fb[offset/4];return 0;
}
int main(void)
{
    struct tgx_io io={.read=read_pixel,.write=write_pixel,
                      .width=TGX_WIDTH,.height=TGX_HEIGHT,.pitch=TGX_WIDTH*4};
    memset(fb,0x55,sizeof(fb));
    assert(tgx_boot_splash(&io)==0);
    assert(writes==TGX_LOGO_WIDTH*TGX_LOGO_HEIGHT && reads==1);
    uint32_t hash=2166136261u;
    size_t left=(TGX_WIDTH-TGX_LOGO_WIDTH)/2, top=(TGX_HEIGHT-TGX_LOGO_HEIGHT)/2;
    for (size_t y=0;y<TGX_HEIGHT;y++) for (size_t x=0;x<TGX_WIDTH;x++) {
        uint32_t c=fb[y*TGX_WIDTH+x];
        if (x>=left && x<left+TGX_LOGO_WIDTH && y>=top && y<top+TGX_LOGO_HEIGHT) {
            for (int shift=16;shift>=0;shift-=8) hash=(hash^((c>>shift)&255))*16777619u;
        } else assert(c==0x55555555);
    }
    /* Independent digest of the converter's RGB pixels, including pink mouth. */
    assert(hash==0x715fda92u && hash==TGX_LOGO_RGB_FNV);
    uint32_t palette[]={0,0xff3388};
    uint8_t good[]={0x82,1,0,0}; /* repeated pink x3, literal black x1 */
    writes=reads=0;
    assert(tgx_splash(&io,good,sizeof(good),palette,2,638,478,2,2)==0);
    assert(writes==4 && reads==1 && fb[478*640+638]==0xff3388 && fb[479*640+639]==0);
    uint8_t truncated[]={0x80}, bad_index[]={0x80,2}, overflow[]={0x84,0};
    writes=reads=0;
    assert(tgx_splash(&io,truncated,1,palette,2,0,0,1,1)==-1);
    assert(tgx_splash(&io,bad_index,2,palette,2,0,0,1,1)==-1);
    assert(tgx_splash(&io,overflow,2,palette,2,0,0,2,2)==-1);
    assert(tgx_splash(&io,good,2,palette,2,0,0,2,2)==-1); /* missing pixel */
    assert(tgx_splash(&io,good,3,palette,2,0,0,2,2)==-1); /* truncated literal */
    assert(tgx_splash(&io,good,4,palette,2,639,479,2,2)==-1);
    assert(tgx_splash(&io,good,4,palette,2,0,0,SIZE_MAX,1)==-1);
    assert(tgx_splash(NULL,good,4,palette,2,0,0,2,2)==-1);
    assert(!writes && !reads);
    fail_write=1;
    assert(tgx_boot_splash(&io)==-2 && !reads);
    fail_write=0;fail_read=1;
    assert(tgx_boot_splash(&io)==-2 && reads==1);
    puts("Splash: exact RGB digest, centered bounds, packets, malformed data and I/O errors passed");
}
