#include "render_pch.h"
#include "MapParticles.h"
#include "particles/particles.h"
#include "appframework/IAppSystem.h"
#include "common.h"
#include "cmodel_engine.h"
#include "gl_rmain.h"
#include "host.h"
#include "lightcache.h"
#include "tier3/tier3.h"
#include "materialsystem/MaterialSystemUtil.h"

namespace {
Vector viewOrigin,viewForward;
bool unsupportedQuery=false;
class WorldParticleQuery : public CBaseAppSystem<IParticleSystemQuery> {
public:
    void GetLightingAtPoint(const Vector &pos,Color &tint) override {
        Vector light; ComputeLighting(pos,NULL,true,light,NULL);
        tint.SetColor(clamp(int(light.x*255),0,255),clamp(int(light.y*255),0,255),clamp(int(light.z*255),0,255),255);
    }
    void TraceLine(const Vector &start,const Vector &end,unsigned mask,
        const IHandleEntity *,int,CBaseTrace *out) override {
        Ray_t ray; ray.Init(start,end); trace_t trace;
        CM_BoxTrace(ray,0,mask,true,trace); *out=trace;
    }
    void GetRandomPointsOnControllingObjectHitBox(CParticleCollection *particles,int cp,int count,
        float,int,Vector *points,Vector,Vector *relative,int *indices) override {
        // Fail the render check if a future PCF requires an unbound model.
        // Initialize outputs only to keep that failing frame deterministic.
        unsupportedQuery=true;
        Vector pos; particles->GetControlPointAtTime(cp,particles->m_flCurTime,&pos);
        for (int i=0;i<count;++i) {
            points[i]=pos; if (relative) relative[i].Init(); if (indices) indices[i]=-1;
        }
    }
    Vector GetLocalPlayerPos() override { return viewOrigin; }
    void GetLocalPlayerEyeVectors(Vector *forward,Vector *right,Vector *up) override {
        QAngle angles; VectorAngles(viewForward,angles);
        AngleVectors(angles,forward,right,up);
    }
    float GetPixelVisibility(int *,const Vector &pos,float) override {
        Ray_t ray; ray.Init(viewOrigin,pos); trace_t tr;
        CM_BoxTrace(ray,0,MASK_SOLID_BRUSHONLY,true,tr); return tr.fraction>=.999f?1.f:0.f;
    }
} query;
CUtlVector<CParticleCollection *> collections;
bool operatorsRegistered=false,started=false;
double lastTime=0;
float simTime=0;
void Draw(void *data) {
    CMatRenderContextPtr context(materials);
    context->MatrixMode(MATERIAL_MODEL); context->PushMatrix(); context->LoadIdentity();
    static_cast<CParticleCollection *>(data)->Render(context,false,NULL);
    context->PopMatrix();
}
}
void SourceIOSShutdownMapParticles() {
    FOR_EACH_VEC(collections,i) delete collections[i]; collections.RemoveAll();
    if (started) {
        g_pParticleSystemMgr->ResetRenderCache();
        g_pParticleSystemMgr->UncacheAllParticleSystems();
        g_pParticleSystemMgr->FlushAllSheets();
    }
    started=false; lastTime=0; simTime=0; unsupportedQuery=false;
}
bool SourceIOSInitializeMapParticles() {
    SourceIOSShutdownMapParticles();
    if (!g_pParticleSystemMgr->Init(&query)) return false;
    started=true;
    if (!operatorsRegistered) {
        g_pParticleSystemMgr->AddBuiltinSimulationOperators();
        g_pParticleSystemMgr->AddBuiltinRenderingOperators(); operatorsRegistered=true;
    }
    if (!g_pParticleSystemMgr->ReadParticleConfigFile("particles/cleansers.pcf",true)) return false;
    const char *data=CM_EntityString(); char token[1024],key[1024];
    while (data) {
        data=COM_ParseFile(data,token,sizeof(token)); if (!data || Q_strcmp(token,"{")) break;
        char classname[128]={},effect[128]={}; Vector origin(0,0,0); QAngle angles(0,0,0); bool active=false;
        while (data) {
            data=COM_ParseFile(data,key,sizeof(key)); if (!data || !Q_strcmp(key,"}")) break;
            data=COM_ParseFile(data,token,sizeof(token)); if (!data) break;
            if (!Q_strcmp(key,"classname")) Q_strncpy(classname,token,sizeof(classname));
            else if (!Q_strcmp(key,"effect_name")) Q_strncpy(effect,token,sizeof(effect));
            else if (!Q_strcmp(key,"start_active")) active=atoi(token)!=0;
            else if (!Q_strcmp(key,"origin")) sscanf(token,"%f %f %f",&origin.x,&origin.y,&origin.z);
            else if (!Q_strcmp(key,"angles")) sscanf(token,"%f %f %f",&angles.x,&angles.y,&angles.z);
        }
        if (Q_strcmp(classname,"info_particle_system") || Q_strcmp(effect,"portal_cleanser") || !active) continue;
        CParticleCollection *p=g_pParticleSystemMgr->CreateParticleCollection(effect,0,collections.Count()+1);
        if (!p || !p->IsValid()) { delete p; return false; }
        Vector forward,right,up; AngleVectors(angles,&forward,&right,&up);
        p->SetControlPoint(0,origin); p->SetControlPointOrientation(0,forward,right,up);
        collections.AddToTail(p);
    }
    Msg("iOS particles: %d original portal_cleanser collections\n",collections.Count());
    lastTime=Plat_FloatTime(); return true;
}
int SourceIOSCollectMapParticles(const WorldListInfo_t &world,const Vector &origin,const Vector &forward,
    CUtlVector<SourceIOSTranslucentDraw> &draws,int &active) {
    active=0; if (!started) return 0;
    viewOrigin=origin; viewForward=forward;
    double now=Plat_FloatTime(); float dt=clamp(float(now-lastTime),0.f,.05f); lastTime=now;
    simTime+=dt; g_pParticleSystemMgr->SetLastSimulationTime(simTime);
    CUtlVector<int> leaves; leaves.SetCount(host_state.worldbrush->numleafs); int submitted=0;
    FOR_EACH_VEC(collections,i) {
        CParticleCollection *p=collections[i]; p->Simulate(dt);
        active+=p->m_nActiveParticles;
        Vector mins,maxs; p->GetBounds(&mins,&maxs);
        if (R_CullBox(mins,maxs)) continue;
        int top=0,count=CM_BoxLeafnums(mins,maxs,leaves.Base(),leaves.Count(),&top),leaf=-1;
        for (int v=0;v<world.m_LeafCount && leaf<0;++v) for (int n=0;n<count;++n)
            if (world.m_pLeafList[v]==leaves[n]) { leaf=v; break; }
        if (leaf<0) continue;
        SourceIOSTranslucentDraw draw={p,leaf,DotProduct((mins+maxs)*.5f-origin,forward),Draw};
        int at=0; while (at<draws.Count() && (draws[at].leaf>leaf ||
            (draws[at].leaf==leaf && draws[at].depth>=draw.depth))) ++at;
        draws.InsertBefore(at,draw); ++submitted;
    }
    return unsupportedQuery?-1:submitted;
}
