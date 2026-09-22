#include "togles/rendermechanism.h"
#include "tier0/icommandline.h"
#include "texture_upload.h"
#include "ToGLESRuntime.h"
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
    CommandLine()->CreateCmdLine("source-ios-probe");
    return InitializeToGLESBackend(detail,capacity);
}

int CheckToGLESUploads(char *detail, size_t capacity)
{
    const int sizes[][2]={{1,1},{2,1},{2,2},{3,5},{5,3},{4,4},{8,8}};
    const GLenum formats[]={GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
        GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,GL_COMPRESSED_RGBA_S3TC_DXT5_EXT};
    const uint8_t rgb[4][3]={{255,0,0},{0,255,0},{170,85,0},{85,170,0}};
    const uint8_t alpha5[8]={255,0,218,182,145,109,72,36};
    GLint oldTexture, oldFBO, oldAlign, oldRow, oldSkipX, oldSkipY, oldUnpackBuffer;
    gGL->glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    gGL->glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFBO);
    gGL->glGetIntegerv(GL_UNPACK_ALIGNMENT,&oldAlign);
    gGL->glGetIntegerv(GL_UNPACK_ROW_LENGTH,&oldRow);
    gGL->glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&oldSkipX);
    gGL->glGetIntegerv(GL_UNPACK_SKIP_ROWS,&oldSkipY);
    gGL->glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&oldUnpackBuffer);
    GLuint unpackBuffer=0;
    gGL->glGenBuffers(1,&unpackBuffer);
    gGL->glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackBuffer);
    gGL->glBufferData(GL_PIXEL_UNPACK_BUFFER,32,NULL,GL_STATIC_DRAW);
    GLuint texture=0, fbo=0;
    gGL->glGenTextures(1,&texture); gGL->glBindTexture(GL_TEXTURE_2D,texture);
    gGL->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    gGL->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    gGL->glGenFramebuffers(1,&fbo); gGL->glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    bool valid=true;
    int completed=0;
    for (int format=0; format<4 && valid; ++format) {
        for (unsigned size=0; size<sizeof(sizes)/sizeof(sizes[0]) && valid; ++size) {
            const int width=sizes[size][0], height=sizes[size][1];
            const int blockSize=format<2 ? 8 : 16;
            const int blocksX=(width+3)/4, blocksY=(height+3)/4;
            uint8_t compressed[64]={0};
            for (int by=0;by<blocksY;++by) for (int bx=0;bx<blocksX;++bx) {
                uint8_t *block=compressed+(by*blocksX+bx)*blockSize;
                uint8_t *color=block+(format<2 ? 0 : 8);
                color[0]=0; color[1]=0xf8; color[2]=0xe0; color[3]=7;
                for (int y=0;y<4;++y) for (int x=0;x<4;++x)
                    color[4+y] |= ((x+bx+by)%4) << (2*x);
                if (format==2) for (int j=0;j<8;j+=2) { block[j]=0x50; block[j+1]=0xfa; }
                if (format==3) {
                    block[0]=255; block[1]=0;
                    uint64_t bits=0;
                    for (int j=0;j<16;++j) bits |= uint64_t(j%8) << (j*3);
                    for (int j=0;j<6;++j) block[2+j]=(bits>>(j*8))&255;
                }
            }
            // Prove the upload owns its tightly packed decode and restores GL state.
            gGL->glPixelStorei(GL_UNPACK_ALIGNMENT,8);
            gGL->glPixelStorei(GL_UNPACK_ROW_LENGTH,11);
            gGL->glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
            gGL->glPixelStorei(GL_UNPACK_SKIP_ROWS,1);
            CompressedTexImage2D(GL_TEXTURE_2D,0,formats[format],width,height,0,blocksX*blocksY*blockSize,compressed);
            GLint alignment,row,skipX,skipY,pbo;
            gGL->glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
            gGL->glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
            gGL->glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipX);
            gGL->glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipY);
            gGL->glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);
            valid = pbo==(GLint)unpackBuffer && alignment==8 && row==11 && skipX==2 && skipY==1;
            gGL->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
            valid &= gGL->glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
            uint8_t actual[256]={0};
            gGL->glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,actual);
            for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
                uint8_t expected[4];
                memcpy(expected,rgb[(x%4+x/4+y/4)%4],3);
                expected[3]=format<2 ? 255 : format==2 ? (x%4)*85 : alpha5[((y%4)*4+x%4)%8];
                valid &= memcmp(actual+4*(y*width+x),expected,4)==0;
            }
            valid &= gGL->glGetError()==GL_NO_ERROR;
            if (!valid) snprintf(detail,capacity,"DXT upload/readback/state failed: format %d, %dx%d",format,width,height);
            else ++completed;
        }
    }
    if (valid) {
        uint8_t compressed[16]={0}; int simple=0, complex=0;
        void *bad=uncompressDXTc(4,4,formats[3],15,0,&simple,&complex,compressed);
        valid &= bad==NULL; free(bad);
        bad=uncompressDXTc(0,4,formats[0],8,0,&simple,&complex,compressed);
        valid &= bad==NULL; free(bad);
        bad=uncompressDXTc(4,4,GL_RGBA8,16,0,&simple,&complex,compressed);
        valid &= bad==NULL; free(bad);
        if (!valid) snprintf(detail,capacity,"Invalid DXT input was accepted");
    }
    gGL->glBindFramebuffer(GL_FRAMEBUFFER,oldFBO);
    gGL->glBindTexture(GL_TEXTURE_2D,oldTexture);
    gGL->glPixelStorei(GL_UNPACK_ALIGNMENT,oldAlign);
    gGL->glPixelStorei(GL_UNPACK_ROW_LENGTH,oldRow);
    gGL->glPixelStorei(GL_UNPACK_SKIP_PIXELS,oldSkipX);
    gGL->glPixelStorei(GL_UNPACK_SKIP_ROWS,oldSkipY);
    gGL->glBindBuffer(GL_PIXEL_UNPACK_BUFFER,oldUnpackBuffer);
    gGL->glDeleteBuffers(1,&unpackBuffer);
    gGL->glDeleteFramebuffers(1,&fbo); gGL->glDeleteTextures(1,&texture);
    if (valid) snprintf(detail,capacity,"EGL rebind + GLES dispatch + %d DXT uploads: PASS",completed);
    return valid;
}
