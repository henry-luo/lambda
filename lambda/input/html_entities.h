/**
 * @file html_entities.h
 * @brief HTML/XML entity resolution and symbol conversion API
 *
 * This module provides unified entity resolution for HTML and XML parsers,
 * supporting both:
 * - ASCII escapes (&lt; &gt; &amp; &quot; &apos;) -> decode to characters
 * - Named entities (&copy; &mdash; etc.) -> return as Symbol for roundtrip
 *
 * Entities are categorized into:
 * 1. ASCII escapes: Always decoded to their character equivalents
 * 2. Named entities: Stored as Lambda Symbol for proper roundtrip formatting
 */

#ifndef HTML_ENTITIES_H
#define HTML_ENTITIES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Entity resolution result type
 */
typedef enum {
    ENTITY_NOT_FOUND = 0,       // Unknown entity
    ENTITY_ASCII_ESCAPE,         // lt, gt, amp, quot, apos - decode inline
    ENTITY_UNICODE_SPACE,        // nbsp, ensp, emsp, thinsp, hairsp - decode inline as Unicode
    ENTITY_UNICODE_MULTI,        // Multi-codepoint sequences - decoded as UTF-8 string
    ENTITY_NAMED                 // Other named entities - return as Symbol
} EntityType;

/**
 * Entity resolution result
 */
typedef struct {
    EntityType type;
    union {
        const char* decoded;     // For ASCII escapes and multi-codepoint: the decoded UTF-8 string
        struct {
            const char* name;    // For named entities: the entity name
            uint32_t codepoint;  // Unicode codepoint for rendering
        } named;
    };
} EntityResult;

/**
 * Resolve an HTML/XML entity by name
 *
 * @param name Entity name without & and ; (e.g., "copy", "lt")
 * @param len Length of the entity name
 * @return EntityResult with type and value information
 *
 * For ASCII escapes (lt, gt, amp, quot, apos):
 *   - Returns ENTITY_ASCII_ESCAPE with decoded string
 *
 * For named entities (copy, reg, mdash, etc.):
 *   - Returns ENTITY_NAMED with name and codepoint
 *
 * For unknown entities:
 *   - Returns ENTITY_NOT_FOUND
 */
EntityResult html_entity_resolve(const char* name, size_t len);

/**
 * Get the Unicode codepoint for a named entity
 * Returns 0 if entity not found
 */
uint32_t html_entity_codepoint(const char* name, size_t len);

/**
 * Get the entity name for a Unicode codepoint (reverse lookup)
 * Returns NULL if no entity maps to this codepoint
 */
const char* html_entity_name_for_codepoint(uint32_t codepoint);

/**
 * Check if an entity name is an ASCII escape (lt, gt, amp, quot, apos)
 */
bool html_entity_is_ascii_escape(const char* name, size_t len);

/**
 * Convert a Unicode codepoint to UTF-8
 * @param codepoint Unicode codepoint
 * @param out Buffer to write UTF-8 bytes (must be at least 5 bytes)
 * @return Number of bytes written, 0 on error
 */
int unicode_to_utf8(uint32_t codepoint, char* out);

#ifdef __cplusplus
}
#endif

#endif // HTML_ENTITIES_H
