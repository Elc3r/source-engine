// Original diagnostic shader. Built only into the iOS graphics probe.
#include "BaseVSShader.h"

BEGIN_VS_SHADER( IOSProbe, "Minimal textured material for the iOS port probe" )
    BEGIN_SHADER_PARAMS
    END_SHADER_PARAMS

    SHADER_INIT_PARAMS()
    {
        SET_FLAGS(MATERIAL_VAR_NO_DEBUG_OVERRIDE);
        SET_FLAGS(MATERIAL_VAR_NOFOG);
    }
    SHADER_FALLBACK { return NULL; }
    SHADER_INIT { LoadTexture(BASETEXTURE); }
    SHADER_DRAW
    {
        SHADOW_STATE
        {
            pShaderShadow->EnableTexture(SHADER_SAMPLER0,true);
            pShaderShadow->EnableSRGBRead(SHADER_SAMPLER0,false);
            pShaderShadow->EnableSRGBWrite(false);
            pShaderShadow->EnableDepthTest(false);
            pShaderShadow->EnableDepthWrites(false);
            pShaderShadow->EnableBlending(false);
            pShaderShadow->EnableAlphaTest(false);
            pShaderShadow->EnableCulling(false);
            pShaderShadow->EnableAlphaWrites(true);
            pShaderShadow->VertexShaderVertexFormat(VERTEX_POSITION,1,NULL,0);
            pShaderShadow->SetVertexShader("ios_probe_vs20",0);
            pShaderShadow->SetPixelShader("ios_probe_ps20",0);
        }
        DYNAMIC_STATE
        {
            BindTexture(SHADER_SAMPLER0,BASETEXTURE);
            const float clipConstants[4]={0,0,2,0};
            pShaderAPI->SetVertexShaderConstant(0,clipConstants,1);
            pShaderAPI->SetVertexShaderIndex(0);
            pShaderAPI->SetPixelShaderIndex(0);
        }
        Draw();
    }
END_SHADER
