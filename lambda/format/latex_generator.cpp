// latex_generator.cpp - Implementation of base LaTeX generator
// Translates latex.js generator.ls to C++

#include "latex_generator.hpp"
#include "../../lib/log.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace lambda {

// =============================================================================
// Length Implementation
// =============================================================================

std::string Length::toCss() const {
    std::stringstream ss;
    ss << value << unit;
    return ss.str();
}

Length Length::parse(const std::string& str) {
    // Parse "12pt", "1.5em", etc.
    size_t i = 0;
    
    // Skip whitespace
    while (i < str.length() && std::isspace(str[i])) i++;
    
    // Parse number
    size_t start = i;
    while (i < str.length() && (std::isdigit(str[i]) || str[i] == '.' || str[i] == '-')) i++;
    
    if (i == start) {
        return Length::zero();  // No number found
    }
    
    double val = std::stod(str.substr(start, i - start));
    
    // Skip whitespace
    while (i < str.length() && std::isspace(str[i])) i++;
    
    // Parse unit
    std::string unit = str.substr(i);
    if (unit.empty()) {
        unit = "pt";  // Default unit
    }
    
    return Length(val, unit);
}

// =============================================================================
// LatexGenerator Implementation
// =============================================================================

LatexGenerator::LatexGenerator(Pool* pool, HtmlWriter* writer)
    : pool_(pool), writer_(writer) {
    
    // Initialize standard counters and lengths
    initStandardCounters();
    initStandardLengths();
    
    // Start with one group on the stack (document level)
    group_stack_.push_back(GroupState());
}

// =============================================================================
// Counter Operations (translate generator.ls lines 110-220)
// =============================================================================

void LatexGenerator::initStandardCounters() {
    // latex.js generator.ls lines 110-145 - standard LaTeX counters
    
    // Document structure counters
    newCounter("part");
    newCounter("chapter");
    newCounter("section", "chapter");
    newCounter("subsection", "section");
    newCounter("subsubsection", "subsection");
    newCounter("paragraph", "subsubsection");
    newCounter("subparagraph", "paragraph");
    
    // List counters
    newCounter("enumi");       // enumerate level 1
    newCounter("enumii");      // enumerate level 2
    newCounter("enumiii");     // enumerate level 3
    newCounter("enumiv");      // enumerate level 4
    
    // Float counters
    newCounter("figure", "chapter");
    newCounter("table", "chapter");
    
    // Footnote and equation counters
    newCounter("footnote", "chapter");
    newCounter("mpfootnote");
    newCounter("equation", "chapter");
    
    // Page counter
    newCounter("page");
    
    // List depth counter
    newCounter("@listdepth");
    
    // TOC depth counter
    newCounter("tocdepth");
    setCounter("tocdepth", 3);  // Default: show up to subsubsection
    
    // Section numbering depth
    newCounter("secnumdepth");
    setCounter("secnumdepth", 3);  // Default: number up to subsubsection
}

void LatexGenerator::newCounter(const std::string& name, const std::string& parent) {
    // latex.js generator.ls lines 147-156
    
    if (counters_.find(name) != counters_.end()) {
        log_warn("Counter '%s' already exists, redefining", name.c_str());
    }
    
    Counter counter;
    counter.value = 0;
    counter.parent = parent;
    
    counters_[name] = counter;
    
    // Register this counter as a child of its parent
    if (!parent.empty()) {
        if (counters_.find(parent) != counters_.end()) {
            counters_[parent].children.push_back(name);
        }
    }
}

void LatexGenerator::stepCounter(const std::string& name) {
    // latex.js generator.ls lines 158-165
    
    if (counters_.find(name) == counters_.end()) {
        log_error("stepCounter: counter '%s' not found", name.c_str());
        return;
    }
    
    // Increment counter
    counters_[name].value++;
    
    // Reset all child counters recursively
    for (const auto& child : counters_[name].children) {
        resetCounterRecursive(child);
    }
}

void LatexGenerator::setCounter(const std::string& name, int value) {
    // latex.js generator.ls lines 167-171
    
    if (counters_.find(name) == counters_.end()) {
        log_error("setCounter: counter '%s' not found", name.c_str());
        return;
    }
    
    counters_[name].value = value;
}

void LatexGenerator::addToCounter(const std::string& name, int delta) {
    // latex.js generator.ls lines 173-177
    
    if (counters_.find(name) == counters_.end()) {
        log_error("addToCounter: counter '%s' not found", name.c_str());
        return;
    }
    
    counters_[name].value += delta;
}

int LatexGenerator::getCounter(const std::string& name) const {
    // latex.js generator.ls line 179
    
    auto it = counters_.find(name);
    if (it == counters_.end()) {
        log_error("getCounter: counter '%s' not found", name.c_str());
        return 0;
    }
    
    return it->second.value;
}

bool LatexGenerator::hasCounter(const std::string& name) const {
    return counters_.find(name) != counters_.end();
}

void LatexGenerator::resetCounterRecursive(const std::string& name) {
    // Helper for stepCounter - reset counter and all descendants
    
    if (counters_.find(name) == counters_.end()) {
        return;
    }
    
    // Reset this counter
    counters_[name].value = 0;
    
    // Reset all children recursively
    for (const auto& child : counters_[name].children) {
        resetCounterRecursive(child);
    }
}

std::string LatexGenerator::formatArabic(int value) const {
    // latex.js symbols.ls - arabic numerals
    return std::to_string(value);
}

std::string LatexGenerator::formatRoman(int value, bool uppercase) const {
    // latex.js symbols.ls - roman numerals
    // Simple implementation for values 1-3999
    
    if (value <= 0 || value >= 4000) {
        return std::to_string(value);  // Fallback to arabic
    }
    
    static const char* const roman_upper[] = {
        "", "M", "MM", "MMM"
    };
    static const char* const roman_hundreds_upper[] = {
        "", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM"
    };
    static const char* const roman_tens_upper[] = {
        "", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC"
    };
    static const char* const roman_ones_upper[] = {
        "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"
    };
    
    static const char* const roman_lower[] = {
        "", "m", "mm", "mmm"
    };
    static const char* const roman_hundreds_lower[] = {
        "", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm"
    };
    static const char* const roman_tens_lower[] = {
        "", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc"
    };
    static const char* const roman_ones_lower[] = {
        "", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix"
    };
    
    std::stringstream ss;
    
    if (uppercase) {
        ss << roman_upper[value / 1000]
           << roman_hundreds_upper[(value % 1000) / 100]
           << roman_tens_upper[(value % 100) / 10]
           << roman_ones_upper[value % 10];
    } else {
        ss << roman_lower[value / 1000]
           << roman_hundreds_lower[(value % 1000) / 100]
           << roman_tens_lower[(value % 100) / 10]
           << roman_ones_lower[value % 10];
    }
    
    return ss.str();
}

std::string LatexGenerator::formatAlph(int value, bool uppercase) const {
    // latex.js symbols.ls - alphabetic numbering
    // a, b, c, ..., z, aa, ab, ...
    
    if (value <= 0) {
        return "";
    }
    
    std::string result;
    value--;  // Convert to 0-based
    
    do {
        char c = 'a' + (value % 26);
        if (uppercase) {
            c = 'A' + (value % 26);
        }
        result = c + result;
        value = value / 26;
    } while (value > 0);
    
    return result;
}

std::string LatexGenerator::formatFnSymbol(int value) const {
    // latex.js symbols.ls - footnote symbols
    static const char* const symbols[] = {
        "*", "†", "‡", "§", "¶", "‖", "**", "††", "‡‡"
    };
    
    if (value <= 0 || value > 9) {
        return std::to_string(value);  // Fallback to numbers
    }
    
    return symbols[value - 1];
}

std::string LatexGenerator::formatCounter(const std::string& name, const std::string& format) const {
    // Get counter value
    int value = getCounter(name);
    
    // Apply format
    if (format == "arabic" || format == "Arabic") {
        return formatArabic(value);
    } else if (format == "roman") {
        return formatRoman(value, false);
    } else if (format == "Roman") {
        return formatRoman(value, true);
    } else if (format == "alph") {
        return formatAlph(value, false);
    } else if (format == "Alph") {
        return formatAlph(value, true);
    } else if (format == "fnsymbol") {
        return formatFnSymbol(value);
    }
    
    // Default: arabic
    return formatArabic(value);
}

std::string LatexGenerator::macro(const std::string& counter_name) const {
    // latex.js generator.ls macro method
    // Returns formatted counter value (e.g., "1.2" for subsection)
    
    if (!hasCounter(counter_name)) {
        return "";
    }
    
    // For hierarchical counters, include parent numbers
    // e.g., subsection shows as "1.2" (chapter.section.subsection)
    std::stringstream ss;
    
    // Build hierarchy
    std::vector<std::string> hierarchy;
    std::string current = counter_name;
    
    // Trace back to root
    while (!current.empty() && hasCounter(current)) {
        hierarchy.insert(hierarchy.begin(), current);
        
        auto it = counters_.find(current);
        if (it != counters_.end() && !it->second.parent.empty()) {
            current = it->second.parent;
        } else {
            break;
        }
    }
    
    // Format as "1.2.3"
    for (size_t i = 0; i < hierarchy.size(); i++) {
        if (i > 0) ss << ".";
        ss << getCounter(hierarchy[i]);
    }
    
    return ss.str();
}

// =============================================================================
// Label/Reference Operations (translate generator.ls lines 222-280)
// =============================================================================

void LatexGenerator::setLabel(const std::string& name) {
    // latex.js generator.ls setLabel method
    
    labels_[name] = current_label_;
    
    log_debug("setLabel: '%s' -> anchor='%s', text='%s'", 
              name.c_str(), current_label_.id.c_str(), current_label_.text.c_str());
}

LabelInfo LatexGenerator::getLabel(const std::string& name) const {
    // latex.js generator.ls label method
    
    auto it = labels_.find(name);
    if (it == labels_.end()) {
        log_warn("getLabel: label '%s' not found", name.c_str());
        return LabelInfo();
    }
    
    return it->second;
}

bool LatexGenerator::hasLabel(const std::string& name) const {
    return labels_.find(name) != labels_.end();
}

void LatexGenerator::setCurrentLabel(const std::string& anchor, const std::string& text) {
    // latex.js generator.ls currentlabel property
    
    current_label_.id = anchor;
    current_label_.text = text;
    current_label_.page = getCounter("page");
    
    log_debug("setCurrentLabel: anchor='%s', text='%s', page=%d", 
              anchor.c_str(), text.c_str(), current_label_.page);
}

std::string LatexGenerator::generateAnchorId(const std::string& prefix) {
    // Generate unique anchor ID
    std::stringstream ss;
    ss << prefix << "-" << (++label_id_counter_);
    return ss.str();
}

// =============================================================================
// Length Operations (translate generator.ls lines 282-390)
// =============================================================================

void LatexGenerator::initStandardLengths() {
    // latex.js generator.ls lines 282-320 - standard LaTeX lengths
    
    // Page dimensions
    newLength("paperwidth", Length(210, "mm"));
    newLength("paperheight", Length(297, "mm"));
    newLength("textwidth", Length(345, "pt"));
    newLength("textheight", Length(550, "pt"));
    
    // Margins
    newLength("oddsidemargin", Length(0, "pt"));
    newLength("evensidemargin", Length(0, "pt"));
    newLength("topmargin", Length(0, "pt"));
    
    // Paragraph spacing
    newLength("parindent", Length(15, "pt"));
    newLength("parskip", Length(0, "pt"));
    newLength("baselineskip", Length(12, "pt"));
    
    // List spacing
    newLength("topsep", Length(8, "pt"));
    newLength("itemsep", Length(4, "pt"));
    newLength("parsep", Length(4, "pt"));
    
    // Box dimensions
    newLength("fboxsep", Length(3, "pt"));
    newLength("fboxrule", Length(0.4, "pt"));
}

void LatexGenerator::newLength(const std::string& name, const Length& value) {
    // latex.js generator.ls newLength method
    
    if (lengths_.find(name) != lengths_.end()) {
        log_warn("Length '%s' already exists, redefining", name.c_str());
    }
    
    lengths_[name] = value;
}

void LatexGenerator::setLength(const std::string& name, const Length& value) {
    // latex.js generator.ls setLength method
    
    if (lengths_.find(name) == lengths_.end()) {
        log_error("setLength: length '%s' not found", name.c_str());
        return;
    }
    
    lengths_[name] = value;
}

Length LatexGenerator::getLength(const std::string& name) const {
    // latex.js generator.ls getLength method
    
    auto it = lengths_.find(name);
    if (it == lengths_.end()) {
        log_error("getLength: length '%s' not found", name.c_str());
        return Length::zero();
    }
    
    return it->second;
}

bool LatexGenerator::hasLength(const std::string& name) const {
    return lengths_.find(name) != lengths_.end();
}

// =============================================================================
// Group/Scope Operations (translate generator.ls lines 392-450)
// =============================================================================

void LatexGenerator::enterGroup() {
    // latex.js generator.ls enterGroup method
    
    // Push a new group state, copying current state
    GroupState new_state;
    if (!group_stack_.empty()) {
        new_state = group_stack_.back();  // Copy current state
    }
    
    group_stack_.push_back(new_state);
    
    log_debug("enterGroup: depth=%zu", group_stack_.size());
}

void LatexGenerator::exitGroup() {
    // latex.js generator.ls exitGroup method
    
    if (group_stack_.size() <= 1) {
        log_error("exitGroup: cannot exit document-level group");
        return;
    }
    
    group_stack_.pop_back();
    
    log_debug("exitGroup: depth=%zu", group_stack_.size());
}

FontContext& LatexGenerator::currentFont() {
    if (group_stack_.empty()) {
        log_error("currentFont: group stack is empty");
        static FontContext default_font;
        return default_font;
    }
    
    return group_stack_.back().font;
}

std::string LatexGenerator::currentAlignment() const {
    if (group_stack_.empty()) {
        return "";
    }
    
    return group_stack_.back().alignment;
}

void LatexGenerator::setAlignment(const std::string& align) {
    if (group_stack_.empty()) {
        log_error("setAlignment: group stack is empty");
        return;
    }
    
    group_stack_.back().alignment = align;
    log_debug("setAlignment: %s", align.c_str());
}

// =============================================================================
// Document Structure (translate generator.ls lines 343-380)
// =============================================================================

void LatexGenerator::startSection(const std::string& level, bool starred,
                                  const std::string& toc_title, const std::string& title) {
    // latex.js generator.ls startsection method
    // This is a virtual method that will be overridden by subclasses
    
    log_debug("startSection: level=%s, starred=%d, title=%s", 
              level.c_str(), starred, title.c_str());
    
    // Default implementation does nothing
    // Subclasses (HtmlGenerator) will override this
}

void LatexGenerator::startList() {
    // latex.js generator.ls startlist method
    
    stepCounter("@listdepth");
    list_depth_ = getCounter("@listdepth");
    
    if (list_depth_ > 6) {
        log_error("startList: too deeply nested (depth=%d)", list_depth_);
    }
    
    log_debug("startList: depth=%d", list_depth_);
}

void LatexGenerator::endList() {
    // latex.js generator.ls endlist method
    
    if (list_depth_ <= 0) {
        log_error("endList: not in a list");
        return;
    }
    
    setCounter("@listdepth", list_depth_ - 1);
    list_depth_ = getCounter("@listdepth");
    
    log_debug("endList: depth=%d", list_depth_);
}

// =============================================================================
// Utility Methods
// =============================================================================

bool LatexGenerator::isBlockLevel(const char* tag) const {
    // latex.js html-generator.ls isBlockLevel
    // Check if HTML tag is block-level element
    
    static const char* const block_tags[] = {
        "address", "blockquote", "body", "center", "dir", "div", "dl",
        "fieldset", "form", "h1", "h2", "h3", "h4", "h5", "h6",
        "hr", "isindex", "menu", "noframes", "noscript", "ol", "p",
        "pre", "table", "ul", "dd", "dt", "frameset", "li", "tbody",
        "td", "tfoot", "th", "thead", "tr", "html",
        nullptr
    };
    
    if (!tag) return false;
    
    for (int i = 0; block_tags[i]; i++) {
        if (strcmp(tag, block_tags[i]) == 0) {
            return true;
        }
    }
    
    return false;
}

} // namespace lambda
