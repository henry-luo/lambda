#include <gtest/gtest.h>
#include "../lambda/io/mark_output_builder.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/core/binary.h"
#include "../lib/shell.h"
#include "../lib/file.h"
#include "../lib/str.h"

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

TEST(PdfFileBuilder, CountingDestinationSharesEncodingAndRetainsNoBytes) {
    MarkOutputBuilder* output = pdf_builder_create();
    MarkOutputBuilder* count = pdf_builder_create(true);
    ASSERT_NE(output, nullptr);
    ASSERT_NE(count, nullptr);
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    {
        MarkBuilder source(input);
        const uint8_t bytes[] = {0, 0xff, 0x41};
        Item dictionary = source.map()
            .put("Text", source.createStringItem("a(b)\\\n\t"))
            .put("Unicode", source.createStringItem("λ😀"))
            .put("Bytes", {.item = x2it(source.createBinary(bytes, sizeof(bytes)))})
            .put("Wide", (int64_t)INT64_MAX)
            .put("Small", 1e-12)
            .final();
        output->ops->append(output, dictionary);
        count->ops->append(count, dictionary);
    }
    pool_destroy(pool);
    ASSERT_TRUE(output->ops->finish(output));
    ASSERT_TRUE(count->ops->finish(count));
    const char* data = nullptr;
    size_t size = 0;
    ASSERT_TRUE(output->ops->bytes(output, &data, &size));
    EXPECT_EQ(count->ops->size(count), size);
    EXPECT_FALSE(count->ops->bytes(count, &data, &size));
    output->ops->destroy(output);
    count->ops->destroy(count);
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

// The written PDF is checked by an independent reader (test/pdf_output_check.py):
// object offsets and stream lengths, the fixture's own semantics, and the same
// objects as its accepted golden PDF with streams inflated and paired raster
// color/alpha samples compared at premultiplied 8-bit precision. The golden
// was rendered and sampled with poppler when it was accepted (`accept` mode).
TEST(PdfFileOutput, LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers) {
    const struct {
        const char* script;
        const char* file;
        const char* page_count;
        const char* golden;
        const char* golden_pdf;
    } fixtures[] = {
        {"test/lambda/pdf/output_resources.ls", "temp/pdf_lambda_resources.pdf", nullptr,
            "test/lambda/pdf/output_resources.txt", "test/lambda/pdf/output_resources.golden.pdf"},
        {"test/lambda/pdf/output_phase6.ls", "temp/pdf_lambda_phase6.pdf", "phase6",
            "test/lambda/pdf/output_phase6.txt", "test/lambda/pdf/output_phase6.golden.pdf"},
        {"test/lambda/pdf/output_pages.ls", "temp/pdf_lambda_pages.pdf", "3",
            "test/lambda/pdf/output_pages.txt", "test/lambda/pdf/output_pages.golden.pdf"},
    };
    const char* tiers[] = {"interp", "jit"};
    for (const char* tier : tiers) {
        ShellEnvEntry env[] = {{"LAMBDA_EXEC_BACKEND", tier}, {"LAMBDA_DISABLE_MIR_CACHE", "1"}, {nullptr, nullptr}};
        ShellOptions options = {};
        options.env = env;
        options.timeout_ms = 30000;
        for (const auto& fixture : fixtures) {
            // share the script oracle so added PDF checks cannot leave a stale C++ copy.
            char* expected = read_text_file(fixture.golden);
            EXPECT_NE(expected, nullptr) << fixture.golden;
            if (!expected) continue;
            const char* trimmed = expected;
            size_t length = strlen(expected);
            str_rtrim(&trimmed, &length);
            expected[length] = '\0';
            EXPECT_GT(length, 0u) << fixture.golden;
            const char* arguments[] = {"./lambda.exe", "run", fixture.script, nullptr};
            ShellResult generated = shell_exec(arguments[0], arguments, &options);
            EXPECT_FALSE(generated.timed_out) << tier;
            EXPECT_EQ(generated.exit_code, 0) << tier << (generated.stderr_buf ? generated.stderr_buf : "");
            if (length) EXPECT_NE(strstr(generated.stdout_buf ? generated.stdout_buf : "",
                expected), nullptr) << tier << fixture.script;
            free(expected);
            shell_result_free(&generated);
            const char* check[] = {"python3", "test/pdf_output_check.py", fixture.file, "golden",
                fixture.golden_pdf, fixture.page_count, nullptr};
            ShellResult checked = shell_exec(check[0], check, &options);
            EXPECT_EQ(checked.exit_code, 0) << tier << (checked.stderr_buf ? checked.stderr_buf : "");
            shell_result_free(&checked);
        }
    }
}

TEST(PdfFileOutput, GoldenComparisonRejectsPixelAndStructuralRegressions) {
    const char* arguments[] = {"python3", "test/test_pdf_output_check.py", nullptr};
    ShellOptions options = {};
    options.timeout_ms = 30000;
    ShellResult checked = shell_exec(arguments[0], arguments, &options);
    EXPECT_FALSE(checked.timed_out);
    EXPECT_EQ(checked.exit_code, 0) << (checked.stderr_buf ? checked.stderr_buf : "");
    shell_result_free(&checked);
}
