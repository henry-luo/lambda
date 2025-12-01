/**
 * @file symbol_resolver.h
 * @brief Unified symbol resolution for rendering HTML entities and emoji shortcodes
 *
 * This module provides a unified API for resolving Symbol items to their
 * UTF-8 string representations during rendering. It combines:
 * - HTML entity names (copy → ©, mdash → —, etc.)
 * - Emoji shortcodes (smile → 😄, heart → ❤️, etc.)
 *
 * Resolution priority:
 * 1. Emoji shortcodes (if enabled)
 * 2. HTML entity names
 */

#ifndef SYMBOL_RESOLVER_H
#define SYMBOL_RESOLVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Symbol type after resolution
 */
typedef enum {
    SYMBOL_UNKNOWN = 0,     // Unknown symbol
    SYMBOL_HTML_ENTITY,     // HTML entity (copy, mdash, etc.)
    SYMBOL_EMOJI            // Emoji shortcode (smile, heart, etc.)
} SymbolType;

/**
 * Result of symbol resolution
 */
typedef struct {
    SymbolType type;
    const char* utf8;           // UTF-8 string representation (static, do not free)
    size_t utf8_len;            // Length of UTF-8 string
    uint32_t codepoint;         // Primary Unicode codepoint (for single-codepoint symbols)
} SymbolResolution;

/**
 * Resolve a symbol name to its UTF-8 representation
 *
 * @param name Symbol name (without & ; or : delimiters)
 * @param len Length of symbol name
 * @return SymbolResolution with UTF-8 string and metadata
 *
 * Example:
 *   SymbolResolution r = resolve_symbol("copy", 4);
 *   // r.type == SYMBOL_HTML_ENTITY
 *   // r.utf8 == "©"
 *   // r.codepoint == 0x00A9
 *
 *   SymbolResolution r = resolve_symbol("smile", 5);
 *   // r.type == SYMBOL_EMOJI
 *   // r.utf8 == "😄"
 */
SymbolResolution resolve_symbol(const char* name, size_t len);

/**
 * Resolve a symbol from a Lambda String* symbol
 * Convenience wrapper that extracts name from String
 */
SymbolResolution resolve_symbol_string(const void* string_ptr);

/**
 * Check if a symbol name is a known emoji shortcode
 */
bool is_emoji_shortcode(const char* name, size_t len);

/**
 * Check if a symbol name is a known HTML entity
 */
bool is_html_entity(const char* name, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SYMBOL_RESOLVER_H
