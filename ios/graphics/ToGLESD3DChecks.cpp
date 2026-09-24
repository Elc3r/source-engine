#include "togles/rendermechanism.h"
#include "ToGLESFixtures.h"
#include "vstdlib/cvar.h"

extern ConVar gl_blitmode;


namespace {
const GLMContextHost *factoryHost=NULL;
void *AppFactory(const char *name, int *status)
{
    if (!strcmp(name,TOGLES_CONTEXT_HOST_INTERFACE_VERSION)) {
        if (status) *status=factoryHost ? IFACE_OK : IFACE_FAILED;
        return const_cast<GLMContextHost *>(factoryHost);
    }
    return VStdLib_GetICVarFactory()(name,status);
}
struct PresentCheck {
    const GLMContextHost *base;
    float expected[4*4];
    int swaps;
    bool valid;
    bool failSync, failSwap;
};
bool Bind(void *data, void *context)
{
    PresentCheck *check=static_cast<PresentCheck *>(data);
    return check->base->makeCurrent(check->base->userData,context);
}
void Size(void *data, uint &width, uint &height)
{
    PresentCheck *check=static_cast<PresentCheck *>(data);
    check->base->displayedSize(check->base->userData,width,height);
}
bool Swap(void *data, CShowPixelsParams *params)
{
    PresentCheck *check=static_cast<PresentCheck *>(data);
    if (params->m_onlySyncView && check->failSync) return false;
    if (!params->m_onlySyncView && check->failSwap) return false;
    if (!params->m_onlySyncView) {
        uint width=0,height=0;
        Size(data,width,height);
        unsigned char pixels[4*4]={};
        GLint draw=0,read=0;
        gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        gGL->glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        gGL->glReadPixels(width-1,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels+4);
        gGL->glReadPixels(0,height-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels+8);
        gGL->glReadPixels(width-1,height-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels+12);
        check->valid=check->valid && params->m_noBlit && !draw && !read && gGL->glGetError()==GL_NO_ERROR;
        for (int i=0;i<4*4;++i) check->valid=check->valid && fabsf(pixels[i]-check->expected[i])<=1;
        ++check->swaps;
    }
    return check->base->showPixels(check->base->userData,params);
}
}

static bool DrawDevice(IDirect3DDevice9 *device, PresentCheck &presentation, char *detail, size_t capacity)
{
    IDirect3DVertexShader9 *vertex=NULL;
    IDirect3DPixelShader9 *pixel=NULL;
    IDirect3DVertexDeclaration9 *declaration=NULL;
    IDirect3DVertexBuffer9 *vertices=NULL;
    IDirect3DIndexBuffer9 *indices=NULL;
    IDirect3DTexture9 *texture=NULL;
    IDirect3DSurface9 *textureSurface=NULL;
    char label[]="ios-d3d-fixture";
    const D3DVERTEXELEMENT9 elements[]={
        {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        D3DDECL_END()};
    // Constant UV isolates device state from the D3D9 half-pixel convention.
    const float vertexData[]={-1,-1,0.5,1, .125,.125,0,1, 3,-1,0.5,1, .125,.125,0,1,
        -1,3,0.5,1, .125,.125,0,1};
    const unsigned short indexData[]={0,1,2};
    bool valid=false;
    HRESULT result=S_OK;
#define D3D_CHECK(call) if ((result=(call))!=S_OK) { snprintf(detail,capacity,#call ": HRESULT 0x%x",unsigned(result)); break; }
    do {
        D3D_CHECK(device->CreateVertexShader(ToGLESFixtures::Tokens(true),&vertex,label,label));
        D3D_CHECK(device->CreatePixelShader(ToGLESFixtures::Tokens(false,true),&pixel,label,label));
        D3D_CHECK(device->CreateVertexDeclaration(elements,&declaration));
        D3D_CHECK(device->CreateVertexBuffer(sizeof(vertexData),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vertices,NULL));
        D3D_CHECK(device->CreateIndexBuffer(sizeof(indexData),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&indices,NULL));
        void *data=NULL;
        D3D_CHECK(vertices->Lock(0,sizeof(vertexData),&data,D3DLOCK_DISCARD));
        if (!data) { snprintf(detail,capacity,"D3D9 vertex lock returned NULL"); break; }
        memcpy(data,vertexData,sizeof(vertexData));
        D3D_CHECK(vertices->Unlock());
        D3D_CHECK(indices->Lock(0,sizeof(indexData),&data,D3DLOCK_DISCARD));
        if (!data) { snprintf(detail,capacity,"D3D9 index lock returned NULL"); break; }
        memcpy(data,indexData,sizeof(indexData));
        D3D_CHECK(indices->Unlock());
        D3D_CHECK(device->CreateTexture(4,4,1,0,D3DFMT_DXT3,D3DPOOL_MANAGED,&texture,NULL,label));
        D3DLOCKED_RECT lock={};
        D3D_CHECK(texture->GetSurfaceLevel(0,&textureSurface));
        RECT region={0,0,4,4};
        D3D_CHECK(textureSurface->LockRect(&lock,&region,0));
        if (!lock.pBits) { snprintf(detail,capacity,"D3D9 texture lock returned NULL"); break; }
        const unsigned char block[]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0,248,224,7,0,0,0,0};
        memcpy(lock.pBits,block,sizeof(block));
        D3D_CHECK(textureSurface->UnlockRect());
        D3D_CHECK(device->SetVertexDeclaration(declaration));
        D3D_CHECK(device->SetVertexShader(vertex));
        D3D_CHECK(device->SetPixelShader(pixel));
        D3D_CHECK(device->SetStreamSource(0,vertices,0,8*sizeof(float)));
        D3D_CHECK(device->SetIndices(indices));
        D3D_CHECK(device->SetTexture(0,texture));
        D3D_CHECK(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        D3D_CHECK(device->SetRenderState(D3DRS_ZENABLE,FALSE));
        D3D_CHECK(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        D3D_CHECK(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));
        D3D_CHECK(device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        D3D_CHECK(device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        D3DVIEWPORT9 viewport={0,0,8,8,0,1};
        D3D_CHECK(device->SetViewport(&viewport));
        const float fixup[]={0,0,2,0};
        D3D_CHECK(device->SetVertexShaderConstantF(0,fixup,1));
        GLenum error=gGL->glGetError();
        if (error) { snprintf(detail,capacity,"D3D9 resource setup: GL 0x%x",error); break; }
        valid=true;
        for (int pass=0;pass<8 && valid;++pass) {
            const float tint[]={(pass==1 || pass>=3)?0.5f:1.0f,1,1,1};
            valid=device->SetPixelShaderConstantF(0,tint,1)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ZENABLE,pass>=5)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_SRGBWRITEENABLE,pass==3)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ALPHATESTENABLE,pass==2)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ALPHAREF,192)==S_OK;
            valid=valid && device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff0000ff,pass==5?0.25f:1.0f,0)==S_OK;
            RECT scissor={0,0,4,4};
            valid=valid && device->SetScissorRect(&scissor)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_SCISSORTESTENABLE,pass==7)==S_OK;
            valid=valid && device->BeginScene()==S_OK;
            valid=valid && device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1)==S_OK;
            valid=valid && device->EndScene()==S_OK;
            unsigned char pixels[8*8*4]={};
            gGL->glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            error=gGL->glGetError();
            valid=valid && error==GL_NO_ERROR;
            const bool rejected=pass==2 || pass==5;
            const float encoded=gGL->m_bHave_GL_EXT_sRGB_write_control
                ? 1.055f*powf(0.5f,1.0f/2.4f)-0.055f : powf(0.5f,1.0f/2.2f);
            const float expected[]={rejected?0.0f:pass==3?255*encoded:255*tint[0],0,
                rejected?255.0f:0.0f,rejected?255.0f:170.0f};
            const float background[]={0,0,255,255};
            for (int i=0;i<8*8;++i) {
                const float *color=pass==7 && (i%8>=4 || i/8>=4) ? background : expected;
                for (int c=0;c<4;++c) valid=valid && fabsf(pixels[i*4+c]-color[c])<=1.0f;
            }
            // Present flips the D3D render target vertically: its lower-left
            // scissor region appears in the upper-left corner of the drawable.
            for (int corner=0;corner<4;++corner) {
                const float *color=pass==7 && corner!=2 ? background : expected;
                memcpy(presentation.expected+corner*4,color,4*sizeof(float));
            }
            const int oldSwaps=presentation.swaps;
            if (valid && pass==0) {
                presentation.failSync=true;
                valid=device->Present(NULL,NULL,NULL,NULL)==D3DERR_DEVICELOST;
                presentation.failSync=false;
                presentation.failSwap=true;
                valid=valid && device->Present(NULL,NULL,NULL,NULL)==D3DERR_DEVICELOST;
                presentation.failSwap=false;
            }
            if (valid) valid=device->Present(NULL,NULL,NULL,NULL)==S_OK;
            GLint drawFBO=0,readFBO=0,box[4]={};
            gGL->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFBO);
            gGL->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFBO);
            gGL->glGetIntegerv(GL_SCISSOR_BOX,box);
            valid=valid && drawFBO && readFBO==drawFBO;
            valid=valid && (gGL->glIsEnabled(GL_SCISSOR_TEST)==(pass==7));
            valid=valid && box[0]==0 && box[1]==0 && box[2]==4 && box[3]==4;
            valid=valid && presentation.valid && presentation.swaps==oldSwaps+1 && gGL->glGetError()==GL_NO_ERROR;
            snprintf(detail,capacity,"D3D9 draw/present %d: %s (GL 0x%x, pixel %u/%u/%u/%u)",pass,
                valid?"PASS":"FAIL",error,pixels[0],pixels[1],pixels[2],pixels[3]);
        }
    } while (false);
#undef D3D_CHECK
    device->SetTexture(0,NULL); device->SetIndices(NULL); device->SetStreamSource(0,NULL,0,0);
    device->SetVertexShader(NULL); device->SetPixelShader(NULL); device->SetVertexDeclaration(NULL);
    if (textureSurface) textureSurface->Release();
    if (texture) texture->Release();
    if (indices) indices->Release();
    if (vertices) vertices->Release();
    if (declaration) declaration->Release();
    if (pixel) pixel->Release();
    if (vertex) vertex->Release();
    return valid;
}

bool CheckToGLESD3DDevice(const GLMContextHost *host, char *detail, size_t capacity)
{
    IDirect3DDevice9Params params={};
    params.m_deviceType=D3DDEVTYPE_HAL;
    D3DPRESENT_PARAMETERS &present=params.m_presentationParameters;
    present.BackBufferWidth=8; present.BackBufferHeight=8;
    present.BackBufferFormat=D3DFMT_A8R8G8B8; present.BackBufferCount=1;
    present.Windowed=TRUE; present.SwapEffect=D3DSWAPEFFECT_DISCARD;
    present.EnableAutoDepthStencil=TRUE; present.AutoDepthStencilFormat=D3DFMT_D24S8;
    present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    // A rejected host must leave a safely releasable device and allow retry.
    GLMContextHost invalidHost=*host;
    invalidHost.makeCurrent=NULL;
    IDirect3DDevice9 *rejected=new IDirect3DDevice9;
    HRESULT result=rejected->Create(&params,&invalidHost);
    rejected->Release();
    bool valid=result!=S_OK && gGL->glGetError()==GL_NO_ERROR;
    snprintf(detail,capacity,"D3D9 invalid host was not rejected cleanly");
    PresentCheck presentation={host,{},0,true,false,false};
    uint width=0,height=0;
    host->displayedSize(host->userData,width,height);
    GLMContextHost hosted=*host;
    hosted.userData=&presentation; hosted.makeCurrent=Bind; hosted.displayedSize=Size; hosted.showPixels=Swap;
    factoryHost=NULL;
    IDirect3D9 *missing=ToGLESCreateD3D9(D3D_SDK_VERSION,AppFactory);
    valid=valid && !missing;
    if (missing) missing->Release();
    factoryHost=&invalidHost;
    IDirect3D9 *invalid=ToGLESCreateD3D9(D3D_SDK_VERSION,AppFactory);
    valid=valid && !invalid;
    if (invalid) invalid->Release();
    factoryHost=&hosted;
    IDirect3D9 *adapter=ToGLESCreateD3D9(D3D_SDK_VERSION,AppFactory);
    factoryHost=NULL;
    if (!adapter) { snprintf(detail,capacity,"Shader API hosted D3D9 factory failed"); return false; }
    D3DCAPS9 caps={};
    D3DADAPTER_IDENTIFIER9 identifier={};
    D3DDISPLAYMODE mode={};
    DWORD quality=99;
    static_assert(sizeof(HRESULT)==4,"D3D HRESULT must be signed 32-bit");
    valid=valid && FAILED(D3DERR_NOTAVAILABLE) && FAILED(D3DERR_DEVICELOST)
        && FAILED(E_FAIL) && SUCCEEDED(S_OK) && SUCCEEDED(S_FALSE);
    valid=valid && adapter->GetAdapterCount()==1;
    valid=valid && adapter->GetDeviceCaps(0,D3DDEVTYPE_HAL,&caps)==S_OK && caps.MaxTextureWidth>=8;
    valid=valid && adapter->GetDeviceCaps(1,D3DDEVTYPE_HAL,&caps)==D3DERR_INVALIDCALL;
    valid=valid && adapter->GetAdapterIdentifier(0,0,&identifier)==S_OK && identifier.Description[0];
    valid=valid && adapter->GetAdapterModeCount(0,D3DFMT_X8R8G8B8)==1;
    valid=valid && adapter->EnumAdapterModes(0,D3DFMT_X8R8G8B8,0,&mode)==S_OK && mode.Width==width && mode.Height==height;
    valid=valid && adapter->EnumAdapterModes(0,D3DFMT_X8R8G8B8,1,&mode)==D3DERR_INVALIDCALL;
    valid=valid && adapter->CheckDeviceType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_A8R8G8B8,TRUE)==S_OK;
    valid=valid && adapter->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,0,D3DRTYPE_TEXTURE,D3DFMT_DXT3)==S_OK;
    valid=valid && adapter->CheckDepthStencilMatch(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_A8R8G8B8,D3DFMT_D24S8)==S_OK;
    valid=valid && adapter->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_NONE,&quality)==S_OK && quality==1;
    valid=valid && adapter->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_4_SAMPLES,&quality)==D3DERR_NOTAVAILABLE && quality==0;
    IDirect3DDevice9 *invalidDevice=NULL;
    valid=valid && adapter->CreateDevice(1,D3DDEVTYPE_HAL,NULL,0,&present,&invalidDevice)==D3DERR_INVALIDCALL && !invalidDevice;
    if (invalidDevice) invalidDevice->Release();
    snprintf(detail,capacity,"Hosted D3D9 adapter checks failed");
    const int oldBlitMode=gl_blitmode.GetInt();
    for (int cycle=0;cycle<2 && valid;++cycle) {
        gl_blitmode.SetValue(cycle);
        IDirect3DDevice9 *device=NULL;
        result=adapter->CreateDevice(0,D3DDEVTYPE_HAL,NULL,0,&present,&device);
        GLenum error=gGL->glGetError();
        valid=result==S_OK && error==GL_NO_ERROR;
        snprintf(detail,capacity,"D3D9 Create %ux%u cycle %d: HRESULT 0x%x, GL 0x%x",width,height,cycle,unsigned(result),error);
        if (valid) {
            D3DCAPS9 deviceCaps={};
            valid=device->GetDeviceCaps(&deviceCaps)==S_OK && !memcmp(&caps,&deviceCaps,sizeof(caps));
            if (valid) valid=DrawDevice(device,presentation,detail,capacity);
        }
        if (device) device->Release();
        error=gGL->glGetError();
        if (error!=GL_NO_ERROR) { valid=false; snprintf(detail,capacity,"D3D9 teardown: GL 0x%x",error); }
    }
    gl_blitmode.SetValue(oldBlitMode);
    adapter->Release();
    return valid;
}
