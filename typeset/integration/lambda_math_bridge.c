#include "lambda_math_bridge.h"
#include "../../lib/log.h"
#include <stdlib.h>
#include <string.h>

// Symbol mapping table for LaTeX to Unicode conversion
typedef struct {
    const char* latex_cmd;
    const char* unicode;
    enum ViewMathClass math_class;
} SymbolMapping;

static const SymbolMapping symbol_mappings[] = {
    // Greek letters
    {"alpha", "α", VIEW_MATH_ORD},
    {"beta", "β", VIEW_MATH_ORD},
    {"gamma", "γ", VIEW_MATH_ORD},
    {"delta", "δ", VIEW_MATH_ORD},
    {"epsilon", "ε", VIEW_MATH_ORD},
    {"pi", "π", VIEW_MATH_ORD},
    {"sigma", "σ", VIEW_MATH_ORD},
    {"theta", "θ", VIEW_MATH_ORD},
    {"lambda", "λ", VIEW_MATH_ORD},
    {"mu", "μ", VIEW_MATH_ORD},
    
    // Operators
    {"pm", "±", VIEW_MATH_BIN},
    {"mp", "∓", VIEW_MATH_BIN},
    {"times", "×", VIEW_MATH_BIN},
    {"div", "÷", VIEW_MATH_BIN},
    {"cdot", "⋅", VIEW_MATH_BIN},
    {"circ", "∘", VIEW_MATH_BIN},
    
    // Relations
    {"leq", "≤", VIEW_MATH_REL},
    {"geq", "≥", VIEW_MATH_REL},
    {"neq", "≠", VIEW_MATH_REL},
    {"approx", "≈", VIEW_MATH_REL},
    {"equiv", "≡", VIEW_MATH_REL},
    
    // Large operators
    {"sum", "∑", VIEW_MATH_OP},
    {"prod", "∏", VIEW_MATH_OP},
    {"int", "∫", VIEW_MATH_OP},
    {"oint", "∮", VIEW_MATH_OP},
    
    // Delimiters
    {"langle", "⟨", VIEW_MATH_OPEN},
    {"rangle", "⟩", VIEW_MATH_CLOSE},
    
    {NULL, NULL, VIEW_MATH_ORD}
};

// Function name table
static const char* function_names[] = {
    "sin", "cos", "tan", "cot", "sec", "csc",
    "arcsin", "arccos", "arctan", "arccot", "arcsec", "arccsc",
    "sinh", "cosh", "tanh", "coth",
    "log", "ln", "exp", "max", "min", "gcd", "lcm",
    "det", "tr", "rank", "dim",
    NULL
};

// Large operator names
static const char* large_operators[] = {
    "sum", "prod", "int", "oint", "iint", "iiint",
    "bigcup", "bigcap", "bigoplus", "bigotimes",
    "bigwedge", "bigvee",
    NULL
};

// Main conversion function
ViewNode* convert_lambda_math_to_viewnode(TypesetEngine* engine, Item math_item) {
    if (!engine || math_item.item == ITEM_NULL || math_item.item == ITEM_ERROR) {
        return NULL;
    }
    
    // Detect the type of Lambda item and convert accordingly
    if (lambda_item_is_element(math_item)) {
        return convert_math_element_to_viewnode(engine, math_item);
    } else if (lambda_item_is_string(math_item)) {
        return convert_math_text(engine, math_item);
    } else if (lambda_item_is_list(math_item)) {
        // For lists, create a group and convert each element
        ViewNode* group = view_node_create(VIEW_NODE_GROUP);
        if (!group) return NULL;
        
        int count = lambda_item_get_list_length(math_item);
        for (int i = 0; i < count; i++) {
            Item child = lambda_item_get_list_element(math_item, i);
            ViewNode* child_node = convert_lambda_math_to_viewnode(engine, child);
            if (child_node) {
                view_node_add_child(group, child_node);
            }
        }
        
        return group;
    }
    
    log_error("convert_lambda_math_to_viewnode: Unsupported Lambda item type");
    return NULL;
}

// Convert math element to view node
ViewNode* convert_math_element_to_viewnode(TypesetEngine* engine, Item element) {
    if (!lambda_item_is_element(element)) {
        return NULL;
    }
    
    const char* op_name = get_lambda_element_operator_name(element);
    if (!op_name) {
        return NULL;
    }
    
    // Dispatch based on operator name
    if (strcmp(op_name, "frac") == 0) {
        return convert_math_fraction(engine, element);
    } else if (strcmp(op_name, "pow") == 0) {
        return convert_math_superscript(engine, element);
    } else if (strcmp(op_name, "subscript") == 0) {
        return convert_math_subscript(engine, element);
    } else if (strcmp(op_name, "sqrt") == 0 || strcmp(op_name, "root") == 0) {
        return convert_math_radical(engine, element);
    } else if (strcmp(op_name, "sum") == 0 || strcmp(op_name, "prod") == 0) {
        return convert_math_sum_product(engine, element);
    } else if (strcmp(op_name, "int") == 0 || strcmp(op_name, "oint") == 0) {
        return convert_math_integral(engine, element);
    } else if (strcmp(op_name, "matrix") == 0 || strcmp(op_name, "pmatrix") == 0 || 
               strcmp(op_name, "bmatrix") == 0) {
        return convert_math_matrix(engine, element);
    } else if (is_function_name(op_name)) {
        return convert_math_function(engine, element);
    } else if (is_math_operator(op_name)) {
        return convert_math_operator(engine, element);
    } else if (strstr(op_name, "_space") != NULL) {
        return convert_math_spacing(engine, element);
    } else if (strcmp(op_name, "hat") == 0 || strcmp(op_name, "tilde") == 0 || 
               strcmp(op_name, "bar") == 0) {
        return convert_math_accent(engine, element);
    } else {
        // Default: treat as symbol
        return convert_math_symbol(engine, element);
    }
}

// Convert fraction element
ViewNode* convert_math_fraction(TypesetEngine* engine, Item frac_element) {
    if (get_lambda_element_child_count(frac_element) < 2) {
        log_error("convert_math_fraction: Fraction requires 2 children");
        return NULL;
    }
    
    Item numerator_item = get_lambda_element_child(frac_element, 0);
    Item denominator_item = get_lambda_element_child(frac_element, 1);
    
    ViewNode* numerator = convert_lambda_math_to_viewnode(engine, numerator_item);
    ViewNode* denominator = convert_lambda_math_to_viewnode(engine, denominator_item);
    
    if (!numerator || !denominator) {
        if (numerator) view_node_release(numerator);
        if (denominator) view_node_release(denominator);
        return NULL;
    }
    
    return create_math_fraction_node(numerator, denominator);
}

// Convert superscript (power) element
ViewNode* convert_math_superscript(TypesetEngine* engine, Item pow_element) {
    if (get_lambda_element_child_count(pow_element) < 2) {
        log_error("convert_math_superscript: Power requires 2 children");
        return NULL;
    }
    
    Item base_item = get_lambda_element_child(pow_element, 0);
    Item exponent_item = get_lambda_element_child(pow_element, 1);
    
    ViewNode* base = convert_lambda_math_to_viewnode(engine, base_item);
    ViewNode* exponent = convert_lambda_math_to_viewnode(engine, exponent_item);
    
    if (!base || !exponent) {
        if (base) view_node_release(base);
        if (exponent) view_node_release(exponent);
        return NULL;
    }
    
    return create_math_script_node(base, exponent, true); // true = superscript
}

// Convert subscript element
ViewNode* convert_math_subscript(TypesetEngine* engine, Item sub_element) {
    if (get_lambda_element_child_count(sub_element) < 2) {
        log_error("convert_math_subscript: Subscript requires 2 children");
        return NULL;
    }
    
    Item base_item = get_lambda_element_child(sub_element, 0);
    Item subscript_item = get_lambda_element_child(sub_element, 1);
    
    ViewNode* base = convert_lambda_math_to_viewnode(engine, base_item);
    ViewNode* subscript = convert_lambda_math_to_viewnode(engine, subscript_item);
    
    if (!base || !subscript) {
        if (base) view_node_release(base);
        if (subscript) view_node_release(subscript);
        return NULL;
    }
    
    return create_math_script_node(base, subscript, false); // false = subscript
}

// Convert radical (sqrt/root) element
ViewNode* convert_math_radical(TypesetEngine* engine, Item sqrt_element) {
    const char* op_name = get_lambda_element_operator_name(sqrt_element);
    int child_count = get_lambda_element_child_count(sqrt_element);
    
    if (child_count < 1) {
        log_error("convert_math_radical: Radical requires at least 1 child");
        return NULL;
    }
    
    ViewNode* radicand = NULL;
    ViewNode* index = NULL;
    
    if (strcmp(op_name, "sqrt") == 0) {
        // Square root: only radicand
        Item radicand_item = get_lambda_element_child(sqrt_element, 0);
        radicand = convert_lambda_math_to_viewnode(engine, radicand_item);
    } else if (strcmp(op_name, "root") == 0) {
        // Nth root: index and radicand
        if (child_count < 2) {
            log_error("convert_math_radical: Root requires 2 children");
            return NULL;
        }
        
        Item index_item = get_lambda_element_child(sqrt_element, 0);
        Item radicand_item = get_lambda_element_child(sqrt_element, 1);
        
        index = convert_lambda_math_to_viewnode(engine, index_item);
        radicand = convert_lambda_math_to_viewnode(engine, radicand_item);
    }
    
    if (!radicand) {
        if (index) view_node_release(index);
        return NULL;
    }
    
    return create_math_radical_node(radicand, index);
}

// Convert text/symbol to math atom
ViewNode* convert_math_text(TypesetEngine* engine, Item text_item) {
    if (!lambda_item_is_string(text_item)) {
        return NULL;
    }
    
    // Extract text from Lambda string
    char* text = extract_text_content_from_lambda_item(text_item);
    if (!text) {
        return NULL;
    }
    
    // Get Unicode representation if available
    const char* unicode = get_unicode_for_symbol(text);
    
    ViewNode* atom = create_math_atom_node(text, unicode);
    
    free(text);
    return atom;
}

// Convert symbol element to math atom
ViewNode* convert_math_symbol(TypesetEngine* engine, Item symbol_item) {
    const char* op_name = get_lambda_element_operator_name(symbol_item);
    if (!op_name) {
        return convert_math_text(engine, symbol_item);
    }
    
    // Look up Unicode representation
    const char* unicode = get_unicode_for_latex_symbol(op_name);
    
    return create_math_atom_node(op_name, unicode);
}

// Utility functions
bool lambda_element_has_operator(Item element, const char* op_name) {
    if (!lambda_item_is_element(element) || !op_name) {
        return false;
    }
    
    const char* element_op = get_lambda_element_operator_name(element);
    return element_op && strcmp(element_op, op_name) == 0;
}

const char* get_lambda_element_operator_name(Item element) {
    // This would need to interface with the actual Lambda data structures
    // For now, return a placeholder
    return "unknown"; // TODO: Implement actual operator name extraction
}

Item get_lambda_element_child(Item element, int index) {
    // This would need to interface with the actual Lambda data structures
    // For now, return error
    return (Item){.item = ITEM_ERROR}; // TODO: Implement actual child access
}

int get_lambda_element_child_count(Item element) {
    // This would need to interface with the actual Lambda data structures
    // For now, return 0
    return 0; // TODO: Implement actual child count
}

bool lambda_item_is_element(Item item) {
    // This would need to interface with the actual Lambda data structures
    return false; // TODO: Implement actual element detection
}

bool lambda_item_is_string(Item item) {
    // This would need to interface with the actual Lambda data structures
    return false; // TODO: Implement actual string detection
}

bool lambda_item_is_list(Item item) {
    // This would need to interface with the actual Lambda data structures
    return false; // TODO: Implement actual list detection
}

int lambda_item_get_list_length(Item item) {
    // This would need to interface with the actual Lambda data structures
    return 0; // TODO: Implement actual list length
}

Item lambda_item_get_list_element(Item item, int index) {
    // This would need to interface with the actual Lambda data structures
    return (Item){.item = ITEM_ERROR}; // TODO: Implement actual list element access
}

char* extract_text_content_from_lambda_item(Item item) {
    // This would need to interface with the actual Lambda data structures
    return NULL; // TODO: Implement actual text extraction
}

// Symbol lookup functions
const char* get_unicode_for_latex_symbol(const char* latex_cmd) {
    if (!latex_cmd) return NULL;
    
    for (int i = 0; symbol_mappings[i].latex_cmd; i++) {
        if (strcmp(symbol_mappings[i].latex_cmd, latex_cmd) == 0) {
            return symbol_mappings[i].unicode;
        }
    }
    
    return NULL;
}

const char* get_unicode_for_symbol(const char* symbol) {
    return get_unicode_for_latex_symbol(symbol);
}

enum ViewMathClass get_math_class_from_operator(const char* op_name) {
    if (!op_name) return VIEW_MATH_ORD;
    
    for (int i = 0; symbol_mappings[i].latex_cmd; i++) {
        if (strcmp(symbol_mappings[i].latex_cmd, op_name) == 0) {
            return symbol_mappings[i].math_class;
        }
    }
    
    return VIEW_MATH_ORD;
}

bool is_math_operator(const char* op_name) {
    return get_math_class_from_operator(op_name) == VIEW_MATH_BIN ||
           get_math_class_from_operator(op_name) == VIEW_MATH_REL;
}

bool is_large_operator(const char* op_name) {
    if (!op_name) return false;
    
    for (int i = 0; large_operators[i]; i++) {
        if (strcmp(large_operators[i], op_name) == 0) {
            return true;
        }
    }
    
    return false;
}

bool is_function_name(const char* name) {
    if (!name) return false;
    
    for (int i = 0; function_names[i]; i++) {
        if (strcmp(function_names[i], name) == 0) {
            return true;
        }
    }
    
    return false;
}

// Placeholder implementations for remaining functions
ViewNode* convert_math_sum_product(TypesetEngine* engine, Item sum_element) {
    // TODO: Implement sum/product conversion
    return NULL;
}

ViewNode* convert_math_integral(TypesetEngine* engine, Item int_element) {
    // TODO: Implement integral conversion
    return NULL;
}

ViewNode* convert_math_matrix(TypesetEngine* engine, Item matrix_element) {
    // TODO: Implement matrix conversion
    return NULL;
}

ViewNode* convert_math_function(TypesetEngine* engine, Item func_element) {
    // TODO: Implement function conversion
    return NULL;
}

ViewNode* convert_math_operator(TypesetEngine* engine, Item op_element) {
    // TODO: Implement operator conversion
    return NULL;
}

ViewNode* convert_math_spacing(TypesetEngine* engine, Item spacing_element) {
    // TODO: Implement spacing conversion
    return NULL;
}

ViewNode* convert_math_accent(TypesetEngine* engine, Item accent_element) {
    // TODO: Implement accent conversion
    return NULL;
}

ViewNode* convert_math_delimiter(TypesetEngine* engine, Item delim_element) {
    // TODO: Implement delimiter conversion
    return NULL;
}

// Options management
MathConversionOptions* math_conversion_options_create(void) {
    MathConversionOptions* options = calloc(1, sizeof(MathConversionOptions));
    if (options) {
        options->default_style = VIEW_MATH_TEXT;
        options->math_scale = 1.0;
        options->use_display_mode = false;
        options->math_font_family = "Latin Modern Math";
        options->render_equation_numbers = false;
    }
    return options;
}

void math_conversion_options_destroy(MathConversionOptions* options) {
    if (options) {
        free(options);
    }
}
