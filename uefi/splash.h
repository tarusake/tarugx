#ifndef TARUGX_SPLASH_H
#define TARUGX_SPLASH_H
#include "blt.h"
/* Validate the entire stream before writing. 0 success, -1 bad image, -2 I/O. */
int tgx_splash(struct tgx_io *, const uint8_t *, size_t, const uint32_t *,
               size_t, size_t, size_t, size_t, size_t);
int tgx_boot_splash(struct tgx_io *);
#endif
