#include "async.h"

#include "transpiler.hpp"

static Array* runtime_async_storage_array(const RuntimeAsyncDeque* deque) {
    if (!deque || !deque->storage_owner) return NULL;
    Item storage = *deque->storage_owner;
    return get_type_id(storage) == LMD_TYPE_ARRAY ? storage.array : NULL;
}

extern "C" void runtime_async_deque_init(RuntimeAsyncDeque* deque,
        Item* storage_owner, int64_t width) {
    if (!deque) return;
    deque->storage_owner = storage_owner;
    deque->head = 0;
    deque->width = width > 0 ? width : 1;
}

extern "C" int64_t runtime_async_deque_size(const RuntimeAsyncDeque* deque) {
    Array* array = runtime_async_storage_array(deque);
    if (!array || deque->head >= array->length) return 0;
    int64_t live = (int64_t)array->length - deque->head;
    return live / deque->width;
}

extern "C" bool runtime_async_deque_push(RuntimeAsyncDeque* deque,
        const Item* record) {
    if (!deque || !deque->storage_owner || !record || deque->width <= 0) return false;
    Item* owner = deque->storage_owner;
    if (get_type_id(*owner) != LMD_TYPE_ARRAY) {
        Item storage = {.array = array()};
        if (!storage.array) return false;
        *owner = storage;
    }

    Array* array_ptr = owner->array;
    int64_t required = array_ptr->length + deque->width;
    while (required > array_ptr->capacity) {
        int64_t old_capacity = array_ptr->capacity;
        expand_list((List*)array_ptr, NULL);
        array_ptr = owner->array;
        if (!array_ptr || array_ptr->capacity <= old_capacity) return false;
    }
    for (int64_t i = 0; i < deque->width; i++) {
        array_set(array_ptr, array_ptr->length + i, record[i]);
    }
    array_ptr->length += (int)deque->width;
    return true;
}

extern "C" bool runtime_async_deque_pop(RuntimeAsyncDeque* deque,
        Item* record) {
    if (!deque || !deque->storage_owner || !record || deque->width <= 0) return false;
    Array* array_ptr = runtime_async_storage_array(deque);
    if (!array_ptr || deque->head + deque->width > array_ptr->length) return false;
    for (int64_t i = 0; i < deque->width; i++) {
        record[i] = array_get(array_ptr, deque->head + i);
        array_ptr->items[deque->head + i] = ItemNull;
    }
    deque->head += deque->width;
    if (deque->head >= array_ptr->length) {
        *deque->storage_owner = ItemNull;
        deque->head = 0;
    }
    return true;
}

extern "C" void runtime_async_deque_clear(RuntimeAsyncDeque* deque) {
    if (!deque || !deque->storage_owner) return;
    *deque->storage_owner = ItemNull;
    deque->head = 0;
}
