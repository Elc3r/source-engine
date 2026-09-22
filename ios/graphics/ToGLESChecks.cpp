#include <GLES3/gl3.h>
#include "tier0/basetypes.h"
#include "tier0/dbg.h"
#include "bitmap/imageformat.h"
#include "togles/linuxwin/dxabstract_types.h"
#include "tier1/utlbuffer.h"
#include "dx9asmtogl2.h"
#include "ToGLESChecks.h"
#include "ToGLESRuntime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
void DecompressBlockDXT1(uint32_t, uint32_t, uint32_t, const uint8_t *, int, int *, int *, uint32_t *);
void DecompressBlockDXT3(uint32_t, uint32_t, uint32_t, const uint8_t *, int, int *, int *, uint32_t *);
void DecompressBlockDXT5(uint32_t, uint32_t, uint32_t, const uint8_t *, int, int *, int *, uint32_t *);
}

// Original, small SM2 fixtures expressed with the engine's D3D9 token constants.
static uint32 Register(unsigned type, unsigned index) {
    return 0x80000000u | ((type << D3DSP_REGTYPE_SHIFT) & D3DSP_REGTYPE_MASK) |
        ((type << D3DSP_REGTYPE_SHIFT2) & D3DSP_REGTYPE_MASK2) | index;
}
static uint32 Dst(unsigned type, unsigned index) { return Register(type, index) | D3DSP_WRITEMASK_ALL; }
static uint32 Src(unsigned type, unsigned index) { return Register(type, index) | D3DVS_NOSWIZZLE; }
static uint32 Op(unsigned opcode, unsigned length) { return opcode | (length << D3DSI_INSTLENGTH_SHIFT); }

static GLuint Translate(GLenum stage, const char *directory, char *detail, size_t capacity) {
    uint32 vertex[] = {0xfffe0200,
        Op(D3DSIO_DCL,2), 0x80000000u | D3DDECLUSAGE_POSITION, Dst(D3DSPR_INPUT,0),
        Op(D3DSIO_DCL,2), 0x80000000u | D3DDECLUSAGE_TEXCOORD, Dst(D3DSPR_INPUT,1),
        Op(D3DSIO_MOV,2), Dst(D3DSPR_RASTOUT,0), Src(D3DSPR_INPUT,0),
        Op(D3DSIO_MOV,2), Dst(D3DSPR_TEXCRDOUT,0), Src(D3DSPR_INPUT,1), D3DPS_END()};
    uint32 pixel[] = {0xffff0200,
        Op(D3DSIO_DCL,2), 0x80000000u, Dst(D3DSPR_TEXTURE,0),
        Op(D3DSIO_DCL,2), 0x80000000u | D3DSTT_2D, Dst(D3DSPR_SAMPLER,0),
        Op(D3DSIO_TEX,3), Dst(D3DSPR_TEMP,0), Src(D3DSPR_TEXTURE,0), Src(D3DSPR_SAMPLER,0),
        Op(D3DSIO_MOV,2), Dst(D3DSPR_COLOROUT,0), Src(D3DSPR_TEMP,0), D3DPS_END()};
    D3DToGL translator;
    CUtlBuffer output(0, 65536, CUtlBuffer::TEXT_BUFFER);
    bool isVertex = false;
    char label[] = "ios-togles-fixture";
    bool vertexStage = stage == GL_VERTEX_SHADER;
    int status = translator.TranslateShader(vertexStage ? vertex : pixel, &output, &isVertex,
        D3DToGL_OptionUseEnvParams | (vertexStage ? D3DToGL_OptionDoFixupZ : 0), 0, 0, label, false);
    if (status != DISASM_OK || isVertex != vertexStage) {
        snprintf(detail, capacity, "D3D9 translation failed"); return 0;
    }
    const char *source = static_cast<const char *>(output.Base());
    char path[1024];
    snprintf(path, sizeof(path), "%s/togles-%s.glsl", directory, vertexStage ? "vertex" : "fragment");
    FILE *file = fopen(path, "w");
    if (file) { fputs(source, file); fclose(file); }
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glGetShaderInfoLog(shader, (GLsizei)capacity, NULL, detail);
        glDeleteShader(shader); return 0;
    }
    return shader;
}

int RunToGLESChecks(const char *directory, char *detail, size_t capacity) {
    // Restore the host probe's bindings, including on a failed check.
    GLint oldProgram, oldVAO, oldTexture, oldFBO, oldBuffer, oldViewport[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVAO);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    GLuint vertex = Translate(GL_VERTEX_SHADER, directory, detail, capacity);
    if (!vertex) return 0;
    GLuint fragment = Translate(GL_FRAGMENT_SHADER, directory, detail, capacity);
    if (!fragment) { glDeleteShader(vertex); return 0; }
    GLuint program = glCreateProgram(), vao = 0, buffer = 0, fbo = 0, color = 0, texture = 0, indexBuffer = 0;
    glAttachShader(program, vertex); glAttachShader(program, fragment); glLinkProgram(program);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    int passed = 0;
    do {
        if (!linked) { glGetProgramInfoLog(program, (GLsizei)capacity, NULL, detail); break; }
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "sampler0"), 0);
        glUniform4f(glGetUniformLocation(program, "vc[0]"), 0, 0, 2, 0);
        glUniform4f(glGetUniformLocation(program, "vcscreen"), 0, 0, 0, 0);
        glUniform1f(glGetUniformLocation(program, "alpha_ref"), 0);
        // Position.xyzw and UV.xyzw; oversized triangle covers the entire FBO.
        const GLfloat vertices[] = {-1,-1,0.5,1, 0,0,0,1, 3,-1,0.5,1, 2,0,0,1, -1,3,0.5,1, 0,2,0,1};
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        // Names are the translator's D3D input register names (v0/v1).
        GLint position = glGetAttribLocation(program, "v0"), uv = glGetAttribLocation(program, "v1");
        if (position < 0 || uv < 0) { snprintf(detail, capacity, "Translated input attributes unavailable: %d/%d", position, uv); break; }
        glEnableVertexAttribArray(position); glVertexAttribPointer(position, 4, GL_FLOAT, GL_FALSE, 8*sizeof(float), NULL);
        glEnableVertexAttribArray(uv); glVertexAttribPointer(uv, 4, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void *)(4*sizeof(float)));
        glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { snprintf(detail, capacity, "Incomplete texture check framebuffer"); break; }
        glViewport(0,0,4,4);
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // Red/green endpoints; each row uses all four palette entries.
        const uint8_t colors[8] = {0x00,0xf8,0xe0,0x07,0xe4,0xe4,0xe4,0xe4};
        const uint8_t rgb[4][3] = {{255,0,0},{0,255,0},{170,85,0},{85,170,0}};
        bool valid = true;
        for (int format=0; format<3 && valid; ++format) {
            alignas(4) uint8_t block[16] = {0};
            uint32_t decoded[16] = {0};
            int simpleAlpha=0, complexAlpha=0;
            if (format == 0) {
                memcpy(block, colors, 8);
                DecompressBlockDXT1(0,0,4,block,1,&simpleAlpha,&complexAlpha,decoded);
            } else {
                memcpy(block+8,colors,8);
                if (format == 1) {
                    // DXT3: repeated alpha nibbles 0,5,10,15 across each row.
                    for (int j=0;j<8;j+=2) { block[j]=0x50; block[j+1]=0xfa; }
                    DecompressBlockDXT3(0,0,4,block,1,&simpleAlpha,&complexAlpha,decoded);
                } else {
                    // DXT5: alpha endpoints 255/0 and all eight selectors twice.
                    block[0]=255; block[1]=0;
                    uint64_t selectors=0;
                    for (int j=0;j<16;++j) selectors |= uint64_t(j%8) << (j*3);
                    for (int j=0;j<6;++j) block[j+2]=(selectors>>(j*8))&255;
                    DecompressBlockDXT5(0,0,4,block,1,&simpleAlpha,&complexAlpha,decoded);
                }
            }
            uint8_t expected[64];
            const uint8_t alpha5[8]={255,0,218,182,145,109,72,36};
            for (int j=0;j<16;++j) {
                memcpy(expected+4*j, rgb[j%4], 3);
                expected[4*j+3]=format==0 ? 255 : format==1 ? (j%4)*85 : alpha5[j%8];
            }
            valid = memcmp(decoded,expected,64)==0;
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,decoded);
            glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES,0,3);
            uint8_t actual[64]={0};
            glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,actual);
            GLenum error=glGetError();
            valid &= memcmp(actual,expected,64)==0 && error==GL_NO_ERROR;
            if (!valid) snprintf(detail,capacity,"DXT%d CPU/GPU mismatch, GL error 0x%x",format==0?1:format==1?3:5,error);
        }
        if (!valid) break;
        // Verify the translator's shader alpha-test fallback actually discards.
        glUniform1f(glGetUniformLocation(program,"alpha_ref"),0.5f);
        glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES,0,3);
        uint8_t pixels[64]={0}; glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        const uint8_t alpha5[8]={255,0,218,182,145,109,72,36};
        for (int j=0;j<16;++j) {
            uint8_t expected[4]={0,0,255,255};
            if (alpha5[j%8]>=128) { memcpy(expected,rgb[j%4],3); expected[3]=alpha5[j%8]; }
            valid &= memcmp(pixels+4*j,expected,4)==0;
        }
        if (!valid || glGetError()!=GL_NO_ERROR) { snprintf(detail,capacity,"Translated alpha-test pixel mismatch"); break; }
        glGenBuffers(1,&indexBuffer); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
        const GLushort indices[]={0,1,2};
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
        glUniform1f(glGetUniformLocation(program,"alpha_ref"),0);
        // Invalid degenerate vertices ensure ignoring either signed base offset
        // cannot accidentally produce the expected image.
        for (int direction=1; direction>=-1; direction-=2) {
            GLfloat padded[48]={0};
            memcpy(padded+(direction>0 ? 24 : 0),vertices,sizeof(vertices));
            glBindBuffer(GL_ARRAY_BUFFER,buffer);
            glBufferData(GL_ARRAY_BUFFER,sizeof(padded),padded,GL_STATIC_DRAW);
            size_t initialOffset=direction>0 ? 0 : sizeof(vertices);
            glVertexAttribPointer(position,4,GL_FLOAT,GL_FALSE,8*sizeof(float),(void *)initialOffset);
            glVertexAttribPointer(uv,4,GL_FLOAT,GL_FALSE,8*sizeof(float),(void *)(initialOffset+4*sizeof(float)));
            glClear(GL_COLOR_BUFFER_BIT);
            DrawToGLESIndexed(GL_TRIANGLES,0,2,3,GL_UNSIGNED_SHORT,NULL,direction*3);
            uint8_t actual[64]={0}; glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,actual);
            for (int j=0;j<16;++j) {
                uint8_t expected[4]; memcpy(expected,rgb[j%4],3); expected[3]=alpha5[j%8];
                valid &= memcmp(actual+4*j,expected,4)==0;
            }
            void *positionPointer=NULL, *uvPointer=NULL;
            glGetVertexAttribPointerv(position,GL_VERTEX_ATTRIB_ARRAY_POINTER,&positionPointer);
            glGetVertexAttribPointerv(uv,GL_VERTEX_ATTRIB_ARRAY_POINTER,&uvPointer);
            GLint boundBuffer=0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&boundBuffer);
            valid &= positionPointer==(void *)initialOffset && uvPointer==(void *)(initialOffset+4*sizeof(float));
            valid &= boundBuffer==(GLint)buffer && glGetError()==GL_NO_ERROR;
        }
        if (!valid) { snprintf(detail,capacity,"Signed base-vertex draw or VAO restoration failed"); break; }
        snprintf(detail,capacity,"D3D9 shaders; DXT1/3/5; alpha discard; signed base-vertex: PASS");
        passed=1;
    } while (false);
    glBindFramebuffer(GL_FRAMEBUFFER,oldFBO); glUseProgram(oldProgram);
    glBindVertexArray(oldVAO); glBindBuffer(GL_ARRAY_BUFFER,oldBuffer);
    glBindTexture(GL_TEXTURE_2D,oldTexture); glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
    glDeleteTextures(1,&texture); glDeleteTextures(1,&color); glDeleteFramebuffers(1,&fbo);
    glDeleteBuffers(1,&indexBuffer); glDeleteBuffers(1,&buffer); glDeleteVertexArrays(1,&vao); glDeleteProgram(program);
    return passed;
}
