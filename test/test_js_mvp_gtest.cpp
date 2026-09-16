#include <gtest/gtest.h>

#include "lambda/js/mvp/mvp.h"

#include <math.h>
#include <string.h>

TEST(JsMvpValueTest, NumbersKeepJsNumberDetails) {
    MvpValue negative_zero = mvp_value_from_number(-0.0);
    MvpValue infinity = mvp_value_from_number(INFINITY);
    MvpValue nan = mvp_value_from_number(NAN);

    EXPECT_TRUE(mvp_value_is_number(negative_zero));
    EXPECT_EQ(signbit(mvp_value_to_number(negative_zero)), 1);
    EXPECT_TRUE(isinf(mvp_value_to_number(infinity)));
    EXPECT_TRUE(mvp_value_is_nan(nan));
    EXPECT_FALSE(mvp_value_is_reference(nan));
}

TEST(JsMvpValueTest, ReferenceAndCompletionPayloadsRoundTrip) {
    MvpHeap heap = {};
    mvp_heap_init(&heap);
    MvpHeapObject* payload = mvp_heap_alloc_values(&heap, 0);
    ASSERT_NE(payload, nullptr);

    MvpValue reference = mvp_value_reference(payload);
    MvpValue completion = mvp_value_completion(MVP_COMPLETION_THROW, payload);
    EXPECT_EQ(mvp_value_reference_object(reference), payload);
    EXPECT_TRUE(mvp_value_is_completion(completion));
    EXPECT_EQ(mvp_value_completion_kind(completion), MVP_COMPLETION_THROW);
    EXPECT_EQ(mvp_value_completion_payload(completion), payload);
    mvp_heap_destroy(&heap);
}

TEST(JsMvpHeapTest, OnlyPreciseRootsRetainValues) {
    MvpHeap heap = {};
    mvp_heap_init(&heap);
    heap.force_every_allocation = 1;

    MvpHeapObject* parent = mvp_heap_alloc_values(&heap, 1);
    ASSERT_NE(parent, nullptr);
    MvpValue roots[1] = {mvp_value_reference(parent)};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&heap, &frame, roots, 1);

    MvpHeapObject* child = mvp_heap_alloc_values(&heap, 0);
    ASSERT_NE(child, nullptr);
    mvp_heap_object_values(parent)[0] = mvp_value_reference(child);
    mvp_heap_collect(&heap);
    EXPECT_EQ(heap.object_count, (size_t)2);

    mvp_root_frame_pop(&heap, &frame);
    mvp_heap_collect(&heap);
    EXPECT_EQ(heap.object_count, (size_t)0);
    mvp_heap_destroy(&heap);
}

TEST(JsMvpHeapTest, PrivateAggregatesTraceAndPreserveValues) {
    MvpHeap heap = {};
    mvp_heap_init(&heap);
    heap.force_every_allocation = 1;

    MvpValue roots[3] = {mvp_array_new(&heap, 0), mvp_value_undefined(),
        mvp_value_undefined()};
    ASSERT_TRUE(mvp_value_is_array(roots[0]));
    MvpRootFrame frame = {};
    mvp_root_frame_push(&heap, &frame, roots, 3);

    roots[1] = mvp_string_new(&heap, "answer", 6);
    roots[2] = mvp_object_new(&heap, mvp_value_null());
    ASSERT_TRUE(mvp_value_is_string(roots[1]));
    ASSERT_TRUE(mvp_value_is_object(roots[2]));
    ASSERT_EQ(mvp_object_set(&heap, roots[2], roots[1], mvp_value_from_number(42)), 1);
    ASSERT_EQ(mvp_array_push(&heap, roots[0], roots[2]), 1);
    mvp_heap_collect(&heap);

    MvpValue retained = mvp_array_get(roots[0], 0);
    EXPECT_EQ(mvp_value_to_number(mvp_object_get(retained, roots[1])), 42.0);
    mvp_root_frame_pop(&heap, &frame);
    mvp_heap_collect(&heap);
    EXPECT_EQ(heap.object_count, (size_t)0);
    mvp_heap_destroy(&heap);
}

TEST(JsMvpHeapTest, NamedFieldsUseThePrivatePropertyIndex) {
    MvpHeap heap = {};
    mvp_heap_init(&heap);
    heap.force_every_allocation = 1;
    MvpValue roots[2] = {mvp_value_undefined(), mvp_value_undefined()};
    MvpRootFrame frame = {};
    mvp_root_frame_push(&heap, &frame, roots, 2);

    roots[0] = mvp_object_new(&heap, mvp_value_null());
    roots[1] = mvp_string_new(&heap, "answer", 6);
    ASSERT_TRUE(mvp_value_is_object(roots[0]));
    ASSERT_EQ(mvp_object_set(&heap, roots[0], roots[1], mvp_value_from_number(42)), 1);

    MvpValue value = mvp_value_undefined();
    ASSERT_EQ(mvp_object_get_named(roots[0], "answer", 6, &value), 1);
    EXPECT_EQ(mvp_value_to_number(value), 42.0);
    ASSERT_EQ(mvp_object_set_named_existing(roots[0], "answer", 6,
        mvp_value_from_number(7)), 1);
    ASSERT_EQ(mvp_object_get_named(roots[0], "answer", 6, &value), 1);
    EXPECT_EQ(mvp_value_to_number(value), 7.0);
    EXPECT_EQ(mvp_object_set_named_existing(roots[0], "missing", 7,
        mvp_value_from_number(0)), 0);
    mvp_root_frame_pop(&heap, &frame);
    mvp_heap_destroy(&heap);
}

TEST(JsMvpValueTest, NewSemanticOperationsUsePrivateHeap) {
    MvpExecution execution = {};
    mvp_execution_init(&execution, 4);
    ASSERT_FALSE(mvp_execution_has_error(&execution));
    MvpValue text = mvp_string_new(&execution.heap, "value=", 6);
    mvp_execution_set_root(&execution, 0, text.bits);
    MvpValue joined = {mvp_op_add(&execution, text.bits, mvp_value_from_number(42).bits)};
    mvp_execution_set_root(&execution, 1, joined.bits);

    size_t length = 0;
    const char* bytes = mvp_string_bytes(joined, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)8);
    EXPECT_EQ(memcmp(bytes, "value=42", 8), 0);
    EXPECT_TRUE(mvp_value_truthy(joined));
    EXPECT_EQ(mvp_op_strict_equal(&execution, joined.bits,
        mvp_string_new(&execution.heap, "value=42", 8).bits), mvp_value_bool(1).bits);
    mvp_execution_destroy(&execution);
}

TEST(JsMvpHeapTest, ExecutionLiteralCacheRootsPrivateStrings) {
    const char text[] = "member";
    const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    MvpExecution execution = {};
    mvp_execution_init(&execution, 0);
    uint64_t first = mvp_execution_literal_string(&execution, (uint64_t)(uintptr_t)text,
        sizeof(text) - 1);
    uint64_t second = mvp_execution_literal_string(&execution, (uint64_t)(uintptr_t)text,
        sizeof(text) - 1);
    ASSERT_TRUE(mvp_value_is_string((MvpValue){first}));
    EXPECT_EQ(first, second);
    for (size_t index = 0; index < sizeof(alphabet) - 1; index++) {
        uint64_t value = mvp_execution_literal_string(&execution,
            (uint64_t)(uintptr_t)&alphabet[index], 1);
        ASSERT_TRUE(mvp_value_is_string((MvpValue){value}));
    }
    EXPECT_EQ(mvp_execution_literal_string(&execution, (uint64_t)(uintptr_t)text,
        sizeof(text) - 1), first);
    mvp_heap_collect(&execution.heap);
    size_t length = 0;
    const char* cached = mvp_string_bytes((MvpValue){first}, &length);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(length, sizeof(text) - 1);
    EXPECT_EQ(memcmp(cached, text, length), 0);
    mvp_execution_destroy(&execution);
}

TEST(JsMvpHeapTest, ExecutionFramePoolClearsReleasedPreciseRoots) {
    MvpExecution execution = {};
    mvp_execution_init(&execution, 0);
    size_t baseline_objects = execution.heap.object_count;
    MvpValue* first = mvp_execution_enter_frame(&execution, 2);
    ASSERT_NE(first, nullptr);
    first[0] = mvp_string_new(&execution.heap, "temporary", 9);
    ASSERT_TRUE(mvp_value_is_string(first[0]));
    mvp_execution_leave_frame(&execution);
    mvp_heap_collect(&execution.heap);
    EXPECT_EQ(execution.heap.object_count, baseline_objects);

    MvpValue* reused = mvp_execution_enter_frame(&execution, 2);
    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(reused, first);
    EXPECT_TRUE(mvp_value_is_undefined(reused[0]));
    EXPECT_TRUE(mvp_value_is_undefined(reused[1]));
    mvp_execution_leave_frame(&execution);
    mvp_execution_destroy(&execution);
}

TEST(JsMvpMirTest, ParserAstAndNewMirExecuteLiteral) {
    const char source[] = "42";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_TRUE(mvp_value_is_number(result.value));
    EXPECT_EQ(mvp_value_to_number(result.value), 42.0);
}

TEST(JsMvpMirTest, NumericFunctionCallsLowerDirectlyToMir) {
    const char source[] =
        "function fib(n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }\n"
        "fib(20);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    ASSERT_TRUE(mvp_value_is_number(result.value));
    EXPECT_EQ(mvp_value_to_number(result.value), 6765.0);
}

TEST(JsMvpMirTest, NumericCountedLoopLowersDirectlyToMir) {
    const char source[] =
        "function sum(n) { let total = 0; for (let i = 0; i < n; i++) total += i; return total; }\n"
        "sum(100);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    ASSERT_TRUE(mvp_value_is_number(result.value));
    EXPECT_EQ(mvp_value_to_number(result.value), 4950.0);
}

TEST(JsMvpMirTest, GenericDirectMirKeepsPrivateStringResultsAlive) {
    const char source[] =
        "function label(n) { let prefix = 'value='; return prefix + n; }\n"
        "label(42);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    ASSERT_NE(result.execution, nullptr);
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)8);
    EXPECT_EQ(memcmp(bytes, "value=42", 8), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirRunsBenchmarkStyleTimingWrapper) {
    const char source[] =
        "function fib(n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }\n"
        "function main() {\n"
        "  const start = process.hrtime.bigint();\n"
        "  const result = fib(20);\n"
        "  const end = process.hrtime.bigint();\n"
        "  if (result === 6765) process.stdout.write('fib: PASS\\n');\n"
        "  process.stdout.write('__TIMING__:' + Number(end - start) / 1e6 + '\\n');\n"
        "}\n"
        "main();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    ASSERT_NE(result.execution, nullptr);
    EXPECT_TRUE(mvp_value_is_undefined(result.value));
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirBuildsPrivateObjectAndArrayLiterals) {
    const char source[] = "({ value: [3, 5, 8] }).value[1];";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 5.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirBuildsNestedStaticLiterals) {
    const char source[] = "({ table: [{ 1: 'one', key: undefined }] }).table[0]['1'];";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* text = mvp_string_bytes(result.value, &length);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(length, 3u);
    EXPECT_EQ(memcmp(text, "one", length), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirFillsPrivateArrays) {
    const char source[] = "let items = new Array(3); items.fill(7); items[2];";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 7.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirIteratesPrivateArrays) {
    const char source[] =
        "function sum(items) { let total = 0; for (const item of items) total += item; "
        "return total; }\n"
        "sum([2, 3, 5]);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 10.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesLoopBreakAndContinueTargets) {
    const char source[] =
        "function sum(items) {\n"
        "  let total = 0;\n"
        "  for (let i = 0; i < items.length; i++) {\n"
        "    if (i === 2) continue;\n"
        "    if (i === 4) break;\n"
        "    total += items[i];\n"
        "  }\n"
        "  return total;\n"
        "}\n"
        "sum([3, 5, 7, 11, 13]);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 19.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirPreservesConditionalValueSelection) {
    const char source[] = "let answer = 2 < 3 ? 'yes' : 'no'; answer;";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)3);
    EXPECT_EQ(memcmp(bytes, "yes", 3), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirSharesTopLevelStateWithFunctions) {
    const char source[] =
        "const limit = parseInt(process.argv[2] || '4');\n"
        "let total = 2;\n"
        "function addLimit() { total += limit; return total; }\n"
        "addLimit();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 6.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirCreatesNamedFunctionValuesForNew) {
    const char source[] =
        "function Pair(left, right) { this.total = left + right; }\n"
        "new Pair(4, 9).total;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 13.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirLowersSwitchSelectionAndBreak) {
    const char source[] =
        "function choose(value) {\n"
        "  switch (value) {\n"
        "    case 1: return 7;\n"
        "    case 2: break;\n"
        "    default: return 9;\n"
        "  }\n"
        "  return 3;\n"
        "}\n"
        "choose(2);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 3.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesPrivateClassesAndReceiverCalls) {
    const char source[] =
        "class Base {\n"
        "  constructor(value) { this.value = value; }\n"
        "  add(step) { this.value += step; return this.value; }\n"
        "}\n"
        "class Child extends Base {\n"
        "  constructor(value) { super(value); }\n"
        "  twice(step) { return this.add(step) + this.add(step); }\n"
        "}\n"
        "let child = new Child(3);\n"
        "child.twice(2);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 12.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirResolvesSuperFromMethodHomePrototype) {
    const char source[] =
        "class Root {}\n"
        "class Middle extends Root {\n"
        "  constructor() { super(); this.middle = 2; }\n"
        "}\n"
        "class Leaf extends Middle {\n"
        "  constructor() { super(); this.leaf = 3; }\n"
        "}\n"
        "let leaf = new Leaf();\n"
        "leaf.middle + leaf.leaf;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 5.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirConstructsFourArgumentClasses) {
    const char source[] =
        "class Total {\n"
        "  constructor(a, b, c, d) { this.value = a + b + c + d; }\n"
        "  get() { return this.value; }\n"
        "}\n"
        "new Total(2, 3, 5, 7).get();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 17.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirAcceptsBareAssignmentStatements) {
    const char source[] =
        "class Counter {\n"
        "  constructor() { this.value = 1; }\n"
        "  add() { this.value += 4; return this.value; }\n"
        "}\n"
        "new Counter().add();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 5.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesPrivateBitwiseOrAndLeftShift) {
    const char source[] = "((5 << 3) | 2) ^ 7;";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 45.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesStaticMethodsAndClassGlobals) {
    const char source[] =
        "class Box { constructor(value) { this.value = value; } }\n"
        "class Factory { static make(value) { return new Box(value); } }\n"
        "Factory.make(9).value;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 9.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirExecutesNestedCallable) {
    const char source[] =
        "class Holder {\n"
        "  value() { return (function() { return 3; })(); }\n"
        "}\n"
        "new Holder().value();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 3.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirRunsArrowClosureWithMutableCapture) {
    const char source[] =
        "class Runner { invoke(fn) { return fn(4); } }\n"
        "function sum() {\n"
        "  let total = 3;\n"
        "  new Runner().invoke((value) => { total += value; });\n"
        "  return total;\n"
        "}\n"
        "sum();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 7.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirRunsArrayForEachWithArrowClosure) {
    const char source[] =
        "function sum() {\n"
        "  let total = 0;\n"
        "  [2, 3, 5].forEach((value) => { total += value; });\n"
        "  return total;\n"
        "}\n"
        "sum();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 10.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesPrivateBenchmarkCollectionAndStringMethods) {
    const char source[] =
        "let text = '{\"head\":{}}';\n"
        "let copied = '';\n"
        "for (let index = 0; index < text.length; index += 1) {\n"
        "  copied += text.substring(index, index + 1);\n"
        "}\n"
        "copied.slice(2, 6);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)4);
    EXPECT_STREQ(bytes, "head");
    EXPECT_EQ(memcmp(bytes, "head", 4), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirReturnsThroughNestedClassMethods) {
    const char source[] =
        "class Relay {\n"
        "  readName() { return this.readStringInternal(); }\n"
        "  readStringInternal() { return 'called'; }\n"
        "  readObject() { return this.readName(); }\n"
        "}\n"
        "new Relay().readObject();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)6);
    EXPECT_EQ(memcmp(bytes, "called", 6), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirPreservesDoWhileFirstIterationAndContinueTest) {
    const char source[] =
        "function count() {\n"
        "  let value = 0;\n"
        "  do {\n"
        "    value += 1;\n"
        "    if (value === 1) continue;\n"
        "    value += 3;\n"
        "  } while (value < 3);\n"
        "  return value;\n"
        "}\n"
        "count();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 5.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirResizesPrivateArraysThroughLength) {
    const char source[] =
        "let values = [3, 5];\n"
        "values.length = 4;\n"
        "values[2] = 8;\n"
        "values.length = 1;\n"
        "values.length;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 1.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirDestructuresArrayIterationAndAssignment) {
    const char source[] =
        "let total = 0;\n"
        "for (const [left, right] of [[2, 3], [5, 7]]) {\n"
        "  total += left * right;\n"
        "}\n"
        "let first = 11;\n"
        "let second = 13;\n"
        "[first, second] = [second, first];\n"
        "total + first - second;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 43.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirCapturesNestedDeclarationsAndExpressions) {
    const char source[] =
        "function addBase(base) {\n"
        "  function add(value) { return base + value; }\n"
        "  return add(5);\n"
        "}\n"
        "const triple = function (value) { return value * 3; };\n"
        "addBase(7) + triple(4);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 24.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirAppliesDefaultParametersAtDirectCalls) {
    const char source[] =
        "function value(left, right = 5, third = left + right) {\n"
        "  return left + right + third;\n"
        "}\n"
        "value(2);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 14.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirSupportsPrivateGlobalAndStaticCollectionHelpers) {
    const char source[] =
        "globalThis.shared = [3];\n"
        "Object.defineProperty(globalThis, 'answer', { value: 9 });\n"
        "Array.isArray(globalThis.shared) ? globalThis.answer : 0;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 9.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirEnumeratesPrivateObjectAndArrayKeys) {
    const char source[] =
        "let total = 0;\n"
        "let values = { left: 3, right: 5 };\n"
        "for (const key in values) total += key === 'left' ? 3 : 5;\n"
        "for (const key in [7, 9]) total += +key;\n"
        "total + (('left' in values && !('missing' in values)) ? 1 : 0);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 10.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesPrivateArrayHigherOrderMethodsAndApply) {
    const char source[] =
        "function collect() {\n"
        "  let values = [];\n"
        "  values.push.apply(values, [2, 3]);\n"
        "  values.unshift(1);\n"
        "  return values.concat([4]).map((value) => value * 2).join(',') + ':' +\n"
        "    values.includes(2);\n"
        "}\n"
        "collect();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)12);
    EXPECT_EQ(memcmp(bytes, "2,4,6,8:true", 12), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirJoinsPrivateArraysWithNullishItems) {
    const char source[] =
        "let values = ['a', 'b', null, undefined, 'c'];\n"
        "values.join('-');\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)7);
    EXPECT_EQ(memcmp(bytes, "a-b---c", 7), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirHandlesPrivateDatePrototypeAndNanForms) {
    const char source[] =
        "let date = new Date(7);\n"
        "Object.getPrototypeOf(date)?.constructor?.name === 'Date' &&\n"
        "  Number(date) === 7 && !Number.isNaN('x') && isNaN('x');\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(result.value.bits, mvp_value_bool(1).bits);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirReadsBenchmarkInputsThroughPrivateFsHost) {
    const char source[] =
        "require('fs').readFileSync('test/benchmark/beng/input/fasta_1000.txt', 'utf-8').length;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_TRUE(mvp_value_is_number(result.value));
    EXPECT_GT(mvp_value_to_number(result.value), 0.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesPrivateMapEntriesAndStringKeys) {
    const char source[] =
        "const counts = new Map();\n"
        "counts.set('AA', 2);\n"
        "counts.set('AA', counts.get('AA') + 3);\n"
        "const entry = counts.entries()[0];\n"
        "entry[0] + ':' + entry[1] + ':' + counts.size;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)6);
    EXPECT_EQ(memcmp(bytes, "AA:5:1", 6), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirIndexesPrivateMapKeys) {
    const char source[] =
        "const map = new Map();\n"
        "for (let i = 0; i < 256; i++) map.set('key' + i, i);\n"
        "map.get('key173');\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 173.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirIndexesPrivateObjectKeys) {
    const char source[] =
        "const object = {};\n"
        "for (let i = 0; i < 256; i++) object['key' + i] = i;\n"
        "object.key173;\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 173.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirHoistsFunctionsBeforeExportAssignment) {
    const char source[] =
        "let exports = {};\n"
        "exports.default = answer;\n"
        "function answer() { return 7; }\n"
        "exports.default();\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 7.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirCallsTenParameterFunctions) {
    const char source[] =
        "function total(a,b,c,d,e,f,g,h,i,j) {\n"
        "  return { value: a+b+c+d+e+f+g+h+i+j }.value;\n"
        "}\n"
        "total(1,2,3,4,5,6,7,8,9,10);\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    EXPECT_EQ(mvp_value_to_number(result.value), 55.0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirUsesDateGetTimeAndArrayStringConversion) {
    const char source[] =
        "let date = new Date(7);\n"
        "[date.getTime(), [].reflection].join(':');\n";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    ASSERT_EQ(result.ok, 1) << result.error;
    size_t length = 0;
    const char* bytes = mvp_string_bytes(result.value, &length);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(length, (size_t)2);
    EXPECT_EQ(memcmp(bytes, "7:", 2), 0);
    mvp_execution_result_destroy(&result);
}

TEST(JsMvpMirTest, GenericDirectMirReportsUncaughtThrowAtTheScriptBoundary) {
    const char source[] = "throw new Error('benchmark failure');";
    MvpExecutionResult result = mvp_execute_source(source, sizeof(source) - 1);
    EXPECT_EQ(result.ok, 0);
    EXPECT_EQ(result.execution, nullptr);
    EXPECT_NE(strstr(result.error, "benchmark failure"), nullptr);
}
