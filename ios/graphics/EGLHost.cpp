#include "togles/rendermechanism.h"
#include "ToGLESRuntime.h"
#include <EGL/egl.h>
bool InitializeIOSMaterial(const GLMContextHost *,const char *,char *,size_t);
namespace {
struct HostSurface { EGLDisplay display; EGLSurface surface; };
bool MakeCurrent(void *data, void *context)
{
    HostSurface *host = static_cast<HostSurface *>(data);
    return eglMakeCurrent(host->display, context ? host->surface : EGL_NO_SURFACE,
        context ? host->surface : EGL_NO_SURFACE, context) == EGL_TRUE;
}
void DisplayedSize(void *data, uint &width, uint &height)
{
    HostSurface *host = static_cast<HostSurface *>(data);
    EGLint w=0, h=0;
    eglQuerySurface(host->display,host->surface,EGL_WIDTH,&w);
    eglQuerySurface(host->display,host->surface,EGL_HEIGHT,&h);
    width=w; height=h;
}
bool ShowPixels(void *data, CShowPixelsParams *params)
{
    HostSurface *host = static_cast<HostSurface *>(data);
    if (params->m_onlySyncView) {
        // ANGLE can defer acquiring/resizing the Metal drawable until the
        // default framebuffer is used. Resolve that before GLM queries its
        // destination size, otherwise the first rotated frame blits at the
        // previous dimensions. Preserve GLM's cached framebuffer bindings.
        GLint read=0;
        gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
        unsigned char pixel[4]={};
        gGL->glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        const GLenum error=gGL->glGetError();
        if (error!=GL_NO_ERROR) { fprintf(stderr,"Native sync readback GL error 0x%x\n",error); return false; }
        EGLint width=0;
        return eglQuerySurface(host->display,host->surface,EGL_WIDTH,&width)==EGL_TRUE;
    }
    return eglSwapBuffers(host->display,host->surface)==EGL_TRUE;
}

}
namespace {
HostSurface liveSurface={};
GLMContextHost liveHost={};
bool liveStarted=false;
EGLContext livePrevious=EGL_NO_CONTEXT;
}
bool DrawToGLESLiveMaterial(char *detail, size_t capacity);
void StopToGLESLiveMaterial();
int StartToGLESMaterialLoop(const char *modules, char *detail, size_t capacity)
{
    if (liveStarted) return 1;
    liveSurface={eglGetCurrentDisplay(),eglGetCurrentSurface(EGL_DRAW)};
    liveHost={};
    livePrevious=eglGetCurrentContext();
    EGLint configID=0,count=0;
    EGLConfig config=NULL;
    if (!eglQueryContext(liveSurface.display,livePrevious,EGL_CONFIG_ID,&configID)) {
        snprintf(detail,capacity,"Live material EGL config query failed"); return 0;
    }
    const EGLint attributes[]={EGL_CONFIG_ID,configID,EGL_NONE};
    const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    if (!eglChooseConfig(liveSurface.display,attributes,&config,1,&count) || !count) {
        snprintf(detail,capacity,"Live material EGL config unavailable"); return 0;
    }
    liveHost.context=eglCreateContext(liveSurface.display,config,EGL_NO_CONTEXT,contextAttributes);
    if (liveHost.context==EGL_NO_CONTEXT || !MakeCurrent(&liveSurface,liveHost.context)) {
        if (liveHost.context!=EGL_NO_CONTEXT) eglDestroyContext(liveSurface.display,liveHost.context);
        snprintf(detail,capacity,"Live material EGL context creation failed"); return 0;
    }
    liveHost.userData=&liveSurface;
    liveHost.makeCurrent=MakeCurrent; liveHost.showPixels=ShowPixels; liveHost.displayedSize=DisplayedSize;
    liveHost.caps.m_hasMixedAttachmentSizes=true;
    liveHost.caps.m_hasFramebufferBlit=true;
    liveHost.caps.m_hasUniformBuffers=true;
    liveHost.caps.m_hasOcclusionQuery=true;
    GLint samples=0; gGL->glGetIntegerv(GL_MAX_SAMPLES,&samples);
    liveHost.caps.m_maxSamples=samples;
    GLMgr::NewGLMgr();
    liveStarted=InitializeIOSMaterial(&liveHost,modules,detail,capacity);
    if (!liveStarted) {
        GLMgr::DelGLMgr();
        MakeCurrent(&liveSurface,livePrevious);
        eglDestroyContext(liveSurface.display,liveHost.context);
    }
    return liveStarted;
}
int DrawToGLESMaterialLoop(char *detail, size_t capacity)
{
    if (!liveStarted || !MakeCurrent(&liveSurface,liveHost.context)) {
        snprintf(detail,capacity,"Live material context bind failed"); return 0;
    }
    CShowPixelsParams sync={}; sync.m_onlySyncView=true;
    if (!ShowPixels(&liveSurface,&sync)) {
        snprintf(detail,capacity,"Live drawable synchronization failed"); return 0;
    }
    return DrawToGLESLiveMaterial(detail,capacity);
}
void StopToGLESMaterialLoop(void)
{
    if (!liveStarted) return;
    MakeCurrent(&liveSurface,liveHost.context);
    StopToGLESLiveMaterial(); GLMgr::DelGLMgr(); liveStarted=false;
    MakeCurrent(&liveSurface,livePrevious);
    eglDestroyContext(liveSurface.display,liveHost.context);
    liveHost.context=EGL_NO_CONTEXT;
}
