// Radiant's PDF view walker (radiant.pdf_walk) driven natively: it hands a
// callback native frames typed by binary schemas, invalidates any frame that
// escapes its callback, and returns a callback's error unchanged. Moved out of
// the binary-schema tests so the Lambda runtime suite need not link Radiant.

#include <gtest/gtest.h>
#include "test_eval_fixture.hpp"
#include "../lambda/runtime/binary_schema.hpp"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/runtime/lambda-error.h"
#include "../lambda/module/radiant/radiant_host_api.hpp"
#include "../lambda/module/radiant/pdf_view_schema_version.hpp"
#include "../lib/arraylist.h"

namespace {

struct WalkProbe {
    Rooted<Item>* escaped;
    Rooted<Item>* failure;
    size_t calls;
    ArrayList* scopes;
    size_t clips;
    size_t transforms;
};
static WalkProbe* active_walk_probe;

static Item probe_walk_callback(Context*, Item event, Item, Item frame) {
    WalkProbe* probe = active_walk_probe;
    probe->calls++;
    probe->escaped->set(frame);
    EXPECT_EQ(item_attr(frame, "sequence").get_num_sized_as_int64(), (int64_t)probe->calls);
    const char* name = event.get_chars();
    const char* scope = nullptr;
    bool entering = event.get_len() > 6 && strncmp(name, "begin_", 6) == 0;
    bool leaving = event.get_len() > 4 && strncmp(name, "end_", 4) == 0;
    if (entering || leaving) scope = name + (entering ? 6 : 4);
    if (scope && probe->scopes) {
        // compare the actual native callback nesting, including mixed scopes.
        uintptr_t kind = strcmp(scope, "page") == 0 ? 1 : strcmp(scope, "clip") == 0 ? 2 :
            strcmp(scope, "transform") == 0 ? 3 : 4;
        if (entering) {
            EXPECT_TRUE(arraylist_append(probe->scopes, (void*)kind));
            if (kind == 2) probe->clips++;
            if (kind == 3) probe->transforms++;
        } else {
            EXPECT_GT(probe->scopes->length, 0);
            EXPECT_EQ((uintptr_t)arraylist_pop(probe->scopes), kind);
        }
    }
    if (probe->failure && event.get_len() == 8 &&
            memcmp(event.get_chars(), "boundary", 8) == 0) return probe->failure->get();
    return ItemNull;
}

class RadiantPdfWalkTest : public EvalContextTest {};

TEST_F(RadiantPdfWalkTest, WalkerInvalidatesEscapedContextAndPreservesCallbackErrors) {
    TypeBinarySchema element = {}, text = {}, context_type = {};
    TypeBinarySchema* types[] = {&element, &text, &context_type};
    const char* bindings[] = {"radiant.DomElement", "radiant.TextRect", "radiant.PdfWalkFrame"};
    for (size_t i = 0; i < 3; i++) {
        types[i]->type_id = LMD_TYPE_TYPE;
        types[i]->kind = TYPE_KIND_BINARY_SCHEMA;
        types[i]->layout = {"probe", 0, nullptr, 0, BINARY_SCHEMA_NATIVE, bindings[i],
            nullptr, BINARY_SCHEMA_BINDING_ABI, RADIANT_PDF_SCHEMA_VERSION};
    }
    const BinarySchemaField context_fields[] = {
        {"x", BINARY_FIELD_F32, 0, 4, nullptr, false, nullptr, nullptr},
        {"sequence", BINARY_FIELD_I32, 0, 4, nullptr, false, nullptr, nullptr},
    };
    context_type.layout.fields = context_fields;
    context_type.layout.field_count = 2;
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
    WalkProbe probe = {&escaped, nullptr, 0, arraylist_new(8), 0, 0};
    ASSERT_NE(probe.scopes, nullptr);
    struct ProbeScope {
        ArrayList* scopes;
        ~ProbeScope() { arraylist_free(scopes); active_walk_probe = nullptr; }
    } scope = {probe.scopes};
    active_walk_probe = &probe;
    result.set(fn_radiant_pdf_walk(path.get(), options.get(), callback.get(), schemas.get()));
    ASSERT_NE(get_type_id(result.get()), LMD_TYPE_ERROR);
    EXPECT_GT(probe.calls, 2u);
    EXPECT_EQ(probe.scopes->length, 0);
    heap_gc_collect();
    EXPECT_EQ(get_type_id(field(escaped.get(), "x")), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(fn_radiant_pdf_glyph_path(escaped.get())), LMD_TYPE_ERROR);
    source_path = "test/html/pdf_lambda_resources.html";
    path.set({.item = s2it(heap_create_name(source_path, strlen(source_path)))});
    probe.calls = 0;
    result.set(fn_radiant_pdf_walk(path.get(), options.get(), callback.get(), schemas.get()));
    ASSERT_NE(get_type_id(result.get()), LMD_TYPE_ERROR);
    EXPECT_EQ(probe.scopes->length, 0);
    EXPECT_GT(probe.clips, 0u);
    EXPECT_GT(probe.transforms, 0u);
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

} // namespace
