#include "binary.h"

#include <string.h>

#include "../lib/mempool.h"
#include "../lib/str.h"

static atomic_int64 binary_payload_copies = {0};

extern "C" const uint8_t* binary_data(const Binary* binary) {
    if (!binary) return NULL;
    if (binary->flags & BINARY_FLAG_INLINE) return binary->inline_bytes;
    if (!binary->storage || binary->offset > binary->storage->capacity ||
        binary->len > binary->storage->capacity - binary->offset) return NULL;
    return binary->storage->data ? binary->storage->data + binary->offset : NULL;
}

extern "C" uint32_t binary_length(const Binary* binary) {
    return binary ? binary->len : 0;
}

extern "C" bool binary_is_ascii(const Binary* binary) {
    return binary && binary->is_ascii;
}

extern "C" ByteSpan binary_span(const Binary* binary) {
    ByteSpan span = {0};
    if (!binary || !binary->storage) return span;
    byte_span_init(&span, binary->storage, binary->offset, binary->len);
    return span;
}

extern "C" bool binary_init_storage(Binary* binary, ByteStorage* storage,
        size_t offset, size_t length, bool is_ascii) {
    if (!binary || !storage || length > UINT32_MAX ||
        offset > storage->capacity || length > storage->capacity - offset ||
        (length > 0 && !storage->data)) return false;
    if (!byte_storage_retain(storage)) return false;
    binary->len = (uint32_t)length;
    binary->is_ascii = is_ascii ? 1 : 0;
    binary->flags = BINARY_FLAG_NONE;
    binary->reserved = 0;
    binary->storage = storage;
    binary->offset = offset;
    return true;
}

extern "C" bool binary_init_slice(Binary* binary, const Binary* source,
        size_t offset, size_t length) {
    if (!binary || !source || !source->storage || offset > source->len ||
        length > source->len - offset) return false;
    const uint8_t* data = binary_data(source);
    if (length > 0 && !data) return false;
    return binary_init_storage(binary, source->storage, source->offset + offset,
        length, length == 0 || str_is_ascii((const char*)data + offset, length));
}

extern "C" void binary_release_storage(Binary* binary) {
    if (!binary || !binary->storage) return;
    ByteStorage* storage = binary->storage;
    // Clear ownership before releasing so repeated teardown paths are idempotent.
    binary->storage = NULL;
    binary->offset = 0;
    byte_storage_release(storage);
}

extern "C" int64_t binary_payload_copy_count(void) {
    return atomic_load64(&binary_payload_copies);
}

extern "C" void binary_record_payload_copy(void) {
    atomic_inc64(&binary_payload_copies);
}

extern "C" Binary* pool_binary_from_bytes(Pool* pool, const void* src, size_t len) {
    if (!pool || (!src && len > 0) || len > UINT32_MAX ||
        len > SIZE_MAX - sizeof(Binary) - 1) return NULL;
    Binary* binary = (Binary*)pool_alloc(pool, sizeof(Binary) + len + 1);
    if (!binary) return NULL;
    binary->len = (uint32_t)len;
    binary->is_ascii = (len == 0 || str_is_ascii((const char*)src, len)) ? 1 : 0;
    binary->flags = BINARY_FLAG_INLINE;
    binary->reserved = 0;
    binary->storage = NULL;
    binary->offset = 0;
    if (len > 0) memcpy(binary->inline_bytes, src, len);
    binary->inline_bytes[len] = 0;
    return binary;
}
