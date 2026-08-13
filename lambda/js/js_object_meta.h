// Tune6 object metadata and exotic-operation contract.
#ifndef LAMBDA_JS_OBJECT_META_H
#define LAMBDA_JS_OBJECT_META_H

#include "js_class.h"

typedef JsClass JsClassId;

typedef enum JsClassFamily : uint8_t {
    JS_CLASS_FAMILY_ORDINARY = 0,
    JS_CLASS_FAMILY_ARRAY,
    JS_CLASS_FAMILY_FUNCTION,
    JS_CLASS_FAMILY_ERROR,
    JS_CLASS_FAMILY_PROXY,
    JS_CLASS_FAMILY_TYPED_ARRAY,
    JS_CLASS_FAMILY_ARGUMENTS,
    JS_CLASS_FAMILY_STRING,
    JS_CLASS_FAMILY_ITERATOR,
    JS_CLASS_FAMILY_COLLECTION,
    JS_CLASS_FAMILY_HOST,
} JsClassFamily;

typedef enum JsClassFlags : uint16_t {
    JS_CLASS_FLAG_NONE = 0,
    JS_CLASS_FLAG_EXOTIC_PROPERTIES = 1u << 0,
    JS_CLASS_FLAG_INTRINSIC_PROTO = 1u << 1,
    JS_CLASS_FLAG_ERROR_CARRIER = 1u << 2,
    JS_CLASS_FLAG_NATIVE_PAYLOAD = 1u << 3,
} JsClassFlags;

typedef enum JsPrototypePolicy : uint8_t {
    JS_PROTO_POLICY_OBJECT = 0,
    JS_PROTO_POLICY_INTRINSIC,
    JS_PROTO_POLICY_TYPED_ARRAY,
    JS_PROTO_POLICY_EXOTIC,
    JS_PROTO_POLICY_HOST,
    JS_PROTO_POLICY_NULL,
} JsPrototypePolicy;

typedef enum JsPropertyOpDisposition : uint8_t {
    JS_PROPERTY_OP_FALLTHROUGH = 0,
    JS_PROPERTY_OP_COMPLETE = 1,
} JsPropertyOpDisposition;

typedef struct JsPropertyOpResult {
    JsPropertyOpDisposition disposition;
    Item completion;
} JsPropertyOpResult;

// Tune5's eight public operations keep their exact ABI in js_props.h. This
// table only selects an internal owner and an explicit completion lane.
typedef JsPropertyOpResult (*JsPropertyOpFn)(Item target, uint64_t lane,
                                              Item observable_key, Item value,
                                              Item receiver, Item descriptor);

typedef struct JsPropertyOps {
    JsPropertyOpFn get;
    JsPropertyOpFn set;
    JsPropertyOpFn define_own;
    JsPropertyOpFn delete_property;
    JsPropertyOpFn has_property;
    JsPropertyOpFn get_own_property_descriptor;
    JsPropertyOpFn own_keys;
    JsPropertyOpFn get_prototype_of;
    JsPropertyOpFn set_prototype_of;
    JsPropertyOpFn is_extensible;
    JsPropertyOpFn prevent_extensions;
} JsPropertyOps;

typedef struct JsClassMeta {
    JsClassId id;
    JsClassFamily family;
    uint16_t flags;
    JsPrototypePolicy prototype_policy;
    const JsPropertyOps* ops;
} JsClassMeta;

// Operation tables are static and immutable; their callbacks live with the
// runtime kernels so metadata selection does not create a second ABI.
extern const JsPropertyOps js_proxy_property_ops;
extern const JsPropertyOps js_typed_array_property_ops;
extern const JsPropertyOps js_iterator_property_ops;
extern const JsPropertyOps js_process_env_property_ops;
extern const JsPropertyOps js_host_property_ops;
extern const JsPropertyOps js_promise_property_ops;

#ifdef __cplusplus
extern "C" {
#endif

const JsClassMeta* js_class_meta_for_id(JsClassId id);
JsClassId js_class_id_from_meta(const JsClassMeta* meta);
void js_object_metadata_initialize(void);
const JsClassMeta* js_object_meta(Item value);
bool js_object_has_class(Item value, JsClassId id);
bool js_object_uses_ordinary_shape(Item value);
bool js_object_uses_default_object_to_primitive(Item value);
TypeMap* js_error_carrier_type_map(void);

#ifdef __cplusplus
}
#endif

#endif  // LAMBDA_JS_OBJECT_META_H
