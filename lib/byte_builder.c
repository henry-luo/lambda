// byte_builder.c - see byte_builder.h
#include "byte_builder.h"

#include "grow_capacity.h"
#include "mem.h"

#include <string.h>

bool byte_builder_init(ByteBuilder* builder, size_t initial_capacity,
                       MemCategory category, bool nul_terminated) {
    if (!builder) return false;
    memset(builder, 0, sizeof(*builder));
    builder->category = category;
    builder->nul_terminated = nul_terminated;
    return byte_builder_reserve(builder, initial_capacity);
}

bool byte_builder_reserve(ByteBuilder* builder, size_t append_bytes) {
    if (!builder) return false;
    size_t terminator = builder->nul_terminated ? 1u : 0u;
    if (append_bytes > SIZE_MAX - builder->length ||
        terminator > SIZE_MAX - builder->length - append_bytes) return false;
    size_t required = builder->length + append_bytes + terminator;
    if (required <= builder->capacity) return true;
    size_t capacity = 0;
    if (!lib_grow_capacity(builder->capacity, required, 64u, &capacity)) return false;
    uint8_t* data = (uint8_t*)mem_realloc(builder->data, capacity, builder->category);
    if (!data) return false;
    builder->data = data;
    builder->capacity = capacity;
    if (builder->nul_terminated) builder->data[builder->length] = '\0';
    return true;
}

bool byte_builder_append(ByteBuilder* builder, const void* data, size_t length) {
    if (!builder || (length > 0 && !data) || !byte_builder_reserve(builder, length)) return false;
    if (length > 0) memmove(builder->data + builder->length, data, length);
    builder->length += length;
    if (builder->nul_terminated) builder->data[builder->length] = '\0';
    return true;
}

bool byte_builder_append_limited(ByteBuilder* builder, const void* data,
                                 size_t length, size_t max_length) {
    if (!builder || builder->length > max_length || length > max_length - builder->length) {
        return false;
    }
    return byte_builder_append(builder, data, length);
}

uint8_t* byte_builder_take(ByteBuilder* builder, size_t* out_length) {
    if (!builder) return NULL;
    uint8_t* data = builder->data;
    if (out_length) *out_length = builder->length;
    memset(builder, 0, sizeof(*builder));
    return data;
}

void byte_builder_destroy(ByteBuilder* builder) {
    if (!builder) return;
    mem_free(builder->data);
    memset(builder, 0, sizeof(*builder));
}
