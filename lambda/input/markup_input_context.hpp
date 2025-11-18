#pragma once

#include "input_context.hpp"
#include "markup-parser.h"

namespace lambda {

/**
 * Specialized InputContext for markup parsing
 *
 * Extends InputContext with state management for lightweight markup languages
 * (Markdown, reStructuredText, Textile, MediaWiki, Org-mode, AsciiDoc).
 * Wraps the existing MarkupParser C structure for backwards compatibility
 * while providing a modern C++ interface integrated with MarkBuilder.
 *
 * Usage:
 *   ParseConfig config = {MARKUP_MARKDOWN, "github", false};
 *   MarkupInputContext ctx(input, source, len, config);
 *   Item doc = ctx.parseDocument();
 */
class MarkupInputContext : public InputContext {
public:
    /**
     * Create markup input context with source tracking and configuration
     *
     * @param input Input stream for markup content
     * @param source Source code buffer
     * @param len Source length in bytes
     * @param config Parser configuration (format, flavor, strictness)
     */
    MarkupInputContext(Input* input, const char* source, size_t len, ParseConfig config)
        : InputContext(input, source, len)
        , parser_(new MarkupParser(input, config))
    {
        if (!parser_) {
            addError("Failed to create markup parser", 0, 0);
        }
    }

    /**
     * Create markup input context without source tracking
     *
     * @param input Input stream for markup content
     * @param config Parser configuration
     */
    MarkupInputContext(Input* input, ParseConfig config)
        : InputContext(input)
        , parser_(new MarkupParser(input, config))
    {
        if (!parser_) {
            addError("Failed to create markup parser", 0, 0);
        }
    }

    /**
     * Destructor - cleans up markup parser
     */
    ~MarkupInputContext() {
        if (parser_) {
            delete parser_;
        }
    }

    // Non-copyable
    MarkupInputContext(const MarkupInputContext&) = delete;
    MarkupInputContext& operator=(const MarkupInputContext&) = delete;

    // Markup-specific methods

    /**
     * Get the markup format being parsed
     */
    MarkupFormat getFormat() const {
        return parser_ ? parser_->config.format : MARKUP_AUTO_DETECT;
    }

    /**
     * Get the markup flavor (e.g., "github", "commonmark")
     */
    const char* getFlavor() const {
        return parser_ ? parser_->config.flavor : nullptr;
    }

    /**
     * Check if parser is in strict mode
     */
    bool isStrictMode() const {
        return parser_ && parser_->config.strict_mode;
    }

    /**
     * Get current line number being parsed
     */
    int getCurrentLine() const {
        return parser_ ? parser_->current_line : 0;
    }

    /**
     * Get total line count
     */
    int getLineCount() const {
        return parser_ ? parser_->line_count : 0;
    }

    /**
     * Get line at specific index
     *
     * @param index Line index (0-based)
     * @return Line content or nullptr if out of bounds
     */
    const char* getLine(int index) const {
        if (!parser_ || index < 0 || index >= parser_->line_count) {
            return nullptr;
        }
        return parser_->lines[index];
    }

    /**
     * Get current line being parsed
     */
    const char* getCurrentLineText() const {
        return getLine(parser_ ? parser_->current_line : -1);
    }

    /**
     * Advance to next line
     *
     * @return true if there's another line, false if at end
     */
    bool nextLine() {
        if (!parser_ || parser_->current_line >= parser_->line_count - 1) {
            return false;
        }
        parser_->current_line++;
        return true;
    }

    /**
     * Check if there are more lines to parse
     */
    bool hasMoreLines() const {
        return parser_ && parser_->current_line < parser_->line_count - 1;
    }

    /**
     * Reset parser state (keeps configuration)
     */
    void resetState() {
        if (parser_) {
            parser_->resetState();
        }
    }

    // List state management

    /**
     * Get current list nesting depth
     */
    int getListDepth() const {
        return parser_ ? parser_->state.list_depth : 0;
    }

    /**
     * Check if currently parsing a list
     */
    bool isInList() const {
        return parser_ && parser_->state.list_depth > 0;
    }

    /**
     * Get list marker at specified depth
     *
     * @param depth List nesting depth (0 = outermost)
     * @return List marker character or '\0' if out of bounds
     */
    char getListMarker(int depth) const {
        if (!parser_ || depth < 0 || depth >= parser_->state.list_depth) {
            return '\0';
        }
        return parser_->state.list_markers[depth];
    }

    /**
     * Get list indentation level at specified depth
     */
    int getListLevel(int depth) const {
        if (!parser_ || depth < 0 || depth >= parser_->state.list_depth) {
            return 0;
        }
        return parser_->state.list_levels[depth];
    }

    // Block state management

    /**
     * Check if currently in code block
     */
    bool isInCodeBlock() const {
        return parser_ && parser_->state.in_code_block;
    }

    /**
     * Get code fence character (` or ~)
     */
    char getCodeFenceChar() const {
        return parser_ ? parser_->state.code_fence_char : '\0';
    }

    /**
     * Get code fence length (3 or more)
     */
    int getCodeFenceLength() const {
        return parser_ ? parser_->state.code_fence_length : 0;
    }

    /**
     * Check if currently in math block
     */
    bool isInMathBlock() const {
        return parser_ && parser_->state.in_math_block;
    }

    /**
     * Get math block delimiter
     */
    const char* getMathDelimiter() const {
        return parser_ ? parser_->state.math_delimiter : nullptr;
    }

    /**
     * Check if currently in blockquote
     */
    bool isInQuoteBlock() const {
        return parser_ && parser_->state.in_quote_block;
    }

    /**
     * Get blockquote nesting depth
     */
    int getQuoteDepth() const {
        return parser_ ? parser_->state.quote_depth : 0;
    }

    // Table state management

    /**
     * Check if currently parsing table
     */
    bool isInTable() const {
        return parser_ && parser_->state.in_table;
    }

    /**
     * Get number of table columns
     */
    int getTableColumns() const {
        return parser_ ? parser_->state.table_columns : 0;
    }

    /**
     * Get current header level (0 if not in header)
     */
    int getHeaderLevel() const {
        return parser_ ? parser_->state.header_level : 0;
    }

    /**
     * Access underlying markup parser (for advanced use)
     *
     * @return Pointer to C MarkupParser structure
     */
    MarkupParser* markupParser() const {
        return parser_;
    }

private:
    MarkupParser* parser_;
};

} // namespace lambda
