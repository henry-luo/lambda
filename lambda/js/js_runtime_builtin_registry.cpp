/**
 * JavaScript runtime built-in registry tables for Lambda.
 */
#include "js_runtime_internal.hpp"

// =============================================================================
// Built-in registry
// =============================================================================

// Intrinsic functions are realm-owned binding values. A shared target never
// implies JavaScript identity; only an explicit binding alias can share a slot.
#define js_builtin_cache (js_runtime_state.builtin_cache.entries)
#define js_builtin_cache_init (js_runtime_state.builtin_cache.initialized)

static bool js_builtin_cache_ensure_roots(void) {
    return js_root_range_ensure_registered(&js_runtime_state.builtin_cache.roots);
}


static const JsIntrinsicTargetSpec JS_INTRINSIC_TARGET_SPECS[] = {
    {JS_BUILTIN_NONE, NULL, NULL,
        JS_BUILTIN_MIR_GENERIC},
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, call_body, mir_kind) \
    {id, call_body, NULL, mir_kind},
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind) \
    {id, call_body, construct_body, mir_kind},
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
};

static const JsBuiltinMethodSpec JS_BUILTIN_METHOD_SPECS[] = {
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, call_body, mir_kind)
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias) \
    {owner, name, len, id, arity, display_name, property_kind, flags, identity_alias},
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
    {JS_BUILTIN_OWNER_NONE, NULL, 0, 0, 0, NULL, JS_BUILTIN_PROPERTY_METHOD,
        0, JS_INTRINSIC_ALIAS_NONE}
};

static const JsBuiltinGlobalSpec JS_BUILTIN_GLOBAL_SPECS[] = {
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, call_body, mir_kind)
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags) \
    {id, name, len, kind, runtime_id, target_id, arity, flags},
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
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
static bool js_builtin_catalog_valid = true;

static void js_builtin_global_initialize_index() {
    if (js_builtin_global_lookup_index_initialized) return;
    int count = (int)(sizeof(JS_BUILTIN_GLOBAL_SPECS) / sizeof(JS_BUILTIN_GLOBAL_SPECS[0]));
    for (int i = 0; i < count; i++) {
        const JsBuiltinGlobalSpec* spec = &JS_BUILTIN_GLOBAL_SPECS[i];
        if (spec->kind != JS_BUILTIN_GLOBAL_NAMESPACE) {
            const JsIntrinsicTargetSpec* target =
                js_intrinsic_target_find(spec->target_id);
            bool invalid_function = spec->kind == JS_BUILTIN_GLOBAL_FUNCTION &&
                (!target || !target->call_body || target->construct_body);
            bool invalid_constructor =
                spec->kind == JS_BUILTIN_GLOBAL_CONSTRUCTOR &&
                (!target || !target->call_body || !target->construct_body);
            if (invalid_function || invalid_constructor) {
                log_error("js-callable-catalog: global binding '%s' has an invalid call/construct matrix",
                    spec->name);
                js_builtin_catalog_valid = false;
            }
        }
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

int js_builtin_global_count() {
    return (int)(sizeof(JS_BUILTIN_GLOBAL_SPECS) / sizeof(JS_BUILTIN_GLOBAL_SPECS[0]));
}

const JsBuiltinGlobalSpec* js_builtin_global_at(int index) {
    if (index < 0 || index >= js_builtin_global_count()) return NULL;
    return &JS_BUILTIN_GLOBAL_SPECS[index];
}

static_assert(sizeof(JS_INTRINSIC_TARGET_SPECS) /
                  sizeof(JS_INTRINSIC_TARGET_SPECS[0]) == JS_BUILTIN_MAX,
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
        if (spec->owner <= JS_BUILTIN_OWNER_NONE ||
            spec->owner >= JS_BUILTIN_OWNER_MAX) {
            log_error("js-callable-catalog: binding '%s' has invalid owner %d",
                spec->name, (int)spec->owner);
            js_builtin_catalog_valid = false;
            continue;
        }
        if (spec->builtin_id > JS_BUILTIN_NONE) {
            const JsIntrinsicTargetSpec* target = js_intrinsic_target_find(
                spec->builtin_id);
            if (!target || target->catalog_id != spec->builtin_id ||
                (!target->call_body && !target->construct_body)) {
                log_error("js-callable-catalog: binding '%s' has no executable target",
                    spec->name);
                js_builtin_catalog_valid = false;
                continue;
            }
        }
        uint32_t slot = js_builtin_spec_name_hash(spec->owner, spec->name, spec->len) &
                        (JS_BUILTIN_LOOKUP_INDEX_SIZE - 1u);
        while (js_builtin_lookup_index[slot]) {
            const JsBuiltinMethodSpec* prior = js_builtin_lookup_index[slot];
            if (prior->owner == spec->owner && prior->len == spec->len &&
                strncmp(prior->name, spec->name, spec->len) == 0) {
                log_error("js-callable-catalog: duplicate owner/property binding '%s'",
                    spec->name);
                js_builtin_catalog_valid = false;
                break;
            }
            slot = (slot + 1u) & (JS_BUILTIN_LOOKUP_INDEX_SIZE - 1u);
        }
        if (!js_builtin_catalog_valid && js_builtin_lookup_index[slot]) continue;
        js_builtin_lookup_index[slot] = spec;
        if (spec->builtin_id > 0 && spec->builtin_id < JS_BUILTIN_MAX &&
            !js_builtin_id_index[spec->builtin_id]) {
            js_builtin_id_index[spec->builtin_id] = spec;
        }
        if (spec->identity_alias != JS_INTRINSIC_ALIAS_NONE) {
            for (int j = 0; j < i; j++) {
                const JsBuiltinMethodSpec* alias = &JS_BUILTIN_METHOD_SPECS[j];
                if (alias->identity_alias != spec->identity_alias) continue;
                const char* alias_name = js_builtin_method_spec_display_name(alias);
                const char* spec_name = js_builtin_method_spec_display_name(spec);
                if (alias->builtin_id != spec->builtin_id ||
                    alias->param_count != spec->param_count ||
                    alias->property_kind != spec->property_kind ||
                    alias->flags != spec->flags || strcmp(alias_name, spec_name) != 0) {
                    log_error("js-callable-catalog: incompatible identity alias '%s'",
                        spec->name);
                    js_builtin_catalog_valid = false;
                }
                break;
            }
        }
    }
    js_builtin_lookup_index_initialized = true;
}

const JsBuiltinMethodSpec* js_builtin_catalog_find(JsBuiltinOwner owner,
                                                    const char* name, int len) {
    if (owner <= JS_BUILTIN_OWNER_NONE || owner >= JS_BUILTIN_OWNER_MAX || !name) return NULL;
    js_builtin_catalog_initialize_index();
    if (!js_builtin_catalog_valid) return NULL;
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

const JsIntrinsicTargetSpec* js_intrinsic_target_find(int catalog_id) {
    if (catalog_id <= JS_BUILTIN_NONE || catalog_id >= JS_BUILTIN_MAX) {
        return NULL;
    }
    return &JS_INTRINSIC_TARGET_SPECS[catalog_id];
}

static Item js_create_builtin_function_from_spec(const JsBuiltinMethodSpec* spec) {
    if (!spec || !js_active_runtime_state || !js_builtin_cache_ensure_roots()) {
        return ItemError;
    }
    if (!js_builtin_cache_init) {
        for (int i = 0; i < JS_INTRINSIC_BINDING_COUNT; i++) {
            js_builtin_cache[i] = ItemNull;
        }
        js_builtin_cache_init = true;
    }
    int identity_slot = (int)(spec - JS_BUILTIN_METHOD_SPECS);
    if (identity_slot < 0 || identity_slot >= JS_INTRINSIC_BINDING_COUNT) {
        return ItemError;
    }
    if (spec->identity_alias != JS_INTRINSIC_ALIAS_NONE) {
        // Binding identity is unique unless the catalog names an alias group.
        for (int i = 0; i < identity_slot; i++) {
            if (JS_BUILTIN_METHOD_SPECS[i].identity_alias == spec->identity_alias) {
                identity_slot = i;
                break;
            }
        }
    }
    if (js_builtin_cache[identity_slot].item != 0 &&
        js_builtin_cache[identity_slot].item != ItemNull.item) {
        return js_builtin_cache[identity_slot];
    }
    const char* display_name = js_builtin_method_spec_display_name(spec);
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    js_function_init_native_module_scope(fn);
    fn->type_id = LMD_TYPE_FUNC;
    fn->param_count = spec->param_count;
    fn->formal_length = -1;
    fn->catalog_id = spec->builtin_id;
    if (spec->builtin_id > JS_BUILTIN_NONE) {
        const JsIntrinsicTargetSpec* target = js_intrinsic_target_find(
            spec->builtin_id);
        if (!target || !target->call_body) return ItemError;
        fn->native_call = target->call_body;
        fn->native_construct = target->construct_body;
        fn->native_policy = JS_NATIVE_CALL_BODY;
    }
    fn->name = heap_create_name(display_name, strlen(display_name));
    fn->flags = spec->flags;
    js_function_finalize_capabilities(fn);
    Item result = (Item){.function = (Function*)fn};
    if (spec->builtin_id == JS_BUILTIN_FUNC_THROW_TYPE_ERROR) {
        Item length_key = (Item){.item = s2it(heap_create_name("length", 6))};
        js_func_init_property(result, length_key, (Item){.item = i2it(0)});
        js_attr_set_writable(result, "length", 6, false);
        js_attr_set_enumerable(result, "length", 6, false);
        js_attr_set_configurable(result, "length", 6, false);
        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        js_func_init_property(result, name_key,
            (Item){.item = s2it(heap_create_name("", 0))});
        js_attr_set_writable(result, "name", 4, false);
        js_attr_set_enumerable(result, "name", 4, false);
        js_attr_set_configurable(result, "name", 4, false);
        Item non_ext_key = (Item){.item = s2it(heap_create_name(
            "__non_extensible__", 17))};
        js_func_init_property(result, non_ext_key, (Item){.item = b2it(true)});
        Item frozen_key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
        js_func_init_property(result, frozen_key, (Item){.item = b2it(true)});
    }
    js_builtin_cache[identity_slot] = result;
    return result;
}

Item js_intrinsic_binding_get(JsBuiltinOwner owner, const char* name, int len) {
    const JsBuiltinMethodSpec* spec = js_builtin_catalog_find(owner, name, len);
    if (!spec || spec->builtin_id <= JS_BUILTIN_NONE) return ItemNull;
    return js_create_builtin_function_from_spec(spec);
}

void js_install_builtin_method_specs(Item object, JsBuiltinOwner owner) {
    for (int i = 0; JS_BUILTIN_METHOD_SPECS[i].name; i++) {
        const JsBuiltinMethodSpec* spec = &JS_BUILTIN_METHOD_SPECS[i];
        if (spec->owner != owner || spec->property_kind != JS_BUILTIN_PROPERTY_METHOD) continue;
        Item key = (Item){.item = s2it(heap_create_name(spec->name, spec->len))};
        Item fn = js_create_builtin_function_from_spec(spec);
        js_set_key_default(object, key, fn);
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
#define JS_BUILTIN_ID(id, call_body, mir_kind)
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags)
#define JS_BUILTIN_OWNER_BINDING(name, len, member, prototype, accessor, flags) \
    {name, len, member, prototype, accessor, flags},
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_OWNER_BINDING
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
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

static JsBuiltinOwner js_get_constructor_static_owner(const char* ctor_name, int ctor_len) {
    const JsBuiltinOwnerBinding* binding = js_find_owner_binding(ctor_name, ctor_len);
    return binding ? binding->member_owner : JS_BUILTIN_OWNER_NONE;
}

void js_populate_builtin_prototype_methods(Item prototype, const char* ctor_name, int ctor_len) {
    const JsBuiltinOwnerBinding* binding = js_find_owner_binding(ctor_name, ctor_len);
    if (!binding) return;
    js_install_builtin_method_specs(prototype, binding->prototype_owner);
    js_install_builtin_accessor_specs(prototype, binding->accessor_owner);
}

void js_builtin_cache_reset() {
    if (!js_active_runtime_state) return;
    for (int i = 0; i < JS_INTRINSIC_BINDING_COUNT; i++) {
        js_builtin_cache[i] = ItemNull;
    }
}

// Wrapper for js_globals.cpp to create Symbol.for / Symbol.keyFor builtins
extern "C" Item js_symbol_builtin_method(int which) {
    if (which == 0) return js_intrinsic_binding_get(
        JS_BUILTIN_OWNER_SYMBOL_STATIC_METHOD, "for", 3);
    if (which == 1) return js_intrinsic_binding_get(
        JS_BUILTIN_OWNER_SYMBOL_STATIC_METHOD, "keyFor", 6);
    return ItemNull;
}

// Populate %TypedArray%.prototype with proper Array builtin methods
// and static methods on the %TypedArray% constructor.
extern "C" void js_populate_typed_array_base_proto(Item proto, Item base_ctor) {
    // Register constructor
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_set_key_default(proto, ctor_key, base_ctor);
    js_mark_non_enumerable(proto, ctor_key);

    // Prototype methods: reuse Array builtins (dispatch handles typed arrays)
    js_install_builtin_function_specs(proto, JS_BUILTIN_OWNER_TYPED_ARRAY_PROTOTYPE_METHOD);

    // %TypedArray%.prototype.toString is exactly Array.prototype.toString.
    {
        Item to_string_key = (Item){.item = s2it(heap_create_name("toString", 8))};
        Item array_to_string = js_intrinsic_binding_get(
            JS_BUILTIN_OWNER_ARRAY_PROTOTYPE_METHOD, "toString", 8);
        js_set_key_default(proto, to_string_key, array_to_string);
        js_mark_non_enumerable(proto, to_string_key);
    }

    // TypedArray-specific methods (stubs — no Array equivalent)
    js_install_builtin_function_specs(proto, JS_BUILTIN_OWNER_TYPED_ARRAY_STUB_METHOD);

    // Symbol.iterator = values (same function object as TypedArray.prototype.values per spec)
    {
        Item si_key = js_well_known_symbol_key(1);
        Item values_key = (Item){.item = s2it(heap_create_name("values", 6))};
        Item values_fn = js_get_key_default(proto, values_key);
        js_set_key_default(proto, si_key, values_fn);
        js_mark_non_enumerable(proto, si_key);
    }

    // get %TypedArray%.prototype[@@toStringTag]
    {
        JsFunction* tag_getter = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
        js_function_init_native_module_scope(tag_getter);
        tag_getter->type_id = LMD_TYPE_FUNC;
        tag_getter->name = heap_create_name("get [Symbol.toStringTag]", 24);
        tag_getter->param_count = 0;
        tag_getter->formal_length = -1;
        // The symbol accessor's spelling is observable metadata; its stored
        // body protects callable behavior from later name mutation.
        tag_getter->native_call = js_intrinsic_typed_array_to_string_tag_body;
        tag_getter->native_policy = JS_NATIVE_CALL_BODY;
        js_function_finalize_capabilities(tag_getter);
        Item getter_item = (Item){.function = (Function*)tag_getter};
        Item tag_name = js_well_known_symbol_key(4);
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
        Item species_name = js_well_known_symbol_key(6);
        Item getter_fn = js_intrinsic_binding_get(
            JS_BUILTIN_OWNER_SPECIES_INTERNAL, "[Symbol.species]", 16);
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
        // install the Symbol.species getter that returns this
        // Phase 3 Stage A: route through unified js_install_native_accessor.
        Item species_name = js_well_known_symbol_key(6);
        Item getter_fn = js_intrinsic_binding_get(
            JS_BUILTIN_OWNER_SPECIES_INTERNAL, "[Symbol.species]", 16);
        js_install_native_accessor(ctor_item, species_name, getter_fn, ItemNull, JSPD_NON_ENUMERABLE);
    }
}
