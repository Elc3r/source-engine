#pragma once
#include "tier1/interface.h"
#include <stddef.h>
extern "C" bool SourceIOSInitializeMapLoader(CreateInterfaceFn factory, char *detail, size_t capacity);
extern "C" void SourceIOSShutdownMapLoader();
extern "C" bool SourceIOSLoadWorldMap(const char *name, char *detail, size_t capacity);
extern "C" bool SourceIOSDrawWorldMap(int width, int height, char *detail, size_t capacity);
