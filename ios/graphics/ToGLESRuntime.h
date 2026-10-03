#pragma once
#include "../ToGLESBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
const char *PortalGameStartupDetail(void);
int InitializeIOSFilesystem(const char *assets, char *detail, size_t capacity);
int InitializeToGLESRuntime(char *detail, size_t capacity);
// Main render thread only. Start borrows the current EGL window surface;
// Stop must run before that surface or the backend dispatch is destroyed.
int StartToGLESMaterialLoop(const char *modules, char *detail, size_t capacity);
int DrawToGLESMaterialLoop(char *detail, size_t capacity);
void StopToGLESMaterialLoop(void);
int IsSourceWorldMapLoaded(void);
void SetSourceGameAudioActive(int active);
int FinishSourceGameQuit(void);
const char *SourceWorldMapDetail(void);
#ifdef __cplusplus
}
#endif
