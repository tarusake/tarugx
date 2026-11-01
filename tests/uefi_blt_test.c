// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#include "blt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t fb[TGX_WIDTH*TGX_HEIGHT], expected[TGX_WIDTH*TGX_HEIGHT];
static int fail, reads, writes;
static int rd(void *p, size_t offset, uint32_t *value)
{
    (void)p; assert(offset%4==0 && offset<sizeof(fb)); reads++;
    if (fail) return 1;
    *value=fb[offset/4]; return 0;
}
static int wr(void *p, size_t offset, uint32_t value)
{
    (void)p; assert(offset%4==0 && offset<sizeof(fb)); writes++;
    if (fail) return 1;
    fb[offset/4]=value; return 0;
}
int main(void)
{
    struct tgx_io io={NULL,rd,wr};
    uint32_t color=0xff123456, buffer[64];
    assert(!tgx_blt(&io,&color,0,0,0,0,0,640,480,0));
    assert(writes==640*480 && reads==1);
    for (unsigned i=0;i<640*480;i++) assert(fb[i]==0x123456);
    /* Nonzero buffer origins and padded pitch; reserved byte is ignored. */
    for (unsigned i=0;i<64;i++) buffer[i]=0xab000000+i;
    assert(!tgx_blt(&io,buffer,2,2,1,639,479,1,1,32));
    assert(fb[640*480-1]==10);
    memset(buffer,0xa5,sizeof(buffer));
    assert(!tgx_blt(&io,buffer,1,639,479,3,2,1,1,32));
    assert(buffer[19]==10 && buffer[18]==0xa5a5a5a5 && buffer[20]==0xa5a5a5a5);
    /* Four overlap directions compared against an independent snapshot. */
    for (unsigned direction=0;direction<4;direction++) {
        size_t sx=10,sy=10,dx=10,dy=10;
        if(direction==0) dx++;
        if(direction==1) dx--;
        if(direction==2) dy++;
        if(direction==3) dy--;
        for(unsigned i=0;i<640*480;i++) fb[i]=i;
        memcpy(expected,fb,sizeof(fb));
        for(size_t y=0;y<8;y++)for(size_t x=0;x<8;x++)
            expected[(dy+y)*640+dx+x]=fb[(sy+y)*640+sx+x];
        assert(!tgx_blt(&io,NULL,3,sx,sy,dx,dy,8,8,0));
        assert(!memcmp(fb,expected,sizeof(fb)));
    }
    writes=reads=0;
    assert(tgx_blt(&io,&color,0,0,0,639,479,2,1,0)==-1);
    assert(tgx_blt(&io,NULL,0,0,0,0,0,1,1,0)==-1);
    assert(tgx_blt(&io,buffer,2,1,0,0,0,8,1,32)==-1);
    assert(tgx_blt(&io,buffer,2,0,SIZE_MAX,0,0,1,2,32)==-1);
    assert(tgx_blt(&io,buffer,2,SIZE_MAX,0,0,0,1,1,4)==-1);
    assert(tgx_blt(&io,buffer,1,0,0,0,0,1,1,3)==-1);
    assert(tgx_blt(&io,buffer,4,0,0,0,0,1,1,0)==-1);
    assert(tgx_blt(&io,buffer,0,0,0,0,0,0,1,0)==-1);
    assert(!writes && !reads);
    fail=1;
    assert(tgx_blt(&io,&color,0,0,0,0,0,1,1,0)==-2);
    assert(tgx_blt(&io,buffer,1,0,0,0,0,1,1,0)==-2);
    puts("GOP BLT: fill, pitch, readback, overlap, overflow and I/O errors passed");
}
