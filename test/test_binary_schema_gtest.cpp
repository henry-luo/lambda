#include <gtest/gtest.h>
#include "test_eval_fixture.hpp"
#include "../lambda/runtime/binary_schema.hpp"
#include "../lambda/core/binary.h"
#include "../lambda/io/mark_output_builder.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/input/input.hpp"
#include "../lib/mem_kind.hpp"
#include "../lambda/runtime/lambda-error.h"
#include "../lib/shell.h"
#include "../lib/file.h"
#include "../lib/str.h"
#include "../lib/arraylist.h"
#include "../lambda/runtime/ast_build.hpp"
#include "../lambda/validator/validator.hpp"

namespace {

struct NativeSchemaEntry { Item label; uint32_t id; };
struct NativeSchemaBatch {
    lam::OwnArr<NativeSchemaEntry> rows;
    lam::OwnArr<NativeSchemaEntry> optional_rows;
    int64_t count;
    int64_t capacity;
};
struct NativeSchemaNumbers {
    const uint16_t* values;
    const uint16_t* prefix;
    uint8_t* bytes;
    uint8_t* nullable_bytes;
    int64_t count;
    uint64_t capacity;
};
#include "binary_schema_fixture.inc"

struct SchemaFixtureBase { uint32_t id; };
struct SchemaFixtureChild : SchemaFixtureBase { float width; };
struct SchemaFixtureRecord {
    int16_t signed_value;
    bool enabled;
    lam::Own<SchemaFixtureChild> child;
    SchemaFixtureChild* required;
};

const BinarySchemaField child_fields[] = {
    BINARY_SCHEMA_MEMBER(SchemaFixtureChild, id, BINARY_FIELD_U32),
    BINARY_SCHEMA_MEMBER(SchemaFixtureChild, width, BINARY_FIELD_F32),
};
const BinarySchema child_schema = {"fixture_child", sizeof(SchemaFixtureChild),
    child_fields, 2, BINARY_SCHEMA_NATIVE};
const BinarySchemaField record_fields[] = {
    BINARY_SCHEMA_MEMBER(SchemaFixtureRecord, signed_value, BINARY_FIELD_I16),
    BINARY_SCHEMA_MEMBER(SchemaFixtureRecord, enabled, BINARY_FIELD_BOOL),
    BINARY_SCHEMA_REFERENCE(SchemaFixtureRecord, child, &child_schema, true),
    BINARY_SCHEMA_REFERENCE(SchemaFixtureRecord, required, &child_schema, false),
};
const BinarySchema record_schema = {"fixture_record", sizeof(SchemaFixtureRecord),
    record_fields, 4, BINARY_SCHEMA_NATIVE};

static TypeUnary occurrence_contract(Operator op, Type* operand, int max_count) {
    TypeUnary type = {};
    type.type_id = LMD_TYPE_TYPE;
    type.kind = TYPE_KIND_UNARY;
    type.op = op;
    type.operand = operand;
    type.max_count = max_count;
    return type;
}

class BinarySchemaTest : public EvalContextTest {
protected:
    Pool* compiler = nullptr;

    void TearDown() override {
        EvalContextTest::TearDown();
        if (compiler) pool_destroy(compiler);
    }

    const BinarySchema* declared(const char* name) {
        if (!compiler) compiler = pool_create();
        Input* input = Input::create(compiler, nullptr);
        char* source = read_text_file("test/lambda/mod_binary_native_schema.ls");
        if (!input || !source) { free(source); return nullptr; }
        Transpiler tp = {};
        tp.source = source;
        tp.pool = compiler;
        tp.arena = input->arena;
        tp.name_pool = input->name_pool;
        tp.type_list = input->type_list;
        tp.root = input->root;
        tp.const_list = arraylist_new(16);
        tp.current_scope = (NameScope*)pool_calloc(compiler, sizeof(NameScope));
        tp.max_errors = 10;
        AstScript* root = nullptr;
        LambdaParseError error = {};
        EXPECT_EQ(lambda_rd_reduce_ast(&tp, source, strlen(source), &root, &error), LAMBDA_PARSE_OK)
            << (error.message ? error.message : "");
        const BinarySchema* result = nullptr;
        for (int i = 0; i < tp.type_list->length; i++) {
            const BinarySchema* schema = binary_schema_from_type((Type*)tp.type_list->data[i]);
            if (schema && strcmp(schema->tag, name) == 0) result = schema;
        }
        arraylist_free(tp.const_list);
        free(source);
        return result;
    }

    const BinarySchema* bind(BinarySchemaOwner* owner, const char* name, const BinarySchema* native) {
        const BinarySchema* schema = declared(name);
        if (!schema) return nullptr;
        Item error = ItemNull;
        const BinarySchema* bound = binary_schema_bind_native(owner, schema, native, &error);
        EXPECT_EQ(get_type_id(error), LMD_TYPE_NULL);
        return bound;
    }
};

TEST_F(EvalContextTest, BinarySliceBuiltinRetainsStorageWithoutMaterializingBytes) {
    const char bytes[] = {'a', 'b', '\0', 'c', 'd', 'e'};
    RootFrame roots(3);
    Rooted<Item> source(roots, {.item = x2it(heap_binary_from_bytes(bytes, sizeof(bytes)))});
    ASSERT_EQ(get_type_id(source.get()), LMD_TYPE_BINARY);
    ByteSpan original = binary_span(source.get().get_binary());
    int64_t allocations = byte_storage_allocation_count();
    int64_t copies = binary_payload_copy_count();
    Rooted<Item> view(roots, fn_slice(source.get(), {.item = i2it(1)}, {.item = i2it(5)}));
    ASSERT_EQ(get_type_id(view.get()), LMD_TYPE_BINARY);
    Rooted<Item> nested(roots, fn_slice(view.get(), {.item = i2it(1)}, {.item = i2it(3)}));
    ASSERT_EQ(get_type_id(nested.get()), LMD_TYPE_BINARY);
    // the public builtin must reach the storage-sharing path, including nested views.
    EXPECT_EQ(binary_span(view.get().get_binary()).storage, original.storage);
    EXPECT_EQ(binary_span(nested.get().get_binary()).storage, original.storage);
    EXPECT_EQ(binary_span(nested.get().get_binary()).offset, original.offset + 2);
    EXPECT_EQ(binary_length(nested.get().get_binary()), 2u);
    EXPECT_EQ(memcmp(binary_data(nested.get().get_binary()), "\0c", 2), 0);
    EXPECT_EQ(byte_storage_allocation_count(), allocations);
    EXPECT_EQ(binary_payload_copy_count(), copies);
    source.set(ItemNull);
    view.set(ItemNull);
    heap_gc_collect();
    EXPECT_EQ(binary_span(nested.get().get_binary()).storage, original.storage);
    EXPECT_EQ(memcmp(binary_data(nested.get().get_binary()), "\0c", 2), 0);
}

TEST_F(BinarySchemaTest, CompiledRecordSpansAreLazyAndTraceItemsAfterTheirParentIsGone) {
    BinarySchemaOwner* owner = binary_schema_owner_create();
    RootFrame roots(4);
    const BinarySchema* bound = bind(owner, "NativeBatch", &native_batch_schema);
    ASSERT_NE(bound, nullptr);
    NativeSchemaEntry entries[2] = {{{.item = s2it(heap_strcpy("retained", 8))}, 10}, {ItemNull, 20}};
    NativeSchemaBatch batch = {lam::OwnArr<NativeSchemaEntry>(entries), lam::OwnArr<NativeSchemaEntry>(entries), 2, 2};
    gc_set_force_collect_interval(eval.heap->gc, 1);
    Rooted<Item> view(roots, binary_schema_native_view(bound, &batch, owner));
    ASSERT_EQ(get_type_id(view.get()), LMD_TYPE_VELMT);
    Rooted<Item> rows(roots, field(view.get(), "rows"));
    ASSERT_EQ(get_type_id(rows.get()), LMD_TYPE_VARRAY);
    view.set(ItemNull);
    heap_gc_collect();
    Rooted<Item> first(roots, varray_get(rows.get().varray, 0));
    ASSERT_EQ(get_type_id(first.get()), LMD_TYPE_VELMT);
    EXPECT_EQ(field(first.get(), "label").get_len(), 8u);
    entries[0].id = 42;
    EXPECT_EQ(field(first.get(), "id").get_num_sized_as_int64(), 42);
    EXPECT_EQ(varray_count(rows.get().varray), 2);
    EXPECT_EQ(varray_get(rows.get().varray, -1).item, ItemNull.item);
    EXPECT_EQ(varray_get(rows.get().varray, 2).item, ItemNull.item);
    TypeUnary contract = occurrence_contract(OPERATOR_ARRAY, bound->fields[0].logical_type, -1);
    EXPECT_EQ(fn_is(rows.get(), {.type = &contract}), BOOL_TRUE);
    TypeUnary optional = occurrence_contract(OPERATOR_OPTIONAL, &contract, 1);
    EXPECT_EQ(fn_is(rows.get(), {.type = &optional}), BOOL_TRUE);
    contract.min_count = contract.max_count = 3;
    EXPECT_EQ(fn_is(rows.get(), {.type = &optional}), BOOL_FALSE);
    contract.min_count = 0;
    contract.max_count = -1;
    Item rejected = ItemNull;
    EXPECT_EQ(varray_set(rows.get().varray, 0, ItemNull, &rejected), VIRTUAL_OP_READONLY);
    gc_set_force_collect_interval(eval.heap->gc, 0);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
    batch.rows = nullptr;
    heap_gc_collect();
    EXPECT_EQ(get_type_id(fn_len_checked(rows.get())), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(varray_get(rows.get().varray, -1)), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(field(first.get(), "id")), LMD_TYPE_ERROR);
    EXPECT_EQ(varray_set(rows.get().varray, 0, ItemNull, &rejected), VIRTUAL_OP_ERROR);
    EXPECT_EQ(fn_is(rows.get(), {.type = &contract}), BOOL_ERROR);
    Rooted<Item> empty_array(roots, {.array = array_plain()});
    EXPECT_EQ(fn_eq(rows.get(), empty_array.get()), BOOL_ERROR);
    EXPECT_EQ(get_type_id(fn_format1(rows.get())), LMD_TYPE_ERROR);
    StrBuf* printed = strbuf_new();
    print_item(printed, rows.get());
    EXPECT_NE(strstr(printed->str, "error"), nullptr);
    strbuf_free(printed);
}

TEST_F(BinarySchemaTest, BindingVersionAndSpanRepresentationMustMatchBeforePublication) {
    const BinarySchema* declared_schema = declared("NativeNumbers");
    ASSERT_NE(declared_schema, nullptr);
    BinarySchemaOwner* owner = binary_schema_owner_create();
    RootFrame roots(1);
    Rooted<Item> error(roots, ItemNull);
    const auto reject = [&](const BinarySchema* wanted, const BinarySchema* native) {
        Item failure = ItemNull;
        EXPECT_EQ(binary_schema_bind_native(owner, wanted, native, &failure), nullptr);
        error.set(failure);
        EXPECT_EQ(get_type_id(error.get()), LMD_TYPE_ERROR);
    };
    BinarySchema wanted = *declared_schema;
    wanted.binding_version = "stale";
    reject(&wanted, &native_numbers_schema);
    wanted.binding_version = nullptr;
    reject(&wanted, &native_numbers_schema);
    wanted = *declared_schema;
    wanted.binding_abi++;
    reject(&wanted, &native_numbers_schema);
    BinarySchema native = native_numbers_schema;
    BinarySchemaField fields[sizeof(native_numbers_schema_fields) / sizeof(native_numbers_schema_fields[0])];
    memcpy(fields, native.fields, sizeof(fields));
    native.fields = fields;
    fields[0].stride++;
    reject(declared_schema, &native);
    fields[0] = native_numbers_schema.fields[0];
    fields[0].extent = nullptr;
    reject(declared_schema, &native);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
}

TEST_F(BinarySchemaTest, EveryStructuralReadFailsAfterNativeStorageIsReleased) {
    BinarySchemaOwner* owner = binary_schema_owner_create();
    SchemaFixtureRecord* native = (SchemaFixtureRecord*)mem_calloc(1, sizeof(SchemaFixtureRecord), MEM_CAT_CONTAINER);
    ASSERT_NE(native, nullptr);
    RootFrame roots(2);
    Rooted<Item> view(roots, binary_schema_native_view(&record_schema, native, owner));
    Rooted<Item> error(roots, ItemNull);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
    mem_free(native);
    heap_gc_collect();
    Item result = ItemNull;
    EXPECT_EQ(velmt_tag(view.get().velmt, &result), VIRTUAL_OP_ERROR);
    EXPECT_EQ(velmt_namespace_uri(view.get().velmt, &result), VIRTUAL_OP_ERROR);
    EXPECT_EQ(velmt_attr_key_at(view.get().velmt, -1, &result), VIRTUAL_OP_ERROR);
    EXPECT_EQ(velmt_attr_value_at(view.get().velmt, -1, &result), VIRTUAL_OP_ERROR);
    EXPECT_EQ(get_type_id(velmt_child_get(view.get().velmt, -1)), LMD_TYPE_ERROR);
    EXPECT_EQ(velmt_attr_set(view.get().velmt, ItemNull, ItemNull, &result), VIRTUAL_OP_ERROR);
    EXPECT_EQ(velmt_child_set(view.get().velmt, 0, ItemNull, &result), VIRTUAL_OP_ERROR);
    EXPECT_EQ(item_keys(view.get()), nullptr);
    error.set(fn_len_checked(view.get()));
    EXPECT_EQ(get_type_id(error.get()), LMD_TYPE_ERROR);
    StrBuf* text = strbuf_new();
    print_item(text, view.get());
    EXPECT_GT(text->length, 0u);
    EXPECT_NE(strstr(text->str, "error"), nullptr);
    strbuf_free(text);
}

TEST_F(BinarySchemaTest, NativeSpansDistinguishEmptyAbsentAndInvalidExtents) {
    BinarySchemaOwner* owner = binary_schema_owner_create();
    RootFrame roots(2);
    const BinarySchema* bound = bind(owner, "NativeBatch", &native_batch_schema);
    ASSERT_NE(bound, nullptr);
    NativeSchemaBatch batch = {};
    Rooted<Item> view(roots, binary_schema_native_view(bound, &batch, owner));
    Item empty = field(view.get(), "rows");
    ASSERT_EQ(get_type_id(empty), LMD_TYPE_VARRAY);
    EXPECT_EQ(varray_count(empty.varray), 0);
    TypeUnary contract = occurrence_contract(OPERATOR_ARRAY, bound->fields[0].logical_type, -1);
    TypeUnary optional = occurrence_contract(OPERATOR_OPTIONAL, &contract, 1);
    EXPECT_EQ(fn_is(empty, {.type = &optional}), BOOL_TRUE);
    EXPECT_EQ(field(view.get(), "optional_rows").item, ItemNull.item);
    NativeSchemaEntry entry = {};
    batch.optional_rows = lam::OwnArr<NativeSchemaEntry>(&entry);
    Item present_empty = field(view.get(), "optional_rows");
    ASSERT_EQ(get_type_id(present_empty), LMD_TYPE_VARRAY);
    EXPECT_EQ(varray_count(present_empty.varray), 0);
    batch.optional_rows = nullptr;
    batch.count = batch.capacity = 1;
    EXPECT_EQ(get_type_id(field(view.get(), "rows")), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(field(view.get(), "optional_rows")), LMD_TYPE_ERROR);
    batch.count = -1;
    EXPECT_EQ(get_type_id(field(view.get(), "rows")), LMD_TYPE_ERROR);
    batch.count = 2; batch.capacity = 1;
    EXPECT_EQ(get_type_id(field(view.get(), "rows")), LMD_TYPE_ERROR);
    batch.count = 0; batch.capacity = -1;
    EXPECT_EQ(get_type_id(field(view.get(), "rows")), LMD_TYPE_ERROR);
    batch.count = batch.capacity = INT64_MAX;
    EXPECT_EQ(get_type_id(field(view.get(), "rows")), LMD_TYPE_ERROR);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
}

TEST_F(BinarySchemaTest, ScalarSpansAndOwnedByteCopiesUseTheSameBindings) {
    BinarySchemaOwner* owner = binary_schema_owner_create();
    RootFrame roots(4);
    const BinarySchema* bound = bind(owner, "NativeNumbers", &native_numbers_schema);
    ASSERT_NE(bound, nullptr);
    uint16_t numbers[] = {1, 500, UINT16_MAX};
    uint8_t bytes[] = {0, 255, 42};
    NativeSchemaNumbers native = {numbers, numbers, bytes, bytes, 3, 3};
    Rooted<Item> view(roots, binary_schema_native_view(bound, &native, owner));
    Rooted<Item> values(roots, field(view.get(), "values"));
    Rooted<Item> copied(roots, field(view.get(), "bytes"));
    ASSERT_EQ(get_type_id(values.get()), LMD_TYPE_VARRAY);
    ASSERT_EQ(get_type_id(copied.get()), LMD_TYPE_BINARY);
    EXPECT_EQ(varray_get(values.get().varray, 1).get_num_sized_as_int64(), 500);
    EXPECT_EQ(varray_get(values.get().varray, 2).get_num_sized_as_int64(), UINT16_MAX);
    Item prefix = field(view.get(), "prefix");
    ASSERT_EQ(get_type_id(prefix), LMD_TYPE_VARRAY);
    EXPECT_EQ(varray_count(prefix.varray), 2);
    EXPECT_EQ(varray_get(prefix.varray, 2).item, ItemNull.item);
    EXPECT_EQ(copied.get().get_len(), 3u);
    EXPECT_EQ(memcmp(copied.get().get_chars(), bytes, 3), 0);
    TypeUnary contract = occurrence_contract(OPERATOR_ARRAY, &TYPE_U16, -1);
    EXPECT_EQ(fn_is(values.get(), {.type = &contract}), BOOL_TRUE);
    bytes[0] = 17;
    EXPECT_EQ((uint8_t)copied.get().get_chars()[0], 0);
    native.count = 0;
    native.values = nullptr;
    Item empty = field(view.get(), "values");
    ASSERT_EQ(get_type_id(empty), LMD_TYPE_VARRAY);
    EXPECT_EQ(varray_count(empty.varray), 0);
    Item present_bytes = field(view.get(), "nullable_bytes");
    ASSERT_EQ(get_type_id(present_bytes), LMD_TYPE_BINARY);
    EXPECT_EQ(present_bytes.get_len(), 0u);
    native.nullable_bytes = nullptr;
    EXPECT_EQ(field(view.get(), "nullable_bytes").item, ItemNull.item);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
    heap_gc_collect();
    EXPECT_EQ(get_type_id(varray_get(values.get().varray, 0)), LMD_TYPE_ERROR);
    EXPECT_EQ((uint8_t)copied.get().get_chars()[1], 255);
}

TEST_F(BinarySchemaTest, TypedBindingsReadInheritedFieldsAndLazyPropertyReferences) {
    SchemaFixtureChild child = {{UINT32_MAX}, 42.25f};
    SchemaFixtureRecord native = {-1234, true, lam::own(&child), &child};
    BinarySchemaOwner* owner = binary_schema_owner_create();
    ASSERT_NE(owner, nullptr);
    RootFrame roots(2);
    Rooted<Item> view(roots, binary_schema_native_view(&record_schema, &native, owner));
    ASSERT_EQ(get_type_id(view.get()), LMD_TYPE_VELMT);
    EXPECT_EQ(field(view.get(), "signed_value").get_num_sized_as_int64(), -1234);
    EXPECT_TRUE(it2b(field(view.get(), "enabled")));
    Rooted<Item> nested(roots, field(view.get(), "child"));
    ASSERT_EQ(get_type_id(nested.get()), LMD_TYPE_VELMT);
    EXPECT_EQ(field(nested.get(), "id").get_num_sized_as_int64(), UINT32_MAX);
    EXPECT_FLOAT_EQ(item_to_f32(field(nested.get(), "width").item), 42.25f);
    // Access reads the admitted native storage instead of an eager copied map.
    child.width = 18.5f;
    heap_gc_collect();
    EXPECT_FLOAT_EQ(item_to_f32(field(nested.get(), "width").item), 18.5f);
    EXPECT_EQ(field(view.get(), "next_sibling").item, ItemNull.item);
    EXPECT_EQ(velmt_child_count(view.get().velmt), 0);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
    EXPECT_EQ(get_type_id(field(view.get(), "enabled")), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(field(nested.get(), "width")), LMD_TYPE_ERROR);
}

TEST_F(BinarySchemaTest, NullableAndRequiredReferencesKeepDistinctContracts) {
    SchemaFixtureRecord native = {};
    BinarySchemaOwner* owner = binary_schema_owner_create();
    RootFrame roots(1);
    Rooted<Item> view(roots, binary_schema_native_view(&record_schema, &native, owner));
    ASSERT_EQ(get_type_id(view.get()), LMD_TYPE_VELMT);
    EXPECT_EQ(field(view.get(), "child").item, ItemNull.item);
    EXPECT_EQ(get_type_id(field(view.get(), "required")), LMD_TYPE_ERROR);
    EXPECT_EQ(velmt_attr_count(view.get().velmt), 4);
    Item rejected = ItemNull;
    EXPECT_EQ(velmt_attr_set(view.get().velmt, ItemNull, ItemNull, &rejected),
        VIRTUAL_OP_READONLY);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
}

TEST_F(BinarySchemaTest, BufferFieldsHonorEndiannessBoundsAndPreciseSourceLifetime) {
    const BinarySchemaField fields[] = {
        {"value", BINARY_FIELD_U32, 0, 4, nullptr, false, nullptr, nullptr},
        {"signed", BINARY_FIELD_I16, 4, 2, nullptr, false, nullptr, nullptr},
    };
    const BinarySchema schema = {"wire_record", 6, fields, 2, BINARY_SCHEMA_BIG};
    const char bytes[] = {0x12, 0x34, 0x56, 0x78, (char)0xff, (char)0xfe};
    RootFrame roots(1);
    Rooted<Item> view(roots, binary_schema_decode(&schema,
        {.item = x2it(heap_binary_from_bytes(bytes, sizeof(bytes)))}));
    ASSERT_EQ(get_type_id(view.get()), LMD_TYPE_VELMT);
    heap_gc_collect();
    EXPECT_EQ(field(view.get(), "value").get_num_sized_as_int64(), 0x12345678);
    EXPECT_EQ(field(view.get(), "signed").get_num_sized_as_int64(), -2);
    EXPECT_EQ(get_type_id(binary_schema_decode(&record_schema,
        {.item = x2it(heap_binary_from_bytes(bytes, sizeof(bytes)))})), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(binary_schema_decode(&schema,
        {.item = x2it(heap_binary_from_bytes(bytes, 5))})), LMD_TYPE_ERROR);
}

TEST_F(BinarySchemaTest, BoxedNativeFieldsTraceItemsAndCheckTheirLogicalContract) {
    struct Record { Item text; };
    const BinarySchemaField fields[] = {BINARY_SCHEMA_ITEM(Record, text, &TYPE_STRING, true)};
    const BinarySchema native = {"boxed", sizeof(Record), fields, 1, BINARY_SCHEMA_NATIVE, "fixture.Boxed"};
    BinarySchemaField wanted = {"text", BINARY_FIELD_ITEM, 0, sizeof(Item), nullptr, true, nullptr, nullptr, &TYPE_STRING};
    BinarySchema declared = {"boxed", 0, &wanted, 1, BINARY_SCHEMA_NATIVE, "fixture.Boxed"};
    BinarySchemaOwner* owner = binary_schema_owner_create();
    Item error = ItemNull;
    const BinarySchema* bound = binary_schema_bind_native(owner, &declared, &native, &error);
    ASSERT_NE(bound, nullptr);
    Record record = {{.item = s2it(heap_strcpy("retained text", 13))}};
    gc_set_force_collect_interval(eval.heap->gc, 1);
    RootFrame roots(1);
    Rooted<Item> view(roots, binary_schema_native_view(bound, &record, owner));
    gc_set_force_collect_interval(eval.heap->gc, 0);
    heap_gc_collect();
    Item text = field(view.get(), "text");
    ASSERT_EQ(get_type_id(text), LMD_TYPE_STRING);
    EXPECT_EQ(text.get_len(), 13u);
    EXPECT_EQ(memcmp(text.get_chars(), "retained text", text.get_len()), 0);
    record.text = ItemNull;
    EXPECT_EQ(field(view.get(), "text").item, ItemNull.item);
    record.text = {.item = b2it(true)};
    EXPECT_EQ(get_type_id(field(view.get(), "text")), LMD_TYPE_ERROR);
    wanted.logical_type = &TYPE_BINARY;
    EXPECT_EQ(binary_schema_bind_native(owner, &declared, &native, &error), nullptr);
    EXPECT_EQ(get_type_id(error), LMD_TYPE_ERROR);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
    EXPECT_EQ(get_type_id(fn_len_checked(view.get())), LMD_TYPE_ERROR);
}

TEST_F(BinarySchemaTest, RejectsIncorrectNativeScalarWidthsBeforeReading) {
    const BinarySchemaField fields[] = {
        BINARY_SCHEMA_MEMBER(SchemaFixtureChild, width, BINARY_FIELD_F64),
    };
    const BinarySchema schema = {"bad_width", sizeof(SchemaFixtureChild), fields,
        1, BINARY_SCHEMA_NATIVE};
    SchemaFixtureChild native = {};
    BinarySchemaOwner* owner = binary_schema_owner_create();
    RootFrame roots(1);
    Rooted<Item> view(roots, binary_schema_native_view(&schema, &native, owner));
    EXPECT_EQ(get_type_id(field(view.get(), "width")), LMD_TYPE_ERROR);
    binary_schema_owner_invalidate(owner);
    binary_schema_owner_release(owner);
}

TEST_F(BinarySchemaTest, MetadataDoesNotConsumePredicatesOrNestedIntersections) {
    // S11.5.1v2: reject legacy metadata and keep nested `&` in the logical type.
    const struct { const char* source; const char* diagnostic; } cases[] = {
        {"type A : binary that {endian: 'big'} {value: u8}", "error[E100]"},
        {"type A : binary {value: u8 that {offset: 4}}", "binary schema field predicates are not supported"},
        {"type A : binary {value: u8, that {endian: 'big'}}", "binary schema predicates are not supported"},
        {"type A : binary & options {value: u8}", "literal metadata map"},
        {"type A : binary & {unknown: 1} {value: u8}", "unsupported binary schema metadata"},
        {"type A : binary {value: u8 & {offset: 1, offset: 2}}", "duplicate binary schema metadata"},
        {"type A : binary {value: (u8 & {offset: 4})}", "requires a fixed-width scalar or binary schema type"},
        {"type A : binary & {layout: 'native', binding: 'fixture.A'} {values: u8[missing] & {pointer: 'native'}}", "count must name an admitted integer field"},
        {"type A : binary & {layout: 'native', binding: 'fixture.A'} {values: u8[count] & {pointer: 'native'}, count: string}", "count must name an admitted integer field"},
        {"type A : binary & {layout: 'native', binding: 'fixture.A'} {values: u8[9223372036854775808] & {pointer: 'native'}}", "binary span count must be"},
        {"type A : binary & {layout: 'native', binding: 'fixture.A'} {values: u8[] & {pointer: 'native'}}", "binary span count must be"},
        {"type A : binary & {layout: 'native', binding: 'fixture.A'} {values: u8[4]}", "counted spans require a native pointer"},
        {"type A : binary & {layout: 'native', binding: 'fixture.A'} {values: u8?[4] & {pointer: 'native'}}", "span elements require a fixed-width scalar or native record"},
    };
    ASSERT_TRUE(create_dir("temp"));
    const char* path = "temp/binary_schema_rejected.ls";
    ShellOptions options = {};
    options.timeout_ms = 30000;
    for (const auto& probe : cases) {
        ASSERT_EQ(write_binary_file(path, probe.source, strlen(probe.source)), 0);
        const char* arguments[] = {"./lambda.exe", path, "--no-log", nullptr};
        ShellResult result = shell_exec(arguments[0], arguments, &options);
        EXPECT_FALSE(result.timed_out) << probe.source;
        EXPECT_NE(result.exit_code, 0) << probe.source;
        const char* out = result.stdout_buf ? result.stdout_buf : "";
        const char* err = result.stderr_buf ? result.stderr_buf : "";
        EXPECT_TRUE(strstr(out, probe.diagnostic) || strstr(err, probe.diagnostic))
            << probe.source << "\n" << out << err;
        shell_result_free(&result);
    }
}

TEST_F(BinarySchemaTest, PdfDestinationRecoveryClearsFailureInBothModes) {
    for (bool measuring : {false, true}) {
        MarkOutputBuilder* output = pdf_builder_create(measuring);
        ASSERT_NE(output, nullptr);
        Pool* pool = pool_create();
        Input* input = Input::create(pool, nullptr);
        ASSERT_NE(input, nullptr);
        {
            MarkBuilder source(input);
            output->ops->append(output, source.createStringItem("before"));
            size_t checkpoint = output->ops->size(output);
            output->ops->append(output, source.createStringItem("discarded"));
            output->ops->append(output, {.item = i2it(42)});
            EXPECT_EQ(get_type_id(output->ops->error(output)), LMD_TYPE_ERROR);
            ASSERT_TRUE(output->ops->rewind(output, checkpoint));
            EXPECT_EQ(get_type_id(output->ops->error(output)), LMD_TYPE_NULL);
            output->ops->append(output, source.createStringItem("replacement"));
            EXPECT_TRUE(output->ops->finish(output));
            EXPECT_EQ(output->ops->size(output), strlen("beforereplacement"));
        }
        pool_destroy(pool);
        output->ops->destroy(output);
    }
}

} // namespace
