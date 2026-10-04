#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "shaderapi/ishaderdevice.h"
#include "shaderapi/ishaderutil.h"
#include "tier1/tier1.h"
#include "vstdlib/cvar.h"
#include "filesystem.h"
#include "materialsystem/materialsystem_config.h"
#include "tier2/tier2.h"
#include "materialsystem/ishader.h"
#include "materialsystem/imesh.h"
#include "mathlib/vmatrix.h"

#include "MapServices.h"
extern "C" int InitializePortalGame(const char *,CreateInterfaceFn,char *,size_t);

namespace {
const GLMContextHost *applicationHost=NULL;
IMaterialSystem *applicationMaterial=NULL;
CSysModule *liveModule=NULL;
IMaterialSystem *liveMaterial=NULL;
void *ApplicationFactory(const char *name, int *status)
{
    if (!strcmp(name,TOGLES_CONTEXT_HOST_INTERFACE_VERSION)) {
        if (status) *status=applicationHost ? IFACE_OK : IFACE_FAILED;
        return const_cast<GLMContextHost *>(applicationHost);
    }
    void *service=VStdLib_GetICVarFactory()(name,status);
    if (!service && applicationMaterial) service=applicationMaterial->QueryInterface(name);
    if (!service) service=QueryMapService(name);
    if (status) *status=service ? IFACE_OK : IFACE_FAILED;
    return service;
}

}

bool InitializeIOSMaterial(const GLMContextHost *host, const char *modules, char *detail, size_t capacity)
{
    // Mount game data before shader caches are opened.
    if (!MountRequestedWorldData(detail,capacity)) return false;
    char materialPath[MAX_PATH],shaderPath[MAX_PATH];
    Q_snprintf(materialPath,sizeof(materialPath),"%s/libmaterialsystem.dylib",modules);
    Q_snprintf(shaderPath,sizeof(shaderPath),"%s/libshaderapidx9.dylib",modules);
    CSysModule *module=Sys_LoadModule(materialPath);
    if (!module) { snprintf(detail,capacity,"Material system module load failed"); return false; }
    CreateInterfaceFn factory=Sys_GetFactory(module);
    IMaterialSystem *material=factory ? static_cast<IMaterialSystem *>(factory(MATERIAL_SYSTEM_INTERFACE_VERSION,NULL)) : NULL;
    bool valid=false,connected=false,initialized=false;
    static GLMContextHost hosted;
    hosted=*host;
    applicationHost=&hosted;
    applicationMaterial=material;
    if (material) {
        material->SetShaderAPI(shaderPath);
        IShaderDeviceMgr *manager=static_cast<IShaderDeviceMgr *>(material->QueryInterface(SHADER_DEVICE_MGR_INTERFACE_VERSION));
        IShaderUtil *util=static_cast<IShaderUtil *>(material->QueryInterface(SHADER_UTIL_INTERFACE_VERSION));
        if (manager && util) {
            connected=material->Connect(ApplicationFactory);
            g_pFullFileSystem->AddSearchPath(modules,"EXECUTABLE_PATH");
            g_pFullFileSystem->MarkPathIDByRequestOnly("EXECUTABLE_PATH",true);
            material->SetAdapter(0,MATERIAL_INIT_ALLOCATE_FULLSCREEN_TEXTURE);
            ConVarRef("mat_queue_mode").SetValue(0);
            initialized=connected && material->Init()==INIT_OK;
            valid=initialized && manager->GetAdapterCount()==1;
            if (valid) {
                MaterialAdapterInfo_t info={};
                ShaderDisplayMode_t mode;
                manager->GetAdapterInfo(0,info);
                manager->GetCurrentModeInfo(&mode,0);
                uint width=0,height=0;
                host->displayedSize(host->userData,width,height);
                valid=info.m_pDriverName[0] && info.m_nMaxDXSupportLevel>=90
                    && mode.m_nWidth==width && mode.m_nHeight==height;
                snprintf(detail,capacity,"Shader API caps: DX %d, drawable %dx%d (%s)",
                    info.m_nMaxDXSupportLevel,mode.m_nWidth,mode.m_nHeight,valid?"PASS":"FAIL");
            } else snprintf(detail,capacity,"Material/shader API Connect or adapter Init failed");
            if (initialized) {
                bool unlit=false,debug=false;
                for (int i=0;i<material->ShaderCount();++i) {
                    IShader *shader=NULL;
                    if (material->GetShaders(i,1,&shader)!=1 || !shader) continue;
                    unlit=unlit || !Q_stricmp(shader->GetName(),"UnlitGeneric");
                    debug=debug || !Q_stricmp(shader->GetName(),"DebugNormalMap");
                }
                valid=valid && unlit && debug;
                if (!valid) snprintf(detail,capacity,"Required standard shaders missing");
                material->ModInit();
                MaterialSystem_Config_t config=material->GetCurrentConfigForVideoCard();
                config.m_VideoMode.m_Width=8;
                config.m_VideoMode.m_Height=8;
                config.m_VideoMode.m_Format=IMAGE_FORMAT_BGRA8888;
                config.dxSupportLevel=90;
                config.m_nAASamples=0;
                config.SetFlag(MATSYS_VIDCFG_FLAGS_WINDOWED,true);
                config.SetFlag(MATSYS_VIDCFG_FLAGS_STENCIL,true);
                if (valid) {
                    valid=material->SetMode(NULL,config);
                    GLenum modeError=gGL->glGetError();
                    valid=valid && modeError==GL_NO_ERROR;
                    if (!valid) snprintf(detail,capacity,"Material SetMode failed (GL 0x%x)",modeError);
                }
                if (valid) valid=InitializeMapServices(modules,ApplicationFactory,detail,capacity);
                if (valid) valid=InitializePortalGame(modules,ApplicationFactory,detail,capacity);
                if (valid) valid=LoadRequestedWorldMap(detail,capacity);
                if (valid) {
                    liveModule=module;
                    liveMaterial=material;
                    return true;
                }
                ShutdownMapServices();
                material->ModShutdown();
                material->Shutdown();
            }
        } else snprintf(detail,capacity,"Material/shader API interface lookup failed");
        if (connected) material->Disconnect();
    } else snprintf(detail,capacity,"Material system interface lookup failed");
    applicationHost=NULL;
    applicationMaterial=NULL;
    Sys_UnloadModule(module);
    GLenum teardownError=gGL ? gGL->glGetError() : GL_INVALID_OPERATION;
    if (valid && teardownError!=GL_NO_ERROR) snprintf(detail,capacity,"Material teardown GL 0x%x",teardownError);
    bool services=gGL && g_pCVar && g_pCVar->FindVar("gl_blitmode");
    bool released=GLMgr::aGLMgr()->GetCurrentContext()==NULL;
    if (valid && (!services || !released)) snprintf(detail,capacity,"Material teardown lost services or retained its context");
    valid=valid && services && released && teardownError==GL_NO_ERROR;
    return valid;
}

// These entry points run only on the UIKit render thread. The borrowed host
// and its native context must outlive the retained material system.
bool DrawToGLESLiveMaterial(char *detail, size_t capacity)
{
    PollGameInput();
    if (!liveMaterial) { snprintf(detail,capacity,"No live material system"); return false; }
    const bool playing=HasLivePortalGame();
    if (playing && !AdvancePortalGame(detail,capacity)) return false;
    // SCR_BeginLoadingPlaque presents the original scene and loading dialog.
    // Keep that image while the host finishes loading and client signon.
    if (playing && IsGameLoading()) return true;
    uint targetWidth=0,targetHeight=0;
    applicationHost->displayedSize(applicationHost->userData,targetWidth,targetHeight);
    int width=0,height=0;
    liveMaterial->GetBackBufferDimensions(width,height);
    bool resize=width!=int(targetWidth) || height!=int(targetHeight);
    if (resize) {
        MaterialSystem_Config_t config=liveMaterial->GetCurrentConfigForVideoCard();
        config.m_VideoMode.m_Width=targetWidth;
        config.m_VideoMode.m_Height=targetHeight;
        liveMaterial->OverrideConfig(config,false);
    }
    liveMaterial->BeginFrame(0);
    IMatRenderContext *context=liveMaterial->GetRenderContext();
    context->BeginRender();
    context->Viewport(0,0,width,height);
    context->ClearColor4ub(37,91,163,255);
    context->ClearBuffers(true,true,true);
    bool valid=true;
    // Present the pending drawable resize before initializing client views.
    if (playing && !resize) valid=DrawPortalGame(width,height,detail,capacity);
    context->EndRender(); context->Release();
    liveMaterial->EndFrame();
    if (valid) liveMaterial->SwapBuffers();
    if (valid && resize) {
        int actualWidth=0,actualHeight=0;
        liveMaterial->GetBackBufferDimensions(actualWidth,actualHeight);
        if (actualWidth!=int(targetWidth) || actualHeight!=int(targetHeight)) {
            snprintf(detail,capacity,"Material resize %ux%u returned %dx%d",targetWidth,targetHeight,actualWidth,actualHeight);
            return false;
        }
    }
    GLenum error=gGL->glGetError();
    if (valid && error!=GL_NO_ERROR) snprintf(detail,capacity,"Live material GL error 0x%x",error);
    return valid && error==GL_NO_ERROR;
}

void StopToGLESLiveMaterial()
{
    if (!liveMaterial) return;
    ShutdownMapServices();
    liveMaterial->ModShutdown(); liveMaterial->Shutdown(); liveMaterial->Disconnect();
    liveMaterial=NULL; applicationMaterial=NULL; applicationHost=NULL;
    Sys_UnloadModule(liveModule); liveModule=NULL;
}
