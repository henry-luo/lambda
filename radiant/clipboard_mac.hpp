#pragma once

#include "clipboard.hpp"

#ifdef __OBJC__
@class NSPasteboard;
// read an owned snapshot from the supplied platform pasteboard (D7.2.5).
ArrayList* clipboard_mac_read_items(NSPasteboard* board);
#endif
