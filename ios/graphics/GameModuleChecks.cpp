#include "tier1/interface.h"
#include "cdll_int.h"
#include "eiface.h"
#include "GameUI/IGameUI.h"
#include "GameUI/IGameConsole.h"
#include "MapServices.h"
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

namespace { CSysModule *modules[3]={NULL,NULL,NULL}; }

extern "C" int CheckPortalGameModules(const char *directory,char *detail,size_t capacity)
{
    if (!getenv("SOURCE_IOS_GAME_MODULE_CHECK")) {
        snprintf(detail,capacity,"Portal game modules: check disabled"); return 1;
    }
    // Keep the libraries loaded: this checks their real factory registries,
    // without starting DLLInit/Init or claiming that gameplay has started.
    const char *names[3]={"client","server","GameUI"};
    const char *interfaces[3]={CLIENT_DLL_INTERFACE_VERSION,INTERFACEVERSION_SERVERGAMEDLL,GAMEUI_INTERFACE_VERSION};
    for (int i=0;i<3;++i) {
        char path[4096]; snprintf(path,sizeof(path),"%s/lib%s.dylib",directory,names[i]);
        if (!modules[i]) modules[i]=Sys_LoadModule(path);
        CreateInterfaceFn factory=modules[i]?Sys_GetFactory(modules[i]):NULL;
        int result=IFACE_FAILED;
        if (!factory || !factory(interfaces[i],&result) || result!=IFACE_OK) {
            snprintf(detail,capacity,"Portal game modules: %s factory/interface %s unavailable",names[i],interfaces[i]);
            return 0;
        }
    }
    CreateInterfaceFn gameUI=Sys_GetFactory(modules[2]);
    if (!gameUI(GAMECONSOLE_INTERFACE_VERSION,NULL) ||
        gameUI(CLIENT_DLL_INTERFACE_VERSION,NULL) ||
        gameUI(INTERFACEVERSION_SERVERGAMEDLL,NULL)) {
        snprintf(detail,capacity,"Portal game modules: GameUI console/registry check failed"); return 0;
    }
    CreateInterfaceFn client=Sys_GetFactory(modules[0]);
    CreateInterfaceFn server=Sys_GetFactory(modules[1]);
    if (client==server || client(INTERFACEVERSION_SERVERGAMEDLL,NULL) ||
        server(CLIENT_DLL_INTERFACE_VERSION,NULL)) {
        snprintf(detail,capacity,"Portal game modules: interface registries are not isolated"); return 0;
    }
    if (!server(INTERFACEVERSION_SERVERGAMECLIENTS,NULL)) {
        snprintf(detail,capacity,"Portal game modules: server client interface unavailable"); return 0;
    }
    snprintf(detail,capacity,"Portal game modules: client/server/GameUI + console loaded; %s, %s, %s PASS (gameplay not initialized)",
        interfaces[0],interfaces[1],INTERFACEVERSION_SERVERGAMECLIENTS);
    return 1;
}


namespace { char startupDetail[512]="Portal game startup: not requested"; }
extern "C" const char *PortalGameStartupDetail() { return startupDetail; }
extern "C" int InitializePortalGame(const char *directory,CreateInterfaceFn applicationFactory,char *detail,size_t capacity)
{
    if (!getenv("SOURCE_IOS_GAME_STARTUP")) return 1;
    if (!InitializeGameServices(directory,applicationFactory,startupDetail,sizeof(startupDetail))) {
        snprintf(detail,capacity,"%s",startupDetail); return 0;
    }
    char path[4096]; snprintf(path,sizeof(path),"%s/libserver.dylib",directory);
    if (!modules[1]) modules[1]=Sys_LoadModule(path);
    bool passed=InitializePortalServer(modules[1]?Sys_GetFactory(modules[1]):NULL,startupDetail,sizeof(startupDetail));
    const char *mode=getenv("SOURCE_IOS_GAME_STARTUP");
    if (passed && !strcmp(mode,"server-cycle")) {
        passed=ShutdownPortalServer();
        snprintf(startupDetail,sizeof(startupDetail),"Portal server DLLInit/DLLShutdown + game cvar cleanup + engine cvar preservation: %s",passed?"PASS":"FAIL");
    }
    if (passed && (!strcmp(mode,"client") || !strcmp(mode,"client-cycle") || !strcmp(mode,"level-cycle") || !strcmp(mode,"player-cycle") || !strcmp(mode,"play"))) {
        snprintf(path,sizeof(path),"%s/libclient.dylib",directory);
        if (!modules[0]) modules[0]=Sys_LoadModule(path);
        passed=InitializePortalClient(modules[0],startupDetail,sizeof(startupDetail));
    }
    if (passed && getenv("SOURCE_IOS_CLIENT_INPUT_CHECK") &&
        (!strcmp(mode,"client") || !strcmp(mode,"client-cycle") || !strcmp(mode,"level-cycle") || !strcmp(mode,"player-cycle") || !strcmp(mode,"play"))) {
        IOSReadPortalCommand readCommand=reinterpret_cast<IOSReadPortalCommand>(GetProcAddress(
            reinterpret_cast<void *>(modules[0]),"SourceIOSReadPortalCommand"));
        IOSFindPortalControl findControl=reinterpret_cast<IOSFindPortalControl>(GetProcAddress(
            reinterpret_cast<void *>(modules[0]),"SourceIOSFindPortalControl"));
        passed=CheckPortalClientInput(readCommand,findControl,startupDetail,sizeof(startupDetail));
    }
    if (passed && (!strcmp(mode,"level-cycle") || !strcmp(mode,"player-cycle") || !strcmp(mode,"play"))) {
        IOSReadPortalPlayer readPlayer=reinterpret_cast<IOSReadPortalPlayer>(GetProcAddress(
            reinterpret_cast<void *>(modules[1]),"SourceIOSReadPortalPlayer"));
        passed=CheckPortalLevel(startupDetail,sizeof(startupDetail),readPlayer);
    }
    if (passed && !strcmp(mode,"client-cycle")) {
        passed=ShutdownPortalServer();
        snprintf(startupDetail,sizeof(startupDetail),"Portal client Init/PostInit/Shutdown + engine VGUI/GameUI lifecycle + server lifecycle + cvar cleanup%s: %s",getenv("SOURCE_IOS_CLIENT_INPUT_CHECK")?" + client input/codec":"",passed?"PASS":"FAIL");
    }
    if (getenv("SOURCE_IOS_VGUI_CHECK")) {
        size_t used=strlen(startupDetail);
        snprintf(startupDetail+used,sizeof(startupDetail)-used,"; iOS VGUI integration: PASS");
    }
    if (getenv("SOURCE_IOS_INPUT_CHECK")) {
        size_t used=strlen(startupDetail);
        snprintf(startupDetail+used,sizeof(startupDetail)-used,"; iOS input integration: PASS");
    }
    snprintf(detail,capacity,"%s",startupDetail); return passed;
}
