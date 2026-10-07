#pragma once

#include "../lambda-data.hpp"
#include "../runtime/side_stack.h"
#include "../../lib/rdb_abi.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JUBE_ABI_VERSION 8
#define JUBE_ABI_VERSION_LEGACY 1
#define JUBE_HOST_API_VERSION 5

// The loader resolves this entry by name from each Jube DLL/DSO; release
// builds compile modules with hidden default visibility (D7.3.6).
#ifdef _WIN32
#define JUBE_MODULE_EXPORT __declspec(dllexport)
#else
#define JUBE_MODULE_EXPORT __attribute__((visibility("default")))
#endif


typedef struct JubeHostAPI JubeHostAPI;
typedef struct JubeTypeDef JubeTypeDef;
typedef struct JubeFuncDef JubeFuncDef;
typedef struct JubeNamespaceDef JubeNamespaceDef;
typedef struct JubeModuleDef JubeModuleDef;
typedef struct JubeHostGcAPI JubeHostGcAPI;
typedef struct JubeRootFrame JubeRootFrame;
typedef struct JubeHostRootAPI JubeHostRootAPI;
typedef struct JubeHostTemplateAPI JubeHostTemplateAPI;
typedef struct JubeHostValueAPI JubeHostValueAPI;
typedef struct JubeHostScriptAPI JubeHostScriptAPI;
typedef struct JubeHostRealmAPI JubeHostRealmAPI;
typedef struct JubeHostDomCatalogAPI JubeHostDomCatalogAPI;
typedef struct JubeModuleRequirements JubeModuleRequirements;
typedef struct JubeGlobalDef JubeGlobalDef;
typedef struct JubeHostRuntimeAPI JubeHostRuntimeAPI;
typedef struct JubeHostNodeAPI JubeHostNodeAPI;
typedef struct JubeHostNodeErrorAPI JubeHostNodeErrorAPI;
typedef struct JubeHostAsyncAPI JubeHostAsyncAPI;
typedef struct JubeHostBinaryAPI JubeHostBinaryAPI;
typedef struct JubeHostStreamAPI JubeHostStreamAPI;
typedef struct JubeHostNetworkAPI JubeHostNetworkAPI;
typedef struct JubeHostNodeZlibAPI JubeHostNodeZlibAPI;
typedef struct JubeHostFilesystemAPI JubeHostFilesystemAPI;
typedef struct JubeBinaryView JubeBinaryView;

// Native work callbacks operate only on module-owned POD state.  They never
// receive an Item, a JS session, or a libuv handle; the host invokes completion
// on the owning JS thread after it has checked that the session remains live.
typedef void (*JubeAsyncWorkCallback)(void* user);
typedef void (*JubeAsyncCompletionCallback)(void* user, int status);
typedef void (*JubeAsyncDestroyCallback)(void* user);

// A hosted request names its immutable host descriptor without selecting an
// executable path by text. The host owns its rid and exact Item span.
enum JubeAsyncResourceKind {
    JUBE_ASYNC_RESOURCE_FILESYSTEM_REQUEST = 1,
};

// Hosted modules name only stable, host-neutral class identities.  The JS
// engine owns its concrete class tags and translates these at the boundary.
enum JubeScriptClass {
    JUBE_SCRIPT_CLASS_URL = 1,
    JUBE_SCRIPT_CLASS_URL_SEARCH_PARAMS = 2,
    JUBE_SCRIPT_CLASS_BLOB = 3,
    JUBE_SCRIPT_CLASS_EVENT_EMITTER = 4,
};

typedef enum JubeHostCapability {
    JUBE_HOST_CAP_NONE = 0,
    JUBE_HOST_CAP_GC_ROOTS = 1ull << 0,
    JUBE_HOST_CAP_NODE_RUNTIME = 1ull << 6,
    JUBE_HOST_CAP_TEMPLATE_SESSION = 1ull << 7,
} JubeHostCapability;

typedef enum JubeFuncFlags {
    JUBE_FN_NONE = 0,
    JUBE_FN_METHOD_ELIGIBLE = 1u << 0,
    JUBE_FN_VARARGS = 1u << 1,
} JubeFuncFlags;

// Modules classify only the JavaScript-observable shapes they consume. The
// host retains its richer internal TypeId representation.
typedef enum JubeValueKind {
    JUBE_VALUE_OTHER = 0,
    JUBE_VALUE_UNDEFINED = 1,
    JUBE_VALUE_NULL = 2,
    JUBE_VALUE_BOOLEAN = 3,
    JUBE_VALUE_NUMBER = 4,
    JUBE_VALUE_STRING = 5,
    JUBE_VALUE_ARRAY = 6,
    JUBE_VALUE_OBJECT = 7,
    JUBE_VALUE_FUNCTION = 8,
    JUBE_VALUE_SYMBOL = 9,
    JUBE_VALUE_BIGINT = 10,
} JubeValueKind;

typedef enum JubeTypeFlags {
    JUBE_TYPE_NONE = 0,
    JUBE_TYPE_NON_OWNING_HOST = 1u << 0,
    JUBE_TYPE_OWNING_NATIVE = 1u << 1,
    JUBE_TYPE_NATIVE_INDEXED = 1u << 2,   // index adapter belongs only to Lambda
    JUBE_TYPE_INDEXED_READONLY = 1u << 3, // getter-only WebIDL index definitions
    JUBE_TYPE_NATIVE_NAMED = 1u << 4,     // named adapter belongs only to Lambda
    JUBE_TYPE_JS_EXACT_NAMES = 1u << 5,   // JS uses js_name; Lambda keeps declared aliases
} JubeTypeFlags;

// Executable DOM host capabilities are selected when a member function is
// published. Property spellings remain metadata and never cross the call ABI
// as semantic selectors (D6.2.2v2).
typedef enum JubeDomElementOperation {
#define DOM_ELEMENT_OP(NAME, thunk) JUBE_DOM_##NAME,
#include "../dom/dom_element_ops.def"
} JubeDomElementOperation;

typedef enum JubeCarrierKind {
    JUBE_CARRIER_VMAP = 0,
    JUBE_CARRIER_VARRAY,
    JUBE_CARRIER_VELMT,
} JubeCarrierKind;

static inline TypeId jube_carrier_type_id(JubeCarrierKind carrier) {
    switch (carrier) {
    case JUBE_CARRIER_VARRAY: return LMD_TYPE_VARRAY;
    case JUBE_CARRIER_VELMT: return LMD_TYPE_VELMT;
    case JUBE_CARRIER_VMAP:
    default: return LMD_TYPE_VMAP;
    }
}

struct JubeTypeDef {
    const char* name;
    uint32_t flags;
    const void* carrier_ops;
    void (*destroy)(void* native);
    JubeCarrierKind carrier;
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

// Import specifiers and globals are intentionally independent: declaring a
// namespace must not silently publish an observable globalThis property.
struct JubeGlobalDef {
    const char* name;
    uint32_t flags;
    // The host invokes this only for an attached JS runtime.  A global builder
    // may use the token to select its session-local cache, but never stores it
    // beyond runtime_detach.
    Item (*build)(void* session);
};

// Declares registration-time host requirements.  This is deliberately a
// compact, additive contract: a loader rejects an incompatible descriptor
// before init can allocate module state or publish callbacks.
struct JubeModuleRequirements {
    uint32_t struct_size;
    uint32_t min_host_api_version;
    uint32_t min_host_api_size;
    uint32_t reserved;
    uint64_t required_host_capabilities;
    // Optional additive Node contract. A record ending at the v1 prefix asks
    // only for the generic host requirements above.
    uint32_t min_node_api_version;
    uint32_t min_node_api_size;
    // Node modules also consume these generic tables.  Keep their minimum
    // layouts explicit so a descriptor cannot enter init with a truncated
    // value, script, or session-root service.
    uint32_t min_value_api_size;
    uint32_t min_script_api_size;
    uint32_t min_root_api_size;
};

#define JUBE_MODULE_REQUIREMENTS_V1_SIZE offsetof(JubeModuleRequirements, min_node_api_version)

// DOM3: binding-table halves of the module interface declaration.
// Shape (names, types, purity, arity, defaults) lives in Lambda type syntax in
// JubeModuleDef.interface_decl; behavior lives here as handler pointers.
// Handlers return 1 when handled, 0 to fall through. D8.4.3 routes abrupt
// completion as the returned ERROR Item; no ambient exception state crosses
// the module boundary.
typedef enum JubeMemberFlags {
    JUBE_MEMBER_NONE = 0,
    JUBE_MEMBER_NON_ENUMERABLE = 1u << 0,  // excluded from own-key enumeration
                                           //   (aliases like baseNode/extentNode)
    JUBE_MEMBER_PROTOTYPE = 1u << 1,      // inherited WebIDL accessor/method
    JUBE_MEMBER_HAS_REQUIRED_ARGS = 1u << 2,
    JUBE_MEMBER_KEYED_ACCESSOR = 1u << 3, // reflect_attr is the fixed named-adapter key
} JubeMemberFlags;

// JS length and missing-argument validation can differ from the Lambda arity.
#define JUBE_MEMBER_REQUIRED_ARGS(count) (JUBE_MEMBER_HAS_REQUIRED_ARGS | ((uint32_t)(count) << 8))

typedef struct JubeMemberBind {
    const char* name;         // snake_case; must match a declared interface member
    const char* js_name;      // optional camelCase override for irregular names
                              //   (innerHTML, namespaceURI, ...); NULL = derived
    int (*get)(Item receiver, Item* out);
    int (*set)(Item receiver, Item value, Item* out);            // absent = readonly
    int (*call)(Item receiver, Item* args, int argc, Item* out); // methods
    const char* reflect_attr; // reflected attribute, or fixed KEYED_ACCESSOR key
                              //   handles get/set; no handler functions needed
    uint32_t flags;           // JubeMemberFlags
    // DS13: the catalog row itself, so a member call lands on the operation
    // with no adapter in between. When `row_index` is set it supersedes `call`:
    // the dispatcher pads or truncates the argument list once and invokes the
    // row at `row_argc`. `row_argc` counts the receiver, matching the catalog's
    // own arity (a 0-argument method is row_argc 1).
    uint16_t row_index;       // JubeDomRowIndex; 0 = not a row
    uint8_t row_argc;
} JubeMemberBind;

typedef struct JubeTypeBinding {
    const char* type_name;    // matches `type X { ... }` in interface_decl
    const JubeTypeDef* host_brand;  // the JubeTypeDef used as vmap->host_type
    const JubeMemberBind* members;
    int32_t member_count;
    // open-name catch-alls (WebIDL named/indexed getters); any may be NULL
    int (*named_get)(Item receiver, Item key, Item* out);
    int (*named_set)(Item receiver, Item key, Item value, Item* out);
    int (*indexed_get)(Item receiver, int64_t index, Item* out);
    int (*indexed_set)(Item receiver, int64_t index, Item value, Item* out);
    // optional existing prototype object for this type (e.g. the engine's
    // Range.prototype); when set, jube_type_prototype adopts it instead of
    // creating a fresh object so constructor/instanceof identity is preserved
    Item (*prototype_seed)(void);
    // open-name membership for `in`/has: answers whether a non-declared name
    // exists (e.g. a CSS property name on a style object) without running the
    // named getter; NULL = named names are not part of `has`
    int (*named_has)(Item receiver, Item key, Item* out);
    // object-operation hooks for large WebIDL surfaces whose descriptor,
    // own-key, delete, and prototype semantics are receiver-specific.
    // reuse the retired object-call slot for explicit VMap indexed bounds.
    int64_t (*indexed_length)(Item receiver);
    int (*object_has)(Item receiver, Item key, Item* out);
    int (*object_delete)(Item receiver, Item key, Item* out);
    int (*object_descriptor)(Item receiver, Item key, Item* out);
    int (*object_own_keys)(Item receiver, Item* out);
    int (*object_prototype)(Item receiver, Item* out);
} JubeTypeBinding;

#ifdef __cplusplus
static_assert(sizeof(((JubeTypeBinding*)0)->indexed_length) == sizeof(void*),
    "indexed bounds must preserve the retired binding slot ABI");
#endif

typedef void (*JubeGcWeakClearFn)(uint64_t* slot, void* context);

// Guest code may reserve this object on its native stack, but its storage is
// private to the host.  That prevents hosted languages from depending on
// LambdaRootFrame's runtime pointer and side-stack watermark layout.
struct JubeRootFrame {
    uintptr_t storage[8];
};

struct JubeHostGcAPI {
    void (*register_root)(uint64_t* slot);
    void (*unregister_root)(uint64_t* slot);
    bool (*root_frame_begin)(LambdaRootFrame* frame, size_t slot_count);
    uint64_t* (*root_frame_take_slot)(LambdaRootFrame* frame);
    void (*root_frame_end)(LambdaRootFrame* frame);
    // Preserve the released v2 root-frame offsets; new GC capabilities are an
    // additive tail so existing rooting-capable modules keep their ABI.
    void (*register_weak)(uint64_t* slot, JubeGcWeakClearFn on_clear, void* context);
    void (*unregister_weak)(uint64_t* slot);
};

// This independently versioned table replaces the legacy JubeHostGcAPI root
// signatures for hosted languages.  It is an additive hosted-language tail so
// existing generic modules retain their unchanged GC table ABI.
struct JubeHostRootAPI {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*root_frame_begin)(JubeRootFrame* frame, size_t slot_count);
    uint64_t* (*root_frame_take_slot)(JubeRootFrame* frame);
    void (*root_frame_end)(JubeRootFrame* frame);
    // Persistent slots belong to a guest runtime, not to a stack frame.  The
    // session token prevents a module from registering roots in another run.
    int (*persistent_root_register)(void* session, uint64_t* slot);
    // Removal is likewise session-bound so a module cannot unregister another
    // runtime's root while a heap is active.
    int (*persistent_root_unregister)(void* session, uint64_t* slot);
};

// A hosted module's fixed cache values share one session-bound persistent-root
// protocol.  The values retain their native all-zero absence sentinel.
typedef struct JubePersistentValueSlots {
    void* session;
    const JubeHostRootAPI* roots;
    Item* values;
    int value_count;
    int registered_count;
} JubePersistentValueSlots;

static inline void jube_persistent_value_slots_reset(JubePersistentValueSlots* slots) {
    if (!slots || !slots->values) return;
    for (int i = 0; i < slots->value_count; i++) slots->values[i] = (Item){0};
}

static inline bool jube_persistent_value_slots_attached(const JubePersistentValueSlots* slots) {
    return slots && slots->session && slots->roots && slots->values && slots->value_count > 0 &&
        slots->registered_count == slots->value_count;
}

static inline int jube_persistent_value_slots_attach(JubePersistentValueSlots* slots,
        void* session, const JubeHostRootAPI* roots, Item* values, int value_count) {
    if (!slots || !session || !roots || !values || value_count <= 0 ||
            !roots->persistent_root_register || !roots->persistent_root_unregister) return -1;
    if (jube_persistent_value_slots_attached(slots)) {
        return slots->session == session && slots->roots == roots && slots->values == values &&
            slots->value_count == value_count ? 0 : -1;
    }
    if (slots->session || slots->roots || slots->values || slots->value_count ||
            slots->registered_count) return -1;
    slots->session = session;
    slots->roots = roots;
    slots->values = values;
    slots->value_count = value_count;
    jube_persistent_value_slots_reset(slots);
    for (; slots->registered_count < value_count; slots->registered_count++) {
        if (roots->persistent_root_register(session,
                &values[slots->registered_count].item) == 0) continue;
        while (slots->registered_count > 0) {
            slots->registered_count--;
            roots->persistent_root_unregister(session, &values[slots->registered_count].item);
        }
        jube_persistent_value_slots_reset(slots);
        slots->session = NULL;
        slots->roots = NULL;
        slots->values = NULL;
        slots->value_count = 0;
        return -1;
    }
    return 0;
}

static inline void jube_persistent_value_slots_detach(JubePersistentValueSlots* slots) {
    if (!slots) return;
    if (slots->session && slots->roots && slots->roots->persistent_root_unregister && slots->values) {
        while (slots->registered_count > 0) {
            slots->registered_count--;
            slots->roots->persistent_root_unregister(slots->session,
                &slots->values[slots->registered_count].item);
        }
    }
    jube_persistent_value_slots_reset(slots);
    slots->session = NULL;
    slots->roots = NULL;
    slots->values = NULL;
    slots->value_count = 0;
    slots->registered_count = 0;
}

// Host shutdown can release a session root registry before module shutdown.
// In that order, clear stale module bookkeeping without a late host callback.
static inline void jube_persistent_value_slots_forget(JubePersistentValueSlots* slots) {
    if (!slots) return;
    jube_persistent_value_slots_reset(slots);
    slots->session = NULL;
    slots->roots = NULL;
    slots->values = NULL;
    slots->value_count = 0;
    slots->registered_count = 0;
}

// Plain descriptors are copied into the mounted Lambda heap before dispatch.
// A guest must not pass JS Items as template payloads or retain borrowed
// Lambda Items after the synchronous emit callback returns (D7.4.2v2).
typedef enum JubeTemplateValueKind {
    JUBE_TEMPLATE_NULL = 0,
    JUBE_TEMPLATE_BOOL,
    JUBE_TEMPLATE_INT,
    JUBE_TEMPLATE_STRING,
    JUBE_TEMPLATE_BINARY,
    JUBE_TEMPLATE_MAP,
    JUBE_TEMPLATE_ARRAY,
} JubeTemplateValueKind;

typedef struct JubeTemplateValue JubeTemplateValue;
typedef struct JubeTemplateField {
    const char* name;
    const JubeTemplateValue* value;
} JubeTemplateField;

struct JubeTemplateValue {
    JubeTemplateValueKind kind;
    const char* bytes;
    size_t byte_length;
    int64_t integer;
    bool boolean;
    const JubeTemplateField* fields;
    size_t field_count;
    const JubeTemplateValue* elements;
    size_t element_count;
};

typedef struct JubeTemplateTarget {
    // NULL selects the root. Otherwise select a named template-state child,
    // falling back to a model attribute before that child has been replaced.
    const char* state_name;
    const char* fallback_attr;
    bool edit_mode;
} JubeTemplateTarget;

typedef Item (*JubeTemplateEmitCallback)(void* user, void* template_session,
                                         Item event_name, Item event_data);
typedef int (*JubeTemplateRenderCallback)(void* user, void* template_session,
                                          Item snapshot);

struct JubeHostTemplateAPI {
    uint32_t api_version;
    uint32_t struct_size;
    void* (*open)(const char* source, const char* reference);
    void (*close)(void* template_session);
    int (*dispatch)(void* template_session, const JubeTemplateTarget* target,
                    const char* event_name, const JubeTemplateValue* event_data,
                    JubeTemplateEmitCallback emit, void* user);
    // The callback may route an emitted Item without converting it to JS.
    // This entry is valid only while dispatch's emit callback is active.
    int (*dispatch_item)(void* template_session, const JubeTemplateTarget* target,
                         const char* event_name, Item event_data,
                         JubeTemplateEmitCallback emit, void* user);
    Item (*attribute)(Item value, const char* name);
    bool (*string_copy)(Item value, char* out, size_t capacity,
                        size_t* out_length);
    int (*render_json)(void* template_session, char** out_bytes,
                       size_t* out_length);
    void (*bytes_release)(char* bytes);
    // Borrowed snapshot is live only during this callback. It can be copied
    // or passed back to dispatch_item before returning.
    int (*render_item)(void* template_session, JubeTemplateRenderCallback callback,
                       void* user);
    Item (*child_at)(Item value, int64_t index);
};

struct JubeHostValueAPI {
    Item (*vmap_new)(void);
    Item (*new_object)(void);
    Item (*array_new)(int length);  // length, not capacity: the array comes back with `length` nulls
    Item (*array_push)(Item array, Item value);
    int64_t (*array_length)(Item array);
    Item (*array_get)(Item array, int64_t index);
    Item (*array_set)(Item array, int64_t index, Item value);
    Item (*property_get)(Item object, Item key);
    Item (*property_set)(Item object, Item key, Item value);
    // Defines an own data property while bypassing accessor dispatch.  This
    // preserves null-prototype dictionary keys such as "__proto__".
    Item (*property_set_own)(Item object, Item key, Item value);
    bool (*property_has_own)(Item object, Item key);
    bool (*property_get_own_data)(Item object, Item key, Item* out_value);
    bool (*is_array)(Item value);
    int (*kind)(Item value);
    bool (*string_copy)(Item value, char* out, size_t out_size, size_t* out_length);
    Item (*string_from_utf8_n)(const char* text, size_t length);
    size_t (*string_length)(Item value);
    // Borrowed UTF-8 storage remains valid until the caller releases or
    // overwrites its rooted Item. This avoids exposing String layout to a
    // binary/codec module that needs read-only input bytes.
    const uint8_t* (*string_bytes)(Item value);
    // Preserves integer-vs-float behavior without exposing tagged values.
    bool (*number_to_int64_exact)(Item value, int64_t* out_value);
    // Creates and unwraps a branded native object without publishing the
    // carrier layout. The type must be a module-declared JubeTypeDef; its
    // destroy hook owns the payload only when JUBE_TYPE_OWNING_NATIVE is set.
    // Creating a registered type activates its owning module before publication.
    Item (*native_object_new)(const JubeTypeDef* type, void* payload);
    void* (*native_object_data)(Item object, const JubeTypeDef* type);
};

typedef Item (*JubeNativeP0)(void);
typedef Item (*JubeNativeP1)(Item);
typedef Item (*JubeNativeP2)(Item, Item);
typedef Item (*JubeNativeP3)(Item, Item, Item);
typedef Item (*JubeNativeP4)(Item, Item, Item, Item);
typedef Item (*JubeNativeP5)(Item, Item, Item, Item, Item);
typedef Item (*JubeNativeP6)(Item, Item, Item, Item, Item, Item);
typedef Item (*JubeNativeP7)(Item, Item, Item, Item, Item, Item, Item);
typedef Item (*JubeNativeP8)(Item, Item, Item, Item, Item, Item, Item, Item);

typedef union JubeNativeTarget {
    JubeNativeP0 p0;
    JubeNativeP1 p1;
    JubeNativeP2 p2;
    JubeNativeP3 p3;
    JubeNativeP4 p4;
    JubeNativeP5 p5;
    JubeNativeP6 p6;
    JubeNativeP7 p7;
    JubeNativeP8 p8;
} JubeNativeTarget;

typedef struct JubeNativeFunctionSpec {
    JubeNativeTarget target;
    int8_t target_arity;
    int8_t adapter_arity;
    uint8_t constructable;
    uint8_t reserved;
} JubeNativeFunctionSpec;

struct JubeHostScriptAPI {
    Item (*new_function)(JubeNativeFunctionSpec spec);
    void (*function_set_prototype)(Item fn_item, Item proto);
    void (*set_function_name)(Item fn_item, Item name_item);
    void (*mark_non_enumerable)(Item object, Item name);
    Item (*global_this)(void);
    Item (*global_property)(Item key);
    Item (*new_error_with_name)(Item error_name, Item message);
    Item (*throw_value)(Item error);
    Item (*reflect_own_keys)(Item obj);
    Item (*object_keys)(Item obj);
    Item (*reflect_delete_property)(Item obj, Item key);
    Item (*call_function)(Item func_item, Item this_val, Item* args, int arg_count);
    bool (*is_truthy)(Item value);
    Item (*intrinsic_prototype_for_class)(int class_id);
    Item (*make_number)(double value);
    double (*get_number)(Item value);
    Item (*date_new_from)(Item value);
    Item (*date_method)(Item date, int method_id);
    int (*class_id)(Item value);
    Item (*to_string)(Item value);
    Item (*throw_type_error_code)(const char* code, const char* message);
    // Creates a JavaScript object with the supplied prototype.  Node leaves
    // need Object.create(null) semantics without naming JS object layouts.
    Item (*object_create)(Item prototype);
    Item (*throw_uri_error_code)(const char* code, const char* message);
    // Converts a routed ERROR Item into the JS value visible at a catch,
    // rejection, or host boundary without consulting ambient state.
    Item (*error_lane_payload)(Item lane);
    void (*mark_non_writable)(Item object, Item key);
    Item (*object_freeze)(Item object);
    Item (*current_this)(void);
    Item (*strict_equal)(Item left, Item right);
    // Host factories select immutable JS metadata before publication; callers
    // must not reclassify an already-published object.
    Item (*new_object_with_class)(int class_id);
    bool (*class_is)(Item object, int class_id);
    // Preserves the engine's observable typeof distinction when a compact
    // host value category intentionally groups multiple internal variants.
    Item (*type_of)(Item value);
    // Closure environments remain host-owned GC objects; a module may fill
    // the returned slots only before handing them to new_closure().
    Item* (*closure_env_new)(int count);
    Item (*new_closure)(JubeNativeFunctionSpec spec, Item* env, int env_size);
    Item (*get_prototype)(Item object);
    void (*set_prototype)(Item object, Item prototype);
    Item (*promise_with_resolvers)(void);
    // Parses decimal text into the engine's BigInt representation without
    // exposing its allocation layout to hosted Node compatibility modules.
    Item (*bigint_from_decimal)(const char* text, size_t length);
    // Narrows only exact in-range BigInts; modules must not infer overflow
    // from the legacy clamping extractor used inside the engine.
    bool (*bigint_to_int64_exact)(Item value, int64_t* out_value);
};

// ---------------------------------------------------------------------------
// The DOM operation catalog as an ABI section (ES39/ES40).
//
// One row per operation in lambda/dom/dom_api.def is expanded here into a slot
// and, in jube_registry.cpp, into the matching body -- so the table cannot
// drift from the catalog: adding a row adds a slot and a body together, and
// the compiler checks the arity of each body against its row.
//
// Every operation has the shape Item f(Item x argc), which is what makes one
// declaration serve every surface. A slot is null when the row's body is
// engine-provided (DOM_F_ENGINE, filled at table time) or not yet written.
// ---------------------------------------------------------------------------
typedef Item (*JubeDomFn0)(void);
typedef Item (*JubeDomFn1)(Item);
typedef Item (*JubeDomFn2)(Item, Item);
typedef Item (*JubeDomFn3)(Item, Item, Item);
typedef Item (*JubeDomFn4)(Item, Item, Item, Item);
typedef Item (*JubeDomFn5)(Item, Item, Item, Item, Item);

typedef enum JubeDomTokenListOperation {
    JUBE_DOM_TOKEN_LIST_ADD = 1,
    JUBE_DOM_TOKEN_LIST_REMOVE,
    JUBE_DOM_TOKEN_LIST_TOGGLE,
    JUBE_DOM_TOKEN_LIST_CONTAINS,
    JUBE_DOM_TOKEN_LIST_ITEM,
    JUBE_DOM_TOKEN_LIST_REPLACE,
    JUBE_DOM_TOKEN_LIST_TO_STRING,
    JUBE_DOM_TOKEN_LIST_ITERATOR,
    JUBE_DOM_TOKEN_LIST_SUPPORTS,
} JubeDomTokenListOperation;

// DS13: the catalog rows again, this time as a flat index space, so a module's
// *static* member table can name a row without linking the host symbol. The
// order is dom_api.def's order -- the same order as the struct below and as the
// slot array the registry builds from it.
typedef enum JubeDomRowIndex {
    JUBE_DOM_ROW_NONE = 0,
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) \
    JUBE_DOM_ROW_##name,
#define DOM_RAW(name, cluster, ret, params, body, flags)
#include "../dom/dom_api.def"
#undef DOM_OP
#undef DOM_RAW
    JUBE_DOM_ROW_COUNT
} JubeDomRowIndex;

struct JubeHostDomCatalogAPI {
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) \
    JubeDomFn##argc name;
#define DOM_RAW(name, cluster, ret, params, body, flags) \
    ret (*name)params;
#include "../dom/dom_api.def"
#undef DOM_OP
#undef DOM_RAW
    // One slot per row, in row order -- nothing is written by hand here.
};


// ES41 (Lambda_Design_DOM_Host_API.md): the JS *shape* of the DOM -- the
// document proxy, prototype values, expandos, live collections, wrapper
// initialisation, JS exceptions and Event construction -- is its own host
// section. `->dom_catalog` is the DOM; a guest that is not JavaScript never touches
// `->realm`.
struct JubeHostRealmAPI {
    Item (*get_document_object_value)(void);
    Item (*document_proxy_for_doc_bridge)(void* doc);
    Item (*document_proxy_get_property)(Item prop_name);
    Item (*document_proxy_set_property)(Item prop_name, Item value);
    Item (*dom_get_prototype_value)(Item obj);
    Item (*range_get_prototype_value)(void);
    Item (*selection_get_prototype_value)(void);
    bool (*expando_has_property)(Item obj, Item key);
    Item (*expando_get_own_property_descriptor)(Item obj, Item key);
    Item (*expando_delete_property)(Item obj, Item key);
    Item (*expando_own_property_names)(Item obj);
    Item (*live_child_collection_bridge)(void* elem, bool elements_only);
    Item (*attribute_collection_bridge)(void* elem);
    Item (*token_list_operation)(Item receiver, int operation, Item* args, int argc);
    Item (*live_document_forms_bridge)(void* doc);
    Item (*live_form_elements_bridge)(void* elem);
    Item (*live_document_get_elements_by_tag_name_bridge)(void* doc, Item query);
    Item (*live_document_get_elements_by_class_name_bridge)(void* doc, Item query);
    Item (*live_document_get_elements_by_name_bridge)(void* doc, Item query);
    Item (*live_element_get_elements_by_tag_name_bridge)(void* elem, Item query);
    Item (*live_element_get_elements_by_class_name_bridge)(void* elem, Item query);
    void (*initialize_node_wrapper)(void* dom_elem);
    Item (*throw_contenteditable_syntax_error)(void);
    Item (*get_selection_function_for_document)(void* doc);
    Item (*document_default_view_bridge)(void* doc);
    Item (*document_create_event_bridge)(Item interface_name);
};

// Node modules compose namespaces through this narrow session boundary; they
// never import JavaScript runtime implementation symbols directly.
struct JubeHostRuntimeAPI {
    uint32_t api_version;
    uint32_t struct_size;
    void* (*current_session)(void);
    bool (*session_is_live)(void* session);
    Item (*session_global_this)(void* session);
    Item (*session_process)(void* session);
    Item (*current_this)(void* session);
    int (*resolve_namespace)(void* session, const char* specifier, Item* out_namespace);
    int (*resolve_host_namespace)(void* session, const char* specifier, Item* out_namespace);
    // Session-owned resource inventory stays in the host table; node-core
    // only publishes these Node-visible methods.
    Item (*session_active_resources_info)(void* session);
    Item (*session_active_handles)(void* session);
};

struct JubeHostNodeErrorAPI {
    uint32_t api_version;
    uint32_t struct_size;
    Item (*throw_type_error_code)(void* session, const char* code, const char* message);
    Item (*throw_range_error_code)(void* session, const char* code, const char* message);
    // Preserves Node zlib's observable `code` and numeric `errno` error shape
    // while keeping the host's Error object layout opaque to native modules.
    Item (*throw_zlib_error)(void* session, const char* method, int status);
    // Preserves Node's code/errno/syscall error object shape for a failed
    // platform syscall without leaking JS Error allocation to a module.
    Item (*throw_system_error)(void* session, const char* syscall, int error_number);
    // Creates the existing Node coded Error form for compatibility leaves
    // whose error category is not a platform errno.
    Item (*throw_error_code)(void* session, const char* code, const char* message);
    // Builds Node's code/errno/syscall network error from a host libuv status;
    // a module never imports libuv merely to reproduce this observable shape.
    Item (*throw_network_error)(void* session, int status, const char* syscall,
                                const char* address, int port);
};

// This is intentionally the first Node async slice: it proves session-owned
// work/cancellation without exposing libuv or an event-loop pointer.  Stream,
// rid, timer, and completion-post services append in later compatible tails.
struct JubeHostAsyncAPI {
    uint32_t api_version;
    uint32_t struct_size;
    int (*work_submit)(void* session, JubeAsyncWorkCallback work,
                       JubeAsyncCompletionCallback complete,
                       JubeAsyncDestroyCallback destroy, void* user,
                       uint32_t* out_request_id);
    int (*work_cancel)(void* session, uint32_t request_id);
    // Timer helpers keep event-loop ownership in the host while allowing the
    // node-core promises facade to avoid direct JS-runtime symbol imports.
    Item (*timer_set_timeout_promise)(Item delay, Item value, Item options);
    Item (*timer_set_immediate_promise)(Item value, Item options);
    Item (*timer_set_interval)(Item callback, Item delay);
    Item (*timer_scheduler_wait)(Item delay, Item options);
    Item (*timer_scheduler_yield)(void);
    Item (*timer_set_timeout)(Item callback, Item delay);
    void (*timer_clear_timeout)(Item timer);
    void (*timer_clear_interval)(Item timer);
    Item (*timer_set_immediate)(Item callback);
    void (*timer_install_promisify_custom)(Item function);
    // Installs a custom promisify implementation without exposing the
    // host-owned well-known Symbol to a compatibility module.
    void (*function_install_promisify_custom)(Item function,
                                               JubeNativeFunctionSpec spec);
    // Publishes callback result field names under the same host-owned Symbol.
    void (*function_install_promisify_args)(Item function, const char* first, const char* second);
    // Posts Node's conventional (err, result) callback shape through the
    // host-owned next-tick queue; the module never retains callback values.
    void (*next_tick_callback)(void* session, Item callback, Item error, Item result);
    // A native request has one context-table rid and one exact Item span.
    // The host releases the row only after libuv has returned ownership, so a
    // module keeps only POD state and the rid in its request tail.
    int (*work_submit_root_span)(void* session, int resource_kind,
                                 const Item* root_values, int root_count,
                                 JubeAsyncWorkCallback work,
                                 JubeAsyncCompletionCallback complete,
                                 JubeAsyncDestroyCallback destroy, void* user,
                                 uint32_t* out_resource_id);
    Item (*work_resource_value)(void* session, uint32_t resource_id,
                                int root_index);
};

// Binary values stay host-owned.  The returned byte pointer is borrowed for
// the duration of the native call and modules must root the source Item before
// any operation that can allocate or re-enter JS.
struct JubeHostBinaryAPI {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*is_typed_array)(Item value);
    uint8_t* (*typed_array_data)(Item value);
    int (*typed_array_length)(Item value);
    // Copies native bytes into a host-owned Buffer value.  The input remains
    // owned by the caller and is not retained after this call returns.  A
    // NULL pointer is valid when length is zero.
    Item (*buffer_from_bytes)(const uint8_t* data, int length);
    // Additive Buffer construction tail.  These preserve typed-array storage
    // ownership in the host while node-core owns Buffer's JS namespace.
    Item (*buffer_alloc)(int length);
    uint8_t* (*buffer_prepare_write)(Item value);
    bool (*is_buffer)(Item value);
    // Exposes ArrayBuffer, typed-array, and DataView byte windows without
    // publishing host object layouts.  The caller roots value before the
    // call because creating a missing backing-buffer wrapper may allocate.
    int (*describe_view)(Item value, JubeBinaryView* out_view);
    // Creates a Buffer view over a validated ArrayBuffer byte range.  The
    // returned Buffer shares backing storage; callers retain no raw host
    // pointers after this call.
    Item (*buffer_view)(Item array_buffer, int byte_offset, int byte_length);
};

enum JubeBinaryViewStatus {
    JUBE_BINARY_VIEW_NOT_VIEW = 0,
    JUBE_BINARY_VIEW_OK = 1,
    JUBE_BINARY_VIEW_DETACHED = 2,
    JUBE_BINARY_VIEW_OUT_OF_BOUNDS = 3,
};

struct JubeBinaryView {
    Item array_buffer;
    int byte_offset;
    int byte_length;
};

// EventEmitter is the first dependency hub to cross the boundary. These
// operations preserve host-owned async-local, domain, and process-warning
// semantics without exposing their runtime state or JS implementation calls.
struct JubeHostNodeEventsAPI {
    uint32_t api_version;
    uint32_t struct_size;
    Item (*als_capture_context)(void);
    Item (*als_context_call)(Item context, Item callback, Item this_val,
                             Item arg1, int64_t has_arg);
    int (*domain_emit_current_error)(Item error);
    Item (*process_emit_warning)(Item warning, Item type_item, Item code_item);
    bool (*is_error_like)(Item value);
    // Callback-style Node services must restore their captured domain before
    // entering guest code; the domain stack itself remains runtime-owned.
    Item (*domain_current)(void);
    Item (*domain_call)(Item domain, Item callback, Item this_val,
                        Item* args, int arg_count);
};

// Permission policy is host-owned so a module cannot infer or bypass its
// configured filesystem allowlist by reaching into the JS runtime directly.
struct JubeHostNodePermissionAPI {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*has_fs_read)(const char* path);
    bool (*has_fs_write)(const char* path);
    // The checks retain Node's existing coded error construction and throw on
    // denial. A callback-style client captures that exception before posting.
    Item (*check_fs_read)(const char* path);
    Item (*check_fs_write)(const char* path);
    // Node-core owns the process.permission object; policy evaluation and
    // grant mutation stay behind this host table.
    bool (*enabled)(void);
    Item (*process_permission_has)(Item scope, Item resource);
    Item (*process_permission_drop)(Item scope, Item resource);
};

// Worker/message-port objects remain host-owned because their transfer and
// queue invariants are part of the JS runtime. Node modules receive only this
// narrow constructor/operation surface and never their layouts.
struct JubeHostWorkerAPI {
    uint32_t api_version;
    uint32_t struct_size;
    Item (*message_channel_new)(void);
    Item (*message_port_new)(void);
    Item (*message_port_move_to_context)(Item port, Item context);
    Item (*receive_message_on_port)(Item port);
    Item (*mark_as_untransferable)(Item value);
    Item (*is_marked_as_untransferable)(Item value);
};

// Node's generic stream implementation stays host-owned until its dedicated
// extraction. Filesystem modules use these factories without importing stream
// classes, EventEmitter internals, or JS runtime symbols.
struct JubeHostStreamAPI {
    uint32_t api_version;
    uint32_t struct_size;
    Item (*file_read_stream_new)(Item path, Item options);
    Item (*file_write_stream_new)(Item path, Item options);
    // TCP handles are host-owned opaque resources. A Node module may retain
    // only the returned rid; no uv handle or loop pointer crosses this ABI.
    int (*tcp_create)(void* session, Item owner, uint32_t* out_resource_id);
    // Bind/address/fd adoption stay POD-only so the module owns the
    // BoundSocket object while the host remains the platform socket owner.
    int (*tcp_bind)(void* session, uint32_t resource_id, const char* address,
                    int port, bool ipv6_only, bool reuse_port);
    int (*tcp_address)(void* session, uint32_t resource_id,
                       char* address, size_t address_size, int* out_port,
                       int* out_family);
    int (*tcp_fd)(void* session, uint32_t resource_id, int* out_fd);
    int (*tcp_adopt_fd)(void* session, uint32_t resource_id, int* out_fd);
    int (*resource_close)(void* session, uint32_t resource_id);
    int (*resource_ref)(void* session, uint32_t resource_id, bool referenced);
    bool (*resource_is_live)(void* session, uint32_t resource_id);
    // zlib transforms reuse the host's generic Transform lifecycle while the
    // module owns the Node-facing codec callbacks.
    Item (*transform_new)(Item options);
    Item (*transform_prototype)(void);
    Item (*readable_push)(Item stream, Item chunk);
    void (*flush_data_if_flowing)(Item stream);
    void (*transform_flush_drained)(Item stream);
};

// Network policy and raw resolver operations stay host-owned, while node-net
// owns public Node setters/getters. The module never observes libuv handles.
struct JubeHostNetworkAPI {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*default_auto_select_family_get)(void);
    void (*default_auto_select_family_set)(bool enabled);
    int (*default_auto_select_family_timeout_get)(void);
    bool (*default_auto_select_family_timeout_set)(int timeout_ms);
    // Network policy stays host-owned. DNS modules ask explicitly so a
    // resolver cannot bypass the process permission boundary by using libc.
    bool (*permission_has_net)(void);
    Item (*permission_make_net_error)(void* session, const char* syscall,
                                      const char* resource);
    // Platform resolver implementation remains host-owned; modules receive
    // only plain strings and never import resolver or socket APIs themselves.
    int (*ip_family)(const char* address);
    bool (*lookup_sync)(const char* hostname, char* address, size_t address_size);
};

// Zlib stays statically linked with the host. Node modules receive only this
// byte-oriented provider and cannot create a second codec dependency in a DSO.
enum JubeNodeZlibCodecMode {
    JUBE_NODE_ZLIB_GZIP,
    JUBE_NODE_ZLIB_GUNZIP,
    JUBE_NODE_ZLIB_DEFLATE,
    JUBE_NODE_ZLIB_INFLATE,
    JUBE_NODE_ZLIB_DEFLATE_RAW,
    JUBE_NODE_ZLIB_INFLATE_RAW,
    JUBE_NODE_ZLIB_UNZIP,
};

struct JubeNodeZlibResult {
    uint8_t* data;
    int length;
    int status;
};

struct JubeHostNodeZlibAPI {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t (*crc32)(const uint8_t* data, int length, uint32_t seed);
    bool (*codec)(enum JubeNodeZlibCodecMode mode, const uint8_t* data, int length,
                  JubeNodeZlibResult* out_result);
    void (*result_release)(JubeNodeZlibResult* result);
    // stateful transforms remain host-owned so the node-zlib DSO does not
    // import libz; the module supplies only validated options and byte views.
    bool (*stream_init)(enum JubeNodeZlibCodecMode mode, int window_bits, int level,
                        int mem_level, int strategy, void** out_state, int* out_status);
    bool (*stream_run)(void* state, const uint8_t* data, int length, int flush,
                       JubeNodeZlibResult* out_result);
    void (*stream_free)(void* state);
};

// The host owns filesystem implementation and platform descriptors. Node
// modules keep only Node argument/error/callback semantics around these ops.
enum JubeNodeFilesystemMode {
    JUBE_NODE_FILESYSTEM_READ,
    JUBE_NODE_FILESYSTEM_WRITE,
    JUBE_NODE_FILESYSTEM_APPEND,
};

struct JubeNodeFilesystemReadWrite {
    enum JubeNodeFilesystemMode mode;
    const char* path;
    const uint8_t* input;
    size_t input_length;
    uint8_t* output;
    size_t output_length;
    int error_number;
    const char* error_syscall;
};

struct JubeNodeFilesystemCopy {
    const char* source_path;
    const char* destination_path;
    int error_number;
    const char* error_syscall;
};

enum JubeNodeFilesystemPathMode {
    JUBE_NODE_FILESYSTEM_PATH_ACCESS,
    JUBE_NODE_FILESYSTEM_PATH_CHMOD,
    JUBE_NODE_FILESYSTEM_PATH_UNLINK,
    JUBE_NODE_FILESYSTEM_PATH_RMDIR,
    JUBE_NODE_FILESYSTEM_PATH_RENAME,
    JUBE_NODE_FILESYSTEM_PATH_MKDIR,
    JUBE_NODE_FILESYSTEM_PATH_TRUNCATE,
    JUBE_NODE_FILESYSTEM_PATH_RM,
    JUBE_NODE_FILESYSTEM_PATH_LINK,
    JUBE_NODE_FILESYSTEM_PATH_SYMLINK,
    JUBE_NODE_FILESYSTEM_PATH_CHOWN,
    JUBE_NODE_FILESYSTEM_PATH_LCHOWN,
    JUBE_NODE_FILESYSTEM_PATH_LCHMOD,
};

// Simple path operations deliberately carry only POD. The module owns Node's
// argument and permission semantics; the host owns platform syscalls.
struct JubeNodeFilesystemPathOperation {
    enum JubeNodeFilesystemPathMode mode;
    const char* path;
    const char* secondary_path;
    int64_t numeric_value;
    int64_t secondary_numeric_value;
    bool recursive;
    int error_number;
    const char* error_syscall;
};

enum JubeNodeFilesystemStringMode {
    JUBE_NODE_FILESYSTEM_STRING_REALPATH,
    JUBE_NODE_FILESYSTEM_STRING_MKDTEMP,
    JUBE_NODE_FILESYSTEM_STRING_READLINK,
};

struct JubeNodeFilesystemStringOperation {
    enum JubeNodeFilesystemStringMode mode;
    const char* path;
    char* output;
    size_t output_length;
    int error_number;
    const char* error_syscall;
};

struct JubeNodeFilesystemDirectoryOperation {
    const char* path;
    char** entries;
    size_t entry_count;
    int error_number;
    const char* error_syscall;
};

enum JubeNodeFilesystemDescriptorMode {
    JUBE_NODE_FILESYSTEM_DESCRIPTOR_OPEN,
    JUBE_NODE_FILESYSTEM_DESCRIPTOR_CLOSE,
    JUBE_NODE_FILESYSTEM_DESCRIPTOR_FCHMOD,
    JUBE_NODE_FILESYSTEM_DESCRIPTOR_READ,
    JUBE_NODE_FILESYSTEM_DESCRIPTOR_WRITE,
    JUBE_NODE_FILESYSTEM_DESCRIPTOR_FCHOWN,
};

struct JubeNodeFilesystemDescriptorOperation {
    enum JubeNodeFilesystemDescriptorMode mode;
    const char* path;
    int descriptor;
    int flags;
    int mode_value;
    int secondary_mode_value;
    uint8_t* bytes;
    size_t byte_length;
    int64_t position;
    size_t transferred;
    int error_number;
    const char* error_syscall;
};

struct JubeNodeFilesystemMetadata {
    uint64_t mode;
    uint64_t size;
    uint64_t ino;
    uint64_t nlink;
    uint64_t dev;
    uint64_t uid;
    uint64_t gid;
    uint64_t rdev;
    uint64_t blksize;
    uint64_t blocks;
    int64_t atime_millis;
    int64_t mtime_millis;
    int64_t ctime_millis;
    int64_t birthtime_millis;
};

enum JubeNodeFilesystemMetadataMode {
    JUBE_NODE_FILESYSTEM_METADATA_STAT,
    JUBE_NODE_FILESYSTEM_METADATA_LSTAT,
    JUBE_NODE_FILESYSTEM_METADATA_FSTAT,
};

struct JubeNodeFilesystemMetadataOperation {
    enum JubeNodeFilesystemMetadataMode mode;
    const char* path;
    int descriptor;
    JubeNodeFilesystemMetadata value;
    int error_number;
    const char* error_syscall;
};

struct JubeNodeFilesystemStatfsOperation {
    const char* path;
    uint64_t type;
    uint64_t bsize;
    uint64_t frsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    int error_number;
    const char* error_syscall;
};

struct JubeHostFilesystemAPI {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*read_write)(JubeNodeFilesystemReadWrite* operation);
    void (*read_write_release)(JubeNodeFilesystemReadWrite* operation);
    // Appended so older filesystem consumers retain their released prefix.
    bool (*copy_file)(JubeNodeFilesystemCopy* operation);
    bool (*path_operation)(JubeNodeFilesystemPathOperation* operation);
    bool (*string_operation)(JubeNodeFilesystemStringOperation* operation);
    void (*string_operation_release)(JubeNodeFilesystemStringOperation* operation);
    bool (*directory_read)(JubeNodeFilesystemDirectoryOperation* operation);
    void (*directory_read_release)(JubeNodeFilesystemDirectoryOperation* operation);
    bool (*descriptor_operation)(JubeNodeFilesystemDescriptorOperation* operation);
    bool (*metadata_operation)(JubeNodeFilesystemMetadataOperation* operation);
    bool (*statfs_operation)(JubeNodeFilesystemStatfsOperation* operation);
};

struct JubeHostNodeAPI {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t capabilities;
    const JubeHostRuntimeAPI* runtime;
    const JubeHostRootAPI* roots;
    const JubeHostNodeErrorAPI* error;
    // Appended after the original Node v1 prefix; consumers size-gate it.
    const JubeHostAsyncAPI* async_ops;
    const JubeHostBinaryAPI* binary;
    const JubeHostNodeEventsAPI* events;
    const JubeHostWorkerAPI* workers;
    // Additive permission tail consumed by extracted filesystem services.
    const JubeHostNodePermissionAPI* permission;
    const JubeHostStreamAPI* streams;
    // Additive network-policy tail consumed by node-net.
    const JubeHostNetworkAPI* network;
    // Additive static-host codec provider; its node-zlib consumer was removed
    // 2026-10-07, and the slot stays so the table layout is unchanged.
    const JubeHostNodeZlibAPI* zlib;
    // Additive static-host filesystem provider consumed by node-fs.
    const JubeHostFilesystemAPI* filesystem;
};

struct JubeHostAPI {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t capabilities;
    const JubeHostGcAPI* gc;
    const JubeHostValueAPI* value;
    const JubeHostScriptAPI* script;
    const JubeHostRealmAPI* realm;   // ES41: the JS shape of the DOM (v2)
    const JubeHostDomCatalogAPI* dom_catalog;  // ES40: the catalog, core + derived (v2)
    // Node-only services are absent from minimal/non-Node hosts.
    const JubeHostNodeAPI* node;
    // Additive host-neutral retained-template service (size-gated).
    const JubeHostTemplateAPI* templates;
};

#define JUBE_HOST_SERVICE_API_VERSION 7
#define JUBE_HOST_ROOT_API_V1_SIZE offsetof(JubeHostRootAPI, persistent_root_register)
#define JUBE_HOST_ROOT_API_H5_PERSISTENT_SIZE sizeof(JubeHostRootAPI)

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

    // -- DOM3 additive tail --
    // JubeModuleDef is always passed by pointer (never embedded in arrays), so
    // appending fields is ABI-safe: the registry gates access on struct_size and
    // accepts v1 modules whose struct_size stops at JUBE_MODULE_DEF_V1_SIZE.
    const char* interface_decl;            // Lambda-type-syntax module interface
    const JubeTypeBinding* type_bindings;  // one per declared type
    int32_t type_binding_count;
    void (*runtime_reset)(void);            // drop JS heap-backed module caches

    // Optional cleanup for values rooted in one Lambda heap. Called while the
    // heap is active, immediately before that runtime destroys it.
    void (*heap_cleanup)(void* heap);


    // Optional registration-time capability/host-shape contract.  Keep this
    // tail-appended so descriptors compiled against earlier Jube headers stay
    // loadable through struct_size gating.
    const JubeModuleRequirements* requirements;

    // The JS host reserves explicit global names during global setup, but the
    // builder runs only when the owning module is first activated. A namespace
    // descriptor never acts as an implicit global declaration.
    const JubeGlobalDef* globals;
    int32_t global_count;

    // Runtime hooks receive an opaque per-JS-runtime session token. The host
    // calls attach lazily, after globalThis and process exist and before the
    // module's first use, then reset/detach while that heap is still live.
    // Modules must release every heap-backed cache during reset or detach and
    // must never retain a detached token.
    void (*runtime_attach)(void* session);
    void (*runtime_reset_session)(void* session);
    void (*runtime_detach)(void* session);

    // Mirrors the manifest dependency edges for statically linked profiles,
    // where no manifest loader exists to establish activation order.
    const char* const* dependencies;
    int32_t dependency_count;

    // Host-subsystem providers (RDB2/RDB3): RDB driver tables this module
    // registers behind lib/rdb.h. The manifest's `rdb:<name>` provides must
    // name exactly these drivers (D7.3.4: the descriptor is ground truth).
    const RdbDriver* const* rdb_drivers;
    int32_t rdb_driver_count;
};

// Size of the frozen v1 layout: everything before the DOM3 additive tail.
#define JUBE_MODULE_DEF_V1_SIZE offsetof(JubeModuleDef, interface_decl)

#ifdef __cplusplus
}

#define JUBE_DEFINE_NATIVE_SPEC(arity, type, member) \
    static inline JubeNativeFunctionSpec jube_native_function_spec( \
            type target, int adapter_arity, bool constructable = false) { \
        JubeNativeFunctionSpec spec = {}; \
        spec.target.member = target; \
        spec.target_arity = arity; \
        spec.adapter_arity = (int8_t)adapter_arity; \
        spec.constructable = constructable ? 1 : 0; \
        return spec; \
    } \
    static inline Item jube_new_function(const JubeHostScriptAPI* api, \
            type target, int adapter_arity) { \
        return api->new_function(jube_native_function_spec( \
            target, adapter_arity)); \
    } \
    static inline Item jube_new_constructor(const JubeHostScriptAPI* api, \
            type target, int adapter_arity) { \
        return api->new_function(jube_native_function_spec( \
            target, adapter_arity, true)); \
    } \
    static inline Item jube_new_closure(const JubeHostScriptAPI* api, \
            type target, int adapter_arity, Item* env, int env_size) { \
        return api->new_closure(jube_native_function_spec( \
            target, adapter_arity), env, env_size); \
    }

JUBE_DEFINE_NATIVE_SPEC(0, JubeNativeP0, p0)
JUBE_DEFINE_NATIVE_SPEC(1, JubeNativeP1, p1)
JUBE_DEFINE_NATIVE_SPEC(2, JubeNativeP2, p2)
JUBE_DEFINE_NATIVE_SPEC(3, JubeNativeP3, p3)
JUBE_DEFINE_NATIVE_SPEC(4, JubeNativeP4, p4)
JUBE_DEFINE_NATIVE_SPEC(5, JubeNativeP5, p5)
JUBE_DEFINE_NATIVE_SPEC(6, JubeNativeP6, p6)
JUBE_DEFINE_NATIVE_SPEC(7, JubeNativeP7, p7)
JUBE_DEFINE_NATIVE_SPEC(8, JubeNativeP8, p8)

#undef JUBE_DEFINE_NATIVE_SPEC
#endif
