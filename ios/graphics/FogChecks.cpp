#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "SceneChecks.h"
#include "mathlib/lightdesc.h"
#include <math.h>

namespace {
void FogQuad(IMatRenderContext *context, IMaterial *material,
             float left, float right, float bottom, float top, float depth, int color=0)
{
    context->Bind(material);
    IMesh *mesh=context->GetDynamicMesh(true);
    CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,2);
    const float vertices[6][2]={{left,bottom},{right,bottom},{right,top},
                               {left,bottom},{right,top},{left,top}};
    for (const auto &vertex : vertices) {
        builder.Position3f(vertex[0],vertex[1],depth);
        if (material->GetVertexFormat() & VERTEX_NORMAL) builder.Normal3f(0,0,1);
        builder.TexCoord2f(0,color%2 ? .75f : .25f,color/2 ? .75f : .25f); builder.AdvanceVertex();
    }
    builder.End(); mesh->Draw();
}
unsigned char EncodeFog(float linear)
{
    float encoded=UsesSRGBColorTarget()
        ? (linear<=.0031308f ? 12.92f*linear : 1.055f*powf(linear,1.f/2.4f)-.055f)
        : powf(linear,1.f/2.2f);
    return static_cast<unsigned char>(encoded*255+.5f);
}
}

bool DrawFogScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity)
{
    samples.count=0;
    unsigned variant=(frame/30)%4;
    bool lit=variant%2,translucent=variant>=2;
    const char *names[]={"ios/unlit-fog","ios/lit-fog","ios/unlit-alpha-fog","ios/lit-alpha-fog"};
    IMaterial *fog=material->FindMaterial(names[variant],TEXTURE_GROUP_OTHER,true);
    IMaterial *clear=material->FindMaterial("ios/unlit",TEXTURE_GROUP_OTHER,true);
    if (!fog || !clear || fog->IsErrorMaterial() || clear->IsErrorMaterial()
        || Q_stricmp(fog->GetShaderName(),lit ? "VertexLitGeneric" : "UnlitGeneric")) {
        snprintf(detail,capacity,"Fog material lookup failed"); return false;
    }
    fog->IncrementReferenceCount(); clear->IncrementReferenceCount();
    const MaterialMatrixMode_t modes[]={MATERIAL_MODEL,MATERIAL_VIEW,MATERIAL_PROJECTION};
    for (auto mode : modes) { context->MatrixMode(mode); context->PushMatrix(); context->LoadIdentity(); }
    unsigned phase=(frame/120)%4;
    float maximum=phase==1 ? .5f : 1;
    bool enabled=phase!=2,green=phase==3;
    context->SetToneMappingScaleLinear(Vector(1,1,1));
    context->FogMode(enabled ? MATERIAL_FOG_LINEAR : MATERIAL_FOG_NONE);
    context->FogStart(.3f); context->FogEnd(.7f); context->FogMaxDensity(maximum);
    context->FogColor3ub(0,green ? 255 : 0,green ? 0 : 255);
    Vector4D ambient[6];
    for (auto &face : ambient) face.Init(lit ? .2f : 0,lit ? .3f : 0,lit ? .4f : 0,0);
    context->SetAmbientLightCube(ambient);
    LightDesc_t light; memset(&light,0,sizeof(light));
    light.m_Type=lit ? MATERIAL_LIGHT_DIRECTIONAL : MATERIAL_LIGHT_DISABLE;
    light.m_Direction.Init(0,0,-1); light.m_Color.Init(.3f,.2f,.1f); light.m_Attenuation0=1;
    context->SetLight(0,light);
    LightDesc_t disabled; memset(&disabled,0,sizeof(disabled)); context->SetLight(1,disabled);
    context->ClearColor4ub(37,91,163,255); context->ClearBuffers(true,true,true);
    FogQuad(context,clear,-1,1,-1,1,.99f,1); // Opaque green destination, alpha=1.
    for (int band=0;band<5;++band)
        FogQuad(context,fog,-1+.4f*band,-1+.4f*(band+1),-1,1,.1f+.2f*band,translucent ? 2 : 0);
    // A material's $nofog override must work without disabling scene fog.
    FogQuad(context,clear,-.15f,.15f,-.15f,.15f,.45f);
    // Rebind the fogged material after the override; this square is fully fogged.
    // It lies over the far band at equal depth and tests restoration in-frame.
    FogQuad(context,fog,.65f,.95f,-.15f,.15f,.9f,translucent ? 2 : 0);
    context->Flush();
    GLint read=0,write=0;
    gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&write);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,write);
    bool valid=true;
    for (int band=0;band<5;++band) for (int row=0;row<3;++row) {
        float u=.1f+.2f*band,v=.25f+.25f*row;
        int x=int(u*width),y=int(v*height);
        SceneSample sample={}; sample.u=(x+.5f)/width; sample.v=(y+.5f)/height;
        float depth=.1f+.2f*band;
        // The production ps_2_b range fog squares its clamped ramp to match
        // the engine's legacy fog appearance. Identity projection makes z
        // directly measurable, including before/start/middle/end/after cases.
        float ramp=enabled ? fmaxf(0,fminf(maximum,(depth-.3f)/.4f)) : 0;
        float amount=ramp*ramp;
        bool override=band==2 && row==1;
        if (override) amount=0;
        float source[3]={0,green ? amount : 0,green ? 0 : amount};
        source[translucent ? 2 : 0]+=(1-amount)*(lit ? .5f : 1);
        const float alpha=128.f/255;
        for (int channel=0;channel<3;++channel) {
            float value=source[channel];
            if (translucent) {
                float destination=channel==1 ? 1 : 0;
                // Fog is applied to the lit source before source-over blending.
                // The square after $nofog draws a second layer in band 4.
                int layers=band==4 && row==1 ? 2 : 1;
                bool srgb=UsesSRGBColorTarget();
                float encoded=powf(value,1.f/2.2f);
                for (int layer=0;layer<layers;++layer)
                    destination=(srgb ? value : encoded)*alpha+destination*(1-alpha);
                sample.rgba[channel]=srgb ? EncodeFog(destination)
                    : static_cast<unsigned char>(destination*255+.5f);
            } else sample.rgba[channel]=EncodeFog(value);
        }
        if (override) { sample.rgba[0]=255; sample.rgba[1]=sample.rgba[2]=0; }
        sample.rgba[3]=255; sample.tolerance=override ? 0 : 2;
        unsigned char pixel[4]={}; gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if (!MatchesSceneSample(pixel,sample) && valid) {
            snprintf(detail,capacity,"Fog variant %u phase %u band %d row %d: %u,%u,%u,%u expected %u,%u,%u,%u",
                variant,phase,band,row,pixel[0],pixel[1],pixel[2],pixel[3],sample.rgba[0],sample.rgba[1],sample.rgba[2],sample.rgba[3]);
            valid=false;
        }
        samples.points[samples.count++]=sample;
    }
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
    context->FogMode(MATERIAL_FOG_NONE); context->FogMaxDensity(1);
    for (auto &face : ambient) face.Init(0,0,0,0);
    context->SetAmbientLightCube(ambient); context->SetLight(0,disabled);
    for (auto mode : modes) { context->MatrixMode(mode); context->PopMatrix(); }
    fog->DecrementReferenceCount(); clear->DecrementReferenceCount();
    return valid;
}
