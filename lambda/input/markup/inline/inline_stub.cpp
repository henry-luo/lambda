/**
 * inline_stub.cpp - Stub inline parser for Phase 2
 *
 * This file provides a minimal implementation of parse_inline_spans
 * that the block parsers depend on. This stub simply returns text as-is
 * without any inline formatting.
 *
 * In Phase 3, this will be replaced with the full inline parser extraction.
 */
#include "../block/block_common.hpp"

namespace lambda {
namespace markup {

/**
 * parse_inline_spans - Stub implementation for Phase 2
 *
 * Simply returns the text as a string without inline formatting.
 * Will be fully implemented in Phase 3.
 */
Item parse_inline_spans(MarkupParser* parser, const char* text) {
    if (!parser || !text || !*text) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // For Phase 2, just return text as a string
    String* content = parser->builder.createString(text);
    if (!content) {
        return Item{.item = ITEM_ERROR};
    }

    return Item{.item = s2it(content)};
}

} // namespace markup
} // namespace lambda
