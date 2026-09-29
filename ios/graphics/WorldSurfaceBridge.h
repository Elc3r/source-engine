#pragma once
#include "BspGeometry.h"
class IMaterial;
class IMaterialSystem;
struct WorldSurfaceVertex { float position[3], normal[3], texture[2], lightmap[2]; };
struct WorldSurfaceBinding {
    int page, offset[2], width, height;
    std::vector<WorldSurfaceVertex> vertices;
};
struct WorldSurfaceBatch {
    int sortID, page, vertexCount, indexCount;
    std::vector<int> faces;
};
// The caller holds the material reference and flushes pending draws first.
// Allocates real lightmap pages; temporary engine world/arena state is released.
bool BuildWorldSurfaceBindings(IMaterialSystem *system, IMaterial *material,
    const BspRenderGeometry &geometry, std::vector<WorldSurfaceBinding> &bindings,
    std::vector<WorldSurfaceBatch> &batches, char *detail, size_t capacity);
