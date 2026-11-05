// SPDX-License-Identifier: MIT
#ifndef TARUGX_CONTEXT_H
#define TARUGX_CONTEXT_H

#include <stdbool.h>

struct pipe_screen;

bool tarugx_context_enable(struct pipe_screen *screen, int fd);

#endif
