#include "togles/rendermechanism.h"
#include "ToGLESRuntime.h"
#include "ToGLESShaderChecks.h"
#include <EGL/egl.h>

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
        EGLint width=0;
        return eglQuerySurface(host->display,host->surface,EGL_WIDTH,&width)==EGL_TRUE;
    }
    return eglSwapBuffers(host->display,host->surface)==EGL_TRUE;
}
}

int CheckToGLESObjects(char *detail, size_t capacity)
{
    EGLDisplay display=eglGetCurrentDisplay();
    EGLContext previous=eglGetCurrentContext();
    EGLSurface draw=eglGetCurrentSurface(EGL_DRAW), read=eglGetCurrentSurface(EGL_READ);
    EGLConfig config=NULL;
    EGLint count=0;
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    if (!eglChooseConfig(display,attributes,&config,1,&count) || !count) {
        snprintf(detail,capacity,"GLM objects: no EGL pbuffer config"); return 0;
    }
    const EGLint surfaceAttributes[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};
    const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttributes);
    EGLContext native=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);
    bool valid=surface!=EGL_NO_SURFACE && native!=EGL_NO_CONTEXT;
    valid=valid && eglMakeCurrent(display,surface,surface,native);
    snprintf(detail,capacity,"GLM objects: EGL creation failed");
    if (valid) {
        HostSurface hostSurface={display,surface};
        GLMContextHost host={};
        host.context=native; host.userData=&hostSurface;
        host.makeCurrent=MakeCurrent; host.showPixels=ShowPixels; host.displayedSize=DisplayedSize;
        host.caps.m_hasMixedAttachmentSizes=true;
        host.caps.m_hasFramebufferBlit=true;
        host.caps.m_hasUniformBuffers=true;
        host.caps.m_hasOcclusionQuery=true;
        GLint samples=0; gGL->glGetIntegerv(GL_MAX_SAMPLES,&samples);
        host.caps.m_maxSamples=samples;
        GLMDisplayParams params={};
        params.m_backBufferWidth=8; params.m_backBufferHeight=8;
        params.m_backBufferFormat=D3DFMT_A8R8G8B8;
        GLMgr::NewGLMgr();
        int uploads=0;
        // Recreate GLM on the same live native context, exercising its ownership
        // boundary and cleanup independently of eglDestroyContext.
        for (int cycle=0; cycle<2 && valid; ++cycle) {
            if (!eglMakeCurrent(display,draw,read,previous)) {
                valid=false; snprintf(detail,capacity,"GLM host setup failed"); break;
            }
            GLMContext *context=GLMgr::aGLMgr()->NewContext(NULL,&params,&host);
            GLenum error=gGL->glGetError();
            valid=context && error==GL_NO_ERROR && eglGetCurrentContext()==native;
            if (valid) {
                context->ReleaseCurrent(true);
                valid=eglGetCurrentContext()==EGL_NO_CONTEXT;
                context->MakeCurrent(true);
                valid=valid && eglGetCurrentContext()==native;
            }
            snprintf(detail,capacity,"GLMContext initialization: GL error 0x%x",error);
            const D3DFORMAT formats[]={D3DFMT_DXT1,D3DFMT_DXT3,D3DFMT_DXT5};
            for (int format=0;format<3 && valid;++format) {
                GLMTexLayoutKey key={};
                key.m_texGLTarget=GL_TEXTURE_2D; key.m_texFormat=formats[format];
                key.m_texFlags=kGLMTexMipped;
                key.m_xSize=4; key.m_ySize=4; key.m_zSize=1;
                CGLMTex *texture=context->NewTex(&key,3,"ios-object-check");
                GLuint fbo=0;
                gGL->glGenFramebuffers(1,&fbo);
                // Two uploads per level check both allocation and replacement.
                for (int pass=0;pass<2 && valid;++pass) for (int mip=0;mip<3 && valid;++mip) {
                    int size=4>>mip;
                    GLMTexLockParams lock={};
                    lock.m_tex=texture; lock.m_mip=mip;
                    lock.m_region.xmax=size; lock.m_region.ymax=size; lock.m_region.zmax=1;
                    char *bytes=NULL; int row=0, depth=0;
                    texture->Lock(&lock,&bytes,&row,&depth);
                    unsigned char block[16]={};
                    unsigned char *color=block+(format?8:0);
                    // Distinct endpoint colors, selecting red then green.
                    color[0]=0; color[1]=248; color[2]=224; color[3]=7;
                    memset(color+4,pass?0x55:0,4);
                    if (format==1) memset(block,255,8);
                    if (format==2) block[0]=255;
                    if (bytes) memcpy(bytes,block,format?16:8);
                    texture->Unlock(&lock);
                    gGL->glBindFramebuffer(GL_FRAMEBUFFER,fbo);
                    gGL->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture->GetTexName(),mip);
                    valid=bytes && gGL->glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
                    unsigned char pixels[64]={};
                    if (valid) gGL->glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                    error=gGL->glGetError();
                    valid=valid && error==GL_NO_ERROR;
                    for (int i=0;i<size*size;++i)
                        valid=valid && pixels[i*4]==(pass?0:255) && pixels[i*4+1]==(pass?255:0)
                            && pixels[i*4+2]==0 && pixels[i*4+3]==255;
                    snprintf(detail,capacity,"CGLMTex format %d mip %d pass %d: %s (GL 0x%x, pixel %u/%u/%u/%u)",
                        format,mip,pass,valid?"PASS":"FAIL",error,pixels[0],pixels[1],pixels[2],pixels[3]);
                    if (valid) ++uploads;
                }
                gGL->glBindFramebuffer(GL_FRAMEBUFFER,0); gGL->glDeleteFramebuffers(1,&fbo);
                context->DelTex(texture);
            }
            if (valid) valid=CheckToGLESShaderDraw(context,detail,capacity);
            if (context) GLMgr::aGLMgr()->DelContext(context);
            error=gGL->glGetError();
            if (error!=GL_NO_ERROR) { valid=false; snprintf(detail,capacity,"GLM teardown: GL error 0x%x",error); }
        }
        GLMgr::DelGLMgr();
        if (valid) snprintf(detail,capacity,"2 GLM cycles + %d mip uploads: PASS\n8 shader draws + cache + link recovery: PASS",uploads);
    }
    if (!eglMakeCurrent(display,draw,read,previous)) {
        valid=false; snprintf(detail,capacity,"GLM objects: restoring host EGL context failed");
    }
    if (native!=EGL_NO_CONTEXT) eglDestroyContext(display,native);
    if (surface!=EGL_NO_SURFACE) eglDestroySurface(display,surface);
    return valid;
}
