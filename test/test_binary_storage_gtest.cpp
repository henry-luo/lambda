#include <gtest/gtest.h>

#include <string.h>

#include "../lambda/lambda.hpp"
#include "../lambda/binary.h"
#include "../lib/log.h"

extern "C" {
#include "../lib/gc/gc_heap.h"
}

namespace {

static void release_binary_payload(void* obj, uint16_t type_tag) {
    if (type_tag == LMD_TYPE_BINARY) binary_release_storage((Binary*)obj);
}

} // namespace

TEST(BinaryStorage, NestedSlicesShareRootStorageWithoutPayloadCopy) {
    const char bytes[] = {'a', 'b', '\0', 'c', 'd', 'e', 'f'};
    ByteStorage* storage = byte_storage_alloc(sizeof(bytes), MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    memcpy(storage->data, bytes, sizeof(bytes));
    Binary source = {};
    ASSERT_TRUE(binary_init_storage(&source, storage, 0, sizeof(bytes), true));
    byte_storage_release(storage);
    ByteSpan source_span = binary_span(&source);
    ASSERT_NE(source_span.storage, nullptr);

    int64_t allocations = byte_storage_allocation_count();
    int64_t copies = binary_payload_copy_count();
    Binary first = {};
    Binary nested = {};
    ASSERT_TRUE(binary_init_slice(&first, &source, 1, 5));
    ASSERT_TRUE(binary_init_slice(&nested, &first, 1, 3));

    ByteSpan first_span = binary_span(&first);
    ByteSpan nested_span = binary_span(&nested);
    EXPECT_EQ(first_span.storage, source_span.storage);
    EXPECT_EQ(nested_span.storage, source_span.storage);
    EXPECT_EQ(first_span.offset, source_span.offset + 1);
    EXPECT_EQ(nested_span.offset, source_span.offset + 2);
    EXPECT_EQ(nested_span.length, 3u);
    EXPECT_EQ(binary_data(&nested)[0], 0u);
    EXPECT_EQ(binary_data(&nested)[1], (uint8_t)'c');
    EXPECT_EQ(binary_data(&nested)[2], (uint8_t)'d');
    EXPECT_EQ(byte_storage_allocation_count(), allocations);
    EXPECT_EQ(binary_payload_copy_count(), copies);
    EXPECT_EQ(byte_storage_ref_count(source_span.storage), 3);
    binary_release_storage(&nested);
    binary_release_storage(&first);
    binary_release_storage(&source);
}

TEST(BinaryStorage, FullEmptyAndInvalidSlicesPreserveBounds) {
    ByteStorage* storage = byte_storage_alloc(5, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    memcpy(storage->data, "bytes", 5);
    Binary source = {};
    ASSERT_TRUE(binary_init_storage(&source, storage, 0, 5, true));
    byte_storage_release(storage);
    ByteSpan source_span = binary_span(&source);

    Binary full = {};
    Binary empty = {};
    Binary invalid = {};
    ASSERT_TRUE(binary_init_slice(&full, &source, 0, 5));
    ASSERT_TRUE(binary_init_slice(&empty, &source, 5, 0));
    EXPECT_EQ(binary_span(&full).storage, source_span.storage);
    EXPECT_EQ(binary_span(&empty).storage, source_span.storage);
    EXPECT_EQ(binary_length(&full), 5u);
    EXPECT_EQ(binary_length(&empty), 0u);
    EXPECT_FALSE(binary_init_slice(&invalid, &source, 6, 0));
    EXPECT_FALSE(binary_init_slice(&invalid, &source, 4, 2));
    binary_release_storage(&empty);
    binary_release_storage(&full);
    binary_release_storage(&source);
}

TEST(BinaryStorage, MutableHandleSharesThenDivergesFromImmutableSnapshot) {
    ByteStorage* storage = byte_storage_alloc(6, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    memcpy(storage->data, "abcdef", 6);
    Binary snapshot = {};
    ASSERT_TRUE(binary_init_storage(&snapshot, storage, 1, 4, true));
    byte_storage_release(storage);

    ByteSpan span = binary_span(&snapshot);
    ByteBufferHandle handle = {};
    ASSERT_TRUE(byte_buffer_init_storage(&handle, span.storage, span.offset,
        span.length, span.length, BYTE_BUFFER_FLAG_NONE, MEM_CAT_CONTAINER));
    EXPECT_EQ(handle.storage, span.storage);
    EXPECT_EQ(handle.storage_offset, span.offset);
    EXPECT_EQ(byte_buffer_data_const(&handle), binary_data(&snapshot));

    uint8_t* writable = byte_buffer_prepare_write(&handle);
    ASSERT_NE(writable, nullptr);
    EXPECT_NE(handle.storage, span.storage);
    EXPECT_EQ(handle.storage_offset, 0u);
    writable[0] = 'z';
    EXPECT_EQ(memcmp(binary_data(&snapshot), "bcde", 4), 0);
    EXPECT_EQ(memcmp(byte_buffer_data_const(&handle), "zcde", 4), 0);

    byte_buffer_destroy(&handle);
    binary_release_storage(&snapshot);
}

TEST(BinaryStorage, LiveSubviewRetainsStorageAfterSourceCollection) {
    char bytes[4096];
    memset(bytes, 0x5a, sizeof(bytes));
    ByteStorage* storage = byte_storage_alloc(sizeof(bytes), MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    memcpy(storage->data, bytes, sizeof(bytes));
    gc_heap_t* gc = gc_heap_create();
    ASSERT_NE(gc, nullptr);
    gc->external_destroy = release_binary_payload;
    Binary* source = (Binary*)gc_heap_calloc(gc, sizeof(Binary), LMD_TYPE_BINARY);
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(binary_init_storage(source, storage, 0, sizeof(bytes), true));
    byte_storage_release(storage);
    Binary* view = (Binary*)gc_heap_calloc(gc, sizeof(Binary), LMD_TYPE_BINARY);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(binary_init_slice(view, source, 2048, 1));
    storage = binary_span(view).storage;
    ASSERT_NE(storage, nullptr);
    ASSERT_EQ(byte_storage_ref_count(storage), 2);

    uint64_t roots[] = {x2it(view)};
    source = nullptr;
    gc_collect(gc, roots, 1, 0, 0);

    EXPECT_EQ(byte_storage_ref_count(storage), 1);
    EXPECT_EQ(binary_data(view)[0], 0x5au);

    int64_t releases = byte_storage_release_count();
    roots[0] = ITEM_NULL;
    view = nullptr;
    gc_collect(gc, roots, 1, 0, 0);
    EXPECT_EQ(byte_storage_release_count(), releases + 1);
    gc_heap_destroy(gc);
}

TEST(ArrayNumStorage, RetainedViewOwnsAStableStorageReference) {
    ByteStorage* storage = byte_storage_alloc(8, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    for (size_t i = 0; i < 8; i++) storage->data[i] = (uint8_t)(10 + i);
    ArrayNum view = {};
    uint8_t shape_bytes[sizeof(ArrayNumShape) + 2 * sizeof(int64_t)] = {};
    ArrayNumShape* shape = (ArrayNumShape*)shape_bytes;
    ASSERT_TRUE(array_num_init_storage_view(&view, shape, storage,
        ELEM_UINT8, 2, 4, true));
    EXPECT_EQ(byte_storage_ref_count(storage), 2);
    EXPECT_EQ(shape->backing_kind, ARRAY_NUM_BACKING_BYTE_STORAGE);
    EXPECT_EQ(shape->backing, storage);
    EXPECT_EQ(shape->offset, 2);
    EXPECT_EQ(view.data, storage->data + 2);
    EXPECT_EQ(view.length, 4);

    byte_storage_release((ByteStorage*)shape->backing);
    shape->backing = nullptr;
    byte_storage_release(storage);
}

TEST(ArrayNumStorage, ReadonlyStorageRejectsMutableViewsAndWrites) {
    uint8_t bytes[4] = {1, 2, 3, 4};
    ByteStorage* storage = byte_storage_wrap(bytes, sizeof(bytes),
        BYTE_STORAGE_FLAG_READ_ONLY | BYTE_STORAGE_FLAG_EXTERNAL, NULL, NULL);
    ASSERT_NE(storage, nullptr);
    ArrayNum mutable_view = {};
    uint8_t mutable_shape_bytes[sizeof(ArrayNumShape) + 2 * sizeof(int64_t)] = {};
    EXPECT_FALSE(array_num_init_storage_view(&mutable_view,
        (ArrayNumShape*)mutable_shape_bytes, storage, ELEM_UINT8, 0, 4, true));

    ArrayNum readonly = {};
    uint8_t readonly_shape_bytes[sizeof(ArrayNumShape) + 2 * sizeof(int64_t)] = {};
    ArrayNumShape* readonly_shape = (ArrayNumShape*)readonly_shape_bytes;
    ASSERT_TRUE(array_num_init_storage_view(&readonly, readonly_shape,
        storage, ELEM_UINT8, 0, 4, false));
    EXPECT_EQ(readonly.data, bytes);
    EXPECT_FALSE(readonly.is_mutable_view);

    byte_storage_release((ByteStorage*)readonly_shape->backing);
    byte_storage_release(storage);
}
