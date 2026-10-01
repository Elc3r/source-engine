#pragma once
#include "tier1/interface.h"
#include <stddef.h>
extern "C" bool SourceIOSInitializeMapLoader(CreateInterfaceFn factory, char *detail, size_t capacity);
extern "C" void SourceIOSShutdownMapLoader();
