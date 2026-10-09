#include "typeset_marks.hpp"
#include "../lib/mem_grow.hpp"
#include <string.h>

TypesetStatus typeset_mark_append(TypesetMarkStore* store, const TypesetMark* mark) {
    if (!store || !store->pool || !store->provider || !store->generation || !mark ||
        !mark->name || !*mark->name || !mark->page_number || mark->kind > TYPESET_MARK_NATIVE)
        return TYPESET_INVALID;
    if (!typeset_source_in_scope(mark->source, store->provider, store->generation, store->sources))
        return TYPESET_STALE;
    if (store->count && store->entries[store->count - 1].page_number > mark->page_number)
        return TYPESET_INVALID;
    if (!lam::pool_grow_array(store->pool, &store->entries, &store->capacity, store->count + 1, 16))
        return TYPESET_OUT_OF_MEMORY;
    store->entries[store->count++] = *mark;
    return TYPESET_OK;
}

TypesetMarkCheckpoint typeset_marks_checkpoint(const TypesetMarkStore* store) {
    return store ? TypesetMarkCheckpoint{store->provider, store->generation, store->count}
        : TypesetMarkCheckpoint{};
}

TypesetStatus typeset_marks_restore(TypesetMarkStore* store, TypesetMarkCheckpoint checkpoint) {
    if (!store || checkpoint.provider != store->provider || checkpoint.generation != store->generation)
        return TYPESET_STALE;
    if (checkpoint.count > store->count) return TYPESET_INVALID;
    // Discarded assignments must not become the following page's entry value.
    store->count = checkpoint.count;
    return TYPESET_OK;
}

const TypesetMark* typeset_mark_select(const TypesetMarkStore* store, TypesetMarkKind kind,
        const char* name, uint32_t page_number, TypesetMarkSelection selection) {
    if (!store || !name || !page_number || selection > TYPESET_MARK_FIRST_EXCEPT) return nullptr;
    const TypesetMark *entry = nullptr, *first = nullptr, *last = nullptr;
    for (size_t i = 0; i < store->count; i++) {
        const TypesetMark* mark = &store->entries[i];
        if (mark->page_number > page_number) break;
        if (mark->kind != kind || strcmp(mark->name, name) != 0) continue;
        if (mark->page_number < page_number) entry = mark;
        else { if (!first) first = mark; last = mark; }
    }
    switch (selection) {
        case TYPESET_MARK_FIRST: return first ? first : entry;
        case TYPESET_MARK_START: return first && first->at_page_start ? first : entry;
        case TYPESET_MARK_LAST: return last ? last : entry;
        case TYPESET_MARK_FIRST_EXCEPT: return first ? nullptr : entry;
    }
    return nullptr;
}
