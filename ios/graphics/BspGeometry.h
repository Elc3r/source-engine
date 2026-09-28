#pragma once
#include <stddef.h>
#include <string>
#include <vector>

struct BspRenderVertex {
    float position[3], normal[3], uv[2], luxel[2];
};
struct BspRenderFace {
    std::string material;
    std::vector<BspRenderVertex> vertices; // ordered convex polygon
    int lightmapSize[2];
    std::vector<float> lighting; // linear RGBA, one static style
};
struct BspRenderGeometry {
    std::vector<BspRenderFace> faces;
};
// Deliberately limited to uncompressed, planar world faces with static LDR
// lighting. Unsupported features fail explicitly; this is not the engine loader.
bool ReadBspGeometry(const void *bytes, size_t size, BspRenderGeometry &result,
    char *detail, size_t capacity);
bool LoadBspGeometryFixture(BspRenderGeometry &result, char *detail, size_t capacity);
