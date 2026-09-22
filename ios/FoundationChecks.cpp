#include "FoundationChecks.h"
#include "tier0/memalloc.h"
#include "tier0/threadtools.h"
#include "tier1/utlbuffer.h"
#include "tier1/strtools.h"
#include "mathlib/mathlib.h"
#include <cmath>
#include <cstring>

namespace {
bool CheckAllocator()
{
    if (!g_pMemAlloc)
        return false;
    unsigned char *memory = static_cast<unsigned char *>(g_pMemAlloc->Alloc(257));
    if (!memory)
        return false;
    bool passed = g_pMemAlloc->GetSize(memory) >= 257;
    for (unsigned i = 0; i < 257; ++i)
        memory[i] = static_cast<unsigned char>(i ^ 0xA5);
    void *grown = g_pMemAlloc->Realloc(memory, 8192);
    if (!grown) {
        g_pMemAlloc->Free(memory);
        return false;
    }
    memory = static_cast<unsigned char *>(grown);
    passed = passed && g_pMemAlloc->GetSize(memory) >= 8192;
    for (unsigned i = 0; i < 257; ++i)
        passed = passed && memory[i] == static_cast<unsigned char>(i ^ 0xA5);
    g_pMemAlloc->Free(memory);
    return passed;
}

bool CheckAlignment()
{
    for (size_t alignment = 16; alignment <= 256; alignment *= 2) {
        void *memory = MemAlloc_AllocAligned(513, alignment);
        if (!memory)
            return false;
        bool aligned = reinterpret_cast<uintptr_t>(memory) % alignment == 0;
        memset(memory, 0x5A, 513);
        MemAlloc_FreeAligned(memory);
        if (!aligned)
            return false;
    }
    return true;
}

const int WorkerCount = 4;
const int Iterations = 2000;
struct SharedState {
    CThreadMutex mutex;
    CThreadEvent start;
    CThreadLocalInt<int> local;
    int counter;
    SharedState() : start(true), counter(0) { local = 99; }
};
struct WorkerState {
    SharedState *shared;
    int index;
    bool passed;
    ThreadId_t threadID;
};

uintp Worker(void *parameter)
{
    WorkerState &worker = *static_cast<WorkerState *>(parameter);
    SharedState &shared = *worker.shared;
    worker.threadID = ThreadGetCurrentId();
    bool passed = static_cast<int>(shared.local) == 0;
    shared.local = worker.index + 1;
    passed = shared.start.Wait(5000) && passed;
    for (int i = 0; i < Iterations; ++i) {
        shared.mutex.Lock();
        ++shared.counter;
        shared.mutex.Unlock();
        // Also exercise concurrent allocator use through the engine interface.
        void *memory = g_pMemAlloc->Alloc(64 + worker.index);
        if (!memory) {
            passed = false;
            break;
        }
        memset(memory, worker.index, 64 + worker.index);
        g_pMemAlloc->Free(memory);
        if (i % 100 == 0)
            ThreadSleep(1);
        passed = passed && static_cast<int>(shared.local) == worker.index + 1;
    }
    worker.passed = passed;
    return 0;
}

bool CheckWorkers()
{
    SharedState shared;
    WorkerState workers[WorkerCount] = {};
    ThreadHandle_t handles[WorkerCount] = {};
    bool passed = true;
    for (int i = 0; i < WorkerCount; ++i) {
        workers[i].shared = &shared;
        workers[i].index = i;
        handles[i] = CreateSimpleThread(Worker, &workers[i]);
        passed = passed && handles[i] != 0;
    }
    passed = shared.start.Set() && passed;
    for (int i = 0; i < WorkerCount; ++i) {
        if (!handles[i])
            continue;
        // POSIX ThreadJoin has no timed implementation. The simulator runner
        // bounds the complete probe and terminates the app if it hangs.
        bool joined = ThreadJoin(handles[i]);
        if (!joined) {
            // We cannot destroy worker state safely while a worker may use it.
            Error("Foundation probe: joining a worker failed\n");
        }
        passed = ReleaseThreadHandle(handles[i]) && passed && workers[i].passed;
        passed = passed && workers[i].threadID != ThreadGetCurrentId();
        for (int j = 0; j < i; ++j)
            passed = passed && workers[i].threadID != workers[j].threadID;
    }
    return passed && shared.counter == WorkerCount * Iterations && static_cast<int>(shared.local) == 99;
}

bool CheckEvents()
{
    CThreadEvent automatic;
    double before = Plat_FloatTime();
    bool passed = !automatic.Wait(25);
    double elapsed = Plat_FloatTime() - before;
    passed = passed && std::isfinite(elapsed) && elapsed >= 0.015;
    passed = automatic.Set() && passed;
    passed = automatic.Wait(0) && passed;
    passed = !automatic.Wait(0) && passed;
    CThreadEvent manual(true);
    passed = manual.Set() && passed;
    passed = manual.Wait(0) && manual.Wait(0) && passed;
    passed = manual.Reset() && passed;
    return !manual.Wait(0) && passed;
}

bool CheckBuffer()
{
    CUtlBuffer buffer;
    buffer.PutInt(0x12345678);
    buffer.PutFloat(3.25f);
    buffer.PutString("Source iOS");
    bool passed = buffer.GetInt() == 0x12345678 && buffer.GetFloat() == 3.25f;
    char text[32] = {};
    buffer.GetString(text, sizeof(text));
    return passed && buffer.IsValid() && strcmp(text, "Source iOS") == 0;
}

bool CheckStrings()
{
    char text[64];
    V_snprintf(text, sizeof(text), "%s/%d", "ios", 27);
    return V_stricmp(text, "IOS/27") == 0 && V_strlen(text) == 6;
}

bool CheckMath()
{
    MathLib_Init();
    Vector vector(3.0f, 4.0f, 0.0f);
    float length = VectorNormalize(vector);
    float sine, cosine;
    SinCos(M_PI_F / 2.0f, &sine, &cosine);
    return std::fabs(length - 5.0f) < 0.001f &&
           std::fabs(vector.x - 0.6f) < 0.001f &&
           std::fabs(vector.y - 0.8f) < 0.001f &&
           std::fabs(vector.z) < 0.001f &&
           std::fabs(sine - 1.0f) < 0.001f && std::fabs(cosine) < 0.001f;
}
}

extern "C" int Source_RunFoundationChecks(FoundationReport report, void *context)
{
    struct Check { const char *name; bool (*run)(); const char *detail; };
    const Check checks[] = {
        {"allocator", CheckAllocator, "Alloc/GetSize/Realloc preserve 257 bytes when growing to 8192"},
        {"alignment", CheckAlignment, "513-byte allocations aligned to 16, 32, 64, 128 and 256 bytes"},
        {"events", CheckEvents, "Timed wait, auto-reset consumption and manual-reset persistence"},
        {"threads", CheckWorkers, "4 workers, 8000 locked increments, isolated TLS and concurrent allocations"},
        {"tier1_buffer", CheckBuffer, "Binary int/float/string round trip through CUtlBuffer"},
        {"tier1_strings", CheckStrings, "Engine formatting and case-insensitive comparison"},
        {"mathlib", CheckMath, "MathLib_Init, (3,4,0) normalization and pi/2 SinCos"},
    };
    bool passed = true;
    for (const Check &check : checks) {
        Msg("Foundation check: %s starting\n", check.name);
        bool result = check.run();
        passed = passed && result;
        report(check.name, result, check.detail, context);
    }
    return passed;
}
