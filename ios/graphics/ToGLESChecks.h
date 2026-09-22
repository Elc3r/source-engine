#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Requires a current GLES 3 context. Writes translated shader sources for diagnosis.
int RunToGLESChecks(const char *directory, char *detail, size_t capacity);
#ifdef __cplusplus
}
#endif
