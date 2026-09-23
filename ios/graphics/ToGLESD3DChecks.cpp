#include "togles/rendermechanism.h"
#include "ToGLESFixtures.h"

static bool DrawDevice(IDirect3DDevice9 *device, char *detail, size_t capacity)
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
        for (int pass=0;pass<7 && valid;++pass) {
            const float tint[]={(pass==1 || pass>=3)?0.5f:1.0f,1,1,1};
            valid=device->SetPixelShaderConstantF(0,tint,1)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ZENABLE,pass>=5)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_SRGBWRITEENABLE,pass==3)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ALPHATESTENABLE,pass==2)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER)==S_OK;
            valid=valid && device->SetRenderState(D3DRS_ALPHAREF,192)==S_OK;
            valid=valid && device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff0000ff,pass==5?0.25f:1.0f,0)==S_OK;
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
            for (int i=0;i<8*8*4;++i) valid=valid && fabsf(pixels[i]-expected[i%4])<=1.0f;
            snprintf(detail,capacity,"D3D9 draw %d: %s (GL 0x%x, pixel %u/%u/%u/%u)",pass,
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
    for (int cycle=0;cycle<2 && valid;++cycle) {
        IDirect3DDevice9 *device=new IDirect3DDevice9;
        result=device->Create(&params,host);
        GLenum error=gGL->glGetError();
        valid=result==S_OK && error==GL_NO_ERROR;
        snprintf(detail,capacity,"D3D9 Create: HRESULT 0x%x, GL 0x%x",unsigned(result),error);
        if (valid) valid=DrawDevice(device,detail,capacity);
        device->Release();
        error=gGL->glGetError();
        if (error!=GL_NO_ERROR) { valid=false; snprintf(detail,capacity,"D3D9 teardown: GL 0x%x",error); }
    }
    return valid;
}
