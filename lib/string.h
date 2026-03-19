#pragma once

#include <stdint.h>
#include "mempool.h"
#include "strview.h"

#ifdef __cplusplus
extern "C" {
#endif

// type_id value for String objects — must match LMD_TYPE_STRING in lambda/lambda.h
// IMPORTANT: do not change without also updating EnumTypeId in lambda/lambda.h
#define STRING_TYPE_ID 10
// type_id value for Binary objects (Binary = String with type_id = LMD_TYPE_BINARY)
#define BINARY_TYPE_ID 11
// type_id value for Symbol objects
#define SYMBOL_TYPE_ID 9

// String structure (simplified version for library use)
// Only define if not already defined by the Lambda engine
#ifndef STRING_STRUCT_DEFINED
typedef struct String {
    uint8_t type_id;          // type identifier (LMD_TYPE_STRING = 10)
    uint8_t is_ascii;         // 1 if all bytes < 0x80, 0 otherwise
    uint8_t _pad[2];          // explicit padding for uint32_t alignment
    uint32_t len;             // byte length of the string
    char chars[];             // flexible array member
} String;
#define STRING_STRUCT_DEFINED
#endif

// String creation and manipulation functions
String* create_string(Pool* pool, const char* str);
String* string_from_strview(StrView view, Pool* pool);

// String comparison, hashing (delegates to str.h)
#include <stdbool.h>
bool     string_eq(const String* a, const String* b);
int      string_cmp(const String* a, const String* b);
uint64_t string_hash(const String* s);
bool     string_eq_cstr(const String* s, const char* cstr);

#ifdef __cplusplus
}
#endif
