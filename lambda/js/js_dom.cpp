/**
 * JavaScript DOM API Bridge Implementation
 *
 * Bridges Lambda's Element data model and Radiant's DomElement/DomDocument
 * to provide standard DOM manipulation APIs callable from JIT-compiled JavaScript.
 *
 * Wrapping: Radiant DOM nodes and document proxy objects are branded native
 * VMaps owned by the module bridge.
 */

#include "js_dom.h"
#include "js_dom_events.h"
#include "js_dom_selection.h"
#include "js_history.h"
#include "js_xhr.h"
#include "js_cssom.h"
#include "js_runtime.h"
#include "js_props.h"
#include "js_property_attrs.h"
#include "js_runtime_state.hpp"
#include "js_function.hpp"
#include "js_event_loop.h"
#include "js_dom_platform.h"
#include "js_dom_observers.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../jube/jube_registry.h"
#include "../io/mark_builder.hpp"
#include "../io/mark_editor.hpp"
#include "../core/mark_reader.hpp"
#include "../module/radiant/radiant_input_value.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/mem_factory.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"

extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_weak(uint64_t* slot);
extern Item js_make_number(double d);
extern "C" Item js_dom_form_submit_bridge(Item form_item);
extern "C" Item js_dom_form_request_submit_bridge(Item form_item, Item submitter);
#include "../../lib/arena.h"
#include "../../lib/str.h"
#include "../../lib/url.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_lifecycle.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_engine.hpp"
#include "../input/css/css_style_node.hpp"
#include "../input/css/css_formatter.hpp"
#include "../input/css/selector_matcher.hpp"
#include "../io/input-allocation-context.h"
#include "../../radiant/view.hpp"
#include "../../radiant/event.hpp"
#include "../../radiant/layout.hpp"
#include "../../radiant/render.hpp"
#include "../input/html5/html5_parser.h"
#include "../../lib/hashmap.h"

extern "C" Item vmap_new(void);
extern "C" Item vmap_backing_get(VMap* vm, Item key);
extern "C" bool vmap_backing_set(VMap* vm, Item key, Item value);
extern void free_document(DomDocument* doc);
extern __thread EvalContext* context;
void parse_xml(Input* input, const char* xml_string);

#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../../lib/mem.h"

// JS undefined helpers (matching js_runtime.cpp encoding)
static inline bool is_js_undefined(Item val) {
    return get_type_id(val) == LMD_TYPE_UNDEFINED;
}

static inline Item js_string_key(const char* s) {
    return js_name_item(s);
}

static void js_dom_refresh_live_child_collections_for_mutation(DomNode* target,
                                                               DomNode* parent);
static void js_dom_refresh_live_form_collections_for_mutation(DomNode* target,
                                                              DomNode* parent,
                                                              DomDocument* doc);
static void js_dom_refresh_select_option_collections_for_mutation(DomNode* target,
                                                                  DomNode* parent,
                                                                  DomDocument* doc);
static void js_dom_refresh_live_lookup_collections_for_mutation(DomNode* target,
                                                                DomNode* parent,
                                                                DomDocument* doc);
// Pre-order walk over `node` and its following siblings, descending into each
// element's children. `visit` returns false to skip that element's subtree.
// Four call sites used to spell this out with a recursive std::function
// lambda; a plain function pointer keeps the traversal out of <functional>
// (CLAUDE.md rule 3).
typedef bool (*DomElementVisit)(DomElement* elem, void* ctx);

static void dom_walk_elements(DomNode* node, DomElementVisit visit, void* ctx) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = (DomElement*)node;
            if (visit(elem, ctx)) dom_walk_elements(elem->first_child, visit, ctx);
        }
        node = node->next_sibling;
    }
}

static void _collect_document_forms_rec(DomNode* node, Item forms);
static void _collect_form_controls_rec(DomNode* node, Item arr);
static void _collect_lookup_rec(DomElement* root, const char* query, Item collection,
                                int kind);

static const char* js_dom_to_attr_cstr(Item value) {
    Item str_value = js_to_string(value);
    const char* s = fn_to_cstr(str_value);
    return s ? s : "";
}

static const char* js_dom_to_dom_string_cstr(Item value) {
    Item string_value = js_to_string(value);
    return fn_to_cstr(string_value);
}
JS_FORWARD_RETURN(const char*, js_dom_to_attribute_cstr, (Item value), js_dom_to_attr_cstr, (value))

// Forward declarations
extern "C" Item js_data_transfer_new_with_strings(const char* text_plain,
                                                  const char* text_html);
extern "C" Item js_dom_add_event_listener_bridge(Item target_item, Item type,
                                                 Item callback, Item opts);
extern "C" Item js_dom_remove_event_listener_bridge(Item target_item, Item type,
                                                    Item callback, Item opts);
extern "C" Item js_dom_dispatch_event_bridge(Item target_item, Item event_item);
extern "C" void js_dom_after_srcdoc_set(void* dom_elem);
extern "C" Item radiant_dom_element_operation(Item elem_item,
                                                JubeDomElementOperation operation,
                                                Item* args, int argc);
extern "C" Item js_prototype_lookup_ex(Item object, Item property, bool* out_found);
static void js_camel_to_css_prop(const char* js_prop, char* css_buf, size_t buf_size);

static Item js_dom_element_prototype_operation_body(Item callee, Item this_value,
        Item* args, int argc, uint64_t* result_home) {
    (void)result_home;
    JsFunction* fn = get_type_id(callee) == LMD_TYPE_FUNC
        ? (JsFunction*)callee.function : NULL;
    JubeDomElementOperation operation = fn
        ? (JubeDomElementOperation)fn->native_target.bits
        : (JubeDomElementOperation)0;
    return radiant_dom_element_operation(this_value, operation, args, argc);
}

static CssDeclaration* js_match_custom_property(DomElement* elem, const char* prop_name);
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);
void js_dom_register_named_elements(DomElement* root);
static bool js_dom_node_is_connected(DomNode* node);
static DomElement* _nearest_select_for_node(DomNode* node);
static void _select_refresh_cached_selected_options_for_node(DomNode* node);
static void _select_ask_for_reset(DomElement* sel);
static bool _get_selectedness(DomElement* opt);
static Item js_dom_svg_get_transform_list(DomElement* elem);
static Item js_dom_svg_get_animated_class_name(DomElement* elem);
static Item js_dom_svg_create_transform(void);
static Item js_dom_svg_create_transform_from_matrix(Item matrix);
static RdtMatrix js_dom_svg_transform_from_element(DomElement* elem);
DomElement* js_dom_find_element_by_id(DomElement* root, const char* id);

static bool js_dom_replace_inner_html(DomElement* elem, const char* html_str,
                                      bool notify_mutation);

static String* js_dom_fragment_text(Item item) {
    String* s = it2s(item);
    // The HTML fragment parser can return zero-length string tokens around
    // element boundaries; browsers do not expose those as empty Text nodes.
    return (s && s->len > 0) ? s : nullptr;
}

// Reserved-attribute filter: attribute names starting with "__lambda_" are used
// internally (e.g. createElementNS records the namespace URI as
// "__lambda_ns_uri") and must not leak through JS-facing attribute
// enumeration, getAttribute lookup, or serialization.
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_is_internal_attr, (const char* name),
    name && strncmp(name, "__lambda_", 9) == 0)
// ============================================================================
// Unique type markers for DOM-adjacent Maps
// ============================================================================

// Sentinels for native style host VMaps.
static const char js_computed_style_vmap_marker = 0;
static const char js_inline_style_vmap_marker = 0;
static const char js_document_proxy_vmap_marker = 0;
static const char js_foreign_doc_vmap_marker = 0;
static const char js_web_animation_vmap_marker = 0;

extern "C" const void* radiant_dom_inline_style_host_type(void);
extern "C" const void* radiant_dom_computed_style_host_type(void);
extern "C" const void* radiant_dom_document_host_type(void);
extern "C" const void* radiant_dom_foreign_document_host_type(void);

// Legacy style map markers are accepted only by type predicates during migration.
static TypeMap js_computed_style_marker = {};
static TypeMap js_inline_style_marker = {};

struct JsWebAnimationHost {
    CssWebAnimationState* state;
};

// Cached JS wrappers are owned by the active EvalContext, not this translation
// unit. The root range is registered while a context is bound, before any
// allocation can publish one of these values.
#define js_dom_implementation_item (js_runtime_state.dom.implementation)
#define js_document_proxy_item (js_runtime_state.dom.document_proxy)
#define js_document_default_view (js_runtime_state.dom.default_view)
#define js_document_title_value (js_runtime_state.dom.title)
#define js_document_fonts_value (js_runtime_state.dom.fonts)
JS_FORWARD_STATIC_EXPRESSION(bool, dom_ensure_roots, (void), (js_active_runtime_state && js_root_range_ensure_registered(&js_runtime_state.dom.roots)))

#define js_document_design_mode (js_runtime_state.dom.design_mode)
#define js_document_active_element (js_runtime_state.dom.active_element)
static Item js_dom_document_element_from_point(DomDocument* doc,
                                               Item x_arg,
                                               Item y_arg);
static void js_dom_absolute_node_position(DomNode* node,
                                          float* out_x,
                                          float* out_y);
static void js_dom_viewport_node_position(DomNode* node,
                                          float* out_x,
                                          float* out_y);
static DomElement* js_dom_offset_parent_element(DomElement* elem);
static int64_t js_dom_offset_coordinate(DomElement* elem, bool x_axis);
static Item js_dom_svg_create_matrix(void);
static Item js_dom_svg_create_point(void);
extern "C" Item js_dom_get_bounding_client_rect_bridge(void* dom_elem);
static bool js_dom_ensure_geometry_snapshot(DomDocument* doc);

// ============================================================================
// Thread-local DOM document context
// ============================================================================

#define _js_current_document (js_runtime_state.dom.current_document)
#define _js_current_ui_context (js_runtime_state.dom.current_ui_context)
// True when a long-lived host (the Radiant `view` window / event_sim loop) owns
// the reflow cycle and pumps the JS event loop across the document's lifetime.
// In that mode geometry queries must NOT rebuild the view tree from under the
// live renderer, and load-time setTimeout(0) callbacks are deferred to the
// host's post-commit pump. The transient `lambda.exe js` document session leaves
// this false and keeps the self-contained rebuild-on-demand behaviour.
#define _js_host_driven_loop (js_runtime_state.dom.host_driven_loop)

extern "C" void js_dom_set_host_driven_loop(bool enabled) {
    if (!js_active_runtime_state) return;
    _js_host_driven_loop = enabled;
}
JS_FORWARD_EXPRESSION(bool, js_dom_is_host_driven_loop, (void), (_js_host_driven_loop))
// The "main" document — the one bound by js_dom_set_document at page load.
// Foreign documents created via document.implementation.create*Document are
// distinct: they have a null defaultView and getSelection() returns null per
// HTML spec, even when the foreign-doc dispatcher temporarily swaps them in
// as _js_current_document.
#define _js_main_document (js_runtime_state.dom.main_document)

static Item js_font_face_set_ready_then(Item callback) {
    if (js_is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return get_type_id(js_document_fonts_value) == LMD_TYPE_MAP
        ? js_document_fonts_value : make_js_undefined();
}

static Item js_create_document_fonts_object(void) {
    Item fonts = js_new_object();
    Item ready = js_new_object();
    Item then_fn = js_new_native_function(js_font_face_set_ready_then);
    js_set_key_cstr(ready, "then", then_fn);
    js_set_key_cstr(fonts, "ready", ready);
    return fonts;
}

// Forward decls (defined further down in the foreign-doc / iframe section).
extern "C" bool js_doc_has_browsing_context(void* doc);
extern "C" void js_doc_mark_has_browsing_context(void* doc);
static Url* js_dom_make_fallback_url(const char* raw_url);

static inline DocState* js_dom_current_state();

static void js_dom_mark_dirty_subtree(DomNode* root) {
    if (!root) return;

    root->layout_dirty = true;
    if (root->is_element()) {
        DomElement* elem = root->as_element();
        // generated table roles survive an ancestor style mutation; no authored
        // declaration exists from which a later reflow could reconstruct them.
        if (!elem->is_table_fixup()) elem->set_styles_resolved(false);
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            js_dom_mark_dirty_subtree(child);
        }
    }
}

static void js_dom_mark_dirty_ancestors(DomNode* node) {
    for (DomNode* cur = node; cur; cur = cur->parent) {
        cur->layout_dirty = true;
        if (cur->is_element()) {
            DomElement* elem = cur->as_element();
            if (!elem->is_table_fixup()) elem->set_styles_resolved(false);
        }
    }
}

static inline uint32_t js_dom_mutation_bit(DomJsMutationKind kind) {
    uint32_t slot = (uint32_t)kind;
    if (slot >= 31) slot = 0;
    return 1u << slot;
}

static inline DomJsMutationKind js_dom_style_mutation_kind(CssPropertyCode prop_id) {
    switch (prop_id) {
        case CSS_PROPERTY_BACKGROUND_COLOR:
        case CSS_PROPERTY_COLOR:
        case CSS_PROPERTY_OPACITY:
        case CSS_PROPERTY_VISIBILITY:
            return DOM_JS_MUTATION_STYLE_REPAINT;
        default:
            return DOM_JS_MUTATION_STYLE;
    }
}

static DomDocument* js_dom_node_owner_document(DomNode* node) {
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur->is_element()) {
            DomElement* elem = cur->as_element();
            if (elem && elem->doc) return elem->doc;
        }
    }
    return nullptr;
}

static DomDocument* js_dom_mutation_document(DomNode* target, DomNode* parent) {
    DomDocument* doc = js_dom_node_owner_document(target);
    if (!doc) doc = js_dom_node_owner_document(parent);
    return doc ? doc : _js_current_document;
}

static DomNode* js_dom_mutation_source_parent(DomNode* parent) {
    // Table fixup boxes are layout-only and have no lifecycle identity; mutation
    // records must retain the nearest source-DOM ancestor instead of pinning them.
    while (parent && parent->is_element() && parent->as_element()->is_table_fixup()) {
        parent = parent->parent;
    }
    return parent;
}

static DomNode* js_dom_table_fixup_significant_sibling(DomNode* child,
                                                       bool forward) {
    for (DomNode* sibling = child ? (forward ? child->next_sibling
                                             : child->prev_sibling) : nullptr;
         sibling; sibling = forward ? sibling->next_sibling : sibling->prev_sibling) {
        if (sibling->is_text()) {
            if (!layout_dom_text_has_non_whitespace(sibling->as_text())) continue;
            return sibling;
        }
        if (!sibling->is_element()) return sibling;
        DisplayValue display = resolve_display_value((void*)sibling);
        if (layout_display_is_none(display)) continue;
        return sibling;
    }
    return nullptr;
}

static bool js_dom_table_fixup_cells_only_descendants(DomNode* node,
                                                       bool* has_cell) {
    if (!node || !node->is_element()) return false;
    for (DomNode* child = node->as_element()->first_child; child;
         child = child->next_sibling) {
        if (child->is_text()) {
            if (!layout_dom_text_has_non_whitespace(child->as_text())) continue;
            return false;
        }
        if (!child->is_element()) return false;
        CssEnum display = resolve_display_value((void*)child).inner;
        if (display == CSS_VALUE_TABLE_CELL) {
            if (has_cell) *has_cell = true;
            continue;
        }
        if (display != CSS_VALUE_TABLE_ROW &&
            display != CSS_VALUE_TABLE_ROW_GROUP &&
            display != CSS_VALUE_TABLE_HEADER_GROUP &&
            display != CSS_VALUE_TABLE_FOOTER_GROUP) {
            return false;
        }
        if (!child->as_element()->is_table_fixup() ||
            !js_dom_table_fixup_cells_only_descendants(child, has_cell)) {
            return false;
        }
    }
    return true;
}

static bool js_dom_table_fixup_is_cells_only_run(DomNode* node) {
    if (!node || !node->is_element() || !node->as_element()->is_table_fixup()) {
        return false;
    }
    bool has_cell = false;
    return js_dom_table_fixup_cells_only_descendants(node, &has_cell) && has_cell;
}

static bool js_dom_removal_merges_table_fixup_runs(DomElement* parent,
                                                  DomNode* child) {
    if (!parent || !child ||
        resolve_display_value((void*)parent).outer != CSS_VALUE_INLINE) {
        return false;
    }
    DomNode* previous = js_dom_table_fixup_significant_sibling(child, false);
    DomNode* next = js_dom_table_fixup_significant_sibling(child, true);
    bool previous_cells = js_dom_table_fixup_is_cells_only_run(previous);
    bool next_cells = js_dom_table_fixup_is_cells_only_run(next);
    return previous_cells && next_cells;
}

static DomElement* js_dom_prepare_children_for_mutation(DomElement* parent,
                                                        DomNode* changed_child = nullptr) {
    DomNode* source_parent = js_dom_mutation_source_parent(
        static_cast<DomNode*>(parent));
    if (!source_parent || !source_parent->is_element()) return nullptr;
    DomElement* source_element = source_parent->as_element();
    bool changes_table_structure = !changed_child || !changed_child->is_element();
    bool merges_table_fixup_runs = false;
    if (changed_child && changed_child->is_element()) {
        DisplayValue display = resolve_display_value((void*)changed_child);
        changes_table_structure = is_table_internal_display(display.inner);
        merges_table_fixup_runs = js_dom_removal_merges_table_fixup_runs(
            source_element, changed_child);
    }
    if (changes_table_structure || merges_table_fixup_runs) {
        // table-internal removal changes the fixup input; rebuild the generated
        // boxes so the next layout sees the authored table structure. Removing
        // a separator between two cell-only runs has the same effect.
        layout_unwrap_anonymous_table_fixups_for_dom_mutation(source_element);
    }
    return source_element;
}

static DocState* js_dom_state_for_nodes(DomNode* target, DomNode* parent) {
    DomDocument* doc = js_dom_mutation_document(target, parent);
    return doc ? doc->state : nullptr;
}

static inline void js_dom_record_mutation_detail(DomJsMutationKind kind,
                                                 DomNode* target,
                                                 DomNode* parent,
                                                 uint32_t sequence) {
    parent = js_dom_mutation_source_parent(parent);
    DomDocument* doc = js_dom_mutation_document(target, parent);
    if (!doc) return;

    if (sequence == 0) {
        sequence = doc->js.mutation_sequence + 1;
    }
    doc->js.mutation_kind_mask |= js_dom_mutation_bit(kind);

    if (doc->js.mutation_record_count < DOM_JS_MUTATION_RECORD_CAP) {
        DomNodeRef target_ref = dom_node_ref(target);
        DomNodeRef parent_ref = dom_node_ref(parent);
        if (target && !dom_node_pin(doc, target_ref, DOM_NODE_PIN_RECONCILE)) return;
        if (parent && !dom_node_pin(doc, parent_ref, DOM_NODE_PIN_RECONCILE)) {
            if (target) dom_node_unpin(doc, target_ref, DOM_NODE_PIN_RECONCILE);
            return;
        }
        DomJsMutationRecord* record =
            &doc->js.mutation_records[doc->js.mutation_record_count++];
        record->sequence = sequence;
        record->kind = kind;
        record->target = target;
        record->parent = parent;
        record->target_id = target_ref.expected_id;
        record->parent_id = parent_ref.expected_id;
    } else {
        doc->js.mutation_record_overflow++;
    }

    if (target) {
        js_dom_mark_dirty_subtree(target);
        js_dom_mark_dirty_ancestors(target);
    }
    if (parent) {
        js_dom_mark_dirty_ancestors(parent);
    }
}

// Helper: increment DOM mutation counter on current document and record the
// mutation shape for future incremental cascade/layout decisions.
static inline void js_dom_mutation_notify(DomJsMutationKind kind = DOM_JS_MUTATION_UNKNOWN,
                                          DomNode* target = nullptr,
                                          DomNode* parent = nullptr,
                                          const char* attribute_name = nullptr,
                                          const char* old_value = nullptr) {
    parent = js_dom_mutation_source_parent(parent);
    DomDocument* doc = js_dom_mutation_document(target, parent);
    if (!doc) return;

    doc->js.mutation_count++;
    doc->js.mutation_sequence++;
    doc->mutation_epoch++;

    bool has_pending_structural_record = false;
    if (doc->js.mutation_record_count > 0) {
        DomJsMutationRecord* last = &doc->js.mutation_records[doc->js.mutation_record_count - 1];
        bool legacy_flush = kind == DOM_JS_MUTATION_UNKNOWN && !target && !parent;
        bool same_detail = last->kind == kind && last->target == target &&
                           last->parent == parent;
        // dom_pre_remove/dom_post_insert publish the precise structural shape
        // before the common notifier; do not record that same mutation twice.
        has_pending_structural_record =
            last->sequence == doc->js.mutation_sequence &&
            (legacy_flush || same_detail);
    }

    if (!has_pending_structural_record) {
        js_dom_record_mutation_detail(kind, target, parent, doc->js.mutation_sequence);
    }
    js_dom_refresh_live_child_collections_for_mutation(target, parent);
    js_dom_refresh_live_form_collections_for_mutation(target, parent, doc);
    js_dom_refresh_select_option_collections_for_mutation(target, parent, doc);
    js_dom_refresh_live_lookup_collections_for_mutation(target, parent, doc);
    js_dom_observers_mutation_notify(kind, target, parent, attribute_name, old_value);

    DocState* st = doc->state;
    if (st) {
        view_state_prune_orphans(st);
        // Layout-affecting JS DOM mutations must request a reflow so JS-built
        // structure (e.g. a script that constructs its UI at load, like the
        // Stage-4B editor's toolbar) gets laid out and becomes hit-testable
        // without waiting for a later edit to trigger relayout. A transient
        // CLI document also needs a style-resolution turn for paint-only
        // mutations: CSS transitions are created by that turn, while the
        // long-lived renderer already owns its normal style/frame cadence.
        if (kind != DOM_JS_MUTATION_STYLE_REPAINT ||
            (_js_current_ui_context && !_js_host_driven_loop)) {
            doc_state_request_reflow(st);
        }
    }
}

extern "C" void js_dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent) {
    // Module-owned DOM setters must still publish mutations through the JS DOM ledger.
    js_dom_mutation_notify(kind, (DomNode*)target, (DomNode*)parent);
}
JS_FORWARD_VOID( js_dom_notify_mutation_detail, (DomJsMutationKind kind, void* target,                                                 void* parent, const char* attribute_name,                                                 const char* old_value), js_dom_mutation_notify, (kind, (DomNode*)target, (DomNode*)parent, attribute_name, old_value))

JS_FORWARD_EXPRESSION(uint64_t, js_dom_mutation_epoch, (DomDocument* doc),
    doc ? doc->mutation_epoch : 0)

static bool js_dom_mutation_node_overlaps_root(DomNode* node, DomNode* root) {
    if (!node || !root) return false;
    for (DomNode* current = node; current; current = current->parent) {
        if (current == root) return true;
    }
    for (DomNode* current = root; current; current = current->parent) {
        if (current == node) return true;
    }
    return false;
}

extern "C" bool js_dom_mutation_since_affects_subtree(
        DomDocument* doc, uint32_t sequence_before, void* root) {
    if (!doc || !root) return false;
    DomNode* root_node = (DomNode*)root;
    for (int index = 0; index < doc->js.mutation_record_count; index++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[index];
        if (record->sequence <= sequence_before) continue;
        if (js_dom_mutation_node_overlaps_root(record->target, root_node) ||
            js_dom_mutation_node_overlaps_root(record->parent, root_node)) {
            return true;
        }
    }
    return false;
}

extern "C" bool js_dom_has_committed_geometry_snapshot(void* dom_doc) {
    DomDocument* doc = (DomDocument*)dom_doc;
    // this predicate is intentionally side-effect free; geometry entry points
    // call js_dom_ensure_geometry_snapshot before inspecting the cached boxes.
    return doc && doc->view_tree && doc->view_tree->root;
}

static thread_local bool js_dom_geometry_flush_in_progress = false;

static bool js_dom_ensure_geometry_snapshot(DomDocument* doc) {
    UiContext* uicon = _js_current_ui_context;
    if (!doc || !uicon || !uicon->headless || _js_host_driven_loop) {
        return js_dom_has_committed_geometry_snapshot(doc);
    }
    if (js_dom_geometry_flush_in_progress) {
        return js_dom_has_committed_geometry_snapshot(doc);
    }

    DomDocument* saved_document = uicon->document;
    uicon->document = doc;
    js_dom_geometry_flush_in_progress = true;

    // CSSOM View geometry reads synchronously flush style and layout. The
    // initial load has no view tree yet, while later reads may have pending
    // DOM mutations from the same script turn.
    if (doc->view_tree && doc->view_tree->root) {
        if (doc->js.mutation_count > 0) {
            radiant_reconcile_js_dom_mutations(uicon, doc);
        }
    } else if (doc->root && radiant_document_ensure_state(
                   doc, "js_dom_geometry_flush")) {
        layout_html_doc(uicon, doc, false);
    }

    js_dom_geometry_flush_in_progress = false;
    uicon->document = saved_document;
    return js_dom_has_committed_geometry_snapshot(doc);
}

extern "C" bool js_dom_tick_headless_animation_frame(void) {
    js_dom_commit_headless_layout();
    DomDocument* doc = _js_current_ui_context && _js_current_ui_context->document
        ? _js_current_ui_context->document : _js_current_document;
    DocState* state = doc && doc->state ? (DocState*)doc->state : nullptr;
    AnimationScheduler* scheduler = state ? state->animation_scheduler : nullptr;
    if (!scheduler || !scheduler->has_active_animations) return false;
    // Batch documents have no native frame clock; advance the same scheduler
    // deterministically so transition events cannot remain queued forever.
    double now = scheduler->current_time + (1.0 / 60.0);
    return animation_scheduler_tick(scheduler, now, &state->dirty_tracker);
}

extern "C" bool js_dom_commit_headless_layout_checkpoint(void) {
    UiContext* uicon = _js_current_ui_context;
    DomDocument* doc = uicon && uicon->document
        ? uicon->document : _js_current_document;
    if (!uicon || !uicon->headless || js_dom_is_host_driven_loop() ||
        !doc || !doc->view_tree || !doc->view_tree->root ||
        doc->js.mutation_count == 0) {
        return false;
    }
    // A one-shot DOM session has no native render loop, so task boundaries must
    // commit pending mutations; geometry getters remain snapshots.
    radiant_reconcile_js_dom_mutations(uicon, doc);
    return true;
}

// keep the older entry point as a boundary wrapper: geometry readers must use
// the same mutation reconciliation checkpoint as the event loop.
JS_FORWARD_RETURN(bool, js_dom_commit_headless_layout, (void),
    js_dom_commit_headless_layout_checkpoint, ())

// ----------------------------------------------------------------------------
// Phase 3: live-range mutation envelopes — thin wrappers that bail when no
// per-document DocState (and thus no live ranges) is attached. All DOM
// mutation paths in this file route through these to keep boundary points
// in sync per WHATWG DOM §5.3.
// ----------------------------------------------------------------------------
JS_FORWARD_STATIC_EXPRESSION(DocState*, js_dom_current_state, (void),
    _js_current_document ? _js_current_document->state : nullptr)

static DocState* js_dom_testdriver_state() {
    if (!_js_current_document) return nullptr;
    if (!_js_current_document->state) {
        radiant_document_ensure_state(_js_current_document, "js_dom_testdriver_key");
    }
    return _js_current_document->state;
}

extern "C" Item js_dom_set_editing_behavior(Item behavior_item) {
    DocState* state = js_dom_testdriver_state();
    if (!state) return make_js_undefined();
    Item behavior_str = js_to_string(behavior_item);
    const char* behavior = fn_to_cstr(behavior_str);
    state_store_set_editing_behavior(state, behavior);
    return make_js_undefined();
}

static uint32_t js_dom_to_u32(Item value) {
    Item num = js_to_number(value);
    TypeId t = get_type_id(num);
    if (t == LMD_TYPE_INT) return (uint32_t)it2i(num);
    if (t == LMD_TYPE_INT64) return (uint32_t)it2l(num);
    if (t == LMD_TYPE_FLOAT) return (uint32_t)it2d(num);
    if (t == LMD_TYPE_BOOL) return it2b(num) ? 1u : 0u;
    return 0;
}

static Item js_dom_testdriver_key(Item key_item,
                                  Item shift_item,
                                  Item ctrl_item,
                                  Item alt_item,
                                  Item meta_item) {
    if (!_js_current_document) return (Item){.item = ITEM_FALSE};
    if (!js_dom_testdriver_state() || !_js_current_ui_context) {
        return (Item){.item = ITEM_FALSE};
    }

    uint32_t wpt_key = js_dom_to_u32(key_item);
    int mods = 0;
    if (js_is_truthy(shift_item)) mods |= RDT_MOD_SHIFT;
    if (js_is_truthy(ctrl_item)) mods |= RDT_MOD_CTRL;
    if (js_is_truthy(alt_item)) mods |= RDT_MOD_ALT;
    if (js_is_truthy(meta_item)) mods |= RDT_MOD_SUPER;

    if (wpt_key >= 'a' && wpt_key <= 'z') {
        wpt_key -= ('a' - 'A');
    }
    RdtEvent event;
    memset(&event, 0, sizeof(event));
    event.key.type = RDT_EVENT_KEY_DOWN;
    event.key.key = wpt_key == 0xE003 ? RDT_KEY_BACKSPACE :
        (wpt_key == 0xE017 ? RDT_KEY_DELETE : (int)wpt_key); // INT_CAST_OK: platform key enum stores code points.
    event.key.mods = mods;
    // Testdriver input must traverse the public platform event path so the
    // same route snapshot, notifications, and DOM fallback serve tests/users.
    handle_event(_js_current_ui_context, _js_current_document, &event);
    return (Item){.item = ITEM_TRUE};
}

static bool js_dom_node_contains(DomNode* ancestor, DomNode* node) {
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur == ancestor) return true;
    }
    return false;
}

static bool js_dom_node_is_connected(DomNode* node) {
    if (!node) return false;
    DomDocument* doc = nullptr;
    if (node->is_element() && node->as_element())
        doc = node->as_element()->doc;
    if (!doc) {
        for (DomNode* cur = node->parent; cur; cur = cur->parent) {
            if (cur->is_element() && cur->as_element() && cur->as_element()->doc) {
                doc = cur->as_element()->doc;
                break;
            }
        }
    }
    if (!doc) doc = _js_current_document;
    if (!doc || !doc->root) return false;

    DomNode* root = static_cast<DomNode*>(doc->root);
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur == root) return true;
    }
    return false;
}

static inline void dom_pre_remove(DomNode* child, bool record_mutation = true) {
    DocState* st = js_dom_state_for_nodes(child, child ? child->parent : nullptr);
    if (st && child) {
        dom_mutation_pre_remove(st, child);

        View* focused = focus_get(st);
        if (focused && js_dom_node_contains(child, (DomNode*)focused)) {
            if (focused->is_element()) {
                DomElement* focused_elem = ((DomNode*)focused)->as_element();
                const char* tag = focused_elem ? focused_elem->tag_name : nullptr;
                if (tag &&
                    (strcasecmp(tag, "textarea") == 0 ||
                     strcasecmp(tag, "input") == 0) &&
                    child->parent) {
                    uint32_t index = dom_node_child_index(child);
                    if (index != UINT32_MAX) {
                        DomBoundary boundary = { child->parent, index };
                        const char* exc = nullptr;
                        if (!state_store_set_selection(
                                st, &boundary, &boundary, &exc)) {
                            log_debug("dom_pre_remove_text_control_selection_handoff_failed: %s",
                                      exc ? exc : "unknown");
                        }
                    }
                }
            }
            focus_clear_preserve_selection(st);
        } else {
            View* caret_view = caret_get_view(st);
            if (caret_view && js_dom_node_contains(child, (DomNode*)caret_view)) {
                state_store_caret_clear(st);
            }

            View* anchor_view = nullptr;
            View* focus_view = nullptr;
            if (selection_get_extent_views(st, &anchor_view, &focus_view) &&
                ((anchor_view && js_dom_node_contains(child, (DomNode*)anchor_view)) ||
                 (focus_view && js_dom_node_contains(child, (DomNode*)focus_view)))) {
                state_store_selection_clear(st);
            }
        }
    }
    if (child && js_document_active_element &&
        js_dom_node_contains(child, (DomNode*)js_document_active_element)) {
        js_document_active_element = nullptr;
    }
    view_pool_release_detached_subtree(child);
    if (record_mutation) {
        js_dom_record_mutation_detail(DOM_JS_MUTATION_CHILD_REMOVE, child,
                                      child ? child->parent : nullptr, 0);
    }
}
static inline void dom_post_insert(DomNode* parent, DomNode* node,
                                   bool record_mutation = true) {
    DocState* st = js_dom_state_for_nodes(node, parent);
    if (st && parent && node) dom_mutation_post_insert(st, parent, node);
    if (record_mutation) {
        js_dom_record_mutation_detail(DOM_JS_MUTATION_CHILD_INSERT, node, parent, 0);
    }
}

static bool js_dom_is_generated_pseudo_node(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    return elem->tag_name && elem->tag_name[0] == ':' && elem->tag_name[1] == ':';
}

static bool js_dom_is_anonymous_table_wrapper(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    return elem->tag_name && strncmp(elem->tag_name, "::anon-", 7) == 0;
}

static DomNode* js_dom_visible_child(DomElement* elem, bool first) {
    DomNode* child = elem ? (first ? elem->first_child : elem->last_child) : nullptr;
    while (child) {
        if (!js_dom_is_generated_pseudo_node(child)) return child;
        if (js_dom_is_anonymous_table_wrapper(child)) {
            // layout-only wrappers are transparent in both traversal directions.
            DomNode* nested = js_dom_visible_child(child->as_element(), first);
            if (nested) return nested;
        }
        child = first ? child->next_sibling : child->prev_sibling;
    }
    return nullptr;
}

static DomNode* js_dom_visible_sibling(DomNode* node, bool forward) {
    DomNode* sibling = node ? (forward ? node->next_sibling : node->prev_sibling) : nullptr;
    while (sibling) {
        if (!js_dom_is_generated_pseudo_node(sibling)) return sibling;
        if (js_dom_is_anonymous_table_wrapper(sibling)) {
            DomNode* child = js_dom_visible_child(sibling->as_element(), forward);
            if (child) return child;
        }
        sibling = forward ? sibling->next_sibling : sibling->prev_sibling;
    }
    DomNode* parent = node ? node->parent : nullptr;
    while (js_dom_is_anonymous_table_wrapper(parent)) {
        sibling = forward ? parent->next_sibling : parent->prev_sibling;
        while (sibling) {
            if (!js_dom_is_generated_pseudo_node(sibling)) return sibling;
            if (js_dom_is_anonymous_table_wrapper(sibling)) {
                DomNode* child = js_dom_visible_child(sibling->as_element(), forward);
                if (child) return child;
            }
            sibling = forward ? sibling->next_sibling : sibling->prev_sibling;
        }
        parent = parent->parent;
    }
    return nullptr;
}

JS_FORWARD_STATIC_RETURN(DomNode*, js_dom_next_script_visible_sibling,
    (DomNode* node), js_dom_visible_sibling, (node, true))
JS_FORWARD_STATIC_RETURN(DomNode*, js_dom_prev_script_visible_sibling,
    (DomNode* node), js_dom_visible_sibling, (node, false))
JS_FORWARD_STATIC_RETURN(DomNode*, js_dom_first_script_visible_child,
    (DomElement* elem), js_dom_visible_child, (elem, true))
JS_FORWARD_STATIC_RETURN(DomNode*, js_dom_last_script_visible_child,
    (DomElement* elem), js_dom_visible_child, (elem, false))

static uint32_t js_dom_utf16_length_from_utf8(const char* text, size_t len) {
    if (!text) return 0;
    uint32_t n = 0;
    const unsigned char* p = (const unsigned char*)text;
    for (size_t i = 0; i < len; i++) {
        unsigned char b = p[i];
        if ((b & 0xC0) == 0x80) continue;
        if (b < 0x80) n += 1;
        else if (b < 0xF0) n += 1;
        else n += 2;
    }
    return n;
}

static int64_t js_dom_to_integer_or_zero(Item value) {
    Item num = js_to_number(value);
    TypeId t = get_type_id(num);
    double d = 0.0;
    if (t == LMD_TYPE_INT) d = (double)it2i(num);
    else if (t == LMD_TYPE_INT64) d = (double)it2l(num);
    else if (t == LMD_TYPE_FLOAT) d = it2d(num);
    else if (t == LMD_TYPE_BOOL) d = it2b(num) ? 1.0 : 0.0;
    if (d != d || d == 0.0) return 0;
    if (d >= (double)INT64_MAX) return INT64_MAX;
    if (d <= (double)INT64_MIN) return INT64_MIN;
    return (int64_t)d;
}

static Item js_dom_throw_index_size_error(const char* message) {
    Item name = js_name_item("IndexSizeError");
    Item msg = js_name_item(
        message ? message : "The index is not in the allowed range.");
    return js_throw_value(js_new_error_with_name(name, msg));
}

static Item js_dom_replace_text_data(DomText* text_node, uint32_t offset,
                                     uint32_t count, const char* repl_chars) {
    if (!text_node) return js_status_ok();
    if (!repl_chars) repl_chars = "";

    uint32_t old_u16_len = dom_text_utf16_length(text_node);
    if (offset > old_u16_len) {
        return js_dom_throw_index_size_error("The offset is larger than the CharacterData length.");
    }
    uint32_t available = old_u16_len - offset;
    if (count > available) count = available;

    size_t repl_len = strlen(repl_chars);
    uint32_t repl_u16_len = js_dom_utf16_length_from_utf8(repl_chars, repl_len);
    const char* old_text = text_node->text ? text_node->text : "";
    DocState* state = js_dom_state_for_nodes(
        (DomNode*)text_node, text_node->parent);
    if (!dom_text_replace_data_contents(state, text_node, offset, count,
                                        repl_chars, repl_len, repl_u16_len)) {
        return js_status_ok();
    }
    js_dom_record_mutation_detail(DOM_JS_MUTATION_TEXT, (DomNode*)text_node,
                                  text_node->parent, 0);
    js_dom_mutation_notify(DOM_JS_MUTATION_TEXT, (DomNode*)text_node,
                           text_node->parent, nullptr, old_text);
    log_debug("js_dom_replace_text_data: offset=%u count=%u replacement_u16=%u",
              offset, count, repl_u16_len);
    return js_status_ok();
}

/**
 * Reset JS DOM state for batch mode. Clears cached document proxy and
 * document pointer so next file starts fresh.
 */
static void expando_reset(); // forward declaration
static void reset_dom_wrapper_cache(); // forward declaration
static void reset_foreign_document_cache(); // forward declaration
static void reset_live_dom_collections(); // forward declaration
static void reset_pending_iframe_loads();
// Phase 6E: text-control helpers are shared with Radiant event/render paths.
#include "../../radiant/event.hpp"
#define tc_is_text_control_elem(e)      tc_is_text_control(e)

extern "C" void* js_dom_current_active_text_control(void) {
    DocState* state = js_dom_current_state();
    if (state) {
        View* focused = focus_get(state);
        if (focused && focused->is_element()) {
            DomElement* elem = ((DomNode*)focused)->as_element();
            if (tc_is_text_control_elem(elem)) return elem;
        }
        DomElement* elem = tc_get_active_element(state);
        if (elem && tc_is_text_control_elem(elem)) return elem;
        elem = tc_get_last_focused_text_control(state);
        if (elem && tc_is_text_control_elem(elem)) return elem;
    }
    if (js_document_active_element &&
        (!_js_current_document || js_document_active_element->doc == _js_current_document) &&
        tc_is_text_control_elem(js_document_active_element)) {
        return js_document_active_element;
    }
    return nullptr;
}

extern "C" void js_dom_batch_reset() {
    // Transient document teardown releases its EvalContext before generic host
    // cleanup reaches here; no context-owned DOM cache may be touched then.
    if (!js_active_runtime_state) return;
    DocState* state = js_dom_current_state();
    js_dom_selection_reset();
    reset_live_dom_collections();
    reset_pending_iframe_loads();
    expando_reset();
    reset_dom_wrapper_cache();
    js_document_proxy_item = (Item){.item = ITEM_NULL};
    js_document_default_view = (Item){.item = ITEM_NULL};
    js_document_title_value = (Item){.item = ITEM_NULL};
    js_document_design_mode = false;
    js_document_active_element = nullptr;
    js_document_fonts_value = (Item){.item = ITEM_NULL};
    _js_current_document = nullptr;
    js_dom_events_reset();
    js_xhr_reset();
    js_storage_reset();
    js_match_media_reset();
    js_dom_observers_reset();
    reset_foreign_document_cache();
    tc_reset_focus_state(state);
}

extern "C" void js_dom_shutdown() {
    // See js_dom_batch_reset: a detached host has no valid DOM cache owner.
    if (!js_active_runtime_state) return;
    reset_live_dom_collections();
    reset_pending_iframe_loads();
    expando_reset();
    reset_dom_wrapper_cache();
    js_dom_events_reset();
    js_xhr_reset();
    js_storage_reset();
    js_match_media_reset();
    js_dom_observers_reset();
    reset_foreign_document_cache();
    _js_current_document = nullptr;
    _js_main_document = nullptr;
}

extern "C" bool js_dom_evaluate_media_query(const char* query) {
    if (!query || !_js_current_document || !_js_current_ui_context) return false;
    CssEngine* engine = (CssEngine*)_js_current_document->services.cached_css_engine;
    if (!engine) return false;
    // matchMedia and @media must share one evaluator and the same live
    // viewport; otherwise JS and cascade disagree after a surface resize.
    css_engine_set_viewport(engine,
        (double)_js_current_ui_context->viewport_width,
        (double)_js_current_ui_context->viewport_height);
    return css_evaluate_media_query(engine, query);
}

// ============================================================================
// DOM Expando Properties
// Allows arbitrary JS values to be stored on DOM elements, e.g.
//   element._myData = { ... }; let x = element._myData;
// DOM expandos live in the owning wrapper's traced VMap backing store.
// ============================================================================

extern "C" Item radiant_dom_lookup_cached_node(void* dom_node);
extern "C" Item radiant_dom_wrap_node(void* dom_node);

#define DOM_EXPANDO_BACKING_KEY "__jube_expando__"

typedef struct AttachedExpandoRoot {
    DomNode* node;
    DomDocument* doc;
    DomNodeRef ref;
    Item map;
} AttachedExpandoRoot;

typedef struct AttachedExpandoEntry {
    DomNode* key;
    AttachedExpandoRoot* root;
} AttachedExpandoEntry;

// Connected-node expando maps carry JS values independently of wrapper reach-
// ability. The root table belongs to the bound document realm, not the thread.
#define s_attached_expando_roots (js_runtime_state.dom_attached_expando_roots)

static uint64_t attached_expando_hash(const void* item,
        uint64_t seed0, uint64_t seed1) {
    const AttachedExpandoEntry* entry = (const AttachedExpandoEntry*)item;
    return hashmap_sip(&entry->key, sizeof(entry->key), seed0, seed1);
}

static int attached_expando_compare(const void* left, const void* right, void*) {
    const AttachedExpandoEntry* a = (const AttachedExpandoEntry*)left;
    const AttachedExpandoEntry* b = (const AttachedExpandoEntry*)right;
    return a->key == b->key ? 0 : ((uintptr_t)a->key < (uintptr_t)b->key ? -1 : 1);
}

static HashMap* attached_expando_table() {
    if (!js_active_runtime_state) return nullptr;
    if (!s_attached_expando_roots) {
        s_attached_expando_roots = hashmap_new(sizeof(AttachedExpandoEntry),
            64, 0, 0, attached_expando_hash, attached_expando_compare,
            nullptr, nullptr);
    }
    return s_attached_expando_roots;
}

static AttachedExpandoRoot* attached_expando_find(DomNode* node) {
    if (!js_active_runtime_state) return nullptr;
    if (!s_attached_expando_roots || !node) return nullptr;
    AttachedExpandoEntry probe = {.key = node, .root = nullptr};
    const AttachedExpandoEntry* found = (const AttachedExpandoEntry*)
        hashmap_get(s_attached_expando_roots, &probe);
    return found ? found->root : nullptr;
}

static bool expando_node_is_attached(DomNode* node) {
    if (!node) return false;
    DomDocument* doc = js_dom_node_owner_document(node);
    if (!doc || !doc->root) return false;
    // A descendant can retain a parent inside a detached subtree; only a
    // complete ancestry chain to the document root makes native ownership a
    // valid reason to keep its expando map globally rooted.
    for (DomNode* current = node; current; current = current->parent) {
        if (current == (DomNode*)doc->root) return true;
    }
    return false;
}

static void attached_expando_root_add(DomNode* node, Item map,
                                      bool attachment_confirmed = false) {
    if (!node || get_type_id(map) != LMD_TYPE_MAP ||
            (!attachment_confirmed && !expando_node_is_attached(node))) return;
    DomDocument* doc = js_dom_node_owner_document(node);
    if (!doc) return;
    AttachedExpandoRoot* existing = attached_expando_find(node);
    if (existing) {
        existing->doc = doc;
        existing->ref = dom_node_ref(node);
        existing->map = map;
        return;
    }
    HashMap* table = attached_expando_table();
    if (!table) return;
    AttachedExpandoRoot* root = (AttachedExpandoRoot*)mem_calloc(
        1, sizeof(AttachedExpandoRoot), MEM_CAT_DOM);
    if (!root) return;
    root->node = node;
    root->doc = doc;
    root->ref = dom_node_ref(node);
    root->map = map;
    AttachedExpandoEntry entry = {.key = node, .root = root};
    hashmap_set(table, &entry);
    if (!hashmap_get(table, &entry)) {
        mem_free(root);
        return;
    }
    // Persistent root lifetime begins only after the owning table accepts the
    // entry; otherwise the failure path resembles an unsafe transient root.
    heap_register_gc_root(&root->map.item);
}

static void attached_expando_root_remove(DomNode* node) {
    AttachedExpandoRoot* root = attached_expando_find(node);
    if (!root) return;
    AttachedExpandoEntry probe = {.key = node, .root = nullptr};
    heap_unregister_gc_root(&root->map.item);
    hashmap_delete(s_attached_expando_roots, &probe);
    mem_free(root);
}

static Item expando_wrapper(DomNode* node, bool create) {
    if (!node) return ItemNull;
    Item wrapper = radiant_dom_lookup_cached_node(node);
    if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap) return wrapper;
    if (!create) {
        if (!node->is_element() || !node->as_element()->doc ||
                node->as_element()->doc->js.doc_node != (void*)node) {
            return ItemNull;
        }
    }
    return radiant_dom_wrap_node(node);
}

static Item expando_get_map(DomNode* node) {
    Item wrapper = expando_wrapper(node, false);
    Item key = js_string_key(DOM_EXPANDO_BACKING_KEY);
    if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap) {
        Item map = vmap_backing_get(wrapper.vmap, key);
        if (get_type_id(map) == LMD_TYPE_MAP) return map;
    }
    AttachedExpandoRoot* attached = attached_expando_find(node);
    if (!attached || get_type_id(attached->map) != LMD_TYPE_MAP) return ItemNull;
    if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap) {
        vmap_backing_set(wrapper.vmap, key, attached->map);
    }
    return attached->map;
}

static Item expando_get_or_create_map(DomNode* node) {
    Item wrapper = expando_wrapper(node, true);
    if (get_type_id(wrapper) != LMD_TYPE_VMAP || !wrapper.vmap) return ItemNull;
    Item key = js_string_key(DOM_EXPANDO_BACKING_KEY);
    Item existing = vmap_backing_get(wrapper.vmap, key);
    if (get_type_id(existing) == LMD_TYPE_MAP) {
        attached_expando_root_add(node, existing);
        return existing;
    }
    Item m = js_new_object();
    // The wrapper's VMap backing store owns detached state. Connected nodes
    // additionally root the map until their native-tree detach transition.
    if (!vmap_backing_set(wrapper.vmap, key, m)) return ItemNull;
    attached_expando_root_add(node, m);
    return m;
}

extern "C" void js_dom_expando_attachment_changed(
        DomDocument*, DomNode* root, bool attached) {
    if (!js_active_runtime_state) return;
    if (!root) return;
    if (attached) {
        Item wrapper = expando_wrapper(root, false);
        if (get_type_id(wrapper) == LMD_TYPE_VMAP && wrapper.vmap) {
            Item map = vmap_backing_get(wrapper.vmap,
                js_string_key(DOM_EXPANDO_BACKING_KEY));
            attached_expando_root_add(root, map, true);
        }
    } else {
        attached_expando_root_remove(root);
    }
    if (root->is_element()) {
        for (DomNode* child = root->as_element()->first_child;
             child; child = child->next_sibling) {
            js_dom_expando_attachment_changed(nullptr, child, attached);
        }
    }
}

static void expando_reset() {
    if (!js_active_runtime_state) return;
    if (!s_attached_expando_roots) return;
    size_t iter = 0;
    void* item = nullptr;
    while (hashmap_iter(s_attached_expando_roots, &iter, &item)) {
        AttachedExpandoEntry* entry = (AttachedExpandoEntry*)item;
        if (!entry->root) continue;
        heap_unregister_gc_root(&entry->root->map.item);
        mem_free(entry->root);
    }
    hashmap_free(s_attached_expando_roots);
    s_attached_expando_roots = nullptr;
}

#undef s_attached_expando_roots

static bool expando_map_has_key(Item exp_map, Item key) {
    if (get_type_id(exp_map) != LMD_TYPE_MAP || !exp_map.map) return false;
    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* s = it2s(key);
    if (!s) return false;
    // Deleted expando entries leave shape tombstones behind; presence checks
    // must use the JS own-slot kernel, not raw shape lookup.
    return js_ordinary_has_own(exp_map, s->chars, (int)s->len);
}

static bool expando_get_property(DomNode* node, Item key, Item* out) {
    if (!node || !out) return false;
    Item exp_map = expando_get_map(node);
    if (!expando_map_has_key(exp_map, key)) return false;
    *out = js_get_key_default(exp_map, key);
    return true;
}

static void expando_set_property(DomNode* node, Item key, Item value) {
    if (!node) return;
    Item exp_map = expando_get_or_create_map(node);
    if (exp_map.item != ITEM_NULL) js_set_key_default(exp_map, key, value);
}
JS_FORWARD_STATIC_EXPRESSION(bool, expando_key_is_engine_internal, (const char* name, int name_len), (name && name_len >= 2 && name[0] == '_' && name[1] == '_'))

extern "C" bool js_dom_expando_has_property(Item obj, Item key) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(obj);
    if (!node) return false;
    Item exp_map = expando_get_map(node);
    return expando_map_has_key(exp_map, key);
}

extern "C" Item js_dom_expando_get_own_property_descriptor(Item obj, Item key) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(obj);
    if (!node) return make_js_undefined();
    Item exp_map = expando_get_map(node);
    if (!expando_map_has_key(exp_map, key)) return make_js_undefined();
    return js_object_get_own_property_descriptor(exp_map, key);
}

extern "C" Item js_dom_expando_delete_property(Item obj, Item key) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(obj);
    if (!node) return (Item){.item = b2it(true)};
    Item exp_map = expando_get_map(node);
    if (!expando_map_has_key(exp_map, key)) return (Item){.item = b2it(true)};
    if (get_type_id(key) != LMD_TYPE_STRING) return (Item){.item = b2it(true)};
    String* key_str = it2s(key);
    if (!key_str) return (Item){.item = b2it(true)};
    // DOM expandos live in a side table, so ordinary wrapper-map delete cannot
    // see them; delete must tombstone the side-table map entry directly.
    bool deleted = js_ordinary_delete(exp_map, key_str->chars, (int)key_str->len);
    return (Item){.item = b2it(deleted)};
}

extern "C" Item js_dom_expando_own_property_names(Item obj) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(obj);
    Item result = js_array_new(0);
    if (!node) return result;
    Item exp_map = expando_get_map(node);
    if (get_type_id(exp_map) != LMD_TYPE_MAP) return result;
    Item names = js_object_get_own_property_names(exp_map);
    if (get_type_id(names) != LMD_TYPE_ARRAY || !names.array) return result;
    for (int i = 0; i < names.array->length; i++) {
        Item key = names.array->items[i];
        if (get_type_id(key) != LMD_TYPE_STRING) continue;
        String* key_str = it2s(key);
        if (!key_str) continue;
        if (expando_key_is_engine_internal(key_str->chars, (int)key_str->len)) continue;
        js_array_push(result, key);
    }
    return result;
}

static bool js_dom_event_attr_name(const char* attr_name, char* prop_buf, size_t prop_buf_size) {
    if (!attr_name || !prop_buf || prop_buf_size == 0) return false;
    size_t len = strlen(attr_name);
    if (len < 3 || len >= prop_buf_size) return false;
    if ((attr_name[0] != 'o' && attr_name[0] != 'O') ||
        (attr_name[1] != 'n' && attr_name[1] != 'N')) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = attr_name[i];
        prop_buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 0x20) : c;
    }
    prop_buf[len] = '\0';
    return true;
}

static void js_dom_compile_event_attr_to_expando(DomElement* elem,
                                                 const char* attr_name,
                                                 const char* attr_value) {
    if (!elem || !attr_name || !attr_value) return;

    char prop_name[64];
    if (!js_dom_event_attr_name(attr_name, prop_name, sizeof(prop_name))) return;

    Item args[2];
    args[0] = js_name_item("event");
    args[1] = js_name_item(attr_value);
    Item fn = js_new_function_from_string(args, 2);
    if (get_type_id(fn) != LMD_TYPE_FUNC) {
        log_error("js_dom_event_attr: failed to compile %s handler", prop_name);
        return;
    }

    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item != ITEM_NULL) {
        js_set_key_default(exp_map, js_name_item(prop_name), fn);
        // Inline handlers are compiled during wrapper construction; wrapping
        // the same node here recursively re-enters initialization until the
        // stack overflows, so register against its canonical native key.
        js_dom_event_handler_property_set_for_node(elem, prop_name,
                                                   (int)strlen(prop_name), fn);
    }
}

static void js_dom_clear_event_attr_expando(DomElement* elem, const char* attr_name) {
    if (!elem || !attr_name) return;

    char prop_name[64];
    if (!js_dom_event_attr_name(attr_name, prop_name, sizeof(prop_name))) return;

    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item != ITEM_NULL) {
        js_set_key_default(exp_map, js_name_item(prop_name), ItemNull);
        js_dom_event_handler_property_set_for_node(elem, prop_name,
                                                   (int)strlen(prop_name), ItemNull);
    }
}

extern "C" bool js_dom_set_event_handler_function(void* dom_elem,
                                                  const char* attr_name,
                                                  Item fn) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !attr_name || !js_is_callable(fn)) return false;

    char prop_name[64];
    if (!js_dom_event_attr_name(attr_name, prop_name, sizeof(prop_name))) return false;

    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item == ITEM_NULL) return false;

    js_set_key_default(exp_map, js_name_item(prop_name), fn);
    js_dom_event_handler_property_set_for_node(elem, prop_name,
                                               (int)strlen(prop_name), fn);
    return true;
}

// ------------------------------------------------------------------
// HTML form-control IDL helpers (Phase 4 click activation).
// `checked` and `disabled` are boolean IDL attributes that must be
// returned as real booleans (assert_true requires `=== true`). Live
// checkedness is owned by StateStore when a document DocState exists;
// the expando fallback only serves detached/no-state DOM use.
// `disabled` is reflected directly to/from the `disabled` content
// attribute.
// ------------------------------------------------------------------

// Lowercase tag-name comparison helper. Returns true if elem->tag_name
// case-insensitively matches `name`.
static inline bool _is_tag(DomElement* elem, const char* name) {
    return elem && elem->tag_name && strcasecmp(elem->tag_name, name) == 0;
}

static bool js_dom_resolve_selector_pseudo_state(void* context,
                                                 DomElement* elem,
                                                 uint32_t pseudo_state) {
    DomDocument* doc = (DomDocument*)context;
    if (_is_tag(elem, "option") &&
        (pseudo_state == PSEUDO_STATE_CHECKED ||
         pseudo_state == PSEUDO_STATE_SELECTED)) {
        // Option selectedness is live IDL state, so attribute-only selector
        // matching makes option:checked stale after script changes .selected.
        return _get_selectedness(elem);
    }
    return state_resolve_selector_pseudo_state(
        doc ? doc->state : nullptr, elem, pseudo_state);
}

static SelectorMatcher* js_dom_create_selector_matcher(DomDocument* doc) {
    if (!doc || !doc->document_pool) return nullptr;
    SelectorMatcher* matcher = selector_matcher_create(doc->document_pool);
    if (matcher) {
        selector_matcher_set_pseudo_state_resolver(
            matcher, js_dom_resolve_selector_pseudo_state, doc);
    }
    return matcher;
}

// native-module selector fast paths must share the DOM resolver so live form
// state, including option:checked, agrees with ordinary JS queries.
JS_FORWARD_RETURN(void*, js_dom_create_selector_matcher_bridge, (void* dom_doc),
    js_dom_create_selector_matcher, ((DomDocument*)dom_doc))

// Returns the lowercased input `type` attribute (e.g. "checkbox", "radio",
// "submit", "button", "text"). Falls back to "text" when missing.
static const char* _input_type_lower(DomElement* elem) {
    static __thread char buf[24];
    const char* raw = elem->get_attribute("type");
    if (!raw || !*raw) return "text";
    int n = 0;
    while (raw[n] && n < (int)sizeof(buf) - 1) {
        buf[n] = (char)tolower((unsigned char)raw[n]);
        n++;
    }
    buf[n] = '\0';
    return buf;
}

static bool _is_checkbox_or_radio(DomElement* elem) {
    if (!_is_tag(elem, "input")) return false;
    const char* t = _input_type_lower(elem);
    return strcmp(t, "checkbox") == 0 || strcmp(t, "radio") == 0;
}

static DocState* _state_for_element(DomElement* elem) {
    if (elem && elem->doc && elem->doc->state) return elem->doc->state;
    return js_dom_current_state();
}

// Read the live "checkedness" state. Initialised lazily from the
// `checked` content attribute (HTML's defaultChecked) on first read.
static bool _get_checkedness(DomElement* elem) {
    DocState* state = _state_for_element(elem);
    if (state) return form_control_get_checked(state, (View*)elem);

    Item exp = expando_get_map((DomNode*)elem);
    if (exp.item != ITEM_NULL) {
        Item v = js_get_name_key(exp, "__checked");
        if (v.item != ITEM_NULL && !is_js_undefined(v)) return js_is_truthy(v);
    }
    // not initialised yet — derive from content attribute.
    return elem->has_attribute("checked");
}

static void _set_checkedness(DomElement* elem, bool v) {
    DocState* state = _state_for_element(elem);
    if (state) {
        form_control_set_checked(state, (View*)elem, v);
        // JS checkedness changes the live form state without passing through
        // native click dispatch, so explicitly refresh dependent :checked CSS.
        radiant_sync_pseudo_state((View*)elem, PSEUDO_STATE_CHECKED, v);
        return;
    }

    Item exp = expando_get_or_create_map((DomNode*)elem);
    if (exp.item == ITEM_NULL) return;
    js_set_name_key(exp, "__checked", (Item){.item = b2it(v)});
}

// Exposed for js_dom_events.cpp pre/post-click activation.
JS_FORWARD_RETURN(bool, js_dom_is_checkbox_or_radio, (void* dom_elem), _is_checkbox_or_radio, ((DomElement*)dom_elem))
JS_FORWARD_RETURN(bool, js_dom_get_checkedness, (void* dom_elem), _get_checkedness, ((DomElement*)dom_elem))
JS_FORWARD_VOID( js_dom_set_checkedness, (void* dom_elem, bool v), _set_checkedness, ((DomElement*)dom_elem, v))
extern "C" void js_dom_after_default_checked_set(void* dom_elem, bool checked) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return;
    Item exp = expando_get_map((DomNode*)elem);
    bool dirty = false;
    if (exp.item != ITEM_NULL) {
        Item v = js_get_key_cstr(exp, "__chkDirty");
        dirty = v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
    }
    // defaultChecked only syncs live checkedness before the user/API dirty flag.
    if (!dirty) _set_checkedness(elem, checked);
}
extern "C" void js_dom_set_checked_dirty(void* dom_elem, bool checked) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return;
    _set_checkedness(elem, checked);
    Item exp = expando_get_or_create_map((DomNode*)elem);
    if (exp.item != ITEM_NULL) {
        js_set_key_cstr(exp, "__chkDirty", (Item){.item = b2it(true)});
    }
}
JS_FORWARD_RETURN(const char*, js_dom_input_type_lower, (void* dom_elem), _input_type_lower, ((DomElement*)dom_elem))
extern "C" const char* js_dom_tag_name_raw(void* dom_elem) {
    DomElement* e = (DomElement*)dom_elem;
    return e ? e->tag_name : nullptr;
}
extern "C" bool js_dom_is_disabled(void* dom_elem) {
    DomElement* e = (DomElement*)dom_elem;
    return e && e->has_attribute("disabled");
}

// Returns true when the element is "connected" to its owning document
// (HTML's "is connected" predicate). Walks the parent chain looking for
// the document's root element. Newly-created elements that haven't been
// inserted into the tree are not connected.
extern "C" bool js_dom_is_connected(void* dom_elem) {
    DomElement* e = (DomElement*)dom_elem;
    if (!e || !e->doc) return false;
    DomElement* root = e->doc->root;
    if (!root) return false;
    DomNode* cur = (DomNode*)e;
    while (cur) {
        if (cur == (DomNode*)root) return true;
        cur = cur->parent;
    }
    return false;
}

// ============================================================================
// Named element access on Window (HTML spec: named access on Window object)
// Walks DOM tree, registers elements with id as global properties
// ============================================================================

static void register_named_elements_recursive(DomElement* elem, Item global) {
    if (!elem) return;
    // Generated layout nodes are not script-visible DOM and can outlive a
    // source subtree; never expose their retained storage as Window globals.
    if (elem->is_synthetic()) return;

    if (elem->id && elem->id[0] != '\0') {
        Item key = js_name_item(elem->id);
        // HTML named-property access on Window reflects the *current* element
        // with this id. Register when there is no own property yet, and also
        // refresh a stale auto-registered wrapper whose element was detached
        // (e.g. after `innerHTML` replaced the subtree). Do NOT clobber a
        // genuine user-assigned global, and keep the first connected element in
        // tree order when ids collide within the current document.
        bool do_register = true;
        if (it2b(js_has_own_property(global, key))) {
            DomNode* exn = static_cast<DomNode*>(
                js_dom_unwrap_element(js_get_key_default(global, key)));
            DomElement* ex = (exn && exn->is_element()) ? exn->as_element() : nullptr;
            if (!ex) {
                do_register = false;                          // user-assigned global
            } else if (ex == elem) {
                do_register = false;                          // already bound to this element
            } else if (js_dom_node_is_connected((DomNode*)ex)) {
                do_register = false;                          // a connected element already owns this id
            }
            // else: existing binding is a stale/detached wrapper → refresh it
        }
        if (do_register) {
            Item wrapped = js_dom_wrap_element(elem);
            js_set_key_default(global, key, wrapped);
            log_debug("js_dom: registered element id='%s' on global object", elem->id);
        }
    }

    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            register_named_elements_recursive(child->as_element(), global);
        }
        child = child->next_sibling;
    }
}

void js_dom_register_named_elements(DomElement* root) {
    if (!root) return;
    Item global = js_get_global_this();
    register_named_elements_recursive(root, global);
}

static void js_dom_install_window_frames_global(void);
static void js_dom_install_window_dialog_globals(void);
static void js_dom_install_window_computed_style_global(void);
static void js_dom_install_dom_parser_global(void);
static void js_dom_install_xml_serializer_global(void);
static DomDocument* js_document_proxy_doc_from_item(Item item);

// ============================================================================
// DOM Context Management
// ============================================================================

extern "C" void js_dom_set_document(void* dom_doc) {
    // Host teardown may notify after the document Runtime has released its
    // capsule. There is then no JS realm whose DOM bindings may be changed.
    if (!js_active_runtime_state) return;
    _js_current_document = (DomDocument*)dom_doc;
    _js_main_document = (DomDocument*)dom_doc;
    // Host cleanup may clear the active EvalContext before restoring the
    // document pointer. The pointer update is still valid, but context-owned
    // JS wrapper storage must not be read without its owner bound.
    if (js_active_runtime_state && js_document_proxy_item.item != ITEM_NULL) {
        TypeId proxy_type = get_type_id(js_document_proxy_item);
        if (proxy_type == LMD_TYPE_VMAP && js_document_proxy_item.vmap &&
            (js_document_proxy_item.vmap->host_type == (const void*)&js_document_proxy_vmap_marker ||
             js_document_proxy_item.vmap->host_type == radiant_dom_document_host_type())) {
            js_document_proxy_item.vmap->host_data = dom_doc;
        }
    }
    if (dom_doc) {
        // document binding creates Radiant-owned wrappers before the first JS
        // DOM property read; lazy module activation must therefore complete at
        // this boundary or radiant_host_api remains null during bootstrap.
        const JubeModuleDef* radiant = jube_find_static_module("radiant");
        if (!radiant || !jube_activate_module(radiant)) {
            log_error("js_dom_set_document: failed to activate radiant module");
            return;
        }
        js_doc_mark_has_browsing_context(dom_doc);
        DomDocument* doc = (DomDocument*)dom_doc;
        // Batch-mode page scripts enter a fresh JS realm after document load;
        // rebind XHR to the retained document URL instead of leaving relative
        // requests with the reset batch's empty base.
        js_xhr_set_base_url(doc->url ? url_get_href(doc->url) : nullptr);
        if (doc->document_pool) {
            css_property_system_init(doc->document_pool);
        }
        // populate global object with element IDs (browser-like named access on Window)
        if (doc->root) {
            js_dom_register_named_elements(doc->root);
        }
        // install window.getSelection() global
        js_dom_selection_install_globals();
        js_dom_install_window_frames_global();
        js_dom_install_window_dialog_globals();
        js_dom_install_window_computed_style_global();
        // install FormData constructor
        extern void js_formdata_install_globals(void);
        js_formdata_install_globals();
        // F-1: install collection interface globals (HTMLCollection,
        // NodeList, RadioNodeList, HTMLFormControlsCollection,
        // HTMLOptionsCollection) so existence and instanceof checks work.
        extern void js_dom_install_collection_globals(void);
        js_dom_install_collection_globals();
        // F-5: install HTMLOptionElement Option() constructor.
        extern void js_dom_install_option_constructor(void);
        js_dom_install_option_constructor();
        js_dom_install_dom_parser_global();
        js_dom_install_xml_serializer_global();
        js_history_install_globals();
        Item global = js_get_global_this();
        js_set_native_key(global, js_string_key("__lambda_testdriver_key"), js_dom_testdriver_key);
        js_set_native_key(global, js_string_key("__lambda_set_editing_behavior"), js_dom_set_editing_behavior);
    }
    log_debug("js_dom_set_document: set document=%p", dom_doc);
}

// host setup may query before it has bound a document Runtime. There is no
// ambient document to borrow in that phase; normal DOM execution binds the
// owning EvalContext before accessing this context-local pointer.
JS_FORWARD_EXPRESSION(void*, js_dom_get_document, (void),
    js_active_runtime_state ? (void*)_js_current_document : nullptr)

extern "C" void js_dom_set_ui_context(void* ui_context) {
    if (!js_active_runtime_state) return;
    _js_current_ui_context = (UiContext*)ui_context;
}

JS_FORWARD_EXPRESSION(void*, js_dom_get_ui_context, (void),
    js_active_runtime_state ? (void*)_js_current_ui_context : nullptr)

// ============================================================================
// Document-as-Node stub
// (Lazy DomElement with tag "#document" so JS Range/Selection APIs can
// accept `document` (or a foreign-doc wrapper) as a container.)
// ============================================================================

static Item lookup_foreign_doc_wrapper(DomDocument* doc); // fwd decl

extern "C" void* js_dom_get_or_create_doc_node(void* doc_v) {
    DomDocument* doc = (DomDocument*)doc_v;
    if (!doc) return nullptr;
    if (doc->js.doc_node) return doc->js.doc_node;
    // build a DomElement with tag "#document". first_child is set to doc->root
    // so dom_node_boundary_length(stub) returns child count of the document
    // (e.g. 1 for HTML docs without doctype, 2 with doctype).
    MarkBuilder builder(doc->input);
    Item e_item = builder.element("#document").final();
    Element* elmt = e_item.element;
    DomElement* stub = dom_element_create(doc, "#document", elmt);
    if (!stub) return nullptr;
    // Synthesize a leading DOCTYPE child so that document.childNodes "length"
    // (per dom_node_boundary_length) is 2 — matching how WPT tests assume HTML
    // documents have <!DOCTYPE> + html as their two top-level children.
    Item dt_item = builder.element("!DOCTYPE").final();
    DomComment* dt = dom_comment_create_detached(dt_item.element, doc);
    DomNode* head_node = nullptr;
    DomNode* tail_node = nullptr;
    if (dt) {
        head_node = (DomNode*)dt;
        tail_node = (DomNode*)dt;
        ((DomNode*)dt)->parent = (DomNode*)stub;
    }
    if (doc->root) {
        if (tail_node) {
            // Forward link only — do NOT set root->prev_sibling, since other
            // radiant code walks prev_sibling without checking parent and
            // could be affected. Only forward traversals (used by
            // dom_node_boundary_length and compareDocumentPosition for the
            // stub) need the link.
            tail_node->next_sibling = (DomNode*)doc->root;
        } else {
            head_node = (DomNode*)doc->root;
        }
        // Treat the stub as document root's parent (DOM semantics: the
        // document IS the parent of the documentElement). Only set when
        // currently null so we don't override real tree relationships.
        if (!((DomNode*)doc->root)->parent) {
            ((DomNode*)doc->root)->parent = (DomNode*)stub;
        }
        DomNode* c = (DomNode*)doc->root;
        while (c->next_sibling) c = c->next_sibling;
        tail_node = c;
    }
    ((DomElement*)stub)->first_child = head_node;
    ((DomElement*)stub)->last_child  = tail_node;
    doc->js.doc_node = stub;
    return stub;
}

// Returns the document proxy / foreign-doc wrapper for the given DomDocument*,
// or ItemNull if none is registered.
static Item doc_to_proxy_item(DomDocument* doc) {
    if (!doc) return ItemNull;
    if (doc == _js_main_document) {
        return js_get_document_object_value();
    }
    return lookup_foreign_doc_wrapper(doc);
}
JS_FORWARD_ITEM(js_dom_document_proxy_for_doc_bridge, (void* doc_v), doc_to_proxy_item, ((DomDocument*)doc_v))

// ============================================================================
// DOM Wrapping / Unwrapping
// ============================================================================

extern "C" void radiant_dom_reset_wrapper_cache(void);
JS_FORWARD_STATIC_VOID( reset_dom_wrapper_cache, (), radiant_dom_reset_wrapper_cache, ())

extern "C" void js_dom_initialize_node_wrapper(void* dom_elem) {
    DomNode* node = (DomNode*)dom_elem;
    if (!node || !node->is_element()) return;
    DomElement* elem = node->as_element();
    int attr_count = 0;
    const char** attr_names = elem->attribute_names(&attr_count);
    for (int i = 0; attr_names && i < attr_count; i++) {
        const char* name = attr_names[i];
        const char* value = elem->get_attribute(name);
        js_dom_compile_event_attr_to_expando(elem, name, value);
    }
}

struct JsDomHtmlInterfaceEntry {
    const char* tag_name;
    const char* constructor_name;
};

static const JsDomHtmlInterfaceEntry s_js_dom_html_interfaces[] = {
    {"a", "HTMLAnchorElement"},
    {"button", "HTMLButtonElement"},
    {"form", "HTMLFormElement"},
    {"input", "HTMLInputElement"},
    {"option", "HTMLOptionElement"},
    {"select", "HTMLSelectElement"},
    {"textarea", "HTMLTextAreaElement"},
};

static const char* js_dom_html_interface_name(DomElement* elem) {
    if (!elem || !elem->tag_name || elem->tag_name[0] == '#') return nullptr;
    int count = (int)(sizeof(s_js_dom_html_interfaces) /
        sizeof(s_js_dom_html_interfaces[0]));
    for (int i = 0; i < count; i++) {
        if (strcasecmp(elem->tag_name, s_js_dom_html_interfaces[i].tag_name) == 0) {
            return s_js_dom_html_interfaces[i].constructor_name;
        }
    }
    return nullptr;
}

static bool js_dom_element_is_svg(DomElement* elem) {
    if (!elem || !elem->tag_name) return false;
    const char* namespace_uri = elem->get_attribute("__lambda_ns_uri");
    if (namespace_uri && strcmp(namespace_uri, "http://www.w3.org/2000/svg") == 0) {
        return true;
    }
    for (DomNode* current = (DomNode*)elem; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* ancestor = current->as_element();
        if (ancestor && ancestor->tag_name && strcasecmp(ancestor->tag_name, "svg") == 0) {
            return true;
        }
    }
    return false;
}

static const char* js_dom_svg_interface_name(DomElement* elem) {
    if (!js_dom_element_is_svg(elem)) return nullptr;
    if (strcasecmp(elem->tag_name, "svg") == 0) return "SVGSVGElement";
    if (strcasecmp(elem->tag_name, "path") == 0) return "SVGPathElement";
    if (strcasecmp(elem->tag_name, "text") == 0 ||
        strcasecmp(elem->tag_name, "tspan") == 0) return "SVGTextContentElement";
    if (strcasecmp(elem->tag_name, "g") == 0 ||
        strcasecmp(elem->tag_name, "rect") == 0 ||
        strcasecmp(elem->tag_name, "circle") == 0 ||
        strcasecmp(elem->tag_name, "ellipse") == 0 ||
        strcasecmp(elem->tag_name, "line") == 0 ||
        strcasecmp(elem->tag_name, "polyline") == 0 ||
        strcasecmp(elem->tag_name, "polygon") == 0 ||
        strcasecmp(elem->tag_name, "image") == 0) {
        return "SVGGraphicsElement";
    }
    return "SVGElement";
}

extern "C" Item js_dom_get_prototype_value(Item obj) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(obj);
    const char* ctor_name = "Node";
    if (node && node->is_element()) {
        DomElement* elem = node->as_element();
        if (elem && elem->tag_name && strcmp(elem->tag_name, "#document-fragment") == 0) {
            ctor_name = elem->shadow_host_element() ? "ShadowRoot" : "DocumentFragment";
        } else {
            // HTML DOM wrappers need HTMLElement as their immediate prototype so
            // browser-library instanceof checks walk HTMLElement -> Element -> Node.
            const char* svg_interface = js_dom_svg_interface_name(elem);
            const char* html_interface = svg_interface ? nullptr : js_dom_html_interface_name(elem);
            ctor_name = svg_interface ? svg_interface : (html_interface ? html_interface :
                ((elem && elem->tag_name && elem->tag_name[0] != '#')
                    ? "HTMLElement" : "Element"));
        }
    }
    Item global = js_get_global_this();
    Item ctor = js_get_key_default(global, js_string_key(ctor_name));
    if (get_type_id(ctor) != LMD_TYPE_FUNC && strcmp(ctor_name, "HTMLElement") == 0) {
        ctor = js_get_key_cstr(global, "Element");
    }
    if (get_type_id(ctor) != LMD_TYPE_FUNC) return ItemNull;
    Item proto = js_get_key_cstr(ctor, "prototype");
    return get_type_id(proto) == LMD_TYPE_MAP ? proto : ItemNull;
}

extern "C" Item radiant_dom_wrap_node(void* dom_elem);

// Jube POC: keep existing JS callers stable while wrapper identity moves
// behind the radiant module boundary.
JS_FORWARD_ITEM(js_dom_wrap_element, (void* dom_elem), radiant_dom_wrap_node, (dom_elem))

extern "C" void* radiant_dom_unwrap_node(Item item);
extern "C" bool radiant_dom_is_node(Item item);

// Jube POC: unwrap/type policy is exposed through the radiant module so DOM
// nodes no longer depend on the retired DOM map wrapper shell.
JS_FORWARD_RETURN(void*, js_dom_unwrap_element, (Item item), radiant_dom_unwrap_node, (item))

extern "C" void* js_dom_unwrap_element_impl(Item item) {
    TypeId tid = get_type_id(item);
    if (tid == LMD_TYPE_VMAP) {
        // Only Radiant-branded VMaps unwrap as DOM nodes; style/CSSOM VMaps
        // must stop here or Radiant and DOM unwrap helpers recurse forever.
        if (radiant_dom_is_node(item)) return item.vmap->host_data;
        DomDocument* doc = js_document_proxy_doc_from_item(item);
        return doc ? js_dom_get_or_create_doc_node(doc) : nullptr;
    }
    return nullptr;
}

// Jube POC: use the module-owned type test so later native wrappers do not
// need every caller to know the concrete carrier representation.
JS_FORWARD_RETURN(bool, js_is_dom_node, (Item item), radiant_dom_is_node, (item))

struct SelectOptionsOwnerEntry {
    Array* array;
    DomElement* owner;
    DomNodeRef owner_ref;
    int kind;
};

struct LiveChildCollectionEntry {
    Array* array;
    DomElement* owner;
    DomNodeRef owner_ref;
    int kind;
};

struct LiveFormCollectionEntry {
    Array* array;
    DomDocument* doc;
    DomElement* owner;
    DomNodeRef owner_ref;
    int kind;
};

struct LiveLookupCollectionEntry {
    Array* array;
    DomDocument* doc;
    DomElement* root;
    DomNodeRef root_ref;
    String* query;
    int kind;
    bool include_root;
};

static const int SELECT_COLLECTION_OPTIONS = 1;
static const int SELECT_COLLECTION_SELECTED_OPTIONS = 2;
static const int SELECT_OPTIONS_OWNER_CACHE_SIZE = 4096;
static const int LIVE_CHILD_COLLECTION_CHILDREN = 1;
static const int LIVE_CHILD_COLLECTION_CHILD_NODES = 2;
static const int LIVE_CHILD_COLLECTION_CACHE_SIZE = 4096;
static const int LIVE_FORM_COLLECTION_DOCUMENT_FORMS = 1;
static const int LIVE_FORM_COLLECTION_FORM_ELEMENTS = 2;
static const int LIVE_FORM_COLLECTION_CACHE_SIZE = 4096;
static const int LIVE_LOOKUP_COLLECTION_TAG = 1;
static const int LIVE_LOOKUP_COLLECTION_CLASS = 2;
static const int LIVE_LOOKUP_COLLECTION_NAME = 3;
static const int LIVE_LOOKUP_COLLECTION_CACHE_SIZE = 4096;
// Live collection tables retain weak JS-array homes and native-node pins. They
// are lazy per-document-realm state, so property reads keep direct local table
// access and never contend with another context.
struct JsDomCollectionRuntimeState {
    SelectOptionsOwnerEntry select_options_owners[SELECT_OPTIONS_OWNER_CACHE_SIZE] = {};
    int select_options_owner_count = 0;
    LiveChildCollectionEntry live_child_collections[LIVE_CHILD_COLLECTION_CACHE_SIZE] = {};
    int live_child_collection_count = 0;
    LiveFormCollectionEntry live_form_collections[LIVE_FORM_COLLECTION_CACHE_SIZE] = {};
    int live_form_collection_count = 0;
    LiveLookupCollectionEntry live_lookup_collections[LIVE_LOOKUP_COLLECTION_CACHE_SIZE] = {};
    int live_lookup_collection_count = 0;
    int refresh_depth = 0;
};

JS_FORWARD_STATIC_EXPRESSION(JsDomCollectionRuntimeState*,
    js_dom_collection_runtime_state_get, (),
    (js_active_runtime_state ? (JsDomCollectionRuntimeState*)js_runtime_state.dom_collection_state : nullptr))

template <typename State>
static State* js_dom_runtime_state_ensure(void*& slot, const char* label) {
    if (!js_active_runtime_state) return nullptr;
    State* state = (State*)slot;
    if (state) return state;
    state = (State*)mem_calloc(1, sizeof(State), MEM_CAT_DOM);
    if (!state) {
        log_error("%s: failed to allocate context state", label);
        return nullptr;
    }
    slot = state;
    return state;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_collection_runtime_state_ensure, (), (js_dom_runtime_state_ensure<JsDomCollectionRuntimeState>(js_runtime_state.dom_collection_state, "js-dom-collections") != nullptr))

#define js_dom_collection_state ((JsDomCollectionRuntimeState*)js_runtime_state.dom_collection_state)
#define s_select_options_owners (js_dom_collection_state->select_options_owners)
#define s_select_options_owner_count (js_dom_collection_state->select_options_owner_count)
#define s_live_child_collections (js_dom_collection_state->live_child_collections)
#define s_live_child_collection_count (js_dom_collection_state->live_child_collection_count)
#define s_live_form_collections (js_dom_collection_state->live_form_collections)
#define s_live_form_collection_count (js_dom_collection_state->live_form_collection_count)
#define s_live_lookup_collections (js_dom_collection_state->live_lookup_collections)
#define s_live_lookup_collection_count (js_dom_collection_state->live_lookup_collection_count)
#define s_dom_collection_refresh_depth (js_dom_collection_state->refresh_depth)

extern "C" void heap_register_gc_weak(uint64_t* slot,
    void (*on_clear)(uint64_t*, void*), void* weak_context);

struct DomCollectionRefreshGuard {
    bool active = false;
    DomCollectionRefreshGuard() {
        active = js_dom_collection_runtime_state_get() != nullptr;
        if (active) s_dom_collection_refresh_depth++;
    }
    ~DomCollectionRefreshGuard() {
        if (active) s_dom_collection_refresh_depth--;
    }
};

static bool live_collection_pin(DomDocument* doc, DomElement* owner,
                                DomNodeRef* out_ref) {
    if (!doc || !owner || !out_ref) return false;
    *out_ref = dom_node_ref((DomNode*)owner);
    if (!dom_node_ref_validate(doc, *out_ref)) return false;
    return dom_node_pin(doc, *out_ref, DOM_NODE_PIN_LIVE_COLLECTION);
}

static void live_collection_unpin(DomDocument* doc, DomNodeRef* ref) {
    if (!doc || !ref || !ref->address) return;
    dom_node_unpin(doc, *ref, DOM_NODE_PIN_LIVE_COLLECTION);
    *ref = {nullptr, 0};
}

static void reset_live_dom_collections() {
    if (!js_dom_collection_runtime_state_get()) return;
    // Live collections keep native owner pointers outside the GC graph; release
    // their explicit lifecycle pins before the next document epoch can recycle.
    for (int i = 0; i < s_select_options_owner_count; i++) {
        heap_unregister_gc_weak((uint64_t*)&s_select_options_owners[i].array);
        DomElement* owner = s_select_options_owners[i].owner;
        live_collection_unpin(owner ? owner->doc : nullptr,
                              &s_select_options_owners[i].owner_ref);
    }
    for (int i = 0; i < s_live_child_collection_count; i++) {
        heap_unregister_gc_weak((uint64_t*)&s_live_child_collections[i].array);
        DomElement* owner = s_live_child_collections[i].owner;
        live_collection_unpin(owner ? owner->doc : nullptr,
                              &s_live_child_collections[i].owner_ref);
    }
    for (int i = 0; i < s_live_form_collection_count; i++) {
        heap_unregister_gc_weak((uint64_t*)&s_live_form_collections[i].array);
        live_collection_unpin(s_live_form_collections[i].doc,
                              &s_live_form_collections[i].owner_ref);
    }
    for (int i = 0; i < s_live_lookup_collection_count; i++) {
        heap_unregister_gc_weak((uint64_t*)&s_live_lookup_collections[i].array);
        live_collection_unpin(s_live_lookup_collections[i].doc,
                              &s_live_lookup_collections[i].root_ref);
    }
    memset(s_select_options_owners, 0, sizeof(s_select_options_owners));
    s_select_options_owner_count = 0;
    memset(s_live_child_collections, 0, sizeof(s_live_child_collections));
    s_live_child_collection_count = 0;
    memset(s_live_form_collections, 0, sizeof(s_live_form_collections));
    s_live_form_collection_count = 0;
    memset(s_live_lookup_collections, 0, sizeof(s_live_lookup_collections));
    s_live_lookup_collection_count = 0;
}

template <typename Entry>
static void js_dom_register_owner_collection(Item collection, DomElement* owner,
        int kind, Entry* entries, int* count, int capacity) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !collection.array || !owner) return;
    for (int i = 0; i < *count; i++) {
        if (entries[i].array == collection.array) {
            if (entries[i].owner != owner) {
                DomElement* old_owner = entries[i].owner;
                live_collection_unpin(old_owner ? old_owner->doc : nullptr,
                                      &entries[i].owner_ref);
                if (!live_collection_pin(owner->doc, owner,
                        &entries[i].owner_ref)) return;
            }
            entries[i].owner = owner;
            entries[i].kind = kind;
            return;
        }
    }
    int entry_index = *count;
    for (int i = 0; i < *count; i++) {
        if (!entries[i].array) {
            DomElement* old_owner = entries[i].owner;
            live_collection_unpin(old_owner ? old_owner->doc : nullptr,
                                  &entries[i].owner_ref);
            entry_index = i;
            break;
        }
    }
    if (entry_index >= capacity) return;
    Entry* entry = &entries[entry_index];
    if (!live_collection_pin(owner->doc, owner, &entry->owner_ref)) return;
    entry->array = collection.array;
    entry->owner = owner;
    entry->kind = kind;
    // Weak-clear the raw array address before its allocator slot can be reused.
    heap_register_gc_weak((uint64_t*)&entry->array, nullptr, nullptr);
    if (entry_index == *count) (*count)++;
}
#define JS_DOM_REGISTER_OWNER_COLLECTION(name, entries, count, capacity) \
static void name(Item collection, DomElement* owner, int kind) { \
    JsDomCollectionRuntimeState* state = js_dom_collection_runtime_state_get(); \
    if (!state) state = js_dom_runtime_state_ensure<JsDomCollectionRuntimeState>( \
        js_runtime_state.dom_collection_state, "js-dom-collections"); \
    if (!state) return; \
    js_dom_register_owner_collection(collection, owner, kind, state->entries, \
        &state->count, capacity); \
}
JS_DOM_REGISTER_OWNER_COLLECTION(_register_select_options_owner,
    select_options_owners, select_options_owner_count,
    SELECT_OPTIONS_OWNER_CACHE_SIZE)

template <typename Entry>
static DomElement* js_dom_collection_owner(Entry* entries, int count,
                                           Item collection, int* out_kind) {
    if (!js_dom_collection_runtime_state_get()) return nullptr;
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !collection.array) return nullptr;
    for (int i = 0; i < count; i++) {
        if (entries[i].array == collection.array) {
            if (out_kind) *out_kind = entries[i].kind;
            return entries[i].owner;
        }
    }
    return nullptr;
}

static DomElement* _select_options_owner(Item collection, int* out_kind) {
    return js_dom_collection_owner(s_select_options_owners,
        s_select_options_owner_count, collection, out_kind);
}
JS_DOM_REGISTER_OWNER_COLLECTION(_register_live_child_collection,
    live_child_collections, live_child_collection_count,
    LIVE_CHILD_COLLECTION_CACHE_SIZE)
#undef JS_DOM_REGISTER_OWNER_COLLECTION

static DomElement* _live_child_collection_owner(Item collection, int* out_kind) {
    return js_dom_collection_owner(s_live_child_collections,
        s_live_child_collection_count, collection, out_kind);
}

template <typename Entry>
struct JsDomLiveCollectionTraits;

template <>
struct JsDomLiveCollectionTraits<LiveFormCollectionEntry> {
    static DomDocument* doc(LiveFormCollectionEntry* entry) { return entry->doc; }
    static DomElement* subject(LiveFormCollectionEntry* entry) { return entry->owner; }
    static DomNodeRef* pin_ref(LiveFormCollectionEntry* entry) {
        return &entry->owner_ref;
    }
    static void assign(LiveFormCollectionEntry* entry, DomDocument* doc,
            DomElement* subject, int kind) {
        entry->doc = doc;
        entry->owner = subject;
        entry->kind = kind;
    }
};

template <>
struct JsDomLiveCollectionTraits<LiveLookupCollectionEntry> {
    static DomDocument* doc(LiveLookupCollectionEntry* entry) { return entry->doc; }
    static DomElement* subject(LiveLookupCollectionEntry* entry) { return entry->root; }
    static DomNodeRef* pin_ref(LiveLookupCollectionEntry* entry) {
        return &entry->root_ref;
    }
    static void assign(LiveLookupCollectionEntry* entry, DomDocument* doc,
            DomElement* subject, int kind) {
        entry->doc = doc;
        entry->root = subject;
        entry->kind = kind;
    }
};

template <typename Entry>
static void js_dom_register_live_collection(Item collection, DomDocument* doc,
        DomElement* subject, int kind, Entry* entries, int* count, int capacity) {
    if (!js_dom_collection_runtime_state_ensure()) return;
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !collection.array) return;
    if (!doc && subject) doc = subject->doc;
    if (!doc && !subject) return;
    DomElement* pin_owner = subject ? subject : doc->root;
    for (int i = 0; i < *count; i++) {
        Entry* entry = &entries[i];
        if (entry->array != collection.array) continue;
        DomElement* old_pin_owner = JsDomLiveCollectionTraits<Entry>::subject(entry);
        if (!old_pin_owner) {
            DomDocument* old_doc = JsDomLiveCollectionTraits<Entry>::doc(entry);
            old_pin_owner = old_doc ? old_doc->root : nullptr;
        }
        if (old_pin_owner != pin_owner || JsDomLiveCollectionTraits<Entry>::doc(entry) != doc) {
            live_collection_unpin(JsDomLiveCollectionTraits<Entry>::doc(entry),
                JsDomLiveCollectionTraits<Entry>::pin_ref(entry));
            if (!live_collection_pin(doc, pin_owner,
                    JsDomLiveCollectionTraits<Entry>::pin_ref(entry))) return;
        }
        JsDomLiveCollectionTraits<Entry>::assign(entry, doc, subject, kind);
        return;
    }
    int entry_index = *count;
    for (int i = 0; i < *count; i++) {
        Entry* entry = &entries[i];
        if (!entry->array) {
            live_collection_unpin(JsDomLiveCollectionTraits<Entry>::doc(entry),
                JsDomLiveCollectionTraits<Entry>::pin_ref(entry));
            entry_index = i;
            break;
        }
    }
    if (entry_index >= capacity) return;
    Entry* entry = &entries[entry_index];
    if (!live_collection_pin(doc, pin_owner,
            JsDomLiveCollectionTraits<Entry>::pin_ref(entry))) return;
    entry->array = collection.array;
    JsDomLiveCollectionTraits<Entry>::assign(entry, doc, subject, kind);
    // the cache is a weak observer; the array must not keep its own slot alive.
    heap_register_gc_weak((uint64_t*)&entry->array, nullptr, nullptr);
    if (entry_index == *count) (*count)++;
}

static void _register_live_form_collection(Item collection, DomDocument* doc,
                                           DomElement* owner, int kind) {
    // The generic helper receives state-owned table pointers by value; initialize
    // the realm state before those arguments are evaluated (D5.2).
    if (!js_dom_collection_runtime_state_ensure()) return;
    js_dom_register_live_collection(collection, doc, owner, kind,
        s_live_form_collections, &s_live_form_collection_count,
        LIVE_FORM_COLLECTION_CACHE_SIZE);
}

template <typename Entry>
static Entry* js_dom_collection_entry(Entry* entries, int count, Item collection) {
    if (!js_dom_collection_runtime_state_get()) return nullptr;
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !collection.array) return nullptr;
    for (int i = 0; i < count; i++) {
        if (entries[i].array == collection.array) {
            return &entries[i];
        }
    }
    return nullptr;
}

static LiveFormCollectionEntry* _live_form_collection_entry(Item collection) {
    return js_dom_collection_entry(s_live_form_collections,
        s_live_form_collection_count, collection);
}

static LiveLookupCollectionEntry* _live_lookup_collection_entry(Item collection);

static void _register_live_lookup_collection(Item collection, DomDocument* doc,
                                             DomElement* root, int kind,
                                             bool include_root, const char* query) {
    if (!query) return;
    if (!doc && root) doc = root->doc;
    if (!doc && !root) return;
    // The generic helper receives state-owned table pointers by value; initialize
    // the realm state before those arguments are evaluated (D5.2).
    if (!js_dom_collection_runtime_state_ensure()) return;
    String* query_name = heap_create_name(query);
    js_dom_register_live_collection(collection, doc, root, kind,
        s_live_lookup_collections, &s_live_lookup_collection_count,
        LIVE_LOOKUP_COLLECTION_CACHE_SIZE);
    LiveLookupCollectionEntry* entry = _live_lookup_collection_entry(collection);
    if (!entry) return;
    entry->query = query_name;
    entry->include_root = include_root;
}

static LiveLookupCollectionEntry* _live_lookup_collection_entry(Item collection) {
    return js_dom_collection_entry(s_live_lookup_collections,
        s_live_lookup_collection_count, collection);
}

extern "C" bool js_dom_collection_has_live_property_state(Item collection) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY) return false;
    if (!js_dom_collection_runtime_state_get()) return false;
    int collection_kind = 0;
    if (_live_child_collection_owner(collection, &collection_kind)) return true;
    if (_live_form_collection_entry(collection)) return true;
    if (_live_lookup_collection_entry(collection)) return true;
    return _select_options_owner(collection, &collection_kind) != nullptr;
}

static Item js_dom_text_replace_data_method(DomText* text_node, Item offset_arg,
                                            Item count_arg, Item data_arg);
static Item js_dom_text_insert_data_method(DomText* text_node, Item offset_arg,
                                           Item data_arg);
static Item js_dom_text_append_data_method(DomText* text_node, Item data_arg);
static Item js_dom_text_delete_data_method(DomText* text_node, Item offset_arg,
                                           Item count_arg);
static Item js_dom_text_substring_data_method(DomText* text_node, Item offset_arg,
                                              Item count_arg);
static Item js_text_data_body(Item callee, Item this_value, Item* args,
                              int argc, uint64_t* result_home);
static void js_dom_expando_flag_set(DomElement* elem, const char* name, Item value);
static void _select_refresh_options_collection(Item collection, DomElement* sel);
static void _select_refresh_selected_options_collection(Item collection, DomElement* sel);

static Item _collection_named_item(Item name_arg) {
    const char* name = fn_to_cstr(name_arg);
    if (!name || !*name) return ItemNull;
    Item self = js_get_this();
    if (get_type_id(self) != LMD_TYPE_ARRAY || !self.array) return ItemNull;
    for (int64_t i = 0; i < self.array->length; i++) {
        Item item = js_elements_get_int(self, i);
        DomElement* elem = (DomElement*)js_dom_unwrap_element(item);
        if (!elem) continue;
        const char* id = elem->get_attribute("id");
        if (id && strcmp(id, name) == 0) return item;
        const char* nm = elem->get_attribute("name");
        if (nm && strcmp(nm, name) == 0) return item;
    }
    return ItemNull;
}

static Item _options_collection_add(Item element_arg, Item before_arg) {
    Item self = js_get_this();
    int kind = 0;
    DomElement* owner = _select_options_owner(self, &kind);
    if (!owner || kind != SELECT_COLLECTION_OPTIONS || !_is_tag(owner, "select")) return ItemNull;

    Item args[2] = { element_arg, before_arg };
    return radiant_dom_element_operation(js_dom_wrap_element(owner), JUBE_DOM_ADD, args, 2);
}

static void _decorate_dom_collection(Item collection, const char* ctor_name) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !ctor_name) return;
    Item named_key = js_name_item("namedItem");
    Item existing = js_get_key_default(collection, named_key);
    if (!js_is_callable(existing)) {
        js_set_native_key(collection, named_key, _collection_named_item);
    }
    Item ctor = js_get_key_default(js_get_global_this(), js_name_item(ctor_name));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_set_key_cstr(collection, "constructor", ctor);
    }
}

static void _decorate_options_collection(Item collection) {
    _decorate_dom_collection(collection, "HTMLOptionsCollection");
    if (get_type_id(collection) != LMD_TYPE_ARRAY) return;

    Item add_key = js_name_item("add");
    Item existing = js_get_key_default(collection, add_key);
    if (js_is_callable(existing)) return;

    // select.options is a live collection object, so install add() on the collection and delegate to the owning select.
    Item add_fn = js_new_native_function(_options_collection_add);
    js_set_function_name(add_fn, add_key);
    js_set_key_default(collection, add_key, add_fn);
}

static bool _array_companion_set_int_slot(Item collection, const char* name,
                                          int name_len, int64_t value) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !collection.array ||
        !js_array_has_props(collection.array) || !name || name_len <= 0) {
        return false;
    }
    Map* props = js_array_props(collection.array);
    if (!props || !map_kind_is_array_props(props->map_kind) || !props->data) return false;
    Item props_item = (Item){.map = props};
    ShapeEntry* entry = nullptr;
    JsShapeSlotStatus status = js_own_shape_slot_status(props_item, name, name_len, nullptr, &entry);
    if (status != JS_SHAPE_SLOT_DATA || !entry || entry->byte_offset < 0 ||
        entry->byte_offset + (int64_t)sizeof(int64_t) > (int64_t)props->data_cap) {
        return false;
    }
    // The array companion uses shaped int64 lanes; an IEEE payload would be
    // decoded as an int poison sentinel by a later property read.
    *(int64_t*)((char*)props->data + entry->byte_offset) = value;
    return true;
}

static void _refresh_live_child_collection(Item collection, DomElement* owner, int kind) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !owner) return;
    // live collection refresh owns the dense backing array; routing through
    // JS length assignment can leave companion-map collection properties stale.
    collection.array->length = 0;
    DomNode* child = js_dom_first_script_visible_child(owner);
    while (child) {
        if (kind == LIVE_CHILD_COLLECTION_CHILD_NODES || child->is_element()) {
            if (child->is_element()) {
                js_array_push(collection, js_dom_wrap_element(child->as_element()));
            } else {
                js_array_push(collection, js_dom_wrap_element((DomElement*)(void*)child));
            }
        }
        child = js_dom_next_script_visible_sibling(child);
    }
    if (js_array_has_props(collection.array)) {
        // decorated collections have a companion map; keep its length in sync
        // because array property reads consult it before the dense length, and
        // normal JS writes can be rejected once the slot is descriptor-backed.
        // A companion map can exist solely for namedItem()/constructor and
        // therefore have no length slot; only update an actual descriptor slot.
        _array_companion_set_int_slot(collection, "length", 6,
                                      collection.array->length);
    }
}

static void _refresh_live_form_collection(Item collection, LiveFormCollectionEntry* entry) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !entry) return;
    // live form collections own their dense array; setting length through JS
    // leaves old numeric slots and companion-map length visible to optimized reads.
    collection.array->length = 0;
    if (entry->kind == LIVE_FORM_COLLECTION_DOCUMENT_FORMS) {
        DomDocument* doc = entry->doc;
        if (doc && doc->root) {
            _collect_document_forms_rec((DomNode*)doc->root, collection);
        }
    } else if (entry->kind == LIVE_FORM_COLLECTION_FORM_ELEMENTS) {
        DomElement* form = entry->owner;
        if (form) {
            _collect_form_controls_rec(form->first_child, collection);
        }
    }
    if (js_array_has_props(collection.array)) {
        _array_companion_set_int_slot(collection, "length", 6,
                                      collection.array->length);
    }
}

static void _refresh_live_lookup_collection(Item collection, LiveLookupCollectionEntry* entry) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !entry) return;
    // lookup collections are live; refresh through JS array pushes because
    // decorated arrays use `extra` for companion properties, not Lambda extras.
    collection.array->length = 0;
    DomElement* root = entry->root ? entry->root : (entry->doc ? entry->doc->root : nullptr);
    const char* query = entry->query ? entry->query->chars : "";
    if (!root) return;
    if (entry->include_root) {
        _collect_lookup_rec(root, query, collection, entry->kind);
    } else {
        DomNode* child = root->first_child;
        while (child) {
            if (child->is_element()) {
                _collect_lookup_rec(child->as_element(), query, collection, entry->kind);
            }
            child = child->next_sibling;
        }
    }
    if (js_array_has_props(collection.array)) {
        _array_companion_set_int_slot(collection, "length", 6,
                                      collection.array->length);
    }
}

static Item _new_live_lookup_collection(DomDocument* doc, DomElement* root,
                                        int kind, bool include_root,
                                        Item query_item, const char* ctor_name) {
    // Collection registration allocates realm state and wraps matching nodes;
    // keep both the query and newly allocated array rooted across those GC
    // boundaries (S6.4, D5.2). Without this, the weak cache can retain a
    // reclaimed array while the refresh walk is still populating it.
    JS_ROOTS(roots,
        query_root, query_item,
        collection_root, js_array_new(0));
    const char* query = js_dom_to_dom_string_cstr(query_root.get());
    if (!query) return ItemNull;
    Item collection = collection_root.get();
    _register_live_lookup_collection(collection, doc, root, kind, include_root, query);
    LiveLookupCollectionEntry* entry = _live_lookup_collection_entry(collection);
    _refresh_live_lookup_collection(collection, entry);
    if (ctor_name) _decorate_dom_collection(collection, ctor_name);
    return collection_root.get();
}

extern "C" Item js_dom_live_child_collection_bridge(void* elem_ptr, bool elements_only) {
    DomElement* elem = (DomElement*)elem_ptr;
    JS_ROOTS(roots, collection_root, js_array_new(0));
    Item collection = collection_root.get();
    if (!elem) return collection;
    int kind = elements_only ? LIVE_CHILD_COLLECTION_CHILDREN : LIVE_CHILD_COLLECTION_CHILD_NODES;
    _register_live_child_collection(collection, elem, kind);
    _refresh_live_child_collection(collection, elem, kind);
    // DOM child collections are live; registering the owner lets the array
    // refresh before script reads instead of freezing a stale mutation snapshot.
    if (elements_only) _decorate_dom_collection(collection, "HTMLCollection");
    return collection_root.get();
}

extern "C" Item js_dom_live_document_forms_bridge(void* doc_ptr) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    JS_ROOTS(roots, collection_root, js_array_new(0));
    Item collection = collection_root.get();
    if (!doc) return collection;
    _register_live_form_collection(collection, doc, nullptr, LIVE_FORM_COLLECTION_DOCUMENT_FORMS);
    LiveFormCollectionEntry* entry = _live_form_collection_entry(collection);
    _refresh_live_form_collection(collection, entry);
    _decorate_dom_collection(collection, "HTMLCollection");
    return collection_root.get();
}

extern "C" Item js_dom_live_form_elements_bridge(void* elem_ptr) {
    DomElement* form = (DomElement*)elem_ptr;
    JS_ROOTS(roots, collection_root, js_array_new(0));
    Item collection = collection_root.get();
    if (!form) return collection;
    _register_live_form_collection(collection, form->doc, form, LIVE_FORM_COLLECTION_FORM_ELEMENTS);
    LiveFormCollectionEntry* entry = _live_form_collection_entry(collection);
    _refresh_live_form_collection(collection, entry);
    _decorate_dom_collection(collection, "HTMLFormControlsCollection");
    return collection_root.get();
}

#define JS_DOM_DOCUMENT_LOOKUP_BRIDGE(name, kind, ctor_name) \
extern "C" Item name(void* doc_ptr, Item query) { \
    DomDocument* doc = (DomDocument*)doc_ptr; \
    return _new_live_lookup_collection(doc, doc ? doc->root : nullptr, \
        kind, true, query, ctor_name); \
}
#define JS_DOM_ELEMENT_LOOKUP_BRIDGE(name, kind) \
extern "C" Item name(void* elem_ptr, Item query) { \
    DomElement* elem = (DomElement*)elem_ptr; \
    return _new_live_lookup_collection(elem ? elem->doc : nullptr, elem, \
        kind, false, query, "HTMLCollection"); \
}
JS_DOM_DOCUMENT_LOOKUP_BRIDGE(js_dom_live_document_get_elements_by_tag_name_bridge,
    LIVE_LOOKUP_COLLECTION_TAG, "HTMLCollection")
JS_DOM_DOCUMENT_LOOKUP_BRIDGE(js_dom_live_document_get_elements_by_class_name_bridge,
    LIVE_LOOKUP_COLLECTION_CLASS, "HTMLCollection")
JS_DOM_DOCUMENT_LOOKUP_BRIDGE(js_dom_live_document_get_elements_by_name_bridge,
    LIVE_LOOKUP_COLLECTION_NAME, nullptr)
JS_DOM_ELEMENT_LOOKUP_BRIDGE(js_dom_live_element_get_elements_by_tag_name_bridge,
    LIVE_LOOKUP_COLLECTION_TAG)
JS_DOM_ELEMENT_LOOKUP_BRIDGE(js_dom_live_element_get_elements_by_class_name_bridge,
    LIVE_LOOKUP_COLLECTION_CLASS)
#undef JS_DOM_ELEMENT_LOOKUP_BRIDGE
#undef JS_DOM_DOCUMENT_LOOKUP_BRIDGE

static void js_dom_refresh_live_child_collections_for_mutation(DomNode* target,
                                                               DomNode* parent) {
    if (!js_dom_collection_runtime_state_get()) return;
    for (int i = 0; i < s_live_child_collection_count; i++) {
        if (!s_live_child_collections[i].array) continue;
        DomElement* owner = s_live_child_collections[i].owner;
        if (!owner) continue;
        if ((DomNode*)owner != target && (DomNode*)owner != parent) continue;
        Item collection = (Item){.array = s_live_child_collections[i].array};
        _refresh_live_child_collection(collection, owner, s_live_child_collections[i].kind);
    }
}

#define JS_DOM_REFRESH_LIVE_DOCUMENT_COLLECTION(name, entries, count, EntryType, owner_doc_expr, refresh) \
static void name(DomNode* target, DomNode* parent, DomDocument* doc) { \
    if (!js_dom_collection_runtime_state_get()) return; \
    (void)target; \
    (void)parent; \
    for (int i = 0; i < count; i++) { \
        EntryType* entry = &entries[i]; \
        if (!entry->array) continue; \
        DomDocument* owner_doc = owner_doc_expr; \
        if (doc && owner_doc && owner_doc != doc) continue; \
        Item collection = (Item){.array = entry->array}; \
        refresh(collection, entry); \
    } \
}

JS_DOM_REFRESH_LIVE_DOCUMENT_COLLECTION(
    js_dom_refresh_live_form_collections_for_mutation,
    s_live_form_collections, s_live_form_collection_count,
    LiveFormCollectionEntry,
    entry->doc ? entry->doc : (entry->owner ? entry->owner->doc : nullptr),
    _refresh_live_form_collection)

static void js_dom_refresh_select_option_collections_for_mutation(DomNode* target,
                                                                  DomNode* parent,
                                                                  DomDocument* doc) {
    if (!js_dom_collection_runtime_state_get()) return;
    (void)target;
    (void)parent;
    for (int i = 0; i < s_select_options_owner_count; i++) {
        if (!s_select_options_owners[i].array) continue;
        DomElement* owner = s_select_options_owners[i].owner;
        if (!owner || !_is_tag(owner, "select")) continue;
        if (doc && owner->doc && owner->doc != doc) continue;
        Item collection = (Item){.array = s_select_options_owners[i].array};
        // optimized length/index reads can bypass the property hook, so
        // structural select changes refresh held option collections here too.
        if (s_select_options_owners[i].kind == SELECT_COLLECTION_OPTIONS) {
            _select_refresh_options_collection(collection, owner);
        } else if (s_select_options_owners[i].kind == SELECT_COLLECTION_SELECTED_OPTIONS) {
            _select_refresh_selected_options_collection(collection, owner);
        }
    }
}

JS_DOM_REFRESH_LIVE_DOCUMENT_COLLECTION(
    js_dom_refresh_live_lookup_collections_for_mutation,
    s_live_lookup_collections, s_live_lookup_collection_count,
    LiveLookupCollectionEntry,
    entry->doc ? entry->doc : (entry->root ? entry->root->doc : nullptr),
    _refresh_live_lookup_collection)

#undef JS_DOM_REFRESH_LIVE_DOCUMENT_COLLECTION

// ============================================================================
// Document Proxy Object
// ============================================================================

static DomDocument* js_document_proxy_doc_from_item(Item item) {
    TypeId tid = get_type_id(item);
    if (tid == LMD_TYPE_VMAP && item.vmap) {
        if (item.vmap->host_type == (const void*)&js_document_proxy_vmap_marker ||
            item.vmap->host_type == radiant_dom_document_host_type()) {
            DomDocument* doc = (DomDocument*)item.vmap->host_data;
            return doc ? doc : (_js_main_document ? _js_main_document : _js_current_document);
        }
        if (item.vmap->host_type == (const void*)&js_foreign_doc_vmap_marker ||
            item.vmap->host_type == radiant_dom_foreign_document_host_type()) {
            return (DomDocument*)item.vmap->host_data;
        }
        return nullptr;
    }
    return nullptr;
}

// Returns the DomDocument* if `item` is a foreign-doc wrapper, else null.
extern "C" void* js_get_foreign_doc(Item item) {
    TypeId tid = get_type_id(item);
    if (tid == LMD_TYPE_VMAP) {
        if (item.vmap &&
            (item.vmap->host_type == (const void*)&js_foreign_doc_vmap_marker ||
             item.vmap->host_type == radiant_dom_foreign_document_host_type())) {
            return item.vmap->host_data;
        }
        return nullptr;
    }
    return nullptr;
}

// Returns true if `item` is the document.implementation singleton.
extern "C" bool js_is_dom_implementation(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    return js_dom_implementation_item.item != 0 &&
        item.item == js_dom_implementation_item.item;
}

extern "C" Item js_get_document_object_value() {
    if (!dom_ensure_roots()) return ItemNull;
    TypeId proxy_type = get_type_id(js_document_proxy_item);
    if (proxy_type == LMD_TYPE_VMAP && js_document_proxy_item.vmap &&
        (js_document_proxy_item.vmap->host_type == (const void*)&js_document_proxy_vmap_marker ||
         js_document_proxy_item.vmap->host_type == radiant_dom_document_host_type())) {
        // Root-range cleanup clears an expired wrapper slot to zero; only a
        // live document VMap may be reused by the next context.
        js_document_proxy_item.vmap->host_data = _js_main_document;
        return js_document_proxy_item;
    }
    js_document_proxy_item = vmap_new();
    if (get_type_id(js_document_proxy_item) != LMD_TYPE_VMAP || !js_document_proxy_item.vmap) {
        return ItemNull;
    }
    // Document proxies are native VMaps; host_data keeps the browsing-context
    // document current without relying on map-kind compatibility shells.
    js_document_proxy_item.vmap->host_type = radiant_dom_document_host_type();
    js_document_proxy_item.vmap->host_data = _js_main_document;
    return js_document_proxy_item;
}

// Dispatch property access on the document proxy object.
JS_FORWARD_ITEM(js_document_proxy_get_property, (Item prop_name),
    js_document_get_property, (prop_name))

// Dispatch property set on the document proxy object.
// NOTE: Must use map_put directly instead of js_set_key_default to avoid
// infinite recursion (js_set_key_default dispatches back here for DOM resources).
extern "C" Item js_document_proxy_set_property(Item prop_name, Item value) {
    if (get_type_id(prop_name) == LMD_TYPE_STRING) {
        String* s = it2s(prop_name);
        if ((s && s->len == 4 && strncmp(s->chars, "href", 4) == 0) ||
            (s && s->len == 8 && strncmp(s->chars, "location", 8) == 0) ||
            (s && s->len == 4 && strncmp(s->chars, "hash", 4) == 0) ||
            (s && s->len == 6 && strncmp(s->chars, "search", 6) == 0) ||
            (s && s->len == 8 && strncmp(s->chars, "pathname", 8) == 0)) {
            // Location writes share the module-owned same-document history
            // machine; this prevents URL reflection and traversal from diverging.
            js_history_set_location(value);
            return value;
        }
        if (s && s->len == 5 && strncmp(s->chars, "title", 5) == 0) {
            // Store title as a static value (proxy map lacks TypeMap for map_put)
            js_document_title_value = value;
            return value;
        }
        // Allow setting defaultView (used by preamble: document.defaultView = window)
        if (s && s->len == 11 && strncmp(s->chars, "defaultView", 11) == 0) {
            js_document_default_view = value;
            return value;
        }
        if (s && s->len == 5 && strncmp(s->chars, "fonts", 5) == 0) {
            js_document_fonts_value = value;
            return value;
        }
        if (s && s->len == 10 && strncmp(s->chars, "designMode", 10) == 0) {
            const char* mode = js_dom_to_attr_cstr(value);
            js_document_design_mode = (mode && strcasecmp(mode, "on") == 0);
            return value;
        }
    }

    DomDocument* expando_doc = _js_current_document ? _js_current_document : _js_main_document;
    void* stub_v = js_dom_get_or_create_doc_node(expando_doc);
    if (!stub_v) return value;

    Item exp_map = expando_get_or_create_map((DomNode*)stub_v);
    if (exp_map.item == ITEM_NULL) return value;
    js_set_key_default(exp_map, prop_name, value);
    return value;
}

// ============================================================================
// Foreign Document Wrappers
// (document.implementation.createHTMLDocument / createDocument results)
// ============================================================================

// Wrap a DomDocument* as a foreign-doc Map for JS access.
// Cached so repeated calls for the same DomDocument* return the same wrapper
// (required for `===` identity comparisons).
static const int FOREIGN_DOC_CACHE_SIZE = 16;
struct ForeignDocCacheEntry {
    DomDocument* doc;
    uint64_t item;
    bool owns_doc;
};

// iframe element -> foreign DomDocument* (lazy created on first access).
struct IframeContentEntry {
    DomElement* iframe;
    DomDocument* doc;
    bool owns_doc;
};
static const int IFRAME_CACHE_SIZE = 32;

// Side table: documents that have a non-null defaultView (browsing context).
// Main doc and iframe content docs go here. Foreign docs from
// document.implementation.create*Document do not.
static const int DOC_WIN_TABLE_SIZE = 32;
// Wrapper identity and foreign-document ownership are document-realm state.
// Calls below use this direct capsule after their normal JS/host entry bind.
struct JsDomForeignDocumentRuntimeState {
    ForeignDocCacheEntry foreign_doc_cache[FOREIGN_DOC_CACHE_SIZE] = {};
    int foreign_doc_cache_count = 0;
    DomDocument* doc_with_window[DOC_WIN_TABLE_SIZE] = {};
    int doc_with_window_count = 0;
    IframeContentEntry iframe_cache[IFRAME_CACHE_SIZE] = {};
    int iframe_cache_count = 0;
    DomElement* pending_iframe_loads[16] = {};
    DomNodeRef pending_iframe_refs[16] = {};
    DomDocument* pending_iframe_docs[16] = {};
    int pending_iframe_load_count = 0;
    bool iframe_load_drain_scheduled = false;
};

JS_FORWARD_STATIC_EXPRESSION(JsDomForeignDocumentRuntimeState*,
    js_dom_foreign_document_state_get, (),
    (js_active_runtime_state ? (JsDomForeignDocumentRuntimeState*)js_runtime_state.dom_foreign_document_state : nullptr))
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_foreign_document_state_ensure, (), (js_dom_runtime_state_ensure<JsDomForeignDocumentRuntimeState>(js_runtime_state.dom_foreign_document_state, "js-dom-foreign-document") != nullptr))

#define js_dom_foreign_document_state ((JsDomForeignDocumentRuntimeState*)js_runtime_state.dom_foreign_document_state)
#define s_foreign_doc_cache (js_dom_foreign_document_state->foreign_doc_cache)
#define s_foreign_doc_cache_count (js_dom_foreign_document_state->foreign_doc_cache_count)
#define s_doc_with_window (js_dom_foreign_document_state->doc_with_window)
#define s_doc_with_window_count (js_dom_foreign_document_state->doc_with_window_count)
#define s_iframe_cache (js_dom_foreign_document_state->iframe_cache)
#define s_iframe_cache_count (js_dom_foreign_document_state->iframe_cache_count)
#define s_pending_iframe_loads (js_dom_foreign_document_state->pending_iframe_loads)
#define s_pending_iframe_refs (js_dom_foreign_document_state->pending_iframe_refs)
#define s_pending_iframe_docs (js_dom_foreign_document_state->pending_iframe_docs)
#define s_pending_iframe_load_count (js_dom_foreign_document_state->pending_iframe_load_count)
#define s_iframe_load_drain_scheduled (js_dom_foreign_document_state->iframe_load_drain_scheduled)

extern "C" bool js_doc_has_browsing_context(void* doc) {
    if (!doc || !js_dom_foreign_document_state_get()) return false;
    for (int i = 0; i < s_doc_with_window_count; i++) {
        if (s_doc_with_window[i] == (DomDocument*)doc) return true;
    }
    return false;
}
extern "C" void js_doc_mark_has_browsing_context(void* doc) {
    if (!doc || !js_dom_foreign_document_state_ensure()) return;
    if (js_doc_has_browsing_context(doc)) return;
    if (s_doc_with_window_count < DOC_WIN_TABLE_SIZE) {
        s_doc_with_window[s_doc_with_window_count++] = (DomDocument*)doc;
    }
}

static IframeContentEntry* lookup_iframe_entry(DomElement* iframe) {
    if (!js_dom_foreign_document_state_get()) return nullptr;
    for (int i = 0; i < s_iframe_cache_count; i++) {
        if (s_iframe_cache[i].iframe == iframe) return &s_iframe_cache[i];
    }
    return NULL;
}

static bool doc_already_destroyed(DomDocument** docs, int doc_count, DomDocument* doc) {
    if (!doc) return true;
    for (int i = 0; i < doc_count; i++) {
        if (docs[i] == doc) return true;
    }
    return false;
}

static void destroy_cached_doc_once(DomDocument** docs, int* doc_count, DomDocument* doc) {
    if (!doc || doc == _js_main_document || doc_already_destroyed(docs, *doc_count, doc)) {
        return;
    }
    if (*doc_count < FOREIGN_DOC_CACHE_SIZE + IFRAME_CACHE_SIZE) {
        docs[*doc_count] = doc;
        (*doc_count)++;
    }
    free_document(doc);
}

static void reset_foreign_document_cache() {
    if (!js_dom_foreign_document_state_get()) return;
    DomDocument* destroyed_docs[FOREIGN_DOC_CACHE_SIZE + IFRAME_CACHE_SIZE] = {};
    int destroyed_doc_count = 0;

    for (int i = 0; i < s_foreign_doc_cache_count; i++) {
        heap_unregister_gc_root(&s_foreign_doc_cache[i].item);
        if (s_foreign_doc_cache[i].owns_doc) {
            destroy_cached_doc_once(
                destroyed_docs, &destroyed_doc_count, s_foreign_doc_cache[i].doc);
        }
        s_foreign_doc_cache[i].doc = nullptr;
        s_foreign_doc_cache[i].item = 0;
        s_foreign_doc_cache[i].owns_doc = false;
    }
    s_foreign_doc_cache_count = 0;

    for (int i = 0; i < s_iframe_cache_count; i++) {
        if (s_iframe_cache[i].owns_doc) {
            destroy_cached_doc_once(
                destroyed_docs, &destroyed_doc_count, s_iframe_cache[i].doc);
        }
        s_iframe_cache[i].iframe = nullptr;
        s_iframe_cache[i].doc = nullptr;
        s_iframe_cache[i].owns_doc = false;
    }
    s_iframe_cache_count = 0;

    for (int i = 0; i < s_doc_with_window_count; i++) {
        s_doc_with_window[i] = nullptr;
    }
    s_doc_with_window_count = 0;
}
JS_FORWARD_STATIC_VOID( js_dom_destroy_adopted_document, (void* data), free_document, ((DomDocument*)data))

static bool js_dom_transfer_document_storage(DomDocument* source,
                                             DomDocument* destination) {
    if (!js_dom_foreign_document_state_get()) return true;
    if (!source || !destination || source == destination ||
        source == _js_main_document) {
        return true;
    }

    ForeignDocCacheEntry* foreign_owner = nullptr;
    IframeContentEntry* iframe_owner = nullptr;
    for (int i = 0; i < s_foreign_doc_cache_count; i++) {
        if (s_foreign_doc_cache[i].doc == source &&
            s_foreign_doc_cache[i].owns_doc) {
            foreign_owner = &s_foreign_doc_cache[i];
            break;
        }
    }
    for (int i = 0; i < s_iframe_cache_count; i++) {
        if (s_iframe_cache[i].doc == source && s_iframe_cache[i].owns_doc) {
            iframe_owner = &s_iframe_cache[i];
            break;
        }
    }
    if (!foreign_owner && !iframe_owner) return true;

    if (!dom_document_add_resource(destination, source,
                                   js_dom_destroy_adopted_document)) {
        log_error("js_dom_adopt: failed to retain source document storage");
        return false;
    }
    if (foreign_owner) foreign_owner->owns_doc = false;
    if (iframe_owner) iframe_owner->owns_doc = false;
    return true;
}

static bool js_dom_rebind_subtree_document(DomNode* node,
                                           DomDocument* source,
                                           DomDocument* destination) {
    if (!node || !source || !destination) return false;
    uint32_t destination_id = destination->next_node_id++;
    if (!dom_node_registry_transfer(source, destination, node, destination_id)) {
        return false;
    }
    node->id = destination_id;
    node->view_state_ref = nullptr;
    if (!node->is_element()) return true;

    DomElement* elem = node->as_element();
    elem->doc = destination;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!js_dom_rebind_subtree_document(child, source, destination)) return false;
    }
    if (elem->shadow_root_element()) {
        if (!js_dom_rebind_subtree_document(
                (DomNode*)elem->shadow_root_element(), source, destination)) return false;
    }
    return true;
}

static bool js_dom_prepare_cross_document_insertion(DomNode* node,
                                                    DomElement* parent) {
    if (!node || !parent || !parent->doc) return false;
    DomDocument* source = js_dom_node_owner_document(node);
    DomDocument* destination = parent->doc;
    if (!source) return true;
    bool cross_document = source != destination;
    if (cross_document &&
        !js_dom_transfer_document_storage(source, destination)) return false;
    // Detach while the source document and lifecycle ids are still current;
    // rebinding first makes removal pins target two incompatible registries.
    if (node->parent) {
        dom_pre_remove(node);
        node->parent->remove_child(node);
    }
    if (cross_document &&
        !js_dom_rebind_subtree_document(node, source, destination)) return false;
    return true;
}

static Item wrap_foreign_doc_owned(DomDocument* doc, bool owns_doc) {
    if (!js_dom_foreign_document_state_ensure()) return ItemNull;
    // Look up cache first.
    for (int i = 0; i < s_foreign_doc_cache_count; i++) {
        if (s_foreign_doc_cache[i].doc == doc) {
            if (!owns_doc) s_foreign_doc_cache[i].owns_doc = false;
            return (Item){.item = s_foreign_doc_cache[i].item};
        }
    }
    Item it = vmap_new();
    if (get_type_id(it) != LMD_TYPE_VMAP || !it.vmap) return ItemNull;
    // Foreign document wrappers are native VMaps; the cached host_data is the
    // document selected during active-document swaps.
    it.vmap->host_type = radiant_dom_foreign_document_host_type();
    it.vmap->host_data = doc;
    if (s_foreign_doc_cache_count < FOREIGN_DOC_CACHE_SIZE) {
        s_foreign_doc_cache[s_foreign_doc_cache_count].doc = doc;
        s_foreign_doc_cache[s_foreign_doc_cache_count].item = it.item;
        s_foreign_doc_cache[s_foreign_doc_cache_count].owns_doc = owns_doc;
        heap_register_gc_root(&s_foreign_doc_cache[s_foreign_doc_cache_count].item);
        s_foreign_doc_cache_count++;
    }
    return it;
}
JS_FORWARD_STATIC_ITEM(wrap_foreign_doc, (DomDocument* doc), wrap_foreign_doc_owned, (doc, true))

// Returns the foreign-doc wrapper for `doc` if one exists, else ItemNull.
static Item lookup_foreign_doc_wrapper(DomDocument* doc) {
    if (!js_dom_foreign_document_state_get()) return ItemNull;
    for (int i = 0; i < s_foreign_doc_cache_count; i++) {
        if (s_foreign_doc_cache[i].doc == doc) {
            return (Item){.item = s_foreign_doc_cache[i].item};
        }
    }
    return ItemNull;
}

static Item js_dom_owner_document_from_node(DomNode* node) {
    DomNode* element_node = node;
    while (element_node && !element_node->is_element()) element_node = element_node->parent;
    if (element_node) {
        DomDocument* owner = element_node->as_element()->doc;
        if (owner && owner != _js_current_document) {
            Item wrapper = lookup_foreign_doc_wrapper(owner);
            if (wrapper.item != ITEM_NULL) return wrapper;
        }
    }
    return js_get_document_object_value();
}

static Item js_dom_parent_element_or_null(DomNode* node) {
    DomNode* parent = node ? node->parent : nullptr;
    if (parent && parent->is_element()) {
        DomElement* elem = parent->as_element();
        // Document and fragment nodes reuse DomElement storage but cannot be
        // exposed through the Element-only parentElement relation.
        if (elem && elem->tag_name && elem->tag_name[0] != '#') {
            return js_dom_wrap_element(elem);
        }
    }
    return ItemNull;
}

static Item js_dom_parent_node_or_null(DomNode* node) {
    DomNode* parent = node ? node->parent : nullptr;
    return parent ? js_dom_wrap_element((void*)parent) : ItemNull;
}
JS_FORWARD_ITEM(js_dom_owner_document_for_node, (void* node_ptr), js_dom_owner_document_from_node, ((DomNode*)node_ptr))

// Build a minimal HTML document tree:
//   <html>
//     <head><title>$title</title></head>
//     <body></body>
//   </html>
// The doc shares the current document's Input* so MarkBuilder can allocate
// Lambda Element backing objects from the same pool.
static DomDocument* create_foreign_html_doc(const char* title) {
    Input* input = _js_current_document ? _js_current_document->input : nullptr;
    if (!input) {
        log_error("create_foreign_html_doc: no current document Input available");
        return nullptr;
    }
    DomDocument* fd = dom_document_create(input);
    if (!fd) return nullptr;

    // Build the html/head/title/body tree using MarkBuilder for the Lambda
    // Element backings, then wrap each in a DomElement bound to the foreign doc.
    auto build_dom_elem = [&](const char* tag, Element*& out_elem) -> DomElement* {
        MarkBuilder builder(input);
        Item item = builder.element(tag).final();
        out_elem = item.element;
        return dom_element_create(fd, tag, out_elem);
    };

    Element* html_e = nullptr;
    DomElement* html_dom = build_dom_elem("html", html_e);
    if (!html_dom) return fd;

    Element* head_e = nullptr;
    DomElement* head_dom = build_dom_elem("head", head_e);

    Element* body_e = nullptr;
    DomElement* body_dom = build_dom_elem("body", body_e);

    Element* title_e = nullptr;
    DomElement* title_dom = build_dom_elem("title", title_e);

    if (head_dom && title_dom) {
        head_dom->append_child(title_dom);
        if (title && *title) {
            DomText* tnode = DomText::create_copy(title, strlen(title), title_dom);
            if (tnode) title_dom->append_child(tnode);
        }
    }
    if (html_dom && head_dom) html_dom->append_child(head_dom);
    if (html_dom && body_dom) html_dom->append_child(body_dom);
    fd->root = html_dom;
    return fd;
}

static DomElement* document_body_element(DomDocument* doc) {
    if (!doc || !doc->root) return nullptr;
    DomNode* child = doc->root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* el = child->as_element();
            if (el->tag_name && strcmp(el->tag_name, "body") == 0) {
                return el;
            }
        }
        child = child->next_sibling;
    }
    return nullptr;
}

static void clear_element_children_for_navigation(DomElement* elem) {
    if (!elem) return;
    while (elem->first_child) {
        DomNode* child = elem->first_child;
        dom_pre_remove(child);
        elem->remove_child(child);
    }
}

static void append_iframe_srcdoc_to_document(DomElement* iframe,
                                             DomDocument* doc) {
    if (!iframe || !doc || !doc->root) return;
    const char* srcdoc = iframe->get_attribute("srcdoc");
    if (!srcdoc || !*srcdoc) return;
    DomElement* body = document_body_element(doc);
    if (!body || !doc->document_pool || !doc->node_arena || !doc->input) return;

    Html5Parser* parser = html5_fragment_parser_create(
        doc->document_pool, doc->node_arena, doc->input);
    if (!parser) return;
    html5_fragment_parse(parser, srcdoc);
    Element* body_elem = html5_fragment_get_body(parser);
    if (!body_elem) return;
    for (int64_t i = 0; i < body_elem->length; i++) {
        TypeId t = get_type_id(body_elem->items[i]);
        if (t == LMD_TYPE_ELEMENT) {
            build_dom_tree_from_element(body_elem->items[i].element,
                                        doc, body);
        } else if (t == LMD_TYPE_STRING) {
            String* s = js_dom_fragment_text(body_elem->items[i]);
            if (!s) continue;
            DomText* tn = dom_text_create(s, body);
            if (tn) {
                tn->parent = body;
                if (!body->first_child) {
                    body->first_child = tn;
                    body->last_child = tn;
                } else {
                    DomNode* last = body->last_child;
                    last->next_sibling = tn;
                    tn->prev_sibling = last;
                    body->last_child = tn;
                }
            }
        }
    }
}

static void replace_iframe_srcdoc_document(DomElement* iframe,
                                            DomDocument* doc) {
    if (!iframe || !doc) return;
    // An embedded document loaded through src must remain intact; only the
    // presence of the srcdoc attribute selects the replacement navigation.
    if (!iframe->get_attribute("srcdoc")) return;
    DomElement* body = document_body_element(doc);
    if (!body) return;
    // Parsed iframes can already own an empty embedded document before a
    // later srcdoc assignment; appending without replacing left queries in
    // that browsing context pointed at the stale document body.
    clear_element_children_for_navigation(body);
    append_iframe_srcdoc_to_document(iframe, doc);
}

// Public: create a foreign HTML document, return wrapped Item.
extern "C" Item js_create_foreign_html_doc(const char* title) {
    DomDocument* fd = create_foreign_html_doc(title ? title : "");
    if (!fd) return ItemNull;
    return wrap_foreign_doc(fd);
}

// native construction must retain the receiver carrying DOMParser's
// prototype; returning another object would discard parseFromString.
JS_FORWARD_STATIC_ITEM(js_dom_parser_constructor, (void), make_js_undefined, ())

static Element* js_dom_parser_xml_document_element(Input* input) {
    if (!input || get_type_id(input->root) != LMD_TYPE_ELEMENT) return nullptr;
    Element* wrapper = input->root.element;
    if (!wrapper) return nullptr;

    Element* document_element = nullptr;
    for (int64_t i = 0; i < wrapper->length; i++) {
        Item child = wrapper->items[i];
        if (get_type_id(child) != LMD_TYPE_ELEMENT || !child.element ||
            !child.element->type) {
            continue;
        }
        TypeElmt* type = (TypeElmt*)child.element->type;
        const char* tag_name = type->name.str;
        if (!tag_name || tag_name[0] == '?' || tag_name[0] == '!' ||
            strncmp(tag_name, "!--", 3) == 0) {
            continue;
        }
        // XML documents have one document element. Rejecting multiple roots
        // here makes DOMParser expose parsererror instead of silently turning
        // malformed markup into an arbitrary DOM subtree.
        if (document_element) return nullptr;
        document_element = child.element;
    }
    return document_element;
}

// DOMParser reports XML parse failures through a document containing a
// parsererror element, which lets callers use normal DOM inspection.
JS_FORWARD_STATIC_ITEM(js_dom_parser_error_document, (void),
    js_create_foreign_xml_doc, ("parsererror"))

static Item js_dom_parser_parse_xml(const char* source) {
    Input* parent_input = _js_current_document ? _js_current_document->input : nullptr;
    if (!parent_input || !parent_input->pool) return ItemNull;

    Input* xml_input = Input::create(parent_input->pool, nullptr, parent_input);
    if (!xml_input) return ItemNull;
    parse_xml(xml_input, source ? source : "");

    Element* xml_root = js_dom_parser_xml_document_element(xml_input);
    if (!xml_root) return js_dom_parser_error_document();

    DomDocument* xml_document = dom_document_create(xml_input);
    if (!xml_document) return ItemNull;
    DomElement* dom_root = build_dom_tree_from_element(xml_root, xml_document, nullptr);
    if (!dom_root) {
        free_document(xml_document);
        return js_dom_parser_error_document();
    }
    xml_document->root = dom_root;
    return wrap_foreign_doc(xml_document);
}

static Item js_dom_parser_parse_from_string(Item source_item, Item type_item) {
    const char* source = fn_to_cstr(source_item);
    const char* type = fn_to_cstr(type_item);
    if (!source) source = "";
    if (!type) type = "text/html";
    if (strcasecmp(type, "text/html") != 0) {
        if (strcasecmp(type, "text/xml") == 0 ||
            strcasecmp(type, "application/xml") == 0 ||
            strcasecmp(type, "image/svg+xml") == 0 ||
            strcasecmp(type, "application/xhtml+xml") == 0) {
            return js_dom_parser_parse_xml(source);
        }
        return js_throw_type_error("Unsupported DOMParser MIME type");
    }

    Item parsed = js_create_foreign_html_doc("");
    if (parsed.item == ITEM_NULL) return ItemNull;
    Item body = js_get_key_cstr(parsed, "body");
    if (body.item == ITEM_NULL || is_js_undefined(body)) return parsed;
    // Reuse the element innerHTML path so detached parsed documents preserve
    // the same node ownership and wrapper identity invariants as live DOM.
    js_set_key_cstr(body, "innerHTML", source_item);
    return parsed;
}

template <typename Method>
static void js_dom_install_native_constructor_global(const char* ctor_name,
        JsNativeP0 ctor_target, const char* method_name, Method method_target) {
    JS_ROOTS(roots,
        global_root, js_get_global_this(),
        ctor_root, js_new_native_constructor(ctor_target),
        proto_root, js_new_object(),
        method_root, js_new_native_function(method_target));
    js_set_function_name(ctor_root.get(), js_name_item(ctor_name));
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_set_key_default(proto_root.get(), js_string_key(method_name),
        method_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    js_set_key_default(global_root.get(), js_string_key(ctor_name),
        ctor_root.get());
}

static void js_dom_install_dom_parser_global(void) {
    // D6.2.2v2: the prototype owns the parse capability, so construction and
    // method publication share one rooted native-constructor transaction.
    js_dom_install_native_constructor_global("DOMParser", js_dom_parser_constructor,
        "parseFromString", js_dom_parser_parse_from_string);
}

// iframe.contentDocument / contentWindow accessors.
// Both currently return the same wrapped foreign HTML document. The foreign
// doc is marked as having a browsing context so its defaultView/getSelection
// resolve normally. js_document_get_property maps "document" / "defaultView"
// back to the same wrapper so identity comparisons hold.
extern "C" Item js_iframe_get_content_document(DomElement* iframe) {
    if (!iframe || !js_dom_foreign_document_state_ensure()) return ItemNull;
    IframeContentEntry* e = lookup_iframe_entry(iframe);
    if (!e) {
        DomDocument* embedded_doc = iframe->embed ? iframe->embedp()->doc : nullptr;
        if (embedded_doc) {
            js_doc_mark_has_browsing_context(embedded_doc);
            replace_iframe_srcdoc_document(iframe, embedded_doc);
            if (s_iframe_cache_count < IFRAME_CACHE_SIZE) {
                s_iframe_cache[s_iframe_cache_count].iframe = iframe;
                s_iframe_cache[s_iframe_cache_count].doc = embedded_doc;
                s_iframe_cache[s_iframe_cache_count].owns_doc = false;
                s_iframe_cache_count++;
            }
            return wrap_foreign_doc_owned(embedded_doc, false);
        }
        DomDocument* doc = create_foreign_html_doc("");
        if (!doc) return ItemNull;
        js_doc_mark_has_browsing_context(doc);
        // Hydrate the iframe document from srcdoc so DOM queries resolve
        // inside the browsing context.
        replace_iframe_srcdoc_document(iframe, doc);
        if (s_iframe_cache_count < IFRAME_CACHE_SIZE) {
            s_iframe_cache[s_iframe_cache_count].iframe = iframe;
            s_iframe_cache[s_iframe_cache_count].doc = doc;
            s_iframe_cache[s_iframe_cache_count].owns_doc = true;
            s_iframe_cache_count++;
        }
        return wrap_foreign_doc(doc);
    }
    return wrap_foreign_doc_owned(e->doc, e->owns_doc);
}
JS_FORWARD_ITEM(js_iframe_get_content_window, (DomElement* iframe), js_iframe_get_content_document, (iframe))

static void js_dom_collect_frame_windows(DomElement* elem, Item frames) {
    if (!elem) return;
    if (elem->tag_name && strcmp(elem->tag_name, "iframe") == 0) {
        js_array_push(frames, js_iframe_get_content_window(elem));
    }
    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_element()) {
            js_dom_collect_frame_windows(child->as_element(), frames);
        }
        child = child->next_sibling;
    }
}

static void js_dom_install_window_frames_global(void) {
    Item frames = js_array_new(0);
    DomDocument* doc = _js_main_document ? _js_main_document : _js_current_document;
    if (doc && doc->root) {
        js_dom_collect_frame_windows(doc->root, frames);
    }
    int64_t length = js_array_length(frames);
    Item length_item = (Item){.item = i2it(length)};
    Item global = js_get_global_this();
    js_set_key_cstr(global, "frames", frames);
    js_set_key_cstr(global, "length", length_item);

    Item window = js_get_key_cstr(global, "window");
    if (get_type_id(window) == LMD_TYPE_MAP) {
        js_set_key_cstr(window, "frames", frames);
        js_set_key_cstr(window, "length", length_item);
    }
}

// ---------------------------------------------------------------------------
// Stage 4C Phase B: window.prompt() with a harness-settable response queue.
// Headless `lambda.exe view` has no dialog UI, so event_sim seeds answers via
// js_window_dialog_push_response() (the `set_prompt` event); each prompt() call
// dequeues one. A NULL response models pressing Cancel → JS null. This lets
// script editors that gate on window.prompt (link URL, mention name) run
// end-to-end. FIFO; context-owned with the document realm.
// ---------------------------------------------------------------------------
#define s_prompt_queue (js_runtime_state.dom.prompt_queue)
#define s_prompt_head (js_runtime_state.dom.prompt_head)
#define s_prompt_tail (js_runtime_state.dom.prompt_tail)

extern "C" void js_window_dialog_push_response(const char* value) {
    if (!js_active_runtime_state) return;
    int next = (s_prompt_tail + 1) % JS_DOM_PROMPT_QUEUE_CAP;
    if (next == s_prompt_head) return;  // queue full — drop
    s_prompt_queue[s_prompt_tail] = value ? mem_strdup(value, MEM_CAT_JS_RUNTIME) : NULL;
    s_prompt_tail = next;
}

extern "C" void js_window_dialog_reset(void) {
    if (!js_active_runtime_state) return;
    while (s_prompt_head != s_prompt_tail) {
        if (s_prompt_queue[s_prompt_head]) mem_free(s_prompt_queue[s_prompt_head]);
        s_prompt_head = (s_prompt_head + 1) % JS_DOM_PROMPT_QUEUE_CAP;
    }
    s_prompt_head = 0;
    s_prompt_tail = 0;
}

// window.prompt(message, default) — positional args (unused); returns the next
// seeded response as a JS string, or null (empty queue / seeded Cancel).
static Item js_window_prompt(Item message_item, Item default_item) {
    (void)message_item; (void)default_item;
    if (!js_active_runtime_state) return ItemNull;
    if (s_prompt_head == s_prompt_tail) return ItemNull;
    char* r = s_prompt_queue[s_prompt_head];
    s_prompt_head = (s_prompt_head + 1) % JS_DOM_PROMPT_QUEUE_CAP;
    if (!r) return ItemNull;  // seeded Cancel
    Item out = js_name_item(r);
    mem_free(r);
    return out;
}

#undef s_prompt_queue
#undef s_prompt_head
#undef s_prompt_tail

static void js_dom_install_window_dialog_globals(void) {
    Item global = js_get_global_this();
    Item fn = js_new_native_function(js_window_prompt);
    js_set_key_cstr(global, "prompt", fn);
    Item window = js_get_key_cstr(global, "window");
    if (get_type_id(window) == LMD_TYPE_MAP) {
        js_set_key_cstr(window, "prompt", fn);
    }
}

// ----------------------------------------------------------------------------
// Iframe `load` event synthesis. After an <iframe> is inserted into the
// document tree, the HTML spec requires firing a `load` event on it once
// its (possibly blank) document is loaded. WPT tests like Document-open.html
// gate their async work on `iframe.onload`. We schedule a setTimeout(0)
// drain that fires `load` on each pending iframe in insertion order.
// ----------------------------------------------------------------------------
static Item _iframe_load_drain(Item this_val, Item* args, int argc) {
    (void)this_val; (void)args; (void)argc;
    if (!js_dom_foreign_document_state_get()) return ItemNull;
    int n = s_pending_iframe_load_count;
    s_pending_iframe_load_count = 0;
    s_iframe_load_drain_scheduled = false;
    DomDocument* sweep_docs[16] = {};
    int sweep_doc_count = 0;
    for (int i = 0; i < n; i++) {
        DomElement* ifr = s_pending_iframe_loads[i];
        s_pending_iframe_loads[i] = nullptr;
        DomDocument* owner_doc = s_pending_iframe_docs[i];
        DomNodeRef ref = s_pending_iframe_refs[i];
        s_pending_iframe_docs[i] = nullptr;
        s_pending_iframe_refs[i] = {nullptr, 0};
        if (!ifr) continue;
        Item ev = js_create_event("load", /*bubbles=*/false, /*cancelable=*/false);
        js_dom_dispatch_event(js_dom_wrap_element(ifr), ev);
        dom_node_unpin(owner_doc, ref, DOM_NODE_PIN_EVENT_QUEUE);
        bool known = false;
        for (int d = 0; d < sweep_doc_count; d++) {
            if (sweep_docs[d] == owner_doc) {
                known = true;
                break;
            }
        }
        if (owner_doc && !known) sweep_docs[sweep_doc_count++] = owner_doc;
    }
    for (int i = 0; i < sweep_doc_count; i++) dom_retire_sweep(sweep_docs[i]);
    return ItemNull;
}

static void _schedule_iframe_load(DomElement* iframe) {
    if (!iframe || !js_dom_foreign_document_state_ensure()) return;
    for (int i = 0; i < s_pending_iframe_load_count; i++) {
        if (s_pending_iframe_loads[i] == iframe) return;
    }
    if (s_pending_iframe_load_count >= 16) return;
    DomNodeRef ref = dom_node_ref((DomNode*)iframe);
    if (!iframe->doc || !dom_node_ref_validate(iframe->doc, ref) ||
        !dom_node_pin(iframe->doc, ref, DOM_NODE_PIN_EVENT_QUEUE)) return;
    int pending_index = s_pending_iframe_load_count++;
    s_pending_iframe_loads[pending_index] = iframe;
    s_pending_iframe_refs[pending_index] = ref;
    s_pending_iframe_docs[pending_index] = iframe->doc;
    if (!s_iframe_load_drain_scheduled) {
        s_iframe_load_drain_scheduled = true;
        Item cb = js_new_native_this_span_function(_iframe_load_drain);
        js_setTimeout(cb, (Item){.item = i2it(0)});
    }
}

static void reset_pending_iframe_loads() {
    if (!js_dom_foreign_document_state_get()) return;
    for (int i = 0; i < s_pending_iframe_load_count; i++) {
        if (s_pending_iframe_docs[i] && s_pending_iframe_refs[i].address) {
            dom_node_unpin(s_pending_iframe_docs[i], s_pending_iframe_refs[i],
                           DOM_NODE_PIN_EVENT_QUEUE);
        }
    }
    memset(s_pending_iframe_loads, 0, sizeof(s_pending_iframe_loads));
    memset(s_pending_iframe_refs, 0, sizeof(s_pending_iframe_refs));
    memset(s_pending_iframe_docs, 0, sizeof(s_pending_iframe_docs));
    s_pending_iframe_load_count = 0;
    s_iframe_load_drain_scheduled = false;
}

extern "C" void js_dom_after_srcdoc_set(void* dom_elem) {
    // srcdoc writes mutate an iframe's nested document asynchronously.
    DomElement* iframe = (DomElement*)dom_elem;
    IframeContentEntry* entry = lookup_iframe_entry(iframe);
    DomDocument* doc = entry ? entry->doc
        : (iframe && iframe->embed ? iframe->embedp()->doc : nullptr);
    if (doc) replace_iframe_srcdoc_document(iframe, doc);
    _schedule_iframe_load(iframe);
}

static DomElement* js_dom_find_iframe_by_name(DomNode* node, const char* target_name) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->tag_name && strcasecmp(elem->tag_name, "iframe") == 0) {
                const char* name = elem->get_attribute("name");
                if (name && strcmp(name, target_name) == 0) {
                    return elem;
                }
            }
            DomElement* found = js_dom_find_iframe_by_name(elem->first_child, target_name);
            if (found) return found;
        }
        node = node->next_sibling;
    }
    return nullptr;
}

extern "C" bool js_dom_navigate_submit_target(const char* target_name, const char* url) {
    DomDocument* doc = _js_current_document ? _js_current_document : _js_main_document;
    if (!doc || !url || !url[0]) return false;

    Url* resolved = doc->url ? url_parse_with_base(url, doc->url) : url_parse(url);
    if (!resolved || !url_is_valid(resolved)) {
        if (resolved) url_destroy(resolved);
        resolved = js_dom_make_fallback_url(url);
        if (!resolved) return false;
    }

    if (!target_name || !target_name[0] || strcmp(target_name, "_self") == 0) {
        if (doc->url) url_destroy(doc->url);
        doc->url = resolved;
        return true;
    }

    if (!doc->root) {
        url_destroy(resolved);
        return false;
    }

    DomElement* iframe = js_dom_find_iframe_by_name((DomNode*)doc->root, target_name);
    if (!iframe) {
        url_destroy(resolved);
        return false;
    }

    Item frame_doc_item = js_iframe_get_content_document(iframe);
    DomDocument* frame_doc = (DomDocument*)js_get_foreign_doc(frame_doc_item);
    if (!frame_doc) {
        url_destroy(resolved);
        return false;
    }

    if (frame_doc->url) url_destroy(frame_doc->url);
    frame_doc->url = resolved;
    _schedule_iframe_load(iframe);
    return true;
}

// ----------------------------------------------------------------------------
// Phase 8C: `new Image(width?, height?)` constructor.
// Creates an HTMLImageElement (`<img>`) parented to the current document, sets
// its `width`/`height` attributes from the (optional) constructor args, and
// returns the wrapped element. The element is NOT inserted into the DOM tree;
// the caller (script) does that explicitly via appendChild/insertBefore.
// ----------------------------------------------------------------------------
extern "C" Item js_image_construct(Item width_arg, Item height_arg, int argc) {
    DomDocument* doc = _js_current_document;
    if (!doc) return ItemNull;
    MarkBuilder builder(doc->input);
    Item elem_item = builder.element("img").final();
    Element* elem = elem_item.element;
    DomElement* dom_elem = dom_element_create(doc, "img", elem);
    if (!dom_elem) return ItemNull;
    if (argc >= 1) {
        const char* w = fn_to_cstr(width_arg);
        if (w) dom_elem->set_attribute("width", w);
    }
    if (argc >= 2) {
        const char* h = fn_to_cstr(height_arg);
        if (h) dom_elem->set_attribute("height", h);
    }
    return js_dom_wrap_element(dom_elem);
}

// Public: create an empty foreign XML/generic document. qualified_name may be
// null (no document element) or a tag to use as the root.
extern "C" Item js_create_foreign_xml_doc(const char* qualified_name) {
    Input* input = _js_current_document ? _js_current_document->input : nullptr;
    if (!input) return ItemNull;
    DomDocument* fd = dom_document_create(input);
    if (!fd) return ItemNull;
    if (qualified_name && *qualified_name) {
        MarkBuilder builder(input);
        Item item = builder.element(qualified_name).final();
        Element* e = item.element;
        DomElement* root = dom_element_create(fd, qualified_name, e);
        fd->root = root;
    }
    return wrap_foreign_doc(fd);
}

// Public: create a DocumentType node. Doctype shares the DomComment storage
// shape, but carries DOM_NODE_DOCTYPE so Range/Selection validation can reject
// it by node type instead of tag-name heuristics.
extern "C" Item js_create_doctype_node(const char* name,
                                        const char* public_id,
                                        const char* system_id) {
    DomDocument* doc = _js_current_document;
    if (!doc) return ItemNull;
    MarkBuilder builder(doc->input);
    Item item = builder.element("!DOCTYPE").text(name ? name : "").final();
    Element* e = item.element;
    DomComment* dt = dom_comment_create_detached(e, doc);
    if (!dt) return ItemNull;
    (void)public_id;
    (void)system_id;
    return js_dom_wrap_element(dt);
}

// Save the current document and switch to the supplied foreign doc.
// Returns the previous document pointer (caller must restore via
// js_dom_restore_active_document).
extern "C" void* js_dom_swap_active_document(void* new_doc) {
    void* prev = (void*)_js_current_document;
    if (new_doc) {
        _js_current_document = (DomDocument*)new_doc;
    }
    return prev;
}

extern "C" void js_dom_restore_active_document(void* prev_doc) {
    _js_current_document = (DomDocument*)prev_doc;
}

// ============================================================================
// document.implementation Singleton
// ============================================================================


static Item js_dom_impl_create_html_document_method(Item title) {
    const char* title_text = fn_to_cstr(title);
    return js_create_foreign_html_doc(title_text ? title_text : "");
}

static Item js_dom_impl_create_document_method(Item namespace_uri, Item qualified_name, Item doctype) {
    (void)namespace_uri;
    (void)doctype;
    const char* qualified_text = fn_to_cstr(qualified_name);
    return js_create_foreign_xml_doc(qualified_text);
}

static Item js_dom_impl_create_document_type_method(Item name, Item public_id, Item system_id) {
    const char* name_text = fn_to_cstr(name);
    const char* public_text = fn_to_cstr(public_id);
    const char* system_text = fn_to_cstr(system_id);
    return js_create_doctype_node(name_text ? name_text : "",
        public_text ? public_text : "", system_text ? system_text : "");
}

static Item js_dom_impl_has_feature_method(Item feature, Item version) {
    (void)feature;
    (void)version;
    // WHATWG requires hasFeature() to return true; the direct callable owns
    // that invariant without re-selecting it from its property spelling.
    return (Item){.item = ITEM_TRUE};
}

template <typename Target>
static void js_dom_set_implementation_method(Item implementation,
        const char* name, Target target, int adapter_arity) {
    Item key = js_name_item(name);
    js_set_key_default(implementation, key,
        js_new_native_function(target, adapter_arity));
    js_mark_non_enumerable(implementation, key);
}

extern "C" Item js_get_dom_implementation(void) {
    if (!dom_ensure_roots()) return ItemNull;
    if (get_type_id(js_dom_implementation_item) == LMD_TYPE_MAP &&
        js_is_dom_implementation(js_dom_implementation_item)) {
        return js_dom_implementation_item;
    }
    // Root-range cleanup clears expired realm cache slots to zero; zero must
    // rebuild this context-owned DOMImplementation instead of surfacing null.
    // A marker-only Map has no shape storage, so compiled property probes
    // dereference a null data pointer. Use a normal JS object and identify the
    // context-owned singleton by its cache identity instead.
    js_dom_implementation_item = js_new_object();
    // DOMImplementation is a plain sentinel map, so property reads cannot
    // reach the native method-call dispatcher unless callable members exist.
    js_dom_set_implementation_method(js_dom_implementation_item, "createHTMLDocument",
        js_dom_impl_create_html_document_method, 1);
    js_dom_set_implementation_method(js_dom_implementation_item, "createDocument",
        js_dom_impl_create_document_method, 3);
    js_dom_set_implementation_method(js_dom_implementation_item, "createDocumentType",
        js_dom_impl_create_document_type_method, 3);
    js_dom_set_implementation_method(js_dom_implementation_item, "hasFeature",
        js_dom_impl_has_feature_method, 2);
    return js_dom_implementation_item;
}

// ============================================================================
// Computed Style Wrapping
// ============================================================================

typedef const void* (*JsDomStyleHostType)(void);

static bool js_is_style_wrapper(Item item, const void* vmap_marker,
        const void* map_marker, JsDomStyleHostType host_type) {
    TypeId tid = get_type_id(item);
    if (tid == LMD_TYPE_VMAP) {
        return item.vmap &&
            (item.vmap->host_type == vmap_marker ||
             item.vmap->host_type == host_type()) &&
            item.vmap->host_data != nullptr;
    }
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == map_marker;
}
JS_FORWARD_STATIC_RETURN(bool, js_is_computed_style, (Item item), js_is_style_wrapper, (item, (const void*)&js_computed_style_vmap_marker, (const void*)&js_computed_style_marker, radiant_dom_computed_style_host_type))
JS_FORWARD_RETURN(bool, js_is_computed_style_item, (Item item), js_is_computed_style, (item))
JS_FORWARD_STATIC_RETURN(bool, js_is_inline_style, (Item item), js_is_style_wrapper, (item, (const void*)&js_inline_style_vmap_marker, (const void*)&js_inline_style_marker, radiant_dom_inline_style_host_type))
JS_FORWARD_RETURN(bool, js_is_inline_style_item, (Item item), js_is_inline_style, (item))


static Item js_dom_get_inline_style_wrapper(DomElement* elem) {
    if (!elem) return ItemNull;
    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item != ITEM_NULL) {
        Item cached = js_get_name_key(exp_map, "__styleWrapper");
        if (js_is_inline_style(cached)) return cached;
    }

    Item wrapped = vmap_new();
    if (get_type_id(wrapped) != LMD_TYPE_VMAP || !wrapped.vmap) return ItemNull;
    // Inline style wrappers are native VMaps; the owner element pointer is the
    // invariant used by style get/set and method dispatch.
    wrapped.vmap->host_type = radiant_dom_inline_style_host_type();
    wrapped.vmap->host_data = elem;
    if (exp_map.item != ITEM_NULL) {
        js_set_name_key(exp_map, "__styleWrapper", wrapped);
    }
    return wrapped;
}

static Item js_classlist_value_item(DomElement* elem) {
    if (!elem || elem->class_count == 0) {
        return js_name_item("");
    }
    StrBuf* sb = strbuf_new_cap(64);
    for (int i = 0; i < elem->class_count; i++) {
        if (i > 0) strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, elem->class_names[i]);
    }
    Item result = js_name_item(sb->str ? sb->str : "");
    strbuf_free(sb);
    return result;
}

typedef enum JsClassListOperation {
    JS_CLASSLIST_ADD = 1,
    JS_CLASSLIST_REMOVE,
    JS_CLASSLIST_TOGGLE,
    JS_CLASSLIST_CONTAINS,
    JS_CLASSLIST_ITEM,
    JS_CLASSLIST_REPLACE,
    JS_CLASSLIST_TO_STRING,
    JS_CLASSLIST_ITERATOR,
} JsClassListOperation;

typedef struct JsClassListMethodSpec {
    const char* name;
    JsClassListOperation operation;
    int formal_length;
} JsClassListMethodSpec;

static Item js_classlist_operation(Item elem_item, JsClassListOperation operation,
                                   Item* args, int argc);

static Item js_classlist_operation_body(Item callee, Item this_value,
        Item* args, int argc, uint64_t* result_home) {
    (void)result_home;
    JsFunction* fn = get_type_id(callee) == LMD_TYPE_FUNC
        ? (JsFunction*)callee.function : NULL;
    JsClassListOperation operation = fn
        ? (JsClassListOperation)fn->native_target.bits : (JsClassListOperation)0;
    return js_classlist_operation(this_value, operation, args, argc);
}

static Item js_dom_get_classlist_wrapper(DomElement* elem, Item elem_item) {
    if (!elem) return ItemNull;
    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    Item cache_key = js_name_item("__classListWrapper");
    Item wrapper = exp_map.item != ITEM_NULL ? js_get_key_default(exp_map, cache_key) : ItemNull;
    JS_ROOTS(roots,
        elem_root, elem_item,
        expando_root, exp_map,
        wrapper_root, wrapper,
        method_root, ItemNull);
    if (get_type_id(wrapper_root.get()) != LMD_TYPE_MAP &&
            get_type_id(wrapper_root.get()) != LMD_TYPE_VMAP) {
        wrapper_root.set(js_new_object());
        static const JsClassListMethodSpec methods[] = {
            {"add", JS_CLASSLIST_ADD, 0},
            {"remove", JS_CLASSLIST_REMOVE, 0},
            {"toggle", JS_CLASSLIST_TOGGLE, 1},
            {"contains", JS_CLASSLIST_CONTAINS, 1},
            {"item", JS_CLASSLIST_ITEM, 1},
            {"replace", JS_CLASSLIST_REPLACE, 2},
            {"toString", JS_CLASSLIST_TO_STRING, 0},
        };
        for (int i = 0; i < (int)(sizeof(methods) / sizeof(methods[0])); i++) {
            method_root.set(js_new_native_payload_function(
                js_classlist_operation_body, (uint64_t)methods[i].operation,
                methods[i].formal_length));
            method_root.set(js_bind_function(method_root.get(), elem_root.get(), NULL, 0));
            js_set_key_default(wrapper_root.get(),
                js_name_item(methods[i].name),
                method_root.get());
        }
        method_root.set(js_new_native_payload_function(
            js_classlist_operation_body, (uint64_t)JS_CLASSLIST_ITERATOR, 0));
        method_root.set(js_bind_function(method_root.get(), elem_root.get(), NULL, 0));
        // DOMTokenList is iterable; delegated UI event routers commonly spread
        // classList while resolving their target before invoking callbacks.
        js_set_key_default(wrapper_root.get(), js_well_known_symbol_key(1), method_root.get());
        if (expando_root.get().item != ITEM_NULL) {
            // The wrapper and owner must remain rooted while function creation
            // allocates; otherwise precise GC can publish a stale cache edge.
            js_set_key_default(expando_root.get(), cache_key, wrapper_root.get());
        }
    }
    js_set_key_cstr(wrapper_root.get(), "length", (Item){.item = i2it((int64_t)elem->class_count)});
    js_set_key_cstr(wrapper_root.get(), "value", js_classlist_value_item(elem));
    return wrapper_root.get();
}

struct JsComputedStyleHost {
    DomElement* elem;
    int pseudo_type;
};

extern "C" Item js_get_computed_style(Item elem_item, Item pseudo_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node || !node->is_element()) {
        log_debug("js_get_computed_style: not a DOM element");
        return ItemNull;
    }

    // determine pseudo-element type: 0=none, 1=before, 2=after
    int pseudo_type = 0;
    if (get_type_id(pseudo_item) == LMD_TYPE_STRING || get_type_id(pseudo_item) == LMD_TYPE_SYMBOL) {
        const char* pseudo_str = fn_to_cstr(pseudo_item);
        if (pseudo_str) {
            // handle both "before" and "::before" or ":before"
            while (*pseudo_str == ':') pseudo_str++;
            if (strcmp(pseudo_str, "before") == 0) pseudo_type = 1;
            else if (strcmp(pseudo_str, "after") == 0) pseudo_type = 2;
        }
    }

    DomElement* elem = node->as_element();
    Pool* pool = elem && elem->doc ? elem->doc->document_pool : nullptr;
    JsComputedStyleHost* host = pool ? (JsComputedStyleHost*)pool_calloc(pool, sizeof(JsComputedStyleHost)) : nullptr;
    if (!host) return ItemNull;
    host->elem = elem;
    host->pseudo_type = pseudo_type;

    Item wrapper = vmap_new();
    if (get_type_id(wrapper) != LMD_TYPE_VMAP || !wrapper.vmap) return ItemNull;
    // Computed style wrappers are native VMaps; pseudo-element state lives in
    // the document pool instead of overloading Map::data_cap.
    wrapper.vmap->host_type = radiant_dom_computed_style_host_type();
    wrapper.vmap->host_data = host;

    log_debug("js_get_computed_style: created wrapper for <%s> pseudo=%d",
              elem->tag_name ? elem->tag_name : "?", pseudo_type);

    return wrapper;
}
JS_FORWARD_STATIC_ITEM(js_window_get_computed_style, (Item elem_item, Item pseudo_item), js_get_computed_style, (elem_item, pseudo_item))

static void js_dom_install_window_computed_style_global(void) {
    Item global = js_get_global_this();
    Item key = js_string_key("getComputedStyle");
    Item existing = js_get_key_default(global, key);
    if (js_is_callable(existing)) return;
    Item fn = js_new_native_function(js_window_get_computed_style);
    js_set_function_name(fn, key);
    // getComputedStyle is a Window/global function now; direct MIR DOM shims
    // were removed so calls resolve through ordinary property dispatch.
    js_set_key_default(global, key, fn);
}

// ============================================================================
// CSS var() Resolution for Custom Properties
// ============================================================================

// classify a CSS token for the consecutive-token ambiguity table
enum CssTokenClass {
    TC_IDENT,       // ident, function, url
    TC_AT_KEYWORD,  // at-keyword
    TC_HASH,        // hash
    TC_DIMENSION,   // dimension
    TC_NUMBER,      // number
    TC_PERCENTAGE,  // percentage
    TC_CDC,         // -->
    TC_LPAREN,      // (
    TC_DELIM_HASH,  // # (delimiter)
    TC_DELIM_MINUS, // - (delimiter)
    TC_DELIM_AT,    // @ (delimiter)
    TC_DELIM_DOT,   // . (delimiter)
    TC_DELIM_PLUS,  // + (delimiter)
    TC_DELIM_SLASH, // / (delimiter)
    TC_DELIM_STAR,  // * (delimiter)
    TC_OTHER
};

static CssTokenClass classify_token(const CssToken* tok) {
    switch (tok->type) {
        case CSS_TOKEN_IDENT:
        case CSS_TOKEN_IDENTIFIER:
        case CSS_TOKEN_CUSTOM_PROPERTY:
            return TC_IDENT;
        case CSS_TOKEN_FUNCTION:
        case CSS_TOKEN_VAR_FUNCTION:
        case CSS_TOKEN_CALC_FUNCTION:
        case CSS_TOKEN_COLOR_FUNCTION:
            return TC_IDENT;  // function tokens start with ident
        case CSS_TOKEN_URL:
            return TC_IDENT;  // url() starts like an ident
        case CSS_TOKEN_AT_KEYWORD:
            return TC_AT_KEYWORD;
        case CSS_TOKEN_HASH:
            return TC_HASH;
        case CSS_TOKEN_DIMENSION:
            return TC_DIMENSION;
        case CSS_TOKEN_NUMBER:
            return TC_NUMBER;
        case CSS_TOKEN_PERCENTAGE:
            return TC_PERCENTAGE;
        case CSS_TOKEN_CDC:
            return TC_CDC;
        case CSS_TOKEN_LEFT_PAREN:
            return TC_LPAREN;
        case CSS_TOKEN_DELIM:
            if (tok->data.delimiter == '#') return TC_DELIM_HASH;
            if (tok->data.delimiter == '-') return TC_DELIM_MINUS;
            if (tok->data.delimiter == '@') return TC_DELIM_AT;
            if (tok->data.delimiter == '.') return TC_DELIM_DOT;
            if (tok->data.delimiter == '+') return TC_DELIM_PLUS;
            if (tok->data.delimiter == '/') return TC_DELIM_SLASH;
            if (tok->data.delimiter == '*') return TC_DELIM_STAR;
            if (tok->data.delimiter == '%') return TC_PERCENTAGE; // bare % is percentage-like
            return TC_OTHER;
        default:
            return TC_OTHER;
    }
}

// check if two adjacent tokens need a comment inserted between them
// per CSS Syntax spec §9.2 "would-be ambiguous token pairs"
static bool tokens_need_comment(CssTokenClass left, CssTokenClass right) {
    // ident/function/url + ident/function/url/-/number/%/dim/CDC/()
    if (left == TC_IDENT) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC ||
               right == TC_LPAREN;
    }
    // at-keyword + ident/function/url/-/number/%/dim/CDC
    if (left == TC_AT_KEYWORD) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC;
    }
    // hash + ident/function/url/-/number/%/dim/CDC
    if (left == TC_HASH) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC;
    }
    // dimension + ident/function/url/-/number/%/dim/CDC
    if (left == TC_DIMENSION) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION || right == TC_CDC;
    }
    // # (delimiter) + ident/function/url/-/number/%/dim
    if (left == TC_DELIM_HASH) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // - (delimiter) + ident/function/url/-/number/%/dim
    if (left == TC_DELIM_MINUS) {
        return right == TC_IDENT || right == TC_DELIM_MINUS || right == TC_NUMBER ||
               right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // number + ident/function/url/number/%/dim/%
    if (left == TC_NUMBER) {
        return right == TC_IDENT || right == TC_NUMBER || right == TC_PERCENTAGE ||
               right == TC_DIMENSION;
    }
    // @ (delimiter) + ident/function/url/-
    if (left == TC_DELIM_AT) {
        return right == TC_IDENT || right == TC_DELIM_MINUS;
    }
    // . (delimiter) + number/%/dim
    if (left == TC_DELIM_DOT) {
        return right == TC_NUMBER || right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // + (delimiter) + number/%/dim
    if (left == TC_DELIM_PLUS) {
        return right == TC_NUMBER || right == TC_PERCENTAGE || right == TC_DIMENSION;
    }
    // / + *
    if (left == TC_DELIM_SLASH) {
        return right == TC_DELIM_STAR;
    }
    return false;
}

/**
 * Resolve a custom property value, substituting var() references.
 * Returns a pool-allocated string with all var() references resolved.
 * Inserts empty CSS comments between ambiguous consecutive tokens per CSS spec §9.2.
 *
 * @param elem     The element context for variable lookup
 * @param val_text The raw value text to resolve
 * @param pool     Memory pool for allocations
 * @param depth    Recursion depth to prevent infinite loops
 * @return Resolved string, or NULL on failure
 */
static const char* js_resolve_custom_property_value(DomElement* elem, const char* val_text, Pool* pool, int depth) {
    if (!val_text || !pool || depth > 10) return val_text;  // max recursion depth

    // quick check: does this value contain var(?
    if (!strstr(val_text, "var(")) return val_text;

    size_t len = strlen(val_text);
    StringBuf* result = stringbuf_new(pool);
    if (!result) return val_text;

    // we'll collect resolved segments, then do token-pair analysis
    // first pass: find and resolve all var() references
    size_t i = 0;

    // we need to collect the resolved text segments for token-pair analysis
    // strategy: build result by scanning for var(--xxx) patterns
    //   - text before var() is literal
    //   - var(--xxx) is replaced with the resolved value of --xxx
    //   - var(--xxx, fallback) uses fallback if --xxx is not defined

    // Track segments for comment insertion between var() boundaries
    struct Segment {
        const char* text;
        size_t len;
        bool from_var;  // true if this segment came from var() substitution
    };
    Segment segments[64];
    int seg_count = 0;

    while (i < len && seg_count < 63) {
        // look for var(
        const char* var_start = strstr(val_text + i, "var(");
        if (!var_start) {
            // no more var() — rest is literal
            if (i < len) {
                segments[seg_count].text = val_text + i;
                segments[seg_count].len = len - i;
                segments[seg_count].from_var = false;
                seg_count++;
            }
            break;
        }

        // literal text before var(
        size_t literal_len = var_start - (val_text + i);
        if (literal_len > 0) {
            // strip trailing exterior comments at the var() boundary per CSS spec
            const char* lit_start = val_text + i;
            size_t adj_len = literal_len;
            while (adj_len >= 4) {
                // find last */ in the segment
                // check if segment ends with */  (possibly followed by whitespace)
                size_t check = adj_len;
                while (check > 0 && (lit_start[check-1] == ' ' || lit_start[check-1] == '\t'))
                    check--;
                if (check >= 2 && lit_start[check-2] == '*' && lit_start[check-1] == '/') {
                    // find matching /* backwards — but must NOT be inside a string
                    size_t search = check - 2;
                    bool found = false;
                    while (search > 0) {
                        search--;
                        if (lit_start[search] == '/' && search + 1 < check - 2 && lit_start[search + 1] == '*') {
                            adj_len = search;
                            // trim trailing whitespace after removing comment
                            while (adj_len > 0 && (lit_start[adj_len-1] == ' ' || lit_start[adj_len-1] == '\t'))
                                adj_len--;
                            found = true;
                            break;
                        }
                    }
                    if (!found) break;
                } else {
                    break;
                }
            }
            segments[seg_count].text = lit_start;
            segments[seg_count].len = adj_len;
            segments[seg_count].from_var = false;
            seg_count++;
        }

        // parse var(--name) or var(--name, fallback)
        const char* p = var_start + 4;  // skip "var("

        // skip whitespace
        while (*p == ' ' || *p == '\t') p++;

        // extract variable name (must start with --)
        if (p[0] != '-' || p[1] != '-') {
            // not a valid var() — treat as literal
            segments[seg_count].text = var_start;
            segments[seg_count].len = 4;  // "var("
            segments[seg_count].from_var = false;
            seg_count++;
            i = (var_start - val_text) + 4;
            continue;
        }

        const char* name_start = p;
        while (*p && *p != ')' && *p != ',') p++;

        size_t name_len = p - name_start;
        // trim trailing whitespace from name
        while (name_len > 0 && (name_start[name_len-1] == ' ' || name_start[name_len-1] == '\t'))
            name_len--;

        char var_name[128];
        if (name_len >= sizeof(var_name)) name_len = sizeof(var_name) - 1;
        memcpy(var_name, name_start, name_len);
        var_name[name_len] = '\0';

        // check for fallback
        const char* fallback = nullptr;
        size_t fallback_len = 0;
        if (*p == ',') {
            p++; // skip comma
            // skip whitespace
            while (*p == ' ' || *p == '\t') p++;
            fallback = p;
            // find matching closing paren, accounting for nested parens
            int paren_depth = 1;
            while (*p && paren_depth > 0) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') { paren_depth--; if (paren_depth == 0) break; }
                p++;
            }
            fallback_len = p - fallback;
            // trim trailing whitespace from fallback
            while (fallback_len > 0 && (fallback[fallback_len-1] == ' ' || fallback[fallback_len-1] == '\t'))
                fallback_len--;
        } else {
            // skip to closing paren
            int paren_depth = 1;
            while (*p && paren_depth > 0) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') { paren_depth--; if (paren_depth == 0) break; }
                p++;
            }
        }

        if (*p == ')') p++; // skip closing paren

        // resolve the variable
        CssDeclaration* var_decl = js_match_custom_property(elem, var_name);
        const char* resolved = nullptr;
        size_t resolved_len = 0;

        if (var_decl) {
            if (var_decl->value_text && var_decl->value_text_len > 0) {
                resolved = var_decl->value_text;
                resolved_len = var_decl->value_text_len;
            } else if (var_decl->value) {
                CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
                if (fmt) {
                    css_format_value(fmt, var_decl->value);
                    String* s = stringbuf_to_string(fmt->output);
                    if (s) {
                        resolved = s->chars;
                        resolved_len = s->len;
                    }
                }
            }
        }

        // trim whitespace from resolved value
        if (resolved) {
            while (resolved_len > 0 && (*resolved == ' ' || *resolved == '\t')) {
                resolved++;
                resolved_len--;
            }
            while (resolved_len > 0 && (resolved[resolved_len-1] == ' ' || resolved[resolved_len-1] == '\t'))
                resolved_len--;
        }

        if (resolved && resolved_len > 0) {
            // recursively resolve nested var() in the resolved value
            char* resolved_copy = (char*)pool_alloc(pool, resolved_len + 1);
            if (resolved_copy) {
                memcpy(resolved_copy, resolved, resolved_len);
                resolved_copy[resolved_len] = '\0';
                const char* nested = js_resolve_custom_property_value(elem, resolved_copy, pool, depth + 1);
                if (nested) {
                    // strip exterior comments from var() result
                    // per spec, comments at boundaries of var() substitution are removed
                    const char* clean = nested;
                    size_t clean_len = strlen(clean);
                    // strip leading comment
                    while (clean_len >= 4 && clean[0] == '/' && clean[1] == '*') {
                        const char* end_comment = strstr(clean + 2, "*/");
                        if (end_comment) {
                            clean = end_comment + 2;
                            clean_len = strlen(clean);
                        } else break;
                    }
                    // strip trailing comment
                    while (clean_len >= 4 && clean[clean_len-1] == '/' && clean[clean_len-2] == '*') {
                        // find the start of this comment by searching backwards for /*
                        size_t j = clean_len - 2;
                        while (j > 0 && !(clean[j] == '/' && clean[j+1] == '*')) j--;
                        if (clean[j] == '/' && clean[j+1] == '*') {
                            clean_len = j;
                        } else break;
                    }
                    segments[seg_count].text = clean;
                    segments[seg_count].len = clean_len;
                    segments[seg_count].from_var = true;
                    seg_count++;
                }
            }
        } else if (fallback && fallback_len > 0) {
            // use fallback value
            char* fb_copy = (char*)pool_alloc(pool, fallback_len + 1);
            if (fb_copy) {
                memcpy(fb_copy, fallback, fallback_len);
                fb_copy[fallback_len] = '\0';
                const char* resolved_fb = js_resolve_custom_property_value(elem, fb_copy, pool, depth + 1);
                segments[seg_count].text = resolved_fb ? resolved_fb : fb_copy;
                segments[seg_count].len = strlen(segments[seg_count].text);
                segments[seg_count].from_var = true;
                seg_count++;
            }
        }
        // else: var() with no value and no fallback — produces nothing (empty)

        i = p - val_text;
    }

    if (seg_count == 0) return "";

    // now concatenate segments with comment insertion between ambiguous token boundaries
    // for segments that come from var() substitution, we need to check the last token
    // of the previous segment against the first token of the next segment
    for (int s = 0; s < seg_count; s++) {
        if (s > 0) {
            // check if we need a comment between previous segment and this one
            // only needed when at least one segment is from var() substitution
            if (segments[s].from_var || segments[s-1].from_var) {
                // get last token of previous segment
                const char* prev_text = segments[s-1].text;
                size_t prev_len = segments[s-1].len;
                const char* cur_text = segments[s].text;
                size_t cur_len = segments[s].len;

                if (prev_len > 0 && cur_len > 0) {
                    // tokenize the last few chars of prev and first few chars of cur
                    // to determine if they'd be ambiguous
                    char* prev_copy = (char*)pool_alloc(pool, prev_len + 1);
                    char* cur_copy = (char*)pool_alloc(pool, cur_len + 1);
                    if (prev_copy && cur_copy) {
                        memcpy(prev_copy, prev_text, prev_len);
                        prev_copy[prev_len] = '\0';
                        memcpy(cur_copy, cur_text, cur_len);
                        cur_copy[cur_len] = '\0';

                        size_t prev_tok_count = 0, cur_tok_count = 0;
                        CssToken* prev_tokens = css_tokenize(prev_copy, prev_len, pool, &prev_tok_count);
                        CssToken* cur_tokens = css_tokenize(cur_copy, cur_len, pool, &cur_tok_count);

                        if (prev_tokens && cur_tokens && prev_tok_count > 0 && cur_tok_count > 0) {
                            // find last non-whitespace token of prev
                            int last_idx = (int)prev_tok_count - 1;
                            while (last_idx >= 0 && prev_tokens[last_idx].type == CSS_TOKEN_WHITESPACE) last_idx--;
                            // skip EOF token
                            while (last_idx >= 0 && prev_tokens[last_idx].type == CSS_TOKEN_EOF) last_idx--;

                            // find first non-whitespace token of cur
                            size_t first_idx = 0;
                            while (first_idx < cur_tok_count && cur_tokens[first_idx].type == CSS_TOKEN_WHITESPACE) first_idx++;

                            if (last_idx >= 0 && first_idx < cur_tok_count &&
                                cur_tokens[first_idx].type != CSS_TOKEN_EOF) {
                                CssTokenClass left_class = classify_token(&prev_tokens[last_idx]);
                                CssTokenClass right_class = classify_token(&cur_tokens[first_idx]);

                                if (tokens_need_comment(left_class, right_class)) {
                                    stringbuf_append_str(result, "/**/");
                                }
                            }
                        }
                    }
                }
            }
        }

        // append segment text
        char* seg_copy = (char*)pool_alloc(pool, segments[s].len + 1);
        if (seg_copy) {
            memcpy(seg_copy, segments[s].text, segments[s].len);
            seg_copy[segments[s].len] = '\0';
            stringbuf_append_str(result, seg_copy);
        }
    }

    String* final_str = stringbuf_to_string(result);
    return (final_str) ? final_str->chars : "";
}

extern "C" Item js_computed_style_get_property(Item style_item, Item prop_name) {
    if (!js_is_computed_style(style_item)) {
        log_debug("js_computed_style_get_property: not a computed style object");
        return ItemNull;
    }

    DomElement* elem = nullptr;
    int pseudo_type = 0;
    if (get_type_id(style_item) == LMD_TYPE_VMAP) {
        JsComputedStyleHost* host = (JsComputedStyleHost*)style_item.vmap->host_data;
        elem = host ? host->elem : nullptr;
        pseudo_type = host ? host->pseudo_type : 0;
    } else {
        Map* wrapper = style_item.map;
        elem = (DomElement*)wrapper->data;
        pseudo_type = (int)wrapper->data_cap;
    }

    if (!elem) return js_name_item("");
    const char* js_prop = fn_to_cstr(prop_name);
    if (!js_prop) return js_name_item("");

    // handle getPropertyValue method separately
    if (strcmp(js_prop, "getPropertyValue") == 0) {
        // return a function-like marker — handled by method dispatch
        return ItemNull;
    }

    // convert camelCase JS property to CSS hyphenated property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    if (strcmp(css_prop, "content-visibility") == 0) {
        const char* hidden = elem->get_attribute("hidden");
        if (hidden && strcasecmp(hidden, "until-found") == 0) {
            return js_name_item("hidden");
        }
        return js_name_item("");
    }

    // look up the CSS property ID
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);
    if (prop_id == CSS_PROPERTY_UNKNOWN || prop_id == 0) {
        // check for CSS custom properties (--foo)
        // note: css_property_code_from_name returns 0 for not-found, CSS_PROPERTY_UNKNOWN is -1
        if (css_prop[0] == '-' && css_prop[1] == '-') {
            // on-demand matching for custom property
            CssDeclaration* decl = js_match_custom_property(elem, css_prop);
            if (decl && (decl->value || decl->value_text)) {
                Pool* pool = elem->doc ? elem->doc->document_pool : nullptr;
                if (!pool) return js_name_item("");
                const char* val = css_serialize_declaration_value(decl, pool);
                if (!val) val = "";
                // trim leading/trailing whitespace per CSS spec
                while (*val == ' ' || *val == '\t' || *val == '\n' || *val == '\r') val++;
                size_t vlen = strlen(val);
                while (vlen > 0 && (val[vlen-1] == ' ' || val[vlen-1] == '\t' || val[vlen-1] == '\n' || val[vlen-1] == '\r')) vlen--;
                char* trimmed = (char*)pool_alloc(pool, vlen + 1);
                if (trimmed) { memcpy(trimmed, val, vlen); trimmed[vlen] = '\0'; val = trimmed; }

                // resolve var() references in the value
                if (val && strstr(val, "var(")) {
                    const char* resolved = js_resolve_custom_property_value(elem, val, pool, 0);
                    if (resolved) val = resolved;
                }

                return js_name_item(val);
            }
            return js_name_item("");
        }
        log_debug("js_computed_style_get_property: unknown CSS property '%s' (from JS '%s')",
                  css_prop, js_prop);
        return js_name_item("");
    }

    char computed[512];
    if (css_prop_serialize_computed(elem, prop_id, pseudo_type,
                                    computed, sizeof(computed))) {
        return js_name_item(computed);
    }
    return js_name_item("");
}

// ============================================================================
// On-demand CSS selector matching for getComputedStyle
// ============================================================================

/**
 * On-demand stylesheet matching for a custom or regular property. Inline
 * regular styles are resolved by their caller before this stylesheet pass.
 */
static CssDeclaration* js_match_custom_property(DomElement* elem, const char* prop_name) {
    if (!elem || !elem->doc || !prop_name) return nullptr;

    DomDocument* doc = elem->doc;
    Pool* pool = doc->document_pool;

    CssDeclaration* best_decl = nullptr;
    CssSpecificity best_spec = {0, 0, 0, 0, false};

    // check inline custom properties first (highest specificity: 1,0,0,0)
    // inline styles are stored in elem->css_variables as a linked list
    // created by dom_element_apply_declaration when style.setProperty("--name", value) is called
    if (elem->css_variables) {
        CssCustomProp* prop = elem->css_variables;
        while (prop) {
            if (prop->name && strcmp(prop->name, prop_name) == 0) {
                // create a synthetic CssDeclaration for the inline custom property
                CssDeclaration* decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
                if (decl) {
                    decl->property_name = prop->name;
                    decl->value = (CssValue*)prop->value;
                    decl->value_text = prop->value_text;
                    decl->value_text_len = prop->value_text_len;
                    decl->specificity = {1, 0, 0, 0, false};  // inline style
                    decl->valid = true;
                    best_decl = decl;
                    best_spec = decl->specificity;
                }
                break;  // linked list: first match is the most recent (prepended)
            }
            prop = prop->next;
        }
    }

    // search stylesheets (lower specificity than inline)
    if (doc->stylesheets && doc->stylesheet_count > 0) {
        SelectorMatcher* matcher = js_dom_create_selector_matcher(doc);
        if (matcher) {
            for (int s = 0; s < doc->stylesheet_count; s++) {
                CssStylesheet* sheet = doc->stylesheets[s];
                if (!sheet) continue;

                for (size_t r = 0; r < sheet->rule_count; r++) {
                    CssRule* rule = sheet->rules[r];
                    if (!rule || rule->type != CSS_RULE_STYLE) continue;
                    if (rule->data.style_rule.declaration_count == 0) continue;

                    bool matched = false;
                    CssSpecificity match_spec = {0, 0, 0, 0, false};

                    CssSelectorGroup* group = rule->data.style_rule.selector_group;
                    CssSelector* single_sel = rule->data.style_rule.selector;

                    if (group && group->selector_count > 0) {
                        for (size_t si = 0; si < group->selector_count; si++) {
                            CssSelector* sel = group->selectors[si];
                            if (!sel) continue;
                            MatchResult result;
                            if (selector_matcher_matches(matcher, sel, elem, &result)) {
                                matched = true;
                                match_spec = result.specificity;
                                break;
                            }
                        }
                    } else if (single_sel) {
                        MatchResult result;
                        if (selector_matcher_matches(matcher, single_sel, elem, &result)) {
                            matched = true;
                            match_spec = result.specificity;
                        }
                    }

                    if (!matched) continue;

                    // find matching custom property by name
                    for (size_t d = 0; d < rule->data.style_rule.declaration_count; d++) {
                        CssDeclaration* decl = rule->data.style_rule.declarations[d];
                        if (!decl || !decl->property_name) continue;
                        if (strcmp(decl->property_name, prop_name) != 0) continue;

                        if (!best_decl || css_specificity_compare(match_spec, best_spec) >= 0) {
                            best_decl = decl;
                            best_spec = match_spec;
                        }
                    }
                }
            }
        }
    }

    return best_decl;
}

// ============================================================================
// Helper: find element by ID (tree walk)
// ============================================================================

DomElement* js_dom_find_element_by_id(DomElement* root, const char* id) {
    if (!root || !id) return nullptr;
    if (root->id && strcmp(root->id, id) == 0) return root;

    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* found = js_dom_find_element_by_id(child->as_element(), id);
            if (found) return found;
        }
        child = child->next_sibling;
    }
    return nullptr;
}

extern "C" void* js_dom_popover_target_for_button(void* button_ptr) {
    DomElement* button = (DomElement*)button_ptr;
    if (!button || !button->doc || !button->tag_name ||
        strcasecmp(button->tag_name, "button") != 0) {
        return nullptr;
    }
    const char* target_id = button->get_attribute("popovertarget");
    if (!target_id || !*target_id || !button->doc->root) return nullptr;
    DomElement* target = js_dom_find_element_by_id(button->doc->root, target_id);
    return target && target->has_attribute("popover") ? target : nullptr;
}

extern "C" int js_dom_popover_target_action(void* button_ptr) {
    DomElement* button = (DomElement*)button_ptr;
    const char* action = button ? button->get_attribute("popovertargetaction") : nullptr;
    if (!action || strcasecmp(action, "toggle") == 0) return 0;
    if (strcasecmp(action, "show") == 0) return 1;
    if (strcasecmp(action, "hide") == 0) return 2;
    return 0;
}

extern "C" bool js_dom_activate_popover(void* popover_ptr, int action) {
    DomElement* popover = (DomElement*)popover_ptr;
    if (!popover || !popover->has_attribute("popover")) return false;

    bool was_open = popover->is_popover_open();
    bool should_open = action == 1 ? true : action == 2 ? false : !was_open;
    if (was_open == should_open) return false;
    popover->set_popover_open(should_open);
    js_dom_notify_mutation(DOM_JS_MUTATION_STYLE, popover, popover->parent);
    return true;
}

static const char* js_dom_normalize_contenteditable(const char* value) {
    if (!value || *value == '\0' || strcasecmp(value, "true") == 0) return "true";
    if (strcasecmp(value, "false") == 0) return "false";
    if (strcasecmp(value, "plaintext-only") == 0) return "plaintext-only";
    if (strcasecmp(value, "inherit") == 0) return "inherit";
    return nullptr;
}

static const char* js_dom_autocapitalize_state(const char* value, bool missing_is_empty) {
    if (!value) return missing_is_empty ? "" : "sentences";
    if (*value == '\0') return "";
    if (strcasecmp(value, "off") == 0 || strcasecmp(value, "none") == 0) return "none";
    if (strcasecmp(value, "on") == 0 || strcasecmp(value, "sentences") == 0) return "sentences";
    if (strcasecmp(value, "characters") == 0) return "characters";
    if (strcasecmp(value, "words") == 0) return "words";
    return "sentences";
}
JS_FORWARD_STATIC_RETURN(bool, js_dom_autocapitalize_inherits_from_form, (DomElement* elem), _is_tag, (elem, "button") || _is_tag(elem, "fieldset") || _is_tag(elem, "input") || _is_tag(elem, "output") || _is_tag(elem, "select") || _is_tag(elem, "textarea"))

static DomElement* js_dom_form_owner(DomElement* elem) {
    if (!elem) return nullptr;
    const char* form_id = elem->get_attribute("form");
    if (form_id && *form_id) {
        DomDocument* doc = elem->doc ? elem->doc : _js_current_document;
        DomElement* root = doc ? doc->root : nullptr;
        return js_dom_find_element_by_id(root, form_id);
    }
    DomNode* p = elem->parent;
    while (p) {
        if (p->is_element()) {
            DomElement* pe = p->as_element();
            if (_is_tag(pe, "form")) return pe;
        }
        p = p->parent;
    }
    return nullptr;
}

static const char* js_dom_get_autocapitalize(DomElement* elem) {
    if (!elem) return "";
    const char* own = elem->get_attribute("autocapitalize");
    if (own) return js_dom_autocapitalize_state(own, true);
    if (js_dom_autocapitalize_inherits_from_form(elem)) {
        DomElement* form = js_dom_form_owner(elem);
        if (form) {
            const char* form_value = form->get_attribute("autocapitalize");
            if (form_value && *form_value) return js_dom_autocapitalize_state(form_value, false);
        }
    }
    return "";
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_autocorrect_attr_state, (const char* value), (!(value && strcasecmp(value, "off") == 0)))

static bool js_dom_autocorrect_disabled_by_input_type(DomElement* elem) {
    if (!_is_tag(elem, "input")) return false;
    const char* type = elem->get_attribute("type");
    if (!type) return false;
    return strcasecmp(type, "password") == 0 ||
        strcasecmp(type, "email") == 0 ||
        strcasecmp(type, "url") == 0;
}

static bool js_dom_get_autocorrect(DomElement* elem) {
    if (!elem) return true;
    if (js_dom_autocorrect_disabled_by_input_type(elem)) return false;
    const char* own = elem->get_attribute("autocorrect");
    if (own || elem->has_attribute("autocorrect"))
        return js_dom_autocorrect_attr_state(own ? own : "");
    if (js_dom_autocapitalize_inherits_from_form(elem)) {
        DomElement* form = js_dom_form_owner(elem);
        if (form) {
            const char* form_value = form->get_attribute("autocorrect");
            if (form_value || form->has_attribute("autocorrect"))
                return js_dom_autocorrect_attr_state(form_value ? form_value : "");
        }
    }
    return true;
}

static bool js_dom_spellcheck_state_from_value(const char* value, bool* out) {
    if (!value || *value == '\0' || strcasecmp(value, "true") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(value, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool js_dom_get_spellcheck(DomElement* elem) {
    DomNode* p = (DomNode*)elem;
    while (p) {
        if (p->is_element()) {
            DomElement* e = p->as_element();
            const char* value = e->get_attribute("spellcheck");
            bool state = true;
            if (value && js_dom_spellcheck_state_from_value(value, &state)) return state;
        }
        p = p->parent;
    }
    return true;
}

static const char* js_dom_writing_suggestions_attr_state(const char* value) {
    if (!value || *value == '\0' || strcasecmp(value, "true") == 0) return "true";
    if (strcasecmp(value, "false") == 0) return "false";
    return "true";
}

static const char* js_dom_get_writing_suggestions(DomElement* elem) {
    DomNode* p = (DomNode*)elem;
    while (p) {
        if (p->is_element()) {
            DomElement* e = p->as_element();
            if (e->has_attribute("writingsuggestions")) {
                return js_dom_writing_suggestions_attr_state(
                    e->get_attribute("writingsuggestions"));
            }
        }
        p = p->parent;
    }
    return "true";
}

static bool js_dom_data_attr_to_dataset_key(const char* attr, char* out, size_t out_cap) {
    if (!attr || !out || out_cap == 0 || strncmp(attr, "data-", 5) != 0) return false;
    const char* p = attr + 5;
    if (!*p) return false;
    size_t len = 0;
    bool upper_next = false;
    while (*p) {
        char c = *p++;
        if (c == '-') {
            upper_next = true;
            continue;
        }
        if (len + 1 >= out_cap) return false;
        if (upper_next && c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[len++] = c;
        upper_next = false;
    }
    out[len] = '\0';
    return len > 0;
}

extern "C" Item js_dom_dataset_property(Item elem_item) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;
    JS_ROOTS(roots,
        elem_root, elem_item,
        dataset_root, js_new_object(),
        key_root, js_string_key("__lambda_dataset_element"),
        value_root, elem_item);
    // Dataset construction allocates keys and shapes; root the unfinished view
    // and owner so a collection cannot leave an asynchronously retained wrapper stale.
    js_define_own_key_storage(dataset_root.get(), key_root.get(), elem_root.get());
    // keep the dataset owner marker private; copying it makes ordinary objects
    // route later writes back to the source element instead of storing fields.
    js_mark_non_enumerable(dataset_root.get(), key_root.get());
    int attr_count = 0;
    const char** names = elem->attribute_names(&attr_count);
    for (int i = 0; i < attr_count; i++) {
        char key_buf[128];
        if (!js_dom_data_attr_to_dataset_key(names[i], key_buf, sizeof(key_buf))) continue;
        const char* value = elem->get_attribute(names[i]);
        key_root.set(js_name_item(key_buf));
        value_root.set(js_name_item(value ? value : ""));
        js_define_own_key_storage(dataset_root.get(), key_root.get(), value_root.get());
    }
    return dataset_root.get();
}

static bool js_dom_has_valid_int_attr(DomElement* elem, const char* attr, long* out) {
    if (!elem || !attr) return false;
    const char* value = elem->get_attribute(attr);
    if (!value) return false;
    char* end = nullptr;
    long parsed = strtol(value, &end, 10);
    if (end == value) return false;
    while (end && *end) {
        if (!isspace((unsigned char)*end)) return false;
        end++;
    }
    if (out) *out = parsed;
    return true;
}

static bool js_dom_is_first_summary_child(DomElement* elem) {
    if (!_is_tag(elem, "summary") || !elem->parent || !elem->parent->is_element()) return false;
    DomElement* parent = elem->parent->as_element();
    if (!_is_tag(parent, "details")) return false;
    DomNode* child = parent->first_child;
    while (child) {
        if (child->is_element()) return child->as_element() == elem;
        child = child->next_sibling;
    }
    return false;
}

static bool js_dom_is_disabled_for_focus(DomElement* elem) {
    if (!elem) return true;
    return form_control_is_disabled(elem->doc ? elem->doc->state : js_dom_current_state(),
                                    static_cast<View*>(elem));
}

static bool js_dom_is_editing_host(DomElement* elem) {
    if (!elem || !elem->has_attribute("contenteditable")) return false;
    const char* state = js_dom_normalize_contenteditable(
        elem->get_attribute("contenteditable"));
    return state && (strcmp(state, "true") == 0 || strcmp(state, "plaintext-only") == 0);
}

static int js_dom_default_tab_index(DomElement* elem) {
    if (!elem || !elem->tag_name) return -1;
    if (_is_tag(elem, "input") || _is_tag(elem, "button") ||
        _is_tag(elem, "select") || _is_tag(elem, "textarea") ||
        _is_tag(elem, "iframe") || _is_tag(elem, "object")) {
        return 0;
    }
    if (_is_tag(elem, "a")) return 0;
    if (js_dom_is_first_summary_child(elem)) return 0;
    return -1;
}

static bool js_dom_is_script_focusable(DomElement* elem) {
    if (!elem || !js_dom_node_is_connected((DomNode*)elem)) return false;
    if (elem->has_attribute("hidden")) return false;
    if (js_dom_is_disabled_for_focus(elem)) return false;

    long tabindex = 0;
    if (js_dom_has_valid_int_attr(elem, "tabindex", &tabindex)) return true;
    if (_is_tag(elem, "input")) {
        const char* type = elem->get_attribute("type");
        return !type || strcasecmp(type, "hidden") != 0;
    }
    if (_is_tag(elem, "button") || _is_tag(elem, "select") ||
        _is_tag(elem, "textarea") || _is_tag(elem, "iframe") ||
        _is_tag(elem, "area")) {
        return true;
    }
    if (_is_tag(elem, "a")) return elem->has_attribute("href");
    if (js_dom_is_first_summary_child(elem)) return true;
    if (js_dom_is_editing_host(elem)) return true;
    return false;
}

static void js_dom_dispatch_focus_events(DomElement* elem) {
    if (!elem) return;
    Item elem_item = js_dom_wrap_element(elem);
    Item focus_event = js_create_native_focus_event("focus", ItemNull);
    js_dom_dispatch_event(elem_item, focus_event);
    Item focusin_event = js_create_native_focus_event("focusin", ItemNull);
    js_dom_dispatch_event(elem_item, focusin_event);
}

static void js_dom_clear_focus_if_disabled_now(DomElement* changed_elem) {
    if (!changed_elem) return;
    DocState* state = changed_elem->doc ? changed_elem->doc->state : js_dom_current_state();
    if (state) {
        View* focused = focus_get(state);
        if (focused && focused->is_element() &&
            js_dom_node_contains((DomNode*)changed_elem, (DomNode*)focused)) {
            DomElement* focused_elem = ((DomNode*)focused)->as_element();
            if (js_dom_is_disabled_for_focus(focused_elem)) {
                focus_clear(state);
            }
        }
    }
    if (js_document_active_element &&
        js_dom_node_contains((DomNode*)changed_elem, (DomNode*)js_document_active_element) &&
        js_dom_is_disabled_for_focus(js_document_active_element)) {
        js_document_active_element = nullptr;
    }
}
JS_FORWARD_VOID( js_dom_after_disabled_attribute_set, (void* elem_ptr), js_dom_clear_focus_if_disabled_now, ((DomElement*)elem_ptr))

static bool js_dom_style_preserves_leading_ws(DomElement* elem, bool inherited) {
    const char* style = elem ? elem->get_attribute("style") : nullptr;
    if (!style) return inherited;
    const char* ws = strstr(style, "white-space");
    if (!ws) return inherited;
    return strstr(ws, "pre") != nullptr || strstr(ws, "break-spaces") != nullptr;
}

static bool js_dom_text_initial_offset(DomText* text, bool preserve_ws, uint32_t* out_offset) {
    if (!text || !text->text || text->length == 0) return false;
    if (preserve_ws) {
        *out_offset = 0;
        return true;
    }

    const char* chars = text->text;
    size_t len = text->length;
    size_t first_visible = 0;
    while (first_visible < len) {
        unsigned char ch = (unsigned char)chars[first_visible];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f') {
            break;
        }
        first_visible++;
    }
    if (first_visible == len) return false;
    *out_offset = js_dom_utf16_length_from_utf8(chars, first_visible);
    return true;
}

static bool js_dom_find_initial_editing_boundary(DomElement* elem,
                                                 bool preserve_ws,
                                                 DomBoundary* out_boundary) {
    if (!elem || !out_boundary) return false;
    bool child_preserve_ws = js_dom_style_preserves_leading_ws(elem, preserve_ws);
    uint32_t index = 0;
    bool have_empty_prefix = false;
    uint32_t empty_prefix_index = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling, index++) {
        if (js_dom_is_generated_pseudo_node(child)) {
            // Generated ::before/::after trees are layout artifacts; a DOM
            // editing host must place its selection in authored content.
            continue;
        }
        if (child->is_text()) {
            uint32_t offset = 0;
            if (js_dom_text_initial_offset(child->as_text(), child_preserve_ws, &offset)) {
                out_boundary->node = child;
                out_boundary->offset = offset;
                return true;
            }
            if (!have_empty_prefix) {
                have_empty_prefix = true;
                empty_prefix_index = index;
            }
            continue;
        }
        if (!child->is_element()) continue;

        DomElement* child_elem = child->as_element();
        if (child_elem->has_attribute("contenteditable")) {
            const char* ce = js_dom_normalize_contenteditable(
                child_elem->get_attribute("contenteditable"));
            if (ce && strcmp(ce, "false") == 0) {
                out_boundary->node = (DomNode*)elem;
                out_boundary->offset = have_empty_prefix ? empty_prefix_index : index;
                return true;
            }
        }
        if (_is_tag(child_elem, "br") || _is_tag(child_elem, "input") ||
            _is_tag(child_elem, "textarea") || _is_tag(child_elem, "hr")) {
            out_boundary->node = (DomNode*)elem;
            out_boundary->offset = index;
            return true;
        }
        if (js_dom_find_initial_editing_boundary(child_elem, child_preserve_ws, out_boundary)) {
            return true;
        }
        if (!have_empty_prefix) {
            have_empty_prefix = true;
            empty_prefix_index = index;
        }
    }
    return false;
}

static bool js_dom_selection_is_inside_element(DomSelection* selection,
                                               DomElement* elem) {
    if (!selection || !elem || selection->range_count == 0 ||
        !selection->ranges[0]) {
        return false;
    }
    DomRange* range = selection->ranges[0];
    DomNode* root = (DomNode*)elem;
    return range->start.node && range->end.node &&
        js_dom_node_contains(root, range->start.node) &&
        js_dom_node_contains(root, range->end.node);
}

static void js_dom_focus_set_selection_for_element(DocState* state, DomElement* elem) {
    if (!state || !elem) return;
    const char* exc = nullptr;
    if (tc_is_text_control_elem(elem) && elem->parent) {
        tc_ensure_init(elem);
        FormControlProp* form = elem->form;
        tc_set_active_element(state, elem);
        tc_set_last_focused_text_control(state, elem);
        if (form) {
            state_store_set_text_control_selection(state, elem,
                form->selection_start, form->selection_end,
                form->selection_direction);
        }
        return;
    }

    if (js_dom_is_editing_host(elem)) {
        DomSelection* existing = state->dom_selection;
        bool selection_inside = js_dom_selection_is_inside_element(existing, elem);
        if (selection_inside) {
            return;
        }
        DomBoundary boundary = { (DomNode*)elem, 0 };
        js_dom_find_initial_editing_boundary(elem, false, &boundary);
        if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
            log_debug("js_dom_focus_selection_editing_host_failed: %s",
                      exc ? exc : "unknown");
        }
    }
}

extern "C" void js_dom_focus_if_editing_host_for_selection(void* dom_node) {
    DomNode* node = (DomNode*)dom_node;
    DomElement* elem = nullptr;
    while (node) {
        if (node->is_element()) {
            DomElement* candidate = node->as_element();
            if (js_dom_is_editing_host(candidate)) {
                elem = candidate;
                break;
            }
        }
        node = node->parent;
    }
    if (!elem) return;
    if (!js_dom_is_script_focusable(elem)) return;
    DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
    View* old_focus = state ? focus_get(state) : nullptr;
    js_document_active_element = elem;
    focus_set_programmatic(state, (View*)elem);
    if (old_focus != (View*)elem) js_dom_dispatch_focus_events(elem);
}

static Item js_dom_throw_syntax_error(const char* message) {
    Item name = js_name_item("SyntaxError");
    Item msg = js_name_item(message ? message : "SyntaxError");
    return js_throw_value(js_new_error_with_name(name, msg));
}
JS_FORWARD_ITEM(js_dom_throw_contenteditable_syntax_error, (void), js_dom_throw_syntax_error, ("Invalid contentEditable value"))

static void _collect_lookup_rec(DomElement* root, const char* query, Item collection,
                                int kind) {
    if (!root || !query) return;
    bool matches = false;
    if (kind == LIVE_LOOKUP_COLLECTION_TAG) {
        matches = root->tag_name && ((query[0] == '*' && query[1] == '\0') ||
            strcasecmp(root->tag_name, query) == 0);
    } else if (kind == LIVE_LOOKUP_COLLECTION_CLASS) {
        for (int i = 0; i < root->class_count; i++) {
            if (root->class_names[i] && strcmp(root->class_names[i], query) == 0) {
                matches = true;
                break;
            }
        }
    } else if (kind == LIVE_LOOKUP_COLLECTION_NAME) {
        const char* attr = root->get_attribute("name");
        matches = attr && strcmp(attr, query) == 0;
    }
    if (matches) js_array_push(collection, js_dom_wrap_element(root));
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            _collect_lookup_rec(child->as_element(), query, collection, kind);
        }
        child = child->next_sibling;
    }
}

// ============================================================================
// Helper: CSS selector parse + match
// ============================================================================

static CssSelectorGroup* parse_css_selector_group(const char* sel_text, Pool* pool) {
    if (!sel_text || !pool) return nullptr;
    size_t sel_len = strlen(sel_text);
    if (sel_len == 0) return nullptr;
    // DOM selector APIs take selector lists; the shared parser preserves comma-separated
    // alternatives used by editor hit-testing such as closest("td, th").
    return css_parse_selector_group_text(sel_text, sel_len, pool);
}

static DomElement* js_dom_selector_group_find_first(SelectorMatcher* matcher,
                                                    CssSelectorGroup* group,
                                                    DomElement* element,
                                                    bool include_element) {
    if (!matcher || !group || !element) return nullptr;

    if (include_element && selector_matcher_matches_group(matcher, group, element, nullptr)) {
        return element;
    }

    DomNode* child_node = element->first_child;
    while (child_node) {
        if (child_node->is_element()) {
            DomElement* found = js_dom_selector_group_find_first(
                matcher, group, child_node->as_element(), true);
            if (found) return found;
        }
        child_node = child_node->next_sibling;
    }
    return nullptr;
}

static bool js_dom_selector_group_result_contains(ArrayList* results, DomElement* element) {
    if (!results || !element) return false;
    for (int i = 0; i < results->length; i++) {
        if ((DomElement*)results->data[i] == element) return true;
    }
    return false;
}

static void js_dom_selector_group_collect_all(SelectorMatcher* matcher,
                                              CssSelectorGroup* group,
                                              DomElement* element,
                                              ArrayList* results,
                                              bool include_element) {
    if (!matcher || !group || !element || !results) return;

    if (include_element && selector_matcher_matches_group(matcher, group, element, nullptr) &&
            !js_dom_selector_group_result_contains(results, element)) {
        arraylist_append(results, element);
    }

    DomNode* child_node = element->first_child;
    while (child_node) {
        if (child_node->is_element()) {
            js_dom_selector_group_collect_all(
                matcher, group, child_node->as_element(), results, true);
        }
        child_node = child_node->next_sibling;
    }
}

// ============================================================================
// Helper: recursive textContent extraction
// ============================================================================

static void collect_text_content(DomNode* node, StrBuf* sb) {
    if (!node) return;
    if (js_dom_is_generated_pseudo_node(node)) return;

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->text && text->length > 0) {
            strbuf_append_str_n(sb, text->text, (int)text->length);
        }
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        DomNode* child = js_dom_first_script_visible_child(elem);
        while (child) {
            collect_text_content(child, sb);
            child = js_dom_next_script_visible_sibling(child);
        }
    }
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_ascii_space, (char ch), (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f'))

static bool js_dom_style_decl_name_matches(const char* seg, const char* end,
                                           const char* prop_name,
                                           const char** colon_out = nullptr) {
    if (colon_out) *colon_out = nullptr;
    if (!seg || !end || !prop_name || end < seg) return false;
    const char* colon = nullptr;
    for (const char* p = seg; p < end; p++) {
        if (*p == ':') {
            colon = p;
            break;
        }
    }
    if (!colon) return false;
    const char* name_start = seg;
    const char* name_end = colon;
    while (name_start < name_end && js_dom_ascii_space(*name_start)) name_start++;
    while (name_end > name_start && js_dom_ascii_space(name_end[-1])) name_end--;
    size_t name_len = (size_t)(name_end - name_start);
    bool matches = strlen(prop_name) == name_len &&
        strncasecmp(name_start, prop_name, name_len) == 0;
    if (matches && colon_out) *colon_out = colon;
    return matches;
}

static bool js_dom_style_decl_value(const char* style_text,
                                    const char* prop_name,
                                    char* out,
                                    size_t out_size) {
    if (out && out_size > 0) out[0] = '\0';
    if (!style_text || !prop_name || !out || out_size == 0) return false;

    const char* seg = style_text;
    while (*seg) {
        const char* end = strchr(seg, ';');
        if (!end) end = seg + strlen(seg);

        const char* colon = nullptr;
        if (js_dom_style_decl_name_matches(seg, end, prop_name, &colon)) {
                const char* value_start = colon + 1;
                const char* value_end = end;
                while (value_start < value_end &&
                       js_dom_ascii_space(*value_start)) {
                    value_start++;
                }
                while (value_end > value_start &&
                       js_dom_ascii_space(value_end[-1])) {
                    value_end--;
                }

                size_t value_len = (size_t)(value_end - value_start);
                if (value_len >= out_size) value_len = out_size - 1;
                memcpy(out, value_start, value_len);
                out[value_len] = '\0';
                return true;
        }

        seg = *end ? end + 1 : end;
    }
    return false;
}

static bool js_dom_update_inline_style_attribute(DomElement* elem,
                                                 const char* prop_name,
                                                 const char* value,
                                                 const char* priority) {
    if (!elem || !prop_name || !value) return false;
    // CSS transitions compare the pre-change and post-change computed styles;
    // capture the former before replacing the durable inline declaration.
    css_transition_capture_before_change(elem,
        css_property_code_from_name(prop_name));
    const char* old_style = dom_element_get_inline_style(elem);
    size_t old_len = old_style ? strlen(old_style) : 0;
    StrBuf* updated = strbuf_new_cap((int)(old_len + strlen(prop_name) +
                                           strlen(value) + 32));
    if (!updated) return false;

    const char* seg = old_style ? old_style : "";
    while (*seg) {
        const char* end = strchr(seg, ';');
        if (!end) end = seg + strlen(seg);
        if (!js_dom_style_decl_name_matches(seg, end, prop_name)) {
            while (seg < end && js_dom_ascii_space(*seg)) seg++;
            while (end > seg && js_dom_ascii_space(end[-1])) end--;
            if (end > seg) {
                if (updated->length > 0) strbuf_append_char(updated, ' ');
                strbuf_append_str_n(updated, seg, (int)(end - seg));
                strbuf_append_char(updated, ';');
            }
        }
        seg = *end ? end + 1 : end;
    }

    if (value[0]) {
        if (updated->length > 0) strbuf_append_char(updated, ' ');
        strbuf_append_str(updated, prop_name);
        strbuf_append_str(updated, ": ");
        strbuf_append_str(updated, value);
        if (priority && priority[0]) {
            strbuf_append_str(updated, " !");
            strbuf_append_str(updated, priority);
        }
        strbuf_append_char(updated, ';');
    }

    // The serialized attribute is the durable source for later recascade;
    // updating only specified_style loses CSSOM writes on the next subtree pass.
    bool applied = elem->set_attribute("style",
        updated->str ? updated->str : "");
    strbuf_free(updated);
    return applied;
}

static float js_dom_parse_positive_css_dimension(const char* value) {
    if (!value) return 0.0f;
    while (js_dom_ascii_space(*value)) value++;
    if (!*value) return 0.0f;
    char* end = nullptr;
    double parsed = strtod(value, &end);
    if (end == value || parsed <= 0.0) return 0.0f;
    while (end && js_dom_ascii_space(*end)) end++;
    if (end && *end && strncasecmp(end, "px", 2) != 0) return 0.0f;
    return (float)parsed;
}

static float js_dom_inline_css_dimension(DomElement* elem,
                                         const char* prop_name) {
    if (!elem || !prop_name) return 0.0f;
    const char* style = elem->get_attribute("style");
    char value[64];
    if (!js_dom_style_decl_value(style, prop_name, value, sizeof(value))) {
        return 0.0f;
    }
    return js_dom_parse_positive_css_dimension(value);
}

static float js_dom_computed_css_dimension(DomElement* elem, bool width_axis) {
    if (!elem) return 0.0f;
    char value[64];
    CssPropertyCode property = width_axis ? CSS_PROPERTY_WIDTH : CSS_PROPERTY_HEIGHT;
    if (!css_prop_serialize_computed(elem, property, 0, value, sizeof(value))) {
        return 0.0f;
    }
    return js_dom_parse_positive_css_dimension(value);
}

static int64_t js_dom_headless_dimension(DomElement* elem, bool width_axis) {
    if (!elem) return 0;
    float layout_value = width_axis ? elem->width : elem->height;
    if (layout_value > 0.0f) return (int64_t)(layout_value + 0.5f);

    float css_value = js_dom_inline_css_dimension(
        elem, width_axis ? "width" : "height");
    if (css_value <= 0.0f) {
        css_value = js_dom_computed_css_dimension(elem, width_axis);
    }
    if (css_value > 0.0f) return (int64_t)(css_value + 0.5f);

    StrBuf* text = strbuf_new_cap(32);
    collect_text_content((DomNode*)elem, text);
    size_t text_len = text ? text->length : 0;
    if (text) strbuf_free(text);
    if (width_axis) return text_len > 0 ? (int64_t)text_len : 1;
    return 1;
}

static int64_t js_dom_geometry_dimension(DomElement* elem, bool width_axis) {
    if (!elem) return 0;
    js_dom_ensure_geometry_snapshot(elem->doc);
    float layout_value = width_axis ? elem->width : elem->height;
    if (layout_value > 0.0f) return (int64_t)(layout_value + 0.5f);
    // Load-time scripts execute before the first host-loop commit. A resolved
    // CSS length is a declaration lookup, not a reflow, and lets those scripts
    // initialize against their declared container size without flushing layout.
    return js_dom_headless_dimension(elem, width_axis);
}

// ============================================================================
// Helper: recursive innerHTML serialization
// ============================================================================

static void collect_html_attr_value(const char* value, StrBuf* sb) {
    if (!value) return;
    for (const char* p = value; *p; p++) {
        if (*p == '&') strbuf_append_str(sb, "&amp;");
        else if (*p == '"') strbuf_append_str(sb, "&quot;");
        else strbuf_append_char(sb, *p);
    }
}

static void collect_html_text_value(const char* value, size_t length,
                                    StrBuf* sb) {
    if (!value || !sb) return;
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (ch == '&') strbuf_append_str(sb, "&amp;");
        else if (ch == '<') strbuf_append_str(sb, "&lt;");
        else if (ch == '>') strbuf_append_str(sb, "&gt;");
        else if (ch == 0xC2 && i + 1 < length &&
                 (unsigned char)value[i + 1] == 0xA0) {
            // HTML fragment serialization canonicalizes a text-node NBSP to
            // its named reference; emitting the raw code point made editing
            // results disagree with browser innerHTML.
            strbuf_append_str(sb, "&nbsp;");
            i++;
        } else {
            strbuf_append_char(sb, (char)ch);
        }
    }
}

static void collect_inner_html(DomNode* node, StrBuf* sb) {
    if (!node) return;
    if (js_dom_is_generated_pseudo_node(node)) return;

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->text && text->length > 0) {
            collect_html_text_value(text->text, text->length, sb);
        }
        return;
    }

    if (node->is_comment()) {
        DomComment* comment = node->as_comment();
        strbuf_append_str(sb, "<!--");
        if (comment->content && comment->length > 0) {
            strbuf_append_str_n(sb, comment->content, (int)comment->length);
        }
        strbuf_append_str(sb, "-->");
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        // opening tag
        strbuf_append_char(sb, '<');
        strbuf_append_str(sb, elem->tag_name ? elem->tag_name : "unknown");

        int attr_count = 0;
        const char** attr_names = elem->attribute_names(&attr_count);
        if (attr_names) {
            for (int i = 0; i < attr_count; i++) {
                const char* name = attr_names[i];
                const char* value = elem->get_attribute(name);
                if (!name) continue;
                if (js_dom_is_internal_attr(name)) continue;
                strbuf_append_char(sb, ' ');
                strbuf_append_str(sb, name);
                strbuf_append_str(sb, "=\"");
                // A present valueless HTML attribute serializes with an empty
                // value; null here represents presence, not attribute absence.
                if (value) collect_html_attr_value(value, sb);
                strbuf_append_char(sb, '"');
            }
        }
        strbuf_append_char(sb, '>');

        // children
        DomNode* child = js_dom_first_script_visible_child(elem);
        while (child) {
            collect_inner_html(child, sb);
            child = js_dom_next_script_visible_sibling(child);
        }

        // closing tag (skip void elements)
        const char* tag = elem->tag_name;
        if (tag && strcmp(tag, "br") != 0 && strcmp(tag, "hr") != 0 &&
            strcmp(tag, "img") != 0 && strcmp(tag, "input") != 0 &&
            strcmp(tag, "meta") != 0 && strcmp(tag, "link") != 0) {
            strbuf_append_str(sb, "</");
            strbuf_append_str(sb, tag);
            strbuf_append_char(sb, '>');
        }
    }
}

static void collect_xml_attr_value(const char* value, StrBuf* sb) {
    if (!value || !sb) return;
    for (const char* p = value; *p; p++) {
        if (*p == '&') strbuf_append_str(sb, "&amp;");
        else if (*p == '<') strbuf_append_str(sb, "&lt;");
        else if (*p == '"') strbuf_append_str(sb, "&quot;");
        else if (*p == '\r') strbuf_append_str(sb, "&#13;");
        else strbuf_append_char(sb, *p);
    }
}

static void collect_xml_text_value(const char* value, size_t length, StrBuf* sb) {
    if (!value || !sb) return;
    for (size_t i = 0; i < length; i++) {
        char ch = value[i];
        if (ch == '&') strbuf_append_str(sb, "&amp;");
        else if (ch == '<') strbuf_append_str(sb, "&lt;");
        else if (ch == '>') strbuf_append_str(sb, "&gt;");
        else strbuf_append_char(sb, ch);
    }
}

static void collect_xml_node(DomNode* node, StrBuf* sb) {
    if (!node || !sb || js_dom_is_generated_pseudo_node(node)) return;
    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text && text->text && text->length > 0) {
            collect_xml_text_value(text->text, text->length, sb);
        }
        return;
    }
    if (node->is_comment()) {
        DomComment* comment = node->as_comment();
        strbuf_append_str(sb, "<!--");
        if (comment && comment->content && comment->length > 0) {
            strbuf_append_str_n(sb, comment->content, (int)comment->length);
        }
        strbuf_append_str(sb, "-->");
        return;
    }
    if (!node->is_element()) return;

    DomElement* elem = node->as_element();
    const char* tag = elem && elem->tag_name ? elem->tag_name : "";
    if (strcmp(tag, "#document-fragment") == 0) {
        for (DomNode* child = js_dom_first_script_visible_child(elem); child;
             child = js_dom_next_script_visible_sibling(child)) {
            collect_xml_node(child, sb);
        }
        return;
    }

    strbuf_append_char(sb, '<');
    strbuf_append_str(sb, tag);
    int attr_count = 0;
    const char** attr_names = elem->attribute_names(&attr_count);
    bool has_xlink_attr = false;
    for (int i = 0; attr_names && i < attr_count; i++) {
        const char* name = attr_names[i];
        if (!name || js_dom_is_internal_attr(name)) continue;
        char xlink_name[128];
        snprintf(xlink_name, sizeof(xlink_name), "__lambda_xlink_%s", name);
        if (elem->get_attribute(xlink_name)) {
            has_xlink_attr = true;
            break;
        }
    }
    if (has_xlink_attr && !elem->get_attribute("xmlns:xlink")) {
        // XLink attributes mirror an unprefixed renderer attribute internally;
        // XMLSerializer must restore their qualified XML identity on output.
        strbuf_append_str(sb, " xmlns:xlink=\"http://www.w3.org/1999/xlink\"");
    }
    for (int i = 0; attr_names && i < attr_count; i++) {
        const char* name = attr_names[i];
        if (!name || js_dom_is_internal_attr(name)) continue;
        char xlink_name[128];
        snprintf(xlink_name, sizeof(xlink_name), "__lambda_xlink_%s", name);
        bool is_xlink_attr = elem->get_attribute(xlink_name) != nullptr;
        strbuf_append_char(sb, ' ');
        if (is_xlink_attr) strbuf_append_str(sb, "xlink:");
        strbuf_append_str(sb, name);
        strbuf_append_str(sb, "=\"");
        const char* value = elem->get_attribute(name);
        if (value) collect_xml_attr_value(value, sb);
        strbuf_append_char(sb, '"');
    }

    DomNode* child = js_dom_first_script_visible_child(elem);
    if (!child) {
        strbuf_append_str(sb, "/>");
        return;
    }
    strbuf_append_char(sb, '>');
    while (child) {
        collect_xml_node(child, sb);
        child = js_dom_next_script_visible_sibling(child);
    }
    strbuf_append_str(sb, "</");
    strbuf_append_str(sb, tag);
    strbuf_append_char(sb, '>');
}
JS_FORWARD_STATIC_ITEM(js_dom_xml_serializer_constructor, (void), make_js_undefined, ())

static Item js_dom_xml_serializer_serialize_to_string(Item node_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(node_item);
    if (!node) {
        DomDocument* doc = js_document_proxy_doc_from_item(node_item);
        node = doc ? (DomNode*)doc->root : nullptr;
    }
    if (!node) return js_throw_type_error("XMLSerializer.serializeToString requires a Node");

    // ModelXmlSerializer exports MaxGraph's detached XML trees through this
    // standard DOM API; serializing the live node preserves namespaces and
    // attributes instead of substituting a format-specific graph snapshot.
    StrBuf* sb = strbuf_new_cap(256);
    collect_xml_node(node, sb);
    String* result = heap_create_name(sb->str ? sb->str : "");
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}
JS_FORWARD_STATIC_VOID( js_dom_install_xml_serializer_global, (void), js_dom_install_native_constructor_global, ("XMLSerializer", js_dom_xml_serializer_constructor, "serializeToString", js_dom_xml_serializer_serialize_to_string))

static void js_dom_collapse_selection_before_child_replace(DomElement* elem,
                                                           const char* context) {
    if (!elem) return;
    DocState* state = js_dom_state_for_nodes((DomNode*)elem, nullptr);
    if (!state || !state->dom_selection ||
        state->dom_selection->range_count == 0) {
        return;
    }
    DomRange* selected_range = state->dom_selection->ranges[0];
    if (!selected_range ||
        (!js_dom_node_contains((DomNode*)elem, selected_range->start.node) &&
         !js_dom_node_contains((DomNode*)elem, selected_range->end.node))) {
        return;
    }

    DomBoundary boundary = { (DomNode*)elem, 0 };
    const char* exc = nullptr;
    if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
        log_debug("js_dom_collapse_selection_before_child_replace: %s rejected: %s",
                  context ? context : "replace children", exc ? exc : "?");
        state_store_selection_clear(state);
        state_store_caret_clear(state);
        return;
    }
    js_dom_queue_selectionchange(state->dom_selection);
}

static bool js_dom_remove_backed_child(DomElement* parent, DomNode* child);

static bool js_dom_replace_inner_html(DomElement* elem, const char* html_str,
                                      bool notify_mutation) {
    if (!elem || !html_str) return false;
    elem = js_dom_prepare_children_for_mutation(elem);
    if (!elem) return false;
    DomDocument* doc = elem->doc;

    js_dom_collapse_selection_before_child_replace(elem, "innerHTML");

    while (elem->first_child) {
        DomNode* child = elem->first_child;
        dom_pre_remove(child, notify_mutation);
        // Observers pin their removedNodes while the wrapper is still live.
        // MarkEditor may retire it as part of the backing deletion below.
        js_dom_observers_mutation_notify(DOM_JS_MUTATION_CHILD_REMOVE,
                                         child, elem, nullptr, nullptr);
        // Remove each child through both representations. Detaching the DOM
        // list before editing Mark leaves lifecycle pins registered for nodes
        // that MarkEditor subsequently retires.
        if (!js_dom_remove_backed_child(elem, child)) return false;
        // MarkEditor relinks the surviving wrappers while removing a backing
        // child. Re-read first_child rather than retaining next_sibling, which
        // may have been retired by that relink.
    }

    if (html_str[0] != '\0') {
        if (!doc || !doc->input) return false;

        Html5Parser* parser = html5_fragment_parser_create(
            doc->document_pool, doc->node_arena, doc->input);
        if (!parser) return false;

        html5_fragment_parse(parser, html_str);
        Element* body_elem = html5_fragment_get_body(parser);
        if (!body_elem) return false;

        for (int64_t i = 0; i < body_elem->length; i++) {
            TypeId type = get_type_id(body_elem->items[i]);
            if (type == LMD_TYPE_ELEMENT) {
                DomElement* child_dom = build_dom_tree_from_element(
                    body_elem->items[i].element, doc, nullptr);
                if (child_dom && elem->append_child(child_dom)) {
                    dom_post_insert((DomNode*)elem, (DomNode*)child_dom,
                                    notify_mutation);
                    js_dom_observers_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT,
                                                     child_dom, elem,
                                                     nullptr, nullptr);
                }
            } else if (type == LMD_TYPE_STRING) {
                String* s = js_dom_fragment_text(body_elem->items[i]);
                if (!s) continue;
                DomText* text_dom = elem->append_text(s->chars);
                if (text_dom) {
                    dom_post_insert((DomNode*)elem, (DomNode*)text_dom,
                                    notify_mutation);
                    js_dom_observers_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT,
                                                     text_dom, elem,
                                                     nullptr, nullptr);
                }
            }
        }
    }

    js_dom_register_named_elements(elem);
    _select_refresh_cached_selected_options_for_node((DomNode*)elem);
    if (notify_mutation) {
        // innerHTML replaces children under the same parent; preserving the
        // remove/insert records avoids broad TREE_REPLACE fallback for local edits.
        js_dom_mutation_notify();
    }
    log_debug("js_dom_replace_inner_html: replaced <%s>",
              elem->tag_name ? elem->tag_name : "?");
    return true;
}

static DomElement* js_dom_parse_html_fragment(DomDocument* doc,
                                              const char* html_str) {
    if (!doc || !doc->input || !html_str) return nullptr;

    Html5Parser* parser = html5_fragment_parser_create(
        doc->document_pool, doc->node_arena, doc->input);
    if (!parser) return nullptr;
    html5_fragment_parse(parser, html_str);
    Element* body_elem = html5_fragment_get_body(parser);
    if (!body_elem) return nullptr;

    DomElement* fragment = dom_document_fragment_create(doc);
    if (!fragment) return nullptr;
    for (int64_t i = 0; i < body_elem->length; i++) {
        TypeId type = get_type_id(body_elem->items[i]);
        if (type == LMD_TYPE_ELEMENT) {
            DomElement* child = build_dom_tree_from_element(
                body_elem->items[i].element, doc, nullptr);
            if (child) ((DomNode*)fragment)->append_child((DomNode*)child);
        } else if (type == LMD_TYPE_STRING) {
            String* text = js_dom_fragment_text(body_elem->items[i]);
            if (!text) continue;
            DomText* child = dom_text_create_detached(text, doc);
            if (child) ((DomNode*)fragment)->append_child((DomNode*)child);
        }
    }
    return fragment;
}

static bool js_dom_exec_insert_html(DomDocument* doc, const char* html_str) {
    if (!doc || !html_str) return false;
    DocState* state = doc->state ? doc->state : js_dom_current_state();
    DomSelection* selection = state ? state->dom_selection : nullptr;
    if (!selection || selection->range_count == 0 || !selection->ranges[0]) {
        return false;
    }
    DomRange* range = selection->ranges[0];
    if (!range->start.node || !range->end.node) return false;

    DomElement* fragment = js_dom_parse_html_fragment(doc, html_str);
    if (!fragment) return false;

    ArrayList* inserted = arraylist_new(8);
    if (!inserted) return false;
    for (DomNode* child = fragment->first_child; child; child = child->next_sibling) {
        if (!arraylist_append(inserted, child)) {
            arraylist_free(inserted);
            return false;
        }
    }

    bool replaced_selection = !dom_range_collapsed(range);
    DomNode* replace_root = replaced_selection ? dom_range_common_ancestor(range) : nullptr;
    const char* exception = nullptr;
    if (replaced_selection && !dom_range_delete_contents(range, &exception)) {
        arraylist_free(inserted);
        return false;
    }
    if (inserted->length > 0 &&
        !dom_range_insert_node(range, (DomNode*)fragment, &exception)) {
        arraylist_free(inserted);
        return false;
    }

    // Editor.js delegates inline paste to execCommand; the bridge removed by
    // DOM API consolidation must report the Range mutation that saves editor state.
    if (replaced_selection && replace_root) {
        js_dom_mutation_notify(DOM_JS_MUTATION_TREE_REPLACE,
                               replace_root, replace_root);
    }
    for (int i = 0; i < inserted->length; i++) {
        DomNode* child = (DomNode*)inserted->data[i];
        js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT, child, child->parent);
    }

    if (inserted->length > 0) {
        DomNode* last = (DomNode*)inserted->data[inserted->length - 1];
        uint32_t end = dom_node_boundary_length(last);
        if (!dom_selection_collapse(selection, last, end, &exception)) {
            arraylist_free(inserted);
            return false;
        }
        js_dom_queue_selectionchange(selection);
    }
    arraylist_free(inserted);
    return true;
}

extern "C" Item js_dom_document_exec_command_bridge(Item command_item,
                                                      Item value_item) {
    JS_ROOTS(roots,
        command_root, command_item,
        value_root, value_item,
        string_root, js_to_string(value_root.get()));
    const char* command = fn_to_cstr(command_root.get());
    const char* value = fn_to_cstr(string_root.get());
    if (!command || !value || strcasecmp(command, "insertHTML") != 0) {
        return (Item){.item = ITEM_FALSE};
    }

    // A precise root keeps the JS string live, while this owned copy keeps the
    // parser input stable if fragment construction triggers a moving collection.
    char* stable_value = mem_strdup(value, MEM_CAT_JS_RUNTIME);
    if (!stable_value) return (Item){.item = ITEM_FALSE};
    bool inserted = js_dom_exec_insert_html(
        (DomDocument*)js_dom_get_document(), stable_value);
    mem_free(stable_value);
    return (Item){.item = b2it(inserted ? 1 : 0)};
}

// ============================================================================
// Helper: get uppercase tag name (per DOM spec)
// ============================================================================

static String* uppercase_tag_name(const char* tag_name) {
    if (!tag_name) return heap_create_name("");
    size_t len = strlen(tag_name);
    // allocate temp on stack for short names
    char buf[64];
    char* upper = (len < sizeof(buf)) ? buf : (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)tag_name[i]);
    }
    upper[len] = '\0';
    String* result = heap_create_name(upper);
    if (upper != buf) mem_free(upper);
    return result;
}

static int64_t js_dom_text_measure(DomNode* node) {
    if (!node) return 0;
    if (node->is_text()) {
        return (int64_t)dom_text_utf16_length(node->as_text());
    }
    if (!node->is_element()) return 0;
    int64_t total = 0;
    for (DomNode* child = node->as_element()->first_child; child;
         child = child->next_sibling) {
        total += js_dom_text_measure(child);
    }
    return total;
}

static bool js_dom_text_offset_before_node(DomNode* node, DomNode* target,
                                           int64_t* offset) {
    if (!node || !target || !offset) return false;
    if (node == target) return true;
    if (node->is_text()) {
        *offset += (int64_t)dom_text_utf16_length(node->as_text());
        return false;
    }
    if (!node->is_element()) return false;
    for (DomNode* child = node->as_element()->first_child; child;
         child = child->next_sibling) {
        if (js_dom_text_offset_before_node(child, target, offset))
            return true;
    }
    return false;
}

static DomElement* js_dom_inline_offset_scope(DomElement* elem) {
    DomElement* fallback = elem && elem->parent && elem->parent->is_element()
        ? elem->parent->as_element() : nullptr;
    for (DomNode* current = elem ? elem->parent : nullptr; current;
         current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* current_elem = current->as_element();
        const char* editable =
            current_elem->get_attribute("contenteditable");
        if (editable) return current_elem;
        if (current_elem->tag_name &&
            strcasecmp(current_elem->tag_name, "body") == 0) {
            return current_elem;
        }
    }
    return fallback;
}

static int64_t js_dom_synthetic_inline_offset_left(DomElement* elem) {
    if (!elem) return 0;
    DomElement* scope = js_dom_inline_offset_scope(elem);
    if (!scope) return 0;
    int64_t offset = 0;
    if (!js_dom_text_offset_before_node((DomNode*)scope, (DomNode*)elem,
                                        &offset)) {
        return 0;
    }
    int64_t width = js_dom_text_measure((DomNode*)elem);
    return offset > 0 || width > 0 ? offset : 0;
}

// ============================================================================
// Document Method Dispatcher
// ============================================================================

static Item js_dom_create_document_fragment(DomDocument* doc) {
    if (!doc || !doc->input) return ItemNull;

    // A fragment owns no Mark tree: its children are moved into the destination
    // element, which is the sole owner of their persistent backing entries.
    DomElement* fragment = dom_element_create(doc, "#document-fragment", nullptr);
    return fragment ? js_dom_wrap_element(fragment) : ItemNull;
}

static const char* JS_TREE_WALKER_ROOT = "__lambda_tree_walker_root";
static const char* JS_TREE_WALKER_CURRENT = "currentNode";
static const char* JS_TREE_WALKER_WHAT_TO_SHOW = "whatToShow";

enum JsDomTreeWalkerStep {
    JS_TREE_WALKER_STEP_NEXT_NODE = 0,
    JS_TREE_WALKER_STEP_FIRST_CHILD,
    JS_TREE_WALKER_STEP_NEXT_SIBLING,
};

static uint32_t js_dom_tree_walker_node_mask(DomNode* node) {
    if (!node) return 0;
    uint32_t node_type = node->is_text() ? 3u : (uint32_t)node->node_type;
    if (node_type == 0 || node_type > 32) return 0;
    return 1u << (node_type - 1u);
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_tree_walker_accepts, (DomNode* node, uint32_t what_to_show), ((js_dom_tree_walker_node_mask(node) & what_to_show) != 0))

static DomNode* js_dom_tree_walker_first_child_raw(DomNode* node) {
    if (!node || !node->is_element()) return nullptr;
    return js_dom_first_script_visible_child(node->as_element());
}

static DomNode* js_dom_tree_walker_next_raw(DomNode* root, DomNode* current) {
    if (!root || !current) return nullptr;
    DomNode* child = js_dom_tree_walker_first_child_raw(current);
    if (child) return child;
    for (DomNode* node = current; node && node != root; node = node->parent) {
        if (node->next_sibling) return node->next_sibling;
    }
    return nullptr;
}

static DomNode* js_dom_tree_walker_next_matching(DomNode* root,
                                                  DomNode* current,
                                                  uint32_t what_to_show) {
    for (DomNode* node = js_dom_tree_walker_next_raw(root, current);
         node;
         node = js_dom_tree_walker_next_raw(root, node)) {
        if (js_dom_tree_walker_accepts(node, what_to_show)) return node;
    }
    return nullptr;
}

static Item js_dom_tree_walker_advance(Item walker_item, JsDomTreeWalkerStep step) {
    JS_ROOTS(roots, walker_root, walker_item, node_root, ItemNull, result_root, ItemNull);
    Item root_item = js_get_key_default(walker_root.get(), js_string_key(JS_TREE_WALKER_ROOT));
    Item current_item = js_get_key_default(walker_root.get(), js_string_key(JS_TREE_WALKER_CURRENT));
    DomNode* root = (DomNode*)js_dom_unwrap_element(root_item);
    DomNode* current = (DomNode*)js_dom_unwrap_element(current_item);
    if (!root || !current) return ItemNull;

    uint32_t what_to_show = js_dom_to_u32(
        js_get_key_default(walker_root.get(), js_string_key(JS_TREE_WALKER_WHAT_TO_SHOW)));
    DomNode* next = nullptr;
    if (step == JS_TREE_WALKER_STEP_FIRST_CHILD) {
        for (DomNode* node = js_dom_tree_walker_first_child_raw(current);
             node;
             node = js_dom_tree_walker_next_raw(root, node)) {
            if (js_dom_tree_walker_accepts(node, what_to_show)) {
                next = node;
                break;
            }
        }
    } else if (step == JS_TREE_WALKER_STEP_NEXT_SIBLING) {
        for (DomNode* node = current->next_sibling;
             node;
             node = node->next_sibling) {
            if (js_dom_tree_walker_accepts(node, what_to_show)) {
                next = node;
                break;
            }
        }
    } else {
        next = js_dom_tree_walker_next_matching(root, current, what_to_show);
    }
    if (!next) return ItemNull;

    // Sanitizer walks detached editor documents; keep the wrapper rooted while
    // publishing it as currentNode so a compacting collection cannot leave the
    // walker pointing at the pre-move wrapper during the next traversal step.
    node_root.set(js_dom_wrap_element(next));
    if (node_root.get().item == ItemNull.item) return ItemNull;
    js_set_key_default(walker_root.get(), js_string_key(JS_TREE_WALKER_CURRENT),
                    node_root.get());
    result_root.set(node_root.get());
    return result_root.get();
}

#define JS_DOM_TREE_WALKER_METHOD(name, step) \
static Item name(Item walker_item) { \
    return js_dom_tree_walker_advance(walker_item, step); \
}
JS_DOM_TREE_WALKER_METHOD(js_dom_tree_walker_next_node_method,
    JS_TREE_WALKER_STEP_NEXT_NODE)
JS_DOM_TREE_WALKER_METHOD(js_dom_tree_walker_first_child_method,
    JS_TREE_WALKER_STEP_FIRST_CHILD)
JS_DOM_TREE_WALKER_METHOD(js_dom_tree_walker_next_sibling_method,
    JS_TREE_WALKER_STEP_NEXT_SIBLING)
#undef JS_DOM_TREE_WALKER_METHOD

extern "C" Item js_dom_create_tree_walker_bridge(Item root_item, Item what_to_show_item) {
    if (!js_dom_unwrap_element(root_item)) return ItemNull;
    RootFrame roots(4);
    Rooted<Item> root_root(roots, root_item);
    Rooted<Item> walker_root(roots, js_new_object());
    Rooted<Item> method_root(roots, ItemNull);
    if (walker_root.get().item == ItemNull.item) return ItemNull;
    uint32_t what_to_show = js_dom_to_u32(what_to_show_item);
    js_set_key_default(walker_root.get(), js_string_key(JS_TREE_WALKER_ROOT), root_root.get());
    js_set_key_default(walker_root.get(), js_string_key(JS_TREE_WALKER_CURRENT), root_root.get());
    js_set_key_default(walker_root.get(), js_string_key(JS_TREE_WALKER_WHAT_TO_SHOW),
                    (Item){.item = i2it((int64_t)what_to_show)});
    Item bound_args[1] = {walker_root.get()};
    method_root.set(js_bind_function(
        js_new_native_function(js_dom_tree_walker_next_node_method),
        make_js_undefined(), bound_args, 1));
    js_set_key_cstr(walker_root.get(), "nextNode", method_root.get());
    bound_args[0] = walker_root.get();
    method_root.set(js_bind_function(
        js_new_native_function(js_dom_tree_walker_first_child_method),
        make_js_undefined(), bound_args, 1));
    js_set_key_cstr(walker_root.get(), "firstChild", method_root.get());
    bound_args[0] = walker_root.get();
    method_root.set(js_bind_function(
        js_new_native_function(js_dom_tree_walker_next_sibling_method),
        make_js_undefined(), bound_args, 1));
    js_set_key_cstr(walker_root.get(), "nextSibling", method_root.get());
    return walker_root.get();
}

extern "C" Item js_dom_document_create_event_bridge(Item interface_name) {
    const char* interface_text = fn_to_cstr(interface_name);
    if (!interface_text) interface_text = "";
    if (strcmp(interface_text, "CustomEvent") == 0) {
        return js_create_custom_event_init("", false, false, false, ItemNull);
    }
    if (strcmp(interface_text, "TextEvent") == 0) {
        return js_create_text_event_init("", false, false, false, ItemNull, "");
    }
    return js_create_event_init("", false, false, false);
}

static bool js_dom_append_document_text(DomDocument* doc, const char* text) {
    DomElement* body = document_body_element(doc);
    if (!body) return false;

    const char* cursor = text;
    while (*cursor) {
        const char* br = nullptr;
        for (const char* scan = cursor; *scan; scan++) {
            if (*scan == '<' && strncasecmp(scan, "<br", 3) == 0) {
                const char* end = strchr(scan, '>');
                if (end) {
                    br = scan;
                    break;
                }
            }
        }
        size_t text_len = br ? (size_t)(br - cursor) : strlen(cursor);
        if (text_len > 0) {
            DomText* text_node = DomText::create_detached_copy(doc, cursor, text_len);
            if (text_node) {
                ((DomNode*)body)->append_child((DomNode*)text_node);
                dom_post_insert((DomNode*)body, (DomNode*)text_node);
            }
        }
        if (!br) break;
        const char* br_end = strchr(br, '>');
        MarkBuilder builder(doc->input);
        Item br_item = builder.element("br").final();
        DomElement* br_elem = dom_element_create(doc, "br", br_item.element);
        if (br_elem) {
            ((DomNode*)body)->append_child((DomNode*)br_elem);
            dom_post_insert((DomNode*)body, (DomNode*)br_elem);
        }
        cursor = br_end ? br_end + 1 : br + 3;
    }
    return true;
}


// ============================================================================
// Document Property Access
// ============================================================================

extern "C" Item js_dom_document_fonts_bridge(void) {
    if (!dom_ensure_roots()) return ItemNull;
    if (get_type_id(js_document_fonts_value) != LMD_TYPE_MAP) {
        // Root-range cleanup clears expired realm cache slots to zero; rebuild
        // the FontFaceSet instead of publishing that non-object as `fonts`.
        js_document_fonts_value = js_create_document_fonts_object();
    }
    return js_document_fonts_value;
}
JS_FORWARD_ITEM(js_dom_document_stylesheets_bridge, (void), js_cssom_get_document_stylesheets, ())

extern "C" Item js_dom_document_default_view_bridge(void* doc_ptr) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    if (!js_doc_has_browsing_context(doc)) {
        return ItemNull;
    }
    if (doc && doc != _js_main_document) {
        Item w = lookup_foreign_doc_wrapper(doc);
        return w.item ? w : ItemNull;
    }
    if (js_document_default_view.item != ITEM_NULL) {
        return js_document_default_view;
    }
    return ItemNull;
}
JS_FORWARD_ITEM(js_dom_document_implementation_bridge, (void), js_get_dom_implementation, ())
JS_FORWARD_EXPRESSION(Item, js_dom_document_design_mode_bridge, (void), (js_name_item(js_document_design_mode ? "on" : "off")))

extern "C" Item js_dom_document_active_element_bridge(void* doc_ptr) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    DomElement* root = doc ? doc->root : nullptr;
    DocState* state = js_dom_current_state();
    View* focused = focus_get(state);
    if (focused && focused->is_element()) {
        return js_dom_wrap_element(((DomNode*)focused)->as_element());
    }
    DomElement* active_element = tc_get_active_element(state);
    if (active_element && active_element->doc == doc) {
        return js_dom_wrap_element(active_element);
    }
    if (js_document_active_element &&
        js_document_active_element->doc == doc &&
        js_dom_node_is_connected((DomNode*)js_document_active_element)) {
        return js_dom_wrap_element(js_document_active_element);
    }
    DomElement* body = document_body_element(doc);
    if (body) return js_dom_wrap_element(body);
    return root ? js_dom_wrap_element(root) : ItemNull;
}

// The nine URL components that document, location and history all expose.
// One accessor row instead of a repeated strcmp chain per surface.
typedef const char* (*JsUrlComponentGet)(const Url*);
typedef struct JsUrlComponent {
    const char* name;
    JsUrlComponentGet get;
} JsUrlComponent;

static const JsUrlComponent k_url_components[] = {
    { "href",     url_get_href },
    { "protocol", url_get_protocol },
    { "hostname", url_get_hostname },
    { "port",     url_get_port },
    { "pathname", url_get_pathname },
    { "search",   url_get_search },
    { "hash",     url_get_hash },
    { "host",     url_get_host },
    { "origin",   url_get_origin },
};

// The component's value as a JS string, or false when `prop` is not one of
// them. A missing URL or component reads as "", as it does in a browser for a
// document that has not been navigated.
static bool js_url_component_item(const Url* url, const char* prop, Item* out) {
    if (!prop || !out) return false;
    for (size_t i = 0; i < sizeof(k_url_components) / sizeof(k_url_components[0]); i++) {
        if (strcmp(k_url_components[i].name, prop) != 0) continue;
        const char* value = url ? k_url_components[i].get(url) : NULL;
        *out = js_name_item(value ? value : "");
        return true;
    }
    return false;
}

static bool js_dom_form_named_getter_reserved_name(const char* prop);
// ----------------------------------------------------------------------
// DOM property identity
//
// The property-dispatch functions used to test a name with a chain of up to
// 138 strcmp calls. This table names every dispatched property once and
// resolves a name to an id with a single binary search; the chains become
// switches over the id.
//
// Rows are in ASCII order of the property name, which strcmp-based bsearch
// relies on; js_dom_prop_id_selftest() checks that in debug builds.
// ----------------------------------------------------------------------
#define JS_DOM_PROPS(X) \
    X(RANGE,                     "Range") \
    X(SELECTION,                 "Selection") \
    X(URL,                       "URL") \
    X(__PROTO__,                 "__proto__") \
    X(ACCEPT,                    "accept") \
    X(ACCEPT_CHARSET,            "acceptCharset") \
    X(ACTION,                    "action") \
    X(ACTIVE_ELEMENT,            "activeElement") \
    X(ATTRIBUTES,                "attributes") \
    X(AUTOCAPITALIZE,            "autocapitalize") \
    X(AUTOCOMPLETE,              "autocomplete") \
    X(AUTOCORRECT,               "autocorrect") \
    X(AUTOFOCUS,                 "autofocus") \
    X(BODY,                      "body") \
    X(CHARACTER_SET,             "characterSet") \
    X(CHARSET,                   "charset") \
    X(CHECKED,                   "checked") \
    X(CHILD_ELEMENT_COUNT,       "childElementCount") \
    X(CHILD_NODES,               "childNodes") \
    X(CHILDREN,                  "children") \
    X(CLASS_LIST,                "classList") \
    X(CLASS_NAME,                "className") \
    X(CLIENT_HEIGHT,             "clientHeight") \
    X(CLIENT_WIDTH,              "clientWidth") \
    X(COLS,                      "cols") \
    X(COMPAT_MODE,               "compatMode") \
    X(CONTENT,                   "content") \
    X(CONTENT_DOCUMENT,          "contentDocument") \
    X(CONTENT_EDITABLE,          "contentEditable") \
    X(CONTENT_TYPE,              "contentType") \
    X(CONTENT_WINDOW,            "contentWindow") \
    X(DATA,                      "data") \
    X(DATASET,                   "dataset") \
    X(DEFAULT_CHECKED,           "defaultChecked") \
    X(DEFAULT_SELECTED,          "defaultSelected") \
    X(DEFAULT_VALUE,             "defaultValue") \
    X(DEFAULT_VIEW,              "defaultView") \
    X(DESIGN_MODE,               "designMode") \
    X(DISABLED,                  "disabled") \
    X(DOCTYPE,                   "doctype") \
    X(DOCUMENT,                  "document") \
    X(DOCUMENT_ELEMENT,          "documentElement") \
    X(ELEMENTS,                  "elements") \
    X(ENCODING,                  "encoding") \
    X(ENCTYPE,                   "enctype") \
    X(ENTER_KEY_HINT,            "enterKeyHint") \
    X(FIRST_CHILD,               "firstChild") \
    X(FIRST_ELEMENT_CHILD,       "firstElementChild") \
    X(FONTS,                     "fonts") \
    X(FORM,                      "form") \
    X(FORM_ACTION,               "formAction") \
    X(FORM_ENCTYPE,              "formEnctype") \
    X(FORM_METHOD,               "formMethod") \
    X(FORM_NO_VALIDATE,          "formNoValidate") \
    X(FORM_TARGET,               "formTarget") \
    X(FORMS,                     "forms") \
    X(HASH,                      "hash") \
    X(HEAD,                      "head") \
    X(HEIGHT,                    "height") \
    X(HREF,                      "href") \
    X(HTML_FOR,                  "htmlFor") \
    X(ID,                        "id") \
    X(IMPLEMENTATION,            "implementation") \
    X(INDEX,                     "index") \
    X(INNER_HTML,                "innerHTML") \
    X(INNER_TEXT,                "innerText") \
    X(INPUT_MODE,                "inputMode") \
    X(IS_CONNECTED,              "isConnected") \
    X(IS_CONTENT_EDITABLE,       "isContentEditable") \
    X(LABEL,                     "label") \
    X(LAST_CHILD,                "lastChild") \
    X(LAST_ELEMENT_CHILD,        "lastElementChild") \
    X(LENGTH,                    "length") \
    X(LOCAL_NAME,                "localName") \
    X(LOCATION,                  "location") \
    X(MAX,                       "max") \
    X(MAX_LENGTH,                "maxLength") \
    X(METHOD,                    "method") \
    X(MIN,                       "min") \
    X(MIN_LENGTH,                "minLength") \
    X(MULTIPLE,                  "multiple") \
    X(NAME,                      "name") \
    X(NAMESPACE_URI,             "namespaceURI") \
    X(NEXT_ELEMENT_SIBLING,      "nextElementSibling") \
    X(NEXT_SIBLING,              "nextSibling") \
    X(NO_VALIDATE,               "noValidate") \
    X(NODE_NAME,                 "nodeName") \
    X(NODE_TYPE,                 "nodeType") \
    X(NODE_VALUE,                "nodeValue") \
    X(OFFSET_HEIGHT,             "offsetHeight") \
    X(OFFSET_LEFT,               "offsetLeft") \
    X(OFFSET_PARENT,             "offsetParent") \
    X(OFFSET_TOP,                "offsetTop") \
    X(OFFSET_WIDTH,              "offsetWidth") \
    X(OPEN,                      "open") \
    X(OPTIONS,                   "options") \
    X(OUTER_HTML,                "outerHTML") \
    X(OWNER_DOCUMENT,            "ownerDocument") \
    X(OWNER_SVGELEMENT,          "ownerSVGElement") \
    X(PARENT_ELEMENT,            "parentElement") \
    X(PARENT_NODE,               "parentNode") \
    X(PATTERN,                   "pattern") \
    X(PLACEHOLDER,               "placeholder") \
    X(PREFIX,                    "prefix") \
    X(PREVIOUS_ELEMENT_SIBLING,  "previousElementSibling") \
    X(PREVIOUS_SIBLING,          "previousSibling") \
    X(READ_ONLY,                 "readOnly") \
    X(READONLY,                  "readonly") \
    X(READY_STATE,               "readyState") \
    X(REQUIRED,                  "required") \
    X(ROWS,                      "rows") \
    X(SCROLL_HEIGHT,             "scrollHeight") \
    X(SCROLL_LEFT,               "scrollLeft") \
    X(SCROLL_TOP,                "scrollTop") \
    X(SCROLL_WIDTH,              "scrollWidth") \
    X(SELECT,                    "select") \
    X(SELECTED,                  "selected") \
    X(SELECTED_INDEX,            "selectedIndex") \
    X(SELECTED_OPTIONS,          "selectedOptions") \
    X(SELECTION_DIRECTION,       "selectionDirection") \
    X(SELECTION_END,             "selectionEnd") \
    X(SELECTION_START,           "selectionStart") \
    X(SET_RANGE_TEXT,            "setRangeText") \
    X(SET_SELECTION_RANGE,       "setSelectionRange") \
    X(SHEET,                     "sheet") \
    X(SIZE,                      "size") \
    X(SLOT,                      "slot") \
    X(SPELLCHECK,                "spellcheck") \
    X(SRCDOC,                    "srcdoc") \
    X(STEP,                      "step") \
    X(STYLE,                     "style") \
    X(STYLE_SHEETS,              "styleSheets") \
    X(TAB_INDEX,                 "tabIndex") \
    X(TAG_NAME,                  "tagName") \
    X(TARGET,                    "target") \
    X(TEXT,                      "text") \
    X(TEXT_CONTENT,              "textContent") \
    X(TEXT_LENGTH,               "textLength") \
    X(TITLE,                     "title") \
    X(TRANSFORM,                 "transform") \
    X(TYPE,                      "type") \
    X(VALIDATION_MESSAGE,        "validationMessage") \
    X(VALIDITY,                  "validity") \
    X(VALUE,                     "value") \
    X(WIDTH,                     "width") \
    X(WILL_VALIDATE,             "willValidate") \
    X(WRAP,                      "wrap") \
    X(WRITING_SUGGESTIONS,       "writingSuggestions")

typedef enum JsDomPropId {
    JS_DOM_PROP_NONE = 0,
#define JS_DOM_PROP_ENUM(name, text) JS_DOM_PROP_##name,
    JS_DOM_PROPS(JS_DOM_PROP_ENUM)
#undef JS_DOM_PROP_ENUM
    JS_DOM_PROP_COUNT
} JsDomPropId;

typedef struct JsDomPropRow {
    const char* name;
    JsDomPropId id;
} JsDomPropRow;

static const JsDomPropRow k_dom_props[] = {
#define JS_DOM_PROP_ROW(name, text) { text, JS_DOM_PROP_##name },
    JS_DOM_PROPS(JS_DOM_PROP_ROW)
#undef JS_DOM_PROP_ROW
};

static int js_dom_prop_row_compare(const void* key, const void* row) {
    return strcmp((const char*)key, ((const JsDomPropRow*)row)->name);
}

// JS_DOM_PROP_NONE for any name the dispatchers do not special-case.
static JsDomPropId js_dom_prop_id(const char* prop) {
    if (!prop) return JS_DOM_PROP_NONE;
    const JsDomPropRow* row = (const JsDomPropRow*)bsearch(prop, k_dom_props,
        sizeof(k_dom_props) / sizeof(k_dom_props[0]), sizeof(k_dom_props[0]),
        js_dom_prop_row_compare);
    return row ? row->id : JS_DOM_PROP_NONE;
}


extern "C" Item js_document_get_property(Item prop_name) {
    if (!_js_current_document) {
        log_debug("js_document_get_property: no document set");
        return ItemNull;
    }

    const char* prop = fn_to_cstr(prop_name);
    JsDomPropId prop_id = js_dom_prop_id(prop);
    if (!prop) return ItemNull;

    DomDocument* doc = _js_current_document;
    DomElement* root = doc->root;  // may be NULL for foreign docs created via createDocument

    // documentElement — the root <html> element
    if (prop_id == JS_DOM_PROP_DOCUMENT_ELEMENT) {
        return root ? js_dom_wrap_element(root) : ItemNull;
    }

    // body — the <body> element
    if (prop_id == JS_DOM_PROP_BODY) {
        DomNode* child = root ? root->first_child : nullptr;
        while (child) {
            if (child->is_element()) {
                DomElement* elem = child->as_element();
                if (elem->tag_name && strcasecmp(elem->tag_name, "body") == 0) {
                    return js_dom_wrap_element(elem);
                }
            }
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // head — the <head> element
    if (prop_id == JS_DOM_PROP_HEAD) {
        DomNode* child = root ? root->first_child : nullptr;
        while (child) {
            if (child->is_element()) {
                DomElement* elem = child->as_element();
                if (elem->tag_name && strcasecmp(elem->tag_name, "head") == 0) {
                    return js_dom_wrap_element(elem);
                }
            }
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // title — text of first <title> element
    if (prop_id == JS_DOM_PROP_TITLE) {
        // search in <head> first
        DomNode* child = root ? root->first_child : nullptr;
        while (child) {
            if (child->is_element()) {
                DomElement* elem = child->as_element();
                if (elem->tag_name && strcasecmp(elem->tag_name, "head") == 0) {
                    DomNode* hchild = elem->first_child;
                    while (hchild) {
                        if (hchild->is_element()) {
                            DomElement* title_elem = hchild->as_element();
                            if (title_elem->tag_name &&
                                strcasecmp(title_elem->tag_name, "title") == 0) {
                                StrBuf* sb = strbuf_new_cap(64);
                                collect_text_content((DomNode*)title_elem, sb);
                                String* result = heap_create_name(sb->str ? sb->str : "");
                                strbuf_free(sb);
                                return (Item){.item = s2it(result)};
                            }
                        }
                        hchild = hchild->next_sibling;
                    }
                }
            }
            child = child->next_sibling;
        }
        return js_name_item("");
    }

    // v12: URL — full document URL as string
    if (prop_id == JS_DOM_PROP_URL) {
        Url* url = doc->url;
        if (url) {
            const char* href = url_get_href(url);
            if (href) return js_name_item(href);
        }
        return js_name_item("");
    }

    // location-style URL access. We model document.location and bare
    // location as aliases of the document/window proxy itself.
    if (prop_id == JS_DOM_PROP_LOCATION || prop_id == JS_DOM_PROP_DOCUMENT) {
        return doc_to_proxy_item(doc);
    }
    {
        Item url_component = ItemNull;
        if (js_url_component_item(doc->url, prop, &url_component)) return url_component;
    }

    // readyState — legacy defaults to "complete"; the Phase 4 post-DOM
    // script scheduler updates this during modeled lifecycle transitions.
    if (prop_id == JS_DOM_PROP_READY_STATE) {
        const char* ready_state = doc->js.ready_state ? doc->js.ready_state : "complete";
        return js_name_item(ready_state);
    }

    if (prop_id == JS_DOM_PROP_FONTS) {
        return js_dom_document_fonts_bridge();
    }

    // compatMode
    if (prop_id == JS_DOM_PROP_COMPAT_MODE) {
        return js_name_item("CSS1Compat");
    }

    // F-1: document.forms — array of all <form> elements in the document.
    if (prop_id == JS_DOM_PROP_FORMS) {
        DomDocument* doc = _js_current_document;
        return js_dom_live_document_forms_bridge((void*)doc);
    }

    // characterSet / charset
    if (prop_id == JS_DOM_PROP_CHARACTER_SET || prop_id == JS_DOM_PROP_CHARSET) {
        return js_name_item("UTF-8");
    }

    // contentType
    if (prop_id == JS_DOM_PROP_CONTENT_TYPE) {
        return js_name_item("text/html");
    }

    // nodeType — DOCUMENT_NODE = 9
    if (prop_id == JS_DOM_PROP_NODE_TYPE) {
        return (Item){.item = i2it(9)};
    }

    // childNodes — return a NodeList-like Array of the document's children
    // (synthesized doctype + documentElement). Backed by the document stub
    // so iteration works.
    if (prop_id == JS_DOM_PROP_CHILD_NODES) {
        DomDocument* doc = _js_current_document;
        if (!doc) return ItemNull;
        void* stub_v = js_dom_get_or_create_doc_node(doc);
        if (!stub_v) return ItemNull;
        DomElement* stub = (DomElement*)stub_v;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = stub->first_child;
        while (child) {
            array_push(arr, js_dom_wrap_element((void*)child));
            child = child->next_sibling;
        }
        return (Item){.array = arr};
    }

    // nodeName
    if (prop_id == JS_DOM_PROP_NODE_NAME) {
        return js_name_item("#document");
    }

    // styleSheets — collection of parsed CSSStyleSheet objects
    if (prop_id == JS_DOM_PROP_STYLE_SHEETS) {
        return js_dom_document_stylesheets_bridge();
    }

    // ownerDocument — the document itself has no owner (returns null)
    if (prop_id == JS_DOM_PROP_OWNER_DOCUMENT) {
        return ItemNull;
    }

    // defaultView — returns window (the global object)
    // Sizzle accesses document.defaultView for getComputedStyle.
    // Foreign documents (created via document.implementation.create*Document)
    // never have a browsing context, so defaultView must be null per HTML spec.
    if (prop_id == JS_DOM_PROP_DEFAULT_VIEW) {
        return js_dom_document_default_view_bridge((void*)_js_current_document);
    }

    // For iframe content docs, expose Window-like properties on the same
    // wrapper so that contentWindow.X works (since contentWindow ===
    // contentDocument here). Also handle on the main doc proxy so existing
    // window-style access through `document` continues to function.
    if (_js_current_document != _js_main_document &&
        js_doc_has_browsing_context(_js_current_document)) {
        if (prop_id == JS_DOM_PROP_DOCUMENT) {
            Item w = lookup_foreign_doc_wrapper(_js_current_document);
            return w.item ? w : ItemNull;
        }
        if (prop_id == JS_DOM_PROP_SELECTION || prop_id == JS_DOM_PROP_RANGE) {
            Item global_ctor = js_get_global_property(prop_name);
            if (get_type_id(global_ctor) == LMD_TYPE_FUNC) return global_ctor;
            return ItemNull;
        }
    }

    // implementation — DOMImplementation (createHTMLDocument, createDocument, ...)
    if (prop_id == JS_DOM_PROP_IMPLEMENTATION) {
        return js_dom_document_implementation_bridge();
    }

    // doctype — DocumentType node (or null if document has none).
    // We synthesize a DOCTYPE node as the first child of the document stub
    // (see js_dom_get_or_create_doc_node). Expose it here so JS code that
    // does `document.doctype` (and Range/Selection APIs that take it as a
    // node argument) work.
    if (prop_id == JS_DOM_PROP_DOCTYPE) {
        DomDocument* doc = _js_current_document;
        if (!doc) return ItemNull;
        void* stub = js_dom_get_or_create_doc_node(doc);
        if (!stub) return ItemNull;
        DomElement* e = (DomElement*)stub;
        DomNode* fc = e->first_child;
        if (!fc) return ItemNull;
        // synthesized doctype is the leading DomComment child
        if (fc->is_comment()) return js_dom_wrap_element(fc);
        return ItemNull;
    }

    DomDocument* expando_doc = _js_current_document ? _js_current_document : _js_main_document;
    void* stub_v = js_dom_get_or_create_doc_node(expando_doc);
    if (stub_v) {
        Item exp_map = expando_get_map((DomNode*)stub_v);
        if (exp_map.item != ITEM_NULL) {
            if (expando_map_has_key(exp_map, prop_name)) {
                return js_get_key_default(exp_map, prop_name);
            }
        }
    }

    // designMode is the legacy whole-document edit toggle. This first cut
    // exposes the IDL state; editing-host default actions still land in the
    // command engine phases.
    if (prop_id == JS_DOM_PROP_DESIGN_MODE) {
        return js_dom_document_design_mode_bridge();
    }

    // activeElement — currently focused element, or <body> as default per spec.
    if (prop_id == JS_DOM_PROP_ACTIVE_ELEMENT) {
        return js_dom_document_active_element_bridge((void*)doc);
    }

    expando_doc = _js_current_document ? _js_current_document : _js_main_document;
    stub_v = js_dom_get_or_create_doc_node(expando_doc);
    if (stub_v) {
        Item exp_map = expando_get_map((DomNode*)stub_v);
        if (exp_map.item != ITEM_NULL) {
            if (expando_map_has_key(exp_map, prop_name)) {
                return js_get_key_default(exp_map, prop_name);
            }
        }
    }

    if (_js_current_document != _js_main_document &&
        js_doc_has_browsing_context(_js_current_document)) {
        Item global_value = js_get_global_property(prop_name);
        if (get_type_id(global_value) == LMD_TYPE_FUNC) {
            return global_value;
        }
    }

    log_debug("js_document_get_property: unknown property '%s'", prop);
    return make_js_undefined();
}

// ============================================================================
// HTML form text-control selection model (§8 of Radiant_Design_Selection.md)
// ============================================================================
//
// `<input type=text|password|email|url|search|tel|number>` and `<textarea>`
// have their own "text control selection" (separate from the document's
// DomSelection). HTML §4.10.6 exposes:
//   .value (mutable)
//   .selectionStart / .selectionEnd (UTF-16 offsets)
//   .selectionDirection ("forward" / "backward" / "none")
//   .setSelectionRange(start, end [, direction])
//   .select()
//   .defaultValue
// The state lives on `FormControlProp` (radiant/form_control.hpp).
// `document.activeElement` and the "last focused text control" are tracked
// via the focus tracker in radiant/text_control.{hpp,cpp}; the helpers
// `tc_is_text_control`, `tc_get_or_create_form`, `tc_ensure_init`,
// `tc_set_value`, `tc_set_selection_range`, and the UTF-8↔UTF-16 conversions
// are all defined in radiant/text_control.cpp and re-used by event.cpp /
// render_form.cpp.

// Public entry — JS Selection.toString() consults this when the document
// selection is empty (or to override the empty result with the focused
// text control's selected substring per WPT stringifier_editable_element).
// Returns nullptr if no text control should contribute.
extern "C" String* js_dom_active_text_control_selected_text(void) {
    uint32_t blen = 0;
    const char* sel = tc_active_selected_text(js_dom_current_state(), &blen);
    if (!sel || !blen) return nullptr;
    return heap_strcpy((char*)sel, (int64_t)blen);
}

static uint8_t text_control_direction_from_item(Item value) {
    const char* d = fn_to_cstr(value);
    if (!d) return 0;
    if (strcmp(d, "forward") == 0) return 1;
    if (strcmp(d, "backward") == 0) return 2;
    return 0;
}

extern "C" Item js_dom_text_control_set_selection_range_bridge(void* dom_elem,
                                                               Item start_arg,
                                                               Item end_arg,
                                                               Item dir_arg);
extern "C" Item js_dom_text_control_select_bridge(void* dom_elem);
extern "C" Item js_dom_text_control_set_range_text_bridge(void* dom_elem,
                                                          Item replacement_arg,
                                                          Item start_arg,
                                                          Item end_arg,
                                                          Item mode_arg);

static bool js_dom_form_named_getter_reserved_name(const char* prop) {
    if (!prop) return true;
    return strcmp(prop, "elements") == 0 ||
        strcmp(prop, "length") == 0 ||
        strcmp(prop, "action") == 0 ||
        strcmp(prop, "method") == 0 ||
        strcmp(prop, "enctype") == 0 ||
        strcmp(prop, "encoding") == 0 ||
        strcmp(prop, "acceptCharset") == 0 ||
        strcmp(prop, "target") == 0 ||
        strcmp(prop, "noValidate") == 0 ||
        strcmp(prop, "autocomplete") == 0 ||
        strcmp(prop, "name") == 0 ||
        strcmp(prop, "submit") == 0 ||
        strcmp(prop, "reset") == 0 ||
        strcmp(prop, "checkValidity") == 0 ||
        strcmp(prop, "reportValidity") == 0 ||
        strcmp(prop, "requestSubmit") == 0;
}

static Item js_text_control_set_selection_range(Item start_arg, Item end_arg, Item dir_arg) {
    Item self = js_get_this();
    DomElement* elem = (DomElement*)js_dom_unwrap_element(self);
    return js_dom_text_control_set_selection_range_bridge((void*)elem, start_arg, end_arg, dir_arg);
}

extern "C" Item js_dom_text_control_set_selection_range_bridge(void* dom_elem,
                                                               Item start_arg,
                                                               Item end_arg,
                                                               Item dir_arg) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !tc_is_text_control_elem(elem)) return make_js_undefined();
    DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();

    int64_t s = it2i(start_arg);
    int64_t e = it2i(end_arg);
    if (s < 0) s = 0;
    if (e < 0) e = 0;
    uint8_t dir = text_control_direction_from_item(dir_arg);
    form_control_set_selection(state, (View*)elem, (uint32_t)s, (uint32_t)e, dir);
    return make_js_undefined();
}

static Item js_text_control_select(void) {
    Item self = js_get_this();
    DomElement* elem = (DomElement*)js_dom_unwrap_element(self);
    return js_dom_text_control_select_bridge((void*)elem);
}

extern "C" Item js_dom_text_control_select_bridge(void* dom_elem) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !tc_is_text_control_elem(elem)) return make_js_undefined();

    tc_ensure_init(elem);
    FormControlProp* f = elem->form;
    DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
    if (js_dom_is_script_focusable(elem)) {
        focus_set_programmatic(state, (View*)elem);
    }
    form_control_set_selection(state, (View*)elem, 0, f->current_value_u16_len, 0);
    return make_js_undefined();
}

static bool js_text_control_set_raw_value(DomElement* elem, const char* new_val,
                                          uint32_t new_len) {
    if (!elem || !tc_is_text_control_elem(elem)) return false;
    tc_ensure_init(elem);
    FormControlProp* f = tc_get_or_create_form(elem);
    if (!f) return false;

    char* buf = (char*)mem_alloc((size_t)new_len + 1, MEM_CAT_DOM);
    if (!buf) return false;
    if (new_val && new_len > 0) memcpy(buf, new_val, new_len);
    buf[new_len] = '\0';

    if (f->current_value) mem_free(f->current_value);
    f->current_value = buf;
    f->current_value_len = new_len;
    f->current_value_u16_len = tc_utf8_to_utf16_length(buf, new_len);
    if (f->selection_start > f->current_value_u16_len)
        f->selection_start = f->current_value_u16_len;
    if (f->selection_end > f->current_value_u16_len)
        f->selection_end = f->current_value_u16_len;
    if (f->selection_start > f->selection_end)
        f->selection_start = f->selection_end;
    f->tc_initialized = 1;
    f->value = buf;

    DocState* state = js_dom_current_state();
    f->state_ref = state;
    form_control_sync_text_control_state(state, (View*)elem);
    form_control_sync_text_control_focus_state(state, (View*)elem);
    bool show_placeholder = f->current_value_len == 0 && f->placeholder && f->placeholder[0];
    state_set_bool(state, elem, STATE_PLACEHOLDER, show_placeholder);
    return true;
}

static Item js_text_control_set_range_text_for_elem(DomElement* elem,
                                                    Item replacement_arg,
                                                    Item start_arg,
                                                    Item end_arg,
                                                    Item mode_arg) {
    if (!elem || !tc_is_text_control_elem(elem)) return make_js_undefined();
    tc_ensure_init(elem);
    FormControlProp* f = elem->form;
    if (!f) return make_js_undefined();

    Item replacement_item = js_to_string(replacement_arg);
    const char* replacement = fn_to_cstr(replacement_item);
    if (!replacement) replacement = "";
    uint32_t replacement_len = (uint32_t)strlen(replacement);
    uint32_t replacement_u16_len = tc_utf8_to_utf16_length(replacement, replacement_len);

    uint32_t old_value_u16_len = f->current_value_u16_len;
    uint32_t old_selection_start = f->selection_start;
    uint32_t old_selection_end = f->selection_end;
    uint8_t old_direction = f->selection_direction;

    uint32_t start = old_selection_start;
    uint32_t end = old_selection_end;
    if (!is_js_undefined(start_arg)) {
        int64_t start_i = js_dom_to_integer_or_zero(start_arg);
        start = start_i < 0 ? 0 : (uint32_t)start_i;
    }
    if (!is_js_undefined(end_arg)) {
        int64_t end_i = js_dom_to_integer_or_zero(end_arg);
        end = end_i < 0 ? 0 : (uint32_t)end_i;
    }
    if (start > end) {
        return js_dom_throw_index_size_error("The start offset is larger than the end offset.");
    }
    if (start > old_value_u16_len) start = old_value_u16_len;
    if (end > old_value_u16_len) end = old_value_u16_len;

    const char* old_value = f->current_value ? f->current_value : "";
    uint32_t start_u8 = tc_utf16_to_utf8_offset(old_value, f->current_value_len, start);
    uint32_t end_u8 = tc_utf16_to_utf8_offset(old_value, f->current_value_len, end);
    if (end_u8 < start_u8) end_u8 = start_u8;

    uint32_t prefix_len = start_u8;
    uint32_t suffix_len = f->current_value_len > end_u8 ? f->current_value_len - end_u8 : 0;
    uint32_t new_len = prefix_len + replacement_len + suffix_len;
    char* new_value = (char*)mem_alloc((size_t)new_len + 1, MEM_CAT_JS_RUNTIME);
    if (!new_value) return make_js_undefined();
    if (prefix_len > 0) memcpy(new_value, old_value, prefix_len);
    if (replacement_len > 0) memcpy(new_value + prefix_len, replacement, replacement_len);
    if (suffix_len > 0) {
        memcpy(new_value + prefix_len + replacement_len, old_value + end_u8, suffix_len);
    }
    new_value[new_len] = '\0';

    uint32_t final_start = old_selection_start;
    uint32_t final_end = old_selection_end;
    uint8_t final_direction = old_direction;
    const char* mode = is_js_undefined(mode_arg) ? "preserve" : fn_to_cstr(mode_arg);
    if (!mode) mode = "preserve";
    uint32_t inserted_end = start + replacement_u16_len;
    if (strcmp(mode, "select") == 0) {
        final_start = start;
        final_end = inserted_end;
        final_direction = 0;
    } else if (strcmp(mode, "start") == 0) {
        final_start = start;
        final_end = start;
        final_direction = 0;
    } else if (strcmp(mode, "end") == 0) {
        final_start = inserted_end;
        final_end = inserted_end;
        final_direction = 0;
    } else {
        int64_t delta = (int64_t)replacement_u16_len - (int64_t)(end - start);
        if (final_start > end) {
            final_start = (uint32_t)((int64_t)final_start + delta);
        } else if (final_start > start) {
            final_start = inserted_end;
        }
        if (final_end > end) {
            final_end = (uint32_t)((int64_t)final_end + delta);
        } else if (final_end > start) {
            final_end = inserted_end;
        }
    }

    if (!js_text_control_set_raw_value(elem, new_value, new_len)) {
        mem_free(new_value);
        return make_js_undefined();
    }
    mem_free(new_value);
    js_dom_expando_flag_set(elem, "__valueDirty", (Item){.item = b2it(true)});
    DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
    form_control_set_selection(state, (View*)elem,
        final_start, final_end, final_direction);
    return make_js_undefined();
}

static Item js_text_control_set_range_text(Item replacement_arg, Item start_arg,
                                           Item end_arg, Item mode_arg) {
    Item self = js_get_this();
    DomElement* elem = (DomElement*)js_dom_unwrap_element(self);
    return js_dom_text_control_set_range_text_bridge((void*)elem, replacement_arg,
        start_arg, end_arg, mode_arg);
}
JS_FORWARD_ITEM(js_dom_text_control_set_range_text_bridge, (void* dom_elem,                                                           Item replacement_arg,                                                           Item start_arg,                                                           Item end_arg,                                                           Item mode_arg), js_text_control_set_range_text_for_elem, ((DomElement*)dom_elem, replacement_arg, start_arg, end_arg, mode_arg))

extern "C" Item js_dom_focus_method_bridge(void* dom_elem, bool focus) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return make_js_undefined();
    DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
    if (focus) {
        if (js_dom_is_script_focusable(elem)) {
            View* old_focus = state ? focus_get(state) : nullptr;
            js_document_active_element = elem;
            focus_set_programmatic(state, (View*)elem);
            // focus() on an already-focused editing host must preserve the
            // live selection; reinitialising it here moves scripted caret
            // placement back to the host start before the next key event.
            if (old_focus != (View*)elem) {
                js_dom_focus_set_selection_for_element(state, elem);
            }
            if (old_focus != (View*)elem) js_dom_dispatch_focus_events(elem);
        }
    } else {
        if (js_document_active_element == elem) js_document_active_element = nullptr;
        if (focus_get(state) == (View*)elem) focus_clear(state);
    }
    return make_js_undefined();
}

extern "C" bool js_dom_focus_editing_host_for_automation(void* dom_elem) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !js_dom_is_editing_host(elem) ||
        !js_dom_is_script_focusable(elem)) {
        return false;
    }
    DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
    if (!state) return false;
    // Template documents have no JS runtime, so returning a JS Item through
    // focus() would dereference an unbound interpreter context. The common
    // focus and Selection writers are sufficient for physical input routing.
    js_document_active_element = elem;
    View* old_focus = focus_get(state);
    focus_set_programmatic(state, (View*)elem);
    // automation focus follows the same idempotent focus contract as JS:
    // preserve an explicitly placed selection when the host already owns focus.
    if (old_focus != (View*)elem) {
        js_dom_focus_set_selection_for_element(state, elem);
    }
    return true;
}

extern "C" Item js_dom_click_method_bridge(Item elem_item) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return make_js_undefined();
    if (elem->tag_name) {
        const char* tag = elem->tag_name;
        bool is_form_ctrl =
            strcasecmp(tag, "button") == 0 ||
            strcasecmp(tag, "input") == 0 ||
            strcasecmp(tag, "select") == 0 ||
            strcasecmp(tag, "textarea") == 0 ||
            strcasecmp(tag, "fieldset") == 0;
        if (is_form_ctrl && elem->has_attribute("disabled")) {
            return make_js_undefined();
        }
    }
    // click() synthesizes a JS MouseEvent and dispatches through the existing event system.
    Item ev = js_create_click_mouse_event();
    return js_dom_dispatch_event(elem_item, ev);
}

#define JS_DOM_EVENT_LISTENER_BRIDGE(name, operation) \
extern "C" Item name(Item target_item, Item type, Item callback, Item opts) { \
    operation(target_item, type, callback, opts); \
    return make_js_undefined(); \
}
JS_DOM_EVENT_LISTENER_BRIDGE(js_dom_add_event_listener_bridge,
    js_dom_add_event_listener)
JS_DOM_EVENT_LISTENER_BRIDGE(js_dom_remove_event_listener_bridge,
    js_dom_remove_event_listener)
#undef JS_DOM_EVENT_LISTENER_BRIDGE

// dispatch keeps the wrapped target identity shared with listener lookup.
JS_FORWARD_ITEM(js_dom_dispatch_event_bridge, (Item target_item, Item event_item),
    js_dom_dispatch_event, (target_item, event_item))

static Item js_dom_text_replace_data_method(DomText* text_node, Item offset_arg,
                                            Item count_arg, Item data_arg) {
    if (!text_node) return make_js_undefined();
    int64_t offset = js_dom_to_integer_or_zero(offset_arg);
    int64_t count = js_dom_to_integer_or_zero(count_arg);
    if (offset < 0 || count < 0) {
        return js_dom_throw_index_size_error("The offset or count is negative.");
    }

    Item data_text_item = js_to_string(data_arg);
    const char* data_text = fn_to_cstr(data_text_item);
    if (!data_text) data_text = "";
    JS_RETURN_IF_ERROR(js_dom_replace_text_data(text_node, (uint32_t)offset,
        (uint32_t)count, data_text));
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_dom_text_insert_data_method, (DomText* text_node, Item offset_arg,                                            Item data_arg), js_dom_text_replace_data_method, (text_node, offset_arg, (Item){.item = i2it(0)}, data_arg))

static Item js_dom_text_append_data_method(DomText* text_node, Item data_arg) {
    if (!text_node) return make_js_undefined();
    return js_dom_text_replace_data_method(text_node,
        (Item){.item = i2it((int64_t)dom_text_utf16_length(text_node))},
        (Item){.item = i2it(0)}, data_arg);
}
JS_FORWARD_STATIC_ITEM(js_dom_text_delete_data_method, (DomText* text_node, Item offset_arg,                                            Item count_arg), js_dom_text_replace_data_method, (text_node, offset_arg, count_arg, js_name_item("")))

static Item js_dom_text_substring_data_method(DomText* text_node, Item offset_arg,
                                              Item count_arg) {
    if (!text_node) return js_name_item("");
    int64_t offset = js_dom_to_integer_or_zero(offset_arg);
    int64_t count = js_dom_to_integer_or_zero(count_arg);
    if (offset < 0 || count < 0) {
        return js_dom_throw_index_size_error("The offset or count is negative.");
    }
    uint32_t old_u16_len = dom_text_utf16_length(text_node);
    if ((uint64_t)offset > old_u16_len) {
        return js_dom_throw_index_size_error("The offset is larger than the CharacterData length.");
    }
    uint32_t available = old_u16_len - (uint32_t)offset;
    uint32_t take = (uint64_t)count > available ? available : (uint32_t)count;
    uint32_t start_u8 = dom_text_utf16_to_utf8(text_node, (uint32_t)offset);
    uint32_t end_u8 = dom_text_utf16_to_utf8(text_node, (uint32_t)offset + take);
    if (end_u8 < start_u8) end_u8 = start_u8;
    const char* chars = text_node->text ? text_node->text : "";
    String* s = heap_strcpy((char*)chars + start_u8, end_u8 - start_u8);
    return (Item){.item = s2it(s)};
}

extern "C" Item js_dom_set_text_data_property(void* text_ptr, Item value) {
    DomText* text_node = (DomText*)text_ptr;
    if (!text_node) return value;
    // The specialized MIR member path must preserve CharacterData's DOMString
    // conversion just like the generic host-property path.
    const char* new_text = js_dom_to_dom_string_cstr(value);
    if (new_text) {
        uint32_t old_u16_len = dom_text_utf16_length(text_node);
        // text data writes must continue through the JS helper because it
        // updates live ranges and publishes the text mutation kind.
        JS_ASSIGN_OR_RETURN(set_result, js_dom_replace_text_data(text_node, 0, old_u16_len, new_text));
        log_debug("js_dom_set_text_data_property: set text node data='%.30s'", new_text);
    }
    return value;
}

#define JS_DOM_TEXT_BRIDGE_1(name, method) \
    extern "C" Item name(void* text_ptr, Item a) { \
        return method((DomText*)text_ptr, a); \
    }
#define JS_DOM_TEXT_BRIDGE_2(name, method) \
    extern "C" Item name(void* text_ptr, Item a, Item b) { \
        return method((DomText*)text_ptr, a, b); \
    }
#define JS_DOM_TEXT_BRIDGE_3(name, method) \
    extern "C" Item name(void* text_ptr, Item a, Item b, Item c) { \
        return method((DomText*)text_ptr, a, b, c); \
    }
JS_DOM_TEXT_BRIDGE_3(js_dom_text_replace_data_bridge, js_dom_text_replace_data_method)
JS_DOM_TEXT_BRIDGE_2(js_dom_text_insert_data_bridge, js_dom_text_insert_data_method)
JS_DOM_TEXT_BRIDGE_1(js_dom_text_append_data_bridge, js_dom_text_append_data_method)
JS_DOM_TEXT_BRIDGE_2(js_dom_text_delete_data_bridge, js_dom_text_delete_data_method)
JS_DOM_TEXT_BRIDGE_2(js_dom_text_substring_data_bridge, js_dom_text_substring_data_method)
#undef JS_DOM_TEXT_BRIDGE_3
#undef JS_DOM_TEXT_BRIDGE_2
#undef JS_DOM_TEXT_BRIDGE_1

enum JsTextDataOperation {
    JS_TEXT_DATA_REPLACE,
    JS_TEXT_DATA_INSERT,
    JS_TEXT_DATA_APPEND,
    JS_TEXT_DATA_DELETE,
    JS_TEXT_DATA_SUBSTRING,
};

static Item js_text_data_body(Item callee, Item this_value, Item* args,
                              int argc, uint64_t* result_home) {
    (void)this_value; (void)result_home;
    JsFunction* fn = (JsFunction*)callee.function;
    DomNode* node = (DomNode*)js_dom_unwrap_element(js_get_this());
    if (!node || !node->is_text() || !fn) return make_js_undefined();
    Item arg0 = argc > 0 ? args[0] : make_js_undefined();
    Item arg1 = argc > 1 ? args[1] : make_js_undefined();
    Item arg2 = argc > 2 ? args[2] : make_js_undefined();
    switch ((JsTextDataOperation)fn->native_target.bits) {
    case JS_TEXT_DATA_REPLACE:
        return js_dom_text_replace_data_method(node->as_text(), arg0, arg1, arg2);
    case JS_TEXT_DATA_INSERT:
        return js_dom_text_insert_data_method(node->as_text(), arg0, arg1);
    case JS_TEXT_DATA_APPEND:
        return js_dom_text_append_data_method(node->as_text(), arg0);
    case JS_TEXT_DATA_DELETE:
        return js_dom_text_delete_data_method(node->as_text(), arg0, arg1);
    case JS_TEXT_DATA_SUBSTRING:
        return js_dom_text_substring_data_method(node->as_text(), arg0, arg1);
    default:
        return ItemError;
    }
}

// ============================================================================
// F-4: Constraint Validation API helpers
// ============================================================================

// Returns true if this element is "barred from constraint validation":
//   - type=hidden for input (submit/button/image/reset are NOT barred per WPT)
//   - disabled
//   - output, object, fieldset, datalist (not submittable)
//   - readonly
//   - element has a datalist ancestor
static bool _elem_is_barred(DomElement* elem) {
    if (!elem || !elem->tag_name) return true;
    const char* tag = elem->tag_name;
    if (strcasecmp(tag, "output") == 0 || strcasecmp(tag, "object") == 0 ||
        strcasecmp(tag, "fieldset") == 0 || strcasecmp(tag, "datalist") == 0) {
        return true;
    }
    // Constraint validation, selector matching, and focusability share the
    // StateStore disabled-fieldset rule so a dynamically inserted control
    // cannot disagree about whether the first-legend exemption applies.
    if (form_control_is_disabled(elem->doc ? elem->doc->state : js_dom_current_state(),
                                 static_cast<View*>(elem))) return true;
    // barred if readonly
    if (elem->has_attribute("readonly")) return true;
    if (strcasecmp(tag, "input") == 0) {
        const char* type = js_dom_input_type_lower(elem);
        // Per HTML spec, input types hidden, reset, button are barred
        // from constraint validation.
        if (strcmp(type, "hidden") == 0 || strcmp(type, "reset") == 0 ||
            strcmp(type, "button") == 0) {
            return true;
        }
    }
    if (strcasecmp(tag, "button") == 0) {
        // <button type=reset|button> is barred. Default and submit are not.
        const char* btype = elem->get_attribute("type");
        if (btype && (strcasecmp(btype, "reset") == 0 || strcasecmp(btype, "button") == 0)) {
            return true;
        }
    }
    if (strcasecmp(tag, "input") == 0 || strcasecmp(tag, "button") == 0 ||
        strcasecmp(tag, "select") == 0 || strcasecmp(tag, "textarea") == 0) {
        // check for datalist ancestor
        DomNode* p = elem->parent;
        while (p) {
            if (p->is_element()) {
                DomElement* pe = p->as_element();
                if (pe->tag_name && strcasecmp(pe->tag_name, "datalist") == 0) return true;
            }
            p = p->parent;
        }
        return false;
    }
    return true; // not a form-associated submittable element
}

// Get the current value of a form element as a C-string (UTF-8).
static const char* _elem_current_value(DomElement* elem) {
    if (!elem || !elem->tag_name) return "";
    const char* tag = elem->tag_name;
    if (strcasecmp(tag, "input") == 0) {
        RadiantInputValueKind kind = radiant_input_value_kind(
            elem->get_attribute("type"));
        if (kind != RADIANT_INPUT_VALUE_TEXT &&
            kind != RADIANT_INPUT_VALUE_UNSUPPORTED) {
            return radiant_input_live_value(elem);
        }
        if (tc_is_text_control(elem)) {
            tc_ensure_init(elem);
            return (elem->form && elem->form->current_value) ? elem->form->current_value : "";
        }
        const char* v = elem->get_attribute("value");
        return v ? v : "";
    }
    if (strcasecmp(tag, "textarea") == 0) {
        tc_ensure_init(elem);
        return (elem->form && elem->form->current_value) ? elem->form->current_value : "";
    }
    return "";
}

// Build and return a ValidityState plain JS object for the given element.

// For a <select required> element, returns true iff no <option> is
// "selected" with a non-empty value (i.e. the placeholder option requires
// user to explicitly pick a non-empty one).
static bool _get_selectedness(DomElement* opt);
static void _set_selectedness(DomElement* opt, bool v);
static void _select_sync_native_selected_index(DomElement* sel,
                                               int selected_index,
                                               int option_count);

// Walk the descendants of `sel` and append each <option> to `arr`. Direct
// optgroup descendants contribute options, but nested option/hr/select/optgroup
// subtrees are not part of the owning select's option list.
static void _collect_options_impl(DomNode* node, Item arr, bool allow_optgroup) {
    while (node) {
        if (node->is_element()) {
            DomElement* ce = (DomElement*)node;
            if (ce->tag_name) {
                if (strcasecmp(ce->tag_name, "option") == 0) {
                    js_array_push(arr, js_dom_wrap_element(ce));
                } else if (strcasecmp(ce->tag_name, "select") == 0 ||
                           strcasecmp(ce->tag_name, "hr") == 0) {
                    // skip nested select/hr subtrees
                } else if (strcasecmp(ce->tag_name, "optgroup") == 0) {
                    if (allow_optgroup) _collect_options_impl(ce->first_child, arr, false);
                } else {
                    _collect_options_impl(ce->first_child, arr, allow_optgroup);
                }
            }
        }
        node = node->next_sibling;
    }
}
JS_FORWARD_STATIC_VOID( _collect_options, (DomNode* node, Item arr), _collect_options_impl, (node, arr, true))

// Get the option's text content (concatenated descendant text, trimmed
// of leading/trailing ASCII whitespace, with internal whitespace
// collapsed per HTML spec for <option> label).
static char* _option_text(DomElement* opt) {
    StrBuf* out = strbuf_new_cap(32);
    if (!out) return mem_strdup("", MEM_CAT_JS_RUNTIME);
    dom_option_text_normalized(opt, out);
    char* result = mem_strdup(out->str ? out->str : "", MEM_CAT_JS_RUNTIME);
    strbuf_free(out);
    return result;
}

// Get/set the option's selectedness. Stored as expando "__selected".
// Default falls back to the `selected` content attribute (defaultSelected).
static bool _get_selectedness(DomElement* opt) {
    if (!opt) return false;
    if (opt->has_option_selectedness()) return dom_option_is_selected(opt);
    Item exp = expando_get_map((DomNode*)opt);
    if (exp.item != ITEM_NULL) {
        Item v = js_get_name_key(exp, "__selected");
        if (v.item != ITEM_NULL && !is_js_undefined(v)) return js_is_truthy(v);
    }
    return dom_option_is_selected(opt);
}
JS_FORWARD_RETURN(bool, js_dom_option_is_selected, (void* dom_elem), _get_selectedness, ((DomElement*)dom_elem))

// Returns true if the select's selectedness has been explicitly modified
// (via selectedIndex/value/option.selected setter), so default-reset
// behavior should NOT be applied.
static bool js_dom_expando_flag_is(DomElement* elem, const char* name) {
    if (!elem) return false;
    Item exp = expando_get_map((DomNode*)elem);
    if (exp.item == ITEM_NULL) return false;
    Item value = js_get_key_default(exp, js_name_item(name));
    return value.item != ITEM_NULL && !is_js_undefined(value) && js_is_truthy(value);
}

static void js_dom_expando_flag_set(DomElement* elem, const char* name, Item value) {
    if (!elem) return;
    Item exp = expando_get_or_create_map((DomNode*)elem);
    if (exp.item == ITEM_NULL) return;
    js_set_key_default(exp, js_name_item(name), value);
}
JS_FORWARD_STATIC_RETURN(bool, _select_is_dirty, (DomElement* sel), js_dom_expando_flag_is, (sel, "__selDirty"))

// Text-control dirty value flag (input/textarea), tracked via expando so we
// can distinguish "API has been called to set value" from "value reflects
// defaultValue". Cleared by form reset.
JS_FORWARD_STATIC_RETURN(bool, _value_is_dirty, (DomElement* elem), js_dom_expando_flag_is, (elem, "__valueDirty"))

extern "C" Item js_dom_text_control_set_value_bridge(void* dom_elem, Item value) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !tc_is_text_control_elem(elem)) return value;
    const char* s = fn_to_cstr(value);
    if (!s) s = "";
    if (elem->tag_name && strcasecmp(elem->tag_name, "input") == 0) {
        const char* itype = _input_type_lower(elem);
        bool single_line = strcmp(itype, "text") == 0 || strcmp(itype, "search") == 0 ||
                           strcmp(itype, "tel") == 0 || strcmp(itype, "url") == 0 ||
                           strcmp(itype, "email") == 0 || strcmp(itype, "password") == 0;
        if (single_line) {
            size_t slen = strlen(s);
            bool has_newline = false;
            for (size_t k = 0; k < slen; k++) {
                if (s[k] == '\r' || s[k] == '\n') { has_newline = true; break; }
            }
            if (has_newline) {
                char* stripped = (char*)mem_alloc(slen + 1, MEM_CAT_JS_RUNTIME);
                if (stripped) {
                    size_t out = 0;
                    for (size_t k = 0; k < slen; k++) {
                        if (s[k] != '\r' && s[k] != '\n') stripped[out++] = s[k];
                    }
                    stripped[out] = '\0';
                    tc_set_value(elem, stripped, out);
                    mem_free(stripped);
                    js_dom_expando_flag_set(elem, "__valueDirty", (Item){.item = b2it(true)});
                    return value;
                }
            }
        }
    }
    // text-control value writes dirty live state without changing the default attribute.
    tc_set_value(elem, s, strlen(s));
    js_dom_expando_flag_set(elem, "__valueDirty", (Item){.item = b2it(true)});
    return value;
}

static Item js_dom_text_control_set_selection_edge(void* dom_elem, Item value,
                                                   bool start_edge) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !tc_is_text_control_elem(elem)) return value;
    tc_ensure_init(elem);
    int64_t v = it2i(value);
    if (v < 0) v = 0;
    uint32_t edge = (uint32_t)v;
    uint32_t start = 0;
    uint32_t end = 0;
    uint8_t direction = 0;
    DocState* state = js_dom_current_state();
    form_control_get_selection(state, (View*)elem,
        start_edge ? NULL : &start, start_edge ? &end : NULL, &direction);
    if (start_edge) {
        start = edge;
        if (start > end) end = start;
    } else {
        end = edge;
        if (start > end) start = end;
    }
    form_control_set_selection(state, (View*)elem, start, end, direction);
    return value;
}
JS_FORWARD_ITEM(js_dom_text_control_set_selection_start_bridge, (void* dom_elem, Item value), js_dom_text_control_set_selection_edge, (dom_elem, value, true))
JS_FORWARD_ITEM(js_dom_text_control_set_selection_end_bridge, (void* dom_elem, Item value), js_dom_text_control_set_selection_edge, (dom_elem, value, false))

static void js_dom_detach_all_children(DomElement* elem) {
    if (!elem) return;
    DomNode* child = elem->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        dom_pre_remove(child);
        child->parent = nullptr;
        child->next_sibling = nullptr;
        child->prev_sibling = nullptr;
        child = next;
    }
    elem->first_child = nullptr;
    elem->last_child = nullptr;
}

extern "C" Item js_dom_text_control_set_selection_direction_bridge(void* dom_elem, Item value) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !tc_is_text_control_elem(elem)) return value;
    tc_ensure_init(elem);
    uint8_t d = text_control_direction_from_item(value);
    uint32_t start = 0;
    uint32_t end = 0;
    DocState* state = js_dom_current_state();
    form_control_get_selection(state, (View*)elem, &start, &end, NULL);
    form_control_set_selection(state, (View*)elem, start, end, d);
    return value;
}

extern "C" Item js_dom_text_control_set_default_value_bridge(void* dom_elem, Item value) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !tc_is_text_control_elem(elem)) return value;
    const char* s = fn_to_cstr(value);
    if (!s) s = "";
    if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
        bool dom_children_changed = elem->first_child != nullptr || *s;
        js_dom_detach_all_children(elem);
        if (*s) {
            DomText* tn = DomText::create_copy(s, strlen(s), elem);
            if (tn) {
                tn->parent = elem;
                elem->first_child = tn;
                elem->last_child = tn;
                dom_post_insert((DomNode*)elem, (DomNode*)tn);
            }
        }
        // defaultValue only updates the live value until script dirties value.
        if (!_value_is_dirty(elem)) {
            tc_set_value(elem, s, strlen(s));
        }
        if (dom_children_changed) {
            js_dom_mutation_notify();
        }
        return value;
    }
    elem->set_attribute("value", s);
    if (!_value_is_dirty(elem)) {
        tc_set_value(elem, s, strlen(s));
    }
    return value;
}

static void _set_selectedness(DomElement* opt, bool v) {
    if (!opt) return;
    // Layout can rebuild independently of a JS EvalContext, so selectedness
    // must have a native mirror rather than only living in an expando map.
    opt->set_option_selectedness(v);
    Item exp = expando_get_or_create_map((DomNode*)opt);
    if (exp.item == ITEM_NULL) return;
    js_set_name_key(exp, "__selected", (Item){.item = b2it(v)});
}

static int _select_index_from_item(Item value) {
    TypeId t = get_type_id(value);
    if (t == LMD_TYPE_INT) return (int)it2i(value); // INT_CAST_OK: option index
    if (t == LMD_TYPE_INT64) return (int)it2l(value); // INT_CAST_OK: option index
    if (t == LMD_TYPE_FLOAT) return (int)it2d(value); // INT_CAST_OK: option index
    return 0;
}

static void _select_set_selected_index(DomElement* sel, int idx) {
    if (!sel) return;
    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    int64_t n = js_array_length(arr);
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
        if (!opt) continue;
        _set_selectedness(opt, (int)i == idx); // INT_CAST_OK: option index
    }
    _select_sync_native_selected_index(
        sel, idx >= 0 && idx < n ? idx : -1,
        (int)n); // INT_CAST_OK: option count
    js_dom_expando_flag_set(sel, "__selDirty", (Item){.item = b2it(true)});
}

static void _select_select_only_option(DomElement* sel, DomElement* selected_opt) {
    if (!sel || !selected_opt) return;
    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    int64_t n = js_array_length(arr);
    int selected_index = -1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
        if (opt) {
            bool selected = opt == selected_opt;
            _set_selectedness(opt, selected);
            if (selected) selected_index = (int)i; // INT_CAST_OK: option index
        }
    }
    _select_sync_native_selected_index(sel, selected_index,
                                       (int)n); // INT_CAST_OK: option count
}

static void _select_normalize_for_selected_options(DomElement* sel, Item options) {
    if (!sel || get_type_id(options) != LMD_TYPE_ARRAY) return;
    if (sel->has_attribute("multiple")) return;
    int64_t n = js_array_length(options);
    int selected_count = 0;
    int last_selected = -1;
    int first_non_disabled = -1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(options, i));
        if (!opt) continue;
        if (_get_selectedness(opt)) {
            selected_count++;
            last_selected = (int)i; // INT_CAST_OK: option index
        }
        if (first_non_disabled < 0 && !opt->has_attribute("disabled")) {
            first_non_disabled = (int)i; // INT_CAST_OK: option index
        }
    }
    int size = 0;
    const char* sz = sel->get_attribute("size");
    if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
    int chosen = -1;
    if (selected_count > 1) chosen = last_selected;
    else if (selected_count == 0 && size <= 1 && !_select_is_dirty(sel)) chosen = first_non_disabled;
    if (chosen < 0) return;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(options, i));
        if (!opt) continue;
        _set_selectedness(opt, (int)i == chosen); // INT_CAST_OK: option index
    }
}

static void _select_refresh_selected_options_collection(Item collection, DomElement* sel) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !sel) return;
    js_set_key_cstr(collection, "length", (Item){.item = i2it(0)});

    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    _select_normalize_for_selected_options(sel, arr);
    int64_t n = js_array_length(arr);
    for (int64_t i = 0; i < n; i++) {
        Item it = js_elements_get_int(arr, i);
        DomElement* opt = (DomElement*)js_dom_unwrap_element(it);
        if (opt && _get_selectedness(opt)) js_array_push(collection, it);
    }
}

static int _select_effective_selected_index(DomElement* sel, Item options) {
    if (!sel || get_type_id(options) != LMD_TYPE_ARRAY) return -1;
    int64_t n = js_array_length(options);
    int sel_idx = -1;
    int first_non_disabled = -1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(options, i));
        if (!opt) continue;
        if (sel_idx < 0 && _get_selectedness(opt)) sel_idx = (int)i; // INT_CAST_OK: option index
        if (first_non_disabled < 0 && !opt->has_attribute("disabled")) {
            first_non_disabled = (int)i; // INT_CAST_OK: option index
        }
    }
    int size = 0;
    const char* sz = sel->get_attribute("size");
    if (sz) {
        char* ep = nullptr;
        long v = strtol(sz, &ep, 10);
        if (ep != sz && v > 0) size = (int)v; // INT_CAST_OK: select display size attribute
    }
    if (sel_idx < 0 && !sel->has_attribute("multiple") && size <= 1 &&
        !_select_is_dirty(sel)) {
        sel_idx = first_non_disabled;
    }
    return sel_idx;
}

static void _select_refresh_options_collection(Item collection, DomElement* sel) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !sel) return;
    // held select.options objects are live; refresh the dense array directly so
    // stale option slots do not survive structural mutations through companion props.
    collection.array->length = 0;
    _collect_options(sel->first_child, collection);
    int64_t n = js_array_length(collection);
    int sel_idx = _select_effective_selected_index(sel, collection);
    js_set_key_cstr(collection, "selectedIndex", (Item){.item = i2it(sel_idx)});
    if (js_array_has_props(collection.array)) {
        Map* props = js_array_props(collection.array);
        Item props_item = (Item){.map = props};
        js_set_key_cstr(props_item, "length", (Item){.item = i2it(n)});
        _array_companion_set_int_slot(collection, "length", 6, n);
    }
}

static DomElement* _nearest_select_for_node(DomNode* node) {
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = cur->as_element();
        if (_is_tag(elem, "select")) return elem;
    }
    return nullptr;
}

static void _select_refresh_cached_selected_options(DomElement* sel) {
    if (!sel) return;
    Item exp = expando_get_map((DomNode*)sel);
    if (exp.item == ITEM_NULL) return;
    Item out = js_get_name_key(exp, "__selectedOptions");
    if (get_type_id(out) == LMD_TYPE_ARRAY) {
        _select_refresh_selected_options_collection(out, sel);
    }
}
JS_FORWARD_STATIC_VOID( _select_refresh_cached_selected_options_for_node, (DomNode* node), _select_refresh_cached_selected_options, (_nearest_select_for_node(node)))

extern "C" void js_array_exotic_before_property_get(Item object, Item key) {
    if (get_type_id(object) != LMD_TYPE_ARRAY) return;
    if (!js_dom_collection_runtime_state_get()) return;
    if (s_dom_collection_refresh_depth > 0) return;
    int child_kind = 0;
    DomElement* child_owner = _live_child_collection_owner(object, &child_kind);
    if (child_owner) {
        // Live collection rebuilds read their own dense slots; guard against
        // re-entering refresh through the generic JS array getters.
        DomCollectionRefreshGuard refresh_guard;
        _refresh_live_child_collection(object, child_owner, child_kind);
        return;
    }
    LiveFormCollectionEntry* form_entry = _live_form_collection_entry(object);
    if (form_entry) {
        DomCollectionRefreshGuard refresh_guard;
        _refresh_live_form_collection(object, form_entry);
        if (get_type_id(key) != LMD_TYPE_STRING) return;
        String* sk = it2s(key);
        if (!sk || sk->len == 0) return;
        if ((sk->len == 6 && strncmp(sk->chars, "length", 6) == 0) ||
            (sk->len == 9 && strncmp(sk->chars, "namedItem", 9) == 0) ||
            (sk->len == 11 && strncmp(sk->chars, "constructor", 11) == 0)) {
            return;
        }
        bool numeric = true;
        for (int64_t i = 0; i < sk->len; i++) {
            if (sk->chars[i] < '0' || sk->chars[i] > '9') {
                numeric = false;
                break;
            }
        }
        if (numeric) return;

        Item matched = ItemNull;
        for (int64_t i = 0; i < object.array->length; i++) {
            Item candidate = js_elements_get_int(object, i);
            DomElement* elem = (DomElement*)js_dom_unwrap_element(candidate);
            if (!elem) continue;
            const char* name = elem->get_attribute("name");
            const char* id = elem->get_attribute("id");
            if ((name && strlen(name) == (size_t)sk->len &&
                 strncmp(name, sk->chars, (size_t)sk->len) == 0) ||
                (id && strlen(id) == (size_t)sk->len &&
                 strncmp(id, sk->chars, (size_t)sk->len) == 0)) {
                matched = candidate;
                break;
            }
        }

        Item prop_key = (Item){.item = s2it(heap_strcpy(sk->chars, sk->len))};
        if (matched.item != ItemNull.item) {
            js_set_key_default(object, prop_key, matched);
        } else if (js_array_has_props(object.array)) {
            Map* props = js_array_props(object.array);
            bool found = false;
            Item existing = js_map_shape_lookup_ext(props, sk->chars, (int)sk->len, &found);
            if (found && js_dom_unwrap_element(existing)) {
                // dynamic named properties can become stale after DOM renames;
                // tombstone only old DOM-backed slots and leave user expandos alone.
                js_delete_property(object, prop_key);
            }
        }
        return;
    }
    LiveLookupCollectionEntry* lookup_entry = _live_lookup_collection_entry(object);
    if (lookup_entry) {
        DomCollectionRefreshGuard refresh_guard;
        _refresh_live_lookup_collection(object, lookup_entry);
        return;
    }
    int kind = 0;
    DomElement* owner = _select_options_owner(object, &kind);
    if (!owner) return;
    if (kind == SELECT_COLLECTION_OPTIONS) {
        DomCollectionRefreshGuard refresh_guard;
        _select_refresh_options_collection(object, owner);
        return;
    }
    if (kind != SELECT_COLLECTION_SELECTED_OPTIONS) return;
    TypeId kt = get_type_id(key);
    if (kt == LMD_TYPE_INT || kt == LMD_TYPE_INT64 || kt == LMD_TYPE_FLOAT) {
        DomCollectionRefreshGuard refresh_guard;
        _select_refresh_selected_options_collection(object, owner);
        return;
    }
    if (kt != LMD_TYPE_STRING) return;
    String* sk = it2s(key);
    if (!sk) return;
    DomCollectionRefreshGuard refresh_guard;
    _select_refresh_selected_options_collection(object, owner);
}

extern "C" void js_array_exotic_before_property_set(Item object, Item key, Item value) {
    if (get_type_id(object) != LMD_TYPE_ARRAY || get_type_id(key) != LMD_TYPE_STRING) return;
    String* sk = it2s(key);
    if (!sk || sk->len != 13 || strncmp(sk->chars, "selectedIndex", 13) != 0) return;
    int kind = 0;
    DomElement* owner = _select_options_owner(object, &kind);
    if (!owner || kind != SELECT_COLLECTION_OPTIONS || !_is_tag(owner, "select")) return;
    _select_set_selected_index(owner, _select_index_from_item(value));
}

// F8/ES19: an attribute write that feeds constraint validation or the ARIA
// mirrors must re-derive them, and a programmatic write fires no `input` event
// for the package to hang that on. Re-arming the control's init bit makes the
// next init phase re-run the same `init` handler rather than adding a second
// reflection route. Name-gated to the inputs of revalidate/reflect, so an
// unrelated attribute never schedules work; the package's own `aria-*` writes
// do not reach this hook at all.
static void js_dom_reinit_behavior_if_constraint_attr(DomElement* elem,
                                                      const char* attr_name) {
    if (!elem || !attr_name || !elem->form_control() || !elem->doc) return;
    static const char* kConstraintAttrs[] = {
        "type", "required", "minlength", "maxlength", "pattern",
        "min", "max", "disabled", "readonly", "value",
    };
    for (size_t i = 0; i < sizeof(kConstraintAttrs)/sizeof(kConstraintAttrs[0]); i++) {
        if (strcasecmp(attr_name, kConstraintAttrs[i]) == 0) {
            form_control_invalidate_behavior_init((DocState*)elem->doc->state,
                                                  (View*)elem);
            return;
        }
    }
}

extern "C" void js_dom_after_set_attribute(void* elem_ptr,
                                           const char* attr_name,
                                           const char* attr_value) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem || !attr_name || !attr_value) return;
    js_dom_compile_event_attr_to_expando(elem, attr_name, attr_value);
    js_dom_reinit_behavior_if_constraint_attr(elem, attr_name);
    if (_is_tag(elem, "option") && strcasecmp(attr_name, "selected") == 0) {
        DomElement* sel = _nearest_select_for_node((DomNode*)elem);
        if (sel && !sel->has_attribute("multiple")) _select_ask_for_reset(sel);
    }
    _select_refresh_cached_selected_options_for_node((DomNode*)elem);
}

extern "C" void js_dom_after_remove_attribute(void* elem_ptr,
                                              const char* attr_name) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem || !attr_name) return;
    js_dom_clear_event_attr_expando(elem, attr_name);
    js_dom_reinit_behavior_if_constraint_attr(elem, attr_name);
    if (_is_tag(elem, "select") && strcasecmp(attr_name, "multiple") == 0) {
        _select_ask_for_reset(elem);
    }
    _select_refresh_cached_selected_options_for_node((DomNode*)elem);
}

extern "C" void js_dom_after_toggle_attribute_remove(void* elem_ptr,
                                                     const char* attr_name) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem || !attr_name) return;
    // toggleAttribute historically only ran the select reset side effect for
    // removing "multiple"; keep the moved dispatch behavior-compatible.
    if (_is_tag(elem, "select") && strcasecmp(attr_name, "multiple") == 0) {
        _select_ask_for_reset(elem);
    }
}

// Find the parent <select> of an <option>. Returns nullptr if none.
static DomElement* _option_owner_select(DomElement* opt) {
    if (!opt) return nullptr;
    for (DomNode* p = opt->parent; p; p = p->parent) {
        if (p->is_element()) {
            DomElement* pe = (DomElement*)p;
            if (pe->tag_name && strcasecmp(pe->tag_name, "select") == 0) return pe;
        }
    }
    return nullptr;
}

// Return the option's effective value (`value` attribute, or text content
// if absent). Heap-allocated cstring caller must mem_free.
static char* _option_value(DomElement* opt) {
    const char* v = opt->get_attribute("value");
    if (v) return mem_strdup(v, MEM_CAT_JS_RUNTIME);
    return _option_text(opt);
}

static char* _select_value(DomElement* elem) {
    if (!elem || !_is_tag(elem, "select")) {
        return mem_strdup("", MEM_CAT_JS_RUNTIME);
    }
    Item options = js_array_new(0);
    _collect_options(elem->first_child, options);
    int64_t count = js_array_length(options);
    DomElement* first_non_disabled = nullptr;
    for (int64_t i = 0; i < count; i++) {
        DomElement* option = (DomElement*)js_dom_unwrap_element(
            js_elements_get_int(options, i));
        if (!option) continue;
        if (_get_selectedness(option)) return _option_value(option);
        if (!first_non_disabled &&
            !option->has_attribute("disabled")) {
            first_non_disabled = option;
        }
    }

    const char* size_attr = elem->get_attribute("size");
    int size = 0;
    if (size_attr) {
        char* end = nullptr;
        long parsed = strtol(size_attr, &end, 10);
        if (end != size_attr && parsed > 0) {
            size = (int)parsed; // INT_CAST_OK: HTML select size
        }
    }
    if (!elem->has_attribute("multiple") && size <= 1 &&
        first_non_disabled && !_select_is_dirty(elem)) {
        return _option_value(first_non_disabled);
    }
    return mem_strdup("", MEM_CAT_JS_RUNTIME);
}

static void _select_sync_native_selected_index(DomElement* sel,
                                               int selected_index,
                                               int option_count) {
    if (!sel || !_is_tag(sel, "select")) return;
    if (sel->form_control()) {
        // Script can reorder/add options without rebuilding the form control;
        // keep native option bounds aligned before storing live selectedness.
        sel->form->option_count = option_count;
    }
    if (sel->doc && sel->doc->state) {
        form_control_set_selected_index((DocState*)sel->doc->state,
                                        static_cast<View*>(sel),
                                        selected_index);
    }
}

// Return index of `opt` within its owner select's options list (-1 if not
// in any select).
static int _option_index_in_select(DomElement* opt) {
    DomElement* sel = _option_owner_select(opt);
    if (!sel) return -1;
    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    int64_t n = js_array_length(arr);
    for (int64_t i = 0; i < n; i++) {
        Item it = js_elements_get_int(arr, i);
        DomElement* ce = (DomElement*)js_dom_unwrap_element(it);
        if (ce == opt) return (int)i; // INT_CAST_OK: option index
    }
    return -1;
}

// Run "ask for a reset" algorithm on a <select>: ensures exactly one option
// is selectedness=true for non-multiple selects. If multiple options had
// selectedness, only the LAST one in tree order remains selected. If none
// had selectedness, the first non-disabled option is selected.
static void _select_ask_for_reset(DomElement* sel) {
    if (!sel) return;
    if (sel->has_attribute("multiple")) return;
    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    int64_t n = js_array_length(arr);
    if (n == 0) return;
    // Find last selected option.
    int last_selected = -1;
    int first_non_disabled = -1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
        if (!opt) continue;
        if (_get_selectedness(opt)) last_selected = (int)i; // INT_CAST_OK: option index
        if (first_non_disabled < 0 && !opt->has_attribute("disabled")) {
            first_non_disabled = (int)i; // INT_CAST_OK: option index
        }
    }
    int chosen = (last_selected >= 0) ? last_selected :
        (first_non_disabled >= 0 ? first_non_disabled : 0);
    int size = 0;
    {
        const char* sz = sel->get_attribute("size");
        if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
    }
    // For display-size <= 1 (the common dropdown), exactly one option
    // must be selected. For listbox (size > 1) without multiple, zero or
    // one option may be selected — but if more than one has selectedness,
    // only the last remains.
    bool require_one = size <= 1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
        if (!opt) continue;
        if (require_one) {
            _set_selectedness(opt, (int)i == chosen); // INT_CAST_OK: option index
        } else {
            // listbox: keep only the last selected (if any was selected).
            if (last_selected < 0) {
                _set_selectedness(opt, false);
            } else {
                _set_selectedness(opt, (int)i == last_selected); // INT_CAST_OK: option index
            }
        }
    }
    int native_index = require_one ? chosen : last_selected;
    _select_sync_native_selected_index(sel, native_index,
                                       (int)n); // INT_CAST_OK: option count
}

extern "C" void js_dom_after_default_selected_set(void* dom_elem, bool selected) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return;
    Item exp = expando_get_map((DomNode*)elem);
    bool dirty = false;
    if (exp.item != ITEM_NULL) {
        Item v = js_get_key_cstr(exp, "__optDirty");
        dirty = v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
    }
    if (dirty) return;
    // defaultSelected only drives live selectedness while the option is clean.
    _set_selectedness(elem, selected);
    DomElement* sel = _option_owner_select(elem);
    if (sel) _select_ask_for_reset(sel);
}
JS_FORWARD_VOID( js_dom_after_select_multiple_removed, (void* dom_elem), _select_ask_for_reset, ((DomElement*)dom_elem))

static int js_dom_select_apply_value(DomElement* elem, const char* value,
        bool sync_native_index) {
    Item arr = js_array_new(0);
    _collect_options(elem->first_child, arr);
    int64_t n = js_array_length(arr);
    int found = -1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
        if (!opt) continue;
        char* option_value = _option_value(opt);
        bool match = option_value && strcmp(option_value, value) == 0;
        mem_free(option_value);
        if (match) { found = (int)i; break; } // INT_CAST_OK: option index
    }
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
        if (!opt) continue;
        _set_selectedness(opt, found >= 0 && (int)i == found); // INT_CAST_OK: option index
    }
    if (sync_native_index) {
        _select_sync_native_selected_index(elem, found,
            (int)n); // INT_CAST_OK: option count
    }
    return found;
}

extern "C" void js_dom_select_set_value_bridge(void* dom_elem, const char* value) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return;
    const char* sv = value ? value : "";
    js_dom_select_apply_value(elem, sv, true);
    js_dom_expando_flag_set(elem, "__selDirty", (Item){.item = b2it(true)});
}
JS_FORWARD_VOID( js_dom_select_set_selected_index_bridge, (void* dom_elem, Item value), _select_set_selected_index, ((DomElement*)dom_elem, _select_index_from_item(value)))

extern "C" void js_dom_select_set_length_bridge(void* dom_elem, Item value) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return;
    int new_len = 0;
    TypeId t = get_type_id(value);
    if (t == LMD_TYPE_INT) new_len = (int)it2i(value); // INT_CAST_OK: option count
    else if (t == LMD_TYPE_FLOAT) {
        // self-tagged float Items have no boxed payload; select length uses
        // the shared numeric decoder before truncating to a DOM option count.
        new_len = (int)it2d(value); // INT_CAST_OK: option count
    }
    if (new_len < 0) new_len = 0;
    Item arr = js_array_new(0);
    _collect_options(elem->first_child, arr);
    int64_t cur = js_array_length(arr);
    if (new_len > cur) {
        int to_add = new_len - (int)cur; // INT_CAST_OK: option count
        DomDocument* doc = elem->doc;
        for (int i = 0; i < to_add; i++) {
            if (!doc || !doc->input) break;
            MarkBuilder builder(doc->input);
            Item nat_item = builder.element("option").final();
            Element* nat = nat_item.element;
            DomElement* opt = dom_element_create(doc, "option", nat);
            if (!opt) break;
            opt->parent = elem;
            if (!elem->first_child) {
                elem->first_child = opt;
                elem->last_child = opt;
            } else {
                DomNode* last = elem->last_child;
                last->next_sibling = opt;
                opt->prev_sibling = last;
                elem->last_child = opt;
            }
        }
    } else if (new_len < cur) {
        for (int64_t i = cur - 1; i >= new_len; i--) {
            DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
            if (!opt) continue;
            DomNode* on = (DomNode*)opt;
            DomNode* parent = on->parent;
            if (!parent) continue;
            DomElement* pe = (DomElement*)parent;
            if (on->prev_sibling) on->prev_sibling->next_sibling = on->next_sibling;
            else pe->first_child = on->next_sibling;
            if (on->next_sibling) on->next_sibling->prev_sibling = on->prev_sibling;
            else pe->last_child = on->prev_sibling;
            on->parent = nullptr;
            on->next_sibling = nullptr;
            on->prev_sibling = nullptr;
        }
    }
    // select.length mutates the option subtree, so keep the JS mutation ledger authoritative.
    js_dom_mutation_notify();
}

static void js_dom_apply_option_selected(DomElement* elem, bool selected) {
    _set_selectedness(elem, selected);
    Item exp = expando_get_or_create_map((DomNode*)elem);
    if (exp.item != ITEM_NULL) {
        js_set_key_cstr(exp, "__optDirty", (Item){.item = b2it(true)});
    }
    // Explicit option.selected wins in non-multiple selects, then refreshes
    // cached selectedOptions exactly as the fallback setter did.
    DomElement* sel = _option_owner_select(elem);
    if (sel && selected && !sel->has_attribute("multiple")) {
        _select_select_only_option(sel, elem);
    } else if (sel) {
        _select_ask_for_reset(sel);
    }
    _select_refresh_cached_selected_options(sel);
}

extern "C" void js_dom_set_option_selected_dirty(void* dom_elem, bool selected) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return;
    js_dom_apply_option_selected(elem, selected);
}

extern "C" void js_dom_set_option_text_bridge(void* dom_elem, const char* value) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return;
    const char* sv = value ? value : "";
    DomNode* child = elem->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        child->parent = nullptr;
        child->next_sibling = nullptr;
        child->prev_sibling = nullptr;
        child = next;
    }
    elem->first_child = nullptr;
    elem->last_child = nullptr;
    DomText* tn = DomText::create_copy(sv, strlen(sv), elem);
    if (tn) {
        tn->parent = elem;
        elem->first_child = tn;
        elem->last_child = tn;
    }
    // option.text replaces children, so publish a structural mutation instead of an attribute record.
    js_dom_mutation_notify();
}

// For a <select required> element, returns true iff no <option> is
// "valueMissing" for a <select required>: the select has no selected option,
// OR all selected options are placeholder label options.
// A placeholder label option = the first option child of the select whose
// value is "" AND text is empty. Only applies when display size is 1 and
// multiple is unset; otherwise placeholders are not recognized.
static bool _select_value_missing(DomElement* sel) {
    if (!sel) return true;
    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    int64_t n = js_array_length(arr);
    if (n == 0) return true;
    int size = 0;
    const char* sz = sel->get_attribute("size");
    if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
    bool is_listbox = sel->has_attribute("multiple") || size > 1;
    // Identify the placeholder option: the first option in the select's
    // option list, only if it is a direct child of the select and has empty
    // value. Options inside an optgroup don't qualify.
    DomElement* placeholder = nullptr;
    if (!is_listbox && n > 0) {
        DomElement* first_opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, 0));
        if (first_opt && first_opt->parent == (DomNode*)sel) {
            char* v = _option_value(first_opt);
            bool empty_value = !v || !*v;
            mem_free(v);
            if (empty_value) placeholder = first_opt;
        }
    }
    bool any_non_placeholder_selected = false;
    bool any_selected = false;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
        if (!opt) continue;
        if (_get_selectedness(opt)) {
            any_selected = true;
            if (opt != placeholder) { any_non_placeholder_selected = true; break; }
        }
    }
    // Apply default-reset: if no option selected and not dirty/listbox, the
    // first non-disabled option counts as selected.
    if (!any_selected && !is_listbox && !_select_is_dirty(sel)) {
        for (int64_t i = 0; i < n; i++) {
            DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
            if (!opt || opt->has_attribute("disabled")) continue;
            any_selected = true;
            if (opt != placeholder) any_non_placeholder_selected = true;
            break;
        }
    }
    return !any_non_placeholder_selected;
}

// ----------------------------------------------------------------------
// F-3: Form reset algorithm
// ----------------------------------------------------------------------

// Reset a single form-control element to its default state, per HTML
// spec §4.10.21.4 "Form reset" + each control's reset algorithm.
static void _reset_form_control(DomElement* elem) {
    if (!elem || !elem->tag_name) return;
    const char* tag = elem->tag_name;
    if (strcasecmp(tag, "input") == 0) {
        const char* itype = _input_type_lower(elem);
        if (strcmp(itype, "checkbox") == 0 || strcmp(itype, "radio") == 0) {
            // checked := defaultChecked (presence of "checked" content attr)
            bool default_checked = elem->has_attribute("checked");
            _set_checkedness(elem, default_checked);
            // Clear dirty checkedness flag.
            Item exp = expando_get_map((DomNode*)elem);
            if (exp.item != ITEM_NULL) {
                js_set_key_cstr(exp, "__chkDirty", (Item){.item = ITEM_NULL});
            }
            return;
        }
        if (strcmp(itype, "submit") == 0 || strcmp(itype, "reset") == 0 ||
            strcmp(itype, "button") == 0 || strcmp(itype, "image") == 0 ||
            strcmp(itype, "hidden") == 0 || strcmp(itype, "file") == 0) {
            // File reset clears the selected FileList; its public value is
            // derived from that list and must never restore the value attribute.
            if (strcmp(itype, "file") == 0) {
                radiant_input_set_files(elem, ItemNull);
                radiant_input_set_live_value(elem, "");
            }
            return;
        }
        RadiantInputValueKind value_kind = radiant_input_value_kind(itype);
        if (value_kind != RADIANT_INPUT_VALUE_TEXT &&
            value_kind != RADIANT_INPUT_VALUE_UNSUPPORTED) {
            radiant_input_reset_live_value(elem);
            return;
        }
        // Text-like input: value := defaultValue (= value attribute)
        if (tc_is_text_control_elem(elem)) {
            tc_ensure_init(elem);
            const char* dv = elem->get_attribute("value");
            if (!dv) dv = "";
            tc_set_value(elem, dv, strlen(dv));
            js_dom_expando_flag_set(elem, "__valueDirty", (Item){.item = ITEM_NULL});
        }
        return;
    }
    if (strcasecmp(tag, "textarea") == 0) {
        if (tc_is_text_control_elem(elem)) {
            tc_ensure_init(elem);
            // textarea defaultValue = descendant text content of original markup
            StrBuf* sb = strbuf_new_cap(64);
            collect_text_content((DomNode*)elem, sb);
            const char* s = sb->str ? sb->str : "";
            tc_set_value(elem, s, sb->length);
            js_dom_expando_flag_set(elem, "__valueDirty", (Item){.item = ITEM_NULL});
            strbuf_free(sb);
        }
        return;
    }
    if (strcasecmp(tag, "select") == 0) {
        // Reset selectedness of all options to their defaults, then run
        // ask-for-reset for non-multiple selects.
        Item arr = js_array_new(0);
        _collect_options(elem->first_child, arr);
        int64_t n = js_array_length(arr);
        for (int64_t i = 0; i < n; i++) {
            DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
            if (!opt) continue;
            // Default selectedness = presence of "selected" content attribute
            _set_selectedness(opt, opt->has_attribute("selected"));
            // Clear per-option dirty selectedness flag.
            Item oexp = expando_get_map((DomNode*)opt);
            if (oexp.item != ITEM_NULL) {
                js_set_key_cstr(oexp, "__optDirty", (Item){.item = ITEM_NULL});
            }
        }
        // Clear the dirty flag so default-reset rules apply again.
        Item exp = expando_get_map((DomNode*)elem);
        if (exp.item != ITEM_NULL) {
            js_set_name_key(exp, "__selDirty", (Item){.item = ITEM_NULL});
        }
        if (!elem->has_attribute("multiple")) {
            _select_ask_for_reset(elem);
        }
        return;
    }
    if (strcasecmp(tag, "output") == 0) {
        // For <output>: textContent := defaultValue. We do not track an
        // explicit "default value override" (defaultValue setter); without
        // an override, defaultValue == descendant text content, so reset
        // is effectively a no-op. This matches WPT reset-form expectations.
        return;
    }
}

// Run the HTML form reset algorithm on a form element. Walks all listed
// controls associated with this form (whether descendants or associated by
// the `form="<id>"` attribute) and resets each. Caller is responsible for
// dispatching the "reset" event before invoking; this just runs the
// per-control reset steps.
// Reset pass over a form's controls. The descendant pass owns any control
// whose nearest enclosing form is this one; the associated pass, run over the
// whole document afterwards, owns only controls that name the form through
// their `form` attribute, and skips the form's own subtree so a control is
// never reset twice.
typedef struct FormResetCtx {
    DomElement* form_elem;
    DomElement* doc_root;
    bool associated_only;
} FormResetCtx;

static bool _is_form_control_tag(DomElement* elem) {
    return elem->tag_name &&
        (strcasecmp(elem->tag_name, "input") == 0 ||
         strcasecmp(elem->tag_name, "textarea") == 0 ||
         strcasecmp(elem->tag_name, "select") == 0 ||
         strcasecmp(elem->tag_name, "output") == 0);
}

// nearest ancestor <form> of `elem`, stopping at (and including) `limit`
static DomElement* _nearest_enclosing_form(DomElement* elem, DomElement* limit) {
    for (DomNode* p = elem->parent; p; p = p->parent) {
        if (p == (DomNode*)limit) return limit;
        if (!p->is_element()) continue;
        DomElement* pe = (DomElement*)p;
        if (pe->tag_name && strcasecmp(pe->tag_name, "form") == 0) return pe;
    }
    return NULL;
}

static bool _form_reset_visit(DomElement* elem, void* ctx) {
    FormResetCtx* state = (FormResetCtx*)ctx;
    if (state->associated_only && elem == state->form_elem) return false;
    if (!elem->tag_name) return false;
    if (_is_form_control_tag(elem)) {
        const char* form_attr = elem->get_attribute("form");
        DomElement* owner = NULL;
        if (form_attr && *form_attr) {
            owner = state->doc_root
                ? js_dom_find_element_by_id(state->doc_root, form_attr) : NULL;
        } else if (!state->associated_only) {
            owner = _nearest_enclosing_form(elem, state->form_elem);
        }
        if (owner == state->form_elem) _reset_form_control(elem);
    }
    return true;
}

static void _run_form_reset(DomElement* form_elem) {
    if (!form_elem) return;
    DomDocument* doc = _js_current_document;
    DomElement* doc_root = doc ? doc->root : nullptr;
    // Determine if form is connected to the document.
    bool form_in_doc = false;
    if (doc_root) {
        DomNode* p = (DomNode*)form_elem;
        while (p) {
            if (p == (DomNode*)doc_root) { form_in_doc = true; break; }
            p = p->parent;
        }
    }
    // First pass: descendant controls (always walk these).
    FormResetCtx reset_ctx = { form_elem, doc_root, false };
    dom_walk_elements(form_elem->first_child, _form_reset_visit, &reset_ctx);
    // For associated controls (via `form` attribute) outside the form
    // subtree, walk the rest of the document — but skip the form_elem
    // subtree to avoid double-resetting.
    if (form_in_doc && doc_root && doc_root != form_elem) {
        reset_ctx.associated_only = true;
        dom_walk_elements((DomNode*)doc_root, _form_reset_visit, &reset_ctx);
    }
}

// Constraint validation for a radio group: does any control with this name in
// the same form scope carry `checked`, and does any of them carry `required`?
typedef struct RadioGroupScanCtx {
    const char* name;
    DomElement* form_scope;
    bool* any_checked;
    bool* any_required;
} RadioGroupScanCtx;

static bool _radio_group_visit(DomElement* elem, void* ctx) {
    RadioGroupScanCtx* state = (RadioGroupScanCtx*)ctx;
    if (!elem->tag_name || strcasecmp(elem->tag_name, "input") != 0) return true;
    if (strcmp(js_dom_input_type_lower(elem), "radio") != 0) return true;
    const char* name = elem->get_attribute("name");
    if (!name || strcmp(name, state->name) != 0) return true;
    if (_nearest_enclosing_form(elem, NULL) == state->form_scope) {
        if (js_dom_get_checkedness(elem)) *state->any_checked = true;
        if (elem->has_attribute("required")) *state->any_required = true;
    }
    return true;
}

static Item _build_validity_state(DomElement* elem) {
    Item vs = js_new_object();
    // Set Symbol.toStringTag = "ValidityState" so
    // Object.prototype.toString.call(validity) === "[object ValidityState]"
    js_set_key_default(vs, js_well_known_symbol_key(4), js_name_item("ValidityState"));
    bool value_missing   = false;
    bool type_mismatch   = false;
    bool pattern_mismatch = false;
    bool too_long        = false; // always false: requires interactive editing
    bool too_short       = false; // always false: requires interactive editing
    bool range_overflow  = false;
    bool range_underflow = false;
    bool step_mismatch   = false;
    bool bad_input       = false; // always false in headless
    bool custom_error    = false;

    // Elements barred from constraint validation (disabled, readonly,
    // hidden, datalist descendants, etc.) report all suffering flags
    // as false. customError flag is also suppressed since checkValidity
    // returns true for barred elements.
    bool barred = !elem || _elem_is_barred(elem);

    if (elem) {
        // customError
        if (elem->form && elem->form->custom_validity_msg &&
            elem->form->custom_validity_msg[0] != '\0') {
            custom_error = true;
        }

        const char* tag = elem->tag_name ? elem->tag_name : "";
        const char* val = _elem_current_value(elem);
        bool val_empty  = (val[0] == '\0');

        // Typed value setters already sanitize through the module codec. Keeping
        // validity on that same grammar prevents calendar and step semantics
        // from disagreeing with the value exposed through the IDL.
        if (!val_empty && strcasecmp(tag, "input") == 0) {
            char sanitized[128];
            radiant_input_value_sanitize(js_dom_input_type_lower(elem), val,
                                          sanitized, sizeof(sanitized));
            val_empty = sanitized[0] == '\0';
        }

        // valueMissing
        // Special-case radio: valueMissing applies to ALL members of the
        // group when any member is required and none is checked, even if
        // this particular element does not carry the required attribute.
        bool radio_handled = false;
        if (strcasecmp(tag, "input") == 0) {
            const char* itype0 = js_dom_input_type_lower(elem);
            if (strcmp(itype0, "radio") == 0) {
                radio_handled = true;
                const char* rname = elem->get_attribute("name");
                bool elem_connected = js_dom_is_connected(elem);
                bool own_required = elem->has_attribute("required");
                if (!rname || !*rname) {
                    // Empty/missing name → no group. WPT expects
                    // valueMissing=false even if required.
                    value_missing = false;
                } else if (!elem_connected) {
                    value_missing = own_required && !js_dom_get_checkedness(elem);
                } else {
                    // Find the ancestor <form>, if any.
                    DomElement* form_scope = nullptr;
                    for (DomNode* p = elem->parent; p; p = p->parent) {
                        if (p->is_element()) {
                            DomElement* pe = (DomElement*)p;
                            if (pe->tag_name && strcasecmp(pe->tag_name, "form") == 0) {
                                form_scope = pe; break;
                            }
                        }
                    }
                    DomElement* root = form_scope ? form_scope :
                        (_js_current_document ? (DomElement*)_js_current_document->root : nullptr);
                    bool any_checked = false;
                    bool any_required = own_required;
                    RadioGroupScanCtx scan_ctx = {
                        rname, form_scope, &any_checked, &any_required
                    };
                    if (root) dom_walk_elements(root->first_child, _radio_group_visit, &scan_ctx);
                    value_missing = any_required && !any_checked;
                }
            }
        }

        if (!radio_handled && elem->has_attribute("required")) {
            if (strcasecmp(tag, "input") == 0) {
                const char* itype = js_dom_input_type_lower(elem);
                if (strcmp(itype, "checkbox") == 0) {
                    value_missing = !js_dom_get_checkedness(elem);
                } else if (strcmp(itype, "file") == 0) {
                    Item files = radiant_input_files(elem);
                    value_missing = get_type_id(files) != LMD_TYPE_ARRAY ||
                                    js_array_length(files) == 0;
                } else {
                    value_missing = val_empty;
                }
            } else if (strcasecmp(tag, "select") == 0) {
                // select with required: missing iff no option selected
                // OR the selected option has empty value.
                value_missing = _select_value_missing(elem);
            } else {
                value_missing = val_empty;
            }
        }

        // typeMismatch: email / url with non-empty invalid value
        if (!val_empty && strcasecmp(tag, "input") == 0) {
            const char* itype = js_dom_input_type_lower(elem);
            if (strcmp(itype, "email") == 0) {
                // simple email check: must contain @
                type_mismatch = (strchr(val, '@') == nullptr);
            } else if (strcmp(itype, "url") == 0) {
                // simple url check: must start with a scheme like https:// or http://
                type_mismatch = !(strncmp(val, "http://", 7) == 0 ||
                                  strncmp(val, "https://", 8) == 0 ||
                                  strncmp(val, "ftp://", 6) == 0 ||
                                  strncmp(val, "ftps://", 7) == 0 ||
                                  strncmp(val, "file://", 7) == 0 ||
                                  strncmp(val, "blob:", 5) == 0 ||
                                  strncmp(val, "data:", 5) == 0);
            }
        }

        // patternMismatch: pattern attr + non-empty value + regex doesn't match
        if (!val_empty && !type_mismatch && strcasecmp(tag, "input") == 0) {
            const char* pattern = elem->get_attribute("pattern");
            if (pattern && *pattern) {
                // HTML pattern anchors the whole value (^(?:pattern)$)
                // Build anchored pattern
                size_t plen = strlen(pattern);
                char* full_pattern = (char*)mem_alloc(plen + 8, MEM_CAT_JS_RUNTIME);
                if (full_pattern) {
                    snprintf(full_pattern, plen + 8, "^(?:%s)$", pattern);
                    Item re = js_create_regex(full_pattern, (int)strlen(full_pattern), "", 0);
                    mem_free(full_pattern);
                    Item val_item = js_name_item(val);
                    Item result = js_regex_test(re, val_item);
                    // pattern mismatch if regex does NOT match
                    pattern_mismatch = !((result.item & 0xFF) != 0 && result.item != ITEM_NULL);
                }
            }
        }

        // Numeric value states share their scalar conversion and step base with
        // valueAsNumber/stepUp, including ISO week and calendar-month rules.
        if (!val_empty && strcasecmp(tag, "input") == 0) {
            const char* itype = js_dom_input_type_lower(elem);
            RadiantInputValidity typed = {};
            radiant_input_value_validate(itype, val,
                elem->get_attribute("min"),
                elem->get_attribute("max"),
                elem->get_attribute("step"), &typed);
            range_overflow = typed.range_overflow;
            range_underflow = typed.range_underflow;
            step_mismatch = typed.step_mismatch;
        }
    }

    bool valid = !(value_missing || type_mismatch || pattern_mismatch || too_long ||
                   too_short || range_overflow || range_underflow || step_mismatch ||
                   bad_input || custom_error);

    // Per HTML spec, the suffering-from-being-missing algorithm requires
    // the element to be "mutable" — only for text-like inputs and
    // textareas. Checkbox/radio/select/file controls keep their
    // valueMissing flag even when barred (they're not "mutable" per say,
    // but their suffering condition isn't gated on mutability).
    if (barred) {
        const char* tag = elem ? elem->tag_name : "";
        bool is_input = elem && tag && strcasecmp(tag, "input") == 0;
        bool gate_value_missing = false;
        if (is_input) {
            const char* itype = js_dom_input_type_lower(elem);
            // gate for text-like types only
            if (strcmp(itype, "checkbox") != 0 && strcmp(itype, "radio") != 0 &&
                strcmp(itype, "file") != 0) {
                gate_value_missing = true;
            }
        } else if (tag && (strcasecmp(tag, "textarea") == 0 ||
                           strcasecmp(tag, "select") == 0)) {
            // textarea is text-like; select per spec also gated.
            // WPT shows select expected==expectedImmutable so gate it.
            gate_value_missing = (strcasecmp(tag, "textarea") == 0);
        }
        if (gate_value_missing) value_missing = false;
        too_long = false;
        too_short = false;
        valid = !(value_missing || type_mismatch || pattern_mismatch ||
                  range_overflow || range_underflow || step_mismatch ||
                  bad_input || custom_error);
    }

    auto _b = [](bool v) -> Item { return (Item){.item = b2it(v)}; };
#define JS_DOM_VALIDITY_FIELDS(M) \
    M("valueMissing", value_missing) M("typeMismatch", type_mismatch) \
    M("patternMismatch", pattern_mismatch) M("tooLong", too_long) \
    M("tooShort", too_short) M("rangeOverflow", range_overflow) \
    M("rangeUnderflow", range_underflow) M("stepMismatch", step_mismatch) \
    M("badInput", bad_input) M("customError", custom_error) M("valid", valid)
#define JS_DOM_SET_VALIDITY_FIELD(name, value) js_set_key_cstr(vs, name, _b(value));
    JS_DOM_VALIDITY_FIELDS(JS_DOM_SET_VALIDITY_FIELD)
#undef JS_DOM_SET_VALIDITY_FIELD
#undef JS_DOM_VALIDITY_FIELDS
    return vs;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_validity_item_is_valid, (Item flag), (((flag.item & 0xFF) != 0 && flag.item != ITEM_NULL)))

static bool js_dom_is_constraint_control(DomElement* elem) {
    if (!elem || !elem->tag_name || _elem_is_barred(elem)) return false;
    return strcasecmp(elem->tag_name, "input") == 0 ||
           strcasecmp(elem->tag_name, "select") == 0 ||
           strcasecmp(elem->tag_name, "textarea") == 0 ||
           strcasecmp(elem->tag_name, "button") == 0;
}

static void js_dom_dispatch_invalid_event(Item target_item, bool include_bubbles) {
    Item ev_obj = js_new_object();
    js_set_key_cstr(ev_obj, "type", js_name_item("invalid"));
    if (include_bubbles) {
        js_set_key_cstr(ev_obj, "bubbles", (Item){.item = ITEM_FALSE});
    }
    js_set_key_cstr(ev_obj, "cancelable", (Item){.item = ITEM_TRUE});
    js_dom_dispatch_event(target_item, ev_obj);
}

static void js_dom_check_form_control_descendants(DomNode* node, bool* all_valid) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (js_dom_is_constraint_control(elem)) {
                Item vs = _build_validity_state(elem);
                Item vf = js_get_key_cstr(vs, "valid");
                if (!js_dom_validity_item_is_valid(vf)) {
                    if (all_valid) *all_valid = false;
                    js_dom_dispatch_invalid_event(js_dom_wrap_element(elem), false);
                }
            }
            js_dom_check_form_control_descendants(elem->first_child, all_valid);
        }
        node = node->next_sibling;
    }
}

extern "C" Item js_dom_form_reset_bridge(Item form_item) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(form_item);
    if (!elem || !elem->tag_name || strcasecmp(elem->tag_name, "form") != 0) {
        return make_js_undefined();
    }
    Item ev = js_create_event("reset", /*bubbles=*/true, /*cancelable=*/true);
    js_set_key_cstr(ev, "isTrusted", (Item){.item = ITEM_TRUE});
    Item dispatched = js_dom_dispatch_event(form_item, ev);
    if (dispatched.item == ITEM_FALSE) return make_js_undefined();
    _run_form_reset(elem);
    return make_js_undefined();
}

static Item js_dom_check_or_report_validity(Item elem_item, bool report) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem || !elem->tag_name) return (Item){.item = ITEM_TRUE};

    if (strcasecmp(elem->tag_name, "form") == 0) {
        bool all_valid = true;
        js_dom_check_form_control_descendants(elem->first_child, &all_valid);
        return (Item){.item = b2it(all_valid)};
    }

    if (_elem_is_barred(elem)) return (Item){.item = ITEM_TRUE};
    Item vs = _build_validity_state(elem);
    Item valid_flag = js_get_key_cstr(vs, "valid");
    bool is_valid = js_dom_validity_item_is_valid(valid_flag);
    if (!is_valid) {
        js_dom_dispatch_invalid_event(elem_item, !report);
    }
    return (Item){.item = b2it(is_valid)};
}
JS_FORWARD_ITEM(js_dom_check_validity_bridge, (Item elem_item), js_dom_check_or_report_validity, (elem_item, false))
JS_FORWARD_ITEM(js_dom_report_validity_bridge, (Item elem_item), js_dom_check_or_report_validity, (elem_item, true))

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// F-0: reflected IDL attributes.
//
// One row per (IDL name, content attribute) pair states which elements
// reflect it and how the value is typed. The five predicates below are all
// filtered lookups over this table; they used to be five hand-written
// strcmp/strcasecmp chains that disagreed about which elements support what
// (see C0.5).
//   X(idl, attr, KIND, default, tags)
// KIND is BOOL (presence), INT (non-negative integer with a default), STR
// (string, missing value "") or MAP (name mapping only — the value handling
// lives in a dedicated case). `tags` is a bitmask; DOM_TAG_ANY means every
// element reflects it, as global attributes do.
// ----------------------------------------------------------------------
#define JS_DOM_TAGS(X) \
    X(INPUT, "input") X(BUTTON, "button") X(SELECT, "select") \
    X(TEXTAREA, "textarea") X(FORM, "form") X(DETAILS, "details") \
    X(FIELDSET, "fieldset") X(OPTION, "option") X(OPTGROUP, "optgroup") \
    X(CANVAS, "canvas") X(IMG, "img") X(SCRIPT, "script") X(IFRAME, "iframe") \
    X(EMBED, "embed") X(SOURCE, "source") X(TRACK, "track") X(AUDIO, "audio") \
    X(VIDEO, "video") X(A, "a") X(AREA, "area") X(LINK, "link") \
    X(BASE, "base") X(LABEL, "label") X(OUTPUT, "output")

enum {
#define JS_DOM_TAG_BIT_ENUM(name, text) DOM_TAG_##name##_INDEX,
    JS_DOM_TAGS(JS_DOM_TAG_BIT_ENUM)
#undef JS_DOM_TAG_BIT_ENUM
    DOM_TAG_COUNT
};
#define JS_DOM_TAG_BIT_CONST(name, text) \
    static const uint32_t DOM_TAG_##name = 1u << DOM_TAG_##name##_INDEX;
JS_DOM_TAGS(JS_DOM_TAG_BIT_CONST)
#undef JS_DOM_TAG_BIT_CONST
static const uint32_t DOM_TAG_ANY = 0xFFFFFFFFu;

static uint32_t js_dom_tag_bit(const DomElement* elem) {
    if (!elem || !elem->tag_name) return 0;
#define JS_DOM_TAG_BIT_MATCH(name, text) \
    if (strcasecmp(elem->tag_name, text) == 0) return DOM_TAG_##name;
    JS_DOM_TAGS(JS_DOM_TAG_BIT_MATCH)
#undef JS_DOM_TAG_BIT_MATCH
    return 0;
}

typedef enum JsDomReflectKind {
    JS_DOM_REFLECT_BOOL = 0,
    JS_DOM_REFLECT_INT,
    JS_DOM_REFLECT_STR,
    JS_DOM_REFLECT_MAP,
} JsDomReflectKind;

// Rows are scanned in order, so a property reflected differently on two
// element types (input.size defaults to 20, select.size to 0) lists the more
// specific row first.
#define JS_DOM_REFLECTED_ATTRS(X) \
    X("disabled", "disabled", BOOL, 0, DOM_TAG_INPUT | DOM_TAG_BUTTON | DOM_TAG_SELECT | \
        DOM_TAG_TEXTAREA | DOM_TAG_FIELDSET | DOM_TAG_OPTION | DOM_TAG_OPTGROUP) \
    X("required", "required", BOOL, 0, DOM_TAG_INPUT | DOM_TAG_SELECT | DOM_TAG_TEXTAREA) \
    X("multiple", "multiple", BOOL, 0, DOM_TAG_INPUT | DOM_TAG_SELECT) \
    X("readOnly", "readonly", BOOL, 0, DOM_TAG_INPUT | DOM_TAG_TEXTAREA) \
    X("readonly", "readonly", BOOL, 0, DOM_TAG_INPUT | DOM_TAG_TEXTAREA) \
    X("noValidate", "novalidate", BOOL, 0, DOM_TAG_FORM) \
    X("formNoValidate", "formnovalidate", BOOL, 0, DOM_TAG_INPUT | DOM_TAG_BUTTON) \
    X("autofocus", "autofocus", BOOL, 0, DOM_TAG_ANY) \
    X("open", "open", BOOL, 0, DOM_TAG_DETAILS) \
    X("defaultChecked", "checked", BOOL, 0, DOM_TAG_INPUT) \
    X("defaultSelected", "selected", BOOL, 0, DOM_TAG_OPTION) \
    X("maxLength", "maxlength", INT, -1, DOM_TAG_INPUT | DOM_TAG_TEXTAREA) \
    X("minLength", "minlength", INT, 0, DOM_TAG_INPUT | DOM_TAG_TEXTAREA) \
    X("size", "size", INT, 20, DOM_TAG_INPUT) \
    X("width", "width", INT, 0, DOM_TAG_INPUT | DOM_TAG_CANVAS) \
    X("height", "height", INT, 0, DOM_TAG_INPUT | DOM_TAG_CANVAS) \
    X("size", "size", INT, 0, DOM_TAG_SELECT) \
    X("rows", "rows", INT, 2, DOM_TAG_TEXTAREA) \
    X("cols", "cols", INT, 20, DOM_TAG_TEXTAREA) \
    X("src", "src", STR, 0, DOM_TAG_IMG | DOM_TAG_SCRIPT | DOM_TAG_IFRAME | DOM_TAG_EMBED | \
        DOM_TAG_SOURCE | DOM_TAG_TRACK | DOM_TAG_AUDIO | DOM_TAG_VIDEO | DOM_TAG_INPUT) \
    X("href", "href", STR, 0, DOM_TAG_A | DOM_TAG_AREA | DOM_TAG_LINK | DOM_TAG_BASE) \
    X("alt", "alt", STR, 0, DOM_TAG_IMG) \
    X("width", "width", STR, 0, DOM_TAG_IFRAME) \
    X("height", "height", STR, 0, DOM_TAG_IFRAME) \
    X("tabIndex", "tabindex", MAP, 0, DOM_TAG_ANY) \
    X("inputMode", "inputmode", MAP, 0, DOM_TAG_ANY) \
    X("enterKeyHint", "enterkeyhint", MAP, 0, DOM_TAG_ANY) \
    X("contentEditable", "contenteditable", MAP, 0, DOM_TAG_ANY) \
    X("acceptCharset", "accept-charset", MAP, 0, DOM_TAG_FORM) \
    X("htmlFor", "for", MAP, 0, DOM_TAG_LABEL | DOM_TAG_OUTPUT) \
    X("formAction", "formaction", MAP, 0, DOM_TAG_INPUT | DOM_TAG_BUTTON) \
    X("formMethod", "formmethod", MAP, 0, DOM_TAG_INPUT | DOM_TAG_BUTTON) \
    X("formEnctype", "formenctype", MAP, 0, DOM_TAG_INPUT | DOM_TAG_BUTTON) \
    X("formEncoding", "formenctype", MAP, 0, DOM_TAG_INPUT | DOM_TAG_BUTTON) \
    X("formTarget", "formtarget", MAP, 0, DOM_TAG_INPUT | DOM_TAG_BUTTON)

typedef struct JsDomReflectSpec {
    const char* idl;
    const char* attr;
    JsDomReflectKind kind;
    int default_value;
    uint32_t tags;
} JsDomReflectSpec;

static const JsDomReflectSpec k_dom_reflected_attrs[] = {
#define JS_DOM_REFLECT_ROW(idl, attr, kind, dflt, tags) \
    { idl, attr, JS_DOM_REFLECT_##kind, dflt, tags },
    JS_DOM_REFLECTED_ATTRS(JS_DOM_REFLECT_ROW)
#undef JS_DOM_REFLECT_ROW
};

// First row whose IDL name and element type both match, or NULL.
static const JsDomReflectSpec* js_dom_reflect_spec(const DomElement* elem,
                                                   const char* prop,
                                                   JsDomReflectKind kind) {
    if (!elem || !prop) return NULL;
    uint32_t tag = js_dom_tag_bit(elem);
    if (!tag) return NULL;
    for (size_t i = 0; i < sizeof(k_dom_reflected_attrs) / sizeof(k_dom_reflected_attrs[0]); i++) {
        const JsDomReflectSpec* spec = &k_dom_reflected_attrs[i];
        if (spec->kind != kind || (spec->tags & tag) == 0) continue;
        if (strcmp(spec->idl, prop) == 0) return spec;
    }
    return NULL;
}

// IDL-name → HTML-attribute-name mapping, independent of element type.
// Returns nullptr when the names are the same (caller uses prop verbatim).
static const char* _idl_to_attr_name(const char* prop) {
    if (!prop) return nullptr;
    for (size_t i = 0; i < sizeof(k_dom_reflected_attrs) / sizeof(k_dom_reflected_attrs[0]); i++) {
        const JsDomReflectSpec* spec = &k_dom_reflected_attrs[i];
        if (strcmp(spec->idl, prop) != 0) continue;
        return strcmp(spec->idl, spec->attr) == 0 ? nullptr : spec->attr;
    }
    return nullptr;
}

// True if `prop` reflects a HTML boolean attribute on the given element.
// Per HTML spec, IDL boolean reflection setters do ToBoolean coercion:
// truthy → set attribute (empty value), falsy → remove attribute.
static bool _is_bool_reflected(DomElement* elem, const char* prop) {
    return js_dom_reflect_spec(elem, prop, JS_DOM_REFLECT_BOOL) != NULL;
}

// True if `prop` reflects a non-negative-integer-with-default attribute.
// Sets *attr_name to the HTML attribute name and *default_val to the
// IDL default returned when attribute is missing or invalid.
static bool _is_int_reflected(DomElement* elem, const char* prop,
                              const char** attr_name, int* default_val) {
    if (!attr_name || !default_val) return false;
    const JsDomReflectSpec* spec = js_dom_reflect_spec(elem, prop, JS_DOM_REFLECT_INT);
    if (!spec) return false;
    *attr_name = spec->attr;
    *default_val = spec->default_value;
    return true;
}

// True if `prop` reflects a string-valued content attribute whose missing
// value is the empty string in the DOM IDL layer.
//
// HTMLIFrameElement width/height are here rather than in the integer group:
// they reflect as strings, and keeping them reflected makes IDL writes
// trigger cascade invalidation instead of becoming inert JS expandos.
static bool _is_string_reflected(DomElement* elem, const char* prop,
                                 const char** attr_name) {
    if (!attr_name) return false;
    const JsDomReflectSpec* spec = js_dom_reflect_spec(elem, prop, JS_DOM_REFLECT_STR);
    if (!spec) return false;
    *attr_name = spec->attr;
    return true;
}

// C0.5: the setter applied the _idl_to_attr_name() mapping on any element, so
// e.g. `div.htmlFor = 'x'` wrote a `for` content attribute that the getter —
// which gates every one of these pairs by tag — then refused to read back, and
// `div.readOnly = true` wrote `readonly=""` instead of storing an expando.
// The table gates the write on the same element/property pairs the getter
// accepts.
static bool _is_mapped_attr_reflected(DomElement* elem, const char* prop) {
    return js_dom_reflect_spec(elem, prop, JS_DOM_REFLECT_MAP) != NULL;
}

// Returns the lowercased input `formmethod` value or "get" default.
static const char* _normalise_method(const char* v) {
    if (v) {
        if (strcasecmp(v, "post") == 0) return "post";
        if (strcasecmp(v, "dialog") == 0) return "dialog";
    }
    return "get";
}
static const char* _normalise_enctype(const char* v) {
    if (v) {
        if (strcasecmp(v, "multipart/form-data") == 0) return "multipart/form-data";
        if (strcasecmp(v, "text/plain") == 0) return "text/plain";
    }
    return "application/x-www-form-urlencoded";
}

// ----------------------------------------------------------------------
// F-1: Form-listed-element collection helpers
// ----------------------------------------------------------------------
// HTML §4.10.3 "form-associated elements" listed predicate. Excludes
// <input type=image> from `form.elements` (per spec note for the elements
// IDL attribute) but `<fieldset>` is included.
static bool _is_listed_form_control(DomElement* e) {
    if (!e || !e->tag_name) return false;
    const char* t = e->tag_name;
    if (strcasecmp(t, "input") == 0) {
        const char* it = _input_type_lower(e);
        return strcmp(it, "image") != 0;
    }
    return strcasecmp(t, "button") == 0 ||
           strcasecmp(t, "select") == 0 ||
           strcasecmp(t, "textarea") == 0 ||
           strcasecmp(t, "fieldset") == 0 ||
           strcasecmp(t, "object") == 0 ||
           strcasecmp(t, "output") == 0;
}

// Walk a subtree and append each listed control to `arr` (in tree order).
// Uses iterative-but-recursive traversal — safe given our shallow form trees.
static void _collect_form_related_rec(DomNode* node, Item arr, bool controls) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            bool selected = controls
                ? _is_listed_form_control(elem)
                : (elem->tag_name && strcasecmp(elem->tag_name, "form") == 0);
            if (selected) {
                js_array_push(arr, js_dom_wrap_element(elem));
            }
            _collect_form_related_rec(elem->first_child, arr, controls);
        }
        node = node->next_sibling;
    }
}
JS_FORWARD_STATIC_VOID( _collect_form_controls_rec, (DomNode* node, Item arr), _collect_form_related_rec, (node, arr, true))
JS_FORWARD_STATIC_VOID( _collect_document_forms_rec, (DomNode* node, Item forms), _collect_form_related_rec, (node, forms, false))

// Return the lowercased name/id key used to look up a form control by name.
// Returns nullptr if the element has neither.
static const char* _form_control_name_or_id(DomElement* e) {
    const char* n = e->get_attribute("name");
    if (n && *n) return n;
    if (e->id && *e->id) return e->id;
    return nullptr;
}

extern "C" int radiant_dom_m4b_href_get(Item receiver, Item* out);
extern "C" int radiant_dom_anchor_hash_get(Item receiver, Item* out);

static bool js_dom_remove_backed_child(DomElement* parent, DomNode* child);

static Item js_dom_template_content(DomElement* template_elem) {
    if (!template_elem || !template_elem->doc) return ItemNull;

    Item content_key = js_string_key("__lambda_template_content");
    Item cached = ItemNull;
    if (expando_get_property((DomNode*)template_elem, content_key, &cached)) {
        return cached;
    }

    DomElement* fragment = dom_element_create(
        template_elem->doc, "#document-fragment", nullptr);
    if (!fragment) return ItemNull;

    // template children belong to the detached template contents, not to the
    // rendered template element; move both DOM and backing entries together.
    DomNode* child = template_elem->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        if (!js_dom_remove_backed_child(template_elem, child) &&
            child->parent == (DomNode*)template_elem) {
            ((DomNode*)template_elem)->remove_child(child);
        }
        ((DomNode*)fragment)->append_child(child);
        child = next;
    }

    Item fragment_item = js_dom_wrap_element(fragment);
    expando_set_property((DomNode*)template_elem, content_key, fragment_item);
    return fragment_item;
}

static Item js_dom_collect_child_nodes(DomElement* elem, bool elements_only) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    DomNode* child = js_dom_first_script_visible_child(elem);
    while (child) {
        if (child->is_element()) {
            array_push(arr, js_dom_wrap_element(child->as_element()));
        } else if (!elements_only) {
            // text and comment nodes share the DOM wrapper path.
            array_push(arr, js_dom_wrap_element((DomElement*)(void*)child));
        }
        child = js_dom_next_script_visible_sibling(child);
    }
    return (Item){.array = arr};
}

// form[name] named getter: collect every listed control whose name or id
// matches.
typedef struct FormNamedGetterCtx {
    const char* name;
    Item matches;
} FormNamedGetterCtx;

static bool _form_named_getter_visit(DomElement* elem, void* ctx) {
    FormNamedGetterCtx* state = (FormNamedGetterCtx*)ctx;
    if (_is_listed_form_control(elem)) {
        const char* key = _form_control_name_or_id(elem);
        if (key && strcmp(key, state->name) == 0) {
            js_array_push(state->matches, js_dom_wrap_element(elem));
        }
    }
    return true;
}

static bool js_dom_get_textlike_property(DomNode* node, Item elem_item,
        Item prop_name, const char* prop, const char* content,
        int64_t length, int64_t node_type, const char* node_name,
        bool is_text, Item* result) {
    if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 ||
            strcmp(prop, "textContent") == 0) {
        *result = js_name_item(content ? content : "");
        return true;
    }
    if (strcmp(prop, "length") == 0) {
        *result = (Item){.item = i2it(length)};
        return true;
    }
    if (strcmp(prop, "nodeType") == 0) {
        *result = (Item){.item = i2it(node_type)};
        return true;
    }
    if (strcmp(prop, "nodeName") == 0) {
        *result = js_name_item(node_name);
        return true;
    }
    if (strcmp(prop, "parentNode") == 0) {
        *result = js_dom_parent_node_or_null(node);
        return true;
    }
    if (strcmp(prop, "parentElement") == 0) {
        *result = js_dom_parent_element_or_null(node);
        return true;
    }
    if (strcmp(prop, "isConnected") == 0) {
        *result = (Item){.item = b2it(js_dom_node_is_connected(node))};
        return true;
    }
    if (strcmp(prop, "nextSibling") == 0) {
        DomNode* sibling = js_dom_next_script_visible_sibling(node);
        *result = sibling ? js_dom_wrap_element((void*)sibling) : ItemNull;
        return true;
    }
    if (strcmp(prop, "previousSibling") == 0) {
        DomNode* sibling = js_dom_prev_script_visible_sibling(node);
        *result = sibling ? js_dom_wrap_element((void*)sibling) : ItemNull;
        return true;
    }
    if (strcmp(prop, "childNodes") == 0) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        *result = (Item){.array = arr};
        return true;
    }
    if (strcmp(prop, "firstChild") == 0 || strcmp(prop, "lastChild") == 0) {
        *result = ItemNull;
        return true;
    }
    if (strcmp(prop, "ownerDocument") == 0) {
        *result = js_dom_owner_document_from_node(node->parent);
        return true;
    }
    if (is_text) {
        JsTextDataOperation operation;
        int arity;
        if (strcmp(prop, "replaceData") == 0) {
            operation = JS_TEXT_DATA_REPLACE; arity = 3;
        } else if (strcmp(prop, "insertData") == 0) {
            operation = JS_TEXT_DATA_INSERT; arity = 2;
        } else if (strcmp(prop, "appendData") == 0) {
            operation = JS_TEXT_DATA_APPEND; arity = 1;
        } else if (strcmp(prop, "deleteData") == 0) {
            operation = JS_TEXT_DATA_DELETE; arity = 2;
        } else if (strcmp(prop, "substringData") == 0) {
            operation = JS_TEXT_DATA_SUBSTRING; arity = 2;
        } else {
            operation = JS_TEXT_DATA_APPEND; arity = 0;
        }
        if (arity > 0) {
            *result = js_bind_function(js_new_native_payload_function(
                js_text_data_body, operation, arity), elem_item, NULL, 0);
            return true;
        }
    }
    return expando_get_property(node, prop_name, result);
}

extern "C" Item js_dom_get_property_impl(Item elem_item, Item prop_name) {
    // Range / Selection wrappers also live under the DOM resource carrier and route here.

    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    const char* prop = fn_to_cstr(prop_name);
    JsDomPropId prop_id = js_dom_prop_id(prop);
    if (!node) {
        log_debug("js_dom_get_property: not a DOM node");
        return ItemNull;
    }

    if (!prop) return ItemNull;

    // F-5: HTMLSelectElement indexed property access (numeric key).
    // `select[i]` returns options[i] or undefined for out-of-range.
    if (node->is_element()) {
        DomElement* sel_elem = node->as_element();
        if (sel_elem && _is_tag(sel_elem, "select")) {
            TypeId kt = get_type_id(prop_name);
            int64_t idx = -1;
            bool numeric = false;
            if (kt == LMD_TYPE_INT) { idx = it2i(prop_name); numeric = true; }
            else if (kt == LMD_TYPE_INT64) { idx = it2l(prop_name); numeric = true; }
            else if (kt == LMD_TYPE_FLOAT) {
                // JS numeric property keys now arrive as boxed Number values;
                // integral floats still denote HTMLSelectElement option indices.
                double d = it2d(prop_name);
                if (isfinite(d) && d >= 0.0 && d == floor(d)) {
                    idx = (int64_t)d;
                    numeric = true;
                }
            }
            else if ((prop[0] >= '0' && prop[0] <= '9')) {
                char* ep = nullptr; long n = strtol(prop, &ep, 10);
                if (ep && *ep == '\0' && n >= 0) { idx = n; numeric = true; }
            }
            if (numeric) {
                Item arr = js_array_new(0);
                _collect_options(sel_elem->first_child, arr);
                if (idx >= 0 && idx < js_array_length(arr)) {
                    return js_elements_get_int(arr, idx);
                }
                return make_js_undefined();
            }
        }
    }

    if (node->is_text()) {
        DomText* text_node = node->as_text();
        Item result = ItemNull;
        if (js_dom_get_textlike_property(node, elem_item, prop_name, prop,
                text_node->text, dom_text_utf16_length(text_node), 3,
                "#text", true, &result)) return result;
        log_debug("js_dom_get_property: unknown text node property '%s'", prop);
        return make_js_undefined();
    }

    if (node->is_comment()) {
        DomComment* comment_node = node->as_comment();
        Item result = ItemNull;
        if (js_dom_get_textlike_property(node, elem_item, prop_name, prop,
                comment_node->content, comment_node->length,
                comment_node->node_type, "#comment", false, &result)) return result;
        log_debug("js_dom_get_property: unknown comment node property '%s'", prop);
        return make_js_undefined();
    }

    // Element properties below — safe to cast
    DomElement* elem = node->as_element();
    if (!elem) {
        log_debug("js_dom_get_property: node is not an element for property '%s'", prop);
        return ItemNull;
    }

    if (_is_tag(elem, "a") || _is_tag(elem, "area")) {
        if (prop_id == JS_DOM_PROP_HASH) {
            Item result = ItemNull;
            if (radiant_dom_anchor_hash_get(elem_item, &result)) return result;
        }
        if (prop_id == JS_DOM_PROP_HREF) {
            Item result = ItemNull;
            if (radiant_dom_m4b_href_get(elem_item, &result)) return result;
        }
    }

    // tagName (uppercased per spec)
    if (prop_id == JS_DOM_PROP_TAG_NAME) {
        return (Item){.item = s2it(uppercase_tag_name(elem->tag_name))};
    }

    // localName (lowercased per spec; tag names are stored lowercase already).
    if (prop_id == JS_DOM_PROP_LOCAL_NAME) {
        const char* tn = elem->tag_name ? elem->tag_name : "";
        return js_name_item(tn);
    }

    // html requires template contents to be exposed through a detached
    // DocumentFragment, so cloning does not clone the hidden template wrapper.
    if (prop_id == JS_DOM_PROP_CONTENT &&
        elem->tag_name && strcasecmp(elem->tag_name, "template") == 0) {
        return js_dom_template_content(elem);
    }

    // namespaceURI. Prefer the URI recorded by createElementNS (kept as a
    // reserved internal attribute); otherwise HTML elements live in the XHTML
    // namespace. The direct createElementNS Document binding records this URI.
    if (prop_id == JS_DOM_PROP_NAMESPACE_URI) {
        const char* ns = elem->get_attribute("__lambda_ns_uri");
        if (ns && ns[0] != '\0') {
            return js_name_item(ns);
        }
        if (js_dom_element_is_svg(elem)) {
            return js_name_item("http://www.w3.org/2000/svg");
        }
        return js_name_item("http://www.w3.org/1999/xhtml");
    }

    if (prop_id == JS_DOM_PROP_OWNER_SVGELEMENT) {
        if (!js_dom_element_is_svg(elem) || strcasecmp(elem->tag_name, "svg") == 0) {
            return ItemNull;
        }
        for (DomNode* current = elem->parent; current; current = current->parent) {
            if (!current->is_element()) continue;
            DomElement* ancestor = current->as_element();
            if (ancestor && ancestor->tag_name && strcasecmp(ancestor->tag_name, "svg") == 0) {
                return js_dom_wrap_element(ancestor);
            }
        }
        return ItemNull;
    }

    // prefix — HTML elements have null prefix.
    if (prop_id == JS_DOM_PROP_PREFIX) {
        return ItemNull;
    }

    // iframe.contentDocument / contentWindow — lazy-create a foreign HTML
    // document that backs the iframe's browsing context. Cached per element
    // so identity comparisons (===) work.
    if (elem->tag_name && strcasecmp(elem->tag_name, "iframe") == 0 &&
        (prop_id == JS_DOM_PROP_CONTENT_DOCUMENT || prop_id == JS_DOM_PROP_CONTENT_WINDOW)) {
        extern Item js_iframe_get_content_document(DomElement* iframe);
        extern Item js_iframe_get_content_window  (DomElement* iframe);
        if (prop_id == JS_DOM_PROP_CONTENT_DOCUMENT)
            return js_iframe_get_content_document(elem);
        return js_iframe_get_content_window(elem);
    }

    // id
    if (prop_id == JS_DOM_PROP_ID) {
        return (Item){.item = elem->id ? s2it(heap_create_name(elem->id))
                                        : s2it(heap_create_name(""))};
    }

    // HTMLElement.slot reflects the content attribute and controls default
    // versus named-slot assignment in the Shadow DOM flattened tree.
    if (prop_id == JS_DOM_PROP_SLOT) {
        const char* slot_name = elem->get_attribute("slot");
        return js_name_item(slot_name ? slot_name : "");
    }

    if (js_dom_element_is_svg(elem) && prop_id == JS_DOM_PROP_TRANSFORM) {
        return js_dom_svg_get_transform_list(elem);
    }

    // SVGAnimatedString is distinct from HTML's string-valued className.
    // Keep baseVal live so SVG libraries changing it update the actual class
    // attribute used by selector matching and rendering.
    if (js_dom_element_is_svg(elem) && prop_id == JS_DOM_PROP_CLASS_NAME) {
        return js_dom_svg_get_animated_class_name(elem);
    }

    // className (space-joined class list)
    if (prop_id == JS_DOM_PROP_CLASS_NAME) {
        return js_classlist_value_item(elem);
    }

    if (prop_id == JS_DOM_PROP_CLASS_LIST) {
        // Nested member chains use the generic property path, so classList
        // must expose the same live owner-bound operations as the MIR fast path.
        return js_dom_get_classlist_wrapper(elem, elem_item);
    }

    // style — live CSSStyleDeclaration-like wrapper for inline style.
    if (prop_id == JS_DOM_PROP_STYLE) {
        return js_dom_get_inline_style_wrapper(elem);
    }

    // textContent / innerText (recursive text extraction)
    if (prop_id == JS_DOM_PROP_TEXT_CONTENT || prop_id == JS_DOM_PROP_INNER_TEXT) {
        StrBuf* sb = strbuf_new_cap(128);
        collect_text_content((DomNode*)elem, sb);
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // innerHTML (recursive HTML serialization of children)
    if (prop_id == JS_DOM_PROP_INNER_HTML) {
        StrBuf* sb = strbuf_new_cap(256);
        DomNode* child = js_dom_first_script_visible_child(elem);
        while (child) {
            collect_inner_html(child, sb);
            child = js_dom_next_script_visible_sibling(child);
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // v12: outerHTML (element itself + children)
    if (prop_id == JS_DOM_PROP_OUTER_HTML) {
        StrBuf* sb = strbuf_new_cap(256);
        collect_inner_html((DomNode*)elem, sb);
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // nodeType
    if (prop_id == JS_DOM_PROP_NODE_TYPE) {
        if (_is_tag(elem, "#document-fragment"))
            return (Item){.item = i2it(11)};
        return (Item){.item = i2it((int64_t)elem->node_type)};
    }

    // childElementCount
    if (prop_id == JS_DOM_PROP_CHILD_ELEMENT_COUNT) {
        int count = 0;
        DomNode* child = js_dom_first_script_visible_child(elem);
        while (child) {
            if (child->is_element()) count++;
            child = js_dom_next_script_visible_sibling(child);
        }
        return (Item){.item = i2it((int64_t)count)};
    }

    // children (array of child DOM elements only)
    if (prop_id == JS_DOM_PROP_CHILDREN) {
        return js_dom_collect_child_nodes(elem, true);
    }

    // parentElement
    if (prop_id == JS_DOM_PROP_PARENT_ELEMENT) {
        return js_dom_parent_element_or_null((DomNode*)elem);
    }

    // parentNode (includes text nodes — returns any parent)
    if (prop_id == JS_DOM_PROP_PARENT_NODE) {
        return js_dom_parent_node_or_null((DomNode*)elem);
    }

    // isConnected — true iff the shadow-inclusive root is the Document.
    if (prop_id == JS_DOM_PROP_IS_CONNECTED) {
        return (Item){.item = b2it(js_dom_node_is_connected((DomNode*)elem) ? 1 : 0)};
    }

    // attributes → a NamedNodeMap-like array of {name, value} entries with
    // .length and indexed access. Not a full NamedNodeMap (getNamedItem etc.
    // are unimplemented), but sufficient for the reconciler's
    // stale-attribute removal loop `for (i=0; i<elem.attributes.length; i++)
    // ... elem.attributes[i].name`. Internal (__lambda_*) attributes are
    // filtered so they don't leak to script.
    if (prop_id == JS_DOM_PROP_ATTRIBUTES) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        Item arr_item = (Item){.array = arr};
        int attr_count = 0;
        const char** attr_names = elem->attribute_names(&attr_count);
        for (int i = 0; attr_names && i < attr_count; i++) {
            const char* name = attr_names[i];
            if (js_dom_is_internal_attr(name)) continue;
            const char* value = elem->get_attribute(name);
            Item pair = js_new_object();
            Item name_item = js_name_item(name);
            Item value_item = js_name_item(value ? value : "");
            // Attr exposes both legacy name/value and Node nodeName/nodeValue;
            // sanitizers iterate the latter aliases from element.attributes.
            js_set_key_cstr(pair, "nodeName", name_item);
            js_set_key_cstr(pair, "nodeValue", value_item);
            js_set_key_cstr(pair, "name", name_item);
            js_set_key_cstr(pair, "value", value_item);
            js_array_push(arr_item, pair);
        }
        return arr_item;
    }

    // ownerDocument — returns the document proxy for any element.
    // For elements owned by a foreign document, return that foreign-doc
    // wrapper (so identity tests like `el.ownerDocument === foreignDoc` hold).
    if (prop_id == JS_DOM_PROP_OWNER_DOCUMENT) {
        DomDocument* od = elem->doc;
        if (od && od != _js_current_document) {
            Item w = lookup_foreign_doc_wrapper(od);
            if (w.item != ITEM_NULL) return w;
        }
        return js_get_document_object_value();
    }

    // firstChild (any node type, not just elements)
    if (prop_id == JS_DOM_PROP_FIRST_CHILD) {
        DomNode* child = js_dom_first_script_visible_child(elem);
        if (!child) return ItemNull;
        if (child->is_element()) return js_dom_wrap_element(child->as_element());
        // wrap text node
        return js_dom_wrap_element((DomElement*)(void*)child);
    }

    // lastChild (any node type)
    if (prop_id == JS_DOM_PROP_LAST_CHILD) {
        DomNode* child = js_dom_last_script_visible_child(elem);
        if (!child) return ItemNull;
        if (child->is_element()) return js_dom_wrap_element(child->as_element());
        return js_dom_wrap_element((DomElement*)(void*)child);
    }

    // nextSibling (any node type)
    if (prop_id == JS_DOM_PROP_NEXT_SIBLING) {
        DomNode* sib = js_dom_next_script_visible_sibling((DomNode*)elem);
        if (!sib) return ItemNull;
        if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
        return js_dom_wrap_element((DomElement*)(void*)sib);
    }

    // previousSibling (any node type)
    if (prop_id == JS_DOM_PROP_PREVIOUS_SIBLING) {
        DomNode* sib = js_dom_prev_script_visible_sibling((DomNode*)elem);
        if (!sib) return ItemNull;
        if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
        return js_dom_wrap_element((DomElement*)(void*)sib);
    }

    // firstElementChild
    if (prop_id == JS_DOM_PROP_FIRST_ELEMENT_CHILD) {
        DomNode* child = js_dom_first_script_visible_child(elem);
        while (child) {
            if (child->is_element()) return js_dom_wrap_element(child->as_element());
            child = js_dom_next_script_visible_sibling(child);
        }
        return ItemNull;
    }

    // lastElementChild
    if (prop_id == JS_DOM_PROP_LAST_ELEMENT_CHILD) {
        DomNode* child = js_dom_last_script_visible_child(elem);
        while (child) {
            if (child->is_element()) return js_dom_wrap_element(child->as_element());
            child = js_dom_prev_script_visible_sibling(child);
        }
        return ItemNull;
    }

    // nextElementSibling
    if (prop_id == JS_DOM_PROP_NEXT_ELEMENT_SIBLING) {
        DomNode* sib = js_dom_next_script_visible_sibling((DomNode*)elem);
        while (sib) {
            if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
            sib = js_dom_next_script_visible_sibling(sib);
        }
        return ItemNull;
    }

    // previousElementSibling
    if (prop_id == JS_DOM_PROP_PREVIOUS_ELEMENT_SIBLING) {
        DomNode* sib = js_dom_prev_script_visible_sibling((DomNode*)elem);
        while (sib) {
            if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
            sib = js_dom_prev_script_visible_sibling(sib);
        }
        return ItemNull;
    }

    // childNodes (all children including text nodes)
    if (prop_id == JS_DOM_PROP_CHILD_NODES) {
        return js_dom_collect_child_nodes(elem, false);
    }

    // children (array of child DOM elements only)
    if (prop_id == JS_DOM_PROP_CHILDREN) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = js_dom_first_script_visible_child(elem);
        while (child) {
            if (child->is_element()) {
                array_push(arr, js_dom_wrap_element(child->as_element()));
            }
            child = js_dom_next_script_visible_sibling(child);
        }
        return (Item){.array = arr};
    }

    // length (for NodeList / HTMLCollection-like results)
    if (prop_id == JS_DOM_PROP_LENGTH) {
        int count = 0;
        DomNode* child = js_dom_first_script_visible_child(elem);
        while (child) {
            if (child->is_element()) count++;
            child = js_dom_next_script_visible_sibling(child);
        }
        return (Item){.item = i2it((int64_t)count)};
    }

    // =========================================================================
    // Layout dimension properties — return values from DomElement fields.
    // After layout_html_doc() these contain real pixel values; before layout
    // they are 0 (which matches current browser behaviour for scripts that
    // run before first paint).
    // =========================================================================

    // offsetWidth / offsetHeight — border box dimensions
    if (prop_id == JS_DOM_PROP_OFFSET_WIDTH) {
        return (Item){.item = i2it(js_dom_geometry_dimension(elem, true))};
    }
    if (prop_id == JS_DOM_PROP_OFFSET_HEIGHT) {
        return (Item){.item = i2it(js_dom_geometry_dimension(elem, false))};
    }

    // clientWidth / clientHeight — border box minus borders
    if (prop_id == JS_DOM_PROP_CLIENT_WIDTH) {
        js_dom_ensure_geometry_snapshot(elem->doc);
        float bw = 0;
        if (elem->bound && elem->boundary()->border) {
            bw = elem->boundary()->border->width.left + elem->boundary()->border->width.right;
        }
        return (Item){.item = i2it((int64_t)(elem->width - bw))};
    }
    if (prop_id == JS_DOM_PROP_CLIENT_HEIGHT) {
        js_dom_ensure_geometry_snapshot(elem->doc);
        float bh = 0;
        if (elem->bound && elem->boundary()->border) {
            bh = elem->boundary()->border->width.top + elem->boundary()->border->width.bottom;
        }
        return (Item){.item = i2it((int64_t)(elem->height - bh))};
    }

    // offsetTop / offsetLeft — position relative to offsetParent
    if (prop_id == JS_DOM_PROP_OFFSET_TOP) {
        js_dom_ensure_geometry_snapshot(elem->doc);
        if (_is_tag(elem, "body") || _is_tag(elem, "html"))
            return (Item){.item = i2it(0)};
        if (elem->doc && js_dom_has_committed_geometry_snapshot(elem->doc))
            return (Item){.item = i2it(js_dom_offset_coordinate(elem, false))};
        return (Item){.item = i2it((int64_t)elem->y)};
    }
    if (prop_id == JS_DOM_PROP_OFFSET_LEFT) {
        js_dom_ensure_geometry_snapshot(elem->doc);
        if (_is_tag(elem, "body") || _is_tag(elem, "html"))
            return (Item){.item = i2it(0)};
        if (elem->doc && js_dom_has_committed_geometry_snapshot(elem->doc))
            return (Item){.item = i2it(js_dom_offset_coordinate(elem, true))};
        if (elem->x == 0.0f) {
            int64_t synthetic_left = js_dom_synthetic_inline_offset_left(elem);
            if (synthetic_left > 0) {
                return (Item){.item = i2it(synthetic_left)};
            }
        }
        return (Item){.item = i2it((int64_t)elem->x)};
    }

    // offsetParent — nearest positioned ancestor (or body)
    if (prop_id == JS_DOM_PROP_OFFSET_PARENT) {
        js_dom_ensure_geometry_snapshot(elem->doc);
        DomElement* parent = js_dom_offset_parent_element(elem);
        return parent ? js_dom_wrap_element(parent) : ItemNull;
    }

    // scrollWidth / scrollHeight — total scrollable content size
    if (prop_id == JS_DOM_PROP_SCROLL_WIDTH) {
        js_dom_ensure_geometry_snapshot(elem->doc);
        float cw = elem->content_width;
        float bw = elem->width;
        return (Item){.item = i2it((int64_t)(cw > bw ? cw : bw))};
    }
    if (prop_id == JS_DOM_PROP_SCROLL_HEIGHT) {
        js_dom_ensure_geometry_snapshot(elem->doc);
        float ch = elem->content_height;
        float bh = elem->height;
        return (Item){.item = i2it((int64_t)(ch > bh ? ch : bh))};
    }

    // scrollTop / scrollLeft — current scroll position
    if (prop_id == JS_DOM_PROP_SCROLL_TOP) {
        if (elem->scroller && elem->scroll()->pane) {
            return (Item){.item = i2it((int64_t)elem->scroll()->pane->v_scroll_position)};
        }
        if (elem->has_pending_element_scroll_y()) {
            return (Item){.item = i2it((int64_t)elem->pending_scroll_y())};
        }
        return (Item){.item = i2it(0)};
    }
    if (prop_id == JS_DOM_PROP_SCROLL_LEFT) {
        if (elem->scroller && elem->scroll()->pane) {
            return (Item){.item = i2it((int64_t)elem->scroll()->pane->h_scroll_position)};
        }
        if (elem->has_pending_element_scroll_x()) {
            return (Item){.item = i2it((int64_t)elem->pending_scroll_x())};
        }
        return (Item){.item = i2it(0)};
    }

    // data (text node content) — check if the wrapped node is actually a DomText
    if (prop_id == JS_DOM_PROP_DATA) {
        DomNode* node = (DomNode*)elem;  // may be DomText wrapped as DomElement*
        if (node->is_text()) {
            DomText* text_node = node->as_text();
            if (text_node->text && text_node->length > 0) {
                String* s = heap_strcpy((char*)text_node->text, text_node->length);
                return (Item){.item = s2it(s)};
            }
            return js_name_item("");
        }
        return ItemNull;
    }

    // nodeName — tag name for elements, "#text" for text nodes
    if (prop_id == JS_DOM_PROP_NODE_NAME) {
        DomNode* node = (DomNode*)elem;
        if (node->is_text()) {
            return js_name_item("#text");
        }
        return (Item){.item = s2it(uppercase_tag_name(elem->tag_name))};
    }

    // HTMLStyleElement.sheet — associated CSSStyleSheet (doesn't require native_element)
    if (prop_id == JS_DOM_PROP_SHEET && elem->tag_name && strcasecmp(elem->tag_name, "style") == 0) {
        return js_cssom_get_style_element_sheet(elem_item);
    }

    // ------------------------------------------------------------------
    // F-5: HTMLSelectElement / HTMLOptionElement IDL properties
    // ------------------------------------------------------------------
    if (_is_tag(elem, "select")) {
        if (prop_id == JS_DOM_PROP_OPTIONS) {
            Item arr = js_array_new(0);
            _decorate_options_collection(arr);
            _register_select_options_owner(arr, elem, SELECT_COLLECTION_OPTIONS);
            _select_refresh_options_collection(arr, elem);
            return arr;
        }
        if (prop_id == JS_DOM_PROP_LENGTH) {
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            return (Item){.item = i2it(js_array_length(arr))};
        }
        if (prop_id == JS_DOM_PROP_SELECTED_OPTIONS) {
            Item exp = expando_get_or_create_map((DomNode*)elem);
            Item cache_key = js_name_item("__selectedOptions");
            Item out = (exp.item != ITEM_NULL) ? js_get_key_default(exp, cache_key) : ItemNull;
            if (get_type_id(out) != LMD_TYPE_ARRAY) {
                out = js_array_new(0);
                _decorate_dom_collection(out, "HTMLCollection");
                if (exp.item != ITEM_NULL) js_set_key_default(exp, cache_key, out);
            }
            _register_select_options_owner(out, elem, SELECT_COLLECTION_SELECTED_OPTIONS);
            _select_refresh_selected_options_collection(out, elem);
            return out;
        }
        if (prop_id == JS_DOM_PROP_SELECTED_INDEX) {
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            int64_t n = js_array_length(arr);
            int first_non_disabled = -1;
            for (int64_t i = 0; i < n; i++) {
                DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
                if (!opt) continue;
                if (_get_selectedness(opt)) return (Item){.item = i2it(i)};
                if (first_non_disabled < 0 && !opt->has_attribute("disabled"))
                    first_non_disabled = (int)i; // INT_CAST_OK: option index
            }
            // Default-selected behavior: non-multiple, size<=1 select with
            // no explicit selectedness picks the first non-disabled option.
            int size = 0;
            const char* sz = elem->get_attribute("size");
            if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
            if (!elem->has_attribute("multiple") && size <= 1
                && first_non_disabled >= 0 && !_select_is_dirty(elem)) {
                return (Item){.item = i2it(first_non_disabled)};
            }
            return (Item){.item = i2it(-1)};
        }
        if (prop_id == JS_DOM_PROP_VALUE) {
            char* value = _select_value(elem);
            String* result = heap_create_name(value ? value : "");
            if (value) mem_free(value);
            return (Item){.item = s2it(result)};
        }
        if (prop_id == JS_DOM_PROP_TYPE) {
            const char* t = elem->has_attribute("multiple")
                ? "select-multiple" : "select-one";
            return js_name_item(t);
        }
    }

    // HTMLOptionElement properties.
    if (_is_tag(elem, "option")) {
        if (prop_id == JS_DOM_PROP_VALUE) {
            char* v = _option_value(elem);
            String* s = heap_create_name(v ? v : "");
            mem_free(v);
            return (Item){.item = s2it(s)};
        }
        if (prop_id == JS_DOM_PROP_TEXT || prop_id == JS_DOM_PROP_LABEL) {
            // label IDL: returns label attribute, falling back to text.
            if (prop_id == JS_DOM_PROP_LABEL) {
                const char* lab = elem->get_attribute("label");
                if (lab && *lab) return js_name_item(lab);
            }
            char* t = _option_text(elem);
            String* s = heap_create_name(t ? t : "");
            mem_free(t);
            return (Item){.item = s2it(s)};
        }
        if (prop_id == JS_DOM_PROP_SELECTED) {
            if (_get_selectedness(elem)) return (Item){.item = b2it(true)};
            // Apply default-reset rule: in a non-multiple, size<=1 select
            // with no option having selectedness, the first non-disabled
            // direct-child option counts as selected.
            DomElement* sel = _option_owner_select(elem);
            if (!sel || _select_is_dirty(sel)) return (Item){.item = b2it(false)};
            if (sel->has_attribute("multiple")) return (Item){.item = b2it(false)};
            int size = 0;
            const char* sz = sel->get_attribute("size");
            if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
            if (size > 1) return (Item){.item = b2it(false)};
            // Walk options of sel; check none has selectedness; find first
            // non-disabled.
            Item arr = js_array_new(0);
            _collect_options(sel->first_child, arr);
            int64_t n = js_array_length(arr);
            int first_nd = -1;
            for (int64_t i = 0; i < n; i++) {
                DomElement* o = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, i));
                if (!o) continue;
                if (_get_selectedness(o)) return (Item){.item = b2it(false)};
                if (first_nd < 0 && !o->has_attribute("disabled"))
                    first_nd = (int)i; // INT_CAST_OK: option index
            }
            if (first_nd < 0) return (Item){.item = b2it(false)};
            DomElement* first = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, first_nd));
            return (Item){.item = b2it(first == elem)};
        }
        if (prop_id == JS_DOM_PROP_INDEX) {
            return (Item){.item = i2it(_option_index_in_select(elem))};
        }
        if (prop_id == JS_DOM_PROP_FORM) {
            DomElement* sel = _option_owner_select(elem);
            if (sel) {
                // Walk up to find the form
                for (DomNode* p = sel->parent; p; p = p->parent) {
                    if (p->is_element()) {
                        DomElement* pe = (DomElement*)p;
                        if (pe->tag_name && strcasecmp(pe->tag_name, "form") == 0)
                            return js_dom_wrap_element(pe);
                    }
                }
            }
            return ItemNull;
        }
    }

    // ------------------------------------------------------------------
    // Boolean IDL attributes that must be returned as real booleans
    // (HTML form-control properties used by activation behavior).
    // ------------------------------------------------------------------
    if (prop_id == JS_DOM_PROP_CHECKED && _is_tag(elem, "input")) {
        return (Item){.item = b2it(_get_checkedness(elem))};
    }
    if (prop_id == JS_DOM_PROP_DISABLED &&
        (_is_tag(elem, "input") || _is_tag(elem, "button") ||
         _is_tag(elem, "select") || _is_tag(elem, "textarea") ||
         _is_tag(elem, "fieldset") || _is_tag(elem, "optgroup") ||
         _is_tag(elem, "option"))) {
        return (Item){.item = b2it(elem->has_attribute("disabled"))};
    }
    if (prop_id == JS_DOM_PROP_VALUE && _is_tag(elem, "input") && !tc_is_text_control_elem(elem)) {
        const char* v = elem->get_attribute("value");
        return js_name_item(v ? v : "");
    }

    // ------------------------------------------------------------------
    // HTML form text-control (HTMLInputElement / HTMLTextAreaElement)
    // properties — must intercept BEFORE attribute fallback so .value
    // returns the live IDL value, not the static `value` attribute.
    // ------------------------------------------------------------------
    if (tc_is_text_control_elem(elem)) {
        if (prop_id == JS_DOM_PROP_VALUE) {
            tc_ensure_init(elem);
            FormControlProp* f = elem->form;
            String* s = heap_strcpy(f->current_value ? f->current_value : (char*)"",
                                    (int64_t)f->current_value_len);
            return (Item){.item = s2it(s)};
        }
        if (prop_id == JS_DOM_PROP_DEFAULT_VALUE) {
            // <input>: getAttribute("value"); <textarea>: text content of children.
            if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
                StrBuf* sb = strbuf_new_cap(64);
                collect_text_content((DomNode*)elem, sb);
                String* s = heap_create_name(sb->str ? sb->str : "");
                strbuf_free(sb);
                return (Item){.item = s2it(s)};
            }
            const char* v = elem->get_attribute("value");
            return js_name_item(v ? v : "");
        }
        if (prop_id == JS_DOM_PROP_SELECTION_START) {
            tc_ensure_init(elem);
            uint32_t start = 0;
            DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
            form_control_get_selection(state, (View*)elem, &start, NULL, NULL);
            return (Item){.item = i2it((int64_t)start)};
        }
        if (prop_id == JS_DOM_PROP_SELECTION_END) {
            tc_ensure_init(elem);
            uint32_t end = 0;
            DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
            form_control_get_selection(state, (View*)elem, NULL, &end, NULL);
            return (Item){.item = i2it((int64_t)end)};
        }
        if (prop_id == JS_DOM_PROP_SELECTION_DIRECTION) {
            tc_ensure_init(elem);
            const char* d = "none";
            uint8_t direction = 0;
            DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
            form_control_get_selection(state, (View*)elem, NULL, NULL, &direction);
            if (direction == 1) d = "forward";
            else if (direction == 2) d = "backward";
            return js_name_item(d);
        }
        if (prop_id == JS_DOM_PROP_TEXT_LENGTH) {
            tc_ensure_init(elem);
            return (Item){.item = i2it((int64_t)elem->form->current_value_u16_len)};
        }
        if (prop_id == JS_DOM_PROP_SET_SELECTION_RANGE)
            return js_new_native_function(js_text_control_set_selection_range);
        if (prop_id == JS_DOM_PROP_SELECT)
            return js_new_native_function(js_text_control_select);
        if (prop_id == JS_DOM_PROP_SET_RANGE_TEXT)
            return js_new_native_function(js_text_control_set_range_text);
    }

    // ------------------------------------------------------------------
    // F-0: IDL attribute reflection for form elements
    // ------------------------------------------------------------------

    // F-1: HTMLFormElement.elements — snapshot array of listed form controls
    if (_is_tag(elem, "form") && prop_id == JS_DOM_PROP_ELEMENTS) {
        return js_dom_live_form_elements_bridge((void*)elem);
    }
    // F-1: HTMLFormElement.length → number of listed controls
    if (_is_tag(elem, "form") && prop_id == JS_DOM_PROP_LENGTH) {
        Item arr = js_array_new(0);
        _collect_form_controls_rec(elem->first_child, arr);
        return (Item){.item = i2it(js_array_length(arr))};
    }

    // Helper: read a non-negative integer attr; return default_val if absent/invalid
    auto _reflect_int_attr = [&](const char* attr_name, int default_val) -> int {
        const char* v = elem->get_attribute(attr_name);
        if (!v) return default_val;
        char* end = nullptr;
        long n = strtol(v, &end, 10);
        return (end != v && n >= 0) ? (int)n : default_val; // INT_CAST_OK: attribute integer value
    };

    // Boolean reflection: required, multiple, readOnly/readonly, noValidate
    if (prop_id == JS_DOM_PROP_REQUIRED &&
        (_is_tag(elem, "input") || _is_tag(elem, "select") || _is_tag(elem, "textarea"))) {
        return (Item){.item = b2it(elem->has_attribute("required"))};
    }
    if (prop_id == JS_DOM_PROP_MULTIPLE &&
        (_is_tag(elem, "input") || _is_tag(elem, "select"))) {
        return (Item){.item = b2it(elem->has_attribute("multiple"))};
    }
    if ((prop_id == JS_DOM_PROP_READ_ONLY || prop_id == JS_DOM_PROP_READONLY) &&
        (_is_tag(elem, "input") || _is_tag(elem, "textarea"))) {
        return (Item){.item = b2it(elem->has_attribute("readonly"))};
    }
    if (prop_id == JS_DOM_PROP_NO_VALIDATE && _is_tag(elem, "form")) {
        return (Item){.item = b2it(elem->has_attribute("novalidate"))};
    }
    if (prop_id == JS_DOM_PROP_OPEN && _is_tag(elem, "details")) {
        return (Item){.item = b2it(elem->has_attribute("open"))};
    }
    // name attribute (all listed form controls and form/fieldset)
    if (prop_id == JS_DOM_PROP_NAME &&
        (_is_tag(elem, "input") || _is_tag(elem, "button") || _is_tag(elem, "select") ||
         _is_tag(elem, "textarea") || _is_tag(elem, "form") || _is_tag(elem, "fieldset") ||
         _is_tag(elem, "output") || _is_tag(elem, "object"))) {
        const char* v = elem->get_attribute("name");
        return js_name_item(v ? v : "");
    }
    // type for button (default "submit"; only valid values: "submit","reset","button")
    if (prop_id == JS_DOM_PROP_TYPE && _is_tag(elem, "button")) {
        const char* v = elem->get_attribute("type");
        if (v && (strcasecmp(v, "submit") == 0 || strcasecmp(v, "reset") == 0 || strcasecmp(v, "button") == 0)) {
            char buf[8];
            for (int i = 0; v[i] && i < 7; i++) buf[i] = (char)tolower((unsigned char)v[i]), buf[i+1] = '\0';
            return js_name_item(buf);
        }
        return js_name_item("submit");
    }
    // placeholder (input, textarea)
    if (prop_id == JS_DOM_PROP_PLACEHOLDER &&
        (_is_tag(elem, "input") || _is_tag(elem, "textarea"))) {
        const char* v = elem->get_attribute("placeholder");
        return js_name_item(v ? v : "");
    }
    // autocomplete (form, input, select, textarea)
    if (prop_id == JS_DOM_PROP_AUTOCOMPLETE &&
        (_is_tag(elem, "form") || _is_tag(elem, "input") || _is_tag(elem, "select") || _is_tag(elem, "textarea"))) {
        const char* v = elem->get_attribute("autocomplete");
        return js_name_item(v ? v : "");
    }
    // pattern, min, max, step, accept (input only — simple string reflection)
    if (_is_tag(elem, "input") &&
        (prop_id == JS_DOM_PROP_PATTERN || prop_id == JS_DOM_PROP_MIN || prop_id == JS_DOM_PROP_MAX ||
         prop_id == JS_DOM_PROP_STEP || prop_id == JS_DOM_PROP_ACCEPT)) {
        const char* v = elem->get_attribute(prop);
        return js_name_item(v ? v : "");
    }
    // HTMLFormElement: action, method, enctype/encoding, acceptCharset, target
    if (_is_tag(elem, "form")) {
        if (prop_id == JS_DOM_PROP_ACTION) {
            const char* v = elem->get_attribute("action");
            if (v && *v) return js_name_item(v);
            // Empty/missing action falls back to document URL per HTML spec.
            DomDocument* doc = elem->doc;
            const char* doc_url = "";
            if (doc && doc->url) {
                const char* href = url_get_href(doc->url);
                if (href) doc_url = href;
            }
            return js_name_item(doc_url);
        }
        if (prop_id == JS_DOM_PROP_TARGET) {
            const char* v = elem->get_attribute(prop);
            return js_name_item(v ? v : "");
        }
        if (prop_id == JS_DOM_PROP_METHOD) {
            const char* v = elem->get_attribute("method");
            return js_name_item(_normalise_method(v));
        }
        if (prop_id == JS_DOM_PROP_ENCTYPE || prop_id == JS_DOM_PROP_ENCODING) {
            const char* v = elem->get_attribute("enctype");
            return js_name_item(_normalise_enctype(v));
        }
        if (prop_id == JS_DOM_PROP_ACCEPT_CHARSET) {
            const char* v = elem->get_attribute("accept-charset");
            return js_name_item(v ? v : "");
        }
    }
    // HTMLTextAreaElement: wrap (default "soft"), rows (default 2), cols (default 20)
    if (_is_tag(elem, "textarea")) {
        if (prop_id == JS_DOM_PROP_WRAP) {
            const char* v = elem->get_attribute("wrap");
            return js_name_item(v ? v : "soft");
        }
        if (prop_id == JS_DOM_PROP_ROWS) {
            return (Item){.item = i2it(_reflect_int_attr("rows", 2))};
        }
        if (prop_id == JS_DOM_PROP_COLS) {
            return (Item){.item = i2it(_reflect_int_attr("cols", 20))};
        }
        if (prop_id == JS_DOM_PROP_MAX_LENGTH) {
            const char* v = elem->get_attribute("maxlength");
            if (!v) return (Item){.item = i2it(-1)};
            char* end = nullptr; long n = strtol(v, &end, 10);
            return (Item){.item = i2it((end != v && n >= 0) ? n : -1)};
        }
        if (prop_id == JS_DOM_PROP_MIN_LENGTH) {
            return (Item){.item = i2it(_reflect_int_attr("minlength", 0))};
        }
    }
    // HTMLInputElement: maxLength (default -1), minLength (default 0), size (default 20)
    if (_is_tag(elem, "input")) {
        if (prop_id == JS_DOM_PROP_WIDTH) {
            return (Item){.item = i2it(_reflect_int_attr("width", 0))};
        }
        if (prop_id == JS_DOM_PROP_HEIGHT) {
            return (Item){.item = i2it(_reflect_int_attr("height", 0))};
        }
        if (prop_id == JS_DOM_PROP_MAX_LENGTH) {
            const char* v = elem->get_attribute("maxlength");
            if (!v) return (Item){.item = i2it(-1)};
            char* end = nullptr; long n = strtol(v, &end, 10);
            return (Item){.item = i2it((end != v && n >= 0) ? n : -1)};
        }
        if (prop_id == JS_DOM_PROP_MIN_LENGTH) {
            return (Item){.item = i2it(_reflect_int_attr("minlength", 0))};
        }
        if (prop_id == JS_DOM_PROP_SIZE) {
            return (Item){.item = i2it(_reflect_int_attr("size", 20))};
        }
    }
    // HTMLSelectElement: size (default 0 unless multiple, but 0 is spec default)
    if (_is_tag(elem, "select") && prop_id == JS_DOM_PROP_SIZE) {
        return (Item){.item = i2it(_reflect_int_attr("size", 0))};
    }
    const char* string_attr = nullptr;
    if (_is_string_reflected(elem, prop, &string_attr)) {
        const char* v = elem->get_attribute(string_attr);
        return js_name_item(v ? v : "");
    }

    // HTMLInputElement.defaultChecked — reflects `checked` content attribute
    if (prop_id == JS_DOM_PROP_DEFAULT_CHECKED && _is_tag(elem, "input")) {
        return (Item){.item = b2it(elem->has_attribute("checked"))};
    }
    // HTMLOptionElement.defaultSelected — reflects `selected` content attribute
    if (prop_id == JS_DOM_PROP_DEFAULT_SELECTED && _is_tag(elem, "option")) {
        return (Item){.item = b2it(elem->has_attribute("selected"))};
    }
    // HTMLLabelElement.htmlFor / HTMLOutputElement.htmlFor — reflects `for`
    if (prop_id == JS_DOM_PROP_HTML_FOR &&
        (_is_tag(elem, "label") || _is_tag(elem, "output"))) {
        const char* v = elem->get_attribute("for");
        return js_name_item(v ? v : "");
    }
    // HTMLOutputElement.defaultValue — descendant text content if no override
    // has been set. We do not yet track an explicit override (defaultValue
    // setter), so this always returns current descendant text content. That
    // matches WPT reset-form-html behavior where defaultValue tracks textContent.
    if (prop_id == JS_DOM_PROP_DEFAULT_VALUE && _is_tag(elem, "output")) {
        StrBuf* sb = strbuf_new_cap(32);
        collect_text_content((DomNode*)elem, sb);
        String* s = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(s)};
    }
    // HTMLOutputElement.value — descendant text content (getter); we do not
    // track an explicit value override yet.
    if (prop_id == JS_DOM_PROP_VALUE && _is_tag(elem, "output")) {
        StrBuf* sb = strbuf_new_cap(32);
        collect_text_content((DomNode*)elem, sb);
        String* s = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(s)};
    }
    // tabIndex — reflects `tabindex` as integer, otherwise returns the HTML
    // default for elements that are naturally focusable.
    if (prop_id == JS_DOM_PROP_TAB_INDEX) {
        long parsed = 0;
        if (js_dom_has_valid_int_attr(elem, "tabindex", &parsed))
            return (Item){.item = i2it(parsed)};
        return (Item){.item = i2it(js_dom_default_tab_index(elem))};
    }
    // CE-4 (Radiant_Design_Content_Editable.md §7): inputMode/enterKeyHint
    // are enumerated reflected attributes. The IDL getter canonicalises the
    // value (lowercase, one of the listed keywords) and returns "" for
    // missing/unknown — matches HTML spec "reflect ... limited to known
    // values" semantics. These are hints to the IME / on-screen keyboard;
    // the focus-time forwarding stub in update_focus_state() reads them.
    if (prop_id == JS_DOM_PROP_INPUT_MODE) {
        const char* v = elem->get_attribute("inputmode");
        if (!v) return js_name_item("");
        // Canonicalise to lowercase and validate against the spec keyword set.
        char buf[16]; size_t i = 0;
        for (; v[i] && i < sizeof(buf) - 1; i++)
            buf[i] = (char)tolower((unsigned char)v[i]);
        buf[i] = '\0';
        const char* keywords[] = {
            "none", "text", "decimal", "numeric",
            "tel", "search", "email", "url", nullptr
        };
        const char* out = "";
        for (int k = 0; keywords[k]; k++) {
            if (strcmp(buf, keywords[k]) == 0) { out = keywords[k]; break; }
        }
        return js_name_item(out);
    }
    if (prop_id == JS_DOM_PROP_ENTER_KEY_HINT) {
        const char* v = elem->get_attribute("enterkeyhint");
        if (!v) return js_name_item("");
        char buf[16]; size_t i = 0;
        for (; v[i] && i < sizeof(buf) - 1; i++)
            buf[i] = (char)tolower((unsigned char)v[i]);
        buf[i] = '\0';
        const char* keywords[] = {
            "enter", "done", "go", "next", "previous", "search", "send", nullptr
        };
        const char* out = "";
        for (int k = 0; keywords[k]; k++) {
            if (strcmp(buf, keywords[k]) == 0) { out = keywords[k]; break; }
        }
        return js_name_item(out);
    }
    // CE-1 / CE-4 (Radiant_Design_Content_Editable.md §4.2 + §10):
    // contentEditable returns "true"/"false"/"plaintext-only"/"inherit".
    // isContentEditable is the computed property — walks ancestors honouring
    // inheritance and ="false" islands.
    if (prop_id == JS_DOM_PROP_CONTENT_EDITABLE) {
        if (!elem->has_attribute("contenteditable")) {
            return js_name_item("inherit");
        }
        const char* out = js_dom_normalize_contenteditable(
            elem->get_attribute("contenteditable"));
        return js_name_item(out ? out : "inherit");
    }
    if (prop_id == JS_DOM_PROP_IS_CONTENT_EDITABLE) {
        // Walk ancestors. Editable iff the nearest ce-bearing ancestor has
        // value true|""|plaintext-only AND we are not inside a ce="false"
        // subtree below that host.
        bool saw_false = false;
        DomNode* p = (DomNode*)elem;
        while (p) {
            if (p->node_type == DOM_NODE_ELEMENT) {
                DomElement* e = (DomElement*)p;
                if (e->has_attribute("contenteditable")) {
                    const char* v = e->get_attribute("contenteditable");
                    if (!v || *v == '\0' || strcasecmp(v, "true") == 0 ||
                        strcasecmp(v, "plaintext-only") == 0) {
                        return (Item){.item = b2it(!saw_false)};
                    }
                    if (strcasecmp(v, "false") == 0) {
                        saw_false = true;
                    }
                }
            }
            p = p->parent;
        }
        return (Item){.item = ITEM_FALSE};
    }
    if (prop_id == JS_DOM_PROP_AUTOCAPITALIZE) {
        return js_name_item(js_dom_get_autocapitalize(elem));
    }
    if (prop_id == JS_DOM_PROP_AUTOCORRECT) {
        return (Item){.item = b2it(js_dom_get_autocorrect(elem))};
    }
    if (prop_id == JS_DOM_PROP_SPELLCHECK) {
        return (Item){.item = b2it(js_dom_get_spellcheck(elem))};
    }
    if (prop_id == JS_DOM_PROP_WRITING_SUGGESTIONS) {
        return js_name_item(js_dom_get_writing_suggestions(elem));
    }
    if (prop_id == JS_DOM_PROP_DATASET) {
        return js_dom_dataset_property(elem_item);
    }
    // autofocus boolean reflection (HTML global attribute).
    if (prop_id == JS_DOM_PROP_AUTOFOCUS) {
        return (Item){.item = b2it(elem->has_attribute("autofocus"))};
    }

    // formAction / formMethod / formEnctype / formTarget / formNoValidate
    // (HTMLButtonElement and HTMLInputElement). Per spec, `formAction` getter
    // returns the document's URL when the attribute is missing or empty.
    if (_is_tag(elem, "input") || _is_tag(elem, "button")) {
        if (prop_id == JS_DOM_PROP_FORM_ACTION) {
            const char* v = elem->get_attribute("formaction");
            if (v && *v) return js_name_item(v);
            // fall back to document URL
            DomDocument* doc = elem->doc;
            const char* doc_url = "";
            if (doc && doc->url) {
                const char* href = url_get_href(doc->url);
                if (href) doc_url = href;
            }
            return js_name_item(doc_url);
        }
        if (prop_id == JS_DOM_PROP_FORM_METHOD) {
            const char* v = elem->get_attribute("formmethod");
            return js_name_item(_normalise_method(v));
        }
        if (prop_id == JS_DOM_PROP_FORM_ENCTYPE) {
            const char* v = elem->get_attribute("formenctype");
            return js_name_item(_normalise_enctype(v));
        }
        if (prop_id == JS_DOM_PROP_FORM_TARGET) {
            const char* v = elem->get_attribute("formtarget");
            return js_name_item(v ? v : "");
        }
        if (prop_id == JS_DOM_PROP_FORM_NO_VALIDATE) {
            return (Item){.item = b2it(elem->has_attribute("formnovalidate"))};
        }
    }

    // ------------------------------------------------------------------
    // F-4: Constraint Validation API property getters
    // ------------------------------------------------------------------
    // willValidate: true if element is a candidate for constraint validation
    if (prop_id == JS_DOM_PROP_WILL_VALIDATE) {
        bool is_form_ctrl = elem->tag_name && (
            strcasecmp(elem->tag_name, "input") == 0 ||
            strcasecmp(elem->tag_name, "select") == 0 ||
            strcasecmp(elem->tag_name, "textarea") == 0 ||
            strcasecmp(elem->tag_name, "button") == 0);
        if (!is_form_ctrl) return (Item){.item = ITEM_FALSE};
        return (Item){.item = b2it(!_elem_is_barred(elem))};
    }
    // validity: returns a ValidityState object
    if (prop_id == JS_DOM_PROP_VALIDITY) {
        return _build_validity_state(elem);
    }
    // validationMessage: custom validity message or empty string
    if (prop_id == JS_DOM_PROP_VALIDATION_MESSAGE) {
        // Barred elements (disabled, readonly, etc.) always have empty validationMessage
        bool is_form_ctrl = elem->tag_name && (
            strcasecmp(elem->tag_name, "input") == 0 ||
            strcasecmp(elem->tag_name, "select") == 0 ||
            strcasecmp(elem->tag_name, "textarea") == 0 ||
            strcasecmp(elem->tag_name, "button") == 0);
        if (!is_form_ctrl || _elem_is_barred(elem)) {
            return js_name_item("");
        }
        if (elem->form && elem->form->custom_validity_msg &&
            elem->form->custom_validity_msg[0] != '\0') {
            return js_name_item(elem->form->custom_validity_msg);
        }
        return js_name_item("");
    }

    // ------------------------------------------------------------------
    // F-1: HTMLFormElement named getter — `form["name"]` returns the
    // listed control whose name or id matches. If multiple controls match,
    // returns a snapshot array (RadioNodeList placeholder). If none match,
    // falls through to expando / attribute lookup.
    // ------------------------------------------------------------------
    if (_is_tag(elem, "form") && prop && *prop) {
        // Skip standard IDL props (already handled above) and known DOM methods
        // to avoid shadowing them.
        if (!js_dom_form_named_getter_reserved_name(prop)) {
            Item matches = js_array_new(0);
            FormNamedGetterCtx named_ctx = { prop, matches };
            dom_walk_elements(elem->first_child, _form_named_getter_visit, &named_ctx);
            int64_t mlen = js_array_length(matches);
            if (mlen == 1) {
                // single match — return the element itself
                Array* a = matches.array;
                return a->items[0];
            }
            if (mlen > 1) {
                // multiple matches — return as RadioNodeList-ish array
                return matches;
            }
        }
    }

    // Dynamic event attributes compiled through setAttribute("onclick", ...)
    // live in the expando table. Prefer the compiled handler over the raw
    // attribute text so EventTarget dispatch can invoke it.
    char event_prop_name[64];
    if (js_dom_event_attr_name(prop, event_prop_name, sizeof(event_prop_name))) {
        Item exp_map = expando_get_map((DomNode*)elem);
        if (exp_map.item != ITEM_NULL) {
            Item val = js_get_name_key(exp_map, event_prop_name);
            if (val.item != ITEM_NULL && !is_js_undefined(val)) {
                return val;
            }
        }
    }

    // fall back to native element attribute access
    if (!elem->is_synthetic()) {
        const char* attr_val = elem->get_attribute(prop);
        if (attr_val) {
            return js_name_item(attr_val);
        }
    }

    // check expando properties (arbitrary JS values stored on this DOM node)
    {
        Item exp_map = expando_get_map((DomNode*)elem);
        if (exp_map.item != ITEM_NULL) {
            Item key = js_name_item(prop);
            if (expando_map_has_key(exp_map, key)) {
                return js_get_key_default(exp_map, key);
            }
        }
    }

    if (prop_id == JS_DOM_PROP___PROTO__) return js_get_prototype(elem_item);
    bool proto_found = false;
    Item proto_value = js_prototype_lookup_ex(elem_item, prop_name, &proto_found);
    if (proto_found) return proto_value;

    log_debug("js_dom_get_property: unknown property '%s' on <%s>",
              prop, elem->tag_name ? elem->tag_name : "?");
    return make_js_undefined();
}

// ============================================================================
// Element Property Set
// ============================================================================

// Helper: convert camelCase JS property name to CSS hyphenated form
// e.g., "fontFamily" → "font-family", "borderWidth" → "border-width"
// "cssFloat" → "float", "display" → "display"
static void js_camel_to_css_prop(const char* js_prop, char* css_buf, size_t buf_size) {
    // special cases
    if (strcmp(js_prop, "cssFloat") == 0) {
        snprintf(css_buf, buf_size, "float");
        return;
    }
    if (strcmp(js_prop, "cssText") == 0) {
        snprintf(css_buf, buf_size, "cssText");
        return;
    }

    size_t out = 0;
    for (size_t i = 0; js_prop[i] && out < buf_size - 2; i++) {
        char c = js_prop[i];
        if (c >= 'A' && c <= 'Z') {
            css_buf[out++] = '-';
            css_buf[out++] = (char)(c + 32);  // to lowercase
        } else {
            css_buf[out++] = c;
        }
    }
    css_buf[out] = '\0';
}

static bool js_inline_style_cssom_property_exposed(const char* css_prop) {
    if (!css_prop) return false;
    // object-view-box is parsed for stylesheet layout tests, but the browser
    // reference CSSOM does not expose dynamic inline writes for this draft
    // property; treating it as writable changes pre-screenshot WPT geometry.
    if (strcasecmp(css_prop, "object-view-box") == 0) return false;
    return true;
}

extern "C" Item js_dom_set_property_impl(Item elem_item, Item prop_name, Item value) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node) {
        log_debug("js_dom_set_property: not a DOM node");
        return ItemNull;
    }

    const char* prop = fn_to_cstr(prop_name);
    JsDomPropId prop_id = js_dom_prop_id(prop);
    if (!prop) return ItemNull;

    // text node CharacterData aliases
    if (node->is_text() &&
        (prop_id == JS_DOM_PROP_DATA ||
         prop_id == JS_DOM_PROP_NODE_VALUE ||
         prop_id == JS_DOM_PROP_TEXT_CONTENT)) {
        DomText* text_node = node->as_text();
        // CharacterData setters accept DOMString, so numeric/boolean values
        // must be converted instead of being mistaken for an absent string.
        const char* new_text = js_dom_to_dom_string_cstr(value);
        if (new_text) {
            uint32_t old_u16_len = dom_text_utf16_length(text_node);
            JS_ASSIGN_OR_RETURN(set_result, js_dom_replace_text_data(text_node, 0, old_u16_len, new_text));
            log_debug("js_dom_set_property: set text node data='%.30s'", new_text);
        }
        return value;
    }

    // CharacterData wrappers are ordinary JS objects too. Cleanup code in
    // DOM libraries stores bookkeeping on Text/Comment nodes before removal.
    if (!node->is_element()) {
        expando_set_property(node, prop_name, value);
        return value;
    }
    DomElement* elem = node->as_element();

    if (prop_id == JS_DOM_PROP_STYLE) {
        const char* style_text = fn_to_cstr(value);
        elem->set_attribute("style", style_text ? style_text : "");
        elem->set_styles_resolved(false);
        js_dom_mutation_notify(DOM_JS_MUTATION_STYLE, (DomNode*)elem, elem->parent);
        log_debug("js_dom_set_property: set style='%.50s' on <%s>",
                  style_text ? style_text : "", elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    auto item_to_scroll_value = [](Item scroll_value) -> float {
        TypeId value_type = get_type_id(scroll_value);
        if (value_type == LMD_TYPE_INT) return (float)it2i(scroll_value);
        if (value_type == LMD_TYPE_INT64) {
            // CSSOM scroll setters must preserve signed wide integers, used by
            // JS unary-minus numeric lowering for vertical-rl scrollLeft.
            return (float)it2l(scroll_value);
        }
        if (value_type == LMD_TYPE_FLOAT) return (float)it2d(scroll_value);
        if (value_type == LMD_TYPE_BOOL) return it2b(scroll_value) ? 1.0f : 0.0f;
        if (value_type == LMD_TYPE_STRING) {
            const char* text = fn_to_cstr(scroll_value);
            if (text) {
                char* end = nullptr;
                double parsed = strtod(text, &end);
                if (end != text) return (float)parsed;
            }
        }
        return 0.0f;
    };

    if (prop_id == JS_DOM_PROP_SCROLL_TOP || prop_id == JS_DOM_PROP_SCROLL_LEFT) {
        float scroll_value = item_to_scroll_value(value);

        bool is_vertical = prop_id == JS_DOM_PROP_SCROLL_TOP;
        bool is_root_scroll_target =
            (elem->tag_name && (strcasecmp(elem->tag_name, "html") == 0 ||
                                strcasecmp(elem->tag_name, "body") == 0));

        if (is_root_scroll_target && elem->doc) {
            // A pending viewport request has no signed element range yet;
            // preserve the DOM non-negative origin until layout commits it.
            if (scroll_value < 0.0f) scroll_value = 0.0f;
            if (is_vertical) {
                elem->doc->pending_viewport_scroll_y = scroll_value;
            } else {
                elem->doc->pending_viewport_scroll_x = scroll_value;
            }
            log_debug("js_dom_set_property: pending viewport %s=%.1f on <%s>",
                      prop, scroll_value, elem->tag_name);
            return value;
        }

        bool layout_pending = elem->doc && elem->doc->state &&
            ((DocState*)elem->doc->state)->lifecycle != DOC_LIFECYCLE_COMMITTED;
        bool vertical_rl_signed_scroll = !is_vertical && scroll_value < 0.0f &&
            layout_element_writing_mode(elem) == WM_VERTICAL_RL;
        const FlexProp* flex = elem->embedp()->flex;
        CssEnum flex_direction = flex
            ? (CssEnum)flex->direction
            : layout_specified_keyword(elem, CSS_PROPERTY_FLEX_DIRECTION, CSS_VALUE_ROW);
        bool column_reverse_signed_scroll = is_vertical && scroll_value < 0.0f &&
            flex_direction == CSS_VALUE_COLUMN_REVERSE;
        bool signed_scroll = vertical_rl_signed_scroll || column_reverse_signed_scroll;
        bool signed_range_unresolved = signed_scroll && elem->scroller &&
            elem->scroll()->pane &&
            (is_vertical ? elem->scroll()->pane->v_min_scroll >= 0.0f
                         : elem->scroll()->pane->h_min_scroll >= 0.0f);
        // CSS Overflow permits negative offsets for vertical-rl and column-reverse
        // flex flows; preserve them while the pane still has its [0, 0]
        // provisional range so final layout can clamp against real overflow.
        if ((!elem->scroller || !elem->scroll()->pane) && scroll_value < 0.0f &&
            !signed_scroll) {
            // Without a committed pane there is no writing-mode-specific
            // signed range to validate against, so pending element scroll
            // state must retain the ordinary non-negative origin.
            scroll_value = 0.0f;
        }
        if (elem->scroller && elem->scroll()->pane && !layout_pending &&
            !signed_range_unresolved) {
            float current_x = 0.0f;
            float current_y = 0.0f;
            DocState* state = elem->doc ? elem->doc->state : nullptr;
            scroll_state_get_position_for_view(state, static_cast<View*>(elem),
                elem->scroll()->pane, &current_x, &current_y, NULL, NULL);
            if (is_vertical) {
                scroll_state_set_position_for_view(state, static_cast<View*>(elem),
                    elem->scroll()->pane, current_x, scroll_value, false);
                elem->set_has_pending_element_scroll_y(false);
            } else {
                scroll_state_set_position_for_view(state, static_cast<View*>(elem),
                    elem->scroll()->pane, scroll_value, current_y, false);
                elem->set_has_pending_element_scroll_x(false);
            }
            log_debug("js_dom_set_property: set %s=%.1f on <%s>",
                      prop, scroll_value, elem->tag_name ? elem->tag_name : "?");
            return value;
        }

        // initial script execution can see a pane before flex sizing computes its max;
        // defer the requested scroll so finalization clamps it against the real overflow.
        if (is_vertical) {
            elem->set_pending_scroll_y(scroll_value);
            elem->set_has_pending_element_scroll_y(true);
        } else {
            elem->set_pending_scroll_x(scroll_value);
            elem->set_has_pending_element_scroll_x(true);
        }
        log_debug("js_dom_set_property: pending element %s=%.1f on <%s>",
                  prop, scroll_value, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    if (prop_id == JS_DOM_PROP_SRCDOC && _is_tag(elem, "iframe")) {
        const char* srcdoc = fn_to_cstr(value);
        elem->set_attribute("srcdoc", srcdoc ? srcdoc : "");
        // srcdoc can be assigned after the browsing context was lazily cached;
        // rehydrate that existing blank document before dispatching its load.
        js_dom_after_srcdoc_set((void*)elem);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    // className
    if (prop_id == JS_DOM_PROP_CLASS_NAME) {
        const char* class_str = fn_to_cstr(value);
        if (class_str) {
            // set_attribute owns the pooled class cache; writing class_names
            // directly bypasses its persistent-field lifetime bookkeeping.
            elem->set_attribute("class", class_str);
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            log_debug("js_dom_set_property: set className='%s' on <%s>",
                      class_str, elem->tag_name ? elem->tag_name : "?");
        }
        return value;
    }

    // CE-1 / CE-4 (Radiant_Design_Content_Editable.md §4.2):
    // contentEditable setter validates per HTML spec. Empty string maps to
    // "inherit" (attribute removed). Invalid values are a SyntaxError — we
    // log and ignore; the proper raise will be wired through the JS
    // DOMException machinery in a follow-up.
    if (prop_id == JS_DOM_PROP_CONTENT_EDITABLE) {
        const char* s = nullptr;
        if (get_type_id(value) == LMD_TYPE_BOOL) {
            s = it2b(value) ? "true" : "false";
        } else {
            s = fn_to_cstr(value);
        }
        if (!s) s = "";
        if (*s == '\0') {
            elem->remove_attribute("contenteditable");
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }
        const char* normalized = js_dom_normalize_contenteditable(s);
        if (!normalized) {
            log_debug("js_dom_contentEditable_setter_syntax_error: invalid value '%s'", s);
            return js_dom_throw_syntax_error("Invalid contentEditable value");
        }
        if (strcmp(normalized, "inherit") == 0) {
            elem->remove_attribute("contenteditable");
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }
        elem->set_attribute("contenteditable", normalized);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (prop_id == JS_DOM_PROP_AUTOCAPITALIZE) {
        const char* s = js_dom_to_attr_cstr(value);
        elem->set_attribute("autocapitalize", s);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (prop_id == JS_DOM_PROP_AUTOCORRECT) {
        elem->set_attribute("autocorrect", js_is_truthy(value) ? "on" : "off");
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (prop_id == JS_DOM_PROP_SPELLCHECK) {
        const char* s = js_is_truthy(value) ? "true" : "false";
        TypeId vt = get_type_id(value);
        if (vt == LMD_TYPE_STRING || vt == LMD_TYPE_SYMBOL) {
            const char* raw = fn_to_cstr(value);
            s = raw ? raw : "";
        }
        elem->set_attribute("spellcheck", s);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (prop_id == JS_DOM_PROP_WRITING_SUGGESTIONS) {
        const char* s = js_is_truthy(value) ? "true" : "false";
        TypeId vt = get_type_id(value);
        if (vt == LMD_TYPE_STRING || vt == LMD_TYPE_SYMBOL) {
            s = js_dom_to_attr_cstr(value);
        }
        elem->set_attribute("writingsuggestions", s);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    // id
    if (prop_id == JS_DOM_PROP_ID) {
        const char* id_str = fn_to_cstr(value);
        if (id_str && elem->doc && elem->doc->document_pool) {
            size_t len = strlen(id_str);
            char* id_copy = (char*)pool_alloc(elem->doc->document_pool, len + 1);
            memcpy(id_copy, id_str, len);
            id_copy[len] = '\0';
            elem->id = id_copy;
            elem->set_attribute("id", id_str);
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            log_debug("js_dom_set_property: set id='%s' on <%s>",
                      id_str, elem->tag_name ? elem->tag_name : "?");
        }
        return value;
    }

    if (prop_id == JS_DOM_PROP_SLOT) {
        const char* slot_name = js_dom_to_attr_cstr(value);
        const char* old_value = elem->get_attribute("slot");
        elem->set_attribute("slot", slot_name ? slot_name : "");
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem,
                               elem->parent, "slot", old_value);
        return value;
    }

    // innerText: replace children while preserving line breaks as <br> nodes.
    if (prop_id == JS_DOM_PROP_INNER_TEXT) {
        const char* text_str = fn_to_cstr(value);
        if (text_str) {
            js_dom_detach_all_children(elem);

            const char* segment = text_str;
            const char* p = text_str;
            while (true) {
                if (*p == '\n' || *p == '\r' || *p == '\0') {
                    size_t segment_len = (size_t)(p - segment);
                    if (segment_len > 0) {
                        DomText* text_node = DomText::create_copy(
                            segment, segment_len, elem);
                        if (text_node) {
                            ((DomNode*)elem)->append_child((DomNode*)text_node);
                            dom_post_insert((DomNode*)elem, (DomNode*)text_node);
                        }
                    }
                    if (*p == '\0') break;
                    MarkBuilder builder(elem->doc->input);
                    Item br_item = builder.element("br").final();
                    if (get_type_id(br_item) == LMD_TYPE_ELEMENT &&
                        br_item.element) {
                        DomElement* br_elem = dom_element_create(
                            elem->doc, "br", br_item.element);
                        if (br_elem) {
                            ((DomNode*)elem)->append_child((DomNode*)br_elem);
                            dom_post_insert((DomNode*)elem, (DomNode*)br_elem);
                        }
                    }
                    if (*p == '\r' && p[1] == '\n') p++;
                    p++;
                    segment = p;
                    continue;
                }
                p++;
            }
            log_debug("js_dom_set_property: set innerText on <%s>",
                      elem->tag_name ? elem->tag_name : "?");
            // innerText is parent-local child replacement, so keep the detailed
            // remove/insert records for incremental reconcile.
            js_dom_mutation_notify();
        }
        return value;
    }

    // textContent
    if (prop_id == JS_DOM_PROP_TEXT_CONTENT) {
        // Node.textContent performs DOMString conversion; reactive libraries
        // routinely assign numbers directly from their data model.
        const char* text_str = value.item == ITEM_NULL
            ? "" : js_dom_to_dom_string_cstr(value);
        if (text_str) {
            // Clear the Mark backing with the DOM chain. Clearing only the
            // links leaves stale children in the parent Element, so a later
            // library append relinks its new SVG below a detached wrapper.
            if (!js_dom_replace_inner_html(elem, "", false)) return ItemNull;
            // DOM string-replace-all uses no replacement node for empty strings.
            if (text_str[0] != '\0') {
                DomText* text_node = elem->append_text(text_str);
                if (!text_node) return ItemNull;
                dom_post_insert((DomNode*)elem, (DomNode*)text_node, false);
            }
            log_debug("js_dom_set_property: set textContent on <%s>",
                      elem->tag_name ? elem->tag_name : "?");
            // textContent replaces children, but its impact is still known:
            // stylesheet text changes CSS globally while ordinary text changes
            // only the target subtree. TREE_REPLACE erased that distinction and
            // incorrectly forced broad reconciliation for both cases.
            DomJsMutationKind kind = elem->tag_name &&
                strcasecmp(elem->tag_name, "style") == 0
                ? DOM_JS_MUTATION_STYLE : DOM_JS_MUTATION_TEXT;
            js_dom_mutation_notify(kind, (DomNode*)elem, (DomNode*)elem);
        }
        return value;
    }

    // v12b: innerHTML setter — parse HTML and replace children
    if (prop_id == JS_DOM_PROP_INNER_HTML) {
        const char* html_str = fn_to_cstr(value);
        if (!html_str) return ItemNull;
        js_dom_replace_inner_html(elem, html_str, true);
        return value;
    }

    // ------------------------------------------------------------------
    // Boolean IDL setters that must update internal state, not just the
    // content attribute. `checked` writes the live "checkedness" flag
    // (HTML §4.10.5.3.21).
    // ------------------------------------------------------------------
    if (prop_id == JS_DOM_PROP_CHECKED && _is_tag(elem, "input")) {
        _set_checkedness(elem, js_is_truthy(value));
        // Mark the dirty checkedness flag so subsequent `checked` content
        // attribute changes do not override the value.
        Item exp = expando_get_or_create_map((DomNode*)elem);
        if (exp.item != ITEM_NULL) {
            js_set_key_cstr(exp, "__chkDirty", (Item){.item = b2it(true)});
        }
        return value;
    }

    // input.defaultChecked setter — reflects `checked` attribute. Per spec,
    // when the dirty checkedness flag is false, current checkedness also
    // updates to match the new default.
    if (prop_id == JS_DOM_PROP_DEFAULT_CHECKED && _is_tag(elem, "input")) {
        bool t = js_is_truthy(value);
        if (t) elem->set_attribute("checked", "");
        else   elem->remove_attribute("checked");
        Item exp = expando_get_map((DomNode*)elem);
        bool dirty = false;
        if (exp.item != ITEM_NULL) {
            Item v = js_get_key_cstr(exp, "__chkDirty");
            dirty = v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
        }
        if (!dirty) _set_checkedness(elem, t);
        return value;
    }

    // ------------------------------------------------------------------
    // F-5: HTMLSelectElement / HTMLOptionElement IDL setters
    // ------------------------------------------------------------------
    if (_is_tag(elem, "select")) {
        if (prop_id == JS_DOM_PROP_VALUE) {
            const char* sv = fn_to_cstr(value);
            if (!sv) sv = "";
            js_dom_select_apply_value(elem, sv, false);
            js_dom_expando_flag_set(elem, "__selDirty", (Item){.item = b2it(true)});
            return value;
        }
        if (prop_id == JS_DOM_PROP_SELECTED_INDEX) {
            _select_set_selected_index(elem, _select_index_from_item(value));
            return value;
        }
        if (prop_id == JS_DOM_PROP_LENGTH) {
            js_dom_select_set_length_bridge((void*)elem, value);
            return value;
        }
    }
    if (_is_tag(elem, "option")) {
        if (prop_id == JS_DOM_PROP_SELECTED) {
            bool selected = js_is_truthy(value);
            js_dom_apply_option_selected(elem, selected);
            return value;
        }
        // option.defaultSelected setter — reflects `selected` attribute.
        // When the dirty selectedness flag is false, current selectedness
        // updates to match the new default.
        if (prop_id == JS_DOM_PROP_DEFAULT_SELECTED) {
            bool t = js_is_truthy(value);
            if (t) elem->set_attribute("selected", "");
            else   elem->remove_attribute("selected");
            Item exp = expando_get_map((DomNode*)elem);
            bool dirty = false;
            if (exp.item != ITEM_NULL) {
                Item v = js_get_key_cstr(exp, "__optDirty");
                dirty = v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
            }
            if (!dirty) {
                _set_selectedness(elem, t);
                DomElement* sel = _option_owner_select(elem);
                if (sel) _select_ask_for_reset(sel);
            }
            return value;
        }
        if (prop_id == JS_DOM_PROP_VALUE) {
            const char* sv = fn_to_cstr(value);
            elem->set_attribute("value", sv ? sv : "");
            return value;
        }
        if (prop_id == JS_DOM_PROP_TEXT) {
            const char* sv = fn_to_cstr(value);
            js_dom_set_option_text_bridge((void*)elem, sv ? sv : "");
            return value;
        }
        if (prop_id == JS_DOM_PROP_DEFAULT_SELECTED) {
            if (js_is_truthy(value)) elem->set_attribute("selected", "");
            else elem->remove_attribute("selected");
            return value;
        }
    }
    if (prop_id == JS_DOM_PROP_VALUE && _is_tag(elem, "input") && !tc_is_text_control_elem(elem)) {
        const char* s = fn_to_cstr(value);
        if (!s) s = "";
        elem->set_attribute("value", s);
        if (elem->form) {
            elem->form->value = elem->get_attribute("value");
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    // ------------------------------------------------------------------
    // Text-control IDL setters — must intercept before the generic
    // expando/attribute fallback. Per HTML §4.10.6.
    // ------------------------------------------------------------------
    if (tc_is_text_control_elem(elem)) {
        if (prop_id == JS_DOM_PROP_VALUE) {
            return js_dom_text_control_set_value_bridge((void*)elem, value);
        }
        if (prop_id == JS_DOM_PROP_SELECTION_START) {
            return js_dom_text_control_set_selection_start_bridge((void*)elem, value);
        }
        if (prop_id == JS_DOM_PROP_SELECTION_END) {
            return js_dom_text_control_set_selection_end_bridge((void*)elem, value);
        }
        if (prop_id == JS_DOM_PROP_SELECTION_DIRECTION) {
            return js_dom_text_control_set_selection_direction_bridge((void*)elem, value);
        }
        if (prop_id == JS_DOM_PROP_DEFAULT_VALUE) {
            return js_dom_text_control_set_default_value_bridge((void*)elem, value);
        }
    }

    // ------------------------------------------------------------------
    // F-0: Reflected attribute setters
    //   - boolean: ToBoolean(value); truthy → set (empty), falsy → remove
    //   - integer: ToInt32(value); write decimal string to attribute
    //   - string : write attribute under spec attribute name (handles
    //              IDL→HTML name mapping like readOnly→readonly)
    // ------------------------------------------------------------------
    {
        if (prop_id == JS_DOM_PROP_TAB_INDEX) {
            long tab_index = 0;
            TypeId value_type = get_type_id(value);
            if (value_type == LMD_TYPE_INT) {
                tab_index = (long)it2i(value);
            } else if (value_type == LMD_TYPE_FLOAT) {
                tab_index = (long)it2d(value);
            } else {
                const char* text = fn_to_cstr(value);
                char* end = nullptr;
                if (text && *text) {
                    long parsed = strtol(text, &end, 10);
                    if (end != text) tab_index = parsed;
                }
            }
            // Widgets assign tabIndex=-1 before calling focus(); storing it as
            // an expando drops the programmatic-focus eligibility and leaves
            // keyboard events on the previous control.
            char text[32];
            snprintf(text, sizeof(text), "%ld", tab_index);
            elem->set_attribute("tabindex", text);
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }

        // Boolean reflection — handle BEFORE int/string so e.g. `disabled`
        // assignment of a non-bool truthy value writes empty string, not the
        // raw value.
        if (_is_bool_reflected(elem, prop)) {
            const char* attr = _idl_to_attr_name(prop);
            if (!attr) attr = prop;
            // lowercase the attr name in case caller passed the camelCase form
            // verbatim (e.g. `readonly` already matches; `disabled` already
            // lowercase). Boolean reflected attrs we list above are already
            // in lowercase form via the mapping table.
            bool truthy = js_is_truthy(value);
            if (truthy) {
                elem->set_attribute(attr, "");
                if (strcmp(attr, "disabled") == 0) {
                    js_dom_clear_focus_if_disabled_now(elem);
                }
            } else {
                elem->remove_attribute(attr);
                if (_is_tag(elem, "select") && strcmp(attr, "multiple") == 0) {
                    _select_ask_for_reset(elem);
                }
            }
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }

        // Integer reflection — coerce to long, write canonical decimal.
        const char* int_attr = nullptr; int int_default = 0;
        if (_is_int_reflected(elem, prop, &int_attr, &int_default)) {
            long n = int_default;
            TypeId vt = get_type_id(value);
            if (vt == LMD_TYPE_INT) {
                n = (long)it2i(value);
            } else if (vt == LMD_TYPE_FLOAT) {
                double d = it2d(value);
                n = (long)d;
            } else {
                const char* s = fn_to_cstr(value);
                if (s && *s) {
                    char* end = nullptr;
                    long parsed = strtol(s, &end, 10);
                    n = (end != s) ? parsed : 0;  // non-numeric → 0
                } else {
                    n = 0;
                }
            }
            if (n < 0) n = int_default;  // negative → reset to default
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", n);
            elem->set_attribute(int_attr, buf);
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }

        // String reflection with IDL→HTML name mapping (readOnly → readonly,
        // formAction → formaction, htmlFor → for, etc.).
        const char* mapped_attr = _idl_to_attr_name(prop);
        if (mapped_attr && _is_mapped_attr_reflected(elem, prop)) {
            const char* s = fn_to_cstr(value);
            if (s) {
                elem->set_attribute(mapped_attr, s);
            } else {
                elem->remove_attribute(mapped_attr);
            }
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }

        const char* string_attr = nullptr;
        if (_is_string_reflected(elem, prop, &string_attr)) {
            const char* s = js_dom_to_attr_cstr(value);
            elem->set_attribute(string_attr, s);
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }

        // <input>.type setter — lowercase, fall back to "text" for unknown.
        if (prop_id == JS_DOM_PROP_TYPE && _is_tag(elem, "input")) {
            const char* s = fn_to_cstr(value);
            if (s && *s) {
                char buf[32];
                size_t i = 0;
                for (; s[i] && i < sizeof(buf) - 1; i++)
                    buf[i] = (char)tolower((unsigned char)s[i]);
                buf[i] = '\0';
                elem->set_attribute("type", buf);
            } else {
                elem->set_attribute("type", "text");
            }
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }
    }

    // Generic property set: arbitrary JS properties on DOM nodes are expandos.
    // Real attribute reflection is handled by the explicit reflected setters
    // above, and setAttribute() remains the DOM attribute mutation path.
    {
        expando_set_property((DomNode*)elem, prop_name, value);
        if (prop[0] == 'o' && prop[1] == 'n' && prop[2] != '\0') {
            // DOM host setters store on* values in the expando side table;
            // register the same write in the listener list so assignment
            // order relative to addEventListener() remains observable.
            js_dom_event_handler_property_set_for_node(
                elem, prop, (int)strlen(prop), value);
        }
    }
    return value;
}

extern "C" Item js_dom_set_style_property(Item elem_item, Item prop_name, Item value) {
    if (js_is_rule_style_decl(elem_item)) {
        // CSSOM style declarations are VMaps, not DOM elements; handle them
        // before DOM unwrapping so nested rule.style.x lowering stays native.
        return js_cssom_rule_decl_set_property(elem_item, prop_name, value);
    }
    if (js_is_css_rule(elem_item)) {
        Item style_obj = js_cssom_rule_get_style(elem_item);
        if (js_is_rule_style_decl(style_obj)) {
            return js_cssom_rule_decl_set_property(style_obj, prop_name, value);
        }
        return ItemNull;
    }

    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // not a DOM element — fall back to normal property set on obj.style
        Item style_obj = js_get_name_key(elem_item, "style");
        TypeId style_type = get_type_id(style_obj);
        if (style_obj.item != ITEM_NULL &&
            (style_type == LMD_TYPE_MAP || style_type == LMD_TYPE_VMAP)) {
            return js_set_key_default(style_obj, prop_name, value);
        }
        return ItemNull;
    }

    const char* js_prop = fn_to_cstr(prop_name);
    // CSSStyleDeclaration assignment performs Web IDL DOMString coercion;
    // reading raw Item storage made numeric animation values look empty.
    const char* val_str = js_dom_to_dom_string_cstr(value);
    if (!js_prop || !val_str) return ItemNull;

    // convert camelCase JS property to CSS property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));
    if (!js_inline_style_cssom_property_exposed(css_prop)) {
        log_debug("js_dom_set_style_property: ignored unsupported CSSOM property %s on <%s>",
                  css_prop, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // handle cssText special case: replace entire inline style
    if (strcmp(css_prop, "cssText") == 0) {
        elem->set_attribute("style", val_str);
        js_dom_mutation_notify(DOM_JS_MUTATION_STYLE, (DomNode*)elem, elem->parent);
        log_debug("js_dom_set_style_property: set cssText='%.50s' on <%s>",
                  val_str, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // CSSOM §6.7.3: setting a property to empty string removes it
    if (!val_str[0]) {
        CssPropertyCode prop_id = css_property_code_from_name(css_prop);
        if (prop_id != CSS_PROPERTY_UNKNOWN && elem->specified_style) {
            js_dom_update_inline_style_attribute(elem, css_prop, "", nullptr);
            elem->set_styles_resolved(false);
            js_dom_mutation_notify(js_dom_style_mutation_kind(prop_id),
                                   (DomNode*)elem, elem->parent);
        }
        log_debug("js_dom_set_style_property: removed %s (CSS: %s) on <%s>",
                  js_prop, css_prop, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // validate: reject values with invalid non-ASCII codepoints (CSS Syntax §4.2)
    for (size_t i = 0; val_str[i]; ) {
        unsigned char b = (unsigned char)val_str[i];
        if (b < 0x80) {
            i++;
        } else {
            UnicodeChar uc = css_parse_unicode_char(val_str + i, strlen(val_str + i));
            if (uc.byte_length == 0 || !css_is_name_char_unicode(uc.codepoint)) {
                log_debug("js_dom_set_style_property: rejecting value with invalid codepoint U+%04X at byte offset %zu (byte=0x%02X)", uc.codepoint, i, b);
                return value;  // silently reject per CSSOM spec
            }
            i += uc.byte_length;
        }
    }

    // apply as inline style (highest cascade priority)
    js_dom_update_inline_style_attribute(elem, css_prop, val_str, nullptr);
    elem->set_styles_resolved(false);  // mark for re-cascading
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);
    js_dom_mutation_notify(js_dom_style_mutation_kind(prop_id),
                           (DomNode*)elem, elem->parent);

    log_debug("js_dom_set_style_property: set %s='%s' (CSS: %s) on <%s>",
              js_prop, val_str, css_prop, elem->tag_name ? elem->tag_name : "?");
    return value;
}

// ============================================================================
// Style Property Read (elem.style.X)
// ============================================================================

extern "C" Item js_dom_get_style_property(Item elem_item, Item prop_name) {
    if (js_is_rule_style_decl(elem_item)) {
        // CSSOM style declarations are VMaps, not DOM elements; handle them
        // before DOM unwrapping so nested rule.style.x lowering stays native.
        return js_cssom_rule_decl_get_property(elem_item, prop_name);
    }
    if (js_is_css_rule(elem_item)) {
        Item style_obj = js_cssom_rule_get_style(elem_item);
        if (js_is_rule_style_decl(style_obj)) {
            return js_cssom_rule_decl_get_property(style_obj, prop_name);
        }
        return js_name_item("");
    }

    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // not a DOM element — fall back to normal property access on obj.style
        Item style_obj = js_get_name_key(elem_item, "style");
        TypeId style_type = get_type_id(style_obj);
        if (style_obj.item != ITEM_NULL &&
            (style_type == LMD_TYPE_MAP || style_type == LMD_TYPE_VMAP)) {
            return js_get_key_default(style_obj, prop_name);
        }
        return js_name_item("");
    }

    const char* js_prop = fn_to_cstr(prop_name);
    if (!js_prop) return js_name_item("");

    // convert camelCase JS property to CSS property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));
    if (!js_inline_style_cssom_property_exposed(css_prop)) {
        return js_name_item("");
    }

    // v12: cssText getter — return the raw inline style string
    if (strcmp(css_prop, "cssText") == 0) {
        const char* inline_style = dom_element_get_inline_style(elem);
        return js_name_item(inline_style ? inline_style : "");
    }

    // look up the CSS property ID
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);
    if (prop_id == CSS_PROPERTY_UNKNOWN) {
        log_debug("js_dom_get_style_property: unknown CSS property '%s'", css_prop);
        return js_name_item("");
    }

    // get the specified value for this property
    CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
    if (!decl || (!decl->value && (!decl->value_text || decl->value_text_len == 0))) {
        // shorthand fallback: if the property is a shorthand (e.g. padding, margin),
        // try the first longhand (e.g. padding-top) since shorthands are expanded
        if (css_property_is_shorthand(prop_id)) {
            char longhand[128];
            snprintf(longhand, sizeof(longhand), "%s-top", css_prop);
            CssPropertyCode lh_id = css_property_code_from_name(longhand);
            if (lh_id != CSS_PROPERTY_UNKNOWN) {
                decl = dom_element_get_specified_value(elem, lh_id);
            }
        }
        if (!decl || (!decl->value && (!decl->value_text || decl->value_text_len == 0))) {
            return js_name_item("");
        }
    }

    // only return values that came from inline styles (element.style.X should
    // only reflect inline styles, not stylesheet rules)
    if (!decl->specificity.inline_style) {
        return js_name_item("");
    }

    Pool* pool = elem->doc ? elem->doc->document_pool : nullptr;
    if (!pool) {
        return js_name_item("");
    }
    const char* serialized = css_serialize_declaration_value(decl, pool);
    return js_name_item(serialized ? serialized : "");
}

// open-name membership for style hosts: `in` answers from the CSS property
// table without invoking a getter (style VMaps have no ordinary shape)
extern "C" Item js_style_css_has(Item style_item, Item prop_name) {
    (void)style_item;
    const char* prop = fn_to_cstr(prop_name);
    if (!prop || !prop[0]) return (Item){.item = b2it(false)};
    char css_prop[128];
    js_camel_to_css_prop(prop, css_prop, sizeof(css_prop));
    if (!js_inline_style_cssom_property_exposed(css_prop)) {
        return (Item){.item = b2it(false)};
    }
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);
    return (Item){.item = b2it(prop_id != CSS_PROPERTY_UNKNOWN && prop_id != 0)};
}

// ============================================================================
// Element Method Dispatcher
// ============================================================================

static void js_dom_set_number_property(Item object, const char* name,
                                       float value) {
    js_set_name_key(object, name, push_d((double)value));
}

Item js_dom_make_rect(double x, double y, double width, double height) {
    Item rect = js_new_object();
    js_set_name_key(rect, "x", push_d(x));
    js_set_name_key(rect, "y", push_d(y));
    js_set_name_key(rect, "top", push_d(y));
    js_set_name_key(rect, "left", push_d(x));
    js_set_name_key(rect, "right", push_d(x + width));
    js_set_name_key(rect, "bottom", push_d(y + height));
    js_set_name_key(rect, "width", push_d(width));
    js_set_name_key(rect, "height", push_d(height));
    return rect;
}

JS_FORWARD_STATIC_ITEM(js_dom_make_rect_object,
    (float x, float y, float width, float height),
    js_dom_make_rect, ((double)x, (double)y, (double)width, (double)height))

static float js_dom_svg_number(Item value, float fallback) {
    Item numeric = js_to_number(value);
    TypeId type = get_type_id(numeric);
    if (type == LMD_TYPE_FLOAT) return (float)it2d(numeric);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) return (float)it2i(numeric);
    return fallback;
}

static RdtMatrix js_dom_svg_matrix_from_item(Item item) {
    RdtMatrix matrix = rdt_matrix_identity();
    matrix.e11 = js_dom_svg_number(js_get_key_cstr(item, "a"), 1.0f);
    matrix.e21 = js_dom_svg_number(js_get_key_cstr(item, "b"), 0.0f);
    matrix.e12 = js_dom_svg_number(js_get_key_cstr(item, "c"), 0.0f);
    matrix.e22 = js_dom_svg_number(js_get_key_cstr(item, "d"), 1.0f);
    matrix.e13 = js_dom_svg_number(js_get_key_cstr(item, "e"), 0.0f);
    matrix.e23 = js_dom_svg_number(js_get_key_cstr(item, "f"), 0.0f);
    return matrix;
}

static Item js_dom_svg_make_matrix_with_interface(RdtMatrix matrix,
                                                   const char* interface_name);
JS_FORWARD_STATIC_ITEM(js_dom_svg_make_matrix, (RdtMatrix matrix), js_dom_svg_make_matrix_with_interface, (matrix, "SVGMatrix"))

enum JsSvgMatrixOperation {
    JS_SVG_MATRIX_MULTIPLY,
    JS_SVG_MATRIX_INVERSE,
    JS_SVG_MATRIX_TRANSLATE,
    JS_SVG_MATRIX_SCALE,
    JS_SVG_MATRIX_ROTATE,
    JS_SVG_MATRIX_FLIP_X,
    JS_SVG_MATRIX_FLIP_Y,
};

static Item js_dom_svg_matrix_operation(Item callee, Item this_value, Item* args,
                                        int argc, uint64_t* result_home) {
    (void)this_value;
    (void)result_home;
    JsFunction* fn = (JsFunction*)callee.function;
    JsSvgMatrixOperation operation = fn
        ? (JsSvgMatrixOperation)fn->native_target.bits : JS_SVG_MATRIX_INVERSE;
    Item arg0 = argc > 0 ? args[0] : make_js_undefined();
    Item arg1 = argc > 1 ? args[1] : make_js_undefined();
    RdtMatrix matrix = js_dom_svg_matrix_from_item(js_get_this());
    switch (operation) {
    case JS_SVG_MATRIX_MULTIPLY: {
        RdtMatrix right = js_dom_svg_matrix_from_item(arg0);
        return js_dom_svg_make_matrix(rdt_matrix_multiply(&matrix, &right));
    }
    case JS_SVG_MATRIX_INVERSE: {
        float determinant = matrix.e11 * matrix.e22 - matrix.e21 * matrix.e12;
        if (fabsf(determinant) < 0.000001f)
            return js_throw_type_error("SVGMatrix is not invertible");
        float reciprocal = 1.0f / determinant;
        RdtMatrix inverse = {
            matrix.e22 * reciprocal, -matrix.e12 * reciprocal,
            (matrix.e12 * matrix.e23 - matrix.e22 * matrix.e13) * reciprocal,
            -matrix.e21 * reciprocal, matrix.e11 * reciprocal,
            (matrix.e21 * matrix.e13 - matrix.e11 * matrix.e23) * reciprocal,
            0, 0, 1
        };
        return js_dom_svg_make_matrix(inverse);
    }
    case JS_SVG_MATRIX_TRANSLATE: {
        RdtMatrix translation = rdt_matrix_translate(js_dom_svg_number(arg0, 0.0f),
                                                      js_dom_svg_number(arg1, 0.0f));
        return js_dom_svg_make_matrix(rdt_matrix_multiply(&matrix, &translation));
    }
    case JS_SVG_MATRIX_SCALE: {
        float value = js_dom_svg_number(arg0, 1.0f);
        RdtMatrix scale = {value, 0, 0, 0, value, 0, 0, 0, 1};
        return js_dom_svg_make_matrix(rdt_matrix_multiply(&matrix, &scale));
    }
    case JS_SVG_MATRIX_ROTATE: {
        float radians = js_dom_svg_number(arg0, 0.0f) * 0.01745329251994329577f;
        float cosine = cosf(radians);
        float sine = sinf(radians);
        RdtMatrix rotation = {cosine, -sine, 0, sine, cosine, 0, 0, 0, 1};
        return js_dom_svg_make_matrix(rdt_matrix_multiply(&matrix, &rotation));
    }
    case JS_SVG_MATRIX_FLIP_X: {
        RdtMatrix flip = {-1, 0, 0, 0, 1, 0, 0, 0, 1};
        return js_dom_svg_make_matrix(rdt_matrix_multiply(&matrix, &flip));
    }
    case JS_SVG_MATRIX_FLIP_Y: {
        RdtMatrix flip = {1, 0, 0, 0, -1, 0, 0, 0, 1};
        return js_dom_svg_make_matrix(rdt_matrix_multiply(&matrix, &flip));
    }
    }
    return ItemNull;
}

static Item js_dom_svg_make_matrix_with_interface(RdtMatrix matrix,
                                                   const char* interface_name) {
    Item result = js_new_object();
    js_dom_set_number_property(result, "a", matrix.e11);
    js_dom_set_number_property(result, "b", matrix.e21);
    js_dom_set_number_property(result, "c", matrix.e12);
    js_dom_set_number_property(result, "d", matrix.e22);
    js_dom_set_number_property(result, "e", matrix.e13);
    js_dom_set_number_property(result, "f", matrix.e23);
    js_set_key_cstr(result, "multiply", js_new_native_payload_function(js_dom_svg_matrix_operation,
            JS_SVG_MATRIX_MULTIPLY, 1));
    js_set_key_cstr(result, "inverse", js_new_native_payload_function(js_dom_svg_matrix_operation,
            JS_SVG_MATRIX_INVERSE, 0));
    js_set_key_cstr(result, "translate", js_new_native_payload_function(js_dom_svg_matrix_operation,
            JS_SVG_MATRIX_TRANSLATE, 2));
    js_set_key_cstr(result, "scale", js_new_native_payload_function(js_dom_svg_matrix_operation,
            JS_SVG_MATRIX_SCALE, 1));
    js_set_key_cstr(result, "rotate", js_new_native_payload_function(js_dom_svg_matrix_operation,
            JS_SVG_MATRIX_ROTATE, 1));
    js_set_key_cstr(result, "flipX", js_new_native_payload_function(js_dom_svg_matrix_operation,
            JS_SVG_MATRIX_FLIP_X, 0));
    js_set_key_cstr(result, "flipY", js_new_native_payload_function(js_dom_svg_matrix_operation,
            JS_SVG_MATRIX_FLIP_Y, 0));
    Item global = js_get_global_this();
    Item ctor = js_get_key_default(global, js_string_key(interface_name));
    Item proto = js_get_key_cstr(ctor, "prototype");
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(result, proto);
    return result;
}
JS_FORWARD_STATIC_ITEM(js_dom_svg_create_matrix, (void), js_dom_svg_make_matrix, (rdt_matrix_identity()))

static DomElement* js_dom_svg_owner_from_value(Item value) {
    Item owner = js_get_key_cstr(value, "__lambda_svg_owner");
    return (DomElement*)js_dom_unwrap_element(owner);
}

static void js_dom_svg_set_transform_attribute(DomElement* elem, RdtMatrix matrix) {
    if (!elem) return;
    char text[192];
    snprintf(text, sizeof(text), "matrix(%g %g %g %g %g %g)",
             (double)matrix.e11, (double)matrix.e21, (double)matrix.e12,
             (double)matrix.e22, (double)matrix.e13, (double)matrix.e23);
    // SVGTransformList mutations must update the backing attribute; otherwise
    // later CTM reads and renderer traversal observe a stale transform.
    elem->set_attribute("transform", text);
    js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
}

static Item js_dom_svg_transform_set_matrix(Item matrix_item) {
    Item transform = js_get_this();
    js_set_key_cstr(transform, "matrix", js_dom_svg_make_matrix(js_dom_svg_matrix_from_item(matrix_item)));
    return ItemNull;
}

static Item js_dom_svg_transform_set_translate(Item x, Item y) {
    RdtMatrix matrix = rdt_matrix_translate(js_dom_svg_number(x, 0.0f),
                                             js_dom_svg_number(y, 0.0f));
    return js_dom_svg_transform_set_matrix(js_dom_svg_make_matrix(matrix));
}

static Item js_dom_svg_transform_set_scale(Item scale) {
    float value = js_dom_svg_number(scale, 1.0f);
    RdtMatrix matrix = {value, 0, 0, 0, value, 0, 0, 0, 1};
    return js_dom_svg_transform_set_matrix(js_dom_svg_make_matrix(matrix));
}

static Item js_dom_svg_transform_set_rotate(Item angle, Item cx, Item cy) {
    float radians = js_dom_svg_number(angle, 0.0f) * 0.01745329251994329577f;
    float center_x = js_dom_svg_number(cx, 0.0f);
    float center_y = js_dom_svg_number(cy, 0.0f);
    float cosine = cosf(radians);
    float sine = sinf(radians);
    RdtMatrix rotation = {cosine, -sine, 0, sine, cosine, 0, 0, 0, 1};
    RdtMatrix before = rdt_matrix_translate(-center_x, -center_y);
    RdtMatrix after = rdt_matrix_translate(center_x, center_y);
    RdtMatrix rotated = rdt_matrix_multiply(&rotation, &before);
    RdtMatrix matrix = rdt_matrix_multiply(&after, &rotated);
    return js_dom_svg_transform_set_matrix(js_dom_svg_make_matrix(matrix));
}

static Item js_dom_svg_make_transform(Item matrix) {
    Item transform = js_new_object();
    js_set_key_cstr(transform, "matrix", js_dom_svg_make_matrix(js_dom_svg_matrix_from_item(matrix)));
    js_set_key_cstr(transform, "type", (Item){.item = i2it(1)});
    js_set_native_key(transform, js_string_key("setMatrix"), js_dom_svg_transform_set_matrix);
    js_set_native_key(transform, js_string_key("setTranslate"), js_dom_svg_transform_set_translate);
    js_set_native_key(transform, js_string_key("setScale"), js_dom_svg_transform_set_scale);
    js_set_native_key(transform, js_string_key("setRotate"), js_dom_svg_transform_set_rotate);
    Item global = js_get_global_this();
    Item ctor = js_get_key_cstr(global, "SVGTransform");
    Item proto = js_get_key_cstr(ctor, "prototype");
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(transform, proto);
    return transform;
}
JS_FORWARD_STATIC_ITEM(js_dom_svg_create_transform, (void), js_dom_svg_make_transform, (js_dom_svg_make_matrix(rdt_matrix_identity())))
JS_FORWARD_STATIC_ITEM(js_dom_svg_create_transform_from_matrix, (Item matrix), js_dom_svg_make_transform, (matrix))

static Item js_dom_svg_transform_list_count_get(void) {
    DomElement* elem = js_dom_svg_owner_from_value(js_get_this());
    const char* value = elem ? elem->get_attribute("transform") : nullptr;
    return (Item){.item = i2it(value && value[0] ? 1 : 0)};
}

static Item js_dom_svg_transform_list_clear(void) {
    DomElement* elem = js_dom_svg_owner_from_value(js_get_this());
    if (elem) {
        elem->set_attribute("transform", "");
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
    }
    return ItemNull;
}

static Item js_dom_svg_transform_list_append(Item transform) {
    DomElement* elem = js_dom_svg_owner_from_value(js_get_this());
    Item matrix = js_get_key_cstr(transform, "matrix");
    if (elem && matrix.item != ITEM_NULL && !is_js_undefined(matrix)) {
        js_dom_svg_set_transform_attribute(elem, js_dom_svg_matrix_from_item(matrix));
    }
    return transform;
}

static Item js_dom_svg_transform_list_consolidate(void) {
    DomElement* elem = js_dom_svg_owner_from_value(js_get_this());
    if (!elem || !elem->get_attribute("transform")) return ItemNull;
    return js_dom_svg_make_transform(
        js_dom_svg_make_matrix(js_dom_svg_transform_from_element(elem)));
}

static Item js_dom_svg_transform_list_get_item(Item index) {
    if (js_dom_svg_number(index, -1.0f) != 0.0f) return ItemNull;
    return js_dom_svg_transform_list_consolidate();
}

static Item js_dom_svg_transform_list_initialize(Item transform) {
    js_dom_svg_transform_list_clear();
    return js_dom_svg_transform_list_append(transform);
}

static Item js_dom_svg_get_transform_list(DomElement* elem) {
    Item list = js_new_object();
    js_set_key_cstr(list, "__lambda_svg_owner", js_dom_wrap_element(elem));
    js_install_native_accessor(list, js_string_key("numberOfItems"),
        js_new_native_function(js_dom_svg_transform_list_count_get), ItemNull,
        JSPD_NON_ENUMERABLE);
    js_set_native_key(list, js_string_key("clear"), js_dom_svg_transform_list_clear);
    js_set_native_key(list, js_string_key("appendItem"), js_dom_svg_transform_list_append);
    js_set_native_key(list, js_string_key("initialize"), js_dom_svg_transform_list_initialize);
    js_set_native_key(list, js_string_key("consolidate"), js_dom_svg_transform_list_consolidate);
    js_set_native_key(list, js_string_key("getItem"), js_dom_svg_transform_list_get_item);
    Item animated = js_new_object();
    js_set_key_cstr(animated, "baseVal", list);
    js_set_key_cstr(animated, "animVal", list);
    return animated;
}

static Item js_dom_svg_class_name_get_base_val(void) {
    DomElement* elem = js_dom_svg_owner_from_value(js_get_this());
    const char* class_name = elem ? elem->get_attribute("class") : nullptr;
    return js_name_item(class_name ? class_name : "");
}

static Item js_dom_svg_class_name_set_base_val(Item value) {
    DomElement* elem = js_dom_svg_owner_from_value(js_get_this());
    if (elem) {
        elem->set_attribute("class", js_dom_to_attr_cstr(value));
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
    }
    return value;
}

static Item js_dom_svg_get_animated_class_name(DomElement* elem) {
    Item animated = js_new_object();
    js_set_key_cstr(animated, "__lambda_svg_owner", js_dom_wrap_element(elem));
    js_install_native_accessor(animated, js_string_key("baseVal"),
        js_new_native_function(js_dom_svg_class_name_get_base_val),
        js_new_native_function(js_dom_svg_class_name_set_base_val),
        JSPD_NON_ENUMERABLE);
    js_install_native_accessor(animated, js_string_key("animVal"),
        js_new_native_function(js_dom_svg_class_name_get_base_val), ItemNull,
        JSPD_NON_ENUMERABLE);
    return animated;
}

static Item js_dom_svg_make_point(float x, float y, const char* interface_name);

static Item js_dom_svg_point_matrix_transform(Item matrix_item) {
    Item point_item = js_get_this();
    float x = js_dom_svg_number(js_get_key_cstr(point_item, "x"), 0.0f);
    float y = js_dom_svg_number(js_get_key_cstr(point_item, "y"), 0.0f);
    RdtMatrix matrix = js_dom_svg_matrix_from_item(matrix_item);
    float transformed_x = 0.0f;
    float transformed_y = 0.0f;
    rdt_matrix_transform_point(&matrix, x, y, &transformed_x, &transformed_y);
    return js_dom_svg_make_point(transformed_x, transformed_y, "DOMPoint");
}

static Item js_dom_svg_make_point(float x, float y, const char* interface_name) {
    Item result = js_new_object();
    js_dom_set_number_property(result, "x", x);
    js_dom_set_number_property(result, "y", y);
    js_dom_set_number_property(result, "z", 0.0f);
    js_dom_set_number_property(result, "w", 1.0f);
    js_set_native_key(result, js_string_key("matrixTransform"), js_dom_svg_point_matrix_transform);
    Item global = js_get_global_this();
    Item ctor = js_get_key_default(global, js_string_key(interface_name));
    Item proto = js_get_key_cstr(ctor, "prototype");
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(result, proto);
    return result;
}
JS_FORWARD_STATIC_ITEM(js_dom_svg_create_point, (void), js_dom_svg_make_point, (0.0f, 0.0f, "SVGPoint"))

static Item js_dom_point_constructor(Item x, Item y, Item z, Item w) {
    Item result = js_dom_svg_make_point(js_dom_svg_number(x, 0.0f),
                                        js_dom_svg_number(y, 0.0f), "DOMPoint");
    js_dom_set_number_property(result, "z", js_dom_svg_number(z, 0.0f));
    js_dom_set_number_property(result, "w", js_dom_svg_number(w, 1.0f));
    return result;
}

static Item js_dom_matrix_constructor(Item init) {
    RdtMatrix matrix = rdt_matrix_identity();
    if (get_type_id(init) == LMD_TYPE_ARRAY && js_array_length(init) >= 6) {
        matrix.e11 = js_dom_svg_number(js_elements_get_int(init, 0), 1.0f);
        matrix.e21 = js_dom_svg_number(js_elements_get_int(init, 1), 0.0f);
        matrix.e12 = js_dom_svg_number(js_elements_get_int(init, 2), 0.0f);
        matrix.e22 = js_dom_svg_number(js_elements_get_int(init, 3), 1.0f);
        matrix.e13 = js_dom_svg_number(js_elements_get_int(init, 4), 0.0f);
        matrix.e23 = js_dom_svg_number(js_elements_get_int(init, 5), 0.0f);
    } else if (get_type_id(init) == LMD_TYPE_MAP) {
        matrix = js_dom_svg_matrix_from_item(init);
    }
    return js_dom_svg_make_matrix_with_interface(matrix, "DOMMatrix");
}

typedef struct JsDomSvgBounds {
    float left;
    float top;
    float right;
    float bottom;
    bool valid;
} JsDomSvgBounds;

static void js_dom_svg_bounds_include_point(JsDomSvgBounds* bounds, float x, float y) {
    if (!bounds) return;
    if (!bounds->valid) {
        bounds->left = bounds->right = x;
        bounds->top = bounds->bottom = y;
        bounds->valid = true;
        return;
    }
    bounds->left = LMB_MIN(bounds->left, x);
    bounds->top = LMB_MIN(bounds->top, y);
    bounds->right = LMB_MAX(bounds->right, x);
    bounds->bottom = LMB_MAX(bounds->bottom, y);
}

static void js_dom_svg_bounds_include_rect(JsDomSvgBounds* bounds, float x, float y,
                                            float width, float height) {
    if (!bounds) return;
    js_dom_svg_bounds_include_point(bounds, x, y);
    js_dom_svg_bounds_include_point(bounds, x + width, y + height);
}

static void js_dom_svg_bounds_include_bounds(JsDomSvgBounds* bounds,
                                              const JsDomSvgBounds* child) {
    if (!bounds || !child || !child->valid) return;
    js_dom_svg_bounds_include_point(bounds, child->left, child->top);
    js_dom_svg_bounds_include_point(bounds, child->right, child->bottom);
}

static void js_dom_svg_bounds_apply_transform(JsDomSvgBounds* bounds,
                                               const RdtMatrix* transform) {
    if (!bounds || !bounds->valid || !transform) return;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    rdt_matrix_transform_rect_bounds(transform, bounds->left, bounds->top,
                                     bounds->right, bounds->bottom,
                                     &left, &top, &right, &bottom);
    bounds->left = left;
    bounds->top = top;
    bounds->right = right;
    bounds->bottom = bottom;
}

static float js_dom_svg_attribute_number(DomElement* elem, const char* name,
                                         float fallback) {
    if (!elem || !name) return fallback;
    const char* value = elem->get_attribute(name);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    float parsed = strtof(value, &end);
    return end != value ? parsed : fallback;
}

static const char* js_dom_svg_skip_number_separators(const char* cursor) {
    while (cursor && (*cursor == ',' || str_char_is_ascii_space(*cursor))) cursor++;
    return cursor;
}

static RdtMatrix js_dom_svg_transform_from_element(DomElement* elem) {
    RdtMatrix transform = rdt_matrix_identity();
    if (!elem) return transform;
    const char* transform_attr = elem->get_attribute("transform");
    if (transform_attr && *transform_attr) {
        float components[6];
        if (svg_parse_transform(transform_attr, components)) {
            transform.e11 = components[0];
            transform.e21 = components[1];
            transform.e12 = components[2];
            transform.e22 = components[3];
            transform.e13 = components[4];
            transform.e23 = components[5];
        }
    }
    if (elem->tag_name && strcasecmp(elem->tag_name, "svg") == 0 && elem->parent) {
        RdtMatrix viewport_offset = rdt_matrix_translate(
            js_dom_svg_attribute_number(elem, "x", 0.0f),
            js_dom_svg_attribute_number(elem, "y", 0.0f));
        transform = rdt_matrix_multiply(&viewport_offset, &transform);
    }
    return transform;
}

static void js_dom_svg_bounds_from_points(const char* points, JsDomSvgBounds* bounds) {
    const char* cursor = points;
    while (cursor && *cursor) {
        cursor = js_dom_svg_skip_number_separators(cursor);
        if (!*cursor) break;
        char* end = nullptr;
        float x = strtof(cursor, &end);
        if (end == cursor) break;
        cursor = js_dom_svg_skip_number_separators(end);
        float y = strtof(cursor, &end);
        if (end == cursor) break;
        js_dom_svg_bounds_include_point(bounds, x, y);
        cursor = end;
    }
}

static int js_dom_svg_font_weight(DomElement* elem) {
    const char* attr = elem ? elem->get_attribute("font-weight") : nullptr;
    if (!attr || !*attr || strcmp(attr, "normal") == 0) return 400;
    if (strcmp(attr, "bold") == 0 || strcmp(attr, "bolder") == 0) return 700;
    if (strcmp(attr, "lighter") == 0) return 300;
    int weight = atoi(attr);
    return weight > 0 ? weight : 400;
}

static FontSlant js_dom_svg_font_slant(DomElement* elem) {
    const char* attr = elem ? elem->get_attribute("font-style") : nullptr;
    if (attr && strcmp(attr, "italic") == 0) return FONT_SLANT_ITALIC;
    if (attr && strcmp(attr, "oblique") == 0) return FONT_SLANT_OBLIQUE;
    return FONT_SLANT_NORMAL;
}

static JsDomSvgBounds js_dom_svg_bounds_for_element(DomElement* elem) {
    JsDomSvgBounds bounds = {};
    if (!elem || !elem->tag_name) return bounds;
    const char* tag = elem->tag_name;
    if (strcasecmp(tag, "rect") == 0 || strcasecmp(tag, "image") == 0 ||
        strcasecmp(tag, "foreignObject") == 0) {
        js_dom_svg_bounds_include_rect(&bounds,
            js_dom_svg_attribute_number(elem, "x", 0.0f),
            js_dom_svg_attribute_number(elem, "y", 0.0f),
            js_dom_svg_attribute_number(elem, "width", 0.0f),
            js_dom_svg_attribute_number(elem, "height", 0.0f));
    } else if (strcasecmp(tag, "circle") == 0) {
        float radius = js_dom_svg_attribute_number(elem, "r", 0.0f);
        js_dom_svg_bounds_include_rect(&bounds,
            js_dom_svg_attribute_number(elem, "cx", 0.0f) - radius,
            js_dom_svg_attribute_number(elem, "cy", 0.0f) - radius,
            radius * 2.0f, radius * 2.0f);
    } else if (strcasecmp(tag, "ellipse") == 0) {
        float rx = js_dom_svg_attribute_number(elem, "rx", 0.0f);
        float ry = js_dom_svg_attribute_number(elem, "ry", 0.0f);
        js_dom_svg_bounds_include_rect(&bounds,
            js_dom_svg_attribute_number(elem, "cx", 0.0f) - rx,
            js_dom_svg_attribute_number(elem, "cy", 0.0f) - ry,
            rx * 2.0f, ry * 2.0f);
    } else if (strcasecmp(tag, "line") == 0) {
        js_dom_svg_bounds_include_point(&bounds,
            js_dom_svg_attribute_number(elem, "x1", 0.0f),
            js_dom_svg_attribute_number(elem, "y1", 0.0f));
        js_dom_svg_bounds_include_point(&bounds,
            js_dom_svg_attribute_number(elem, "x2", 0.0f),
            js_dom_svg_attribute_number(elem, "y2", 0.0f));
    } else if (strcasecmp(tag, "polyline") == 0 || strcasecmp(tag, "polygon") == 0) {
        js_dom_svg_bounds_from_points(elem->get_attribute("points"), &bounds);
    } else if (strcasecmp(tag, "path") == 0) {
        // `getBBox()` is consumed immediately by SVG callers; preserve the
        // renderer's path parser so malformed geometry cannot turn into a
        // latent ItemError in an unrelated constructor call.
        RdtPath* path = svg_parse_path_d(elem->get_attribute("d"));
        if (path) {
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            if (rdt_path_get_bounds(path, &left, &top, &right, &bottom)) {
                js_dom_svg_bounds_include_rect(&bounds, left, top,
                                                right - left, bottom - top);
            }
            rdt_path_free(path);
        }
    } else if (strcasecmp(tag, "text") == 0 || strcasecmp(tag, "tspan") == 0) {
        StrBuf* text = strbuf_new_cap(32);
        collect_text_content((DomNode*)elem, text);
        float font_size = js_dom_svg_attribute_number(elem, "font-size",
            elem->font && elem->font->font_size > 0.0f ? elem->font->font_size : 16.0f);
        const char* family = elem->get_attribute("font-family");
        if (!family && elem->font) family = elem->font->family;
        UiContext* ui_context = elem->doc
            ? (UiContext*)elem->doc->js.host_ui_context : nullptr;
        SvgTextMetrics metrics = {};
        svg_measure_text_metrics(text && text->str ? text->str : "", font_size,
            ui_context ? ui_context->font_ctx : nullptr, family,
            js_dom_svg_font_weight(elem), js_dom_svg_font_slant(elem), &metrics);
        float x = js_dom_svg_attribute_number(elem, "x", 0.0f);
        const char* anchor = elem->get_attribute("text-anchor");
        if (anchor && strcmp(anchor, "middle") == 0) x -= metrics.width * 0.5f;
        else if (anchor && strcmp(anchor, "end") == 0) x -= metrics.width;
        float y = js_dom_svg_attribute_number(elem, "y", 0.0f);
        js_dom_svg_bounds_include_rect(&bounds, x, y - metrics.ascent,
                                        metrics.width, metrics.ascent + metrics.descent);
        if (text) strbuf_free(text);
    } else if (strcasecmp(tag, "g") == 0 || strcasecmp(tag, "svg") == 0 ||
               strcasecmp(tag, "a") == 0 || strcasecmp(tag, "switch") == 0) {
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;
            DomElement* child_elem = child->as_element();
            JsDomSvgBounds child_bounds = js_dom_svg_bounds_for_element(child_elem);
            RdtMatrix child_transform = js_dom_svg_transform_from_element(child_elem);
            js_dom_svg_bounds_apply_transform(&child_bounds, &child_transform);
            js_dom_svg_bounds_include_bounds(&bounds, &child_bounds);
        }
    }
    return bounds;
}

static Item js_dom_svg_get_bbox_for_element(DomElement* elem) {
    JsDomSvgBounds bounds = js_dom_svg_bounds_for_element(elem);
    if (!bounds.valid) return js_dom_make_rect_object(0.0f, 0.0f, 0.0f, 0.0f);
    return js_dom_make_rect_object(bounds.left, bounds.top,
                                   bounds.right - bounds.left,
                                   bounds.bottom - bounds.top);
}

static bool js_dom_svg_parse_viewbox(const char* value, float* min_x, float* min_y,
                                     float* width, float* height) {
    if (!value || !min_x || !min_y || !width || !height) return false;
    float values[4] = {};
    const char* cursor = value;
    for (int i = 0; i < 4; i++) {
        cursor = js_dom_svg_skip_number_separators(cursor);
        char* end = nullptr;
        values[i] = strtof(cursor, &end);
        if (end == cursor) return false;
        cursor = end;
    }
    if (values[2] <= 0.0f || values[3] <= 0.0f) return false;
    *min_x = values[0];
    *min_y = values[1];
    *width = values[2];
    *height = values[3];
    return true;
}

static RdtMatrix js_dom_svg_viewbox_transform(DomElement* elem) {
    RdtMatrix transform = rdt_matrix_identity();
    if (!elem) return transform;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float viewbox_width = 0.0f;
    float viewbox_height = 0.0f;
    const char* viewbox = elem->get_attribute("viewBox");
    if (!viewbox) viewbox = elem->get_attribute("viewbox");
    if (!js_dom_svg_parse_viewbox(viewbox, &min_x, &min_y, &viewbox_width, &viewbox_height)) {
        return transform;
    }
    float viewport_width = js_dom_svg_attribute_number(elem, "width", 0.0f);
    float viewport_height = js_dom_svg_attribute_number(elem, "height", 0.0f);
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
        // hit-testing already has committed CSS geometry; flushing through the
        // generic DOM path can rebuild this element before the second axis.
        if (viewport_width <= 0.0f) viewport_width = elem->width;
        if (viewport_height <= 0.0f) viewport_height = elem->height;
    }
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
        if (viewport_width <= 0.0f) {
            viewport_width = (float)js_dom_geometry_dimension(elem, true);
        }
        if (viewport_height <= 0.0f) {
            viewport_height = (float)js_dom_geometry_dimension(elem, false);
        }
    }
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) return transform;
    float scale_x = viewport_width / viewbox_width;
    float scale_y = viewport_height / viewbox_height;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    const char* preserve = elem->get_attribute("preserveAspectRatio");
    if (!preserve || strstr(preserve, "none") == nullptr) {
        float scale = LMB_MIN(scale_x, scale_y);
        offset_x = (viewport_width - viewbox_width * scale) * 0.5f;
        offset_y = (viewport_height - viewbox_height * scale) * 0.5f;
        scale_x = scale;
        scale_y = scale;
    }
    transform.e11 = scale_x;
    transform.e22 = scale_y;
    transform.e13 = offset_x - min_x * scale_x;
    transform.e23 = offset_y - min_y * scale_y;
    return transform;
}

static RdtMatrix js_dom_svg_ctm(DomElement* elem, bool screen_space) {
    RdtMatrix matrix = rdt_matrix_identity();
    if (!elem) return matrix;
    DomElement* chain[64];
    int count = 0;
    DomElement* outermost_svg = nullptr;
    for (DomNode* current = (DomNode*)elem; current && count < 64;
         current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* candidate = current->as_element();
        chain[count++] = candidate;
        if (candidate && candidate->tag_name && strcasecmp(candidate->tag_name, "svg") == 0) {
            outermost_svg = candidate;
        }
    }
    bool in_svg = false;
    for (int i = count - 1; i >= 0; i--) {
        DomElement* current = chain[i];
        if (!current || !current->tag_name) continue;
        if (strcasecmp(current->tag_name, "svg") == 0) {
            in_svg = true;
            RdtMatrix viewbox_transform = js_dom_svg_viewbox_transform(current);
            matrix = rdt_matrix_multiply(&matrix, &viewbox_transform);
        }
        if (in_svg) {
            RdtMatrix local_transform = js_dom_svg_transform_from_element(current);
            matrix = rdt_matrix_multiply(&matrix, &local_transform);
        }
    }
    if (screen_space && outermost_svg) {
        float x = 0.0f;
        float y = 0.0f;
        js_dom_viewport_node_position((DomNode*)outermost_svg, &x, &y);
        RdtMatrix layout_transform = rdt_matrix_translate(x, y);
        matrix = rdt_matrix_multiply(&layout_transform, &matrix);
    }
    return matrix;
}

static bool js_dom_svg_matrix_unproject_point(const RdtMatrix* matrix,
                                              float x, float y,
                                              float* local_x,
                                              float* local_y) {
    if (!matrix || !local_x || !local_y) return false;
    float determinant = matrix->e11 * matrix->e22 - matrix->e21 * matrix->e12;
    if (fabsf(determinant) < 0.000001f) return false;
    float dx = x - matrix->e13;
    float dy = y - matrix->e23;
    *local_x = (matrix->e22 * dx - matrix->e12 * dy) / determinant;
    *local_y = (-matrix->e21 * dx + matrix->e11 * dy) / determinant;
    return true;
}

static const float JS_DOM_SVG_STROKE_HIT_AIM_SLOP_PX = 3.0f;
static const int JS_DOM_SVG_PATH_HIT_MAX_CUBIC_DEPTH = 10;

typedef struct JsDomSvgPathHitContext {
    float point_x;
    float point_y;
    float curve_flatness;
    float stroke_radius;
    float stroke_dash[32];
    float stroke_dash_total;
    float stroke_dash_offset;
    float subpath_stroke_length;
    float first_stroke_start_x;
    float first_stroke_start_y;
    float first_stroke_end_x;
    float first_stroke_end_y;
    float last_stroke_start_x;
    float last_stroke_start_y;
    float last_stroke_end_x;
    float last_stroke_end_y;
    int stroke_dash_count;
    RdtStrokeCap stroke_cap;
    RdtStrokeJoin stroke_join;
    float stroke_miter_limit;
    int fill_winding;
    int fill_crossings;
    bool fill_on_edge;
    bool subpath_fill_on_edge;
    bool has_current;
    bool subpath_has_draw;
    bool subpath_closed;
    bool has_first_stroke_segment;
    bool has_last_stroke_segment;
    bool stroke_enabled;
    bool stroke_hit;
    float subpath_area_twice;
    float current_x;
    float current_y;
    float subpath_start_x;
    float subpath_start_y;
} JsDomSvgPathHitContext;

static float js_dom_svg_point_segment_distance_sq(float point_x, float point_y,
                                                  float start_x, float start_y,
                                                  float end_x, float end_y) {
    float dx = end_x - start_x;
    float dy = end_y - start_y;
    float length_sq = dx * dx + dy * dy;
    if (length_sq <= 0.000001f) {
        float px = point_x - start_x;
        float py = point_y - start_y;
        return px * px + py * py;
    }
    float projection = ((point_x - start_x) * dx + (point_y - start_y) * dy) /
        length_sq;
    if (projection < 0.0f) projection = 0.0f;
    if (projection > 1.0f) projection = 1.0f;
    float closest_x = start_x + dx * projection;
    float closest_y = start_y + dy * projection;
    float px = point_x - closest_x;
    float py = point_y - closest_y;
    return px * px + py * py;
}

static bool js_dom_svg_point_in_triangle(float point_x, float point_y,
                                         float ax, float ay, float bx, float by,
                                         float cx, float cy) {
    float ab = (point_x - ax) * (by - ay) - (point_y - ay) * (bx - ax);
    float bc = (point_x - bx) * (cy - by) - (point_y - by) * (cx - bx);
    float ca = (point_x - cx) * (ay - cy) - (point_y - cy) * (ax - cx);
    return (ab >= 0.0f && bc >= 0.0f && ca >= 0.0f) ||
        (ab <= 0.0f && bc <= 0.0f && ca <= 0.0f);
}

static bool js_dom_svg_stroke_segment_contains(const JsDomSvgPathHitContext* context,
                                                float start_x, float start_y,
                                                float end_x, float end_y,
                                                bool start_cap, bool end_cap) {
    if (!context) return false;
    float dx = end_x - start_x;
    float dy = end_y - start_y;
    float length = hypotf(dx, dy);
    if (length <= 0.000001f) {
        float px = context->point_x - start_x;
        float py = context->point_y - start_y;
        return px * px + py * py <= context->stroke_radius * context->stroke_radius;
    }
    float ux = dx / length;
    float uy = dy / length;
    float point_dx = context->point_x - start_x;
    float point_dy = context->point_y - start_y;
    float along = point_dx * ux + point_dy * uy;
    float across = point_dx * -uy + point_dy * ux;
    if (fabsf(across) > context->stroke_radius) return false;
    float min_along = 0.0f;
    float max_along = length;
    if (context->stroke_cap == RDT_CAP_SQUARE) {
        if (start_cap) min_along -= context->stroke_radius;
        if (end_cap) max_along += context->stroke_radius;
    }
    if (along >= min_along && along <= max_along) return true;
    if (context->stroke_cap != RDT_CAP_ROUND) return false;
    if (start_cap && along < 0.0f) {
        return point_dx * point_dx + point_dy * point_dy <=
            context->stroke_radius * context->stroke_radius;
    }
    if (end_cap && along > length) {
        float end_dx = context->point_x - end_x;
        float end_dy = context->point_y - end_y;
        return end_dx * end_dx + end_dy * end_dy <=
            context->stroke_radius * context->stroke_radius;
    }
    return false;
}

static bool js_dom_svg_dash_is_on_at(const JsDomSvgPathHitContext* context,
                                     float distance, float* out_remaining) {
    if (!context || context->stroke_dash_count <= 0 || context->stroke_dash_total <= 0.0f) {
        if (out_remaining) *out_remaining = 1.0e30f;
        return true;
    }
    float position = fmodf(distance + context->stroke_dash_offset,
        context->stroke_dash_total);
    if (position < 0.0f) position += context->stroke_dash_total;
    for (int index = 0; index < context->stroke_dash_count; index++) {
        float length = context->stroke_dash[index];
        if (position < length || index == context->stroke_dash_count - 1) {
            if (out_remaining) *out_remaining = LMB_MAX(length - position, 0.0f);
            return (index & 1) == 0;
        }
        position -= length;
    }
    if (out_remaining) *out_remaining = 0.0f;
    return false;
}

static void js_dom_svg_path_hit_add_join(JsDomSvgPathHitContext* context,
                                         float prev_start_x, float prev_start_y,
                                         float vertex_x, float vertex_y,
                                         float next_end_x, float next_end_y) {
    if (!context || context->stroke_hit) return;
    float prev_dx = vertex_x - prev_start_x;
    float prev_dy = vertex_y - prev_start_y;
    float next_dx = next_end_x - vertex_x;
    float next_dy = next_end_y - vertex_y;
    float prev_length = hypotf(prev_dx, prev_dy);
    float next_length = hypotf(next_dx, next_dy);
    if (prev_length <= 0.000001f || next_length <= 0.000001f) return;
    float vertex_dx = context->point_x - vertex_x;
    float vertex_dy = context->point_y - vertex_y;
    // Flattened closed curves share this vertex between two butt strips. Keep
    // the shared centerline covered so an epsilon at move/close cannot create
    // a false gap in an otherwise continuous stroke.
    if (vertex_dx * vertex_dx + vertex_dy * vertex_dy <=
        context->stroke_radius * context->stroke_radius) {
        context->stroke_hit = true;
        return;
    }
    float prev_x = prev_dx / prev_length;
    float prev_y = prev_dy / prev_length;
    float next_x = next_dx / next_length;
    float next_y = next_dy / next_length;
    float cross = prev_x * next_y - prev_y * next_x;
    if (fabsf(cross) <= 0.000001f) return;
    float outward_sign = cross > 0.0f ? -1.0f : 1.0f;
    float prev_normal_x = -prev_y * outward_sign;
    float prev_normal_y = prev_x * outward_sign;
    float next_normal_x = -next_y * outward_sign;
    float next_normal_y = next_x * outward_sign;
    float outer_prev_x = vertex_x + prev_normal_x * context->stroke_radius;
    float outer_prev_y = vertex_y + prev_normal_y * context->stroke_radius;
    float outer_next_x = vertex_x + next_normal_x * context->stroke_radius;
    float outer_next_y = vertex_y + next_normal_y * context->stroke_radius;
    if (context->stroke_join == RDT_JOIN_ROUND) {
        float point_dx = context->point_x - vertex_x;
        float point_dy = context->point_y - vertex_y;
        context->stroke_hit = point_dx * point_dx + point_dy * point_dy <=
            context->stroke_radius * context->stroke_radius;
        return;
    }
    if (context->stroke_join == RDT_JOIN_BEVEL) {
        context->stroke_hit = js_dom_svg_point_in_triangle(context->point_x, context->point_y,
            vertex_x, vertex_y, outer_prev_x, outer_prev_y, outer_next_x, outer_next_y);
        return;
    }
    float determinant = prev_x * -next_y - prev_y * -next_x;
    if (fabsf(determinant) <= 0.000001f) return;
    float delta_x = outer_next_x - outer_prev_x;
    float delta_y = outer_next_y - outer_prev_y;
    float prev_distance = (delta_x * -next_y - delta_y * -next_x) / determinant;
    float miter_x = outer_prev_x + prev_x * prev_distance;
    float miter_y = outer_prev_y + prev_y * prev_distance;
    float miter_length = hypotf(miter_x - vertex_x, miter_y - vertex_y);
    if (miter_length > context->stroke_miter_limit * context->stroke_radius) {
        context->stroke_hit = js_dom_svg_point_in_triangle(context->point_x, context->point_y,
            vertex_x, vertex_y, outer_prev_x, outer_prev_y, outer_next_x, outer_next_y);
        return;
    }
    context->stroke_hit = js_dom_svg_point_in_triangle(context->point_x, context->point_y,
        outer_prev_x, outer_prev_y, outer_next_x, outer_next_y, miter_x, miter_y);
}

static void js_dom_svg_path_hit_add_stroke_segment(JsDomSvgPathHitContext* context,
                                                    float start_x, float start_y,
                                                    float end_x, float end_y) {
    if (!context || !context->stroke_enabled) return;
    if (context->stroke_dash_count == 0 && context->has_last_stroke_segment) {
        js_dom_svg_path_hit_add_join(context, context->last_stroke_start_x,
            context->last_stroke_start_y, start_x, start_y, end_x, end_y);
    }
    float dx = end_x - start_x;
    float dy = end_y - start_y;
    float length = hypotf(dx, dy);
    if (length <= 0.000001f) return;
    if (!context->has_first_stroke_segment) {
        context->first_stroke_start_x = start_x;
        context->first_stroke_start_y = start_y;
        context->first_stroke_end_x = end_x;
        context->first_stroke_end_y = end_y;
        context->has_first_stroke_segment = true;
    }
    if (context->stroke_dash_count <= 0) {
        if (js_dom_svg_stroke_segment_contains(context, start_x, start_y,
                end_x, end_y, false, false)) {
            context->stroke_hit = true;
        }
    } else {
        float segment_offset = 0.0f;
        while (segment_offset < length) {
            float remaining = 0.0f;
            float path_offset = context->subpath_stroke_length + segment_offset;
            bool dash_on = js_dom_svg_dash_is_on_at(context, path_offset, &remaining);
            if (remaining <= 0.000001f) remaining = length - segment_offset;
            float fragment_length = LMB_MIN(remaining, length - segment_offset);
            if (dash_on) {
                float start_fraction = segment_offset / length;
                float end_fraction = (segment_offset + fragment_length) / length;
                float fragment_start_x = start_x + dx * start_fraction;
                float fragment_start_y = start_y + dy * start_fraction;
                float fragment_end_x = start_x + dx * end_fraction;
                float fragment_end_y = start_y + dy * end_fraction;
                bool start_cap = path_offset <= 0.000001f ||
                    !js_dom_svg_dash_is_on_at(context, path_offset - 0.0001f, nullptr);
                bool end_cap = !js_dom_svg_dash_is_on_at(context,
                    path_offset + fragment_length + 0.0001f, nullptr);
                if (js_dom_svg_stroke_segment_contains(context, fragment_start_x, fragment_start_y,
                        fragment_end_x, fragment_end_y, start_cap, end_cap)) {
                    context->stroke_hit = true;
                }
            }
            segment_offset += fragment_length;
        }
    }
    context->subpath_stroke_length += length;
    context->last_stroke_start_x = start_x;
    context->last_stroke_start_y = start_y;
    context->last_stroke_end_x = end_x;
    context->last_stroke_end_y = end_y;
    context->has_last_stroke_segment = true;
}

static void js_dom_svg_path_hit_add_fill_edge(JsDomSvgPathHitContext* context,
                                              float start_x, float start_y,
                                              float end_x, float end_y) {
    if (!context || fabsf(end_y - start_y) <= 0.000001f) return;
    bool upward = start_y <= context->point_y && end_y > context->point_y;
    bool downward = start_y > context->point_y && end_y <= context->point_y;
    if (!upward && !downward) return;
    float side = (end_x - start_x) * (context->point_y - start_y) -
        (context->point_x - start_x) * (end_y - start_y);
    if (fabsf(side) <= 0.000001f) {
        context->subpath_fill_on_edge = true;
        return;
    }
    if (upward && side > 0.0f) {
        context->fill_winding++;
        context->fill_crossings++;
    } else if (downward && side < 0.0f) {
        context->fill_winding--;
        context->fill_crossings++;
    }
}

static void js_dom_svg_path_hit_add_segment(JsDomSvgPathHitContext* context,
                                            float start_x, float start_y,
                                            float end_x, float end_y,
                                            bool include_stroke) {
    if (!context) return;
    context->subpath_area_twice += start_x * end_y - end_x * start_y;
    if (include_stroke) {
        js_dom_svg_path_hit_add_stroke_segment(context, start_x, start_y, end_x, end_y);
    }
    js_dom_svg_path_hit_add_fill_edge(context, start_x, start_y, end_x, end_y);
}

static void js_dom_svg_path_hit_commit_subpath_fill_edge(JsDomSvgPathHitContext* context) {
    if (!context) return;
    if (fabsf(context->subpath_area_twice) > 0.000001f &&
        context->subpath_fill_on_edge) {
        context->fill_on_edge = true;
    }
}

static void js_dom_svg_path_hit_finish_subpath(JsDomSvgPathHitContext* context) {
    if (!context || !context->has_current || !context->subpath_has_draw) return;
    if (!context->subpath_closed) {
        // SVG fill implicitly closes an open subpath, while its stroke does not.
        js_dom_svg_path_hit_add_segment(context, context->current_x, context->current_y,
                                        context->subpath_start_x, context->subpath_start_y,
                                        false);
        // A zero-area contour, such as <line>, has no fill boundary to pick.
        js_dom_svg_path_hit_commit_subpath_fill_edge(context);
    }
    if (!context->subpath_closed && context->has_first_stroke_segment &&
        context->stroke_dash_count == 0) {
        if (js_dom_svg_stroke_segment_contains(context, context->first_stroke_start_x,
                context->first_stroke_start_y, context->first_stroke_end_x,
                context->first_stroke_end_y, true, false) ||
            js_dom_svg_stroke_segment_contains(context, context->last_stroke_start_x,
                context->last_stroke_start_y, context->last_stroke_end_x,
                context->last_stroke_end_y, false, true)) {
            context->stroke_hit = true;
        }
    }
}

static void js_dom_svg_path_hit_flatten_cubic(JsDomSvgPathHitContext* context,
                                              float start_x, float start_y,
                                              float control1_x, float control1_y,
                                              float control2_x, float control2_y,
                                              float end_x, float end_y,
                                              int depth) {
    if (!context) return;
    float flatness_sq = context->curve_flatness * context->curve_flatness;
    float control1_distance_sq = js_dom_svg_point_segment_distance_sq(
        control1_x, control1_y, start_x, start_y, end_x, end_y);
    float control2_distance_sq = js_dom_svg_point_segment_distance_sq(
        control2_x, control2_y, start_x, start_y, end_x, end_y);
    if (depth >= JS_DOM_SVG_PATH_HIT_MAX_CUBIC_DEPTH ||
        (control1_distance_sq <= flatness_sq && control2_distance_sq <= flatness_sq)) {
        js_dom_svg_path_hit_add_segment(context, start_x, start_y, end_x, end_y, true);
        return;
    }
    float start_control1_x = (start_x + control1_x) * 0.5f;
    float start_control1_y = (start_y + control1_y) * 0.5f;
    float controls_x = (control1_x + control2_x) * 0.5f;
    float controls_y = (control1_y + control2_y) * 0.5f;
    float control2_end_x = (control2_x + end_x) * 0.5f;
    float control2_end_y = (control2_y + end_y) * 0.5f;
    float middle_control1_x = (start_control1_x + controls_x) * 0.5f;
    float middle_control1_y = (start_control1_y + controls_y) * 0.5f;
    float middle_control2_x = (controls_x + control2_end_x) * 0.5f;
    float middle_control2_y = (controls_y + control2_end_y) * 0.5f;
    float middle_x = (middle_control1_x + middle_control2_x) * 0.5f;
    float middle_y = (middle_control1_y + middle_control2_y) * 0.5f;
    js_dom_svg_path_hit_flatten_cubic(context, start_x, start_y,
        start_control1_x, start_control1_y, middle_control1_x, middle_control1_y,
        middle_x, middle_y, depth + 1);
    js_dom_svg_path_hit_flatten_cubic(context, middle_x, middle_y,
        middle_control2_x, middle_control2_y, control2_end_x, control2_end_y,
        end_x, end_y, depth + 1);
}

static void js_dom_svg_path_add_ellipse(RdtPath* path, float cx, float cy,
                                        float rx, float ry) {
    if (!path || rx <= 0.0f || ry <= 0.0f) return;
    const float kappa = 0.5522847498307936f;
    rdt_path_move_to(path, cx + rx, cy);
    rdt_path_cubic_to(path, cx + rx, cy + kappa * ry,
        cx + kappa * rx, cy + ry, cx, cy + ry);
    rdt_path_cubic_to(path, cx - kappa * rx, cy + ry,
        cx - rx, cy + kappa * ry, cx - rx, cy);
    rdt_path_cubic_to(path, cx - rx, cy - kappa * ry,
        cx - kappa * rx, cy - ry, cx, cy - ry);
    rdt_path_cubic_to(path, cx + kappa * rx, cy - ry,
        cx + rx, cy - kappa * ry, cx + rx, cy);
    rdt_path_close(path);
}

static void js_dom_svg_path_add_rect(RdtPath* path, float x, float y,
                                     float width, float height,
                                     float rx, float ry) {
    if (!path || width <= 0.0f || height <= 0.0f) return;
    rx = LMB_MIN(fabsf(rx), width * 0.5f);
    ry = LMB_MIN(fabsf(ry), height * 0.5f);
    if (rx <= 0.0f || ry <= 0.0f) {
        rdt_path_move_to(path, x, y);
        rdt_path_line_to(path, x + width, y);
        rdt_path_line_to(path, x + width, y + height);
        rdt_path_line_to(path, x, y + height);
        rdt_path_close(path);
        return;
    }
    const float kappa = 0.5522847498307936f;
    float right = x + width;
    float bottom = y + height;
    rdt_path_move_to(path, x + rx, y);
    rdt_path_line_to(path, right - rx, y);
    rdt_path_cubic_to(path, right - rx + kappa * rx, y,
        right, y + ry - kappa * ry, right, y + ry);
    rdt_path_line_to(path, right, bottom - ry);
    rdt_path_cubic_to(path, right, bottom - ry + kappa * ry,
        right - rx + kappa * rx, bottom, right - rx, bottom);
    rdt_path_line_to(path, x + rx, bottom);
    rdt_path_cubic_to(path, x + rx - kappa * rx, bottom,
        x, bottom - ry + kappa * ry, x, bottom - ry);
    rdt_path_line_to(path, x, y + ry);
    rdt_path_cubic_to(path, x, y + ry - kappa * ry,
        x + rx - kappa * rx, y, x + rx, y);
    rdt_path_close(path);
}

static bool js_dom_svg_path_add_points(RdtPath* path, const char* points,
                                       bool close_path) {
    if (!path || !points) return false;
    bool has_point = false;
    const char* cursor = points;
    while (cursor && *cursor) {
        cursor = js_dom_svg_skip_number_separators(cursor);
        if (!*cursor) break;
        char* end = nullptr;
        float x = strtof(cursor, &end);
        if (end == cursor) break;
        cursor = js_dom_svg_skip_number_separators(end);
        float y = strtof(cursor, &end);
        if (end == cursor) break;
        if (has_point) rdt_path_line_to(path, x, y);
        else rdt_path_move_to(path, x, y);
        has_point = true;
        cursor = end;
    }
    if (has_point && close_path) rdt_path_close(path);
    return has_point;
}

static bool js_dom_svg_is_basic_shape(DomElement* elem) {
    if (!elem || !elem->tag_name) return false;
    const char* tag = elem->tag_name;
    return strcasecmp(tag, "rect") == 0 || strcasecmp(tag, "circle") == 0 ||
        strcasecmp(tag, "ellipse") == 0 || strcasecmp(tag, "line") == 0 ||
        strcasecmp(tag, "polyline") == 0 || strcasecmp(tag, "polygon") == 0 ||
        strcasecmp(tag, "path") == 0;
}

static RdtPath* js_dom_svg_basic_shape_path(DomElement* elem) {
    if (!js_dom_svg_is_basic_shape(elem)) return nullptr;
    const char* tag = elem->tag_name;
    if (strcasecmp(tag, "path") == 0) {
        const char* d = elem->get_attribute("d");
        return d && *d ? svg_parse_path_d(d) : nullptr;
    }
    RdtPath* path = rdt_path_new();
    if (!path) return nullptr;
    bool valid = false;
    if (strcasecmp(tag, "rect") == 0) {
        float width = js_dom_svg_attribute_number(elem, "width", 0.0f);
        float height = js_dom_svg_attribute_number(elem, "height", 0.0f);
        const char* rx_attr = elem->get_attribute("rx");
        const char* ry_attr = elem->get_attribute("ry");
        float rx = rx_attr ? js_dom_svg_attribute_number(elem, "rx", 0.0f)
            : (ry_attr ? js_dom_svg_attribute_number(elem, "ry", 0.0f) : 0.0f);
        float ry = ry_attr ? js_dom_svg_attribute_number(elem, "ry", 0.0f) : rx;
        if (width > 0.0f && height > 0.0f) {
            js_dom_svg_path_add_rect(path,
                js_dom_svg_attribute_number(elem, "x", 0.0f),
                js_dom_svg_attribute_number(elem, "y", 0.0f), width, height, rx, ry);
            valid = true;
        }
    } else if (strcasecmp(tag, "circle") == 0) {
        float radius = js_dom_svg_attribute_number(elem, "r", 0.0f);
        if (radius > 0.0f) {
            js_dom_svg_path_add_ellipse(path,
                js_dom_svg_attribute_number(elem, "cx", 0.0f),
                js_dom_svg_attribute_number(elem, "cy", 0.0f), radius, radius);
            valid = true;
        }
    } else if (strcasecmp(tag, "ellipse") == 0) {
        float rx = js_dom_svg_attribute_number(elem, "rx", 0.0f);
        float ry = js_dom_svg_attribute_number(elem, "ry", 0.0f);
        if (rx > 0.0f && ry > 0.0f) {
            js_dom_svg_path_add_ellipse(path,
                js_dom_svg_attribute_number(elem, "cx", 0.0f),
                js_dom_svg_attribute_number(elem, "cy", 0.0f), rx, ry);
            valid = true;
        }
    } else if (strcasecmp(tag, "line") == 0) {
        rdt_path_move_to(path, js_dom_svg_attribute_number(elem, "x1", 0.0f),
                         js_dom_svg_attribute_number(elem, "y1", 0.0f));
        rdt_path_line_to(path, js_dom_svg_attribute_number(elem, "x2", 0.0f),
                         js_dom_svg_attribute_number(elem, "y2", 0.0f));
        valid = true;
    } else {
        valid = js_dom_svg_path_add_points(path, elem->get_attribute("points"),
            strcasecmp(tag, "polygon") == 0);
    }
    if (!valid) {
        rdt_path_free(path);
        return nullptr;
    }
    return path;
}

static bool js_dom_svg_path_hit_visit(void* userdata, RdtPathCommand command,
                                      const float* args, int arg_count) {
    JsDomSvgPathHitContext* context = (JsDomSvgPathHitContext*)userdata;
    if (!context) return false;
    switch (command) {
    case RDT_PATH_MOVE:
        if (arg_count < 2) return false;
        js_dom_svg_path_hit_finish_subpath(context);
        context->current_x = args[0];
        context->current_y = args[1];
        context->subpath_start_x = args[0];
        context->subpath_start_y = args[1];
        context->has_current = true;
        context->subpath_has_draw = false;
        context->subpath_closed = false;
        context->subpath_stroke_length = 0.0f;
        context->has_first_stroke_segment = false;
        context->has_last_stroke_segment = false;
        context->subpath_area_twice = 0.0f;
        context->subpath_fill_on_edge = false;
        return true;
    case RDT_PATH_LINE:
        if (arg_count < 2 || !context->has_current) return false;
        js_dom_svg_path_hit_add_segment(context, context->current_x, context->current_y,
                                        args[0], args[1], true);
        context->current_x = args[0];
        context->current_y = args[1];
        context->subpath_has_draw = true;
        context->subpath_closed = false;
        return true;
    case RDT_PATH_QUAD:
        if (arg_count < 4 || !context->has_current) return false;
        js_dom_svg_path_hit_flatten_cubic(context, context->current_x, context->current_y,
            context->current_x + (args[0] - context->current_x) * (2.0f / 3.0f),
            context->current_y + (args[1] - context->current_y) * (2.0f / 3.0f),
            args[2] + (args[0] - args[2]) * (2.0f / 3.0f),
            args[3] + (args[1] - args[3]) * (2.0f / 3.0f),
            args[2], args[3], 0);
        context->current_x = args[2];
        context->current_y = args[3];
        context->subpath_has_draw = true;
        context->subpath_closed = false;
        return true;
    case RDT_PATH_CUBIC:
        if (arg_count < 6 || !context->has_current) return false;
        js_dom_svg_path_hit_flatten_cubic(context, context->current_x, context->current_y,
            args[0], args[1], args[2], args[3], args[4], args[5], 0);
        context->current_x = args[4];
        context->current_y = args[5];
        context->subpath_has_draw = true;
        context->subpath_closed = false;
        return true;
    case RDT_PATH_CLOSE:
        if (!context->has_current || !context->subpath_has_draw) return true;
        js_dom_svg_path_hit_add_segment(context, context->current_x, context->current_y,
                                        context->subpath_start_x, context->subpath_start_y,
                                        true);
        if (context->stroke_dash_count == 0 && context->has_first_stroke_segment &&
            context->has_last_stroke_segment) {
            js_dom_svg_path_hit_add_join(context, context->last_stroke_start_x,
                context->last_stroke_start_y, context->subpath_start_x,
                context->subpath_start_y, context->first_stroke_end_x,
                context->first_stroke_end_y);
        }
        context->current_x = context->subpath_start_x;
        context->current_y = context->subpath_start_y;
        context->subpath_closed = true;
        js_dom_svg_path_hit_commit_subpath_fill_edge(context);
        return true;
    case RDT_PATH_RECT:
    case RDT_PATH_CIRCLE:
        // SVG path data is lowered to move/line/cubic commands before this
        // visitor. Other backend-only path primitives have no SVG `d` source.
        return false;
    }
    return false;
}

static const char* js_dom_svg_cascaded_property_value(DomElement* elem,
                                                       const char* name,
                                                       char* buffer,
                                                       size_t buffer_size) {
    if (!elem || !name || !buffer || buffer_size == 0 || !elem->doc) return nullptr;
    // This selector matcher returns only real stylesheet declarations.  The
    // resolved style tree also contains SVG initial values (notably
    // stroke:none), which must not mask a presentation attribute.
    CssDeclaration* declaration = js_match_custom_property(elem, name);
    if (!declaration || (!declaration->value &&
            (!declaration->value_text || declaration->value_text_len == 0))) return nullptr;
    const char* serialized = css_serialize_declaration_value(declaration,
        elem->doc->document_pool);
    if (!serialized || !*serialized) return nullptr;
    size_t length = strlen(serialized);
    if (length >= buffer_size) length = buffer_size - 1;
    memcpy(buffer, serialized, length);
    buffer[length] = '\0';
    return buffer;
}

static const char* js_dom_svg_presentation_value(DomElement* elem, const char* name,
                                                  bool inherits, char* buffer,
                                                  size_t buffer_size) {
    if (!elem || !name || !buffer || buffer_size == 0) return nullptr;
    for (DomNode* node = (DomNode*)elem; node && node->is_element();
         node = node->parent) {
        DomElement* current = node->as_element();
        const char* inline_value = svg_get_inline_style_property(
            current->get_attribute("style"), name, buffer, buffer_size);
        if (inline_value) return inline_value;
        const char* cascaded_value = js_dom_svg_cascaded_property_value(current, name,
            buffer, buffer_size);
        if (cascaded_value) return cascaded_value;
        const char* attribute = current->get_attribute(name);
        if (attribute) return attribute;
        if (!inherits) break;
    }
    return nullptr;
}

static float js_dom_svg_presentation_number(DomElement* elem, const char* name,
                                            float fallback, bool inherits) {
    char value_buffer[64] = {};
    const char* value = js_dom_svg_presentation_value(elem, name, inherits,
        value_buffer, sizeof(value_buffer));
    if (!value) return fallback;
    char* end = nullptr;
    float parsed = strtof(value, &end);
    return end != value ? parsed : fallback;
}

static bool js_dom_svg_paint_is_present(DomElement* elem, const char* paint_name,
                                        bool paint_is_present_by_default) {
    if (!elem || !paint_name) return false;
    char paint_buffer[256] = {};
    const char* paint = js_dom_svg_presentation_value(elem, paint_name, true,
        paint_buffer, sizeof(paint_buffer));
    if (paint && (!*paint || strcasecmp(paint, "none") == 0)) return false;
    return paint || paint_is_present_by_default;
}

typedef struct JsDomSvgShapeHit {
    bool fill;
    bool stroke;
    bool bounding_box;
} JsDomSvgShapeHit;

static void js_dom_svg_configure_stroke_hit(DomElement* elem, float min_scale,
                                            JsDomSvgPathHitContext* context) {
    if (!context) return;
    float stroke_width = js_dom_svg_presentation_number(elem, "stroke-width", 1.0f, true);
    context->stroke_enabled = stroke_width > 0.0f;
    if (!context->stroke_enabled) return;
    context->stroke_radius = stroke_width * 0.5f + JS_DOM_SVG_STROKE_HIT_AIM_SLOP_PX / min_scale;
    context->stroke_cap = RDT_CAP_BUTT;
    context->stroke_join = RDT_JOIN_MITER;
    context->stroke_miter_limit = 4.0f;
    char cap_buffer[64] = {};
    char join_buffer[64] = {};
    char dash_buffer[256] = {};
    const char* cap = js_dom_svg_presentation_value(elem, "stroke-linecap", true,
        cap_buffer, sizeof(cap_buffer));
    const char* join = js_dom_svg_presentation_value(elem, "stroke-linejoin", true,
        join_buffer, sizeof(join_buffer));
    if (cap && strcasecmp(cap, "round") == 0) context->stroke_cap = RDT_CAP_ROUND;
    else if (cap && strcasecmp(cap, "square") == 0) context->stroke_cap = RDT_CAP_SQUARE;
    if (join && strcasecmp(join, "round") == 0) context->stroke_join = RDT_JOIN_ROUND;
    else if (join && strcasecmp(join, "bevel") == 0) context->stroke_join = RDT_JOIN_BEVEL;
    context->stroke_miter_limit = js_dom_svg_presentation_number(elem,
        "stroke-miterlimit", 4.0f, true);
    if (context->stroke_miter_limit < 1.0f) context->stroke_miter_limit = 1.0f;
    context->stroke_dash_offset = js_dom_svg_presentation_number(elem,
        "stroke-dashoffset", 0.0f, true);
    const char* dasharray = js_dom_svg_presentation_value(elem, "stroke-dasharray", true,
        dash_buffer, sizeof(dash_buffer));
    if (!dasharray || !*dasharray || strcasecmp(dasharray, "none") == 0) return;
    const char* cursor = dasharray;
    while (*cursor && context->stroke_dash_count < 16) {
        cursor = js_dom_svg_skip_number_separators(cursor);
        if (!*cursor) break;
        char* end = nullptr;
        float dash = strtof(cursor, &end);
        if (end == cursor) break;
        if (dash > 0.0f) {
            context->stroke_dash[context->stroke_dash_count++] = dash;
        }
        cursor = end;
    }
    if (context->stroke_dash_count & 1) {
        int original_count = context->stroke_dash_count;
        for (int index = 0; index < original_count && context->stroke_dash_count < 32; index++) {
            context->stroke_dash[context->stroke_dash_count++] = context->stroke_dash[index];
        }
    }
    for (int index = 0; index < context->stroke_dash_count; index++) {
        context->stroke_dash_total += context->stroke_dash[index];
    }
    if (context->stroke_dash_total <= 0.0f) context->stroke_dash_count = 0;
}

static JsDomSvgShapeHit js_dom_svg_basic_shape_hit_local_point(DomElement* elem,
                                                               float local_x,
                                                               float local_y,
                                                               float min_scale) {
    JsDomSvgShapeHit result = {};
    if (!elem) return result;
    // Every basic SVG primitive is lowered to the same contour visitor.  Its
    // bounding box contains non-painted gaps, so it cannot decide a hit.
    RdtPath* path = js_dom_svg_basic_shape_path(elem);
    if (!path) return result;
    JsDomSvgPathHitContext context = {};
    context.point_x = local_x;
    context.point_y = local_y;
    context.curve_flatness = 0.5f / min_scale;
    js_dom_svg_configure_stroke_hit(elem, min_scale, &context);
    bool visited = rdt_path_visit(path, js_dom_svg_path_hit_visit, &context);
    if (visited) js_dom_svg_path_hit_finish_subpath(&context);
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    result.bounding_box = rdt_path_get_bounds(path, &left, &top, &right, &bottom) &&
        local_x >= left && local_x <= right && local_y >= top && local_y <= bottom;
    rdt_path_free(path);
    if (!visited) return result;
    {
        char fill_rule_buffer[64] = {};
        const char* fill_rule = js_dom_svg_presentation_value(elem, "fill-rule", true,
            fill_rule_buffer, sizeof(fill_rule_buffer));
        bool even_odd = fill_rule && strcasecmp(fill_rule, "evenodd") == 0;
        if (context.fill_on_edge || (even_odd
                ? (context.fill_crossings & 1) != 0
                : context.fill_winding != 0)) {
            result.fill = true;
        }
    }
    result.stroke = context.stroke_enabled && context.stroke_hit;
    return result;
}

static JsDomSvgShapeHit js_dom_svg_basic_shape_hit_viewport_point(DomElement* elem,
                                                                  float viewport_x,
                                                                  float viewport_y) {
    JsDomSvgShapeHit result = {};
    if (!elem) return result;
    RdtMatrix screen_ctm = js_dom_svg_ctm(elem, true);
    float local_x = 0.0f;
    float local_y = 0.0f;
    if (!js_dom_svg_matrix_unproject_point(&screen_ctm, viewport_x, viewport_y,
                                            &local_x, &local_y)) {
        return result;
    }
    float scale_x = hypotf(screen_ctm.e11, screen_ctm.e21);
    float scale_y = hypotf(screen_ctm.e12, screen_ctm.e22);
    float min_scale = LMB_MIN(scale_x, scale_y);
    if (min_scale < 0.0001f) min_scale = 0.0001f;
    return js_dom_svg_basic_shape_hit_local_point(elem, local_x, local_y, min_scale);
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_svg_tag_is, (DomElement* elem, const char* tag), (elem && elem->tag_name && tag && strcasecmp(elem->tag_name, tag) == 0))

static bool js_dom_svg_viewport_local_bounds(DomElement* elem, float* left, float* top,
                                             float* right, float* bottom) {
    if (!js_dom_svg_tag_is(elem, "svg") || !left || !top || !right || !bottom) return false;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    const char* viewbox = elem->get_attribute("viewBox");
    if (!viewbox) viewbox = elem->get_attribute("viewbox");
    if (js_dom_svg_parse_viewbox(viewbox, &min_x, &min_y, &width, &height)) {
        *left = min_x;
        *top = min_y;
        *right = min_x + width;
        *bottom = min_y + height;
        return true;
    }
    width = js_dom_svg_attribute_number(elem, "width", elem->width);
    height = js_dom_svg_attribute_number(elem, "height", elem->height);
    if (width <= 0.0f || height <= 0.0f) return false;
    *left = 0.0f;
    *top = 0.0f;
    *right = width;
    *bottom = height;
    return true;
}

static JsDomSvgShapeHit js_dom_svg_bounds_hit_viewport_point(DomElement* elem,
                                                              float viewport_x,
                                                              float viewport_y) {
    JsDomSvgShapeHit result = {};
    if (!elem) return result;
    RdtMatrix screen_ctm = js_dom_svg_ctm(elem, true);
    float local_x = 0.0f;
    float local_y = 0.0f;
    if (!js_dom_svg_matrix_unproject_point(&screen_ctm, viewport_x, viewport_y,
                                            &local_x, &local_y)) {
        return result;
    }
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    if (js_dom_svg_tag_is(elem, "svg")) {
        if (!js_dom_svg_viewport_local_bounds(elem, &left, &top, &right, &bottom)) {
            return result;
        }
    } else {
        JsDomSvgBounds bounds = js_dom_svg_bounds_for_element(elem);
        if (!bounds.valid) return result;
        left = bounds.left;
        top = bounds.top;
        right = bounds.right;
        bottom = bounds.bottom;
    }
    result.bounding_box = local_x >= left && local_x <= right &&
        local_y >= top && local_y <= bottom;
    // Image/text/foreignObject content is painted by its own renderer. Its
    // exposed SVG geometry is its viewport box, not a union of child boxes.
    result.fill = result.bounding_box;
    return result;
}

static DomElement* js_dom_svg_use_reference(DomElement* elem) {
    if (!elem || !elem->doc || !js_dom_svg_tag_is(elem, "use")) return nullptr;
    const char* href = elem->get_attribute("href");
    if (!href) href = elem->get_attribute("xlink:href");
    if (!href || href[0] != '#' || !href[1]) return nullptr;
    return js_dom_find_element_by_id(elem->doc->root, href + 1);
}

static JsDomSvgShapeHit js_dom_svg_reference_hit_viewport_point(DomElement* reference,
                                                                 const RdtMatrix* reference_ctm,
                                                                 float viewport_x,
                                                                 float viewport_y) {
    JsDomSvgShapeHit result = {};
    if (!reference || !reference_ctm) return result;
    if (js_dom_svg_is_basic_shape(reference)) {
        float local_x = 0.0f;
        float local_y = 0.0f;
        if (!js_dom_svg_matrix_unproject_point(reference_ctm, viewport_x, viewport_y,
                                                &local_x, &local_y)) {
            return result;
        }
        float scale_x = hypotf(reference_ctm->e11, reference_ctm->e21);
        float scale_y = hypotf(reference_ctm->e12, reference_ctm->e22);
        float min_scale = LMB_MIN(scale_x, scale_y);
        if (min_scale < 0.0001f) min_scale = 0.0001f;
        return js_dom_svg_basic_shape_hit_local_point(reference, local_x, local_y, min_scale);
    }
    if (!js_dom_svg_tag_is(reference, "g") && !js_dom_svg_tag_is(reference, "a") &&
        !js_dom_svg_tag_is(reference, "symbol")) {
        return result;
    }
    for (DomNode* child = reference->last_child; child; child = child->prev_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        RdtMatrix child_transform = js_dom_svg_transform_from_element(child_elem);
        RdtMatrix child_ctm = rdt_matrix_multiply(reference_ctm, &child_transform);
        JsDomSvgShapeHit child_hit = js_dom_svg_reference_hit_viewport_point(child_elem,
            &child_ctm, viewport_x, viewport_y);
        result.fill = result.fill || child_hit.fill;
        result.stroke = result.stroke || child_hit.stroke;
        result.bounding_box = result.bounding_box || child_hit.bounding_box;
    }
    return result;
}

static JsDomSvgShapeHit js_dom_svg_use_hit_viewport_point(DomElement* elem,
                                                           float viewport_x,
                                                           float viewport_y) {
    JsDomSvgShapeHit result = {};
    DomElement* reference = js_dom_svg_use_reference(elem);
    if (!reference) return result;
    RdtMatrix instance_ctm = js_dom_svg_ctm(elem, true);
    RdtMatrix offset = rdt_matrix_translate(
        js_dom_svg_attribute_number(elem, "x", 0.0f),
        js_dom_svg_attribute_number(elem, "y", 0.0f));
    instance_ctm = rdt_matrix_multiply(&instance_ctm, &offset);
    RdtMatrix reference_transform = js_dom_svg_transform_from_element(reference);
    RdtMatrix reference_ctm = rdt_matrix_multiply(&instance_ctm, &reference_transform);
    result = js_dom_svg_reference_hit_viewport_point(reference, &reference_ctm,
        viewport_x, viewport_y);
    JsDomSvgBounds bounds = js_dom_svg_bounds_for_element(reference);
    float local_x = 0.0f;
    float local_y = 0.0f;
    if (bounds.valid && js_dom_svg_matrix_unproject_point(&reference_ctm, viewport_x,
            viewport_y, &local_x, &local_y)) {
        result.bounding_box = local_x >= bounds.left && local_x <= bounds.right &&
            local_y >= bounds.top && local_y <= bounds.bottom;
    }
    return result;
}

typedef enum JsDomSvgPointerEventsMode {
    JS_DOM_SVG_POINTER_EVENTS_VISIBLE_PAINTED,
    JS_DOM_SVG_POINTER_EVENTS_VISIBLE_FILL,
    JS_DOM_SVG_POINTER_EVENTS_VISIBLE_STROKE,
    JS_DOM_SVG_POINTER_EVENTS_VISIBLE,
    JS_DOM_SVG_POINTER_EVENTS_PAINTED,
    JS_DOM_SVG_POINTER_EVENTS_FILL,
    JS_DOM_SVG_POINTER_EVENTS_STROKE,
    JS_DOM_SVG_POINTER_EVENTS_ALL,
    JS_DOM_SVG_POINTER_EVENTS_BOUNDING_BOX,
    JS_DOM_SVG_POINTER_EVENTS_NONE,
} JsDomSvgPointerEventsMode;

static JsDomSvgPointerEventsMode js_dom_svg_pointer_events_mode(DomElement* elem) {
    char pointer_events_buffer[64] = {};
    const char* value = js_dom_svg_presentation_value(elem, "pointer-events", true,
        pointer_events_buffer, sizeof(pointer_events_buffer));
    if (!value || !*value || strcasecmp(value, "auto") == 0 ||
        strcasecmp(value, "visiblePainted") == 0) {
        return JS_DOM_SVG_POINTER_EVENTS_VISIBLE_PAINTED;
    }
    if (strcasecmp(value, "visibleFill") == 0) return JS_DOM_SVG_POINTER_EVENTS_VISIBLE_FILL;
    if (strcasecmp(value, "visibleStroke") == 0) return JS_DOM_SVG_POINTER_EVENTS_VISIBLE_STROKE;
    if (strcasecmp(value, "visible") == 0) return JS_DOM_SVG_POINTER_EVENTS_VISIBLE;
    if (strcasecmp(value, "painted") == 0) return JS_DOM_SVG_POINTER_EVENTS_PAINTED;
    if (strcasecmp(value, "fill") == 0) return JS_DOM_SVG_POINTER_EVENTS_FILL;
    if (strcasecmp(value, "stroke") == 0) return JS_DOM_SVG_POINTER_EVENTS_STROKE;
    if (strcasecmp(value, "all") == 0) return JS_DOM_SVG_POINTER_EVENTS_ALL;
    if (strcasecmp(value, "bounding-box") == 0) return JS_DOM_SVG_POINTER_EVENTS_BOUNDING_BOX;
    if (strcasecmp(value, "none") == 0) return JS_DOM_SVG_POINTER_EVENTS_NONE;
    return JS_DOM_SVG_POINTER_EVENTS_VISIBLE_PAINTED;
}

static bool js_dom_svg_element_is_visible_for_pointer_events(DomElement* elem) {
    char visibility_buffer[64] = {};
    const char* visibility = js_dom_svg_presentation_value(elem, "visibility", true,
        visibility_buffer, sizeof(visibility_buffer));
    return !visibility || (strcasecmp(visibility, "hidden") != 0 &&
        strcasecmp(visibility, "collapse") != 0);
}

static bool js_dom_svg_pointer_events_selects_geometry(JsDomSvgPointerEventsMode mode,
                                                        bool visible,
                                                        bool fill_painted,
                                                        bool stroke_painted,
                                                        const JsDomSvgShapeHit* hit) {
    if (!hit || mode == JS_DOM_SVG_POINTER_EVENTS_NONE) return false;
    switch (mode) {
    case JS_DOM_SVG_POINTER_EVENTS_VISIBLE_PAINTED:
        return visible && ((fill_painted && hit->fill) || (stroke_painted && hit->stroke));
    case JS_DOM_SVG_POINTER_EVENTS_VISIBLE_FILL:
        return visible && hit->fill;
    case JS_DOM_SVG_POINTER_EVENTS_VISIBLE_STROKE:
        return visible && hit->stroke;
    case JS_DOM_SVG_POINTER_EVENTS_VISIBLE:
        return visible && (hit->fill || hit->stroke);
    case JS_DOM_SVG_POINTER_EVENTS_PAINTED:
        return (fill_painted && hit->fill) || (stroke_painted && hit->stroke);
    case JS_DOM_SVG_POINTER_EVENTS_FILL:
        return hit->fill;
    case JS_DOM_SVG_POINTER_EVENTS_STROKE:
        return hit->stroke;
    case JS_DOM_SVG_POINTER_EVENTS_ALL:
        return hit->fill || hit->stroke;
    case JS_DOM_SVG_POINTER_EVENTS_BOUNDING_BOX:
        return hit->bounding_box;
    case JS_DOM_SVG_POINTER_EVENTS_NONE:
        return false;
    }
    return false;
}

static bool js_dom_svg_element_skips_hit_test(DomElement* elem) {
    if (!elem || !elem->tag_name) return true;
    const char* tag = elem->tag_name;
    if (strcasecmp(tag, "defs") == 0 || strcasecmp(tag, "clipPath") == 0 ||
        strcasecmp(tag, "mask") == 0 || strcasecmp(tag, "marker") == 0 ||
        strcasecmp(tag, "pattern") == 0 || strcasecmp(tag, "linearGradient") == 0 ||
        strcasecmp(tag, "radialGradient") == 0 || strcasecmp(tag, "title") == 0 ||
        strcasecmp(tag, "desc") == 0 || strcasecmp(tag, "metadata") == 0) {
        return true;
    }
    char display_buffer[64] = {};
    const char* display = js_dom_svg_presentation_value(elem, "display", false,
        display_buffer, sizeof(display_buffer));
    // visibility and pointer-events are inherited, but descendants may
    // override either one. Only display:none removes the entire subtree.
    return display && strcasecmp(display, "none") == 0;
}

static bool js_dom_svg_point_is_within_viewports(DomElement* elem, float x, float y) {
    for (DomNode* node = (DomNode*)elem; node && node->is_element(); node = node->parent) {
        DomElement* current = node->as_element();
        if (!js_dom_svg_tag_is(current, "svg")) continue;
        char overflow_buffer[64] = {};
        const char* overflow = js_dom_svg_presentation_value(current, "overflow", false,
            overflow_buffer, sizeof(overflow_buffer));
        if (overflow && strcasecmp(overflow, "visible") == 0) continue;
        float local_x = 0.0f;
        float local_y = 0.0f;
        RdtMatrix ctm = js_dom_svg_ctm(current, true);
        if (!js_dom_svg_matrix_unproject_point(&ctm, x, y, &local_x, &local_y)) {
            return false;
        }
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        if (!js_dom_svg_viewport_local_bounds(current, &left, &top, &right, &bottom) ||
            local_x < left || local_x > right || local_y < top || local_y > bottom) {
            return false;
        }
    }
    return true;
}

static bool js_dom_svg_element_contains_viewport_point(DomElement* elem,
                                                       float x, float y) {
    if (!elem || js_dom_svg_element_skips_hit_test(elem) ||
        !js_dom_svg_point_is_within_viewports(elem, x, y)) return false;
    JsDomSvgPointerEventsMode pointer_events = js_dom_svg_pointer_events_mode(elem);
    if (pointer_events == JS_DOM_SVG_POINTER_EVENTS_NONE) return false;
    JsDomSvgShapeHit hit = {};
    bool fill_painted = false;
    bool stroke_painted = false;
    if (js_dom_svg_is_basic_shape(elem)) {
        hit = js_dom_svg_basic_shape_hit_viewport_point(elem, x, y);
        fill_painted = js_dom_svg_paint_is_present(elem, "fill", true);
        stroke_painted = js_dom_svg_paint_is_present(elem, "stroke", false);
    } else if (js_dom_svg_tag_is(elem, "use")) {
        DomElement* reference = js_dom_svg_use_reference(elem);
        hit = js_dom_svg_use_hit_viewport_point(elem, x, y);
        fill_painted = js_dom_svg_paint_is_present(reference ? reference : elem, "fill", true);
        stroke_painted = js_dom_svg_paint_is_present(reference ? reference : elem, "stroke", false);
    } else if (js_dom_svg_tag_is(elem, "g") || js_dom_svg_tag_is(elem, "a") ||
               js_dom_svg_tag_is(elem, "switch")) {
        // Container bounds are only a union for getBBox(); blank union gaps
        // belong to no painted child and must not become a drag target.
        return false;
    } else {
        hit = js_dom_svg_bounds_hit_viewport_point(elem, x, y);
        if (js_dom_svg_tag_is(elem, "image") ||
            js_dom_svg_tag_is(elem, "foreignObject") || js_dom_svg_tag_is(elem, "svg")) {
            fill_painted = true;
        } else {
            fill_painted = js_dom_svg_paint_is_present(elem, "fill", true);
            stroke_painted = js_dom_svg_paint_is_present(elem, "stroke", false);
        }
    }
    return js_dom_svg_pointer_events_selects_geometry(pointer_events,
        js_dom_svg_element_is_visible_for_pointer_events(elem), fill_painted,
        stroke_painted, &hit);
}

static DomElement* js_dom_svg_element_from_point_walk(DomElement* elem,
                                                       float x, float y) {
    if (!elem || js_dom_svg_element_skips_hit_test(elem) ||
        !js_dom_svg_point_is_within_viewports(elem, x, y)) return nullptr;
    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        DomElement* hit = js_dom_svg_element_from_point_walk(child_elem, x, y);
        if (hit) return hit;
    }
    return js_dom_svg_element_contains_viewport_point(elem, x, y) ? elem : nullptr;
}

static DomElement* js_dom_svg_element_from_document_point_walk(DomNode* node,
                                                                float x, float y) {
    if (!node || !node->is_element()) return nullptr;
    DomElement* elem = node->as_element();
    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        if (child_elem->tag_name && strcasecmp(child_elem->tag_name, "svg") == 0) {
            DomElement* hit = js_dom_svg_element_from_point_walk(child_elem, x, y);
            if (hit) return hit;
        }
        DomElement* hit = js_dom_svg_element_from_document_point_walk(child, x, y);
        if (hit) return hit;
    }
    return nullptr;
}

extern "C" void* js_dom_document_svg_element_from_point(void* doc_ptr,
                                                          float x, float y) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    if (!doc || !doc->root || !js_dom_has_committed_geometry_snapshot(doc)) {
        return nullptr;
    }
    return js_dom_svg_element_from_document_point_walk((DomNode*)doc->root, x, y);
}

static void js_dom_absolute_node_position(DomNode* node,
                                          float* out_x,
                                          float* out_y) {
    float x = 0.0f;
    float y = 0.0f;
    for (DomNode* cur = node; cur; cur = cur->parent) {
        x += cur->x;
        y += cur->y;
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static DomElement* js_dom_offset_parent_element(DomElement* elem) {
    if (!elem) return nullptr;
    DomNode* p = elem->parent;
    while (p) {
        if (p->is_element()) {
            DomElement* pe = p->as_element();
            if (pe->tag_name && strcasecmp(pe->tag_name, "body") == 0)
                return pe;
            if (pe->tag_name &&
                (strcasecmp(pe->tag_name, "table") == 0 ||
                 strcasecmp(pe->tag_name, "td") == 0 ||
                 strcasecmp(pe->tag_name, "th") == 0)) {
                return pe;
            }
            if (pe->position && pe->positionp()->position != CSS_VALUE_STATIC)
                return pe;
        }
        p = p->parent;
    }
    return nullptr;
}

static int64_t js_dom_offset_coordinate(DomElement* elem, bool x_axis) {
    if (!elem) return 0;
    float abs_x = 0.0f;
    float abs_y = 0.0f;
    js_dom_absolute_node_position((DomNode*)elem, &abs_x, &abs_y);
    float value = x_axis ? abs_x : abs_y;

    DomElement* offset_parent = js_dom_offset_parent_element(elem);
    if (offset_parent) {
        if (!offset_parent->tag_name ||
            strcasecmp(offset_parent->tag_name, "body") != 0) {
            float parent_x = 0.0f;
            float parent_y = 0.0f;
            js_dom_absolute_node_position((DomNode*)offset_parent, &parent_x,
                                          &parent_y);
            value -= x_axis ? parent_x : parent_y;
        }
    }
    return (int64_t)value;
}

static void js_dom_scroll_offset_for_node(DomNode* node,
                                          float* out_x,
                                          float* out_y) {
    if (out_x) *out_x = 0.0f;
    if (out_y) *out_y = 0.0f;
    if (!node || !node->is_element()) return;
    if (!node->is_block()) {
        return;
    }
    DomElement* elem = node->as_element();
    if (!elem || !elem->scroller || !elem->scroll()->pane) return;
    if (out_x) *out_x = elem->scroll()->pane->h_scroll_position;
    if (out_y) *out_y = elem->scroll()->pane->v_scroll_position;
}

static void js_dom_viewport_node_position(DomNode* node,
                                          float* out_x,
                                          float* out_y) {
    float x = 0.0f;
    float y = 0.0f;
    DomNode* origin = node;
    for (DomNode* cur = node; cur; cur = cur->parent) {
        x += cur->x;
        y += cur->y;
        if (cur != origin) {
            float scroll_x = 0.0f;
            float scroll_y = 0.0f;
            js_dom_scroll_offset_for_node(cur, &scroll_x, &scroll_y);
            x -= scroll_x;
            y -= scroll_y;
        }
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_point_in_box, (float px, float py,                                 float x, float y,                                 float w, float h), (w > 0.0f && h > 0.0f && x <= px && px < x + w && y <= py && py < y + h))

static bool js_dom_text_contains_point(DomText* text,
                                       float abs_x,
                                       float abs_y,
                                       float px,
                                       float py) {
    if (!text) return false;
    for (TextRect* rect = text->rect; rect; rect = rect->next) {
        if (js_dom_point_in_box(px, py, abs_x + rect->x, abs_y + rect->y,
                rect->width, rect->height)) {
            return true;
        }
    }
    return false;
}

static bool js_dom_element_from_point_skips_subtree(DomElement* elem) {
    if (!elem || !elem->tag_name) return false;
    return _is_tag(elem, "head") ||
        _is_tag(elem, "style") ||
        _is_tag(elem, "script") ||
        _is_tag(elem, "title") ||
        _is_tag(elem, "meta") ||
        _is_tag(elem, "link") ||
        _is_tag(elem, "base") ||
        _is_tag(elem, "noscript") ||
        _is_tag(elem, "template");
}

static float js_dom_shadow_node_synthetic_width(DomNode* node) {
    if (!node) return 0.0f;
    if (node->is_text()) {
        DomText* text = node->as_text();
        return text && text->length > 0 ? (float)text->length : 0.0f;
    }
    if (!node->is_element()) return 0.0f;
    int64_t width = js_dom_headless_dimension(node->as_element(), true);
    return width > 0 ? (float)width : 1.0f;
}

static float js_dom_shadow_node_synthetic_height(DomNode* node) {
    if (!node) return 0.0f;
    if (node->is_text()) {
        DomText* text = node->as_text();
        return text && text->length > 0 ? 1.0f : 0.0f;
    }
    if (!node->is_element()) return 0.0f;
    int64_t height = js_dom_headless_dimension(node->as_element(), false);
    return height > 0 ? (float)height : 1.0f;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_dom_is_document_fragment_element, (DomElement* elem), (elem && elem->tag_name && strcmp(elem->tag_name, "#document-fragment") == 0))

static DomElement* js_dom_shadow_element_from_point_walk(DomNode* node,
                                                         float abs_x,
                                                         float abs_y,
                                                         float px,
                                                         float py) {
    if (!node) return nullptr;
    float node_x = abs_x + node->x;
    float node_y = abs_y + node->y;

    if (node->is_text()) {
        if (js_dom_point_in_box(px, py, node_x, node_y,
                js_dom_shadow_node_synthetic_width(node),
                js_dom_shadow_node_synthetic_height(node)) &&
            node->parent && node->parent->is_element()) {
            return node->parent->as_element();
        }
        return nullptr;
    }
    if (!node->is_element()) return nullptr;

    DomElement* elem = node->as_element();
    if (js_dom_element_from_point_skips_subtree(elem)) return nullptr;

    DomElement* best = nullptr;
    float child_x = node_x;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        DomElement* hit = js_dom_shadow_element_from_point_walk(child, child_x,
            node_y, px, py);
        if (hit) best = hit;
        child_x += js_dom_shadow_node_synthetic_width(child);
    }
    if (best) return best;
    if (js_dom_is_document_fragment_element(elem)) return nullptr;

    return js_dom_point_in_box(px, py, node_x, node_y,
        js_dom_shadow_node_synthetic_width(node),
        js_dom_shadow_node_synthetic_height(node)) ? elem : nullptr;
}

static DomElement* js_dom_element_from_point_walk(DomNode* node,
                                                  float abs_x,
                                                  float abs_y,
                                                  float px,
                                                  float py) {
    if (!node || !node->view_type) return nullptr;
    float node_x = abs_x + node->x;
    float node_y = abs_y + node->y;

    if (node->is_text()) {
        return js_dom_text_contains_point(node->as_text(), abs_x, abs_y,
            px, py) && node->parent && node->parent->is_element()
            ? node->parent->as_element() : nullptr;
    }
    if (!node->is_element()) return nullptr;

    DomElement* elem = node->as_element();
    if (js_dom_element_from_point_skips_subtree(elem)) return nullptr;
    if (elem->tag_name && strcasecmp(elem->tag_name, "svg") == 0) {
        // SVG's geometry walker is authoritative: falling through to the DOM
        // layout box would resurrect elements rejected by pointer-events.
        return js_dom_svg_element_from_point_walk(elem, px, py);
    }
    if (elem->shadow_root_element()) {
        DomElement* shadow_hit = js_dom_shadow_element_from_point_walk(
            (DomNode*)elem->shadow_root_element(), node_x, node_y, px, py);
        if (shadow_hit) return shadow_hit;
    }

    float child_base_x = node_x;
    float child_base_y = node_y;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    js_dom_scroll_offset_for_node(node, &scroll_x, &scroll_y);
    child_base_x -= scroll_x;
    child_base_y -= scroll_y;

    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        DomElement* hit = js_dom_element_from_point_walk(child, child_base_x,
            child_base_y, px, py);
        if (hit) return hit;
    }

    float hit_width = node->width;
    float hit_height = node->height;
    if (js_dom_is_editing_host(elem)) {
        if (hit_width <= 0.0f) {
            hit_width = (float)js_dom_headless_dimension(elem, true);
        }
        if (hit_height <= 0.0f) {
            hit_height = (float)js_dom_headless_dimension(elem, false);
        }
    }

    return js_dom_point_in_box(px, py, node_x, node_y, hit_width,
        hit_height) ? elem : nullptr;
}

static DomElement* js_dom_shadow_element_from_document_point_walk(DomNode* node,
                                                                  float px,
                                                                  float py) {
    if (!node || !node->is_element()) return nullptr;
    DomElement* elem = node->as_element();
    if (js_dom_element_from_point_skips_subtree(elem)) return nullptr;

    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        DomElement* hit = js_dom_shadow_element_from_document_point_walk(child,
            px, py);
        if (hit) return hit;
    }
    if (!elem->shadow_root_element()) return nullptr;

    float host_x = 0.0f;
    float host_y = 0.0f;
    js_dom_viewport_node_position((DomNode*)elem, &host_x, &host_y);
    DomElement* hit = js_dom_shadow_element_from_point_walk((DomNode*)elem->shadow_root_element(),
        host_x, host_y, px, py);
    if (hit) return hit;
    if (host_x != 0.0f || host_y != 0.0f) {
        // Shadow descendants in headless editing tests expose synthetic
        // offsetLeft/offsetTop before Radiant has real shadow layout boxes.
        return js_dom_shadow_element_from_point_walk(
            (DomNode*)elem->shadow_root_element(), 0.0f, 0.0f, px, py);
    }
    return nullptr;
}

static Item js_dom_document_element_from_point(DomDocument* doc,
                                               Item x_arg,
                                               Item y_arg);

JS_FORWARD_STATIC_EXPRESSION(float, js_dom_item_to_float, (Item value),
    js_dom_svg_number(value, 0.0f))
JS_FORWARD_STATIC_ITEM(js_dom_float_item, (float value), js_make_number, ((double)value))

static Item js_dom_make_plain_boundary_object(DomBoundary boundary) {
    if (!boundary.node) return ItemNull;
    Item out = js_new_object();
    js_set_key_cstr(out, "node", js_dom_wrap_element(boundary.node));
    js_set_key_cstr(out, "offset", (Item){.item = i2it((int64_t)boundary.offset)});
    return out;
}

static Item js_dom_make_boundary_object(DomBoundary boundary) {
    Item out = js_dom_make_plain_boundary_object(boundary);
    if (get_type_id(out) != LMD_TYPE_MAP) return out;

    DomBoundary all_start;
    DomBoundary all_end;
    if (dom_selection_user_select_all_range_for_node(boundary.node,
            &all_start, &all_end)) {
        js_set_key_cstr(out, "selectAllStart", js_dom_make_plain_boundary_object(all_start));
        js_set_key_cstr(out, "selectAllEnd", js_dom_make_plain_boundary_object(all_end));
    }
    DomBoundary triple_start;
    DomBoundary triple_end;
    if (dom_selection_triple_click_range_for_node(boundary.node,
            &triple_start, &triple_end)) {
        js_set_key_cstr(out, "tripleClickStart", js_dom_make_plain_boundary_object(triple_start));
        js_set_key_cstr(out, "tripleClickEnd", js_dom_make_plain_boundary_object(triple_end));
    }
    return out;
}

static bool js_dom_boundary_inside_text_control(DomBoundary boundary) {
    for (DomNode* node = boundary.node; node; node = node->parent) {
        if (node->is_element() && tc_is_text_control_elem(node->as_element())) {
            return true;
        }
    }
    return false;
}

static Item js_dom_boundary_from_point(DomElement* elem,
                                       Item x_arg,
                                       Item y_arg,
                                       Item behavior_arg) {
    if (!elem || !elem->doc || !_js_current_ui_context ||
        !js_dom_has_committed_geometry_snapshot(elem->doc) ||
        !elem->doc->view_tree || !elem->doc->view_tree->root) {
        return ItemNull;
    }

    float x = js_dom_item_to_float(x_arg);
    float y = js_dom_item_to_float(y_arg);
    EditingSurface surface;
    if (editing_surface_from_target(static_cast<View*>(elem), &surface) &&
        editing_surface_is_rich(&surface)) {
        EditingBoundary hit;
        editing_boundary_clear(&hit);
        EditingPointBehavior behavior = EDITING_POINT_BEHAVIOR_DEFAULT;
        const char* behavior_text = fn_to_cstr(behavior_arg);
        if (behavior_text && strcasecmp(behavior_text, "mac") == 0) {
            behavior = EDITING_POINT_BEHAVIOR_MAC;
        }
        if (editing_geometry_hit_test_boundary(_js_current_ui_context,
                elem->doc->view_tree->root, &surface, x, y,
                EDITING_CLAMP_SKIP_TEXT_CONTROLS, &hit, behavior) &&
            hit.kind == EDITING_BOUNDARY_DOM &&
            hit.dom.node) {
            return js_dom_make_boundary_object(hit.dom);
        }
    }

    DomBoundary boundary = dom_hit_test_to_boundary(
        elem->doc->view_tree->root, x, y);
    if (!boundary.node || js_dom_boundary_inside_text_control(boundary)) {
        return ItemNull;
    }
    return js_dom_make_boundary_object(boundary);
}

extern "C" void* js_dom_document_element_from_point_native(void* doc_ptr,
                                                            float x, float y) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    if (!doc || !js_dom_has_committed_geometry_snapshot(doc) ||
        !doc->view_tree || !doc->view_tree->root) {
        return nullptr;
    }

    DomElement* shadow_hit = js_dom_shadow_element_from_document_point_walk(
        static_cast<DomNode*>(doc->root), x, y);
    if (shadow_hit) return shadow_hit;

    return js_dom_element_from_point_walk(
        static_cast<DomNode*>(doc->view_tree->root),
        0.0f, 0.0f, x, y);
}

static Item js_dom_document_element_from_point(DomDocument* doc,
                                               Item x_arg,
                                               Item y_arg) {
    if (!doc || !_js_current_ui_context) return ItemNull;
    float x = js_dom_item_to_float(x_arg);
    float y = js_dom_item_to_float(y_arg);
    DomElement* hit = (DomElement*)js_dom_document_element_from_point_native(doc, x, y);
    return hit ? js_dom_wrap_element(hit) : ItemNull;
}
JS_FORWARD_ITEM(js_dom_document_element_from_point_bridge, (void* doc_ptr,                                                           Item x_arg,                                                           Item y_arg), js_dom_document_element_from_point, ((DomDocument*)doc_ptr, x_arg, y_arg))

static Item js_dom_text_control_caret_bounds(DomElement* elem) {
    if (!elem || !tc_is_text_control_elem(elem) || !_js_current_ui_context) {
        return ItemNull;
    }

    DocState* state = elem->doc ? elem->doc->state : js_dom_current_state();
    if (!state && elem->doc) {
        state = radiant_document_ensure_state(elem->doc,
            "js_dom_text_control_caret_bounds");
    }

    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    if (!form) return ItemNull;

    uint32_t start_u16 = 0;
    uint32_t end_u16 = 0;
    uint8_t direction = 0;
    form_control_get_selection(state, static_cast<View*>(elem),
        &start_u16, &end_u16, &direction);
    uint32_t focus_u16 = direction == 2 ? start_u16 : end_u16;

    const char* value = form->current_value ? form->current_value : form->value;
    uint32_t value_len = form->current_value_len;
    uint32_t focus_utf8 = tc_utf16_to_utf8_offset(value ? value : "",
        value_len, focus_u16);

    EditingCaretRect rect;
    editing_caret_rect_clear(&rect);
    if (!editing_geometry_text_control_caret_rect(_js_current_ui_context,
            elem, focus_utf8, &rect) || !rect.valid) {
        return ItemNull;
    }
    return js_dom_make_rect_object(rect.x, rect.y, rect.width, rect.height);
}

static Item js_dom_text_control_boundary_from_point(DomElement* elem,
                                                    Item x_arg,
                                                    Item y_arg) {
    if (!elem || !tc_is_text_control_elem(elem) || !_js_current_ui_context ||
        !elem->doc || !js_dom_has_committed_geometry_snapshot(elem->doc)) {
        return ItemNull;
    }

    EditingBoundary hit;
    editing_boundary_clear(&hit);
    if (!editing_geometry_text_control_boundary_from_point(
            _js_current_ui_context, elem, js_dom_item_to_float(x_arg),
            js_dom_item_to_float(y_arg), &hit) ||
        hit.kind != EDITING_BOUNDARY_TEXT_CONTROL) {
        return ItemNull;
    }

    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    const char* value = form && form->current_value
        ? form->current_value
        : (form ? form->value : "");
    uint32_t value_len = form ? form->current_value_len : 0;
    uint32_t offset_u16 = tc_utf8_to_utf16_offset(value ? value : "",
        value_len, hit.offset);

    Item out = js_new_object();
    js_set_key_cstr(out, "node", js_dom_wrap_element(elem));
    js_set_key_cstr(out, "offset", (Item){.item = i2it((int64_t)offset_u16)});
    js_set_key_cstr(out, "byteOffset", (Item){.item = i2it((int64_t)hit.offset)});
    return out;
}
JS_FORWARD_ITEM(js_dom_text_control_caret_bounds_bridge, (void* elem), js_dom_text_control_caret_bounds, ((DomElement*)elem))
JS_FORWARD_ITEM(js_dom_text_control_boundary_from_point_bridge, (void* elem,                                                                Item x_arg,                                                                Item y_arg), js_dom_text_control_boundary_from_point, ((DomElement*)elem, x_arg, y_arg))
JS_FORWARD_ITEM(js_dom_boundary_from_point_bridge, (void* elem,                                                   Item x_arg,                                                   Item y_arg,                                                   Item behavior_arg), js_dom_boundary_from_point, ((DomElement*)elem, x_arg, y_arg, behavior_arg))

extern "C" Item js_dom_get_bounding_client_rect_bridge(void* dom_elem) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return ItemNull;
    if (elem->doc) js_dom_ensure_geometry_snapshot(elem->doc);
    if (layout_noscript_content_suppressed(elem)) {
        return js_dom_make_rect_object(0.0, 0.0, 0.0, 0.0);
    }
    float abs_x = 0.0f;
    float abs_y = 0.0f;
    js_dom_viewport_node_position((DomNode*)elem, &abs_x, &abs_y);
    float width = elem->width;
    float height = elem->height;
    return js_dom_make_rect_object(abs_x, abs_y,
        width > 0.0f ? width : (float)js_dom_geometry_dimension(elem, true),
        height > 0.0f ? height : (float)js_dom_geometry_dimension(elem, false));
}

extern "C" Item js_dom_get_client_rects_bridge(void* dom_elem) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return js_array_new(0);
    if (elem->doc) js_dom_ensure_geometry_snapshot(elem->doc);
    if (layout_noscript_content_suppressed(elem)) {
        return js_array_new(0);
    }

    float abs_x = 0.0f;
    float abs_y = 0.0f;
    js_dom_viewport_node_position((DomNode*)elem, &abs_x, &abs_y);
    float w = elem->width;
    float h = elem->height;
    if (w <= 0.0f) w = (float)js_dom_geometry_dimension(elem, true);
    if (h <= 0.0f) h = (float)js_dom_geometry_dimension(elem, false);

    Item rect = js_new_object();
    Item k;
    k = js_name_item("x");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)abs_x)});
    k = js_name_item("y");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)abs_y)});
    k = js_name_item("top");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)abs_y)});
    k = js_name_item("left");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)abs_x)});
    k = js_name_item("right");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)(abs_x + w))});
    k = js_name_item("bottom");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)(abs_y + h))});
    k = js_name_item("width");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)w)});
    k = js_name_item("height");
    js_set_key_default(rect, k, (Item){.item = i2it((int64_t)h)});

    Item arr = js_array_new(0);
    js_array_push(arr, rect);
    return arr;
}

extern "C" Item js_dom_scroll_into_view_bridge(void* dom_elem) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem) return make_js_undefined();
    DomDocument* doc = elem->doc ? elem->doc : _js_current_document;
    if (doc) {
        if (doc->pending_scroll_into_view_target) {
            dom_node_unpin(doc,
                {(DomNode*)doc->pending_scroll_into_view_target,
                 doc->pending_scroll_into_view_target_id},
                DOM_NODE_PIN_RECONCILE);
        }
        DomNodeRef ref = dom_node_ref((DomNode*)elem);
        if (!dom_node_ref_validate(doc, ref) ||
            !dom_node_pin(doc, ref, DOM_NODE_PIN_RECONCILE)) {
            return make_js_undefined();
        }
        doc->pending_scroll_into_view_target = elem;
        doc->pending_scroll_into_view_target_id = ref.expected_id;
        log_debug("js_dom_scrollIntoView: queued target <%s>",
                  elem->tag_name ? elem->tag_name : "?");
    }
    return make_js_undefined();
}

extern "C" Item js_dom_scroll_operation_bridge(Item elem_item,
                                                JubeDomElementOperation operation,
                                                Item* args, int argc) {
    float x = 0.0f;
    float y = 0.0f;
    if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
        Item left = js_get_key_cstr(args[0], "left");
        Item top = js_get_key_cstr(args[0], "top");
        x = js_dom_item_to_float(left);
        y = js_dom_item_to_float(top);
    } else {
        if (argc >= 1) x = js_dom_item_to_float(args[0]);
        if (argc >= 2) y = js_dom_item_to_float(args[1]);
    }
    if (x != x) x = 0.0f;
    if (y != y) y = 0.0f;
    if (operation == JUBE_DOM_SCROLL_BY) {
        x += js_dom_item_to_float(js_dom_get_property_impl(elem_item, js_string_key("scrollLeft")));
        y += js_dom_item_to_float(js_dom_get_property_impl(elem_item, js_string_key("scrollTop")));
    }
    // scroll(), scrollTo(), and scrollBy() share the element scroll setters
    // so pending viewport/element scroll state stays in one place.
    js_dom_set_property_impl(elem_item, js_string_key("scrollLeft"), js_dom_float_item(x));
    js_dom_set_property_impl(elem_item, js_string_key("scrollTop"), js_dom_float_item(y));
    return make_js_undefined();
}

static int64_t js_dom_backed_child_index(DomElement* parent, DomElement* child) {
    if (!parent || !child || !parent->doc || !parent->doc->input) return -1;
    Element* parent_backing = dom_element_to_element(parent);
    Element* child_backing = dom_element_to_element(child);
    for (int64_t i = 0; i < parent_backing->length; i++) {
        Item item = parent_backing->items[i];
        if (get_type_id(item) == LMD_TYPE_ELEMENT && item.element == child_backing) {
            return i;
        }
    }
    return -1;
}

static int64_t js_dom_backed_node_index(DomElement* parent, DomNode* child) {
    if (!parent || !child) return -1;
    if (child->is_element()) {
        return js_dom_backed_child_index(parent, child->as_element());
    }
    if (!child->is_text()) return -1;

    DomText* text = child->as_text();
    Element* parent_backing = dom_element_to_element(parent);
    if (!text || !text->native_string || !parent_backing) return -1;
    for (int64_t i = 0; i < parent_backing->length; i++) {
        Item item = parent_backing->items[i];
        if (get_type_id(item) == LMD_TYPE_STRING &&
            item.get_string() == text->native_string) {
            return i;
        }
    }
    return -1;
}

static bool js_dom_remove_backed_child(DomElement* parent, DomNode* child);

static bool js_dom_remove_backed_element_item(DomElement* parent,
                                               DomElement* child,
                                               int64_t child_index) {
    if (!parent || !child || child_index < 0 || !parent->doc ||
        !parent->doc->input) {
        return false;
    }
    // drag libraries may detach a wrapper before reinsertion; remove its stale
    // Mark slot first so a later relink cannot restore the previous order.
    Element* parent_backing = dom_element_to_element(parent);
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child({.element = parent_backing},
                                            (int)child_index);
    if (get_type_id(result) != LMD_TYPE_ELEMENT || result.element != parent_backing) {
        log_error("js_dom_remove_backed_element_item: inline delete changed backing identity");
        return false;
    }
    return true;
}

static bool js_dom_append_backed_element(DomElement* parent, DomNode* child) {
    if (!parent || !child || !child->is_element()) return false;
    parent = js_dom_prepare_children_for_mutation(parent);
    if (!parent) return false;
    DomElement* child_elem = child->as_element();
    int64_t backed_index = js_dom_backed_child_index(parent, child_elem);
    if (backed_index >= 0) {
        if (child->parent == parent) {
            if (parent->last_child == child) return true;
            // Move an existing sibling through both trees so the backing order
            // stays aligned with the DOM appendChild() ordering rule.
            if (!js_dom_remove_backed_child(parent, child)) return false;
        } else if (child->parent) {
            if (!child->parent->is_element() ||
                !((DomNode*)child->parent)->remove_child(child)) return false;
        } else if (!js_dom_remove_backed_element_item(parent, child_elem,
                                                       backed_index)) {
            return false;
        }
        return parent->append_child(child_elem);
    }
    if (child->parent) {
        if (!child->parent->is_element()) return false;
        DomElement* previous_parent = child->parent->as_element();
        if (previous_parent == parent && parent->last_child == child) {
            return true;
        }
        // appendChild() moves an existing node before appending it.  Passing a
        // linked child straight to MarkEditor duplicates its backing entry and
        // leaves the DOM sibling chain inconsistent after library capability
        // probes reappend a temporary fieldset.
        if (!js_dom_remove_backed_child(previous_parent, child)) return false;
    }
    if (!parent->append_child(child_elem)) return false;
    if (child->parent != parent) {
        // UI-mode MarkEditor updates the backing Element tree but does not own
        // dynamically-created DomNode links; retain both trees for event-time
        // reconciliation and selector traversal.
        return ((DomNode*)parent)->append_child(child);
    }
    return true;
}

static int64_t js_dom_backed_insertion_index(DomElement* parent,
                                              DomNode* ref_child) {
    if (!parent) return -1;
    int64_t insert_index = dom_element_to_element(parent)->length;
    for (DomNode* candidate = ref_child; candidate;
         candidate = candidate->next_sibling) {
        insert_index = js_dom_backed_node_index(parent, candidate);
        if (insert_index >= 0) break;
    }
    if (insert_index < 0) insert_index = dom_element_to_element(parent)->length;
    return insert_index;
}

static bool js_dom_insert_backed_element(DomElement* parent, DomNode* child,
                                         DomNode* ref_child) {
    if (!parent || !child || !child->is_element()) return false;
    parent = js_dom_prepare_children_for_mutation(parent);
    if (!parent) return false;
    if (ref_child && ref_child->parent != (DomNode*)parent) return false;
    DomElement* child_elem = child->as_element();
    if (parent->is_synthetic() || child_elem->is_synthetic()) {
        if (child->parent && !child->parent->remove_child(child)) return false;
        return ((DomNode*)parent)->insert_before(child, ref_child);
    }
    if (!parent->doc || !parent->doc->input) return false;
    if (child == ref_child ||
        (child->parent == (DomNode*)parent && child->next_sibling == ref_child)) {
        return true;
    }

    if (child->parent) {
        if (!child->parent->is_element() ||
            !js_dom_remove_backed_child(child->parent->as_element(), child)) {
            return false;
        }
    } else {
        int64_t backed_index = js_dom_backed_child_index(parent, child_elem);
        if (backed_index >= 0 &&
            !js_dom_remove_backed_element_item(parent, child_elem, backed_index)) {
            return false;
        }
    }

    int64_t insert_index = js_dom_backed_insertion_index(parent, ref_child);
    if (insert_index < 0) return false;

    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Element* parent_backing = dom_element_to_element(parent);
    Item result = editor.elmt_insert_child({.element = parent_backing},
                                            (int)insert_index,
                                            {.element = dom_element_to_element(child_elem)});
    if (get_type_id(result) != LMD_TYPE_ELEMENT || result.element != parent_backing) {
        log_error("js_dom_insert_backed_element: inline insert changed backing identity");
        return false;
    }
    if (child->parent != (DomNode*)parent) {
        // MarkEditor relinks only wrappers already present in the backing
        // Element; a newly-created mark wrapper still needs the DOM-side link
        // or the renderer loses the element immediately after insertion.
        return ((DomNode*)parent)->insert_before(child, ref_child);
    }
    return true;
}

static bool js_dom_insert_backed_text(DomElement* parent, DomText* text,
                                      DomNode* ref_child) {
    if (!parent || !text || !text->native_string || !parent->doc ||
        !parent->doc->input) return false;
    parent = js_dom_prepare_children_for_mutation(parent);
    if (!parent) return false;
    String* native_string = text->native_string;

    if (text->parent) {
        if (!text->parent->is_element() || !dom_text_remove(text)) {
            return false;
        }
        // static Mark text is invalidated when unlinked, but appendChild must
        // carry the original backing item through a move before relinking it.
        if (!text->native_string) text->native_string = native_string;
    }

    int64_t insert_index = dom_element_to_element(parent)->length;
    if (ref_child) {
        insert_index = -1;
        for (DomNode* candidate = ref_child; candidate;
             candidate = candidate->next_sibling) {
            int64_t candidate_index = js_dom_backed_node_index(parent, candidate);
            if (candidate_index >= 0) {
                insert_index = candidate_index;
                break;
            }
        }
        if (insert_index < 0) {
            // DOM-only siblings are preserved by MarkEditor relinking; count
            // backed siblings before the reference to keep their relative order.
            insert_index = 0;
            for (DomNode* candidate = parent->first_child;
                 candidate && candidate != ref_child;
                 candidate = candidate->next_sibling) {
                if (js_dom_backed_node_index(parent, candidate) >= 0) {
                    insert_index++;
                }
            }
        }
    }

    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Element* parent_backing = dom_element_to_element(parent);
    Item result = editor.elmt_insert_child(
        {.element = parent_backing},
        (int)insert_index,
        {.item = s2it(native_string)});
    if (get_type_id(result) != LMD_TYPE_ELEMENT || result.element != parent_backing) {
        log_error("js_dom_insert_backed_text: inline insert changed backing identity");
        return false;
    }
    if (text->parent != (DomNode*)parent) {
        // MarkEditor can update the backing list before the wrapper relink;
        // complete the DOM-side link without creating a second Mark string.
        return ((DomNode*)parent)->insert_before((DomNode*)text, ref_child);
    }
    bool at_requested_position = ref_child
        ? text->next_sibling == ref_child
        : parent->last_child == (DomNode*)text;
    if (!at_requested_position) {
        // Backing relink ignores layout-only pseudos, so restore the requested
        // live-DOM position without editing the already-correct Mark order.
        if (!((DomNode*)parent)->remove_child((DomNode*)text)) return false;
        return ((DomNode*)parent)->insert_before((DomNode*)text, ref_child);
    }
    return true;
}

static bool js_dom_append_fragment_children(DomElement* parent,
                                             DomElement* fragment) {
    if (!parent || !fragment) return false;
    DomNode* child = fragment->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        if (!js_dom_prepare_cross_document_insertion(child, parent)) {
            return false;
        }
        if (child->is_element()) {
            // DOM Standard: appending a DocumentFragment splices its children;
            // keep the backing Element order aligned with the live DOM chain.
            if (!js_dom_append_backed_element(parent, child)) return false;
        } else if (child->is_text()) {
            if (!js_dom_insert_backed_text(parent, child->as_text(), nullptr)) {
                return false;
            }
        } else if (!((DomNode*)parent)->append_child(child)) {
            return false;
        }
        dom_post_insert((DomNode*)parent, child);
        child = next;
    }
    js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT,
                           (DomNode*)parent, (DomNode*)parent);
    return true;
}

static bool js_dom_insert_before_child(DomElement* parent, DomNode* child,
                                       DomNode* ref_child) {
    if (!parent || !child) return false;
    if (child == ref_child ||
        (child->parent == (DomNode*)parent && child->next_sibling == ref_child)) {
        return true;
    }

    // a same-parent reorder changes Element::items as well as sibling links;
    // otherwise HTMLCollection keeps reporting the source order after insertBefore().
    if (child->parent) dom_pre_remove(child);
    bool inserted = child->is_element()
        ? js_dom_insert_backed_element(parent, child, ref_child)
        : child->is_text()
            ? js_dom_insert_backed_text(parent, child->as_text(), ref_child)
            : ((child->parent ? child->parent->remove_child(child) : true) &&
               ((DomNode*)parent)->insert_before(child, ref_child));
    if (!inserted) return false;

    dom_post_insert((DomNode*)parent, child);
    js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT, child, (DomNode*)parent);
    return true;
}

static bool js_dom_insert_text_before_child(DomElement* parent, Item text_item,
                                            DomNode* ref_child) {
    if (!parent || get_type_id(text_item) != LMD_TYPE_STRING) return false;
    String* source = it2s(text_item);
    if (!source) return false;

    if (parent->is_synthetic()) {
        DomText* text = DomText::create_detached_copy(
            parent->doc, source->chars, source->len);
        if (!text || !((DomNode*)parent)->insert_before(text, ref_child)) return false;
        dom_post_insert((DomNode*)parent, text);
        js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT, text,
                               (DomNode*)parent);
        return true;
    }
    if (!parent->doc || !parent->doc->input) return false;

    int64_t insert_index = js_dom_backed_insertion_index(parent, ref_child);
    if (insert_index < 0) return false;
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Element* parent_backing = dom_element_to_element(parent);
    Item result = editor.elmt_insert_child({.element = parent_backing},
                                            (int)insert_index, text_item);
    if (get_type_id(result) != LMD_TYPE_ELEMENT ||
        result.element != parent_backing ||
        insert_index >= parent_backing->length) {
        log_error("js_dom_insert_text_before_child: inline insert changed backing identity");
        return false;
    }

    String* inserted_string = parent_backing->items[insert_index].get_string();
    DomText* inserted_text = nullptr;
    if (parent->doc->input->ui_mode) {
        for (DomNode* candidate = parent->first_child; candidate;
             candidate = candidate->next_sibling) {
            if (candidate->is_text() &&
                candidate->as_text()->native_string == inserted_string) {
                inserted_text = candidate->as_text();
                break;
            }
        }
    } else {
        inserted_text = DomText::create(inserted_string, parent);
        if (inserted_text &&
            !((DomNode*)parent)->insert_before(inserted_text, ref_child)) {
            inserted_text = nullptr;
        }
    }
    if (!inserted_text) return false;
    dom_post_insert((DomNode*)parent, inserted_text);
    js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT, inserted_text,
                           (DomNode*)parent);
    return true;
}

static bool js_dom_text_is_backed_child(DomElement* parent, DomText* text) {
    if (!parent || !text || !text->native_string) return false;
    Element* backing = dom_element_to_element(parent);
    if (!backing) return false;
    for (int64_t i = 0; i < backing->length; i++) {
        Item item = backing->items[i];
        if (get_type_id(item) == LMD_TYPE_STRING &&
            item.get_string() == text->native_string) {
            return true;
        }
    }
    return false;
}

static bool js_dom_remove_backed_child(DomElement* parent, DomNode* child) {
    if (!parent || !child) return false;
    parent = js_dom_prepare_children_for_mutation(parent, child);
    if (!parent || child->parent != parent) return false;
    if (!child->is_element()) {
        if (child->is_text()) {
            DomText* text = (DomText*)child;
            if (js_dom_text_is_backed_child(parent, text)) {
                return dom_text_remove(text);
            }
            // Some DOM replacements have already removed the Mark string but
            // retain its wrapper until the next native mutation. Do not delete
            // an unrelated sibling a second time; finish that DOM unlink only.
            return ((DomNode*)parent)->remove_child(child);
        }
        if (child->is_comment()) return dom_comment_remove((DomComment*)child);
        return ((DomNode*)parent)->remove_child(child);
    }

    DomElement* child_elem = child->as_element();
    if (parent->is_synthetic() || !child_elem || child_elem->is_synthetic()) {
        return ((DomNode*)parent)->remove_child(child);
    }
    if (!parent->doc || !parent->doc->input) return false;

    int64_t child_index = js_dom_backed_child_index(parent, child_elem);
    if (child_index < 0) {
        // Positional DOM APIs can insert a DOM-only element between backed
        // siblings. Its removal must not delete an unrelated Mark child.
        return ((DomNode*)parent)->remove_child(child);
    }

    if (!js_dom_remove_backed_element_item(parent, child_elem, child_index)) {
        return false;
    }

    // MarkEditor changes the backing Element list but does not unlink the
    // dynamically-created DomNode sibling chain.  Leave that to the base DOM
    // unlinker so layout cannot traverse a removed wrapper through its former
    // siblings after a library's temporary-DOM capability probe finishes.
    return ((DomNode*)parent)->remove_child(child);
}

static bool js_dom_replace_document_element(DomElement* old_root,
                                            DomElement* replacement) {
    if (!old_root || !replacement || old_root == replacement ||
        !old_root->doc || old_root->doc->root != old_root ||
        replacement->is_synthetic()) {
        return old_root == replacement;
    }

    DomDocument* doc = old_root->doc;
    if (!doc->input || replacement->parent) return false;

    // Use the document proxy as the logical parent for adoption and DOM links;
    // the parser's #document Mark element remains the persistent backing root.
    DomElement* document_node = (DomElement*)js_dom_get_or_create_doc_node(doc);
    if (!document_node || !js_dom_prepare_cross_document_insertion(
            (DomNode*)replacement, document_node)) {
        return false;
    }

    Element* replacement_backing = dom_element_to_element(replacement);
    Element* old_backing = dom_element_to_element(old_root);
    if (!replacement_backing || !old_backing) return false;

    Element* input_root = doc->input->root.element;
    if (!input_root) return false;

    int64_t old_index = -1;
    bool input_is_document = false;
    TypeElmt* input_root_type = (TypeElmt*)input_root->type;
    if (input_root_type && input_root_type->name.str &&
        strcmp(input_root_type->name.str, "#document") == 0) {
        input_is_document = true;
        for (int64_t i = 0; i < input_root->length; i++) {
            Item child = input_root->items[i];
            if (get_type_id(child) == LMD_TYPE_ELEMENT &&
                child.element == old_backing) {
                old_index = i;
                break;
            }
        }
    } else if (input_root == old_backing) {
        old_index = 0;
    }
    if (old_index < 0) return false;

    dom_pre_remove((DomNode*)old_root, false);
    if (input_is_document) {
        MarkEditor editor(doc->input, EDIT_MODE_INLINE);
        Item result = editor.elmt_replace_child(
            {.element = input_root}, (int)old_index,
            {.element = replacement_backing});
        if (get_type_id(result) != LMD_TYPE_ELEMENT ||
            result.element != input_root) {
            log_error("js_dom_replace_document_element: backing root changed identity");
            return false;
        }
    } else {
        doc->input->root = {.element = replacement_backing};
    }

    DomNode* old_parent = old_root->parent;
    DomNode* old_next = old_root->next_sibling;
    DomNode* link_prev = nullptr;
    if (old_parent && old_parent->is_element()) {
        DomElement* parent = old_parent->as_element();
        for (DomNode* current = parent->first_child; current;
             current = current->next_sibling) {
            if (current == old_root) break;
            link_prev = current;
        }
        if (link_prev) {
            link_prev->next_sibling = (DomNode*)replacement;
        } else {
            parent->first_child = (DomNode*)replacement;
        }
        if (old_next) {
            old_next->prev_sibling = (DomNode*)replacement;
        } else {
            parent->last_child = (DomNode*)replacement;
        }
    }
    dom_node_cancel_detached(doc, (DomNode*)replacement);
    replacement->parent = old_parent;
    // Document proxies intentionally keep the documentElement's prev link
    // null even when a synthetic doctype precedes it.
    replacement->prev_sibling = old_root->prev_sibling;
    replacement->next_sibling = old_next;
    old_root->parent = nullptr;
    old_root->prev_sibling = nullptr;
    old_root->next_sibling = nullptr;

    doc->root = replacement;
    doc->html_root = replacement_backing;
    dom_node_schedule_detached(doc, (DomNode*)old_root);
    js_dom_mutation_notify(DOM_JS_MUTATION_TREE_REPLACE,
                           (DomNode*)replacement, old_parent);
    return true;
}

extern "C" Item js_dom_append_child_bridge(void* parent_ptr, Item child_arg) {
    DomElement* elem = (DomElement*)parent_ptr;
    if (!elem) return ItemNull;
    DomNode* child_node = (DomNode*)js_dom_unwrap_element(child_arg);
    if (!child_node) {
        log_error("js_dom_append_child_bridge: argument is not a DOM node");
        return ItemNull;
    }
    if (child_node->is_element()) {
        DomElement* child_elem = child_node->as_element();
        if (child_elem->tag_name && strcmp(child_elem->tag_name, "#document-fragment") == 0) {
            if (!js_dom_append_fragment_children(elem, child_elem)) return ItemNull;
            return child_arg;
        }
    }
    if (!js_dom_prepare_cross_document_insertion(child_node, elem)) return ItemNull;
    if (child_node->is_element()) {
        // DOM-created elements have independent Mark backings, so append through
        // DomElement to preserve the renderer's tree as well as DOM links.
        if (!js_dom_append_backed_element(elem, child_node)) return ItemNull;
    } else if (child_node->is_text()) {
        if (!js_dom_insert_backed_text(elem, child_node->as_text(), nullptr)) return ItemNull;
    } else if (!((DomNode*)elem)->append_child(child_node)) {
        return ItemNull;
    }
    dom_post_insert((DomNode*)elem, child_node);
    if (child_node->is_element() && child_node->as_element()->tag() == MARKUP_NAME_OPTION &&
        elem->tag() == MARKUP_NAME_SELECT) {
        _select_ask_for_reset(elem);
    }
    _select_refresh_cached_selected_options_for_node((DomNode*)elem);
    js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT, child_node, (DomNode*)elem);
    if (child_node->is_element()) {
        DomElement* ce = child_node->as_element();
        if (ce->tag_name && strcmp(ce->tag_name, "iframe") == 0) {
            _schedule_iframe_load(ce);
        }
    }
    return child_arg;
}

extern "C" Item js_dom_remove_child_bridge(void* parent_ptr, Item child_arg) {
    DomElement* elem = (DomElement*)parent_ptr;
    if (!elem) return ItemNull;
    DomNode* child_node = (DomNode*)js_dom_unwrap_element(child_arg);
    if (!child_node) {
        log_error("js_dom_remove_child_bridge: argument is not a DOM node");
        return ItemNull;
    }
    dom_pre_remove(child_node);
    if (!js_dom_remove_backed_child(elem, child_node)) return ItemNull;
    if (child_node->is_element() && child_node->as_element()->tag() == MARKUP_NAME_OPTION &&
        elem->tag() == MARKUP_NAME_SELECT) {
        _select_ask_for_reset(elem);
    }
    js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_REMOVE, child_node, (DomNode*)elem);
    return child_arg;
}

extern "C" Item js_dom_insert_before_bridge(void* parent_ptr, Item new_child_arg,
                                            Item ref_child_arg) {
    DomElement* elem = (DomElement*)parent_ptr;
    if (!elem) return ItemNull;
    DomNode* new_child = (DomNode*)js_dom_unwrap_element(new_child_arg);
    DomNode* ref_child = (DomNode*)js_dom_unwrap_element(ref_child_arg);
    if (!new_child) return ItemNull;
    DomNode* parent_node = (DomNode*)elem;
    if (new_child == ref_child) {
        // insertBefore(node, node) must stay a no-op; detaching first drops
        // keyed reconciler children and changes live-range behavior.
        return new_child_arg;
    }
    if (ref_child && ref_child->parent != parent_node) {
        log_error("js_dom_insert_before_bridge: reference node is not a child of target parent");
        return ItemNull;
    }
    if (new_child->is_element()) {
        DomElement* new_elem = new_child->as_element();
        if (new_elem->tag_name && strcmp(new_elem->tag_name, "#document-fragment") == 0) {
            bool mutated = false;
            DomNode* frag_child = new_elem->first_child;
            while (frag_child) {
                DomNode* next = frag_child->next_sibling;
                if (!js_dom_prepare_cross_document_insertion(frag_child, elem)) {
                    return ItemNull;
                }
                if (parent_node->insert_before(frag_child, ref_child)) {
                    dom_post_insert(parent_node, frag_child);
                    mutated = true;
                }
                frag_child = next;
            }
            if (mutated) js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_INSERT, (DomNode*)elem, (DomNode*)elem);
            return new_child_arg;
        }
    }
    if (!js_dom_prepare_cross_document_insertion(new_child, elem)) return ItemNull;
    if (!js_dom_insert_before_child(elem, new_child, ref_child)) {
        return ItemNull;
    }
    return new_child_arg;
}

extern "C" Item js_dom_remove_bridge(void* node_ptr) {
    DomNode* node = (DomNode*)node_ptr;
    if (!node) return ItemNull;
    if (node->parent) {
        DomNode* old_parent = node->parent;
        DomElement* owner_select = nullptr;
        if (node->is_element() && node->as_element()->tag() == MARKUP_NAME_OPTION) {
            owner_select = _option_owner_select(node->as_element());
        }
        // removal must notify live ranges before native sibling links change.
        dom_pre_remove(node);
        if (old_parent->is_element()) {
            // Node.remove() must share removeChild()'s dual-tree path. Leaving
            // the Mark entry behind after an SVG library tears down its Paper
            // makes a later textContent clear operate on a retired wrapper.
            if (!js_dom_remove_backed_child(old_parent->as_element(), node)) {
                return ItemNull;
            }
        } else if (!old_parent->remove_child(node)) {
            return ItemNull;
        }
        if (owner_select) _select_ask_for_reset(owner_select);
        js_dom_mutation_notify(DOM_JS_MUTATION_CHILD_REMOVE, node, old_parent);
    }
    return ItemNull;
}

extern "C" Item js_dom_adopt_node_bridge(Item node_arg) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(node_arg);
    if (!node) return ItemNull;
    // adoptNode detaches through remove() bookkeeping so live ranges/focus see the original parent.
    js_dom_remove_bridge((void*)node);
    return node_arg;
}

extern "C" Item js_dom_location_navigate_bridge(void* doc_ptr, Item next_url_item,
                                                 bool replace) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    const char* next_url = fn_to_cstr(next_url_item);
    if (doc && next_url && next_url[0]) {
        if (doc->pending_navigation_url) {
            mem_free(doc->pending_navigation_url);
        }
        doc->pending_navigation_url = mem_strdup(next_url, MEM_CAT_DOM);
        log_info("js-location-navigate: %s navigation to %s",
            replace ? "replace" : "assign", next_url);
    }
    return make_js_undefined();
}

extern "C" Item js_dom_document_open_bridge(void* doc_ptr) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    if (!doc) return js_get_document_object_value();
    DocState* state = doc->state ? doc->state : js_dom_current_state();
    if (state) {
        const char* exc = nullptr;
        if (!state_store_set_selection(state, NULL, NULL, &exc)) {
            log_debug("js_document_open: selection clear rejected: %s",
                      exc ? exc : "?");
        }
    }
    DomElement* body = document_body_element(doc);
    if (body) {
        clear_element_children_for_navigation(body);
        js_dom_mutation_notify(DOM_JS_MUTATION_TREE_REPLACE,
                               (DomNode*)body,
                               (DomNode*)body->parent);
    }
    return js_get_document_object_value();
}

extern "C" Item js_dom_document_write_bridge(void* doc_ptr, Item text_arg) {
    DomDocument* doc = (DomDocument*)doc_ptr;
    if (!doc || !doc->root) return ItemNull;
    const char* text = fn_to_cstr(text_arg);
    if (!text) return ItemNull;

    if (!js_dom_append_document_text(doc, text)) {
        log_debug("js_document_write_bridge: no body element found");
        return ItemNull;
    }
    log_debug("js_document_write_bridge: appended '%s' to body", text);
    return ItemNull;
}

extern "C" Item js_dom_normalize_bridge(void* elem_ptr) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem) return ItemNull;
    DomNode* child = elem->first_child;
    while (child) {
        if (child->is_text()) {
            DomText* text = child->as_text();
            while (child->next_sibling && child->next_sibling->is_text()) {
                DomText* next_text = child->next_sibling->as_text();
                uint32_t head_u16  = dom_text_utf16_length(text);
                uint32_t tail_u16  = dom_text_utf16_length(next_text);
                size_t new_len = text->length + next_text->length;
                char* combined = (char*)pool_alloc(elem->doc->document_pool, new_len + 1);
                if (!combined) break;
                if (text->text && text->length > 0)
                    memcpy(combined, text->text, text->length);
                if (next_text->text && next_text->length > 0)
                    memcpy(combined + text->length, next_text->text, next_text->length);
                combined[new_len] = '\0';
                String* s = dom_document_create_string(elem->doc, combined, new_len);
                pool_free(elem->doc->document_pool, combined);
                if (!s) break;
                // normalize() must replace the surviving Mark string before
                // removing its sibling; otherwise a later innerHTML rebuild
                // relinks both stale text entries into the DOM child list.
                if (!dom_text_replace_backed_string(text, s) &&
                    !dom_text_adopt_document_string(text, elem->doc, s)) break;
                DocState* st = js_dom_current_state();
                if (st) {
                    dom_mutation_text_replace_data(st, text, head_u16, 0, tail_u16);
                    dom_mutation_text_merge(st, text, next_text, head_u16);
                }
                DomNode* remove_node = child->next_sibling;
                dom_pre_remove(remove_node);
                if (!js_dom_remove_backed_child(elem, remove_node)) break;
            }
        }
        child = child->next_sibling;
    }
    return ItemNull;
}

static void js_dom_clone_content_attributes(DomElement* source, DomElement* clone) {
    if (!source || !clone || source->is_synthetic()) return;

    int attr_count = 0;
    const char** attr_names = source->attribute_names(&attr_count);
    for (int attr_index = 0; attr_index < attr_count; attr_index++) {
        const char* name = attr_names[attr_index];
        const char* value = source->get_attribute(name);
        if (value) {
            clone->set_attribute(name, value);
        } else if (source->has_attribute(name)) {
            // Boolean markup attributes have presence but no string payload.
            clone->set_attribute(name, "");
        }
    }
}

extern "C" Item js_dom_clone_node_bridge(void* elem_ptr, Item deep_arg, bool has_deep) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem) return ItemNull;
    bool deep = has_deep ? js_is_truthy(deep_arg) : false;
    // clones need a fresh native element; sharing the source buffer makes later
    // attribute removal on the clone dangle the original's native attribute data.
    MarkBuilder _clone_builder(elem->doc->input);
    Item _clean_elem = _clone_builder.element(elem->tag_name).final();
    DomElement* clone = dom_element_create(elem->doc, elem->tag_name, _clean_elem.element);
    if (!clone) return ItemNull;
    js_dom_clone_content_attributes(elem, clone);
    // DOM cloning must not copy source-bound wrapper caches; their host_data would
    // make clone.classList/style writes mutate the original element.
    clone->tag_id = elem->tag_id;
    if (deep) {
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) {
                Item child_clone = js_dom_clone_node_bridge(child->as_element(), deep_arg, has_deep);
                DomNode* cloned_child = (DomNode*)js_dom_unwrap_element(child_clone);
                if (cloned_child) {
                    ((DomNode*)clone)->append_child(cloned_child);
                }
            } else if (child->is_text()) {
                DomText* text = child->as_text();
                String* s = text->native_string;
                DomText* text_clone = dom_text_create(s, clone);
                if (text_clone) {
                    ((DomNode*)clone)->append_child((DomNode*)text_clone);
                }
            }
            child = child->next_sibling;
        }
    }
    return js_dom_wrap_element(clone);
}

extern "C" Item js_dom_replace_child_bridge(void* parent_ptr, Item new_child_arg,
                                            Item old_child_arg) {
    DomElement* elem = (DomElement*)parent_ptr;
    if (!elem) return ItemNull;
    DomNode* new_child = (DomNode*)js_dom_unwrap_element(new_child_arg);
    DomNode* old_child = (DomNode*)js_dom_unwrap_element(old_child_arg);
    if (!new_child || !old_child) return ItemNull;
    if (old_child->parent != (DomNode*)elem) return ItemNull;
    if (new_child == old_child) {
        // self-replace is tree-stable, but live ranges still observe the remove step.
        dom_pre_remove(old_child);
        return old_child_arg;
    }
    if (!js_dom_prepare_cross_document_insertion(new_child, elem)) return ItemNull;

    if (new_child->is_element() && old_child->is_element() && elem->doc &&
        elem->doc->input) {
        DomElement* new_elem = new_child->as_element();
        DomElement* old_elem = old_child->as_element();
        int64_t old_index = js_dom_backed_child_index(elem, old_elem);
        if (new_elem && old_elem && old_index >= 0) {
            if (new_child->parent && new_child->parent != (DomNode*)elem) {
                if (!new_child->parent->is_element() ||
                    !js_dom_remove_backed_child(new_child->parent->as_element(), new_child)) {
                    return ItemNull;
                }
            }
            dom_pre_remove(old_child);
            if (!dom_node_replace_in_parent(elem, old_child, new_child)) return ItemNull;
            MarkEditor editor(elem->doc->input, EDIT_MODE_INLINE);
            Item result = editor.elmt_replace_child({.element = dom_element_to_element(elem)},
                old_index, {.element = dom_element_to_element(new_elem)});
            if (get_type_id(result) != LMD_TYPE_ELEMENT ||
                result.element != dom_element_to_element(elem)) {
                // The DOM link is committed first so the UI relink keeps the
                // removed wrapper out of a later Mark-backed reconstruction.
                log_error("js_dom_replace_child: inline replace changed backing identity");
                return ItemNull;
            }
            dom_post_insert((DomNode*)elem, new_child);
            js_dom_observers_child_replace_notify((DomNode*)elem, new_child, old_child);
            js_dom_mutation_notify();
            return old_child_arg;
        }
    }
    ((DomNode*)elem)->insert_before(new_child, old_child);
    dom_post_insert((DomNode*)elem, new_child);
    dom_pre_remove(old_child);
    ((DomNode*)elem)->remove_child(old_child);
    // A replacement is one childList record containing both nodes; split
    // records leave observers unable to update a replaced editor root.
    js_dom_observers_child_replace_notify((DomNode*)elem, new_child, old_child);
    // dom_post_insert/dom_pre_remove already supplied precise incremental
    // ledger entries; this flush must not downgrade them to a broad replace.
    js_dom_mutation_notify();
    return old_child_arg;
}

typedef enum JsDomChildNodePlacement {
    JS_DOM_CHILD_NODE_REPLACE,
    JS_DOM_CHILD_NODE_AFTER,
    JS_DOM_CHILD_NODE_BEFORE,
} JsDomChildNodePlacement;

typedef struct JsDomRelativeArgument {
    DomNode* node;
    Item text;
    uint64_t* text_root;
} JsDomRelativeArgument;

static Item js_dom_child_node_insert_relative(DomNode* node, Item* args, int argc,
                                               JsDomChildNodePlacement placement) {
    if (!node) return (Item){.item = ITEM_JS_UNDEFINED};
    if (placement == JS_DOM_CHILD_NODE_REPLACE && node->is_element() &&
        node->as_element()->doc &&
        node->as_element()->doc->root == node->as_element()) {
        if (argc != 1) return (Item){.item = ITEM_JS_UNDEFINED};
        DomNode* replacement = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!replacement || !replacement->is_element() ||
            !js_dom_replace_document_element(node->as_element(),
                                              replacement->as_element())) {
            return (Item){.item = ITEM_JS_UNDEFINED};
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    DomNode* parent_node = node->parent;
    DomElement* parent = parent_node ? parent_node->as_element() : nullptr;
    if (!parent || !parent->doc) return (Item){.item = ITEM_JS_UNDEFINED};

    JsDomRelativeArgument* insertion_args = nullptr;
    if (argc > 0) {
        insertion_args = (JsDomRelativeArgument*)mem_calloc(
            (size_t)argc, sizeof(JsDomRelativeArgument), MEM_CAT_JS_RUNTIME);
        if (!insertion_args) return ItemNull;
    }
    RootFrame text_roots((size_t)argc);
    if (argc > 0 && !text_roots.valid()) {
        mem_free(insertion_args);
        return ItemError;
    }

    // Web IDL converts every non-Node argument before changing the tree.  In
    // particular, a later throwing ToString must not leave earlier arguments
    // inserted into the document.
    for (int i = 0; i < argc; i++) {
        DomNode* insertion = (DomNode*)js_dom_unwrap_element(args[i]);
        if (!insertion) {
            Item text_item = js_to_string(args[i]);
            if (item_is_error(text_item)) {
                mem_free(insertion_args);
                return text_item;
            }
            insertion_args[i].text = text_item;
            insertion_args[i].text_root = text_roots.take_slot();
            if (insertion_args[i].text_root) {
                *insertion_args[i].text_root = text_item.item;
            }
        }
        insertion_args[i].node = insertion;
    }

    // before() keeps the receiver as the insertion reference; after() starts
    // with its following sibling so sibling moves preserve their DOM order.
    DomNode* viable_next = placement == JS_DOM_CHILD_NODE_BEFORE
        ? node : node->next_sibling;
    while (viable_next) {
        bool moved_by_arguments = false;
        for (int i = 0; i < argc; i++) {
            if (insertion_args[i].node == viable_next) {
                moved_by_arguments = true;
                break;
            }
        }
        if (!moved_by_arguments) break;
        viable_next = viable_next->next_sibling;
    }

    if (placement == JS_DOM_CHILD_NODE_REPLACE) {
        // Remove through the backing-aware path so replaceWith() cannot leave
        // a stale Mark child that a later UI relink resurrects.
        dom_pre_remove(node);
        if (!js_dom_remove_backed_child(parent, node)) {
            mem_free(insertion_args);
            return (Item){.item = ITEM_JS_UNDEFINED};
        }
    }

    for (int i = 0; i < argc; i++) {
        DomNode* insertion = insertion_args[i].node;
        if (!insertion) {
            Item text = insertion_args[i].text_root
                ? (Item){.item = *insertion_args[i].text_root}
                : insertion_args[i].text;
            if (!js_dom_insert_text_before_child(parent, text, viable_next)) {
                mem_free(insertion_args);
                return (Item){.item = ITEM_JS_UNDEFINED};
            }
            continue;
        }
        DomDocument* source = js_dom_node_owner_document(insertion);
        if (source && source != parent->doc &&
            !js_dom_prepare_cross_document_insertion(insertion, parent)) {
            mem_free(insertion_args);
            return (Item){.item = ITEM_JS_UNDEFINED};
        }
        if (!js_dom_insert_before_child(parent, insertion, viable_next)) {
            mem_free(insertion_args);
            return (Item){.item = ITEM_JS_UNDEFINED};
        }
    }

    mem_free(insertion_args);
    _select_refresh_cached_selected_options_for_node(parent_node);
    js_dom_mutation_notify();
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_dom_replace_with_bridge(void* node_ptr, Item* args, int argc) {
    DomNode* node = (DomNode*)node_ptr;
    return js_dom_child_node_insert_relative(
        node, args, argc, JS_DOM_CHILD_NODE_REPLACE);
}

extern "C" Item js_dom_insert_adjacent_element_bridge(void* elem_ptr, Item position_arg,
                                                      Item new_node_arg) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem) return ItemNull;
    const char* position = fn_to_cstr(position_arg);
    DomNode* new_node = (DomNode*)js_dom_unwrap_element(new_node_arg);
    if (!position || !new_node) return ItemNull;
    if (new_node->parent) {
        dom_pre_remove(new_node);
        new_node->parent->remove_child(new_node);
    }
    DomNode* new_parent = nullptr;
    if (strcasecmp(position, "beforebegin") == 0) {
        if (elem->parent && elem->parent->is_element()) {
            elem->parent->insert_before(new_node, (DomNode*)elem);
            new_parent = elem->parent;
        }
    } else if (strcasecmp(position, "afterbegin") == 0) {
        ((DomNode*)elem)->insert_before(new_node, elem->first_child);
        new_parent = (DomNode*)elem;
    } else if (strcasecmp(position, "beforeend") == 0) {
        ((DomNode*)elem)->append_child(new_node);
        new_parent = (DomNode*)elem;
    } else if (strcasecmp(position, "afterend") == 0) {
        if (elem->parent && elem->parent->is_element()) {
            elem->parent->insert_before(new_node, elem->next_sibling);
            new_parent = elem->parent;
        }
    }
    if (new_parent) {
        dom_post_insert(new_parent, new_node);
        js_dom_mutation_notify();
    }
    return new_node_arg;
}

extern "C" Item js_dom_insert_adjacent_html_bridge(void* elem_ptr, Item position_arg,
                                                   Item html_arg) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem) return ItemNull;
    const char* position = fn_to_cstr(position_arg);
    const char* html_str = fn_to_cstr(html_arg);
    if (!position || !html_str || !elem->doc) return ItemNull;
    DomDocument* doc = elem->doc;
    DomElement* fragment = js_dom_parse_html_fragment(doc, html_str);
    if (!fragment) return ItemNull;

    DomElement* target_parent = nullptr;
    DomNode* ref_node = nullptr;
    if (strcasecmp(position, "beforebegin") == 0) {
        if (!elem->parent || !elem->parent->is_element()) return ItemNull;
        target_parent = elem->parent->as_element();
        ref_node = (DomNode*)elem;
    } else if (strcasecmp(position, "afterbegin") == 0) {
        target_parent = elem;
        ref_node = elem->first_child;
    } else if (strcasecmp(position, "beforeend") == 0) {
        target_parent = elem;
        ref_node = nullptr;
    } else if (strcasecmp(position, "afterend") == 0) {
        if (!elem->parent || !elem->parent->is_element()) return ItemNull;
        target_parent = elem->parent->as_element();
        ref_node = elem->next_sibling;
    } else {
        log_error("js_dom_insert_adjacent_html_bridge: invalid position '%s'", position);
        return ItemNull;
    }

    while (fragment->first_child) {
        DomNode* child = fragment->first_child;
        fragment->remove_child(child);
        if (ref_node)
            ((DomNode*)target_parent)->insert_before(child, ref_node);
        else
            ((DomNode*)target_parent)->append_child(child);
    }
    return ItemNull;
}

extern "C" Item js_dom_append_variadic_bridge(void* elem_ptr, Item* args, int argc) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem) return (Item){.item = ITEM_JS_UNDEFINED};
    for (int i = 0; i < argc; i++) {
        DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[i]);
        if (child_node) {
            if (child_node->is_element()) {
                DomElement* child_elem = child_node->as_element();
                if (child_elem->tag_name &&
                    strcmp(child_elem->tag_name, "#document-fragment") == 0) {
                    if (!js_dom_append_fragment_children(elem, child_elem)) {
                        return (Item){.item = ITEM_JS_UNDEFINED};
                    }
                    continue;
                }
            }
            // ParentNode.append is the path used by DOMParser consumers such
            // as HTMX; it must adopt foreign nodes before relinking them.
            if (!js_dom_prepare_cross_document_insertion(child_node, elem)) {
                return (Item){.item = ITEM_JS_UNDEFINED};
            }
            ((DomNode*)elem)->append_child(child_node);
            dom_post_insert((DomNode*)elem, child_node);
        } else {
            const char* text = fn_to_cstr(args[i]);
            if (text) {
                DomText* tn = DomText::create_copy(text, strlen(text), elem);
                if (tn) ((DomNode*)elem)->append_child(tn);
            }
        }
    }
    js_dom_mutation_notify();
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_dom_prepend_variadic_bridge(void* elem_ptr, Item* args, int argc) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem) return (Item){.item = ITEM_JS_UNDEFINED};
    DomNode* ref = elem->first_child;
    for (int i = 0; i < argc; i++) {
        DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[i]);
        if (child_node) {
            if (!js_dom_prepare_cross_document_insertion(child_node, elem)) {
                return (Item){.item = ITEM_JS_UNDEFINED};
            }
            ((DomNode*)elem)->insert_before(child_node, ref);
            dom_post_insert((DomNode*)elem, child_node);
            if (elem->tag() == MARKUP_NAME_SELECT && child_node->is_element() &&
                child_node->as_element()->tag() == MARKUP_NAME_OPTION) {
                DomElement* child_elem = child_node->as_element();
                if (_get_selectedness(child_elem) && !elem->has_attribute("multiple")) {
                    _select_select_only_option(elem, child_elem);
                } else {
                    _select_ask_for_reset(elem);
                }
            }
        } else {
            const char* text = fn_to_cstr(args[i]);
            if (text) {
                DomText* tn = DomText::create_copy(text, strlen(text), elem);
                if (tn) ((DomNode*)elem)->insert_before(tn, ref);
            }
        }
    }
    _select_refresh_cached_selected_options_for_node((DomNode*)elem);
    js_dom_mutation_notify();
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_dom_element_operation_impl(Item elem_item,
        JubeDomElementOperation operation, Item* args, int argc) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node) {
        log_error("js-dom-operation: receiver is not a DOM node");
        return ItemNull;
    }
    DomElement* elem = node->as_element(); // may be nullptr for text/comment nodes

    // HTMLSelectElement.remove(index) must be tested before generic
    // ChildNode.remove(); otherwise the select overload is shadowed and
    // remove(index) silently becomes node self-removal.
    if (operation == JUBE_DOM_REMOVE && elem && elem->tag_name && strcasecmp(elem->tag_name, "select") == 0) {
        // remove() with no args: spec calls ChildNode.remove(); but
        // HTMLSelectElement overrides — with no arg, do nothing per WPT.
        if (argc < 1) return ItemNull;
        TypeId t = get_type_id(args[0]);
        int idx = -1;
        if (t == LMD_TYPE_INT) idx = (int)it2i(args[0]); // INT_CAST_OK: index
        else if (t == LMD_TYPE_FLOAT) {
            // remove(index) may receive an inline float from JS Number
            // lowering, so decode through Item instead of dereferencing.
            idx = (int)it2d(args[0]); // INT_CAST_OK: index
        }
        if (idx < 0) return ItemNull;
        Item arr = js_array_new(0);
        _collect_options(elem->first_child, arr);
        if (idx >= js_array_length(arr)) return ItemNull;
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, idx));
        if (!opt || !opt->parent) return ItemNull;
        DomElement* parent = (DomElement*)opt->parent;
        DomNode* on = (DomNode*)opt;
        dom_pre_remove(on);
        if (on->prev_sibling) on->prev_sibling->next_sibling = on->next_sibling;
        else parent->first_child = on->next_sibling;
        if (on->next_sibling) on->next_sibling->prev_sibling = on->prev_sibling;
        else parent->last_child = on->prev_sibling;
        on->parent = nullptr; on->next_sibling = nullptr; on->prev_sibling = nullptr;
        js_dom_mutation_notify();
        return ItemNull;
    }

    // v12b: remove() — self-removal from parent (works on any node type)
    if (operation == JUBE_DOM_REMOVE) {
        if (node->parent) {
            DomElement* owner_select = nullptr;
            if (node->is_element() && node->as_element()->tag() == MARKUP_NAME_OPTION) {
                owner_select = _option_owner_select(node->as_element());
            }
            // Phase 8A: live-range cascade must run before the structural change.
            dom_pre_remove(node);
            node->parent->remove_child(node);
            if (owner_select) _select_ask_for_reset(owner_select);
            js_dom_mutation_notify();
        }
        return ItemNull;
    }

    // v12: contains(other) → boolean (works on any node type)
    if (operation == JUBE_DOM_CONTAINS) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        return js_dom_contains(elem_item, args[0]);
    }

    // Structural equality is a Node operation, so it must remain available to
    // text and comment wrappers before the Element-only method handling below.
    if (operation == JUBE_DOM_IS_EQUAL_NODE) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        return js_dom_is_equal_node(elem_item, args[0]);
    }

    // Node identity is a published capability, not a name-selected host call;
    // keep wrapper identity tied to the underlying node (D6.2.2v2).
    if (operation == JUBE_DOM_IS_SAME_NODE) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        return js_dom_is_same_node(elem_item, args[0]);
    }

    // CharacterData.replaceData(offset, count, data) — text nodes only.
    if (node->is_text() && operation == JUBE_DOM_REPLACE_DATA) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item count_arg = argc >= 2 ? args[1] : make_js_undefined();
        Item data_arg = argc >= 3 ? args[2] : make_js_undefined();
        return js_dom_text_replace_data_method(node->as_text(), offset_arg, count_arg, data_arg);
    }
    if (node->is_text() && operation == JUBE_DOM_INSERT_DATA) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item data_arg = argc >= 2 ? args[1] : make_js_undefined();
        return js_dom_text_insert_data_method(node->as_text(), offset_arg, data_arg);
    }
    if (node->is_text() && operation == JUBE_DOM_APPEND_DATA) {
        Item data_arg = argc >= 1 ? args[0] : make_js_undefined();
        return js_dom_text_append_data_method(node->as_text(), data_arg);
    }
    if (node->is_text() && operation == JUBE_DOM_DELETE_DATA) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item count_arg = argc >= 2 ? args[1] : make_js_undefined();
        return js_dom_text_delete_data_method(node->as_text(), offset_arg, count_arg);
    }
    if (node->is_text() && operation == JUBE_DOM_SUBSTRING_DATA) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item count_arg = argc >= 2 ? args[1] : make_js_undefined();
        return js_dom_text_substring_data_method(node->as_text(), offset_arg, count_arg);
    }

    // EventTarget is a Node capability, so text/comment nodes must reach the
    // same typed call targets before Element-only operations begin (D6.2.2v2).
    if (operation == JUBE_DOM_ADD_EVENT_LISTENER) {
        return argc >= 2
            ? js_dom_add_event_listener_bridge(elem_item, args[0], args[1], argc > 2 ? args[2] : ItemNull)
            : make_js_undefined();
    }
    if (operation == JUBE_DOM_REMOVE_EVENT_LISTENER) {
        return argc >= 2
            ? js_dom_remove_event_listener_bridge(elem_item, args[0], args[1], argc > 2 ? args[2] : ItemNull)
            : make_js_undefined();
    }
    if (operation == JUBE_DOM_DISPATCH_EVENT) {
        if (argc >= 1) return js_dom_dispatch_event_bridge(elem_item, args[0]);
        return (Item){.item = ITEM_FALSE};
    }

    // All remaining methods require an element node
    if (!elem) {
        log_debug("js-dom-operation: operation %d called on non-element node", (int)operation);
        return ItemNull;
    }

    // attachShadow(init) -> lightweight DocumentFragment-backed ShadowRoot.
    // Radiant does not render a full shadow tree yet, but WPT focus/editing
    // tests need a stable root object that supports appendChild/activeElement
    // while light DOM stays addressable.
    if (operation == JUBE_DOM_ATTACH_SHADOW) {
        const char* mode = "open";
        bool delegates_focus = false;
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
            Item mode_item = js_get_key_cstr(args[0], "mode");
            const char* mode_text = fn_to_cstr(mode_item);
            if (mode_text && mode_text[0]) mode = mode_text;
            Item delegates_item = js_get_key_cstr(args[0], "delegatesFocus");
            delegates_focus = js_is_truthy(delegates_item);
        }

        MarkBuilder builder(elem->doc ? elem->doc->input : nullptr);
        Item frag_item = builder.element("#document-fragment").final();
        Element* frag_elem = frag_item.element;
        DomElement* frag = dom_element_create(elem->doc, "#document-fragment", frag_elem);
        frag->set_shadow_host_element(elem);
        elem->set_shadow_root_element(frag);
        Item root = js_dom_wrap_element(frag);

        js_set_key_cstr(root, "host", elem_item);
        js_set_key_cstr(root, "mode", js_name_item(mode));
        js_set_key_cstr(root, "delegatesFocus", (Item){.item = b2it(delegates_focus)});

        Item exp_map = expando_get_or_create_map((DomNode*)elem);
        if (exp_map.item != ITEM_NULL) {
            Item visible_root = (strcasecmp(mode, "closed") == 0) ? ItemNull : root;
            js_set_key_cstr(exp_map, "shadowRoot", visible_root);
            js_set_key_cstr(exp_map, "__shadowRootInternal", root);
        }
        return root;
    }

    // getAttribute(name) → string or null
    if (operation == JUBE_DOM_GET_ATTRIBUTE) {
        if (argc < 1) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return ItemNull;
        if (js_dom_is_internal_attr(attr_name)) return ItemNull;
        const char* val = elem->get_attribute(attr_name);
        if (val) return js_name_item(val);
        if (elem->has_attribute(attr_name))
            return js_name_item("");
        return ItemNull;
    }

    // setAttribute(name, value)
    if (operation == JUBE_DOM_SET_ATTRIBUTE) {
        if (argc < 2) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        const char* attr_val = js_dom_to_attr_cstr(args[1]);
        if (!attr_name || !attr_val) return ItemNull;
        if (js_dom_is_internal_attr(attr_name)) return ItemNull;
        const char* old_value = elem->get_attribute(attr_name);
        elem->set_attribute(attr_name, attr_val);
        js_dom_compile_event_attr_to_expando(elem, attr_name, attr_val);
        js_dom_reinit_behavior_if_constraint_attr(elem, attr_name);
        if (_is_tag(elem, "option") && strcasecmp(attr_name, "selected") == 0) {
            DomElement* sel = _nearest_select_for_node((DomNode*)elem);
            if (sel && !sel->has_attribute("multiple")) _select_ask_for_reset(sel);
        }
        _select_refresh_cached_selected_options_for_node((DomNode*)elem);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem,
                               elem->parent, attr_name, old_value);
        return ItemNull;
    }

    // SVG/XLink attributes must retain namespace identity while also mirroring
    // their qualified name into the shared DOM attribute store used by the SVG
    // renderer. The legacy XLink branch is the only non-null namespace traced
    // by the drawing probes; other namespaces keep ordinary DOM semantics.
    if (operation == JUBE_DOM_SET_ATTRIBUTE_NS) {
        if (argc < 3) return ItemNull;
        const char* namespace_uri = fn_to_cstr(args[0]);
        const char* qualified_name = fn_to_cstr(args[1]);
        const char* value = js_dom_to_attr_cstr(args[2]);
        if (!qualified_name || !value) return ItemNull;
        const char* local_name = strrchr(qualified_name, ':');
        local_name = local_name ? local_name + 1 : qualified_name;
        const char* stored_name = qualified_name;
        char xlink_name[128];
        if (namespace_uri && strcmp(namespace_uri, "http://www.w3.org/1999/xlink") == 0) {
            snprintf(xlink_name, sizeof(xlink_name), "__lambda_xlink_%s", local_name);
            elem->set_attribute(xlink_name, value);
            // Renderer-side image/use resolution reads the ordinary SVG name.
            stored_name = local_name;
        }
        const char* old_value = elem->get_attribute(stored_name);
        elem->set_attribute(stored_name, value);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem,
                               elem->parent, stored_name, old_value);
        return ItemNull;
    }

    if (operation == JUBE_DOM_GET_ATTRIBUTE_NS) {
        if (argc < 2) return ItemNull;
        const char* namespace_uri = fn_to_cstr(args[0]);
        const char* local_name = fn_to_cstr(args[1]);
        if (!local_name) return ItemNull;
        char xlink_name[128];
        const char* lookup_name = local_name;
        if (namespace_uri && strcmp(namespace_uri, "http://www.w3.org/1999/xlink") == 0) {
            snprintf(xlink_name, sizeof(xlink_name), "__lambda_xlink_%s", local_name);
            lookup_name = xlink_name;
        }
        const char* value = elem->get_attribute(lookup_name);
        return value ? js_name_item(value) : ItemNull;
    }

    if (operation == JUBE_DOM_REMOVE_ATTRIBUTE_NS) {
        if (argc < 2) return ItemNull;
        const char* namespace_uri = fn_to_cstr(args[0]);
        const char* local_name = fn_to_cstr(args[1]);
        if (!local_name) return ItemNull;
        char xlink_name[128];
        const char* stored_name = local_name;
        if (namespace_uri && strcmp(namespace_uri, "http://www.w3.org/1999/xlink") == 0) {
            snprintf(xlink_name, sizeof(xlink_name), "__lambda_xlink_%s", local_name);
            elem->remove_attribute(xlink_name);
        }
        const char* old_value = elem->get_attribute(stored_name);
        elem->remove_attribute(stored_name);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem,
                               elem->parent, stored_name, old_value);
        return ItemNull;
    }

    // hasAttribute(name) → boolean
    if (operation == JUBE_DOM_HAS_ATTRIBUTE) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return (Item){.item = ITEM_FALSE};
        if (js_dom_is_internal_attr(attr_name)) return (Item){.item = ITEM_FALSE};
        bool has = elem->has_attribute(attr_name);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // getAttributeNames() → array of attribute name strings (DOM §4.9)
    if (operation == JUBE_DOM_GET_ATTRIBUTE_NAMES) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        Item arr_item = (Item){.array = arr};
        int attr_count = 0;
        const char** attr_names = elem->attribute_names(&attr_count);
        for (int i = 0; attr_names && i < attr_count; i++) {
            if (js_dom_is_internal_attr(attr_names[i])) continue;
            js_array_push(arr_item, js_name_item(attr_names[i]));
        }
        return arr_item;
    }

    // removeAttribute(name)
    if (operation == JUBE_DOM_REMOVE_ATTRIBUTE) {
        if (argc < 1) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return ItemNull;
        const char* old_value = elem->get_attribute(attr_name);
        elem->remove_attribute(attr_name);
        js_dom_clear_event_attr_expando(elem, attr_name);
        js_dom_reinit_behavior_if_constraint_attr(elem, attr_name);
        if (_is_tag(elem, "select") && strcasecmp(attr_name, "multiple") == 0) {
            _select_ask_for_reset(elem);
        }
        _select_refresh_cached_selected_options_for_node((DomNode*)elem);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem,
                               elem->parent, attr_name, old_value);
        return ItemNull;
    }

    // getElementsByTagName(tagName) — descendants of this element
    if (operation == JUBE_DOM_GET_ELEMENTS_BY_TAG_NAME) {
        if (argc < 1) return ItemNull;
        return js_dom_live_element_get_elements_by_tag_name_bridge((void*)elem, args[0]);
    }

    // getElementsByClassName(className) — descendants of this element
    if (operation == JUBE_DOM_GET_ELEMENTS_BY_CLASS_NAME) {
        if (argc < 1) return ItemNull;
        return js_dom_live_element_get_elements_by_class_name_bridge((void*)elem, args[0]);
    }

    // querySelector(selector) — from this element
    if (operation == JUBE_DOM_QUERY_SELECTOR) {
        if (argc < 1) return ItemNull;
        const char* sel_text = js_dom_to_dom_string_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->document_pool;
        CssSelectorGroup* selector_group = parse_css_selector_group(sel_text, pool);
        if (!selector_group) return js_dom_throw_syntax_error("Invalid selector");

        SelectorMatcher* matcher = js_dom_create_selector_matcher(elem->doc);
        // CSS Selectors defines :scope relative to the Element query receiver.
        // Without this binding, jQuery's scoped relative selectors match no descendants.
        selector_matcher_set_scope_element(matcher, elem);
        DomElement* found = js_dom_selector_group_find_first(
            matcher, selector_group, elem, false);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // querySelectorAll(selector) — from this element
    if (operation == JUBE_DOM_QUERY_SELECTOR_ALL) {
        if (argc < 1) return ItemNull;
        const char* sel_text = js_dom_to_dom_string_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->document_pool;
        CssSelectorGroup* selector_group = parse_css_selector_group(sel_text, pool);

        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;

        if (!selector_group) return js_dom_throw_syntax_error("Invalid selector");

        SelectorMatcher* matcher = js_dom_create_selector_matcher(elem->doc);
        // Keep :scope anchored to this Element for relative selector queries.
        selector_matcher_set_scope_element(matcher, elem);
        ArrayList* results = arraylist_new(16);
        if (!results) return (Item){.array = arr};
        js_dom_selector_group_collect_all(
            matcher, selector_group, elem, results, false);
        for (int i = 0; i < results->length; i++) {
            array_push(arr, js_dom_wrap_element((DomElement*)results->data[i]));
        }
        arraylist_free(results);
        return (Item){.array = arr};
    }

    // matches(selector) → boolean
    if (operation == JUBE_DOM_MATCHES) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* sel_text = js_dom_to_dom_string_cstr(args[0]);
        if (!sel_text || !elem->doc) return (Item){.item = ITEM_FALSE};

        Pool* pool = elem->doc->document_pool;
        CssSelectorGroup* selector_group = parse_css_selector_group(sel_text, pool);
        if (!selector_group) return js_dom_throw_syntax_error("Invalid selector");

        SelectorMatcher* matcher = js_dom_create_selector_matcher(elem->doc);
        MatchResult result;
        bool matched = selector_matcher_matches_group(matcher, selector_group, elem, &result);
        return (Item){.item = b2it(matched ? 1 : 0)};
    }

    // closest(selector) → element or null
    if (operation == JUBE_DOM_CLOSEST) {
        if (argc < 1) return ItemNull;
        const char* sel_text = js_dom_to_dom_string_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->document_pool;
        CssSelectorGroup* selector_group = parse_css_selector_group(sel_text, pool);
        if (!selector_group) return js_dom_throw_syntax_error("Invalid selector");

        SelectorMatcher* matcher = js_dom_create_selector_matcher(elem->doc);
        MatchResult mresult;
        DomElement* current = elem;
        while (current) {
            if (selector_matcher_matches_group(matcher, selector_group, current, &mresult)) {
                return js_dom_wrap_element(current);
            }
            DomNode* parent = current->parent;
            current = (parent && parent->is_element()) ? parent->as_element() : nullptr;
        }
        return ItemNull;
    }

    if (operation == JUBE_DOM_APPEND_CHILD) {
        return js_dom_append_child_bridge((void*)elem, argc > 0 ? args[0] : ItemNull);
    }

    if (operation == JUBE_DOM_REMOVE_CHILD) {
        return js_dom_remove_child_bridge((void*)elem, argc > 0 ? args[0] : ItemNull);
    }

    if (operation == JUBE_DOM_INSERT_BEFORE) {
        return js_dom_insert_before_bridge((void*)elem,
            argc > 0 ? args[0] : ItemNull, argc > 1 ? args[1] : ItemNull);
    }

    // hasChildNodes() → boolean
    if (operation == JUBE_DOM_HAS_CHILD_NODES) {
        bool has = (js_dom_first_script_visible_child(elem) != nullptr);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    if (operation == JUBE_DOM_NORMALIZE) {
        return js_dom_normalize_bridge((void*)elem);
    }

    if (operation == JUBE_DOM_CLONE_NODE) {
        return js_dom_clone_node_bridge((void*)elem,
            argc > 0 ? args[0] : ItemNull, argc > 0);
    }

    // v12b: replaceChild(newChild, oldChild)
    if (operation == JUBE_DOM_REPLACE_CHILD) {
        if (argc < 2) return ItemNull;
        return js_dom_replace_child_bridge(elem, args[0], args[1]);
    }

    // replaceWith(...nodes) — replace this node in its parent's children with
    // the given nodes (or strings, coerced to text). Per DOM §4.2.7 "ChildNode".
    // Per spec this performs a removal + (pre-)insert, which is observably
    // distinct from a no-op for live ranges anchored inside `node` even when
    // the only argument is `node` itself.
    if (operation == JUBE_DOM_REPLACE_WITH) {
        return js_dom_replace_with_bridge((void*)node, args, argc);
    }

    if (operation == JUBE_DOM_AFTER) {
        return js_dom_child_node_insert_relative(
            node, args, argc, JS_DOM_CHILD_NODE_AFTER);
    }

    if (operation == JUBE_DOM_BEFORE) {
        return js_dom_child_node_insert_relative(
            node, args, argc, JS_DOM_CHILD_NODE_BEFORE);
    }

    // v12b: toggleAttribute(name [, force])
    if (operation == JUBE_DOM_TOGGLE_ATTRIBUTE) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return (Item){.item = ITEM_FALSE};

        bool has = elem->has_attribute(attr_name);
        const char* old_value = has ? elem->get_attribute(attr_name) : nullptr;
        bool should_have;
        if (argc >= 2) {
            should_have = js_is_truthy(args[1]);
        } else {
            should_have = !has; // toggle
        }

        if (should_have && !has) {
            elem->set_attribute(attr_name, "");
        } else if (!should_have && has) {
            elem->remove_attribute(attr_name);
            if (_is_tag(elem, "select") && strcasecmp(attr_name, "multiple") == 0) {
                _select_ask_for_reset(elem);
            }
        }
        if (should_have != has) {
            // MutationObserver filters depend on toggleAttribute preserving the
            // changed name; a null name turns self-filtered updates into feedback loops.
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem,
                                   elem->parent, attr_name, old_value);
        }
        return (Item){.item = b2it(should_have ? 1 : 0)};
    }

    // v12b: insertAdjacentElement(position, newElement)
    if (operation == JUBE_DOM_INSERT_ADJACENT_ELEMENT) {
        if (argc < 2) return ItemNull;
        return js_dom_insert_adjacent_element_bridge((void*)elem, args[0], args[1]);
    }

    // v12b: insertAdjacentHTML(position, text)
    if (operation == JUBE_DOM_INSERT_ADJACENT_HTML) {
        if (argc < 2) return ItemNull;
        return js_dom_insert_adjacent_html_bridge((void*)elem, args[0], args[1]);
    }

    // SVG member records land here with an explicit operation; keeping their
    // native-object construction in this one target prevents property reads
    // and calls from diverging (D6.2.2v2).
    if (js_dom_element_is_svg(elem) && operation == JUBE_DOM_CREATE_SVG_MATRIX) {
        return js_dom_svg_create_matrix();
    }
    if (js_dom_element_is_svg(elem) && operation == JUBE_DOM_CREATE_SVG_POINT) {
        return js_dom_svg_create_point();
    }
    if (js_dom_element_is_svg(elem) && operation == JUBE_DOM_CREATE_SVG_TRANSFORM) {
        return js_dom_svg_create_transform();
    }
    if (js_dom_element_is_svg(elem) && operation == JUBE_DOM_CREATE_SVG_TRANSFORM_FROM_MATRIX) {
        return argc >= 1 ? js_dom_svg_create_transform_from_matrix(args[0]) : ItemNull;
    }
    if (js_dom_element_is_svg(elem) && operation == JUBE_DOM_GET_BBOX) {
        return js_dom_svg_get_bbox_for_element(elem);
    }
    if (js_dom_element_is_svg(elem) && operation == JUBE_DOM_GET_CTM) {
        return js_dom_svg_make_matrix(js_dom_svg_ctm(elem, false));
    }
    if (js_dom_element_is_svg(elem) && operation == JUBE_DOM_GET_SCREEN_CTM) {
        return js_dom_svg_make_matrix(js_dom_svg_ctm(elem, true));
    }

    // getBoundingClientRect() — returns {top, left, right, bottom, width, height}
    // Walks parent chain to compute absolute position.
    if (operation == JUBE_DOM_GET_BOUNDING_CLIENT_RECT) {
        return js_dom_get_bounding_client_rect_bridge((void*)elem);
    }

    if (operation == JUBE_DOM_SCROLL_INTO_VIEW) {
        return js_dom_scroll_into_view_bridge((void*)elem);
    }

    if (operation == JUBE_DOM_SCROLL ||
        operation == JUBE_DOM_SCROLL_TO ||
        operation == JUBE_DOM_SCROLL_BY) {
        return js_dom_scroll_operation_bridge(elem_item, operation, args, argc);
    }

    // compareDocumentPosition(otherNode) — returns bitmask per W3C DOM spec
    if (operation == JUBE_DOM_COMPARE_DOCUMENT_POSITION) {
        if (argc < 1) return (Item){.item = i2it(0)};
        DomNode* other = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!other) return (Item){.item = i2it(1)}; // disconnected
        if (node == other) return (Item){.item = i2it(0)};
        // check if node is ancestor of other (node contains other → 16+4)
        for (DomNode* p = other->parent; p; p = p->parent) {
            if (p == node) return (Item){.item = i2it(16 + 4)};
        }
        // check if other is ancestor of node (other contains node → 8+2)
        for (DomNode* p = node->parent; p; p = p->parent) {
            if (p == other) return (Item){.item = i2it(8 + 2)};
        }
        // find common ancestor and determine document order
        // collect ancestors of node
        DomNode* a_path[256]; int a_depth = 0;
        for (DomNode* p = node; p && a_depth < 256; p = p->parent) a_path[a_depth++] = p;
        DomNode* b_path[256]; int b_depth = 0;
        for (DomNode* p = other; p && b_depth < 256; p = p->parent) b_path[b_depth++] = p;
        // check if same tree (roots must match)
        if (a_depth == 0 || b_depth == 0 || a_path[a_depth-1] != b_path[b_depth-1]) {
            return (Item){.item = i2it(1)}; // disconnected
        }
        // walk down from common ancestor to find order
        int ai = a_depth - 1, bi = b_depth - 1;
        while (ai > 0 && bi > 0 && a_path[ai-1] == b_path[bi-1]) { ai--; bi--; }
        // a_path[ai] and b_path[bi] are siblings under common ancestor
        DomNode* a_child = (ai > 0) ? a_path[ai-1] : node;
        DomNode* b_child = (bi > 0) ? b_path[bi-1] : other;
        // scan siblings to determine order
        for (DomNode* s = a_child->next_sibling; s; s = s->next_sibling) {
            if (s == b_child) return (Item){.item = i2it(4)}; // other follows
        }
        return (Item){.item = i2it(2)}; // other precedes
    }

    // append(...nodes) — ParentNode.append(), accepts multiple args and strings
    if (operation == JUBE_DOM_APPEND) {
        return js_dom_append_variadic_bridge((void*)elem, args, argc);
    }

    // prepend(...nodes) — ParentNode.prepend()
    if (operation == JUBE_DOM_PREPEND) {
        return js_dom_prepend_variadic_bridge((void*)elem, args, argc);
    }

    // getClientRects() — returns array containing single DOMRect (same as getBoundingClientRect)
    if (operation == JUBE_DOM_GET_CLIENT_RECTS) {
        return js_dom_get_client_rects_bridge((void*)elem);
    }

    if (operation == JUBE_DOM_TEXT_CONTROL_CARET_BOUNDS &&
        tc_is_text_control_elem(elem)) {
        return js_dom_text_control_caret_bounds(elem);
    }
    if (operation == JUBE_DOM_TEXT_CONTROL_BOUNDARY_FROM_POINT &&
        tc_is_text_control_elem(elem)) {
        Item x_arg = argc >= 1 ? args[0] : (Item){.item = i2it(0)};
        Item y_arg = argc >= 2 ? args[1] : (Item){.item = i2it(0)};
        return js_dom_text_control_boundary_from_point(elem, x_arg, y_arg);
    }
    if (operation == JUBE_DOM_BOUNDARY_FROM_POINT) {
        Item x_arg = argc >= 1 ? args[0] : (Item){.item = i2it(0)};
        Item y_arg = argc >= 2 ? args[1] : (Item){.item = i2it(0)};
        Item behavior_arg = argc >= 3 ? args[2] : make_js_undefined();
        return js_dom_boundary_from_point(elem, x_arg, y_arg, behavior_arg);
    }

    // focus() / blur() — stubs for headless mode
    if (operation == JUBE_DOM_FOCUS || operation == JUBE_DOM_BLUR) {
        return js_dom_focus_method_bridge((void*)elem, operation == JUBE_DOM_FOCUS);
    }

    // HTMLElement.click() — synthesise and dispatch a `click` MouseEvent
    // (bubbles, cancelable, composed). Per the HTML spec §6.4.4, calling
    // click() on a disabled form control is a no-op (no event fires).
    if (operation == JUBE_DOM_CLICK) {
        return js_dom_click_method_bridge(elem_item);
    }

    if (operation == JUBE_DOM_SHOW_POPOVER) {
        // programmatic opening must use the same state transition as a popover button
        if (elem) js_dom_activate_popover((void*)elem, 1);
        return make_js_undefined();
    }

    if (operation == JUBE_DOM_HIDE_POPOVER) {
        // programmatic closing must publish the same live-state transition as a popover button
        if (elem) js_dom_activate_popover((void*)elem, 2);
        return make_js_undefined();
    }

    if (operation == JUBE_DOM_SHOW_MODAL) {
        if (elem && elem->tag_id == MARKUP_NAME_DIALOG) {
            // showModal makes the dialog open and establishes modal top-layer state
            elem->set_dialog_modal(true);
            elem->set_attribute("open", "");
            js_dom_notify_mutation(DOM_JS_MUTATION_ATTRIBUTE,
                                   (DomNode*)elem, elem->parent);
        }
        return make_js_undefined();
    }

    // getElementById(id) — for DocumentFragment hosts. The DOM spec puts
    // this on NonElementParentNode (Document + DocumentFragment). For
    // every other element it is undefined; we still implement it as a
    // tree-scoped lookup since several WPT tests use the synthetic
    // `#document-fragment` element returned by `createDocumentFragment`.
    if (operation == JUBE_DOM_GET_ELEMENT_BY_ID) {
        if (argc < 1) return ItemNull;
        const char* id = fn_to_cstr(args[0]);
        if (!id) return ItemNull;
        DomElement* found = js_dom_find_element_by_id(elem, id);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // setSelectionRange(start, end [, direction]) — text controls only.
    if (operation == JUBE_DOM_SET_SELECTION_RANGE && tc_is_text_control_elem(elem)) {
        // preserve the legacy DOM fallback no-op when required offsets are absent.
        if (argc < 2) return make_js_undefined();
        return js_dom_text_control_set_selection_range_bridge((void*)elem,
            argc >= 1 ? args[0] : make_js_undefined(),
            argc >= 2 ? args[1] : make_js_undefined(),
            argc >= 3 ? args[2] : make_js_undefined());
    }

    // setRangeText(replacement [, start, end, selectionMode]) — text controls only.
    if (operation == JUBE_DOM_SET_RANGE_TEXT && tc_is_text_control_elem(elem)) {
        return js_dom_text_control_set_range_text_bridge((void*)elem,
            argc >= 1 ? args[0] : make_js_undefined(),
            argc >= 2 ? args[1] : make_js_undefined(),
            argc >= 3 ? args[2] : make_js_undefined(),
            argc >= 4 ? args[3] : make_js_undefined());
    }

    // select() — text controls only. Selects the entire value and focuses.
    if (operation == JUBE_DOM_SELECT && tc_is_text_control_elem(elem)) {
        return js_dom_text_control_select_bridge((void*)elem);
    }

    // ----------------------------------------------------------------
    // F-4: Constraint Validation methods
    // ----------------------------------------------------------------

    // setCustomValidity(message): store custom validity message
    if (operation == JUBE_DOM_SET_CUSTOM_VALIDITY) {
        FormControlProp* f = tc_get_or_create_form(elem);
        const char* msg = (argc > 0) ? fn_to_cstr(args[0]) : "";
        if (!msg) msg = "";
        if (f->custom_validity_msg) { mem_free(f->custom_validity_msg); }
        f->custom_validity_msg = mem_strdup(msg, MEM_CAT_DOM);
        return make_js_undefined();
    }

    // ----------------------------------------------------------------
    // F-3: form.reset() — fire `reset` event (cancelable), then run reset
    // algorithm on all listed form controls.
    // ----------------------------------------------------------------
    if (operation == JUBE_DOM_RESET && elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) {
        return js_dom_form_reset_bridge(elem_item);
    }

    if (operation == JUBE_DOM_SUBMIT && elem->tag_name &&
        strcasecmp(elem->tag_name, "form") == 0) {
        return js_dom_form_submit_bridge(elem_item);
    }
    if (operation == JUBE_DOM_REQUEST_SUBMIT && elem->tag_name &&
        strcasecmp(elem->tag_name, "form") == 0) {
        return js_dom_form_request_submit_bridge(elem_item,
            argc >= 1 ? args[0] : make_js_undefined());
    }

    // checkValidity(): fire invalid event if not valid, return bool
    if (operation == JUBE_DOM_CHECK_VALIDITY) {
        return js_dom_check_validity_bridge(elem_item);
    }

    // reportValidity(): same as checkValidity() in headless (no UI feedback)
    if (operation == JUBE_DOM_REPORT_VALIDITY) {
        return js_dom_report_validity_bridge(elem_item);
    }

    // HTMLSelectElement.namedItem(name) — search options by id/name.
    if (operation == JUBE_DOM_NAMED_ITEM && elem->tag_name && strcasecmp(elem->tag_name, "select") == 0) {
        if (argc < 1) return ItemNull;
        const char* name = fn_to_cstr(args[0]);
        if (!name || !*name) return ItemNull;
        Item arr = js_array_new(0);
        _collect_options(elem->first_child, arr);
        int64_t n = js_array_length(arr);
        for (int64_t i = 0; i < n; i++) {
            Item item = js_elements_get_int(arr, i);
            DomElement* opt = (DomElement*)js_dom_unwrap_element(item);
            if (!opt) continue;
            const char* id = opt->get_attribute("id");
            if (id && strcmp(id, name) == 0) return item;
            const char* nm = opt->get_attribute("name");
            if (nm && strcmp(nm, name) == 0) return item;
        }
        return ItemNull;
    }

    // HTMLSelectElement.add(element, before) — insert option/optgroup
    if (operation == JUBE_DOM_ADD && elem->tag_name && strcasecmp(elem->tag_name, "select") == 0) {
        if (argc < 1) return ItemNull;
        DomElement* new_opt = (DomElement*)js_dom_unwrap_element(args[0]);
        if (!new_opt || !new_opt->tag_name) return ItemNull;
        // Per spec, must be HTMLOptionElement or HTMLOptGroupElement, otherwise TypeError.
        if (strcasecmp(new_opt->tag_name, "option") != 0 &&
            strcasecmp(new_opt->tag_name, "optgroup") != 0) {
            return ItemNull;
        }
        // If new_opt is an ancestor of elem, must throw HierarchyRequestError.
        for (DomNode* p = (DomNode*)elem; p; p = p->parent) {
            if ((DomElement*)p == new_opt) {
                Item n = js_name_item("HierarchyRequestError");
                Item m = js_name_item(
                    "Failed to execute 'add' on 'HTMLSelectElement': "
                    "The new child element contains the parent.");
                return js_throw_value(js_new_error_with_name(n, m));
            }
        }
        // before: null/undefined/missing/-1 → append; else if number → option at index;
        // else if element → that element.
        DomElement* before_elem = nullptr;
        bool append_at_end = true;
        if (argc >= 2 && args[1].item != ITEM_NULL && !is_js_undefined(args[1])) {
            TypeId bt = get_type_id(args[1]);
            if (bt == LMD_TYPE_INT) {
                int idx = (int)it2i(args[1]); // INT_CAST_OK: index
                if (idx >= 0) {
                    Item arr = js_array_new(0);
                    _collect_options(elem->first_child, arr);
                    if (idx < js_array_length(arr)) {
                        before_elem = (DomElement*)js_dom_unwrap_element(js_elements_get_int(arr, idx));
                        append_at_end = false;
                    }
                }
            } else {
                DomElement* be = (DomElement*)js_dom_unwrap_element(args[1]);
                if (be) { before_elem = be; append_at_end = false; }
            }
        }
        // No-op if before == new_opt (per spec).
        if (before_elem == new_opt) return ItemNull;
        // Detach new_opt from current parent first.
        if (new_opt->parent) {
            DomElement* op = (DomElement*)new_opt->parent;
            if (new_opt->prev_sibling) new_opt->prev_sibling->next_sibling = new_opt->next_sibling;
            else op->first_child = new_opt->next_sibling;
            if (new_opt->next_sibling) new_opt->next_sibling->prev_sibling = new_opt->prev_sibling;
            else op->last_child = new_opt->prev_sibling;
            new_opt->next_sibling = nullptr;
            new_opt->prev_sibling = nullptr;
            new_opt->parent = nullptr;
        }
        // Insert into elem.
        new_opt->parent = elem;
        if (append_at_end || !before_elem) {
            if (!elem->first_child) {
                elem->first_child = new_opt;
                elem->last_child = new_opt;
            } else {
                DomNode* last = elem->last_child;
                last->next_sibling = new_opt;
                new_opt->prev_sibling = last;
                elem->last_child = new_opt;
            }
        } else {
            // Insert before before_elem (which must be a child of elem,
            // or a descendant — for nested optgroup case we still insert
            // before its position in the option list, but DOM-wise we
            // insert before the closest ancestor that's a direct child).
            DomNode* anchor = (DomNode*)before_elem;
            while (anchor && anchor->parent != elem) anchor = anchor->parent;
            if (!anchor) {
                // Not in this select — append at end.
                if (!elem->first_child) {
                    elem->first_child = new_opt;
                    elem->last_child = new_opt;
                } else {
                    DomNode* last = elem->last_child;
                    last->next_sibling = new_opt;
                    new_opt->prev_sibling = last;
                    elem->last_child = new_opt;
                }
            } else {
                new_opt->prev_sibling = anchor->prev_sibling;
                new_opt->next_sibling = anchor;
                if (anchor->prev_sibling) anchor->prev_sibling->next_sibling = new_opt;
                else elem->first_child = new_opt;
                anchor->prev_sibling = new_opt;
            }
        }
        js_dom_mutation_notify();
        return ItemNull;
    }
    log_debug("js-dom-operation: unsupported operation %d", (int)operation);
    return ItemNull;
}

// ============================================================================
// classList API (v12)
// ============================================================================

static Item js_classlist_operation(Item elem_item, JsClassListOperation operation,
                                   Item* args, int argc) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        log_error("js-classlist-operation: not a DOM element");
        return ItemNull;
    }

    // add(className, ...)
    if (operation == JS_CLASSLIST_ADD) {
        for (int i = 0; i < argc; i++) {
            const char* cls = fn_to_cstr(args[i]);
            if (cls) elem->add_class(cls);
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return ItemNull;
    }

    // remove(className, ...)
    if (operation == JS_CLASSLIST_REMOVE) {
        for (int i = 0; i < argc; i++) {
            const char* cls = fn_to_cstr(args[i]);
            if (cls) elem->remove_class(cls);
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return ItemNull;
    }

    // toggle(className [, force]) → boolean
    if (operation == JS_CLASSLIST_TOGGLE) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return (Item){.item = ITEM_FALSE};

        if (argc >= 2) {
            // force parameter: add if truthy, remove if falsy
            bool force = js_is_truthy(args[1]);
            if (force) {
                elem->add_class(cls);
                js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
                return (Item){.item = ITEM_TRUE};
            } else {
                elem->remove_class(cls);
                js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
                return (Item){.item = ITEM_FALSE};
            }
        }
        // no force: toggle
        bool result = elem->toggle_class(cls);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return (Item){.item = b2it(result ? 1 : 0)};
    }

    // contains(className) → boolean
    if (operation == JS_CLASSLIST_CONTAINS) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return (Item){.item = ITEM_FALSE};
        bool has = elem->has_class(cls);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // item(index) → string or null
    if (operation == JS_CLASSLIST_ITEM) {
        if (argc < 1) return ItemNull;
        int64_t idx = it2i(args[0]);
        if (idx < 0 || idx >= elem->class_count) return ItemNull;
        return js_name_item(elem->class_names[idx]);
    }

    // replace(oldClass, newClass) → boolean
    if (operation == JS_CLASSLIST_REPLACE) {
        if (argc < 2) return (Item){.item = ITEM_FALSE};
        const char* old_cls = fn_to_cstr(args[0]);
        const char* new_cls = fn_to_cstr(args[1]);
        if (!old_cls || !new_cls) return (Item){.item = ITEM_FALSE};
        if (!elem->has_class(old_cls)) return (Item){.item = ITEM_FALSE};
        elem->remove_class(old_cls);
        elem->add_class(new_cls);
        js_dom_mutation_notify();
        return (Item){.item = ITEM_TRUE};
    }

    // toString() → space-separated class string
    if (operation == JS_CLASSLIST_TO_STRING) {
        return js_classlist_value_item(elem);
    }

    if (operation == JS_CLASSLIST_ITERATOR) {
        Item values = js_array_new(0);
        for (int i = 0; i < elem->class_count; i++) {
            js_array_push(values, js_name_item(elem->class_names[i]));
        }
        return js_get_iterator(values);
    }

    log_error("js-classlist-operation: invalid operation %d", (int)operation);
    return ItemNull;
}

extern "C" Item js_classlist_get_property(Item elem_item, Item prop_name) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    // length
    if (strcmp(prop, "length") == 0) {
        return (Item){.item = i2it((int64_t)elem->class_count)};
    }

    // value — space-separated class string
    if (strcmp(prop, "value") == 0) {
        return js_classlist_value_item(elem);
    }

    // numeric index → item(index)
    // (not common but classList[0] should work)

    log_debug("js_classlist_get_property: unknown property '%s'", prop);
    return ItemNull;
}

// ============================================================================
// dataset API (v12)
// ============================================================================

// Helper: convert camelCase to data-kebab-case attribute name
// e.g., "fooBar" → "data-foo-bar"
static void camel_to_data_attr(const char* camel, char* buf, size_t buf_size) {
    size_t pos = 0;
    // prefix with "data-"
    const char* prefix = "data-";
    size_t plen = 5;
    if (buf_size <= plen) { buf[0] = '\0'; return; }
    memcpy(buf, prefix, plen);
    pos = plen;

    for (const char* p = camel; *p && pos < buf_size - 2; p++) {
        if (isupper((unsigned char)*p)) {
            buf[pos++] = '-';
            buf[pos++] = (char)tolower((unsigned char)*p);
        } else {
            buf[pos++] = *p;
        }
    }
    buf[pos] = '\0';
}

extern "C" Item js_dataset_get_property(Item elem_item, Item prop_name) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    char attr_name[256];
    camel_to_data_attr(prop, attr_name, sizeof(attr_name));

    const char* val = elem->get_attribute(attr_name);
    if (val) {
        return js_name_item(val);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_dataset_set_property(Item elem_item, Item prop_name, Item value) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    const char* prop = fn_to_cstr(prop_name);
    const char* val_str = fn_to_cstr(value);
    if (!prop || !val_str) return ItemNull;

    char attr_name[256];
    camel_to_data_attr(prop, attr_name, sizeof(attr_name));

    const char* old_value = elem->get_attribute(attr_name);
    elem->set_attribute(attr_name, val_str);
    // Dataset writes must expose their data-* name so MutationObserver
    // consumers can suppress their own bookkeeping mutations.
    js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem,
                           elem->parent, attr_name, old_value);
    return value;
}

extern "C" bool js_dom_dataset_set_object_property(Item dataset, Item key,
                                                       Item value) {
    JS_ROOTS(roots, dataset_root, dataset, key_root, key, value_root, value, owner_root, ItemNull);
    if (get_type_id(key_root.get()) != LMD_TYPE_STRING) return false;
    String* key_string = it2s(key_root.get());
    if (!key_string ||
        (key_string->len == 24 &&
         strncmp(key_string->chars, "__lambda_dataset_element", 24) == 0)) {
        return false;
    }
    // Property lookup may collect while an async handler owns the only
    // references to this dataset view; keep the receiver and operands precise.
    owner_root.set(js_get_key_cstr(dataset_root.get(), "__lambda_dataset_element"));
    if (!js_dom_unwrap_element(owner_root.get())) return false;
    js_dataset_set_property(owner_root.get(), key_root.get(), value_root.get());
    return true;
}

// ============================================================================
// location API (v12) — document.URL / document.location
// ============================================================================

extern "C" Item js_location_get_property(Item prop_name) {
    if (!_js_current_document) {
        log_debug("js_location_get_property: no document set");
        return js_name_item("");
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return js_name_item("");

    Url* url = _js_current_document->url;
    if (!url) {
        log_debug("js_location_get_property: document has no URL");
        return js_name_item("");
    }

    {
        Item url_component = ItemNull;
        if (js_url_component_item(url, prop, &url_component)) return url_component;
    }

    log_debug("js_location_get_property: unknown property '%s'", prop);
    return js_name_item("");
}

static Url* js_dom_make_fallback_url(const char* raw_url) {
    if (!raw_url) return nullptr;

    Url* url = url_create();
    if (!url) return nullptr;

    const char* query = strchr(raw_url, '?');
    const char* hash = strchr(raw_url, '#');
    const char* end = raw_url + strlen(raw_url);
    const char* path_end = end;
    if (query && (!hash || query < hash)) path_end = query;
    else if (hash) path_end = hash;

    size_t pathname_len = (size_t)(path_end - raw_url);
    char* pathname = (char*)mem_alloc(pathname_len + 1, MEM_CAT_DOM);
    memcpy(pathname, raw_url, pathname_len);
    pathname[pathname_len] = '\0';

    url->href = url_create_string(raw_url);
    url->pathname = url_create_string(pathname);
    url->search = url_create_string(query ? query : "");
    url->hash = url_create_string(hash ? hash : "");
    url->protocol = url_create_string("");
    url->origin = url_create_string("");
    url->host = url_create_string("");
    url->hostname = url_create_string("");
    url->port = url_create_string("");
    url->is_valid = true;

    mem_free(pathname);
    return url;
}

// ============================================================================
// Node.contains() (v12)
// ============================================================================

extern "C" Item js_dom_contains(Item elem_item, Item other_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    DomNode* other = (DomNode*)js_dom_unwrap_element(other_item);
    if (!node || !other) return (Item){.item = ITEM_FALSE};

    // a node contains itself per spec
    if (node == other) return (Item){.item = ITEM_TRUE};

    // walk up from other's parent
    DomNode* current = other->parent;
    while (current) {
        if (current == node) return (Item){.item = ITEM_TRUE};
        current = current->parent;
    }
    return (Item){.item = ITEM_FALSE};
}

// ============================================================================
// Node.isEqualNode()
// ============================================================================

static bool js_dom_equal_cstr(const char* left, const char* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    return strcmp(left, right) == 0;
}

static bool js_dom_equal_children(DomElement* left, DomElement* right);

static bool js_dom_nodes_are_equal(DomNode* left, DomNode* right) {
    if (left == right) return true;
    if (!left || !right || left->node_type != right->node_type) return false;

    if (left->is_text()) {
        DomText* left_text = left->as_text();
        DomText* right_text = right->as_text();
        return left_text->length == right_text->length &&
            (left_text->length == 0 ||
             (left_text->text && right_text->text &&
              memcmp(left_text->text, right_text->text, left_text->length) == 0));
    }

    if (left->is_comment()) {
        DomComment* left_comment = left->as_comment();
        DomComment* right_comment = right->as_comment();
        return js_dom_equal_cstr(left_comment->tag_name, right_comment->tag_name) &&
            left_comment->length == right_comment->length &&
            (left_comment->length == 0 ||
             (left_comment->content && right_comment->content &&
              memcmp(left_comment->content, right_comment->content,
                     left_comment->length) == 0));
    }

    if (!left->is_element()) return false;

    DomElement* left_elem = left->as_element();
    DomElement* right_elem = right->as_element();
    if (!js_dom_equal_cstr(left_elem->tag_name, right_elem->tag_name)) return false;

    int left_attr_count = 0;
    const char** left_attr_names = left_elem->attribute_names(&left_attr_count);
    int right_visible_attr_count = 0;
    int right_attr_count = 0;
    const char** right_attr_names = right_elem->attribute_names(&right_attr_count);
    for (int i = 0; right_attr_names && i < right_attr_count; i++) {
        if (!js_dom_is_internal_attr(right_attr_names[i])) right_visible_attr_count++;
    }

    int left_visible_attr_count = 0;
    for (int i = 0; left_attr_names && i < left_attr_count; i++) {
        const char* name = left_attr_names[i];
        if (js_dom_is_internal_attr(name)) continue;
        left_visible_attr_count++;
        if (!right_elem->has_attribute(name)) return false;
        if (!js_dom_equal_cstr(left_elem->get_attribute(name),
                               right_elem->get_attribute(name))) return false;
    }
    if (left_visible_attr_count != right_visible_attr_count) return false;

    return js_dom_equal_children(left_elem, right_elem);
}

static bool js_dom_equal_children(DomElement* left, DomElement* right) {
    DomNode* left_child = left->first_child;
    DomNode* right_child = right->first_child;
    while (left_child && right_child) {
        if (!js_dom_nodes_are_equal(left_child, right_child)) return false;
        left_child = left_child->next_sibling;
        right_child = right_child->next_sibling;
    }
    return !left_child && !right_child;
}

extern "C" Item js_dom_is_equal_node(Item node_item, Item other_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(node_item);
    DomNode* other = (DomNode*)js_dom_unwrap_element(other_item);
    // Node equality is structural: separately created but matching trees must
    // compare equal so editor ownership checks do not depend on wrapper identity.
    return (Item){.item = b2it(js_dom_nodes_are_equal(node, other))};
}

extern "C" Item js_dom_is_same_node(Item node_item, Item other_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(node_item);
    DomNode* other = (DomNode*)js_dom_unwrap_element(other_item);
    return (Item){.item = b2it(node && node == other)};
}

// ============================================================================
// style.setProperty() / style.removeProperty() (v12b)
// ============================================================================

static Item js_dom_style_set_property_for_elem(DomElement* elem, Item prop_arg,
                                               Item value_arg, Item priority_arg,
                                               bool has_priority) {
    if (!elem) return ItemNull;
    const char* css_prop = fn_to_cstr(prop_arg);
    const char* val_str = fn_to_cstr(value_arg);
    if (!css_prop || !val_str) return ItemNull;
    if (!js_inline_style_cssom_property_exposed(css_prop)) {
        log_debug("js_dom_style_method: ignored unsupported CSSOM property '%s' on <%s>",
                  css_prop, elem->tag_name ? elem->tag_name : "?");
        return ItemNull;
    }

    const char* priority = nullptr;
    if (has_priority) {
        const char* requested_priority = fn_to_cstr(priority_arg);
        if (requested_priority && strcasecmp(requested_priority, "important") == 0) {
            priority = "important";
        }
    }
    int applied = js_dom_update_inline_style_attribute(
        elem, css_prop, val_str, priority) ? 1 : 0;
    elem->set_styles_resolved(false);
    if (applied) {
        CssPropertyCode prop_id = css_property_code_from_name(css_prop);
        js_dom_mutation_notify(js_dom_style_mutation_kind(prop_id),
                               (DomNode*)elem, elem->parent);
    }
    log_debug("js_dom_style_method: setProperty '%s: %s' on <%s>",
              css_prop, val_str, elem->tag_name ? elem->tag_name : "?");
    return ItemNull;
}

// inline style parsing and mutation invalidation remain centralized here.
JS_FORWARD_ITEM(js_dom_style_set_property_bridge,
    (void* dom_elem, Item prop_arg, Item value_arg, Item priority_arg, bool has_priority),
    js_dom_style_set_property_for_elem,
    ((DomElement*)dom_elem, prop_arg, value_arg, priority_arg, has_priority))

static Item js_dom_style_remove_property_for_elem(DomElement* elem, Item prop_arg) {
    if (!elem) return js_name_item("");
    const char* css_prop = fn_to_cstr(prop_arg);
    if (!css_prop) return js_name_item("");

    // get old value before removing
    CssPropertyCode prop_id = css_property_code_from_name(css_prop);
    Item old_val = js_name_item("");
    if (prop_id != CSS_PROPERTY_UNKNOWN && elem->specified_style) {
        CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
        if (decl && decl->specificity.inline_style) {
            // serialize old value via the getter
            Item owner_item = js_dom_wrap_element(elem);
            Item prop_item = js_name_item(css_prop);
            old_val = js_dom_get_style_property(owner_item, prop_item);
        }
        js_dom_update_inline_style_attribute(elem, css_prop, "", nullptr);
        js_dom_mutation_notify(js_dom_style_mutation_kind(prop_id),
                               (DomNode*)elem, elem->parent);
    }
    elem->set_styles_resolved(false);
    log_debug("js_dom_style_method: removeProperty '%s' on <%s>",
              css_prop, elem->tag_name ? elem->tag_name : "?");
    return old_val;
}

// old-value serialization and mutation invalidation remain centralized here.
JS_FORWARD_ITEM(js_dom_style_remove_property_bridge, (void* dom_elem, Item prop_arg),
    js_dom_style_remove_property_for_elem, ((DomElement*)dom_elem, prop_arg))


// ============================================================================
// F-1: Collection interface globals
// ----------------------------------------------------------------------------
// HTMLCollection / NodeList / RadioNodeList / HTMLFormControlsCollection /
// HTMLOptionsCollection are exposed so that:
//   - typeof HTMLCollection === 'function'
//   - HTMLCollection.prototype.{item,namedItem} exist
// In Lambda's headless runtime these are stub interface objects: calling
// them as constructors throws TypeError (per WebIDL). Real instances are
// still plain Arrays — methods on the prototype object exist for IDL
// surface conformance but are not used for actual collection access (which
// is satisfied by Array .item() and indexed access).
// ============================================================================
JS_FORWARD_STATIC_ITEM(_coll_illegal_constructor, (Item /*first*/), js_throw_type_error, ("Illegal constructor"))

static void _set_iface_to_string_tag(Item proto, const char* name) {
    if (get_type_id(proto) != LMD_TYPE_MAP || !name) return;
    // WebIDL interface prototypes carry @@toStringTag; selector/tooltip
    // libraries use this brand to distinguish a DOM Element from plain data.
    js_set_key_default(proto, js_well_known_symbol_key(4), js_name_item(name));
}

static void _install_iface(Item global, const char* name) {
    RootFrame roots(4);
    Rooted<Item> global_root(roots, global);
    Rooted<Item> key_root(roots, js_name_item(name));
    Rooted<Item> ctor_root(roots,
        js_get_key_default(global_root.get(), key_root.get()));
    Rooted<Item> proto_root(roots, ItemNull);
    if (js_is_callable(ctor_root.get())) {
        proto_root.set(js_get_key_cstr(ctor_root.get(), "prototype"));
        _set_iface_to_string_tag(proto_root.get(), name);
        return;
    }
    // Interface constructors share one native illegal-constructor callback but
    // must not share a cached JsFunction: each owns a distinct `.prototype`.
    // WebIDL illegal constructors still have [[Construct]]; their body, rather
    // than a missing capability, supplies the required TypeError (D6.2.2v2).
    ctor_root.set(js_new_distinct_native_constructor(
        _coll_illegal_constructor));
    js_set_function_name(ctor_root.get(), key_root.get());
    proto_root.set(js_new_object());
    _set_iface_to_string_tag(proto_root.get(), name);
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    js_set_key_default(global_root.get(), key_root.get(), ctor_root.get());
}

static Item _document_fragment_ctor(void) {
    // Both construction paths must create the same detached native node so
    // fragment insertion retains the move-children invariant.
    return js_dom_create_document_fragment(_js_current_document);
}

template <typename Target>
static void js_dom_install_value_constructor(Item global, const char* name,
        Target target, bool set_string_tag) {
    JS_ROOTS(roots,
        global_root, global,
        ctor_root, js_new_distinct_native_constructor(target),
        proto_root, js_new_object());
    Item name_key = js_string_key(name);
    js_set_function_name(ctor_root.get(), name_key);
    if (set_string_tag) _set_iface_to_string_tag(proto_root.get(), name);
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    js_set_key_default(global_root.get(), name_key, ctor_root.get());
}
JS_FORWARD_STATIC_VOID( _install_document_fragment_iface, (Item global), js_dom_install_value_constructor, (global, "DocumentFragment", _document_fragment_ctor, true))

static Item _iface_proto(Item global, const char* name) {
    Item ctor = js_get_key_default(global, js_name_item(name));
    if (!js_is_callable(ctor)) return ItemNull;
    Item proto = js_get_key_cstr(ctor, "prototype");
    return get_type_id(proto) == LMD_TYPE_MAP ? proto : ItemNull;
}

static void _install_nodelist_for_each(Item global) {
    Item node_list_proto = _iface_proto(global, "NodeList");
    Item array_ctor = js_get_key_cstr(global, "Array");
    Item array_proto = js_get_key_cstr(array_ctor, "prototype");
    Item array_for_each = js_get_key_cstr(array_proto, "forEach");
    if (get_type_id(node_list_proto) == LMD_TYPE_MAP &&
        js_is_callable(array_for_each)) {
        // Query APIs return Arrays, but libraries feature-detect the WebIDL
        // NodeList prototype before choosing their iteration path.
        js_set_key_cstr(node_list_proto, "forEach", array_for_each);
    }
}

static void _link_iface_proto(Item global, const char* name, const char* base_name) {
    Item proto = _iface_proto(global, name);
    Item base_proto = _iface_proto(global, base_name);
    if (get_type_id(proto) == LMD_TYPE_MAP && get_type_id(base_proto) == LMD_TYPE_MAP) {
        js_set_prototype(proto, base_proto);
    }
}

static void _set_ctor_int_constant(Item ctor, const char* name, int64_t value) {
    js_set_key_default(ctor, js_name_item(name),
        (Item){.item = i2it(value)});
}

static bool _xpath_attr_name_matches(const char* expression, const char* attr_name) {
    if (!expression || !attr_name) return false;
    const char* marker = "starts-with(name(), \"";
    size_t marker_len = strlen(marker);
    const char* scan = expression;
    while ((scan = strstr(scan, marker)) != nullptr) {
        const char* prefix = scan + marker_len;
        const char* end = strchr(prefix, '\"');
        if (!end) break;
        size_t prefix_len = (size_t)(end - prefix);
        if (prefix_len > 0 && strncmp(attr_name, prefix, prefix_len) == 0) return true;
        scan = end + 1;
    }
    return false;
}

static bool _xpath_element_matches(Item expression_item, DomElement* elem) {
    const char* expression = fn_to_cstr(expression_item);
    if (!expression || !elem) return false;
    int attr_count = 0;
    const char** attr_names = elem->attribute_names(&attr_count);
    for (int i = 0; attr_names && i < attr_count; i++) {
        if (_xpath_attr_name_matches(expression, attr_names[i])) return true;
    }
    return false;
}

static void _xpath_collect_descendants(DomNode* node, Item expression, Item matches) {
    for (DomNode* current = node; current; current = current->next_sibling) {
        if (!current->is_element()) continue;
        DomElement* elem = current->as_element();
        if (_xpath_element_matches(expression, elem)) {
            js_array_push(matches, js_dom_wrap_element(elem));
        }
        if (elem->first_child) {
            _xpath_collect_descendants(elem->first_child, expression, matches);
        }
    }
}

static Item _xpath_result_iterate_next(void) {
    Item self = js_get_this();
    Item items = js_get_key_cstr(self, "__lambda_xpath_items");
    Item index_item = js_get_key_cstr(self, "__lambda_xpath_index");
    int64_t index = get_type_id(index_item) == LMD_TYPE_INT ? it2i(index_item) : 0;
    if (get_type_id(items) != LMD_TYPE_ARRAY || index >= js_array_length(items)) {
        return ItemNull;
    }
    Item match = js_elements_get_int(items, index);
    js_set_key_cstr(self, "__lambda_xpath_index", (Item){.item = i2it(index + 1)});
    return match;
}

static Item _xpath_expression_evaluate(Item context_node, Item /*result_type*/,
                                       Item /*existing_result*/) {
    Item self = js_get_this();
    Item expression = js_get_key_cstr(self, "__lambda_xpath_source");
    DomElement* root = (DomElement*)js_dom_unwrap_element(context_node);
    Item matches = js_array_new(0);
    if (root && root->first_child) {
        // XPath `.//*` selects descendants, not the context node itself; htmx
        // checks the context node separately before consuming this iterator.
        _xpath_collect_descendants(root->first_child, expression, matches);
    }

    Item result = js_new_object();
    js_set_key_cstr(result, "__lambda_xpath_items", matches);
    js_set_key_cstr(result, "__lambda_xpath_index", (Item){.item = i2it(0)});
    Item iterate_next = js_new_distinct_native_function(_xpath_result_iterate_next);
    js_set_function_name(iterate_next, js_string_key("iterateNext"));
    js_set_key_cstr(result, "iterateNext", iterate_next);
    return result;
}

static Item _xpath_evaluator_create_expression(Item expression, Item /*resolver*/) {
    Item compiled = js_new_object();
    js_set_key_cstr(compiled, "__lambda_xpath_source", js_to_string(expression));
    Item evaluate = js_new_distinct_native_function(_xpath_expression_evaluate);
    js_set_function_name(evaluate, js_string_key("evaluate"));
    js_set_key_cstr(compiled, "evaluate", evaluate);
    return compiled;
}

static Item _xpath_evaluator_ctor(void) {
    Item evaluator = js_new_object();
    Item create_expression = js_new_distinct_native_function(
        _xpath_evaluator_create_expression);
    js_set_function_name(create_expression, js_string_key("createExpression"));
    js_set_key_cstr(evaluator, "createExpression", create_expression);

    Item global = js_get_global_this();
    Item ctor = js_get_key_cstr(global, "XPathEvaluator");
    Item proto = js_get_key_cstr(ctor, "prototype");
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(evaluator, proto);
    return evaluator;
}
JS_FORWARD_STATIC_VOID( _install_xpath_evaluator, (Item global), js_dom_install_value_constructor, (global, "XPathEvaluator", _xpath_evaluator_ctor, false))

static void _install_node_iface(Item global) {
    RootFrame roots(3);
    Rooted<Item> global_root(roots, global);
    Rooted<Item> ctor_root(roots,
        js_new_distinct_native_constructor(_coll_illegal_constructor));
    Rooted<Item> proto_root(roots, js_new_object());
    js_set_function_name(ctor_root.get(), js_name_item("Node"));
    _set_iface_to_string_tag(proto_root.get(), "Node");
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    static const struct { const char* name; int value; } node_constants[] = {
        {"ELEMENT_NODE", 1}, {"ATTRIBUTE_NODE", 2}, {"TEXT_NODE", 3},
        {"CDATA_SECTION_NODE", 4}, {"ENTITY_REFERENCE_NODE", 5},
        {"ENTITY_NODE", 6}, {"PROCESSING_INSTRUCTION_NODE", 7},
        {"COMMENT_NODE", 8}, {"DOCUMENT_NODE", 9},
        {"DOCUMENT_TYPE_NODE", 10}, {"DOCUMENT_FRAGMENT_NODE", 11},
        {"NOTATION_NODE", 12},
    };
    for (size_t i = 0; i < sizeof(node_constants) / sizeof(node_constants[0]); i++) {
        _set_ctor_int_constant(ctor_root.get(), node_constants[i].name,
            node_constants[i].value);
    }
    js_set_key_cstr(global_root.get(), "Node", ctor_root.get());
}

static JsWebAnimationHost* js_web_animation_host(Item value) {
    if (get_type_id(value) == LMD_TYPE_VMAP && value.vmap &&
        value.vmap->host_type == (const void*)&js_web_animation_vmap_marker) {
        return (JsWebAnimationHost*)value.vmap->host_data;
    }
    if (get_type_id(value) == LMD_TYPE_MAP) {
        Item holder = js_get_key_cstr(value, "__lambda_web_animation_host");
        if (get_type_id(holder) == LMD_TYPE_VMAP && holder.vmap &&
            holder.vmap->host_type == (const void*)&js_web_animation_vmap_marker) {
            return (JsWebAnimationHost*)holder.vmap->host_data;
        }
    }
    return nullptr;
}

JS_FORWARD_STATIC_EXPRESSION(float, js_web_animation_number, (Item value, float fallback),
    js_dom_svg_number(value, fallback))

static CssAnimComposite js_web_animation_composite(Item value) {
    const char* text = fn_to_cstr(value);
    if (text && strcasecmp(text, "add") == 0) return CSS_ANIM_COMPOSITE_ADD;
    if (text && strcasecmp(text, "accumulate") == 0) {
        return CSS_ANIM_COMPOSITE_ACCUMULATE;
    }
    return CSS_ANIM_COMPOSITE_REPLACE;
}

static CssKeyframes* js_web_animation_parse_keyframes(DomElement* element,
                                                       Item keyframes_item) {
    if (!element || !element->doc ||
        get_type_id(keyframes_item) != LMD_TYPE_ARRAY) return nullptr;

    int count = (int)js_array_length(keyframes_item);
    if (count <= 0) return nullptr;
    if (count > 64) count = 64;

    Pool* pool = element->doc->document_pool;
    CssKeyframes* keyframes = (CssKeyframes*)pool_calloc(
        pool, sizeof(CssKeyframes));
    if (!keyframes) return nullptr;
    keyframes->name = "web-animation";
    keyframes->stops = (CssKeyframeStop*)pool_calloc(
        pool, sizeof(CssKeyframeStop) * count);
    if (!keyframes->stops) return nullptr;
    keyframes->stop_count = count;

    for (int i = 0; i < count; i++) {
        CssKeyframeStop* stop = &keyframes->stops[i];
        stop->offset = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
        Item frame = js_elements_get_int(keyframes_item, i);
        if (get_type_id(frame) != LMD_TYPE_MAP &&
            get_type_id(frame) != LMD_TYPE_VMAP) continue;

        Item offset = js_get_key_cstr(frame, "offset");
        if (!is_js_undefined(offset) && offset.item != ITEM_NULL) {
            float parsed_offset = js_web_animation_number(offset, stop->offset);
            if (isfinite(parsed_offset)) stop->offset = parsed_offset;
        }

        Item names = js_object_get_own_property_names(frame);
        if (get_type_id(names) != LMD_TYPE_ARRAY) continue;
        int name_count = (int)js_array_length(names);
        for (int j = 0; j < name_count; j++) {
            const char* js_name = fn_to_cstr(js_elements_get_int(names, j));
            if (!js_name || strcmp(js_name, "offset") == 0 ||
                strcmp(js_name, "composite") == 0) continue;

            char css_name[128];
            js_camel_to_css_prop(js_name, css_name, sizeof(css_name));
            CssPropertyCode property = css_property_code_from_name(css_name);
            if (property == CSS_PROPERTY_UNKNOWN || property == 0) continue;

            const char* value = fn_to_cstr(js_get_key_default(
                frame, js_string_key(js_name)));
            if (!value || !value[0]) continue;

            CssAnimatedProp parsed;
            if (!css_animation_parse_property_value(property, value, &parsed,
                                                    pool)) continue;
            parsed.composite = js_web_animation_composite(js_get_key_cstr(frame, "composite"));
            stop->properties = (CssAnimatedProp*)pool_calloc(
                pool, sizeof(CssAnimatedProp));
            if (!stop->properties) return nullptr;
            stop->properties[0] = parsed;
            stop->property_count = 1;
            break;
        }
    }
    return keyframes;
}

// the headless runner samples currentTime explicitly; pausing only needs to
// prevent an implicit clock from changing that deterministic sample.
JS_FORWARD_STATIC_EXPRESSION(Item, js_web_animation_pause, (void), ItemNull)

static Item js_web_animation_current_time_get(void) {
    JsWebAnimationHost* host = js_web_animation_host(js_get_this());
    return host && host->state ? js_make_number(host->state->current_time_ms)
                               : ItemNull;
}

static Item js_web_animation_current_time_set(Item value) {
    JsWebAnimationHost* host = js_web_animation_host(js_get_this());
    if (host && host->state) {
        Item numeric = js_to_number(value);
        TypeId type = get_type_id(numeric);
        double current_time = 0.0;
        if (type == LMD_TYPE_FLOAT) current_time = it2d(numeric);
        else if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
            current_time = (double)it2i(numeric);
        }
        css_web_animation_set_current_time(host->state, current_time);
        log_debug("web-anim: currentTime set to %.1fms", current_time);
    }
    return value;
}

static Item js_dom_element_animate(Item keyframes_item, Item options_item) {
    DomElement* element = (DomElement*)js_dom_unwrap_element(js_get_this());
    if (!element || !element->doc) return ItemNull;

    CssKeyframes* keyframes = js_web_animation_parse_keyframes(
        element, keyframes_item);
    if (!keyframes) return ItemNull;

    double duration_ms = 0.0;
    TimingFunction timing = {};
    timing.type = TIMING_LINEAR;
    if (get_type_id(options_item) == LMD_TYPE_MAP ||
        get_type_id(options_item) == LMD_TYPE_VMAP) {
        Item duration = js_get_key_cstr(options_item, "duration");
        if (!is_js_undefined(duration) && duration.item != ITEM_NULL) {
            duration_ms = js_web_animation_number(duration, 0.0f);
        }
        Item easing = js_get_key_cstr(options_item, "easing");
        const char* easing_text = fn_to_cstr(easing);
        if (easing_text) {
            css_animation_parse_timing_function_text(easing_text, &timing);
        }
    }

    CssWebAnimationState* state = css_web_animation_create(
        element, keyframes, duration_ms, &timing, element->doc->document_pool);
    if (!state) return ItemNull;

    JsWebAnimationHost* host = (JsWebAnimationHost*)pool_calloc(
        element->doc->document_pool, sizeof(JsWebAnimationHost));
    if (!host) return ItemNull;
    host->state = state;

    Item holder = vmap_new();
    if (get_type_id(holder) != LMD_TYPE_VMAP || !holder.vmap) return ItemNull;
    holder.vmap->host_type = (const void*)&js_web_animation_vmap_marker;
    holder.vmap->host_data = host;
    // A plain object keeps pause() on the normal JS method path; the private
    // native holder supplies the DOM-owned state to currentTime accessors.
    Item animation = js_new_object();
    js_set_key_cstr(animation, "__lambda_web_animation_host", holder);
    js_set_native_key(animation, js_string_key("pause"), js_web_animation_pause);
    js_install_native_accessor(animation, js_string_key("currentTime"),
        js_new_native_function(js_web_animation_current_time_get),
        js_new_native_function(js_web_animation_current_time_set),
        JSPD_NON_ENUMERABLE);
    return animation;
}

extern "C" void js_dom_install_collection_globals(void) {
    Item global = js_get_global_this();
    _install_iface(global, "Window");
    _link_iface_proto(global, "Window", "EventTarget");
    Item window_proto = _iface_proto(global, "Window");
    if (get_type_id(window_proto) == LMD_TYPE_MAP) {
        // The browsing-context global is the Window instance; exposing only
        // `window` left WebIDL brand checks such as event.view instanceof Window
        // unable to enter pointer-driven editor paths.
        js_set_prototype(global, window_proto);
    }
    _install_node_iface(global);
    // Document wrappers are module-owned, but bare WebIDL constructor lookup
    // must still succeed before libraries inspect static Document features.
    static const char* iface_links[][2] = {
        {"Document", "Node"}, {"HTMLDocument", "Document"},
        {"Element", "Node"}, {"HTMLElement", "Element"},
        {"SVGElement", "Element"}, {"SVGGraphicsElement", "SVGElement"},
        {"SVGSVGElement", "SVGGraphicsElement"},
        {"SVGPathElement", "SVGGraphicsElement"},
        {"SVGTextContentElement", "SVGGraphicsElement"},
    };
    for (size_t i = 0; i < sizeof(iface_links) / sizeof(iface_links[0]); i++) {
        _install_iface(global, iface_links[i][0]);
        _link_iface_proto(global, iface_links[i][0], iface_links[i][1]);
    }
    // JointJS Vectorizer gates its SVG implementation on window.SVGAngle;
    // without this legacy WebIDL interface it installs a non-SVG fallback.
    static const char* svg_ifaces[] = {"SVGAngle", "SVGMatrix", "SVGTransform", "SVGPoint"};
    for (size_t i = 0; i < sizeof(svg_ifaces) / sizeof(svg_ifaces[0]); i++) {
        _install_iface(global, svg_ifaces[i]);
    }
    js_dom_install_value_constructor(global, "DOMMatrix", js_dom_matrix_constructor, true);
    js_dom_install_value_constructor(global, "DOMPoint", js_dom_point_constructor, true);
    int html_interface_count = (int)(sizeof(s_js_dom_html_interfaces) /
        sizeof(s_js_dom_html_interfaces[0]));
    for (int i = 0; i < html_interface_count; i++) {
        // Specialized HTML wrappers must inherit HTMLElement so WebIDL brand
        // checks do not collapse every form control to the generic interface.
        _install_iface(global, s_js_dom_html_interfaces[i].constructor_name);
        _link_iface_proto(global, s_js_dom_html_interfaces[i].constructor_name,
                          "HTMLElement");
    }
    _install_document_fragment_iface(global);
    _link_iface_proto(global, "DocumentFragment", "Node");
    _install_iface(global, "ShadowRoot");
    _link_iface_proto(global, "ShadowRoot", "DocumentFragment");
    Item element_proto = _iface_proto(global, "Element");
    if (get_type_id(element_proto) == LMD_TYPE_MAP) {
        // Bootstrap deliberately calls these WebIDL methods through
        // Element.prototype.querySelector(All).call(element, selector).
        RootFrame method_roots(2);
        Rooted<Item> element_proto_root(method_roots, element_proto);
        Rooted<Item> method_root(method_roots,
            js_new_native_payload_function(js_dom_element_prototype_operation_body,
                (uint64_t)JUBE_DOM_QUERY_SELECTOR, 1));
        // Bootstrap needs these WebIDL prototype aliases before any instance
        // exists. Each carries its direct operation payload; populating every
        // Jube prototype here would mutate the sealed NameId module table.
        js_set_key_cstr(element_proto_root.get(), "querySelector", method_root.get());
        method_root.set(js_new_native_payload_function(
            js_dom_element_prototype_operation_body,
            (uint64_t)JUBE_DOM_QUERY_SELECTOR_ALL, 1));
        js_set_key_cstr(element_proto_root.get(), "querySelectorAll", method_root.get());
        js_set_native_key(element_proto_root.get(), js_string_key("animate"), js_dom_element_animate);
    }
    static const char* collection_ifaces[] = {
        "Range", "Selection", "HTMLCollection", "HTMLFormControlsCollection",
        "HTMLOptionsCollection", "NodeList",
    };
    for (size_t i = 0; i < sizeof(collection_ifaces) / sizeof(collection_ifaces[0]); i++) {
        _install_iface(global, collection_ifaces[i]);
    }
    _install_iface(global, "CSSNestedDeclarations");
    _install_nodelist_for_each(global);
    _install_iface(global, "RadioNodeList");
    _install_xpath_evaluator(global);
    log_debug("js_dom_install_collection_globals: installed collection interfaces");
}

// ----------------------------------------------------------------------------
// F-5: Option constructor — `new Option(text, value, defaultSelected, selected)`
// ----------------------------------------------------------------------------
static Item _option_ctor(Item text_arg, Item value_arg, Item def_sel_arg, Item sel_arg) {
    DomDocument* doc = _js_current_document;
    if (!doc || !doc->input) return ItemNull;
    MarkBuilder builder(doc->input);
    Item nat_item = builder.element("option").final();
    Element* nat = nat_item.element;
    DomElement* opt = dom_element_create(doc, "option", nat);
    if (!opt) return ItemNull;
    // text → option's text content (single text child).
    if (text_arg.item != ITEM_NULL && !is_js_undefined(text_arg)) {
        const char* t = fn_to_cstr(text_arg);
        if (t && *t) {
            DomText* tn = DomText::create_copy(t, strlen(t), opt);
            if (tn) {
                tn->parent = opt;
                opt->first_child = tn;
                opt->last_child = tn;
            }
        }
    }
    // value attr — only set if value_arg is provided AND not undefined.
    if (value_arg.item != ITEM_NULL && !is_js_undefined(value_arg)) {
        const char* v = fn_to_cstr(value_arg);
        opt->set_attribute("value", v ? v : "");
    }
    // defaultSelected → `selected` content attribute.
    if (def_sel_arg.item != ITEM_NULL && !is_js_undefined(def_sel_arg) &&
        js_is_truthy(def_sel_arg)) {
        opt->set_attribute("selected", "");
    }
    // selected → live selectedness flag.
    if (sel_arg.item != ITEM_NULL && !is_js_undefined(sel_arg)) {
        _set_selectedness(opt, js_is_truthy(sel_arg));
    }
    return js_dom_wrap_element(opt);
}

extern "C" void js_dom_install_option_constructor(void) {
    Item global = js_get_global_this();
    Item ctor = js_new_native_constructor(_option_ctor);
    js_set_function_name(ctor, js_name_item("Option"));
    Item proto = js_new_object();
    js_set_key_cstr(proto, "constructor", ctor);
    js_set_key_cstr(ctor, "prototype", proto);
    js_set_key_cstr(global, "Option", ctor);
    log_debug("js_dom_install_option_constructor: installed Option");
}
JS_FORWARD_VOID( js_dom_collections_release_context, (void), reset_live_dom_collections, ())

#undef js_dom_collection_state
#undef s_select_options_owners
#undef s_select_options_owner_count
#undef s_live_child_collections
#undef s_live_child_collection_count
#undef s_live_form_collections
#undef s_live_form_collection_count
#undef s_live_lookup_collections
#undef s_live_lookup_collection_count
#undef s_dom_collection_refresh_depth

static void js_dom_destroy_context_state(void** slot, bool has_entries,
                                         const char* label) {
    if (!slot || !*slot) return;
    // All weak homes and native pins are removed before heap destruction.
    if (has_entries) log_error("%s: context destroyed before roots were released", label);
    mem_free(*slot);
    *slot = nullptr;
}

extern "C" void js_dom_collections_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->dom_collection_state) return;
    JsDomCollectionRuntimeState* state =
        (JsDomCollectionRuntimeState*)runtime_state->dom_collection_state;
    js_dom_destroy_context_state(&runtime_state->dom_collection_state,
        state->select_options_owner_count || state->live_child_collection_count ||
        state->live_form_collection_count || state->live_lookup_collection_count,
        "js-dom-collections");
}

extern "C" void js_dom_foreign_documents_release_context(void) {
    reset_pending_iframe_loads();
    reset_foreign_document_cache();
}

#undef js_dom_foreign_document_state
#undef s_foreign_doc_cache
#undef s_foreign_doc_cache_count
#undef s_doc_with_window
#undef s_doc_with_window_count
#undef s_iframe_cache
#undef s_iframe_cache_count
#undef s_pending_iframe_loads
#undef s_pending_iframe_refs
#undef s_pending_iframe_docs
#undef s_pending_iframe_load_count
#undef s_iframe_load_drain_scheduled

extern "C" void js_dom_foreign_documents_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->dom_foreign_document_state) return;
    JsDomForeignDocumentRuntimeState* state =
        (JsDomForeignDocumentRuntimeState*)runtime_state->dom_foreign_document_state;
    js_dom_destroy_context_state(&runtime_state->dom_foreign_document_state,
        state->foreign_doc_cache_count || state->doc_with_window_count ||
        state->iframe_cache_count || state->pending_iframe_load_count,
        "js-dom-foreign-document");
}
