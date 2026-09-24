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

namespace {
const GLMContextHost *applicationHost=NULL;
IMaterialSystem *applicationMaterial=NULL;
void *ApplicationFactory(const char *name, int *status)
{
    if (!strcmp(name,TOGLES_CONTEXT_HOST_INTERFACE_VERSION)) {
        if (status) *status=applicationHost ? IFACE_OK : IFACE_FAILED;
        return const_cast<GLMContextHost *>(applicationHost);
    }
    void *service=VStdLib_GetICVarFactory()(name,status);
    if (!service && applicationMaterial) service=applicationMaterial->QueryInterface(name);
    if (status) *status=service ? IFACE_OK : IFACE_FAILED;
    return service;
}
}

bool CheckToGLESMaterial(const GLMContextHost *host, const char *modules, char *detail, size_t capacity)
{
    char materialPath[MAX_PATH],shaderPath[MAX_PATH];
    Q_snprintf(materialPath,sizeof(materialPath),"%s/libmaterialsystem.dylib",modules);
    Q_snprintf(shaderPath,sizeof(shaderPath),"%s/libshaderapidx9.dylib",modules);
    CSysModule *module=Sys_LoadModule(materialPath);
    if (!module) { snprintf(detail,capacity,"Material system module load failed"); return false; }
    CreateInterfaceFn factory=Sys_GetFactory(module);
    IMaterialSystem *material=factory ? static_cast<IMaterialSystem *>(factory(MATERIAL_SYSTEM_INTERFACE_VERSION,NULL)) : NULL;
    bool valid=false,connected=false,initialized=false;
    applicationHost=host;
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
                if (valid) {
                    material->BeginFrame(0);
                    IMatRenderContext *context=material->GetRenderContext();
                    context->BeginRender();
                    context->Viewport(0,0,8,8);
                    context->ClearColor4ub(37,91,163,255);
                    context->ClearBuffers(true,true,true);
                    unsigned char pixels[8*8*4]={};
                    context->ReadPixels(0,0,8,8,pixels,IMAGE_FORMAT_RGBA8888);
                    context->EndRender();
                    context->Release();
                    material->EndFrame();
                    for (int i=0;i<64;++i) valid=valid && pixels[i*4]==37 && pixels[i*4+1]==91
                        && pixels[i*4+2]==163 && pixels[i*4+3]==255;
                    if (!valid) snprintf(detail,capacity,"Material clear/readback mismatch: %u,%u,%u,%u",pixels[0],pixels[1],pixels[2],pixels[3]);
                }
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
