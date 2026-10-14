#include <gtest/gtest.h>
#include "../lambda/runtime/binary_schema.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/input/input.hpp"
#include "../lib/mem_kind.hpp"
#include "../lambda/module/radiant/radiant_host_api.hpp"
#include "../lambda/runtime/lambda-error.h"
#include "../lib/shell.h"
#include "../lib/file.h"

namespace {

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

struct WalkProbe {
    Rooted<Item>* escaped;
    Rooted<Item>* failure;
    size_t calls;
};
static WalkProbe* active_walk_probe;

static Item probe_walk_callback(Context*, Item event, Item, Item frame) {
    WalkProbe* probe = active_walk_probe;
    probe->calls++;
    probe->escaped->set(frame);
    if (probe->failure && event.get_len() == 8 &&
            memcmp(event.get_chars(), "boundary", 8) == 0) return probe->failure->get();
    return ItemNull;
}

class BinarySchemaTest : public ::testing::Test {
protected:
    EvalContext eval = {};
    Pool* names = nullptr;

    void SetUp() override {
        names = pool_create();
        ASSERT_NE(names, nullptr);
        ASSERT_TRUE(eval_context_init(&eval));
        heap_init();
        ASSERT_NE(eval.heap, nullptr);
        eval.pool = eval.heap->pool;
        eval.name_pool = name_pool_create_runtime(names);
        ASSERT_NE(eval.name_pool, nullptr);
        ASSERT_TRUE(lambda_side_stack_bind());
    }

    void TearDown() override {
        heap_destroy();
        eval.heap = nullptr;
        EXPECT_TRUE(eval_context_shutdown(&eval));
        pool_destroy(names);
    }

    Item field(Item view, const char* name) {
        RootFrame roots(1);
        Rooted<Item> receiver(roots, view);
        Item key = {.item = s2it(heap_create_name(name, strlen(name)))};
        return velmt_attr_get(receiver.get().velmt, key);
    }
};

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

TEST_F(BinarySchemaTest, WalkerInvalidatesEscapedContextAndPreservesCallbackErrors) {
    TypeBinarySchema element = {}, text = {}, context_type = {};
    TypeBinarySchema* types[] = {&element, &text, &context_type};
    const char* bindings[] = {"radiant.DomElement", "radiant.TextRect", "radiant.PdfWalkFrame"};
    for (size_t i = 0; i < 3; i++) {
        types[i]->type_id = LMD_TYPE_TYPE;
        types[i]->kind = TYPE_KIND_BINARY_SCHEMA;
        types[i]->layout = {"probe", 0, nullptr, 0, BINARY_SCHEMA_NATIVE, bindings[i]};
    }
    const BinarySchemaField context_fields[] = {
        {"x", BINARY_FIELD_F32, 0, 4, nullptr, false, nullptr, nullptr},
    };
    context_type.layout.fields = context_fields;
    context_type.layout.field_count = 1;
    RootFrame roots(7);
    const char* source_path = "test/html/pdf_lambda_views.html";
    Rooted<Item> schemas(roots, vmap_new()), options(roots, vmap_new()),
        path(roots, {.item = s2it(heap_create_name(source_path, strlen(source_path)))}),
        escaped(roots, ItemNull), failure(roots, ItemNull), callback(roots, ItemNull), result(roots, ItemNull);
    const char* keys[] = {"element", "text_rect", "context"};
    for (size_t i = 0; i < 3; i++) {
        Item key = {.item = s2it(heap_create_name(keys[i], strlen(keys[i])))};
        ASSERT_NE(get_type_id(vmap_set(schemas.get(), key, {.type = types[i]})), LMD_TYPE_ERROR);
    }
    TypeFunc signature = {};
    signature.type_id = LMD_TYPE_FUNC;
    signature.param_count = signature.required_param_count = 3;
    signature.returned = signature.return_contract = &TYPE_ANY;
    callback.set({.function = to_fn_named((fn_ptr)probe_walk_callback, 3, "schema_walk_probe")});
    lambda_function_mark_lambda_boxed_function(callback.get().function);
    lambda_function_mark_mir_context_abi(callback.get().function);
    lambda_function_mark_mir_public_return_shape(callback.get().function, LAMBDA_MIR_PUBLIC_RETURN_ITEM);
    lambda_function_set_type(callback.get().function, &signature);
    WalkProbe probe = {&escaped, nullptr, 0};
    struct ProbeScope { ~ProbeScope() { active_walk_probe = nullptr; } } scope;
    active_walk_probe = &probe;
    result.set(fn_radiant_pdf_walk(path.get(), options.get(), callback.get(), schemas.get()));
    ASSERT_NE(get_type_id(result.get()), LMD_TYPE_ERROR);
    EXPECT_GT(probe.calls, 2u);
    heap_gc_collect();
    EXPECT_EQ(get_type_id(field(escaped.get(), "x")), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(fn_radiant_pdf_glyph_path(escaped.get())), LMD_TYPE_ERROR);
    failure.set(err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, "callback probe failed", nullptr)));
    probe.failure = &failure;
    probe.calls = 0;
    result.set(fn_radiant_pdf_walk(path.get(), options.get(), callback.get(), schemas.get()));
    EXPECT_EQ(result.get().item, failure.get().item);
    EXPECT_EQ(get_type_id(field(escaped.get(), "x")), LMD_TYPE_ERROR);
    signature.is_proc = true;
    lambda_function_mark_lambda_boxed_procedure(callback.get().function);
    probe.calls = 0;
    result.set(fn_radiant_pdf_walk(path.get(), options.get(), callback.get(), schemas.get()));
    EXPECT_EQ(get_type_id(result.get()), LMD_TYPE_ERROR);
    EXPECT_EQ(probe.calls, 0u);
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

TEST_F(BinarySchemaTest, LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers) {
    const char* tiers[] = {"interp", "jit"};
    for (const char* tier : tiers) {
        ShellEnvEntry env[] = {{"LAMBDA_EXEC_BACKEND", tier}, {"LAMBDA_DISABLE_MIR_CACHE", "1"}, {nullptr, nullptr}};
        ShellOptions options = {};
        options.env = env;
        options.timeout_ms = 30000;
        const char* arguments[] = {"./lambda.exe", "run", "test/lambda/pdf/output_resources.ls", nullptr};
        ShellResult generated = shell_exec(arguments[0], arguments, &options);
        EXPECT_FALSE(generated.timed_out) << tier;
        EXPECT_EQ(generated.exit_code, 0) << tier << (generated.stderr_buf ? generated.stderr_buf : "");
        EXPECT_NE(strstr(generated.stdout_buf ? generated.stdout_buf : "",
            "[true, true, true, true, true, true, true, true]"), nullptr) << tier;
        shell_result_free(&generated);
        const char* check[] = {"python3", "test/pdf_output_check.py", "temp/pdf_lambda_resources.pdf", nullptr};
        ShellResult checked = shell_exec(check[0], check, &options);
        EXPECT_EQ(checked.exit_code, 0) << tier << (checked.stderr_buf ? checked.stderr_buf : "");
        shell_result_free(&checked);
    }
}

} // namespace
