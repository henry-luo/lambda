// lib/hashmap_typed.hpp - C++ typed facade over lib/hashmap.
//
// The C hashmap owns storage and allocation. This facade only binds an entry
// type and key policy to C-compatible callbacks at compile time.

#ifndef LIB_HASHMAP_TYPED_HPP
#define LIB_HASHMAP_TYPED_HPP

#include "hashmap_helpers.h"

#include <stddef.h>
#include <string.h>

// These key policies use direct member fields. Equality is sufficient for
// hashmap; it never requires an ordering comparator.
template<typename Entry, auto Field>
struct HashMapCStrMemberKeyOps {
    static uint64_t hash(const Entry& entry, uint64_t seed0, uint64_t seed1) {
        const char* key = entry.*Field;
        return hashmap_sip(key, strlen(key), seed0, seed1);
    }

    static bool equals(const Entry& first, const Entry& second) {
        return strcmp(first.*Field, second.*Field) == 0;
    }
};

template<typename Entry, auto Field>
struct HashMapIntegralMemberKeyOps {
    static uint64_t hash(const Entry& entry, uint64_t seed0, uint64_t seed1) {
        const auto& key = entry.*Field;
        return hashmap_sip(&key, sizeof(key), seed0, seed1);
    }

    static bool equals(const Entry& first, const Entry& second) {
        return (first.*Field) == (second.*Field);
    }
};

template<typename Entry, auto First, auto Second>
struct HashMapIdentity2MemberKeyOps {
    static uint64_t hash(const Entry& entry, uint64_t seed0, uint64_t seed1) {
        const auto& first = entry.*First;
        const auto& second = entry.*Second;
        return hashmap_hash_identity2(&first, sizeof(first), &second, sizeof(second),
            seed0, seed1);
    }

    static bool equals(const Entry& first, const Entry& second) {
        return (first.*First) == (second.*First) &&
            (first.*Second) == (second.*Second);
    }
};

template<typename Entry, auto First, auto Second, auto Third>
struct HashMapIdentity3MemberKeyOps {
    static uint64_t hash(const Entry& entry, uint64_t seed0, uint64_t seed1) {
        const auto& first = entry.*First;
        const auto& second = entry.*Second;
        const auto& third = entry.*Third;
        return hashmap_hash_identity3(&first, sizeof(first), &second, sizeof(second),
            &third, sizeof(third), seed0, seed1);
    }

    static bool equals(const Entry& first, const Entry& second) {
        return (first.*First) == (second.*First) &&
            (first.*Second) == (second.*Second) &&
            (first.*Third) == (second.*Third);
    }
};

template<typename Entry, typename KeyOps, void (*EntryFree)(Entry*) = nullptr>
struct TypedHashMap {
    // Keep this type zero-initialized before init(), like other lib handles.
    HashMap* map;

    static HashMap* create(size_t capacity, uint64_t seed0 = 0, uint64_t seed1 = 0) {
        return hashmap_new(sizeof(Entry), capacity, seed0, seed1,
            hash_callback, compare_callback, entry_free_callback(), NULL);
    }

    bool init(size_t capacity, uint64_t seed0 = 0, uint64_t seed1 = 0) {
        if (map) return false;
        map = create(capacity, seed0, seed1);
        return map != NULL;
    }

    void destroy() {
        if (map) hashmap_free(map);
        map = NULL;
    }

    bool initialized() const { return map != NULL; }
    bool oom() const { return map && hashmap_oom(map); }
    size_t count() const { return map ? hashmap_count(map) : 0; }
    HashMap* raw() { return map; }
    const HashMap* raw() const { return map; }

    Entry* get(const Entry& key) { return get(map, key); }
    const Entry* get(const Entry& key) const { return get(map, key); }
    const Entry* set(const Entry& entry) { return set(map, entry); }
    const Entry* erase(const Entry& key) { return erase(map, key); }

    bool next(size_t* cursor, Entry** entry) {
        void* item = NULL;
        if (!map || !hashmap_iter(map, cursor, &item)) return false;
        if (entry) *entry = (Entry*)item;
        return true;
    }

    static Entry* get(HashMap* map, const Entry& key) {
        return map ? (Entry*)hashmap_get(map, &key) : NULL;
    }

    static const Entry* get(const HashMap* map, const Entry& key) {
        return map ? (const Entry*)hashmap_get((HashMap*)map, &key) : NULL;
    }

    static const Entry* set(HashMap* map, const Entry& entry) {
        return map ? (const Entry*)hashmap_set(map, &entry) : NULL;
    }

    static const Entry* erase(HashMap* map, const Entry& key) {
        return map ? (const Entry*)hashmap_delete(map, &key) : NULL;
    }

private:
    static uint64_t hash_callback(const void* item, uint64_t seed0, uint64_t seed1) {
        return KeyOps::hash(*(const Entry*)item, seed0, seed1);
    }

    static int compare_callback(const void* first, const void* second, void* udata) {
        (void)udata;
        return KeyOps::equals(*(const Entry*)first, *(const Entry*)second) ? 0 : 1;
    }

    static void free_callback(void* item) {
        EntryFree((Entry*)item);
    }

    static void (*entry_free_callback())(void*) {
        if constexpr (EntryFree != nullptr) return free_callback;
        return nullptr;
    }
};

#endif
