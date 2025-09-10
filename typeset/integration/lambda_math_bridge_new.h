#pragma once

#include "../view/view_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for Lambda types
typedef struct Element Element;
typedef struct String String;
typedef struct Item Item;

// Integration between Lambda math parser and view tree system
// Converts Lambda element trees (from input-math.cpp) to ViewTree structures

// Symbol mapping for math notation conversion
typedef struct {
    const char* lambda_element;
    const char* unicode_symbol;
    enum ViewMathClass math_class;
} MathSymbolMapping;

// Element type mapping for conversion
typedef struct {
    const char* lambda_element;
    enum ViewMathElementType view_type;
} MathElementMapping;

// Main conversion functions

/**
 * Convert Lambda math element tree to ViewTree
 * @param lambda_root Root element from Lambda math parser
 * @return ViewTree suitable for typesetting, or NULL on error
 */
ViewTree* convert_lambda_math_to_viewtree(Element* lambda_root);

/**
 * Convert individual Lambda element to ViewNode
 * @param lambda_element Lambda element to convert
 * @return ViewNode, or NULL on error
 */
ViewNode* convert_lambda_element_to_viewnode(Element* lambda_element);

// Specialized conversion functions for different math constructs

/**
 * Convert Lambda fraction element (e.g., from \frac{a}{b})
 * @param lambda_element Lambda fraction element
 * @param view_node Pre-allocated ViewNode to populate
 * @return Populated ViewNode, or NULL on error
 */
ViewNode* convert_lambda_fraction_element(Element* lambda_element, ViewNode* view_node);

/**
 * Convert Lambda radical element (e.g., from \sqrt{x} or \sqrt[n]{x})
 * @param lambda_element Lambda radical element
 * @param view_node Pre-allocated ViewNode to populate
 * @return Populated ViewNode, or NULL on error
 */
ViewNode* convert_lambda_radical_element(Element* lambda_element, ViewNode* view_node);

/**
 * Convert Lambda script element (e.g., from x^2 or x_i)
 * @param lambda_element Lambda script element
 * @param view_node Pre-allocated ViewNode to populate
 * @return Populated ViewNode, or NULL on error
 */
ViewNode* convert_lambda_script_element(Element* lambda_element, ViewNode* view_node);

/**
 * Convert Lambda operator element (e.g., from \sum, \int, etc.)
 * @param lambda_element Lambda operator element
 * @param view_node Pre-allocated ViewNode to populate
 * @return Populated ViewNode, or NULL on error
 */
ViewNode* convert_lambda_operator_element(Element* lambda_element, ViewNode* view_node);

/**
 * Convert Lambda matrix element 
 * @param lambda_element Lambda matrix element
 * @param view_node Pre-allocated ViewNode to populate
 * @return Populated ViewNode, or NULL on error
 */
ViewNode* convert_lambda_matrix_element(Element* lambda_element, ViewNode* view_node);

/**
 * Convert Lambda accent element (e.g., from \hat{x}, \tilde{y})
 * @param lambda_element Lambda accent element
 * @param view_node Pre-allocated ViewNode to populate
 * @return Populated ViewNode, or NULL on error
 */
ViewNode* convert_lambda_accent_element(Element* lambda_element, ViewNode* view_node);

/**
 * Convert Lambda atom element (symbols, variables, numbers, etc.)
 * @param lambda_element Lambda atom element
 * @param view_node Pre-allocated ViewNode to populate
 * @return Populated ViewNode, or NULL on error
 */
ViewNode* convert_lambda_atom_element(Element* lambda_element, ViewNode* view_node);

// Utility functions for Lambda integration

/**
 * Create string from C string (wrapper for Lambda string system)
 * @param cstr C string to convert
 * @return Lambda String object, or NULL on error
 */
String* string_from_cstr(const char* cstr);

/**
 * Destroy Lambda string object
 * @param str String to destroy
 */
void string_destroy(String* str);

/**
 * Get type name from Lambda element
 * @param element Lambda element
 * @return Element type name, or NULL if not available
 */
const char* lambda_element_get_type_name(Element* element);

/**
 * Get string attribute from Lambda element
 * @param element Lambda element
 * @param attr_name Attribute name to retrieve
 * @return String value, or NULL if not found
 */
String* lambda_element_get_string_attribute(Element* element, const char* attr_name);

#ifdef __cplusplus
}
#endif
