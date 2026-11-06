// SPDX-License-Identifier: MIT
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <math.h>
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
#define PIPELINE_TRIANGLES 8

static int
find_display(int fd, drmModeRes **resources_out,
             drmModeConnector **connector_out,
             drmModeEncoder **encoder_out, drmModeCrtc **crtc_out)
{
    drmModeRes *resources = drmModeGetResources(fd);
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeCrtc *crtc = NULL;
    int i;

    if (!resources)
        return -1;
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED &&
            connector->count_modes && connector->encoder_id)
            break;
        drmModeFreeConnector(connector);
        connector = NULL;
    }
    if (connector)
        encoder = drmModeGetEncoder(fd, connector->encoder_id);
    if (encoder && encoder->crtc_id)
        crtc = drmModeGetCrtc(fd, encoder->crtc_id);
    if (!connector || !encoder || !crtc) {
        drmModeFreeCrtc(crtc);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        return -1;
    }
    *resources_out = resources;
    *connector_out = connector;
    *encoder_out = encoder;
    *crtc_out = crtc;
    return 0;
}

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
find_rgb565_config(EGLDisplay display, int depth_test)
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
        EGLint depth_size = 0;

        eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &visual);
        eglGetConfigAttrib(display, configs[i], EGL_RENDERABLE_TYPE,
                           &renderable);
        eglGetConfigAttrib(display, configs[i], EGL_SURFACE_TYPE, &surfaces);
        eglGetConfigAttrib(display, configs[i], EGL_DEPTH_SIZE, &depth_size);
        if ((uint32_t)visual == DRM_FORMAT_RGB565 &&
            (renderable & EGL_OPENGL_ES2_BIT) &&
            (surfaces & EGL_WINDOW_BIT) && (!depth_test || depth_size == 16))
            return configs[i];
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    static const char vertex_source[] =
        "attribute vec2 position;\n"
        "attribute float depth;\n"
        "attribute vec4 color;\n"
        "varying lowp vec4 v_color;\n"
        "void main() { gl_Position = vec4(position, depth, 1.0);"
        " v_color = color; }\n";
    static const char fragment_source[] =
        "varying lowp vec4 v_color;\n"
        "void main() { gl_FragColor = v_color; }\n";
    static const char textured_vertex_source[] =
        "attribute vec2 position;\n"
        "attribute float depth;\n"
        "attribute vec4 color;\n"
        "attribute vec2 texcoord;\n"
        "varying lowp vec4 v_color;\n"
        "varying mediump vec2 v_texcoord;\n"
        "void main() { gl_Position = vec4(position, depth, 1.0);"
        " v_color = color; v_texcoord = texcoord; }\n";
    static const char textured_fragment_source[] =
        "uniform sampler2D texture0;\n"
        "varying lowp vec4 v_color;\n"
        "varying mediump vec2 v_texcoord;\n"
        "void main() { gl_FragColor = texture2D(texture0, v_texcoord) *"
        " v_color; }\n";
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
    static const GLfloat gradient_colors[] = {
        0.25f, 0.25f, 0.25f, 1.0f,
        0.75f, 0.25f, 0.25f, 1.0f,
        0.50f, 0.75f, 0.25f, 1.0f,
    };
    static const GLfloat texture_colors[] = {
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
    };
    static const GLfloat texture_coordinates[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
    };
    static const uint16_t texture_pixels[16] = {
        0xf800, 0x07e0, 0x001f, 0xffff,
        0x07e0, 0x001f, 0xffff, 0xf800,
        0x001f, 0xffff, 0xffe0, 0x07ff,
        0xffff, 0xf800, 0x07e0, 0x001f,
    };
    static const GLfloat depth_values[] = {
        -0.6f, -0.6f, -0.6f,
         0.4f,  0.4f,  0.4f,
    };
    static const GLfloat depth_colors[] = {
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
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
    static const GLfloat strip_positions[] = {
        -0.5f, -0.5f, 0.5f, -0.5f,
        -0.5f,  0.5f, 0.5f,  0.5f,
    };
    static const GLfloat fan_positions[] = {
         0.0f,  0.0f,
        -0.5f, -0.5f, 0.5f, -0.5f,
         0.5f,  0.5f, -0.5f, 0.5f,
        -0.5f, -0.5f,
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
    static const GLfloat cube_vertices[8][3] = {
        {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
        {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1},
    };
    static const unsigned cube_faces[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
        {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0},
    };
    static const GLfloat cube_face_colors[6][4] = {
        {1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1},
        {1, 1, 0, 1}, {1, 0, 1, 1}, {0, 1, 1, 1},
    };
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    struct drm_mode_map_dumb map_request = {0};
    struct gbm_surface *gbm_surface = NULL;
    struct gbm_device *gbm = NULL;
    struct gbm_bo *buffer = NULL;
    drmModeRes *resources = NULL;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeCrtc *original_crtc = NULL;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config;
    EGLint major;
    EGLint minor;
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint texture_object = 0;
    uint16_t *pixels = MAP_FAILED;
    uint32_t handle;
    uint32_t stride;
    uint32_t framebuffer_id = 0;
    size_t size;
    struct timespec draw_start;
    struct timespec draw_end;
    double draw_ms;
    unsigned iterations = 1;
    unsigned i;
    int software_fallback = 0;
    int batch = 0;
    int pipeline = 0;
    int strip = 0;
    int fan = 0;
    int indexed = 0;
    int gradient = 0;
    int cull_mode = 0;
    int front_face_cw = 0;
    int scissor_mode = 0;
    int depth_test = 0;
    int depth_one = 0;
    int textured = 0;
    int display_demo = 0;
    int cube_demo = 0;
    GLsizei vertex_count;
    GLfloat pipeline_positions[PIPELINE_TRIANGLES * 6];
    GLfloat pipeline_colors[PIPELINE_TRIANGLES * 12];
    GLfloat cube_positions[36 * 2];
    GLfloat cube_depths[36];
    GLfloat cube_colors[36 * 4];
    int position_location;
    int depth_location;
    int color_location;
    int texcoord_location;
    const char *path;
    int fd = -1;
    int result = 1;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s /dev/dri/cardN [iterations] "
                        "[--software|--batch|--pipeline|--strip|--fan|"
                        "--indexed|--gradient|"
                        "--cull-back|--cull-front|--cull-back-cw|"
                        "--cull-front-cw|--scissor|--depth|--depth-one|"
                        "--texture|--display-demo|--cube]\n",
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
        } else if (!strcmp(argv[3], "--pipeline")) {
            pipeline = 1;
        } else if (!strcmp(argv[3], "--strip")) {
            strip = 1;
        } else if (!strcmp(argv[3], "--fan")) {
            fan = 1;
        } else if (!strcmp(argv[3], "--indexed")) {
            indexed = 1;
        } else if (!strcmp(argv[3], "--gradient")) {
            gradient = 1;
        } else if (!strcmp(argv[3], "--cull-back")) {
            cull_mode = GL_BACK;
        } else if (!strcmp(argv[3], "--cull-front")) {
            cull_mode = GL_FRONT;
        } else if (!strcmp(argv[3], "--cull-back-cw")) {
            cull_mode = GL_BACK;
            front_face_cw = 1;
        } else if (!strcmp(argv[3], "--cull-front-cw")) {
            cull_mode = GL_FRONT;
            front_face_cw = 1;
        } else if (!strcmp(argv[3], "--scissor")) {
            scissor_mode = 1;
        } else if (!strcmp(argv[3], "--depth")) {
            depth_test = 1;
        } else if (!strcmp(argv[3], "--depth-one")) {
            depth_test = 1;
            depth_one = 1;
        } else if (!strcmp(argv[3], "--texture")) {
            textured = 1;
        } else if (!strcmp(argv[3], "--display-demo")) {
            depth_test = 1;
            display_demo = 1;
        } else if (!strcmp(argv[3], "--cube")) {
            depth_test = 1;
            cube_demo = 1;
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
    config = find_rgb565_config(display, depth_test);
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
    vertex_shader = compile_shader(GL_VERTEX_SHADER,
                                   textured ? textured_vertex_source :
                                   vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
                                     textured ? textured_fragment_source :
                                     fragment_source);
    if (!vertex_shader || !fragment_shader)
        goto out;
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glUseProgram(program);
    position_location = glGetAttribLocation(program, "position");
    depth_location = glGetAttribLocation(program, "depth");
    color_location = glGetAttribLocation(program, "color");
    texcoord_location = glGetAttribLocation(program, "texcoord");
    glViewport(0, 0, WIDTH, HEIGHT);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    if (cull_mode) {
        glEnable(GL_CULL_FACE);
        glCullFace(cull_mode);
        glFrontFace(front_face_cw ? GL_CW : GL_CCW);
    }
    if (strip) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClearDepthf(1.0f);
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }
    glClear(GL_COLOR_BUFFER_BIT |
            (depth_test ? GL_DEPTH_BUFFER_BIT : 0));
    if (textured) {
        glGenTextures(1, &texture_object);
        glBindTexture(GL_TEXTURE_2D, texture_object);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 4, 0, GL_RGB,
                     GL_UNSIGNED_SHORT_5_6_5, texture_pixels);
        glUniform1i(glGetUniformLocation(program, "texture0"), 0);
    }
    if (pipeline) {
        for (i = 0; i < PIPELINE_TRIANGLES; i++) {
            memcpy(pipeline_positions + i * 6, positions,
                   6 * sizeof(*positions));
            memcpy(pipeline_colors + i * 12, colors,
                   12 * sizeof(*colors));
        }
    }
    glVertexAttribPointer(position_location, 2, GL_FLOAT, GL_FALSE, 0,
                          cube_demo ? cube_positions :
                          (strip ? strip_positions :
                          (fan ? fan_positions :
                          (pipeline ? pipeline_positions :
                          (batch ? batch_positions : positions)))));
    glVertexAttribPointer(color_location, 4, GL_FLOAT, GL_FALSE, 0,
                          cube_demo ? cube_colors :
                          (depth_test ? depth_colors :
                          (pipeline ? pipeline_colors :
                          (batch ? batch_colors :
                          (gradient ? gradient_colors :
                          (textured ? texture_colors : colors))))));
    if (textured) {
        glVertexAttribPointer(texcoord_location, 2, GL_FLOAT, GL_FALSE, 0,
                              texture_coordinates);
        glEnableVertexAttribArray(texcoord_location);
    }
    if (depth_test) {
        glVertexAttribPointer(depth_location, 1, GL_FLOAT, GL_FALSE, 0,
                              cube_demo ? cube_depths : depth_values);
        glEnableVertexAttribArray(depth_location);
    }
    glEnableVertexAttribArray(position_location);
    glEnableVertexAttribArray(color_location);
    if (software_fallback) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ZERO);
    }
    if (scissor_mode) {
        glScissor(800, 0, 320, HEIGHT);
        glEnable(GL_SCISSOR_TEST);
    }
    vertex_count = strip ? 4 : (fan ? 6 :
        (pipeline ? PIPELINE_TRIANGLES * 3 :
        (depth_test && !depth_one ? 6 : (batch ? 12 : 3))));
    clock_gettime(CLOCK_MONOTONIC, &draw_start);
    if (cube_demo) {
        unsigned frame;

        if (find_display(fd, &resources, &connector, &encoder,
                         &original_crtc))
            goto out;
        for (frame = 0; frame < 16; frame++) {
            GLfloat projected[8][3];
            float angle = frame * (float)M_PI / 8.0f;
            float sy = sinf(angle), cy = cosf(angle);
            float sx = sinf(angle * 0.63f), cx = cosf(angle * 0.63f);
            unsigned face;
            unsigned output = 0;

            for (i = 0; i < 8; i++) {
                float x = cube_vertices[i][0];
                float y = cube_vertices[i][1];
                float z = cube_vertices[i][2];
                float rx = cy * x + sy * z;
                float rz = -sy * x + cy * z;
                float ry = cx * y - sx * rz;
                float camera_z = sx * y + cx * rz + 4.0f;

                projected[i][0] = rx * 1.35f / camera_z;
                projected[i][1] = ry * 2.4f / camera_z;
                projected[i][2] = (camera_z - 4.0f) * 0.45f;
            }
            for (face = 0; face < 6; face++) {
                static const unsigned order[6] = {0, 1, 2, 0, 2, 3};
                unsigned corner;

                for (corner = 0; corner < 6; corner++, output++) {
                    unsigned vertex = cube_faces[face][order[corner]];
                    unsigned component;

                    cube_positions[output * 2] = projected[vertex][0];
                    cube_positions[output * 2 + 1] = projected[vertex][1];
                    cube_depths[output] = projected[vertex][2];
                    for (component = 0; component < 4; component++)
                        cube_colors[output * 4 + component] =
                            cube_face_colors[face][component];
                }
            }
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glFinish();
            if (glGetError() != GL_NO_ERROR ||
                !eglSwapBuffers(display, surface))
                goto out;
            {
                struct gbm_bo *next =
                    gbm_surface_lock_front_buffer(gbm_surface);
                uint32_t next_framebuffer = 0;
                uint32_t next_handle;
                uint32_t next_stride;

                if (!next)
                    goto out;
                next_handle = gbm_bo_get_handle(next).u32;
                next_stride = gbm_bo_get_stride(next);
                if (drmModeAddFB(fd, WIDTH, HEIGHT, 16, 16, next_stride,
                                 next_handle, &next_framebuffer) ||
                    drmModeSetCrtc(fd, encoder->crtc_id, next_framebuffer,
                                   0, 0, &connector->connector_id, 1,
                                   &connector->modes[0])) {
                    gbm_surface_release_buffer(gbm_surface, next);
                    goto out;
                }
                if (framebuffer_id)
                    drmModeRmFB(fd, framebuffer_id);
                if (buffer)
                    gbm_surface_release_buffer(gbm_surface, buffer);
                buffer = next;
                framebuffer_id = next_framebuffer;
                handle = next_handle;
                stride = next_stride;
            }
            usleep(160000);
        }
    } else for (i = 0; i < iterations; i++) {
        if (indexed)
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
        else
            glDrawArrays(strip ? GL_TRIANGLE_STRIP :
                         (fan ? GL_TRIANGLE_FAN : GL_TRIANGLES),
                         0, vertex_count);
        glFinish();
    }
    clock_gettime(CLOCK_MONOTONIC, &draw_end);
    draw_ms = (draw_end.tv_sec - draw_start.tv_sec) * 1000.0 +
              (draw_end.tv_nsec - draw_start.tv_nsec) / 1000000.0;
    if (!cube_demo) {
        if (glGetError() != GL_NO_ERROR || !eglSwapBuffers(display, surface))
            goto out;
        buffer = gbm_surface_lock_front_buffer(gbm_surface);
        if (!buffer)
            goto out;
        handle = gbm_bo_get_handle(buffer).u32;
        stride = gbm_bo_get_stride(buffer);
    }
    if (display_demo) {
        resources = drmModeGetResources(fd);
        if (!resources)
            goto out;
        for (i = 0; i < (unsigned)resources->count_connectors; i++) {
            connector = drmModeGetConnector(fd, resources->connectors[i]);
            if (connector && connector->connection == DRM_MODE_CONNECTED &&
                connector->count_modes && connector->encoder_id)
                break;
            drmModeFreeConnector(connector);
            connector = NULL;
        }
        if (!connector)
            goto out;
        encoder = drmModeGetEncoder(fd, connector->encoder_id);
        if (!encoder || !encoder->crtc_id)
            goto out;
        original_crtc = drmModeGetCrtc(fd, encoder->crtc_id);
        if (drmModeAddFB(fd, WIDTH, HEIGHT, 16, 16, stride, handle,
                         &framebuffer_id) ||
            drmModeSetCrtc(fd, encoder->crtc_id, framebuffer_id, 0, 0,
                           &connector->connector_id, 1,
                           &connector->modes[0]))
            goto out;
        puts("display_demo=10s");
        fflush(stdout);
        sleep(10);
    }
    size = (size_t)stride * HEIGHT;
    map_request.handle = handle;
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_request))
        goto out;
    pixels = mmap(NULL, size, PROT_READ, MAP_SHARED, fd,
                  map_request.offset);
    if (pixels == MAP_FAILED)
        goto out;
    path = "fpga";
    if (software_fallback)
        path = "softpipe";
    else if (batch)
        path = "fpga-batch";
    else if (pipeline)
        path = "fpga-pipeline";
    else if (strip)
        path = "fpga-strip";
    else if (fan)
        path = "fpga-fan";
    else if (indexed)
        path = "fpga-indexed";
    else if (gradient)
        path = "fpga-gradient";
    else if (textured)
        path = "fpga-texture";
    else if (cube_demo)
        path = "fpga-cube";
    else if (cull_mode == GL_BACK)
        path = front_face_cw ? "fpga-cull-back-cw" : "fpga-cull-back";
    else if (cull_mode == GL_FRONT)
        path = front_face_cw ? "fpga-cull-front-cw" : "fpga-cull-front";
    else if (scissor_mode)
        path = "fpga-scissor";
    else if (depth_test)
        path = depth_one ? "fpga-depth-one" : "fpga-depth";
    printf("renderer=%s\ncenter=%04x\ncorner=%04x\n"
           "scissor_outside=%04x\nscissor_inside=%04x\nstride=%u\n"
           "path=%s\ndraws=%u\ntriangles=%u\ndraw_ms=%.3f\n",
           glGetString(GL_RENDERER),
           pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2], pixels[0],
           pixels[(HEIGHT / 2) * (stride / 2) + 760],
           pixels[(HEIGHT / 2) * (stride / 2) + 1080],
           stride, path, cube_demo ? 16 : iterations,
           cube_demo ? 16 * 12 : iterations * (strip ? 2 : (fan ? 4 :
                         (pipeline ? PIPELINE_TRIANGLES :
                         (depth_test ? 2 : (batch ? 4 : 1))))), draw_ms);
    if (batch)
        printf("top_left=%04x\ntop_right=%04x\n"
               "bottom_left=%04x\nbottom_right=%04x\n",
               pixels[198 * (stride / 2) + 480],
               pixels[198 * (stride / 2) + 1440],
               pixels[738 * (stride / 2) + 480],
               pixels[738 * (stride / 2) + 1440]);
    if ((gradient &&
         ((pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] >> 11) < 14 ||
          (pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] >> 11) > 17 ||
          ((pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] >> 5) & 63) < 29 ||
          ((pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] >> 5) & 63) > 34 ||
          (pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] & 31) < 7 ||
          (pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] & 31) > 9)) ||
        (depth_test && !cube_demo &&
         pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0x07e0) ||
        (textured &&
         pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0xffe0) ||
        (!batch && !pipeline && !gradient && !depth_test && !textured &&
         ((cull_mode == GL_FRONT) == front_face_cw) &&
         pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0xf800) ||
        (cull_mode && ((cull_mode == GL_FRONT) != front_face_cw) &&
         pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0x001f) ||
        (scissor_mode &&
         (pixels[(HEIGHT / 2) * (stride / 2) + 760] != 0x001f ||
          pixels[(HEIGHT / 2) * (stride / 2) + 1080] != 0xf800)) ||
        (batch &&
         (pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0x001f ||
          pixels[198 * (stride / 2) + 480] != 0xffe0 ||
          pixels[198 * (stride / 2) + 1440] != 0xffff ||
          pixels[738 * (stride / 2) + 480] != 0xf800 ||
          pixels[738 * (stride / 2) + 1440] != 0x07e0)) ||
        (pipeline &&
         pixels[(HEIGHT / 2) * (stride / 2) + WIDTH / 2] != 0xf800) ||
        (!pipeline && !strip && !fan && pixels[0] != 0x001f)) {
        fputs("RGB565 readback mismatch\n", stderr);
        goto out;
    }
    result = 0;
out:
    if (original_crtc && connector)
        drmModeSetCrtc(fd, original_crtc->crtc_id, original_crtc->buffer_id,
                       original_crtc->x, original_crtc->y,
                       &connector->connector_id, 1,
                       original_crtc->mode_valid ? &original_crtc->mode :
                       NULL);
    if (framebuffer_id)
        drmModeRmFB(fd, framebuffer_id);
    drmModeFreeCrtc(original_crtc);
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    if (pixels != MAP_FAILED)
        munmap(pixels, size);
    if (buffer)
        gbm_surface_release_buffer(gbm_surface, buffer);
    if (display != EGL_NO_DISPLAY) {
        if (context != EGL_NO_CONTEXT && texture_object)
            glDeleteTextures(1, &texture_object);
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
