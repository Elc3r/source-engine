#pragma once
#include "MapEntityInspection.h"
bool SourceIOSInitializeMapParticles();
void SourceIOSShutdownMapParticles();
int SourceIOSCollectMapParticles(const WorldListInfo_t &world,const Vector &origin,
    const Vector &forward,CUtlVector<SourceIOSTranslucentDraw> &draws,int &active);
