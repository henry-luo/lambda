#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool js_unicode_id_is_start(uint32_t codepoint);
bool js_unicode_id_is_continue(uint32_t codepoint);

#ifdef __cplusplus
}
#endif
