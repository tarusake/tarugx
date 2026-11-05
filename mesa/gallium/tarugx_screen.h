// SPDX-License-Identifier: MIT
#ifndef TARUGX_SCREEN_H
#define TARUGX_SCREEN_H

struct pipe_screen;
struct pipe_screen_config;

struct pipe_screen *
tarugx_screen_create(int fd, const struct pipe_screen_config *config);

#endif
