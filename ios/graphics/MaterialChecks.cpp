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
#include "SceneChecks.h"
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

// Observe the native window backbuffer immediately before the real EGL swap.
struct MaterialPresentation {
    const GLMContextHost *base;
    int swaps;
    bool valid, solid, worldPixelsVerified, gameSession;
    char failure[160];
    SceneSamples scene;
};
MaterialPresentation *livePresentation=NULL;
bool PresentationBind(void *data, void *context)
{
    auto *check=static_cast<MaterialPresentation *>(data);
    return check->base->makeCurrent(check->base->userData,context);
}
void PresentationSize(void *data, uint &width, uint &height)
{
    auto *check=static_cast<MaterialPresentation *>(data);
    check->base->displayedSize(check->base->userData,width,height);
}
bool PresentationSwap(void *data, CShowPixelsParams *params)
{
    auto *check=static_cast<MaterialPresentation *>(data);
    if (!params->m_onlySyncView) {
        uint width=0,height=0;
        PresentationSize(data,width,height);
        GLint draw=0,read=0;
        gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        if (!(width>=8 && height>=8 && !draw && !read && params->m_noBlit))
            snprintf(check->failure,sizeof(check->failure),"FBO draw=%d read=%d, size=%ux%u, noBlit=%d",draw,read,width,height,params->m_noBlit);
        check->valid=check->valid && width>=8 && height>=8 && !draw && !read && params->m_noBlit;
        // GL bottom-up coordinates: presentation flips the engine backbuffer.
        const unsigned char colors[4][4]={{0,0,255,255},{255,255,0,255},
                                         {255,0,0,255},{0,255,0,255}};
        const unsigned char background[4]={37,91,163,255};
        if (HasLoadedWorldMap() && !check->worldPixelsVerified) {
            // First-map evidence, before UIKit overlays: reject a clear/black
            // frame and require spatial variation across the native backbuffer.
            int covered=0, darkest=765, brightest=0;
            for (int y=0;y<4;++y) for (int x=0;x<4;++x) {
                unsigned char pixel[4]={};
                gGL->glReadPixels((2*x+1)*width/8,(2*y+1)*height/8,
                    1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                const int light=pixel[0]+pixel[1]+pixel[2];
                if (light>24 && memcmp(pixel,background,3)) ++covered;
                if (light<darkest) darkest=light;
                if (light>brightest) brightest=light;
            }
            check->worldPixelsVerified=covered>=8 && brightest-darkest>48;
        }
        if (check->scene.count) for (int i=0;i<check->scene.count;++i) {
            const SceneSample &sample=check->scene.points[i];
            unsigned char pixel[4]={};
            gGL->glReadPixels(int(sample.u*width),int((1-sample.v)*height),1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            check->valid=check->valid && MatchesSceneSample(pixel,sample);
        }
        else if (!check->gameSession && !HasLoadedWorldMap() && width>=8 && height>=8) for (int i=0;i<4;++i) {
            unsigned char pixel[4]={};
            gGL->glReadPixels((i%2 ? 3 : 1)*width/4,(i/2 ? 3 : 1)*height/4,
                1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            check->valid=check->valid && !memcmp(pixel,check->solid ? background : colors[i],4);
        }
        check->valid=check->valid && gGL->glGetError()==GL_NO_ERROR;
        ++check->swaps;
    }
    bool swapped=check->base->showPixels(check->base->userData,params);
    if (!swapped) snprintf(check->failure,sizeof(check->failure),"Native host swap failed");
    check->valid=check->valid && swapped;
    return swapped;
}

// Native-size probes inspect the actual draw FBO without allocating and
// downloading a full-screen D3D staging surface for each one-pixel sample.
void ReadNativePixel(IMatRenderContext *context, int x, int y, unsigned char pixel[4])
{
    context->Flush();
    GLint read=0,draw=0;
    gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,draw);
    gGL->glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
}

bool DrawMaterialFixture(IMaterialSystem *material, IMatRenderContext *context, bool alpha, char *detail, size_t capacity, int width=8, int height=8)
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
    unsigned char expected[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
    if (alpha) { expected[1][3]=192; expected[2][3]=128; expected[3][3]=64; }
    bool valid=true;
    // Keep exhaustive coverage for tiny startup fixtures; sample the centers
    // and outer corners at native resolution without a full-screen readback.
    int samples=width==8 && height==8 ? 64 : 8;
    for (int i=0;i<samples && valid;++i) {
        int x=samples==64 ? i%8 : (i<4 ? (i%2 ? 3 : 1)*width/4 : (i%2 ? width-1 : 0));
        int y=samples==64 ? i/8 : (i<4 ? (i/2 ? 3 : 1)*height/4 : ((i-4)/2 ? height-1 : 0));
        unsigned char pixel[4]={};
        if (samples==64) context->ReadPixels(x,y,1,1,pixel,IMAGE_FORMAT_RGBA8888);
        else ReadNativePixel(context,x,y,pixel);
        const unsigned char *color=expected[(y>=height/2)*2+(x>=width/2)];
        if (memcmp(pixel,color,4)) {
            snprintf(detail,capacity,"%s %dx%d pixel (%d,%d): %u,%u,%u,%u expected %u,%u,%u,%u",
                name,width,height,x,y,pixel[0],pixel[1],pixel[2],pixel[3],color[0],color[1],color[2],color[3]);
            valid=false;
        }
    }
    draw->DecrementReferenceCount();
    return valid;
}


bool HasCompiledUnlit()
{
    return g_pFullFileSystem->FileExists("shaders/fxc/vertexlit_and_unlit_generic_vs20.vcs","GAME")
        && g_pFullFileSystem->FileExists("shaders/fxc/vertexlit_and_unlit_generic_ps20b.vcs","GAME");
}

bool DrawUnlitMaterial(IMaterialSystem *material, IMatRenderContext *context,
    bool tint, int width, int height, char *detail, size_t capacity)
{
    IMaterial *draw=material->FindMaterial(tint ? "ios/unlit-tint" : "ios/unlit",TEXTURE_GROUP_OTHER,true);
    if (!draw || draw->IsErrorMaterial() || Q_stricmp(draw->GetShaderName(),"UnlitGeneric")) {
        snprintf(detail,capacity,"UnlitGeneric fixture lookup failed"); return false;
    }
    draw->IncrementReferenceCount();
    const MaterialMatrixMode_t modes[]={MATERIAL_MODEL,MATERIAL_VIEW,MATERIAL_PROJECTION};
    for (auto mode : modes) { context->MatrixMode(mode); context->PushMatrix(); context->LoadIdentity(); }
    context->FogMode(MATERIAL_FOG_NONE);
    context->SetToneMappingScaleLinear(Vector(1,1,1));
    context->ClearColor4ub(37,91,163,255); context->ClearBuffers(true,true,true);
    context->Bind(draw);
    IMesh *mesh=context->GetDynamicMesh(true);
    CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,1);
    const float vertices[3][4]={{-1,-1,0,1},{3,-1,2,1},{-1,3,0,-1}};
    for (const auto &vertex : vertices) {
        builder.Position3f(vertex[0],vertex[1],.5f);
        if (draw->GetVertexFormat() & VERTEX_NORMAL) builder.Normal3f(0,0,1);
        builder.TexCoord2f(0,vertex[2],vertex[3]); builder.AdvanceVertex();
    }
    builder.End(); mesh->Draw();
    const unsigned char colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
    bool valid=true;
    const int samples=width==8 && height==8 ? 64 : 8;
    for (int i=0;i<samples && valid;++i) {
        int x=samples==64 ? i%8 : (i<4 ? (i%2 ? 3 : 1)*width/4 : (i%2 ? width-1 : 0));
        int y=samples==64 ? i/8 : (i<4 ? (i/2 ? 3 : 1)*height/4 : ((i-4)/2 ? height-1 : 0));
        unsigned char pixel[4]={},expected[4];
        ReadNativePixel(context,x,y,pixel);
        memcpy(expected,colors[(y>=height/2)*2+(x>=width/2)],4);
        if (tint) expected[1]=0;
        valid=!memcmp(pixel,expected,4);
        if (!valid) snprintf(detail,capacity,"UnlitGeneric tint %d pixel %d,%d: %u,%u,%u,%u expected %u,%u,%u,%u",
            tint,x,y,pixel[0],pixel[1],pixel[2],pixel[3],expected[0],expected[1],expected[2],expected[3]);
    }
    for (auto mode : modes) { context->MatrixMode(mode); context->PopMatrix(); }
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

bool CheckToGLESMaterial(const GLMContextHost *host, const char *modules, char *detail, size_t capacity, bool retain)
{
    // Probe cycles have finished and unloaded their shader caches. Mount game
    // data before opening the retained modules so cached VCS headers and later
    // static-combo reads always resolve to the same source file.
    if (retain && !MountRequestedWorldData(detail,capacity)) return false;
    const bool gameSession=retain && getenv("SOURCE_IOS_GAME_ROOT");
    char materialPath[MAX_PATH],shaderPath[MAX_PATH];
    Q_snprintf(materialPath,sizeof(materialPath),"%s/libmaterialsystem.dylib",modules);
    Q_snprintf(shaderPath,sizeof(shaderPath),"%s/libshaderapidx9.dylib",modules);
    CSysModule *module=Sys_LoadModule(materialPath);
    if (!module) { snprintf(detail,capacity,"Material system module load failed"); return false; }
    CreateInterfaceFn factory=Sys_GetFactory(module);
    IMaterialSystem *material=factory ? static_cast<IMaterialSystem *>(factory(MATERIAL_SYSTEM_INTERFACE_VERSION,NULL)) : NULL;
    bool valid=false,connected=false,initialized=false;
    static MaterialPresentation presentation;
    static GLMContextHost hosted;
    presentation={host,0,true,false};
    presentation.gameSession=gameSession;
    hosted=*host;
    hosted.userData=&presentation;
    hosted.makeCurrent=PresentationBind;
    hosted.displayedSize=PresentationSize;
    hosted.showPixels=PresentationSwap;
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
                if (valid && !gameSession) {
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
                    if (HasCompiledUnlit()) for (int pass=0;pass<3 && valid;++pass)
                        valid=DrawUnlitMaterial(material,context,pass==1,8,8,detail,capacity);
                    context->EndRender();
                    context->Release();
                    material->EndFrame();
                    if (valid) material->SwapBuffers();
                    // Alternate a clear frame and a new material frame to catch
                    // stale presentation and rendering state after a swap.
                    for (int frame=1;frame<3 && valid;++frame) {
                        material->BeginFrame(0);
                        context=material->GetRenderContext();
                        context->BeginRender();
                        context->Viewport(0,0,8,8);
                        context->ClearColor4ub(37,91,163,255);
                        context->ClearBuffers(true,true,true);
                        presentation.solid=frame==1;
                        if (!presentation.solid) valid=DrawMaterialFixture(material,context,false,detail,capacity);
                        context->EndRender(); context->Release();
                        material->EndFrame();
                        if (valid) material->SwapBuffers();
                    }
                    if (valid && (!presentation.valid || presentation.swaps!=3)) {
                        valid=false;
                        snprintf(detail,capacity,"Material window presentation failed (%d/3 swaps, pixels/swap %s)",
                            presentation.swaps,presentation.valid ? "PASS" : "FAIL");
                    }

                }
                if (valid) valid=InitializeMapServices(modules,ApplicationFactory,detail,capacity);
                const char *startupMode=getenv("SOURCE_IOS_GAME_STARTUP");
                bool levelCycle=startupMode && (!strcmp(startupMode,"level-cycle") || !strcmp(startupMode,"player-cycle"));
                if (valid && retain && !levelCycle) valid=LoadRequestedWorldMap(detail,capacity);
                if (valid && retain) valid=InitializePortalGame(modules,ApplicationFactory,detail,capacity);
                if (valid && retain && levelCycle) valid=LoadRequestedWorldMap(detail,capacity);
                if (valid && retain) {
                    livePresentation=&presentation;
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

namespace {
bool CheckNativeDepthStencil(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, char *detail, size_t capacity)
{
    IMaterial *draw=material->FindMaterial("ios/depth",TEXTURE_GROUP_OTHER,true);
    if (!draw || draw->IsErrorMaterial()) {
        snprintf(detail,capacity,"Depth fixture missing"); return false;
    }
    draw->IncrementReferenceCount();
    context->ClearBuffers(true,true,true);
    context->SetStencilEnable(true);
    context->SetStencilTestMask(255); context->SetStencilWriteMask(255);
    context->SetStencilFailOperation(STENCILOPERATION_KEEP);
    context->SetStencilZFailOperation(STENCILOPERATION_KEEP);
    bool valid=true;
    // Initial stencil zero admits red. Then replace with 1, reject a farther
    // green by depth, reject nearer blue by stencil, and finally admit blue.
    const float depths[]={.25f,.25f,.75f,.125f,.125f};
    for (int pass=0;pass<5 && valid;++pass) {
        context->SetStencilReferenceValue(pass==0 || pass==3 ? 0 : 1);
        context->SetStencilCompareFunction(pass==1 || pass==2 ? STENCILCOMPARISONFUNCTION_ALWAYS : STENCILCOMPARISONFUNCTION_EQUAL);
        context->SetStencilPassOperation(pass==1 ? STENCILOPERATION_REPLACE : STENCILOPERATION_KEEP);
        context->Bind(draw);
        IMesh *mesh=context->GetDynamicMesh(true);
        CMeshBuilder builder; builder.Begin(mesh,MATERIAL_TRIANGLES,1);
        const float positions[3][2]={{-1,-1},{3,-1},{-1,3}};
        for (const auto &position : positions) {
            builder.Position3f(position[0],position[1],depths[pass]);
            builder.TexCoord2f(0,pass==2 ? .75f : .25f,pass>=3 ? .75f : .25f);
            builder.AdvanceVertex();
        }
        builder.End(); mesh->Draw();
        for (int corner=0;corner<4 && valid;++corner) {
            unsigned char pixel[4]={};
            ReadNativePixel(context,corner%2 ? width-1 : 0,corner/2 ? height-1 : 0,pixel);
            unsigned char expected[4]={255,0,0,255};
            if (pass==4) { expected[0]=0; expected[2]=255; }
            valid=!memcmp(pixel,expected,4);
            if (!valid) snprintf(detail,capacity,"Depth/stencil %dx%d pass %d corner %d: %u,%u,%u,%u",
                width,height,pass,corner,pixel[0],pixel[1],pixel[2],pixel[3]);
        }
    }
    context->SetStencilEnable(false);
    draw->DecrementReferenceCount();
    return valid;
}
int checkedWidth=0,checkedHeight=0,resizeChecks=0;
unsigned sceneFrame=0;
}

// These entry points run only on the UIKit render thread. The borrowed host
// and its native context must outlive the retained material system.
bool DrawToGLESLiveMaterial(char *detail, size_t capacity)
{
    PollInspectionInput();
    if (!liveMaterial) { snprintf(detail,capacity,"No live material system"); return false; }
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
    int swaps=livePresentation->swaps;
    liveMaterial->BeginFrame(0);
    IMatRenderContext *context=liveMaterial->GetRenderContext();
    context->BeginRender();
    context->Viewport(0,0,width,height);
    context->ClearColor4ub(37,91,163,255);
    context->ClearBuffers(true,true,true);
    bool valid=true;
    const bool worldLoaded=HasLoadedWorldMap();
    if (!worldLoaded && (checkedWidth!=width || checkedHeight!=height)) {
        valid=CheckNativeDepthStencil(liveMaterial,context,width,height,detail,capacity);
        if (valid) { checkedWidth=width; checkedHeight=height; ++resizeChecks; }
        context->ClearBuffers(true,true,true);
    }
    livePresentation->scene.count=0;
    if (worldLoaded) {
        context->MatrixMode(MATERIAL_MODEL);
        context->LoadIdentity();
        context->FogMode(MATERIAL_FOG_NONE);
        context->SetToneMappingScaleLinear(Vector(1,1,1));
    }
    bool scene=!worldLoaded && HasCompiledUnlit() && width>=64 && height>=64;
    if (worldLoaded) valid=DrawLoadedWorldMap(liveMaterial,width,height,detail,capacity);
    else if (valid && scene) valid=DrawPerspectiveScene(liveMaterial,context,width,height,sceneFrame++,livePresentation->scene,detail,capacity);
    else if (valid) valid=HasCompiledUnlit()
        ? DrawUnlitMaterial(liveMaterial,context,false,width,height,detail,capacity)
        : DrawMaterialFixture(liveMaterial,context,false,detail,capacity,width,height);
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
    if (valid && (!livePresentation->valid || livePresentation->swaps!=swaps+1)) {
        snprintf(detail,capacity,"Live material window pixels/swap failed: %s (%d/%d swaps)",livePresentation->failure,livePresentation->swaps,swaps+1); return false;
    }
    const char *sceneModes[]={"3D UnlitGeneric","3D ambient","3D directional","3D point","3D spot","3D two lights","3D slot 1 only","3D swapped lights","UnlitGeneric alpha blend","Lit/unlit fog + alpha","BSP geometry + lightmaps","Perspective BSP + lightmaps"};
    if (valid && error==GL_NO_ERROR && worldLoaded) {
        return true;
    }
    if (valid && error==GL_NO_ERROR) snprintf(detail,capacity,"%s %ux%u + depth/stencil: PASS (%d sizes)",scene ? sceneModes[SceneLightingMode(sceneFrame-1)] : "Native",targetWidth,targetHeight,resizeChecks);
    return valid && error==GL_NO_ERROR;
}
void StopToGLESLiveMaterial()
{
    if (!liveMaterial) return;
    ResetLightmapScene();
    ShutdownMapServices();
    liveMaterial->ModShutdown(); liveMaterial->Shutdown(); liveMaterial->Disconnect();
    checkedWidth=checkedHeight=resizeChecks=0; sceneFrame=0;
    liveMaterial=NULL; livePresentation=NULL; applicationMaterial=NULL; applicationHost=NULL;
    Sys_UnloadModule(liveModule); liveModule=NULL;
}

extern "C" int IsSourceWorldMapRendered()
{
    return HasLoadedWorldMap() && livePresentation && livePresentation->valid &&
        livePresentation->worldPixelsVerified;
}

// VGUI integration samples the active native backbuffer without constructing a
// full-screen D3D staging texture. Coordinates follow VGUI's top-left origin.
extern "C" bool SourceIOSReadSurfacePixels(IMatRenderContext *context,int x,int y,
    int width,int height,unsigned char *pixels)
{
    context->Flush();
    int vx=0,vy=0,vw=0,vh=0; context->GetViewport(vx,vy,vw,vh);
    GLint read=0,draw=0;
    gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,draw);
    // The D3D-to-GL renderer reverses projection Y for offscreen targets.
    // Window pixels need the usual GL bottom-left conversion.
    int readY=context->GetRenderTarget() ? vy+y : vy+vh-y-height;
    gGL->glReadPixels(vx+x,readY,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    gGL->glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
    return gGL->glGetError()==GL_NO_ERROR;
}
