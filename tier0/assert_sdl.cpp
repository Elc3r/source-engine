//========= Copyright Valve Corporation, All rights reserved. ============//
// SDL assert-window state, shared by launcher and dialog services.
#include "tier0/dbg.h"
#if defined(USE_SDL)
#include "SDL.h"
#endif

#if defined( USE_SDL )
SDL_Window *g_SDLWindow = NULL;

DBG_INTERFACE void SetAssertDialogParent( struct SDL_Window *window )
{
	g_SDLWindow = window;
}

DBG_INTERFACE struct SDL_Window * GetAssertDialogParent()
{
	return g_SDLWindow;
}
#endif

