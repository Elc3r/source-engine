#include "togles/rendermechanism.h"
#include "ToGLESFixtures.h"
#include "ToGLESShaderChecks.h"
#include "ToGLESRuntime.h"

namespace {
CGLMProgram *MakeProgram(GLMContext *context, bool vertex, bool tinted, char *detail, size_t capacity)
{
    CUtlBuffer output(0,65536,CUtlBuffer::TEXT_BUFFER);
    if (!ToGLESFixtures::Translate(vertex,output,detail,capacity,tinted)) return NULL;
    CGLMProgram *program=context->NewProgram(vertex?kGLMVertexProgram:kGLMFragmentProgram,
        static_cast<char *>(output.Base()),"ios-glm-shader-check");
    if (!program->CheckValidity(kGLMGLSL)) {
        gGL->glGetShaderInfoLog(program->m_descs[kGLMGLSL].m_object.glsl,capacity,NULL,detail);
        context->DelProgram(program); return NULL;
    }
    return program;
}
int CountCachedPairs(GLMContext *context)
{
    int count=0;
    for (int i=0;i<(1<<20);++i) {
        GLMShaderPairInfo info={};
        context->QueryShaderPair(i,&info);
        if (info.m_status<0) return count;
        if (info.m_status==1) ++count;
    }
    return -1;
}
bool WriteBuffer(CGLMBuffer *buffer, const void *data, size_t size)
{
    GLMBuffLockParams lock={};
    lock.m_nSize=size; lock.m_bDiscard=true;
    char *address=NULL;
    buffer->Lock(&lock,&address);
    if (!address) return false;
    memcpy(address,data,size);
    buffer->Unlock();
    return gGL->glGetError()==GL_NO_ERROR;
}
}

bool CheckToGLESShaderDraw(GLMContext *context, char *detail, size_t capacity)
{
    CGLMProgram *vertex=MakeProgram(context,true,false,detail,capacity);
    if (!vertex) return false;
    CGLMProgram *fragment=MakeProgram(context,false,false,detail,capacity);
    CGLMProgram *tinted=fragment ? MakeProgram(context,false,true,detail,capacity) : NULL;
    if (!tinted) {
        if (fragment) context->DelProgram(fragment);
        context->DelProgram(vertex); return false;
    }
    // Both stages compile, but incompatible varyings must fail at link time.
    char incompatibleSource[]="#version 300 es\nprecision highp float;\nin vec3 oT0;\nout vec4 color;\nvoid main(){color=vec4(oT0,1.0);}";
    CGLMProgram *incompatible=context->NewProgram(kGLMFragmentProgram,incompatibleSource,"ios-incompatible-varying");
    GLuint vao=0;
    gGL->glGenVertexArrays(1,&vao); gGL->glBindVertexArray(vao);
    const float vertices[]={-1,-1,0.5,1, 0,0,0,1, 3,-1,0.5,1, 2,0,0,1, -1,3,0.5,1, 0,2,0,1};
    const unsigned short indices[]={0,1,2};
    CGLMBuffer *vbo=context->NewBuffer(kGLMVertexBuffer,sizeof(vertices),GLMBufferOptionDynamic);
    CGLMBuffer *ibo=context->NewBuffer(kGLMIndexBuffer,sizeof(indices),GLMBufferOptionDynamic);
    bool valid=WriteBuffer(vbo,vertices,sizeof(vertices)) && WriteBuffer(ibo,indices,sizeof(indices));
    snprintf(detail,capacity,"CGLMBuffer lock/upload failed");

    GLMTexLayoutKey key={};
    key.m_texGLTarget=GL_TEXTURE_2D; key.m_texFormat=D3DFMT_DXT3;
    key.m_xSize=4; key.m_ySize=4; key.m_zSize=1;
    CGLMTex *texture=context->NewTex(&key,1,"ios-glm-sampled-texture");
    GLMTexLockParams lock={};
    lock.m_tex=texture; lock.m_region.xmax=4; lock.m_region.ymax=4; lock.m_region.zmax=1;
    char *bytes=NULL; int row=0,depth=0;
    texture->Lock(&lock,&bytes,&row,&depth);
    // Four colors and alpha values per row; independent expected values below.
    const unsigned char block[]={0x50,0xfa,0x50,0xfa,0x50,0xfa,0x50,0xfa,
        0,248,224,7,0xe4,0xe4,0xe4,0xe4};
    if (bytes) memcpy(bytes,block,sizeof(block)); else valid=false;
    texture->Unlock(&lock);
    gGL->glActiveTexture(GL_TEXTURE0);
    gGL->glBindTexture(GL_TEXTURE_2D,texture->GetTexName());
    gGL->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    gGL->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    // Draw into the isolated EGL pbuffer; no GLM FBO/device state is claimed here.
    gGL->glBindFramebuffer(GL_FRAMEBUFFER,0);
    gGL->glViewport(0,0,4,4);
    gGL->glDisable(GL_DEPTH_TEST); gGL->glDisable(GL_CULL_FACE); gGL->glDisable(GL_BLEND);
    gGL->glBindBuffer(GL_ARRAY_BUFFER,vbo->GetHandle());
    gGL->glEnableVertexAttribArray(0); gGL->glEnableVertexAttribArray(1);
    gGL->glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,8*sizeof(float),NULL);
    gGL->glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,8*sizeof(float),(void *)(4*sizeof(float)));
    gGL->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo->GetHandle());
    int draws=0;
    {
        CGLMShaderPair pair(context);
        if (valid) {
            valid=incompatible->CheckValidity(kGLMGLSL);
            pair.SetProgramPair(vertex,incompatible);
            const bool rejected=!pair.ValidateProgramPair();
            const GLenum error=gGL->glGetError();
            valid=valid && rejected && error==GL_NO_ERROR;
            snprintf(detail,capacity,"GLM incompatible pair: %s (GL 0x%x)",valid?"rejected":"FAIL",error);
        }
        // Relink the same pair in both directions and change constants between
        // draws without relinking, checking stale uniform/program state.
        for (int pass=0;pass<4 && valid;++pass) {
            const bool useTint=pass==1 || pass==2;
            if (pass!=2) {
                pair.SetProgramPair(vertex,useTint?tinted:fragment);
                valid=pair.ValidateProgramPair();
                if (!valid) {
                    gGL->glGetProgramInfoLog(pair.m_program,capacity,NULL,detail); break;
                }
            }
            gGL->glUseProgram(pair.m_program);
            if (pair.m_locVertexParams<0 || pair.m_locSamplers[0]<0 || pair.m_locAlphaRef<0 ||
                (useTint && pair.m_locFragmentParams<0)) {
                valid=false; snprintf(detail,capacity,"GLM shader uniform metadata missing"); break;
            }
            const float vertexConstant[]={0,0,2,0}, screen[]={0,0,0,0};
            const float tint[]={pass==1?0.5f:1.0f,pass==2?0.5f:1.0f,1,1};
            gGL->glUniform4fv(pair.m_locVertexParams,1,vertexConstant);
            gGL->glUniform4fv(pair.m_locVertexScreenParams,1,screen);
            if (useTint) gGL->glUniform4fv(pair.m_locFragmentParams,1,tint);
            gGL->glUniform1f(pair.m_locAlphaRef,pass==3?0.5f:0.0f);
            gGL->glClearColor(0,0,1,1); gGL->glClear(GL_COLOR_BUFFER_BIT);
            DrawToGLESIndexed(GL_TRIANGLES,0,2,3,GL_UNSIGNED_SHORT,NULL,0);
            unsigned char pixels[64]={};
            gGL->glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            GLenum error=gGL->glGetError();
            valid=error==GL_NO_ERROR;
            const unsigned char colors[4][4]={{255,0,0,0},{0,255,0,85},{170,85,0,170},{85,170,0,255}};
            for (int pixel=0;pixel<16 && valid;++pixel) {
                float expected[4];
                for (int c=0;c<4;++c) expected[c]=colors[pixel%4][c]*(useTint?tint[c]:1.0f);
                if (pass==3 && pixel%4<2) { expected[0]=expected[1]=0; expected[2]=expected[3]=255; }
                for (int c=0;c<4;++c) if (fabsf(pixels[4*pixel+c]-expected[c])>1.0f) valid=false;
            }
            snprintf(detail,capacity,"GLM shader draw %d: %s (GL 0x%x, first pixel %u/%u/%u/%u)",
                pass,valid?"PASS":"FAIL",error,pixels[0],pixels[1],pixels[2],pixels[3]);
            if (valid) ++draws;
        }
        gGL->glUseProgram(0);
    }
    if (valid) {
        context->LinkShaderPair(vertex,fragment);
        valid=CountCachedPairs(context)==1;
        context->LinkShaderPair(vertex,fragment);
        valid=valid && CountCachedPairs(context)==1;
        context->LinkShaderPair(vertex,tinted);
        valid=valid && CountCachedPairs(context)==2;
        context->ClearShaderPairCache();
        valid=valid && CountCachedPairs(context)==0 && gGL->glGetError()==GL_NO_ERROR;
        if (!valid) snprintf(detail,capacity,"GLM shader-pair cache reuse/purge failed");
    }
    gGL->glBindVertexArray(0);
    context->DelBuffer(ibo); context->DelBuffer(vbo);
    gGL->glDeleteVertexArrays(1,&vao);
    context->DelTex(texture);
    context->DelProgram(incompatible);
    context->DelProgram(tinted); context->DelProgram(fragment); context->DelProgram(vertex);
    GLenum error=gGL->glGetError();
    if (error!=GL_NO_ERROR) { valid=false; snprintf(detail,capacity,"GLM shader cleanup: GL 0x%x",error); }
    if (valid) snprintf(detail,capacity,"%d GLM shader/buffer draws: PASS",draws);
    return valid;
}
