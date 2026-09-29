//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//

// wrapper for the material system for the engine.

#include "render_pch.h"
#include "view.h"
#include "zone.h"
#include <float.h>
#include "sys_dll.h"
#include "materialsystem/imesh.h"
#include "gl_water.h"
#include "utlrbtree.h"
#include "istudiorender.h"
#include "tier0/dbg.h"
#include "KeyValues.h"
#include "vstdlib/random.h"
#include "lightcache.h"
#include "sysexternal.h"
#include "cmd.h"
#include "modelloader.h"
#include "tier0/icommandline.h"
#include "materialsystem/imaterial.h"
#include "toolframework/itoolframework.h"
#include "toolframework/itoolsystem.h"
#include "tier2/p4helpers.h"
#include "p4lib/ip4.h"
#include "vgui/ISystem.h"
#include <vgui_controls/Controls.h>


extern ConVar developer;
#ifdef _WIN32
#include <crtdbg.h>
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifndef SWDS
extern IMaterialSystem *materials;

IMaterial*	g_materialWireframe;
IMaterial*	g_materialTranslucentSingleColor;
IMaterial*	g_materialTranslucentVertexColor;
IMaterial*	g_materialWorldWireframe;
IMaterial*	g_materialWorldWireframeZBuffer;
IMaterial*	g_materialBrushWireframe;
IMaterial*	g_materialDecalWireframe;
IMaterial*	g_materialDebugLightmap;
IMaterial*	g_materialDebugLightmapZBuffer;
IMaterial*	g_materialDebugLuxels;
IMaterial*	g_materialLeafVisWireframe;
IMaterial*	g_pMaterialWireframeVertexColor;
IMaterial*  g_pMaterialWireframeVertexColorIgnoreZ;
IMaterial*  g_pMaterialLightSprite;
IMaterial*  g_pMaterialShadowBuild;
IMaterial*  g_pMaterialMRMWireframe;
IMaterial*  g_pMaterialWriteZ;
IMaterial*  g_pMaterialWaterDuDv;
IMaterial*  g_pMaterialWaterFirstPass;
IMaterial*  g_pMaterialWaterSecondPass;
IMaterial*	g_pMaterialAmbientCube;
IMaterial*	g_pMaterialDebugFlat;
IMaterial*	g_pMaterialDepthWrite[2][2];
IMaterial*	g_pMaterialSSAODepthWrite[2][2];

#ifdef NEWMESH
CUtlVector<IVertexBuffer *> g_WorldStaticMeshes;  // fixme - rename to g_WorldStaticVertexBuffers
#else
CUtlVector<IMesh *> g_WorldStaticMeshes;
#endif

void WorldStaticMeshCreate( void );
void WorldStaticMeshDestroy( void );


bool TangentSpaceSurfaceSetup( SurfaceHandle_t surfID, Vector &tVect );
void TangentSpaceComputeBasis( Vector& tangent, Vector& binormal, const Vector& normal, const Vector& tVect, bool negateTangent );


//-----------------------------------------------------------------------------
// A console command edit a particular material
//-----------------------------------------------------------------------------
CON_COMMAND_F( mat_edit, "Bring up the material under the crosshair in the editor", FCVAR_CHEAT )
{
	if ( !toolframework->InToolMode() )
		return;

	IMaterial* pMaterial = NULL;
	if ( args.ArgC() < 2 )
	{
		pMaterial = GetMaterialAtCrossHair();
	}
	else
	{
		const char *pMaterialName = args[ 1 ];
		pMaterial = materials->FindMaterial( pMaterialName, "edited materials", false );
	}

	if ( !pMaterial )
	{
		ConMsg( "no/bad material\n" );
	}
	else
	{
		IToolSystem *pToolSystem = toolframework->SwitchToTool( "Material Editor" );
		if ( pToolSystem )
		{
			ConMsg( "editing material \"%s\"\n", pMaterial->GetName() );

			KeyValues *pKeyValues = new KeyValues( "EditMaterial" );
			pKeyValues->SetString( "material", pMaterial->GetName() );
			pToolSystem->PostMessage( 0, pKeyValues );
			pKeyValues->deleteThis();
		}
	}
}


//-----------------------------------------------------------------------------
// A console command to spew out the material under the crosshair
//-----------------------------------------------------------------------------
CON_COMMAND_F( mat_crosshair, "Display the name of the material under the crosshair", FCVAR_CHEAT )
{
	IMaterial* pMaterial = GetMaterialAtCrossHair();
	if (!pMaterial)
		ConMsg ("no/bad material\n");
	else
		ConMsg ("hit material \"%s\"\n", pMaterial->GetName());
}

//-----------------------------------------------------------------------------
// A console command to open the material under the crosshair in the associated editor.
//-----------------------------------------------------------------------------
CON_COMMAND_F( mat_crosshair_edit, "open the material under the crosshair in the editor defined by mat_crosshair_edit_editor", FCVAR_CHEAT )
{
	IMaterial* pMaterial = GetMaterialAtCrossHair();
	if (!pMaterial)
	{
		ConMsg ("no/bad material\n");
	}
	else
	{
		char chResolveName[ 256 ] = {0}, chResolveNameArg[ 256 ] = {0};
		Q_snprintf( chResolveNameArg, sizeof( chResolveNameArg ) - 1, "materials/%s.vmt", pMaterial->GetName() );
		char const *szResolvedName = g_pFileSystem->RelativePathToFullPath( chResolveNameArg, "game", chResolveName, sizeof( chResolveName ) - 1 );
		if ( p4 )
		{
			CP4AutoEditAddFile autop4( szResolvedName );
		}
		else
		{
			Warning( "run with -p4 to get p4 operations upon mat_crosshair_edit\n" );
		}
		vgui::system()->ShellExecute( "open", szResolvedName );
	}
}

//-----------------------------------------------------------------------------
// A console command to open the material under the crosshair in the associated editor.
//-----------------------------------------------------------------------------
CON_COMMAND_F( mat_crosshair_explorer, "open the material under the crosshair in explorer and highlight the vmt file", FCVAR_CHEAT )
{
	IMaterial* pMaterial = GetMaterialAtCrossHair();
	if (!pMaterial)
	{
		ConMsg ("no/bad material\n");
	}
	else
	{
		char chResolveName[ 256 ] = {0}, chResolveNameArg[ 256 ] = {0};
		Q_snprintf( chResolveNameArg, sizeof( chResolveNameArg ) - 1, "materials/%s.vmt", pMaterial->GetName() );
		char const *szResolvedName = g_pFileSystem->RelativePathToFullPath( chResolveNameArg, "game", chResolveName, sizeof( chResolveName ) - 1 );
		char params[256];
		Q_snprintf( params, sizeof( params ) - 1, "/E,/SELECT,%s", szResolvedName );
		vgui::system()->ShellExecuteEx( "open", "explorer.exe", params );
	}
}

//-----------------------------------------------------------------------------
// A console command to open the material under the crosshair in the associated editor.
//-----------------------------------------------------------------------------
CON_COMMAND_F( mat_crosshair_reloadmaterial, "reload the material under the crosshair", FCVAR_CHEAT )
{
	IMaterial* pMaterial = GetMaterialAtCrossHair();
	if (!pMaterial)
	{
		ConMsg ("no/bad material\n");
	}
	else
	{
		materials->ReloadMaterials( pMaterial->GetName() );
	}
}

CON_COMMAND_F( mat_crosshair_printmaterial, "print the material under the crosshair", FCVAR_CHEAT )
{
	IMaterial* pMaterial = GetMaterialAtCrossHair();
	if (!pMaterial)
	{
		ConMsg ("no/bad material\n");
	}
	else
	{
		materials->DebugPrintUsedMaterials( pMaterial->GetName(), true );
	}
}

static void TestBumpSanity( SurfaceHandle_t surfID )
{
	ASSERT_SURF_VALID( surfID );
	// use the last one to check if we need a bumped lightmap, but don't have it so that we can warn.
	bool needsBumpmap = SurfNeedsBumpedLightmaps( surfID );
	bool hasBumpmap = SurfHasBumpedLightmaps( surfID );
	
	if ( needsBumpmap && !hasBumpmap && MSurf_Samples( surfID ) )
	{
		Warning( "Need to rebuild map to get bumped lighting on material %s\n", 
			materialSortInfoArray[MSurf_MaterialSortID( surfID )].material->GetName() );
	}
}

void MaterialSytsem_DoBumpWarnings( void )
{
	int sortID;
	IMaterial *pPrevMaterial = NULL;

	for( sortID = 0; sortID < g_WorldStaticMeshes.Count(); sortID++ )
	{
		if( pPrevMaterial == materialSortInfoArray[sortID].material )
		{
			continue;
		}
		// Find one surface in each material sort info type
		for ( int surfaceIndex = 0; surfaceIndex < host_state.worldbrush->numsurfaces; surfaceIndex++ )
		{
			SurfaceHandle_t surfID = SurfaceHandleFromIndex( surfaceIndex );

			if( MSurf_MaterialSortID( surfID ) == sortID )
			{
				TestBumpSanity( surfID );
				break;
			}
		}
		pPrevMaterial = materialSortInfoArray[sortID].material;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static void GenerateTexCoordsForPrimVerts( void )
{
	int j, k, l;
	for ( int surfaceIndex = 0; surfaceIndex < host_state.worldbrush->numsurfaces; surfaceIndex++ )
	{
		SurfaceHandle_t surfID = SurfaceHandleFromIndex( surfaceIndex );
/*
		if( pSurf->numPrims > 0 )
		{
			ConMsg( "pSurf %d has %d prims (normal: %f %f %f dist: %f)\n", 
				( int )i, ( int )pSurf->numPrims, 
				pSurf->plane->normal[0], pSurf->plane->normal[1], pSurf->plane->normal[2], 
				pSurf->plane->dist );
			ConMsg( "\tfirst primID: %d\n", ( int )pSurf->firstPrimID );
		}
*/
		for( j = 0; j < MSurf_NumPrims( surfID ); j++ )
		{
			mprimitive_t *pPrim;
			assert( MSurf_FirstPrimID( surfID ) + j < host_state.worldbrush->numprimitives );
			pPrim = &host_state.worldbrush->primitives[MSurf_FirstPrimID( surfID ) + j];
			for( k = 0; k < pPrim->vertCount; k++ )
			{
				int lightmapSize[2];
				int lightmapPageSize[2];
				float sOffset, sScale, tOffset, tScale;
				
				materials->GetLightmapPageSize( 
					SortInfoToLightmapPage( MSurf_MaterialSortID( surfID ) ), 
					&lightmapPageSize[0], &lightmapPageSize[1] );
				lightmapSize[0] = ( MSurf_LightmapExtents( surfID )[0] ) + 1;
				lightmapSize[1] = ( MSurf_LightmapExtents( surfID )[1] ) + 1;

				sScale = 1.0f / ( float )lightmapPageSize[0];
				sOffset = ( float )MSurf_OffsetIntoLightmapPage( surfID )[0] * sScale;
				sScale = MSurf_LightmapExtents( surfID )[0] * sScale;

				tScale = 1.0f / ( float )lightmapPageSize[1];
				tOffset = ( float )MSurf_OffsetIntoLightmapPage( surfID )[1] * tScale;
				tScale = MSurf_LightmapExtents( surfID )[1] * tScale;

				for ( l = 0; l < pPrim->vertCount; l++ )
				{
					// world-space vertex
					assert( l+pPrim->firstVert < host_state.worldbrush->numprimverts );
					mprimvert_t &vert = host_state.worldbrush->primverts[l+pPrim->firstVert];
					Vector& vec = vert.pos;

					// base texture coordinate
					vert.texCoord[0] = DotProduct (vec, MSurf_TexInfo( surfID )->textureVecsTexelsPerWorldUnits[0].AsVector3D()) + 
						MSurf_TexInfo( surfID )->textureVecsTexelsPerWorldUnits[0][3];
					vert.texCoord[0] /= MSurf_TexInfo( surfID )->material->GetMappingWidth();

					vert.texCoord[1] = DotProduct (vec, MSurf_TexInfo( surfID )->textureVecsTexelsPerWorldUnits[1].AsVector3D()) + 
						MSurf_TexInfo( surfID )->textureVecsTexelsPerWorldUnits[1][3];
					vert.texCoord[1] /= MSurf_TexInfo( surfID )->material->GetMappingHeight();

					if ( (MSurf_Flags( surfID ) & SURFDRAW_NOLIGHT) )
					{
						vert.lightCoord[0] = 0.5f;
						vert.lightCoord[1] = 0.5f;
					}
					else if ( MSurf_LightmapExtents( surfID )[0] == 0 )
					{
						vert.lightCoord[0] = sOffset;
						vert.lightCoord[1] = tOffset;
					}
					else
					{
						vert.lightCoord[0] = DotProduct (vec, MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[0].AsVector3D()) + 
							MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[0][3];
						vert.lightCoord[0] -= MSurf_LightmapMins( surfID )[0];
						vert.lightCoord[0] += 0.5f;
						vert.lightCoord[0] /= ( float )MSurf_LightmapExtents( surfID )[0]; //pSurf->texinfo->texture->width;

						vert.lightCoord[1] = DotProduct (vec, MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[1].AsVector3D()) + 
							MSurf_TexInfo( surfID )->lightmapVecsLuxelsPerWorldUnits[1][3];
						vert.lightCoord[1] -= MSurf_LightmapMins( surfID )[1];
						vert.lightCoord[1] += 0.5f;
						vert.lightCoord[1] /= ( float )MSurf_LightmapExtents( surfID )[1]; //pSurf->texinfo->texture->height;
						
						vert.lightCoord[0] = sOffset + vert.lightCoord[0] * sScale;
						vert.lightCoord[1] = tOffset + vert.lightCoord[1] * tScale;
					}
				}
			}			
		}
	}
}

struct sortmap_t
{
	MaterialSystem_SortInfo_t info;
	int index;
};

IMatRenderContext *pSortMapRenderContext;

int __cdecl SortMapCompareFunc( const void *pElem0, const void *pElem1 )
{
	const sortmap_t *pMap0 = (const sortmap_t *)pElem0;
	const sortmap_t *pMap1 = (const sortmap_t *)pElem1;
	return pSortMapRenderContext->CompareMaterialCombos( pMap0->info.material, pMap1->info.material, pMap0->info.lightmapPageID, pMap1->info.lightmapPageID );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void MaterialSystem_CreateSortinfo( void )
{
	Assert( !materialSortInfoArray );

	int nSortIDs = materials->GetNumSortIDs();
	materialSortInfoArray = ( MaterialSystem_SortInfo_t * )new MaterialSystem_SortInfo_t[ nSortIDs ];
	Assert( materialSortInfoArray );
	materials->GetSortInfo( materialSortInfoArray );

	int i = 0;
	sortmap_t *pMap = (sortmap_t *)_alloca( sizeof(sortmap_t) * nSortIDs );
	for ( i = 0; i < nSortIDs; i++ )
	{
		pMap[i].info = materialSortInfoArray[i];
		pMap[i].index = i;
	}

	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
	pSortMapRenderContext = pRenderContext;

	qsort( pMap, nSortIDs, sizeof( sortmap_t ), SortMapCompareFunc );

	int *pSortIDRemap = (int *)_alloca( sizeof(int) * nSortIDs );
	for ( i = 0; i < nSortIDs; i++ )
	{
		materialSortInfoArray[i] = pMap[i].info;
		pSortIDRemap[pMap[i].index] = i;
		//Msg("Material %s, lightmap %d ", materialSortInfoArray[i].material->GetName(), materialSortInfoArray[i].lightmapPageID );
	}

	for ( int surfaceIndex = 0; surfaceIndex < host_state.worldbrush->numsurfaces; surfaceIndex++ )
	{
		SurfaceHandle_t surfID = SurfaceHandleFromIndex( surfaceIndex );
		int sortID = MSurf_MaterialSortID( surfID );
#if _DEBUG
		IMaterial *pMaterial = MSurf_TexInfo( surfID )->material;

		if ( !HushAsserts() )
		{
			Assert ( materialSortInfoArray[pSortIDRemap[sortID]].material == pMaterial );
		}
#endif
		MSurf_MaterialSortID( surfID ) = pSortIDRemap[sortID];
	}

	// Create texcoords for subdivided surfaces
	GenerateTexCoordsForPrimVerts();
	// Create the hardware vertex buffers for each face
	WorldStaticMeshCreate();
	if ( developer.GetInt() )
	{
		MaterialSytsem_DoBumpWarnings();
	}
}

//-----------------------------------------------------------------------------
// Purpose: This builds the surface info for a terrain face
//-----------------------------------------------------------------------------

void BuildMSurfaceVerts( const worldbrushdata_t *pBrushData, SurfaceHandle_t surfID, Vector *verts,
							Vector2D *texCoords, Vector2D lightCoords[][4] )
{ 
	SurfaceCtx_t ctx;
	SurfSetupSurfaceContext( ctx, surfID );

	int vertCount = MSurf_VertCount( surfID );
	int vertFirstIndex = MSurf_FirstVertIndex( surfID );
	for ( int i = 0; i < vertCount; i++ )
	{
		int vertIndex = pBrushData->vertindices[vertFirstIndex + i];

		// world-space vertex
		Vector& vec = pBrushData->vertexes[vertIndex].position;

		// output to mesh
		if ( verts )
		{
			VectorCopy( vec, verts[i] );
		}

		if ( texCoords )
		{
			SurfComputeTextureCoordinate( ctx, surfID, vec, texCoords[i] );
		}

		//
		// garymct: normalized (within space of surface) lightmap texture coordinates
		//
		if ( lightCoords )
		{
			SurfComputeLightmapCoordinate( ctx, surfID, vec, lightCoords[i][0] );

			if ( MSurf_Flags( surfID ) & SURFDRAW_BUMPLIGHT )
			{
				// bump maps appear left to right in lightmap page memory, calculate the offset for the
				// width of a single map
				for ( int bumpID = 1; bumpID <= NUM_BUMP_VECTS; bumpID++ )
				{
					lightCoords[i][bumpID][0] = lightCoords[i][0][0] + (bumpID * ctx.m_BumpSTexCoordOffset);
					lightCoords[i][bumpID][1] = lightCoords[i][0][1];
				}
			}
		}
	}
}



void BuildMSurfacePrimVerts( worldbrushdata_t *pBrushData, mprimitive_t *prim, CMeshBuilder &builder, SurfaceHandle_t surfID )
{
	Vector tVect;
	bool negate = false;
// FIXME: For some reason, normals are screwed up on water surfaces.  Revisit this once we have normals started in primverts.
	if ( MSurf_Flags( surfID ) & SURFDRAW_TANGENTSPACE )
	{
		negate = TangentSpaceSurfaceSetup( surfID, tVect );
	}
//	Vector& normal = pBrushData->vertnormals[ pBrushData->vertnormalindices[MSurf_FirstVertNormal( surfID )] ];

	for ( int i = 0; i < prim->vertCount; i++ )
	{
		mprimvert_t &primVert = pBrushData->primverts[prim->firstVert + i];
		builder.Position3fv( primVert.pos.Base() );
		builder.Normal3fv( MSurf_Plane( surfID ).normal.Base() );
//		builder.Normal3fv( normal.Base() );
		builder.TexCoord2fv( 0, primVert.texCoord );
		builder.TexCoord2fv( 1, primVert.lightCoord );
		if ( MSurf_Flags( surfID ) & SURFDRAW_TANGENTSPACE )
		{
			Vector tangentS, tangentT;
			TangentSpaceComputeBasis( tangentS, tangentT, MSurf_Plane( surfID ).normal, tVect, false );
			builder.TangentS3fv( tangentS.Base() );
			builder.TangentT3fv( tangentT.Base() );
		}
		builder.AdvanceVertex();
	}
}

void BuildMSurfacePrimIndices( worldbrushdata_t *pBrushData, mprimitive_t *prim, CMeshBuilder &builder )
{
	for ( int i = 0; i < prim->indexCount; i++ )
	{
		unsigned short primIndex = pBrushData->primindices[prim->firstIndex + i];
		builder.Index( primIndex - prim->firstVert );
		builder.AdvanceIndex();
	}
}

//-----------------------------------------------------------------------------
// Here's a version of the mesh builder used to allow for client DLL to draw brush models
//-----------------------------------------------------------------------------

#endif // SWDS


#ifndef SWDS
IMaterial *GetMaterialAtCrossHair( void )
{
	Vector endPoint;
	Vector lightmapColor;

	// max_range * sqrt(3)
	VectorMA( MainViewOrigin(), COORD_EXTENT * 1.74f, MainViewForward(), endPoint );
	
	SurfaceHandle_t hitSurfID = R_LightVec( MainViewOrigin(), endPoint, false, lightmapColor );
	if( IS_SURF_VALID( hitSurfID ) )
	{
		return MSurf_TexInfo( hitSurfID )->material;
	}
	else
	{
		return NULL;
	}
}

// hack
extern void DrawLightmapPage( int lightmapPageID );

static float textureS, textureT;
static SurfaceHandle_t s_CrossHairSurfID;;
static Vector crossHairDiffuseLightColor;
static Vector crossHairBaseColor;
static float lightmapCoords[2];

void SaveSurfAtCrossHair()
{
	Vector endPoint;
	Vector lightmapColor;

	// max_range * sqrt(3)
	VectorMA(  MainViewOrigin(), COORD_EXTENT * 1.74f, MainViewForward(), endPoint );
	
	s_CrossHairSurfID = R_LightVec( MainViewOrigin(), endPoint, false, lightmapColor, 
		&textureS, &textureT, &lightmapCoords[0], &lightmapCoords[1] );
}


void DebugDrawLightmapAtCrossHair() // Lol
{
#if 0
	IMaterial *pMaterial;
	int lightmapPageSize[2];

	if( !s_CrossHairSurfID )
		return;
	materials->GetLightmapPageSize( materialSortInfoArray[MSurf_MaterialSortID( s_CrossHairSurfID )].lightmapPageID, 
		&lightmapPageSize[0], &lightmapPageSize[1] );
	pMaterial = MSurf_TexInfo( s_CrossHairSurfID )->material;
//	pMaterial->GetLowResColorSample( textureS, textureT, baseColor );
	DrawLightmapPage( materialSortInfoArray[MSurf_MaterialSortID( s_CrossHairSurfID )].lightmapPageID );

	int i;
	for( i = 0; i < 2; i++ )
	{
		xy[i] = 
			( ( float )pCrossHairSurf->offsetIntoLightmapPage[i] / ( float )lightmapPageSize[i] ) +
			lightmapCoord[i] * ( pCrossHairSurf->lightmapExtents[i] / ( float )lightmapPageSize[i] );
	}

	materials->Bind( g_materialWireframe );
	IMesh* pMesh = materials->GetDynamicMesh( g_materialWireframe );
	
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_QUAD, 1 );

	meshBuilder.Position3f( 
	meshBuilder.AdvanceVertex();

	meshBuilder.End();
	pMesh->Draw();
#endif
}

void ReleaseMaterialSystemObjects();
void RestoreMaterialSystemObjects( int nChangeFlags );

void ForceMatSysRestore()
{
	ReleaseMaterialSystemObjects();
	RestoreMaterialSystemObjects( 0 );
}

#endif
