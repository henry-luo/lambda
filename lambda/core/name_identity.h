#pragma once

#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "../../lib/string.h"
#include "../../lib/str.h"
#include "../../lib/hash.h"

// A NameId identifies an immutable generated catalog record. Dynamic names use
// NAME_ID_NONE; hashes and section offsets must never be stored in this type.
typedef uint32_t NameId;
typedef uint32_t SectionNameId;
typedef String* NameRef;
typedef String* PropertyKeyRef;

enum NameKeyKind {
    NAME_KEY_STRING = 0,
    NAME_KEY_SYMBOL = 1,
    NAME_KEY_PRIVATE = 2,
};

#define NAME_ID_NONE ((NameId)0)
#define NAME_ARRAY_INDEX_NONE UINT32_MAX

typedef struct NameMeta {
    uint32_t hash;
    uint32_t array_index;
    uint16_t flags;
    uint8_t key_kind;
    uint8_t reserved;
    NameId predefined_id;
} NameMeta;

typedef struct NameClassification {
    uint32_t hash;
    uint32_t array_index;
    uint16_t flags;
    uint8_t is_ascii;
} NameClassification;

// Generated catalogs expose records through this fixed image. The String view
// starts at len, preserving the existing String ABI while keeping NameMeta
// immediately before it. 127 bytes covers the predefined spellings; dynamic
// names continue to use NamePool allocation.
#if defined(_MSC_VER)
typedef __declspec(align(8)) struct WellKnownNameRecord {
#else
typedef struct __attribute__((aligned(8))) WellKnownNameRecord {
#endif
    NameMeta meta;
    uint32_t len;
    uint8_t flags;
    char chars[127];
} WellKnownNameRecord;

#if defined(__cplusplus)
static_assert(sizeof(NameMeta) == 16, "NameMeta ABI must remain 16 bytes");
static_assert(sizeof(NameId) == 4, "NameId ABI must remain 32 bits");
static_assert(sizeof(PropertyKeyRef) == sizeof(void*), "PropertyKeyRef must be pointer-sized");
static_assert(offsetof(String, chars) == 5, "String character offset is part of the NameRecord ABI");
static_assert(offsetof(WellKnownNameRecord, len) == sizeof(NameMeta), "generated records must embed String after NameMeta");
static_assert(alignof(WellKnownNameRecord) >= 8, "generated records must be 8-byte aligned");
#else
_Static_assert(sizeof(NameMeta) == 16, "NameMeta ABI must remain 16 bytes");
_Static_assert(sizeof(NameId) == 4, "NameId ABI must remain 32 bits");
_Static_assert(sizeof(PropertyKeyRef) == sizeof(void*), "PropertyKeyRef must be pointer-sized");
_Static_assert(offsetof(String, chars) == 5, "String character offset is part of the NameRecord ABI");
_Static_assert(offsetof(WellKnownNameRecord, len) == sizeof(NameMeta), "generated records must embed String after NameMeta");
_Static_assert(_Alignof(WellKnownNameRecord) >= 8, "generated records must be 8-byte aligned");
#endif

static inline bool string_is_pooled(const String* string) {
    return string && string->is_pooled != 0;
}

static inline NameMeta* name_ref_meta(NameRef name) {
    if (!string_is_pooled(name)) {
        assert(false && "NameMeta requires a pooled String");
        return NULL;
    }
    return (NameMeta*)((uint8_t*)name - sizeof(NameMeta));
}

static inline const NameMeta* name_ref_meta_const(NameRef name) {
    return (const NameMeta*)name_ref_meta(name);
}

static inline NameMeta* property_key_meta(PropertyKeyRef key) {
    return name_ref_meta(key);
}

static inline NameId name_ref_id(NameRef name) {
    const NameMeta* meta = name_ref_meta_const(name);
    return meta ? meta->predefined_id : NAME_ID_NONE;
}

static inline NameId property_key_id(PropertyKeyRef key) {
    return name_ref_id(key);
}

static inline uint8_t property_key_kind(PropertyKeyRef key) {
    NameMeta* meta = property_key_meta(key);
    return meta ? meta->key_kind : NAME_KEY_STRING;
}

static inline uint32_t property_key_hash(PropertyKeyRef key) {
    NameMeta* meta = property_key_meta(key);
    return meta ? meta->hash : 0;
}

static inline uint32_t property_key_array_index(PropertyKeyRef key) {
    NameMeta* meta = property_key_meta(key);
    return meta ? meta->array_index : NAME_ARRAY_INDEX_NONE;
}

static inline bool property_key_equal(PropertyKeyRef a, PropertyKeyRef b) {
    return a == b;
}

static inline bool property_key_requires_identity(PropertyKeyRef key) {
    return string_is_pooled(key) && property_key_kind(key) != NAME_KEY_STRING;
}

#ifdef __cplusplus
extern "C" {
#endif
NameId well_known_name_id(StrView name);
StrView well_known_name_view(NameId id);
NameRef well_known_name_ref(NameId id);
PropertyKeyRef well_known_key_ref(NameId id);
#ifdef __cplusplus
}
#endif

static inline NameClassification name_classify_ordinary(const char* bytes, size_t length) {
    NameClassification result = {0, NAME_ARRAY_INDEX_NONE, 0, 1};
    if (!bytes) return result;
    result.hash = hash_fnv1a_32(bytes, length);
    if (result.hash == 0) result.hash = 1;

    uint64_t index = 0;
    bool array_index = length > 0 && length <= 10 &&
        !(length > 1 && bytes[0] == '0');
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c >= 128) result.is_ascii = 0;
        if (!array_index || c < '0' || c > '9') {
            array_index = false;
            continue;
        }
        index = index * 10 + (uint64_t)(c - '0');
        if (index > 0xFFFFFFFEULL) array_index = false;
    }
    if (array_index) result.array_index = (uint32_t)index;
    return result;
}
