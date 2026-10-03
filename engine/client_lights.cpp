//========= Copyright Valve Corporation, All rights reserved. ============//
// Client light storage, allocation and decay shared with the world renderer.

#include "render_pch.h"
#include "client.h"
#include "cl_main.h"
#include "gl_lightmap.h"
#include "tier0/memdbgon.h"

// FIXME: put these on hunk?
dlight_t		cl_dlights[MAX_DLIGHTS];
dlight_t		cl_elights[MAX_ELIGHTS];
CFastPointLeafNum g_DLightLeafAccessors[MAX_DLIGHTS];
CFastPointLeafNum g_ELightLeafAccessors[MAX_ELIGHTS];

static int CL_AllocLightFromArray( dlight_t *pLights, int lightCount, int key )
{
	int		i;

	// first look for an exact key match
	if (key)
	{
		for ( i = 0; i < lightCount; i++ )
		{
			if (pLights[i].key == key)
				return i;
		}
	}

	// then look for anything else
	for ( i = 0; i < lightCount; i++ )
	{
		if (pLights[i].die < cl.GetTime())
			return i;
	}

	return 0;
}

bool g_bActiveDlights = false;
bool g_bActiveElights = false;
/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_AllocDlight (int key)
{
	int i = CL_AllocLightFromArray( cl_dlights, MAX_DLIGHTS, key );
	dlight_t *dl = &cl_dlights[i];
	R_MarkDLightNotVisible( i );
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
	r_dlightchanged |= (1 << i);
	r_dlightactive |= (1 << i);
	g_bActiveDlights = true;
	return dl;
}


/*
===============
CL_AllocElight

===============
*/
dlight_t *CL_AllocElight (int key)
{
	int i = CL_AllocLightFromArray( cl_elights, MAX_ELIGHTS, key );
	dlight_t *el = &cl_elights[i];
	memset (el, 0, sizeof(*el));
	el->key = key;
	g_bActiveElights = true;
	return el;
}


/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int			i;
	dlight_t	*dl;
	float		time;

	time = cl.GetFrameTime();
	if ( time <= 0.0f )
		return;

	g_bActiveDlights = false;
	g_bActiveElights = false;
	dl = cl_dlights;

	r_dlightchanged = 0;
	r_dlightactive = 0;

	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (!dl->IsRadiusGreaterThanZero())
		{
			R_MarkDLightNotVisible( i );
			continue;
		}

		if ( dl->die < cl.GetTime() )
		{
			r_dlightchanged |= (1 << i);
			dl->radius = 0;
		}
		else if (dl->decay)
		{
			r_dlightchanged |= (1 << i);

			dl->radius -= time*dl->decay;
			if (dl->radius < 0)
			{
				dl->radius = 0;
			}
		}

		if (dl->IsRadiusGreaterThanZero())
		{
			g_bActiveDlights = true;
			r_dlightactive |= (1 << i);
		}
		else
		{
			R_MarkDLightNotVisible( i );
		}
	}

	dl = cl_elights;
	for (i=0 ; i<MAX_ELIGHTS ; i++, dl++)
	{
		if (!dl->IsRadiusGreaterThanZero())
			continue;

		if (dl->die < cl.GetTime())
		{
			dl->radius = 0;
			continue;
		}

		dl->radius -= time*dl->decay;
		if (dl->radius < 0)
		{
			dl->radius = 0;
		}
		if ( dl->IsRadiusGreaterThanZero() )
		{
			g_bActiveElights = true;
		}
	}
}
