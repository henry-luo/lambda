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

typedef enum JubeFuncFlags {
    JUBE_FN_NONE = 0,
    JUBE_FN_METHOD_ELIGIBLE = 1u << 0,
    JUBE_FN_VARARGS = 1u << 1,
} JubeFuncFlags;

struct JubeTypeDef {
    const char* name;
    uint32_t flags;
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
