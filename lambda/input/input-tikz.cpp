// Bounded source parser for the first native TikZ/PGFPlots picture subset.
// Syntax is kept as Mark; all drawing semantics stay in the script package.

#include "input-context.hpp"
#include "input-latex-scanner.h"
#include "input-parsers.h"
#include "../io/mark_builder.hpp"
#include "../../lib/str.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

using lambda::InputContext;

namespace {

class PlotExpressionParser {
public:
    PlotExpressionParser(InputContext& context, const char* source, size_t length,
                         size_t source_offset)
        : ctx_(context), builder_(context.builder), source_(source), length_(length),
          offset_(source_offset), position_(0), nodes_(0) {}

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

    void error(const char* message) {
        ctx_.tracker.seek(offset_ + position_);
        ctx_.addError(ctx_.location(), "tikz: %s", message);
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
        if (c == '\\' && position_ + 2 <= length_ && source_[position_ + 1] == 'x' &&
                (position_ + 2 == length_ || !isalpha((unsigned char)source_[position_ + 2]))) {
            position_ += 2;
            return builder_.element("variable").attr("name", "x").final();
        }
        if (isalpha((unsigned char)c)) {
            size_t begin = position_;
            while (position_ < length_ && isalpha((unsigned char)source_[position_])) position_++;
            size_t length = position_ - begin;
            char name[32];
            if (length >= sizeof(name)) { error("plot expression name too long"); return ItemNull; }
            str_copy(name, sizeof(name), source_ + begin, length);
            if (strcmp(name, "x") == 0 || strcmp(name, "pi") == 0)
                return builder_.element("variable").attr("name", name).final();
            bool function = strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 ||
                strcmp(name, "tan") == 0 || strcmp(name, "exp") == 0 ||
                strcmp(name, "ln") == 0 || strcmp(name, "sqrt") == 0 ||
                strcmp(name, "abs") == 0 || strcmp(name, "deg") == 0 ||
                strcmp(name, "rad") == 0;
            if (!function) { error("unsupported plot expression name"); return ItemNull; }
            skip_space();
            if (position_ >= length_ || source_[position_++] != '(') {
                error("plot function requires an argument"); return ItemNull;
            }
            Item argument = expression(0, depth + 1);
            skip_space();
            if (position_ >= length_ || source_[position_++] != ')') {
                error("unclosed plot function"); return ItemNull;
            }
            return builder_.element("function_call").attr("name", name)
                .child(argument).final();
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
    struct OptionSpan { size_t begin, end; };
    OptionSpan axis_defaults_[16];
    size_t axis_default_count_ = 0;
    struct StyleSpan { size_t name_begin, name_end, value_begin, value_end; };
    StyleSpan styles_[32];
    size_t style_count_ = 0;

    void error(const char* message) {
        ctx_.tracker.seek(position_);
        ctx_.addError(ctx_.location(), "tikz: %s", message);
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

    bool expand_style(ElementBuilder& parent, size_t key_begin, size_t key_end,
                      size_t depth) {
        if (depth > 8) { error("TikZ style expansion exceeds 8 levels"); return false; }
        for (size_t i = style_count_; i > 0; i--) {
            const StyleSpan& style = styles_[i - 1];
            size_t name_length = style.name_end - style.name_begin;
            if (name_length == key_end - key_begin &&
                    memcmp(source_ + style.name_begin, source_ + key_begin, name_length) == 0)
                return append_options(parent, style.value_begin, style.value_end,
                                      false, false, true, depth + 1);
        }
        return false;
    }

    bool append_options(ElementBuilder& parent, size_t begin, size_t end,
                        bool pgfplots_defaults = false, bool define_styles = false,
                        bool use_styles = false, size_t style_depth = 0) {
        size_t item_begin = begin;
        size_t depth = 0;
        for (size_t cursor = begin; cursor <= end; cursor++) {
            char c = cursor < end ? source_[cursor] : ',';
            if (c == '\\' && cursor + 1 < end) { cursor++; continue; }
            if (c == '%') {
                while (cursor < end && source_[cursor] != '\n' && source_[cursor] != '\r')
                    cursor++;
                continue;
            }
            if (c == '{' || c == '[' || c == '(') depth++;
            else if ((c == '}' || c == ']' || c == ')') && depth > 0) depth--;
            if (c != ',' || depth != 0) continue;
            size_t key_begin = item_begin, key_end = cursor;
            size_t value_begin = cursor, value_end = cursor;
            size_t nested = 0;
            for (size_t i = item_begin; i < cursor; i++) {
                if (source_[i] == '%') {
                    while (i < cursor && source_[i] != '\n' && source_[i] != '\r') i++;
                    continue;
                }
                if (source_[i] == '{' || source_[i] == '[' || source_[i] == '(') nested++;
                else if ((source_[i] == '}' || source_[i] == ']' || source_[i] == ')') && nested > 0) nested--;
                else if (source_[i] == '=' && nested == 0) {
                    key_end = i;
                    value_begin = i + 1;
                    value_end = cursor;
                    break;
                }
            }
            trim_option_span(source_, &key_begin, &key_end);
            trim_option_span(source_, &value_begin, &value_end);
            if (value_end > value_begin && source_[value_begin] == '{' &&
                    source_[value_end - 1] == '}') {
                value_begin++;
                value_end--;
            }
            if (key_end > key_begin) {
                size_t key_length = key_end - key_begin;
                if (define_styles && key_length > 7 &&
                        memcmp(source_ + key_end - 7, "/.style", 7) == 0) {
                    if (style_count_ == 32) { error("too many TikZ styles"); return false; }
                    styles_[style_count_++] = {key_begin, key_end - 7,
                                               value_begin, value_end};
                    item_begin = cursor + 1;
                    continue;
                }
                if (use_styles && value_begin == value_end) {
                    bool matched = false;
                    for (size_t i = style_count_; i > 0; i--) {
                        const StyleSpan& style = styles_[i - 1];
                        if (style.name_end - style.name_begin == key_length &&
                                memcmp(source_ + style.name_begin,
                                       source_ + key_begin, key_length) == 0) {
                            matched = true;
                            break;
                        }
                    }
                    if (matched) {
                        if (!expand_style(parent, key_begin, key_end, style_depth)) return false;
                        item_begin = cursor + 1;
                        continue;
                    }
                }
                if (pgfplots_defaults) {
                    bool append_axis_style = key_length == strlen("every axis/.append style") &&
                        memcmp(source_ + key_begin, "every axis/.append style", key_length) == 0;
                    bool replace_axis_style = key_length == strlen("every axis/.style") &&
                        memcmp(source_ + key_begin, "every axis/.style", key_length) == 0;
                    if (!append_axis_style && !replace_axis_style) {
                        error("unsupported pgfplotsset key"); return false;
                    }
                    // /.style replaces prior defaults; /.append style preserves them.
                    if (replace_axis_style) axis_default_count_ = 0;
                    if (axis_default_count_ == 16) {
                        error("too many PGFPlots axis style declarations"); return false;
                    }
                    // Inherited options precede local axis options in TikZ key order.
                    axis_defaults_[axis_default_count_++] = {value_begin, value_end};
                }
                ElementBuilder option = builder_.element("option");
                option.attr("key", source_item(key_begin, key_end));
                option.attr("value", source_item(value_begin, value_end));
                parent.child(option.final());
            }
            item_begin = cursor + 1;
        }
        return true;
    }

    bool options(ElementBuilder& parent, bool define_styles = false,
                 bool use_styles = false) {
        skip_space_comments();
        if (position_ >= length_ || source_[position_] != '[') return true;
        size_t begin = 0, end = 0;
        if (!group('[', ']', &begin, &end)) return false;
        parent.attr("options_source", source_item(begin, end));
        return append_options(parent, begin, end, false, define_styles, use_styles);
    }

    bool inherited_style(ElementBuilder& parent, const char* name) {
        for (size_t i = style_count_; i > 0; i--) {
            const StyleSpan& style = styles_[i - 1];
            size_t length = style.name_end - style.name_begin;
            if (length == strlen(name) &&
                    memcmp(source_ + style.name_begin, name, length) == 0)
                return append_options(parent, style.value_begin, style.value_end,
                                      false, false, true);
        }
        return true;
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

    bool number(size_t* at, size_t end, bool physical, double* value) {
        skip_inner_space(source_, at, end);
        if (*at >= end) return false;
        char* after = nullptr;
        double n = strtod(source_ + *at, &after);
        size_t next = (size_t)(after - source_);
        if (next == *at || next > end || !isfinite(n)) return false;
        *at = next;
        if (physical) {
            // Unitless TikZ coordinates use the current 1 cm basis.
            if (*at + 2 <= end && memcmp(source_ + *at, "mm", 2) == 0) {
                n /= 10.0; *at += 2;
            } else if (*at + 2 <= end && memcmp(source_ + *at, "cm", 2) == 0) {
                *at += 2;
            } else if (*at + 2 <= end && memcmp(source_ + *at, "pt", 2) == 0) {
                n *= 2.54 / 72.27; *at += 2;
            } else if (*at + 2 <= end && memcmp(source_ + *at, "bp", 2) == 0) {
                n *= 2.54 / 72.0; *at += 2;
            }
        }
        *value = n;
        return true;
    }

    bool coordinate(size_t* at, size_t end, bool physical, double* x, double* y) {
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != '(') return false;
        if (!number(at, end, physical, x)) return false;
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != ',') return false;
        if (!number(at, end, physical, y)) return false;
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != ')') return false;
        return true;
    }

    bool positioned_coordinate(size_t* at, size_t end, double* x, double* y,
                               const char** system) {
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[*at] != '(') return false;
        size_t start = *at + 1;
        skip_inner_space(source_, &start, end);
        const char* prefix = nullptr;
        const char* kind = "cartesian";
        if (start + strlen("axis description cs:") <= end &&
                memcmp(source_ + start, "axis description cs:",
                       strlen("axis description cs:")) == 0) {
            prefix = "axis description cs:";
            kind = "axis-description";
        } else if (start + strlen("axis cs:") <= end &&
                   memcmp(source_ + start, "axis cs:", strlen("axis cs:")) == 0) {
            prefix = "axis cs:";
            kind = "axis";
        }
        if (!prefix) {
            if (!coordinate(at, end, true, x, y)) return false;
        } else {
            *at = start + strlen(prefix);
            if (!number(at, end, false, x)) return false;
            skip_inner_space(source_, at, end);
            if (*at >= end || source_[(*at)++] != ',' ||
                    !number(at, end, false, y)) return false;
            skip_inner_space(source_, at, end);
            if (*at >= end || source_[(*at)++] != ')') return false;
        }
        *system = kind;
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

    bool append_path_point(ElementBuilder& parent, size_t* at, size_t end) {
        double x = 0.0, y = 0.0;
        size_t start = *at;
        if (coordinate(at, end, true, &x, &y)) {
            parent.child(builder_.element("point").attr("x", x).attr("y", y).final());
            return true;
        }
        size_t begin = 0, finish = 0;
        size_t after = latex_scan_group_end(source_, end, start, '(', ')', &begin, &finish);
        trim_span(source_, &begin, &finish);
        if (!after || !identifier(begin, finish)) {
            error("expected finite Cartesian coordinate or named node");
            return false;
        }
        *at = after;
        parent.child(builder_.element("point").attr("ref", source_item(begin, finish)).final());
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

    bool append_point(ElementBuilder& parent, double x, double y) {
        if (++points_ > 10000) { error("picture exceeds the 10000 point limit"); return false; }
        parent.child(builder_.element("point").attr("x", x).attr("y", y).final());
        return true;
    }

    bool append_path_vertex(ElementBuilder& parent, size_t* at, size_t end,
                            double* x, double* y, bool* cartesian) {
        skip_inner_space(source_, at, end);
        size_t start = *at;
        if (coordinate(at, end, true, x, y)) {
            if (!append_point(parent, *x, *y)) return false;
            *cartesian = true;
            return true;
        }
        *at = start;
        if (++points_ > 10000) { error("picture exceeds the 10000 point limit"); return false; }
        if (!append_path_point(parent, at, end)) return false;
        *cartesian = false;
        return true;
    }

    bool ellipse_radii(size_t* at, size_t end, double* rx, double* ry) {
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != '(' || !number(at, end, true, rx)) return false;
        skip_inner_space(source_, at, end);
        if (*at + 3 > end || memcmp(source_ + *at, "and", 3) != 0) return false;
        *at += 3;
        if (!number(at, end, true, ry)) return false;
        skip_inner_space(source_, at, end);
        if (*at >= end || source_[(*at)++] != ')' || *rx <= 0.0 || *ry <= 0.0) return false;
        return true;
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

    bool append_inline_node(ElementBuilder& parent, size_t* at, size_t end) {
        if (!path_word(at, end, "node")) return false;
        ElementBuilder el = builder_.element("node");
        el.attr("inline", true);
        if (!inherited_style(el, "every node")) return false;
        skip_inner_space(source_, at, end);
        if (*at < end && source_[*at] == '[') {
            size_t begin = 0, finish = 0;
            size_t after = latex_scan_group_end(source_, end, *at, '[', ']',
                                                 &begin, &finish);
            if (!after) { error("unclosed inline node options"); return false; }
            if (!append_options(el, begin, finish, false, false, true)) return false;
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
        bool cartesian = false;
        if (!append_path_vertex(parent, &at, end, &x, &y, &cartesian)) {
            error("expected TikZ path coordinate"); return false;
        }
        while (true) {
            skip_inner_space(source_, &at, end);
            if (at == end) return true;
            if (path_word(&at, end, "rectangle")) {
                double x2 = 0.0, y2 = 0.0;
                if (!cartesian || !coordinate(&at, end, true, &x2, &y2)) {
                    error("rectangle requires a second coordinate"); return false;
                }
                if (!append_point(parent, x2, y2)) return false;
                parent.attr("shape", "rectangle");
                x = x2; y = y2;
                skip_inner_space(source_, &at, end);
                if (at != end) { error("rectangle must end after its second coordinate"); return false; }
                return true;
            } else if (path_word(&at, end, "ellipse")) {
                double rx = 0.0, ry = 0.0;
                if (!cartesian || !ellipse_radii(&at, end, &rx, &ry)) {
                    error("ellipse requires positive x and y radii"); return false;
                }
                parent.attr("shape", "ellipse").attr("rx", rx).attr("ry", ry);
                skip_inner_space(source_, &at, end);
                if (at != end) { error("ellipse must end after its radii"); return false; }
                return true;
            } else if (at + 2 <= end &&
                       (memcmp(source_ + at, "--", 2) == 0 ||
                        memcmp(source_ + at, "-|", 2) == 0 ||
                        memcmp(source_ + at, "|-", 2) == 0)) {
                if (source_[at] != '-') parent.attr("line_mode", "vertical-horizontal");
                else if (source_[at + 1] != '-')
                    parent.attr("line_mode", "horizontal-vertical");
                at += 2;
                skip_inner_space(source_, &at, end);
                if (at + 4 <= end && memcmp(source_ + at, "node", 4) == 0 &&
                        !append_inline_node(parent, &at, end)) return false;
                double x2 = 0.0, y2 = 0.0;
                bool next_cartesian = false;
                if (!append_path_vertex(parent, &at, end, &x2, &y2, &next_cartesian)) {
                    error("line segment requires a second coordinate"); return false;
                }
                x = x2; y = y2; cartesian = next_cartesian;
            } else if (at + 2 <= end && memcmp(source_ + at, "..", 2) == 0) {
                at += 2;
                if (!cartesian || !path_word(&at, end, "controls")) {
                    error("TikZ curve requires control points"); return false;
                }
                double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
                double x3 = 0.0, y3 = 0.0;
                if (!coordinate(&at, end, true, &x1, &y1) ||
                        !path_word(&at, end, "and") ||
                        !coordinate(&at, end, true, &x2, &y2) ||
                        !path_word(&at, end, "..") ||
                        !coordinate(&at, end, true, &x3, &y3)) {
                    error("TikZ cubic curve requires two controls and an endpoint"); return false;
                }
                if (!append_cubic(parent, x, y, x1, y1, x2, y2, x3, y3)) return false;
                x = x3; y = y3; cartesian = true;
            } else {
                error("unsupported path operator in TikZ geometry"); return false;
            }
        }
    }

    bool append_points(ElementBuilder& parent, size_t begin, size_t end, bool physical) {
        size_t at = begin;
        size_t count = 0;
        while (at < end) {
            skip_inner_space(source_, &at, end);
            if (at == end) break;
            if (++points_ > 10000) { error("picture exceeds the 10000 point limit"); return false; }
            double x = 0.0, y = 0.0;
            if (!coordinate(&at, end, physical, &x, &y)) {
                error("expected finite Cartesian coordinate");
                return false;
            }
            parent.child(builder_.element("point").attr("x", x).attr("y", y).final());
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

    bool plot(ElementBuilder& parent) {
        ElementBuilder node = builder_.element("plot");
        bool append_cycle = position_ < length_ && source_[position_] == '+';
        if (append_cycle) position_++;
        node.attr("append_cycle", append_cycle);
        if (!inherited_style(node, "every plot") || !options(node, false, true))
            return false;
        skip_space_comments();
        size_t begin = 0, end = 0;
        if (word("coordinates")) {
            node.attr("input_kind", "coordinates");
            if (!group('{', '}', &begin, &end)) return false;
            if (!append_points(node, begin, end, false)) return false;
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
                                                ends[part] - starts[part], starts[part]);
                Item tree = expression.parse();
                if (tree.item == ITEM_NULL) return false;
                node.child(builder_.element(part == 0 ? "x_expression" : "y_expression")
                    .child(tree).final());
            }
        } else {
            node.attr("input_kind", "expression");
            if (!group('{', '}', &begin, &end)) return false;
            PlotExpressionParser expression(ctx_, source_ + begin, end - begin, begin);
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
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected plot semicolon"); return false;
        }
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
        }
        if (!word("at")) { error("node requires an explicit position"); return false; }
        size_t at = position_;
        double x = 0.0, y = 0.0;
        const char* system = nullptr;
        if (!positioned_coordinate(&at, length_, &x, &y, &system)) {
            error("expected node coordinate"); return false;
        }
        position_ = at;
        size_t begin = 0, end = 0;
        if (!group('{', '}', &begin, &end)) return false;
        el.attr("x", x).attr("y", y).attr("coord_system", system)
            .attr("source", source_item(begin, end));
        skip_space_comments();
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected node semicolon"); return false;
        }
        parent.child(el.final());
        return true;
    }

    bool named_coordinate(ElementBuilder& parent) {
        skip_space_comments();
        size_t begin = 0, end = 0;
        if (!group('(', ')', &begin, &end)) return false;
        trim_span(source_, &begin, &end);
        if (!identifier(begin, end)) { error("invalid coordinate name"); return false; }
        if (!word("at")) { error("named coordinate requires at"); return false; }
        size_t at = position_;
        double x = 0.0, y = 0.0;
        const char* system = nullptr;
        if (!positioned_coordinate(&at, length_, &x, &y, &system)) {
            error("expected named coordinate position"); return false;
        }
        position_ = at;
        skip_space_comments();
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected coordinate semicolon"); return false;
        }
        parent.child(builder_.element("coordinate").attr("id", source_item(begin, end))
            .attr("x", x).attr("y", y).attr("coord_system", system).final());
        return true;
    }

    bool environment(ElementBuilder& parent, const char* name, size_t depth) {
        if (strcmp(name, "tikzpicture") != 0 && strcmp(name, "scope") != 0 &&
                strcmp(name, "axis") != 0 && strcmp(name, "semilogxaxis") != 0 &&
                strcmp(name, "semilogyaxis") != 0 && strcmp(name, "loglogaxis") != 0 &&
                strcmp(name, "polaraxis") != 0) {
            error("unsupported environment"); return false;
        }
        ElementBuilder el = builder_.element(name);
        size_t saved_styles = style_count_;
        if (strcmp(name, "axis") == 0 || strcmp(name, "semilogxaxis") == 0 ||
                strcmp(name, "semilogyaxis") == 0 || strcmp(name, "loglogaxis") == 0 ||
                strcmp(name, "polaraxis") == 0) {
            for (size_t i = 0; i < axis_default_count_; i++) {
                if (!append_options(el, axis_defaults_[i].begin, axis_defaults_[i].end))
                    return false;
            }
        }
        if (!options(el, strcmp(name, "tikzpicture") == 0, false)) return false;
        if (!parse_children(el, name, depth + 1)) return false;
        style_count_ = saved_styles;
        parent.child(el.final());
        return true;
    }

    bool parse_children(ElementBuilder& parent, const char* closing, size_t depth) {
        if (depth > 64) { error("picture nesting exceeds 64 scopes"); return false; }
        while (position_ < length_) {
            skip_space_comments();
            if (position_ == length_) break;
            if (++commands_ > 8192) { error("picture exceeds the 8192 command limit"); return false; }
            if (source_[position_] != '\\') { error("expected TikZ command"); return false; }
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
                    return true;
                }
                if (!environment(parent, env, depth)) return false;
            } else if (strcmp(name, "addplot") == 0) {
                if (!plot(parent)) return false;
            } else if (strcmp(name, "addlegendentry") == 0) {
                if (!legend(parent, false)) return false;
            } else if (strcmp(name, "legend") == 0) {
                if (!legend(parent, true)) return false;
            } else if (strcmp(name, "draw") == 0 || strcmp(name, "path") == 0) {
                if (!path(parent, name)) return false;
            } else if (strcmp(name, "node") == 0) {
                if (!node(parent)) return false;
            } else if (strcmp(name, "coordinate") == 0) {
                if (!named_coordinate(parent)) return false;
            } else if (!closing && strcmp(name, "usepgfplotslibrary") == 0) {
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                parent.child(builder_.element("pgfplots_library")
                    .attr("source", source_item(begin, end)).final());
            } else if (!closing && strcmp(name, "pgfplotsset") == 0) {
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                ElementBuilder setting = builder_.element("pgfplots_setting");
                if (!append_options(setting, begin, end, true)) return false;
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
