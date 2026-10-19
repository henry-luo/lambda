#pragma once
#include "../lib/ref_count.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// backend-neutral path handles share a frozen entry buffer; builders detach before appending.
struct RdtPath {
    enum Cmd { CMD_MOVE, CMD_LINE, CMD_CUBIC, CMD_CLOSE, CMD_RECT, CMD_CIRCLE };
    struct Entry {
        Cmd cmd;
        float args[8]; // max args: cubic(6), rect(6), circle(4)
    };
    struct RdtPathStorage* storage;
    Entry* entries;
    int count;
    int capacity;
};

struct RdtPathStorage {
    RefCount refs;
    uint64_t hash;
    float bounds[4];
    bool hash_cached, bounds_cached, has_bounds;
    // the entry array immediately follows this header.
};

uint64_t rdt_path_hash(const RdtPath* path);

inline size_t rdt_path_storage_bytes(const RdtPath* path) {
    return path ? sizeof(RdtPath) + (path->storage ? sizeof(RdtPathStorage) +
        (size_t)path->capacity * sizeof(RdtPath::Entry) : 0) : 0;
}

inline bool rdt_path_geometry_equal(const RdtPath* first, const RdtPath* second) {
    if (first == second || (first && second && first->storage == second->storage)) return true;
    return first && second && first->count == second->count &&
        (first->count == 0 || memcmp(first->entries, second->entries,
            (size_t)first->count * sizeof(RdtPath::Entry)) == 0);
}
