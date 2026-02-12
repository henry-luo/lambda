/**
 * markup_common.hpp - Shared types and utilities for unified markup parsing
 *
 * This header provides common definitions used across all markup format parsers.
 * It defines unified block/inline types, delimiter specifications, and detection
 * result structures that enable code sharing between format-specific adapters.
 */
#ifndef MARKUP_COMMON_HPP
#define MARKUP_COMMON_HPP

#include "../../lambda-data.hpp"
#include "../input-context.hpp"
#include "../parse_error.hpp"
#include <cstring>
#include <algorithm>

namespace lambda {
namespace markup {

// Forward declarations
class MarkupParser;
class FormatAdapter;

// ============================================================================
// Escape Character Handling (CommonMark §2.4)
// ============================================================================

/**
 * ESCAPABLE_CHARS - Characters that can be escaped with a backslash
 *
 * CommonMark specifies that any ASCII punctuation character can be escaped.
 * The backslash before a punctuation character is treated as an escape.
 */
static constexpr const char* ESCAPABLE_CHARS =
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

/**
 * is_escapable - Check if a character can be escaped with backslash
 *
 * @param c The character to check
 * @return true if the character is an ASCII punctuation that can be escaped
 */
inline bool is_escapable(char c) {
    // ascii punctuation range check for performance
    if (c < '!' || c > '~') return false;
    return strchr(ESCAPABLE_CHARS, c) != nullptr;
}

/**
 * is_ascii_punctuation - Check if a character is ASCII punctuation
 *
 * CommonMark defines ASCII punctuation as any of:
 * !, ", #, $, %, &, ', (, ), *, +, ,, -, ., /, :, ;, <, =, >, ?, @, [, \, ], ^, _, `, {, |, }, ~
 */
inline bool is_ascii_punctuation(char c) {
    return is_escapable(c);
}

// ============================================================================
// Format Identification
// ============================================================================

/**
 * Format - Supported markup format types
 *
 * Each format has its own adapter that provides detection rules and delimiter
 * specifications. The shared parsers use these rules to handle format differences.
 */
enum class Format {
    MARKDOWN,       // CommonMark, GFM, etc.
    RST,            // reStructuredText
    WIKI,           // MediaWiki
    TEXTILE,        // Textile
    ORG,            // Org-mode
    ASCIIDOC,       // AsciiDoc
    MAN,            // Unix man pages (troff)
    TYPST,          // Typst markup language
    AUTO_DETECT     // Detect from content/filename
};

/**
 * Flavor - Format-specific variants
 */
enum class Flavor {
    DEFAULT,
    // Markdown flavors
    COMMONMARK,
    GITHUB,         // GitHub Flavored Markdown
    GITLAB,
    PANDOC,
    // Wiki flavors
    MEDIAWIKI,
    DOKUWIKI,
    TIDDLYWIKI,
    // RST flavors
    SPHINX,
    // Org flavors
    ORGMODE,
    // AsciiDoc flavors
    ASCIIDOCTOR
};

// ============================================================================
// Block Element Types
// ============================================================================

/**
 * BlockType - Unified block element classification
 *
 * All formats map their block elements to these types. This enables shared
 * parsing logic while preserving format-specific detection rules.
 */
enum class BlockType {
    PARAGRAPH,          // Normal text paragraph
    HEADER,             // h1-h6 headers
    LIST_ITEM,          // Individual list item
    ORDERED_LIST,       // Numbered list container
    UNORDERED_LIST,     // Bullet list container
    DEFINITION_LIST,    // Term-definition list
    CODE_BLOCK,         // Fenced or indented code
    QUOTE,              // Blockquote
    TABLE,              // Table container
    TABLE_ROW,          // Table row
    MATH,               // Display math block
    DIVIDER,            // Horizontal rule / thematic break
    COMMENT,            // Comment block
    FOOTNOTE_DEF,       // Footnote definition
    DIRECTIVE,          // RST directive, Org #+BEGIN, Man .XX
    METADATA,           // YAML frontmatter, Org properties
    RAW_HTML,           // Pass-through HTML block
    BLANK               // Blank line(s)
};

// ============================================================================
// Inline Element Types
// ============================================================================

/**
 * InlineType - Unified inline element classification
 */
enum class InlineType {
    TEXT,               // Plain text
    BOLD,               // Strong emphasis
    ITALIC,             // Regular emphasis
    BOLD_ITALIC,        // Combined emphasis
    CODE,               // Inline code
    LINK,               // Hyperlink
    IMAGE,              // Image
    MATH,               // Inline math
    STRIKETHROUGH,      // Strikethrough text
    SUPERSCRIPT,        // Superscript
    SUBSCRIPT,          // Subscript
    UNDERLINE,          // Underline (some formats)
    EMOJI,              // Emoji shortcode
    FOOTNOTE_REF,       // Footnote reference
    CITATION,           // Citation reference
    CITE,               // Inline citation (Textile ??)
    SPAN,               // Generic span with modifiers (Textile %)
    TEMPLATE,           // Wiki template, variable expansion
    LINE_BREAK,         // Hard line break
    ESCAPE              // Escaped character
};

// ============================================================================
// Delimiter Specification
// ============================================================================

/**
 * DelimiterSpec - Defines opening/closing delimiters for inline elements
 *
 * Format adapters provide arrays of these to configure emphasis parsing.
 * The shared inline parser uses these to detect and parse inline elements.
 */
struct DelimiterSpec {
    const char* open;       // Opening delimiter (e.g., "**")
    const char* close;      // Closing delimiter (e.g., "**")
    InlineType type;        // Element type to create
    bool nestable;          // Whether content can have nested inline elements
    bool flanking_rules;    // Use CommonMark flanking delimiter rules
};

// ============================================================================
// Detection Result Structures
// ============================================================================

/**
 * HeaderInfo - Result of header detection
 */
struct HeaderInfo {
    int level;              // 1-6 (0 if invalid)
    const char* text_start; // Start of header text content
    const char* text_end;   // End of header text content
    bool uses_underline;    // Setext-style (consumes extra line)
    bool valid;             // Whether detection succeeded

    HeaderInfo() : level(0), text_start(nullptr), text_end(nullptr),
                   uses_underline(false), valid(false) {}
};

/**
 * ListItemInfo - Result of list item detection
 */
struct ListItemInfo {
    char marker;            // '-', '*', '+', '#', etc.
    int indent;             // Leading whitespace (spaces)
    int number;             // For ordered lists (0 for unordered)
    const char* text_start; // Start of item text
    const char* marker_end; // End of marker (for continuation detection)
    bool is_ordered;        // Ordered vs unordered
    bool is_task;           // Task list item [ ] or [x]
    bool task_checked;      // If task, whether checked
    bool valid;             // Whether detection succeeded

    ListItemInfo() : marker(0), indent(0), number(0), text_start(nullptr),
                     marker_end(nullptr), is_ordered(false), is_task(false),
                     task_checked(false), valid(false) {}
};

/**
 * CodeFenceInfo - Result of code fence detection
 */
struct CodeFenceInfo {
    char fence_char;        // '`' or '~' or '#' (for Org #+BEGIN_SRC)
    int fence_length;       // Number of fence characters
    int indent;             // Leading indentation
    const char* info_string; // Language identifier
    size_t info_length;     // Length of info string
    bool valid;             // Whether detection succeeded

    CodeFenceInfo() : fence_char(0), fence_length(0), indent(0),
                      info_string(nullptr), info_length(0), valid(false) {}
};

/**
 * LinkInfo - Result of link detection
 */
struct LinkInfo {
    const char* text_start;     // Link text start
    const char* text_end;       // Link text end
    const char* url_start;      // URL start
    const char* url_end;        // URL end
    const char* title_start;    // Optional title start
    const char* title_end;      // Optional title end
    const char* end_pos;        // Position after entire link construct
    bool is_reference;          // Reference-style link [text][ref]
    const char* ref_start;      // Reference ID start (if reference-style)
    const char* ref_end;        // Reference ID end
    bool valid;                 // Whether detection succeeded

    LinkInfo() : text_start(nullptr), text_end(nullptr), url_start(nullptr),
                 url_end(nullptr), title_start(nullptr), title_end(nullptr),
                 end_pos(nullptr), is_reference(false), ref_start(nullptr),
                 ref_end(nullptr), valid(false) {}
};

/**
 * BlockquoteInfo - Result of blockquote detection
 */
struct BlockquoteInfo {
    int depth;              // Nesting level (number of > markers)
    const char* content_start; // Start of content after markers
    bool valid;

    BlockquoteInfo() : depth(0), content_start(nullptr), valid(false) {}
};

// ============================================================================
// Error Categories
// ============================================================================

/**
 * MarkupErrorCategory - Classification of parsing errors/warnings
 */
enum class MarkupErrorCategory {
    SYNTAX,           // Malformed syntax (e.g., invalid list marker)
    STRUCTURE,        // Improper nesting (e.g., unclosed list)
    REFERENCE,        // Unresolved link/footnote reference
    ENCODING,         // Character encoding issues
    UNCLOSED,         // Unclosed delimiter (e.g., missing **)
    UNEXPECTED,       // Unexpected token/character
    DEPRECATED,       // Deprecated syntax usage
    LIMIT_EXCEEDED    // Nesting depth, line length exceeded
};

/**
 * Get human-readable name for error category
 */
inline const char* category_name(MarkupErrorCategory cat) {
    switch (cat) {
        case MarkupErrorCategory::SYNTAX: return "syntax";
        case MarkupErrorCategory::STRUCTURE: return "structure";
        case MarkupErrorCategory::REFERENCE: return "reference";
        case MarkupErrorCategory::ENCODING: return "encoding";
        case MarkupErrorCategory::UNCLOSED: return "unclosed";
        case MarkupErrorCategory::UNEXPECTED: return "unexpected";
        case MarkupErrorCategory::DEPRECATED: return "deprecated";
        case MarkupErrorCategory::LIMIT_EXCEEDED: return "limit";
        default: return "unknown";
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Skip leading whitespace, return number of spaces (tabs = 4 spaces)
 */
inline int skip_whitespace(const char** pos) {
    int count = 0;
    while (**pos == ' ' || **pos == '\t') {
        count += (**pos == '\t') ? 4 : 1;
        (*pos)++;
    }
    return count;
}

/**
 * Count leading spaces (not converting tabs)
 */
inline int count_leading_spaces(const char* line) {
    int count = 0;
    while (*line == ' ') { count++; line++; }
    return count;
}

/**
 * Check if line is blank (only whitespace)
 */
inline bool is_blank_line(const char* line) {
    if (!line) return true;
    while (*line) {
        if (*line != ' ' && *line != '\t' && *line != '\r' && *line != '\n') {
            return false;
        }
        line++;
    }
    return true;
}

/**
 * Trim trailing whitespace from a string, return new length
 */
inline size_t trim_trailing(const char* start, size_t len) {
    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t' ||
                       start[len-1] == '\r' || start[len-1] == '\n')) {
        len--;
    }
    return len;
}

/**
 * Check if string starts with prefix
 */
inline bool starts_with(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

/**
 * Check if string starts with prefix (case-insensitive)
 */
inline bool starts_with_icase(const char* str, const char* prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix)) {
            return false;
        }
        str++;
        prefix++;
    }
    return true;
}

/**
 * Find closing delimiter, respecting nesting and escapes
 */
const char* find_closing_delimiter(const char* start, const char* delimiter,
                                   bool respect_escapes = true);

/**
 * Convert InlineType to HTML tag name
 */
inline const char* inline_type_to_tag(InlineType type) {
    switch (type) {
        case InlineType::BOLD: return "strong";
        case InlineType::ITALIC: return "em";
        case InlineType::BOLD_ITALIC: return "strong"; // Nested em inside
        case InlineType::CODE: return "code";
        case InlineType::STRIKETHROUGH: return "s";
        case InlineType::SUPERSCRIPT: return "sup";
        case InlineType::SUBSCRIPT: return "sub";
        case InlineType::UNDERLINE: return "u";
        case InlineType::LINK: return "a";
        case InlineType::IMAGE: return "img";
        case InlineType::MATH: return "math";
        case InlineType::EMOJI: return "span";
        case InlineType::FOOTNOTE_REF: return "sup";
        case InlineType::CITATION: return "cite";
        case InlineType::CITE: return "cite";
        case InlineType::SPAN: return "span";
        default: return "span";
    }
}

/**
 * Convert BlockType to element tag name
 */
inline const char* block_type_to_tag(BlockType type) {
    switch (type) {
        case BlockType::PARAGRAPH: return "p";
        case BlockType::HEADER: return "h1"; // Level added separately
        case BlockType::ORDERED_LIST: return "ol";
        case BlockType::UNORDERED_LIST: return "ul";
        case BlockType::LIST_ITEM: return "li";
        case BlockType::DEFINITION_LIST: return "dl";
        case BlockType::CODE_BLOCK: return "pre";
        case BlockType::QUOTE: return "blockquote";
        case BlockType::TABLE: return "table";
        case BlockType::TABLE_ROW: return "tr";
        case BlockType::MATH: return "math";
        case BlockType::DIVIDER: return "hr";
        case BlockType::COMMENT: return "comment";
        case BlockType::FOOTNOTE_DEF: return "footnote";
        case BlockType::DIRECTIVE: return "directive";
        case BlockType::METADATA: return "metadata";
        case BlockType::RAW_HTML: return "html";
        default: return "div";
    }
}

// ============================================================================
// Constants
// ============================================================================

constexpr int MAX_HEADER_LEVEL = 6;
constexpr int MAX_LIST_DEPTH = 10;
constexpr int MAX_QUOTE_DEPTH = 10;
constexpr int MAX_INLINE_NESTING = 20;
constexpr size_t MAX_LINE_LENGTH = 10000;

} // namespace markup
} // namespace lambda

#endif // MARKUP_COMMON_HPP
