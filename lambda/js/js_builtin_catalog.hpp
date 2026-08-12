#pragma once

// Shared builtin metadata contract used by registration, MIR lowering, and dispatch.

#include "js_runtime.h"
#include "js_class.h"
#include "js_typed_array.h"

enum JsConstructorId {
    JS_CTOR_OBJECT = 1,
    JS_CTOR_ARRAY,
    JS_CTOR_FUNCTION,
    JS_CTOR_STRING,
    JS_CTOR_NUMBER,
    JS_CTOR_BOOLEAN,
    JS_CTOR_SYMBOL,
    JS_CTOR_BIGINT,
    JS_CTOR_ERROR,
    JS_CTOR_TYPE_ERROR,
    JS_CTOR_RANGE_ERROR,
    JS_CTOR_REFERENCE_ERROR,
    JS_CTOR_SYNTAX_ERROR,
    JS_CTOR_URI_ERROR,
    JS_CTOR_EVAL_ERROR,
    JS_CTOR_REGEXP,
    JS_CTOR_DATE,
    JS_CTOR_PROMISE,
    JS_CTOR_MAP,
    JS_CTOR_SET,
    JS_CTOR_WEAKMAP,
    JS_CTOR_WEAKSET,
    JS_CTOR_WEAKREF,
    JS_CTOR_FINALIZATION_REGISTRY,
    JS_CTOR_ARRAY_BUFFER,
    JS_CTOR_SHARED_ARRAY_BUFFER,
    JS_CTOR_DATAVIEW,
    JS_CTOR_INT8ARRAY,
    JS_CTOR_UINT8ARRAY,
    JS_CTOR_UINT8CLAMPEDARRAY,
    JS_CTOR_INT16ARRAY,
    JS_CTOR_UINT16ARRAY,
    JS_CTOR_INT32ARRAY,
    JS_CTOR_UINT32ARRAY,
    JS_CTOR_FLOAT16ARRAY,
    JS_CTOR_FLOAT32ARRAY,
    JS_CTOR_FLOAT64ARRAY,
    JS_CTOR_BIGINT64ARRAY,
    JS_CTOR_BIGUINT64ARRAY,
    JS_CTOR_AGGREGATE_ERROR,
    JS_CTOR_PROXY,
    JS_CTOR_EVENT,
    JS_CTOR_CUSTOM_EVENT,
    JS_CTOR_EVENT_TARGET,
    JS_CTOR_UI_EVENT,
    JS_CTOR_FOCUS_EVENT,
    JS_CTOR_MOUSE_EVENT,
    JS_CTOR_WHEEL_EVENT,
    JS_CTOR_KEYBOARD_EVENT,
    JS_CTOR_COMPOSITION_EVENT,
    JS_CTOR_INPUT_EVENT,
    JS_CTOR_POINTER_EVENT,
    JS_CTOR_STATIC_RANGE,
    JS_CTOR_TIMEOUT,
    JS_CTOR_IMMEDIATE,
    JS_CTOR_TRANSITION_EVENT,
    JS_CTOR_ANIMATION_EVENT,
    JS_CTOR_MAX
};

enum JsBuiltinOwner {
    JS_BUILTIN_OWNER_NONE = 0,
#define JS_BUILTIN_OWNER(owner) owner,
#define JS_BUILTIN_ID(id, call_body, mir_kind)
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
    JS_BUILTIN_OWNER_MAX
};

enum JsBuiltinMirLoweringKind {
    JS_BUILTIN_MIR_GENERIC = 0,
    JS_BUILTIN_MIR_MATH,
    JS_BUILTIN_MIR_DATE
};

enum JsBuiltinPropertyKind {
    JS_BUILTIN_PROPERTY_METHOD = 0,
    JS_BUILTIN_PROPERTY_ACCESSOR
};

enum JsBuiltinId {
    JS_BUILTIN_NONE = 0,
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, call_body, mir_kind) id,
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind) id,
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
    JS_BUILTIN_MAX
};

enum JsBuiltinGlobalKind {
    JS_BUILTIN_GLOBAL_NAMESPACE = 0,
    JS_BUILTIN_GLOBAL_FUNCTION,
    JS_BUILTIN_GLOBAL_CONSTRUCTOR
};

#define JS_BUILTIN_GLOBAL_INSTALL 1
#define JS_BUILTIN_GLOBAL_NON_ENUMERABLE 2
#define JS_BUILTIN_GLOBAL_CUSTOM_GC 4
#define JS_BUILTIN_GLOBAL_TIMER_PROMISIFY 8
#define JS_BUILTIN_GLOBAL_MIR_DIRECT 16
#define JS_BUILTIN_GLOBAL_ERROR_CLASS 32
#define JS_BUILTIN_GLOBAL_TYPED_ARRAY 64
#define JS_BUILTIN_GLOBAL_REALM_INSTALL 128

enum JsBuiltinGlobalId {
    JS_BUILTIN_GLOBAL_NONE = 0,
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, call_body, mir_kind)
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags) id,
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
    JS_BUILTIN_GLOBAL_MAX
};

enum JsIntrinsicBindingAlias {
    JS_INTRINSIC_ALIAS_NONE = 0,
    JS_INTRINSIC_ALIAS_STRING_TRIM_START,
    JS_INTRINSIC_ALIAS_STRING_TRIM_END,
    JS_INTRINSIC_ALIAS_DATE_UTC_STRING,
    JS_INTRINSIC_ALIAS_ARRAY_ITERATOR,
    JS_INTRINSIC_ALIAS_MAP_ITERATOR,
    JS_INTRINSIC_ALIAS_SET_ITERATOR
};

enum {
    JS_INTRINSIC_BINDING_COUNT = 0
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, call_body, mir_kind)
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind)
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias) + 1
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
};

struct JsBuiltinMethodSpec {
    JsBuiltinOwner owner;
    const char* name;
    int len;
    int builtin_id;
    int param_count;
    const char* display_name;
    JsBuiltinPropertyKind property_kind;
    int flags;
    JsIntrinsicBindingAlias identity_alias;
};

struct JsIntrinsicTargetSpec {
    int catalog_id;
    JsNativeCallBody call_body;
    JsNativeConstructBody construct_body;
    JsBuiltinMirLoweringKind mir_kind;
};

// Target bodies are declared from the same catalog that stores their pointers;
// adding a direct target cannot silently omit its typed declaration.
#define JS_BUILTIN_OWNER(owner)
#define JS_BUILTIN_ID(id, call_body, mir_kind) \
    Item call_body(Item callee, Item this_value, Item* args, int argc, \
                   uint64_t* result_home);
#define JS_BUILTIN_CONSTRUCTOR_TARGET(id, call_body, construct_body, mir_kind) \
    Item call_body(Item callee, Item this_value, Item* args, int argc, \
                   uint64_t* result_home); \
    Item construct_body(Item callee, Item* args, int argc, Item new_target, \
                        uint64_t* result_home);
#define JS_BUILTIN_METHOD(owner, name, len, id, arity, display_name, property_kind, flags, identity_alias)
#define JS_BUILTIN_GLOBAL(id, name, len, kind, runtime_id, target_id, arity, flags)
#include "js_builtin_catalog.def"
#undef JS_BUILTIN_GLOBAL
#undef JS_BUILTIN_METHOD
#undef JS_BUILTIN_CONSTRUCTOR_TARGET
#undef JS_BUILTIN_ID
#undef JS_BUILTIN_OWNER
const JsIntrinsicTargetSpec* js_intrinsic_target_find(int catalog_id);

struct JsBuiltinGlobalSpec {
    JsBuiltinGlobalId id;
    const char* name;
    int len;
    JsBuiltinGlobalKind kind;
    int runtime_id;
    int target_id;
    int param_count;
    int flags;
};

const JsBuiltinMethodSpec* js_builtin_catalog_find(JsBuiltinOwner owner, const char* name, int len);
const JsBuiltinMethodSpec* js_builtin_catalog_find_id(int builtin_id);
int js_builtin_catalog_lookup_id(JsBuiltinOwner owner, const char* name, int len);
int js_builtin_catalog_lookup_constructor_id(const char* ctor_name, int ctor_len,
                                             const char* prop_name, int prop_len);
int js_builtin_catalog_lookup_member_id(const char* owner_name, int owner_len,
                                        const char* prop_name, int prop_len);
JsBuiltinMirLoweringKind js_builtin_mir_kind(int builtin_id);
const JsBuiltinGlobalSpec* js_builtin_global_find(const char* name, int len);
bool js_builtin_global_has_flag(const char* name, int len, int flag);
int js_builtin_global_count();
const JsBuiltinGlobalSpec* js_builtin_global_at(int index);
Item js_intrinsic_binding_get(JsBuiltinOwner owner, const char* name, int len);
void js_install_builtin_method_specs(Item object, JsBuiltinOwner owner);
void js_install_builtin_function_specs(Item object, JsBuiltinOwner owner);
void js_install_builtin_accessor_specs(Item object, JsBuiltinOwner owner);
void js_populate_builtin_prototype_methods(Item prototype, const char* ctor_name, int ctor_len);
void js_populate_dataview_prototype_methods(Item prototype);
