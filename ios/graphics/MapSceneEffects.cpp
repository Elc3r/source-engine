#include "render_pch.h"
#include "MapSceneEffects.h"
#include "common.h"
#include "cmodel_engine.h"
#include "gl_rmain.h"
#include "host.h"
#include "rope_physics.h"
#include "rope_shared.h"
#include "tier2/beamsegdraw.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "tier3/tier3.h"
#include "tier1/KeyValues.h"
#include "filesystem.h"
#include "materialsystem/imaterialvar.h"

namespace {
// Initial, settled geometry only. No game entities, wind or moving parents.
class SettledRope : public CRopePhysics<ROPE_MAX_SEGMENTS> {
public:
    Vector start,end;
    void GetNodeForces(CSimplePhysics::CNode *,int,Vector *accel) override { accel->Init(ROPE_GRAVITY); }
    void ApplyConstraints(CSimplePhysics::CNode *nodes,int count) override {
        CBaseRopePhysics::ApplyConstraints(nodes,count);
        nodes[0].m_vPos=start; nodes[count-1].m_vPos=end;
    }
    void Settle(float length,int count) {
        SetNumNodes(count); SetupSimulation(length/(count-1)); Restart();
        for (int i=0;i<count;++i) {
            Vector p; VectorLerp(start,end,float(i)/(count-1),p); GetNode(i)->Init(p);
        }
        Simulate(5);
    }
};
struct Effect {
    CMaterialReference material;
    CUtlVector<BeamSeg_t> points;
    Vector mins,maxs,center;
    color32 color;
    float radius=0;
    bool spotlight=false;
};
CUtlVector<Effect *> effects;
struct RopeKey {
    Vector origin;
    char name[128]={},next[128]={},material[MAX_PATH]={};
    float width=2,slack=0,scale=1;
    int nodes=ROPE_MAX_SEGMENTS,subdiv=2;
};
void Finish(Effect *e) {
    if (!e->material || e->material->IsErrorMaterial()) { delete e; return; }
    e->center=(e->mins+e->maxs)*.5f; effects.AddToTail(e);
}
void AddRope(const Vector &start,const Vector &end,float width,float slack,
    float scale,int nodes,int subdiv,const char *material,float hang=-1) {
    SettledRope rope; rope.start=start; rope.end=end;
    float length=MAX(0,float(int((end-start).Length()))+slack+ROPESLACK_FUDGEFACTOR);
    if (hang>=0) {
        // Solve length with the original spring solver for the NPC's requested
        // hang below its lower attachment. No guessed camera cable shape.
        float low=(end-start).Length(),high=low+4*hang+32;
        for (int k=0;k<16;++k) {
            length=(low+high)*.5f; rope.Settle(length,nodes);
            float bottom=MIN(start.z,end.z);
            for (int i=0;i<nodes;++i) bottom=MIN(bottom,rope.GetNode(i)->m_vPos.z);
            if (MIN(start.z,end.z)-bottom<hang) low=length; else high=length;
        }
    }
    rope.Settle(length,nodes);
    Effect *e=new Effect; e->material.Init(materials->FindMaterial(material,TEXTURE_GROUP_OTHER));
    e->mins.Init(FLT_MAX,FLT_MAX,FLT_MAX); e->maxs.Init(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    int steps=clamp(subdiv+1,1,9);
    for (int i=0;i<(nodes-1)*steps+1;++i) {
        int n=MIN(i/steps,nodes-2); float t=float(i-n*steps)/steps;
        BeamSeg_t p={};
        const Vector &a=rope.GetNode(MAX(n-1,0))->m_vPos,&b=rope.GetNode(n)->m_vPos;
        const Vector &c=rope.GetNode(n+1)->m_vPos,&d=rope.GetNode(MIN(n+2,nodes-1))->m_vPos;
        Catmull_Rom_Spline(a,b,c,d,t,p.m_vPos);
        p.m_vColor.Init(1,1,1); p.m_flAlpha=1; p.m_flWidth=width;
        p.m_flTexCoord=float(i)/((nodes-1)*steps)*4*length/MAX(scale,.01f)/MAX(e->material->GetMappingHeight(),1);
        e->points.AddToTail(p);
        VectorMin(e->mins,p.m_vPos-Vector(width,width,width),e->mins);
        VectorMax(e->maxs,p.m_vPos+Vector(width,width,width),e->maxs);
    }
    Finish(e);
}
IMaterial *SpriteMaterial(const char *material,int mode) {
    // Match the original sprite loader's separate material per render mode.
    char file[MAX_PATH],variant[MAX_PATH];
    Q_snprintf(file,sizeof(file),"materials/%s.vmt",material);
    Q_snprintf(variant,sizeof(variant),"%s_ios_inspection_%d",material,mode);
    KeyValues *kv=new KeyValues("Sprite");
    if (!kv->LoadFromFile(g_pFullFileSystem,file,"GAME")) { kv->deleteThis(); return NULL; }
    kv->SetInt("$spriteRenderMode",mode);
    kv->SetInt("$ignorevertexcolors",0);
    return materials->FindProceduralMaterial(variant,TEXTURE_GROUP_CLIENT_EFFECTS,kv);
}
void AddGlow(const Vector &pos,const char *material,float radius,color32 color,int mode=kRenderGlow) {
    Effect *e=new Effect; e->material.Init(SpriteMaterial(material,mode));
    e->radius=radius; e->color=color;
    e->mins=pos-Vector(radius,radius,radius); e->maxs=pos+Vector(radius,radius,radius);
    Finish(e);
}
void Draw(void *data) {
    Effect &e=*static_cast<Effect *>(data);
    CMatRenderContextPtr context(materials);
    context->MatrixMode(MATERIAL_MODEL); context->PushMatrix(); context->LoadIdentity();
    if (e.spotlight) {
        // Original lampbeam proxy fades the shaft toward the vertical view.
        Vector camera; context->GetWorldSpaceCameraPosition(&camera);
        Vector direction=e.points[0].m_vPos-camera; VectorNormalize(direction);
        bool found=false; IMaterialVar *alpha=e.material->FindVar("$alpha",&found,false);
        if (found) alpha->SetFloatValue(1-fabs(direction.z));
    }
    context->Bind(e.material);
    if (e.points.Count()) {
        CBeamSegDraw beam; beam.Start(context,e.points.Count(),e.material);
        FOR_EACH_VEC(e.points,i) beam.NextSeg(&e.points[i]);
        beam.End();
    } else {
        VMatrix view; context->GetMatrix(MATERIAL_VIEW,&view);
        Vector right(view[0][0],view[0][1],view[0][2]),up(view[1][0],view[1][1],view[1][2]);
        IMesh *mesh=context->GetDynamicMesh(); CMeshBuilder builder;
        builder.Begin(mesh,MATERIAL_QUADS,1);
        const float x[4]={-1,-1,1,1},y[4]={-1,1,1,-1};
        for (int i=0;i<4;++i) {
            Vector p=e.center+e.radius*(right*x[i]+up*y[i]);
            builder.Position3fv(p.Base()); builder.Color4ub(e.color.r,e.color.g,e.color.b,e.color.a);
            builder.TexCoord2f(0,(x[i]+1)*.5f,(1-y[i])*.5f); builder.AdvanceVertex();
        }
        builder.End(); mesh->Draw();
    }
    context->PopMatrix();
}
}
int SourceIOSSceneEffectCount() { return effects.Count(); }
void SourceIOSShutdownSceneEffects() {
    FOR_EACH_VEC(effects,i) delete effects[i]; effects.RemoveAll();
}
void SourceIOSAddCameraEffects(const Vector &eye,const Vector wires[4]) {
    AddGlow(eye,"sprites/glow1",.15f*materials->FindMaterial("sprites/glow1",TEXTURE_GROUP_OTHER)->GetMappingWidth(),{255,0,0,128},kRenderWorldGlow);
    for (int i=0;i<2;++i) AddRope(wires[2*i],wires[2*i+1],.7f,0,1,ROPE_MAX_SEGMENTS,2,"cable/cable",9);
}
void SourceIOSInitializeSceneEffects() {
    SourceIOSShutdownSceneEffects();
    CUtlVector<RopeKey> keys;
    const char *data=CM_EntityString(); char token[1024],key[1024];
    while (data) {
        data=COM_ParseFile(data,token,sizeof(token)); if (!data || Q_strcmp(token,"{")) break;
        RopeKey rope; char classname[128]={}; QAngle angles(0,0,0); int flags=0;
        color32 color={255,255,255,255}; float spotLength=500,spotWidth=100;
        while (data) {
            data=COM_ParseFile(data,key,sizeof(key)); if (!data || !Q_strcmp(key,"}")) break;
            data=COM_ParseFile(data,token,sizeof(token)); if (!data) break;
            if (!Q_strcmp(key,"classname")) Q_strncpy(classname,token,sizeof(classname));
            else if (!Q_strcmp(key,"origin")) sscanf(token,"%f %f %f",&rope.origin.x,&rope.origin.y,&rope.origin.z);
            else if (!Q_strcmp(key,"angles")) sscanf(token,"%f %f %f",&angles.x,&angles.y,&angles.z);
            else if (!Q_strcmp(key,"targetname")) Q_strncpy(rope.name,token,sizeof(rope.name));
            else if (!Q_strcmp(key,"NextKey")) Q_strncpy(rope.next,token,sizeof(rope.next));
            else if (!Q_strcmp(key,"RopeMaterial")) Q_strncpy(rope.material,token,sizeof(rope.material));
            else if (!Q_strcmp(key,"Width")) rope.width=atof(token);
            else if (!Q_strcmp(key,"Slack")) rope.slack=atof(token);
            else if (!Q_strcmp(key,"TextureScale")) rope.scale=atof(token);
            else if (!Q_strcmp(key,"Subdiv")) rope.subdiv=atoi(token);
            else if (!Q_strcmp(key,"Type")) rope.nodes=atoi(token)==0?10:atoi(token)==1?4:2;
            else if (!Q_strcmp(key,"spawnflags")) flags=atoi(token);
            else if (!Q_strcmp(key,"spotlightlength")) spotLength=atof(token);
            else if (!Q_strcmp(key,"spotlightwidth")) spotWidth=atof(token);
            else if (!Q_strcmp(key,"rendercolor")) {
                int r=255,g=255,b=255; sscanf(token,"%d %d %d",&r,&g,&b); color.r=r; color.g=g; color.b=b;
            }
        }
        if (!Q_strcmp(classname,"move_rope") || !Q_strcmp(classname,"keyframe_rope")) keys.AddToTail(rope);
        if (!Q_strcmp(classname,"point_spotlight") && (flags&1)) {
            AddGlow(rope.origin,"sprites/light_glow03",60,color);
            Vector direction; AngleVectors(angles,&direction); trace_t trace;
            Ray_t ray; ray.Init(rope.origin,rope.origin+direction*spotLength);
            CM_BoxTrace(ray,0,MASK_SOLID_BRUSHONLY,true,trace);
            Effect *e=new Effect; e->material.Init(SpriteMaterial("sprites/glow_test02",kRenderTransAdd));
            e->spotlight=true;
            BeamSeg_t a={rope.origin,Vector(color.r,color.g,color.b)/255.f,0,0,64/255.f};
            BeamSeg_t b={trace.endpos,a.m_vColor,1,spotWidth,0};
            e->points.AddToTail(a); e->points.AddToTail(b);
            VectorMin(a.m_vPos,b.m_vPos,e->mins); VectorMax(a.m_vPos,b.m_vPos,e->maxs);
            e->mins-=Vector(spotWidth,spotWidth,spotWidth); e->maxs+=Vector(spotWidth,spotWidth,spotWidth); Finish(e);
        }
    }
    FOR_EACH_VEC(keys,i) if (keys[i].next[0]) FOR_EACH_VEC(keys,j)
        if (!Q_strcmp(keys[i].next,keys[j].name)) {
            const RopeKey &r=keys[i]; char name[MAX_PATH]; Q_strncpy(name,r.material[0]?r.material:"cable/cable",sizeof(name));
            Q_StripExtension(name,name,sizeof(name));
            AddRope(r.origin,keys[j].origin,r.width,r.slack,r.scale,r.nodes,r.subdiv,name); break;
        }
}
int SourceIOSCollectSceneEffects(const WorldListInfo_t &world,const Vector &origin,const Vector &forward,
    CUtlVector<SourceIOSTranslucentDraw> &draws) {
    CUtlVector<int> leaves; leaves.SetCount(host_state.worldbrush->numleafs); int submitted=0;
    FOR_EACH_VEC(effects,i) {
        Effect &e=*effects[i]; if (R_CullBox(e.mins,e.maxs)) continue;
        int top=0,count=CM_BoxLeafnums(e.mins,e.maxs,leaves.Base(),leaves.Count(),&top),leaf=-1;
        for (int v=0;v<world.m_LeafCount && leaf<0;++v) for (int p=0;p<count;++p)
            if (world.m_pLeafList[v]==leaves[p]) { leaf=v; break; }
        if (leaf<0) continue;
        // World glows must be occluded even when their authored sprite disables
        // depth testing. Trace before drawing, as the client glow visibility does.
        if (e.radius>0) {
            trace_t tr; Ray_t ray; ray.Init(origin,e.center); CM_BoxTrace(ray,0,MASK_SOLID_BRUSHONLY,true,tr);
            if (tr.fraction<.999f) continue;
        }
        SourceIOSTranslucentDraw draw={&e,leaf,DotProduct(e.center-origin,forward),Draw};
        int at=0; while (at<draws.Count() && (draws[at].leaf>leaf ||
            (draws[at].leaf==leaf && draws[at].depth>=draw.depth))) ++at;
        draws.InsertBefore(at,draw); ++submitted;
    }
    return submitted;
}
