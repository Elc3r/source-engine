#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "SceneChecks.h"
#include <math.h>

namespace {
void FogQuad(IMatRenderContext *context, IMaterial *material,
             float left, float right, float bottom, float top, float depth)
{
    context->Bind(material);
    IMesh *mesh=context->GetDynamicMesh(true);
    CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,2);
    const float vertices[6][2]={{left,bottom},{right,bottom},{right,top},
                               {left,bottom},{right,top},{left,top}};
    for (const auto &vertex : vertices) {
        builder.Position3f(vertex[0],vertex[1],depth);
        if (material->GetVertexFormat() & VERTEX_NORMAL) builder.Normal3f(0,0,1);
        builder.TexCoord2f(0,.25f,.25f); builder.AdvanceVertex();
    }
    builder.End(); mesh->Draw();
}
unsigned char EncodeFog(float linear)
{
    float encoded=gGL->m_bHave_GL_EXT_sRGB_write_control
        ? (linear<=.0031308f ? 12.92f*linear : 1.055f*powf(linear,1.f/2.4f)-.055f)
        : powf(linear,1.f/2.2f);
    return static_cast<unsigned char>(encoded*255+.5f);
}
}

bool DrawFogScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity)
{
    samples.count=0;
    IMaterial *fog=material->FindMaterial("ios/unlit-fog",TEXTURE_GROUP_OTHER,true);
    IMaterial *clear=material->FindMaterial("ios/unlit",TEXTURE_GROUP_OTHER,true);
    if (!fog || !clear || fog->IsErrorMaterial() || clear->IsErrorMaterial()
        || Q_stricmp(fog->GetShaderName(),"UnlitGeneric")) {
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
    context->ClearColor4ub(37,91,163,255); context->ClearBuffers(true,true,true);
    for (int band=0;band<5;++band)
        FogQuad(context,fog,-1+.4f*band,-1+.4f*(band+1),-1,1,.1f+.2f*band);
    // A material's $nofog override must work without disabling scene fog.
    FogQuad(context,clear,-.15f,.15f,-.15f,.15f,.45f);
    // Rebind the fogged material after the override; this square is fully fogged.
    // It lies over the far band at equal depth and tests restoration in-frame.
    FogQuad(context,fog,.65f,.95f,-.15f,.15f,.9f);
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
        sample.rgba[0]=EncodeFog(1-amount);
        sample.rgba[1]=green ? EncodeFog(amount) : 0;
        sample.rgba[2]=green ? 0 : EncodeFog(amount);
        sample.rgba[3]=255; sample.tolerance=override || !enabled ? 0 : 2;
        unsigned char pixel[4]={}; gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if (!MatchesSceneSample(pixel,sample) && valid) {
            snprintf(detail,capacity,"Fog phase %u band %d row %d: %u,%u,%u,%u expected %u,%u,%u,%u",
                phase,band,row,pixel[0],pixel[1],pixel[2],pixel[3],sample.rgba[0],sample.rgba[1],sample.rgba[2],sample.rgba[3]);
            valid=false;
        }
        samples.points[samples.count++]=sample;
    }
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
    context->FogMode(MATERIAL_FOG_NONE); context->FogMaxDensity(1);
    for (auto mode : modes) { context->MatrixMode(mode); context->PopMatrix(); }
    fog->DecrementReferenceCount(); clear->DecrementReferenceCount();
    return valid;
}
