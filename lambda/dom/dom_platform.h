#pragma once

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

Item js_storage_local_object(void);
Item js_storage_session_object(void);
void js_storage_reset(void);

Item js_match_media(Item query_item);
void js_match_media_notify_resize(void);
void js_match_media_reset(void);

// Host-facing entry point (F23) — see the note in dom.h.
#ifdef __cplusplus
struct JsRuntimeState;
void dom_platform_destroy_context(JsRuntimeState* state);
#endif

#ifdef __cplusplus
}
#endif
