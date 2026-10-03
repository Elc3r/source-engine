#include "EngineServices.h"
#include "vstdlib/cvar.h"
#include "icvar.h"
#include "tier1/tier1.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"

namespace { ICvar *service=NULL; }
bool InitializeEngineServices(char *detail, size_t capacity)
{
    if (service) return service==g_pCVar;
    CreateInterfaceFn factory=VStdLib_GetICVarFactory();
    ICvar *candidate=static_cast<ICvar *>(factory(CVAR_INTERFACE_VERSION,NULL));
    if (!candidate || !candidate->Connect(factory)) {
        snprintf(detail,capacity,"ICvar connection failed"); return false;
    }
    if (candidate->Init()!=INIT_OK || candidate!=g_pCVar) {
        candidate->Disconnect();
        snprintf(detail,capacity,"ICvar initialization failed"); return false;
    }
    service=candidate;
    return true;
}
