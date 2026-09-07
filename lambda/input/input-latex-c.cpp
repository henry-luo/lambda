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
        strcmp(name, "mathbb") == 0 || strcmp(name, "mathfrak") == 0 ||
        strcmp(name, "mathscr") == 0 || strcmp(name, "mathnormal") == 0 ||
        strcmp(name, "operatorname") == 0 || strcmp(name, "displaystyle") == 0 ||
        strcmp(name, "textstyle") == 0 || strcmp(name, "scriptstyle") == 0 ||
        strcmp(name, "scriptscriptstyle") == 0;
}

static bool is_spacing_command(const char* name) {
    return strcmp(name, "quad") == 0 || strcmp(name, "qquad") == 0 ||
        strcmp(name, "hspace") == 0 || strcmp(name, "vspace") == 0 ||
        strcmp(name, "hskip") == 0 || strcmp(name, "vskip") == 0 ||
        strcmp(name, "kern") == 0 || strcmp(name, "mkern") == 0;
}

static bool is_sized_delimiter_command(const char* name) {
    return strcmp(name, "big") == 0 || strcmp(name, "bigl") == 0 ||
        strcmp(name, "bigr") == 0 || strcmp(name, "bigm") == 0 ||
        strcmp(name, "Big") == 0 || strcmp(name, "Bigl") == 0 ||
        strcmp(name, "Bigr") == 0 || strcmp(name, "Bigm") == 0 ||
        strcmp(name, "bigg") == 0 || strcmp(name, "biggl") == 0 ||
        strcmp(name, "biggr") == 0 || strcmp(name, "biggm") == 0 ||
        strcmp(name, "Bigg") == 0 || strcmp(name, "Biggl") == 0 ||
        strcmp(name, "Biggr") == 0 || strcmp(name, "Biggm") == 0;
}

static bool is_box_command(const char* name) {
    return strcmp(name, "bbox") == 0 || strcmp(name, "boxed") == 0 ||
        strcmp(name, "fbox") == 0 || strcmp(name, "llap") == 0 ||
        strcmp(name, "rlap") == 0 || strcmp(name, "clap") == 0 ||
        strcmp(name, "mathllap") == 0 || strcmp(name, "mathrlap") == 0 ||
        strcmp(name, "mathclap") == 0;
}

static bool is_infix_fraction_command(const char* name) {
    return strcmp(name, "over") == 0 || strcmp(name, "atop") == 0 ||
        strcmp(name, "above") == 0 || strcmp(name, "choose") == 0 ||
        strcmp(name, "brace") == 0 || strcmp(name, "brack") == 0;
}

static bool is_supported_math_environment(const char* name) {
    return strcmp(name, "array") == 0 || strcmp(name, "matrix") == 0 ||
        strcmp(name, "pmatrix") == 0 || strcmp(name, "bmatrix") == 0 ||
        strcmp(name, "Bmatrix") == 0 || strcmp(name, "vmatrix") == 0 ||
        strcmp(name, "Vmatrix") == 0 || strcmp(name, "cases") == 0 ||
        strcmp(name, "rcases") == 0 || strcmp(name, "dcases") == 0 ||
        strcmp(name, "aligned") == 0 || strcmp(name, "align") == 0 ||
        strcmp(name, "equation") == 0 || strcmp(name, "smallmatrix") == 0;
}

class DirectMathParser {
public:
    DirectMathParser(InputContext& context, const char* source, size_t length,
                     size_t source_offset, bool ascii, bool allow_infix = true)
        : ctx_(context), builder_(context.builder), source_(source), length_(length),
          offset_(source_offset), position_(0), ascii_(ascii), allow_infix_(allow_infix) {}

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
    bool allow_infix_;

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
        if (open == '{') {
            // MathLive keeps an unterminated command argument as recoverable
            // input (for example, `\\rule{` still produces its default rule).
            // Preserve the remaining source instead of rejecting the formula.
            *content_start = position_ < length_ ? position_ + 1 : length_;
            *content_end = length_;
            position_ = length_;
            return true;
        }
        // A bare unmatched '[' is punctuation, not an empty optional group.
        return false;
    }

    Item parse_group() {
        size_t begin = 0, end = 0;
        if (!consume_group_span('{', '}', &begin, &end)) return ItemNull;
        return parse_group_contents(begin, end);
    }

    Item parse_group_contents(size_t begin, size_t end) {
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
        if (!ascii_) {
            // TeX takes precisely one token for an unbraced argument: in
            // `\\frac57`, 5 is the numerator and 7 is the denominator.
            // parse_primary() would consume the full numeric or word run.
            if (source_[position_] == '\\') return parse_command();
            if (isdigit((unsigned char)source_[position_]) || isalpha((unsigned char)source_[position_])) {
                return builder_.createStringItem(source_ + position_++, 1);
            }
            return parse_punctuation_or_operator();
        }
        return parse_primary();
    }

    Item parse_optional_script_arg() {
        skip_space();
        if (position_ >= length_) return ItemNull;
        return parse_script_arg();
    }

    Item parse_color_content() {
        skip_space();
        if (position_ >= length_ || source_[position_] != '{') return parse_script_arg();
        size_t begin = 0, end = 0;
        if (!consume_group_span('{', '}', &begin, &end)) return ItemNull;
        size_t first = begin;
        size_t last = end;
        while (first < last && isspace((unsigned char)source_[first])) first++;
        while (last > first && isspace((unsigned char)source_[last - 1])) last--;
        if (last - first >= 2 && source_[first] == '$' && source_[last - 1] == '$') {
            // Color commands inherit text-mode `$...$` switches.  The markers
            // delimit the embedded math; MathLive does not emit them as glyphs.
            return parse_group_contents(first + 1, last - 1);
        }
        return parse_group_contents(begin, end);
    }

    Item parse_dimension_arg() {
        skip_space();
        if (position_ >= length_) return ItemNull;
        if (source_[position_] == '{') {
            size_t begin = 0;
            size_t end = 0;
            if (!consume_group_span('{', '}', &begin, &end)) return ItemNull;
            return builder_.createStringItem(source_ + begin, end - begin);
        }
        size_t begin = position_;
        while (position_ < length_ && !isspace((unsigned char)source_[position_])) position_++;
        return builder_.createStringItem(source_ + begin, position_ - begin);
    }

    Item parse_atom_with_scripts() {
        Item base = parse_primary();
        if (!item_present(base)) return base;
        Item sub = ItemNull;
        Item sup = ItemNull;
        const char* modifier = nullptr;
        for (;;) {
            skip_space();
            if (position_ < length_ && source_[position_] == '\\') {
                char modifier_name[96];
                char modifier_full[104];
                size_t end = latex_scan_command(source_, length_, position_, modifier_name,
                                                sizeof(modifier_name), modifier_full,
                                                sizeof(modifier_full));
                if (end != 0 && (strcmp(modifier_name, "limits") == 0 ||
                                 strcmp(modifier_name, "nolimits") == 0)) {
                    position_ = end;
                    modifier = strcmp(modifier_name, "limits") == 0 ? "limits" : "nolimits";
                    continue;
                }
            }
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
        if (modifier != nullptr) result.attr("modifier", builder_.createStringItem(modifier));
        return result.final();
    }

    Item parse_primary() {
        skip_space();
        if (position_ >= length_) return ItemNull;
        char c = source_[position_];
        if (c == '{') return parse_group();
        if (c == '[') {
            Item group = parse_brack_group();
            return item_present(group) ? group : parse_punctuation_or_operator();
        }
        if (c == '\\') return parse_command();
        if ((unsigned char)c >= 0x80) {
            size_t start = position_;
            while (position_ < length_ && (unsigned char)source_[position_] >= 0x80) position_++;
            ElementBuilder elem = builder_.element("unicode_text");
            elem.attr("value", builder_.createStringItem(source_ + start, position_ - start));
            return elem.final();
        }
        if (c == '#' || c == '!') {
            size_t start = position_++;
            while (position_ < length_ &&
                   (source_[position_] == '#' || source_[position_] == '!' ||
                    isdigit((unsigned char)source_[position_]))) position_++;
            // Color-literal punctuation and digits stay in one upright atom;
            // splitting them would introduce binary-operator spacing.
            ElementBuilder elem = builder_.element("raw_math_text");
            elem.attr("value", builder_.createStringItem(source_ + start, position_ - start));
            return elem.final();
        }
        if (isdigit((unsigned char)c) || (c == '.' && position_ + 1 < length_ && isdigit((unsigned char)source_[position_ + 1]))) {
            size_t start = position_++;
            while (position_ < length_ && (isdigit((unsigned char)source_[position_]) || source_[position_] == '.')) position_++;
            return builder_.createStringItem(source_ + start, position_ - start);
        }
        if (isalpha((unsigned char)c)) {
            size_t start = position_++;
            // TeX math makes each bare letter its own ordinary atom. ASCII
            // math keeps multi-letter identifiers (for example, `alpha`).
            if (ascii_) {
                while (position_ < length_ && isalpha((unsigned char)source_[position_])) position_++;
            }
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
        if (!allow_infix_ && is_infix_fraction_command(name)) return ItemNull;

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
            // Fraction rendering shares the numerator/denominator contract for
            // bar and no-bar forms; `top`/`bottom` left binomials empty.
            if (item_present(top)) elem.attr("numer", top);
            if (item_present(bottom)) elem.attr("denom", bottom);
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
        if (strcmp(name, "xrightarrow") == 0 || strcmp(name, "xleftarrow") == 0) {
            Item lower = ItemNull;
            skip_space();
            if (position_ < length_ && source_[position_] == '[') lower = parse_brack_group();
            Item upper = parse_script_arg();
            ElementBuilder elem = builder_.element("extended_arrow");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(upper)) elem.attr("upper", upper);
            if (item_present(lower)) elem.attr("lower", lower);
            return elem.final();
        }
        if (strcmp(name, "mathop") == 0) {
            Item body = parse_script_arg();
            ElementBuilder elem = builder_.element("mathop");
            if (item_present(body)) elem.attr("body", body);
            return elem.final();
        }
        if (strcmp(name, "stackrel") == 0 || strcmp(name, "overset") == 0 ||
            strcmp(name, "underset") == 0) {
            Item annotation = parse_script_arg();
            Item base = parse_script_arg();
            ElementBuilder elem = builder_.element("overunder_command");
            elem.attr("cmd", builder_.createStringItem(strcmp(name, "stackrel") == 0 ? "\\overset" : full));
            if (item_present(annotation)) elem.attr("annotation", annotation);
            if (item_present(base)) elem.attr("base", base);
            return elem.final();
        }
        if (strcmp(name, "left") == 0) return parse_delimiter_group(full);
        if (is_sized_delimiter_command(name)) {
            ElementBuilder elem = builder_.element("sized_delimiter");
            elem.attr("size", builder_.createStringItem(full));
            if (has_sized_delimiter_token()) elem.attr("delim", parse_delimiter_token());
            return elem.final();
        }
        if (strcmp(name, "begin") == 0) return parse_environment();
        if (strcmp(name, "text") == 0 || strcmp(name, "mbox") == 0 || strcmp(name, "hbox") == 0) {
            return parse_text_command(full);
        }
        if (is_text_command(name)) return parse_text_command(full);
        if (is_style_command(name)) {
            ElementBuilder elem = builder_.element("style_command");
            elem.attr("cmd", builder_.createStringItem(full));
            bool declaration = strcmp(name, "displaystyle") == 0 ||
                strcmp(name, "textstyle") == 0 || strcmp(name, "scriptstyle") == 0 ||
                strcmp(name, "scriptscriptstyle") == 0;
            if (!declaration) {
                Item arg = parse_script_arg();
                if (item_present(arg)) elem.attr("arg", arg);
            }
            return elem.final();
        }
        if (is_spacing_command(name)) {
            ElementBuilder elem = builder_.element("space_command");
            bool dimensional = strcmp(name, "hspace") == 0 || strcmp(name, "vspace") == 0 ||
                strcmp(name, "hskip") == 0 || strcmp(name, "vskip") == 0 ||
                strcmp(name, "kern") == 0 || strcmp(name, "mkern") == 0;
            if (dimensional) {
                elem.attr("cmd", builder_.createStringItem(full));
                Item value = parse_dimension_arg();
                if (item_present(value)) elem.attr("value", value);
            } else {
                elem.attr("value", builder_.createStringItem(full));
            }
            return elem.final();
        }
        if (strcmp(name, "color") == 0 || strcmp(name, "textcolor") == 0 || strcmp(name, "colorbox") == 0) {
            Item options = ItemNull;
            skip_space();
            if (position_ < length_ && source_[position_] == '[') options = parse_brack_group();
            Item color = parse_script_arg();
            Item content = ItemNull;
            if (strcmp(name, "textcolor") == 0 || strcmp(name, "colorbox") == 0) content = parse_color_content();
            ElementBuilder elem = builder_.element(strcmp(name, "color") == 0 ?
                                                     "color_switch" : "color_command");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(options)) elem.attr("options", options);
            if (item_present(color)) elem.attr("color", color);
            if (item_present(content)) elem.attr("content", content);
            return elem.final();
        }
        if (is_box_command(name)) {
            Item options = ItemNull;
            skip_space();
            if (strcmp(name, "bbox") == 0 && position_ < length_ && source_[position_] == '[') {
                options = parse_brack_group();
            }
            Item content = parse_optional_script_arg();
            ElementBuilder elem = builder_.element("box_command");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(options)) elem.attr("options", options);
            if (item_present(content)) elem.attr("content", content);
            return elem.final();
        }
        if (strcmp(name, "hat") == 0 || strcmp(name, "bar") == 0 || strcmp(name, "vec") == 0 ||
            strcmp(name, "dot") == 0 || strcmp(name, "ddot") == 0 || strcmp(name, "dddot") == 0 ||
            strcmp(name, "ddddot") == 0 || strcmp(name, "tilde") == 0 || strcmp(name, "acute") == 0 ||
            strcmp(name, "breve") == 0 || strcmp(name, "check") == 0 || strcmp(name, "grave") == 0 ||
            strcmp(name, "widehat") == 0 || strcmp(name, "widetilde") == 0 ||
            strcmp(name, "overline") == 0 || strcmp(name, "underline") == 0 ||
            strcmp(name, "overbrace") == 0 || strcmp(name, "underbrace") == 0 ||
            strcmp(name, "overrightarrow") == 0 || strcmp(name, "underrightarrow") == 0 ||
            strcmp(name, "overleftarrow") == 0 || strcmp(name, "underleftarrow") == 0 ||
            strcmp(name, "overleftrightarrow") == 0 || strcmp(name, "underleftrightarrow") == 0 ||
            strcmp(name, "mathring") == 0) {
            Item base = parse_optional_script_arg();
            ElementBuilder elem = builder_.element("accent");
            elem.attr("cmd", builder_.createStringItem(full));
            if (item_present(base)) elem.attr("base", base);
            return elem.final();
        }
        if (strcmp(name, "ne") == 0 || strcmp(name, "neq") == 0) {
            ElementBuilder elem = builder_.element("not_overlay");
            elem.attr("target", builder_.createStringItem("="));
            return elem.final();
        }
        if (is_greek_letter(name) || is_math_operator(name) || is_trig_function(name) || is_log_function(name)) {
            ElementBuilder elem = builder_.element(is_big_operator(name) || is_trig_function(name) || is_log_function(name) ? "command" : "symbol_command");
            elem.attr("name", builder_.createStringItem(name));
            return elem.final();
        }
        if (strcmp(name, "right") == 0) {
            ElementBuilder elem = builder_.element("malformed_right");
            elem.attr("delim", parse_delimiter_token());
            return elem.final();
        }
        if (strcmp(name, "end") == 0) {
            // An unmatched closing environment is an error token; leave its
            // following group for ordinary math parsing, as MathLive does.
            return builder_.element("malformed_end").final();
        }
        if (strcmp(name, "limits") == 0 || strcmp(name, "nolimits") == 0) {
            return builder_.createSymbolItem(name);
        }
        if (strcmp(name, ",") == 0 || strcmp(name, ":") == 0 || strcmp(name, ";") == 0 || strcmp(name, "!") == 0) {
            ElementBuilder elem = builder_.element("space_command");
            elem.attr("value", builder_.createStringItem(full));
            return elem.final();
        }
        if (strcmp(name, "not") == 0) {
            skip_space();
            if (position_ + 1 < length_ && source_[position_] == '{' && source_[position_ + 1] == '}') {
                // Keep the empty-argument spelling distinct: adjacent
                // `\not{}` targets are coalesced by MathLive's renderer.
                position_ += 2;
                return builder_.element("not_empty").final();
            }
            if (position_ < length_ && source_[position_] == '\\') {
                char target_name[96];
                char target_full[104];
                if (read_command(target_name, sizeof(target_name), target_full, sizeof(target_full))) {
                    ElementBuilder elem = builder_.element("not_overlay");
                    elem.attr("target", builder_.createStringItem(target_full));
                    return elem.final();
                }
            }
            if (position_ < length_ && strchr("=<>|", source_[position_]) != nullptr) {
                ElementBuilder elem = builder_.element("not_overlay");
                elem.attr("target", builder_.createStringItem(source_ + position_, 1));
                position_++;
                return elem.final();
            }
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

    bool has_sized_delimiter_token() {
        skip_space();
        if (position_ >= length_) return false;
        char c = source_[position_];
        if (strchr("()[]{}|<>/.", c) != nullptr) return true;
        if (c != '\\') return false;
        char name[96];
        char full[104];
        size_t end = latex_scan_command(source_, length_, position_, name,
                                        sizeof(name), full, sizeof(full));
        if (end == 0) return false;
        return strcmp(name, "{") == 0 || strcmp(name, "}") == 0 ||
            strcmp(name, "|") == 0 || strcmp(name, "vert") == 0 ||
            strcmp(name, "Vert") == 0 || strcmp(name, "lvert") == 0 ||
            strcmp(name, "rvert") == 0 || strcmp(name, "lVert") == 0 ||
            strcmp(name, "rVert") == 0 || strcmp(name, "langle") == 0 ||
            strcmp(name, "rangle") == 0;
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
                if (strcmp(name, "middle") == 0) {
                    ElementBuilder middle = builder_.element("middle_delim");
                    middle.attr("delim", parse_delimiter_token());
                    elem.child(middle.final());
                    continue;
                }
                position_ = save;
            }
            Item child = parse_atom_with_scripts();
            if (!item_present(child)) break;
            elem.child(child);
        }
        // MathLive recovers incomplete delimiter pairs with a null right side.
        elem.attr("right", builder_.createStringItem("."));
        return elem.final();
    }

    bool find_infix_fraction(size_t* command_begin, size_t* command_end,
                             char* command, size_t command_capacity) {
        size_t brace_depth = 0;
        for (size_t cursor = position_; cursor < length_;) {
            char c = source_[cursor];
            if (c == '{') {
                brace_depth++;
                cursor++;
                continue;
            }
            if (c == '}') {
                if (brace_depth > 0) brace_depth--;
                cursor++;
                continue;
            }
            if (c != '\\') {
                cursor++;
                continue;
            }

            char name[96];
            char full[104];
            size_t end = latex_scan_command(source_, length_, cursor, name,
                                            sizeof(name), full, sizeof(full));
            if (end == 0) {
                cursor++;
                continue;
            }
            if (brace_depth == 0 && is_infix_fraction_command(name)) {
                *command_begin = cursor;
                *command_end = end;
                size_t command_len = strlen(full);
                if (command_len >= command_capacity) command_len = command_capacity - 1;
                memcpy(command, full, command_len);
                command[command_len] = '\0';
                return true;
            }
            cursor = end;
        }
        return false;
    }

    Item parse_infix_fraction_side(size_t begin, size_t end, bool allow_infix) {
        DirectMathParser nested(ctx_, source_ + begin, end - begin, offset_ + begin, ascii_, allow_infix);
        ElementBuilder group = builder_.element("group");
        nested.parse_into(group, false);
        return group.final();
    }

    bool parse_infix_fraction(ElementBuilder& parent) {
        if (!allow_infix_ || ascii_ || position_ != 0) return false;
        size_t command_begin = 0;
        size_t command_end = 0;
        char command[104];
        if (!find_infix_fraction(&command_begin, &command_end, command, sizeof(command))) return false;

        // TeX scopes one infix fraction to its enclosing math list. Nested
        // braces start their own scope, but another unbraced infix operator is
        // recovery text rather than a recursively nested denominator fraction.
        ElementBuilder fraction = builder_.element("infix_frac");
        fraction.attr("cmd", builder_.createStringItem(command));
        fraction.attr("numer", parse_infix_fraction_side(position_, command_begin, false));
        fraction.attr("denom", parse_infix_fraction_side(command_end, length_, false));
        parent.child(fraction.final());
        position_ = length_;
        return true;
    }

    size_t find_environment_end(const char* name, size_t from, size_t* body_end,
                                bool* found, bool* recovered) {
        if (found) *found = false;
        if (recovered) *recovered = false;
        size_t name_len = strlen(name);
        size_t recovery_end = 0;
        size_t recovery_body_end = 0;
        for (size_t i = from; i + 5 < length_; i++) {
            if (source_[i] != '\\' || !starts_with(source_, length_, i, "\\end{")) continue;
            size_t name_start = i + 5;
            size_t end_name_start = 0;
            size_t end_name_end = 0;
            size_t after_end = latex_scan_group_end(source_, length_, i + 4, '{', '}',
                                                     &end_name_start, &end_name_end);
            if (after_end != 0 && recovery_end == 0) {
                // MathLive recovers an unmatched environment by ending at the
                // first closing environment token rather than consuming it as
                // ordinary math content.
                recovery_body_end = i;
                recovery_end = after_end;
            }
            if (name_start + name_len < length_ &&
                memcmp(source_ + name_start, name, name_len) == 0 &&
                source_[name_start + name_len] == '}') {
                if (body_end) *body_end = i;
                if (found) *found = true;
                return name_start + name_len + 1;
            }
        }
        if (recovery_end != 0) {
            if (body_end) *body_end = recovery_body_end;
            if (found) *found = true;
            if (recovered) *recovered = true;
            return recovery_end;
        }
        return length_;
    }

    Item parse_environment() {
        skip_space();
        size_t name_begin = 0, name_end = 0;
        // MathLive treats a bare `\begin` as an empty recoverable construct.
        // Do not turn that local omission into a fatal input parse error.
        if (position_ >= length_ || source_[position_] != '{') return ItemNull;
        if (!consume_group_span('{', '}', &name_begin, &name_end)) return ItemNull;
        char env_name[96];
        size_t env_len = name_end - name_begin;
        if (env_len >= sizeof(env_name)) env_len = sizeof(env_name) - 1;
        memcpy(env_name, source_ + name_begin, env_len);
        env_name[env_len] = '\0';
        if (!is_supported_math_environment(env_name)) {
            // MathLive ignores an unknown opening environment, then lets the
            // following `\end{...}` take its ordinary error-recovery path.
            return builder_.element("ignored_begin").final();
        }
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
        bool recovered_end = false;
        size_t after_end = find_environment_end(env_name, position_, &body_end, &found_end,
                                                &recovered_end);
        if (recovered_end) {
            // A differently named closing token cannot close this environment.
            // Preserve the recoverable tree while exposing MathLive's error class.
            elem.attr("error", builder_.createStringItem("unbalanced-environment"));
        }
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
        if (!matrix_mode && parse_infix_fraction(parent)) return;
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
        parse_children(root, 0, true);
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

    bool consume_brack_group_span(size_t* content_start, size_t* content_end) {
        if (position_ >= length_ || source_[position_] != '[') return false;
        size_t end = latex_scan_group_end(source_, length_, position_, '[', ']',
                                          content_start, content_end);
        if (end != 0) {
            position_ = end;
            return true;
        }
        error("unterminated optional argument");
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
        nested.parse_children(group, 0, false);
        return group.final();
    }

    Item parse_brack_group() {
        size_t begin = 0, end = 0;
        if (!consume_brack_group_span(&begin, &end)) return ItemNull;
        ElementBuilder group = builder_.element("brack_group");
        DirectLatexParser nested(ctx_);
        nested.source_ = source_ + begin;
        nested.length_ = end - begin;
        nested.position_ = 0;
        nested.parse_children(group, 0, false);
        return group.final();
    }

    bool append_group_contents(ElementBuilder& parent, bool empty_marker) {
        size_t begin = 0, end = 0;
        if (!consume_group_span(&begin, &end)) return false;
        if (begin == end && empty_marker) {
            parent.child(builder_.element("curly_group").final());
            return true;
        }
        DirectLatexParser nested(ctx_);
        nested.source_ = source_ + begin;
        nested.length_ = end - begin;
        nested.position_ = 0;
        nested.parse_children(parent, 0, false);
        return true;
    }

    size_t source_offset(const char* cursor) const {
        return (size_t)(cursor - ctx_.source());
    }

    Item make_math_element(const char* math_source, size_t math_length, bool display) {
        ElementBuilder elem = builder_.element(display ? "display_math" : "inline_math");
        elem.attr("source", builder_.createStringItem(math_source, math_length));
        DirectMathParser math(ctx_, math_source, math_length, source_offset(math_source), false);
        Item ast = math.parse();
        if (item_present(ast)) elem.attr("ast", ast);
        return elem.final();
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
            while (body_len > 0 && isspace((unsigned char)*body_source)) {
                body_source++;
                body_len--;
            }
            while (body_len > 0 && isspace((unsigned char)body_source[body_len - 1])) body_len--;
            elem.attr("source", builder_.createStringItem(body_source, body_len));
            DirectMathParser math(ctx_, body_source, body_len, source_offset(body_source), false);
            Item ast = math.parse();
            if (item_present(ast)) elem.attr("ast", ast);
        } else {
            DirectLatexParser nested(ctx_);
            nested.source_ = body_source;
            nested.length_ = body_len;
            nested.position_ = 0;
            nested.parse_children(elem, 0, true);
        }
        position_ = after_end;
        return elem.final();
    }

    Item parse_section_command(const char* name) {
        ElementBuilder elem = builder_.element(strcmp(name, "paragraph") == 0 ? "paragraph_command" : name);
        size_t begin = 0, end = 0;
        while (position_ < length_ && (source_[position_] == ' ' || source_[position_] == '\t')) position_++;
        if (position_ < length_ && source_[position_] == '[') {
            Item toc = parse_brack_group();
            if (item_present(toc)) elem.attr("toc", toc);
        }
        if (consume_group_span(&begin, &end)) {
            DirectLatexParser nested(ctx_);
            nested.source_ = source_ + begin;
            nested.length_ = end - begin;
            nested.position_ = 0;
            ElementBuilder title = builder_.element("curly_group");
            nested.parse_children(title, 0, false);
            elem.attr("title", title.final());
        }
        return elem.final();
    }

    Item parse_command() {
        char name[96];
        char full[104];
        if (!read_command(name, sizeof(name), full, sizeof(full))) return ItemNull;
        if (name[0] == '\0') return ItemNull;
        if (name[0] == ' ' || name[0] == '\t' || name[0] == '\n') return builder_.createStringItem(" ");
        bool starred = position_ < length_ && source_[position_] == '*';
        if (starred) position_++;
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
                nested.parse_children(elem, 0, false);
            }
            return elem.final();
        }
        if (strcmp(name, "\\") == 0) return builder_.createSymbolItem("row_sep");
        if (strlen(name) == 1 && strchr("$%#&_{}", name[0])) return builder_.createStringItem(name, 1);
        if (strcmp(name, ",") == 0 || strcmp(name, ";") == 0 || strcmp(name, ":") == 0 || strcmp(name, "!") == 0 ||
            strcmp(name, "quad") == 0 || strcmp(name, "qquad") == 0) return builder_.createSymbolItem(name);
        char tag[sizeof(name) + 2];
        memcpy(tag, name, strlen(name) + 1);
        if (starred && strcmp(name, "newtheorem") == 0) strcat(tag, "*");
        ElementBuilder elem = builder_.element(tag);
        bool macro_definition = strcmp(name, "newcommand") == 0 ||
            strcmp(name, "renewcommand") == 0 || strcmp(name, "providecommand") == 0 ||
            strcmp(name, "def") == 0 || strcmp(name, "gdef") == 0 ||
            strcmp(name, "edef") == 0 || strcmp(name, "xdef") == 0;
        int curly_index = 0;
        while (position_ < length_) {
            if (source_[position_] == '{') {
                if (macro_definition && curly_index == 0) {
                    size_t begin = 0, end = 0;
                    if (!consume_group_span(&begin, &end)) break;
                    elem.child(builder_.createStringItem(source_ + begin, end - begin));
                } else if (macro_definition) {
                    Item group = parse_group();
                    if (item_present(group)) elem.child(group);
                } else if (!append_group_contents(elem, true)) {
                    break;
                }
                curly_index++;
            } else if (source_[position_] == '[') {
                Item group = parse_brack_group();
                if (item_present(group)) elem.child(group);
            } else break;
        }
        return elem.final();
    }

    void parse_delimited_math(ElementBuilder& parent, const char* close_seq, size_t close_len,
                              bool display) {
        size_t math_start = position_ + 2;
        size_t math_end = math_start;
        while (math_end + close_len <= length_ && !starts_with(source_, length_, math_end, close_seq)) math_end++;
        bool found_close = math_end + close_len <= length_;
        if (!found_close) error("missing math delimiter");
        parent.child(make_math_element(source_ + math_start, math_end - math_start, display));
        position_ = math_end + (found_close ? close_len : 0);
    }

    bool is_block_command() const {
        char name[96];
        char full[104];
        if (latex_scan_command(source_, length_, position_, name, sizeof(name), full, sizeof(full)) == 0) return false;
        return strcmp(name, "begin") == 0 || strcmp(name, "end") == 0 ||
            strcmp(name, "documentclass") == 0 || strcmp(name, "usepackage") == 0 ||
            strcmp(name, "title") == 0 || strcmp(name, "author") == 0 ||
            strcmp(name, "date") == 0 || strcmp(name, "maketitle") == 0 ||
            strcmp(name, "tableofcontents") == 0 || strcmp(name, "part") == 0 ||
            strcmp(name, "chapter") == 0 || strcmp(name, "section") == 0 ||
            strcmp(name, "subsection") == 0 || strcmp(name, "subsubsection") == 0 ||
            strcmp(name, "paragraph") == 0 || strcmp(name, "subparagraph") == 0;
    }

    void parse_inline_children(ElementBuilder& parent, int stop, bool stop_at_block,
                               bool stop_at_parbreak) {
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
                parent.child(make_math_element(source_ + position_, math_end - position_, display));
                position_ = found_close ? math_end + (display ? 2 : 1) : math_end;
                text_begin = position_;
                continue;
            }
            if (c == '\\') {
                if (stop_at_block && is_block_command()) {
                    append_text(parent, text_begin, position_);
                    return;
                }
                append_text(parent, text_begin, position_);
                if (starts_with(source_, length_, position_, "\\(")) {
                    parse_delimited_math(parent, "\\)", 2, false);
                    text_begin = position_;
                    continue;
                }
                if (starts_with(source_, length_, position_, "\\[")) {
                    parse_delimited_math(parent, "\\]", 2, true);
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
                position_ += 2;
                if (stop_at_parbreak) return;
                parent.child(builder_.createSymbolItem("parbreak"));
                text_begin = position_;
                continue;
            }
            position_++;
        }
        append_text(parent, text_begin, position_);
    }

    void parse_children(ElementBuilder& parent, int stop, bool paragraph_mode) {
        if (!paragraph_mode) {
            parse_inline_children(parent, stop, false, false);
            return;
        }
        while (position_ < length_) {
            if (stop && source_[position_] == (char)stop) return;
            if (source_[position_] == '\n' && position_ + 1 < length_ && source_[position_ + 1] == '\n') {
                position_ += 2;
                continue;
            }
            if (source_[position_] == '\\' && is_block_command()) {
                Item command = parse_command();
                if (item_present(command)) parent.child(command);
                continue;
            }
            // A fresh builder preserves document order across paragraph breaks.
            ElementBuilder paragraph = builder_.element("paragraph");
            parse_inline_children(paragraph, stop, true, true);
            parent.child(paragraph.final());
        }
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
