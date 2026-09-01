#include "../../jube/jube_registry.h"
#include "../../input/css/dom_element.hpp"
#include "../../io/input-allocation-context.h"
#include "../../runtime/lambda-error.h"
#include "../../runtime/transpiler.hpp"
#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
#include "../../../radiant/layout.hpp"
#include "../../../radiant/render.hpp"
#include "../../../radiant/event.hpp"
#include "../../../lib/log.h"
#include "../../../lib/mem.h"
#include "../../../lib/mem_context.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/mempool.h"
#include "../../runtime/side_stack.h"
#include "../../runtime/gc/gc_heap.h"
#include "../../../lib/url.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>

String* heap_create_name(const char* name, size_t len);

extern DomDocument* load_lambda_html_doc(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source,
    bool track_source_lines, bool execute_scripts);
extern void free_document(DomDocument* doc);
RADIANT_C_API Item radiant_dom_wrap_node(void* dom_elem);
RADIANT_C_API void* radiant_dom_unwrap_node(Item item);
RADIANT_C_API int radiant_dom_host_get_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_set_property(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_host_has_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_delete_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_own_property_descriptor(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_own_property_names(Item object, Item* out);
RADIANT_C_API Item radiant_dom_host_prototype(Item object);
RADIANT_C_API void radiant_dom_host_invalidate(Item object);
RADIANT_C_API int radiant_dom_document_host_get_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_set_property(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_document_host_has_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_delete_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_descriptor(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_names(Item object, Item* out);
RADIANT_C_API Item radiant_dom_document_host_prototype(Item object);

const JubeHostAPI* radiant_host_api = nullptr;
extern __thread EvalContext* context;
extern __thread Context* input_context;
extern "C" Item js_formdata_collect_form_entries(void* form_elem, void* submitter_elem);
extern "C" Item dom_check_validity_bridge(Item elem_item);
extern "C" bool dom_focus_first_invalid_form_control(void* form_elem);
extern "C" Item dom_form_reset_bridge(Item form_item);
extern "C" bool dom_navigate_submit_target(const char* target_name, const char* url);
extern "C" void* dom_popover_target_for_button(void* button);
extern "C" int dom_popover_target_action(void* button);
extern "C" bool dom_activate_popover(void* popover, int action);
extern "C" bool radiant_dispatch_submit_event_from_script(void* form_node,
                                                            void* submitter_node);
extern "C" Item dom_scroll_into_view_bridge(void* dom_elem);

extern "C" Item vmap_new(void);
extern "C" void vmap_set(Item vmap_item, Item key, Item value);
#ifdef __APPLE__
extern "C" Item radiant_lambda_fn_call3_into(Function* fn, Item a, Item b, Item c,
                                               uint64_t* result_home) asm("_fn_call3_into");
#else
extern "C" Item radiant_lambda_fn_call3_into(Function* fn, Item a, Item b, Item c,
                                               uint64_t* result_home) asm("fn_call3_into");
#endif
Item vmap_get_by_item(VMap* vm, Item key);

#define RADIANT_CUSTOM_LAYOUT_MAX_REGISTRY 64
#define RADIANT_CUSTOM_LAYOUT_NAME_CAP 64
// Rich graph ports may sit below nested table/tbody/tr wrappers; keep traversal
// bounded while exposing enough laid-out ancestry for semantic attachment.
#define RADIANT_VELMT_CHILD_DEPTH 32
#define RADIANT_VELMT_MAGIC 0x56454c4d54ULL

typedef struct RadiantCustomLayoutEntry {
    char name[RADIANT_CUSTOM_LAYOUT_NAME_CAP];
    Heap* owner_heap;
    Item fn;
    bool rooted;
} RadiantCustomLayoutEntry;

typedef struct RadiantVelmtHost {
    uint64_t magic;
    uint64_t pass_id;
    int depth;
    Velmt velmt;
} RadiantVelmtHost;

typedef struct RadiantCustomPaintResource {
    CustomLayoutPaintState paint;
    Item* roots;
    int root_count;
    Heap* owner_heap;
} RadiantCustomPaintResource;

typedef struct RadiantLayoutResource {
    UiContext ui_context;
} RadiantLayoutResource;

static RadiantCustomLayoutEntry g_radiant_custom_layouts[RADIANT_CUSTOM_LAYOUT_MAX_REGISTRY];
static int g_radiant_custom_layout_count = 0;
static uint64_t g_radiant_velmt_next_pass_id = 1;
static THREAD_LOCAL uint64_t g_radiant_velmt_active_pass_id = 0;

RADIANT_C_API const void* radiant_velmt_host_type(void);

static Item radiant_string_item(const char* value) {
    return value ? (Item){.item = s2it(heap_create_name(value))} : ItemNull;
}

static Item radiant_string_item_n(const char* value, size_t length) {
    return value ? (Item){.item = s2it(heap_create_name(value, length))} : ItemNull;
}

static Item radiant_int_item(int64_t value) {
    return (Item){.item = i2it(value)};
}

static Item radiant_bool_item(bool value) {
    return (Item){.item = b2it(value ? 1 : 0)};
}

static Item radiant_float_item(double value) {
    return push_d(value);
}

static Item radiant_key_item(const char* key) {
    return radiant_string_item(key);
}

static Item radiant_obj_new(void) {
    return vmap_new();
}

static void radiant_obj_set(Item obj, const char* key, Item value) {
    if (!radiant_host_api || !radiant_host_api->value || !key) return;
    Item key_item = radiant_key_item(key);
    // custom layout Velmt values are plain Lambda VMaps so Lambda callbacks can
    // read them without entering JS object storage.
    if (get_type_id(obj) == LMD_TYPE_VMAP && obj.vmap) {
        vmap_set(obj, key_item, value);
        return;
    }
    radiant_host_api->value->property_set(obj, key_item, value);
}

static void radiant_rooted_obj_set(Rooted<Item>& rooted_obj, const char* key, Item value) {
    radiant_obj_set(rooted_obj.get(), key, value);
}

static void radiant_rooted_obj_set_optional_float(Rooted<Item>& rooted_obj,
                                                   const char* key, float value) {
    radiant_rooted_obj_set(rooted_obj, key,
        value >= 0.0f ? radiant_float_item(value) : ItemNull);
}

static bool radiant_item_is_missing(Item item) {
    return item.item == ItemNull.item || item.item == ITEM_JS_UNDEFINED;
}

static Item radiant_obj_get(Item obj, const char* key) {
    if (!radiant_host_api || !radiant_host_api->value || !key) return ItemNull;
    if (radiant_item_is_missing(obj)) return ItemNull;
    Item key_item = radiant_key_item(key);
    if (get_type_id(obj) == LMD_TYPE_MAP && obj.map) {
        // lambda custom layout callbacks may pass Velmt-shaped maps; JS property_get
        // assumes JS object metadata and can crash on plain Lambda maps.
        return map_get(obj.map, key_item);
    }
    if (get_type_id(obj) == LMD_TYPE_VMAP && obj.vmap) {
        return vmap_get_by_item(obj.vmap, key_item);
    }
    return radiant_host_api->value->property_get(obj, key_item);
}

static Item radiant_array_new_item(int capacity) {
    RootFrame roots(1);
    Rooted<Array*> rooted_arr(roots, (Array*)NULL);
    Array* arr = array();
    rooted_arr.set(arr);
    if (arr && capacity > 0) {
        Item* items = (Item*)heap_data_calloc((size_t)capacity * sizeof(Item));
        arr = rooted_arr.get();
        arr->items = items;
        arr->capacity = capacity;
    }
    return rooted_arr.get() ? (Item){.array = rooted_arr.get()} : ItemNull;
}

static void radiant_array_push_item(Item array_item, Item value) {
    if (get_type_id(array_item) != LMD_TYPE_ARRAY || !array_item.array) return;
    Array* arr = array_item.array;
    if (arr->length >= arr->capacity) {
        log_error("CUSTOM_LAYOUT_ARRAY_CAPACITY_EXCEEDED length=%lld capacity=%lld",
                  (long long)arr->length, (long long)arr->capacity);
        return;
    }
    arr->items[arr->length++] = value;
}

static Item radiant_obj_get_alias(Item obj, const char* primary_key, const char* alias_key) {
    Item value = radiant_obj_get(obj, primary_key);
    if (radiant_item_is_missing(value) && alias_key) {
        value = radiant_obj_get(obj, alias_key);
    }
    return radiant_item_is_missing(value) ? ItemNull : value;
}

static bool radiant_item_to_int(Item item, int* out);
static Heap* radiant_custom_layout_heap(const CustomLayoutContext* layout_context);

static void radiant_custom_paint_clear(RadiantCustomPaintResource* resource) {
    if (!resource) return;
    if (resource->roots && resource->owner_heap && resource->owner_heap->gc) {
        for (int i = 0; i < resource->root_count; i++) {
            // Document teardown may run outside the callback's thread-local
            // EvalContext, so roots must be removed from their actual owner.
            gc_unregister_root(resource->owner_heap->gc, &resource->roots[i].item);
        }
    }
    if (resource->roots) mem_free(resource->roots);
    if (resource->paint.layers) mem_free(resource->paint.layers);
    resource->roots = nullptr;
    resource->root_count = 0;
    resource->paint.layers = nullptr;
    resource->paint.layer_count = 0;
}

static void radiant_custom_paint_destroy(void* data) {
    RadiantCustomPaintResource* resource = (RadiantCustomPaintResource*)data;
    if (!resource) return;
    radiant_custom_paint_clear(resource);
    mem_free(resource);
}

static RadiantCustomPaintResource* radiant_custom_paint_resource(
    const CustomLayoutContext* context) {
    if (!context || !context->parent || !context->parent->doc) return nullptr;
    if (context->parent->custom_layout_paint_prop()) {
        CustomLayoutPaintState* paint =
            (CustomLayoutPaintState*)context->parent->custom_layout_paint_prop();
        return (RadiantCustomPaintResource*)paint;
    }

    RadiantCustomPaintResource* resource = (RadiantCustomPaintResource*)mem_calloc(
        1, sizeof(RadiantCustomPaintResource), MEM_CAT_LAYOUT);
    if (!resource) return nullptr;
    resource->owner_heap = radiant_custom_layout_heap(context);
    if (!resource->owner_heap) {
        mem_free(resource);
        return nullptr;
    }
    if (!dom_document_add_resource(context->parent->doc, resource,
                                   radiant_custom_paint_destroy)) {
        mem_free(resource);
        return nullptr;
    }
    context->parent->set_custom_layout_paint_prop(&resource->paint);
    return resource;
}

static bool radiant_custom_layout_parse_paint_layers(const CustomLayoutContext* context,
                                                     Item result_item) {
    Item layers_item = radiant_obj_get(result_item, "paint_layers");
    if (radiant_item_is_missing(layers_item)) {
        if (context && context->parent && context->parent->custom_layout_paint_prop()) {
            // A later reflow may stop returning generated paint; clear the prior
            // result so stale subscenes cannot survive merely because the field is absent.
            radiant_custom_paint_clear((RadiantCustomPaintResource*)
                context->parent->custom_layout_paint_prop());
        }
        return true;
    }
    if (get_type_id(layers_item) != LMD_TYPE_ARRAY || !layers_item.array) {
        log_error("CUSTOM_LAYOUT_LAMBDA_PAINT: result.paint_layers must be an array");
        return false;
    }

    int layer_count = (int)layers_item.array->length; // INT_CAST_OK: Lambda array length is bounded by native allocation limits below.
    Item* roots = nullptr;
    CustomLayoutPaintLayer* layers = nullptr;
    if (layer_count > 0) {
        roots = (Item*)mem_calloc((size_t)layer_count, sizeof(Item), MEM_CAT_LAYOUT);
        layers = (CustomLayoutPaintLayer*)mem_calloc(
            (size_t)layer_count, sizeof(CustomLayoutPaintLayer), MEM_CAT_LAYOUT);
        if (!roots || !layers) {
            if (roots) mem_free(roots);
            if (layers) mem_free(layers);
            log_error("CUSTOM_LAYOUT_LAMBDA_PAINT: failed to allocate %d retained layers",
                      layer_count);
            return false;
        }
    }

    for (int i = 0; i < layer_count; i++) {
        Item layer_item = layers_item.array->items[i];
        Item content = radiant_obj_get(layer_item, "content");
        if (get_type_id(content) != LMD_TYPE_ELEMENT || !content.element) {
            log_error("CUSTOM_LAYOUT_LAMBDA_PAINT: layer %d content must be an element", i);
            if (roots) mem_free(roots);
            if (layers) mem_free(layers);
            return false;
        }
        int z = 0;
        radiant_item_to_int(radiant_obj_get(layer_item, "z"), &z);
        roots[i] = content;
        layers[i].content = content.element;
        layers[i].z = z;
        layers[i].order = i;
    }

    RadiantCustomPaintResource* resource = radiant_custom_paint_resource(context);
    if (!resource) {
        if (roots) mem_free(roots);
        if (layers) mem_free(layers);
        log_error("CUSTOM_LAYOUT_LAMBDA_PAINT: failed to attach document resource");
        return false;
    }
    // Reflow replaces the complete generated layer set; retaining old roots
    // would keep stale SVG trees alive across every interaction.
    radiant_custom_paint_clear(resource);
    for (int i = 0; i < layer_count; i++) {
        gc_register_root(resource->owner_heap->gc, &roots[i].item);
    }
    resource->roots = roots;
    resource->root_count = layer_count;
    resource->paint.layers = layers;
    resource->paint.layer_count = layer_count;
    return true;
}

static bool radiant_item_to_float(Item item, float* out) {
    if (!out) return false;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
        type == LMD_TYPE_FLOAT ||
        type == LMD_TYPE_NUM_SIZED || type == LMD_TYPE_UINT64) {
        *out = (float)it2d(item);
        return true;
    }
    return false;
}

static bool radiant_item_to_index(Item item, int* out) {
    if (!out) return false;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
        type == LMD_TYPE_FLOAT ||
        type == LMD_TYPE_NUM_SIZED || type == LMD_TYPE_UINT64) {
        int64_t value = it2i(item);
        if (value < 0 || value > INT_MAX) return false;
        *out = (int)value; // INT_CAST_OK: child indexes are bounded to native registry array slots.
        return true;
    }
    return false;
}

static bool radiant_item_to_int(Item item, int* out) {
    if (!out) return false;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
        type == LMD_TYPE_FLOAT ||
        type == LMD_TYPE_NUM_SIZED || type == LMD_TYPE_UINT64) {
        int64_t value = it2i(item);
        if (value < INT_MIN || value > INT_MAX) return false;
        *out = (int)value; // INT_CAST_OK: z-index is stored in PositionProp as native int.
        return true;
    }
    return false;
}

static void radiant_layout_collect_text(DomNode* node, StrBuf* text) {
    if (!node || !text) return;
    if (node->is_text()) {
        DomText* text_node = node->as_text();
        if (text_node && text_node->text && text_node->length > 0) {
            strbuf_append_str_n(text, text_node->text, text_node->length);
        }
        return;
    }
    if (!node->is_element()) return;
    DomElement* elem = node->as_element();
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        radiant_layout_collect_text(child, text);
    }
}

static Item radiant_layout_text_item(DomNode* node) {
    if (!node) return radiant_string_item("");
    StrBuf* text = strbuf_new();
    if (!text) return radiant_string_item("");
    radiant_layout_collect_text(node, text);
    Item result = radiant_string_item(text->str ? text->str : "");
    strbuf_free(text);
    return result;
}

static Item radiant_layout_edges_item(const VelmtEdges* edges) {
    if (!radiant_host_api || !radiant_host_api->value || !edges) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> rooted_obj(roots, radiant_obj_new());
    radiant_rooted_obj_set(rooted_obj, "left", radiant_float_item(edges->left));
    radiant_rooted_obj_set(rooted_obj, "right", radiant_float_item(edges->right));
    radiant_rooted_obj_set(rooted_obj, "top", radiant_float_item(edges->top));
    radiant_rooted_obj_set(rooted_obj, "bottom", radiant_float_item(edges->bottom));
    return rooted_obj.get();
}

static Item radiant_layout_box_item(const VelmtBox* box) {
    if (!radiant_host_api || !radiant_host_api->value || !box) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> rooted_obj(roots, radiant_obj_new());
    radiant_rooted_obj_set(rooted_obj, "x", radiant_float_item(box->x));
    radiant_rooted_obj_set(rooted_obj, "y", radiant_float_item(box->y));
    radiant_rooted_obj_set(rooted_obj, "width", radiant_float_item(box->width));
    radiant_rooted_obj_set(rooted_obj, "height", radiant_float_item(box->height));
    return rooted_obj.get();
}

static Item radiant_layout_attrs_item(DomElement* elem) {
    if (!radiant_host_api || !radiant_host_api->value || !elem) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> rooted_attrs(roots, radiant_obj_new());
    int attr_count = 0;
    const char** names = elem->attribute_names(&attr_count);
    for (int i = 0; names && i < attr_count; i++) {
        const char* name = names[i];
        if (!name) continue;
        const char* value = elem->get_attribute(name);
        radiant_rooted_obj_set(rooted_attrs, name, radiant_string_item(value ? value : ""));
    }
    return rooted_attrs.get();
}

typedef struct RadiantStyleSnapshotContext {
    uint64_t* style_root;
} RadiantStyleSnapshotContext;

static bool radiant_layout_style_snapshot_callback(StyleNode* node, void* context) {
    if (!node || !node->winning_decl || !context) return true;
    CssDeclaration* decl = node->winning_decl;
    const char* name = decl->property_name ? decl->property_name :
        css_property_spelling_from_code(decl->property_code);
    if (!name || !decl->value_text) return true;

    RadiantStyleSnapshotContext* snapshot = (RadiantStyleSnapshotContext*)context;
    Item style = (Item){.item = snapshot->style_root ? *snapshot->style_root : ItemNull.item};
    radiant_obj_set(style, name,
        radiant_string_item_n(decl->value_text, decl->value_text_len));
    return true;
}

static Item radiant_layout_style_item(DomElement* elem) {
    if (!radiant_host_api || !radiant_host_api->value) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> rooted_style(roots, radiant_obj_new());
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return rooted_style.get();

    RadiantStyleSnapshotContext context;
    context.style_root = rooted_style.home();
    style_tree_foreach(elem->specified_style, radiant_layout_style_snapshot_callback, &context);
    return rooted_style.get();
}

static Item radiant_layout_velmt_host_item_depth(const Velmt* velmt, int depth);
static Item radiant_layout_view_children_item(View* view, int depth);

static bool radiant_is_velmt_host_item(Item item) {
    return get_type_id(item) == LMD_TYPE_VMAP && item.vmap &&
        item.vmap->host_type == radiant_velmt_host_type();
}

static RadiantVelmtHost* radiant_velmt_host_from_item(Item item) {
    if (!radiant_is_velmt_host_item(item)) return nullptr;
    RadiantVelmtHost* host = (RadiantVelmtHost*)item.vmap->host_data;
    if (!host || host->magic != RADIANT_VELMT_MAGIC) return nullptr;
    return host;
}

static RadiantVelmtHost* radiant_velmt_host_active_from_item(Item item) {
    RadiantVelmtHost* host = radiant_velmt_host_from_item(item);
    if (!host || host->pass_id == 0 ||
        host->pass_id != g_radiant_velmt_active_pass_id) {
        return nullptr;
    }
    return host;
}

static void radiant_velmt_host_destroy(void* native) {
    RadiantVelmtHost* host = (RadiantVelmtHost*)native;
    if (!host) return;
    host->magic = 0;
    mem_free(host);
}

static Item radiant_layout_velmt_host_item_depth(const Velmt* velmt, int depth) {
    if (!radiant_host_api || !radiant_host_api->value || !velmt ||
        g_radiant_velmt_active_pass_id == 0) {
        return ItemNull;
    }
    Item obj = radiant_host_api->value->vmap_new();
    if (get_type_id(obj) != LMD_TYPE_VMAP || !obj.vmap) return ItemNull;

    RadiantVelmtHost* host = (RadiantVelmtHost*)mem_calloc(
        1, sizeof(RadiantVelmtHost), MEM_CAT_EVAL);
    if (!host) return ItemNull;
    host->magic = RADIANT_VELMT_MAGIC;
    host->pass_id = g_radiant_velmt_active_pass_id;
    host->depth = depth;
    memcpy(&host->velmt, velmt, sizeof(Velmt));
    obj.vmap->host_type = radiant_velmt_host_type();
    obj.vmap->host_data = host;
    return obj;
}

static Item radiant_velmt_project_property(const Velmt* velmt, int depth, const char* key) {
    if (!velmt || !key) return ItemNull;
    if (strcmp(key, "index") == 0) return radiant_int_item(velmt->index);
    if (strcmp(key, "tag") == 0 || strcmp(key, "node_name") == 0) {
        return radiant_string_item(velmt->view ? velmt->view->node_name() : "");
    }
    if (strcmp(key, "width") == 0 || strcmp(key, "wd") == 0) {
        return radiant_float_item(velmt->border_box.width);
    }
    if (strcmp(key, "height") == 0 || strcmp(key, "hg") == 0) {
        return radiant_float_item(velmt->border_box.height);
    }
    if (strcmp(key, "box") == 0) return radiant_layout_box_item(&velmt->border_box);
    if (strcmp(key, "children") == 0) {
        return radiant_layout_view_children_item(velmt->view, depth);
    }
    if (strcmp(key, "text") == 0) return radiant_layout_text_item((DomNode*)velmt->view);
    if (strcmp(key, "style") == 0) return radiant_layout_style_item(velmt->element);
    if (strcmp(key, "margin") == 0) return radiant_layout_edges_item(&velmt->margin);
    if (strcmp(key, "border") == 0) return radiant_layout_edges_item(&velmt->border);
    if (strcmp(key, "padding") == 0) return radiant_layout_edges_item(&velmt->padding);
    if (strcmp(key, "id") == 0) {
        return velmt->element ? radiant_string_item(velmt->element->id) : ItemNull;
    }
    if (strcmp(key, "attrs") == 0) {
        return velmt->element ? radiant_layout_attrs_item(velmt->element) : ItemNull;
    }
    return ItemNull;
}

RADIANT_C_API int radiant_velmt_host_get_property(Item object, Item key, Item* out) {
    if (!out) return 0;
    RadiantVelmtHost* host = radiant_velmt_host_active_from_item(object);
    if (!host) {
        // Velmt handles are scoped to one custom layout callback/result parse.
        *out = ItemNull;
        return 1;
    }
    const char* key_name = fn_to_cstr(key);
    if (!key_name) {
        *out = ItemNull;
        return 1;
    }
    *out = radiant_velmt_project_property(&host->velmt, host->depth, key_name);
    return 1;
}

RADIANT_C_API int radiant_velmt_host_set_property(Item object, Item key, Item value, Item* out) {
    (void)object; (void)key; (void)value;
    if (out) *out = ItemNull;
    return 0;
}

RADIANT_C_API int radiant_velmt_host_has_property(Item object, Item key, Item* out) {
    Item value = ItemNull;
    if (!radiant_velmt_host_get_property(object, key, &value)) return 0;
    if (out) *out = radiant_bool_item(!radiant_item_is_missing(value));
    return 1;
}

RADIANT_C_API int radiant_velmt_host_delete_property(Item object, Item key, Item* out) {
    (void)object; (void)key;
    if (out) *out = radiant_bool_item(false);
    return 1;
}

RADIANT_C_API int radiant_velmt_host_own_property_names(Item object, Item* out) {
    (void)object;
    if (!out) return 0;
    static const char* keys[] = {
        "index", "tag", "id", "width", "height", "wd", "hg", "box",
        "children", "text", "style", "margin", "border", "padding", "attrs"
    };
    int key_count = (int)(sizeof(keys) / sizeof(keys[0])); // INT_CAST_OK: fixed small static key table.
    Item arr = radiant_array_new_item(key_count);
    for (int i = 0; i < key_count; i++) {
        radiant_array_push_item(arr, radiant_string_item(keys[i]));
    }
    *out = arr;
    return 1;
}

RADIANT_C_API int radiant_velmt_host_own_property_descriptor(Item object, Item key, Item* out) {
    Item value = ItemNull;
    if (!out || !radiant_velmt_host_get_property(object, key, &value) ||
        radiant_item_is_missing(value)) {
        return 0;
    }
    Item desc = radiant_obj_new();
    radiant_obj_set(desc, "value", value);
    radiant_obj_set(desc, "writable", radiant_bool_item(false));
    radiant_obj_set(desc, "enumerable", radiant_bool_item(true));
    radiant_obj_set(desc, "configurable", radiant_bool_item(false));
    *out = desc;
    return 1;
}

static int radiant_layout_view_child_count(View* view) {
    if (!view || !view->is_element()) return 0;
    DomElement* elem = view->as_element();
    int count = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        View* child_view = (View*)child;
        if (child_view && child_view->view_type != RDT_VIEW_NONE) count++;
    }
    return count;
}

static Item radiant_layout_view_children_item(View* view, int depth) {
    if (!radiant_host_api || !radiant_host_api->value) return ItemNull;
    int child_count = depth > 0 ? radiant_layout_view_child_count(view) : 0;
    RootFrame roots(1);
    Rooted<Item> rooted_children(roots, radiant_array_new_item(child_count));
    if (!view || !view->is_element() || depth <= 0) return rooted_children.get();

    DomElement* elem = view->as_element();
    int index = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        View* child_view = (View*)child;
        if (!child_view || child_view->view_type == RDT_VIEW_NONE) continue;
        Velmt child_velmt;
        custom_layout_fill_velmt_from_view(&child_velmt, child_view, index, false);
        Item child_item = radiant_layout_velmt_host_item_depth(&child_velmt, depth - 1);
        radiant_array_push_item(rooted_children.get(), child_item);
        index++;
    }
    return rooted_children.get();
}

static Item radiant_layout_velmt_host_item(const Velmt* velmt) {
    return radiant_layout_velmt_host_item_depth(velmt, RADIANT_VELMT_CHILD_DEPTH);
}

static Item radiant_layout_parent_item(const CustomLayoutContext* context) {
    if (!radiant_host_api || !radiant_host_api->value || !context || !context->parent) return ItemNull;
    Velmt parent;
    memset(&parent, 0, sizeof(parent));
    parent.view = (View*)context->parent;
    parent.element = context->parent;
    parent.index = -1;
    parent.border_box.x = 0.0f;
    parent.border_box.y = 0.0f;
    parent.border_box.width = context->parent->width;
    parent.border_box.height = context->parent->height;
    if (context->parent) {
        BoxMetrics metrics = layout_box_metrics(context->parent);
        parent.margin.left = metrics.margin.left;
        parent.margin.right = metrics.margin.right;
        parent.margin.top = metrics.margin.top;
        parent.margin.bottom = metrics.margin.bottom;
        parent.border.left = metrics.border.left;
        parent.border.right = metrics.border.right;
        parent.border.top = metrics.border.top;
        parent.border.bottom = metrics.border.bottom;
        parent.padding.left = metrics.padding.left;
        parent.padding.right = metrics.padding.right;
        parent.padding.top = metrics.padding.top;
        parent.padding.bottom = metrics.padding.bottom;
    }
    return radiant_layout_velmt_host_item(&parent);
}

static Item radiant_layout_children_item(const CustomLayoutContext* context) {
    if (!radiant_host_api || !radiant_host_api->value || !context) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> rooted_arr(roots, radiant_array_new_item(context->child_count));
    for (int i = 0; i < context->child_count; i++) {
        Item child = radiant_layout_velmt_host_item(&context->children[i]);
        radiant_array_push_item(rooted_arr.get(), child);
    }
    return rooted_arr.get();
}

static Item radiant_layout_context_item(const CustomLayoutContext* context) {
    if (!radiant_host_api || !radiant_host_api->value || !context) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> rooted_obj(roots, radiant_obj_new());
    radiant_rooted_obj_set(rooted_obj, "layout_name", radiant_string_item(context->layout_name));
    radiant_rooted_obj_set(rooted_obj, "available_width", radiant_float_item(context->available_width));
    radiant_rooted_obj_set(rooted_obj, "available_height", radiant_float_item(context->available_height));
    radiant_rooted_obj_set_optional_float(rooted_obj, "css_width", context->css_width);
    radiant_rooted_obj_set_optional_float(rooted_obj, "css_height", context->css_height);
    radiant_rooted_obj_set(rooted_obj, "child_available_width", radiant_float_item(context->child_available_width));
    radiant_rooted_obj_set(rooted_obj, "child_available_height", radiant_float_item(context->child_available_height));
    radiant_rooted_obj_set(rooted_obj, "child_available_width_definite",
        radiant_bool_item(context->child_available_width_definite));
    radiant_rooted_obj_set(rooted_obj, "child_available_height_definite",
        radiant_bool_item(context->child_available_height_definite));
    radiant_rooted_obj_set(rooted_obj, "child_available_width_source",
        radiant_string_item(context->child_available_width_source));
    radiant_rooted_obj_set(rooted_obj, "child_available_height_source",
        radiant_string_item(context->child_available_height_source));
    radiant_rooted_obj_set(rooted_obj, "direction", radiant_string_item(
        context->direction == CSS_VALUE_RTL ? "rtl" : "ltr"));
    radiant_rooted_obj_set(rooted_obj, "writing_mode", radiant_string_item(
        context->writing_mode ? context->writing_mode : "horizontal-tb"));
    radiant_rooted_obj_set(rooted_obj, "child_count", radiant_int_item(context->child_count));
    return rooted_obj.get();
}

static Heap* radiant_custom_layout_heap(const CustomLayoutContext* layout_context) {
    Runtime* runtime = (layout_context && layout_context->parent && layout_context->parent->doc)
        ? layout_context->parent->doc->lambda_runtime : nullptr;
    if (runtime && runtime->heap) return runtime->heap;
    return ::context ? ::context->heap : nullptr;
}

static RadiantCustomLayoutEntry* radiant_custom_layout_entry(const char* name, Heap* owner_heap) {
    if (!name || name[0] == '\0') return nullptr;
    for (int i = 0; i < g_radiant_custom_layout_count; i++) {
        if (g_radiant_custom_layouts[i].owner_heap == owner_heap &&
            strcmp(g_radiant_custom_layouts[i].name, name) == 0) {
            return &g_radiant_custom_layouts[i];
        }
    }
    return nullptr;
}

static RadiantCustomLayoutEntry* radiant_custom_layout_free_entry(void) {
    for (int i = 0; i < g_radiant_custom_layout_count; i++) {
        if (!g_radiant_custom_layouts[i].owner_heap &&
            !g_radiant_custom_layouts[i].rooted) {
            return &g_radiant_custom_layouts[i];
        }
    }
    if (g_radiant_custom_layout_count >= RADIANT_CUSTOM_LAYOUT_MAX_REGISTRY) return nullptr;
    return &g_radiant_custom_layouts[g_radiant_custom_layout_count++];
}

static bool radiant_custom_layout_parse_result(const CustomLayoutContext* context,
                                               Item result_item,
                                               CustomLayoutResult* result) {
    if (!context || !result) return false;
    Item width_item = radiant_obj_get(result_item, "width");
    float width = 0.0f;
    if (!radiant_item_to_float(width_item, &width)) {
        width_item = radiant_obj_get(result_item, "wd");
    }
    if (radiant_item_to_float(width_item, &width)) {
        result->width = width;
        result->has_width = true;
    }
    Item height_item = radiant_obj_get(result_item, "height");
    float height = 0.0f;
    if (!radiant_item_to_float(height_item, &height)) {
        height_item = radiant_obj_get(result_item, "hg");
    }
    if (radiant_item_to_float(height_item, &height)) {
        result->height = height;
        result->has_height = true;
    }
    Item baseline_item = radiant_obj_get(result_item, "baseline");
    float baseline = 0.0f;
    if (radiant_item_to_float(baseline_item, &baseline)) {
        result->baseline = baseline;
        result->has_baseline = true;
    }

    Item placements = radiant_obj_get(result_item, "placements");
    if (get_type_id(placements) != LMD_TYPE_ARRAY || !placements.array) {
        log_error("CUSTOM_LAYOUT_LAMBDA_RESULT: result.placements must be an array");
        return false;
    }

    bool ok = true;
    for (int i = 0; i < placements.array->length; i++) {
        Item placement = placements.array->items[i];
        Item index_item = radiant_obj_get(placement, "index");
        int child_index = -1;
        if (!radiant_item_to_index(index_item, &child_index)) {
            index_item = radiant_obj_get(placement, "child_index");
            radiant_item_to_index(index_item, &child_index);
        }
        if (child_index < 0) {
            Item child = radiant_obj_get(placement, "child");
            Item child_index_item = radiant_obj_get(child, "index");
            radiant_item_to_index(child_index_item, &child_index);
        }

        float x = 0.0f;
        float y = 0.0f;
        bool has_x = radiant_item_to_float(radiant_obj_get(placement, "x"), &x);
        bool has_y = radiant_item_to_float(radiant_obj_get(placement, "y"), &y);
        if (!has_x || !has_y || child_index < 0 || child_index >= context->child_count ||
            !custom_layout_result_place(result, child_index, x, y)) {
            log_error("CUSTOM_LAYOUT_LAMBDA_PLACEMENT: invalid placement at index %d", i);
            ok = false;
        } else {
            Item z_item = radiant_obj_get(placement, "z");
            int z = 0;
            if (radiant_item_to_int(z_item, &z)) {
                CustomLayoutPlacement* stored = &result->placements[result->placement_count - 1];
                stored->z = z;
                stored->has_z = true;
            }
        }
    }
    if (ok && !radiant_custom_layout_parse_paint_layers(context, result_item)) {
        ok = false;
    }
    return ok;
}

static bool radiant_lambda_custom_layout_callback(const CustomLayoutContext* context,
                                                  CustomLayoutResult* result) {
    if (!radiant_host_api || !radiant_host_api->script || !context || !result) return false;
    Heap* owner_heap = radiant_custom_layout_heap(context);
    RadiantCustomLayoutEntry* entry = radiant_custom_layout_entry(context->layout_name, owner_heap);
    if (!entry || get_type_id(entry->fn) != LMD_TYPE_FUNC) {
        log_error("CUSTOM_LAYOUT_LAMBDA_MISSING_FN: layout='%s' heap=%p",
                  context && context->layout_name ? context->layout_name : "(null)", owner_heap);
        return false;
    }

    uint64_t previous_pass_id = g_radiant_velmt_active_pass_id;
    uint64_t pass_id = g_radiant_velmt_next_pass_id++;
    if (g_radiant_velmt_next_pass_id == 0) g_radiant_velmt_next_pass_id = 1;
    g_radiant_velmt_active_pass_id = pass_id;

    EvalContext* callback_context = nullptr;
    Context* saved_input_context = input_context;
    Runtime* runtime = (context->parent && context->parent->doc)
        ? context->parent->doc->lambda_runtime : nullptr;
    if (runtime && runtime->heap) {
        callback_context = runtime_get_eval_context(runtime);
        if (!callback_context) {
            g_radiant_velmt_active_pass_id = previous_pass_id;
            return false;
        }
        callback_context->heap = runtime->heap;
        callback_context->name_pool = runtime->name_pool;
        callback_context->pool = runtime->heap->pool;
        callback_context->type_info = type_info;
        // Retained callbacks borrow their Runtime-owned side stack rather
        // than fabricating an activation-local context.
        if (!lambda_side_stack_bind()) {
            g_radiant_velmt_active_pass_id = previous_pass_id;
            log_error("CUSTOM_LAYOUT_LAMBDA_SIDE_STACK: layout='%s'", context->layout_name);
            return false;
        }
        if (runtime->ui_mode && runtime->result_arena) {
            callback_context->ui_mode = true;
            callback_context->arena = runtime->result_arena;
            input_context = (Context*)callback_context;
        } else {
            input_context = nullptr;
        }
        if (!eval_context_init(callback_context)) {
            input_context = saved_input_context;
            g_radiant_velmt_active_pass_id = previous_pass_id;
            return false;
        }
    }

    bool ok = false;
    {
        // Every argument builder allocates. Keep prior arguments, the retained
        // callback, and its result exact-rooted until result parsing completes.
        RootFrame roots(5);
        Rooted<Item> rooted_fn(roots, entry->fn);
        Rooted<Item> rooted_parent(roots, radiant_layout_parent_item(context));
        Rooted<Item> rooted_children(roots, radiant_layout_children_item(context));
        Rooted<Item> rooted_layout_context(roots, radiant_layout_context_item(context));
        Rooted<Item> rooted_result(roots, ItemNull);
        LAMBDA_SCALAR_HOME(callback_result_home);
        // Lambda-registered callbacks are core Function values; the Jube script
        // call hook is JS-specific. MIR callbacks also require the explicit
        // caller home so native layout dispatch cannot shift their public ABI.
        rooted_result.set(radiant_lambda_fn_call3_into(rooted_fn.get().function,
            rooted_parent.get(), rooted_children.get(), rooted_layout_context.get(),
            &callback_result_home));
        if (get_type_id(rooted_result.get()) == LMD_TYPE_ERROR) {
            log_error("CUSTOM_LAYOUT_LAMBDA_EXCEPTION: layout='%s'", context->layout_name);
        } else {
            ok = radiant_custom_layout_parse_result(context, rooted_result.get(), result);
        }
    }
    // Retained callback diagnostics belong to the canonical document context
    // and are consumed at this host boundary.
    if (callback_context && callback_context->last_error) {
        err_free(callback_context->last_error);
        callback_context->last_error = nullptr;
    }
    g_radiant_velmt_active_pass_id = previous_pass_id;
    input_context = saved_input_context;
    return ok;
}

static DomDocument* radiant_dom_document_from_node(DomNode* node) {
    DomNode* current = node;
    while (current) {
        if (current->is_element()) {
            DomElement* elem = current->as_element();
            if (elem && elem->doc) return elem->doc;
        }
        current = current->parent;
    }
    return nullptr;
}

static DomNode* radiant_dom_node_from_item(Item node_item, const char* func_name) {
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(node_item);
    if (!node) {
        log_error("JUBE_RADIANT_%s: expected DOM node wrapper", func_name);
    }
    return node;
}

static DomElement* radiant_dom_element_from_item(Item node_item, const char* func_name) {
    DomNode* node = radiant_dom_node_from_item(node_item, func_name);
    if (!node) return nullptr;
    if (!node->is_element()) {
        log_error("JUBE_RADIANT_%s: expected DOM element wrapper", func_name);
        return nullptr;
    }
    return node->as_element();
}

static DomDocument* radiant_load_html_source(const char* html_source, int viewport_width,
                                             int viewport_height, const char* func_name) {
    if (!html_source || !html_source[0]) {
        log_error("JUBE_RADIANT_%s: missing HTML source", func_name);
        return nullptr;
    }

    Url* source_url = get_current_dir();
    Pool* doc_pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "radiant.render.document");
    if (!source_url || !doc_pool) {
        log_error("JUBE_RADIANT_%s: failed to create in-memory document inputs", func_name);
        if (source_url) url_destroy(source_url);
        if (doc_pool) pool_destroy(doc_pool);
        return nullptr;
    }

    DomDocument* doc = load_lambda_html_doc(source_url, NULL, viewport_width,
                                            viewport_height, doc_pool, html_source,
                                            false, false);
    if (!doc) {
        log_error("JUBE_RADIANT_%s: failed to parse in-memory HTML", func_name);
        url_destroy(source_url);
        pool_destroy(doc_pool);
        return nullptr;
    }

    if (!doc->root) {
        log_error("JUBE_RADIANT_%s: in-memory document has no root element", func_name);
        free_document(doc);
        return nullptr;
    }
    return doc;
}

static DomDocument* radiant_load_html_document(const char* path, const char* func_name) {
    if (!path || !path[0]) {
        log_error("JUBE_RADIANT_%s: missing HTML path", func_name);
        return nullptr;
    }

    Url* cwd = get_current_dir();
    Url* html_url = parse_url(cwd, path);
    if (cwd) url_destroy(cwd);

    Pool* doc_pool = mem_pool_create(NULL, MEM_ROLE_LAYOUT, "radiant.document");
    if (!html_url || !doc_pool) {
        log_error("JUBE_RADIANT_%s: failed to create document inputs for '%s'", func_name, path);
        if (html_url) url_destroy(html_url);
        if (doc_pool) pool_destroy(doc_pool);
        return nullptr;
    }

    DomDocument* doc = load_lambda_html_doc(html_url, NULL, 800, 600, doc_pool,
                                            nullptr, false, false);
    if (!doc) {
        log_error("JUBE_RADIANT_%s: failed to load HTML document '%s'", func_name, path);
        url_destroy(html_url);
        pool_destroy(doc_pool);
        return nullptr;
    }
    if (!doc->root) {
        log_error("JUBE_RADIANT_%s: document '%s' has no root element", func_name, path);
        free_document(doc);
        return nullptr;
    }
    return doc;
}

static void radiant_layout_resource_destroy(void* data) {
    RadiantLayoutResource* resource = (RadiantLayoutResource*)data;
    if (!resource) return;
    // The document destroys resources after its view tree; detaching prevents
    // UiContext cleanup from recursively taking ownership of the same document.
    resource->ui_context.document = nullptr;
    ui_context_cleanup(&resource->ui_context);
    mem_free(resource);
}

static RadiantLayoutResource* radiant_layout_resource_for_document(
    DomDocument* doc, const char* func_name) {
    if (!doc) return nullptr;
    for (DomDocumentResource* entry = doc->resources; entry; entry = entry->next) {
        if (entry->destroy == radiant_layout_resource_destroy) {
            return (RadiantLayoutResource*)entry->data;
        }
    }

    RadiantLayoutResource* resource = (RadiantLayoutResource*)mem_calloc(
        1, sizeof(RadiantLayoutResource), MEM_CAT_LAYOUT);
    if (!resource) return nullptr;
    if (ui_context_init(&resource->ui_context, true, 1.0f) != 0) {
        log_error("JUBE_RADIANT_%s: failed to initialize retained UI context", func_name);
        mem_free(resource);
        return nullptr;
    }
    if (!dom_document_add_resource(doc, resource, radiant_layout_resource_destroy)) {
        resource->ui_context.document = nullptr;
        ui_context_cleanup(&resource->ui_context);
        mem_free(resource);
        return nullptr;
    }
    return resource;
}

static bool radiant_layout_document(DomDocument* doc, UiContext* uicon,
                                    int viewport_width, int viewport_height,
                                    const char* func_name) {
    if (!doc || !uicon) return false;

    ui_context_create_surface(uicon, viewport_width, viewport_height);
    uicon->window_width = viewport_width;
    uicon->window_height = viewport_height;
    uicon->document = doc;
    process_document_font_faces(uicon, doc);
    // custom layout callbacks run only during an explicit layout pass; geometry
    // reads cannot substitute for this lifecycle boundary.
    // A retained document already owns its ViewTree shell; treating another
    // pass as initial layout overwrites that shell and leaks its layout pool.
    layout_html_doc(uicon, doc, doc->view_tree != nullptr);
    return doc->view_tree && doc->view_tree->root;
}

RADIANT_C_API Item fn_radiant_load(Item path_item) {
    DomDocument* doc = radiant_load_html_document(fn_to_cstr(path_item), "LOAD");
    if (!doc || !doc->root) return ItemNull;
    // The POC exposes the document through its root wrapper until document
    // wrappers become a first-class native type in the VMap phase.
    return radiant_dom_wrap_node(doc->root);
}

RADIANT_C_API Item fn_radiant_root(Item doc_item) {
    DomNode* node = radiant_dom_node_from_item(doc_item, "ROOT");
    if (!node) return ItemNull;
    DomDocument* doc = radiant_dom_document_from_node(node);
    if (!doc || !doc->root) {
        log_error("JUBE_RADIANT_ROOT: DOM node has no owning root document");
        return ItemNull;
    }
    return radiant_dom_wrap_node(doc->root);
}

// ES30's traversal surface is deliberately element-only. Text/comment nodes
// remain invisible to policy, while every returned wrapper still goes through
// the canonical bridge and its generation check.
RADIANT_C_API Item fn_radiant_document_root(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "DOCUMENT_ROOT");
    if (!node) return ItemNull;
    DomDocument* doc = radiant_dom_document_from_node(node);
    return doc && doc->root ? radiant_dom_wrap_node(doc->root) : ItemNull;
}

RADIANT_C_API Item fn_radiant_first_element_child(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "FIRST_ELEMENT_CHILD");
    DomElement* elem = node && node->is_element() ? node->as_element() : nullptr;
    if (!elem) return ItemNull;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (child->is_element()) return radiant_dom_wrap_node(child->as_element());
    }
    return ItemNull;
}

RADIANT_C_API Item fn_radiant_next_element_sibling(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "NEXT_ELEMENT_SIBLING");
    if (!node) return ItemNull;
    for (DomNode* sibling = node->next_sibling; sibling;
         sibling = sibling->next_sibling) {
        if (sibling->is_element()) return radiant_dom_wrap_node(sibling->as_element());
    }
    return ItemNull;
}

// ES30: native answers only layout-coupled focus eligibility. The returned
// snapshot preserves DOM order; focus.ls owns HTML's tabindex ordering.
RADIANT_C_API Item fn_radiant_focus_candidates(Item root_item) {
    DomElement* root = radiant_dom_element_from_item(root_item, "FOCUS_CANDIDATES");
    if (!root) return radiant_array_new_item(0);

    ArrayList* candidates = arraylist_new(16);
    if (!candidates) return ItemNull;
    focus_collect_candidates((View*)root, candidates, false);

    RootFrame roots(2);
    Rooted<Item> result(roots, radiant_array_new_item(candidates->length));
    Rooted<Item> candidate(roots, ItemNull);
    for (int i = 0; i < candidates->length; i++) {
        View* view = static_cast<View*>(candidates->data[i]);
        candidate.set(radiant_obj_new());
        radiant_rooted_obj_set(candidate, "node",
                               radiant_dom_wrap_node((DomElement*)view));
        int tab_index = focus_tab_index(view);
        radiant_rooted_obj_set(candidate, "tab_index", radiant_int_item(tab_index));
        radiant_rooted_obj_set(candidate, "order", radiant_int_item(i));
        radiant_rooted_obj_set(candidate, "sequential",
                               radiant_bool_item(is_view_focusable(view)));
        radiant_array_push_item(result.get(), candidate.get());
    }
    arraylist_free(candidates);
    return result.get();
}

RADIANT_C_API Item fn_radiant_focused(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "FOCUSED");
    DocState* state = elem && elem->doc ? (DocState*)elem->doc->state : nullptr;
    return radiant_bool_item(state && focus_get(state) == (View*)elem);
}

RADIANT_C_API Item fn_radiant_focus_set(Item node_item, Item from_keyboard_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "FOCUS_SET");
    DocState* state = elem && elem->doc ? (DocState*)elem->doc->state : nullptr;
    if (!state || !is_view_programmatically_focusable((View*)elem)) {
        return radiant_bool_item(false);
    }
    focus_set(state, (View*)elem, is_truthy(from_keyboard_item));
    return radiant_bool_item(true);
}

RADIANT_C_API Item fn_radiant_scroll_into_view(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "SCROLL_INTO_VIEW");
    if (!elem) return radiant_bool_item(false);
    dom_scroll_into_view_bridge(elem);
    return radiant_bool_item(true);
}

RADIANT_C_API Item fn_radiant_embedding_element(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "EMBEDDING_ELEMENT");
    if (!node) return ItemNull;
    DomDocument* doc = radiant_dom_document_from_node(node);
    DomElement* iframe = dom_document_embedding_element(doc);
    return iframe ? radiant_dom_wrap_node(iframe) : ItemNull;
}

RADIANT_C_API Item fn_radiant_embedded_document_root(Item iframe_item) {
    DomElement* iframe = radiant_dom_element_from_item(iframe_item,
                                                        "EMBEDDED_DOCUMENT_ROOT");
    if (!iframe || iframe->tag() != MARKUP_NAME_IFRAME || !iframe->embed ||
        !iframe->embedp()->doc || !iframe->embedp()->doc->root) {
        return ItemNull;
    }
    return radiant_dom_wrap_node(iframe->embedp()->doc->root);
}

RADIANT_C_API Item fn_radiant_attr(Item node_item, Item name_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "ATTR");
    const char* name = fn_to_cstr(name_item);
    if (!elem || !name || !name[0]) return ItemNull;
    return radiant_string_item(elem->get_attribute(name));
}

RADIANT_C_API Item fn_radiant_set_attr(Item node_item, Item name_item, Item value_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "SET_ATTR");
    const char* name = fn_to_cstr(name_item);
    const char* value = fn_to_cstr(value_item);
    if (!elem || !name || !name[0]) return ItemNull;
    // A null value removes the attribute. ARIA needs both halves of that: some
    // mirrors are present-or-absent (aria-disabled), while aria-invalid is
    // deliberately written "false" rather than removed, because assistive tech
    // reads an explicit false as "validation ran and this control is OK" (F7).
    //
    // Ask the Item, not the C string: fn_to_cstr maps every non-text Item —
    // null included — to "", never to nullptr, so `!value` was unreachable and
    // a clear wrote an empty attribute instead of removing one. Silent because
    // the common case is clearing an attribute that is already absent, which
    // aria.ls's set_if_changed elides before reaching here; it only showed on a
    // real present -> absent transition.
    if (radiant_item_is_missing(value_item)) {
        Item args1[1] = {name_item};
        radiant_dom_element_operation(node_item, JUBE_DOM_REMOVE_ATTRIBUTE, args1, 1);
        return node_item;
    }
    // Attribute writes from Lambda must share JS DOM side effects such as
    // event-attribute compilation, selection refresh, and mutation notices.
    Item args[2] = {name_item, value_item};
    radiant_dom_element_operation(node_item, JUBE_DOM_SET_ATTRIBUTE, args, 2);
    return node_item;
}

// ---------------------------------------------------------------------------
// Interaction-state primitives (vibe/Lambda_Design_DOM_State.md ES4/ES6).
// Behavior templates read and write canonical engine state through these; the
// storage itself stays native so layout, paint, and the CSS selector matcher
// keep reading it directly.
// ---------------------------------------------------------------------------

// Lambda spells state names in snake_case; the engine interns CSS pseudo-class
// spellings. `read_only` marks the hot states the native transition code owns —
// hover/active/focus change per pointer move, so script may observe but not
// drive them (the hot-path guard would otherwise be meaningless).
// Engine-backed state is mostly boolean pseudo-class state, but a few names
// carry a payload (ES4). `value` is text: the HTML attribute is its default and
// the live buffer answers once the control has one, exactly as `checked` falls
// back to the `checked` attribute until a ViewState bit exists.
typedef enum RadiantStateKind { RSTATE_BOOL = 0, RSTATE_TEXT } RadiantStateKind;

static const struct {
    const char* lambda_name;
    const char* state_name;
    // Read-only covers two kinds of name: the hot ones a handler must never
    // drive (hover/active/focus…), and the *derived* ones — required, optional,
    // readonly — which are pure functions of the markup since the reflection
    // pass retired (F3b/ES16). A write to a derived name is a category error:
    // it would route to form_control_set_*, mutating the control instead of
    // recording a verdict. Changing one means changing the attribute (ESO36).
    bool read_only;
    uint32_t pseudo_flag;   // 0 when the name drives no CSS pseudo-class
    RadiantStateKind kind;
} RADIANT_STATE_NAME_MAP[] = {
    {"hover",             STATE_HOVER,          true,  PSEUDO_STATE_HOVER, RSTATE_BOOL},
    {"active",            STATE_ACTIVE,         true,  PSEUDO_STATE_ACTIVE, RSTATE_BOOL},
    {"focus",             STATE_FOCUS,          true,  PSEUDO_STATE_FOCUS, RSTATE_BOOL},
    {"focus_within",      STATE_FOCUS_WITHIN,   true,  0, RSTATE_BOOL},
    {"focus_visible",     STATE_FOCUS_VISIBLE,  true,  0, RSTATE_BOOL},
    {"visited",           STATE_VISITED,        false, PSEUDO_STATE_VISITED, RSTATE_BOOL},
    {"link",              STATE_LINK,           false, PSEUDO_STATE_LINK, RSTATE_BOOL},
    {"checked",           STATE_CHECKED,        false, PSEUDO_STATE_CHECKED, RSTATE_BOOL},
    {"indeterminate",     STATE_INDETERMINATE,  false, PSEUDO_STATE_INDETERMINATE, RSTATE_BOOL},
    {"disabled",          STATE_DISABLED,       false, PSEUDO_STATE_DISABLED, RSTATE_BOOL},
    {"enabled",           STATE_ENABLED,        false, PSEUDO_STATE_ENABLED, RSTATE_BOOL},
    {"readonly",          STATE_READONLY,       true,  PSEUDO_STATE_READ_ONLY, RSTATE_BOOL},
    {"valid",             STATE_VALID,          false, PSEUDO_STATE_VALID, RSTATE_BOOL},
    {"invalid",           STATE_INVALID,        false, PSEUDO_STATE_INVALID, RSTATE_BOOL},
    {"required",          STATE_REQUIRED,       true,  PSEUDO_STATE_REQUIRED, RSTATE_BOOL},
    {"optional",          STATE_OPTIONAL,       true,  PSEUDO_STATE_OPTIONAL, RSTATE_BOOL},
    {"placeholder_shown", STATE_PLACEHOLDER,    false, 0, RSTATE_BOOL},
    {"selected",          STATE_SELECTED,       false, 0, RSTATE_BOOL},
    // text-valued: no interned pseudo name and no pseudo-class of its own
    {"value",             "value",              false, 0, RSTATE_TEXT},
};

static const char* radiant_state_name_lookup(const char* lambda_name, bool* out_read_only,
                                            uint32_t* out_pseudo_flag = nullptr,
                                            RadiantStateKind* out_kind = nullptr) {
    if (!lambda_name) return nullptr;
    for (size_t i = 0; i < sizeof(RADIANT_STATE_NAME_MAP) / sizeof(RADIANT_STATE_NAME_MAP[0]); i++) {
        if (strcmp(RADIANT_STATE_NAME_MAP[i].lambda_name, lambda_name) == 0) {
            if (out_read_only) *out_read_only = RADIANT_STATE_NAME_MAP[i].read_only;
            if (out_pseudo_flag) *out_pseudo_flag = RADIANT_STATE_NAME_MAP[i].pseudo_flag;
            if (out_kind) *out_kind = RADIANT_STATE_NAME_MAP[i].kind;
            return RADIANT_STATE_NAME_MAP[i].state_name;
        }
    }
    return nullptr;
}

// Resolve the element and its document's state store in one step; every state
// primitive needs both and must fail the same way when either is missing.
static DocState* radiant_state_for_element(Item node_item, const char* op,
                                           DomElement** out_elem) {
    DomElement* elem = radiant_dom_element_from_item(node_item, op);
    if (!elem) return nullptr;
    DomDocument* doc = radiant_dom_document_from_node((DomNode*)elem);
    if (!doc) {
        log_error("JUBE_RADIANT_%s: element has no owning document", op);
        return nullptr;
    }
    // The store is created lazily — a headless `radiant.load()` document has
    // none until something asks for state, so ensure rather than require it.
    DocState* state = radiant_document_ensure_state(doc, op);
    if (!state) {
        log_error("JUBE_RADIANT_%s: could not open document state store", op);
        return nullptr;
    }
    if (out_elem) *out_elem = elem;
    return state;
}

RADIANT_C_API Item fn_radiant_get_state(Item node_item, Item name_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "GET_STATE", &elem);
    const char* name = fn_to_cstr(name_item);
    if (!state || !name) return ItemNull;
    RadiantStateKind kind = RSTATE_BOOL;
    const char* interned = radiant_state_name_lookup(name, nullptr, nullptr, &kind);
    if (!interned) {
        log_error("JUBE_RADIANT_GET_STATE: unknown state name '%s'", name);
        return ItemNull;
    }
    if (kind == RSTATE_TEXT) {
        // Live value when the control has a buffer, else the `value` attribute
        // — the attribute is the default, the buffer is the current value.
        FormControlProp* f = elem->form_control();
        if (f && f->current_value) return radiant_string_item(f->current_value);
        const char* attr = elem->get_attribute("value");
        return radiant_string_item(attr ? attr : "");
    }
    return (Item){.item = b2it(state_get_bool(state, elem, interned) ? 1 : 0)};
}

RADIANT_C_API Item fn_radiant_set_state(Item node_item, Item name_item, Item value_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "SET_STATE", &elem);
    const char* name = fn_to_cstr(name_item);
    if (!state || !name) return (Item){.item = b2it(0)};
    bool read_only = false;
    uint32_t pseudo_flag = 0;
    RadiantStateKind kind = RSTATE_BOOL;
    const char* interned = radiant_state_name_lookup(name, &read_only, &pseudo_flag, &kind);
    if (!interned) {
        log_error("JUBE_RADIANT_SET_STATE: unknown state name '%s'", name);
        return (Item){.item = b2it(0)};
    }
    if (read_only) {
        log_error("JUBE_RADIANT_SET_STATE: '%s' is engine-owned and read-only", name);
        return (Item){.item = b2it(0)};
    }
    // state_set_bool routes each name to its canonical home — packed ViewState
    // bits, the form-control writers, or the generic state map — and schedules
    // the pseudo-class restyle, so script never bypasses that bookkeeping.
    if (kind == RSTATE_TEXT) {
        // tc_set_value is the canonical writer: it replaces the buffer, collapses
        // the selection, refreshes placeholder state and mirrors the legacy
        // pointer the renderer reads. Script must not poke the buffer directly.
        if (!tc_is_text_control(elem)) {
            log_error("JUBE_RADIANT_SET_STATE: '%s' is only defined on text controls", name);
            return (Item){.item = b2it(0)};
        }
        const char* text = fn_to_cstr(value_item);
        if (!text) text = "";
        tc_set_value(elem, text, strlen(text));
        return (Item){.item = b2it(1)};
    }
    bool want = is_truthy(value_item);
    // Only a real change is worth a restyle. The native writers return early
    // when the value is unchanged, and validation re-runs on every keystroke,
    // so syncing unconditionally would schedule a reflow per keypress.
    // A first write counts as a change even when it equals the default: the
    // cascade has never seen this bit, so nothing has matched on it yet (ESO32).
    bool first_write = state_get(state, elem, interned).item == ItemNull.item;
    bool changed = first_write || state_get_bool(state, elem, interned) != want;
    state_set_bool(state, elem, interned, want);
    // the canonical bit is written; CSS only sees it once the pseudo-class
    // cascade re-runs, which the native writers schedule at each call site
    if (changed && pseudo_flag) radiant_sync_pseudo_state((View*)elem, pseudo_flag, want);
    // Report what actually happened, not merely that a writer was called: a
    // form-state write is a no-op until layout has built the control's
    // FormControlProp, and a silent false success would hide that.
    bool got = state_get_bool(state, elem, interned);
    if (got != want) {
        log_error("JUBE_RADIANT_SET_STATE: '%s' did not take on <%s> (control state needs layout?)",
                  name, elem->tag_name ? elem->tag_name : "?");
    }
    return (Item){.item = b2it(got == want ? 1 : 0)};
}

extern "C" bool radiant_dispatch_event_from_script(void* dom_node, const char* event_name);

RADIANT_C_API Item fn_radiant_dispatch(Item node_item, Item name_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "DISPATCH");
    const char* name = fn_to_cstr(name_item);
    if (!elem || !name || !name[0]) return (Item){.item = b2it(0)};
    bool ok = radiant_dispatch_event_from_script((void*)elem, name);
    return (Item){.item = b2it(ok ? 1 : 0)};
}

// Walk a subtree collecting radio controls that share `name`. The tree walk is
// mechanism and stays native; which peers to clear, and when, is the behavior
// template's policy (ES6).
// One document-order walk for every "elements in this scope sharing that
// control name" question. Radio exclusivity and details exclusivity differ only
// in which elements qualify, how far the scope reaches, and whether the subject
// counts itself — so the walk and its count-then-fill pass are parameterised
// rather than copied into a second near-identical collector.
typedef bool (*RadiantNamedPeerMatch)(DomElement* elem, const char* name);

static bool radiant_tag_ieq(const char* tag, const char* lower, const char* upper) {
    return tag && (strcmp(tag, lower) == 0 || strcmp(tag, upper) == 0);
}

static bool radiant_named_peer_is_radio(DomElement* elem, const char* name) {
    if (!radiant_tag_ieq(elem->tag_name, "input", "INPUT")) return false;
    const char* type = elem->get_attribute("type");
    const char* peer = elem->get_attribute("name");
    return type && strcmp(type, "radio") == 0 && peer && strcmp(peer, name) == 0;
}

static bool radiant_named_peer_is_details(DomElement* elem, const char* name) {
    if (!radiant_tag_ieq(elem->tag_name, "details", "DETAILS")) return false;
    const char* peer = elem->get_attribute("name");
    return peer && strcmp(peer, name) == 0;
}

static void radiant_collect_named_peers(DomNode* node, const char* name,
                                        RadiantNamedPeerMatch match, DomElement* skip,
                                        Item out_array, int* count, bool count_only) {
    for (DomNode* n = node; n; n = n->next_sibling) {
        if (n->node_type == DOM_NODE_ELEMENT) {
            DomElement* elem = n->as_element();
            if (!elem) continue;
            if (elem != skip && match(elem, name)) {
                if (count_only) { (*count)++; }
                else { radiant_array_push_item(out_array, radiant_dom_wrap_node(elem)); }
            }
            radiant_collect_named_peers(elem->first_child, name, match, skip,
                                        out_array, count, count_only);
        }
    }
}

// Sized in one pass, filled in a second: the array is allocated exactly and the
// GC root is held across the fill, which is why this is not a single push loop.
static Item radiant_named_peer_array(DomNode* scope, const char* name,
                                     RadiantNamedPeerMatch match, DomElement* skip) {
    if (!scope) return radiant_array_new_item(0);
    int count = 0;
    radiant_collect_named_peers(scope, name, match, skip, ItemNull, &count, true);
    RootFrame roots(1);
    Rooted<Item> rooted(roots, radiant_array_new_item(count));
    int unused = 0;
    radiant_collect_named_peers(scope, name, match, skip, rooted.get(), &unused, false);
    return rooted.get();
}

// Root to search from: the owning <form> when there is one, else the document.
static DomNode* radiant_radio_scope(DomElement* elem) {
    for (DomNode* n = (DomNode*)elem; n; n = n->parent) {
        if (n->node_type != DOM_NODE_ELEMENT) continue;
        DomElement* e = n->as_element();
        if (!e) continue;
        const char* tag = e->tag_name;
        if (tag && (strcmp(tag, "FORM") == 0 || strcmp(tag, "form") == 0)) return n;
    }
    DomDocument* doc = radiant_dom_document_from_node((DomNode*)elem);
    return doc ? (DomNode*)doc->root : nullptr;
}

RADIANT_C_API Item fn_radiant_form_of(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "FORM_OF");
    if (!elem) return ItemNull;
    for (DomNode* n = (DomNode*)elem; n; n = n->parent) {
        if (n->node_type != DOM_NODE_ELEMENT) continue;
        DomElement* e = n->as_element();
        if (!e) continue;
        const char* tag = e->tag_name;
        if (tag && (strcmp(tag, "FORM") == 0 || strcmp(tag, "form") == 0)) {
            return radiant_dom_wrap_node(e);
        }
    }
    return ItemNull;
}

// Presence, not value. An HTML boolean attribute (`open`, `disabled`, `checked`
// in markup) is written with an empty value, and `get_attribute` answers null
// for it — so `attr(node, name) != null` reports *absent* for exactly the
// attributes whose whole meaning is presence. This is the read half of the
// removal half `set_attr` already has (F7), and the same split the DOM draws
// between getAttribute and hasAttribute.
RADIANT_C_API Item fn_radiant_has_attr(Item node_item, Item name_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "HAS_ATTR");
    const char* name = fn_to_cstr(name_item);
    if (!elem || !name || !name[0]) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(elem->has_attribute(name) ? 1 : 0)};
}

// Tree navigation at the waist. `form_of` already answers the one specialised
// case ("nearest ancestor <form>"); templates need the general shape too — a
// <summary> must reach its <details>. These cannot be read off `~` directly:
// a wrapped element carries the `html_element` interface, whose member table
// has no dom_node traversal members, and the free-function form is what the
// rest of the package already speaks (`attr`, `get_state`, `radio_group`).
// `closest` delegates to JUBE_DOM_CLOSEST rather than re-walking with its own
// selector matcher, so there is exactly one implementation of the search.
RADIANT_C_API Item fn_radiant_parent(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "PARENT");
    if (!elem) return ItemNull;
    DomNode* parent = ((DomNode*)elem)->parent;
    // parentElement semantics: a non-element parent (the document) answers null
    if (!parent || parent->node_type != DOM_NODE_ELEMENT) return ItemNull;
    DomElement* parent_elem = parent->as_element();
    return parent_elem ? radiant_dom_wrap_node(parent_elem) : ItemNull;
}

RADIANT_C_API Item fn_radiant_closest(Item node_item, Item selector_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "CLOSEST");
    const char* selector = fn_to_cstr(selector_item);
    if (!elem || !selector || !selector[0]) return ItemNull;
    Item args[1] = {selector_item};
    return radiant_dom_element_operation(node_item, JUBE_DOM_CLOSEST, args, 1);
}

static void* radiant_optional_dom_element(Item node_item) {
    if (radiant_item_is_missing(node_item)) return nullptr;
    DomNode* node = (DomNode*)radiant_dom_unwrap_node(node_item);
    return node && node->is_element() ? (void*)node : nullptr;
}

// F4: form-data construction remains a native tree walk, but the package gets
// the resulting entry list and owns serialization/order decisions.
RADIANT_C_API Item fn_radiant_form_entries(Item form_item, Item submitter_item) {
    DomElement* form = radiant_dom_element_from_item(form_item, "FORM_ENTRIES");
    if (!form || !form->tag_name || strcasecmp(form->tag_name, "form") != 0) {
        return ItemNull;
    }
    return js_formdata_collect_form_entries(
        (void*)form, radiant_optional_dom_element(submitter_item));
}

RADIANT_C_API Item fn_radiant_form_url(Item form_item) {
    DomElement* form = radiant_dom_element_from_item(form_item, "FORM_URL");
    if (!form || !form->doc || !form->doc->url) return radiant_string_item("");
    return radiant_string_item(url_get_href(form->doc->url));
}

RADIANT_C_API Item fn_radiant_form_encode(Item value_item) {
    const char* value = fn_to_cstr(value_item);
    if (!value) value = "";
    size_t value_len = strlen(value);
    size_t encoded_len = url_encode_measure(value, value_len, URL_KEEP_FORM,
                                            true, nullptr);
    char* encoded = (char*)mem_alloc(encoded_len + 1, MEM_CAT_TEMP);
    if (!encoded) return ItemNull;
    url_encode_write(value, value_len, URL_KEEP_FORM, true, encoded);
    encoded[encoded_len] = '\0';
    Item result = radiant_string_item_n(encoded, encoded_len);
    mem_free(encoded);
    return result;
}

RADIANT_C_API Item fn_radiant_submit_event(Item form_item, Item submitter_item) {
    DomElement* form = radiant_dom_element_from_item(form_item, "SUBMIT_EVENT");
    if (!form || !form->tag_name || strcasecmp(form->tag_name, "form") != 0) {
        return radiant_bool_item(false);
    }
    return radiant_bool_item(radiant_dispatch_submit_event_from_script(
        (void*)form, radiant_optional_dom_element(submitter_item)));
}

static bool radiant_is_constraint_control(DomElement* elem) {
    if (!elem || !elem->tag_name) return false;
    return strcasecmp(elem->tag_name, "input") == 0 ||
           strcasecmp(elem->tag_name, "select") == 0 ||
           strcasecmp(elem->tag_name, "textarea") == 0 ||
           strcasecmp(elem->tag_name, "button") == 0;
}

static DomElement* radiant_first_invalid_form_control(DomNode* node,
                                                       DocState* state) {
    for (DomNode* current = node; current; current = current->next_sibling) {
        if (!current->is_element()) continue;
        DomElement* elem = current->as_element();
        if (radiant_is_constraint_control(elem) &&
                state_get_bool(state, elem, STATE_INVALID)) {
            return elem;
        }
        DomElement* nested = radiant_first_invalid_form_control(
            elem->first_child, state);
        if (nested) return nested;
    }
    return nullptr;
}

RADIANT_C_API Item fn_radiant_check_validity(Item form_item) {
    DomElement* form = radiant_dom_element_from_item(form_item, "CHECK_VALIDITY");
    if (!form || !form->tag_name || strcasecmp(form->tag_name, "form") != 0) {
        return radiant_bool_item(false);
    }
    DomDocument* doc = radiant_dom_document_from_node((DomNode*)form);
    DocState* state = doc ? radiant_document_ensure_state(doc, "CHECK_VALIDITY")
                            : nullptr;
    if (!state) return radiant_bool_item(false);

    // A live author-script realm owns invalid-event dispatch. Reuse its
    // established validity bridge so every invalid control receives the event.
    if (doc->js_has_dom_realm) {
        Item valid = dom_check_validity_bridge(form_item);
        if (!is_truthy(valid)) {
            dom_focus_first_invalid_form_control((void*)form);
        }
        return radiant_bool_item(is_truthy(valid));
    }

    // F3 owns the constraint pass and publishes one canonical :invalid bit.
    // Submission consumes that verdict directly; rebuilding a JS ValidityState
    // here crosses into a realm that script-less Lambda evaluators do not own.
    DomElement* invalid = radiant_first_invalid_form_control(
        form->first_child, state);
    if (invalid) focus_set_programmatic(state, (View*)invalid);
    return radiant_bool_item(invalid == nullptr);
}

RADIANT_C_API Item fn_radiant_reset_form(Item form_item) {
    DomElement* form = radiant_dom_element_from_item(form_item, "RESET_FORM");
    if (!form || !form->tag_name || strcasecmp(form->tag_name, "form") != 0) {
        return radiant_bool_item(false);
    }
    dom_form_reset_bridge(form_item);
    return radiant_bool_item(true);
}

static uint64_t g_radiant_form_boundary_serial = 1;

RADIANT_C_API Item fn_radiant_form_boundary() {
    char boundary[64];
    snprintf(boundary, sizeof(boundary), "----LambdaFormBoundary-%llu",
             (unsigned long long)g_radiant_form_boundary_serial++);
    return radiant_string_item(boundary);
}

RADIANT_C_API Item fn_radiant_navigation_destination(Item source_item,
                                                      Item url_item,
                                                      Item target_root_item) {
    DomElement* source = radiant_dom_element_from_item(source_item,
                                                        "NAVIGATION_DESTINATION");
    DomElement* target_root = radiant_dom_element_from_item(target_root_item,
                                                             "NAVIGATION_DESTINATION");
    const char* raw_url = fn_to_cstr(url_item);
    RootFrame roots(1);
    Rooted<Item> result(roots, radiant_obj_new());
    radiant_rooted_obj_set(result, "kind", radiant_string_item("document"));
    radiant_rooted_obj_set(result, "fragment", ItemNull);
    if (!source || !source->doc || !target_root || !target_root->doc ||
        !raw_url || !raw_url[0]) {
        return result.get();
    }
    Url* resolved = source->doc->url
        ? url_parse_with_base(raw_url, source->doc->url) : url_parse(raw_url);
    if (!resolved || !url_is_valid(resolved)) {
        if (resolved) url_destroy(resolved);
        return result.get();
    }
    const char* hash = url_get_hash(resolved);
    if (hash && hash[0] == '#' && target_root->doc->url &&
        radiant_urls_match_without_fragment(target_root->doc->url, resolved)) {
        radiant_rooted_obj_set(result, "kind", radiant_string_item("fragment"));
        radiant_rooted_obj_set(result, "fragment", radiant_string_item(hash + 1));
    }
    url_destroy(resolved);
    return result.get();
}

RADIANT_C_API Item fn_radiant_request_navigation(Item request_item) {
    Item source_item = radiant_obj_get(request_item, "source");
    if (!radiant_item_is_missing(source_item)) {
        DomElement* source = radiant_dom_element_from_item(source_item,
                                                            "REQUEST_NAVIGATION");
        Item target_item = radiant_obj_get(request_item, "target");
        DomElement* target = radiant_item_is_missing(target_item) ? nullptr :
            radiant_dom_element_from_item(target_item, "REQUEST_NAVIGATION");
        DomElement* fragment = nullptr;
        Item fragment_item = radiant_obj_get(request_item, "fragment_target");
        if (!radiant_item_is_missing(fragment_item)) {
            fragment = radiant_dom_element_from_item(fragment_item,
                                                      "REQUEST_NAVIGATION");
        }
        const char* url = fn_to_cstr(radiant_obj_get(request_item, "url"));
        const char* kind = fn_to_cstr(radiant_obj_get(request_item, "target_kind"));
        const char* target_name = fn_to_cstr(radiant_obj_get(request_item,
                                                              "target_name"));
        RadiantNavigationTargetKind target_kind = kind && strcmp(kind, "new") == 0
            ? RADIANT_NAVIGATION_TARGET_NEW : RADIANT_NAVIGATION_TARGET_EXISTING;
        return radiant_bool_item(radiant_queue_navigation_request(
            source, url, target, target_kind, target_name, fragment));
    }

    const char* target = fn_to_cstr(radiant_obj_get(request_item, "target"));
    const char* url = fn_to_cstr(radiant_obj_get(request_item, "url"));
    const char* method = fn_to_cstr(radiant_obj_get(request_item, "method"));
    const char* enctype = fn_to_cstr(radiant_obj_get(request_item, "enctype"));
    const char* body = fn_to_cstr(radiant_obj_get(request_item, "body"));
    if (!target || !target[0]) target = "_self";
    if (!method || !method[0]) method = "get";
    if (!enctype || !enctype[0]) enctype = "application/x-www-form-urlencoded";
    if (!url || !url[0]) return radiant_bool_item(false);

    // The browsing layer owns network transport; this waist records the
    // serialized request while handing its URL/target to document navigation.
    log_info("FORM_NAVIGATION_HANDOFF method=%s enctype=%s target=%s url=%s body=%s",
             method, enctype, target, url, body ? body : "");
    return radiant_bool_item(dom_navigate_submit_target(target, url));
}

RADIANT_C_API Item fn_radiant_radio_group(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "RADIO_GROUP");
    if (!elem) return ItemNull;
    const char* name = elem->get_attribute("name");
    // an unnamed radio forms no group; it is its own sole member
    if (!name || !name[0]) return radiant_array_new_item(0);
    // skip = nullptr: the group includes the subject, which is what form.ls
    // relies on when it clears every member before checking the clicked one.
    return radiant_named_peer_array(radiant_radio_scope(elem), name,
                                    radiant_named_peer_is_radio, nullptr);
}

// HTML 4.11.1 exclusive accordion: <details> sharing a non-empty `name` in one
// node tree form a group of which at most one member may be open. Two
// deliberate differences from radio_group:
//   * the scope is the node tree, never the form owner — details grouping has
//     nothing to do with form ownership;
//   * the subject is excluded, so the policy can close "the others" without
//     needing node identity to recognise itself in its own group.
RADIANT_C_API Item fn_radiant_details_group(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "DETAILS_GROUP");
    if (!elem) return ItemNull;
    const char* name = elem->get_attribute("name");
    // an absent or empty name forms no group, so the subject stands alone
    if (!name || !name[0]) return radiant_array_new_item(0);
    DomDocument* doc = radiant_dom_document_from_node((DomNode*)elem);
    return radiant_named_peer_array(doc ? (DomNode*)doc->root : nullptr, name,
                                    radiant_named_peer_is_details, elem);
}

extern "C" bool radiant_select_dropdown_is_open(void* dom_node);
extern "C" bool radiant_select_set_dropdown_open(void* dom_node, bool open);

RADIANT_C_API Item fn_radiant_dropdown_open(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "DROPDOWN_OPEN");
    if (!elem) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(radiant_select_dropdown_is_open((void*)elem) ? 1 : 0)};
}

RADIANT_C_API Item fn_radiant_set_dropdown_open(Item node_item, Item open_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "SET_DROPDOWN_OPEN");
    if (!elem) return (Item){.item = b2it(0)};
    bool ok = radiant_select_set_dropdown_open((void*)elem, is_truthy(open_item));
    return (Item){.item = b2it(ok ? 1 : 0)};
}

// The package chooses activation by declaring this behavior; native resolves
// `popovertarget` and applies the live visibility transition once.
RADIANT_C_API Item fn_radiant_activate_popover(Item node_item) {
    DomElement* button = radiant_dom_element_from_item(node_item, "ACTIVATE_POPOVER");
    if (!button) return radiant_bool_item(false);
    void* popover = dom_popover_target_for_button((void*)button);
    if (!popover) return radiant_bool_item(false);
    int action = dom_popover_target_action((void*)button);
    return radiant_bool_item(dom_activate_popover(popover, action));
}

// Option count for a <select>; the option list itself is layout-owned.
RADIANT_C_API Item fn_radiant_option_count(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "OPTION_COUNT");
    if (!elem || !elem->form_control()) return ItemNull;
    return radiant_int_item((int64_t)elem->form_control()->option_count);
}

RADIANT_C_API Item fn_radiant_selected_index(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "SELECTED_INDEX", &elem);
    if (!state) return ItemNull;
    return radiant_int_item((int64_t)form_control_get_selected_index(state, (View*)elem));
}

RADIANT_C_API Item fn_radiant_set_selected_index(Item node_item, Item index_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "SET_SELECTED_INDEX", &elem);
    if (!state) return (Item){.item = b2it(0)};
    int64_t idx = it2l(index_item);
    form_control_set_selected_index(state, (View*)elem, (int)idx);
    return (Item){.item = b2it(1)};
}

// Custom-validity read, for the dom package's constraint validation (F3). The
// control's value is not here: it is engine-backed state, read through
// get_state(elem, "value") like every other state name (ES4).
RADIANT_C_API Item fn_radiant_custom_validity(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "CUSTOM_VALIDITY");
    if (!elem || !elem->form_control()) return radiant_string_item("");
    const char* msg = elem->form_control()->custom_validity_msg;
    return radiant_string_item(msg ? msg : "");
}

// Is this control one that holds editable text? The dom package's catch-all
// `view <input>` template matches every input, including checkbox and radio,
// so the validation handlers gate on this the same way the retired native pass
// gated on tc_is_text_control — a checkbox's value attribute is not a value to
// length-check or parse.
RADIANT_C_API Item fn_radiant_text_control(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "TEXT_CONTROL");
    if (!elem) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(tc_is_text_control(elem) ? 1 : 0)};
}

// ---- F5: the editing waist -------------------------------------------------
//
// Three unit systems meet here and the conversions live only in this block:
// the buffer is UTF-8 *bytes*, the selection IDL is *UTF-16* code units (that
// is what selectionStart means), and everything Lambda sees is *codepoints* —
// `len`, `slice` and `ord` are all codepoint-indexed, so a template computing
// `slice(value, 0, start)` can only be handed codepoint offsets (ES9).

// Resolve the control's live buffer for offset conversion.
static const char* radiant_tc_buffer(DomElement* elem, uint32_t* out_len) {
    *out_len = 0;
    FormControlProp* f = elem ? elem->form_control() : nullptr;
    if (!f) return nullptr;
    if (f->current_value) { *out_len = f->current_value_len; return f->current_value; }
    if (f->value) { *out_len = (uint32_t)strlen(f->value); return f->value; }
    return "";
}

static uint32_t radiant_cp_to_u16(const char* buf, uint32_t len, uint32_t cp) {
    size_t byte_off = str_utf8_char_to_byte(buf, len, cp);
    if (byte_off == STR_NPOS || byte_off > len) byte_off = len;
    return tc_utf8_to_utf16_length(buf, (uint32_t)byte_off);
}

static uint32_t radiant_u16_to_cp(const char* buf, uint32_t len, uint32_t u16) {
    uint32_t byte_off = tc_utf16_to_utf8_offset(buf, len, u16);
    return (uint32_t)str_utf8_byte_to_char(buf, len, byte_off);
}

// Caret/selection reads, in codepoints.
static Item radiant_selection_edge(Item node_item, const char* op, bool want_end) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, op, &elem);
    if (!state || !elem) return ItemNull;
    uint32_t s = 0, e = 0; uint8_t dir = 0;
    form_control_get_selection(state, (View*)elem, &s, &e, &dir);
    uint32_t len = 0;
    const char* buf = radiant_tc_buffer(elem, &len);
    if (!buf) return ItemNull;
    return radiant_int_item((int64_t)radiant_u16_to_cp(buf, len, want_end ? e : s));
}

RADIANT_C_API Item fn_radiant_selection_start(Item node_item) {
    return radiant_selection_edge(node_item, "SELECTION_START", false);
}

RADIANT_C_API Item fn_radiant_selection_end(Item node_item) {
    return radiant_selection_edge(node_item, "SELECTION_END", true);
}

RADIANT_C_API Item fn_radiant_set_selection(Item node_item, Item start_item, Item end_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "SET_SELECTION", &elem);
    if (!state || !elem) return (Item){.item = b2it(0)};
    uint32_t len = 0;
    const char* buf = radiant_tc_buffer(elem, &len);
    if (!buf) return (Item){.item = b2it(0)};
    int64_t s = it2l(start_item), e = it2l(end_item);
    if (s < 0) s = 0;
    if (e < s) e = s;
    form_control_set_selection(state, (View*)elem,
                               radiant_cp_to_u16(buf, len, (uint32_t)s),
                               radiant_cp_to_u16(buf, len, (uint32_t)e), 0);
    return (Item){.item = b2it(1)};
}

// Counts splices this waist has performed. The engine samples it either side of
// a beforeinput dispatch so it can tell "the applier edited" from "the applier
// claimed the intent but changed nothing" — a maxlength-blocked keystroke is
// the second, and it must not produce an `input` event.
static uint64_t g_radiant_splice_epoch = 0;

extern "C" uint64_t radiant_splice_epoch(void) { return g_radiant_splice_epoch; }

// The splice itself stays native (ES9): the template decides *what* range is
// replaced with *what* text, the engine owns the buffer, the mirrors and the
// caret. Events are deliberately not fired here — this runs inside beforeinput,
// and the engine dispatches `input` after the applier returns, exactly as it
// does for its own splice.
// F9: the caret-operation waist. The template names a WHATWG operation; native
// resolves where it lands and performs it after the dispatch returns. Reported
// through an epoch for the same reason `request_change` is — a primitive has no
// EventContext, and moving the caret dispatches events that need one.
extern "C" void radiant_caret_operation_request(const char* operation, bool extend);

extern "C" int editing_controller_caret_surface_kind(DocState* state);

// Which surface the caret sits in. Resolving it is mechanism; what a key means
// there is policy, and the two surfaces genuinely differ — a single-line input
// has no vertical motion, a rich surface has no value boundary.
// F11: the dropdown's hover cursor — which option is highlighted while the
// popup is open. Storage and paint stay native; which key moves it does not.
// F13: the DOM-range waist. The DOM counts offsets in UTF-16 code units; every
// Lambda-facing offset is a codepoint (ES9), so both primitives convert at this
// boundary and nowhere else.
bool dom_edit_replace_range_u16(DocState* state, DomText* text,
                                uint32_t start_u16, uint32_t end_u16,
                                const char* replacement, uint32_t* out_caret_u16);
bool dom_edit_set_caret_u16(DocState* state, uint32_t caret_u16);
bool dom_edit_insert_at_boundary_u16(DocState* state, const char* text_data,
                                     uint32_t* out_caret_u16);
bool dom_edit_range_in_format(DocState* state, const char* tag);
bool dom_edit_wrap_range_u16(DocState* state, uint32_t start_u16,
                             uint32_t end_u16, const char* tag);
bool dom_edit_unwrap_range_u16(DocState* state, uint32_t start_u16,
                               uint32_t end_u16, const char* tag);
bool dom_edit_insert_html(DocState* state, const char* html);
bool dom_edit_replace_pending_range(DocState* state, const char* replacement);
bool dom_edit_delete_pending_range(DocState* state);
bool dom_edit_insert_paragraph(DocState* state);
bool dom_edit_insert_line_break(DocState* state);

static uint32_t radiant_u16_to_cp(const char* text, uint32_t u16) {
    if (!text) return 0;
    uint32_t bytes = (uint32_t)strlen(text);
    uint32_t byte_off = tc_utf16_to_utf8_offset(text, bytes, u16);
    return (uint32_t)str_utf8_byte_to_char(text, bytes, byte_off);
}

static uint32_t radiant_cp_to_u16(const char* text, uint32_t cp) {
    if (!text) return 0;
    uint32_t bytes = (uint32_t)strlen(text);
    size_t byte_off = str_utf8_char_to_byte(text, bytes, cp);
    if (byte_off == STR_NPOS || byte_off > bytes) byte_off = bytes;
    return tc_utf8_to_utf16_length(text, (uint32_t)byte_off);
}

// The text node an editing dispatch resolved to, and its range. Three scalar
// accessors rather than one map, matching selection_start/selection_end: the
// module has no map-building idiom, and a template reads these once each.
// Null when the edit did not land inside a single text node. Structural
// commands use the raw pending endpoints through their dedicated primitives.
RADIANT_C_API Item fn_radiant_dom_edit_node(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_EDIT_NODE", &elem);
    if (!state || !state->editing.pending_dom_edit_text) return ItemNull;
    return radiant_dom_wrap_node((void*)state->editing.pending_dom_edit_text);
}

static Item radiant_dom_edit_offset(Item node_item, const char* op, bool want_end) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, op, &elem);
    if (!state) return ItemNull;
    DomText* text = state->editing.pending_dom_edit_text;
    if (!text) return ItemNull;
    const char* data = text->text ? text->text : "";
    uint32_t u16 = want_end ? state->editing.pending_dom_edit_end
                            : state->editing.pending_dom_edit_start;
    return radiant_int_item((int64_t)radiant_u16_to_cp(data, u16));
}

RADIANT_C_API Item fn_radiant_dom_edit_start(Item node_item) {
    return radiant_dom_edit_offset(node_item, "DOM_EDIT_START", false);
}

RADIANT_C_API Item fn_radiant_dom_edit_end(Item node_item) {
    return radiant_dom_edit_offset(node_item, "DOM_EDIT_END", true);
}

// The resolved node's current text, so a template can compute a delete range
// without a second round trip through the DOM.
RADIANT_C_API Item fn_radiant_dom_edit_text(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_EDIT_TEXT", &elem);
    if (!state || !state->editing.pending_dom_edit_text) return ItemNull;
    DomText* text = state->editing.pending_dom_edit_text;
    return radiant_string_item(text->text ? text->text : "");
}

// Insert at the edit's boundary, creating a text node when there is
// none. A different operation from a range replacement — `dom_replace_range`
// addresses an existing node — so it is named separately rather than folded in.
// Returns the new caret offset in codepoints, or null.
RADIANT_C_API Item fn_radiant_dom_insert_at_boundary(Item node_item, Item text_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_INSERT_AT_BOUNDARY", &elem);
    if (!state) return ItemNull;
    const char* data = fn_to_cstr(text_item);
    uint32_t caret_u16 = 0;
    if (!dom_edit_insert_at_boundary_u16(state, data ? data : "", &caret_u16)) {
        return ItemNull;
    }
    DomNode* caret = dom_edit_caret_node();
    const char* after = (caret && caret->is_text())
        ? (static_cast<DomText*>(caret)->text ? static_cast<DomText*>(caret)->text : "")
        : "";
    return radiant_int_item((int64_t)radiant_u16_to_cp(after, caret_u16));
}

// Place the caret in the resolved text node without editing it. Two arguments,
// deliberately: this exists because dom_replace_range could not grow a fifth.
RADIANT_C_API Item fn_radiant_dom_set_caret(Item node_item, Item offset_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_SET_CARET", &elem);
    if (!state) return (Item){.item = b2it(0)};
    DomText* text = state->editing.pending_dom_edit_text;
    if (!text) return (Item){.item = b2it(0)};
    const char* data = text->text ? text->text : "";
    int64_t cp = it2l(offset_item);
    if (cp < 0) cp = 0;
    bool ok = dom_edit_set_caret_u16(state, radiant_cp_to_u16(data, (uint32_t)cp));
    return (Item){.item = b2it(ok ? 1 : 0)};
}

// Splice the resolved text node. Returns the new caret offset in codepoints, or
// null when the splice did not happen.
RADIANT_C_API Item fn_radiant_dom_replace_range(Item node_item, Item start_item,
                                                Item end_item, Item text_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_REPLACE_RANGE", &elem);
    if (!state) return ItemNull;
    DomText* text = state->editing.pending_dom_edit_text;
    if (!text) return ItemNull;
    const char* data = text->text ? text->text : "";
    int64_t cp_start = it2l(start_item), cp_end = it2l(end_item);
    if (cp_start < 0) cp_start = 0;
    if (cp_end < cp_start) cp_end = cp_start;
    const char* repl = fn_to_cstr(text_item);
    uint32_t caret_u16 = 0;
    const char* repl_text = repl ? repl : "";
    if (!dom_edit_replace_range_u16(state, text,
                                    radiant_cp_to_u16(data, (uint32_t)cp_start),
                                    radiant_cp_to_u16(data, (uint32_t)cp_end),
                                    repl_text, &caret_u16)) {
        return ItemNull;
    }
    const char* after = text->text ? text->text : "";
    return radiant_int_item((int64_t)radiant_u16_to_cp(after, caret_u16));
}

// F14.1: the formatting primitives. All offsets in codepoints, like every
// other Lambda-facing offset (ES9); the tag and the toggle decision come from
// `commands.ls`, which is the whole point of the seam.
RADIANT_C_API Item fn_radiant_dom_range_format(Item node_item, Item tag_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_RANGE_FORMAT", &elem);
    const char* tag = fn_to_cstr(tag_item);
    if (!state || !tag) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(dom_edit_range_in_format(state, tag) ? 1 : 0)};
}

// The codepoint offsets a wrap/unwrap addresses are converted against the
// resolved node's *current* text, so this helper has to run before the splits.
static bool radiant_dom_format_bounds(DocState* state, Item start_item,
                                      Item end_item, uint32_t* out_start,
                                      uint32_t* out_end) {
    DomText* text = state ? state->editing.pending_dom_edit_text : nullptr;
    if (!text) return false;
    const char* data = text->text ? text->text : "";
    int64_t cp_start = it2l(start_item), cp_end = it2l(end_item);
    if (cp_start < 0) cp_start = 0;
    if (cp_end < cp_start) cp_end = cp_start;
    *out_start = radiant_cp_to_u16(data, (uint32_t)cp_start);
    *out_end = radiant_cp_to_u16(data, (uint32_t)cp_end);
    return true;
}

RADIANT_C_API Item fn_radiant_dom_wrap_range(Item node_item, Item start_item,
                                             Item end_item, Item tag_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_WRAP_RANGE", &elem);
    const char* tag = fn_to_cstr(tag_item);
    uint32_t start = 0, end = 0;
    if (!state || !tag || !radiant_dom_format_bounds(state, start_item, end_item,
                                                     &start, &end)) {
        return (Item){.item = b2it(0)};
    }
    return (Item){.item = b2it(dom_edit_wrap_range_u16(state, start, end, tag) ? 1 : 0)};
}

RADIANT_C_API Item fn_radiant_dom_unwrap_range(Item node_item, Item start_item,
                                               Item end_item, Item tag_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_UNWRAP_RANGE", &elem);
    const char* tag = fn_to_cstr(tag_item);
    uint32_t start = 0, end = 0;
    if (!state || !tag || !radiant_dom_format_bounds(state, start_item, end_item,
                                                     &start, &end)) {
        return (Item){.item = b2it(0)};
    }
    return (Item){.item = b2it(dom_edit_unwrap_range_u16(state, start, end, tag) ? 1 : 0)};
}

// insertHTML: the command formerly special-cased by the native bridge, now
// reached through the same package command path as every other command.
RADIANT_C_API Item fn_radiant_dom_insert_html(Item node_item, Item html_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "DOM_INSERT_HTML", &elem);
    const char* html = fn_to_cstr(html_item);
    if (!state || !html) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(dom_edit_insert_html(state, html) ? 1 : 0)};
}

// F14.2: structural range replacement. The pending endpoints stay in the
// waist, so this Lambda-facing primitive needs only the host and payload.
RADIANT_C_API Item fn_radiant_dom_replace_dom_range(Item node_item,
                                                    Item text_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item,
                                                "DOM_REPLACE_DOM_RANGE", &elem);
    const char* text = fn_to_cstr(text_item);
    if (!state || !text) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(dom_edit_replace_pending_range(state, text) ? 1 : 0)};
}

RADIANT_C_API Item fn_radiant_dom_delete_dom_range(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item,
                                                "DOM_DELETE_DOM_RANGE", &elem);
    if (!state) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(dom_edit_delete_pending_range(state) ? 1 : 0)};
}

RADIANT_C_API Item fn_radiant_dom_insert_paragraph(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item,
                                                "DOM_INSERT_PARAGRAPH", &elem);
    if (!state) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(dom_edit_insert_paragraph(state) ? 1 : 0)};
}

RADIANT_C_API Item fn_radiant_dom_insert_line_break(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item,
                                                "DOM_INSERT_LINE_BREAK", &elem);
    if (!state) return (Item){.item = b2it(0)};
    return (Item){.item = b2it(dom_edit_insert_line_break(state) ? 1 : 0)};
}

extern "C" void radiant_key_intent_request(const char* name);

// F11: the template names an edit intent; native resolves it to a type and
// fills the payload. Epoch-reported, like the caret operation.
RADIANT_C_API Item fn_radiant_key_intent(Item node_item, Item name_item) {
    const char* name = fn_to_cstr(name_item);
    if (!name || !*name) return (Item){.item = b2it(0)};
    radiant_key_intent_request(name);
    return (Item){.item = b2it(1)};
}

RADIANT_C_API Item fn_radiant_hover_index(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "HOVER_INDEX", &elem);
    if (!state || !elem) return ItemNull;
    return radiant_int_item((int64_t)form_control_get_hover_index(state, (View*)elem));
}

RADIANT_C_API Item fn_radiant_set_hover_index(Item node_item, Item index_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "SET_HOVER_INDEX", &elem);
    if (!state || !elem) return (Item){.item = b2it(0)};
    form_control_set_hover_index(state, (View*)elem, (int)it2l(index_item));
    return (Item){.item = b2it(1)};
}

RADIANT_C_API Item fn_radiant_caret_surface(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "CARET_SURFACE", &elem);
    if (!state) return ItemNull;
    int kind = editing_controller_caret_surface_kind(state);
    if (kind == 1) return radiant_string_item("text");
    if (kind == 2) return radiant_string_item("rich");
    if (kind == 3) return radiant_string_item("textarea");
    return ItemNull;
}

RADIANT_C_API Item fn_radiant_caret_operation(Item node_item, Item op_item,
                                              Item extend_item) {
    const char* op = fn_to_cstr(op_item);
    if (!op || !*op) return (Item){.item = b2it(0)};
    radiant_caret_operation_request(op, is_truthy(extend_item));
    return (Item){.item = b2it(1)};
}

// ESO48: key choice stays in the package; native resolves the live scrollport,
// range and geometry after this behavior-only request returns.
RADIANT_C_API Item fn_radiant_scroll_operation(Item node_item, Item op_item) {
    const char* op = fn_to_cstr(op_item);
    if (!op || !*op) return (Item){.item = b2it(0)};
    radiant_scroll_operation_request(op);
    return (Item){.item = b2it(1)};
}

// F10: the context-menu waist. The template names the target and the enable
// mask; the popup position comes from the right click native already resolved,
// so physical pixels never reach policy.
RADIANT_C_API Item fn_radiant_open_context_menu(Item node_item, Item mask_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "OPEN_CONTEXT_MENU", &elem);
    if (!state || !elem) return (Item){.item = b2it(0)};
    int64_t mask = it2l(mask_item);
    if (mask < 0) mask = 0;
    bool ok = context_menu_open_pending(state, (View*)elem, (uint32_t)mask);
    return (Item){.item = b2it(ok ? 1 : 0)};
}

RADIANT_C_API Item fn_radiant_close_context_menu(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "CLOSE_CONTEXT_MENU", &elem);
    if (!state) return (Item){.item = b2it(0)};
    context_menu_close(state);
    return (Item){.item = b2it(1)};
}

// The element the in-flight right click landed on, or the target of an already
// open menu. Resolving the hit is mechanism; deciding what it deserves is not.
RADIANT_C_API Item fn_radiant_context_menu_target(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "CONTEXT_MENU_TARGET", &elem);
    if (!state) return ItemNull;
    View* target = state->pending_context_menu_target
        ? state->pending_context_menu_target : state->context_menu_target;
    if (!target || !target->is_element()) return ItemNull;
    return radiant_dom_wrap_node((void*)target);   // View is the DomNode
}

// Clipboard text, for the paste enable rule. Read-only: the clipboard write
// side stays native with the cut/copy execution.
RADIANT_C_API Item fn_radiant_clipboard_text() {
    const char* clip = clipboard_get_text();
    if (!clip || !*clip) return ItemNull;
    return radiant_string_item(clip);
}

// F2b/#4: the password reveal window. Which control reveals, what gets revealed
// and when are the template's call; this only converts the codepoint range it
// names into the byte window the renderer masks against. An empty or inverted
// range clears, which is how the template says "mask everything".
RADIANT_C_API Item fn_radiant_set_password_reveal(Item node_item, Item start_item,
                                                  Item end_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "SET_PASSWORD_REVEAL", &elem);
    if (!state || !elem || !tc_is_text_control(elem)) return (Item){.item = b2it(0)};
    uint32_t len = 0;
    const char* buf = radiant_tc_buffer(elem, &len);
    if (!buf) return (Item){.item = b2it(0)};

    int64_t cp_start = it2l(start_item), cp_end = it2l(end_item);
    if (cp_start < 0) cp_start = 0;
    if (cp_end <= cp_start) {
        form_control_password_reveal_clear(state, (View*)elem);
        return (Item){.item = b2it(1)};
    }
    size_t b_start = str_utf8_char_to_byte(buf, len, (size_t)cp_start);
    size_t b_end = str_utf8_char_to_byte(buf, len, (size_t)cp_end);
    if (b_start == STR_NPOS || b_start > len) b_start = len;
    if (b_end == STR_NPOS || b_end > len) b_end = len;
    form_control_password_reveal_set(state, (View*)elem,
                                     (uint32_t)b_start, (uint32_t)b_end);
    return (Item){.item = b2it(1)};
}

RADIANT_C_API Item fn_radiant_replace_range(Item node_item, Item start_item,
                                            Item end_item, Item text_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "REPLACE_RANGE", &elem);
    if (!state || !elem) return (Item){.item = b2it(0)};
    if (!tc_is_text_control(elem)) {
        log_error("JUBE_RADIANT_REPLACE_RANGE: <%s> is not a text control",
                  elem->tag_name ? elem->tag_name : "?");
        return (Item){.item = b2it(0)};
    }
    uint32_t len = 0;
    const char* buf = radiant_tc_buffer(elem, &len);
    if (!buf) return (Item){.item = b2it(0)};

    int64_t cp_start = it2l(start_item), cp_end = it2l(end_item);
    if (cp_start < 0) cp_start = 0;
    if (cp_end < cp_start) cp_end = cp_start;
    size_t b_start = str_utf8_char_to_byte(buf, len, (size_t)cp_start);
    size_t b_end = str_utf8_char_to_byte(buf, len, (size_t)cp_end);
    // A codepoint index past the end clamps to the end rather than failing:
    // a template computing `len(value)` as an end offset is asking for exactly
    // that, and STR_NPOS would otherwise splice at a garbage offset.
    if (b_start == STR_NPOS || b_start > len) b_start = len;
    if (b_end == STR_NPOS || b_end > len) b_end = len;

    const char* repl = fn_to_cstr(text_item);
    if (!repl) repl = "";
    uint32_t repl_len = (uint32_t)strlen(repl);
    // A collapsed range with nothing to insert changes no bytes; treating it as
    // a splice would raise the epoch and manufacture an `input` event.
    if (b_start == b_end && repl_len == 0) return (Item){.item = b2it(1)};
    bool ok = te_replace_byte_range_no_events(
        elem, state, (View*)elem, (uint32_t)b_start, (uint32_t)b_end,
        repl, repl_len);
    if (ok) g_radiant_splice_epoch++;
    return (Item){.item = b2it(ok ? 1 : 0)};
}

// ---- change-on-blur (ESO42) ------------------------------------------------
//
// The value as it stood when the control gained focus. The snapshot itself is
// engine mechanism (te_focus_capture_value); what a template does with it —
// deciding whether the value was committed — is the policy.
RADIANT_C_API Item fn_radiant_value_at_focus(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "VALUE_AT_FOCUS");
    FormControlProp* f = elem ? elem->form_control() : nullptr;
    if (!f || !f->value_at_focus) return ItemNull;
    return radiant_string_item(f->value_at_focus);
}

// Counts change requests. The engine samples it across the commit hook so the
// template can answer "the value was committed" without dispatching the event
// itself — native still fires `change`, which is what keeps it ahead of `blur`
// and keeps the state machine's DISPATCH_CHANGE observation intact.
static uint64_t g_radiant_change_request_epoch = 0;

extern "C" uint64_t radiant_change_request_epoch(void) {
    return g_radiant_change_request_epoch;
}

RADIANT_C_API Item fn_radiant_request_change(Item node_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "REQUEST_CHANGE");
    if (!elem) return (Item){.item = b2it(0)};
    g_radiant_change_request_epoch++;
    return (Item){.item = b2it(1)};
}

// Range geometry for the ARIA value mirrors (F7). `value` is the *computed*
// value, not the normalized 0..1 the engine stores, because that is what
// aria-valuenow reports. All three return null on a control that is not a
// range, which is how the template tells the two cases apart.
static Item radiant_range_field(Item node_item, const char* op, int which) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, op, &elem);
    FormControlProp* f = elem ? elem->form_control() : nullptr;
    if (!state || !f || f->control_type != FORM_CONTROL_RANGE) return ItemNull;
    if (which == 1) return radiant_float_item((double)f->range_min);
    if (which == 2) return radiant_float_item((double)f->range_max);
    float normalized = form_control_get_range_value(state, (View*)elem);
    return radiant_float_item(
        (double)(f->range_min + (f->range_max - f->range_min) * normalized));
}

RADIANT_C_API Item fn_radiant_range_value(Item n) { return radiant_range_field(n, "RANGE_VALUE", 0); }
RADIANT_C_API Item fn_radiant_range_min(Item n) { return radiant_range_field(n, "RANGE_MIN", 1); }
RADIANT_C_API Item fn_radiant_range_max(Item n) { return radiant_range_field(n, "RANGE_MAX", 2); }

// ---- IME session (ES18/F7) -------------------------------------------------
//
// The preedit is document-scoped, so these take any node purely to find the
// document — the body, in practice, since that is where the session template
// matches. Writing it is what suppresses nothing else: the placeholder rule and
// the orphan cleanup are the template's policy, expressed through these.
RADIANT_C_API Item fn_radiant_ime_preedit(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "IME_PREEDIT", &elem);
    if (!state || !elem) return ItemNull;
    const char* p = editing_composition_preedit(state, (View*)elem, nullptr, nullptr);
    return p ? radiant_string_item(p) : ItemNull;
}

RADIANT_C_API Item fn_radiant_set_ime_preedit(Item node_item, Item text_item,
                                              Item caret_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "SET_IME_PREEDIT", &elem);
    if (!state || !elem) return (Item){.item = b2it(0)};
    const char* text = fn_to_cstr(text_item);
    int64_t caret = it2l(caret_item);
    if (caret < 0) caret = 0;
    editing_composition_set_preedit(state, (View*)elem, text,
                                    text ? (uint32_t)strlen(text) : 0,
                                    (uint32_t)caret);
    return (Item){.item = b2it(1)};
}

RADIANT_C_API Item fn_radiant_clear_ime_preedit(Item node_item) {
    DomElement* elem = nullptr;
    DocState* state = radiant_state_for_element(node_item, "CLEAR_IME_PREEDIT", &elem);
    if (!state) return (Item){.item = b2it(0)};
    editing_composition_clear_preedit(state);
    return (Item){.item = b2it(1)};
}

RADIANT_C_API Item fn_radiant_free(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "FREE");
    if (!node) return ItemNull;
    DomDocument* doc = radiant_dom_document_from_node(node);
    if (!doc) {
        log_error("JUBE_RADIANT_FREE: DOM node has no owning document");
        return ItemNull;
    }
    free_document(doc);
    return ItemNull;
}

RADIANT_C_API Item fn_radiant_layout(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "LAYOUT");
    if (!node) return radiant_bool_item(false);
    DomDocument* doc = radiant_dom_document_from_node(node);
    if (!doc || !doc->root) {
        log_error("JUBE_RADIANT_LAYOUT: DOM node has no owning root document");
        return radiant_bool_item(false);
    }

    int viewport_width = doc->viewport.width > 0 ? doc->viewport.width : 800;
    int viewport_height = doc->viewport.height > 0 ? doc->viewport.height : 600;
    RadiantLayoutResource* resource = radiant_layout_resource_for_document(doc, "LAYOUT");
    if (!resource) return radiant_bool_item(false);
    // View-tree font handles borrow allocations from UiContext, so retained
    // layouts must reuse the document resource until free_document tears down the tree.
    bool ok = radiant_layout_document(doc, &resource->ui_context,
                                      viewport_width, viewport_height, "LAYOUT");
    return radiant_bool_item(ok);
}

RADIANT_C_API Item fn_radiant_render_svg(Item html_item, Item width_item, Item height_item) {
    const char* html_source = fn_to_cstr(html_item);
    int viewport_width = 0;
    int viewport_height = 0;
    if (!radiant_item_to_int(width_item, &viewport_width) ||
        !radiant_item_to_int(height_item, &viewport_height) ||
        viewport_width <= 0 || viewport_height <= 0) {
        log_error("JUBE_RADIANT_RENDER_SVG: expected positive viewport dimensions, types=%d/%d values=%llu/%llu",
                  (int)get_type_id(width_item), (int)get_type_id(height_item),
                  (unsigned long long)width_item.item,
                  (unsigned long long)height_item.item);
        return ItemNull;
    }

    DomDocument* doc = radiant_load_html_source(html_source, viewport_width,
                                                viewport_height, "RENDER_SVG");
    if (!doc) return ItemNull;

    UiContext uicon = {};
    if (ui_context_init(&uicon, true, 1.0f) != 0) {
        log_error("JUBE_RADIANT_RENDER_SVG: failed to initialize headless UI context");
        free_document(doc);
        return ItemNull;
    }
    if (!radiant_layout_document(doc, &uicon, viewport_width, viewport_height, "RENDER_SVG")) {
        ui_context_cleanup(&uicon);
        return ItemNull;
    }

    char* svg = render_view_tree_to_svg(&uicon, doc->view_tree->root,
                                        viewport_width, viewport_height, doc->state);
    Item result = radiant_string_item(svg);
    if (svg) mem_free(svg);
    // the render API owns the transient document through the UI context.
    ui_context_cleanup(&uicon);
    return result;
}

RADIANT_C_API Item fn_radiant_box(Item node_item) {
    DomNode* node = radiant_dom_node_from_item(node_item, "BOX");
    if (!node || node->view_type == RDT_VIEW_NONE) return ItemNull;
    VelmtBox box;
    box.x = node->x;
    box.y = node->y;
    box.width = node->width;
    box.height = node->height;
    RootFrame roots(1);
    Rooted<Item> rooted_obj(roots, radiant_layout_box_item(&box));
    // The compatibility aliases allocate boxed floats; retain the VMap owner
    // across both writes instead of appending through a stale moved header.
    radiant_rooted_obj_set(rooted_obj, "wd", radiant_float_item(box.width));
    radiant_rooted_obj_set(rooted_obj, "hg", radiant_float_item(box.height));
    return rooted_obj.get();
}

RADIANT_C_API Item fn_radiant_poc_attr(Item path_item) {
    DomDocument* doc = radiant_load_html_document(fn_to_cstr(path_item), "POC");
    if (!doc || !doc->root) return ItemNull;

    doc->root->set_attribute("data-poc", "ok");
    Item result = radiant_string_item(doc->root->get_attribute("data-poc"));
    free_document(doc);
    return result;
}

RADIANT_C_API Item fn_radiant_register_layout(Item name_item, Item fn_item) {
    const char* name = fn_to_cstr(name_item);
    if (!name || name[0] == '\0' || get_type_id(fn_item) != LMD_TYPE_FUNC) {
        log_error("JUBE_RADIANT_REGISTER_LAYOUT: expected name and fn callback");
        return radiant_bool_item(false);
    }
    if (!radiant_host_api || !radiant_host_api->gc) {
        log_error("JUBE_RADIANT_REGISTER_LAYOUT: radiant host API not initialized");
        return radiant_bool_item(false);
    }

    Heap* owner_heap = ::context ? ::context->heap : nullptr;
    if (!owner_heap) {
        log_error("JUBE_RADIANT_REGISTER_LAYOUT: no active Lambda heap for '%s'", name);
        return radiant_bool_item(false);
    }

    RadiantCustomLayoutEntry* entry = radiant_custom_layout_entry(name, owner_heap);
    if (!entry) {
        entry = radiant_custom_layout_free_entry();
        if (!entry) {
            log_error("JUBE_RADIANT_REGISTER_LAYOUT: registry full for '%s'", name);
            return radiant_bool_item(false);
        }
        size_t name_len = strlen(name);
        if (name_len >= RADIANT_CUSTOM_LAYOUT_NAME_CAP) {
            log_error("JUBE_RADIANT_REGISTER_LAYOUT: layout name too long '%s'", name);
            return radiant_bool_item(false);
        }
        memset(entry, 0, sizeof(*entry));
        // registered layout names outlive the Lambda string argument; keep a
        // registry-owned key for callbacks triggered by later layout passes.
        memcpy(entry->name, name, name_len + 1);
        entry->owner_heap = owner_heap;
        entry->fn = ItemNull;
        radiant_host_api->gc->register_root(&entry->fn.item);
        entry->rooted = true;
    }

    entry->fn = fn_item;
    if (!custom_layout_register(entry->name, radiant_lambda_custom_layout_callback)) {
        log_error("JUBE_RADIANT_REGISTER_LAYOUT: native registry failed for '%s'", entry->name);
        return radiant_bool_item(false);
    }
    log_info("JUBE_RADIANT_REGISTER_LAYOUT: registered custom layout '%s' heap=%p",
             entry->name, entry->owner_heap);
    return radiant_bool_item(true);
}

RADIANT_C_API Item fn_radiant_velmt_tag(Item velmt_item) {
    return radiant_obj_get_alias(velmt_item, "tag", "node_name");
}

RADIANT_C_API Item fn_radiant_velmt_index(Item velmt_item) {
    return radiant_obj_get(velmt_item, "index");
}

RADIANT_C_API Item fn_radiant_velmt_id(Item velmt_item) {
    return radiant_obj_get(velmt_item, "id");
}

static Item radiant_velmt_attr_or(Item velmt_item, Item name_item, Item default_item) {
    Item attrs = radiant_obj_get(velmt_item, "attrs");
    const char* name = fn_to_cstr(name_item);
    if (!name || name[0] == '\0') return default_item;
    Item value = radiant_obj_get(attrs, name);
    return radiant_item_is_missing(value) ? default_item : value;
}

RADIANT_C_API Item fn_radiant_velmt_attr(Item velmt_item, Item name_item) {
    return radiant_velmt_attr_or(velmt_item, name_item, ItemNull);
}

RADIANT_C_API Item fn_radiant_velmt_attr_or(Item velmt_item, Item name_item, Item default_item) {
    return radiant_velmt_attr_or(velmt_item, name_item, default_item);
}

RADIANT_C_API Item fn_radiant_velmt_width(Item velmt_item) {
    return radiant_obj_get_alias(velmt_item, "width", "wd");
}

RADIANT_C_API Item fn_radiant_velmt_height(Item velmt_item) {
    return radiant_obj_get_alias(velmt_item, "height", "hg");
}

RADIANT_C_API Item fn_radiant_velmt_box(Item velmt_item) {
    return radiant_obj_get(velmt_item, "box");
}

RADIANT_C_API Item fn_radiant_velmt_children(Item velmt_item) {
    Item children = radiant_obj_get(velmt_item, "children");
    return radiant_item_is_missing(children) ? ItemNull : children;
}

RADIANT_C_API Item fn_radiant_velmt_text(Item velmt_item) {
    Item text = radiant_obj_get(velmt_item, "text");
    return radiant_item_is_missing(text) ? radiant_string_item("") : text;
}

static Item radiant_velmt_style_or(Item velmt_item, Item name_item, Item default_item) {
    Item style = radiant_obj_get(velmt_item, "style");
    const char* name = fn_to_cstr(name_item);
    if (!name || name[0] == '\0') return default_item;
    Item value = radiant_obj_get(style, name);
    return radiant_item_is_missing(value) ? default_item : value;
}

RADIANT_C_API Item fn_radiant_velmt_style(Item velmt_item, Item name_item) {
    return radiant_velmt_style_or(velmt_item, name_item, ItemNull);
}

RADIANT_C_API Item fn_radiant_velmt_style_or(Item velmt_item, Item name_item, Item default_item) {
    return radiant_velmt_style_or(velmt_item, name_item, default_item);
}

RADIANT_C_API Item fn_radiant_velmt_margin(Item velmt_item) {
    return radiant_obj_get(velmt_item, "margin");
}

RADIANT_C_API Item fn_radiant_velmt_border(Item velmt_item) {
    return radiant_obj_get(velmt_item, "border");
}

RADIANT_C_API Item fn_radiant_velmt_padding(Item velmt_item) {
    return radiant_obj_get(velmt_item, "padding");
}

static int radiant_module_init(const JubeHostAPI* host) {
    if (!host || host->api_version != JUBE_HOST_API_VERSION ||
        !host->gc || !host->value || !host->script || !host->dom) {
        log_error("JUBE_RADIANT: missing host API during module init");
        return -1;
    }
    radiant_host_api = host;
    log_info("JUBE_RADIANT: static radiant module initialized");
    return 0;
}

static void radiant_module_shutdown(void) {
    custom_layout_registry_clear();
    for (int i = 0; i < g_radiant_custom_layout_count; i++) {
        RadiantCustomLayoutEntry* entry = &g_radiant_custom_layouts[i];
        if (entry->rooted && radiant_host_api && radiant_host_api->gc) {
            radiant_host_api->gc->unregister_root(&entry->fn.item);
        }
        memset(entry, 0, sizeof(*entry));
    }
    g_radiant_custom_layout_count = 0;
    radiant_host_api = nullptr;
}

static void radiant_custom_layout_heap_cleanup(void* heap_ptr) {
    Heap* heap = (Heap*)heap_ptr;
    if (!heap) return;
    for (int i = 0; i < g_radiant_custom_layout_count; i++) {
        RadiantCustomLayoutEntry* entry = &g_radiant_custom_layouts[i];
        if (entry->owner_heap != heap) continue;
        if (entry->rooted && heap->gc) {
            // Registry slots are process-stable, but callback values are only
            // valid for the runtime heap that JIT-compiled their functions.
            gc_unregister_root(heap->gc, &entry->fn.item);
        }
        memset(entry, 0, sizeof(*entry));
    }
    while (g_radiant_custom_layout_count > 0) {
        RadiantCustomLayoutEntry* tail =
            &g_radiant_custom_layouts[g_radiant_custom_layout_count - 1];
        if (tail->owner_heap || tail->rooted) break;
        g_radiant_custom_layout_count--;
    }
}

static const JubeTypeDef radiant_types[] = {
    {"dom_node", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"range", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"selection", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    // DOM3: style hosts are record-driven; no hand-written host ops remain
    {"inline_style", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"computed_style", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"stylesheet", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"css_rule", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"rule_style_decl", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"document", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"foreign_document", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"velmt", JUBE_TYPE_OWNING_NATIVE, NULL, radiant_velmt_host_destroy},
    {"character_data", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"svg_element", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"input_element", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"select_element", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"textarea_element", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"option_element", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"html_element", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL},
    {"event", JUBE_TYPE_OWNING_NATIVE, NULL, radiant_dom_event_destroy},
};

RADIANT_C_API const void* radiant_dom_node_host_type(void) {
    return &radiant_types[0];
}

RADIANT_C_API const void* radiant_dom_range_host_type(void) {
    return &radiant_types[1];
}

RADIANT_C_API const void* radiant_dom_selection_host_type(void) {
    return &radiant_types[2];
}

RADIANT_C_API const void* radiant_dom_inline_style_host_type(void) {
    return &radiant_types[3];
}

RADIANT_C_API const void* radiant_dom_computed_style_host_type(void) {
    return &radiant_types[4];
}

RADIANT_C_API const void* radiant_dom_stylesheet_host_type(void) {
    return &radiant_types[5];
}

RADIANT_C_API const void* radiant_dom_css_rule_host_type(void) {
    return &radiant_types[6];
}

RADIANT_C_API const void* radiant_dom_rule_style_decl_host_type(void) {
    return &radiant_types[7];
}

RADIANT_C_API const void* radiant_dom_document_host_type(void) {
    return &radiant_types[8];
}

RADIANT_C_API const void* radiant_dom_foreign_document_host_type(void) {
    return &radiant_types[9];
}

RADIANT_C_API const void* radiant_velmt_host_type(void) {
    return &radiant_types[10];
}

RADIANT_C_API const void* radiant_dom_character_data_host_type(void) {
    return &radiant_types[11];
}

RADIANT_C_API const void* radiant_dom_svg_element_host_type(void) {
    return &radiant_types[12];
}

RADIANT_C_API const void* radiant_dom_input_element_host_type(void) {
    return &radiant_types[13];
}

RADIANT_C_API const void* radiant_dom_select_element_host_type(void) {
    return &radiant_types[14];
}

RADIANT_C_API const void* radiant_dom_textarea_element_host_type(void) {
    return &radiant_types[15];
}

RADIANT_C_API const void* radiant_dom_option_element_host_type(void) {
    return &radiant_types[16];
}

RADIANT_C_API const void* radiant_dom_html_element_host_type(void) {
    return &radiant_types[17];
}

RADIANT_C_API const void* radiant_dom_event_host_type(void) {
    return &radiant_types[18];
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
static const JubeFuncDef radiant_functions[] = {
    {"load", "fn(path: string) -> dom_node", (fn_ptr)fn_radiant_load, JUBE_FN_NONE,
     "Item fn_radiant_load(Item path)", (fn_ptr)fn_radiant_load},
    {"root", "fn(doc: dom_node) -> dom_node", (fn_ptr)fn_radiant_root, JUBE_FN_NONE,
     "Item fn_radiant_root(Item doc)", (fn_ptr)fn_radiant_root},
    {"document_root", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_document_root, JUBE_FN_NONE,
     "Item fn_radiant_document_root(Item node)", (fn_ptr)fn_radiant_document_root},
    {"first_element_child", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_first_element_child, JUBE_FN_NONE,
     "Item fn_radiant_first_element_child(Item node)", (fn_ptr)fn_radiant_first_element_child},
    {"next_element_sibling", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_next_element_sibling, JUBE_FN_NONE,
     "Item fn_radiant_next_element_sibling(Item node)", (fn_ptr)fn_radiant_next_element_sibling},
    {"focus_candidates", "fn(root: dom_node) -> array", (fn_ptr)fn_radiant_focus_candidates, JUBE_FN_NONE,
     "Item fn_radiant_focus_candidates(Item root)", (fn_ptr)fn_radiant_focus_candidates},
    {"focused", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_focused, JUBE_FN_NONE,
     "Item fn_radiant_focused(Item node)", (fn_ptr)fn_radiant_focused},
    {"focus_set", "fn(node: dom_node, from_keyboard: bool) -> bool", (fn_ptr)fn_radiant_focus_set, JUBE_FN_NONE,
     "Item fn_radiant_focus_set(Item node, Item from_keyboard)", (fn_ptr)fn_radiant_focus_set},
    {"scroll_into_view", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_scroll_into_view, JUBE_FN_NONE,
     "Item fn_radiant_scroll_into_view(Item node)", (fn_ptr)fn_radiant_scroll_into_view},
    {"embedding_element", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_embedding_element, JUBE_FN_NONE,
     "Item fn_radiant_embedding_element(Item node)", (fn_ptr)fn_radiant_embedding_element},
    {"embedded_document_root", "fn(iframe: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_embedded_document_root, JUBE_FN_NONE,
     "Item fn_radiant_embedded_document_root(Item iframe)", (fn_ptr)fn_radiant_embedded_document_root},
    {"navigation_destination", "fn(source: dom_node, url: string, target_root: dom_node) -> map", (fn_ptr)fn_radiant_navigation_destination, JUBE_FN_NONE,
     "Item fn_radiant_navigation_destination(Item source, Item url, Item target_root)", (fn_ptr)fn_radiant_navigation_destination},
    {"attr", "fn(node: dom_node, name: string) -> string", (fn_ptr)fn_radiant_attr, JUBE_FN_NONE,
     "Item fn_radiant_attr(Item node, Item name)", (fn_ptr)fn_radiant_attr},
    {"set_attr", "fn(node: dom_node, name: string, value: string) -> dom_node", (fn_ptr)fn_radiant_set_attr, JUBE_FN_NONE,
     "Item fn_radiant_set_attr(Item node, Item name, Item value)", (fn_ptr)fn_radiant_set_attr},
    {"get_state", "fn(node: dom_node, name: string) -> any", (fn_ptr)fn_radiant_get_state, JUBE_FN_NONE,
     "Item fn_radiant_get_state(Item node, Item name)", (fn_ptr)fn_radiant_get_state},
    {"set_state", "fn(node: dom_node, name: string, value: any) -> bool", (fn_ptr)fn_radiant_set_state, JUBE_FN_NONE,
     "Item fn_radiant_set_state(Item node, Item name, Item value)", (fn_ptr)fn_radiant_set_state},
    {"dispatch", "fn(node: dom_node, name: string) -> bool", (fn_ptr)fn_radiant_dispatch, JUBE_FN_NONE,
     "Item fn_radiant_dispatch(Item node, Item name)", (fn_ptr)fn_radiant_dispatch},
    {"form_of", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_form_of, JUBE_FN_NONE,
     "Item fn_radiant_form_of(Item node)", (fn_ptr)fn_radiant_form_of},
    {"has_attr", "fn(node: dom_node, name: string) -> bool", (fn_ptr)fn_radiant_has_attr, JUBE_FN_NONE,
     "Item fn_radiant_has_attr(Item node, Item name)", (fn_ptr)fn_radiant_has_attr},
    {"parent", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_parent, JUBE_FN_NONE,
     "Item fn_radiant_parent(Item node)", (fn_ptr)fn_radiant_parent},
    {"closest", "fn(node: dom_node, selector: string) -> dom_node|null", (fn_ptr)fn_radiant_closest, JUBE_FN_NONE,
     "Item fn_radiant_closest(Item node, Item selector)", (fn_ptr)fn_radiant_closest},
    {"form_entries", "fn(form: dom_node, submitter: dom_node|null) -> array", (fn_ptr)fn_radiant_form_entries, JUBE_FN_NONE,
     "Item fn_radiant_form_entries(Item form, Item submitter)", (fn_ptr)fn_radiant_form_entries},
    {"form_url", "fn(form: dom_node) -> string", (fn_ptr)fn_radiant_form_url, JUBE_FN_NONE,
     "Item fn_radiant_form_url(Item form)", (fn_ptr)fn_radiant_form_url},
    {"form_encode", "fn(value: string) -> string", (fn_ptr)fn_radiant_form_encode, JUBE_FN_NONE,
     "Item fn_radiant_form_encode(Item value)", (fn_ptr)fn_radiant_form_encode},
    {"submit_event", "fn(form: dom_node, submitter: dom_node|null) -> bool", (fn_ptr)fn_radiant_submit_event, JUBE_FN_NONE,
     "Item fn_radiant_submit_event(Item form, Item submitter)", (fn_ptr)fn_radiant_submit_event},
    {"check_validity", "fn(form: dom_node) -> bool", (fn_ptr)fn_radiant_check_validity, JUBE_FN_NONE,
     "Item fn_radiant_check_validity(Item form)", (fn_ptr)fn_radiant_check_validity},
    {"reset_form", "fn(form: dom_node) -> bool", (fn_ptr)fn_radiant_reset_form, JUBE_FN_NONE,
     "Item fn_radiant_reset_form(Item form)", (fn_ptr)fn_radiant_reset_form},
    {"form_boundary", "fn() -> string", (fn_ptr)fn_radiant_form_boundary, JUBE_FN_NONE,
     "Item fn_radiant_form_boundary()", (fn_ptr)fn_radiant_form_boundary},
    {"request_navigation", "fn(request: map) -> bool", (fn_ptr)fn_radiant_request_navigation, JUBE_FN_NONE,
     "Item fn_radiant_request_navigation(Item request)", (fn_ptr)fn_radiant_request_navigation},
    {"radio_group", "fn(node: dom_node) -> array", (fn_ptr)fn_radiant_radio_group, JUBE_FN_NONE,
     "Item fn_radiant_radio_group(Item node)", (fn_ptr)fn_radiant_radio_group},
    {"details_group", "fn(node: dom_node) -> array", (fn_ptr)fn_radiant_details_group, JUBE_FN_NONE,
     "Item fn_radiant_details_group(Item node)", (fn_ptr)fn_radiant_details_group},
    {"dropdown_open", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_dropdown_open, JUBE_FN_NONE,
     "Item fn_radiant_dropdown_open(Item node)", (fn_ptr)fn_radiant_dropdown_open},
    {"set_dropdown_open", "fn(node: dom_node, open: bool) -> bool", (fn_ptr)fn_radiant_set_dropdown_open, JUBE_FN_NONE,
     "Item fn_radiant_set_dropdown_open(Item node, Item open)", (fn_ptr)fn_radiant_set_dropdown_open},
    {"activate_popover", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_activate_popover, JUBE_FN_NONE,
     "Item fn_radiant_activate_popover(Item node)", (fn_ptr)fn_radiant_activate_popover},
    {"option_count", "fn(node: dom_node) -> int|null", (fn_ptr)fn_radiant_option_count, JUBE_FN_NONE,
     "Item fn_radiant_option_count(Item node)", (fn_ptr)fn_radiant_option_count},
    {"selected_index", "fn(node: dom_node) -> int|null", (fn_ptr)fn_radiant_selected_index, JUBE_FN_NONE,
     "Item fn_radiant_selected_index(Item node)", (fn_ptr)fn_radiant_selected_index},
    {"set_selected_index", "fn(node: dom_node, index: int) -> bool", (fn_ptr)fn_radiant_set_selected_index, JUBE_FN_NONE,
     "Item fn_radiant_set_selected_index(Item node, Item index)", (fn_ptr)fn_radiant_set_selected_index},
    {"custom_validity", "fn(node: dom_node) -> string", (fn_ptr)fn_radiant_custom_validity, JUBE_FN_NONE,
     "Item fn_radiant_custom_validity(Item node)", (fn_ptr)fn_radiant_custom_validity},
    {"text_control", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_text_control, JUBE_FN_NONE,
     "Item fn_radiant_text_control(Item node)", (fn_ptr)fn_radiant_text_control},
    // F5 editing waist — all offsets in codepoints
    {"selection_start", "fn(node: dom_node) -> int", (fn_ptr)fn_radiant_selection_start, JUBE_FN_NONE,
     "Item fn_radiant_selection_start(Item node)", (fn_ptr)fn_radiant_selection_start},
    {"selection_end", "fn(node: dom_node) -> int", (fn_ptr)fn_radiant_selection_end, JUBE_FN_NONE,
     "Item fn_radiant_selection_end(Item node)", (fn_ptr)fn_radiant_selection_end},
    {"set_selection", "fn(node: dom_node, start: int, end: int) -> bool", (fn_ptr)fn_radiant_set_selection, JUBE_FN_NONE,
     "Item fn_radiant_set_selection(Item node, Item start, Item end)", (fn_ptr)fn_radiant_set_selection},
    {"replace_range", "fn(node: dom_node, start: int, end: int, text: string) -> bool", (fn_ptr)fn_radiant_replace_range, JUBE_FN_NONE,
     "Item fn_radiant_replace_range(Item node, Item start, Item end, Item text)", (fn_ptr)fn_radiant_replace_range},
    {"set_password_reveal", "fn(node: dom_node, start: int, end: int) -> bool", (fn_ptr)fn_radiant_set_password_reveal, JUBE_FN_NONE,
     "Item fn_radiant_set_password_reveal(Item node, Item start, Item end)", (fn_ptr)fn_radiant_set_password_reveal},
    {"dom_set_caret", "fn(node: dom_node, offset: int) -> bool", (fn_ptr)fn_radiant_dom_set_caret, JUBE_FN_NONE,
     "Item fn_radiant_dom_set_caret(Item node, Item offset)", (fn_ptr)fn_radiant_dom_set_caret},
    {"dom_insert_at_boundary", "fn(node: dom_node, text: string) -> int|null", (fn_ptr)fn_radiant_dom_insert_at_boundary, JUBE_FN_NONE,
     "Item fn_radiant_dom_insert_at_boundary(Item node, Item text)", (fn_ptr)fn_radiant_dom_insert_at_boundary},
    {"dom_edit_node", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_dom_edit_node, JUBE_FN_NONE,
     "Item fn_radiant_dom_edit_node(Item node)", (fn_ptr)fn_radiant_dom_edit_node},
    {"dom_edit_start", "fn(node: dom_node) -> int|null", (fn_ptr)fn_radiant_dom_edit_start, JUBE_FN_NONE,
     "Item fn_radiant_dom_edit_start(Item node)", (fn_ptr)fn_radiant_dom_edit_start},
    {"dom_edit_end", "fn(node: dom_node) -> int|null", (fn_ptr)fn_radiant_dom_edit_end, JUBE_FN_NONE,
     "Item fn_radiant_dom_edit_end(Item node)", (fn_ptr)fn_radiant_dom_edit_end},
    {"dom_edit_text", "fn(node: dom_node) -> string|null", (fn_ptr)fn_radiant_dom_edit_text, JUBE_FN_NONE,
     "Item fn_radiant_dom_edit_text(Item node)", (fn_ptr)fn_radiant_dom_edit_text},
    {"dom_replace_range", "fn(node: dom_node, start: int, end: int, text: string) -> int|null", (fn_ptr)fn_radiant_dom_replace_range, JUBE_FN_NONE,
     "Item fn_radiant_dom_replace_range(Item node, Item start, Item end, Item text)", (fn_ptr)fn_radiant_dom_replace_range},
    {"dom_range_format", "fn(node: dom_node, tag: string) -> bool", (fn_ptr)fn_radiant_dom_range_format, JUBE_FN_NONE,
     "Item fn_radiant_dom_range_format(Item node, Item tag)", (fn_ptr)fn_radiant_dom_range_format},
    {"dom_wrap_range", "fn(node: dom_node, start: int, end: int, tag: string) -> bool", (fn_ptr)fn_radiant_dom_wrap_range, JUBE_FN_NONE,
     "Item fn_radiant_dom_wrap_range(Item node, Item start, Item end, Item tag)", (fn_ptr)fn_radiant_dom_wrap_range},
    {"dom_unwrap_range", "fn(node: dom_node, start: int, end: int, tag: string) -> bool", (fn_ptr)fn_radiant_dom_unwrap_range, JUBE_FN_NONE,
     "Item fn_radiant_dom_unwrap_range(Item node, Item start, Item end, Item tag)", (fn_ptr)fn_radiant_dom_unwrap_range},
    {"dom_insert_html", "fn(node: dom_node, html: string) -> bool", (fn_ptr)fn_radiant_dom_insert_html, JUBE_FN_NONE,
     "Item fn_radiant_dom_insert_html(Item node, Item html)", (fn_ptr)fn_radiant_dom_insert_html},
    {"dom_replace_dom_range", "fn(node: dom_node, text: string) -> bool", (fn_ptr)fn_radiant_dom_replace_dom_range, JUBE_FN_NONE,
     "Item fn_radiant_dom_replace_dom_range(Item node, Item text)", (fn_ptr)fn_radiant_dom_replace_dom_range},
    {"dom_delete_dom_range", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_dom_delete_dom_range, JUBE_FN_NONE,
     "Item fn_radiant_dom_delete_dom_range(Item node)", (fn_ptr)fn_radiant_dom_delete_dom_range},
    {"dom_insert_paragraph", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_dom_insert_paragraph, JUBE_FN_NONE,
     "Item fn_radiant_dom_insert_paragraph(Item node)", (fn_ptr)fn_radiant_dom_insert_paragraph},
    {"dom_insert_line_break", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_dom_insert_line_break, JUBE_FN_NONE,
     "Item fn_radiant_dom_insert_line_break(Item node)", (fn_ptr)fn_radiant_dom_insert_line_break},
    {"key_intent", "fn(node: dom_node, name: string) -> bool", (fn_ptr)fn_radiant_key_intent, JUBE_FN_NONE,
     "Item fn_radiant_key_intent(Item node, Item name)", (fn_ptr)fn_radiant_key_intent},
    {"hover_index", "fn(node: dom_node) -> int|null", (fn_ptr)fn_radiant_hover_index, JUBE_FN_NONE,
     "Item fn_radiant_hover_index(Item node)", (fn_ptr)fn_radiant_hover_index},
    {"set_hover_index", "fn(node: dom_node, index: int) -> bool", (fn_ptr)fn_radiant_set_hover_index, JUBE_FN_NONE,
     "Item fn_radiant_set_hover_index(Item node, Item index)", (fn_ptr)fn_radiant_set_hover_index},
    {"caret_surface", "fn(node: dom_node) -> string|null", (fn_ptr)fn_radiant_caret_surface, JUBE_FN_NONE,
     "Item fn_radiant_caret_surface(Item node)", (fn_ptr)fn_radiant_caret_surface},
    {"caret_operation", "fn(node: dom_node, operation: string, extend: bool) -> bool", (fn_ptr)fn_radiant_caret_operation, JUBE_FN_NONE,
     "Item fn_radiant_caret_operation(Item node, Item operation, Item extend)", (fn_ptr)fn_radiant_caret_operation},
    {"scroll_operation", "fn(node: dom_node, operation: string) -> bool", (fn_ptr)fn_radiant_scroll_operation, JUBE_FN_NONE,
     "Item fn_radiant_scroll_operation(Item node, Item operation)", (fn_ptr)fn_radiant_scroll_operation},
    {"open_context_menu", "fn(node: dom_node, enabled_mask: int) -> bool", (fn_ptr)fn_radiant_open_context_menu, JUBE_FN_NONE,
     "Item fn_radiant_open_context_menu(Item node, Item enabled_mask)", (fn_ptr)fn_radiant_open_context_menu},
    {"close_context_menu", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_close_context_menu, JUBE_FN_NONE,
     "Item fn_radiant_close_context_menu(Item node)", (fn_ptr)fn_radiant_close_context_menu},
    {"context_menu_target", "fn(node: dom_node) -> dom_node|null", (fn_ptr)fn_radiant_context_menu_target, JUBE_FN_NONE,
     "Item fn_radiant_context_menu_target(Item node)", (fn_ptr)fn_radiant_context_menu_target},
    {"clipboard_text", "fn() -> string|null", (fn_ptr)fn_radiant_clipboard_text, JUBE_FN_NONE,
     "Item fn_radiant_clipboard_text()", (fn_ptr)fn_radiant_clipboard_text},
    {"ime_preedit", "fn(node: dom_node) -> any", (fn_ptr)fn_radiant_ime_preedit, JUBE_FN_NONE,
     "Item fn_radiant_ime_preedit(Item node)", (fn_ptr)fn_radiant_ime_preedit},
    {"set_ime_preedit", "fn(node: dom_node, text: any, caret: int) -> bool", (fn_ptr)fn_radiant_set_ime_preedit, JUBE_FN_NONE,
     "Item fn_radiant_set_ime_preedit(Item node, Item text, Item caret)", (fn_ptr)fn_radiant_set_ime_preedit},
    {"clear_ime_preedit", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_clear_ime_preedit, JUBE_FN_NONE,
     "Item fn_radiant_clear_ime_preedit(Item node)", (fn_ptr)fn_radiant_clear_ime_preedit},
    {"range_value", "fn(node: dom_node) -> any", (fn_ptr)fn_radiant_range_value, JUBE_FN_NONE,
     "Item fn_radiant_range_value(Item node)", (fn_ptr)fn_radiant_range_value},
    {"range_min", "fn(node: dom_node) -> any", (fn_ptr)fn_radiant_range_min, JUBE_FN_NONE,
     "Item fn_radiant_range_min(Item node)", (fn_ptr)fn_radiant_range_min},
    {"range_max", "fn(node: dom_node) -> any", (fn_ptr)fn_radiant_range_max, JUBE_FN_NONE,
     "Item fn_radiant_range_max(Item node)", (fn_ptr)fn_radiant_range_max},
    {"value_at_focus", "fn(node: dom_node) -> any", (fn_ptr)fn_radiant_value_at_focus, JUBE_FN_NONE,
     "Item fn_radiant_value_at_focus(Item node)", (fn_ptr)fn_radiant_value_at_focus},
    {"request_change", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_request_change, JUBE_FN_NONE,
     "Item fn_radiant_request_change(Item node)", (fn_ptr)fn_radiant_request_change},
    {"free", "fn(node: dom_node) -> null", (fn_ptr)fn_radiant_free, JUBE_FN_NONE,
     "Item fn_radiant_free(Item node)", (fn_ptr)fn_radiant_free},
    {"layout", "fn(node: dom_node) -> bool", (fn_ptr)fn_radiant_layout, JUBE_FN_NONE,
     "Item fn_radiant_layout(Item node)", (fn_ptr)fn_radiant_layout},
    {"render_svg", "fn(html: string, width: int, height: int) -> string|null", (fn_ptr)fn_radiant_render_svg, JUBE_FN_NONE,
     "Item fn_radiant_render_svg(Item html, Item width, Item height)", (fn_ptr)fn_radiant_render_svg},
    {"box", "fn(node: dom_node) -> map|null", (fn_ptr)fn_radiant_box, JUBE_FN_NONE,
     "Item fn_radiant_box(Item node)", (fn_ptr)fn_radiant_box},
    {"poc_attr", "fn(path: string) -> string", (fn_ptr)fn_radiant_poc_attr, JUBE_FN_NONE,
     "Item fn_radiant_poc_attr(Item path)", (fn_ptr)fn_radiant_poc_attr},
    {"register_layout", "fn(name: string, callback: fn) -> bool", (fn_ptr)fn_radiant_register_layout, JUBE_FN_NONE,
     "Item fn_radiant_register_layout(Item name, Item callback)", (fn_ptr)fn_radiant_register_layout},
    {"velmt_index", "fn(velmt: map) -> int|null", (fn_ptr)fn_radiant_velmt_index, JUBE_FN_NONE,
     "Item fn_radiant_velmt_index(Item velmt)", (fn_ptr)fn_radiant_velmt_index},
    {"velmt_tag", "fn(velmt: map) -> string|null", (fn_ptr)fn_radiant_velmt_tag, JUBE_FN_NONE,
     "Item fn_radiant_velmt_tag(Item velmt)", (fn_ptr)fn_radiant_velmt_tag},
    {"velmt_id", "fn(velmt: map) -> string|null", (fn_ptr)fn_radiant_velmt_id, JUBE_FN_NONE,
     "Item fn_radiant_velmt_id(Item velmt)", (fn_ptr)fn_radiant_velmt_id},
    {"velmt_attr", "fn(velmt: map, name: string) -> any", (fn_ptr)fn_radiant_velmt_attr, JUBE_FN_NONE,
     "Item fn_radiant_velmt_attr(Item velmt, Item name)", (fn_ptr)fn_radiant_velmt_attr},
    {"velmt_attr_or", "fn(velmt: map, name: string, default_value: any) -> any", (fn_ptr)fn_radiant_velmt_attr_or, JUBE_FN_NONE,
     "Item fn_radiant_velmt_attr_or(Item velmt, Item name, Item default_value)", (fn_ptr)fn_radiant_velmt_attr_or},
    {"velmt_width", "fn(velmt: map) -> float|null", (fn_ptr)fn_radiant_velmt_width, JUBE_FN_NONE,
     "Item fn_radiant_velmt_width(Item velmt)", (fn_ptr)fn_radiant_velmt_width},
    {"velmt_height", "fn(velmt: map) -> float|null", (fn_ptr)fn_radiant_velmt_height, JUBE_FN_NONE,
     "Item fn_radiant_velmt_height(Item velmt)", (fn_ptr)fn_radiant_velmt_height},
    {"velmt_box", "fn(velmt: map) -> map|null", (fn_ptr)fn_radiant_velmt_box, JUBE_FN_NONE,
     "Item fn_radiant_velmt_box(Item velmt)", (fn_ptr)fn_radiant_velmt_box},
    {"velmt_children", "fn(velmt: map) -> array|null", (fn_ptr)fn_radiant_velmt_children, JUBE_FN_NONE,
     "Item fn_radiant_velmt_children(Item velmt)", (fn_ptr)fn_radiant_velmt_children},
    {"velmt_text", "fn(velmt: map) -> string", (fn_ptr)fn_radiant_velmt_text, JUBE_FN_NONE,
     "Item fn_radiant_velmt_text(Item velmt)", (fn_ptr)fn_radiant_velmt_text},
    {"velmt_style", "fn(velmt: map, name: string) -> any", (fn_ptr)fn_radiant_velmt_style, JUBE_FN_NONE,
     "Item fn_radiant_velmt_style(Item velmt, Item name)", (fn_ptr)fn_radiant_velmt_style},
    {"velmt_style_or", "fn(velmt: map, name: string, default_value: any) -> any", (fn_ptr)fn_radiant_velmt_style_or, JUBE_FN_NONE,
     "Item fn_radiant_velmt_style_or(Item velmt, Item name, Item default_value)", (fn_ptr)fn_radiant_velmt_style_or},
    {"velmt_margin", "fn(velmt: map) -> map|null", (fn_ptr)fn_radiant_velmt_margin, JUBE_FN_NONE,
     "Item fn_radiant_velmt_margin(Item velmt)", (fn_ptr)fn_radiant_velmt_margin},
    {"velmt_border", "fn(velmt: map) -> map|null", (fn_ptr)fn_radiant_velmt_border, JUBE_FN_NONE,
     "Item fn_radiant_velmt_border(Item velmt)", (fn_ptr)fn_radiant_velmt_border},
    {"velmt_padding", "fn(velmt: map) -> map|null", (fn_ptr)fn_radiant_velmt_padding, JUBE_FN_NONE,
     "Item fn_radiant_velmt_padding(Item velmt)", (fn_ptr)fn_radiant_velmt_padding},
};
#pragma clang diagnostic pop

// DOM3 declared interface + binding tables (radiant_dom_iface.cpp)
extern const char radiant_dom_interface_decl[];
extern const JubeTypeBinding radiant_dom_type_bindings[];
extern const int32_t radiant_dom_type_binding_count;

static const JubeModuleDef radiant_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "radiant",
    "0.3.0",
    "Radiant DOM and layout access",
    radiant_types,
    (int32_t)(sizeof(radiant_types) / sizeof(radiant_types[0])),
    radiant_functions,
    (int32_t)(sizeof(radiant_functions) / sizeof(radiant_functions[0])),
    NULL,
    0,
    radiant_module_init,
    radiant_module_shutdown,
    radiant_dom_interface_decl,
    radiant_dom_type_bindings,
    radiant_dom_type_binding_count,
    NULL,
    radiant_custom_layout_heap_cleanup,
};

RADIANT_C_API const JubeModuleDef* radiant_jube_module(void) {
    return &radiant_module;
}

RADIANT_C_API void radiant_jube_register_static(void) {
    jube_register_static_module(&radiant_module);
}
