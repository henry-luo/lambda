#include "js_dom_observers.h"
#include "js_dom.h"
#include "js_runtime.h"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_lifecycle.hpp"
#include "../../radiant/view.hpp"
#include "../../lib/log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define JS_OBSERVER_CAP 64
#define JS_OBSERVER_TARGET_CAP 32

typedef enum JsObserverKind {
    JS_OBSERVER_MUTATION,
    JS_OBSERVER_RESIZE,
    JS_OBSERVER_INTERSECTION
} JsObserverKind;

typedef struct JsObserverTarget {
    DomNode* node;
    DomDocument* owner_doc;
    DomNodeRef node_ref;
    bool child_list;
    bool attributes;
    bool character_data;
    bool subtree;
    bool attribute_old_value;
    bool character_data_old_value;
    char attribute_filter[8][64];
    int attribute_filter_count;
    DomNode* transient_roots[8];
    DomNodeRef transient_refs[8];
    int transient_root_count;
    float last_width;
    float last_height;
    float last_ratio;
    bool last_intersecting;
    bool sampled;
} JsObserverTarget;

typedef struct JsObserverState {
    Item object;
    Item callback;
    JsObserverKind kind;
    DomElement* root;
    DomNodeRef root_ref;
    float root_margin[4];
    bool root_margin_percent[4];
    float thresholds[16];
    int threshold_count;
    JsObserverTarget targets[JS_OBSERVER_TARGET_CAP];
    int target_count;
} JsObserverState;

static JsObserverState observers[JS_OBSERVER_CAP] = {};
static int observer_count = 0;
static bool observer_delivery_scheduled = false;
static uint64_t observer_roots_epoch = 0;
extern __thread EvalContext* context;
extern "C" uint64_t js_get_heap_epoch(void);
extern "C" void heap_register_gc_root(uint64_t* slot);

static Item js_geometry_observer_initial_sample(void);

extern Item js_make_number(double value);

static void observer_register_roots(void) {
    uint64_t epoch = js_get_heap_epoch();
    if (observer_roots_epoch == epoch) return;
    for (int i = 0; i < JS_OBSERVER_CAP; i++) {
        heap_register_gc_root(&observers[i].object.item);
        heap_register_gc_root(&observers[i].callback.item);
    }
    observer_roots_epoch = epoch;
}

static Item observer_key(const char* name) {
    return js_make_string(name);
}

static Item observer_pending(JsObserverState* observer) {
    return js_property_get(observer->object, observer_key("__lambdaObserverRecords"));
}

static void observer_replace_pending(JsObserverState* observer) {
    js_property_set(observer->object, observer_key("__lambdaObserverRecords"), js_array_new(0));
}

static JsObserverState* observer_from_this(void) {
    Item receiver = js_get_this();
    for (int i = 0; i < observer_count; i++) {
        if (observers[i].object.item == receiver.item) return &observers[i];
    }
    return nullptr;
}

static DomDocument* observer_node_document(DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current->is_element() && current->as_element()->doc) {
            return current->as_element()->doc;
        }
    }
    return (DomDocument*)js_dom_get_document();
}

static bool observer_pin_node(DomDocument* doc, DomNode* node, DomNodeRef* out) {
    if (!doc || !node || !out) return false;
    *out = dom_node_ref(node);
    if (!dom_node_ref_validate(doc, *out)) return false;
    return dom_node_pin(doc, *out, DOM_NODE_PIN_OBSERVER);
}

static void observer_release_target(JsObserverTarget* target) {
    if (!target) return;
    if (target->owner_doc && target->node_ref.address) {
        dom_node_unpin(target->owner_doc, target->node_ref, DOM_NODE_PIN_OBSERVER);
    }
    for (int i = 0; i < target->transient_root_count; i++) {
        if (target->owner_doc && target->transient_refs[i].address) {
            dom_node_unpin(target->owner_doc, target->transient_refs[i],
                           DOM_NODE_PIN_OBSERVER);
        }
    }
    memset(target, 0, sizeof(*target));
}

static void observer_release_transient_roots(JsObserverTarget* target) {
    if (!target) return;
    for (int i = 0; i < target->transient_root_count; i++) {
        if (target->owner_doc && target->transient_refs[i].address) {
            dom_node_unpin(target->owner_doc, target->transient_refs[i],
                           DOM_NODE_PIN_OBSERVER);
        }
    }
    memset(target->transient_roots, 0, sizeof(target->transient_roots));
    memset(target->transient_refs, 0, sizeof(target->transient_refs));
    target->transient_root_count = 0;
}

static JsObserverState* observer_create(JsObserverKind kind, Item callback) {
    if (!js_is_callable(callback)) {
        js_throw_type_error("Observer callback must be callable");
        return nullptr;
    }
    if (observer_count >= JS_OBSERVER_CAP) {
        log_error("dom-observer: observer capacity %d exhausted", JS_OBSERVER_CAP);
        return nullptr;
    }
    observer_register_roots();
    RootFrame roots((Context*)context, 2);
    Rooted<Item> callback_root(roots, callback);
    Rooted<Item> object_root(roots, ItemNull);
    JsObserverState* observer = &observers[observer_count++];
    memset(observer, 0, sizeof(*observer));
    observer->kind = kind;
    observer->callback = callback_root.get();
    object_root.set(js_new_object());
    observer->object = object_root.get();
    // Native state is indexed by object identity; keeping callback and records
    // on that object makes the GC ownership match the observable lifetime.
    js_property_set(observer->object, observer_key("__lambdaObserverCallback"), callback_root.get());
    observer_replace_pending(observer);
    return observer;
}

static bool observer_option_bool(Item options, const char* name) {
    TypeId type = get_type_id(options);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return false;
    return js_is_truthy(js_property_get(options, observer_key(name)));
}

static Item observer_option(Item options, const char* name) {
    TypeId type = get_type_id(options);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) {
        return ItemNull;
    }
    return js_property_get(options, observer_key(name));
}

static int observer_parse_root_margin(const char* text, float* values,
                                      bool* percents) {
    if (!text || !values || !percents) return 0;
    float parsed_values[4] = {};
    bool parsed_percents[4] = {};
    int count = 0;
    const char* cursor = text;
    while (*cursor && count < 4) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') cursor++;
        if (!*cursor) break;
        char* end = nullptr;
        double value = strtod(cursor, &end);
        if (end == cursor) return 0;
        cursor = end;
        bool percent = *cursor == '%';
        if (percent) {
            cursor++;
        } else if (cursor[0] == 'p' && cursor[1] == 'x') {
            cursor += 2;
        } else if (value != 0.0) {
            return 0;
        }
        parsed_values[count] = (float)value;
        parsed_percents[count] = percent;
        count++;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') cursor++;
    }
    if (*cursor || count == 0) return 0;

    int source[4] = {0, 0, 0, 0};
    if (count == 2) {
        source[1] = source[3] = 1;
    } else if (count == 3) {
        source[1] = source[3] = 1;
        source[2] = 2;
    } else if (count == 4) {
        source[1] = 1;
        source[2] = 2;
        source[3] = 3;
    }
    for (int i = 0; i < 4; i++) {
        values[i] = parsed_values[source[i]];
        percents[i] = parsed_percents[source[i]];
    }
    return count;
}

static void observer_add_threshold(JsObserverState* observer, double value) {
    if (!observer || value < 0.0 || value > 1.0 ||
        observer->threshold_count >= 16) return;
    int insert = observer->threshold_count;
    while (insert > 0 && observer->thresholds[insert - 1] > value) {
        observer->thresholds[insert] = observer->thresholds[insert - 1];
        insert--;
    }
    observer->thresholds[insert] = (float)value;
    observer->threshold_count++;
}

static void observer_parse_thresholds(JsObserverState* observer, Item options) {
    Item threshold = observer_option(options, "threshold");
    if (get_type_id(threshold) == LMD_TYPE_ARRAY) {
        int64_t count = js_array_length(threshold);
        for (int64_t i = 0; i < count; i++) {
            Item number = js_to_number(js_array_get_int(threshold, i));
            if (get_type_id(number) == LMD_TYPE_INT) {
                observer_add_threshold(observer, (double)it2i(number));
            } else if (get_type_id(number) == LMD_TYPE_FLOAT) {
                observer_add_threshold(observer, it2d(number));
            }
        }
    } else {
        Item number = js_to_number(threshold);
        if (get_type_id(number) == LMD_TYPE_INT) {
            observer_add_threshold(observer, (double)it2i(number));
        } else if (get_type_id(number) == LMD_TYPE_FLOAT) {
            observer_add_threshold(observer, it2d(number));
        }
    }
    if (observer->threshold_count == 0) observer_add_threshold(observer, 0.0);
}

static JsObserverTarget* observer_find_target(JsObserverState* observer, DomNode* node) {
    for (int i = 0; i < observer->target_count; i++) {
        if (observer->targets[i].node == node) return &observer->targets[i];
    }
    return nullptr;
}

static Item js_observer_disconnect(void) {
    JsObserverState* observer = observer_from_this();
    if (observer) {
        for (int i = 0; i < observer->target_count; i++) {
            observer_release_target(&observer->targets[i]);
        }
        observer->target_count = 0;
        observer_replace_pending(observer);
    }
    return make_js_undefined();
}

static Item js_observer_take_records(void) {
    JsObserverState* observer = observer_from_this();
    if (!observer) return js_array_new(0);
    Item records = observer_pending(observer);
    observer_replace_pending(observer);
    return records;
}

static Item js_mutation_observer_observe(Item target_item, Item options) {
    JsObserverState* observer = observer_from_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(target_item);
    if (!observer || observer->kind != JS_OBSERVER_MUTATION || !node) {
        return make_js_undefined();
    }
    bool child_list = observer_option_bool(options, "childList");
    bool attributes = observer_option_bool(options, "attributes");
    bool character_data = observer_option_bool(options, "characterData");
    bool attribute_old_value = observer_option_bool(options, "attributeOldValue");
    bool character_data_old_value = observer_option_bool(options, "characterDataOldValue");
    if (attribute_old_value) attributes = true;
    if (character_data_old_value) character_data = true;
    if (!child_list && !attributes && !character_data) {
        js_throw_type_error("MutationObserver options must enable a mutation type");
        return make_js_undefined();
    }
    JsObserverTarget* target = observer_find_target(observer, node);
    if (!target) {
        if (observer->target_count >= JS_OBSERVER_TARGET_CAP) {
            log_error("dom-observer: MutationObserver target capacity exhausted");
            return make_js_undefined();
        }
        target = &observer->targets[observer->target_count++];
        memset(target, 0, sizeof(*target));
        target->node = node;
        target->owner_doc = observer_node_document(node);
        if (!observer_pin_node(target->owner_doc, node, &target->node_ref)) {
            memset(target, 0, sizeof(*target));
            observer->target_count--;
            return make_js_undefined();
        }
    }
    target->child_list = child_list;
    target->attributes = attributes;
    target->character_data = character_data;
    target->subtree = observer_option_bool(options, "subtree");
    target->attribute_old_value = attribute_old_value;
    target->character_data_old_value = character_data_old_value;
    target->attribute_filter_count = 0;
    Item filter = js_property_get(options, observer_key("attributeFilter"));
    if (get_type_id(filter) == LMD_TYPE_ARRAY) {
        int64_t count = js_array_length(filter);
        for (int64_t i = 0; i < count && target->attribute_filter_count < 8; i++) {
            const char* name = fn_to_cstr(js_array_get_int(filter, i));
            if (!name) continue;
            size_t len = strlen(name);
            if (len >= sizeof(target->attribute_filter[0])) {
                len = sizeof(target->attribute_filter[0]) - 1;
            }
            memcpy(target->attribute_filter[target->attribute_filter_count], name, len);
            target->attribute_filter[target->attribute_filter_count][len] = '\0';
            target->attribute_filter_count++;
        }
        if (target->attribute_filter_count > 0) target->attributes = true;
    }
    return make_js_undefined();
}

static Item js_observer_unobserve(Item target_item) {
    JsObserverState* observer = observer_from_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(target_item);
    if (!observer || !node) return make_js_undefined();
    for (int i = 0; i < observer->target_count; i++) {
        if (observer->targets[i].node != node) continue;
        observer_release_target(&observer->targets[i]);
        for (int j = i; j + 1 < observer->target_count; j++) {
            observer->targets[j] = observer->targets[j + 1];
        }
        observer->target_count--;
        memset(&observer->targets[observer->target_count], 0, sizeof(JsObserverTarget));
        break;
    }
    return make_js_undefined();
}

static Item js_geometry_observer_observe(Item target_item) {
    JsObserverState* observer = observer_from_this();
    DomNode* node = (DomNode*)js_dom_unwrap_element(target_item);
    if (!observer || observer->kind == JS_OBSERVER_MUTATION || !node) {
        return make_js_undefined();
    }
    if (observer_find_target(observer, node)) return make_js_undefined();
    if (observer->target_count >= JS_OBSERVER_TARGET_CAP) {
        log_error("dom-observer: geometry observer target capacity exhausted");
        return make_js_undefined();
    }
    JsObserverTarget* target = &observer->targets[observer->target_count++];
    memset(target, 0, sizeof(*target));
    target->node = node;
    target->owner_doc = observer_node_document(node);
    if (!observer_pin_node(target->owner_doc, node, &target->node_ref)) {
        memset(target, 0, sizeof(*target));
        observer->target_count--;
        return make_js_undefined();
    }
    // Geometry observation requires an initial sample even when observe() did
    // not dirty layout, but sampling waits until the current script's writes
    // settle so ResizeObserver reports the latest box once per checkpoint.
    js_microtask_enqueue(js_new_function((void*)js_geometry_observer_initial_sample, 0));
    return make_js_undefined();
}

static Item js_observer_deliver(void) {
    observer_delivery_scheduled = false;
    DomDocument* sweep_docs[JS_OBSERVER_CAP * JS_OBSERVER_TARGET_CAP] = {};
    int sweep_doc_count = 0;
    for (int i = 0; i < observer_count; i++) {
        JsObserverState* observer = &observers[i];
        Item records = observer_pending(observer);
        if (js_array_length(records) <= 0) continue;
        observer_replace_pending(observer);
        Item args[2] = {records, observer->object};
        js_call_function(observer->callback, make_js_undefined(), args, 2);
        if (observer->kind == JS_OBSERVER_MUTATION) {
            for (int j = 0; j < observer->target_count; j++) {
                DomDocument* owner_doc = observer->targets[j].owner_doc;
                observer_release_transient_roots(&observer->targets[j]);
                bool known = false;
                for (int d = 0; d < sweep_doc_count; d++) {
                    if (sweep_docs[d] == owner_doc) {
                        known = true;
                        break;
                    }
                }
                if (owner_doc && !known) sweep_docs[sweep_doc_count++] = owner_doc;
            }
        }
    }
    for (int i = 0; i < sweep_doc_count; i++) dom_retire_sweep(sweep_docs[i]);
    return make_js_undefined();
}

static void observer_schedule_delivery(void) {
    if (observer_delivery_scheduled) return;
    observer_delivery_scheduled = true;
    js_microtask_enqueue(js_new_function((void*)js_observer_deliver, 0));
}

static void observer_queue_record(JsObserverState* observer, Item record) {
    js_array_push(observer_pending(observer), record);
    observer_schedule_delivery();
}

static void observer_install_common_methods(JsObserverState* observer, bool mutation) {
    js_property_set(observer->object, observer_key("disconnect"),
        js_new_function((void*)js_observer_disconnect, 0));
    if (mutation) {
        js_property_set(observer->object, observer_key("observe"),
            js_new_function((void*)js_mutation_observer_observe, 2));
        js_property_set(observer->object, observer_key("takeRecords"),
            js_new_function((void*)js_observer_take_records, 0));
    } else {
        js_property_set(observer->object, observer_key("observe"),
            js_new_function((void*)js_geometry_observer_observe, 1));
        js_property_set(observer->object, observer_key("unobserve"),
            js_new_function((void*)js_observer_unobserve, 1));
    }
}

extern "C" Item js_mutation_observer_new(Item callback) {
    JsObserverState* observer = observer_create(JS_OBSERVER_MUTATION, callback);
    if (!observer) return ItemNull;
    observer_install_common_methods(observer, true);
    return observer->object;
}

extern "C" Item js_resize_observer_new(Item callback) {
    JsObserverState* observer = observer_create(JS_OBSERVER_RESIZE, callback);
    if (!observer) return ItemNull;
    observer_install_common_methods(observer, false);
    return observer->object;
}

extern "C" Item js_intersection_observer_new(Item callback, Item options) {
    JsObserverState* observer = observer_create(JS_OBSERVER_INTERSECTION, callback);
    if (!observer) return ItemNull;
    observer_install_common_methods(observer, false);
    Item root_item = observer_option(options, "root");
    observer->root = (DomElement*)js_dom_unwrap_element(root_item);
    if (observer->root) {
        observer_pin_node(observer->root->doc, (DomNode*)observer->root,
                          &observer->root_ref);
    }
    js_property_set(observer->object, observer_key("root"),
        observer->root ? root_item : ItemNull);
    Item margin_item = observer_option(options, "rootMargin");
    const char* margin = fn_to_cstr(margin_item);
    if (!observer_parse_root_margin(margin ? margin : "0px", observer->root_margin,
                                    observer->root_margin_percent)) {
        observer_parse_root_margin("0px", observer->root_margin,
                                   observer->root_margin_percent);
        margin = "0px";
    }
    js_property_set(observer->object, observer_key("rootMargin"),
        js_make_string(margin ? margin : "0px"));
    observer_parse_thresholds(observer, options);
    js_property_set(observer->object, observer_key("thresholds"), js_array_new(0));
    Item thresholds = js_property_get(observer->object, observer_key("thresholds"));
    for (int i = 0; i < observer->threshold_count; i++) {
        js_array_push(thresholds, js_make_number(observer->thresholds[i]));
    }
    return observer->object;
}

static bool observer_node_matches(DomNode* changed, DomNode* registered, bool subtree) {
    if (changed == registered) return true;
    if (!subtree) return false;
    for (DomNode* node = changed ? changed->parent : nullptr; node; node = node->parent) {
        if (node == registered) return true;
    }
    return false;
}

static bool observer_node_is_descendant_of(DomNode* changed, DomNode* root) {
    for (DomNode* node = changed; node; node = node->parent) {
        if (node == root) return true;
    }
    return false;
}

static bool observer_registration_matches(JsObserverTarget* registration,
                                          DomNode* changed) {
    if (observer_node_matches(changed, registration->node, registration->subtree)) return true;
    if (!registration->subtree) return false;
    for (int i = 0; i < registration->transient_root_count; i++) {
        if (observer_node_is_descendant_of(changed, registration->transient_roots[i])) return true;
    }
    return false;
}

static bool observer_attribute_filter_matches(JsObserverTarget* registration,
                                              const char* attribute_name) {
    if (registration->attribute_filter_count <= 0) return true;
    if (!attribute_name) return false;
    for (int i = 0; i < registration->attribute_filter_count; i++) {
        if (strcasecmp(registration->attribute_filter[i], attribute_name) == 0) return true;
    }
    return false;
}

extern "C" void js_dom_observers_mutation_notify(DomJsMutationKind kind,
    void* target_ptr, void* parent_ptr, const char* attribute_name, const char* old_value)
{
    DomNode* target = (DomNode*)target_ptr;
    DomNode* parent = (DomNode*)parent_ptr;
    bool child = kind == DOM_JS_MUTATION_CHILD_INSERT ||
                 kind == DOM_JS_MUTATION_CHILD_REMOVE ||
                 kind == DOM_JS_MUTATION_TREE_REPLACE;
    bool attribute = kind == DOM_JS_MUTATION_ATTRIBUTE ||
                     kind == DOM_JS_MUTATION_STYLE ||
                     kind == DOM_JS_MUTATION_STYLE_REPAINT;
    bool character = kind == DOM_JS_MUTATION_TEXT;
    DomNode* observed_node = child ? parent : target;
    if (!observed_node) return;

    for (int i = 0; i < observer_count; i++) {
        JsObserverState* observer = &observers[i];
        if (observer->kind != JS_OBSERVER_MUTATION) continue;
        for (int j = 0; j < observer->target_count; j++) {
            JsObserverTarget* registration = &observer->targets[j];
            if (!observer_registration_matches(registration, observed_node)) continue;
            if ((child && !registration->child_list) ||
                (attribute && !registration->attributes) ||
                (character && !registration->character_data)) continue;
            if (attribute &&
                !observer_attribute_filter_matches(registration, attribute_name)) continue;
            Item record = js_new_object();
            js_property_set(record, observer_key("type"),
                js_make_string(child ? "childList" : attribute ? "attributes" : "characterData"));
            js_property_set(record, observer_key("target"), js_dom_wrap_element(observed_node));
            Item added = js_array_new(0);
            Item removed = js_array_new(0);
            if (kind == DOM_JS_MUTATION_CHILD_INSERT && target) {
                js_array_push(added, js_dom_wrap_element(target));
            } else if (kind == DOM_JS_MUTATION_CHILD_REMOVE && target) {
                js_array_push(removed, js_dom_wrap_element(target));
            }
            js_property_set(record, observer_key("addedNodes"), added);
            js_property_set(record, observer_key("removedNodes"), removed);
            js_property_set(record, observer_key("previousSibling"), ItemNull);
            js_property_set(record, observer_key("nextSibling"), ItemNull);
            js_property_set(record, observer_key("attributeName"),
                attribute_name ? js_make_string(attribute_name) : ItemNull);
            js_property_set(record, observer_key("attributeNamespace"), ItemNull);
            bool include_old = (attribute && registration->attribute_old_value) ||
                               (character && registration->character_data_old_value);
            js_property_set(record, observer_key("oldValue"),
                include_old && old_value ? js_make_string(old_value) : ItemNull);
            observer_queue_record(observer, record);
            // A removed subtree remains observed through the microtask
            // checkpoint, which is why mutations made before delivery still
            // belong to this observer's batch.
            if (kind == DOM_JS_MUTATION_CHILD_REMOVE && registration->subtree && target &&
                registration->transient_root_count < 8) {
                int transient_index = registration->transient_root_count;
                if (observer_pin_node(registration->owner_doc, target,
                        &registration->transient_refs[transient_index])) {
                    registration->transient_roots[transient_index] = target;
                    registration->transient_root_count++;
                }
            }
            break;
        }
    }
}

static double observer_number_property(Item object, const char* name) {
    Item number = js_to_number(js_property_get(object, observer_key(name)));
    TypeId type = get_type_id(number);
    if (type == LMD_TYPE_INT) return (double)it2i(number);
    if (type == LMD_TYPE_FLOAT) return it2d(number);
    return 0.0;
}

static Item observer_rect(Item target_item, float* x, float* y, float* width, float* height) {
    Item rect = js_dom_element_method(target_item, observer_key("getBoundingClientRect"), nullptr, 0);
    *x = (float)observer_number_property(rect, "x");
    *y = (float)observer_number_property(rect, "y");
    *width = (float)observer_number_property(rect, "width");
    *height = (float)observer_number_property(rect, "height");
    return rect;
}

static Item observer_make_rect(float x, float y, float width, float height) {
    Item rect = js_new_object();
    js_property_set(rect, observer_key("x"), js_make_number(x));
    js_property_set(rect, observer_key("y"), js_make_number(y));
    js_property_set(rect, observer_key("top"), js_make_number(y));
    js_property_set(rect, observer_key("left"), js_make_number(x));
    js_property_set(rect, observer_key("right"), js_make_number(x + width));
    js_property_set(rect, observer_key("bottom"), js_make_number(y + height));
    js_property_set(rect, observer_key("width"), js_make_number(width));
    js_property_set(rect, observer_key("height"), js_make_number(height));
    return rect;
}

static float observer_resolve_margin(JsObserverState* observer, int side,
                                     float root_width, float root_height) {
    float basis = (side == 0 || side == 2) ? root_height : root_width;
    return observer->root_margin_percent[side]
        ? observer->root_margin[side] * basis / 100.0f
        : observer->root_margin[side];
}

static bool observer_threshold_crossed(JsObserverState* observer,
                                       float old_ratio, float new_ratio) {
    for (int i = 0; i < observer->threshold_count; i++) {
        float threshold = observer->thresholds[i];
        if ((old_ratio < threshold && new_ratio >= threshold) ||
            (old_ratio >= threshold && new_ratio < threshold)) {
            return true;
        }
    }
    return false;
}

extern "C" void js_dom_observers_post_layout(void) {
    // Host-driven layout can finish after the loader restored its JS context;
    // sampling is deferred until the retained runtime is installed by the pump.
    if (!context) return;
    UiContext* uicon = (UiContext*)js_dom_get_ui_context();
    if (!uicon) return;
    for (int i = 0; i < observer_count; i++) {
        JsObserverState* observer = &observers[i];
        if (observer->kind == JS_OBSERVER_MUTATION) continue;
        for (int j = 0; j < observer->target_count; j++) {
            JsObserverTarget* target = &observer->targets[j];
            Item target_item = js_dom_wrap_element(target->node);
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            Item rect = observer_rect(target_item, &x, &y, &width, &height);
            if (observer->kind == JS_OBSERVER_RESIZE) {
                if (target->sampled && fabsf(width - target->last_width) < 0.01f &&
                    fabsf(height - target->last_height) < 0.01f) continue;
                target->sampled = true;
                target->last_width = width;
                target->last_height = height;
                Item entry = js_new_object();
                js_property_set(entry, observer_key("target"), target_item);
                js_property_set(entry, observer_key("contentRect"), rect);
                Item box = js_new_object();
                js_property_set(box, observer_key("inlineSize"), js_make_number(width));
                js_property_set(box, observer_key("blockSize"), js_make_number(height));
                Item boxes = js_array_new(0);
                js_array_push(boxes, box);
                js_property_set(entry, observer_key("contentBoxSize"), boxes);
                js_property_set(entry, observer_key("borderBoxSize"), boxes);
                observer_queue_record(observer, entry);
                continue;
            }

            float root_x = 0.0f;
            float root_y = 0.0f;
            float root_width = uicon->viewport_width;
            float root_height = uicon->viewport_height;
            if (observer->root) {
                observer_rect(js_dom_wrap_element(observer->root), &root_x, &root_y,
                              &root_width, &root_height);
            }
            float root_top = root_y - observer_resolve_margin(observer, 0,
                root_width, root_height);
            float root_right = root_x + root_width + observer_resolve_margin(observer, 1,
                root_width, root_height);
            float root_bottom = root_y + root_height + observer_resolve_margin(observer, 2,
                root_width, root_height);
            float root_left = root_x - observer_resolve_margin(observer, 3,
                root_width, root_height);
            float left = x > root_left ? x : root_left;
            float top = y > root_top ? y : root_top;
            float right = x + width < root_right ? x + width : root_right;
            float bottom = y + height < root_bottom ? y + height : root_bottom;
            float intersection_width = right > left ? right - left : 0.0f;
            float intersection_height = bottom > top ? bottom - top : 0.0f;
            float area = width * height;
            float ratio = area > 0.0f ? (intersection_width * intersection_height) / area : 0.0f;
            bool intersecting = intersection_width > 0.0f && intersection_height > 0.0f;
            if (target->sampled && target->last_intersecting == intersecting &&
                !observer_threshold_crossed(observer, target->last_ratio, ratio)) continue;
            target->sampled = true;
            target->last_intersecting = intersecting;
            target->last_ratio = ratio;
            Item entry = js_new_object();
            js_property_set(entry, observer_key("target"), target_item);
            js_property_set(entry, observer_key("boundingClientRect"), rect);
            js_property_set(entry, observer_key("intersectionRatio"), js_make_number(ratio));
            js_property_set(entry, observer_key("isIntersecting"), (Item){.item = b2it(intersecting)});
            Item intersection = observer_make_rect(left, top,
                intersection_width, intersection_height);
            js_property_set(entry, observer_key("intersectionRect"), intersection);
            js_property_set(entry, observer_key("rootBounds"), observer_make_rect(
                root_left, root_top, root_right - root_left, root_bottom - root_top));
            js_property_set(entry, observer_key("time"), js_make_number(0.0));
            observer_queue_record(observer, entry);
        }
    }
}

static Item js_geometry_observer_initial_sample(void) {
    js_dom_observers_post_layout();
    return make_js_undefined();
}

extern "C" void js_dom_observers_reset(void) {
    for (int i = 0; i < observer_count; i++) {
        JsObserverState* observer = &observers[i];
        for (int j = 0; j < observer->target_count; j++) {
            observer_release_target(&observer->targets[j]);
        }
        if (observer->root && observer->root_ref.address) {
            dom_node_unpin(observer->root->doc, observer->root_ref,
                           DOM_NODE_PIN_OBSERVER);
        }
    }
    memset(observers, 0, sizeof(observers));
    observer_count = 0;
    observer_delivery_scheduled = false;
}
