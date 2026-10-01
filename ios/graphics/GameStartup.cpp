#include "render_pch.h"
#include "server.h"
#include "eiface.h"
#include "cdll_int.h"
#include "client.h"
#include "cdll_engine_int.h"
#include "sys_dll.h"
#include "host.h"
#include "icvar.h"
#include "tier1/tier1.h"
#include "server_class.h"
#include "tier0/icommandline.h"

extern CGlobalVars g_ServerGlobalVariables;
extern CreateInterfaceFn g_ClientFactory;
namespace {
bool initialized=false,clientInitialized=false;
ConVar *engineCheats=NULL;
CreateInterfaceFn applicationFactory=NULL;
char lastInterface[128]={};
bool lastAvailable=false;
void *StartupFactory(const char *name,int *status) {
    void *result=applicationFactory?applicationFactory(name,status):NULL;
    Q_strncpy(lastInterface,name,sizeof(lastInterface)); lastAvailable=result!=NULL;
    Msg("iOS game startup interface %s: %s\n",name,result?"available":"missing");
    return result;
}
}
extern "C" bool SourceIOSInitializePortalServer(CreateInterfaceFn gameFactory,char *detail,size_t capacity) {
    if (initialized) { snprintf(detail,capacity,"Portal server DLLInit already complete"); return true; }
    if (!gameFactory || !g_AppSystemFactory) { snprintf(detail,capacity,"Portal server: factory unavailable"); return false; }
    applicationFactory=g_AppSystemFactory;
    lastInterface[0]=0;
    g_ServerFactory=gameFactory;
    g_iServerGameDLLVersion=INTERFACEVERSION_SERVERGAMEDLL_INT;
    serverGameDLL=static_cast<IServerGameDLL *>(gameFactory(INTERFACEVERSION_SERVERGAMEDLL,NULL));
    serverGameClients=static_cast<IServerGameClients *>(gameFactory(INTERFACEVERSION_SERVERGAMECLIENTS,NULL));
    if (!serverGameDLL || !serverGameClients) { snprintf(detail,capacity,"Portal server: game interface unavailable"); return false; }
    g_ServerGlobalVariables.maxClients=1;
    g_ServerGlobalVariables.interval_per_tick=serverGameDLL->GetTickInterval();
    engineCheats=g_pCVar->FindVar("sv_cheats");
    const char *root=getenv("SOURCE_IOS_GAME_ROOT");
    if (root) Q_snprintf(com_gamedir,sizeof(com_gamedir),"%s/portal",root);
    initialized=serverGameDLL->DLLInit(StartupFactory,StartupFactory,StartupFactory,&g_ServerGlobalVariables);
    snprintf(detail,capacity,"Portal server DLLInit: %s%s%s",initialized?"PASS":"FAIL",
        !initialized && !lastAvailable?"; unavailable interface ":"",
        !initialized && !lastAvailable?lastInterface:"");
    if (initialized) {
        ConVar *portal=g_pCVar->FindVar("sv_portal_placement_never_fail");
        int classes=0;
        for (ServerClass *type=serverGameDLL->GetAllServerClasses();type;type=type->m_pNext) ++classes;
        bool valid=portal && portal->IsFlagSet(FCVAR_GAMEDLL) && engineCheats &&
            g_pCVar->FindVar("sv_cheats")==engineCheats && classes>0;
        snprintf(detail,capacity,"Portal server DLLInit: %s; %d server classes; game cvar ownership %s",
            valid?"PASS":"FAIL",classes,valid?"PASS":"FAIL");
        Msg("%s\n",detail); return valid;
    }
    Msg("%s\n",detail);
    return false;
}
extern "C" bool SourceIOSShutdownPortalServer() {
    bool valid=true;
    if (clientInitialized && g_ClientDLL) g_ClientDLL->Shutdown();
    clientInitialized=false; g_ClientDLL=NULL; g_ClientFactory=NULL;
    if (initialized && serverGameDLL) {
        serverGameDLL->DLLShutdown();
        valid=!g_pCVar->FindVar("sv_portal_placement_never_fail") &&
            engineCheats && g_pCVar->FindVar("sv_cheats")==engineCheats;
    }
    initialized=false; serverGameDLL=NULL; serverGameClients=NULL;
    g_ServerFactory=NULL; g_iServerGameDLLVersion=0;
    applicationFactory=NULL; return valid;
}

extern "C" bool SourceIOSInitializePortalClient(CreateInterfaceFn gameFactory,char *detail,size_t capacity) {
    if (!initialized || !gameFactory) { snprintf(detail,capacity,"Portal client: server/factory unavailable"); return false; }
    applicationFactory=g_AppSystemFactory;
    lastInterface[0]=0;
    g_ClientFactory=gameFactory;
    g_ClientDLL=static_cast<IBaseClientDLL *>(gameFactory(CLIENT_DLL_INTERFACE_VERSION,NULL));
    bool started=g_ClientDLL && g_ClientDLL->Init(StartupFactory,StartupFactory,&g_ClientGlobalVariables);
    clientInitialized=started;
    // The client also checks tier-connected globals after its direct queries.
    if (!started && lastAvailable && !applicationFactory("MatSystemSurface008",NULL)) {
        Q_strncpy(lastInterface,"MatSystemSurface008",sizeof(lastInterface));
        lastAvailable=false;
    }
    if (!started) { g_ClientDLL=NULL; g_ClientFactory=NULL; }
    snprintf(detail,capacity,"Portal server DLLInit: PASS; client Init: %s%s%s",started?"PASS":"FAIL",
        !started && !lastAvailable?"; unavailable interface ":"",
        !started && !lastAvailable?lastInterface:"");
    Msg("%s\n",detail); return started;
}
