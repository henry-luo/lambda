#include "../../jube/jube_registry.h"
#include "../../input/css/dom_element.hpp"
#include "../../transpiler.hpp"
#include "radiant_host_api.hpp"
#include "radiant_dom_bridge.hpp"
#include "../../../radiant/layout.hpp"
#include "../../../radiant/render.hpp"
#include "../../../lib/log.h"
#include "../../../lib/mem.h"
#include "../../../lib/mem_context.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/mempool.h"
#include "../../../lib/gc/gc_heap.h"
#include "../../../lib/url.h"
#include <limits.h>
#include <string.h>

String* heap_create_name(const char* name, size_t len);

extern DomDocument* load_lambda_html_doc(Url* html_url, const char* css_filename,
    int viewport_width, int viewport_height, Pool* pool, const char* html_source,
    bool track_source_lines, bool execute_scripts);
extern void free_document(DomDocument* doc);
RADIANT_C_API Item radiant_dom_wrap_node(void* dom_elem);
RADIANT_C_API void* radiant_dom_unwrap_node(Item item);
RADIANT_C_API int radiant_dom_host_get_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_set_property(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_host_call_method(Item object, Item method_name,
                                            Item* args, int argc, Item* out);
RADIANT_C_API int radiant_dom_host_has_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_delete_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_own_property_descriptor(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_host_own_property_names(Item object, Item* out);
RADIANT_C_API Item radiant_dom_host_prototype(Item object);
RADIANT_C_API void radiant_dom_host_invalidate(Item object);
RADIANT_C_API int radiant_dom_document_host_get_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_set_property(Item object, Item key, Item value, Item* out);
RADIANT_C_API int radiant_dom_document_host_call_method(Item object, Item method_name,
                                                     Item* args, int argc, Item* out);
RADIANT_C_API int radiant_dom_document_host_has_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_delete_property(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_descriptor(Item object, Item key, Item* out);
RADIANT_C_API int radiant_dom_document_host_own_property_names(Item object, Item* out);
RADIANT_C_API Item radiant_dom_document_host_prototype(Item object);

const JubeHostAPI* radiant_host_api = nullptr;
extern __thread EvalContext* context;
extern __thread Context* input_context;
extern "C" Context* _lambda_rt;

extern "C" Item vmap_new(void);
extern "C" void vmap_set(Item vmap_item, Item key, Item value);
#ifdef __APPLE__
extern "C" Item radiant_lambda_fn_call3(Function* fn, Item a, Item b, Item c) asm("_fn_call3");
#else
extern "C" Item radiant_lambda_fn_call3(Function* fn, Item a, Item b, Item c) asm("fn_call3");
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
    double* ptr = (double*)heap_calloc(sizeof(double), LMD_TYPE_FLOAT);
    if (!ptr) return ItemNull;
    *ptr = value;
    return (Item){.item = d2it(ptr)};
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

static void radiant_obj_set_optional_float(Item obj, const char* key, float value) {
    radiant_obj_set(obj, key, value >= 0.0f ? radiant_float_item(value) : ItemNull);
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
    Array* arr = array();
    if (arr && capacity > 0) {
        arr->items = (Item*)heap_data_calloc((size_t)capacity * sizeof(Item));
        arr->capacity = capacity;
    }
    return arr ? (Item){.array = arr} : ItemNull;
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
    if (context->parent->custom_layout_paint) {
        CustomLayoutPaintState* paint =
            (CustomLayoutPaintState*)context->parent->custom_layout_paint;
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
    context->parent->custom_layout_paint = &resource->paint;
    return resource;
}

static bool radiant_custom_layout_parse_paint_layers(const CustomLayoutContext* context,
                                                     Item result_item) {
    Item layers_item = radiant_obj_get(result_item, "paint_layers");
    if (radiant_item_is_missing(layers_item)) {
        if (context && context->parent && context->parent->custom_layout_paint) {
            // A later reflow may stop returning generated paint; clear the prior
            // result so stale subscenes cannot survive merely because the field is absent.
            radiant_custom_paint_clear((RadiantCustomPaintResource*)
                context->parent->custom_layout_paint);
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
        type == LMD_TYPE_FLOAT || type == LMD_TYPE_FLOAT64 ||
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
        type == LMD_TYPE_FLOAT || type == LMD_TYPE_FLOAT64 ||
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
        type == LMD_TYPE_FLOAT || type == LMD_TYPE_FLOAT64 ||
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
    Item obj = radiant_obj_new();
    radiant_obj_set(obj, "left", radiant_float_item(edges->left));
    radiant_obj_set(obj, "right", radiant_float_item(edges->right));
    radiant_obj_set(obj, "top", radiant_float_item(edges->top));
    radiant_obj_set(obj, "bottom", radiant_float_item(edges->bottom));
    return obj;
}

static Item radiant_layout_box_item(const VelmtBox* box) {
    if (!radiant_host_api || !radiant_host_api->value || !box) return ItemNull;
    Item obj = radiant_obj_new();
    radiant_obj_set(obj, "x", radiant_float_item(box->x));
    radiant_obj_set(obj, "y", radiant_float_item(box->y));
    radiant_obj_set(obj, "width", radiant_float_item(box->width));
    radiant_obj_set(obj, "height", radiant_float_item(box->height));
    return obj;
}

static Item radiant_layout_attrs_item(DomElement* elem) {
    if (!radiant_host_api || !radiant_host_api->value || !elem) return ItemNull;
    Item attrs = radiant_obj_new();
    int attr_count = 0;
    const char** names = dom_element_get_attribute_names(elem, &attr_count);
    for (int i = 0; names && i < attr_count; i++) {
        const char* name = names[i];
        if (!name) continue;
        const char* value = dom_element_get_attribute(elem, name);
        radiant_obj_set(attrs, name, radiant_string_item(value ? value : ""));
    }
    return attrs;
}

typedef struct RadiantStyleSnapshotContext {
    Item style;
} RadiantStyleSnapshotContext;

static bool radiant_layout_style_snapshot_callback(StyleNode* node, void* context) {
    if (!node || !node->winning_decl || !context) return true;
    CssDeclaration* decl = node->winning_decl;
    const char* name = decl->property_name ? decl->property_name :
        css_property_get_name(decl->property_id);
    if (!name || !decl->value_text) return true;

    RadiantStyleSnapshotContext* snapshot = (RadiantStyleSnapshotContext*)context;
    radiant_obj_set(snapshot->style, name,
        radiant_string_item_n(decl->value_text, decl->value_text_len));
    return true;
}

static Item radiant_layout_style_item(DomElement* elem) {
    if (!radiant_host_api || !radiant_host_api->value) return ItemNull;
    Item style = radiant_obj_new();
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return style;

    RadiantStyleSnapshotContext context;
    context.style = style;
    style_tree_foreach(elem->specified_style, radiant_layout_style_snapshot_callback, &context);
    return style;
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

static int radiant_velmt_host_get_property(Item object, Item key, Item* out) {
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

static int radiant_velmt_host_set_property(Item object, Item key, Item value, Item* out) {
    (void)object; (void)key; (void)value;
    if (out) *out = ItemNull;
    return 0;
}

static int radiant_velmt_host_has_property(Item object, Item key, Item* out) {
    Item value = ItemNull;
    if (!radiant_velmt_host_get_property(object, key, &value)) return 0;
    if (out) *out = radiant_bool_item(!radiant_item_is_missing(value));
    return 1;
}

static int radiant_velmt_host_delete_property(Item object, Item key, Item* out) {
    (void)object; (void)key;
    if (out) *out = radiant_bool_item(false);
    return 1;
}

static int radiant_velmt_host_own_property_names(Item object, Item* out) {
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

static int radiant_velmt_host_own_property_descriptor(Item object, Item key, Item* out) {
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
    Item children = radiant_array_new_item(child_count);
    if (!view || !view->is_element() || depth <= 0) return children;

    DomElement* elem = view->as_element();
    int index = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        View* child_view = (View*)child;
        if (!child_view || child_view->view_type == RDT_VIEW_NONE) continue;
        Velmt child_velmt;
        custom_layout_fill_velmt_from_view(&child_velmt, child_view, index, false);
        radiant_array_push_item(children, radiant_layout_velmt_host_item_depth(&child_velmt, depth - 1));
        index++;
    }
    return children;
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
    Item arr = radiant_array_new_item(context->child_count);
    for (int i = 0; i < context->child_count; i++) {
        radiant_array_push_item(arr, radiant_layout_velmt_host_item(&context->children[i]));
    }
    return arr;
}

static Item radiant_layout_context_item(const CustomLayoutContext* context) {
    if (!radiant_host_api || !radiant_host_api->value || !context) return ItemNull;
    Item obj = radiant_obj_new();
    radiant_obj_set(obj, "layout_name", radiant_string_item(context->layout_name));
    radiant_obj_set(obj, "available_width", radiant_float_item(context->available_width));
    radiant_obj_set(obj, "available_height", radiant_float_item(context->available_height));
    radiant_obj_set_optional_float(obj, "css_width", context->css_width);
    radiant_obj_set_optional_float(obj, "css_height", context->css_height);
    radiant_obj_set(obj, "child_available_width", radiant_float_item(context->child_available_width));
    radiant_obj_set(obj, "child_available_height", radiant_float_item(context->child_available_height));
    radiant_obj_set(obj, "child_available_width_definite",
        radiant_bool_item(context->child_available_width_definite));
    radiant_obj_set(obj, "child_available_height_definite",
        radiant_bool_item(context->child_available_height_definite));
    radiant_obj_set(obj, "child_available_width_source",
        radiant_string_item(context->child_available_width_source));
    radiant_obj_set(obj, "child_available_height_source",
        radiant_string_item(context->child_available_height_source));
    radiant_obj_set(obj, "direction", radiant_string_item(
        context->direction == CSS_VALUE_RTL ? "rtl" : "ltr"));
    radiant_obj_set(obj, "writing_mode", radiant_string_item(
        context->writing_mode ? context->writing_mode : "horizontal-tb"));
    radiant_obj_set(obj, "child_count", radiant_int_item(context->child_count));
    return obj;
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

    EvalContext callback_context = {};
    EvalContext* saved_context = ::context;
    Context* saved_input_context = input_context;
    Context* saved_lambda_rt = _lambda_rt;
    Runtime* runtime = (context->parent && context->parent->doc)
        ? context->parent->doc->lambda_runtime : nullptr;
    if (runtime && runtime->heap) {
        // Script-document layout runs after its stack-local Runner is gone;
        // every Velmt and callback allocation must use the retained runtime.
        callback_context.heap = runtime->heap;
        callback_context.nursery = runtime->nursery;
        callback_context.name_pool = runtime->name_pool;
        callback_context.pool = runtime->reuse_pool
            ? runtime->reuse_pool : runtime->heap->pool;
        callback_context.type_info = type_info;
        if (runtime->ui_mode && runtime->result_arena) {
            callback_context.ui_mode = true;
            callback_context.arena = runtime->result_arena;
            input_context = (Context*)&callback_context;
        } else {
            input_context = nullptr;
        }
        ::context = &callback_context;
        // MIR helpers read _lambda_rt directly; retained callbacks must switch it with context.
        _lambda_rt = (Context*)&callback_context;
    }

    Item args[3];
    args[0] = radiant_layout_parent_item(context);
    args[1] = radiant_layout_children_item(context);
    args[2] = radiant_layout_context_item(context);
    // Lambda-registered callbacks are core Function values; the Jube script
    // call hook is JS-specific and corrupts Lambda fn call frames.
    Item result_item = radiant_lambda_fn_call3(entry->fn.function, args[0], args[1], args[2]);
    if (get_type_id(result_item) == LMD_TYPE_ERROR) {
        g_radiant_velmt_active_pass_id = previous_pass_id;
        ::context = saved_context;
        input_context = saved_input_context;
        _lambda_rt = saved_lambda_rt;
        log_error("CUSTOM_LAYOUT_LAMBDA_EXCEPTION: layout='%s'", context->layout_name);
        return false;
    }
    bool ok = radiant_custom_layout_parse_result(context, result_item, result);
    g_radiant_velmt_active_pass_id = previous_pass_id;
    ::context = saved_context;
    input_context = saved_input_context;
    _lambda_rt = saved_lambda_rt;
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
    if (ui_context_init(&resource->ui_context, true) != 0) {
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

RADIANT_C_API Item fn_radiant_attr(Item node_item, Item name_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "ATTR");
    const char* name = fn_to_cstr(name_item);
    if (!elem || !name || !name[0]) return ItemNull;
    return radiant_string_item(dom_element_get_attribute(elem, name));
}

RADIANT_C_API Item fn_radiant_set_attr(Item node_item, Item name_item, Item value_item) {
    DomElement* elem = radiant_dom_element_from_item(node_item, "SET_ATTR");
    const char* name = fn_to_cstr(name_item);
    const char* value = fn_to_cstr(value_item);
    if (!elem || !name || !name[0] || !value) return ItemNull;
    // Attribute writes from Lambda must share JS DOM side effects such as
    // event-attribute compilation, selection refresh, and mutation notices.
    Item args[2] = {name_item, value_item};
    Item method = (Item){.item = s2it(heap_create_name("setAttribute"))};
    radiant_dom_element_method(node_item, method, args, 2);
    return node_item;
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

    int viewport_width = doc->viewport_width > 0 ? doc->viewport_width : 800;
    int viewport_height = doc->viewport_height > 0 ? doc->viewport_height : 600;
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
    if (ui_context_init(&uicon, true) != 0) {
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
    Item obj = radiant_layout_box_item(&box);
    radiant_obj_set(obj, "wd", radiant_float_item(box.width));
    radiant_obj_set(obj, "hg", radiant_float_item(box.height));
    return obj;
}

RADIANT_C_API Item fn_radiant_poc_attr(Item path_item) {
    DomDocument* doc = radiant_load_html_document(fn_to_cstr(path_item), "POC");
    if (!doc || !doc->root) return ItemNull;

    dom_element_set_attribute(doc->root, "data-poc", "ok");
    Item result = radiant_string_item(dom_element_get_attribute(doc->root, "data-poc"));
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
    if (!host || host->api_version != JUBE_ABI_VERSION ||
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

extern const JubeHostObjectOps radiant_dom_node_host_ops;
const JubeHostObjectOps radiant_dom_node_host_ops = {
    radiant_dom_host_get_property,
    radiant_dom_host_set_property,
    radiant_dom_host_call_method,
    radiant_dom_host_has_property,
    radiant_dom_host_delete_property,
    radiant_dom_host_own_property_descriptor,
    radiant_dom_host_own_property_names,
    radiant_dom_host_prototype,
    radiant_dom_host_invalidate,
    NULL,
};

static const JubeHostObjectOps radiant_velmt_host_ops = {
    radiant_velmt_host_get_property,
    radiant_velmt_host_set_property,
    NULL,
    radiant_velmt_host_has_property,
    radiant_velmt_host_delete_property,
    radiant_velmt_host_own_property_descriptor,
    radiant_velmt_host_own_property_names,
    NULL,
    NULL,
    radiant_velmt_host_destroy,
};


static const JubeTypeDef radiant_types[] = {
    {"dom_node", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"range", JUBE_TYPE_NON_OWNING_HOST, NULL, &radiant_dom_node_host_ops, NULL},
    {"selection", JUBE_TYPE_NON_OWNING_HOST, NULL, &radiant_dom_node_host_ops, NULL},
    // DOM3: style hosts are record-driven; no hand-written host ops remain
    {"inline_style", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"computed_style", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"stylesheet", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"css_rule", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"rule_style_decl", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"document", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"foreign_document", JUBE_TYPE_NON_OWNING_HOST, NULL, NULL, NULL},
    {"velmt", JUBE_TYPE_OWNING_NATIVE, NULL, &radiant_velmt_host_ops, NULL},
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
static const JubeFuncDef radiant_functions[] = {
    {"load", "fn(path: string) -> dom_node", (fn_ptr)fn_radiant_load, JUBE_FN_NONE,
     "Item fn_radiant_load(Item path)", (fn_ptr)fn_radiant_load},
    {"root", "fn(doc: dom_node) -> dom_node", (fn_ptr)fn_radiant_root, JUBE_FN_NONE,
     "Item fn_radiant_root(Item doc)", (fn_ptr)fn_radiant_root},
    {"attr", "fn(node: dom_node, name: string) -> string", (fn_ptr)fn_radiant_attr, JUBE_FN_NONE,
     "Item fn_radiant_attr(Item node, Item name)", (fn_ptr)fn_radiant_attr},
    {"set_attr", "fn(node: dom_node, name: string, value: string) -> dom_node", (fn_ptr)fn_radiant_set_attr, JUBE_FN_NONE,
     "Item fn_radiant_set_attr(Item node, Item name, Item value)", (fn_ptr)fn_radiant_set_attr},
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
    "0.2.0",
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
    10,  // DOM3 Phase 4e: document/foreign_document are binding-hook driven
    NULL,
    radiant_custom_layout_heap_cleanup,
};

RADIANT_C_API const JubeModuleDef* radiant_jube_module(void) {
    return &radiant_module;
}

RADIANT_C_API void radiant_jube_register_static(void) {
    jube_register_static_module(&radiant_module);
}
