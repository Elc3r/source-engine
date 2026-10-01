#include "InputChecks.h"
#include "inputsystem/iinputsystem.h"
#include "SDL.h"
#include "tier0/dbg.h"

// Opt-in startup integration check. Run before the client/inspector owns contacts.
// This exercises the real dylib, SDL watcher, engine queue and accumulator API.
bool CheckIOSInput(IInputSystem *input)
{
    bool passed=true;
    auto send=[&](Uint32 type,Sint64 device,Sint64 finger,float dx=0.f) {
        SDL_Event event={}; event.type=type;
        event.tfinger.touchId=device; event.tfinger.fingerId=finger;
        event.tfinger.x=.3f; event.tfinger.y=.4f; event.tfinger.dx=dx;
        passed=(SDL_PushEvent(&event)==1)&&passed;
    };
    auto expect=[&](int type,int count) {
        input->PollInputState();
        passed=(input->GetEventCount()==count)&&passed;
        const InputEvent_t *events=input->GetEventData();
        for(int i=0;i<input->GetEventCount();++i)
            passed=(events[i].m_nType==type && events[i].m_nData==i)&&passed;
    };
    send(SDL_FINGERDOWN,7,INT64_MAX);
    send(SDL_FINGERDOWN,8,INT64_MAX);
    send(SDL_FINGERDOWN,7,-999);
    expect(IE_FingerDown,3);
    send(SDL_FINGERMOTION,7,-999,.125f);
    input->PollInputState();
    passed=(input->GetEventCount()==1 && input->GetEventData()[0].m_nData==2)&&passed;
    float dx=0,dy=0;
    passed=input->GetTouchAccumulators(2,dx,dy)&&dx==.125f&&passed;
    passed=input->GetTouchAccumulators(2,dx,dy)&&dx==0.f&&passed;
    passed=!input->GetTouchAccumulators(-1,dx,dy)&&dx==0.f&&passed;
    passed=!input->GetTouchAccumulators(10,dx,dy)&&passed;
    send(SDL_FINGERDOWN,7,-999); // duplicate down
    for(int i=0;i<8;++i) send(SDL_FINGERDOWN,9,i); // seven slots + overflow
    input->PollInputState();
    passed=(input->GetEventCount()==7)&&passed;
    SDL_Event lost={}; lost.type=SDL_WINDOWEVENT;
    lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    passed=(SDL_PushEvent(&lost)==1)&&passed;
    expect(IE_FingerUp,10);
    send(SDL_FINGERMOTION,7,-999);
    input->PollInputState(); passed=(input->GetEventCount()==0)&&passed;
    send(SDL_FINGERDOWN,7,-999);
    expect(IE_FingerDown,1);
    send(SDL_FINGERUP,7,-999);
    expect(IE_FingerUp,1);
    send(SDL_FINGERDOWN,7,-999);
    expect(IE_FingerDown,1);
    input->Shutdown();
    expect(IE_FingerUp,1);
    send(SDL_FINGERDOWN,7,-999);
    input->PollInputState(); passed=(input->GetEventCount()==0)&&passed;
    passed=(input->Init()==INIT_OK)&&passed;
    send(SDL_FINGERDOWN,7,-999);
    expect(IE_FingerDown,1);
    send(SDL_FINGERUP,7,-999);
    expect(IE_FingerUp,1);
    input->PollInputState();
    // Remove our injected events before the inspector starts draining SDL.
    SDL_FlushEvents(SDL_FINGERDOWN,SDL_FINGERMOTION);
    SDL_Event window;
    SDL_PeepEvents(&window,1,SDL_GETEVENT,SDL_WINDOWEVENT,SDL_WINDOWEVENT);
    Msg("iOS input integration: %s; 64-bit IDs, device separation, accumulators, overflow, focus cancellation, shutdown/reinit\n",passed?"PASS":"FAIL");
    return passed;
}
