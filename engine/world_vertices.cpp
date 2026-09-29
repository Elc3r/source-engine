//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Brush vertex generation, independent of world mesh ownership.
//===========================================================================//
#include "render_pch.h"

bool TangentSpaceSurfaceSetup( SurfaceHandle_t surfID, Vector &tVect )
{
	Vector sVect;
	VectorCopy( MSurf_TexInfo( surfID )->textureVecsTexelsPerWorldUnits[0].AsVector3D(), sVect );
	VectorCopy( MSurf_TexInfo( surfID )->textureVecsTexelsPerWorldUnits[1].AsVector3D(), tVect );
	VectorNormalize( sVect );
	VectorNormalize( tVect );
	Vector tmpVect;
	CrossProduct( sVect, tVect, tmpVect );
	// Make sure that the tangent space works if textures are mapped "backwards".
	if( DotProduct( MSurf_Plane( surfID ).normal, tmpVect ) > 0.0f )
	{
		return true;
	}
	return false;
}

void TangentSpaceComputeBasis( Vector& tangentS, Vector& tangentT, const Vector& normal, const Vector& tVect, bool negateTangent )
{
	// tangent x binormal = normal
	// tangent = sVect
	// binormal = tVect
	CrossProduct( normal, tVect, tangentS );
	VectorNormalize( tangentS );
	CrossProduct( tangentS, normal, tangentT );
	VectorNormalize( tangentT );

	if ( negateTangent )
	{
		VectorScale( tangentS, -1.0f, tangentS );
	}
}


#ifndef SWDS
void BuildBrushModelVertexArray(worldbrushdata_t *pBrushData, SurfaceHandle_t surfID, BrushVertex_t* pVerts )
{
	SurfaceCtx_t ctx;
	SurfSetupSurfaceContext( ctx, surfID );

	Vector tVect;
	bool negate = false;
	if ( MSurf_Flags( surfID ) & SURFDRAW_TANGENTSPACE )
	{
		negate = TangentSpaceSurfaceSetup( surfID, tVect );
	}

	for ( int i = 0; i < MSurf_VertCount( surfID ); i++ )
	{
		int vertIndex = pBrushData->vertindices[MSurf_FirstVertIndex( surfID ) + i];

		// world-space vertex
		Vector& vec = pBrushData->vertexes[vertIndex].position;

		// output to mesh
		VectorCopy( vec, pVerts[i].m_Pos );

		Vector2D uv;
		SurfComputeTextureCoordinate( ctx, surfID, vec, pVerts[i].m_TexCoord );

		// garymct: normalized (within space of surface) lightmap texture coordinates
		SurfComputeLightmapCoordinate( ctx, surfID, vec, pVerts[i].m_LightmapCoord );

// Activate this if necessary
//		if ( surf->flags & SURFDRAW_BUMPLIGHT )
//		{
//			// bump maps appear left to right in lightmap page memory, calculate
//			// the offset for the width of a single map. The pixel shader will use
//			// this to compute the actual texture coordinates
//			builder.TexCoord2f( 2, ctx.m_BumpSTexCoordOffset, 0.0f );
//		}

		Vector& normal = pBrushData->vertnormals[ pBrushData->vertnormalindices[MSurf_FirstVertNormal( surfID ) + i] ];
		VectorCopy( normal, pVerts[i].m_Normal );

		if ( MSurf_Flags( surfID ) & SURFDRAW_TANGENTSPACE )
		{
			Vector tangentS, tangentT;
			TangentSpaceComputeBasis( tangentS, tangentT, normal, tVect, negate );
			VectorCopy( tangentS, pVerts[i].m_TangentS );
			VectorCopy( tangentT, pVerts[i].m_TangentT );
		}
	}
}
#endif // SWDS
