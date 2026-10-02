#include <gtest/gtest.h>
#include "../lambda/io/mark_output_builder.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/core/binary.h"

namespace {

static int destroyed_output_builders;

static void release_output_builder(void* obj, uint16_t type_tag) {
    if (type_tag == LMD_TYPE_ELEMENT && container_is_virtual_list((Container*)obj) &&
            ((VirtualOutputElement*)obj)->builder) destroyed_output_builders++;
    heap_gc_destroy_external_payload(obj, type_tag);
}

} // namespace

TEST(PdfFileBuilder, CopiesBorrowedBytesWithoutItemsAndSurvivesCollection) {
    gc_heap_t* gc = gc_heap_create();
    ASSERT_NE(gc, nullptr);
    destroyed_output_builders = 0;
    gc->external_destroy = release_output_builder;
    VirtualOutputElement* file = (VirtualOutputElement*)gc_heap_calloc(
        gc, sizeof(VirtualOutputElement), LMD_TYPE_ELEMENT);
    ASSERT_NE(file, nullptr);
    file->type_id = LMD_TYPE_ELEMENT;
    file->builder = pdf_builder_create();
    ASSERT_NE(file->builder, nullptr);
    file->is_virtual = 1;

    ByteStorage* storage = byte_storage_alloc(3, MEM_CAT_CONTAINER);
    ASSERT_NE(storage, nullptr);
    const uint8_t bytes[] = {0, 0xff, 0x41};
    memcpy(storage->data, bytes, sizeof(bytes));
    Binary binary = {};
    ASSERT_TRUE(binary_init_storage(&binary, storage, 0, 3, true));
    byte_storage_release(storage);
    for (int i = 0; i < 4096; i++) list_push(file, {.item = x2it(&binary)});
    binary_release_storage(&binary);
    EXPECT_EQ(file->items, nullptr);
    EXPECT_EQ(file->capacity, 0);
    Item result = list_end(file);
    EXPECT_EQ(result.element, file);

    uint64_t roots[] = {result.item};
    gc_collect(gc, roots, 1);
    EXPECT_EQ(destroyed_output_builders, 0);
    const char* data = nullptr;
    size_t size = 0;
    ASSERT_TRUE(virtual_output_bytes(result, &data, &size));
    ASSERT_EQ(size, 4096u * sizeof(bytes));
    for (size_t i = 0; i < size; i += sizeof(bytes)) {
        ASSERT_EQ(memcmp(data + i, bytes, sizeof(bytes)), 0);
    }
    gc_collect(gc, nullptr, 0);
    EXPECT_EQ(destroyed_output_builders, 1);
    gc_heap_destroy(gc);
    EXPECT_EQ(destroyed_output_builders, 1);
}

TEST(PdfFileBuilder, CopiesInputOwnedDictionaryBeforeItsInputIsDestroyed) {
    MarkOutputBuilder* output = pdf_builder_create();
    ASSERT_NE(output, nullptr);
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    {
        MarkBuilder source(input);
        const uint8_t bytes[] = {0, 0xff};
        Item dictionary = source.map()
            .put("Text", source.createStringItem("a(b)"))
            .put("Bytes", {.item = x2it(source.createBinary(bytes, sizeof(bytes)))})
            .put("Wide", (int64_t)INT64_MAX)
            .final();
        output->ops->append(output, dictionary);
    }
    // encoded output has no remaining edge into the destroyed Input arena.
    pool_destroy(pool);
    ASSERT_TRUE(output->ops->finish(output));
    const char* data = nullptr;
    size_t size = 0;
    ASSERT_TRUE(output->ops->bytes(output, &data, &size));
    const char expected[] = " << /Text (a\\(b\\)) /Bytes <00FF> /Wide 9223372036854775807 >> ";
    ASSERT_EQ(size, sizeof(expected) - 1);
    EXPECT_EQ(memcmp(data, expected, size), 0);
    output->ops->destroy(output);
}

TEST(PdfFileBuilder, RejectsCyclicDictionariesWithoutPublishingPartialBytes) {
    Map map = {};
    Map* self = &map;
    StrView name = strview_init("Cycle", 5);
    ShapeEntry field = {};
    field.name = &name;
    shape_entry_set_type(&field, &TYPE_MAP);
    TypeMap type = {};
    type.shape = type.last = &field;
    type.length = 1;
    map.type_id = LMD_TYPE_MAP;
    map.type = &type;
    map.data = &self;
    MarkOutputBuilder* output = pdf_builder_create();
    ASSERT_NE(output, nullptr);
    output->ops->append(output, {.map = &map});
    EXPECT_FALSE(output->ops->finish(output));
    const char* data = nullptr;
    size_t size = 0;
    EXPECT_FALSE(output->ops->bytes(output, &data, &size));
    output->ops->destroy(output);
}

TEST(PdfFileBuilder, RejectsDictionaryMetadataAllocationFailure) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    {
        MarkBuilder source(input);
        Item dictionary = source.map().put("Count", (int64_t)1).final();
        MarkOutputBuilder* output = pdf_builder_create();
        ASSERT_NE(output, nullptr);
        memtrack_fault_inject(0);
        output->ops->append(output, dictionary);
        memtrack_fault_clear();
        EXPECT_FALSE(output->ops->finish(output));
        const char* data = nullptr;
        size_t size = 0;
        EXPECT_FALSE(output->ops->bytes(output, &data, &size));
        output->ops->destroy(output);
    }
    pool_destroy(pool);
}
