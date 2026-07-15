#include "byte_storage.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static atomic_int64 byte_storage_allocations = {0};
static atomic_int64 byte_storage_releases = {0};
static atomic_int32 byte_storage_fail_next_allocation = {0};

static void byte_storage_release_owned(void* data, size_t capacity, void* context) {
    (void)capacity;
    (void)context;
    mem_free(data);
}

static bool byte_storage_range_valid(size_t capacity, size_t offset, size_t length) {
    return offset <= capacity && length <= capacity - offset;
}

ByteStorage* byte_storage_alloc(size_t capacity, MemCategory category) {
    if (__atomic_exchange_n(&byte_storage_fail_next_allocation.v, 0,
            __ATOMIC_SEQ_CST) != 0) {
        return NULL;
    }
    ByteStorage* storage = (ByteStorage*)mem_calloc(1, sizeof(ByteStorage), category);
    if (!storage) return NULL;
    if (capacity > 0) {
        storage->data = (uint8_t*)mem_alloc(capacity, category);
        if (!storage->data) {
            mem_free(storage);
            return NULL;
        }
    }
    atomic_store32(&storage->refs, 1);
    storage->capacity = capacity;
    storage->release_data = byte_storage_release_owned;
    atomic_inc64(&byte_storage_allocations);
    return storage;
}

ByteStorage* byte_storage_wrap(void* data, size_t capacity, uint32_t flags,
        ByteStorageReleaseFn release_data, void* context) {
    if (capacity > 0 && !data) return NULL;
    ByteStorage* storage = (ByteStorage*)mem_calloc(
        1, sizeof(ByteStorage), MEM_CAT_CONTAINER);
    if (!storage) return NULL;
    atomic_store32(&storage->refs, 1);
    storage->data = (uint8_t*)data;
    storage->capacity = capacity;
    storage->flags = flags | BYTE_STORAGE_FLAG_EXTERNAL;
    storage->release_data = release_data;
    storage->release_context = context;
    atomic_inc64(&byte_storage_allocations);
    return storage;
}

ByteStorage* byte_storage_retain(ByteStorage* storage) {
    if (!storage) return NULL;
    int32_t refs = atomic_load32(&storage->refs);
    for (;;) {
        // A zero count is already released; INT32_MAX cannot be retained safely.
        assert(refs > 0 && refs < INT32_MAX);
        if (refs <= 0 || refs == INT32_MAX) return NULL;
        int32_t desired = refs + 1;
        if (__atomic_compare_exchange_n(&storage->refs.v, &refs, desired, false,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            return storage;
        }
    }
}

void byte_storage_release(ByteStorage* storage) {
    if (!storage) return;
    int32_t refs = atomic_load32(&storage->refs);
    for (;;) {
        assert(refs > 0);
        if (refs <= 0) return;
        int32_t desired = refs - 1;
        if (!__atomic_compare_exchange_n(&storage->refs.v, &refs, desired, false,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            continue;
        }
        if (desired == 0) {
            uint8_t* data = storage->data;
            size_t capacity = storage->capacity;
            ByteStorageReleaseFn release_data = storage->release_data;
            void* context = storage->release_context;
            storage->data = NULL;
            storage->capacity = 0;
            storage->release_data = NULL;
            storage->release_context = NULL;
            if (release_data) release_data(data, capacity, context);
            atomic_inc64(&byte_storage_releases);
            mem_free(storage);
        }
        return;
    }
}

int32_t byte_storage_ref_count(const ByteStorage* storage) {
    return storage ? atomic_load32(&storage->refs) : 0;
}

int64_t byte_storage_allocation_count(void) {
    return atomic_load64(&byte_storage_allocations);
}

int64_t byte_storage_release_count(void) {
    return atomic_load64(&byte_storage_releases);
}

void byte_storage_fail_next_allocation_for_test(void) {
    atomic_store32(&byte_storage_fail_next_allocation, 1);
}

bool byte_span_init(ByteSpan* span, ByteStorage* storage,
        size_t offset, size_t length) {
    if (!span || !storage || !byte_storage_range_valid(storage->capacity, offset, length)) {
        return false;
    }
    if (length > 0 && !storage->data) return false;
    span->storage = storage;
    span->offset = offset;
    span->length = length;
    return true;
}

const uint8_t* byte_span_data(const ByteSpan* span) {
    if (!span || !span->storage ||
        !byte_storage_range_valid(span->storage->capacity, span->offset, span->length) ||
        (span->length > 0 && !span->storage->data)) {
        return NULL;
    }
    if (!span->storage->data) return NULL;
    return span->storage->data + span->offset;
}

bool byte_span_subspan(const ByteSpan* source, size_t offset,
        size_t length, ByteSpan* result) {
    if (!source || !result || !source->storage ||
        !byte_storage_range_valid(source->length, offset, length) ||
        !byte_storage_range_valid(source->storage->capacity, source->offset, source->length)) {
        return false;
    }
    size_t absolute_offset = source->offset + offset;
    return byte_span_init(result, source->storage, absolute_offset, length);
}

static bool byte_buffer_range_valid(const ByteBufferHandle* handle) {
    return handle && handle->storage &&
        byte_storage_range_valid(handle->storage->capacity,
            handle->storage_offset, handle->byte_length) &&
        (handle->byte_length == 0 || handle->storage->data);
}

static void byte_buffer_clear(ByteBufferHandle* handle) {
    if (!handle) return;
    memset(handle, 0, sizeof(ByteBufferHandle));
}

bool byte_buffer_init(ByteBufferHandle* handle, size_t byte_length,
        size_t max_byte_length, uint32_t flags, MemCategory category) {
    if (!handle || byte_length > max_byte_length) return false;
    byte_buffer_clear(handle);
    ByteStorage* storage = byte_storage_alloc(byte_length, category);
    if (!storage) return false;
    if (byte_length > 0) memset(storage->data, 0, byte_length);
    if (flags & BYTE_BUFFER_FLAG_SHARED) {
        storage->flags |= BYTE_STORAGE_FLAG_SHARED_MUTABLE;
    }
    handle->storage = storage;
    handle->byte_length = byte_length;
    handle->max_byte_length = max_byte_length;
    handle->generation = 1;
    handle->flags = flags & ~BYTE_BUFFER_FLAG_DETACHED;
    handle->category = category;
    return true;
}

bool byte_buffer_init_storage(ByteBufferHandle* handle, ByteStorage* storage,
        size_t storage_offset, size_t byte_length, size_t max_byte_length,
        uint32_t flags, MemCategory category) {
    if (!handle || !storage || byte_length > max_byte_length ||
        !byte_storage_range_valid(storage->capacity, storage_offset, byte_length) ||
        (byte_length > 0 && !storage->data)) {
        return false;
    }
    ByteStorage* retained = byte_storage_retain(storage);
    if (!retained) return false;
    byte_buffer_clear(handle);
    handle->storage = retained;
    handle->storage_offset = storage_offset;
    handle->byte_length = byte_length;
    handle->max_byte_length = max_byte_length;
    handle->generation = 1;
    handle->flags = flags & ~BYTE_BUFFER_FLAG_DETACHED;
    handle->category = category;
    return true;
}

void byte_buffer_destroy(ByteBufferHandle* handle) {
    if (!handle) return;
    ByteStorage* storage = handle->storage;
    byte_buffer_clear(handle);
    byte_storage_release(storage);
}

bool byte_buffer_is_detached(const ByteBufferHandle* handle) {
    return !handle || (handle->flags & BYTE_BUFFER_FLAG_DETACHED) != 0;
}

bool byte_buffer_is_resizable(const ByteBufferHandle* handle) {
    return handle && (handle->flags & BYTE_BUFFER_FLAG_RESIZABLE) != 0;
}

bool byte_buffer_is_shared(const ByteBufferHandle* handle) {
    return handle && (handle->flags & BYTE_BUFFER_FLAG_SHARED) != 0;
}

const uint8_t* byte_buffer_data_const(const ByteBufferHandle* handle) {
    if (byte_buffer_is_detached(handle) || !byte_buffer_range_valid(handle) ||
        handle->byte_length == 0) {
        return NULL;
    }
    return handle->storage->data + handle->storage_offset;
}

uint8_t* byte_buffer_prepare_write(ByteBufferHandle* handle) {
    if (byte_buffer_is_detached(handle) || !byte_buffer_range_valid(handle) ||
        handle->byte_length == 0) {
        return NULL;
    }
    ByteStorage* storage = handle->storage;
    bool shared = byte_buffer_is_shared(handle);
    bool must_clone = !shared &&
        (byte_storage_ref_count(storage) > 1 ||
         (storage->flags & BYTE_STORAGE_FLAG_READ_ONLY) != 0);
    if (must_clone) {
        ByteStorage* replacement = byte_storage_alloc(handle->byte_length, handle->category);
        if (!replacement) return NULL;
        memcpy(replacement->data, storage->data + handle->storage_offset,
            handle->byte_length);
        // The handle is the coherence point: swapping here keeps every view on
        // the new allocation while immutable snapshots retain the old storage.
        handle->storage = replacement;
        handle->storage_offset = 0;
        handle->generation++;
        byte_storage_release(storage);
    }
    // Non-shared callers may mutate only a private writable allocation; this
    // assertion catches any write path that would violate Binary snapshot COW.
    assert(shared || (byte_storage_ref_count(handle->storage) == 1 &&
        (handle->storage->flags & BYTE_STORAGE_FLAG_READ_ONLY) == 0));
    return handle->storage->data + handle->storage_offset;
}

bool byte_buffer_resize(ByteBufferHandle* handle, size_t new_length) {
    if (byte_buffer_is_detached(handle) || !byte_buffer_range_valid(handle) ||
        !byte_buffer_is_resizable(handle) || new_length > handle->max_byte_length) {
        return false;
    }
    if (new_length == handle->byte_length) return true;
    ByteStorage* replacement = byte_storage_alloc(new_length, handle->category);
    if (!replacement) return false;
    if (byte_buffer_is_shared(handle)) {
        replacement->flags |= BYTE_STORAGE_FLAG_SHARED_MUTABLE;
    }
    size_t copy_length = handle->byte_length < new_length ?
        handle->byte_length : new_length;
    if (copy_length > 0) {
        memcpy(replacement->data, handle->storage->data + handle->storage_offset,
            copy_length);
    }
    if (new_length > copy_length) {
        memset(replacement->data + copy_length, 0, new_length - copy_length);
    }
    ByteStorage* previous = handle->storage;
    handle->storage = replacement;
    handle->storage_offset = 0;
    handle->byte_length = new_length;
    handle->generation++;
    byte_storage_release(previous);
    return true;
}

void byte_buffer_detach(ByteBufferHandle* handle) {
    if (!handle || byte_buffer_is_detached(handle)) return;
    ByteStorage* previous = handle->storage;
    handle->storage = NULL;
    handle->storage_offset = 0;
    handle->byte_length = 0;
    handle->flags |= BYTE_BUFFER_FLAG_DETACHED;
    handle->generation++;
    byte_storage_release(previous);
}

bool byte_buffer_transfer(ByteBufferHandle* source, ByteBufferHandle* destination,
        size_t new_length, bool fixed_length) {
    if (!source || !destination || source == destination ||
        byte_buffer_is_detached(source) || byte_buffer_is_shared(source)) {
        return false;
    }
    bool destination_resizable = !fixed_length && byte_buffer_is_resizable(source);
    // A fixed source transfers into a fixed destination sized to new_length;
    // carrying the source maximum rejects growth and exposes stale maxByteLength.
    size_t max_length = destination_resizable ? source->max_byte_length : new_length;
    if (new_length > max_length) return false;
    uint32_t flags = destination_resizable ? BYTE_BUFFER_FLAG_RESIZABLE :
        BYTE_BUFFER_FLAG_NONE;
    ByteBufferHandle next;
    if (!byte_buffer_init(&next, new_length, max_length, flags, source->category)) {
        return false;
    }
    size_t copy_length = source->byte_length < new_length ?
        source->byte_length : new_length;
    if (copy_length > 0) {
        memcpy(next.storage->data, source->storage->data + source->storage_offset,
            copy_length);
    }
    *destination = next;
    byte_buffer_detach(source);
    return true;
}
