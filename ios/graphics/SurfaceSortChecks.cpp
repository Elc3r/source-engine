#include "render_pch.h"
#include "ToGLESRuntime.h"

namespace {
const int surfaceCount=31; // Cross two 15-surface blocks in each material batch.
bool CheckBatch(CMSurfaceSortList &list, int sortGroup, int sortID, msurface2_t *surfaces)
{
    const surfacesortgroup_t &batch=list.GetGroupForSortID(sortGroup,sortID);
    CUtlVector<msurface2_t *> ordered;
    list.GetSurfaceListForGroup(ordered,batch);
    if (ordered.Count()!=surfaceCount || batch.surfaceCount!=surfaceCount
        || list.GetSurfaceAtHead(batch)!=surfaces) return false;
    int vertices=0,triangles=0,nodeVertices=0,nodeIndices=0;
    for (int i=0; i<surfaceCount; ++i) {
        if (ordered[i]!=surfaces+i) return false;
        const int count=3+i%3;
        vertices+=count;
        triangles+=count-2;
        if (i%2==0) { nodeVertices+=count; nodeIndices+=3*(count-2); }
    }
    return batch.vertexCount==vertices && batch.triangleCount==triangles
        && batch.vertexCountNoDetail==nodeVertices && batch.indexCountNoDetail==nodeIndices;
}

bool Empty(CMSurfaceSortList &list, int group, int id)
{
    const surfacesortgroup_t &batch=list.GetGroupForSortID(group,id);
    return !batch.surfaceCount && !batch.vertexCount && !batch.triangleCount
        && !batch.vertexCountNoDetail && !batch.indexCountNoDetail
        && list.GetSurfaceAtHead(batch)==NULL;
}
}

int CheckSurfaceSort(char *detail, size_t capacity)
{
    CMSurfaceSortList list;
    list.Init(2,1);
    msurface2_t surfaces[MAX_MAT_SORT_GROUPS][surfaceCount]={};
    bool valid=true;
    for (int cycle=0; cycle<2 && valid; ++cycle) {
        for (int group=0; group<MAX_MAT_SORT_GROUPS; ++group) {
            valid=valid && list.GetSortList(group).Count()==0 && Empty(list,group,0);
            for (int i=0; i<surfaceCount; ++i) {
                surfaces[group][i].flags=i%2==0 ? SURFDRAW_NODE : 0;
                MSurf_SetVertCount(&surfaces[group][i],3+i%3);
                list.AddSurfaceToTail(&surfaces[group][i],group,1);
            }
        }
        // Resizing moves sparse groups and must repair the compact-list pointers.
        // On the second cycle force another growth beyond the retained capacity.
        const int highID=cycle==0 ? 299 : 699;
        list.EnsureMaxSortIDs(highID+1);
        for (int group=0; group<MAX_MAT_SORT_GROUPS; ++group) {
            valid=valid && CheckBatch(list,group,1,surfaces[group])
                && list.GetSortList(group).Count()==1
                && list.GetSortList(group)[0]==&list.GetGroupForSortID(group,1)
                && Empty(list,group,highID);
            list.AddSurfaceToTail(&surfaces[group][0],group,highID);
            const surfacesortgroup_t &added=list.GetGroupForSortID(group,highID);
            valid=valid && list.GetSortList(group).Count()==2
                && list.GetSortList(group)[1]==&added && added.surfaceCount==1
                && added.vertexCount==3 && added.triangleCount==1
                && list.GetSurfaceAtHead(added)==&surfaces[group][0];
        }
        list.Reset();
        for (int group=0; group<MAX_MAT_SORT_GROUPS; ++group)
            valid=valid && list.GetSortList(group).Count()==0 && Empty(list,group,1)
                && Empty(list,group,highID);
    }
    list.Shutdown();
    snprintf(detail,capacity,"Surface batches: all sort groups + block chains + counts + growth + reset: %s",valid?"PASS":"FAIL");
    return valid;
}
