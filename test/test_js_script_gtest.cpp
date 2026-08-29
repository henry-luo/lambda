#include <gtest/gtest.h>

#include "../lib/file.h"
#include "../lib/strbuf.h"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/module_registry.h"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_interp.hpp"

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
    ASSERT_TRUE(js_transpiler_parse(tp, source, sizeof(source) - 1));

    TSNode root = ts_tree_root_node(tp->tree);
    JsAstNode* ast = build_js_ast_indexed(tp, root);
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
    Item counter = js_get_module_var(counter_entry->slot);
    heap_gc_collect();
    Item increment = flt2it(2.0);
    Item after_gc = js_call_function(counter, make_js_undefined(), &increment, 1);
    ASSERT_FALSE(item_is_error(after_gc));
    EXPECT_EQ(js_strict_equal(after_gc, flt2it(44.0)).item, b2it(true));

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

TEST(JsInterpreter, SupportsModuleMetadataAndDynamicImports) {
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
    ASSERT_EQ(setenv("JS_EXECUTION_BACKEND", "ast", 1), 0);
    Item result = transpile_js_to_mir(&runtime, source,
        "test/js/interp_esm/dynamic.js", NULL);
    ASSERT_EQ(unsetenv("JS_EXECUTION_BACKEND"), 0);

    ASSERT_FALSE(item_is_error(result));
    Item dynamic_value = js_get_key_default(js_get_global_this(),
        js_make_string("__interp_dynamic_counter"));
    EXPECT_EQ(js_strict_equal(dynamic_value, flt2it(40.0)).item, b2it(true));
    ASSERT_NE(module_get_for_runtime(&runtime, "test/js/interp_esm/dep.mjs"),
        nullptr);

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

TEST(JsInterpreter, KeepsDeclarationIdentityAndPerIterationClosures) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] =
        "let declaration = identity; function identity() { return 1; } "
        "let first; let second; let third; "
        "for (let i = 0; i < 3; i++) { "
        "if (i === 0) first = () => i; "
        "if (i === 1) second = () => i; "
        "if (i === 2) third = () => i; } "
        "(declaration === identity ? 0 : 1000) + first() * 100 + second() * 10 + third();";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "iteration-closure.js", NULL);

    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(12.0)).item, b2it(true));

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

TEST(JsInterpreter, RejectsUnsupportedFormsBeforeExecution) {
    Runtime runtime = {};
    runtime_init(&runtime);

    const char source[] = "let sideEffect = 0; async function* work() { yield sideEffect; } sideEffect;";
    Item result = js_interp_execute_source(&runtime, source, sizeof(source) - 1,
        "unsupported-async.js", NULL);

    EXPECT_TRUE(item_is_error(result));
    ASSERT_NE(runtime.scripts, nullptr);
    EXPECT_EQ(runtime.scripts->length, 1);
    EXPECT_NE(runtime.eval_context, nullptr);
    EXPECT_EQ(js_global_binding_exists(js_make_string("sideEffect")), 0);

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
    Item reader = js_get_module_var(reader_entry->slot);
    heap_gc_collect();
    Item result = js_call_function(reader, make_js_undefined(), NULL, 0);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(js_strict_equal(result, flt2it(7.0)).item, b2it(true));

    runtime_cleanup(&runtime);
}
