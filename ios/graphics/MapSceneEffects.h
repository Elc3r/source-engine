#pragma once
#include "MapEntityInspection.h"
void SourceIOSInitializeSceneEffects();
void SourceIOSShutdownSceneEffects();
int SourceIOSSceneEffectCount();
void SourceIOSAddCameraEffects(const Vector &eye,const Vector wires[4]);
int SourceIOSCollectSceneEffects(const WorldListInfo_t &world,const Vector &origin,
    const Vector &forward,CUtlVector<SourceIOSTranslucentDraw> &draws);
