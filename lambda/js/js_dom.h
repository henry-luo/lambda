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
 *   firstElementChild, lastElementChild, nextElementSibling,
 *   previousElementSibling, childElementCount, nodeType
 * Falls back to getAttribute for unrecognized properties.
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with property name
 * @return Property value as Item
 */
Item js_dom_get_property(Item elem, Item prop_name);

// =============================================================================
// Element Method Dispatcher
// =============================================================================

/**
 * Dispatch elem.method(args) calls on DOM elements.
 * Supported: getAttribute, setAttribute, hasAttribute, removeAttribute,
 *   querySelector, querySelectorAll, matches, closest,
 *   appendChild, removeChild, insertBefore
 * @param elem        Wrapped DOM element Item
 * @param method_name String Item with method name
 * @param args        Array of argument Items
 * @param argc        Argument count
 * @return Result Item
 */
Item js_dom_element_method(Item elem, Item method_name, Item* args, int argc);

#ifdef __cplusplus
}
#endif
