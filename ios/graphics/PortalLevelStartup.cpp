#include "render_pch.h"
#include "eiface.h"
#include "sv_plugin.h"
#include "server.h"
#include "sys_dll.h"
#include "host.h"
#include "modelloader.h"
#include "cmodel_engine.h"
#include "cl_pluginhelpers.h"
#include "server_class.h"
#include "tier1/strtools.h"
#include "networkstringtable.h"
#include "sv_main.h"
#include "shadowmgr.h"
#include "staticpropmgr.h"

extern CGlobalVars g_ServerGlobalVariables;
extern void SV_InitSendTables(ServerClass *classes);
extern void SV_TermSendTables(ServerClass *classes);
namespace { bool serverStarted=false,gameStarted=false,levelStarted=false; int savedMark=0; }

extern "C" void SourceIOSShutdownPortalLevel() {
    if (levelStarted) {
        Msg("iOS Portal LevelShutdown: begin\n");
        g_pServerPluginHandler->LevelShutdown();
        levelStarted=false;
    }
    if (gameStarted) { serverGameDLL->GameShutdown(); gameStarted=false; }
    if (serverStarted) {
        // Match Host_ShutdownServer: release world ownership before sv.Shutdown
        // clears host_state.worldmodel, and before rewinding its hunk storage.
        g_pShadowMgr->LevelShutdown();
        StaticPropMgr()->LevelShutdown();
        Host_FreeStateAndWorld(true);
        sv.Shutdown();
        sv.Clear();
        SV_TermSendTables(serverGameDLL->GetAllServerClasses());
        CM_FreeMap();
        Hunk_FreeToLowMark(savedMark);
        serverStarted=false;
    }
}

extern "C" bool SourceIOSCheckPortalLevel(char *detail,size_t capacity) {
    const char *map=getenv("SOURCE_IOS_WORLD_MAP");
    if (!serverGameDLL || !serverGameClients || !map || Q_strncmp(map,"maps/",5) ||
        Q_strstr(map,"..") || !V_GetFileExtension(map) || Q_stricmp(V_GetFileExtension(map),"bsp")) {
        snprintf(detail,capacity,"Portal level: server/map unavailable"); return false;
    }
    serverGameEnts=static_cast<IServerGameEnts *>(g_ServerFactory(INTERFACEVERSION_SERVERGAMEENTS,NULL));
    if (!serverGameEnts) { snprintf(detail,capacity,"Portal level: ServerGameEnts001 unavailable"); return false; }
    char name[MAX_PATH]; Q_FileBase(map,name,sizeof(name));
    savedMark=Hunk_LowMark();
    Msg("iOS Portal server infrastructure: begin\n");
    sv.Init(false); serverStarted=true;
    sv.InitMaxClients();
    host_state.interval_per_tick=serverGameDLL->GetTickInterval();
    g_ServerGlobalVariables.interval_per_tick=host_state.interval_per_tick;
    SV_InitSendTables(serverGameDLL->GetAllServerClasses());
    Msg("iOS Portal GameInit: begin\n");
    gameStarted=serverGameDLL->GameInit();
    if (!gameStarted) { snprintf(detail,capacity,"Portal GameInit: FAIL"); SourceIOSShutdownPortalLevel(); return false; }
    Msg("iOS Portal SpawnServer %s: begin\n",name);
    if (!sv.SpawnServer(name,map,NULL)) {
        snprintf(detail,capacity,"Portal SpawnServer: FAIL"); SourceIOSShutdownPortalLevel(); return false;
    }
    Msg("iOS Portal LevelInit: begin\n");
    levelStarted=true;
    g_pServerPluginHandler->LevelInit(name,CM_EntityString(),NULL,NULL,false,false);
    Msg("iOS Portal ServerActivate: begin\n");
    bool passed=SV_ActivateServer();
    int entities=0;
    for (int i=0;i<sv.num_edicts;++i)
        if (!sv.edicts[i].IsFree() && sv.edicts[i].GetUnknown()) ++entities;
    passed=passed && sv.IsActive() && entities>1 && sv.edicts && !sv.edicts[0].IsFree();
    snprintf(detail,capacity,"Portal GameInit/SpawnServer/LevelInit/ServerActivate: %s; %d live edicts (player not connected)",passed?"PASS":"FAIL",entities);
    Msg("%s\n",detail);
    SourceIOSShutdownPortalLevel();
    return passed;
}
