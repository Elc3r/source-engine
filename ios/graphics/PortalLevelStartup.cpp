#include "render_pch.h"
#include "PortalPlayerProbe.h"
#include "cl_pred.h"
#include "vgui_baseui_interface.h"
#include "render.h"
#include "cdll_int.h"
#include "cdll_engine_int.h"
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
#include "client.h"
#include "cl_main.h"
#include "net.h"
#include "sys.h"
#include "iclient.h"
#include "tier0/platform.h"
#include "screen.h"
#include "l_studio.h"
#include "r_local.h"
#include "materialsystem/materialsystem_config.h"

extern CGlobalVars g_ServerGlobalVariables;
extern void SV_InitSendTables(ServerClass *classes);
extern void SV_TermSendTables(ServerClass *classes);
namespace { bool serverStarted=false,gameStarted=false,levelStarted=false,networkStarted=false,renderStarted=false; int savedMark=0; bool playing=false; IOSReadPortalPlayer playerReader=NULL; double lastFrame=0,tickRemainder=0;
}

extern "C" void SourceIOSShutdownPortalLevel() {
    if (playing) host_initialized=false;
    playing=false; playerReader=NULL; tickRemainder=0;
    if (serverStarted) Host_AllowQueuedMaterialSystem(false);
    if (networkStarted) cl.Disconnect("iOS player cycle complete",false);
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
    if (networkStarted) { NET_Shutdown(); networkStarted=false; }
    if (renderStarted) { SCR_EndLoadingPlaque(); SCR_Shutdown(); ShutdownStudioRender(); g_pMaterialSystemConfig=NULL; renderStarted=false; }
}

extern "C" bool SourceIOSCheckPortalLevel(char *detail,size_t capacity,IOSReadPortalPlayer readPlayer) {
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
    const char *mode=getenv("SOURCE_IOS_GAME_STARTUP");
    if (passed && mode && (!strcmp(mode,"player-cycle") || !strcmp(mode,"play"))) {
        Msg("iOS Portal localhost connection: begin\n");
        g_pMaterialSystemConfig=&materials->GetCurrentConfigForVideoCard();
        InitStudioRender(); renderStarted=true;
        R_InitStudio();
        SCR_Init();
        NET_Init(false); networkStarted=true;
        CL_Init();
        cl.Connect("localhost","ios-probe");
        double start=Plat_FloatTime();
        int state=-1;
        while (!cl.IsActive() && Plat_FloatTime()-start<12.0) {
            double now=Plat_FloatTime();
            NET_RunFrame(now);
            host_frametime=host_state.interval_per_tick;
            g_ServerGlobalVariables.realtime=now;
            cl.RunFrame();
            cl.CheckUpdatingSteamResources();
            CL_Move(0,true);
            SV_Frame(true);
            CL_ReadPackets(true);
            if (state!=cl.m_nSignonState) {
                state=cl.m_nSignonState;
                Msg("iOS Portal localhost signon: %d; server clients %d\n",state,sv.GetClientCount());
            }
            Sys_Sleep(1);
        }
        passed=cl.IsActive() && sv.GetClientCount()==1 && sv.GetClient(0)->IsActive() &&
            !sv.GetClient(0)->IsFakeClient() && !sv.edicts[1].IsFree() && sv.edicts[1].GetUnknown();
        IOSPortalPlayer before={},after={};
        int ticks=0;
        if (passed) {
            // UIKit owns the GLES context; keep this bounded probe on its thread.
            Host_AllowQueuedMaterialSystem(false);
            SCR_EndLoadingPlaque();
            passed=readPlayer && readPlayer(1,&before);
            int firstTick=sv.m_nTickCount;
            for (int i=0;passed && i<8;++i) {
                NET_RunFrame(Plat_FloatTime());
                host_frametime=host_state.interval_per_tick;
                g_ServerGlobalVariables.realtime=Plat_FloatTime();
                ClientDLL_FrameStageNotify(FRAME_START);
                g_ClientDLL->IN_SetSampleTime(host_state.interval_per_tick);
                CL_Move(0,true);
                SV_Frame(true);
                CL_ReadPackets(true);
                cl.RunFrame();
                passed=cl.IsActive() && readPlayer(1,&after);
            }
            ticks=sv.m_nTickCount-firstTick;
            passed=passed && ticks==8 && after.tickBase>before.tickBase &&
                after.command>before.command && after.command==cl.lastoutgoingcommand;
        }
        snprintf(detail,capacity,"Portal localhost player + usercmd simulation: %s; signon %d; %d ticks; server command %d/%d; tickbase %d -> %d",
            passed?"PASS":"FAIL",cl.m_nSignonState,ticks,after.command,cl.lastoutgoingcommand,before.tickBase,after.tickBase);
        Msg("%s\n",detail);
    }
    if (passed && mode && !strcmp(mode,"play")) {
        // The UIKit bootstrap bypasses the desktop loading-screen transition.
        // Enter gameplay through the original UI lifecycle so touch input is
        // no longer gated by the menu panel.
        EngineVGui()->HideGameUI();
        // IsPaused gates native CreateMove on completion of host startup.
        // The UIKit host has now initialized its services and connected player.
        host_initialized=true;
        playing=true; playerReader=readPlayer; lastFrame=Plat_FloatTime(); tickRemainder=0;
        snprintf(detail,capacity,"Portal live server/client: PASS; player connected; original renderer");
    } else SourceIOSShutdownPortalLevel();
    return passed;
}


extern "C" bool SourceIOSIsPortalGameLive() { return playing; }
extern "C" bool SourceIOSAdvancePortalGame(char *detail,size_t capacity) {
    if (!playing || !cl.IsActive() || !sv.IsActive()) { snprintf(detail,capacity,"Portal live connection lost"); return false; }
    double now=Plat_FloatTime(),elapsed=now-lastFrame;
    lastFrame=now;
    // Loading/background stalls must not generate an unbounded catch-up burst.
    tickRemainder+=elapsed>=0 && elapsed<.25?elapsed:0;
    ++host_framecount; g_ClientGlobalVariables.framecount=host_framecount;
    ClientDLL_FrameStageNotify(FRAME_START);
    while (tickRemainder>=host_state.interval_per_tick) {
        host_frametime=host_state.interval_per_tick;
        g_ServerGlobalVariables.realtime=now;
        NET_RunFrame(now);
        cl.SetFrameTime(host_state.interval_per_tick);
        g_ClientDLL->IN_SetSampleTime(host_state.interval_per_tick);
        ClientDLL_ProcessInput();
        CL_Move(0,true);
        SV_Frame(true);
        CL_ReadPackets(true);
        cl.RunFrame();
        tickRemainder-=host_state.interval_per_tick;
    }
    g_ClientGlobalVariables.interpolation_amount=tickRemainder/host_state.interval_per_tick;
    CL_RunPrediction(PREDICTION_NORMAL);
    ClientDLL_Update();
    IOSPortalPlayer player={};
    if (!playerReader || !playerReader(1,&player)) { snprintf(detail,capacity,"Portal live player unavailable"); return false; }
    snprintf(detail,capacity,"Portal LIVE: server tick %d; command %d/%d; player %.1f %.1f %.1f; yaw %.1f; move %.0f/%.0f; flags %x",
        sv.m_nTickCount,player.command,cl.lastoutgoingcommand,player.x,player.y,player.z,cl.viewangles.y,player.forward,player.side,player.flags);
    return true;
}
extern "C" bool SourceIOSDrawPortalGame(int width,int height,char *detail,size_t capacity) {
    if (!playing || !g_ClientDLL || width<1 || height<1) return false;
    EngineVGui()->Simulate();
    ClientDLL_FrameStageNotify(FRAME_RENDER_START);
    g_EngineRenderer->FrameBegin();
    cl.UpdateAreaBits_BackwardsCompatible();
    vrect_t rect={}; rect.width=width; rect.height=height;
    g_ClientDLL->View_Render(&rect);
    ClientDLL_FrameStageNotify(FRAME_RENDER_END);
    g_EngineRenderer->FrameEnd();
    return true;
}
