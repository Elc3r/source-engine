#include "render_pch.h"
#include "zone.h"
#include "tier1/memstack.h"
#include "WorldSurfaceBridge.h"
#include <limits.h>
#include <math.h>

extern CMemoryStack g_HunkMemoryStack;

namespace {
struct WorldScope {
    IMaterialSystem *previousMaterial;
    WorldScope(IMaterialSystem *system):previousMaterial(materials) { materials=system; }
    ~WorldScope() {
        host_state.SetWorldModel(NULL);
        materialSortInfoArray=NULL;
        materials=previousMaterial;
        g_HunkMemoryStack.Term();
    }
};
}

bool BuildWorldSurfaceBindings(IMaterialSystem *system, IMaterial *material,
    const BspRenderGeometry &geometry, std::vector<WorldSurfaceBinding> &bindings,
    char *detail, size_t capacity)
{
    bindings.clear();
    // This adapter is deliberately limited to the two-face reference scene.
    if (!system || !material || geometry.faces.size()!=2 || host_state.worldmodel
        || host_state.worldbrush || materialSortInfoArray || g_HunkMemoryStack.GetBase()) {
        snprintf(detail,capacity,"World surface bridge: invalid input or world already owned"); return false;
    }
    for (const auto &face:geometry.faces) {
        if (face.material!=geometry.faces[0].material || face.lightmapSize[0]!=4
            || face.lightmapSize[1]!=4 || face.vertices.size()<3 || face.vertices.size()>64
            || face.textureSize[0]!=material->GetMappingWidth()
            || face.textureSize[1]!=material->GetMappingHeight()
            || face.lightmapMins[0]<SHRT_MIN || face.lightmapMins[0]>SHRT_MAX
            || face.lightmapMins[1]<SHRT_MIN || face.lightmapMins[1]>SHRT_MAX) {
            snprintf(detail,capacity,"World surface bridge: unsupported fixture layout"); return false;
        }
    }
    if (material->GetPropertyFlag(MATERIAL_PROPERTY_NEEDS_BUMPED_LIGHTMAPS)
        || !g_HunkMemoryStack.Init(1024*1024)) {
        snprintf(detail,capacity,"World surface bridge: bumped material or arena initialization failed"); return false;
    }
    std::vector<MaterialSystem_SortInfo_t> sort;
    WorldScope scope(system);
    worldbrushdata_t world={}; model_t model={};
    world.numsurfaces=world.numtexinfo=2;
    world.surfaces1=static_cast<msurface1_t *>(Hunk_Alloc(2*sizeof(msurface1_t)));
    world.surfaces2=static_cast<msurface2_t *>(Hunk_Alloc(2*sizeof(msurface2_t)));
    world.surfacelighting=static_cast<msurfacelighting_t *>(Hunk_Alloc(2*sizeof(msurfacelighting_t)));
    world.texinfo=static_cast<mtexinfo_t *>(Hunk_Alloc(2*sizeof(mtexinfo_t)));
    model.type=mod_brush; model.brush.pShared=&world;
    host_state.SetWorldModel(&model);
    for (int i=0; i<2; ++i) {
        const auto &face=geometry.faces[i];
        auto &info=world.texinfo[i]; info.material=material;
        for (int axis=0; axis<2; ++axis) {
            for (int c=0; c<4; ++c) {
                info.textureVecsTexelsPerWorldUnits[axis][c]=face.textureVectors[axis][c];
                info.lightmapVecsLuxelsPerWorldUnits[axis][c]=face.lightmapVectors[axis][c];
            }
            world.surfacelighting[i].m_LightmapMins[axis]=face.lightmapMins[axis];
            world.surfacelighting[i].m_LightmapExtents[axis]=face.lightmapSize[axis]-1;
        }
        world.surfaces2[i].texinfo=i;
        world.surfaces2[i].flags=SURFDRAW_NODE;
        MSurf_SetVertCount(&world.surfaces2[i],face.vertices.size());
    }
    MaterialSystem_RegisterLightmapSurfaces();
    const int count=system->GetNumSortIDs();
    if (count<=0 || count>SHRT_MAX) {
        snprintf(detail,capacity,"World surface bridge: invalid material sort count"); return false;
    }
    sort.resize(count); system->GetSortInfo(sort.data());
    materialSortInfoArray=sort.data();
    std::vector<WorldSurfaceBinding> result;
    for (int i=0; i<2; ++i) {
        SurfaceHandle_t surface=SurfaceHandleFromIndex(i);
        const int id=MSurf_MaterialSortID(surface);
        if (id<0 || id>=count || sort[id].material!=material || sort[id].lightmapPageID<0) {
            snprintf(detail,capacity,"World surface bridge: invalid material/page binding"); return false;
        }
        WorldSurfaceBinding binding={}; binding.page=SortInfoToLightmapPage(id);
        for (int axis=0; axis<2; ++axis) binding.offset[axis]=MSurf_OffsetIntoLightmapPage(surface)[axis];
        system->GetLightmapPageSize(binding.page,&binding.width,&binding.height);
        if (binding.width<=0 || binding.height<=0 || binding.offset[0]<0 || binding.offset[1]<0
            || binding.offset[0]+4>binding.width || binding.offset[1]+4>binding.height) {
            snprintf(detail,capacity,"World surface bridge: atlas bounds invalid"); return false;
        }
        SurfaceCtx_t context; SurfSetupSurfaceContext(context,surface);
        for (const auto &vertex:geometry.faces[i].vertices) {
            Vector position(vertex.position[0],vertex.position[1],vertex.position[2]);
            Vector2D texture,lightmap;
            SurfComputeTextureCoordinate(context,surface,position,texture);
            SurfComputeLightmapCoordinate(context,surface,position,lightmap);
            if (!texture.IsValid() || !lightmap.IsValid()) {
                snprintf(detail,capacity,"World surface bridge: non-finite UVs"); return false;
            }
            WorldSurfaceUV uv={{texture.x,texture.y},{lightmap.x,lightmap.y}};
            binding.vertices.push_back(uv);
        }
        result.push_back(binding);
    }
    if (result[0].page!=result[1].page
        || (result[0].offset[0]==result[1].offset[0] && result[0].offset[1]==result[1].offset[1])) {
        snprintf(detail,capacity,"World surface bridge: expected distinct regions on one page"); return false;
    }
    bindings.swap(result);
    return true;
}
