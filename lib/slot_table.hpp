#pragma once

// Slot table: generation-checked references for Handle<T> fields.
//
// A holder that may outlive its target keeps a Handle<T> {index, gen} instead
// of a pointer. The table belongs to a node that outlives every holder.
// Lookup reads only the table, never the target, so a stale handle is detected
// without touching freed memory. When the target dies its owner releases the
// slot: the generation advances and every copy of the handle goes stale at
// once, with no walk over the holders. A slot whose generation would wrap is
// retired for good, so a stale handle can never match a later target.
//
// A pointer returned by lookup is a borrow for the current turn: hold it in a
// local or on the Stack, never store it in a Heap object.

#include <stdint.h>
#include "mempool.h"
#include "mem_grow.hpp"
#include "mem_kind.hpp"

namespace lam {

template<class T>
struct SlotTable {
    struct Slot {
        T* target;
        uint32_t gen;        // current generation; handles carry the gen they were issued with
        uint32_t next_free;  // 1-based free-list link, 0 = end
    };

    Pool* pool = nullptr;    // owner storage for the slot array
    Slot* slots = nullptr;
    size_t capacity = 0;
    uint32_t used = 0;       // slots ever handed out
    uint32_t free_head = 0;  // 1-based index of the first free slot, 0 = none
    uint32_t live = 0;

    bool init(Pool* owner_pool) {
        pool = owner_pool;
        slots = nullptr;
        capacity = 0;
        used = free_head = live = 0;
        return pool != nullptr;
    }

    void destroy() {
        if (pool && slots) pool_free(pool, slots);
        slots = nullptr;
        capacity = 0;
        used = free_head = live = 0;
    }

    // Returns a null handle (gen 0) on failure.
    Handle<T> insert(T* target) {
        Handle<T> handle = {0, 0};
        if (!target || !pool) return handle;
        uint32_t index;
        if (free_head) {
            index = free_head - 1;
            free_head = slots[index].next_free;
        } else {
            if (used == UINT32_MAX) return handle;
            if (!pool_grow_array(pool, &slots, &capacity, (size_t)used + 1, 16)) return handle;
            index = used++;
            slots[index].gen = 0;
        }
        Slot* slot = &slots[index];
        slot->gen++;          // a recycled slot starts one past its released generation
        slot->target = target;
        slot->next_free = 0;
        live++;
        handle.index = index;
        handle.gen = slot->gen;
        return handle;
    }

    T* lookup(Handle<T> handle) const {
        if (handle.gen == 0 || handle.index >= used) return nullptr;
        const Slot* slot = &slots[handle.index];
        return slot->gen == handle.gen ? slot->target : nullptr;
    }

    // Invalidates every copy of `handle`. False if it was already stale.
    bool release(Handle<T> handle) {
        if (!lookup(handle)) return false;
        Slot* slot = &slots[handle.index];
        slot->target = nullptr;
        live--;
        slot->gen++;
        if (slot->gen == UINT32_MAX) return true;  // retired: the next insert would wrap to a live gen
        slot->next_free = free_head;
        free_head = handle.index + 1;
        return true;
    }
};

} // namespace lam
