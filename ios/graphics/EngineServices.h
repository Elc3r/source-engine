#pragma once
#include <stddef.h>
// ICvar is process-owned, like the statically linked engine ConVars.
// Repeated scene/probe initialization reuses this service.
bool InitializeEngineServices(char *detail, size_t capacity);
bool CheckEngineServices(char *detail, size_t capacity);
