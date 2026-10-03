//========= Copyright Valve Corporation, All rights reserved. ============//
//
// World surface lightmap allocation and texture coordinates.
//===========================================================================//

#include "render_pch.h"
#include "utlrbtree.h"
#include "tier0/memdbgon.h"

MaterialSystem_SortInfo_t *materialSortInfoArray = 0;

int SortInfoToLightmapPage( int sortID )
{
        return materialSortInfoArray[sortID].lightmapPageID;
}

#ifndef SWDS
static void RegisterLightmappedSurface( SurfaceHandle_t surfID )
{
	int lightmapSize[2];
	int allocationWidth, allocationHeight;
	bool bNeedsBumpmap;

	// fixme: lightmapSize needs to be in msurface_t once we
	// switch over to having lightmap size untied to base texture
	// size
	lightmapSize[0] = ( MSurf_LightmapExtents( surfID )[0] ) + 1;
	lightmapSize[1] = ( MSurf_LightmapExtents( surfID )[1] ) + 1;

	// Allocate all bumped lightmaps next to each other so that we can just
	// increment the s texcoord by pSurf->bumpSTexCoordOffset to render the next
	// of the three lightmaps
	bNeedsBumpmap = SurfNeedsBumpedLightmaps( surfID );
	if( bNeedsBumpmap )
	{
		MSurf_Flags( surfID ) |= SURFDRAW_BUMPLIGHT;
		allocationWidth = lightmapSize[0] * ( NUM_BUMP_VECTS+1 );
	}
	else
	{
		MSurf_Flags( surfID ) &= ~SURFDRAW_BUMPLIGHT;
		allocationWidth = lightmapSize[0];
	}
	allocationHeight = lightmapSize[1];

	// register this surface's lightmap
	int offsetIntoLightmapPage[2];
	MSurf_MaterialSortID( surfID ) = materials->AllocateLightmap(
		allocationWidth,
		allocationHeight,
		offsetIntoLightmapPage,
		MSurf_TexInfo( surfID )->material );

	MSurf_OffsetIntoLightmapPage( surfID )[0] = offsetIntoLightmapPage[0];
	MSurf_OffsetIntoLightmapPage( surfID )[1] = offsetIntoLightmapPage[1];
}

static void RegisterUnlightmappedSurface( SurfaceHandle_t surfID )
{
	MSurf_MaterialSortID( surfID ) = materials->AllocateWhiteLightmap( MSurf_TexInfo( surfID )->material );
	MSurf_OffsetIntoLightmapPage( surfID )[0] = 0;
	MSurf_OffsetIntoLightmapPage( surfID )[1] = 0;
}

static bool LightmapLess( const SurfaceHandle_t& surfID1, const SurfaceHandle_t& surfID2 )
{
	// FIXME: This really should be in the material system,
	// as it completely depends on the behavior of the lightmap packer
	bool hasLightmap1 = (MSurf_Flags( surfID1 ) & SURFDRAW_NOLIGHT) == 0;
	bool hasLightmap2 = (MSurf_Flags( surfID2 ) & SURFDRAW_NOLIGHT) == 0;

	// We want lightmapped surfaces to show up first
	if (hasLightmap1 != hasLightmap2)
		return hasLightmap1 > hasLightmap2;

	// The sort by enumeration ID
	IMaterial* pMaterial1 = MSurf_TexInfo( surfID1 )->material;
	IMaterial* pMaterial2 = MSurf_TexInfo( surfID2 )->material;
	int enum1 = pMaterial1->GetEnumerationID();
	int enum2 = pMaterial2->GetEnumerationID();
	if (enum1 != enum2)
		return enum1 < enum2;

	bool hasLightstyle1 = (MSurf_Flags( surfID1 ) & SURFDRAW_HASLIGHTSYTLES) == 0;
	bool hasLightstyle2 = (MSurf_Flags( surfID2 ) & SURFDRAW_HASLIGHTSYTLES) == 0;

	// We want Lightstyled surfaces to show up first
	if (hasLightstyle1 != hasLightstyle2)
		return hasLightstyle1 > hasLightstyle2;

	// Then sort by lightmap area for better packing... (big areas first)
	// NOTE: Don't care about bumpmap increasing area here because it is a linear factor
	// (all surfs with the same material have the same bumpmapping cost)
#if 1
	int area1 = MSurf_LightmapExtents( surfID1 )[0] * MSurf_LightmapExtents( surfID1 )[1];
	int area2 = MSurf_LightmapExtents( surfID2 )[0] * MSurf_LightmapExtents( surfID2 )[1];
	return area2 < area1;
#else
	// Previous algorithm: pack minimum height first
	// NOTE: In d1_trainstation_05, greatest area results in fewer material splits
	//		so I've switched over to that heuristic
	return MSurf_LightmapExtents( surfID1 )[1] < MSurf_LightmapExtents( surfID2 )[1];
#endif
}

void MaterialSystem_RegisterLightmapSurfaces( void )
{
	SurfaceHandle_t surfID = SURFACE_HANDLE_INVALID;

	materials->BeginLightmapAllocation();

	// Add all the surfaces to a list, sorted by lightmapped
	// then by material enumeration then by area
	CUtlRBTree< SurfaceHandle_t, int >	surfaces( 0, host_state.worldbrush->numsurfaces, LightmapLess );
	for( int surfaceIndex = 0; surfaceIndex < host_state.worldbrush->numsurfaces; surfaceIndex++ )
	{
		surfID = SurfaceHandleFromIndex( surfaceIndex );
		if( ( MSurf_TexInfo( surfID )->flags & SURF_NOLIGHT ) ||
			( MSurf_Flags( surfID ) & SURFDRAW_NOLIGHT) )
		{
			MSurf_Flags( surfID ) |= SURFDRAW_NOLIGHT;
		}
		else
		{
			MSurf_Flags( surfID ) &= ~SURFDRAW_NOLIGHT;
		}

		surfaces.Insert(surfID);
	}

	// iterate sorted surfaces
	surfID = SURFACE_HANDLE_INVALID;
	for (int i = surfaces.FirstInorder(); i != surfaces.InvalidIndex(); i = surfaces.NextInorder(i) )
	{
		surfID = surfaces[i];

		bool hasLightmap = ( MSurf_Flags( surfID ) & SURFDRAW_NOLIGHT) == 0;
		if ( hasLightmap )
		{
			RegisterLightmappedSurface( surfID );
		}
		else
		{
			RegisterUnlightmappedSurface( surfID );
		}
	}
	materials->EndLightmapAllocation();
}

bool SurfHasBumpedLightmaps( SurfaceHandle_t surfID )
{
	ASSERT_SURF_VALID( surfID );
	bool hasBumpmap = false;
	if( ( MSurf_TexInfo( surfID )->flags & SURF_BUMPLIGHT ) &&
		( !( MSurf_TexInfo( surfID )->flags & SURF_NOLIGHT ) ) &&
		( host_state.worldbrush->lightdata ) &&
		( MSurf_Samples( surfID ) ) )
	{
		hasBumpmap = true;
	}
	return hasBumpmap;
}

bool SurfNeedsBumpedLightmaps( SurfaceHandle_t surfID )
{
	ASSERT_SURF_VALID( surfID );
	assert( MSurf_TexInfo( surfID ) );
	assert( MSurf_TexInfo( surfID )->material );
	return MSurf_TexInfo( surfID )->material->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_BUMPED_LIGHTMAPS );
}

bool SurfHasLightmap( SurfaceHandle_t surfID )
{
	ASSERT_SURF_VALID( surfID );

	bool hasLightmap = false;
	if( ( !( MSurf_TexInfo( surfID )->flags & SURF_NOLIGHT ) ) &&
		( host_state.worldbrush->lightdata ) &&
		( MSurf_Samples( surfID ) ) )
	{
		hasLightmap = true;
	}
	return hasLightmap;
}

bool SurfNeedsLightmap( SurfaceHandle_t surfID )
{
	ASSERT_SURF_VALID( surfID );
	assert( MSurf_TexInfo( surfID ) );
	assert( MSurf_TexInfo( surfID )->material );
	if (MSurf_TexInfo( surfID )->flags & SURF_NOLIGHT)
		return false;

	return MSurf_TexInfo( surfID )->material->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_LIGHTMAP );
}



void SurfComputeTextureCoordinate( SurfaceCtx_t const& ctx, SurfaceHandle_t surfID,
									    Vector const& vec, Vector2D& uv )
{
	mtexinfo_t* pTexInfo = MSurf_TexInfo( surfID );

	// base texture coordinate
	uv.x = DotProduct (vec, pTexInfo->textureVecsTexelsPerWorldUnits[0].AsVector3D()) +
		pTexInfo->textureVecsTexelsPerWorldUnits[0][3];
	uv.x /= pTexInfo->material->GetMappingWidth();

	uv.y = DotProduct (vec, pTexInfo->textureVecsTexelsPerWorldUnits[1].AsVector3D()) +
		pTexInfo->textureVecsTexelsPerWorldUnits[1][3];
	uv.y /= pTexInfo->material->GetMappingHeight();
}

#if _DEBUG
void CheckTexCoord( float coord )
{
	Assert(coord <= 1.0f );
}
#endif

void SurfComputeLightmapCoordinate( SurfaceCtx_t const& ctx, SurfaceHandle_t surfID,
										 Vector const& vec, Vector2D& uv )
{
	if ( (MSurf_Flags( surfID ) & SURFDRAW_NOLIGHT) )
	{
		uv.x = uv.y = 0.5f;
	}
	else if ( MSurf_LightmapExtents( surfID )[0] == 0 )
	{
		uv = (0.5f * ctx.m_Scale + ctx.m_Offset);
	}
	else
	{
		mtexinfo_t* pTexInfo = MSurf_TexInfo( surfID );

		uv.x = DotProduct (vec, pTexInfo->lightmapVecsLuxelsPerWorldUnits[0].AsVector3D()) +
			pTexInfo->lightmapVecsLuxelsPerWorldUnits[0][3];
		uv.x -= MSurf_LightmapMins( surfID )[0];
		uv.x += 0.5f;

		uv.y = DotProduct (vec, pTexInfo->lightmapVecsLuxelsPerWorldUnits[1].AsVector3D()) +
			pTexInfo->lightmapVecsLuxelsPerWorldUnits[1][3];
		uv.y -= MSurf_LightmapMins( surfID )[1];
		uv.y += 0.5f;

		uv *= ctx.m_Scale;
		uv += ctx.m_Offset;

		assert( uv.IsValid() );
	}
#if _DEBUG
	// This was here for check against displacements and they actually get calculated later correctly.
//	CheckTexCoord( uv.x );
//	CheckTexCoord( uv.y );
#endif
	uv.x = clamp(uv.x, 0.0f, 1.0f);
	uv.y = clamp(uv.y, 0.0f, 1.0f);
}


//-----------------------------------------------------------------------------
// Compute a context necessary for creating vertex data
//-----------------------------------------------------------------------------

void SurfSetupSurfaceContext( SurfaceCtx_t& ctx, SurfaceHandle_t surfID )
{
	materials->GetLightmapPageSize(
		SortInfoToLightmapPage( MSurf_MaterialSortID( surfID ) ),
		&ctx.m_LightmapPageSize[0], &ctx.m_LightmapPageSize[1] );
	ctx.m_LightmapSize[0] = ( MSurf_LightmapExtents( surfID )[0] ) + 1;
	ctx.m_LightmapSize[1] = ( MSurf_LightmapExtents( surfID )[1] ) + 1;

	ctx.m_Scale.x = 1.0f / ( float )ctx.m_LightmapPageSize[0];
	ctx.m_Scale.y = 1.0f / ( float )ctx.m_LightmapPageSize[1];

	ctx.m_Offset.x = ( float )MSurf_OffsetIntoLightmapPage( surfID )[0] * ctx.m_Scale.x;
	ctx.m_Offset.y = ( float )MSurf_OffsetIntoLightmapPage( surfID )[1] * ctx.m_Scale.y;

	if ( ctx.m_LightmapPageSize[0] != 0.0f )
	{
		ctx.m_BumpSTexCoordOffset = ( float )ctx.m_LightmapSize[0] / ( float )ctx.m_LightmapPageSize[0];
	}
	else
	{
		ctx.m_BumpSTexCoordOffset = 0.0f;
	}
}

#endif // SWDS
