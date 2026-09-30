//========= Copyright Valve Corporation, All rights reserved. ============//
// World light conversion and attenuation shared by brush and studio rendering.

#include "render_pch.h"
#include "mathlib/lightdesc.h"
#include <float.h>
#include "tier0/memdbgon.h"

#define MIN_LIGHT_VALUE 0.03f

bool WorldLightToMaterialLight( dworldlight_t* pWorldLight, LightDesc_t& light )
{
	// BAD
	light.m_Attenuation0 = 0.0f;
	light.m_Attenuation1 = 0.0f;
	light.m_Attenuation2 = 0.0f;

	switch(pWorldLight->type)
	{
	case emit_spotlight:
		light.m_Type = MATERIAL_LIGHT_SPOT;
		light.m_Attenuation0 = pWorldLight->constant_attn;
		light.m_Attenuation1 = pWorldLight->linear_attn;
		light.m_Attenuation2 = pWorldLight->quadratic_attn;
		light.m_Theta = 2.0 * acos( pWorldLight->stopdot );
		light.m_Phi = 2.0 * acos( pWorldLight->stopdot2 );
		light.m_ThetaDot = pWorldLight->stopdot;
		light.m_PhiDot = pWorldLight->stopdot2;
		light.m_Falloff = pWorldLight->exponent ? pWorldLight->exponent : 1.0f;
		break;

	case emit_surface:
		// A 180 degree spotlight
		light.m_Type = MATERIAL_LIGHT_SPOT;
		light.m_Attenuation2 = 1.0;
		light.m_Theta = M_PI;
		light.m_Phi = M_PI;
		light.m_ThetaDot = 0.0f;
		light.m_PhiDot = 0.0f;
		light.m_Falloff = 1.0f;
		break;

	case emit_point:
		light.m_Type = MATERIAL_LIGHT_POINT;
		light.m_Attenuation0 = pWorldLight->constant_attn;
		light.m_Attenuation1 = pWorldLight->linear_attn;
		light.m_Attenuation2 = pWorldLight->quadratic_attn;
		break;

	case emit_skylight:
		light.m_Type = MATERIAL_LIGHT_DIRECTIONAL;
		break;

	// NOTE: Can't do quake lights in hardware (x-r factor)
	case emit_quakelight:	// not supported
	case emit_skyambient:	// doesn't factor into local lighting
		// skip these
		return false;
	}

	// No attenuation case..
	if ((light.m_Attenuation0 == 0.0f) && (light.m_Attenuation1 == 0.0f) &&
		(light.m_Attenuation2 == 0.0f))
	{
		light.m_Attenuation0 = 1.0f;
	}

	// renormalize light intensity...
	memcpy( &light.m_Position, &pWorldLight->origin, 3 * sizeof(float) );
	memcpy( &light.m_Direction, &pWorldLight->normal, 3 * sizeof(float) );
	light.m_Color[0] = pWorldLight->intensity[0];
	light.m_Color[1] = pWorldLight->intensity[1];
	light.m_Color[2] = pWorldLight->intensity[2];

	// Make it stop when the lighting gets to min%...
	float intensity = sqrtf( DotProduct( light.m_Color, light.m_Color ) );

	// Compute the light range based on attenuation factors
	if (pWorldLight->radius != 0)
	{
		light.m_Range = pWorldLight->radius;
	}
	else
	{
		// FALLBACK: older lights use this
		if (light.m_Attenuation2 == 0.0f)
		{
			if (light.m_Attenuation1 == 0.0f)
			{
				light.m_Range = sqrtf(FLT_MAX);
			}
			else
			{
				light.m_Range = (intensity / MIN_LIGHT_VALUE - light.m_Attenuation0) / light.m_Attenuation1;
			}
		}
		else
		{
			float a = light.m_Attenuation2;
			float b = light.m_Attenuation1;
			float c = light.m_Attenuation0 - intensity / MIN_LIGHT_VALUE;
			float discrim = b * b - 4 * a * c;
			if (discrim < 0.0f)
				light.m_Range = sqrtf(FLT_MAX);
			else
			{
				light.m_Range = (-b + sqrtf(discrim)) / (2.0f * a);
				if (light.m_Range < 0)
					light.m_Range = 0;
			}
		}
	}
	light.m_Flags = LIGHTTYPE_OPTIMIZATIONFLAGS_DERIVED_VALUES_CALCED;
	if( light.m_Attenuation0 != 0.0f )
	{
		light.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION0;
	}
	if( light.m_Attenuation1 != 0.0f )
	{
		light.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION1;
	}
	if( light.m_Attenuation2 != 0.0f )
	{
		light.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION2;
	}
	return true;
}

float Engine_WorldLightDistanceFalloff( const dworldlight_t *wl, const Vector& delta, bool bNoRadiusCheck )
{
	float falloff;

	switch (wl->type)
	{
		case emit_surface:
#if 1
			// Cull out stuff that's too far
			if (wl->radius != 0)
			{
				if ( DotProduct( delta, delta ) > (wl->radius * wl->radius))
					return 0.0f;
			}

			return InvRSquared(delta);
#else
			// 1/r*r
			falloff = DotProduct( delta, delta );
			if (falloff < 1)
				return 1.f;
			else
				return 1.f / falloff;
#endif

			break;

		case emit_skylight:
			return 1.f;
			break;

		case emit_quakelight:
			// X - r;
			falloff = wl->linear_attn - FastSqrt( DotProduct( delta, delta ) );
			if (falloff < 0)
				return 0.f;

			return falloff;
			break;

		case emit_skyambient:
			return 1.f;
			break;

		case emit_point:
		case emit_spotlight:	// directional & positional
			{
				float dist2, dist;

				dist2 = DotProduct( delta, delta );
				dist = FastSqrt( dist2 );

				// Cull out stuff that's too far
				if (!bNoRadiusCheck && (wl->radius != 0) && (dist > wl->radius))
					return 0.f;

				return 1.f / (wl->constant_attn + wl->linear_attn * dist + wl->quadratic_attn * dist2);
			}

			break;
		default:
			// Bug: need to return an error
			break;
	}
	return 1.f;
}

float Engine_WorldLightAngle( const dworldlight_t *wl, const Vector& lnormal, const Vector& snormal, const Vector& delta )
{
	float dot, dot2, ratio = 0;

	switch (wl->type)
	{
		case emit_surface:
			dot = DotProduct( snormal, delta );
			if (dot < 0)
				return 0;

			dot2 = -DotProduct (delta, lnormal);
			if (dot2 <= ON_EPSILON/10)
				return 0; // behind light surface

			return dot * dot2;

		case emit_point:
			dot = DotProduct( snormal, delta );
			if (dot < 0)
				return 0;
			return dot;

		case emit_spotlight:
//			return 1.0; // !!!
			dot = DotProduct( snormal, delta );
			if (dot < 0)
				return 0;

			dot2 = -DotProduct (delta, lnormal);
			if (dot2 <= wl->stopdot2)
				return 0; // outside light cone

			ratio = dot;
			if (dot2 >= wl->stopdot)
				return ratio;	// inside inner cone

			if ((wl->exponent == 1) || (wl->exponent == 0))
			{
				ratio *= (dot2 - wl->stopdot2) / (wl->stopdot - wl->stopdot2);
			}
			else
			{
				ratio *= pow((dot2 - wl->stopdot2) / (wl->stopdot - wl->stopdot2), wl->exponent );
			}
			return ratio;

		case emit_skylight:
			dot2 = -DotProduct( snormal, lnormal );
			if (dot2 < 0)
				return 0;
			return dot2;

		case emit_quakelight:
			// linear falloff
			dot = DotProduct( snormal, delta );
			if (dot < 0)
				return 0;
			return dot;

		case emit_skyambient:
			// not supported
			return 1;

		default:
			// Bug: need to return an error
			break;
	} 
	return 0;
}
