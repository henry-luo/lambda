/**
 * js_dom_events.cpp — DOM Event System for Lambda JS Runtime
 *
 * Implements the EventTarget interface (addEventListener, removeEventListener,
 * dispatchEvent) with full 3-phase propagation (capture → target → bubble).
 *
 * Listener storage: simple flat array of {key, listeners} entries keyed by
 * DomNode pointer. Avoids modifying the DomNode struct.
 */

#include "js_dom_events.h"
#include "js_dom.h"
#include "js_dom_selection.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_class.h"
#include "../lambda.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../jube/jube_registry.h"
#include "../module/radiant/radiant_dom_bridge.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/strbuf.h"
#include "../../lib/url.h"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_lifecycle.hpp"

#include <cstring>
#include <cmath>

extern Item js_make_number(double d);

struct EventContext;
extern "C" bool radiant_synthetic_dom_dispatch_is_reentry(Item event_item);
extern "C" Item radiant_dispatch_synthetic_dom_event(Item target_item,
                                                      Item event_item);
extern "C" bool radiant_author_template_event_live(const char* event_name);
extern "C" bool radiant_author_template_dispatch_begin(Item event);
extern "C" void radiant_author_template_dispatch_end(Item event);
extern "C" void radiant_dispatch_author_template_participant(void* dom_node,
                                                               Item event,
                                                               const char* event_name);
extern "C" void radiant_dom_event_set_lambda_dispatch_position(
    Item event, Item current_target, int event_phase);
extern "C" void radiant_dom_event_clear_lambda_dispatch_position(Item event);

// Forward decls used by Event helpers below (signatures from js_runtime.h /
// js_dom.h, declared here under extern "C" to avoid header coupling).
extern __thread EvalContext* context;

// Shared form-control classification used by requestSubmit/reset helpers.
extern "C" const char* js_dom_input_type_lower(void* dom_elem);
extern "C" Item js_formdata_collect_form_entries(void* form_elem, void* submitter_elem);
extern "C" bool js_dom_navigate_submit_target(const char* target_name, const char* url);
extern "C" Item js_dom_check_validity_bridge(Item elem_item);
extern "C" Item radiant_dom_element_operation(Item elem_item,
                                                JubeDomElementOperation operation,
                                                Item* args, int argc);
// Append `text` to `sb` using the HTML application/x-www-form-urlencoded
// serializer. Encoding straight into the buffer avoids the allocate-copy-free
// round trip the per-field url_encode_component() calls used to pay. This is
// also the spec's serializer, which encodes U+0020 as '+' — encodeURIComponent
// rules (%20, and ! ~ ' ( ) left literal) are not what a form submission emits.
static void js_dom_append_form_encoded(StrBuf* sb, const char* text, size_t len) {
    if (!sb || !text || len == 0) return;
    size_t need = url_encode_measure(text, len, URL_KEEP_FORM, true, NULL);
    if (!strbuf_ensure_cap(sb, sb->length + need + 1)) return;
    sb->length += url_encode_write(text, len, URL_KEEP_FORM, true, sb->str + sb->length);
    sb->str[sb->length] = '\0';
}

static char* js_dom_build_submit_query(Item entries) {
    if (get_type_id(entries) != LMD_TYPE_ARRAY) {
        return mem_strdup("", MEM_CAT_JS_RUNTIME);
    }

    StrBuf* sb = strbuf_new_cap(128);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_elements_get_int(entries, i);
        if (get_type_id(pair) != LMD_TYPE_ARRAY || js_array_length(pair) < 2) continue;

        const char* key = fn_to_cstr(js_elements_get_int(pair, 0));
        const char* val = fn_to_cstr(js_elements_get_int(pair, 1));
        if (!key) continue;
        if (!val) val = "";

        if (sb->length > 0) strbuf_append_char(sb, '&');
        js_dom_append_form_encoded(sb, key, strlen(key));
        strbuf_append_char(sb, '=');
        js_dom_append_form_encoded(sb, val, strlen(val));
    }

    char* query = mem_strdup(sb->str ? sb->str : "", MEM_CAT_JS_RUNTIME);
    strbuf_free(sb);
    return query;
}

static const char* js_dom_pick_submit_attr(DomElement* submitter, DomElement* form,
                                           const char* submitter_attr,
                                           const char* form_attr) {
    if (submitter) {
        const char* submitter_val = submitter->get_attribute(submitter_attr);
        if (submitter_val && *submitter_val) return submitter_val;
    }
    if (form) {
        const char* form_val = form->get_attribute(form_attr);
        if (form_val && *form_val) return form_val;
    }
    return "";
}

static void js_dom_run_form_submit_navigation(DomElement* form, DomElement* submitter) {
    if (!form) return;

    const char* method = js_dom_pick_submit_attr(submitter, form, "formmethod", "method");
    if (!method || !*method) method = "get";

    const char* target = js_dom_pick_submit_attr(submitter, form, "formtarget", "target");
    const char* action = js_dom_pick_submit_attr(submitter, form, "formaction", "action");
    if ((!action || !*action) && form->doc && form->doc->url) {
        action = url_get_href(form->doc->url);
    }
    if (!action || !*action) return;

    char* nav_url = nullptr;
    if (strcasecmp(method, "get") == 0) {
        Item entries = js_formdata_collect_form_entries(form, submitter);
        char* query = js_dom_build_submit_query(entries);
        size_t action_len = strlen(action);
        size_t query_len = strlen(query);
        bool has_query = strchr(action, '?') != nullptr;
        size_t extra = query_len > 0 ? 1 : 0;
        nav_url = (char*)mem_alloc(action_len + extra + query_len + 1, MEM_CAT_JS_RUNTIME);
        memcpy(nav_url, action, action_len);
        size_t pos = action_len;
        if (query_len > 0) {
            nav_url[pos++] = has_query ? '&' : '?';
            memcpy(nav_url + pos, query, query_len);
            pos += query_len;
        }
        nav_url[pos] = '\0';
        mem_free(query);
    } else {
        nav_url = mem_strdup(action, MEM_CAT_JS_RUNTIME);
    }

    if (nav_url) {
        js_dom_navigate_submit_target(target, nav_url);
        mem_free(nav_url);
    }
}

static void event_apply_new_target_prototype(Item event) {
    Item new_target = js_get_new_target();
    TypeId nt_type = get_type_id(new_target);
    if (nt_type == LMD_TYPE_MAP || nt_type == LMD_TYPE_FUNC) {
        Item proto = js_get_key_cstr(new_target, "prototype");
        TypeId proto_type = get_type_id(proto);
        if (proto_type == LMD_TYPE_MAP || proto_type == LMD_TYPE_FUNC ||
            proto_type == LMD_TYPE_ARRAY || proto_type == LMD_TYPE_ELEMENT) {
            radiant_dom_event_set_prototype_override(event, proto);
        }
    }
}

// Event timestamps share the document-relative performance clock. Spec
// requires that `timeStamp` is directly comparable with performance.now().
// The value is clamped to 5 microsecond resolution per HR-Time / WPT
// `Event-timestamp-safe-resolution` (timing-attack hardening).
static double event_now_ms() {
    double ms = js_performance_now_ms();
    // clamp to 5us = 0.005ms resolution
    return floor(ms / 0.005) * 0.005;
}

static Item event_exception_message(Item err) {
    Item msg = err;
    if (get_type_id(err) == LMD_TYPE_MAP || get_type_id(err) == LMD_TYPE_OBJECT) {
        Item m = js_get_name_key(err, "message");
        if (get_type_id(m) == LMD_TYPE_STRING) msg = m;
        else msg = js_to_string(err);
    } else if (get_type_id(err) != LMD_TYPE_STRING) {
        msg = js_to_string(err);
    }
    if (item_is_error(msg)) {
        (void)js_error_lane_payload(msg);
        msg = js_name_item("<error while formatting exception>");
    }
    return msg;
}

static void log_event_exception_detail(const char* source, const char* type, Item err) {
    // Online pages often expose unsupported DOM APIs inside event callbacks; log
    // the thrown message so the compatibility ledger can name the missing API.
    Item msg = event_exception_message(err);
    if (get_type_id(msg) == LMD_TYPE_STRING) {
        String* s = it2s(msg);
        log_error("%s for '%s' threw: %.*s; continuing dispatch",
            source ? source : "event callback", type ? type : "", s ? (int)s->len : 0, s ? s->chars : "");
    }
    else {
        log_error("%s for '%s' threw item type=%d; continuing dispatch",
            source ? source : "event callback", type ? type : "", get_type_id(msg));
    }
}

// Report an exception thrown by an event listener / handler to
// `window.onerror` (HTML spec: report exception). Best-effort: if
// onerror is not a function, just swallow.
static void report_exception_to_window_onerror(Item err, const char* type) {
    Item global = js_get_global_this();
    if (global.item == 0) return;
    Item onerr = js_get_name_key(global, "onerror");
    if (!js_is_callable(onerr)) return;
    Item msg = event_exception_message(err);
    Item args[5] = { msg, ItemNull, (Item){.item = b2it(false)}, (Item){.item = b2it(false)}, err };
    Item onerror_result = js_call_function(onerr, global, args, 5);
    if (item_is_error(onerror_result)) (void)js_error_lane_payload(onerror_result);
    (void)type;
}

extern "C" DomElement* js_dom_find_form_owner(void* control_ptr) {
    DomElement* control = (DomElement*)control_ptr;
    if (!control) return nullptr;
    const char* form_id = control->get_attribute("form");
    if (form_id && *form_id) {
        DomDocument* doc = control->doc;
        if (doc && doc->root) return js_dom_find_element_by_id(doc->root, form_id);
        return nullptr;
    }

    DomNode* p = control->parent;
    while (p && p->is_element()) {
        DomElement* elem = p->as_element();
        if (elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) return elem;
        p = p->parent;
    }
    return nullptr;
}

extern "C" bool js_dom_is_submit_button(void* elem_ptr) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem || !elem->tag_name) return false;
    if (strcasecmp(elem->tag_name, "input") == 0) {
        const char* type = js_dom_input_type_lower(elem);
        return strcmp(type, "submit") == 0 || strcmp(type, "image") == 0;
    }
    if (strcasecmp(elem->tag_name, "button") == 0) {
        const char* type = js_dom_input_type_lower(elem);
        return strcmp(type, "text") == 0 || strcmp(type, "submit") == 0;
    }
    return false;
}

extern "C" bool js_dom_is_reset_button(void* elem_ptr) {
    DomElement* elem = (DomElement*)elem_ptr;
    if (!elem || !elem->tag_name) return false;
    if (strcasecmp(elem->tag_name, "input") == 0) {
        return strcmp(js_dom_input_type_lower(elem), "reset") == 0;
    }
    if (strcasecmp(elem->tag_name, "button") == 0) {
        return strcmp(js_dom_input_type_lower(elem), "reset") == 0;
    }
    return false;
}

static Item js_dom_throw_named_error(const char* name, const char* message) {
    Item error_name = js_name_item(name ? name : "Error");
    Item error_message = js_name_item(message ? message : "");
    return js_throw_value(js_new_error_with_name(error_name, error_message));
}

static Item js_dom_resolve_request_submitter(DomElement* form,
                                             Item submitter_item,
                                             bool* has_submitter,
                                             DomElement** out_submitter) {
    if (has_submitter) *has_submitter = false;
    if (out_submitter) *out_submitter = nullptr;
    TypeId st = get_type_id(submitter_item);
    if (submitter_item.item == 0 || st == LMD_TYPE_UNDEFINED) {
        return make_js_undefined();
    }

    if (has_submitter) *has_submitter = true;
    DomNode* node = (DomNode*)js_dom_unwrap_element(submitter_item);
    DomElement* submitter = (node && node->is_element()) ? node->as_element() : nullptr;
    if (!js_dom_is_submit_button(submitter)) {
        return js_throw_type_error("requestSubmit submitter must be a submit button");
    }

    DomElement* owner = js_dom_find_form_owner(submitter);
    if (owner != form) {
        return js_dom_throw_named_error("NotFoundError",
            "requestSubmit submitter is not owned by this form");
    }
    if (out_submitter) *out_submitter = submitter;
    return make_js_undefined();
}

static bool js_dom_should_validate_submit(DomElement* form, DomElement* submitter) {
    if (form && form->has_attribute("novalidate")) return false;
    if (submitter && submitter->has_attribute("formnovalidate")) return false;
    return true;
}

extern "C" Item js_dom_form_submit_bridge(Item form_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(form_item);
    DomElement* form = (node && node->is_element()) ? node->as_element() : nullptr;
    if (!form || !form->tag_name || strcasecmp(form->tag_name, "form") != 0) {
        return make_js_undefined();
    }

    // submit() intentionally bypasses validation and the cancelable submit event;
    // requestSubmit() below keeps those checks before entering this shared navigation path.
    js_dom_run_form_submit_navigation(form, nullptr);
    return make_js_undefined();
}

extern "C" Item js_dom_form_request_submit_bridge(Item form_item, Item submitter_item) {
    DomNode* node = (DomNode*)js_dom_unwrap_element(form_item);
    DomElement* form = (node && node->is_element()) ? node->as_element() : nullptr;
    if (!form || !form->tag_name || strcasecmp(form->tag_name, "form") != 0) {
        return make_js_undefined();
    }

    bool has_submitter = false;
    DomElement* submitter = nullptr;
    JS_ASSIGN_OR_RETURN(submitter_result, js_dom_resolve_request_submitter(form, submitter_item,
        &has_submitter, &submitter));
    if (has_submitter && !submitter) return make_js_undefined();

    if (js_dom_should_validate_submit(form, submitter)) {
        Item valid = js_dom_check_validity_bridge(form_item);
        if (!js_is_truthy(valid)) return make_js_undefined();
    }

    RootFrame roots(1);
    Rooted<Item> submit_event_root(roots, js_create_event("submit", true, true));
    js_set_key_cstr(submit_event_root.get(), "isTrusted", (Item){.item = ITEM_TRUE});
    js_set_key_cstr(submit_event_root.get(), "submitter", submitter ? js_dom_wrap_element(submitter) : ItemNull);
    Item submit_ok = js_dom_dispatch_event(form_item, submit_event_root.get());
    if (submit_ok.item == ITEM_FALSE) return make_js_undefined();

    js_dom_run_form_submit_navigation(form, submitter);
    return make_js_undefined();
}

// ============================================================================
// Event listener storage
// ============================================================================

struct EventListener {
    char*   type;       // event type string (owned copy)
    uint64_t* callback_root; // stable GC root for function/object callback
    uint64_t* signal_root;   // stable GC root for AbortSignal, when present
    uint64_t order;     // registration order, including on<type> handlers
    bool    capture;    // capture phase listener
    bool    once;       // remove after first invocation
    bool    passive;    // passive listener (cannot preventDefault)
    bool    is_idl_handler; // on<type> handler, not an addEventListener entry
    bool    removed;    // tombstone — set when removed during dispatch
};

// per-node listener list
struct NodeListeners {
    EventListener* items;
    int count;
    int capacity;
};

// flat array mapping void* keys → NodeListeners
struct NodeListenerEntry {
    void* key;
    DomDocument* owner_doc;
    DomNodeRef node_ref;
    uint64_t* target_root;
    NodeListeners listeners;
};

struct EventTargetIndexEntry {
    void* key;
    int slot;
};
HASHMAP_DEFINE_PTRKEY(event_target_index, EventTargetIndexEntry, key)

struct EventTypeCountEntry {
    const char* type;
    int count;
};

static void event_type_count_entry_free(void* item) {
    EventTypeCountEntry* entry = (EventTypeCountEntry*)item;
    if (entry && entry->type) mem_free((void*)entry->type);
}

HASHMAP_DEFINE_STRKEY(event_type_count, EventTypeCountEntry, type)

// DOM listener registration is semantic realm state. The state capsule is
// entered once at a JS/host boundary; all dispatch and lookup code below then
// uses ordinary owner-thread loads/stores, never a lock or atomic operation.
struct JsDomEventRuntimeState {
    NodeListenerEntry* entries = nullptr;
    int entry_count = 0;
    int entry_capacity = 0;
    struct hashmap* entry_index = nullptr;
    struct hashmap* type_counts = nullptr;
    uint64_t registration_order = 0;
};

static JsDomEventRuntimeState* js_dom_event_runtime_state_get() {
    if (!js_active_runtime_state) return nullptr;
    return (JsDomEventRuntimeState*)js_runtime_state.dom_event_state;
}

static bool js_dom_event_runtime_state_ensure() {
    if (!js_active_runtime_state) return false;
    if (js_dom_event_runtime_state_get()) return true;
    JsDomEventRuntimeState* state = (JsDomEventRuntimeState*)mem_calloc(1,
        sizeof(JsDomEventRuntimeState), MEM_CAT_JS_RUNTIME);
    if (!state) {
        log_error("js-dom-events: failed to allocate context state");
        return false;
    }
    js_runtime_state.dom_event_state = state;
    return true;
}

// These aliases retain the compact legacy implementation while each expands
// to a direct field of the already-bound context-local capsule.
#define js_dom_event_state ((JsDomEventRuntimeState*)js_runtime_state.dom_event_state)
#define _entries (js_dom_event_state->entries)
#define _entry_count (js_dom_event_state->entry_count)
#define _entry_capacity (js_dom_event_state->entry_capacity)
#define _entry_index (js_dom_event_state->entry_index)
#define _type_counts (js_dom_event_state->type_counts)
#define _event_registration_order (js_dom_event_state->registration_order)

// sentinel pointers for non-element targets
static const int _window_sentinel = 0;
static const int _document_sentinel = 0;
JS_FORWARD_STATIC_EXPRESSION(Item, event_listener_root_item, (uint64_t* root), (root ? (Item){.item = *root} : ItemNull))

static void event_listener_release_roots(EventListener* listener) {
    if (!listener) return;
    if (listener->callback_root) {
        heap_unregister_gc_root(listener->callback_root);
        mem_free(listener->callback_root);
        listener->callback_root = nullptr;
    }
    if (listener->signal_root) {
        heap_unregister_gc_root(listener->signal_root);
        mem_free(listener->signal_root);
        listener->signal_root = nullptr;
    }
}

static bool js_dom_event_is_document_target(Item target) {
    if (get_type_id(target) == LMD_TYPE_VMAP && target.vmap && target.vmap->host_type) {
        const JubeTypeDef* type = jube_find_type_by_host_type(target.vmap->host_type);
        if (type && type->name) {
            return strcmp(type->name, "document") == 0 ||
                   strcmp(type->name, "foreign_document") == 0;
        }
    }
    Item current_doc = js_get_document_object_value();
    return target.item != ITEM_NULL && target.item == current_doc.item;
}

// get the key pointer for a target item
static void* get_event_target_key(Item target) {
    // document wrappers are host VMAPs; key them through the registry instead
    // of the old proxy-brand predicate so listener storage follows host types.
    if (js_dom_event_is_document_target(target)) {
        return (void*)&_document_sentinel;
    }
    // check for DOM node
    void* node = js_dom_unwrap_element(target);
    if (node) return node;
    // If target IS the global (window) object, key on the window sentinel so
    // that addEventListener on window and dispatch through the path agree.
    {
        Item global = js_get_global_this();
        if (target.item != 0 && target.item == global.item) {
            return (void*)&_window_sentinel;
        }
    }
    // Plain JS object EventTarget — key on the object pointer itself so
    // each `new EventTarget()` instance has its own listener list.
    TypeId tid = get_type_id(target);
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT || tid == LMD_TYPE_VMAP) {
        return (void*)target.container;
    }
    // fallback: treat as window
    return (void*)&_window_sentinel;
}

static bool event_target_needs_root(Item target, void* key, DomNodeRef node_ref) {
    if (!key || node_ref.address ||
        key == (void*)&_window_sentinel || key == (void*)&_document_sentinel) {
        return false;
    }
    if (js_dom_unwrap_element(target)) return false;
    TypeId type = get_type_id(target);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP;
}

static int find_listener_entry_slot(void* key) {
    if (!key) return -1;
    if (_entry_index) {
        EventTargetIndexEntry lookup = {key, -1};
        const EventTargetIndexEntry* found =
            (const EventTargetIndexEntry*)hashmap_get(_entry_index, &lookup);
        if (found && found->slot >= 0 && found->slot < _entry_count) {
            return found->slot;
        }
        return -1;
    }
    for (int i = 0; i < _entry_count; i++) {
        if (_entries[i].key == key) return i;
    }
    return -1;
}

static bool index_listener_entry(void* key, int slot) {
    if (!_entry_index) {
        _entry_index = event_target_index_new(16);
        if (_entry_index) {
            for (int i = 0; i < _entry_count; i++) {
                EventTargetIndexEntry existing = {_entries[i].key, i};
                hashmap_set(_entry_index, &existing);
            }
        }
    }
    if (!_entry_index) return false;
    EventTargetIndexEntry entry = {key, slot};
    hashmap_set(_entry_index, &entry);
    if (hashmap_oom(_entry_index)) {
        hashmap_free(_entry_index);
        _entry_index = nullptr;
        return false;
    }
    return true;
}

static NodeListeners* get_or_create_listeners(void* key, DomDocument* owner_doc,
                                              DomNodeRef node_ref, Item target) {
    int existing_slot = find_listener_entry_slot(key);
    if (existing_slot >= 0) {
        NodeListenerEntry* existing = &_entries[existing_slot];
        if (!existing->target_root && event_target_needs_root(target, key, node_ref)) {
            // A raw GC-container key can otherwise dangle and alias a reused allocation.
            existing->target_root = heap_gc_root_slot_new(target.item);
            if (!existing->target_root) return nullptr;
        }
        return &existing->listeners;
    }

    // grow if needed
    if (_entry_count >= _entry_capacity) {
        int new_cap = _entry_capacity == 0 ? 16 : _entry_capacity * 2;
        NodeListenerEntry* new_entries = (NodeListenerEntry*)mem_calloc(new_cap, sizeof(NodeListenerEntry), MEM_CAT_JS_RUNTIME);
        if (_entries && _entry_count > 0) {
            memcpy(new_entries, _entries, _entry_count * sizeof(NodeListenerEntry));
            mem_free(_entries);
        }
        _entries = new_entries;
        _entry_capacity = new_cap;
    }

    int new_slot = _entry_count++;
    NodeListenerEntry* entry = &_entries[new_slot];
    entry->key = key;
    entry->owner_doc = owner_doc;
    entry->node_ref = node_ref;
    if (event_target_needs_root(target, key, node_ref)) {
        // Listener indices retain plain EventTargets for the document epoch;
        // without this root the pointer key can outlive its GC allocation.
        entry->target_root = heap_gc_root_slot_new(target.item);
        if (!entry->target_root) {
            _entry_count--;
            memset(entry, 0, sizeof(*entry));
            return nullptr;
        }
    }
    if (owner_doc && node_ref.address) {
        // Listener tables live outside the DOM tree and may survive removal;
        // the event-queue pin closes that native lifetime edge until reset.
        dom_node_pin(owner_doc, node_ref, DOM_NODE_PIN_EVENT_QUEUE);
    }
    // The slot, rather than the relocatable NodeListenerEntry address, is indexed.
    (void)index_listener_entry(key, new_slot);
    return &entry->listeners;
}

static NodeListeners* find_listeners(void* key) {
    int slot = find_listener_entry_slot(key);
    return slot >= 0 ? &_entries[slot].listeners : nullptr;
}

// The cascade decides once whether any JS listener can observe this event.
// Counts include IDL attributes and are decremented at tombstoning time, so a
// removed or once listener never keeps a later dispatch on the slow path.
static void note_listener_type(const char* type, int delta) {
    if (!type || !type[0] || delta == 0) return;
    if (!_type_counts && delta > 0) {
        _type_counts = event_type_count_new_with_free(16, event_type_count_entry_free);
    }
    if (!_type_counts) return;
    EventTypeCountEntry lookup = {type, 0};
    const EventTypeCountEntry* found =
        (const EventTypeCountEntry*)hashmap_get(_type_counts, &lookup);
    int old_count = found ? found->count : 0;
    int new_count = old_count + delta;
    if (new_count <= 0) {
        if (found) {
            const EventTypeCountEntry* removed =
                (const EventTypeCountEntry*)hashmap_delete(_type_counts, &lookup);
            if (removed && removed->type) mem_free((void*)removed->type);
        }
        return;
    }

    // Listener compaction frees listener.type. The dispatch index must own its
    // key so removing one listener cannot hide peers of the same event type.
    const char* owned_type = found ? found->type : mem_strdup(type, MEM_CAT_JS_RUNTIME);
    if (!owned_type) return;
    EventTypeCountEntry updated = {owned_type, new_count};
    hashmap_set(_type_counts, &updated);
    if (hashmap_oom(_type_counts)) {
        if (!found) mem_free((void*)owned_type);
        hashmap_free(_type_counts);
        _type_counts = nullptr;
        log_error("js-dom-events: listener type index allocation failed");
    }
}

static bool has_listener_type(const char* type) {
    if (!_type_counts || !type || !type[0]) return false;
    EventTypeCountEntry lookup = {type, 0};
    const EventTypeCountEntry* found =
        (const EventTypeCountEntry*)hashmap_get(_type_counts, &lookup);
    return found && found->count > 0;
}

static void tombstone_listener(EventListener* listener) {
    if (!listener || listener->removed) return;
    listener->removed = true;
    note_listener_type(listener->type, -1);
}

static void nl_push(NodeListeners* nl, EventListener listener) {
    if (nl->count >= nl->capacity) {
        int new_cap = nl->capacity == 0 ? 8 : nl->capacity * 2;
        EventListener* new_items = (EventListener*)mem_calloc(new_cap, sizeof(EventListener), MEM_CAT_JS_RUNTIME);
        if (nl->items && nl->count > 0) {
            memcpy(new_items, nl->items, nl->count * sizeof(EventListener));
            mem_free(nl->items);
        }
        nl->items = new_items;
        nl->capacity = new_cap;
    }
    nl->items[nl->count++] = listener;
}

static EventListener* nl_find_snapshot_listener(NodeListeners* nl, uint64_t order) {
    if (!nl) return nullptr;
    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (el->removed) continue;
        if (el->order == order) {
            return el;
        }
    }
    return nullptr;
}

static EventListener* nl_find_idl_listener(NodeListeners* nl, const char* type) {
    if (!nl || !type) return nullptr;
    for (int i = 0; i < nl->count; i++) {
        EventListener* listener = &nl->items[i];
        if (!listener->removed && listener->is_idl_handler && listener->type &&
            strcmp(listener->type, type) == 0) {
            return listener;
        }
    }
    return nullptr;
}

static bool event_handler_target_supported(Item target) {
    Item global = js_get_global_this();
    if (target.item != 0 && target.item == global.item) return true;
    if (js_dom_event_is_document_target(target)) return true;
    if (js_dom_unwrap_element(target)) return true;
    return get_type_id(target) == LMD_TYPE_MAP &&
           js_class_id(target) == JS_CLASS_EVENT_TARGET;
}

static void event_handler_property_set_for_key(void* key, Item target,
                                                const char* property_name,
                                                int property_name_len,
                                                Item value,
                                                DomDocument* owner_doc,
                                                DomNodeRef node_ref) {
    if (!property_name || property_name_len < 3 || property_name[0] != 'o' ||
        property_name[1] != 'n' || !key) {
        return;
    }

    char stack_type[64];
    int type_len = property_name_len - 2;
    if (type_len <= 0 || type_len >= (int)sizeof(stack_type)) return;
    memcpy(stack_type, property_name + 2, (size_t)type_len);
    stack_type[type_len] = '\0';

    if (owner_doc && node_ref.address && !dom_node_ref_validate(owner_doc, node_ref)) return;
    NodeListeners* listeners = find_listeners(key);
    if (!listeners) {
        listeners = get_or_create_listeners(key, owner_doc, node_ref, target);
    }
    if (!listeners) return;

    EventListener* handler = nl_find_idl_listener(listeners, stack_type);
    if (!js_is_callable(value)) {
        if (handler) tombstone_listener(handler);
        return;
    }
    if (handler) {
        *handler->callback_root = value.item;
        return;
    }

    char* type_copy = mem_strdup(stack_type, MEM_CAT_JS_RUNTIME);
    uint64_t* callback_root = heap_gc_root_slot_new(value.item);
    if (!type_copy || !callback_root) {
        if (type_copy) mem_free(type_copy);
        if (callback_root) {
            heap_unregister_gc_root(callback_root);
            mem_free(callback_root);
        }
        log_error("js-dom-events: failed to root '%s' handler", stack_type);
        return;
    }
    EventListener listener = {};
    listener.type = type_copy;
    listener.callback_root = callback_root;
    listener.order = ++_event_registration_order;
    listener.is_idl_handler = true;
    nl_push(listeners, listener);
    note_listener_type(listener.type, 1);
}

extern "C" void js_dom_event_handler_property_set(Item target,
                                                    const char* property_name,
                                                    int property_name_len,
                                                    Item value) {
    if (!js_dom_event_runtime_state_ensure() || !event_handler_target_supported(target)) return;
    void* key = get_event_target_key(target);
    DomNode* node = (DomNode*)js_dom_unwrap_element(target);
    DomDocument* owner_doc = node && node->is_element()
        ? node->as_element()->doc : nullptr;
    DomNodeRef node_ref = node ? dom_node_ref(node) : DomNodeRef{nullptr, 0};
    event_handler_property_set_for_key(key, target, property_name,
                                       property_name_len, value, owner_doc, node_ref);
}

extern "C" void js_dom_event_handler_property_set_for_node(
        void* dom_node, const char* property_name, int property_name_len, Item value) {
    if (!js_dom_event_runtime_state_ensure()) return;
    DomNode* node = (DomNode*)dom_node;
    DomDocument* owner_doc = node && node->is_element()
        ? node->as_element()->doc : nullptr;
    DomNodeRef node_ref = node ? dom_node_ref(node) : DomNodeRef{nullptr, 0};
    event_handler_property_set_for_key(dom_node, ItemNull, property_name,
                                       property_name_len, value, owner_doc, node_ref);
}

// ============================================================================
// Parse options argument
// ============================================================================

static Item parse_listener_options(Item opts, bool* capture, bool* once,
                                    bool* passive, bool* has_passive,
                                    Item* signal_out) {
    *capture = false;
    *once = false;
    *passive = false;
    *has_passive = false;
    *signal_out = ItemNull;

    if (opts.item == 0 || get_type_id(opts) == LMD_TYPE_NULL ||
        get_type_id(opts) == LMD_TYPE_UNDEFINED) {
        return ItemNull;
    }

    // boolean argument = useCapture. Per WebIDL, anything that is not a
    // dictionary (object/map) is converted via ToBoolean for the boolean union.
    TypeId tid = get_type_id(opts);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_OBJECT) {
        *capture = js_is_truthy(opts);
        return ItemNull;
    }

    // options object: {capture, once, passive, signal}
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT) {
        Item cap_key = js_name_item("capture");
        Item once_key = js_name_item("once");
        Item passive_key = js_name_item("passive");
        Item signal_key = js_name_item("signal");

        Item cap_val = js_get_key_default(opts, cap_key);
        Item once_val = js_get_key_default(opts, once_key);
        Item passive_val = js_get_key_default(opts, passive_key);
        Item signal_val = js_get_key_default(opts, signal_key);

        if (cap_val.item != 0 && get_type_id(cap_val) != LMD_TYPE_UNDEFINED)
            *capture = js_is_truthy(cap_val);
        if (once_val.item != 0 && get_type_id(once_val) != LMD_TYPE_UNDEFINED)
            *once = js_is_truthy(once_val);
        if (passive_val.item != 0 && get_type_id(passive_val) != LMD_TYPE_UNDEFINED) {
            *passive = js_is_truthy(passive_val);
            *has_passive = true;
        }
        if (signal_val.item != 0) {
            TypeId st = get_type_id(signal_val);
            if (st == LMD_TYPE_NULL) {
                // Per spec, signal must be an AbortSignal — null is a TypeError.
                Item n = js_name_item("TypeError");
                Item m = js_name_item(
                    "Failed to execute 'addEventListener' on 'EventTarget': "
                    "member signal is not of type 'AbortSignal'.");
                return js_throw_value(js_new_error_with_name(n, m));
            }
            if (st == LMD_TYPE_MAP || st == LMD_TYPE_OBJECT) {
                *signal_out = signal_val;
            }
        }
    }
    return ItemNull;
}

// returns true if signal_item is an already-aborted AbortSignal
static bool signal_is_aborted(Item signal_item) {
    if (signal_item.item == 0) return false;
    TypeId t = get_type_id(signal_item);
    if (t != LMD_TYPE_MAP && t != LMD_TYPE_OBJECT) return false;
    Item ab = js_get_key_cstr(signal_item, "aborted");
    return js_is_truthy(ab);
}

// ============================================================================
// addEventListener / removeEventListener
// ============================================================================

void js_dom_add_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item) {
    if (!js_dom_event_runtime_state_ensure()) return;
    const char* type = fn_to_cstr(type_item);
    if (!type) {
        log_debug("js_dom_add_event_listener: invalid type");
        return;
    }

    // Per spec the options flattening happens before any further checks; in
    // particular it must run even when the callback is null so getter side
    // effects (used by feature-detection code) fire.
    bool capture = false, once = false, passive = false, has_passive = false;
    Item signal = ItemNull;
    Item options_result = parse_listener_options(opts_item, &capture, &once, &passive,
        &has_passive, &signal);
    if (item_is_error(options_result)) return;

    // Per spec: addEventListener with null/undefined callback is a no-op.
    TypeId cb_tid = get_type_id(cb_item);
    if (cb_item.item == 0 || cb_tid == LMD_TYPE_NULL || cb_tid == LMD_TYPE_UNDEFINED) {
        return;
    }
    // Callback must be either a function or an object with handleEvent (checked
    // lazily at dispatch time). Reject obviously-bad types like numbers/booleans.
    if (cb_tid != LMD_TYPE_FUNC && cb_tid != LMD_TYPE_MAP &&
        cb_tid != LMD_TYPE_OBJECT && cb_tid != LMD_TYPE_ELEMENT) {
        log_debug("js_dom_add_event_listener: callback must be function or object (got tid=%d)", cb_tid);
        return;
    }

    // Per spec: if signal is an already-aborted AbortSignal, do not add.
    if (signal_is_aborted(signal)) {
        return;
    }

    void* key = get_event_target_key(elem_item);

    // HTML "default passive" rule: when `passive` is omitted in the options,
    // listeners for touchstart/touchmove/wheel/mousewheel on window /
    // document / documentElement / body default to passive=true.
    // https://dom.spec.whatwg.org/#default-passive-value
    if (!has_passive) {
        bool is_passive_event = (strcmp(type, "touchstart") == 0 ||
                                 strcmp(type, "touchmove") == 0 ||
                                 strcmp(type, "wheel") == 0 ||
                                 strcmp(type, "mousewheel") == 0);
        if (is_passive_event) {
            bool is_root_target = false;
            if (key == (void*)&_window_sentinel ||
                key == (void*)&_document_sentinel) {
                is_root_target = true;
            } else {
                DomElement* el = (DomElement*)js_dom_unwrap_element(elem_item);
                if (el && el->tag_name &&
                    (strcasecmp(el->tag_name, "html") == 0 ||
                     strcasecmp(el->tag_name, "body") == 0)) {
                    is_root_target = true;
                }
            }
            if (is_root_target) {
                passive = true;
                has_passive = true;
            }
        }
    }

    DomNode* event_node = (DomNode*)js_dom_unwrap_element(elem_item);
    DomDocument* event_doc = nullptr;
    if (event_node) {
        for (DomNode* current = event_node; current; current = current->parent) {
            if (current->is_element() && current->as_element()->doc) {
                event_doc = current->as_element()->doc;
                break;
            }
        }
        if (!event_doc) event_doc = (DomDocument*)js_dom_get_document();
    }
    DomNodeRef event_ref = event_node ? dom_node_ref(event_node) : DomNodeRef{nullptr, 0};
    if (event_node && (!event_doc || !dom_node_ref_validate(event_doc, event_ref))) return;
    NodeListeners* nl = get_or_create_listeners(
        key, event_doc, event_ref, elem_item);
    if (!nl) {
        log_error("js-dom-events: failed to retain listener target");
        return;
    }

    // check for duplicate (same type + callback + capture); ignore tombstones
    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (el->removed) continue;
        if (!el->is_idl_handler && strcmp(el->type, type) == 0 && el->capture == capture &&
            event_listener_root_item(el->callback_root).item == cb_item.item) {
            log_debug("js_dom_add_event_listener: duplicate listener for '%s', skipping", type);
            return;
        }
    }

    // allocate type string copy
    size_t type_len = strlen(type);
    char* type_copy = (char*)mem_calloc(1, type_len + 1, MEM_CAT_JS_RUNTIME);
    memcpy(type_copy, type, type_len);

    EventListener listener = {};
    listener.type = type_copy;
    // Listener storage lives outside the traced heap and can outlive the
    // registration turn. Stable root slots keep callbacks alive and receive
    // forwarding-pointer updates when a later event triggers collection.
    listener.callback_root = heap_gc_root_slot_new(cb_item.item);
    listener.signal_root = signal.item != 0 && get_type_id(signal) != LMD_TYPE_NULL
        ? heap_gc_root_slot_new(signal.item) : nullptr;
    if (!listener.callback_root ||
        (signal.item != 0 && get_type_id(signal) != LMD_TYPE_NULL && !listener.signal_root)) {
        event_listener_release_roots(&listener);
        mem_free(type_copy);
        log_error("js_dom_add_event_listener: failed to root '%s' listener", type);
        return;
    }
    listener.order = ++_event_registration_order;
    listener.capture = capture;
    listener.once = once;
    listener.passive = passive;

    nl_push(nl, listener);
    note_listener_type(listener.type, 1);
    log_debug("js_dom_add_event_listener: added '%s' listener (capture=%d, once=%d, passive=%d) on %p",
              type, (int)capture, (int)once, (int)passive, key);
}

void js_dom_remove_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item) {
    if (!js_dom_event_runtime_state_get()) return;
    const char* type = fn_to_cstr(type_item);
    if (!type) return;

    // removeEventListener only reads capture from options (per spec). Do NOT
    // read passive/signal getters here — feature-detection tests rely on this.
    bool capture = false;
    if (opts_item.item != 0) {
        TypeId opt_tid = get_type_id(opts_item);
        if (opt_tid == LMD_TYPE_MAP || opt_tid == LMD_TYPE_OBJECT) {
            Item cap_val = js_get_name_key(opts_item, "capture");
            if (cap_val.item != 0 && get_type_id(cap_val) != LMD_TYPE_UNDEFINED)
                capture = js_is_truthy(cap_val);
        } else {
            // Non-dictionary: ToBoolean.
            capture = js_is_truthy(opts_item);
        }
    }

    void* key = get_event_target_key(elem_item);
    NodeListeners* nl = find_listeners(key);
    if (!nl) return;

    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (el->removed) continue;
        if (!el->is_idl_handler && strcmp(el->type, type) == 0 && el->capture == capture &&
            event_listener_root_item(el->callback_root).item == cb_item.item) {
            // tombstone — actual storage is reclaimed at next opportunity.
            // This protects in-flight dispatch loops walking the array.
            tombstone_listener(el);
            log_debug("js_dom_remove_event_listener: removed '%s' listener from %p", type, key);
            return;
        }
    }
}

// Compact tombstoned listeners from a NodeListeners array. Safe to call only
// when no dispatch is walking this list.
static void nl_compact(NodeListeners* nl) {
    int w = 0;
    for (int r = 0; r < nl->count; r++) {
        if (nl->items[r].removed) {
            if (nl->items[r].type) mem_free(nl->items[r].type);
            event_listener_release_roots(&nl->items[r]);
            continue;
        }
        if (w != r) nl->items[w] = nl->items[r];
        w++;
    }
    nl->count = w;
}

// ============================================================================
// Event Object Creation
// ============================================================================

// all event field writers share one key materialization path; only the value
// representation varies, so keeping conversion outside this helper avoids
// five copies of the same property write and its allocation ordering.
static void event_set_value(Item event, const char* key, Item value) {
    js_set_name_key(event, key, value);
}

#define EVENT_SET_VALUE(name, type, expression) \
    static void name(Item event, const char* key, type value) { \
        event_set_value(event, key, expression); \
    }
EVENT_SET_VALUE(event_set_str, const char*, value ? js_name_item(value) : ItemNull)
EVENT_SET_VALUE(event_set_bool, bool, (Item){.item = b2it(value ? 1 : 0)})
EVENT_SET_VALUE(event_set_int, int, (Item){.item = i2it(value)})
EVENT_SET_VALUE(event_set_double, double, js_make_number(value))
EVENT_SET_VALUE(event_set_item, Item, value)
#undef EVENT_SET_VALUE

static void event_mark_non_writable(Item event, const char* key) {
    Item k = js_name_item(key);
    js_mark_non_writable(event, k);
}

// Get the current event flag values from per-event slots stored on the event
// object itself (so nested dispatches don't trample each other).
static bool event_flag_get(Item event, const char* key) {
    Item v = js_get_key_default(event, js_name_item(key));
    return js_is_truthy(v);
}

static bool js_event_is_object(Item event) {
    return radiant_dom_event_is(event);
}

// Per-event state and standard methods live on the native record, including
// the Jube-declared preventDefault/stopPropagation/composedPath entry points.

// initEvent(type, bubbles, cancelable) — legacy. No-op while dispatching.
extern "C" Item js_event_init_event(Item type_arg, Item b_arg, Item c_arg) {
    // Per spec, type is mandatory — throw TypeError if missing/undefined.
    TypeId tt = get_type_id(type_arg);
    if (type_arg.item == 0 || tt == LMD_TYPE_UNDEFINED) {
        Item n = js_name_item("TypeError");
        Item m = js_name_item(
            "Failed to execute 'initEvent' on 'Event': "
            "1 argument required, but only 0 present.");
        return js_throw_value(js_new_error_with_name(n, m));
    }
    Item ev = js_get_this();
    if (!js_event_is_object(ev)) return make_js_undefined();
    Item args[] = {type_arg, b_arg, c_arg};
    Item result = ItemNull;
    radiant_dom_event_call(ev, "initEvent", args, 3, &result);
    return make_js_undefined();
}

// initCustomEvent(type, bubbles, cancelable, detail) — legacy.
extern "C" Item js_event_init_custom_event(Item type_arg, Item b_arg, Item c_arg, Item detail_arg) {
    js_event_init_event(type_arg, b_arg, c_arg);
    Item ev = js_get_this();
    if (js_event_is_object(ev)) {
        // Per spec, omitted detail defaults to null (not undefined).
        TypeId dt = get_type_id(detail_arg);
        if (detail_arg.item == 0 || dt == LMD_TYPE_UNDEFINED) detail_arg = ItemNull;
        event_set_item(ev, "detail", detail_arg);
    }
    return make_js_undefined();
}

// initTextEvent(type, bubbles, cancelable, view, data, inputMethod, locale)
// Legacy WebKit/Blink editing tests still use this obsolete API.
extern "C" Item js_event_init_text_event(Item type_arg, Item b_arg,
        Item c_arg, Item view_arg, Item data_arg, Item input_method_arg,
        Item locale_arg) {
    js_event_init_event(type_arg, b_arg, c_arg);
    Item ev = js_get_this();
    if (!js_event_is_object(ev)) return make_js_undefined();
    if (event_flag_get(ev, "__dispatch_flag")) return make_js_undefined();

    TypeId vt = get_type_id(view_arg);
    if (view_arg.item == 0 || vt == LMD_TYPE_UNDEFINED)
        event_set_item(ev, "view", ItemNull);
    else
        event_set_item(ev, "view", view_arg);

    const char* data = fn_to_cstr(data_arg);
    event_set_str(ev, "data", data ? data : "");

    TypeId mt = get_type_id(input_method_arg);
    if (input_method_arg.item == 0 || mt == LMD_TYPE_UNDEFINED)
        event_set_int(ev, "inputMethod", 0);
    else {
        Item num = js_to_number(input_method_arg);
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_INT) event_set_int(ev, "inputMethod", (int)it2i(num));
        else if (nt == LMD_TYPE_INT64) event_set_int(ev, "inputMethod", (int)it2l(num));
        else if (nt == LMD_TYPE_FLOAT) event_set_int(ev, "inputMethod", (int)it2d(num));
        else event_set_int(ev, "inputMethod", 0);
    }

    const char* locale = fn_to_cstr(locale_arg);
    event_set_str(ev, "locale", locale ? locale : "");
    return make_js_undefined();
}

static Item js_create_event_init_with_class(const char* type, bool bubbles,
        bool cancelable, bool composed, JsClass class_id) {
    RootFrame roots(1);
    Rooted<Item> event_root(roots, radiant_dom_event_create(type, bubbles,
        cancelable, composed, (int)class_id));
    // Event construction performs many allocating property writes; keep the
    // partially initialized receiver precise until it is returned to JS.
    Item event = event_root.get();

    // The host carrier has no realm until JS constructs it. Capture the
    // intrinsic class prototype now, so Lambda-only dispatch never causes the
    // bridge to lazily construct JS intrinsics without a JS Input.
    radiant_dom_event_set_prototype_override(event,
        js_get_intrinsic_prototype_for_class((int)class_id));

    event_set_int(event, "eventPhase", 0);  // NONE initially
    event_set_double(event, "timeStamp", event_now_ms());

    // target / currentTarget / srcElement default to null.
    event_set_item(event, "target", ItemNull);
    event_set_item(event, "srcElement", ItemNull);
    event_set_item(event, "currentTarget", ItemNull);

    js_set_key_default(event, make_string_item("initEvent"),
                       js_new_native_function(js_event_init_event));

    // F17 projects legacy aliases directly from the native record. Per-wrapper
    // accessors would recreate a second cancellation state.

    event_apply_new_target_prototype(event);

    return event_root.get();
}

JS_FORWARD_ITEM(js_create_event_init,
    (const char* type, bool bubbles, bool cancelable, bool composed),
    js_create_event_init_with_class,
    (type, bubbles, cancelable, composed, JS_CLASS_EVENT))
JS_FORWARD_ITEM(js_create_event, (const char* type, bool bubbles, bool cancelable),
    js_create_event_init, (type, bubbles, cancelable, false))

Item js_create_text_event_init(const char* type, bool bubbles, bool cancelable,
                               bool composed, Item view, const char* data) {
    RootFrame roots(2);
    Rooted<Item> view_root(roots, view);
    Rooted<Item> event_root(roots, js_create_event_init_with_class(type, bubbles,
        cancelable, composed, JS_CLASS_EVENT));
    event_set_item(event_root.get(), "view", view_root.get().item ? view_root.get() : ItemNull);
    event_set_str(event_root.get(), "data", data ? data : "");
    event_set_int(event_root.get(), "inputMethod", 0);
    event_set_str(event_root.get(), "locale", "");
    Item ite_key = js_name_item("initTextEvent");
    js_set_key_default(event_root.get(), ite_key,
        js_new_native_function(js_event_init_text_event));
    return event_root.get();
}

Item js_create_custom_event_init(const char* type, bool bubbles, bool cancelable,
                                 bool composed, Item detail) {
    RootFrame roots(2);
    Rooted<Item> detail_root(roots, detail);
    Rooted<Item> event_root(roots, js_create_event_init_with_class(type, bubbles,
        cancelable, composed, JS_CLASS_CUSTOM_EVENT));
    event_set_item(event_root.get(), "detail", detail_root.get());
    Item ice_key = js_name_item("initCustomEvent");
    js_set_key_default(event_root.get(), ice_key,
        js_new_native_function(js_event_init_custom_event));
    return event_root.get();
}

// ============================================================================
// Generic EventTarget — plain JS object with addEventListener / removeEventListener
// / dispatchEvent methods bound such that `this` is the storage key.
// ============================================================================

extern "C" Item js_eventtarget_add_listener(Item type, Item callback, Item opts) {
    Item self = js_get_this();
    js_dom_add_event_listener(self, type, callback, opts);
    return make_js_undefined();
}

extern "C" Item js_eventtarget_remove_listener(Item type, Item callback, Item opts) {
    Item self = js_get_this();
    js_dom_remove_event_listener(self, type, callback, opts);
    return make_js_undefined();
}

extern "C" Item js_eventtarget_dispatch(Item event_item) {
    Item self = js_get_this();
    return js_dom_dispatch_event(self, event_item);
}

Item js_create_event_target(void) {
    Item et = js_new_object_with_class(JS_CLASS_EVENT_TARGET);
#define JS_EVENT_TARGET_METHODS(M) \
    M("addEventListener", js_eventtarget_add_listener) \
    M("removeEventListener", js_eventtarget_remove_listener) M("dispatchEvent", js_eventtarget_dispatch)
#define JS_EVENT_TARGET_INSTALL_METHOD(name, target) \
    js_set_key_default(et, make_string_item(name), js_new_native_function(target));
    JS_EVENT_TARGET_METHODS(JS_EVENT_TARGET_INSTALL_METHOD)
#undef JS_EVENT_TARGET_INSTALL_METHOD
#undef JS_EVENT_TARGET_METHODS
    return et;
}

// ============================================================================
// Event subclasses (UIEvent / MouseEvent / KeyboardEvent / FocusEvent /
// WheelEvent / CompositionEvent). Each is a thin wrapper that builds the
// base Event object then stamps in the dictionary fields with the spec
// defaults.
// ============================================================================

static Item read_init(Item init, const char* key) {
    if (init.item == 0) return ItemNull;
    TypeId t = get_type_id(init);
    if (t != LMD_TYPE_MAP && t != LMD_TYPE_OBJECT && t != LMD_TYPE_VMAP) return ItemNull;
    return js_get_name_key(init, key);
}

static bool init_present(Item v) {
    if (v.item == 0) return false;
    TypeId t = get_type_id(v);
    return t != LMD_TYPE_NULL && t != LMD_TYPE_UNDEFINED;
}

static bool init_bool(Item init, const char* key, bool def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    return js_is_truthy(v);
}

static int init_int(Item init, const char* key, int def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    Item num = js_to_number(v);
    TypeId t = get_type_id(num);
    if (t == LMD_TYPE_INT) return (int)it2i(num);
    if (t == LMD_TYPE_INT64) return (int)it2l(num);
    if (t == LMD_TYPE_FLOAT) return (int)it2d(num);
    return def;
}

static double init_double(Item init, const char* key, double def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    Item num = js_to_number(v);
    TypeId t = get_type_id(num);
    if (t == LMD_TYPE_FLOAT) return it2d(num);
    if (t == LMD_TYPE_INT) return (double)it2i(num);
    if (t == LMD_TYPE_INT64) return (double)it2l(num);
    return def;
}

static const char* init_str(Item init, const char* key, const char* def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    const char* s = fn_to_cstr(v);
    return s ? s : def;
}

static Item init_nullable_str_item(Item init, const char* key) {
    Item v = read_init(init, key);
    if (v.item == 0) return ItemNull;
    TypeId t = get_type_id(v);
    if (t == LMD_TYPE_NULL || t == LMD_TYPE_UNDEFINED) return ItemNull;
    const char* s = fn_to_cstr(v);
    return s ? js_name_item(s) : ItemNull;
}

static Item init_item(Item init, const char* key) {
    Item v = read_init(init, key);
    if (!init_present(v)) return ItemNull;
    return v;
}

// EventModifierInit dict members shared by Mouse/Keyboard.
static void stamp_modifiers(Item ev, Item init) {
    event_set_bool(ev, "ctrlKey",  init_bool(init, "ctrlKey", false));
    event_set_bool(ev, "shiftKey", init_bool(init, "shiftKey", false));
    event_set_bool(ev, "altKey",   init_bool(init, "altKey", false));
    event_set_bool(ev, "metaKey",  init_bool(init, "metaKey", false));
    event_set_bool(ev, "modifierAltGraph", init_bool(init, "modifierAltGraph", false));
    event_set_bool(ev, "modifierCapsLock", init_bool(init, "modifierCapsLock", false));
    event_set_bool(ev, "modifierFn", init_bool(init, "modifierFn", false));
    event_set_bool(ev, "modifierFnLock", init_bool(init, "modifierFnLock", false));
    event_set_bool(ev, "modifierHyper", init_bool(init, "modifierHyper", false));
    event_set_bool(ev, "modifierNumLock", init_bool(init, "modifierNumLock", false));
    event_set_bool(ev, "modifierScrollLock", init_bool(init, "modifierScrollLock", false));
    event_set_bool(ev, "modifierSuper", init_bool(init, "modifierSuper", false));
    event_set_bool(ev, "modifierSymbol", init_bool(init, "modifierSymbol", false));
    event_set_bool(ev, "modifierSymbolLock", init_bool(init, "modifierSymbolLock", false));
}

extern "C" Item js_event_get_modifier_state(Item key_arg) {
    Item ev = js_get_this();
    if (!js_event_is_object(ev)) return (Item){.item = ITEM_FALSE};
    const char* key = fn_to_cstr(key_arg);
    if (!key) return (Item){.item = ITEM_FALSE};
    char buf[64]; buf[0] = 0;
    if (strcmp(key, "Alt") == 0) snprintf(buf, sizeof(buf), "altKey");
    else if (strcmp(key, "Control") == 0) snprintf(buf, sizeof(buf), "ctrlKey");
    else if (strcmp(key, "Shift") == 0)   snprintf(buf, sizeof(buf), "shiftKey");
    else if (strcmp(key, "Meta") == 0)    snprintf(buf, sizeof(buf), "metaKey");
    else snprintf(buf, sizeof(buf), "modifier%s", key);
    Item v = js_get_name_key(ev, buf);
    return (Item){.item = (js_is_truthy(v)) ? ITEM_TRUE : ITEM_FALSE};
}

// Build a UIEvent base. `view` is constrained to be Window, null, or undefined
// (per IDL); throws TypeError if a non-Window/non-null value is supplied.
static Item build_ui_event(const char* type, Item init, const char* class_name) {
    JsClass class_id = js_class_from_name(class_name,
        class_name ? (int)strlen(class_name) : 0);
    Item ev = js_create_event_init_with_class(type ? type : "",
        init_bool(init, "bubbles", false),
        init_bool(init, "cancelable", false),
        init_bool(init, "composed", false), class_id);
    Item view = init_item(init, "view");
    // Per IDL view is Window? — we accept null/undefined or a value that looks
    // like the global window object. Reject other types with TypeError.
    if (init_present(view)) {
        TypeId vt = get_type_id(view);
        if (vt != LMD_TYPE_MAP && vt != LMD_TYPE_OBJECT && vt != LMD_TYPE_VMAP) {
            Item n = js_name_item("TypeError");
            Item m = js_name_item(
                "Failed to construct event: view member is not of type Window.");
            return js_throw_value(js_new_error_with_name(n, m));
        }
        event_set_item(ev, "view", view);
    } else {
        event_set_item(ev, "view", ItemNull);
    }
    event_set_int(ev, "detail", init_int(init, "detail", 0));
    return ev;
}

#define JS_DOM_UI_EVENT_CTOR(name, class_name) \
    extern "C" Item name(Item type_arg, Item init_arg) { \
        return build_ui_event(fn_to_cstr(type_arg), init_arg, class_name); \
    }
JS_DOM_UI_EVENT_CTOR(js_ctor_ui_event_fn, "UIEvent")

extern "C" Item js_ctor_focus_event_fn(Item type_arg, Item init_arg) {
    JS_ASSIGN_OR_RETURN(ev, build_ui_event(fn_to_cstr(type_arg), init_arg, "FocusEvent"));
    event_set_item(ev, "relatedTarget", init_item(init_arg, "relatedTarget"));
    return ev;
}

static Item js_ctor_mouse_event_with_class(Item type_arg, Item init_arg,
        const char* class_name) {
    JS_ASSIGN_OR_RETURN(ev, build_ui_event(fn_to_cstr(type_arg), init_arg, class_name));
    stamp_modifiers(ev, init_arg);
    event_set_double(ev, "screenX", init_double(init_arg, "screenX", 0.0));
    event_set_double(ev, "screenY", init_double(init_arg, "screenY", 0.0));
    event_set_double(ev, "clientX", init_double(init_arg, "clientX", 0.0));
    event_set_double(ev, "clientY", init_double(init_arg, "clientY", 0.0));
    event_set_double(ev, "pageX",   init_double(init_arg, "pageX", 0.0));
    event_set_double(ev, "pageY",   init_double(init_arg, "pageY", 0.0));
    event_set_double(ev, "x",       init_double(init_arg, "clientX", 0.0));
    event_set_double(ev, "y",       init_double(init_arg, "clientY", 0.0));
    event_set_double(ev, "offsetX", 0.0);
    event_set_double(ev, "offsetY", 0.0);
    event_set_double(ev, "movementX", init_double(init_arg, "movementX", 0.0));
    event_set_double(ev, "movementY", init_double(init_arg, "movementY", 0.0));
    event_set_int(ev, "button",  init_int(init_arg, "button", 0));
    event_set_int(ev, "buttons", init_int(init_arg, "buttons", 0));
    event_set_item(ev, "relatedTarget", init_item(init_arg, "relatedTarget"));
    js_set_name_key(ev, "getModifierState", js_new_native_function(js_event_get_modifier_state));
    return ev;
}

#define JS_DOM_MOUSE_EVENT_CTOR(name, class_name) \
    extern "C" Item name(Item type_arg, Item init_arg) { \
        return js_ctor_mouse_event_with_class(type_arg, init_arg, class_name); \
    }
JS_DOM_MOUSE_EVENT_CTOR(js_ctor_mouse_event_fn, "MouseEvent")
#undef JS_DOM_MOUSE_EVENT_CTOR

extern "C" Item js_ctor_wheel_event_fn(Item type_arg, Item init_arg) {
    JS_ASSIGN_OR_RETURN(ev, js_ctor_mouse_event_with_class(type_arg, init_arg, "WheelEvent"));
    event_set_double(ev, "deltaX", init_double(init_arg, "deltaX", 0.0));
    event_set_double(ev, "deltaY", init_double(init_arg, "deltaY", 0.0));
    event_set_double(ev, "deltaZ", init_double(init_arg, "deltaZ", 0.0));
    event_set_int(ev, "deltaMode", init_int(init_arg, "deltaMode", 0));
    event_set_int(ev, "DOM_DELTA_PIXEL", 0);
    event_set_int(ev, "DOM_DELTA_LINE", 1);
    event_set_int(ev, "DOM_DELTA_PAGE", 2);
    return ev;
}

extern "C" Item js_ctor_keyboard_event_fn(Item type_arg, Item init_arg) {
    JS_ASSIGN_OR_RETURN(ev, build_ui_event(fn_to_cstr(type_arg), init_arg, "KeyboardEvent"));
    stamp_modifiers(ev, init_arg);
    event_set_str(ev, "key",  init_str(init_arg, "key", ""));
    event_set_str(ev, "code", init_str(init_arg, "code", ""));
    event_set_int(ev, "location",     init_int(init_arg, "location", 0));
    event_set_bool(ev, "repeat",      init_bool(init_arg, "repeat", false));
    event_set_bool(ev, "isComposing", init_bool(init_arg, "isComposing", false));
    event_set_int(ev, "charCode",     init_int(init_arg, "charCode", 0));
    event_set_int(ev, "keyCode",      init_int(init_arg, "keyCode", 0));
    event_set_int(ev, "which",        init_int(init_arg, "which", 0));
    event_set_int(ev, "DOM_KEY_LOCATION_STANDARD", 0);
    event_set_int(ev, "DOM_KEY_LOCATION_LEFT", 1);
    event_set_int(ev, "DOM_KEY_LOCATION_RIGHT", 2);
    event_set_int(ev, "DOM_KEY_LOCATION_NUMPAD", 3);
    js_set_name_key(ev, "getModifierState", js_new_native_function(js_event_get_modifier_state));
    return ev;
}

extern "C" Item js_ctor_composition_event_fn(Item type_arg, Item init_arg) {
    JS_ASSIGN_OR_RETURN(ev, build_ui_event(fn_to_cstr(type_arg), init_arg, "CompositionEvent"));
    event_set_str(ev, "data", init_str(init_arg, "data", ""));
    return ev;
}

static Item js_create_native_event_init(bool bubbles, bool cancelable,
                                        bool composed);
static Item js_create_trusted_native_event(const char* type, Item init,
                                           Item (*ctor)(Item, Item));

extern "C" Item js_create_native_composition_event(const char* type,
    const char* data)
{
    // Boundary hover events are intentionally non-bubbling; setting the
    // factory default to true made ancestor mouseenter handlers fire twice.
    bool boundary_hover = type &&
        (strcmp(type, "mouseenter") == 0 || strcmp(type, "mouseleave") == 0);
    Item init = js_create_native_event_init(!boundary_hover, false, true);
    event_set_str(init, "data", data ? data : "");
    return js_create_trusted_native_event(type ? type : "compositionupdate", init,
                                          js_ctor_composition_event_fn);
}

// CE-7 (Radiant_Design_Content_Editable.md §6.1, §10): StaticRange
// constructor. Per Input Events Level 2 / DOM, a StaticRange is an
// immutable snapshot of {startContainer, startOffset, endContainer,
// endOffset}; `collapsed` is derived. We expose the four fields and
// `collapsed` directly on the constructed object, then mark those data
// properties non-writable so script cannot mutate the snapshot.
extern "C" Item js_ctor_static_range_fn(Item init) {
    // StaticRange has a branded immutable snapshot lane; leaving it as a plain
    // map makes InputEvent treat script-created boundaries as live DOM nodes.
    Item obj = js_new_object_with_class(JS_CLASS_STATIC_RANGE);
    Item start_container = init_item(init, "startContainer");
    Item end_container = init_item(init, "endContainer");
    int start_offset = init_int(init, "startOffset", 0);
    int end_offset = init_int(init, "endOffset", 0);
    event_set_item(obj, "startContainer", start_container);
    event_set_int (obj, "startOffset",    start_offset);
    event_set_item(obj, "endContainer",   end_container);
    event_set_int (obj, "endOffset",      end_offset);
    bool collapsed = (start_container.item == end_container.item) &&
                     (start_offset == end_offset);
    event_set_bool(obj, "collapsed", collapsed);
    event_mark_non_writable(obj, "startContainer");
    event_mark_non_writable(obj, "startOffset");
    event_mark_non_writable(obj, "endContainer");
    event_mark_non_writable(obj, "endOffset");
    event_mark_non_writable(obj, "collapsed");
    return obj;
}

static Item js_input_event_get_target_ranges(Item* args, int argc);
static void js_input_event_install_target_ranges(Item ev, Item target_ranges);

static Item js_input_event_snapshot_range(Item range) {
    Item init = js_new_object();
    event_set_item(init, "startContainer", init_item(range, "startContainer"));
    event_set_int(init, "startOffset", init_int(range, "startOffset", 0));
    event_set_item(init, "endContainer", init_item(range, "endContainer"));
    event_set_int(init, "endOffset", init_int(range, "endOffset", 0));
    return js_ctor_static_range_fn(init);
}

static bool js_input_event_is_static_range(Item range) {
    if (get_type_id(range) != LMD_TYPE_MAP) return false;
    return js_class_id(range) == JS_CLASS_STATIC_RANGE;
}
JS_FORWARD_STATIC_ITEM(js_input_event_throw_dom_exception, (const char* name, const char* message), js_throw_named_error_text, (name ? name : "InvalidStateError", message ? message : ""))

static Item js_input_event_live_target_ranges(Item target_ranges) {
    Item ranges = js_array_new(0);
    if (get_type_id(target_ranges) != LMD_TYPE_ARRAY) return ranges;

    int64_t len = js_array_length(target_ranges);
    for (int64_t i = 0; i < len; i++) {
        Item range = js_elements_get_int(target_ranges, i);
        TypeId range_type = get_type_id(range);
        if (range_type != LMD_TYPE_MAP &&
            range_type != LMD_TYPE_OBJECT &&
            range_type != LMD_TYPE_VMAP) {
            continue;
        }
        Item start_container = init_item(range, "startContainer");
        Item end_container = init_item(range, "endContainer");
        bool static_range = js_input_event_is_static_range(range);
        bool static_range_has_dom_boundary =
            js_dom_unwrap_element(start_container) ||
            js_dom_unwrap_element(end_container);
        if (static_range && !static_range_has_dom_boundary) {
            js_array_push(ranges, js_input_event_snapshot_range(range));
            continue;
        }
        const char* exc = nullptr;
        JS_ASSIGN_OR_RETURN(live_range, js_dom_create_live_range_from_boundaries(
            start_container,
            init_int(range, "startOffset", 0),
            end_container,
            init_int(range, "endOffset", 0),
            &exc));
        if (live_range.item == ItemNull.item) {
            return js_input_event_throw_dom_exception(exc ? exc : "InvalidStateError",
                "Invalid InputEvent targetRanges boundary");
        }
        js_array_push(ranges, live_range);
    }
    return ranges;
}

extern "C" Item js_ctor_input_event_fn(Item type_arg, Item init_arg) {
    JS_ASSIGN_OR_RETURN(ev, build_ui_event(fn_to_cstr(type_arg), init_arg, "InputEvent"));
    event_set_item(ev, "data", init_nullable_str_item(init_arg, "data"));
    event_set_str(ev, "inputType", init_str(init_arg, "inputType", ""));
    event_set_bool(ev, "isComposing", init_bool(init_arg, "isComposing", false));
    event_set_item(ev, "dataTransfer", init_item(init_arg, "dataTransfer"));
    JS_ASSIGN_OR_RETURN(target_ranges, js_input_event_live_target_ranges(init_item(init_arg, "targetRanges")));
    js_input_event_install_target_ranges(ev, target_ranges);
    return ev;
}

extern "C" Item js_ctor_pointer_event_fn(Item type_arg, Item init_arg) {
    JS_ASSIGN_OR_RETURN(ev, js_ctor_mouse_event_with_class(type_arg, init_arg,
        "PointerEvent"));
    event_set_int(ev, "pointerId", init_int(init_arg, "pointerId", 0));
    event_set_double(ev, "width",  init_double(init_arg, "width", 1.0));
    event_set_double(ev, "height", init_double(init_arg, "height", 1.0));
    event_set_double(ev, "pressure", init_double(init_arg, "pressure", 0.0));
    event_set_double(ev, "tangentialPressure", init_double(init_arg, "tangentialPressure", 0.0));
    event_set_int(ev, "tiltX", init_int(init_arg, "tiltX", 0));
    event_set_int(ev, "tiltY", init_int(init_arg, "tiltY", 0));
    event_set_int(ev, "twist", init_int(init_arg, "twist", 0));
    event_set_str(ev, "pointerType", init_str(init_arg, "pointerType", ""));
    event_set_bool(ev, "isPrimary",  init_bool(init_arg, "isPrimary", false));
    return ev;
}

static Item js_ctor_timing_event(Item type_arg, Item init_arg, JsClass class_id,
                                 const char* name_key) {
    RootFrame roots(3);
    Rooted<Item> type_root(roots, type_arg);
    Rooted<Item> init_root(roots, init_arg);
    Rooted<Item> event_root(roots, js_create_event_init_with_class(fn_to_cstr(type_root.get()),
        init_bool(init_root.get(), "bubbles", false),
        init_bool(init_root.get(), "cancelable", false),
        init_bool(init_root.get(), "composed", false), class_id));
    // Subclass property writes allocate after the Event initializer's root
    // frame closes, so the partially built receiver needs its own precise root (D5.4.3).
    Item ev = event_root.get();
    event_set_str(ev, name_key, init_str(init_root.get(), name_key, ""));
    event_set_double(ev, "elapsedTime", init_double(init_root.get(), "elapsedTime", 0.0));
    event_set_str(ev, "pseudoElement", init_str(init_root.get(), "pseudoElement", ""));
    return ev;
}

#define JS_DOM_TIMING_EVENT_CTOR(name, class_id, property_name) \
    extern "C" Item name(Item type_arg, Item init_arg) { \
        return js_ctor_timing_event(type_arg, init_arg, class_id, property_name); \
    }
JS_DOM_TIMING_EVENT_CTOR(js_ctor_transition_event_fn,
    JS_CLASS_TRANSITION_EVENT, "propertyName")
JS_DOM_TIMING_EVENT_CTOR(js_ctor_animation_event_fn,
    JS_CLASS_ANIMATION_EVENT, "animationName")
#undef JS_DOM_TIMING_EVENT_CTOR
#undef JS_DOM_UI_EVENT_CTOR

// Build a synthetic click MouseEvent (composed=true, bubbles=true, cancelable=true)
// for `HTMLElement.prototype.click()`. Per spec, all coordinate / button fields
// default to 0; modifiers all false; detail = 1.
extern "C" Item js_create_click_mouse_event(void) {
    Item init = js_create_native_event_init(true, true, true);
    event_set_int(init, "detail", 1);
    return js_ctor_mouse_event_fn(js_name_item("click"), init);
}

// ============================================================================
// Native event factories — entry points used by the Radiant input bridge.
// All set isTrusted=true (per spec, browser-fired events are trusted) and
// stamp standard EventInit defaults appropriate for each interface.
// ============================================================================

static Item js_create_native_event_init(bool bubbles, bool cancelable,
                                        bool composed) {
    Item init = js_new_object();
    event_set_bool(init, "bubbles", bubbles);
    event_set_bool(init, "cancelable", cancelable);
    event_set_bool(init, "composed", composed);
    return init;
}

static Item js_create_trusted_native_event(const char* type, Item init,
                                           Item (*ctor)(Item, Item)) {
    Item event = ctor(js_name_item(type ? type : ""), init);
    event_set_bool(event, "isTrusted", true);
    return event;
}

static void stamp_modifier_init(Item init, bool ctrl, bool shift, bool alt, bool meta) {
    event_set_bool(init, "ctrlKey",  ctrl);
    event_set_bool(init, "shiftKey", shift);
    event_set_bool(init, "altKey",   alt);
    event_set_bool(init, "metaKey",  meta);
}

static void stamp_client_coordinates(Item init, double client_x, double client_y,
                                     bool include_page) {
    event_set_double(init, "clientX", client_x);
    event_set_double(init, "clientY", client_y);
    event_set_double(init, "screenX", client_x);
    event_set_double(init, "screenY", client_y);
    if (include_page) {
        event_set_double(init, "pageX", client_x);
        event_set_double(init, "pageY", client_y);
    }
}

extern "C" Item js_create_native_mouse_event(const char* type,
    double client_x, double client_y,
    int button, int buttons,
    bool ctrl, bool shift, bool alt, bool meta,
    int detail, Item related_target)
{
    Item init = js_create_native_event_init(true, true, true);
    event_set_int(init, "detail", detail);
    stamp_client_coordinates(init, client_x, client_y, true);
    event_set_int(init, "button", button);
    event_set_int(init, "buttons", buttons);
    stamp_modifier_init(init, ctrl, shift, alt, meta);
    if (related_target.item != 0) {
        event_set_item(init, "relatedTarget", related_target);
    }
    return js_create_trusted_native_event(type, init, js_ctor_mouse_event_fn);
}
JS_FORWARD_VOID( js_event_set_timestamp, (Item event, double timestamp_ms), event_set_double, (event, "timeStamp", timestamp_ms))

extern "C" Item js_create_native_pointer_event(const char* type,
    double client_x, double client_y,
    int button, int buttons,
    bool ctrl, bool shift, bool alt, bool meta,
    const char* pointer_type, int pointer_id, bool is_primary)
{
    Item init = js_create_native_event_init(true, true, true);
    stamp_client_coordinates(init, client_x, client_y, true);
    event_set_int(init, "button", button);
    event_set_int(init, "buttons", buttons);
    stamp_modifier_init(init, ctrl, shift, alt, meta);
    event_set_int(init, "pointerId", pointer_id);
    event_set_double(init, "width", 1.0);
    event_set_double(init, "height", 1.0);
    event_set_double(init, "pressure", buttons ? 0.5 : 0.0);
    event_set_str(init, "pointerType", pointer_type ? pointer_type : "mouse");
    event_set_bool(init, "isPrimary", is_primary);
    return js_create_trusted_native_event(type, init, js_ctor_pointer_event_fn);
}

extern "C" Item js_create_native_css_event(const char* type,
    const char* detail_name, const char* detail_value, double elapsed_time)
{
    Item init = js_create_native_event_init(true, false, false);
    event_set_str(init, detail_name ? detail_name : "propertyName",
                  detail_value ? detail_value : "");
    event_set_double(init, "elapsedTime", elapsed_time);
    event_set_str(init, "pseudoElement", "");
    Item (*ctor)(Item, Item) = detail_name && strcmp(detail_name, "animationName") == 0
        ? js_ctor_animation_event_fn : js_ctor_transition_event_fn;
    return js_create_trusted_native_event(type, init, ctor);
}

extern "C" Item js_create_native_drag_event(const char* type,
    double client_x, double client_y, Item data_transfer,
    bool ctrl, bool shift, bool alt, bool meta)
{
    // DragEvent extends MouseEvent; reuse the MouseEvent ctor for geometry and
    // attach the shared DataTransfer afterward (as js_create_native_input_event
    // stamps fields onto the constructed event). button/buttons=0/1 mirror a
    // primary-button drag.
    Item ev = js_create_native_mouse_event(type, client_x, client_y,
        /*button=*/0, /*buttons=*/1, ctrl, shift, alt, meta,
        /*detail=*/0, ItemNull);
    if (data_transfer.item != 0 && data_transfer.item != ITEM_NULL) {
        event_set_item(ev, "dataTransfer", data_transfer);
    }
    return ev;
}

extern "C" Item js_create_native_keyboard_event(const char* type,
    const char* key, const char* code,
    int legacy_key_code,
    bool ctrl, bool shift, bool alt, bool meta,
    bool repeat)
{
    Item init = js_create_native_event_init(true, true, true);
    if (key) event_set_str(init, "key", key);
    if (code) event_set_str(init, "code", code);
    event_set_int(init, "keyCode", legacy_key_code);
    event_set_int(init, "which", legacy_key_code);
    event_set_bool(init, "repeat", repeat);
    stamp_modifier_init(init, ctrl, shift, alt, meta);
    return js_create_trusted_native_event(type, init, js_ctor_keyboard_event_fn);
}

extern "C" Item js_create_native_focus_event(const char* type, Item related_target) {
    // focus/blur do NOT bubble; focusin/focusout DO. Caller decides via type.
    bool bubbles = (type && (strcmp(type, "focusin") == 0 || strcmp(type, "focusout") == 0));
    Item init = js_create_native_event_init(bubbles, false, true);
    if (related_target.item != 0) {
        event_set_item(init, "relatedTarget", related_target);
    }
    return js_create_trusted_native_event(type, init, js_ctor_focus_event_fn);
}

// CE-3 follow-up (Radiant_Design_Content_Editable.md §6.1): Range-backed
// target list for `event.getTargetRanges()`. Script-created InputEvents store
// live Range wrappers so DOM mutations between construction and
// getTargetRanges() are reflected as StaticRange snapshots. Native editing
// events may also stash already-snapshotted StaticRange-shaped maps.
static Item js_input_event_get_target_ranges(Item* args, int argc) {
    (void)args; (void)argc;
    Item ev = js_get_this();
    if (!js_event_is_object(ev)) return js_array_new(0);
    Item stashed = js_get_name_key(ev, "__target_ranges");
    Item ranges = js_array_new(0);
    if (get_type_id(stashed) != LMD_TYPE_ARRAY) return ranges;

    int64_t len = js_array_length(stashed);
    for (int64_t i = 0; i < len; i++) {
        Item range = js_elements_get_int(stashed, i);
        TypeId range_type = get_type_id(range);
        // DOM-backed constructor ranges are promoted to live Range host
        // wrappers so mutations adjust their boundaries. Restricting this
        // snapshot path to plain maps silently dropped every live wrapper.
        if (range_type != LMD_TYPE_MAP &&
            range_type != LMD_TYPE_OBJECT &&
            range_type != LMD_TYPE_VMAP) {
            continue;
        }
        js_array_push(ranges, js_input_event_snapshot_range(range));
    }
    return ranges;
}

static void js_input_event_install_target_ranges(Item ev, Item target_ranges) {
    Item ranges = target_ranges;
    if (get_type_id(ranges) != LMD_TYPE_ARRAY) {
        ranges = js_array_new(0);
    }
    js_set_name_key(ev, "__target_ranges", ranges);
    Item gtr_key = js_name_item("getTargetRanges");
    js_set_key_default(ev, gtr_key,
        js_new_native_span_function(js_input_event_get_target_ranges));
}

extern "C" Item js_create_native_input_event(const char* type,
    const char* input_type, const char* data,
    bool is_composing, Item data_transfer, Item target_ranges)
{
    // Per Input Events Level 2 §3.2: `beforeinput` is cancelable, `input`
    // is not. Both bubble and are composed.
    bool is_beforeinput = (type && strcmp(type, "beforeinput") == 0);
    Item init = js_create_native_event_init(true, is_beforeinput, true);
    if (data) event_set_str(init, "data", data);
    else event_set_item(init, "data", ItemNull);
    event_set_str(init, "inputType", input_type ? input_type : "");
    event_set_bool(init, "isComposing", is_composing);
    if (data_transfer.item != 0) {
        event_set_item(init, "dataTransfer", data_transfer);
    }
    Item ev = js_create_trusted_native_event(type ? type : "beforeinput", init,
                                              js_ctor_input_event_fn);
    // Stash the StaticRange[] snapshot so getTargetRanges() can return it.
    // If caller passed ItemNull, store an empty array — the WPT
    // `input-events-get-target-ranges*` tests expect the method to always
    // return an array (possibly empty), never null.
    js_input_event_install_target_ranges(ev, target_ranges);
    return ev;
}

extern "C" Item js_create_native_wheel_event(const char* type,
    double client_x, double client_y,
    double delta_x, double delta_y,
    int buttons,
    bool ctrl, bool shift, bool alt, bool meta)
{
    Item init = js_create_native_event_init(true, true, true);
    stamp_client_coordinates(init, client_x, client_y, false);
    event_set_int(init, "buttons", buttons);
    event_set_double(init, "deltaX", delta_x);
    event_set_double(init, "deltaY", delta_y);
    event_set_int(init, "deltaMode", 0); // DOM_DELTA_PIXEL
    stamp_modifier_init(init, ctrl, shift, alt, meta);
    return js_create_trusted_native_event(type ? type : "wheel", init,
                                          js_ctor_wheel_event_fn);
}

// ============================================================================
// Legacy IE-style `window.event` plumbing for the Radiant inline-handler
// (`onclick="..."`) path. The bridge dispatch (`js_dom_dispatch_event`)
// already sets/restores `window.event` around its listener invocation.
// Inline handlers compiled by `collect_and_compile_event_handlers` take no
// `event` parameter, so the only way for handler bodies like
// `onclick="alert(event.type)"` to see the event is through this global.
// `js_set_window_event_for_legacy` returns the prior value (so the caller
// can restore it after invoking the handler).
// ============================================================================
extern "C" Item js_set_window_event_for_legacy(Item event) {
    Item global = js_get_global_this();
    Item event_key = js_name_item("event");
    Item prev = js_get_key_default(global, event_key);
    js_set_key_default(global, event_key, event);
    return prev;
}

extern "C" void js_restore_window_event_for_legacy(Item prev) {
    Item global = js_get_global_this();
    js_set_name_key(global, "event", prev);
}

// ============================================================================
// Event Dispatch (3-phase propagation)
// ============================================================================

// build propagation path from target to root
static int build_path(Item target, void** path, bool* path_is_dom, int max_path) {
    int count = 0;
    void* key = get_event_target_key(target);

    // Non-element document/window targets use the same canonical keys as
    // addEventListener(), so dispatchEvent() finds listeners registered on
    // document/window even when the JS object is a proxy/wrapper.
    if (key == (void*)&_document_sentinel) {
        if (count < max_path) { path_is_dom[count] = false; path[count++] = (void*)&_document_sentinel; }
        if (count < max_path) { path_is_dom[count] = false; path[count++] = (void*)&_window_sentinel; }
        return count;
    }
    if (key == (void*)&_window_sentinel) {
        if (count < max_path) { path_is_dom[count] = false; path[count++] = (void*)&_window_sentinel; }
        return count;
    }

    // start from target's DOM node
    void* node_ptr = js_dom_unwrap_element(target);
    if (!node_ptr) {
        // plain JS-object EventTarget
        path_is_dom[count] = false;
        path[count++] = key;
        return count;
    }

    DomNode* node = (DomNode*)node_ptr;

    // walk from target up to root
    DomNode* current = node;
    while (current && count < max_path) {
        path_is_dom[count] = true;
        path[count++] = (void*)current;
        current = current->parent;
    }

    // add document and window sentinels at the end (root of propagation)
    if (count < max_path) { path_is_dom[count] = false; path[count++] = (void*)&_document_sentinel; }
    if (count < max_path) { path_is_dom[count] = false; path[count++] = (void*)&_window_sentinel; }

    return count;
}

// wrap a path key back into an Item for currentTarget
static Item wrap_path_key(void* key, bool key_is_dom) {
    if (key == (void*)&_window_sentinel) {
        // window currentTarget is globalThis (the window object).
        return js_get_global_this();
    }
    if (key == (void*)&_document_sentinel) {
        return js_get_document_object_value();
    }
    // Plain JS-object EventTarget: key is a container pointer (Map/Object/VMap).
    // Test this before treating the key as a DomNode: js_dom_wrap_element
    // dereferences its input as a node, and generic EventTarget maps are not
    // layout nodes despite sharing this listener-key path.
    if (key && !key_is_dom) {
        Item it; it.item = 0; it.container = (Container*)key;
        TypeId tid = get_type_id(it);
        if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT || tid == LMD_TYPE_VMAP) {
            return it;
        }
    }
    Item dom = js_dom_wrap_element(key);
    if (dom.item != 0 && get_type_id(dom) != LMD_TYPE_NULL) return dom;
    return ItemNull;
}

// fire listeners on a specific node for a given phase. `reported_phase`, if
// non-zero, overrides the eventPhase value visible to listeners (used at the
// target node so capture-then-bubble sub-passes both report AT_TARGET).
static void set_event_dispatch_position(void* key, bool key_is_dom, Item event,
                                        int phase, int reported_phase = 0) {
    int visible_phase = reported_phase ? reported_phase : phase;
    Item current_target = wrap_path_key(key, key_is_dom);
    event_set_int(event, "eventPhase", visible_phase);
    js_set_name_key(event, "currentTarget", current_target);
    radiant_dom_event_set_lambda_dispatch_position(event, current_target, visible_phase);
}

static void fire_listeners(void* key, const char* type, Item event, int phase,
                           bool key_is_dom, int reported_phase = 0) {
    RootFrame roots(5);
    Rooted<Item> event_root(roots, event);
    Rooted<Item> callback_root(roots, ItemNull);
    Rooted<Item> this_root(roots, ItemNull);
    Rooted<Item> result_root(roots, ItemNull);
    Rooted<Item> err_root(roots, ItemNull);
    NodeListeners* nl = find_listeners(key);
    if (!nl || nl->count == 0) return;

    set_event_dispatch_position(key, key_is_dom, event_root.get(), phase,
                                reported_phase);

    // Check stop-immediate flag against per-event slot.
    #define _STOP_IMM event_flag_get(event_root.get(), "__stop_imm")

    // Build a value snapshot: addEventListener() can grow and reallocate the
    // listener array during dispatch, so snapshots must not point into it.
    uint64_t* snap = nl && nl->count > 0
        ? (uint64_t*)alloca(sizeof(uint64_t) * nl->count)
        : nullptr;
    int snap_count = 0;
    for (int i = 0; nl && i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (el->removed) continue;
        if (strcmp(el->type, type) != 0) continue;
        if (el->is_idl_handler && phase == 1) continue;
        // phase filter — capturing fires only capture listeners; bubbling only
        // non-capture; AT_TARGET fires both.
        if (phase == 1 && !el->capture) continue;
        if (phase == 3 && el->capture) continue;
        // signal — if its AbortSignal aborted, treat as removed
        Item listener_signal = event_listener_root_item(el->signal_root);
        if (signal_is_aborted(listener_signal)) { tombstone_listener(el); continue; }
        snap[snap_count++] = el->order;
    }

    for (int i = 0; i < snap_count; i++) {
        if (_STOP_IMM) break;
        NodeListeners* live_nl = find_listeners(key);
        EventListener* live = nl_find_snapshot_listener(live_nl, snap[i]);
        // re-check tombstone in case a prior listener removed this one
        if (!live) continue;
        Item live_signal = event_listener_root_item(live->signal_root);
        if (signal_is_aborted(live_signal)) { tombstone_listener(live); continue; }

        // Resolve callback — function or {handleEvent}
        // A prior listener may collect and move heap objects. Reload from the
        // stable root instead of using the raw snapshot Item, or the remaining
        // listeners disappear from the same dispatch.
        Item callback = event_listener_root_item(live->callback_root);
        Item this_for_call = wrap_path_key(key, key_is_dom);
        if (!live->is_idl_handler && !js_is_callable(callback)) {
            // EventListener WebIDL: if value is an object, call handleEvent on it
            Item he = js_get_name_key(callback, "handleEvent");
            if (!js_is_callable(he)) continue;
            // per spec, `this` is the EventListener object itself
            this_for_call = callback;
            callback = he;
        }
        callback_root.set(callback);
        this_root.set(this_for_call);

        // Set passive flag on the event so preventDefault no-ops within this
        // listener (per HTML spec, passive listeners cannot cancel).
        bool was_passive = event_flag_get(event_root.get(), "__in_passive");
        event_set_bool(event_root.get(), "__in_passive", live->passive);

        // Mark for once-removal BEFORE invocation so that recursion / re-add
        // sees the slot as removed.
        if (live->once) tombstone_listener(live);

        // call the callback with event as argument; isolate exceptions per spec
        Item args[1] = { event_root.get() };
        // Callback allocation can collect the unhandled error lane before the
        // native dispatcher reports it, so retain its return in this frame.
        result_root.set(js_call_function_into(callback_root.get(), this_root.get(),
            args, 1, result_root.home()));
        if (item_is_error(result_root.get())) {
            err_root.set(js_error_lane_payload(result_root.get()));
            log_event_exception_detail(live->is_idl_handler
                ? "event handler" : "event listener", type, err_root.get());
            report_exception_to_window_onerror(err_root.get(), type);
        } else if (live->is_idl_handler &&
                   get_type_id(result_root.get()) == LMD_TYPE_BOOL &&
                   !it2b(result_root.get())) {
            radiant_dom_event_prevent_default(event_root.get());
        }

        // restore previous passive context
        event_set_bool(event_root.get(), "__in_passive", was_passive);
    }

    #undef _STOP_IMM
}

Item js_dom_dispatch_event(Item elem_item, Item event_item) {
    RootFrame roots(4);
    Rooted<Item> elem_root(roots, elem_item);
    Rooted<Item> event_root(roots, event_item);
    Rooted<Item> global_root(roots, ItemNull);
    Rooted<Item> previous_global_event_root(roots, ItemNull);
    elem_item = elem_root.get();
    event_item = event_root.get();
    if (!js_dom_event_runtime_state_ensure()) return (Item){.item = ITEM_FALSE};
    // Per spec: dispatchEvent(null) / dispatchEvent(non-Event) throws TypeError.
    TypeId evt_tid = get_type_id(event_item);
    if (event_item.item == 0 || evt_tid == LMD_TYPE_NULL ||
        evt_tid == LMD_TYPE_UNDEFINED) {
        Item n = js_name_item("TypeError");
        Item m = js_name_item(
            "Failed to execute 'dispatchEvent' on 'EventTarget': "
            "parameter 1 is not of type 'Event'.");
        return js_throw_value(js_new_error_with_name(n, m));
    }
    // get event type
    Item type_val = js_get_name_key(event_item, "type");
    const char* type = fn_to_cstr(type_val);
    if (!type) {
        log_error("js_dom_dispatch_event: event has no type");
        return (Item){.item = ITEM_FALSE};
    }

    // F19/ES25: direct DOM dispatch enters the same native scope as trusted
    // input. The re-entry guard keeps the shared propagation walk below as the
    // single engine for both paths instead of creating a JS-only default tier.
    if (radiant_dom_event_is(event_item) &&
        !radiant_synthetic_dom_dispatch_is_reentry(event_item) &&
        js_dom_unwrap_element(elem_item)) {
        Item unified_result = radiant_dispatch_synthetic_dom_event(elem_item,
                                                                    event_item);
        if (unified_result.item != ITEM_NULL) return unified_result;
    }

    // get bubbles flag
    Item bubbles_val = js_get_name_key(event_item, "bubbles");
    bool bubbles = js_is_truthy(bubbles_val);

    // Spec: throw InvalidStateError DOMException if event is already being dispatched.
    if (event_flag_get(event_item, "__dispatch_flag")) {
        Item n = js_name_item("InvalidStateError");
        Item m = js_name_item(
            "Failed to execute 'dispatchEvent' on 'EventTarget': "
            "The event is already being dispatched.");
        return js_throw_value(js_new_error_with_name(n, m));
    }

    // dispatch retargets constructor null placeholders on every dispatch.
    js_set_name_key(event_item, "target", elem_item);
    js_set_name_key(event_item, "srcElement", elem_item);

    // Mark event as dispatching.
    event_set_bool(event_item, "__dispatch_flag", true);

    // Per DOM spec, the propagation flags (stop, stop-immediate, canceled)
    // are NOT reset at the start of dispatch. They persist whether set
    // before-dispatch (legacy: stopPropagation()/preventDefault() called
    // before dispatchEvent) or by a previous re-dispatch.

    // build propagation path (target → ... → document → window)
    void* path[128];
    bool path_is_dom[128] = {};
    // DOM node ids can numerically equal Lambda TypeIds, so carry origin
    // metadata with each path entry instead of reinterpreting a DomNode pointer
    // as a generic EventTarget container during bubbling.
    int path_len = build_path(elem_item, path, path_is_dom, 128);

    if (path_len == 0) {
        event_set_item(event_item, "__dispatch_path", ItemNull);
        event_set_bool(event_item, "__dispatch_flag", false);
        return (Item){.item = ITEM_TRUE};
    }

    Item dispatch_path = js_array_new(0);
    for (int i = 0; i < path_len; i++) {
        js_array_push(dispatch_path, wrap_path_key(path[i], path_is_dom[i]));
    }
    // Store the exact path before invoking listeners: DOM mutations during
    // dispatch must not rewrite composedPath(), and generic targets/window
    // cannot be reconstructed from a DOM parent chain.
    event_set_item(event_item, "__dispatch_path", dispatch_path);

    // Legacy IE-style `window.event`: set to the in-flight event for the
    // duration of dispatch, restored to its prior value (typically
    // `undefined`) afterwards. Per HTML, the slot must read `undefined`
    // when called inside a Shadow Tree listener (we don't model Shadow
    // DOM headlessly, so we always set it).
    Item global = js_get_global_this();
    global_root.set(global);
    Item event_key = js_name_item("event");
    Item prev_global_event = js_get_key_default(global_root.get(), event_key);
    previous_global_event_root.set(prev_global_event);
    js_set_key_default(global_root.get(), event_key, event_root.get());

    #define _STOP_PROP event_flag_get(event_item, "__stop_prop")
    #define _STOP_IMM event_flag_get(event_item, "__stop_imm")
    // F18: masks are captured once for this cascade. A miss means the
    // corresponding store cannot contribute at any path node this dispatch.
    bool js_live = has_listener_type(type);
    bool author_live = radiant_author_template_event_live(type);
    bool author_cascade = author_live &&
        radiant_author_template_dispatch_begin(event_item);
    if (!author_cascade) author_live = false;

    // path[0] = target, path[1] = parent, ... path[n-1] = window
    // Phase 1: Capture — from root down to target (exclusive)
    for (int i = path_len - 1; i > 0; i--) {
        if (_STOP_PROP) break;
        if (js_live) {
            fire_listeners(path[i], type, event_item, 1, path_is_dom[i]);
        }
    }

    // Phase 2: Target — per spec, capture-listeners run first then bubble
    // listeners, both reported with eventPhase = AT_TARGET (2). A plain stop
    // still permits peers on this node; only immediate stop suppresses them.
    bool target_reached = !_STOP_PROP;
    if (target_reached) {
        if (js_live) {
            fire_listeners(path[0], type, event_item, 1, path_is_dom[0], 2);
        }
    }
    if (target_reached && !_STOP_IMM) {
        if (js_live) {
            fire_listeners(path[0], type, event_item, 3, path_is_dom[0], 2);
        }
    }
    if (target_reached && author_live && path_is_dom[0] && !_STOP_IMM) {
        set_event_dispatch_position(path[0], path_is_dom[0], event_item, 3, 2);
        radiant_dispatch_author_template_participant(path[0], event_item, type);
    }

    // Phase 3: Bubble — from target parent up to root
    if (bubbles) {
        for (int i = 1; i < path_len; i++) {
            if (_STOP_PROP) break;
            if (js_live) {
                fire_listeners(path[i], type, event_item, 3, path_is_dom[i]);
            }
            if (author_live && path_is_dom[i] && !_STOP_IMM) {
                set_event_dispatch_position(path[i], path_is_dom[i], event_item, 3);
                radiant_dispatch_author_template_participant(path[i], event_item, type);
            }
        }
    }

    if (author_cascade) radiant_author_template_dispatch_end(event_item);
    #undef _STOP_IMM
    #undef _STOP_PROP

    // set eventPhase to NONE after dispatch
    event_set_int(event_item, "eventPhase", 0);

    // currentTarget is reset to null after dispatch (per spec).
    js_set_name_key(event_item, "currentTarget", ItemNull);
    radiant_dom_event_clear_lambda_dispatch_position(event_item);

    // Per DOM spec §2.10 step 26: at the end of dispatch, unset stop
    // propagation flag, stop immediate propagation flag, and dispatch flag.
    // (canceled / defaultPrevented flag PERSISTS across dispatches.)
    event_set_bool(event_item, "__stop_prop", false);
    event_set_bool(event_item, "__stop_imm", false);
    event_set_bool(event_item, "cancelBubble", false);

    // Clear dispatching flag.
    event_set_bool(event_item, "__dispatch_flag", false);
    event_set_item(event_item, "__dispatch_path", ItemNull);

    // Restore the previous `window.event` value (legacy IE-style).
    js_set_key_default(global_root.get(), event_key, previous_global_event_root.get());

    // Compact tombstoned listeners now that dispatch is done. Walk all
    // touched nodes in the path.
    for (int i = 0; i < path_len; i++) {
        NodeListeners* nl = find_listeners(path[i]);
        if (nl) nl_compact(nl);
    }

    bool prevented = event_flag_get(event_item, "__default_prevented");
    if (prevented) {
        event_set_bool(event_item, "defaultPrevented", true);
        event_set_bool(event_item, "returnValue", false);
    }

    log_debug("js_dom_dispatch_event: dispatched '%s' on %p (prevented=%d)",
              type, (void*)path[0], (int)prevented);

    // dispatchEvent returns false only when the event is cancelable AND
    // preventDefault was called.
    Item cancelable = js_get_key_cstr(event_item, "cancelable");
    bool ret_false = prevented && js_is_truthy(cancelable);
    return (Item){.item = ret_false ? ITEM_FALSE : ITEM_TRUE};
}

// ============================================================================
// Lifecycle
// ============================================================================

void js_dom_events_reset(void) {
    if (!js_dom_event_runtime_state_get()) return;
    for (int i = 0; i < _entry_count; i++) {
        NodeListeners* nl = &_entries[i].listeners;
        for (int j = 0; j < nl->count; j++) {
            if (nl->items[j].type) mem_free(nl->items[j].type);
            event_listener_release_roots(&nl->items[j]);
        }
        if (nl->items) mem_free(nl->items);
        if (_entries[i].owner_doc && _entries[i].node_ref.address) {
            dom_node_unpin(_entries[i].owner_doc, _entries[i].node_ref,
                           DOM_NODE_PIN_EVENT_QUEUE);
        }
        if (_entries[i].target_root) {
            heap_unregister_gc_root(_entries[i].target_root);
            mem_free(_entries[i].target_root);
            _entries[i].target_root = nullptr;
        }
    }
    if (_entries) {
        mem_free(_entries);
        _entries = nullptr;
    }
    _entry_count = 0;
    _entry_capacity = 0;
    if (_entry_index) {
        hashmap_free(_entry_index);
        _entry_index = nullptr;
    }
    if (_type_counts) {
        hashmap_free(_type_counts);
        _type_counts = nullptr;
    }
    _event_registration_order = 0;
}

#undef js_dom_event_state
#undef _entries
#undef _entry_count
#undef _entry_capacity
#undef _entry_index
#undef _type_counts
#undef _event_registration_order

extern "C" void js_dom_events_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->dom_event_state) return;
    JsDomEventRuntimeState* state =
        (JsDomEventRuntimeState*)runtime_state->dom_event_state;
    // js_runtime_state_release_heap_resources() resets listener roots before
    // heap destruction; only the empty context-local capsule remains here.
    if (state->entries || state->entry_index) {
        log_error("js-dom-events: context destroyed before listener roots were released");
    }
    mem_free(state);
    runtime_state->dom_event_state = nullptr;
}
