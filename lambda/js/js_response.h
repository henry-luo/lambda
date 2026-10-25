#pragma once

#include "js_runtime.h"

Item js_install_response(Item global);
Item js_install_request(Item global);
template<typename T> class Rooted;
// shared BodyInit extraction for constructors and the HTTP transport.
Item js_fetch_body_bytes(Item body, Rooted<Item>& content_type);
// Copies transport bytes into precisely traced response storage (D5.3.3).
Item js_response_from_bytes(const void* bytes, int length, Item headers,
    int status, Item status_text, Item url, bool redirected = false);
