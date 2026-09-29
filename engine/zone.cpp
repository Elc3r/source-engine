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
#include "memstack.h"
#include "tier0/memalloc.h"

#ifdef _X360
#define HUNK_USE_16MB_PAGE
#endif

CMemoryStack g_HunkMemoryStack;
#ifdef HUNK_USE_16MB_PAGE
CMemoryStack g_HunkOverflow;
static bool g_bWarnedOverflow;
#endif

/*
===================
Hunk_AllocName
===================
*/
void *Hunk_AllocName (int size, const char *name, bool bClear)
{
	MEM_ALLOC_CREDIT();
	void * p = g_HunkMemoryStack.Alloc( size, bClear );
	if ( p )
		return p;
#ifdef HUNK_USE_16MB_PAGE
	if ( !g_bWarnedOverflow )
	{
		g_bWarnedOverflow = true;
		DevMsg( "Note: Hunk base page exhausted\n" );
	}

	p = g_HunkOverflow.Alloc( size, bClear );
	if ( p )
		return p;
#endif
	Error( "Engine hunk overflow!\n" );
	return NULL;
}

/*
===================
Hunk_Alloc
===================
*/
void *Hunk_Alloc(int size, bool bClear )
{
	MEM_ALLOC_CREDIT();
	return Hunk_AllocName( size, NULL, bClear );
}

int	Hunk_LowMark(void)
{
	return (int)( g_HunkMemoryStack.GetCurrentAllocPoint() );
}

void Hunk_FreeToLowMark(int mark)
{
	Assert( mark < g_HunkMemoryStack.GetSize() );
#ifdef HUNK_USE_16MB_PAGE
	g_HunkOverflow.FreeAll();
	g_bWarnedOverflow = false;
#endif
	g_HunkMemoryStack.FreeToAllocPoint( mark );
}

int Hunk_MallocSize()
{
#ifdef HUNK_USE_16MB_PAGE
	return g_HunkMemoryStack.GetSize() + g_HunkOverflow.GetSize();
#else
	return g_HunkMemoryStack.GetSize();
#endif
}

int Hunk_Size()
{
#ifdef HUNK_USE_16MB_PAGE
	return g_HunkMemoryStack.GetUsed() + g_HunkOverflow.GetUsed();
#else
	return g_HunkMemoryStack.GetUsed();
#endif
}

void Hunk_Print()
{
#ifdef HUNK_USE_16MB_PAGE
	Msg( "Total used memory:      %d (%d/%d)\n", Hunk_Size(), g_HunkMemoryStack.GetUsed(), g_HunkOverflow.GetUsed() );
	Msg( "Total committed memory: %d (%d/%d)\n", Hunk_MallocSize(), g_HunkMemoryStack.GetSize(), g_HunkOverflow.GetSize() );
#else
	Msg( "Total used memory:      %d\n", Hunk_Size() );
	Msg( "Total committed memory: %d\n", Hunk_MallocSize() );
#endif
}
