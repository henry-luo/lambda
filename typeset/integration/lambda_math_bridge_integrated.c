#include "lambda_math_bridge.h"
#include "../view/view_tree.h"
#include "../../lambda/lambda.h"
#include "../../lambda/lambda-data.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"
#include <string.h>
#include <stdlib.h>

// Integration with actual Lambda math parser and element system

// Symbol mapping between Lambda element names and math notation
static const struct {
    const char* lambda_element;
    const char* unicode_symbol;
    enum ViewMathClass math_class;
} symbol_mappings[] = {
    // Greek letters (from Lambda math parser)
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
    
    // Operators (from Lambda math parser) 
    {"add", "+", VIEW_MATH_BIN},
    {"sub", "−", VIEW_MATH_BIN},
    {"mul", "×", VIEW_MATH_BIN},
    {"div", "÷", VIEW_MATH_BIN},
    {"pm", "±", VIEW_MATH_BIN},
    {"mp", "∓", VIEW_MATH_BIN},
    {"times", "×", VIEW_MATH_BIN},
    {"cdot", "⋅", VIEW_MATH_BIN},
    {"ast", "∗", VIEW_MATH_BIN},
    
    // Relations (from Lambda math parser)
    {"eq", "=", VIEW_MATH_REL},
    {"ne", "≠", VIEW_MATH_REL},
    {"lt", "<", VIEW_MATH_REL},
    {"le", "≤", VIEW_MATH_REL},
    {"gt", ">", VIEW_MATH_REL},
    {"ge", "≥", VIEW_MATH_REL},
    {"approx", "≈", VIEW_MATH_REL},
    {"equiv", "≡", VIEW_MATH_REL},
    
    // Functions (from Lambda math parser)
    {"sin", "sin", VIEW_MATH_OP},
    {"cos", "cos", VIEW_MATH_OP},
    {"tan", "tan", VIEW_MATH_OP},
    {"log", "log", VIEW_MATH_OP},
    {"ln", "ln", VIEW_MATH_OP},
    {"exp", "exp", VIEW_MATH_OP},
    
    // Big operators (from Lambda math parser)
    {"sum", "∑", VIEW_MATH_OP},
    {"prod", "∏", VIEW_MATH_OP},
    {"int", "∫", VIEW_MATH_OP},
    
    {NULL, NULL, VIEW_MATH_ORD}
};

// Lambda element type name mapping
static const struct {
    const char* lambda_element;
    enum ViewMathElementType view_type;
} element_type_mappings[] = {
    {"frac", VIEW_MATH_FRACTION},
    {"sqrt", VIEW_MATH_RADICAL},
    {"root", VIEW_MATH_RADICAL},
    {"pow", VIEW_MATH_SCRIPT},
    {"subscript", VIEW_MATH_SCRIPT},
    {"sum", VIEW_MATH_OPERATOR},
    {"prod", VIEW_MATH_OPERATOR},
    {"int", VIEW_MATH_OPERATOR},
    {"matrix", VIEW_MATH_MATRIX},
    {"pmatrix", VIEW_MATH_MATRIX},
    {"bmatrix", VIEW_MATH_MATRIX},
    {"vmatrix", VIEW_MATH_MATRIX},
    {"cases", VIEW_MATH_MATRIX},
    {"align", VIEW_MATH_MATRIX},
    {"hat", VIEW_MATH_ACCENT},
    {"tilde", VIEW_MATH_ACCENT},
    {"bar", VIEW_MATH_ACCENT},
    {"dot", VIEW_MATH_ACCENT},
    {"ddot", VIEW_MATH_ACCENT},
    {NULL, VIEW_MATH_ATOM}
};

// Get string attribute from Lambda element (safe wrapper)
static String* lambda_element_get_string_attribute(Element* element, const char* attr_name) {
    if (!element || !attr_name) return NULL;
    
    // Create string item for attribute name
    String* attr_string = string_from_cstr(attr_name);
    if (!attr_string) return NULL;
    
    Item attr_key = {.item = (uint64_t)attr_string};
    
    // Get attribute value from element
    Item attr_value = elmt_get(element, attr_key);
    
    // Clean up attribute name string
    string_destroy(attr_string);
    
    // Check if result is a string
    if (attr_value.item == ITEM_NULL || attr_value.item == ITEM_ERROR) {
        return NULL;
    }
    
    // Extract string from typed item
    TypedItem* typed_item = (TypedItem*)attr_value.item;
    if (typed_item && typed_item->type_id == LMD_TYPE_STRING) {
        return typed_item->string;
    }
    
    return NULL;
}

// Get element type name from Lambda element
static const char* lambda_element_get_type_name(Element* element) {
    if (!element) return NULL;
    
    // Access the type information from the element
    TypeElmt* type_elmt = (TypeElmt*)element->type;
    if (!type_elmt) return NULL;
    
    // Return the element name from the type
    if (type_elmt->name.str) {
        return type_elmt->name.str;
    }
    
    return "unknown";
}

// Convert Lambda element to ViewNode
ViewNode* convert_lambda_element_to_viewnode(Element* lambda_element) {
    if (!lambda_element) return NULL;
    
    const char* element_name = lambda_element_get_type_name(lambda_element);
    if (!element_name) return NULL;
    
    // Determine ViewNode type based on Lambda element name
    enum ViewMathElementType view_type = VIEW_MATH_ATOM;
    for (int i = 0; element_type_mappings[i].lambda_element; i++) {
        if (strcmp(element_name, element_type_mappings[i].lambda_element) == 0) {
            view_type = element_type_mappings[i].view_type;
            break;
        }
    }
    
    // Create ViewNode
    ViewNode* view_node = view_node_create(VIEW_NODE_MATH_ELEMENT);
    if (!view_node) return NULL;
    
    view_node->content.math_element.element_type = view_type;
    
    // Handle different element types
    switch (view_type) {
        case VIEW_MATH_FRACTION:
            return convert_lambda_fraction_element(lambda_element, view_node);
            
        case VIEW_MATH_RADICAL:
            return convert_lambda_radical_element(lambda_element, view_node);
            
        case VIEW_MATH_SCRIPT:
            return convert_lambda_script_element(lambda_element, view_node);
            
        case VIEW_MATH_OPERATOR:
            return convert_lambda_operator_element(lambda_element, view_node);
            
        case VIEW_MATH_MATRIX:
            return convert_lambda_matrix_element(lambda_element, view_node);
            
        case VIEW_MATH_ACCENT:
            return convert_lambda_accent_element(lambda_element, view_node);
            
        case VIEW_MATH_ATOM:
        default:
            return convert_lambda_atom_element(lambda_element, view_node);
    }
}

// Convert Lambda fraction element (e.g., from \frac{a}{b})
ViewNode* convert_lambda_fraction_element(Element* lambda_element, ViewNode* view_node) {
    if (!lambda_element || !view_node) return NULL;
    
    view_node->content.math_element.content.fraction.style = MATH_FRACTION_DISPLAY;
    
    // Get numerator and denominator from Lambda element children
    // Lambda fractions have 2 children: numerator (index 0) and denominator (index 1)
    List* element_list = (List*)lambda_element;
    
    if (element_list->length >= 2) {
        // Get numerator (first child)
        Item numerator_item = list_get(element_list, 0);
        if (numerator_item.item != ITEM_NULL && numerator_item.item != ITEM_ERROR) {
            TypedItem* typed_num = (TypedItem*)numerator_item.item;
            if (typed_num && typed_num->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.fraction.numerator = 
                    convert_lambda_element_to_viewnode(typed_num->element);
            }
        }
        
        // Get denominator (second child)  
        Item denominator_item = list_get(element_list, 1);
        if (denominator_item.item != ITEM_NULL && denominator_item.item != ITEM_ERROR) {
            TypedItem* typed_denom = (TypedItem*)denominator_item.item;
            if (typed_denom && typed_denom->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.fraction.denominator = 
                    convert_lambda_element_to_viewnode(typed_denom->element);
            }
        }
    }
    
    return view_node;
}

// Convert Lambda radical element (e.g., from \sqrt{x} or \sqrt[n]{x})
ViewNode* convert_lambda_radical_element(Element* lambda_element, ViewNode* view_node) {
    if (!lambda_element || !view_node) return NULL;
    
    const char* element_name = lambda_element_get_type_name(lambda_element);
    
    if (strcmp(element_name, "sqrt") == 0) {
        view_node->content.math_element.content.radical.has_index = false;
    } else if (strcmp(element_name, "root") == 0) {
        view_node->content.math_element.content.radical.has_index = true;
    }
    
    List* element_list = (List*)lambda_element;
    
    if (view_node->content.math_element.content.radical.has_index && element_list->length >= 2) {
        // For root: first child is index, second is radicand
        Item index_item = list_get(element_list, 0);
        if (index_item.item != ITEM_NULL && index_item.item != ITEM_ERROR) {
            TypedItem* typed_index = (TypedItem*)index_item.item;
            if (typed_index && typed_index->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.radical.index = 
                    convert_lambda_element_to_viewnode(typed_index->element);
            }
        }
        
        Item radicand_item = list_get(element_list, 1);
        if (radicand_item.item != ITEM_NULL && radicand_item.item != ITEM_ERROR) {
            TypedItem* typed_radicand = (TypedItem*)radicand_item.item;
            if (typed_radicand && typed_radicand->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.radical.radicand = 
                    convert_lambda_element_to_viewnode(typed_radicand->element);
            }
        }
    } else if (element_list->length >= 1) {
        // For sqrt: only one child (radicand)
        Item radicand_item = list_get(element_list, 0);
        if (radicand_item.item != ITEM_NULL && radicand_item.item != ITEM_ERROR) {
            TypedItem* typed_radicand = (TypedItem*)radicand_item.item;
            if (typed_radicand && typed_radicand->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.radical.radicand = 
                    convert_lambda_element_to_viewnode(typed_radicand->element);
            }
        }
    }
    
    return view_node;
}

// Convert Lambda script element (e.g., from x^2 or x_i)
ViewNode* convert_lambda_script_element(Element* lambda_element, ViewNode* view_node) {
    if (!lambda_element || !view_node) return NULL;
    
    const char* element_name = lambda_element_get_type_name(lambda_element);
    
    if (strcmp(element_name, "pow") == 0) {
        view_node->content.math_element.content.script.script_type = MATH_SCRIPT_SUPERSCRIPT;
    } else if (strcmp(element_name, "subscript") == 0) {
        view_node->content.math_element.content.script.script_type = MATH_SCRIPT_SUBSCRIPT;
    }
    
    List* element_list = (List*)lambda_element;
    
    if (element_list->length >= 2) {
        // First child is base, second is script
        Item base_item = list_get(element_list, 0);
        if (base_item.item != ITEM_NULL && base_item.item != ITEM_ERROR) {
            TypedItem* typed_base = (TypedItem*)base_item.item;
            if (typed_base && typed_base->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.script.base = 
                    convert_lambda_element_to_viewnode(typed_base->element);
            }
        }
        
        Item script_item = list_get(element_list, 1);
        if (script_item.item != ITEM_NULL && script_item.item != ITEM_ERROR) {
            TypedItem* typed_script = (TypedItem*)script_item.item;
            if (typed_script && typed_script->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.script.script = 
                    convert_lambda_element_to_viewnode(typed_script->element);
            }
        }
    }
    
    return view_node;
}

// Convert Lambda operator element (e.g., from \sum, \int, etc.)
ViewNode* convert_lambda_operator_element(Element* lambda_element, ViewNode* view_node) {
    if (!lambda_element || !view_node) return NULL;
    
    const char* element_name = lambda_element_get_type_name(lambda_element);
    
    // Find symbol mapping
    for (int i = 0; symbol_mappings[i].lambda_element; i++) {
        if (strcmp(element_name, symbol_mappings[i].lambda_element) == 0) {
            view_node->content.math_element.content.operator.symbol = 
                strdup(symbol_mappings[i].unicode_symbol);
            view_node->content.math_element.content.operator.math_class = 
                symbol_mappings[i].math_class;
            break;
        }
    }
    
    // Handle limits for big operators
    List* element_list = (List*)lambda_element;
    if (element_list->length >= 2) {
        // Check for subscript (lower limit)
        Item lower_item = list_get(element_list, 0);
        if (lower_item.item != ITEM_NULL && lower_item.item != ITEM_ERROR) {
            TypedItem* typed_lower = (TypedItem*)lower_item.item;
            if (typed_lower && typed_lower->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.operator.lower_limit = 
                    convert_lambda_element_to_viewnode(typed_lower->element);
            }
        }
    }
    
    if (element_list->length >= 3) {
        // Check for superscript (upper limit)
        Item upper_item = list_get(element_list, 1);
        if (upper_item.item != ITEM_NULL && upper_item.item != ITEM_ERROR) {
            TypedItem* typed_upper = (TypedItem*)upper_item.item;
            if (typed_upper && typed_upper->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.operator.upper_limit = 
                    convert_lambda_element_to_viewnode(typed_upper->element);
            }
        }
    }
    
    return view_node;
}

// Convert Lambda matrix element (placeholder - matrices are complex)
ViewNode* convert_lambda_matrix_element(Element* lambda_element, ViewNode* view_node) {
    if (!lambda_element || !view_node) return NULL;
    
    // Basic matrix setup - full implementation would handle rows/columns
    view_node->content.math_element.content.matrix.rows = 1;
    view_node->content.math_element.content.matrix.cols = 1;
    view_node->content.math_element.content.matrix.elements = NULL; // TODO: implement
    
    return view_node;
}

// Convert Lambda accent element (e.g., from \hat{x}, \tilde{y})
ViewNode* convert_lambda_accent_element(Element* lambda_element, ViewNode* view_node) {
    if (!lambda_element || !view_node) return NULL;
    
    const char* element_name = lambda_element_get_type_name(lambda_element);
    
    // Map accent types
    if (strcmp(element_name, "hat") == 0) {
        view_node->content.math_element.content.accent.accent_symbol = strdup("^");
    } else if (strcmp(element_name, "tilde") == 0) {
        view_node->content.math_element.content.accent.accent_symbol = strdup("~");
    } else if (strcmp(element_name, "bar") == 0) {
        view_node->content.math_element.content.accent.accent_symbol = strdup("¯");
    } else if (strcmp(element_name, "dot") == 0) {
        view_node->content.math_element.content.accent.accent_symbol = strdup("˙");
    }
    
    // Get base from first child
    List* element_list = (List*)lambda_element;
    if (element_list->length >= 1) {
        Item base_item = list_get(element_list, 0);
        if (base_item.item != ITEM_NULL && base_item.item != ITEM_ERROR) {
            TypedItem* typed_base = (TypedItem*)base_item.item;
            if (typed_base && typed_base->type_id == LMD_TYPE_ELEMENT) {
                view_node->content.math_element.content.accent.base = 
                    convert_lambda_element_to_viewnode(typed_base->element);
            }
        }
    }
    
    return view_node;
}

// Convert Lambda atom element (symbols, variables, numbers, etc.)
ViewNode* convert_lambda_atom_element(Element* lambda_element, ViewNode* view_node) {
    if (!lambda_element || !view_node) return NULL;
    
    const char* element_name = lambda_element_get_type_name(lambda_element);
    
    // Check if it's a known symbol
    for (int i = 0; symbol_mappings[i].lambda_element; i++) {
        if (strcmp(element_name, symbol_mappings[i].lambda_element) == 0) {
            view_node->content.math_element.content.atom.symbol = 
                strdup(symbol_mappings[i].unicode_symbol);
            view_node->content.math_element.content.atom.math_class = 
                symbol_mappings[i].math_class;
            return view_node;
        }
    }
    
    // For unknown elements, use the element name as the symbol
    view_node->content.math_element.content.atom.symbol = strdup(element_name);
    view_node->content.math_element.content.atom.math_class = VIEW_MATH_ORD;
    
    return view_node;
}

// Main entry point: convert Lambda math tree to ViewTree
ViewTree* convert_lambda_math_to_viewtree(Element* lambda_root) {
    if (!lambda_root) return NULL;
    
    ViewTree* view_tree = view_tree_create();
    if (!view_tree) return NULL;
    
    // Set basic properties
    view_tree->title = strdup("Mathematical Expression");
    view_tree->creator = strdup("Lambda Math Typesetter");
    
    // Convert the root Lambda element
    ViewNode* root_node = convert_lambda_element_to_viewnode(lambda_root);
    if (!root_node) {
        view_tree_destroy(view_tree);
        return NULL;
    }
    
    view_tree->root = root_node;
    
    // Calculate document size based on content (basic estimation)
    view_tree->document_size.width = 400.0;  // Default width
    view_tree->document_size.height = 100.0; // Default height
    
    return view_tree;
}

// Helper function to create string from C string (wrapper for Lambda string system)
String* string_from_cstr(const char* cstr) {
    if (!cstr) return NULL;
    
    // Create string using Lambda's string system
    // This is a placeholder - actual implementation depends on Lambda's string API
    String* str = (String*)malloc(sizeof(String) + strlen(cstr) + 1);
    if (!str) return NULL;
    
    str->ref_cnt = 1;
    str->length = strlen(cstr);
    strcpy((char*)(str + 1), cstr);
    
    return str;
}

// Helper function to destroy string
void string_destroy(String* str) {
    if (!str) return;
    
    str->ref_cnt--;
    if (str->ref_cnt <= 0) {
        free(str);
    }
}
