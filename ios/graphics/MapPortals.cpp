#include "render_pch.h"
#include "MapPortals.h"
#include "common.h"
#include "cmodel_engine.h"
#include "gl_rmain.h"
#include "host.h"
#include "view_shared.h"
#include "tier3/tier3.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"

namespace {
struct Portal {
    Vector origin,forward,right,up;
    QAngle angles;
    char name[128]={};
    bool second=false,ready=false;
    matrix3x4_t localToWorld;
    VMatrix toLinked;
    CTextureReference target;
    CMaterialReference surface,border;
};
Portal portals[2];
bool enabled=false;
void DrawMesh(Portal &p,IMaterial *material,float offset) {
    CMatRenderContextPtr context(materials);
    context->Bind(material);
    context->MatrixMode(MATERIAL_MODEL); context->PushMatrix(); context->LoadIdentity();
    IMesh *mesh=context->GetDynamicMesh(false); CMeshBuilder builder;
    builder.Begin(mesh,MATERIAL_TRIANGLE_STRIP,2);
    const float x[4]={1,1,-1,-1},y[4]={-1,1,-1,1};
    for (int i=0;i<4;++i) {
        Vector pos=p.origin+p.forward*offset+p.right*(32*x[i])+p.up*(54*y[i]);
        float tangent[4]={-p.right.x,-p.right.y,-p.right.z,1};
        builder.Position3fv(pos.Base()); builder.Normal3fv(p.forward.Base());
        builder.TangentS3fv((-p.right).Base()); builder.TangentT3fv(p.up.Base());
        builder.UserData(tangent); builder.Color4ub(255,255,255,255);
        builder.TexCoord2f(0,(1-x[i])*.5f,(1-y[i])*.5f);
        builder.TexCoord2f(1,(1-x[i])*.5f,(1-y[i])*.5f); builder.AdvanceVertex();
    }
    builder.End(); mesh->Draw(); context->PopMatrix();
}
void Draw(void *data) {
    Portal &p=*static_cast<Portal *>(data);
    if (!p.ready) return;
    DrawMesh(p,p.surface,.1f);
    bool found=false;
    IMaterialVar *time=p.border->FindVar("$time",&found,false);
    if (found) time->SetFloatValue(Plat_FloatTime());
    DrawMesh(p,p.border,.15f);
}
}
void SourceIOSShutdownPortalInspection() {
    for (int i=0;i<2;++i) {
        portals[i].surface.Shutdown(); portals[i].border.Shutdown(); portals[i].target.Shutdown();
        portals[i].ready=false;
    }
    enabled=false;
}
bool SourceIOSInitializePortalInspection() {
    SourceIOSShutdownPortalInspection();
    const char *pair=getenv("SOURCE_IOS_PORTAL_PREVIEW");
    if (!pair || !*pair) return true;
    char requested[2][128];
    if (sscanf(pair,"%127s %127s",requested[0],requested[1])!=2 || !Q_strcmp(requested[0],requested[1])) return false;
    bool found[2]={false,false};
    const char *data=CM_EntityString(); char token[1024],key[1024];
    while (data) {
        data=COM_ParseFile(data,token,sizeof(token)); if (!data || Q_strcmp(token,"{")) break;
        char classname[128]={},name[128]={}; Vector origin(0,0,0); QAngle angles(0,0,0); bool second=false;
        while (data) {
            data=COM_ParseFile(data,key,sizeof(key)); if (!data || !Q_strcmp(key,"}")) break;
            data=COM_ParseFile(data,token,sizeof(token)); if (!data) break;
            if (!Q_strcmp(key,"classname")) Q_strncpy(classname,token,sizeof(classname));
            else if (!Q_strcmp(key,"targetname")) Q_strncpy(name,token,sizeof(name));
            else if (!Q_strcmp(key,"origin")) sscanf(token,"%f %f %f",&origin.x,&origin.y,&origin.z);
            else if (!Q_strcmp(key,"angles")) sscanf(token,"%f %f %f",&angles.x,&angles.y,&angles.z);
            else if (!Q_strcmp(key,"PortalTwo")) second=atoi(token)!=0;
        }
        if (Q_strcmp(classname,"prop_portal")) continue;
        for (int i=0;i<2;++i) if (!Q_strcmp(name,requested[i])) {
            Portal &p=portals[i]; p.origin=origin; p.angles=angles; p.second=second;
            Q_strncpy(p.name,name,sizeof(p.name)); AngleVectors(angles,&p.forward,&p.right,&p.up);
            AngleMatrix(angles,origin,p.localToWorld); found[i]=true;
        }
    }
    if (!found[0] || !found[1] || portals[0].second==portals[1].second) return false;
    materials->BeginRenderTargetAllocation();
    for (int i=0;i<2;++i) {
        Portal &p=portals[i]; const char *target=p.second?"_rt_Portal2":"_rt_Portal1";
        p.target.Init(materials->CreateNamedRenderTargetTextureEx2(target,1,1,RT_SIZE_FULL_FRAME_BUFFER,
            materials->GetBackBufferFormat(),MATERIAL_RT_DEPTH_SHARED,TEXTUREFLAGS_CLAMPS|TEXTUREFLAGS_CLAMPT,
            CREATERENDERTARGETFLAGS_HDR));
    }
    materials->EndRenderTargetAllocation();
    for (int i=0;i<2;++i) {
        Portal &p=portals[i];
        if (!p.target || p.target->IsError()) return false;
        p.surface.Init(materials->FindMaterial(p.second?"models/portals/portal_2_dynamicmesh":"models/portals/portal_1_dynamicmesh",TEXTURE_GROUP_CLIENT_EFFECTS));
        p.border.Init(materials->FindMaterial(p.second?"models/portals/portalstaticoverlay_2":"models/portals/portalstaticoverlay_1",TEXTURE_GROUP_CLIENT_EFFECTS));
        if (!p.surface || !p.border || p.surface->IsErrorMaterial() || p.border->IsErrorMaterial()) return false;
        p.surface->FindVar("$usealternateviewmatrix",NULL,false)->SetIntValue(0);
        p.border->FindVar("$PortalOpenAmount",NULL,false)->SetFloatValue(1);
        p.border->FindVar("$PortalStatic",NULL,false)->SetFloatValue(0);
        VMatrix inverse,remote(portals[1-i].localToWorld),rotation;
        MatrixInverseTR(p.localToWorld,inverse); rotation.Identity(); rotation[0][0]=rotation[1][1]=-1;
        p.toLinked=remote*rotation*inverse;
    }
    enabled=true; Msg("iOS portal render preview: %s <-> %s (one level, BSP state unchanged)\n",requested[0],requested[1]);
    return true;
}
int SourceIOSRenderPortalViews(const CViewSetup &view,bool (*drawScene)(const CViewSetup &,ITexture *,const Vector *,bool,char *,size_t)) {
    if (!enabled) return 0;
    int rendered=0;
    CMatRenderContextPtr context(materials);
    for (int i=0;i<2;++i) {
        Portal &p=portals[i],&remote=portals[1-i]; p.ready=false;
        if (DotProduct(view.origin-p.origin,p.forward)<=0) continue;
        Vector extent=Vector(2,2,2)+Vector(fabs(p.right.x)*32+fabs(p.up.x)*54,
            fabs(p.right.y)*32+fabs(p.up.y)*54,fabs(p.right.z)*32+fabs(p.up.z)*54);
        // The main frustum isn't pushed yet. PVS avoids most offscreen work;
        // aperture frustum clipping will be added with recursive views.
        const byte *pvs=CM_ClusterPVS(CM_LeafCluster(CM_PointLeafnum(view.origin)));
        if (!CM_BoxVisible(p.origin-extent,p.origin+extent,pvs,CM_ClusterPVSSize())) continue;
        CViewSetup portalView=view;
        portalView.origin=p.toLinked*view.origin;
        portalView.angles=TransformAnglesToWorldSpace(view.angles,p.toLinked.As3x4());
        portalView.width=portalView.m_nUnscaledWidth=p.target->GetActualWidth();
        portalView.height=portalView.m_nUnscaledHeight=p.target->GetActualHeight();
        portalView.m_flAspectRatio=float(view.width)/view.height;
        float clip[4]={remote.forward.x,remote.forward.y,remote.forward.z,
            DotProduct(remote.forward,remote.origin-remote.forward*.5f)};
        context->PushCustomClipPlane(clip);
        Vector visibilityOrigin=remote.origin+remote.forward*2;
        char detail[1024]; p.ready=drawScene(portalView,p.target,&visibilityOrigin,false,detail,sizeof(detail));
        context->PopCustomClipPlane();
        if (!p.ready) return -1;
        ++rendered;
    }
    return rendered;
}
int SourceIOSCollectPortals(const WorldListInfo_t &world,const Vector &origin,const Vector &forward,
    CUtlVector<SourceIOSTranslucentDraw> &draws) {
    if (!enabled) return 0;
    int submitted=0;
    for (int i=0;i<2;++i) {
        Portal &p=portals[i]; if (!p.ready || DotProduct(origin-p.origin,p.forward)<=0) continue;
        Vector mins=p.origin-Vector(55,55,55),maxs=p.origin+Vector(55,55,55);
        if (R_CullBox(mins,maxs)) continue;
        int leaf=CM_PointLeafnum(p.origin+p.forward),at=-1;
        for (int v=0;v<world.m_LeafCount;++v) if (world.m_pLeafList[v]==leaf) { at=v; break; }
        if (at<0) continue;
        SourceIOSTranslucentDraw draw={&p,at,DotProduct(p.origin-origin,forward),Draw};
        int insert=0; while (insert<draws.Count() && (draws[insert].leaf>at ||
            (draws[insert].leaf==at && draws[insert].depth>=draw.depth))) ++insert;
        draws.InsertBefore(insert,draw); ++submitted;
    }
    return submitted;
}
