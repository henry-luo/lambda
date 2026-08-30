#pragma once

// ts_runtime.h — TypeScript runtime helper declarations (callable from C/MIR)

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// runtime type reporting
Item ts_type_info(Item value);

#ifdef __cplusplus
}
#endif
