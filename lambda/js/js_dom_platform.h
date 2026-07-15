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

#ifdef __cplusplus
}
#endif
