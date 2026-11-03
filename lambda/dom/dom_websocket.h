#pragma once
#include "../lambda-data.hpp"
struct DomGcEventOwner;
#ifdef LAMBDA_NO_NETWORK
inline DomGcEventOwner* dom_websocket_event_owner(Item) { return nullptr; }
inline void js_websocket_install(Item, Item) {}
#else
DomGcEventOwner* dom_websocket_event_owner(Item receiver);
// installed only in browser realms, outside D7.3.1's core global allowlist.
void js_websocket_install(Item global, Item event_target_prototype);
#endif
