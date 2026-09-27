#pragma once
#include <stddef.h>
class IMaterialSystem;
class IMatRenderContext;
struct SceneSample {
    float u, v; // source framebuffer coordinates, normalized
    unsigned char rgba[4];
    unsigned char tolerance;
};
struct SceneSamples {
    int count;
    SceneSample points[49];
};
bool DrawPerspectiveScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity);

inline bool MatchesSceneSample(const unsigned char pixel[4], const SceneSample &sample)
{
    for (int i=0;i<4;++i) {
        int difference=int(pixel[i])-int(sample.rgba[i]);
        int tolerance=i==3 ? 0 : sample.tolerance;
        if (difference < -tolerance || difference > tolerance) return false;
    }
    return true;
}

// Each base mode spans one full revolution. The final mode exercises both
// lights, a lone light in slot 1, and the same pair with swapped slots.
inline unsigned SceneLightingMode(unsigned frame)
{
    unsigned mode=(frame/1200+7)%8;
    if (mode==7) return 9;
    if (mode==6) return 8;
    return mode==5 ? 5+(frame%360)/120 : mode;
}

bool DrawBlendScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity);

bool DrawFogScene(IMaterialSystem *material, IMatRenderContext *context,
    int width, int height, unsigned frame, SceneSamples &samples, char *detail, size_t capacity);
