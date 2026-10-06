#include "../lambda-data.hpp"
#include "input-allocation-context.h"
#include "mark_builder.hpp"
#include "../core/collection_storage.h"
#include "../input/css/dom_node.hpp"
#include "../../lib/arena.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/math_checked.hpp"

// UI content helpers, shared by this Input-owned append and the runtime's
// list_push: UI element content lives in an arena the collector never traces
// (D4.1.1v2), so what it holds must be owned by the document (D4.5.2).

// Copy a string into `arena` as a fat [DomText][String][chars] node, the form
// the DOM build adopts in place.
Item ui_copy_string_to_arena(Arena* arena, Item str_item) {
    String* src = str_item.get_safe_string();
    if (!src) return str_item;
    DomText* text = DomText::create_in(arena, src->len);
    if (!text) return ItemNull;
    String* dst = dom_text_to_string(text);
    dst->flags = 0;
    dst->is_ascii = src->is_ascii;
    memcpy(dst->chars, src->chars, src->len + 1);
    return {.item = s2it(dst)};
}

// S2.6.4 merge of adjacent content strings into one fat DomText on `arena`.
Item ui_merge_strings_to_arena(Arena* arena, String* prev, String* next) {
    size_t new_len = prev->len + next->len;
    DomText* text = DomText::create_in(arena, new_len);
    if (!text) return ItemNull;
    String* merged = dom_text_to_string(text);
    merged->flags = 0;
    merged->is_ascii = prev->is_ascii && next->is_ascii;
    memcpy(merged->chars, prev->chars, prev->len);
    memcpy(merged->chars + prev->len, next->chars, next->len);
    merged->chars[new_len] = '\0';
    return {.item = s2it(merged)};
}

// Deep-copy a non-string content value into `owner` unless the owner's arena or
// pool already holds it: a GC symbol, array, element or other boxed value left
// in UI content dangles once collected, while the DOM build and every later
// rebuild read it again. Wide numbers need no copy (array_set rebases them into
// the list's own storage), and an owned container needs no walk because its
// content was normalized when it was built.
Item ui_copy_content_to_input(Input* owner, Item item) {
    void* storage = nullptr;
    switch (get_type_id(item)) {
    case LMD_TYPE_SYMBOL:  storage = item.get_safe_symbol();  break;
    case LMD_TYPE_BINARY:  case LMD_TYPE_DECIMAL:  case LMD_TYPE_DTIME:
        storage = (void*)item.string_ptr;  break;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:  case LMD_TYPE_RANGE:
        storage = item.array;  break;
    case LMD_TYPE_MAP:  storage = item.map;  break;
    case LMD_TYPE_ELEMENT:  storage = item.element;  break;
    default:  return item;
    }
    if (!storage || !owner) return item;
    if (arena_owns(owner->arena, storage) || pool_owns(owner->pool, storage)) return item;
    MarkBuilder builder(owner);
    return builder.deep_copy(item);
}

bool list_grow_io(List* list, int64_t min_capacity, Pool* pool, Arena* arena) {
    if (!list || (!pool && !arena)) return false;
    int64_t previous_capacity = list->capacity;
    int64_t new_capacity = previous_capacity ? previous_capacity * 2 : 8;
    if (new_capacity < min_capacity) new_capacity = min_capacity;
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
    // S2.6.4: an element always appends content, including parser-owned elements.
    if (arr->type_id == LMD_TYPE_ELEMENT) {
        InputAllocationContext* owner = input_allocation_context;
        Input* ui_input = owner && owner->pool == pool && owner->arena == arena && owner->ui_mode
            ? owner->input : nullptr;
        list_push_with_owner((List*)arr, item, pool, arena, ui_input);
        return;
    }
    if (arr->length + arr->extra + 2 > arr->capacity &&
            !list_grow_io((List*)arr, 0, pool, arena)) return;
    array_set(arr, arr->length, item);
    arr->length++;
}

void list_push_with_owner(List* list, Item item, Pool* pool, Arena* arena,
        Input* ui_input) {
    if (!list || (!pool && !arena)) return;
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_NULL) return;

    if (type_id == LMD_TYPE_ARRAY) {
        List* nested = item.array;
        if (nested && nested->is_spreadable) {
            if (!nested->items) {
                if (nested->length == 0) return;
                log_error("list_push_io: content list has no backing storage");
                return;
            }
            for (int64_t i = 0; i < nested->length; i++) {
                list_push_with_owner(list, nested->items[i], pool, arena, ui_input);
            }
            return;
        }
    }

    bool ui_mode = ui_input && arena;
    if (type_id == LMD_TYPE_STRING) {
        String* text = item.get_safe_string();
        if (text && text->len == 0) return;  // S2.6.2: empty text contributes no content
        if (ui_mode) item = ui_copy_string_to_arena(arena, item);
    } else if (ui_mode && list->type_id == LMD_TYPE_ELEMENT) {
        // D4.5.2: element content is document-owned, as in the runtime's list_push
        item = ui_copy_content_to_input(ui_input, item);
    }
    if (type_id == LMD_TYPE_STRING && list->length > 0 && list->items) {
        String* previous = list->items[list->length - 1].get_safe_string();
        String* next = item.get_safe_string();
        if (previous && next) {
            if (ui_mode) {
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
            merged->flags = 0;
            merged->is_ascii = previous->is_ascii && next->is_ascii;
            list->items[list->length - 1] = {.item = s2it(merged)};
            return;
        }
    }

    if (list->length + list->extra + 2 > list->capacity &&
            !list_grow_io(list, 0, pool, arena)) return;
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
        allocation->ui_mode ? allocation->input : nullptr);
}

void list_push_pooled(List* list, Item item, Pool* pool) {
    list_push_with_owner(list, item, pool, nullptr, nullptr);
}
