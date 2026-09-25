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

bool DrawMaterialFixture(IMaterialSystem *material, IMatRenderContext *context, bool alpha, char *detail, size_t capacity)
{
    const char *name=alpha ? "ios/draw-alpha" : "ios/draw";
    IMaterial *draw=material->FindMaterial(name,TEXTURE_GROUP_OTHER,true);
    if (!draw || draw->IsErrorMaterial() || Q_stricmp(draw->GetShaderName(),"IOSProbe")
        || draw->GetMappingWidth()!=4 || draw->GetMappingHeight()!=4) {
        snprintf(detail,capacity,"%s: IOSProbe VMT/VTF load failed",name); return false;
    }
    draw->IncrementReferenceCount();
    context->Bind(draw);
    IMesh *mesh=context->GetDynamicMesh(true);
    CMeshBuilder builder;
    builder.Begin(mesh,MATERIAL_TRIANGLES,1);
    builder.Position3f(-1,-1,.5f); builder.TexCoord2f(0,0,1); builder.AdvanceVertex();
    builder.Position3f(3,-1,.5f); builder.TexCoord2f(0,2,1); builder.AdvanceVertex();
    builder.Position3f(-1,3,.5f); builder.TexCoord2f(0,0,-1); builder.AdvanceVertex();
    builder.End();
    mesh->Draw();
    unsigned char pixels[8*8*4]={};
    context->ReadPixels(0,0,8,8,pixels,IMAGE_FORMAT_RGBA8888);
    unsigned char expected[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
    if (alpha) { expected[1][3]=192; expected[2][3]=128; expected[3][3]=64; }
    bool valid=true;
    for (int y=0;y<8 && valid;++y) for (int x=0;x<8 && valid;++x) {
        const unsigned char *pixel=&pixels[(y*8+x)*4];
        const unsigned char *color=expected[(y/4)*2+x/4];
        if (memcmp(pixel,color,4)) {
            snprintf(detail,capacity,"%s pixel (%d,%d): %u,%u,%u,%u expected %u,%u,%u,%u",
                name,x,y,pixel[0],pixel[1],pixel[2],pixel[3],color[0],color[1],color[2],color[3]);
            valid=false;
        }
    }
    draw->DecrementReferenceCount();
    return valid;
}


bool DrawStandardMaterial(IMaterialSystem *material, IMatRenderContext *context, char *detail, size_t capacity)
{
    const unsigned char colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
    const unsigned char background[4]={37,91,163,255};
    const MaterialMatrixMode_t modes[]={MATERIAL_MODEL,MATERIAL_VIEW,MATERIAL_PROJECTION};
    for (MaterialMatrixMode_t mode : modes) { context->MatrixMode(mode); context->PushMatrix(); }
    bool valid=true;
    for (int pass=0;pass<4 && valid;++pass) {
        VMatrix model,view,projection;
        model.Identity(); view.Identity(); projection.Identity();
        if (pass==1) {
            model[0][0]=model[1][1]=.5f;
            model[0][3]=-.5f; model[1][3]=.5f;
        } else if (pass==2) {
            // Noncommuting transforms: projection * view * model gives a
            // half-size quad centered at (+.5,-.5), not at (+1,-1).
            model[0][3]=1; model[1][3]=-1;
            view[0][0]=view[1][1]=2;
            projection[0][0]=projection[1][1]=.25f;
        }
        context->MatrixMode(MATERIAL_MODEL); context->LoadMatrix(model);
        context->MatrixMode(MATERIAL_VIEW); context->LoadMatrix(view);
        context->MatrixMode(MATERIAL_PROJECTION); context->LoadMatrix(projection);
        const char *name=pass ? "ios/standard-transform" : "ios/standard";
        IMaterial *draw=material->FindMaterial(name,TEXTURE_GROUP_OTHER,true);
        valid=draw && !draw->IsErrorMaterial() && !Q_stricmp(draw->GetShaderName(),"screenspace_general_dx9");
        if (!valid) { snprintf(detail,capacity,"%s: standard shader lookup failed",name); break; }
        draw->IncrementReferenceCount();
        context->ClearColor4ub(37,91,163,255);
        context->ClearBuffers(true,true,true);
        context->Bind(draw);
        IMesh *mesh=context->GetDynamicMesh(true);
        CMeshBuilder builder;
        builder.Begin(mesh,MATERIAL_TRIANGLES,2);
        const float vertices[6][4]={{-1,-1,0,1},{1,-1,1,1},{1,1,1,0},
                                  {-1,-1,0,1},{1,1,1,0},{-1,1,0,0}};
        for (const auto &vertex : vertices) {
            builder.Position3f(vertex[0],vertex[1],.5f);
            builder.TexCoord2f(0,vertex[2],vertex[3]);
            if (pass) builder.Color4ub(255,255,255,255);
            builder.AdvanceVertex();
        }
        builder.End(); mesh->Draw();
        unsigned char pixels[8*8*4]={};
        context->ReadPixels(0,0,8,8,pixels,IMAGE_FORMAT_RGBA8888);
        const int left=pass==2 ? 4 : 0, top=pass==2 ? 4 : 0;
        const int size=pass==1 || pass==2 ? 4 : 8;
        for (int y=0;y<8 && valid;++y) for (int x=0;x<8 && valid;++x) {
            bool inside=x>=left && x<left+size && y>=top && y<top+size;
            const unsigned char *expected=inside ? colors[((y-top)/(size/2))*2+(x-left)/(size/2)] : background;
            const unsigned char *pixel=&pixels[4*(y*8+x)];
            if (memcmp(pixel,expected,4)) {
                snprintf(detail,capacity,"Standard matrix pass %d pixel (%d,%d): %u,%u,%u,%u expected %u,%u,%u,%u",
                    pass,x,y,pixel[0],pixel[1],pixel[2],pixel[3],expected[0],expected[1],expected[2],expected[3]);
                valid=false;
            }
        }
        draw->DecrementReferenceCount();
    }
    for (MaterialMatrixMode_t mode : modes) { context->MatrixMode(mode); context->PopMatrix(); }
    return valid;
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
                    bool clearValid=true;
                    for (int i=0;i<64;++i) clearValid=clearValid && pixels[i*4]==37 && pixels[i*4+1]==91
                        && pixels[i*4+2]==163 && pixels[i*4+3]==255;
                    valid=valid && clearValid;
                    if (!clearValid) snprintf(detail,capacity,"Material clear/readback mismatch: %u,%u,%u,%u",pixels[0],pixels[1],pixels[2],pixels[3]);
                    for (int pass=0;valid && pass<3;++pass)
                        valid=DrawMaterialFixture(material,context,pass==1,detail,capacity);
                    if (valid) valid=DrawStandardMaterial(material,context,detail,capacity);
                    context->EndRender();
                    context->Release();
                    material->EndFrame();

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
