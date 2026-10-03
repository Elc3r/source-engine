#pragma once
#include <stddef.h>
// ICvar is process-owned, like the statically linked engine ConVars.

bool InitializeEngineServices(char *detail, size_t capacity);
