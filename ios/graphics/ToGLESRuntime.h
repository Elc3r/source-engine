#pragma once
#include "../ToGLESBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
int InitializeToGLESRuntime(char *detail, size_t capacity);
int CheckToGLESObjects(char *detail, size_t capacity);
int CheckToGLESUploads(char *detail, size_t capacity);
#ifdef __cplusplus
}
#endif
