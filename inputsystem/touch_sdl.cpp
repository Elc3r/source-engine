//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Linux/Android touch implementation for inputsystem
//
//===========================================================================//

/* For force feedback testing. */
#include "inputsystem.h"
#include "tier1/convar.h"
#include "tier0/icommandline.h"
#include "SDL.h"
#include "SDL_touch.h"
// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Handle the events coming from the Touch SDL subsystem.
//-----------------------------------------------------------------------------
int TouchSDLWatcher( void *userInfo, SDL_Event *event )
{
	CInputSystem *pInputSystem = (CInputSystem *)userInfo;

	if( !event || !pInputSystem ) return 1;

	switch ( event->type ) {
	case SDL_APP_WILLENTERBACKGROUND:
	case SDL_APP_TERMINATING:
		pInputSystem->CancelTouch();
		break;
	case SDL_WINDOWEVENT:
		if ( event->window.event == SDL_WINDOWEVENT_FOCUS_LOST )
			pInputSystem->CancelTouch();
		break;
	case SDL_FINGERDOWN:
		pInputSystem->SDLFingerEvent( IE_FingerDown, event->tfinger.touchId, event->tfinger.fingerId, event->tfinger.x, event->tfinger.y, event->tfinger.dx, event->tfinger.dy );
		break;
	case SDL_FINGERUP:
		pInputSystem->SDLFingerEvent( IE_FingerUp, event->tfinger.touchId, event->tfinger.fingerId, event->tfinger.x, event->tfinger.y, event->tfinger.dx, event->tfinger.dy );
		break;
	case SDL_FINGERMOTION:
		pInputSystem->SDLFingerEvent( IE_FingerMotion ,event->tfinger.touchId, event->tfinger.fingerId, event->tfinger.x, event->tfinger.y, event->tfinger.dx, event->tfinger.dy );
		break;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Initialize touch input
//-----------------------------------------------------------------------------
void CInputSystem::InitializeTouch( void )
{
	if ( m_bTouchInitialized )
		ShutdownTouch();

	// abort startup if user requests no touch
	if ( CommandLine()->FindParm("-notouch") ) return;

	memset( m_touchAccumX, 0, sizeof(m_touchAccumX) );
	memset( m_touchAccumY, 0, sizeof(m_touchAccumY) );

	memset( m_touchContacts, 0, sizeof(m_touchContacts) );
	m_bTouchInitialized = true;
	SDL_AddEventWatch(TouchSDLWatcher, this);
}

void CInputSystem::ShutdownTouch()
{
	if ( !m_bTouchInitialized )
		return;

	SDL_DelEventWatch( TouchSDLWatcher, this );
	CancelTouch();
	m_bTouchInitialized = false;
}

bool CInputSystem::GetTouchAccumulators( int fingerId, float &dx, float &dy )
{
	dx = dy = 0.f;
	if ( fingerId < 0 || fingerId >= TOUCH_FINGER_MAX_COUNT )
		return false;
	dx = m_touchAccumX[fingerId];
	dy = m_touchAccumY[fingerId];

	m_touchAccumX[fingerId] = m_touchAccumY[fingerId] = 0.f;

	return true;
}

void CInputSystem::FingerEvent(int eventType, int fingerId, float x, float y, float dx, float dy)
{
	if( fingerId < 0 || fingerId >= TOUCH_FINGER_MAX_COUNT )
		return;

	if( eventType == IE_FingerUp )
	{
		m_touchAccumX[fingerId] = 0.f;
		m_touchAccumY[fingerId] = 0.f;
	}
	else
	{
		m_touchAccumX[fingerId] += dx;
		m_touchAccumY[fingerId] += dy;
	}

	int _x,_y;
	memcpy( &_x, &x, sizeof(float) );
	memcpy( &_y, &y, sizeof(float) );
	PostEvent(eventType, m_nLastSampleTick, fingerId, _x, _y);
}


// SDL identifiers are arbitrary 64-bit values, unique only within a touch device.
// Keep the engine's small contact indices stable until the corresponding release.
void CInputSystem::SDLFingerEvent( int eventType, int64 deviceId, int64 fingerId,
    float x, float y, float dx, float dy )
{
    if ( !m_bTouchInitialized ) return;
    int slot = -1, freeSlot = -1;
    for ( int i = 0; i < TOUCH_FINGER_MAX_COUNT; ++i )
    {
        const TouchContact &contact = m_touchContacts[i];
        if ( contact.active && contact.deviceId == deviceId && contact.fingerId == fingerId )
            slot = i;
        if ( !contact.active && freeSlot < 0 ) freeSlot = i;
    }
    if ( eventType == IE_FingerDown )
    {
        if ( slot >= 0 ) return; // Ignore duplicate downs; never reset a held control.
        slot = freeSlot;
        if ( slot < 0 ) return; // Excess contacts remain ignored until a new down.
        TouchContact &contact = m_touchContacts[slot];
        contact.deviceId = deviceId;
        contact.fingerId = fingerId;
        contact.active = true;
    }
    if ( slot < 0 ) return; // A motion after cancellation must not re-press controls.
    m_touchContacts[slot].x = x;
    m_touchContacts[slot].y = y;
    FingerEvent( eventType, slot, x, y, dx, dy );
    if ( eventType == IE_FingerUp ) m_touchContacts[slot].active = false;
}

void CInputSystem::CancelTouch()
{
    for ( int i = 0; i < TOUCH_FINGER_MAX_COUNT; ++i )
    {
        TouchContact &contact = m_touchContacts[i];
        if ( contact.active )
            FingerEvent( IE_FingerUp, i, contact.x, contact.y, 0.f, 0.f );
        contact.active = false;
        m_touchAccumX[i] = m_touchAccumY[i] = 0.f;
    }
}
