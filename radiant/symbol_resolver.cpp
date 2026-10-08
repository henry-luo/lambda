/**
 * @file symbol_resolver.cpp
 * @brief Unified symbol resolution for rendering HTML entities and emoji shortcodes
 */

#include "view.hpp"
#include "../lambda/input/css/css_symbol_hook.h"
#include "../lib/html_entities.h"
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

    CssSymbolResolution resolved = css_symbol_resolve(name, len);
    if (resolved.utf8) {
        result.type = resolved.kind == CSS_SYMBOL_EMOJI ? SYMBOL_EMOJI : SYMBOL_HTML_ENTITY;
        result.utf8 = lam::up(resolved.utf8);
        result.utf8_len = strlen(resolved.utf8);
        // emoji can be sequences; only entities expose a single codepoint here.
        if (result.type == SYMBOL_HTML_ENTITY) result.codepoint = utf8_first_codepoint(resolved.utf8);
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
