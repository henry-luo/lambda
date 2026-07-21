#pragma once

// Active runtime process state. This declaration deliberately lives outside
// frozen lambda.h so consumers of the native/MIR-direct runtime have a
// provider-owned surface.
#ifdef __cplusplus
extern "C" {
#endif

extern bool g_dry_run;

#ifdef __cplusplus
}
#endif
