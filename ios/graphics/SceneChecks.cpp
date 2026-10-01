#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "mathlib/vmatrix.h"
#include "mathlib/lightdesc.h"
#include "SceneChecks.h"
#include <math.h>

bool UsesSRGBColorTarget()
{
    GLint framebuffer=0,encoding=GL_LINEAR;
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
    gGL->glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
        framebuffer ? GL_COLOR_ATTACHMENT0 : GL_BACK,GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,&encoding);
    return encoding==GL_SRGB;
}

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
// Source's non-bump VertexLitGeneric evaluates local lights at vertices.
// Interpolate those vertex values at the ray hit, not a per-pixel light value.
float LocalLightAtVertex(const float vertex[3], int face, float sign,
                         float angle, bool spot)
{
    const float worldPosition[3]={-1.4f,1.6f,-2.4f};
    float position[3]; InverseRotation(worldPosition,angle,position);
    float delta[3],distanceSquared=0;
    for (int i=0;i<3;++i) { delta[i]=position[i]-vertex[i]; distanceSquared+=delta[i]*delta[i]; }
    float distance=sqrtf(distanceSquared);
    float result=fmaxf(0,sign*delta[face]/distance)/(.4f+.2f*distance+.15f*distanceSquared);
    if (spot) {
        float length=sqrtf(1.4f*1.4f+1.6f*1.6f+2.4f*2.4f);
        float worldDirection[3]={1.4f/length,-1.6f/length,2.4f/length},direction[3];
        InverseRotation(worldDirection,angle,direction);
        float cosine=0;
        for (int i=0;i<3;++i) cosine-=direction[i]*delta[i]/distance;
        float ramp=fmaxf(0,(cosine-cosf(.35f))/(cosf(.125f)-cosf(.35f)));
        result*=fminf(1,ramp*ramp);
    }
    return result;
}
float InterpolatedLocalLight(const float point[3], int face, float angle, bool spot)
{
    int x=(face+1)%3,y=(face+2)%3;
    float a=(point[x]/halfSize+1)*.5f,b=(point[y]/halfSize+1)*.5f;
    float corners[4][3]={};
    const int coordinates[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
    float value[4];
    for (int i=0;i<4;++i) {
        corners[i][face]=point[face]>0 ? halfSize : -halfSize;
        corners[i][x]=coordinates[i][0]*halfSize; corners[i][y]=coordinates[i][1]*halfSize;
        value[i]=LocalLightAtVertex(corners[i],face,point[face]>0 ? 1 : -1,angle,spot);
    }
    // Barycentric weights in the actual object-space triangle automatically
    // account for perspective-correct interpolation at the visible ray hit.
    return b<=a ? (1-a)*value[0]+(a-b)*value[1]+b*value[2]
                : (1-b)*value[0]+a*value[2]+(b-a)*value[3];
}
// Independent ray/box reference: it does not project GPU triangles or reuse
// the engine's MVP multiplication. Exclude edge samples where rasterization
// and texture filtering conventions can legitimately change the byte result.
bool ReferencePixel(float u, float v, float aspect, float angle, unsigned lighting, unsigned char rgba[4], bool &hit)
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
    memcpy(rgba,colors[(tv>.5f)*2+(tu>.5f)],4);
    if (lighting) {
        const float towardLight[3]={-.48f,.64f,-.6f};
        const float ambient[3]={.08f,.12f,.16f}, color[3]={.6f,.45f,.3f};
        float localLight[3]; InverseRotation(towardLight,angle,localLight);
        float cosine=fmaxf(0,localLight[face]*(point[face]>0 ? 1 : -1));
        float intensity=lighting==6 ? 0 : lighting>=3
            ? InterpolatedLocalLight(point,face,angle,lighting>=4) : (lighting==2 ? cosine : 0);
        const float secondToward[3]={.6f,0,-.8f},secondColor[3]={.18f,.32f,.5f};
        float secondLocal[3]; InverseRotation(secondToward,angle,secondLocal);
        float secondIntensity=lighting>=5 ? fmaxf(0,secondLocal[face]*(point[face]>0 ? 1 : -1)) : 0;
        for (int i=0;i<3;++i) {
            float linear=ambient[i]+color[i]*intensity+secondColor[i]*secondIntensity;
            float encoded=UsesSRGBColorTarget()
                ? (linear<=.0031308f ? 12.92f*linear : 1.055f*powf(linear,1.f/2.4f)-.055f)
                : powf(linear,1.f/2.2f);
            rgba[i]=static_cast<unsigned char>(rgba[i]*encoded+.5f);
        }
    }
    return true;
}
}

bool DrawPerspectiveScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity)
{
    samples.count=0;
    unsigned lighting=SceneLightingMode(frame);
    if (lighting==11) return DrawLightmapScene(material,context,width,height,frame,samples,detail,capacity,true);
    if (lighting==10) return DrawLightmapScene(material,context,width,height,frame,samples,detail,capacity);
    if (lighting==9) return DrawFogScene(material,context,width,height,frame,samples,detail,capacity);
    if (lighting==8) return DrawBlendScene(material,context,width,height,frame,samples,detail,capacity);
    IMaterial *draw=material->FindMaterial(lighting ? "ios/lit" : "ios/unlit",TEXTURE_GROUP_OTHER,true);
    if (!draw || draw->IsErrorMaterial() || Q_stricmp(draw->GetShaderName(),lighting ? "VertexLitGeneric" : "UnlitGeneric")) {
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
    Vector4D ambient[6];
    for (auto &face : ambient) face.Init(lighting ? .08f : 0,lighting ? .12f : 0,lighting ? .16f : 0,0);
    context->SetAmbientLightCube(ambient);
    LightDesc_t light;
    // Initialize every field consumed by SetLight, even for a directional light.
    memset(&light,0,sizeof(light));
    light.m_Type=lighting==6 ? MATERIAL_LIGHT_DISABLE : lighting>=4 ? MATERIAL_LIGHT_SPOT : lighting==3 ? MATERIAL_LIGHT_POINT
                : lighting==2 ? MATERIAL_LIGHT_DIRECTIONAL : MATERIAL_LIGHT_DISABLE;
    light.m_Direction.Init(.48f,-.64f,.6f);
    light.m_Color.Init(.6f,.45f,.3f); light.m_Attenuation0=1;
    if (lighting>=3) {
        light.m_Position.Init(-1.4f,1.6f,-2.4f);
        light.m_Direction=-light.m_Position; VectorNormalize(light.m_Direction);
        light.m_Attenuation0=.4f; light.m_Attenuation1=.2f; light.m_Attenuation2=.15f;
        light.m_Range=100; light.m_Theta=.25f; light.m_Phi=.7f; light.m_Falloff=2;
    }
    LightDesc_t second;
    memset(&second,0,sizeof(second));
    second.m_Type=lighting>=5 ? MATERIAL_LIGHT_DIRECTIONAL : MATERIAL_LIGHT_DISABLE;
    second.m_Direction.Init(-.6f,0,.8f);
    second.m_Color.Init(.18f,.32f,.5f); second.m_Attenuation0=1;
    // Always update both slots: returning to one/no light must remove stale state.
    context->SetLight(lighting==7 ? 1 : 0,light);
    context->SetLight(lighting==7 ? 0 : 1,second);
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
        if (!ReferencePixel(sample.u,sample.v,aspect,angle,lighting,sample.rgba,hit)) continue;
        // Grazing faces can have large UV derivatives. Require a stable pixel
        // footprint too, rather than relying only on a fixed UV edge margin.
        sample.tolerance=lighting && hit ? 2 : 0;
        bool stable=true;
        for (int dy=-1;dy<=1 && stable;++dy) for (int dx=-1;dx<=1 && stable;++dx) {
            unsigned char nearby[4]; bool nearbyHit=false;
            stable=ReferencePixel(sample.u+2.f*dx/width,sample.v+2.f*dy/height,
                aspect,angle,lighting,nearby,nearbyHit) && nearbyHit==hit && MatchesSceneSample(nearby,sample);
        }
        if (!stable) continue;
        unsigned char pixel[4]={}; gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        valid=MatchesSceneSample(pixel,sample);
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
