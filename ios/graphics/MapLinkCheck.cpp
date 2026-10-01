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
#include "filesystem/IQueuedLoader.h"
#include "cmodel_private.h"

extern CreateInterfaceFn g_AppSystemFactory;
extern CMemoryStack g_HunkMemoryStack;
namespace {
bool loaderStarted=false, librariesConnected=false, memoryStarted=false;
int savedMemoryBudget=0;
unsigned savedCacheLimit=0;
model_t *loadedWorld=NULL;
}
extern "C" void SourceIOSShutdownMapLoader()
{
    if (loaderStarted) {
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
