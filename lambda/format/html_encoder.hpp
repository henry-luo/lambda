#pragma once

#include "../../lib/strbuf.h"
#include <stddef.h>  // for size_t

namespace html {

/**
 * HTML entity encoder for safe text output
 * 
 * Escapes characters that have special meaning in HTML:
 * - & → &amp;
 * - < → &lt;
 * - > → &gt;
 * - " → &quot;
 * - ' → &#39; (optional, for attribute safety)
 */
class HtmlEncoder {
public:
    /**
     * Escape HTML special characters in text
     * Writes result to the provided StrBuf
     * 
     * @param sb Output buffer (must not be NULL)
     * @param text Raw text that may contain special chars
     * @param len Length of text (0 means use strlen)
     */
    static void escape(StrBuf* sb, const char* text, size_t len = 0);
    
    /**
     * Escape text for use in HTML attributes
     * Includes quote escaping
     * Writes result to the provided StrBuf
     * 
     * @param sb Output buffer (must not be NULL)
     * @param text Raw attribute value
     * @param len Length of text (0 means use strlen)
     */
    static void escape_attribute(StrBuf* sb, const char* text, size_t len = 0);
    
    /**
     * Check if text needs escaping
     * Fast pre-check to avoid unnecessary string copies
     * 
     * @param text Text to check
     * @param len Length of text (0 means use strlen)
     * @return true if contains characters needing escape
     */
    static bool needs_escaping(const char* text, size_t len = 0);
    
    // Common HTML entities
    static constexpr const char* NBSP = "&nbsp;";     // Non-breaking space
    static constexpr const char* ZWSP = "\u200B";     // Zero-width space (U+200B)
    static constexpr const char* SHY = "&shy;";       // Soft hyphen
    static constexpr const char* MDASH = "—";         // Em dash
    static constexpr const char* NDASH = "–";         // En dash
    static constexpr const char* HELLIP = "…";        // Ellipsis
};

} // namespace html
