// ts_runtime.cpp — runtime surface retained after direct TypeScript lowering.

#include "ts_runtime.h"
#include "../lambda-data.hpp"
#include "../js/js_runtime.h"

static const char* ts_runtime_type_name(TypeId type) {
    switch (type) {
    case LMD_TYPE_NULL: return "null";
    case LMD_TYPE_UNDEFINED: return "undefined";
    case LMD_TYPE_BOOL: return "boolean";
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT: return "number";
    case LMD_TYPE_STRING: return "string";
    case LMD_TYPE_SYMBOL: return "symbol";
    case LMD_TYPE_FUNC: return "function";
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM: return "array";
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT: return "object";
    default: return get_type_name(type);
    }
}

extern "C" Item ts_type_info(Item value) {
    // The direct parser stores TS annotations as compile-time Lambda Type*
    // facts; no TS wrapper can reach a JavaScript value at this boundary.
    return (Item){.item = s2it(heap_create_name(
        ts_runtime_type_name(get_type_id(value))))};
}
