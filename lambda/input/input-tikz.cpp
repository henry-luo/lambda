// Bounded source parser for the first native TikZ/PGFPlots picture subset.
// Syntax is kept as Mark; all drawing semantics stay in the script package.

#include "input-context.hpp"
#include "input-latex-scanner.h"
#include "input-parsers.h"
#include "../io/mark_builder.hpp"
#include "../../lib/str.h"
#include "../../lib/strbuf.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

using lambda::InputContext;

namespace {

static void report_tikz_error(InputContext& ctx, size_t offset, const char* message) {
    ctx.tracker.seek(offset);
    char detail[512];
    size_t length = str_copy(detail, sizeof(detail), "tikz: ", 6);
    str_cat(detail, length, sizeof(detail), message, strlen(message));
    ctx.addError(ctx.location(), detail);
}

class PlotExpressionParser {
public:
    // An anchored parser reads substituted pic text and reports at `source_offset` only.
    PlotExpressionParser(InputContext& context, const char* source, size_t length,
                         size_t source_offset, bool anchored = false)
        : ctx_(context), builder_(context.builder), source_(source), length_(length),
          offset_(source_offset), position_(0), nodes_(0), anchored_(anchored) {}

    Item parse() {
        if (length_ > 4096) { error("plot expression exceeds 4096 bytes"); return ItemNull; }
        Item root = expression(0, 0);
        skip_space();
        if (root.item != ITEM_NULL && position_ != length_)
            error("unexpected plot expression token");
        return root;
    }

private:
    InputContext& ctx_;
    MarkBuilder& builder_;
    const char* source_;
    size_t length_;
    size_t offset_;
    size_t position_;
    size_t nodes_;
    bool anchored_;

    void error(const char* message) {
        report_tikz_error(ctx_, anchored_ ? offset_ : offset_ + position_, message);
    }

    void skip_space() {
        while (position_ < length_ && isspace((unsigned char)source_[position_])) position_++;
    }

    static int precedence(char op) {
        if (op == '>' || op == '<' || op == '=' || op == '!') return 1;
        if (op == '+' || op == '-') return 2;
        if (op == '*' || op == '/') return 3;
        if (op == '^') return 4;
        return 0;
    }

    Item expression(int minimum, size_t depth) {
        if (depth > 64 || ++nodes_ > 1024) {
            error("plot expression exceeds complexity limit"); return ItemNull;
        }
        Item lhs = primary(depth + 1);
        if (lhs.item == ITEM_NULL) return lhs;
        while (true) {
            skip_space();
            if (position_ >= length_) return lhs;
            if (source_[position_] == 'r' &&
                    (position_ + 1 == length_ ||
                     !isalpha((unsigned char)source_[position_ + 1]))) {
                // PGF math's postfix r marks a trig argument as radians.
                position_++;
                lhs = builder_.element("radian_value").child(lhs).final();
                continue;
            }
            if (source_[position_] == '?' && minimum == 0) {
                position_++;
                Item when_true = expression(0, depth + 1);
                skip_space();
                if (when_true.item == ITEM_NULL) return when_true;
                if (position_ >= length_ || source_[position_++] != ':') {
                    error("conditional plot expression requires ':'"); return ItemNull;
                }
                Item when_false = expression(0, depth + 1);
                if (when_false.item == ITEM_NULL) return when_false;
                lhs = builder_.element("conditional").child(lhs)
                    .child(when_true).child(when_false).final();
                continue;
            }
            char op = source_[position_];
            int power = precedence(op);
            if (power == 0 || power < minimum) return lhs;
            size_t op_length = 1;
            if (power == 1 && position_ + 1 < length_ && source_[position_ + 1] == '=')
                op_length = 2;
            else if (op == '=' || op == '!') {
                error("unsupported plot comparison operator"); return ItemNull;
            }
            position_ += op_length;
            // Exponentiation is right-associative; the other operators are left-associative.
            Item rhs = expression(power + (op == '^' ? 0 : 1), depth + 1);
            if (rhs.item == ITEM_NULL) return rhs;
            char spelling[3] = {op, op_length == 2 ? '=' : '\0', '\0'};
            lhs = builder_.element("binary").attr("op", spelling)
                .child(lhs).child(rhs).final();
        }
    }

    Item primary(size_t depth) {
        skip_space();
        if (position_ >= length_) { error("missing plot expression operand"); return ItemNull; }
        char c = source_[position_];
        if (c == '+' || c == '-') {
            position_++;
            Item operand = expression(4, depth + 1);
            if (operand.item == ITEM_NULL) return operand;
            char spelling[2] = {c, '\0'};
            return builder_.element("unary").attr("op", spelling).child(operand).final();
        }
        if (c == '(') {
            position_++;
            Item nested = expression(0, depth + 1);
            skip_space();
            if (position_ >= length_ || source_[position_++] != ')') {
                error("unclosed plot expression group"); return ItemNull;
            }
            return nested;
        }
        if (isdigit((unsigned char)c) || c == '.') {
            char* after = nullptr;
            double value = strtod(source_ + position_, &after);
            size_t next = (size_t)(after - source_);
            if (next == position_ || next > length_ || !isfinite(value)) {
                error("invalid plot expression number"); return ItemNull;
            }
            position_ = next;
            return builder_.element("number_literal").attr("value", value).final();
        }
        if (c == '\\' && position_ + 1 < length_ &&
                isalpha((unsigned char)source_[position_ + 1])) {
            size_t begin = ++position_;
            while (position_ < length_ && isalpha((unsigned char)source_[position_]))
                position_++;
            return builder_.element("variable")
                .attr("name", builder_.createStringItem(source_ + begin,
                    position_ - begin)).final();
        }
        if (c == '#' && position_ + 1 < length_ &&
                isdigit((unsigned char)source_[position_ + 1])) {
            size_t begin = position_;
            position_ += 2;
            return builder_.element("variable")
                .attr("name", builder_.createStringItem(source_ + begin,
                    position_ - begin)).final();
        }
        if (isalpha((unsigned char)c)) {
            size_t begin = position_;
            while (position_ < length_ &&
                    (isalnum((unsigned char)source_[position_]) ||
                     source_[position_] == '_')) position_++;
            size_t length = position_ - begin;
            char name[32];
            if (length >= sizeof(name)) { error("plot expression name too long"); return ItemNull; }
            str_copy(name, sizeof(name), source_ + begin, length);
            if (strcmp(name, "x") == 0 || strcmp(name, "pi") == 0)
                return builder_.element("variable").attr("name", name).final();
            skip_space();
            // Preserve arbitrary function names; script validates the PGF math vocabulary.
            if (position_ >= length_ || source_[position_] != '(')
                return builder_.element("variable").attr("name", name).final();
            position_++;
            ElementBuilder call = builder_.element("function_call");
            call.attr("name", name);
            size_t argument_count = 0;
            while (true) {
                if (++argument_count > 8) {
                    error("plot function exceeds eight arguments"); return ItemNull;
                }
                Item argument = expression(0, depth + 1);
                if (argument.item == ITEM_NULL) return argument;
                call.child(argument);
                skip_space();
                if (position_ >= length_) {
                    error("unclosed plot function"); return ItemNull;
                }
                if (source_[position_] == ')') { position_++; break; }
                if (source_[position_++] != ',') {
                    error("plot function arguments require commas"); return ItemNull;
                }
            }
            return call.final();
        }
        error("unsupported plot expression token");
        return ItemNull;
    }
};

class TikzParser {
public:
    TikzParser(InputContext& context)
        : ctx_(context), builder_(context.builder), source_(context.source()),
          length_(context.source_length()), position_(0), commands_(0), points_(0) {}

    Item parse() {
        ElementBuilder picture = builder_.element("tikz_picture");
        if (length_ > 1024 * 1024) {
            error("source exceeds the 1 MiB picture limit");
        } else {
            parse_children(picture, nullptr, 0);
        }
        return picture.final();
    }

private:
    InputContext& ctx_;
    MarkBuilder& builder_;
    const char* source_;
    size_t length_;
    size_t position_;
    size_t commands_;
    size_t points_;
    struct OptionSpan { const char* text; size_t begin, end; };
    OptionSpan axis_defaults_[16];
    size_t axis_default_count_ = 0;
    // Key handlers retained as source spans; expansion substitutes #1/#2 into
    // scratch text, so each span records the buffer it indexes.
    enum StyleKind { STYLE_ONE_ARG, STYLE_TWO_ARGS, STYLE_APPEND, STYLE_DEFAULT, STYLE_PIC };
    struct StyleSpan {
        const char* text;
        size_t name_begin, name_end, value_begin, value_end;
        StyleKind kind;
    };
    struct TextSpan { const char* text; size_t begin, end; };
    StyleSpan styles_[64];
    size_t style_count_ = 0;
    // Pic bodies parse from substituted scratch text; diagnostics point at the \pic command.
    size_t error_anchor_ = SIZE_MAX;
    bool group_ended_ = false;

    size_t located(size_t offset) const {
        return error_anchor_ != SIZE_MAX ? error_anchor_ : offset;
    }

    void error(const char* message) {
        report_tikz_error(ctx_, located(position_), message);
    }

    static bool space(char c) { return isspace((unsigned char)c) != 0; }

    void skip_space_comments() {
        while (position_ < length_) {
            if (space(source_[position_])) {
                position_++;
            } else if (source_[position_] == '%') {
                while (position_ < length_ && source_[position_] != '\n' && source_[position_] != '\r')
                    position_++;
            } else break;
        }
    }

    static void trim_span(const char* source, size_t* begin, size_t* end) {
        while (*begin < *end && space(source[*begin])) (*begin)++;
        while (*end > *begin && space(source[*end - 1])) (*end)--;
    }

    static void trim_option_span(const char* source, size_t* begin, size_t* end) {
        // TeX comments between keys are whitespace, including their newline.
        while (*begin < *end) {
            while (*begin < *end && space(source[*begin])) (*begin)++;
            if (*begin >= *end || source[*begin] != '%') break;
            while (*begin < *end && source[*begin] != '\n' && source[*begin] != '\r')
                (*begin)++;
        }
        while (*end > *begin && space(source[*end - 1])) (*end)--;
    }

    Item source_item(size_t begin, size_t end) {
        return builder_.createStringItem(source_ + begin, end - begin);
    }

    Item text_item(const char* text, size_t begin, size_t end) {
        return builder_.createStringItem(text + begin, end - begin);
    }

    bool group(char open, char close, size_t* begin, size_t* end) {
        skip_space_comments();
        size_t after = latex_scan_group_end(source_, length_, position_, open, close,
                                             begin, end);
        if (!after) {
            error(open == '{' ? "expected balanced group" : "expected balanced options");
            return false;
        }
        position_ = after;
        return true;
    }

    bool word(const char* expected) {
        skip_space_comments();
        size_t n = strlen(expected);
        if (position_ + n > length_ || memcmp(source_ + position_, expected, n) != 0)
            return false;
        if (position_ + n < length_ && isalpha((unsigned char)source_[position_ + n]))
            return false;
        position_ += n;
        return true;
    }

    bool command(char* name, size_t capacity) {
        char full[104];
        size_t after = latex_scan_command(source_, length_, position_, name, capacity,
                                           full, sizeof(full));
        if (!after) {
            error("expected command");
            return false;
        }
        position_ = after;
        return true;
    }

    static bool span_equals(const char* a, size_t a_begin, size_t a_end,
                            const char* b, size_t b_begin, size_t b_end) {
        return a_end - a_begin == b_end - b_begin &&
            memcmp(a + a_begin, b + b_begin, a_end - a_begin) == 0;
    }

    bool style_named(const StyleSpan& style, const char* text, size_t begin, size_t end) const {
        return span_equals(style.text, style.name_begin, style.name_end, text, begin, end);
    }

    // Newest handler of `kind` for a name, or SIZE_MAX.
    size_t find_handler(const char* text, size_t begin, size_t end, StyleKind kind) const {
        for (size_t i = style_count_; i > 0; i--) {
            if (styles_[i - 1].kind == kind && style_named(styles_[i - 1], text, begin, end))
                return i - 1;
        }
        return SIZE_MAX;
    }

    bool is_style(const char* text, size_t begin, size_t end) const {
        return find_handler(text, begin, end, STYLE_ONE_ARG) != SIZE_MAX ||
            find_handler(text, begin, end, STYLE_TWO_ARGS) != SIZE_MAX ||
            find_handler(text, begin, end, STYLE_APPEND) != SIZE_MAX;
    }

    // Record a /.style, /.style 2 args, /.append style, /.default or /.pic key.
    bool define_handler(const char* text, size_t key_begin, size_t key_end,
                        size_t value_begin, size_t value_end) {
        static const struct { const char* suffix; StyleKind kind; } handlers[] = {
            {"/.style 2 args", STYLE_TWO_ARGS}, {"/.append style", STYLE_APPEND},
            {"/.style", STYLE_ONE_ARG}, {"/.default", STYLE_DEFAULT}, {"/.pic", STYLE_PIC}};
        for (const auto& handler : handlers) {
            size_t n = strlen(handler.suffix);
            if (key_end - key_begin <= n ||
                    memcmp(text + key_end - n, handler.suffix, n) != 0) continue;
            size_t name_end = key_end - n;
            while (name_end > key_begin && space(text[name_end - 1])) name_end--;
            if (name_end == key_begin) return true;
            if (style_count_ == sizeof(styles_) / sizeof(styles_[0])) {
                error("too many TikZ styles"); return false;
            }
            styles_[style_count_++] = {text, key_begin, name_end, value_begin, value_end,
                                       handler.kind};
            return true;
        }
        return true;
    }

    // TeX parameter substitution for a handler body; ## stays a literal #.
    bool substitute_parameters(const char* text, size_t begin, size_t end,
                               const TextSpan* args, int count, int arity, StrBuf* out) {
        for (size_t i = begin; i < end; i++) {
            char c = text[i];
            if (c == '#' && i + 1 < end && text[i + 1] == '#') {
                strbuf_append_char(out, '#');
                i++;
            } else if (c == '#' && i + 1 < end && text[i + 1] >= '1' && text[i + 1] <= '9') {
                int index = text[i + 1] - '1';
                if (index >= arity) {
                    error("TikZ style parameter exceeds its argument count"); return false;
                }
                if (index < count)
                    strbuf_append_str_n(out, args[index].text + args[index].begin,
                                        args[index].end - args[index].begin);
                i++;
            } else {
                strbuf_append_char(out, c);
            }
        }
        return true;
    }

    // Split a two-argument style value `{first}{second}`.
    bool two_arguments(const TextSpan& value, TextSpan* args) {
        size_t at = value.begin;
        for (int index = 0; index < 2; index++) {
            while (at < value.end && space(value.text[at])) at++;
            size_t begin = 0, end = 0;
            size_t after = latex_scan_group_end(value.text, value.end, at, '{', '}',
                                                &begin, &end);
            if (!after) { error("two-argument TikZ style needs {first}{second}"); return false; }
            args[index] = {value.text, begin, end};
            at = after;
        }
        while (at < value.end && space(value.text[at])) at++;
        if (at != value.end) { error("two-argument TikZ style needs {first}{second}"); return false; }
        return true;
    }

    // Expand a style use in key order: the newest base definition, then its later appends.
    bool expand_style(ElementBuilder& parent, const char* text, size_t name_begin,
                      size_t name_end, const TextSpan* value, size_t depth) {
        if (depth > 8) { error("TikZ style expansion exceeds 8 levels"); return false; }
        size_t one = find_handler(text, name_begin, name_end, STYLE_ONE_ARG);
        size_t two = find_handler(text, name_begin, name_end, STYLE_TWO_ARGS);
        size_t base = one == SIZE_MAX ? two : two == SIZE_MAX ? one : (one > two ? one : two);
        int arity = base != SIZE_MAX && styles_[base].kind == STYLE_TWO_ARGS ? 2 : 1;
        TextSpan given = {nullptr, 0, 0};
        if (value) given = *value;
        else {
            size_t fallback = find_handler(text, name_begin, name_end, STYLE_DEFAULT);
            if (fallback != SIZE_MAX)
                given = {styles_[fallback].text, styles_[fallback].value_begin,
                         styles_[fallback].value_end};
        }
        TextSpan args[2];
        int count = 0;
        if (arity == 2) {
            if (!given.text) { error("two-argument TikZ style needs {first}{second}"); return false; }
            if (!two_arguments(given, args)) return false;
            count = 2;
        } else if (given.text) {
            args[0] = given;
            count = 1;
        }
        for (size_t i = base == SIZE_MAX ? 0 : base; i < style_count_; i++) {
            const StyleSpan& style = styles_[i];
            if (i != base && (style.kind != STYLE_APPEND ||
                    !style_named(style, text, name_begin, name_end))) continue;
            StrBuf* body = strbuf_new();
            bool ok = substitute_parameters(style.text, style.value_begin, style.value_end,
                                            args, count, arity, body) &&
                append_options(parent, body->str ? body->str : "", 0, body->length,
                               false, false, true, depth + 1);
            strbuf_free(body);
            if (!ok) return false;
        }
        return true;
    }

    bool append_options(ElementBuilder& parent, const char* text, size_t begin, size_t end,
                        bool pgfplots_defaults = false, bool define_styles = false,
                        bool use_styles = false, size_t style_depth = 0) {
        size_t item_begin = begin;
        size_t depth = 0;
        for (size_t cursor = begin; cursor <= end; cursor++) {
            char c = cursor < end ? text[cursor] : ',';
            if (c == '\\' && cursor + 1 < end) { cursor++; continue; }
            if (c == '%') {
                while (cursor < end && text[cursor] != '\n' && text[cursor] != '\r')
                    cursor++;
                continue;
            }
            if (c == '{' || c == '[' || c == '(') depth++;
            else if ((c == '}' || c == ']' || c == ')') && depth > 0) depth--;
            if (c != ',' || depth != 0) continue;
            size_t key_begin = item_begin, key_end = cursor;
            size_t value_begin = cursor, value_end = cursor;
            size_t nested = 0;
            bool has_value = false;
            for (size_t i = item_begin; i < cursor; i++) {
                if (text[i] == '%') {
                    while (i < cursor && text[i] != '\n' && text[i] != '\r') i++;
                    continue;
                }
                if (text[i] == '{' || text[i] == '[' || text[i] == '(') nested++;
                else if ((text[i] == '}' || text[i] == ']' || text[i] == ')') && nested > 0) nested--;
                else if (text[i] == '=' && nested == 0) {
                    key_end = i;
                    value_begin = i + 1;
                    value_end = cursor;
                    has_value = true;
                    break;
                }
            }
            trim_option_span(text, &key_begin, &key_end);
            trim_option_span(text, &value_begin, &value_end);
            // Unwrap one brace group only when it spans the whole value, so
            // `{a}{b}` (two-argument style values) keeps both groups.
            size_t group_begin = 0, group_end = 0;
            if (value_end > value_begin && text[value_begin] == '{' &&
                    latex_scan_group_end(text, value_end, value_begin, '{', '}',
                                         &group_begin, &group_end) == value_end) {
                value_begin = group_begin;
                value_end = group_end;
            }
            if (key_end > key_begin) {
                size_t key_length = key_end - key_begin;
                // Retain declarations as options too, so script can validate handlers.
                if (define_styles &&
                        !define_handler(text, key_begin, key_end, value_begin, value_end))
                    return false;
                if (use_styles) {
                    bool style_key = key_length == 5 && memcmp(text + key_begin, "style", 5) == 0;
                    if (style_key && value_end > value_begin &&
                            is_style(text, value_begin, value_end)) {
                        // `style=name` uses a style without an argument.
                        if (!expand_style(parent, text, value_begin, value_end, nullptr,
                                          style_depth)) return false;
                        item_begin = cursor + 1;
                        continue;
                    }
                    if (!style_key && is_style(text, key_begin, key_end)) {
                        TextSpan argument = {text, value_begin, value_end};
                        if (!expand_style(parent, text, key_begin, key_end,
                                          has_value ? &argument : nullptr, style_depth))
                            return false;
                        item_begin = cursor + 1;
                        continue;
                    }
                }
                if (pgfplots_defaults) {
                    bool append_axis_style = key_length == strlen("every axis/.append style") &&
                        memcmp(text + key_begin, "every axis/.append style", key_length) == 0;
                    bool replace_axis_style = key_length == strlen("every axis/.style") &&
                        memcmp(text + key_begin, "every axis/.style", key_length) == 0;
                    if (!append_axis_style && !replace_axis_style) {
                        error("unsupported pgfplotsset key"); return false;
                    }
                    // /.style replaces prior defaults; /.append style preserves them.
                    if (replace_axis_style) axis_default_count_ = 0;
                    if (axis_default_count_ == 16) {
                        error("too many PGFPlots axis style declarations"); return false;
                    }
                    // Inherited options precede local axis options in TikZ key order.
                    axis_defaults_[axis_default_count_++] = {text, value_begin, value_end};
                }
                ElementBuilder option = builder_.element("option");
                option.attr("key", text_item(text, key_begin, key_end));
                option.attr("value", text_item(text, value_begin, value_end));
                parent.child(option.final());
            }
            item_begin = cursor + 1;
        }
        return true;
    }

    bool options(ElementBuilder& parent, bool define_styles = false,
                 bool use_styles = false, bool record_source = true) {
        skip_space_comments();
        if (position_ >= length_ || source_[position_] != '[') return true;
        size_t begin = 0, end = 0;
        if (!group('[', ']', &begin, &end)) return false;
        if (record_source) parent.attr("options_source", source_item(begin, end));
        return append_options(parent, source_, begin, end, false, define_styles, use_styles);
    }

    bool inherited_style(ElementBuilder& parent, const char* name) {
        size_t length = strlen(name);
        if (!is_style(name, 0, length)) return true;
        return expand_style(parent, name, 0, length, nullptr, 0);
    }

    static void skip_inner_space(const char* source, size_t* at, size_t end) {
        while (*at < end) {
            if (space(source[*at])) {
                (*at)++;
            } else if (source[*at] == '%') {
                // A commented path delimiter is whitespace, not an operator.
                while (*at < end && source[*at] != '\n' && source[*at] != '\r') (*at)++;
            } else break;
        }
    }

    bool number(size_t* at, size_t end, bool physical, double* value,
                bool* explicit_unit = nullptr) {
        skip_inner_space(source_, at, end);
        if (*at >= end) return false;
        char* after = nullptr;
        double n = strtod(source_ + *at, &after);
        size_t next = (size_t)(after - source_);
        if (next == *at || next > end || !isfinite(n)) return false;
        *at = next;
        bool has_unit = false;
        if (physical) {
            // Unitless TikZ coordinates use the current 1 cm basis.
            if (*at + 2 <= end && memcmp(source_ + *at, "mm", 2) == 0) {
                n /= 10.0; *at += 2; has_unit = true;
            } else if (*at + 2 <= end && memcmp(source_ + *at, "cm", 2) == 0) {
                *at += 2; has_unit = true;
            } else if (*at + 2 <= end && memcmp(source_ + *at, "pt", 2) == 0) {
                n *= 2.54 / 72.27; *at += 2; has_unit = true;
            } else if (*at + 2 <= end && memcmp(source_ + *at, "bp", 2) == 0) {
                n *= 2.54 / 72.0; *at += 2; has_unit = true;
            }
        }
        if (explicit_unit) *explicit_unit = has_unit;
        *value = n;
        return true;
    }

    bool coordinate(size_t* at, size_t end, bool physical, double* x, double* y,
                    bool* x_explicit = nullptr, bool* y_explicit = nullptr) {
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != '(') return false;
        if (!number(at, end, physical, x, x_explicit)) return false;
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != ',') return false;
        if (!number(at, end, physical, y, y_explicit)) return false;
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != ')') return false;
        return true;
    }

    bool path_coordinate(size_t* at, size_t end, double base_x, double base_y,
                         double* x, double* y, bool* updates_reference,
                         bool* x_explicit = nullptr, bool* y_explicit = nullptr) {
        skip_inner_space(source_, at, end);
        // TikZ + uses the reference origin without moving it; ++ advances it.
        bool relative = *at < end && source_[*at] == '+';
        bool advancing = relative && *at + 1 < end && source_[*at + 1] == '+';
        if (relative) *at += advancing ? 2 : 1;
        if (!coordinate(at, end, true, x, y, x_explicit, y_explicit)) return false;
        if (relative) { *x += base_x; *y += base_y; }
        *updates_reference = !relative || advancing;
        return true;
    }

    bool control_coordinate(size_t* at, size_t end, double* x, double* y,
                            bool* relative) {
        skip_inner_space(source_, at, end);
        *relative = *at < end && source_[*at] == '+';
        if (*relative) {
            (*at)++;
            if (*at < end && source_[*at] == '+') (*at)++;
        }
        return coordinate(at, end, true, x, y);
    }

    // Parse `axis cs:x,y` or `axis description cs:x,y` from a coordinate body.
    bool axis_coordinate(size_t begin, size_t finish, double* x, double* y,
                         const char** system) {
        static const char* const prefixes[][2] = {
            {"axis description cs:", "axis-description"}, {"axis cs:", "axis"}};
        trim_span(source_, &begin, &finish);
        for (const auto& prefix : prefixes) {
            size_t n = strlen(prefix[0]);
            if (finish - begin <= n || memcmp(source_ + begin, prefix[0], n) != 0) continue;
            size_t at = begin + n;
            if (!number(&at, finish, false, x)) return false;
            skip_inner_space(source_, &at, finish);
            if (at >= finish || source_[at++] != ',' || !number(&at, finish, false, y))
                return false;
            skip_inner_space(source_, &at, finish);
            if (at != finish) return false;
            *system = prefix[1];
            return true;
        }
        return false;
    }

    // Attributes for a non-literal coordinate body: calc `$...$`, polar `a:r`,
    // computed `x,y` sources, or a node name with an optional `.anchor`.
    // Node and pic `at` targets use `at_` names; path points keep `ref`/`calc`.
    bool coordinate_form(ElementBuilder& el, size_t begin, size_t finish, bool node_at) {
        trim_span(source_, &begin, &finish);
        if (begin == finish) return false;
        if (finish - begin >= 2 && source_[begin] == '$' && source_[finish - 1] == '$') {
            size_t calc_begin = begin + 1, calc_end = finish - 1;
            trim_span(source_, &calc_begin, &calc_end);
            if (calc_begin == calc_end) return false;
            el.attr(node_at ? "at_calc" : "calc", source_item(calc_begin, calc_end));
            return true;
        }
        size_t polar_at = begin;
        double angle = 0.0, radius = 0.0;
        bool radius_explicit = false;
        if (number(&polar_at, finish, false, &angle)) {
            skip_inner_space(source_, &polar_at, finish);
            if (polar_at < finish && source_[polar_at++] == ':' &&
                    number(&polar_at, finish, true, &radius, &radius_explicit)) {
                skip_inner_space(source_, &polar_at, finish);
                if (polar_at == finish && radius >= 0.0) {
                    el.attr("polar_angle", angle).attr("polar_radius", radius)
                        .attr("radius_explicit", radius_explicit);
                    return true;
                }
            }
        }
        size_t comma = begin;
        size_t braces = 0, parens = 0;
        for (; comma < finish; comma++) {
            char c = source_[comma];
            if (c == '{') braces++;
            else if (c == '}' && braces > 0) braces--;
            else if (c == '(') parens++;
            else if (c == ')' && parens > 0) parens--;
            else if (c == ',' && braces == 0 && parens == 0) break;
        }
        if (comma < finish) {
            size_t x_begin = begin, x_end = comma;
            size_t y_begin = comma + 1, y_end = finish;
            trim_span(source_, &x_begin, &x_end);
            trim_span(source_, &y_begin, &y_end);
            if (x_begin == x_end || y_begin == y_end) return false;
            // Retain computed coordinates for bounded PGF math evaluation in script.
            el.attr("x_source", source_item(x_begin, x_end))
                .attr("y_source", source_item(y_begin, y_end));
            return true;
        }
        size_t dot = begin;
        while (dot < finish && source_[dot] != '.') dot++;
        size_t name_end = dot, anchor_begin = dot < finish ? dot + 1 : finish;
        size_t anchor_end = finish;
        trim_span(source_, &begin, &name_end);
        trim_span(source_, &anchor_begin, &anchor_end);
        if (!identifier(begin, name_end) || (dot < finish && anchor_begin == anchor_end))
            return false;
        el.attr(node_at ? "at_ref" : "ref", source_item(begin, name_end));
        // Anchor names are validated by the script anchor table.
        if (dot < finish)
            el.attr(node_at ? "at_anchor" : "anchor", source_item(anchor_begin, anchor_end));
        return true;
    }

    // A node, coordinate or pic `at (...)`: literal Cartesian and axis
    // coordinates return their numbers; other forms become `at_` attributes.
    bool at_coordinate(ElementBuilder& el, size_t* at, size_t end, double* x, double* y,
                       const char** system, bool* literal) {
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[*at] != '(') return false;
        size_t probe = *at;
        if (coordinate(&probe, end, true, x, y)) {
            *at = probe;
            *system = "cartesian";
            *literal = true;
            return true;
        }
        size_t begin = 0, finish = 0;
        size_t after = latex_scan_group_end(source_, end, *at, '(', ')', &begin, &finish);
        if (!after) return false;
        if (axis_coordinate(begin, finish, x, y, system)) {
            *literal = true;
        } else {
            if (!coordinate_form(el, begin, finish, true)) return false;
            *literal = false;
        }
        *at = after;
        return true;
    }

    bool identifier(size_t begin, size_t end) {
        if (begin == end || !(isalpha((unsigned char)source_[begin]) ||
                              source_[begin] == '_')) return false;
        for (size_t i = begin + 1; i < end; i++) {
            if (!isalnum((unsigned char)source_[i]) && source_[i] != '_' &&
                source_[i] != '-') return false;
        }
        return true;
    }

    bool append_path_point(ElementBuilder& parent, size_t* at, size_t end,
                           bool move = false, const char* via = nullptr) {
        double x = 0.0, y = 0.0;
        size_t start = *at;
        bool x_explicit = false, y_explicit = false;
        if (coordinate(at, end, true, &x, &y, &x_explicit, &y_explicit)) {
            ElementBuilder literal = builder_.element("point");
            literal.attr("x", x).attr("y", y).attr("x_explicit", x_explicit)
                .attr("y_explicit", y_explicit).attr("move", move);
            // `via` records a -| or |- connector into this vertex.
            if (via) literal.attr("via", via);
            parent.child(literal.final());
            return true;
        }
        size_t begin = 0, finish = 0;
        size_t after = latex_scan_group_end(source_, end, start, '(', ')', &begin, &finish);
        ElementBuilder point = builder_.element("point");
        const char* system = nullptr;
        bool parsed = false;
        if (after && axis_coordinate(begin, finish, &x, &y, &system)) {
            point.attr("x", x).attr("y", y).attr("coord_system", system);
            parsed = true;
        } else if (after) {
            parsed = coordinate_form(point, begin, finish, false);
        }
        if (!parsed) {
            error("expected finite Cartesian coordinate or named node");
            return false;
        }
        *at = after;
        point.attr("move", move);
        if (via) point.attr("via", via);
        parent.child(point.final());
        return true;
    }
    bool path_word(size_t* at, size_t end, const char* expected) {
        skip_inner_space(source_, at, end);
        size_t n = strlen(expected);
        if (*at + n > end || memcmp(source_ + *at, expected, n) != 0) return false;
        if (*at + n < end && isalpha((unsigned char)source_[*at + n])) return false;
        *at += n;
        return true;
    }

    bool append_point(ElementBuilder& parent, double x, double y,
                      bool x_explicit = false, bool y_explicit = false,
                      bool move = false, const char* via = nullptr) {
        if (++points_ > 10000) { error("picture exceeds the 10000 point limit"); return false; }
        ElementBuilder point = builder_.element("point");
        point.attr("x", x).attr("y", y).attr("x_explicit", x_explicit)
            .attr("y_explicit", y_explicit).attr("move", move);
        if (via) point.attr("via", via);
        parent.child(point.final());
        return true;
    }

    bool append_path_vertex(ElementBuilder& parent, size_t* at, size_t end,
                            double base_x, double base_y,
                            double* x, double* y, bool* cartesian,
                            bool* updates_reference, bool move = false,
                            const char* via = nullptr) {
        skip_inner_space(source_, at, end);
        size_t start = *at;
        bool x_explicit = false, y_explicit = false;
        if (path_coordinate(at, end, base_x, base_y, x, y,
                            updates_reference, &x_explicit, &y_explicit)) {
            if (!append_point(parent, *x, *y, x_explicit, y_explicit, move, via)) return false;
            *cartesian = true;
            return true;
        }
        *at = start;
        if (++points_ > 10000) { error("picture exceeds the 10000 point limit"); return false; }
        if (!append_path_point(parent, at, end, move, via)) return false;
        *cartesian = false;
        *updates_reference = true;
        return true;
    }

    bool ellipse_radii(size_t* at, size_t end, double* rx, double* ry,
                       bool* rx_explicit, bool* ry_explicit) {
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != '(' ||
                !number(at, end, true, rx, rx_explicit)) return false;
        skip_inner_space(source_, at, end);
        if (*at + 3 > end || memcmp(source_ + *at, "and", 3) != 0) return false;
        *at += 3;
        if (!number(at, end, true, ry, ry_explicit)) return false;
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != ')' || *rx <= 0.0 || *ry <= 0.0) return false;
        return true;
    }

    bool circle_radius(size_t* at, size_t end, double* radius,
                       bool* explicit_unit) {
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != '(' ||
                !number(at, end, true, radius, explicit_unit)) return false;
        skip_inner_space(source_, at, end);
        return *at < end && source_[(*at)++] == ')' && *radius > 0.0;
    }

    bool append_cubic(ElementBuilder& parent, double x0, double y0,
                      double x1, double y1, double x2, double y2,
                      double x3, double y3) {
        // Sampling keeps the path renderer bounded while preserving TikZ's cubic shape.
        const int steps = 32;
        for (int i = 1; i <= steps; i++) {
            double t = (double)i / (double)steps;
            double u = 1.0 - t;
            double x = u * u * u * x0 + 3.0 * u * u * t * x1 +
                3.0 * u * t * t * x2 + t * t * t * x3;
            double y = u * u * u * y0 + 3.0 * u * u * t * y1 +
                3.0 * u * t * t * y2 + t * t * t * y3;
            if (!append_point(parent, x, y)) return false;
        }
        return true;
    }

    // `on_segment` marks a node written between a connector and its target,
    // which TikZ places along that segment rather than at the current point.
    bool append_inline_node(ElementBuilder& parent, size_t* at, size_t end,
                            bool on_segment = false) {
        if (!path_word(at, end, "node")) return false;
        ElementBuilder el = builder_.element("node");
        el.attr("inline", true);
        if (on_segment) el.attr("segment", true);
        if (!inherited_style(el, "every node")) return false;
        skip_inner_space(source_, at, end);
        if (*at < end && source_[*at] == '[') {
            size_t begin = 0, finish = 0;
            size_t after = latex_scan_group_end(source_, end, *at, '[', ']',
                                                 &begin, &finish);
            if (!after) { error("unclosed inline node options"); return false; }
            if (!append_options(el, source_, begin, finish, false, false, true)) return false;
            *at = after;
        }
        skip_inner_space(source_, at, end);
        size_t begin = 0, finish = 0;
        size_t after = latex_scan_group_end(source_, end, *at, '{', '}',
                                             &begin, &finish);
        if (!after) { error("inline node requires text"); return false; }
        *at = after;
        el.attr("source", source_item(begin, finish));
        parent.child(el.final());
        return true;
    }

    bool append_path_geometry(ElementBuilder& parent, size_t begin, size_t end) {
        size_t at = begin;
        double x = 0.0, y = 0.0;
        double reference_x = 0.0, reference_y = 0.0;
        bool cartesian = false;
        bool updates_reference = true;
        bool arc_seen = false;
        size_t shape_probe = at;
        if (path_word(&shape_probe, end, "circle") ||
                path_word(&shape_probe, end, "ellipse")) {
            // TikZ's omitted initial coordinate means the current origin.
            if (!append_point(parent, 0.0, 0.0)) return false;
            cartesian = true;
        } else if (!append_path_vertex(parent, &at, end, 0.0, 0.0,
                                       &x, &y, &cartesian,
                                       &updates_reference)) {
            error("expected TikZ path coordinate"); return false;
        }
        if (updates_reference) { reference_x = x; reference_y = y; }
        while (true) {
            skip_inner_space(source_, &at, end);
            if (at == end) return true;
            size_t node_at = at;
            if (path_word(&node_at, end, "node")) {
                // A path node attaches to the preceding vertex, including a
                // node immediately after the final coordinate.
                if (!append_inline_node(parent, &at, end)) return false;
            } else if (arc_seen) {
                error("path operators after an arc are not supported"); return false;
            } else if (path_word(&at, end, "arc")) {
                size_t source_begin = 0, source_end = 0;
                size_t after = latex_scan_group_end(source_, end, at,
                    '(', ')', &source_begin, &source_end);
                if (!after) { error("arc requires angle and radius group"); return false; }
                parent.attr("arc_source", source_item(source_begin, source_end));
                at = after;
                arc_seen = true;
            } else if (path_word(&at, end, "rectangle")) {
                double x2 = 0.0, y2 = 0.0;
                bool advance = true;
                bool x_explicit = false, y_explicit = false;
                if (!cartesian || !path_coordinate(&at, end, reference_x,
                        reference_y, &x2, &y2, &advance,
                        &x_explicit, &y_explicit)) {
                    error("rectangle requires a second coordinate"); return false;
                }
                if (!append_point(parent, x2, y2, x_explicit, y_explicit)) return false;
                parent.attr("shape", "rectangle");
                x = x2; y = y2;
                if (advance) { reference_x = x2; reference_y = y2; }
                skip_inner_space(source_, &at, end);
                if (at != end && !append_inline_node(parent, &at, end)) return false;
                skip_inner_space(source_, &at, end);
                if (at != end) { error("rectangle accepts only a following node"); return false; }
                return true;
            } else if (path_word(&at, end, "grid")) {
                double x2 = 0.0, y2 = 0.0;
                bool next_cartesian = false, advance = true;
                if (!append_path_vertex(parent, &at, end,
                        reference_x, reference_y, &x2, &y2,
                        &next_cartesian, &advance)) {
                    error("grid requires a second coordinate"); return false;
                }
                parent.attr("shape", "grid");
                skip_inner_space(source_, &at, end);
                if (at != end) { error("grid must end after its second coordinate"); return false; }
                return true;
            } else if (path_word(&at, end, "circle")) {
                double radius = 0.0;
                bool explicit_unit = false;
                if (!circle_radius(&at, end, &radius, &explicit_unit)) {
                    error("circle requires a positive radius"); return false;
                }
                parent.attr("shape", "ellipse").attr("rx", radius).attr("ry", radius)
                    .attr("rx_explicit", explicit_unit).attr("ry_explicit", explicit_unit);
            } else if (path_word(&at, end, "ellipse")) {
                double rx = 0.0, ry = 0.0;
                bool rx_explicit = false, ry_explicit = false;
                if (!ellipse_radii(&at, end, &rx, &ry,
                        &rx_explicit, &ry_explicit)) {
                    error("ellipse requires positive x and y radii"); return false;
                }
                parent.attr("shape", "ellipse").attr("rx", rx).attr("ry", ry)
                    .attr("rx_explicit", rx_explicit).attr("ry_explicit", ry_explicit);
                skip_inner_space(source_, &at, end);
                // A following inline node annotates the completed ellipse.
            } else if (at + 2 <= end &&
                       (memcmp(source_ + at, "--", 2) == 0 ||
                        memcmp(source_ + at, "-|", 2) == 0 ||
                        memcmp(source_ + at, "|-", 2) == 0)) {
                const char* via = source_[at] != '-' ? "|-" :
                    source_[at + 1] != '-' ? "-|" : nullptr;
                if (source_[at] != '-') parent.attr("line_mode", "vertical-horizontal");
                else if (source_[at + 1] != '-')
                    parent.attr("line_mode", "horizontal-vertical");
                at += 2;
                skip_inner_space(source_, &at, end);
                if (at + 4 <= end && memcmp(source_ + at, "node", 4) == 0 &&
                        !append_inline_node(parent, &at, end, true)) return false;
                double x2 = 0.0, y2 = 0.0;
                bool next_cartesian = false;
                bool advance = true;
                if (!append_path_vertex(parent, &at, end,
                        reference_x, reference_y, &x2, &y2,
                        &next_cartesian, &advance, false, via)) {
                    error("line segment requires a second coordinate"); return false;
                }
                x = x2; y = y2; cartesian = next_cartesian;
                if (advance) { reference_x = x2; reference_y = y2; }
            } else if (at + 2 <= end && memcmp(source_ + at, "..", 2) == 0) {
                at += 2;
                if (!cartesian || !path_word(&at, end, "controls")) {
                    error("TikZ curve requires control points"); return false;
                }
                double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
                double x3 = 0.0, y3 = 0.0;
                bool first_relative = false, second_relative = false;
                if (!control_coordinate(&at, end, &x1, &y1, &first_relative) ||
                        !path_word(&at, end, "and") ||
                        !control_coordinate(&at, end, &x2, &y2, &second_relative) ||
                        !path_word(&at, end, "..") ||
                        !coordinate(&at, end, true, &x3, &y3)) {
                    error("TikZ cubic curve requires two controls and an endpoint"); return false;
                }
                if (first_relative) { x1 += x; y1 += y; }
                if (second_relative) { x2 += x3; y2 += y3; }
                if (!append_cubic(parent, x, y, x1, y1, x2, y2, x3, y3)) return false;
                x = x3; y = y3; cartesian = true;
                reference_x = x3; reference_y = y3;
            } else if (at < end && (source_[at] == '(' || source_[at] == '+')) {
                // A coordinate without a connector begins another subpath.
                bool next_cartesian = false, advance = true;
                if (!append_path_vertex(parent, &at, end,
                        reference_x, reference_y, &x, &y,
                        &next_cartesian, &advance, true)) return false;
                cartesian = next_cartesian;
                if (advance) { reference_x = x; reference_y = y; }
            } else {
                error("unsupported path operator in TikZ geometry"); return false;
            }
        }
    }

    // Split a parenthesized `x,y` body at its top-level comma.
    bool pair_sources(size_t begin, size_t finish, size_t* x_begin, size_t* x_end,
                      size_t* y_begin, size_t* y_end) {
        size_t comma = begin, nested = 0;
        for (; comma < finish; comma++) {
            char c = source_[comma];
            if (c == '{' || c == '(') nested++;
            else if ((c == '}' || c == ')') && nested > 0) nested--;
            else if (c == ',' && nested == 0) break;
        }
        if (comma == finish) return false;
        *x_begin = begin; *x_end = comma; *y_begin = comma + 1; *y_end = finish;
        trim_span(source_, x_begin, x_end);
        trim_span(source_, y_begin, y_end);
        return *x_begin < *x_end && *y_begin < *y_end;
    }

    // PGFPlots coordinate lists: numeric pairs, retained sources for
    // `symbolic x/y coords`, and explicit error offsets after +-, += or -=.
    bool append_points(ElementBuilder& parent, size_t begin, size_t end) {
        size_t at = begin;
        size_t count = 0;
        while (at < end) {
            skip_inner_space(source_, &at, end);
            if (at == end) break;
            if (++points_ > 10000) { error("picture exceeds the 10000 point limit"); return false; }
            ElementBuilder point = builder_.element("point");
            double x = 0.0, y = 0.0;
            size_t probe = at;
            if (coordinate(&probe, end, false, &x, &y)) {
                point.attr("x", x).attr("y", y);
                at = probe;
            } else {
                size_t body_begin = 0, body_end = 0;
                size_t x_begin = 0, x_end = 0, y_begin = 0, y_end = 0;
                size_t after = latex_scan_group_end(source_, end, at, '(', ')',
                                                     &body_begin, &body_end);
                if (!after || !pair_sources(body_begin, body_end, &x_begin, &x_end,
                                            &y_begin, &y_end)) {
                    error("expected finite Cartesian coordinate");
                    return false;
                }
                point.attr("x_source", source_item(x_begin, x_end))
                    .attr("y_source", source_item(y_begin, y_end));
                at = after;
            }
            skip_inner_space(source_, &at, end);
            bool both = at + 2 <= end && source_[at] == '+' && source_[at + 1] == '-';
            bool plus = at + 2 <= end && source_[at] == '+' && source_[at + 1] == '=';
            bool minus = at + 2 <= end && source_[at] == '-' && source_[at + 1] == '=';
            if (both || plus || minus) {
                at += 2;
                double ex = 0.0, ey = 0.0;
                if (!coordinate(&at, end, false, &ex, &ey)) {
                    error("error bar offsets require a numeric (x,y) pair"); return false;
                }
                if (both || plus) point.attr("error_plus_x", ex).attr("error_plus_y", ey);
                if (both || minus) point.attr("error_minus_x", ex).attr("error_minus_y", ey);
            }
            parent.child(point.final());
            count++;
        }
        if (!count) { error("empty coordinate list"); return false; }
        return true;
    }

    bool semicolon(size_t* end) {
        size_t start = position_;
        size_t braces = 0, brackets = 0, parens = 0;
        for (; position_ < length_; position_++) {
            char c = source_[position_];
            if (c == '\\' && position_ + 1 < length_) { position_++; continue; }
            if (c == '%') {
                while (position_ < length_ && source_[position_] != '\n' && source_[position_] != '\r') position_++;
                continue;
            }
            if (c == '{') braces++;
            else if (c == '[') brackets++;
            else if (c == '(') parens++;
            else if (c == '}' && braces > 0) braces--;
            else if (c == ']' && brackets > 0) brackets--;
            else if (c == ')' && parens > 0) parens--;
            else if (c == ';' && braces == 0 && brackets == 0 && parens == 0) {
                *end = position_++;
                return true;
            }
        }
        position_ = start;
        error("missing command semicolon");
        return false;
    }

    bool plot_input(ElementBuilder& node) {
        skip_space_comments();
        size_t begin = 0, end = 0;
        if (word("coordinates")) {
            node.attr("input_kind", "coordinates");
            if (!group('{', '}', &begin, &end)) return false;
            if (!append_points(node, begin, end)) return false;
        } else if (word("table")) {
            // Table text or file name stays raw; script selects columns and resolves files.
            node.attr("input_kind", "table");
            ElementBuilder table = builder_.element("table_options");
            skip_space_comments();
            if (position_ < length_ && source_[position_] == '[') {
                if (!group('[', ']', &begin, &end) ||
                        !append_options(table, source_, begin, end)) return false;
            }
            if (!group('{', '}', &begin, &end)) return false;
            node.attr("table_source", source_item(begin, end));
            node.attr("table_offset", (int64_t)located(begin));
            node.child(table.final());
        } else if (word("fill")) {
            // fillbetween: `fill between[of=A and B]` names two earlier paths.
            if (!word("between")) { error("expected fill between"); return false; }
            node.attr("input_kind", "fill_between");
            ElementBuilder fill = builder_.element("fill_between");
            skip_space_comments();
            if (position_ < length_ && source_[position_] == '[') {
                if (!group('[', ']', &begin, &end) ||
                        !append_options(fill, source_, begin, end)) return false;
            }
            node.child(fill.final());
        } else if (position_ < length_ && source_[position_] == '(') {
            // A PGFPlots parametric pair has two independently sampled expressions.
            node.attr("input_kind", "parametric");
            if (!group('(', ')', &begin, &end)) return false;
            size_t comma = begin, nested = 0;
            for (; comma < end; comma++) {
                char c = source_[comma];
                if (c == '{' || c == '(') nested++;
                else if ((c == '}' || c == ')') && nested > 0) nested--;
                else if (c == ',' && nested == 0) break;
            }
            if (comma == end) { error("parametric plot requires x and y expressions"); return false; }
            size_t starts[2] = {begin, comma + 1};
            size_t ends[2] = {comma, end};
            for (int part = 0; part < 2; part++) {
                trim_span(source_, &starts[part], &ends[part]);
                if (ends[part] > starts[part] + 1 && source_[starts[part]] == '{' &&
                        source_[ends[part] - 1] == '}') {
                    starts[part]++; ends[part]--;
                }
                PlotExpressionParser expression(ctx_, source_ + starts[part],
                                                ends[part] - starts[part],
                                                located(starts[part]),
                                                error_anchor_ != SIZE_MAX);
                Item tree = expression.parse();
                if (tree.item == ITEM_NULL) return false;
                node.child(builder_.element(part == 0 ? "x_expression" : "y_expression")
                    .child(tree).final());
            }
        } else {
            node.attr("input_kind", "expression");
            if (!group('{', '}', &begin, &end)) return false;
            PlotExpressionParser expression(ctx_, source_ + begin, end - begin,
                                            located(begin), error_anchor_ != SIZE_MAX);
            Item tree = expression.parse();
            if (tree.item == ITEM_NULL) return false;
            node.child(tree);
        }
        skip_space_comments();
        if (position_ + 4 <= length_ &&
                memcmp(source_ + position_, "node", 4) == 0) {
            size_t at = position_;
            if (!append_inline_node(node, &at, length_)) return false;
            position_ = at;
            skip_space_comments();
        }
        // \closedcycle closes an area plot down to the zero line.
        if (latex_scan_starts_with(source_, length_, position_, "\\closedcycle") &&
                (position_ + 12 >= length_ || !isalpha((unsigned char)source_[position_ + 12]))) {
            position_ += 12;
            node.attr("closed_cycle", true);
            skip_space_comments();
        }
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected plot semicolon"); return false;
        }
        return true;
    }

    bool plot(ElementBuilder& parent) {
        ElementBuilder node = builder_.element("plot");
        bool append_cycle = position_ < length_ && source_[position_] == '+';
        if (append_cycle) position_++;
        node.attr("append_cycle", append_cycle);
        if (!inherited_style(node, "every plot") || !options(node, false, true) ||
                !plot_input(node)) return false;
        parent.child(node.final());
        return true;
    }

    bool path(ElementBuilder& parent, const char* action) {
        ElementBuilder node = builder_.element("path");
        node.attr("action", action);
        if (!inherited_style(node, "every path") || !options(node, false, true))
            return false;
        size_t begin = position_, end = 0;
        if (!semicolon(&end)) return false;
        node.attr("source", source_item(begin, end));
        size_t plot_at = begin;
        if (path_word(&plot_at, end, "plot")) {
            // Reuse the PGF plot grammar for a plot operator in a drawing path.
            position_ = plot_at;
            node.attr("shape", "plot");
            if (!options(node, false, true) || !plot_input(node)) return false;
            parent.child(node.final());
            return true;
        }
        size_t system_at = begin;
        if (path_word(&system_at, end, "l-system") ||
                path_word(&system_at, end, "lindenmayer system")) {
            skip_inner_space(source_, &system_at, end);
            size_t options_begin = 0, options_end = 0;
            size_t after = latex_scan_group_end(source_, end, system_at,
                '[', ']', &options_begin, &options_end);
            if (!after) { error("l-system requires a bracketed specification"); return false; }
            system_at = after;
            skip_inner_space(source_, &system_at, end);
            if (system_at != end) { error("unexpected content after l-system"); return false; }
            node.attr("shape", "l-system")
                .attr("system_source", source_item(options_begin, options_end));
            parent.child(node.final());
            return true;
        }
        for (size_t cursor = begin; cursor + 3 <= end; cursor++) {
            if (memcmp(source_ + cursor, "arc", 3) != 0) continue;
            bool before = cursor == begin || !isalpha((unsigned char)source_[cursor - 1]);
            bool after = cursor + 3 == end ||
                !isalpha((unsigned char)source_[cursor + 3]);
            if (before && after) {
                // Preserve the ordered path source for the script arc-chain grammar.
                node.attr("shape", "arc-chain");
                parent.child(node.final());
                return true;
            }
        }
        if (!append_path_geometry(node, begin, end)) return false;
        parent.child(node.final());
        return true;
    }

    bool legend(ElementBuilder& parent, bool multiple) {
        size_t begin = 0, end = 0;
        if (!group('{', '}', &begin, &end)) return false;
        if (!multiple) {
            parent.child(builder_.element("legend_entry")
                .attr("source", source_item(begin, end)).attr("from_list", false).final());
            return true;
        }
        size_t start = begin, depth = 0;
        for (size_t cursor = begin; cursor <= end; cursor++) {
            char c = cursor < end ? source_[cursor] : ',';
            if (c == '\\' && cursor + 1 < end) { cursor++; continue; }
            if (c == '{') depth++;
            else if (c == '}' && depth > 0) depth--;
            if (c != ',' || depth != 0) continue;
            size_t label_begin = start, label_end = cursor;
            trim_span(source_, &label_begin, &label_end);
            if (label_begin == label_end) { error("empty plot legend entry"); return false; }
            parent.child(builder_.element("legend_entry")
                .attr("source", source_item(label_begin, label_end))
                .attr("from_list", true).final());
            start = cursor + 1;
        }
        return true;
    }

    bool node(ElementBuilder& parent) {
        ElementBuilder el = builder_.element("node");
        if (!inherited_style(el, "every node") || !options(el, false, true))
            return false;
        skip_space_comments();
        if (position_ < length_ && source_[position_] == '(') {
            size_t begin = 0, end = 0;
            if (!group('(', ')', &begin, &end)) return false;
            trim_span(source_, &begin, &end);
            if (!identifier(begin, end)) {
                error("invalid node name"); return false;
            }
            el.attr("id", source_item(begin, end));
            // TikZ also accepts options after the name, e.g. `(b) [right=of a]`.
            if (!options(el, false, true, false)) return false;
        }
        // A standalone TikZ node without `at` uses the current point (the origin here).
        double x = 0.0, y = 0.0;
        const char* system = nullptr;
        bool literal = true;
        if (word("at")) {
            size_t at = position_;
            if (!at_coordinate(el, &at, length_, &x, &y, &system, &literal)) {
                error("expected node coordinate"); return false;
            }
            position_ = at;
        }
        size_t begin = 0, end = 0;
        if (!group('{', '}', &begin, &end)) return false;
        if (literal) el.attr("x", x).attr("y", y).attr("coord_system", system);
        el.attr("source", source_item(begin, end));
        skip_space_comments();
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected node semicolon"); return false;
        }
        parent.child(el.final());
        return true;
    }

    bool named_coordinate(ElementBuilder& parent) {
        skip_space_comments();
        ElementBuilder coordinate_node = builder_.element("coordinate");
        if (!options(coordinate_node, false, true)) return false;
        size_t begin = 0, end = 0;
        if (!group('(', ')', &begin, &end)) return false;
        trim_span(source_, &begin, &end);
        if (!identifier(begin, end)) { error("invalid coordinate name"); return false; }
        if (!word("at")) { error("named coordinate requires at"); return false; }
        size_t at = position_;
        double x = 0.0, y = 0.0;
        const char* system = nullptr;
        bool literal = true;
        if (!at_coordinate(coordinate_node, &at, length_, &x, &y, &system, &literal)) {
            error("expected named coordinate position"); return false;
        }
        position_ = at;
        skip_space_comments();
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected coordinate semicolon"); return false;
        }
        coordinate_node.attr("id", source_item(begin, end));
        if (literal) coordinate_node.attr("x", x).attr("y", y).attr("coord_system", system);
        parent.child(coordinate_node.final());
        return true;
    }

    // \pic[options] (name) at (coordinate) {pic=argument};  The pic body is
    // parsed from its `/.pic` handler with #1 substituted, inside its own style group.
    bool pic(ElementBuilder& parent, size_t depth, size_t command_at) {
        ElementBuilder el = builder_.element("pic");
        if (!options(el, false, true)) return false;
        skip_space_comments();
        if (position_ < length_ && source_[position_] == '(') {
            size_t begin = 0, end = 0;
            if (!group('(', ')', &begin, &end)) return false;
            trim_span(source_, &begin, &end);
            if (!identifier(begin, end)) { error("invalid pic name"); return false; }
            el.attr("id", source_item(begin, end));
        }
        double x = 0.0, y = 0.0;
        const char* system = nullptr;
        bool literal = true;
        if (word("at")) {
            size_t at = position_;
            if (!at_coordinate(el, &at, length_, &x, &y, &system, &literal)) {
                error("expected pic coordinate"); return false;
            }
            position_ = at;
        }
        if (literal) el.attr("x", x).attr("y", y).attr("coord_system", system);
        size_t begin = 0, end = 0;
        if (!group('{', '}', &begin, &end)) return false;
        size_t name_end = begin;
        while (name_end < end && source_[name_end] != '=') name_end++;
        size_t name_begin = begin, argument_begin = name_end < end ? name_end + 1 : end;
        size_t argument_end = end;
        size_t trimmed_end = name_end;
        trim_span(source_, &name_begin, &trimmed_end);
        trim_span(source_, &argument_begin, &argument_end);
        size_t definition = find_handler(source_, name_begin, trimmed_end, STYLE_PIC);
        if (definition == SIZE_MAX) { error("unknown TikZ pic"); return false; }
        el.attr("name", source_item(name_begin, trimmed_end));
        if (name_end < end) el.attr("argument", source_item(argument_begin, argument_end));
        skip_space_comments();
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected pic semicolon"); return false;
        }
        TextSpan argument = {source_, argument_begin, argument_end};
        StrBuf* body = strbuf_new();
        const StyleSpan& pic_style = styles_[definition];
        bool ok = substitute_parameters(pic_style.text, pic_style.value_begin,
                                        pic_style.value_end, &argument,
                                        name_end < end ? 1 : 0, 1, body);
        if (ok) {
            const char* saved_source = source_;
            size_t saved_length = length_, saved_position = position_;
            size_t saved_anchor = error_anchor_, saved_styles = style_count_;
            source_ = body->str ? body->str : "";
            length_ = body->length;
            position_ = 0;
            if (error_anchor_ == SIZE_MAX) error_anchor_ = command_at;
            ok = parse_children(el, nullptr, depth + 1);
            source_ = saved_source;
            length_ = saved_length;
            position_ = saved_position;
            error_anchor_ = saved_anchor;
            // Styles defined in the body index the scratch text and end with the pic.
            style_count_ = saved_styles;
        }
        strbuf_free(body);
        if (!ok) return false;
        parent.child(el.final());
        return true;
    }

    bool foreach_command(ElementBuilder& parent) {
        skip_space_comments();
        size_t bindings_begin = position_;
        size_t count = 0;
        while (true) {
            skip_space_comments();
            if (position_ >= length_ || source_[position_] != '\\' || ++count > 4) {
                error("foreach needs one to four command bindings"); return false;
            }
            char variable[96];
            if (!command(variable, sizeof(variable))) return false;
            skip_space_comments();
            if (position_ < length_ && source_[position_] == '/') {
                position_++;
                continue;
            }
            break;
        }
        size_t bindings_end = position_;
        if (!word("in")) { error("foreach bindings require in"); return false; }
        size_t items_begin = 0, items_end = 0;
        if (!group('{', '}', &items_begin, &items_end)) return false;
        skip_space_comments();
        size_t body_begin = 0, body_end = 0;
        if (position_ < length_ && source_[position_] == '{') {
            if (!group('{', '}', &body_begin, &body_end)) return false;
        } else {
            body_begin = position_;
            if (position_ >= length_ || source_[position_] != '\\' ||
                    !semicolon(&body_end)) {
                error("foreach body requires a TikZ command or group"); return false;
            }
            body_end++;
        }
        parent.child(builder_.element("foreach")
            .attr("bindings", source_item(bindings_begin, bindings_end))
            .attr("items", source_item(items_begin, items_end))
            .attr("body", source_item(body_begin, body_end)).final());
        return true;
    }

    bool pgf_definition(ElementBuilder& parent, const char* kind) {
        skip_space_comments();
        if (position_ >= length_ || source_[position_] != '\\') {
            error("PGF definition requires a control sequence"); return false;
        }
        char variable[96];
        if (!command(variable, sizeof(variable))) return false;
        size_t begin = 0, end = 0;
        if (!group('{', '}', &begin, &end)) return false;
        parent.child(builder_.element("pgf_definition")
            .attr("kind", kind).attr("variable", variable)
            .attr("source", source_item(begin, end)).final());
        return true;
    }

    bool pgfplots_foreach(ElementBuilder& parent) {
        size_t range_begin = 0, range_end = 0;
        size_t body_begin = 0, body_end = 0;
        if (!group('{', '}', &range_begin, &range_end) ||
                !group('{', '}', &body_begin, &body_end)) return false;
        parent.child(builder_.element("pgfplots_foreach")
            .attr("range", source_item(range_begin, range_end))
            .attr("body", source_item(body_begin, body_end)).final());
        return true;
    }

    bool pgfmath_setmacro(ElementBuilder& parent) {
        size_t variable_begin = 0, variable_end = 0;
        size_t source_begin = 0, source_end = 0;
        if (!group('{', '}', &variable_begin, &variable_end) ||
                !group('{', '}', &source_begin, &source_end)) return false;
        trim_span(source_, &variable_begin, &variable_end);
        if (variable_begin >= variable_end || source_[variable_begin] != '\\' ||
                !identifier(variable_begin + 1, variable_end)) {
            error("PGF math macro needs a control-sequence name"); return false;
        }
        parent.child(builder_.element("pgf_definition")
            .attr("kind", "setmacro")
            .attr("variable", source_item(variable_begin + 1, variable_end))
            .attr("source", source_item(source_begin, source_end)).final());
        skip_space_comments();
        if (position_ < length_ && source_[position_] == ';') position_++;
        return true;
    }

    static bool axis_environment(const char* name) {
        return strcmp(name, "axis") == 0 || strcmp(name, "semilogxaxis") == 0 ||
            strcmp(name, "semilogyaxis") == 0 || strcmp(name, "loglogaxis") == 0 ||
            strcmp(name, "polaraxis") == 0;
    }

    bool axis_default_options(ElementBuilder& el) {
        for (size_t i = 0; i < axis_default_count_; i++) {
            if (!append_options(el, axis_defaults_[i].text, axis_defaults_[i].begin,
                                axis_defaults_[i].end))
                return false;
        }
        return true;
    }

    // groupplot: each \nextgroupplot starts an axis carrying the inherited axis
    // defaults, the groupplot options and its own options, in that key order.
    bool group_plot(ElementBuilder& parent, size_t depth) {
        ElementBuilder plots = builder_.element("groupplot");
        size_t saved_styles = style_count_;
        size_t options_begin = 0, options_end = 0;
        bool has_options = false;
        skip_space_comments();
        if (position_ < length_ && source_[position_] == '[') {
            if (!group('[', ']', &options_begin, &options_end)) return false;
            has_options = true;
            plots.attr("options_source", source_item(options_begin, options_end));
            if (!append_options(plots, source_, options_begin, options_end)) return false;
        }
        skip_space_comments();
        char name[96];
        size_t count = 0;
        group_ended_ = false;
        while (!group_ended_) {
            skip_space_comments();
            size_t member_at = position_;
            if (position_ >= length_ || source_[position_] != '\\' ||
                    !command(name, sizeof(name)) || strcmp(name, "nextgroupplot") != 0) {
                // Report at the offending command, not after its name.
                position_ = member_at;
                error("groupplot requires \\nextgroupplot before its plots"); return false;
            }
            if (++count > 64) { error("groupplot exceeds 64 plots"); return false; }
            ElementBuilder axis = builder_.element("axis");
            if (!axis_default_options(axis)) return false;
            if (has_options && !append_options(axis, source_, options_begin, options_end))
                return false;
            if (!options(axis, false, false)) return false;
            if (!parse_children(axis, "groupplot", depth + 1, true)) return false;
            plots.child(axis.final());
        }
        style_count_ = saved_styles;
        parent.child(plots.final());
        return true;
    }

    bool environment(ElementBuilder& parent, const char* name, size_t depth) {
        if (strcmp(name, "groupplot") == 0) return group_plot(parent, depth);
        bool axis = axis_environment(name);
        if (strcmp(name, "tikzpicture") != 0 && strcmp(name, "scope") != 0 && !axis) {
            error("unsupported environment"); return false;
        }
        ElementBuilder el = builder_.element(name);
        size_t saved_styles = style_count_;
        if (axis && !axis_default_options(el)) return false;
        // Pictures and scopes are TeX groups: their style definitions end with them.
        bool drawing_group = !axis;
        if (!options(el, drawing_group, strcmp(name, "scope") == 0)) return false;
        if (!parse_children(el, name, depth + 1)) return false;
        style_count_ = saved_styles;
        parent.child(el.final());
        return true;
    }

    bool parse_children(ElementBuilder& parent, const char* closing, size_t depth,
                        bool group_axis = false) {
        if (depth > 64) { error("picture nesting exceeds 64 scopes"); return false; }
        // Commands that retain spans across commands must index the document source.
        bool document_text = error_anchor_ == SIZE_MAX;
        while (position_ < length_) {
            skip_space_comments();
            if (position_ == length_) break;
            if (++commands_ > 8192) { error("picture exceeds the 8192 command limit"); return false; }
            if (source_[position_] != '\\') { error("expected TikZ command"); return false; }
            size_t command_start = position_;
            char name[96];
            if (!command(name, sizeof(name))) return false;
            if (strcmp(name, "begin") == 0 || strcmp(name, "end") == 0) {
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                char env[96];
                size_t n = end - begin;
                if (n >= sizeof(env)) { error("environment name too long"); return false; }
                str_copy(env, sizeof(env), source_ + begin, n);
                if (strcmp(name, "end") == 0) {
                    if (!closing || strcmp(closing, env) != 0) {
                        error("mismatched environment end"); return false;
                    }
                    if (group_axis) group_ended_ = true;
                    return true;
                }
                if (!environment(parent, env, depth)) return false;
            } else if (group_axis && strcmp(name, "nextgroupplot") == 0) {
                // The next group member starts here; the group loop re-reads it.
                position_ = command_start;
                return true;
            } else if (strcmp(name, "addplot") == 0) {
                if (!plot(parent)) return false;
            } else if (strcmp(name, "addlegendentry") == 0) {
                if (!legend(parent, false)) return false;
            } else if (strcmp(name, "legend") == 0) {
                if (!legend(parent, true)) return false;
            } else if (strcmp(name, "draw") == 0 || strcmp(name, "path") == 0 ||
                       strcmp(name, "fill") == 0 || strcmp(name, "filldraw") == 0 ||
                       strcmp(name, "clip") == 0) {
                if (!path(parent, name)) return false;
            } else if (strcmp(name, "node") == 0) {
                if (!node(parent)) return false;
            } else if (strcmp(name, "coordinate") == 0) {
                if (!named_coordinate(parent)) return false;
            } else if (strcmp(name, "pic") == 0) {
                if (!pic(parent, depth, command_start)) return false;
            } else if (strcmp(name, "tikzset") == 0) {
                // Definitions are retained as options and scoped like TeX groups.
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                ElementBuilder setting = builder_.element("tikzset");
                setting.attr("source", source_item(begin, end));
                if (!append_options(setting, source_, begin, end, false, true, false))
                    return false;
                parent.child(setting.final());
            } else if (strcmp(name, "foreach") == 0) {
                if (!foreach_command(parent)) return false;
            } else if (strcmp(name, "def") == 0 || strcmp(name, "xdef") == 0) {
                if (!pgf_definition(parent, name)) return false;
            } else if (strcmp(name, "pgfmathsetmacro") == 0) {
                if (!pgfmath_setmacro(parent)) return false;
            } else if (strcmp(name, "tikzmath") == 0) {
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                parent.child(builder_.element("tikzmath")
                    .attr("source", source_item(begin, end)).final());
            } else if (strcmp(name, "pgfplotsinvokeforeach") == 0) {
                if (!pgfplots_foreach(parent)) return false;
            } else if (!closing && document_text && strcmp(name, "usepgfplotslibrary") == 0) {
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                parent.child(builder_.element("pgfplots_library")
                    .attr("source", source_item(begin, end)).final());
            } else if (!closing && document_text && strcmp(name, "pgfplotsset") == 0) {
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                ElementBuilder setting = builder_.element("pgfplots_setting");
                if (!append_options(setting, source_, begin, end, true)) return false;
                parent.child(setting.final());
            } else {
                error("unsupported TikZ command"); return false;
            }
        }
        if (closing) { error("missing environment end"); return false; }
        return true;
    }
};

} // namespace

void parse_tikz_direct(Input* input, const char* source) {
    if (!input || !source) return;
    InputContext context(input, source);
    TikzParser parser(context);
    input->root = parser.parse();
    if (context.hasErrors()) context.logErrors();
}
