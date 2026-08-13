#pragma once

// Runtime-owned async storage primitives. This header deliberately depends
// only on Lambda's Item/Array representation; JS and Lambda task policy stay
// in their respective frontends (D6.3.1, JR7.1).
#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RuntimeAsyncDeque {
    Item* storage_owner;
    int64_t head;
    int64_t width;
} RuntimeAsyncDeque;

void runtime_async_deque_init(RuntimeAsyncDeque* deque, Item* storage_owner,
                              int64_t width);
bool runtime_async_deque_push(RuntimeAsyncDeque* deque, const Item* record);
bool runtime_async_deque_pop(RuntimeAsyncDeque* deque, Item* record);
int64_t runtime_async_deque_size(const RuntimeAsyncDeque* deque);
void runtime_async_deque_clear(RuntimeAsyncDeque* deque);

#ifdef __cplusplus
}
#endif
