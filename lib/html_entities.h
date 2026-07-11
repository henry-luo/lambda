/**
 * @file html_entities.h
 * @brief Unified HTML5 named-entity resolution API
 *
 * Single authoritative table (2 125 WHATWG entries) with O(log n) binary
 * search.  Every named entity — including the five XML/ASCII escapes —
 * resolves to a pre-encoded UTF-8 string.
 *
 * The table is auto-generated from the WHATWG spec:
 *   python3 utils/generate_html5_entities.py
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
 * Look up a named HTML entity.
 *
 * @param name  Entity name without leading '&' or trailing ';'
 * @param len   Length of @p name
 * @return Pre-encoded UTF-8 replacement string (static storage), or NULL if
 *         the name is not a known entity.
 */
const char* html_entity_lookup(const char* name, size_t len);

/**
 * Check if an entity name is one of the five XML/ASCII escapes
 * (lt, gt, amp, quot, apos).
 */
bool html_entity_is_ascii_escape(const char* name, size_t len);

/**
 * Reverse lookup: find the shortest entity name whose UTF-8 value matches
 * the codepoint.  Returns NULL if no entity maps to this codepoint.
 *
 * Note: Only works for single-codepoint entities.
 */
const char* html_entity_name_for_codepoint(uint32_t codepoint);

/**
 * Extract the first Unicode codepoint from a UTF-8 string.
 * Useful after html_entity_lookup() when you need the numeric codepoint.
 *
 * @param utf8  Pointer to a valid UTF-8 sequence
 * @return Decoded codepoint, or 0 on error / empty string.
 */
uint32_t utf8_first_codepoint(const char* utf8);

#ifdef __cplusplus
}
#endif

#endif // HTML_ENTITIES_H
