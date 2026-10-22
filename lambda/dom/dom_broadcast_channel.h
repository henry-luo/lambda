#pragma once

#include "../lambda-data.hpp"

struct gc_heap;

// declared Jube host state; listener edges are traced from the wrapper (D5.3.3).
Item dom_broadcast_channel_create(Item name);
void* dom_broadcast_channel_native(Item receiver);
void dom_broadcast_channel_listeners_changed(void* channel);

extern "C" {
Item dom_broadcast_channel_name(Item receiver);
Item dom_broadcast_channel_handler(Item receiver, Item error_handler);
Item dom_broadcast_channel_set_handler(Item receiver, Item error_handler, Item value);
Item dom_broadcast_channel_post(Item receiver, Item message);
Item dom_broadcast_channel_close(Item receiver);
void dom_broadcast_channel_destroy(void* channel);
}
