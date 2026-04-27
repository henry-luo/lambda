/**
 * js_dom_selection.h — JS bindings for W3C Selection / Range
 *
 * Provides Range and Selection host objects backed by radiant/dom_range.{hpp,cpp}.
 * See vibe/radiant/Radiant_Design_Selection.md (Phase 2).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// Document-method entry points (called from js_document_method dispatcher).
Item js_dom_create_range(void);
Item js_dom_get_selection(void);

// Wrap an existing native DomRange* into a JS Range object (used by
// selection.getRangeAt). Returns ItemNull on failure. Takes a strong
// reference (caller can release theirs).
Item js_dom_wrap_range(void* dom_range);

// Test/extract helpers
bool js_dom_is_range_object(Item item);
bool js_dom_is_selection_object(Item item);
void* js_dom_unwrap_range(Item item);        // returns DomRange*
void* js_dom_unwrap_selection(Item item);    // returns DomSelection*

// Property dispatch hooks invoked from js_dom_get_property /
// js_dom_set_property when the wrapper's Map::type matches a Range or
// Selection sentinel. Returning ItemNull means "no such property".
Item js_dom_range_get_property    (Item obj, Item key);
Item js_dom_selection_get_property(Item obj, Item key);

// Identity check: does this Item have the Range / Selection marker in
// Map::type? (Used by js_dom dispatch and by `instanceof`-style code.)
bool js_dom_item_is_range    (Item item);
bool js_dom_item_is_selection(Item item);

// Install window.getSelection on the global object. Called from
// js_dom_set_document() once the document is bound.
void js_dom_selection_install_globals(void);

// Reset internal pools (call from js_dom batch reset).
void js_dom_selection_reset(void);

#ifdef __cplusplus
}  // extern "C"
#endif
