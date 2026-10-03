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
    if (passed) {
        snprintf(path,sizeof(path),"%s/libclient.dylib",directory);
        if (!modules[0]) modules[0]=Sys_LoadModule(path);
        passed=InitializePortalClient(modules[0],startupDetail,sizeof(startupDetail));
    }
    if (passed) {
        passed=StartGameSession(startupDetail,sizeof(startupDetail));
    }
    snprintf(detail,capacity,"%s",startupDetail); return passed;
}
