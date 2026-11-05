// SPDX-License-Identifier: MIT
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    struct gbm_device *gbm;
    EGLDisplay display;
    EGLContext context;
    EGLConfig config;
    EGLint config_count;
    EGLint major;
    EGLint minor;
    int fd;
    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE,
    };
    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE,
    };

    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/dri/renderDXXX\n", argv[0]);
        return 2;
    }
    fd = open(argv[1], O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 3;
    gbm = gbm_create_device(fd);
    get_platform_display = (void *)eglGetProcAddress(
        "eglGetPlatformDisplayEXT");
    if (!gbm || !get_platform_display)
        return 4;
    display = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    if (!eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(display, config_attributes, &config, 1,
                         &config_count) ||
        config_count != 1)
        return 5;
    context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                               context_attributes);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return 6;
    printf("renderer=%s\nvendor=%s\nversion=%s\n",
           glGetString(GL_RENDERER), glGetString(GL_VENDOR),
           glGetString(GL_VERSION));
    eglDestroyContext(display, context);
    eglTerminate(display);
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
