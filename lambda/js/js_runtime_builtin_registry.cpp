/**
 * JavaScript runtime built-in registry tables for Lambda.
 */
#include "js_runtime_internal.hpp"

// =============================================================================
// Built-in registry
// =============================================================================

// Built-in method function cache — keyed by builtin_id
static Item js_builtin_cache[JS_BUILTIN_MAX];
static bool js_builtin_cache_init = false;

extern "C" void js_func_init_property(Item fn_item, Item key, Item value);

typedef struct JsBuiltinDescriptor {
    JsBuiltinDispatchGroup dispatch_group;
    JsBuiltinMirLoweringKind mir_kind;
} JsBuiltinDescriptor;

static const JsBuiltinDescriptor JS_BUILTIN_DESCRIPTORS[] = {
    {JS_BUILTIN_DISPATCH_NONE, JS_BUILTIN_MIR_GENERIC},
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, dispatch_group, mir_kind) {dispatch_group, mir_kind},
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, use_cache)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
};

static const JsBuiltinMethodSpec JS_BUILTIN_METHOD_SPECS[] = {
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, dispatch_group, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, use_cache) \
    {owner, name, len, id, arity, display_name, property_kind, flags, use_cache},
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
    {JS_BUILTIN_OWNER_NONE, NULL, 0, 0, 0, NULL, JS_BUILTIN_PROPERTY_METHOD, 0, false}
};

static const JsBuiltinGlobalSpec JS_BUILTIN_GLOBAL_SPECS[] = {
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, dispatch_group, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, use_cache)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, arity, flags) \
    {id, name, len, kind, runtime_id, arity, flags},
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
};

static_assert(sizeof(JS_BUILTIN_GLOBAL_SPECS) / sizeof(JS_BUILTIN_GLOBAL_SPECS[0]) ==
                  JS_BUILTIN_GLOBAL_MAX - 1,
              "global builtin catalog IDs must remain dense");

static uint32_t js_builtin_name_hash(uint32_t seed, const char* name, int len) {
    uint32_t h = seed;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)name[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

#define JS_BUILTIN_GLOBAL_LOOKUP_INDEX_SIZE 256u
static const JsBuiltinGlobalSpec* js_builtin_global_lookup_index[
    JS_BUILTIN_GLOBAL_LOOKUP_INDEX_SIZE];
static bool js_builtin_global_lookup_index_initialized = false;

static void js_builtin_global_initialize_index() {
    if (js_builtin_global_lookup_index_initialized) return;
    int count = (int)(sizeof(JS_BUILTIN_GLOBAL_SPECS) / sizeof(JS_BUILTIN_GLOBAL_SPECS[0]));
    for (int i = 0; i < count; i++) {
        const JsBuiltinGlobalSpec* spec = &JS_BUILTIN_GLOBAL_SPECS[i];
        uint32_t slot = js_builtin_name_hash(2166136261u, spec->name, spec->len) &
                        (JS_BUILTIN_GLOBAL_LOOKUP_INDEX_SIZE - 1u);
        while (js_builtin_global_lookup_index[slot]) {
            slot = (slot + 1u) & (JS_BUILTIN_GLOBAL_LOOKUP_INDEX_SIZE - 1u);
        }
        js_builtin_global_lookup_index[slot] = spec;
    }
    js_builtin_global_lookup_index_initialized = true;
}

const JsBuiltinGlobalSpec* js_builtin_global_find(const char* name, int len) {
    if (!name || len <= 0) return NULL;
    js_builtin_global_initialize_index();
    uint32_t slot = js_builtin_name_hash(2166136261u, name, len) &
                    (JS_BUILTIN_GLOBAL_LOOKUP_INDEX_SIZE - 1u);
    for (uint32_t probe = 0; probe < JS_BUILTIN_GLOBAL_LOOKUP_INDEX_SIZE; probe++) {
        const JsBuiltinGlobalSpec* spec = js_builtin_global_lookup_index[slot];
        if (!spec) return NULL;
        if (spec->len == len &&
            strncmp(spec->name, name, len) == 0) {
            return spec;
        }
        slot = (slot + 1u) & (JS_BUILTIN_GLOBAL_LOOKUP_INDEX_SIZE - 1u);
    }
    return NULL;
}

bool js_builtin_global_has_flag(const char* name, int len, int flag) {
    const JsBuiltinGlobalSpec* spec = js_builtin_global_find(name, len);
    return spec && (spec->flags & flag) != 0;
}

int js_builtin_typed_array_type(const char* name, int len) {
    const JsBuiltinGlobalSpec* spec = js_builtin_global_find(name, len);
    if (!spec || !(spec->flags & JS_BUILTIN_GLOBAL_TYPED_ARRAY)) return -1;
    switch (spec->runtime_id) {
    case JS_CTOR_INT8ARRAY: return JS_TYPED_INT8;
    case JS_CTOR_UINT8ARRAY: return JS_TYPED_UINT8;
    case JS_CTOR_UINT8CLAMPEDARRAY: return JS_TYPED_UINT8_CLAMPED;
    case JS_CTOR_INT16ARRAY: return JS_TYPED_INT16;
    case JS_CTOR_UINT16ARRAY: return JS_TYPED_UINT16;
    case JS_CTOR_INT32ARRAY: return JS_TYPED_INT32;
    case JS_CTOR_UINT32ARRAY: return JS_TYPED_UINT32;
    case JS_CTOR_FLOAT16ARRAY: return JS_TYPED_FLOAT16;
    case JS_CTOR_FLOAT32ARRAY: return JS_TYPED_FLOAT32;
    case JS_CTOR_FLOAT64ARRAY: return JS_TYPED_FLOAT64;
    case JS_CTOR_BIGINT64ARRAY: return JS_TYPED_BIGINT64;
    case JS_CTOR_BIGUINT64ARRAY: return JS_TYPED_BIGUINT64;
    default: return -1;
    }
}

int js_builtin_global_count() {
    return (int)(sizeof(JS_BUILTIN_GLOBAL_SPECS) / sizeof(JS_BUILTIN_GLOBAL_SPECS[0]));
}

const JsBuiltinGlobalSpec* js_builtin_global_at(int index) {
    if (index < 0 || index >= js_builtin_global_count()) return NULL;
    return &JS_BUILTIN_GLOBAL_SPECS[index];
}

static_assert(sizeof(JS_BUILTIN_DESCRIPTORS) / sizeof(JS_BUILTIN_DESCRIPTORS[0]) == JS_BUILTIN_MAX,
              "builtin catalog IDs must remain dense");
static_assert(JS_BUILTIN_ARR_TO_LOCALE_STRING - JS_BUILTIN_ARR_PUSH == 38,
              "Array builtin range arithmetic requires contiguous catalog IDs");
static_assert(JS_BUILTIN_STR_TO_LOCALE_UPPER_CASE - JS_BUILTIN_STR_CHAR_AT == 47,
              "String builtin range arithmetic requires contiguous catalog IDs");
static_assert(JS_BUILTIN_NUM_TO_EXPONENTIAL - JS_BUILTIN_NUM_TO_STRING == 4,
              "Number builtin range arithmetic requires contiguous catalog IDs");
static_assert(JS_BUILTIN_MATH_LOG1P - JS_BUILTIN_MATH_ABS == 34,
              "Math builtin range arithmetic requires contiguous catalog IDs");

#define JS_BUILTIN_LOOKUP_INDEX_SIZE 1024u
static const JsBuiltinMethodSpec* js_builtin_lookup_index[JS_BUILTIN_LOOKUP_INDEX_SIZE];
static const JsBuiltinMethodSpec* js_builtin_id_index[JS_BUILTIN_MAX];
static bool js_builtin_lookup_index_initialized = false;

static const char* js_builtin_method_spec_display_name(const JsBuiltinMethodSpec* spec) {
    return spec->display_name ? spec->display_name : spec->name;
}

static uint32_t js_builtin_spec_name_hash(JsBuiltinOwner owner, const char* name, int len) {
    return js_builtin_name_hash(2166136261u ^ (uint32_t)owner, name, len);
}

static void js_builtin_catalog_initialize_index() {
    if (js_builtin_lookup_index_initialized) return;
    for (int i = 0; JS_BUILTIN_METHOD_SPECS[i].name; i++) {
        const JsBuiltinMethodSpec* spec = &JS_BUILTIN_METHOD_SPECS[i];
        uint32_t slot = js_builtin_spec_name_hash(spec->owner, spec->name, spec->len) &
                        (JS_BUILTIN_LOOKUP_INDEX_SIZE - 1u);
        while (js_builtin_lookup_index[slot]) {
            slot = (slot + 1u) & (JS_BUILTIN_LOOKUP_INDEX_SIZE - 1u);
        }
        js_builtin_lookup_index[slot] = spec;
        if (spec->builtin_id > 0 && spec->builtin_id < JS_BUILTIN_MAX &&
            !js_builtin_id_index[spec->builtin_id]) {
            js_builtin_id_index[spec->builtin_id] = spec;
        }
    }
    js_builtin_lookup_index_initialized = true;
}

const JsBuiltinMethodSpec* js_builtin_catalog_find(JsBuiltinOwner owner,
                                                    const char* name, int len) {
    if (owner <= JS_BUILTIN_OWNER_NONE || owner >= JS_BUILTIN_OWNER_MAX || !name) return NULL;
    js_builtin_catalog_initialize_index();
    uint32_t slot = js_builtin_spec_name_hash(owner, name, len) &
                    (JS_BUILTIN_LOOKUP_INDEX_SIZE - 1u);
    for (uint32_t probe = 0; probe < JS_BUILTIN_LOOKUP_INDEX_SIZE; probe++) {
        const JsBuiltinMethodSpec* spec = js_builtin_lookup_index[slot];
        if (!spec) return NULL;
        if (spec->owner == owner && spec->len == len &&
            strncmp(spec->name, name, len) == 0) {
            return spec;
        }
        slot = (slot + 1u) & (JS_BUILTIN_LOOKUP_INDEX_SIZE - 1u);
    }
    return NULL;
}

const JsBuiltinMethodSpec* js_builtin_catalog_find_id(int builtin_id) {
    if (builtin_id <= JS_BUILTIN_NONE || builtin_id >= JS_BUILTIN_MAX) return NULL;
    js_builtin_catalog_initialize_index();
    return js_builtin_id_index[builtin_id];
}

int js_builtin_catalog_lookup_id(JsBuiltinOwner owner, const char* name, int len) {
    const JsBuiltinMethodSpec* spec = js_builtin_catalog_find(owner, name, len);
    return spec ? spec->builtin_id : JS_BUILTIN_NONE;
}

JsBuiltinDispatchGroup js_builtin_dispatch_group(int builtin_id) {
    if (builtin_id <= JS_BUILTIN_NONE || builtin_id >= JS_BUILTIN_MAX) {
        return JS_BUILTIN_DISPATCH_NONE;
    }
    return JS_BUILTIN_DESCRIPTORS[builtin_id].dispatch_group;
}

JsBuiltinMirLoweringKind js_builtin_mir_kind(int builtin_id) {
    if (builtin_id <= JS_BUILTIN_NONE || builtin_id >= JS_BUILTIN_MAX) {
        return JS_BUILTIN_MIR_GENERIC;
    }
    return JS_BUILTIN_DESCRIPTORS[builtin_id].mir_kind;
}

Item js_lookup_builtin_method_spec(JsBuiltinOwner owner, const char* name, int len) {
    const JsBuiltinMethodSpec* spec = js_builtin_catalog_find(owner, name, len);
    if (!spec || spec->builtin_id <= JS_BUILTIN_NONE) return ItemNull;
    return js_get_or_create_builtin(spec->builtin_id,
                                    js_builtin_method_spec_display_name(spec),
                                    spec->param_count);
}

static Item js_create_builtin_function_from_spec(const JsBuiltinMethodSpec* spec) {
    const char* display_name = js_builtin_method_spec_display_name(spec);
    if (spec->use_cache && spec->flags == 0 && spec->builtin_id > 0) {
        return js_get_or_create_builtin(spec->builtin_id, display_name, spec->param_count);
    }
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->param_count = spec->param_count;
    fn->formal_length = -1;
    fn->builtin_id = spec->builtin_id;
    fn->name = heap_create_name(display_name, strlen(display_name));
    fn->flags = spec->flags;
    return (Item){.function = (Function*)fn};
}

void js_install_builtin_method_specs(Item object, JsBuiltinOwner owner) {
    for (int i = 0; JS_BUILTIN_METHOD_SPECS[i].name; i++) {
        const JsBuiltinMethodSpec* spec = &JS_BUILTIN_METHOD_SPECS[i];
        if (spec->owner != owner || spec->property_kind != JS_BUILTIN_PROPERTY_METHOD) continue;
        Item key = (Item){.item = s2it(heap_create_name(spec->name, spec->len))};
        Item fn = js_create_builtin_function_from_spec(spec);
        js_property_set(object, key, fn);
        js_mark_non_enumerable(object, key);
    }
}

static void js_install_builtin_method_specs_on_function(Item function_item,
                                                        JsBuiltinOwner owner,
                                                        bool skip_existing) {
    if (get_type_id(function_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)function_item.function;
    for (int i = 0; JS_BUILTIN_METHOD_SPECS[i].name; i++) {
        const JsBuiltinMethodSpec* spec = &JS_BUILTIN_METHOD_SPECS[i];
        if (spec->owner != owner || spec->property_kind != JS_BUILTIN_PROPERTY_METHOD) continue;
        Item key = (Item){.item = s2it(heap_create_name(spec->name, spec->len))};
        if (skip_existing && fn->properties_map.item != 0 &&
            get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            Item existing = map_get(fn->properties_map.map, key);
            if (existing.item != ItemNull.item) continue;
        }
        Item method = js_create_builtin_function_from_spec(spec);
        js_func_init_property(function_item, key, method);
        js_mark_non_enumerable(function_item, key);
    }
}

void js_install_builtin_function_specs(Item object, JsBuiltinOwner owner) {
    js_install_builtin_method_specs(object, owner);
}

void js_install_builtin_accessor_specs(Item object, JsBuiltinOwner owner) {
    for (int i = 0; JS_BUILTIN_METHOD_SPECS[i].name; i++) {
        const JsBuiltinMethodSpec* spec = &JS_BUILTIN_METHOD_SPECS[i];
        if (spec->owner != owner || spec->property_kind != JS_BUILTIN_PROPERTY_ACCESSOR) continue;
        Item getter = js_create_builtin_function_from_spec(spec);
        Item prop_name = (Item){.item = s2it(heap_create_name(spec->name, spec->len))};
        js_install_native_accessor(object, prop_name, getter, ItemNull, JSPD_NON_ENUMERABLE);
    }
}

void js_populate_dataview_prototype_methods(Item prototype) {
    js_install_builtin_function_specs(prototype, JS_BUILTIN_OWNER_DATAVIEW_PROTOTYPE_METHOD);
    js_install_builtin_accessor_specs(prototype, JS_BUILTIN_OWNER_DATAVIEW_ACCESSOR);
}

typedef struct JsBuiltinTypeOwnerMap {
    TypeId type;
    JsBuiltinOwner owner;
} JsBuiltinTypeOwnerMap;

typedef struct JsBuiltinOwnerBinding {
    const char* name;
    int len;
    JsBuiltinOwner member_owner;
    JsBuiltinOwner prototype_owner;
    JsBuiltinOwner accessor_owner;
    int flags;
} JsBuiltinOwnerBinding;

#define JS_BUILTIN_OWNER_BINDING_SPECIES 1
static const JsBuiltinOwnerBinding JS_BUILTIN_OWNER_BINDINGS[] = {
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, dispatch_group, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, use_cache)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, arity, flags)
#define JS_BUILTIN_OWNER_BINDING(name, len, member, prototype, accessor, flags) \
    {name, len, member, prototype, accessor, flags},
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_OWNER_BINDING
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
    {NULL, 0, JS_BUILTIN_OWNER_NONE, JS_BUILTIN_OWNER_NONE, JS_BUILTIN_OWNER_NONE, 0}
};

static const JsBuiltinOwnerBinding* js_find_owner_binding(const char* name, int len) {
    if (!name) return NULL;
    for (int i = 0; JS_BUILTIN_OWNER_BINDINGS[i].name; i++) {
        const JsBuiltinOwnerBinding* binding = &JS_BUILTIN_OWNER_BINDINGS[i];
        if (binding->len == len && strncmp(binding->name, name, len) == 0) return binding;
    }
    return NULL;
}

static JsBuiltinOwner js_find_type_owner(const JsBuiltinTypeOwnerMap* map, TypeId type) {
    if (!map) return JS_BUILTIN_OWNER_NONE;
    for (int i = 0; map[i].owner != JS_BUILTIN_OWNER_NONE; i++) {
        if (map[i].type == type) return map[i].owner;
    }
    return JS_BUILTIN_OWNER_NONE;
}

static const JsBuiltinTypeOwnerMap JS_PROTOTYPE_TYPE_OWNER_MAP[] = {
    {LMD_TYPE_ARRAY, JS_BUILTIN_OWNER_ARRAY_PROTOTYPE_METHOD},
    {LMD_TYPE_FUNC, JS_BUILTIN_OWNER_FUNCTION_PROTOTYPE_METHOD},
    {LMD_TYPE_INT, JS_BUILTIN_OWNER_NUMBER_PROTOTYPE_METHOD},
    {LMD_TYPE_FLOAT, JS_BUILTIN_OWNER_NUMBER_PROTOTYPE_METHOD},
    {LMD_TYPE_DECIMAL, JS_BUILTIN_OWNER_BIGINT_PROTOTYPE_METHOD},
    {LMD_TYPE_STRING, JS_BUILTIN_OWNER_STRING_PROTOTYPE_METHOD},
    {LMD_TYPE_BOOL, JS_BUILTIN_OWNER_BOOLEAN_PROTOTYPE_METHOD},
    {LMD_TYPE_NULL, JS_BUILTIN_OWNER_NONE}
};

static JsBuiltinOwner js_get_constructor_static_owner(const char* ctor_name, int ctor_len) {
    const JsBuiltinOwnerBinding* binding = js_find_owner_binding(ctor_name, ctor_len);
    return binding ? binding->member_owner : JS_BUILTIN_OWNER_NONE;
}

static JsBuiltinOwner js_get_prototype_owner_for_type(TypeId type) {
    return js_find_type_owner(JS_PROTOTYPE_TYPE_OWNER_MAP, type);
}

int js_builtin_catalog_lookup_constructor_id(const char* ctor_name, int ctor_len,
                                             const char* prop_name, int prop_len) {
    JsBuiltinOwner owner = js_get_constructor_static_owner(ctor_name, ctor_len);
    return js_builtin_catalog_lookup_id(owner, prop_name, prop_len);
}

int js_builtin_catalog_lookup_member_id(const char* owner_name, int owner_len,
                                        const char* prop_name, int prop_len) {
    JsBuiltinOwner owner = js_get_constructor_static_owner(owner_name, owner_len);
    return js_builtin_catalog_lookup_id(owner, prop_name, prop_len);
}

static bool js_builtin_type_has_own_to_string(TypeId type) {
    return type == LMD_TYPE_FUNC || type == LMD_TYPE_BOOL ||
           type == LMD_TYPE_ARRAY || type == LMD_TYPE_STRING ||
           type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
           type == LMD_TYPE_FLOAT || type == LMD_TYPE_DECIMAL;
}

static bool js_builtin_type_has_own_value_of(TypeId type) {
    return type == LMD_TYPE_FUNC || type == LMD_TYPE_INT ||
           type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT ||
           type == LMD_TYPE_DECIMAL;
}

static bool js_builtin_type_uses_number_prototype(TypeId type) {
    return type == LMD_TYPE_INT || type == LMD_TYPE_INT64 ||
           type == LMD_TYPE_FLOAT;
}

static JsBuiltinOwner js_get_prototype_owner_for_class_or_type(int js_class,
                                                                  TypeId fallback_type) {
    switch ((JsClass)js_class) {
    case JS_CLASS_OBJECT: return JS_BUILTIN_OWNER_OBJECT_PROTOTYPE_METHOD;
    case JS_CLASS_FUNCTION: return JS_BUILTIN_OWNER_FUNCTION_PROTOTYPE_METHOD;
    case JS_CLASS_BOOLEAN: return JS_BUILTIN_OWNER_BOOLEAN_PROTOTYPE_METHOD;
    case JS_CLASS_NUMBER: return JS_BUILTIN_OWNER_NUMBER_PROTOTYPE_METHOD;
    case JS_CLASS_BIGINT: return JS_BUILTIN_OWNER_BIGINT_PROTOTYPE_METHOD;
    case JS_CLASS_SYMBOL: return JS_BUILTIN_OWNER_SYMBOL_PROTOTYPE_METHOD;
    case JS_CLASS_STRING: return JS_BUILTIN_OWNER_STRING_PROTOTYPE_METHOD;
    case JS_CLASS_ARRAY: return JS_BUILTIN_OWNER_ARRAY_PROTOTYPE_METHOD;
    case JS_CLASS_DATE: return JS_BUILTIN_OWNER_DATE_PROTOTYPE_METHOD;
    case JS_CLASS_REGEXP: return JS_BUILTIN_OWNER_REGEXP_PROTOTYPE_METHOD;
    case JS_CLASS_PROMISE: return JS_BUILTIN_OWNER_PROMISE_PROTOTYPE_METHOD;
    case JS_CLASS_MAP: return JS_BUILTIN_OWNER_MAP_PROTOTYPE_METHOD;
    case JS_CLASS_SET: return JS_BUILTIN_OWNER_SET_PROTOTYPE_METHOD;
    case JS_CLASS_WEAK_MAP: return JS_BUILTIN_OWNER_WEAKMAP_PROTOTYPE_METHOD;
    case JS_CLASS_WEAK_SET: return JS_BUILTIN_OWNER_WEAKSET_PROTOTYPE_METHOD;
    case JS_CLASS_WEAK_REF: return JS_BUILTIN_OWNER_WEAKREF_PROTOTYPE_METHOD;
    case JS_CLASS_FINALIZATION_REGISTRY:
        return JS_BUILTIN_OWNER_FINALIZATION_REGISTRY_PROTOTYPE_METHOD;
    case JS_CLASS_ARRAY_BUFFER: return JS_BUILTIN_OWNER_ARRAYBUFFER_PROTOTYPE_METHOD;
    case JS_CLASS_DATA_VIEW: return JS_BUILTIN_OWNER_DATAVIEW_PROTOTYPE_METHOD;
    case JS_CLASS_TYPED_ARRAY: return JS_BUILTIN_OWNER_TYPED_ARRAY_PROTOTYPE_METHOD;
    default:
        return js_get_prototype_owner_for_type(fallback_type);
    }
}

static void js_append_builtin_method_spec_names(JsBuiltinOwner owner, Item result) {
    if (owner == JS_BUILTIN_OWNER_NONE) return;
    for (int i = 0; JS_BUILTIN_METHOD_SPECS[i].name; i++) {
        const JsBuiltinMethodSpec* spec = &JS_BUILTIN_METHOD_SPECS[i];
        if (spec->owner != owner) continue;
        Item key = (Item){.item = s2it(heap_create_name(spec->name, spec->len))};
        js_array_push(result, key);
    }
}

static Item js_builtin_registry_data_descriptor_from_spec(const JsBuiltinMethodSpec* spec) {
    if (!spec) return make_js_undefined();
    Item value = js_create_builtin_function_from_spec(spec);
    Item desc = js_new_object();
    js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
    js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
    return desc;
}

static Item js_builtin_registry_accessor_descriptor_from_spec(const JsBuiltinMethodSpec* spec) {
    if (!spec) return make_js_undefined();
    Item getter = js_create_builtin_function_from_spec(spec);
    Item desc = js_new_object();
    js_property_set(desc, (Item){.item = s2it(heap_create_name("get", 3))}, getter);
    js_property_set(desc, (Item){.item = s2it(heap_create_name("set", 3))}, make_js_undefined());
    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
    return desc;
}

static Item js_lookup_unconditional_object_prototype_method(const char* name, int len) {
    const JsBuiltinMethodSpec* spec = js_builtin_catalog_find(
        JS_BUILTIN_OWNER_OBJECT_PROTOTYPE_METHOD, name, len);
    if (!spec) return ItemNull;
    if (spec->builtin_id == JS_BUILTIN_OBJ_TO_STRING ||
        spec->builtin_id == JS_BUILTIN_OBJ_VALUE_OF) {
        return ItemNull;
    }
    return js_get_or_create_builtin(spec->builtin_id,
                                    js_builtin_method_spec_display_name(spec),
                                    spec->param_count);
}

extern "C" Item js_builtin_registry_prototype_method_descriptor(
    int js_class, TypeId fallback_type, const char* name, int len) {
    JsBuiltinOwner owner = js_get_prototype_owner_for_class_or_type(js_class, fallback_type);
    const JsBuiltinMethodSpec* spec = js_builtin_catalog_find(owner, name, len);
    if (spec) return js_builtin_registry_data_descriptor_from_spec(spec);

    if ((JsClass)js_class == JS_CLASS_TYPED_ARRAY) {
        spec = js_builtin_catalog_find(JS_BUILTIN_OWNER_TYPED_ARRAY_STUB_METHOD, name, len);
        if (spec) return js_builtin_registry_data_descriptor_from_spec(spec);
        spec = js_builtin_catalog_find(JS_BUILTIN_OWNER_TYPED_ARRAY_ACCESSOR, name, len);
        if (spec) return js_builtin_registry_accessor_descriptor_from_spec(spec);
    }
    if ((JsClass)js_class == JS_CLASS_DATA_VIEW) {
        spec = js_builtin_catalog_find(JS_BUILTIN_OWNER_DATAVIEW_ACCESSOR, name, len);
        if (spec) return js_builtin_registry_accessor_descriptor_from_spec(spec);
    }
    if ((JsClass)js_class == JS_CLASS_ARRAY_BUFFER) {
        spec = js_builtin_catalog_find(JS_BUILTIN_OWNER_ARRAYBUFFER_ACCESSOR, name, len);
        if (spec) return js_builtin_registry_accessor_descriptor_from_spec(spec);
    }
    return make_js_undefined();
}

extern "C" bool js_builtin_registry_has_prototype_method(
    int js_class, TypeId fallback_type, const char* name, int len) {
    Item desc = js_builtin_registry_prototype_method_descriptor(js_class, fallback_type, name, len);
    return get_type_id(desc) != LMD_TYPE_UNDEFINED;
}

extern "C" void js_append_builtin_method_names_for_class(
    int js_class, TypeId fallback_type, Item result) {
    JsBuiltinOwner owner = js_get_prototype_owner_for_class_or_type(js_class, fallback_type);
    js_append_builtin_method_spec_names(owner, result);
    if ((JsClass)js_class == JS_CLASS_TYPED_ARRAY) {
        js_append_builtin_method_spec_names(JS_BUILTIN_OWNER_TYPED_ARRAY_STUB_METHOD, result);
        js_append_builtin_method_spec_names(JS_BUILTIN_OWNER_TYPED_ARRAY_ACCESSOR, result);
    } else if ((JsClass)js_class == JS_CLASS_DATA_VIEW) {
        js_append_builtin_method_spec_names(JS_BUILTIN_OWNER_DATAVIEW_ACCESSOR, result);
    } else if ((JsClass)js_class == JS_CLASS_ARRAY_BUFFER) {
        js_append_builtin_method_spec_names(JS_BUILTIN_OWNER_ARRAYBUFFER_ACCESSOR, result);
    }
}

void js_populate_builtin_prototype_methods(Item prototype, const char* ctor_name, int ctor_len) {
    const JsBuiltinOwnerBinding* binding = js_find_owner_binding(ctor_name, ctor_len);
    if (!binding) return;
    js_install_builtin_method_specs(prototype, binding->prototype_owner);
    js_install_builtin_accessor_specs(prototype, binding->accessor_owner);
}

Item js_lookup_builtin_prototype_method_for_class(JsClass cls, const char* name, int len) {
    JsBuiltinOwner owner = js_get_prototype_owner_for_class_or_type((int)cls, LMD_TYPE_MAP);
    return js_lookup_builtin_method_spec(owner, name, len);
}

void js_builtin_cache_reset() {
    for (int i = 0; i < JS_BUILTIN_MAX; i++) js_builtin_cache[i] = ItemNull;
}

Item js_get_or_create_builtin(int builtin_id, const char* name, int param_count) {
    if (!js_builtin_cache_init) {
        for (int i = 0; i < JS_BUILTIN_MAX; i++) js_builtin_cache[i] = ItemNull;
        js_builtin_cache_init = true;
    }
    if (js_builtin_cache[builtin_id].item != ItemNull.item) {
        return js_builtin_cache[builtin_id];
    }
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = NULL;  // not needed, dispatch uses builtin_id
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->builtin_id = builtin_id;
    if (builtin_id == JS_BUILTIN_FUNC_THROW_TYPE_ERROR) {
        fn->name = heap_create_name("", 0);
    } else {
        fn->name = heap_create_name(name, strlen(name));
    }
    fn->prototype = ItemNull;
    // bound_this_store remains zeroed; bound state is represented only by its
    // flag so scalar payload storage is never mistaken for a truthy Item.
    Item result = {.function = (Function*)fn};
    if (builtin_id == JS_BUILTIN_FUNC_THROW_TYPE_ERROR) {
        Item length_key = (Item){.item = s2it(heap_create_name("length", 6))};
        js_func_init_property(result, length_key, (Item){.item = i2it(0)});
        js_attr_set_writable(result, "length", 6, false);
        js_attr_set_enumerable(result, "length", 6, false);
        js_attr_set_configurable(result, "length", 6, false);

        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        js_func_init_property(result, name_key, (Item){.item = s2it(heap_create_name("", 0))});
        js_attr_set_writable(result, "name", 4, false);
        js_attr_set_enumerable(result, "name", 4, false);
        js_attr_set_configurable(result, "name", 4, false);

        Item non_ext_key = (Item){.item = s2it(heap_create_name("__non_extensible__", 17))};
        js_func_init_property(result, non_ext_key, (Item){.item = b2it(true)});
        Item frozen_key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
        js_func_init_property(result, frozen_key, (Item){.item = b2it(true)});
    }
    js_builtin_cache[builtin_id] = result;
    return result;
}

// Wrapper for js_globals.cpp to create Symbol.for / Symbol.keyFor builtins
extern "C" Item js_symbol_builtin_method(int which) {
    if (which == 0) return js_get_or_create_builtin(JS_BUILTIN_SYMBOL_FOR, "for", 1);
    if (which == 1) return js_get_or_create_builtin(JS_BUILTIN_SYMBOL_KEY_FOR, "keyFor", 1);
    return ItemNull;
}

// v18k: Lookup static methods on constructor functions (Object.keys, Array.isArray, etc.)
// Returns ItemNull if not a known constructor or not a known static method.
Item js_lookup_constructor_static(const char* ctor_name, int ctor_len,
                                          const char* prop_name, int prop_len) {
    JsBuiltinOwner owner = js_get_constructor_static_owner(ctor_name, ctor_len);
    Item method = js_lookup_builtin_method_spec(owner, prop_name, prop_len);
    if (method.item != ItemNull.item) return method;

    // Handle .prototype on any constructor — delegate to the constructor's property access
    if (prop_len == 9 && strncmp(prop_name, "prototype", 9) == 0) {
        Item ctor_name_item = (Item){.item = s2it(heap_create_name(ctor_name, ctor_len))};
        Item ctor = js_get_constructor(ctor_name_item);
        if (get_type_id(ctor) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            return js_property_get(ctor, proto_key);
        }
    }
    return ItemNull;
}

// Externally callable wrapper for js_lookup_constructor_static (from transpiler)
extern "C" Item js_constructor_static_property(Item ctor_name, Item prop_name) {
    String* cn = it2s(ctor_name);
    String* pn = it2s(prop_name);
    if (!cn || !pn) return ItemNull;
    Item v = js_lookup_constructor_static(cn->chars, (int)cn->len, pn->chars, (int)pn->len);
    if (v.item != ItemNull.item) return v;
    // Fall back to general property access on the constructor function object.
    // This handles standard function properties (length, name, prototype) and
    // any user-assigned own properties on the constructor.
    Item ctor = js_get_constructor(ctor_name);
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        return js_property_get(ctor, prop_name);
    }
    return make_js_undefined();
}

// Populate %TypedArray%.prototype with proper Array builtin methods
// and static methods on the %TypedArray% constructor.
extern "C" void js_populate_typed_array_base_proto(Item proto, Item base_ctor) {
    // Register constructor
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_property_set(proto, ctor_key, base_ctor);
    js_mark_non_enumerable(proto, ctor_key);

    // Prototype methods: reuse Array builtins (dispatch handles typed arrays)
    js_install_builtin_function_specs(proto, JS_BUILTIN_OWNER_TYPED_ARRAY_PROTOTYPE_METHOD);

    // %TypedArray%.prototype.toString is exactly Array.prototype.toString.
    {
        Item to_string_key = (Item){.item = s2it(heap_create_name("toString", 8))};
        Item array_to_string = js_get_or_create_builtin(JS_BUILTIN_ARR_TO_STRING, "toString", 0);
        js_property_set(proto, to_string_key, array_to_string);
        js_mark_non_enumerable(proto, to_string_key);
    }

    // TypedArray-specific methods (stubs — no Array equivalent)
    js_install_builtin_function_specs(proto, JS_BUILTIN_OWNER_TYPED_ARRAY_STUB_METHOD);

    // Symbol.iterator = values (same function object as TypedArray.prototype.values per spec)
    {
        Item si_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
        Item values_key = (Item){.item = s2it(heap_create_name("values", 6))};
        Item values_fn = js_property_get(proto, values_key);
        js_property_set(proto, si_key, values_fn);
        js_mark_non_enumerable(proto, si_key);
    }

    // get %TypedArray%.prototype[@@toStringTag]
    {
        JsFunction* tag_getter = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
        tag_getter->type_id = LMD_TYPE_FUNC;
        tag_getter->name = heap_create_name("get [Symbol.toStringTag]", 24);
        tag_getter->param_count = 0;
        tag_getter->formal_length = -1;
        Item getter_item = (Item){.function = (Function*)tag_getter};
        Item tag_name = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
        js_install_native_accessor(proto, tag_name, getter_item, ItemNull, JSPD_NON_ENUMERABLE);
    }

    // Accessor getter stubs for buffer, byteLength, byteOffset, length
    // These throw TypeError when accessed on non-TypedArray (ES spec §23.2.3.1/2/3)
    // Phase 3 Stage A: route through unified js_install_native_accessor.
    js_install_builtin_accessor_specs(proto, JS_BUILTIN_OWNER_TYPED_ARRAY_ACCESSOR);

    // Install static methods from/of on %TypedArray% constructor (base_ctor)
    {
        js_install_builtin_method_specs_on_function(
            base_ctor, JS_BUILTIN_OWNER_TYPED_ARRAY_STATIC_METHOD, false);

        // Install get [Symbol.species]() { return this; } on %TypedArray%
        // Phase 3 Stage A: route through unified js_install_native_accessor.
        Item species_name = (Item){.item = s2it(heap_create_name("__sym_6", 7))};
        Item getter_fn = js_get_or_create_builtin(JS_BUILTIN_ITER_IDENTITY, "get [Symbol.species]", 0);
        js_install_native_accessor(base_ctor, species_name, getter_fn, ItemNull, JSPD_NON_ENUMERABLE);
    }
}

// Populate all known static methods on a constructor function as own properties.
// This makes them visible to hasOwnProperty, getOwnPropertyDescriptor, getOwnPropertyNames.
extern "C" void js_populate_constructor_statics(Item ctor_item, const char* ctor_name, int ctor_len) {
    JsBuiltinOwner owner = js_get_constructor_static_owner(ctor_name, ctor_len);
    js_install_builtin_method_specs_on_function(ctor_item, owner, true);

    // ES spec: install get [Symbol.species]() { return this; } on constructors
    // that support @@species (Array, RegExp, Promise, Map, Set, ArrayBuffer, TypedArray constructors)
    const JsBuiltinOwnerBinding* binding = js_find_owner_binding(ctor_name, ctor_len);
    bool needs_species = binding && (binding->flags & JS_BUILTIN_OWNER_BINDING_SPECIES);
    if (needs_species) {
        // install getter: __get___sym_6 → function that returns this
        // Phase 3 Stage A: route through unified js_install_native_accessor.
        Item species_name = (Item){.item = s2it(heap_create_name("__sym_6", 7))};
        Item getter_fn = js_get_or_create_builtin(JS_BUILTIN_ITER_IDENTITY, "get [Symbol.species]", 0);
        js_install_native_accessor(ctor_item, species_name, getter_fn, ItemNull, JSPD_NON_ENUMERABLE);
    }
}

// Lookup built-in method by name for a given receiver type
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len) {
    // Object.prototype methods (available on all objects and arrays)
    Item object_method = js_lookup_unconditional_object_prototype_method(name, len);
    if (object_method.item != ItemNull.item) return object_method;
    if (len == 8 && strncmp(name, "toString", 8) == 0 &&
        !js_builtin_type_has_own_to_string(type)) {
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_TO_STRING, "toString", 0);
    }
    if (type == LMD_TYPE_BOOL) {
        Item method = js_lookup_builtin_method_spec(
            JS_BUILTIN_OWNER_BOOLEAN_PROTOTYPE_METHOD, name, len);
        if (method.item != ItemNull.item) return method;
    }
    if (len == 7 && strncmp(name, "valueOf", 7) == 0 &&
        !js_builtin_type_has_own_value_of(type)) {
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_VALUE_OF, "valueOf", 0);
    }

    // Function.prototype methods
    if (type == LMD_TYPE_FUNC) {
        Item method = js_lookup_builtin_method_spec(
            JS_BUILTIN_OWNER_FUNCTION_PROTOTYPE_METHOD, name, len);
        if (method.item != ItemNull.item) return method;
    }

    // Array.prototype methods
    if (type == LMD_TYPE_ARRAY) {
        Item method = js_lookup_builtin_method_spec(
            JS_BUILTIN_OWNER_ARRAY_PROTOTYPE_METHOD, name, len);
        if (method.item != ItemNull.item) return method;
    }

    // String.prototype methods
    if (type == LMD_TYPE_STRING) {
        Item method = js_lookup_builtin_method_spec(
            JS_BUILTIN_OWNER_STRING_PROTOTYPE_METHOD, name, len);
        if (method.item != ItemNull.item) return method;
    }

    // Number.prototype methods
    if (js_builtin_type_uses_number_prototype(type)) {
        Item method = js_lookup_builtin_method_spec(
            JS_BUILTIN_OWNER_NUMBER_PROTOTYPE_METHOD, name, len);
        if (method.item != ItemNull.item) return method;
    }

    return ItemNull;
}

// v26: Return all builtin method names for a prototype type as a Lambda array.
// Used by getOwnPropertyNames to enumerate builtin methods on prototype objects.
extern "C" void js_append_builtin_method_names(TypeId type, Item result) {
    JsBuiltinOwner owner = js_get_prototype_owner_for_type(type);
    if (owner == JS_BUILTIN_OWNER_NONE) owner = JS_BUILTIN_OWNER_OBJECT_PROTOTYPE_METHOD;
    if (owner != JS_BUILTIN_OWNER_NONE) {
        js_append_builtin_method_spec_names(owner, result);
        if (type == LMD_TYPE_ARRAY) {
            Item locale_key = (Item){.item = s2it(heap_create_name("toLocaleString", 14))};
            js_array_push(result, locale_key);
        }
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
        js_array_push(result, ctor_key);
        return;
    }
}
