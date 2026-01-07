// html_generator.cpp - Implementation of HTML generator for LaTeX documents
// Translates latex.js html-generator.ls to C++

#include "html_generator.hpp"
#include "../../lib/log.h"
#include <sstream>
#include <algorithm>
#include <cstring>

namespace lambda {

// =============================================================================
// Typography Helper Functions
// =============================================================================

// Process typography substitutions (dashes, ligatures, quotes)
// Returns processed string - caller owns the result
static std::string processTypography(const char* input) {
    if (!input) return "";

    std::string result;
    result.reserve(strlen(input) * 2);  // Reserve extra space for potential expansions

    const char* p = input;
    while (*p) {
        // Check for --- (em dash) first (before --)
        if (p[0] == '-' && p[1] == '-' && p[2] == '-') {
            result += "\xE2\x80\x94";  // U+2014 em dash
            p += 3;
            continue;
        }
        // Check for -- (en dash)
        if (p[0] == '-' && p[1] == '-') {
            result += "\xE2\x80\x93";  // U+2013 en dash
            p += 2;
            continue;
        }
        // Note: Single hyphens (-) are NOT converted to Unicode hyphens (U+2010)
        // LaTeX.js keeps regular ASCII hyphens for words like "daughter-in-law"

        // Check for ligatures - order matters! Check longer ligatures first
        // ffi ligature
        if (p[0] == 'f' && p[1] == 'f' && p[2] == 'i') {
            result += "\xEF\xAC\x83";  // U+FB03 ffi ligature
            p += 3;
            continue;
        }
        // ffl ligature
        if (p[0] == 'f' && p[1] == 'f' && p[2] == 'l') {
            result += "\xEF\xAC\x84";  // U+FB04 ffl ligature
            p += 3;
            continue;
        }
        // ff ligature
        if (p[0] == 'f' && p[1] == 'f') {
            result += "\xEF\xAC\x80";  // U+FB00 ff ligature
            p += 2;
            continue;
        }
        // fi ligature
        if (p[0] == 'f' && p[1] == 'i') {
            result += "\xEF\xAC\x81";  // U+FB01 fi ligature
            p += 2;
            continue;
        }
        // fl ligature
        if (p[0] == 'f' && p[1] == 'l') {
            result += "\xEF\xAC\x82";  // U+FB02 fl ligature
            p += 2;
            continue;
        }
        // Check for << (left guillemet)
        if (p[0] == '<' && p[1] == '<') {
            result += "\xC2\xAB";  // U+00AB left guillemet «
            p += 2;
            continue;
        }
        // Check for >> (right guillemet)
        if (p[0] == '>' && p[1] == '>') {
            result += "\xC2\xBB";  // U+00BB right guillemet »
            p += 2;
            continue;
        }
        // Check for `` (opening double quote)
        if (p[0] == '`' && p[1] == '`') {
            result += "\xE2\x80\x9C";  // U+201C left double quotation mark
            p += 2;
            continue;
        }
        // Check for '' (closing double quote)
        if (p[0] == '\'' && p[1] == '\'') {
            result += "\xE2\x80\x9D";  // U+201D right double quotation mark
            p += 2;
            continue;
        }
        // Check for ` (opening single quote)
        if (p[0] == '`') {
            result += "\xE2\x80\x98";  // U+2018 left single quotation mark
            p += 1;
            continue;
        }
        // Default: copy character as-is
        result += *p++;
    }

    return result;
}

// =============================================================================
// Constructor
// =============================================================================

HtmlGenerator::HtmlGenerator(Pool* pool, HtmlWriter* writer)
    : LatexGenerator(pool, writer), math_mode_(false), verbatim_mode_(false),
      capture_writer_(nullptr), original_writer_(nullptr) {

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

void HtmlGenerator::trimTrailingWhitespace() {
    writer_->trimTrailingWhitespace();
}

bool HtmlGenerator::hasTrailingWhitespace() const {
    return writer_->hasTrailingWhitespace();
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

void HtmlGenerator::divWithStyle(const char* style_value) {
    // Create div with style attribute only
    writer_->openTag("div", nullptr, nullptr, style_value);
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

    // Skip EMPTY_STRING sentinel ("lambda.nil")
    size_t len = strlen(content);
    if (len == 10 && strncmp(content, "lambda.nil", 10) == 0) {
        return;
    }

    // In verbatim mode or monospace font, preserve all and don't process typography
    // Monospace fonts (like \texttt) should not use ligatures or dash conversion
    if (verbatim_mode_ || currentFont().family == FontFamily::Typewriter) {
        writer_->writeText(content);
        return;
    }

    // In normal mode, apply typography transformations (dashes, ligatures, quotes)
    std::string processed = processTypography(content);
    writer_->writeText(processed.c_str());
}

void HtmlGenerator::textWithClass(const char* content, const char* css_class) {
    // Create a span with the given class and text content
    writer_->openTag("span", css_class);
    writer_->writeText(content);
    writer_->closeTag("span");
}

void HtmlGenerator::rawHtml(const char* html) {
    // Write raw HTML without escaping - used for SVG and other pre-rendered content
    if (!html) return;
    writer_->writeRawHtml(html);
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
    // Only output classes for non-default font properties
    std::stringstream ss;

    // Build CSS class from font properties (only non-default values)
    switch (font.family) {
        case FontFamily::Roman: break;  // Roman is default, don't output
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
        case FontShape::ExplicitUpright: ss << "up "; break;  // Explicit upright toggle from italic
        case FontShape::Upright: break;  // Default upright, no class
    }

    // Include font size as class (e.g., "small", "large") for LaTeX.js compatibility
    switch (font.size) {
        case FontSize::Tiny: ss << "tiny "; break;
        case FontSize::ScriptSize: ss << "scriptsize "; break;
        case FontSize::FootnoteSize: ss << "footnotesize "; break;
        case FontSize::Small: ss << "small "; break;
        case FontSize::NormalSize: break;  // Default, don't output
        case FontSize::Large: ss << "large "; break;
        case FontSize::Large2: ss << "Large "; break;
        case FontSize::Large3: ss << "LARGE "; break;
        case FontSize::Huge: ss << "huge "; break;
        case FontSize::Huge2: ss << "Huge "; break;
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

    // Generate anchor ID - use "sec" prefix for all section-level elements
    std::string anchor = generateAnchorId("sec");

    // Set label info for \label command
    std::string label_text = number;
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
    // Expected format for chapter: <h1 id="sec-1"><div>Chapter 1</div>Title</h1>
    // Expected format for others: <h2 id="sec-1">1 Section Name</h2>

    int heading_level = getHeadingLevel(level);

    // Create heading element with anchor ID as raw attribute
    char tag[4];
    snprintf(tag, sizeof(tag), "h%d", heading_level);

    std::stringstream attrs;
    attrs << "id=\"" << anchor << "\"";
    writer_->openTagRaw(tag, attrs.str().c_str());

    // Section number followed by title
    if (!number.empty()) {
        if (level == "chapter") {
            // latex.js: chaphead = @create @block, @macro(\chaptername) ++ (@createText @symbol \space) ++ @macro(\the + sec)
            // Creates: <div>Chapter X</div>Title
            writer_->openTag("div");
            text("Chapter ");
            text(number.c_str());
            closeElement();  // div
        } else {
            // Other sections: number + quad + title
            text(number.c_str());
            text("\xe2\x80\x83");  // UTF-8 encoding of U+2003 (em space / quad)
        }
    }

    // Section title
    text(title.c_str());

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

void HtmlGenerator::startItemize(const char* alignment) {
    // html-generator.ls startItemize method - match LaTeX.js output

    LatexGenerator::startList();  // Update list depth counter
    pushListState("itemize");

    // Store alignment in list state
    if (alignment && alignment[0]) {
        currentList().alignment = alignment;
    }

    // Use "list" class, add alignment class if present
    if (alignment && alignment[0]) {
        std::string list_class = std::string("list ") + alignment;
        writer_->openTag("ul", list_class.c_str());
    } else {
        writer_->openTag("ul", "list");
    }
}

void HtmlGenerator::endItemize() {
    // html-generator.ls endItemize method

    writer_->closeTag("ul");
    popListState();
    LatexGenerator::endList();
}

void HtmlGenerator::startEnumerate(const char* alignment) {
    // html-generator.ls startEnumerate method

    LatexGenerator::startList();
    pushListState("enumerate");

    // Store alignment in list state
    if (alignment && alignment[0]) {
        currentList().alignment = alignment;
    }

    // Use "list" class, add alignment class if present
    if (alignment && alignment[0]) {
        std::string list_class = std::string("list ") + alignment;
        writer_->openTag("ol", list_class.c_str());
    } else {
        writer_->openTag("ol", "list");
    }
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

    writer_->openTag("dl", "list");
}

void HtmlGenerator::endDescription() {
    // html-generator.ls endDescription method

    writer_->closeTag("dl");
    popListState();
    LatexGenerator::endList();
}

void HtmlGenerator::createItem(const char* label) {
    // html-generator.ls createItem method - match LaTeX.js output format
    // Expected format for itemize:
    //   <li><span class="itemlabel"><span class="hbox llap">•</span></span><p>content</p></li>
    // Expected format for enumerate:
    //   <li><span class="itemlabel"><span class="hbox llap">1.</span></span><p>content</p></li>

    if (list_stack_.empty()) {
        log_error("createItem: not in a list environment");
        return;
    }

    ListState& state = currentList();
    state.item_count++;

    if (state.type == "itemize") {
        // Check if we have an alignment class to add to the li
        const char* li_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("li", li_class);
        // Add item label span structure matching LaTeX.js
        writer_->openTag("span", "itemlabel");
        writer_->openTag("span", "hbox llap");

        // Check if custom label is provided
        if (label != nullptr) {
            // Use custom label (can be empty string for \item[])
            writer_->writeRawHtml(label);
        } else {
            // Use different bullet markers based on nesting depth (matching LaTeX default)
            int depth = getListDepth();
            if (depth == 2) {
                // Level 2: en-dash with rm bf up styling
                writer_->openTag("span", "rm bf up");
                writer_->writeRawHtml("–");  // U+2013 EN DASH
                writer_->closeTag("span");  // close rm bf up
            } else if (depth == 3) {
                writer_->writeRawHtml("*");  // Level 3: asterisk
            } else if (depth >= 4) {
                writer_->writeRawHtml("·");  // Level 4+: middle dot (U+00B7)
            } else {
                writer_->writeRawHtml("•");  // Level 1: bullet (U+2022)
            }
        }

        writer_->closeTag("span");  // close hbox llap
        writer_->closeTag("span");  // close itemlabel
        // Open paragraph for content with alignment class if present
        const char* p_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("p", p_class);
    } else if (state.type == "enumerate") {
        // Step the enumerate counter for this depth level
        std::string counter_name;
        int depth = getListDepth();
        switch (depth) {
            case 1: counter_name = "enumi"; break;
            case 2: counter_name = "enumii"; break;
            case 3: counter_name = "enumiii"; break;
            case 4: counter_name = "enumiv"; break;
            default: counter_name = "enumi"; break;
        }
        stepCounter(counter_name);

        // Set current label for \label commands within this item
        // The anchor is item-N where N is the counter value
        int counter_value = getCounter(counter_name);
        std::string item_anchor = "item-" + std::to_string(counter_value);
        std::string item_text = std::to_string(counter_value);
        setCurrentLabel(item_anchor, item_text);

        // Check if we have an alignment class to add
        const char* li_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("li", li_class);
        // Add item label span structure matching LaTeX.js
        writer_->openTag("span", "itemlabel");
        writer_->openTag("span", "hbox llap");
        // Generate enumerate label based on depth (counter already incremented)
        std::string enumLabel = getEnumerateLabel(depth);
        writer_->writeRawHtml(enumLabel.c_str());
        writer_->closeTag("span");  // close hbox llap
        writer_->closeTag("span");  // close itemlabel
        // Open paragraph for content with alignment class if present
        const char* p_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("p", p_class);
    } else if (state.type == "description") {
        if (label) {
            writer_->openTag("dt", nullptr);
            text(label);
            writer_->closeTag("dt");
        }
        writer_->openTag("dd", nullptr);
        // Open paragraph for content (same as itemize/enumerate)
        writer_->openTag("p", nullptr);
    }
}

void HtmlGenerator::endItem() {
    // End list item - closes <p> and <li> for itemize/enumerate
    // For description, just closes <dd>

    if (list_stack_.empty()) {
        log_error("endItem: not in a list environment");
        return;
    }

    ListState& state = currentList();

    if (state.type == "itemize" || state.type == "enumerate") {
        // Check if there's an empty <p> tag at the end (from paragraph break before end of item)
        // If so, remove it instead of closing it
        bool removed = writer_->removeLastOpenedTagIfEmpty("p");
        log_debug("endItem: removeLastOpenedTagIfEmpty(p) = %d", removed ? 1 : 0);
        if (!removed) {
            // Check if a <p> is actually open before trying to close it
            // After nested block elements (like nested lists), there may be no <p> to close
            bool p_open = writer_->isTagOpen("p");
            log_debug("endItem: isTagOpen(p) = %d", p_open ? 1 : 0);
            if (p_open) {
                // <p> is open, trim whitespace and close it
                writer_->trimTrailingWhitespace();
                writer_->closeTag("p");
            }
            // If no <p> is open, just skip closing it
        }
        // Close <li> tag
        writer_->closeTag("li");
    } else if (state.type == "description") {
        // Close <p> tag first if open
        if (!writer_->removeLastOpenedTagIfEmpty("p")) {
            if (writer_->isTagOpen("p")) {
                writer_->trimTrailingWhitespace();
                writer_->closeTag("p");
            }
        }
        // Close <dd> tag
        writer_->closeTag("dd");
    }
}

void HtmlGenerator::itemParagraphBreak() {
    // Handle paragraph break within list item - closes </p> only
    // A new <p> is opened lazily when content is encountered
    // This is used when parbreak appears inside list item content

    if (list_stack_.empty()) {
        log_error("itemParagraphBreak: not in a list environment");
        return;
    }

    ListState& state = currentList();

    if (state.type == "itemize" || state.type == "enumerate" || state.type == "description") {
        // Trim trailing whitespace before closing paragraph
        writer_->trimTrailingWhitespace();
        // Close current <p> - new one will be opened lazily
        writer_->closeTag("p");
    }
}

std::string HtmlGenerator::getEnumerateLabel(int depth) const {
    // html-generator.ls getEnumerateLabel method
    // Get the label format for enumerate at given depth
    // Returns HTML with id and appropriate format matching LaTeX.js output
    //
    // Default LaTeX enumerate labels:
    // Level 1: <span id="item-1">1.</span> (arabic with dot)
    // Level 2: <span id="item-2">(a)</span> (alph with parens)
    // Level 3: <span id="item-3">i.</span> (roman with dot)
    // Level 4: <span id="item-4">A.</span> (Alph with dot)

    std::string counter_name;
    std::string format;
    std::string prefix = "";
    std::string suffix = ".";

    switch (depth) {
        case 1:
            counter_name = "enumi";
            format = "arabic";
            suffix = ".";
            break;
        case 2:
            counter_name = "enumii";
            format = "alph";
            prefix = "(";
            suffix = ")";
            break;
        case 3:
            counter_name = "enumiii";
            format = "roman";
            suffix = ".";
            break;
        case 4:
            counter_name = "enumiv";
            format = "Alph";
            suffix = ".";
            break;
        default:
            return "";
    }

    if (hasCounter(counter_name)) {
        int value = getCounter(counter_name);
        std::string item_id = "item-" + std::to_string(value);
        std::string formatted = formatCounter(counter_name, format);
        return "<span id=\"" + item_id + "\">" + prefix + formatted + suffix + "</span>";
    }

    return "";
}

// =============================================================================
// Environment Methods (html-generator.ls lines 382-450)
// =============================================================================

void HtmlGenerator::startQuote() {
    writer_->openTag("div", "list quote");
}

void HtmlGenerator::endQuote() {
    writer_->closeTag("div");
}

void HtmlGenerator::startQuotation() {
    writer_->openTag("div", "list quotation");
}

void HtmlGenerator::endQuotation() {
    writer_->closeTag("div");
}

void HtmlGenerator::startVerse() {
    writer_->openTag("div", "list verse");
}

void HtmlGenerator::endVerse() {
    writer_->closeTag("div");
}

void HtmlGenerator::startCenter() {
    writer_->openTag("div", "list center");
    setAlignment("center");
}

void HtmlGenerator::endCenter() {
    writer_->closeTag("div");
}

void HtmlGenerator::startFlushLeft() {
    writer_->openTag("div", "list flushleft");
    setAlignment("left");
}

void HtmlGenerator::endFlushLeft() {
    writer_->closeTag("div");
}

void HtmlGenerator::startFlushRight() {
    writer_->openTag("div", "list flushright");
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
    // Use HTML5 figure tag for table floats (matches LaTeX.js behavior)
    writer_->openTag("figure", "table-float");
}

void HtmlGenerator::endTable() {
    writer_->closeTag("figure");
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
    // Note: ID will be added by caption if present
    // Use HTML5 figure tag for semantic structure (matches LaTeX.js)
    writer_->openTag("figure", "figure-float");
}

void HtmlGenerator::endFigure() {
    writer_->closeTag("figure");
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

    // Generate anchor for labeling
    std::string anchor_id;
    if (state.type == "figure") {
        if (state.anchor.empty()) {
            state.anchor = generateAnchorId("fig");
        }
        anchor_id = state.anchor;
        setCurrentLabel(state.anchor, formatCounter("figure", "arabic"));
    } else if (state.type == "table") {
        if (state.anchor.empty()) {
            state.anchor = generateAnchorId("tab");
        }
        anchor_id = state.anchor;
        setCurrentLabel(state.anchor, formatCounter("table", "arabic"));
    }

    // Open caption div with ID
    writer_->openTag("div", "caption", anchor_id.c_str());

    // Write caption label
    std::string label;
    if (state.type == "figure") {
        label = "Figure " + formatCounter("figure", "arabic");
    } else if (state.type == "table") {
        label = "Table " + formatCounter("table", "arabic");
    }

    if (!label.empty()) {
        span("caption-label");
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

void HtmlGenerator::startInlineMathWithSource(const char* latex_source) {
    math_mode_ = true;
    // Store original LaTeX source in data-latex attribute for math typesetting
    std::string attrs = "class=\"math inline\"";
    if (latex_source && *latex_source) {
        // Escape special characters for HTML attribute
        std::string escaped;
        for (const char* p = latex_source; *p; p++) {
            switch (*p) {
                case '"': escaped += "&quot;"; break;
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                default: escaped += *p; break;
            }
        }
        attrs += " data-latex=\"" + escaped + "\"";
    }
    writer_->openTagRaw("span", attrs.c_str());
}

void HtmlGenerator::endInlineMath() {
    writer_->closeTag("span");
    math_mode_ = false;
}

void HtmlGenerator::startDisplayMath() {
    math_mode_ = true;
    writer_->openTag("div", "math display");
}

void HtmlGenerator::startDisplayMathWithSource(const char* latex_source) {
    math_mode_ = true;
    // Store original LaTeX source in data-latex attribute for math typesetting
    std::string attrs = "class=\"math display\"";
    if (latex_source && *latex_source) {
        // Escape special characters for HTML attribute
        std::string escaped;
        for (const char* p = latex_source; *p; p++) {
            switch (*p) {
                case '"': escaped += "&quot;"; break;
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                default: escaped += *p; break;
            }
        }
        attrs += " data-latex=\"" + escaped + "\"";
    }
    writer_->openTagRaw("div", attrs.c_str());
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

void HtmlGenerator::writeZWS() {
    // Output zero-width space character (U+200B) to preserve word boundaries
    // This allows line breaking but doesn't collapse like regular spaces
    text("\u200B");
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

        writer_->openTagRaw("a", attrs.str().c_str());
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

// =============================================================================
// Capture Mode Methods (supports nested captures)
// =============================================================================

void HtmlGenerator::startCapture() {
    // Push current writer onto stack and create a new capture writer
    CaptureState state;
    state.previous_writer = writer_;
    state.capture_writer = new TextHtmlWriter(pool_, false);  // No pretty-printing
    capture_stack_.push_back(state);
    writer_ = state.capture_writer;
    log_debug("startCapture: depth=%zu", capture_stack_.size());
}

std::string HtmlGenerator::endCapture() {
    // Pop the capture stack and restore previous writer
    if (capture_stack_.empty()) {
        log_error("endCapture: not in capture mode");
        return "";
    }

    CaptureState state = capture_stack_.back();
    capture_stack_.pop_back();

    const char* html = state.capture_writer->getHtml();
    std::string result = html ? html : "";

    // Restore previous writer
    writer_ = state.previous_writer;

    // Clean up capture writer
    delete state.capture_writer;

    log_debug("endCapture: depth=%zu, captured=%zu chars", capture_stack_.size(), result.size());
    return result;
}

void HtmlGenerator::createItemWithHtmlLabel(const char* html_label) {
    // Create list item with pre-rendered HTML as the custom label
    // This is used for enumerate/itemize with \item[...] where the label contains formatting

    if (list_stack_.empty()) {
        log_error("createItemWithHtmlLabel: not in a list environment");
        return;
    }

    ListState& state = currentList();
    state.item_count++;

    if (state.type == "itemize") {
        const char* li_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("li", li_class);
        writer_->openTag("span", "itemlabel");
        writer_->openTag("span", "hbox llap");

        // Write the pre-rendered HTML label directly (no extra wrapper for itemize)
        if (html_label && html_label[0]) {
            writer_->writeRawHtml(html_label);
        }

        writer_->closeTag("span");  // close hbox llap
        writer_->closeTag("span");  // close itemlabel

        const char* p_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("p", p_class);
    } else if (state.type == "enumerate") {
        // For enumerate with custom label, do NOT step the counter
        // The custom label replaces the number but the next regular item continues the count

        const char* li_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("li", li_class);
        writer_->openTag("span", "itemlabel");
        writer_->openTag("span", "hbox llap");

        // Write the pre-rendered HTML label wrapped in a span (custom label replaces number)
        writer_->openTag("span", nullptr);
        if (html_label && html_label[0]) {
            writer_->writeRawHtml(html_label);
        }
        writer_->closeTag("span");

        writer_->closeTag("span");  // close hbox llap
        writer_->closeTag("span");  // close itemlabel

        const char* p_class = state.alignment.empty() ? nullptr : state.alignment.c_str();
        writer_->openTag("p", p_class);
    } else if (state.type == "description") {
        // For description, use the HTML label as the dt content
        writer_->openTag("dt", nullptr);
        if (html_label && html_label[0]) {
            writer_->writeRawHtml(html_label);
        }
        writer_->closeTag("dt");
        writer_->openTag("dd", nullptr);
        writer_->openTag("p", nullptr);
    }
}

} // namespace lambda
