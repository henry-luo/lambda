// latex_generator.hpp - Base generator class for LaTeX processing
// Translates latex.js generator.ls to C++
// Manages document state, counters, labels, lengths, and group scoping

#ifndef LATEX_GENERATOR_HPP
#define LATEX_GENERATOR_HPP

#include "html_writer.hpp"
#include "../../lib/mempool.h"
#include <string>
#include <map>
#include <vector>
#include <memory>

namespace lambda {

// Forward declarations
class HtmlWriter;

// =============================================================================
// Counter System (latex.js generator.ls lines 110-220)
// =============================================================================

struct Counter {
    int value = 0;
    std::string parent;  // Parent counter (for automatic reset)
    std::vector<std::string> children;  // Child counters to reset
};

// =============================================================================
// Label/Reference System (latex.js generator.ls lines 222-280)
// =============================================================================

struct LabelInfo {
    std::string id;          // Anchor ID (e.g., "sec-1")
    std::string text;        // Reference text (e.g., "1.2")
    int page = 1;           // Page number (for \pageref)
};

// =============================================================================
// Length System (latex.js generator.ls lines 282-390)
// =============================================================================

// LaTeX length with unit conversion to CSS
struct Length {
    double value;
    std::string unit;  // pt, em, ex, cm, mm, in, pc
    
    Length() : value(0.0), unit("pt") {}
    Length(double v, const std::string& u) : value(v), unit(u) {}
    
    static Length zero() { return Length(0.0, "pt"); }
    
    // Convert to CSS string
    std::string toCss() const;
    
    // Parse from string (e.g., "12pt", "1.5em")
    static Length parse(const std::string& str);
};

// =============================================================================
// Font Context (for group scoping)
// =============================================================================

enum class FontSeries { Normal, Bold };
// Note: ExplicitUpright is used when toggling from italic to upright (e.g., \em in italic context)
// It produces <span class="up"> unlike default Upright which produces no span
enum class FontShape { Upright, Italic, Slanted, SmallCaps, ExplicitUpright };
enum class FontFamily { Roman, SansSerif, Typewriter };
enum class FontSize { 
    Tiny, ScriptSize, FootnoteSize, Small, 
    NormalSize, Large, Large2, Large3, Huge, Huge2 
};

struct FontContext {
    FontSeries series = FontSeries::Normal;
    FontShape shape = FontShape::Upright;
    FontFamily family = FontFamily::Roman;
    FontSize size = FontSize::NormalSize;
    bool em_active = false;  // \em toggling state
    
    FontContext() = default;
};

// =============================================================================
// Group State (for scoping)
// =============================================================================

struct GroupState {
    FontContext font;
    std::string alignment;  // "centering", "raggedright", "raggedleft"
    std::map<std::string, Length> local_lengths;
    
    GroupState() = default;
};

// =============================================================================
// LatexGenerator - Base generator class
// Translates latex.js generator.ls
// =============================================================================

class LatexGenerator {
protected:
    Pool* pool_;
    HtmlWriter* writer_;
    
    // Document state
    std::string document_class_;
    std::string document_title_;
    std::string document_author_;
    std::string document_date_;
    bool in_document_ = false;
    
    // Counter system (latex.js generator.ls lines 110-220)
    std::map<std::string, Counter> counters_;
    std::map<std::string, std::vector<std::string>> counter_resets_;  // parent → children
    
    // Label/reference system (latex.js generator.ls lines 222-280)
    std::map<std::string, LabelInfo> labels_;
    LabelInfo current_label_;
    int label_id_counter_ = 0;
    
    // Length system (latex.js generator.ls lines 282-390)
    std::map<std::string, Length> lengths_;
    
    // Group/scope stack (latex.js generator.ls lines 392-450)
    std::vector<GroupState> group_stack_;
    
    // List depth tracking
    int list_depth_ = 0;
    
public:
    LatexGenerator(Pool* pool, HtmlWriter* writer);
    virtual ~LatexGenerator() = default;
    
    // Access to writer
    HtmlWriter* writer() { return writer_; }
    
    // ==========================================================================
    // Counter Operations (latex.js generator.ls lines 110-220)
    // ==========================================================================
    
    // Create a new counter with optional parent
    // latex.js: newCounter: (c, p) !->
    void newCounter(const std::string& name, const std::string& parent = "");
    
    // Step counter (increment and reset children)
    // latex.js: stepCounter: (c) !->
    void stepCounter(const std::string& name);
    
    // Set counter value
    // latex.js: setCounter: (c, v) !->
    void setCounter(const std::string& name, int value);
    
    // Add to counter value
    // latex.js: addToCounter: (c, v) !->
    void addToCounter(const std::string& name, int delta);
    
    // Get counter value
    // latex.js: counter: (c) ->
    int getCounter(const std::string& name) const;
    
    // Check if counter exists
    bool hasCounter(const std::string& name) const;
    
    // Format counter as arabic numerals (1, 2, 3, ...)
    // latex.js: in symbols.ls
    std::string formatArabic(int value) const;
    
    // Format counter as lowercase roman numerals (i, ii, iii, ...)
    // latex.js: in symbols.ls
    std::string formatRoman(int value, bool uppercase = false) const;
    
    // Format counter as alphabetic (a, b, c, ... z, aa, ab, ...)
    // latex.js: in symbols.ls
    std::string formatAlph(int value, bool uppercase = false) const;
    
    // Format counter as footnote symbol (*, †, ‡, §, ¶, ...)
    // latex.js: in symbols.ls
    std::string formatFnSymbol(int value) const;
    
    // Get formatted counter value using specified format
    // latex.js: macro: (c) ->
    std::string formatCounter(const std::string& name, const std::string& format) const;
    
    // ==========================================================================
    // Label/Reference Operations (latex.js generator.ls lines 222-280)
    // ==========================================================================
    
    // Set label with current counter context
    // latex.js: setLabel: (label) !->
    void setLabel(const std::string& name);
    
    // Get label information
    // latex.js: label: (name) ->
    LabelInfo getLabel(const std::string& name) const;
    
    // Check if label exists
    bool hasLabel(const std::string& name) const;
    
    // Set current label context (called by \refstepcounter, sections)
    // latex.js: currentlabel property
    void setCurrentLabel(const std::string& anchor, const std::string& text);
    
    // Generate unique anchor ID
    // latex.js: generate anchor ID from counter
    std::string generateAnchorId(const std::string& prefix = "ref");
    
    // ==========================================================================
    // Length Operations (latex.js generator.ls lines 282-390)
    // ==========================================================================
    
    // Create a new length variable
    // latex.js: newLength: (l) !->
    void newLength(const std::string& name, const Length& value = Length::zero());
    
    // Set length value
    // latex.js: setLength: (l, v) !->
    void setLength(const std::string& name, const Length& value);
    
    // Get length value
    // latex.js: getLength: (l) ->
    Length getLength(const std::string& name) const;
    
    // Check if length exists
    bool hasLength(const std::string& name) const;
    
    // ==========================================================================
    // Group/Scope Operations (latex.js generator.ls lines 392-450)
    // ==========================================================================
    
    // Enter a new group scope
    // latex.js: enterGroup: !->
    void enterGroup();
    
    // Exit current group scope
    // latex.js: exitGroup: !->
    void exitGroup();
    
    // Get current group nesting depth (1 = document level, 2+ = inside explicit groups)
    size_t groupDepth() const { return group_stack_.size(); }
    
    // Get current font context
    FontContext& currentFont();
    
    // Get current alignment mode
    std::string currentAlignment() const;
    
    // Set alignment for current group
    void setAlignment(const std::string& align);
    
    // ==========================================================================
    // Document Structure (latex.js generator.ls lines 343-380)
    // ==========================================================================
    
    // Start a section (chapter, section, subsection, etc.)
    // latex.js: startsection: (sec, level, s, toc, ttl) ->
    virtual void startSection(const std::string& level, bool starred, 
                             const std::string& toc_title, const std::string& title);
    
    // List management
    // latex.js: startlist: -> and endlist: !->
    void startList();
    void endList();
    int getListDepth() const { return list_depth_; }
    
    // ==========================================================================
    // Utility Methods
    // ==========================================================================
    
    // Get formatted counter macro (e.g., "1.2" for section counter)
    // latex.js: macro: (counter_name) ->
    std::string macro(const std::string& counter_name) const;
    
    // Check if HTML tag is block-level
    // latex.js: isBlockLevel in html-generator.ls
    bool isBlockLevel(const char* tag) const;
    
protected:
    // Initialize standard LaTeX counters
    void initStandardCounters();
    
    // Initialize standard LaTeX lengths
    void initStandardLengths();
    
    // Reset counter and all descendants recursively
    void resetCounterRecursive(const std::string& name);
};

} // namespace lambda

#endif // LATEX_GENERATOR_HPP
