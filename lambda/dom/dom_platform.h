#pragma once

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

Item dom_storage_local_object(void);
Item dom_storage_session_object(void);
void dom_storage_install_interface(Item prototype);
void dom_install_storage_globals(Item global);
void dom_storage_reset(void);
// Rebinds the realm-local storage cache to the current document/session.
void dom_storage_bind_document(void);

Item dom_match_media(Item query_item);
void dom_match_media_notify_resize(void);
void dom_match_media_reset(void);

// caller owns the normalized comma-separated UI language list (mem_free).
char* dom_platform_preferred_languages(void);

// Host-facing entry point (F23) — see the note in dom.h.
#ifdef __cplusplus
struct JsRuntimeState;
#endif

#ifdef __cplusplus
}
#endif
