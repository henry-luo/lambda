/**
 * JavaScript DOM API Bridge for Lambda
 *
 * Wraps Lambda's Element data model and Radiant's DomElement layer
 * to expose standard DOM manipulation APIs from JavaScript.
 *
 * Wrapping strategy:
 *   DomElement* is wrapped in a Map with a distinct type marker
 *   (js_dom_type_marker address) and the DomElement* stored in Map::data.
 *   This allows efficient O(1) wrapping/unwrapping with zero HashMap overhead.
 *
 * All functions use extern "C" for MIR JIT compatibility.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// =============================================================================
// DOM Context Management
// =============================================================================

/**
 * Set the current DomDocument for JS execution.
 * Must be called before executing JS code that accesses the DOM.
 * @param dom_doc  DomDocument* (void* for C linkage compatibility)
 */
void js_dom_set_document(void* dom_doc);

/**
 * Get the current DomDocument.
 * @return DomDocument* cast to void*, or NULL if no document is set
 */
void* js_dom_get_document(void);

// =============================================================================
// DOM Wrapping / Unwrapping
// =============================================================================

/**
 * Wrap a DomElement* into a Lambda Item (Map with DOM type marker).
 * @param dom_elem  DomElement* (void* for C linkage)
 * @return Item wrapping the element, or ITEM_NULL if dom_elem is NULL
 */
Item js_dom_wrap_element(void* dom_elem);

/**
 * Unwrap a Lambda Item to get the DomElement*.
 * @param item  Item previously returned by js_dom_wrap_element
 * @return DomElement* or NULL if item is not a DOM node
 */
void* js_dom_unwrap_element(Item item);

/**
 * Check if an Item is a wrapped DOM node.
 * @param item  Item to test
 * @return true if item is a wrapped DomElement
 */
bool js_is_dom_node(Item item);

// =============================================================================
// Document Method Dispatcher
// =============================================================================

/**
 * Dispatch document.method(args) calls.
 * Supported methods: getElementById, getElementsByClassName, getElementsByTagName,
 *   querySelector, querySelectorAll, createElement, createTextNode
 * @param method_name String Item with method name
 * @param args        Array of argument Items
 * @param argc        Argument count
 * @return Result Item
 */
Item js_document_method(Item method_name, Item* args, int argc);

/**
 * Dispatch document.property access.
 * Supported properties: body, documentElement, head, title
 * @param prop_name String Item with property name
 * @return Property value as Item
 */
Item js_document_get_property(Item prop_name);

// =============================================================================
// Element Property Access (DOM-aware)
// =============================================================================

/**
 * Get a DOM element property.
 * Supported: tagName, id, className, textContent, children, parentElement,
 *   parentNode, firstChild, lastChild, firstElementChild, lastElementChild,
 *   nextSibling, previousSibling, nextElementSibling, previousElementSibling,
 *   childNodes, childElementCount, nodeType, offsetWidth, offsetHeight,
 *   clientWidth, clientHeight, data (text nodes)
 * Falls back to getAttribute for unrecognized properties.
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with property name
 * @return Property value as Item
 */
Item js_dom_get_property(Item elem, Item prop_name);

/**
 * Set a DOM element property.
 * Supported: className, id, textContent, data (text nodes)
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with property name
 * @param value      Value to set
 * @return The value that was set, or ITEM_NULL on failure
 */
Item js_dom_set_property(Item elem, Item prop_name, Item value);

/**
 * Set a CSS inline style property on a DOM element.
 * Converts camelCase JS property names to CSS hyphenated form.
 * E.g., "fontFamily" → "font-family", "display" → "display"
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with JS-style property name
 * @param value      String Item with CSS value
 * @return The value that was set, or ITEM_NULL on failure
 */
Item js_dom_set_style_property(Item elem, Item prop_name, Item value);
Item js_dom_get_style_property(Item elem, Item prop_name);

// =============================================================================
// Computed Style (window.getComputedStyle)
// =============================================================================

/**
 * Get computed style object for a DOM element.
 * Returns a wrapper object whose properties resolve to computed CSS values.
 * @param elem   Wrapped DOM element Item
 * @param pseudo String Item for pseudo-element ("before", "after") or null
 * @return Computed style wrapper Item
 */
Item js_get_computed_style(Item elem, Item pseudo);

/**
 * Get a computed CSS property value from a computed style wrapper.
 * @param style_item  Computed style wrapper from js_get_computed_style
 * @param prop_name   String Item with CSS property name (camelCase or hyphenated)
 * @return String Item with the computed CSS value
 */
Item js_computed_style_get_property(Item style_item, Item prop_name);

/**
 * Check if an Item is a computed style wrapper object.
 * @param item  Item to test
 * @return true if item wraps a computed style
 */
bool js_is_computed_style_item(Item item);

// =============================================================================
// Element Method Dispatcher
// =============================================================================

/**
 * Dispatch elem.method(args) calls on DOM elements.
 * Supported: getAttribute, setAttribute, hasAttribute, removeAttribute,
 *   querySelector, querySelectorAll, matches, closest,
 *   appendChild, removeChild, insertBefore,
 *   hasChildNodes, normalize, cloneNode
 * @param elem        Wrapped DOM element Item
 * @param method_name String Item with method name
 * @param args        Array of argument Items
 * @param argc        Argument count
 * @return Result Item
 */
Item js_dom_element_method(Item elem, Item method_name, Item* args, int argc);

// =============================================================================
// classList API (v12)
// =============================================================================

/**
 * Dispatch classList.method(args) calls.
 * Supported: add, remove, toggle, contains, item, replace, entries, forEach, toString
 * @param elem        Wrapped DOM element Item
 * @param method_name String Item with method name
 * @param args        Array of argument Items
 * @param argc        Argument count
 * @return Result Item
 */
Item js_classlist_method(Item elem, Item method_name, Item* args, int argc);

/**
 * Get a classList property.
 * Supported: length, value
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with property name
 * @return Property value as Item
 */
Item js_classlist_get_property(Item elem, Item prop_name);

// =============================================================================
// dataset API (v12)
// =============================================================================

/**
 * Get a dataset property (camelCase → data-kebab-case attribute).
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with camelCase property name
 * @return String value or undefined
 */
Item js_dataset_get_property(Item elem, Item prop_name);

/**
 * Set a dataset property (camelCase → data-kebab-case attribute).
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with camelCase property name
 * @param value      String value to set
 * @return The value that was set
 */
Item js_dataset_set_property(Item elem, Item prop_name, Item value);

// =============================================================================
// location API (v12)
// =============================================================================

/**
 * Get a location/URL property.
 * Supported: href, protocol, hostname, port, pathname, search, hash, host, origin
 * @param prop_name  String Item with property name
 * @return String value
 */
Item js_location_get_property(Item prop_name);

// =============================================================================
// Node.contains() (v12)
// =============================================================================

/**
 * Check if a node contains another node (or is the same node).
 * @param elem   Wrapped DOM element (parent)
 * @param other  Wrapped DOM element (potential descendant)
 * @return Boolean Item
 */
Item js_dom_contains(Item elem, Item other);

// =============================================================================
// style.setProperty() / style.removeProperty() (v12b)
// =============================================================================

/**
 * Dispatch style method calls: setProperty, removeProperty.
 * @param elem        Wrapped DOM element Item
 * @param method_name String Item with method name
 * @param args        Array of argument Items
 * @param argc        Argument count
 * @return Result Item
 */
Item js_dom_style_method(Item elem, Item method_name, Item* args, int argc);

#ifdef __cplusplus
}
#endif
