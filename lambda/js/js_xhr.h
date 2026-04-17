/**
 * XMLHttpRequest implementation for Radiant browser context.
 *
 * Synchronous HTTP via http_fetch() from input_http.cpp.
 * XHR objects created by js_xhr_new() carry a hidden __xhr_id
 * that indexes into a flat state array. Methods read js_get_this()
 * to resolve the id and operate on the C-level state.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// Constructor — called by transpiler for `new XMLHttpRequest()`
Item js_xhr_new(void);

// Reset all XHR state (call between batch runs)
void js_xhr_reset(void);

// XHR methods (called via js_new_function on XHR objects)
Item js_xhr_open(Item method_arg, Item url_arg, Item async_arg);
Item js_xhr_set_request_header(Item name_arg, Item value_arg);
Item js_xhr_send(Item body_arg);
Item js_xhr_abort(void);
Item js_xhr_get_response_header(Item name_arg);
Item js_xhr_get_all_response_headers(void);

#ifdef __cplusplus
}
#endif
