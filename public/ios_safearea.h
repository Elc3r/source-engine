#ifndef SOURCE_IOS_SAFEAREA_H
#define SOURCE_IOS_SAFEAREA_H

#include <SDL_hints.h>
#include <stdio.h>

// UIKit publishes normalized insets so every drawable resolution uses the
// same safe area, including after a landscape rotation.
#define SOURCE_IOS_SAFE_AREA_HINT "SourceIOSSafeArea"

#ifdef __cplusplus
inline void SourceIOSGetSafeArea(int width, int height, int &left, int &top, int &right, int &bottom)
{
    float l = 0, t = 0, r = 0, b = 0;
    const char *value = SDL_GetHint(SOURCE_IOS_SAFE_AREA_HINT);
    if (value) sscanf(value, "%f %f %f %f", &l, &t, &r, &b);
    left = int(l * width + .5f);
    top = int(t * height + .5f);
    right = int(r * width + .5f);
    bottom = int(b * height + .5f);
}
#endif

#endif
