#pragma once

// Typed pool: individual allocation and release of one object type, with
// type-stable storage (D4.5.1 type-stable pools).
//
// A released object's slot is kept on this pool's own free list and only ever
// handed back as another T, never returned to the backing Pool for other
// sizes. A stale pointer therefore still points at a T (zeroed or reused),
// never at an unrelated object, so a missed invalidation cannot become type
// confusion. The slots are freed with the backing Pool.

#include <string.h>
#include "mempool.h"
#include "mem_kind.hpp"

namespace lam {

template<class T>
struct TypedPool {
    static_assert(sizeof(T) >= sizeof(void*), "a released slot must hold the free-list link");
    static_assert(!KindIsVoid<T>::value, "TypedPool<void> has no static type");

    Pool* pool = nullptr;      // backing owner storage
    void* free_head = nullptr; // intrusive free list through released slots
    size_t live = 0;
    size_t retained = 0;       // released slots waiting for reuse

    void init(Pool* owner_pool) {
        pool = owner_pool;
        free_head = nullptr;
        live = retained = 0;
    }

    // A zeroed T, reusing a released slot when one is available.
    T* alloc_zero() {
        void* slot = free_head;
        if (slot) {
            memcpy(&free_head, slot, sizeof(void*));
            retained--;
            memset(slot, 0, sizeof(T));
        } else {
            if (!pool) return nullptr;
            slot = pool_calloc(pool, sizeof(T));
            if (!slot) return nullptr;
        }
        live++;
        return (T*)slot;
    }

    // Keeps the slot for a later T; the caller has already torn the object down.
    void release(T* object) {
        if (!object) return;
#ifndef NDEBUG
        // poison so a stale reader sees garbage, not the old field values
        memset((void*)object, 0xDD, sizeof(T));
#endif
        memcpy((void*)object, &free_head, sizeof(void*));
        free_head = object;
        live--;
        retained++;
    }
};

} // namespace lam
