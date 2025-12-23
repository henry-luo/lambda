/**
 * CSS @font-face Rule Parser
 *
 * Parses @font-face rules from CSS and extracts font descriptors.
 * This module handles the CSS-specific parsing; font loading is handled by Radiant.
 */

#pragma once

#include "css_style.hpp"

// Maximum number of src entries in a single @font-face rule
#define CSS_FONT_FACE_MAX_SRC 8

// Individual src entry with URL and format
typedef struct CssFontFaceSrc {
    char* url;                   // URL from url(...)
    char* format;                // Format string from format(...), e.g. "woff", "truetype"
} CssFontFaceSrc;

// Font face descriptor extracted from @font-face rule
typedef struct CssFontFaceDescriptor {
    char* family_name;           // font-family value
    char* src_url;               // URL from src: url(...) - first/fallback URL for backwards compat
    char* src_local;             // local font name from src: local(...)
    CssFontFaceSrc* src_urls;    // Array of all src URL entries with formats
    int src_count;               // Number of entries in src_urls array
    CssEnum font_style;          // normal, italic, oblique
    CssEnum font_weight;         // normal, bold, or numeric 100-900
    CssEnum font_display;        // auto, block, swap, fallback, optional
} CssFontFaceDescriptor;

/**
 * Parse @font-face rule content and extract font descriptor
 *
 * @param content The raw content string from generic_rule.content
 *                Format: "{ font-family: ahem; src: url(...); ... }"
 * @param pool Memory pool for allocations
 * @return Parsed font face descriptor, or NULL on error
 */
CssFontFaceDescriptor* css_parse_font_face_content(const char* content, Pool* pool);

/**
 * Resolve relative URL against base path
 *
 * @param url Relative or absolute URL
 * @param base_path Base path for resolution (e.g., path to HTML file)
 * @param pool Memory pool for result allocation
 * @return Resolved absolute path, or NULL on error
 */
char* css_resolve_font_url(const char* url, const char* base_path, Pool* pool);

/**
 * Extract all @font-face descriptors from a stylesheet
 *
 * @param stylesheet The parsed CSS stylesheet
 * @param base_path Base path for URL resolution
 * @param pool Memory pool for allocations
 * @param out_count Output: number of descriptors extracted
 * @return Array of font face descriptors, or NULL if none found
 */
CssFontFaceDescriptor** css_extract_font_faces(CssStylesheet* stylesheet,
                                                const char* base_path,
                                                Pool* pool,
                                                int* out_count);

/**
 * Free a font face descriptor (if not pool-allocated)
 */
void css_font_face_descriptor_free(CssFontFaceDescriptor* descriptor);
