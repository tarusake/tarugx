// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#ifndef TARUGX_BLT_H
#define TARUGX_BLT_H
#include <stddef.h>
#include <stdint.h>
#define TGX_WIDTH 640u
#define TGX_HEIGHT 480u
/* Matches GOP's four BLT operations. Callbacks perform exactly one DWORD. */
struct tgx_io {
    void *context;
    int (*read)(void *, size_t, uint32_t *);
    int (*write)(void *, size_t, uint32_t);
};
/* 0 success, -1 invalid geometry/buffer, -2 transport error. */
int tgx_blt(struct tgx_io *, void *, unsigned, size_t, size_t,
            size_t, size_t, size_t, size_t, size_t);
#endif
