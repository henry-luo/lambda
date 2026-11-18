#pragma once

#include "lambda/lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

void js_history_install_globals(void);
void dom_history_install_interface(Item prototype);
Item js_history_set_location(Item value, bool hash_member);
void dom_scroll_to_fragment_target(void* target, bool dispatch_beforematch);
void js_history_reset(void);
struct RadiantHistoryTraversal;
bool dom_history_queue_events(const struct RadiantHistoryTraversal* traversal, bool popstate);

#ifdef __cplusplus
}
#endif
