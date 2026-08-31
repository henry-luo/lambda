// Direct LaTeX and math input parser.
//
// The parser deliberately keeps the scanner small: one cursor, recursive
// groups, and MarkBuilder values.  It has no CST or private AST dependency.

#include "input-context.hpp"
#include "input-latex-scanner.h"
#include "input-latex-tables.h"
#include "input-parsers.h"
#include "../io/mark_builder.hpp"
#include "../../lib/log.h"
#include <ctype.h>
#include <string.h>

using lambda::InputContext;

namespace {

static bool starts_with(const char* s, size_t n, size_t p, const char* needle) {
    return latex_scan_starts_with(s, n, p, needle);
}

static bool item_present(Item item) {
    return item.item != ITEM_NULL;
}

static bool is_big_operator(const char* name) {
    return strcmp(name, "sum") == 0 || strcmp(name, "prod") == 0 ||
        strcmp(name, "coprod") == 0 || strcmp(name, "int") == 0 ||
        strcmp(name, "iint") == 0 || strcmp(name, "iiint") == 0 ||
        strcmp(name, "oint") == 0 || strcmp(name, "lim") == 0 ||
        strcmp(name, "limsup") == 0 || strcmp(name, "liminf") == 0 ||
        strcmp(name, "sup") == 0 || strcmp(name, "inf") == 0 ||
        strcmp(name, "max") == 0 || strcmp(name, "min") == 0 ||
        strcmp(name, "det") == 0 || strcmp(name, "gcd") == 0;
}

static bool is_text_command(const char* name) {
    return strcmp(name, "text") == 0 || strcmp(name, "mbox") == 0 ||
        strcmp(name, "hbox") == 0 || strcmp(name, "textrm") == 0 ||
        strcmp(name, "textit") == 0 || strcmp(name, "textbf") == 0 ||
        strcmp(name, "texttt") == 0 || strcmp(name, "textsf") == 0 ||
        strcmp(name, "textsc") == 0 || strcmp(name, "emph") == 0;
}

static bool is_style_command(const char* name) {
    return strcmp(name, "mathrm") == 0 || strcmp(name, "mathbf") == 0 ||
        strcmp(name, "mathit") == 0 || strcmp(name, "mathsf") == 0 ||
        strcmp(name, "mathtt") == 0 || strcmp(name, "mathcal") == 0 ||
        strcmp(name, "mathbb") == 0 || strcmp(name, "displaystyle") == 0 ||
        strcmp(name, "textstyle") == 0 || strcmp(name, "scriptstyle") == 0 ||
        strcmp(name, "scriptscriptstyle") == 0;
}

static bool is_spacing_command(const char* name) {
    return strcmp(name, "quad") == 0 || strcmp(name, "qquad") == 0 ||
        strcmp(name, "hspace") == 0 || strcmp(name, "vspace") == 0 ||
        strcmp(name, "kern") == 0 || strcmp(name, "mkern") == 0;
}

class DirectMathParser {
public:
    DirectMathParser(InputContext& context, const char* source, size_t length,
                     size_t source_offset, bool ascii)
        : ctx_(context), builder_(context.builder), source_(source), length_(length),
          offset_(source_offset), position_(0), ascii_(ascii) {}

    Item parse() {
        ElementBuilder root = builder_.element("math");
        parse_children(root, false);
        return root.final();
    }

    void parse_into(ElementBuilder& parent, bool matrix_mode) {
        parse_children(parent, matrix_mode);
    }

private:
    InputContext& ctx_;
    MarkBuilder& builder_;
    const char* source_;
    size_t length_;
    size_t offset_;
    size_t position_;
    bool ascii_;

    void error(const char* message) {
        ctx_.tracker.seek(offset_ + position_);
        ctx_.addError(ctx_.location(), "latex_math_c: %s", message);
    }

    void skip_space() {
        while (position_ < length_) {
            char c = source_[position_];
            if (c == '%' ) {
                while (position_ < length_ && source_[position_] != '\n' && source_[position_] != '\r') position_++;
                continue;
            }
            if (!isspace((unsigned char)c)) break;
            position_++;
        }
    }

    bool consume_group_span(char open, char close, size_t* content_start, size_t* content_end) {
        size_t end = latex_scan_group_end(source_, length_, position_, open, close,
                                          content_start, content_end);
        if (end != 0) {
            position_ = end;
            return true;
        }
        error("unterminated group");
        position_ = length_;
        return false;
    }

    Item parse_group() {
        size_t begin = 0, end = 0;
        if (!consume_group_span('{', '}', &begin, &end)) return ItemNull;
        DirectMathParser nested(ctx_, source_ + begin, end - begin, offset_ + begin, ascii_);
        ElementBuilder group = builder_.element("group");
        nested.parse_into(group, false);
        return group.final();
    }

    Item parse_brack_group() {
        size_t begin = 0, end = 0;
        if (!consume_group_span('[', ']', &begin, &end)) return ItemNull;
        DirectMathParser nested(ctx_, source_ + begin, end - begin, offset_ + begin, ascii_);
        ElementBuilder group = builder_.element("brack_group");
        nested.parse_into(group, false);
        return group.final();
    }

    Item parse_paren_script_group() {
        size_t begin = 0, end = 0;
        if (!consume_group_span('(', ')', &begin, &end)) return ItemNull;
        DirectMathParser nested(ctx_, source_ + begin, end - begin, offset_ + begin, ascii_);
        ElementBuilder group = builder_.element("group");
        nested.parse_into(group, false);
        return group.final();
    }

    Item parse_script_arg() {
        skip_space();
        if (position_ >= length_) {
            error("missing script argument");
            return ItemNull;
        }
        if (source_[position_] == '{') return parse_group();
        if (ascii_ && source_[position_] == '(') return parse_paren_script_group();
        return parse_primary();
    }

    Item parse_atom_with_scripts() {
        Item base = parse_primary();
        if (!item_present(base)) return base;
        Item sub = ItemNull;
        Item sup = ItemNull;
        for (;;) {
            skip_space();
            if (position_ >= length_ || (source_[position_] != '_' && source_[position_] != '^')) break;
            char marker = source_[position_++];
            Item value = parse_script_arg();
            if (marker == '_') sub = value;
            else sup = value;
        }
        if (!item_present(sub) && !item_present(sup)) return base;
        ElementBuilder result = builder_.element("subsup");
        result.attr("base", base);
        if (item_present(sub)) result.attr("sub", sub);
        if (item_present(sup)) result.attr("sup", sup);
        return result.final();
    }

    Item parse_primary() {
        skip_space();
        if (position_ >= length_) return ItemNull;
        char c = source_[position_];
        if (c == '{') return parse_group();
        if (c == '[') return parse_brack_group();
        if (c == '\\') return parse_command();
        if (isdigit((unsigned char)c) || (c == '.' && position_ + 1 < length_ && isdigit((unsigned char)source_[position_ + 1]))) {
            size_t start = position_++;
            while (position_ < length_ && (isdigit((unsigned char)source_[position_]) || source_[position_] == '.')) position_++;
            return builder_.createStringItem(source_ + start, position_ - start);
        }
        if (isalpha((unsigned char)c)) {
            size_t start = position_++;
            while (position_ < length_ && isalpha((unsigned char)source_[position_])) position_++;
            return builder_.createStringItem(source_ + start, position_ - start);
        }
        return parse_punctuation_or_operator();
    }

    Item parse_punctuation_or_operator() {
        size_t start = position_++;
        char c = source_[start];
        const char* tag = "punctuation";
        if (c == '&') {
            return builder_.createSymbolItem("col_sep");
        }
        if (c == ';' && position_ < length_ && source_[position_] == ';') {
            position_++;
            return builder_.createSymbolItem("row_sep");
        }
        if (c == '\\' && position_ < length_ && source_[position_] == '\\') {
            position_++;
            return builder_.createSymbolItem("row_sep");
        }
        if (c == '-' && position_ < length_ && source_[position_] == '>') {
            position_++;
            tag = "relation";
        } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=') {
            tag = (c == '=') ? "relation" : "operator";
        }
        else if (c == '<' || c == '>') {
            tag = "relation";
            if (position_ < length_ && source_[position_] == '=') position_++;
        } else if (c == '!' && position_ < length_ && source_[position_] == '=') {
            position_++;
            tag = "relation";
        }
        ElementBuilder elem = builder_.element(tag);
        elem.attr("value", builder_.createStringItem(source_ + start, position_ - start));
        return elem.final();
    }

    bool read_command(char* name, size_t name_capacity, char* full, size_t full_capacity) {
        size_t end = latex_scan_command(source_, length_, position_, name,
                                         name_capacity, full, full_capacity);
        if (end == 0) return false;
        position_ = end;
        return true;
    }

    Item parse_command() {
        char name[96];
        char full[104];
        if (!read_command(name, sizeof(name), full, sizeof(full))) return ItemNull;
        if (name[0] == '\0') return ItemNull;
        if (name[0] == ' ' || name[0] == '\t' || name[0] == '\n') return builder_.createStringItem(" ");

        if (strcmp(name, "frac") == 0 || strcmp(name, "dfrac") == 0 ||
            strcmp(name, "tfrac") == 0 || strcmp(name, "cfrac") == 0) {
            Item numer = parse_script_arg();
            Item denom = parse_script_arg();
            ElementBuilder elem = builder_.element("fraction");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(numer)) elem.attr("numer", numer);
            if (item_present(denom)) elem.attr("denom", denom);
            return elem.final();
        }
        if (strcmp(name, "binom") == 0 || strcmp(name, "dbinom") == 0 || strcmp(name, "tbinom") == 0) {
            Item top = parse_script_arg();
            Item bottom = parse_script_arg();
            ElementBuilder elem = builder_.element("binomial");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(top)) elem.attr("top", top);
            if (item_present(bottom)) elem.attr("bottom", bottom);
            return elem.final();
        }
        if (strcmp(name, "sqrt") == 0) {
            Item index = ItemNull;
            skip_space();
            if (position_ < length_ && source_[position_] == '[') index = parse_brack_group();
            Item radicand = parse_script_arg();
            ElementBuilder elem = builder_.element("radical");
            if (item_present(index)) elem.attr("index", index);
            if (item_present(radicand)) elem.attr("radicand", radicand);
            return elem.final();
        }
        if (is_big_operator(name)) {
            ElementBuilder elem = builder_.element("big_operator");
            elem.attr("op", builder_.createStringItem(full));
            skip_space();
            while (position_ < length_ && (source_[position_] == '_' || source_[position_] == '^')) {
                char marker = source_[position_++];
                Item value = parse_script_arg();
                if (marker == '_') elem.attr("lower", value);
                else elem.attr("upper", value);
                skip_space();
            }
            return elem.final();
        }
        if (strcmp(name, "left") == 0) return parse_delimiter_group(full);
        if (strcmp(name, "begin") == 0) return parse_environment();
        if (strcmp(name, "text") == 0 || strcmp(name, "mbox") == 0 || strcmp(name, "hbox") == 0) {
            return parse_text_command(full);
        }
        if (is_text_command(name)) return parse_text_command(full);
        if (is_style_command(name)) {
            Item arg = parse_script_arg();
            ElementBuilder elem = builder_.element("style_command");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(arg)) elem.attr("arg", arg);
            return elem.final();
        }
        if (is_spacing_command(name)) {
            ElementBuilder elem = builder_.element("space_command");
            elem.attr("value", builder_.createStringItem(full));
            skip_space();
            if (position_ < length_ && source_[position_] == '{') {
                Item arg = parse_group();
                if (item_present(arg)) elem.attr("arg", arg);
            }
            return elem.final();
        }
        if (strcmp(name, "color") == 0 || strcmp(name, "textcolor") == 0) {
            Item options = ItemNull;
            skip_space();
            if (position_ < length_ && source_[position_] == '[') options = parse_brack_group();
            Item content = parse_script_arg();
            ElementBuilder elem = builder_.element("color_command");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(options)) elem.attr("options", options);
            if (item_present(content)) elem.attr("content", content);
            return elem.final();
        }
        if (strcmp(name, "hat") == 0 || strcmp(name, "bar") == 0 || strcmp(name, "vec") == 0 ||
            strcmp(name, "dot") == 0 || strcmp(name, "ddot") == 0 || strcmp(name, "tilde") == 0 ||
            strcmp(name, "overline") == 0 || strcmp(name, "underline") == 0 || strcmp(name, "mathring") == 0) {
            Item base = parse_script_arg();
            ElementBuilder elem = builder_.element("accent");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(base)) elem.attr("base", base);
            return elem.final();
        }
        if (is_greek_letter(name) || is_math_operator(name) || is_trig_function(name) || is_log_function(name)) {
            ElementBuilder elem = builder_.element(is_trig_function(name) || is_log_function(name) ? "command" : "symbol_command");
            elem.attr("name", builder_.createStringItem(name));
            return elem.final();
        }
        if (strcmp(name, "right") == 0 || strcmp(name, "limits") == 0 || strcmp(name, "nolimits") == 0) {
            return builder_.createSymbolItem(name);
        }
        if (strcmp(name, ",") == 0 || strcmp(name, ":") == 0 || strcmp(name, ";") == 0 || strcmp(name, "!") == 0) {
            ElementBuilder elem = builder_.element("space_command");
            elem.attr("value", builder_.createStringItem(full));
            return elem.final();
        }
        if (strlen(name) == 1 && strchr("{}[]()$%#&_", name[0])) {
            return builder_.createStringItem(name, 1);
        }
        ElementBuilder elem = builder_.element("command");
        elem.attr("name", builder_.createStringItem(name));
        for (;;) {
            skip_space();
            if (position_ < length_ && source_[position_] == '{') {
                elem.child(parse_group());
            } else if (position_ < length_ && source_[position_] == '[') {
                elem.child(parse_brack_group());
            } else break;
        }
        return elem.final();
    }

    Item parse_text_command(const char* full) {
        skip_space();
        size_t begin = 0, end = 0;
        ElementBuilder elem = builder_.element("text_command");
        elem.attr("cmd", builder_.createStringItem(full));
        if (consume_group_span('{', '}', &begin, &end)) {
            elem.attr("content", builder_.createStringItem(source_ + begin, end - begin));
        }
        return elem.final();
    }

    Item parse_delimiter_token() {
        skip_space();
        if (position_ >= length_) return builder_.createStringItem(".");
        if (source_[position_] == '\\') {
            char name[96], full[104];
            read_command(name, sizeof(name), full, sizeof(full));
            return builder_.createStringItem(full);
        }
        return builder_.createStringItem(source_ + position_++, 1);
    }

    Item parse_delimiter_group(const char* full) {
        (void)full;
        Item left = parse_delimiter_token();
        ElementBuilder elem = builder_.element("delimiter_group");
        elem.attr("left", left);
        for (;;) {
            skip_space();
            size_t save = position_;
            if (position_ < length_ && source_[position_] == '\\') {
                char name[96], command[104];
                read_command(name, sizeof(name), command, sizeof(command));
                if (strcmp(name, "right") == 0) {
                    elem.attr("right", parse_delimiter_token());
                    return elem.final();
                }
                position_ = save;
            }
            Item child = parse_atom_with_scripts();
            if (!item_present(child)) break;
            elem.child(child);
        }
        error("missing \\right delimiter");
        elem.attr("right", builder_.createStringItem("."));
        return elem.final();
    }

    size_t find_environment_end(const char* name, size_t from, size_t* body_end,
                                bool* found) {
        if (found) *found = false;
        size_t name_len = strlen(name);
        for (size_t i = from; i + 5 + name_len < length_; i++) {
            if (source_[i] != '\\' || !starts_with(source_, length_, i, "\\end{")) continue;
            size_t name_start = i + 5;
            if (memcmp(source_ + name_start, name, name_len) == 0 && source_[name_start + name_len] == '}') {
                if (body_end) *body_end = i;
                if (found) *found = true;
                return name_start + name_len + 1;
            }
        }
        return length_;
    }

    Item parse_environment() {
        skip_space();
        size_t name_begin = 0, name_end = 0;
        if (!consume_group_span('{', '}', &name_begin, &name_end)) return ItemNull;
        char env_name[96];
        size_t env_len = name_end - name_begin;
        if (env_len >= sizeof(env_name)) env_len = sizeof(env_name) - 1;
        memcpy(env_name, source_ + name_begin, env_len);
        env_name[env_len] = '\0';
        ElementBuilder elem = builder_.element("environment");
        elem.attr("name", builder_.createStringItem(env_name));
        if (strcmp(env_name, "array") == 0 && position_ < length_ && source_[position_] == '{') {
            size_t columns_begin = 0, columns_end = 0;
            if (consume_group_span('{', '}', &columns_begin, &columns_end)) {
                elem.attr("columns", builder_.createStringItem(source_ + columns_begin,
                                                                 columns_end - columns_begin));
            }
        }
        size_t body_end = length_;
        bool found_end = false;
        size_t after_end = find_environment_end(env_name, position_, &body_end, &found_end);
        if (!found_end) error("missing \\end environment");
        const char* body_source = source_ + position_;
        size_t body_len = body_end >= position_ ? body_end - position_ : 0;
        ElementBuilder body = builder_.element("env_body");
        DirectMathParser nested(ctx_, body_source, body_len, offset_ + position_, ascii_);
        nested.parse_into(body, true);
        elem.attr("body", body.final());
        position_ = after_end;
        return elem.final();
    }

    void parse_children(ElementBuilder& parent, bool matrix_mode) {
        while (position_ < length_) {
            skip_space();
            if (position_ >= length_) break;
            if (matrix_mode && source_[position_] == '&') {
                position_++;
                parent.child(builder_.createSymbolItem("col_sep"));
                continue;
            }
            if (matrix_mode && starts_with(source_, length_, position_, "\\\\")) {
                position_ += 2;
                parent.child(builder_.createSymbolItem("row_sep"));
                continue;
            }
            Item child = parse_atom_with_scripts();
            if (item_present(child)) parent.child(child);
            else if (position_ < length_) position_++;
        }
    }
};

class DirectLatexParser {
public:
    DirectLatexParser(InputContext& context)
        : ctx_(context), builder_(context.builder), source_(context.source()),
          length_(context.source_length()), position_(0) {}

    Item parse() {
        ElementBuilder root = builder_.element("latex_document");
        parse_children(root, 0);
        return root.final();
    }

private:
    InputContext& ctx_;
    MarkBuilder& builder_;
    const char* source_;
    size_t length_;
    size_t position_;

    void error(const char* message) {
        ctx_.tracker.seek(position_);
        ctx_.addError(ctx_.location(), "latex_c: %s", message);
    }

    void append_text(ElementBuilder& parent, size_t begin, size_t end) {
        if (end <= begin) return;
        parent.text(source_ + begin, end - begin);
    }

    bool consume_group_span(size_t* content_start, size_t* content_end) {
        if (position_ >= length_ || source_[position_] != '{') return false;
        size_t end = latex_scan_group_end(source_, length_, position_, '{', '}',
                                          content_start, content_end);
        if (end != 0) {
            position_ = end;
            return true;
        }
        error("unterminated document group");
        position_ = length_;
        return false;
    }

    bool read_command(char* name, size_t name_capacity, char* full, size_t full_capacity) {
        size_t end = latex_scan_command(source_, length_, position_, name,
                                         name_capacity, full, full_capacity);
        if (end == 0) return false;
        position_ = end;
        return true;
    }

    Item parse_group() {
        size_t begin = 0, end = 0;
        if (!consume_group_span(&begin, &end)) return ItemNull;
        ElementBuilder group = builder_.element("curly_group");
        DirectLatexParser nested(ctx_);
        nested.source_ = source_ + begin;
        nested.length_ = end - begin;
        nested.position_ = 0;
        nested.parse_children(group, 0);
        return group.final();
    }

    size_t find_environment_end(const char* name, size_t from, size_t* body_end,
                                bool* found) {
        if (found) *found = false;
        size_t name_len = strlen(name);
        for (size_t i = from; i + 5 + name_len < length_; i++) {
            if (!starts_with(source_, length_, i, "\\end{")) continue;
            size_t name_start = i + 5;
            if (memcmp(source_ + name_start, name, name_len) == 0 && source_[name_start + name_len] == '}') {
                if (body_end) *body_end = i;
                if (found) *found = true;
                return name_start + name_len + 1;
            }
        }
        return length_;
    }

    Item parse_environment(const char* name) {
        ElementBuilder elem = builder_.element(name);
        size_t body_end = length_;
        bool found_end = false;
        if ((strcmp(name, "tabular") == 0 || strcmp(name, "array") == 0) && position_ < length_ && source_[position_] == '{') {
            size_t columns_begin = 0, columns_end = 0;
            if (consume_group_span(&columns_begin, &columns_end)) {
                elem.attr("columns", builder_.createStringItem(source_ + columns_begin, columns_end - columns_begin));
            }
        }
        size_t after_end = find_environment_end(name, position_, &body_end, &found_end);
        if (!found_end) error("missing \\end environment");
        const char* body_source = source_ + position_;
        size_t body_len = body_end >= position_ ? body_end - position_ : 0;
        if (is_raw_text_environment(name)) {
            elem.text(body_source, body_len);
        } else if (is_math_environment(name)) {
            DirectMathParser math(ctx_, body_source, body_len, position_, false);
            ElementBuilder math_root = builder_.element("math");
            math.parse_into(math_root, true);
            elem.attr("name", builder_.createStringItem(name));
            elem.attr("body", math_root.final());
        } else {
            DirectLatexParser nested(ctx_);
            nested.source_ = body_source;
            nested.length_ = body_len;
            nested.position_ = 0;
            nested.parse_children(elem, 0);
        }
        position_ = after_end;
        return elem.final();
    }

    Item parse_section_command(const char* name) {
        ElementBuilder elem = builder_.element(name);
        size_t begin = 0, end = 0;
        while (position_ < length_ && (source_[position_] == ' ' || source_[position_] == '\t')) position_++;
        if (position_ < length_ && source_[position_] == '[') {
            // optional short title is retained as an attribute.
            position_++;
            begin = position_;
            while (position_ < length_ && source_[position_] != ']') position_++;
            end = position_;
            if (position_ < length_) position_++;
            elem.attr("short", builder_.createStringItem(source_ + begin, end - begin));
        }
        if (consume_group_span(&begin, &end)) {
            DirectLatexParser nested(ctx_);
            nested.source_ = source_ + begin;
            nested.length_ = end - begin;
            nested.position_ = 0;
            ElementBuilder title = builder_.element("curly_group");
            nested.parse_children(title, 0);
            elem.child(title.final());
        }
        return elem.final();
    }

    Item parse_command() {
        char name[96];
        char full[104];
        if (!read_command(name, sizeof(name), full, sizeof(full))) return ItemNull;
        if (name[0] == '\0') return ItemNull;
        if (name[0] == ' ' || name[0] == '\t' || name[0] == '\n') return builder_.createStringItem(" ");
        if (strcmp(name, "begin") == 0) {
            size_t begin = 0, end = 0;
            if (!consume_group_span(&begin, &end)) return ItemNull;
            char env[96];
            size_t env_len = end - begin;
            if (env_len >= sizeof(env)) env_len = sizeof(env) - 1;
            memcpy(env, source_ + begin, env_len);
            env[env_len] = '\0';
            return parse_environment(env);
        }
        if (strcmp(name, "end") == 0) return ItemNull;
        if (strcmp(name, "item") == 0) return builder_.element("item").final();
        if (strcmp(name, "documentclass") == 0 || strcmp(name, "usepackage") == 0) {
            ElementBuilder elem = builder_.element(name);
            size_t begin = 0, end = 0;
            if (position_ < length_ && source_[position_] == '[') {
                position_++;
                begin = position_;
                while (position_ < length_ && source_[position_] != ']') position_++;
                end = position_;
                if (position_ < length_) position_++;
                elem.attr("options", builder_.createStringItem(source_ + begin, end - begin));
            }
            if (consume_group_span(&begin, &end)) elem.attr("name", builder_.createStringItem(source_ + begin, end - begin));
            return elem.final();
        }
        if (strcmp(name, "part") == 0 || strcmp(name, "chapter") == 0 || strcmp(name, "section") == 0 ||
            strcmp(name, "subsection") == 0 || strcmp(name, "subsubsection") == 0 || strcmp(name, "paragraph") == 0 ||
            strcmp(name, "subparagraph") == 0) return parse_section_command(name);
        if (strcmp(name, "textbf") == 0 || strcmp(name, "textit") == 0 || strcmp(name, "texttt") == 0 ||
            strcmp(name, "textrm") == 0 || strcmp(name, "textsf") == 0 || strcmp(name, "emph") == 0 ||
            strcmp(name, "underline") == 0) {
            ElementBuilder elem = builder_.element(name);
            size_t begin = 0, end = 0;
            if (consume_group_span(&begin, &end)) {
                DirectLatexParser nested(ctx_);
                nested.source_ = source_ + begin;
                nested.length_ = end - begin;
                nested.position_ = 0;
                nested.parse_children(elem, 0);
            }
            return elem.final();
        }
        if (strcmp(name, "\\") == 0) return builder_.createSymbolItem("row_sep");
        if (strlen(name) == 1 && strchr("$%#&_{}", name[0])) return builder_.createStringItem(name, 1);
        if (strcmp(name, ",") == 0 || strcmp(name, ";") == 0 || strcmp(name, ":") == 0 || strcmp(name, "!") == 0 ||
            strcmp(name, "quad") == 0 || strcmp(name, "qquad") == 0) return builder_.createSymbolItem(name);
        ElementBuilder elem = builder_.element(name);
        size_t begin = 0, end = 0;
        while (position_ < length_) {
            if (source_[position_] == '{') {
                if (!consume_group_span(&begin, &end)) break;
                DirectLatexParser nested(ctx_);
                nested.source_ = source_ + begin;
                nested.length_ = end - begin;
                nested.position_ = 0;
                ElementBuilder group = builder_.element("curly_group");
                nested.parse_children(group, 0);
                elem.child(group.final());
            } else if (source_[position_] == '[') {
                position_++;
                begin = position_;
                while (position_ < length_ && source_[position_] != ']') position_++;
                end = position_;
                if (position_ < length_) position_++;
                elem.child(builder_.createStringItem(source_ + begin, end - begin));
            } else break;
        }
        return elem.final();
    }

    void parse_delimited_math(ElementBuilder& parent, const char* close_seq, size_t close_len) {
        size_t math_start = position_ + 2;
        size_t math_end = math_start;
        while (math_end + close_len <= length_ && !starts_with(source_, length_, math_end, close_seq)) math_end++;
        bool found_close = math_end + close_len <= length_;
        if (!found_close) error("missing math delimiter");
        DirectMathParser math(ctx_, source_ + math_start, math_end - math_start, math_start, false);
        parent.child(math.parse());
        position_ = math_end + (found_close ? close_len : 0);
    }

    void parse_children(ElementBuilder& parent, int stop) {
        size_t text_begin = position_;
        while (position_ < length_) {
            char c = source_[position_];
            if (stop && c == (char)stop) break;
            if (c == '%') {
                append_text(parent, text_begin, position_);
                while (position_ < length_ && source_[position_] != '\n' && source_[position_] != '\r') position_++;
                if (position_ < length_) position_++;
                text_begin = position_;
                continue;
            }
            if (c == '{') {
                append_text(parent, text_begin, position_);
                parent.child(parse_group());
                text_begin = position_;
                continue;
            }
            if (c == '$') {
                append_text(parent, text_begin, position_);
                ++position_;
                bool display = position_ < length_ && source_[position_] == '$';
                if (display) ++position_;
                size_t math_end = position_;
                while (math_end < length_) {
                    if (source_[math_end] == '$' && (!display || (math_end + 1 < length_ && source_[math_end + 1] == '$'))) break;
                    math_end++;
                }
                bool found_close = math_end < length_;
                if (!found_close) error("missing dollar math delimiter");
                DirectMathParser math(ctx_, source_ + position_, math_end - position_, position_, false);
                parent.child(math.parse());
                position_ = found_close ? math_end + (display ? 2 : 1) : math_end;
                text_begin = position_;
                continue;
            }
            if (c == '\\') {
                append_text(parent, text_begin, position_);
                if (starts_with(source_, length_, position_, "\\(")) {
                    parse_delimited_math(parent, "\\)", 2);
                    text_begin = position_;
                    continue;
                }
                if (starts_with(source_, length_, position_, "\\[")) {
                    parse_delimited_math(parent, "\\]", 2);
                    text_begin = position_;
                    continue;
                }
                Item command = parse_command();
                if (item_present(command)) parent.child(command);
                text_begin = position_;
                continue;
            }
            if (c == '\n' && position_ + 1 < length_ && source_[position_ + 1] == '\n') {
                append_text(parent, text_begin, position_);
                parent.child(builder_.createSymbolItem("parbreak"));
                position_ += 2;
                text_begin = position_;
                continue;
            }
            position_++;
        }
        append_text(parent, text_begin, position_);
    }
};

} // namespace

extern "C" Item parse_math_direct_to_ast(Input* input, const char* math_source, size_t math_len, const char* flavor) {
    if (!input || !math_source) return ItemNull;
    InputContext context(input, math_source, math_len);
    bool ascii = flavor && (strcmp(flavor, "ascii") == 0 || strcmp(flavor, "asciimath") == 0);
    DirectMathParser parser(context, context.source(), context.source_length(), 0, ascii);
    Item result = parser.parse();
    if (context.hasErrors()) context.logErrors();
    return result;
}

void parse_latex_direct(Input* input, const char* latex_string) {
    if (!input || !latex_string) return;
    InputContext context(input, latex_string);
    DirectLatexParser parser(context);
    input->root = parser.parse();
    if (context.hasErrors()) context.logErrors();
}
