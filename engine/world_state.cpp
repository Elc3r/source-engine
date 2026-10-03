//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Shared world-model binding, independent of host frame orchestration.
//===========================================================================//

#include "render_pch.h"
#include "host.h"

CCommonHostState host_state;

void CCommonHostState::SetWorldModel( model_t *pModel )
{
	worldmodel = pModel;
	if ( pModel )
	{
		worldbrush = pModel->brush.pShared;
	}
	else
	{
		worldbrush = NULL;
	}
}
