/**
 * @file symbol_resolver.cpp
 * @brief Unified symbol resolution for rendering HTML entities and emoji shortcodes
 */

#include "view.hpp"
#include "../lambda/input/css/css_symbol_hook.h"
#include "../lib/html_entities.h"
#include "../lib/emoji_shortcodes.h"
#include <cstring>

bool is_html_entity(const char* name, size_t len) {
    return html_entity_lookup(name, len) != nullptr;
}

SymbolResolution resolve_symbol(const char* name, size_t len) {
    SymbolResolution result = {};
    result.type = SYMBOL_UNKNOWN;
    result.utf8 = nullptr;
    result.utf8_len = 0;
    result.codepoint = 0;

    if (!name || len == 0) {
        return result;
    }

    // First check emoji (higher priority for symbols like "heart")
    const char* emoji_utf8 = emoji_shortcode_lookup(name, len);
    if (emoji_utf8) {
        result.type = SYMBOL_EMOJI;
        result.utf8 = lam::up(emoji_utf8);
        result.utf8_len = strlen(emoji_utf8);
        // Note: codepoint is 0 for emoji since many are multi-codepoint
        return result;
    }

    // Then check HTML entities
    const char* replacement = html_entity_lookup(name, len);
    if (replacement) {
        result.type = SYMBOL_HTML_ENTITY;
        result.utf8 = lam::up(replacement);
        result.utf8_len = strlen(replacement);
        result.codepoint = utf8_first_codepoint(replacement);
        return result;
    }

    return result;
}

SymbolResolution resolve_symbol_string(const void* string_ptr) {
    SymbolResolution result = {};
    result.type = SYMBOL_UNKNOWN;

    if (!string_ptr) {
        return result;
    }

    // view.hpp exposes the canonical Lambda String; keep symbol decoding on that single layout.
    const String* str = (const String*)string_ptr;
    return resolve_symbol(str->chars, str->len);
}

static CssSymbolResolution radiant_css_symbol_resolve(const char* name, size_t length) {
    SymbolResolution result = resolve_symbol(name, length);
    CssSymbolKind kind = CSS_SYMBOL_UNKNOWN;
    if (result.type == SYMBOL_EMOJI) kind = CSS_SYMBOL_EMOJI;
    else if (result.type == SYMBOL_HTML_ENTITY) kind = CSS_SYMBOL_HTML_ENTITY;
    return {kind, result.utf8};
}

void radiant_register_css_symbol_hook() {
    css_symbol_resolve_register(radiant_css_symbol_resolve);
}
