// latex_parser.cpp - Main LatexParser implementation
// Entry points, state management, and high-level parsing rules

#include "latex_parser.hpp"
#include "latex_registry.hpp"
#include "../../../lib/log.h"
#include <cstdarg>
#include <cstring>

namespace lambda {
namespace latex {

// Forward declarations for math parser integration
extern "C" void parse_math(Input* input, const char* math_string, const char* flavor);

// =============================================================================
// Constructor / Destructor
// =============================================================================

LatexParser::LatexParser(Input* input, const char* source, size_t len)
    : input_(input)
    , ctx_(input, source, len)
    , builder_(ctx_.builder)
    , source_(source)
    , pos_(source)
    , end_(source + len)
    , mode_(LatexMode::Vertical)
    , depth_(0)
{
    // initialize mode stack with vertical mode
    mode_stack_.push(LatexMode::Vertical);
}

LatexParser::~LatexParser() {
    // RAII cleanup - nothing special needed
}

// =============================================================================
// Main Entry Point
// =============================================================================

Item LatexParser::parse() {
    return parse_document();
}

Item LatexParser::parse_document() {
    // create root document element
    Element* root = builder_.element("latex_document").final().element;
    if (!root) {
        error("Failed to create LaTeX root document element");
        return ItemError;
    }

    // skip initial whitespace
    skip_whitespace();

    int element_count = 0;
    const int MAX_ELEMENTS = 10000;  // safety limit

    while (!at_end() && element_count < MAX_ELEMENTS) {
        // check for paragraph breaks at loop start
        if (is_paragraph_break()) {
            // create parbreak element
            Item parbreak = create_parbreak();
            if (parbreak.item != ITEM_NULL && parbreak.item != ITEM_ERROR) {
                list_push((List*)root, parbreak);
            }
            // skip the blank line(s)
            while (!at_end() && (*pos_ == '\n' || *pos_ == ' ' || *pos_ == '\t')) {
                advance();
            }
            continue;
        }

        // handle single newline as potential space
        if (peek() == '\n') {
            advance();
            // single newline acts as space between content
            if (!at_end() && peek() != '\n' && element_count > 0) {
                // check if we should add space (not after whitespace-eating elements)
                // for simplicity, add space
                Item space = create_space();
                if (space.item != ITEM_NULL) {
                    list_push((List*)root, space);
                }
            }
            continue;
        }

        // parse next element
        Item element = parse_content();

        if (element.item == ITEM_ERROR) {
            // stop on error
            break;
        }

        if (element.item != ITEM_NULL) {
            list_push((List*)root, element);
            element_count++;
        }
    }

    // set content length
    ((TypeElmt*)root->type)->content_length = ((List*)root)->length;

    return Item{.element = root};
}

// =============================================================================
// Content Parsing
// =============================================================================

Item LatexParser::parse_content() {
    depth_++;
    if (depth_ > MAX_DEPTH) {
        depth_--;
        error("Maximum parsing depth exceeded");
        return ItemError;
    }

    Item result = ItemNull;

    // skip comments
    while (is_comment()) {
        skip_comment();
        skip_whitespace();
    }

    if (at_end()) {
        depth_--;
        return ItemNull;
    }

    char c = peek();

    // dispatch based on first character
    switch (c) {
        case '\\':
            result = parse_command();
            break;

        case '$':
            result = parse_inline_math();
            break;

        case '{':
            result = parse_group();
            break;

        case '}':
            // end of group - return null to signal caller
            depth_--;
            return ItemNull;

        case '%':
            skip_comment();
            result = parse_content();
            break;

        default:
            // text content
            result = parse_text();
            break;
    }

    depth_--;
    return result;
}

// =============================================================================
// Mode Management
// =============================================================================

void LatexParser::enter_mode(LatexMode m) {
    mode_stack_.push(mode_);
    mode_ = m;
}

void LatexParser::exit_mode() {
    if (!mode_stack_.empty()) {
        mode_ = mode_stack_.top();
        mode_stack_.pop();
    }
}

// =============================================================================
// Position Tracking
// =============================================================================

SourceSpan LatexParser::make_span(size_t start) const {
    SourceSpan span;
    span.start_offset = start;
    span.end_offset = offset();
    // TODO: compute line/col from tracker
    span.start_line = 0;
    span.start_col = 0;
    span.end_line = 0;
    span.end_col = 0;
    return span;
}

// =============================================================================
// Character Access
// =============================================================================

char LatexParser::peek(int off) const {
    if (pos_ + off >= end_) return '\0';
    return pos_[off];
}

char LatexParser::advance() {
    if (at_end()) return '\0';
    return *pos_++;
}

bool LatexParser::match(const char* str) {
    size_t len = strlen(str);
    if (remaining() < len) return false;
    if (strncmp(pos_, str, len) != 0) return false;
    pos_ += len;
    return true;
}

bool LatexParser::match(char c) {
    if (peek() != c) return false;
    advance();
    return true;
}

bool LatexParser::match_word(const char* word) {
    size_t len = strlen(word);
    if (remaining() < len) return false;
    if (strncmp(pos_, word, len) != 0) return false;
    // check that it's not followed by alphanumeric
    if (remaining() > len && isalnum(pos_[len])) return false;
    pos_ += len;
    return true;
}

bool LatexParser::lookahead(const char* str) const {
    size_t len = strlen(str);
    if (remaining() < len) return false;
    return strncmp(pos_, str, len) == 0;
}

// =============================================================================
// Whitespace & Comment Handling
// =============================================================================

void LatexParser::skip_spaces() {
    // skip horizontal whitespace only (space, tab)
    while (!at_end() && (peek() == ' ' || peek() == '\t')) {
        advance();
    }
}

void LatexParser::skip_whitespace() {
    // skip all whitespace including newlines and comments
    while (!at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
        } else if (c == '%') {
            skip_comment();
        } else {
            break;
        }
    }
}

void LatexParser::skip_comment() {
    // skip % comment to end of line
    if (peek() != '%') return;

    while (!at_end() && peek() != '\n') {
        advance();
    }
    // skip the newline
    if (!at_end() && peek() == '\n') {
        advance();
    }
}

bool LatexParser::is_paragraph_break() {
    // blank line detection (PEG.js style)
    // A paragraph break is: newline, optional spaces/comments, newline

    if (peek() != '\n') return false;

    const char* p = pos_ + 1;

    // skip spaces and tabs
    while (p < end_ && (*p == ' ' || *p == '\t')) p++;

    // skip comment if present
    if (p < end_ && *p == '%') {
        while (p < end_ && *p != '\n') p++;
        if (p < end_ && *p == '\n') p++;
    }

    // check for another newline
    return p < end_ && *p == '\n';
}

// =============================================================================
// Group Balancing
// =============================================================================

void LatexParser::start_balanced() {
    balance_stack_.push(0);
}

bool LatexParser::is_balanced() {
    return !balance_stack_.empty() && balance_stack_.top() == 0;
}

void LatexParser::end_balanced() {
    if (!balance_stack_.empty()) {
        balance_stack_.pop();
    }
}

// =============================================================================
// Groups
// =============================================================================

Item LatexParser::parse_group() {
    // parse {...}
    if (!match('{')) return ItemNull;

    // create a group element
    Element* group = builder_.element("group").final().element;
    if (!group) {
        return ItemError;
    }

    // parse content until closing brace
    while (!at_end() && peek() != '}') {
        Item child = parse_content();
        if (child.item == ITEM_ERROR) {
            return ItemError;
        }
        if (child.item != ITEM_NULL) {
            list_push((List*)group, child);
        }
    }

    // skip closing brace
    if (!match('}')) {
        error("Expected closing brace '}'");
    }

    ((TypeElmt*)group->type)->content_length = ((List*)group)->length;
    return Item{.element = group};
}

Item LatexParser::parse_opt_group() {
    // parse [...]
    if (!match('[')) return ItemNull;

    return parse_balanced_content(']');
}

Item LatexParser::parse_balanced_content(char end_char) {
    // parse content until end_char, handling nested braces
    StringBuf* sb = ctx_.sb;
    stringbuf_reset(sb);

    int depth = 1;
    while (!at_end() && depth > 0) {
        char c = peek();

        if (c == end_char && depth == 1) {
            advance();  // skip end char
            break;
        } else if (c == '{') {
            depth++;
            stringbuf_append_char(sb, c);
            advance();
        } else if (c == '}') {
            depth--;
            if (depth > 0) {
                stringbuf_append_char(sb, c);
            }
            advance();
        } else if (c == '\\') {
            // escape sequence
            stringbuf_append_char(sb, c);
            advance();
            if (!at_end()) {
                stringbuf_append_char(sb, peek());
                advance();
            }
        } else {
            stringbuf_append_char(sb, c);
            advance();
        }
    }

    if (sb->length > 0) {
        return create_text(sb->str->chars, sb->length);
    }
    return ItemNull;
}

std::string LatexParser::parse_balanced_braces() {
    // parse content inside matched {} without the outer braces
    std::string result;

    int depth = 1;
    while (!at_end() && depth > 0) {
        char c = peek();

        if (c == '{') {
            depth++;
            result += c;
            advance();
        } else if (c == '}') {
            depth--;
            if (depth > 0) {
                result += c;
            }
            advance();
        } else if (c == '\\') {
            result += c;
            advance();
            if (!at_end()) {
                result += peek();
                advance();
            }
        } else {
            result += c;
            advance();
        }
    }

    return result;
}

// =============================================================================
// Identifier Parsing
// =============================================================================

std::string LatexParser::parse_identifier() {
    std::string id;

    while (!at_end() && (isalnum(peek()) || peek() == '_' || peek() == '*')) {
        id += advance();
    }

    return id;
}

// =============================================================================
// Element Creation Helpers
// =============================================================================

Item LatexParser::create_element(const char* tag) {
    Element* elem = builder_.element(tag).final().element;
    if (!elem) {
        return ItemError;
    }
    return Item{.element = elem};
}

Item LatexParser::create_text(const char* text, size_t len) {
    String* str = builder_.createString(text, len);
    if (!str) {
        return ItemError;
    }
    return Item{.item = s2it(str)};
}

Item LatexParser::create_text(const std::string& text) {
    return create_text(text.c_str(), text.length());
}

Item LatexParser::create_space() {
    return create_text(" ", 1);
}

Item LatexParser::create_nbsp() {
    return create_text("\u00A0", 2);  // non-breaking space
}

Item LatexParser::create_parbreak() {
    return create_element("parbreak");
}

Item LatexParser::char_from_code(int code) {
    // convert Unicode code point to UTF-8 string
    char buf[5] = {0};

    if (code < 0x80) {
        buf[0] = (char)code;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
    } else {
        buf[0] = (char)(0xF0 | (code >> 18));
        buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code & 0x3F));
    }

    return create_text(buf, strlen(buf));
}

// =============================================================================
// Error Handling
// =============================================================================

void LatexParser::error(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ctx_.addError(ctx_.tracker.location(), buf);
    log_error("LaTeX parser error: %s", buf);
}

void LatexParser::warning(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ctx_.addWarning(ctx_.tracker.location(), buf);
}

void LatexParser::expect(char c) {
    if (!match(c)) {
        error("Expected '%c'", c);
    }
}

void LatexParser::expect(const char* str) {
    if (!match(str)) {
        error("Expected '%s'", str);
    }
}

// =============================================================================
// Numbers
// =============================================================================

int LatexParser::parse_integer() {
    int result = 0;
    bool negative = false;

    skip_spaces();

    if (match('-')) {
        negative = true;
    } else {
        match('+');
    }

    while (!at_end() && isdigit(peek())) {
        result = result * 10 + (advance() - '0');
    }

    return negative ? -result : result;
}

double LatexParser::parse_float() {
    double result = 0.0;
    bool negative = false;

    skip_spaces();

    if (match('-')) {
        negative = true;
    } else {
        match('+');
    }

    // integer part
    while (!at_end() && isdigit(peek())) {
        result = result * 10 + (advance() - '0');
    }

    // decimal part
    if (match('.')) {
        double frac = 0.0;
        double div = 10.0;
        while (!at_end() && isdigit(peek())) {
            frac += (advance() - '0') / div;
            div *= 10.0;
        }
        result += frac;
    }

    return negative ? -result : result;
}

int LatexParser::parse_hex(int digits) {
    int result = 0;
    for (int i = 0; i < digits && !at_end(); i++) {
        char c = peek();
        int digit = 0;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            break;
        }
        result = result * 16 + digit;
        advance();
    }
    return result;
}

int LatexParser::parse_octal() {
    int result = 0;
    while (!at_end() && peek() >= '0' && peek() <= '7') {
        result = result * 8 + (advance() - '0');
    }
    return result;
}

// =============================================================================
// Lengths
// =============================================================================

Length LatexParser::parse_length() {
    skip_spaces();
    double value = parse_float();
    skip_spaces();
    std::string unit = parse_length_unit();

    // optional plus/minus for rubber lengths
    skip_spaces();
    if (match_word("plus")) {
        skip_spaces();
        parse_float();
        parse_length_unit();
    }
    skip_spaces();
    if (match_word("minus")) {
        skip_spaces();
        parse_float();
        parse_length_unit();
    }

    return Length{value, unit};
}

std::string LatexParser::parse_length_unit() {
    static const char* units[] = {
        "sp", "pt", "px", "dd", "mm", "pc", "cc", "cm", "in", "ex", "em",
        "bp", "mu", "fil", "fill", "filll", nullptr
    };

    for (const char** u = units; *u; u++) {
        if (match_word(*u)) {
            return *u;
        }
    }

    // default to pt
    return "pt";
}

// =============================================================================
// Numeric Expressions (for counters)
// =============================================================================

int LatexParser::parse_num_expr() {
    int result = parse_num_term();

    while (true) {
        skip_spaces();
        if (match('+')) {
            skip_spaces();
            result += parse_num_term();
        } else if (match('-')) {
            skip_spaces();
            result -= parse_num_term();
        } else {
            break;
        }
    }

    return result;
}

int LatexParser::parse_num_term() {
    int result = parse_num_factor();

    while (true) {
        skip_spaces();
        if (match('*')) {
            skip_spaces();
            result *= parse_num_factor();
        } else if (match('/')) {
            skip_spaces();
            int divisor = parse_num_factor();
            if (divisor != 0) {
                result /= divisor;
            }
        } else {
            break;
        }
    }

    return result;
}

int LatexParser::parse_num_factor() {
    skip_spaces();

    // unary +/-
    if (match('-')) {
        return -parse_num_factor();
    }
    if (match('+')) {
        return parse_num_factor();
    }

    // parentheses
    if (match('(')) {
        int result = parse_num_expr();
        expect(')');
        return result;
    }

    // \value{counter}
    if (lookahead("\\value")) {
        return parse_value_command();
    }

    // integer
    return parse_integer();
}

int LatexParser::parse_value_command() {
    if (!match("\\value")) return 0;
    expect('{');
    std::string name = parse_identifier();
    expect('}');

    // TODO: look up counter value from input context
    return 0;
}

// =============================================================================
// Section Level Lookup
// =============================================================================

bool LatexParser::is_section_command() const {
    if (!lookahead("\\")) return false;

    const char* p = pos_ + 1;
    std::string name;
    while (p < end_ && isalpha(*p)) {
        name += *p++;
    }

    return get_section_level_for(name.c_str()) >= -1;
}

int LatexParser::get_section_level(const std::string& name) const {
    return get_section_level_for(name.c_str());
}

// =============================================================================
// Environment Type Checks
// =============================================================================

bool LatexParser::is_math_environment(const std::string& name) const {
    return is_math_environment_name(name.c_str());
}

bool LatexParser::is_verbatim_environment(const std::string& name) const {
    return is_verbatim_environment_name(name.c_str());
}

bool LatexParser::is_list_environment(const std::string& name) const {
    return is_list_environment_name(name.c_str());
}

} // namespace latex
} // namespace lambda
