#include <gtest/gtest.h>

#include "../lib/file.h"
#include "../lib/strbuf.h"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/module_registry.h"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_interp.hpp"
#include "../lambda/js/js_function.hpp"
#include "../lambda/js/js_property_attrs.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/js/js_mir_internal.hpp"
#include "../lambda/runtime/sys_func_registry.h"
#include "../lambda/input/input-script-cache.h"
#include "../lambda/mir/mir.h"
#include "../lib/mem.h"

#include <pthread.h>
#include <sched.h>

TEST(JsModuleResolution, ResolvesHttpModuleSpecifiersAsUrls) {
    char resolved[256];
    const char* base = "https://docs.example.test/vite/assets/main.js";

    jm_resolve_module_path(base, "./chunk.js", 10, resolved, sizeof(resolved));
    EXPECT_STREQ(resolved, "https://docs.example.test/vite/assets/chunk.js");

    jm_resolve_module_path(base, "../shared.js", 12, resolved, sizeof(resolved));
    EXPECT_STREQ(resolved, "https://docs.example.test/vite/shared.js");

    jm_resolve_module_path(base, "/vite/assets/root.js", 20, resolved, sizeof(resolved));
    EXPECT_STREQ(resolved, "https://docs.example.test/vite/assets/root.js");
}

TEST(JsModuleResolution, ClassifiesHttpModuleSourcesForUrlCacheEntries) {
    EXPECT_TRUE(js_path_is_http_url("https://docs.example.test/vite/main.js"));
    EXPECT_TRUE(js_path_is_http_url("http://docs.example.test/vite/main.js"));
    EXPECT_FALSE(js_path_is_http_url("/vite/main.js"));
    EXPECT_FALSE(js_path_is_http_url("main.js"));
}

TEST(JsCallableDefinitions, SharesAstDefinitionWithoutSharingCaptures) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] = "function classFactory(x) { return class { value = x; }; } "
        "var A = classFactory(3); var B = classFactory(4); "
        "var av = new A(); var bv = new B(); "
        "function make(x) { return function() { return x; }; } "
        "var first = make(1); var second = make(2); first;";
    Item first = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "definitions.js", NULL);
    ASSERT_EQ(get_type_id(first), LMD_TYPE_FUNC);
    const char read_second[] = "second;";
    Item second = js_interp_execute_source(&runtime, read_second, sizeof(read_second) - 1,
        "read-definition.js", NULL);
    ASSERT_EQ(get_type_id(second), LMD_TYPE_FUNC);
    JsFunction* a = (JsFunction*)first.function;
    JsFunction* b = (JsFunction*)second.function;
    EXPECT_NE(a, b);
    EXPECT_EQ((void*)a, (void*)static_cast<Function*>(a));
    EXPECT_TRUE(function_has_abi(static_cast<Function*>(a),
        FN_ENTRY_ABI_JS_FUNCTION));
    EXPECT_EQ(a->code, b->code);
    EXPECT_EQ(js_fn_ast_definition(a), js_fn_ast_definition(b));
    EXPECT_EQ(a->code->definition, js_fn_ast_definition(a)->definition);
    EXPECT_EQ(a->code->definition_module,
        js_fn_ast_definition(a)->definition_module);
    EXPECT_EQ(a->code->param_count, js_fn_param_count(a));
    EXPECT_NE(js_fn_ast(a)->env, js_fn_ast(b)->env);
    EXPECT_EQ(js_call_function(first, ItemNull, NULL, 0).item, flt2it(1.0).item);
    EXPECT_EQ(js_call_function(second, ItemNull, NULL, 0).item, flt2it(2.0).item);
    JsScript* script = (JsScript*)runtime.scripts->data[0];
    EXPECT_EQ(hashmap_count(script->field_initializers), 1u);
    const char fields[] = "av.value + bv.value;";
    EXPECT_EQ(js_interp_execute_source(&runtime, fields, sizeof(fields) - 1,
        "read-fields.js", NULL).item, flt2it(7.0).item);
    runtime_cleanup(&runtime);
}

TEST(JsCallableDefinitions, CachesParameterShapeAndFormalLength) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] =
        "function mapped(first, second) { first = 40; return arguments[0]; } "
        "function factory(default_value) { return function(first, second = default_value, ...rest) "
        "{ return first + second + rest.length; }; } "
        "var first = factory(40); var second = factory(41); "
        "class Box { constructor(first, second = 2, ...rest) {} } "
        "[mapped(1, 2), first.length, Box.length];";
    Item lengths = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "cached-parameter-shape.js", NULL);

    ASSERT_FALSE(item_is_error(lengths));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(lengths, 0), flt2it(40.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(lengths, 1), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(lengths, 2), flt2it(1.0)).item,
        b2it(true));

    const char read_first[] = "first;";
    Item first = js_interp_execute_source(&runtime, read_first, sizeof(read_first) - 1,
        "read-cached-first.js", NULL);
    const char read_second[] = "second;";
    Item second = js_interp_execute_source(&runtime, read_second, sizeof(read_second) - 1,
        "read-cached-second.js", NULL);
    const char read_mapped[] = "mapped;";
    Item mapped = js_interp_execute_source(&runtime, read_mapped, sizeof(read_mapped) - 1,
        "read-cached-mapped.js", NULL);

    ASSERT_EQ(get_type_id(first), LMD_TYPE_FUNC);
    ASSERT_EQ(get_type_id(second), LMD_TYPE_FUNC);
    ASSERT_EQ(get_type_id(mapped), LMD_TYPE_FUNC);
    JsFunction* first_function = (JsFunction*)first.function;
    JsFunction* second_function = (JsFunction*)second.function;
    JsFunction* mapped_function = (JsFunction*)mapped.function;
    ASSERT_EQ(first_function->code, second_function->code);
    EXPECT_EQ(first_function->code->param_count, 3);
    EXPECT_EQ(first_function->code->formal_length, 1);
    EXPECT_TRUE(first_function->code->has_non_simple_params);
    EXPECT_FALSE(js_fn_ast_has_simple_params(first_function));
    EXPECT_EQ(mapped_function->code->param_count, 2);
    EXPECT_EQ(mapped_function->code->formal_length, 2);
    EXPECT_FALSE(mapped_function->code->has_non_simple_params);
    EXPECT_TRUE(js_fn_ast_has_simple_params(mapped_function));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ElidesProvenEmptyFunctionEnvironment) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] =
        "function outer() { return function() { return 7; }; } "
        "function observesArguments() { return arguments.length; } "
        "function ownsLocal() { let value = 1; return value; } "
        "function captures(value) { function empty() { "
        "return function() { return value; }; } return empty(); } "
        "var captured = captures(8); "
        "outer;";
    Item outer_item = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "elided-function-environment.js", NULL);
    ASSERT_EQ(get_type_id(outer_item), LMD_TYPE_FUNC);

    const char arguments_source[] = "observesArguments;";
    Item arguments_item = js_interp_execute_source(&runtime, arguments_source,
        sizeof(arguments_source) - 1, "read-observes-arguments.js", NULL);
    const char local_source[] = "ownsLocal;";
    Item local_item = js_interp_execute_source(&runtime, local_source,
        sizeof(local_source) - 1, "read-owns-local.js", NULL);
    const char captured_source[] = "captured;";
    Item captured_item = js_interp_execute_source(&runtime, captured_source,
        sizeof(captured_source) - 1, "read-captured.js", NULL);
    ASSERT_EQ(get_type_id(arguments_item), LMD_TYPE_FUNC);
    ASSERT_EQ(get_type_id(local_item), LMD_TYPE_FUNC);
    ASSERT_EQ(get_type_id(captured_item), LMD_TYPE_FUNC);

    JsFunction* outer = (JsFunction*)outer_item.function;
    EXPECT_TRUE(js_fn_ast_elides_function_environment(outer));
    EXPECT_FALSE(js_fn_ast_elides_function_environment(
        (JsFunction*)arguments_item.function));
    EXPECT_FALSE(js_fn_ast_elides_function_environment(
        (JsFunction*)local_item.function));

    Item escaped = js_call_function(outer_item, make_js_undefined(), NULL, 0);
    ASSERT_EQ(get_type_id(escaped), LMD_TYPE_FUNC);
    // The escaped closure received the outer closure environment, not a
    // short-lived empty record from the call that created it.
    EXPECT_EQ(js_fn_ast((JsFunction*)escaped.function)->env, nullptr);
    EXPECT_EQ(js_call_function(escaped, make_js_undefined(), NULL, 0).item,
        flt2it(7.0).item);
    {
        PersistentRooted<Item> captured_root(captured_item);
        ASSERT_TRUE(captured_root.valid());
        heap_gc_collect();
        // `empty` has no own bindings, but its escaped child must keep the
        // surrounding `captures` environment rather than a popped call record.
        EXPECT_EQ(js_call_function(captured_root.get(), make_js_undefined(), NULL, 0).item,
            flt2it(8.0).item);
    }
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PlansImmutableCallArgumentShapeAtBuild) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] = "callee(1, 2); callee(...values);";
    JsScript* script = js_interp_prepare_script(&runtime, source, sizeof(source) - 1,
        "planned-call-shape.js", false);

    ASSERT_NE(script, nullptr);
    JsProgramNode* program = (JsProgramNode*)script->ast_root;
    ASSERT_NE(program, nullptr);
    ASSERT_NE(program->body, nullptr);
    ASSERT_EQ(program->body->node_type, AST_NODE_EXPR_STMT);
    JsExpressionStatementNode* first_statement =
        (JsExpressionStatementNode*)program->body;
    ASSERT_EQ(first_statement->expression->node_type, AST_NODE_CALL_EXPR);
    JsCallNode* plain_call = (JsCallNode*)first_statement->expression;
    ASSERT_NE(program->body->next, nullptr);
    ASSERT_EQ(program->body->next->node_type, AST_NODE_EXPR_STMT);
    JsExpressionStatementNode* second_statement =
        (JsExpressionStatementNode*)program->body->next;
    ASSERT_EQ(second_statement->expression->node_type, AST_NODE_CALL_EXPR);
    JsCallNode* spread_call = (JsCallNode*)second_statement->expression;

    EXPECT_TRUE(plain_call->interp_call_shape_planned);
    EXPECT_EQ(plain_call->interp_source_argc, 2u);
    EXPECT_FALSE(plain_call->interp_has_spread_args);
    EXPECT_TRUE(spread_call->interp_call_shape_planned);
    EXPECT_EQ(spread_call->interp_source_argc, 1u);
    EXPECT_TRUE(spread_call->interp_has_spread_args);

    runtime_cleanup(&runtime);
}

static uint64_t js_test_callable_target(Context*, uint64_t value) { return value; }

TEST(JsCallableDefinitions, LiveMirValuesSurviveWeakTableTeardown) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] = "0;";
    js_interp_execute_source(&runtime, source, sizeof(source) - 1, "code-owner.js", NULL);
    {
        RootFrame roots(2);
        Rooted<Item> first(roots, js_new_distinct_function_mir((void*)js_test_callable_target, 1));
        Rooted<Item> second(roots, js_new_distinct_function_mir((void*)js_test_callable_target, 1));
        ASSERT_EQ(get_type_id(first.get()), LMD_TYPE_FUNC);
        ASSERT_EQ(get_type_id(second.get()), LMD_TYPE_FUNC);
        JsFunction* a = (JsFunction*)first.get().function;
        JsFunction* b = (JsFunction*)second.get().function;
        EXPECT_NE(a, b);
        ASSERT_EQ(a->code, b->code);
        EXPECT_EQ(a->code->intern_refcount, 2u);
        JsRuntimeState* state = js_runtime_state_for(runtime.eval_context);
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(hashmap_count(state->callable_code_interned), 1u);
        // surviving values retain the record after its weak owner disappears.
        js_callable_code_table_destroy(state->callable_code_interned);
        state->callable_code_interned = NULL;
        EXPECT_EQ(a->code->intern_table, nullptr);
        EXPECT_EQ(js_fn_param_count(a), 1);
    }
    runtime_cleanup(&runtime);
}

TEST(JsRuntimeResources, SharesOneContextCapsuleAcrossHostOwners) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] = "0;";
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, source,
        sizeof(source) - 1, "resource-capsule.js", NULL)));

    EvalContext* owner = runtime.eval_context;
    ASSERT_NE(owner, nullptr);
    RuntimeResourceTable* resources = js_runtime_resource_table();
    ASSERT_NE(resources, nullptr);
    EXPECT_EQ(resources, context_capsule(owner,
        CONTEXT_CAPSULE_RUNTIME_RESOURCES));

    {
        RootFrame roots(1);
        Rooted<Item> resource_owner(roots, js_new_object());
        const RuntimeResourceDescriptor* descriptor =
            runtime_resource_descriptor_from_legacy_name("timer");
        ASSERT_NE(descriptor, nullptr);
        uint32_t id = runtime_resource_table_add(resources, resource_owner.get(),
            descriptor, NULL, NULL, true);
        ASSERT_NE(id, 0u);
        EXPECT_EQ(runtime_resource_table_active_count(resources), 1);
        EXPECT_EQ(runtime_resource_table_value(resources,
            runtime_resource_table_entry(resources, id)).item,
            resource_owner.get().item);
        runtime_resource_table_remove(resources, id);
        EXPECT_EQ(runtime_resource_table_active_count(resources), 0);
    }

    runtime_cleanup(&runtime);
}

TEST(JsScalarAnalysis, ResultFactsDoNotWeakenCallEffects) {
    JitImportMetadata metadata = {};
    ASSERT_TRUE(jit_import_get_metadata("js_add", &metadata));
    EXPECT_EQ(jit_import_scalar_return_class(&metadata), SCALAR_RETURN_F64);
    EXPECT_EQ(metadata.gc_effect, JIT_EFFECT_MAY_GC);
    EXPECT_EQ(metadata.reentry_effect, JIT_REENTRY_YES);
    EXPECT_EQ(metadata.exception_effect, JIT_EXCEPTION_MAY_SET);
    EXPECT_EQ(metadata.flags & JIT_IMPORT_NUMBER_STACK_PRESERVES, 0u);
    ASSERT_TRUE(jit_import_get_metadata("js_subtract", &metadata));
    EXPECT_NE(metadata.flags & JIT_IMPORT_RESULT_CALLER_OWNED, 0u);
    EXPECT_EQ(metadata.flags & JIT_IMPORT_NUMBER_STACK_PRESERVES, 0u);
    metadata = {};
    metadata.ret_class = JIT_VALUE_BOXED_ITEM;
    EXPECT_EQ(jit_import_scalar_return_class(&metadata), SCALAR_RETURN_DYNAMIC);
    EXPECT_EQ(jit_scalar_return_class_for_type(LMD_TYPE_ANY), SCALAR_RETURN_DYNAMIC);
    EXPECT_EQ(jit_scalar_return_class_for_type(LMD_TYPE_FLOAT), SCALAR_RETURN_F64);
    EXPECT_EQ(jit_scalar_return_class_for_type(LMD_TYPE_INT64), SCALAR_RETURN_I64);
    EXPECT_EQ(jit_scalar_return_class_for_type(LMD_TYPE_STRING), SCALAR_RETURN_NONE);
}

static Item js_test_async_close_returns_undefined() {
    return make_js_undefined();
}

TEST(JsIteratorClose, RawAsyncCloseDistinguishesAbsentReturnFromUndefined) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char bootstrap_source[] = "0;";
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime,
        bootstrap_source, sizeof(bootstrap_source) - 1,
        "iterator-close-unit-bootstrap.js", NULL)));

    {
        RootFrame roots(4);
        Rooted<Item> absent_iterator(roots, js_new_object());
        Rooted<Item> callable_iterator(roots, js_new_object());
        Rooted<Item> return_fn(roots,
            js_new_native_function(js_test_async_close_returns_undefined));
        Rooted<Item> return_key(roots, js_name_item("return", 6));
        ASSERT_FALSE(item_is_error(js_set_key_default(callable_iterator.get(),
            return_key.get(), return_fn.get())));

        Item absent_result = js_async_iterator_close_result(absent_iterator.get());
        ASSERT_FALSE(item_is_error(absent_result));
        EXPECT_FALSE(js_async_iterator_close_needs_await(absent_result));

        Item undefined_result = js_async_iterator_close_result(callable_iterator.get());
        ASSERT_FALSE(item_is_error(undefined_result));
        EXPECT_TRUE(js_async_iterator_close_needs_await(undefined_result));
        EXPECT_EQ(undefined_result.item, make_js_undefined().item);
    }

    runtime_cleanup(&runtime);
}

TEST(JsAstStructure, ExtensionChildCatalogIsComplete) {
    EXPECT_TRUE(js_ast_child_catalog_complete());
}

TEST(JsDirectScope, IndexesLargeUnicodeBindingsWithStableSlots) {
    const int binding_count = 768;
    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    for (int index = 0; index < binding_count; index++) {
        strbuf_append_str(source, "var ");
        uint32_t codepoint = 0x4E00u + (uint32_t)index;
        if ((index & 1) == 0) {
            ASSERT_TRUE(strbuf_append_utf8(source, codepoint));
        } else {
            strbuf_append_format(source, "\\u%04X", (unsigned int)codepoint);
        }
        strbuf_append_str(source, " = ");
        strbuf_append_int(source, index);
        strbuf_append_str(source, ";\n");
    }

    JsTranspiler* transpiler = js_transpiler_create(NULL);
    ASSERT_NE(transpiler, nullptr);
    ASSERT_TRUE(js_transpiler_parse_c(transpiler, source->str, source->length,
        JS_PARSE_AUTO));
    ASSERT_NE(transpiler->global_scope, nullptr);
    EXPECT_TRUE(transpiler->global_scope->binding_slots_planned);
    EXPECT_EQ(transpiler->global_scope->binding_slot_count,
        (uint32_t)binding_count);

    NameEntry* binding = transpiler->global_scope->first;
    for (int index = 0; index < binding_count; index++) {
        ASSERT_NE(binding, nullptr);
        EXPECT_EQ(binding->slot, index);
        if (index == 0 || index == binding_count / 2 ||
                index == binding_count - 1) {
            EXPECT_EQ(js_scope_lookup(transpiler, binding->name), binding);
        }
        binding = binding->next;
    }
    EXPECT_EQ(binding, nullptr);

    js_transpiler_destroy(transpiler);
    strbuf_free(source);
}

static bool js_test262_append_file(StrBuf* source, const char* path) {
    char* contents = read_text_file(path);
    if (!contents) return false;
    strbuf_append_str(source, contents);
    strbuf_append_char(source, '\n');
    free(contents);
    return true;
}

TEST(JsScriptOwnership, AdoptsCommonScriptPrefixIntoRuntimeCatalog) {
    const char source[] = "let retained = 41; retained + 1;";
    JsTranspiler* tp = js_transpiler_create(NULL);
    ASSERT_NE(tp, nullptr);
    ASSERT_TRUE(js_transpiler_parse_c(tp, source, sizeof(source) - 1,
        JS_PARSE_AUTO));

    JsAstNode* ast = tp->ast_root;
    ASSERT_NE(ast, nullptr);

    Pool* ast_pool = tp->pool;
    NamePool* static_names = tp->name_pool;
    AstNode* ast_root = tp->ast_root;
    ASSERT_NE(ast_pool, nullptr);
    ASSERT_NE(static_names, nullptr);
    ASSERT_NE(ast_root, nullptr);

    Runtime runtime = {};
    runtime.scripts = arraylist_new(2);
    ASSERT_NE(runtime.scripts, nullptr);

    JsScript* script = js_script_adopt_transpiler(tp, &runtime, "owner.js");
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->profile, &js_profile);
    EXPECT_EQ(script->pool, ast_pool);
    EXPECT_EQ(script->name_pool, static_names);
    EXPECT_EQ(script->ast_root, ast_root);
    EXPECT_EQ(script->source_length, sizeof(source) - 1);
    EXPECT_NE(script->source, source);
    EXPECT_STREQ(script->source, source);
    EXPECT_STREQ(script->reference, "owner.js");
    EXPECT_EQ(script->index, 0);
    EXPECT_EQ(script->module_state_id, 0u);
    ASSERT_EQ(runtime.scripts->length, 1);
    EXPECT_EQ(runtime.scripts->data[0], (Script*)script);

    // Runtime teardown owns the adopted Script once it has been catalogued.
    runtime_free_all_scripts(&runtime);
}

TEST(JsScriptOwnership, IndexesFunctionParentsStructurallyInTopLevelAwaitModule) {
    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/assert.js"));
    uint32_t test_start = source->length;
    ASSERT_TRUE(js_test262_append_file(source,
        "ref/test262/test/language/expressions/class/"
        "cpn-class-expr-computed-property-name-from-await-expression.js"));

    JsTranspiler* tp = js_transpiler_create(NULL);
    ASSERT_NE(tp, nullptr);
    ASSERT_TRUE(js_transpiler_parse_c(tp, source->str, source->length,
        JS_PARSE_MODULE));

    uint32_t target_function_count = 0;
    for (uint32_t i = 0; i < tp->ast_index.function_count; i++) {
        AstFunctionIndexEntry* function = &tp->ast_index.functions[i];
        if (function->node->source_span.start_byte < test_start) continue;
        EXPECT_EQ(function->parent, AST_FUNCTION_ID_INVALID);
        target_function_count++;
    }
    EXPECT_EQ(target_function_count, 2u);
    js_transpiler_destroy(tp);
    strbuf_free(source);
}

TEST(JsInterpreter, ExecutesThroughSharedRuntimeAndModuleState) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "let base = 40; var answer = base + 2; answer;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "interpreter.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, flt2it(42.0).item);
    ASSERT_EQ(runtime.scripts->length, 1);
    EXPECT_EQ(((Script*)runtime.scripts->data[0])->profile, &js_profile);
    EXPECT_EQ(runtime.eval_context->runtime, &runtime);
    EXPECT_EQ(runtime.eval_context->active_module_state->module_id,
        ((Script*)runtime.scripts->data[0])->module_state_id);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesMutableClosuresOnTheSharedHeap) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function makeCounter(start) { let value = start; "
        "return function(step) { value += step; return value; }; } "
        "var counter = makeCounter(40); counter(1); counter(1);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "closure.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, flt2it(42.0).item);

    JsScript* script = (JsScript*)runtime.scripts->data[0];
    NameEntry* counter_entry = nullptr;
    for (NameEntry* entry = script->global_scope->first; entry; entry = entry->next) {
        if (entry->name && entry->name->len == 7 &&
                memcmp(entry->name->chars, "counter", 7) == 0) {
            counter_entry = entry;
            break;
        }
    }
    ASSERT_NE(counter_entry, nullptr);
    Item counter = lambda_active_module_var_at((uint32_t)counter_entry->slot);
    heap_gc_collect();
    Item increment = flt2it(2.0);
    Item after_gc = js_call_function(counter, make_js_undefined(), &increment, 1);
    ASSERT_FALSE(item_is_error(after_gc));
    EXPECT_EQ(js_strict_equal(after_gc, flt2it(44.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainsIntrinsicPrototypeCacheAcrossCollectionAndMutation) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char initialize[] =
        "Array.prototype.p1IntrinsicCacheProbe = 41; Array.prototype;";
    Item result = js_interp_execute_source(&runtime, initialize,
        sizeof(initialize) - 1, "intrinsic-cache.js", NULL);
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);

    {
        PersistentRooted<Item> cached_root(
            js_get_intrinsic_prototype_for_class(JS_CLASS_ARRAY));
        ASSERT_TRUE(cached_root.valid());
        ASSERT_EQ(get_type_id(cached_root.get()), LMD_TYPE_MAP);
        heap_gc_collect();
        EXPECT_EQ(cached_root.get().item,
            js_get_intrinsic_prototype_for_class(JS_CLASS_ARRAY).item);

        const char mutate[] = "Array.prototype.p1IntrinsicCacheProbe = 42;";
        ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, mutate,
            sizeof(mutate) - 1, "intrinsic-cache-mutation.js", NULL)));
        PersistentRooted<Item> refreshed_root(
            js_get_intrinsic_prototype_for_class(JS_CLASS_ARRAY));
        ASSERT_TRUE(refreshed_root.valid());
        EXPECT_EQ(refreshed_root.get().item, cached_root.get().item);
        EXPECT_EQ(js_get_key_default(refreshed_root.get(),
            js_name_item("p1IntrinsicCacheProbe", 21)).item, flt2it(42.0).item);
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExplicitAstSelectorUsesTheSharedScriptPath) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);

    const char source[] = "var answer = 6 * 7; answer;";
    Item result = transpile_js_to_mir(&runtime, source, "selector.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, flt2it(42.0).item);
    ASSERT_EQ(runtime.scripts->length, 1);
    EXPECT_EQ(((Script*)runtime.scripts->data[0])->profile, &js_profile);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AutoPromotesClosedHotFunctionToMirSatellite) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "auto", 1), 0);
    ASSERT_EQ(setenv("JS_JIT_THRESHOLD", "2", 1), 0);

    const char source[] =
        "function squareSum(limit) { "
        "  var total = 0; "
        "  for (var index = 0; index < limit; index = index + 1) { "
        "    total = total + index * index; "
        "  } "
        "  return total; "
        "} "
        "squareSum(3); squareSum(4); squareSum;";
    Item function_item = transpile_js_to_mir(&runtime, source,
        "p2-satellite.js", NULL);

    ASSERT_EQ(unsetenv("JS_JIT_THRESHOLD"), 0);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_EQ(get_type_id(function_item), LMD_TYPE_FUNC);
    JsFunction* function = (JsFunction*)function_item.function;
    ASSERT_EQ(js_fn_body_kind(function), JS_FUNCTION_BODY_CODE);
    ASSERT_EQ(function->code->p2_promotion.state, FN_PROMOTION_COMPILED);
    ASSERT_NE(js_function_get_ptr(function_item), nullptr);

    Item limit = flt2it(5.0);
    Item result = js_call_function(function_item, make_js_undefined(), &limit, 1);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(30.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ReusesCommonAstCacheAcrossFreshRuntimes) {
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);

    const char source[] = "var answer = 21 * 2; answer;";
    Runtime first_runtime = {};
    runtime_init(&first_runtime);
    Item first = transpile_js_to_mir(&first_runtime, source,
        "<common-ast-cache>", NULL);
    ASSERT_FALSE(item_is_error(first));
    EXPECT_EQ(first.item, flt2it(42.0).item);
    ASSERT_NE(first_runtime.scripts, nullptr);
    JsScript* first_template = (JsScript*)first_runtime.scripts->data[0];
    ASSERT_TRUE(first_template->cache_owned_template);
    ASSERT_NE(first_template->cache_compilation_unit_id, 0u);
    runtime_cleanup(&first_runtime);

    Runtime second_runtime = {};
    runtime_init(&second_runtime);
    Item second = transpile_js_to_mir(&second_runtime, source,
        "<common-ast-cache>", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(second));
    EXPECT_EQ(second.item, flt2it(42.0).item);
    ASSERT_NE(second_runtime.scripts, nullptr);
    JsScript* second_instance = (JsScript*)second_runtime.scripts->data[0];
    EXPECT_EQ(second_instance->cache_template, (Script*)first_template);
    EXPECT_EQ(second_instance->cache_compilation_unit_id,
        first_template->cache_compilation_unit_id);
    uint32_t dense_module_id = UINT32_MAX;
    EXPECT_TRUE(runtime_module_state_id_for_unit(&second_runtime,
        second_instance->cache_compilation_unit_id, &dense_module_id));
    EXPECT_EQ(dense_module_id, second_instance->module_state_id);
    runtime_cleanup(&second_runtime);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_EQ(after.ast_builds, before.ast_builds + 1);
    EXPECT_EQ(after.ast_hits, before.ast_hits + 1);
    EXPECT_EQ(after.module_hits, before.module_hits + 1);
}

TEST(JsInterpreter, AutoPrebuildsStaticImportClosureAsAst) {
    ASSERT_EQ(file_ensure_dir("temp"), 0);
    static int prebuild_generation = 0;
    int generation = ++prebuild_generation;
    char root_path[128];
    char left_path[128];
    char right_path[128];
    char root_source[512];
    snprintf(root_path, sizeof(root_path),
        "temp/js-ast-prebuild-root-%d.mjs", generation);
    snprintf(left_path, sizeof(left_path),
        "temp/js-ast-prebuild-left-%d.mjs", generation);
    snprintf(right_path, sizeof(right_path),
        "temp/js-ast-prebuild-right-%d.mjs", generation);
    snprintf(root_source, sizeof(root_source),
        "import { left } from \"./js-ast-prebuild-left-%d.mjs\";\n"
        "import { right } from \"./js-ast-prebuild-right-%d.mjs\";\n"
        "export const answer = left + right;\n", generation, generation);
    const char left_source[] = "export const left = 19;\n";
    const char right_source[] = "export const right = 23;\n";
    ASSERT_EQ(write_binary_file(root_path, root_source, strlen(root_source)), 0);
    ASSERT_EQ(write_binary_file(left_path, left_source, sizeof(left_source) - 1), 0);
    ASSERT_EQ(write_binary_file(right_path, right_source, sizeof(right_source) - 1), 0);

    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "auto", 1), 0);
    ASSERT_EQ(setenv("LAMBDA_MODULE_AST_THREADS", "2", 1), 0);

    Runtime runtime = {};
    runtime_init(&runtime);
    Item namespace_obj = load_js_module(&runtime, root_path);

    ASSERT_EQ(unsetenv("LAMBDA_MODULE_AST_THREADS"), 0);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_get_key_default(namespace_obj, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&runtime);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.ast_builds, before.ast_builds + 2);
    EXPECT_GE(after.ast_hits, before.ast_hits + 2);
    EXPECT_EQ(after.mir_builds, before.mir_builds);
}

TEST(RuntimePrebuild, CleansWorkerModuleRegistry) {
    Runtime worker = {};
    runtime_init(&worker);
    module_register_for_runtime(&worker, "temp/prebuild-worker-cleanup.ls",
        "lambda", ItemNull, NULL);
    ASSERT_NE(module_registry_first_for_runtime(&worker), nullptr);

    runtime_cleanup_ast_prebuild_worker(&worker);

    EXPECT_EQ(module_registry_first_for_runtime(&worker), nullptr);
    EXPECT_EQ(worker.scripts, nullptr);
    EXPECT_EQ(worker.loaded_script_index, nullptr);
}

TEST(JsJubeRuntime, DropsPrototypeRootsAcrossFreshRuntimes) {
    const char source[] = "hostobjDemo.create(40).bump(2);";

    Runtime first_runtime = {};
    runtime_init(&first_runtime);
    Item first = js_interp_execute_source(&first_runtime, source, sizeof(source) - 1,
        "jube-prototype-first.js", NULL);
    ASSERT_FALSE(item_is_error(first));
    runtime_cleanup(&first_runtime);

    Runtime second_runtime = {};
    runtime_init(&second_runtime);
    Item second = js_interp_execute_source(&second_runtime, source, sizeof(source) - 1,
        "jube-prototype-second.js", NULL);
    ASSERT_FALSE(item_is_error(second));
    runtime_cleanup(&second_runtime);
}

struct JsCommonAstBuildWaiter {
    const char* source;
    size_t source_length;
    const char* reference;
    InputScriptBuildScope build;
};

static void* js_common_ast_build_waiter_main(void* opaque) {
    JsCommonAstBuildWaiter* waiter = (JsCommonAstBuildWaiter*)opaque;
    (void)js_common_ast_cache_begin_build(&waiter->build, waiter->source,
        waiter->source_length, waiter->reference, false, false, false);
    return NULL;
}

TEST(JsInterpreter, SingleFlightsCommonAstTemplateBuild) {
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    static int cache_generation = 0;
    char reference[64];
    snprintf(reference, sizeof(reference), "<js-common-ast-claim-%d>",
        ++cache_generation);
    const char source[] = "var answer = 21 * 2; answer;";

    InputScriptBuildScope owner = {};
    ASSERT_EQ(js_common_ast_cache_begin_build(&owner, source,
        sizeof(source) - 1, reference, false, false, false), INPUT_SCRIPT_BUILD_OWNER);
    EXPECT_EQ(owner.kind, INPUT_SCRIPT_BUILD_AST);

    JsCommonAstBuildWaiter waiter = {source, sizeof(source) - 1, reference, {}};
    pthread_t worker = {};
    ASSERT_EQ(pthread_create(&worker, NULL, js_common_ast_build_waiter_main,
        &waiter), 0);

    bool wait_observed = false;
    for (int attempt = 0; attempt < 100000; attempt++) {
        InputScriptCacheStats current = {};
        input_script_cache_get_stats(cache, &current);
        if (current.single_flight_waits > before.single_flight_waits) {
            wait_observed = true;
            break;
        }
        sched_yield();
    }

    Runtime runtime = {};
    runtime_init(&runtime);
    bool published = false;
    JsTranspiler* transpiler = js_transpiler_create(&runtime);
    if (!transpiler || !js_transpiler_parse_c(transpiler, source,
            sizeof(source) - 1, JS_PARSE_AUTO) || !transpiler->ast_root ||
            transpiler->has_errors) {
        ADD_FAILURE() << "failed to build common JS AST template";
        js_transpiler_destroy(transpiler);
    } else {
        JsScript* script = js_script_adopt_transpiler(transpiler, &runtime,
            reference);
        published = script && js_common_ast_cache_admit(&runtime, script, source,
            sizeof(source) - 1, reference, false, false, false);
        if (!published && script) {
            runtime_free_script(&runtime, (Script*)script, true);
        }
    }
    js_common_ast_cache_complete_build(&owner, published, false);
    ASSERT_EQ(pthread_join(worker, NULL), 0);
    EXPECT_TRUE(wait_observed);
    EXPECT_TRUE(published);
    EXPECT_EQ(waiter.build.state, INPUT_SCRIPT_BUILD_READY);
    EXPECT_EQ(waiter.build.kind, INPUT_SCRIPT_BUILD_AST);
    js_common_ast_cache_complete_build(&waiter.build, false, false);
    runtime_cleanup(&runtime);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_EQ(after.ast_builds, before.ast_builds + 1);
    EXPECT_GE(after.single_flight_waits, before.single_flight_waits + 1);
}

struct JsCommonMirBuildWaiter {
    bool module_mode;
    JsMirLeaseSession* session;
    const char* source;
    size_t source_length;
    const char* reference;
    InputScriptBuildScope build;
};

static void* js_common_mir_build_waiter_main(void* opaque) {
    JsCommonMirBuildWaiter* waiter = (JsCommonMirBuildWaiter*)opaque;
    if (waiter->module_mode) {
        (void)js_module_mir_cache_begin_build(waiter->source,
            waiter->source_length, waiter->reference, &waiter->build);
    } else {
        (void)js_mir_lease_session_begin_build(waiter->session, true,
            waiter->source, waiter->source_length, waiter->reference, NULL,
            &waiter->build);
    }
    return NULL;
}

TEST(JsInterpreter, SingleFlightsCommonMirLeaseBuild) {
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    JsMirLeaseSession* session = js_mir_lease_session_create();
    ASSERT_NE(session, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    static int cache_generation = 0;
    char reference[64];
    snprintf(reference, sizeof(reference), "<js-mir-lease-claim-%d>",
        ++cache_generation);
    const char source[] = "var cachedAnswer = 42;";

    InputScriptBuildScope owner = {};
    ASSERT_EQ(js_mir_lease_session_begin_build(session, true, source,
        sizeof(source) - 1, reference, NULL, &owner), INPUT_SCRIPT_BUILD_OWNER);
    EXPECT_EQ(owner.kind, INPUT_SCRIPT_BUILD_MIR);

    JsCommonMirBuildWaiter waiter = {false, session, source,
        sizeof(source) - 1, reference, {}};
    pthread_t worker = {};
    int create_result = pthread_create(&worker, NULL,
        js_common_mir_build_waiter_main, &waiter);
    if (create_result != 0) {
        js_mir_lease_session_complete_build(&owner, false, false);
        js_mir_lease_session_close(session);
        ADD_FAILURE() << "failed to create MIR lease claim waiter";
        return;
    }

    bool wait_observed = false;
    for (int attempt = 0; attempt < 100000; attempt++) {
        InputScriptCacheStats current = {};
        input_script_cache_get_stats(cache, &current);
        if (current.single_flight_waits > before.single_flight_waits) {
            wait_observed = true;
            break;
        }
        sched_yield();
    }

    Runtime runtime = {};
    runtime_init(&runtime);
    JsPreambleState compiled = {};
    Item compile_result = compile_js_mir_preamble_len(&runtime, source,
        sizeof(source) - 1, reference, &compiled);
    bool published = !item_is_error(compile_result) &&
        js_mir_lease_session_adopt_build(session, &owner, &compiled) != NULL;
    if (!published) preamble_state_destroy(&compiled);
    js_mir_lease_session_complete_build(&owner, published, false);
    ASSERT_EQ(pthread_join(worker, NULL), 0);
    EXPECT_TRUE(wait_observed);
    EXPECT_TRUE(published);
    EXPECT_EQ(waiter.build.state, INPUT_SCRIPT_BUILD_READY);
    EXPECT_EQ(waiter.build.kind, INPUT_SCRIPT_BUILD_MIR);
    js_mir_lease_session_complete_build(&waiter.build, false, false);
    js_mir_lease_session_close(session);
    runtime_cleanup(&runtime);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_EQ(after.mir_builds, before.mir_builds + 1);
    EXPECT_GE(after.single_flight_waits, before.single_flight_waits + 1);
}

TEST(JsInterpreter, SingleFlightsClosedModuleMirBuild) {
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    static int cache_generation = 0;
    char reference[64];
    snprintf(reference, sizeof(reference), "<js-module-mir-claim-%d>",
        ++cache_generation);
    const char source[] = "export const answer = 42;";

    InputScriptBuildScope owner = {};
    ASSERT_EQ(js_module_mir_cache_begin_build(source, sizeof(source) - 1,
        reference, &owner), INPUT_SCRIPT_BUILD_OWNER);
    EXPECT_EQ(owner.kind, INPUT_SCRIPT_BUILD_MIR);

    JsCommonMirBuildWaiter waiter = {true, NULL, source,
        sizeof(source) - 1, reference, {}};
    pthread_t worker = {};
    int create_result = pthread_create(&worker, NULL,
        js_common_mir_build_waiter_main, &waiter);
    if (create_result != 0) {
        js_module_mir_cache_complete_build(&owner, false, false);
        ADD_FAILURE() << "failed to create module MIR claim waiter";
        return;
    }

    bool wait_observed = false;
    for (int attempt = 0; attempt < 100000; attempt++) {
        InputScriptCacheStats current = {};
        input_script_cache_get_stats(cache, &current);
        if (current.single_flight_waits > before.single_flight_waits) {
            wait_observed = true;
            break;
        }
        sched_yield();
    }

    // The cache adapter is opaque: exercise its claim/ownership handoff with
    // a real empty MIR context while the execution semantics stay covered by
    // ReusesClosedModuleMirArtifactAcrossFreshRuntimes.
    JsModuleMirArtifact* compiled = (JsModuleMirArtifact*)mem_calloc(1,
        sizeof(JsModuleMirArtifact), MEM_CAT_JS_RUNTIME);
    if (!compiled) {
        js_module_mir_cache_complete_build(&owner, false, false);
        ASSERT_EQ(pthread_join(worker, NULL), 0);
        js_module_mir_cache_complete_build(&waiter.build, false, false);
        ADD_FAILURE() << "failed to allocate module MIR artifact";
        return;
    }
    compiled->image.mir_ctx = MIR_init();
    compiled->image.entry_func = (void*)js_test_callable_target;
    compiled->image.owns_compiled_state = true;
    if (!compiled->image.mir_ctx) {
        js_module_mir_artifact_destroy(compiled);
        js_module_mir_cache_complete_build(&owner, false, false);
        ASSERT_EQ(pthread_join(worker, NULL), 0);
        js_module_mir_cache_complete_build(&waiter.build, false, false);
        ADD_FAILURE() << "failed to initialize module MIR context";
        return;
    }

    InputCacheScope* scope = NULL;
    bool published = js_module_mir_cache_adopt_build(&owner, compiled, &scope) != NULL;
    if (!published) js_module_mir_artifact_destroy(compiled);
    js_module_mir_cache_complete_build(&owner, published, false);
    if (scope) input_script_cache_close_scope(scope);
    ASSERT_EQ(pthread_join(worker, NULL), 0);
    EXPECT_TRUE(wait_observed);
    EXPECT_TRUE(published);
    EXPECT_EQ(waiter.build.state, INPUT_SCRIPT_BUILD_READY);
    js_module_mir_cache_complete_build(&waiter.build, false, false);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_EQ(after.mir_builds, before.mir_builds + 1);
    EXPECT_GE(after.single_flight_waits, before.single_flight_waits + 1);
}

TEST(JsInterpreter, ReusesClosedModuleMirArtifactAcrossFreshRuntimes) {
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    const char filename[] = "test/js/script_cache/closed_module.mjs";
    Runtime first = {};
    runtime_init(&first);
    Item first_namespace = load_js_module(&first, filename);
    ASSERT_FALSE(item_is_error(first_namespace));
    EXPECT_EQ(js_get_key_default(first_namespace, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Item second_namespace = load_js_module(&second, filename);
    ASSERT_FALSE(item_is_error(second_namespace));
    EXPECT_EQ(js_get_key_default(second_namespace, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.mir_builds, before.mir_builds + 1);
    EXPECT_GE(after.mir_hits, before.mir_hits + 1);
    EXPECT_GE(after.module_hits, before.module_hits + 1);
}

TEST(JsInterpreter, ReusesSynchronousStaticImportModuleMirAcrossFreshRuntimes) {
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    const char filename[] = "test/js/script_cache/static_import_module.mjs";
    Runtime first = {};
    runtime_init(&first);
    Item first_namespace = load_js_module(&first, filename);
    ASSERT_FALSE(item_is_error(first_namespace));
    EXPECT_EQ(js_get_key_default(first_namespace, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&first);

    Runtime second = {};
    runtime_init(&second);
    Item second_namespace = load_js_module(&second, filename);
    ASSERT_FALSE(item_is_error(second_namespace));
    EXPECT_EQ(js_get_key_default(second_namespace, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    // The child and parent compile once, then both enter fresh second-runtime
    // namespaces through their retained immutable images (D8.5.1v2).
    EXPECT_GE(after.mir_builds, before.mir_builds + 2);
    EXPECT_GE(after.mir_hits, before.mir_hits + 2);
    EXPECT_GE(after.module_hits, before.module_hits + 2);
}

TEST(JsInterpreter, RetiresStaticImportModuleMirConeWhenDependencyChanges) {
    ASSERT_EQ(file_ensure_dir("temp"), 0);
    static int cache_generation = 0;
    char root_path[128];
    char dependency_path[128];
    char root_source[256];
    int generation = ++cache_generation;
    snprintf(root_path, sizeof(root_path),
        "temp/js-static-cache-root-%d.mjs", generation);
    snprintf(dependency_path, sizeof(dependency_path),
        "temp/js-static-cache-dependency-%d.mjs", generation);
    snprintf(root_source, sizeof(root_source),
        "import { base } from \"./js-static-cache-dependency-%d.mjs\";\n"
        "export const answer = base + 1;\n", generation);
    const char first_dependency[] = "export const base = 41;\n";
    const char second_dependency[] = "export const base = 52;\n";
    ASSERT_EQ(write_binary_file(root_path, root_source, strlen(root_source)), 0);
    ASSERT_EQ(write_binary_file(dependency_path, first_dependency,
        sizeof(first_dependency) - 1), 0);

    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);

    Runtime first = {};
    runtime_init(&first);
    Item first_namespace = load_js_module(&first, root_path);
    ASSERT_FALSE(item_is_error(first_namespace));
    EXPECT_EQ(js_get_key_default(first_namespace, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&first);

    // A different source generation must retire the cached importer before
    // the next realm can observe the child namespace (D8.5.1v2).
    ASSERT_EQ(write_binary_file(dependency_path, second_dependency,
        sizeof(second_dependency) - 1), 0);

    Runtime second = {};
    runtime_init(&second);
    Item second_namespace = load_js_module(&second, root_path);
    ASSERT_FALSE(item_is_error(second_namespace));
    EXPECT_EQ(js_get_key_default(second_namespace, js_make_string("answer")).item,
        flt2it(53.0).item);
    runtime_cleanup(&second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.dependency_invalidations,
        before.dependency_invalidations + 1);
}

TEST(JsInterpreter, ReusesAstTemplatesForClassModuleEvalAndTypeScript) {
    InputScriptCache* cache = input_manager_global_script_cache();
    ASSERT_NE(cache, nullptr);
    InputScriptCacheStats before = {};
    input_script_cache_get_stats(cache, &before);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);

    const char class_source[] =
        "class Box { constructor(value) { this.value = value; } "
        "read() { return this.value; } } "
        "function make() { return new Box(42).read(); } make();";
    Runtime class_first = {};
    runtime_init(&class_first);
    Item class_first_result = transpile_js_to_mir(&class_first, class_source,
        "<ast-overlay-class>", NULL);
    ASSERT_FALSE(item_is_error(class_first_result));
    EXPECT_EQ(class_first_result.item, flt2it(42.0).item);
    runtime_cleanup(&class_first);

    Runtime class_second = {};
    runtime_init(&class_second);
    Item class_second_result = transpile_js_to_mir(&class_second, class_source,
        "<ast-overlay-class>", NULL);
    ASSERT_FALSE(item_is_error(class_second_result));
    EXPECT_EQ(class_second_result.item, flt2it(42.0).item);
    runtime_cleanup(&class_second);

    const char module_source[] = "export const answer = 42;";
    Runtime module_first = {};
    runtime_init(&module_first);
    Item module_first_result = js_interp_execute_es_module_source(&module_first,
        module_source, sizeof(module_source) - 1, "<ast-overlay-module.mjs>", NULL);
    ASSERT_FALSE(item_is_error(module_first_result));
    EXPECT_EQ(js_get_key_default(module_first_result, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&module_first);

    Runtime module_second = {};
    runtime_init(&module_second);
    Item module_second_result = js_interp_execute_es_module_source(&module_second,
        module_source, sizeof(module_source) - 1, "<ast-overlay-module.mjs>", NULL);
    ASSERT_FALSE(item_is_error(module_second_result));
    EXPECT_EQ(js_get_key_default(module_second_result, js_make_string("answer")).item,
        flt2it(42.0).item);
    runtime_cleanup(&module_second);

    const char eval_source[] = "var answer = 42; answer;";
    Runtime eval_first = {};
    runtime_init(&eval_first);
    Item eval_first_result = js_interp_execute_indirect_eval_source(&eval_first,
        eval_source, sizeof(eval_source) - 1, "<ast-overlay-eval>", NULL);
    ASSERT_FALSE(item_is_error(eval_first_result));
    EXPECT_EQ(eval_first_result.item, flt2it(42.0).item);
    runtime_cleanup(&eval_first);

    Runtime eval_second = {};
    runtime_init(&eval_second);
    Item eval_second_result = js_interp_execute_indirect_eval_source(&eval_second,
        eval_source, sizeof(eval_source) - 1, "<ast-overlay-eval>", NULL);
    ASSERT_FALSE(item_is_error(eval_second_result));
    EXPECT_EQ(eval_second_result.item, flt2it(42.0).item);
    runtime_cleanup(&eval_second);

    const char typescript_source[] = "const answer: number = 42; answer;";
    Runtime typescript_first = {};
    runtime_init(&typescript_first);
    Item typescript_first_result = transpile_js_typescript_to_mir_len(
        &typescript_first, typescript_source, sizeof(typescript_source) - 1,
        "<ast-overlay.ts>", NULL);
    ASSERT_FALSE(item_is_error(typescript_first_result));
    EXPECT_EQ(typescript_first_result.item, flt2it(42.0).item);
    runtime_cleanup(&typescript_first);

    Runtime typescript_second = {};
    runtime_init(&typescript_second);
    Item typescript_second_result = transpile_js_typescript_to_mir_len(
        &typescript_second, typescript_source, sizeof(typescript_source) - 1,
        "<ast-overlay.ts>", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(typescript_second_result));
    EXPECT_EQ(typescript_second_result.item, flt2it(42.0).item);
    runtime_cleanup(&typescript_second);

    InputScriptCacheStats after = {};
    input_script_cache_get_stats(cache, &after);
    EXPECT_GE(after.ast_builds, before.ast_builds + 4);
    EXPECT_GE(after.ast_hits, before.ast_hits + 4);
    EXPECT_GE(after.module_hits, before.module_hits + 4);
}

TEST(JsMir, CapturesTopLevelForOfBindingsAfterSiblingFunctionDeclaration) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "function invoke(callback) { return callback(); } "
        "let sum = 0; "
        "for (let value of [3, 4]) { "
        "sum += invoke(function() { return value; }); } "
        "globalThis.__lambdaForOfClosureResult = sum;";
    Item result = transpile_js_to_mir(&runtime, source, "for-of-closure.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(result));
    Item sum = js_get_key_default(js_get_global_this(),
        js_make_string("__lambdaForOfClosureResult"));
    EXPECT_EQ(sum.item, flt2it(7.0).item);

    runtime_cleanup(&runtime);
}

TEST(JsMir, KeepsShadowedTailCallAsOrdinaryCall) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "function descend(n) { if (n === 0) return 0; "
        "{ const descend = function(x) { return x + 40; }; return descend(n); } } "
        "globalThis.__lambdaShadowedTailCall = descend(2);";
    Item result = transpile_js_to_mir(&runtime, source, "shadowed-tail-call.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(result));
    Item value = js_get_key_default(js_get_global_this(),
        js_make_string("__lambdaShadowedTailCall"));
    EXPECT_EQ(value.item, flt2it(42.0).item);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, SeparateClassicScriptsReadHarnessGlobalLexicalBindings) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char harness_source[] = "const harnessValue = 42;";
    JsScript* harness = js_interp_prepare_script(&runtime, harness_source,
        sizeof(harness_source) - 1, "harness.js");
    ASSERT_NE(harness, nullptr);
    ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, harness, NULL)));

    const char test_source[] = "harnessValue;";
    Item result = js_interp_execute_source(&runtime, test_source,
        sizeof(test_source) - 1, "test.js", NULL);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, flt2it(42.0).item);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainedHarnessRebuildsAfterRealmReplacement) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char harness_source[] =
        "function assertHarness(value) { if (!value) throw new Error('failed'); }";
    JsScript* harness = js_interp_prepare_script(&runtime, harness_source,
        sizeof(harness_source) - 1, "harness.js");
    ASSERT_NE(harness, nullptr);

    ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, harness, NULL)));
    const char first_test[] = "assertHarness(true);";
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, first_test,
        sizeof(first_test) - 1, "first.js", NULL)));

    // The harness AST survives; its function objects must be recreated with
    // the new realm instead of surviving the old heap generation.
    runtime_reset_heap(&runtime);
    ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, harness, NULL)));
    const char second_test[] = "assertHarness(true);";
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, second_test,
        sizeof(second_test) - 1, "second.js", NULL)));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ReusesFunctionAstTemplateAcrossHeapReplacement) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function cachedHarness() { return 'cached realm literal'; } cachedHarness();";
    JsScript* first = js_interp_prepare_script(&runtime, source, sizeof(source) - 1,
        "cached-harness.js");
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(runtime.scripts->length, 1);
    ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, first, NULL)));

    runtime_reset_heap(&runtime);
    JsScript* second = js_interp_prepare_script(&runtime, source, sizeof(source) - 1,
        "cached-harness.js");
    // The parsed function is shared, while the replacement realm receives an
    // execution overlay with its own lazy callable metadata (D8.5.1v2).
    EXPECT_NE(second, first);
    EXPECT_EQ(second->cache_template, (Script*)first);
    EXPECT_EQ(runtime.scripts->length, 2);
    ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, second, NULL)));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainsAstStringAndBigIntLiteralsInRealmCache) {
    Runtime runtime = {};
    runtime_init(&runtime);
    struct LiteralCase {
        const char* source;
        size_t length;
        const char* filename;
        TypeId type_id;
    } cases[] = {
        {"'cached literal';", sizeof("'cached literal';") - 1,
            "cached-string-literal.js", LMD_TYPE_STRING},
        {"1234567890123456789012345678901234567890n;",
            sizeof("1234567890123456789012345678901234567890n;") - 1,
            "cached-bigint-literal.js", LMD_TYPE_DECIMAL},
    };

    for (const LiteralCase& test : cases) {
        JsScript* script = js_interp_prepare_script(&runtime, test.source,
            test.length, test.filename, false);
        ASSERT_NE(script, nullptr);
        ASSERT_EQ(script->runtime_literal_count, 1u);

        {
            RootFrame roots(3);
            Rooted<Item> first(roots, js_interp_execute_script(&runtime, script, NULL));
            ASSERT_FALSE(item_is_error(first.get()));
            ASSERT_EQ(get_type_id(first.get()), test.type_id);
            Rooted<Item> second(roots, js_interp_execute_script(&runtime, script, NULL));
            ASSERT_FALSE(item_is_error(second.get()));
            EXPECT_EQ(first.get().item, second.get().item);

            JsRuntimeState* state = js_runtime_state_for(runtime.eval_context);
            ASSERT_NE(state, nullptr);
            JsAstLiteralCacheEntry* entry = state->ast_literal_cache.entries;
            const void* image = script->cache_template ? (const void*)script->cache_template
                : (const void*)script;
            while (entry && entry->ast_image != image) {
                entry = entry->next;
            }
            ASSERT_NE(entry, nullptr);
            EXPECT_TRUE(entry->materialized[0]);
            Item* cached = root_vector_at(&state->ast_literal_cache.values,
                entry->first_root_slot);
            ASSERT_NE(cached, nullptr);
            EXPECT_EQ(cached->item, first.get().item);

            heap_gc_collect();
            Rooted<Item> after_gc(roots, js_interp_execute_script(&runtime, script, NULL));
            ASSERT_FALSE(item_is_error(after_gc.get()));
            EXPECT_EQ(js_strict_equal(first.get(), after_gc.get()).item, b2it(true));
        }

        // Cached ASTs can outlive this heap; the replacement realm must make
        // a fresh, precisely rooted literal cache instead of using old slots.
        runtime_reset_heap(&runtime);
        JsScript* replacement = js_interp_prepare_script(&runtime, test.source,
            test.length, test.filename, false);
        ASSERT_NE(replacement, nullptr);
        RootFrame replacement_roots(1);
        Rooted<Item> replacement_value(replacement_roots,
            js_interp_execute_script(&runtime, replacement, NULL));
        ASSERT_FALSE(item_is_error(replacement_value.get()));
        ASSERT_EQ(get_type_id(replacement_value.get()), test.type_id);

        JsRuntimeState* replacement_state = js_runtime_state_for(runtime.eval_context);
        ASSERT_NE(replacement_state, nullptr);
        JsAstLiteralCacheEntry* replacement_entry = replacement_state->ast_literal_cache.entries;
        const void* replacement_image = replacement->cache_template
            ? (const void*)replacement->cache_template : (const void*)replacement;
        while (replacement_entry && replacement_entry->ast_image != replacement_image) {
            replacement_entry = replacement_entry->next;
        }
        ASSERT_NE(replacement_entry, nullptr);
        EXPECT_TRUE(replacement_entry->materialized[0]);
    }
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, KeepsSynthesizedTypeScriptEnumLiteralsOutOfAstLiteralCache) {
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] = "enum Hue { Red = 7 } Hue[7];";
    {
        RootFrame roots(2);
        Rooted<Item> result(roots, transpile_js_typescript_to_mir_len(&runtime,
            source, sizeof(source) - 1, "<synthetic-literal-cache.ts>", NULL));
        ASSERT_FALSE(item_is_error(result.get()));
        Rooted<Item> expected(roots, js_make_string("Red"));
        EXPECT_EQ(js_strict_equal(result.get(), expected.get()).item, b2it(true));
    }
    runtime_cleanup(&runtime);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
}

TEST(JsInterpreter, LazyGlobalsPreserveOwnDescriptorsAndReplacements) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] =
        "var before = 'Uint16Array' in globalThis; "
        "var descriptor = Object.getOwnPropertyDescriptor(globalThis, 'Uint16Array'); "
        "var identity = descriptor.value === Uint16Array && Uint16Array === globalThis.Uint16Array; "
        "globalThis.Float64Array = 17; delete globalThis.BigUint64Array; "
        "Object.defineProperty(globalThis, 'Int16Array', {value: 23}); "
        "var math = Object.getOwnPropertyDescriptor(globalThis, 'Math'); "
        "before && identity && descriptor.writable && descriptor.configurable && "
        "!descriptor.enumerable && Float64Array === 17 && Int16Array === 23 && "
        "!('BigUint64Array' in globalThis) && math.value === Math && "
        "Object.getPrototypeOf(Uint16Array.prototype) === Object.getPrototypeOf(Uint8Array.prototype);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "lazy-globals.js", NULL);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PublishesWebStreamConstructorsToGlobalThis) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] =
        "var readable = new ReadableStream({}); "
        "var writable = new WritableStream({}); "
        "var transform = new TransformStream({}); "
        "var encoder = new TextEncoderStream(); "
        "var decoder = new TextDecoderStream(); "
        "var piped = readable.pipeThrough(new TextEncoderStream()); "
        "typeof ReadableStream === 'function' && "
        "typeof WritableStream === 'function' && "
        "typeof TransformStream === 'function' && "
        "typeof TextEncoderStream === 'function' && "
        "typeof TextDecoderStream === 'function' && "
        "typeof readable.getReader === 'function' && "
        "typeof readable.pipeTo === 'function' && "
        "typeof readable.pipeThrough === 'function' && "
        "typeof piped.getReader === 'function' && "
        "typeof writable.getWriter === 'function' && "
        "typeof transform.readable.getReader === 'function' && "
        "typeof transform.writable.getWriter === 'function' && "
        "typeof encoder.writable.getWriter === 'function' && "
        "typeof decoder.readable.getReader === 'function';";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "web-stream-globals.js", NULL);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainsExternalClassicCallbackArgumentsAcrossUnits) {
    Runtime runtime = {};
    runtime_init(&runtime);
    runtime.js_ast_backend = true;

    const char preamble_source[] = "var window = globalThis;";
    JsPreambleState preamble = {};
    uint64_t result_home = 0;
    ASSERT_FALSE(item_is_error(transpile_js_to_mir_preamble_len(&runtime,
        preamble_source, sizeof(preamble_source) - 1,
        "classic-preamble.js", &preamble, &result_home)));
    uint32_t preamble_state_id = preamble.module_state_id;
    ASSERT_TRUE(lambda_module_state_reserve_and_activate(
        (uint32_t)preamble.module_var_count));
    ASSERT_TRUE(lambda_module_state_copy_var_prefix(preamble_state_id,
        lambda_active_module_state_id(), (uint32_t)preamble.module_var_count));

    const char loader_source[] =
        "var modules = { gitbook: 41 }; "
        "window.require = function(items, callback) { "
        "items = items.map(function(name) { "
        "name = name.toLowerCase(); "
        "if (!modules[name]) { throw new Error('unknown module'); } "
        "return modules[name]; "
        "}); "
        "callback.apply(null, items); "
        "};";
    ASSERT_FALSE(item_is_error(transpile_js_to_mir_with_preamble_len(&runtime,
        loader_source, sizeof(loader_source) - 1,
        "classic-loader.js", &preamble, &result_home)));

    const char sync_source[] =
        "if (typeof window !== 'undefined') { "
        "var globalSyncTick = 1; "
        "}";
    ASSERT_FALSE(item_is_error(transpile_js_to_mir_with_preamble_len(&runtime,
        sync_source, sizeof(sync_source) - 1,
        "classic-global-sync.js", &preamble, &result_home)));

    const char consumer_source[] =
        "require(['gitbook'], function(value) { "
        "globalThis.__cachedClassicResult = value + 1; "
        "}); "
        "if (globalThis.__cachedClassicResult !== 42) { "
        "throw new Error('cached classic callback lost its argument'); "
        "}";
    EXPECT_FALSE(item_is_error(transpile_js_to_mir_with_preamble_len(&runtime,
        consumer_source, sizeof(consumer_source) - 1,
        "classic-consumer.js", &preamble, &result_home)));

    preamble_state_destroy(&preamble);
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainedTypedArrayAndPropertyHelpersKeepStrictnessAndIsolation) {
    Runtime runtime = {};
    runtime_init(&runtime);
    StrBuf* source = strbuf_new();
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/sta.js"));
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/assert.js"));
    JsScript* base = js_interp_prepare_script(&runtime, source->str, source->length, "base.js");
    ASSERT_NE(base, nullptr);
    strbuf_reset(source);
    strbuf_append_str(source, "\"use strict\";\nfunction helperStrict() { return this; }\n");
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/propertyHelper.js"));
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/testTypedArray.js"));
    JsScript* helpers = js_interp_prepare_script(&runtime, source->str, source->length, "includes.js");
    strbuf_free(source);
    ASSERT_NE(helpers, nullptr);
    const int checkpoint = runtime.scripts->length;
    const uint32_t module_checkpoint = runtime.next_module_state_id;
    const char test[] =
        "assert.sameValue(helperStrict(), undefined); "
        "testWithTypedArrayConstructors(function(C) { assert.sameValue(new C(2).length, 2); }); "
        "verifyProperty({x: 1}, 'x', {value: 1, writable: true}); "
        "assert.sameValue(typeof verifyProperty, 'function'); "
        "verifyProperty = 0; typedArrayConstructors.length = 0; true;";
    for (int i = 0; i < 2; i++) {
        ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, base, NULL)));
        ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, helpers, NULL)));
        heap_gc_collect();
        Item result = js_interp_execute_source(&runtime, test, sizeof(test) - 1, "test.js", NULL);
        ASSERT_FALSE(item_is_error(result));
        EXPECT_EQ(result.item, ITEM_TRUE);
        runtime_reset_heap(&runtime);
        runtime_release_script_generation(&runtime, checkpoint, module_checkpoint);
    }
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, NativeHarnessSourceEntryInstallsTest262Helpers) {
    Runtime runtime = {};
    runtime_init(&runtime);
    const char source[] =
        "assert.sameValue(typeof assert, 'function'); "
        "assert.sameValue(typeof verifyProperty, 'function'); true;";
    Item result = js_interp_execute_test262_source(&runtime, source,
        sizeof(source) - 1, "native-harness.js", true, NULL);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, NativeHarnessMatchesCanonicalPropertyAndThrowChecks) {
    struct Probe { const char* source; const char* expected; };
    const Probe probes[] = {
        {"verifyProperty({x: 1}, 'x', {value: 1, writable: true, enumerable: true, configurable: true});", "ok"},
        {"var o = {x: 1}; verifyProperty(o, 'x', {configurable: true}); if ('x' in o) throw new Error();", "ok"},
        {"var o = {x: 1}; verifyProperty(o, 'x', {writable: true, configurable: true}, {restore: true}); if (o.x !== 1) throw new Error();", "ok"},
        {"verifyProperty({}, 'x', undefined);", "ok"},
        {"verifyProperty({}, 'x');", "Test262Error"},
        {"verifyProperty({x: 1}, 'x', {writable: undefined});", "ok"},
        {"verifyProperty({x: 1}, 'x', {writable: 1});", "Test262Error"},
        {"verifyProperty({x: 1}, 'x', {invalid: true});", "Test262Error"},
        {"var s = Symbol(); var o = {}; o[s] = 1; verifyProperty(o, s, {enumerable: true, writable: true});", "ok"},
        {"var p = new Proxy({x: 1}, {set: function() { return true; }}); verifyProperty(p, 'x', {writable: true});", "Test262Error"},
        {"var p = new Proxy({x: 1}, {deleteProperty: function() { return true; }}); verifyProperty(p, 'x', {configurable: true});", "Test262Error"},
        {"var p = new Proxy({x: 1}, {ownKeys: function() { return []; }}); verifyProperty(p, 'x', {enumerable: true});", "Test262Error"},
        {"var p = new Proxy({x: 1}, {set: function() { throw new TypeError(); }}); verifyProperty(p, 'x', {writable: true});", "Test262Error"},
        {"var p = new Proxy({x: 1}, {set: function() { throw new RangeError(); }}); verifyProperty(p, 'x', {writable: true});", "Test262Error"},
        {"var d = {get writable() { throw new RangeError(); }}; verifyProperty({x: 1}, 'x', d);", "RangeError"},
        {"assert.throws(TypeError, function() { throw new TypeError(); });", "ok"},
        {"assert.throws(Error, function() { throw new TypeError(); });", "Test262Error"},
        {"assert.throws(TypeError, function() { throw {constructor: TypeError}; });", "ok"},
        {"assert.throws(Array, function() { throw []; });", "ok"},
        {"assert.throws(undefined, function() { throw new TypeError(); });", "TypeError"},
        {"assert.throws(Function, function() { throw function() {}; });", "Test262Error"},
        {"assert.throws(TypeError, function() { throw {get constructor() { throw new RangeError(); }}; });", "RangeError"},
    };
    for (const Probe& probe : probes) {
        SCOPED_TRACE(probe.source);
        for (int native = 0; native < 2; native++) {
            SCOPED_TRACE(native);
            Runtime runtime = {};
            runtime_init(&runtime);
            StrBuf* source = strbuf_new();
            if (!native) {
                ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/sta.js"));
                ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/assert.js"));
                ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/propertyHelper.js"));
            }
            strbuf_append_str(source, "var outcome = 'ok'; try { ");
            strbuf_append_str(source, probe.source);
            strbuf_append_str(source,
                " } catch (error) { outcome = error.name || error.constructor.name; } outcome;");
            JsScript* script = js_interp_prepare_script(&runtime, source->str, source->length, "parity.js");
            strbuf_free(source);
            ASSERT_NE(script, nullptr);
            script->test262_native_harness = native != 0;
            Item result = js_interp_execute_script(&runtime, script, NULL);
            ASSERT_FALSE(item_is_error(result));
            ASSERT_EQ(get_type_id(result), LMD_TYPE_STRING);
            EXPECT_STREQ(it2s(result)->chars, probe.expected);
            runtime_cleanup(&runtime);
        }
    }
}

TEST(JsScriptOwnership, ReleasesBatchScriptGenerationAfterHeapReset) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char harness_source[] = "function harnessFn() { return 42; }";
    JsScript* harness = js_interp_prepare_script(&runtime, harness_source,
        sizeof(harness_source) - 1, "harness.js");
    ASSERT_NE(harness, nullptr);
    const int test_script_checkpoint = runtime.scripts->length;
    const uint32_t test_module_state_checkpoint = runtime.next_module_state_id;

    ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, harness, NULL)));
    const char test_source[] = "harnessFn();";
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, test_source,
        sizeof(test_source) - 1, "test.js", NULL)));
    ASSERT_EQ(runtime.scripts->length, test_script_checkpoint + 1);

    runtime_reset_heap(&runtime);
    runtime_release_script_generation(&runtime, test_script_checkpoint,
        test_module_state_checkpoint);
    EXPECT_EQ(runtime.scripts->length, test_script_checkpoint);
    EXPECT_EQ(runtime.next_module_state_id, test_module_state_checkpoint);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, UsesSharedCommonJsResolverAndModuleRegistry) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);

    const char source[] =
        "var first = require('./main.cjs'); "
        "var second = require('./main.cjs'); "
        "[first.answer, second.answer, globalThis.__interp_cjs_main_loads, "
        "globalThis.__interp_cjs_dep_loads, first === second, typeof module];";
    Item result = transpile_js_to_mir(&runtime, source,
        "test/js/interp_cjs/entry.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, flt2it(42.0).item);
    EXPECT_EQ(js_elements_get_int(result, 1).item, flt2it(42.0).item);
    EXPECT_EQ(js_elements_get_int(result, 2).item, flt2it(1.0).item);
    EXPECT_EQ(js_elements_get_int(result, 3).item, flt2it(1.0).item);
    EXPECT_EQ(js_elements_get_int(result, 4).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 5),
        js_make_string("undefined")).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_cjs/main.cjs"),
        nullptr);
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_cjs/dep.cjs"),
        nullptr);
    EXPECT_EQ(runtime.eval_context->active_module_state->module_id,
        ((Script*)runtime.scripts->data[0])->module_state_id);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, LinksEsModulesWithLiveRegistryBindings) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char direct_source[] =
        "export let counter = 40; export function bump() { counter += 1; }";
    Item direct_namespace = js_interp_execute_es_module_source(&runtime,
        direct_source, sizeof(direct_source) - 1,
        "test/js/interp_esm/direct.mjs", NULL);
    ASSERT_FALSE(item_is_error(direct_namespace));
    Item direct_bump = js_get_key_default(direct_namespace, js_make_string("bump"));
    ASSERT_EQ(get_type_id(direct_bump), LMD_TYPE_FUNC);
    ASSERT_FALSE(item_is_error(js_call_function(direct_bump, make_js_undefined(),
        NULL, 0)));
    EXPECT_EQ(js_strict_equal(js_get_key_default(direct_namespace,
        js_make_string("counter")), flt2it(41.0)).item, b2it(true));

    const char source[] =
        "import { bump, counter } from './dep.mjs'; "
        "bump(); export const answer = counter + 1;";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    Item answer = js_get_key_default(namespace_obj, js_make_string("answer"));
    ASSERT_FALSE(item_is_error(answer));
    EXPECT_EQ(js_strict_equal(answer, flt2it(42.0)).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/main.mjs"),
        nullptr);
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/dep.mjs"),
        nullptr);

    const char global_probe[] = "typeof answer;";
    Item global_result = js_interp_execute_source(&runtime, global_probe,
        sizeof(global_probe) - 1, "esm-global-probe.js", NULL);
    ASSERT_FALSE(item_is_error(global_result));
    EXPECT_EQ(js_strict_equal(global_result, js_make_string("undefined")).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, SupportsModuleMetadataAndInlineDynamicImports) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char metadata_source[] = "export default import.meta.url;";
    Item metadata = js_interp_execute_es_module_source(&runtime,
        metadata_source, sizeof(metadata_source) - 1,
        "test/js/interp_esm/metadata.mjs", NULL);
    ASSERT_FALSE(item_is_error(metadata));
    EXPECT_EQ(js_strict_equal(js_get_key_default(metadata, js_make_string("default")),
        js_make_string("test/js/interp_esm/metadata.mjs")).item, b2it(true));

    const char source[] =
        "globalThis.__interp_dynamic_counter = 0; "
        "import('./dep.mjs').then(function(ns) { "
        "globalThis.__interp_dynamic_counter = ns.counter; });";
    runtime.js_document_base_url = "test/js/interp_esm/document.html";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item result = transpile_js_to_mir(&runtime, source,
        "<inline-script-0>", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(result));
    Item dynamic_value = js_get_key_default(js_get_global_this(),
        js_make_string("__interp_dynamic_counter"));
    EXPECT_EQ(js_strict_equal(dynamic_value, flt2it(40.0)).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/dep.mjs"),
        nullptr);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, OrdersDynamicImportsThroughTopLevelAwaitDependencies) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "import { gate, aStarted, bStarted } from './tla-order-setup.mjs'; "
        "globalThis.__interp_tla_order = ''; "
        "const imports = Promise.all(["
        "bStarted.promise.then(() => import('./tla-order-a.mjs').finally(() => "
        "globalThis.__interp_tla_order += 'A')).catch(() => {}), "
        "import('./tla-order-b.mjs').finally(() => "
        "globalThis.__interp_tla_order += 'B').catch(() => {})]); "
        "Promise.all([aStarted.promise, bStarted.promise]).then(gate.resolve); "
        "imports.then(() => globalThis.__interp_tla_order += '!');";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item result = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/tla-order-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(result));
    Item order = js_get_key_default(js_get_global_this(),
        js_make_string("__interp_tla_order"));
    EXPECT_EQ(js_strict_equal(order, js_make_string("BA!")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, OrdersDynamicImportRejectionsThroughTopLevelAwaitDependencies) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "import { gate, aStarted, bStarted } from './tla-order-setup.mjs'; "
        "globalThis.__interp_tla_rejection_order = ''; "
        "const imports = Promise.all(["
        "bStarted.promise.then(() => import('./tla-order-a.mjs').finally(() => "
        "globalThis.__interp_tla_rejection_order += 'A')).catch(() => {}), "
        "import('./tla-order-b.mjs').finally(() => "
        "globalThis.__interp_tla_rejection_order += 'B').catch(() => {})]); "
        "Promise.all([aStarted.promise, bStarted.promise]).then(() => "
        "gate.reject('expected rejection')); "
        "imports.then(() => globalThis.__interp_tla_rejection_order += '!');";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item result = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/tla-rejection-order-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(result));
    Item order = js_get_key_default(js_get_global_this(),
        js_make_string("__interp_tla_rejection_order"));
    EXPECT_EQ(js_strict_equal(order, js_make_string("BA!")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, SupportsTopLevelAwaitInModules) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "await 1;";
    Item namespace_obj = js_interp_execute_es_module_source(&runtime, source,
        sizeof(source) - 1, "test/js/interp_esm/top-level-await.mjs", NULL);

    ASSERT_FALSE(item_is_error(namespace_obj));
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PublishesDestructuredModuleExports) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "export const { check } = { check: false };";
    Item namespace_obj = js_interp_execute_es_module_source(&runtime, source,
        sizeof(source) - 1, "test/js/interp_esm/destructured-export.mjs", NULL);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("check")), (Item){.item = b2it(false)}).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PublishesAnonymousDefaultClassesAfterTopLevelAwait) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "function base() { return class {}; } "
        "export default class extends base(await 1) {};";
    Item namespace_obj = js_interp_execute_es_module_source(&runtime, source,
        sizeof(source) - 1, "test/js/interp_esm/default-class-await.mjs", NULL);

    char diagnostic[256] = {};
    js_error_lane_format(namespace_obj, diagnostic, sizeof(diagnostic));
    ASSERT_FALSE(item_is_error(namespace_obj)) << diagnostic;
    EXPECT_EQ(get_type_id(js_get_key_default(namespace_obj,
        js_make_string("default"))), LMD_TYPE_FUNC);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PublishesDefaultExpressionAfterTopLevelAwait) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "export default await 42;";
    Item namespace_obj = js_interp_execute_es_module_source(&runtime, source,
        sizeof(source) - 1, "test/js/interp_esm/default-expression-await.mjs", NULL);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("default")), flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ResumesMultipleTopLevelAwaitsBeforePublishingExports) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "export const first = await 1; "
        "export default await Promise.resolve(42); "
        "export const third = await 'done';";
    Item namespace_obj = js_interp_execute_es_module_source(&runtime, source,
        sizeof(source) - 1, "test/js/interp_esm/multiple-awaits.mjs", NULL);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj, js_make_string("first")),
        flt2it(1.0)).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj, js_make_string("default")),
        flt2it(42.0)).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj, js_make_string("third")),
        js_make_string("done")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AwaitsDynamicImportOfSuspendedModule) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "const dependency = await import('./dynamic-tla-dependency.mjs'); "
        "export default [dependency.first, dependency.default, dependency.third];";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = js_interp_execute_es_module_source(&runtime, source,
        sizeof(source) - 1, "test/js/interp_esm/dynamic-tla-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    Item values = js_get_key_default(namespace_obj, js_make_string("default"));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(values, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(values, 1), flt2it(42.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(values, 2), js_make_string("done")).item,
        b2it(true));
    Item dependency = js_module_get(js_make_string(
        "test/js/interp_esm/dynamic-tla-dependency.mjs"));
    EXPECT_EQ(js_strict_equal(js_get_key_default(dependency, js_make_string("default")),
        flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainedAstModuleAwaitsDynamicImportOfSuspendedModule) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "const dependency = await import('./dynamic-tla-dependency.mjs'); "
        "export default [dependency.first, dependency.default, dependency.third];";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/dynamic-tla-retained-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    Item values = js_get_key_default(namespace_obj, js_make_string("default"));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(values, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(values, 1), flt2it(42.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(values, 2), js_make_string("done")).item,
        b2it(true));
    Item dependency = js_module_get(js_make_string(
        "test/js/interp_esm/dynamic-tla-dependency.mjs"));
    EXPECT_EQ(js_strict_equal(js_get_key_default(dependency, js_make_string("default")),
        flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RejectsDynamicImportCoercionWithTheThrownValue) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "0;", 2,
        "dynamic-import-bootstrap.js", NULL)));

    Item rejected = js_dynamic_import(js_get_import_meta());
    Item completion = js_await_sync(rejected);

    ASSERT_TRUE(item_is_error(completion));
    Item name = js_get_key_default(js_error_lane_payload(completion),
        js_make_string("name"));
    EXPECT_EQ(js_strict_equal(name, js_make_string("TypeError")).item, b2it(true));

    const char module_source[] =
        "import(import.meta).catch(function(error) { "
        "globalThis.__interp_dynamic_error_name = error.name; });";
    Item namespace_obj = js_interp_execute_es_module_source(&runtime, module_source,
        sizeof(module_source) - 1, "dynamic-import-import-meta.mjs", NULL);
    ASSERT_FALSE(item_is_error(namespace_obj));
    Item observed_name = js_get_key_default(js_get_global_this(),
        js_make_string("__interp_dynamic_error_name"));
    EXPECT_EQ(js_strict_equal(observed_name, js_make_string("TypeError")).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ModuleDynamicImportsResolveFromDocumentReference) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "0;", 2,
        "module-dynamic-import-setup.js", NULL)));

    const char source[] =
        "import('./dep.mjs').then(function(ns) { "
        "globalThis.__module_dynamic_counter = ns.counter; });";
    Item namespace_obj = transpile_js_module_to_mir(&runtime, source,
        "test/js/interp_esm/document.html");

    ASSERT_FALSE(item_is_error(namespace_obj));
    Item dynamic_value = js_get_key_default(js_get_global_this(),
        js_make_string("__module_dynamic_counter"));
    EXPECT_EQ(js_strict_equal(dynamic_value, flt2it(40.0)).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/dep.mjs"),
        nullptr);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, StaticModuleHonorsRequestedAstBackend) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "export const answer = 42;";
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "0;", 2,
        "ast-module-backend-setup.js", NULL)));
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_module_to_mir(&runtime, source,
        "ast-module-backend.mjs");
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("answer")), flt2it(42.0)).item, b2it(true));
    JsRuntimeState* state = js_runtime_state_for(runtime.eval_context);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(js_code_store_count(&state->code_store), 0);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesLiveBindingsThroughNamedReexports) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "import { bump, liveCounter } from './reexport.mjs'; "
        "bump(); export default liveCounter;";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/reexport-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("default")), flt2it(41.0)).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/reexport.mjs"),
        nullptr);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesLiveBindingsThroughStarReexports) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "import { bump, counter } from './star.mjs'; "
        "bump(); export default counter;";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/star-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("default")), flt2it(41.0)).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/star.mjs"),
        nullptr);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExportsNamespaceObjectsAndAnonymousDefaultFunctions) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char namespace_source[] =
        "import { dependency } from './namespace.mjs'; "
        "dependency.bump(); export default dependency.counter;";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_to_mir(&runtime, namespace_source,
        "test/js/interp_esm/namespace-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("default")), flt2it(41.0)).item, b2it(true));

    const char anonymous_source[] = "export default function() { return 42; }";
    Item anonymous = js_interp_execute_es_module_source(&runtime, anonymous_source,
        sizeof(anonymous_source) - 1, "test/js/interp_esm/anonymous-default.mjs", NULL);
    ASSERT_FALSE(item_is_error(anonymous));
    Item default_function = js_get_key_default(anonymous, js_make_string("default"));
    ASSERT_FALSE(item_is_error(default_function));
    Item called = js_call_function(default_function, make_js_undefined(), NULL, 0);
    ASSERT_FALSE(item_is_error(called));
    EXPECT_EQ(js_strict_equal(called, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InstantiatesHoistedExportsBeforeCircularDependencies) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "import answer from './circular-a.mjs'; export default answer;";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/circular-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("default")), flt2it(42.0)).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/circular-a.mjs"),
        nullptr);
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/circular-b.mjs"),
        nullptr);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RejectsAmbiguousStarExportsBeforeModuleBodyExecution) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "export * from './duplicate-first.mjs'; "
        "export * from './duplicate-second.mjs'; "
        "globalThis.__interp_ambiguous_star_body = true;";
    Item result = js_interp_execute_es_module_source(&runtime, source,
        sizeof(source) - 1, "test/js/interp_esm/ambiguous-star.mjs", NULL);

    EXPECT_TRUE(item_is_error(result));
    EXPECT_EQ(js_has_own_property(js_get_global_this(),
        js_make_string("__interp_ambiguous_star_body")).item, b2it(false));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ImportsLambdaModulesThroughTheSharedRegistry) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "import { increment } from './lambda_dep.ls'; "
        "export default increment(41);";
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item namespace_obj = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/lambda-main.mjs", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(namespace_obj));
    EXPECT_EQ(js_strict_equal(js_get_key_default(namespace_obj,
        js_make_string("default")), flt2it(42.0)).item, b2it(true));
    ModuleDescriptor* lambda_module = module_get_for_runtime(&runtime,
        "test/js/interp_esm/lambda_dep.ls");
    ASSERT_NE(lambda_module, nullptr);
    EXPECT_STREQ(lambda_module->source_lang, "lambda");

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExecutesControlFlowAndPropertyReferences) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let total = 0; for (let i = 0; i < 6; i++) { "
        "if (i === 3) continue; total += i; } "
        "let state = { total: total }; state.answer = state.total + 2; state.answer;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "control-flow.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(14.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EvaluatesLegacyForInInitializerBeforeRightHandSide) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var first = (function() { var effects = 0; "
        "for (var value = ++effects in {}); return effects; })(); "
        "var second = (function() { var stored; "
        "for (var value = 0 in stored = value, {}); return stored; })(); "
        "var third = (function() { for (var value = 0 in {}); return value; })(); "
        "var fourth = (function() { var effects = 0, iterations = 0, stored; "
        "for (var value = (++effects, -1) in stored = value, {a: 0, b: 1, c: 2}) "
        "{ ++iterations; } return (stored === -1 ? 1 : 0) + "
        "(effects === 1 ? 2 : 0) + (iterations === 3 ? 4 : 0); })(); "
        "[first, second, third, fourth];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "legacy-for-in.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, flt2it(1.0).item);
    EXPECT_EQ(js_elements_get_int(result, 1).item, flt2it(0.0).item);
    EXPECT_EQ(js_elements_get_int(result, 2).item, flt2it(0.0).item);
    EXPECT_EQ(js_elements_get_int(result, 3).item, flt2it(7.0).item);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExecutesThrowCatchAndFinallyCompletions) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let caught = 0; try { throw 40; } catch (value) { caught = value + 2; } "
        "finally { caught += 1; } caught;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "completion.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(43.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesArrowLexicalThisAcrossTheSharedCallKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let object = { value: 40, make: function() { return () => this.value + 2; } }; "
        "var callback = object.make(); callback();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "arrow-this.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EvaluatesObjectMethodsWithTheSharedThisCallPath) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let point = { value: 40, add(extra) { return this.value + extra; } }; "
        "point.add(2);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "object-method.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DefinesObjectAccessorsThroughTheSharedPropertyKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let point = { raw: 40, get answer() { return this.raw + 2; }, "
        "set answer(value) { this.raw = value - 2; } }; point.answer = 44; point.answer;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "object-accessor.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(44.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, KeepsAccessorCellsVirtualAndAliveAcrossCollection) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let point = { get answer() { return 42; } }; point;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "virtual-accessor-cell.js", NULL);

    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    PersistentRooted<Item> point_root(result);
    ASSERT_TRUE(point_root.valid());
    ShapeEntry* entry = js_find_shape_entry(point_root.get(), "answer", 6);
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(jspd_is_accessor(entry));
    EXPECT_EQ(entry->byte_offset, -1);
    ASSERT_NE(entry->accessor, nullptr);

    heap_gc_collect();
    Item answer = js_get_key_default(point_root.get(), js_name_item("answer", 6));
    ASSERT_FALSE(item_is_error(answer));
    EXPECT_EQ(js_strict_equal(answer, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ConvertsAccessorDescriptorsBetweenVirtualAndDataStorage) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char install_source[] =
        "let point = { answer: 1 }; "
        "Object.defineProperty(point, 'answer', { "
        "get() { return 42; }, configurable: true }); point;";
    Item point = js_interp_execute_source(&runtime, install_source,
        sizeof(install_source) - 1, "virtual-accessor-convert.js", NULL);

    ASSERT_EQ(get_type_id(point), LMD_TYPE_MAP);
    PersistentRooted<Item> point_root(point);
    ASSERT_TRUE(point_root.valid());
    ShapeEntry* entry = js_find_shape_entry(point_root.get(), "answer", 6);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(jspd_is_accessor(entry));
    EXPECT_EQ(entry->byte_offset, -1);
    ASSERT_NE(entry->accessor, nullptr);
    EXPECT_EQ(js_strict_equal(js_get_key_default(point_root.get(),
        js_name_item("answer", 6)), flt2it(42.0)).item, b2it(true));

    const char materialize_source[] =
        "Object.defineProperty(point, 'answer', { value: 9, writable: true, "
        "enumerable: true, configurable: true }); point;";
    point_root.set(js_interp_execute_source(&runtime, materialize_source,
        sizeof(materialize_source) - 1, "virtual-accessor-materialize.js", NULL));

    ASSERT_EQ(get_type_id(point_root.get()), LMD_TYPE_MAP);
    entry = js_find_shape_entry(point_root.get(), "answer", 6);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(jspd_is_accessor(entry));
    EXPECT_GE(entry->byte_offset, 0);
    EXPECT_EQ(entry->accessor, nullptr);
    EXPECT_EQ(js_strict_equal(js_get_key_default(point_root.get(),
        js_name_item("answer", 6)), flt2it(9.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ConstructsOrdinaryFunctionsThroughTheCommonCallKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function Point(value) { this.value = value; } "
        "let point = new Point(40); point.value + 2;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "construct.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InvokesInterpretedCallbacksFromNativeBuiltins) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let values = [20, 21]; "
        "values.map(value => value + 1).reduce((sum, value) => sum + value, 0);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "native-callback.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(43.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesStrictPrimitiveReceiverAcrossIntrinsicCallbacks) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "\"use strict\"; "
        "Boolean.prototype.toString = function() { return typeof this; }; "
        "var direct = [true, false].toLocaleString(); "
        "Object.defineProperty(Boolean.prototype, 'toString', { get: function() { "
        "var receiver_type = typeof this; return function() { return receiver_type; }; } }); "
        "var getter = [true, false].toLocaleString(); "
        "[direct, getter];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "strict-primitive-receiver.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0),
        js_make_string("boolean,boolean")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1),
        js_make_string("boolean,boolean")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ObservesInheritedBigIntWrapperCoercionAccessors) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "const BigIntToString = BigInt.prototype.toString; "
        "let gets = 0; let calls = 0; "
        "const stringify = function() { ++calls; return `${BigIntToString.call(this)}foo`; }; "
        "Object.defineProperty(BigInt.prototype, 'toString', { get: function() { "
        "++gets; return stringify; } }); "
        "const boxed = Object(1n); "
        "const default_value = '' + boxed; "
        "const string_value = `${boxed}`; "
        "[default_value === '1', string_value === '1foo', gets === 1, calls === 1];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "bigint-wrapper-coercion.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    for (int index = 0; index < 4; index++) {
        EXPECT_EQ(js_elements_get_int(result, index).item, b2it(true))
            << "BigInt wrapper coercion result index " << index;
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DelegatesAstGeneratorYieldsThroughTheSharedAsyncProtocol) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function* inner() { yield 40; yield 2; } "
        "function* outer() { yield* inner(); } "
        "var sync = outer(); var first = sync.next(); var second = sync.next(); "
        "var complete = sync.next(); "
        "async function* asyncOuter() { yield* inner(); } "
        "var asyncNext = asyncOuter().next(); "
        "[first.value === 40, !first.done, second.value === 2, !second.done, "
        "complete.done, asyncNext instanceof Promise];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "generator-delegation.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    for (int index = 0; index < 6; index++) {
        EXPECT_EQ(js_elements_get_int(result, index).item, b2it(true))
            << "generator delegation result index " << index;
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ForwardsAstGeneratorReturnThroughDelegatedIterator) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var returnGets = 0; var iterable = { next: function() { return { value: 1, done: false }; }, "
        "get return() { returnGets += 1; return null; } }; "
        "iterable[Symbol.iterator] = function() { return iterable; }; "
        "function* outer() { yield* iterable; } "
        "var iterator = outer(); iterator.next(); var result = iterator.return(2); "
        "[result.value, result.done, returnGets];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "generator-delegation-return.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(2.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 1).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2), flt2it(1.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ClosesAstGeneratorsFromForOfWithoutReplayingPriorStatements) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var started = 0; var finalized = 0; "
        "function* values() { started += 1; try { yield; } finally { finalized += 1; } } "
        "var iterator = values(); for (var value of iterator) { break; } [started, finalized];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "generator-for-of-close.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(1.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ReplaysNestedGeneratorYieldsBeforeAdvancingTheStatementList) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function* nested() { yield yield 1; } "
        "var iter = nested(); var first = iter.next(); var second = iter.next(3); "
        "var third = iter.next(); "
        "[first.value, first.done, second.value, second.done, third.value, third.done];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "nested-generator-yield.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 1).item, b2it(false));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2), flt2it(3.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 3).item, b2it(false));
    EXPECT_EQ(get_type_id(js_elements_get_int(result, 4)), LMD_TYPE_UNDEFINED);
    EXPECT_EQ(js_elements_get_int(result, 5).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ReplaysPriorNestedGeneratorInputsThroughSpreadExpressions) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var calls = 0; var nested = function*() { calls += 1; yield [...yield yield]; }; "
        "var iter = nested(); var first = iter.next(); "
        "var second = iter.next(['a', 'b', 'c']); var third = iter.next(second.value); "
        "[first.value, first.done, second.value, second.done, third.value, third.done, calls];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "nested-generator-yield-spread.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(get_type_id(js_elements_get_int(result, 0)), LMD_TYPE_UNDEFINED);
    EXPECT_EQ(js_elements_get_int(result, 1).item, b2it(false));
    EXPECT_EQ(js_array_length(js_elements_get_int(result, 2)), 3);
    EXPECT_EQ(js_elements_get_int(result, 3).item, b2it(false));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(js_elements_get_int(result, 4), 0),
        js_make_string("a")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(js_elements_get_int(result, 4), 2),
        js_make_string("c")).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 5).item, b2it(false));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 6), flt2it(1.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AppliesArrayHoleSemanticsThroughNativeCallbacks) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var copy = [0, 1, , , 1]; copy.copyWithin(0, 1, 4); "
        "var copied_holes = copy[0] === 1 && copy[4] === 1 && "
        "!copy.hasOwnProperty(1) && !copy.hasOwnProperty(2) && !copy.hasOwnProperty(3); "
        "var deleted_before_hole = false; "
        "var deleted_array = [0, , 2]; "
        "Object.defineProperty(deleted_array, '0', { get: function() { "
        "delete Array.prototype[1]; return 0; }, configurable: true }); "
        "Array.prototype[1] = 1; "
        "var deleted_result = deleted_array.every(function(value, index) { "
        "deleted_before_hole = true; return index !== 1; }); "
        "delete Array.prototype[1]; "
        "var added_array = [0, , 2]; "
        "Object.defineProperty(added_array, '0', { get: function() { "
        "Object.defineProperty(Array.prototype, '1', { get: function() { return 6.99; }, "
        "configurable: true }); return 0; }, configurable: true }); "
        "var added_result = added_array.every(function(value, index) { "
        "return index !== 1 || value !== 6.99; }); "
        "delete Array.prototype[1]; "
        "Object.defineProperty(Array.prototype, '0', { get: function() { return 11; }, "
        "configurable: true }); "
        "var inherited_accessor_result = [,,,].every(function(value, index) { "
        "return index !== 0 || value !== 11; }); "
        "delete Array.prototype[0]; "
        "Array.prototype[1] = 13; "
        "var inherited_data_result = [,,,].every(function(value, index) { "
        "return index !== 1 || value !== 13; }); "
        "delete Array.prototype[1]; "
        "[copied_holes, deleted_result, deleted_before_hole, !added_result, "
        "!inherited_accessor_result, !inherited_data_result];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "array-holes.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    for (int index = 0; index < 6; index++) {
        EXPECT_EQ(js_elements_get_int(result, index).item, b2it(true))
            << "array-hole result index " << index;
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, KeepsDeclarationAndShadowedClosureBindingIdentity) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let declaration = identity; function identity() { return 1; } "
        "let first; let second; let third; "
        "for (let i = 0; i < 3; i++) { "
        "if (i === 0) first = () => i; "
        "if (i === 1) second = () => i; "
        "if (i === 2) third = () => i; } "
        "function make(value) { var outer = () => value; { let value = 7; "
        "var inner = () => value; return [outer, inner]; } } var pair = make(3); "
        "[(declaration === identity ? 0 : 1000) + first() * 100 + second() * 10 + third(), "
        "pair[0]() * 10 + pair[1]()];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "iteration-closure.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(12.0)).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(37.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, BindsUnbracedAnnexBFunctionSelfReferencesLexically) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "if (false) function _f() {} else function f() { initial = f; f = 123; }";
    JsScript* script = js_interp_prepare_script(&runtime, source, sizeof(source) - 1,
        "annexb-branch.js");
    ASSERT_NE(script, nullptr);

    JsIfNode* conditional = (JsIfNode*)((JsProgramNode*)script->ast_root)->body;
    ASSERT_NE(conditional, nullptr);
    ASSERT_NE(conditional->alternate_vars, nullptr);
    ASSERT_NE(conditional->alternate, nullptr);
    JsFunctionNode* function = (JsFunctionNode*)conditional->alternate;
    ASSERT_NE(function->name, nullptr);
    EXPECT_EQ(function->name->len, 1u);
    EXPECT_EQ(function->name->chars[0], 'f');
    EXPECT_EQ(function->vars->parent, conditional->alternate_vars);
    ASSERT_NE(function->body, nullptr);
    JsBlockNode* body = (JsBlockNode*)function->body;
    ASSERT_NE(body->statements, nullptr);
    ASSERT_NE(body->statements->next, nullptr);

    NameEntry* branch_f = nullptr;
    for (NameEntry* entry = conditional->alternate_vars->first; entry; entry = entry->next) {
        if (entry->name && entry->name->len == 1 && entry->name->chars[0] == 'f') {
            branch_f = entry;
            break;
        }
    }
    ASSERT_NE(conditional->alternate_vars->first, nullptr);
    ASSERT_NE(branch_f, nullptr);
    JsAssignmentNode* first = (JsAssignmentNode*)((JsExpressionStatementNode*)
        body->statements)->expression;
    JsAssignmentNode* second = (JsAssignmentNode*)((JsExpressionStatementNode*)
        body->statements->next)->expression;
    ASSERT_NE(first->right, nullptr);
    ASSERT_NE(second->left, nullptr);
    EXPECT_EQ(((JsIdentifierNode*)first->right)->entry, branch_f);
    EXPECT_EQ(((JsIdentifierNode*)second->left)->entry, branch_f);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AppliesAnnexBVarCompanionOnlyWhenNoLexicalConflictExists) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char loop_source[] =
        "(0,eval)('for (let f in { key: 0 }) {{ function f() {} }}'); typeof f;";
    Item loop_result = js_interp_execute_source(&runtime, loop_source,
        sizeof(loop_source) - 1, "annexb-loop-conflict.js", NULL);
    ASSERT_FALSE(item_is_error(loop_result));
    EXPECT_EQ(js_strict_equal(loop_result, js_make_string("undefined")).item, b2it(true));

    const char catch_source[] =
        "(0,eval)('try { throw 0; } catch (f) {{ function f() {} }}'); typeof f;";
    Item catch_result = js_interp_execute_source(&runtime, catch_source,
        sizeof(catch_source) - 1, "annexb-catch-exception.js", NULL);
    ASSERT_FALSE(item_is_error(catch_result));
    EXPECT_EQ(js_strict_equal(catch_result, js_make_string("function")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EvaluatesSloppyCallAssignmentTargetsBeforeReferenceError) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var fCalled = 0; var gCalled = 0; "
        "function f() { fCalled++; return {}; } function g() { gCalled++; return 1; } "
        "var compound = false; try { f() += g(); } catch (error) { compound = error instanceof ReferenceError; } "
        "var update = false; try { f()++; } catch (error) { update = error instanceof ReferenceError; } "
        "var forIn = false; try { for (f() in [1]) {} } catch (error) { forIn = error instanceof ReferenceError; } "
        "var forOf = false; try { for (f() of [1]) {} } catch (error) { forOf = error instanceof ReferenceError; } "
        "compound && update && forIn && forOf && fCalled === 4 && gCalled === 0;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "annexb-call-assignment-target.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, CreatesDynamicFunctionWithHtmlCloseCommentParameter) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var created = Function('\\n-->', ''); typeof created === 'function' && created.length === 0;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "dynamic-html-close-comment.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RejectsDynamicFunctionHtmlCloseCommentWithoutLineTerminator) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var caught; try { Function('-->', ''); } catch (error) { caught = error; } "
        "caught instanceof SyntaxError && caught.constructor === SyntaxError;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "dynamic-html-close-comment-invalid.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ThrowsCatchableSyntaxErrorsForInvalidDynamicFunctionBodies) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var duplicate = false; var restricted = false; "
        "try { Function('a', 'a', '\"use strict\";'); } "
        "catch (error) { duplicate = error instanceof SyntaxError; } "
        "try { Function('eval', '\"use strict\";'); } "
        "catch (error) { restricted = error instanceof SyntaxError; } "
        "duplicate && restricted;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "dynamic-function-early-error.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EnforcesConstAssignmentsThroughSharedEnvironmentCells) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let outcome = 0; { const value = 1; "
        "try { value = 2; } catch (error) { outcome = 42; } } outcome;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "const-binding.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ResolvesLaterLexicalBindingsToTheirTdzCells) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let value = 99; function test() { try { var observed = value; } "
        "catch (error) { return 42; } let value = 1; return observed; } test();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "tdz-binding.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesTypeofAndTdzAbruptCompletionOrder) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let side = 0; let type = typeof missing; { try { local += (side = 42); } "
        "catch (error) {} let local = 1; } (type === 'undefined' ? 10 : 0) + side;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "typeof-tdz.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(10.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExecutesSwitchInItsSharedLexicalEnvironment) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let selected = 0; switch (2) { case 1: selected = 100; break; "
        "case 2: let base = 40; selected = base; default: selected += 2; break; } selected;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "switch.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RoutesLabeledLoopCompletionsThroughTheAstStack) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let count = 0; outer: for (let row = 0; row < 3; row++) { "
        "for (let column = 0; column < 2; column++) { count++; "
        "if (row === 1) continue outer; if (row === 2) break outer; } } count;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "labels.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(4.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, UsesSharedObjectEnvironmentRecordsForWith) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let value = 0; let scope = { value: 40 }; with (scope) { value += 2; } "
        "value + scope.value;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "with.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainsWithObjectEnvironmentForEscapedAstClosures) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let callback; let scope = { value: 40 }; "
        "with (scope) { callback = () => value + 2; } callback();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "with-closure.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExecutesSynchronousIteratorLoopsWithLexicalCells) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let captured; for (let value of [40]) { captured = () => value; } captured() + 2;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "iterator-loops.js", NULL);

    ASSERT_EQ(runtime.scripts->length, 1);
    EXPECT_TRUE(js_interp_script_is_supported((JsScript*)runtime.scripts->data[0]));
    EXPECT_FALSE(item_is_error(result));
    if (!item_is_error(result)) {
        EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExecutesForInThroughSharedPropertyRuntime) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let entries = { first: 40, second: 2 }; let count = 0; "
        "for (let key in entries) { count += entries[key]; } count;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "for-in.js", NULL);

    EXPECT_FALSE(item_is_error(result));
    if (!item_is_error(result)) {
        EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EvaluatesTemplateSubstitutionsWithJavaScriptCoercion) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "let value = 40; `value=${value + 2}`;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "template.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, js_make_string("value=42")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, CallsTaggedTemplatesThroughTheSharedFunctionRuntime) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function tag(parts, value) { return parts[0] + value + parts.raw[1]; } "
        "tag`answer=${40 + 2}!`;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "tagged-template.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, js_make_string("answer=42!")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExpandsArrayObjectAndCallSpreadThroughRuntimeHelpers) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function sum(a, b, c) { return a + b + c; } let values = [40]; "
        "let array = [...values, 2]; let base = { answer: 2 }; "
        "let copy = { ...base }; sum(...[20, 21], 1) + array[0] + copy.answer;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "spread.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(84.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, BindsDestructuringDefaultsAndRestInSharedCells) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let [first = 20, second = 21, ...tail] = [20, 21, 1]; "
        "let { answer, bonus = 2, ...remaining } = { answer: 40, extra: 1 }; "
        "first + second + tail[0] + answer + bonus + remaining.extra;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "destructuring.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(85.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InfersNamesForAnonymousDestructuringDefaults) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let [arrow = () => {}, ordinary = function() {}, generator = function*() {}, "
        "classValue = class {}, namedClass = class Explicit {}, "
        "staticName = class { static name() {} }] = []; "
        "let { property: objectArrow = () => {} } = {}; "
        "let staticMethodClass = [class { static name() {} }][0]; "
        "let parameterNames = (([parameterClass = class {}, "
        "parameterNamed = class ExplicitParameter {}, "
        "parameterStatic = class { static name() {} }]) => "
        "[parameterClass.name, parameterNamed.name, "
        "parameterStatic.name !== 'parameterStatic'])([]); "
        "var assignedParameterFunction; "
        "assignedParameterFunction = ([assignedClass = class {}, "
        "assignedNamed = class ExplicitAssigned {}, "
        "assignedStatic = class { static name() {} }]) => "
        "[assignedClass.name, assignedNamed.name, "
        "assignedStatic.name !== 'assignedStatic']; "
        "let assignedParameterNames = assignedParameterFunction([]); "
        "[arrow.name, ordinary.name, generator.name, classValue.name, objectArrow.name, "
        "namedClass.name, staticName.name !== 'staticName', "
        "typeof staticMethodClass.name === 'function', parameterNames[0], "
        "parameterNames[1], parameterNames[2], assignedParameterNames[0], "
        "assignedParameterNames[1], assignedParameterNames[2]];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "destructuring-default-names.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), js_make_string("arrow")).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), js_make_string("ordinary")).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2), js_make_string("generator")).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 3), js_make_string("classValue")).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 4), js_make_string("objectArrow")).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 5), js_make_string("Explicit")).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 6).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 7).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 8),
        js_make_string("parameterClass")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 9),
        js_make_string("ExplicitParameter")).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 10).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 11),
        js_make_string("assignedClass")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 12),
        js_make_string("ExplicitAssigned")).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 13).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InfersCallableNamesAtEvaluationBoundaries) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let declared = function() {}; let assigned; assigned = function*() {}; "
        "let object = { value: () => {}, method() {}, get reader() { return 1; }, "
        "set writer(value) {} }; class Named { method() {} get reader() { return 1; } "
        "set writer(value) {} static method() {} static value = function() {} } "
        "let objectReader = Object.getOwnPropertyDescriptor(object, 'reader').get; "
        "let objectWriter = Object.getOwnPropertyDescriptor(object, 'writer').set; "
        "let classReader = Object.getOwnPropertyDescriptor(Named.prototype, 'reader').get; "
        "let classWriter = Object.getOwnPropertyDescriptor(Named.prototype, 'writer').set; "
        "[declared.name, assigned.name, object.value.name, object.method.name, "
        "objectReader.name, objectWriter.name, Named.prototype.method.name, "
        "classReader.name, classWriter.name, Named.method.name, Named.value.name];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "callable-names.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    const char* expected[] = {"declared", "assigned", "value", "method", "get reader",
        "set writer", "method", "get reader", "set writer", "method", "value"};
    for (int index = 0; index < (int)(sizeof(expected) / sizeof(expected[0])); index++) {
        EXPECT_EQ(js_strict_equal(js_elements_get_int(result, index),
            js_make_string(expected[index])).item, b2it(true));
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AssignsClassMethodNamesFromEvaluatedKeys) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let named = Symbol('method'); let anonymous = Symbol(); class C { "
        "[named]() {} [anonymous]() {} static [named]() {} static [anonymous]() {} } "
        "[C.prototype[named].name, C.prototype[anonymous].name, C[named].name, "
        "C[anonymous].name];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "class-computed-method-names.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    const char* expected[] = {"[method]", "", "[method]", ""};
    for (int index = 0; index < (int)(sizeof(expected) / sizeof(expected[0])); index++) {
        EXPECT_EQ(js_strict_equal(js_elements_get_int(result, index),
            js_make_string(expected[index])).item, b2it(true));
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesWithVarInitializerReference) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var object = { value: 'initial' }; "
        "var erase = function() { delete object.value; return 'replacement'; }; "
        "with (object) { var value = erase(); } "
        "[typeof value, object.value];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "with-var-initializer-reference.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0),
        js_make_string("undefined")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1),
        js_make_string("replacement")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ResolvesClosureLocalsBeforeCapturedWithBindings) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var object = { value: 'outer' }; with (object) { "
        "var closure = function() { var value = 'inner'; return [value, object.value]; }; } "
        "closure();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "with-closure-local-binding.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0),
        js_make_string("inner")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1),
        js_make_string("outer")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesNamedFunctionExpressionSelfBindings) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let sloppy = function Self() { Self = 1; return Self; }; "
        "let evaluated = function EvalName() { eval('EvalName = 1'); return EvalName; }; "
        "let generated = function* GeneratorName() { GeneratorName = 1; return GeneratorName; }; "
        "let strictThrown = false; let strict = function StrictName() { 'use strict'; "
        "StrictName = 1; }; try { strict(); } catch (error) { strictThrown = error instanceof TypeError; } "
        "let strictGeneratorThrown = false; let strictGenerated = function* StrictGeneratorName() { "
        "'use strict'; StrictGeneratorName = 1; }; try { strictGenerated().next(); } "
        "catch (error) { strictGeneratorThrown = error instanceof TypeError; } "
        "[sloppy() === sloppy, evaluated() === evaluated, generated().next().value === generated, "
        "strictThrown, strictGeneratorThrown];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "named-function-expression-bindings.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    for (int index = 0; index < 5; index++) {
        EXPECT_EQ(js_elements_get_int(result, index).item, b2it(true));
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RequiresObjectCoercibleForEmptyObjectPatterns) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let nullThrown = false; let undefinedThrown = false; "
        "try { (({}) => {})(null); } catch (error) { nullThrown = error instanceof TypeError; } "
        "try { (({}) => {})(undefined); } catch (error) { "
        "undefinedThrown = error instanceof TypeError; } [nullThrown, undefinedThrown];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "object-pattern-coercible.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 1).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InitializesGeneratorParametersBeforeFirstResume) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let nullThrown = false; let undefinedThrown = false; "
        "function* values({}) {} "
        "try { values(null); } catch (error) { nullThrown = error instanceof TypeError; } "
        "try { values(undefined); } catch (error) { "
        "undefinedThrown = error instanceof TypeError; } [nullThrown, undefinedThrown];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "generator-parameter-instantiation.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 1).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, BindsClassMethodRestParameters) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Container { array([...values]) { return values; } "
        "object({...properties}) { return properties; } } "
        "let instance = new Container(); let array = instance.array([1, 2, 3]); "
        "let object = instance.object({value: 4}); "
        "[Array.isArray(array), array.length, array[0], array[2], object.value];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "class-method-rest-parameter.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(3.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 3), flt2it(3.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 4), flt2it(4.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AssignsDestructuringPatternsToPropertyReferences) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var target = {}; var array = [4]; var object = {value: 5}; "
        "var arrayResult = [target.element] = array; "
        "var objectResult = ({...target.rest} = object); "
        "for ([target.loopElement] of [[6]]) {} "
        "[target.element, target.rest.value, arrayResult === array, "
        "objectResult === object, target.loopElement];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "destructuring-property-assignment.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(4.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(5.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 2).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 3).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 4), flt2it(6.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, TreatsSloppyYieldAsIdentifierOutsideGenerators) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var yield = 9; var value; [value = yield] = []; value;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "sloppy-yield-identifier.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(9.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, KeepsSelfReferentialDefaultParametersInTdz) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var outer = 0; var calls = 0; var caught = false; "
        "var f = function(value = value) { calls += 1; }; "
        "try { f(); } catch (error) { caught = error instanceof ReferenceError; } "
        "[caught, calls];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "self-referential-default-parameter.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(0.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, SealsLexicalSlotsBeforeActivation) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function outer(first) { let local = first; return local + 1; } outer(4);";
    JsScript* script = js_interp_prepare_script(&runtime, source, sizeof(source) - 1,
        "sealed-lexical-slots.js", false);

    ASSERT_NE(script, nullptr);
    ASSERT_NE(script->global_scope, nullptr);
    EXPECT_TRUE(script->global_scope->binding_slots_planned);
    EXPECT_EQ(script->global_scope->binding_slot_count, 1u);
    JsProgramNode* program = (JsProgramNode*)script->ast_root;
    ASSERT_NE(program, nullptr);
    ASSERT_NE(program->body, nullptr);
    ASSERT_EQ(program->body->node_type, AST_NODE_FUNC);
    JsFunctionNode* outer = (JsFunctionNode*)program->body;
    ASSERT_NE(outer->vars, nullptr);
    EXPECT_TRUE(outer->vars->binding_slots_planned);
    EXPECT_EQ(outer->vars->binding_slot_count, 1u);
    JsBlockNode* body = (JsBlockNode*)outer->body;
    ASSERT_NE(body, nullptr);
    ASSERT_NE(body->vars, nullptr);
    EXPECT_TRUE(body->vars->binding_slots_planned);
    EXPECT_EQ(body->vars->binding_slot_count, 1u);

    Item result = js_interp_execute_script(&runtime, script, NULL);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(5.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, SealsSyntheticFieldInitializerSlotsBeforeActivation) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "class Box { value = 3; } new Box().value;";
    JsScript* script = js_interp_prepare_script(&runtime, source, sizeof(source) - 1,
        "sealed-field-initializer-slots.js", false);

    ASSERT_NE(script, nullptr);
    JsProgramNode* program = (JsProgramNode*)script->ast_root;
    ASSERT_NE(program, nullptr);
    ASSERT_NE(program->body, nullptr);
    ASSERT_EQ(program->body->node_type, AST_NODE_CLASS);
    JsClassNode* box = (JsClassNode*)program->body;
    ASSERT_NE(box->body, nullptr);
    JsBlockNode* class_body = (JsBlockNode*)box->body;
    ASSERT_NE(class_body->statements, nullptr);
    ASSERT_EQ(class_body->statements->node_type, AST_NODE_FIELD);
    JsFunctionNode* initializer = js_script_field_initializer_ensure(script,
        (JsFieldDefinitionNode*)class_body->statements);
    ASSERT_NE(initializer, nullptr);
    ASSERT_NE(initializer->vars, nullptr);
    EXPECT_TRUE(initializer->vars->binding_slots_planned);
    EXPECT_EQ(initializer->vars->binding_slot_count, 0u);

    Item result = js_interp_execute_script(&runtime, script, NULL);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(3.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ReusesParameterRootWindowAcrossOrdinaryAndGeneratorCalls) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function ordinary(first = { value: 1 }, second = { value: 2 }, ...rest) { "
        "return first.value + second.value + rest.length; } "
        "function* suspended(first = { value: 3 }, second = { value: 4 }, ...rest) { "
        "yield first.value + second.value + rest.length; } "
        "var ordinary_result = ordinary(undefined, undefined, 7, 8); "
        "var iterator = suspended(undefined, undefined, 9, 10, 11); "
        "[ordinary_result, iterator.next().value];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "parameter-root-window.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(5.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(10.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ResumesGeneratorDestructuringWithoutAdvancingIteratorsTwice) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var nextCount = 0; var returnCount = 0; var reached = 0; "
        "var iterator = { next: function() { nextCount += 1; return {done:false, "
        "value:undefined}; }, return: function() { returnCount += 1; return {}; } }; "
        "var iterable = {}; iterable[Symbol.iterator] = function() { return iterator; }; "
        "function* valueTarget() { [{} = yield] = iterable; reached += 1; } "
        "var first = valueTarget(); first.next(); var firstResult = first.return(7); "
        "var keyReturnCount = 0; var keyIterator = { return: function() { "
        "keyReturnCount += 1; return {}; } }; var keyIterable = {}; "
        "keyIterable[Symbol.iterator] = function() { return keyIterator; }; "
        "function* keyTarget() { [...{}[yield]] = keyIterable; reached += 1; } "
        "var second = keyTarget(); second.next(); var secondResult = second.return(8); "
        "[nextCount, returnCount, reached, firstResult.value, firstResult.done, "
        "keyReturnCount, secondResult.value, secondResult.done];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "generator-destructure-resume.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2), flt2it(0.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 3), flt2it(7.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 4).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 5), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 6), flt2it(8.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 7).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InjectsGeneratorThrowsThroughEnclosingTryCompletions) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function* values() { yield 1; try { yield 2; } catch (error) { yield error; } "
        "yield 3; } var iterator = values(); var first = iterator.next(); "
        "var second = iterator.next(); var marker = {}; var third = iterator.throw(marker); "
        "var fourth = iterator.next(); [first.value, second.value, third.value === marker, "
        "fourth.value, fourth.done];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "generator-throw-try-catch.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(2.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 2).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 3), flt2it(3.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 4).item, b2it(false));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PropagatesComputedObjectPatternKeyErrors) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var key, target; var caught = false; "
        "try { 0, ({ [key.value]: target } = {}); } "
        "catch (error) { caught = error instanceof TypeError; } caught;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "object-pattern-computed-key-error.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RejectsEvalVarRedeclarationInDefaultParameters) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var calls = 0; var caught = false; "
        "var f = function(value = eval('var value = 42')) { calls += 1; }; "
        "try { f(); } catch (error) { caught = error instanceof SyntaxError; } "
        "[caught, calls];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "eval-default-parameter-var.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(0.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DestructuringConsumesAndClosesIteratorsLazily) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var first = 0; var second = 0; function* values() { first += 1; "
        "yield; second += 1; } var [[,] = values()] = []; [first, second];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "lazy-destructuring.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(0.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesAbruptCompletionWhenClosingCustomIterators) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var nextCount = 0; var returnCount = 0; var caughtStep = false; "
        "var stepIterator = { next: function() { nextCount += 1; throw new Error(); }, "
        "return: function() { returnCount += 1; return {}; } }; var stepIterable = {}; "
        "stepIterable[Symbol.iterator] = function() { return stepIterator; }; "
        "try { 0, [value] = stepIterable; } catch (error) { caughtStep = true; } "
        "var bodyCount = 0; var caughtBody = false; var bodyError = new Error(); "
        "var bodyIterable = {}; "
        "bodyIterable[Symbol.iterator] = function() { return { next: function() { "
        "return { done: false, value: 0 }; }, return: 'not callable' }; }; "
        "try { for (var entry of bodyIterable) { bodyCount += 1; throw bodyError; } } "
        "catch (error) { caughtBody = error === bodyError; } "
        "[caughtStep, nextCount, returnCount, caughtBody, bodyCount];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "iterator-close-abrupt.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2), flt2it(0.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 3).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 4), flt2it(1.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PropagatesIteratorStepErrorsFromForOfDestructuring) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var nextCount = 0; var returnCount = 0; var caught = false; var iterable = {}; "
        "var iterator = { next: function() { nextCount += 1; throw new Error(); }, "
        "return: function() { returnCount += 1; return {}; } }; "
        "iterable[Symbol.iterator] = function() { return iterator; }; "
        "try { for ([value] of [iterable]) {} } catch (error) { caught = true; } "
        "[caught, nextCount, returnCount];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "for-of-destructuring-step-error.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2), flt2it(0.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EvaluatesDestructuringReferenceBeforeIteratorStep) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var log = []; function source() { log.push('source'); var iterator = { "
        "next: function() { log.push('iterator-step'); return { get done() { "
        "log.push('iterator-done'); return true; }, get value() { log.push('iterator-value'); } }; } }; "
        "var value = {}; value[Symbol.iterator] = function() { log.push('iterator'); "
        "return iterator; }; return value; } function target() { log.push('target'); "
        "return target = { set q(value) { log.push('set'); } }; } function targetKey() { "
        "log.push('target-key'); return { toString: function() { log.push('target-key-tostring'); "
        "return 'q'; } }; } ([target()[targetKey()]] = source()); log;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "destructuring-reference-order.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    const char* expected[] = {"source", "iterator", "target", "target-key",
        "iterator-step", "iterator-done", "target-key-tostring", "set"};
    ASSERT_EQ(js_array_length(result), (int64_t)(sizeof(expected) / sizeof(expected[0])));
    for (int64_t index = 0; index < js_array_length(result); index++) {
        SCOPED_TRACE(index);
        EXPECT_EQ(js_strict_equal(js_elements_get_int(result, index),
            js_make_string(expected[index])).item, b2it(true));
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AppliesLogicalAssignmentNamingAndReferenceOrder) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var andValue = 1; andValue &&= function() {}; var orValue = 0; "
        "orValue ||= (() => {}); var nullishValue; nullishValue ?" "?= class {}; "
        "var coerced = false; var caught = false; var key = { toString: function() { "
        "coerced = true; return 'key'; } }; try { null[key] &&= 1; } "
        "catch (error) { caught = error instanceof TypeError; } "
        "[andValue.name, orValue.name, nullishValue.name, caught, coerced];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "logical-assignment.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0),
        js_make_string("andValue")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1),
        js_make_string("orValue")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 2),
        js_make_string("nullishValue")).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 3).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 4).item, b2it(false));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesCompoundAssignmentReferenceAcrossDirectEval) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function test() { var x = 3; var inner = (function() { "
        "x *= (eval('var x = 2;'), 4); return x; })(); "
        "return [inner, x]; } test();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "compound-assignment-direct-eval.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(2.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(12.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, BindsPatternParametersThroughTheCommonCallKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function read([first, second = 2, ...tail], { extra = 3 }) { "
        "return first + second + tail[0] + extra; } read([20, 21, 1], { extra: 0 });";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "pattern-parameters.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesLaterParameterTdzDuringDefaultInitialization) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let calls = 0; let fn = (first = later, later) => { calls++; }; "
        "try { fn(); } catch (error) { [error instanceof ReferenceError, calls]; }";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "parameter-tdz.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_elements_get_int(result, 0).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(0.0)).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesDestructuringAssignmentValueAndCatchPatterns) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let source = [40]; let target = 0; let assigned = ([target] = source); "
        "let caught = 0; try { throw { value: 2 }; } catch ({ value }) { caught = value; } "
        "(assigned === source ? 0 : 100) + target + caught;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "pattern-assignment.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ShortCircuitsOptionalChainsAndLogicalAssignments) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let calls = 0; let missing = null; let member = missing?.method(calls = 1); "
        "let fn; let direct = fn?.(calls = 2); let value = 0; value ||= 40; "
        "value &&= value + 2; let fallback = null; fallback ?" "?= value; "
        "(member === undefined ? 1 : 0) + (direct === undefined ? 1 : 0) + calls + value + fallback;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "optional-logical.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(86.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DeletesPropertyReferencesThroughTheSharedObjectRuntime) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let value = { answer: 40 }; let deleted = delete value.answer; "
        "deleted && value.answer === undefined ? 42 : 0;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "delete.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ConstructsRegexLiteralsThroughTheSharedRuntime) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "/^ab+$/i.test('ABb');";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "regex.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ConstructsClassesThroughTheSharedFunctionAndObjectRuntime) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Point { constructor(value) { this.value = value; } "
        "add(extra) { return this.value + extra; } "
        "static forty() { return 40; } } "
        "let point = new Point(40); Point.forty() + point.add(2);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "class.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(82.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EvaluatesClassStaticsAndImplicitDerivedConstruction) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Base { constructor(value) { this.value = value; } "
        "static answer = 40; static { this.offset = 1; } } "
        "class Child extends Base { twice() { return this.value * 2; } } "
        "Base.answer + Base.offset + new Child(21).twice();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "class-inheritance.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(83.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InitializesInstanceFieldsAtSharedConstructionTime) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Point { value = 40; answer = this.value + 2; } new Point().answer;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "class-fields.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, InitializesComputedSymbolClassFields) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let x = Symbol(); let y = Symbol(); class C { [x]; [y] = 42; } "
        "let instance = new C(); "
        "let xDescriptor = Object.getOwnPropertyDescriptor(instance, x); "
        "let yDescriptor = Object.getOwnPropertyDescriptor(instance, y); "
        "[Object.prototype.hasOwnProperty.call(instance, x), "
        "Object.prototype.hasOwnProperty.call(instance, y), instance[x] === undefined, "
        "instance[y] === 42, xDescriptor.enumerable, xDescriptor.writable, "
        "xDescriptor.configurable, yDescriptor.enumerable, yDescriptor.writable, "
        "yDescriptor.configurable];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "computed-symbol-class-fields.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    for (int index = 0; index < 10; index++) {
        EXPECT_EQ(js_elements_get_int(result, index).item, b2it(true));
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesLongSymbolKeysAndDescriptions) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var prefix = 'x'.repeat(127); var first_key = prefix + 'a'; "
        "var second_key = prefix + 'b'; var first = Symbol.for(first_key); "
        "var second = Symbol.for(second_key); var described = Symbol(prefix + 'c'); "
        "var empty = Symbol(''); var absent = Symbol(); var undefined_desc = Symbol(undefined); "
        "[first !== second, Symbol.keyFor(first) === first_key, "
        "Symbol.keyFor(second) === second_key, described.description === prefix + 'c', "
        "described.toString() === 'Symbol(' + prefix + 'c)', empty.description === '', "
        "absent.description === undefined, undefined_desc.description === undefined];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "long-symbol-records.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    for (int index = 0; index < 8; index++) {
        EXPECT_EQ(js_elements_get_int(result, index).item, b2it(true))
            << "long symbol record result index " << index;
    }

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, UsesCoreSymbolsForJsValueIdentity) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var absent = Symbol(); var empty = Symbol(''); "
        "var registered = Symbol.for('registry'); var local = Symbol('registry'); "
        "var o = {}; o[absent] = 1; o[empty] = 2; "
        "var seen = null; var has_seen = null; var proxy = new Proxy({}, { "
        "get: function(target, key) { seen = key; return 0; }, "
        "has: function(target, key) { has_seen = key; return true; } }); "
        "proxy[absent]; "
        "if (registered !== Symbol.for('registry') || local === registered || "
        "Symbol.iterator !== Symbol.iterator || seen !== absent || "
        "!(absent in Object.create(proxy)) || has_seen !== absent || "
        "o[absent] !== 1 || o[empty] !== 2 || !(absent in o) || "
        "Object.getOwnPropertySymbols(o).length !== 2) throw new Error(); "
        "[absent, empty, registered, local, Symbol.iterator];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "core-symbol-identity.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    Item absent_item = js_elements_get_int(result, 0);
    Item empty_item = js_elements_get_int(result, 1);
    Item registered_item = js_elements_get_int(result, 2);
    Item local_item = js_elements_get_int(result, 3);
    Item iterator_item = js_elements_get_int(result, 4);
    ASSERT_EQ(get_type_id(absent_item), LMD_TYPE_SYMBOL);
    ASSERT_EQ(get_type_id(empty_item), LMD_TYPE_SYMBOL);
    ASSERT_EQ(get_type_id(registered_item), LMD_TYPE_SYMBOL);
    ASSERT_EQ(get_type_id(local_item), LMD_TYPE_SYMBOL);
    ASSERT_EQ(get_type_id(iterator_item), LMD_TYPE_SYMBOL);

    Symbol* absent = absent_item.get_safe_symbol();
    Symbol* empty = empty_item.get_safe_symbol();
    Symbol* registered = registered_item.get_safe_symbol();
    Symbol* local = local_item.get_safe_symbol();
    Symbol* iterator = iterator_item.get_safe_symbol();
    ASSERT_NE(absent, nullptr);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(registered, nullptr);
    ASSERT_NE(local, nullptr);
    ASSERT_NE(iterator, nullptr);
    Item cached_iterator = js_well_known_symbol_key(1);
    ASSERT_EQ(get_type_id(cached_iterator), LMD_TYPE_SYMBOL);
    EXPECT_EQ(cached_iterator.item, iterator_item.item);
    EXPECT_NE(absent, empty);
    EXPECT_NE(registered, local);
    EXPECT_EQ(absent->kind, SYMBOL_JS_UNIQUE_UNDESCRIBED);
    EXPECT_EQ(empty->kind, SYMBOL_JS_UNIQUE);
    EXPECT_EQ(registered->kind, SYMBOL_JS_REGISTERED);
    EXPECT_EQ(local->kind, SYMBOL_JS_UNIQUE);
    EXPECT_EQ(iterator->kind, SYMBOL_JS_WELL_KNOWN);
    EXPECT_EQ(absent->len, 0u);
    EXPECT_EQ(empty->len, 0u);
    EXPECT_STREQ(registered->chars, "registry");
    EXPECT_STREQ(local->chars, "registry");
    EXPECT_STREQ(iterator->chars, "Symbol.iterator");
    EXPECT_NE(absent->name_id, NAME_ID_NONE);
    EXPECT_NE(iterator->name_id, NAME_ID_NONE);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DefinesClassAccessorsThroughTheSharedPropertyKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Point { constructor(value) { this.raw = value; } "
        "get answer() { return this.raw + 2; } "
        "set answer(value) { this.raw = value - 2; } } "
        "let point = new Point(40); point.answer = 44; point.answer;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "class-accessor.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(44.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExecutesSupportedAsyncGeneratorForm) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "let sideEffect = 0; async function* work() { yield sideEffect; } sideEffect;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "async-generator.js", NULL);

    EXPECT_FALSE(item_is_error(result));
    ASSERT_NE(runtime.scripts, nullptr);
    EXPECT_EQ(runtime.scripts->length, 1);
    EXPECT_NE(runtime.eval_context, nullptr);
    EXPECT_EQ(js_global_binding_exists(js_make_string("sideEffect")), 1);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, SuspendsAsyncGeneratorAwaitsBeforeYielding) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let order = []; async function* values() { order.push('start'); "
        "let value = await Promise.resolve(1); order.push('after'); yield value; } "
        "async function run() { let iterator = values(); let pending = iterator.next(); "
        "order.push('sync'); let result = await pending; order.push('done'); "
        "return [result.value, order.join(',')]; } run();";
    Item promise = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "async-generator-await-order.js", NULL);

    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1),
        js_make_string("start,sync,after,done")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RejectsAsyncGeneratorYieldedPromises) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "async function* values() { yield Promise.reject('rejected value'); } "
        "async function run() { try { await values().next(); return 'resolved'; } "
        "catch (value) { return value; } } run();";
    Item promise = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "async-generator-yield-rejection.js", NULL);

    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, js_make_string("rejected value")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, IteratesAsyncGeneratorsWithForAwait) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "async function* values() { yield 40; yield 2; } "
        "async function sum() { let total = 0; "
        "for await (let value of values()) { "
        "total += await Promise.resolve(value); } return total; } "
        "sum();";
    Item promise = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "for-await-async-generator.js", NULL);

    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ResumesAsyncGeneratorForAwaitHeadYield) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let target = {}; let count = 0; "
        "async function* values() { "
        "for await ([target[yield]] of [[33]]) { count += 1; } } "
        "values();";
    Item generator = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "async-generator-for-await-head-yield.js", NULL);

    ASSERT_FALSE(item_is_error(generator));
    Item first = js_await_sync_incremental(js_generator_next(generator,
        make_js_undefined()));
    ASSERT_FALSE(item_is_error(first));
    EXPECT_EQ(js_iterator_result_value(first).item, ITEM_JS_UNDEFINED);
    EXPECT_EQ(js_iterator_result_done(first).item, b2it(false));
    JsGeneratorStateRecord* state = js_generator_get_ast_state(generator);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->ast_replay_skip, 1);
    Item second = js_await_sync_incremental(js_generator_next(generator,
        js_make_string("key")));
    ASSERT_FALSE(item_is_error(second));
    EXPECT_EQ(js_iterator_result_value(second).item, ITEM_JS_UNDEFINED);
    EXPECT_EQ(js_iterator_result_done(second).item, b2it(true));
    EXPECT_EQ(state->ast_replay_skip, 1);

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ResumesSuspendedExpressionsWithoutRepeatingEffects) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let generatorCount = 0; function* generator() { return ++generatorCount + (yield 10); } "
        "let iterator = generator(); let first = iterator.next(); let second = iterator.next(5); "
        "let asyncCount = 0; async function asyncValue() { "
        "return (++asyncCount === 1) ? await Promise.resolve(5) : 99; } "
        "[first.value, second.value, second.done, generatorCount, asyncValue(), asyncCount];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "suspended-expression-replay.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), flt2it(10.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), flt2it(6.0)).item,
        b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 2).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 3), flt2it(1.0)).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 5), flt2it(1.0)).item,
        b2it(true));

    Item awaited = js_await_sync_incremental(js_elements_get_int(result, 4));
    ASSERT_FALSE(item_is_error(awaited));
    EXPECT_EQ(js_strict_equal(awaited, flt2it(5.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ClosesForAwaitIteratorAfterValueAwaitRejection) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "async function run() { let closed = false; let iterator = { "
        "next: function() { return Promise.resolve({ value: Promise.reject('x'), done: false }); }, "
        "return: function() { closed = true; return Promise.resolve({ done: true }); } }; "
        "iterator[Symbol.asyncIterator] = function() { return iterator; }; "
        "try { for await (let value of iterator) {} } catch (error) {} return closed; } run();";
    Item promise = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "for-await-value-rejection.js", NULL);

    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, AwaitsForAwaitIteratorCloseBeforeReturning) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "async function run() { let closed = false; let iterator = { "
        "next: function() { return Promise.resolve({ value: 1, done: false }); }, "
        "return: function() { return Promise.resolve().then(function() { closed = true; return {}; }); } }; "
        "iterator[Symbol.asyncIterator] = function() { return iterator; }; "
        "for await (let value of iterator) { break; } return closed; } run();";
    Item promise = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "for-await-close.js", NULL);

    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesForAwaitCloseCompletionPrecedence) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function iterator(close) { let value = { next: function() { return Promise.resolve({ value: 1, done: false }); }, "
        "return: function() { return close(); } }; "
        "value[Symbol.asyncIterator] = function() { return value; }; return value; } "
        "async function returnCompletion() { try { for await (let value of iterator(function() { return Promise.reject('close'); })) { return 'source'; } } "
        "catch (error) { return error; } } "
        "async function throwCompletion() { try { for await (let value of iterator(function() { return Promise.reject('close'); })) { throw 'source'; } } "
        "catch (error) { return error; } } "
        "async function primitiveCompletion() { try { for await (let value of iterator(function() { return Promise.resolve(1); })) { break; } } "
        "catch (error) { return error instanceof TypeError; } return false; } "
        "async function undefinedCompletion() { try { for await (let value of iterator(function() { return undefined; })) { break; } } "
        "catch (error) { return error instanceof TypeError; } return false; } "
        "async function check() { let returned = await returnCompletion(); "
        "let thrown = await throwCompletion(); let primitive = await primitiveCompletion(); "
        "let undefinedClose = await undefinedCompletion(); "
        "return returned === 'close' && thrown === 'source' && primitive && undefinedClose; } check();";
    Item promise = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "for-await-close-precedence.js", NULL);

    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsMir, AwaitsForAwaitIteratorCloseBeforeReturning) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "async function run() { let closed = false; let iterator = { "
        "next: function() { return Promise.resolve({ value: 1, done: false }); }, "
        "return: function() { return Promise.resolve().then(function() { closed = true; return {}; }); } }; "
        "iterator[Symbol.asyncIterator] = function() { return iterator; }; "
        "for await (let value of iterator) { break; } return closed; } run();";
    Item promise = transpile_js_to_mir(&runtime, source, "for-await-mir-close.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsMir, AwaitsAsyncGeneratorCloseBeforeReturning) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "async function* values() { try { yield 1; } finally { "
        "await Promise.resolve().then(function() { globalThis.generatorClosed = true; }); } } "
        "async function run() { globalThis.generatorClosed = false; "
        "for await (let value of values()) { break; } return globalThis.generatorClosed; } run();";
    Item promise = transpile_js_to_mir(&runtime, source,
        "for-await-mir-async-generator-close.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsMir, ClosesForAwaitIteratorAfterValueAwaitRejection) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "async function run() { let closed = false; let iterator = { "
        "next: function() { return Promise.resolve({ value: Promise.reject('x'), done: false }); }, "
        "return: function() { closed = true; return Promise.resolve({ done: true }); } }; "
        "iterator[Symbol.asyncIterator] = function() { return iterator; }; "
        "try { for await (let value of iterator) {} } catch (error) {} return closed; } run();";
    Item promise = transpile_js_to_mir(&runtime, source,
        "for-await-mir-value-rejection.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsMir, AwaitsNestedForAwaitCloseBeforeReturning) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "function iterator(mark) { let sent = false; let value = { "
        "next: function() { if (sent) return Promise.resolve({ done: true }); sent = true; return Promise.resolve({ value: 1, done: false }); }, "
        "return: function() { return Promise.resolve().then(function() { globalThis[mark] = true; return {}; }); } }; "
        "value[Symbol.asyncIterator] = function() { return value; }; return value; } "
        "async function run() { globalThis.outerClosed = false; globalThis.innerClosed = false; "
        "let outer = iterator('outerClosed'); let inner = iterator('innerClosed'); "
        "for await (let outerValue of outer) { for await (let innerValue of inner) { return 'done'; } } } "
        "async function check() { await run(); return globalThis.outerClosed && globalThis.innerClosed; } check();";
    Item promise = transpile_js_to_mir(&runtime, source,
        "for-await-mir-nested-close.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsMir, AwaitsForAwaitCloseOnLabeledAbruptJumps) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "function iterator(mark) { let sent = false; let value = { "
        "next: function() { if (sent) return Promise.resolve({ done: true }); sent = true; return Promise.resolve({ value: 1, done: false }); }, "
        "return: function() { return Promise.resolve().then(function() { globalThis[mark] = true; return {}; }); } }; "
        "value[Symbol.asyncIterator] = function() { return value; }; return value; } "
        "async function labeledBreak() { globalThis.breakOuter = false; globalThis.breakInner = false; "
        "let outer = iterator('breakOuter'); let inner = iterator('breakInner'); "
        "outer: for await (let outerValue of outer) { for await (let innerValue of inner) { break outer; } } "
        "return globalThis.breakOuter && globalThis.breakInner; } "
        "async function labeledContinue() { globalThis.continueOuter = false; globalThis.continueInner = false; "
        "let outer = iterator('continueOuter'); let inner = iterator('continueInner'); "
        "outer: for await (let outerValue of outer) { for await (let innerValue of inner) { continue outer; } } "
        "return !globalThis.continueOuter && globalThis.continueInner; } "
        "async function check() { return await labeledBreak() && await labeledContinue(); } check();";
    Item promise = transpile_js_to_mir(&runtime, source,
        "for-await-mir-labeled-close.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsMir, PreservesForAwaitCloseCompletionPrecedence) {
    Runtime runtime = {};
    runtime_init(&runtime);
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "mir", 1), 0);

    const char source[] =
        "function iterator(close) { let value = { next: function() { return Promise.resolve({ value: 1, done: false }); }, "
        "return: function() { return close(); } }; "
        "value[Symbol.asyncIterator] = function() { return value; }; return value; } "
        "async function returnCompletion() { try { for await (let value of iterator(function() { return Promise.reject('close'); })) { return 'source'; } } "
        "catch (error) { return error; } } "
        "async function throwCompletion() { try { for await (let value of iterator(function() { return Promise.reject('close'); })) { throw 'source'; } } "
        "catch (error) { return error; } } "
        "async function primitiveCompletion() { try { for await (let value of iterator(function() { return Promise.resolve(1); })) { break; } } "
        "catch (error) { return error instanceof TypeError; } return false; } "
        "async function undefinedCompletion() { try { for await (let value of iterator(function() { return undefined; })) { break; } } "
        "catch (error) { return error instanceof TypeError; } return false; } "
        "async function check() { let returned = await returnCompletion(); "
        "let thrown = await throwCompletion(); let primitive = await primitiveCompletion(); "
        "let undefinedClose = await undefinedCompletion(); "
        "return [returned, thrown, primitive, undefinedClose]; } check();";
    Item promise = transpile_js_to_mir(&runtime, source,
        "for-await-mir-close-precedence.js", NULL);

    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);
    ASSERT_FALSE(item_is_error(promise));
    Item result = js_await_sync_incremental(promise);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0),
        js_make_string("close")).item, b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1),
        js_make_string("source")).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 2).item, b2it(true));
    EXPECT_EQ(js_elements_get_int(result, 3).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DirectEvalSharesInterpretedFunctionEnvironment) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function mutate() { let value = 40; eval('value += 2'); return value; } mutate();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "direct-eval.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    const char introduced_source[] =
        "function introduce() { eval('var value = 20'); value += 2; return value; } introduce();";
    Item introduced = js_interp_execute_source(&runtime, introduced_source,
        sizeof(introduced_source) - 1, "direct-eval-var.js", NULL);

    ASSERT_FALSE(item_is_error(introduced));
    EXPECT_EQ(js_strict_equal(introduced, flt2it(22.0)).item, b2it(true));

    const char global_source[] = "let value = 40; eval('value += 2'); value;";
    Item global = js_interp_execute_source(&runtime, global_source,
        sizeof(global_source) - 1, "direct-eval-global.js", NULL);

    ASSERT_FALSE(item_is_error(global));
    EXPECT_EQ(js_strict_equal(global, flt2it(42.0)).item, b2it(true));

    const char eval_harness_source[] =
        "var __globalObject = Function('return this;')(); "
        "function fnGlobalObject() { return __globalObject; }";
    Item eval_harness = js_interp_execute_source(&runtime, eval_harness_source,
        sizeof(eval_harness_source) - 1, "direct-eval-harness.js", NULL);

    ASSERT_FALSE(item_is_error(eval_harness));

    const char annexb_global_source[] =
        "Object.defineProperty(fnGlobalObject(), 'f', { value: 'x', enumerable: true, "
        "writable: true, configurable: false }); "
        "eval('var global = fnGlobalObject(); if (global !== fnGlobalObject()) throw new Error(); "
        "if (f !== \\\"x\\\") throw new Error(); if (true) function f() {} else function _f() {}'); "
        "global === fnGlobalObject() && typeof globalThis.f === 'function' && "
        "!Object.getOwnPropertyDescriptor(fnGlobalObject(), 'f').configurable;";
    Item annexb_global = js_interp_execute_source(&runtime, annexb_global_source,
        sizeof(annexb_global_source) - 1, "direct-eval-annexb.js", NULL);

    ASSERT_FALSE(item_is_error(annexb_global));
    EXPECT_EQ(annexb_global.item, b2it(true));

    const char annexb_else_source[] =
        "Object.defineProperty(fnGlobalObject(), 'f', { value: 'x', enumerable: true, "
        "writable: true, configurable: false }); "
        "eval('var global = fnGlobalObject(); if (global !== fnGlobalObject()) throw new Error(); "
        "if (f !== \\\"x\\\") throw new Error(); if (false) function _f() {} else function f() {}'); "
        "global === fnGlobalObject() && typeof globalThis.f === 'function' && "
        "!Object.getOwnPropertyDescriptor(fnGlobalObject(), 'f').configurable;";
    Item annexb_else = js_interp_execute_source(&runtime, annexb_else_source,
        sizeof(annexb_else_source) - 1, "direct-eval-annexb-else.js", NULL);

    ASSERT_FALSE(item_is_error(annexb_else));
    EXPECT_EQ(annexb_else.item, b2it(true));

    const char indirect_source[] =
        "function indirect() { let local = 40; let saved = eval; return saved('typeof local'); } "
        "indirect();";
    Item indirect = js_interp_execute_source(&runtime, indirect_source,
        sizeof(indirect_source) - 1, "indirect-eval.js", NULL);

    ASSERT_FALSE(item_is_error(indirect));
    ASSERT_EQ(get_type_id(indirect), LMD_TYPE_STRING);
    EXPECT_STREQ(it2s(indirect)->chars, "undefined");

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DirectEvalAnnexBUsesRetainedTest262Assertions) {
    Runtime runtime = {};
    runtime_init(&runtime);

    StrBuf* harness = strbuf_new();
    ASSERT_NE(harness, nullptr);
    ASSERT_TRUE(js_test262_append_file(harness, "ref/test262/harness/sta.js"));
    ASSERT_TRUE(js_test262_append_file(harness, "ref/test262/harness/assert.js"));
    ASSERT_TRUE(js_test262_append_file(harness,
        "ref/test262/harness/nativeFunctionMatcher.js"));
    Item harness_result = js_interp_execute_source(&runtime, harness->str,
        harness->length, "test262-harness.js", NULL);
    strbuf_free(harness);
    ASSERT_FALSE(item_is_error(harness_result));

    StrBuf* source = strbuf_create("globalThis.__lambda_can_block = true;\n");
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/fnGlobalObject.js"));
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/propertyHelper.js"));
    ASSERT_TRUE(js_test262_append_file(source,
        "ref/test262/test/annexB/language/eval-code/direct/"
        "global-if-decl-else-decl-b-eval-global-existing-global-init.js"));
    Item result = js_interp_execute_source(&runtime, source->str, source->length,
        "test262-annexb-direct-eval.js", NULL);
    strbuf_free(source);

    EXPECT_FALSE(item_is_error(result));
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ReadsNewTargetThroughTheSharedConstructCallKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function Base() { let lexical = () => new.target; "
        "this.answer = (new.target === Base ? 40 : 0) + "
        "(lexical() === Base ? 2 : 0); } new Base().answer;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "new-target.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ExecutesSuperThroughTheSharedClassCallKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Parent { constructor(value) { this.value = value; } "
        "get doubled() { return this.value * 2; } "
        "set doubled(value) { this.value = value / 2; } "
        "add(value) { return this.value + value; } "
        "static increment(value) { return value + 1; } } "
        "class Child extends Parent { field = this.value + 1; "
        "constructor(value) { super(value); this.doubled = 84; } "
        "add(value) { return super.add(value) + 1; } "
        "read() { return super.doubled; } "
        "fromArrow() { let parent = () => super.add(0); return parent(); } "
        "static increment(value) { return super.increment(value) + 1; } } "
        "let child = new Child(40); child.add(0) + child.read() + child.fromArrow() + child.field + "
        "Child.increment(0);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "super.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(212.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ConstructsPromiseSubclassesThroughAstSuperCalls) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let calls = 0; class Child extends Promise { constructor(executor) { "
        "return super(executor); } } new Child(function(resolve) { calls++; resolve(); }); calls;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "promise-super.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(1.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesObjectMethodSuperHomeAcrossTheSharedCallKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let parent = { value: 40, add(value) { return this.value + value; } }; "
        "let child = { value: 40, add(value) { return super.add(value) + 2; } }; "
        "Object.setPrototypeOf(child, parent); child.add(0);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "object-super.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainsArgumentsAcrossNestedCallsAndEscapedArrows) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function collect(first) { let read = () => arguments[0] + arguments.length; "
        "first = 40; arguments[0] = 41; return (arguments.callee === collect ? 0 : 100) + "
        "read() + first; } "
        "function defaults(value = arguments.length + 2) { return value; } "
        "function strictValue(value) { 'use strict'; value = 40; return arguments[0]; } "
        "collect(20) + defaults() + strictValue(20);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "arguments.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(105.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, EnumeratesOnlySuppliedMappedArguments) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function keys(a, b, c) { a = 40; b = 50; c = 60; "
        "return Object.keys(arguments).join(',') + ':' + arguments.length; } "
        "[keys(), keys(1, 2), keys(1, 2, 3), keys(1, 2, 3, 4)].join('|');";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "arguments-enumeration.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_TRUE(js_strict_equal(result, js_make_string(
        ":0|0,1:2|0,1,2:3|0,1,2,3:4")).item == b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, SharesPrivateClassElementsWithTheRuntimeKernel) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Vault { #value = 2; #increment() { this.#value++; return this.#value; } "
        "get value() { return this.#increment(); } static #counter = 3; "
        "static #read() { return this.#counter; } static run() { let later = () => this.#read(); "
        "return later(); } probe(other) { return #value in other; } "
        "nested() { function read(instance) { return instance.#value; } return read(this); } "
        "evalRead() { return eval('this.#value'); } } "
        "class PrivateAccess { #slot = 4; get #value() { return this.#slot; } "
        "set #value(next) { this.#slot = next; } write() { this.#value = 8; return this.#value; } } "
        "let vault = new Vault(); let access = new PrivateAccess(); vault.value + Vault.run() + "
        "(vault.probe(vault) ? 10 : 0) + vault.nested() + vault.evalRead() + access.write();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "private-class.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(30.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, ResolvesPrivateNamesThroughNestedClassEnvironments) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class Outer { #value = 7; #twice() { return this.#value * 2; } "
        "Inner = class { read(outer) { return outer.#value + outer.#twice(); } } } "
        "let outer = new Outer(); new outer.Inner().read(outer);";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "nested-private-class.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(21.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, DistinguishesInheritedPrivateFieldNames) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "class A { #x = 'A'; read() { return this.#x; } } "
        "class B extends A { #x = 'B'; read() { return this.#x; } } "
        "new B().read();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "inherited-private-fields.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, js_make_string("B")).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, NamesAnonymousClassesBeforeStaticInitializers) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var observed; var C = class { static field = (observed = this.name); }; "
        "[observed, C.name];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "anonymous-class-static-name.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 0), js_make_string("C")).item,
        b2it(true));
    EXPECT_EQ(js_strict_equal(js_elements_get_int(result, 1), js_make_string("C")).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesThisForStaticFieldDirectEval) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var C = class { static f = 'test'; static g = this.f + '262'; "
        "static h = eval('this.g') + 'test'; }; C.h;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "static-field-direct-eval-this.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, js_make_string("test262test")).item,
        b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, UsesCreateDataPropertyForJsonCallbackHolders) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "Object.defineProperty(Object.prototype, '', { set: function() { "
        "throw new Error('setter called'); }, configurable: true }); "
        "let parsedHolder; JSON.parse('2', function() { parsedHolder = this; }); "
        "let stringifiedHolder; let value = {}; JSON.stringify(value, function() { "
        "stringifiedHolder = this; }); "
        "let parsed = Object.getOwnPropertyDescriptor(parsedHolder, ''); "
        "let stringified = Object.getOwnPropertyDescriptor(stringifiedHolder, ''); "
        "let parsedDeleted = delete parsedHolder['']; "
        "let stringifiedDeleted = delete stringifiedHolder['']; "
        "(Object.getPrototypeOf(parsedHolder) === Object.prototype && parsed.value === 2 && "
        "parsed.writable && parsed.enumerable && parsed.configurable && "
        "Object.getPrototypeOf(stringifiedHolder) === Object.prototype && "
        "stringified.value === value && stringified.writable && stringified.enumerable && "
        "stringified.configurable && parsedDeleted && stringifiedDeleted && "
        "!Object.prototype.hasOwnProperty.call(parsedHolder, '') && "
        "!Object.prototype.hasOwnProperty.call(stringifiedHolder, '')) ? 42 : 0;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "json-callback-holder.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(42.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RunsJsonReviverWrapperThroughRetainedTest262Harness) {
    Runtime runtime = {};
    runtime_init(&runtime);

    StrBuf* harness = strbuf_new();
    ASSERT_NE(harness, nullptr);
    ASSERT_TRUE(js_test262_append_file(harness, "ref/test262/harness/sta.js"));
    ASSERT_TRUE(js_test262_append_file(harness, "ref/test262/harness/assert.js"));
    JsScript* retained = js_interp_prepare_script(&runtime, harness->str,
        harness->length, "test262-harness.js");
    strbuf_free(harness);
    ASSERT_NE(retained, nullptr);
    ASSERT_FALSE(item_is_error(js_interp_execute_script(&runtime, retained, NULL)));

    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    strbuf_append_str(source, "let testResult = 'ok'; try {\n");
    ASSERT_TRUE(js_test262_append_file(source, "ref/test262/harness/propertyHelper.js"));
    ASSERT_TRUE(js_test262_append_file(source,
        "ref/test262/test/built-ins/JSON/parse/reviver-wrapper.js"));
    strbuf_append_str(source,
        "\n} catch (error) { testResult = error.name + ': ' + error.message; } testResult;");
    Item result = js_interp_execute_source(&runtime, source->str, source->length,
        "json-reviver-wrapper.js", NULL);
    strbuf_free(source);

    ASSERT_FALSE(item_is_error(result));
    ASSERT_EQ(get_type_id(result), LMD_TYPE_STRING);
    EXPECT_STREQ(it2s(result)->chars, "ok");
    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PreservesExactMethodSourceText) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let computed = { /* before */[ /* a */ \"f\" /* b */ ] /* c */ "
        "( /* d */ ) /* e */ { /* f */ }/* after */ }.f; "
        "class C { /* before */#instance /* a */ ( /* b */ ) /* c */ "
        "{ /* d */ }/* after */ getInstance() { return this.#instance; } "
        "/* before */static #statik /* a */ ( /* b */ ) /* c */ "
        "{ /* d */ }/* after */ static getStatic() { return this.#statik; } } "
        "let instance = new C(); [computed.toString(), instance.getInstance().toString(), "
        "C.getStatic().toString()];";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "method-source.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_STREQ(it2s(js_elements_get_int(result, 0))->chars,
        "[ /* a */ \"f\" /* b */ ] /* c */ ( /* d */ ) /* e */ { /* f */ }");
    EXPECT_STREQ(it2s(js_elements_get_int(result, 1))->chars,
        "#instance /* a */ ( /* b */ ) /* c */ { /* d */ }");
    EXPECT_STREQ(it2s(js_elements_get_int(result, 2))->chars,
        "#statik /* a */ ( /* b */ ) /* c */ { /* d */ }");

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, RetainsPrivateClassIdentityForEscapedClosuresAcrossGc) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "function makeReader() { class Box { #value = 7; reader() { return () => this.#value; } } "
        "return new Box().reader(); } var reader = makeReader(); reader;";
    Item executed = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "private-closure.js", NULL);

    ASSERT_FALSE(item_is_error(executed));
    JsScript* script = (JsScript*)runtime.scripts->data[0];
    NameEntry* reader_entry = nullptr;
    for (NameEntry* entry = script->global_scope->first; entry; entry = entry->next) {
        if (entry->name && entry->name->len == 6 &&
                memcmp(entry->name->chars, "reader", 6) == 0) {
            reader_entry = entry;
            break;
        }
    }
    ASSERT_NE(reader_entry, nullptr);
    Item reader = lambda_active_module_var_at((uint32_t)reader_entry->slot);
    heap_gc_collect();
    Item result = js_call_function(reader, make_js_undefined(), NULL, 0);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(7.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}

TEST(JsInterpreter, PublishesNavigatorServiceWorkerRegistrations) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "var worker = navigator.serviceWorker; "
        "typeof worker.getRegistrations === 'function' && "
        "worker.getRegistrations() instanceof Promise;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "navigator-service-worker.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, b2it(true));

    runtime_cleanup(&runtime);
}
