#pragma once

#include <stdint.h>
#include "mempool.h"
#include "strview.h"

#ifdef __cplusplus
extern "C" {
#endif

// String structure (simplified version for library use)
// Only define if not already defined by the Lambda engine
#ifndef STRING_STRUCT_DEFINED
typedef struct String {
    uint32_t len:22;          // string len, up to 4MB
    uint32_t ref_cnt:10;      // ref_cnt, up to 1024 refs
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
