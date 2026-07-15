// Shared refcounted byte storage for immutable spans and mutable buffer handles.
#ifndef LIB_BYTE_STORAGE_H
#define LIB_BYTE_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atomic.h"
#include "memtrack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ByteStorageReleaseFn)(void* data, size_t capacity, void* context);

enum ByteStorageFlags {
    BYTE_STORAGE_FLAG_NONE = 0,
    BYTE_STORAGE_FLAG_READ_ONLY = 1u << 0,
    BYTE_STORAGE_FLAG_SHARED_MUTABLE = 1u << 1,
    BYTE_STORAGE_FLAG_EXTERNAL = 1u << 2,
};

typedef struct ByteStorage {
    atomic_int32 refs;
    uint8_t* data;
    size_t capacity;
    uint32_t flags;
    ByteStorageReleaseFn release_data;
    void* release_context;
} ByteStorage;

// ByteSpan is borrowed: creating or deriving one does not retain its storage.
typedef struct ByteSpan {
    ByteStorage* storage;
    size_t offset;
    size_t length;
} ByteSpan;

enum ByteBufferFlags {
    BYTE_BUFFER_FLAG_NONE = 0,
    BYTE_BUFFER_FLAG_DETACHED = 1u << 0,
    BYTE_BUFFER_FLAG_RESIZABLE = 1u << 1,
    BYTE_BUFFER_FLAG_SHARED = 1u << 2,
};

// A ByteBufferHandle has stable identity while its retained storage may be
// replaced by resize, detach, transfer, or copy-on-write.
typedef struct ByteBufferHandle {
    ByteStorage* storage;
    size_t storage_offset;
    size_t byte_length;
    size_t max_byte_length;
    uint64_t generation;
    uint32_t flags;
    MemCategory category;
} ByteBufferHandle;

ByteStorage* byte_storage_alloc(size_t capacity, MemCategory category);
ByteStorage* byte_storage_wrap(void* data, size_t capacity, uint32_t flags,
    ByteStorageReleaseFn release_data, void* context);
ByteStorage* byte_storage_retain(ByteStorage* storage);
void byte_storage_release(ByteStorage* storage);
int32_t byte_storage_ref_count(const ByteStorage* storage);
int64_t byte_storage_allocation_count(void);
int64_t byte_storage_release_count(void);
void byte_storage_fail_next_allocation_for_test(void);

bool byte_span_init(ByteSpan* span, ByteStorage* storage,
    size_t offset, size_t length);
const uint8_t* byte_span_data(const ByteSpan* span);
bool byte_span_subspan(const ByteSpan* source, size_t offset,
    size_t length, ByteSpan* result);

bool byte_buffer_init(ByteBufferHandle* handle, size_t byte_length,
    size_t max_byte_length, uint32_t flags, MemCategory category);
bool byte_buffer_init_storage(ByteBufferHandle* handle, ByteStorage* storage,
    size_t storage_offset, size_t byte_length, size_t max_byte_length,
    uint32_t flags, MemCategory category);
void byte_buffer_destroy(ByteBufferHandle* handle);
const uint8_t* byte_buffer_data_const(const ByteBufferHandle* handle);
uint8_t* byte_buffer_prepare_write(ByteBufferHandle* handle);
bool byte_buffer_resize(ByteBufferHandle* handle, size_t new_length);
void byte_buffer_detach(ByteBufferHandle* handle);
bool byte_buffer_transfer(ByteBufferHandle* source, ByteBufferHandle* destination,
    size_t new_length, bool fixed_length);
bool byte_buffer_is_detached(const ByteBufferHandle* handle);
bool byte_buffer_is_resizable(const ByteBufferHandle* handle);
bool byte_buffer_is_shared(const ByteBufferHandle* handle);

#ifdef __cplusplus
}
#endif

#endif
