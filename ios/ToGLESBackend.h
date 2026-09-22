#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// The host owns EGL and must keep its context current while using this dispatch.
// Initialize tier0's command line before initializing the renderer's entry table.
int InitializeToGLESBackend(char *detail, size_t capacity);
void ShutdownToGLESBackend(void);
// D3D9 attribute-based shaders only: fallback does not emulate gl_VertexID.
void DrawToGLESIndexed(unsigned mode, unsigned start, unsigned end, int count,
    unsigned type, const void *indices, int baseVertex);
#ifdef __cplusplus
}
#endif
