#include "MapServices.h"
#include "appframework/IAppSystem.h"
#include "datacache/idatacache.h"
#include "datacache/imdlcache.h"
#include "istudiorender.h"
#include "vphysics_interface.h"
#include "tier1/strtools.h"

namespace {
CSysModule *modules[3]={};
CreateInterfaceFn factories[3]={};
struct Service { IAppSystem *system; bool connected, initialized; };
Service services[4]={};
}
void *QueryMapService(const char *name)
{
    for (CreateInterfaceFn factory : factories)
        if (factory) if (void *result=factory(name,NULL)) return result;
    return NULL;
}
void ShutdownMapServices()
{
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
    snprintf(detail,capacity,"Map services: data/model cache + physics + studio render initialized: PASS");
    return true;
}
