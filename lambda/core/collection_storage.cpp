#include "collection_storage.h"

#include <string.h>

void list_relocate_owned_tail(List* list, Item* old_items, int64_t old_capacity,
                              Item* new_items, int64_t new_capacity) {
    if (!list || !old_items || !new_items || list->extra <= 0 ||
            old_capacity < list->extra || new_capacity < list->extra) return;

    memmove(new_items + (new_capacity - list->extra),
        old_items + (old_capacity - list->extra),
        (size_t)list->extra * sizeof(Item));

    int64_t dense_capacity = new_capacity - list->extra;
    int64_t dense_count = list->length < dense_capacity ? list->length : dense_capacity;
    for (int64_t i = 0; i < dense_count; i++) {
        Item item = new_items[i];
        if (!(((item._type_id == LMD_TYPE_FLOAT && item.double_ptr > 1) ||
                    item._type_id == LMD_TYPE_FLOAT64) ||
                item._type_id == LMD_TYPE_INT64 || item._type_id == LMD_TYPE_UINT64)) continue;
        Item* old_pointer = (Item*)item.double_ptr;
        if (old_pointer < old_items || old_pointer >= old_items + old_capacity) continue;
        int64_t end_offset = old_items + old_capacity - old_pointer;
        void* new_pointer = new_items + new_capacity - end_offset;
        new_items[i] = {.item = is_float_type_id(item._type_id) ? d2it(new_pointer) :
            item._type_id == LMD_TYPE_INT64 ? l2it(new_pointer) : u2it(new_pointer)};
    }

    // Growth copies the old buffer before moving its owned tail. Clear its
    // vacated source; for JS arrays, also stamp every newly exposed dense slot
    // so a sparse spec length cannot observe stale payloads or zeroed nulls.
    int64_t new_tail_start = new_capacity - list->extra;
    int64_t old_tail_start = old_capacity - list->extra;
    Item vacant = list->has_js_props ?
        Item{.item = ITEM_JS_DELETED_SENTINEL} : ItemNull;
    int64_t vacant_end = list->has_js_props ?
        new_tail_start : old_capacity;
    if (vacant_end > new_tail_start) vacant_end = new_tail_start;
    for (int64_t i = old_tail_start; i < vacant_end; i++) {
        new_items[i] = vacant;
    }
}
