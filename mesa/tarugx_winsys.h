// SPDX-License-Identifier: MIT
#ifndef TARUGX_WINSYS_H
#define TARUGX_WINSYS_H

#include <stdbool.h>
#include <stdint.h>

#define TARUGX_MAX_BATCH_TRIANGLES 4

struct tarugx_winsys {
    int fd;
    uint32_t submit_syncobj;
    uint32_t gpu_version;
    uint64_t caps;
};

struct tarugx_winsys_surface {
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size;
    uint32_t depth_offset;
    bool depth;
};

struct tarugx_winsys_triangle {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    uint32_t color;
    uint32_t gradient_r;
    uint32_t gradient_g;
    uint32_t gradient_b;
    uint32_t depth_flags;
    uint16_t depth;
    int32_t gradient_z_x;
    int32_t gradient_z_y;
};

bool tarugx_winsys_open(struct tarugx_winsys *winsys, int fd);
void tarugx_winsys_close(struct tarugx_winsys *winsys);
bool tarugx_winsys_has_cap(const struct tarugx_winsys *winsys,
                           uint64_t capability);
bool tarugx_winsys_supports_triangle_interpolation(
    const struct tarugx_winsys *winsys);
bool tarugx_winsys_supports_depth(const struct tarugx_winsys *winsys);
bool tarugx_winsys_surface_create(struct tarugx_winsys *winsys,
                                  uint32_t width, uint32_t height,
                                  struct tarugx_winsys_surface *surface);
bool tarugx_winsys_surface_query(struct tarugx_winsys *winsys,
                                 uint32_t handle,
                                 struct tarugx_winsys_surface *surface);
void tarugx_winsys_surface_destroy(struct tarugx_winsys *winsys,
                                   struct tarugx_winsys_surface *surface);
bool tarugx_winsys_submit_triangle(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    const struct tarugx_winsys_triangle *triangle);
bool tarugx_winsys_submit_triangles(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    const struct tarugx_winsys_triangle *triangles,
    uint32_t count);
bool tarugx_winsys_submit_fill(struct tarugx_winsys *winsys,
                               const struct tarugx_winsys_surface *surface,
                               uint32_t color);
bool tarugx_winsys_submit_color_depth_clear(
    struct tarugx_winsys *winsys,
    const struct tarugx_winsys_surface *surface,
    uint32_t color, uint16_t depth);
bool tarugx_winsys_wait(struct tarugx_winsys *winsys, int64_t timeout_ns);

#endif
