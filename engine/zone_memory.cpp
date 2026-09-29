//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//						ZONE MEMORY ALLOCATION
//
// There is never any space between memblocks, and there will never be two
// contiguous free memblocks.
//
// The rover can be left pointing at a non-empty block
//
// The zone calls are pretty much only used for small strings and structures,
// all big things are allocated on the hunk.
//=============================================================================//

#include "basetypes.h"
#include "zone.h"
#include "host.h"
#include "tier1/strtools.h"
#include "tier0/icommandline.h"
#include "memstack.h"
#include "datacache/idatacache.h"
#include "sys_dll.h"
#include "tier0/memalloc.h"

#define MINIMUM_WIN_MEMORY			0x03000000	// FIXME: copy from sys_dll.cpp, find a common header at some point

#ifdef _X360
#define HUNK_USE_16MB_PAGE
#endif

extern CMemoryStack g_HunkMemoryStack;
#ifdef HUNK_USE_16MB_PAGE
extern CMemoryStack g_HunkOverflow;
#endif

static int GetTargetCacheSize()
{
	int nMemLimit = host_parms.memsize - Hunk_Size();
	if ( nMemLimit < 0x100000 )
	{
		nMemLimit = 0x100000;
	}
	return nMemLimit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Memory_Init( void )
{
	MEM_ALLOC_CREDIT();

#ifdef PLATFORM_64BITS
    // Seems to need to be larger to not get exhausted on
    // 64-bit. Perhaps because of larger pointer sizes.
    int nMaxBytes = 128*1024*1024;
#else
	int nMaxBytes = 48*1024*1024;
#endif
	const int nMinCommitBytes = 0x8000;
#ifndef HUNK_USE_16MB_PAGE
	const int nInitialCommit = 0x280000;
	while ( !g_HunkMemoryStack.Init( nMaxBytes, nMinCommitBytes, nInitialCommit ) )
	{
		Warning( "Unable to allocate %d MB of memory, trying %d MB instead\n", nMaxBytes, nMaxBytes/2 );
		nMaxBytes /= 2;
		if ( nMaxBytes < MINIMUM_WIN_MEMORY )
		{
			Error( "Failed to allocate minimum memory requirement for game (%d MB)\n", MINIMUM_WIN_MEMORY/(1024*1024));
		}
	}
#else
	if ( !g_HunkMemoryStack.InitPhysical( 16*1024*1024 ) || !g_HunkOverflow.Init( nMaxBytes - 16*1024*1024, nMinCommitBytes ) )
	{
		Error( "Failed to allocate minimum memory requirement for game (%d MB)\n", nMaxBytes );
	}

#endif
	g_pDataCache->SetSize( GetTargetCacheSize() );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Memory_Shutdown( void )
{
	g_HunkMemoryStack.FreeAll();

	// This disconnects the engine data cache
	g_pDataCache->SetSize( 0 );
}
