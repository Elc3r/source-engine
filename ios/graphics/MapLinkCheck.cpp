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
#include "filesystem/IQueuedLoader.h"
#include "cmodel_private.h"

extern CreateInterfaceFn g_AppSystemFactory;
namespace { bool loaderStarted=false, librariesConnected=false; }
extern "C" void SourceIOSShutdownMapLoader()
{
    if (loaderStarted) {
        modelloader->Shutdown();
        g_pQueuedLoader->InstallLoader(RESOURCEPRELOAD_MODEL,NULL);
        DisconnectMDLCacheNotify();
        loaderStarted=false;
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
    ConVar_Register();
    ConnectMDLCacheNotify();
    modelloader->Init();
    loaderStarted=true;
    bool valid=modelloader->GetCount()==0 && physprop && physcollision;
    snprintf(detail,capacity,"Actual model loader Init + physics binding + empty registry: %s",valid?"PASS":"FAIL");
    if (!valid) SourceIOSShutdownMapLoader();
    return valid;
}
