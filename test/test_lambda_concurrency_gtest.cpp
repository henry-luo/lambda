#include <gtest/gtest.h>
#include "test_lambda_native_compiler.hpp"
#include <mpdecimal.h>
#include <cstring>
#include <cstdio>
#include <float.h>

#ifndef _WIN32
#include <pthread.h>
#include <time.h>
#endif

#include "../lambda/runtime/concurrency.h"
#include "../lambda/runtime/async.h"
#include "../lambda/runtime/activation.h"
#include "../lambda/lambda.hpp"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lambda/runtime/lambda-error.h"
#include "../lambda/runtime/render_map.h"
#include "../lambda/runtime/template_state.h"
#include "../lambda/runtime/template_registry.h"
#include "../lambda/runtime/template_host.h"
#include "../lambda/runtime/edit_bridge.h"
#include "../lambda/runtime/recovery_frame.h"
#include "../lambda/runtime/lambda-stack.h"
#include "../lambda/runtime/runtime-state.h"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/interp.hpp"
#include "../lambda/runtime/native_collection.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/runtime/gc/gc_object_zone.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_interp.hpp"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/jube/jube_interface.h"
#include "../lambda/jube/jube_registry.h"
#include "lambda/core/lambda-decimal.hpp"
#include "lambda/core/name_pool.hpp"
#include "../lambda/mir/mir.h"
#include "../lambda/mir/mir-gen.h"
#include "lambda/input/input.hpp"
#include "lambda/input/css/dom_element.hpp"
#include "../lambda/dom/dom.h"
#include "lambda/io/mark_builder.hpp"
#include "../lambda/validator/validator.hpp"
#include "lib/mem_factory.h"
#include "lib/memtrack.h"
#include "lib/fiber.h"
#include "lib/mempool.h"
#include "lib/file.h"
#include "lib/rdb_sqlite_tx.h"
#include "lib/uv_loop.h"
#include "lib/url.h"
#include "lib/str.h"
#include "lib/strbuf.h"
#include "../lambda/runtime/sys_func_registry.h"
#include "radiant/view.hpp"
#include "radiant/radiant.hpp"
#include "radiant/render.hpp"
#include "radiant/window_host.hpp"

extern __thread EvalContext* context;
extern UiContext ui_context;

extern "C" Item vmap_new(void);
extern "C" void vmap_gc_destroy(void* object, void* data);
extern const JubeTypeBinding radiant_dom_type_bindings[];
extern const int32_t radiant_dom_type_binding_count;

#ifndef _WIN32
extern __thread Context* input_context;
#endif

#ifndef LAMBDA_NO_MIR
TEST(LambdaJitImports, NoGcMetadataMatchesAudit) {
    // registry edits must preserve the effect audit consumed by precise roots.
    EXPECT_TRUE(jit_import_validate_no_gc_allowlist());
}
#endif

TEST(LambdaArrayNumShape, RejectsOutOfRangeRankBeforeStackDecode) {
    ArrayNum array = {};
    array.type_id = LMD_TYPE_ARRAY_NUM;
    array.set_elem_type(ELEM_INT64);
    array.is_ndim = 1;
    array.length = 1;
    array.capacity = 1;
    int64_t data[1] = {0};
    array.data = data;

    // Deliberately allocate a 1-D descriptor then corrupt its rank. The decoder
    // must reject it before it reads or writes past this descriptor or a 32-slot stack buffer.
    uint8_t shape_storage[sizeof(ArrayNumShape) + 2 * sizeof(int64_t)] = {};
    ArrayNumShape* shape = (ArrayNumShape*)shape_storage;
    shape->ndim = LAMBDA_ARRAY_NUM_MAX_NDIM + 1;
    array.extra = (int64_t)(uintptr_t)shape;
    Item item = {.array_num = &array};

    EXPECT_EQ(get_type_id(fn_shape(item)), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(fn_matmul(item, item)), LMD_TYPE_ERROR);
}

TEST(LambdaDecimal, QuietInt64ExtractionRejectsOverflowAndInvalidComparison) {
    mpd_context_t* context = decimal_fixed_context();
    mpd_t* large = mpd_new(context);
    ASSERT_NE(large, nullptr);
    uint32_t status = 0;
    mpd_qset_string(large, "9223372036854775808", context, &status);
    ASSERT_EQ(status, 0u);

    // The quiet extractor must not enter libmpdec's SIGFPE path on overflow.
    int64_t extracted = 0;
    EXPECT_FALSE(decimal_mpd_try_to_int64(large, context, &extracted));

    mpd_qset_string(large, "9223372036854775807", context, &status);
    ASSERT_EQ(status, 0u);
    EXPECT_TRUE(decimal_mpd_try_to_int64(large, context, &extracted));
    EXPECT_EQ(extracted, INT64_MAX);

    Decimal valid_decimal = {};
    ASSERT_TRUE(decimal_take_mpd(&valid_decimal, DECIMAL_FIXED, large));
    Decimal invalid_decimal = {};
    Item valid = {.item = c2it(&valid_decimal)};
    Item invalid = {.item = c2it(&invalid_decimal)};
    int comparison = 0;
    EXPECT_FALSE(decimal_cmp_items(invalid, valid, &comparison));

    decimal_payload_release(&valid_decimal);
}

TEST(LambdaDecimal, DoubleBoundaryConversionIsFallibleAndExact) {
    mpd_context_t* context = decimal_fixed_context();
    mpd_t* decimal = mpd_new(context);
    ASSERT_NE(decimal, nullptr);
    uint32_t status = 0;
    double value = 0.0;

    mpd_qset_string(decimal, "5e-324", context, &status);
    ASSERT_EQ(status, 0u);
    ASSERT_TRUE(decimal_mpd_try_to_double(decimal, context, &value));
    EXPECT_EQ(value, DBL_TRUE_MIN);

    status = 0;
    mpd_qset_string(decimal, "1.7976931348623157e308", context, &status);
    ASSERT_EQ(status, 0u);
    ASSERT_TRUE(decimal_mpd_try_to_double(decimal, context, &value));
    EXPECT_EQ(value, DBL_MAX);

    char spelling[64] = {};
    lambda_double_to_shortest(DBL_TRUE_MIN, spelling, sizeof(spelling));
    status = 0;
    mpd_qset_string(decimal, spelling, context, &status);
    ASSERT_EQ(status, 0u);
    ASSERT_TRUE(decimal_mpd_try_to_double(decimal, context, &value));
    EXPECT_EQ(value, DBL_TRUE_MIN);

    lambda_double_to_shortest(DBL_MAX, spelling, sizeof(spelling));
    status = 0;
    mpd_qset_string(decimal, spelling, context, &status);
    ASSERT_EQ(status, 0u);
    ASSERT_TRUE(decimal_mpd_try_to_double(decimal, context, &value));
    EXPECT_EQ(value, DBL_MAX);

    status = 0;
    mpd_qset_string(decimal, "9007199254740991", context, &status);
    ASSERT_EQ(status, 0u);
    ASSERT_TRUE(decimal_mpd_try_to_double(decimal, context, &value));
    EXPECT_EQ(value, 9007199254740991.0);

    EXPECT_FALSE(decimal_mpd_try_to_double(nullptr, context, &value));
    EXPECT_FALSE(item_try_to_double(ItemNull, &value));
    EXPECT_EQ(get_type_id(coerce_num_sized(ItemNull, NUM_FLOAT32)), LMD_TYPE_ERROR);

    mpd_del(decimal);
}

TEST(LambdaNumericRuntime, BitwiseRejectsNonIntegerItems) {
    Item fractional = push_d(1.5);
    Item one = {.item = i2it(1)};

    EXPECT_EQ(get_type_id(fn_band_item(fractional, one)), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(fn_bnot_item(fractional)), LMD_TYPE_ERROR);
    EXPECT_EQ(get_type_id(fn_shl_item(one, fractional)), LMD_TYPE_ERROR);
}

TEST(LambdaJitDebugInfo, FinalFunctionRangeUsesJitAllocationFrontier) {
    MIR_context_t ctx = MIR_init();
    ASSERT_NE(ctx, nullptr);
    MIR_gen_init(ctx);
    MIR_gen_set_optimize_level(ctx, 0);

    MIR_module_t module = MIR_new_module(ctx, "debug_span_test");
    MIR_type_t return_type = MIR_T_I64;
    MIR_item_t function = MIR_new_func(ctx, "debug_span_test", 1, &return_type, 0);
    MIR_reg_t accumulator = MIR_new_func_reg(ctx, function->u.func, MIR_T_I64, "accumulator");
    MIR_append_insn(ctx, function,
        MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, accumulator), MIR_new_int_op(ctx, 0)));
    for (int64_t i = 0; i < 12000; i++) {
        MIR_append_insn(ctx, function,
            MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, accumulator),
                         MIR_new_reg_op(ctx, accumulator), MIR_new_int_op(ctx, 1024 + i)));
    }
    MIR_append_insn(ctx, function, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, accumulator)));
    MIR_finish_func(ctx);
    MIR_finish_module(ctx);
    MIR_load_module(ctx, module);
    MIR_link(ctx, MIR_set_gen_interface, nullptr);
    ASSERT_NE(MIR_gen(ctx, function), nullptr);

    uint8_t* code_start = (uint8_t*)function->u.func->machine_code;
    uint8_t* code_end = _MIR_get_new_code_addr(ctx, 0);
    ASSERT_NE(code_start, nullptr);
    ASSERT_GT(code_end, code_start);
    ASSERT_GT((uintptr_t)(code_end - code_start), 65536u);

    void* debug_info = build_debug_info_table(ctx, nullptr);
    ASSERT_NE(debug_info, nullptr);
    EXPECT_NE(lookup_debug_info(debug_info, code_start + 65536), nullptr);
    free_debug_info_table(debug_info);

    MIR_gen_finish(ctx);
    MIR_finish(ctx);
}

TEST(LambdaValidator, TypeMismatchSuggestionsAreAttachedAndReported) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    SchemaValidator* validator = schema_validator_create(pool);
    ASSERT_NE(validator, nullptr);

    Type expected = {};
    expected.type_id = LMD_TYPE_STRING;
    Item actual = {.item = i2it(42)};
    ValidationResult* result = validator->validate_type(actual.to_const(), &expected);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->errors, nullptr);
    ASSERT_NE(result->errors->suggestions, nullptr);

    String* formatted = format_error_with_context(result->errors, pool);
    ASSERT_NE(formatted, nullptr);
    EXPECT_NE(strstr(formatted->chars, "Try wrapping the value in quotes"), nullptr);

    schema_validator_set_show_suggestions(validator, false);
    ValidationResult* suppressed = validator->validate_type(actual.to_const(), &expected);
    ASSERT_NE(suppressed, nullptr);
    ASSERT_NE(suppressed->errors, nullptr);
    EXPECT_EQ(suppressed->errors->suggestions, nullptr);
    String* suppressed_formatted = format_error_with_context(suppressed->errors, pool);
    ASSERT_NE(suppressed_formatted, nullptr);
    EXPECT_EQ(strstr(suppressed_formatted->chars, "Suggestions:"), nullptr);

    schema_validator_destroy(validator);
    pool_destroy(pool);
}

struct CallableRealmSnapshot {
    Item math_abs = ItemNull;
    Item array_constructor = ItemNull;
    bool stable_lookup = false;
    bool iterator_alias = false;
};

enum { JUBE_CATALOG_TEST_TYPES = 64, JUBE_CATALOG_TEST_WORKERS = 8 };

struct JubeCatalogWorker {
    uv_sem_t* start;
    JubeModuleDef module;
    JubeTypeDef types[JUBE_CATALOG_TEST_TYPES];
    JubeTypeBinding bindings[JUBE_CATALOG_TEST_TYPES];
    char names[JUBE_CATALOG_TEST_TYPES][40];
    StrBuf* source;
    int status;
    bool lookups_valid;
};

static int jube_catalog_test_get(Item, Item* out) { *out = ItemNull; return 1; }

static void jube_catalog_compile_worker(void* data) {
    JubeCatalogWorker* worker = (JubeCatalogWorker*)data;
    uv_sem_wait(worker->start);
    worker->status = jube_compile_module_interface(&worker->module);
    worker->lookups_valid = worker->status == 0;
    for (unsigned i = 0; i < JUBE_CATALOG_TEST_TYPES && worker->lookups_valid; i++) {
        const JubeTypeDef* type = &worker->types[i];
        // other modules may still be growing the shared catalog during these reads.
        worker->lookups_valid = jube_iface_type_by_name(type->name, strlen(type->name)) == type &&
            jube_member_count(type) == 1 && jube_member_ordinal(type, "value", 5) == 0;
        (void)jube_interface_registry_digest();
    }
}

TEST(JubeDom4, ConcurrentInterfacePublicationKeepsAllModuleRecords) {
    uint64_t before = jube_interface_registry_digest();
    uv_sem_t start;
    ASSERT_EQ(uv_sem_init(&start, 0), 0);
    JubeCatalogWorker workers[JUBE_CATALOG_TEST_WORKERS] = {};
    uv_thread_t threads[JUBE_CATALOG_TEST_WORKERS];
    static const JubeMemberBind member = {"value", nullptr, jube_catalog_test_get};
    unsigned created = 0;
    for (unsigned i = 0; i < JUBE_CATALOG_TEST_WORKERS; i++) {
        JubeCatalogWorker* worker = &workers[i];
        worker->start = &start;
        worker->source = strbuf_new();
        ASSERT_NE(worker->source, nullptr);
        for (unsigned j = 0; j < JUBE_CATALOG_TEST_TYPES; j++) {
            str_fmt(worker->names[j], sizeof(worker->names[j]), "catalog_race_%u_%u", i, j);
            worker->types[j].name = worker->names[j];
            worker->bindings[j].type_name = worker->names[j];
            worker->bindings[j].host_brand = &worker->types[j];
            worker->bindings[j].members = &member;
            worker->bindings[j].member_count = 1;
            strbuf_append_format(worker->source, "type %s { value: int }\n", worker->names[j]);
        }
        worker->module.abi_version = JUBE_ABI_VERSION;
        worker->module.struct_size = sizeof(JubeModuleDef);
        worker->module.name = worker->names[0];
        worker->module.types = worker->types;
        worker->module.type_count = JUBE_CATALOG_TEST_TYPES;
        worker->module.interface_decl = worker->source->str;
        worker->module.type_bindings = worker->bindings;
        worker->module.type_binding_count = JUBE_CATALOG_TEST_TYPES;
    }
    for (unsigned i = 0; i < JUBE_CATALOG_TEST_WORKERS; i++) {
        int rc = uv_thread_create(&threads[i], jube_catalog_compile_worker, &workers[i]);
        EXPECT_EQ(rc, 0);
        if (rc != 0) break;
        created++;
    }
    for (unsigned i = 0; i < created; i++) uv_sem_post(&start);
    for (unsigned i = 0; i < created; i++) ASSERT_EQ(uv_thread_join(&threads[i]), 0);
    EXPECT_EQ(created, JUBE_CATALOG_TEST_WORKERS);
    for (unsigned i = 0; i < created; i++) {
        EXPECT_EQ(workers[i].status, 0);
        EXPECT_TRUE(workers[i].lookups_valid);
        for (const JubeTypeDef& type : workers[i].types)
            EXPECT_EQ(jube_iface_type_by_name(type.name, strlen(type.name)), &type);
    }
    for (unsigned i = 0; i < JUBE_CATALOG_TEST_WORKERS; i++) {
        if (workers[i].source) {
            jube_interface_remove_module(&workers[i].module);
            strbuf_free(workers[i].source);
        }
    }
    uv_sem_destroy(&start);
    EXPECT_EQ(jube_interface_registry_digest(), before);
}

TEST(JubeDom4, CatalogueMembersPreserveColours) {
    jube_register_builtin_modules();
    const JubeModuleDef* radiant = jube_find_static_module("radiant");
    ASSERT_NE(radiant, nullptr);
    ASSERT_TRUE(jube_activate_module(radiant));

    // a member door must preserve its catalogue row's colour (D7.1.1).
    static const struct { const char* iface; const char* name; bool is_proc; } colours[] = {
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv, iface, member, js_name) \
        {#iface, (member)[0] ? member : #name, (sig)[0] == 'p'},
#include "../lambda/dom/dom_api.def"
#undef DOM_OP
    };
    for (const auto& entry : colours) {
        if (strcmp(entry.iface, "none") == 0) continue;
        const JubeTypeDef* type = jube_iface_type_by_name(entry.iface,
            (uint32_t)strlen(entry.iface));
        ASSERT_NE(type, nullptr) << entry.iface;
        int ordinal = jube_member_ordinal(type, entry.name, (uint32_t)strlen(entry.name));
        ASSERT_GE(ordinal, 0) << entry.iface << "." << entry.name;
        EXPECT_EQ(jube_member_is_proc_at(type, ordinal), entry.is_proc)
            << entry.iface << "." << entry.name;
    }
}

TEST(JubeDom4, LayoutRegistrationIsNotPublic) {
    jube_register_builtin_modules();
    for (const char* name : {"radiant", "dom"}) {
        const JubeModuleDef* module = jube_find_static_module(name);
        ASSERT_NE(module, nullptr);
        for (int32_t i = 0; i < module->function_count; i++) {
            EXPECT_STRNE(module->functions[i].name, "register_layout") << name;
        }
    }
}

TEST(JubeDom4, OrdinalParityCoversDeclaredMembersAndHusks) {
    Runtime runtime = {};
    runtime_init(&runtime);
    uint64_t result_home = 0;
    Item warmup = transpile_js_to_mir(&runtime, "0;", "<dom4-parity>", &result_home);
    ASSERT_FALSE(item_is_error(warmup));

    const JubeModuleDef* radiant = jube_find_static_module("radiant");
    ASSERT_NE(radiant, nullptr);
    ASSERT_TRUE(jube_activate_module(radiant));

    for (int type_index = 0; type_index < radiant_dom_type_binding_count; type_index++) {
        const JubeTypeBinding* binding = &radiant_dom_type_bindings[type_index];
        const JubeTypeDef* type = jube_iface_type_by_name(
            binding->type_name, (uint32_t)strlen(binding->type_name));
        ASSERT_NE(type, nullptr) << binding->type_name;
        int slot = jube_iface_type_slot(type);
        ASSERT_GE(slot, 0) << binding->type_name;
        int member_count = jube_member_count(type);
        ASSERT_GE(member_count, 0) << binding->type_name;
        // these wrappers and JS keys span allocating host calls; the native
        // stack is not a GC owner (D6.3.3).
        RootFrame roots(4);
        Rooted<Item> husk_root(roots, vmap_new());
        Rooted<Item> key_root(roots, ItemNull), camel_root(roots, ItemNull), value_root(roots, ItemNull);
        Item husk = husk_root.get();
        ASSERT_EQ(get_type_id(husk), LMD_TYPE_VMAP);
        husk.vmap->host_type = type;
        husk.vmap->host_data = nullptr;

        for (int ordinal = 0; ordinal < member_count; ordinal++) {
            const char* snake_name = jube_member_name_at(type, ordinal, false);
            const char* camel_name = jube_member_name_at(type, ordinal, true);
            ASSERT_NE(snake_name, nullptr) << binding->type_name << " ordinal " << ordinal;
            ASSERT_NE(camel_name, nullptr) << binding->type_name << " ordinal " << ordinal;
            // receiver-specific binding variants share a declared spelling;
            // name lookup selects its first ordinal, while all variants are guarded.
            int snake_ordinal = jube_member_ordinal(type, snake_name,
                (uint32_t)strlen(snake_name));
            int camel_ordinal = jube_member_ordinal(type, camel_name,
                (uint32_t)strlen(camel_name));
            ASSERT_GE(snake_ordinal, 0) << binding->type_name;
            ASSERT_GE(camel_ordinal, 0) << binding->type_name;
            ASSERT_STREQ(jube_member_name_at(type, snake_ordinal, false), snake_name);
            ASSERT_STREQ(jube_member_name_at(type, camel_ordinal, true), camel_name);
            key_root.set(js_make_string(snake_name));
            camel_root.set(js_make_string(camel_name));
            Item key = key_root.get();
            Item camel_key = camel_root.get();
            const char* member_name = camel_name;
            Item by_name = ItemNull;
            Item by_ordinal = ItemNull;
            int name_handled = jube_member_get(husk, key, &by_name);
            int ordinal_handled = jube_member_get_by_ordinal(
                husk, slot, (uint32_t)ordinal, &by_ordinal);
            EXPECT_EQ(name_handled, ordinal_handled)
                << binding->type_name << "." << member_name;
            EXPECT_EQ(by_name.item, by_ordinal.item)
                << binding->type_name << "." << member_name;

            Item camel_by_name = ItemNull;
            EXPECT_EQ(jube_member_get(husk, camel_key, &camel_by_name), name_handled);
            EXPECT_EQ(camel_by_name.item, by_name.item)
                << binding->type_name << "." << member_name;

            Item name_set = ItemNull;
            Item ordinal_set = ItemNull;
            value_root.set(js_make_string("dom4-parity"));
            Item value = value_root.get();
            // ordinal stores use JS Set completion, while Lambda Set
            // returns its assigned value (D9.5.4).
            EXPECT_EQ(jube_member_set_js(husk, key, value, husk, &name_set),
                jube_member_set_by_ordinal(husk, slot, (uint32_t)ordinal,
                    value, &ordinal_set));
            EXPECT_EQ(name_set.item, ordinal_set.item)
                << binding->type_name << "." << member_name;

            Item ordinal_call = ItemNull;
            int ordinal_called = jube_member_call_by_ordinal(
                husk, slot, (uint32_t)ordinal, nullptr, 0, &ordinal_call);
            Item name_method = ItemNull;
            int name_method_handled = jube_member_get(husk, key, &name_method);
            int name_called = 0;
            if (name_method_handled && get_type_id(name_method) == LMD_TYPE_FUNC) {
                name_called = 1;
                name_method = js_call_function(name_method, husk, nullptr, 0);
            }
            EXPECT_EQ(name_called, ordinal_called)
                << binding->type_name << "." << member_name;
            if (jube_member_kind_at(type, ordinal) != JUBE_MEMBER_KIND_METHOD) {
                EXPECT_EQ(ordinal_called, 0);
            }
        }

        // A valid family slot with a non-method ordinal must never enter the
        // call binding; this is the mismatched kind × member guard sweep.
        for (int ordinal = 0; ordinal < member_count; ordinal++) {
            if (jube_member_kind_at(type, ordinal) == JUBE_MEMBER_KIND_METHOD) continue;
            Item out = ItemNull;
            EXPECT_EQ(jube_member_call_by_ordinal(
                husk, slot, (uint32_t)ordinal, nullptr, 0, &out), 0)
                << binding->type_name << " ordinal " << ordinal;
        }
    }

    runtime_cleanup(&runtime);
}

static bool capture_callable_realm(Runtime* runtime,
                                   CallableRealmSnapshot* snapshot) {
    if (!runtime || !snapshot) return false;
    runtime_init(runtime);
    uint64_t result_home = 0;
    Item result = transpile_js_to_mir(runtime, "0;", "<callable-realm>",
        &result_home);
    if (item_is_error(result)) {
        runtime_cleanup(runtime);
        return false;
    }

    bool captured = false;
    {
        RootFrame roots(7);
        Rooted<Item> global_root(roots, js_get_global_this());
        Rooted<Item> math_root(roots, js_get_key_default(global_root.get(),
            js_make_string("Math")));
        Rooted<Item> abs_root(roots, js_get_key_default(math_root.get(),
            js_make_string("abs")));
        Rooted<Item> array_root(roots, js_get_key_default(global_root.get(),
            js_make_string("Array")));
        Rooted<Item> prototype_root(roots, js_get_key_default(array_root.get(),
            js_make_string("prototype")));
        Rooted<Item> values_root(roots, js_get_key_default(prototype_root.get(),
            js_make_string("values")));
        Rooted<Item> iterator_root(roots, js_get_key_default(prototype_root.get(),
            js_well_known_symbol_key(1)));

        snapshot->math_abs = abs_root.get();
        snapshot->array_constructor = array_root.get();
        snapshot->stable_lookup = abs_root.get().item ==
            js_get_key_default(math_root.get(), js_make_string("abs")).item;
        snapshot->iterator_alias = values_root.get().item == iterator_root.get().item;
        captured = get_type_id(snapshot->math_abs) == LMD_TYPE_FUNC &&
            get_type_id(snapshot->array_constructor) == LMD_TYPE_FUNC;
    }

    EvalContext* owner = runtime_get_eval_context(runtime);
    // D6.2/D7.2.2: leave both heaps alive while comparing identity, but
    // detach every derived TLS cache before another realm becomes current.
    if (!js_runtime_state_shutdown(owner) ||
            !eval_context_shutdown(owner)) {
        return false;
    }
    return captured;
}

TEST(JsCallableRealmIdentity, IntrinsicsAreStableWithinAndDistinctAcrossContexts) {
    Runtime first = {};
    Runtime second = {};
    CallableRealmSnapshot first_snapshot = {};
    CallableRealmSnapshot second_snapshot = {};

    bool first_captured = capture_callable_realm(&first, &first_snapshot);
    bool second_captured = capture_callable_realm(&second, &second_snapshot);

    EXPECT_TRUE(first_captured);
    EXPECT_TRUE(second_captured);
    EXPECT_TRUE(first_snapshot.stable_lookup);
    EXPECT_TRUE(second_snapshot.stable_lookup);
    EXPECT_TRUE(first_snapshot.iterator_alias);
    EXPECT_TRUE(second_snapshot.iterator_alias);
    EXPECT_NE(first_snapshot.math_abs.item, second_snapshot.math_abs.item);
    EXPECT_NE(first_snapshot.array_constructor.item,
              second_snapshot.array_constructor.item);

    runtime_cleanup(&second);
    runtime_cleanup(&first);
}

TEST(LambdaScriptCache, ReusesMirImageAcrossFreshRuntimes) {
    const char* source = "1 + 2";
    const char* identity = "<lambda-cache-fresh-runtime>";
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    LambdaTestNativeCompiler compiler;
    compiler.select(LAMBDA_TIER_JIT);
    Runtime first = {};
    runtime_init(&first);
    Input* first_output = run_script_mir(&first, source, (char*)identity, true);
    ASSERT_NE(first_output, nullptr);
    EXPECT_EQ(get_type_id(first_output->root), LMD_TYPE_INT);
    EXPECT_EQ(it2i(first_output->root), 3);
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Input* second_output = run_script_mir(&second, source, (char*)identity, true);
    ASSERT_NE(second_output, nullptr);
    EXPECT_EQ(get_type_id(second_output->root), LMD_TYPE_INT);
    EXPECT_EQ(it2i(second_output->root), 3);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.mir_builds, before.mir_builds + 1);
    EXPECT_GE(after.mir_hits, before.mir_hits + 1);
    EXPECT_GE(after.module_hits, before.module_hits + 1);
}

// D8.5.1: immutable images retain their compiler identity across fresh contexts.
struct LambdaCacheRuntime {
    Runtime runtime = {};
    LambdaCacheRuntime() { runtime_init(&runtime); }
    ~LambdaCacheRuntime() { runtime_cleanup(&runtime); }
};

TEST(LambdaAstIndex, PublishesUnreadDecompositionTargets) {
    struct IndexOwner {
        AstIndex value = {};
        ~IndexOwner() { ast_index_destroy(&value); }
    } index;
    // exceed the initial node-table capacity while publishing only two nodes.
    NameEntry targets[320] = {};
    NameEntry* entries[320];
    for (int i = 0; i < 320; i++) entries[i] = &targets[i];
    AstIdentNode source = {};
    source.node_type = AST_NODE_IDENT;
    source.entry = entries[0];
    AstDecomposeNode declaration = {};
    declaration.node_type = AST_NODE_DECOMPOSE;
    declaration.as = (AstNode*)&source;
    declaration.name_count = 320;
    declaration.entries = entries;
    ASSERT_TRUE(ast_index_build_profile(&index.value, (AstNode*)&declaration, NULL));
    EXPECT_EQ(index.value.count, 2u);
    EXPECT_EQ(index.value.binding_count, 320u);
    for (int i = 0; i < 320; i++) {
        AstBindingId id = ast_index_find_binding_entry(&index.value, entries[i]);
        EXPECT_NE(id, AST_BINDING_ID_INVALID);
        EXPECT_EQ(ast_index_binding(&index.value, id), entries[i]);
    }
    AstBindingId first = ast_index_find_binding_entry(&index.value, entries[0]);
    EXPECT_EQ(ast_index_binding_id(&index.value, (AstNode*)&source), first);
}

TEST(LambdaNativeModuleImports, DescriptorMetadataMatchesResolvedEntries) {
    LambdaCacheRuntime scope;
    const char* names[] = {"dom_node_name", "dom_create_node", "dom_set_base_and_extent"};
    for (const char* name : names) {
        ASSERT_NE(find_dynamic_sys_func_import(name), nullptr) << name;
        JitImportMetadata metadata = {};
        ASSERT_TRUE(jit_import_get_metadata(name, &metadata)) << name;
        EXPECT_EQ(metadata.ret_class, JIT_VALUE_BOXED_ITEM);
        EXPECT_EQ(metadata.gc_effect, JIT_EFFECT_MAY_GC);
        EXPECT_NE(metadata.reentry_effect, JIT_REENTRY_NO);
        EXPECT_EQ(jit_import_scalar_return_class(&metadata), SCALAR_RETURN_DYNAMIC);
    }
    JitImportMetadata missing = {};
    EXPECT_EQ(find_dynamic_sys_func_import("missing_descriptor_function"), nullptr);
    EXPECT_FALSE(jit_import_get_metadata("missing_descriptor_function", &missing));
}

TEST(LambdaScriptCache, ReusesMvpImagesAcrossCompilerSpellings) {
    const struct { const char* source; const char* identity; } cases[] = {
        {"fn answer() => 41; answer() + 1", "<lambda-cache-compiler-isolation>"},
        // method tables and bound receiver snapshots must survive evaluator
        // teardown and reuse the same MVP image (S9.3.1).
        {"type C { value: int,\n fn read() => value\n pn bump() { value = value + 1 } }\n"
         "pn main() { var c = <C value: 7>; let saved = c.read; c.bump()^;\n"
         "if (saved() == 7 and c.read() == 8) 42 else 0 }",
         "<lambda-cache-method-compiler-isolation>"},
        {"fn local(seed) { var value = seed; pn read() { return value };\n"
         "let saved = () => value; value = value + 2; return [saved(), value] };\n"
         "let result = local(38); if (result[0] == 38 and result[1] == 40) 42 else 0",
         "<lambda-cache-shared-home-isolation>"},
        // rest context and its generated entry survive cache reuse across
        // fresh EvalContexts; a nested default must see its own empty rest.
        {"fn defaults(n = len(varg())^, ...) any^ => n;\n"
         "fn tail(n, ...) => if (n == 0) varg(0) else tail(n - 1, n);\n"
         "fn outer(...) { let n = defaults(); return [n, len(varg()), tail(3, 9)] };\n"
         "let result = outer(1, 2); if (result == [0, 2, 1]) 42 else 0",
         "<lambda-cache-variadic-isolation>"},
        // binder selections belong to an invocation, including cached images
        // entered under a fresh EvalContext (D4.1.4).
        {"fn choose(T: type, value: T) T => value;\n"
         "fn selected(value: number as T) type => T;\n"
         "let a = choose(int, 40)^; let b = choose(float, 2.5)^;\n"
         "let integral = selected(3)^; let fractional = selected(3.5)^;\n"
         "if (a == 40 and b == 2.5 and integral == int and fractional == float) 42 else 0",
         "<lambda-cache-binder-isolation>"}
    };
    const char* compilers[] = {nullptr, "mvp", ""};
    for (const auto& test : cases) {
        const Script* owners[3] = {};
        for (unsigned i = 0; i < 3; i++) {
            LambdaTestNativeCompiler compiler(LAMBDA_TIER_JIT);
            compiler.select_compiler(compilers[i]);
            LambdaCacheRuntime scope;
            Runtime& runtime = scope.runtime;
            Input* output = run_script_mir(&runtime, test.source, (char*)test.identity, true);
            ASSERT_NE(output, nullptr);
            ASSERT_EQ(get_type_id(output->root), LMD_TYPE_INT);
            EXPECT_EQ(it2i(output->root), 42);
            ASSERT_NE(runtime.scripts, nullptr);
            ASSERT_GT(runtime.scripts->length, 0);
            Script* instance = (Script*)runtime.scripts->data[0];
            owners[i] = instance->cache_template ? instance->cache_template : instance;
            EXPECT_TRUE(owners[i]->cache_owned_template);
            if (i == 1) EXPECT_EQ(owners[1], owners[0]);
            if (i == 2) EXPECT_EQ(owners[2], owners[0]);
        }
    }
}

TEST(LambdaScriptCache, ForeignExportsRetainExecutionOwnedNamespaces) {
    // D7.4.1/D6.2.3: each fresh runtime owns the guest environment; a sealed
    // Core image must not retain a previous runtime's mutable JS namespace.
    const char* source =
        "import guest: ._mir_mvp_js_exports\n"
        "import other: ._mir_mvp_js_export_module\n"
        "let saved = guest.arity2\n"
        "let borrowed = other.exported_value()^\n"
        "let same = (saved == borrowed)^\n"
        "let initial = guest.arity0()^\n"
        "let first = saved(2, 3)^\n"
        "let changed = guest.step()^\n"
        "let later = saved(2, 3)^;\n"
        "if (same and initial == 41 and first == 46 and changed == 42 and later == 47) 42 else 0";
    LambdaTestNativeCompiler compiler(LAMBDA_TIER_JIT);
    compiler.select_compiler("mvp");
    for (int iteration = 0; iteration < 3; iteration++) {
        LambdaCacheRuntime scope;
        Input* output = run_script_mir(&scope.runtime, source,
            (char*)"test/lambda/mvp/_cache_js_exports.ls", true);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(get_type_id(output->root), LMD_TYPE_INT);
        EXPECT_EQ(it2i(output->root), 42);
        ASSERT_NE(scope.runtime.scripts, nullptr);
        ASSERT_GT(scope.runtime.scripts->length, 0);
        Script* script = (Script*)scope.runtime.scripts->data[0];
        ASSERT_NE(script->jit_context, nullptr);
        EXPECT_TRUE(script->cache_cross_lang_tainted);
        EXPECT_FALSE(script->cache_mir_artifact);
    }
}

TEST(LambdaScriptCache, MvpNativeViewsRetainStateAndHandlerHomes) {
    const char* source =
        "view <mvp_native_counter> state count: 0, value: 1.000000000000001 {\n"
        "  let delta = ~.delta;\n"
        "  <state count:count, value:value, delta:delta>\n"
        "}\n"
        "on advance(event) {\n"
        "  let churn = [for (i in 1 to 60) {text:string(i)}]\n"
        "  count = count + event.delta + delta\n"
        "  value = event.value\n"
        "  return value\n"
        "}\n"
        "on reject(event) { error(\"event failure\") }\n"
        "view int { 1.0e-310 }\n"
        "<mvp_native_counter delta:2, value:1.0e-310>\n";
    LambdaTestNativeCompiler compiler(LAMBDA_TIER_JIT);
    compiler.select_compiler("mvp");
    for (int iteration = 0; iteration < 2; iteration++) {
        TemplateHostSession* mounted = template_host_session_open(source,
            "test/lambda/mvp/_cache_native_views.ls");
        ASSERT_NE(mounted, nullptr);
        {
            TemplateHostBinding binding(mounted);
            ASSERT_TRUE(binding.valid());
            RootFrame roots(4);
            Rooted<Item> model(roots, Item{.item = template_host_session_root_word(mounted)});
            Rooted<Item> rendered(roots, fn_apply1(model.get()));
            Rooted<Item> scalar(roots, fn_apply1(Item{.item = i2it(1)}));
            EXPECT_FALSE(lambda_item_is_pending(scalar.get().item));
            EXPECT_EQ(get_type_id(scalar.get()), LMD_TYPE_FLOAT);
            EXPECT_EQ(it2d(scalar.get()), 1.0e-310);
            ASSERT_EQ(get_type_id(rendered.get()), LMD_TYPE_ELEMENT);
            EXPECT_EQ(it2i(item_attr(rendered.get(), "count")), 0);
            EXPECT_EQ(it2d(item_attr(rendered.get(), "value")), 1.000000000000001);
            TemplateEntry* entry = template_registry_match(g_template_registry, model.get(), false, nullptr);
            ASSERT_NE(entry, nullptr);
            ASSERT_NE(entry->body_func, nullptr);
            EXPECT_EQ(entry->interp_view, nullptr);
            ASSERT_NE(entry->handlers, nullptr);
            EXPECT_EQ(entry->handlers->interp_handler, nullptr);
            bool handled = false;
            Rooted<Item> outcome(roots, template_dispatch_event(model.get(), false,
                "advance", model.get(), &handled));
            EXPECT_TRUE(handled);
            EXPECT_FALSE(lambda_item_is_pending(outcome.get().item));
            EXPECT_EQ(get_type_id(outcome.get()), LMD_TYPE_FLOAT);
            EXPECT_EQ(it2d(outcome.get()), 1.0e-310);
            rendered.set(fn_apply1(model.get()));
            EXPECT_EQ(it2i(item_attr(rendered.get(), "count")), 4);
            EXPECT_EQ(it2d(item_attr(rendered.get(), "value")), 1.0e-310);
            outcome.set(template_dispatch_event(model.get(), false, "reject", model.get(), &handled));
            EXPECT_TRUE(handled);
            EXPECT_EQ(get_type_id(outcome.get()), LMD_TYPE_ERROR);
            LambdaError* error = it2err(outcome.get());
            ASSERT_NE(error, nullptr);
            EXPECT_STREQ(error->message, "event failure");
        }
        template_host_session_close(mounted);
    }
}

TEST(LambdaScriptCache, MvpCallsInterpretedFunctionAndNativeCallback) {
    // D7.4.2/D8.3.1: one context retains both entry ABIs through reentry;
    // the callback's containers, Error and cold float outlive both callees.
    const char* source =
        "fn through(f: function, cb: function) any^ => f(cb, 41)^\n"
        "fn callback(n: int) any^ {\n"
        "  let kept = {value: 1.0e-310 * n}\n"
        "  let churn = [for (i in 1 to 40) {text: string(i)}]\n"
        "  return [kept.value, error(\"callback error\"), kept]\n"
        "}\n"
        "<bridge through: through, callback: callback>\n";
    LambdaTestNativeCompiler compiler(LAMBDA_TIER_JIT);
    compiler.select_compiler("mvp");
    TemplateHostSession* mounted = template_host_session_open(source,
        "test/lambda/mvp/_cache_cross_tier_callback.ls");
    ASSERT_NE(mounted, nullptr);
    // fatal assertions still release the mounted session after leaving roots.
    auto exercise = [&]() {
        TemplateHostBinding binding(mounted);
        ASSERT_TRUE(binding.valid());
        ASSERT_NE(context->runtime, nullptr);
        RootFrame roots(8);
        Rooted<Item> model(roots, Item{.item = template_host_session_root_word(mounted)});
        Rooted<Item> through(roots, item_attr(model.get(), "through"));
        Rooted<Item> callback(roots, item_attr(model.get(), "callback"));
        ASSERT_EQ(get_type_id(through.get()), LMD_TYPE_FUNC);
        ASSERT_EQ(get_type_id(callback.get()), LMD_TYPE_FUNC);
        ASSERT_NE(through.get().function->ptr, nullptr);
        ASSERT_NE(callback.get().function->ptr, nullptr);
        // pin T0 while invoking the already compiled MVP entries, so this
        // unit cannot pass by promoting the interpreted leg to native code.
        compiler.select(LAMBDA_TIER_INTERP);
        Script* interpreted = load_script(context->runtime,
            "test/lambda/mvp/_cache_interpreted_relay.ls",
            "fn relay(cb: function, n: int) any^ {\n"
            "  let churn = [for (i in 1 to 40) {text: string(i)}]\n"
            "  cb(n)^\n"
            "}\n"
            "fn exhaust(cb: function, n: int) any^ => 1 + exhaust(cb, n + 1)^\n"
            "pn invoke(cb: function, n: int) any^ { return exhaust(cb, n)^ }\n"
            "pn caught(cb: function, n: int) any^ {\n"
            "  var outcome = 0\n"
            "  invoke(cb, n) ^ { outcome = 77 }\n"
            "  return outcome\n"
            "}\n"
            "<legs relay: relay, exhaust: exhaust, caught: caught>\n");
        ASSERT_NE(interpreted, nullptr);
        Runner runner = {};
        runner_init(context->runtime, &runner);
        runner.script = interpreted;
        runner.context = context;
        Rooted<Item> legs(roots, interp_run_script(&runner, false));
        ASSERT_EQ(get_type_id(legs.get()), LMD_TYPE_ELEMENT);
        Rooted<Item> relay(roots, item_attr(legs.get(), "relay"));
        Rooted<Item> exhaust(roots, item_attr(legs.get(), "exhaust"));
        Rooted<Item> caught(roots, item_attr(legs.get(), "caught"));
        ASSERT_EQ(get_type_id(relay.get()), LMD_TYPE_FUNC);
        ASSERT_EQ(get_type_id(exhaust.get()), LMD_TYPE_FUNC);
        ASSERT_EQ(get_type_id(caught.get()), LMD_TYPE_FUNC);
        ASSERT_EQ(relay.get().function->entry_abi, FN_ENTRY_ABI_LAMBDA_INTERPRETED);
        Rooted<Item> result(roots, ItemNull);
        uint64_t* root_top = context->side_root_top;
        result.set(fn_call2(through.get().function, relay.get(), callback.get()));
        EXPECT_EQ(context->side_root_top, root_top);
        EXPECT_EQ(relay.get().function->entry_abi, FN_ENTRY_ABI_LAMBDA_INTERPRETED);
        EXPECT_FALSE(lambda_item_is_pending(result.get().item));
        ASSERT_EQ(get_type_id(result.get()), LMD_TYPE_ARRAY);
        gc_collect(context->heap->gc, context->side_root_base,
            context->side_root_top - context->side_root_base);
        ASSERT_EQ(result.get().array->length, 3);
        EXPECT_DOUBLE_EQ(it2d(array_get(result.get().array, 0)), 1.0e-310 * 41.0);
        LambdaError* error = it2err(array_get(result.get().array, 1));
        ASSERT_NE(error, nullptr);
        EXPECT_STREQ(error->message, "callback error");
        Item kept = array_get(result.get().array, 2);
        EXPECT_DOUBLE_EQ(it2d(item_attr(kept, "value")), 1.0e-310 * 41.0);
        EXPECT_FALSE(interp_has_active_state());

        // exercise escaped and callee-local faults without consuming the
        // default recursion budget; restore the knob before any assertion.
        const char* depth = getenv("LAMBDA_INTERP_DEPTH");
        char* saved_depth = depth ? strdup(depth) : nullptr;
        bool selected_depth = shell_setenv("LAMBDA_INTERP_DEPTH", "48");
        bool landed = true;
        bool state_released = true;
        uint32_t capabilities[] = {LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY,
            LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER};
        for (uint32_t capability : capabilities) {
            LambdaRecoveryFrame* boundary = lambda_recovery_frame_begin_for(context, capability);
            bool restored = false;
            LambdaFaultReason reason = LAMBDA_FAULT_NONE;
            if (boundary && LAMBDA_RECOVERY_FRAME_SETJMP(boundary)) {
                restored = lambda_recovery_frame_restore_landing(boundary);
                reason = boundary->fault.reason;
                _lambda_stack_overflow_flag = false;
            } else if (boundary && lambda_recovery_frame_arm(boundary)) {
                result.set(fn_call2(through.get().function, exhaust.get(), callback.get()));
            }
            if (boundary) lambda_recovery_frame_end(boundary);
            landed &= restored && reason == LAMBDA_FAULT_STACK_OVERFLOW;
            state_released &= !interp_has_active_state();
        }
        result.set(fn_call2(caught.get().function, callback.get(), Item{.item = i2it(1)}));
        bool restored_depth = saved_depth
            ? shell_setenv("LAMBDA_INTERP_DEPTH", saved_depth)
            : shell_unsetenv("LAMBDA_INTERP_DEPTH");
        free(saved_depth);
        EXPECT_TRUE(selected_depth);
        EXPECT_TRUE(restored_depth);
        EXPECT_TRUE(landed);
        EXPECT_TRUE(state_released);
        EXPECT_FALSE(interp_has_active_state());
        EXPECT_EQ(get_type_id(result.get()), LMD_TYPE_INT);
        EXPECT_EQ(it2i(result.get()), 77);
        EXPECT_EQ(context->side_root_top, root_top);
        result.set(fn_call2(through.get().function, relay.get(), callback.get()));
        EXPECT_EQ(get_type_id(result.get()), LMD_TYPE_ARRAY);
        EXPECT_FALSE(interp_has_active_state());
    };
    exercise();
    template_host_session_close(mounted);
}

TEST(LambdaScriptCache, MvpNativeEditHandlersWriteTheModel) {
    const char* source =
        "edit <mvp_native_edit> { ~ }\n"
        "on update(event) { ~.value = event.next; return ~.value }\n"
        "<mvp_native_edit value:1, next:9>\n";
    LambdaTestNativeCompiler compiler(LAMBDA_TIER_JIT);
    compiler.select_compiler("mvp");
    TemplateHostSession* mounted = template_host_session_open(source,
        "test/lambda/mvp/_cache_native_edits.ls");
    ASSERT_NE(mounted, nullptr);
    {
        TemplateHostBinding binding(mounted);
        ASSERT_TRUE(binding.valid());
        RootFrame roots(2);
        Rooted<Item> model(roots, Item{.item = template_host_session_root_word(mounted)});
        ASSERT_EQ(get_type_id(model.get()), LMD_TYPE_ELEMENT);
        EXPECT_EQ(it2i(item_attr(model.get(), "value")), 1);
        edit_bridge_init(nullptr);
        bool handled = false;
        Rooted<Item> outcome(roots, template_dispatch_event(model.get(), true,
            "update", model.get(), &handled));
        EXPECT_TRUE(handled);
        EXPECT_EQ(get_type_id(outcome.get()), LMD_TYPE_INT);
        EXPECT_EQ(it2i(outcome.get()), 9);
        EXPECT_EQ(it2i(item_attr(model.get(), "value")), 9);
    }
    template_host_session_close(mounted);
}

TEST(LambdaScriptCache, MvpNativeHandlersJoinScopedChildren) {
    const char* source =
        "let initial = temp('mvp_event_join', {count:0})^;\n"
        "pn child() { sleep(1)^; put temp.'mvp_event_join'#count = 1 }\n"
        "view <mvp_event_join> { ~ }\n"
        "on join(event) {\n"
        "  if (true) { start(child) }\n"
        "  return temp.'mvp_event_join'#count\n"
        "}\n"
        "<mvp_event_join>\n";
    LambdaTestNativeCompiler compiler(LAMBDA_TIER_JIT);
    compiler.select_compiler("mvp");
    TemplateHostSession* mounted = template_host_session_open(source,
        "test/lambda/mvp/_cache_native_event_join.ls");
    ASSERT_NE(mounted, nullptr);
    {
        TemplateHostBinding binding(mounted);
        ASSERT_TRUE(binding.valid());
        RootFrame roots(2);
        Rooted<Item> model(roots, Item{.item = template_host_session_root_word(mounted)});
        bool handled = false;
        Rooted<Item> outcome(roots, template_dispatch_event(model.get(), false,
            "join", model.get(), &handled));
        EXPECT_TRUE(handled);
        // the nested block joins before the return reads the committed value.
        EXPECT_EQ(get_type_id(outcome.get()), LMD_TYPE_INT);
        EXPECT_EQ(it2i(outcome.get()), 1);
    }
    template_host_session_close(mounted);
}

TEST(LambdaScriptCache, AutoFallbackCachesMirImportConeAndViews) {
    const char* identity = "test/lambda/auto_cache_root.ls";
    const char* source =
        "import .script_cache_auto_child\n"
        "view int { ~ }\n"
        "edit <cache_auto_widget> state count: 0 { <div \"ok\"> }\n"
        "on click(evt) { count = count + 1 }\n"
        "apply(cache_auto_value + 41)\n";
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    LambdaTestNativeCompiler compiler;
    // A prior eager image must not occupy AUTO's fallback image key.
    compiler.select(LAMBDA_TIER_JIT);
    Runtime eager = {};
    runtime_init(&eager);
    Input* eager_output = run_script_mir(&eager, source, (char*)identity, true);
    ASSERT_NE(eager_output, nullptr);
    EXPECT_EQ(it2i(eager_output->root), 42);
    runtime_cleanup(&eager);

    compiler.select(LAMBDA_TIER_AUTO);
    Runtime first = {};
    runtime_init(&first);
    Input* first_output = run_script_mir(&first, source, (char*)identity, true);
    ASSERT_NE(first_output, nullptr);
    EXPECT_EQ(it2i(first_output->root), 42);
    ASSERT_NE(first.scripts, nullptr);
    Script* first_root = (Script*)first.scripts->data[0];
    ASSERT_TRUE(first_root->cache_owned_template);
    ASSERT_NE(first_root->direct_imports, nullptr);
    Script* first_dependency = (Script*)first_root->direct_imports->data[0];
    ASSERT_NE(first_dependency->cache_template, nullptr);
    const Script* first_dependency_image = first_dependency->cache_template;
    EXPECT_TRUE(first_dependency_image->cache_mir_artifact);
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Input* second_output = run_script_mir(&second, source, (char*)identity, true);
    ASSERT_NE(second_output, nullptr);
    EXPECT_EQ(it2i(second_output->root), 42);
    Script* second_root = (Script*)second.scripts->data[0];
    EXPECT_EQ(second_root->cache_template, first_root);
    ASSERT_NE(second_root->direct_imports, nullptr);
    Script* second_dependency = (Script*)second_root->direct_imports->data[0];
    EXPECT_EQ(second_dependency->cache_template, first_dependency_image);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.mir_builds, before.mir_builds + 2);
    EXPECT_GE(after.mir_hits, before.mir_hits + 1);
}

TEST(LambdaScriptCache, MapsLogicalUnitsToDenseModuleSlabs) {
    Runtime runtime = {};
    runtime_init(&runtime);

    Script* local_script = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    Script* colliding_local = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    Script* cached_script = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    ASSERT_NE(local_script, nullptr);
    ASSERT_NE(colliding_local, nullptr);
    ASSERT_NE(cached_script, nullptr);
    // A cache-wide logical unit must not become a sparse EvalContext slab ID.
    cached_script->cache_compilation_unit_id = 1;
    runtime_register_script(&runtime, local_script);
    runtime_register_script(&runtime, colliding_local);
    runtime_register_script(&runtime, cached_script);

    uint32_t dense_module_id = UINT32_MAX;
    ASSERT_TRUE(runtime_module_state_id_for_unit(&runtime,
        cached_script->cache_compilation_unit_id, &dense_module_id));
    EXPECT_EQ(local_script->module_state_id, 0u);
    EXPECT_EQ(colliding_local->module_state_id, 1u);
    EXPECT_EQ(cached_script->module_state_id, 2u);
    EXPECT_EQ(dense_module_id, cached_script->module_state_id);
    EXPECT_EQ(script_module_layout_id(colliding_local),
        colliding_local->module_state_id);
    EXPECT_EQ(script_module_layout_id(cached_script),
        cached_script->cache_compilation_unit_id |
        LAMBDA_MODULE_ID_LOGICAL_UNIT_FLAG);

    runtime_cleanup(&runtime);
}

TEST(LambdaScriptCache, ReusesInterpreterAstTemplateAcrossFreshRuntimes) {
    const char* source = "40 + 2";
    const char* identity = "<lambda-cache-interpreter-ast>";
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    LambdaTestNativeCompiler compiler;
    compiler.select(LAMBDA_TIER_INTERP);
    Runtime first = {};
    runtime_init(&first);
    Input* first_output = run_script_mir(&first, source, (char*)identity, true);
    ASSERT_NE(first_output, nullptr);
    ASSERT_EQ(it2i(first_output->root), 42);
    ASSERT_NE(first.scripts, nullptr);
    Script* first_template = (Script*)first.scripts->data[0];
    ASSERT_TRUE(first_template->cache_owned_template);
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Input* second_output = run_script_mir(&second, source, (char*)identity, true);
    ASSERT_NE(second_output, nullptr);
    EXPECT_EQ(it2i(second_output->root), 42);
    ASSERT_NE(second.scripts, nullptr);
    Script* second_instance = (Script*)second.scripts->data[0];
    EXPECT_EQ(second_instance->cache_template, first_template);
    uint32_t dense_module_id = UINT32_MAX;
    EXPECT_TRUE(runtime_module_state_id_for_unit(&second,
        second_instance->cache_compilation_unit_id, &dense_module_id));
    EXPECT_EQ(dense_module_id, second_instance->module_state_id);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.ast_builds, before.ast_builds + 1);
    EXPECT_GE(after.ast_hits, before.ast_hits + 1);
    EXPECT_GE(after.module_hits, before.module_hits + 1);
}

TEST(LambdaScriptCache, ReusesInterpreterAstImportConeAcrossFreshRuntimes) {
    const char* path = "test/lambda/import_vars.ls";
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    LambdaTestNativeCompiler compiler;
    compiler.select(LAMBDA_TIER_INTERP);
    Runtime first = {};
    runtime_init(&first);
    Input* first_output = run_script_mir(&first, NULL, (char*)path, true);
    ASSERT_NE(first_output, nullptr);
    ASSERT_NE(get_type_id(first_output->root), LMD_TYPE_ERROR);
    ASSERT_NE(first.scripts, nullptr);
    Script* first_root = (Script*)first.scripts->data[0];
    ASSERT_TRUE(first_root->cache_owned_template);
    ASSERT_NE(first_root->direct_imports, nullptr);
    ASSERT_EQ(first_root->direct_imports->length, 1);
    Script* first_dependency = (Script*)first_root->direct_imports->data[0];
    // Static prebuild may hand the importer an execution shell around the AST owner.
    const Script* first_dependency_image = first_dependency->cache_template
        ? first_dependency->cache_template : first_dependency;
    ASSERT_TRUE(first_dependency_image->cache_owned_template);
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Input* second_output = run_script_mir(&second, NULL, (char*)path, true);
    ASSERT_NE(second_output, nullptr);
    ASSERT_NE(get_type_id(second_output->root), LMD_TYPE_ERROR);
    ASSERT_NE(second.scripts, nullptr);
    Script* second_root = (Script*)second.scripts->data[0];
    ASSERT_EQ(second_root->cache_template, first_root);
    ASSERT_NE(second_root->direct_imports, nullptr);
    ASSERT_EQ(second_root->direct_imports->length, 1);
    Script* second_dependency = (Script*)second_root->direct_imports->data[0];
    EXPECT_NE(second_dependency, first_dependency);
    EXPECT_EQ(second_dependency->cache_template, first_dependency_image);
    uint32_t dependency_slab = UINT32_MAX;
    EXPECT_TRUE(runtime_module_state_id_for_unit(&second,
        second_dependency->cache_compilation_unit_id, &dependency_slab));
    EXPECT_EQ(dependency_slab, second_dependency->module_state_id);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.ast_builds, before.ast_builds + 2);
    EXPECT_GE(after.ast_hits, before.ast_hits + 1);
    EXPECT_GE(after.module_hits, before.module_hits + 1);
}

TEST(LambdaScriptCache, ReusesInterpreterAstViewTemplateAcrossFreshRuntimes) {
    const char* path = "test/lambda/script_cache_view_overlay.ls";
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    LambdaTestNativeCompiler compiler;
    compiler.select(LAMBDA_TIER_INTERP);
    Runtime first = {};
    runtime_init(&first);
    Input* first_output = run_script_mir(&first, NULL, (char*)path, true);
    ASSERT_NE(first_output, nullptr);
    ASSERT_EQ(it2i(first_output->root), 42);
    ASSERT_NE(first.scripts, nullptr);
    Script* first_template = (Script*)first.scripts->data[0];
    ASSERT_TRUE(first_template->cache_owned_template);
    ASSERT_NE(first_template->ast_overlay_strings, nullptr);
    ASSERT_EQ(first_template->ast_overlay_strings->length, 1);
    const char* first_ref = (const char*)first_template->ast_overlay_strings->data[0];
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Input* second_output = run_script_mir(&second, NULL, (char*)path, true);
    ASSERT_NE(second_output, nullptr);
    EXPECT_EQ(it2i(second_output->root), 42);
    ASSERT_NE(second.scripts, nullptr);
    Script* second_instance = (Script*)second.scripts->data[0];
    EXPECT_EQ(second_instance->cache_template, first_template);
    ASSERT_NE(second_instance->ast_overlay_strings, nullptr);
    ASSERT_EQ(second_instance->ast_overlay_strings->length, 1);
    EXPECT_NE(second_instance->ast_overlay_strings->data[0], first_ref);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.ast_builds, before.ast_builds + 1);
    EXPECT_GE(after.ast_hits, before.ast_hits + 1);
    EXPECT_GE(after.module_hits, before.module_hits + 1);
}

TEST(LambdaScriptCache, ReusesImportedMirConeAcrossFreshRuntimes) {
    const char* path = "test/lambda/import_vars.ls";
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    LambdaTestNativeCompiler compiler;
    compiler.select(LAMBDA_TIER_JIT);
    Runtime first = {};
    runtime_init(&first);
    Input* first_output = run_script_mir(&first, NULL, (char*)path, true);
    ASSERT_NE(first_output, nullptr);
    EXPECT_NE(get_type_id(first_output->root), LMD_TYPE_ERROR);
    ASSERT_NE(first.scripts, nullptr);
    Script* first_root = (Script*)first.scripts->data[0];
    ASSERT_TRUE(first_root->cache_owned_template);
    ASSERT_NE(first_root->direct_imports, nullptr);
    ASSERT_EQ(first_root->direct_imports->length, 1);
    Script* first_dependency = (Script*)first_root->direct_imports->data[0];
    ASSERT_TRUE(first_dependency->cache_owned_template);
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Input* second_output = run_script_mir(&second, NULL, (char*)path, true);
    ASSERT_NE(second_output, nullptr);
    EXPECT_NE(get_type_id(second_output->root), LMD_TYPE_ERROR);
    ASSERT_NE(second.scripts, nullptr);
    Script* second_root = (Script*)second.scripts->data[0];
    EXPECT_EQ(second_root->cache_template, first_root);
    ASSERT_NE(second_root->direct_imports, nullptr);
    ASSERT_EQ(second_root->direct_imports->length, 1);
    Script* second_dependency = (Script*)second_root->direct_imports->data[0];
    EXPECT_NE(second_dependency, first_dependency);
    EXPECT_EQ(second_dependency->cache_template, first_dependency);
    uint32_t dependency_slab = UINT32_MAX;
    EXPECT_TRUE(runtime_module_state_id_for_unit(&second,
        second_dependency->cache_compilation_unit_id, &dependency_slab));
    EXPECT_EQ(dependency_slab, second_dependency->module_state_id);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.mir_builds, before.mir_builds + 2);
    EXPECT_GE(after.mir_hits, before.mir_hits + 1);
    EXPECT_GE(after.module_hits, before.module_hits + 1);
}

TEST(LambdaScriptCache, RetiresCachedImportConeWhenDependencyFileChanges) {
    const char* root_path = "temp/script_cache_dependency_root.ls";
    const char* dependency_path = "temp/script_cache_dependency_child.ls";
    const char* root_source = "import .script_cache_dependency_child\ncache_dependency_value";
    const char* first_dependency_source = "pub let cache_dependency_value = 1";
    const char* second_dependency_source = "pub let cache_dependency_value = 22";
    ASSERT_EQ(write_binary_file(root_path, root_source, strlen(root_source)), 0);
    ASSERT_EQ(write_binary_file(dependency_path, first_dependency_source,
        strlen(first_dependency_source)), 0);

    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    LambdaTestNativeCompiler compiler;
    compiler.select(LAMBDA_TIER_JIT);
    Runtime first = {};
    runtime_init(&first);
    Input* first_output = run_script_mir(&first, NULL, (char*)root_path, true);
    ASSERT_NE(first_output, nullptr);
    ASSERT_EQ(it2i(first_output->root), 1);
    ASSERT_NE(first.scripts, nullptr);
    Script* first_root = (Script*)first.scripts->data[0];
    ASSERT_TRUE(first_root->cache_owned_template);
    ASSERT_NE(first_root->direct_imports, nullptr);
    ASSERT_EQ(first_root->direct_imports->length, 1);
    Script* first_dependency =
        (Script*)first_root->direct_imports->data[0];
    ASSERT_TRUE(first_dependency->cache_owned_template);
    runtime_cleanup(&first);

    // Keep the replacement a different size so the file-generation detector
    // forces the common cache to verify exact source bytes before a cache hit.
    ASSERT_EQ(write_binary_file(dependency_path, second_dependency_source,
        strlen(second_dependency_source)), 0);
    Runtime second = {};
    runtime_init(&second);
    Input* second_output = run_script_mir(&second, NULL, (char*)root_path, true);
    ASSERT_NE(second_output, nullptr);
    EXPECT_EQ(it2i(second_output->root), 22);
    ASSERT_NE(second.scripts, nullptr);
    Script* second_root = (Script*)second.scripts->data[0];
    EXPECT_NE(second_root->cache_template, first_root);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.invalidations, before.invalidations + 2);
    EXPECT_GE(after.dependency_invalidations,
        before.dependency_invalidations + 1);
}

typedef struct LambdaCacheBuildThread {
    const char* source;
    const char* identity;
    pthread_mutex_t* mutex;
    pthread_cond_t* start_cond;
    int* waiting;
    int worker_count;
    bool passed;
} LambdaCacheBuildThread;

static void* lambda_cache_build_thread_run(void* value) {
    LambdaCacheBuildThread* worker = (LambdaCacheBuildThread*)value;
    pthread_mutex_lock(worker->mutex);
    (*worker->waiting)++;
    if (*worker->waiting == worker->worker_count) {
        pthread_cond_broadcast(worker->start_cond);
    }
    else {
        while (*worker->waiting < worker->worker_count) {
            pthread_cond_wait(worker->start_cond, worker->mutex);
        }
    }
    pthread_mutex_unlock(worker->mutex);

    Runtime runtime = {};
    runtime_init(&runtime);
    Script* script = load_script_mir_direct(&runtime, worker->identity,
        worker->source, false);
    worker->passed = script && script->main_func;
    runtime_cleanup(&runtime);
    return NULL;
}

TEST(LambdaScriptCache, SingleFlightsSameSourceMirBuildAcrossFreshRuntimes) {
    const int term_count = 64;
    const size_t source_length = (size_t)(term_count - 1) * 4 + 1;
    char* source = (char*)mem_alloc(source_length + 1, MEM_CAT_SYSTEM);
    ASSERT_NE(source, nullptr);
    char* cursor = source;
    for (int i = 1; i < term_count; i++) {
        memcpy(cursor, "1 + ", 4);
        cursor += 4;
    }
    *cursor++ = '1';
    *cursor = '\0';

    static int cache_single_flight_generation = 0;
    char identity[64];
    snprintf(identity, sizeof(identity), "<lambda-cache-single-flight-%d>",
        ++cache_single_flight_generation);

    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);
    LambdaTestNativeCompiler compiler;
    compiler.select(LAMBDA_TIER_JIT);

    const int worker_count = 6;
    pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
    int waiting = 0;
    LambdaCacheBuildThread workers[worker_count] = {};
    pthread_t threads[worker_count];
    for (int i = 0; i < worker_count; i++) {
        workers[i].source = source;
        workers[i].identity = identity;
        workers[i].mutex = &start_mutex;
        workers[i].start_cond = &start_cond;
        workers[i].waiting = &waiting;
        workers[i].worker_count = worker_count;
        ASSERT_EQ(pthread_create(&threads[i], NULL, lambda_cache_build_thread_run,
            &workers[i]), 0);
    }
    for (int i = 0; i < worker_count; i++) {
        ASSERT_EQ(pthread_join(threads[i], NULL), 0);
        EXPECT_TRUE(workers[i].passed);
    }
    pthread_cond_destroy(&start_cond);
    pthread_mutex_destroy(&start_mutex);
    mem_free(source);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_EQ(after.mir_builds, before.mir_builds + 1);
    EXPECT_GE(after.mir_hits, before.mir_hits + worker_count - 1);
    EXPECT_GE(after.single_flight_waits, before.single_flight_waits + 1);
}

static gc_heap_t* concurrency_test_gc;

typedef struct RecordFrame {
    int id;
    int* order;
    int* order_count;
    Item result;
    LambdaTask* send_target;
    Item send_value;
} RecordFrame;

// Test frames are plain C structs owned by the test; an entry receives one as
// a raw pointer Item, which the collector ignores as unmanaged.
static Item frame_arg(void* frame) {
    return (Item){.item = (uint64_t)(uintptr_t)frame};
}

static LambdaTask* entry_task(Activation* self) {
    return (LambdaTask*)activation_user(self);
}

static Item record_and_complete(Activation* self, Item arg) {
    RecordFrame* frame = (RecordFrame*)(uintptr_t)arg.item;
    frame->order[(*frame->order_count)++] = frame->id;
    if (frame->send_target) {
        EXPECT_EQ(lambda_task_send(entry_task(self), frame->send_target,
            frame->send_value), LAMBDA_SEND_OK);
    }
    return frame->result;
}

typedef struct ParkFrame {
    int runs;
    Item resumed;
} ParkFrame;

typedef struct WaitFrame {
    Item handle;
    Item timeout;
} WaitFrame;

// Parks once with no registered wakeup; the test supplies the wake.
static Item park_then_finish(Activation* self, Item arg) {
    ParkFrame* frame = (ParkFrame*)(uintptr_t)arg.item;
    frame->runs++;
    frame->resumed = lambda_task_suspend(entry_task(self));
    frame->runs++;
    return frame->resumed;
}

static Item wait_with_timeout(Activation* self, Item arg) {
    (void)self;
    WaitFrame* frame = (WaitFrame*)(uintptr_t)arg.item;
    return pn_wait2(frame->handle, frame->timeout);
}

static Item raise_task_fault(Activation* self, Item arg) {
    (void)self;
    (void)arg;
    // The activation owns the native target for this fault; it can never
    // jump to the scheduler's stack.
    if (!lambda_recovery_frame_raise_fault(
            LAMBDA_FAULT_SIDE_STACK_EXHAUSTION, ERR_TYPE_MISMATCH)) {
        ADD_FAILURE() << "task fault had no recovery target";
    }
    return ItemNull;
}

// Holds a fresh VMap in the parked task's own root segment.
static Item hold_vmap_then_park(Activation* self, Item arg) {
    VMap** held_out = (VMap**)(uintptr_t)arg.item;
    RootFrame roots(1);
    Rooted<Item> held(roots, (Item){.vmap = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP)});
    *held_out = held.get().vmap;
    (void)lambda_task_suspend(entry_task(self));
    return held.get();
}

class LambdaConcurrencyRuntime : public ::testing::Test {
protected:
    EvalContext eval = {};
    Heap heap = {};
    Pool* pool = NULL;
    LambdaScheduler* scheduler = NULL;

    void SetUp() override {
        concurrency_test_gc = gc_heap_create();
        ASSERT_NE(concurrency_test_gc, nullptr);
        heap.gc = concurrency_test_gc;
        pool = mem_pool_create(NULL, MEM_ROLE_RUNTIME_HEAP, "test.concurrency.runtime");
        ASSERT_NE(pool, nullptr);
        heap.pool = pool;
        eval.heap = &heap;
        ASSERT_TRUE(eval_context_init(&eval));
        err_set_heap_allocator(heap_calloc);
        gc_set_root_visitor(concurrency_test_gc, activation_gc_visit_roots);
        scheduler = lambda_scheduler_create(3);
        ASSERT_NE(scheduler, nullptr);
        eval.scheduler = scheduler;
    }

    void TearDown() override {
        lambda_scheduler_destroy(scheduler);
        scheduler = NULL;
        eval.scheduler = NULL;
        lambda_uv_cleanup();
        // the minimal fixture owns the same realm shape arena as a full runtime.
        runtime_shape_tree_release(&eval);
        err_set_heap_allocator(NULL);
        EXPECT_TRUE(eval_context_shutdown(&eval));
        gc_heap_destroy(concurrency_test_gc);
        concurrency_test_gc = NULL;
        if (eval.name_pool) name_pool_release(eval.name_pool);
        eval.name_pool = NULL;
        mem_pool_destroy(pool);
        pool = NULL;
    }
};

TEST_F(LambdaConcurrencyRuntime, GcMapAllocatorHonorsMinimumCapacity) {
    // shape metadata outlives every map through the fixture's heap teardown.
    TypeMap* shape = (TypeMap*)pool_calloc(pool, sizeof(TypeMap));
    ASSERT_NE(shape, nullptr);
    shape->type_id = LMD_TYPE_MAP;
    shape->byte_size = sizeof(Item);
    const int64_t capacities[] = {0, 4, 64};
    RootFrame roots(1);
    Rooted<Map*> owner(roots, nullptr);
    for (int64_t capacity : capacities) {
        owner.set(map_alloc_for_type(shape, capacity));
        ASSERT_NE(owner.get(), nullptr);
        ASSERT_TRUE(gc_is_managed(concurrency_test_gc, owner.get()));
        int64_t expected = capacity > shape->byte_size ? capacity : shape->byte_size;
        EXPECT_EQ(owner.get()->type, shape);
        EXPECT_EQ(owner.get()->data_cap, expected);
        EXPECT_EQ(owner.get()->data, (char*)owner.get() + sizeof(Map));
        memset(owner.get()->data, 0x5a, (size_t)expected);
        // the GC owns the header and inline data across a rooted collection.
        gc_collect(concurrency_test_gc, eval.side_root_base,
            eval.side_root_top - eval.side_root_base);
        EXPECT_FALSE(gc_get_header(owner.get())->gc_flags & GC_FLAG_FREED);
        for (int64_t i = 0; i < expected; i++)
            EXPECT_EQ(((uint8_t*)owner.get()->data)[i], 0x5a);
    }
    EXPECT_EQ(map_alloc_for_type(nullptr, 0), nullptr);
    EXPECT_EQ(map_alloc_for_type(shape, (int64_t)INT32_MAX + 1), nullptr);
}

TEST_F(LambdaConcurrencyRuntime, NativeMethodStorageUsesEphemeronsWithoutJsRealm) {
    EXPECT_EQ(js_active_runtime_state, nullptr);
    concurrency_test_gc->js_native_trace = [](void* data, gc_heap_t* gc) {
        native_collection_gc_trace((Map*)data, gc);
    };
    concurrency_test_gc->js_native_destroy = [](void* data) {
        gc_native_seen_t seen = {};
        gc_native_seen_init(&seen);
        native_collection_heap_destroy((Map*)data, &seen);
        gc_native_seen_dispose(&seen);
    };
    TypeMap shape = {};
    shape.type_id = LMD_TYPE_MAP;
    RootFrame roots(4);
    Rooted<Item> storage(roots, native_collection_create(&shape, pool, NATIVE_COLLECTION_MAP, true));
    ASSERT_EQ(get_type_id(storage.get()), LMD_TYPE_MAP);
    Rooted<Item> key(roots, {.map = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP)});
    ASSERT_NE(key.get().map, nullptr);
    key.get().map->type_id = LMD_TYPE_MAP;
    key.get().map->type = &shape;
    Rooted<void*> environment(roots, heap_calloc_closure_env(sizeof(Item)));
    ASSERT_NE(environment.get(), nullptr);
    owned_item_slot_store((Item*)environment.get(), 1, 0, key.get());
    Rooted<Function*> captured(roots, to_closure_named(nullptr, 0, environment.get(), "method test"));
    ASSERT_NE(captured.get(), nullptr);
    captured.get()->closure_field_count = 1;
    NativeCollectionData* data = native_collection_data(storage.get());
    ASSERT_TRUE(native_collection_set(data, key.get(), {.function = captured.get()}));
    Function* observed = captured.get();
    captured.set(nullptr);
    environment.set(nullptr);
    gc_collect(concurrency_test_gc, eval.side_root_base, eval.side_root_top - eval.side_root_base);
    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, observed));
    EXPECT_FALSE(gc_get_header(observed)->gc_flags & GC_FLAG_FREED);
    EXPECT_EQ(hashmap_count(data->hmap), 1u);
    // the value captures its key, but that cycle must not keep either alive
    // after the last independent key root is gone (D6.1.1).
    key.set(ItemNull);
    gc_collect(concurrency_test_gc, eval.side_root_base, eval.side_root_top - eval.side_root_base);
    // ownership queries include freed object-zone slots; the header records
    // reclamation until the next allocation can reuse that slot.
    EXPECT_TRUE(gc_get_header(observed)->gc_flags & GC_FLAG_FREED);
    EXPECT_EQ(hashmap_count(data->hmap), 0u);
    gc_native_seen_t seen = {};
    gc_native_seen_init(&seen);
    native_collection_heap_destroy(storage.get().map, &seen);
    gc_native_seen_dispose(&seen);
}

struct AttachmentTeardownProbe {
    int worker_started;
    int worker_finished;
    int offload_released;
    int attachment_released;
    bool release_order_valid;
};

static void attachment_teardown_worker(LambdaOffload* job, void* data) {
    AttachmentTeardownProbe* probe = (AttachmentTeardownProbe*)data;
    __atomic_store_n(&probe->worker_started, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(lambda_offload_cancel_flag(job), __ATOMIC_ACQUIRE)) uv_sleep(1);
    __atomic_store_n(&probe->worker_finished, 1, __ATOMIC_RELEASE);
}

static void attachment_teardown_offload_release(void* data) {
    AttachmentTeardownProbe* probe = (AttachmentTeardownProbe*)data;
    probe->release_order_valid = __atomic_load_n(&probe->worker_finished, __ATOMIC_ACQUIRE) != 0;
    probe->offload_released++;
}

static void attachment_teardown_attachment_release(void* data) {
    AttachmentTeardownProbe* probe = (AttachmentTeardownProbe*)data;
    probe->release_order_valid = probe->release_order_valid && probe->offload_released == 1;
    probe->attachment_released++;
}

static Item attachment_teardown_task(Activation* self, Item arg) {
    AttachmentTeardownProbe* probe = (AttachmentTeardownProbe*)(uintptr_t)arg.item;
    static const char attachment_key = 0;
    static const LambdaOffloadOps ops = {
        attachment_teardown_worker, NULL, attachment_teardown_offload_release
    };
    if (!lambda_task_attachment_install(entry_task(self), &attachment_key, probe,
            attachment_teardown_attachment_release)) return ItemError;
    return lambda_task_offload(&ops, probe, 0);
}

// S12.4.2–S12.4.3: native resources must outlive orphaned workers during shutdown.
TEST_F(LambdaConcurrencyRuntime, ShutdownJoinsOffloadBeforeReleasingTaskAttachment) {
    AttachmentTeardownProbe probe = {};
    ASSERT_NE(lambda_task_create(scheduler, attachment_teardown_task, frame_arg(&probe)), nullptr);
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    for (int i = 0; i < 1000 && !__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE); i++) uv_sleep(1);
    ASSERT_EQ(__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE), 1);
    lambda_scheduler_destroy(scheduler);
    scheduler = NULL;
    eval.scheduler = NULL;
    EXPECT_EQ(probe.offload_released, 1);
    EXPECT_EQ(probe.attachment_released, 1);
    EXPECT_TRUE(probe.release_order_valid);
}

struct InterruptTeardownProbe {
    uv_sem_t started;
    uv_sem_t interrupted;
    int worker_done;
    int cancel_done;
    int released;
    bool release_order_valid;
    bool delay_worker;
};
static void interrupt_teardown_worker(LambdaOffload*, void* data) {
    InterruptTeardownProbe* probe = (InterruptTeardownProbe*)data;
    uv_sem_post(&probe->started);
    uv_sem_wait(&probe->interrupted);
    if (probe->delay_worker) uv_sleep(30);
    __atomic_store_n(&probe->worker_done, 1, __ATOMIC_RELEASE);
}
static void interrupt_teardown_cancel(void* data) {
    InterruptTeardownProbe* probe = (InterruptTeardownProbe*)data;
    uv_sem_post(&probe->interrupted);
    if (!probe->delay_worker) uv_sleep(30);
    __atomic_store_n(&probe->cancel_done, 1, __ATOMIC_RELEASE);
}
static void interrupt_teardown_release(void* data) {
    InterruptTeardownProbe* probe = (InterruptTeardownProbe*)data;
    probe->release_order_valid = __atomic_load_n(&probe->worker_done, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&probe->cancel_done, __ATOMIC_ACQUIRE);
    probe->released++;
}
static Item interrupt_teardown_task(Activation*, Item arg) {
    static const LambdaOffloadOps ops = {
        interrupt_teardown_worker, NULL, interrupt_teardown_release, interrupt_teardown_cancel
    };
    return lambda_task_offload(&ops, (void*)(uintptr_t)arg.item, 0);
}

TEST_F(LambdaConcurrencyRuntime, ShutdownJoinsInterruptAndWorkerBeforeRelease) {
    for (int phase = 0; phase < 2; phase++) {
        InterruptTeardownProbe probe = {};
        probe.delay_worker = phase != 0;
        ASSERT_EQ(uv_sem_init(&probe.started, 0), 0);
        ASSERT_EQ(uv_sem_init(&probe.interrupted, 0), 0);
        ASSERT_NE(lambda_task_create(scheduler, interrupt_teardown_task, frame_arg(&probe)), nullptr);
        ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
        uv_sem_wait(&probe.started);
        lambda_scheduler_destroy(scheduler);
        scheduler = NULL;
        eval.scheduler = NULL;
        EXPECT_EQ(probe.released, 1);
        EXPECT_TRUE(probe.release_order_valid);
        uv_sem_destroy(&probe.started);
        uv_sem_destroy(&probe.interrupted);
        if (phase == 0) {
            scheduler = lambda_scheduler_create(3);
            ASSERT_NE(scheduler, nullptr);
            eval.scheduler = scheduler;
        }
    }
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
struct RuntimeDatabaseTeardownProbe {
    Pool* pool;
    RdbSqliteFileSession* session;
    RdbSqliteClone* clone;
    int worker_started;
    int worker_finished;
    int other_closed;
    int attachment_released;
    int clone_released;
    int close_status;
    bool current_at_release;
    bool worker_joined_at_release;
    bool other_closed_at_release;
    bool close_refused_while_cloned;
};
static const char runtime_database_attachment_key = 0;

static void runtime_database_worker(LambdaOffload* job, void* data) {
    RuntimeDatabaseTeardownProbe* probe = (RuntimeDatabaseTeardownProbe*)data;
    __atomic_store_n(&probe->worker_started, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(lambda_offload_cancel_flag(job), __ATOMIC_ACQUIRE)) uv_sleep(1);
    rdb_sqlite_clone_run(probe->clone, lambda_offload_cancel_flag(job));
    __atomic_store_n(&probe->worker_finished, 1, __ATOMIC_RELEASE);
}
static void runtime_database_clone_release(void* data) {
    RuntimeDatabaseTeardownProbe* probe = (RuntimeDatabaseTeardownProbe*)data;
    probe->close_refused_while_cloned = rdb_sqlite_file_session_close(probe->session) == RDB_ERROR;
    rdb_sqlite_clone_discard(probe->clone);
    probe->clone = NULL;
    probe->clone_released++;
}
static void runtime_database_release(void* data) {
    RuntimeDatabaseTeardownProbe* probe = (RuntimeDatabaseTeardownProbe*)data;
    probe->worker_joined_at_release = __atomic_load_n(&probe->worker_finished, __ATOMIC_ACQUIRE) != 0;
    probe->other_closed_at_release = probe->other_closed == 1;
    probe->current_at_release = rdb_sqlite_file_session_current(probe->session);
    probe->close_status = rdb_sqlite_file_session_close(probe->session);
    pool_destroy(probe->pool);
    probe->pool = NULL;
    probe->attachment_released++;
}
static void runtime_database_other_close(void* data) {
    ((RuntimeDatabaseTeardownProbe*)data)->other_closed++;
}
static Item runtime_database_task(Activation* self, Item arg) {
    RuntimeDatabaseTeardownProbe* probe = (RuntimeDatabaseTeardownProbe*)(uintptr_t)arg.item;
    static const LambdaOffloadOps ops = {runtime_database_worker, NULL, runtime_database_clone_release};
    if (!lambda_task_attachment_install(entry_task(self), &runtime_database_attachment_key,
            probe, runtime_database_release)) return ItemError;
    return lambda_task_offload(&ops, probe, 0);
}

// S12.4.2–S12.4.3: the SQLite owner closes after its worker is joined.
TEST(LambdaDocumentTasks, RuntimeTeardownKeepsDatabaseUntilTaskCleanup) {
    for (int reset = 0; reset < 2; reset++) {
        const char* path = reset ? "temp/runtime_db_heap_reset.db" : "temp/runtime_db_cleanup.db";
        remove(path);
        Runtime runtime = {};
        runtime_init(&runtime);
        ASSERT_TRUE(eval_context_transfer_thread(runtime_get_eval_context(&runtime)));
        ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "null;", 5,
            "runtime-db-owner.js", NULL)));
        EvalContext* owner = runtime_get_eval_context(&runtime);
        RuntimeDatabaseTeardownProbe probe = {};
        probe.pool = pool_create();
        ASSERT_NE(probe.pool, nullptr);
        RdbSqliteTxn* seed = rdb_sqlite_tx_begin(probe.pool, path);
        ASSERT_NE(seed, nullptr);
        const char* sql = "CREATE TABLE sample(value INTEGER)";
        ASSERT_EQ(rdb_sqlite_tx_execute(seed, sql, strlen(sql), NULL, 0, NULL, NULL), RDB_OK);
        ASSERT_EQ(rdb_sqlite_tx_finish(seed, false), RDB_OK);
        probe.session = rdb_sqlite_file_session_open(probe.pool, path);
        ASSERT_NE(probe.session, nullptr);
        probe.clone = rdb_sqlite_file_session_clone(probe.pool, probe.session, 1024 * 1024);
        ASSERT_NE(probe.clone, nullptr);
        Pool* orphan_pool = pool_create();
        ASSERT_NE(orphan_pool, nullptr);
        RdbSqliteFileSession* orphan = rdb_sqlite_file_session_open(orphan_pool, path);
        ASSERT_NE(orphan, nullptr);
        RuntimeResourceTable* table = runtime_resource_table_context(owner);
        ASSERT_NE(table, nullptr);
        static const RuntimeResourceDescriptor other = {
            RUNTIME_RESOURCE_TIMER, RUNTIME_RESOURCE_GROUP_NONE, "teardown-probe"
        };
        ASSERT_NE(runtime_resource_table_add_native_owned(table, NULL, &other,
            runtime_database_other_close, &probe), 0u);
        LambdaScheduler* scheduler = lambda_scheduler_create(3);
        ASSERT_NE(scheduler, nullptr);
        runtime_set_scheduler(&runtime, scheduler);
        ASSERT_NE(lambda_task_create(scheduler, runtime_database_task, frame_arg(&probe)), nullptr);
        ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
        for (int i = 0; i < 1000 && !__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE); i++) uv_sleep(1);
        ASSERT_EQ(__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE), 1);
        if (reset) runtime_reset_heap(&runtime);
        else runtime_cleanup(&runtime);
        EXPECT_EQ(probe.attachment_released, 1);
        EXPECT_EQ(probe.clone_released, 1);
        EXPECT_TRUE(probe.close_refused_while_cloned);
        EXPECT_TRUE(probe.worker_joined_at_release);
        EXPECT_TRUE(probe.other_closed_at_release);
        EXPECT_TRUE(probe.current_at_release);
        EXPECT_EQ(probe.close_status, RDB_OK);
        // Connections whose owners failed to close still reach the registry backstop.
        EXPECT_FALSE(rdb_sqlite_file_session_current(orphan));
        EXPECT_EQ(rdb_sqlite_file_session_close(orphan), RDB_OK);
        pool_destroy(orphan_pool);
        if (reset) runtime_cleanup(&runtime);
        remove(path);
    }
}
#endif

struct DocumentJoinProbe { int worker_started; int worker_finished; int task_finished; };
static DocumentJoinProbe* document_join_probe;
static void* document_join_owner;

static void* document_join_current() { return document_join_owner; }
static Function* document_test_procedure(EvalContext* owner, fn_ptr entry) {
    Function* fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    if (!fn) return NULL;
    function_init_abi(fn, LMD_TYPE_FUNC, FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE);
    fn->ptr = entry;
    fn->runtime_context = (Context*)owner;
    fn->requires_runtime_context = true;
    fn->mir_public_return_shape = LAMBDA_MIR_PUBLIC_RETURN_ITEM;
    return fn;
}

static Item document_lifetime_guard_noop(Context*) { return ItemNull; }

TEST_F(LambdaConcurrencyRuntime, DocumentLifetimeGuardClosesAdmissionAcrossSchedulers) {
    int document = 0;
    document_join_owner = &document;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(2);
    Function* fn = document_test_procedure(&eval, (fn_ptr)document_lifetime_guard_noop);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    LambdaScheduler* second = lambda_scheduler_create(3);
    ASSERT_NE(second, nullptr);
    memtrack_fault_inject(0);
    LambdaDocumentLifetimeGuard* failed = lambda_document_lifetime_guard_begin(&document);
    memtrack_fault_clear();
    EXPECT_EQ(failed, nullptr);
    LambdaDocumentLifetimeGuard* guard = lambda_document_lifetime_guard_begin(&document);
    ASSERT_NE(guard, nullptr);
    EXPECT_EQ(lambda_document_lifetime_guard_begin(&document), nullptr);
    struct GuardThreadProbe { LambdaDocumentLifetimeGuard* guard; bool current; bool ended; } thread_probe = {
        guard, true, true
    };
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, [](void* data) {
        GuardThreadProbe* probe = (GuardThreadProbe*)data;
        probe->current = lambda_document_lifetime_guard_is_current(probe->guard);
        probe->ended = lambda_document_lifetime_guard_end(probe->guard);
    }, &thread_probe), 0);
    ASSERT_EQ(uv_thread_join(&thread), 0);
    EXPECT_FALSE(thread_probe.current);
    EXPECT_FALSE(thread_probe.ended);
    EXPECT_TRUE(lambda_document_lifetime_guard_is_current(guard));
    EXPECT_TRUE(item_is_error(lambda_task_start_document(procedure.get(), NULL)));
    eval.scheduler = second;
    EXPECT_TRUE(item_is_error(lambda_task_start_document(procedure.get(), NULL)));
    EXPECT_TRUE(lambda_document_lifetime_guard_end(guard));
    Rooted<Item> admitted(roots, lambda_task_start_document(procedure.get(), NULL));
    EXPECT_TRUE(lambda_task_handle_is(admitted.get()));
    EXPECT_EQ(lambda_document_tasks_release(&document), 1);
    eval.scheduler = scheduler;
    lambda_scheduler_destroy(second);
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
struct RuntimeDocumentJoinProbe {
    LambdaTask* parent;
    LambdaTask* child;
    RdbSqliteFileSession* session;
    int* resource_closed;
    Runtime* runtime;
    LambdaScheduler* scheduler;
    bool reenter;
    bool reset;
    bool reentrant_preserved;
    bool cancelled;
    bool session_live;
    bool resource_live;
    bool state_live;
    bool child_finished;
    bool parent_finished;
};
struct RuntimeDocumentJoinResource {
    RdbSqliteFileSession* session;
    int closed;
    int close_status;
};
struct RuntimeDocumentJoinReentry {
    Runtime* runtime;
    LambdaScheduler* scheduler;
    RuntimeDocumentJoinResource* resource;
    void* retiring_document;
    bool reset;
    bool attempted;
    bool preserved;
    bool external_rejected;
    bool guard_rejected;
};
static RuntimeDocumentJoinProbe* runtime_document_join_probes;
static const char runtime_document_join_template[] = "document-join-template";
static const char runtime_document_join_state[] = "document-join-state";

static RuntimeDocumentJoinProbe* runtime_document_join_current(bool child) {
    LambdaTask* task = lambda_task_running();
    for (int i = 0; i < 2; i++) {
        RuntimeDocumentJoinProbe* probe = &runtime_document_join_probes[i];
        if ((child ? probe->child : probe->parent) == task) return probe;
    }
    return NULL;
}
static Item runtime_document_join_child(Context*) {
    RuntimeDocumentJoinProbe* probe = runtime_document_join_current(true);
    if (!probe) return ItemError;
    Item result = pn_sleep((Item){.item = i2it(5)});
    probe->child_finished = !item_is_error(result) &&
        rdb_sqlite_file_session_current(probe->session) && !*probe->resource_closed;
    return result;
}
static Item runtime_document_join_parent(Context* owner) {
    RuntimeDocumentJoinProbe* probe = runtime_document_join_current(false);
    if (!probe) return ItemError;
    if (probe->reenter) {
        if (probe->reset) runtime_reset_heap(probe->runtime);
        else runtime_cleanup(probe->runtime);
        probe->reentrant_preserved = !*probe->resource_closed &&
            rdb_sqlite_file_session_current(probe->session) &&
            runtime_scheduler(probe->runtime) == probe->scheduler;
    }
    Item result = pn_receive();
    probe->cancelled = item_is_error(result);
    probe->session_live = rdb_sqlite_file_session_current(probe->session);
    probe->resource_live = !*probe->resource_closed;
    probe->state_live = render_map_get_doc_root().item == i2it(61) &&
        tmpl_state_get((Item){.item = i2it(31)}, runtime_document_join_template,
            runtime_document_join_state).item == i2it(77);
    LambdaTask* task = lambda_task_running();
    lambda_task_set_cleanup_masked(task, true);
    RootFrame roots(3);
    Rooted<Item> held_result(roots, result);
    Function* fn = document_test_procedure((EvalContext*)owner, (fn_ptr)runtime_document_join_child);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    document_join_owner = probe;
    Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
    probe->child = lambda_task_from_handle(handle.get());
    Item joined = pn_wait1(handle.get());
    probe->parent_finished = !item_is_error(joined);
    lambda_task_set_cleanup_masked(task, false);
    return held_result.get();
}
static void runtime_document_resource_closed(void* data) {
    RuntimeDocumentJoinResource* resource = (RuntimeDocumentJoinResource*)data;
    resource->closed++;
    resource->close_status = rdb_sqlite_file_session_close(resource->session);
}
static void runtime_document_join_reenter(uv_timer_t* timer) {
    RuntimeDocumentJoinReentry* probe = (RuntimeDocumentJoinReentry*)timer->data;
    probe->attempted = true;
    if (probe->reset) runtime_reset_heap(probe->runtime);
    else runtime_cleanup(probe->runtime);
    probe->preserved = !probe->resource->closed &&
        rdb_sqlite_file_session_current(probe->resource->session) &&
        runtime_scheduler(probe->runtime) == probe->scheduler;
    RootFrame roots(1);
    Function* fn = document_test_procedure(runtime_get_eval_context(probe->runtime),
        (fn_ptr)runtime_document_join_child);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    document_join_owner = probe->retiring_document ? probe->retiring_document : probe;
    if (!probe->retiring_document) {
        bool guard_started = lambda_document_retirement_guard_begin(probe->scheduler, probe);
        probe->guard_rejected = !guard_started;
        if (guard_started) lambda_document_retirement_guard_end(probe->scheduler, probe);
    } else {
        probe->guard_rejected = true;
    }
    Item started = lambda_task_start_document(procedure.get(), NULL);
    probe->external_rejected = item_is_error(started);
    if (lambda_task_handle_is(started)) lambda_task_cancel(lambda_task_from_handle(started));
    uv_close((uv_handle_t*)timer, NULL);
}

// S13.3.1v2: document and runtime joins keep the SQLite owner live through cleanup.
TEST(LambdaDocumentTasks, RuntimeShutdownJoinsDocumentCleanupBeforeResourceClose) {
    for (int mode = 0; mode < 3; mode++) {
        bool reset = mode == 1;
        bool guarded = mode == 2;
        const char* path = guarded ? "temp/document_join_guarded.db" :
            (reset ? "temp/document_join_reset.db" : "temp/document_join_cleanup.db");
        remove(path);
        Runtime runtime = {};
        runtime_init(&runtime);
        EvalContext* owner = runtime_get_eval_context(&runtime);
        ASSERT_TRUE(eval_context_transfer_thread(owner));
        ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "null;", 5,
            "document-runtime-join.js", NULL)));
        render_map_init();
        render_map_set_doc_root((Item){.item = i2it(61)});
        tmpl_state_set((Item){.item = i2it(31)}, runtime_document_join_template,
            runtime_document_join_state, (Item){.item = i2it(77)});
        Pool* pool = pool_create();
        ASSERT_NE(pool, nullptr);
        RdbSqliteTxn* seed = rdb_sqlite_tx_begin(pool, path);
        ASSERT_NE(seed, nullptr);
        const char* sql = "CREATE TABLE sample(value INTEGER)";
        ASSERT_EQ(rdb_sqlite_tx_execute(seed, sql, strlen(sql), NULL, 0, NULL, NULL), RDB_OK);
        ASSERT_EQ(rdb_sqlite_tx_finish(seed, false), RDB_OK);
        RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
        ASSERT_NE(session, nullptr);
        RuntimeDocumentJoinResource resource = {session, 0, RDB_ERROR};
        RuntimeResourceTable* table = runtime_resource_table_context(owner);
        ASSERT_NE(table, nullptr);
        static const RuntimeResourceDescriptor marker = {
            RUNTIME_RESOURCE_TIMER, RUNTIME_RESOURCE_GROUP_NONE, "document-join-probe"
        };
        ASSERT_NE(runtime_resource_table_add_native_owned(table, NULL, &marker,
            runtime_document_resource_closed, &resource), 0u);
        LambdaScheduler* scheduler = lambda_scheduler_create(4);
        ASSERT_NE(scheduler, nullptr);
        runtime_set_scheduler(&runtime, scheduler);
        RuntimeDocumentJoinProbe probes[2] = {};
        runtime_document_join_probes = probes;
        lambda_set_document_probe(document_join_current);
        {
            RootFrame roots(3);
            Function* fn = document_test_procedure(owner, (fn_ptr)runtime_document_join_parent);
            ASSERT_NE(fn, nullptr);
            Rooted<Item> procedure(roots, (Item){.function = fn});
            Rooted<Item> first(roots, ItemNull), second(roots, ItemNull);
            for (int i = 0; i < 2; i++) {
                probes[i].session = session;
                probes[i].resource_closed = &resource.closed;
                probes[i].runtime = &runtime;
                probes[i].scheduler = scheduler;
                probes[i].reenter = i == 0;
                probes[i].reset = reset != 0;
                document_join_owner = &probes[i];
                Item handle = lambda_task_start_document(procedure.get(), NULL);
                if (i == 0) first.set(handle); else second.set(handle);
                probes[i].parent = lambda_task_from_handle(handle);
                ASSERT_NE(probes[i].parent, nullptr);
            }
            ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 2);
            EXPECT_TRUE(probes[0].reentrant_preserved) << reset;
        }
        if (guarded) {
            ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, &probes[0]));
            ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, &probes[1]));
            EXPECT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_RETRY);
            EXPECT_FALSE(runtime.cleanup_incomplete);
            EXPECT_EQ(runtime_scheduler(&runtime), scheduler);
            EXPECT_EQ(resource.closed, 0);
            EXPECT_TRUE(rdb_sqlite_file_session_current(session));
            runtime_reset_heap(&runtime);
            EXPECT_EQ(runtime_scheduler(&runtime), scheduler);
            EXPECT_EQ(resource.closed, 0);
        }
        {
            RuntimeExecutionScope active(owner);
            if (guarded) EXPECT_FALSE(lambda_document_tasks_join_guarded(scheduler, &probes[0]));
            else if (reset) runtime_reset_heap(&runtime);
            else EXPECT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_RETRY);
            EXPECT_EQ(runtime_scheduler(&runtime), scheduler) << reset;
            EXPECT_EQ(resource.closed, 0) << reset;
            EXPECT_TRUE(rdb_sqlite_file_session_current(session)) << reset;
        }
        RuntimeDocumentJoinReentry reentry = {&runtime, scheduler, &resource,
            guarded ? &probes[0] : nullptr, reset, false, false, false, false};
        uv_timer_t timer = {};
        ASSERT_EQ(uv_timer_init(lambda_uv_loop(), &timer), 0);
        timer.data = &reentry;
        ASSERT_EQ(uv_timer_start(&timer, runtime_document_join_reenter, 0, 0), 0);
        if (guarded) {
            EXPECT_TRUE(lambda_document_tasks_join_guarded(scheduler, &probes[0]));
            EXPECT_EQ(resource.closed, 0);
            EXPECT_TRUE(rdb_sqlite_file_session_current(session));
            EXPECT_TRUE(lambda_document_tasks_join_guarded(scheduler, &probes[1]));
            EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &probes[0]));
            EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &probes[1]));
            EXPECT_EQ(resource.closed, 0);
            EXPECT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_COMPLETE);
        } else if (reset) runtime_reset_heap(&runtime);
        else EXPECT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_COMPLETE);
        EXPECT_TRUE(reentry.attempted) << reset;
        EXPECT_TRUE(reentry.preserved) << reset;
        EXPECT_TRUE(reentry.external_rejected) << reset;
        EXPECT_TRUE(reentry.guard_rejected) << reset;
        for (int i = 0; i < 2; i++) {
            EXPECT_TRUE(probes[i].cancelled) << reset << ":" << i;
            EXPECT_TRUE(probes[i].session_live) << reset << ":" << i;
            EXPECT_TRUE(probes[i].resource_live) << reset << ":" << i;
            EXPECT_TRUE(probes[i].state_live) << reset << ":" << i;
            EXPECT_TRUE(probes[i].child_finished) << reset << ":" << i;
            EXPECT_TRUE(probes[i].parent_finished) << reset << ":" << i;
        }
        EXPECT_EQ(resource.closed, 1);
        EXPECT_EQ(resource.close_status, RDB_OK);
        pool_destroy(pool);
        if (reset) runtime_cleanup(&runtime);
        lambda_set_document_probe(NULL);
        document_join_owner = NULL;
        runtime_document_join_probes = NULL;
        remove(path);
    }
}
#endif
static int document_test_tick_start(uv_timer_t* timer) {
    int status = uv_timer_init(lambda_uv_loop(), timer);
    return status ? status : uv_timer_start(timer, [](uv_timer_t*) {}, 1, 10);
}
static void document_test_tick_stop(uv_timer_t* timer) {
    uv_timer_stop(timer);
    uv_close((uv_handle_t*)timer, NULL);
    uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);
}
static void document_join_worker(LambdaOffload* job, void* data) {
    DocumentJoinProbe* probe = (DocumentJoinProbe*)data;
    __atomic_store_n(&probe->worker_started, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(lambda_offload_cancel_flag(job), __ATOMIC_ACQUIRE)) uv_sleep(1);
    // A non-cooperative native operation may finish after the former five-second cutoff.
    uv_sleep(5300);
    __atomic_store_n(&probe->worker_finished, 1, __ATOMIC_RELEASE);
}
static Item document_join_procedure(Context*) {
    static const LambdaOffloadOps ops = {document_join_worker, NULL, NULL};
    Item result = lambda_task_offload(&ops, document_join_probe, 0);
    document_join_probe->task_finished = 1;
    return result;
}

// S13.3.1v2: cancellation must join a document's native work before retirement.
TEST_F(LambdaConcurrencyRuntime, DocumentReleaseJoinsSlowCancelledWorker) {
    DocumentJoinProbe probe = {};
    document_join_probe = &probe;
    document_join_owner = &probe;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(2);
    Function* fn = document_test_procedure(&eval, (fn_ptr)document_join_procedure);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(handle.get()));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    for (int i = 0; i < 1000 && !__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE); i++) uv_sleep(1);
    ASSERT_EQ(__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE), 1);
    uv_timer_t timer = {};
    ASSERT_EQ(document_test_tick_start(&timer), 0);
    EXPECT_EQ(lambda_document_tasks_release(&probe), 1);
    EXPECT_EQ(__atomic_load_n(&probe.worker_finished, __ATOMIC_ACQUIRE), 1);
    EXPECT_EQ(probe.task_finished, 1);
    EXPECT_EQ(lambda_task_state(lambda_task_from_handle(handle.get())), LAMBDA_TASK_DONE);
    // Keep the probe live even when the regression leaves the worker behind.
    while (lambda_task_state(lambda_task_from_handle(handle.get())) != LAMBDA_TASK_DONE) {
        uv_run(lambda_uv_loop(), UV_RUN_ONCE);
        lambda_scheduler_run_ready(scheduler);
    }
    document_test_tick_stop(&timer);
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    document_join_probe = NULL;
}

struct DocumentCleanupProbe { LambdaTask* child; int child_started; int child_finished; int parent_finished; };
static DocumentCleanupProbe* document_cleanup_probe;

static Item document_cleanup_child(Context*) {
    document_cleanup_probe->child_started = 1;
    Item result = pn_sleep((Item){.item = i2it(5300)});
    if (!item_is_error(result)) document_cleanup_probe->child_finished = 1;
    return result;
}
static Item document_cleanup_parent(Context* owner) {
    LambdaTask* task = lambda_task_running();
    (void)lambda_task_suspend(task);
    // A child created by cancellation-masked cleanup must be joined, not cancelled.
    lambda_task_set_cleanup_masked(task, true);
    RootFrame roots(2);
    Function* fn = document_test_procedure((EvalContext*)owner, (fn_ptr)document_cleanup_child);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> handle(roots, lambda_task_start_function_scoped(procedure.get(), NULL, true));
    document_cleanup_probe->child = lambda_task_from_handle(handle.get());
    Item result = pn_wait1(handle.get());
    if (!item_is_error(result)) document_cleanup_probe->parent_finished = 1;
    lambda_task_set_cleanup_masked(task, false);
    return result;
}

// S13.3.1v2 and S13.3.2: cancellation-masked cleanup can join new descendants.
TEST_F(LambdaConcurrencyRuntime, DocumentReleaseJoinsChildStartedDuringCleanup) {
    DocumentCleanupProbe probe = {};
    document_cleanup_probe = &probe;
    document_join_owner = &probe;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(2);
    Function* fn = document_test_procedure(&eval, (fn_ptr)document_cleanup_parent);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(handle.get()));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    uv_timer_t timer = {};
    ASSERT_EQ(document_test_tick_start(&timer), 0);
    EXPECT_EQ(lambda_document_tasks_release(&probe), 1);
    EXPECT_EQ(probe.child_started, 1);
    EXPECT_EQ(probe.child_finished, 1);
    EXPECT_EQ(probe.parent_finished, 1);
    EXPECT_EQ(lambda_task_state(lambda_task_from_handle(handle.get())), LAMBDA_TASK_DONE);
    // Keep the probe live until a regressing scheduler finally drains its tasks.
    while (lambda_task_state(lambda_task_from_handle(handle.get())) != LAMBDA_TASK_DONE) {
        uv_run(lambda_uv_loop(), UV_RUN_ONCE);
        lambda_scheduler_run_ready(scheduler);
    }
    document_test_tick_stop(&timer);
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    document_cleanup_probe = NULL;
}

struct DocumentRetireProbe {
    LambdaScheduler* scheduler;
    void* document;
    LambdaTask* child;
    LambdaTask* grandchild;
    LambdaTask* explicit_grandchild;
    int first_cancel;
    int second_cancel;
    int child_finished;
    int foreign_finished;
    bool guard_started;
    bool guard_ended_early;
    bool detached_during_parent;
    bool ordinary_child_cancelled;
    bool grandchild_cancelled;
    bool explicit_grandchild_cancelled;
    bool join_refused_reentrant;
};
static DocumentRetireProbe* document_retire_probe;

static Item document_retire_child(Context*) {
    document_retire_probe->child_finished++;
    return ItemNull;
}
static Item document_retire_parent(Context* owner) {
    DocumentRetireProbe* probe = document_retire_probe;
    probe->join_refused_reentrant = !lambda_scheduler_join_document_tasks(probe->scheduler);
    probe->guard_started = lambda_document_retirement_guard_begin(probe->scheduler, probe->document);
    probe->guard_ended_early = lambda_document_retirement_guard_end(probe->scheduler, probe->document);
    probe->first_cancel = lambda_document_tasks_cancel(probe->scheduler, probe->document);
    probe->second_cancel = lambda_document_tasks_cancel(probe->scheduler, probe->document);
    probe->detached_during_parent = lambda_document_tasks_detach_joined(probe->scheduler, probe->document);
    LambdaTask* current = lambda_task_running();
    RootFrame roots(3);
    Function* fn = document_test_procedure((EvalContext*)owner, (fn_ptr)document_retire_child);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> ordinary(roots, lambda_task_start_function_scoped(procedure.get(), NULL, true));
    probe->ordinary_child_cancelled = lambda_task_cancel_requested(lambda_task_from_handle(ordinary.get()));
    lambda_task_set_cleanup_masked(current, true);
    document_join_owner = probe->document;
    Rooted<Item> child(roots, lambda_task_start_document(procedure.get(), NULL));
    probe->child = lambda_task_from_handle(child.get());
    lambda_task_set_cleanup_masked(current, false);
    return ItemNull;
}
static Item document_retire_foreign(Context*) {
    Item result = lambda_task_suspend(lambda_task_running());
    document_retire_probe->foreign_finished++;
    return result;
}

struct GuardedDocumentJoinReentry {
    LambdaScheduler* scheduler;
    void* document;
    bool called;
    bool refused;
    bool still_owned;
};

// S13.3.1v2/S13.3.2: refusal keeps the document owner until cleanup finishes.
TEST_F(LambdaConcurrencyRuntime, GuardedDocumentJoinRetriesAfterActiveAndReentrantCalls) {
    DocumentCleanupProbe cleanup_probe = {};
    DocumentRetireProbe foreign_probe = {};
    int foreign_document = 0;
    document_cleanup_probe = &cleanup_probe;
    document_retire_probe = &foreign_probe;
    document_join_owner = &cleanup_probe;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(4);
    Function* parent_fn = document_test_procedure(&eval, (fn_ptr)document_cleanup_parent);
    ASSERT_NE(parent_fn, nullptr);
    Rooted<Item> parent_proc(roots, (Item){.function = parent_fn});
    Function* foreign_fn = document_test_procedure(&eval, (fn_ptr)document_retire_foreign);
    ASSERT_NE(foreign_fn, nullptr);
    Rooted<Item> foreign_proc(roots, (Item){.function = foreign_fn});
    Rooted<Item> parent(roots, lambda_task_start_document(parent_proc.get(), NULL));
    document_join_owner = &foreign_document;
    Rooted<Item> foreign(roots, lambda_task_start_document(foreign_proc.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(parent.get()));
    ASSERT_TRUE(lambda_task_handle_is(foreign.get()));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 2);
    EXPECT_FALSE(lambda_document_tasks_join_guarded(scheduler, &cleanup_probe));
    EXPECT_FALSE(lambda_task_cancel_requested(lambda_task_from_handle(parent.get())));
    ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, &cleanup_probe));
    LambdaDocumentLifetimeGuard* lifetime = lambda_document_lifetime_guard_begin(&cleanup_probe);
    ASSERT_NE(lifetime, nullptr);
    {
        RuntimeExecutionScope active(&eval);
        EXPECT_EQ(lambda_document_tasks_release(&cleanup_probe), -1);
        EXPECT_FALSE(lambda_document_tasks_join_guarded(scheduler, &cleanup_probe));
        EXPECT_TRUE(lambda_task_cancel_requested(lambda_task_from_handle(parent.get())));
        EXPECT_FALSE(lambda_document_tasks_detach_joined(scheduler, &cleanup_probe));
        document_join_owner = &cleanup_probe;
        EXPECT_TRUE(item_is_error(lambda_task_start_document(parent_proc.get(), NULL)));
    }
    GuardedDocumentJoinReentry reentry = {scheduler, &cleanup_probe, false, false, false};
    uv_timer_t timer = {};
    ASSERT_EQ(uv_timer_init(lambda_uv_loop(), &timer), 0);
    timer.data = &reentry;
    ASSERT_EQ(uv_timer_start(&timer, [](uv_timer_t* handle) {
        GuardedDocumentJoinReentry* probe = (GuardedDocumentJoinReentry*)handle->data;
        probe->called = true;
        probe->refused = !lambda_document_tasks_join_guarded(probe->scheduler, probe->document);
        probe->still_owned = !lambda_document_tasks_detach_joined(probe->scheduler, probe->document);
        uv_close((uv_handle_t*)handle, NULL);
    }, 0, 0), 0);
    EXPECT_TRUE(lambda_document_tasks_join_guarded(scheduler, &cleanup_probe));
    EXPECT_TRUE(reentry.called);
    EXPECT_TRUE(reentry.refused);
    EXPECT_TRUE(reentry.still_owned);
    EXPECT_EQ(cleanup_probe.child_finished, 1);
    EXPECT_EQ(cleanup_probe.parent_finished, 1);
    EXPECT_FALSE(lambda_task_cancel_requested(lambda_task_from_handle(foreign.get())));
    EXPECT_TRUE(item_is_error(lambda_task_start_document(parent_proc.get(), NULL)));
    EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &cleanup_probe));
    EXPECT_TRUE(lambda_document_lifetime_guard_end(lifetime));
    EXPECT_EQ(lambda_document_tasks_cancel(scheduler, &foreign_document), 1);
    EXPECT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    EXPECT_EQ(foreign_probe.foreign_finished, 1);
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, &foreign_document));
    uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    document_cleanup_probe = NULL;
    document_retire_probe = NULL;
}

struct GuardedDocumentStallProbe { bool parked; bool finished; };
static GuardedDocumentStallProbe* guarded_document_stall_probe;
static GuardedDocumentStallProbe* guarded_document_stall_other_probe;
static LambdaTask* guarded_document_stall_other_task;
static RdbSqliteFileSession* guarded_document_stall_session;
static bool* guarded_document_stall_session_live;

static Item guarded_document_stall_task(Context*) {
    LambdaTask* task = lambda_task_running();
    GuardedDocumentStallProbe* probe = task == guarded_document_stall_other_task
        ? guarded_document_stall_other_probe : guarded_document_stall_probe;
    (void)lambda_task_suspend(task);
    lambda_task_set_cleanup_masked(task, true);
    probe->parked = true;
    if (probe == guarded_document_stall_probe && guarded_document_stall_session && guarded_document_stall_session_live)
        *guarded_document_stall_session_live &= rdb_sqlite_file_session_current(guarded_document_stall_session);
    Item result = lambda_task_suspend(task);
    if (probe == guarded_document_stall_probe && guarded_document_stall_session && guarded_document_stall_session_live)
        *guarded_document_stall_session_live &= rdb_sqlite_file_session_current(guarded_document_stall_session);
    probe->finished = true;
    lambda_task_set_cleanup_masked(task, false);
    return result;
}

TEST_F(LambdaConcurrencyRuntime, SchedulerShutdownProgressDoesNotPollUnrelatedTimer) {
    GuardedDocumentStallProbe probe = {};
    guarded_document_stall_probe = &probe;
    document_join_owner = &probe;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(2);
    Function* fn = document_test_procedure(&eval, (fn_ptr)guarded_document_stall_task);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
    LambdaTask* task = lambda_task_from_handle(handle.get());
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    bool timer_fired = false;
    uv_timer_t timer = {};
    ASSERT_EQ(uv_timer_init(lambda_uv_loop(), &timer), 0);
    timer.data = &timer_fired;
    ASSERT_EQ(uv_timer_start(&timer, [](uv_timer_t* handle) {
        *(bool*)handle->data = true;
        uv_close((uv_handle_t*)handle, NULL);
    }, 100, 0), 0);
    EXPECT_EQ(lambda_scheduler_document_tasks_progress(scheduler), LAMBDA_DOCUMENT_JOIN_ADVANCED);
    EXPECT_TRUE(probe.parked);
    EXPECT_FALSE(timer_fired);
    EXPECT_EQ(lambda_scheduler_document_tasks_progress(scheduler), LAMBDA_DOCUMENT_JOIN_WAITING);
    EXPECT_FALSE(timer_fired);
    EXPECT_TRUE(item_is_error(lambda_task_start_document(procedure.get(), NULL)));
    lambda_task_resume(task);
    EXPECT_EQ(lambda_scheduler_document_tasks_progress(scheduler), LAMBDA_DOCUMENT_JOIN_DONE);
    EXPECT_TRUE(probe.finished);
    EXPECT_FALSE(timer_fired);
    uv_timer_stop(&timer);
    uv_close((uv_handle_t*)&timer, NULL);
    lambda_uv_run_loop(lambda_uv_loop(), UV_RUN_NOWAIT);
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    guarded_document_stall_probe = NULL;
}

struct RuntimeCleanupExportTaskProbe {
    Runtime* runtime;
    bool began;
    bool deferred;
};

static RuntimeCleanupExportTaskProbe* runtime_cleanup_export_task_probe;

static Item runtime_cleanup_export_task(Context*) {
    LambdaTask* task = lambda_task_running();
    (void)lambda_task_suspend(task);
    lambda_task_set_cleanup_masked(task, true);
    RuntimeCleanupExportTaskProbe* probe = runtime_cleanup_export_task_probe;
    RenderExportSession session = {};
    probe->began = render_export_session_begin_source(&session, "<html></html>", 80, 60, false);
    if (probe->began) {
        // This is the same evaluator a synchronous package layout can borrow.
        session.document->loader_runtime = lam::up(probe->runtime);
        probe->deferred = !render_export_session_finish(&session);
    }
    lambda_task_set_cleanup_masked(task, false);
    return ItemNull;
}

TEST(LambdaDocumentTasks, MaskedCleanupExportRetainsRuntimeUntilRetry) {
    ASSERT_EXIT({
        for (int reset = 0; reset < 2; reset++) {
            Runtime* runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_EVAL);
            if (!runtime) _Exit(2);
            runtime_init(runtime);
            EvalContext* owner = runtime_get_eval_context(runtime);
            if (!owner || !eval_context_transfer_thread(owner) ||
                item_is_error(js_interp_execute_source(runtime, "null;", 5,
                    "runtime-cleanup-export.js", NULL))) _Exit(3);
            LambdaScheduler* scheduler = runtime_scheduler(runtime);
            if (!scheduler) {
                scheduler = lambda_scheduler_create(3);
                runtime_set_scheduler(runtime, scheduler);
            }
            if (!scheduler) _Exit(4);
            RuntimeCleanupExportTaskProbe probe = {};
            probe.runtime = runtime;
            runtime_cleanup_export_task_probe = &probe;
            document_join_owner = &probe;
            lambda_set_document_probe(document_join_current);
            {
                RootFrame roots(2);
                Function* fn = document_test_procedure(owner, (fn_ptr)runtime_cleanup_export_task);
                if (!fn) _Exit(5);
                Rooted<Item> procedure(roots, (Item){.function = fn});
                Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
                if (!lambda_task_handle_is(handle.get()) || lambda_scheduler_run_ready(scheduler) != 1) _Exit(6);
            }
            Heap* heap = runtime_heap(runtime);
            if (reset) runtime_reset_heap(runtime);
            else if (runtime_try_cleanup(runtime) != RUNTIME_CLEANUP_RETRY) _Exit(7);
            if (!probe.began || !probe.deferred || runtime->native_borrows != 1 ||
                runtime->cleanup_in_progress || runtime->cleanup_incomplete || runtime_heap(runtime) != heap ||
                render_export_pending_count() != 1) _Exit(8);
            if (!render_export_retire_pending() || render_export_pending_count() || runtime->native_borrows ||
                runtime_try_cleanup(runtime) != RUNTIME_CLEANUP_COMPLETE) _Exit(9);
            mem_free(runtime);
            runtime_cleanup_export_task_probe = NULL;
            document_join_owner = NULL;
            lambda_set_document_probe(NULL);
        }
        _Exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(LambdaConcurrencyRuntime, GuardedDocumentJoinRetainsStalledCleanup) {
    for (int guarded = 0; guarded < 2; guarded++) {
        GuardedDocumentStallProbe probe = {};
        guarded_document_stall_probe = &probe;
        document_join_owner = &probe;
        lambda_set_document_probe(document_join_current);
        RootFrame roots(2);
        Function* fn = document_test_procedure(&eval, (fn_ptr)guarded_document_stall_task);
        ASSERT_NE(fn, nullptr);
        Rooted<Item> procedure(roots, (Item){.function = fn});
        Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
        LambdaTask* task = lambda_task_from_handle(handle.get());
        ASSERT_NE(task, nullptr);
        ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
        if (guarded) ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, &probe));
        // With no referenced loop handles, masked cleanup has no source of progress.
        ASSERT_EQ(uv_loop_alive(lambda_uv_loop()), 0);
        if (guarded) EXPECT_FALSE(lambda_document_tasks_join_guarded(scheduler, &probe));
        else ASSERT_EQ(lambda_document_tasks_release(&probe), -1);
        EXPECT_TRUE(probe.parked);
        EXPECT_FALSE(probe.finished);
        EXPECT_FALSE(lambda_document_tasks_detach_joined(scheduler, &probe));
        EXPECT_TRUE(item_is_error(lambda_task_start_document(procedure.get(), NULL)));
        lambda_task_resume(task);
        if (guarded) EXPECT_TRUE(lambda_document_tasks_join_guarded(scheduler, &probe));
        else EXPECT_EQ(lambda_document_tasks_release(&probe), 1);
        EXPECT_TRUE(probe.finished);
        if (guarded) EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &probe));
        lambda_set_document_probe(NULL);
        document_join_owner = NULL;
        guarded_document_stall_probe = NULL;
    }
}

struct WindowRetirementResource : DomDocumentResourceData {
    UiContext* ui;
    Runtime* script;
    Runtime* loader;
    EvalContext* loader_owner;
    RdbSqliteFileSession* session;
    Pool* session_pool;
    bool* checked;
    void* other_document;
    bool* held_guard;
};

struct WindowRetirementReentry {
    UiContext* ui;
    DomDocument* document;
    bool called;
    bool remove_loader;
};

static void window_retirement_reenter(uv_timer_t* timer) {
    WindowRetirementReentry* probe = (WindowRetirementReentry*)timer->data;
    probe->called = true;
    // A second close must not free the owner while the first join is driving the loop.
    if (free_document_profiled(probe->document, nullptr) || probe->ui->destroy_document()) _Exit(13);
    if (dom_retain_owned_document(probe->document)) _Exit(30);
    Runtime* js_runtime = probe->document->js.runtime;
    if (js_runtime) {
        script_runner_cleanup_js_state(probe->document);
        if (probe->document->js.runtime != js_runtime) _Exit(29);
    }
    if (probe->remove_loader) probe->document->loader_runtime = nullptr;
    uv_close((uv_handle_t*)timer, NULL);
}

static void window_retirement_during_uv_run(uv_timer_t* timer) {
    WindowRetirementReentry* probe = (WindowRetirementReentry*)timer->data;
    probe->called = true;
    if (!context || !context->scheduler ||
        lambda_document_tasks_release(probe->document) != -1 ||
        lambda_scheduler_join_document_tasks(context->scheduler)) _Exit(39);
    if (probe->ui->destroy_document()) _Exit(35);
    uv_close((uv_handle_t*)timer, NULL);
}

static void window_retirement_resource_destroy(DomDocumentResourceData* data) {
    WindowRetirementResource* resource = (WindowRetirementResource*)data;
    EvalContext* previous = context;
    bool retained = runtime_try_cleanup(resource->script) == RUNTIME_CLEANUP_RETRY &&
        runtime_try_cleanup(resource->loader) == RUNTIME_CLEANUP_RETRY &&
        !resource->ui->destroy_document();
    bool rejected = false;
    if (eval_context_transfer_thread(resource->loader_owner)) {
        retained = retained && rdb_sqlite_file_session_current(resource->session);
        {
            RootFrame roots(1);
            Function* fn = document_test_procedure(resource->loader_owner, (fn_ptr)document_lifetime_guard_noop);
            Rooted<Item> procedure(roots, (Item){.function = fn});
            rejected = fn && item_is_error(lambda_task_start_document(procedure.get(), NULL));
        }
        int close_status = rdb_sqlite_file_session_close(resource->session);
        pool_destroy(resource->session_pool);
        if (resource->other_document) {
            *resource->held_guard = lambda_document_retirement_guard_begin(
                runtime_scheduler(resource->loader), resource->other_document);
        }
        bool restored = eval_context_transfer_thread(previous);
        retained = retained && close_status == RDB_OK && restored;
    } else retained = false;
    *resource->checked = retained && rejected;
    mem_free(resource);
}

enum WindowRetirementMode { WINDOW_RETIRE_DIRECT, WINDOW_RETIRE_EXPORT, WINDOW_RETIRE_HOST };

static void window_retirement_unrelated_timer(uv_timer_t* timer) {
    bool* fired = (bool*)timer->data;
    *fired = true;
    uv_close((uv_handle_t*)timer, NULL);
}

static void exercise_stalled_window_document(bool distinct_loader, bool retry_runtime_cleanup,
        bool change_owner, WindowRetirementMode mode = WINDOW_RETIRE_DIRECT) {
    bool export_handoff = mode == WINDOW_RETIRE_EXPORT;
    bool host_handoff = mode == WINDOW_RETIRE_HOST;
    UiContext ui_storage = {};
    UiContext* ui = &ui_storage;
    RenderExportSession export_session = {};
    if (export_handoff) {
        if (!distinct_loader || !render_export_session_begin_source(&export_session,
                "<html></html>", 80, 60, false)) _Exit(40);
        ui = export_session.ui_context;
        if (!ui || !ui->destroy_document()) _Exit(41);
        export_session.document = nullptr;
    } else if (host_handoff) {
        if (!distinct_loader || !radiant_window_host_begin(nullptr) ||
                ui_context_init(&ui_context, true, 1.0f) != 0) _Exit(43);
        ui = &ui_context;
    }
    Runtime* loader = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_EVAL);
    Runtime* script = distinct_loader
        ? (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_EVAL) : loader;
    if (!script || !loader) _Exit(2);
    runtime_init(loader);
    if (distinct_loader) runtime_init(script);
    EvalContext* script_owner = runtime_get_eval_context(script);
    EvalContext* loader_owner = runtime_get_eval_context(loader);
    if (distinct_loader && (!script_owner || !eval_context_transfer_thread(script_owner) ||
            item_is_error(js_interp_execute_source(script, "null;", 5,
                "stalled-window-script.js", NULL)))) _Exit(21);
    if (!script_owner || !loader_owner || !eval_context_transfer_thread(loader_owner) ||
        item_is_error(js_interp_execute_source(loader, "null;", 5,
            "stalled-window-document.js", NULL))) _Exit(3);
    LambdaScheduler* scheduler = runtime_scheduler(loader);
    if (!scheduler) {
        scheduler = lambda_scheduler_create(3);
        runtime_set_scheduler(loader, scheduler);
    }
    Pool* pool = pool_create();
    Input* input = pool ? Input::create(pool) : nullptr;
    DomDocument* document = input ? dom_document_create(input) : nullptr;
    if (!scheduler || !document) _Exit(4);
    if (host_handoff) radiant_window_host_loop_started();
    dom_document_finalize_loader_pool(document, pool);
    const char* path = distinct_loader ? "temp/window-retirement-loader.db" : "temp/window-retirement-single.db";
    remove(path);
    Pool* session_pool = pool_create();
    RdbSqliteTxn* seed = session_pool ? rdb_sqlite_tx_begin(session_pool, path) : nullptr;
    const char* sql = "CREATE TABLE sample(value INTEGER)";
    if (!seed || rdb_sqlite_tx_execute(seed, sql, strlen(sql), NULL, 0, NULL, NULL) != RDB_OK ||
        rdb_sqlite_tx_finish(seed, false) != RDB_OK) _Exit(19);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(session_pool, path);
    if (!session) _Exit(20);
    if (distinct_loader) {
        document->lambda_runtime = script;
        document->owns_script_runtime = true;
    } else document->js.runtime = loader;
    script->dom_doc = document;
    loader->dom_doc = document;
    ui->document = lam::up(document);
    if (export_handoff) export_session.document = lam::up(document);
    if (distinct_loader) {
        document->loader_runtime = lam::up(loader);
        ui->loader_runtime = lam::own(loader);
    }
    bool resource_checked = false;
    bool held_guard = false;
    int other_document = 0;
    WindowRetirementResource* resource = (WindowRetirementResource*)mem_calloc(
        1, sizeof(WindowRetirementResource), MEM_CAT_LAYOUT);
    if (!resource) _Exit(11);
    resource->ui = ui;
    resource->script = script;
    resource->loader = loader;
    resource->loader_owner = loader_owner;
    resource->session = session;
    resource->session_pool = session_pool;
    resource->checked = &resource_checked;
    resource->other_document = retry_runtime_cleanup ? &other_document : nullptr;
    resource->held_guard = &held_guard;
    if (!dom_document_add_resource(document, resource, window_retirement_resource_destroy)) _Exit(12);
    GuardedDocumentStallProbe probe = {};
    GuardedDocumentStallProbe other_probe = {};
    bool session_live = true;
    guarded_document_stall_probe = &probe;
    guarded_document_stall_session = session;
    guarded_document_stall_session_live = &session_live;
    document_join_owner = document;
    lambda_set_document_probe(document_join_current);
    WindowRetirementReentry outer = {ui, document, false, false};
    uv_timer_t outer_timer = {};
    if (uv_timer_init(lambda_uv_loop(), &outer_timer) != 0) _Exit(36);
    outer_timer.data = &outer;
    if (uv_timer_start(&outer_timer, window_retirement_during_uv_run, 0, 0) != 0) _Exit(37);
    lambda_uv_run_loop(lambda_uv_loop(), UV_RUN_NOWAIT);
    if (!outer.called || ui->document.get() != document) _Exit(38);
    WindowRetirementReentry reentry = {ui, document, false, change_owner};
    uv_timer_t timer = {};
    if (uv_timer_init(lambda_uv_loop(), &timer) != 0) _Exit(14);
    timer.data = &reentry;
    if (uv_timer_start(&timer, window_retirement_reenter, 0, 0) != 0) _Exit(15);
    if (distinct_loader) {
        if (!eval_context_transfer_thread(script_owner)) _Exit(22);
        LambdaScheduler* script_scheduler = runtime_scheduler(script);
        if (!script_scheduler) {
            script_scheduler = lambda_scheduler_create(3);
            runtime_set_scheduler(script, script_scheduler);
        }
        RootFrame roots(2);
        Function* fn = document_test_procedure(script_owner, (fn_ptr)guarded_document_stall_task);
        if (!fn || !script_scheduler) _Exit(23);
        Rooted<Item> procedure(roots, (Item){.function = fn});
        Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
        guarded_document_stall_other_task = lambda_task_from_handle(handle.get());
        guarded_document_stall_other_probe = &other_probe;
        if (!guarded_document_stall_other_task || lambda_scheduler_run_ready(script_scheduler) != 1) _Exit(24);
    }
    if (!eval_context_transfer_thread(loader_owner)) _Exit(25);
    LambdaTask* task = nullptr;
    {
        RootFrame roots(2);
        Function* fn = document_test_procedure(loader_owner, (fn_ptr)guarded_document_stall_task);
        if (!fn) _Exit(5);
        Rooted<Item> procedure(roots, (Item){.function = fn});
        Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
        task = lambda_task_from_handle(handle.get());
        if (!task || lambda_scheduler_run_ready(scheduler) != 1) _Exit(6);
    }
    // Allocation failure cannot detach the document or begin its resource walk.
    memtrack_fault_inject(0);
    bool closed = ui->destroy_document();
    memtrack_fault_clear();
    if (closed || ui->document.get() != document || document->destroying || resource_checked) _Exit(16);
    memtrack_fault_inject(1);
    closed = ui->destroy_document();
    memtrack_fault_clear();
    if (closed || ui->document.get() != document || ui->document_retirement || resource_checked) _Exit(31);
    if (distinct_loader) {
        // A refused second pin must preserve the first pin and the admission barrier.
        loader->cleanup_in_progress = true;
        closed = ui->destroy_document();
        loader->cleanup_in_progress = false;
        if (closed || !ui->document_retirement || ui->document.get() != document ||
            runtime_try_cleanup(script) != RUNTIME_CLEANUP_RETRY) _Exit(17);
        {
            RootFrame roots(1);
            Function* fn = document_test_procedure(loader_owner, (fn_ptr)document_lifetime_guard_noop);
            Rooted<Item> procedure(roots, (Item){.function = fn});
            if (!fn || !item_is_error(lambda_task_start_document(procedure.get(), NULL))) _Exit(18);
        }
    }
    if (ui->destroy_document() || ui->document.get() != document || document->destroying ||
        (distinct_loader && ui->loader_runtime.get() != loader) || probe.finished || !reentry.called) _Exit(7);
    if (change_owner) {
        if (ui->destroy_document() || !ui->document_retirement || document->loader_runtime) _Exit(34);
        document->loader_runtime = lam::up(loader);
    }
    if (export_handoff || host_handoff) {
        bool timer_fired = false;
        uv_timer_t unrelated = {};
        if (uv_timer_init(lambda_uv_loop(), &unrelated) != 0) _Exit(44);
        unrelated.data = &timer_fired;
        if (uv_timer_start(&unrelated, window_retirement_unrelated_timer, 100, 0) != 0) _Exit(45);
        bool finished = export_handoff ? render_export_session_finish(&export_session) :
            radiant_window_host_finish(nullptr, nullptr, nullptr, nullptr, false, true);
        if (finished || timer_fired || (export_handoff &&
                (render_export_pending_count() != 1 || export_session.owned_ui_context)) ||
                (host_handoff && !radiant_window_host_pending())) _Exit(42);
        bool retried = export_handoff ? render_export_retire_pending() : radiant_window_host_retry();
        if (retried || timer_fired) _Exit(46);
        uv_timer_stop(&unrelated);
        uv_close((uv_handle_t*)&unrelated, NULL);
    }
    if (distinct_loader) {
        if (!other_probe.parked || other_probe.finished || !probe.parked ||
            !eval_context_transfer_thread(script_owner)) _Exit(26);
        lambda_task_resume(guarded_document_stall_other_task);
        bool completed = export_handoff ? render_export_retire_pending() :
            host_handoff ? radiant_window_host_retry() : ui->destroy_document();
        if (completed || ui->document.get() != document || !other_probe.finished ||
            !probe.parked || probe.finished) _Exit(27);
    } else if (!probe.parked) _Exit(28);
    if (!eval_context_transfer_thread(loader_owner)) _Exit(8);
    lambda_task_resume(task);
    bool completed = export_handoff ? render_export_retire_pending() :
        host_handoff ? radiant_window_host_retry() : ui->destroy_document();
    for (int attempt = 0; !completed && attempt < 8 && (export_handoff || host_handoff); attempt++)
        completed = export_handoff ? render_export_retire_pending() : radiant_window_host_retry();
    if ((!export_handoff && ui->document) || (export_handoff && render_export_pending_count()) ||
            (host_handoff && radiant_window_host_pending()) ||
            !probe.finished || !session_live || !resource_checked) _Exit(9);
    if (retry_runtime_cleanup) {
        if (completed || !held_guard || !ui->document_retirement || !pool_calloc(pool, 8) ||
            runtime_try_cleanup(loader) != RUNTIME_CLEANUP_RETRY ||
            !eval_context_transfer_thread(loader_owner) ||
            !lambda_document_retirement_guard_end(scheduler, &other_document) || !ui->destroy_document()) _Exit(32);
    } else if (!completed) _Exit(33);
    if (distinct_loader && !export_handoff && !host_handoff && !ui_context_release_loader_runtime(ui)) _Exit(10);
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    guarded_document_stall_probe = NULL;
    guarded_document_stall_other_probe = NULL;
    guarded_document_stall_other_task = NULL;
    guarded_document_stall_session = NULL;
    guarded_document_stall_session_live = NULL;
    remove(path);
    _Exit(0);
}

TEST(LambdaDocumentTasks, WindowRetainsDocumentUntilStalledCleanupFinishes) {
    ASSERT_EXIT(exercise_stalled_window_document(false, false, false), ::testing::ExitedWithCode(0), "");
}

TEST(LambdaDocumentTasks, WindowJoinsDistinctLoaderBeforeDestroyingDocument) {
    ASSERT_EXIT(exercise_stalled_window_document(true, false, false), ::testing::ExitedWithCode(0), "");
}

TEST(LambdaDocumentTasks, ExportHandoffRetainsSqliteUntilBothSchedulersJoin) {
    ASSERT_EXIT(exercise_stalled_window_document(true, false, false, WINDOW_RETIRE_EXPORT), ::testing::ExitedWithCode(0), "");
}

TEST(LambdaDocumentTasks, WindowHostRetainsSqliteWithoutWaitingOnUnrelatedTimer) {
    ASSERT_EXIT(exercise_stalled_window_document(true, false, false, WINDOW_RETIRE_HOST), ::testing::ExitedWithCode(0), "");
}

TEST(LambdaDocumentTasks, WindowRetainsDestroyedDocumentResourcesUntilRuntimeRetry) {
    ASSERT_EXIT(exercise_stalled_window_document(false, true, false), ::testing::ExitedWithCode(0), "");
}

TEST(LambdaDocumentTasks, WindowRetainsOwnersWhenLoaderChangesDuringJoin) {
    ASSERT_EXIT(exercise_stalled_window_document(true, false, true), ::testing::ExitedWithCode(0), "");
}

TEST(LambdaDocumentTasks, GuardedJoinRejectsSchedulerWithoutEvaluator) {
    ASSERT_EQ(context, nullptr);
    LambdaScheduler* scheduler = lambda_scheduler_create(3);
    ASSERT_NE(scheduler, nullptr);
    int document = 0;
    ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, &document));
    EXPECT_FALSE(lambda_document_tasks_join_guarded(scheduler, &document));
    EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &document));
    lambda_scheduler_destroy(scheduler);
    lambda_uv_cleanup();
}

TEST(LambdaDocumentTasks, RetirementGuardRetainsHeaplessRuntime) {
    Runtime runtime = {};
    runtime_init(&runtime);
    EvalContext* owner = runtime_get_eval_context(&runtime);
    ASSERT_NE(owner, nullptr);
    ASSERT_TRUE(eval_context_transfer_thread(owner));
    ASSERT_EQ(runtime_heap(&runtime), nullptr);
    LambdaScheduler* scheduler = lambda_scheduler_create(3);
    ASSERT_NE(scheduler, nullptr);
    runtime_set_scheduler(&runtime, scheduler);
    struct HeaplessCloseProbe {
        LambdaScheduler* scheduler;
        int closed;
        bool guard_rejected;
    } close_probe = {scheduler, 0, false};
    RuntimeResourceTable* table = runtime_resource_table_context_ensure(owner);
    ASSERT_NE(table, nullptr);
    static const RuntimeResourceDescriptor marker = {
        RUNTIME_RESOURCE_TIMER, RUNTIME_RESOURCE_GROUP_NONE, "heapless-guard-probe"
    };
    ASSERT_NE(runtime_resource_table_add_native_owned(table, nullptr, &marker,
        [](void* data) {
            HeaplessCloseProbe* probe = (HeaplessCloseProbe*)data;
            probe->closed++;
            bool started = lambda_document_retirement_guard_begin(probe->scheduler, probe);
            probe->guard_rejected = !started;
            if (started) lambda_document_retirement_guard_end(probe->scheduler, probe);
        }, &close_probe), 0u);
    int document = 0;
    ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, &document));
    EXPECT_TRUE(lambda_scheduler_has_document_retirement_guards(scheduler));
    ASSERT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_RETRY);
    EXPECT_EQ(runtime_get_eval_context(&runtime), owner);
    EXPECT_EQ(runtime_scheduler(&runtime), scheduler);
    EXPECT_EQ(close_probe.closed, 0);
    runtime_reset_heap(&runtime);
    EXPECT_EQ(runtime_scheduler(&runtime), scheduler);
    EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &document));
    EXPECT_FALSE(lambda_scheduler_has_document_retirement_guards(scheduler));
    runtime_reset_heap(&runtime);
    EXPECT_TRUE(lambda_document_retirement_guard_begin(scheduler, &document));
    EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &document));
    ASSERT_TRUE(runtime_document_retirement_pin(&runtime));
    ASSERT_TRUE(runtime_document_retirement_pin(&runtime));
    EXPECT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_RETRY);
    EXPECT_EQ(runtime_try_cleanup_retirement_pinned(&runtime), RUNTIME_CLEANUP_RETRY);
    runtime_reset_heap(&runtime);
    EXPECT_EQ(runtime_scheduler(&runtime), scheduler);
    EXPECT_EQ(close_probe.closed, 0);
    EXPECT_TRUE(runtime_document_retirement_unpin(&runtime));
    EXPECT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_RETRY);
    EXPECT_TRUE(runtime_document_retirement_unpin(&runtime));
    EXPECT_FALSE(runtime_document_retirement_unpin(&runtime));
    EXPECT_EQ(runtime_try_cleanup(&runtime), RUNTIME_CLEANUP_COMPLETE);
    EXPECT_EQ(runtime_scheduler(&runtime), nullptr);
    EXPECT_EQ(close_probe.closed, 1);
    EXPECT_TRUE(close_probe.guard_rejected);
}

TEST(LambdaDocumentTasks, PinnedRuntimeCleanupKeepsOtherOwnersPinned) {
    Runtime runtimes[2] = {};
    for (Runtime& runtime : runtimes) runtime_init(&runtime);
    EvalContext* owners[] = {runtime_get_eval_context(&runtimes[0]), runtime_get_eval_context(&runtimes[1])};
    ASSERT_NE(owners[0], nullptr);
    ASSERT_NE(owners[1], nullptr);
    for (int i = 0; i < 2; i++) {
        ASSERT_TRUE(eval_context_transfer_thread(owners[i]));
        ASSERT_TRUE(runtime_document_retirement_pin(&runtimes[i]));
    }
    ASSERT_TRUE(eval_context_transfer_thread(owners[0]));
    struct PinCleanupProbe { Runtime* first; Runtime* second; bool safe; } probe = {
        &runtimes[0], &runtimes[1], false
    };
    RuntimeResourceTable* table = runtime_resource_table_context_ensure(owners[0]);
    ASSERT_NE(table, nullptr);
    static const RuntimeResourceDescriptor marker = {
        RUNTIME_RESOURCE_TIMER, RUNTIME_RESOURCE_GROUP_NONE, "retirement-pin-probe"
    };
    ASSERT_NE(runtime_resource_table_add_native_owned(table, nullptr, &marker, [](void* data) {
        PinCleanupProbe* probe = (PinCleanupProbe*)data;
        probe->safe = !runtime_document_retirement_unpin(probe->first) &&
            runtime_try_cleanup(probe->second) == RUNTIME_CLEANUP_RETRY;
    }, &probe), 0u);
    EXPECT_EQ(runtime_try_cleanup_retirement_pinned(&runtimes[0]), RUNTIME_CLEANUP_COMPLETE);
    EXPECT_TRUE(probe.safe);
    ASSERT_TRUE(eval_context_transfer_thread(owners[1]));
    EXPECT_TRUE(runtime_document_retirement_unpin(&runtimes[1]));
    EXPECT_EQ(runtime_try_cleanup(&runtimes[1]), RUNTIME_CLEANUP_COMPLETE);
}

static Item document_retire_transfer(Context* owner) {
    DocumentRetireProbe* probe = document_retire_probe;
    (void)lambda_document_tasks_cancel(probe->scheduler, probe->document);
    RootFrame roots(2);
    Function* fn = document_test_procedure((EvalContext*)owner, (fn_ptr)document_retire_child);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    // The probe is switched by the host to the second document before this turn.
    Rooted<Item> child(roots, lambda_task_start_document(procedure.get(), NULL));
    probe->child = lambda_task_from_handle(child.get());
    return ItemNull;
}

static Item document_retire_cleanup_branch(Context* owner) {
    DocumentRetireProbe* probe = document_retire_probe;
    RootFrame roots(3);
    Function* fn = document_test_procedure((EvalContext*)owner, (fn_ptr)document_retire_child);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> handle(roots, lambda_task_start_function_scoped(procedure.get(), NULL, true));
    probe->grandchild = lambda_task_from_handle(handle.get());
    probe->grandchild_cancelled = lambda_task_cancel_requested(probe->grandchild);
    Rooted<Item> explicit_handle(roots, lambda_task_start_document(procedure.get(), NULL));
    probe->explicit_grandchild = lambda_task_from_handle(explicit_handle.get());
    probe->explicit_grandchild_cancelled = lambda_task_cancel_requested(probe->explicit_grandchild);
    return ItemNull;
}

static Item document_retire_deep_parent(Context* owner) {
    DocumentRetireProbe* probe = document_retire_probe;
    (void)lambda_document_tasks_cancel(probe->scheduler, probe->document);
    LambdaTask* current = lambda_task_running();
    lambda_task_set_cleanup_masked(current, true);
    RootFrame roots(2);
    Function* fn = document_test_procedure((EvalContext*)owner, (fn_ptr)document_retire_cleanup_branch);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> handle(roots, lambda_task_start_function_scoped(procedure.get(), NULL, true));
    probe->child = lambda_task_from_handle(handle.get());
    lambda_task_set_cleanup_masked(current, false);
    return ItemNull;
}

// S13.3.2: a cleanup child can delegate work without cancelling its descendants.
TEST_F(LambdaConcurrencyRuntime, DocumentRetirementAllowsCleanupDescendants) {
    DocumentRetireProbe probe = {};
    probe.scheduler = scheduler;
    probe.document = &probe;
    document_retire_probe = &probe;
    document_join_owner = probe.document;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(2);
    Function* fn = document_test_procedure(&eval, (fn_ptr)document_retire_deep_parent);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> parent(roots, lambda_task_start_document(procedure.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(parent.get()));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    ASSERT_NE(probe.child, nullptr);
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    ASSERT_NE(probe.grandchild, nullptr);
    ASSERT_NE(probe.explicit_grandchild, nullptr);
    EXPECT_FALSE(probe.grandchild_cancelled);
    EXPECT_FALSE(probe.explicit_grandchild_cancelled);
    EXPECT_FALSE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 2);
    EXPECT_EQ(probe.child_finished, 2);
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    document_retire_probe = NULL;
}

// S13.3.1v2: an explicit different document owns its task, even if its starter is retiring.
TEST_F(LambdaConcurrencyRuntime, DocumentRetirementRespectsExplicitDifferentOwner) {
    DocumentRetireProbe probe = {};
    int other_document = 0;
    probe.scheduler = scheduler;
    probe.document = &probe;
    document_retire_probe = &probe;
    document_join_owner = probe.document;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(2);
    Function* fn = document_test_procedure(&eval, (fn_ptr)document_retire_transfer);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> parent(roots, lambda_task_start_document(procedure.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(parent.get()));
    document_join_owner = &other_document;
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    ASSERT_NE(probe.child, nullptr);
    EXPECT_FALSE(lambda_task_cancel_requested(probe.child));
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    EXPECT_FALSE(lambda_document_tasks_detach_joined(scheduler, &other_document));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    EXPECT_EQ(probe.child_finished, 1);
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, &other_document));
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    document_retire_probe = NULL;
}

// S13.3.1v2: a host may request cancellation from the task and detach only after its descendants join.
TEST_F(LambdaConcurrencyRuntime, DocumentRetirementKeepsOwnershipUntilDescendantsFinish) {
    DocumentRetireProbe probe = {};
    int foreign_document = 0;
    probe.scheduler = scheduler;
    probe.document = &probe;
    document_retire_probe = &probe;
    document_join_owner = probe.document;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(5);
    Function* parent_fn = document_test_procedure(&eval, (fn_ptr)document_retire_parent);
    ASSERT_NE(parent_fn, nullptr);
    Rooted<Item> parent_proc(roots, (Item){.function = parent_fn});
    Rooted<Item> parent(roots, lambda_task_start_document(parent_proc.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(parent.get()));
    document_join_owner = &foreign_document;
    Function* foreign_fn = document_test_procedure(&eval, (fn_ptr)document_retire_foreign);
    ASSERT_NE(foreign_fn, nullptr);
    Rooted<Item> foreign_proc(roots, (Item){.function = foreign_fn});
    Rooted<Item> foreign(roots, lambda_task_start_document(foreign_proc.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(foreign.get()));
    EXPECT_EQ(lambda_document_tasks_cancel(NULL, probe.document), -1);
    EXPECT_FALSE(lambda_document_tasks_detach_joined(NULL, probe.document));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 2);
    EXPECT_EQ(probe.first_cancel, 1);
    EXPECT_EQ(probe.second_cancel, 1);
    EXPECT_TRUE(probe.guard_started);
    EXPECT_FALSE(probe.guard_ended_early);
    EXPECT_FALSE(probe.detached_during_parent);
    EXPECT_TRUE(probe.join_refused_reentrant);
    EXPECT_TRUE(probe.ordinary_child_cancelled);
    ASSERT_NE(probe.child, nullptr);
    EXPECT_FALSE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    EXPECT_EQ(lambda_document_tasks_cancel(scheduler, probe.document), 2);
    EXPECT_FALSE(lambda_task_cancel_requested(probe.child));
    EXPECT_FALSE(lambda_task_cancel_requested(lambda_task_from_handle(foreign.get())));
    document_join_owner = probe.document;
    Item rejected = lambda_task_start_document(parent_proc.get(), NULL);
    EXPECT_TRUE(item_is_error(rejected));
    if (lambda_task_handle_is(rejected)) lambda_task_cancel(lambda_task_from_handle(rejected));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 2);
    EXPECT_EQ(probe.child_finished, 1);
    EXPECT_FALSE(lambda_document_retirement_guard_end(scheduler, probe.document));
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    rejected = lambda_task_start_document(parent_proc.get(), NULL);
    EXPECT_TRUE(item_is_error(rejected));
    if (lambda_task_handle_is(rejected)) lambda_task_cancel(lambda_task_from_handle(rejected));
    EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, probe.document));
    Function* child_fn = document_test_procedure(&eval, (fn_ptr)document_retire_child);
    ASSERT_NE(child_fn, nullptr);
    Rooted<Item> child_proc(roots, (Item){.function = child_fn});
    Item reopened = lambda_task_start_document(child_proc.get(), NULL);
    ASSERT_TRUE(lambda_task_handle_is(reopened));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    EXPECT_EQ(probe.child_finished, 2);
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    EXPECT_FALSE(lambda_document_tasks_detach_joined(scheduler, &foreign_document));
    EXPECT_EQ(lambda_document_tasks_cancel(scheduler, &foreign_document), 1);
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    EXPECT_EQ(probe.foreign_finished, 1);
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, &foreign_document));
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    document_retire_probe = NULL;
}

// S13.3.1v2: shutdown blocks new document owners even when no tasks existed.
TEST_F(LambdaConcurrencyRuntime, RuntimeShutdownClosesDocumentTaskAdmission) {
    int document = 0;
    document_join_owner = &document;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(1);
    Function* fn = document_test_procedure(&eval, (fn_ptr)document_retire_child);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    ASSERT_TRUE(lambda_scheduler_join_document_tasks(scheduler));
    EXPECT_TRUE(item_is_error(lambda_task_start_document(procedure.get(), NULL)));
    EXPECT_FALSE(lambda_document_retirement_guard_begin(scheduler, &document));
    EXPECT_TRUE(lambda_scheduler_join_document_tasks(scheduler));
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
}

// S13.3.1v2: teardown must reject new starts even when no task supplied a marker.
TEST_F(LambdaConcurrencyRuntime, DocumentRetirementGuardCoversEmptyOwner) {
    DocumentRetireProbe probe = {};
    probe.scheduler = scheduler;
    probe.document = &probe;
    document_retire_probe = &probe;
    document_join_owner = probe.document;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(1);
    Function* fn = document_test_procedure(&eval, (fn_ptr)document_retire_child);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    EXPECT_FALSE(lambda_document_retirement_guard_begin(NULL, probe.document));
    memtrack_fault_inject(0);
    bool guarded_after_failure = lambda_document_retirement_guard_begin(scheduler, probe.document);
    memtrack_fault_clear();
    EXPECT_FALSE(guarded_after_failure);
    EXPECT_FALSE(lambda_document_retirement_guard_end(scheduler, probe.document));
    ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, probe.document));
    EXPECT_TRUE(lambda_document_retirement_guard_begin(scheduler, probe.document));
    EXPECT_FALSE(lambda_scheduler_join_document_tasks(scheduler));
    EXPECT_TRUE(item_is_error(lambda_task_start_document(procedure.get(), NULL)));
    EXPECT_EQ(lambda_document_tasks_cancel(scheduler, probe.document), 0);
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    EXPECT_TRUE(item_is_error(lambda_task_start_document(procedure.get(), NULL)));
    EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, probe.document));
    Item started = lambda_task_start_document(procedure.get(), NULL);
    ASSERT_TRUE(lambda_task_handle_is(started));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    EXPECT_EQ(probe.child_finished, 1);
    EXPECT_TRUE(lambda_document_tasks_detach_joined(scheduler, probe.document));
    lambda_set_document_probe(NULL);
    document_join_owner = NULL;
    document_retire_probe = NULL;
}

struct DocumentSwitchProbe {
    Runtime* other;
    uv_sem_t resume_worker;
    int worker_started;
    int worker_finished;
    int task_finished;
    bool switched;
};
static DocumentSwitchProbe* document_switch_probe;

static void document_switch_worker(LambdaOffload* job, void* data) {
    DocumentSwitchProbe* probe = (DocumentSwitchProbe*)data;
    __atomic_store_n(&probe->worker_started, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(lambda_offload_cancel_flag(job), __ATOMIC_ACQUIRE)) uv_sleep(1);
    uv_sem_wait(&probe->resume_worker);
    __atomic_store_n(&probe->worker_finished, 1, __ATOMIC_RELEASE);
}
static Item document_switch_procedure(Context*) {
    static const LambdaOffloadOps ops = {document_switch_worker, NULL, NULL};
    Item result = lambda_task_offload(&ops, document_switch_probe, 0);
    document_switch_probe->task_finished = 1;
    return result;
}

// A shared-loop callback must not strand the retiring document's task in another evaluator.
static void document_join_restores_owner(bool guarded) {
    Runtime runtimes[2] = {};
    struct Cleanup {
        Runtime* runtimes;
        ~Cleanup() {
            lambda_set_document_probe(NULL);
            document_join_owner = NULL;
            document_switch_probe = NULL;
            for (int i = 1; i >= 0; i--) {
                if (!runtimes[i].eval_context) continue;
                js_runtime_context_enter_turn(&runtimes[i], runtimes[i].eval_context);
                runtime_cleanup(&runtimes[i]);
            }
        }
    } cleanup = {runtimes};
    for (int i = 0; i < 2; i++) {
        runtime_init(&runtimes[i]);
        ASSERT_TRUE(eval_context_transfer_thread(runtime_get_eval_context(&runtimes[i])));
        ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtimes[i], "null;", 5,
            "document-join-context.js", NULL)));
    }
    EvalContext* owner = runtime_get_eval_context(&runtimes[0]);
    ASSERT_TRUE(js_runtime_context_enter_turn(&runtimes[0], owner));
    LambdaScheduler* scheduler = lambda_scheduler_create(3);
    ASSERT_NE(scheduler, nullptr);
    runtime_set_scheduler(&runtimes[0], scheduler);
    DocumentSwitchProbe probe = {};
    probe.other = &runtimes[1];
    ASSERT_EQ(uv_sem_init(&probe.resume_worker, 0), 0);
    document_switch_probe = &probe;
    document_join_owner = &probe;
    lambda_set_document_probe(document_join_current);
    RootFrame roots(2);
    Function* fn = document_test_procedure(owner, (fn_ptr)document_switch_procedure);
    ASSERT_NE(fn, nullptr);
    Rooted<Item> procedure(roots, (Item){.function = fn});
    Rooted<Item> handle(roots, lambda_task_start_document(procedure.get(), NULL));
    ASSERT_TRUE(lambda_task_handle_is(handle.get()));
    ASSERT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    for (int i = 0; i < 1000 && !__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE); i++) uv_sleep(1);
    ASSERT_EQ(__atomic_load_n(&probe.worker_started, __ATOMIC_ACQUIRE), 1);
    uv_timer_t timer = {};
    ASSERT_EQ(uv_timer_init(lambda_uv_loop(), &timer), 0);
    timer.data = &probe;
    ASSERT_EQ(uv_timer_start(&timer, [](uv_timer_t* timer) {
        DocumentSwitchProbe* probe = (DocumentSwitchProbe*)timer->data;
        probe->switched = js_runtime_context_enter_turn(probe->other, probe->other->eval_context);
        uv_sem_post(&probe->resume_worker);
    }, 10, 0), 0);
    if (guarded) {
        ASSERT_TRUE(lambda_document_retirement_guard_begin(scheduler, &probe));
        EXPECT_TRUE(lambda_document_tasks_join_guarded(scheduler, &probe));
    } else {
        EXPECT_EQ(lambda_document_tasks_release(&probe), 1);
    }
    bool restored = context == owner;
    if (!restored) ASSERT_TRUE(js_runtime_context_enter_turn(&runtimes[0], owner));
    EXPECT_TRUE(probe.switched);
    EXPECT_TRUE(restored);
    EXPECT_EQ(__atomic_load_n(&probe.worker_finished, __ATOMIC_ACQUIRE), 1);
    EXPECT_EQ(probe.task_finished, 1);
    while (lambda_task_state(lambda_task_from_handle(handle.get())) != LAMBDA_TASK_DONE) {
        uv_run(lambda_uv_loop(), UV_RUN_ONCE);
        lambda_scheduler_run_ready(scheduler);
    }
    if (guarded) EXPECT_TRUE(lambda_document_retirement_guard_end(scheduler, &probe));
    uv_close((uv_handle_t*)&timer, NULL);
    uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);
    uv_sem_destroy(&probe.resume_worker);
}

TEST(LambdaDocumentTasks, ReleaseRestoresOwnerAfterAnotherDocumentRuns) {
    document_join_restores_owner(false);
}

TEST(LambdaDocumentTasks, GuardedJoinRestoresOwnerAfterAnotherDocumentRuns) {
    document_join_restores_owner(true);
}

// S7.4.4: a payload-less compatibility value never borrows another error's state.
TEST_F(LambdaConcurrencyRuntime, LegacyErrorMembersDoNotBorrowAmbientDiagnostics) {
    // Error members intern their stable default text in the runtime name pool.
    eval.name_pool = name_pool_create_runtime(pool);
    ASSERT_NE(eval.name_pool, nullptr);
    RootFrame roots(3);
    Rooted<Item> key(roots, (Item){.item = s2it(heap_strcpy("message", 7))});
    (void)runtime_error_item(ERR_TYPE_MISMATCH, "first unrelated failure", NULL);
    Rooted<Item> first(roots, fn_member(ItemError, key.get()));
    ASSERT_EQ(get_type_id(first.get()), LMD_TYPE_STRING);
    EXPECT_STRNE(first.get().get_chars(), "first unrelated failure");
    (void)runtime_error_item(ERR_INVALID_CALL, "second unrelated failure", NULL);
    Rooted<Item> second(roots, fn_member(ItemError, key.get()));
    ASSERT_EQ(get_type_id(second.get()), LMD_TYPE_STRING);
    EXPECT_STREQ(first.get().get_chars(), second.get().get_chars());
    EXPECT_STRNE(second.get().get_chars(), "second unrelated failure");
}

TEST_F(LambdaConcurrencyRuntime, SmallRuntimeContainersKeepExactBumpHomes) {
    RootFrame roots(2);
    Rooted<Item> held_list(roots, ItemNull);
    Rooted<Item> held_numbers(roots, ItemNull);
    List* items = (List*)heap_calloc(sizeof(List), LMD_TYPE_ARRAY);
    ASSERT_NE(items, nullptr);
    items->type_id = LMD_TYPE_ARRAY;
    held_list.set((Item){.array = items});
    EXPECT_TRUE(items->is_heap);
    EXPECT_EQ(items->length, 0);
    EXPECT_EQ(items->capacity, 0);
    EXPECT_EQ(items->items, nullptr);
    EXPECT_TRUE(gc_get_header(items)->gc_flags & GC_FLAG_BUMP);

    ArrayNum* numbers = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    ASSERT_NE(numbers, nullptr);
    numbers->type_id = LMD_TYPE_ARRAY_NUM;
    numbers->set_elem_type(ELEM_INT);
    held_numbers.set((Item){.array_num = numbers});
    EXPECT_TRUE(numbers->is_heap);
    EXPECT_EQ(numbers->length, 0);
    EXPECT_EQ(numbers->data, nullptr);
    EXPECT_TRUE(gc_get_header(numbers)->gc_flags & GC_FLAG_BUMP);

    // D6.3: the allocator changes physical homes, never the exact-root contract.
    gc_collect(concurrency_test_gc, eval.side_root_base,
        eval.side_root_top - eval.side_root_base);
    EXPECT_EQ(held_list.get().array, items);
    EXPECT_EQ(held_numbers.get().array_num, numbers);
    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, items));
    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, numbers));
    EXPECT_FALSE(gc_is_managed(concurrency_test_gc, (uint8_t*)items + 1));
}

TEST_F(LambdaConcurrencyRuntime, LargeRuntimeContainersKeepZeroedFallback) {
    RootFrame roots(1);
    Rooted<Item> held(roots, ItemNull);
    size_t size = GC_LARGE_OBJECT_THRESHOLD + sizeof(List);
    List* items = (List*)heap_calloc(size, LMD_TYPE_ARRAY);
    ASSERT_NE(items, nullptr);
    items->type_id = LMD_TYPE_ARRAY;
    held.set((Item){.array = items});
    EXPECT_TRUE(items->is_heap);
    EXPECT_TRUE(gc_get_header(items)->gc_flags & GC_FLAG_LARGE);
    EXPECT_EQ(items->length, 0);
    EXPECT_EQ(items->items, nullptr);
    EXPECT_EQ(((uint8_t*)items)[size - 1], 0);
    gc_collect(concurrency_test_gc, eval.side_root_base,
        eval.side_root_top - eval.side_root_base);
    EXPECT_EQ(held.get().array, items);
    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, items));
}

TEST_F(LambdaConcurrencyRuntime, ScalarSnapshotKeepsPreciseHeaderAndPrivatePayload) {
    RootFrame roots(2);
    Rooted<ArrayNum*> source(roots, array_num_new(ELEM_INT, 4));
    ASSERT_NE(source.get(), nullptr);
    for (int i = 0; i < 4; i++) source.get()->items[i] = i + 1;
    int64_t scratch[4] = {};
    Rooted<Item> copy(roots, lambda_array_scalar_snapshot(
        (Item){.array_num = source.get()}, scratch, sizeof(scratch)));
    ASSERT_EQ(get_type_id(copy.get()), LMD_TYPE_ARRAY_NUM);
    EXPECT_NE(copy.get().array_num, source.get());
    EXPECT_EQ(copy.get().array_num->data, scratch);
    EXPECT_EQ(source.get()->cow_state & COW_STATE_SHARED, 0);
    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, copy.get().array_num));
    // D6.3: tracing finds the header; the scalar span needs no stack scan.
    gc_collect(concurrency_test_gc, eval.side_root_base,
        eval.side_root_top - eval.side_root_base);
    EXPECT_EQ(copy.get().array_num->data, scratch);
    for (int i = 0; i < 4; i++) EXPECT_EQ(scratch[i], i + 1);
    copy.get().array_num->items[0] = 99;
    EXPECT_EQ(source.get()->items[0], 1);
}

TEST_F(LambdaConcurrencyRuntime, ScalarSnapshotRetainsOrdinaryCowForOtherLayouts) {
    RootFrame roots(2);
    Rooted<ArrayNum*> large(roots, array_num_new(ELEM_INT, 8));
    Rooted<ArrayNum*> wide(roots, array_num_new(ELEM_FLOAT64, 4));
    ASSERT_NE(large.get(), nullptr);
    ASSERT_NE(wide.get(), nullptr);
    int64_t scratch[4] = {};
    EXPECT_EQ(lambda_array_scalar_snapshot((Item){.array_num = large.get()},
        scratch, sizeof(scratch)).array_num, large.get());
    EXPECT_EQ(lambda_array_scalar_snapshot((Item){.array_num = wide.get()},
        scratch, sizeof(scratch)).array_num, wide.get());
    EXPECT_NE(large.get()->cow_state & COW_STATE_SHARED, 0);
    EXPECT_NE(wide.get()->cow_state & COW_STATE_SHARED, 0);
}

TEST_F(LambdaConcurrencyRuntime, SchedulerRunsRunnableTasksInFifoOrder) {
    int order[3] = {};
    int count = 0;
    RecordFrame frames[3] = {
        {1, order, &count, {.item = i2it(11)}, NULL, ItemNull},
        {2, order, &count, {.item = i2it(22)}, NULL, ItemNull},
        {3, order, &count, {.item = i2it(33)}, NULL, ItemNull},
    };
    LambdaTask* first = lambda_task_create(scheduler, record_and_complete, frame_arg(&frames[0]));
    LambdaTask* second = lambda_task_create(scheduler, record_and_complete, frame_arg(&frames[1]));
    LambdaTask* third = lambda_task_create(scheduler, record_and_complete, frame_arg(&frames[2]));

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(lambda_scheduler_run_ready(scheduler), 3);
    ASSERT_EQ(count, 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
    EXPECT_EQ(lambda_scheduler_live_count(scheduler), 0);
}

TEST_F(LambdaConcurrencyRuntime, SharedLoopWakeRetainsTasksUntilTheirOwnerIsCurrent) {
    int order[1] = {}, count = 0;
    RecordFrame frame = {1, order, &count, {.item = i2it(11)}, NULL, ItemNull};
    LambdaTask* task = lambda_task_create(scheduler, record_and_complete, frame_arg(&frame));
    ASSERT_NE(task, nullptr);
    EvalContext foreign = {};
    ASSERT_TRUE(eval_context_shutdown(&eval));
    ASSERT_TRUE(eval_context_init(&foreign));
    // retiring a different document polls the same libuv loop and consumes this wake notification.
    uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);
    EXPECT_EQ(lambda_scheduler_run_ready(scheduler), 0);
    EXPECT_EQ(lambda_scheduler_run_one(scheduler), 0);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_RUNNABLE);
    EXPECT_TRUE(eval_context_shutdown(&foreign));
    ASSERT_TRUE(eval_context_init(&eval));
    EXPECT_EQ(lambda_scheduler_run_ready(scheduler), 1);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(lambda_task_result(task).item, i2it(11));
}

TEST_F(LambdaConcurrencyRuntime, SharedMapRetypeKeepsSiblingShapeWithoutJsRealm) {
    ASSERT_FALSE(js_realm_runtime_is_active());
    eval.pool = pool;
    Input* owner = runtime_shape_tree();
    ASSERT_NE(owner, nullptr);
    String* key = name_pool_create_len(owner->name_pool, "value", 5);
    String* tail = name_pool_create_len(owner->name_pool, "tail", 4);
    String* replacement = name_pool_create_len(owner->name_pool, "updated", 7);
    ASSERT_NE(key, nullptr);
    ASSERT_NE(tail, nullptr);
    ASSERT_NE(replacement, nullptr);
    Map* maps[2] = {map_pooled(owner->pool), map_pooled(owner->pool)};
    for (Map* map : maps) {
        ASSERT_NE(map, nullptr);
        map_put(map, key, {.item = b2it(false)}, owner);
        map_put(map, tail, {.item = i2it(19)}, owner);
    }
    TypeMap* shared = (TypeMap*)maps[0]->type;
    ASSERT_EQ(maps[1]->type, shared);
    ASSERT_TRUE(typemap_is_shared_shape(shared));
    ASSERT_EQ(fn_map_set({.map = maps[0]}, {.item = s2it(key)},
        {.item = s2it(replacement)}).item, ItemNull.item);
    TypeMap* changed = (TypeMap*)maps[0]->type;
    ShapeEntry* value = typemap_hash_lookup(changed, "value", 5);
    ShapeEntry* trailing = typemap_hash_lookup(changed, "tail", 4);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(trailing, nullptr);
    EXPECT_STREQ(_map_read_field(value, maps[0]->data).get_safe_string()->chars, "updated");
    EXPECT_EQ(lambda_int_item_value(_map_read_field(trailing, maps[0]->data)), 19);
    EXPECT_EQ(maps[1]->type, shared);
    value = typemap_hash_lookup(shared, "value", 5);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type->type_id, LMD_TYPE_BOOL);
    EXPECT_EQ(_map_read_field(value, maps[1]->data).item, b2it(false));
    EXPECT_FALSE(js_realm_runtime_is_active());
}

// JSCU25: a weak registration counts as live until it leaves, a drain with
// nothing left to settle it ends without error, and a token from a destroyed
// scheduler is ignored by its successor.
TEST_F(LambdaConcurrencyRuntime, WeakRegistrationsCountUntilTheyLeave) {
    uint64_t token = lambda_scheduler_weak_enter();
    ASSERT_NE(token, 0u);
    EXPECT_EQ(lambda_scheduler_live_count(scheduler), 1);
    EXPECT_EQ(lambda_scheduler_drain(scheduler), 0);
    EXPECT_EQ(lambda_scheduler_live_count(scheduler), 1);
    lambda_scheduler_weak_leave(token);
    EXPECT_EQ(lambda_scheduler_live_count(scheduler), 0);

    uint64_t stale = lambda_scheduler_weak_enter();
    lambda_scheduler_destroy(scheduler);
    scheduler = lambda_scheduler_create(3);
    ASSERT_NE(scheduler, nullptr);
    uint64_t fresh = lambda_scheduler_weak_enter();
    lambda_scheduler_weak_leave(stale);
    EXPECT_EQ(lambda_scheduler_live_count(scheduler), 1);
    lambda_scheduler_weak_leave(fresh);
    EXPECT_EQ(lambda_scheduler_live_count(scheduler), 0);
}

TEST_F(LambdaConcurrencyRuntime, TaskPollFaultCompletesWithDurableStaticResult) {
    LambdaTask* first = lambda_task_create(scheduler, raise_task_fault, ItemNull);
    LambdaTask* second = lambda_task_create(scheduler, raise_task_fault, ItemNull);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    Item first_result = lambda_task_result(first);
    ASSERT_EQ(get_type_id(first_result), LMD_TYPE_ERROR);
    EXPECT_EQ(it2err(first_result)->code, ERR_STACK_OVERFLOW);
    EXPECT_TRUE(it2err(first_result)->is_static);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);

    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    Item second_result = lambda_task_result(second);
    ASSERT_EQ(get_type_id(second_result), LMD_TYPE_ERROR);
    EXPECT_EQ(it2err(second_result)->code, ERR_STACK_OVERFLOW);
    EXPECT_TRUE(it2err(second_result)->is_static);
    // The second landing reuses the TLS handoff record, but it cannot alter
    // the first completed task's own static result.
    EXPECT_EQ(it2err(first_result)->code, ERR_STACK_OVERFLOW);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);
}

TEST_F(LambdaConcurrencyRuntime, OwnedSlotsPreserveWideIntegersWithoutGcScalarCells) {
    int64_t source_i = INT64_MAX;
    uint64_t source_u = UINT64_MAX;
    uint64_t alloc_i = gc_scalar_tag_allocation_count(LMD_TYPE_INT64);
    uint64_t alloc_u = gc_scalar_tag_allocation_count(LMD_TYPE_UINT64);
    uint64_t alloc_f = gc_scalar_tag_allocation_count(LMD_TYPE_FLOAT);
    Item slots[4] = {};

    EXPECT_FALSE(gc_is_managed(concurrency_test_gc, &source_i));
    EXPECT_FALSE(gc_is_managed(concurrency_test_gc, &source_u));

    owned_item_slot_store(slots, 2, 0, (Item){.item = l2it(&source_i)});
    owned_item_slot_store(slots, 2, 1, (Item){.item = u2it(&source_u)});
    source_i = 0;
    source_u = 0;

    ASSERT_EQ(get_type_id(slots[0]), LMD_TYPE_INT64);
    ASSERT_EQ(get_type_id(slots[1]), LMD_TYPE_UINT64);
    EXPECT_FALSE(gc_is_managed(concurrency_test_gc,
        (void*)(uintptr_t)slots[0].int64_ptr));
    EXPECT_FALSE(gc_is_managed(concurrency_test_gc,
        (void*)(uintptr_t)slots[1].uint64_ptr));
    EXPECT_EQ(gc_scalar_tag_allocation_count(LMD_TYPE_INT64), alloc_i);
    EXPECT_EQ(gc_scalar_tag_allocation_count(LMD_TYPE_UINT64), alloc_u);
    EXPECT_EQ(gc_scalar_tag_allocation_count(LMD_TYPE_FLOAT), alloc_f);

    gc_register_root(concurrency_test_gc, &slots[0].item);
    gc_register_root(concurrency_test_gc, &slots[1].item);
    gc_set_poison_freed(concurrency_test_gc, 1);
    gc_collect(concurrency_test_gc, NULL, 0);
    Item owned_i = owned_item_slot_read(slots, 2, 0, false);
    Item owned_u = owned_item_slot_read(slots, 2, 1, false);
    EXPECT_EQ(owned_i.get_int64(), INT64_MAX);
    EXPECT_EQ(owned_u.get_uint64(), UINT64_MAX);
    // Escaped reads must survive their owning slot being overwritten.
    owned_item_slot_store(slots, 2, 0, (Item){.item = i2it(1)});
    owned_item_slot_store(slots, 2, 1, (Item){.item = i2it(2)});
    slots[2].item = 0;
    slots[3].item = 0;
    EXPECT_EQ(owned_i.get_int64(), INT64_MAX);
    EXPECT_EQ(owned_u.get_uint64(), UINT64_MAX);
    gc_unregister_root(concurrency_test_gc, &slots[0].item);
    gc_unregister_root(concurrency_test_gc, &slots[1].item);
}

TEST_F(LambdaConcurrencyRuntime, OwnedSlotsRehomeTinyFloatsAndPreserveJsNumbers) {
    int64_t number = 42;
    double tiny = 5e-324;
    Item slots[4] = {};
    owned_item_slot_store(slots, 2, 0, (Item){.item = l2it(&number)});
    owned_item_slot_store(slots, 2, 1, lambda_float_ptr_to_item(&tiny));

    Item lambda_number = owned_item_slot_read(slots, 2, 0, false);
    Item js_number = js_owned_item_slot_read(slots, 2, 0, false);
    Item saved_float = owned_item_slot_read(slots, 2, 1, false);
    EXPECT_EQ(get_type_id(lambda_number), LMD_TYPE_INT64);
    EXPECT_EQ(js_number.item, i2it(42));
    slots[2].item = 0;
    slots[3].item = 0;
    EXPECT_EQ(lambda_number.get_int64(), 42);
    EXPECT_EQ(saved_float.get_double(), tiny);
}

TEST_F(LambdaConcurrencyRuntime, ScalarReadHostTracksEvaluatorBinding) {
    int64_t value = INT64_MAX;
    Item borrowed = {.item = l2it(&value)};
    Item owned = scalar_storage_read(borrowed, false);
    EXPECT_NE(owned.item, borrowed.item);
    ASSERT_TRUE(eval_context_shutdown(&eval));
    Item native_read = scalar_storage_read(borrowed, false);
    EXPECT_EQ(native_read.item, borrowed.item);
    ASSERT_TRUE(eval_context_init(&eval));
    EXPECT_NE(scalar_storage_read(borrowed, false).item, borrowed.item);
}

TEST_F(LambdaConcurrencyRuntime, GcRejectsEveryScalarObjectAllocationRoute) {
    EXPECT_DEATH({
        gc_heap_alloc(concurrency_test_gc, sizeof(int64_t), LMD_TYPE_INT64);
    }, "gc-scalar-invariant");
    EXPECT_DEATH({
        gc_heap_calloc_class(concurrency_test_gc, sizeof(uint64_t),
            LMD_TYPE_UINT64, 0);
    }, "gc-scalar-invariant");
    EXPECT_DEATH({
        gc_heap_bump_alloc(concurrency_test_gc, sizeof(uint64_t) * 2,
            sizeof(double), LMD_TYPE_FLOAT, 0);
    }, "gc-scalar-invariant");
}

TEST_F(LambdaConcurrencyRuntime, MailboxIsBoundedAndDequeuesOnlyFromFifoHead) {
    ParkFrame target_frame = {};
    LambdaTask* target = lambda_task_create(scheduler, park_then_finish, frame_arg(&target_frame));
    ASSERT_NE(target, nullptr);

    EXPECT_EQ(lambda_task_mailbox_capacity(target), 3);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(10)}), LAMBDA_SEND_OK);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(20)}), LAMBDA_SEND_OK);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(30)}), LAMBDA_SEND_OK);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(40)}), LAMBDA_SEND_FULL);

    Item value = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(target, &value));
    EXPECT_EQ(it2i(value), 10);
    ASSERT_TRUE(lambda_task_mailbox_receive(target, &value));
    EXPECT_EQ(it2i(value), 20);
    ASSERT_TRUE(lambda_task_mailbox_receive(target, &value));
    EXPECT_EQ(it2i(value), 30);
    EXPECT_FALSE(lambda_task_mailbox_receive(target, &value));
}

TEST_F(LambdaConcurrencyRuntime, TaskPersistentSlotsOwnWideIntegerPayloads) {
    int order[1] = {};
    int count = 0;
    int64_t result_source = INT64_MAX;
    RecordFrame result_frame = {
        1, order, &count, {.item = l2it(&result_source)}, NULL, ItemNull};
    LambdaTask* completed = lambda_task_create(
        scheduler, record_and_complete, frame_arg(&result_frame));
    ASSERT_NE(completed, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    result_source = 0;

    ParkFrame parked_frame = {};
    LambdaTask* parked = lambda_task_create(
        scheduler, park_then_finish, frame_arg(&parked_frame));
    ASSERT_NE(parked, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    uint64_t message_source = UINT64_MAX;
    ASSERT_EQ(lambda_task_send(NULL, parked,
        (Item){.item = u2it(&message_source)}), LAMBDA_SEND_OK);
    message_source = 0;

    gc_set_poison_freed(concurrency_test_gc, 1);
    gc_collect(concurrency_test_gc, NULL, 0);

    EXPECT_EQ(lambda_task_result(completed).get_int64(), INT64_MAX);
    Item message = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(parked, &message));
    EXPECT_EQ(message.get_uint64(), UINT64_MAX);

    uint64_t resume_source = UINT64_MAX - 1;
    lambda_task_resume_external(parked,
        (Item){.item = u2it(&resume_source)});
    resume_source = 0;
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    EXPECT_EQ(lambda_task_result(parked).get_uint64(), UINT64_MAX - 1);
}

TEST_F(LambdaConcurrencyRuntime, CompletionIsPublishedAfterFinalSend) {
    int order[1] = {};
    int count = 0;
    RecordFrame sender_frame = {
        7, order, &count, {.item = i2it(99)}, NULL, {.item = i2it(42)}};
    LambdaTask* sender = lambda_task_create(
        scheduler, record_and_complete, frame_arg(&sender_frame));
    ASSERT_NE(sender, nullptr);
    ParkFrame receiver_frame = {};
    LambdaTask* receiver = lambda_task_create(scheduler, park_then_finish,
        frame_arg(&receiver_frame));
    ASSERT_NE(receiver, nullptr);
    sender_frame.send_target = receiver;
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);

    EXPECT_GT(lambda_task_last_send_sequence(sender), 0u);
    EXPECT_GT(lambda_task_completion_sequence(sender),
        lambda_task_last_send_sequence(sender));
    Item received = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(receiver, &received));
    EXPECT_EQ(it2i(received), 42);
}

TEST_F(LambdaConcurrencyRuntime, CancellationUnparksAndIsIdempotent) {
    ParkFrame frame = {};
    LambdaTask* task = lambda_task_create(scheduler, park_then_finish, frame_arg(&frame));
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(task), LAMBDA_TASK_PARKED);

    EXPECT_TRUE(lambda_task_cancel(task));
    EXPECT_TRUE(lambda_task_cancel(task));
    EXPECT_TRUE(lambda_task_cancel_requested(task));
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_RUNNABLE);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_DONE);
    EXPECT_EQ(get_type_id(lambda_task_result(task)), LMD_TYPE_ERROR);
    EXPECT_TRUE(lambda_task_cancel(task));
}

TEST_F(LambdaConcurrencyRuntime, CleanupMaskDefersCancellationUntilUnmasked) {
    ParkFrame frame = {};
    LambdaTask* task = lambda_task_create(scheduler, park_then_finish, frame_arg(&frame));
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(task), LAMBDA_TASK_PARKED);

    lambda_task_set_cleanup_masked(task, true);
    EXPECT_TRUE(lambda_task_cancel(task));
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_PARKED);
    lambda_task_set_cleanup_masked(task, false);
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_RUNNABLE);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    EXPECT_EQ(get_type_id(lambda_task_result(task)), LMD_TYPE_ERROR);
}

TEST_F(LambdaConcurrencyRuntime, WaitTimeoutDoesNotCancelObservedTask) {
    ParkFrame target_frame = {};
    LambdaTask* target = lambda_task_create(scheduler, park_then_finish, frame_arg(&target_frame));
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(target), LAMBDA_TASK_PARKED);

    WaitFrame waiter_frame = {
        lambda_task_handle(target), (Item){.item = i2it(1)}};
    LambdaTask* waiter = lambda_task_create(scheduler, wait_with_timeout, frame_arg(&waiter_frame));
    ASSERT_NE(waiter, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(waiter), LAMBDA_TASK_PARKED);

    ASSERT_NE(lambda_uv_loop(), nullptr);
    // A previously queued async wake may satisfy UV_RUN_ONCE before the 1 ms
    // timeout; keep polling until the timeout resumes and runs the waiter.
    for (int i = 0; i < 16 && lambda_task_state(waiter) != LAMBDA_TASK_DONE; i++) {
        uv_run(lambda_uv_loop(), UV_RUN_ONCE);
        lambda_scheduler_run_ready(scheduler);
    }
    EXPECT_EQ(lambda_task_state(waiter), LAMBDA_TASK_DONE);
    ASSERT_EQ(get_type_id(lambda_task_result(waiter)), LMD_TYPE_ERROR);
    LambdaError* error = it2err(lambda_task_result(waiter));
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_TIMEOUT);
    EXPECT_EQ(lambda_task_state(target), LAMBDA_TASK_PARKED);
    EXPECT_FALSE(lambda_task_cancel_requested(target));
}

TEST_F(LambdaConcurrencyRuntime, SchedulerOwnsEveryTaskGcEdge) {
    int initial_slots = concurrency_test_gc->root_slot_count;
    int initial_ranges = concurrency_test_gc->root_range_count;
    ParkFrame frame = {};
    LambdaTask* task = lambda_task_create(scheduler, park_then_finish, frame_arg(&frame));
    ASSERT_NE(task, nullptr);

    // handle, result, resume value and the mailbox; the task's own frames
    // live in its activation's segment, traced without registration (RA3).
    EXPECT_EQ(concurrency_test_gc->root_slot_count, initial_slots + 3);
    EXPECT_EQ(concurrency_test_gc->root_range_count, initial_ranges + 1);

    lambda_scheduler_destroy(scheduler);
    scheduler = NULL;
    eval.scheduler = NULL;
    EXPECT_EQ(concurrency_test_gc->root_slot_count, initial_slots);
    EXPECT_EQ(concurrency_test_gc->root_range_count, initial_ranges);
}

TEST_F(LambdaConcurrencyRuntime, ParkedFramesAndMailboxesSurviveCollection) {
    VMap* frame_value = NULL;
    LambdaTask* task = lambda_task_create(scheduler, hold_vmap_then_park,
        frame_arg(&frame_value));
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_NE(frame_value, nullptr);

    VMap* message_value = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    ASSERT_NE(message_value, nullptr);
    ASSERT_EQ(lambda_task_send(NULL, task, (Item){.vmap = message_value}), LAMBDA_SEND_OK);

    gc_collect(concurrency_test_gc, NULL, 0);

    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, frame_value));
    Item message = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(task, &message));
    EXPECT_EQ(message.vmap, message_value);
    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, message.vmap));
}

static Item completed_managed_result(Activation*, Item) {
    VMap* value = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    if (!value) return ItemError;
    value->type_id = LMD_TYPE_VMAP;
    return (Item){.vmap = value};
}

static void* completed_result_expected;
static bool completed_result_destroyed;
static void observe_completed_result_destroy(void* object, void* data) {
    if (object == completed_result_expected) completed_result_destroyed = true;
    vmap_gc_destroy(object, data);
}

TEST_F(LambdaConcurrencyRuntime, CompletedResultFollowsHandleReachability) {
    // this minimal fixture supplies the lifecycle callback normally installed by heap_init.
    concurrency_test_gc->vmap_destroy = observe_completed_result_destroy;
    completed_result_destroyed = false;
    RootFrame roots(1);
    Rooted<Item> handle(roots, ItemNull);
    LambdaTask* task = lambda_task_create(scheduler, completed_managed_result, ItemNull);
    ASSERT_NE(task, nullptr);
    handle.set(lambda_task_handle(task));
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    VMap* result = lambda_task_result(task).vmap;
    ASSERT_NE(result, nullptr);
    completed_result_expected = result;
    gc_collect(concurrency_test_gc, eval.side_root_base, eval.side_root_top - eval.side_root_base);
    EXPECT_FALSE(completed_result_destroyed);
    handle.set(ItemNull);
    gc_collect(concurrency_test_gc, eval.side_root_base, eval.side_root_top - eval.side_root_base);
    EXPECT_TRUE(completed_result_destroyed);
    EXPECT_EQ(get_type_id(lambda_task_result(task)), LMD_TYPE_NULL);
}

// ---------------------------------------------------------------------------
// Activation core (vibe/Lambda_Design_Runtime_Async.md P0)
// ---------------------------------------------------------------------------

extern "C" void heap_gc_collect(void);

class ActivationCore : public LambdaConcurrencyRuntime {
protected:
    void SetUp() override {
        LambdaConcurrencyRuntime::SetUp();
        gc_set_poison_freed(concurrency_test_gc, 1);
    }
};

static Item activation_count_up(Activation* self, Item arg) {
    (void)self;
    int64_t total = lambda_int_item_to_i64(arg);
    for (int i = 0; i < 3; i++) {
        Item input = activation_suspend((Item){.item = i2it(total)});
        total += lambda_int_item_to_i64(input);
    }
    return (Item){.item = i2it(total)};
}

TEST_F(ActivationCore, SuspendAndResumeExchangeValues) {
    Activation* activation = activation_create(activation_count_up,
        (Item){.item = i2it(1)}, true, NULL);
    ASSERT_NE(activation, nullptr);
    EXPECT_EQ(activation_status(activation), ACTIVATION_NEW);
    EXPECT_EQ(activation_resume(activation, ItemNull), ACTIVATION_SUSPENDED);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(activation)), 1);
    EXPECT_EQ(activation_resume(activation, (Item){.item = i2it(10)}), ACTIVATION_SUSPENDED);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(activation)), 11);
    EXPECT_EQ(activation_resume(activation, (Item){.item = i2it(100)}), ACTIVATION_SUSPENDED);
    EXPECT_EQ(activation_resume(activation, (Item){.item = i2it(1000)}), ACTIVATION_DONE);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(activation)), 1111);
    EXPECT_EQ(activation_current(), nullptr);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);
    activation_destroy(activation);
}

static Item activation_check_execution_depth(Activation*, Item arg) {
    // D6.4.1: a fresh stack cannot advertise quiescence while guest frames run.
    EXPECT_GT(context->execution_depth, 0u);
    Item resumed = activation_suspend(arg);
    EXPECT_GT(context->execution_depth, 0u);
    return resumed;
}

TEST_F(ActivationCore, GuestExecutionDepthFollowsSuspension) {
    uint32_t original_depth = context->execution_depth;
    Activation* activation = activation_create(activation_check_execution_depth,
        ItemNull, true, NULL);
    ASSERT_NE(activation, nullptr);
    {
        RuntimeExecutionScope caller;
        uint32_t caller_depth = context->execution_depth;
        EXPECT_EQ(activation_resume(activation, ItemNull), ACTIVATION_SUSPENDED);
        EXPECT_EQ(context->execution_depth, caller_depth);
        EXPECT_EQ(activation_resume(activation, ItemNull), ACTIVATION_DONE);
        EXPECT_EQ(context->execution_depth, caller_depth);
    }
    EXPECT_EQ(context->execution_depth, original_depth);
    activation_destroy(activation);
}

// RA10: beneath a native barrier the activation cannot park; once the
// barrier is left it can. A barrier pins only the stack that raised it.
static Item activation_barriered(Activation* self, Item arg) {
    (void)self;
    activation_barrier_enter();
    Item refused = activation_suspend(arg);
    activation_barrier_leave();
    if (get_type_id(refused) != LMD_TYPE_ERROR) return ItemNull;
    return activation_suspend(arg);
}

TEST_F(ActivationCore, NativeBarrierRefusesSuspension) {
    Activation* activation = activation_create(activation_barriered,
        (Item){.item = i2it(7)}, true, NULL);
    ASSERT_NE(activation, nullptr);
    EXPECT_EQ(activation_resume(activation, ItemNull), ACTIVATION_SUSPENDED);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(activation)), 7);
    EXPECT_EQ(activation_resume(activation, (Item){.item = i2it(9)}), ACTIVATION_DONE);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(activation)), 9);
    activation_destroy(activation);
}

// RA6: work run through activation_call_on_base runs on the thread's base
// stack — about 3 MB of live frames, more than an activation's 2 MB stack
// holds, completes there — and it cannot park.
// Each frame's array is read after the recursive call returns, so every frame
// stays live: no loop transformation can fold the recursion away.
__attribute__((noinline)) static int64_t base_stack_depth_probe(int64_t depth) {
    volatile char pad[1024];
    pad[depth & 1023] = 1;
    if (depth <= 0) return 0;
    int64_t below = base_stack_depth_probe(depth - 1);
    return below + pad[depth & 1023];
}

static void* base_stack_work(void* data) {
    int64_t* out = (int64_t*)data;
    out[0] = base_stack_depth_probe(3 * 1024);  // about 3 MB of frames
    out[1] = get_type_id(activation_suspend(ItemNull)) == LMD_TYPE_ERROR ? 1 : 0;
    return out;
}

static Item activation_calls_on_base(Activation* self, Item arg) {
    (void)self;
    (void)arg;
    int64_t out[2] = {0, 0};
    bool faulted = true;
    void* result = activation_call_on_base(base_stack_work, out, &faulted);
    if (faulted || result != out || out[1] != 1) return ItemNull;
    return (Item){.item = i2it(out[0])};
}

TEST_F(ActivationCore, StackHungryWorkRunsOnTheBaseStack) {
    Activation* activation = activation_create(activation_calls_on_base,
        ItemNull, true, NULL);
    ASSERT_NE(activation, nullptr);
    EXPECT_EQ(activation_resume(activation, ItemNull), ACTIVATION_DONE);
    EXPECT_EQ(activation_fault(activation), nullptr);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(activation)), 3 * 1024);
    activation_destroy(activation);
}

static Item activation_inner(Activation* self, Item arg) {
    (void)self;
    Item input = activation_suspend(arg);
    return (Item){.item = i2it(lambda_int_item_to_i64(input) * 2)};
}

static Item activation_outer(Activation* self, Item arg) {
    Activation* inner = activation_create(activation_inner, arg, true, NULL);
    EXPECT_EQ(activation_resume(inner, ItemNull), ACTIVATION_SUSPENDED);
    EXPECT_EQ(activation_current(), self);
    // The outer stack parks while the inner one is parked too.
    Item from_base = activation_suspend(activation_value(inner));
    EXPECT_EQ(activation_resume(inner, from_base), ACTIVATION_DONE);
    Item result = activation_value(inner);
    activation_destroy(inner);
    return result;
}

TEST_F(ActivationCore, NestedActivationsReturnToTheirResumer) {
    Activation* outer = activation_create(activation_outer,
        (Item){.item = i2it(7)}, true, NULL);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(activation_resume(outer, ItemNull), ACTIVATION_SUSPENDED);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(outer)), 7);
    EXPECT_EQ(activation_resume(outer, (Item){.item = i2it(21)}), ACTIVATION_DONE);
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(outer)), 42);
    activation_destroy(outer);
}

static Item activation_hold_string(Activation* self, Item arg) {
    (void)self;
    (void)arg;
    RootFrame roots(1);
    Rooted<Item> held(roots, (Item){.item = s2it(heap_strcpy("parked-root", 11))});
    // A collection runs while this frame is parked off the running stack.
    activation_suspend(ItemNull);
    String* string = (String*)(uintptr_t)(held.get().item & ~ITEM_HIGH_BYTE_MASK);
    bool intact = string && string->len == 11 &&
        memcmp(string->chars, "parked-root", 11) == 0;
    return (Item){.item = b2it(intact ? 1 : 0)};
}

TEST_F(ActivationCore, ParkedRootSegmentSurvivesCollection) {
    Activation* activation = activation_create(activation_hold_string,
        ItemNull, true, NULL);
    ASSERT_NE(activation, nullptr);
    ASSERT_EQ(activation_resume(activation, ItemNull), ACTIVATION_SUSPENDED);
    for (int i = 0; i < 3; i++) {
        (void)heap_strcpy("garbage", 7);
        heap_gc_collect();
    }
    ASSERT_EQ(activation_resume(activation, ItemNull), ACTIVATION_DONE);
    EXPECT_EQ(activation_value(activation).item, (uint64_t)ITEM_TRUE);
    activation_destroy(activation);
}

TEST_F(ActivationCore, WeakParkedActivationIsTracedOnlyThroughItsOwner) {
    Activation* activation = activation_create(activation_hold_string,
        ItemNull, false, NULL);
    ASSERT_NE(activation, nullptr);
    ASSERT_EQ(activation_resume(activation, ItemNull), ACTIVATION_SUSPENDED);
    // No owner trace runs here, so the string the weak activation holds is
    // unreachable; its destroy must still release the stack cleanly.
    heap_gc_collect();
    activation_destroy(activation);
    EXPECT_EQ(activation_current(), nullptr);
}

static int activation_recurse(volatile int depth) {
    volatile char pad[512];
    pad[0] = (char)depth;
    return activation_recurse(depth + 1) + pad[0];
}

static Item activation_overflow(Activation* self, Item arg) {
    (void)self;
    (void)arg;
    return (Item){.item = i2it(activation_recurse(0))};
}

TEST_F(ActivationCore, StackOverflowLandsOnTheActivationsOwnBoundary) {
    lambda_stack_init();
    Activation* activation = activation_create(activation_overflow,
        ItemNull, true, NULL);
    ASSERT_NE(activation, nullptr);
    ASSERT_EQ(activation_resume(activation, ItemNull), ACTIVATION_DONE);
    const LambdaFaultRecord* fault = activation_fault(activation);
    ASSERT_NE(fault, nullptr);
    EXPECT_EQ(fault->error.code, ERR_STACK_OVERFLOW);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);
    activation_destroy(activation);
    // The base stack is untouched and can host another activation.
    Activation* again = activation_create(activation_count_up,
        (Item){.item = i2it(0)}, true, NULL);
    EXPECT_EQ(activation_resume(again, ItemNull), ACTIVATION_SUSPENDED);
    activation_destroy(again);
}

// A bounded native recursion must land on the activation's own boundary too.
__attribute__((noinline)) static int64_t activation_finite_recursion(int64_t depth) {
    volatile char pad[1024];
    pad[depth & 1023] = 1;
    if (depth <= 0) return 0;
    int64_t below = activation_finite_recursion(depth - 1);
    return below + pad[depth & 1023];
}

static Item activation_finite_overflow(Activation*, Item) {
    return (Item){.item = i2it(activation_finite_recursion(3 * 1024))};
}

TEST_F(ActivationCore, FiniteNativeOverflowLandsOnTheActivationsOwnBoundary) {
    lambda_stack_init();
    Activation* activation = activation_create(activation_finite_overflow,
        ItemNull, true, NULL);
    ASSERT_NE(activation, nullptr);
    ASSERT_EQ(activation_resume(activation, ItemNull), ACTIVATION_DONE);
    const LambdaFaultRecord* fault = activation_fault(activation);
    ASSERT_NE(fault, nullptr);
    EXPECT_EQ(fault->error.code, ERR_STACK_OVERFLOW);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);
    activation_destroy(activation);
}

TEST_F(ActivationCore, ColdParkedActivationsCompactAndRestore) {
    // past the resident budget the oldest parked activations are copied out and
    // their pages returned; a collection must still see their roots, and a
    // resume must find every frame and root exactly where it left them.
    const int count = LAMBDA_STACK_DEFAULT_BUDGET / (4 * fiber_page_size()) + 1;
    Activation** held = (Activation**)mem_calloc(count, sizeof(Activation*), MEM_CAT_EVAL);
    Activation** counters = (Activation**)mem_calloc(count, sizeof(Activation*), MEM_CAT_EVAL);
    ASSERT_NE(held, nullptr);
    ASSERT_NE(counters, nullptr);
    for (int i = 0; i < count; i++) {
        held[i] = activation_create(activation_hold_string, ItemNull, true, NULL);
        ASSERT_NE(held[i], nullptr);
        ASSERT_EQ(activation_resume(held[i], ItemNull), ACTIVATION_SUSPENDED);
        counters[i] = activation_create(activation_count_up,
            (Item){.item = i2it(i)}, true, NULL);
        ASSERT_NE(counters[i], nullptr);
        ASSERT_EQ(activation_resume(counters[i], ItemNull), ACTIVATION_SUSPENDED);
    }
    for (int round = 0; round < 3; round++) {
        (void)heap_strcpy("garbage", 7);
        heap_gc_collect();
    }
    // The first counter is long compacted; its parked value reads from the image.
    EXPECT_EQ(lambda_int_item_to_i64(activation_value(counters[0])), 0);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(activation_resume(held[i], ItemNull), ACTIVATION_DONE);
        EXPECT_EQ(activation_value(held[i]).item, (uint64_t)ITEM_TRUE) << "held " << i;
        activation_destroy(held[i]);
        EXPECT_EQ(activation_resume(counters[i], (Item){.item = i2it(1)}),
            ACTIVATION_SUSPENDED);
        EXPECT_EQ(lambda_int_item_to_i64(activation_value(counters[i])), i + 1);
    }
    // Abandoning compacted and warm activations alike releases them.
    for (int i = 0; i < count; i++) activation_destroy(counters[i]);
    mem_free(held);
    mem_free(counters);
    EXPECT_EQ(activation_current(), nullptr);
}

static Item activation_park_mixed_recovery_chain(Activation*, Item) {
    LambdaRecoveryFrame local = {};
    if (!lambda_recovery_frame_push(&local, LAMBDA_RECOVERY_CAP_TEST_CONTAINMENT)) return ItemError;
    LambdaRecoveryFrame* owned = lambda_recovery_frame_begin_for(context,
        LAMBDA_RECOVERY_CAP_TEST_CONTAINMENT);
    if (!owned) return ItemError;
    activation_suspend(ItemNull);
    bool released = lambda_recovery_frame_end(owned) && lambda_recovery_frame_pop(&local);
    return Item{.item = b2it(released)};
}

TEST_F(ActivationCore, ColdAbandonmentWalksMixedRecoveryOwnership) {
    const int count = LAMBDA_STACK_DEFAULT_BUDGET / (2 * fiber_page_size()) + 1;
    Activation** parked = (Activation**)mem_calloc(count, sizeof(Activation*), MEM_CAT_EVAL);
    ASSERT_NE(parked, nullptr);
    for (int i = 0; i < count; i++) {
        parked[i] = activation_create(activation_park_mixed_recovery_chain, ItemNull, true, NULL);
        ASSERT_NE(parked[i], nullptr);
        ASSERT_EQ(activation_resume(parked[i], ItemNull), ACTIVATION_SUSPENDED);
    }
    heap_gc_collect();
    for (int i = 0; i < count; i++) {
        if (i & 1) {
            EXPECT_EQ(activation_resume(parked[i], ItemNull), ACTIVATION_DONE);
            EXPECT_EQ(activation_value(parked[i]).item, (uint64_t)ITEM_TRUE);
        }
        activation_destroy(parked[i]);
    }
    mem_free(parked);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);
}

TEST_F(ActivationCore, AbandonedParkedActivationsAreReleased) {
    for (int i = 0; i < 100; i++) {
        Activation* activation = activation_create(activation_count_up,
            (Item){.item = i2it(i)}, true, NULL);
        ASSERT_NE(activation, nullptr);
        ASSERT_EQ(activation_resume(activation, ItemNull), ACTIVATION_SUSPENDED);
        activation_destroy(activation);
    }
    EXPECT_EQ(activation_current(), nullptr);
    EXPECT_EQ(activation_suspend(ItemNull).item != ItemNull.item, true);
}

static Item activation_pool_storage_probe(Activation*, Item arg) {
    // touch multiple pages in every region; a pooled mapping must be reclaimed
    // before either its native frames or its precise roots are installed.
    volatile uint8_t native[64 * 1024];
    RootFrame roots(4096);
    if (!roots.valid()) return ItemNull;
    Rooted<Item> held(roots, (Item){.item = s2it(heap_strcpy("pooled-root", 11))});
    for (size_t i = 1; i < 4096; i++) *roots.slot(i) = held.get().item;
    uint64_t* numbers = context->side_number_top;
    for (size_t i = 0; i < 4096; i++) {
        uint64_t* slot = lambda_side_number_alloc();
        if (!slot) return ItemNull;
        *slot = i + arg.item;
    }
    for (size_t i = 0; i < sizeof(native); i += 4096) native[i] = (uint8_t)(i / 4096);
    activation_suspend(arg);
    String* string = (String*)(uintptr_t)(held.get().item & ~ITEM_HIGH_BYTE_MASK);
    bool intact = string && string->len == 11 && !memcmp(string->chars, "pooled-root", 11);
    for (size_t i = 1; i < 4096; i++) intact &= *roots.slot(i) == held.get().item;
    for (size_t i = 0; i < 4096; i++) intact &= numbers[i] == i + arg.item;
    for (size_t i = 0; i < sizeof(native); i += 4096) intact &= native[i] == i / 4096;
    return (Item){.item = b2it(intact)};
}

TEST_F(ActivationCore, IdlePoolReusesAndTrimsAllThreeRegions) {
    // exceed the pool bound, then reuse it both before and after a collection.
    // parked roots survive GC; idle storage is not part of the root region.
    const int count = 40;
    Activation* parked[count];
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < count; i++) {
            parked[i] = activation_create(activation_pool_storage_probe,
                (Item){.item = i2it(i + round)}, true, NULL);
            ASSERT_NE(parked[i], nullptr);
            ASSERT_EQ(activation_resume(parked[i], ItemNull), ACTIVATION_SUSPENDED);
        }
        heap_gc_collect();
        for (int i = 0; i < count; i++) {
            ASSERT_EQ(activation_resume(parked[i], ItemNull), ACTIVATION_DONE);
            EXPECT_EQ(activation_value(parked[i]).item, (uint64_t)ITEM_TRUE);
            activation_destroy(parked[i]);
        }
        if (round % 2) heap_gc_collect();
    }
    EXPECT_EQ(activation_current(), nullptr);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);
}

TEST_F(ActivationCore, IdlePoolDoesNotRetainCompletedValues) {
    concurrency_test_gc->vmap_destroy = observe_completed_result_destroy;
    completed_result_destroyed = false;
    Activation* activation = activation_create(completed_managed_result, ItemNull, true, NULL);
    ASSERT_NE(activation, nullptr);
    ASSERT_EQ(activation_resume(activation, ItemNull), ACTIVATION_DONE);
    completed_result_expected = activation_value(activation).vmap;
    ASSERT_NE(completed_result_expected, nullptr);
    activation_destroy(activation);
    // the idle stack still contains its transfer slot, but owns no live roots.
    heap_gc_collect();
    EXPECT_TRUE(completed_result_destroyed);
}

#ifndef _WIN32

// The script code and its import cone are compiled once below.  Every worker
// receives the same sealed Script pointers, but owns its heap/module slabs/ICs.
// This is the runtime-globals invariant: only immutable code is shared.
typedef struct SharedModuleStressCase {
    const char* name;
    const char* source;
    Script* script;
} SharedModuleStressCase;

// mark.type reaches a string-annotated chart helper; Lambda single-quoted
// literals are symbols and must not be used for this fixture's mark names.
static const char* kSharedModuleStressChartBar =
    "import vega: lambda.chart.vega\n"
    "import chart: lambda.chart.chart\n"
    "let spec = vega.convert({width: 120, height: 80, data: {values: "
    "[{category: 'A', amount: 2}, {category: 'B', amount: 5}]}, mark: "
    "{type: \"bar\"}, encoding: {x: {field: 'category', type: 'nominal'}, "
    "y: {field: 'amount', type: 'quantitative'}}})\n"
    "len(format(chart.render_spec(spec), 'xml'))\n";

static const char* kSharedModuleStressChartLine =
    "import vega: lambda.chart.vega\n"
    "import chart: lambda.chart.chart\n"
    "let spec = vega.convert({width: 120, height: 80, data: {values: "
    "[{x: 0, y: 3}, {x: 1, y: 7}, {x: 2, y: 4}]}, mark: {type: \"line\"}, "
    "encoding: {x: {field: 'x', type: 'quantitative'}, y: {field: 'y', "
    "type: 'quantitative'}}})\n"
    "len(format(chart.render_spec(spec), 'xml'))\n";

static const char* kSharedModuleStressPdfPageCount =
    "import pdf: lambda.doc.pdf.pdf\n"
    "let doc = input('test/input/test.pdf', 'pdf') ^ { null }\n"
    "pdf.pdf_page_count(doc)\n";

static const char* kSharedModuleStressPdfContent =
    "import resolve: lambda.doc.pdf.resolve\n"
    "let doc = input('test/input/test.pdf', 'pdf') ^ { null }\n"
    "let page = resolve.page_at(doc, 0)\n"
    "len(resolve.page_content_bytes(doc, page))\n";

static SharedModuleStressCase shared_module_stress_cases[] = {
    {"chart-bar", kSharedModuleStressChartBar, NULL},
    {"chart-line", kSharedModuleStressChartLine, NULL},
    {"pdf-page-count", kSharedModuleStressPdfPageCount, NULL},
    {"pdf-content", kSharedModuleStressPdfContent, NULL},
};
static const int shared_module_stress_case_count =
    (int)(sizeof(shared_module_stress_cases) / sizeof(shared_module_stress_cases[0]));
static Runtime shared_module_stress_runtime = {};

static uint64_t shared_module_stress_now_ms(void) {
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

typedef struct SharedModuleStressGate {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int setup_turn;
    int ready_count;
    bool release_workers;
} SharedModuleStressGate;

typedef enum SharedModuleStressMode {
    SHARED_MODULE_STRESS_COLD_INSTANTIATE,
    SHARED_MODULE_STRESS_HOT_LOOP,
    SHARED_MODULE_STRESS_WARM_ONLY,
} SharedModuleStressMode;

typedef struct SharedModuleStressWorker {
    SharedModuleStressCase* cases;
    int case_count;
    int worker_id;
    SharedModuleStressGate* gate;
    SharedModuleStressMode mode;
    EvalContext eval;
    Pool* setup_pool;
    ArrayList* import_cone;
    ArrayList* visited_scripts;
    uint64_t evaluations;
    uint64_t started_ms;
    uint64_t stopped_ms;
    uintptr_t module_state_address;
    TypeId failed_result_type;
    int failed_error_code;
    bool module_uses_shared_consts;
    const char* failure;
} SharedModuleStressWorker;

extern "C" bool prepare_context_module_state(void* mir_ctx, void* consts,
                                               void* type_list);

static bool shared_module_stress_select_case(SharedModuleStressWorker* worker,
                                             SharedModuleStressCase* test_case) {
    if (!test_case || !test_case->script || !test_case->script->main_func ||
            !test_case->script->const_list) {
        return false;
    }
    worker->eval.consts = (void**)test_case->script->const_list->data;
    worker->eval.type_list = test_case->script->type_list;
    worker->eval.current_file = test_case->script->reference;
    return true;
}

static bool shared_module_stress_is_integral_result(Item result) {
    switch (get_type_id(result)) {
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
        return true;
    default:
        return false;
    }
}

static bool shared_module_stress_execute_case(SharedModuleStressWorker* worker,
                                              SharedModuleStressCase* test_case,
                                              int64_t* result_value) {
    if (!shared_module_stress_select_case(worker, test_case)) return false;
    worker->eval.result = ItemNull;
    Item result = test_case->script->main_func((Context*)&worker->eval);
    worker->eval.result = result;
    if (worker->eval.heap) worker->eval.heap->result_root = result.item;
    if (!shared_module_stress_is_integral_result(result) || worker->eval.last_error != NULL) {
        worker->failed_result_type = get_type_id(result);
        worker->failed_error_code = worker->eval.last_error ?
            worker->eval.last_error->code : 0;
        return false;
    }
    if (result_value) *result_value = 0;
    return true;
}

static bool shared_module_stress_collect_import_cone(SharedModuleStressWorker* worker,
                                                     Script* script, bool include_script) {
    if (!script || !script->main_func || !worker->import_cone ||
            !worker->visited_scripts) return false;
    for (int i = 0; i < worker->visited_scripts->length; i++) {
        if ((Script*)worker->visited_scripts->data[i] == script) return true;
    }
    // Mark before descending so circular imports match the runner's graph
    // walk rather than recursively re-entering the same sealed module.
    if (!arraylist_append(worker->visited_scripts, script)) return false;

    if (script->direct_imports) {
        for (int i = 0; i < script->direct_imports->length; i++) {
            Script* dependency = (Script*)script->direct_imports->data[i];
            if (!shared_module_stress_collect_import_cone(worker, dependency, true)) return false;
        }
    }
    return !include_script || arraylist_append(worker->import_cone, script) != 0;
}

static void shared_module_stress_record_module_state(SharedModuleStressWorker* worker,
                                                     SharedModuleStressCase* test_case) {
    for (uint32_t i = 0; i < worker->eval.module_state_capacity; i++) {
        LambdaModuleState* state = worker->eval.module_states[i];
        if (!state || state->consts != test_case->script->const_list->data) continue;
        worker->module_state_address = (uintptr_t)state;
        worker->module_uses_shared_consts = true;
        return;
    }
}

static bool shared_module_stress_init_worker(SharedModuleStressWorker* worker) {
    memset(&worker->eval, 0, sizeof(worker->eval));
    if (!eval_context_matches(&worker->eval)) {
        worker->failure = "eval thread ownership";
        return false;
    }
    input_context = (Context*)&worker->eval;
    lambda_stack_init();
    worker->eval.stack_limit = _lambda_stack_limit;
    if (!lambda_side_stack_bind()) return false;
    // Compiled scripts share their AST pool; allocating per-thread runtime
    // names from it races the pool allocator during concurrent setup.
    worker->setup_pool = mem_pool_create(NULL, MEM_ROLE_AST, "concurrency.worker");
    if (!worker->setup_pool) {
        worker->failure = "worker setup pool";
        return false;
    }
    worker->eval.pool = worker->setup_pool;
    worker->eval.runtime = &shared_module_stress_runtime;
    // Match runner setup: generated property keys require runtime NameIds,
    // not an idless AST name pool (D5.6.1).
    worker->eval.name_pool = name_pool_create_runtime(worker->setup_pool);
    heap_init();
    if (!worker->eval.heap) return false;
    worker->eval.pool = worker->eval.heap->pool;
    worker->eval.type_info = type_info;
    // PDF input resolves relative paths through cwd; a hand-built EvalContext
    // must establish the same context-owned URL as runner_setup_context().
    worker->eval.cwd = get_current_dir();
    worker->eval.decimal_ctx = decimal_fixed_context();
    worker->eval.context_alloc = heap_alloc;
    worker->eval.scheduler = lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
    worker->import_cone = arraylist_new(worker->case_count * 4);
    worker->visited_scripts = arraylist_new(worker->case_count * 4);
    if (!worker->eval.name_pool || !worker->eval.cwd || !worker->eval.scheduler ||
            !worker->import_cone || !worker->visited_scripts) {
        worker->failure = "context resource initialization";
        return false;
    }

    // Match the normal runner: initialize package imports, but leave each
    // selected test script for the evaluation phase below.
    for (int i = 0; i < worker->case_count; i++) {
        if (!shared_module_stress_collect_import_cone(worker, worker->cases[i].script, false)) {
            worker->failure = worker->cases[i].name;
            return false;
        }
    }
    for (int i = 0; i < worker->import_cone->length; i++) {
        Script* script = (Script*)worker->import_cone->data[i];
        if (script->jit_context && !prepare_context_module_state((void*)script->jit_context,
                script->const_list ? script->const_list->data : NULL, script->type_list)) {
            worker->failure = script->reference;
            return false;
        }
    }
    for (int i = 0; i < worker->case_count; i++) {
        Script* script = worker->cases[i].script;
        if (!script || !script->jit_context || !prepare_context_module_state(
                (void*)script->jit_context,
                script->const_list ? script->const_list->data : NULL, script->type_list)) {
            worker->failure = worker->cases[i].name;
            return false;
        }
    }
    for (int i = 0; i < worker->import_cone->length; i++) {
        Script* script = (Script*)worker->import_cone->data[i];
        worker->eval.consts = script->const_list ? (void**)script->const_list->data : NULL;
        worker->eval.type_list = script->type_list;
        worker->eval.current_file = script->reference;
        worker->eval.result = script->main_func((Context*)&worker->eval);
        if (worker->eval.heap) worker->eval.heap->result_root = worker->eval.result.item;
    }
    for (int i = 0; i < worker->case_count; i++) {
        if (!shared_module_stress_execute_case(worker, &worker->cases[i], NULL)) {
            worker->failure = worker->cases[i].name;
            return false;
        }
        if (i == 0) shared_module_stress_record_module_state(worker, &worker->cases[i]);
    }
    return true;
}

static void shared_module_stress_destroy_worker(SharedModuleStressWorker* worker) {
    if (!eval_context_matches(&worker->eval)) {
        worker->failure = "eval thread ownership during teardown";
        return;
    }
    input_context = (Context*)&worker->eval;
    if (worker->eval.scheduler) {
        lambda_scheduler_destroy(worker->eval.scheduler);
        worker->eval.scheduler = NULL;
    }
    lambda_module_state_destroy();
    if (worker->import_cone) {
        arraylist_free(worker->import_cone);
        worker->import_cone = NULL;
    }
    if (worker->visited_scripts) {
        arraylist_free(worker->visited_scripts);
        worker->visited_scripts = NULL;
    }
    if (worker->eval.name_pool) {
        name_pool_release(worker->eval.name_pool);
        worker->eval.name_pool = NULL;
    }
    if (worker->setup_pool) {
        pool_destroy(worker->setup_pool);
        worker->setup_pool = NULL;
    }
    if (worker->eval.heap) heap_destroy();
    if (worker->eval.cwd) {
        url_destroy(worker->eval.cwd);
        worker->eval.cwd = NULL;
    }
    lambda_stack_cleanup();
    input_context = NULL;
}

static void* shared_module_stress_worker_main(void* arg) {
    SharedModuleStressWorker* worker = (SharedModuleStressWorker*)arg;
    SharedModuleStressGate* gate = worker->gate;
    // Generated code and runtime helpers resolve allocation/context state
    // through TLS for the entire evaluation, not only during setup.
    if (!eval_context_init(&worker->eval)) {
        worker->failure = "eval thread initialization";
        return NULL;
    }
    input_context = (Context*)&worker->eval;
    worker->started_ms = shared_module_stress_now_ms();
    // The worker's budget starts at pthread entry.  Context construction is
    // deliberately included: the test must not hide setup cost behind a
    // separate, longer process-wide timing window.
    uint64_t deadline = worker->started_ms + 20000u;

    // Construction touches cold process/runtime setup; serialize it so the
    // following concurrent phase isolates shared-module execution correctness.
    pthread_mutex_lock(&gate->mutex);
    while (gate->setup_turn != worker->worker_id) {
        pthread_cond_wait(&gate->condition, &gate->mutex);
    }
    bool initialized = shared_module_stress_init_worker(worker);
    gate->setup_turn++;
    gate->ready_count++;
    pthread_cond_broadcast(&gate->condition);
    while (!gate->release_workers) {
        pthread_cond_wait(&gate->condition, &gate->mutex);
    }
    pthread_mutex_unlock(&gate->mutex);

    if (initialized) {
        uint64_t state = 0x9e3779b97f4a7c15ULL ^ (uint64_t)(worker->worker_id + 1);
        while (worker->mode == SHARED_MODULE_STRESS_HOT_LOOP &&
                shared_module_stress_now_ms() < deadline) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            SharedModuleStressCase* test_case = &worker->cases[
                (int)((state >> 32) % (uint64_t)worker->case_count)];
            if (!shared_module_stress_execute_case(worker, test_case, NULL)) {
                worker->failure = test_case->name;
                break;
            }
            worker->evaluations++;
        }
        if (worker->mode == SHARED_MODULE_STRESS_COLD_INSTANTIATE) {
            for (int i = 0; i < worker->case_count; i++) {
                if (!shared_module_stress_execute_case(worker, &worker->cases[i], NULL)) {
                    worker->failure = worker->cases[i].name;
                    break;
                }
                if (i == 0) shared_module_stress_record_module_state(worker,
                    &worker->cases[i]);
                worker->evaluations++;
            }
        }
    } else {
        if (!worker->failure) worker->failure = "context initialization";
    }
    shared_module_stress_destroy_worker(worker);
    if (!eval_context_shutdown(&worker->eval) && !worker->failure) {
        worker->failure = "eval thread shutdown";
    }
    worker->stopped_ms = shared_module_stress_now_ms();
    return NULL;
}

class RuntimeGlobalsConcurrency : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        runtime_init(&shared_module_stress_runtime);
        shared_module_stress_runtime.use_mir_direct = true;

        // Cache the shared package code before worker contexts exist.  Calling
        // load_script from a worker would test per-thread compilation instead
        // of shared immutable module execution.
        Script* chart_package = load_script_mir_direct(&shared_module_stress_runtime,
        "lmd/package/chart/chart.ls", NULL, true);
        Script* pdf_package = load_script_mir_direct(&shared_module_stress_runtime,
        "lmd/package/doc/pdf/pdf.ls", NULL, true);
        ASSERT_NE(chart_package, nullptr);
        ASSERT_NE(pdf_package, nullptr);
        for (int i = 0; i < shared_module_stress_case_count; i++) {
            shared_module_stress_cases[i].script = load_script_mir_direct(
                &shared_module_stress_runtime, shared_module_stress_cases[i].name,
                shared_module_stress_cases[i].source, false);
            ASSERT_NE(shared_module_stress_cases[i].script, nullptr)
                << shared_module_stress_cases[i].name;
            ASSERT_NE(shared_module_stress_cases[i].script->main_func, nullptr)
                << shared_module_stress_cases[i].name;
        }
    }

    static void TearDownTestSuite() {
        runtime_cleanup(&shared_module_stress_runtime);
        memset(&shared_module_stress_runtime, 0, sizeof(shared_module_stress_runtime));
    }
};

static bool shared_module_stress_run_workers(SharedModuleStressWorker* workers,
                                             int worker_count) {
    SharedModuleStressGate gate = {};
    if (pthread_mutex_init(&gate.mutex, NULL) != 0) return false;
    if (pthread_cond_init(&gate.condition, NULL) != 0) {
        pthread_mutex_destroy(&gate.mutex);
        return false;
    }
    pthread_t threads[4] = {};
    if (worker_count > (int)(sizeof(threads) / sizeof(threads[0]))) return false;
    pthread_attr_t attributes = {};
    if (pthread_attr_init(&attributes) != 0) return false;
    // Chart/PDF MIR entrypoints have deep native frames; the platform default
    // worker stack is too small and faults in chkstk before Lambda runs.
    if (pthread_attr_setstacksize(&attributes, 8u * 1024u * 1024u) != 0) {
        pthread_attr_destroy(&attributes);
        return false;
    }

    for (int i = 0; i < worker_count; i++) {
        workers[i].cases = shared_module_stress_cases;
        workers[i].case_count = shared_module_stress_case_count;
        workers[i].worker_id = i;
        workers[i].gate = &gate;
        if (pthread_create(&threads[i], &attributes, shared_module_stress_worker_main,
                &workers[i]) != 0) {
            pthread_attr_destroy(&attributes);
            return false;
        }
    }
    pthread_attr_destroy(&attributes);

    pthread_mutex_lock(&gate.mutex);
    while (gate.ready_count != worker_count) {
        pthread_cond_wait(&gate.condition, &gate.mutex);
    }
    gate.release_workers = true;
    pthread_cond_broadcast(&gate.condition);
    pthread_mutex_unlock(&gate.mutex);

    for (int i = 0; i < worker_count; i++) {
        if (pthread_join(threads[i], NULL) != 0) return false;
    }

    pthread_cond_destroy(&gate.condition);
    pthread_mutex_destroy(&gate.mutex);
    return true;
}

TEST_F(RuntimeGlobalsConcurrency, SharedChartAndPdfModulesStayStableAcrossEvalThreads) {
    constexpr int worker_count = 4;
    SharedModuleStressWorker workers[worker_count] = {};
    for (int i = 0; i < worker_count; i++) {
        workers[i].mode = SHARED_MODULE_STRESS_HOT_LOOP;
    }

    ASSERT_TRUE(shared_module_stress_run_workers(workers, worker_count));
    for (int i = 0; i < worker_count; i++) {
        EXPECT_STREQ(workers[i].failure, nullptr) << "worker " << i
            << " result_type=" << workers[i].failed_result_type
            << " error=" << workers[i].failed_error_code;
        EXPECT_GT(workers[i].evaluations, 0u) << "worker " << i;
        EXPECT_GE(workers[i].stopped_ms - workers[i].started_ms, 20000u)
            << "worker " << i << " must run for its own full 20-second budget";
    }
}

TEST_F(RuntimeGlobalsConcurrency, ModuleStateSlabsArePrivateToEachEvalContext) {
    constexpr int worker_count = 4;
    SharedModuleStressWorker workers[worker_count] = {};
    for (int i = 0; i < worker_count; i++) {
        workers[i].mode = SHARED_MODULE_STRESS_WARM_ONLY;
    }

    ASSERT_TRUE(shared_module_stress_run_workers(workers, worker_count));
    for (int i = 0; i < worker_count; i++) {
        EXPECT_STREQ(workers[i].failure, nullptr) << "worker " << i
            << " result_type=" << workers[i].failed_result_type
            << " error=" << workers[i].failed_error_code;
        EXPECT_TRUE(workers[i].module_uses_shared_consts) << "worker " << i;
        EXPECT_NE(workers[i].module_state_address, 0u) << "worker " << i;
    }
    for (int i = 0; i < worker_count; i++) {
        for (int j = i + 1; j < worker_count; j++) {
            EXPECT_NE(workers[i].module_state_address, workers[j].module_state_address);
        }
    }
}

#else

TEST(RuntimeGlobalsConcurrency, SharedChartAndPdfModulesStayStableAcrossEvalThreads) {
    GTEST_SKIP() << "the native concurrency harness requires pthread support";
}

#endif

#ifndef _WIN32
struct DryRunOwnerWorker {
    Item existing_file;
    bool dry_run;
    bool passed;
};

static void* dry_run_owner_worker(void* opaque) {
    DryRunOwnerWorker* worker = (DryRunOwnerWorker*)opaque;
    Runtime runtime = {};
    runtime.dry_run = worker->dry_run;
    EvalContext owner = {};
    owner.runtime = &runtime;
    if (!eval_context_init(&owner)) return nullptr;
    worker->passed = true;
    // each thread changes its own configuration while its peer performs real I/O.
    for (int i = 0; i < 128; i++) {
        runtime.dry_run = worker->dry_run != ((i & 1) != 0);
        if (lambda_dry_run_enabled() != runtime.dry_run ||
                fn_exists(worker->existing_file) != (runtime.dry_run ? BOOL_FALSE : BOOL_TRUE)) {
            worker->passed = false;
            break;
        }
    }
    worker->passed &= eval_context_shutdown(&owner);
    return nullptr;
}

TEST(LambdaRunConfiguration, DryRunBelongsToEachActiveOwnerIncludingNativeIo) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder builder(input);
    Item path = builder.createStringItem("./AGENTS.md");
    DryRunOwnerWorker workers[2] = {{path, false, false}, {path, true, false}};
    pthread_t threads[2];
    int launched = 0;
    for (; launched < 2; launched++) {
        if (pthread_create(&threads[launched], nullptr, dry_run_owner_worker, &workers[launched])) break;
    }
    for (int i = 0; i < launched; i++) EXPECT_EQ(pthread_join(threads[i], nullptr), 0);
    ASSERT_EQ(launched, 2);
    EXPECT_TRUE(workers[0].passed);
    EXPECT_TRUE(workers[1].passed);
}
#endif
