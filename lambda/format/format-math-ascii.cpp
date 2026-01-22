#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include <stdio.h>
#include <string.h>

// Helper functions from existing formatter
extern TypeId get_type_id(Item item);

// Forward declaration for Lambda type system
typedef struct TypeElmt TypeElmt;

// ASCII Math formatting definitions
typedef struct {
    const char* element_name;
    const char* ascii_format;
    bool has_children;
    bool needs_parentheses;
    bool is_binary_op;
    int arg_count;
} ASCIIMathFormatDef;

// Forward declarations
static void format_ascii_math_item(StringBuf* sb, Item item, int depth);
static void format_ascii_math_element(StringBuf* sb, Element* elem, int depth);
static void format_ascii_math_children(StringBuf* sb, List* children, int depth);
static void format_ascii_math_children_with_template(StringBuf* sb, List* children, const char* format_str, int depth);
static void format_ascii_math_string(StringBuf* sb, String* str);
static const ASCIIMathFormatDef* find_ascii_format_def(const char* element_name);

// reader-based forward declarations
static void format_ascii_math_item_reader(StringBuf* sb, const ItemReader& item, int depth);
static void format_ascii_math_element_reader(StringBuf* sb, const ElementReader& elem, int depth);
static void format_ascii_math_children_reader(StringBuf* sb, const ElementReader& elem, int depth);

// ASCII Math format definitions table
static const ASCIIMathFormatDef ascii_format_defs[] = {
    // Basic arithmetic operators
    {"add", "+", true, false, true, 2},
    {"sub", "-", true, false, true, 2},
    {"unary_minus", "-{1}", true, false, false, 1},
    {"mul", "*", true, false, true, 2},
    {"implicit_mul", "", true, false, true, 2},
    {"div", "/", true, false, true, 2},

    // Powers and roots
    {"pow", "^", true, false, true, 2},
    {"sqrt", "sqrt({1})", true, false, false, 1},
    {"root", "root({2})({1})", true, false, false, 2},
    {"floor", "floor({1})", true, false, false, 1},
    {"ceil", "ceil({1})", true, false, false, 1},

    // Fractions
    {"frac", "({1})/({2})", true, false, false, 2},
    {"dfrac", "({1})/({2})", true, false, false, 2},
    {"tfrac", "({1})/({2})", true, false, false, 2},
    {"cfrac", "({1})/({2})", true, false, false, 2},

    // Trigonometric functions
    {"sin", "sin({1})", true, false, false, 1},
    {"cos", "cos({1})", true, false, false, 1},
    {"tan", "tan({1})", true, false, false, 1},
    {"csc", "csc({1})", true, false, false, 1},
    {"sec", "sec({1})", true, false, false, 1},
    {"cot", "cot({1})", true, false, false, 1},

    // Inverse trigonometric functions
    {"arcsin", "arcsin({1})", true, false, false, 1},
    {"arccos", "arccos({1})", true, false, false, 1},
    {"arctan", "arctan({1})", true, false, false, 1},

    // Hyperbolic functions
    {"sinh", "sinh({1})", true, false, false, 1},
    {"cosh", "cosh({1})", true, false, false, 1},
    {"tanh", "tanh({1})", true, false, false, 1},

    // Logarithmic functions
    {"log", "log({1})", true, false, false, 1},
    {"ln", "ln({1})", true, false, false, 1},
    {"lg", "lg({1})", true, false, false, 1},

    // Relations
    {"eq", "=", true, false, true, 2},
    {"neq", "!=", true, false, true, 2},
    {"lt", "<", true, false, true, 2},
    {"le", "<=", true, false, true, 2},
    {"leq", "<=", true, false, true, 2},
    {"gt", ">", true, false, true, 2},
    {"ge", ">=", true, false, true, 2},
    {"geq", ">=", true, false, true, 2},
    {"approx", "~~", true, false, true, 2},
    {"equiv", "-=", true, false, true, 2},

    // Greek letters (as identifiers)
    {"alpha", "alpha", false, false, false, 0},
    {"beta", "beta", false, false, false, 0},
    {"gamma", "gamma", false, false, false, 0},
    {"delta", "delta", false, false, false, 0},
    {"epsilon", "epsilon", false, false, false, 0},
    {"zeta", "zeta", false, false, false, 0},
    {"eta", "eta", false, false, false, 0},
    {"theta", "theta", false, false, false, 0},
    {"iota", "iota", false, false, false, 0},
    {"kappa", "kappa", false, false, false, 0},
    {"lambda", "lambda", false, false, false, 0},
    {"mu", "mu", false, false, false, 0},
    {"nu", "nu", false, false, false, 0},
    {"xi", "xi", false, false, false, 0},
    {"omicron", "omicron", false, false, false, 0},
    {"pi", "pi", false, false, false, 0},
    {"rho", "rho", false, false, false, 0},
    {"sigma", "sigma", false, false, false, 0},
    {"tau", "tau", false, false, false, 0},
    {"upsilon", "upsilon", false, false, false, 0},
    {"phi", "phi", false, false, false, 0},
    {"chi", "chi", false, false, false, 0},
    {"psi", "psi", false, false, false, 0},
    {"omega", "omega", false, false, false, 0},

    // Special symbols
    {"infinity", "oo", false, false, false, 0},
    {"infty", "oo", false, false, false, 0},
    {"pm", "+-", false, false, false, 0},
    {"mp", "-+", false, false, false, 0},

    // Big operators - enable children processing for bounds notation
    {"sum", "sum", true, false, false, 0},
    {"prod", "prod", true, false, false, 0},
    {"int", "int", true, false, false, 0},
    {"oint", "oint", true, false, false, 0},

    // Limits - enable children processing for bounds notation
    {"lim", "lim", true, false, false, 0},
    {"limsup", "limsup", false, false, false, 0},
    {"liminf", "liminf", false, false, false, 0},

    // Set operations
    {"cup", " uu ", true, false, true, 2},
    {"cap", " nn ", true, false, true, 2},
    {"in", " in ", true, false, true, 2},
    {"notin", " !in ", true, false, true, 2},
    {"subset", " sub ", true, false, true, 2},
    {"supset", " sup ", true, false, true, 2},
    {"subseteq", " sube ", true, false, true, 2},
    {"supseteq", " supe ", true, false, true, 2},

    // Logic
    {"and", " and ", true, false, true, 2},
    {"or", " or ", true, false, true, 2},
    {"not", "not ", true, false, false, 1},
    {"implies", " => ", true, false, true, 2},
    {"iff", " <=> ", true, false, true, 2},

    // Arrows
    {"to", "->", true, false, true, 2},
    {"rightarrow", " -> ", true, false, true, 2},
    {"leftarrow", " <- ", true, false, true, 2},
    {"leftrightarrow", " <-> ", true, false, true, 2},
    {"Rightarrow", " => ", true, false, true, 2},
    {"Leftarrow", " <= ", true, false, true, 2},
    {"Leftrightarrow", " <=> ", true, false, true, 2},

    // Brackets and grouping
    {"abs", "|{1}|", true, false, false, 1},
    {"norm", "||{1}||", true, false, false, 1},
    {"floor", "|_{1}_|", true, false, false, 1},
    {"ceil", "|^{1}^|", true, false, false, 1},

    // Subscripts and superscripts (handled specially)
    {"subscript", "{1}_{2}", true, false, false, 2},
    {"superscript", "{1}^{2}", true, false, false, 2},

    // End marker
    {NULL, NULL, false, false, false, 0}
};

// Find format definition for element
static const ASCIIMathFormatDef* find_ascii_format_def(const char* element_name) {
    if (!element_name || !*element_name) {
        return NULL;
    }

    for (int i = 0; ascii_format_defs[i].element_name != NULL; i++) {
        if (strcmp(ascii_format_defs[i].element_name, element_name) == 0) {
            return &ascii_format_defs[i];
        }
    }
    return NULL;
}

// Format ASCII math string (simple text)
static void format_ascii_math_string(StringBuf* sb, String* str) {
    if (str && str->chars) {
        stringbuf_append_str(sb, str->chars);
    }
}

// Format children with template substitution
static void format_ascii_math_children_with_template(StringBuf* sb, List* children, const char* format_str, int depth) {
    if (!format_str || !children) return;

    const char* p = format_str;
    while (*p) {
        if (*p == '{' && *(p+1) >= '1' && *(p+1) <= '9' && *(p+2) == '}') {
            // Template substitution {1}, {2}, etc.
            int arg_index = *(p+1) - '1';  // Convert '1' to 0, '2' to 1, etc.

            if (arg_index < children->length) {
                format_ascii_math_item(sb, children->items[arg_index], depth + 1);
            }
            p += 3;  // Skip {n}
        } else {
            stringbuf_append_char(sb, *p);
            p++;
        }
    }
}

// Format children without template
static void format_ascii_math_children(StringBuf* sb, List* children, int depth) {
    if (!children) return;

    for (int i = 0; i < children->length; i++) {
        if (i > 0) {
            stringbuf_append_str(sb, " ");
        }
        format_ascii_math_item(sb, children->items[i], depth + 1);
    }
}

// Helper function to check if an item is a simple bound (single identifier or number)
static bool is_simple_bound(Item item) {
    uint32_t type_id = item.item & 0xFF;
    return (type_id == LMD_TYPE_SYMBOL || type_id == LMD_TYPE_STRING);
}

// Get operator precedence (higher number = higher precedence)
static int get_operator_precedence(const char* op_name) {
    if (!op_name) return 0;

    // Relations (lowest precedence)
    if (strcmp(op_name, "eq") == 0 || strcmp(op_name, "neq") == 0 ||
        strcmp(op_name, "lt") == 0 || strcmp(op_name, "gt") == 0 ||
        strcmp(op_name, "leq") == 0 || strcmp(op_name, "geq") == 0) {
        return 1;
    }

    // Addition and subtraction
    if (strcmp(op_name, "add") == 0 || strcmp(op_name, "sub") == 0) {
        return 2;
    }

    // Multiplication and division
    if (strcmp(op_name, "mul") == 0 || strcmp(op_name, "div") == 0 ||
        strcmp(op_name, "implicit_mul") == 0) {
        return 3;
    }

    // Power (highest precedence)
    if (strcmp(op_name, "pow") == 0 || strcmp(op_name, "power") == 0) {
        return 4;
    }

    return 0; // Unknown operators
}

// Check if child needs parentheses when used as operand of parent
static bool needs_parentheses(const char* parent_op, const char* child_op, bool is_right_operand = false) {
    if (!parent_op || !child_op) return false;

    int parent_prec = get_operator_precedence(parent_op);
    int child_prec = get_operator_precedence(child_op);

    if (child_prec == 0) return false; // Unknown child operator

    // Child needs parentheses if it has lower precedence than parent
    if (child_prec < parent_prec) return true;

    // Special case: for right-associative operators like power,
    // left operand with same precedence needs parentheses
    if (strcmp(parent_op, "pow") == 0 && !is_right_operand && child_prec == parent_prec) {
        return true;
    }

    return false;
}

// Format ASCII math element
static void format_ascii_math_element(StringBuf* sb, Element* elem, int depth) {
    if (!elem) return;

    // Get element type and name using Lambda's API
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type || !elmt_type->name.str) return;

    const char* element_name = elmt_type->name.str;
    const ASCIIMathFormatDef* def = find_ascii_format_def(element_name);

    // Special handling for sum/prod/int/lim with bounds notation
    printf("DEBUG: Checking element '%s' with length %ld\n", element_name, elem->length);
    if ((strcmp(element_name, "sum") == 0 || strcmp(element_name, "prod") == 0 ||
         strcmp(element_name, "int") == 0 || strcmp(element_name, "oint") == 0 ||
         strcmp(element_name, "lim") == 0) &&
        elem->length >= 2) {
        printf("DEBUG: Using special sum/prod/int/lim formatting for '%s'\n", element_name);

        // Special handling for limit notation
        if (strcmp(element_name, "lim") == 0) {
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "_(");
            format_ascii_math_item(sb, elem->items[0], depth + 1);  // limit expression (e.g., x->0)
            stringbuf_append_str(sb, ")");
            if (elem->length >= 2) {
                stringbuf_append_str(sb, " ");
                format_ascii_math_item(sb, elem->items[1], depth + 1);  // function expression
            }
        } else {
            // Format as: sum_(lower_bound)^upper_bound summand
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "_(");
            format_ascii_math_item(sb, elem->items[0], depth + 1);  // lower bound
            stringbuf_append_str(sb, ")");
            if (elem->length >= 2) {
                stringbuf_append_str(sb, "^");
                format_ascii_math_item(sb, elem->items[1], depth + 1);  // upper bound
            }
            if (elem->length >= 3) {
                stringbuf_append_str(sb, " ");
                format_ascii_math_item(sb, elem->items[2], depth + 1);  // summand
            }

            // For integrals, add differential if present
            if ((strcmp(element_name, "int") == 0 || strcmp(element_name, "oint") == 0) &&
                elem->length >= 4) {
                stringbuf_append_str(sb, " ");
                format_ascii_math_item(sb, elem->items[3], depth + 1);  // differential
            }
        }
        printf("DEBUG: Finished special formatting\n");
        return;
    } else if (def && def->ascii_format) {
        if (def->has_children && elem->length > 0) {
            if (def->is_binary_op && elem->length == 2) {
                // Binary operator: format as {left} op {right} with precedence-aware parentheses

                // Check if left operand needs parentheses
                bool left_needs_parens = false;
                TypeId left_type_id = get_type_id(elem->items[0]);
                if (left_type_id == LMD_TYPE_ELEMENT) {
                    Element* left_elem = elem->items[0].element;
                    if (left_elem && left_elem->type) {
                        TypeElmt* left_elmt_type = (TypeElmt*)left_elem->type;
                        if (left_elmt_type && left_elmt_type->name.str) {
                            left_needs_parens = needs_parentheses(element_name, left_elmt_type->name.str, false);
                        }
                    }
                }

                // Check if right operand needs parentheses
                bool right_needs_parens = false;
                TypeId right_type_id = get_type_id(elem->items[1]);
                if (right_type_id == LMD_TYPE_ELEMENT) {
                    Element* right_elem = elem->items[1].element;
                    if (right_elem && right_elem->type) {
                        TypeElmt* right_elmt_type = (TypeElmt*)right_elem->type;
                        if (right_elmt_type && right_elmt_type->name.str) {
                            right_needs_parens = needs_parentheses(element_name, right_elmt_type->name.str, true);
                        }
                    }
                }

                // Format left operand with parentheses if needed
                if (left_needs_parens) stringbuf_append_str(sb, "(");
                format_ascii_math_item(sb, elem->items[0], depth + 1);
                if (left_needs_parens) stringbuf_append_str(sb, ")");

                // Add operator with spaces for better readability
                stringbuf_append_str(sb, " ");
                stringbuf_append_str(sb, def->ascii_format);
                stringbuf_append_str(sb, " ");

                // Format right operand with parentheses if needed
                if (right_needs_parens) stringbuf_append_str(sb, "(");
                format_ascii_math_item(sb, elem->items[1], depth + 1);
                if (right_needs_parens) stringbuf_append_str(sb, ")");
            } else {
                // Use template formatting
                format_ascii_math_children_with_template(sb, (List*)elem, def->ascii_format, depth);
            }
        } else {
            // No children, just output the format string
            stringbuf_append_str(sb, def->ascii_format);
        }
    } else {
        // Unknown element - output element name and children
        stringbuf_append_str(sb, element_name);
        if (elem->length > 0) {
            stringbuf_append_str(sb, "(");
            format_ascii_math_children(sb, (List*)elem, depth);
            stringbuf_append_str(sb, ")");
        }
    }
}

// Format ASCII math item (main dispatcher)
static void format_ascii_math_item(StringBuf* sb, Item item, int depth) {
    if (depth > 50) {  // Prevent infinite recursion
        stringbuf_append_str(sb, "...");
        return;
    }

    TypeId type_id = get_type_id(item);
    printf("DEBUG format_ascii_math_item: item=0x%lx, type_id=%d, depth=%d\n", item.item, type_id, depth);

    switch (type_id) {
        case LMD_TYPE_ELEMENT: {
            Element* elem = item.element;
            format_ascii_math_element(sb, elem, depth);
            break;
        }
        case LMD_TYPE_STRING: {
            printf("DEBUG: Processing LMD_TYPE_STRING\n");
            String* str = item.get_string();
            if (str) {
                printf("DEBUG: String content: '%s'\n", str->chars);
            }
            format_ascii_math_string(sb, str);
            break;
        }
        case LMD_TYPE_SYMBOL: {
            printf("DEBUG: Processing LMD_TYPE_SYMBOL\n");
            String* str = item.get_string();
            if (str && str->chars) {
                printf("DEBUG: Symbol content: '%s'\n", str->chars);
                format_ascii_math_string(sb, str);
            } else {
                printf("DEBUG: Symbol has NULL content\n");
                stringbuf_append_str(sb, "?");
            }
            break;
        }
        case LMD_TYPE_INT: {
            int64_t value = item.get_int56();
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%" PRId64, value);
            stringbuf_append_str(sb, buffer);
            break;
        }
        case LMD_TYPE_FLOAT: {
            double* val_ptr = (double*)item.double_ptr;
            if (val_ptr) {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%.10g", *val_ptr);
                stringbuf_append_str(sb, buffer);
            }
            break;
        }
        default:
            // Unknown type - try to output something reasonable
            printf("DEBUG: Unknown type %d for item 0x%lx\n", type_id, item.item);
            fflush(stdout);
            stringbuf_append_str(sb, "[UNKNOWN]");
            break;
    }
}

// Main ASCII math formatter function
String* format_math_ascii_standalone(Pool* pool, Item root_item) {
    printf("DEBUG: format_math_ascii_standalone called with item=0x%lx\n", root_item.item);
    fflush(stdout);
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // use MarkReader API
    ItemReader root(root_item.to_const());
    format_ascii_math_item_reader(sb, root, 0);

    String* result = stringbuf_to_string(sb);
    printf("DEBUG: format_math_ascii_standalone result='%s'\n", result ? result->chars : "NULL");
    fflush(stdout);
    return result;
}

// ===== MarkReader-based implementations =====

// format children using reader API
static void format_ascii_math_children_reader(StringBuf* sb, const ElementReader& elem, int depth) {
    auto it = elem.children();
    ItemReader child;
    bool first = true;
    while (it.next(&child)) {
        if (!first) {
            stringbuf_append_str(sb, " ");
        }
        first = false;
        format_ascii_math_item_reader(sb, child, depth + 1);
    }
}

// format children with template substitution using reader API
static void format_ascii_math_children_with_template_reader(StringBuf* sb, const ElementReader& elem, const char* format_str, int depth) {
    if (!format_str) return;

    const char* p = format_str;
    while (*p) {
        if (*p == '{' && *(p+1) >= '1' && *(p+1) <= '9' && *(p+2) == '}') {
            // template substitution {1}, {2}, etc.
            int arg_index = *(p+1) - '1';  // convert '1' to 0, '2' to 1, etc.

            ItemReader child = elem.childAt(arg_index);
            if (!child.isNull()) {
                format_ascii_math_item_reader(sb, child, depth + 1);
            }
            p += 3;  // skip {n}
        } else {
            stringbuf_append_char(sb, *p);
            p++;
        }
    }
}

// get operator name from element reader
static const char* get_operator_name_reader(const ItemReader& item) {
    if (item.isElement()) {
        ElementReader elem = item.asElement();
        return elem.tagName();
    }
    return nullptr;
}

// check if child needs parentheses using reader API
static bool needs_parentheses_reader(const char* parent_op, const ItemReader& child_item, bool is_right_operand) {
    if (!parent_op) return false;

    const char* child_op = get_operator_name_reader(child_item);
    if (!child_op) return false;

    int parent_prec = get_operator_precedence(parent_op);
    int child_prec = get_operator_precedence(child_op);

    if (child_prec == 0) return false; // unknown child operator

    // child needs parentheses if it has lower precedence than parent
    if (child_prec < parent_prec) return true;

    // special case: for right-associative operators like power,
    // left operand with same precedence needs parentheses
    if (strcmp(parent_op, "pow") == 0 && !is_right_operand && child_prec == parent_prec) {
        return true;
    }

    return false;
}

// format ASCII math element using reader API
static void format_ascii_math_element_reader(StringBuf* sb, const ElementReader& elem, int depth) {
    const char* element_name = elem.tagName();
    if (!element_name) return;

    const ASCIIMathFormatDef* def = find_ascii_format_def(element_name);

    // get element length
    long elem_length = 0;
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        elem_length++;
    }

    // special handling for sum/prod/int/lim with bounds notation
    printf("DEBUG: Checking element '%s' with length %ld\n", element_name, elem_length);
    if ((strcmp(element_name, "sum") == 0 || strcmp(element_name, "prod") == 0 ||
         strcmp(element_name, "int") == 0 || strcmp(element_name, "oint") == 0 ||
         strcmp(element_name, "lim") == 0) &&
        elem_length >= 2) {
        printf("DEBUG: Using special sum/prod/int/lim formatting for '%s'\n", element_name);

        // special handling for limit notation
        if (strcmp(element_name, "lim") == 0) {
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "_(");
            ItemReader child0 = elem.childAt(0);
            format_ascii_math_item_reader(sb, child0, depth + 1);  // limit expression
            stringbuf_append_str(sb, ")");
            if (elem_length >= 2) {
                stringbuf_append_str(sb, " ");
                ItemReader child1 = elem.childAt(1);
                format_ascii_math_item_reader(sb, child1, depth + 1);  // function expression
            }
        } else {
            // format as: sum_(lower_bound)^upper_bound summand
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "_(");
            ItemReader child0 = elem.childAt(0);
            format_ascii_math_item_reader(sb, child0, depth + 1);  // lower bound
            stringbuf_append_str(sb, ")");
            if (elem_length >= 2) {
                stringbuf_append_str(sb, "^");
                ItemReader child1 = elem.childAt(1);
                format_ascii_math_item_reader(sb, child1, depth + 1);  // upper bound
            }
            if (elem_length >= 3) {
                stringbuf_append_str(sb, " ");
                ItemReader child2 = elem.childAt(2);
                format_ascii_math_item_reader(sb, child2, depth + 1);  // summand
            }

            // for integrals, add differential if present
            if ((strcmp(element_name, "int") == 0 || strcmp(element_name, "oint") == 0) &&
                elem_length >= 4) {
                stringbuf_append_str(sb, " ");
                ItemReader child3 = elem.childAt(3);
                format_ascii_math_item_reader(sb, child3, depth + 1);  // differential
            }
        }
        printf("DEBUG: Finished special formatting\n");
        return;
    } else if (def && def->ascii_format) {
        if (def->has_children && elem_length > 0) {
            if (def->is_binary_op && elem_length == 2) {
                // binary operator: format as {left} op {right} with precedence-aware parentheses
                ItemReader left_item = elem.childAt(0);
                ItemReader right_item = elem.childAt(1);

                // check if operands need parentheses
                bool left_needs_parens = needs_parentheses_reader(element_name, left_item, false);
                bool right_needs_parens = needs_parentheses_reader(element_name, right_item, true);

                // format left operand with parentheses if needed
                if (left_needs_parens) stringbuf_append_str(sb, "(");
                format_ascii_math_item_reader(sb, left_item, depth + 1);
                if (left_needs_parens) stringbuf_append_str(sb, ")");

                // add operator with spaces for better readability
                stringbuf_append_str(sb, " ");
                stringbuf_append_str(sb, def->ascii_format);
                stringbuf_append_str(sb, " ");

                // format right operand with parentheses if needed
                if (right_needs_parens) stringbuf_append_str(sb, "(");
                format_ascii_math_item_reader(sb, right_item, depth + 1);
                if (right_needs_parens) stringbuf_append_str(sb, ")");
            } else {
                // use template formatting
                format_ascii_math_children_with_template_reader(sb, elem, def->ascii_format, depth);
            }
        } else {
            // no children, just output the format string
            stringbuf_append_str(sb, def->ascii_format);
        }
    } else {
        // unknown element - output element name and children
        stringbuf_append_str(sb, element_name);
        if (elem_length > 0) {
            stringbuf_append_str(sb, "(");
            format_ascii_math_children_reader(sb, elem, depth);
            stringbuf_append_str(sb, ")");
        }
    }
}

// format ASCII math item using reader API
static void format_ascii_math_item_reader(StringBuf* sb, const ItemReader& item, int depth) {
    if (depth > 50) {  // prevent infinite recursion
        stringbuf_append_str(sb, "...");
        return;
    }

    if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_ascii_math_element_reader(sb, elem, depth);
    }
    else if (item.isString()) {
        printf("DEBUG: Processing string via reader\n");
        String* str = item.asString();
        if (str) {
            printf("DEBUG: String content: '%s'\n", str->chars);
        }
        format_ascii_math_string(sb, str);
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
    else {
        // unknown type
        printf("DEBUG: Unknown type via reader\n");
        fflush(stdout);
        stringbuf_append_str(sb, "[UNKNOWN]");
    }
}
