#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "mathlib/vmatrix.h"
#include "SceneChecks.h"
#include <math.h>

namespace {
const float halfSize=.8f, cameraDistance=5, verticalScale=1.7320508f, tilt=.35f;
void InverseRotation(const float input[3], float angle, float output[3])
{
    float x=cosf(angle)*input[0]-sinf(angle)*input[2];
    float z=sinf(angle)*input[0]+cosf(angle)*input[2];
    output[0]=x;
    output[1]=cosf(tilt)*input[1]+sinf(tilt)*z;
    output[2]=-sinf(tilt)*input[1]+cosf(tilt)*z;
}
bool IntersectBox(const float origin[3], const float direction[3], float size, float &nearT, int &face)
{
    nearT=0; float farT=100;
    face=-1;
    for (int axis=0;axis<3;++axis) {
        if (fabsf(direction[axis])<1e-6f) {
            if (fabsf(origin[axis])>size) return false;
            continue;
        }
        float a=(-size-origin[axis])/direction[axis],b=(size-origin[axis])/direction[axis];
        if (a>b) { float temporary=a; a=b; b=temporary; }
        if (a>nearT) { nearT=a; face=axis; }
        if (b<farT) farT=b;
        if (nearT>farT) return false;
    }
    return face>=0 && farT>0;
}
void FaceUV(const float position[3], int face, float &u, float &v)
{
    u=(position[face==0 ? 2 : 0]/halfSize+1)*.5f;
    v=(1+(face==1 ? position[2] : -position[1])/halfSize)*.5f;
    if (position[face]>0) u=1-u;
}
// Independent ray/box reference: it does not project GPU triangles or reuse
// the engine's MVP multiplication. Exclude edge samples where rasterization
// and texture filtering conventions can legitimately change the byte result.
bool ReferencePixel(float u, float v, float aspect, float angle, unsigned char rgba[4], bool &hit)
{
    const float camera[3]={0,0,-cameraDistance};
    const float ray[3]={(2*u-1)*aspect/verticalScale,(1-2*v)/verticalScale,1};
    float origin[3],direction[3],distance=0; int face=0;
    InverseRotation(camera,angle,origin); InverseRotation(ray,angle,direction);
    hit=IntersectBox(origin,direction,halfSize,distance,face);
    if (!hit) {
        if (IntersectBox(origin,direction,halfSize+.04f,distance,face)) return false;
        const unsigned char background[4]={37,91,163,255};
        memcpy(rgba,background,4); return true;
    }
    float point[3]; for (int i=0;i<3;++i) point[i]=origin[i]+distance*direction[i];
    float tu,tv; FaceUV(point,face,tu,tv);
    if (tu<.04f || tu>.96f || tv<.04f || tv>.96f || fabsf(tu-.5f)<.04f || fabsf(tv-.5f)<.04f) return false;
    const unsigned char colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
    memcpy(rgba,colors[(tv>.5f)*2+(tu>.5f)],4); return true;
}
}

bool DrawPerspectiveScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity)
{
    samples.count=0;
    IMaterial *draw=material->FindMaterial("ios/unlit",TEXTURE_GROUP_OTHER,true);
    if (!draw || draw->IsErrorMaterial() || Q_stricmp(draw->GetShaderName(),"UnlitGeneric")) {
        snprintf(detail,capacity,"Perspective material missing"); return false;
    }
    draw->IncrementReferenceCount();
    float angle=.45f+float(frame%1200)*.0052359878f;
    float aspect=float(width)/height;
    VMatrix model,view,projection;
    model.Identity(); view.Identity(); projection.Init(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0);
    float cy=cosf(angle),sy=sinf(angle),cx=cosf(tilt),sx=sinf(tilt);
    model[0][0]=cy; model[0][1]=sy*sx; model[0][2]=sy*cx;
    model[1][1]=cx; model[1][2]=-sx;
    model[2][0]=-sy; model[2][1]=cy*sx; model[2][2]=cy*cx;
    view[2][3]=cameraDistance;
    projection[0][0]=verticalScale/aspect; projection[1][1]=verticalScale;
    projection[2][2]=20.f/19.f; projection[2][3]=-20.f/19.f; projection[3][2]=1;
    const MaterialMatrixMode_t modes[]={MATERIAL_MODEL,MATERIAL_VIEW,MATERIAL_PROJECTION};
    const VMatrix matrices[]={model,view,projection};
    for (int i=0;i<3;++i) { context->MatrixMode(modes[i]); context->PushMatrix(); context->LoadMatrix(matrices[i]); }
    context->FogMode(MATERIAL_FOG_NONE); context->SetToneMappingScaleLinear(Vector(1,1,1));
    context->ClearColor4ub(37,91,163,255); context->ClearBuffers(true,true,true);
    context->Bind(draw);
    IMesh *mesh=context->GetDynamicMesh(true);
    CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,12);
    const int corners[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    for (int index=0;index<6;++index) {
        int side=frame%2 ? 5-index : index; // Reverse submission order to test depth occlusion.
        int axis=side/2;
        for (const auto &corner : corners) {
            float position[3]={},normal[3]={};
            position[axis]=(side%2 ? 1 : -1)*halfSize; normal[axis]=side%2 ? 1 : -1;
            position[(axis+1)%3]=corner[0]*halfSize; position[(axis+2)%3]=corner[1]*halfSize;
            float u,v; FaceUV(position,axis,u,v);
            builder.Position3f(position[0],position[1],position[2]);
            if (draw->GetVertexFormat() & VERTEX_NORMAL) builder.Normal3f(normal[0],normal[1],normal[2]);
            builder.TexCoord2f(0,u,v); builder.AdvanceVertex();
        }
    }
    builder.End(); mesh->Draw(); context->Flush();
    GLint read=0,write=0;
    gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&write);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,write);
    bool valid=true; int hits=0,backgrounds=0;
    const float offsets[7]={-.45f,-.14f,-.07f,0,.07f,.14f,.45f};
    for (int row=0;row<7 && valid;++row) for (int column=0;column<7 && valid;++column) {
        int x=int(width*.5f+offsets[column]*height);
        int y=int(height*.5f+offsets[row]*height);
        if (x<0) x=0;
        if (x>=width) x=width-1;
        SceneSample sample={}; sample.u=float(x+.5f)/width; sample.v=float(y+.5f)/height;
        bool hit=false;
        if (!ReferencePixel(sample.u,sample.v,aspect,angle,sample.rgba,hit)) continue;
        // Grazing faces can have large UV derivatives. Require a stable pixel
        // footprint too, rather than relying only on a fixed UV edge margin.
        bool stable=true;
        for (int dy=-1;dy<=1 && stable;++dy) for (int dx=-1;dx<=1 && stable;++dx) {
            unsigned char nearby[4]; bool nearbyHit=false;
            stable=ReferencePixel(sample.u+2.f*dx/width,sample.v+2.f*dy/height,
                aspect,angle,nearby,nearbyHit) && nearbyHit==hit && !memcmp(nearby,sample.rgba,4);
        }
        if (!stable) continue;
        unsigned char pixel[4]={}; gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        valid=!memcmp(pixel,sample.rgba,4);
        if (!valid) snprintf(detail,capacity,"Perspective frame %u pixel %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u",
            frame,x,y,pixel[0],pixel[1],pixel[2],pixel[3],sample.rgba[0],sample.rgba[1],sample.rgba[2],sample.rgba[3]);
        samples.points[samples.count++]=sample;
        if (hit) ++hits; else ++backgrounds;
    }
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
    for (auto mode : modes) { context->MatrixMode(mode); context->PopMatrix(); }
    draw->DecrementReferenceCount();
    if (valid && (hits<3 || backgrounds<3)) {
        snprintf(detail,capacity,"Insufficient perspective coverage: %d object / %d background",hits,backgrounds); valid=false;
    }
    return valid;
}
