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
#include "Overlay.h"
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
#include "staticpropmgr.h"
#include "ispatialpartitioninternal.h"
#include "l_studio.h"
#include "engine/ivmodelrender.h"
#include "cdll_engine_int.h"
#include "filesystem/IQueuedLoader.h"
#include "cmodel_private.h"
#include "cmodel_engine.h"

extern CreateInterfaceFn g_AppSystemFactory;
extern CMemoryStack g_HunkMemoryStack;
namespace {
bool loaderStarted=false, librariesConnected=false, memoryStarted=false;
int savedMemoryBudget=0;
unsigned savedCacheLimit=0;

}
extern "C" void SourceIOSShutdownMapLoader()
{
    if (loaderStarted) {
        host_state.SetWorldModel(NULL);
        modelloader->Shutdown();
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
    // Host_Init normally initializes the shared token parser before sound
    // scripts/DSP presets or game modules parse engine-format files.
    COM_Init();
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
