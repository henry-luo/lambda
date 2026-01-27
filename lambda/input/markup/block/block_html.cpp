/**
 * block_html.cpp - Raw HTML block parser
 *
 * Parses HTML blocks that should pass through without markdown processing.
 * CommonMark defines 7 types of HTML blocks with different start/end conditions.
 *
 * Type 1: <pre>, <script>, <style>, <textarea> - ends at closing tag
 * Type 2: <!-- comment --> - ends at -->
 * Type 3: <? processing instruction ?> - ends at ?>
 * Type 4: <!DOCTYPE or similar - ends at >
 * Type 5: <![CDATA[ - ends at ]]>
 * Type 6: Block-level HTML tags - ends at blank line
 * Type 7: Complete open/close tag on single line - ends at blank line
 */
#include "block_common.hpp"
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

// ============================================================================
// HTML Block Tag Lists (CommonMark spec)
// ============================================================================

// Type 1: Raw text elements (pre, script, style, textarea)
static const char* type1_tags[] = {
    "pre", "script", "style", "textarea", nullptr
};

// Type 6: Block-level HTML elements
static const char* type6_tags[] = {
    "address", "article", "aside", "base", "basefont", "blockquote", "body",
    "caption", "center", "col", "colgroup", "dd", "details", "dialog",
    "dir", "div", "dl", "dt", "fieldset", "figcaption", "figure",
    "footer", "form", "frame", "frameset",
    "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr",
    "html", "iframe", "legend", "li", "link", "main", "menu", "menuitem",
    "nav", "noframes", "ol", "optgroup", "option", "p", "param",
    "search", "section", "summary", "table", "tbody", "td",
    "tfoot", "th", "thead", "title", "tr", "track", "ul", nullptr
};

// ============================================================================
// Helper Functions
// ============================================================================

// Case-insensitive prefix match
static bool starts_with_ci(const char* str, const char* prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix)) {
            return false;
        }
        str++;
        prefix++;
    }
    return true;
}

// Check if character ends a tag name (space, tab, >, />, or end of line)
static bool is_tag_name_end(char c) {
    return c == ' ' || c == '\t' || c == '>' || c == '/' || c == '\0' || c == '\n' || c == '\r';
}

// Check if line contains a string (case-insensitive)
static bool line_contains_ci(const char* line, const char* needle) {
    size_t needle_len = strlen(needle);
    while (*line) {
        if (starts_with_ci(line, needle)) {
            return true;
        }
        line++;
    }
    return false;
}

// Check if line contains a string (case-sensitive)
static bool line_contains(const char* line, const char* needle) {
    return strstr(line, needle) != nullptr;
}

// Check if a line is blank (only whitespace)
static bool is_blank(const char* line) {
    while (*line) {
        if (*line != ' ' && *line != '\t' && *line != '\r' && *line != '\n') {
            return false;
        }
        line++;
    }
    return true;
}

// ============================================================================
// Type 7 Tag Validation
// ============================================================================

/**
 * is_attribute_name_start - Check if char can start an attribute name
 * Per CommonMark spec: [A-Za-z_:]
 */
static inline bool is_attribute_name_start(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}

/**
 * is_attribute_name_char - Check if char is valid in attribute name
 * Per CommonMark spec: [A-Za-z0-9_.:-]
 */
static inline bool is_attribute_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == ':' || c == '-';
}

/**
 * skip_ws - Skip whitespace
 */
static inline const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/**
 * try_parse_complete_tag - Try to parse a complete open or closing tag
 * Returns pointer past '>' on success, nullptr on failure
 */
static const char* try_parse_complete_tag(const char* start) {
    const char* p = start;

    // Check for closing tag
    bool is_closing = false;
    if (*p == '/') {
        is_closing = true;
        p++;
    }

    // Must have a tag name starting with ASCII letter
    if (!isalpha((unsigned char)*p)) return nullptr;

    // Parse tag name
    while (isalnum((unsigned char)*p) || *p == '-') p++;

    if (is_closing) {
        // Closing tag: optional whitespace then >
        p = skip_ws(p);
        if (*p == '>') return p + 1;
        return nullptr;
    }

    // Open tag: parse attributes
    bool need_whitespace = false;  // After tag name, attributes can start directly

    while (*p) {
        const char* before_ws = p;
        p = skip_ws(p);
        bool had_whitespace = (p != before_ws);

        // Check for end of tag
        if (*p == '>') return p + 1;
        if (p[0] == '/' && p[1] == '>') return p + 2;  // Self-closing

        // If we need whitespace before an attribute but didn't get any, fail
        if (need_whitespace && !had_whitespace) {
            return nullptr;
        }

        // Parse attribute name - must start with valid char
        if (!is_attribute_name_start(*p)) {
            // If not at end of tag, it's invalid
            if (*p != '>' && !(p[0] == '/' && p[1] == '>')) {
                return nullptr;
            }
            continue;
        }

        while (is_attribute_name_char(*p)) p++;

        p = skip_ws(p);

        // Check for attribute value
        if (*p == '=') {
            p++;
            p = skip_ws(p);

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
// HTML Block Type Detection
// ============================================================================

/**
 * HtmlBlockType - The 7 types of HTML blocks defined by CommonMark
 */
enum class HtmlBlockType {
    NONE = 0,
    TYPE_1,  // pre, script, style, textarea
    TYPE_2,  // <!-- comment -->
    TYPE_3,  // <? processing instruction ?>
    TYPE_4,  // <!DOCTYPE or similar
    TYPE_5,  // <![CDATA[
    TYPE_6,  // Block-level tags
    TYPE_7   // Complete tag on single line
};

/**
 * detect_html_block_type - Detect what type of HTML block starts at this line
 *
 * @param line The line to check
 * @return HtmlBlockType indicating the block type, or NONE if not an HTML block
 */
HtmlBlockType detect_html_block_type(const char* line) {
    // Skip up to 3 spaces
    const char* p = line;
    int spaces = 0;
    while (*p == ' ' && spaces < 4) { spaces++; p++; }
    if (spaces >= 4) return HtmlBlockType::NONE;  // Indented code, not HTML

    // Must start with <
    if (*p != '<') return HtmlBlockType::NONE;
    p++;

    // Type 2: <!-- comment
    if (p[0] == '!' && p[1] == '-' && p[2] == '-') {
        return HtmlBlockType::TYPE_2;
    }

    // Type 3: <? processing instruction
    if (*p == '?') {
        return HtmlBlockType::TYPE_3;
    }

    // Type 4: <! followed by ASCII letter (DOCTYPE, etc.)
    if (*p == '!' && isalpha((unsigned char)p[1])) {
        return HtmlBlockType::TYPE_4;
    }

    // Type 5: <![CDATA[
    if (strncmp(p, "![CDATA[", 8) == 0) {
        return HtmlBlockType::TYPE_5;
    }

    // Check for closing tag (starts with /)
    bool is_closing = false;
    if (*p == '/') {
        is_closing = true;
        p++;
    }

    // Must have a tag name starting with ASCII letter
    if (!isalpha((unsigned char)*p)) {
        return HtmlBlockType::NONE;
    }

    // Extract tag name
    const char* tag_start = p;
    while (isalnum((unsigned char)*p) || *p == '-') {
        p++;
    }
    size_t tag_len = p - tag_start;

    // Check if valid tag name terminator
    if (!is_tag_name_end(*p)) {
        return HtmlBlockType::NONE;
    }

    // Create lowercase copy of tag name for comparison
    char tag_name[32];
    if (tag_len >= sizeof(tag_name)) tag_len = sizeof(tag_name) - 1;
    for (size_t i = 0; i < tag_len; i++) {
        tag_name[i] = tolower((unsigned char)tag_start[i]);
    }
    tag_name[tag_len] = '\0';

    // Type 1: pre, script, style, textarea (opening tag only)
    if (!is_closing) {
        for (int i = 0; type1_tags[i]; i++) {
            if (strcmp(tag_name, type1_tags[i]) == 0) {
                return HtmlBlockType::TYPE_1;
            }
        }
    }

    // Type 6: Block-level tags (opening or closing)
    for (int i = 0; type6_tags[i]; i++) {
        if (strcmp(tag_name, type6_tags[i]) == 0) {
            return HtmlBlockType::TYPE_6;
        }
    }

    // Type 7: Complete open/close tag on single line
    // Must be a syntactically valid tag with only whitespace following
    // Start from after '<'
    const char* tag_end = try_parse_complete_tag(p - (is_closing ? 0 : 0));
    // Actually we need to start from right after '<'
    // p is already past the optional /, and at the tag name start
    // Let's restart from after '<'
    const char* from_lt = line;
    while (*from_lt == ' ' && from_lt - line < 3) from_lt++;
    if (*from_lt == '<') {
        from_lt++;  // past '<'
        tag_end = try_parse_complete_tag(from_lt);
        if (tag_end) {
            // Check if rest of line is only whitespace
            const char* rest = tag_end;
            while (*rest == ' ' || *rest == '\t') rest++;
            if (*rest == '\0' || *rest == '\n' || *rest == '\r') {
                return HtmlBlockType::TYPE_7;
            }
        }
    }

    return HtmlBlockType::NONE;
}

/**
 * is_html_block_start - Check if line starts an HTML block
 *
 * @param line The line to check
 * @return true if this line starts an HTML block
 */
bool is_html_block_start(const char* line) {
    HtmlBlockType type = detect_html_block_type(line);
    log_debug("is_html_block_start: line='%s' type=%d", line, (int)type);
    return type != HtmlBlockType::NONE;
}

/**
 * html_block_can_interrupt_paragraph - Check if line starts an HTML block that can interrupt a paragraph
 *
 * Per CommonMark spec, only HTML block types 1-6 can interrupt a paragraph.
 * Type 7 (complete open/closing tag on single line) cannot interrupt paragraphs.
 *
 * @param line The line to check
 * @return true if this line starts an HTML block that can interrupt a paragraph
 */
bool html_block_can_interrupt_paragraph(const char* line) {
    HtmlBlockType type = detect_html_block_type(line);
    // Types 1-6 can interrupt paragraphs, type 7 cannot
    return type >= HtmlBlockType::TYPE_1 && type <= HtmlBlockType::TYPE_6;
}

// ============================================================================
// HTML Block End Condition Checking
// ============================================================================

/**
 * check_html_block_end - Check if line ends an HTML block of given type
 *
 * @param line Current line
 * @param type Block type
 * @param next_is_blank Whether the next line is blank
 * @return true if this line ends the HTML block
 */
bool check_html_block_end(const char* line, HtmlBlockType type, bool next_is_blank) {
    switch (type) {
        case HtmlBlockType::TYPE_1:
            // Ends when line contains </pre>, </script>, </style>, or </textarea>
            return line_contains_ci(line, "</pre>") ||
                   line_contains_ci(line, "</script>") ||
                   line_contains_ci(line, "</style>") ||
                   line_contains_ci(line, "</textarea>");

        case HtmlBlockType::TYPE_2:
            // Ends when line contains -->
            return line_contains(line, "-->");

        case HtmlBlockType::TYPE_3:
            // Ends when line contains ?>
            return line_contains(line, "?>");

        case HtmlBlockType::TYPE_4:
            // Ends when line contains >
            return strchr(line, '>') != nullptr;

        case HtmlBlockType::TYPE_5:
            // Ends when line contains ]]>
            return line_contains(line, "]]>");

        case HtmlBlockType::TYPE_6:
        case HtmlBlockType::TYPE_7:
            // Ends when followed by a blank line
            return next_is_blank;

        default:
            return false;
    }
}

// ============================================================================
// HTML Block Parser
// ============================================================================

/**
 * parse_html_block - Parse a raw HTML block
 *
 * Parses the HTML block and feeds it to the shared HTML5 parser.
 * Creates an html-block element with the raw content for output.
 *
 * @param parser The markup parser
 * @param line The starting line
 * @return Item containing the HTML element
 */
Item parse_html_block(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    HtmlBlockType block_type = detect_html_block_type(line);
    if (block_type == HtmlBlockType::NONE) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create a raw HTML element
    // Use "html-block" tag that the CommonMark formatter expects
    Element* html_elem = create_element(parser, "html-block");
    if (!html_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Collect all lines of the HTML block
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Check if next line is blank (for type 6 and 7 end conditions)
        bool next_is_blank = false;
        if (parser->current_line + 1 < parser->line_count) {
            next_is_blank = is_blank(parser->lines[parser->current_line + 1]);
        } else {
            // End of document counts as blank for type 6/7
            next_is_blank = true;
        }

        // Add current line to content
        if (sb->length > 0) {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, current);
        parser->current_line++;

        // Check if this line ends the block
        // For types 1-5, check if the current line contains the end marker
        // For types 6-7, check if followed by blank line
        if (block_type == HtmlBlockType::TYPE_6 || block_type == HtmlBlockType::TYPE_7) {
            if (next_is_blank) {
                break;
            }
        } else {
            if (check_html_block_end(current, block_type, next_is_blank)) {
                break;
            }
        }
    }

    // Feed HTML content to the shared HTML5 parser
    // This accumulates all HTML into a single DOM tree
    if (sb->length > 0) {
        parser->parseHtmlFragment(sb->str->chars);
    }

    // Create content string for the raw HTML element
    // (preserves original content for output formats that need it)
    String* content = parser->builder.createString(sb->str->chars, sb->length);
    Item content_item = {.item = s2it(content)};
    list_push((List*)html_elem, content_item);
    increment_element_content_length(html_elem);

    return Item{.item = (uint64_t)html_elem};
}

} // namespace markup
} // namespace lambda
