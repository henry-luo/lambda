/**
 * JavaScript CSSOM (CSS Object Model) Bridge for Lambda
 *
 * Wraps Lambda's CSS parser structures (CssStylesheet, CssRule, CssDeclaration)
 * to expose standard CSSOM APIs from JavaScript:
 *   - document.styleSheets / document.styleSheets[N]
 *   - HTMLStyleElement.sheet
 *   - CSSStyleSheet.cssRules / .rules / .insertRule() / .deleteRule()
 *   - CSSStyleRule.selectorText (read/write) / .style / .cssText
 *   - CSSStyleDeclaration property access for rule declarations
 *
 * Uses the same sentinel-marker Map wrapping pattern as js_dom.cpp.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// =============================================================================
// CSSOM Type Checking
// =============================================================================

/** Check if an Item is a wrapped CSSStyleSheet */
bool js_is_stylesheet(Item item);

/** Check if an Item is a wrapped CSSStyleRule */
bool js_is_css_rule(Item item);

/** Check if an Item is a wrapped CSSStyleDeclaration (rule declarations) */
bool js_is_rule_style_decl(Item item);

// =============================================================================
// CSSStyleSheet Wrapper
// =============================================================================

/**
 * Wrap a CssStylesheet* into a Lambda Item.
 * @param stylesheet  CssStylesheet* (void* for C linkage)
 * @return Item wrapping the stylesheet
 */
Item js_cssom_wrap_stylesheet(void* stylesheet);

/**
 * Get property of a CSSStyleSheet wrapper.
 * Supported: cssRules, rules, length, disabled, type
 * @param sheet_item  Wrapped stylesheet Item
 * @param prop_name   String Item with property name
 * @return Property value
 */
Item js_cssom_stylesheet_get_property(Item sheet_item, Item prop_name);

/**
 * Call method on a CSSStyleSheet wrapper.
 * Supported: insertRule(text, index), deleteRule(index)
 * @param sheet_item   Wrapped stylesheet Item
 * @param method_name  String Item with method name
 * @param args         Argument array
 * @param argc         Argument count
 * @return Result Item
 */
Item js_cssom_stylesheet_method(Item sheet_item, Item method_name, Item* args, int argc);

// =============================================================================
// CSSRule Wrapper
// =============================================================================

/**
 * Wrap a CssRule* into a Lambda Item.
 * @param rule  CssRule* (void* for C linkage)
 * @param pool  Pool* for serialization (stored in data_cap)
 * @return Item wrapping the rule
 */
Item js_cssom_wrap_rule(void* rule, void* pool);

/**
 * Get property of a CSSStyleRule wrapper.
 * Supported: selectorText, style, cssText, type
 * @param rule_item  Wrapped rule Item
 * @param prop_name  String Item with property name
 * @return Property value
 */
Item js_cssom_rule_get_property(Item rule_item, Item prop_name);

/**
 * Set property on a CSSStyleRule wrapper.
 * Supported: selectorText (re-parses selector)
 * @param rule_item  Wrapped rule Item
 * @param prop_name  String Item with property name
 * @param value      Value to set
 * @return The value that was set
 */
Item js_cssom_rule_set_property(Item rule_item, Item prop_name, Item value);

// =============================================================================
// CSSStyleDeclaration Wrapper (for rule declarations)
// =============================================================================

/**
 * Get property of a CSSStyleDeclaration wrapper (rule declarations).
 * Supports camelCase CSS property access and getPropertyValue().
 * @param decl_item  Wrapped declaration Item
 * @param prop_name  String Item with property name
 * @return Property value as string Item
 */
Item js_cssom_rule_decl_get_property(Item decl_item, Item prop_name);

/**
 * Set property on a CSSStyleDeclaration wrapper (rule declarations).
 * Parses the value as CSS and replaces or adds the declaration.
 * @param decl_item  Wrapped declaration Item
 * @param prop_name  String Item with property name (camelCase or CSS)
 * @param value      String Item with CSS value
 * @return The value that was set
 */
Item js_cssom_rule_decl_set_property(Item decl_item, Item prop_name, Item value);

/**
 * Call method on a CSSStyleDeclaration wrapper.
 * Supported: getPropertyValue(prop), setProperty(prop, value), removeProperty(prop)
 * @param decl_item    Wrapped declaration Item
 * @param method_name  String Item with method name
 * @param args         Argument array
 * @param argc         Argument count
 * @return Result Item
 */
Item js_cssom_rule_decl_method(Item decl_item, Item method_name, Item* args, int argc);

// =============================================================================
// document.styleSheets Access
// =============================================================================

/**
 * Get the document.styleSheets collection.
 * Returns an array-like Item with stylesheet wrappers.
 * @return Array Item of wrapped stylesheets, or ITEM_NULL
 */
Item js_cssom_get_document_stylesheets(void);

// =============================================================================
// HTMLStyleElement .sheet Access
// =============================================================================

/**
 * Get the .sheet property of a <style> element.
 * Searches document's stylesheet list for the one parsed from this element.
 * @param elem  Wrapped DOM element Item (must be a <style> element)
 * @return Wrapped CSSStyleSheet Item, or ITEM_NULL
 */
Item js_cssom_get_style_element_sheet(Item elem);

// =============================================================================
// CSS Namespace Object (CSS.supports, CSS.escape)
// =============================================================================

/** Check if an Item is the CSS namespace object */
bool js_is_css_namespace(Item item);

/**
 * Get the global CSS namespace object (for CSS.supports(), CSS.escape()).
 * @return Item wrapping the CSS namespace
 */
Item js_get_css_object_value(void);

/**
 * Call method on the CSS namespace object.
 * Supported: supports(property, value), supports(conditionText), escape(ident)
 */
Item js_css_namespace_method(Item obj, Item method_name, Item* args, int argc);

/** Reset the CSS namespace object (for cleanup between tests) */
void js_reset_css_namespace_object(void);

#ifdef __cplusplus
}
#endif
