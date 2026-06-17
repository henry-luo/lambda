/**
 * JavaScript DOM API Bridge Implementation
 *
 * Bridges Lambda's Element data model and Radiant's DomElement/DomDocument
 * to provide standard DOM manipulation APIs callable from JIT-compiled JavaScript.
 *
 * Wrapping: DomElement* is stored in a Map struct with a unique type marker
 * pointer (js_dom_type_marker) in the Map::type field, and DomElement* in
 * Map::data. This gives O(1) wrap/unwrap with zero HashMap allocation per node.
 */

#include "js_dom.h"
#include "js_dom_events.h"
#include "js_dom_selection.h"
#include "js_xhr.h"
#include "js_cssom.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../mark_builder.hpp"
#include "../mark_editor.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "../../lib/str.h"
#include "../../lib/url.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_style_node.hpp"
#include "../input/css/css_formatter.hpp"
#include "../input/css/selector_matcher.hpp"
#include "../../radiant/view.hpp"
#include "../../radiant/form_control.hpp"
#include "../../radiant/state_store.hpp"
#include "../../radiant/dom_range.hpp"
#include "../../radiant/editing.hpp"
#include "../../radiant/editing_dispatch.hpp"
#include "../../radiant/editing_intent.hpp"
#include "../../radiant/editing_rich_transaction.hpp"
#include "../../radiant/editing_target_range.hpp"
#include "../../radiant/handler.hpp"
#include "../input/html5/html5_parser.h"

extern "C" void heap_unregister_gc_root(uint64_t* slot);

#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include "../../lib/mem.h"

// JS undefined helpers (matching js_runtime.cpp encoding)
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}
static inline bool is_js_undefined(Item val) {
    return get_type_id(val) == LMD_TYPE_UNDEFINED;
}

static inline Item js_string_key(const char* s) {
    return (Item){.item = s2it(heap_create_name(s))};
}

static const char* js_dom_to_attr_cstr(Item value) {
    Item str_value = js_to_string(value);
    const char* s = fn_to_cstr(str_value);
    return s ? s : "";
}

// Forward declarations
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" Item js_eventtarget_add_listener(Item type, Item callback, Item opts);
extern "C" Item js_eventtarget_remove_listener(Item type, Item callback, Item opts);
extern "C" Item js_data_transfer_new_with_strings(const char* text_plain,
                                                  const char* text_html);
extern "C" Item js_eventtarget_dispatch(Item event_item);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" void js_throw_value(Item error);
static void js_camel_to_css_prop(const char* js_prop, char* css_buf, size_t buf_size);
static CssDeclaration* js_match_element_property(DomElement* elem, CssPropertyId prop_id, int pseudo_type);
static CssDeclaration* js_match_custom_property(DomElement* elem, const char* prop_name);
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);

static String* js_dom_create_document_string(DomDocument* doc, const char* str, size_t len) {
    if (!doc || !doc->arena || !str) return nullptr;
    String* s = (String*)arena_alloc(doc->arena, sizeof(String) + len + 1);
    if (!s) return nullptr;
    s->len = (uint32_t)len;
    s->is_ascii = str_is_ascii(str, len) ? 1 : 0;
    if (len > 0) memcpy(s->chars, str, len);
    s->chars[len] = '\0';
    return s;
}

// ============================================================================
// Unique type marker for DOM-wrapped Maps
// ============================================================================

// Sentinel used in Map::type to distinguish DOM wrappers from regular Maps.
// Address uniqueness is all that matters; the content is unused.
static TypeMap js_dom_type_marker = {};

// Sentinel used in Map::type to distinguish computed style wrappers.
// Map::data stores the DomElement*, Map::data_cap stores pseudo-element type (0=none, 1=before, 2=after).
static TypeMap js_computed_style_marker = {};

// Sentinel used in Map::type to distinguish element.style wrappers.
// Map::data stores the owning DomElement*.
static TypeMap js_inline_style_marker = {};

// Sentinel used in Map::type to distinguish document proxy objects.
// Map::data is unused (the document is accessed via _js_current_document).
static TypeMap js_document_proxy_marker = {};

// Sentinel used in Map::type to distinguish foreign-document wrappers
// (created via document.implementation.createHTMLDocument / createDocument).
// Map::data stores the owning DomDocument*. map_kind = MAP_KIND_FOREIGN_DOC.
static TypeMap js_foreign_doc_marker = {};

// Sentinel used in Map::type to distinguish the document.implementation singleton.
// Map::data is unused.
static TypeMap js_dom_implementation_marker = {};

// Cached singleton for document.implementation.
static Item js_dom_implementation_item = {.item = ITEM_NULL};

// Cached singleton document proxy object
static Item js_document_proxy_item = {.item = ITEM_NULL};

// Stored document.defaultView value (kept separate to avoid infinite recursion
// when calling js_property_set on the document proxy which is MAP_KIND_DOM)
static Item js_document_default_view = {.item = ITEM_NULL};

// Stored document.title value (same reason as defaultView)
static Item js_document_title_value = {.item = ITEM_NULL};
static bool js_document_design_mode = false;
static DomElement* js_document_active_element = nullptr;

// Cached document.fonts object for the FontFaceSet ready shim.
static Item js_document_fonts_value = {.item = ITEM_NULL};

// ============================================================================
// Thread-local DOM document context
// ============================================================================

static __thread DomDocument* _js_current_document = nullptr;
// The "main" document — the one bound by js_dom_set_document at page load.
// Foreign documents created via document.implementation.create*Document are
// distinct: they have a null defaultView and getSelection() returns null per
// HTML spec, even when the foreign-doc dispatcher temporarily swaps them in
// as _js_current_document.
static __thread DomDocument* _js_main_document = nullptr;
static void js_dom_install_window_location_history_globals(void);

static Item js_font_face_set_ready_then(Item callback) {
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return js_document_fonts_value.item != ITEM_NULL ? js_document_fonts_value : make_js_undefined();
}

static Item js_create_document_fonts_object(void) {
    Item fonts = js_new_object();
    Item ready = js_new_object();
    Item then_fn = js_new_function((void*)js_font_face_set_ready_then, 1);
    js_property_set(ready, js_string_key("then"), then_fn);
    js_property_set(fonts, js_string_key("ready"), ready);
    return fonts;
}

extern "C" bool js_dom_current_is_main_document(void) {
    return _js_current_document != nullptr && _js_current_document == _js_main_document;
}

// Forward decls (defined further down in the foreign-doc / iframe section).
extern "C" bool js_doc_has_browsing_context(void* doc);
extern "C" void js_doc_mark_has_browsing_context(void* doc);
extern "C" void* js_get_foreign_doc(Item item);
static Url* js_dom_make_fallback_url(const char* raw_url);

static inline DocState* js_dom_current_state();

static void js_dom_mark_dirty_subtree(DomNode* root) {
    if (!root) return;

    root->layout_dirty = true;
    if (root->is_element()) {
        DomElement* elem = root->as_element();
        elem->styles_resolved = false;
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            js_dom_mark_dirty_subtree(child);
        }
    }
}

static void js_dom_mark_dirty_ancestors(DomNode* node) {
    for (DomNode* cur = node; cur; cur = cur->parent) {
        cur->layout_dirty = true;
        if (cur->is_element()) {
            cur->as_element()->styles_resolved = false;
        }
    }
}

static inline uint32_t js_dom_mutation_bit(DomJsMutationKind kind) {
    uint32_t slot = (uint32_t)kind;
    if (slot >= 31) slot = 0;
    return 1u << slot;
}

static inline DomJsMutationKind js_dom_style_mutation_kind(CssPropertyId prop_id) {
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

static inline void js_dom_record_mutation_detail(DomJsMutationKind kind,
                                                 DomNode* target,
                                                 DomNode* parent,
                                                 uint32_t sequence) {
    DomDocument* doc = _js_current_document;
    if (!doc) return;

    if (sequence == 0) {
        sequence = doc->js_mutation_sequence + 1;
    }
    doc->js_mutation_kind_mask |= js_dom_mutation_bit(kind);

    if (doc->js_mutation_record_count < DOM_JS_MUTATION_RECORD_CAP) {
        DomJsMutationRecord* record =
            &doc->js_mutation_records[doc->js_mutation_record_count++];
        record->sequence = sequence;
        record->kind = kind;
        record->target = target;
        record->parent = parent;
        record->target_id = target ? target->id : 0;
        record->parent_id = parent ? parent->id : 0;
    } else {
        doc->js_mutation_record_overflow++;
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
                                          DomNode* parent = nullptr) {
    DomDocument* doc = _js_current_document;
    if (!doc) return;

    doc->js_mutation_count++;
    doc->js_mutation_sequence++;

    bool has_pending_structural_record = false;
    if (kind == DOM_JS_MUTATION_UNKNOWN && !target && !parent &&
        doc->js_mutation_record_count > 0) {
        DomJsMutationRecord* last = &doc->js_mutation_records[doc->js_mutation_record_count - 1];
        has_pending_structural_record = last->sequence == doc->js_mutation_sequence;
    }

    if (!has_pending_structural_record) {
        js_dom_record_mutation_detail(kind, target, parent, doc->js_mutation_sequence);
    }

    DocState* st = js_dom_current_state();
    if (st) view_state_prune_orphans(st);
}

// ----------------------------------------------------------------------------
// Phase 3: live-range mutation envelopes — thin wrappers that bail when no
// per-document DocState (and thus no live ranges) is attached. All DOM
// mutation paths in this file route through these to keep boundary points
// in sync per WHATWG DOM §5.3.
// ----------------------------------------------------------------------------
static inline DocState* js_dom_current_state() {
    return _js_current_document ? _js_current_document->state : nullptr;
}

static DocState* js_dom_testdriver_state() {
    if (!_js_current_document) return nullptr;
    if (!_js_current_document->state) {
        radiant_document_ensure_state(_js_current_document, "js_dom_testdriver_key");
    }
    return _js_current_document->state;
}

static bool js_dom_testdriver_selection_noncollapsed(DocState* state) {
    return state && state->dom_selection &&
        state->dom_selection->range_count > 0 &&
        !dom_selection_is_collapsed(state->dom_selection);
}

static View* js_dom_testdriver_current_target(DocState* state,
                                              int* fallback_offset) {
    if (fallback_offset) *fallback_offset = 0;
    if (state && state->dom_selection &&
        state->dom_selection->range_count > 0 &&
        state->dom_selection->ranges[0]) {
        DomBoundary focus = dom_selection_focus_boundary(state->dom_selection);
        if (!focus.node) focus = dom_selection_anchor_boundary(state->dom_selection);
        if (focus.node) {
            if (fallback_offset && focus.node->is_text()) {
                DomText* text = focus.node->as_text();
                *fallback_offset = (int)dom_text_utf16_to_utf8(
                    text, focus.offset); // INT_CAST_OK: fallback byte offsets use int in the editing API.
            }
            return static_cast<View*>(focus.node);
        }
    }
    if (state) {
        View* focused = focus_get(state);
        if (focused) return focused;
    }
    return js_document_active_element
        ? static_cast<View*>(js_document_active_element)
        : nullptr;
}

static bool js_dom_testdriver_rich_surface(View* target,
                                           EditingSurface* surface) {
    if (!target || !surface) return false;
    if (editing_surface_from_target(target, surface) &&
        editing_surface_is_rich(surface)) {
        if (surface->owner) surface->view = static_cast<View*>(surface->owner);
        return true;
    }
    DomNode* node = static_cast<DomNode*>(target);
    for (DomNode* cur = node ? node->parent : nullptr; cur; cur = cur->parent) {
        if (!cur->is_element()) continue;
        if (editing_surface_from_target(static_cast<View*>(cur), surface) &&
            editing_surface_is_rich(surface)) {
            if (surface->owner) surface->view = static_cast<View*>(surface->owner);
            return true;
        }
    }
    return false;
}

static InputIntentType js_dom_testdriver_delete_intent(uint32_t wpt_key,
                                                       int mods,
                                                       bool has_range) {
    bool word_modifier = (mods & (RDT_MOD_CTRL | RDT_MOD_ALT)) != 0;
    if (wpt_key == 0xE003) {
        if (has_range || !word_modifier) return INPUT_INTENT_DELETE_CONTENT_BACKWARD;
        return INPUT_INTENT_DELETE_WORD_BACKWARD;
    }
    if (wpt_key == 0xE017) {
        if (has_range || !word_modifier) return INPUT_INTENT_DELETE_CONTENT_FORWARD;
        return INPUT_INTENT_DELETE_WORD_FORWARD;
    }
    return INPUT_INTENT_NONE;
}

static Item js_dom_testdriver_static_range_item(const EditingTargetRange* r) {
    Item obj = js_new_object();
    Item start = (r && r->start.node) ? js_dom_wrap_element(r->start.node) : ItemNull;
    Item end = (r && r->end.node) ? js_dom_wrap_element(r->end.node) : ItemNull;
    js_property_set(obj, js_string_key("startContainer"), start);
    js_property_set(obj, js_string_key("endContainer"), end);
    js_property_set(obj, js_string_key("startOffset"),
        (Item){.item = i2it(r ? (int64_t)r->start.offset : 0)});
    js_property_set(obj, js_string_key("endOffset"),
        (Item){.item = i2it(r ? (int64_t)r->end.offset : 0)});
    bool collapsed = r && r->start.node == r->end.node &&
        r->start.offset == r->end.offset;
    js_property_set(obj, js_string_key("collapsed"), (Item){.item = b2it(collapsed)});
    return obj;
}

static DomElement* js_dom_testdriver_input_event_target(View* target) {
    if (!target) return nullptr;
    EditingSurface surface;
    if (editing_surface_from_target(target, &surface) &&
        editing_surface_is_rich(&surface) && surface.owner) {
        return surface.owner;
    }
    DomNode* node = static_cast<DomNode*>(target);
    while (node && !node->is_element()) {
        node = node->parent;
    }
    return node ? node->as_element() : nullptr;
}

static bool js_dom_input_intent_uses_transfer_payload(InputIntentType type) {
    switch (type) {
        case INPUT_INTENT_INSERT_FROM_PASTE:
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION:
        case INPUT_INTENT_INSERT_FROM_DROP:
        case INPUT_INTENT_DELETE_BY_DRAG:
        case INPUT_INTENT_DELETE_BY_CUT:
            return true;
        default:
            return false;
    }
}

static bool js_dom_testdriver_dispatch_input_event(EventContext* evcon,
                                                   View* target,
                                                   const char* type,
                                                   const EditingIntent* intent,
                                                   void* user) {
    (void)user;
    DomElement* dom_target = js_dom_testdriver_input_event_target(target);
    if (!evcon || !dom_target || !type || !intent) return false;

    Item ranges_arr = js_array_new(0);
    if (strcmp(type, "beforeinput") == 0 && evcon->editing_target_ranges_active) {
        for (uint32_t i = 0; i < evcon->editing_target_range_count; i++) {
            js_array_push(ranges_arr,
                js_dom_testdriver_static_range_item(&evcon->editing_target_ranges[i]));
        }
    }

    EditingSurface surface;
    bool has_surface = editing_surface_from_target(target, &surface);
    bool rich_transfer = has_surface && editing_surface_is_rich(&surface) &&
        js_dom_input_intent_uses_transfer_payload(intent->type);
    const char* data = rich_transfer ? nullptr : intent->data;
    Item data_transfer = rich_transfer
        ? js_data_transfer_new_with_strings(intent->data, intent->html_data)
        : ItemNull;

    Item ev = js_create_native_input_event(type,
        input_intent_type_name(intent->type),
        data,
        intent->is_composing,
        data_transfer,
        ranges_arr);
    Item target_item = js_dom_wrap_element(dom_target);
    js_dom_dispatch_event(target_item, ev);
    return js_event_is_default_prevented(ev);
}

struct JsDomTestdriverMutationArgs {
    View* fallback_view;
    int fallback_offset;
};

static bool js_dom_testdriver_rich_mutate(EventContext* evcon,
                                          DocState* state,
                                          const EditingSurface* surface,
                                          const EditingIntent* intent,
                                          void* user) {
    (void)evcon;
    if (intent && (intent->type == INPUT_INTENT_FORMAT_BOLD ||
                   intent->type == INPUT_INTENT_FORMAT_ITALIC ||
                   intent->type == INPUT_INTENT_FORMAT_UNDERLINE ||
                   intent->type == INPUT_INTENT_FORMAT_STRIKETHROUGH ||
                   intent->type == INPUT_INTENT_FORMAT_SUBSCRIPT ||
                   intent->type == INPUT_INTENT_FORMAT_SUPERSCRIPT)) {
        return editing_rich_default_format(state, surface, intent,
                                           nullptr, nullptr);
    }
    if (intent && intent->type == INPUT_INTENT_FORMAT_BLOCK) {
        return editing_rich_default_format_block(state, surface, intent,
                                                 nullptr, nullptr);
    }
    JsDomTestdriverMutationArgs* args = (JsDomTestdriverMutationArgs*)user;
    View* fallback_view = nullptr;
    int fallback_offset = 0;
    if (!state || !state->dom_selection ||
        state->dom_selection->range_count == 0) {
        fallback_view = args ? args->fallback_view : nullptr;
        fallback_offset = args ? args->fallback_offset : 0;
    }
    return editing_rich_default_replace(state, intent,
        fallback_view, fallback_offset, nullptr, nullptr);
}

static uint32_t js_dom_testdriver_u32(Item value) {
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
    DocState* state = js_dom_testdriver_state();
    if (!state) return (Item){.item = ITEM_FALSE};

    uint32_t wpt_key = js_dom_testdriver_u32(key_item);
    int mods = 0;
    if (js_is_truthy(shift_item)) mods |= RDT_MOD_SHIFT;
    if (js_is_truthy(ctrl_item)) mods |= RDT_MOD_CTRL;
    if (js_is_truthy(alt_item)) mods |= RDT_MOD_ALT;
    if (js_is_truthy(meta_item)) mods |= RDT_MOD_SUPER;

    InputIntent intent;
    memset(&intent, 0, sizeof(intent));
    intent.type = js_dom_testdriver_delete_intent(
        wpt_key, mods, js_dom_testdriver_selection_noncollapsed(state));
    if (intent.type == INPUT_INTENT_NONE) return (Item){.item = ITEM_FALSE};
    intent.key = wpt_key == 0xE003 ? RDT_KEY_BACKSPACE : RDT_KEY_DELETE;
    intent.mods = mods;

    int fallback_offset = 0;
    View* target = js_dom_testdriver_current_target(state, &fallback_offset);
    if (!target) return (Item){.item = ITEM_FALSE};

    EditingSurface surface;
    if (!js_dom_testdriver_rich_surface(target, &surface)) {
        return (Item){.item = ITEM_FALSE};
    }

    EventContext evcon;
    memset(&evcon, 0, sizeof(evcon));
    evcon.target_document = _js_current_document;
    evcon.event.key.type = RDT_EVENT_KEY_DOWN;
    evcon.event.key.key = intent.key;
    evcon.event.key.mods = mods;

    EditingDispatchHooks hooks;
    hooks.dispatch_input_event = js_dom_testdriver_dispatch_input_event;
    hooks.dispatch_lambda_event = nullptr;
    hooks.copy_selection = nullptr;
    hooks.user = nullptr;

    JsDomTestdriverMutationArgs mutate_args;
    mutate_args.fallback_view = target;
    mutate_args.fallback_offset = fallback_offset;

    EditingTransaction tx;
    memset(&tx, 0, sizeof(tx));
    tx.surface = &surface;
    tx.intent = &intent;
    tx.hooks = &hooks;
    tx.mutate = js_dom_testdriver_rich_mutate;
    tx.mutate_user = &mutate_args;
    tx.operation = "testdriver-key";
    tx.dispatch_input_without_mutation = false;
    tx.mutation_invalidates_layout = true;
    tx.mutation_invalidates_paint = true;

    bool prevented = false;
    bool mutated = false;
    bool ok = editing_run_transaction(&evcon, &tx, &prevented, &mutated, nullptr);
    if (ok && mutated && _js_current_document) {
        js_dom_mutation_notify(DOM_JS_MUTATION_TEXT, surface.owner, surface.owner);
        js_dom_queue_selectionchange(state->dom_selection);
    }
    log_debug("js_dom_testdriver_key: key=%u inputType=%s ok=%d prevented=%d mutated=%d",
              wpt_key, input_intent_type_name(intent.type),
              ok ? 1 : 0, prevented ? 1 : 0, mutated ? 1 : 0);
    return (Item){.item = b2it(ok && (prevented || mutated))};
}

static bool js_dom_exec_command_is_core_text(const char* cmd) {
    if (!cmd) return false;
    return strcasecmp(cmd, "insertText") == 0 ||
        strcasecmp(cmd, "insertHTML") == 0 ||
        strcasecmp(cmd, "insertParagraph") == 0 ||
        strcasecmp(cmd, "insertLineBreak") == 0 ||
        strcasecmp(cmd, "delete") == 0 ||
        strcasecmp(cmd, "forwardDelete") == 0;
}

static bool js_dom_exec_command_is_inline_format(const char* cmd) {
    if (!cmd) return false;
    return strcasecmp(cmd, "bold") == 0 ||
        strcasecmp(cmd, "italic") == 0 ||
        strcasecmp(cmd, "underline") == 0 ||
        strcasecmp(cmd, "strikethrough") == 0 ||
        strcasecmp(cmd, "subscript") == 0 ||
        strcasecmp(cmd, "superscript") == 0;
}

static bool js_dom_exec_command_is_block_structure(const char* cmd) {
    if (!cmd) return false;
    return strcasecmp(cmd, "formatBlock") == 0;
}

static bool js_dom_exec_command_is_native(const char* cmd) {
    return js_dom_exec_command_is_core_text(cmd) ||
        js_dom_exec_command_is_inline_format(cmd) ||
        js_dom_exec_command_is_block_structure(cmd);
}

static bool js_dom_exec_command_is_supported(const char* cmd) {
    if (!cmd) return false;
    if (js_dom_exec_command_is_native(cmd)) return true;
    return strcasecmp(cmd, "copy") == 0 ||
        strcasecmp(cmd, "cut") == 0 ||
        strcasecmp(cmd, "paste") == 0;
}

static bool js_dom_exec_command_map_intent(const char* cmd,
                                           const char* value,
                                           InputIntent* out) {
    if (!cmd || !out) return false;
    memset(out, 0, sizeof(*out));
    if (strcasecmp(cmd, "insertText") == 0) {
        out->type = INPUT_INTENT_INSERT_TEXT;
        out->data = value ? value : "";
        return true;
    }
    if (strcasecmp(cmd, "insertHTML") == 0) {
        out->type = INPUT_INTENT_INSERT_FROM_PASTE;
        out->data = value ? value : "";
        out->html_data = value ? value : "";
        out->data_mime = "text/html";
        return true;
    }
    if (strcasecmp(cmd, "insertParagraph") == 0) {
        out->type = INPUT_INTENT_INSERT_PARAGRAPH;
        return true;
    }
    if (strcasecmp(cmd, "insertLineBreak") == 0) {
        out->type = INPUT_INTENT_INSERT_LINE_BREAK;
        return true;
    }
    if (strcasecmp(cmd, "delete") == 0) {
        out->type = INPUT_INTENT_DELETE_CONTENT_BACKWARD;
        out->key = RDT_KEY_BACKSPACE;
        return true;
    }
    if (strcasecmp(cmd, "forwardDelete") == 0) {
        out->type = INPUT_INTENT_DELETE_CONTENT_FORWARD;
        out->key = RDT_KEY_DELETE;
        return true;
    }
    if (strcasecmp(cmd, "bold") == 0) {
        out->type = INPUT_INTENT_FORMAT_BOLD;
        return true;
    }
    if (strcasecmp(cmd, "italic") == 0) {
        out->type = INPUT_INTENT_FORMAT_ITALIC;
        return true;
    }
    if (strcasecmp(cmd, "underline") == 0) {
        out->type = INPUT_INTENT_FORMAT_UNDERLINE;
        return true;
    }
    if (strcasecmp(cmd, "strikethrough") == 0) {
        out->type = INPUT_INTENT_FORMAT_STRIKETHROUGH;
        return true;
    }
    if (strcasecmp(cmd, "subscript") == 0) {
        out->type = INPUT_INTENT_FORMAT_SUBSCRIPT;
        return true;
    }
    if (strcasecmp(cmd, "superscript") == 0) {
        out->type = INPUT_INTENT_FORMAT_SUPERSCRIPT;
        return true;
    }
    if (strcasecmp(cmd, "formatBlock") == 0) {
        out->type = INPUT_INTENT_FORMAT_BLOCK;
        out->data = value ? value : "";
        return true;
    }
    return false;
}

static bool js_dom_exec_command_has_rich_target(void) {
    DocState* state = js_dom_testdriver_state();
    if (!state) return false;
    int fallback_offset = 0;
    View* target = js_dom_testdriver_current_target(state, &fallback_offset);
    if (!target) return false;
    EditingSurface surface;
    return js_dom_testdriver_rich_surface(target, &surface);
}

static Item js_dom_exec_command_native(Item* args, int argc) {
    if (!_js_current_document || !args || argc < 1) {
        return (Item){.item = ITEM_FALSE};
    }
    const char* cmd = fn_to_cstr(args[0]);
    if (!js_dom_exec_command_is_native(cmd)) {
        return (Item){.item = ITEM_FALSE};
    }

    DocState* state = js_dom_testdriver_state();
    if (!state) return (Item){.item = ITEM_FALSE};

    const char* value = argc >= 3 ? fn_to_cstr(args[2]) : "";
    InputIntent intent;
    if (!js_dom_exec_command_map_intent(cmd, value, &intent)) {
        return (Item){.item = ITEM_FALSE};
    }

    int fallback_offset = 0;
    View* target = js_dom_testdriver_current_target(state, &fallback_offset);
    if (!target) return (Item){.item = ITEM_FALSE};

    EditingSurface surface;
    if (!js_dom_testdriver_rich_surface(target, &surface)) {
        return (Item){.item = ITEM_FALSE};
    }

    EventContext evcon;
    memset(&evcon, 0, sizeof(evcon));
    evcon.target_document = _js_current_document;
    evcon.event.key.type = RDT_EVENT_KEY_DOWN;
    evcon.event.key.key = intent.key;
    evcon.event.key.mods = intent.mods;

    EditingDispatchHooks hooks;
    hooks.dispatch_input_event = js_dom_testdriver_dispatch_input_event;
    hooks.dispatch_lambda_event = nullptr;
    hooks.copy_selection = nullptr;
    hooks.user = nullptr;

    JsDomTestdriverMutationArgs mutate_args;
    mutate_args.fallback_view = target;
    mutate_args.fallback_offset = fallback_offset;

    EditingTransaction tx;
    memset(&tx, 0, sizeof(tx));
    tx.surface = &surface;
    tx.intent = &intent;
    tx.hooks = &hooks;
    tx.mutate = js_dom_testdriver_rich_mutate;
    tx.mutate_user = &mutate_args;
    tx.operation = "execCommand";
    tx.dispatch_input_without_mutation = false;
    tx.mutation_invalidates_layout = true;
    tx.mutation_invalidates_paint = true;

    bool prevented = false;
    bool mutated = false;
    bool ok = editing_run_transaction(&evcon, &tx, &prevented, &mutated, nullptr);
    if (ok && mutated && _js_current_document) {
        js_dom_mutation_notify(DOM_JS_MUTATION_TEXT, surface.owner, surface.owner);
        js_dom_queue_selectionchange(state->dom_selection);
    }
    log_debug("js_dom_exec_command_native: command=%s inputType=%s ok=%d prevented=%d mutated=%d",
              cmd ? cmd : "?", input_intent_type_name(intent.type),
              ok ? 1 : 0, prevented ? 1 : 0, mutated ? 1 : 0);
    return (Item){.item = b2it(ok && (prevented || mutated))};
}

static uintptr_t js_dom_exec_command_inline_tag(const char* cmd) {
    if (!cmd) return 0;
    if (strcasecmp(cmd, "bold") == 0) return HTM_TAG_B;
    if (strcasecmp(cmd, "italic") == 0) return HTM_TAG_I;
    if (strcasecmp(cmd, "underline") == 0) return HTM_TAG_U;
    if (strcasecmp(cmd, "strikethrough") == 0) return HTM_TAG_S;
    if (strcasecmp(cmd, "subscript") == 0) return HTM_TAG_SUB;
    if (strcasecmp(cmd, "superscript") == 0) return HTM_TAG_SUP;
    return 0;
}

static bool js_dom_exec_command_inline_tag_matches(const char* cmd,
                                                   uintptr_t tag) {
    if (!cmd || !tag) return false;
    if (strcasecmp(cmd, "bold") == 0) {
        return tag == HTM_TAG_B || tag == HTM_TAG_STRONG;
    }
    if (strcasecmp(cmd, "italic") == 0) {
        return tag == HTM_TAG_I || tag == HTM_TAG_EM;
    }
    if (strcasecmp(cmd, "underline") == 0) {
        return tag == HTM_TAG_U;
    }
    if (strcasecmp(cmd, "strikethrough") == 0) {
        return tag == HTM_TAG_S || tag == HTM_TAG_STRIKE;
    }
    if (strcasecmp(cmd, "subscript") == 0) {
        return tag == HTM_TAG_SUB;
    }
    if (strcasecmp(cmd, "superscript") == 0) {
        return tag == HTM_TAG_SUP;
    }
    return false;
}

static bool js_dom_exec_command_query_inline_state(const char* cmd) {
    uintptr_t tag = js_dom_exec_command_inline_tag(cmd);
    if (!tag) return false;
    DocState* state = js_dom_testdriver_state();
    if (!state || !state->dom_selection ||
        state->dom_selection->range_count == 0 ||
        !state->dom_selection->ranges[0]) {
        return false;
    }

    DomBoundary boundary = dom_selection_focus_boundary(state->dom_selection);
    DomNode* node = boundary.node;
    if (!node) return false;

    EditingSurface surface;
    if (!editing_surface_from_target(static_cast<View*>(node), &surface) ||
        !editing_surface_is_rich(&surface)) {
        return false;
    }

    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur->is_element()) {
            DomElement* elem = cur->as_element();
            if (elem && js_dom_exec_command_inline_tag_matches(cmd, elem->tag())) {
                return true;
            }
        }
        if (cur == static_cast<DomNode*>(surface.owner)) break;
    }
    return false;
}

static bool js_dom_exec_command_block_tag(uintptr_t tag) {
    return tag == HTM_TAG_P ||
        tag == HTM_TAG_DIV ||
        tag == HTM_TAG_H1 ||
        tag == HTM_TAG_H2 ||
        tag == HTM_TAG_H3 ||
        tag == HTM_TAG_H4 ||
        tag == HTM_TAG_H5 ||
        tag == HTM_TAG_H6 ||
        tag == HTM_TAG_BLOCKQUOTE ||
        tag == HTM_TAG_PRE;
}

static const char* js_dom_exec_command_query_format_block_value(void) {
    DocState* state = js_dom_testdriver_state();
    if (!state || !state->dom_selection ||
        state->dom_selection->range_count == 0 ||
        !state->dom_selection->ranges[0]) {
        return "";
    }

    DomBoundary boundary =
        dom_selection_focus_boundary(state->dom_selection);
    DomNode* node = boundary.node;
    if (!node) return "";

    EditingSurface surface;
    if (!editing_surface_from_target(static_cast<View*>(node), &surface) ||
        !editing_surface_is_rich(&surface)) {
        return "";
    }

    DomNode* owner_node = static_cast<DomNode*>(surface.owner);
    for (DomNode* cur = node; cur && cur != owner_node; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = cur->as_element();
        if (elem && js_dom_exec_command_block_tag(elem->tag())) {
            return elem->tag_name ? elem->tag_name : "";
        }
    }
    return "";
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

static inline void dom_pre_remove(DomNode* child) {
    DocState* st = js_dom_current_state();
    if (st && child) {
        dom_mutation_pre_remove(st, child);

        View* focused = focus_get(st);
        if (focused && js_dom_node_contains(child, (DomNode*)focused)) {
            focus_clear_preserve_selection(st);
        } else {
            View* caret_view = caret_get_view(st);
            if (caret_view && js_dom_node_contains(child, (DomNode*)caret_view)) {
                state_store_legacy_caret_clear(st);
            }

            View* anchor_view = nullptr;
            View* focus_view = nullptr;
            if (selection_get_extent_views(st, &anchor_view, &focus_view) &&
                ((anchor_view && js_dom_node_contains(child, (DomNode*)anchor_view)) ||
                 (focus_view && js_dom_node_contains(child, (DomNode*)focus_view)))) {
                state_store_legacy_selection_clear(st);
            }
        }
    }
    if (child && js_document_active_element &&
        js_dom_node_contains(child, (DomNode*)js_document_active_element)) {
        js_document_active_element = nullptr;
    }
    js_dom_record_mutation_detail(DOM_JS_MUTATION_CHILD_REMOVE, child,
                                  child ? child->parent : nullptr, 0);
}
static inline void dom_post_insert(DomNode* parent, DomNode* node) {
    DocState* st = js_dom_current_state();
    if (st && parent && node) dom_mutation_post_insert(st, parent, node);
    js_dom_record_mutation_detail(DOM_JS_MUTATION_CHILD_INSERT, node, parent, 0);
}
static inline void dom_text_replace_data(DomText* text, uint32_t off,
                                         uint32_t cnt, uint32_t repl_len) {
    DocState* st = js_dom_current_state();
    if (st && text) dom_mutation_text_replace_data(st, text, off, cnt, repl_len);
    js_dom_record_mutation_detail(DOM_JS_MUTATION_TEXT, (DomNode*)text,
                                  text ? text->parent : nullptr, 0);
}

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

static DomDocument* js_dom_text_owner_document(DomText* text_node) {
    DomNode* parent = text_node ? text_node->parent : nullptr;
    while (parent && !parent->is_element()) parent = parent->parent;
    if (parent && parent->is_element()) return parent->as_element()->doc;
    return _js_current_document;
}

static void js_dom_throw_index_size_error(const char* message) {
    Item name = (Item){.item = s2it(heap_create_name("IndexSizeError"))};
    Item msg = (Item){.item = s2it(heap_create_name(
        message ? message : "The index is not in the allowed range."))};
    js_throw_value(js_new_error_with_name(name, msg));
}

static bool js_dom_replace_text_data(DomText* text_node, uint32_t offset,
                                     uint32_t count, const char* repl_chars) {
    if (!text_node) return false;
    if (!repl_chars) repl_chars = "";

    uint32_t old_u16_len = dom_text_utf16_length(text_node);
    if (offset > old_u16_len) {
        js_dom_throw_index_size_error("The offset is larger than the CharacterData length.");
        return false;
    }
    uint32_t available = old_u16_len - offset;
    if (count > available) count = available;

    uint32_t u8_off = dom_text_utf16_to_utf8(text_node, offset);
    uint32_t u8_end = dom_text_utf16_to_utf8(text_node, offset + count);
    if (u8_end < u8_off) u8_end = u8_off;

    size_t repl_len = strlen(repl_chars);
    uint32_t repl_u16_len = js_dom_utf16_length_from_utf8(repl_chars, repl_len);
    const char* old_text = text_node->text ? text_node->text : "";
    size_t old_len = text_node->length;
    size_t prefix_len = u8_off;
    size_t suffix_len = old_len > u8_end ? old_len - u8_end : 0;
    size_t new_len = prefix_len + repl_len + suffix_len;

    char* buf = (char*)mem_alloc(new_len + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return false;
    if (prefix_len) memcpy(buf, old_text, prefix_len);
    if (repl_len) memcpy(buf + prefix_len, repl_chars, repl_len);
    if (suffix_len) memcpy(buf + prefix_len + repl_len, old_text + u8_end, suffix_len);
    buf[new_len] = '\0';

    DomDocument* doc = js_dom_text_owner_document(text_node);
    String* s = js_dom_create_document_string(doc, buf, new_len);
    mem_free(buf);
    if (!s) return false;

    text_node->native_string = s;
    text_node->text = s->chars;
    text_node->length = new_len;
    dom_text_replace_data(text_node, offset, count, repl_u16_len);
    js_dom_mutation_notify();
    log_debug("js_dom_replace_text_data: offset=%u count=%u replacement_u16=%u",
              offset, count, repl_u16_len);
    return true;
}

/**
 * Reset JS DOM state for batch mode. Clears cached document proxy and
 * document pointer so next file starts fresh.
 */
static void expando_reset(); // forward declaration
static void reset_dom_wrapper_cache(); // forward declaration
// Phase 6E: text-control helpers are shared with Radiant event/render paths.
#include "../../radiant/text_control.hpp"
#define tc_is_text_control_elem(e)      tc_is_text_control(e)
extern "C" void js_dom_batch_reset() {
    DocState* state = js_dom_current_state();
    js_dom_selection_reset();
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
    expando_reset();
    tc_reset_focus_state(state);
}

// ============================================================================
// DOM Expando Properties
// Allows arbitrary JS values to be stored on DOM elements, e.g.
//   element._myData = { ... }; let x = element._myData;
// Uses a global side-table mapping DomNode* → JS Map of expandos.
// ============================================================================

// simple open-addressing hash table for DomNode* → Item (JS Map)
#define EXPANDO_TABLE_SIZE 256
static struct { DomNode* key; Item map; } _expando_table[EXPANDO_TABLE_SIZE];
static bool _expando_initialized = false;

static void expando_init() {
    if (!_expando_initialized) {
        memset(_expando_table, 0, sizeof(_expando_table));
        _expando_initialized = true;
    }
}

static Item expando_get_map(DomNode* node) {
    expando_init();
    uintptr_t h = ((uintptr_t)node >> 4) % EXPANDO_TABLE_SIZE;
    for (int i = 0; i < EXPANDO_TABLE_SIZE; i++) {
        int idx = (h + i) % EXPANDO_TABLE_SIZE;
        if (_expando_table[idx].key == node) return _expando_table[idx].map;
        if (_expando_table[idx].key == nullptr) return ItemNull;
    }
    return ItemNull;
}

static Item expando_get_or_create_map(DomNode* node) {
    expando_init();
    uintptr_t h = ((uintptr_t)node >> 4) % EXPANDO_TABLE_SIZE;
    // look for existing entry or first empty slot
    int first_empty = -1;
    for (int i = 0; i < EXPANDO_TABLE_SIZE; i++) {
        int idx = (h + i) % EXPANDO_TABLE_SIZE;
        if (_expando_table[idx].key == node) return _expando_table[idx].map;
        if (_expando_table[idx].key == nullptr) {
            if (first_empty < 0) first_empty = idx;
            break;
        }
    }
    if (first_empty < 0) return ItemNull; // table full
    Item m = js_new_object();
    _expando_table[first_empty].key = node;
    _expando_table[first_empty].map = m;
    return m;
}

static bool expando_map_has_key(Item exp_map, Item key) {
    if (get_type_id(exp_map) != LMD_TYPE_MAP || !exp_map.map) return false;
    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* s = it2s(key);
    if (!s) return false;
    TypeMap* tm = (TypeMap*)exp_map.map->type;
    if (!tm) return false;
    return typemap_hash_lookup(tm, s->chars, (int)s->len) != nullptr;
}

static void expando_reset() {
    memset(_expando_table, 0, sizeof(_expando_table));
    _expando_initialized = false;
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
    args[0] = (Item){.item = s2it(heap_create_name("event"))};
    args[1] = (Item){.item = s2it(heap_create_name(attr_value))};
    Item fn = js_new_function_from_string(args, 2);
    if (get_type_id(fn) != LMD_TYPE_FUNC) {
        log_error("js_dom_event_attr: failed to compile %s handler", prop_name);
        if (js_check_exception()) {
            (void)js_clear_exception();
        }
        return;
    }

    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item != ITEM_NULL) {
        js_property_set(exp_map, (Item){.item = s2it(heap_create_name(prop_name))}, fn);
    }
}

static void js_dom_clear_event_attr_expando(DomElement* elem, const char* attr_name) {
    if (!elem || !attr_name) return;

    char prop_name[64];
    if (!js_dom_event_attr_name(attr_name, prop_name, sizeof(prop_name))) return;

    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item != ITEM_NULL) {
        js_property_set(exp_map, (Item){.item = s2it(heap_create_name(prop_name))}, ItemNull);
    }
}

extern "C" bool js_dom_set_event_handler_function(void* dom_elem,
                                                  const char* attr_name,
                                                  Item fn) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !attr_name || get_type_id(fn) != LMD_TYPE_FUNC) return false;

    char prop_name[64];
    if (!js_dom_event_attr_name(attr_name, prop_name, sizeof(prop_name))) return false;

    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item == ITEM_NULL) return false;

    js_property_set(exp_map, (Item){.item = s2it(heap_create_name(prop_name))}, fn);
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

// Returns the lowercased input `type` attribute (e.g. "checkbox", "radio",
// "submit", "button", "text"). Falls back to "text" when missing.
static const char* _input_type_lower(DomElement* elem) {
    static __thread char buf[24];
    const char* raw = dom_element_get_attribute(elem, "type");
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
        Item key = (Item){.item = s2it(heap_create_name("__checked"))};
        Item v = js_property_get(exp, key);
        if (v.item != ITEM_NULL && !is_js_undefined(v)) return js_is_truthy(v);
    }
    // not initialised yet — derive from content attribute.
    return dom_element_has_attribute(elem, "checked");
}

static void _set_checkedness(DomElement* elem, bool v) {
    DocState* state = _state_for_element(elem);
    if (state) {
        form_control_set_checked(state, (View*)elem, v);
        return;
    }

    Item exp = expando_get_or_create_map((DomNode*)elem);
    if (exp.item == ITEM_NULL) return;
    Item key = (Item){.item = s2it(heap_create_name("__checked"))};
    js_property_set(exp, key, (Item){.item = b2it(v)});
}

// Exposed for js_dom_events.cpp pre/post-click activation.
extern "C" bool js_dom_is_checkbox_or_radio(void* dom_elem) {
    return _is_checkbox_or_radio((DomElement*)dom_elem);
}
extern "C" bool js_dom_get_checkedness(void* dom_elem) {
    return _get_checkedness((DomElement*)dom_elem);
}
extern "C" void js_dom_set_checkedness(void* dom_elem, bool v) {
    _set_checkedness((DomElement*)dom_elem, v);
}
extern "C" const char* js_dom_input_type_lower(void* dom_elem) {
    return _input_type_lower((DomElement*)dom_elem);
}
extern "C" const char* js_dom_tag_name_raw(void* dom_elem) {
    DomElement* e = (DomElement*)dom_elem;
    return e ? e->tag_name : nullptr;
}
extern "C" bool js_dom_is_disabled(void* dom_elem) {
    DomElement* e = (DomElement*)dom_elem;
    return e && dom_element_has_attribute(e, "disabled");
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

    if (elem->id && elem->id[0] != '\0') {
        Item key = (Item){.item = s2it(heap_create_name(elem->id))};
        // HTML named-property access on Window reflects the *current* element
        // with this id. Register when there is no own property yet, and also
        // refresh a stale auto-registered wrapper whose element was detached
        // (e.g. after `innerHTML` replaced the subtree). Do NOT clobber a
        // genuine user-assigned global, and keep the first connected element in
        // tree order when ids collide within the current document.
        bool do_register = true;
        if (it2b(js_has_own_property(global, key))) {
            DomNode* exn = static_cast<DomNode*>(
                js_dom_unwrap_element(js_property_get(global, key)));
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
            js_property_set(global, key, wrapped);
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

// ============================================================================
// DOM Context Management
// ============================================================================

extern "C" void js_dom_set_document(void* dom_doc) {
    _js_current_document = (DomDocument*)dom_doc;
    _js_main_document = (DomDocument*)dom_doc;
    if (js_document_proxy_item.item != ITEM_NULL &&
        get_type_id(js_document_proxy_item) == LMD_TYPE_MAP &&
        js_document_proxy_item.map) {
        js_document_proxy_item.map->map_kind = MAP_KIND_DOC_PROXY;
        js_document_proxy_item.map->type = (void*)&js_document_proxy_marker;
        js_document_proxy_item.map->data = dom_doc;
        js_document_proxy_item.map->data_cap = 0;
    }
    if (dom_doc) {
        js_doc_mark_has_browsing_context(dom_doc);
        DomDocument* doc = (DomDocument*)dom_doc;
        if (doc->pool) {
            css_property_system_init(doc->pool);
        }
        // populate global object with element IDs (browser-like named access on Window)
        if (doc->root) {
            js_dom_register_named_elements(doc->root);
        }
        // install window.getSelection() global
        js_dom_selection_install_globals();
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
        js_dom_install_window_location_history_globals();
        Item global = js_get_global_this();
        js_property_set(global, js_string_key("__lambda_testdriver_key"),
            js_new_function((void*)js_dom_testdriver_key, 5));
    }
    log_debug("js_dom_set_document: set document=%p", dom_doc);
}

extern "C" void* js_dom_get_document(void) {
    return (void*)_js_current_document;
}

// ============================================================================
// Document-as-Node stub
// (Lazy DomElement with tag "#document" so JS Range/Selection APIs can
// accept `document` (or a foreign-doc wrapper) as a container.)
// ============================================================================

static Item lookup_foreign_doc_wrapper(DomDocument* doc); // fwd decl

extern "C" void* js_dom_get_or_create_doc_node(void* doc_v) {
    DomDocument* doc = (DomDocument*)doc_v;
    if (!doc) return nullptr;
    if (doc->js_doc_node) return doc->js_doc_node;
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
    doc->js_doc_node = stub;
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

// ============================================================================
// DOM Wrapping / Unwrapping
// ============================================================================

static const int DOM_WRAPPER_CACHE_CHUNK_SIZE = 4096;
struct DomWrapperCacheEntry {
    DomNode* node;
    uint64_t item;
};
struct DomWrapperCacheChunk {
    DomWrapperCacheEntry entries[DOM_WRAPPER_CACHE_CHUNK_SIZE];
    int count;
    DomWrapperCacheChunk* next;
};
static __thread DomWrapperCacheChunk* s_dom_wrapper_cache_head = nullptr;
static __thread DomWrapperCacheChunk* s_dom_wrapper_cache_tail = nullptr;

static Item lookup_dom_wrapper(DomNode* node) {
    for (DomWrapperCacheChunk* chunk = s_dom_wrapper_cache_head; chunk; chunk = chunk->next) {
        for (int i = 0; i < chunk->count; i++) {
            if (chunk->entries[i].node == node) {
                return (Item){.item = chunk->entries[i].item};
            }
        }
    }
    return ItemNull;
}

static DomWrapperCacheChunk* alloc_dom_wrapper_cache_chunk() {
    DomWrapperCacheChunk* chunk = (DomWrapperCacheChunk*)mem_alloc(
        sizeof(DomWrapperCacheChunk), MEM_CAT_JS_RUNTIME);
    if (!chunk) return nullptr;
    memset(chunk, 0, sizeof(*chunk));
    if (!s_dom_wrapper_cache_head) {
        s_dom_wrapper_cache_head = chunk;
        s_dom_wrapper_cache_tail = chunk;
    } else {
        s_dom_wrapper_cache_tail->next = chunk;
        s_dom_wrapper_cache_tail = chunk;
    }
    return chunk;
}

static void cache_dom_wrapper(DomNode* node, Item wrapper) {
    if (!node || wrapper.item == ITEM_NULL) return;
    DomWrapperCacheChunk* chunk = s_dom_wrapper_cache_tail;
    if (!chunk || chunk->count >= DOM_WRAPPER_CACHE_CHUNK_SIZE) {
        chunk = alloc_dom_wrapper_cache_chunk();
        if (!chunk) return;
    }
    int index = chunk->count++;
    chunk->entries[index].node = node;
    chunk->entries[index].item = wrapper.item;
    heap_register_gc_root(&chunk->entries[index].item);
}

static void reset_dom_wrapper_cache() {
    DomWrapperCacheChunk* chunk = s_dom_wrapper_cache_head;
    while (chunk) {
        for (int i = 0; i < chunk->count; i++) {
            heap_unregister_gc_root(&chunk->entries[i].item);
            chunk->entries[i].node = nullptr;
            chunk->entries[i].item = 0;
        }
        DomWrapperCacheChunk* next = chunk->next;
        mem_free(chunk);
        chunk = next;
    }
    s_dom_wrapper_cache_head = nullptr;
    s_dom_wrapper_cache_tail = nullptr;
}

extern "C" Item js_dom_wrap_element(void* dom_elem) {
    if (!dom_elem) return ItemNull;

    DomNode* node = (DomNode*)dom_elem;
    Item cached = lookup_dom_wrapper(node);
    if (cached.item != ITEM_NULL) return cached;

    // If this DomNode is a document stub, return the document proxy / foreign
    // doc wrapper instead so identity comparisons in JS (e.g. `r.startContainer
    // === document`) work.
    if (node->is_element()) {
        DomElement* e = node->as_element();
        if (e->doc && e->doc->js_doc_node == (void*)e) {
            Item proxy = doc_to_proxy_item(e->doc);
            if (proxy.item != ITEM_NULL) return proxy;
        }
    }
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_DOM;
    wrapper->type = (void*)&js_dom_type_marker;  // DOM marker
    wrapper->data = dom_elem;                     // store DomNode* directly
    wrapper->data_cap = 0;

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        int attr_count = 0;
        const char** attr_names = dom_element_get_attribute_names(elem, &attr_count);
        for (int i = 0; attr_names && i < attr_count; i++) {
            const char* name = attr_names[i];
            const char* value = dom_element_get_attribute(elem, name);
            js_dom_compile_event_attr_to_expando(elem, name, value);
        }
        log_debug("js_dom_wrap_element: wrapped DomElement tag='%s' as Map=%p",
                  elem->tag_name ? elem->tag_name : "(null)", (void*)wrapper);
    } else if (node->is_text()) {
        log_debug("js_dom_wrap_element: wrapped DomText as Map=%p", (void*)wrapper);
    }

    Item wrapped = (Item){.map = wrapper};
    cache_dom_wrapper(node, wrapped);
    return wrapped;
}

extern "C" void* js_dom_unwrap_element(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return nullptr;

    Map* m = item.map;
    if (m->type == (void*)&js_dom_type_marker) {
        return m->data;
    }
    // document proxy / foreign-doc wrapper → return the doc-stub DomElement
    // (lazy-create) so JS Range/Selection APIs accept `document` as a container.
    if (m->map_kind == MAP_KIND_DOC_PROXY) {
        DomDocument* doc = (DomDocument*)(m->data ? m->data : (void*)_js_main_document);
        if (!doc) doc = _js_current_document;
        return js_dom_get_or_create_doc_node(doc);
    }
    if (m->map_kind == MAP_KIND_FOREIGN_DOC) {
        return js_dom_get_or_create_doc_node(m->data);
    }
    return nullptr;
}

extern "C" bool js_is_dom_node(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_dom_type_marker;
}

struct SelectOptionsOwnerEntry {
    Array* array;
    DomElement* owner;
    int kind;
};

static const int SELECT_COLLECTION_OPTIONS = 1;
static const int SELECT_COLLECTION_SELECTED_OPTIONS = 2;
static const int SELECT_OPTIONS_OWNER_CACHE_SIZE = 4096;
static __thread SelectOptionsOwnerEntry s_select_options_owners[SELECT_OPTIONS_OWNER_CACHE_SIZE] = {};
static __thread int s_select_options_owner_count = 0;

static void _register_select_options_owner(Item collection, DomElement* owner, int kind) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !collection.array || !owner) return;
    for (int i = 0; i < s_select_options_owner_count; i++) {
        if (s_select_options_owners[i].array == collection.array) {
            s_select_options_owners[i].owner = owner;
            s_select_options_owners[i].kind = kind;
            return;
        }
    }
    if (s_select_options_owner_count >= SELECT_OPTIONS_OWNER_CACHE_SIZE) return;
    s_select_options_owners[s_select_options_owner_count].array = collection.array;
    s_select_options_owners[s_select_options_owner_count].owner = owner;
    s_select_options_owners[s_select_options_owner_count].kind = kind;
    s_select_options_owner_count++;
}

static DomElement* _select_options_owner(Item collection, int* out_kind) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !collection.array) return nullptr;
    for (int i = 0; i < s_select_options_owner_count; i++) {
        if (s_select_options_owners[i].array == collection.array) {
            if (out_kind) *out_kind = s_select_options_owners[i].kind;
            return s_select_options_owners[i].owner;
        }
    }
    return nullptr;
}

extern "C" Item js_get_this(void);
extern "C" Item js_dom_element_method(Item elem_item, Item method_name, Item* args, int argc);
extern "C" Item js_new_function(void* func_ptr, int param_count);
static Item js_dom_text_replace_data_method(DomText* text_node, Item offset_arg,
                                            Item count_arg, Item data_arg);
static Item js_dom_text_insert_data_method(DomText* text_node, Item offset_arg,
                                           Item data_arg);
static Item js_dom_text_append_data_method(DomText* text_node, Item data_arg);
static Item js_dom_text_delete_data_method(DomText* text_node, Item offset_arg,
                                           Item count_arg);
static Item js_dom_text_substring_data_method(DomText* text_node, Item offset_arg,
                                              Item count_arg);
static Item js_text_replace_data_method(Item offset_arg, Item count_arg, Item data_arg);
static Item js_text_insert_data_method(Item offset_arg, Item data_arg);
static Item js_text_append_data_method(Item data_arg);
static Item js_text_delete_data_method(Item offset_arg, Item count_arg);
static Item js_text_substring_data_method(Item offset_arg, Item count_arg);
static void _value_mark_dirty(DomElement* elem);

static Item _collection_named_item(Item name_arg) {
    const char* name = fn_to_cstr(name_arg);
    if (!name || !*name) return ItemNull;
    Item self = js_get_this();
    if (get_type_id(self) != LMD_TYPE_ARRAY || !self.array) return ItemNull;
    for (int64_t i = 0; i < self.array->length; i++) {
        Item item = js_array_get_int(self, i);
        DomElement* elem = (DomElement*)js_dom_unwrap_element(item);
        if (!elem) continue;
        const char* id = dom_element_get_attribute(elem, "id");
        if (id && strcmp(id, name) == 0) return item;
        const char* nm = dom_element_get_attribute(elem, "name");
        if (nm && strcmp(nm, name) == 0) return item;
    }
    return ItemNull;
}

static void _decorate_dom_collection(Item collection, const char* ctor_name) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !ctor_name) return;
    Item named_key = (Item){.item = s2it(heap_create_name("namedItem"))};
    Item existing = js_property_get(collection, named_key);
    if (get_type_id(existing) != LMD_TYPE_FUNC) {
        js_property_set(collection, named_key, js_new_function((void*)_collection_named_item, 1));
    }
    Item ctor = js_property_get(js_get_global_this(),
        (Item){.item = s2it(heap_create_name(ctor_name))});
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_property_set(collection, (Item){.item = s2it(heap_create_name("constructor"))}, ctor);
    }
}

static Item js_dom_matches_method(Item selector) {
    Item self = js_get_this();
    Item method = (Item){.item = s2it(heap_create_name("matches"))};
    return js_dom_element_method(self, method, &selector, 1);
}

static Item js_dom_focus_method(Item elem_item) {
    Item self = js_dom_unwrap_element(elem_item) ? elem_item : js_get_this();
    Item method = (Item){.item = s2it(heap_create_name("focus"))};
    return js_dom_element_method(self, method, NULL, 0);
}

static Item js_dom_blur_method(Item elem_item) {
    Item self = js_dom_unwrap_element(elem_item) ? elem_item : js_get_this();
    Item method = (Item){.item = s2it(heap_create_name("blur"))};
    return js_dom_element_method(self, method, NULL, 0);
}

static Item js_dom_select_method(Item elem_item) {
    Item self = js_dom_unwrap_element(elem_item) ? elem_item : js_get_this();
    Item method = (Item){.item = s2it(heap_create_name("select"))};
    return js_dom_element_method(self, method, NULL, 0);
}

// ============================================================================
// Document Proxy Object
// ============================================================================

extern "C" bool js_is_document_proxy(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->map_kind == MAP_KIND_DOC_PROXY || m->map_kind == MAP_KIND_FOREIGN_DOC;
}

// Returns the DomDocument* if `item` is a foreign-doc wrapper, else null.
extern "C" void* js_get_foreign_doc(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return nullptr;
    Map* m = item.map;
    if (m->map_kind != MAP_KIND_FOREIGN_DOC) return nullptr;
    if (m->type != (void*)&js_foreign_doc_marker) return nullptr;
    return m->data;
}

// Returns true if `item` is the document.implementation singleton.
extern "C" bool js_is_dom_implementation(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_dom_implementation_marker;
}

extern "C" Item js_get_document_object_value() {
    if (js_document_proxy_item.item != ITEM_NULL) {
        if (get_type_id(js_document_proxy_item) == LMD_TYPE_MAP &&
            js_document_proxy_item.map) {
            js_document_proxy_item.map->map_kind = MAP_KIND_DOC_PROXY;
            js_document_proxy_item.map->type = (void*)&js_document_proxy_marker;
            js_document_proxy_item.map->data = _js_main_document;
            js_document_proxy_item.map->data_cap = 0;
        }
        return js_document_proxy_item;
    }
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_DOC_PROXY;
    wrapper->type = (void*)&js_document_proxy_marker;
    wrapper->data = _js_main_document;
    wrapper->data_cap = 0;
    js_document_proxy_item = (Item){.map = wrapper};
    heap_register_gc_root(&js_document_proxy_item.item);
    return js_document_proxy_item;
}

// Dispatch method calls on the document proxy object.
// Routes to js_document_method which handles getElementById, querySelector, etc.
extern "C" Item js_document_proxy_method(Item method_name, Item* args, int argc) {
    return js_document_method(method_name, args, argc);
}

// Dispatch property access on the document proxy object.
// Routes to js_document_get_property which handles body, documentElement, etc.
extern "C" Item js_document_proxy_get_property(Item prop_name) {
    return js_document_get_property(prop_name);
}

// Dispatch property set on the document proxy object.
// NOTE: Must use map_put directly instead of js_property_set to avoid
// infinite recursion (js_property_set dispatches back here for MAP_KIND_DOM).
extern "C" Item js_document_proxy_set_property(Item prop_name, Item value) {
    if (get_type_id(prop_name) == LMD_TYPE_STRING) {
        String* s = it2s(prop_name);
        if (s && s->len == 4 && strncmp(s->chars, "href", 4) == 0) {
            const char* next_url = fn_to_cstr(value);
            DomDocument* doc = _js_current_document ? _js_current_document : _js_main_document;
            if (doc && next_url && next_url[0]) {
                if (doc->pending_navigation_url) {
                    mem_free(doc->pending_navigation_url);
                }
                doc->pending_navigation_url = mem_strdup(next_url, MEM_CAT_DOM);
                log_info("js_location_set_href: pending navigation to %s", next_url);
            }
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
    js_property_set(exp_map, prop_name, value);
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
struct ForeignDocCacheEntry { DomDocument* doc; uint64_t item; };
static __thread ForeignDocCacheEntry s_foreign_doc_cache[FOREIGN_DOC_CACHE_SIZE] = {};
static __thread int s_foreign_doc_cache_count = 0;

// Side table: documents that have a non-null defaultView (browsing context).
// Main doc and iframe content docs go here. Foreign docs from
// document.implementation.create*Document do not.
static const int DOC_WIN_TABLE_SIZE = 32;
static __thread DomDocument* s_doc_with_window[DOC_WIN_TABLE_SIZE] = {};
static __thread int s_doc_with_window_count = 0;
extern "C" bool js_doc_has_browsing_context(void* doc) {
    if (!doc) return false;
    for (int i = 0; i < s_doc_with_window_count; i++) {
        if (s_doc_with_window[i] == (DomDocument*)doc) return true;
    }
    return false;
}
extern "C" void js_doc_mark_has_browsing_context(void* doc) {
    if (!doc) return;
    if (js_doc_has_browsing_context(doc)) return;
    if (s_doc_with_window_count < DOC_WIN_TABLE_SIZE) {
        s_doc_with_window[s_doc_with_window_count++] = (DomDocument*)doc;
    }
}

// iframe element -> foreign DomDocument* (lazy created on first access).
struct IframeContentEntry {
    DomElement* iframe;
    DomDocument* doc;
};
static const int IFRAME_CACHE_SIZE = 32;
static __thread IframeContentEntry s_iframe_cache[IFRAME_CACHE_SIZE] = {};
static __thread int s_iframe_cache_count = 0;
static IframeContentEntry* lookup_iframe_entry(DomElement* iframe) {
    for (int i = 0; i < s_iframe_cache_count; i++) {
        if (s_iframe_cache[i].iframe == iframe) return &s_iframe_cache[i];
    }
    return NULL;
}

static Item wrap_foreign_doc(DomDocument* doc) {
    // Look up cache first.
    for (int i = 0; i < s_foreign_doc_cache_count; i++) {
        if (s_foreign_doc_cache[i].doc == doc) {
            return (Item){.item = s_foreign_doc_cache[i].item};
        }
    }
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_FOREIGN_DOC;
    wrapper->type = (void*)&js_foreign_doc_marker;
    wrapper->data = doc;
    wrapper->data_cap = 0;
    Item it = (Item){.map = wrapper};
    if (s_foreign_doc_cache_count < FOREIGN_DOC_CACHE_SIZE) {
        s_foreign_doc_cache[s_foreign_doc_cache_count].doc = doc;
        s_foreign_doc_cache[s_foreign_doc_cache_count].item = it.item;
        heap_register_gc_root(&s_foreign_doc_cache[s_foreign_doc_cache_count].item);
        s_foreign_doc_cache_count++;
    }
    return it;
}

// Returns the foreign-doc wrapper for `doc` if one exists, else ItemNull.
static Item lookup_foreign_doc_wrapper(DomDocument* doc) {
    for (int i = 0; i < s_foreign_doc_cache_count; i++) {
        if (s_foreign_doc_cache[i].doc == doc) {
            return (Item){.item = s_foreign_doc_cache[i].item};
        }
    }
    return ItemNull;
}

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
            String* tstr = js_dom_create_document_string(fd, title, strlen(title));
            DomText* tnode = dom_text_create_detached(tstr, fd);
            if (tnode) title_dom->append_child(tnode);
        }
    }
    if (html_dom && head_dom) html_dom->append_child(head_dom);
    if (html_dom && body_dom) html_dom->append_child(body_dom);
    fd->root = html_dom;
    return fd;
}

// Public: create a foreign HTML document, return wrapped Item.
extern "C" Item js_create_foreign_html_doc(const char* title) {
    DomDocument* fd = create_foreign_html_doc(title ? title : "");
    if (!fd) return ItemNull;
    return wrap_foreign_doc(fd);
}

// iframe.contentDocument / contentWindow accessors.
// Both currently return the same wrapped foreign HTML document. The foreign
// doc is marked as having a browsing context so its defaultView/getSelection
// resolve normally. js_document_get_property maps "document" / "defaultView"
// back to the same wrapper so identity comparisons hold.
extern "C" Item js_iframe_get_content_document(DomElement* iframe) {
    if (!iframe) return ItemNull;
    IframeContentEntry* e = lookup_iframe_entry(iframe);
    if (!e) {
        DomDocument* doc = create_foreign_html_doc("");
        if (!doc) return ItemNull;
        js_doc_mark_has_browsing_context(doc);
        // If the iframe carries a `srcdoc` attribute, parse its HTML and
        // append the parsed children to the foreign doc's <body>. This is
        // what move-selection-range-into-different-root.tentative.html
        // (and any other iframe-srcdoc test) relies on so getElementById
        // on the contentDocument can resolve nodes.
        const char* srcdoc = dom_element_get_attribute(iframe, "srcdoc");
        if (srcdoc && *srcdoc && doc->root) {
            // Locate the foreign doc's <body> (children of html: head, body).
            DomElement* body = nullptr;
            DomNode* child = doc->root->first_child;
            while (child) {
                if (child->is_element()) {
                    DomElement* el = child->as_element();
                    if (el->tag_name && strcmp(el->tag_name, "body") == 0) {
                        body = el; break;
                    }
                }
                child = child->next_sibling;
            }
            if (body && doc->pool && doc->arena && doc->input) {
                Html5Parser* parser = html5_fragment_parser_create(
                    doc->pool, doc->arena, doc->input);
                if (parser) {
                    html5_fragment_parse(parser, srcdoc);
                    Element* body_elem = html5_fragment_get_body(parser);
                    if (body_elem) {
                        for (int64_t i = 0; i < body_elem->length; i++) {
                            TypeId t = get_type_id(body_elem->items[i]);
                            if (t == LMD_TYPE_ELEMENT) {
                                build_dom_tree_from_element(
                                    body_elem->items[i].element, doc, body);
                            } else if (t == LMD_TYPE_STRING) {
                                String* s = it2s(body_elem->items[i]);
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
                }
            }
        }
        if (s_iframe_cache_count < IFRAME_CACHE_SIZE) {
            s_iframe_cache[s_iframe_cache_count].iframe = iframe;
            s_iframe_cache[s_iframe_cache_count].doc = doc;
            s_iframe_cache_count++;
        }
        return wrap_foreign_doc(doc);
    }
    return wrap_foreign_doc(e->doc);
}
extern "C" Item js_iframe_get_content_window(DomElement* iframe) {
    return js_iframe_get_content_document(iframe);
}

// ----------------------------------------------------------------------------
// Iframe `load` event synthesis. After an <iframe> is inserted into the
// document tree, the HTML spec requires firing a `load` event on it once
// its (possibly blank) document is loaded. WPT tests like Document-open.html
// gate their async work on `iframe.onload`. We schedule a setTimeout(0)
// drain that fires `load` on each pending iframe in insertion order.
// ----------------------------------------------------------------------------
static __thread DomElement* s_pending_iframe_loads[16] = {};
static __thread int s_pending_iframe_load_count = 0;
static __thread bool s_iframe_load_drain_scheduled = false;

static Item _iframe_load_drain(Item this_val, Item* args, int argc) {
    (void)this_val; (void)args; (void)argc;
    int n = s_pending_iframe_load_count;
    s_pending_iframe_load_count = 0;
    s_iframe_load_drain_scheduled = false;
    for (int i = 0; i < n; i++) {
        DomElement* ifr = s_pending_iframe_loads[i];
        s_pending_iframe_loads[i] = nullptr;
        if (!ifr) continue;
        Item ev = js_create_event("load", /*bubbles=*/false, /*cancelable=*/false);
        js_dom_dispatch_event(js_dom_wrap_element(ifr), ev);
    }
    return ItemNull;
}

static void _schedule_iframe_load(DomElement* iframe) {
    if (!iframe) return;
    for (int i = 0; i < s_pending_iframe_load_count; i++) {
        if (s_pending_iframe_loads[i] == iframe) return;
    }
    if (s_pending_iframe_load_count >= 16) return;
    s_pending_iframe_loads[s_pending_iframe_load_count++] = iframe;
    if (!s_iframe_load_drain_scheduled) {
        s_iframe_load_drain_scheduled = true;
        Item cb = js_new_function((void*)_iframe_load_drain, 0);
        js_setTimeout(cb, (Item){.item = i2it(0)});
    }
}

static DomElement* js_dom_find_iframe_by_name(DomNode* node, const char* target_name) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->tag_name && strcasecmp(elem->tag_name, "iframe") == 0) {
                const char* name = dom_element_get_attribute(elem, "name");
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
        if (w) dom_element_set_attribute(dom_elem, "width", w);
    }
    if (argc >= 2) {
        const char* h = fn_to_cstr(height_arg);
        if (h) dom_element_set_attribute(dom_elem, "height", h);
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

// Public: create a DocumentType stub. We model it as a plain DOM element with
// tag "!DOCTYPE" so that node-style operations (parent/sibling) still work.
extern "C" Item js_create_doctype_node(const char* name,
                                        const char* public_id,
                                        const char* system_id) {
    DomDocument* doc = _js_current_document;
    if (!doc) return ItemNull;
    MarkBuilder builder(doc->input);
    Item item = builder.element("!DOCTYPE").final();
    Element* e = item.element;
    DomElement* dt = dom_element_create(doc, "!DOCTYPE", e);
    if (!dt) return ItemNull;
    Item wrapped = js_dom_wrap_element(dt);
    // Attach name/publicId/systemId as DOM attributes for property access.
    if (name)      js_dom_set_property(wrapped, (Item){.item = s2it(heap_create_name("name"))},      (Item){.item = s2it(heap_create_name(name))});
    if (public_id) js_dom_set_property(wrapped, (Item){.item = s2it(heap_create_name("publicId"))},  (Item){.item = s2it(heap_create_name(public_id))});
    if (system_id) js_dom_set_property(wrapped, (Item){.item = s2it(heap_create_name("systemId"))},  (Item){.item = s2it(heap_create_name(system_id))});
    return wrapped;
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

extern "C" Item js_get_dom_implementation(void) {
    if (js_dom_implementation_item.item != ITEM_NULL) {
        return js_dom_implementation_item;
    }
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_PLAIN;
    wrapper->type = (void*)&js_dom_implementation_marker;
    wrapper->data = nullptr;
    wrapper->data_cap = 0;
    js_dom_implementation_item = (Item){.map = wrapper};
    heap_register_gc_root(&js_dom_implementation_item.item);
    return js_dom_implementation_item;
}

// Dispatch a method call on the document.implementation singleton.
// Returns true and sets *out if the method was handled.
extern "C" bool js_dom_implementation_method(Item method_name, Item* args, int argc, Item* out) {
    const char* m = fn_to_cstr(method_name);
    if (!m) return false;
    if (strcmp(m, "createHTMLDocument") == 0) {
        const char* title = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        if (!title) title = "";
        *out = js_create_foreign_html_doc(title);
        return true;
    }
    if (strcmp(m, "createDocument") == 0) {
        // (namespaceURI, qualifiedName, doctype)
        const char* qname = (argc >= 2) ? fn_to_cstr(args[1]) : nullptr;
        *out = js_create_foreign_xml_doc(qname);
        return true;
    }
    if (strcmp(m, "createDocumentType") == 0) {
        const char* name      = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        const char* public_id = (argc >= 2) ? fn_to_cstr(args[1]) : "";
        const char* system_id = (argc >= 3) ? fn_to_cstr(args[2]) : "";
        *out = js_create_doctype_node(name ? name : "", public_id ? public_id : "", system_id ? system_id : "");
        return true;
    }
    if (strcmp(m, "hasFeature") == 0) {
        // Per WHATWG: always returns true.
        *out = (Item){.item = ITEM_TRUE};
        return true;
    }
    return false;
}

// ============================================================================
// Computed Style Wrapping
// ============================================================================

static bool js_is_computed_style(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_computed_style_marker;
}

extern "C" bool js_is_computed_style_item(Item item) {
    return js_is_computed_style(item);
}

extern "C" Item js_dom_get_style_property(Item elem_item, Item prop_name);
extern "C" Item js_dom_set_style_property(Item elem_item, Item prop_name, Item value);

static bool js_is_inline_style(Item item) {
    TypeId tid = get_type_id(item);
    if (tid != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m->type == (void*)&js_inline_style_marker;
}

static DomElement* js_inline_style_owner(Item item) {
    if (!js_is_inline_style(item)) return nullptr;
    return (DomElement*)item.map->data;
}

static Item js_dom_get_inline_style_wrapper(DomElement* elem) {
    if (!elem) return ItemNull;
    Item exp_map = expando_get_or_create_map((DomNode*)elem);
    if (exp_map.item != ITEM_NULL) {
        Item key = (Item){.item = s2it(heap_create_name("__styleWrapper"))};
        Item cached = js_property_get(exp_map, key);
        if (js_is_inline_style(cached)) return cached;
    }

    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_DOM;
    wrapper->type = (void*)&js_inline_style_marker;
    wrapper->data = elem;
    wrapper->data_cap = 0;

    Item wrapped = (Item){.map = wrapper};
    if (exp_map.item != ITEM_NULL) {
        Item key = (Item){.item = s2it(heap_create_name("__styleWrapper"))};
        js_property_set(exp_map, key, wrapped);
    }
    return wrapped;
}

// Helper: serialize a CssValue to a string Item
static Item css_value_to_string_item(CssValue* val) {
    if (!val) return (Item){.item = s2it(heap_create_name(""))};

    switch (val->type) {
        case CSS_VALUE_TYPE_KEYWORD: {
            // resolve named color keywords to rgb() for getComputedStyle
            uint8_t r, g, b, a;
            if (css_named_color_to_rgba(val->data.keyword, &r, &g, &b, &a)) {
                char buf[64];
                if (a < 255) {
                    snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %g)", r, g, b, a / 255.0);
                } else {
                    snprintf(buf, sizeof(buf), "rgb(%d, %d, %d)", r, g, b);
                }
                return (Item){.item = s2it(heap_create_name(buf))};
            }
            const CssEnumInfo* info = css_enum_info(val->data.keyword);
            if (info && info->name) {
                return (Item){.item = s2it(heap_create_name(info->name))};
            }
            break;
        }
        case CSS_VALUE_TYPE_LENGTH: {
            char buf[64];
            const char* unit_str = "px";
            switch (val->data.length.unit) {
                case CSS_UNIT_PX: unit_str = "px"; break;
                case CSS_UNIT_EM: unit_str = "em"; break;
                case CSS_UNIT_REM: unit_str = "rem"; break;
                case CSS_UNIT_PERCENT: unit_str = "%"; break;
                case CSS_UNIT_VW: unit_str = "vw"; break;
                case CSS_UNIT_VH: unit_str = "vh"; break;
                case CSS_UNIT_CM: unit_str = "cm"; break;
                case CSS_UNIT_MM: unit_str = "mm"; break;
                case CSS_UNIT_IN: unit_str = "in"; break;
                case CSS_UNIT_PT: unit_str = "pt"; break;
                case CSS_UNIT_PC: unit_str = "pc"; break;
                default: unit_str = "px"; break;
            }
            double v = val->data.length.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%s", (int)v, unit_str);
            } else {
                snprintf(buf, sizeof(buf), "%g%s", v, unit_str);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_PERCENTAGE: {
            char buf[64];
            double v = val->data.percentage.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%%", (int)v);
            } else {
                snprintf(buf, sizeof(buf), "%g%%", v);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_NUMBER: {
            char buf[64];
            if (val->data.number.is_integer) {
                snprintf(buf, sizeof(buf), "%d", (int)val->data.number.value);
            } else {
                snprintf(buf, sizeof(buf), "%g", val->data.number.value);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_STRING:
            if (val->data.string) {
                // for getComputedStyle, string values are returned with quotes
                char buf[256];
                snprintf(buf, sizeof(buf), "\"%s\"", val->data.string);
                return (Item){.item = s2it(heap_create_name(buf))};
            }
            break;
        case CSS_VALUE_TYPE_COLOR: {
            char buf[64];
            snprintf(buf, sizeof(buf), "rgb(%d, %d, %d)",
                     val->data.color.data.rgba.r,
                     val->data.color.data.rgba.g,
                     val->data.color.data.rgba.b);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        default:
            break;
    }
    return (Item){.item = s2it(heap_create_name(""))};
}

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

    // create a computed style wrapper
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->map_kind = MAP_KIND_DOM;
    wrapper->type = (void*)&js_computed_style_marker;
    wrapper->data = node->as_element();     // store DomElement*
    wrapper->data_cap = pseudo_type;        // store pseudo type

    log_debug("js_get_computed_style: created wrapper for <%s> pseudo=%d",
              node->as_element()->tag_name ? node->as_element()->tag_name : "?", pseudo_type);

    return (Item){.map = wrapper};
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

    Map* wrapper = style_item.map;
    DomElement* elem = (DomElement*)wrapper->data;
    int pseudo_type = (int)wrapper->data_cap;

    if (!elem) return (Item){.item = s2it(heap_create_name(""))};

    const char* js_prop = fn_to_cstr(prop_name);
    if (!js_prop) return (Item){.item = s2it(heap_create_name(""))};

    // handle getPropertyValue method separately
    if (strcmp(js_prop, "getPropertyValue") == 0) {
        // return a function-like marker — handled by method dispatch
        return ItemNull;
    }

    // convert camelCase JS property to CSS hyphenated property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    // look up the CSS property ID
    CssPropertyId prop_id = css_property_id_from_name(css_prop);
    if (prop_id == CSS_PROPERTY_UNKNOWN || prop_id == 0) {
        // check for CSS custom properties (--foo)
        // note: css_property_get_id_by_name returns 0 for not-found, CSS_PROPERTY_UNKNOWN is -1
        if (css_prop[0] == '-' && css_prop[1] == '-') {
            // on-demand matching for custom property
            CssDeclaration* decl = js_match_custom_property(elem, css_prop);
            if (decl && (decl->value || decl->value_text)) {
                const char* val = nullptr;

                // prefer raw source text (preserves comments, blocks) unless it
                // contains a backslash (which needs escape resolution via parsed value)
                bool use_raw = decl->value_text && decl->value_text_len > 0
                    && !memchr(decl->value_text, '\\', decl->value_text_len);
                if (use_raw) {
                    val = decl->value_text;
                } else if (decl->value) {
                    Pool* pool = elem->doc ? elem->doc->pool : nullptr;
                    if (!pool) return (Item){.item = s2it(heap_create_name(""))};
                    CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
                    if (!fmt) return (Item){.item = s2it(heap_create_name(""))};
                    css_format_value(fmt, decl->value);
                    String* result = stringbuf_to_string(fmt->output);
                    val = result ? result->chars : "";
                } else if (decl->value_text && decl->value_text_len > 0) {
                    val = decl->value_text;
                }
                if (!val) val = "";
                // trim leading/trailing whitespace per CSS spec
                while (*val == ' ' || *val == '\t' || *val == '\n' || *val == '\r') val++;
                size_t vlen = strlen(val);
                while (vlen > 0 && (val[vlen-1] == ' ' || val[vlen-1] == '\t' || val[vlen-1] == '\n' || val[vlen-1] == '\r')) vlen--;
                Pool* pool = elem->doc ? elem->doc->pool : nullptr;
                if (pool) {
                    char* trimmed = (char*)pool_alloc(pool, vlen + 1);
                    if (trimmed) { memcpy(trimmed, val, vlen); trimmed[vlen] = '\0'; val = trimmed; }
                }

                // resolve var() references in the value
                if (val && strstr(val, "var(")) {
                    const char* resolved = js_resolve_custom_property_value(elem, val, pool, 0);
                    if (resolved) val = resolved;
                }

                return (Item){.item = s2it(heap_create_name(val))};
            }
            return (Item){.item = s2it(heap_create_name(""))};
        }
        log_debug("js_computed_style_get_property: unknown CSS property '%s' (from JS '%s')",
                  css_prop, js_prop);
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // get the specified (cascaded) value for this property
    CssDeclaration* decl = nullptr;
    if (pseudo_type == 1) {
        decl = dom_element_get_pseudo_element_value(elem, prop_id, 1); // ::before
    } else if (pseudo_type == 2) {
        decl = dom_element_get_pseudo_element_value(elem, prop_id, 2); // ::after
    } else {
        decl = dom_element_get_specified_value(elem, prop_id);
    }

    if (!decl || !decl->value) {
        // return CSS initial/default value for the property
        // CSS spec: 'content' initial value is 'normal'
        //   - on regular elements: computed value is 'normal'
        //   - on ::before/::after pseudo-elements: 'normal' computes to 'none'
        if (prop_id == CSS_PROPERTY_CONTENT) {
            if (pseudo_type == 1 || pseudo_type == 2) {
                return (Item){.item = s2it(heap_create_name("none"))};
            }
            return (Item){.item = s2it(heap_create_name("normal"))};
        }

        // if cascade hasn't happened yet, try on-demand matching
        if (!decl) {
            decl = js_match_element_property(elem, prop_id, pseudo_type);
        }

        if (!decl || !decl->value) {
            // return CSS initial values for common properties
            switch (prop_id) {
                case CSS_PROPERTY_VISIBILITY:
                    return (Item){.item = s2it(heap_create_name("visible"))};
                case CSS_PROPERTY_DISPLAY:
                    return (Item){.item = s2it(heap_create_name("inline"))};
                case CSS_PROPERTY_POSITION:
                    return (Item){.item = s2it(heap_create_name("static"))};
                case CSS_PROPERTY_FLOAT:
                    return (Item){.item = s2it(heap_create_name("none"))};
                case CSS_PROPERTY_OVERFLOW:
                case CSS_PROPERTY_OVERFLOW_X:
                case CSS_PROPERTY_OVERFLOW_Y:
                    return (Item){.item = s2it(heap_create_name("visible"))};
                default:
                    return (Item){.item = s2it(heap_create_name(""))};
            }
        }
    }

    // CSS spec: for ::before/::after, content 'normal' computes to 'none'
    if (prop_id == CSS_PROPERTY_CONTENT && (pseudo_type == 1 || pseudo_type == 2)) {
        if (decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            decl->value->data.keyword == CSS_VALUE_NORMAL) {
            return (Item){.item = s2it(heap_create_name("none"))};
        }
    }

    return css_value_to_string_item(decl->value);
}

// ============================================================================
// On-demand CSS selector matching for getComputedStyle
// ============================================================================

// check if a shorthand property covers the requested longhand
static bool css_shorthand_covers_longhand(CssPropertyId shorthand_id, CssPropertyId longhand_id) {
    switch (shorthand_id) {
        case CSS_PROPERTY_BACKGROUND:
            return longhand_id == CSS_PROPERTY_BACKGROUND_COLOR ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_IMAGE ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_POSITION ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_SIZE ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_REPEAT ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_ATTACHMENT ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_ORIGIN ||
                   longhand_id == CSS_PROPERTY_BACKGROUND_CLIP;
        default:
            return false;
    }
}

/**
 * Match an element against all parsed stylesheets to find a specific property.
 * Used when CSS cascade hasn't happened yet (JS runs before cascade).
 *
 * This performs a mini-cascade for a single element + property:
 * iterates all stylesheet rules, matches selectors, and returns the
 * declaration with highest specificity for the requested property.
 */
static CssDeclaration* js_match_element_property(DomElement* elem, CssPropertyId prop_id, int pseudo_type) {
    if (!elem || !elem->doc) return nullptr;

    DomDocument* doc = elem->doc;
    if (!doc->stylesheets || doc->stylesheet_count <= 0) {
        log_debug("js_match_element_property: no stylesheets available");
        return nullptr;
    }

    Pool* pool = doc->pool;
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) return nullptr;

    CssDeclaration* best_decl = nullptr;
    CssSpecificity best_spec = {0, 0, 0, 0, false};

    // map pseudo_type (0=none, 1=before, 2=after) to PseudoElementType
    PseudoElementType target_pseudo = PSEUDO_ELEMENT_NONE;
    if (pseudo_type == 1) target_pseudo = PSEUDO_ELEMENT_BEFORE;
    else if (pseudo_type == 2) target_pseudo = PSEUDO_ELEMENT_AFTER;

    for (int s = 0; s < doc->stylesheet_count; s++) {
        CssStylesheet* sheet = doc->stylesheets[s];
        if (!sheet) continue;

        for (size_t r = 0; r < sheet->rule_count; r++) {
            CssRule* rule = sheet->rules[r];
            if (!rule || rule->type != CSS_RULE_STYLE) continue;
            if (rule->data.style_rule.declaration_count == 0) continue;

            // try matching selector(s) against element
            bool matched = false;
            CssSpecificity match_spec = {0, 0, 0, 0, false};
            PseudoElementType matched_pseudo = PSEUDO_ELEMENT_NONE;

            // handle selector group (comma-separated)
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
                        matched_pseudo = result.pseudo_element;
                        break;
                    }
                }
            } else if (single_sel) {
                MatchResult result;
                if (selector_matcher_matches(matcher, single_sel, elem, &result)) {
                    matched = true;
                    match_spec = result.specificity;
                    matched_pseudo = result.pseudo_element;
                }
            }

            if (!matched) continue;

            // check pseudo-element type matches what we're looking for
            if (matched_pseudo != target_pseudo) continue;

            // find the requested property in this rule's declarations
            for (size_t d = 0; d < rule->data.style_rule.declaration_count; d++) {
                CssDeclaration* decl = rule->data.style_rule.declarations[d];
                if (!decl) continue;
                if (decl->property_id != prop_id &&
                    !css_shorthand_covers_longhand(decl->property_id, prop_id)) continue;

                // compare specificity — take highest
                if (!best_decl || css_specificity_compare(match_spec, best_spec) >= 0) {
                    best_decl = decl;
                    best_spec = match_spec;
                }
            }
        }
    }

    if (best_decl) {
        log_debug("js_match_element_property: found property %d for <%s> pseudo=%d via on-demand matching",
                  prop_id, elem->tag_name ? elem->tag_name : "?", pseudo_type);
    }

    return best_decl;
}

/**
 * On-demand matching for CSS custom properties (--variable-name).
 * Matches element against all stylesheets and returns the best-matching
 * declaration for the given custom property name.
 */
static CssDeclaration* js_match_custom_property(DomElement* elem, const char* prop_name) {
    if (!elem || !elem->doc || !prop_name) return nullptr;

    DomDocument* doc = elem->doc;
    Pool* pool = doc->pool;

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
        SelectorMatcher* matcher = selector_matcher_create(pool);
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

static DomElement* dom_find_by_id(DomElement* root, const char* id) {
    if (!root || !id) return nullptr;
    if (root->id && strcmp(root->id, id) == 0) return root;

    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* found = dom_find_by_id(child->as_element(), id);
            if (found) return found;
        }
        child = child->next_sibling;
    }
    return nullptr;
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

static bool js_dom_autocapitalize_inherits_from_form(DomElement* elem) {
    return _is_tag(elem, "button") || _is_tag(elem, "fieldset") ||
        _is_tag(elem, "input") || _is_tag(elem, "output") ||
        _is_tag(elem, "select") || _is_tag(elem, "textarea");
}

static DomElement* js_dom_form_owner(DomElement* elem) {
    if (!elem) return nullptr;
    const char* form_id = dom_element_get_attribute(elem, "form");
    if (form_id && *form_id) {
        DomDocument* doc = elem->doc ? elem->doc : _js_current_document;
        DomElement* root = doc ? doc->root : nullptr;
        return dom_find_by_id(root, form_id);
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
    const char* own = dom_element_get_attribute(elem, "autocapitalize");
    if (own) return js_dom_autocapitalize_state(own, true);
    if (js_dom_autocapitalize_inherits_from_form(elem)) {
        DomElement* form = js_dom_form_owner(elem);
        if (form) {
            const char* form_value = dom_element_get_attribute(form, "autocapitalize");
            if (form_value && *form_value) return js_dom_autocapitalize_state(form_value, false);
        }
    }
    return "";
}

static bool js_dom_autocorrect_attr_state(const char* value) {
    return !(value && strcasecmp(value, "off") == 0);
}

static bool js_dom_autocorrect_disabled_by_input_type(DomElement* elem) {
    if (!_is_tag(elem, "input")) return false;
    const char* type = dom_element_get_attribute(elem, "type");
    if (!type) return false;
    return strcasecmp(type, "password") == 0 ||
        strcasecmp(type, "email") == 0 ||
        strcasecmp(type, "url") == 0;
}

static bool js_dom_get_autocorrect(DomElement* elem) {
    if (!elem) return true;
    if (js_dom_autocorrect_disabled_by_input_type(elem)) return false;
    const char* own = dom_element_get_attribute(elem, "autocorrect");
    if (own || dom_element_has_attribute(elem, "autocorrect"))
        return js_dom_autocorrect_attr_state(own ? own : "");
    if (js_dom_autocapitalize_inherits_from_form(elem)) {
        DomElement* form = js_dom_form_owner(elem);
        if (form) {
            const char* form_value = dom_element_get_attribute(form, "autocorrect");
            if (form_value || dom_element_has_attribute(form, "autocorrect"))
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
            const char* value = dom_element_get_attribute(e, "spellcheck");
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
            if (dom_element_has_attribute(e, "writingsuggestions")) {
                return js_dom_writing_suggestions_attr_state(
                    dom_element_get_attribute(e, "writingsuggestions"));
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

static bool js_dom_has_valid_int_attr(DomElement* elem, const char* attr, long* out) {
    if (!elem || !attr) return false;
    const char* value = dom_element_get_attribute(elem, attr);
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
    if (dom_element_has_attribute(elem, "disabled") &&
        (_is_tag(elem, "button") || _is_tag(elem, "input") ||
         _is_tag(elem, "select") || _is_tag(elem, "textarea") ||
         _is_tag(elem, "fieldset") || _is_tag(elem, "option") ||
         _is_tag(elem, "optgroup"))) {
        return true;
    }
    DomNode* p = elem->parent;
    while (p) {
        if (p->is_element()) {
            DomElement* pe = p->as_element();
            if (_is_tag(pe, "fieldset") && dom_element_has_attribute(pe, "disabled")) {
                DomNode* first = pe->first_child;
                while (first && !first->is_element()) first = first->next_sibling;
                if (first && first->as_element() && _is_tag(first->as_element(), "legend") &&
                    js_dom_node_contains(first, (DomNode*)elem)) {
                    p = p->parent;
                    continue;
                }
                return true;
            }
        }
        p = p->parent;
    }
    return false;
}

static bool js_dom_is_editing_host(DomElement* elem) {
    if (!elem || !dom_element_has_attribute(elem, "contenteditable")) return false;
    const char* state = js_dom_normalize_contenteditable(
        dom_element_get_attribute(elem, "contenteditable"));
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
    if (dom_element_has_attribute(elem, "hidden")) return false;
    if (js_dom_is_disabled_for_focus(elem)) return false;

    long tabindex = 0;
    if (js_dom_has_valid_int_attr(elem, "tabindex", &tabindex)) return true;
    if (_is_tag(elem, "input")) {
        const char* type = dom_element_get_attribute(elem, "type");
        return !type || strcasecmp(type, "hidden") != 0;
    }
    if (_is_tag(elem, "button") || _is_tag(elem, "select") ||
        _is_tag(elem, "textarea") || _is_tag(elem, "iframe") ||
        _is_tag(elem, "area")) {
        return true;
    }
    if (_is_tag(elem, "a")) return dom_element_has_attribute(elem, "href");
    if (js_dom_is_first_summary_child(elem)) return true;
    if (js_dom_is_editing_host(elem)) return true;
    return false;
}

static bool js_dom_style_preserves_leading_ws(DomElement* elem, bool inherited) {
    const char* style = elem ? dom_element_get_attribute(elem, "style") : nullptr;
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
        if (dom_element_has_attribute(child_elem, "contenteditable")) {
            const char* ce = js_dom_normalize_contenteditable(
                dom_element_get_attribute(child_elem, "contenteditable"));
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

static void js_dom_focus_set_selection_for_element(DocState* state, DomElement* elem) {
    if (!state || !elem) return;
    const char* exc = nullptr;
    if (tc_is_text_control_elem(elem) && elem->parent) {
        uint32_t index = dom_node_child_index((DomNode*)elem);
        if (index != UINT32_MAX) {
            DomBoundary boundary = { elem->parent, index };
            if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
                log_debug("js_dom_focus_selection_text_control_failed: %s",
                          exc ? exc : "unknown");
            }
        }
        return;
    }

    if (js_dom_is_editing_host(elem)) {
        DomSelection* existing = state->dom_selection;
        if (existing && existing->range_count > 0 && existing->ranges[0]) {
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
    if (!node || !node->is_element()) return;
    DomElement* elem = node->as_element();
    if (!js_dom_is_editing_host(elem)) return;
    if (!js_dom_is_script_focusable(elem)) return;
    DocState* state = js_dom_current_state();
    js_document_active_element = elem;
    focus_set(state, (View*)elem, false);
}

static void js_dom_throw_syntax_error(const char* message) {
    Item name = (Item){.item = s2it(heap_create_name("SyntaxError"))};
    Item msg = (Item){.item = s2it(heap_create_name(message ? message : "SyntaxError"))};
    js_throw_value(js_new_error_with_name(name, msg));
}

// ============================================================================
// Helper: find elements by class name (tree walk, appends to array)
// ============================================================================

static void dom_find_by_class(DomElement* root, const char* cls, Array* arr) {
    if (!root || !cls) return;
    for (int i = 0; i < root->class_count; i++) {
        if (root->class_names[i] && strcmp(root->class_names[i], cls) == 0) {
            array_push(arr, js_dom_wrap_element(root));
            break;
        }
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            dom_find_by_class(child->as_element(), cls, arr);
        }
        child = child->next_sibling;
    }
}

// ============================================================================
// Helper: find elements by tag name (tree walk, appends to array)
// ============================================================================

static void dom_find_by_tag(DomElement* root, const char* tag, Array* arr) {
    if (!root || !tag) return;
    // case-insensitive comparison for HTML tags; "*" matches all elements.
    if (root->tag_name && ((tag[0] == '*' && tag[1] == '\0') ||
            strcasecmp(root->tag_name, tag) == 0)) {
        array_push(arr, js_dom_wrap_element(root));
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            dom_find_by_tag(child->as_element(), tag, arr);
        }
        child = child->next_sibling;
    }
}

static void dom_find_descendants_by_tag(DomElement* root, const char* tag, Array* arr) {
    if (!root || !tag) return;
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            dom_find_by_tag(child->as_element(), tag, arr);
        }
        child = child->next_sibling;
    }
}

static void dom_find_descendants_by_class(DomElement* root, const char* cls, Array* arr) {
    if (!root || !cls) return;
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            dom_find_by_class(child->as_element(), cls, arr);
        }
        child = child->next_sibling;
    }
}

static void dom_find_by_name(DomElement* root, const char* name, Array* arr) {
    if (!root || !name) return;
    const char* attr = dom_element_get_attribute(root, "name");
    if (attr && strcmp(attr, name) == 0) {
        array_push(arr, js_dom_wrap_element(root));
    }
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            dom_find_by_name(child->as_element(), name, arr);
        }
        child = child->next_sibling;
    }
}

// ============================================================================
// Helper: CSS selector parse + match
// ============================================================================

static CssSelector* parse_css_selector(const char* sel_text, Pool* pool) {
    if (!sel_text || !pool) return nullptr;
    size_t sel_len = strlen(sel_text);
    if (sel_len == 0) return nullptr;

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(sel_text, sel_len, pool, &token_count);
    if (!tokens || token_count == 0) return nullptr;

    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
    return selector;
}

// ============================================================================
// Helper: recursive textContent extraction
// ============================================================================

static void collect_text_content(DomNode* node, StrBuf* sb) {
    if (!node) return;

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->text && text->length > 0) {
            strbuf_append_str_n(sb, text->text, (int)text->length);
        }
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        DomNode* child = elem->first_child;
        while (child) {
            collect_text_content(child, sb);
            child = child->next_sibling;
        }
    }
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

static void collect_inner_html(DomNode* node, StrBuf* sb) {
    if (!node) return;

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->text && text->length > 0) {
            strbuf_append_str_n(sb, text->text, (int)text->length);
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
        const char** attr_names = dom_element_get_attribute_names(elem, &attr_count);
        if (attr_names) {
            for (int i = 0; i < attr_count; i++) {
                const char* name = attr_names[i];
                const char* value = dom_element_get_attribute(elem, name);
                if (!name || !value) continue;
                strbuf_append_char(sb, ' ');
                strbuf_append_str(sb, name);
                strbuf_append_str(sb, "=\"");
                collect_html_attr_value(value, sb);
                strbuf_append_char(sb, '"');
            }
        }
        strbuf_append_char(sb, '>');

        // children
        DomNode* child = elem->first_child;
        while (child) {
            collect_inner_html(child, sb);
            child = child->next_sibling;
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

// ============================================================================
// Document Method Dispatcher
// ============================================================================

extern "C" Item js_document_method(Item method_name, Item* args, int argc) {
    const char* method = fn_to_cstr(method_name);
    if (!method) {
        log_error("js_document_method: invalid method name");
        return ItemNull;
    }

    // createEvent is document-independent — handle before document presence check
    // so that JS scripts without an associated HTML document can still construct
    // events.
    if (strcmp(method, "createEvent") == 0) {
        const char* iface = (argc > 0) ? fn_to_cstr(args[0]) : NULL;
        if (!iface) iface = "";
        if (strcmp(iface, "CustomEvent") == 0) {
            return js_create_custom_event_init("", false, false, false, ItemNull);
        }
        return js_create_event_init("", false, false, false);
    }

    if (!_js_current_document) {
        log_error("js_document_method: no document set");
        return ItemNull;
    }

    DomDocument* doc = _js_current_document;
    DomElement* root = doc->root; // may be null for foreign docs without a root

    log_debug("js_document_method: '%s' with %d args", method, argc);

    // Location methods on document.location/window.location proxy.
    if (strcmp(method, "assign") == 0 || strcmp(method, "replace") == 0) {
        if (argc < 1) return make_js_undefined();
        const char* next_url = fn_to_cstr(args[0]);
        if (next_url && next_url[0]) {
            if (doc->pending_navigation_url) {
                mem_free(doc->pending_navigation_url);
            }
            doc->pending_navigation_url = mem_strdup(next_url, MEM_CAT_DOM);
            log_info("js_location_%s: pending navigation to %s", method, next_url);
        }
        return make_js_undefined();
    }
    if (strcmp(method, "reload") == 0) {
        return make_js_undefined();
    }

    // getElementById(id)
    if (strcmp(method, "getElementById") == 0) {
        if (argc < 1) return ItemNull;
        const char* id = fn_to_cstr(args[0]);
        if (!id) return ItemNull;
        DomElement* found = dom_find_by_id(root, id);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // getElementsByClassName(className)
    if (strcmp(method, "getElementsByClassName") == 0) {
        if (argc < 1) return ItemNull;
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return ItemNull;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        dom_find_by_class(root, cls, arr);
        return (Item){.array = arr};
    }

    // getElementsByTagName(tagName)
    if (strcmp(method, "getElementsByTagName") == 0) {
        if (argc < 1) return ItemNull;
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) return ItemNull;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        dom_find_by_tag(root, tag, arr);
        return (Item){.array = arr};
    }

    // getElementsByName(name)
    if (strcmp(method, "getElementsByName") == 0) {
        if (argc < 1) return ItemNull;
        const char* name = fn_to_cstr(args[0]);
        if (!name) return ItemNull;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        dom_find_by_name(root, name, arr);
        return (Item){.array = arr};
    }

    // querySelector(selector)
    if (strcmp(method, "querySelector") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text) return ItemNull;

        Pool* pool = doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) {
            // per DOM spec, throw SyntaxError for invalid selectors
            Item err_name = (Item){.item = s2it(heap_create_name("SyntaxError"))};
            Item err_msg = (Item){.item = s2it(heap_create_name("is not a valid selector"))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
            return ItemNull;
        }

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement* found = selector_matcher_find_first(matcher, selector, root);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // querySelectorAll(selector)
    if (strcmp(method, "querySelectorAll") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text) return ItemNull;

        Pool* pool = doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) {
            // return empty array
            Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
            arr->type_id = LMD_TYPE_ARRAY;
            arr->items = nullptr;
            arr->length = 0;
            arr->capacity = 0;
            return (Item){.array = arr};
        }

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement** results = nullptr;
        int count = 0;
        selector_matcher_find_all(matcher, selector, root, &results, &count);

        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        for (int i = 0; i < count; i++) {
            array_push(arr, js_dom_wrap_element(results[i]));
        }
        return (Item){.array = arr};
    }

    // createElement(tagName)
    if (strcmp(method, "createElement") == 0) {
        if (argc < 1) return ItemNull;
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) return ItemNull;

        // use MarkBuilder to create a proper Lambda Element
        MarkBuilder builder(doc->input);
        Item elem_item = builder.element(tag).final();

        // build DomElement wrapper
        Element* elem = elem_item.element;
        DomElement* dom_elem = dom_element_create(doc, tag, elem);
        return js_dom_wrap_element(dom_elem);
    }

    // createElementNS(namespace, tagName) — treat same as createElement, ignoring namespace
    if (strcmp(method, "createElementNS") == 0) {
        if (argc < 2) return ItemNull;
        // args[0] = namespace URI (ignored), args[1] = qualified tag name
        const char* tag = fn_to_cstr(args[1]);
        if (!tag) return ItemNull;

        MarkBuilder builder(doc->input);
        Item elem_item = builder.element(tag).final();

        Element* elem = elem_item.element;
        DomElement* dom_elem = dom_element_create(doc, tag, elem);
        return js_dom_wrap_element(dom_elem);
    }

    // createTextNode(text)
    if (strcmp(method, "createTextNode") == 0) {
        if (argc < 1) return ItemNull;
        const char* text = fn_to_cstr(args[0]);
        if (!text) return ItemNull;

        // create a detached String-backed DomText node (no parent yet)
        String* str = js_dom_create_document_string(doc, text, strlen(text));
        DomText* text_node = dom_text_create_detached(str, doc);
        if (!text_node) {
            log_error("js_document_method: createTextNode failed for '%s'", text);
            return ItemNull;
        }
        return js_dom_wrap_element(text_node);
    }

    // normalize() — delegate to root element's normalize
    if (strcmp(method, "normalize") == 0) {
        if (doc->root) {
            Item root_item = js_dom_wrap_element(doc->root);
            js_dom_element_method(root_item, (Item){.item = s2it(heap_create_name("normalize"))}, nullptr, 0);
        }
        return ItemNull;
    }

    // document.write(text) / document.writeln(text)
    // Appends the text content to the document body as a text node.
    // Note: real document.write parses HTML and inserts at the current parse point,
    // but for CSS2.1 tests this simplified approach works since tests only write
    // simple text ("PASS", "FAIL", CSS property values, etc.)
    if (strcmp(method, "write") == 0 || strcmp(method, "writeln") == 0) {
        if (argc < 1) return ItemNull;
        const char* text = fn_to_cstr(args[0]);
        if (!text) return ItemNull;

        // find <body> element
        DomElement* body = nullptr;
        DomNode* child = doc->root->first_child;
        while (child) {
            if (child->is_element()) {
                DomElement* e = child->as_element();
                if (e->tag_name && strcmp(e->tag_name, "body") == 0) {
                    body = e;
                    break;
                }
            }
            child = child->next_sibling;
        }
        if (!body) {
            log_debug("js_document_method: write - no body element found");
            return ItemNull;
        }

        // create a text node and append to body
        String* str = js_dom_create_document_string(doc, text, strlen(text));
        DomText* text_node = dom_text_create_detached(str, doc);
        if (text_node) {
            ((DomNode*)body)->append_child((DomNode*)text_node);
            dom_post_insert((DomNode*)body, (DomNode*)text_node);
            log_debug("js_document_method: write '%s' appended to body", text);
        }

        return ItemNull;
    }

    // v12b: createDocumentFragment()
    if (strcmp(method, "createDocumentFragment") == 0) {
        // create a lightweight container element with a special tag
        MarkBuilder builder(doc->input);
        Item frag_item = builder.element("#document-fragment").final();
        Element* frag_elem = frag_item.element;
        DomElement* dom_frag = dom_element_create(doc, "#document-fragment", frag_elem);
        return js_dom_wrap_element(dom_frag);
    }

    // v12b: createComment(data)
    if (strcmp(method, "createComment") == 0) {
        const char* text = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        if (!text) text = "";

        // create a Lambda Element with comment tag for backing
        MarkBuilder builder(doc->input);
        Item comment_item = builder.element("!--").text(text).final();
        Element* comment_elem = comment_item.element;

        DomComment* comment_node = dom_comment_create_detached(comment_elem, doc);
        if (!comment_node) return ItemNull;
        return js_dom_wrap_element(comment_node);
    }

    // createProcessingInstruction(target, data) — model as a comment-like node
    // tagged "?target" so node ops still work. Used heavily in WPT XML tests.
    if (strcmp(method, "createProcessingInstruction") == 0) {
        const char* target = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        const char* data   = (argc >= 2) ? fn_to_cstr(args[1]) : "";
        if (!target) target = "";
        if (!data) data = "";
        MarkBuilder builder(doc->input);
        Item pi_item = builder.element("?").text(data).final();
        Element* pi_elem = pi_item.element;
        DomElement* pi_node = dom_element_create(doc, target, pi_elem);
        if (!pi_node) return ItemNull;
        return js_dom_wrap_element(pi_node);
    }

    // v12b: importNode(node [, deep]) — deep clone a node
    if (strcmp(method, "importNode") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* source = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!source || !source->is_element()) return ItemNull;
        bool deep = (argc >= 2) ? js_is_truthy(args[1]) : false;
        Item source_item = js_dom_wrap_element(source);
        Item deep_arg = (Item){.item = b2it(deep ? 1 : 0)};
        Item method_str = (Item){.item = s2it(heap_create_name("cloneNode"))};
        return js_dom_element_method(source_item, method_str, &deep_arg, 1);
    }

    // v12b: adoptNode(node) — detach from current parent
    if (strcmp(method, "adoptNode") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* node = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!node) return ItemNull;
        if (node->parent) {
            dom_pre_remove(node);
            node->parent->remove_child(node);
        }
        return args[0];
    }

    // EventTarget interface on document
    if (strcmp(method, "addEventListener") == 0) {
        if (argc >= 2) {
            js_dom_add_event_listener(js_get_document_object_value(), args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "removeEventListener") == 0) {
        if (argc >= 2) {
            js_dom_remove_event_listener(js_get_document_object_value(), args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "dispatchEvent") == 0) {
        if (argc >= 1) {
            return js_dom_dispatch_event(js_get_document_object_value(), args[0]);
        }
        return (Item){.item = ITEM_FALSE};
    }

    // document.execCommand(cmd, [showUI], [value]) — legacy editing API.
    // Native EC commands run through the same rich editing transaction
    // envelope as synthetic key input. Clipboard commands still delegate to a
    // JS-side helper installed by the WPT shim so page clipboard handlers can
    // populate the synthetic clipboard store.
    if (strcmp(method, "execCommand") == 0) {
        const char* cmd = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        if (js_dom_exec_command_is_native(cmd)) {
            return js_dom_exec_command_native(args, argc);
        }
        Item helper_key = (Item){.item = s2it(heap_create_name("__lambda_execCommand_handler"))};
        Item helper = js_get_global_property(helper_key);
        TypeId htype = get_type_id(helper);
        if (htype == LMD_TYPE_FUNC) {
            // The WPT clipboard shim installs this helper to fire
            // copy/cut/paste event listeners during synthetic gestures —
            // see Radiant_Clipboard_WPT_Status.md. For non-clipboard
            // commands, the helper itself returns false.
            return js_call_function(helper, js_get_document_object_value(), args, argc);
        }
        return (Item){.item = ITEM_FALSE};
    }
    // queryCommand* reports the native EC command surface plus clipboard
    // commands. State/value/indeterm grow with the EC tiers.
    if (strcmp(method, "queryCommandSupported") == 0) {
        const char* cmd = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        bool supported = js_dom_exec_command_is_supported(cmd);
        return (Item){.item = b2it(supported)};
    }
    if (strcmp(method, "queryCommandEnabled") == 0) {
        const char* cmd = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        bool enabled = false;
        if (cmd && js_dom_exec_command_is_native(cmd)) {
            enabled = js_document_design_mode ||
                js_dom_exec_command_has_rich_target();
        } else if (cmd && (strcasecmp(cmd, "copy") == 0 ||
                          strcasecmp(cmd, "cut") == 0 ||
                          strcasecmp(cmd, "paste") == 0)) {
            enabled = true;
        }
        return (Item){.item = b2it(enabled)};
    }
    if (strcmp(method, "queryCommandIndeterm") == 0 ||
        strcmp(method, "queryCommandState") == 0) {
        const char* cmd = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        if (strcmp(method, "queryCommandState") == 0 &&
            js_dom_exec_command_is_inline_format(cmd)) {
            return (Item){.item = b2it(js_dom_exec_command_query_inline_state(cmd))};
        }
        return (Item){.item = ITEM_FALSE};
    }
    if (strcmp(method, "queryCommandValue") == 0) {
        const char* cmd = (argc >= 1) ? fn_to_cstr(args[0]) : "";
        if (cmd && strcasecmp(cmd, "formatBlock") == 0) {
            const char* value = js_dom_exec_command_query_format_block_value();
            return (Item){.item = s2it(heap_create_name(value))};
        }
        return (Item){.item = s2it(heap_create_name(""))};
    }
    // document.contains(node) — true iff node is document or a descendant.
    if (strcmp(method, "contains") == 0) {
        if (argc >= 1) {
            Item arg = args[0];
            // self → true (per DOM spec, node.contains(node) is true)
            Item doc_self = js_get_document_object_value();
            if (arg.item == doc_self.item) return (Item){.item = ITEM_TRUE};
            // synthesized doctype (and any other direct child of the document
            // stub other than the documentElement) — true.
            DomDocument* doc = _js_current_document;
            if (doc) {
                void* stub_v = js_dom_get_or_create_doc_node(doc);
                DomElement* stub = (DomElement*)stub_v;
                void* nv = js_dom_unwrap_element(arg);
                if (stub && nv) {
                    DomNode* n = (DomNode*)nv;
                    DomNode* cur = n;
                    while (cur) {
                        if (cur == (DomNode*)stub) return (Item){.item = ITEM_TRUE};
                        cur = cur->parent;
                    }
                }
            }
            Item doc_elem = js_document_get_property((Item){.item = s2it(heap_create_name("documentElement"))});
            if (doc_elem.item != ITEM_NULL && doc_elem.item != ITEM_JS_UNDEFINED) {
                // also: documentElement itself is a descendant, contains(documentElement) → true
                if (arg.item == doc_elem.item) return (Item){.item = ITEM_TRUE};
                return js_dom_contains(doc_elem, arg);
            }
        }
        return (Item){.item = ITEM_TRUE};
    }
    // document.compareDocumentPosition(node) — return CONTAINS|FOLLOWING (20) for all nodes
    if (strcmp(method, "compareDocumentPosition") == 0) {
        return (Item){.item = i2it(20)};
    }
    // document.createDocumentFragment() — return a dummy element
    if (strcmp(method, "createDocumentFragment") == 0) {
        return ItemNull;
    }

    // Selection / Range API
    if (strcmp(method, "createRange") == 0) {
        return js_dom_create_range();
    }
    if (strcmp(method, "getSelection") == 0) {
        // Per HTML spec: document.getSelection() returns null when defaultView
        // is null (i.e., document has no associated browsing context).
        // Main doc and iframe content docs have a browsing context; foreign
        // docs from document.implementation.create*Document do not.
        if (!js_doc_has_browsing_context(_js_current_document)) {
            return ItemNull;
        }
        return js_dom_get_selection();
    }

    // document.appendChild(node) — for documents without a root, set the
    // node as the root; otherwise append to the existing root. (WPT
    // setupRangeTests appends elements/comments to the foreign XML doc.)
    if (strcmp(method, "appendChild") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* child = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!child) return ItemNull;
        if (!doc->root && child->is_element()) {
            doc->root = child->as_element();
        } else if (doc->root) {
            ((DomNode*)doc->root)->append_child(child);
            dom_post_insert((DomNode*)doc->root, child);
        }
        return args[0];
    }

    log_debug("js_document_method: unknown method '%s'", method);
    return ItemNull;
}

// ============================================================================
// Document Property Access
// ============================================================================

extern "C" Item js_document_get_property(Item prop_name) {
    if (!_js_current_document) {
        log_debug("js_document_get_property: no document set");
        return ItemNull;
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    DomDocument* doc = _js_current_document;
    DomElement* root = doc->root;  // may be NULL for foreign docs created via createDocument

    // documentElement — the root <html> element
    if (strcmp(prop, "documentElement") == 0) {
        return root ? js_dom_wrap_element(root) : ItemNull;
    }

    // body — the <body> element
    if (strcmp(prop, "body") == 0) {
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
    if (strcmp(prop, "head") == 0) {
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
    if (strcmp(prop, "title") == 0) {
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
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // v12: URL — full document URL as string
    if (strcmp(prop, "URL") == 0) {
        Url* url = doc->url;
        if (url) {
            const char* href = url_get_href(url);
            if (href) return (Item){.item = s2it(heap_create_name(href))};
        }
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // location-style URL access. We model document.location and bare
    // location as aliases of the document/window proxy itself.
    if (strcmp(prop, "location") == 0 || strcmp(prop, "document") == 0) {
        return doc_to_proxy_item(doc);
    }
    if (strcmp(prop, "href") == 0) {
        const char* href = (doc->url && url_get_href(doc->url)) ? url_get_href(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(href))};
    }
    if (strcmp(prop, "protocol") == 0) {
        const char* protocol = (doc->url && url_get_protocol(doc->url)) ? url_get_protocol(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(protocol))};
    }
    if (strcmp(prop, "hostname") == 0) {
        const char* hostname = (doc->url && url_get_hostname(doc->url)) ? url_get_hostname(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(hostname))};
    }
    if (strcmp(prop, "port") == 0) {
        const char* port = (doc->url && url_get_port(doc->url)) ? url_get_port(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(port))};
    }
    if (strcmp(prop, "pathname") == 0) {
        const char* pathname = (doc->url && url_get_pathname(doc->url)) ? url_get_pathname(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(pathname))};
    }
    if (strcmp(prop, "search") == 0) {
        const char* search = (doc->url && url_get_search(doc->url)) ? url_get_search(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(search))};
    }
    if (strcmp(prop, "hash") == 0) {
        const char* hash = (doc->url && url_get_hash(doc->url)) ? url_get_hash(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(hash))};
    }
    if (strcmp(prop, "host") == 0) {
        const char* host = (doc->url && url_get_host(doc->url)) ? url_get_host(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(host))};
    }
    if (strcmp(prop, "origin") == 0) {
        const char* origin = (doc->url && url_get_origin(doc->url)) ? url_get_origin(doc->url) : "";
        return (Item){.item = s2it(heap_create_name(origin))};
    }

    // readyState — legacy defaults to "complete"; the Phase 4 post-DOM
    // script scheduler updates this during modeled lifecycle transitions.
    if (strcmp(prop, "readyState") == 0) {
        const char* ready_state = doc->js_ready_state ? doc->js_ready_state : "complete";
        return (Item){.item = s2it(heap_create_name(ready_state))};
    }

    if (strcmp(prop, "fonts") == 0) {
        if (js_document_fonts_value.item == ITEM_NULL) {
            js_document_fonts_value = js_create_document_fonts_object();
        }
        return js_document_fonts_value;
    }

    // compatMode
    if (strcmp(prop, "compatMode") == 0) {
        return (Item){.item = s2it(heap_create_name("CSS1Compat"))};
    }

    // F-1: document.forms — array of all <form> elements in the document.
    if (strcmp(prop, "forms") == 0) {
        Item arr = js_array_new(0);
        DomDocument* doc = _js_current_document;
        if (doc && doc->root) {
            std::function<void(DomNode*)> walk = [&](DomNode* n) {
                while (n) {
                    if (n->is_element()) {
                        DomElement* ce = (DomElement*)n;
                        if (ce->tag_name && strcasecmp(ce->tag_name, "form") == 0) {
                            Item fitem = js_dom_wrap_element(ce);
                            js_array_push(arr, fitem);
                            // Named access: forms[name] / forms[id] per HTML
                            // §HTMLCollection-namedItem semantics.
                            const char* nm = dom_element_get_attribute(ce, "name");
                            if (nm && *nm) {
                                js_property_set(arr,
                                    (Item){.item = s2it(heap_create_name(nm))},
                                    fitem);
                            }
                            const char* id = dom_element_get_attribute(ce, "id");
                            if (id && *id && (!nm || strcmp(id, nm) != 0)) {
                                js_property_set(arr,
                                    (Item){.item = s2it(heap_create_name(id))},
                                    fitem);
                            }
                        }
                        walk(ce->first_child);
                    }
                    n = n->next_sibling;
                }
            };
            walk((DomNode*)doc->root);
        }
        return arr;
    }

    // characterSet / charset
    if (strcmp(prop, "characterSet") == 0 || strcmp(prop, "charset") == 0) {
        return (Item){.item = s2it(heap_create_name("UTF-8"))};
    }

    // contentType
    if (strcmp(prop, "contentType") == 0) {
        return (Item){.item = s2it(heap_create_name("text/html"))};
    }

    // nodeType — DOCUMENT_NODE = 9
    if (strcmp(prop, "nodeType") == 0) {
        return (Item){.item = i2it(9)};
    }

    // childNodes — return a NodeList-like Array of the document's children
    // (synthesized doctype + documentElement). Backed by the document stub
    // so iteration works.
    if (strcmp(prop, "childNodes") == 0) {
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
    if (strcmp(prop, "nodeName") == 0) {
        return (Item){.item = s2it(heap_create_name("#document"))};
    }

    // styleSheets — collection of parsed CSSStyleSheet objects
    if (strcmp(prop, "styleSheets") == 0) {
        return js_cssom_get_document_stylesheets();
    }

    // ownerDocument — the document itself has no owner (returns null)
    if (strcmp(prop, "ownerDocument") == 0) {
        return ItemNull;
    }

    // defaultView — returns window (the global object)
    // Sizzle accesses document.defaultView for getComputedStyle.
    // Foreign documents (created via document.implementation.create*Document)
    // never have a browsing context, so defaultView must be null per HTML spec.
    if (strcmp(prop, "defaultView") == 0) {
        if (!js_doc_has_browsing_context(_js_current_document)) {
            return ItemNull;
        }
        // For iframe content docs (foreign with browsing context), the
        // "window" is modeled as the same wrapper object — so identity
        // checks like `iframe.contentDocument.defaultView === iframe.contentWindow`
        // hold (both resolve to wrap_foreign_doc(doc)).
        if (_js_current_document != _js_main_document) {
            Item w = lookup_foreign_doc_wrapper(_js_current_document);
            return w.item ? w : ItemNull;
        }
        // Return stored window object if set by preamble (document.defaultView = window)
        if (js_document_default_view.item != ITEM_NULL) {
            return js_document_default_view;
        }
        return ItemNull;
    }

    // For iframe content docs, expose Window-like properties on the same
    // wrapper so that contentWindow.X works (since contentWindow ===
    // contentDocument here). Also handle on the main doc proxy so existing
    // window-style access through `document` continues to function.
    if (_js_current_document != _js_main_document &&
        js_doc_has_browsing_context(_js_current_document)) {
        if (strcmp(prop, "document") == 0) {
            Item w = lookup_foreign_doc_wrapper(_js_current_document);
            return w.item ? w : ItemNull;
        }
        if (strcmp(prop, "Selection") == 0 || strcmp(prop, "Range") == 0) {
            Item global_ctor = js_get_global_property(prop_name);
            if (get_type_id(global_ctor) == LMD_TYPE_FUNC) return global_ctor;
            // Class stub: a Map with __class_name__ so `instanceof` works.
            // js_instanceof_classname fast-paths on the name to call our
            // js_dom_item_is_selection / js_dom_item_is_range checks.
            extern Item js_new_object();
            extern Item js_property_set(Item obj, Item key, Item value);
            Item ctor = js_new_object();
            String* cn = heap_create_name(prop);
            Item key = (Item){.item = s2it(heap_create_name("__class_name__"))};
            js_property_set(ctor, key, (Item){.item = s2it(cn)});
            return ctor;
        }
    }

    // implementation — DOMImplementation (createHTMLDocument, createDocument, ...)
    if (strcmp(prop, "implementation") == 0) {
        return js_get_dom_implementation();
    }

    // doctype — DocumentType node (or null if document has none).
    // We synthesize a DOCTYPE node as the first child of the document stub
    // (see js_dom_get_or_create_doc_node). Expose it here so JS code that
    // does `document.doctype` (and Range/Selection APIs that take it as a
    // node argument) work.
    if (strcmp(prop, "doctype") == 0) {
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

    // EventTarget methods must be callable even when MIR takes the generic
    // property-call fallback instead of the document method dispatcher.
    if (strcmp(prop, "addEventListener") == 0)
        return js_new_function((void*)js_eventtarget_add_listener, 3);
    if (strcmp(prop, "removeEventListener") == 0)
        return js_new_function((void*)js_eventtarget_remove_listener, 3);
    if (strcmp(prop, "dispatchEvent") == 0)
        return js_new_function((void*)js_eventtarget_dispatch, 1);

    // Document method names accessed as properties return ITEM_TRUE for feature detection
    static const char* doc_methods[] = {
        "getElementById", "getElementsByTagName", "getElementsByClassName",
        "getElementsByName", "querySelector", "querySelectorAll",
        "createElement", "createElementNS", "createTextNode", "createComment",
        "createDocumentFragment", "importNode", "adoptNode",
        "addEventListener", "removeEventListener",
        "createRange", "getSelection",
        "createEvent",
        // CE-6 (§9): legacy editing APIs we deliberately retain for feature
        // detection only. execCommand returns false; queryCommand* return
        // false/"" per §9 — they never mutate and never dispatch.
        "execCommand",
        "queryCommandSupported", "queryCommandEnabled",
        "queryCommandIndeterm", "queryCommandState", "queryCommandValue",
        NULL
    };
    for (int i = 0; doc_methods[i]; i++) {
        if (strcmp(prop, doc_methods[i]) == 0) {
            return (Item){.item = ITEM_TRUE};
        }
    }

    // designMode is the legacy whole-document edit toggle. This first cut
    // exposes the IDL state; editing-host default actions still land in the
    // command engine phases.
    if (strcmp(prop, "designMode") == 0) {
        return (Item){.item = s2it(heap_create_name(js_document_design_mode ? "on" : "off"))};
    }

    // activeElement — currently focused element, or <body> as default per spec.
    if (strcmp(prop, "activeElement") == 0) {
        DocState* state = js_dom_current_state();
        View* focused = focus_get(state);
        if (focused && focused->is_element()) {
            return js_dom_wrap_element(((DomNode*)focused)->as_element());
        }
        DomElement* active_element = tc_get_active_element(state);
        if (active_element && active_element->doc == doc) return js_dom_wrap_element(active_element);
        if (js_document_active_element &&
            js_document_active_element->doc == doc &&
            js_dom_node_is_connected((DomNode*)js_document_active_element)) {
            return js_dom_wrap_element(js_document_active_element);
        }
        // default to <body> if available
        DomNode* child = root ? root->first_child : nullptr;
        while (child) {
            if (child->is_element()) {
                DomElement* e = child->as_element();
                if (e->tag_name && strcasecmp(e->tag_name, "body") == 0)
                    return js_dom_wrap_element(e);
            }
            child = child->next_sibling;
        }
        return root ? js_dom_wrap_element(root) : ItemNull;
    }

    DomDocument* expando_doc = _js_current_document ? _js_current_document : _js_main_document;
    void* stub_v = js_dom_get_or_create_doc_node(expando_doc);
    if (stub_v) {
        Item exp_map = expando_get_map((DomNode*)stub_v);
        if (exp_map.item != ITEM_NULL) {
            if (expando_map_has_key(exp_map, prop_name)) {
                return js_property_get(exp_map, prop_name);
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

static Item js_text_control_set_selection_range(Item start_arg, Item end_arg, Item dir_arg) {
    Item self = js_get_this();
    DomElement* elem = (DomElement*)js_dom_unwrap_element(self);
    if (!elem || !tc_is_text_control_elem(elem)) return make_js_undefined();

    int64_t s = it2i(start_arg);
    int64_t e = it2i(end_arg);
    if (s < 0) s = 0;
    if (e < 0) e = 0;
    uint8_t dir = text_control_direction_from_item(dir_arg);
    form_control_set_selection(js_dom_current_state(), (View*)elem, (uint32_t)s, (uint32_t)e, dir);
    return make_js_undefined();
}

static Item js_text_control_select(void) {
    Item self = js_get_this();
    DomElement* elem = (DomElement*)js_dom_unwrap_element(self);
    if (!elem || !tc_is_text_control_elem(elem)) return make_js_undefined();

    tc_ensure_init(elem);
    FormControlProp* f = elem->form;
    if (js_dom_is_script_focusable(elem)) {
        focus_set(js_dom_current_state(), (View*)elem, false);
    }
    form_control_set_selection(js_dom_current_state(), (View*)elem, 0, f->current_value_u16_len, 0);
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
        js_dom_throw_index_size_error("The start offset is larger than the end offset.");
        return make_js_undefined();
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
    _value_mark_dirty(elem);
    form_control_set_selection(js_dom_current_state(), (View*)elem,
        final_start, final_end, final_direction);
    return make_js_undefined();
}

static Item js_text_control_set_range_text(Item replacement_arg, Item start_arg,
                                           Item end_arg, Item mode_arg) {
    Item self = js_get_this();
    DomElement* elem = (DomElement*)js_dom_unwrap_element(self);
    return js_text_control_set_range_text_for_elem(elem, replacement_arg,
        start_arg, end_arg, mode_arg);
}

static Item js_dom_text_replace_data_method(DomText* text_node, Item offset_arg,
                                            Item count_arg, Item data_arg) {
    if (!text_node) return make_js_undefined();
    int64_t offset = js_dom_to_integer_or_zero(offset_arg);
    int64_t count = js_dom_to_integer_or_zero(count_arg);
    if (offset < 0 || count < 0) {
        js_dom_throw_index_size_error("The offset or count is negative.");
        return make_js_undefined();
    }

    Item data_text_item = js_to_string(data_arg);
    const char* data_text = fn_to_cstr(data_text_item);
    if (!data_text) data_text = "";
    js_dom_replace_text_data(text_node, (uint32_t)offset, (uint32_t)count, data_text);
    return make_js_undefined();
}

static Item js_dom_text_insert_data_method(DomText* text_node, Item offset_arg,
                                           Item data_arg) {
    return js_dom_text_replace_data_method(text_node, offset_arg, (Item){.item = i2it(0)}, data_arg);
}

static Item js_dom_text_append_data_method(DomText* text_node, Item data_arg) {
    if (!text_node) return make_js_undefined();
    return js_dom_text_replace_data_method(text_node,
        (Item){.item = i2it((int64_t)dom_text_utf16_length(text_node))},
        (Item){.item = i2it(0)}, data_arg);
}

static Item js_dom_text_delete_data_method(DomText* text_node, Item offset_arg,
                                           Item count_arg) {
    return js_dom_text_replace_data_method(text_node, offset_arg, count_arg,
        (Item){.item = s2it(heap_create_name(""))});
}

static Item js_dom_text_substring_data_method(DomText* text_node, Item offset_arg,
                                              Item count_arg) {
    if (!text_node) return (Item){.item = s2it(heap_create_name(""))};
    int64_t offset = js_dom_to_integer_or_zero(offset_arg);
    int64_t count = js_dom_to_integer_or_zero(count_arg);
    if (offset < 0 || count < 0) {
        js_dom_throw_index_size_error("The offset or count is negative.");
        return make_js_undefined();
    }
    uint32_t old_u16_len = dom_text_utf16_length(text_node);
    if ((uint64_t)offset > old_u16_len) {
        js_dom_throw_index_size_error("The offset is larger than the CharacterData length.");
        return make_js_undefined();
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

static Item js_text_replace_data_method(Item offset_arg, Item count_arg, Item data_arg) {
    Item self = js_get_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(self);
    if (!node || !node->is_text()) return make_js_undefined();
    return js_dom_text_replace_data_method(node->as_text(), offset_arg, count_arg, data_arg);
}

static Item js_text_insert_data_method(Item offset_arg, Item data_arg) {
    Item self = js_get_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(self);
    if (!node || !node->is_text()) return make_js_undefined();
    return js_dom_text_insert_data_method(node->as_text(), offset_arg, data_arg);
}

static Item js_text_append_data_method(Item data_arg) {
    Item self = js_get_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(self);
    if (!node || !node->is_text()) return make_js_undefined();
    return js_dom_text_append_data_method(node->as_text(), data_arg);
}

static Item js_text_delete_data_method(Item offset_arg, Item count_arg) {
    Item self = js_get_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(self);
    if (!node || !node->is_text()) return make_js_undefined();
    return js_dom_text_delete_data_method(node->as_text(), offset_arg, count_arg);
}

static Item js_text_substring_data_method(Item offset_arg, Item count_arg) {
    Item self = js_get_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(self);
    if (!node || !node->is_text()) return make_js_undefined();
    return js_dom_text_substring_data_method(node->as_text(), offset_arg, count_arg);
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
    // barred if disabled
    if (dom_element_has_attribute(elem, "disabled")) return true;
    // barred if descendant of a disabled <fieldset>, except when descendant
    // of that fieldset's first <legend> child.
    {
        DomNode* prev = (DomNode*)elem;
        for (DomNode* p = elem->parent; p; prev = p, p = p->parent) {
            if (!p->is_element()) continue;
            DomElement* pe = p->as_element();
            if (!pe->tag_name) continue;
            if (strcasecmp(pe->tag_name, "fieldset") == 0 &&
                dom_element_has_attribute(pe, "disabled")) {
                // Find first legend child of pe.
                DomElement* first_legend = nullptr;
                for (DomNode* c = pe->first_child; c; c = c->next_sibling) {
                    if (c->is_element()) {
                        DomElement* ce = c->as_element();
                        if (ce->tag_name && strcasecmp(ce->tag_name, "legend") == 0) {
                            first_legend = ce; break;
                        }
                    }
                }
                // If `prev` (the child of fieldset on path to elem) is the
                // first legend, the element is NOT barred by this fieldset.
                if (!first_legend || prev != (DomNode*)first_legend) return true;
            }
        }
    }
    // barred if readonly
    if (dom_element_has_attribute(elem, "readonly")) return true;
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
        const char* btype = dom_element_get_attribute(elem, "type");
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
        if (tc_is_text_control(elem)) {
            tc_ensure_init(elem);
            return (elem->form && elem->form->current_value) ? elem->form->current_value : "";
        }
        const char* v = dom_element_get_attribute(elem, "value");
        return v ? v : "";
    }
    if (strcasecmp(tag, "textarea") == 0) {
        tc_ensure_init(elem);
        return (elem->form && elem->form->current_value) ? elem->form->current_value : "";
    }
    return "";
}

// Build and return a ValidityState plain JS object for the given element.

// Parse "HH:MM[:SS[.mmm]]" to milliseconds since midnight (-1 on error)
static long long _time_to_ms(const char* t) {
    if (!t || !*t) return -1;
    int h = 0, m = 0, s = 0;
    int parsed = sscanf(t, "%d:%d:%d", &h, &m, &s);
    if (parsed < 2) return -1;
    return (long long)h * 3600000 + m * 60000 + s * 1000;
}

// Parse "YYYY-MM-DD" to days since 1970-01-01 (-9999999 on error)
static long long _date_to_days(int year, int mon, int day) {
    int y = year - 1;
    long long days = 365LL * y + y/4 - y/100 + y/400;
    static const int dm[] = {0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    days += dm[mon];
    if (mon > 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days++;
    days += day - 1;
    return days - 719162LL;  // subtract days before 1970-01-01
}

static long long _parse_date(const char* s) {
    if (!s || !*s) return -(1LL<<62);
    int year = 0, mon = 0, day = 0;
    if (sscanf(s, "%d-%d-%d", &year, &mon, &day) != 3) return -(1LL<<62);
    return _date_to_days(year, mon, day);
}

// Parse "YYYY-MM" to months since 1970-01 (0-based)
static long long _parse_month(const char* s) {
    if (!s || !*s) return -(1LL<<62);
    int year = 0, mon = 0;
    if (sscanf(s, "%d-%d", &year, &mon) != 2) return -(1LL<<62);
    return (long long)(year - 1970) * 12 + (mon - 1);
}

// Parse "YYYY-Www" to a monotonically increasing week index
static long long _parse_week(const char* s) {
    if (!s || !*s) return -(1LL<<62);
    int year = 0, week = 0;
    if (sscanf(s, "%d-W%d", &year, &week) != 2) return -(1LL<<62);
    return (long long)(year - 1970) * 53 + (week - 1);
}

// Parse "YYYY-MM-DDTHH:MM[:SS]" to ms since epoch
static long long _parse_datetime(const char* s) {
    if (!s || !*s) return -(1LL<<62);
    int year = 0, mon = 0, day = 0, h = 0, m = 0, sec = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &h, &m, &sec) < 5) return -(1LL<<62);
    long long days = _date_to_days(year, mon, day);
    return days * 86400000LL + (long long)h * 3600000 + m * 60000 + sec * 1000;
}

// For a <select required> element, returns true iff no <option> is
// "selected" with a non-empty value (i.e. the placeholder option requires
// user to explicitly pick a non-empty one).
static bool _get_selectedness(DomElement* opt);
static void _set_selectedness(DomElement* opt, bool v);

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

static void _collect_options(DomNode* node, Item arr) {
    _collect_options_impl(node, arr, true);
}

// Get the option's text content (concatenated descendant text, trimmed
// of leading/trailing ASCII whitespace, with internal whitespace
// collapsed per HTML spec for <option> label).
static char* _option_text(DomElement* opt) {
    StrBuf* sb = strbuf_new_cap(32);
    collect_text_content((DomNode*)opt, sb);
    // collapse whitespace
    StrBuf* out = strbuf_new_cap(32);
    bool prev_ws = true;
    if (sb->str) {
        for (size_t i = 0; i < sb->length; i++) {
            char c = sb->str[i];
            bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f');
            if (ws) {
                if (!prev_ws) strbuf_append_char(out, ' ');
                prev_ws = true;
            } else {
                strbuf_append_char(out, c);
                prev_ws = false;
            }
        }
    }
    // trim trailing
    while (out->length > 0 && out->str[out->length - 1] == ' ') {
        out->str[--out->length] = '\0';
    }
    char* result = mem_strdup(out->str ? out->str : "", MEM_CAT_JS_RUNTIME);
    strbuf_free(sb);
    strbuf_free(out);
    return result;
}

// Get/set the option's selectedness. Stored as expando "__selected".
// Default falls back to the `selected` content attribute (defaultSelected).
static bool _get_selectedness(DomElement* opt) {
    if (!opt) return false;
    Item exp = expando_get_map((DomNode*)opt);
    if (exp.item != ITEM_NULL) {
        Item key = (Item){.item = s2it(heap_create_name("__selected"))};
        Item v = js_property_get(exp, key);
        if (v.item != ITEM_NULL && !is_js_undefined(v)) return js_is_truthy(v);
    }
    return dom_element_has_attribute(opt, "selected");
}

// Returns true if the select's selectedness has been explicitly modified
// (via selectedIndex/value/option.selected setter), so default-reset
// behavior should NOT be applied.
static bool _select_is_dirty(DomElement* sel) {
    if (!sel) return false;
    Item exp = expando_get_map((DomNode*)sel);
    if (exp.item == ITEM_NULL) return false;
    Item v = js_property_get(exp,
        (Item){.item = s2it(heap_create_name("__selDirty"))});
    return v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
}

static void _select_mark_dirty(DomElement* sel) {
    if (!sel) return;
    Item exp = expando_get_or_create_map((DomNode*)sel);
    if (exp.item == ITEM_NULL) return;
    js_property_set(exp,
        (Item){.item = s2it(heap_create_name("__selDirty"))},
        (Item){.item = b2it(true)});
}

// Text-control dirty value flag (input/textarea), tracked via expando so we
// can distinguish "API has been called to set value" from "value reflects
// defaultValue". Cleared by form reset.
static bool _value_is_dirty(DomElement* elem) {
    if (!elem) return false;
    Item exp = expando_get_map((DomNode*)elem);
    if (exp.item == ITEM_NULL) return false;
    Item v = js_property_get(exp,
        (Item){.item = s2it(heap_create_name("__valueDirty"))});
    return v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
}
static void _value_mark_dirty(DomElement* elem) {
    if (!elem) return;
    Item exp = expando_get_or_create_map((DomNode*)elem);
    if (exp.item == ITEM_NULL) return;
    js_property_set(exp,
        (Item){.item = s2it(heap_create_name("__valueDirty"))},
        (Item){.item = b2it(true)});
}
static void _value_clear_dirty(DomElement* elem) {
    if (!elem) return;
    Item exp = expando_get_map((DomNode*)elem);
    if (exp.item == ITEM_NULL) return;
    js_property_set(exp,
        (Item){.item = s2it(heap_create_name("__valueDirty"))},
        (Item){.item = ITEM_NULL});
}

static void _set_selectedness(DomElement* opt, bool v) {
    if (!opt) return;
    Item exp = expando_get_or_create_map((DomNode*)opt);
    if (exp.item == ITEM_NULL) return;
    Item key = (Item){.item = s2it(heap_create_name("__selected"))};
    js_property_set(exp, key, (Item){.item = b2it(v)});
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
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
        if (!opt) continue;
        _set_selectedness(opt, (int)i == idx); // INT_CAST_OK: option index
    }
    _select_mark_dirty(sel);
}

static void _select_select_only_option(DomElement* sel, DomElement* selected_opt) {
    if (!sel || !selected_opt) return;
    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    int64_t n = js_array_length(arr);
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
        if (opt) _set_selectedness(opt, opt == selected_opt);
    }
}

static void _select_normalize_for_selected_options(DomElement* sel, Item options) {
    if (!sel || get_type_id(options) != LMD_TYPE_ARRAY) return;
    if (dom_element_has_attribute(sel, "multiple")) return;
    int64_t n = js_array_length(options);
    int selected_count = 0;
    int last_selected = -1;
    int first_non_disabled = -1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(options, i));
        if (!opt) continue;
        if (_get_selectedness(opt)) {
            selected_count++;
            last_selected = (int)i; // INT_CAST_OK: option index
        }
        if (first_non_disabled < 0 && !dom_element_has_attribute(opt, "disabled")) {
            first_non_disabled = (int)i; // INT_CAST_OK: option index
        }
    }
    int size = 0;
    const char* sz = dom_element_get_attribute(sel, "size");
    if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
    int chosen = -1;
    if (selected_count > 1) chosen = last_selected;
    else if (selected_count == 0 && size <= 1 && !_select_is_dirty(sel)) chosen = first_non_disabled;
    if (chosen < 0) return;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(options, i));
        if (!opt) continue;
        _set_selectedness(opt, (int)i == chosen); // INT_CAST_OK: option index
    }
}

static void _select_refresh_selected_options_collection(Item collection, DomElement* sel) {
    if (get_type_id(collection) != LMD_TYPE_ARRAY || !sel) return;
    js_property_set(collection, (Item){.item = s2it(heap_create_name("length"))},
                    (Item){.item = i2it(0)});

    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    _select_normalize_for_selected_options(sel, arr);
    int64_t n = js_array_length(arr);
    for (int64_t i = 0; i < n; i++) {
        Item it = js_array_get_int(arr, i);
        DomElement* opt = (DomElement*)js_dom_unwrap_element(it);
        if (opt && _get_selectedness(opt)) js_array_push(collection, it);
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
    Item cache_key = (Item){.item = s2it(heap_create_name("__selectedOptions"))};
    Item out = js_property_get(exp, cache_key);
    if (get_type_id(out) == LMD_TYPE_ARRAY) {
        _select_refresh_selected_options_collection(out, sel);
    }
}

static void _select_refresh_cached_selected_options_for_node(DomNode* node) {
    _select_refresh_cached_selected_options(_nearest_select_for_node(node));
}

extern "C" void js_dom_collection_before_property_get(Item object, Item key) {
    if (get_type_id(object) != LMD_TYPE_ARRAY) return;
    int kind = 0;
    DomElement* owner = _select_options_owner(object, &kind);
    if (!owner || kind != SELECT_COLLECTION_SELECTED_OPTIONS) return;
    TypeId kt = get_type_id(key);
    if (kt == LMD_TYPE_INT || kt == LMD_TYPE_INT64 || kt == LMD_TYPE_FLOAT) {
        _select_refresh_selected_options_collection(object, owner);
        return;
    }
    if (kt != LMD_TYPE_STRING) return;
    String* sk = it2s(key);
    if (!sk) return;
    _select_refresh_selected_options_collection(object, owner);
}

extern "C" void js_dom_options_collection_before_property_set(Item object, Item key, Item value) {
    if (get_type_id(object) != LMD_TYPE_ARRAY || get_type_id(key) != LMD_TYPE_STRING) return;
    String* sk = it2s(key);
    if (!sk || sk->len != 13 || strncmp(sk->chars, "selectedIndex", 13) != 0) return;
    int kind = 0;
    DomElement* owner = _select_options_owner(object, &kind);
    if (!owner || kind != SELECT_COLLECTION_OPTIONS || !_is_tag(owner, "select")) return;
    _select_set_selected_index(owner, _select_index_from_item(value));
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
    const char* v = dom_element_get_attribute(opt, "value");
    if (v) return mem_strdup(v, MEM_CAT_JS_RUNTIME);
    return _option_text(opt);
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
        Item it = js_array_get_int(arr, i);
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
    if (dom_element_has_attribute(sel, "multiple")) return;
    Item arr = js_array_new(0);
    _collect_options(sel->first_child, arr);
    int64_t n = js_array_length(arr);
    if (n == 0) return;
    // Find last selected option.
    int last_selected = -1;
    int first_non_disabled = -1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
        if (!opt) continue;
        if (_get_selectedness(opt)) last_selected = (int)i; // INT_CAST_OK: option index
        if (first_non_disabled < 0 && !dom_element_has_attribute(opt, "disabled")) {
            first_non_disabled = (int)i; // INT_CAST_OK: option index
        }
    }
    int chosen = (last_selected >= 0) ? last_selected :
        (first_non_disabled >= 0 ? first_non_disabled : 0);
    int size = 0;
    {
        const char* sz = dom_element_get_attribute(sel, "size");
        if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
    }
    // For display-size <= 1 (the common dropdown), exactly one option
    // must be selected. For listbox (size > 1) without multiple, zero or
    // one option may be selected — but if more than one has selectedness,
    // only the last remains.
    bool require_one = size <= 1;
    for (int64_t i = 0; i < n; i++) {
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
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
    const char* sz = dom_element_get_attribute(sel, "size");
    if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
    bool is_listbox = dom_element_has_attribute(sel, "multiple") || size > 1;
    // Identify the placeholder option: the first option in the select's
    // option list, only if it is a direct child of the select and has empty
    // value. Options inside an optgroup don't qualify.
    DomElement* placeholder = nullptr;
    if (!is_listbox && n > 0) {
        DomElement* first_opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, 0));
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
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
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
            DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
            if (!opt || dom_element_has_attribute(opt, "disabled")) continue;
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
            bool default_checked = dom_element_has_attribute(elem, "checked");
            _set_checkedness(elem, default_checked);
            // Clear dirty checkedness flag.
            Item exp = expando_get_map((DomNode*)elem);
            if (exp.item != ITEM_NULL) {
                js_property_set(exp,
                    (Item){.item = s2it(heap_create_name("__chkDirty"))},
                    (Item){.item = ITEM_NULL});
            }
            return;
        }
        if (strcmp(itype, "submit") == 0 || strcmp(itype, "reset") == 0 ||
            strcmp(itype, "button") == 0 || strcmp(itype, "image") == 0 ||
            strcmp(itype, "hidden") == 0 || strcmp(itype, "file") == 0) {
            // Buttons/hidden/image/file: skip (file resets selection only).
            if (strcmp(itype, "file") == 0 && tc_is_text_control_elem(elem)) {
                tc_ensure_init(elem);
                tc_set_value(elem, "", 0);
            }
            return;
        }
        // Text-like input: value := defaultValue (= value attribute)
        if (tc_is_text_control_elem(elem)) {
            tc_ensure_init(elem);
            const char* dv = dom_element_get_attribute(elem, "value");
            if (!dv) dv = "";
            tc_set_value(elem, dv, strlen(dv));
            _value_clear_dirty(elem);
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
            _value_clear_dirty(elem);
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
            DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
            if (!opt) continue;
            // Default selectedness = presence of "selected" content attribute
            _set_selectedness(opt, dom_element_has_attribute(opt, "selected"));
            // Clear per-option dirty selectedness flag.
            Item oexp = expando_get_map((DomNode*)opt);
            if (oexp.item != ITEM_NULL) {
                js_property_set(oexp,
                    (Item){.item = s2it(heap_create_name("__optDirty"))},
                    (Item){.item = ITEM_NULL});
            }
        }
        // Clear the dirty flag so default-reset rules apply again.
        Item exp = expando_get_map((DomNode*)elem);
        if (exp.item != ITEM_NULL) {
            Item key = (Item){.item = s2it(heap_create_name("__selDirty"))};
            js_property_set(exp, key, (Item){.item = ITEM_NULL});
        }
        if (!dom_element_has_attribute(elem, "multiple")) {
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
    std::function<void(DomNode*, DomElement*)> walk =
        [&](DomNode* node, DomElement* nearest_form) {
        while (node) {
            if (node->is_element()) {
                DomElement* ce = (DomElement*)node;
                if (ce->tag_name) {
                    bool is_ctrl =
                        strcasecmp(ce->tag_name, "input") == 0 ||
                        strcasecmp(ce->tag_name, "textarea") == 0 ||
                        strcasecmp(ce->tag_name, "select") == 0 ||
                        strcasecmp(ce->tag_name, "output") == 0;
                    if (is_ctrl) {
                        const char* fa = dom_element_get_attribute(ce, "form");
                        DomElement* owner = nullptr;
                        if (fa && *fa) {
                            owner = doc_root ? dom_find_by_id(doc_root, fa) : nullptr;
                        } else {
                            owner = nearest_form;
                        }
                        if (owner == form_elem) _reset_form_control(ce);
                    }
                    bool is_form = strcasecmp(ce->tag_name, "form") == 0;
                    DomElement* new_nearest = is_form ? ce : nearest_form;
                    // Don't change nearest_form for the form_elem itself
                    // when it appears at the top of recursion.
                    walk(ce->first_child, new_nearest);
                }
            }
            node = node->next_sibling;
        }
    };
    // Walk the form's own descendants (handles detached forms too).
    walk(form_elem->first_child, form_elem);
    // For associated controls (via `form` attribute) outside the form
    // subtree, walk the rest of the document — but skip the form_elem
    // subtree to avoid double-resetting.
    if (form_in_doc && doc_root && doc_root != form_elem) {
        std::function<void(DomNode*)> walk_assoc = [&](DomNode* node) {
            while (node) {
                if (node == (DomNode*)form_elem) {
                    node = node->next_sibling; continue;
                }
                if (node->is_element()) {
                    DomElement* ce = (DomElement*)node;
                    if (ce->tag_name) {
                        bool is_ctrl =
                            strcasecmp(ce->tag_name, "input") == 0 ||
                            strcasecmp(ce->tag_name, "textarea") == 0 ||
                            strcasecmp(ce->tag_name, "select") == 0 ||
                            strcasecmp(ce->tag_name, "output") == 0;
                        if (is_ctrl) {
                            const char* fa = dom_element_get_attribute(ce, "form");
                            if (fa && *fa) {
                                DomElement* owner = dom_find_by_id(doc_root, fa);
                                if (owner == form_elem) _reset_form_control(ce);
                            }
                        }
                        walk_assoc(ce->first_child);
                    }
                }
                node = node->next_sibling;
            }
        };
        walk_assoc((DomNode*)doc_root);
    }
}

extern "C" void js_dom_run_form_reset(void* form_elem) {
    _run_form_reset((DomElement*)form_elem);
}

static Item _build_validity_state(DomElement* elem) {
    Item vs = js_new_object();
    // Set Symbol.toStringTag = "ValidityState" so
    // Object.prototype.toString.call(validity) === "[object ValidityState]"
    js_property_set(vs,
        (Item){.item = s2it(heap_create_name("__sym_4"))},
        (Item){.item = s2it(heap_create_name("ValidityState"))});
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

        // For typed input controls (date, time, number, etc.) the value
        // sanitization algorithm replaces an unparseable value with the
        // empty string. WPT tests expect valueMissing=true when the
        // value attribute is an invalid date/time/number string.
        if (!val_empty && strcasecmp(tag, "input") == 0) {
            const char* itype = js_dom_input_type_lower(elem);
            bool sanitized_empty = false;
            if (strcmp(itype, "number") == 0 || strcmp(itype, "range") == 0) {
                char* ep = nullptr;
                strtod(val, &ep);
                if (ep == val || *ep != '\0') sanitized_empty = true;
            } else if (strcmp(itype, "date") == 0) {
                if (_parse_date(val) == -(1LL<<62)) sanitized_empty = true;
                else {
                    // Also validate Y/M/D ranges
                    int y=0,m=0,d=0;
                    if (sscanf(val, "%d-%d-%d", &y,&m,&d) != 3 ||
                        m < 1 || m > 12 || d < 1 || d > 31) sanitized_empty = true;
                }
            } else if (strcmp(itype, "month") == 0) {
                if (_parse_month(val) == -(1LL<<62)) sanitized_empty = true;
                else { int y=0,m=0;
                    if (sscanf(val, "%d-%d", &y,&m) != 2 || m<1 || m>12) sanitized_empty=true;
                }
            } else if (strcmp(itype, "week") == 0) {
                if (_parse_week(val) == -(1LL<<62)) sanitized_empty = true;
                else { int y=0,w=0;
                    if (sscanf(val, "%d-W%d", &y,&w) != 2 || w<1 || w>53) sanitized_empty=true;
                }
            } else if (strcmp(itype, "time") == 0) {
                int h=0, m=0, s=0;
                int n = sscanf(val, "%d:%d:%d", &h, &m, &s);
                if (n < 2 || h<0 || h>23 || m<0 || m>59 || (n==3 && (s<0 || s>59))) sanitized_empty = true;
                // reject extra colon segments like "12:00:00:001"
                if (!sanitized_empty) {
                    int colons = 0;
                    for (const char* p = val; *p; p++) if (*p == ':') colons++;
                    if (colons > 2) sanitized_empty = true;
                }
            } else if (strcmp(itype, "datetime-local") == 0) {
                // Accept either "T" or single space as separator (per spec
                // setter normalises space → T, but raw attribute may have
                // either). However extra spaces (e.g. "  ") are invalid.
                char tmp[64]; size_t vl = strlen(val);
                bool space_sep = false;
                if (vl < sizeof(tmp)) {
                    strcpy(tmp, val);
                    // require exactly one space or T at position 10
                    if (vl >= 11 && tmp[10] == ' ' && tmp[11] != ' ') {
                        tmp[10] = 'T'; space_sep = true;
                    }
                }
                const char* parse_str = space_sep ? tmp : val;
                if (_parse_datetime(parse_str) == -(1LL<<62)) sanitized_empty = true;
                else {
                    int y=0,mo=0,d=0,h=0,mi=0;
                    if (sscanf(parse_str, "%d-%d-%dT%d:%d", &y,&mo,&d,&h,&mi) < 5 ||
                        mo<1||mo>12||d<1||d>31||h<0||h>23||mi<0||mi>59)
                        sanitized_empty = true;
                }
            } else if (strcmp(itype, "color") == 0) {
                // valid color: #RRGGBB
                if (val[0] != '#' || strlen(val) != 7) sanitized_empty = true;
                else for (int i = 1; i < 7; i++)
                    if (!((val[i]>='0'&&val[i]<='9')||(val[i]>='a'&&val[i]<='f')||(val[i]>='A'&&val[i]<='F')))
                    { sanitized_empty = true; break; }
            }
            if (sanitized_empty) val_empty = true;
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
                const char* rname = dom_element_get_attribute(elem, "name");
                bool elem_connected = js_dom_is_connected(elem);
                bool own_required = dom_element_has_attribute(elem, "required");
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
                    std::function<void(DomNode*)> scan = [&](DomNode* n) {
                        while (n) {
                            if (n->is_element()) {
                                DomElement* ce = (DomElement*)n;
                                if (ce->tag_name && strcasecmp(ce->tag_name, "input") == 0) {
                                    const char* tt = js_dom_input_type_lower(ce);
                                    if (strcmp(tt, "radio") == 0) {
                                        const char* nm = dom_element_get_attribute(ce, "name");
                                        if (nm && strcmp(nm, rname) == 0) {
                                            DomElement* ce_form = nullptr;
                                            for (DomNode* pp = ce->parent; pp; pp = pp->parent) {
                                                if (pp->is_element()) {
                                                    DomElement* pe2 = (DomElement*)pp;
                                                    if (pe2->tag_name && strcasecmp(pe2->tag_name, "form") == 0) {
                                                        ce_form = pe2; break;
                                                    }
                                                }
                                            }
                                            if (ce_form == form_scope) {
                                                if (js_dom_get_checkedness(ce)) any_checked = true;
                                                if (dom_element_has_attribute(ce, "required")) any_required = true;
                                            }
                                        }
                                    }
                                    scan(ce->first_child);
                                } else {
                                    scan(ce->first_child);
                                }
                            }
                            n = n->next_sibling;
                        }
                    };
                    if (root) scan(root->first_child);
                    value_missing = any_required && !any_checked;
                }
            }
        }

        if (!radio_handled && dom_element_has_attribute(elem, "required")) {
            if (strcasecmp(tag, "input") == 0) {
                const char* itype = js_dom_input_type_lower(elem);
                if (strcmp(itype, "checkbox") == 0) {
                    value_missing = !js_dom_get_checkedness(elem);
                } else if (strcmp(itype, "file") == 0) {
                    // file input: missing iff no files. We don't support
                    // file uploads in headless, so always missing if required.
                    value_missing = true;
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
            const char* pattern = dom_element_get_attribute(elem, "pattern");
            if (pattern && *pattern) {
                // HTML pattern anchors the whole value (^(?:pattern)$)
                // Build anchored pattern
                size_t plen = strlen(pattern);
                char* full_pattern = (char*)mem_alloc(plen + 8, MEM_CAT_JS_RUNTIME);
                if (full_pattern) {
                    snprintf(full_pattern, plen + 8, "^(?:%s)$", pattern);
                    Item re = js_create_regex(full_pattern, (int)strlen(full_pattern), "", 0);
                    mem_free(full_pattern);
                    Item val_item = (Item){.item = s2it(heap_create_name(val))};
                    Item result = js_regex_test(re, val_item);
                    // pattern mismatch if regex does NOT match
                    pattern_mismatch = !((result.item & 0xFF) != 0 && result.item != ITEM_NULL);
                }
            }
        }

        // rangeOverflow / rangeUnderflow: number input type only (simplified)
        if (!val_empty && strcasecmp(tag, "input") == 0) {
            const char* itype = js_dom_input_type_lower(elem);
            if (strcmp(itype, "number") == 0 || strcmp(itype, "range") == 0) {
                char* end_ptr = nullptr;
                double dval = strtod(val, &end_ptr);
                if (end_ptr != val && *end_ptr == '\0') { // valid number
                    const char* max_attr = dom_element_get_attribute(elem, "max");
                    if (max_attr && *max_attr) {
                        double dmax = strtod(max_attr, &end_ptr);
                        if (end_ptr != max_attr) range_overflow = (dval > dmax);
                    }
                    const char* min_attr = dom_element_get_attribute(elem, "min");
                    if (min_attr && *min_attr) {
                        double dmin = strtod(min_attr, &end_ptr);
                        if (end_ptr != min_attr) range_underflow = (dval < dmin);
                    }
                    // stepMismatch: if step attr set and value doesn't align
                    const char* step_attr = dom_element_get_attribute(elem, "step");
                    if (step_attr && strcmp(step_attr, "any") != 0 && *step_attr) {
                        double dstep = strtod(step_attr, &end_ptr);
                        if (end_ptr != step_attr && dstep > 0) {
                            const char* min_attr2 = dom_element_get_attribute(elem, "min");
                            double base = (min_attr2 && *min_attr2) ? strtod(min_attr2, nullptr) : 0.0;
                            double remainder = fmod(dval - base, dstep);
                            if (remainder < 0) remainder += dstep;
                            step_mismatch = (remainder > 1e-10 && (dstep - remainder) > 1e-10);
                        }
                    }
                }
            } else if (strcmp(itype, "date") == 0) {
                // ISO date strings are lexicographically ordered, so string compare works
                const char* max_a = dom_element_get_attribute(elem, "max");
                if (max_a && *max_a) range_overflow  = (strcmp(val, max_a) > 0);
                const char* min_a = dom_element_get_attribute(elem, "min");
                if (min_a && *min_a) range_underflow = (strcmp(val, min_a) < 0);
                const char* step_a = dom_element_get_attribute(elem, "step");
                if (step_a && *step_a && strcmp(step_a, "any") != 0) {
                    char* ep = nullptr;
                    long long step_days = (long long)strtod(step_a, &ep);
                    if (ep != step_a && step_days > 0) {
                        long long vdays = _parse_date(val);
                        long long base_days = min_a && *min_a ? _parse_date(min_a) : 0LL;
                        if (vdays != -(1LL<<62) && base_days != -(1LL<<62)) {
                            long long rem = (vdays - base_days) % step_days;
                            if (rem < 0) rem += step_days;
                            step_mismatch = (rem != 0);
                        }
                    }
                }
            } else if (strcmp(itype, "month") == 0) {
                const char* max_a = dom_element_get_attribute(elem, "max");
                if (max_a && *max_a) range_overflow  = (strcmp(val, max_a) > 0);
                const char* min_a = dom_element_get_attribute(elem, "min");
                if (min_a && *min_a) range_underflow = (strcmp(val, min_a) < 0);
                const char* step_a = dom_element_get_attribute(elem, "step");
                if (step_a && *step_a && strcmp(step_a, "any") != 0) {
                    char* ep = nullptr;
                    long long step_mon = (long long)strtod(step_a, &ep);
                    if (ep != step_a && step_mon > 0) {
                        long long vmon = _parse_month(val);
                        long long base_mon = min_a && *min_a ? _parse_month(min_a) : 0LL;
                        if (vmon != -(1LL<<62) && base_mon != -(1LL<<62)) {
                            long long rem = (vmon - base_mon) % step_mon;
                            if (rem < 0) rem += step_mon;
                            step_mismatch = (rem != 0);
                        }
                    }
                }
            } else if (strcmp(itype, "week") == 0) {
                const char* max_a = dom_element_get_attribute(elem, "max");
                if (max_a && *max_a) range_overflow  = (strcmp(val, max_a) > 0);
                const char* min_a = dom_element_get_attribute(elem, "min");
                if (min_a && *min_a) range_underflow = (strcmp(val, min_a) < 0);
                const char* step_a = dom_element_get_attribute(elem, "step");
                if (step_a && *step_a && strcmp(step_a, "any") != 0) {
                    char* ep = nullptr;
                    long long step_wk = (long long)strtod(step_a, &ep);
                    if (ep != step_a && step_wk > 0) {
                        long long vwk = _parse_week(val);
                        long long base_wk = min_a && *min_a ? _parse_week(min_a) : 0LL;
                        if (vwk != -(1LL<<62) && base_wk != -(1LL<<62)) {
                            long long rem = (vwk - base_wk) % step_wk;
                            if (rem < 0) rem += step_wk;
                            step_mismatch = (rem != 0);
                        }
                    }
                }
            } else if (strcmp(itype, "time") == 0) {
                // lexicographic compare works for HH:MM:SS format
                const char* max_a = dom_element_get_attribute(elem, "max");
                if (max_a && *max_a) range_overflow  = (strcmp(val, max_a) > 0);
                const char* min_a = dom_element_get_attribute(elem, "min");
                if (min_a && *min_a) range_underflow = (strcmp(val, min_a) < 0);
                const char* step_a = dom_element_get_attribute(elem, "step");
                if (step_a && *step_a && strcmp(step_a, "any") != 0) {
                    char* ep = nullptr;
                    long long step_ms = (long long)strtod(step_a, &ep);
                    if (ep != step_a && step_ms > 0) {
                        long long vms = _time_to_ms(val);
                        long long base_ms = min_a && *min_a ? _time_to_ms(min_a) : 0LL;
                        if (vms >= 0 && base_ms >= 0) {
                            long long rem = (vms - base_ms) % step_ms;
                            if (rem < 0) rem += step_ms;
                            step_mismatch = (rem != 0);
                        }
                    }
                }
            } else if (strcmp(itype, "datetime-local") == 0) {
                const char* max_a = dom_element_get_attribute(elem, "max");
                if (max_a && *max_a) range_overflow  = (strcmp(val, max_a) > 0);
                const char* min_a = dom_element_get_attribute(elem, "min");
                if (min_a && *min_a) range_underflow = (strcmp(val, min_a) < 0);
                const char* step_a = dom_element_get_attribute(elem, "step");
                if (step_a && *step_a && strcmp(step_a, "any") != 0) {
                    char* ep = nullptr;
                    long long step_ms = (long long)strtod(step_a, &ep);
                    if (ep != step_a && step_ms > 0) {
                        long long vms = _parse_datetime(val);
                        long long base_ms = min_a && *min_a ? _parse_datetime(min_a) : 0LL;
                        if (vms != -(1LL<<62) && base_ms != -(1LL<<62)) {
                            long long rem = (vms - base_ms) % step_ms;
                            if (rem < 0) rem += step_ms;
                            step_mismatch = (rem != 0);
                        }
                    }
                }
            }
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
    js_property_set(vs, (Item){.item = s2it(heap_create_name("valueMissing"))},    _b(value_missing));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("typeMismatch"))},    _b(type_mismatch));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("patternMismatch"))}, _b(pattern_mismatch));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("tooLong"))},         _b(too_long));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("tooShort"))},        _b(too_short));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("rangeOverflow"))},   _b(range_overflow));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("rangeUnderflow"))},  _b(range_underflow));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("stepMismatch"))},    _b(step_mismatch));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("badInput"))},        _b(bad_input));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("customError"))},     _b(custom_error));
    js_property_set(vs, (Item){.item = s2it(heap_create_name("valid"))},           _b(valid));
    return vs;
}

// ----------------------------------------------------------------------
// F-0: IDL-name → HTML-attribute-name mapping for reflected attributes.
// Returns nullptr when no mapping exists (caller uses prop name verbatim).
// ----------------------------------------------------------------------
static const char* _idl_to_attr_name(const char* prop) {
    if (!prop) return nullptr;
    switch (prop[0]) {
        case 'r':
            if (strcmp(prop, "readOnly") == 0) return "readonly";
            break;
        case 'n':
            if (strcmp(prop, "noValidate") == 0) return "novalidate";
            break;
        case 'm':
            if (strcmp(prop, "maxLength") == 0) return "maxlength";
            if (strcmp(prop, "minLength") == 0) return "minlength";
            break;
        case 'a':
            if (strcmp(prop, "acceptCharset") == 0) return "accept-charset";
            break;
        case 'd':
            if (strcmp(prop, "defaultChecked") == 0) return "checked";
            if (strcmp(prop, "defaultSelected") == 0) return "selected";
            break;
        case 'h':
            if (strcmp(prop, "htmlFor") == 0) return "for";
            break;
        case 't':
            if (strcmp(prop, "tabIndex") == 0) return "tabindex";
            break;
        case 'i':
            // CE-4 (Radiant_Design_Content_Editable.md §7).
            if (strcmp(prop, "inputMode") == 0) return "inputmode";
            break;
        case 'e':
            // CE-4 (Radiant_Design_Content_Editable.md §7).
            if (strcmp(prop, "enterKeyHint") == 0) return "enterkeyhint";
            break;
        case 'c':
            // CE-1 / CE-4 (Radiant_Design_Content_Editable.md §4.2 + §10).
            if (strcmp(prop, "contentEditable") == 0) return "contenteditable";
            break;
        case 'f':
            if (strcmp(prop, "formAction") == 0) return "formaction";
            if (strcmp(prop, "formMethod") == 0) return "formmethod";
            if (strcmp(prop, "formEnctype") == 0) return "formenctype";
            if (strcmp(prop, "formEncoding") == 0) return "formenctype";
            if (strcmp(prop, "formTarget") == 0) return "formtarget";
            if (strcmp(prop, "formNoValidate") == 0) return "formnovalidate";
            break;
    }
    return nullptr;
}

// True if `prop` reflects a HTML boolean attribute on the given element.
// Per HTML spec, IDL boolean reflection setters do ToBoolean coercion:
// truthy → set attribute (empty value), falsy → remove attribute.
static bool _is_bool_reflected(DomElement* elem, const char* prop) {
    if (!elem || !elem->tag_name) return false;
    const char* tag = elem->tag_name;
    bool input = strcasecmp(tag, "input") == 0;
    bool button = strcasecmp(tag, "button") == 0;
    bool select = strcasecmp(tag, "select") == 0;
    bool textarea = strcasecmp(tag, "textarea") == 0;
    bool form = strcasecmp(tag, "form") == 0;
    bool fieldset = strcasecmp(tag, "fieldset") == 0;
    bool option = strcasecmp(tag, "option") == 0;
    bool optgroup = strcasecmp(tag, "optgroup") == 0;
    if (strcmp(prop, "disabled") == 0)
        return input || button || select || textarea || fieldset || option || optgroup;
    if (strcmp(prop, "required") == 0) return input || select || textarea;
    if (strcmp(prop, "multiple") == 0) return input || select;
    if (strcmp(prop, "readOnly") == 0 || strcmp(prop, "readonly") == 0)
        return input || textarea;
    if (strcmp(prop, "noValidate") == 0) return form;
    if (strcmp(prop, "formNoValidate") == 0) return input || button;
    if (strcmp(prop, "autofocus") == 0) return true;
    if (strcmp(prop, "defaultChecked") == 0) return input;
    if (strcmp(prop, "defaultSelected") == 0) return option;
    return false;
}

// True if `prop` reflects a non-negative-integer-with-default attribute.
// Sets *attr_name to the HTML attribute name and *default_val to the
// IDL default returned when attribute is missing or invalid.
static bool _is_int_reflected(DomElement* elem, const char* prop,
                              const char** attr_name, int* default_val) {
    if (!elem || !elem->tag_name || !attr_name || !default_val) return false;
    const char* tag = elem->tag_name;
    bool input = strcasecmp(tag, "input") == 0;
    bool textarea = strcasecmp(tag, "textarea") == 0;
    bool select = strcasecmp(tag, "select") == 0;
    if (strcmp(prop, "maxLength") == 0 && (input || textarea)) {
        *attr_name = "maxlength"; *default_val = -1; return true;
    }
    if (strcmp(prop, "minLength") == 0 && (input || textarea)) {
        *attr_name = "minlength"; *default_val = 0; return true;
    }
    if (strcmp(prop, "size") == 0 && input) {
        *attr_name = "size"; *default_val = 20; return true;
    }
    if (strcmp(prop, "width") == 0 && input) {
        *attr_name = "width"; *default_val = 0; return true;
    }
    if (strcmp(prop, "height") == 0 && input) {
        *attr_name = "height"; *default_val = 0; return true;
    }
    if (strcmp(prop, "size") == 0 && select) {
        *attr_name = "size"; *default_val = 0; return true;
    }
    if (strcmp(prop, "rows") == 0 && textarea) {
        *attr_name = "rows"; *default_val = 2; return true;
    }
    if (strcmp(prop, "cols") == 0 && textarea) {
        *attr_name = "cols"; *default_val = 20; return true;
    }
    return false;
}

// True if `prop` reflects a string-valued content attribute whose missing
// value is the empty string in the DOM IDL layer.
static bool _is_string_reflected(DomElement* elem, const char* prop,
                                 const char** attr_name) {
    if (!elem || !elem->tag_name || !prop || !attr_name) return false;
    const char* tag = elem->tag_name;
    if (strcmp(prop, "src") == 0) {
        if (strcasecmp(tag, "img") == 0 || strcasecmp(tag, "script") == 0 ||
            strcasecmp(tag, "iframe") == 0 || strcasecmp(tag, "embed") == 0 ||
            strcasecmp(tag, "source") == 0 || strcasecmp(tag, "track") == 0 ||
            strcasecmp(tag, "audio") == 0 || strcasecmp(tag, "video") == 0 ||
            strcasecmp(tag, "input") == 0) {
            *attr_name = "src";
            return true;
        }
    }
    if (strcmp(prop, "href") == 0) {
        if (strcasecmp(tag, "a") == 0 || strcasecmp(tag, "area") == 0 ||
            strcasecmp(tag, "link") == 0 || strcasecmp(tag, "base") == 0) {
            *attr_name = "href";
            return true;
        }
    }
    if (strcmp(prop, "alt") == 0 && strcasecmp(tag, "img") == 0) {
        *attr_name = "alt";
        return true;
    }
    return false;
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
static void _collect_form_controls_rec(DomNode* node, Item arr) {
    while (node) {
        if (node->is_element()) {
            DomElement* ce = (DomElement*)node;
            if (_is_listed_form_control(ce)) {
                js_array_push(arr, js_dom_wrap_element(ce));
            }
            _collect_form_controls_rec(ce->first_child, arr);
        }
        node = node->next_sibling;
    }
}

// Return the lowercased name/id key used to look up a form control by name.
// Returns nullptr if the element has neither.
static const char* _form_control_name_or_id(DomElement* e) {
    const char* n = dom_element_get_attribute(e, "name");
    if (n && *n) return n;
    if (e->id && *e->id) return e->id;
    return nullptr;
}

extern "C" Item js_dom_get_property(Item elem_item, Item prop_name) {
    // Range / Selection wrappers also live under MAP_KIND_DOM and route here.
    if (js_dom_item_is_range(elem_item))
        return js_dom_range_get_property(elem_item, prop_name);
    if (js_dom_item_is_selection(elem_item))
        return js_dom_selection_get_property(elem_item, prop_name);

    if (js_is_inline_style(elem_item)) {
        DomElement* owner = js_inline_style_owner(elem_item);
        if (!owner) return ItemNull;
        Item owner_item = js_dom_wrap_element(owner);
        return js_dom_get_style_property(owner_item, prop_name);
    }

    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    const char* prop = fn_to_cstr(prop_name);
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
            else if ((prop[0] >= '0' && prop[0] <= '9')) {
                char* ep = nullptr; long n = strtol(prop, &ep, 10);
                if (ep && *ep == '\0' && n >= 0) { idx = n; numeric = true; }
            }
            if (numeric) {
                Item arr = js_array_new(0);
                _collect_options(sel_elem->first_child, arr);
                if (idx >= 0 && idx < js_array_length(arr)) {
                    return js_array_get_int(arr, idx);
                }
                return make_js_undefined();
            }
        }
    }

    // Text node properties
    if (node->is_text()) {
        DomText* text_node = node->as_text();
        if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 || strcmp(prop, "textContent") == 0) {
            return text_node->text ? (Item){.item = s2it(heap_create_name(text_node->text))}
                                   : (Item){.item = s2it(heap_create_name(""))};
        }
        if (strcmp(prop, "length") == 0) {
            return (Item){.item = i2it((int64_t)dom_text_utf16_length(text_node))};
        }
        if (strcmp(prop, "nodeType") == 0) {
            return (Item){.item = i2it(3)}; // TEXT_NODE
        }
        if (strcmp(prop, "nodeName") == 0) {
            return (Item){.item = s2it(heap_create_name("#text"))};
        }
        if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
            DomNode* parent = text_node->parent;
            if (parent && parent->is_element()) {
                return js_dom_wrap_element(parent->as_element());
            }
            return ItemNull;
        }
        if (strcmp(prop, "nextSibling") == 0) {
            DomNode* sib = text_node->next_sibling;
            if (!sib) return ItemNull;
            return js_dom_wrap_element((void*)sib);
        }
        if (strcmp(prop, "previousSibling") == 0) {
            DomNode* sib = text_node->prev_sibling;
            if (!sib) return ItemNull;
            return js_dom_wrap_element((void*)sib);
        }
        // childNodes — text nodes have no children; return an empty NodeList
        // (per WHATWG DOM Node.childNodes is non-null on every node).
        if (strcmp(prop, "childNodes") == 0) {
            Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
            arr->type_id = LMD_TYPE_ARRAY;
            return (Item){.array = arr};
        }
        if (strcmp(prop, "firstChild") == 0 || strcmp(prop, "lastChild") == 0) {
            return ItemNull;
        }
        if (strcmp(prop, "ownerDocument") == 0) {
            // Walk up to find an element parent and use its doc.
            DomNode* p = text_node->parent;
            while (p && !p->is_element()) p = p->parent;
            if (p) {
                DomDocument* od = p->as_element()->doc;
                if (od && od != _js_current_document) {
                    Item w = lookup_foreign_doc_wrapper(od);
                    if (w.item != ITEM_NULL) return w;
                }
            }
            return js_get_document_object_value();
        }
        if (strcmp(prop, "replaceData") == 0) {
            return js_bind_function(js_new_function((void*)js_text_replace_data_method, 3),
                elem_item, NULL, 0);
        }
        if (strcmp(prop, "insertData") == 0) {
            return js_bind_function(js_new_function((void*)js_text_insert_data_method, 2),
                elem_item, NULL, 0);
        }
        if (strcmp(prop, "appendData") == 0) {
            return js_bind_function(js_new_function((void*)js_text_append_data_method, 1),
                elem_item, NULL, 0);
        }
        if (strcmp(prop, "deleteData") == 0) {
            return js_bind_function(js_new_function((void*)js_text_delete_data_method, 2),
                elem_item, NULL, 0);
        }
        if (strcmp(prop, "substringData") == 0) {
            return js_bind_function(js_new_function((void*)js_text_substring_data_method, 2),
                elem_item, NULL, 0);
        }
        log_debug("js_dom_get_property: unknown text node property '%s'", prop);
        return ItemNull;
    }

    // Comment node properties
    if (node->is_comment()) {
        DomComment* comment_node = node->as_comment();
        if (strcmp(prop, "data") == 0 || strcmp(prop, "nodeValue") == 0 || strcmp(prop, "textContent") == 0) {
            return comment_node->content ? (Item){.item = s2it(heap_create_name(comment_node->content))}
                                         : (Item){.item = s2it(heap_create_name(""))};
        }
        if (strcmp(prop, "nodeType") == 0) {
            // Reflect the underlying DomNodeType so the synthesized DOCTYPE
            // (created with node_type = DOM_NODE_DOCTYPE = 10) is visible to
            // DOM-spec node tests; plain comments still report COMMENT_NODE (8).
            return (Item){.item = i2it((int64_t)comment_node->node_type)};
        }
        if (strcmp(prop, "nodeName") == 0) {
            return (Item){.item = s2it(heap_create_name("#comment"))};
        }
        if (strcmp(prop, "length") == 0) {
            return (Item){.item = i2it((int64_t)comment_node->length)};
        }
        if (strcmp(prop, "parentNode") == 0 || strcmp(prop, "parentElement") == 0) {
            DomNode* parent = comment_node->parent;
            if (parent && parent->is_element()) {
                return js_dom_wrap_element(parent->as_element());
            }
            return ItemNull;
        }
        // childNodes — comment nodes have no children; return empty NodeList
        if (strcmp(prop, "childNodes") == 0) {
            Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
            arr->type_id = LMD_TYPE_ARRAY;
            return (Item){.array = arr};
        }
        if (strcmp(prop, "firstChild") == 0 || strcmp(prop, "lastChild") == 0) {
            return ItemNull;
        }
        if (strcmp(prop, "ownerDocument") == 0) {
            DomNode* p = comment_node->parent;
            while (p && !p->is_element()) p = p->parent;
            if (p) {
                DomDocument* od = p->as_element()->doc;
                if (od && od != _js_current_document) {
                    Item w = lookup_foreign_doc_wrapper(od);
                    if (w.item != ITEM_NULL) return w;
                }
            }
            return js_get_document_object_value();
        }
        log_debug("js_dom_get_property: unknown comment node property '%s'", prop);
        return ItemNull;
    }

    // Element properties below — safe to cast
    DomElement* elem = node->as_element();
    if (!elem) {
        log_debug("js_dom_get_property: node is not an element for property '%s'", prop);
        return ItemNull;
    }

    // tagName (uppercased per spec)
    if (strcmp(prop, "tagName") == 0) {
        return (Item){.item = s2it(uppercase_tag_name(elem->tag_name))};
    }

    // localName (lowercased per spec; tag names are stored lowercase already).
    if (strcmp(prop, "localName") == 0) {
        const char* tn = elem->tag_name ? elem->tag_name : "";
        return (Item){.item = s2it(heap_create_name(tn))};
    }

    // namespaceURI — HTML elements live in the XHTML namespace.
    if (strcmp(prop, "namespaceURI") == 0) {
        return (Item){.item = s2it(heap_create_name("http://www.w3.org/1999/xhtml"))};
    }

    // prefix — HTML elements have null prefix.
    if (strcmp(prop, "prefix") == 0) {
        return ItemNull;
    }

    // iframe.contentDocument / contentWindow — lazy-create a foreign HTML
    // document that backs the iframe's browsing context. Cached per element
    // so identity comparisons (===) work.
    if (elem->tag_name && strcasecmp(elem->tag_name, "iframe") == 0 &&
        (strcmp(prop, "contentDocument") == 0 || strcmp(prop, "contentWindow") == 0)) {
        extern Item js_iframe_get_content_document(DomElement* iframe);
        extern Item js_iframe_get_content_window  (DomElement* iframe);
        if (strcmp(prop, "contentDocument") == 0)
            return js_iframe_get_content_document(elem);
        return js_iframe_get_content_window(elem);
    }

    // id
    if (strcmp(prop, "id") == 0) {
        return (Item){.item = elem->id ? s2it(heap_create_name(elem->id))
                                        : s2it(heap_create_name(""))};
    }

    // className (space-joined class list)
    if (strcmp(prop, "className") == 0) {
        if (elem->class_count == 0) return (Item){.item = s2it(heap_create_name(""))};
        StrBuf* sb = strbuf_new_cap(64);
        for (int i = 0; i < elem->class_count; i++) {
            if (i > 0) strbuf_append_char(sb, ' ');
            strbuf_append_str(sb, elem->class_names[i]);
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // style — live CSSStyleDeclaration-like wrapper for inline style.
    if (strcmp(prop, "style") == 0) {
        return js_dom_get_inline_style_wrapper(elem);
    }

    // textContent (recursive text extraction)
    if (strcmp(prop, "textContent") == 0) {
        StrBuf* sb = strbuf_new_cap(128);
        collect_text_content((DomNode*)elem, sb);
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // innerHTML (recursive HTML serialization of children)
    if (strcmp(prop, "innerHTML") == 0) {
        StrBuf* sb = strbuf_new_cap(256);
        DomNode* child = elem->first_child;
        while (child) {
            collect_inner_html(child, sb);
            child = child->next_sibling;
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // v12: outerHTML (element itself + children)
    if (strcmp(prop, "outerHTML") == 0) {
        StrBuf* sb = strbuf_new_cap(256);
        collect_inner_html((DomNode*)elem, sb);
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    // nodeType
    if (strcmp(prop, "nodeType") == 0) {
        if (_is_tag(elem, "#document-fragment"))
            return (Item){.item = i2it(11)};
        return (Item){.item = i2it((int64_t)elem->node_type)};
    }

    // childElementCount
    if (strcmp(prop, "childElementCount") == 0) {
        int count = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) count++;
            child = child->next_sibling;
        }
        return (Item){.item = i2it((int64_t)count)};
    }

    // children (array of child DOM elements only)
    if (strcmp(prop, "children") == 0) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) {
                array_push(arr, js_dom_wrap_element(child->as_element()));
            }
            child = child->next_sibling;
        }
        return (Item){.array = arr};
    }

    // parentElement
    if (strcmp(prop, "parentElement") == 0) {
        DomNode* parent = (DomNode*)elem->parent;
        if (parent && parent->is_element()) {
            return js_dom_wrap_element(parent->as_element());
        }
        return ItemNull;
    }

    // parentNode (includes text nodes — returns any parent)
    if (strcmp(prop, "parentNode") == 0) {
        DomNode* parent = (DomNode*)elem->parent;
        if (parent && parent->is_element()) {
            return js_dom_wrap_element(parent->as_element());
        }
        return ItemNull;
    }

    // ownerDocument — returns the document proxy for any element.
    // For elements owned by a foreign document, return that foreign-doc
    // wrapper (so identity tests like `el.ownerDocument === foreignDoc` hold).
    if (strcmp(prop, "ownerDocument") == 0) {
        DomDocument* od = elem->doc;
        if (od && od != _js_current_document) {
            Item w = lookup_foreign_doc_wrapper(od);
            if (w.item != ITEM_NULL) return w;
        }
        return js_get_document_object_value();
    }

    // contentDocument / contentWindow — for iframe elements, return the main
    // document proxy so scripts like `iframe.contentDocument.createElement()`
    // work without crashing.  Not a real sub-document, but sufficient for
    // crash-safety tests and simple DOM manipulation.
    if (strcmp(prop, "contentDocument") == 0 || strcmp(prop, "contentWindow") == 0) {
        return js_get_document_object_value();
    }

    // firstChild (any node type, not just elements)
    if (strcmp(prop, "firstChild") == 0) {
        DomNode* child = elem->first_child;
        if (!child) return ItemNull;
        if (child->is_element()) return js_dom_wrap_element(child->as_element());
        // wrap text node
        return js_dom_wrap_element((DomElement*)(void*)child);
    }

    // lastChild (any node type)
    if (strcmp(prop, "lastChild") == 0) {
        DomNode* child = elem->last_child;
        if (!child) return ItemNull;
        if (child->is_element()) return js_dom_wrap_element(child->as_element());
        return js_dom_wrap_element((DomElement*)(void*)child);
    }

    // nextSibling (any node type)
    if (strcmp(prop, "nextSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->next_sibling;
        if (!sib) return ItemNull;
        if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
        return js_dom_wrap_element((DomElement*)(void*)sib);
    }

    // previousSibling (any node type)
    if (strcmp(prop, "previousSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->prev_sibling;
        if (!sib) return ItemNull;
        if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
        return js_dom_wrap_element((DomElement*)(void*)sib);
    }

    // firstElementChild
    if (strcmp(prop, "firstElementChild") == 0) {
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) return js_dom_wrap_element(child->as_element());
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // lastElementChild
    if (strcmp(prop, "lastElementChild") == 0) {
        DomNode* child = elem->last_child;
        while (child) {
            if (child->is_element()) return js_dom_wrap_element(child->as_element());
            child = child->prev_sibling;
        }
        return ItemNull;
    }

    // nextElementSibling
    if (strcmp(prop, "nextElementSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->next_sibling;
        while (sib) {
            if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
            sib = sib->next_sibling;
        }
        return ItemNull;
    }

    // previousElementSibling
    if (strcmp(prop, "previousElementSibling") == 0) {
        DomNode* sib = ((DomNode*)elem)->prev_sibling;
        while (sib) {
            if (sib->is_element()) return js_dom_wrap_element(sib->as_element());
            sib = sib->prev_sibling;
        }
        return ItemNull;
    }

    // childNodes (all children including text nodes)
    if (strcmp(prop, "childNodes") == 0) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) {
                array_push(arr, js_dom_wrap_element(child->as_element()));
            } else {
                // wrap text/comment nodes
                array_push(arr, js_dom_wrap_element((DomElement*)(void*)child));
            }
            child = child->next_sibling;
        }
        return (Item){.array = arr};
    }

    // children (array of child DOM elements only)
    if (strcmp(prop, "children") == 0) {
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) {
                array_push(arr, js_dom_wrap_element(child->as_element()));
            }
            child = child->next_sibling;
        }
        return (Item){.array = arr};
    }

    // length (for NodeList / HTMLCollection-like results)
    if (strcmp(prop, "length") == 0) {
        int count = 0;
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_element()) count++;
            child = child->next_sibling;
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
    if (strcmp(prop, "offsetWidth") == 0) {
        return (Item){.item = i2it((int64_t)elem->width)};
    }
    if (strcmp(prop, "offsetHeight") == 0) {
        return (Item){.item = i2it((int64_t)elem->height)};
    }

    // clientWidth / clientHeight — border box minus borders
    if (strcmp(prop, "clientWidth") == 0) {
        float bw = 0;
        if (elem->bound && elem->bound->border) {
            bw = elem->bound->border->width.left + elem->bound->border->width.right;
        }
        return (Item){.item = i2it((int64_t)(elem->width - bw))};
    }
    if (strcmp(prop, "clientHeight") == 0) {
        float bh = 0;
        if (elem->bound && elem->bound->border) {
            bh = elem->bound->border->width.top + elem->bound->border->width.bottom;
        }
        return (Item){.item = i2it((int64_t)(elem->height - bh))};
    }

    // offsetTop / offsetLeft — position relative to offsetParent
    if (strcmp(prop, "offsetTop") == 0) {
        return (Item){.item = i2it((int64_t)elem->y)};
    }
    if (strcmp(prop, "offsetLeft") == 0) {
        return (Item){.item = i2it((int64_t)elem->x)};
    }

    // offsetParent — nearest positioned ancestor (or body)
    if (strcmp(prop, "offsetParent") == 0) {
        DomNode* p = elem->parent;
        while (p) {
            if (p->is_element()) {
                DomElement* pe = p->as_element();
                // body is always an offsetParent
                if (pe->tag_name && strcasecmp(pe->tag_name, "body") == 0) {
                    return js_dom_wrap_element(pe);
                }
                // positioned element (not static)
                if (pe->position && pe->position->position != CSS_VALUE_STATIC) {
                    return js_dom_wrap_element(pe);
                }
            }
            p = p->parent;
        }
        return ItemNull;
    }

    // scrollWidth / scrollHeight — total scrollable content size
    if (strcmp(prop, "scrollWidth") == 0) {
        float cw = elem->content_width;
        float bw = elem->width;
        return (Item){.item = i2it((int64_t)(cw > bw ? cw : bw))};
    }
    if (strcmp(prop, "scrollHeight") == 0) {
        float ch = elem->content_height;
        float bh = elem->height;
        return (Item){.item = i2it((int64_t)(ch > bh ? ch : bh))};
    }

    // scrollTop / scrollLeft — current scroll position
    if (strcmp(prop, "scrollTop") == 0) {
        if (elem->scroller && elem->scroller->pane) {
            return (Item){.item = i2it((int64_t)elem->scroller->pane->v_scroll_position)};
        }
        return (Item){.item = i2it(0)};
    }
    if (strcmp(prop, "scrollLeft") == 0) {
        if (elem->scroller && elem->scroller->pane) {
            return (Item){.item = i2it((int64_t)elem->scroller->pane->h_scroll_position)};
        }
        return (Item){.item = i2it(0)};
    }

    // data (text node content) — check if the wrapped node is actually a DomText
    if (strcmp(prop, "data") == 0) {
        DomNode* node = (DomNode*)elem;  // may be DomText wrapped as DomElement*
        if (node->is_text()) {
            DomText* text_node = node->as_text();
            if (text_node->text && text_node->length > 0) {
                String* s = heap_strcpy((char*)text_node->text, text_node->length);
                return (Item){.item = s2it(s)};
            }
            return (Item){.item = s2it(heap_create_name(""))};
        }
        return ItemNull;
    }

    // nodeName — tag name for elements, "#text" for text nodes
    if (strcmp(prop, "nodeName") == 0) {
        DomNode* node = (DomNode*)elem;
        if (node->is_text()) {
            return (Item){.item = s2it(heap_create_name("#text"))};
        }
        return (Item){.item = s2it(uppercase_tag_name(elem->tag_name))};
    }

    // HTMLStyleElement.sheet — associated CSSStyleSheet (doesn't require native_element)
    if (strcmp(prop, "sheet") == 0 && elem->tag_name && strcasecmp(elem->tag_name, "style") == 0) {
        return js_cssom_get_style_element_sheet(elem_item);
    }

    // ------------------------------------------------------------------
    // F-5: HTMLSelectElement / HTMLOptionElement IDL properties
    // ------------------------------------------------------------------
    if (_is_tag(elem, "select")) {
        if (strcmp(prop, "options") == 0) {
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            // Compute selectedIndex (with default-reset rule) so the
            // collection's `selectedIndex` property mirrors the select.
            int64_t n = js_array_length(arr);
            int sel_idx = -1, first_non_disabled = -1;
            for (int64_t i = 0; i < n; i++) {
                DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
                if (!opt) continue;
                if (sel_idx < 0 && _get_selectedness(opt)) sel_idx = (int)i; // INT_CAST_OK
                if (first_non_disabled < 0 && !dom_element_has_attribute(opt, "disabled"))
                    first_non_disabled = (int)i; // INT_CAST_OK
            }
            int size = 0;
            const char* sz = dom_element_get_attribute(elem, "size");
            if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
            if (sel_idx < 0 && !dom_element_has_attribute(elem, "multiple") && size <= 1
                && !_select_is_dirty(elem))
                sel_idx = first_non_disabled;
            js_property_set(arr, (Item){.item = s2it(heap_create_name("selectedIndex"))},
                            (Item){.item = i2it(sel_idx)});
            js_property_set(arr, (Item){.item = s2it(heap_create_name("length"))},
                            (Item){.item = i2it(n)});
            _decorate_dom_collection(arr, "HTMLOptionsCollection");
            _register_select_options_owner(arr, elem, SELECT_COLLECTION_OPTIONS);
            return arr;
        }
        if (strcmp(prop, "length") == 0) {
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            return (Item){.item = i2it(js_array_length(arr))};
        }
        if (strcmp(prop, "selectedOptions") == 0) {
            Item exp = expando_get_or_create_map((DomNode*)elem);
            Item cache_key = (Item){.item = s2it(heap_create_name("__selectedOptions"))};
            Item out = (exp.item != ITEM_NULL) ? js_property_get(exp, cache_key) : ItemNull;
            if (get_type_id(out) != LMD_TYPE_ARRAY) {
                out = js_array_new(0);
                _decorate_dom_collection(out, "HTMLCollection");
                if (exp.item != ITEM_NULL) js_property_set(exp, cache_key, out);
            }
            _register_select_options_owner(out, elem, SELECT_COLLECTION_SELECTED_OPTIONS);
            _select_refresh_selected_options_collection(out, elem);
            return out;
        }
        if (strcmp(prop, "selectedIndex") == 0) {
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            int64_t n = js_array_length(arr);
            int first_non_disabled = -1;
            for (int64_t i = 0; i < n; i++) {
                DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
                if (!opt) continue;
                if (_get_selectedness(opt)) return (Item){.item = i2it(i)};
                if (first_non_disabled < 0 && !dom_element_has_attribute(opt, "disabled"))
                    first_non_disabled = (int)i; // INT_CAST_OK: option index
            }
            // Default-selected behavior: non-multiple, size<=1 select with
            // no explicit selectedness picks the first non-disabled option.
            int size = 0;
            const char* sz = dom_element_get_attribute(elem, "size");
            if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
            if (!dom_element_has_attribute(elem, "multiple") && size <= 1
                && first_non_disabled >= 0 && !_select_is_dirty(elem)) {
                return (Item){.item = i2it(first_non_disabled)};
            }
            return (Item){.item = i2it(-1)};
        }
        if (strcmp(prop, "value") == 0) {
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            int64_t n = js_array_length(arr);
            int first_non_disabled = -1;
            for (int64_t i = 0; i < n; i++) {
                DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
                if (!opt) continue;
                if (_get_selectedness(opt)) {
                    char* v = _option_value(opt);
                    String* s = heap_create_name(v ? v : "");
                    mem_free(v);
                    return (Item){.item = s2it(s)};
                }
                if (first_non_disabled < 0 && !dom_element_has_attribute(opt, "disabled"))
                    first_non_disabled = (int)i; // INT_CAST_OK: option index
            }
            int size = 0;
            const char* sz = dom_element_get_attribute(elem, "size");
            if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
            if (!dom_element_has_attribute(elem, "multiple") && size <= 1
                && first_non_disabled >= 0 && !_select_is_dirty(elem)) {
                DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, first_non_disabled));
                if (opt) {
                    char* v = _option_value(opt);
                    String* s = heap_create_name(v ? v : "");
                    mem_free(v);
                    return (Item){.item = s2it(s)};
                }
            }
            return (Item){.item = s2it(heap_create_name(""))};
        }
        if (strcmp(prop, "type") == 0) {
            const char* t = dom_element_has_attribute(elem, "multiple")
                ? "select-multiple" : "select-one";
            return (Item){.item = s2it(heap_create_name(t))};
        }
    }

    // HTMLOptionElement properties.
    if (_is_tag(elem, "option")) {
        if (strcmp(prop, "value") == 0) {
            char* v = _option_value(elem);
            String* s = heap_create_name(v ? v : "");
            mem_free(v);
            return (Item){.item = s2it(s)};
        }
        if (strcmp(prop, "text") == 0 || strcmp(prop, "label") == 0) {
            // label IDL: returns label attribute, falling back to text.
            if (strcmp(prop, "label") == 0) {
                const char* lab = dom_element_get_attribute(elem, "label");
                if (lab && *lab) return (Item){.item = s2it(heap_create_name(lab))};
            }
            char* t = _option_text(elem);
            String* s = heap_create_name(t ? t : "");
            mem_free(t);
            return (Item){.item = s2it(s)};
        }
        if (strcmp(prop, "selected") == 0) {
            if (_get_selectedness(elem)) return (Item){.item = b2it(true)};
            // Apply default-reset rule: in a non-multiple, size<=1 select
            // with no option having selectedness, the first non-disabled
            // direct-child option counts as selected.
            DomElement* sel = _option_owner_select(elem);
            if (!sel || _select_is_dirty(sel)) return (Item){.item = b2it(false)};
            if (dom_element_has_attribute(sel, "multiple")) return (Item){.item = b2it(false)};
            int size = 0;
            const char* sz = dom_element_get_attribute(sel, "size");
            if (sz) { char* ep = nullptr; long v = strtol(sz, &ep, 10); if (ep != sz && v > 0) size = (int)v; }
            if (size > 1) return (Item){.item = b2it(false)};
            // Walk options of sel; check none has selectedness; find first
            // non-disabled.
            Item arr = js_array_new(0);
            _collect_options(sel->first_child, arr);
            int64_t n = js_array_length(arr);
            int first_nd = -1;
            for (int64_t i = 0; i < n; i++) {
                DomElement* o = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
                if (!o) continue;
                if (_get_selectedness(o)) return (Item){.item = b2it(false)};
                if (first_nd < 0 && !dom_element_has_attribute(o, "disabled"))
                    first_nd = (int)i; // INT_CAST_OK: option index
            }
            if (first_nd < 0) return (Item){.item = b2it(false)};
            DomElement* first = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, first_nd));
            return (Item){.item = b2it(first == elem)};
        }
        if (strcmp(prop, "index") == 0) {
            return (Item){.item = i2it(_option_index_in_select(elem))};
        }
        if (strcmp(prop, "form") == 0) {
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
    if (strcmp(prop, "checked") == 0 && _is_tag(elem, "input")) {
        return (Item){.item = b2it(_get_checkedness(elem))};
    }
    if (strcmp(prop, "disabled") == 0 &&
        (_is_tag(elem, "input") || _is_tag(elem, "button") ||
         _is_tag(elem, "select") || _is_tag(elem, "textarea") ||
         _is_tag(elem, "fieldset") || _is_tag(elem, "optgroup") ||
         _is_tag(elem, "option"))) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "disabled"))};
    }
    if (strcmp(prop, "value") == 0 && _is_tag(elem, "input") && !tc_is_text_control_elem(elem)) {
        const char* v = dom_element_get_attribute(elem, "value");
        return (Item){.item = s2it(heap_create_name(v ? v : ""))};
    }

    // ------------------------------------------------------------------
    // HTML form text-control (HTMLInputElement / HTMLTextAreaElement)
    // properties — must intercept BEFORE attribute fallback so .value
    // returns the live IDL value, not the static `value` attribute.
    // ------------------------------------------------------------------
    if (tc_is_text_control_elem(elem)) {
        if (strcmp(prop, "value") == 0) {
            tc_ensure_init(elem);
            FormControlProp* f = elem->form;
            String* s = heap_strcpy(f->current_value ? f->current_value : (char*)"",
                                    (int64_t)f->current_value_len);
            return (Item){.item = s2it(s)};
        }
        if (strcmp(prop, "defaultValue") == 0) {
            // <input>: getAttribute("value"); <textarea>: text content of children.
            if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
                StrBuf* sb = strbuf_new_cap(64);
                collect_text_content((DomNode*)elem, sb);
                String* s = heap_create_name(sb->str ? sb->str : "");
                strbuf_free(sb);
                return (Item){.item = s2it(s)};
            }
            const char* v = dom_element_get_attribute(elem, "value");
            return (Item){.item = s2it(heap_create_name(v ? v : ""))};
        }
        if (strcmp(prop, "selectionStart") == 0) {
            tc_ensure_init(elem);
            uint32_t start = 0;
            form_control_get_selection(js_dom_current_state(), (View*)elem, &start, NULL, NULL);
            return (Item){.item = i2it((int64_t)start)};
        }
        if (strcmp(prop, "selectionEnd") == 0) {
            tc_ensure_init(elem);
            uint32_t end = 0;
            form_control_get_selection(js_dom_current_state(), (View*)elem, NULL, &end, NULL);
            return (Item){.item = i2it((int64_t)end)};
        }
        if (strcmp(prop, "selectionDirection") == 0) {
            tc_ensure_init(elem);
            const char* d = "none";
            uint8_t direction = 0;
            form_control_get_selection(js_dom_current_state(), (View*)elem, NULL, NULL, &direction);
            if (direction == 1) d = "forward";
            else if (direction == 2) d = "backward";
            return (Item){.item = s2it(heap_create_name(d))};
        }
        if (strcmp(prop, "textLength") == 0) {
            tc_ensure_init(elem);
            return (Item){.item = i2it((int64_t)elem->form->current_value_u16_len)};
        }
        if (strcmp(prop, "setSelectionRange") == 0)
            return js_new_function((void*)js_text_control_set_selection_range, 3);
        if (strcmp(prop, "select") == 0)
            return js_new_function((void*)js_text_control_select, 0);
        if (strcmp(prop, "setRangeText") == 0)
            return js_new_function((void*)js_text_control_set_range_text, 4);
    }

    // ------------------------------------------------------------------
    // F-0: IDL attribute reflection for form elements
    // ------------------------------------------------------------------

    // F-1: HTMLFormElement.elements — snapshot array of listed form controls
    if (_is_tag(elem, "form") && strcmp(prop, "elements") == 0) {
        Item arr = js_array_new(0);
        _collect_form_controls_rec(elem->first_child, arr);
        return arr;
    }
    // F-1: HTMLFormElement.length → number of listed controls
    if (_is_tag(elem, "form") && strcmp(prop, "length") == 0) {
        Item arr = js_array_new(0);
        _collect_form_controls_rec(elem->first_child, arr);
        return (Item){.item = i2it(js_array_length(arr))};
    }

    // Helper: read a non-negative integer attr; return default_val if absent/invalid
    auto _reflect_int_attr = [&](const char* attr_name, int default_val) -> int {
        const char* v = dom_element_get_attribute(elem, attr_name);
        if (!v) return default_val;
        char* end = nullptr;
        long n = strtol(v, &end, 10);
        return (end != v && n >= 0) ? (int)n : default_val; // INT_CAST_OK: attribute integer value
    };

    // Boolean reflection: required, multiple, readOnly/readonly, noValidate
    if (strcmp(prop, "required") == 0 &&
        (_is_tag(elem, "input") || _is_tag(elem, "select") || _is_tag(elem, "textarea"))) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "required"))};
    }
    if (strcmp(prop, "multiple") == 0 &&
        (_is_tag(elem, "input") || _is_tag(elem, "select"))) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "multiple"))};
    }
    if ((strcmp(prop, "readOnly") == 0 || strcmp(prop, "readonly") == 0) &&
        (_is_tag(elem, "input") || _is_tag(elem, "textarea"))) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "readonly"))};
    }
    if (strcmp(prop, "noValidate") == 0 && _is_tag(elem, "form")) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "novalidate"))};
    }
    // name attribute (all listed form controls and form/fieldset)
    if (strcmp(prop, "name") == 0 &&
        (_is_tag(elem, "input") || _is_tag(elem, "button") || _is_tag(elem, "select") ||
         _is_tag(elem, "textarea") || _is_tag(elem, "form") || _is_tag(elem, "fieldset") ||
         _is_tag(elem, "output") || _is_tag(elem, "object"))) {
        const char* v = dom_element_get_attribute(elem, "name");
        return (Item){.item = s2it(heap_create_name(v ? v : ""))};
    }
    // type for button (default "submit"; only valid values: "submit","reset","button")
    if (strcmp(prop, "type") == 0 && _is_tag(elem, "button")) {
        const char* v = dom_element_get_attribute(elem, "type");
        if (v && (strcasecmp(v, "submit") == 0 || strcasecmp(v, "reset") == 0 || strcasecmp(v, "button") == 0)) {
            char buf[8];
            for (int i = 0; v[i] && i < 7; i++) buf[i] = (char)tolower((unsigned char)v[i]), buf[i+1] = '\0';
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        return (Item){.item = s2it(heap_create_name("submit"))};
    }
    // placeholder (input, textarea)
    if (strcmp(prop, "placeholder") == 0 &&
        (_is_tag(elem, "input") || _is_tag(elem, "textarea"))) {
        const char* v = dom_element_get_attribute(elem, "placeholder");
        return (Item){.item = s2it(heap_create_name(v ? v : ""))};
    }
    // autocomplete (form, input, select, textarea)
    if (strcmp(prop, "autocomplete") == 0 &&
        (_is_tag(elem, "form") || _is_tag(elem, "input") || _is_tag(elem, "select") || _is_tag(elem, "textarea"))) {
        const char* v = dom_element_get_attribute(elem, "autocomplete");
        return (Item){.item = s2it(heap_create_name(v ? v : ""))};
    }
    // pattern, min, max, step, accept (input only — simple string reflection)
    if (_is_tag(elem, "input") &&
        (strcmp(prop, "pattern") == 0 || strcmp(prop, "min") == 0 || strcmp(prop, "max") == 0 ||
         strcmp(prop, "step") == 0 || strcmp(prop, "accept") == 0)) {
        const char* v = dom_element_get_attribute(elem, prop);
        return (Item){.item = s2it(heap_create_name(v ? v : ""))};
    }
    // HTMLFormElement: action, method, enctype/encoding, acceptCharset, target
    if (_is_tag(elem, "form")) {
        if (strcmp(prop, "action") == 0) {
            const char* v = dom_element_get_attribute(elem, "action");
            if (v && *v) return (Item){.item = s2it(heap_create_name(v))};
            // Empty/missing action falls back to document URL per HTML spec.
            DomDocument* doc = elem->doc;
            const char* doc_url = "";
            if (doc && doc->url) {
                const char* href = url_get_href(doc->url);
                if (href) doc_url = href;
            }
            return (Item){.item = s2it(heap_create_name(doc_url))};
        }
        if (strcmp(prop, "target") == 0) {
            const char* v = dom_element_get_attribute(elem, prop);
            return (Item){.item = s2it(heap_create_name(v ? v : ""))};
        }
        if (strcmp(prop, "method") == 0) {
            const char* v = dom_element_get_attribute(elem, "method");
            return (Item){.item = s2it(heap_create_name(_normalise_method(v)))};
        }
        if (strcmp(prop, "enctype") == 0 || strcmp(prop, "encoding") == 0) {
            const char* v = dom_element_get_attribute(elem, "enctype");
            return (Item){.item = s2it(heap_create_name(_normalise_enctype(v)))};
        }
        if (strcmp(prop, "acceptCharset") == 0) {
            const char* v = dom_element_get_attribute(elem, "accept-charset");
            return (Item){.item = s2it(heap_create_name(v ? v : ""))};
        }
    }
    // HTMLTextAreaElement: wrap (default "soft"), rows (default 2), cols (default 20)
    if (_is_tag(elem, "textarea")) {
        if (strcmp(prop, "wrap") == 0) {
            const char* v = dom_element_get_attribute(elem, "wrap");
            return (Item){.item = s2it(heap_create_name(v ? v : "soft"))};
        }
        if (strcmp(prop, "rows") == 0) {
            return (Item){.item = i2it(_reflect_int_attr("rows", 2))};
        }
        if (strcmp(prop, "cols") == 0) {
            return (Item){.item = i2it(_reflect_int_attr("cols", 20))};
        }
        if (strcmp(prop, "maxLength") == 0) {
            const char* v = dom_element_get_attribute(elem, "maxlength");
            if (!v) return (Item){.item = i2it(-1)};
            char* end = nullptr; long n = strtol(v, &end, 10);
            return (Item){.item = i2it((end != v && n >= 0) ? n : -1)};
        }
        if (strcmp(prop, "minLength") == 0) {
            return (Item){.item = i2it(_reflect_int_attr("minlength", 0))};
        }
    }
    // HTMLInputElement: maxLength (default -1), minLength (default 0), size (default 20)
    if (_is_tag(elem, "input")) {
        if (strcmp(prop, "width") == 0) {
            return (Item){.item = i2it(_reflect_int_attr("width", 0))};
        }
        if (strcmp(prop, "height") == 0) {
            return (Item){.item = i2it(_reflect_int_attr("height", 0))};
        }
        if (strcmp(prop, "maxLength") == 0) {
            const char* v = dom_element_get_attribute(elem, "maxlength");
            if (!v) return (Item){.item = i2it(-1)};
            char* end = nullptr; long n = strtol(v, &end, 10);
            return (Item){.item = i2it((end != v && n >= 0) ? n : -1)};
        }
        if (strcmp(prop, "minLength") == 0) {
            return (Item){.item = i2it(_reflect_int_attr("minlength", 0))};
        }
        if (strcmp(prop, "size") == 0) {
            return (Item){.item = i2it(_reflect_int_attr("size", 20))};
        }
    }
    // HTMLSelectElement: size (default 0 unless multiple, but 0 is spec default)
    if (_is_tag(elem, "select") && strcmp(prop, "size") == 0) {
        return (Item){.item = i2it(_reflect_int_attr("size", 0))};
    }
    const char* string_attr = nullptr;
    if (_is_string_reflected(elem, prop, &string_attr)) {
        const char* v = dom_element_get_attribute(elem, string_attr);
        return (Item){.item = s2it(heap_create_name(v ? v : ""))};
    }

    // HTMLInputElement.defaultChecked — reflects `checked` content attribute
    if (strcmp(prop, "defaultChecked") == 0 && _is_tag(elem, "input")) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "checked"))};
    }
    // HTMLOptionElement.defaultSelected — reflects `selected` content attribute
    if (strcmp(prop, "defaultSelected") == 0 && _is_tag(elem, "option")) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "selected"))};
    }
    // HTMLLabelElement.htmlFor / HTMLOutputElement.htmlFor — reflects `for`
    if (strcmp(prop, "htmlFor") == 0 &&
        (_is_tag(elem, "label") || _is_tag(elem, "output"))) {
        const char* v = dom_element_get_attribute(elem, "for");
        return (Item){.item = s2it(heap_create_name(v ? v : ""))};
    }
    // HTMLOutputElement.defaultValue — descendant text content if no override
    // has been set. We do not yet track an explicit override (defaultValue
    // setter), so this always returns current descendant text content. That
    // matches WPT reset-form-html behavior where defaultValue tracks textContent.
    if (strcmp(prop, "defaultValue") == 0 && _is_tag(elem, "output")) {
        StrBuf* sb = strbuf_new_cap(32);
        collect_text_content((DomNode*)elem, sb);
        String* s = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(s)};
    }
    // HTMLOutputElement.value — descendant text content (getter); we do not
    // track an explicit value override yet.
    if (strcmp(prop, "value") == 0 && _is_tag(elem, "output")) {
        StrBuf* sb = strbuf_new_cap(32);
        collect_text_content((DomNode*)elem, sb);
        String* s = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(s)};
    }
    // tabIndex — reflects `tabindex` as integer, otherwise returns the HTML
    // default for elements that are naturally focusable.
    if (strcmp(prop, "tabIndex") == 0) {
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
    if (strcmp(prop, "inputMode") == 0) {
        const char* v = dom_element_get_attribute(elem, "inputmode");
        if (!v) return (Item){.item = s2it(heap_create_name(""))};
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
        return (Item){.item = s2it(heap_create_name(out))};
    }
    if (strcmp(prop, "enterKeyHint") == 0) {
        const char* v = dom_element_get_attribute(elem, "enterkeyhint");
        if (!v) return (Item){.item = s2it(heap_create_name(""))};
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
        return (Item){.item = s2it(heap_create_name(out))};
    }
    // CE-1 / CE-4 (Radiant_Design_Content_Editable.md §4.2 + §10):
    // contentEditable returns "true"/"false"/"plaintext-only"/"inherit".
    // isContentEditable is the computed property — walks ancestors honouring
    // inheritance and ="false" islands.
    if (strcmp(prop, "contentEditable") == 0) {
        if (!dom_element_has_attribute(elem, "contenteditable")) {
            return (Item){.item = s2it(heap_create_name("inherit"))};
        }
        const char* out = js_dom_normalize_contenteditable(
            dom_element_get_attribute(elem, "contenteditable"));
        return (Item){.item = s2it(heap_create_name(out ? out : "inherit"))};
    }
    if (strcmp(prop, "isContentEditable") == 0) {
        // Walk ancestors. Editable iff the nearest ce-bearing ancestor has
        // value true|""|plaintext-only AND we are not inside a ce="false"
        // subtree below that host.
        bool saw_false = false;
        DomNode* p = (DomNode*)elem;
        while (p) {
            if (p->node_type == DOM_NODE_ELEMENT) {
                DomElement* e = (DomElement*)p;
                if (dom_element_has_attribute(e, "contenteditable")) {
                    const char* v = dom_element_get_attribute(e, "contenteditable");
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
    if (strcmp(prop, "autocapitalize") == 0) {
        return (Item){.item = s2it(heap_create_name(js_dom_get_autocapitalize(elem)))};
    }
    if (strcmp(prop, "autocorrect") == 0) {
        return (Item){.item = b2it(js_dom_get_autocorrect(elem))};
    }
    if (strcmp(prop, "spellcheck") == 0) {
        return (Item){.item = b2it(js_dom_get_spellcheck(elem))};
    }
    if (strcmp(prop, "writingSuggestions") == 0) {
        return (Item){.item = s2it(heap_create_name(js_dom_get_writing_suggestions(elem)))};
    }
    if (strcmp(prop, "dataset") == 0) {
        Item dataset = js_new_object();
        int attr_count = 0;
        const char** names = dom_element_get_attribute_names(elem, &attr_count);
        for (int i = 0; i < attr_count; i++) {
            char key_buf[128];
            if (!js_dom_data_attr_to_dataset_key(names[i], key_buf, sizeof(key_buf))) continue;
            const char* value = dom_element_get_attribute(elem, names[i]);
            js_property_set(dataset,
                (Item){.item = s2it(heap_create_name(key_buf))},
                (Item){.item = s2it(heap_create_name(value ? value : ""))});
        }
        return dataset;
    }
    // autofocus boolean reflection (HTML global attribute).
    if (strcmp(prop, "autofocus") == 0) {
        return (Item){.item = b2it(dom_element_has_attribute(elem, "autofocus"))};
    }

    // formAction / formMethod / formEnctype / formTarget / formNoValidate
    // (HTMLButtonElement and HTMLInputElement). Per spec, `formAction` getter
    // returns the document's URL when the attribute is missing or empty.
    if (_is_tag(elem, "input") || _is_tag(elem, "button")) {
        if (strcmp(prop, "formAction") == 0) {
            const char* v = dom_element_get_attribute(elem, "formaction");
            if (v && *v) return (Item){.item = s2it(heap_create_name(v))};
            // fall back to document URL
            DomDocument* doc = elem->doc;
            const char* doc_url = "";
            if (doc && doc->url) {
                const char* href = url_get_href(doc->url);
                if (href) doc_url = href;
            }
            return (Item){.item = s2it(heap_create_name(doc_url))};
        }
        if (strcmp(prop, "formMethod") == 0) {
            const char* v = dom_element_get_attribute(elem, "formmethod");
            return (Item){.item = s2it(heap_create_name(_normalise_method(v)))};
        }
        if (strcmp(prop, "formEnctype") == 0) {
            const char* v = dom_element_get_attribute(elem, "formenctype");
            return (Item){.item = s2it(heap_create_name(_normalise_enctype(v)))};
        }
        if (strcmp(prop, "formTarget") == 0) {
            const char* v = dom_element_get_attribute(elem, "formtarget");
            return (Item){.item = s2it(heap_create_name(v ? v : ""))};
        }
        if (strcmp(prop, "formNoValidate") == 0) {
            return (Item){.item = b2it(dom_element_has_attribute(elem, "formnovalidate"))};
        }
    }

    // ------------------------------------------------------------------
    // F-4: Constraint Validation API property getters
    // ------------------------------------------------------------------
    // willValidate: true if element is a candidate for constraint validation
    if (strcmp(prop, "willValidate") == 0) {
        bool is_form_ctrl = elem->tag_name && (
            strcasecmp(elem->tag_name, "input") == 0 ||
            strcasecmp(elem->tag_name, "select") == 0 ||
            strcasecmp(elem->tag_name, "textarea") == 0 ||
            strcasecmp(elem->tag_name, "button") == 0);
        if (!is_form_ctrl) return (Item){.item = ITEM_FALSE};
        return (Item){.item = b2it(!_elem_is_barred(elem))};
    }
    // validity: returns a ValidityState object
    if (strcmp(prop, "validity") == 0) {
        return _build_validity_state(elem);
    }
    // validationMessage: custom validity message or empty string
    if (strcmp(prop, "validationMessage") == 0) {
        // Barred elements (disabled, readonly, etc.) always have empty validationMessage
        bool is_form_ctrl = elem->tag_name && (
            strcasecmp(elem->tag_name, "input") == 0 ||
            strcasecmp(elem->tag_name, "select") == 0 ||
            strcasecmp(elem->tag_name, "textarea") == 0 ||
            strcasecmp(elem->tag_name, "button") == 0);
        if (!is_form_ctrl || _elem_is_barred(elem)) {
            return (Item){.item = s2it(heap_create_name(""))};
        }
        if (elem->form && elem->form->custom_validity_msg &&
            elem->form->custom_validity_msg[0] != '\0') {
            return (Item){.item = s2it(heap_create_name(elem->form->custom_validity_msg))};
        }
        return (Item){.item = s2it(heap_create_name(""))};
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
        static const char* form_idl_props[] = {
            "elements", "length", "action", "method", "enctype", "encoding",
            "acceptCharset", "target", "noValidate", "autocomplete", "name",
            "submit", "reset", "checkValidity", "reportValidity", "requestSubmit",
            nullptr
        };
        bool is_idl = false;
        for (int i = 0; form_idl_props[i]; i++) {
            if (strcmp(prop, form_idl_props[i]) == 0) { is_idl = true; break; }
        }
        if (!is_idl) {
            Item matches = js_array_new(0);
            std::function<void(DomNode*)> walk = [&](DomNode* node) {
                while (node) {
                    if (node->is_element()) {
                        DomElement* ce = (DomElement*)node;
                        if (_is_listed_form_control(ce)) {
                            const char* k = _form_control_name_or_id(ce);
                            if (k && strcmp(k, prop) == 0) {
                                js_array_push(matches, js_dom_wrap_element(ce));
                            }
                        }
                        walk(ce->first_child);
                    }
                    node = node->next_sibling;
                }
            };
            walk(elem->first_child);
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
            Item key = (Item){.item = s2it(heap_create_name(event_prop_name))};
            Item val = js_property_get(exp_map, key);
            if (val.item != ITEM_NULL && !is_js_undefined(val)) {
                return val;
            }
        }
    }

    // fall back to native element attribute access
    if (elem->native_element) {
        const char* attr_val = dom_element_get_attribute(elem, prop);
        if (attr_val) {
            return (Item){.item = s2it(heap_create_name(attr_val))};
        }
    }

    // EventTarget methods must be callable even when MIR takes the generic
    // property-call fallback instead of the DOM method dispatcher.
    if (strcmp(prop, "addEventListener") == 0)
        return js_new_function((void*)js_eventtarget_add_listener, 3);
    if (strcmp(prop, "removeEventListener") == 0)
        return js_new_function((void*)js_eventtarget_remove_listener, 3);
    if (strcmp(prop, "dispatchEvent") == 0)
        return js_new_function((void*)js_eventtarget_dispatch, 1);
    if (strcmp(prop, "focus") == 0) {
        Item bound_args[1] = { elem_item };
        return js_bind_function(js_new_function((void*)js_dom_focus_method, 1),
            make_js_undefined(), bound_args, 1);
    }
    if (strcmp(prop, "blur") == 0) {
        Item bound_args[1] = { elem_item };
        return js_bind_function(js_new_function((void*)js_dom_blur_method, 1),
            make_js_undefined(), bound_args, 1);
    }
    if (strcmp(prop, "select") == 0) {
        Item bound_args[1] = { elem_item };
        return js_bind_function(js_new_function((void*)js_dom_select_method, 1),
            make_js_undefined(), bound_args, 1);
    }
    if (strcmp(prop, "setSelectionRange") == 0)
        return js_bind_function(js_new_function((void*)js_text_control_set_selection_range, 3),
            elem_item, NULL, 0);
    if (strcmp(prop, "matches") == 0 ||
        strcmp(prop, "webkitMatchesSelector") == 0 ||
        strcmp(prop, "msMatchesSelector") == 0) {
        return js_new_function((void*)js_dom_matches_method, 1);
    }

    // DOM method names accessed as properties (not calls) return ITEM_TRUE
    // so that feature-detection patterns like `elem.getAttribute && elem.getAttribute("class")`
    // work correctly (jQuery, Sizzle, etc.)
    static const char* dom_methods[] = {
        "getAttribute", "setAttribute", "removeAttribute", "hasAttribute", "toggleAttribute",
        "querySelector", "querySelectorAll", "closest", "matches",
        "appendChild", "removeChild", "replaceChild", "replaceWith", "insertBefore",
        "insertAdjacentElement", "insertAdjacentHTML",
        "attachShadow",
        "cloneNode", "contains", "hasChildNodes", "normalize",
        "addEventListener", "removeEventListener", "dispatchEvent",
        "remove", "getBoundingClientRect", "getElementsByTagName",
        "getElementsByClassName", "compareDocumentPosition",
        "append", "prepend", "getClientRects", "focus", "blur",
        "toString",
        "setSelectionRange", "select",
        "submit", "reset", "checkValidity", "reportValidity", "requestSubmit",
        "setCustomValidity",
        NULL
    };
    for (int i = 0; dom_methods[i]; i++) {
        if (strcmp(prop, dom_methods[i]) == 0) {
            return (Item){.item = ITEM_TRUE};
        }
    }

    // check expando properties (arbitrary JS values stored on this DOM node)
    {
        Item exp_map = expando_get_map((DomNode*)elem);
        if (exp_map.item != ITEM_NULL) {
            Item key = (Item){.item = s2it(heap_create_name(prop))};
            if (expando_map_has_key(exp_map, key)) {
                return js_property_get(exp_map, key);
            }
        }
    }

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

// Helper: parse class_names from space-separated string, updates elem->class_names/class_count
static void parse_class_names(DomElement* elem, const char* class_str) {
    if (!elem || !class_str) return;

    // count classes
    int count = 0;
    const char* p = class_str;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        count++;
        while (*p && *p != ' ') p++;
    }

    // allocate class_names array in the document arena
    Pool* pool = elem->doc ? elem->doc->pool : nullptr;
    const char** names = nullptr;
    if (count > 0 && pool) {
        names = (const char**)pool_alloc(pool, count * sizeof(const char*));
        int idx = 0;
        p = class_str;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            const char* start = p;
            while (*p && *p != ' ') p++;
            size_t len = p - start;
            char* cname = (char*)pool_alloc(pool, len + 1);
            memcpy(cname, start, len);
            cname[len] = '\0';
            names[idx++] = cname;
        }
    }

    elem->class_names = names;
    elem->class_count = count;
    elem->styles_resolved = false;  // mark for re-cascading
}

extern "C" Item js_dom_set_property(Item elem_item, Item prop_name, Item value) {
    // Range / Selection wrappers expose only read-only attributes; silently
    // ignore writes (matches W3C: setters are no-ops, not TypeError).
    if (js_dom_item_is_range(elem_item) || js_dom_item_is_selection(elem_item))
        return value;

    if (js_is_inline_style(elem_item)) {
        DomElement* owner = js_inline_style_owner(elem_item);
        if (!owner) return ItemNull;
        Item owner_item = js_dom_wrap_element(owner);
        return js_dom_set_style_property(owner_item, prop_name, value);
    }

    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node) {
        log_debug("js_dom_set_property: not a DOM node");
        return ItemNull;
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return ItemNull;

    // text node .data property
    if (node->is_text() && strcmp(prop, "data") == 0) {
        DomText* text_node = node->as_text();
        const char* new_text = fn_to_cstr(value);
        if (new_text) {
            uint32_t old_u16_len = dom_text_utf16_length(text_node);
            js_dom_replace_text_data(text_node, 0, old_u16_len, new_text);
            log_debug("js_dom_set_property: set text node data='%.30s'", new_text);
        }
        return value;
    }

    // remaining properties require an element node
    if (!node->is_element()) {
        log_debug("js_dom_set_property: node is not an element for property '%s'", prop);
        return ItemNull;
    }
    DomElement* elem = node->as_element();

    auto item_to_scroll_value = [](Item scroll_value) -> float {
        TypeId value_type = get_type_id(scroll_value);
        if (value_type == LMD_TYPE_INT) return (float)it2i(scroll_value);
        if (value_type == LMD_TYPE_FLOAT) return (float)it2d(scroll_value);
        if (value_type == LMD_TYPE_BOOL) return it2b(scroll_value) ? 1.0f : 0.0f;
        return 0.0f;
    };

    if (strcmp(prop, "scrollTop") == 0 || strcmp(prop, "scrollLeft") == 0) {
        float scroll_value = item_to_scroll_value(value);
        if (scroll_value < 0.0f) scroll_value = 0.0f;

        bool is_vertical = strcmp(prop, "scrollTop") == 0;
        bool is_root_scroll_target =
            (elem->tag_name && (strcasecmp(elem->tag_name, "html") == 0 ||
                                strcasecmp(elem->tag_name, "body") == 0));

        if (is_root_scroll_target && elem->doc) {
            if (is_vertical) {
                elem->doc->pending_viewport_scroll_y = scroll_value;
            } else {
                elem->doc->pending_viewport_scroll_x = scroll_value;
            }
            log_debug("js_dom_set_property: pending viewport %s=%.1f on <%s>",
                      prop, scroll_value, elem->tag_name);
            return value;
        }

        if (elem->scroller && elem->scroller->pane) {
            if (is_vertical) {
                elem->scroller->pane->v_scroll_position = scroll_value;
            } else {
                elem->scroller->pane->h_scroll_position = scroll_value;
            }
            log_debug("js_dom_set_property: set %s=%.1f on <%s>",
                      prop, scroll_value, elem->tag_name ? elem->tag_name : "?");
            return value;
        }
    }

    if (strcmp(prop, "srcdoc") == 0 && _is_tag(elem, "iframe")) {
        const char* srcdoc = fn_to_cstr(value);
        dom_element_set_attribute(elem, "srcdoc", srcdoc ? srcdoc : "");
        _schedule_iframe_load(elem);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    // className
    if (strcmp(prop, "className") == 0) {
        const char* class_str = fn_to_cstr(value);
        if (class_str) {
            parse_class_names(elem, class_str);
            // also update the native element attribute
            dom_element_set_attribute(elem, "class", class_str);
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
    if (strcmp(prop, "contentEditable") == 0) {
        const char* s = fn_to_cstr(value);
        if (!s) s = "";
        if (*s == '\0') {
            dom_element_remove_attribute(elem, "contenteditable");
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }
        const char* normalized = js_dom_normalize_contenteditable(s);
        if (!normalized) {
            log_debug("js_dom_contentEditable_setter_syntax_error: invalid value '%s'", s);
            js_dom_throw_syntax_error("Invalid contentEditable value");
            return value;
        }
        if (strcmp(normalized, "inherit") == 0) {
            dom_element_remove_attribute(elem, "contenteditable");
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }
        dom_element_set_attribute(elem, "contenteditable", normalized);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (strcmp(prop, "autocapitalize") == 0) {
        const char* s = js_dom_to_attr_cstr(value);
        dom_element_set_attribute(elem, "autocapitalize", s);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (strcmp(prop, "autocorrect") == 0) {
        dom_element_set_attribute(elem, "autocorrect", js_is_truthy(value) ? "on" : "off");
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (strcmp(prop, "spellcheck") == 0) {
        const char* s = js_is_truthy(value) ? "true" : "false";
        TypeId vt = get_type_id(value);
        if (vt == LMD_TYPE_STRING || vt == LMD_TYPE_SYMBOL) {
            const char* raw = fn_to_cstr(value);
            s = raw ? raw : "";
        }
        dom_element_set_attribute(elem, "spellcheck", s);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    if (strcmp(prop, "writingSuggestions") == 0) {
        const char* s = js_is_truthy(value) ? "true" : "false";
        TypeId vt = get_type_id(value);
        if (vt == LMD_TYPE_STRING || vt == LMD_TYPE_SYMBOL) {
            s = js_dom_to_attr_cstr(value);
        }
        dom_element_set_attribute(elem, "writingsuggestions", s);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    // id
    if (strcmp(prop, "id") == 0) {
        const char* id_str = fn_to_cstr(value);
        if (id_str && elem->doc && elem->doc->pool) {
            size_t len = strlen(id_str);
            char* id_copy = (char*)pool_alloc(elem->doc->pool, len + 1);
            memcpy(id_copy, id_str, len);
            id_copy[len] = '\0';
            elem->id = id_copy;
            dom_element_set_attribute(elem, "id", id_str);
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            log_debug("js_dom_set_property: set id='%s' on <%s>",
                      id_str, elem->tag_name ? elem->tag_name : "?");
        }
        return value;
    }

    // textContent
    if (strcmp(prop, "textContent") == 0) {
        const char* text_str = fn_to_cstr(value);
        if (text_str) {
            // remove all children and add a single text node
            // first, detach all children (firing pre_remove for live ranges)
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
            // create a text node with the new content
            String* s = js_dom_create_document_string(elem->doc, text_str, strlen(text_str));
            DomText* text_node = dom_text_create(s, elem);
            if (text_node) {
                ((DomNode*)text_node)->parent = (DomNode*)elem;
                elem->first_child = (DomNode*)text_node;
                elem->last_child = (DomNode*)text_node;
                dom_post_insert((DomNode*)elem, (DomNode*)text_node);
            }
            log_debug("js_dom_set_property: set textContent on <%s>",
                      elem->tag_name ? elem->tag_name : "?");
            js_dom_mutation_notify(DOM_JS_MUTATION_TREE_REPLACE, (DomNode*)elem, elem->parent);
        }
        return value;
    }

    // v12b: innerHTML setter — parse HTML and replace children
    if (strcmp(prop, "innerHTML") == 0) {
        const char* html_str = fn_to_cstr(value);
        if (!html_str) return ItemNull;
        DomDocument* doc = elem->doc;

        // 1. Remove all existing children
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

        if (doc && doc->input && elem->native_element &&
            elem->native_element->length > 0) {
            if (elem->native_element->length > INT32_MAX) {
                log_error("js_dom_innerHTML_clear_native_children: too many children");
                return value;
            }
            MarkEditor editor(doc->input, EDIT_MODE_INLINE);
            Item cleared = editor.elmt_delete_children(
                {.element = elem->native_element},
                0,
                (int)elem->native_element->length);
            if (get_type_id(cleared) == LMD_TYPE_ELEMENT) {
                elem->native_element = cleared.element;
            } else {
                log_error("js_dom_innerHTML_clear_native_children: edit failed");
                return value;
            }
        }

        // 2. Empty string → done (cleared children)
        if (html_str[0] == '\0') {
            _select_refresh_cached_selected_options_for_node((DomNode*)elem);
            return value;
        }

        // 3. Parse HTML fragment
        if (!doc || !doc->input) return value;

        Html5Parser* parser = html5_fragment_parser_create(
            doc->pool, doc->arena, doc->input);
        if (!parser) return value;

        html5_fragment_parse(parser, html_str);
        Element* body_elem = html5_fragment_get_body(parser);
        if (!body_elem) return value;

        // 4. Convert parsed Lambda Elements to DOM nodes and append
        for (int64_t i = 0; i < body_elem->length; i++) {
            TypeId type = get_type_id(body_elem->items[i]);
            if (type == LMD_TYPE_ELEMENT) {
                DomElement* child_dom = build_dom_tree_from_element(
                    body_elem->items[i].element, doc, nullptr);
                if (child_dom) {
                    dom_element_append_child(elem, child_dom);
                }
            } else if (type == LMD_TYPE_STRING) {
                String* s = it2s(body_elem->items[i]);
                if (s) {
                    dom_element_append_text(elem, s->chars);
                }
            }
        }

        log_debug("js_dom_set_property: set innerHTML on <%s>",
                  elem->tag_name ? elem->tag_name : "?");
        // Re-register element ids on the global object so HTML5 named-property
        // access on Window picks up dynamically inserted elements (the WPT
        // selection tests rely on `document.body.innerHTML = "<div id=foo>..."`
        // making `foo` a global).
        js_dom_register_named_elements(elem);
        _select_refresh_cached_selected_options_for_node((DomNode*)elem);
        js_dom_mutation_notify(DOM_JS_MUTATION_TREE_REPLACE, (DomNode*)elem, elem->parent);
        return value;
    }

    // ------------------------------------------------------------------
    // Boolean IDL setters that must update internal state, not just the
    // content attribute. `checked` writes the live "checkedness" flag
    // (HTML §4.10.5.3.21).
    // ------------------------------------------------------------------
    if (strcmp(prop, "checked") == 0 && _is_tag(elem, "input")) {
        _set_checkedness(elem, js_is_truthy(value));
        // Mark the dirty checkedness flag so subsequent `checked` content
        // attribute changes do not override the value.
        Item exp = expando_get_or_create_map((DomNode*)elem);
        if (exp.item != ITEM_NULL) {
            js_property_set(exp,
                (Item){.item = s2it(heap_create_name("__chkDirty"))},
                (Item){.item = b2it(true)});
        }
        return value;
    }

    // input.defaultChecked setter — reflects `checked` attribute. Per spec,
    // when the dirty checkedness flag is false, current checkedness also
    // updates to match the new default.
    if (strcmp(prop, "defaultChecked") == 0 && _is_tag(elem, "input")) {
        bool t = js_is_truthy(value);
        if (t) dom_element_set_attribute(elem, "checked", "");
        else   dom_element_remove_attribute(elem, "checked");
        Item exp = expando_get_map((DomNode*)elem);
        bool dirty = false;
        if (exp.item != ITEM_NULL) {
            Item v = js_property_get(exp,
                (Item){.item = s2it(heap_create_name("__chkDirty"))});
            dirty = v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
        }
        if (!dirty) _set_checkedness(elem, t);
        return value;
    }

    // ------------------------------------------------------------------
    // F-5: HTMLSelectElement / HTMLOptionElement IDL setters
    // ------------------------------------------------------------------
    if (_is_tag(elem, "select")) {
        if (strcmp(prop, "value") == 0) {
            const char* sv = fn_to_cstr(value);
            if (!sv) sv = "";
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            int64_t n = js_array_length(arr);
            int found = -1;
            for (int64_t i = 0; i < n; i++) {
                DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
                if (!opt) continue;
                char* v = _option_value(opt);
                bool match = v && strcmp(v, sv) == 0;
                mem_free(v);
                if (match) { found = (int)i; break; } // INT_CAST_OK: option index
            }
            // Per spec, set selectedness of all options accordingly.
            for (int64_t i = 0; i < n; i++) {
                DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
                if (!opt) continue;
                _set_selectedness(opt, found >= 0 && (int)i == found); // INT_CAST_OK: option index
            }
            _select_mark_dirty(elem);
            return value;
        }
        if (strcmp(prop, "selectedIndex") == 0) {
            _select_set_selected_index(elem, _select_index_from_item(value));
            return value;
        }
        if (strcmp(prop, "length") == 0) {
            // Setting length adjusts the option list. Increase: append
            // empty <option> children. Decrease: remove trailing options.
            int new_len = 0;
            { TypeId t = get_type_id(value);
              if (t == LMD_TYPE_INT) new_len = (int)it2i(value); // INT_CAST_OK: count
              else if (t == LMD_TYPE_FLOAT) {
                  double* d = (double*)(value.item & 0x00FFFFFFFFFFFFFF);
                  if (d) new_len = (int)*d; // INT_CAST_OK: count
              }
            }
            if (new_len < 0) new_len = 0;
            Item arr = js_array_new(0);
            _collect_options(elem->first_child, arr);
            int64_t cur = js_array_length(arr);
            if (new_len > cur) {
                int to_add = new_len - (int)cur; // INT_CAST_OK: count
                DomDocument* doc = elem->doc;
                for (int i = 0; i < to_add; i++) {
                    // Create a proper Lambda Element + DomElement so
                    // it has a backing native_element for attributes.
                    if (!doc || !doc->input) break;
                    MarkBuilder builder(doc->input);
                    Item nat_item = builder.element("option").final();
                    Element* nat = nat_item.element;
                    DomElement* opt = dom_element_create(doc, "option", nat);
                    if (!opt) break;
                    // append to elem
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
                // remove trailing options
                for (int64_t i = cur - 1; i >= new_len; i--) {
                    DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, i));
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
            js_dom_mutation_notify();
            return value;
        }
    }
    if (_is_tag(elem, "option")) {
        if (strcmp(prop, "selected") == 0) {
            bool selected = js_is_truthy(value);
            _set_selectedness(elem, selected);
            // Mark the option's dirty selectedness flag.
            Item exp = expando_get_or_create_map((DomNode*)elem);
            if (exp.item != ITEM_NULL) {
                js_property_set(exp,
                    (Item){.item = s2it(heap_create_name("__optDirty"))},
                    (Item){.item = b2it(true)});
            }
            // The option explicitly being selected wins for non-multiple
            // selects; do not let a later selected option in tree order undo it.
            DomElement* sel = _option_owner_select(elem);
            if (sel && selected && !dom_element_has_attribute(sel, "multiple")) {
                _select_select_only_option(sel, elem);
            } else if (sel) {
                _select_ask_for_reset(sel);
            }
            _select_refresh_cached_selected_options(sel);
            return value;
        }
        // option.defaultSelected setter — reflects `selected` attribute.
        // When the dirty selectedness flag is false, current selectedness
        // updates to match the new default.
        if (strcmp(prop, "defaultSelected") == 0) {
            bool t = js_is_truthy(value);
            if (t) dom_element_set_attribute(elem, "selected", "");
            else   dom_element_remove_attribute(elem, "selected");
            Item exp = expando_get_map((DomNode*)elem);
            bool dirty = false;
            if (exp.item != ITEM_NULL) {
                Item v = js_property_get(exp,
                    (Item){.item = s2it(heap_create_name("__optDirty"))});
                dirty = v.item != ITEM_NULL && !is_js_undefined(v) && js_is_truthy(v);
            }
            if (!dirty) {
                _set_selectedness(elem, t);
                DomElement* sel = _option_owner_select(elem);
                if (sel) _select_ask_for_reset(sel);
            }
            return value;
        }
        if (strcmp(prop, "value") == 0) {
            const char* sv = fn_to_cstr(value);
            dom_element_set_attribute(elem, "value", sv ? sv : "");
            return value;
        }
        if (strcmp(prop, "text") == 0) {
            // Setting text replaces the children with a single text node.
            const char* sv = fn_to_cstr(value);
            if (!sv) sv = "";
            // Detach existing children
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
            String* str = js_dom_create_document_string(elem->doc, sv, strlen(sv));
            DomText* tn = dom_text_create(str, elem);
            if (tn) {
                tn->parent = elem;
                elem->first_child = tn;
                elem->last_child = tn;
            }
            js_dom_mutation_notify();
            return value;
        }
        if (strcmp(prop, "defaultSelected") == 0) {
            if (js_is_truthy(value)) dom_element_set_attribute(elem, "selected", "");
            else dom_element_remove_attribute(elem, "selected");
            return value;
        }
    }
    if (strcmp(prop, "value") == 0 && _is_tag(elem, "input") && !tc_is_text_control_elem(elem)) {
        const char* s = fn_to_cstr(value);
        if (!s) s = "";
        dom_element_set_attribute(elem, "value", s);
        if (elem->form) {
            elem->form->value = dom_element_get_attribute(elem, "value");
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }

    // ------------------------------------------------------------------
    // Text-control IDL setters — must intercept before the generic
    // expando/attribute fallback. Per HTML §4.10.6.
    // ------------------------------------------------------------------
    if (tc_is_text_control_elem(elem)) {
        DocState* tc_st = js_dom_current_state();
        if (strcmp(prop, "value") == 0) {
            const char* s = fn_to_cstr(value);
            if (!s) s = "";
            // Per HTML spec §4.10.6: for single-line text controls (not textarea),
            // strip CR and LF from the value before storing.
            if (elem->tag_name && strcasecmp(elem->tag_name, "input") == 0) {
                const char* itype = _input_type_lower(elem);
                bool single_line = strcmp(itype, "text") == 0 || strcmp(itype, "search") == 0 ||
                                   strcmp(itype, "tel") == 0 || strcmp(itype, "url") == 0 ||
                                   strcmp(itype, "email") == 0 || strcmp(itype, "password") == 0;
                if (single_line) {
                    // strip CR and LF
                    size_t slen = strlen(s);
                    bool has_newline = false;
                    for (size_t k = 0; k < slen; k++) { if (s[k] == '\r' || s[k] == '\n') { has_newline = true; break; } }
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
                            _value_mark_dirty(elem);
                            return value;
                        }
                    }
                }
            }
            tc_set_value(elem, s, strlen(s));
            _value_mark_dirty(elem);
            return value;
        }
        if (strcmp(prop, "selectionStart") == 0) {
            tc_ensure_init(elem);
            int64_t v = it2i(value);
            if (v < 0) v = 0;
            uint32_t start = (uint32_t)v;
            uint32_t end = 0;
            uint8_t direction = 0;
            form_control_get_selection(tc_st, (View*)elem, NULL, &end, &direction);
            if (start > end) end = start;
            form_control_set_selection(tc_st, (View*)elem, start, end, direction);
            return value;
        }
        if (strcmp(prop, "selectionEnd") == 0) {
            tc_ensure_init(elem);
            int64_t v = it2i(value);
            if (v < 0) v = 0;
            uint32_t end = (uint32_t)v;
            uint32_t start = 0;
            uint8_t direction = 0;
            form_control_get_selection(tc_st, (View*)elem, &start, NULL, &direction);
            if (start > end) start = end;
            form_control_set_selection(tc_st, (View*)elem, start, end, direction);
            return value;
        }
        if (strcmp(prop, "selectionDirection") == 0) {
            tc_ensure_init(elem);
            const char* s = fn_to_cstr(value);
            uint8_t d = 0;
            if (s) {
                if (strcmp(s, "forward") == 0) d = 1;
                else if (strcmp(s, "backward") == 0) d = 2;
            }
            uint32_t start = 0;
            uint32_t end = 0;
            form_control_get_selection(tc_st, (View*)elem, &start, &end, NULL);
            form_control_set_selection(tc_st, (View*)elem, start, end, d);
            return value;
        }
        if (strcmp(prop, "defaultValue") == 0) {
            const char* s = fn_to_cstr(value);
            if (!s) s = "";
            if (elem->tag_name && strcasecmp(elem->tag_name, "textarea") == 0) {
                // textarea.defaultValue setter: replace descendant text
                // content with a single text node holding the new value.
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
                if (*s) {
                    String* str = js_dom_create_document_string(elem->doc, s, strlen(s));
                    DomText* tn = dom_text_create(str, elem);
                    if (tn) {
                        tn->parent = elem;
                        elem->first_child = tn;
                        elem->last_child = tn;
                    }
                }
                // Per spec: API value updates only when dirty flag is false.
                if (!_value_is_dirty(elem)) {
                    tc_set_value(elem, s, strlen(s));
                }
                js_dom_mutation_notify(DOM_JS_MUTATION_TREE_REPLACE, (DomNode*)elem, elem->parent);
                return value;
            }
            // input.defaultValue setter: reflects "value" attribute.
            dom_element_set_attribute(elem, "value", s);
            if (!_value_is_dirty(elem)) {
                tc_set_value(elem, s, strlen(s));
            }
            return value;
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
                dom_element_set_attribute(elem, attr, "");
            } else {
                dom_element_remove_attribute(elem, attr);
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
            dom_element_set_attribute(elem, int_attr, buf);
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }

        // String reflection with IDL→HTML name mapping (readOnly → readonly,
        // formAction → formaction, htmlFor → for, etc.).
        const char* mapped_attr = _idl_to_attr_name(prop);
        if (mapped_attr) {
            const char* s = fn_to_cstr(value);
            if (s) {
                dom_element_set_attribute(elem, mapped_attr, s);
            } else {
                dom_element_remove_attribute(elem, mapped_attr);
            }
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }

        // <input>.type setter — lowercase, fall back to "text" for unknown.
        if (strcmp(prop, "type") == 0 && _is_tag(elem, "input")) {
            const char* s = fn_to_cstr(value);
            if (s && *s) {
                char buf[32];
                size_t i = 0;
                for (; s[i] && i < sizeof(buf) - 1; i++)
                    buf[i] = (char)tolower((unsigned char)s[i]);
                buf[i] = '\0';
                dom_element_set_attribute(elem, "type", buf);
            } else {
                dom_element_set_attribute(elem, "type", "text");
            }
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
            return value;
        }
    }

    // generic property set — store as expando AND as HTML attribute when possible
    // This allows `element.myProp = someObj` to be stored and retrieved as JS values,
    // while still reflecting string/bool values to the DOM attributes.
    TypeId val_type = get_type_id(value);

    // always store in expando map for JS-level retrieval
    {
        Item exp_map = expando_get_or_create_map((DomNode*)elem);
        if (exp_map.item != ITEM_NULL) {
            Item key = (Item){.item = s2it(heap_create_name(prop))};
            js_property_set(exp_map, key, value);
        }
    }

    // also reflect to HTML attributes for string/bool/number values
    if (val_type == LMD_TYPE_BOOL) {
        bool is_true = (value.item & 0xFF) != 0;
        if (is_true) {
            dom_element_set_attribute(elem, prop, "");
        } else {
            dom_element_remove_attribute(elem, prop);
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return value;
    }
    char nbuf[64];
    const char* val_str = nullptr;
    if (val_type == LMD_TYPE_INT) {
        snprintf(nbuf, sizeof(nbuf), "%lld", (long long)it2i(value));
        val_str = nbuf;
    } else if (val_type == LMD_TYPE_FLOAT) {
        double dv = it2d(value);
        long long iv = (long long)dv;
        if ((double)iv == dv) snprintf(nbuf, sizeof(nbuf), "%lld", iv);
        else snprintf(nbuf, sizeof(nbuf), "%g", dv);
        val_str = nbuf;
    } else {
        val_str = fn_to_cstr(value);
    }
    if (val_str && *val_str) {
        dom_element_set_attribute(elem, prop, val_str);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
    }
    return value;
}

extern "C" Item js_dom_set_style_property(Item elem_item, Item prop_name, Item value) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // not a DOM element — fall back to normal property set on obj.style
        Item style_key = (Item){.item = s2it(heap_create_name("style"))};
        Item style_obj = js_property_get(elem_item, style_key);
        if (style_obj.item != ITEM_NULL && get_type_id(style_obj) == LMD_TYPE_MAP) {
            return js_property_set(style_obj, prop_name, value);
        }
        return ItemNull;
    }

    const char* js_prop = fn_to_cstr(prop_name);
    const char* val_str = fn_to_cstr(value);
    if (!js_prop || !val_str) return ItemNull;

    // convert camelCase JS property to CSS property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    // handle cssText special case: replace entire inline style
    if (strcmp(css_prop, "cssText") == 0) {
        dom_element_remove_inline_styles(elem);
        if (val_str[0]) {
            dom_element_apply_inline_style(elem, val_str);
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_STYLE, (DomNode*)elem, elem->parent);
        log_debug("js_dom_set_style_property: set cssText='%.50s' on <%s>",
                  val_str, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // CSSOM §6.7.3: setting a property to empty string removes it
    if (!val_str[0]) {
        CssPropertyId prop_id = css_property_id_from_name(css_prop);
        if (prop_id != CSS_PROPERTY_UNKNOWN && elem->specified_style) {
            style_tree_remove_property(elem->specified_style, prop_id);
            elem->styles_resolved = false;
            js_dom_mutation_notify(js_dom_style_mutation_kind(prop_id),
                                   (DomNode*)elem, elem->parent);
        }
        log_debug("js_dom_set_style_property: removed %s (CSS: %s) on <%s>",
                  js_prop, css_prop, elem->tag_name ? elem->tag_name : "?");
        return value;
    }

    // build a single-declaration inline style string: "property: value"
    char style_decl[256];
    snprintf(style_decl, sizeof(style_decl), "%s: %s", css_prop, val_str);

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
    dom_element_apply_inline_style(elem, style_decl);
    elem->styles_resolved = false;  // mark for re-cascading
    CssPropertyId prop_id = css_property_id_from_name(css_prop);
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
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // not a DOM element — fall back to normal property access on obj.style
        Item style_key = (Item){.item = s2it(heap_create_name("style"))};
        Item style_obj = js_property_get(elem_item, style_key);
        if (style_obj.item != ITEM_NULL && get_type_id(style_obj) == LMD_TYPE_MAP) {
            return js_property_get(style_obj, prop_name);
        }
        return (Item){.item = s2it(heap_create_name(""))};
    }

    const char* js_prop = fn_to_cstr(prop_name);
    if (!js_prop) return (Item){.item = s2it(heap_create_name(""))};

    // convert camelCase JS property to CSS property
    char css_prop[128];
    js_camel_to_css_prop(js_prop, css_prop, sizeof(css_prop));

    // v12: cssText getter — return the raw inline style string
    if (strcmp(css_prop, "cssText") == 0) {
        const char* inline_style = dom_element_get_inline_style(elem);
        return (Item){.item = s2it(heap_create_name(inline_style ? inline_style : ""))};
    }

    // look up the CSS property ID
    CssPropertyId prop_id = css_property_id_from_name(css_prop);
    if (prop_id == CSS_PROPERTY_UNKNOWN) {
        log_debug("js_dom_get_style_property: unknown CSS property '%s'", css_prop);
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // get the specified value for this property
    CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
    if (!decl || !decl->value) {
        // shorthand fallback: if the property is a shorthand (e.g. padding, margin),
        // try the first longhand (e.g. padding-top) since shorthands are expanded
        if (css_property_is_shorthand(prop_id)) {
            char longhand[128];
            snprintf(longhand, sizeof(longhand), "%s-top", css_prop);
            CssPropertyId lh_id = css_property_id_from_name(longhand);
            if (lh_id != CSS_PROPERTY_UNKNOWN) {
                decl = dom_element_get_specified_value(elem, lh_id);
            }
        }
        if (!decl || !decl->value) {
            return (Item){.item = s2it(heap_create_name(""))};
        }
    }

    // only return values that came from inline styles (element.style.X should
    // only reflect inline styles, not stylesheet rules)
    if (!decl->specificity.inline_style) {
        return (Item){.item = s2it(heap_create_name(""))};
    }

    // convert the CSS value back to a string
    CssValue* val = decl->value;
    switch (val->type) {
        case CSS_VALUE_TYPE_KEYWORD: {
            const CssEnumInfo* info = css_enum_info(val->data.keyword);
            if (info && info->name) {
                return (Item){.item = s2it(heap_create_name(info->name))};
            }
            break;
        }
        case CSS_VALUE_TYPE_LENGTH: {
            char buf[64];
            const char* unit_str = "";
            switch (val->data.length.unit) {
                case CSS_UNIT_PX: unit_str = "px"; break;
                case CSS_UNIT_EM: unit_str = "em"; break;
                case CSS_UNIT_REM: unit_str = "rem"; break;
                case CSS_UNIT_PERCENT: unit_str = "%"; break;
                case CSS_UNIT_VW: unit_str = "vw"; break;
                case CSS_UNIT_VH: unit_str = "vh"; break;
                case CSS_UNIT_CM: unit_str = "cm"; break;
                case CSS_UNIT_MM: unit_str = "mm"; break;
                case CSS_UNIT_IN: unit_str = "in"; break;
                case CSS_UNIT_PT: unit_str = "pt"; break;
                case CSS_UNIT_PC: unit_str = "pc"; break;
                default: unit_str = "px"; break;
            }
            double v = val->data.length.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%s", (int)v, unit_str);
            } else {
                snprintf(buf, sizeof(buf), "%g%s", v, unit_str);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_PERCENTAGE: {
            char buf[64];
            double v = val->data.percentage.value;
            if (v == (int)v) {
                snprintf(buf, sizeof(buf), "%d%%", (int)v);
            } else {
                snprintf(buf, sizeof(buf), "%g%%", v);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_NUMBER: {
            char buf[64];
            if (val->data.number.is_integer) {
                snprintf(buf, sizeof(buf), "%d", (int)val->data.number.value);
            } else {
                snprintf(buf, sizeof(buf), "%g", val->data.number.value);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case CSS_VALUE_TYPE_STRING:
            if (val->data.string) {
                return (Item){.item = s2it(heap_create_name(val->data.string))};
            }
            break;
        case CSS_VALUE_TYPE_CUSTOM:
            if (val->data.custom_property.name) {
                return (Item){.item = s2it(heap_create_name(val->data.custom_property.name))};
            }
            break;
        default:
            break;
    }

    return (Item){.item = s2it(heap_create_name(""))};
}

// ============================================================================
// Element Method Dispatcher
// ============================================================================

extern "C" Item js_dom_element_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(elem_item);
    if (!node) {
        log_error("js_dom_element_method: not a DOM element");
        return ItemNull;
    }
    DomElement* elem = node->as_element(); // may be nullptr for text/comment nodes

    const char* method = fn_to_cstr(method_name);
    if (!method) {
        log_error("js_dom_element_method: invalid method name");
        return ItemNull;
    }

    log_debug("js_dom_element_method: '%s' on <%s>", method,
              node->node_name() ? node->node_name() : "?");

    // v12b: remove() — self-removal from parent (works on any node type)
    if (strcmp(method, "remove") == 0) {
        if (node->parent) {
            DomElement* owner_select = nullptr;
            if (node->is_element() && node->as_element()->tag() == HTM_TAG_OPTION) {
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
    if (strcmp(method, "contains") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        return js_dom_contains(elem_item, args[0]);
    }

    // CharacterData.replaceData(offset, count, data) — text nodes only.
    if (node->is_text() && strcmp(method, "replaceData") == 0) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item count_arg = argc >= 2 ? args[1] : make_js_undefined();
        Item data_arg = argc >= 3 ? args[2] : make_js_undefined();
        return js_dom_text_replace_data_method(node->as_text(), offset_arg, count_arg, data_arg);
    }
    if (node->is_text() && strcmp(method, "insertData") == 0) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item data_arg = argc >= 2 ? args[1] : make_js_undefined();
        return js_dom_text_insert_data_method(node->as_text(), offset_arg, data_arg);
    }
    if (node->is_text() && strcmp(method, "appendData") == 0) {
        Item data_arg = argc >= 1 ? args[0] : make_js_undefined();
        return js_dom_text_append_data_method(node->as_text(), data_arg);
    }
    if (node->is_text() && strcmp(method, "deleteData") == 0) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item count_arg = argc >= 2 ? args[1] : make_js_undefined();
        return js_dom_text_delete_data_method(node->as_text(), offset_arg, count_arg);
    }
    if (node->is_text() && strcmp(method, "substringData") == 0) {
        Item offset_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item count_arg = argc >= 2 ? args[1] : make_js_undefined();
        return js_dom_text_substring_data_method(node->as_text(), offset_arg, count_arg);
    }

    // All remaining methods require an element node
    if (!elem) {
        log_debug("js_dom_element_method: '%s' called on non-element node, ignored", method);
        return ItemNull;
    }

    // attachShadow(init) -> lightweight DocumentFragment-backed ShadowRoot.
    // Radiant does not render a full shadow tree yet, but WPT focus/editing
    // tests need a stable root object that supports appendChild/activeElement
    // while light DOM stays addressable.
    if (strcmp(method, "attachShadow") == 0) {
        const char* mode = "open";
        bool delegates_focus = false;
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
            Item mode_item = js_property_get(args[0],
                (Item){.item = s2it(heap_create_name("mode"))});
            const char* mode_text = fn_to_cstr(mode_item);
            if (mode_text && mode_text[0]) mode = mode_text;
            Item delegates_item = js_property_get(args[0],
                (Item){.item = s2it(heap_create_name("delegatesFocus"))});
            delegates_focus = js_is_truthy(delegates_item);
        }

        MarkBuilder builder(elem->doc ? elem->doc->input : nullptr);
        Item frag_item = builder.element("#document-fragment").final();
        Element* frag_elem = frag_item.element;
        DomElement* frag = dom_element_create(elem->doc, "#document-fragment", frag_elem);
        Item root = js_dom_wrap_element(frag);

        js_property_set(root, (Item){.item = s2it(heap_create_name("host"))}, elem_item);
        js_property_set(root, (Item){.item = s2it(heap_create_name("mode"))},
            (Item){.item = s2it(heap_create_name(mode))});
        js_property_set(root, (Item){.item = s2it(heap_create_name("innerHTML"))},
            (Item){.item = s2it(heap_create_name(""))});
        js_property_set(root, (Item){.item = s2it(heap_create_name("nodeType"))},
            (Item){.item = i2it(11)});
        js_property_set(root, (Item){.item = s2it(heap_create_name("delegatesFocus"))},
            (Item){.item = b2it(delegates_focus)});

        Item exp_map = expando_get_or_create_map((DomNode*)elem);
        if (exp_map.item != ITEM_NULL) {
            Item visible_root = (strcasecmp(mode, "closed") == 0) ? ItemNull : root;
            js_property_set(exp_map,
                (Item){.item = s2it(heap_create_name("shadowRoot"))},
                visible_root);
            js_property_set(exp_map,
                (Item){.item = s2it(heap_create_name("__shadowRootInternal"))},
                root);
        }
        return root;
    }

    // getAttribute(name) → string or null
    if (strcmp(method, "getAttribute") == 0) {
        if (argc < 1) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return ItemNull;
        const char* val = dom_element_get_attribute(elem, attr_name);
        if (val) return (Item){.item = s2it(heap_create_name(val))};
        if (dom_element_has_attribute(elem, attr_name))
            return (Item){.item = s2it(heap_create_name(""))};
        return ItemNull;
    }

    // setAttribute(name, value)
    if (strcmp(method, "setAttribute") == 0) {
        if (argc < 2) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        const char* attr_val = js_dom_to_attr_cstr(args[1]);
        if (!attr_name || !attr_val) return ItemNull;
        dom_element_set_attribute(elem, attr_name, attr_val);
        js_dom_compile_event_attr_to_expando(elem, attr_name, attr_val);
        if (_is_tag(elem, "option") && strcasecmp(attr_name, "selected") == 0) {
            DomElement* sel = _nearest_select_for_node((DomNode*)elem);
            if (sel && !dom_element_has_attribute(sel, "multiple")) _select_ask_for_reset(sel);
        }
        _select_refresh_cached_selected_options_for_node((DomNode*)elem);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return ItemNull;
    }

    // hasAttribute(name) → boolean
    if (strcmp(method, "hasAttribute") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return (Item){.item = ITEM_FALSE};
        bool has = dom_element_has_attribute(elem, attr_name);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // removeAttribute(name)
    if (strcmp(method, "removeAttribute") == 0) {
        if (argc < 1) return ItemNull;
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return ItemNull;
        dom_element_remove_attribute(elem, attr_name);
        js_dom_clear_event_attr_expando(elem, attr_name);
        if (_is_tag(elem, "select") && strcasecmp(attr_name, "multiple") == 0) {
            _select_ask_for_reset(elem);
        }
        _select_refresh_cached_selected_options_for_node((DomNode*)elem);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return ItemNull;
    }

    // getElementsByTagName(tagName) — descendants of this element
    if (strcmp(method, "getElementsByTagName") == 0) {
        if (argc < 1) return ItemNull;
        const char* tag = fn_to_cstr(args[0]);
        if (!tag) return ItemNull;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        dom_find_descendants_by_tag(elem, tag, arr);
        return (Item){.array = arr};
    }

    // getElementsByClassName(className) — descendants of this element
    if (strcmp(method, "getElementsByClassName") == 0) {
        if (argc < 1) return ItemNull;
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return ItemNull;
        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;
        dom_find_descendants_by_class(elem, cls, arr);
        return (Item){.array = arr};
    }

    // querySelector(selector) — from this element
    if (strcmp(method, "querySelector") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) return ItemNull;

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement* found = selector_matcher_find_first(matcher, selector, elem);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // querySelectorAll(selector) — from this element
    if (strcmp(method, "querySelectorAll") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);

        Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        arr->type_id = LMD_TYPE_ARRAY;
        arr->items = nullptr;
        arr->length = 0;
        arr->capacity = 0;

        if (!selector) return (Item){.array = arr};

        SelectorMatcher* matcher = selector_matcher_create(pool);
        DomElement** results = nullptr;
        int count = 0;
        selector_matcher_find_all(matcher, selector, elem, &results, &count);
        for (int i = 0; i < count; i++) {
            array_push(arr, js_dom_wrap_element(results[i]));
        }
        return (Item){.array = arr};
    }

    // matches(selector) → boolean
    if (strcmp(method, "matches") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return (Item){.item = ITEM_FALSE};

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) return (Item){.item = ITEM_FALSE};

        SelectorMatcher* matcher = selector_matcher_create(pool);
        MatchResult result;
        bool matched = selector_matcher_matches(matcher, selector, elem, &result);
        return (Item){.item = b2it(matched ? 1 : 0)};
    }

    // closest(selector) → element or null
    if (strcmp(method, "closest") == 0) {
        if (argc < 1) return ItemNull;
        const char* sel_text = fn_to_cstr(args[0]);
        if (!sel_text || !elem->doc) return ItemNull;

        Pool* pool = elem->doc->pool;
        CssSelector* selector = parse_css_selector(sel_text, pool);
        if (!selector) return ItemNull;

        SelectorMatcher* matcher = selector_matcher_create(pool);
        MatchResult mresult;
        DomElement* current = elem;
        while (current) {
            if (selector_matcher_matches(matcher, selector, current, &mresult)) {
                return js_dom_wrap_element(current);
            }
            DomNode* parent = current->parent;
            current = (parent && parent->is_element()) ? parent->as_element() : nullptr;
        }
        return ItemNull;
    }

    // appendChild(child) — appends a child element or text node
    if (strcmp(method, "appendChild") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!child_node) {
            log_error("js_dom_element_method appendChild: argument is not a DOM node");
            return ItemNull;
        }
        // v12b: DocumentFragment support — move children instead of the fragment itself
        if (child_node->is_element()) {
            DomElement* child_elem = child_node->as_element();
            if (child_elem->tag_name && strcmp(child_elem->tag_name, "#document-fragment") == 0) {
                DomNode* frag_child = child_elem->first_child;
                while (frag_child) {
                    DomNode* next = frag_child->next_sibling;
                    dom_pre_remove(frag_child);
                    child_elem->remove_child(frag_child);
                    ((DomNode*)elem)->append_child(frag_child);
                    dom_post_insert((DomNode*)elem, frag_child);
                    frag_child = next;
                }
                js_dom_mutation_notify();
                return args[0];
            }
        }
        // detach child from current parent if any
        if (child_node->parent) {
            DomNode* old_parent = child_node->parent;
            if (old_parent->is_element()) {
                dom_pre_remove(child_node);
                old_parent->remove_child(child_node);
            }
        }
        // use DomNode::append_child which handles all node types
        ((DomNode*)elem)->append_child(child_node);
        dom_post_insert((DomNode*)elem, child_node);
        if (child_node->is_element() && child_node->as_element()->tag() == HTM_TAG_OPTION &&
            elem->tag() == HTM_TAG_SELECT) {
            _select_ask_for_reset(elem);
        }
        _select_refresh_cached_selected_options_for_node((DomNode*)elem);
        js_dom_mutation_notify();
        // If we just inserted an <iframe>, queue its synthetic load event.
        if (child_node->is_element()) {
            DomElement* ce = child_node->as_element();
            if (ce->tag_name && strcmp(ce->tag_name, "iframe") == 0) {
                _schedule_iframe_load(ce);
            }
        }
        return args[0];  // return the appended child
    }

    // removeChild(child) — removes a child node (element or text)
    if (strcmp(method, "removeChild") == 0) {
        if (argc < 1) return ItemNull;
        DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[0]);
        if (!child_node) {
            log_error("js_dom_element_method removeChild: argument is not a DOM node");
            return ItemNull;
        }
        dom_pre_remove(child_node);
        ((DomNode*)elem)->remove_child(child_node);
        if (child_node->is_element() && child_node->as_element()->tag() == HTM_TAG_OPTION &&
            elem->tag() == HTM_TAG_SELECT) {
            _select_ask_for_reset(elem);
        }
        js_dom_mutation_notify();
        return args[0];  // return the removed child
    }

    // insertBefore(newChild, refChild)
    if (strcmp(method, "insertBefore") == 0) {
        if (argc < 2) return ItemNull;
        DomNode* new_child = (DomNode*)js_dom_unwrap_element(args[0]);
        DomNode* ref_child = (DomNode*)js_dom_unwrap_element(args[1]);
        if (!new_child) return ItemNull;
        // v12b: DocumentFragment support — move children instead of the fragment itself
        if (new_child->is_element()) {
            DomElement* new_elem = new_child->as_element();
            if (new_elem->tag_name && strcmp(new_elem->tag_name, "#document-fragment") == 0) {
                DomNode* frag_child = new_elem->first_child;
                while (frag_child) {
                    DomNode* next = frag_child->next_sibling;
                    dom_pre_remove(frag_child);
                    new_elem->remove_child(frag_child);
                    ((DomNode*)elem)->insert_before(frag_child, ref_child);
                    dom_post_insert((DomNode*)elem, frag_child);
                    frag_child = next;
                }
                js_dom_mutation_notify();
                return args[0];
            }
        }
        // detach from old parent
        if (new_child->parent) {
            dom_pre_remove(new_child);
            new_child->parent->remove_child(new_child);
        }
        ((DomNode*)elem)->insert_before(new_child, ref_child);
        dom_post_insert((DomNode*)elem, new_child);
        js_dom_mutation_notify();
        return args[0];
    }

    // hasChildNodes() → boolean
    if (strcmp(method, "hasChildNodes") == 0) {
        bool has = (elem->first_child != nullptr);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // normalize() — merge adjacent text nodes
    if (strcmp(method, "normalize") == 0) {
        DomNode* child = elem->first_child;
        while (child) {
            if (child->is_text()) {
                DomText* text = child->as_text();
                // merge consecutive text nodes
                while (child->next_sibling && child->next_sibling->is_text()) {
                    DomText* next_text = child->next_sibling->as_text();
                    uint32_t head_u16  = dom_text_utf16_length(text);
                    uint32_t tail_u16  = dom_text_utf16_length(next_text);
                    // concatenate text content
                    size_t new_len = text->length + next_text->length;
                    char* combined = (char*)pool_alloc(elem->doc->pool, new_len + 1);
                    if (text->text && text->length > 0)
                        memcpy(combined, text->text, text->length);
                    if (next_text->text && next_text->length > 0)
                        memcpy(combined + text->length, next_text->text, next_text->length);
                    combined[new_len] = '\0';
                    // update main text node
                    String* s = js_dom_create_document_string(elem->doc, combined, new_len);
                    if (!s) break;
                    text->native_string = s;
                    text->text = s->chars;
                    text->length = new_len;
                    // Spec: ranges with (next_text, k) move to (text, head_u16 + k);
                    // appended-data shift handled separately.
                    {
                        DocState* st = js_dom_current_state();
                        if (st) {
                            // (a) shift any existing endpoints in `text` past head_u16 by tail_u16
                            dom_mutation_text_replace_data(st, text, head_u16, 0, tail_u16);
                            // (b) retarget endpoints inside `next_text` into the merged `text`
                            dom_mutation_text_merge(st, text, next_text, head_u16);
                        }
                    }
                    // remove the next text node
                    DomNode* remove_node = child->next_sibling;
                    dom_pre_remove(remove_node);
                    ((DomNode*)elem)->remove_child(remove_node);
                }
            }
            child = child->next_sibling;
        }
        return ItemNull;
    }

    // cloneNode(deep) — clone element (and optionally children)
    if (strcmp(method, "cloneNode") == 0) {
        bool deep = (argc > 0) ? js_is_truthy(args[0]) : false;
        // create a new element with its own independent native_element.
        // Passing elem->native_element causes a shallow copy that shares the
        // data buffer; a subsequent removeAttribute on the clone would free
        // that shared buffer and leave the original with a dangling pointer.
        // Instead, create a clean element via MarkBuilder (same approach as
        // createElement) and then copy each content attribute individually.
        MarkBuilder _clone_builder(elem->doc->input);
        Item _clean_elem = _clone_builder.element(elem->tag_name).final();
        DomElement* clone = dom_element_create(elem->doc, elem->tag_name, _clean_elem.element);
        if (!clone) return ItemNull;
        // copy all content attributes from original to clone (per DOM spec §4.6)
        if (elem->native_element) {
            int attr_count = 0;
            const char** attr_names = dom_element_get_attribute_names(elem, &attr_count);
            for (int _ai = 0; _ai < attr_count; _ai++) {
                const char* aname = attr_names[_ai];
                const char* aval  = dom_element_get_attribute(elem, aname);
                if (aval) dom_element_set_attribute(clone, aname, aval);
            }
        }
        // also copy IDL-set attributes stored in expando that may not be in
        // native_element yet (e.g. ele.type="url" on a fresh createElement)
        {
            Item orig_expando = expando_get_map((DomNode*)elem);
            if (orig_expando.item != ITEM_NULL) {
                Item clone_expando = expando_get_or_create_map((DomNode*)clone);
                if (clone_expando.item != ITEM_NULL) {
                    // walk the expando map's shape to copy string/scalar entries
                    Map* em = orig_expando.map;
                    if (em && em->type) {
                        TypeMap* em_type = (TypeMap*)em->type;
                        ShapeEntry* se = em_type->shape;
                        while (se) {
                            if (se->name && se->name->str) {
                                const char* ek = se->name->str;
                                Item ev = js_property_get(orig_expando,
                                    (Item){.item = s2it(heap_create_name(ek))});
                                if (!is_js_undefined(ev)) {
                                    js_property_set(clone_expando,
                                        (Item){.item = s2it(heap_create_name(ek))}, ev);
                                }
                            }
                            se = se->next;
                        }
                    }
                }
            }
        }
        // copy id and class
        clone->id = elem->id;
        clone->class_names = elem->class_names;
        clone->class_count = elem->class_count;
        clone->tag_id = elem->tag_id;
        // deep clone: recursively clone children
        if (deep) {
            DomNode* child = elem->first_child;
            while (child) {
                if (child->is_element()) {
                    // recursively clone child element
                    Item child_wrapped = js_dom_wrap_element(child->as_element());
                    Item child_clone = js_dom_element_method(child_wrapped, method_name, args, argc);
                    DomNode* cloned_child = (DomNode*)js_dom_unwrap_element(child_clone);
                    if (cloned_child) {
                        ((DomNode*)clone)->append_child(cloned_child);
                    }
                } else if (child->is_text()) {
                    // clone text node
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

    // v12b: replaceChild(newChild, oldChild)
    if (strcmp(method, "replaceChild") == 0) {
        if (argc < 2) return ItemNull;
        DomNode* new_child = (DomNode*)js_dom_unwrap_element(args[0]);
        DomNode* old_child = (DomNode*)js_dom_unwrap_element(args[1]);
        if (!new_child || !old_child) return ItemNull;
        if (old_child->parent != (DomNode*)elem) return ItemNull;
        // Self-replace: per DOM §4.2.3 the algorithm still performs an adopt +
        // remove + insert sequence. For live ranges that means a boundary
        // anchored inside the node collapses to (container, indexOf(node)).
        if (new_child == old_child) {
            dom_pre_remove(old_child);
            // Tree shape unchanged; the call is otherwise a no-op.
            return args[1];
        }
        // detach new_child from its current parent
        if (new_child->parent) {
            dom_pre_remove(new_child);
            new_child->parent->remove_child(new_child);
        }
        // insert new before old, then remove old
        ((DomNode*)elem)->insert_before(new_child, old_child);
        dom_post_insert((DomNode*)elem, new_child);
        dom_pre_remove(old_child);
        ((DomNode*)elem)->remove_child(old_child);
        return args[1]; // return removed old child
    }

    // replaceWith(...nodes) — replace this node in its parent's children with
    // the given nodes (or strings, coerced to text). Per DOM §4.2.7 "ChildNode".
    // Per spec this performs a removal + (pre-)insert, which is observably
    // distinct from a no-op for live ranges anchored inside `node` even when
    // the only argument is `node` itself.
    if (strcmp(method, "replaceWith") == 0) {
        DomNode* parent = node->parent;
        if (!parent) return ItemNull;
        // Compute the viable next sibling: first following sibling that is
        // not among the replacement nodes (pre-spec algorithm).
        DomNode* viable_next = node->next_sibling;
        for (;;) {
            bool in_args = false;
            if (!viable_next) break;
            for (int i = 0; i < argc; i++) {
                if ((DomNode*)js_dom_unwrap_element(args[i]) == viable_next) {
                    in_args = true; break;
                }
            }
            if (!in_args) break;
            viable_next = viable_next->next_sibling;
        }
        // Detach the receiver first; this collapses any live-range endpoint
        // that lived inside `node` to (parent, indexOf(node)).
        dom_pre_remove(node);
        parent->remove_child(node);
        // Insert each argument in order. Skip nulls / non-DOM args.
        for (int i = 0; i < argc; i++) {
            DomNode* a = (DomNode*)js_dom_unwrap_element(args[i]);
            if (!a) continue;
            if (a->parent) {
                dom_pre_remove(a);
                a->parent->remove_child(a);
            }
            if (viable_next && viable_next->parent == parent) {
                parent->insert_before(a, viable_next);
            } else {
                parent->append_child(a);
            }
            dom_post_insert(parent, a);
        }
        js_dom_mutation_notify();
        return ItemNull;
    }

    // v12b: toggleAttribute(name [, force])
    if (strcmp(method, "toggleAttribute") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* attr_name = fn_to_cstr(args[0]);
        if (!attr_name) return (Item){.item = ITEM_FALSE};

        bool has = dom_element_has_attribute(elem, attr_name);
        bool should_have;
        if (argc >= 2) {
            should_have = js_is_truthy(args[1]);
        } else {
            should_have = !has; // toggle
        }

        if (should_have && !has) {
            dom_element_set_attribute(elem, attr_name, "");
        } else if (!should_have && has) {
            dom_element_remove_attribute(elem, attr_name);
            if (_is_tag(elem, "select") && strcasecmp(attr_name, "multiple") == 0) {
                _select_ask_for_reset(elem);
            }
        }
        if (should_have != has) {
            js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        }
        return (Item){.item = b2it(should_have ? 1 : 0)};
    }

    // v12b: insertAdjacentElement(position, newElement)
    if (strcmp(method, "insertAdjacentElement") == 0) {
        if (argc < 2) return ItemNull;
        const char* position = fn_to_cstr(args[0]);
        DomNode* new_node = (DomNode*)js_dom_unwrap_element(args[1]);
        if (!position || !new_node) return ItemNull;

        // detach from old parent
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
        return args[1]; // return the inserted element
    }

    // v12b: insertAdjacentHTML(position, text)
    if (strcmp(method, "insertAdjacentHTML") == 0) {
        if (argc < 2) return ItemNull;
        const char* position = fn_to_cstr(args[0]);
        const char* html_str = fn_to_cstr(args[1]);
        if (!position || !html_str || !elem->doc) return ItemNull;

        DomDocument* doc = elem->doc;
        if (!doc->input) return ItemNull;

        // parse the HTML fragment
        Html5Parser* parser = html5_fragment_parser_create(
            doc->pool, doc->arena, doc->input);
        if (!parser) return ItemNull;
        html5_fragment_parse(parser, html_str);
        Element* body_elem = html5_fragment_get_body(parser);
        if (!body_elem) return ItemNull;

        // determine target parent and reference node based on position
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
            log_error("insertAdjacentHTML: invalid position '%s'", position);
            return ItemNull;
        }

        // build DOM nodes from parsed fragment and insert
        for (int64_t i = 0; i < body_elem->length; i++) {
            TypeId type = get_type_id(body_elem->items[i]);
            if (type == LMD_TYPE_ELEMENT) {
                DomElement* child_dom = build_dom_tree_from_element(
                    body_elem->items[i].element, doc, nullptr);
                if (child_dom) {
                    if (ref_node)
                        ((DomNode*)target_parent)->insert_before((DomNode*)child_dom, ref_node);
                    else
                        ((DomNode*)target_parent)->append_child((DomNode*)child_dom);
                }
            } else if (type == LMD_TYPE_STRING) {
                String* s = it2s(body_elem->items[i]);
                DomText* text_node = dom_text_create_detached(s, doc);
                if (text_node) {
                    if (ref_node)
                        ((DomNode*)target_parent)->insert_before((DomNode*)text_node, ref_node);
                    else
                        ((DomNode*)target_parent)->append_child((DomNode*)text_node);
                }
            }
        }
        return ItemNull;
    }

    // EventTarget interface
    if (strcmp(method, "addEventListener") == 0) {
        if (argc >= 2) {
            js_dom_add_event_listener(elem_item, args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "removeEventListener") == 0) {
        if (argc >= 2) {
            js_dom_remove_event_listener(elem_item, args[0], args[1], argc > 2 ? args[2] : ItemNull);
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (strcmp(method, "dispatchEvent") == 0) {
        if (argc >= 1) {
            return js_dom_dispatch_event(elem_item, args[0]);
        }
        return (Item){.item = ITEM_FALSE};
    }

    // getBoundingClientRect() — returns {top, left, right, bottom, width, height}
    // Walks parent chain to compute absolute position.
    if (strcmp(method, "getBoundingClientRect") == 0) {
        float abs_x = 0, abs_y = 0;
        DomNode* n = (DomNode*)elem;
        while (n) {
            abs_x += n->x;
            abs_y += n->y;
            n = n->parent;
        }
        float w = elem->width;
        float h = elem->height;

        Item rect = js_new_object();
        Item k;
        k = (Item){.item = s2it(heap_create_name("x"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_x)});
        k = (Item){.item = s2it(heap_create_name("y"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_y)});
        k = (Item){.item = s2it(heap_create_name("top"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_y)});
        k = (Item){.item = s2it(heap_create_name("left"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_x)});
        k = (Item){.item = s2it(heap_create_name("right"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)(abs_x + w))});
        k = (Item){.item = s2it(heap_create_name("bottom"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)(abs_y + h))});
        k = (Item){.item = s2it(heap_create_name("width"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)w)});
        k = (Item){.item = s2it(heap_create_name("height"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)h)});
        return rect;
    }

    // compareDocumentPosition(otherNode) — returns bitmask per W3C DOM spec
    if (strcmp(method, "compareDocumentPosition") == 0) {
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
    if (strcmp(method, "append") == 0) {
        for (int i = 0; i < argc; i++) {
            DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[i]);
            if (child_node) {
                // detach from current parent if any
                if (child_node->parent) child_node->parent->remove_child(child_node);
                ((DomNode*)elem)->append_child(child_node);
            } else {
                // string arg → create text node
                const char* text = fn_to_cstr(args[i]);
                if (text) {
                    String* s = js_dom_create_document_string(elem->doc, text, strlen(text));
                    DomText* tn = dom_text_create(s, elem);
                    if (tn) ((DomNode*)elem)->append_child(tn);
                }
            }
        }
        js_dom_mutation_notify();
        return make_js_undefined();
    }

    // prepend(...nodes) — ParentNode.prepend()
    if (strcmp(method, "prepend") == 0) {
        // insert before first child
        DomNode* ref = elem->first_child;
        for (int i = 0; i < argc; i++) {
            DomNode* child_node = (DomNode*)js_dom_unwrap_element(args[i]);
            if (child_node) {
                if (child_node->parent) child_node->parent->remove_child(child_node);
                ((DomNode*)elem)->insert_before(child_node, ref);
                if (elem->tag() == HTM_TAG_SELECT && child_node->is_element() &&
                    child_node->as_element()->tag() == HTM_TAG_OPTION) {
                    DomElement* child_elem = child_node->as_element();
                    if (_get_selectedness(child_elem) && !dom_element_has_attribute(elem, "multiple")) {
                        _select_select_only_option(elem, child_elem);
                    } else {
                        _select_ask_for_reset(elem);
                    }
                }
            } else {
                const char* text = fn_to_cstr(args[i]);
                if (text) {
                    String* s = js_dom_create_document_string(elem->doc, text, strlen(text));
                    DomText* tn = dom_text_create(s, elem);
                    if (tn) ((DomNode*)elem)->insert_before(tn, ref);
                }
            }
        }
        _select_refresh_cached_selected_options_for_node((DomNode*)elem);
        js_dom_mutation_notify();
        return make_js_undefined();
    }

    // getClientRects() — returns array containing single DOMRect (same as getBoundingClientRect)
    if (strcmp(method, "getClientRects") == 0) {
        // compute absolute position
        float abs_x = 0, abs_y = 0;
        DomNode* n2 = (DomNode*)elem;
        while (n2) {
            abs_x += n2->x;
            abs_y += n2->y;
            n2 = n2->parent;
        }
        float w = elem->width;
        float h = elem->height;
        // create the DOMRect
        Item rect = js_new_object();
        Item k;
        k = (Item){.item = s2it(heap_create_name("x"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_x)});
        k = (Item){.item = s2it(heap_create_name("y"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_y)});
        k = (Item){.item = s2it(heap_create_name("top"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_y)});
        k = (Item){.item = s2it(heap_create_name("left"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)abs_x)});
        k = (Item){.item = s2it(heap_create_name("right"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)(abs_x + w))});
        k = (Item){.item = s2it(heap_create_name("bottom"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)(abs_y + h))});
        k = (Item){.item = s2it(heap_create_name("width"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)w)});
        k = (Item){.item = s2it(heap_create_name("height"))};
        js_property_set(rect, k, (Item){.item = i2it((int64_t)h)});
        // wrap in array
        Item arr = js_array_new(0);
        js_array_push(arr, rect);
        return arr;
    }

    // focus() / blur() — stubs for headless mode
    if (strcmp(method, "focus") == 0 || strcmp(method, "blur") == 0) {
        DocState* state = js_dom_current_state();
        if (strcmp(method, "focus") == 0) {
            if (js_dom_is_script_focusable(elem)) {
                js_document_active_element = elem;
                focus_set(state, (View*)elem, false);
                js_dom_focus_set_selection_for_element(state, elem);
            }
        } else {
            if (js_document_active_element == elem) js_document_active_element = nullptr;
            if (focus_get(state) == (View*)elem) focus_clear(state);
        }
        return make_js_undefined();
    }

    // HTMLElement.click() — synthesise and dispatch a `click` MouseEvent
    // (bubbles, cancelable, composed). Per the HTML spec §6.4.4, calling
    // click() on a disabled form control is a no-op (no event fires).
    if (strcmp(method, "click") == 0) {
        // Disabled form-control guard: button/input/select/textarea with the
        // `disabled` content attribute set must not dispatch the click event.
        if (elem->tag_name) {
            const char* tag = elem->tag_name;
            bool is_form_ctrl =
                strcasecmp(tag, "button") == 0 ||
                strcasecmp(tag, "input") == 0 ||
                strcasecmp(tag, "select") == 0 ||
                strcasecmp(tag, "textarea") == 0 ||
                strcasecmp(tag, "fieldset") == 0;
            if (is_form_ctrl && dom_element_has_attribute(elem, "disabled")) {
                return make_js_undefined();
            }
        }
        Item ev = js_create_click_mouse_event();
        return js_dom_dispatch_event(elem_item, ev);
    }

    // getElementById(id) — for DocumentFragment hosts. The DOM spec puts
    // this on NonElementParentNode (Document + DocumentFragment). For
    // every other element it is undefined; we still implement it as a
    // tree-scoped lookup since several WPT tests use the synthetic
    // `#document-fragment` element returned by `createDocumentFragment`.
    if (strcmp(method, "getElementById") == 0) {
        if (argc < 1) return ItemNull;
        const char* id = fn_to_cstr(args[0]);
        if (!id) return ItemNull;
        DomElement* found = dom_find_by_id(elem, id);
        return found ? js_dom_wrap_element(found) : ItemNull;
    }

    // setSelectionRange(start, end [, direction]) — text controls only.
    if (strcmp(method, "setSelectionRange") == 0 && tc_is_text_control_elem(elem)) {
        if (argc < 2) return make_js_undefined();
        int64_t s = it2i(args[0]);
        int64_t e = it2i(args[1]);
        if (s < 0) s = 0;
        if (e < 0) e = 0;
        uint8_t dir = 0;
        if (argc >= 3) {
            const char* d = fn_to_cstr(args[2]);
            if (d) {
                if (strcmp(d, "forward") == 0) dir = 1;
                else if (strcmp(d, "backward") == 0) dir = 2;
            }
        }
        form_control_set_selection(js_dom_current_state(), (View*)elem, (uint32_t)s, (uint32_t)e, dir);
        return make_js_undefined();
    }

    // setRangeText(replacement [, start, end, selectionMode]) — text controls only.
    if (strcmp(method, "setRangeText") == 0 && tc_is_text_control_elem(elem)) {
        Item replacement_arg = argc >= 1 ? args[0] : make_js_undefined();
        Item start_arg = argc >= 2 ? args[1] : make_js_undefined();
        Item end_arg = argc >= 3 ? args[2] : make_js_undefined();
        Item mode_arg = argc >= 4 ? args[3] : make_js_undefined();
        return js_text_control_set_range_text_for_elem(elem, replacement_arg,
            start_arg, end_arg, mode_arg);
    }

    // select() — text controls only. Selects the entire value and focuses.
    if (strcmp(method, "select") == 0 && tc_is_text_control_elem(elem)) {
        tc_ensure_init(elem);
        FormControlProp* f = elem->form;
        if (js_dom_is_script_focusable(elem)) {
            focus_set(js_dom_current_state(), (View*)elem, false);
        }
        form_control_set_selection(js_dom_current_state(), (View*)elem, 0, f->current_value_u16_len, 0);
        return make_js_undefined();
    }

    // ----------------------------------------------------------------
    // F-4: Constraint Validation methods
    // ----------------------------------------------------------------

    // setCustomValidity(message): store custom validity message
    if (strcmp(method, "setCustomValidity") == 0) {
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
    if (strcmp(method, "reset") == 0 && elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) {
        // Per HTML §4.10.21.4: fire `reset` event (bubbles, cancelable);
        // if not cancelled, run reset algorithm on each listed control.
        Item ev = js_create_event("reset", /*bubbles=*/true, /*cancelable=*/true);
        // form.reset() fires a trusted event per spec.
        js_property_set(ev, (Item){.item = s2it(heap_create_name("isTrusted"))},
                        (Item){.item = ITEM_TRUE});
        Item dispatched = js_dom_dispatch_event(elem_item, ev);
        if (dispatched.item == (ITEM_FALSE)) return make_js_undefined();
        _run_form_reset(elem);
        return make_js_undefined();
    }

    // checkValidity(): fire invalid event if not valid, return bool
    if (strcmp(method, "checkValidity") == 0) {
        // form.checkValidity(): check all listed form controls
        if (elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) {
            bool all_valid = true;
            std::function<void(DomNode*)> check_all = [&](DomNode* node) {
                while (node) {
                    if (node->is_element()) {
                        DomElement* ce = (DomElement*)node;
                        if (ce->tag_name && !_elem_is_barred(ce) &&
                            (strcasecmp(ce->tag_name, "input") == 0 ||
                             strcasecmp(ce->tag_name, "select") == 0 ||
                             strcasecmp(ce->tag_name, "textarea") == 0 ||
                             strcasecmp(ce->tag_name, "button") == 0)) {
                            Item vs = _build_validity_state(ce);
                            Item vf = js_property_get(vs, (Item){.item = s2it(heap_create_name("valid"))});
                            if (!((vf.item & 0xFF) != 0 && vf.item != ITEM_NULL)) {
                                all_valid = false;
                                Item cev = js_new_object();
                                js_property_set(cev, (Item){.item = s2it(heap_create_name("type"))},
                                                (Item){.item = s2it(heap_create_name("invalid"))});
                                js_property_set(cev, (Item){.item = s2it(heap_create_name("cancelable"))},
                                                (Item){.item = ITEM_TRUE});
                                js_dom_dispatch_event(js_dom_wrap_element(ce), cev);
                            }
                        }
                        check_all(ce->first_child);
                    }
                    node = node->next_sibling;
                }
            };
            check_all(elem->first_child);
            return (Item){.item = b2it(all_valid)};
        }
        // barred elements are always valid
        if (_elem_is_barred(elem)) return (Item){.item = ITEM_TRUE};
        Item vs = _build_validity_state(elem);
        Item valid_flag = js_property_get(vs, (Item){.item = s2it(heap_create_name("valid"))});
        bool is_valid = ((valid_flag.item & 0xFF) != 0 && valid_flag.item != ITEM_NULL);
        if (!is_valid) {
            // fire "invalid" event (not bubbles, but cancelable per spec)
            Item ev_obj = js_new_object();
            js_property_set(ev_obj, (Item){.item = s2it(heap_create_name("type"))},
                            (Item){.item = s2it(heap_create_name("invalid"))});
            js_property_set(ev_obj, (Item){.item = s2it(heap_create_name("bubbles"))},
                            (Item){.item = ITEM_FALSE});
            js_property_set(ev_obj, (Item){.item = s2it(heap_create_name("cancelable"))},
                            (Item){.item = ITEM_TRUE});
            js_dom_dispatch_event(elem_item, ev_obj);
        }
        return (Item){.item = b2it(is_valid)};
    }

    // reportValidity(): same as checkValidity() in headless (no UI feedback)
    if (strcmp(method, "reportValidity") == 0) {
        if (elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) {
            // delegate to checkValidity for forms
            return js_dom_element_method(elem_item, (Item){.item = s2it(heap_create_name("checkValidity"))}, nullptr, 0);
        }
        if (_elem_is_barred(elem)) return (Item){.item = ITEM_TRUE};
        Item vs = _build_validity_state(elem);
        Item valid_flag = js_property_get(vs, (Item){.item = s2it(heap_create_name("valid"))});
        bool is_valid = ((valid_flag.item & 0xFF) != 0 && valid_flag.item != ITEM_NULL);
        if (!is_valid) {
            Item ev_obj = js_new_object();
            js_property_set(ev_obj, (Item){.item = s2it(heap_create_name("type"))},
                            (Item){.item = s2it(heap_create_name("invalid"))});
            js_property_set(ev_obj, (Item){.item = s2it(heap_create_name("cancelable"))},
                            (Item){.item = ITEM_TRUE});
            js_dom_dispatch_event(elem_item, ev_obj);
        }
        return (Item){.item = b2it(is_valid)};
    }

    // HTMLSelectElement.namedItem(name) — search options by id/name.
    if (strcmp(method, "namedItem") == 0 && elem->tag_name && strcasecmp(elem->tag_name, "select") == 0) {
        if (argc < 1) return ItemNull;
        const char* name = fn_to_cstr(args[0]);
        if (!name || !*name) return ItemNull;
        Item arr = js_array_new(0);
        _collect_options(elem->first_child, arr);
        int64_t n = js_array_length(arr);
        for (int64_t i = 0; i < n; i++) {
            Item item = js_array_get_int(arr, i);
            DomElement* opt = (DomElement*)js_dom_unwrap_element(item);
            if (!opt) continue;
            const char* id = dom_element_get_attribute(opt, "id");
            if (id && strcmp(id, name) == 0) return item;
            const char* nm = dom_element_get_attribute(opt, "name");
            if (nm && strcmp(nm, name) == 0) return item;
        }
        return ItemNull;
    }

    // HTMLSelectElement.add(element, before) — insert option/optgroup
    if (strcmp(method, "add") == 0 && elem->tag_name && strcasecmp(elem->tag_name, "select") == 0) {
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
                extern void js_throw_value(Item error);
                extern Item js_new_error_with_name(Item type_name, Item message);
                Item n = (Item){.item = s2it(heap_create_name("HierarchyRequestError"))};
                Item m = (Item){.item = s2it(heap_create_name(
                    "Failed to execute 'add' on 'HTMLSelectElement': "
                    "The new child element contains the parent."))};
                js_throw_value(js_new_error_with_name(n, m));
                return ItemNull;
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
                        before_elem = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, idx));
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
    // HTMLSelectElement.remove(index) — remove option at index.
    if (strcmp(method, "remove") == 0 && elem->tag_name && strcasecmp(elem->tag_name, "select") == 0) {
        // remove() with no args: spec calls ChildNode.remove(); but
        // HTMLSelectElement overrides — with no arg, do nothing per WPT.
        if (argc < 1) return ItemNull;
        TypeId t = get_type_id(args[0]);
        int idx = -1;
        if (t == LMD_TYPE_INT) idx = (int)it2i(args[0]); // INT_CAST_OK: index
        else if (t == LMD_TYPE_FLOAT) {
            double* d = (double*)(args[0].item & 0x00FFFFFFFFFFFFFF);
            if (d) idx = (int)*d; // INT_CAST_OK: index
        }
        if (idx < 0) return ItemNull;
        Item arr = js_array_new(0);
        _collect_options(elem->first_child, arr);
        if (idx >= js_array_length(arr)) return ItemNull;
        DomElement* opt = (DomElement*)js_dom_unwrap_element(js_array_get_int(arr, idx));
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
    log_debug("js_dom_element_method: unknown method '%s'", method);
    return ItemNull;
}

// ============================================================================
// classList API (v12)
// ============================================================================

extern "C" Item js_classlist_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        log_error("js_classlist_method: not a DOM element");
        return ItemNull;
    }

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    log_debug("js_classlist_method: '%s' on <%s>", method,
              elem->tag_name ? elem->tag_name : "?");

    // add(className, ...)
    if (strcmp(method, "add") == 0) {
        for (int i = 0; i < argc; i++) {
            const char* cls = fn_to_cstr(args[i]);
            if (cls) dom_element_add_class(elem, cls);
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return ItemNull;
    }

    // remove(className, ...)
    if (strcmp(method, "remove") == 0) {
        for (int i = 0; i < argc; i++) {
            const char* cls = fn_to_cstr(args[i]);
            if (cls) dom_element_remove_class(elem, cls);
        }
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return ItemNull;
    }

    // toggle(className [, force]) → boolean
    if (strcmp(method, "toggle") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return (Item){.item = ITEM_FALSE};

        if (argc >= 2) {
            // force parameter: add if truthy, remove if falsy
            bool force = js_is_truthy(args[1]);
            if (force) {
                dom_element_add_class(elem, cls);
                js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
                return (Item){.item = ITEM_TRUE};
            } else {
                dom_element_remove_class(elem, cls);
                js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
                return (Item){.item = ITEM_FALSE};
            }
        }
        // no force: toggle
        bool result = dom_element_toggle_class(elem, cls);
        js_dom_mutation_notify(DOM_JS_MUTATION_ATTRIBUTE, (DomNode*)elem, elem->parent);
        return (Item){.item = b2it(result ? 1 : 0)};
    }

    // contains(className) → boolean
    if (strcmp(method, "contains") == 0) {
        if (argc < 1) return (Item){.item = ITEM_FALSE};
        const char* cls = fn_to_cstr(args[0]);
        if (!cls) return (Item){.item = ITEM_FALSE};
        bool has = dom_element_has_class(elem, cls);
        return (Item){.item = b2it(has ? 1 : 0)};
    }

    // item(index) → string or null
    if (strcmp(method, "item") == 0) {
        if (argc < 1) return ItemNull;
        int64_t idx = it2i(args[0]);
        if (idx < 0 || idx >= elem->class_count) return ItemNull;
        return (Item){.item = s2it(heap_create_name(elem->class_names[idx]))};
    }

    // replace(oldClass, newClass) → boolean
    if (strcmp(method, "replace") == 0) {
        if (argc < 2) return (Item){.item = ITEM_FALSE};
        const char* old_cls = fn_to_cstr(args[0]);
        const char* new_cls = fn_to_cstr(args[1]);
        if (!old_cls || !new_cls) return (Item){.item = ITEM_FALSE};
        if (!dom_element_has_class(elem, old_cls)) return (Item){.item = ITEM_FALSE};
        dom_element_remove_class(elem, old_cls);
        dom_element_add_class(elem, new_cls);
        js_dom_mutation_notify();
        return (Item){.item = ITEM_TRUE};
    }

    // toString() → space-separated class string
    if (strcmp(method, "toString") == 0) {
        if (elem->class_count == 0) return (Item){.item = s2it(heap_create_name(""))};
        StrBuf* sb = strbuf_new_cap(64);
        for (int i = 0; i < elem->class_count; i++) {
            if (i > 0) strbuf_append_char(sb, ' ');
            strbuf_append_str(sb, elem->class_names[i]);
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    log_debug("js_classlist_method: unknown method '%s'", method);
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
        if (elem->class_count == 0) return (Item){.item = s2it(heap_create_name(""))};
        StrBuf* sb = strbuf_new_cap(64);
        for (int i = 0; i < elem->class_count; i++) {
            if (i > 0) strbuf_append_char(sb, ' ');
            strbuf_append_str(sb, elem->class_names[i]);
        }
        String* result = heap_create_name(sb->str ? sb->str : "");
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
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

    const char* val = dom_element_get_attribute(elem, attr_name);
    if (val) {
        return (Item){.item = s2it(heap_create_name(val))};
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

    dom_element_set_attribute(elem, attr_name, val_str);
    return value;
}

// ============================================================================
// location API (v12) — document.URL / document.location
// ============================================================================

extern "C" Item js_location_get_property(Item prop_name) {
    if (!_js_current_document) {
        log_debug("js_location_get_property: no document set");
        return (Item){.item = s2it(heap_create_name(""))};
    }

    const char* prop = fn_to_cstr(prop_name);
    if (!prop) return (Item){.item = s2it(heap_create_name(""))};

    Url* url = _js_current_document->url;
    if (!url) {
        log_debug("js_location_get_property: document has no URL");
        return (Item){.item = s2it(heap_create_name(""))};
    }

    if (strcmp(prop, "href") == 0) {
        const char* href = url_get_href(url);
        return (Item){.item = s2it(heap_create_name(href ? href : ""))};
    }
    if (strcmp(prop, "protocol") == 0) {
        const char* proto = url_get_protocol(url);
        return (Item){.item = s2it(heap_create_name(proto ? proto : ""))};
    }
    if (strcmp(prop, "hostname") == 0) {
        const char* hostname = url_get_hostname(url);
        return (Item){.item = s2it(heap_create_name(hostname ? hostname : ""))};
    }
    if (strcmp(prop, "port") == 0) {
        const char* port = url_get_port(url);
        return (Item){.item = s2it(heap_create_name(port ? port : ""))};
    }
    if (strcmp(prop, "pathname") == 0) {
        const char* pathname = url_get_pathname(url);
        return (Item){.item = s2it(heap_create_name(pathname ? pathname : ""))};
    }
    if (strcmp(prop, "search") == 0) {
        const char* search = url_get_search(url);
        return (Item){.item = s2it(heap_create_name(search ? search : ""))};
    }
    if (strcmp(prop, "hash") == 0) {
        const char* hash = url_get_hash(url);
        return (Item){.item = s2it(heap_create_name(hash ? hash : ""))};
    }
    if (strcmp(prop, "host") == 0) {
        const char* host = url_get_host(url);
        return (Item){.item = s2it(heap_create_name(host ? host : ""))};
    }
    if (strcmp(prop, "origin") == 0) {
        const char* origin = url_get_origin(url);
        return (Item){.item = s2it(heap_create_name(origin ? origin : ""))};
    }

    log_debug("js_location_get_property: unknown property '%s'", prop);
    return (Item){.item = s2it(heap_create_name(""))};
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

static Item js_history_apply_url(Item url_item) {
    if (get_type_id(url_item) == LMD_TYPE_UNDEFINED || is_js_undefined(url_item)) {
        return make_js_undefined();
    }

    const char* next_url = fn_to_cstr(url_item);
    DomDocument* doc = _js_current_document ? _js_current_document : _js_main_document;
    if (!doc || !next_url || !next_url[0]) return make_js_undefined();

    Url* resolved = doc->url ? url_parse_with_base(next_url, doc->url) : url_parse(next_url);
    if (!resolved || !url_is_valid(resolved)) {
        if (resolved) url_destroy(resolved);
        resolved = js_dom_make_fallback_url(next_url);
        if (!resolved) return make_js_undefined();
    }

    if (doc->url) url_destroy(doc->url);
    doc->url = resolved;
    return make_js_undefined();
}

extern "C" Item js_history_push_state(Item state, Item title, Item url_item) {
    (void)state;
    (void)title;
    return js_history_apply_url(url_item);
}

extern "C" Item js_history_replace_state(Item state, Item title, Item url_item) {
    (void)state;
    (void)title;
    return js_history_apply_url(url_item);
}

static Item js_history_noop(Item arg) {
    (void)arg;
    return make_js_undefined();
}

static void js_dom_install_window_location_history_globals(void) {
    Item global = js_get_global_this();
    Item doc_proxy = js_get_document_object_value();
    js_property_set(global, js_string_key("location"), doc_proxy);

    Item history = js_new_object();
    js_property_set(history, js_string_key("pushState"),
        js_new_function((void*)js_history_push_state, 3));
    js_property_set(history, js_string_key("replaceState"),
        js_new_function((void*)js_history_replace_state, 3));
    js_property_set(history, js_string_key("back"),
        js_new_function((void*)js_history_noop, 1));
    js_property_set(history, js_string_key("forward"),
        js_new_function((void*)js_history_noop, 1));
    js_property_set(history, js_string_key("go"),
        js_new_function((void*)js_history_noop, 1));
    js_property_set(history, js_string_key("length"), (Item){.item = i2it(1)});
    js_property_set(global, js_string_key("history"), history);

    Item window = js_property_get(global, js_string_key("window"));
    if (get_type_id(window) == LMD_TYPE_MAP) {
        js_property_set(window, js_string_key("location"), doc_proxy);
        js_property_set(window, js_string_key("history"), history);
    }
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
// style.setProperty() / style.removeProperty() (v12b)
// ============================================================================

extern "C" Item js_dom_style_method(Item elem_item, Item method_name, Item* args, int argc) {
    DomElement* elem = (DomElement*)js_dom_unwrap_element(elem_item);
    if (!elem) {
        // check if this is a CSSOM rule — transpiler routes rule.style.setProperty() here too
        if (js_is_css_rule(elem_item)) {
            // get the .style wrapper from the rule
            Item style_prop = (Item){.item = s2it(heap_create_name("style"))};
            Item style_decl = js_cssom_rule_get_property(elem_item, style_prop);
            if (js_is_rule_style_decl(style_decl)) {
                return js_cssom_rule_decl_method(style_decl, method_name, args, argc);
            }
        }
        return ItemNull;
    }

    const char* method = fn_to_cstr(method_name);
    if (!method) return ItemNull;

    // setProperty(property, value [, priority])
    if (strcmp(method, "setProperty") == 0) {
        if (argc < 2) return ItemNull;
        const char* css_prop = fn_to_cstr(args[0]);
        const char* val_str = fn_to_cstr(args[1]);
        if (!css_prop || !val_str) return ItemNull;

        char style_decl[256];
        if (argc >= 3) {
            const char* priority = fn_to_cstr(args[2]);
            if (priority && strcasecmp(priority, "important") == 0) {
                snprintf(style_decl, sizeof(style_decl), "%s: %s !important", css_prop, val_str);
            } else {
                snprintf(style_decl, sizeof(style_decl), "%s: %s", css_prop, val_str);
            }
        } else {
            snprintf(style_decl, sizeof(style_decl), "%s: %s", css_prop, val_str);
        }
        int applied = dom_element_apply_inline_style(elem, style_decl);
        elem->styles_resolved = false;
        if (applied) {
            CssPropertyId prop_id = css_property_id_from_name(css_prop);
            js_dom_mutation_notify(js_dom_style_mutation_kind(prop_id),
                                   (DomNode*)elem, elem->parent);
        }
        log_debug("js_dom_style_method: setProperty '%s: %s' on <%s>",
                  css_prop, val_str, elem->tag_name ? elem->tag_name : "?");
        return ItemNull;
    }

    // removeProperty(property) — returns old value
    if (strcmp(method, "removeProperty") == 0) {
        if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
        const char* css_prop = fn_to_cstr(args[0]);
        if (!css_prop) return (Item){.item = s2it(heap_create_name(""))};

        // get old value before removing
        CssPropertyId prop_id = css_property_id_from_name(css_prop);
        Item old_val = (Item){.item = s2it(heap_create_name(""))};
        if (prop_id != CSS_PROPERTY_UNKNOWN && elem->specified_style) {
            CssDeclaration* decl = dom_element_get_specified_value(elem, prop_id);
            if (decl && decl->specificity.inline_style) {
                // serialize old value via the getter
                Item prop_item = (Item){.item = s2it(heap_create_name(css_prop))};
                old_val = js_dom_get_style_property(elem_item, prop_item);
            }
            // remove the declaration from the style tree
            style_tree_remove_property(elem->specified_style, prop_id);
            js_dom_mutation_notify(js_dom_style_mutation_kind(prop_id),
                                   (DomNode*)elem, elem->parent);
        }
        elem->styles_resolved = false;
        log_debug("js_dom_style_method: removeProperty '%s' on <%s>",
                  css_prop, elem->tag_name ? elem->tag_name : "?");
        return old_val;
    }

    log_debug("js_dom_style_method: unknown method '%s'", method);
    return ItemNull;
}

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

extern "C" Item js_throw_type_error(const char* message);
extern "C" void js_set_function_name(Item fn_item, Item name_item);
extern "C" Item js_new_function(void* func_ptr, int param_count);

static Item _coll_illegal_constructor(Item /*first*/) {
    return js_throw_type_error("Illegal constructor");
}

static void _install_iface(Item global, const char* name) {
    Item ctor = js_new_function((void*)_coll_illegal_constructor, 0);
    js_set_function_name(ctor, (Item){.item = s2it(heap_create_name(name))});
    Item proto = js_new_object();
    js_property_set(proto, (Item){.item = s2it(heap_create_name("constructor"))}, ctor);
    js_property_set(ctor, (Item){.item = s2it(heap_create_name("prototype"))}, proto);
    js_property_set(global, (Item){.item = s2it(heap_create_name(name))}, ctor);
}

extern "C" void js_dom_install_collection_globals(void) {
    Item global = js_get_global_this();
    _install_iface(global, "HTMLCollection");
    _install_iface(global, "HTMLFormControlsCollection");
    _install_iface(global, "HTMLOptionsCollection");
    _install_iface(global, "NodeList");
    _install_iface(global, "RadioNodeList");
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
            String* str = js_dom_create_document_string(doc, t, strlen(t));
            DomText* tn = dom_text_create(str, opt);
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
        dom_element_set_attribute(opt, "value", v ? v : "");
    }
    // defaultSelected → `selected` content attribute.
    if (def_sel_arg.item != ITEM_NULL && !is_js_undefined(def_sel_arg) &&
        js_is_truthy(def_sel_arg)) {
        dom_element_set_attribute(opt, "selected", "");
    }
    // selected → live selectedness flag.
    if (sel_arg.item != ITEM_NULL && !is_js_undefined(sel_arg)) {
        _set_selectedness(opt, js_is_truthy(sel_arg));
    }
    return js_dom_wrap_element(opt);
}

extern "C" void js_dom_install_option_constructor(void) {
    Item global = js_get_global_this();
    Item ctor = js_new_function((void*)_option_ctor, 4);
    js_set_function_name(ctor, (Item){.item = s2it(heap_create_name("Option"))});
    Item proto = js_new_object();
    js_property_set(proto, (Item){.item = s2it(heap_create_name("constructor"))}, ctor);
    js_property_set(ctor, (Item){.item = s2it(heap_create_name("prototype"))}, proto);
    js_property_set(global, (Item){.item = s2it(heap_create_name("Option"))}, ctor);
    log_debug("js_dom_install_option_constructor: installed Option");
}
