#include "ToGLESRuntime.h"
#include "togles/rendermechanism.h"
#include "EngineServices.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"
#include "tier1/strtools.h"
#include "tier0/threadtools.h"
#include "vstdlib/cvar.h"

namespace { IFileSystem *filesystem=NULL; }
int InitializeIOSFilesystem(const char *assets, char *detail, size_t capacity)
{
    if (!InitializeEngineServices(detail,capacity)) return 0;
    if (!filesystem) {
        CreateInterfaceFn factory=VStdLib_GetICVarFactory();
        IFileSystem *candidate=static_cast<IFileSystem *>(factory(FILESYSTEM_INTERFACE_VERSION,NULL));
        if (!candidate || !candidate->Connect(factory) || candidate->Init()!=INIT_OK) {
            snprintf(detail,capacity,"Engine filesystem initialization failed"); return 0;
        }
        if (ToGLConnectLibraries(factory)!=gGL || g_pFullFileSystem!=candidate) {
            snprintf(detail,capacity,"ToGLES filesystem service connection failed"); return 0;
        }
        filesystem=candidate;
    }
    filesystem->AddSearchPath(assets,"GAME",PATH_ADD_TO_TAIL);
    return 1;
}
