#include "ToGLESRuntime.h"
#include "basetypes.h"
#include "../../engine/zone.h"
#include "tier1/memstack.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern CMemoryStack g_HunkMemoryStack;

// Exercise the engine allocator with a small, explicitly owned arena. Full
// Memory_Init/Shutdown additionally requires the engine data-cache service.
int CheckWorldMemory(char *detail, size_t capacity)
{
    if (g_HunkMemoryStack.GetBase()) {
        snprintf(detail,capacity,"World hunk already owned by another service");
        return 0;
    }
    bool valid=true;
    for (int cycle=0; cycle<2 && valid; ++cycle) {
        if (!g_HunkMemoryStack.Init(1024*1024)) {
            snprintf(detail,capacity,"World hunk initialization failed");
            return 0;
        }
        unsigned char *prefix=static_cast<unsigned char *>(Hunk_AllocName(37,"ios-world-guard"));
        valid=Hunk_Size()>=37 && Hunk_LowMark()==Hunk_Size()
            && Hunk_MallocSize()>=Hunk_Size() && (uintptr_t(prefix)%16)==0;
        for (int i=0; i<37; ++i) valid=valid && prefix[i]==0;
        memset(prefix,0xa5,37);
        const int mark=Hunk_LowMark();
        unsigned char *block=static_cast<unsigned char *>(Hunk_Alloc(65539,false));
        valid=valid && (uintptr_t(block)%16)==0 && Hunk_Size()>=mark+65539;
        memset(block,0x7b,65539);
        Hunk_FreeToLowMark(mark);
        valid=valid && Hunk_Size()==mark;
        unsigned char *reused=static_cast<unsigned char *>(Hunk_Alloc(65539));
        valid=valid && reused==block;
        for (int i=0; i<65539; ++i) valid=valid && reused[i]==0;
        for (int i=0; i<37; ++i) valid=valid && prefix[i]==0xa5;
        {
            CHunkMemory<int> indices(0,257);
            valid=valid && indices.Count()==257 && indices.Base()!=NULL;
            for (int i=0; i<257; ++i) indices[i]=i*3;
            for (int i=0; i<257; ++i) valid=valid && indices[i]==i*3;
            indices.Purge(); // Individual arrays do not release the world arena.
            valid=valid && indices.Count()==0 && Hunk_Size()>mark+65539;
        }
        Hunk_FreeToLowMark(0);
        valid=valid && Hunk_Size()==0 && Hunk_LowMark()==0;
        g_HunkMemoryStack.Term();
        valid=valid && g_HunkMemoryStack.GetBase()==NULL;
    }
    snprintf(detail,capacity,"World hunk alignment + zeroing + rewind + reuse + 2 lifecycles: %s",valid?"PASS":"FAIL");
    return valid;
}
