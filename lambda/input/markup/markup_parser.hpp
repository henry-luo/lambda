/**
 * markup_parser.hpp - Core markup parser class definition
 *
 * This header defines the MarkupParser class that coordinates parsing
 * of lightweight markup formats. It extends InputContext to inherit
 * the MarkBuilder, error tracking, and source tracking infrastructure.
 *
 * The parser uses FormatAdapter instances to handle format-specific
 * detection and delegates actual parsing to shared block/inline parsers.
 */
#ifndef MARKUP_PARSER_HPP
#define MARKUP_PARSER_HPP

#include "markup_common.hpp"
#include "format_adapter.hpp"
#include "../input-context.hpp"
#include "../../mark_builder.hpp"
#include <string>

namespace lambda {
namespace markup {

/**
 * ParseConfig - Parser configuration options
 */
struct ParseConfig {
    Format format;          // Target format (or AUTO_DETECT)
    Flavor flavor;          // Format-specific variant
    bool strict_mode;       // Strict vs lenient parsing
    bool collect_metadata;  // Whether to parse frontmatter/properties
    bool resolve_refs;      // Whether to resolve link/footnote references

    ParseConfig()
        : format(Format::AUTO_DETECT)
        , flavor(Flavor::DEFAULT)
        , strict_mode(false)
        , collect_metadata(true)
        , resolve_refs(true)
    {}
};

/**
 * ParserState - Current parsing state
 */
struct ParserState {
    // List parsing state
    char list_markers[MAX_LIST_DEPTH];
    int list_levels[MAX_LIST_DEPTH];
    int list_depth;

    // Code block state
    bool in_code_block;
    CodeFenceInfo code_fence;

    // Math block state
    bool in_math_block;
    char math_delimiter[16];

    // Quote state
    bool in_quote;
    int quote_depth;

    // Table state
    bool in_table;
    int table_columns;

    // General state
    int header_level;
    bool in_paragraph;

    ParserState() { reset(); }

    void reset() {
        memset(list_markers, 0, sizeof(list_markers));
        memset(list_levels, 0, sizeof(list_levels));
        list_depth = 0;

        in_code_block = false;
        code_fence = CodeFenceInfo();

        in_math_block = false;
        memset(math_delimiter, 0, sizeof(math_delimiter));

        in_quote = false;
        quote_depth = 0;

        in_table = false;
        table_columns = 0;

        header_level = 0;
        in_paragraph = false;
    }
};

/**
 * MarkupParser - Main parser class for lightweight markup
 *
 * Extends InputContext to use MarkBuilder for creating Lambda data structures.
 * Uses FormatAdapter for format-specific detection rules.
 */
class MarkupParser : public InputContext {
public:
    // Configuration
    ParseConfig config;

    // Format adapter (owned by registry, not this class)
    FormatAdapter* adapter_;

    // Document lines
    char** lines;
    int line_count;
    int current_line;

    // Parsing state
    ParserState state;

    // ========================================================================
    // Construction / Destruction
    // ========================================================================

    /**
     * Construct parser with input and configuration
     */
    MarkupParser(Input* input, const ParseConfig& cfg = ParseConfig());

    /**
     * Destructor
     */
    ~MarkupParser();

    // Non-copyable
    MarkupParser(const MarkupParser&) = delete;
    MarkupParser& operator=(const MarkupParser&) = delete;

    // ========================================================================
    // Parsing Interface
    // ========================================================================

    /**
     * Parse content and return document Item
     */
    Item parseContent(const char* content);

    /**
     * Reset parser state for new document
     */
    void resetState();

    /**
     * Get current format adapter
     */
    FormatAdapter* adapter() const { return adapter_; }

    /**
     * Set format adapter (for format detection)
     */
    void setAdapter(FormatAdapter* adapter) { adapter_ = adapter; }

    // ========================================================================
    // Error Reporting
    // ========================================================================

    /**
     * Add a structured markup parsing error
     */
    void addMarkupError(MarkupErrorCategory category,
                        const std::string& message,
                        const std::string& hint = "");

    /**
     * Convenience: warn about unclosed delimiter
     */
    void warnUnclosed(const char* delimiter, size_t start_line);

    /**
     * Convenience: warn about invalid syntax
     */
    void warnInvalidSyntax(const char* construct, const char* expected);

    /**
     * Convenience: note about unresolved reference
     */
    void noteUnresolvedReference(const char* ref_type, const char* ref_id);

    // ========================================================================
    // Line Utilities
    // ========================================================================

    /**
     * Get current line content (or nullptr if past end)
     */
    const char* currentLine() const {
        if (current_line >= 0 && current_line < line_count) {
            return lines[current_line];
        }
        return nullptr;
    }

    /**
     * Get next line content (or nullptr if at end)
     */
    const char* nextLine() const {
        if (current_line + 1 >= 0 && current_line + 1 < line_count) {
            return lines[current_line + 1];
        }
        return nullptr;
    }

    /**
     * Peek at line at offset from current
     */
    const char* peekLine(int offset) const {
        int idx = current_line + offset;
        if (idx >= 0 && idx < line_count) {
            return lines[idx];
        }
        return nullptr;
    }

    /**
     * Advance to next line
     */
    void advanceLine() {
        if (current_line < line_count) {
            current_line++;
        }
    }

    /**
     * Check if at end of document
     */
    bool atEnd() const {
        return current_line >= line_count;
    }

    /**
     * Get current source location
     */
    SourceLocation location() const {
        return tracker.location();
    }

private:
    // Split content into lines
    void splitLines(const char* content);

    // Free line memory
    void freeLines();
};

// ============================================================================
// Block Parser Function Signatures
// ============================================================================

// These are implemented in block/*.cpp files

/**
 * Parse entire document, returns root element
 */
Item parse_document(MarkupParser* parser);

/**
 * Parse a single block element at current position
 */
Item parse_block_element(MarkupParser* parser);

/**
 * Detect block type at current line
 */
BlockType detect_block_type(MarkupParser* parser, const char* line);

// Individual block parsers
Item parse_header(MarkupParser* parser, const char* line);
Item parse_paragraph(MarkupParser* parser, const char* line);
Item parse_list_structure(MarkupParser* parser, int base_indent);
Item parse_code_block(MarkupParser* parser, const char* line);
Item parse_blockquote(MarkupParser* parser, const char* line);
Item parse_table(MarkupParser* parser, const char* line);
Item parse_math_block(MarkupParser* parser, const char* line);
Item parse_thematic_break(MarkupParser* parser);
Item parse_metadata(MarkupParser* parser);

// ============================================================================
// Inline Parser Function Signatures
// ============================================================================

// These are implemented in inline/*.cpp files

/**
 * Parse inline content within text
 */
Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * Parse emphasis (bold, italic, etc.)
 */
Item parse_emphasis(MarkupParser* parser, const char** text);

/**
 * Parse inline code span
 */
Item parse_code_span(MarkupParser* parser, const char** text);

/**
 * Parse link
 */
Item parse_link(MarkupParser* parser, const char** text);

/**
 * Parse image
 */
Item parse_image(MarkupParser* parser, const char** text);

} // namespace markup
} // namespace lambda

#endif // MARKUP_PARSER_HPP
