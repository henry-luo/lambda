#include <gtest/gtest.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../lib/byte_storage.h"

typedef struct ReleaseProbe {
    int calls;
    void* expected_data;
    size_t expected_capacity;
} ReleaseProbe;

static void release_probe_callback(void* data, size_t capacity, void* context) {
    ReleaseProbe* probe = (ReleaseProbe*)context;
    probe->calls++;
    EXPECT_EQ(data, probe->expected_data);
    EXPECT_EQ(capacity, probe->expected_capacity);
    free(data);
}

TEST(ByteStorageTest, EmptyStorageHasAValidEmptySpan) {
    ByteStorage* storage = byte_storage_alloc(0, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    EXPECT_EQ(byte_storage_ref_count(storage), 1);
    EXPECT_EQ(storage->data, nullptr);

    ByteSpan span;
    EXPECT_TRUE(byte_span_init(&span, storage, 0, 0));
    EXPECT_EQ(byte_span_data(&span), nullptr);
    byte_storage_release(storage);
}

TEST(ByteStorageTest, NestedSubspansFlattenOntoRootStorage) {
    ByteStorage* storage = byte_storage_alloc(16, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    for (size_t i = 0; i < storage->capacity; i++) storage->data[i] = (uint8_t)i;

    ByteSpan root;
    ByteSpan middle;
    ByteSpan nested;
    ASSERT_TRUE(byte_span_init(&root, storage, 2, 12));
    ASSERT_TRUE(byte_span_subspan(&root, 3, 7, &middle));
    ASSERT_TRUE(byte_span_subspan(&middle, 2, 3, &nested));
    EXPECT_EQ(nested.storage, storage);
    EXPECT_EQ(nested.offset, 7u);
    EXPECT_EQ(nested.length, 3u);
    ASSERT_NE(byte_span_data(&nested), nullptr);
    EXPECT_EQ(byte_span_data(&nested)[0], 7u);
    byte_storage_release(storage);
}

TEST(ByteStorageTest, RejectsOutOfBoundsAndOverflowingRanges) {
    ByteStorage* storage = byte_storage_alloc(8, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    ByteSpan span;
    EXPECT_FALSE(byte_span_init(&span, storage, 9, 0));
    EXPECT_FALSE(byte_span_init(&span, storage, 7, 2));
    EXPECT_FALSE(byte_span_init(&span, storage, SIZE_MAX, 1));
    ASSERT_TRUE(byte_span_init(&span, storage, 1, 6));
    EXPECT_FALSE(byte_span_subspan(&span, 5, 2, &span));
    EXPECT_FALSE(byte_span_subspan(&span, SIZE_MAX, 1, &span));
    byte_storage_release(storage);
}

TEST(ByteStorageTest, LastReleaseInvokesExternalCleanupOnce) {
    void* data = malloc(11);
    ASSERT_NE(data, nullptr);
    ReleaseProbe probe = {0, data, 11};
    ByteStorage* storage = byte_storage_wrap(data, 11, BYTE_STORAGE_FLAG_READ_ONLY,
        release_probe_callback, &probe);
    ASSERT_NE(storage, nullptr);
    EXPECT_NE(storage->flags & BYTE_STORAGE_FLAG_READ_ONLY, 0u);
    EXPECT_EQ(byte_storage_retain(storage), storage);
    EXPECT_EQ(byte_storage_ref_count(storage), 2);
    byte_storage_release(storage);
    EXPECT_EQ(probe.calls, 0);
    byte_storage_release(storage);
    EXPECT_EQ(probe.calls, 1);
}

typedef struct RetainThreadArgs {
    ByteStorage* storage;
    int iterations;
} RetainThreadArgs;

static void* retain_release_thread(void* context) {
    RetainThreadArgs* args = (RetainThreadArgs*)context;
    for (int i = 0; i < args->iterations; i++) {
        ByteStorage* retained = byte_storage_retain(args->storage);
        if (retained) byte_storage_release(retained);
    }
    return NULL;
}

TEST(ByteStorageTest, ConcurrentRetainReleaseKeepsOwnerReference) {
    enum { THREAD_COUNT = 8, ITERATIONS = 10000 };
    ByteStorage* storage = byte_storage_alloc(32, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    pthread_t threads[THREAD_COUNT];
    RetainThreadArgs args = {storage, ITERATIONS};
    for (int i = 0; i < THREAD_COUNT; i++) {
        ASSERT_EQ(pthread_create(&threads[i], NULL, retain_release_thread, &args), 0);
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        ASSERT_EQ(pthread_join(threads[i], NULL), 0);
    }
    EXPECT_EQ(byte_storage_ref_count(storage), 1);
    byte_storage_release(storage);
}

TEST(ByteBufferHandleTest, ResizeReleasesOldStorageAndAdvancesGeneration) {
    int64_t releases_before = byte_storage_release_count();
    ByteBufferHandle handle;
    ASSERT_TRUE(byte_buffer_init(&handle, 4, 12, BYTE_BUFFER_FLAG_RESIZABLE,
        MEM_CAT_CONTAINER));
    uint8_t* data = byte_buffer_prepare_write(&handle);
    ASSERT_NE(data, nullptr);
    data[0] = 7;
    uint64_t generation = handle.generation;

    ASSERT_TRUE(byte_buffer_resize(&handle, 9));
    ASSERT_EQ(handle.byte_length, 9u);
    EXPECT_GT(handle.generation, generation);
    EXPECT_EQ(byte_buffer_data_const(&handle)[0], 7u);
    EXPECT_EQ(byte_buffer_data_const(&handle)[8], 0u);
    EXPECT_EQ(byte_storage_release_count(), releases_before + 1);
    byte_buffer_destroy(&handle);
    EXPECT_EQ(byte_storage_release_count(), releases_before + 2);
}

TEST(ByteBufferHandleTest, PreparedWriteClonesSharedReadonlyStorage) {
    ByteStorage* storage = byte_storage_alloc(4, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    memcpy(storage->data, "abcd", 4);
    storage->flags |= BYTE_STORAGE_FLAG_READ_ONLY;
    ByteBufferHandle handle;
    ASSERT_TRUE(byte_buffer_init_storage(&handle, storage, 1, 2, 2,
        BYTE_BUFFER_FLAG_NONE, MEM_CAT_CONTAINER));
    uint64_t generation = handle.generation;
    uint8_t* data = byte_buffer_prepare_write(&handle);
    ASSERT_NE(data, nullptr);
    EXPECT_GT(handle.generation, generation);
    EXPECT_EQ(handle.storage_offset, 0u);
    data[0] = 'z';
    EXPECT_EQ(storage->data[1], 'b');
    EXPECT_EQ(byte_buffer_data_const(&handle)[0], 'z');
    byte_buffer_destroy(&handle);
    byte_storage_release(storage);
}

TEST(ByteBufferHandleTest, FailedCowLeavesOriginalHandleAndStorageIntact) {
    ByteStorage* storage = byte_storage_alloc(4, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    memcpy(storage->data, "safe", 4);
    ByteBufferHandle handle = {};
    ASSERT_TRUE(byte_buffer_init_storage(&handle, storage, 0, 4, 4,
        BYTE_BUFFER_FLAG_NONE, MEM_CAT_CONTAINER));
    ByteStorage* original = handle.storage;
    uint64_t generation = handle.generation;

    byte_storage_fail_next_allocation_for_test();
    EXPECT_EQ(byte_buffer_prepare_write(&handle), nullptr);
    EXPECT_EQ(handle.storage, original);
    EXPECT_EQ(handle.generation, generation);
    EXPECT_EQ(handle.storage_offset, 0u);
    EXPECT_EQ(memcmp(byte_buffer_data_const(&handle), "safe", 4), 0);
    EXPECT_EQ(byte_storage_ref_count(storage), 2);
    byte_buffer_destroy(&handle);
    byte_storage_release(storage);
}

TEST(ByteBufferHandleTest, TransferDetachesSourceWithoutOrphaningStorage) {
    ByteBufferHandle source = {};
    ByteBufferHandle destination = {};
    ASSERT_TRUE(byte_buffer_init(&source, 3, 6, BYTE_BUFFER_FLAG_RESIZABLE,
        MEM_CAT_CONTAINER));
    uint8_t* data = byte_buffer_prepare_write(&source);
    ASSERT_NE(data, nullptr);
    memcpy(data, "xyz", 3);

    ASSERT_TRUE(byte_buffer_transfer(&source, &destination, 5, false));
    EXPECT_TRUE(byte_buffer_is_detached(&source));
    EXPECT_EQ(destination.byte_length, 5u);
    EXPECT_EQ(destination.max_byte_length, 6u);
    EXPECT_TRUE(byte_buffer_is_resizable(&destination));
    EXPECT_EQ(memcmp(byte_buffer_data_const(&destination), "xyz", 3), 0);
    byte_buffer_destroy(&source);
    byte_buffer_destroy(&destination);
}
