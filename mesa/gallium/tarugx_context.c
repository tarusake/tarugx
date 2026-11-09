// SPDX-License-Identifier: MIT
#include "tarugx_context.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "draw/draw_vbuf.h"
#include "frontend/winsys_handle.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "softpipe/sp_context.h"
#include "softpipe/sp_tile_cache.h"

#include "tarugx_drm.h"
#include "tarugx_winsys.h"

#define TARUGX_MAX_SCREENS 8
#define TARUGX_MAX_CONTEXTS 16
#define TARUGX_VERTEX_FLOATS 12
#define TARUGX_CLIP_MAX_VERTICES 8
#define TARUGX_CLIP_MAX_TRIANGLES (TARUGX_CLIP_MAX_VERTICES - 2)
#define TARUGX_MAX_INFLIGHT_BATCHES 32

struct tarugx_screen_state {
    struct pipe_screen *screen;
    int fd;
    struct pipe_context *(*context_create)(struct pipe_screen *, void *,
                                           unsigned);
    void (*destroy)(struct pipe_screen *);
};

struct tarugx_context_state {
    struct pipe_context *context;
    struct softpipe_context *softpipe;
    struct vbuf_render *backend;
    struct tarugx_winsys winsys;
    bool disable_acceleration;
    enum mesa_prim primitive;
    void (*set_primitive)(struct vbuf_render *, enum mesa_prim);
    void (*draw_arrays)(struct vbuf_render *, unsigned, unsigned);
    void (*draw_elements)(struct vbuf_render *, const uint16_t *, unsigned);
    void (*clear)(struct pipe_context *, unsigned,
                  const struct pipe_scissor_state *,
                  const union pipe_color_union *, double, unsigned);
    void (*destroy)(struct pipe_context *);
    struct tarugx_winsys_surface texture;
};

static pthread_mutex_t tarugx_state_lock = PTHREAD_MUTEX_INITIALIZER;
static struct tarugx_screen_state tarugx_screens[TARUGX_MAX_SCREENS];
static struct tarugx_context_state tarugx_contexts[TARUGX_MAX_CONTEXTS];

static struct tarugx_screen_state *
tarugx_find_screen(struct pipe_screen *screen)
{
    unsigned i;

    for (i = 0; i < TARUGX_MAX_SCREENS; i++)
        if (tarugx_screens[i].screen == screen)
            return &tarugx_screens[i];
    return NULL;
}

static struct tarugx_context_state *
tarugx_find_context(struct pipe_context *context, struct vbuf_render *backend)
{
    unsigned i;

    for (i = 0; i < TARUGX_MAX_CONTEXTS; i++) {
        if ((context && tarugx_contexts[i].context == context) ||
            (backend && tarugx_contexts[i].backend == backend))
            return &tarugx_contexts[i];
    }
    return NULL;
}

static void
tarugx_flush_softpipe_render_cache(struct softpipe_context *softpipe)
{
    unsigned i;

    for (i = 0; i < softpipe->framebuffer.nr_cbufs; i++)
        if (softpipe->cbuf_cache[i])
            sp_flush_tile_cache(softpipe->cbuf_cache[i]);
    if (softpipe->zsbuf_cache)
        sp_flush_tile_cache(softpipe->zsbuf_cache);
    softpipe->dirty_render_cache = false;
}

static bool
tarugx_target_surface(struct tarugx_context_state *state,
                      struct tarugx_winsys_surface *surface)
{
    struct pipe_framebuffer_state *framebuffer = &state->softpipe->framebuffer;
    struct pipe_surface *target;
    struct winsys_handle handle = { .type = WINSYS_HANDLE_TYPE_KMS };

    if (framebuffer->nr_cbufs != 1 || !framebuffer->cbufs[0])
        return false;
    target = framebuffer->cbufs[0];
    if (target->format != PIPE_FORMAT_B5G6R5_UNORM ||
        target->width != 1920 || target->height != 1080 ||
        !state->context->screen->resource_get_handle(
            state->context->screen, state->context, target->texture,
            &handle, PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE))
        return false;
    if (!tarugx_winsys_surface_query(&state->winsys, handle.handle,
                                     surface))
        return false;
    return surface->width == target->width &&
           surface->height == target->height &&
           surface->pitch == handle.stride &&
           handle.stride == target->width * 2;
}

static bool
tarugx_draw_state_supported(struct tarugx_context_state *state,
                            const struct tarugx_winsys_surface *surface,
                            bool textured)
{
    const struct softpipe_context *softpipe = state->softpipe;
    const struct pipe_rasterizer_state *rasterizer = softpipe->rasterizer;
    const struct pipe_blend_state *blend = softpipe->blend;
    const struct pipe_depth_stencil_alpha_state *depth =
        softpipe->depth_stencil;

    return rasterizer && blend && depth &&
           !rasterizer->rasterizer_discard &&
           !rasterizer->poly_smooth && !rasterizer->multisample &&
           rasterizer->fill_front == PIPE_POLYGON_MODE_FILL &&
           rasterizer->fill_back == PIPE_POLYGON_MODE_FILL &&
           !blend->logicop_enable && !blend->dither &&
           !blend->rt[0].blend_enable &&
           blend->rt[0].colormask == PIPE_MASK_RGBA &&
           (!depth->depth_enabled ||
            (surface->depth && softpipe->framebuffer.zsbuf &&
             softpipe->framebuffer.zsbuf->format == PIPE_FORMAT_Z16_UNORM &&
             softpipe->framebuffer.zsbuf->width == surface->width &&
             softpipe->framebuffer.zsbuf->height == surface->height)) &&
           !depth->stencil[0].enabled && !depth->alpha_enabled &&
           softpipe->vertex_info.num_attribs == (textured ? 3 : 2) &&
           softpipe->vertex_info.attrib[0].emit == EMIT_4F &&
           softpipe->vertex_info.attrib[1].emit == EMIT_4F &&
           (!textured || softpipe->vertex_info.attrib[2].emit == EMIT_4F);
}

static bool
tarugx_texture_state(struct tarugx_context_state *state,
                     struct tarugx_winsys_surface **texture,
                     float *u_scale, float *v_scale)
{
    struct softpipe_context *softpipe = state->softpipe;
    struct pipe_sampler_view *view;
    const struct pipe_sampler_state *sampler;
    struct pipe_resource *resource;
    struct pipe_transfer *transfer = NULL;
    struct pipe_box box;
    const void *pixels;
    bool uploaded;

    *texture = NULL;
    *u_scale = 1.0f;
    *v_scale = 1.0f;
    if (!softpipe->num_sampler_views[PIPE_SHADER_FRAGMENT] &&
        !softpipe->num_samplers[PIPE_SHADER_FRAGMENT])
        return true;
    if (softpipe->num_sampler_views[PIPE_SHADER_FRAGMENT] != 1 ||
        softpipe->num_samplers[PIPE_SHADER_FRAGMENT] != 1 ||
        !tarugx_winsys_supports_texture(&state->winsys))
        return false;
    view = softpipe->sampler_views[PIPE_SHADER_FRAGMENT][0];
    sampler = softpipe->samplers[PIPE_SHADER_FRAGMENT][0];
    if (!view || !sampler || view->format != PIPE_FORMAT_B5G6R5_UNORM ||
        view->target != PIPE_TEXTURE_2D || view->u.tex.first_level ||
        view->u.tex.last_level || view->u.tex.first_layer ||
        view->u.tex.last_layer ||
        sampler->min_img_filter != PIPE_TEX_FILTER_NEAREST ||
        sampler->mag_img_filter != PIPE_TEX_FILTER_NEAREST ||
        sampler->min_mip_filter != PIPE_TEX_MIPFILTER_NONE ||
        sampler->wrap_s != PIPE_TEX_WRAP_CLAMP_TO_EDGE ||
        sampler->wrap_t != PIPE_TEX_WRAP_CLAMP_TO_EDGE)
        return false;
    resource = view->texture;
    if (!resource || !resource->width0 || !resource->height0 ||
        (resource->width0 & 1) || resource->depth0 != 1 ||
        resource->array_size != 1 || resource->last_level)
        return false;
    if (!state->texture.handle || state->texture.width != resource->width0 ||
        state->texture.height != resource->height0) {
        tarugx_winsys_surface_destroy(&state->winsys, &state->texture);
        if (!tarugx_winsys_texture_create(&state->winsys, resource->width0,
                                          resource->height0,
                                          &state->texture))
            return false;
    }
    box = (struct pipe_box) {
        .width = resource->width0,
        .height = resource->height0,
        .depth = 1,
    };
    pixels = state->context->texture_map(state->context, resource, 0,
                                         PIPE_MAP_READ, &box, &transfer);
    if (!pixels || !transfer)
        return false;
    uploaded = tarugx_winsys_upload_texture(&state->winsys, pixels,
                                             transfer->stride,
                                             &state->texture);
    state->context->texture_unmap(state->context, transfer);
    if (!uploaded)
        return false;
    if (!sampler->unnormalized_coords) {
        *u_scale = resource->width0;
        *v_scale = resource->height0;
    }
    *texture = &state->texture;
    return true;
}

static bool
tarugx_depth_func(unsigned pipe_func, uint32_t *hardware_func)
{
    switch (pipe_func) {
    case PIPE_FUNC_NEVER:    *hardware_func = 0; return true;
    case PIPE_FUNC_LESS:     *hardware_func = 1; return true;
    case PIPE_FUNC_EQUAL:    *hardware_func = 2; return true;
    case PIPE_FUNC_LEQUAL:   *hardware_func = 3; return true;
    case PIPE_FUNC_GREATER:  *hardware_func = 4; return true;
    case PIPE_FUNC_NOTEQUAL: *hardware_func = 5; return true;
    case PIPE_FUNC_GEQUAL:   *hardware_func = 6; return true;
    case PIPE_FUNC_ALWAYS:   *hardware_func = 7; return true;
    default: return false;
    }
}

static bool
tarugx_colors_equal(const float *a, const float *b)
{
    unsigned i;

    for (i = 0; i < 3; i++)
        if (fabsf(a[i] - b[i]) > 0.00001f)
            return false;
    return true;
}

static bool
tarugx_fixed_16_8(float value, int32_t *fixed)
{
    long converted;

    if (!isfinite(value) || value < -32768.0f ||
        value > 8388607.0f / 256.0f)
        return false;
    converted = lrintf(value * 256.0f);
    if (converted < -8388608L || converted > 8388607L)
        return false;
    *fixed = (int32_t)converted;
    return true;
}

static uint32_t
tarugx_rgb888(const float *rgba)
{
    uint32_t red = (uint32_t)lrintf(fminf(fmaxf(rgba[0], 0.0f), 1.0f) *
                                    255.0f);
    uint32_t green = (uint32_t)lrintf(fminf(fmaxf(rgba[1], 0.0f), 1.0f) *
                                      255.0f);
    uint32_t blue = (uint32_t)lrintf(fminf(fmaxf(rgba[2], 0.0f), 1.0f) *
                                     255.0f);

    return red << 16 | green << 8 | blue;
}

static bool
tarugx_pack_gradient(double dx, double dy, uint32_t *packed)
{
    long fixed_x;
    long fixed_y;

    if (!isfinite(dx) || !isfinite(dy) ||
        dx < -256.0 || dx > 32767.0 / 128.0 ||
        dy < -256.0 || dy > 32767.0 / 128.0)
        return false;
    fixed_x = lrint(dx * 128.0);
    fixed_y = lrint(dy * 128.0);
    if (fixed_x < INT16_MIN || fixed_x > INT16_MAX ||
        fixed_y < INT16_MIN || fixed_y > INT16_MAX)
        return false;
    *packed = (uint16_t)fixed_x | (uint32_t)(uint16_t)fixed_y << 16;
    return true;
}

static bool
tarugx_pack_depth_gradient(double value, int32_t *packed)
{
    long fixed;

    if (!isfinite(value) || value < -32768.0 ||
        value > 8388607.0 / 256.0)
        return false;
    fixed = lrint(value * 256.0);
    if (fixed < -8388608L || fixed > 8388607L)
        return false;
    *packed = (int32_t)fixed;
    return true;
}

static bool
tarugx_interpolated_channel(const float *v0, const float *v1,
                            const float *v2, unsigned channel,
                            double sample_x, double sample_y,
                            double determinant, uint32_t *base,
                            uint32_t *gradient)
{
    double x0 = v0[0], y0 = v0[1];
    double x1 = v1[0], y1 = v1[1];
    double x2 = v2[0], y2 = v2[1];
    double c0 = v0[4 + channel] * 255.0;
    double c1 = v1[4 + channel] * 255.0;
    double c2 = v2[4 + channel] * 255.0;
    double dx = ((c1 - c0) * (y2 - y0) -
                 (c2 - c0) * (y1 - y0)) / determinant;
    double dy = ((x1 - x0) * (c2 - c0) -
                 (x2 - x0) * (c1 - c0)) / determinant;
    double value = c0 + dx * (sample_x - x0) +
                   dy * (sample_y - y0);
    long rounded;

    if (!isfinite(value) || value < 0.0 || value > 255.0 ||
        !tarugx_pack_gradient(dx, dy, gradient))
        return false;
    rounded = lrint(value);
    if (rounded < 0 || rounded > 255)
        return false;
    *base = (uint32_t)rounded;
    return true;
}

static bool
tarugx_prepare_triangle(struct tarugx_context_state *state,
                        const struct tarugx_winsys_surface *surface,
                        bool textured, float u_scale, float v_scale,
                        const float *v0, const float *v1, const float *v2,
                        struct tarugx_winsys_triangle *triangle)
{
    int32_t x0, y0, x1, y1, x2, y2;
    bool flat = tarugx_colors_equal(v0 + 4, v1 + 4) &&
                tarugx_colors_equal(v0 + 4, v2 + 4);
    int32_t minimum_x;
    int32_t minimum_y;
    int32_t maximum_x;
    int32_t maximum_y;
    double sample_x;
    double sample_y;
    double determinant;
    uint32_t base[3];
    uint32_t gradient[3];
    const struct pipe_depth_stencil_alpha_state *depth_state =
        state->softpipe->depth_stencil;
    bool depth_enabled = depth_state->depth_enabled;
    uint32_t depth_func = 0;
    unsigned channel;

    for (channel = 0; channel < 3; channel++)
        if (!isfinite(v0[4 + channel]) ||
            !isfinite(v1[4 + channel]) ||
            !isfinite(v2[4 + channel]))
            return false;
    if (!tarugx_fixed_16_8(v0[0], &x0) ||
        !tarugx_fixed_16_8(v0[1], &y0) ||
        !tarugx_fixed_16_8(v1[0], &x1) ||
        !tarugx_fixed_16_8(v1[1], &y1) ||
        !tarugx_fixed_16_8(v2[0], &x2) ||
        !tarugx_fixed_16_8(v2[1], &y2))
        return false;
    *triangle = (struct tarugx_winsys_triangle) {
        .x0 = x0,
        .y0 = y0,
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .color = tarugx_rgb888(v0 + 4),
    };
    /* A flat color still needs the interpolation setup below when UVs are
     * present.  Returning here would submit a textured command with all UV
     * fields left at zero, sampling texel (0, 0) for the whole triangle. */
    if (flat && !depth_enabled && !textured)
        return true;
    if ((!flat &&
         (!tarugx_winsys_supports_triangle_interpolation(&state->winsys) ||
          !isfinite(v0[3]) || !isfinite(v1[3]) || !isfinite(v2[3]) ||
          fabsf(v0[3] - v1[3]) > 0.00001f ||
          fabsf(v0[3] - v2[3]) > 0.00001f)) ||
        (depth_enabled &&
         (!surface->depth ||
          !tarugx_winsys_supports_depth(&state->winsys) ||
          !tarugx_depth_func(depth_state->depth_func, &depth_func))))
        return false;

    /* Fit colors against the same 16.8 vertex coordinates sent to hardware,
     * then evaluate the initial color at the first clipped pixel center. */
    minimum_x = MIN2(MIN2(x0, x1), x2);
    minimum_y = MIN2(MIN2(y0, y1), y2);
    maximum_x = MAX2(MAX2(x0, x1), x2);
    maximum_y = MAX2(MAX2(y0, y1), y2);
    sample_x = (minimum_x < 0 ? 0 : minimum_x >> 8) + 0.5;
    sample_y = (minimum_y < 0 ? 0 : minimum_y >> 8) + 0.5;
    if (sample_x >= surface->width || sample_y >= surface->height)
        return false;
    v0 = (const float[12]) { x0 / 256.0f, y0 / 256.0f,
                             v0[2], v0[3], v0[4], v0[5], v0[6], v0[7],
                             v0[8], v0[9], v0[10], v0[11] };
    v1 = (const float[12]) { x1 / 256.0f, y1 / 256.0f,
                             v1[2], v1[3], v1[4], v1[5], v1[6], v1[7],
                             v1[8], v1[9], v1[10], v1[11] };
    v2 = (const float[12]) { x2 / 256.0f, y2 / 256.0f,
                             v2[2], v2[3], v2[4], v2[5], v2[6], v2[7],
                             v2[8], v2[9], v2[10], v2[11] };
    determinant = ((double)v1[0] - v0[0]) * ((double)v2[1] - v0[1]) -
                  ((double)v2[0] - v0[0]) * ((double)v1[1] - v0[1]);
    if (!isfinite(determinant) || fabs(determinant) < 1.0 / 65536.0)
        return false;
    if (!flat) {
        for (channel = 0; channel < 3; channel++)
            if (!tarugx_interpolated_channel(v0, v1, v2, channel,
                                             sample_x, sample_y, determinant,
                                             &base[channel], &gradient[channel]))
                return false;
        triangle->color = base[0] << 16 | base[1] << 8 | base[2];
        triangle->gradient_r = gradient[0];
        triangle->gradient_g = gradient[1];
        triangle->gradient_b = gradient[2];
    }
    if (depth_enabled) {
        double z0 = v0[2] * 65535.0;
        double z1 = v1[2] * 65535.0;
        double z2 = v2[2] * 65535.0;
        double dx = ((z1 - z0) * (v2[1] - v0[1]) -
                     (z2 - z0) * (v1[1] - v0[1])) / determinant;
        double dy = ((v1[0] - v0[0]) * (z2 - z0) -
                     (v2[0] - v0[0]) * (z1 - z0)) / determinant;
        double value = z0 + dx * (sample_x - v0[0]) +
                       dy * (sample_y - v0[1]);
        long rounded;

        if (!isfinite(z0) || !isfinite(z1) || !isfinite(z2) ||
            z0 < 0.0 || z0 > 65535.0 || z1 < 0.0 || z1 > 65535.0 ||
            z2 < 0.0 || z2 > 65535.0 || !isfinite(value) ||
            value < 0.0 || value > 65535.0 ||
            !tarugx_pack_depth_gradient(dx, &triangle->gradient_z_x) ||
            !tarugx_pack_depth_gradient(dy, &triangle->gradient_z_y))
            return false;
        rounded = lrint(value);
        if (rounded < 0 || rounded > UINT16_MAX)
            return false;
        triangle->depth = (uint16_t)rounded;
        triangle->depth_flags = DRM_TARUGX_DEPTH_ENABLE |
            (depth_state->depth_writemask ? DRM_TARUGX_DEPTH_WRITE : 0) |
            (depth_func << DRM_TARUGX_DEPTH_FUNC_SHIFT);
    }
    if (textured) {
        double u0 = v0[8] * u_scale;
        double u1 = v1[8] * u_scale;
        double u2 = v2[8] * u_scale;
        double v0_tex = v0[9] * v_scale;
        double v1_tex = v1[9] * v_scale;
        double v2_tex = v2[9] * v_scale;
        double du_dx = ((u1 - u0) * (v2[1] - v0[1]) -
                        (u2 - u0) * (v1[1] - v0[1])) / determinant;
        double du_dy = ((v1[0] - v0[0]) * (u2 - u0) -
                        (v2[0] - v0[0]) * (u1 - u0)) / determinant;
        double dv_dx = ((v1_tex - v0_tex) * (v2[1] - v0[1]) -
                        (v2_tex - v0_tex) * (v1[1] - v0[1])) /
                       determinant;
        double dv_dy = ((v1[0] - v0[0]) * (v2_tex - v0_tex) -
                        (v2[0] - v0[0]) * (v1_tex - v0_tex)) /
                       determinant;
        double quantized_du_dx;
        double quantized_du_dy;
        double quantized_dv_dx;
        double quantized_dv_dy;
        double center_x;
        double center_y;
        double center_u;
        double center_v;
        double initial_u;
        double initial_v;
        int32_t start_x = minimum_x < 0 ? 0 : minimum_x >> 8;
        int32_t start_y = minimum_y < 0 ? 0 : minimum_y >> 8;
        int32_t end_x = MIN2((int32_t)surface->width,
                             (maximum_x + 255) >> 8);
        int32_t end_y = MIN2((int32_t)surface->height,
                             (maximum_y + 255) >> 8);

        if (start_x >= end_x || start_y >= end_y ||
            !tarugx_pack_depth_gradient(du_dx, &triangle->gradient_u_x) ||
            !tarugx_pack_depth_gradient(du_dy, &triangle->gradient_u_y) ||
            !tarugx_pack_depth_gradient(dv_dx, &triangle->gradient_v_x) ||
            !tarugx_pack_depth_gradient(dv_dy, &triangle->gradient_v_y))
            return false;
        quantized_du_dx = triangle->gradient_u_x / 256.0;
        quantized_du_dy = triangle->gradient_u_y / 256.0;
        quantized_dv_dx = triangle->gradient_v_x / 256.0;
        quantized_dv_dy = triangle->gradient_v_y / 256.0;
        center_x = ((double)start_x + end_x - 1.0) * 0.5;
        center_y = ((double)start_y + end_y - 1.0) * 0.5;
        center_u = u0 + du_dx * (center_x + 0.5 - v0[0]) +
                   du_dy * (center_y + 0.5 - v0[1]);
        center_v = v0_tex + dv_dx * (center_x + 0.5 - v0[0]) +
                   dv_dy * (center_y + 0.5 - v0[1]);
        /* The hardware ABI stores UV gradients as signed 16.8.  Anchor the
         * quantized plane at the bounding-box centre so rounding error is
         * shared across both sides instead of accumulating from one edge. */
        initial_u = center_u - quantized_du_dx * (center_x - start_x) -
                    quantized_du_dy * (center_y - start_y);
        initial_v = center_v - quantized_dv_dx * (center_x - start_x) -
                    quantized_dv_dy * (center_y - start_y);
        if (!tarugx_fixed_16_8(initial_u, &triangle->u) ||
            !tarugx_fixed_16_8(initial_v, &triangle->v))
            return false;
    }
    return true;
}

static bool
tarugx_triangle_culled(const struct tarugx_context_state *state,
                       const struct tarugx_winsys_triangle *triangle)
{
    const struct pipe_rasterizer_state *rasterizer =
        state->softpipe->rasterizer;
    int64_t x10 = (int64_t)triangle->x1 - triangle->x0;
    int64_t y10 = (int64_t)triangle->y1 - triangle->y0;
    int64_t x20 = (int64_t)triangle->x2 - triangle->x0;
    int64_t y20 = (int64_t)triangle->y2 - triangle->y0;
    int64_t area = x10 * y20 - x20 * y10;
    bool front_facing;

    /* Degenerate triangles cover no samples and need no FPGA command. */
    if (!area)
        return true;
    /* vbuf vertices are already in framebuffer coordinates, whose Y axis is
     * inverted relative to OpenGL window coordinates. */
    front_facing = (area < 0) == rasterizer->front_ccw;
    return rasterizer->cull_face &
           (front_facing ? PIPE_FACE_FRONT : PIPE_FACE_BACK);
}

static unsigned
tarugx_clip_edge(const float input[][TARUGX_VERTEX_FLOATS],
                 unsigned input_count,
                 float output[][TARUGX_VERTEX_FLOATS], unsigned axis,
                 float boundary, bool keep_greater)
{
    const float *previous;
    bool previous_inside;
    unsigned output_count = 0;
    unsigned i, component;

    if (!input_count)
        return 0;
    previous = input[input_count - 1];
    previous_inside = keep_greater ? previous[axis] >= boundary :
                                     previous[axis] <= boundary;
    for (i = 0; i < input_count; i++) {
        const float *current = input[i];
        bool current_inside = keep_greater ? current[axis] >= boundary :
                                             current[axis] <= boundary;

        if (current_inside != previous_inside) {
            double denominator = (double)current[axis] - previous[axis];
            double amount = ((double)boundary - previous[axis]) /
                            denominator;

            if (!isfinite(amount) || output_count >= TARUGX_CLIP_MAX_VERTICES)
                return 0;
            for (component = 0; component < TARUGX_VERTEX_FLOATS;
                 component++)
                output[output_count][component] =
                    previous[component] +
                    (current[component] - previous[component]) * amount;
            output[output_count][axis] = boundary;
            output_count++;
        }
        if (current_inside) {
            if (output_count >= TARUGX_CLIP_MAX_VERTICES)
                return 0;
            memcpy(output[output_count], current,
                   sizeof(output[output_count]));
            output_count++;
        }
        previous = current;
        previous_inside = current_inside;
    }
    return output_count;
}

static unsigned
tarugx_clip_triangle(const struct tarugx_context_state *state,
                     const float *v0, const float *v1, const float *v2,
                     float output[][TARUGX_VERTEX_FLOATS])
{
    const struct pipe_rasterizer_state *rasterizer =
        state->softpipe->rasterizer;
    const struct pipe_scissor_state *clip = &state->softpipe->cliprect[0];
    float first[TARUGX_CLIP_MAX_VERTICES][TARUGX_VERTEX_FLOATS] = {{0}};
    float second[TARUGX_CLIP_MAX_VERTICES][TARUGX_VERTEX_FLOATS] = {{0}};
    unsigned count = 3;

    memcpy(first[0], v0, sizeof(first[0]));
    memcpy(first[1], v1, sizeof(first[1]));
    memcpy(first[2], v2, sizeof(first[2]));
    if (!rasterizer->scissor) {
        memcpy(output, first, count * sizeof(first[0]));
        return count;
    }
    if (clip->minx >= clip->maxx || clip->miny >= clip->maxy)
        return 0;
    count = tarugx_clip_edge(first, count, second, 0, clip->minx, true);
    count = tarugx_clip_edge(second, count, first, 0, clip->maxx, false);
    count = tarugx_clip_edge(first, count, second, 1, clip->miny, true);
    count = tarugx_clip_edge(second, count, output, 1, clip->maxy, false);
    return count;
}

static bool
tarugx_draw_vertices(struct tarugx_context_state *state,
                     const uint16_t *indices, unsigned start, unsigned count)
{
    const float *vertices;
    struct tarugx_winsys_surface surface;
    struct tarugx_winsys_surface *texture;
    struct tarugx_winsys_triangle *triangles;
    float u_scale;
    float v_scale;
    unsigned triangle_count;
    unsigned prepared_count = 0;
    unsigned maximum_triangles;
    unsigned inflight_batches = 0;
    unsigned stride;
    unsigned i;

    if (state->disable_acceleration ||
        !tarugx_target_surface(state, &surface) ||
        !tarugx_texture_state(state, &texture, &u_scale, &v_scale) ||
        !tarugx_draw_state_supported(state, &surface, texture != NULL))
        return false;
    switch (state->primitive) {
    case MESA_PRIM_TRIANGLES:
        if (count % 3)
            return false;
        triangle_count = count / 3;
        break;
    case MESA_PRIM_TRIANGLE_STRIP:
    case MESA_PRIM_TRIANGLE_FAN:
        if (count < 3)
            return true;
        triangle_count = count - 2;
        break;
    default:
        return false;
    }
    vertices = state->backend->map_vertices(state->backend);
    stride = state->softpipe->vertex_info.size;
    if (!vertices || stride < (texture ? 12 : 8))
        return false;
    if (triangle_count > UINT_MAX / TARUGX_CLIP_MAX_TRIANGLES)
        return false;
    maximum_triangles = triangle_count * TARUGX_CLIP_MAX_TRIANGLES;
    triangles = calloc(maximum_triangles, sizeof(*triangles));
    if (!triangles)
        return false;
    for (i = 0; i < triangle_count; i++) {
        float clipped[TARUGX_CLIP_MAX_VERTICES][TARUGX_VERTEX_FLOATS];
        unsigned clipped_count;
        unsigned clipped_index;
        unsigned ia;
        unsigned ib;
        unsigned ic;
        unsigned a;
        unsigned b;
        unsigned c;

        if (state->primitive == MESA_PRIM_TRIANGLES) {
            ia = i * 3;
            ib = ia + 1;
            ic = ia + 2;
        } else if (state->primitive == MESA_PRIM_TRIANGLE_STRIP) {
            ia = i + (i & 1);
            ib = i + ((i & 1) ? 0 : 1);
            ic = i + 2;
        } else {
            ia = 0;
            ib = i + 1;
            ic = i + 2;
        }
        a = indices ? indices[ia] : start + ia;
        b = indices ? indices[ib] : start + ib;
        c = indices ? indices[ic] : start + ic;

        if (!isfinite(vertices[a * stride]) ||
            !isfinite(vertices[a * stride + 1]) ||
            !isfinite(vertices[b * stride]) ||
            !isfinite(vertices[b * stride + 1]) ||
            !isfinite(vertices[c * stride]) ||
            !isfinite(vertices[c * stride + 1])) {
            free(triangles);
            return false;
        }
        clipped_count = tarugx_clip_triangle(state,
                                             vertices + a * stride,
                                             vertices + b * stride,
                                             vertices + c * stride,
                                             clipped);
        for (clipped_index = 1; clipped_index + 1 < clipped_count;
             clipped_index++) {
            if (prepared_count >= maximum_triangles ||
                !tarugx_prepare_triangle(state, &surface, texture != NULL,
                                         u_scale, v_scale, clipped[0],
                                         clipped[clipped_index],
                                         clipped[clipped_index + 1],
                                         &triangles[prepared_count])) {
                free(triangles);
                return false;
            }
            if (!tarugx_triangle_culled(state,
                                        &triangles[prepared_count]))
                prepared_count++;
        }
    }
    for (i = 0; i < prepared_count; i += TARUGX_MAX_BATCH_TRIANGLES) {
        unsigned batch_count = MIN2(TARUGX_MAX_BATCH_TRIANGLES,
                                    prepared_count - i);

        bool submitted;

        if (!i)
            tarugx_flush_softpipe_render_cache(state->softpipe);
        submitted = texture ?
            tarugx_winsys_submit_textured_triangles(
                &state->winsys, &surface, texture, triangles + i,
                batch_count) :
            tarugx_winsys_submit_triangles(
                &state->winsys, &surface, triangles + i, batch_count);

        if (!submitted) {
            if (inflight_batches)
                tarugx_winsys_wait(&state->winsys, INT64_MAX);
            free(triangles);
            return false;
        }
        inflight_batches++;
        if (inflight_batches == TARUGX_MAX_INFLIGHT_BATCHES) {
            if (!tarugx_winsys_wait(&state->winsys, INT64_MAX)) {
                free(triangles);
                return false;
            }
            inflight_batches = 0;
        }
    }
    if (inflight_batches &&
        !tarugx_winsys_wait(&state->winsys, INT64_MAX)) {
        free(triangles);
        return false;
    }
    free(triangles);
    return true;
}

static void
tarugx_set_primitive(struct vbuf_render *backend, enum mesa_prim primitive)
{
    struct tarugx_context_state *state = tarugx_find_context(NULL, backend);

    if (!state)
        return;
    state->primitive = primitive;
    state->set_primitive(backend, primitive);
}

static void
tarugx_draw_arrays(struct vbuf_render *backend, unsigned start,
                   unsigned count)
{
    struct tarugx_context_state *state = tarugx_find_context(NULL, backend);

    if (state && tarugx_draw_vertices(state, NULL, start, count))
        return;
    if (state)
        state->draw_arrays(backend, start, count);
}

static void
tarugx_draw_elements(struct vbuf_render *backend, const uint16_t *indices,
                     unsigned count)
{
    struct tarugx_context_state *state = tarugx_find_context(NULL, backend);

    if (state && tarugx_draw_vertices(state, indices, 0, count))
        return;
    if (state)
        state->draw_elements(backend, indices, count);
}

static void
tarugx_clear(struct pipe_context *context, unsigned buffers,
             const struct pipe_scissor_state *scissor,
             const union pipe_color_union *color, double depth,
             unsigned stencil)
{
    struct tarugx_context_state *state = tarugx_find_context(context, NULL);
    struct tarugx_winsys_surface surface;

    if (state && !state->disable_acceleration && !scissor && color &&
        tarugx_target_surface(state, &surface)) {
        bool submitted = false;

        tarugx_flush_softpipe_render_cache(state->softpipe);

        if (buffers == (PIPE_CLEAR_COLOR0 | PIPE_CLEAR_DEPTH) &&
            surface.depth && state->softpipe->framebuffer.zsbuf &&
            state->softpipe->framebuffer.zsbuf->format ==
                PIPE_FORMAT_Z16_UNORM &&
            state->softpipe->framebuffer.zsbuf->width == surface.width &&
            state->softpipe->framebuffer.zsbuf->height == surface.height &&
            isfinite(depth) && depth >= 0.0 && depth <= 1.0)
            submitted = tarugx_winsys_submit_color_depth_clear(
                &state->winsys, &surface, tarugx_rgb888(color->f),
                (uint16_t)lrint(depth * UINT16_MAX));
        else if (buffers == PIPE_CLEAR_COLOR0)
            submitted = tarugx_winsys_submit_fill(
                &state->winsys, &surface, tarugx_rgb888(color->f));
        if (submitted &&
            tarugx_winsys_wait(&state->winsys, INT64_MAX))
            return;
    }
    if (state)
        state->clear(context, buffers, scissor, color, depth, stencil);
}

static void
tarugx_context_destroy(struct pipe_context *context)
{
    struct tarugx_context_state saved;
    struct tarugx_context_state *state;

    pthread_mutex_lock(&tarugx_state_lock);
    state = tarugx_find_context(context, NULL);
    if (!state) {
        pthread_mutex_unlock(&tarugx_state_lock);
        return;
    }
    saved = *state;
    memset(state, 0, sizeof(*state));
    pthread_mutex_unlock(&tarugx_state_lock);
    saved.backend->set_primitive = saved.set_primitive;
    saved.backend->draw_arrays = saved.draw_arrays;
    saved.backend->draw_elements = saved.draw_elements;
    context->clear = saved.clear;
    context->destroy = saved.destroy;
    tarugx_winsys_surface_destroy(&saved.winsys, &saved.texture);
    tarugx_winsys_close(&saved.winsys);
    saved.destroy(context);
}

static struct pipe_context *
tarugx_context_create(struct pipe_screen *screen, void *priv, unsigned flags)
{
    struct tarugx_screen_state *screen_state;
    struct tarugx_context_state *state = NULL;
    struct pipe_context *context;
    unsigned i;
    int fd;

    pthread_mutex_lock(&tarugx_state_lock);
    screen_state = tarugx_find_screen(screen);
    if (!screen_state) {
        pthread_mutex_unlock(&tarugx_state_lock);
        return NULL;
    }
    fd = screen_state->fd;
    context = screen_state->context_create(screen, priv, flags);
    if (!context) {
        pthread_mutex_unlock(&tarugx_state_lock);
        return NULL;
    }
    for (i = 0; i < TARUGX_MAX_CONTEXTS; i++) {
        if (!tarugx_contexts[i].context) {
            state = &tarugx_contexts[i];
            break;
        }
    }
    if (!state || !tarugx_winsys_open(&state->winsys, fd)) {
        pthread_mutex_unlock(&tarugx_state_lock);
        context->destroy(context);
        return NULL;
    }
    state->context = context;
    state->softpipe = softpipe_context(context);
    state->backend = state->softpipe->vbuf_backend;
    state->disable_acceleration = getenv("TARUGX_DISABLE_ACCEL") != NULL;
    state->set_primitive = state->backend->set_primitive;
    state->draw_arrays = state->backend->draw_arrays;
    state->draw_elements = state->backend->draw_elements;
    state->clear = context->clear;
    state->destroy = context->destroy;
    state->backend->set_primitive = tarugx_set_primitive;
    state->backend->draw_arrays = tarugx_draw_arrays;
    state->backend->draw_elements = tarugx_draw_elements;
    context->clear = tarugx_clear;
    context->destroy = tarugx_context_destroy;
    pthread_mutex_unlock(&tarugx_state_lock);
    return context;
}

static void
tarugx_screen_destroy(struct pipe_screen *screen)
{
    struct tarugx_screen_state saved;
    struct tarugx_screen_state *state;

    pthread_mutex_lock(&tarugx_state_lock);
    state = tarugx_find_screen(screen);
    if (!state) {
        pthread_mutex_unlock(&tarugx_state_lock);
        return;
    }
    saved = *state;
    memset(state, 0, sizeof(*state));
    pthread_mutex_unlock(&tarugx_state_lock);
    screen->context_create = saved.context_create;
    screen->destroy = saved.destroy;
    close(saved.fd);
    saved.destroy(screen);
}

bool
tarugx_context_enable(struct pipe_screen *screen, int fd)
{
    struct tarugx_screen_state *state = NULL;
    unsigned i;
    int duplicate;

    if (!screen || fd < 0)
        return false;
    duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0)
        return false;
    pthread_mutex_lock(&tarugx_state_lock);
    for (i = 0; i < TARUGX_MAX_SCREENS; i++) {
        if (!tarugx_screens[i].screen) {
            state = &tarugx_screens[i];
            break;
        }
    }
    if (!state) {
        pthread_mutex_unlock(&tarugx_state_lock);
        close(duplicate);
        return false;
    }
    *state = (struct tarugx_screen_state) {
        .screen = screen,
        .fd = duplicate,
        .context_create = screen->context_create,
        .destroy = screen->destroy,
    };
    screen->context_create = tarugx_context_create;
    screen->destroy = tarugx_screen_destroy;
    pthread_mutex_unlock(&tarugx_state_lock);
    return true;
}
