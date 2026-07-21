#include "../lambda-data.hpp"
#include "input-allocation-context.h"
#include "../core/collection_storage.h"
#include "../input/css/dom_node.hpp"
#include "../../lib/arena.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/checked_math.hpp"

static Item ui_copy_string_to_arena(Arena* arena, Item str_item) {
    String* src = str_item.get_safe_string();
    if (!src) return str_item;
    DomText* text = DomText::create_in(arena, src->len);
    if (!text) return ItemNull;
    String* dst = dom_text_to_string(text);
    dst->is_ascii = src->is_ascii;
    memcpy(dst->chars, src->chars, src->len + 1);
    return {.item = s2it(dst)};
}

static Item ui_merge_strings_to_arena(Arena* arena, String* prev, String* next) {
    size_t new_len = prev->len + next->len;
    DomText* text = DomText::create_in(arena, new_len);
    if (!text) return ItemNull;
    String* merged = dom_text_to_string(text);
    merged->is_ascii = prev->is_ascii && next->is_ascii;
    memcpy(merged->chars, prev->chars, prev->len);
    memcpy(merged->chars + prev->len, next->chars, next->len);
    merged->chars[new_len] = '\0';
    return {.item = s2it(merged)};
}

static bool expand_list_io(List* list, Pool* pool, Arena* arena) {
    if (!list || (!pool && !arena)) return false;
    int64_t previous_capacity = list->capacity;
    int64_t new_capacity = previous_capacity ? previous_capacity * 2 : 8;
    size_t new_size;
    if (!lam::checked_mul((size_t)new_capacity, sizeof(Item), &new_size)) return false;

    Item* old_items = list->items;
    Item* new_items = arena ? (Item*)arena_alloc(arena, new_size)
                            : (Item*)pool_calloc(pool, new_size);
    if (!new_items) return false;
    if (old_items && previous_capacity > 0) {
        memcpy(new_items, old_items, (size_t)previous_capacity * sizeof(Item));
    }
    list->items = new_items;
    list->capacity = new_capacity;
    // Input owners do not collect. Rebase only storage-local wide scalars after
    // moving their backing buffer, so a parser never publishes stale interiors.
    list_relocate_owned_tail(list, old_items, previous_capacity, new_items, new_capacity);
    return true;
}

void array_append(Array* arr, Item item, Pool* pool, Arena* arena) {
    if (!arr || (!pool && !arena)) return;
    if (arr->length + arr->extra + 2 > arr->capacity &&
            !expand_list_io((List*)arr, pool, arena)) return;
    array_set(arr, arr->length, item);
    arr->length++;
}

static void list_push_with_owner(List* list, Item item, Pool* pool, Arena* arena,
        bool disable_string_merging, bool ui_mode) {
    if (!list || (!pool && !arena)) return;
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_NULL) return;

    if (type_id == LMD_TYPE_ARRAY) {
        List* nested = item.array;
        if (nested && nested->is_content) {
            if (!nested->items) {
                if (nested->length == 0) return;
                log_error("list_push_io: content list has no backing storage");
                return;
            }
            for (int64_t i = 0; i < nested->length; i++) {
                list_push_with_owner(list, nested->items[i], pool, arena,
                    disable_string_merging, ui_mode);
            }
            return;
        }
    }

    if (type_id == LMD_TYPE_STRING && list->is_content && ui_mode && arena) {
        item = ui_copy_string_to_arena(arena, item);
    }
    if (type_id == LMD_TYPE_STRING && !disable_string_merging && list->length > 0 &&
            list->items) {
        String* previous = list->items[list->length - 1].get_safe_string();
        String* next = item.get_safe_string();
        if (previous && next) {
            if (ui_mode && arena) {
                list->items[list->length - 1] = ui_merge_strings_to_arena(arena, previous, next);
                return;
            }
            size_t new_len = previous->len + next->len;
            String* merged = (String*)(arena ? arena_alloc(arena, sizeof(String) + new_len + 1)
                : pool_calloc(pool, sizeof(String) + new_len + 1));
            if (!merged) return;
            memcpy(merged->chars, previous->chars, previous->len);
            memcpy(merged->chars + previous->len, next->chars, next->len);
            merged->chars[new_len] = '\0';
            merged->len = new_len;
            merged->is_ascii = previous->is_ascii && next->is_ascii;
            list->items[list->length - 1] = {.item = s2it(merged)};
            return;
        }
    }

    if (list->length + list->extra + 2 > list->capacity &&
            !expand_list_io(list, pool, arena)) return;
    array_set((Array*)list, list->length, item);
    list->length++;
}

void list_push_io(List* list, Item item) {
    InputAllocationContext* allocation = input_allocation_context;
    if (!allocation || (!allocation->pool && !allocation->arena)) {
        log_error("list_push_io: missing input allocation owner");
        return;
    }
    list_push_with_owner(list, item, allocation->pool, allocation->arena,
        allocation->disable_string_merging, allocation->ui_mode);
}

void list_push_pooled(List* list, Item item, Pool* pool) {
    list_push_with_owner(list, item, pool, nullptr, false, false);
}
