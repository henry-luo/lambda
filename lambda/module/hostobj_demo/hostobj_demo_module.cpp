#include "../../lambda.hpp"
#include "../../jube/jube_registry.h"
#include "../../../lib/mem.h"
#include "../../../lib/log.h"
#include <string.h>

typedef struct HostObjDemoNative {
    int64_t value;
    Item expando;
    bool expando_rooted;
} HostObjDemoNative;

static Item s_hostobj_demo_namespace = ItemNull;
static Item s_hostobj_demo_proto = ItemNull;
static Item s_hostobj_demo_ctor = ItemNull;
static bool s_hostobj_demo_roots_registered = false;
static int64_t s_hostobj_demo_destroy_count = 0;
static const JubeHostAPI* s_hostobj_demo_host = NULL;

extern const JubeTypeDef s_hostobj_demo_types[1];
static void hostobj_demo_destroy(void* payload);

static Item hostobj_demo_key(const char* name) {
    return (Item){.item = s2it(heap_create_name(name))};
}

static bool hostobj_demo_key_eq(Item key, const char* name) {
    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* str = key.get_safe_string();
    size_t len = strlen(name);
    return str && str->len == len && memcmp(str->chars, name, len) == 0;
}

static Item hostobj_demo_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static HostObjDemoNative* hostobj_demo_native(Item receiver) {
    if (get_type_id(receiver) != LMD_TYPE_VMAP || !receiver.vmap ||
            receiver.vmap->host_type != (const void*)&s_hostobj_demo_types[0]) {
        return NULL;
    }
    return (HostObjDemoNative*)receiver.vmap->host_data;
}

static int64_t hostobj_demo_item_to_int(Item value, int64_t fallback) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) return it2i(value);
    if (type == LMD_TYPE_INT64) return *((int64_t*)(value.item & 0x00FFFFFFFFFFFFFF));
    if (type == LMD_TYPE_FLOAT) return (int64_t)it2d(value);
    return fallback;
}

static Item hostobj_demo_expando(HostObjDemoNative* native) {
    if (!native) return ItemNull;
    if (native->expando.item == ItemNull.item || native->expando.item == 0) {
        native->expando = s_hostobj_demo_host->value->new_object();
        s_hostobj_demo_host->gc->register_root(&native->expando.item);
        native->expando_rooted = true;
    }
    return native->expando;
}

static Item hostobj_demo_data_descriptor(Item value, bool writable, bool enumerable, bool configurable) {
    Item desc = s_hostobj_demo_host->value->new_object();
    s_hostobj_demo_host->value->property_set(desc, hostobj_demo_key("value"), value);
    s_hostobj_demo_host->value->property_set(desc, hostobj_demo_key("writable"),
        (Item){.item = b2it(writable)});
    s_hostobj_demo_host->value->property_set(desc, hostobj_demo_key("enumerable"),
        (Item){.item = b2it(enumerable)});
    s_hostobj_demo_host->value->property_set(desc, hostobj_demo_key("configurable"),
        (Item){.item = b2it(configurable)});
    return desc;
}

static Item hostobj_demo_get_prototype(void) {
    if (s_hostobj_demo_proto.item != ItemNull.item && s_hostobj_demo_proto.item != 0) {
        return s_hostobj_demo_proto;
    }
    s_hostobj_demo_proto = s_hostobj_demo_host->value->new_object();
    s_hostobj_demo_host->gc->register_root(&s_hostobj_demo_proto.item);
    return s_hostobj_demo_proto;
}

static Item hostobj_demo_construct(Item initial);

static Item hostobj_demo_get_constructor(void) {
    if (s_hostobj_demo_ctor.item != ItemNull.item && s_hostobj_demo_ctor.item != 0) {
        return s_hostobj_demo_ctor;
    }
    Item proto = hostobj_demo_get_prototype();
    s_hostobj_demo_ctor = s_hostobj_demo_host->script->new_function((void*)hostobj_demo_construct, 1);
    s_hostobj_demo_host->script->set_function_name(s_hostobj_demo_ctor, hostobj_demo_key("HostObjDemo"));
    s_hostobj_demo_host->script->function_set_prototype(s_hostobj_demo_ctor, proto);
    s_hostobj_demo_host->value->property_set(proto, hostobj_demo_key("constructor"), s_hostobj_demo_ctor);
    s_hostobj_demo_host->script->mark_non_enumerable(proto, hostobj_demo_key("constructor"));
    s_hostobj_demo_host->gc->register_root(&s_hostobj_demo_ctor.item);
    return s_hostobj_demo_ctor;
}

static Item hostobj_demo_make(int64_t value) {
    HostObjDemoNative* native = (HostObjDemoNative*)mem_calloc(1, sizeof(HostObjDemoNative), MEM_CAT_JS_RUNTIME);
    native->value = value;
    native->expando = ItemNull;
    Item wrapper = s_hostobj_demo_host->value->vmap_new();
    if (get_type_id(wrapper) != LMD_TYPE_VMAP || !wrapper.vmap) {
        mem_free(native);
        return ItemNull;
    }
    wrapper.vmap->host_type = (const void*)&s_hostobj_demo_types[0];
    wrapper.vmap->host_data = native;
    return wrapper;
}

static Item hostobj_demo_construct(Item initial) {
    return hostobj_demo_make(hostobj_demo_item_to_int(initial, 0));
}

static Item hostobj_demo_destroyed(void) {
    return (Item){.item = i2it(s_hostobj_demo_destroy_count)};
}

static Item fn_hostobj_demo_answer(void) {
    return (Item){.item = i2it(42)};
}

static Item fn_hostobj_demo_add(Item left, Item right) {
    int64_t value = hostobj_demo_item_to_int(left, 0) + hostobj_demo_item_to_int(right, 0);
    return (Item){.item = i2it(value)};
}

static Item hostobj_demo_release(Item receiver) {
    if (get_type_id(receiver) != LMD_TYPE_VMAP || !receiver.vmap ||
            receiver.vmap->host_type != (const void*)&s_hostobj_demo_types[0]) {
        return hostobj_demo_undefined();
    }
    HostObjDemoNative* native = (HostObjDemoNative*)receiver.vmap->host_data;
    if (native) {
        receiver.vmap->host_data = NULL;
        hostobj_demo_destroy(native);
    }
    return hostobj_demo_undefined();
}

static int hostobj_demo_get_property(Item receiver, Item key, Item* out) {
    if (!out) return 0;
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (!native) {
        *out = hostobj_demo_undefined();
        return 1;
    }
    if (hostobj_demo_key_eq(key, "value")) {
        *out = (Item){.item = i2it(native->value)};
        return 1;
    }
    if (hostobj_demo_key_eq(key, "label")) {
        *out = hostobj_demo_key("hostobj-demo");
        return 1;
    }
    if (hostobj_demo_key_eq(key, "bump")) {
        *out = s_hostobj_demo_host->script->new_function((void*)hostobj_demo_construct, 1);
        return 1;
    }
    if (native->expando.item != ItemNull.item && native->expando.item != 0) {
        *out = s_hostobj_demo_host->value->property_get(native->expando, key);
        return 1;
    }
    *out = hostobj_demo_undefined();
    return 1;
}

static int hostobj_demo_set_property(Item receiver, Item key, Item value, Item* out) {
    if (!out) return 0;
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (!native) {
        *out = hostobj_demo_undefined();
        return 1;
    }
    if (hostobj_demo_key_eq(key, "value")) {
        native->value = hostobj_demo_item_to_int(value, native->value);
        *out = value;
        return 1;
    }
    if (hostobj_demo_key_eq(key, "label") || hostobj_demo_key_eq(key, "bump")) {
        *out = value;
        return 1;
    }
    Item expando = hostobj_demo_expando(native);
    *out = s_hostobj_demo_host->value->property_set(expando, key, value);
    return 1;
}

static int hostobj_demo_call_method(Item receiver, Item method_name, Item* args, int argc, Item* out) {
    if (!out) return 0;
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (!native) {
        *out = hostobj_demo_undefined();
        return 1;
    }
    if (hostobj_demo_key_eq(method_name, "bump")) {
        int64_t delta = argc > 0 ? hostobj_demo_item_to_int(args[0], 1) : 1;
        native->value += delta;
        *out = (Item){.item = i2it(native->value)};
        return 1;
    }
    return 0;
}

static int hostobj_demo_has_property(Item receiver, Item key, Item* out) {
    if (!out) return 0;
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    bool present = hostobj_demo_key_eq(key, "value") ||
        hostobj_demo_key_eq(key, "label") ||
        hostobj_demo_key_eq(key, "bump");
    if (!present && native && native->expando.item != ItemNull.item && native->expando.item != 0) {
        Item value = s_hostobj_demo_host->value->property_get(native->expando, key);
        present = value.item != ITEM_JS_UNDEFINED && value.item != ITEM_NULL;
    }
    *out = (Item){.item = b2it(present)};
    return 1;
}

static int hostobj_demo_delete_property(Item receiver, Item key, Item* out) {
    if (!out) return 0;
    if (hostobj_demo_key_eq(key, "value") || hostobj_demo_key_eq(key, "label") ||
            hostobj_demo_key_eq(key, "bump")) {
        *out = (Item){.item = b2it(false)};
        return 1;
    }
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (native && native->expando.item != ItemNull.item && native->expando.item != 0) {
        *out = s_hostobj_demo_host->script->reflect_delete_property(native->expando, key);
        return 1;
    }
    *out = (Item){.item = b2it(true)};
    return 1;
}

static int hostobj_demo_get_own_property_descriptor(Item receiver, Item key, Item* out) {
    if (!out) return 0;
    Item value = hostobj_demo_undefined();
    if (hostobj_demo_key_eq(key, "value") || hostobj_demo_key_eq(key, "label")) {
        hostobj_demo_get_property(receiver, key, &value);
        *out = hostobj_demo_data_descriptor(value, hostobj_demo_key_eq(key, "value"), true, false);
        return 1;
    }
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (native && native->expando.item != ItemNull.item && native->expando.item != 0) {
        value = s_hostobj_demo_host->value->property_get(native->expando, key);
        if (value.item != ITEM_JS_UNDEFINED && value.item != ITEM_NULL) {
            *out = hostobj_demo_data_descriptor(value, true, true, true);
            return 1;
        }
    }
    *out = hostobj_demo_undefined();
    return 1;
}

static int hostobj_demo_own_property_keys(Item receiver, Item* out) {
    if (!out) return 0;
    Item keys = s_hostobj_demo_host->value->array_new(0);
    s_hostobj_demo_host->value->array_push(keys, hostobj_demo_key("value"));
    s_hostobj_demo_host->value->array_push(keys, hostobj_demo_key("label"));
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (native && native->expando.item != ItemNull.item && native->expando.item != 0) {
        Item expando_keys = s_hostobj_demo_host->script->reflect_own_keys(native->expando);
        if (get_type_id(expando_keys) == LMD_TYPE_ARRAY && expando_keys.array) {
            Array* arr = expando_keys.array;
            for (int64_t i = 0; i < arr->length; i++) {
                s_hostobj_demo_host->value->array_push(keys, arr->items[i]);
            }
        }
    }
    *out = keys;
    return 1;
}

static Item hostobj_demo_prototype(Item receiver) {
    (void)receiver;
    return hostobj_demo_get_prototype();
}

static void hostobj_demo_invalidate(Item receiver) {
    if (get_type_id(receiver) != LMD_TYPE_VMAP || !receiver.vmap) return;
    receiver.vmap->host_data = NULL;
}

static void hostobj_demo_destroy(void* payload) {
    HostObjDemoNative* native = (HostObjDemoNative*)payload;
    if (!native) return;
    if (native->expando_rooted) s_hostobj_demo_host->gc->unregister_root(&native->expando.item);
    s_hostobj_demo_destroy_count++;
    mem_free(native);
}

static const JubeHostObjectOps s_hostobj_demo_host_ops = {
    hostobj_demo_get_property,
    hostobj_demo_set_property,
    hostobj_demo_call_method,
    hostobj_demo_has_property,
    hostobj_demo_delete_property,
    hostobj_demo_get_own_property_descriptor,
    hostobj_demo_own_property_keys,
    hostobj_demo_prototype,
    hostobj_demo_invalidate,
    hostobj_demo_destroy
};

const JubeTypeDef s_hostobj_demo_types[1] = {
    {"hostobj_demo", JUBE_TYPE_OWNING_NATIVE, NULL, &s_hostobj_demo_host_ops, NULL},
};

extern "C" Item hostobj_demo_namespace(void) {
    if (!s_hostobj_demo_roots_registered) {
        s_hostobj_demo_host->gc->register_root(&s_hostobj_demo_namespace.item);
        s_hostobj_demo_roots_registered = true;
    }
    if (s_hostobj_demo_namespace.item != ItemNull.item && s_hostobj_demo_namespace.item != 0) {
        return s_hostobj_demo_namespace;
    }
    s_hostobj_demo_namespace = s_hostobj_demo_host->value->new_object();
    Item ctor = hostobj_demo_get_constructor();
    s_hostobj_demo_host->value->property_set(s_hostobj_demo_namespace,
        hostobj_demo_key("HostObjDemo"), ctor);
    s_hostobj_demo_host->value->property_set(s_hostobj_demo_namespace, hostobj_demo_key("create"),
        s_hostobj_demo_host->script->new_function((void*)hostobj_demo_construct, 1));
    s_hostobj_demo_host->value->property_set(s_hostobj_demo_namespace, hostobj_demo_key("release"),
        s_hostobj_demo_host->script->new_function((void*)hostobj_demo_release, 1));
    s_hostobj_demo_host->value->property_set(s_hostobj_demo_namespace, hostobj_demo_key("destroyed"),
        s_hostobj_demo_host->script->new_function((void*)hostobj_demo_destroyed, 0));
    return s_hostobj_demo_namespace;
}

static const char* const s_hostobj_demo_specifiers[] = {
    "hostobjDemo",
    "hostobj_demo",
    NULL
};

static const JubeNamespaceDef s_hostobj_demo_namespaces[] = {
    {s_hostobj_demo_specifiers, 2, hostobj_demo_namespace, NULL, 0},
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
static const JubeFuncDef s_hostobj_demo_functions[] = {
    {"answer", "fn() -> int", (fn_ptr)fn_hostobj_demo_answer, JUBE_FN_NONE,
     "Item fn_hostobj_demo_answer(void)", (fn_ptr)fn_hostobj_demo_answer},
    {"add", "fn(left: int, right: int) -> int", (fn_ptr)fn_hostobj_demo_add, JUBE_FN_NONE,
     "Item fn_hostobj_demo_add(Item left, Item right)", (fn_ptr)fn_hostobj_demo_add},
};
#pragma clang diagnostic pop

static int hostobj_demo_init(const JubeHostAPI* host) {
    if (!host || host->api_version != JUBE_ABI_VERSION || !host->gc || !host->value || !host->script) {
        log_error("JUBE_HOSTOBJ_DEMO: missing required host API tables");
        return -1;
    }
    s_hostobj_demo_host = host;
    log_info("JUBE_HOSTOBJ_DEMO: static module initialized");
    return 0;
}

static const JubeModuleDef s_hostobj_demo_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "hostobj_demo",
    "0.1.0",
    "Jube host-object proof module",
    s_hostobj_demo_types,
    1,
    s_hostobj_demo_functions,
    (int32_t)(sizeof(s_hostobj_demo_functions) / sizeof(s_hostobj_demo_functions[0])),
    s_hostobj_demo_namespaces,
    1,
    hostobj_demo_init,
    NULL
};

extern "C" void hostobj_demo_jube_register_static(void) {
    jube_register_static_module(&s_hostobj_demo_module);
}

extern "C" const JubeModuleDef* hostobj_demo_jube_module(void) {
    return &s_hostobj_demo_module;
}

extern "C" const JubeModuleDef* jube_module(void) {
    return hostobj_demo_jube_module();
}
