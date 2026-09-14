// byte_builder.h - allocator-aware append-only byte storage.
#ifndef LIB_BYTE_BUILDER_H
#define LIB_BYTE_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memtrack.h"

#ifdef __cplusplus
extern "C" {
#endif

// ByteBuilder owns contiguous bytes through mem_alloc/mem_realloc.  Text
// users may request a trailing NUL without making embedded NULs special.
typedef struct ByteBuilder {
    uint8_t* data;
    size_t length;
    size_t capacity;
    MemCategory category;
    bool nul_terminated;
} ByteBuilder;

bool byte_builder_init(ByteBuilder* builder, size_t initial_capacity,
                       MemCategory category, bool nul_terminated);
bool byte_builder_reserve(ByteBuilder* builder, size_t append_bytes);
bool byte_builder_append(ByteBuilder* builder, const void* data, size_t length);
bool byte_builder_append_limited(ByteBuilder* builder, const void* data,
                                 size_t length, size_t max_length);
uint8_t* byte_builder_take(ByteBuilder* builder, size_t* out_length);
void byte_builder_destroy(ByteBuilder* builder);

#ifdef __cplusplus
}
#endif

#endif
