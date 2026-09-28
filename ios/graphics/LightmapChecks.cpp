#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "SceneChecks.h"
#include <math.h>
#include <vector>

namespace {
int page=-1,offsets[2][2],pageWidth,pageHeight;
unsigned uploaded=~0u;
IMaterialSystem *owner=NULL;
void RestoreLightmapScene(int) { uploaded=~0u; }
const float levels[4]={.125f,.25f,.5f,1.f};
unsigned char LightmapByte(float value)
{
    // Independent LDR upload reference: gamma 2.2, overbright 2, byte rounding.
    float encoded=floorf(powf(value,1.f/2.2f)*.5f*255+.5f)/255;
    float linear=encoded<=.04045f ? encoded/12.92f : powf((encoded+.055f)/1.055f,2.4f);
    linear=fminf(1,linear*powf(2,2.2f));
    float result=gGL->m_bHave_GL_EXT_sRGB_write_control
        ? (linear<=.0031308f ? linear*12.92f : 1.055f*powf(linear,1.f/2.4f)-.055f)
        : powf(linear,1.f/2.2f);
    return static_cast<unsigned char>(result*255+.5f);
}
}
void ResetLightmapScene()
{
    if (owner) owner->RemoveRestoreFunc(RestoreLightmapScene);
    owner=NULL; page=-1; uploaded=~0u;
}

bool DrawLightmapScene(IMaterialSystem *material, IMatRenderContext *context,
    int width,int height,unsigned frame,SceneSamples &samples,char *detail,size_t capacity)
{
    samples.count=0;
    IMaterial *draw=material->FindMaterial("ios/lightmapped",TEXTURE_GROUP_OTHER,true);
    if (!draw || draw->IsErrorMaterial() || Q_stricmp(draw->GetShaderName(),"LightmappedGeneric")) {
        snprintf(detail,capacity,"LightmappedGeneric lookup failed"); return false;
    }
    draw->IncrementReferenceCount(); context->Bind(draw);
    if (page<0) {
        context->Flush();
        material->BeginLightmapAllocation();
        int first=material->AllocateLightmap(4,4,offsets[0],draw);
        int second=material->AllocateLightmap(4,4,offsets[1],draw);
        material->EndLightmapAllocation();
        std::vector<MaterialSystem_SortInfo_t> sort(material->GetNumSortIDs());
        material->GetSortInfo(sort.data());
        if (first<0 || second<0 || first>=int(sort.size()) || second>=int(sort.size())
            || sort[first].lightmapPageID!=sort[second].lightmapPageID
            || (offsets[0][0]==offsets[1][0] && offsets[0][1]==offsets[1][1])) {
            snprintf(detail,capacity,"Lightmap atlas allocation failed");
            draw->DecrementReferenceCount(); return false;
        }
        page=sort[first].lightmapPageID;
        material->GetLightmapPageSize(page,&pageWidth,&pageHeight);
        // Device Reset recreates lightmap textures without their contents.
        // Re-upload both regions on the next draw, even if the phase is unchanged.
        owner=material; owner->AddRestoreFunc(RestoreLightmapScene);
    }
    unsigned phase=(frame/120)%4;
    if (uploaded!=phase) {
        context->Flush();
        int size[2]={4,4};
        // Alternate whole-page batched locks and direct subrectangle locks.
        if (phase%2) material->BeginUpdateLightmaps();
        for (int tile=uploaded==~0u ? 0 : 1;tile<2;++tile) {
            float pixels[4*4*4];
            for (int y=0;y<4;++y) for (int x=0;x<4;++x) {
                int index=(y*4+x)*4;
                for (int c=0;c<3;++c)
                    pixels[index+c]=levels[tile ? (x/2+2*(y/2)+phase+c)%4 : (c+1)%4];
                pixels[index+3]=1;
            }
            material->UpdateLightmap(page,size,offsets[tile],pixels,NULL,NULL,NULL);
        }
        if (phase%2) material->EndUpdateLightmaps();
        uploaded=phase;
    }
    const MaterialMatrixMode_t modes[]={MATERIAL_MODEL,MATERIAL_VIEW,MATERIAL_PROJECTION};
    for (auto mode:modes) { context->MatrixMode(mode); context->PushMatrix(); context->LoadIdentity(); }
    context->FogMode(MATERIAL_FOG_NONE); context->SetToneMappingScaleLinear(Vector(1,1,1));
    context->ClearColor4ub(37,91,163,255); context->ClearBuffers(true,true,true);
    context->BindLightmapPage(page);
    for (int tile=0;tile<2;++tile) {
        context->Bind(draw);
        IMesh *mesh=context->GetDynamicMesh(true);
        CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,2);
        const float vertices[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        for (const auto &v:vertices) {
            builder.Position3f(-1+tile+v[0],-1+2*v[1],.5f);
            if (draw->GetVertexFormat() & VERTEX_NORMAL) builder.Normal3f(0,0,1);
            builder.TexCoord2f(0,v[0],v[1]);
            // Mirror only the lightmap UV to distinguish the two samplers.
            builder.TexCoord2f(1,(offsets[tile][0]+.5f+3*(1-v[0]))/pageWidth,
                (offsets[tile][1]+.5f+3*v[1])/pageHeight);
            builder.AdvanceVertex();
        }
        builder.End(); mesh->Draw();
    }
    context->Flush();
    GLint read=0,write=0;
    gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&write);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,write);
    bool valid=true;
    for (int tile=0;tile<2;++tile) for (int row=0;row<2;++row) for (int col=0;col<2;++col) {
        int x=int((tile*.5f+.125f+.25f*col)*width),y=int((.25f+.5f*row)*height);
        SceneSample sample={}; sample.u=(x+.5f)/width; sample.v=(y+.5f)/height;
        // The D3D vertex translator flips projected Y; framebuffer rows therefore
        // run opposite to this quad's texture V coordinate.
        int textureRow=1-row,base=col+2*textureRow;
        for (int c=0;c<3;++c) {
            bool present=c==0 ? (base==0 || base==3) : c==1 ? (base==1 || base==3) : base==2;
            float value=levels[tile ? (1-col+2*textureRow+phase+c)%4 : (c+1)%4];
            sample.rgba[c]=present ? LightmapByte(value) : 0;
        }
        sample.rgba[3]=255; sample.tolerance=2;
        unsigned char pixel[4]={}; gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if (!MatchesSceneSample(pixel,sample) && valid) {
            snprintf(detail,capacity,"Lightmap phase %u tile %d uv %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u",
                phase,tile,col,row,pixel[0],pixel[1],pixel[2],pixel[3],sample.rgba[0],sample.rgba[1],sample.rgba[2],sample.rgba[3]);
            valid=false;
        }
        samples.points[samples.count++]=sample;
    }
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
    context->BindLightmapPage(MATERIAL_SYSTEM_LIGHTMAP_PAGE_WHITE);
    for (auto mode:modes) { context->MatrixMode(mode); context->PopMatrix(); }
    draw->DecrementReferenceCount(); return valid;
}
