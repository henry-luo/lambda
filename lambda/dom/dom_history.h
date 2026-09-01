#pragma once

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

void js_history_install_globals(void);
Item js_history_set_location(Item value);
void js_history_reset(void);

#ifdef __cplusplus
}
#endif

