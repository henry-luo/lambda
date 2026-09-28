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

    bool options(ElementBuilder& parent) {
        skip_space_comments();
        if (position_ >= length_ || source_[position_] != '[') return true;
        size_t begin = 0, end = 0;
        if (!group('[', ']', &begin, &end)) return false;
        parent.attr("options_source", source_item(begin, end));
        size_t item_begin = begin;
        size_t depth = 0;
        for (size_t cursor = begin; cursor <= end; cursor++) {
            char c = cursor < end ? source_[cursor] : ',';
            if (c == '\\' && cursor + 1 < end) { cursor++; continue; }
            if (c == '{' || c == '[' || c == '(') depth++;
            else if ((c == '}' || c == ']' || c == ')') && depth > 0) depth--;
            if (c != ',' || depth != 0) continue;
            size_t key_begin = item_begin, key_end = cursor;
            size_t value_begin = cursor, value_end = cursor;
            size_t nested = 0;
            for (size_t i = item_begin; i < cursor; i++) {
                if (source_[i] == '{' || source_[i] == '[' || source_[i] == '(') nested++;
                else if ((source_[i] == '}' || source_[i] == ']' || source_[i] == ')') && nested > 0) nested--;
                else if (source_[i] == '=' && nested == 0) {
                    key_end = i;
                    value_begin = i + 1;
                    value_end = cursor;
                    break;
                }
            }
            trim_span(source_, &key_begin, &key_end);
            trim_span(source_, &value_begin, &value_end);
            if (value_end > value_begin && source_[value_begin] == '{' &&
                    source_[value_end - 1] == '}') {
                value_begin++;
                value_end--;
            }
            if (key_end > key_begin) {
                ElementBuilder option = builder_.element("option");
                option.attr("key", source_item(key_begin, key_end));
                option.attr("value", source_item(value_begin, value_end));
                parent.child(option.final());
            }
            item_begin = cursor + 1;
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

    bool append_points(ElementBuilder& parent, size_t begin, size_t end, bool physical,
                       bool path_operators) {
        size_t at = begin;
        size_t count = 0;
        while (at < end) {
            skip_inner_space(source_, &at, end);
            if (at == end) break;
            if (count > 0 && path_operators) {
                if (at + 2 <= end && memcmp(source_ + at, "--", 2) == 0) {
                    at += 2;
                    skip_inner_space(source_, &at, end);
                }
                else { error("unsupported path operator"); return false; }
            }
            if (++points_ > 10000) { error("picture exceeds the 10000 point limit"); return false; }
            if (path_operators) {
                if (!append_path_point(parent, &at, end)) return false;
            } else {
                double x = 0.0, y = 0.0;
                if (!coordinate(&at, end, physical, &x, &y)) {
                    error("expected finite Cartesian coordinate");
                    return false;
                }
                parent.child(builder_.element("point").attr("x", x).attr("y", y).final());
            }
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
        if (!options(node)) return false;
        size_t begin = 0, end = 0;
        if (word("coordinates")) {
            node.attr("input_kind", "coordinates");
            if (!group('{', '}', &begin, &end)) return false;
            if (!append_points(node, begin, end, false, false)) return false;
        } else {
            node.attr("input_kind", "expression");
            if (!group('{', '}', &begin, &end)) return false;
            PlotExpressionParser expression(ctx_, source_ + begin, end - begin, begin);
            Item tree = expression.parse();
            if (tree.item == ITEM_NULL) return false;
            node.child(tree);
        }
        skip_space_comments();
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected plot semicolon"); return false;
        }
        parent.child(node.final());
        return true;
    }

    bool path(ElementBuilder& parent, const char* action) {
        ElementBuilder node = builder_.element("path");
        node.attr("action", action);
        if (!options(node)) return false;
        size_t begin = position_, end = 0;
        if (!semicolon(&end)) return false;
        node.attr("source", source_item(begin, end));
        if (!append_points(node, begin, end, true, true)) return false;
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
        if (!options(el)) return false;
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
        if (!coordinate(&at, length_, true, &x, &y)) {
            error("expected node coordinate"); return false;
        }
        position_ = at;
        size_t begin = 0, end = 0;
        if (!group('{', '}', &begin, &end)) return false;
        el.attr("x", x).attr("y", y).attr("source", source_item(begin, end));
        skip_space_comments();
        if (position_ >= length_ || source_[position_++] != ';') {
            error("expected node semicolon"); return false;
        }
        parent.child(el.final());
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
        if (!options(el)) return false;
        if (!parse_children(el, name, depth + 1)) return false;
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
            } else if (!closing && strcmp(name, "usepgfplotslibrary") == 0) {
                size_t begin = 0, end = 0;
                if (!group('{', '}', &begin, &end)) return false;
                parent.child(builder_.element("pgfplots_library")
                    .attr("source", source_item(begin, end)).final());
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
