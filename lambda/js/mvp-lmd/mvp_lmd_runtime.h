#pragma once
#include "../../lambda-data.hpp"

struct MvpLmdProgram;
enum MvpLmdFailure { LMD_MVP_CAPABILITY, LMD_MVP_REFERENCE, LMD_MVP_TYPE,
    LMD_MVP_RANGE, LMD_MVP_MEMORY };

// internal callee-only marker; never exposed as a JavaScript value.
static inline uint64_t mvp_lmd_method_token(int64_t method) {
    return ITEM_JS_UNDEFINED | ((uint64_t)method << 8);
}

// shared compile-time/runtime spelling classification; no JIT import is needed.
enum MvpLmdMethod { LMD_METHOD_FILL = 9, LMD_METHOD_PUSH, LMD_METHOD_POP,
    LMD_METHOD_JOIN, LMD_METHOD_CHAR_AT, LMD_METHOD_CODE_AT, LMD_METHOD_REPEAT };
enum MvpLmdStringRead { LMD_STRING_INDEX, LMD_STRING_CHAR, LMD_STRING_CODE, LMD_STRING_FROM_CODE };
int mvp_lmd_builtin_method(String* key, TypeId owner = LMD_TYPE_MAP);

extern "C" {
Item mvp_lmd_fail(int64_t kind, int64_t site);
double mvp_lmd_string_to_number(String* string);
Item mvp_lmd_number_to_string(double value);
Item mvp_lmd_string_concat(Item left, Item right);
int64_t mvp_lmd_string_compare(Item left, Item right);
Item mvp_lmd_string_at(Item string, double index, int64_t mode);
double mvp_lmd_number_pow(double base, double exponent);
int64_t mvp_lmd_string_key(String* string, int64_t typed = 0);
Item mvp_lmd_array_store(Item array, uint32_t index, Item value);
Item mvp_lmd_array_new(int64_t length);
Item mvp_lmd_property_key(Item string);
Item mvp_lmd_object_new(TypeMap* shape, int64_t collection);
Item mvp_lmd_property_get(Item owner, Item name, int64_t callee);
Item mvp_lmd_property_set(Item owner, Item name, Item value);
Item mvp_lmd_property_delete(Item owner, Item name);
Item mvp_lmd_property_has(Item owner, Item name, int64_t inherited);
Item mvp_lmd_object_project(Item owner, int64_t projection);
Item mvp_lmd_map_call(Item owner, Item method, Item key, Item value);
int64_t mvp_lmd_map_next(Item owner, int64_t cursor);
Item mvp_lmd_map_entry(Item owner, int64_t cursor, int64_t projection);
Item mvp_lmd_function_new(uint64_t code_id, MvpLmdProgram* program);
}
