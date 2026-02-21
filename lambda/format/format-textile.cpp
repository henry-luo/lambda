// format-textile.cpp — Thin wrapper delegating to unified markup emitter
// Original implementation (~760 lines) replaced by format-markup.cpp

#include "format.h"
#include "format-markup.h"
#include "../../lib/stringbuf.h"

void format_textile(StringBuf* sb, Item root_item) {
    format_markup(sb, root_item, &TEXTILE_RULES);
}

String* format_textile_string(Pool* pool, Item root_item) {
    return format_markup_string(pool, root_item, &TEXTILE_RULES);
}
