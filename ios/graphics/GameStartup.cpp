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
#include "tier3/tier3.h"
#include "server_class.h"
#include "tier0/icommandline.h"
#include "vgui_baseui_interface.h"
#include "ivideomode.h"
#include "vgui/IPanel.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "igame.h"
#include "inputsystem/iinputsystem.h"
#include "cmd.h"
#include "console.h"
#include "keys.h"
#include "GameEventManager.h"
#include "game/client/iclientrendertargets.h"
#include "toolframework/itoolframework.h"
#include "materialproxyfactory.h"

extern CGlobalVars g_ServerGlobalVariables;
extern CreateInterfaceFn g_ClientFactory;
extern IClientRenderTargets *g_pClientRenderTargets;
extern void InitWellKnownRenderTargets();
extern void ShutdownWellKnownRenderTargets();
extern void SourceIOSInitRenderMaterials();
extern void SourceIOSShutdownRenderMaterials();
extern CSysModule *g_ClientDLLModule;
extern void Con_ColorPrintf(const Color &color,const char *format,...);
extern "C" void SourceIOSShutdownPortalLevel();
namespace {
bool initialized=false,clientInitialized=false,uiInitialized=false,eventsInitialized=false,clientTargetsInitialized=false,toolsInitialized=false;
ConVar *engineCheats=NULL;
const char *gameCvarName=!strcmp(SOURCE_IOS_GAME,"portal") ? "sv_portal_placement_never_fail" : "sk_plr_dmg_pistol";
CreateInterfaceFn applicationFactory=NULL;
CMaterialProxyFactory portalProxyFactory;
IMaterialProxyFactory *previousProxyFactory=NULL;
bool proxyFactoryInstalled=false;
SpewOutputFunc_t previousSpewOutput=NULL;
SpewRetval_t ConsoleSpew(SpewType_t type,const char *message) {
    Con_ColorPrintf(*GetSpewOutputColor(),"%s",message);
    return previousSpewOutput(type,message);
}
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
    g_iServerGameClientsVersion=serverGameClients?4:0;
    if (!serverGameDLL || !serverGameClients) { snprintf(detail,capacity,"Portal server: game interface unavailable"); return false; }
    g_ServerGlobalVariables.maxClients=1;
    g_ServerGlobalVariables.interval_per_tick=serverGameDLL->GetTickInterval();
    engineCheats=g_pCVar->FindVar("sv_cheats");
    const char *root=getenv("SOURCE_IOS_GAME_ROOT");
    if (root) Q_snprintf(com_gamedir,sizeof(com_gamedir),"%s/%s",root,SOURCE_IOS_GAME);
    eventsInitialized=g_GameEventManager.Init();
    if (!eventsInitialized) { snprintf(detail,capacity,"Portal server: game events unavailable"); return false; }
    initialized=serverGameDLL->DLLInit(StartupFactory,StartupFactory,StartupFactory,&g_ServerGlobalVariables);
    snprintf(detail,capacity,"Portal server DLLInit: %s%s%s",initialized?"PASS":"FAIL",
        !initialized && !lastAvailable?"; unavailable interface ":"",
        !initialized && !lastAvailable?lastInterface:"");
    if (initialized) {
        ConVar *gameCvar=g_pCVar->FindVar(gameCvarName);
        int classes=0;
        for (ServerClass *type=serverGameDLL->GetAllServerClasses();type;type=type->m_pNext) ++classes;
        bool valid=gameCvar && gameCvar->IsFlagSet(FCVAR_GAMEDLL) && engineCheats &&
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
    if (previousSpewOutput) {
        SpewOutputFunc(previousSpewOutput);
        previousSpewOutput=NULL;
        Con_Shutdown();
    }
    SourceIOSShutdownPortalLevel();
    if (clientTargetsInitialized) {
        SourceIOSShutdownRenderMaterials();
        if (g_pClientRenderTargets) g_pClientRenderTargets->ShutdownClientRenderTargets();
        else ShutdownWellKnownRenderTargets();
    }
    clientTargetsInitialized=false; g_pClientRenderTargets=NULL;
    if (clientInitialized && g_ClientDLL) {
        Msg("iOS Portal client Shutdown: begin\n");
        ClientDLL_Shutdown();
        valid=!g_pCVar->FindVar("cl_drawhud");
        Msg("iOS Portal client Shutdown + client cvar cleanup: %s\n",valid?"PASS":"FAIL");
    }
    clientInitialized=false; g_ClientDLL=NULL; g_ClientFactory=NULL;
    if (toolsInitialized) { toolframework->Shutdown(); toolframework->Disconnect(); toolsInitialized=false; }
    if (uiInitialized) {
        EngineVGui()->Shutdown();
        valid=valid && !EngineVGui()->IsInitialized() && !EngineVGui()->GetPanel(PANEL_CLIENTDLL);
        Msg("iOS engine VGUI Shutdown + root cleanup: %s\n",valid?"PASS":"FAIL");
    }
    uiInitialized=false;
    if (proxyFactoryInstalled) {
        materials->SetMaterialProxyFactory(previousProxyFactory);
        previousProxyFactory=NULL; proxyFactoryInstalled=false;
    }
    g_ClientDLLModule=NULL;
    VideoMode_Destroy();
    if (initialized && serverGameDLL) {
        serverGameDLL->DLLShutdown();
        valid=valid && !g_pCVar->FindVar(gameCvarName) &&
            engineCheats && g_pCVar->FindVar("sv_cheats")==engineCheats;
    }
    if (eventsInitialized) g_GameEventManager.Shutdown();
    eventsInitialized=false;
    initialized=false; serverGameDLL=NULL; serverGameClients=NULL;
    g_ServerFactory=NULL; g_iServerGameDLLVersion=0; g_iServerGameClientsVersion=0;
    applicationFactory=NULL; return valid;
}

extern "C" bool SourceIOSInitializePortalClient(CSysModule *module,char *detail,size_t capacity) {
    CreateInterfaceFn gameFactory=module?Sys_GetFactory(module):NULL;
    if (!initialized || !gameFactory) { snprintf(detail,capacity,"Portal client: server/factory unavailable"); return false; }
    applicationFactory=g_AppSystemFactory;
    if (!toolsInitialized) {
        if (!toolframework->Connect(applicationFactory) || toolframework->Init()!=INIT_OK) {
            snprintf(detail,capacity,"Portal tool framework initialization failed"); return false;
        }
        toolsInitialized=true;
    }
    // The desktop Shader_Connect path installs this original engine factory.
    // It resolves game material proxies through the real loaded client DLL.
    g_ClientDLLModule=module;
    if (!proxyFactoryInstalled) {
        previousProxyFactory=materials->GetMaterialProxyFactory();
        materials->SetMaterialProxyFactory(&portalProxyFactory);
        proxyFactoryInstalled=true;
    }
    const struct { ButtonCode_t key; const char *command; } bindings[]={
        {KEY_W,"+forward"},{KEY_S,"+back"},{KEY_A,"+moveleft"},{KEY_D,"+moveright"},
        {KEY_SPACE,"+jump"},{KEY_E,"+use"},{KEY_LCONTROL,"+duck"},
        {KEY_LEFT,"+left"},{KEY_RIGHT,"+right"},{KEY_UP,"+lookup"},{KEY_DOWN,"+lookdown"},
        {KEY_F,"+attack"},{KEY_G,"+attack2"},{KEY_F5,"save quick"},{KEY_F9,"load quick"}
    };
    for (const auto &binding : bindings)
        if (!Key_BindingForKey(binding.key) || !*Key_BindingForKey(binding.key))
            Key_SetBinding(binding.key,binding.command);
    if (!strcmp(SOURCE_IOS_GAME,"portal")) {
        IMaterialProxy *openProxy=portalProxyFactory.CreateProxy("PortalOpenAmount");
        if (!openProxy) { snprintf(detail,capacity,"Portal material proxy factory unavailable"); return false; }
        portalProxyFactory.DeleteProxy(openProxy);
    }
    if (!uiInitialized) {
        // Map loading connected tier 3 before the optional UI services existed.
        // Refresh those bindings now that the full real service group is loaded.
        g_pInputSystem=static_cast<IInputSystem *>(applicationFactory(INPUTSYSTEM_INTERFACE_VERSION,NULL));
        if (!g_pInputSystem) { snprintf(detail,capacity,"Portal client: input system unavailable"); return false; }
        DisconnectTier3Libraries();
        ConnectTier3Libraries(&applicationFactory,1);
        VideoMode_Create();
        if (!videomode || !videomode->Init()) {
            snprintf(detail,capacity,"Portal client: UIKit video mode unavailable"); return false;
        }
        Msg("iOS engine VGUI Init: begin (%d x %d)\n",videomode->GetModeUIWidth(),videomode->GetModeUIHeight());
        EngineVGui()->Init();
        uiInitialized=EngineVGui()->IsInitialized();
        const VGuiPanel_t roots[]={PANEL_ROOT,PANEL_CLIENTDLL,PANEL_GAMEUIDLL,PANEL_TOOLS,PANEL_GAMEDLL,PANEL_CLIENTDLL_TOOLS};
        for (VGuiPanel_t type : roots) {
            vgui::VPANEL panel=EngineVGui()->GetPanel(type);
            int width=0,height=0;
            if (panel) g_pVGuiPanel->GetSize(panel,width,height);
            if (!panel || width!=videomode->GetModeUIWidth() || height!=videomode->GetModeUIHeight()) {
                snprintf(detail,capacity,"Portal client: engine VGUI root %d size/handle invalid",int(type)); return false;
            }
        }
        if (g_pVGuiPanel->GetParent(EngineVGui()->GetPanel(PANEL_ROOT))!=g_pVGuiSurface->GetEmbeddedPanel()) {
            snprintf(detail,capacity,"Portal client: engine VGUI root is detached"); return false;
        }
        Msg("iOS engine VGUI Init: %s\n",uiInitialized?"PASS":"FAIL");
    }
    if (applicationFactory("MatSystemSurface008",NULL) && !EngineVGui()->GetPanel(PANEL_CLIENTDLL)) {
        snprintf(detail,capacity,"Portal server DLLInit: PASS; client Init blocked: engine VGUI root panels are not initialized");
        Msg("%s\n",detail); return false;
    }
    lastInterface[0]=0;
    g_ClientFactory=gameFactory;
    g_ClientDLL=static_cast<IBaseClientDLL *>(gameFactory(CLIENT_DLL_INTERFACE_VERSION,NULL));
    bool started=g_ClientDLL!=NULL;
    if (started) ClientDLL_Init();
    clientInitialized=started;
    if (started) {
        const char *mode=getenv("SOURCE_IOS_GAME_STARTUP");
        if (mode && (!strcmp(mode,"play") || !strcmp(mode,"menu"))) {
            g_pClientRenderTargets=static_cast<IClientRenderTargets *>(gameFactory(CLIENTRENDERTARGETS_INTERFACE_VERSION,NULL));
            if (!g_pClientRenderTargets && !strcmp(SOURCE_IOS_GAME,"portal")) { snprintf(detail,capacity,"Portal client render targets unavailable"); return false; }
            if (g_pClientRenderTargets) {
                materials->BeginRenderTargetAllocation();
                g_pClientRenderTargets->InitClientRenderTargets(materials,g_pMaterialSystemHardwareConfig);
                materials->EndRenderTargetAllocation();
            } else {
                InitWellKnownRenderTargets();
            }
            clientTargetsInitialized=true;
            SourceIOSInitRenderMaterials();
        }
        g_ClientDLL->PostInit();
        serverGameDLL->PostInit();
        EngineVGui()->Connect(); EngineVGui()->PostInit();
        // The standalone host also needs the engine's console output path.
        Con_Init();
        SpewActivate("console",1);
        previousSpewOutput=GetSpewOutputFunc();
        SpewOutputFunc(ConsoleSpew);
        // The host dispatches input through the engine before VGUI simulates.
        // Prevent the surface from consuming the same events a second time.
        g_pMatSystemSurface->AttachToWindow(NULL);
        // Schemes can load before their sizing panels have drawable bounds.
        // Rebuild proportional fonts once the complete UI hierarchy exists.
        int previousWidth, previousHeight;
        g_pVGuiSurface->GetScreenSize(previousWidth, previousHeight);
        bool previousOverride = g_pVGuiSurface->ForceScreenSizeOverride(true,
            videomode->GetModeUIWidth(), videomode->GetModeUIHeight());
        g_pVGuiSurface->OnScreenSizeChanged(previousWidth, previousHeight);
        g_pVGuiSurface->ForceScreenSizeOverride(previousOverride, previousWidth, previousHeight);
        // Menu font handles can exist with no glyphs when resource platform
        // conditions discard every face. Check the real initialized UI scheme.
        auto uiScheme=g_pVGuiSchemeManager->GetIScheme(g_pVGuiSchemeManager->GetDefaultScheme());
        vgui::HFont menuFont=uiScheme ? uiScheme->GetFont("MenuLarge",true) : 0;
        if (!menuFont || g_pVGuiSurface->GetFontTall(menuFont)<=0 ||
            g_pVGuiSurface->GetCharacterWidth(menuFont,L'R')<=0) {
            snprintf(detail,capacity,"Portal UI menu font has no glyphs"); return false;
        }
        ConVar *hud=g_pCVar->FindVar("cl_drawhud");
        started=hud && hud->IsFlagSet(FCVAR_CLIENTDLL) && g_pCVar->FindVar("sv_cheats")==engineCheats;
    }
    // The client also checks tier-connected globals after its direct queries.
    if (!started && lastAvailable && !applicationFactory("MatSystemSurface008",NULL)) {
        Q_strncpy(lastInterface,"MatSystemSurface008",sizeof(lastInterface));
        lastAvailable=false;
    }
    if (!started && !clientInitialized) { g_ClientDLL=NULL; g_ClientFactory=NULL; }
    snprintf(detail,capacity,"Portal server DLLInit: PASS; client Init/PostInit + engine VGUI roots + client cvar ownership: %s%s%s",started?"PASS":"FAIL",
        !started && !lastAvailable?"; unavailable interface ":"",
        !started && !lastAvailable?lastInterface:"");
    Msg("%s\n",detail); return started;
}

extern "C" bool SourceIOSDispatchPortalInput() {
    if (!clientInitialized || !g_ClientDLL) return false;
    const InputEvent_t *events=g_pInputSystem->GetEventData();
    for (int i=0;i<g_pInputSystem->GetEventCount();++i)
        if (events[i].m_nType==IE_FingerDown || events[i].m_nType==IE_FingerUp)
            Msg("iOS live touch: type %d; slot %d; menu %d\n",events[i].m_nType,events[i].m_nData,EngineVGui()->IsGameUIVisible());
    game->DispatchAllStoredGameMessages();
    Cbuf_Execute();
    return true;
}
