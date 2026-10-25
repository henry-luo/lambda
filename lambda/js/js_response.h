#pragma once

#include "js_runtime.h"

Item js_install_response(Item global);
Item js_install_request(Item global);
// Copies transport bytes into precisely traced response storage (D5.3.3).
Item js_response_from_bytes(const void* bytes, int length, Item headers,
    int status, Item status_text, Item url);
