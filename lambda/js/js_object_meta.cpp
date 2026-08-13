// Tune6 immutable JavaScript object metadata.

#include "js_object_meta.h"
#include "../lambda.hpp"

extern "C" bool js_promise_vmap_is(Item value);

// The array order is the frozen JsClass order. Metadata is static data: it has
// no Items, realm prototypes, mutable caches, or module-owned callbacks.
#define JS_META(id) { id, JS_CLASS_FAMILY_ORDINARY, JS_CLASS_FLAG_INTRINSIC_PROTO, \
                     JS_PROTO_POLICY_INTRINSIC, NULL }
#define JS_META_F(id, family, flags, policy) \
    { id, family, flags, policy, NULL }
static const JsClassMeta js_class_meta_table[JS_CLASS__COUNT] = {
    JS_META(JS_CLASS_NONE), JS_META(JS_CLASS_OBJECT),
    JS_META_F(JS_CLASS_FUNCTION, JS_CLASS_FAMILY_FUNCTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META(JS_CLASS_BOOLEAN), JS_META(JS_CLASS_NUMBER),
    JS_META_F(JS_CLASS_STRING, JS_CLASS_FAMILY_STRING,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META(JS_CLASS_SYMBOL), JS_META(JS_CLASS_BIGINT),
    JS_META_F(JS_CLASS_ARRAY, JS_CLASS_FAMILY_ARRAY, JS_CLASS_FLAG_INTRINSIC_PROTO,
              JS_PROTO_POLICY_INTRINSIC),
    JS_META(JS_CLASS_DATE),
    { JS_CLASS_REGEXP, JS_CLASS_FAMILY_ORDINARY, JS_CLASS_FLAG_NATIVE_PAYLOAD,
              JS_PROTO_POLICY_INTRINSIC, NULL },
    JS_META_F(JS_CLASS_ERROR, JS_CLASS_FAMILY_ERROR,
              JS_CLASS_FLAG_INTRINSIC_PROTO | JS_CLASS_FLAG_ERROR_CARRIER,
              JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_AGGREGATE_ERROR, JS_CLASS_FAMILY_ERROR,
              JS_CLASS_FLAG_INTRINSIC_PROTO | JS_CLASS_FLAG_ERROR_CARRIER,
              JS_PROTO_POLICY_INTRINSIC),
    JS_META(JS_CLASS_PROMISE),
    { JS_CLASS_TYPED_ARRAY, JS_CLASS_FAMILY_TYPED_ARRAY,
              JS_CLASS_FLAG_INTRINSIC_PROTO | JS_CLASS_FLAG_NATIVE_PAYLOAD,
              JS_PROTO_POLICY_TYPED_ARRAY, &js_typed_array_property_ops },
    { JS_CLASS_ARRAY_BUFFER, JS_CLASS_FAMILY_TYPED_ARRAY,
              JS_CLASS_FLAG_INTRINSIC_PROTO | JS_CLASS_FLAG_NATIVE_PAYLOAD,
              JS_PROTO_POLICY_INTRINSIC, &js_typed_array_property_ops },
    { JS_CLASS_DATA_VIEW, JS_CLASS_FAMILY_TYPED_ARRAY,
              JS_CLASS_FLAG_INTRINSIC_PROTO | JS_CLASS_FLAG_NATIVE_PAYLOAD,
              JS_PROTO_POLICY_INTRINSIC, &js_typed_array_property_ops },
    JS_META(JS_CLASS_READABLE_STREAM),
    JS_META(JS_CLASS_WRITABLE_STREAM), JS_META(JS_CLASS_STRING_DECODER),
    JS_META(JS_CLASS_TEXT_DECODER), JS_META(JS_CLASS_TEXT_ENCODER),
    JS_META(JS_CLASS_EVENT), JS_META(JS_CLASS_CUSTOM_EVENT),
    JS_META(JS_CLASS_EVENT_TARGET), JS_META(JS_CLASS_EVENT_EMITTER),
    JS_META(JS_CLASS_DOM_EXCEPTION), JS_META(JS_CLASS_ABORT_CONTROLLER),
    JS_META(JS_CLASS_ABORT_SIGNAL), JS_META(JS_CLASS_ABORT_ERROR),
    JS_META(JS_CLASS_MESSAGE_CHANNEL), JS_META(JS_CLASS_MESSAGE_PORT),
    JS_META(JS_CLASS_URL), JS_META(JS_CLASS_URL_SEARCH_PARAMS),
    JS_META(JS_CLASS_FORM_DATA), JS_META(JS_CLASS_BLOB), JS_META(JS_CLASS_FILE),
    JS_META(JS_CLASS_DATA_TRANSFER), JS_META(JS_CLASS_AGENT),
    JS_META(JS_CLASS_CLIENT_REQUEST), JS_META(JS_CLASS_INCOMING_MESSAGE),
    JS_META(JS_CLASS_SERVER), JS_META(JS_CLASS_SERVER_RESPONSE),
    JS_META(JS_CLASS_SOCKET), JS_META(JS_CLASS_SECURE_CONTEXT),
    JS_META(JS_CLASS_TLS_SERVER), JS_META(JS_CLASS_TLS_SOCKET),
    JS_META(JS_CLASS_RANGE), JS_META(JS_CLASS_SELECTION),
    JS_META(JS_CLASS_CANVAS_RENDERING_CONTEXT_2D),
    JS_META(JS_CLASS_OFFSCREEN_CANVAS), JS_META(JS_CLASS_CSS_NESTED_DECLARATIONS),
    JS_META(JS_CLASS_TIMEOUT),
    JS_META_F(JS_CLASS_MAP, JS_CLASS_FAMILY_COLLECTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_SET, JS_CLASS_FAMILY_COLLECTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_WEAK_MAP, JS_CLASS_FAMILY_COLLECTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_WEAK_SET, JS_CLASS_FAMILY_COLLECTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_WEAK_REF, JS_CLASS_FAMILY_COLLECTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_MAP_ITERATOR, JS_CLASS_FAMILY_ITERATOR,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_SET_ITERATOR, JS_CLASS_FAMILY_ITERATOR,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_GENERATOR, JS_CLASS_FAMILY_ITERATOR,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_GENERATOR_FUNCTION, JS_CLASS_FAMILY_FUNCTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_ASYNC_FUNCTION, JS_CLASS_FAMILY_FUNCTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META_F(JS_CLASS_ARGUMENTS, JS_CLASS_FAMILY_ARGUMENTS,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META(JS_CLASS_CLIPBOARD_ITEM),
    JS_META(JS_CLASS_RAW_JSON),
    JS_META_F(JS_CLASS_FINALIZATION_REGISTRY, JS_CLASS_FAMILY_COLLECTION,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC),
    JS_META(JS_CLASS_TYPE_ERROR), JS_META(JS_CLASS_RANGE_ERROR),
    JS_META(JS_CLASS_SYNTAX_ERROR), JS_META(JS_CLASS_REFERENCE_ERROR),
    JS_META(JS_CLASS_URI_ERROR), JS_META(JS_CLASS_EVAL_ERROR),
    { JS_CLASS_SHARED_ARRAY_BUFFER, JS_CLASS_FAMILY_TYPED_ARRAY,
              JS_CLASS_FLAG_INTRINSIC_PROTO | JS_CLASS_FLAG_NATIVE_PAYLOAD,
              JS_PROTO_POLICY_INTRINSIC, &js_typed_array_property_ops },
    JS_META(JS_CLASS_UI_EVENT),
    JS_META(JS_CLASS_MOUSE_EVENT), JS_META(JS_CLASS_KEYBOARD_EVENT),
    JS_META(JS_CLASS_FOCUS_EVENT), JS_META(JS_CLASS_WHEEL_EVENT),
    JS_META(JS_CLASS_COMPOSITION_EVENT), JS_META(JS_CLASS_INPUT_EVENT),
    JS_META(JS_CLASS_POINTER_EVENT), JS_META(JS_CLASS_STATIC_RANGE),
    JS_META(JS_CLASS_CLIPBOARD_EVENT), JS_META(JS_CLASS_PERMISSION_STATUS),
    JS_META(JS_CLASS_CLIPBOARD), JS_META(JS_CLASS_READABLE),
    JS_META(JS_CLASS_WRITABLE), JS_META(JS_CLASS_DUPLEX),
    JS_META(JS_CLASS_TRANSFORM), JS_META(JS_CLASS_PASS_THROUGH),
    JS_META(JS_CLASS_IMMEDIATE), JS_META(JS_CLASS_TRANSITION_EVENT),
    JS_META(JS_CLASS_ANIMATION_EVENT), JS_META(JS_CLASS_FILE_LIST),
    { JS_CLASS_PROXY, JS_CLASS_FAMILY_PROXY,
              JS_CLASS_FLAG_EXOTIC_PROPERTIES | JS_CLASS_FLAG_NATIVE_PAYLOAD,
              JS_PROTO_POLICY_EXOTIC, &js_proxy_property_ops },
    { JS_CLASS_STRING_ITERATOR, JS_CLASS_FAMILY_ITERATOR,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC,
              &js_iterator_property_ops },
    { JS_CLASS_TYPED_ARRAY_ITERATOR, JS_CLASS_FAMILY_ITERATOR,
              JS_CLASS_FLAG_INTRINSIC_PROTO, JS_PROTO_POLICY_INTRINSIC,
              &js_iterator_property_ops },
    { JS_CLASS_PROCESS_ENV, JS_CLASS_FAMILY_ORDINARY,
              JS_CLASS_FLAG_NONE, JS_PROTO_POLICY_OBJECT,
              &js_process_env_property_ops },
    JS_META(JS_CLASS_CSS_NAMESPACE), JS_META(JS_CLASS_CSSOM),
    { JS_CLASS_WEB_API_RESOURCE, JS_CLASS_FAMILY_HOST,
              JS_CLASS_FLAG_EXOTIC_PROPERTIES, JS_PROTO_POLICY_HOST,
              &js_host_property_ops },
};
#undef JS_META
#undef JS_META_F

static TypeMap js_error_carrier_type = {};

// Promise instances use the VMap carrier, while Promise.prototype remains an
// ordinary shape-backed Map. Keep the exotic callbacks on the carrier-only
// metadata view so prototype bootstrap writes are not intercepted as payload
// operations (D7.4.1v2).
static const JsClassMeta js_promise_vmap_meta = {
    JS_CLASS_PROMISE, JS_CLASS_FAMILY_ORDINARY,
    JS_CLASS_FLAG_INTRINSIC_PROTO | JS_CLASS_FLAG_NATIVE_PAYLOAD,
    JS_PROTO_POLICY_INTRINSIC, &js_promise_property_ops,
};

extern "C" TypeMap* js_error_carrier_type_map(void) {
    js_object_metadata_initialize();
    if (!js_error_carrier_type.js_meta) {
        js_error_carrier_type.type_id = LMD_TYPE_MAP;
        js_error_carrier_type.js_meta = js_class_meta_for_id(JS_CLASS_ERROR);
    }
    return &js_error_carrier_type;
}

const JsClassMeta* js_class_meta_for_id(JsClassId id) {
    if (id >= JS_CLASS__COUNT) id = JS_CLASS_NONE;
    return &js_class_meta_table[id];
}

void js_object_metadata_initialize(void) {
    // EmptyMap is the shared zero-shape blueprint used by JS allocation and
    // the explicit Input boundary. Giving it ordinary metadata makes a newly
    // published object classifiable without changing map_put's shape sentinel.
    extern TypeMap EmptyMap;
    if (!EmptyMap.js_meta) EmptyMap.js_meta = js_class_meta_for_id(JS_CLASS_OBJECT);
}

JsClassId js_class_id_from_meta(const JsClassMeta* meta) {
    return meta ? meta->id : JS_CLASS_NONE;
}

const JsClassMeta* js_object_meta(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_ERROR) {
        return js_class_meta_for_id(js_error_class_id(value));
    }
    if (type == LMD_TYPE_VMAP && js_promise_vmap_is(value)) {
        return &js_promise_vmap_meta;
    }
    if (type == LMD_TYPE_VMAP && value.vmap && value.vmap->host_type) {
        return js_class_meta_for_id(JS_CLASS_WEB_API_RESOURCE);
    }
    if (type == LMD_TYPE_MAP && value.map &&
            value.map->type == js_error_carrier_type_map()) {
        // resting LambdaError values share the Map lane; the typed carrier is
        // the semantic owner, so do not let its physical prefix classify it.
        return js_class_meta_for_id(js_error_class_id(value));
    }
    if (type == LMD_TYPE_MAP && value.map && value.map->type) {
        TypeMap* tm = (TypeMap*)value.map->type;
#ifndef NDEBUG
        assert(typemap_ptr_is_plausible(tm));
#endif
        if (tm->js_meta) return tm->js_meta;
    }
    if (type == LMD_TYPE_ARRAY_NUM && js_is_ordinary_numeric_array(value))
        return js_class_meta_for_id(JS_CLASS_ARRAY);
    if (type == LMD_TYPE_ARRAY && value.array && js_array_has_props(value.array)) {
        Map* props = js_array_props(value.array);
        if (props && props->type) {
            TypeMap* tm = (TypeMap*)props->type;
#ifndef NDEBUG
            assert(typemap_ptr_is_plausible(tm));
#endif
            if (tm->js_meta) return tm->js_meta;
        }
        return js_class_meta_for_id(JS_CLASS_ARRAY);
    }
    if (type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM)
        return js_class_meta_for_id(JS_CLASS_ARRAY);
    if (type == LMD_TYPE_FUNC)
        return js_class_meta_for_id(JS_CLASS_FUNCTION);
    return js_class_meta_for_id(JS_CLASS_NONE);
}

bool js_object_has_class(Item value, JsClassId id) {
    return js_class_id_from_meta(js_object_meta(value)) == id;
}

bool js_object_uses_ordinary_shape(Item value) {
    const JsClassMeta* meta = js_object_meta(value);
    if (!meta) return true;
    JsClassId id = meta->id;
    // Intrinsic wrappers still use ordinary named-property storage, but their
    // prototype chain is observable. Only unbranded maps may take the bare-map
    // fast path that intentionally omits Object.prototype (D3.4.7).
    return id == JS_CLASS_NONE || id == JS_CLASS_OBJECT;
}

bool js_object_uses_default_object_to_primitive(Item value) {
    JsClassId id = js_class_id_from_meta(js_object_meta(value));
    return id == JS_CLASS_CSS_NAMESPACE || id == JS_CLASS_CSSOM ||
           id == JS_CLASS_WEB_API_RESOURCE;
}
