#pragma once
#include "mathlib/vector.h"
// BSP-authored initial poses for inspection, without client/server simulation.
bool SourceIOSInitializeRefractionTexture();
void SourceIOSShutdownRefractionTexture();
int SourceIOSInitializeEntityModels();
void SourceIOSShutdownEntityModels();
int SourceIOSDrawEntityModels(const Vector &viewOrigin, int &pending);

int SourceIOSDrawBrushEntities(const Vector &viewOrigin, int &total, int &pending);

#include "staticpropmgr.h"
#include "utlvector.h"
int SourceIOSCollectTranslucentBrushes(const WorldListInfo_t &world, const Vector &origin,
    const Vector &forward, CUtlVector<SourceIOSTranslucentDraw> &draws, int &refractive);
