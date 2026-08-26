#include <gtest/gtest.h>

#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/module_registry.h"
#include "../lambda/js/js_transpiler.hpp"
#include "../lambda/js/js_interp.hpp"

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

    const char source[] = "let sideEffect = 0; async function work() { return sideEffect; } sideEffect;";
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
