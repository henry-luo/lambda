#ifndef WINDOWS_COMPAT_H
#define WINDOWS_COMPAT_H

#ifdef _WIN32
#include <string.h>
#include <stdlib.h>

// strndup is not available on Windows, provide compatibility implementation
static inline char* strndup(const char* s, size_t n) {
    size_t len = strlen(s);
    if (n < len) len = n;
    
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}

#endif // _WIN32

#endif // WINDOWS_COMPAT_H
