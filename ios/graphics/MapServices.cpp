#include "MapServices.h"
#include "InputChecks.h"
#include "inputsystem/iinputsystem.h"
#include "vgui/IVGui.h"
#include "vgui/ISurface.h"
#include <dlfcn.h>
#include "SDL.h"
#include <stdlib.h>
#include "filesystem.h"
#include "tier2/tier2.h"
#include "MapLoaderBootstrap.h"
#include "appframework/IAppSystem.h"
#include "datacache/idatacache.h"
#include "datacache/imdlcache.h"
#include "istudiorender.h"
#include "vphysics_interface.h"
#include "tier1/strtools.h"

namespace {
CSysModule *modules[8]={};
CreateInterfaceFn factories[8]={};
struct Service { IAppSystem *system; bool connected, initialized; };
Service services[4]={};
CSysModule *loaderModule=NULL;
decltype(&SourceIOSShutdownMapLoader) stopLoader=NULL;
IAppSystem *gameServices[5]={};
bool gameConnected[5]={},gameInitialized[5]={};
bool worldLoaded=false;
char worldDetail[512]={};
}
void *QueryMapService(const char *name)
{
    for (CreateInterfaceFn factory : factories)
        if (factory) if (void *result=factory(name,NULL)) return result;
    // Original app-system groups also expose each loaded system's secondary
    // interfaces (notably IMatSystemSurface), rather than only DLL registries.
    for (IAppSystem *service : gameServices)
        if (service) if (void *result=service->QueryInterface(name)) return result;
    if (loaderModule) {
        CreateInterfaceFn engine=Sys_GetFactory(loaderModule);
        if (engine) if (void *result=engine(name,NULL)) return result;
    }
    return NULL;
}
void ShutdownMapServices()
{
    if (loaderModule) {
        typedef bool (*Stop)();
        Stop stop=reinterpret_cast<Stop>(GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSShutdownPortalServer"));
        if (stop) stop();
    }
    // VGUI schemes own surface textures; release them before the surface.
    const int shutdownOrder[]={3,4,2,1,0};
    for (int i : shutdownOrder) if (gameInitialized[i]) gameServices[i]->Shutdown();
    for (int i=4;i>=0;--i) if (gameConnected[i]) gameServices[i]->Disconnect();
    for (int i=7;i>=3;--i) {
        factories[i]=NULL;
        if (modules[i]) Sys_UnloadModule(modules[i]);
        modules[i]=NULL; gameServices[i-3]=NULL;
        gameConnected[i-3]=gameInitialized[i-3]=false;
    }
    if (stopLoader) stopLoader();
    worldLoaded=false;
    worldDetail[0]=0;
    stopLoader=NULL;
    if (loaderModule) Sys_UnloadModule(loaderModule);
    loaderModule=NULL;
    // Keep every interface available until all dependent systems shut down.
    for (int i=3;i>=0;--i) if (services[i].initialized) services[i].system->Shutdown();
    for (int i=3;i>=0;--i) if (services[i].connected) services[i].system->Disconnect();
    for (auto &service : services) service={};
    for (int i=2;i>=0;--i) {
        factories[i]=NULL;
        if (modules[i]) Sys_UnloadModule(modules[i]);
        modules[i]=NULL;
    }
}
bool InitializeMapServices(const char *directory, CreateInterfaceFn factory, char *detail, size_t capacity)
{
    if (services[3].initialized) return true;
    const char *names[]={"datacache","vphysics","studiorender"};
    for (int i=0;i<3;++i) {
        char path[MAX_PATH];
        Q_snprintf(path,sizeof(path),"%s/lib%s.dylib",directory,names[i]);
        modules[i]=Sys_LoadModule(path);
        factories[i]=modules[i] ? Sys_GetFactory(modules[i]) : NULL;
        if (!factories[i]) {
            snprintf(detail,capacity,"Map services: cannot load %s",names[i]);
            ShutdownMapServices(); return false;
        }
    }
    services[0].system=static_cast<IDataCache *>(QueryMapService(DATACACHE_INTERFACE_VERSION));
    services[1].system=static_cast<IPhysics *>(QueryMapService(VPHYSICS_INTERFACE_VERSION));
    services[2].system=static_cast<IMDLCache *>(QueryMapService(MDLCACHE_INTERFACE_VERSION));
    services[3].system=static_cast<IStudioRender *>(QueryMapService(STUDIO_RENDER_INTERFACE_VERSION));
    const char *labels[]={"data cache","physics","model cache","studio render"};
    for (int i=0;i<4;++i) {
        if (!services[i].system || !services[i].system->Connect(factory)) {
            // Connect may have established tier bindings before rejecting a dependency.
            if (services[i].system) services[i].system->Disconnect();
            snprintf(detail,capacity,"Map services: %s Connect failed",labels[i]);
            ShutdownMapServices(); return false;
        }
        services[i].connected=true;
    }
    for (int i=0;i<4;++i) {
        if (services[i].system->Init()!=INIT_OK) {
            services[i].system->Shutdown();
            snprintf(detail,capacity,"Map services: %s Init failed",labels[i]);
            ShutdownMapServices(); return false;
        }
        services[i].initialized=true;
    }
    char loaderPath[MAX_PATH];
    Q_snprintf(loaderPath,sizeof(loaderPath),"%s/libEngineMapLinkCheck.dylib",directory);
    loaderModule=Sys_LoadModule(loaderPath);
    auto startLoader=loaderModule ? reinterpret_cast<decltype(&SourceIOSInitializeMapLoader)>(
        GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSInitializeMapLoader")) : NULL;
    stopLoader=loaderModule ? reinterpret_cast<decltype(&SourceIOSShutdownMapLoader)>(
        GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSShutdownMapLoader")) : NULL;
    if (!startLoader || !stopLoader) {
        snprintf(detail,capacity,"Map loader module/entry points unavailable");
        ShutdownMapServices(); return false;
    }
    if (!startLoader(factory,detail,capacity)) { ShutdownMapServices(); return false; }
    return true;
}

bool MountRequestedWorldData(char *detail, size_t capacity)
{
    const char *root=getenv("SOURCE_IOS_GAME_ROOT");
    const char *map=getenv("SOURCE_IOS_WORLD_MAP");
    if (!root && !map) return true;
    if (!root || !map || !g_pFullFileSystem) {
        snprintf(detail,capacity,"World data: game root/map/filesystem missing"); return false;
    }
    // Simulator integration reads the user's existing game data. Native device
    // packaging and general gameinfo search-path handling are separate work.
    char path[MAX_PATH];
    const char *archives[]={"hl2/hl2_misc_dir.vpk","hl2/hl2_textures_dir.vpk","portal/portal_pak_dir.vpk"};
    for (const char *archive : archives) {
        Q_snprintf(path,sizeof(path),"%s/%s",root,archive);
        g_pFullFileSystem->AddSearchPath(path,"GAME",PATH_ADD_TO_HEAD);
    }
    Q_snprintf(path,sizeof(path),"%s/hl2",root);
    g_pFullFileSystem->AddSearchPath(path,"GAME",PATH_ADD_TO_HEAD);
    Q_snprintf(path,sizeof(path),"%s/portal",root);
    g_pFullFileSystem->AddSearchPath(path,"GAME",PATH_ADD_TO_HEAD);
    Q_snprintf(path,sizeof(path),"%s/platform/platform_misc_dir.vpk",root);
    g_pFullFileSystem->AddSearchPath(path,"PLATFORM",PATH_ADD_TO_TAIL);
    g_pFullFileSystem->AddSearchPath(path,"GAME",PATH_ADD_TO_TAIL);
    Q_snprintf(path,sizeof(path),"%s/platform",root);
    g_pFullFileSystem->AddSearchPath(path,"PLATFORM",PATH_ADD_TO_HEAD);
    g_pFullFileSystem->AddSearchPath(path,"GAME",PATH_ADD_TO_TAIL);
    Q_snprintf(path,sizeof(path),"%s/portal",root);
    g_pFullFileSystem->AddSearchPath(path,"MOD",PATH_ADD_TO_TAIL);
    return true;
}

bool HasLoadedWorldMap() { return worldLoaded; }
bool LoadRequestedWorldMap(char *detail, size_t capacity)
{
    const char *root=getenv("SOURCE_IOS_GAME_ROOT");
    const char *map=getenv("SOURCE_IOS_WORLD_MAP");
    if (!root && !map) {
        // Synthetic world probes own the same hunk arena. The two loader
        // lifecycle checks have finished; retain render/cache services only.
        if (stopLoader) stopLoader();
        stopLoader=NULL;
        if (loaderModule) Sys_UnloadModule(loaderModule);
        loaderModule=NULL;
        return true;
    }
    if (!root || !map || !loaderModule || !g_pFullFileSystem) {
        snprintf(detail,capacity,"World map: game root/map/services missing"); return false;
    }
    auto load=reinterpret_cast<decltype(&SourceIOSLoadWorldMap)>(
        GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSLoadWorldMap"));
    if (!load) { snprintf(detail,capacity,"World map: loader entry point missing"); return false; }
    worldLoaded=load(map,detail,capacity);
    Q_strncpy(worldDetail,detail,sizeof(worldDetail));
    return worldLoaded;
}

extern "C" int IsSourceWorldMapLoaded() { return worldLoaded ? 1 : 0; }

extern "C" const char *SourceWorldMapDetail() { return worldDetail; }

bool DrawLoadedWorldMap(IMaterialSystem *system, int width, int height, char *detail, size_t capacity)
{
    auto draw=loaderModule ? reinterpret_cast<decltype(&SourceIOSDrawWorldMap)>(
        GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSDrawWorldMap")) : NULL;
    if (!worldLoaded || !draw) { snprintf(detail,capacity,"World renderer entry point unavailable"); return false; }
    IMaterialSystem *previous=materials;
    materials=system;
    bool valid=draw(width,height,detail,capacity);
    materials=previous;
    return valid;
}

extern "C" void MoveSourceWorldCamera(float forward, float right, float yaw, float pitch, float seconds)
{
    auto move=loaderModule ? reinterpret_cast<decltype(&SourceIOSMoveWorldCamera)>(
        GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSMoveWorldCamera")) : NULL;
    if (worldLoaded && move) move(forward,right,yaw,pitch,seconds);
}


bool InitializeGameServices(const char *directory,CreateInterfaceFn factory,char *detail,size_t capacity)
{
    g_pFullFileSystem->AddSearchPath(directory,"EXECUTABLE_PATH",PATH_ADD_TO_HEAD);
    char *writePath=SDL_GetPrefPath("SourceEngine","Portal");
    if (!writePath) { snprintf(detail,capacity,"Portal writable sandbox path unavailable"); return false; }
    g_pFullFileSystem->AddSearchPath(writePath,"DEFAULT_WRITE_PATH",PATH_ADD_TO_HEAD);
    g_pFullFileSystem->AddSearchPath(writePath,"MOD",PATH_ADD_TO_HEAD);
    char configPath[MAX_PATH]; Q_snprintf(configPath,sizeof(configPath),"%sconfig",writePath);
    g_pFullFileSystem->AddSearchPath(configPath,"CONFIG",PATH_ADD_TO_HEAD);
    SDL_free(writePath);
    const char *names[]={"soundemittersystem","scenefilecache","inputsystem","vgui2","vguimatsurface"};
    const char *interfaces[]={"VSoundEmitter002","SceneFileCache002","InputSystemVersion001","VGUI_ivgui008","VGUI_Surface030"};
    // VGUI and its surface query each other during Connect. Publish every
    // factory first, then connect all systems before initializing any of them.
    for (int i=3;i<8;++i) {
        char path[MAX_PATH]; Q_snprintf(path,sizeof(path),"%s/lib%s.dylib",directory,names[i-3]);
        modules[i]=Sys_LoadModule(path); factories[i]=modules[i]?Sys_GetFactory(modules[i]):NULL;
        gameServices[i-3]=factories[i]?static_cast<IAppSystem *>(factories[i](interfaces[i-3],NULL)):NULL;
        if (!gameServices[i-3]) {
            snprintf(detail,capacity,"Portal game service %s load failed",names[i-3]); return false;
        }
    }
    for (int i=0;i<5;++i) {
        if (!gameServices[i]->Connect(factory)) {
            gameServices[i]->Disconnect();
            snprintf(detail,capacity,"Portal game service %s Connect failed",names[i]); return false;
        }
        gameConnected[i]=true;
    }
    for (int i=0;i<5;++i) {
        if (gameServices[i]->Init()!=INIT_OK) {
            snprintf(detail,capacity,"Portal game service %s Init failed",names[i]); return false;
        }
        gameInitialized[i]=true;
    }
    if (getenv("SOURCE_IOS_VGUI_CHECK")) {
        typedef bool (*Check)(char *,size_t);
        Check check=reinterpret_cast<Check>(GetProcAddress(reinterpret_cast<void *>(modules[7]),"SourceIOSCheckVGUI"));
        if (!check || !check(detail,capacity)) return false;
    }
    if (getenv("SOURCE_IOS_INPUT_CHECK") && !CheckIOSInput(static_cast<IInputSystem *>(QueryMapService(INPUTSYSTEM_INTERFACE_VERSION)))) {
        snprintf(detail,capacity,"iOS input integration: FAIL"); return false;
    }
    return true;
}
bool InitializePortalServer(CreateInterfaceFn gameFactory,char *detail,size_t capacity)
{
    typedef bool (*Start)(CreateInterfaceFn,char *,size_t);
    Start start=loaderModule?reinterpret_cast<Start>(GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSInitializePortalServer")):NULL;
    if (!start) { snprintf(detail,capacity,"Portal server bootstrap entry point unavailable"); return false; }
    return start(gameFactory,detail,capacity);
}

bool InitializePortalClient(CreateInterfaceFn gameFactory,char *detail,size_t capacity)
{
    typedef bool (*Start)(CreateInterfaceFn,char *,size_t);
    Start start=loaderModule?reinterpret_cast<Start>(GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSInitializePortalClient")):NULL;
    if (!start) { snprintf(detail,capacity,"Portal client bootstrap entry point unavailable"); return false; }
    return start(gameFactory,detail,capacity);
}
bool ShutdownPortalServer()
{
    typedef bool (*Stop)();
    Stop stop=loaderModule?reinterpret_cast<Stop>(GetProcAddress(reinterpret_cast<void *>(loaderModule),"SourceIOSShutdownPortalServer")):NULL;
    return stop && stop();
}

void PollInspectionInput()
{
    if (!gameInitialized[2]) return;
    IInputSystem *input=static_cast<IInputSystem *>(gameServices[2]);
    input->PollInputState();
    typedef bool (*Dispatch)();
    Dispatch dispatch=loaderModule?reinterpret_cast<Dispatch>(GetProcAddress(
        reinterpret_cast<void *>(loaderModule),"SourceIOSDispatchPortalInput")):NULL;
    if (dispatch && dispatch()) return;
    // Without an initialized client, discard inspector-only deltas.
    // Polling prevents the watched event queue growing during long sessions.
    for (int slot=0;slot<10;++slot) { float dx,dy; input->GetTouchAccumulators(slot,dx,dy); }
}

bool CheckPortalClientInput(IOSReadPortalCommand readCommand,IOSFindPortalControl findControl,char *detail,size_t capacity) {
    typedef bool (*Check)(IOSReadPortalCommand,IOSFindPortalControl,char *,size_t);
    Check check=loaderModule?reinterpret_cast<Check>(GetProcAddress(
        reinterpret_cast<void *>(loaderModule),"SourceIOSCheckPortalClientInput")):NULL;
    if (!check) { snprintf(detail,capacity,"Portal client input check entry point unavailable"); return false; }
    return check(readCommand,findControl,detail,capacity);
}
