// SPDX-License-Identifier: MIT
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define WIDTH 1920
#define HEIGHT 1080

static GLuint
compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile failed: %s\n", log);
        return 0;
    }
    return shader;
}

static EGLConfig
find_rgb565_config(EGLDisplay display)
{
    EGLConfig configs[128];
    EGLint count = 0;
    EGLint i;

    if (!eglGetConfigs(display, configs, 128, &count))
        return NULL;
    for (i = 0; i < count; i++) {
        EGLint visual = 0;
        EGLint renderable = 0;
        EGLint surfaces = 0;

        eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &visual);
        eglGetConfigAttrib(display, configs[i], EGL_RENDERABLE_TYPE,
                           &renderable);
        eglGetConfigAttrib(display, configs[i], EGL_SURFACE_TYPE, &surfaces);
        if ((uint32_t)visual == DRM_FORMAT_RGB565 &&
            (renderable & EGL_OPENGL_ES2_BIT) &&
            (surfaces & EGL_WINDOW_BIT))
            return configs[i];
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    static const char vertex_source[] =
        "attribute vec2 position;\n"
        "attribute vec4 color;\n"
        "varying lowp vec4 v_color;\n"
        "void main() { gl_Position = vec4(position, 0.0, 1.0);"
        " v_color = color; }\n";
    static const char fragment_source[] =
        "varying lowp vec4 v_color;\n"
        "void main() { gl_FragColor = v_color; }\n";
    static const GLfloat positions[] = {
        -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
        -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
        -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
        -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
    };
    static const GLfloat colors[] = {
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
    };
    static const GLfloat batch_positions[] = {
        -0.9f, -0.9f, -0.1f, -0.9f, -0.5f, -0.1f,
         0.1f, -0.9f,  0.9f, -0.9f,  0.5f, -0.1f,
        -0.9f,  0.1f, -0.1f,  0.1f, -0.5f,  0.9f,
         0.1f,  0.1f,  0.9f,  0.1f,  0.5f,  0.9f,
    };
    static const GLfloat batch_colors[] = {
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
    };
    static const GLushort indices[] = {0, 1, 2};
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    struct drm_mode_map_dumb map_request = {0};
    struct gbm_surface *gbm_surface = NULL;
    struct gbm_device *gbm = NULL;
    struct gbm_bo *buffer = NULL;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config;
    EGLint major;
    EGLint minor;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    uint16_t *pixels = MAP_FAILED;
    uint32_t handle;
    uint32_t stride;
    size_t size;
    struct timespec draw_start;
    struct timespec draw_end;
    double draw_ms;
    unsigned iterations = 1;
    unsigned i;
    int software_fallback = 0;
    int batch = 0;
    int indexed = 0;
    GLsizei vertex_count;
    int position_location;
    int color_location;
    int fd = -1;
    int result = 1;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s /dev/dri/cardN [iterations] "
                        "[--software|--batch|--indexed]\n",
                argv[0]);
        return 2;
    }
    if (argc >= 3) {
        char *end = NULL;
        unsigned long value = strtoul(argv[2], &end, 10);

        if (!end || *end || !value || value > 1000) {
            fputs("iterations must be between 1 and 1000\n", stderr);
            return 2;
        }
        iterations = (unsigned)value;
    }
    if (argc == 4) {
        if (!strcmp(argv[3], "--software")) {
            software_fallback = 1;
        } else if (!strcmp(argv[3], "--batch")) {
            batch = 1;
        } else if (!strcmp(argv[3], "--indexed")) {
            indexed = 1;
        } else {
            fputs("unknown mode\n", stderr);
            return 2;
        }
    }
    fd = open(argv[1], O_RDWR | O_CLOEXEC);
    if (fd < 0 || drmSetMaster(fd)) {
        perror("open/set DRM master");
        goto out;
    }
    gbm = gbm_create_device(fd);
    get_platform_display = (void *)eglGetProcAddress(
        "eglGetPlatformDisplayEXT");
    if (!gbm || !get_platform_display)
        goto out;
    display = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    if (!eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_ES_API))
        goto out;
    config = find_rgb565_config(display);
    if (!config) {
        fputs("RGB565 EGL config not found\n", stderr);
        goto out;
    }
    gbm_surface = gbm_surface_create(gbm, WIDTH, HEIGHT, DRM_FORMAT_RGB565,
                                     GBM_BO_USE_RENDERING |
                                     GBM_BO_USE_SCANOUT);
    if (!gbm_surface)
        goto out;
    surface = eglCreateWindowSurface(display, config,
                                     (EGLNativeWindowType)gbm_surface, NULL);
    {
        const EGLint attributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE,
        };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                   attributes);
    }
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context))
        goto out;
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex_shader || !fragment_shader)
        goto out;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glUseProgram(program);
    position_location = glGetAttribLocation(program, "position");
    color_location = glGetAttribLocation(program, "color");
    glViewport(0, 0, WIDTH, HEIGHT);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(position_location, 2, GL_FLOAT, GL_FALSE, 0,
                          batch ? batch_positions : positions);
    glVertexAttribPointer(color_location, 4, GL_FLOAT, GL_FALSE, 0,
                          batch ? batch_colors : colors);
    glEnableVertexAttribArray(position_location);
    glEnableVertexAttribArray(color_location);
    if (software_fallback) {
        glScissor(0, 0, WIDTH, HEIGHT);
        glEnable(GL_SCISSOR_TEST);
    }
    vertex_count = batch ? 12 : 3;
    clock_gettime(CLOCK_MONOTONIC, &draw_start);
    for (i = 0; i < iterations; i++) {
        if (indexed)
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
        else
            glDrawArrays(GL_TRIANGLES, 0, vertex_count);
        glFinish();
    }
    clock_gettime(CLOCK_MONOTONIC, &draw_end);
    draw_ms = (draw_end.tv_sec - draw_start.tv_sec) * 1000.0 +
              (draw_end.tv_nsec - draw_start.tv_nsec) / 1000000.0;
    if (glGetError() != GL_NO_ERROR || !eglSwapBuffers(display, surface))
        goto out;
    buffer = gbm_surface_lock_front_buffer(gbm_surface);
    if (!buffer)
        goto out;
    handle = gbm_bo_get_handle(buffer).u32;
    stride = gbm_bo_get_stride(buffer);
    size = (size_t)stride * HEIGHT;
    map_request.handle = handle;
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_request))
        goto out;
    pixels = mmap(NULL, size, PROT_READ, MAP_SHARED, fd,
                  map_request.offset);
    if (pixels == MAP_FAILED)
        goto out;
    printf("renderer=%s\ncenter=%04x\ncorner=%04x\nstride=%u\n"
           "path=%s\ndraws=%u\ntriangles=%u\ndraw_ms=%.3f\n",
           glGetString(GL_RENDERER),
           pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2], pixels[0],
           stride, software_fallback ? "softpipe" :
                   (batch ? "fpga-batch" :
                    (indexed ? "fpga-indexed" : "fpga")), iterations,
           iterations * (batch ? 4 : 1), draw_ms);
    if (batch)
        printf("top_left=%04x\ntop_right=%04x\n"
               "bottom_left=%04x\nbottom_right=%04x\n",
               pixels[198 * (stride / 2) + 480],
               pixels[198 * (stride / 2) + 1440],
               pixels[738 * (stride / 2) + 480],
               pixels[738 * (stride / 2) + 1440]);
    if ((!batch &&
         pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0xf800) ||
        (batch &&
         (pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0x001f ||
          pixels[198 * (stride / 2) + 480] != 0xffe0 ||
          pixels[198 * (stride / 2) + 1440] != 0xffff ||
          pixels[738 * (stride / 2) + 480] != 0xf800 ||
          pixels[738 * (stride / 2) + 1440] != 0x07e0)) ||
        pixels[0] != 0x001f) {
        fputs("RGB565 readback mismatch\n", stderr);
        goto out;
    }
    result = 0;
out:
    if (pixels != MAP_FAILED)
        munmap(pixels, size);
    if (buffer)
        gbm_surface_release_buffer(gbm_surface, buffer);
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(display, surface);
        eglTerminate(display);
    }
    if (gbm_surface)
        gbm_surface_destroy(gbm_surface);
    if (gbm)
        gbm_device_destroy(gbm);
    if (fd >= 0) {
        drmDropMaster(fd);
        close(fd);
    }
    return result;
}
