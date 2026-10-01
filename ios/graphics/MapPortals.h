#pragma once
#include "MapEntityInspection.h"
class ITexture;
struct CViewSetup;
bool SourceIOSInitializePortalInspection();
void SourceIOSShutdownPortalInspection();
int SourceIOSRenderPortalViews(const CViewSetup &view,bool (*drawScene)(const CViewSetup &,ITexture *,const Vector *,bool,char *,size_t));
int SourceIOSCollectPortals(const WorldListInfo_t &world,const Vector &origin,const Vector &forward,
    CUtlVector<SourceIOSTranslucentDraw> &draws);
