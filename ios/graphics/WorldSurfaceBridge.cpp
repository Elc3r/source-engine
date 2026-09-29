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
    const int vertexCount=geometry.faces[0].vertices.size()+geometry.faces[1].vertices.size();
    world.numvertexes=world.numvertindices=world.numvertnormals=world.numvertnormalindices=vertexCount;
    world.vertexes=static_cast<mvertex_t *>(Hunk_Alloc(vertexCount*sizeof(mvertex_t)));
    world.vertindices=static_cast<unsigned short *>(Hunk_Alloc(vertexCount*sizeof(unsigned short)));
    world.vertnormals=static_cast<Vector *>(Hunk_Alloc(vertexCount*sizeof(Vector)));
    world.vertnormalindices=static_cast<unsigned short *>(Hunk_Alloc(vertexCount*sizeof(unsigned short)));
    world.surfacenormals=static_cast<msurfacenormal_t *>(Hunk_Alloc(2*sizeof(msurfacenormal_t)));
    int firstVertex=0;
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
        world.surfaces2[i].firstvertindex=firstVertex;
        world.surfacenormals[i].firstvertnormal=firstVertex;
        for (const auto &vertex:face.vertices) {
            world.vertexes[firstVertex].position.Init(vertex.position[0],vertex.position[1],vertex.position[2]);
            world.vertnormals[firstVertex].Init(vertex.normal[0],vertex.normal[1],vertex.normal[2]);
            world.vertindices[firstVertex]=world.vertnormalindices[firstVertex]=firstVertex;
            ++firstVertex;
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
        const size_t count=geometry.faces[i].vertices.size();
        BrushVertex_t *vertices=static_cast<BrushVertex_t *>(Hunk_Alloc(count*sizeof(BrushVertex_t)));
        BuildBrushModelVertexArray(&world,surface,vertices);
        for (size_t v=0; v<count; ++v) {
            const auto &vertex=vertices[v];
            if (!vertex.m_Pos.IsValid() || !vertex.m_Normal.IsValid()
                || !vertex.m_TexCoord.IsValid() || !vertex.m_LightmapCoord.IsValid()) {
                snprintf(detail,capacity,"World surface bridge: non-finite brush vertex"); return false;
            }
            WorldSurfaceVertex output={};
            memcpy(output.position,vertex.m_Pos.Base(),sizeof(output.position));
            memcpy(output.normal,vertex.m_Normal.Base(),sizeof(output.normal));
            memcpy(output.texture,vertex.m_TexCoord.Base(),sizeof(output.texture));
            memcpy(output.lightmap,vertex.m_LightmapCoord.Base(),sizeof(output.lightmap));
            binding.vertices.push_back(output);
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
