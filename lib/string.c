#include "string.h"
#include <string.h>

/* Simple string creation helper */
String* create_string(VariableMemPool* pool, const char* str) {
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
