#pragma once

#include "js_runtime.h"

// Fetch §5.1: native Headers and the shared HeadersInit conversion boundary.
Item js_install_headers(Item global);
Item js_headers_list_from_init(Item init, bool request_guard = false);
// Transport bytes are Latin1. Publish the final response block with an
// immutable guard and filter forbidden response headers (Fetch §2.2.2).
Item js_headers_create_http(char* const* lines, int count);
Item js_headers_create(Item init, bool immutable = false);
Item js_headers_clone(Item headers);
// Shared Fetch/WebIDL conversion; validates UTF-16 code units before byte use.
Item js_fetch_byte_string(Item value);
