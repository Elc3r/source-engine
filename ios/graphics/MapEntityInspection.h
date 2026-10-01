#pragma once
#include "mathlib/vector.h"
// BSP-authored initial poses for inspection, without client/server simulation.
int SourceIOSInitializeEntityModels();
void SourceIOSShutdownEntityModels();
int SourceIOSDrawEntityModels(const Vector &viewOrigin, int &pending);

int SourceIOSDrawBrushEntities(const Vector &viewOrigin, int &total, int &pending);
