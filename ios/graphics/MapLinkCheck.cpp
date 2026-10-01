#include "render_pch.h"
#include "modelloader.h"
#include "render.h"

// Preserve the actual loader and renderer interface implementations.
extern "C" IModelLoader *SourceIOSMapLoaderLinkAnchor()
{
    return modelloader;
}

extern "C" IRender *SourceIOSWorldRendererLinkAnchor()
{
    return g_EngineRenderer;
}

#include "MapLoaderBootstrap.h"
#include "tier3/tier3.h"
#include "filesystem_engine.h"
#include "vphysics_interface.h"
#include "datacache/imdlcache.h"
#include "datacache/idatacache.h"
#include "zone.h"
#include "host.h"
#include "tier1/memstack.h"
#include "bspfile.h"
#include "common.h"
#include "client.h"
#include "shadowmgr.h"
#include "r_areaportal.h"
#include "filesystem/IQueuedLoader.h"
#include "cmodel_private.h"
#include "cmodel_engine.h"

extern CreateInterfaceFn g_AppSystemFactory;
extern CMemoryStack g_HunkMemoryStack;
namespace {
bool loaderStarted=false, librariesConnected=false, memoryStarted=false;
int savedMemoryBudget=0;
unsigned savedCacheLimit=0;
model_t *loadedWorld=NULL;
bool worldRendererStarted=false;
bool worldLightmapsDirty=false;
void RestoreWorldLightmaps(int) { worldLightmapsDirty=true; }
Vector cameraOrigin;
QAngle cameraAngles;
unsigned cameraCollisions=0;
}
extern "C" void SourceIOSShutdownMapLoader()
{
    if (worldRendererStarted) {
        materials->RemoveRestoreFunc(RestoreWorldLightmaps);
        R_LevelShutdown();
        g_pShadowMgr->LevelShutdown();
        worldRendererStarted=false;
        worldLightmapsDirty=false;
        g_pMaterialSystemConfig=NULL;
    }
    if (loaderStarted) {
        host_state.SetWorldModel(NULL);
        if (loadedWorld) modelloader->UnreferenceModel(loadedWorld,IModelLoader::FMODELLOADER_CLIENT);
        modelloader->Shutdown();
        if (loadedWorld) CM_FreeMap();
        loadedWorld=NULL;
        g_pQueuedLoader->InstallLoader(RESOURCEPRELOAD_MODEL,NULL);
        DisconnectMDLCacheNotify();
        loaderStarted=false;
    }
    if (memoryStarted) {
        Memory_Shutdown();
        // This bootstrap owns the arena across module load/unload cycles.
        g_HunkMemoryStack.Term();
        g_pDataCache->SetSize(savedCacheLimit);
        host_parms.memsize=savedMemoryBudget;
        memoryStarted=false;
    }
    physprop=NULL; physcollision=NULL;
    g_pFileSystem=NULL; g_AppSystemFactory=NULL;
    if (librariesConnected) {
        ConVar_Unregister();
        DisconnectTier3Libraries();
        DisconnectTier2Libraries();
        DisconnectTier1Libraries();
        librariesConnected=false;
    }
}
extern "C" bool SourceIOSInitializeMapLoader(CreateInterfaceFn factory, char *detail, size_t capacity)
{
    if (loaderStarted) return true;
    if (!factory) { snprintf(detail,capacity,"Map loader: no application factory"); return false; }
    ConnectTier1Libraries(&factory,1);
    ConnectTier2Libraries(&factory,1);
    ConnectTier3Libraries(&factory,1);
    librariesConnected=true;
    g_AppSystemFactory=factory;
    g_pFileSystem=g_pFullFileSystem;
    if (!g_pCVar || !g_pFileSystem || !g_pQueuedLoader || !g_pMDLCache ||
        !g_pDataCache || !g_pStudioRender || !g_pMaterialSystem ||
        !g_pMaterialSystemHardwareConfig ||
        !factory(VPHYSICS_COLLISION_INTERFACE_VERSION,NULL) ||
        !factory(VPHYSICS_SURFACEPROPS_INTERFACE_VERSION,NULL)) {
        snprintf(detail,capacity,"Map loader: required filesystem/cache/render/physics interface missing");
        SourceIOSShutdownMapLoader(); return false;
    }
    if (g_HunkMemoryStack.GetBase()) {
        snprintf(detail,capacity,"Map loader: world arena already owned");
        SourceIOSShutdownMapLoader(); return false;
    }
    DataCacheStatus_t cacheStatus;
    DataCacheLimits_t cacheLimits;
    g_pDataCache->GetStatus(&cacheStatus,&cacheLimits);
    savedCacheLimit=cacheLimits.nMaxBytes;
    savedMemoryBudget=host_parms.memsize;
    host_parms.memsize=256*1024*1024;
    Memory_Init();
    memoryStarted=true;
    ConVar_Register();
    ConnectMDLCacheNotify();
    modelloader->Init();
    loaderStarted=true;
    bool valid=modelloader->GetCount()==0 && physprop && physcollision && g_HunkMemoryStack.GetBase() && Hunk_Size()==0;
    snprintf(detail,capacity,"Actual model loader + world memory + physics binding + empty registry: %s",valid?"PASS":"FAIL");
    if (!valid) SourceIOSShutdownMapLoader();
    return valid;
}

extern "C" bool SourceIOSLoadWorldMap(const char *name, char *detail, size_t capacity)
{
    if (!loaderStarted || !memoryStarted || loadedWorld) {
        snprintf(detail,capacity,"World map: loader unavailable or a map is already loaded"); return false;
    }
    if (!name || Q_strncmp(name,"maps/",5) || Q_strstr(name,"..") ||
        Q_strstr(name,"\\") || Q_stricmp(V_GetFileExtension(name) ? V_GetFileExtension(name) : "","bsp")) {
        snprintf(detail,capacity,"World map: expected a relative maps/*.bsp path"); return false;
    }
    FileHandle_t file=g_pFileSystem->Open(name,"rb","GAME");
    if (!file) { snprintf(detail,capacity,"World map: %s not found in GAME",name); return false; }
    dheader_t header;
    const int size=g_pFileSystem->Size(file);
    const int read=g_pFileSystem->Read(&header,sizeof(header),file);
    g_pFileSystem->Close(file);
    if (read!=sizeof(header) || header.ident!=IDBSPHEADER ||
        header.version<MINBSPVERSION || header.version>BSPVERSION) {
        snprintf(detail,capacity,"World map: unsupported or truncated BSP header"); return false;
    }
    for (int i=0;i<HEADER_LUMPS;++i) {
        const lump_t &lump=header.lumps[i];
        if (lump.fileofs<0 || lump.filelen<0 || lump.fileofs>size || lump.filelen>size-lump.fileofs) {
            snprintf(detail,capacity,"World map: lump %d exceeds the BSP file",i); return false;
        }
    }
    const int required[]={LUMP_PLANES,LUMP_VERTEXES,LUMP_NODES,LUMP_LEAFS,
        LUMP_MODELS,LUMP_FACES,LUMP_TEXINFO,LUMP_TEXDATA,LUMP_BRUSHES,
        LUMP_BRUSHSIDES,LUMP_LEAFBRUSHES,LUMP_AREAS};
    for (int lump : required) if (!header.lumps[lump].filelen) {
        snprintf(detail,capacity,"World map: missing required geometry/collision lump %d",lump); return false;
    }
    // This is basic file preflight, not semantic BSP validation. The original
    // loader validates and consumes the data; errors still follow engine policy.
    loadedWorld=modelloader->GetModelForName(name,IModelLoader::FMODELLOADER_CLIENT);
    const worldbrushdata_t *brush=loadedWorld ? loadedWorld->brush.pShared : NULL;
    bool valid=loadedWorld && loadedWorld->type==mod_brush && brush &&
        brush->numsurfaces>0 && brush->numnodes>0 && brush->numleafs>0;
    if (valid) snprintf(detail,capacity,"%s: %d vertices, %d surfaces, %d nodes, %d leaves; BSP load PASS",
        name,brush->numvertexes,brush->numsurfaces,brush->numnodes,brush->numleafs);
    else snprintf(detail,capacity,"Actual BSP loader: %s (FAIL)",name);
    if (!valid) SourceIOSShutdownMapLoader();
    return valid;
}

extern void R_ResetLightStyles();
extern float r_blend;
static bool InitializeWorldRenderer(char *detail, size_t capacity)
{
    const char *data=CM_EntityString();
    char token[1024], key[1024], classname[128], origin[128], angles[128];
    bool found=false;
    while (data && !found) {
        data=COM_ParseFile(data,token,sizeof(token));
        if (!data || Q_strcmp(token,"{")) break;
        classname[0]=origin[0]=angles[0]=0;
        while (data) {
            data=COM_ParseFile(data,key,sizeof(key));
            if (!data || !Q_strcmp(key,"}")) break;
            data=COM_ParseFile(data,token,sizeof(token));
            if (!data) break;
            if (!Q_strcmp(key,"classname")) Q_strncpy(classname,token,sizeof(classname));
            else if (!Q_strcmp(key,"origin")) Q_strncpy(origin,token,sizeof(origin));
            else if (!Q_strcmp(key,"angles")) Q_strncpy(angles,token,sizeof(angles));
        }
        if (!Q_strcmp(classname,"info_player_start")) {
            found=sscanf(origin,"%f %f %f",&cameraOrigin.x,&cameraOrigin.y,&cameraOrigin.z)==3 &&
                sscanf(angles,"%f %f %f",&cameraAngles.x,&cameraAngles.y,&cameraAngles.z)==3;
        }
    }
    if (!found) { snprintf(detail,capacity,"World renderer: no valid info_player_start camera"); return false; }
    cameraOrigin.z+=64;
    host_state.SetWorldModel(loadedWorld);
    g_pMaterialSystemConfig=&materials->GetCurrentConfigForVideoCard();
    cameraCollisions=0;
    Ray_t stationary; trace_t placement;
    stationary.Init(cameraOrigin,cameraOrigin,Vector(-4,-4,-4),Vector(4,4,4));
    CM_BoxTrace(stationary,0,MASK_SOLID,true,placement);
    if (placement.startsolid || placement.allsolid) {
        host_state.SetWorldModel(NULL);
        g_pMaterialSystemConfig=NULL;
        snprintf(detail,capacity,"Camera hull starts inside BSP collision"); return false;
    }
    r_framecount=1;
    R_ResetLightStyles();
    r_blend=1.0f;
    R_DecalInit();
    materials->CacheUsedMaterials();
    g_pShadowMgr->LevelInit(host_state.worldbrush->numsurfaces);
    R_LoadWorldGeometry();
    BuildGammaTable(2.2f,2.2f,0.0f,OVERBRIGHT);
    R_RedownloadAllLightmaps();
    R_Surface_LevelInit();
    R_Areaportal_LevelInit();
    materials->AddRestoreFunc(RestoreWorldLightmaps);
    // No client/server simulation is running to provide area activation yet.
    memset(cl.m_chAreaBits,0xff,sizeof(cl.m_chAreaBits));
    memset(cl.m_chAreaPortalBits,0xff,sizeof(cl.m_chAreaPortalBits));
    worldRendererStarted=true;
    return true;
}
extern "C" bool SourceIOSDrawWorldMap(int width, int height, char *detail, size_t capacity)
{
    if (!loadedWorld || width<1 || height<1) {
        snprintf(detail,capacity,"World renderer: map or drawable unavailable"); return false;
    }
    if (!worldRendererStarted && !InitializeWorldRenderer(detail,capacity)) return false;
    if (worldLightmapsDirty) {
        R_RedownloadAllLightmaps();
        worldLightmapsDirty=false;
    }
    CViewSetup view;
    view.x=view.y=view.m_nUnscaledX=view.m_nUnscaledY=0;
    view.width=view.m_nUnscaledWidth=width;
    view.height=view.m_nUnscaledHeight=height;
    view.origin=cameraOrigin; view.angles=cameraAngles;
    view.fov=90; view.zNear=4; view.zFar=10000;
    view.m_bDoBloomAndToneMapping=false;
    Frustum frustum;
    ++r_framecount;
    g_EngineRenderer->SetMainView(view.origin,view.angles);
    g_EngineRenderer->Push3DView(view,0,NULL,frustum);
    g_EngineRenderer->ViewSetupVis(true,1,&view.origin);
    IWorldRenderList *list=g_EngineRenderer->CreateWorldList();
    WorldListInfo_t info={};
    g_EngineRenderer->BuildWorldLists(list,&info,-1,NULL,false,NULL);
    g_EngineRenderer->DrawWorldLists(list,DRAWWORLDLISTS_DRAW_STRICTLYABOVEWATER |
        DRAWWORLDLISTS_DRAW_STRICTLYUNDERWATER | DRAWWORLDLISTS_DRAW_INTERSECTSWATER,0);
    list->Release();
    g_EngineRenderer->PopView(frustum);
    snprintf(detail,capacity,"Actual Portal world: %d visible leaves; camera %.0f %.0f %.0f; yaw %.0f; wall hits %u",info.m_LeafCount,
        cameraOrigin.x,cameraOrigin.y,cameraOrigin.z,cameraAngles.y,cameraCollisions);
    return info.m_LeafCount>0;
}

// A flying inspection camera, not the client movement simulation. Sweep a small
// hull through original BSP collision and slide the remaining move on planes.
extern "C" void SourceIOSMoveWorldCamera(float forward, float right, float yaw, float pitch, float seconds)
{
    if (!worldRendererStarted) return;
    cameraAngles.y=AngleNormalize(cameraAngles.y+yaw);
    cameraAngles.x=clamp(cameraAngles.x+pitch,-85.0f,85.0f);
    Vector ahead,side;
    AngleVectors(cameraAngles,&ahead,&side,NULL);
    Vector remaining=ahead*forward+side*right;
    float length=remaining.Length();
    if (length>1) remaining/=length;
    remaining*=160.0f*clamp(seconds,0.0f,0.05f);
    for (int bump=0;bump<3 && remaining.LengthSqr()>0.0001f;++bump) {
        Ray_t ray; trace_t trace;
        ray.Init(cameraOrigin,cameraOrigin+remaining,Vector(-4,-4,-4),Vector(4,4,4));
        CM_BoxTrace(ray,0,MASK_SOLID,true,trace);
        if (trace.startsolid || trace.allsolid) break;
        cameraOrigin=trace.endpos;
        if (trace.fraction>=1) break;
        ++cameraCollisions;
        remaining*=1.0f-trace.fraction;
        remaining-=trace.plane.normal*DotProduct(remaining,trace.plane.normal);
    }
}
