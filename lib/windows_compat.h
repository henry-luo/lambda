#ifndef WINDOWS_COMPAT_H
#define WINDOWS_COMPAT_H

#ifdef _WIN32
#include <string.h>
#include <stdlib.h>
#include <direct.h>

// Windows does not expose the POSIX environment and directory APIs used by
// the cross-platform runtime, so keep their compatibility surface centralized.
static inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value ? value : "") == 0 ? 0 : -1;
}

static inline int unsetenv(const char* name) {
    return _putenv_s(name, "") == 0 ? 0 : -1;
}

static inline int lambda_mkdir(const char* path, int mode) {
    (void)mode;
    return _mkdir(path);
}

// mingw's Windows headers already declare strndup; redeclaring it static here
// conflicts with that external declaration, so keep the fallback for other
// Windows toolchains only.
#if !defined(__MINGW32__)
static inline char* strndup(const char* s, size_t n) {
    size_t len = strlen(s);
    if (n < len) len = n;
    
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}
#endif

// memmem is not available on Windows, provide compatibility implementation
static inline void* memmem(const void* haystack, size_t hlen,
                           const void* needle, size_t nlen) {
    if (nlen == 0) return (void*)haystack;
    if (nlen > hlen) return NULL;
    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) return (void*)(h + i);
    }
    return NULL;
}

#else

#include <sys/stat.h>

static inline int lambda_mkdir(const char* path, int mode) {
    return mkdir(path, mode);
}

#endif // _WIN32

#endif // WINDOWS_COMPAT_H
