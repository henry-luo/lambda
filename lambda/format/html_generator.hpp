// html_generator.hpp - HTML generator for LaTeX documents
// Translates latex.js html-generator.ls to C++
//
// TODO: Migration to lib/ data structures needed (remove std::*)
// Current std::* usages:
// - std::string: 20+ occurrences in method parameters/returns (use const char* or StrBuf*)
// - std::vector<CaptureState>: capture_stack_ (use fixed array with max depth ~16)
// - std::vector<TableState>: table_stack_ (use fixed array with max depth ~8)
// - std::vector<ListState>: list_stack_ (use fixed array with max depth ~8)
// - std::vector<FloatState>: float_stack_ (use fixed array with max depth ~4)
// - std::vector<TocEntry>: toc_entries_ (use ArrayList)
// - std::vector<std::string> in TableState::column_specs (use fixed array of char*)
// - std::map: None currently
//
// Migration strategy:
// 1. Replace std::string params/returns with const char* (most are already C-strings)
// 2. Replace std::vector<T> stacks with fixed arrays: T stack_[MAX]; int stack_top_;
// 3. Replace std::vector<TocEntry> with ArrayList (variable-size collection)
// 4. Update methods like length(), getFontClass(), getFontStyle() to return StrBuf* or write to buffer
// 5. Update startCapture()/endCapture() to work with StrBuf* capture buffers

#pragma once

#include "latex_generator.hpp"
#include "html_writer.hpp"
#include "../lambda-data.hpp"
#include <string>
#include <vector>
#include <map>

namespace lambda {

// =============================================================================
// HtmlGenerator - Extends LatexGenerator with HTML output capabilities
// Translates latex.js html-generator.ls (lines 1-600)
// =============================================================================

class HtmlGenerator : public LatexGenerator {
public:
    // Constructor
    HtmlGenerator(Pool* pool, HtmlWriter* writer);
    virtual ~HtmlGenerator() = default;

    // =============================================================================
    // Element Creation Methods (html-generator.ls lines 50-150)
    // =============================================================================

    // Create HTML element with tag and optional attributes
    // Returns the element (for text mode: writes immediately; for node mode: returns Element item)
    void create(const char* tag, const char* attrs = nullptr);

    // Create element with children
    void createWithChildren(const char* tag, const char* attrs = nullptr);

    // Close current element
    void closeElement();

    // Trim trailing whitespace from output (for paragraph handling)
    void trimTrailingWhitespace();

    // Check if output has trailing whitespace
    bool hasTrailingWhitespace() const;

    // Create heading element (h1-h6)
    void h(int level, const char* attrs = nullptr);

    // Create span element
    void span(const char* attrs = nullptr);

    // Create span element with style attribute
    void spanWithStyle(const char* style_value);

    // Create span element with class and style
    void spanWithClassAndStyle(const char* css_class, const char* style_value);

    // Create div element
    void div(const char* attrs = nullptr);

    // Create div element with style only
    void divWithStyle(const char* style_value);

    // Create div element with class and style
    void divWithClassAndStyle(const char* css_class, const char* style_value);

    // Create paragraph element
    void p(const char* attrs = nullptr);

    // Write text content
    void text(const char* content);

    // Write text with CSS class
    void textWithClass(const char* content, const char* css_class);

    // Write raw HTML (not escaped)
    void rawHtml(const char* html);

    // =============================================================================
    // Length and Style Methods (html-generator.ls lines 152-200)
    // =============================================================================

    // Convert LaTeX length to CSS
    std::string length(const Length& len) const;
    std::string length(const std::string& length_name) const;

    // Apply font style to element
    void applyFontStyle(const FontContext& font);

    // Get CSS class for font context
    std::string getFontClass(const FontContext& font) const;

    // Get CSS style string for font context
    std::string getFontStyle(const FontContext& font) const;

    // =============================================================================
    // Document Structure Methods (html-generator.ls lines 202-300)
    // =============================================================================

    // Override from LatexGenerator - create section element
    void startSection(const std::string& level, bool starred,
                     const std::string& toc_title, const std::string& title) override;

    // Create section heading
    void createSectionHeading(const std::string& level, const std::string& number,
                             const std::string& title, const std::string& anchor);

    // Create table of contents entry
    void addTocEntry(const std::string& level, const std::string& number,
                    const std::string& title, const std::string& anchor);

    // =============================================================================
    // List Methods (html-generator.ls lines 302-380)
    // =============================================================================

    // Start list environment (itemize, enumerate, description)
    void startItemize(const char* alignment = nullptr);
    void startEnumerate(const char* alignment = nullptr);
    void startDescription();

    // End list environment
    void endItemize();
    void endEnumerate();
    void endDescription();

    // Create list item
    void createItem(const char* label = nullptr);

    // Create list item with pre-rendered HTML label (for custom labels with formatting)
    void createItemWithHtmlLabel(const char* html_label);

    // End list item (closes <p> and <li> for itemize/enumerate)
    void endItem();

    // Handle paragraph break within list item - closes <p> and opens new <p>
    void itemParagraphBreak();

    // Get enumerate label for current depth and format
    std::string getEnumerateLabel(int depth) const;

    // =============================================================================
    // Environment Methods (html-generator.ls lines 382-450)
    // =============================================================================

    // Quote environments
    void startQuote();
    void endQuote();

    void startQuotation();
    void endQuotation();

    void startVerse();
    void endVerse();

    // Alignment environments
    void startCenter();
    void endCenter();

    void startFlushLeft();
    void endFlushLeft();

    void startFlushRight();
    void endFlushRight();

    // Verbatim environment
    void startVerbatim();
    void endVerbatim();
    void verbatimText(const char* text);

    // =============================================================================
    // Table Methods (html-generator.ls lines 452-520)
    // =============================================================================

    // Table environment
    void startTable(const char* position = nullptr);
    void endTable();

    // Tabular environment
    void startTabular(const char* column_spec);
    void endTabular();

    // Table row
    void startRow();
    void endRow();

    // Table cell
    void startCell(const char* align = nullptr);
    void endCell();

    // Horizontal line
    void hline();

    // =============================================================================
    // Float Methods (html-generator.ls lines 522-580)
    // =============================================================================

    // Figure environment
    void startFigure(const char* position = nullptr);
    void endFigure();

    // Caption
    void startCaption();
    void endCaption();

    // Graphics
    void includegraphics(const char* filename, const char* options = nullptr);

    // =============================================================================
    // Math Methods (html-generator.ls lines 582-640)
    // =============================================================================

    // Inline math
    void startInlineMath();
    void startInlineMathWithSource(const char* latex_source);
    void endInlineMath();

    // Display math
    void startDisplayMath();
    void startDisplayMathWithSource(const char* latex_source);
    void endDisplayMath();

    // Equation environment
    void startEquation(bool starred = false);
    void endEquation(bool starred = false);

    // Math content
    void mathContent(const char* content);

    // =============================================================================
    // Special Character Methods (html-generator.ls lines 642-700)
    // =============================================================================

    // Ligatures
    void ligature(const char* chars);

    // Accents and diacritics
    void accent(const char* type, const char* base);

    // Special symbols
    void symbol(const char* name);

    // Space commands
    void space(const char* type);

    // Line break
    void lineBreak(bool newpage = false);

    // Zero-width space marker (for proper word boundaries after commands/groups)
    void writeZWS();

    // =============================================================================
    // Reference Methods (html-generator.ls lines 702-750)
    // =============================================================================

    // Create hyperlink
    void hyperlink(const char* url, const char* text = nullptr);

    // Create internal reference
    void ref(const char* label_name);
    void pageref(const char* label_name);

    // Create citation
    void cite(const char* key);

    // Create footnote
    void footnote(const char* text);

    // =============================================================================
    // Utility Methods
    // =============================================================================

    // Escape HTML special characters (if not using HtmlWriter)
    static std::string escapeHtml(const std::string& text);

    // Get HTML tag for sectioning level
    static const char* getSectionTag(const std::string& level);

    // Get heading level (1-6) for section type
    static int getHeadingLevel(const std::string& level);

    // Check if in math mode
    bool inMathMode() const { return math_mode_; }

    // Check if in verbatim mode
    bool inVerbatimMode() const { return verbatim_mode_; }

    // =============================================================================
    // Capture Mode (for rendering content to a string)
    // Supports nested captures using a stack
    // =============================================================================

    // Start capturing output to a string buffer (can be nested)
    void startCapture();

    // Stop capturing and return the captured HTML string
    std::string endCapture();

    // Check if currently in capture mode
    bool inCaptureMode() const { return !capture_stack_.empty(); }

protected:
    // State tracking
    bool math_mode_;
    bool verbatim_mode_;

    // Capture mode state - supports nesting via stack
    struct CaptureState {
        HtmlWriter* previous_writer;   // Writer before this capture started
        HtmlWriter* capture_writer;    // The capture writer created
    };
    std::vector<CaptureState> capture_stack_;

    // Legacy capture mode state (kept for backward compatibility, unused with new stack)
    HtmlWriter* capture_writer_;      // Temporary writer for capture mode
    HtmlWriter* original_writer_;     // Saved writer during capture

    // Table state
    struct TableState {
        std::vector<std::string> column_specs;
        int current_column;
        bool in_header_row;
    };
    std::vector<TableState> table_stack_;

    // List state
    struct ListState {
        std::string type;  // "itemize", "enumerate", "description"
        int item_count;
        std::string alignment;  // "centering", "raggedright", "raggedleft", or empty
    };
    std::vector<ListState> list_stack_;

    // Float state
    struct FloatState {
        std::string type;  // "figure", "table"
        std::string position;
        bool has_caption;
        std::string anchor;  // anchor ID for label/ref
    };
    std::vector<FloatState> float_stack_;

    // TOC entries
    struct TocEntry {
        std::string level;
        std::string number;
        std::string title;
        std::string anchor;
    };
    std::vector<TocEntry> toc_entries_;

    // Helper methods
    void pushTableState(const std::vector<std::string>& cols);
    void popTableState();
    TableState& currentTable();

    void pushListState(const std::string& type);
    void popListState();
    ListState& currentList();

    void pushFloatState(const std::string& type, const char* position);
    void popFloatState();
    FloatState& currentFloat();

    // Parse column specification for tabular
    std::vector<std::string> parseColumnSpec(const char* spec);
};

} // namespace lambda
