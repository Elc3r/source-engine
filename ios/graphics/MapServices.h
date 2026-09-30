#pragma once
#include "tier1/interface.h"
#include <stddef.h>
void *QueryMapService(const char *name);
bool InitializeMapServices(const char *modules, CreateInterfaceFn factory, char *detail, size_t capacity);
void ShutdownMapServices();
