/**
 * JavaScript runtime built-in registry tables for Lambda.
 */
#include "js_runtime_internal.hpp"

// =============================================================================
// Built-in registry
// =============================================================================

// Built-in method function cache — keyed by builtin_id
static Item js_builtin_cache[JS_BUILTIN_MAX];
static bool js_builtin_cache_init = false;

extern "C" void js_func_init_property(Item fn_item, Item key, Item value);

static const char* js_builtin_method_spec_display_name(const JsBuiltinMethodSpec* spec) {
    return spec->display_name ? spec->display_name : spec->name;
}

static const JsBuiltinMethodSpec* js_find_builtin_method_spec(const JsBuiltinMethodSpec* specs, const char* name, int len) {
    if (!specs || !name) return NULL;
    for (int i = 0; specs[i].name; i++) {
        if (len == specs[i].len && strncmp(name, specs[i].name, len) == 0) {
            return &specs[i];
        }
    }
    return NULL;
}

Item js_lookup_builtin_method_spec(const JsBuiltinMethodSpec* specs, const char* name, int len) {
    const JsBuiltinMethodSpec* spec = js_find_builtin_method_spec(specs, name, len);
    if (spec) {
        return js_get_or_create_builtin(spec->builtin_id, js_builtin_method_spec_display_name(spec), spec->param_count);
    }
    return ItemNull;
}

void js_install_builtin_method_specs(Item object, const JsBuiltinMethodSpec* specs) {
    if (!specs) return;
    for (int i = 0; specs[i].name; i++) {
        Item key = (Item){.item = s2it(heap_create_name(specs[i].name, specs[i].len))};
        Item fn = js_get_or_create_builtin(specs[i].builtin_id, js_builtin_method_spec_display_name(&specs[i]), specs[i].param_count);
        js_property_set(object, key, fn);
        js_mark_non_enumerable(object, key);
    }
}

static void js_install_builtin_method_specs_on_function(Item function_item, const JsBuiltinMethodSpec* specs, bool skip_existing) {
    if (!specs || get_type_id(function_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)function_item.function;
    for (int i = 0; specs[i].name; i++) {
        Item key = (Item){.item = s2it(heap_create_name(specs[i].name, specs[i].len))};
        if (skip_existing && fn->properties_map.item != 0 &&
            get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            Item existing = map_get(fn->properties_map.map, key);
            if (existing.item != ItemNull.item) continue;
        }
        Item method = js_get_or_create_builtin(specs[i].builtin_id, js_builtin_method_spec_display_name(&specs[i]), specs[i].param_count);
        js_func_init_property(function_item, key, method);
        js_mark_non_enumerable(function_item, key);
    }
}

static Item js_create_builtin_function_from_spec(const JsBuiltinMethodSpec* spec, int flags, bool use_cache) {
    const char* display_name = js_builtin_method_spec_display_name(spec);
    if (use_cache && flags == 0 && spec->builtin_id > 0) {
        return js_get_or_create_builtin(spec->builtin_id, display_name, spec->param_count);
    }
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->param_count = spec->param_count;
    fn->formal_length = -1;
    fn->builtin_id = spec->builtin_id;
    fn->name = heap_create_name(display_name, strlen(display_name));
    fn->flags = flags;
    return (Item){.function = (Function*)fn};
}

void js_install_builtin_function_specs(Item object, const JsBuiltinMethodSpec* specs, int flags, bool use_cache) {
    if (!specs) return;
    for (int i = 0; specs[i].name; i++) {
        Item key = (Item){.item = s2it(heap_create_name(specs[i].name, specs[i].len))};
        Item fn = js_create_builtin_function_from_spec(&specs[i], flags, use_cache);
        js_property_set(object, key, fn);
        js_mark_non_enumerable(object, key);
    }
}

void js_install_builtin_accessor_specs(Item object, const JsBuiltinMethodSpec* specs, int flags) {
    if (!specs) return;
    for (int i = 0; specs[i].name; i++) {
        Item getter = js_create_builtin_function_from_spec(&specs[i], flags, false);
        Item prop_name = (Item){.item = s2it(heap_create_name(specs[i].name, specs[i].len))};
        js_install_native_accessor(object, prop_name, getter, ItemNull, JSPD_NON_ENUMERABLE);
    }
}

const JsBuiltinMethodSpec JS_MATH_METHOD_SPECS[] = {
    {"abs", 3, JS_BUILTIN_MATH_ABS, 1},
    {"floor", 5, JS_BUILTIN_MATH_FLOOR, 1},
    {"ceil", 4, JS_BUILTIN_MATH_CEIL, 1},
    {"round", 5, JS_BUILTIN_MATH_ROUND, 1},
    {"sqrt", 4, JS_BUILTIN_MATH_SQRT, 1},
    {"pow", 3, JS_BUILTIN_MATH_POW, 2},
    {"min", 3, JS_BUILTIN_MATH_MIN, 2},
    {"max", 3, JS_BUILTIN_MATH_MAX, 2},
    {"log", 3, JS_BUILTIN_MATH_LOG, 1},
    {"log10", 5, JS_BUILTIN_MATH_LOG10, 1},
    {"log2", 4, JS_BUILTIN_MATH_LOG2, 1},
    {"exp", 3, JS_BUILTIN_MATH_EXP, 1},
    {"sin", 3, JS_BUILTIN_MATH_SIN, 1},
    {"cos", 3, JS_BUILTIN_MATH_COS, 1},
    {"tan", 3, JS_BUILTIN_MATH_TAN, 1},
    {"sign", 4, JS_BUILTIN_MATH_SIGN, 1},
    {"trunc", 5, JS_BUILTIN_MATH_TRUNC, 1},
    {"random", 6, JS_BUILTIN_MATH_RANDOM, 0},
    {"asin", 4, JS_BUILTIN_MATH_ASIN, 1},
    {"acos", 4, JS_BUILTIN_MATH_ACOS, 1},
    {"atan", 4, JS_BUILTIN_MATH_ATAN, 1},
    {"atan2", 5, JS_BUILTIN_MATH_ATAN2, 2},
    {"cbrt", 4, JS_BUILTIN_MATH_CBR, 1},
    {"hypot", 5, JS_BUILTIN_MATH_HYPOT, 2},
    {"clz32", 5, JS_BUILTIN_MATH_CLZ32, 1},
    {"fround", 6, JS_BUILTIN_MATH_FROUND, 1},
    {"imul", 4, JS_BUILTIN_MATH_IMUL, 2},
    {"sinh", 4, JS_BUILTIN_MATH_SINH, 1},
    {"cosh", 4, JS_BUILTIN_MATH_COSH, 1},
    {"tanh", 4, JS_BUILTIN_MATH_TANH, 1},
    {"asinh", 5, JS_BUILTIN_MATH_ASINH, 1},
    {"acosh", 5, JS_BUILTIN_MATH_ACOSH, 1},
    {"atanh", 5, JS_BUILTIN_MATH_ATANH, 1},
    {"expm1", 5, JS_BUILTIN_MATH_EXPM1, 1},
    {"log1p", 5, JS_BUILTIN_MATH_LOG1P, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_OBJECT_PROTOTYPE_METHOD_SPECS[] = {
    {"hasOwnProperty", 14, JS_BUILTIN_OBJ_HAS_OWN_PROPERTY, 1},
    {"propertyIsEnumerable", 20, JS_BUILTIN_OBJ_PROPERTY_IS_ENUMERABLE, 1},
    {"toString", 8, JS_BUILTIN_OBJ_TO_STRING, 0},
    {"valueOf", 7, JS_BUILTIN_OBJ_VALUE_OF, 0},
    {"isPrototypeOf", 13, JS_BUILTIN_OBJ_IS_PROTOTYPE_OF, 1},
    {"toLocaleString", 14, JS_BUILTIN_OBJ_TO_LOCALE_STRING, 0},
    {"__defineGetter__", 16, JS_BUILTIN_OBJ_DEFINE_GETTER, 2},
    {"__defineSetter__", 16, JS_BUILTIN_OBJ_DEFINE_SETTER, 2},
    {"__lookupGetter__", 16, JS_BUILTIN_OBJ_LOOKUP_GETTER, 1},
    {"__lookupSetter__", 16, JS_BUILTIN_OBJ_LOOKUP_SETTER, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_FUNCTION_PROTOTYPE_METHOD_SPECS[] = {
    {"call", 4, JS_BUILTIN_FUNC_CALL, 1},
    {"apply", 5, JS_BUILTIN_FUNC_APPLY, 2},
    {"bind", 4, JS_BUILTIN_FUNC_BIND, 1},
    {"toString", 8, JS_BUILTIN_FUNC_TO_STRING, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_BOOLEAN_PROTOTYPE_METHOD_SPECS[] = {
    {"toString", 8, JS_BUILTIN_BOOL_TO_STRING, 0},
    {"valueOf", 7, JS_BUILTIN_BOOL_VALUE_OF, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_ARRAY_PROTOTYPE_METHOD_SPECS[] = {
    {"push", 4, JS_BUILTIN_ARR_PUSH, 1},
    {"pop", 3, JS_BUILTIN_ARR_POP, 0},
    {"shift", 5, JS_BUILTIN_ARR_SHIFT, 0},
    {"unshift", 7, JS_BUILTIN_ARR_UNSHIFT, 1},
    {"join", 4, JS_BUILTIN_ARR_JOIN, 1},
    {"slice", 5, JS_BUILTIN_ARR_SLICE, 2},
    {"splice", 6, JS_BUILTIN_ARR_SPLICE, 2},
    {"indexOf", 7, JS_BUILTIN_ARR_INDEX_OF, 1},
    {"lastIndexOf", 11, JS_BUILTIN_ARR_LAST_INDEX_OF, 1},
    {"includes", 8, JS_BUILTIN_ARR_INCLUDES, 1},
    {"map", 3, JS_BUILTIN_ARR_MAP, 1},
    {"filter", 6, JS_BUILTIN_ARR_FILTER, 1},
    {"reduce", 6, JS_BUILTIN_ARR_REDUCE, 1},
    {"forEach", 7, JS_BUILTIN_ARR_FOR_EACH, 1},
    {"find", 4, JS_BUILTIN_ARR_FIND, 1},
    {"findIndex", 9, JS_BUILTIN_ARR_FIND_INDEX, 1},
    {"some", 4, JS_BUILTIN_ARR_SOME, 1},
    {"every", 5, JS_BUILTIN_ARR_EVERY, 1},
    {"sort", 4, JS_BUILTIN_ARR_SORT, 1},
    {"reverse", 7, JS_BUILTIN_ARR_REVERSE, 0},
    {"concat", 6, JS_BUILTIN_ARR_CONCAT, 1},
    {"flat", 4, JS_BUILTIN_ARR_FLAT, 0},
    {"flatMap", 7, JS_BUILTIN_ARR_FLAT_MAP, 1},
    {"fill", 4, JS_BUILTIN_ARR_FILL, 1},
    {"copyWithin", 10, JS_BUILTIN_ARR_COPY_WITHIN, 2},
    {"toString", 8, JS_BUILTIN_ARR_TO_STRING, 0},
    {"toLocaleString", 14, JS_BUILTIN_ARR_TO_LOCALE_STRING, 0},
    {"keys", 4, JS_BUILTIN_ARR_KEYS, 0},
    {"values", 6, JS_BUILTIN_ARR_VALUES, 0},
    {"entries", 7, JS_BUILTIN_ARR_ENTRIES, 0},
    {"at", 2, JS_BUILTIN_ARR_AT, 1},
    {"item", 4, JS_BUILTIN_ARR_ITEM, 1},
    {"reduceRight", 11, JS_BUILTIN_ARR_REDUCE_RIGHT, 1},
    {"findLast", 8, JS_BUILTIN_ARR_FIND_LAST, 1},
    {"findLastIndex", 13, JS_BUILTIN_ARR_FIND_LAST_INDEX, 1},
    {"toSorted", 8, JS_BUILTIN_ARR_TO_SORTED, 1},
    {"toReversed", 10, JS_BUILTIN_ARR_TO_REVERSED, 0},
    {"toSpliced", 9, JS_BUILTIN_ARR_TO_SPLICED, 2},
    {"with", 4, JS_BUILTIN_ARR_WITH, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_NUMBER_PROTOTYPE_METHOD_SPECS[] = {
    {"toString", 8, JS_BUILTIN_NUM_TO_STRING, 1},
    {"valueOf", 7, JS_BUILTIN_NUM_VALUE_OF, 0},
    {"toLocaleString", 14, JS_BUILTIN_OBJ_TO_LOCALE_STRING, 0},
    {"toFixed", 7, JS_BUILTIN_NUM_TO_FIXED, 1},
    {"toPrecision", 11, JS_BUILTIN_NUM_TO_PRECISION, 1},
    {"toExponential", 13, JS_BUILTIN_NUM_TO_EXPONENTIAL, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_SYMBOL_PROTOTYPE_METHOD_SPECS[] = {
    {"toString", 8, JS_BUILTIN_SYM_TO_STRING, 0},
    {"valueOf", 7, JS_BUILTIN_SYM_VALUE_OF, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_BIGINT_PROTOTYPE_METHOD_SPECS[] = {
    {"toString", 8, JS_BUILTIN_BIGINT_TO_STRING, 0},
    {"valueOf", 7, JS_BUILTIN_BIGINT_VALUE_OF, 0},
    {"toLocaleString", 14, JS_BUILTIN_BIGINT_TO_LOCALE_STRING, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_BIGINT_STATIC_METHOD_SPECS[] = {
    {"asIntN", 6, JS_BUILTIN_BIGINT_AS_INT_N, 2},
    {"asUintN", 7, JS_BUILTIN_BIGINT_AS_UINT_N, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_STRING_PROTOTYPE_METHOD_SPECS[] = {
    {"charAt", 6, JS_BUILTIN_STR_CHAR_AT, 1},
    {"charCodeAt", 10, JS_BUILTIN_STR_CHAR_CODE_AT, 1},
    {"indexOf", 7, JS_BUILTIN_STR_INDEX_OF, 1},
    {"lastIndexOf", 11, JS_BUILTIN_STR_LAST_INDEX_OF, 1},
    {"includes", 8, JS_BUILTIN_STR_INCLUDES, 1},
    {"slice", 5, JS_BUILTIN_STR_SLICE, 2},
    {"substring", 9, JS_BUILTIN_STR_SUBSTRING, 2},
    {"toLowerCase", 11, JS_BUILTIN_STR_TO_LOWER_CASE, 0},
    {"toUpperCase", 11, JS_BUILTIN_STR_TO_UPPER_CASE, 0},
    {"trim", 4, JS_BUILTIN_STR_TRIM, 0},
    {"trimStart", 9, JS_BUILTIN_STR_TRIM_START, 0},
    {"trimEnd", 7, JS_BUILTIN_STR_TRIM_END, 0},
    {"split", 5, JS_BUILTIN_STR_SPLIT, 2},
    {"replace", 7, JS_BUILTIN_STR_REPLACE, 2},
    {"replaceAll", 10, JS_BUILTIN_STR_REPLACE_ALL, 2},
    {"match", 5, JS_BUILTIN_STR_MATCH, 1},
    {"matchAll", 8, JS_BUILTIN_STR_MATCH_ALL, 1},
    {"search", 6, JS_BUILTIN_STR_SEARCH, 1},
    {"startsWith", 10, JS_BUILTIN_STR_STARTS_WITH, 1},
    {"endsWith", 8, JS_BUILTIN_STR_ENDS_WITH, 1},
    {"repeat", 6, JS_BUILTIN_STR_REPEAT, 1},
    {"padStart", 8, JS_BUILTIN_STR_PAD_START, 1},
    {"padEnd", 6, JS_BUILTIN_STR_PAD_END, 1},
    {"toString", 8, JS_BUILTIN_STR_TO_STRING, 0},
    {"valueOf", 7, JS_BUILTIN_STR_VALUE_OF, 0},
    {"codePointAt", 11, JS_BUILTIN_STR_CODE_POINT_AT, 1},
    {"normalize", 9, JS_BUILTIN_STR_NORMALIZE, 0},
    {"concat", 6, JS_BUILTIN_STR_CONCAT, 1},
    {"at", 2, JS_BUILTIN_STR_AT, 1},
    {"localeCompare", 13, JS_BUILTIN_STR_LOCALE_COMPARE, 1},
    {"trimLeft", 8, JS_BUILTIN_STR_TRIM_START, 0, "trimStart"},
    {"trimRight", 9, JS_BUILTIN_STR_TRIM_END, 0, "trimEnd"},
    {"isWellFormed", 12, JS_BUILTIN_STR_IS_WELL_FORMED, 0},
    {"toWellFormed", 12, JS_BUILTIN_STR_TO_WELL_FORMED, 0},
    {"anchor", 6, JS_BUILTIN_STR_ANCHOR, 1},
    {"big", 3, JS_BUILTIN_STR_BIG, 0},
    {"blink", 5, JS_BUILTIN_STR_BLINK, 0},
    {"bold", 4, JS_BUILTIN_STR_BOLD, 0},
    {"fixed", 5, JS_BUILTIN_STR_FIXED, 0},
    {"fontcolor", 9, JS_BUILTIN_STR_FONTCOLOR, 1},
    {"fontsize", 8, JS_BUILTIN_STR_FONTSIZE, 1},
    {"italics", 7, JS_BUILTIN_STR_ITALICS, 0},
    {"link", 4, JS_BUILTIN_STR_LINK, 1},
    {"small", 5, JS_BUILTIN_STR_SMALL, 0},
    {"strike", 6, JS_BUILTIN_STR_STRIKE, 0},
    {"sub", 3, JS_BUILTIN_STR_SUB, 0},
    {"sup", 3, JS_BUILTIN_STR_SUP, 0},
    {"substr", 6, JS_BUILTIN_STR_SUBSTR, 2},
    {"toLocaleLowerCase", 17, JS_BUILTIN_STR_TO_LOCALE_LOWER_CASE, 0},
    {"toLocaleUpperCase", 17, JS_BUILTIN_STR_TO_LOCALE_UPPER_CASE, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_PROMISE_PROTOTYPE_METHOD_SPECS[] = {
    {"then", 4, JS_BUILTIN_PROMISE_PROTO_THEN, 2},
    {"catch", 5, JS_BUILTIN_PROMISE_PROTO_CATCH, 1},
    {"finally", 7, JS_BUILTIN_PROMISE_PROTO_FINALLY, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_MAP_PROTOTYPE_METHOD_SPECS[] = {
    {"set", 3, JS_BUILTIN_MAP_SET, 2},
    {"get", 3, JS_BUILTIN_MAP_GET, 1},
    {"has", 3, JS_BUILTIN_MAP_HAS, 1},
    {"delete", 6, JS_BUILTIN_MAP_DELETE, 1},
    {"clear", 5, JS_BUILTIN_MAP_CLEAR, 0},
    {"forEach", 7, JS_BUILTIN_MAP_FOREACH, 1},
    {"keys", 4, JS_BUILTIN_MAP_KEYS, 0},
    {"values", 6, JS_BUILTIN_MAP_VALUES, 0},
    {"entries", 7, JS_BUILTIN_MAP_ENTRIES, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_SET_PROTOTYPE_METHOD_SPECS[] = {
    {"add", 3, JS_BUILTIN_SET_ADD, 1},
    {"has", 3, JS_BUILTIN_SET_HAS, 1},
    {"delete", 6, JS_BUILTIN_SET_DELETE, 1},
    {"clear", 5, JS_BUILTIN_SET_CLEAR, 0},
    {"forEach", 7, JS_BUILTIN_SET_FOREACH, 1},
    {"values", 6, JS_BUILTIN_SET_VALUES, 0},
    {"entries", 7, JS_BUILTIN_SET_ENTRIES, 0},
    {"intersection", 12, JS_BUILTIN_SET_INTERSECTION, 1},
    {"union", 5, JS_BUILTIN_SET_UNION, 1},
    {"difference", 10, JS_BUILTIN_SET_DIFFERENCE, 1},
    {"symmetricDifference", 19, JS_BUILTIN_SET_SYM_DIFF, 1},
    {"isSubsetOf", 10, JS_BUILTIN_SET_IS_SUBSET, 1},
    {"isSupersetOf", 12, JS_BUILTIN_SET_IS_SUPERSET, 1},
    {"isDisjointFrom", 14, JS_BUILTIN_SET_IS_DISJOINT, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_WEAKMAP_PROTOTYPE_METHOD_SPECS[] = {
    {"set", 3, JS_BUILTIN_WEAKMAP_SET, 2},
    {"get", 3, JS_BUILTIN_WEAKMAP_GET, 1},
    {"has", 3, JS_BUILTIN_WEAKMAP_HAS, 1},
    {"delete", 6, JS_BUILTIN_WEAKMAP_DELETE, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_WEAKSET_PROTOTYPE_METHOD_SPECS[] = {
    {"add", 3, JS_BUILTIN_WEAKSET_ADD, 1},
    {"has", 3, JS_BUILTIN_WEAKSET_HAS, 1},
    {"delete", 6, JS_BUILTIN_WEAKSET_DELETE, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_WEAKREF_PROTOTYPE_METHOD_SPECS[] = {
    {"deref", 5, JS_BUILTIN_WEAKREF_DEREF, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_FINALIZATION_REGISTRY_PROTOTYPE_METHOD_SPECS[] = {
    {"register", 8, JS_BUILTIN_FINALIZATION_REGISTER, 2},
    {"unregister", 10, JS_BUILTIN_FINALIZATION_UNREGISTER, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_ARRAYBUFFER_PROTOTYPE_METHOD_SPECS[] = {
    {"slice", 5, JS_BUILTIN_ARRAYBUFFER_SLICE, 2},
    {"resize", 6, JS_BUILTIN_ARRAYBUFFER_RESIZE, 1},
    // Js54 P8: ArrayBuffer.prototype.transfer / transferToFixedLength (ES2024)
    {"transfer", 8, JS_BUILTIN_ARRAYBUFFER_TRANSFER, 0},
    {"transferToFixedLength", 21, JS_BUILTIN_ARRAYBUFFER_TRANSFER_TO_FIXED_LENGTH, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_ARRAYBUFFER_ACCESSOR_SPECS[] = {
    {"byteLength", 10, JS_BUILTIN_ARRAYBUFFER_GET_BYTE_LENGTH, 0, "get byteLength"},
    {"resizable", 9, JS_BUILTIN_ARRAYBUFFER_GET_RESIZABLE, 0, "get resizable"},
    {"maxByteLength", 13, JS_BUILTIN_ARRAYBUFFER_GET_MAX_BYTE_LENGTH, 0, "get maxByteLength"},
    // Js54 P8: ES2024 ArrayBuffer.prototype.detached accessor
    {"detached", 8, JS_BUILTIN_ARRAYBUFFER_GET_DETACHED, 0, "get detached"},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_SHAREDARRAYBUFFER_PROTOTYPE_METHOD_SPECS[] = {
    {"slice", 5, JS_BUILTIN_SHAREDARRAYBUFFER_SLICE, 2},
    {"grow", 4, JS_BUILTIN_SHAREDARRAYBUFFER_GROW, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_SHAREDARRAYBUFFER_ACCESSOR_SPECS[] = {
    {"byteLength", 10, JS_BUILTIN_SHAREDARRAYBUFFER_GET_BYTE_LENGTH, 0, "get byteLength"},
    {"growable", 8, JS_BUILTIN_SHAREDARRAYBUFFER_GET_GROWABLE, 0, "get growable"},
    {"maxByteLength", 13, JS_BUILTIN_SHAREDARRAYBUFFER_GET_MAX_BYTE_LENGTH, 0, "get maxByteLength"},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_DATAVIEW_PROTOTYPE_METHOD_SPECS[] = {
    {"getInt8", 7, -2, 1},
    {"getUint8", 8, -2, 1},
    {"getInt16", 8, -2, 1},
    {"getUint16", 9, -2, 1},
    {"getInt32", 8, -2, 1},
    {"getUint32", 9, -2, 1},
    {"getFloat32", 10, -2, 1},
    {"getFloat64", 10, -2, 1},
    {"getBigInt64", 11, -2, 1},
    {"getBigUint64", 12, -2, 1},
    {"setInt8", 7, -2, 2},
    {"setUint8", 8, -2, 2},
    {"setInt16", 8, -2, 2},
    {"setUint16", 9, -2, 2},
    {"setInt32", 8, -2, 2},
    {"setUint32", 9, -2, 2},
    {"setFloat32", 10, -2, 2},
    {"setFloat64", 10, -2, 2},
    {"setBigInt64", 11, -2, 2},
    {"setBigUint64", 12, -2, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_DATAVIEW_ACCESSOR_SPECS[] = {
    {"buffer", 6, 0, 0, "get buffer"},
    {"byteLength", 10, 0, 0, "get byteLength"},
    {"byteOffset", 10, 0, 0, "get byteOffset"},
    {NULL, 0, 0, 0}
};

void js_populate_dataview_prototype_methods(Item prototype) {
    js_install_builtin_function_specs(prototype, JS_DATAVIEW_PROTOTYPE_METHOD_SPECS, 0, false);
    js_install_builtin_accessor_specs(prototype, JS_DATAVIEW_ACCESSOR_SPECS,
        JS_FUNC_FLAG_DATA_VIEW_ACCESSOR | JS_FUNC_FLAG_STRICT);
}

const JsBuiltinMethodSpec JS_DATE_PROTOTYPE_METHOD_SPECS[] = {
    {"getTime", 7, JS_BUILTIN_DATE_GET_TIME, 0},
    {"getFullYear", 11, JS_BUILTIN_DATE_GET_FULL_YEAR, 0},
    {"getMonth", 8, JS_BUILTIN_DATE_GET_MONTH, 0},
    {"getDate", 7, JS_BUILTIN_DATE_GET_DATE, 0},
    {"getHours", 8, JS_BUILTIN_DATE_GET_HOURS, 0},
    {"getMinutes", 10, JS_BUILTIN_DATE_GET_MINUTES, 0},
    {"getSeconds", 10, JS_BUILTIN_DATE_GET_SECONDS, 0},
    {"getMilliseconds", 15, JS_BUILTIN_DATE_GET_MILLISECONDS, 0},
    {"getDay", 6, JS_BUILTIN_DATE_GET_DAY, 0},
    {"getUTCFullYear", 14, JS_BUILTIN_DATE_GET_UTC_FULL_YEAR, 0},
    {"getUTCMonth", 11, JS_BUILTIN_DATE_GET_UTC_MONTH, 0},
    {"getUTCDate", 10, JS_BUILTIN_DATE_GET_UTC_DATE, 0},
    {"getUTCHours", 11, JS_BUILTIN_DATE_GET_UTC_HOURS, 0},
    {"getUTCMinutes", 13, JS_BUILTIN_DATE_GET_UTC_MINUTES, 0},
    {"getUTCSeconds", 13, JS_BUILTIN_DATE_GET_UTC_SECONDS, 0},
    {"getUTCMilliseconds", 18, JS_BUILTIN_DATE_GET_UTC_MILLISECONDS, 0},
    {"getUTCDay", 9, JS_BUILTIN_DATE_GET_UTC_DAY, 0},
    {"getTimezoneOffset", 17, JS_BUILTIN_DATE_GET_TIMEZONE_OFFSET, 0},
    {"setTime", 7, JS_BUILTIN_DATE_SET_TIME, 1},
    {"setFullYear", 11, JS_BUILTIN_DATE_SET_FULL_YEAR, 3},
    {"setMonth", 8, JS_BUILTIN_DATE_SET_MONTH, 2},
    {"setDate", 7, JS_BUILTIN_DATE_SET_DATE, 1},
    {"setHours", 8, JS_BUILTIN_DATE_SET_HOURS, 4},
    {"setMinutes", 10, JS_BUILTIN_DATE_SET_MINUTES, 3},
    {"setSeconds", 10, JS_BUILTIN_DATE_SET_SECONDS, 2},
    {"setMilliseconds", 15, JS_BUILTIN_DATE_SET_MILLISECONDS, 1},
    {"setUTCFullYear", 14, JS_BUILTIN_DATE_SET_UTC_FULL_YEAR, 3},
    {"setUTCMonth", 11, JS_BUILTIN_DATE_SET_UTC_MONTH, 2},
    {"setUTCDate", 10, JS_BUILTIN_DATE_SET_UTC_DATE, 1},
    {"setUTCHours", 11, JS_BUILTIN_DATE_SET_UTC_HOURS, 4},
    {"setUTCMinutes", 13, JS_BUILTIN_DATE_SET_UTC_MINUTES, 3},
    {"setUTCSeconds", 13, JS_BUILTIN_DATE_SET_UTC_SECONDS, 2},
    {"setUTCMilliseconds", 18, JS_BUILTIN_DATE_SET_UTC_MILLISECONDS, 1},
    {"toISOString", 11, JS_BUILTIN_DATE_TO_ISO_STRING, 0},
    {"toJSON", 6, JS_BUILTIN_DATE_TO_JSON, 1},
    {"toUTCString", 11, JS_BUILTIN_DATE_TO_UTC_STRING, 0},
    {"toGMTString", 11, JS_BUILTIN_DATE_TO_UTC_STRING, 0},
    {"toDateString", 12, JS_BUILTIN_DATE_TO_DATE_STRING, 0},
    {"toTimeString", 12, JS_BUILTIN_DATE_TO_TIME_STRING, 0},
    {"toString", 8, JS_BUILTIN_DATE_TO_STRING, 0},
    {"toLocaleString", 14, JS_BUILTIN_OBJ_TO_LOCALE_STRING, 0},
    {"toLocaleDateString", 18, JS_BUILTIN_DATE_TO_LOCALE_DATE_STRING, 0},
    {"toLocaleTimeString", 18, JS_BUILTIN_DATE_TO_LOCALE_TIME_STRING, 0},
    {"valueOf", 7, JS_BUILTIN_DATE_VALUE_OF, 0},
    {"getYear", 7, JS_BUILTIN_DATE_GET_YEAR, 0},
    {"setYear", 7, JS_BUILTIN_DATE_SET_YEAR, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_REGEXP_PROTOTYPE_METHOD_SPECS[] = {
    {"exec", 4, JS_BUILTIN_REGEXP_EXEC, 1},
    {"test", 4, JS_BUILTIN_REGEXP_TEST, 1},
    {"toString", 8, JS_BUILTIN_REGEXP_TO_STRING, 0},
    {"compile", 7, JS_BUILTIN_REGEXP_COMPILE, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_JSON_METHOD_SPECS[] = {
    {"parse", 5, JS_BUILTIN_JSON_PARSE, 2},
    {"stringify", 9, JS_BUILTIN_JSON_STRINGIFY, 3},
    {"rawJSON", 7, JS_BUILTIN_JSON_RAW_JSON, 1},
    {"isRawJSON", 9, JS_BUILTIN_JSON_IS_RAW_JSON, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_REFLECT_METHOD_SPECS[] = {
    {"apply", 5, JS_BUILTIN_REFLECT_APPLY, 3},
    {"construct", 9, JS_BUILTIN_REFLECT_CONSTRUCT, 2},
    {"defineProperty", 14, JS_BUILTIN_REFLECT_DEFINE_PROPERTY, 3},
    {"deleteProperty", 14, JS_BUILTIN_REFLECT_DELETE_PROPERTY, 2},
    {"get", 3, JS_BUILTIN_REFLECT_GET, 2},
    {"getOwnPropertyDescriptor", 24, JS_BUILTIN_REFLECT_GET_OWN_PROPERTY_DESCRIPTOR, 2},
    {"getPrototypeOf", 14, JS_BUILTIN_REFLECT_GET_PROTOTYPE_OF, 1},
    {"has", 3, JS_BUILTIN_REFLECT_HAS, 2},
    {"isExtensible", 12, JS_BUILTIN_REFLECT_IS_EXTENSIBLE, 1},
    {"ownKeys", 7, JS_BUILTIN_REFLECT_OWN_KEYS, 1},
    {"preventExtensions", 17, JS_BUILTIN_REFLECT_PREVENT_EXTENSIONS, 1},
    {"set", 3, JS_BUILTIN_REFLECT_SET, 3},
    {"setPrototypeOf", 14, JS_BUILTIN_REFLECT_SET_PROTOTYPE_OF, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_ATOMICS_METHOD_SPECS[] = {
    {"add", 3, JS_BUILTIN_ATOMICS_ADD, 3},
    {"and", 3, JS_BUILTIN_ATOMICS_AND, 3},
    {"compareExchange", 15, JS_BUILTIN_ATOMICS_COMPAREEXCHANGE, 4},
    {"exchange", 8, JS_BUILTIN_ATOMICS_EXCHANGE, 3},
    {"isLockFree", 10, JS_BUILTIN_ATOMICS_ISLOCKFREE, 1},
    {"load", 4, JS_BUILTIN_ATOMICS_LOAD, 2},
    {"notify", 6, JS_BUILTIN_ATOMICS_NOTIFY, 3},
    {"or", 2, JS_BUILTIN_ATOMICS_OR, 3},
    {"pause", 5, JS_BUILTIN_ATOMICS_PAUSE, 0},
    {"store", 5, JS_BUILTIN_ATOMICS_STORE, 3},
    {"sub", 3, JS_BUILTIN_ATOMICS_SUB, 3},
    {"wait", 4, JS_BUILTIN_ATOMICS_WAIT, 4},
    {"waitAsync", 9, JS_BUILTIN_ATOMICS_WAIT_ASYNC, 4},
    {"xor", 3, JS_BUILTIN_ATOMICS_XOR, 3},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_CSS_METHOD_SPECS[] = {
    {"supports", 8, JS_BUILTIN_CSS_SUPPORTS, 2},
    {"escape", 6, JS_BUILTIN_CSS_ESCAPE, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_OBJECT_STATIC_METHOD_SPECS[] = {
    {"keys", 4, JS_BUILTIN_OBJECT_KEYS, 1},
    {"values", 6, JS_BUILTIN_OBJECT_VALUES, 1},
    {"entries", 7, JS_BUILTIN_OBJECT_ENTRIES, 1},
    {"fromEntries", 11, JS_BUILTIN_OBJECT_FROM_ENTRIES, 1},
    {"create", 6, JS_BUILTIN_OBJECT_CREATE, 2},
    {"assign", 6, JS_BUILTIN_OBJECT_ASSIGN, 2},
    {"freeze", 6, JS_BUILTIN_OBJECT_FREEZE, 1},
    {"isFrozen", 8, JS_BUILTIN_OBJECT_IS_FROZEN, 1},
    {"seal", 4, JS_BUILTIN_OBJECT_SEAL, 1},
    {"isSealed", 8, JS_BUILTIN_OBJECT_IS_SEALED, 1},
    {"preventExtensions", 17, JS_BUILTIN_OBJECT_PREVENT_EXTENSIONS, 1},
    {"isExtensible", 12, JS_BUILTIN_OBJECT_IS_EXTENSIBLE, 1},
    {"is", 2, JS_BUILTIN_OBJECT_IS, 2},
    {"getPrototypeOf", 14, JS_BUILTIN_OBJECT_GET_PROTOTYPE_OF, 1},
    {"setPrototypeOf", 14, JS_BUILTIN_OBJECT_SET_PROTOTYPE_OF, 2},
    {"defineProperty", 14, JS_BUILTIN_OBJECT_DEFINE_PROPERTY, 3},
    {"defineProperties", 16, JS_BUILTIN_OBJECT_DEFINE_PROPERTIES, 2},
    {"getOwnPropertyDescriptor", 24, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTOR, 2},
    {"getOwnPropertyDescriptors", 25, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTORS, 1},
    {"getOwnPropertyNames", 19, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_NAMES, 1},
    {"getOwnPropertySymbols", 21, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_SYMBOLS, 1},
    {"hasOwn", 6, JS_BUILTIN_OBJECT_HAS_OWN, 2},
    {"groupBy", 7, JS_BUILTIN_OBJECT_GROUP_BY, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_ARRAY_STATIC_METHOD_SPECS[] = {
    {"isArray", 7, JS_BUILTIN_ARRAY_IS_ARRAY, 1},
    {"from", 4, JS_BUILTIN_ARRAY_FROM, 1},
    {"of", 2, JS_BUILTIN_ARRAY_OF, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_STRING_STATIC_METHOD_SPECS[] = {
    {"fromCharCode", 12, JS_BUILTIN_STRING_FROM_CHAR_CODE, 1},
    {"fromCodePoint", 13, JS_BUILTIN_STRING_FROM_CODE_POINT, 1},
    {"raw", 3, JS_BUILTIN_STRING_RAW, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_DATE_STATIC_METHOD_SPECS[] = {
    {"now", 3, JS_BUILTIN_DATE_NOW, 0},
    {"parse", 5, JS_BUILTIN_DATE_PARSE, 1},
    {"UTC", 3, JS_BUILTIN_DATE_UTC, 7},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_PROMISE_STATIC_METHOD_SPECS[] = {
    {"resolve", 7, JS_BUILTIN_PROMISE_RESOLVE, 1},
    {"reject", 6, JS_BUILTIN_PROMISE_REJECT, 1},
    {"all", 3, JS_BUILTIN_PROMISE_ALL, 1},
    {"allSettled", 10, JS_BUILTIN_PROMISE_ALL_SETTLED, 1},
    {"any", 3, JS_BUILTIN_PROMISE_ANY, 1},
    {"race", 4, JS_BUILTIN_PROMISE_RACE, 1},
    {"withResolvers", 13, JS_BUILTIN_PROMISE_WITH_RESOLVERS, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_NUMBER_STATIC_METHOD_SPECS[] = {
    {"isFinite", 8, JS_BUILTIN_NUMBER_IS_FINITE, 1},
    {"isNaN", 5, JS_BUILTIN_NUMBER_IS_NAN, 1},
    {"isInteger", 9, JS_BUILTIN_NUMBER_IS_INTEGER, 1},
    {"isSafeInteger", 13, JS_BUILTIN_NUMBER_IS_SAFE_INTEGER, 1},
    {"parseInt", 8, JS_BUILTIN_NUMBER_PARSE_INT, 2},
    {"parseFloat", 10, JS_BUILTIN_NUMBER_PARSE_FLOAT, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_MAP_STATIC_METHOD_SPECS[] = {
    {"groupBy", 7, JS_BUILTIN_MAP_GROUP_BY, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_ARRAYBUFFER_STATIC_METHOD_SPECS[] = {
    {"isView", 6, JS_BUILTIN_ARRAYBUFFER_ISVIEW, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_PROXY_STATIC_METHOD_SPECS[] = {
    {"revocable", 9, JS_BUILTIN_PROXY_REVOCABLE, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_TYPED_ARRAY_STATIC_METHOD_SPECS[] = {
    {"from", 4, JS_BUILTIN_TYPED_ARRAY_FROM, 1},
    {"of", 2, JS_BUILTIN_TYPED_ARRAY_OF, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_SYMBOL_STATIC_METHOD_SPECS[] = {
    {"for", 3, JS_BUILTIN_SYMBOL_FOR, 1},
    {"keyFor", 6, JS_BUILTIN_SYMBOL_KEY_FOR, 1},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_TYPED_ARRAY_PROTOTYPE_METHOD_SPECS[] = {
    {"at", 2, JS_BUILTIN_ARR_AT, 1},
    {"copyWithin", 10, JS_BUILTIN_ARR_COPY_WITHIN, 2},
    {"entries", 7, JS_BUILTIN_ARR_ENTRIES, 0},
    {"every", 5, JS_BUILTIN_ARR_EVERY, 1},
    {"fill", 4, JS_BUILTIN_ARR_FILL, 1},
    {"filter", 6, JS_BUILTIN_ARR_FILTER, 1},
    {"find", 4, JS_BUILTIN_ARR_FIND, 1},
    {"findIndex", 9, JS_BUILTIN_ARR_FIND_INDEX, 1},
    {"findLast", 8, JS_BUILTIN_ARR_FIND_LAST, 1},
    {"findLastIndex", 13, JS_BUILTIN_ARR_FIND_LAST_INDEX, 1},
    {"forEach", 7, JS_BUILTIN_ARR_FOR_EACH, 1},
    {"includes", 8, JS_BUILTIN_ARR_INCLUDES, 1},
    {"indexOf", 7, JS_BUILTIN_ARR_INDEX_OF, 1},
    {"join", 4, JS_BUILTIN_ARR_JOIN, 1},
    {"keys", 4, JS_BUILTIN_ARR_KEYS, 0},
    {"lastIndexOf", 11, JS_BUILTIN_ARR_LAST_INDEX_OF, 1},
    {"map", 3, JS_BUILTIN_ARR_MAP, 1},
    {"reduce", 6, JS_BUILTIN_ARR_REDUCE, 1},
    {"reduceRight", 11, JS_BUILTIN_ARR_REDUCE_RIGHT, 1},
    {"reverse", 7, JS_BUILTIN_ARR_REVERSE, 0},
    {"slice", 5, JS_BUILTIN_ARR_SLICE, 2},
    {"some", 4, JS_BUILTIN_ARR_SOME, 1},
    {"sort", 4, JS_BUILTIN_ARR_SORT, 1},
    {"toReversed", 10, JS_BUILTIN_ARR_TO_REVERSED, 0},
    {"toSorted", 8, JS_BUILTIN_ARR_TO_SORTED, 1},
    {"toString", 8, JS_BUILTIN_ARR_TO_STRING, 0},
    {"values", 6, JS_BUILTIN_ARR_VALUES, 0},
    {"with", 4, JS_BUILTIN_ARR_WITH, 2},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_TYPED_ARRAY_STUB_METHOD_SPECS[] = {
    {"set", 3, 0, 1},
    {"subarray", 8, 0, 2},
    {"toLocaleString", 14, 0, 0},
    {NULL, 0, 0, 0}
};

const JsBuiltinMethodSpec JS_TYPED_ARRAY_ACCESSOR_SPECS[] = {
    {"buffer", 6, 0, 0, "get buffer"},
    {"byteLength", 10, 0, 0, "get byteLength"},
    {"byteOffset", 10, 0, 0, "get byteOffset"},
    {"length", 6, 0, 0, "get length"},
    {NULL, 0, 0, 0}
};

static const JsBuiltinMethodSpec* js_get_constructor_static_method_specs(const char* ctor_name, int ctor_len) {
    if (!ctor_name) return NULL;
    if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) return JS_OBJECT_STATIC_METHOD_SPECS;
    if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) return JS_ARRAY_STATIC_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "String", 6) == 0) return JS_STRING_STATIC_METHOD_SPECS;
    if (ctor_len == 4 && strncmp(ctor_name, "Date", 4) == 0) return JS_DATE_STATIC_METHOD_SPECS;
    if (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) return JS_PROMISE_STATIC_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "Number", 6) == 0) return JS_NUMBER_STATIC_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "BigInt", 6) == 0) return JS_BIGINT_STATIC_METHOD_SPECS;
    if (ctor_len == 3 && strncmp(ctor_name, "Map", 3) == 0) return JS_MAP_STATIC_METHOD_SPECS;
    if (ctor_len == 11 && strncmp(ctor_name, "ArrayBuffer", 11) == 0) return JS_ARRAYBUFFER_STATIC_METHOD_SPECS;
    if (ctor_len == 5 && strncmp(ctor_name, "Proxy", 5) == 0) return JS_PROXY_STATIC_METHOD_SPECS;
    if (ctor_len == 10 && strncmp(ctor_name, "TypedArray", 10) == 0) return JS_TYPED_ARRAY_STATIC_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "Symbol", 6) == 0) return JS_SYMBOL_STATIC_METHOD_SPECS;
    return NULL;
}

static const JsBuiltinMethodSpec* js_get_prototype_method_specs_for_ctor(const char* ctor_name, int ctor_len) {
    if (!ctor_name) return NULL;
    if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) return JS_OBJECT_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) return JS_ARRAY_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 8 && strncmp(ctor_name, "Function", 8) == 0) return JS_FUNCTION_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "Number", 6) == 0) return JS_NUMBER_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "BigInt", 6) == 0) return JS_BIGINT_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "String", 6) == 0) return JS_STRING_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) return JS_PROMISE_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 3 && strncmp(ctor_name, "Map", 3) == 0) return JS_MAP_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 3 && strncmp(ctor_name, "Set", 3) == 0) return JS_SET_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 7 && strncmp(ctor_name, "WeakMap", 7) == 0) return JS_WEAKMAP_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 7 && strncmp(ctor_name, "WeakSet", 7) == 0) return JS_WEAKSET_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 7 && strncmp(ctor_name, "WeakRef", 7) == 0) return JS_WEAKREF_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 20 && strncmp(ctor_name, "FinalizationRegistry", 20) == 0) return JS_FINALIZATION_REGISTRY_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 11 && strncmp(ctor_name, "ArrayBuffer", 11) == 0) return JS_ARRAYBUFFER_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 17 && strncmp(ctor_name, "SharedArrayBuffer", 17) == 0) return JS_SHAREDARRAYBUFFER_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 4 && strncmp(ctor_name, "Date", 4) == 0) return JS_DATE_PROTOTYPE_METHOD_SPECS;
    if (ctor_len == 6 && strncmp(ctor_name, "RegExp", 6) == 0) return JS_REGEXP_PROTOTYPE_METHOD_SPECS;
    return NULL;
}

static const JsBuiltinMethodSpec* js_get_prototype_method_specs_for_type(TypeId type) {
    if (type == LMD_TYPE_ARRAY) return JS_ARRAY_PROTOTYPE_METHOD_SPECS;
    if (type == LMD_TYPE_FUNC) return JS_FUNCTION_PROTOTYPE_METHOD_SPECS;
    if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) return JS_NUMBER_PROTOTYPE_METHOD_SPECS;
    if (type == LMD_TYPE_DECIMAL) return JS_BIGINT_PROTOTYPE_METHOD_SPECS;
    if (type == LMD_TYPE_STRING) return JS_STRING_PROTOTYPE_METHOD_SPECS;
    if (type == LMD_TYPE_BOOL) return JS_BOOLEAN_PROTOTYPE_METHOD_SPECS;
    return NULL;
}

static const JsBuiltinMethodSpec* js_get_prototype_method_specs_for_class_or_type(
    int js_class, TypeId fallback_type, int* out_flags, bool* out_use_cache) {
    if (out_flags) *out_flags = 0;
    if (out_use_cache) *out_use_cache = true;
    switch ((JsClass)js_class) {
    case JS_CLASS_OBJECT: return JS_OBJECT_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_FUNCTION: return JS_FUNCTION_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_BOOLEAN: return JS_BOOLEAN_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_NUMBER: return JS_NUMBER_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_BIGINT: return JS_BIGINT_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_SYMBOL: return JS_SYMBOL_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_STRING: return JS_STRING_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_ARRAY: return JS_ARRAY_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_DATE: return JS_DATE_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_REGEXP: return JS_REGEXP_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_PROMISE: return JS_PROMISE_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_MAP: return JS_MAP_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_SET: return JS_SET_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_WEAK_MAP: return JS_WEAKMAP_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_WEAK_SET: return JS_WEAKSET_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_WEAK_REF: return JS_WEAKREF_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_FINALIZATION_REGISTRY: return JS_FINALIZATION_REGISTRY_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_ARRAY_BUFFER: return JS_ARRAYBUFFER_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_DATA_VIEW:
        if (out_use_cache) *out_use_cache = false;
        return JS_DATAVIEW_PROTOTYPE_METHOD_SPECS;
    case JS_CLASS_TYPED_ARRAY:
        if (out_flags) *out_flags = JS_FUNC_FLAG_TYPED_ARRAY_METHOD;
        if (out_use_cache) *out_use_cache = false;
        return JS_TYPED_ARRAY_PROTOTYPE_METHOD_SPECS;
    default:
        break;
    }
    return js_get_prototype_method_specs_for_type(fallback_type);
}

static void js_append_builtin_method_spec_names(const JsBuiltinMethodSpec* specs, Item result);

static Item js_builtin_registry_data_descriptor_from_spec(const JsBuiltinMethodSpec* spec,
                                                          int flags, bool use_cache) {
    if (!spec) return make_js_undefined();
    Item value = js_create_builtin_function_from_spec(spec, flags, use_cache);
    Item desc = js_new_object();
    js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
    js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
    return desc;
}

static Item js_builtin_registry_accessor_descriptor_from_spec(const JsBuiltinMethodSpec* spec,
                                                              int flags) {
    if (!spec) return make_js_undefined();
    Item getter = js_create_builtin_function_from_spec(spec, flags, false);
    Item desc = js_new_object();
    js_property_set(desc, (Item){.item = s2it(heap_create_name("get", 3))}, getter);
    js_property_set(desc, (Item){.item = s2it(heap_create_name("set", 3))}, make_js_undefined());
    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
    return desc;
}

extern "C" Item js_builtin_registry_prototype_method_descriptor(
    int js_class, TypeId fallback_type, const char* name, int len) {
    int flags = 0;
    bool use_cache = true;
    const JsBuiltinMethodSpec* specs =
        js_get_prototype_method_specs_for_class_or_type(js_class, fallback_type, &flags, &use_cache);
    const JsBuiltinMethodSpec* spec = js_find_builtin_method_spec(specs, name, len);
    if (spec) return js_builtin_registry_data_descriptor_from_spec(spec, flags, use_cache);

    if ((JsClass)js_class == JS_CLASS_TYPED_ARRAY) {
        spec = js_find_builtin_method_spec(JS_TYPED_ARRAY_STUB_METHOD_SPECS, name, len);
        if (spec) {
            return js_builtin_registry_data_descriptor_from_spec(
                spec, JS_FUNC_FLAG_TYPED_ARRAY_METHOD, false);
        }
        spec = js_find_builtin_method_spec(JS_TYPED_ARRAY_ACCESSOR_SPECS, name, len);
        if (spec) {
            return js_builtin_registry_accessor_descriptor_from_spec(
                spec, JS_FUNC_FLAG_TYPED_ARRAY_METHOD);
        }
    }
    if ((JsClass)js_class == JS_CLASS_DATA_VIEW) {
        spec = js_find_builtin_method_spec(JS_DATAVIEW_ACCESSOR_SPECS, name, len);
        if (spec) {
            return js_builtin_registry_accessor_descriptor_from_spec(
                spec, JS_FUNC_FLAG_DATA_VIEW_ACCESSOR | JS_FUNC_FLAG_STRICT);
        }
    }
    if ((JsClass)js_class == JS_CLASS_ARRAY_BUFFER) {
        spec = js_find_builtin_method_spec(JS_ARRAYBUFFER_ACCESSOR_SPECS, name, len);
        if (spec) {
            return js_builtin_registry_accessor_descriptor_from_spec(
                spec, JS_FUNC_FLAG_STRICT);
        }
    }
    return make_js_undefined();
}

extern "C" bool js_builtin_registry_has_prototype_method(
    int js_class, TypeId fallback_type, const char* name, int len) {
    Item desc = js_builtin_registry_prototype_method_descriptor(js_class, fallback_type, name, len);
    return get_type_id(desc) != LMD_TYPE_UNDEFINED;
}

extern "C" void js_append_builtin_method_names_for_class(
    int js_class, TypeId fallback_type, Item result) {
    int flags = 0;
    bool use_cache = true;
    (void)flags;
    (void)use_cache;
    const JsBuiltinMethodSpec* specs =
        js_get_prototype_method_specs_for_class_or_type(js_class, fallback_type, NULL, NULL);
    js_append_builtin_method_spec_names(specs, result);
    if ((JsClass)js_class == JS_CLASS_TYPED_ARRAY) {
        js_append_builtin_method_spec_names(JS_TYPED_ARRAY_STUB_METHOD_SPECS, result);
        js_append_builtin_method_spec_names(JS_TYPED_ARRAY_ACCESSOR_SPECS, result);
    } else if ((JsClass)js_class == JS_CLASS_DATA_VIEW) {
        js_append_builtin_method_spec_names(JS_DATAVIEW_ACCESSOR_SPECS, result);
    } else if ((JsClass)js_class == JS_CLASS_ARRAY_BUFFER) {
        js_append_builtin_method_spec_names(JS_ARRAYBUFFER_ACCESSOR_SPECS, result);
    }
}

void js_populate_builtin_prototype_methods(Item prototype, const char* ctor_name, int ctor_len) {
    const JsBuiltinMethodSpec* specs = js_get_prototype_method_specs_for_ctor(ctor_name, ctor_len);
    js_install_builtin_method_specs(prototype, specs);
    if (ctor_len == 11 && strncmp(ctor_name, "ArrayBuffer", 11) == 0) {
        js_install_builtin_accessor_specs(prototype, JS_ARRAYBUFFER_ACCESSOR_SPECS, JS_FUNC_FLAG_STRICT);
    }
    if (ctor_len == 17 && strncmp(ctor_name, "SharedArrayBuffer", 17) == 0) {
        js_install_builtin_accessor_specs(prototype, JS_SHAREDARRAYBUFFER_ACCESSOR_SPECS, JS_FUNC_FLAG_STRICT);
    }
}

static void js_append_builtin_method_spec_names(const JsBuiltinMethodSpec* specs, Item result) {
    if (!specs) return;
    for (int i = 0; specs[i].name; i++) {
        Item key = (Item){.item = s2it(heap_create_name(specs[i].name, specs[i].len))};
        js_array_push(result, key);
    }
}

Item js_lookup_builtin_prototype_method_for_class(JsClass cls, const char* name, int len) {
    int flags = 0;
    bool use_cache = true;
    const JsBuiltinMethodSpec* specs =
        js_get_prototype_method_specs_for_class_or_type((int)cls, LMD_TYPE_MAP, &flags, &use_cache);
    (void)flags;
    (void)use_cache;
    return js_lookup_builtin_method_spec(specs, name, len);
}

void js_builtin_cache_reset() {
    for (int i = 0; i < JS_BUILTIN_MAX; i++) js_builtin_cache[i] = ItemNull;
}

Item js_get_or_create_builtin(int builtin_id, const char* name, int param_count) {
    if (!js_builtin_cache_init) {
        for (int i = 0; i < JS_BUILTIN_MAX; i++) js_builtin_cache[i] = ItemNull;
        js_builtin_cache_init = true;
    }
    if (js_builtin_cache[builtin_id].item != ItemNull.item) {
        return js_builtin_cache[builtin_id];
    }
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = NULL;  // not needed, dispatch uses builtin_id
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->builtin_id = builtin_id;
    if (builtin_id == JS_BUILTIN_FUNC_THROW_TYPE_ERROR) {
        fn->name = heap_create_name("", 0);
    } else {
        fn->name = heap_create_name(name, strlen(name));
    }
    fn->prototype = ItemNull;
    // NOTE: bound_this left as 0 (from pool_calloc). Do NOT set to ItemNull
    // because ItemNull.item is non-zero (0x100000000000000) and the bound
    // function check uses `fn->bound_this.item` as a boolean test.
    Item result = {.function = (Function*)fn};
    if (builtin_id == JS_BUILTIN_FUNC_THROW_TYPE_ERROR) {
        Item length_key = (Item){.item = s2it(heap_create_name("length", 6))};
        js_func_init_property(result, length_key, (Item){.item = i2it(0)});
        js_attr_set_writable(result, "length", 6, false);
        js_attr_set_enumerable(result, "length", 6, false);
        js_attr_set_configurable(result, "length", 6, false);

        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        js_func_init_property(result, name_key, (Item){.item = s2it(heap_create_name("", 0))});
        js_attr_set_writable(result, "name", 4, false);
        js_attr_set_enumerable(result, "name", 4, false);
        js_attr_set_configurable(result, "name", 4, false);

        Item non_ext_key = (Item){.item = s2it(heap_create_name("__non_extensible__", 17))};
        js_func_init_property(result, non_ext_key, (Item){.item = b2it(true)});
        Item frozen_key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
        js_func_init_property(result, frozen_key, (Item){.item = b2it(true)});
    }
    js_builtin_cache[builtin_id] = result;
    return result;
}

// Wrapper for js_globals.cpp to create Symbol.for / Symbol.keyFor builtins
extern "C" Item js_symbol_builtin_method(int which) {
    if (which == 0) return js_get_or_create_builtin(JS_BUILTIN_SYMBOL_FOR, "for", 1);
    if (which == 1) return js_get_or_create_builtin(JS_BUILTIN_SYMBOL_KEY_FOR, "keyFor", 1);
    return ItemNull;
}

// v18k: Lookup static methods on constructor functions (Object.keys, Array.isArray, etc.)
// Returns ItemNull if not a known constructor or not a known static method.
Item js_lookup_constructor_static(const char* ctor_name, int ctor_len,
                                          const char* prop_name, int prop_len) {
    const JsBuiltinMethodSpec* specs = js_get_constructor_static_method_specs(ctor_name, ctor_len);
    Item method = js_lookup_builtin_method_spec(specs, prop_name, prop_len);
    if (method.item != ItemNull.item) return method;

    // Handle .prototype on any constructor — delegate to the constructor's property access
    if (prop_len == 9 && strncmp(prop_name, "prototype", 9) == 0) {
        Item ctor_name_item = (Item){.item = s2it(heap_create_name(ctor_name, ctor_len))};
        Item ctor = js_get_constructor(ctor_name_item);
        if (get_type_id(ctor) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            return js_property_get(ctor, proto_key);
        }
    }
    return ItemNull;
}

// Externally callable wrapper for js_lookup_constructor_static (from transpiler)
extern "C" Item js_constructor_static_property(Item ctor_name, Item prop_name) {
    String* cn = it2s(ctor_name);
    String* pn = it2s(prop_name);
    if (!cn || !pn) return ItemNull;
    Item v = js_lookup_constructor_static(cn->chars, (int)cn->len, pn->chars, (int)pn->len);
    if (v.item != ItemNull.item) return v;
    // Fall back to general property access on the constructor function object.
    // This handles standard function properties (length, name, prototype) and
    // any user-assigned own properties on the constructor.
    Item ctor = js_get_constructor(ctor_name);
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        return js_property_get(ctor, prop_name);
    }
    return make_js_undefined();
}

// Populate %TypedArray%.prototype with proper Array builtin methods
// and static methods on the %TypedArray% constructor.
extern "C" void js_populate_typed_array_base_proto(Item proto, Item base_ctor) {
    // Register constructor
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_property_set(proto, ctor_key, base_ctor);
    js_mark_non_enumerable(proto, ctor_key);

    // Prototype methods: reuse Array builtins (dispatch handles typed arrays)
    js_install_builtin_function_specs(proto, JS_TYPED_ARRAY_PROTOTYPE_METHOD_SPECS,
        JS_FUNC_FLAG_TYPED_ARRAY_METHOD, false);

    // %TypedArray%.prototype.toString is exactly Array.prototype.toString.
    {
        Item to_string_key = (Item){.item = s2it(heap_create_name("toString", 8))};
        Item array_to_string = js_get_or_create_builtin(JS_BUILTIN_ARR_TO_STRING, "toString", 0);
        js_property_set(proto, to_string_key, array_to_string);
        js_mark_non_enumerable(proto, to_string_key);
    }

    // TypedArray-specific methods (stubs — no Array equivalent)
    js_install_builtin_function_specs(proto, JS_TYPED_ARRAY_STUB_METHOD_SPECS,
        JS_FUNC_FLAG_TYPED_ARRAY_METHOD, false);

    // Symbol.iterator = values (same function object as TypedArray.prototype.values per spec)
    {
        Item si_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
        Item values_key = (Item){.item = s2it(heap_create_name("values", 6))};
        Item values_fn = js_property_get(proto, values_key);
        js_property_set(proto, si_key, values_fn);
        js_mark_non_enumerable(proto, si_key);
    }

    // get %TypedArray%.prototype[@@toStringTag]
    {
        JsFunction* tag_getter = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
        tag_getter->type_id = LMD_TYPE_FUNC;
        tag_getter->name = heap_create_name("get [Symbol.toStringTag]", 24);
        tag_getter->param_count = 0;
        tag_getter->formal_length = -1;
        Item getter_item = (Item){.function = (Function*)tag_getter};
        Item tag_name = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
        js_install_native_accessor(proto, tag_name, getter_item, ItemNull, JSPD_NON_ENUMERABLE);
    }

    // Accessor getter stubs for buffer, byteLength, byteOffset, length
    // These throw TypeError when accessed on non-TypedArray (ES spec §23.2.3.1/2/3)
    // Phase 3 Stage A: route through unified js_install_native_accessor.
    js_install_builtin_accessor_specs(proto, JS_TYPED_ARRAY_ACCESSOR_SPECS,
        JS_FUNC_FLAG_TYPED_ARRAY_METHOD);

    // Install static methods from/of on %TypedArray% constructor (base_ctor)
    {
        js_install_builtin_method_specs_on_function(base_ctor, JS_TYPED_ARRAY_STATIC_METHOD_SPECS, false);

        // Install get [Symbol.species]() { return this; } on %TypedArray%
        // Phase 3 Stage A: route through unified js_install_native_accessor.
        Item species_name = (Item){.item = s2it(heap_create_name("__sym_6", 7))};
        Item getter_fn = js_get_or_create_builtin(JS_BUILTIN_ITER_IDENTITY, "get [Symbol.species]", 0);
        js_install_native_accessor(base_ctor, species_name, getter_fn, ItemNull, JSPD_NON_ENUMERABLE);
    }
}

// Populate all known static methods on a constructor function as own properties.
// This makes them visible to hasOwnProperty, getOwnPropertyDescriptor, getOwnPropertyNames.
extern "C" void js_populate_constructor_statics(Item ctor_item, const char* ctor_name, int ctor_len) {
    const JsBuiltinMethodSpec* specs = js_get_constructor_static_method_specs(ctor_name, ctor_len);
    js_install_builtin_method_specs_on_function(ctor_item, specs, true);

    // ES spec: install get [Symbol.species]() { return this; } on constructors
    // that support @@species (Array, RegExp, Promise, Map, Set, ArrayBuffer, TypedArray constructors)
    bool needs_species = false;
    if ((ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) ||
        (ctor_len == 6 && strncmp(ctor_name, "RegExp", 6) == 0) ||
        (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) ||
        (ctor_len == 3 && strncmp(ctor_name, "Map", 3) == 0) ||
        (ctor_len == 3 && strncmp(ctor_name, "Set", 3) == 0) ||
        (ctor_len == 11 && strncmp(ctor_name, "ArrayBuffer", 11) == 0)) {
        needs_species = true;
    }
    if (needs_species) {
        // install getter: __get___sym_6 → function that returns this
        // Phase 3 Stage A: route through unified js_install_native_accessor.
        Item species_name = (Item){.item = s2it(heap_create_name("__sym_6", 7))};
        Item getter_fn = js_get_or_create_builtin(JS_BUILTIN_ITER_IDENTITY, "get [Symbol.species]", 0);
        js_install_native_accessor(ctor_item, species_name, getter_fn, ItemNull, JSPD_NON_ENUMERABLE);
    }
}

// Lookup built-in method by name for a given receiver type
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len) {
    // Object.prototype methods (available on all objects and arrays)
    if (len == 14 && strncmp(name, "hasOwnProperty", 14) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_HAS_OWN_PROPERTY, "hasOwnProperty", 1);
    if (len == 20 && strncmp(name, "propertyIsEnumerable", 20) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_PROPERTY_IS_ENUMERABLE, "propertyIsEnumerable", 1);
    if (len == 8 && strncmp(name, "toString", 8) == 0 && type != LMD_TYPE_FUNC && type != LMD_TYPE_BOOL && type != LMD_TYPE_ARRAY && type != LMD_TYPE_STRING && type != LMD_TYPE_INT && type != LMD_TYPE_INT64 && type != LMD_TYPE_FLOAT && type != LMD_TYPE_DECIMAL)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_TO_STRING, "toString", 0);
    if (type == LMD_TYPE_BOOL) {
        Item method = js_lookup_builtin_method_spec(JS_BOOLEAN_PROTOTYPE_METHOD_SPECS, name, len);
        if (method.item != ItemNull.item) return method;
    }
    if (len == 7 && strncmp(name, "valueOf", 7) == 0 && type != LMD_TYPE_FUNC && type != LMD_TYPE_INT && type != LMD_TYPE_INT64 && type != LMD_TYPE_FLOAT && type != LMD_TYPE_DECIMAL)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_VALUE_OF, "valueOf", 0);
    if (len == 13 && strncmp(name, "isPrototypeOf", 13) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_IS_PROTOTYPE_OF, "isPrototypeOf", 1);
    if (len == 14 && strncmp(name, "toLocaleString", 14) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_TO_LOCALE_STRING, "toLocaleString", 0);
    if (len == 16 && strncmp(name, "__defineGetter__", 16) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_DEFINE_GETTER, "__defineGetter__", 2);
    if (len == 16 && strncmp(name, "__defineSetter__", 16) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_DEFINE_SETTER, "__defineSetter__", 2);
    if (len == 16 && strncmp(name, "__lookupGetter__", 16) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_LOOKUP_GETTER, "__lookupGetter__", 1);
    if (len == 16 && strncmp(name, "__lookupSetter__", 16) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_LOOKUP_SETTER, "__lookupSetter__", 1);

    // Function.prototype methods
    if (type == LMD_TYPE_FUNC) {
        Item method = js_lookup_builtin_method_spec(JS_FUNCTION_PROTOTYPE_METHOD_SPECS, name, len);
        if (method.item != ItemNull.item) return method;
    }

    // Array.prototype methods
    if (type == LMD_TYPE_ARRAY) {
        Item method = js_lookup_builtin_method_spec(JS_ARRAY_PROTOTYPE_METHOD_SPECS, name, len);
        if (method.item != ItemNull.item) return method;
    }

    // String.prototype methods
    if (type == LMD_TYPE_STRING) {
        Item method = js_lookup_builtin_method_spec(JS_STRING_PROTOTYPE_METHOD_SPECS, name, len);
        if (method.item != ItemNull.item) return method;
    }

    // Number.prototype methods
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        Item method = js_lookup_builtin_method_spec(JS_NUMBER_PROTOTYPE_METHOD_SPECS, name, len);
        if (method.item != ItemNull.item) return method;
    }

    return ItemNull;
}

// v26: Return all builtin method names for a prototype type as a Lambda array.
// Used by getOwnPropertyNames to enumerate builtin methods on prototype objects.
extern "C" void js_append_builtin_method_names(TypeId type, Item result) {
    const JsBuiltinMethodSpec* specs = js_get_prototype_method_specs_for_type(type);
    if (!specs) specs = JS_OBJECT_PROTOTYPE_METHOD_SPECS;
    if (specs) {
        js_append_builtin_method_spec_names(specs, result);
        if (type == LMD_TYPE_ARRAY) {
            Item locale_key = (Item){.item = s2it(heap_create_name("toLocaleString", 14))};
            js_array_push(result, locale_key);
        }
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
        js_array_push(result, ctor_key);
        return;
    }
}
