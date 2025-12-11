#pragma once

#include <string>
#include <string_view>

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
     * 
     * @param text Raw text that may contain special chars
     * @return HTML-safe string with entities
     */
    static std::string escape(std::string_view text);
    
    /**
     * Escape text for use in HTML attributes
     * Includes quote escaping
     * 
     * @param text Raw attribute value
     * @return Attribute-safe string with entities
     */
    static std::string escape_attribute(std::string_view text);
    
    /**
     * Check if text needs escaping
     * Fast pre-check to avoid unnecessary string copies
     * 
     * @param text Text to check
     * @return true if contains characters needing escape
     */
    static bool needs_escaping(std::string_view text);
    
    // Common HTML entities
    static constexpr const char* NBSP = "&nbsp;";     // Non-breaking space
    static constexpr const char* ZWSP = "\u200B";     // Zero-width space (U+200B)
    static constexpr const char* SHY = "&shy;";       // Soft hyphen
    static constexpr const char* MDASH = "—";         // Em dash
    static constexpr const char* NDASH = "–";         // En dash
    static constexpr const char* HELLIP = "…";        // Ellipsis
};

} // namespace html
