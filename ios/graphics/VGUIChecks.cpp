#include "MatSystemSurface.h"
#include "vgui_controls/Panel.h"
#include "vgui_controls/Controls.h"
#include "vgui/IPanel.h"
#include "vgui/IVGui.h"
#include "vgui/IScheme.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include "bitmap/imageformat.h"
#include "tier3/tier3.h"
#include "tier0/dbg.h"

extern CMatSystemSurface g_MatSystemSurface;
extern "C" bool SourceIOSReadSurfacePixels(IMatRenderContext *,int,int,int,int,unsigned char *);
namespace {
class InputFixturePanel : public vgui::Panel {
public:
    vgui::HFont font;
    int paints=0;
    InputFixturePanel(vgui::Panel *parent,vgui::HFont handle)
        : Panel(parent,"IOSInputFixture"),font(handle) {
        SetBounds(16,12,128,64); SetVisible(true);
        SetPaintBackgroundEnabled(false); SetPaintBorderEnabled(false);
    }
    void Paint() override {
        ++paints;
        g_MatSystemSurface.DrawSetColor(0,255,255,255);
        g_MatSystemSurface.DrawFilledRect(-8,8,72,28);
        g_MatSystemSurface.DrawSetTextFont(font);
        g_MatSystemSurface.DrawSetTextColor(255,255,255,255);
        g_MatSystemSurface.DrawSetTextPos(8,34);
        g_MatSystemSurface.DrawPrintText(L"iOS Žluť",8);
    }
};
}
extern "C" bool SourceIOSCheckVGUI(char *detail,size_t capacity)
{
    auto &surface=g_MatSystemSurface;
    // Keep this check independent of the 8x8 smoke-test backbuffer and the
    // simulator's native window size. Exercise a real color/depth render target.
    CTextureReference target;
    g_pMaterialSystem->BeginRenderTargetAllocation();
    target.Init(g_pMaterialSystem->CreateNamedRenderTargetTextureEx2("_rt_IOSVGUIFixture",
        256,128,RT_SIZE_NO_CHANGE,IMAGE_FORMAT_RGBA8888,MATERIAL_RT_DEPTH_SEPARATE,
        TEXTUREFLAGS_CLAMPS|TEXTUREFLAGS_CLAMPT,0));
    g_pMaterialSystem->EndRenderTargetAllocation();
    if (!target || target->IsError()) { snprintf(detail,capacity,"iOS VGUI: render target FAIL"); return false; }
    vgui::HFont font=surface.CreateFont();
    bool valid=surface.SetFontGlyphSet(font,"Helvetica",18,400,0,0,vgui::ISurface::FONTFLAG_ANTIALIAS);
    int textWidth=0,textHeight=0;
    if(valid) surface.GetTextSize(font,L"iOS Žluť",textWidth,textHeight);
    valid=valid && textWidth>20 && textHeight>8;
    if(!valid) { snprintf(detail,capacity,"iOS VGUI: system font creation/metrics FAIL"); return false; }

    g_pMaterialSystem->BeginFrame(0);
    CMatRenderContextPtr context(g_pMaterialSystem);
    int width=target->GetActualWidth(),height=target->GetActualHeight();
    context->PushRenderTargetAndViewport(target,0,0,width,height);
    context->ClearColor4ub(0,0,0,255); context->ClearBuffers(true,true,true);
    {
        vgui::Panel root(NULL,"IOSVGUIFixtureRoot");
        root.SetParent(surface.GetEmbeddedPanel());
        root.SetBounds(0,0,160,80); root.SetVisible(true);
        root.SetPaintBackgroundEnabled(false); root.SetPaintBorderEnabled(false);
        InputFixturePanel child(&root,font);
        valid=vgui::ipanel()->GetParent(child.GetVPanel())==root.GetVPanel() && valid;
        surface.SolveTraverse(surface.GetEmbeddedPanel(),false);
        surface.PaintTraverse(root.GetVPanel());
        valid=child.paints==1 && valid;
        unsigned char pixel[4]={};
        valid=SourceIOSReadSurfacePixels(context,30,24,1,1,pixel)&&valid;
        valid=pixel[0]<8 && pixel[1]>240 && pixel[2]>240 && valid;
        valid=SourceIOSReadSurfacePixels(context,12,24,1,1,pixel)&&valid;
        valid=pixel[0]<8 && pixel[1]<8 && pixel[2]<8 && valid;
        unsigned char glyphs[128*24*4]={};
        valid=SourceIOSReadSurfacePixels(context,16,44,128,24,glyphs)&&valid;
        int white=0;
        for(int i=0;i<128*24;++i)
            if(glyphs[i*4]>100 && glyphs[i*4+1]>100 && glyphs[i*4+2]>100) ++white;
        valid=white>20 && valid;
        snprintf(detail,capacity,"iOS VGUI panels + clipping + cyan fill + CoreText/FreeType glyph pixels: %s; %d glyph pixels",
                 valid?"PASS":"FAIL",white);

    }
    context->PopRenderTargetAndViewport();
    context.SafeRelease(); g_pMaterialSystem->EndFrame();
    Msg("%s\n",detail); return valid;
}
