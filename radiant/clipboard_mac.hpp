#pragma once

#include "clipboard.hpp"

#ifdef __OBJC__
@class NSPasteboard;
// read an owned snapshot from the supplied platform pasteboard (D10.2.1).
ArrayList* clipboard_mac_read_items(NSPasteboard* board);
#endif
