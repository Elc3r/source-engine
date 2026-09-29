#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "SceneChecks.h"
#include "BspGeometry.h"
#include "WorldSurfaceBridge.h"
#include "mathlib/vmatrix.h"
#include <math.h>
#include <vector>

namespace {
int page=-1,offsets[2][2];
unsigned uploaded=~0u;
IMaterialSystem *owner=NULL;
void RestoreLightmapScene(int) { uploaded=~0u; }
BspRenderGeometry geometry;
std::vector<WorldSurfaceBinding> worldBindings;
bool spatialGeometry=false;
const float levels[4]={32.f/255,64.f/255,128.f/255,1.f};
const float guard[3]={32.f/255,64.f/255,.5f};
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
float DecodeLightmap(float value)
{
    value=floorf(value*1024+.5f)/1024;
    float encoded=floorf(powf(value,1.f/2.2f)*.5f*255+.5f)/255;
    return encoded<=.04045f ? encoded/12.92f : powf((encoded+.055f)/1.055f,2.4f);
}
unsigned char EncodeLight(float linear)
{
    linear=fminf(1,linear*powf(2,2.2f));
    float value=gGL->m_bHave_GL_EXT_sRGB_write_control
        ? (linear<=.0031308f ? 12.92f*linear : 1.055f*powf(linear,1.f/2.4f)-.055f)
        : powf(linear,1.f/2.2f);
    return static_cast<unsigned char>(value*255+.5f);
}
// Analytic fixture reference, independent of BSP parsing, triangulation and
// engine matrix operations. Rays intersect z=.5 and z=.4*x-.4 from (cameraX,0,-4).
bool SpatialReference(float u,float v,float aspect,float cameraX,unsigned phase,
    unsigned char rgba[4],int &hit)
{
    float dx=(2*u-1)*aspect/1.7320508f,dy=(1-2*v)/1.7320508f;
    float tx=0,ty=0; hit=-1;
    float best=100;
    for (int face=0;face<2;++face) {
        float t=face ? (3.6f+.4f*cameraX)/(1-.4f*dx) : 4.5f;
        float x=cameraX+t*dx,y=t*dy,extent=face ? .65f : 1;
        if (fabsf(fabsf(x)-extent)<.012f || fabsf(fabsf(y)-extent)<.012f) return false;
        if (t>0 && t<best && fabsf(x)<extent && fabsf(y)<extent) {
            best=t; hit=face; tx=(x/extent+1)*.5f; ty=(y/extent+1)*.5f;
        }
    }
    rgba[3]=255;
    if (hit<0) { rgba[0]=37; rgba[1]=91; rgba[2]=163; return true; }
    if (fabsf(tx-.5f)<.015f || fabsf(ty-.5f)<.015f) return false;
    int base=(tx>=.5f ? 1 : 0)+(ty>=.5f ? 2 : 0);
    float lx=3*(1-tx),ly=3*ty;
    int x0=int(lx),y0=int(ly); float fx=lx-x0,fy=ly-y0;
    for (int c=0;c<3;++c) {
        bool present=c==0 ? (base==0 || base==3) : c==1 ? (base==1 || base==3) : base==2;
        float linear=0;
        for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
            int xx=x0+x,yy=y0+y; if (xx>3) xx=3; if (yy>3) yy=3;
            float value=hit ? levels[(xx/2+2*(yy/2)+phase+c)%4] : guard[c];
            linear+=DecodeLightmap(value)*(x ? fx : 1-fx)*(y ? fy : 1-fy);
        }
        rgba[c]=present ? EncodeLight(linear) : 0;
    }
    return true;
}
bool CheckSpatialBspPixels(int width,int height,float cameraX,unsigned phase,
    SceneSamples &samples,char *detail,size_t capacity)
{
    const float positions[7]={-.43f,-.17f,-.105f,.035f,.105f,.17f,.43f};
    int coverage[3]={};
    for (int row=0;row<7;++row) for (int col=0;col<7;++col) {
        int x=int(width*.5f+positions[col]*height),y=int(height*.5f+positions[row]*height);
        if (x<0 || x>=width || y<0 || y>=height) continue;
        SceneSample sample={}; sample.u=(x+.5f)/width; sample.v=(y+.5f)/height;
        int hit=-1;
        if (!SpatialReference(sample.u,sample.v,float(width)/height,cameraX,phase,sample.rgba,hit)) continue;
        sample.tolerance=hit<0 ? 0 : 2;
        bool stable=true;
        for (int dy=-1;dy<=1 && stable;++dy) for (int dx=-1;dx<=1 && stable;++dx) {
            unsigned char nearby[4]; int nearbyHit;
            stable=SpatialReference(sample.u+2.f*dx/width,sample.v+2.f*dy/height,float(width)/height,
                cameraX,phase,nearby,nearbyHit) && hit==nearbyHit && MatchesSceneSample(nearby,sample);
        }
        if (!stable) continue;
        unsigned char pixel[4]; gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if (!MatchesSceneSample(pixel,sample)) {
            snprintf(detail,capacity,"Spatial BSP phase %u face %d pixel %d,%d: %u,%u,%u expected %u,%u,%u",
                phase,hit,x,y,pixel[0],pixel[1],pixel[2],sample.rgba[0],sample.rgba[1],sample.rgba[2]);
            return false;
        }
        ++coverage[hit+1]; samples.points[samples.count++]=sample;
    }
    if (coverage[0]<3 || coverage[1]<3 || coverage[2]<3) {
        snprintf(detail,capacity,"Spatial BSP coverage: background %d rear %d front %d",coverage[0],coverage[1],coverage[2]);
        return false;
    }
    return true;
}

}
void ResetLightmapScene()
{
    if (owner) owner->RemoveRestoreFunc(RestoreLightmapScene);
    owner=NULL; page=-1; uploaded=~0u; geometry.faces.clear(); worldBindings.clear();
}

bool DrawLightmapScene(IMaterialSystem *material, IMatRenderContext *context,
    int width,int height,unsigned frame,SceneSamples &samples,char *detail,size_t capacity,bool spatial)
{
    samples.count=0;
    if (spatial!=spatialGeometry) { ResetLightmapScene(); spatialGeometry=spatial; }
    if (geometry.faces.empty()) {
        if (!LoadBspGeometryFixture(geometry,detail,capacity,spatial)) return false;
        // This scene's independent pixel reference describes our two-face fixture.
        if (geometry.faces.size()!=2 || geometry.faces[0].material!=geometry.faces[1].material
            || geometry.faces[0].lightmapSize[0]!=4 || geometry.faces[0].lightmapSize[1]!=4
            || geometry.faces[1].lightmapSize[0]!=4 || geometry.faces[1].lightmapSize[1]!=4) {
            snprintf(detail,capacity,"Unexpected BSP reference fixture layout"); return false;
        }
    }
    IMaterial *draw=material->FindMaterial(geometry.faces[0].material.c_str(),TEXTURE_GROUP_OTHER,true);
    if (!draw || draw->IsErrorMaterial() || Q_stricmp(draw->GetShaderName(),"LightmappedGeneric")) {
        snprintf(detail,capacity,"LightmappedGeneric lookup failed"); return false;
    }
    draw->IncrementReferenceCount(); context->Bind(draw);
    if (page<0) {
        context->Flush();
        if (!BuildWorldSurfaceBindings(material,draw,geometry,worldBindings,detail,capacity)) {
            draw->DecrementReferenceCount(); return false;
        }
        page=worldBindings[0].page;
        for (int face=0; face<2; ++face) for (int axis=0; axis<2; ++axis)
            offsets[face][axis]=worldBindings[face].offset[axis];
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
                int quadrant=(x/2+2*(y/2)+(tile ? phase : 0))%4;
                int source=((quadrant/2*2+y%2)*4+quadrant%2*2+x%2)*4;
                for (int c=0;c<4;++c) pixels[index+c]=geometry.faces[tile].lighting[source+c];
            }
            material->UpdateLightmap(page,size,offsets[tile],pixels,NULL,NULL,NULL);
        }
        if (phase%2) material->EndUpdateLightmaps();
        uploaded=phase;
    }
    const MaterialMatrixMode_t modes[]={MATERIAL_MODEL,MATERIAL_VIEW,MATERIAL_PROJECTION};
    for (auto mode:modes) { context->MatrixMode(mode); context->PushMatrix(); context->LoadIdentity(); }
    float cameraX=.2f*sinf(float(frame%1200)*6.2831853f/1200);
    if (spatial) {
        VMatrix view,projection; view.Identity(); memset(projection.Base(),0,sizeof(float)*16);
        view[0][3]=-cameraX; view[2][3]=4;
        projection[0][0]=1.7320508f/(float(width)/height); projection[1][1]=1.7320508f;
        projection[2][2]=20.f/19; projection[2][3]=-20.f/19; projection[3][2]=1;
        context->MatrixMode(MATERIAL_VIEW); context->LoadMatrix(view);
        context->MatrixMode(MATERIAL_PROJECTION); context->LoadMatrix(projection);
    }
    context->FogMode(MATERIAL_FOG_NONE); context->SetToneMappingScaleLinear(Vector(1,1,1));
    context->ClearColor4ub(37,91,163,255); context->ClearBuffers(true,true,true);
    context->BindLightmapPage(page);
    for (int index=0;index<2;++index) {
        int tile=spatial && frame%2 ? 1-index : index;
        context->Bind(draw);
        IMesh *mesh=context->GetDynamicMesh(true);
        const auto &face=geometry.faces[tile];
        CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,int(face.vertices.size())-2);
        for (size_t triangle=1;triangle+1<face.vertices.size();++triangle) {
            const size_t indices[3]={0,triangle,triangle+1};
            for (size_t index:indices) {
                const auto &vertex=face.vertices[index];
                builder.Position3fv(vertex.position);
                if (draw->GetVertexFormat() & VERTEX_NORMAL) builder.Normal3fv(vertex.normal);
                builder.TexCoord2fv(0,worldBindings[tile].vertices[index].texture);
                builder.TexCoord2fv(1,worldBindings[tile].vertices[index].lightmap);
                builder.AdvanceVertex();
            }
        }
        builder.End(); mesh->Draw();
    }
    context->Flush();
    GLint read=0,write=0;
    gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&write);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,write);
    bool valid=true;
    if (spatial) valid=CheckSpatialBspPixels(width,height,cameraX,phase,samples,detail,capacity);
    else for (int tile=0;tile<2;++tile) for (int row=0;row<2;++row) for (int col=0;col<2;++col) {
        int x=int((tile*.5f+.125f+.25f*col)*width),y=int((.25f+.5f*row)*height);
        SceneSample sample={}; sample.u=(x+.5f)/width; sample.v=(y+.5f)/height;
        // The D3D vertex translator flips projected Y; framebuffer rows therefore
        // run opposite to this quad's texture V coordinate.
        int textureRow=1-row,base=col+2*textureRow;
        for (int c=0;c<3;++c) {
            bool present=c==0 ? (base==0 || base==3) : c==1 ? (base==1 || base==3) : base==2;
            float value=tile ? levels[(1-col+2*textureRow+phase+c)%4] : guard[c];
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
