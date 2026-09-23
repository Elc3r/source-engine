#include "EngineServices.h"
#include "vstdlib/cvar.h"
#include "icvar.h"
#include "tier1/tier1.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"

namespace {
ICvar *service=NULL;
int callbacks=0;
float oldNumber=0;
char oldText[32]={};
void Changed(IConVar *variable, const char *previous, float number)
{
    if (Q_strcmp(variable->GetName(),"ios_service_check")) return;
    ++callbacks;
    oldNumber=number;
    Q_strncpy(oldText,previous,sizeof(oldText));
}
}

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

bool CheckEngineServices(char *detail, size_t capacity)
{
    if (!InitializeEngineServices(detail,capacity)) return false;
    bool valid=service->FindVar("gl_blitmode")!=NULL;
    callbacks=0;
    service->InstallGlobalChangeCallback(Changed);
    {
        ConVar variable("ios_service_check","0",0,"iOS cvar lifecycle probe",true,0,true,2);
        valid=valid && service->FindVar(variable.GetName())==&variable;
        variable.SetValue(1);
        valid=valid && callbacks==1 && oldNumber==0 && !Q_strcmp(oldText,"0") && variable.GetInt()==1;
        variable.SetValue(1);
        valid=valid && callbacks==1;
        variable.SetValue("1.5");
        valid=valid && callbacks==2 && oldNumber==1 && variable.GetFloat()==1.5f;
        variable.SetValue(10);
        valid=valid && callbacks==3 && oldNumber==1.5f && variable.GetInt()==2;
        variable.AddFlags(FCVAR_MATERIAL_SYSTEM_THREAD);
        variable.SetValue(1);
        valid=valid && variable.GetInt()==2 && callbacks==3 && service->HasQueuedMaterialThreadConVarSets();
        const int flags=service->ProcessQueuedMaterialThreadConVarSets();
        valid=valid && (flags & FCVAR_MATERIAL_SYSTEM_THREAD) && variable.GetInt()==1 && callbacks==4
            && !service->HasQueuedMaterialThreadConVarSets();
        // Dynamic ConVars require explicit unregistration before destruction.
        service->UnregisterConCommand(&variable);
    }
    service->RemoveGlobalChangeCallback(Changed);
    valid=valid && !service->FindVar("ios_service_check");
    snprintf(detail,capacity,"ICvar registration + callbacks + material queue + cleanup: %s",valid?"PASS":"FAIL");
    return valid;
}
