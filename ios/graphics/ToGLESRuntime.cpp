#include "togles/rendermechanism.h"
#include "tier0/icommandline.h"
#include "texture_upload.h"
#include "ToGLESRuntime.h"
#include "EngineServices.h"
#include <EGL/egl.h>
#include <string.h>

int InitializeToGLESRuntime(char *detail, size_t capacity)
{
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
    if (!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT) ||
        !eglMakeCurrent(display,draw,read,context) || eglGetCurrentContext()!=context) {
        snprintf(detail,capacity,"EGL context release/rebind failed"); return 0;
    }
    CommandLine()->CreateCmdLine("source-ios");
    return InitializeEngineServices(detail,capacity) && InitializeToGLESBackend(detail,capacity);
}
