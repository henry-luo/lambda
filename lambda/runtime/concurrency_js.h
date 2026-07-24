#pragma once

#ifdef __cplusplus
#include "../lambda-data.hpp"
#else
#include "../lambda.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void lambda_concurrency_js_init(void);
Item lambda_js_wrap_procedure(Function* function, int arity, const char* name);

#ifdef __cplusplus
}
#endif
