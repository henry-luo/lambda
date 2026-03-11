// format-math-ascii.cpp - ASCII Math formatter for tree-sitter Mark AST
//
// Converts the tree-sitter-latex-math Mark Element AST back to ASCII math text.
// The AST has element tags like: math, subsup, operator, relation, group,
// radical, command, fraction, frac_like, delimiter_group, etc.
// Variables and numbers appear as plain String children.
//
// This formatter handles both LaTeX-parsed and ASCII-parsed math since
// both go through the same unified tree-sitter grammar.

#include "format-math-shared.hpp"

// Forward declarations (format_item/format_element_impl/format_children from shared header):
static void format_children(StringBuf* sb, const ElementReader& elem, int depth, const char* sep);
static void format_children_range(StringBuf* sb, const ElementReader& elem,
                                   int64_t start, int64_t end, int depth, const char* sep);

// ============================================================================
// Command name → ASCII math mapping
// ============================================================================

struct CmdAsciiEntry {
    const char* cmd;    // LaTeX command (without backslash)
    const char* ascii;  // ASCII math output
};

// Greek letters: \alpha → alpha
static const CmdAsciiEntry GREEK_TABLE[] = {
    {"alpha", "alpha"}, {"beta", "beta"}, {"gamma", "gamma"}, {"delta", "delta"},
    {"epsilon", "epsilon"}, {"varepsilon", "epsilon"}, {"zeta", "zeta"},
    {"eta", "eta"}, {"theta", "theta"}, {"vartheta", "theta"},
    {"iota", "iota"}, {"kappa", "kappa"}, {"lambda", "lambda"},
    {"mu", "mu"}, {"nu", "nu"}, {"xi", "xi"},
    {"pi", "pi"}, {"varpi", "pi"}, {"rho", "rho"}, {"varrho", "rho"},
    {"sigma", "sigma"}, {"varsigma", "sigma"}, {"tau", "tau"},
    {"upsilon", "upsilon"}, {"phi", "phi"}, {"varphi", "phi"},
    {"chi", "chi"}, {"psi", "psi"}, {"omega", "omega"},
    {"Gamma", "Gamma"}, {"Delta", "Delta"}, {"Theta", "Theta"},
    {"Lambda", "Lambda"}, {"Xi", "Xi"}, {"Pi", "Pi"},
    {"Sigma", "Sigma"}, {"Upsilon", "Upsilon"}, {"Phi", "Phi"},
    {"Psi", "Psi"}, {"Omega", "Omega"},
    {nullptr, nullptr}
};

// Operator commands: \times → xx, \pm → +-
static const CmdAsciiEntry OP_TABLE[] = {
    {"times", "xx"}, {"cdot", "*"}, {"ast", "**"},
    {"pm", "+-"}, {"mp", "-+"}, {"div", "-:"},
    {"circ", "@"}, {"oplus", "o+"}, {"otimes", "ox"}, {"odot", "o."},
    {"setminus", "\\\\"},
    {"cap", "nn"}, {"cup", "uu"}, {"wedge", "^^"}, {"vee", "vv"},
    {nullptr, nullptr}
};

// Relation commands: \leq → <=, \neq → !=
static const CmdAsciiEntry REL_TABLE[] = {
    {"leq", "<="}, {"le", "<="}, {"geq", ">="}, {"ge", ">="},
    {"neq", "!="}, {"ne", "!="},
    {"equiv", "-="}, {"approx", "~~"}, {"cong", "~="},
    {"prec", "-<"}, {"succ", ">-"},
    {"in", "in"}, {"notin", "!in"},
    {"subset", "sub"}, {"supset", "sup"},
    {"subseteq", "sube"}, {"supseteq", "supe"},
    {"propto", "prop"},
    {"to", "->"}, {"rightarrow", "->"}, {"leftarrow", "<-"},
    {"leftrightarrow", "<->"}, {"Rightarrow", "=>"}, {"Leftrightarrow", "<=>"}, {"mapsto", "|->"},
    {"perp", "_|_"}, {"parallel", "||"},
    {nullptr, nullptr}
};

// Symbol commands: \infty → oo, \emptyset → emptyset
static const CmdAsciiEntry SYM_TABLE[] = {
    {"infty", "oo"}, {"infinity", "oo"},
    {"emptyset", "emptyset"}, {"varnothing", "emptyset"},
    {"partial", "del"}, {"nabla", "grad"},
    {"forall", "AA"}, {"exists", "EE"},
    {"aleph", "aleph"}, {"hbar", "hbar"}, {"ell", "ell"},
    {"ldots", "..."}, {"cdots", "cdots"}, {"dots", "..."},
    {nullptr, nullptr}
};

// Big operator commands: \sum → sum, \int → int
static const CmdAsciiEntry BIGOP_TABLE[] = {
    {"sum", "sum"}, {"prod", "prod"}, {"coprod", "coprod"},
    {"int", "int"}, {"iint", "iint"}, {"iiint", "iiint"}, {"oint", "oint"},
    {"lim", "lim"}, {"limsup", "limsup"}, {"liminf", "liminf"},
    {"sup", "sup"}, {"inf", "inf"}, {"max", "max"}, {"min", "min"},
    {"det", "det"}, {"gcd", "gcd"},
    {nullptr, nullptr}
};

// Function commands: \sin → sin
static const CmdAsciiEntry FUNC_TABLE[] = {
    {"sin", "sin"}, {"cos", "cos"}, {"tan", "tan"},
    {"sec", "sec"}, {"csc", "csc"}, {"cot", "cot"},
    {"arcsin", "arcsin"}, {"arccos", "arccos"}, {"arctan", "arctan"},
    {"sinh", "sinh"}, {"cosh", "cosh"}, {"tanh", "tanh"},
    {"log", "log"}, {"ln", "ln"}, {"exp", "exp"},
    {"ker", "ker"}, {"deg", "deg"}, {"hom", "hom"},
    {"arg", "arg"}, {"dim", "dim"},
    {nullptr, nullptr}
};

static const char* lookup_cmd(const CmdAsciiEntry* table, const char* cmd) {
    for (const CmdAsciiEntry* e = table; e->cmd; e++) {
        if (strcmp(e->cmd, cmd) == 0) return e->ascii;
    }
    return nullptr;
}

// Look up a command name across all tables, return ASCII equivalent
static const char* cmd_to_ascii(const char* cmd) {
    if (!cmd) return nullptr;
    // strip leading backslash if present
    if (cmd[0] == '\\') cmd++;

    const char* result;
    if ((result = lookup_cmd(GREEK_TABLE, cmd))) return result;
    if ((result = lookup_cmd(OP_TABLE, cmd))) return result;
    if ((result = lookup_cmd(REL_TABLE, cmd))) return result;
    if ((result = lookup_cmd(SYM_TABLE, cmd))) return result;
    if ((result = lookup_cmd(BIGOP_TABLE, cmd))) return result;
    if ((result = lookup_cmd(FUNC_TABLE, cmd))) return result;
    return nullptr;
}

// Check if a command is a big operator (uses limits notation: sum_(...)^...)
static bool is_bigop_cmd(const char* cmd) {
    if (!cmd) return false;
    if (cmd[0] == '\\') cmd++;
    return lookup_cmd(BIGOP_TABLE, cmd) != nullptr;
}

// Check if a command is a function (sin, cos, etc.)
static bool is_func_cmd(const char* cmd) {
    if (!cmd) return false;
    if (cmd[0] == '\\') cmd++;
    return lookup_cmd(FUNC_TABLE, cmd) != nullptr;
}

// ============================================================================
// Word Coalescing for ASCII Output
// ============================================================================
// Tree-sitter parses "sin" as 3 symbol nodes (s,i,n). We need to coalesce
// consecutive single-char string children into known ASCII math words.

// All known words that should be recognized during coalescing.
// Sorted by length descending so longer matches win.
static const char* KNOWN_WORDS[] = {
    // 8+ chars
    "emptyset", "infinity",
    // 7 chars
    "epsilon", "upsilon", "arcsin", "arccos", "arctan",
    // 6 chars
    "lambda", "limsup", "liminf", "coprod", "approx",
    // 5 chars
    "alpha", "gamma", "delta", "theta", "kappa", "sigma", "omega",
    "Gamma", "Delta", "Theta", "Sigma", "Omega",
    // 4 chars
    "beta", "zeta", "iota", "sqrt", "sinh", "cosh", "tanh",
    "lim", // 3 chars but listed here to ensure ordering
    "prod", "oint",
    // 3 chars
    "sin", "cos", "tan", "cot", "sec", "csc", "log", "exp",
    "sum", "int", "sup", "inf", "max", "min", "det", "gcd",
    "phi", "chi", "psi",
    "Phi", "Psi",
    // 2 chars
    "mu", "nu", "xi", "pi", "ln", "oo",
    "Xi", "Pi",
    nullptr
};

// Check if a string is a single ASCII letter
static bool is_single_letter_str(const ItemReader& item) {
    if (!item.isString()) return false;
    String* s = item.asString();
    return s && s->len == 1 &&
           ((s->chars[0] >= 'a' && s->chars[0] <= 'z') ||
            (s->chars[0] >= 'A' && s->chars[0] <= 'Z'));
}

// Try to match a known word starting at index `start` in the children of elem.
// Returns the length of the matched word (0 if no match).
static int try_match_known_word(const ElementReader& elem, int64_t start, int64_t count,
                                 char* out_word, int out_word_max) {
    // Collect consecutive single-char string children
    int run_len = 0;
    char buf[64];
    for (int64_t i = start; i < count && run_len < 63; i++) {
        ItemReader child = elem.childAt(i);
        if (!is_single_letter_str(child)) break;
        buf[run_len++] = child.asString()->chars[0];
    }
    buf[run_len] = '\0';

    if (run_len < 2) return 0;

    // Try matching known words (longest first by checking full table)
    int best_len = 0;
    for (const char** w = KNOWN_WORDS; *w; w++) {
        int wlen = (int)strlen(*w);
        if (wlen <= run_len && wlen > best_len && strncmp(buf, *w, wlen) == 0) {
            best_len = wlen;
            // Don't break — table is sorted longest first so first match is longest
            break; // Actually words are ordered by length desc, so first match IS longest
        }
    }

    if (best_len > 0 && best_len < out_word_max) {
        memcpy(out_word, buf, best_len);
        out_word[best_len] = '\0';
    }
    return best_len;
}

// Check if a known word should be treated as a function (emits as name(arg))
// when followed by a parenthesized group
static bool is_ascii_function_word(const char* word) {
    static const char* FUNC_WORDS[] = {
        "sin", "cos", "tan", "cot", "sec", "csc",
        "arcsin", "arccos", "arctan",
        "sinh", "cosh", "tanh",
        "log", "ln", "exp",
        "sqrt", "abs", "floor", "ceil",
        nullptr
    };
    for (const char** w = FUNC_WORDS; *w; w++) {
        if (strcmp(word, *w) == 0) return true;
    }
    return false;
}

// Check if children starting at index form a parenthesized group: punc"(" ... punc")"
// Returns the index past the closing paren, or -1 if no match.
static int64_t find_matching_paren(const ElementReader& elem, int64_t start, int64_t count) {
    if (start >= count) return -1;
    ItemReader child = elem.childAt(start);
    if (!child.isElement()) return -1;
    ElementReader ce = child.asElement();
    const char* tag = ce.tagName();
    if (!tag || strcmp(tag, "punctuation") != 0) return -1;
    ItemReader val = ce.get_attr("value");
    if (val.isNull() || !val.isString()) return -1;
    if (strcmp(val.asString()->chars, "(") != 0) return -1;

    // Found opening paren at `start`, find matching close
    int depth_count = 1;
    for (int64_t i = start + 1; i < count; i++) {
        ItemReader c = elem.childAt(i);
        if (c.isElement()) {
            ElementReader el = c.asElement();
            const char* t = el.tagName();
            if (t && strcmp(t, "punctuation") == 0) {
                ItemReader v = el.get_attr("value");
                if (!v.isNull() && v.isString()) {
                    if (strcmp(v.asString()->chars, "(") == 0) depth_count++;
                    else if (strcmp(v.asString()->chars, ")") == 0) {
                        depth_count--;
                        if (depth_count == 0) return i + 1;  // past closing paren
                    }
                }
            }
        }
    }
    return -1;
}


// Format `operator` element: value attr contains the operator text
static void format_operator(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString()) {
        const char* v = val.asString()->chars;
        // Check if it's a LaTeX command
        const char* ascii = cmd_to_ascii(v);
        if (ascii) {
            stringbuf_append_str(sb, ascii);
        } else {
            stringbuf_append_str(sb, v);
        }
    }
}

// Format `relation` element: value attr contains the relation operator
static void format_relation(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString()) {
        const char* v = val.asString()->chars;
        const char* ascii = cmd_to_ascii(v);
        if (ascii) {
            stringbuf_append_str(sb, ascii);
        } else {
            stringbuf_append_str(sb, v);
        }
    }
}

// Format `subsup` element: base^sup_sub
static void format_subsup(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader base = elem.get_attr("base");
    ItemReader sub = elem.get_attr("sub");
    ItemReader sup = elem.get_attr("sup");

    // Check if base is a big operator
    bool base_is_bigop = false;
    if (!base.isNull() && base.isElement()) {
        ElementReader base_elem = base.asElement();
        const char* tag = base_elem.tagName();
        if (tag && strcmp(tag, "command") == 0) {
            ItemReader name = base_elem.get_attr("name");
            if (!name.isNull() && name.isString()) {
                base_is_bigop = is_bigop_cmd(name.asString()->chars);
            }
        }
    }

    // Emit base
    if (!base.isNull()) {
        format_item(sb, base, depth + 1);
    }

    // Emit subscript
    if (!sub.isNull()) {
        stringbuf_append_str(sb, "_");
        if (sub.isElement()) {
            ElementReader sub_elem = sub.asElement();
            const char* tag = sub_elem.tagName();
            if (tag && strcmp(tag, "group") == 0) {
                // Group: wrap in parens, emit inner content
                stringbuf_append_str(sb, "(");
                format_children(sb, sub_elem, depth + 1, " ");
                stringbuf_append_str(sb, ")");
            } else if (tag && strcmp(tag, "paren_script") == 0) {
                // paren_script already has ( and ) as children — emit directly
                format_children(sb, sub_elem, depth + 1, " ");
            } else {
                format_item(sb, sub, depth + 1);
            }
        } else {
            format_item(sb, sub, depth + 1);
        }
    }

    // Emit superscript
    if (!sup.isNull()) {
        stringbuf_append_str(sb, "^");
        if (sup.isElement()) {
            ElementReader sup_elem = sup.asElement();
            const char* tag = sup_elem.tagName();
            if (tag && strcmp(tag, "group") == 0) {
                stringbuf_append_str(sb, "(");
                format_children(sb, sup_elem, depth + 1, " ");
                stringbuf_append_str(sb, ")");
            } else if (tag && strcmp(tag, "paren_script") == 0) {
                // paren_script already has ( and ) as children — emit directly
                format_children(sb, sup_elem, depth + 1, " ");
            } else {
                format_item(sb, sup, depth + 1);
            }
        } else {
            format_item(sb, sup, depth + 1);
        }
    }
}

// Format `group` element: {contents} → just emit contents (groups are transparent in ASCII)
static void format_group(StringBuf* sb, const ElementReader& elem, int depth) {
    format_children(sb, elem, depth, " ");
}

// Format `radical` element: sqrt(radicand) or root(index)(radicand)
static void format_radical(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader idx = elem.get_attr("index");
    ItemReader rad = elem.get_attr("radicand");

    if (!idx.isNull()) {
        stringbuf_append_str(sb, "root(");
        format_item(sb, idx, depth + 1);
        stringbuf_append_str(sb, ")(");
        if (!rad.isNull()) format_item(sb, rad, depth + 1);
        stringbuf_append_str(sb, ")");
    } else {
        stringbuf_append_str(sb, "sqrt(");
        if (!rad.isNull()) format_item(sb, rad, depth + 1);
        stringbuf_append_str(sb, ")");
    }
}

// Format `fraction` element: (numer)/(denom)
static void format_fraction(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader numer = elem.get_attr("numer");
    ItemReader denom = elem.get_attr("denom");

    stringbuf_append_str(sb, "(");
    if (!numer.isNull()) format_item(sb, numer, depth + 1);
    stringbuf_append_str(sb, ")/(");
    if (!denom.isNull()) format_item(sb, denom, depth + 1);
    stringbuf_append_str(sb, ")");
}

// Format `frac_like` element (merged grammar): fallback to fraction-like format
static void format_frac_like(StringBuf* sb, const ElementReader& elem, int depth) {
    // frac_like defaults produce children; try to find numer/denom from children
    format_fraction(sb, elem, depth);
}

// Format `command` element: \alpha → alpha, \sin → sin(arg), etc.
static void format_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader name_attr = elem.get_attr("name");
    if (name_attr.isNull() || !name_attr.isString()) {
        stringbuf_append_str(sb, "?");
        return;
    }
    const char* name = name_attr.asString()->chars;
    const char* ascii = cmd_to_ascii(name);

    if (ascii) {
        stringbuf_append_str(sb, ascii);
    } else {
        // Unknown command → output name directly
        stringbuf_append_str(sb, name);
    }

    // If command has arg children, format them
    auto it = elem.children();
    ItemReader child;
    bool has_args = false;
    while (it.next(&child)) {
        if (!has_args) {
            // For function-like commands, wrap in parens
            if (is_func_cmd(name)) {
                stringbuf_append_str(sb, "(");
            }
            has_args = true;
        } else {
            stringbuf_append_str(sb, " ");
        }
        format_item(sb, child, depth + 1);
    }
    if (has_args && is_func_cmd(name)) {
        stringbuf_append_str(sb, ")");
    }
}

// Format `symbol_command` element: \infty → oo
static void format_symbol_command(StringBuf* sb, const ElementReader& elem) {
    ItemReader name_attr = elem.get_attr("name");
    if (!name_attr.isNull() && name_attr.isString()) {
        const char* name = name_attr.asString()->chars;
        const char* ascii = cmd_to_ascii(name);
        stringbuf_append_str(sb, ascii ? ascii : name);
    }
}

// Format `delimiter_group`: (content) or [content] etc.
static void format_delimiter_group(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader left = elem.get_attr("left");
    ItemReader right = elem.get_attr("right");

    if (!left.isNull() && left.isString()) {
        const char* l = left.asString()->chars;
        // strip \left prefix if present
        if (l[0] == '\\') l++;
        stringbuf_append_str(sb, l);
    }

    format_children(sb, elem, depth, " ");

    if (!right.isNull() && right.isString()) {
        const char* r = right.asString()->chars;
        if (r[0] == '\\') r++;
        stringbuf_append_str(sb, r);
    }
}

// Format `accent` element: hat(x), bar(x), etc.
static void format_accent(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader base = elem.get_attr("base");

    if (!cmd.isNull() && cmd.isString()) {
        const char* c = cmd.asString()->chars;
        if (c[0] == '\\') c++;
        stringbuf_append_str(sb, c);
    }
    stringbuf_append_str(sb, "(");
    if (!base.isNull()) format_item(sb, base, depth + 1);
    stringbuf_append_str(sb, ")");
}

// Format `environment` element: matrix, cases, etc.
static void format_environment(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader name = elem.get_attr("name");
    if (!name.isNull() && name.isString()) {
        stringbuf_append_str(sb, name.asString()->chars);
    }
    stringbuf_append_str(sb, "(");
    ItemReader body = elem.get_attr("body");
    if (!body.isNull()) format_item(sb, body, depth + 1);
    stringbuf_append_str(sb, ")");
}

// Format `infix_frac`: numer \over denom → (numer)/(denom)
static void format_infix_frac(StringBuf* sb, const ElementReader& elem, int depth) {
    format_fraction(sb, elem, depth);
}

// Format `text_command` or `textstyle_command`: \text{...} → "..."
static void format_text_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader content = elem.get_attr("content");
    if (!content.isNull() && content.isString()) {
        stringbuf_append_str(sb, "\"");
        stringbuf_append_str(sb, content.asString()->chars);
        stringbuf_append_str(sb, "\"");
    } else {
        // May have children instead (textstyle_command merged format)
        stringbuf_append_str(sb, "\"");
        format_children(sb, elem, depth, " ");
        stringbuf_append_str(sb, "\"");
    }
}

// Format `space_command` / `spacing_command`: \quad → " "
static void format_space_command(StringBuf* sb, const ElementReader& elem) {
    (void)elem;
    stringbuf_append_str(sb, " ");
}

// ============================================================================
// Per-format symbol handler
// ============================================================================

// format_symbol_impl: emit ASCII math symbol representation
static void format_symbol_impl(StringBuf* sb, Symbol* sym) {
    if (!sym || !sym->chars) return;
    // Symbols: row_sep → semicolon, col_sep → comma, other → cmd_to_ascii lookup
    if (strcmp(sym->chars, "row_sep") == 0) { stringbuf_append_str(sb, "; "); }
    else if (strcmp(sym->chars, "col_sep") == 0) { stringbuf_append_str(sb, ", "); }
    else {
        const char* ascii = cmd_to_ascii(sym->chars);
        stringbuf_append_str(sb, ascii ? ascii : sym->chars);
    }
}

// ============================================================================
// Dispatch
// ============================================================================

static void format_element_impl(StringBuf* sb, const ElementReader& elem, int depth) {
    const char* tag = elem.tagName();
    if (!tag) return;

    log_debug("format-math-ascii: element '%s' depth=%d", tag, depth);

    if (strcmp(tag, "math") == 0) return format_math_root(sb, elem, depth);
    if (strcmp(tag, "operator") == 0) return format_operator(sb, elem);
    if (strcmp(tag, "relation") == 0) return format_relation(sb, elem);
    if (strcmp(tag, "punctuation") == 0) return format_punctuation(sb, elem);
    if (strcmp(tag, "subsup") == 0) return format_subsup(sb, elem, depth);
    if (strcmp(tag, "group") == 0) return format_group(sb, elem, depth);
    if (strcmp(tag, "brack_group") == 0) return format_group(sb, elem, depth);
    if (strcmp(tag, "radical") == 0) return format_radical(sb, elem, depth);
    if (strcmp(tag, "fraction") == 0) return format_fraction(sb, elem, depth);
    if (strcmp(tag, "frac_like") == 0) return format_frac_like(sb, elem, depth);
    if (strcmp(tag, "binomial") == 0) return format_fraction(sb, elem, depth); // fallback
    if (strcmp(tag, "genfrac") == 0) return format_fraction(sb, elem, depth);  // fallback
    if (strcmp(tag, "infix_frac") == 0) return format_infix_frac(sb, elem, depth);
    if (strcmp(tag, "command") == 0) return format_command(sb, elem, depth);
    if (strcmp(tag, "symbol_command") == 0) return format_symbol_command(sb, elem);
    if (strcmp(tag, "delimiter_group") == 0) return format_delimiter_group(sb, elem, depth);
    if (strcmp(tag, "accent") == 0) return format_accent(sb, elem, depth);
    if (strcmp(tag, "environment") == 0) return format_environment(sb, elem, depth);
    if (strcmp(tag, "env_body") == 0) return format_children(sb, elem, depth, " ");
    if (strcmp(tag, "text_command") == 0) return format_text_command(sb, elem, depth);
    if (strcmp(tag, "textstyle_command") == 0) return format_text_command(sb, elem, depth);
    if (strcmp(tag, "style_command") == 0) return format_text_command(sb, elem, depth);
    if (strcmp(tag, "space_command") == 0) return format_space_command(sb, elem);
    if (strcmp(tag, "spacing_command") == 0) return format_space_command(sb, elem);
    if (strcmp(tag, "hspace_command") == 0) return format_space_command(sb, elem);
    if (strcmp(tag, "skip_command") == 0) return format_space_command(sb, elem);

    // ASCII-specific nodes
    if (strcmp(tag, "ascii_operator") == 0) { format_children(sb, elem, depth, ""); return; }
    if (strcmp(tag, "quoted_text") == 0) { format_children(sb, elem, depth, ""); return; }
    if (strcmp(tag, "paren_script") == 0) {
        // paren_script children already include literal "(" and ")" punctuation,
        // so just emit all children without adding extra parens
        format_children(sb, elem, depth, " ");
        return;
    }

    // Other elements: emit children with fallback
    if (strcmp(tag, "big_operator") == 0) {
        ItemReader op = elem.get_attr("op");
        if (!op.isNull() && op.isString()) {
            const char* name = op.asString()->chars;
            const char* ascii = cmd_to_ascii(name);
            stringbuf_append_str(sb, ascii ? ascii : name);
        }
        return;
    }

    // Limits modifier, middle_delim, sized_delimiter, etc: skip or emit value
    if (strcmp(tag, "limits_modifier") == 0) return; // handled by subsup
    if (strcmp(tag, "delimiter") == 0) return format_delimiter(sb, elem);
    if (strcmp(tag, "sized_delimiter") == 0) {
        ItemReader delim = elem.get_attr("delim");
        if (!delim.isNull() && delim.isString()) stringbuf_append_str(sb, delim.asString()->chars);
        return;
    }

    // Generic fallback: emit all children
    log_debug("format-math-ascii: unknown element '%s', using fallback", tag);
    format_children(sb, elem, depth, " ");
}

// Core coalescing loop over children [start, end)
// This is the workhorse function that handles word coalescing and subsup boundary merging.
static void format_children_range(StringBuf* sb, const ElementReader& elem,
                                   int64_t start, int64_t end, int depth, const char* sep) {
    bool first = true;

    for (int64_t i = start; i < end; ) {
        ItemReader child = elem.childAt(i);

        // Try to coalesce consecutive single-char strings into known words
        if (is_single_letter_str(child)) {
            // Collect consecutive single-char strings, also peeking into trailing subsup base
            char buf[64];
            int run_len = 0;
            int64_t j;
            for (j = i; j < end && run_len < 62; j++) {
                ItemReader c = elem.childAt(j);
                if (is_single_letter_str(c)) {
                    buf[run_len++] = c.asString()->chars[0];
                } else {
                    break;
                }
            }
            // Peek into subsup base if present right after letter run
            bool has_trailing_subsup = false;
            if (j < end && run_len > 0 && run_len < 62) {
                ItemReader next = elem.childAt(j);
                if (next.isElement()) {
                    ElementReader next_elem = next.asElement();
                    const char* ntag = next_elem.tagName();
                    if (ntag && strcmp(ntag, "subsup") == 0) {
                        ItemReader base = next_elem.get_attr("base");
                        if (!base.isNull() && base.isString()) {
                            String* bs = base.asString();
                            if (bs && bs->len == 1 &&
                                ((bs->chars[0] >= 'a' && bs->chars[0] <= 'z') ||
                                 (bs->chars[0] >= 'A' && bs->chars[0] <= 'Z'))) {
                                buf[run_len] = bs->chars[0];
                                run_len++;
                                has_trailing_subsup = true;
                            }
                        }
                    }
                }
            }
            buf[run_len] = '\0';

            // Try matching the longest known word
            int best_len = 0;
            if (run_len >= 2) {
                for (const char** w = KNOWN_WORDS; *w; w++) {
                    int wlen = (int)strlen(*w);
                    if (wlen <= run_len && wlen > best_len && strncmp(buf, *w, wlen) == 0) {
                        best_len = wlen;
                        break; // table sorted longest-first
                    }
                }
            }

            if (best_len > 0) {
                char word[64];
                memcpy(word, buf, best_len);
                word[best_len] = '\0';

                // How many direct string children consumed
                int direct_chars = (j - i); // number of string children before subsup
                bool word_extends_into_subsup = has_trailing_subsup && (best_len > direct_chars);

                if (!first && sep && sep[0]) stringbuf_append_str(sb, sep);
                first = false;

                // Check if this is a function word followed by parens: sin(...)
                if (!word_extends_into_subsup && is_ascii_function_word(word)) {
                    int64_t after_word = i + best_len;
                    int64_t paren_end = find_matching_paren(elem, after_word, end);
                    if (paren_end > 0) {
                        stringbuf_append_str(sb, word);
                        stringbuf_append_str(sb, "(");
                        // Format inner paren content with coalescing
                        format_children_range(sb, elem, after_word + 1, paren_end - 1, depth + 1, sep);
                        stringbuf_append_str(sb, ")");
                        i = paren_end;
                        continue;
                    }
                }

                if (word_extends_into_subsup) {
                    // Word extends into subsup base: emit word then sub/sup parts
                    int chars_before_subsup = direct_chars; // e.g. "sigm" = 4
                    int64_t subsup_idx = j; // index of the subsup element
                    ElementReader subsup_elem = elem.childAt(subsup_idx).asElement();

                    // Check if the word is a function/bigop with the subsup
                    stringbuf_append_str(sb, word);

                    // Emit sub/sup from the subsup element
                    ItemReader sub = subsup_elem.get_attr("sub");
                    ItemReader sup = subsup_elem.get_attr("sup");

                    if (!sub.isNull()) {
                        stringbuf_append_str(sb, "_");
                        if (sub.isElement()) {
                            ElementReader sub_elem = sub.asElement();
                            const char* stag = sub_elem.tagName();
                            if (stag && strcmp(stag, "paren_script") == 0) {
                                // paren_script children already include ( and )
                                format_children(sb, sub_elem, depth + 1, "");
                            } else if (stag && strcmp(stag, "group") == 0) {
                                stringbuf_append_str(sb, "(");
                                format_children(sb, sub_elem, depth + 1, " ");
                                stringbuf_append_str(sb, ")");
                            } else {
                                format_item(sb, sub, depth + 1);
                            }
                        } else {
                            format_item(sb, sub, depth + 1);
                        }
                    }

                    if (!sup.isNull()) {
                        stringbuf_append_str(sb, "^");
                        if (sup.isElement()) {
                            ElementReader sup_elem = sup.asElement();
                            const char* stag = sup_elem.tagName();
                            if (stag && strcmp(stag, "paren_script") == 0) {
                                // paren_script children already include ( and )
                                format_children(sb, sup_elem, depth + 1, "");
                            } else if (stag && strcmp(stag, "group") == 0) {
                                stringbuf_append_str(sb, "(");
                                format_children(sb, sup_elem, depth + 1, " ");
                                stringbuf_append_str(sb, ")");
                            } else {
                                format_item(sb, sup, depth + 1);
                            }
                        } else {
                            format_item(sb, sup, depth + 1);
                        }
                    }

                    i = subsup_idx + 1; // skip past subsup
                    continue;
                }

                // Simple known word (no subsup extension)
                stringbuf_append_str(sb, word);
                i += best_len;
                continue;
            }
            // No known word match — fall through to emit single char
        }

        if (!first && sep && sep[0]) {
            stringbuf_append_str(sb, sep);
        }
        first = false;
        format_item(sb, child, depth + 1);
        i++;
    }
}

static void format_children(StringBuf* sb, const ElementReader& elem, int depth, const char* sep) {
    int64_t count = elem.childCount();
    format_children_range(sb, elem, 0, count, depth, sep);
}

// ============================================================================
// Public Entry Points
// ============================================================================

String* format_math_ascii_standalone(Pool* pool, Item root_item) {
    log_debug("format-math-ascii: format_math_ascii_standalone called");
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return nullptr;

    ItemReader root(root_item.to_const());
    format_item(sb, root, 0);

    String* result = stringbuf_to_string(sb);
    log_debug("format-math-ascii: result='%s'", result ? result->chars : "NULL");
    return result;
}
