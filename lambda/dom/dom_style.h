#pragma once

#include "dom_provider.hpp"
#include "../lambda-data.hpp"
#include "../input/css/css_style.hpp"

struct DomElement;
struct DomDocument;
typedef bool (*DomStyleUsedValueSync)(DomDocument* document);

// Native CSS providers borrow elements/names. Serialized strings use the caller's
// pool; computed values may borrow document storage and are consumed synchronously
// (D4.5.1v4 / D4.1.4v5, D7.1.3 / D7.5.3).
#define DOM_STYLE_ENGINE_OPERATIONS(OP) \
    OP(bool, ensure_computed, (DomElement* element, bool needs_used_value)) \
    OP(String*, serialize_computed_value, (Pool* pool, DomElement* element, CssPropertyCode property, int pseudo_type)) \
    OP(String*, serialize_custom_property, (Pool* pool, DomElement* element, const char* name, size_t name_length)) \
    OP(const CssValue*, compute_custom_property, (Pool* pool, DomElement* element, const char* name, size_t name_length, bool preserve_tokens)) \
    OP(void, capture_transition, (DomElement* element, CssPropertyCode property)) \
    OP(void, set_used_value_sync, (DomStyleUsedValueSync sync)) \
    OP(bool, focus_is_hidden, (DomElement* element))

struct DomStyleEngineOps {
#define DOM_STYLE_FIELD(result, name, parameters) result (*name) parameters;
    DOM_STYLE_ENGINE_OPERATIONS(DOM_STYLE_FIELD)
#undef DOM_STYLE_FIELD
};

// Complete immutable tables have process lifetime; conflicting publication fails.
extern "C" bool dom_style_register_engine(const DomStyleEngineOps* operations);
extern "C" const DomStyleEngineOps* dom_style_engine_ops(void);
