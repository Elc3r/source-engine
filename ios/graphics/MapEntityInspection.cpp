#include "render_pch.h"
#include "MapEntityInspection.h"
#include "modelloader.h"
#include "common.h"
#include "cmodel_engine.h"
#include "gl_rmain.h"
#include "gl_rsurf.h"
#include "host.h"
#include "iclientrenderable.h"
#include "datacache/imdlcache.h"
#include "engine/ivmodelrender.h"
#include "studio.h"
#include "bone_setup.h"
#include "tier3/tier3.h"
#include "cdll_engine_int.h"
#include "utlvector.h"

namespace {
// A render-only adapter. It never registers as a networked client entity and
// never claims that BSP keyvalues represent the running game's entity state.
class InspectionModel : public IClientUnknown, public IClientRenderable {
public:
    ClientRenderHandle_t handle=INVALID_CLIENT_RENDER_HANDLE;
    CBaseHandle refHandle;
    IClientUnknown *GetIClientUnknown() override { return this; }
    void SetRefEHandle(const CBaseHandle &value) override { refHandle=value; }
    const CBaseHandle &GetRefEHandle() const override { return refHandle; }
    ICollideable *GetCollideable() override { return NULL; }
    IClientRenderable *GetClientRenderable() override { return this; }
    IClientNetworkable *GetClientNetworkable() override { return NULL; }
    IClientEntity *GetIClientEntity() override { return NULL; }
    C_BaseEntity *GetBaseEntity() override { return NULL; }
    IClientThinkable *GetClientThinkable() override { return NULL; }
    ClientRenderHandle_t &RenderHandle() override { return handle; }
    ClientShadowHandle_t GetShadowHandle() const override { return CLIENTSHADOW_INVALID_HANDLE; }
    void ComputeFxBlend() override {}
    int GetFxBlend() override { return 255; }
    bool LODTest() override { return true; }
    bool IsTwoPass() override { return false; }
    void OnThreadedDrawSetup() override {}
    bool UsesFlexDelayedWeights() override { return false; }
    void SetupWeights(const matrix3x4_t *,int count,float *weights,float *delayed) override {
        for (int i=0;i<count;++i) { weights[i]=0; if (delayed) delayed[i]=0; }
    }
    void DoAnimationEvents() override {}
    IPVSNotify *GetPVSNotifyInterface() override { return NULL; }
    void GetColorModulation(float *color) override { color[0]=color[1]=color[2]=1; }
    bool UsesPowerOfTwoFrameBufferTexture() override { return false; }
    bool UsesFullFrameBufferTexture() override { return false; }
    void GetShadowRenderBounds(Vector &mins,Vector &maxs,ShadowType_t) override { GetRenderBounds(mins,maxs); }
    bool ShouldReceiveProjectedTextures(int) override { return false; }
    bool GetShadowCastDistance(float *,ShadowType_t) const override { return false; }
    bool GetShadowCastDirection(Vector *,ShadowType_t) const override { return false; }
    bool IsShadowDirty() override { return false; }
    void MarkShadowDirty(bool) override {}
    IClientRenderable *GetShadowParent() override { return NULL; }
    IClientRenderable *FirstShadowChild() override { return NULL; }
    IClientRenderable *NextShadowPeer() override { return NULL; }
    ShadowType_t ShadowCastType() override { return SHADOWS_NONE; }
    void CreateModelInstance() override {}
    ModelInstanceHandle_t GetModelInstance() override { return MODEL_INSTANCE_INVALID; }
    int LookupAttachment(const char *) override { return -1; }
    bool GetAttachment(int,Vector &,QAngle &) override { return false; }
    bool GetAttachment(int,matrix3x4_t &) override { return false; }
    float *GetRenderClipPlane() override { return NULL; }
    void RecordToolMessage() override {}
    bool IgnoresZBuffer() const override { return false; }
    model_t *model=NULL;
    Vector origin;
    QAngle angles;
    matrix3x4_t transform;
    int skin=0,body=0,index=-1;
    char animation[128]={};
    const Vector &GetRenderOrigin() override { return origin; }
    const QAngle &GetRenderAngles() override { return angles; }
    const matrix3x4_t &RenderableToWorldTransform() override { return transform; }
    const model_t *GetModel() const override { return model; }
    bool ShouldDraw() override { return model!=NULL; }
    bool IsTransparent() override { return (model->flags&MODELFLAG_TRANSLUCENT)!=0; }
    int GetSkin() override { return skin; }
    int GetBody() override { return body; }
    void GetRenderBounds(Vector &mins,Vector &maxs) override { mins=model->mins; maxs=model->maxs; }
    void GetRenderBoundsWorldspace(Vector &mins,Vector &maxs) override {
        TransformAABB(transform,model->mins,model->maxs,mins,maxs);
    }
    bool SetupBones(matrix3x4_t *out,int capacity,int mask,float time) override {
        studiohdr_t *hdr=g_pMDLCache->GetStudioHdr(model->studio);
        if (!hdr || hdr->numbones<1 || hdr->numbones>MAXSTUDIOBONES || capacity<hdr->numbones) return false;
        if (!out) return true;
        CStudioHdr studio(hdr,g_pMDLCache);
        float pose[MAXSTUDIOPOSEPARAM]={};
        Vector positions[MAXSTUDIOBONES]; Quaternion rotations[MAXSTUDIOBONES];
        IBoneSetup setup(&studio,BONE_USED_BY_ANYTHING,pose);
        setup.InitPose(positions,rotations);
        int sequence=0;
        if (animation[0]) for (int i=0;i<studio.GetNumSeq();++i)
            if (!Q_stricmp(studio.pSeqdesc(i).pszLabel(),animation)) { sequence=i; break; }
        if (studio.GetNumSeq()>0) setup.AccumulatePose(positions,rotations,sequence,0,1,0,NULL);
        Studio_BuildMatrices(&studio,angles,origin,positions,rotations,-1,1,out,BONE_USED_BY_ANYTHING);
        return true;
    }
    int DrawModel(int flags) override {
        ModelRenderInfo_t info;
        info.origin=origin; info.angles=angles; info.pRenderable=this; info.pModel=model;
        info.pModelToWorld=&transform; info.flags=flags; info.entity_index=index;
        info.skin=skin; info.body=body; info.hitboxset=0; info.instance=MODEL_INSTANCE_INVALID;
        return modelrender->DrawModelEx(info);
    }
};
CUtlVector<InspectionModel *> models;
int skipped=0;
struct InspectionBrush {
    model_t *model;
    Vector origin;
    QAngle angles;
    matrix3x4_t transform;
};
CUtlVector<InspectionBrush> brushes;
int pendingBrushes=0;
}

void SourceIOSShutdownEntityModels()
{
    FOR_EACH_VEC(models,i) {
        modelloader->UnreferenceModel(models[i]->model,IModelLoader::FMODELLOADER_CLIENT);
        delete models[i];
    }
    models.RemoveAll(); skipped=0;
    // Release original brush render batches while the world/inline models still
    // exist. The normal client keeps the module loaded until the next level;
    // this inspector can unload it, so it must not leave those allocations live.
    if (brushes.Count()) R_BrushBatchInit();
    FOR_EACH_VEC(brushes,i)
        modelloader->UnreferenceModel(brushes[i].model,IModelLoader::FMODELLOADER_CLIENT);
    brushes.RemoveAll(); pendingBrushes=0;
}

int SourceIOSInitializeEntityModels()
{
    SourceIOSShutdownEntityModels();
    const char *data=CM_EntityString();
    char token[1024],key[1024];
    int entityIndex=-1;
    while (data) {
        data=COM_ParseFile(data,token,sizeof(token));
        if (!data || Q_strcmp(token,"{")) break;
        char classname[128]={},name[MAX_PATH]={},animation[128]={};
        Vector origin(0,0,0); QAngle angles(0,0,0);
        int skin=0,body=0; bool disabled=false; int alpha=255,mode=0;
        while (data) {
            data=COM_ParseFile(data,key,sizeof(key));
            if (!data || !Q_strcmp(key,"}")) break;
            data=COM_ParseFile(data,token,sizeof(token));
            if (!data) break;
            if (!Q_strcmp(key,"classname")) Q_strncpy(classname,token,sizeof(classname));
            else if (!Q_strcmp(key,"model")) Q_strncpy(name,token,sizeof(name));
            else if (!Q_strcmp(key,"origin")) sscanf(token,"%f %f %f",&origin.x,&origin.y,&origin.z);
            else if (!Q_strcmp(key,"angles")) sscanf(token,"%f %f %f",&angles.x,&angles.y,&angles.z);
            else if (!Q_strcmp(key,"DefaultAnim")) Q_strncpy(animation,token,sizeof(animation));
            else if (!Q_strcmp(key,"skin")) skin=atoi(token);
            else if (!Q_strcmp(key,"SetBodyGroup")) body=atoi(token);
            else if (!Q_strcmp(key,"StartDisabled")) disabled=atoi(token)!=0;
            else if (!Q_strcmp(key,"renderamt")) alpha=atoi(token);
            else if (!Q_strcmp(key,"rendermode")) mode=atoi(token);
        }
        ++entityIndex;
        if (!Q_strcmp(classname,"func_brush") || !Q_strcmp(classname,"func_door") ||
            !Q_strcmp(classname,"func_tracktrain")) {
            // Trigger volumes and other invisible inline models are deliberately
            // excluded. Disabled brush entities stay hidden at this snapshot.
            if (disabled) continue;
            char *end=NULL;
            long inlineIndex=name[0]=='*'?strtol(name+1,&end,10):0;
            if (inlineIndex<1 || !end || *end || inlineIndex>=host_state.worldbrush->numsubmodels ||
                alpha!=255 || mode!=0) { ++pendingBrushes; continue; }
            model_t *brush=modelloader->GetModelForName(name,IModelLoader::FMODELLOADER_CLIENT);
            if (!brush || brush->type!=mod_brush ||
                (brush->flags&(MODELFLAG_TRANSLUCENT|MODELFLAG_FRAMEBUFFER_TEXTURE))) {
                if (brush) modelloader->UnreferenceModel(brush,IModelLoader::FMODELLOADER_CLIENT);
                ++pendingBrushes; continue;
            }
            // Some doors/trains are collision-only and have no drawable faces.
            if (!brush->brush.nummodelsurfaces) {
                modelloader->UnreferenceModel(brush,IModelLoader::FMODELLOADER_CLIENT); continue;
            }
            InspectionBrush entry={brush,origin,angles,{}};
            AngleMatrix(angles,origin,entry.transform);
            brushes.AddToTail(entry);
            continue;
        }
        if (Q_strcmp(classname,"prop_dynamic") && Q_strcmp(classname,"prop_physics") &&
            Q_strcmp(classname,"prop_physics_override")) continue;
        if (!name[0] || name[0]=='*' || disabled) { ++skipped; continue; }
        // Explicitly defer alpha entities and alternate render modes until they
        // can share the translucent leaf scheduling with other scene objects.
        if (alpha!=255 || mode!=0) { ++skipped; continue; }
        model_t *model=modelloader->GetModelForName(name,IModelLoader::FMODELLOADER_CLIENT);
        if (!model || model->type!=mod_studio || (model->flags&(MODELFLAG_TRANSLUCENT|MODELFLAG_STUDIOHDR_USES_FB_TEXTURE))) {
            if (model) modelloader->UnreferenceModel(model,IModelLoader::FMODELLOADER_CLIENT);
            ++skipped; continue;
        }
        InspectionModel *entry=new InspectionModel;
        entry->model=model; entry->origin=origin; entry->angles=angles;
        entry->skin=skin; entry->body=body; entry->index=entityIndex;
        Q_strncpy(entry->animation,animation,sizeof(entry->animation));
        AngleMatrix(angles,origin,entry->transform);
        models.AddToTail(entry);
    }
    return models.Count();
}

int SourceIOSDrawEntityModels(const Vector &viewOrigin,int &pending)
{
    const byte *pvs=CM_ClusterPVS(CM_LeafCluster(CM_PointLeafnum(viewOrigin)));
    const int pvsSize=CM_ClusterPVSSize();
    int drawn=0; pending=skipped;
    CMatRenderContextPtr context(materials);
    context->MatrixMode(MATERIAL_MODEL); context->PushMatrix(); context->LoadIdentity();
    FOR_EACH_VEC(models,i) {
        InspectionModel &entry=*models[i];
        Vector mins,maxs; entry.GetRenderBoundsWorldspace(mins,maxs);
        if (R_CullBox(mins,maxs) || !CM_BoxVisible(mins,maxs,pvs,pvsSize)) continue;
        drawn+=entry.DrawModel(STUDIO_RENDER)!=0;
    }
    context->MatrixMode(MATERIAL_MODEL); context->PopMatrix();
    return drawn;
}


int SourceIOSDrawBrushEntities(const Vector &viewOrigin,int &total,int &pending)
{
    total=brushes.Count(); pending=pendingBrushes;
    const byte *pvs=CM_ClusterPVS(CM_LeafCluster(CM_PointLeafnum(viewOrigin)));
    const int pvsSize=CM_ClusterPVSSize();
    int submitted=0;
    FOR_EACH_VEC(brushes,i) {
        const InspectionBrush &entry=brushes[i];
        Vector mins,maxs;
        TransformAABB(entry.transform,entry.model->mins,entry.model->maxs,mins,maxs);
        if (R_CullBox(mins,maxs) || !CM_BoxVisible(mins,maxs,pvs,pvsSize)) continue;
        // NULL is the original renderer's supported path without entity proxies.
        // This snapshot cannot update timer frames or other game-driven proxies.
        R_DrawBrushModel(NULL,entry.model,entry.origin,entry.angles,DEPTH_MODE_NORMAL,true,false);
        ++submitted;
    }
    return submitted;
}
