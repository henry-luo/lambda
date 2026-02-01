#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "string.h"

/* Simple string creation helper */
String* create_string(Pool* pool, const char* str) {
    if (!str || !pool) return NULL;

    size_t len = strlen(str);
    String* string = (String*)pool_calloc(pool, sizeof(String) + len + 1);
    if (!string) return NULL;

    string->len = (uint32_t)len;
    string->ref_cnt = 1;
    memcpy(string->chars, str, len);
    string->chars[len] = '\0';

    return string;
}

/* Create string from StrView */
String* string_from_strview(StrView view, Pool* pool) {
    if (!view.str || !pool) return NULL;
    // Allow empty strings (length == 0)

    String* string = (String*)pool_calloc(pool, sizeof(String) + view.length + 1);
    if (!string) return NULL;

    string->len = (uint32_t)view.length;
    string->ref_cnt = 1;
    if (view.length > 0) {
        memcpy(string->chars, view.str, view.length);
    }
    string->chars[view.length] = '\0';

    return string;
}
