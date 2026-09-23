#include "togles/rendermechanism.h"
#include "materialsystem/imaterialsystem.h"
#include "shaderapi/ishaderdevice.h"
#include "shaderapi/ishaderutil.h"
#include "tier1/tier1.h"
#include "vstdlib/cvar.h"

namespace {
const GLMContextHost *applicationHost=NULL;
void *ApplicationFactory(const char *name, int *status)
{
    if (!strcmp(name,TOGLES_CONTEXT_HOST_INTERFACE_VERSION)) {
        if (status) *status=applicationHost ? IFACE_OK : IFACE_FAILED;
        return const_cast<GLMContextHost *>(applicationHost);
    }
    return VStdLib_GetICVarFactory()(name,status);
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
    if (material) {
        material->SetShaderAPI(shaderPath);
        IShaderDeviceMgr *manager=static_cast<IShaderDeviceMgr *>(material->QueryInterface(SHADER_DEVICE_MGR_INTERFACE_VERSION));
        IShaderUtil *util=static_cast<IShaderUtil *>(material->QueryInterface(SHADER_UTIL_INTERFACE_VERSION));
        if (manager && util) {
            connected=material->Connect(ApplicationFactory);
            initialized=connected && manager->Init()==INIT_OK;
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
            if (initialized) manager->Shutdown();
        } else snprintf(detail,capacity,"Material/shader API interface lookup failed");
        if (connected) material->Disconnect();
    } else snprintf(detail,capacity,"Material system interface lookup failed");
    applicationHost=NULL;
    Sys_UnloadModule(module);
    valid=valid && gGL && g_pCVar && g_pCVar->FindVar("gl_blitmode") && gGL->glGetError()==GL_NO_ERROR;
    return valid;
}
