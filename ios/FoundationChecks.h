#pragma once

// C boundary keeps UIKit independent of the engine's C++ headers and macros.
typedef void (*FoundationReport)(const char *name, int passed, const char *detail, void *context);
typedef int (*FoundationRun)(FoundationReport report, void *context);

#ifdef __cplusplus
extern "C" {
#endif
int Source_RunFoundationChecks(FoundationReport report, void *context);
#ifdef __cplusplus
}
#endif
