#pragma once
#include "../ToGLESBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
int InitializeToGLESRuntime(char *detail, size_t capacity);
int CheckToGLESFilesystem(const char *assets, const char *writable, char *detail, size_t capacity);
int CheckToGLESObjects(char *detail, size_t capacity, const char *modules);
int CheckToGLESUploads(char *detail, size_t capacity);
#ifdef __cplusplus
}
#endif
