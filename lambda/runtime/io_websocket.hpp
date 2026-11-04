#pragma once

#include "../lambda-data.hpp"

// shared by the procedural module and the browser adapter (D9.3.4).
bool io_websocket_protocol_token(const char* text);
Item io_websocket_protocol(Item connection);
Item io_websocket_close_start(Item connection, Item code, Item reason);
void io_websocket_abort(Item connection);
struct CookieJar;
Item io_websocket_open_browser(Item url, Item options, CookieJar* cookies);
