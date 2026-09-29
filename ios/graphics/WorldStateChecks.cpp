#include "render_pch.h"
#include "zone.h"
#include "tier1/memstack.h"
#include "ToGLESRuntime.h"

extern CMemoryStack g_HunkMemoryStack;

namespace {
void Populate(worldbrushdata_t &world, int count, int extent)
{
    world.numsurfaces=count;
    world.surfaces1=static_cast<msurface1_t *>(Hunk_Alloc(count*sizeof(msurface1_t)));
    world.surfaces2=static_cast<msurface2_t *>(Hunk_Alloc(count*sizeof(msurface2_t)));
    world.surfacelighting=static_cast<msurfacelighting_t *>(Hunk_Alloc(count*sizeof(msurfacelighting_t)));
    for (int i=0; i<count; ++i) {
        world.surfaces1[i].textureMins[0]=extent+i;
        world.surfacelighting[i].m_LightmapExtents[0]=extent+2*i;
    }
}

bool CheckSurfaces(model_t &model, int count, int extent)
{
    host_state.SetWorldModel(&model);
    bool valid=host_state.worldmodel==&model && host_state.worldbrush==model.brush.pShared;
    // These real renderer accessors resolve their default world through host_state.
    for (int i=0; i<count && valid; ++i) {
        SurfaceHandle_t surface=SurfaceHandleFromIndex(i);
        valid=surface==&model.brush.pShared->surfaces2[i] && MSurf_Index(surface)==i
            && MSurf_TextureMins(surface)[0]==extent+i
            && MSurf_LightmapExtents(surface)[0]==extent+2*i;
    }
    return valid;
}
}

int CheckWorldState(char *detail, size_t capacity)
{
    if (host_state.worldmodel || host_state.worldbrush || g_HunkMemoryStack.GetBase()) {
        snprintf(detail,capacity,"World state or arena already owned by another service");
        return 0;
    }
    bool valid=true;
    const float tickInterval=host_state.interval_per_tick;
    for (int cycle=0; cycle<2 && valid; ++cycle) {
        if (!g_HunkMemoryStack.Init(1024*1024)) {
            snprintf(detail,capacity,"World-state arena initialization failed");
            return 0;
        }
        worldbrushdata_t first={}, second={};
        model_t world={}, inlineModel={}, replacement={};
        world.type=inlineModel.type=replacement.type=mod_brush;
        world.brush.pShared=inlineModel.brush.pShared=&first;
        replacement.brush.pShared=&second;
        Populate(first,3,17+cycle);
        Populate(second,2,41+cycle);
        valid=CheckSurfaces(world,3,17+cycle)
            && CheckSurfaces(inlineModel,3,17+cycle)
            && CheckSurfaces(replacement,2,41+cycle)
            && CheckSurfaces(world,3,17+cycle);
        // Unbind before freeing world-owned surface arrays.
        host_state.SetWorldModel(NULL);
        valid=valid && !host_state.worldmodel && !host_state.worldbrush
            && host_state.interval_per_tick==tickInterval;
        Hunk_FreeToLowMark(0);
        valid=valid && Hunk_Size()==0;
        g_HunkMemoryStack.Term();
    }
    snprintf(detail,capacity,"World binding + shared brush + surface accessors + replacement + 2 unloads: %s",valid?"PASS":"FAIL");
    return valid;
}
