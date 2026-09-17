#pragma once

// Common ownership contract for GC-managed durable environments. Raw closure
// captures retain their Item-at-byte-zero ABI; lexical interpreters describe
// their extension through this view instead of teaching the collector a second
// independent storage protocol.

#ifdef __cplusplus
#include "../lambda.hpp"
#else
#include "../lambda.h"
#endif

typedef enum GcEnvironmentLayoutKind {
    GC_ENVIRONMENT_LAYOUT_ITEM_SLOTS = 0,
    GC_ENVIRONMENT_LAYOUT_LEXICAL = 1,
} GcEnvironmentLayoutKind;

typedef struct GcEnvironmentStorage {
    GcEnvironmentLayoutKind kind;
    Item* item_slots;
    uint64_t* scalar_slots;
    uint32_t item_count;
    void* outer;
    Item* retained_items;
    uint32_t retained_item_count;
} GcEnvironmentStorage;

typedef void (*GcEnvironmentVisitItemFn)(void* context, uint64_t item);
typedef void (*GcEnvironmentVisitObjectFn)(void* context, void* object);

static inline void gc_environment_storage_init(GcEnvironmentStorage* storage,
        GcEnvironmentLayoutKind kind, Item* item_slots, uint64_t* scalar_slots,
        uint32_t item_count, void* outer, Item* retained_items,
        uint32_t retained_item_count) {
    if (!storage) return;
    storage->kind = kind;
    storage->item_slots = item_slots;
    storage->scalar_slots = scalar_slots;
    storage->item_count = item_count;
    storage->outer = outer;
    storage->retained_items = retained_items;
    storage->retained_item_count = retained_item_count;
}

static inline Item gc_environment_storage_read(const GcEnvironmentStorage* storage,
        uint32_t slot, bool immortal) {
    if (!storage || !storage->item_slots || slot >= storage->item_count) {
#ifdef __cplusplus
        return Item{};
#else
        return (Item)0;
#endif
    }
    return owned_item_slot_read(storage->item_slots, storage->item_count, slot, immortal);
}

static inline void gc_environment_storage_store(const GcEnvironmentStorage* storage,
        uint32_t slot, Item value) {
    if (!storage || !storage->item_slots || slot >= storage->item_count) return;
    owned_item_slot_store(storage->item_slots, storage->item_count, slot, value);
}

static inline bool gc_environment_storage_copy_owned_slots(
        const GcEnvironmentStorage* destination,
        const GcEnvironmentStorage* source) {
    if (!destination || !source || !destination->item_slots || !source->item_slots ||
            destination->item_count != source->item_count) return false;
    for (uint32_t i = 0; i < source->item_count; i++) {
        destination->item_slots[i] = source->item_slots[i];
        if (destination->scalar_slots && source->scalar_slots) {
            destination->scalar_slots[i] = source->scalar_slots[i];
        }
    }
    return true;
}

static inline void gc_environment_storage_visit(const GcEnvironmentStorage* storage,
        void* context, GcEnvironmentVisitObjectFn visit_object,
        GcEnvironmentVisitItemFn visit_item) {
    if (!storage || !visit_item) return;
    if (storage->outer && visit_object) visit_object(context, storage->outer);
    if (storage->retained_items) {
        for (uint32_t i = 0; i < storage->retained_item_count; i++) {
            visit_item(context, ((const uint64_t*)(const void*)storage->retained_items)[i]);
        }
    }
    if (storage->item_slots) {
        for (uint32_t i = 0; i < storage->item_count; i++) {
            visit_item(context, ((const uint64_t*)(const void*)storage->item_slots)[i]);
        }
    }
}
