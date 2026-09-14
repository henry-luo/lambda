// lib/hashmap_helpers.h - Boilerplate eraser for hashmap.h.
//
// Most of the codebase's hashmap users repeat the same shape: a struct whose
// first field is a C-string key (either `char name[N]` or `const char* name`),
// compared with strcmp and hashed with hashmap_sip. This header provides:
//
//   1. C macro factories for common key shapes.
//   2. HASHMAP_DEFINE_STRKEY(name, struct_type, key_field) - emits cmp/hash
//      functions plus a `<name>_new(cap)` factory in one line.
//   3. hashmap_typed.hpp supplies the equivalent typed facade for C++ callers.
//
// Example:
//   struct VarScopeEntry { char name[128]; MirVarEntry var; };
//   HASHMAP_DEFINE_STRKEY(var_scope, struct VarScopeEntry, name)
//   // ... var_scope_new(0) -> struct hashmap* keyed by VarScopeEntry::name

#ifndef LIB_HASHMAP_HELPERS_H
#define LIB_HASHMAP_HELPERS_H

#include "hashmap.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define HASHMAP_HELPER_UNUSED __attribute__((unused))
#else
#define HASHMAP_HELPER_UNUSED
#endif

static inline uint64_t hashmap_hash_identity2(const void* first, size_t first_size,
                                               const void* second, size_t second_size,
                                               uint64_t seed0, uint64_t seed1) {
    uint64_t h1 = hashmap_murmur(first, first_size, seed0, seed1);
    uint64_t h2 = hashmap_murmur(second, second_size, seed0, seed1);
    return h1 ^ (h2 * UINT64_C(0x9e3779b97f4a7c15));
}

static inline uint64_t hashmap_hash_identity3(const void* first, size_t first_size,
                                               const void* second, size_t second_size,
                                               const void* third, size_t third_size,
                                               uint64_t seed0, uint64_t seed1) {
    uint64_t h1 = hashmap_murmur(first, first_size, seed0, seed1);
    uint64_t h2 = hashmap_murmur(second, second_size, seed0, seed1);
    uint64_t h3 = hashmap_murmur(third, third_size, seed0, seed1);
    return h1 ^ (h2 * UINT64_C(0x9e3779b97f4a7c15)) ^
        (h3 * UINT64_C(0x517cc1b727220a95));
}

// --- macro: emit cmp/hash/new for a struct keyed by a C-string field --------
// Works for both `char name[N]` and `const char* name` because `e->field` decays
// to `const char*` in either case.
#define HASHMAP_DEFINE_STRKEY(name, struct_type, key_field) \
    static inline const char* name##_key(const void* item) { \
        return ((const struct_type*)item)->key_field; \
    } \
    static uint64_t name##_hash(const void* item, uint64_t s0, uint64_t s1) { \
        const char* k = name##_key(item); \
        return hashmap_sip(k, strlen(k), s0, s1); \
    } \
    static int name##_cmp(const void* a, const void* b, void* udata) { \
        (void)udata; \
        return strcmp(name##_key(a), name##_key(b)); \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new(size_t cap) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, NULL, NULL); \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new_with_free(size_t cap, void (*elfree)(void*)) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, elfree, NULL); \
    }

// emit cmp/hash/new for a struct keyed by pointer identity at field.
#define HASHMAP_DEFINE_PTRKEY(name, struct_type, key_field) \
    static uint64_t name##_hash(const void* item, uint64_t s0, uint64_t s1) { \
        uintptr_t p = (uintptr_t)((const struct_type*)item)->key_field; \
        return hashmap_sip(&p, sizeof(p), s0, s1); \
    } \
    static int name##_cmp(const void* a, const void* b, void* udata) { \
        (void)udata; \
        const struct_type* ea = (const struct_type*)a; \
        const struct_type* eb = (const struct_type*)b; \
        return ea->key_field == eb->key_field ? 0 : 1; \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new(size_t cap) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, NULL, NULL); \
    }

// emit cmp/hash/new for a struct keyed by an integer field. Works for any
// integer type the C compiler can compare with `<` (int32_t, int64_t,
// uint32_t, uint64_t, size_t, etc.). Hash mixes the raw bytes via sip.
#define HASHMAP_DEFINE_INTKEY(name, struct_type, key_field) \
    static uint64_t name##_hash(const void* item, uint64_t s0, uint64_t s1) { \
        const struct_type* e = (const struct_type*)item; \
        return hashmap_sip(&e->key_field, sizeof(e->key_field), s0, s1); \
    } \
    static int name##_cmp(const void* a, const void* b, void* udata) { \
        (void)udata; \
        const struct_type* ea = (const struct_type*)a; \
        const struct_type* eb = (const struct_type*)b; \
        if (ea->key_field == eb->key_field) return 0; \
        return ea->key_field < eb->key_field ? -1 : 1; \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new(size_t cap) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, NULL, NULL); \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new_with_free(size_t cap, void (*elfree)(void*)) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, elfree, NULL); \
    }

// emit cmp/hash/new for a struct whose key is a (pointer, length) pair —
// non-NUL-terminated string slice. str_field must be `const char*` or `char*`;
// len_field must be an unsigned integer-typed field. Compares via length-then-
// memcmp; hashes via hashmap_sip on the byte range.
//
// Works equally well when the key is a StrView-like inline struct where the
// pointer and length live at sibling fields, including StrView itself:
//   struct E { StrView k; ... };
//   HASHMAP_DEFINE_LENSTRKEY(my_e, struct E, k.str, k.length)
#define HASHMAP_DEFINE_LENSTRKEY(name, struct_type, str_field, len_field) \
    static uint64_t name##_hash(const void* item, uint64_t s0, uint64_t s1) { \
        const struct_type* e = (const struct_type*)item; \
        if (!e->str_field || e->len_field == 0) return 0; \
        return hashmap_sip(e->str_field, e->len_field, s0, s1); \
    } \
    static int name##_cmp(const void* a, const void* b, void* udata) { \
        (void)udata; \
        const struct_type* ea = (const struct_type*)a; \
        const struct_type* eb = (const struct_type*)b; \
        if (ea->len_field != eb->len_field) \
            return ea->len_field < eb->len_field ? -1 : 1; \
        if (ea->len_field == 0) return 0; \
        return memcmp(ea->str_field, eb->str_field, ea->len_field); \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new(size_t cap) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, NULL, NULL); \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new_with_free(size_t cap, void (*elfree)(void*)) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, elfree, NULL); \
    }

// emit cmp/hash/new for a struct keyed by two integral or pointer identity fields.
// field expressions may name nested fields, e.g. `key.source_item.item`.
#define HASHMAP_DEFINE_FIELD2_KEY(name, struct_type, field1, field2) \
    static uint64_t name##_hash(const void* item, uint64_t s0, uint64_t s1) { \
        const struct_type* e = (const struct_type*)item; \
        return hashmap_hash_identity2(&e->field1, sizeof(e->field1), \
            &e->field2, sizeof(e->field2), s0, s1); \
    } \
    static int name##_cmp(const void* a, const void* b, void* udata) { \
        (void)udata; \
        const struct_type* ea = (const struct_type*)a; \
        const struct_type* eb = (const struct_type*)b; \
        return ea->field1 == eb->field1 && ea->field2 == eb->field2 ? 0 : 1; \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new(size_t cap) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, NULL, NULL); \
    }

// emit cmp/hash/new for a struct keyed by three integral or pointer identity fields.
// field expressions may name nested fields, e.g. `key.model_item.item`.
#define HASHMAP_DEFINE_FIELD3_KEY(name, struct_type, field1, field2, field3) \
    static uint64_t name##_hash(const void* item, uint64_t s0, uint64_t s1) { \
        const struct_type* e = (const struct_type*)item; \
        return hashmap_hash_identity3(&e->field1, sizeof(e->field1), \
            &e->field2, sizeof(e->field2), &e->field3, sizeof(e->field3), s0, s1); \
    } \
    static int name##_cmp(const void* a, const void* b, void* udata) { \
        (void)udata; \
        const struct_type* ea = (const struct_type*)a; \
        const struct_type* eb = (const struct_type*)b; \
        return ea->field1 == eb->field1 && ea->field2 == eb->field2 && \
            ea->field3 == eb->field3 ? 0 : 1; \
    } \
    static inline HASHMAP_HELPER_UNUSED struct hashmap* name##_new(size_t cap) { \
        return hashmap_new(sizeof(struct_type), cap, 0, 0, \
                           name##_hash, name##_cmp, NULL, NULL); \
    }

#ifdef __cplusplus
}
#endif

#endif
