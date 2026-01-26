/**
 * inline_html.cpp - Raw inline HTML parser
 *
 * Parses inline HTML tags that should pass through without markdown processing.
 * CommonMark defines several types of raw HTML:
 * - Open tags: <tagname attr="value">
 * - Closing tags: </tagname>
 * - HTML comments: <!-- comment -->
 * - Processing instructions: <? ... ?>
 * - Declarations: <! ... >
 * - CDATA sections: <![CDATA[ ... ]]>
 */
#include "inline_common.hpp"
#include "lib/log.h"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

// ============================================================================
// Helper Functions
// ============================================================================

// Helper: Create element from parser
static inline Element* create_element(MarkupParser* parser, const char* tag) {
    return parser->builder.element(tag).final().element;
}

// Helper: Increment element content length
static inline void increment_element_content_length(Element* elem) {
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    elmt_type->content_length++;
}

/**
 * is_ascii_letter - Check if character is ASCII letter
 */
static inline bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/**
 * is_tag_name_char - Check if character is valid in a tag name
 */
static inline bool is_tag_name_char(char c) {
    return isalnum((unsigned char)c) || c == '-';
}

/**
 * is_attribute_name_start_char - Check if character can start an attribute name
 * Per CommonMark spec: [A-Za-z_:]
 */
static inline bool is_attribute_name_start_char(char c) {
    return is_ascii_letter(c) || c == '_' || c == ':';
}

/**
 * is_attribute_name_char - Check if character is valid in an attribute name
 * Per CommonMark spec: [A-Za-z0-9_.:-]
 */
static inline bool is_attribute_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == ':' || c == '-';
}

/**
 * skip_whitespace - Skip over whitespace characters
 */
static inline const char* skip_whitespace(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/**
 * try_parse_html_comment - Try to parse <!-- ... -->
 *
 * Per CommonMark spec section 6.6:
 * - Starts with <!--
 * - Text must not start with > or ->
 * - Text must not end with -
 * - Text must not contain --
 * - Ends with -->
 *
 * But for compatibility with browsers, we accept all comments that
 * start with <!-- and end with -->
 */
static const char* try_parse_html_comment(const char* start) {
    // Must start with <!--
    if (strncmp(start, "<!--", 4) != 0) return nullptr;

    const char* p = start + 4;

    // Find -->
    while (*p) {
        if (strncmp(p, "-->", 3) == 0) {
            return p + 3;
        }
        // Cannot contain --
        if (p[0] == '-' && p[1] == '-' && p[2] != '>') {
            // Continue searching - CommonMark doesn't disallow -- inside
        }
        p++;
    }

    return nullptr;  // No closing -->
}

/**
 * try_parse_processing_instruction - Try to parse <? ... ?>
 */
static const char* try_parse_processing_instruction(const char* start) {
    if (start[0] != '<' || start[1] != '?') return nullptr;

    const char* p = start + 2;

    // Find ?>
    while (*p) {
        if (p[0] == '?' && p[1] == '>') {
            return p + 2;
        }
        p++;
    }

    return nullptr;
}

/**
 * try_parse_declaration - Try to parse <! ... > (not comment or CDATA)
 */
static const char* try_parse_declaration(const char* start) {
    if (start[0] != '<' || start[1] != '!') return nullptr;

    // Skip comment and CDATA which are handled separately
    if (start[2] == '-') return nullptr;  // Comment
    if (start[2] == '[') return nullptr;  // CDATA

    // Must have ASCII letter after <!
    if (!is_ascii_letter(start[2])) return nullptr;

    const char* p = start + 3;

    // Find >
    while (*p && *p != '>') p++;

    if (*p == '>') return p + 1;

    return nullptr;
}

/**
 * try_parse_cdata - Try to parse <![CDATA[ ... ]]>
 */
static const char* try_parse_cdata(const char* start) {
    if (strncmp(start, "<![CDATA[", 9) != 0) return nullptr;

    const char* p = start + 9;

    // Find ]]>
    while (*p) {
        if (strncmp(p, "]]>", 3) == 0) {
            return p + 3;
        }
        p++;
    }

    return nullptr;
}

/**
 * try_parse_html_tag - Try to parse <tag ...> or </tag>
 * Returns pointer past '>' on success, nullptr on failure
 */
static const char* try_parse_html_tag(const char* start) {
    if (*start != '<') return nullptr;

    const char* p = start + 1;

    // Check for closing tag
    bool is_closing = false;
    if (*p == '/') {
        is_closing = true;
        p++;
    }

    // Must have a tag name starting with ASCII letter
    if (!is_ascii_letter(*p)) return nullptr;

    // Parse tag name
    while (is_tag_name_char(*p)) p++;

    if (is_closing) {
        // Closing tag: </tagname> with optional whitespace before >
        p = skip_whitespace(p);
        if (*p == '>') return p + 1;
        return nullptr;
    }

    // Open tag: parse attributes
    bool need_whitespace = false;  // After tag name, attributes can start without ws

    while (*p) {
        const char* before_ws = p;
        p = skip_whitespace(p);
        bool had_whitespace = (p != before_ws);

        // Check for end of tag
        if (*p == '>') return p + 1;
        if (p[0] == '/' && p[1] == '>') return p + 2;  // Self-closing

        // Must have whitespace before another attribute
        if (need_whitespace && !had_whitespace) {
            return nullptr;
        }

        // Parse attribute name - must start with valid start char
        if (!is_attribute_name_start_char(*p)) {
            // If not an attribute start, must be end of tag or error
            if (*p == '>' || (p[0] == '/' && p[1] == '>')) continue;
            return nullptr;
        }

        while (is_attribute_name_char(*p)) p++;

        p = skip_whitespace(p);

        // Check for attribute value
        if (*p == '=') {
            p++;
            p = skip_whitespace(p);

            if (*p == '"') {
                // Double-quoted value
                p++;
                while (*p && *p != '"') p++;
                if (*p != '"') return nullptr;
                p++;
            } else if (*p == '\'') {
                // Single-quoted value
                p++;
                while (*p && *p != '\'') p++;
                if (*p != '\'') return nullptr;
                p++;
            } else {
                // Unquoted value - allowed characters
                while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
                       *p != '"' && *p != '\'' && *p != '=' && *p != '<' &&
                       *p != '>' && *p != '`') {
                    p++;
                }
            }
        }
        // After parsing an attribute, need whitespace before next attribute
        need_whitespace = true;
    }

    return nullptr;  // Didn't find >
}

// ============================================================================
// Autolink Parser
// ============================================================================

/**
 * is_uri_scheme_char - Check if character is valid in URI scheme
 * Per CommonMark: [A-Za-z][A-Za-z0-9+.-]{0,31}
 */
static inline bool is_uri_scheme_char(char c) {
    return isalnum((unsigned char)c) || c == '+' || c == '.' || c == '-';
}

/**
 * try_parse_autolink_uri - Try to parse URI autolink <scheme:...>
 *
 * CommonMark spec: A URI autolink consists of <, an absolute URI, and >.
 * An absolute URI is a scheme followed by : followed by zero or more characters
 * other than ASCII control characters, space, <, and >.
 * Scheme: [A-Za-z][A-Za-z0-9+.-]{0,31}
 */
static const char* try_parse_autolink_uri(const char* start, const char** url_start, const char** url_end) {
    if (*start != '<') return nullptr;

    const char* p = start + 1;

    // Must start with ASCII letter
    if (!is_ascii_letter(*p)) return nullptr;
    p++;

    // Read scheme: [A-Za-z0-9+.-]{0,31}
    int scheme_len = 1;
    while (is_uri_scheme_char(*p) && scheme_len < 32) {
        scheme_len++;
        p++;
    }

    // Must have : after scheme
    if (*p != ':') return nullptr;
    p++;

    *url_start = start + 1;

    // Read URI content until > or invalid char
    while (*p && *p != '>' && *p != ' ' && *p != '<' && (unsigned char)*p >= 32) {
        p++;
    }

    if (*p != '>') return nullptr;

    *url_end = p;
    return p + 1; // past >
}

/**
 * try_parse_autolink_email - Try to parse email autolink <email@domain>
 *
 * Per CommonMark: an email address matching a complex regex pattern.
 * Simplified: local-part @ domain
 */
static const char* try_parse_autolink_email(const char* start, const char** email_start, const char** email_end) {
    if (*start != '<') return nullptr;

    const char* p = start + 1;
    *email_start = p;

    // Local part: [a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+
    bool has_local = false;
    while (*p && (isalnum((unsigned char)*p) ||
           *p == '.' || *p == '!' || *p == '#' || *p == '$' || *p == '%' ||
           *p == '&' || *p == '\'' || *p == '*' || *p == '+' || *p == '/' ||
           *p == '=' || *p == '?' || *p == '^' || *p == '_' || *p == '`' ||
           *p == '{' || *p == '|' || *p == '}' || *p == '~' || *p == '-')) {
        has_local = true;
        p++;
    }

    if (!has_local || *p != '@') return nullptr;
    p++; // skip @

    // Domain: [a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*
    bool has_domain = false;
    while (*p && *p != '>') {
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '.') {
            has_domain = true;
            p++;
        } else {
            return nullptr; // invalid char
        }
    }

    if (!has_domain || *p != '>') return nullptr;

    *email_end = p;
    return p + 1; // past >
}

/**
 * parse_autolink - Parse autolinks <URL> or <email>
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing link element, or ITEM_UNDEFINED if not matched
 */
Item parse_autolink(MarkupParser* parser, const char** text) {
    if (!parser || !text || !*text || **text != '<') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* start = *text;
    const char* end = nullptr;
    const char* url_start = nullptr;
    const char* url_end = nullptr;
    bool is_email = false;

    // Try URI autolink first
    end = try_parse_autolink_uri(start, &url_start, &url_end);

    // Try email autolink if URI failed
    if (!end) {
        end = try_parse_autolink_email(start, &url_start, &url_end);
        is_email = (end != nullptr);
    }

    if (!end) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create link element
    Element* link = create_element(parser, "a");
    if (!link) {
        return Item{.item = ITEM_ERROR};
    }

    // Extract URL/email text
    size_t url_len = url_end - url_start;
    char* url_buf = (char*)arena_alloc(parser->input()->arena, url_len + 1);
    memcpy(url_buf, url_start, url_len);
    url_buf[url_len] = '\0';

    // Add href attribute (mailto: for email)
    String* href_key = parser->builder.createString("href");
    String* href_val;
    if (is_email) {
        char* mailto = (char*)arena_alloc(parser->input()->arena, url_len + 8);
        strcpy(mailto, "mailto:");
        strcat(mailto, url_buf);
        href_val = parser->builder.createString(mailto);
    } else {
        href_val = parser->builder.createString(url_buf);
    }
    parser->builder.putToElement(link, href_key, Item{.item = s2it(href_val)});

    // Add link text (same as URL/email, no mailto)
    String* link_text = parser->builder.createString(url_buf);
    list_push((List*)link, Item{.item = s2it(link_text)});
    increment_element_content_length(link);

    *text = end;
    return Item{.item = (uint64_t)link};
}

// ============================================================================
// Main Parser Function
// ============================================================================

/**
 * parse_raw_html - Parse inline raw HTML
 *
 * Parses inline HTML and feeds it to the shared HTML5 parser.
 * Creates a raw-html element with the raw content for output.
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @return Item containing raw-html element, or ITEM_UNDEFINED if not matched
 */
Item parse_raw_html(MarkupParser* parser, const char** text) {
    if (!parser || !text || !*text) {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* start = *text;
    if (*start != '<') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* end = nullptr;

    // Try each type of raw HTML
    if (!end) end = try_parse_html_comment(start);
    if (!end) end = try_parse_processing_instruction(start);
    if (!end) end = try_parse_cdata(start);
    if (!end) end = try_parse_declaration(start);
    if (!end) end = try_parse_html_tag(start);

    if (!end) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create raw-html element with the content
    Element* html_elem = create_element(parser, "raw-html");
    if (!html_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Add the raw HTML content as a string child
    size_t len = end - start;

    // Feed HTML content to the shared HTML5 parser
    // This accumulates all HTML into a single DOM tree
    // Create a null-terminated copy for the fragment parser
    char* html_copy = (char*)arena_alloc(parser->input()->arena, len + 1);
    memcpy(html_copy, start, len);
    html_copy[len] = '\0';
    parser->parseHtmlFragment(html_copy);

    // Create content string for the raw-html element
    // (preserves original content for output formats that need it)
    String* content = parser->builder.createString(start, len);
    Item content_item = {.item = s2it(content)};
    list_push((List*)html_elem, content_item);
    increment_element_content_length(html_elem);

    // Advance position
    *text = end;

    log_debug("parse_raw_html: parsed '%.*s'", (int)len, start);

    return Item{.item = (uint64_t)html_elem};
}

} // namespace markup
} // namespace lambda
