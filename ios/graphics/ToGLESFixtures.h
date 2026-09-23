#pragma once
#include "tier0/basetypes.h"
#include "tier0/dbg.h"
#include "bitmap/imageformat.h"
#include "togles/linuxwin/dxabstract_types.h"
#include "tier1/utlbuffer.h"
#include "dx9asmtogl2.h"
#include <stdio.h>

namespace ToGLESFixtures {
// Original, small SM2 fixtures expressed with the engine's D3D9 token constants.
static uint32 Register(unsigned type, unsigned index) {
    return 0x80000000u | ((type << D3DSP_REGTYPE_SHIFT) & D3DSP_REGTYPE_MASK) |
        ((type << D3DSP_REGTYPE_SHIFT2) & D3DSP_REGTYPE_MASK2) | index;
}
static uint32 Dst(unsigned type, unsigned index) { return Register(type, index) | D3DSP_WRITEMASK_ALL; }
static uint32 Src(unsigned type, unsigned index) { return Register(type, index) | D3DVS_NOSWIZZLE; }
static uint32 Op(unsigned opcode, unsigned length) { return opcode | (length << D3DSI_INSTLENGTH_SHIFT); }

inline uint32 *Tokens(bool vertexStage, bool tinted=false)
{
    static uint32 vertex[] = {0xfffe0200,
        Op(D3DSIO_DCL,2), 0x80000000u | D3DDECLUSAGE_POSITION, Dst(D3DSPR_INPUT,0),
        Op(D3DSIO_DCL,2), 0x80000000u | D3DDECLUSAGE_TEXCOORD, Dst(D3DSPR_INPUT,1),
        Op(D3DSIO_MOV,2), Dst(D3DSPR_RASTOUT,0), Src(D3DSPR_INPUT,0),
        Op(D3DSIO_MOV,2), Dst(D3DSPR_TEXCRDOUT,0), Src(D3DSPR_INPUT,1), D3DPS_END()};
    static uint32 pixel[] = {0xffff0200,
        Op(D3DSIO_DCL,2), 0x80000000u, Dst(D3DSPR_TEXTURE,0),
        Op(D3DSIO_DCL,2), 0x80000000u | D3DSTT_2D, Dst(D3DSPR_SAMPLER,0),
        Op(D3DSIO_TEX,3), Dst(D3DSPR_TEMP,0), Src(D3DSPR_TEXTURE,0), Src(D3DSPR_SAMPLER,0),
        Op(D3DSIO_MOV,2), Dst(D3DSPR_COLOROUT,0), Src(D3DSPR_TEMP,0), D3DPS_END()};
    static uint32 tintedPixel[] = {0xffff0200,
        Op(D3DSIO_DCL,2), 0x80000000u, Dst(D3DSPR_TEXTURE,0),
        Op(D3DSIO_DCL,2), 0x80000000u | D3DSTT_2D, Dst(D3DSPR_SAMPLER,0),
        Op(D3DSIO_TEX,3), Dst(D3DSPR_TEMP,0), Src(D3DSPR_TEXTURE,0), Src(D3DSPR_SAMPLER,0),
        Op(D3DSIO_MUL,3), Dst(D3DSPR_TEMP,0), Src(D3DSPR_TEMP,0), Src(D3DSPR_CONST,0),
        Op(D3DSIO_MOV,2), Dst(D3DSPR_COLOROUT,0), Src(D3DSPR_TEMP,0), D3DPS_END()};
    return vertexStage ? vertex : tinted ? tintedPixel : pixel;
}

inline bool Translate(bool vertexStage, CUtlBuffer &output, char *detail, size_t capacity, bool tinted=false)
{
    D3DToGL translator;
    bool isVertex = false;
    char label[] = "ios-togles-fixture";
    int status = translator.TranslateShader(Tokens(vertexStage,tinted), &output, &isVertex,
        D3DToGL_OptionUseEnvParams | (vertexStage ? D3DToGL_OptionDoFixupZ : 0), 0, 0, label, false);
    if (status != DISASM_OK || isVertex != vertexStage) {
        snprintf(detail, capacity, "D3D9 translation failed"); return false;
    }
    return true;
}
}
