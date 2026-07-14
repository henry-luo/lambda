// hostobj_demo — the Jube proof module.
//
// DOM3 form: the script-facing shape is declared once in Lambda type syntax
// (interface_decl) and behavior is a binding table of handler pointers; the
// generic engine dispatch (member records + expando store + prototype) does
// everything the hand-written JubeHostObjectOps used to do. host_ops is NULL —
// that absence is the falsifiable proof of the DOM3 design promise.

#include "../../lambda.hpp"
#include "../../jube/jube_registry.h"
#include "../../../lib/mem.h"
#include "../../../lib/log.h"
#include <string.h>

typedef struct HostObjDemoNative {
    int64_t value;
} HostObjDemoNative;

static Item s_hostobj_demo_namespace = ItemNull;
static Item s_hostobj_demo_ctor = ItemNull;
static bool s_hostobj_demo_roots_registered = false;
static int64_t s_hostobj_demo_destroy_count = 0;
static const JubeHostAPI* s_hostobj_demo_host = NULL;

extern const JubeTypeDef s_hostobj_demo_types[1];
static void hostobj_demo_destroy(void* payload);

static Item hostobj_demo_key(const char* name) {
    return (Item){.item = s2it(heap_create_name(name))};
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

// ---- member bindings (behavior half of the interface declaration) ----

static int hostobj_demo_value_get(Item receiver, Item* out) {
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (!native || !out) return 0;
    *out = (Item){.item = i2it(native->value)};
    return 1;
}

static int hostobj_demo_value_set(Item receiver, Item value, Item* out) {
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (!native || !out) return 0;
    native->value = hostobj_demo_item_to_int(value, native->value);
    *out = value;
    return 1;
}

static int hostobj_demo_label_get(Item receiver, Item* out) {
    (void)receiver;
    if (!out) return 0;
    *out = hostobj_demo_key("hostobj-demo");
    return 1;
}

static int hostobj_demo_bump_call(Item receiver, Item* args, int argc, Item* out) {
    HostObjDemoNative* native = hostobj_demo_native(receiver);
    if (!native || !out) return 0;
    int64_t delta = argc > 0 ? hostobj_demo_item_to_int(args[0], 1) : 1;
    native->value += delta;
    *out = (Item){.item = i2it(native->value)};
    return 1;
}

// ---- construction / namespace ----

static Item hostobj_demo_construct(Item initial);

static Item hostobj_demo_get_constructor(void) {
    if (s_hostobj_demo_ctor.item != ItemNull.item && s_hostobj_demo_ctor.item != 0) {
        return s_hostobj_demo_ctor;
    }
    // the constructor's .prototype must be the same object the generic
    // dispatch reports for instances, or instanceof breaks
    Item proto = jube_type_prototype(&s_hostobj_demo_types[0]);
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

static void hostobj_demo_destroy(void* payload) {
    HostObjDemoNative* native = (HostObjDemoNative*)payload;
    if (!native) return;
    s_hostobj_demo_destroy_count++;
    mem_free(native);
}

// ---- descriptors ----

const JubeTypeDef s_hostobj_demo_types[1] = {
    // host_ops intentionally NULL: dispatch is fully record-driven; the owning
    // finalizer moves to JubeTypeDef.destroy
    {"hostobj_demo", JUBE_TYPE_OWNING_NATIVE, NULL, NULL, hostobj_demo_destroy},
};

static const char s_hostobj_demo_interface[] =
    "type hostobj_demo {\n"
    "    value: int,\n"
    "    label: string,\n"
    "    bump: fn(delta: int) int\n"
    "}\n";

static const JubeMemberBind s_hostobj_demo_members[] = {
    {"value", NULL, NULL, NULL, hostobj_demo_value_get, hostobj_demo_value_set, NULL, NULL},
    {"label", NULL, NULL, NULL, hostobj_demo_label_get, NULL, NULL, NULL},
    {"bump",  NULL, NULL, NULL, NULL, NULL, hostobj_demo_bump_call, NULL},
};

static const JubeTypeBinding s_hostobj_demo_bindings[] = {
    {"hostobj_demo", &s_hostobj_demo_types[0], s_hostobj_demo_members,
     (int32_t)(sizeof(s_hostobj_demo_members) / sizeof(s_hostobj_demo_members[0])),
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
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

static void hostobj_demo_runtime_reset(void) {
    if (s_hostobj_demo_host && s_hostobj_demo_roots_registered) {
        s_hostobj_demo_host->gc->unregister_root(&s_hostobj_demo_namespace.item);
        s_hostobj_demo_host->gc->unregister_root(&s_hostobj_demo_ctor.item);
    }
    // Namespace/constructor Items belong to the current JS heap and must not
    // survive a document or batch-runtime boundary.
    s_hostobj_demo_namespace = ItemNull;
    s_hostobj_demo_ctor = ItemNull;
    s_hostobj_demo_roots_registered = false;
}

static const JubeModuleDef s_hostobj_demo_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "hostobj_demo",
    "0.2.0",
    "Jube host-object proof module (DOM3 declared interface)",
    s_hostobj_demo_types,
    1,
    s_hostobj_demo_functions,
    (int32_t)(sizeof(s_hostobj_demo_functions) / sizeof(s_hostobj_demo_functions[0])),
    s_hostobj_demo_namespaces,
    1,
    hostobj_demo_init,
    NULL,
    s_hostobj_demo_interface,
    s_hostobj_demo_bindings,
    (int32_t)(sizeof(s_hostobj_demo_bindings) / sizeof(s_hostobj_demo_bindings[0])),
    hostobj_demo_runtime_reset,
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
