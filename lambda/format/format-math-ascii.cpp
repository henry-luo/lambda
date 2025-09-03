#include "format.h"
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

// ASCII Math format definitions table
static const ASCIIMathFormatDef ascii_format_defs[] = {
    // Basic arithmetic operators
    {"add", " + ", true, false, true, 2},
    {"sub", " - ", true, false, true, 2},
    {"unary_minus", "-{1}", true, false, false, 1},
    {"mul", " * ", true, false, true, 2},
    {"implicit_mul", "", true, false, true, 2},
    {"div", " / ", true, false, true, 2},
    
    // Powers and roots
    {"pow", "{1}^{2}", true, false, false, 2},
    {"sqrt", "sqrt({1})", true, false, false, 1},
    {"root", "root({2})({1})", true, false, false, 2},
    
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
    {"eq", " = ", true, false, true, 2},
    {"neq", " != ", true, false, true, 2},
    {"lt", " < ", true, false, true, 2},
    {"le", " <= ", true, false, true, 2},
    {"leq", " <= ", true, false, true, 2},
    {"gt", " > ", true, false, true, 2},
    {"ge", " >= ", true, false, true, 2},
    {"geq", " >= ", true, false, true, 2},
    {"approx", " ~~ ", true, false, true, 2},
    {"equiv", " -= ", true, false, true, 2},
    
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
    
    // Big operators
    {"sum", "sum", false, false, false, 0},
    {"prod", "prod", false, false, false, 0},
    {"int", "int", false, false, false, 0},
    {"oint", "oint", false, false, false, 0},
    
    // Limits
    {"lim", "lim", false, false, false, 0},
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
    {"to", " -> ", true, false, true, 2},
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
static void format_ascii_math_string(StringBuf* sb, Item str_item) {
    String* str = (String*)str_item.pointer;
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

// Format ASCII math element
static void format_ascii_math_element(StringBuf* sb, Element* elem, int depth) {
    if (!elem) return;
    
    // Get element type and name using Lambda's API
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type || !elmt_type->name.str) return;
    
    const char* element_name = elmt_type->name.str;
    const ASCIIMathFormatDef* def = find_ascii_format_def(element_name);
    
    if (def && def->ascii_format) {
        if (def->has_children && elem->length > 0) {
            if (def->is_binary_op && elem->length == 2) {
                // Binary operator: format as {left} op {right}
                format_ascii_math_item(sb, elem->items[0], depth + 1);
                stringbuf_append_str(sb, def->ascii_format);
                format_ascii_math_item(sb, elem->items[1], depth + 1);
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
    
    switch (type_id) {
        case LMD_TYPE_ELEMENT: {
            Element* elem = (Element*)item.pointer;
            format_ascii_math_element(sb, elem, depth);
            break;
        }
        case LMD_TYPE_STRING: {
            format_ascii_math_string(sb, item);
            break;
        }
        case LMD_TYPE_INT: {
            int value = item.int_val;
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", value);
            stringbuf_append_str(sb, buffer);
            break;
        }
        case LMD_TYPE_FLOAT: {
            double* val_ptr = (double*)item.pointer;
            if (val_ptr) {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%.10g", *val_ptr);
                stringbuf_append_str(sb, buffer);
            }
            break;
        }
        default:
            // Unknown type - try to output something reasonable
            stringbuf_append_str(sb, "?");
            break;
    }
}

// Main ASCII math formatter function
String* format_math_ascii_standalone(VariableMemPool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    format_ascii_math_item(sb, root_item, 0);
    
    String* result = stringbuf_to_string(sb);
    return result;
}
