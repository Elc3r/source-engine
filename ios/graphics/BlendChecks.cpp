#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "SceneChecks.h"
#include <math.h>

namespace {
void DrawLayer(IMatRenderContext *context, IMaterial *material, float depth,
               int flip, float extent=1, bool blue=false)
{
    context->Bind(material);
    IMesh *mesh=context->GetDynamicMesh(true);
    CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,2);
    const float corners[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    for (const auto &corner : corners) {
        float u=(corner[0]+1)*.5f,v=(1-corner[1])*.5f;
        if (flip==1) u=1-u;
        if (flip==2) v=1-v;
        if (extent<1) { u=.25f; v=blue ? .75f : .25f; }
        builder.Position3f(corner[0]*extent,corner[1]*extent,depth);
        if (material->GetVertexFormat() & VERTEX_NORMAL) builder.Normal3f(0,0,1);
        builder.TexCoord2f(0,u,v); builder.AdvanceVertex();
    }
    builder.End(); mesh->Draw();
}
float Decode(float value)
{
    return value<=.04045f ? value/12.92f : powf((value+.055f)/1.055f,2.4f);
}
float Encode(float value)
{
    return value<=.0031308f ? value*12.92f : 1.055f*powf(value,1.f/2.4f)-.055f;
}
const unsigned char colors[4][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,0}};
void BlendReference(unsigned char rgba[4], int source)
{
    const float alpha[4]={1,192.f/255,128.f/255,64.f/255};
    for (int channel=0;channel<3;++channel) {
        float destination=rgba[channel]/255.f;
        // Native sRGB targets blend in linear space; the legacy shader-gamma
        // fallback blends encoded values in its linear attachment.
        bool srgb=gGL->m_bHave_GL_EXT_sRGB_write_control;
        if (srgb) destination=Decode(destination);
        float value=colors[source][channel]/255.f*alpha[source]+destination*(1-alpha[source]);
        if (srgb) value=Encode(value);
        rgba[channel]=static_cast<unsigned char>(value*255+.5f);
    }
}
}

bool DrawBlendScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity)
{
    samples.count=0;
    IMaterial *opaque=material->FindMaterial("ios/unlit",TEXTURE_GROUP_OTHER,true);
    IMaterial *alpha=material->FindMaterial("ios/unlit-alpha",TEXTURE_GROUP_OTHER,true);
    if (!opaque || !alpha || opaque->IsErrorMaterial() || alpha->IsErrorMaterial()
        || Q_stricmp(alpha->GetShaderName(),"UnlitGeneric")) {
        snprintf(detail,capacity,"Blend lookup: opaque=%d alpha=%d shader=%s translucent=%d",
            opaque && !opaque->IsErrorMaterial(),alpha && !alpha->IsErrorMaterial(),
            alpha ? alpha->GetShaderName() : "null",alpha && alpha->IsTranslucent()); return false;
    }
    opaque->IncrementReferenceCount(); alpha->IncrementReferenceCount();
    // FindMaterial returns a queue-friendly wrapper; its translucency query
    // becomes valid only after Bind has precached the real material state.
    context->Bind(alpha);
    if (!alpha->IsTranslucent()) {
        opaque->DecrementReferenceCount(); alpha->DecrementReferenceCount();
        snprintf(detail,capacity,"UnlitGeneric alpha fixture is not translucent"); return false;
    }
    const MaterialMatrixMode_t modes[]={MATERIAL_MODEL,MATERIAL_VIEW,MATERIAL_PROJECTION};
    for (auto mode : modes) { context->MatrixMode(mode); context->PushMatrix(); context->LoadIdentity(); }
    context->FogMode(MATERIAL_FOG_NONE); context->SetToneMappingScaleLinear(Vector(1,1,1));
    context->ClearColor4ub(37,91,163,255); context->ClearBuffers(true,true,true);
    DrawLayer(context,opaque,.8f,0);
    unsigned order=(frame/120)%3;
    DrawLayer(context,alpha,.4f,order==2 ? 2 : 1);
    if (order) DrawLayer(context,alpha,.2f,order==2 ? 1 : 2);
    // Translucent draws must not write depth; opaque draws must restore it.
    DrawLayer(context,opaque,.6f,0,.2f);
    DrawLayer(context,opaque,.7f,0,.15f,true);
    context->Flush();
    GLint read=0,write=0;
    gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&write);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,write);
    bool valid=true;
    const float locations[5]={.125f,.375f,.5f,.625f,.875f};
    for (float v : locations) for (float u : locations) {
        // The center sample tests opaque state/depth restoration. Other center
        // row/column samples fall on a texture boundary and are omitted.
        bool center=u==.5f && v==.5f;
        if (!center && (u==.5f || v==.5f)) continue;
        int x=int(u*width),y=int(v*height);
        SceneSample sample={}; sample.u=(x+.5f)/width; sample.v=(y+.5f)/height;
        sample.rgba[3]=255; sample.tolerance=center ? 0 : 2;
        int quadrant=(v>.5f)*2+(u>.5f);
        memcpy(sample.rgba,colors[center ? 0 : quadrant],3);
        if (!center) {
            BlendReference(sample.rgba,quadrant^(order==2 ? 2 : 1));
            if (order) BlendReference(sample.rgba,quadrant^(order==2 ? 1 : 2));
        }
        unsigned char pixel[4]={}; gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if (!MatchesSceneSample(pixel,sample) && valid) {
            snprintf(detail,capacity,"Blend order %u pixel %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u",
                order,x,y,pixel[0],pixel[1],pixel[2],pixel[3],sample.rgba[0],sample.rgba[1],sample.rgba[2],sample.rgba[3]);
            valid=false;
        }
        samples.points[samples.count++]=sample;
    }
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
    for (auto mode : modes) { context->MatrixMode(mode); context->PopMatrix(); }
    opaque->DecrementReferenceCount(); alpha->DecrementReferenceCount();
    return valid;
}
