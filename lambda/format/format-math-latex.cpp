// format-math-latex.cpp - LaTeX Math formatter for tree-sitter Mark AST
//
// Converts the tree-sitter-latex-math Mark Element AST back to LaTeX text.
// The AST has element tags like: math, subsup, operator, relation, group,
// radical, command, fraction, delimiter_group, environment, etc.
// Variables and numbers appear as plain String children.

#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include <stdio.h>
#include <string.h>

// Forward declarations
static void format_item(StringBuf* sb, const ItemReader& item, int depth);
static void format_element(StringBuf* sb, const ElementReader& elem, int depth);
static void format_children(StringBuf* sb, const ElementReader& elem, int depth, const char* sep);

// ============================================================================
// Helper: check if item needs braces when used as a sub/superscript argument
// Single chars/digits and single commands don't need braces; everything else does.
// ============================================================================
static bool needs_script_braces(const ItemReader& item) {
    if (item.isString()) {
        String* s = item.asString();
        return s && s->len > 1; // single char doesn't need braces
    }
    if (item.isElement()) {
        ElementReader elem = item.asElement();
        const char* tag = elem.tagName();
        if (!tag) return true;
        // Single commands (\alpha, \infty) don't need braces
        if (strcmp(tag, "command") == 0 || strcmp(tag, "symbol_command") == 0) {
            return elem.childCount() > 0; // if has args, needs braces
        }
        // group already represents {}, don't double-brace
        if (strcmp(tag, "group") == 0) return false;
        // Everything else needs braces
        return true;
    }
    return false;
}

// ============================================================================
// Element formatters
// ============================================================================

// Format the root `math` element: emit children separated by spaces
static void format_math_root(StringBuf* sb, const ElementReader& elem, int depth) {
    format_children(sb, elem, depth, " ");
}

// Format `operator` element: value attr contains the operator text (e.g., "+", "-", "\\times")
static void format_operator(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString()) {
        stringbuf_append_str(sb, val.asString()->chars);
    }
}

// Format `relation` element: value attr contains the relation (e.g., "=", "\\leq", "\\to")
static void format_relation(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString()) {
        stringbuf_append_str(sb, val.asString()->chars);
    }
}

// Format `punctuation` element
static void format_punctuation(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString()) {
        stringbuf_append_str(sb, val.asString()->chars);
    }
}

// Format `subsup` element: base_{sub}^{sup}
static void format_subsup(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader base = elem.get_attr("base");
    ItemReader sub = elem.get_attr("sub");
    ItemReader sup = elem.get_attr("sup");

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
                // Group: emit {contents}
                stringbuf_append_str(sb, "{");
                format_children(sb, sub_elem, depth + 1, " ");
                stringbuf_append_str(sb, "}");
            } else if (needs_script_braces(sub)) {
                stringbuf_append_str(sb, "{");
                format_item(sb, sub, depth + 1);
                stringbuf_append_str(sb, "}");
            } else {
                format_item(sb, sub, depth + 1);
            }
        } else if (needs_script_braces(sub)) {
            stringbuf_append_str(sb, "{");
            format_item(sb, sub, depth + 1);
            stringbuf_append_str(sb, "}");
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
                stringbuf_append_str(sb, "{");
                format_children(sb, sup_elem, depth + 1, " ");
                stringbuf_append_str(sb, "}");
            } else if (needs_script_braces(sup)) {
                stringbuf_append_str(sb, "{");
                format_item(sb, sup, depth + 1);
                stringbuf_append_str(sb, "}");
            } else {
                format_item(sb, sup, depth + 1);
            }
        } else if (needs_script_braces(sup)) {
            stringbuf_append_str(sb, "{");
            format_item(sb, sup, depth + 1);
            stringbuf_append_str(sb, "}");
        } else {
            format_item(sb, sup, depth + 1);
        }
    }
}

// Format `group` element: {contents}
// Groups in LaTeX are delimited by braces
static void format_group(StringBuf* sb, const ElementReader& elem, int depth) {
    // In LaTeX output, groups should emit their contents
    // When used as fraction args or radical args,
    // the parent already emits the braces
    format_children(sb, elem, depth, " ");
}

// Format `radical` element: \sqrt{radicand} or \sqrt[index]{radicand}
static void format_radical(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader idx = elem.get_attr("index");
    ItemReader rad = elem.get_attr("radicand");

    stringbuf_append_str(sb, "\\sqrt");

    if (!idx.isNull()) {
        stringbuf_append_str(sb, "[");
        format_item(sb, idx, depth + 1);
        stringbuf_append_str(sb, "]");
    }

    stringbuf_append_str(sb, "{");
    if (!rad.isNull()) format_item(sb, rad, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `fraction` element: \frac{numer}{denom} (or its cmd variant like \dfrac, \tfrac)
static void format_fraction(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader numer = elem.get_attr("numer");
    ItemReader denom = elem.get_attr("denom");

    // emit the command name (e.g., \frac, \dfrac, \tfrac)
    if (!cmd.isNull() && cmd.isString()) {
        const char* c = cmd.asString()->chars;
        // cmd already includes backslash from the converter
        stringbuf_append_str(sb, c);
    } else {
        stringbuf_append_str(sb, "\\frac");
    }

    stringbuf_append_str(sb, "{");
    if (!numer.isNull()) format_item(sb, numer, depth + 1);
    stringbuf_append_str(sb, "}");

    stringbuf_append_str(sb, "{");
    if (!denom.isNull()) format_item(sb, denom, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `frac_like` element (merged grammar): the first child is typically
// the command itself, and subsequent children are group args
// Actually, frac_like from the grammar might have numer/denom attrs in some cases,
// but more commonly has group children.
static void format_frac_like(StringBuf* sb, const ElementReader& elem, int depth) {
    // First try fraction-style attrs
    ItemReader numer = elem.get_attr("numer");
    ItemReader denom = elem.get_attr("denom");
    if (!numer.isNull() && !denom.isNull()) {
        ItemReader cmd = elem.get_attr("cmd");
        if (!cmd.isNull() && cmd.isString()) {
            stringbuf_append_str(sb, cmd.asString()->chars);
        } else {
            stringbuf_append_str(sb, "\\frac");
        }
        stringbuf_append_str(sb, "{");
        format_item(sb, numer, depth + 1);
        stringbuf_append_str(sb, "}");
        stringbuf_append_str(sb, "{");
        format_item(sb, denom, depth + 1);
        stringbuf_append_str(sb, "}");
        return;
    }

    // Fallback: children are the groups (from default handler)
    // First child might be the command, rest are groups
    int64_t count = elem.childCount();
    if (count == 0) return;

    // Check if first child is a string that looks like a command
    ItemReader first = elem.childAt(0);
    if (first.isString()) {
        const char* s = first.asString()->chars;
        if (s && s[0] == '\\') {
            // It's the command (e.g., "\frac")
            stringbuf_append_str(sb, s);
            // Remaining children are groups
            for (int64_t i = 1; i < count; i++) {
                ItemReader child = elem.childAt(i);
                if (child.isElement()) {
                    ElementReader ce = child.asElement();
                    const char* tag = ce.tagName();
                    if (tag && strcmp(tag, "group") == 0) {
                        stringbuf_append_str(sb, "{");
                        format_children(sb, ce, depth + 1, " ");
                        stringbuf_append_str(sb, "}");
                    } else {
                        format_item(sb, child, depth + 1);
                    }
                } else {
                    format_item(sb, child, depth + 1);
                }
            }
            return;
        }
    }

    // Generic fallback: all children are groups, emit \frac{a}{b}
    stringbuf_append_str(sb, "\\frac");
    for (int64_t i = 0; i < count; i++) {
        ItemReader child = elem.childAt(i);
        if (child.isElement()) {
            ElementReader ce = child.asElement();
            const char* tag = ce.tagName();
            if (tag && strcmp(tag, "group") == 0) {
                stringbuf_append_str(sb, "{");
                format_children(sb, ce, depth + 1, " ");
                stringbuf_append_str(sb, "}");
            } else {
                stringbuf_append_str(sb, "{");
                format_item(sb, child, depth + 1);
                stringbuf_append_str(sb, "}");
            }
        } else {
            stringbuf_append_str(sb, "{");
            format_item(sb, child, depth + 1);
            stringbuf_append_str(sb, "}");
        }
    }
}

// Format `binomial` element: \binom{top}{bottom}
static void format_binomial(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader top = elem.get_attr("top");
    ItemReader bottom = elem.get_attr("bottom");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    } else {
        stringbuf_append_str(sb, "\\binom");
    }

    stringbuf_append_str(sb, "{");
    if (!top.isNull()) format_item(sb, top, depth + 1);
    stringbuf_append_str(sb, "}");

    stringbuf_append_str(sb, "{");
    if (!bottom.isNull()) format_item(sb, bottom, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `command` element: \name or \name{arg}
static void format_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader name_attr = elem.get_attr("name");
    if (name_attr.isNull() || !name_attr.isString()) {
        stringbuf_append_str(sb, "\\?");
        return;
    }
    const char* name = name_attr.asString()->chars;

    // Emit backslash + command name
    stringbuf_append_str(sb, "\\");
    stringbuf_append_str(sb, name);

    // If command has arg children, emit them in braces
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader ce = child.asElement();
            const char* tag = ce.tagName();
            if (tag && strcmp(tag, "group") == 0) {
                stringbuf_append_str(sb, "{");
                format_children(sb, ce, depth + 1, " ");
                stringbuf_append_str(sb, "}");
            } else if (tag && strcmp(tag, "brack_group") == 0) {
                stringbuf_append_str(sb, "[");
                format_children(sb, ce, depth + 1, " ");
                stringbuf_append_str(sb, "]");
            } else {
                stringbuf_append_str(sb, "{");
                format_item(sb, child, depth + 1);
                stringbuf_append_str(sb, "}");
            }
        } else {
            format_item(sb, child, depth + 1);
        }
    }
}

// Format `symbol_command` element: \infty, \alpha, etc.
static void format_symbol_command(StringBuf* sb, const ElementReader& elem) {
    ItemReader name_attr = elem.get_attr("name");
    if (!name_attr.isNull() && name_attr.isString()) {
        stringbuf_append_str(sb, "\\");
        stringbuf_append_str(sb, name_attr.asString()->chars);
    }
}

// Format `delimiter_group`: \left( ... \right)
static void format_delimiter_group(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader left = elem.get_attr("left");
    ItemReader right = elem.get_attr("right");

    stringbuf_append_str(sb, "\\left");
    if (!left.isNull() && left.isString()) {
        stringbuf_append_str(sb, left.asString()->chars);
    } else {
        stringbuf_append_str(sb, ".");
    }

    stringbuf_append_str(sb, " ");
    format_children(sb, elem, depth, " ");
    stringbuf_append_str(sb, " ");

    stringbuf_append_str(sb, "\\right");
    if (!right.isNull() && right.isString()) {
        stringbuf_append_str(sb, right.asString()->chars);
    } else {
        stringbuf_append_str(sb, ".");
    }
}

// Format `accent` element: \hat{base}, \bar{base}, etc.
static void format_accent(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader base = elem.get_attr("base");

    if (!cmd.isNull() && cmd.isString()) {
        const char* c = cmd.asString()->chars;
        // cmd should already have backslash from the converter
        if (c[0] != '\\') stringbuf_append_str(sb, "\\");
        stringbuf_append_str(sb, c[0] == '\\' ? c : c);
    } else {
        stringbuf_append_str(sb, "\\hat");
    }

    stringbuf_append_str(sb, "{");
    if (!base.isNull()) format_item(sb, base, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `big_operator` element: \int_{lower}^{upper}, \sum_{lower}^{upper}
static void format_big_operator(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader op = elem.get_attr("op");
    ItemReader lower = elem.get_attr("lower");
    ItemReader upper = elem.get_attr("upper");

    if (!op.isNull() && op.isString()) {
        stringbuf_append_str(sb, op.asString()->chars);
    }

    if (!lower.isNull()) {
        stringbuf_append_str(sb, "_{");
        format_item(sb, lower, depth + 1);
        stringbuf_append_str(sb, "}");
    }

    if (!upper.isNull()) {
        stringbuf_append_str(sb, "^{");
        format_item(sb, upper, depth + 1);
        stringbuf_append_str(sb, "}");
    }
}

// Format `environment` element: \begin{name}...\end{name}
static void format_environment(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader name = elem.get_attr("name");
    ItemReader columns = elem.get_attr("columns");
    ItemReader body = elem.get_attr("body");

    stringbuf_append_str(sb, "\\begin{");
    if (!name.isNull() && name.isString()) {
        stringbuf_append_str(sb, name.asString()->chars);
    }
    stringbuf_append_str(sb, "}");

    if (!columns.isNull() && columns.isString()) {
        stringbuf_append_str(sb, "{");
        stringbuf_append_str(sb, columns.asString()->chars);
        stringbuf_append_str(sb, "}");
    }

    stringbuf_append_str(sb, " ");
    if (!body.isNull()) format_item(sb, body, depth + 1);
    stringbuf_append_str(sb, " ");

    stringbuf_append_str(sb, "\\end{");
    if (!name.isNull() && name.isString()) {
        stringbuf_append_str(sb, name.asString()->chars);
    }
    stringbuf_append_str(sb, "}");
}

// Format `env_body`: children separated by spaces
static void format_env_body(StringBuf* sb, const ElementReader& elem, int depth) {
    format_children(sb, elem, depth, " ");
}

// Format `text_command` element: \text{content}
static void format_text_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader content = elem.get_attr("content");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    } else {
        stringbuf_append_str(sb, "\\text");
    }

    stringbuf_append_str(sb, "{");
    if (!content.isNull() && content.isString()) {
        stringbuf_append_str(sb, content.asString()->chars);
    } else {
        format_children(sb, elem, depth, " ");
    }
    stringbuf_append_str(sb, "}");
}

// Format `style_command` element: \mathbf{arg}, \displaystyle{arg}
static void format_style_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader arg = elem.get_attr("arg");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    }

    if (!arg.isNull()) {
        stringbuf_append_str(sb, "{");
        format_item(sb, arg, depth + 1);
        stringbuf_append_str(sb, "}");
    }
}

// Format `space_command` element: \quad, \;, \, etc.
static void format_space_command(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString()) {
        stringbuf_append_str(sb, val.asString()->chars);
    }
}

// Format `infix_frac`: convert to \frac{numer}{denom}
static void format_infix_frac(StringBuf* sb, const ElementReader& elem, int depth) {
    // infix fracs are converted to use fraction format in output
    format_fraction(sb, elem, depth);
}

// Format `genfrac`: \genfrac{left}{right}{thickness}{style}{numer}{denom}
static void format_genfrac(StringBuf* sb, const ElementReader& elem, int depth) {
    stringbuf_append_str(sb, "\\genfrac");

    static const char* attrs[] = {"left_delim", "right_delim", "thickness", "style", "numer", "denom", nullptr};
    for (const char** a = attrs; *a; a++) {
        ItemReader val = elem.get_attr(*a);
        stringbuf_append_str(sb, "{");
        if (!val.isNull()) format_item(sb, val, depth + 1);
        stringbuf_append_str(sb, "}");
    }
}

// Format `overunder_command`: \overset{annotation}{base}, \underset, \stackrel
static void format_overunder_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader annotation = elem.get_attr("annotation");
    ItemReader base = elem.get_attr("base");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    }

    stringbuf_append_str(sb, "{");
    if (!annotation.isNull()) format_item(sb, annotation, depth + 1);
    stringbuf_append_str(sb, "}");

    stringbuf_append_str(sb, "{");
    if (!base.isNull()) format_item(sb, base, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `extensible_arrow`: \xrightarrow[below]{above}
static void format_extensible_arrow(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader below = elem.get_attr("below");
    ItemReader above = elem.get_attr("above");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    }

    if (!below.isNull()) {
        stringbuf_append_str(sb, "[");
        format_item(sb, below, depth + 1);
        stringbuf_append_str(sb, "]");
    }

    stringbuf_append_str(sb, "{");
    if (!above.isNull()) format_item(sb, above, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `sized_delimiter`: \big(, \Big|, etc.
static void format_sized_delimiter(StringBuf* sb, const ElementReader& elem) {
    ItemReader size = elem.get_attr("size");
    ItemReader delim = elem.get_attr("delim");

    stringbuf_append_str(sb, "\\");
    if (!size.isNull() && size.isString()) {
        stringbuf_append_str(sb, size.asString()->chars);
    }
    if (!delim.isNull() && delim.isString()) {
        stringbuf_append_str(sb, delim.asString()->chars);
    }
}

// Format `color_command`: \textcolor{color}{content}
static void format_color_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader color = elem.get_attr("color");
    ItemReader content = elem.get_attr("content");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    }

    stringbuf_append_str(sb, "{");
    if (!color.isNull()) format_item(sb, color, depth + 1);
    stringbuf_append_str(sb, "}");

    if (!content.isNull()) {
        stringbuf_append_str(sb, "{");
        format_item(sb, content, depth + 1);
        stringbuf_append_str(sb, "}");
    }
}

// Format `box_command`: \boxed{content}
static void format_box_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader options = elem.get_attr("options");
    ItemReader content = elem.get_attr("content");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    }

    if (!options.isNull()) {
        stringbuf_append_str(sb, "[");
        format_item(sb, options, depth + 1);
        stringbuf_append_str(sb, "]");
    }

    stringbuf_append_str(sb, "{");
    if (!content.isNull()) format_item(sb, content, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `phantom_command`: \phantom{content}
static void format_phantom_command(StringBuf* sb, const ElementReader& elem, int depth) {
    format_box_command(sb, elem, depth); // same structure: cmd, options, content
}

// Format `mathop_command`: \mathop{content}
static void format_mathop_command(StringBuf* sb, const ElementReader& elem, int depth) {
    stringbuf_append_str(sb, "\\mathop");
    ItemReader content = elem.get_attr("content");
    stringbuf_append_str(sb, "{");
    if (!content.isNull()) format_item(sb, content, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `matrix_command`: \matrix{body} (plain TeX)
static void format_matrix_command(StringBuf* sb, const ElementReader& elem, int depth) {
    ItemReader cmd = elem.get_attr("cmd");
    ItemReader body = elem.get_attr("body");

    if (!cmd.isNull() && cmd.isString()) {
        stringbuf_append_str(sb, cmd.asString()->chars);
    }

    stringbuf_append_str(sb, "{");
    if (!body.isNull()) format_item(sb, body, depth + 1);
    stringbuf_append_str(sb, "}");
}

// Format `delimiter` element (standalone)
static void format_delimiter(StringBuf* sb, const ElementReader& elem) {
    ItemReader val = elem.get_attr("value");
    if (!val.isNull() && val.isString()) {
        stringbuf_append_str(sb, val.asString()->chars);
    }
}

// Format `rule_command`: \rule[raise]{width}{height}
static void format_rule_command(StringBuf* sb, const ElementReader& elem, int depth) {
    stringbuf_append_str(sb, "\\rule");
    ItemReader raise = elem.get_attr("raise");
    ItemReader width = elem.get_attr("width");
    ItemReader height = elem.get_attr("height");

    if (!raise.isNull()) {
        stringbuf_append_str(sb, "[");
        format_item(sb, raise, depth + 1);
        stringbuf_append_str(sb, "]");
    }
    stringbuf_append_str(sb, "{");
    if (!width.isNull()) format_item(sb, width, depth + 1);
    stringbuf_append_str(sb, "}");
    stringbuf_append_str(sb, "{");
    if (!height.isNull()) format_item(sb, height, depth + 1);
    stringbuf_append_str(sb, "}");
}

// ============================================================================
// Dispatch
// ============================================================================

static void format_element(StringBuf* sb, const ElementReader& elem, int depth) {
    const char* tag = elem.tagName();
    if (!tag) return;

    log_debug("format-math-latex: element '%s' depth=%d", tag, depth);

    if (strcmp(tag, "math") == 0) return format_math_root(sb, elem, depth);
    if (strcmp(tag, "operator") == 0) return format_operator(sb, elem);
    if (strcmp(tag, "relation") == 0) return format_relation(sb, elem);
    if (strcmp(tag, "punctuation") == 0) return format_punctuation(sb, elem);
    if (strcmp(tag, "subsup") == 0) return format_subsup(sb, elem, depth);
    if (strcmp(tag, "group") == 0) return format_group(sb, elem, depth);
    if (strcmp(tag, "brack_group") == 0) {
        stringbuf_append_str(sb, "[");
        format_children(sb, elem, depth, " ");
        stringbuf_append_str(sb, "]");
        return;
    }
    if (strcmp(tag, "radical") == 0) return format_radical(sb, elem, depth);
    if (strcmp(tag, "fraction") == 0) return format_fraction(sb, elem, depth);
    if (strcmp(tag, "frac_like") == 0) return format_frac_like(sb, elem, depth);
    if (strcmp(tag, "binomial") == 0) return format_binomial(sb, elem, depth);
    if (strcmp(tag, "genfrac") == 0) return format_genfrac(sb, elem, depth);
    if (strcmp(tag, "infix_frac") == 0) return format_infix_frac(sb, elem, depth);
    if (strcmp(tag, "command") == 0) return format_command(sb, elem, depth);
    if (strcmp(tag, "symbol_command") == 0) return format_symbol_command(sb, elem);
    if (strcmp(tag, "delimiter_group") == 0) return format_delimiter_group(sb, elem, depth);
    if (strcmp(tag, "delimiter") == 0) return format_delimiter(sb, elem);
    if (strcmp(tag, "accent") == 0) return format_accent(sb, elem, depth);
    if (strcmp(tag, "big_operator") == 0) return format_big_operator(sb, elem, depth);
    if (strcmp(tag, "environment") == 0) return format_environment(sb, elem, depth);
    if (strcmp(tag, "env_body") == 0) return format_env_body(sb, elem, depth);
    if (strcmp(tag, "text_command") == 0) return format_text_command(sb, elem, depth);
    if (strcmp(tag, "textstyle_command") == 0) return format_text_command(sb, elem, depth);
    if (strcmp(tag, "style_command") == 0) return format_style_command(sb, elem, depth);
    if (strcmp(tag, "space_command") == 0) return format_space_command(sb, elem);
    if (strcmp(tag, "spacing_command") == 0) return format_space_command(sb, elem);
    if (strcmp(tag, "hspace_command") == 0) return format_space_command(sb, elem);
    if (strcmp(tag, "skip_command") == 0) return format_space_command(sb, elem);
    if (strcmp(tag, "overunder_command") == 0) return format_overunder_command(sb, elem, depth);
    if (strcmp(tag, "extensible_arrow") == 0) return format_extensible_arrow(sb, elem, depth);
    if (strcmp(tag, "sized_delimiter") == 0) return format_sized_delimiter(sb, elem);
    if (strcmp(tag, "color_command") == 0) return format_color_command(sb, elem, depth);
    if (strcmp(tag, "box_command") == 0) return format_box_command(sb, elem, depth);
    if (strcmp(tag, "phantom_command") == 0) return format_phantom_command(sb, elem, depth);
    if (strcmp(tag, "mathop_command") == 0) return format_mathop_command(sb, elem, depth);
    if (strcmp(tag, "matrix_command") == 0) return format_matrix_command(sb, elem, depth);
    if (strcmp(tag, "matrix_body") == 0) return format_children(sb, elem, depth, " ");
    if (strcmp(tag, "rule_command") == 0) return format_rule_command(sb, elem, depth);
    if (strcmp(tag, "limits_modifier") == 0) return; // handled by subsup

    // ASCII-specific nodes that might appear (handle gracefully)
    if (strcmp(tag, "ascii_operator") == 0) { format_children(sb, elem, depth, ""); return; }
    if (strcmp(tag, "quoted_text") == 0) { format_children(sb, elem, depth, ""); return; }
    if (strcmp(tag, "paren_script") == 0) { format_children(sb, elem, depth, " "); return; }

    // Generic fallback: emit all children
    log_debug("format-math-latex: unknown element '%s', using fallback", tag);
    format_children(sb, elem, depth, " ");
}

// ============================================================================
// Item dispatch
// ============================================================================

static void format_item(StringBuf* sb, const ItemReader& item, int depth) {
    if (depth > 50) {
        stringbuf_append_str(sb, "...");
        return;
    }

    if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element(sb, elem, depth);
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str && str->chars) {
            stringbuf_append_str(sb, str->chars);
        }
    }
    else if (item.isSymbol()) {
        Symbol* sym = item.asSymbol();
        if (sym && sym->chars) {
            // Symbols: row_sep → \\, col_sep → &
            if (strcmp(sym->chars, "row_sep") == 0) {
                stringbuf_append_str(sb, " \\\\ ");
            } else if (strcmp(sym->chars, "col_sep") == 0) {
                stringbuf_append_str(sb, " & ");
            } else {
                // Command name as symbol - emit with backslash
                stringbuf_append_str(sb, "\\");
                stringbuf_append_str(sb, sym->chars);
            }
        }
    }
    else if (item.isInt()) {
        int value = item.asInt();
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", value);
        stringbuf_append_str(sb, buffer);
    }
    else if (item.isFloat()) {
        double value = item.asFloat();
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.10g", value);
        stringbuf_append_str(sb, buffer);
    }
}

// ============================================================================
// Children helpers
// ============================================================================

static void format_children(StringBuf* sb, const ElementReader& elem, int depth, const char* sep) {
    int64_t count = elem.childCount();
    bool first = true;
    for (int64_t i = 0; i < count; i++) {
        if (!first && sep && sep[0]) {
            stringbuf_append_str(sb, sep);
        }
        first = false;
        ItemReader child = elem.childAt(i);
        format_item(sb, child, depth + 1);
    }
}

// ============================================================================
// Public Entry Point
// ============================================================================

String* format_math_latex_standalone(Pool* pool, Item root_item) {
    log_debug("format-math-latex: format_math_latex_standalone called");
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return nullptr;

    ItemReader root(root_item.to_const());
    format_item(sb, root, 0);

    String* result = stringbuf_to_string(sb);
    log_debug("format-math-latex: result='%s'", result ? result->chars : "NULL");
    return result;
}
