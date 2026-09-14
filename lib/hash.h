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

// Original DJB2 addition form. Keep the initial state explicit because some
// language runtimes use zero rather than the conventional 5381 basis.
static inline uint64_t hash_djb2_add_extend(uint64_t hash, const void* data,
                                            size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < len; i++) hash = hash * 33 + p[i];
    return hash;
}

static inline uint64_t hash_djb2_add_extend_cstr(uint64_t hash, const char* s) {
    for (unsigned char c; (c = (unsigned char)*s) != 0; s++) hash = hash * 33 + c;
    return hash;
}

// FNV-1a 32-bit.
static inline uint32_t hash_fnv1a_32_extend(uint32_t hash, const void* data,
                                            size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < len; i++) hash = (hash ^ p[i]) * 0x01000193u;
    return hash;
}

static inline uint32_t hash_fnv1a_32(const void* data, size_t len) {
    return hash_fnv1a_32_extend(0x811c9dc5u, data, len);
}

static inline uint32_t hash_fnv1a_32_cstr(const char* s) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned char c; (c = (unsigned char)*s) != 0; s++) h = (h ^ c) * 0x01000193u;
    return h;
}

// FNV-1a 64-bit. Stateful updates let composite keys retain their exact
// field order without open-coding the byte loop at each call site.
#define HASH_FNV1A_64_OFFSET_BASIS UINT64_C(0xcbf29ce484222325)
#define HASH_FNV1A_64_PRIME UINT64_C(0x100000001b3)

static inline uint64_t hash_fnv1a_64_extend_byte(uint64_t hash, uint8_t byte) {
    return (hash ^ byte) * HASH_FNV1A_64_PRIME;
}

static inline uint64_t hash_fnv1a_64_extend(uint64_t hash, const void* data,
                                            size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < len; i++) hash = hash_fnv1a_64_extend_byte(hash, p[i]);
    return hash;
}

static inline uint64_t hash_fnv1a_64_extend_cstr(uint64_t hash, const char* s) {
    for (unsigned char c; (c = (unsigned char)*s) != 0; s++) {
        hash = hash_fnv1a_64_extend_byte(hash, c);
    }
    return hash;
}

// Feed a fixed-width scalar in little-endian order, independent of host ABI.
static inline uint64_t hash_fnv1a_64_extend_u64le(uint64_t hash, uint64_t value) {
    for (size_t i = 0; i < sizeof(value); i++) {
        hash = hash_fnv1a_64_extend_byte(hash, (uint8_t)(value >> (i * 8u)));
    }
    return hash;
}

static inline uint64_t hash_fnv1a_64(const void* data, size_t len) {
    return hash_fnv1a_64_extend(HASH_FNV1A_64_OFFSET_BASIS, data, len);
}

static inline uint64_t hash_fnv1a_64_cstr(const char* s) {
    return hash_fnv1a_64_extend_cstr(HASH_FNV1A_64_OFFSET_BASIS, s);
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

// Ordered hash combination for structural cache keys. This exact operation
// is deliberately non-finalizing: callers may continue combining fields.
static inline uint64_t hash_combine_u64(uint64_t hash, uint64_t value) {
    return hash ^ (value + UINT64_C(0x9e3779b97f4a7c15) +
                   (hash << 6u) + (hash >> 2u));
}

#ifdef __cplusplus
}
#endif

#endif
