// html_generator.cpp - Implementation of HTML generator for LaTeX documents
// Translates latex.js html-generator.ls to C++

#include "html_generator.hpp"
#include "../../lib/log.h"
#include <sstream>
#include <algorithm>

namespace lambda {

// =============================================================================
// Constructor
// =============================================================================

HtmlGenerator::HtmlGenerator(Pool* pool, HtmlWriter* writer)
    : LatexGenerator(pool, writer), math_mode_(false), verbatim_mode_(false) {
    
    log_debug("HtmlGenerator created");
}

// =============================================================================
// Element Creation Methods (html-generator.ls lines 50-150)
// =============================================================================

void HtmlGenerator::create(const char* tag, const char* attrs) {
    // html-generator.ls create method - simple element
    writer_->openTag(tag, attrs);
    writer_->closeTag(tag);
}

void HtmlGenerator::createWithChildren(const char* tag, const char* classes) {
    // html-generator.ls create method - element with children
    // Note: attrs is treated as a class name, not raw attributes
    writer_->openTag(tag, classes);
}

void HtmlGenerator::closeElement() {
    // Close the most recently opened tag
    // Note: HtmlWriter tracks this internally
    writer_->closeTag(nullptr);  // nullptr means close current tag
}

void HtmlGenerator::h(int level, const char* attrs) {
    // html-generator.ls h method
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    
    char tag[4];
    snprintf(tag, sizeof(tag), "h%d", level);
    
    createWithChildren(tag, attrs);
}

void HtmlGenerator::span(const char* attrs) {
    // html-generator.ls span method
    createWithChildren("span", attrs);
}

void HtmlGenerator::spanWithStyle(const char* style_value) {
    // Create span with style attribute directly
    writer_->openTag("span", nullptr, nullptr, style_value);
}

void HtmlGenerator::spanWithClassAndStyle(const char* css_class, const char* style_value) {
    // Create span with both class and style attributes
    writer_->openTag("span", css_class, nullptr, style_value);
}

void HtmlGenerator::div(const char* attrs) {
    // html-generator.ls div method
    createWithChildren("div", attrs);
}

void HtmlGenerator::divWithClassAndStyle(const char* css_class, const char* style_value) {
    // Create div with both class and style attributes
    writer_->openTag("div", css_class, nullptr, style_value);
}

void HtmlGenerator::p(const char* attrs) {
    // html-generator.ls p method
    createWithChildren("p", attrs);
}

void HtmlGenerator::text(const char* content) {
    // html-generator.ls text method
    if (!content) return;
    
    // In verbatim mode, preserve all whitespace
    // In normal mode, HtmlWriter handles entity escaping
    writer_->writeText(content);
}

void HtmlGenerator::textWithClass(const char* content, const char* css_class) {
    // Create a span with the given class and text content
    writer_->openTag("span", css_class);
    writer_->writeText(content);
    writer_->closeTag("span");
}

// =============================================================================
// Length and Style Methods (html-generator.ls lines 152-200)
// =============================================================================

std::string HtmlGenerator::length(const Length& len) const {
    // html-generator.ls length method - convert Length to CSS
    return len.toCss();
}

std::string HtmlGenerator::length(const std::string& length_name) const {
    // html-generator.ls length method - lookup named length
    if (hasLength(length_name)) {
        return getLength(length_name).toCss();
    }
    
    log_warn("length: unknown length '%s'", length_name.c_str());
    return "0pt";
}

void HtmlGenerator::applyFontStyle(const FontContext& font) {
    // html-generator.ls applyStyle method
    // Apply font styling to current element
    
    std::string style = getFontStyle(font);
    if (!style.empty()) {
        writer_->writeAttribute("style", style.c_str());
    }
    
    std::string css_class = getFontClass(font);
    if (!css_class.empty()) {
        writer_->writeAttribute("class", css_class.c_str());
    }
}

std::string HtmlGenerator::getFontClass(const FontContext& font) const {
    // html-generator.ls getFontClass method
    std::stringstream ss;
    
    // Build CSS class from font properties
    switch (font.family) {
        case FontFamily::Roman: ss << "rm "; break;
        case FontFamily::SansSerif: ss << "sf "; break;
        case FontFamily::Typewriter: ss << "tt "; break;
    }
    
    switch (font.series) {
        case FontSeries::Bold: ss << "bf "; break;
        case FontSeries::Normal: break;
    }
    
    switch (font.shape) {
        case FontShape::Italic: ss << "it "; break;
        case FontShape::Slanted: ss << "sl "; break;
        case FontShape::SmallCaps: ss << "sc "; break;
        case FontShape::Upright: break;
    }
    
    std::string result = ss.str();
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();  // Remove trailing space
    }
    
    return result;
}

std::string HtmlGenerator::getFontStyle(const FontContext& font) const {
    // html-generator.ls getFontStyle method
    std::stringstream ss;
    
    // Font size
    const char* size_str = nullptr;
    switch (font.size) {
        case FontSize::Tiny: size_str = "0.5em"; break;
        case FontSize::ScriptSize: size_str = "0.7em"; break;
        case FontSize::FootnoteSize: size_str = "0.8em"; break;
        case FontSize::Small: size_str = "0.9em"; break;
        case FontSize::NormalSize: break;
        case FontSize::Large: size_str = "1.2em"; break;
        case FontSize::Large2: size_str = "1.44em"; break;
        case FontSize::Large3: size_str = "1.73em"; break;
        case FontSize::Huge: size_str = "2.07em"; break;
        case FontSize::Huge2: size_str = "2.49em"; break;
    }
    
    if (size_str) {
        ss << "font-size: " << size_str << ";";
    }
    
    return ss.str();
}

// =============================================================================
// Document Structure Methods (html-generator.ls lines 202-300)
// =============================================================================

void HtmlGenerator::startSection(const std::string& level, bool starred,
                                 const std::string& toc_title, const std::string& title) {
    // html-generator.ls startsection method
    
    log_debug("startSection: level=%s, starred=%d, title=%s", 
              level.c_str(), starred, title.c_str());
    
    // Get counter name for this section level
    std::string counter_name = level;
    
    // Step counter (unless starred)
    if (!starred && hasCounter(counter_name)) {
        stepCounter(counter_name);
    }
    
    // Get section number
    std::string number;
    if (!starred) {
        number = macro(counter_name);  // e.g., "1.2.3"
    }
    
    // Generate anchor ID
    std::string anchor = generateAnchorId(level);
    
    // Set label info for \label command
    std::string label_text = number;
    if (!title.empty()) {
        if (!number.empty()) {
            label_text += " " + title;
        } else {
            label_text = title;
        }
    }
    setCurrentLabel(anchor, label_text);
    
    // Add to TOC (unless starred)
    if (!starred) {
        addTocEntry(level, number, toc_title.empty() ? title : toc_title, anchor);
    }
    
    // Create section heading HTML
    createSectionHeading(level, number, title, anchor);
}

void HtmlGenerator::createSectionHeading(const std::string& level, const std::string& number,
                                         const std::string& title, const std::string& anchor) {
    // html-generator.ls createSectionHeading method
    
    int heading_level = getHeadingLevel(level);
    
    // Create heading element with anchor
    std::stringstream attrs;
    attrs << "id=\"" << anchor << "\"";
    
    h(heading_level, attrs.str().c_str());
    
    // Section number
    if (!number.empty()) {
        span("class=\"section-number\"");
        text(number.c_str());
        closeElement();  // span
        text(" ");
    }
    
    // Section title
    span("class=\"section-title\"");
    text(title.c_str());
    closeElement();  // span
    
    closeElement();  // h*
}

void HtmlGenerator::addTocEntry(const std::string& level, const std::string& number,
                               const std::string& title, const std::string& anchor) {
    // html-generator.ls addTocEntry method
    
    TocEntry entry;
    entry.level = level;
    entry.number = number;
    entry.title = title;
    entry.anchor = anchor;
    
    toc_entries_.push_back(entry);
    
    log_debug("addTocEntry: level=%s, number=%s, title=%s", 
              level.c_str(), number.c_str(), title.c_str());
}

// =============================================================================
// List Methods (html-generator.ls lines 302-380)
// =============================================================================

void HtmlGenerator::startItemize() {
    // html-generator.ls startItemize method
    
    LatexGenerator::startList();  // Update list depth counter
    pushListState("itemize");
    
    writer_->openTag("ul", "itemize");
}

void HtmlGenerator::endItemize() {
    // html-generator.ls endItemize method
    
    writer_->closeTag("ul");
    popListState();
    LatexGenerator::endList();
}

void HtmlGenerator::startEnumerate() {
    // html-generator.ls startEnumerate method
    
    LatexGenerator::startList();
    pushListState("enumerate");
    
    writer_->openTag("ol", "enumerate");
}

void HtmlGenerator::endEnumerate() {
    // html-generator.ls endEnumerate method
    
    writer_->closeTag("ol");
    popListState();
    LatexGenerator::endList();
}

void HtmlGenerator::startDescription() {
    // html-generator.ls startDescription method
    
    LatexGenerator::startList();
    pushListState("description");
    
    writer_->openTag("dl", "description");
}

void HtmlGenerator::endDescription() {
    // html-generator.ls endDescription method
    
    writer_->closeTag("dl");
    popListState();
    LatexGenerator::endList();
}

void HtmlGenerator::createItem(const char* label) {
    // html-generator.ls createItem method
    
    if (list_stack_.empty()) {
        log_error("createItem: not in a list environment");
        return;
    }
    
    ListState& state = currentList();
    state.item_count++;
    
    if (state.type == "itemize") {
        writer_->openTag("li", nullptr);
    } else if (state.type == "enumerate") {
        // For enumerate, we can let HTML handle numbering, or use custom labels
        writer_->openTag("li", nullptr);
    } else if (state.type == "description") {
        if (label) {
            writer_->openTag("dt", nullptr);
            text(label);
            writer_->closeTag("dt");
        }
        writer_->openTag("dd", nullptr);
    }
}

std::string HtmlGenerator::getEnumerateLabel(int depth) const {
    // html-generator.ls getEnumerateLabel method
    // Get the label format for enumerate at given depth
    
    // Default LaTeX enumerate labels:
    // Level 1: 1., 2., 3., ... (arabic)
    // Level 2: (a), (b), (c), ... (alph)
    // Level 3: i., ii., iii., ... (roman)
    // Level 4: A., B., C., ... (Alph)
    
    std::string counter_name;
    std::string format;
    
    switch (depth) {
        case 1:
            counter_name = "enumi";
            format = "arabic";
            break;
        case 2:
            counter_name = "enumii";
            format = "alph";
            break;
        case 3:
            counter_name = "enumiii";
            format = "roman";
            break;
        case 4:
            counter_name = "enumiv";
            format = "Alph";
            break;
        default:
            return "";
    }
    
    if (hasCounter(counter_name)) {
        return formatCounter(counter_name, format);
    }
    
    return "";
}

// =============================================================================
// Environment Methods (html-generator.ls lines 382-450)
// =============================================================================

void HtmlGenerator::startQuote() {
    writer_->openTag("blockquote", "quote");
}

void HtmlGenerator::endQuote() {
    writer_->closeTag("blockquote");
}

void HtmlGenerator::startQuotation() {
    writer_->openTag("blockquote", "quotation");
}

void HtmlGenerator::endQuotation() {
    writer_->closeTag("blockquote");
}

void HtmlGenerator::startVerse() {
    writer_->openTag("div", "verse");
}

void HtmlGenerator::endVerse() {
    writer_->closeTag("div");
}

void HtmlGenerator::startCenter() {
    writer_->openTag("div", "center");
    setAlignment("center");
}

void HtmlGenerator::endCenter() {
    writer_->closeTag("div");
}

void HtmlGenerator::startFlushLeft() {
    writer_->openTag("div", "flushleft");
    setAlignment("left");
}

void HtmlGenerator::endFlushLeft() {
    writer_->closeTag("div");
}

void HtmlGenerator::startFlushRight() {
    writer_->openTag("div", "flushright");
    setAlignment("right");
}

void HtmlGenerator::endFlushRight() {
    writer_->closeTag("div");
}

void HtmlGenerator::startVerbatim() {
    verbatim_mode_ = true;
    writer_->openTag("pre", "verbatim");
}

void HtmlGenerator::endVerbatim() {
    writer_->closeTag("pre");
    verbatim_mode_ = false;
}

void HtmlGenerator::verbatimText(const char* text) {
    if (!verbatim_mode_) {
        log_warn("verbatimText: not in verbatim mode");
    }
    writer_->writeText(text);
}

// =============================================================================
// Table Methods (html-generator.ls lines 452-520)
// =============================================================================

void HtmlGenerator::startTable(const char* position) {
    pushFloatState("table", position);
    writer_->openTag("div", "table-float");
}

void HtmlGenerator::endTable() {
    writer_->closeTag("div");
    popFloatState();
}

void HtmlGenerator::startTabular(const char* column_spec) {
    // Parse column specification
    std::vector<std::string> cols = parseColumnSpec(column_spec);
    pushTableState(cols);
    
    writer_->openTag("table", "tabular");
}

void HtmlGenerator::endTabular() {
    writer_->closeTag("table");
    popTableState();
}

void HtmlGenerator::startRow() {
    if (table_stack_.empty()) {
        log_error("startRow: not in a table");
        return;
    }
    
    TableState& state = currentTable();
    state.current_column = 0;
    
    writer_->openTag("tr", nullptr);
}

void HtmlGenerator::endRow() {
    writer_->closeTag("tr");
}

void HtmlGenerator::startCell(const char* align) {
    if (table_stack_.empty()) {
        log_error("startCell: not in a table");
        return;
    }
    
    TableState& state = currentTable();
    
    // Get alignment from column spec if not specified
    std::string alignment = align ? align : "";
    if (alignment.empty() && state.current_column < state.column_specs.size()) {
        alignment = state.column_specs[state.current_column];
    }
    
    // Build attributes
    std::stringstream attrs;
    if (!alignment.empty()) {
        attrs << "class=\"" << alignment << "\"";
    }
    
    const char* tag = state.in_header_row ? "th" : "td";
    writer_->openTag(tag, attrs.str().c_str());
    
    state.current_column++;
}

void HtmlGenerator::endCell() {
    if (table_stack_.empty()) {
        log_error("endCell: not in a table");
        return;
    }
    
    TableState& state = currentTable();
    const char* tag = state.in_header_row ? "th" : "td";
    writer_->closeTag(tag);
}

void HtmlGenerator::hline() {
    // Create a horizontal line in table
    // In HTML, this is typically done with CSS borders
    // For now, just add a class to the current row
    writer_->writeAttribute("class", "hline");
}

// =============================================================================
// Float Methods (html-generator.ls lines 522-580)
// =============================================================================

void HtmlGenerator::startFigure(const char* position) {
    pushFloatState("figure", position);
    writer_->openTag("div", "figure-float");
}

void HtmlGenerator::endFigure() {
    writer_->closeTag("div");
    popFloatState();
}

void HtmlGenerator::startCaption() {
    if (float_stack_.empty()) {
        log_error("startCaption: not in a float environment");
        return;
    }
    
    FloatState& state = currentFloat();
    state.has_caption = true;
    
    // Step the appropriate counter
    if (state.type == "figure") {
        stepCounter("figure");
    } else if (state.type == "table") {
        stepCounter("table");
    }
    
    writer_->openTag("div", "caption");
    
    // Write caption label
    std::string label;
    if (state.type == "figure") {
        label = "Figure " + formatCounter("figure", "arabic");
    } else if (state.type == "table") {
        label = "Table " + formatCounter("table", "arabic");
    }
    
    if (!label.empty()) {
        span("class=\"caption-label\"");
        text(label.c_str());
        text(": ");
        closeElement();  // span
    }
}

void HtmlGenerator::endCaption() {
    writer_->closeTag("div");
}

void HtmlGenerator::includegraphics(const char* filename, const char* options) {
    // Create img element with raw attributes
    std::stringstream attrs;
    attrs << "src=\"" << filename << "\"";
    
    if (options) {
        attrs << " " << options;
    }
    
    writer_->openTagRaw("img", attrs.str().c_str());
    writer_->closeTag("img");
}

// =============================================================================
// Math Methods (html-generator.ls lines 582-640)
// =============================================================================

void HtmlGenerator::startInlineMath() {
    math_mode_ = true;
    writer_->openTag("span", "math inline");
}

void HtmlGenerator::endInlineMath() {
    writer_->closeTag("span");
    math_mode_ = false;
}

void HtmlGenerator::startDisplayMath() {
    math_mode_ = true;
    writer_->openTag("div", "math display");
}

void HtmlGenerator::endDisplayMath() {
    writer_->closeTag("div");
    math_mode_ = false;
}

void HtmlGenerator::startEquation(bool starred) {
    math_mode_ = true;
    
    if (!starred) {
        stepCounter("equation");
    }
    
    std::string attrs = starred ? "class=\"equation*\"" : "class=\"equation\"";
    writer_->openTag("div", attrs.c_str());
    
    // Equation number (if not starred)
    if (!starred) {
        std::string number = "(" + formatCounter("equation", "arabic") + ")";
        span("class=\"equation-number\"");
        text(number.c_str());
        closeElement();  // span
    }
}

void HtmlGenerator::endEquation(bool starred) {
    writer_->closeTag("div");
    math_mode_ = false;
}

void HtmlGenerator::mathContent(const char* content) {
    if (!math_mode_) {
        log_warn("mathContent: not in math mode");
    }
    
    // For now, just write the content directly
    // In a full implementation, this would be rendered using MathML or KaTeX
    writer_->writeText(content);
}

// =============================================================================
// Special Character Methods (html-generator.ls lines 642-700)
// =============================================================================

void HtmlGenerator::ligature(const char* chars) {
    // html-generator.ls ligature method
    // Common ligatures: ff, fi, fl, ffi, ffl
    
    if (strcmp(chars, "ff") == 0) {
        text("ﬀ");
    } else if (strcmp(chars, "fi") == 0) {
        text("ﬁ");
    } else if (strcmp(chars, "fl") == 0) {
        text("ﬂ");
    } else if (strcmp(chars, "ffi") == 0) {
        text("ﬃ");
    } else if (strcmp(chars, "ffl") == 0) {
        text("ﬄ");
    } else {
        text(chars);  // Fallback
    }
}

void HtmlGenerator::accent(const char* type, const char* base) {
    // html-generator.ls accent method
    // Combining diacritical marks
    
    text(base);
    
    // Add combining character
    if (strcmp(type, "acute") == 0) {
        text("\u0301");  // Combining acute accent
    } else if (strcmp(type, "grave") == 0) {
        text("\u0300");  // Combining grave accent
    } else if (strcmp(type, "circumflex") == 0) {
        text("\u0302");  // Combining circumflex
    } else if (strcmp(type, "tilde") == 0) {
        text("\u0303");  // Combining tilde
    } else if (strcmp(type, "umlaut") == 0) {
        text("\u0308");  // Combining diaeresis
    }
    // Add more as needed
}

void HtmlGenerator::symbol(const char* name) {
    // html-generator.ls symbol method
    // Special LaTeX symbols
    
    // This would lookup the symbol in a table
    // For now, just write the name
    text(name);
}

void HtmlGenerator::space(const char* type) {
    // html-generator.ls space method
    
    if (strcmp(type, "quad") == 0) {
        text("\u2003");  // Em space
    } else if (strcmp(type, "qquad") == 0) {
        text("\u2003\u2003");  // Two em spaces
    } else if (strcmp(type, "nbsp") == 0) {
        text("\u00A0");  // Non-breaking space
    } else if (strcmp(type, "thinspace") == 0) {
        text("\u2009");  // Thin space
    } else {
        text(" ");  // Regular space
    }
}

void HtmlGenerator::lineBreak(bool newpage) {
    if (newpage) {
        writer_->openTag("div", "page-break");
        writer_->closeTag("div");
    } else {
        // <br> is a self-closing tag
        writer_->writeSelfClosingTag("br", nullptr, nullptr);
    }
}

// =============================================================================
// Reference Methods (html-generator.ls lines 702-750)
// =============================================================================

void HtmlGenerator::hyperlink(const char* url, const char* text_content) {
    std::stringstream attrs;
    attrs << "href=\"" << url << "\"";
    
    writer_->openTag("a", attrs.str().c_str());
    
    if (text_content) {
        text(text_content);
    } else {
        text(url);
    }
    
    writer_->closeTag("a");
}

void HtmlGenerator::ref(const char* label_name) {
    if (hasLabel(label_name)) {
        LabelInfo info = getLabel(label_name);
        
        std::stringstream attrs;
        attrs << "href=\"#" << info.id << "\"";
        
        writer_->openTag("a", attrs.str().c_str());
        text(info.text.c_str());
        writer_->closeTag("a");
    } else {
        text("??");  // Unknown reference
        log_warn("ref: label '%s' not found", label_name);
    }
}

void HtmlGenerator::pageref(const char* label_name) {
    if (hasLabel(label_name)) {
        LabelInfo info = getLabel(label_name);
        
        std::stringstream attrs;
        attrs << "href=\"#" << info.id << "\"";
        
        writer_->openTag("a", attrs.str().c_str());
        text(std::to_string(info.page).c_str());
        writer_->closeTag("a");
    } else {
        text("??");
        log_warn("pageref: label '%s' not found", label_name);
    }
}

void HtmlGenerator::cite(const char* key) {
    // Create citation link
    std::stringstream attrs;
    attrs << "href=\"#cite-" << key << "\"";
    
    writer_->openTag("a", attrs.str().c_str());
    text("[");
    text(key);
    text("]");
    writer_->closeTag("a");
}

void HtmlGenerator::footnote(const char* text_content) {
    stepCounter("footnote");
    
    std::string number = formatCounter("footnote", "arabic");
    std::string anchor = "fn-" + number;
    
    // Create superscript footnote marker
    std::stringstream attrs;
    attrs << "href=\"#" << anchor << "\" class=\"footnote-ref\"";
    
    writer_->openTag("sup", nullptr);
    writer_->openTag("a", attrs.str().c_str());
    text(number.c_str());
    writer_->closeTag("a");
    writer_->closeTag("sup");
    
    // TODO: Store footnote text for rendering at end of page/document
}

// =============================================================================
// Utility Methods
// =============================================================================

std::string HtmlGenerator::escapeHtml(const std::string& text) {
    // html-generator.ls escapeHtml method
    std::stringstream ss;
    
    for (char c : text) {
        switch (c) {
            case '&': ss << "&amp;"; break;
            case '<': ss << "&lt;"; break;
            case '>': ss << "&gt;"; break;
            case '"': ss << "&quot;"; break;
            case '\'': ss << "&#39;"; break;
            default: ss << c; break;
        }
    }
    
    return ss.str();
}

const char* HtmlGenerator::getSectionTag(const std::string& level) {
    // Get HTML heading tag for section level
    int heading_level = getHeadingLevel(level);
    
    static char tag[4];
    snprintf(tag, sizeof(tag), "h%d", heading_level);
    return tag;
}

int HtmlGenerator::getHeadingLevel(const std::string& level) {
    // html-generator.ls getHeadingLevel method
    
    if (level == "part") return 1;
    if (level == "chapter") return 1;
    if (level == "section") return 2;
    if (level == "subsection") return 3;
    if (level == "subsubsection") return 4;
    if (level == "paragraph") return 5;
    if (level == "subparagraph") return 6;
    
    return 2;  // Default
}

// =============================================================================
// Helper Methods - State Management
// =============================================================================

void HtmlGenerator::pushTableState(const std::vector<std::string>& cols) {
    TableState state;
    state.column_specs = cols;
    state.current_column = 0;
    state.in_header_row = false;
    
    table_stack_.push_back(state);
}

void HtmlGenerator::popTableState() {
    if (!table_stack_.empty()) {
        table_stack_.pop_back();
    }
}

HtmlGenerator::TableState& HtmlGenerator::currentTable() {
    if (table_stack_.empty()) {
        log_error("currentTable: table stack is empty");
        static TableState default_state;
        return default_state;
    }
    return table_stack_.back();
}

void HtmlGenerator::pushListState(const std::string& type) {
    ListState state;
    state.type = type;
    state.item_count = 0;
    
    list_stack_.push_back(state);
}

void HtmlGenerator::popListState() {
    if (!list_stack_.empty()) {
        list_stack_.pop_back();
    }
}

HtmlGenerator::ListState& HtmlGenerator::currentList() {
    if (list_stack_.empty()) {
        log_error("currentList: list stack is empty");
        static ListState default_state;
        return default_state;
    }
    return list_stack_.back();
}

void HtmlGenerator::pushFloatState(const std::string& type, const char* position) {
    FloatState state;
    state.type = type;
    state.position = position ? position : "";
    state.has_caption = false;
    
    float_stack_.push_back(state);
}

void HtmlGenerator::popFloatState() {
    if (!float_stack_.empty()) {
        float_stack_.pop_back();
    }
}

HtmlGenerator::FloatState& HtmlGenerator::currentFloat() {
    if (float_stack_.empty()) {
        log_error("currentFloat: float stack is empty");
        static FloatState default_state;
        return default_state;
    }
    return float_stack_.back();
}

std::vector<std::string> HtmlGenerator::parseColumnSpec(const char* spec) {
    // Parse LaTeX column specification
    // Examples: "lrc", "l|c|r", "p{3cm}c"
    
    std::vector<std::string> cols;
    if (!spec) return cols;
    
    for (const char* p = spec; *p; p++) {
        switch (*p) {
            case 'l':
                cols.push_back("left");
                break;
            case 'c':
                cols.push_back("center");
                break;
            case 'r':
                cols.push_back("right");
                break;
            case '|':
                // Vertical line - could store for border rendering
                break;
            case 'p':
                // Paragraph column - skip width spec
                cols.push_back("left");
                while (*p && *p != '}') p++;
                break;
            default:
                // Ignore other characters
                break;
        }
    }
    
    return cols;
}

} // namespace lambda
