/**
 * JavaScript DOM API Bridge for Lambda
 *
 * Wraps Lambda's Element data model and Radiant's DomElement layer
 * to expose standard DOM manipulation APIs from JavaScript.
 *
 * Wrapping strategy:
 *   Radiant DOM nodes are branded native VMaps owned by the radiant bridge.
 *   Document and foreign-document proxies are branded native VMaps as well.
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
void dom_set_document(void* dom_doc);

/**
 * Get the current DomDocument.
 * @return DomDocument* cast to void*, or NULL if no document is set
 */
void* dom_get_document(void);

/**
 * Set the current Radiant UiContext for JS DOM layout/geometry queries.
 * The pointer is borrowed from the active JS document session.
 */
void dom_set_ui_context(void* ui_context);

/**
 * Get the current Radiant UiContext.
 */
void* dom_get_ui_context(void);

// Records whether the active document Runtime is owned by a host frame loop.
// This setting is context-local and must be applied after binding that Runtime.
void dom_set_host_driven_loop(bool enabled);
bool dom_is_host_driven_loop(void);

/**
 * Return whether an Array is a DOM-owned live collection whose property reads
 * can refresh named or indexed values from the current document tree.
 */
bool dom_collection_has_live_property_state(Item collection);

// DOMRect-shaped object: x/y/top/left/right/bottom/width/height as doubles,
// on interned keys. dom.cpp, dom_observers.cpp and dom_selection.cpp
// each built this themselves.
Item dom_make_rect(double x, double y, double width, double height);

/**
 * Return whether the document has a committed geometry snapshot.
 * This predicate never performs style resolution or layout.
 */
bool dom_has_committed_geometry_snapshot(void* dom_doc);

/**
 * Return the topmost painted SVG element at viewport coordinates, or NULL.
 * Native pointer dispatch uses this alongside regular CSS-box hit testing so
 * SVG descendants retain their geometric event target even though they do not
 * each own a CSS layout box.
 */
void* dom_document_svg_element_from_point(void* dom_doc, float x, float y);

/**
 * Return the exact native target used by document.elementFromPoint() without
 * allocating a JavaScript wrapper. Automation assertions use this bridge so
 * their result cannot diverge from the public DOM API.
 */
void* dom_document_element_from_point_native(void* dom_doc, float x, float y);

// Commits a pending transient-document reflow at the script/event-loop
// checkpoint. Long-lived Radiant sessions keep ownership of their frame loop.
bool dom_commit_headless_layout(void);

/** Advance the active document's CSS animation scheduler by one headless frame. */
bool dom_tick_headless_animation_frame(void);

/** Commit pending DOM mutations at a one-shot headless rendering checkpoint. */
bool dom_commit_headless_layout_checkpoint(void);

// Lazy DomElement* with tag "#document" used so JS Range/Selection APIs can
// accept `document` (or a foreign-doc wrapper) as a node container.
void* dom_get_or_create_doc_node(void* dom_doc);

/**
 * Focus a contenteditable host through the shared DOM Selection state without
 * constructing a JavaScript return value. Used by native automation for
 * Lambda template documents, which do not own a JS runtime.
 */
bool dom_focus_editing_host_for_automation(void* dom_elem);

// =============================================================================
// Named Element Access on Window
// =============================================================================

/**
 * Register all elements with 'id' attributes as properties on the global object.
 * Implements browser-like named access on the Window object.
 * @param root  DomElement* root of the DOM tree (void* for C linkage in header)
 */
#ifdef __cplusplus
struct DomElement;
void dom_register_named_elements(DomElement* root);
DomElement* dom_find_element_by_id(DomElement* root, const char* id);
#endif

/** Resolve and activate a popover target from a button activation. */
void* dom_popover_target_for_button(void* button);
int dom_popover_target_action(void* button);
bool dom_activate_popover(void* popover, int action);

// =============================================================================
// DOM Wrapping / Unwrapping
// =============================================================================

/**
 * Wrap a DomElement* into a Lambda Item (Map with DOM type marker).
 * @param dom_elem  DomElement* (void* for C linkage)
 * @return Item wrapping the element, or ITEM_NULL if dom_elem is NULL
 */
Item dom_wrap_element(void* dom_elem);

/**
 * Unwrap a Lambda Item to get the DomElement*.
 * @param item  Item previously returned by dom_wrap_element
 * @return DomElement* or NULL if item is not a DOM node
 */
void* dom_unwrap_element(Item item);

/**
 * Return an <option>'s live selectedness, including IDL writes to .selected.
 * This is distinct from the immutable selected content attribute.
 */
bool dom_option_selectedness(void* dom_elem);

/**
 * Create a selector matcher configured for a DOM document's live UI state.
 * The opaque return value is a SelectorMatcher* for Radiant's native bridge.
 */
void* dom_create_selector_matcher_bridge(void* dom_doc);

/** Return the identity-preserving Document proxy that owns a DOM node. */
Item dom_owner_document_for_node(void* node);

/**
 * Check if an Item is a wrapped DOM node.
 * @param item  Item to test
 * @return true if item is a wrapped DomElement
 */
bool js_is_dom_node(Item item);

/**
 * Check if an Item is a document proxy object.
 * @param item  Item to test
 * @return true if item is the document proxy
 */

/**
 * Get the document proxy object for bare 'document' identifier resolution.
 * Returns a singleton Map-based proxy whose callable properties are installed
 * as direct targets under D6.2.2v2; dynamic data properties use the getter.
 * @return Document proxy Item
 */
Item js_get_document_object_value(void);

/**
 * Resolve dynamic property access on the document proxy.
 */
Item js_document_proxy_get_property(Item prop_name);

/**
 * Dispatch property set on document proxy.
 */
Item js_document_proxy_set_property(Item prop_name, Item value);

// =============================================================================
// Foreign documents & document.implementation
// =============================================================================

/** Returns DomDocument* if `item` is a foreign-doc wrapper, else null. */
void* js_get_foreign_doc(Item item);

/** Returns true if `item` is the document.implementation singleton. */
bool js_is_dom_implementation(Item item);

/** Get (lazily creating) the document.implementation singleton. */
Item js_get_dom_implementation(void);

/** Build a foreign HTML document (used by createHTMLDocument). */
Item js_create_foreign_html_doc(const char* title);

/** Build a foreign XML/empty document (used by createDocument). */
Item js_create_foreign_xml_doc(const char* qualified_name);

/** Build a DocumentType node stub (used by createDocumentType). */
Item js_create_doctype_node(const char* name, const char* public_id, const char* system_id);

/** Save the current active document and switch to `new_doc`. Returns the prior doc. */
void* dom_swap_active_document(void* new_doc);
void  dom_restore_active_document(void* prev_doc);

// =============================================================================
// Document Method Dispatcher
// =============================================================================

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
 * Install a compiled event handler function into the DOM element's IDL
 * handler slot, e.g. "onclick". This is used for initial HTML attributes
 * after they are compiled by the document script runner.
 */
bool dom_set_event_handler_function(void* dom_elem, const char* attr_name, Item fn);

/**
 * Set a CSS inline style property on a DOM element.
 * Converts camelCase JS property names to CSS hyphenated form.
 * E.g., "fontFamily" → "font-family", "display" → "display"
 * @param elem       Wrapped DOM element Item
 * @param prop_name  String Item with JS-style property name
 * @param value      String Item with CSS value
 * @return The value that was set, or ITEM_NULL on failure
 */
Item dom_set_style_property(Item elem, Item prop_name, Item value);
Item dom_get_style_property(Item elem, Item prop_name);

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
bool js_is_inline_style_item(Item item);

// =============================================================================
// classList API (v12)
// =============================================================================

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
Item dom_contains(Item elem, Item other);

/**
 * Compare two wrapped DOM nodes using the DOM structural-equality algorithm.
 * @return Boolean Item
 */
Item dom_is_equal_node(Item node, Item other);
Item dom_is_same_node(Item node, Item other);

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

// =============================================================================
// Host-facing entry points (F23)
//
// The Radiant engine, the Lambda runtime and the JS runtime all drive these.
// They are declared here instead of being re-declared `extern "C"` at each call
// site, because a prototype copied per caller is how a signature drifts from
// its definition without the linker noticing. Jube modules reach the same core
// through JubeHostDomAPI (ES34), never through this header.
// =============================================================================

/** Session/runtime teardown. */
void dom_shutdown(void);
void dom_batch_reset(void);
void dom_collections_release_context(void);
void dom_foreign_documents_release_context(void);

/** Element-state queries used by the native event and form paths. */
bool dom_is_disabled(void* dom_elem);
bool dom_is_connected(void* dom_elem);

/** Activation bridges invoked after the UA tier claims an event. */
Item dom_focus_method_bridge(void* dom_elem, bool focus);
Item dom_scroll_into_view_bridge(void* dom_elem);
void dom_select_set_selected_index_bridge(void* dom_elem, Item value);

/** dataset expando write, routed from the JS property-set path. */
bool dom_dataset_set_object_property(Item dataset, Item key, Item value);

// -----------------------------------------------------------------------------
// Scheduling seam (F25/ES33)
//
// The DOM core defers work — observer delivery, selectionchange coalescing, the
// iframe load drain — but it does not own the loop that runs it, and naming the
// JS event loop directly would make the core JS-specific for what is really a
// "run this after the current turn settles" request. These two entries are
// implemented by the adapter in lambda/js/, which is where knowledge of the
// loop belongs; the callback Item itself is ordinary runtime value construction
// and stays with the caller.
// -----------------------------------------------------------------------------

/** Run `callback` once the current script's writes settle (microtask turn). */
void dom_schedule_microtask(Item callback);

/** Run `callback` on the next task turn (zero-delay timer). */
void dom_schedule_task(Item callback);

#ifdef __cplusplus
struct DomDocument;
struct JsRuntimeState;

/** Per-runtime-state teardown for the DOM-owned caches. */
void dom_collections_destroy_context(JsRuntimeState* state);
void dom_foreign_documents_destroy_context(JsRuntimeState* state);

/** contenteditable HTML insertion, driven by the editing waist. */
bool dom_exec_insert_html(DomDocument* doc, const char* html);

/** Mutation-sequence reads for incremental reconciliation. */
uint64_t dom_mutation_epoch(DomDocument* doc);
bool dom_mutation_since_affects_subtree(
        DomDocument* doc, uint32_t sequence_before, void* root);
#endif

#ifdef __cplusplus
}
#endif
