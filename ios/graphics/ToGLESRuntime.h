#pragma once
#include "../ToGLESBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
const char *PortalGameStartupDetail(void);
int CheckPortalGameModules(const char *directory, char *detail, size_t capacity);
int InitializeToGLESRuntime(char *detail, size_t capacity);
int CheckToGLESFilesystem(const char *assets, const char *writable, char *detail, size_t capacity);
int CheckToGLESObjects(char *detail, size_t capacity, const char *modules);
// Main render thread only. Start borrows the current EGL window surface;
// Stop must run before that surface or the backend dispatch is destroyed.
int StartToGLESMaterialLoop(const char *modules, char *detail, size_t capacity);
int DrawToGLESMaterialLoop(char *detail, size_t capacity);
void StopToGLESMaterialLoop(void);
int IsSourceWorldMapLoaded(void);
int IsSourceWorldMapRendered(void);
void MoveSourceWorldCamera(float forward, float right, float yaw, float pitch, float seconds);
const char *SourceWorldMapDetail(void);
int CheckSurfaceSort(char *detail, size_t capacity);
int CheckWorldState(char *detail, size_t capacity);
int CheckWorldMemory(char *detail, size_t capacity);
int CheckToGLESUploads(char *detail, size_t capacity);
#ifdef __cplusplus
}
#endif
