// Original diagnostic shader. Built only into the iOS graphics probe.
#include "BaseVSShader.h"

BEGIN_VS_SHADER( IOSProbe, "Minimal textured material for the iOS port probe" )
    BEGIN_SHADER_PARAMS
        SHADER_PARAM( DEPTHENABLE, SHADER_PARAM_TYPE_BOOL, "0", "Enable depth checks for resize fixtures" )
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
            pShaderShadow->EnableDepthTest(params[DEPTHENABLE]->GetIntValue()!=0);
            pShaderShadow->EnableDepthWrites(params[DEPTHENABLE]->GetIntValue()!=0);
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
            // Preserve the engine math-constant contract used by compiled HLSL.
            const float clipConstants[4]={0,1,2,.5f};
            pShaderAPI->SetVertexShaderConstant(0,clipConstants,1);
            pShaderAPI->SetVertexShaderIndex(0);
            pShaderAPI->SetPixelShaderIndex(0);
        }
        Draw();
    }
END_SHADER
