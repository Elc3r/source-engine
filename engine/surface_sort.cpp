//========= Copyright Valve Corporation, All rights reserved. ============//
//
// World surface batches, independent of material-system startup.
//===========================================================================//

#include "render_pch.h"
#include "tier0/memdbgon.h"

void CMSurfaceSortList::Init( int maxSortIDs, int minMaterialLists )
{
	m_list.RemoveAll();
	m_list.EnsureCapacity(minMaterialLists);
	m_maxSortIDs = maxSortIDs;
	int groupMax = maxSortIDs*MAX_MAT_SORT_GROUPS;
	m_groups.RemoveAll();
	m_groups.EnsureCount(groupMax);
	int groupBytes = (groupMax+7)>>3;
	m_groupUsed.EnsureCount(groupBytes);
	Q_memset(m_groupUsed.Base(), 0, groupBytes);

	for ( int i = 0; i < MAX_MAT_SORT_GROUPS; i++ )
	{
		m_sortGroupLists[i].RemoveAll();
		int cap = (i==0) ? 128 : 16;
		m_sortGroupLists[i].EnsureCapacity(cap);
		groupOffset[i] = m_maxSortIDs * i;
	}
	InitGroup(&m_emptyGroup);
}

void CMSurfaceSortList::InitGroup( surfacesortgroup_t *pGroup )
{
	pGroup->listHead = -1;
	pGroup->listTail = -1;
	pGroup->vertexCount = 0;
	pGroup->groupListIndex = -1;
	pGroup->vertexCountNoDetail = 0;
	pGroup->indexCountNoDetail = 0;
	pGroup->triangleCount = 0;
	pGroup->surfaceCount = 0;
}

void CMSurfaceSortList::Shutdown()
{
}

void CMSurfaceSortList::Reset()
{
	Init( m_maxSortIDs, m_list.NumAllocated() );
}

// this resizes the groups and groupUsed arrays
void CMSurfaceSortList::EnsureMaxSortIDs( int newMaxSortIDs )
{
	if ( newMaxSortIDs > m_maxSortIDs )
	{
		int oldMax = m_maxSortIDs;
		// compute new size, expand by minimum of 256
		newMaxSortIDs += 255;
		newMaxSortIDs -= (newMaxSortIDs&255);
		int groupMax = newMaxSortIDs * MAX_MAT_SORT_GROUPS;
		int groupBytes = (groupMax+7)>>3;
		// resize the arrays
		m_groups.EnsureCount(groupMax);
		m_groupUsed.EnsureCount(groupBytes);
		// now loop through the list backwards and move the old data over
		for ( int i = MAX_MAT_SORT_GROUPS; --i >= 0; )
		{
			for ( int j = newMaxSortIDs; --j >= 0; )
			{
				int newIndex = (i * newMaxSortIDs) + j;
				if ( j < oldMax )
				{
					// when i == 0, the group indices overlap so they don't need to be remapped
					if ( i != 0 )
					{
						int oldIndex = (i * oldMax) + j;
						MarkGroupNotUsed(newIndex);
						if ( IsGroupUsed(oldIndex) )
						{
							MarkGroupNotUsed(oldIndex);
							MarkGroupUsed(newIndex);
							m_groups[newIndex] = m_groups[oldIndex];
							InitGroup( &m_groups[oldIndex] );
						}
					}
					if ( IsGroupUsed(newIndex) && m_groups[newIndex].groupListIndex >= 0 )
					{
						m_sortGroupLists[i][m_groups[newIndex].groupListIndex] = &m_groups[newIndex];
					}
				}
				else
				{
					MarkGroupNotUsed(newIndex);
				}
			}
			groupOffset[i] = i*newMaxSortIDs;
		}
		m_maxSortIDs = newMaxSortIDs;
	}
}


void CMSurfaceSortList::AddSurfaceToTail( msurface2_t *pSurface, int sortGroup, int sortID )
{
	Assert(sortGroup<MAX_MAT_SORT_GROUPS);
	int index = groupOffset[sortGroup] + sortID;
	surfacesortgroup_t *pGroup = &m_groups[index];
	if ( !IsGroupUsed(index) )
	{
		MarkGroupUsed(index);
		InitGroup(pGroup);
	}
	materiallist_t *pList = NULL;
	short prevIndex = -1;
	int vertCount = MSurf_VertCount(pSurface);
	int triangleCount = vertCount - 2;
	pGroup->triangleCount += triangleCount;
	pGroup->surfaceCount++;
	pGroup->vertexCount += vertCount;
	if (MSurf_Flags(pSurface) & SURFDRAW_NODE)
	{
		pGroup->vertexCountNoDetail += vertCount;
		pGroup->indexCountNoDetail += triangleCount * 3;
	}
	if ( pGroup->listTail != m_list.InvalidIndex() )
	{
		// existing block
		pList = &m_list[pGroup->listTail];
		if ( pList->count >= ARRAYSIZE(pList->pSurfaces) )
		{
			prevIndex = pGroup->listTail;
			// no space in existing block
			pList = NULL;
		}
	}
	// use existing block?
	if ( pList )
	{
		pList->pSurfaces[pList->count] = pSurface;
		pList->count++;
	}
	else
	{
		// allocate a new block
		short nextBlock = m_list.AddToTail();
		if ( prevIndex >= 0 )
		{
			m_list[prevIndex].nextBlock = nextBlock;
		}
		pGroup->listTail = nextBlock;
		// handle the first use case
		if ( pGroup->listHead == m_list.InvalidIndex() )
		{
			// UNDONE: This should really be sorted by sortID would help reduce state changes
			// NOTE: Doesn't seem to help much in benchmarks to sort this vector
			index = m_sortGroupLists[sortGroup].AddToTail(pGroup);
			pGroup->groupListIndex = index;
			pGroup->listHead = nextBlock;
		}
		pList = &m_list[nextBlock];
		pList->nextBlock = m_list.InvalidIndex();
		pList->count = 1;
		pList->pSurfaces[0] = pSurface;
	}
}

msurface2_t *CMSurfaceSortList::GetSurfaceAtHead( const surfacesortgroup_t &group ) const
{
	if ( group.listHead == m_list.InvalidIndex() )
		return NULL;
	Assert(m_list[group.listHead].count>0);
	return m_list[group.listHead].pSurfaces[0];
}

void CMSurfaceSortList::GetSurfaceListForGroup( CUtlVector<msurface2_t *> &list, const surfacesortgroup_t &group ) const
{
	MSL_FOREACH_SURFACE_IN_GROUP_BEGIN( *this, group, surfID )
	{
		list.AddToTail(surfID);
	}
	MSL_FOREACH_SURFACE_IN_GROUP_END()
}
