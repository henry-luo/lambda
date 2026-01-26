/**
 * format_adapter.hpp - Abstract interface for format-specific behavior
 *
 * Each markup format implements this interface to provide its detection rules
 * and delimiter specifications. The shared parsers use these adapters to handle
 * format differences without scattered if-else chains.
 *
 * The adapter pattern enables:
 * - Shared block/inline parsers that work for all formats
 * - Format-specific detection rules in isolated, testable units
 * - Easy addition of new formats without modifying core parsing logic
 */
#ifndef FORMAT_ADAPTER_HPP
#define FORMAT_ADAPTER_HPP

#include "markup_common.hpp"

namespace lambda {
namespace markup {

/**
 * FormatAdapter - Abstract interface for format-specific behavior
 *
 * Each format (Markdown, RST, Wiki, etc.) implements this to provide:
 * - Block element detection (headers, lists, code blocks, etc.)
 * - Inline element delimiters (emphasis, links, etc.)
 * - Format-specific feature flags
 *
 * Actual parsing is done by shared functions using these rules.
 */
class FormatAdapter {
public:
    virtual ~FormatAdapter() = default;

    // ========================================================================
    // Format Identification
    // ========================================================================

    /**
     * Get the format type this adapter handles
     */
    virtual Format format() const = 0;

    /**
     * Get human-readable format name
     */
    virtual const char* name() const = 0;

    /**
     * Get common file extensions for this format (null-terminated array)
     * Example: {".md", ".markdown", nullptr}
     */
    virtual const char* const* extensions() const = 0;

    // ========================================================================
    // Block Detection
    // ========================================================================

    /**
     * Detect if line is a header
     *
     * @param line Current line content
     * @param next_line Next line (for underline-style headers), may be null
     * @return HeaderInfo with detection results
     */
    virtual HeaderInfo detectHeader(const char* line, const char* next_line) = 0;

    /**
     * Detect if line is a list item
     *
     * @param line Current line content
     * @return ListItemInfo with detection results
     */
    virtual ListItemInfo detectListItem(const char* line) = 0;

    /**
     * Detect if line starts a code fence/block
     *
     * @param line Current line content
     * @return CodeFenceInfo with detection results
     */
    virtual CodeFenceInfo detectCodeFence(const char* line) = 0;

    /**
     * Check if line closes a code fence
     *
     * @param line Current line content
     * @param open_info The CodeFenceInfo from when block was opened
     * @return true if this line closes the code block
     */
    virtual bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) = 0;

    /**
     * Detect if line is a blockquote
     *
     * @param line Current line content
     * @return BlockquoteInfo with detection results
     */
    virtual BlockquoteInfo detectBlockquote(const char* line) = 0;

    /**
     * Detect if line starts a table
     *
     * @param line Current line content
     * @param next_line Next line (for header separator detection), may be null
     * @return true if this starts a table
     */
    virtual bool detectTable(const char* line, const char* next_line) = 0;

    /**
     * Detect if line is a thematic break / horizontal rule
     *
     * @param line Current line content
     * @return true if line is a thematic break
     */
    virtual bool detectThematicBreak(const char* line) = 0;

    /**
     * Detect if line is an indented code block line
     *
     * @param line Current line content
     * @param content_start Output: where code content starts
     * @return true if line is indented code
     */
    virtual bool detectIndentedCode(const char* line, const char** content_start) {
        // Default: 4+ spaces = indented code (Markdown style)
        int spaces = count_leading_spaces(line);
        if (spaces >= 4 && !is_blank_line(line)) {
            if (content_start) *content_start = line + 4;
            return true;
        }
        return false;
    }

    /**
     * Detect if content starts with metadata block (frontmatter)
     *
     * @param content Document content
     * @return true if document starts with metadata
     */
    virtual bool detectMetadata(const char* content) {
        return false; // Override in formats that support frontmatter
    }

    // ========================================================================
    // Inline Detection
    // ========================================================================

    /**
     * Get emphasis delimiter specifications for this format
     *
     * @return Array of DelimiterSpec (null-terminated or use count)
     */
    virtual const DelimiterSpec* emphasisDelimiters() const = 0;

    /**
     * Get number of emphasis delimiter specifications
     */
    virtual size_t emphasisDelimiterCount() const = 0;

    /**
     * Detect a link at the current position
     *
     * @param pos Current position in text
     * @return LinkInfo with detection results
     */
    virtual LinkInfo detectLink(const char* pos) = 0;

    /**
     * Detect an image at the current position
     *
     * @param pos Current position in text
     * @return LinkInfo with detection results (reused for images)
     */
    virtual LinkInfo detectImage(const char* pos) = 0;

    /**
     * Check if character at position is escaped
     *
     * @param text Full text
     * @param pos Position to check
     * @return true if character is escaped (preceded by backslash)
     */
    virtual bool isEscaped(const char* text, const char* pos) {
        if (pos <= text) return false;
        int backslashes = 0;
        const char* p = pos - 1;
        while (p >= text && *p == '\\') {
            backslashes++;
            p--;
        }
        return (backslashes % 2) == 1;
    }

    // ========================================================================
    // Feature Support
    // ========================================================================

    /**
     * Check if format supports a specific feature
     *
     * Common features:
     * - "task_lists" - [ ] and [x] checkboxes
     * - "tables" - pipe tables
     * - "footnotes" - [^ref] footnotes
     * - "strikethrough" - ~~text~~
     * - "math" - $...$ and $$...$$ math
     * - "emoji" - :emoji: shortcodes
     * - "autolink" - automatic URL detection
     * - "smart_quotes" - typographic quotes
     * - "definition_lists" - term: definition
     * - "abbreviations" - *[abbr]: definition
     */
    virtual bool supportsFeature(const char* feature) const {
        (void)feature;
        return false;
    }

    /**
     * Get the escape character for this format
     */
    virtual char escapeChar() const {
        return '\\';
    }

    /**
     * Get characters that can be escaped in this format
     */
    virtual const char* escapableChars() const {
        return "\\`*_{}[]()#+-.!"; // Markdown default
    }

protected:
    FormatAdapter() = default;
};

// ============================================================================
// Format Registry
// ============================================================================

/**
 * FormatRegistry - Factory and lookup for format adapters
 *
 * Provides singleton access to format adapters and automatic format detection.
 */
class FormatRegistry {
public:
    /**
     * Get adapter for a specific format
     *
     * @param format The format type
     * @return Pointer to adapter (never null for valid formats)
     */
    static FormatAdapter* getAdapter(Format format);

    /**
     * Detect format from content and/or filename
     *
     * @param content Document content (for heuristic detection)
     * @param filename Optional filename (for extension-based detection)
     * @return Detected format adapter
     */
    static FormatAdapter* detectAdapter(const char* content, const char* filename = nullptr);

    /**
     * Detect format from filename extension
     *
     * @param filename Filename to check
     * @return Format type, or AUTO_DETECT if unknown
     */
    static Format detectFromFilename(const char* filename);

    /**
     * Detect format from content heuristics
     *
     * @param content Document content
     * @return Format type based on content patterns
     */
    static Format detectFromContent(const char* content);

    /**
     * Register a custom format adapter
     *
     * @param adapter Adapter to register (registry takes ownership)
     */
    static void registerAdapter(FormatAdapter* adapter);

private:
    FormatRegistry() = default;
};

// ============================================================================
// Adapter Registration Macro
// ============================================================================

/**
 * Helper macro to register a format adapter at static initialization time
 *
 * Usage:
 *   REGISTER_FORMAT_ADAPTER(MarkdownAdapter)
 */
#define REGISTER_FORMAT_ADAPTER(AdapterClass) \
    static AdapterClass s_##AdapterClass##_instance; \
    static struct AdapterClass##_Registrar { \
        AdapterClass##_Registrar() { \
            FormatRegistry::registerAdapter(&s_##AdapterClass##_instance); \
        } \
    } s_##AdapterClass##_registrar

} // namespace markup
} // namespace lambda

#endif // FORMAT_ADAPTER_HPP
