#include "render_pch.h"
#include "cl_pred.h"
#include "vgui_baseui_interface.h"
#include "render.h"
#include "ivideomode.h"
#include "cdll_int.h"
#include "cdll_engine_int.h"
#include "eiface.h"
#include "sv_plugin.h"
#include "server.h"
#include "sys_dll.h"
#include "host.h"
#include "host_state.h"
#include "host_saverestore.h"
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
#include "icliententitylist.h"
#include "engine/audio/sound.h"
#include "engine/audio/snd_device.h"
#include "engine/audio/soundservice.h"

extern IClientEntityList *entitylist;
extern IAudioDevice *g_AudioDevice;

extern CGlobalVars g_ServerGlobalVariables;
extern void _Host_SetGlobalTime();
extern void SV_InitSendTables(ServerClass *classes);
extern void SV_TermSendTables(ServerClass *classes);
extern "C" void SourceIOSUpdateVideoMode();
extern void ReleaseMaterialSystemObjects();
extern void RestoreMaterialSystemObjects(int changeFlags);
extern void Host_UpdateSounds();
extern void InitMaterialSystemConfig(bool);
extern void UpdateMaterialSystemConfig();
extern "C" void SourceIOSSetSoundFocus(bool active);
extern void S_BlockSound();
extern void S_UnblockSound();
namespace { bool serverStarted=false,gameStarted=false,levelStarted=false,networkStarted=false,renderStarted=false,saveStarted=false; int savedMark=0; bool playing=false,clientFrameReady=false; double lastFrame=0,tickRemainder=0,reconnectStarted=0;
bool audioStarted=false,audioPaused=false,quitRequested=false;
}

extern "C" void SourceIOSSetPortalAudioActive(bool active) {
    SourceIOSSetSoundFocus(active);
    if (!audioStarted || audioPaused==!active) return;
    if (active) S_UnblockSound(); else S_BlockSound();
    audioPaused=!active;
}

extern "C" void SourceIOSShutdownPortalLevel() {
    if (renderStarted) {
        materials->RemoveReleaseFunc(ReleaseMaterialSystemObjects);
        materials->RemoveRestoreFunc(RestoreMaterialSystemObjects);
    }
    if (saveStarted) { saverestore->Shutdown(); saveStarted=false; }
    if (playing) host_initialized=false;
    playing=false; clientFrameReady=false; tickRemainder=0; reconnectStarted=0;
    if (serverStarted) Host_AllowQueuedMaterialSystem(false);
    if (networkStarted) cl.Disconnect("iOS shutdown",false);
    if (levelStarted) {
        Msg("iOS Portal LevelShutdown: begin\n");
        g_pServerPluginHandler->LevelShutdown();
        levelStarted=false;
    }
    if (gameStarted) { serverGameDLL->GameShutdown(); gameStarted=false; }
    HostState_Init();
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
    if (audioStarted) { S_Shutdown(); audioStarted=false; audioPaused=false; }
    if (renderStarted) { SCR_EndLoadingPlaque(); SCR_Shutdown(); ShutdownStudioRender(); g_pMaterialSystemConfig=NULL; renderStarted=false; }
}

extern "C" bool SourceIOSStartGameSession(char *detail,size_t capacity) {
    const char *map=getenv("SOURCE_IOS_WORLD_MAP");
    if (!serverGameDLL || !serverGameClients || !map || Q_strncmp(map,"maps/",5) ||
        Q_strstr(map,"..") || !V_GetFileExtension(map) || Q_stricmp(V_GetFileExtension(map),"bsp")) {
        snprintf(detail,capacity,"Portal level: server/map unavailable"); return false;
    }
    serverGameEnts=static_cast<IServerGameEnts *>(g_ServerFactory(INTERFACEVERSION_SERVERGAMEENTS,NULL));
    if (!serverGameEnts) { snprintf(detail,capacity,"Portal level: ServerGameEnts001 unavailable"); return false; }
    char name[MAX_PATH]; Q_FileBase(map,name,sizeof(name));
    savedMark=Hunk_LowMark();
    Host_SetHunkLevel(savedMark);
    Msg("iOS Portal server infrastructure: begin\n");
    saverestore->Init(); saveStarted=true;
    sv.Init(false); serverStarted=true;
    sv.InitMaxClients();
    host_state.interval_per_tick=serverGameDLL->GetTickInterval();
    g_ServerGlobalVariables.interval_per_tick=host_state.interval_per_tick;
    SV_InitSendTables(serverGameDLL->GetAllServerClasses());
    SourceIOSSetSoundFocus(true);
    S_Init(); audioStarted=true;
    if (!g_AudioDevice || !g_AudioDevice->IsActive()) {
        snprintf(detail,capacity,"Portal audio device unavailable");
        SourceIOSShutdownPortalLevel(); return false;
    }
    Msg("iOS Portal audio: %s; %d Hz; %d channels; %d bits\n",
        g_AudioDevice->DeviceName(),g_AudioDevice->DeviceDmaSpeed(),
        g_AudioDevice->DeviceChannels(),g_AudioDevice->DeviceSampleBits());
    const char *mode=getenv("SOURCE_IOS_GAME_STARTUP");
    if (mode && !strcmp(mode,"menu")) {
        InitMaterialSystemConfig(false);
        InitStudioRender(); renderStarted=true;
        materials->AddReleaseFunc(ReleaseMaterialSystemObjects);
        materials->AddRestoreFunc(RestoreMaterialSystemObjects);
        R_InitStudio();
        SCR_Init();
        NET_Init(false); networkStarted=true;
        CL_Init();
        HostState_Init();
        Host_AllowQueuedMaterialSystem(false);
        host_initialized=true;
        playing=true; lastFrame=Plat_FloatTime(); tickRemainder=0;
        EngineVGui()->ActivateGameUI();
        // Use the original chapter selection and map_background startup path.
        Cbuf_AddText("startupmenu\n");
        snprintf(detail,capacity,"Portal main menu: PASS; loading native background map");
        return true;
    }
    return false;
}

extern "C" bool SourceIOSIsPortalGameLive() { return playing; }
extern "C" bool SourceIOSIsGameLoading() { return playing && scr_disabled_for_loading; }
extern "C" bool SourceIOSAdvancePortalGame(char *detail,size_t capacity) {
    clientFrameReady=false;
    if (!playing) { snprintf(detail,capacity,"Portal game not running"); return false; }
    double now=Plat_FloatTime(),elapsed=now-lastFrame;
    lastFrame=now;
    // Loading/background stalls must not generate an unbounded catch-up burst.
    tickRemainder+=elapsed>=0 && elapsed<.25?elapsed:0;
    // Portal opening/static fades use absolute frame time, not simulation
    // ticks. Publish the same globals as the original desktop host; leaving
    // absoluteframetime at zero keeps linked portals permanently opaque.
    realtime=now;
    host_frametime=elapsed>=0 && elapsed<.25 ? float(elapsed) : 0.0f;
    // Sound envelopes, channel crossfades and ducking use the sound-service
    // frame delta, which the desktop host normally publishes in Host_FilterTime.
    if (g_pSoundServices) g_pSoundServices->SetSoundFrametime(host_frametime,host_frametime);
    host_time+=host_frametime;
    ++host_framecount;
    _Host_SetGlobalTime();
    // load/changelevel first disconnect the client; process the original host
    // state machine before treating that temporary disconnect as a failure.
    HostState_FrameTransitions(host_frametime);
    Cbuf_Execute();
    UpdateMaterialSystemConfig();
    if (!sv.IsActive() && cl.m_nSignonState==SIGNONSTATE_NONE) {
        reconnectStarted=0; tickRemainder=0;
        NET_RunFrame(now);
        cl.RunFrame();
        if (!EngineVGui()->IsGameUIVisible()) EngineVGui()->ActivateGameUI();
        snprintf(detail,capacity,"Portal main menu; no active game");
        return true;
    }
    if (!cl.IsActive() || !entitylist || !entitylist->GetClientEntity(cl.m_nPlayerSlot+1)) {
        if (!reconnectStarted) reconnectStarted=now;
        if (now-reconnectStarted>30.0) {
            snprintf(detail,capacity,"Portal reconnect timed out; signon %d",cl.m_nSignonState); return false;
        }
        NET_RunFrame(now);
        host_frametime=host_state.interval_per_tick;
        ++host_tickcount;
        cl.RunFrame();
        cl.CheckUpdatingSteamResources();
        CL_Move(0,true);
        SV_Frame(true);
        CL_ReadPackets(true);
        Host_AllowQueuedMaterialSystem(false);
        tickRemainder=0; lastFrame=Plat_FloatTime();
        snprintf(detail,capacity,"Portal loading; signon %d",cl.m_nSignonState);
        return true;
    }
    if (reconnectStarted) {
        reconnectStarted=0; tickRemainder=0; lastFrame=Plat_FloatTime();
        Host_AllowQueuedMaterialSystem(false);
        SCR_EndLoadingPlaque();
        // Background maps keep GameUI visible; this native call also unpauses
        // the scene after signon activates the single-player menu.
        EngineVGui()->HideGameUI();
    }
    if (!sv.IsActive()) { snprintf(detail,capacity,"Portal server unavailable"); return false; }
    ClientDLL_FrameStageNotify(FRAME_START);
    while (tickRemainder>=host_state.interval_per_tick) {
        host_frametime=host_state.interval_per_tick;
        ++host_tickcount;
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
    // SIGNONSTATE_FULL can arrive in the reconnect packet pump. The client
    // entities are only ready for rendering after the normal frame update.
    clientFrameReady=true;
    return true;
}
extern "C" bool SourceIOSDrawPortalGame(int width,int height,char *detail,size_t capacity) {
    if (!playing || !g_ClientDLL || width<1 || height<1) return false;
    if (videomode->GetModeWidth()!=width || videomode->GetModeHeight()!=height)
        SourceIOSUpdateVideoMode();
    EngineVGui()->Simulate();
    if (!cl.IsActive() || !clientFrameReady) {
        EngineVGui()->Paint(PAINT_UIPANELS);
        Host_UpdateSounds();
        saverestore->OnFrameRendered();
        return true;
    }
    ClientDLL_FrameStageNotify(FRAME_RENDER_START);
    g_EngineRenderer->FrameBegin();
    cl.UpdateAreaBits_BackwardsCompatible();
    vrect_t rect={}; rect.width=width; rect.height=height;
    g_ClientDLL->View_Render(&rect);
    ClientDLL_FrameStageNotify(FRAME_RENDER_END);
    g_EngineRenderer->FrameEnd();
    // View_Render publishes the original listener state via Host_SetAudioState.
    Host_UpdateSounds();
    saverestore->OnFrameRendered();
    return true;
}

extern "C" void SourceIOSRequestQuit() { quitRequested=true; }
extern "C" bool SourceIOSFinishRequestedQuit() {
    if (!quitRequested) return false;
    // Complete Save & Quit's queued native save before the UIKit host exits.
    Cbuf_Execute();
    if (saveStarted) saverestore->FinishAsyncSave();
    if (audioStarted) { S_Shutdown(); audioStarted=false; }
    return true;
}
