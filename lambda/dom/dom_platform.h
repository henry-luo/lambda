#pragma once

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

Item dom_storage_local_object(void);
Item dom_storage_session_object(void);
void dom_storage_reset(void);

Item dom_match_media(Item query_item);
void dom_match_media_notify_resize(void);
void dom_match_media_reset(void);

// Host-facing entry point (F23) — see the note in dom.h.
#ifdef __cplusplus
struct JsRuntimeState;
void dom_platform_destroy_context(JsRuntimeState* state);
#endif

#ifdef __cplusplus
}
#endif
