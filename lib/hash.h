// lib/hash.h - Small inline string/byte hashers.
//
// For bulk-data hashing prefer hashmap_xxhash3 in hashmap.h. These helpers exist
// because callers keep rolling tiny djb2/FNV-1a variants for short keys; pick
// one of these instead of writing a new one.

#ifndef LIB_HASH_H
#define LIB_HASH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// djb2 (Bernstein) - cheap, decent distribution for short ASCII keys.
static inline uint32_t hash_djb2(const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    uint32_t h = 5381;
    for (size_t i = 0; i < len; i++) h = (h * 33) ^ p[i];
    return h;
}

static inline uint32_t hash_djb2_cstr(const char* s) {
    uint32_t h = 5381;
    for (unsigned char c; (c = (unsigned char)*s) != 0; s++) h = (h * 33) ^ c;
    return h;
}

// FNV-1a 32-bit.
static inline uint32_t hash_fnv1a_32(const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static inline uint32_t hash_fnv1a_32_cstr(const char* s) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned char c; (c = (unsigned char)*s) != 0; s++) h = (h ^ c) * 0x01000193u;
    return h;
}

// FNV-1a 64-bit.
static inline uint64_t hash_fnv1a_64(const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) h = (h ^ p[i]) * 0x100000001b3ULL;
    return h;
}

static inline uint64_t hash_fnv1a_64_cstr(const char* s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c; (c = (unsigned char)*s) != 0; s++) h = (h ^ c) * 0x100000001b3ULL;
    return h;
}

// default C-string hasher (FNV-1a 64). Sized via strlen at call site.
static inline uint64_t hash_cstr(const char* s) {
    return hash_fnv1a_64_cstr(s);
}

// pointer hashing - identity-like with avalanche (good for hashmap keying on
// addresses). Wang's 64-bit mix.
static inline uint64_t hash_ptr(const void* p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    x = (~x) + (x << 21);
    x =  x ^ (x >> 24);
    x = (x + (x << 3)) + (x << 8);
    x =  x ^ (x >> 14);
    x = (x + (x << 2)) + (x << 4);
    x =  x ^ (x >> 28);
    x =  x + (x << 31);
    return x;
}

#ifdef __cplusplus
}
#endif

#endif
