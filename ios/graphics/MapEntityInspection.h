#pragma once
#include "mathlib/vector.h"
// BSP-authored initial poses for inspection, without client/server simulation.
int SourceIOSInitializeEntityModels();
void SourceIOSShutdownEntityModels();
int SourceIOSDrawEntityModels(const Vector &viewOrigin, int &pending);
