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

namespace {
// Process-owned service shared with ToGLES, matching the static cvar lifetime.
IFileSystem *filesystem=NULL;
const unsigned char payload[]={0,255,128,1,2,3,4,5,6,7,8,9};
struct AsyncResult {
    CThreadEvent finished;
    ThreadId_t caller;
    bool valid;
    bool missing;
    bool packed;
    AsyncResult(bool absent, bool archive):caller(ThreadGetCurrentId()),valid(false),missing(absent),packed(archive) {}
};
void ReadFinished(const FileAsyncRequest_t &request, int bytes, FSAsyncStatus_t status)
{
    AsyncResult *result=static_cast<AsyncResult *>(request.pContext);
    result->valid=result->missing ? status==FSASYNC_ERR_FILEOPEN :
        status==FSASYNC_OK && bytes==5 && request.pData && !memcmp(request.pData,result->packed ? static_cast<const void *>("nlitG") : payload+2,5);
    result->valid=result->valid && ThreadGetCurrentId()!=result->caller;
    result->finished.Set();
}
bool AsyncCheck(bool missing, bool packed=false)
{
    AsyncResult result(missing,packed);
    FileAsyncRequest_t request;
    request.pszFilename=missing ? "missing.bin" : packed ? "materials/ios/packed.vmt" : "checks/renamed.bin";
    request.pszPathID=packed ? "PACKED" : "GAMEWRITE";
    request.nOffset=2; request.nBytes=5;
    request.pfnCallback=ReadFinished; request.pContext=&result;
    FSAsyncControl_t control=NULL;
    FSAsyncStatus_t status=filesystem->AsyncRead(request,&control);
    bool valid=status>=FSASYNC_OK && control && result.finished.Wait(5000);
    if (control) {
        // Keep callback state alive until the worker has finished, even on failure.
        filesystem->AsyncFinish(control,true);
        filesystem->AsyncRelease(control);
    }
    return valid && result.valid;
}
}

int CheckToGLESFilesystem(const char *assets, const char *writable, char *detail, size_t capacity)
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
    bool valid=g_pFullFileSystem==filesystem;
    filesystem->RemoveAllSearchPaths();
    filesystem->CreateDirHierarchy(writable);
    filesystem->AddSearchPath(assets,"GAME");
    filesystem->AddSearchPath(writable,"GAMEWRITE");
    filesystem->MarkPathIDByRequestOnly("GAMEWRITE",true);
    char archive[MAX_PATH];
    Q_snprintf(archive,sizeof(archive),"%s/probe_dir.vpk",assets);
    filesystem->AddSearchPath(archive,"PACKED");
    filesystem->MarkPathIDByRequestOnly("PACKED",true);
    KeyValues *material=new KeyValues("material");
    valid=valid && material->LoadFromFile(filesystem,"materials/ios/probe.vmt","GAME");
    valid=valid && !Q_strcmp(material->GetName(),"UnlitGeneric")
        && !Q_strcmp(material->GetString("$basetexture"),"ios/probe")
        && material->GetInt("$vertexcolor")==1 && material->GetInt("$vertexalpha")==1;
    material->deleteThis();
    KeyValues *packed=new KeyValues("material");
    valid=valid && packed->LoadFromFile(filesystem,"materials/ios/packed.vmt","PACKED")
        && !Q_strcmp(packed->GetName(),"UnlitGeneric")
        && !Q_strcmp(packed->GetString("$basetexture"),"ios/probe");
    packed->deleteThis();
    snprintf(detail,capacity,"Engine filesystem: bundled VMT load failed");
    if (!valid) return 0;
    filesystem->CreateDirHierarchy("checks","GAMEWRITE");
    CUtlBuffer written;
    written.Put(payload,sizeof(payload));
    valid=filesystem->WriteFile("checks/data.bin","GAMEWRITE",written);
    valid=valid && filesystem->RenameFile("checks/data.bin","checks/renamed.bin","GAMEWRITE");
    CUtlBuffer read;
    valid=valid && filesystem->ReadFile("checks/renamed.bin","GAMEWRITE",read)
        && read.TellPut()==sizeof(payload) && !memcmp(read.Base(),payload,sizeof(payload));
    FileHandle_t file=filesystem->Open("checks/renamed.bin","rb","GAMEWRITE");
    unsigned char slice[5]={};
    if (file) {
        filesystem->Seek(file,2,FILESYSTEM_SEEK_HEAD);
        valid=valid && filesystem->Read(slice,sizeof(slice),file)==sizeof(slice) && !memcmp(slice,payload+2,sizeof(slice));
        filesystem->Close(file);
    } else valid=false;
    FileFindHandle_t find;
    const char *name=filesystem->FindFirstEx("checks/*.bin","GAMEWRITE",&find);
    valid=valid && name && !Q_strcmp(name,"renamed.bin");
    if (name) filesystem->FindClose(find);
    valid=valid && !filesystem->FileExists("checks/renamed.bin","GAME")
        && !filesystem->FileExists("missing.bin","GAME");
    snprintf(detail,capacity,"Engine filesystem: sandbox write/seek/search failed");
    if (valid) {
        valid=AsyncCheck(false) && AsyncCheck(true) && AsyncCheck(false,true);
        snprintf(detail,capacity,"Engine filesystem: worker read/missing-file callback failed");
    }
    filesystem->RemoveFile("checks/renamed.bin","GAMEWRITE");
    valid=valid && !filesystem->FileExists("checks/renamed.bin","GAMEWRITE");
    if (valid) snprintf(detail,capacity,"Filesystem VMT/VPK + I/O + async: PASS");
    return valid;
}
