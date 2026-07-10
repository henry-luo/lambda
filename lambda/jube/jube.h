#pragma once

#include "../lambda.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JUBE_ABI_VERSION 1

typedef struct JubeHostAPI JubeHostAPI;
typedef struct JubeTypeDef JubeTypeDef;
typedef struct JubeFuncDef JubeFuncDef;
typedef struct JubeNamespaceDef JubeNamespaceDef;
typedef struct JubeModuleDef JubeModuleDef;
typedef struct JubeHostObjectOps JubeHostObjectOps;

typedef enum JubeFuncFlags {
    JUBE_FN_NONE = 0,
    JUBE_FN_METHOD_ELIGIBLE = 1u << 0,
    JUBE_FN_VARARGS = 1u << 1,
} JubeFuncFlags;

typedef enum JubeTypeFlags {
    JUBE_TYPE_NONE = 0,
    JUBE_TYPE_NON_OWNING_HOST = 1u << 0,
    JUBE_TYPE_OWNING_NATIVE = 1u << 1,
} JubeTypeFlags;

struct JubeHostObjectOps {
    int (*get_property)(Item receiver, Item key, Item* out);
    int (*set_property)(Item receiver, Item key, Item value, Item* out);
    int (*call_method)(Item receiver, Item method_name, Item* args, int argc, Item* out);
    int (*has_property)(Item receiver, Item key, Item* out);
    int (*delete_property)(Item receiver, Item key, Item* out);
    int (*get_own_property_descriptor)(Item receiver, Item key, Item* out);
    int (*own_property_keys)(Item receiver, Item* out);
    Item (*prototype)(Item receiver);
    void (*invalidate)(Item receiver);
    void (*destroy)(void* native);
};

struct JubeTypeDef {
    const char* name;
    uint32_t flags;
    const void* vmap_ops;
    const JubeHostObjectOps* host_ops;
    // Deprecated for host objects; use host_ops->destroy so the lifecycle
    // surface stays with the rest of the native object protocol.
    void (*destroy)(void* native);
};

struct JubeFuncDef {
    const char* name;
    const char* signature;
    fn_ptr func;
    uint32_t flags;
    const char* native_signature;
    fn_ptr native_func;
};

struct JubeNamespaceDef {
    const char* const* specifiers;
    int32_t specifier_count;
    Item (*build)(void);
    const JubeFuncDef* funcs;
    int32_t func_count;
};

struct JubeHostAPI {
    uint32_t api_version;
};

struct JubeModuleDef {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* name;
    const char* version;
    const char* description;

    const JubeTypeDef* types;
    int32_t type_count;
    const JubeFuncDef* functions;
    int32_t function_count;
    const JubeNamespaceDef* namespaces;
    int32_t namespace_count;

    int (*init)(const JubeHostAPI* host);
    void (*shutdown)(void);
};

#ifdef __cplusplus
}
#endif
