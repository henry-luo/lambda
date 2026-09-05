//==============================================================================
// Lambda Structured Error System Tests
//
// Tests the error handling infrastructure including:
// - Error code categories (1xx syntax, 2xx semantic, 3xx runtime, etc.)
// - Error message formatting
// - Stack trace capture
// - Negative test cases that verify proper error reporting
//==============================================================================

#include <gtest/gtest.h>
#include "../lambda/runtime/lambda-error.h"
#include "../lambda/runtime/ast_build.hpp"
#include "../lambda/runtime/mir_emitter_shared.hpp"
#include "../lambda/runtime/type_contract.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/arraylist.h"
#include "../lib/memtrack.h"
#include "../lib/mempool.h"
#include "../lib/shell.h"
#include <string>
#include <cstring>

#ifdef _WIN32
#define LAMBDA_EXE "lambda.exe"
#else
#define LAMBDA_EXE "./lambda.exe"
#endif

//==============================================================================
// Type-contract metadata tests
//==============================================================================

TEST(ValueRepresentationTest, CanonicalRepUsesTheFullSemanticContract) {
    EXPECT_EQ(lambda_canonical_rep(&TYPE_INT), VALUE_REP_INT_LANE);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_FLOAT), VALUE_REP_F64);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_BOOL), VALUE_REP_I64);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_INT64), VALUE_REP_I64);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_UINT64), VALUE_REP_U64);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_STRING), VALUE_REP_RAW_GC_POINTER);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_TYPE), VALUE_REP_RAW_NON_GC_POINTER);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_ANY), VALUE_REP_ITEM);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_INTEGER), VALUE_REP_ITEM);
    EXPECT_EQ(lambda_canonical_rep(&TYPE_NUMBER), VALUE_REP_ITEM);
    EXPECT_EQ(lambda_canonical_rep_for_type_id(LMD_TYPE_INT),
        VALUE_REP_INT_LANE);
    EXPECT_EQ(lambda_canonical_rep_for_type_id(LMD_TYPE_INT64),
        VALUE_REP_I64);
    EXPECT_EQ(lambda_canonical_rep_for_type_id(LMD_TYPE_ARRAY_NUM),
        VALUE_REP_RAW_GC_POINTER);

    TypeConstrained constrained = {};
    constrained.type_id = LMD_TYPE_TYPE;
    constrained.kind = TYPE_KIND_CONSTRAINED;
    constrained.base = &TYPE_FLOAT;
    EXPECT_EQ(lambda_canonical_rep((Type*)&constrained), VALUE_REP_F64);

    TypeParam parameter = {};
    parameter.type_id = LMD_TYPE_TYPE;
    parameter.kind = TYPE_KIND_PARAM;
    parameter.full_type = &TYPE_INT;
    EXPECT_EQ(lambda_canonical_rep((Type*)&parameter), VALUE_REP_INT_LANE);

    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Type* nullable_int = lambda_type_nullable_normalized(pool, &TYPE_INT);
    Type* int_or_string = lambda_type_union_normalized(pool, &TYPE_INT,
        &TYPE_STRING);
    ASSERT_NE(nullable_int, nullptr);
    ASSERT_NE(int_or_string, nullptr);
    EXPECT_EQ(lambda_canonical_rep(nullable_int), VALUE_REP_INT_LANE);
    EXPECT_EQ(lambda_canonical_rep(int_or_string), VALUE_REP_ITEM);
    Type* nullable_int64 = lambda_type_nullable_normalized(pool, &TYPE_INT64);
    ASSERT_NE(nullable_int64, nullptr);
    EXPECT_EQ(lambda_canonical_rep(nullable_int64), VALUE_REP_ITEM);
    pool_destroy(pool);
}

TEST(ValueRepresentationTest, DirectTransitionsKeepLogicalAndPhysicalAxesSeparate) {
    EXPECT_NE(VALUE_REP_INT_LANE, VALUE_REP_I64);
    EXPECT_NE(VALUE_REP_INT_LANE, VALUE_REP_MACHINE_I64);
    EXPECT_EQ(em_mir_type_for_rep(VALUE_REP_ITEM), MIR_T_I64);
    EXPECT_EQ(em_mir_type_for_rep(VALUE_REP_INT_LANE), MIR_T_I64);
    EXPECT_EQ(em_mir_type_for_rep(VALUE_REP_I64), MIR_T_I64);
    EXPECT_EQ(em_mir_type_for_rep(VALUE_REP_F64), MIR_T_D);

    MirEmitter emitter = {};
    MirValue lane = em_value(1, MIR_T_I64, LMD_TYPE_INT,
        VALUE_REP_INT_LANE, JIT_VALUE_NON_GC_SCALAR, &TYPE_INT);
    MirValue same = em_require_rep(&emitter, lane, VALUE_REP_INT_LANE);
    EXPECT_EQ(same.reg, lane.reg);
    EXPECT_EQ(same.rep, VALUE_REP_INT_LANE);
    EXPECT_EQ(same.semantic_contract, &TYPE_INT);
}

TEST(ValueRepresentationTest, UnsupportedDirectTransitionFailsClosed) {
    MirEmitter emitter = {};
    MirValue lane = em_value(1, MIR_T_I64, LMD_TYPE_INT,
        VALUE_REP_INT_LANE, JIT_VALUE_NON_GC_SCALAR, &TYPE_INT);
    EXPECT_DEATH(em_require_rep(&emitter, lane, VALUE_REP_MACHINE_I64), "");
}

TEST(TypeContractMetadataTest, InternalTopExclusionsStayDistinctFromAny) {
    EXPECT_TRUE(lambda_type_accepts_error(&TYPE_ANY));
    EXPECT_TRUE(lambda_type_accepts_null(&TYPE_ANY));

    EXPECT_FALSE(lambda_type_accepts_error(&TYPE_ANY_NO_ERROR));
    EXPECT_TRUE(lambda_type_accepts_null(&TYPE_ANY_NO_ERROR));
    EXPECT_TRUE(lambda_type_accepts_error(&TYPE_ANY_NO_NULL));
    EXPECT_FALSE(lambda_type_accepts_null(&TYPE_ANY_NO_NULL));
    EXPECT_FALSE(lambda_type_accepts_error(&TYPE_ANY_NO_ERROR_OR_NULL));
    EXPECT_FALSE(lambda_type_accepts_null(&TYPE_ANY_NO_ERROR_OR_NULL));

    EXPECT_STREQ(type_contract_display_name(&TYPE_ANY_NO_ERROR), "any \\ error");
    EXPECT_STREQ(type_contract_display_name(&TYPE_ANY_NO_ERROR_OR_NULL),
        "any \\ {error, null}");
}

TEST(TypeContractMetadataTest, RemovesAndNormalizesErrorAndNullConstituents) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    Type* int_or_error = lambda_type_union_normalized(pool, &TYPE_INT, &TYPE_ERROR);
    ASSERT_NE(int_or_error, nullptr);
    EXPECT_TRUE(lambda_type_has_proven_error(int_or_error));
    EXPECT_EQ(lambda_type_remove_error(pool, int_or_error), &TYPE_INT);

    Type* int_or_null = lambda_type_union_normalized(pool, &TYPE_INT, &TYPE_NULL);
    ASSERT_NE(int_or_null, nullptr);
    EXPECT_EQ(lambda_type_remove_error_and_null(pool, int_or_null), &TYPE_INT);

    EXPECT_EQ(lambda_type_remove_error(pool, &TYPE_ANY), &TYPE_ANY_NO_ERROR);
    EXPECT_EQ(lambda_type_remove_error_and_null(pool, &TYPE_ANY),
        &TYPE_ANY_NO_ERROR_OR_NULL);
    EXPECT_EQ(lambda_type_union_normalized(pool, &TYPE_ANY_NO_ERROR, &TYPE_ERROR),
        &TYPE_ANY);

    pool_destroy(pool);
}

TEST(TypeContractMetadataTest, OrNarrowingRetainsTheCleanInternalTop) {
    const char* args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/type_or_narrowing.ls", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);

    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    // An explicit-any source remains dynamically open. `or` removes its
    // error/null cases without collapsing the result back to true any.
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_BINARY (op \"or\") (value_type \"any \\\\ {error, null}\")"),
        nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_BINARY (op \"or\") (value_type \"int\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_CALL_EXPR (value_type \"type\") (value_may_error true)"),
        nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_BINARY (op \"or\") (value_type \"type\") (value_may_error false)"),
        nullptr);

    shell_result_free(&result);
}

TEST(TypeContractMetadataTest, AstDumpPreservesSignatureAndEffectMetadata) {
    const char* args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/type_contract_metadata.ls", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);

    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_FUNC (name \"forward\") (return_contract \"any \\\\ error\") "
        "(return_contract_explicit false)"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_PARAM (name \"value\") (contract \"any \\\\ error\") "
        "(contract_explicit false)"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_FUNC (name \"explicit\") (return_contract \"any\") "
        "(return_contract_explicit true)"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_PARAM (name \"value\") (contract \"any\") "
        "(contract_explicit true)"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_FUNC_EXPR (return_contract \"any \\\\ error\") "
        "(return_contract_explicit false)"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_FUNC (name \"precise\") (return_contract \"any \\\\ error\") "
        "(return_contract_explicit false) (effective_return \"int\")"), nullptr);
    // An unannotated procedure can propagate its unannotated parameter's
    // error effect even though its declared return contract remains `any`.
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_PROC (name \"procedural\") (return_contract \"any\") "
        "(return_contract_explicit false) (effective_return \"any \\\\ error\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_SYS_FUNC (name \"int\") (success_type \"number\") "
        "(may_return_error true)"), nullptr);

    shell_result_free(&result);

    const char* import_args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/type_contract_metadata_import.ls", NULL};
    result = shell_exec(LAMBDA_EXE, import_args, &options);
    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_IDENT (name \"imported\") (function_return_contract \"any \\\\ error\") "
        "(function_return_contract_explicit false) (function_effective_return \"any \\\\ error\")"),
        nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_IDENT (name \"imported_precise\") "
        "(function_return_contract \"any \\\\ error\") "
        "(function_return_contract_explicit false) (function_effective_return \"int\")"),
        nullptr);
    shell_result_free(&result);
}

TEST(TypeInferenceStructuralTest, IP1OperatorsPublishPreciseTypes) {
    // IP1 [Type_Infer TIG5/TIG6/TIG11/TIG12/TIG16]: operators that previously
    // fell back to `any` now publish the type their operands prove.
    const char* args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/type_infer_ip1.ls", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);
    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);

    // TIG5: `and` over two bools is bool, not any; over mixed operands it is
    // their union (a union renders under the shared `type` tag).
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_BINARY (op \"and\") (value_type \"bool\")"), nullptr)
        << result.stdout_buf;
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_BINARY (op \"and\") (value_type \"type\")"), nullptr)
        << result.stdout_buf;
    // TIG6: string/string relational comparison is bool — the old rule only
    // admitted native-numeric pairs and left every other comparable pair open.
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_BINARY (op \"<\") (value_type \"bool\")"), nullptr)
        << result.stdout_buf;
    // No relational node may still report `any` in this fixture.
    EXPECT_EQ(strstr(result.stdout_buf,
        "(AST_NODE_BINARY (op \"<\") (value_type \"any\")"), nullptr)
        << result.stdout_buf;

    shell_result_free(&result);
}

TEST(TypeInferenceStructuralTest, IP2SysFuncRowsResolvePreciseResults) {
    // IP2 [Type_Infer TI4/TIG4]: registry rows derive a precise success type
    // from the call site instead of falling back to `any`.
    const char* args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/type_infer_ip2.ls", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);
    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);

    // Real-scalar transcendentals and carrier-preserving rounding → float;
    // text transforms → string; order-preserving collection ops → array.
    EXPECT_NE(strstr(result.stdout_buf, "CALL_EXPR (value_type \"float\")"), nullptr)
        << result.stdout_buf;
    EXPECT_NE(strstr(result.stdout_buf, "CALL_EXPR (value_type \"string\")"), nullptr)
        << result.stdout_buf;
    EXPECT_NE(strstr(result.stdout_buf, "CALL_EXPR (value_type \"array\")"), nullptr)
        << result.stdout_buf;
    // Every sys-func call in this fixture must be resolved; none may remain open.
    EXPECT_EQ(strstr(result.stdout_buf, "CALL_EXPR (value_type \"any\")"), nullptr)
        << result.stdout_buf;

    shell_result_free(&result);
}

TEST(TypeInferenceStructuralTest, IP6JsOperatorsPublishPreciseTypes) {
    // IP6 [Type_Infer TIG13/TI2]: JS binary expressions publish the type the
    // operator produces. Every one of them used to be typed `float`.
    const char* args[] = {LAMBDA_EXE, "--emit-js-ast-dump",
        "test/js/type_infer_ip6.js", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);
    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);

    // Equality, relational and membership tests are predicates.
    EXPECT_NE(strstr(result.stdout_buf,
        "(op strict_eq) (value_type \"bool\")"), nullptr) << result.stdout_buf;
    EXPECT_NE(strstr(result.stdout_buf,
        "(op lt) (value_type \"bool\")"), nullptr) << result.stdout_buf;
    EXPECT_NE(strstr(result.stdout_buf,
        "(op in) (value_type \"bool\")"), nullptr) << result.stdout_buf;
    // `+` is overloaded: string concatenation and numeric addition both appear.
    EXPECT_NE(strstr(result.stdout_buf,
        "(op add) (value_type \"string\")"), nullptr) << result.stdout_buf;
    EXPECT_NE(strstr(result.stdout_buf,
        "(op add) (value_type \"float\")"), nullptr) << result.stdout_buf;
    // JS numbers are binary64: a bitwise result is still a number, and typing
    // it `int` would claim a carrier the lowering does not produce.
    EXPECT_NE(strstr(result.stdout_buf,
        "(op bit_or) (value_type \"float\")"), nullptr) << result.stdout_buf;
    // No comparison may still report the old blanket `float`.
    EXPECT_EQ(strstr(result.stdout_buf,
        "(op strict_eq) (value_type \"float\")"), nullptr) << result.stdout_buf;

    shell_result_free(&result);
}

TEST(TypeContractMetadataTest, ImplicitParameterErrorMatchArmIsLinted) {
    const char* args[] = {LAMBDA_EXE, "test/lambda/type_implicit_param_match_lint.ls", NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);

    ASSERT_EQ(result.exit_code, 0) << (result.stdout_buf ? result.stdout_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "lambda_match_lint: line 3: `case error:` is unreachable for implicit parameter 'value'; declare 'value: any' to accept error values"),
        nullptr);
    EXPECT_EQ(strstr(result.stdout_buf, "line 11: `case error:` is unreachable"), nullptr);

    shell_result_free(&result);
}

//==============================================================================
// Numeric boundary-admission tests
//==============================================================================

TEST(NumericBoundaryAdmissionTest, ExactScalarConversionsPreserveTargetTags) {
    Item converted = ItemError;

    ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(3.0), &TYPE_INT, &converted));
    EXPECT_EQ(get_type_id(converted), LMD_TYPE_INT);
    EXPECT_EQ(lambda_int_item_to_i64(converted), 3);
    EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(3.5), &TYPE_INT, &converted));

    struct SignedBoundaryCase {
        Type* target;
        NumSizedType tag;
        double lower;
        double upper;
    } signed_cases[] = {
        {&TYPE_I8, NUM_INT8, -128.0, 127.0},
        {&TYPE_I16, NUM_INT16, -32768.0, 32767.0},
        {&TYPE_I32, NUM_INT32, -2147483648.0, 2147483647.0},
    };
    for (const SignedBoundaryCase& test : signed_cases) {
        ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(test.lower), test.target, &converted));
        EXPECT_EQ(get_type_id(converted), LMD_TYPE_NUM_SIZED);
        EXPECT_EQ(converted.get_num_type(), test.tag);
        EXPECT_EQ(converted.get_num_sized_as_int64(), (int64_t)test.lower);
        EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(test.lower - 1.0),
            test.target, &converted));
        ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(test.upper), test.target, &converted));
        EXPECT_EQ(converted.get_num_sized_as_int64(), (int64_t)test.upper);
        EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(test.upper + 1.0),
            test.target, &converted));
    }

    struct UnsignedBoundaryCase {
        Type* target;
        NumSizedType tag;
        double upper;
    } unsigned_cases[] = {
        {&TYPE_U8, NUM_UINT8, 255.0},
        {&TYPE_U16, NUM_UINT16, 65535.0},
        {&TYPE_U32, NUM_UINT32, 4294967295.0},
    };
    for (const UnsignedBoundaryCase& test : unsigned_cases) {
        ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(0.0), test.target, &converted));
        EXPECT_EQ(get_type_id(converted), LMD_TYPE_NUM_SIZED);
        EXPECT_EQ(converted.get_num_type(), test.tag);
        EXPECT_EQ(converted.get_num_sized_as_int64(), 0);
        EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(-1.0), test.target, &converted));
        ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(test.upper), test.target, &converted));
        EXPECT_EQ(converted.get_num_sized_as_int64(), (int64_t)test.upper);
        EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(test.upper + 1.0),
            test.target, &converted));
    }

    ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(16777216.0), &TYPE_F32, &converted));
    EXPECT_EQ(converted.get_num_type(), NUM_FLOAT32);
    EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(16777217.0), &TYPE_F32, &converted));
    ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(2048.0), &TYPE_F16, &converted));
    EXPECT_EQ(converted.get_num_type(), NUM_FLOAT16);
    EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(2049.0), &TYPE_F16, &converted));

    ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(-0.0), &TYPE_F32, &converted));
    EXPECT_TRUE(signbit(converted.get_num_sized_as_double()));
    // v5 retains shared IEEE poison as part of int while finite values must
    // remain in the exact int53 band.
    ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(NAN), &TYPE_INT, &converted));
    EXPECT_TRUE(lambda_item_is_merged_poison(converted.item));
    EXPECT_TRUE(isnan(lambda_int_item_value(converted)));
    ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(INFINITY), &TYPE_INT, &converted));
    // Admitted inf keeps the shared representation rather than re-tagging into
    // an int-only sentinel, so the Item is the plain inline IEEE bits.
    EXPECT_TRUE(lambda_item_is_merged_poison(converted.item));
    EXPECT_EQ(lambda_int_item_value(converted), INFINITY);
    ASSERT_TRUE(lambda_numeric_boundary_admit(push_d(-INFINITY), &TYPE_INT, &converted));
    EXPECT_EQ(lambda_int_item_value(converted), -INFINITY);

    EXPECT_FALSE(lambda_numeric_boundary_admit(push_d(9007199254740994.0), &TYPE_INT, &converted));
}

//==============================================================================
// Error Code Category Tests
//==============================================================================

class ErrorCodeCategoryTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorCodeCategoryTest, SyntaxErrorCategory) {
    // All 1xx codes should be syntax errors
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_SYNTAX_ERROR));
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_UNEXPECTED_TOKEN));
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_MISSING_TOKEN));
    EXPECT_TRUE(ERR_IS_SYNTAX(ERR_UNTERMINATED_STRING));

    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SEMANTIC(ERR_SYNTAX_ERROR));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_SYNTAX_ERROR));
    EXPECT_FALSE(ERR_IS_IO(ERR_SYNTAX_ERROR));
    EXPECT_FALSE(ERR_IS_INTERNAL(ERR_SYNTAX_ERROR));
}

TEST_F(ErrorCodeCategoryTest, SemanticErrorCategory) {
    // All 2xx codes should be semantic errors
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_SEMANTIC_ERROR));
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_TYPE_MISMATCH));
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_UNDEFINED_VARIABLE));
    EXPECT_TRUE(ERR_IS_SEMANTIC(ERR_UNDEFINED_FUNCTION));

    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_TYPE_MISMATCH));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_TYPE_MISMATCH));
}

TEST_F(ErrorCodeCategoryTest, RuntimeErrorCategory) {
    // All 3xx codes should be runtime errors
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_RUNTIME_ERROR));
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_NULL_REFERENCE));
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_DIVISION_BY_ZERO));
    EXPECT_TRUE(ERR_IS_RUNTIME(ERR_INDEX_OUT_OF_BOUNDS));

    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_RUNTIME_ERROR));
    EXPECT_FALSE(ERR_IS_SEMANTIC(ERR_RUNTIME_ERROR));
}

TEST_F(ErrorCodeCategoryTest, IOErrorCategory) {
    // All 4xx codes should be I/O errors
    EXPECT_TRUE(ERR_IS_IO(ERR_IO_ERROR));
    EXPECT_TRUE(ERR_IS_IO(ERR_FILE_NOT_FOUND));
    EXPECT_TRUE(ERR_IS_IO(ERR_NETWORK_ERROR));

    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_IO_ERROR));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_IO_ERROR));
}

TEST_F(ErrorCodeCategoryTest, InternalErrorCategory) {
    // All 5xx codes should be internal errors
    EXPECT_TRUE(ERR_IS_INTERNAL(ERR_INTERNAL_ERROR));
    EXPECT_TRUE(ERR_IS_INTERNAL(ERR_NOT_IMPLEMENTED));
    EXPECT_TRUE(ERR_IS_INTERNAL(ERR_POOL_EXHAUSTED));

    // Should not be other categories
    EXPECT_FALSE(ERR_IS_SYNTAX(ERR_INTERNAL_ERROR));
    EXPECT_FALSE(ERR_IS_RUNTIME(ERR_INTERNAL_ERROR));
}

//==============================================================================
// Error Creation Tests
//==============================================================================

class ErrorCreationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorCreationTest, CreateSimpleError) {
    SourceLocation loc = {
        .file = nullptr,
        .line = 0,
        .column = 0
    };
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "Test error message", &loc);

    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_SYNTAX_ERROR);
    EXPECT_STREQ(error->message, "Test error message");

    err_free(error);
}

TEST_F(ErrorCreationTest, FaultRecordUsesStaticErrorStorage) {
    LambdaFaultRecord record = {};
    lambda_fault_record_init(&record);
    EXPECT_EQ(lambda_fault_record_error(&record), nullptr);

    lambda_fault_record_prepare(&record, LAMBDA_FAULT_SIDE_STACK_EXHAUSTION,
                                ERR_OK);
    LambdaError* error = lambda_fault_record_error(&record);
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(error->is_static);
    EXPECT_FALSE(error->is_heap);
    EXPECT_EQ(error->code, ERR_STACK_OVERFLOW);
    EXPECT_STREQ(lambda_fault_reason_name(record.reason), "side_stack_exhaustion");
    EXPECT_STREQ(error->message, "Side-stack capacity exhausted");

    // Fault records may be temporarily installed as last_error; ordinary error
    // cleanup must leave their pre-reserved message and embedded storage intact.
    err_free(error);
    EXPECT_EQ(lambda_fault_record_error(&record), error);
    EXPECT_STREQ(error->message, "Side-stack capacity exhausted");
}

TEST_F(ErrorCreationTest, ErrorAllocationFailureBuildsStaticOomFault) {
    LambdaFaultRecord record = {};
    lambda_fault_record_from_error_allocation_failure(&record, ERR_TYPE_MISMATCH);

    LambdaError* error = lambda_fault_record_error(&record);
    ASSERT_NE(error, nullptr);
    EXPECT_TRUE(record.active);
    EXPECT_EQ(record.reason, LAMBDA_FAULT_OUT_OF_MEMORY);
    EXPECT_EQ(record.prior_error_code, ERR_TYPE_MISMATCH);
    EXPECT_EQ(error->code, ERR_OUT_OF_MEMORY);
    EXPECT_TRUE(error->is_static);
    EXPECT_STREQ(error->message, "Out of memory");
}

TEST_F(ErrorCreationTest, CreateErrorWithLocation) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 42,
        .column = 10
    };

    LambdaError* error = err_create(ERR_TYPE_MISMATCH, "Type mismatch error", &loc);

    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_TYPE_MISMATCH);
    EXPECT_EQ(error->location.line, 42u);
    EXPECT_EQ(error->location.column, 10u);
    EXPECT_STREQ(error->location.file, "test.ls");

    err_free(error);
}

TEST(AstBuildAllocationTest, SizedLiteralCopyFailureDoesNotCrash) {
    const char source[] = "1i8";
    Pool* pool = pool_create_sized(64 * 1024);
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);

    Transpiler tp = {};
    tp.source = source;
    tp.pool = pool;
    tp.arena = input->arena;
    tp.name_pool = input->name_pool;
    tp.type_list = input->type_list;
    tp.root = input->root;
    tp.const_list = arraylist_new(16);
    tp.current_scope = (NameScope*)pool_calloc(pool, sizeof(NameScope));
    tp.max_errors = 10;
    ASSERT_NE(tp.const_list, nullptr);
    ASSERT_NE(tp.current_scope, nullptr);

    AstScript* root = nullptr;
    LambdaParseError parse_error = {};
    memtrack_fault_inject(0);
    LambdaParseStatus status = lambda_rd_reduce_ast(&tp, source,
        sizeof(source) - 1, &root, &parse_error);
    if (status == LAMBDA_PARSE_OK && root) lambda_ast_finalize_script(&tp, root);
    memtrack_fault_clear();

    EXPECT_EQ(status, LAMBDA_PARSE_OK);
    EXPECT_NE(root, nullptr);
    EXPECT_GT(tp.error_count, 0);

    arraylist_free(tp.const_list);
    arraylist_free(input->type_list);
    pool_destroy(pool);
}

TEST_F(ErrorCreationTest, CreateFormattedError) {
    SourceLocation loc = {
        .file = nullptr,
        .line = 0,
        .column = 0
    };
    LambdaError* error = err_createf(ERR_UNDEFINED_VARIABLE, &loc,
        "Variable '%s' not defined in scope", "myVar");

    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_UNDEFINED_VARIABLE);
    EXPECT_NE(strstr(error->message, "myVar"), nullptr);

    err_free(error);
}

TEST_F(ErrorCreationTest, CreateFormattedErrorPreservesLongMessage) {
    char detail[1501];
    memset(detail, 'x', sizeof(detail) - 1);
    detail[sizeof(detail) - 1] = '\0';
    SourceLocation loc = {};

    LambdaError* error = err_createf(ERR_RUNTIME_ERROR, &loc,
        "prefix:%s:suffix", detail);

    ASSERT_NE(error, nullptr);
    size_t expected_length = strlen("prefix:") + strlen(detail) + strlen(":suffix");
    EXPECT_EQ(strlen(error->message), expected_length);
    EXPECT_EQ(error->message[expected_length - strlen(":suffix") - 1], 'x');
    EXPECT_EQ(strstr(error->message, ":suffix"), error->message + expected_length - strlen(":suffix"));

    err_free(error);
}

TEST_F(ErrorCreationTest, CreateErrorWithHelp) {
    SourceLocation loc = {
        .file = nullptr,
        .line = 0,
        .column = 0
    };
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "Missing semicolon", &loc);
    ASSERT_NE(error, nullptr);

    err_add_help(error, "Consider adding ';' at the end of the statement");

    // after adding help, the help field should not be null
    ASSERT_NE(error->help, nullptr) << "help should be set after err_add_help";

    // check the content - help text contains "adding"
    EXPECT_TRUE(strstr(error->help, "adding") != nullptr)
        << "help text should contain 'adding', got: " << error->help;

    err_free(error);
}

//==============================================================================
// Error Formatting Tests
//==============================================================================

class ErrorFormattingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorFormattingTest, FormatBasicError) {
    SourceLocation loc = {
        .file = "script.ls",
        .line = 10,
        .column = 5
    };

    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "Unexpected token", &loc);
    char* formatted = err_format(error);

    ASSERT_NE(formatted, nullptr);
    // Check that it contains key elements
    EXPECT_NE(strstr(formatted, "script.ls"), nullptr);
    EXPECT_NE(strstr(formatted, "10"), nullptr);
    EXPECT_NE(strstr(formatted, "Unexpected token"), nullptr);

    free(formatted);
    err_free(error);
}

TEST_F(ErrorFormattingTest, ErrorCodeName) {
    EXPECT_STREQ(err_code_name(ERR_OK), "OK");
    EXPECT_STREQ(err_code_name(ERR_SYNTAX_ERROR), "SYNTAX_ERROR");
    EXPECT_STREQ(err_code_name(ERR_TYPE_MISMATCH), "TYPE_MISMATCH");
    EXPECT_STREQ(err_code_name(ERR_RUNTIME_ERROR), "RUNTIME_ERROR");
    EXPECT_STREQ(err_code_name(ERR_FILE_NOT_FOUND), "FILE_NOT_FOUND");
    EXPECT_STREQ(err_code_name(ERR_INTERNAL_ERROR), "INTERNAL_ERROR");
}

TEST_F(ErrorFormattingTest, ErrorCategoryName) {
    EXPECT_STREQ(err_category_name(ERR_SYNTAX_ERROR), "Syntax");
    EXPECT_STREQ(err_category_name(ERR_TYPE_MISMATCH), "Semantic");
    EXPECT_STREQ(err_category_name(ERR_RUNTIME_ERROR), "Runtime");
    EXPECT_STREQ(err_category_name(ERR_FILE_NOT_FOUND), "I/O");
    EXPECT_STREQ(err_category_name(ERR_INTERNAL_ERROR), "Internal");
}

//==============================================================================
// Source Context Tests
//==============================================================================

class SourceContextTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

    const char* sample_source =
        "let x = 10\n"
        "let y = 20\n"
        "let z = x + y + undefined_var\n"
        "print(z)\n";
};

TEST_F(SourceContextTest, GetSourceLine) {
    // line 1
    char* line1 = err_get_source_line(sample_source, 1);
    ASSERT_NE(line1, nullptr);
    EXPECT_STREQ(line1, "let x = 10");
    free(line1);

    // line 3
    char* line3 = err_get_source_line(sample_source, 3);
    ASSERT_NE(line3, nullptr);
    EXPECT_STREQ(line3, "let z = x + y + undefined_var");
    free(line3);

    // line beyond source
    char* line10 = err_get_source_line(sample_source, 10);
    EXPECT_EQ(line10, nullptr);
}

TEST_F(SourceContextTest, GetSourceLineCount) {
    // sample_source has 4 lines, but trailing newline counts as start of line 5
    int count = err_get_source_line_count(sample_source);
    EXPECT_GE(count, 4);  // at least 4 lines

    // single line with no newline
    EXPECT_EQ(err_get_source_line_count("hello"), 1);

    // empty source
    EXPECT_EQ(err_get_source_line_count(""), 1);
    EXPECT_EQ(err_get_source_line_count(nullptr), 0);
}

TEST_F(SourceContextTest, ExtractContext) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 3,
        .column = 17,
        .end_line = 3,
        .end_column = 29,  // span "undefined_var"
        .source = nullptr
    };

    LambdaError* error = err_create(ERR_UNDEFINED_VARIABLE, "undefined variable 'undefined_var'", &loc);

    // extract context (stores source reference)
    err_extract_context(error, sample_source, 2);
    EXPECT_EQ(error->location.source, sample_source);

    err_free(error);
}

TEST_F(SourceContextTest, FormatWithContextLines) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 3,
        .column = 17,
        .end_line = 3,
        .end_column = 29,
        .source = nullptr
    };

    LambdaError* error = err_create(ERR_UNDEFINED_VARIABLE, "undefined variable 'undefined_var'", &loc);
    err_extract_context(error, sample_source, 1);

    char* formatted = err_format_with_context(error, 1);
    ASSERT_NE(formatted, nullptr);

    // should contain location prefix
    EXPECT_NE(strstr(formatted, "test.ls:3:17"), nullptr)
        << "Should contain location prefix\n" << formatted;

    // should contain error code
    EXPECT_NE(strstr(formatted, "E202"), nullptr)
        << "Should contain error code\n" << formatted;

    // should contain the error line
    EXPECT_NE(strstr(formatted, "let z = x + y + undefined_var"), nullptr)
        << "Should contain source line\n" << formatted;

    // should contain carets for span
    EXPECT_NE(strstr(formatted, "^"), nullptr)
        << "Should contain caret pointer\n" << formatted;

    free(formatted);
    err_free(error);
}

TEST_F(SourceContextTest, FormatWithMultipleContextLines) {
    SourceLocation loc = {
        .file = "script.ls",
        .line = 3,
        .column = 5,
        .end_line = 3,
        .end_column = 5,
        .source = nullptr
    };

    LambdaError* error = err_create(ERR_TYPE_MISMATCH, "expected int, found string", &loc);
    err_extract_context(error, sample_source, 2);

    char* formatted = err_format_with_context(error, 2);
    ASSERT_NE(formatted, nullptr);

    // with context_lines=2, should show lines 1,2,3,4,5 (but only 4 exist)
    EXPECT_NE(strstr(formatted, "let x = 10"), nullptr)
        << "Should contain context line before\n" << formatted;
    EXPECT_NE(strstr(formatted, "let y = 20"), nullptr)
        << "Should contain context line before\n" << formatted;
    EXPECT_NE(strstr(formatted, "let z ="), nullptr)
        << "Should contain error line\n" << formatted;

    free(formatted);
    err_free(error);
}

//==============================================================================
// JSON Output Tests
//==============================================================================

class JSONOutputTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(JSONOutputTest, FormatSingleError) {
    SourceLocation loc = {
        .file = "test.ls",
        .line = 10,
        .column = 5,
        .end_line = 10,
        .end_column = 15,
        .source = nullptr
    };

    LambdaError* error = err_create(ERR_TYPE_MISMATCH, "expected int, found string", &loc);
    char* json = err_format_json(error);

    ASSERT_NE(json, nullptr);

    // check JSON structure
    EXPECT_NE(strstr(json, "\"code\": 201"), nullptr) << "Should contain error code\n" << json;
    EXPECT_NE(strstr(json, "\"name\": \"TYPE_MISMATCH\""), nullptr) << "Should contain error name\n" << json;
    EXPECT_NE(strstr(json, "\"category\": \"Semantic\""), nullptr) << "Should contain category\n" << json;
    EXPECT_NE(strstr(json, "\"message\": \"expected int, found string\""), nullptr) << "Should contain message\n" << json;
    EXPECT_NE(strstr(json, "\"file\": \"test.ls\""), nullptr) << "Should contain file\n" << json;
    EXPECT_NE(strstr(json, "\"line\": 10"), nullptr) << "Should contain line\n" << json;
    EXPECT_NE(strstr(json, "\"column\": 5"), nullptr) << "Should contain column\n" << json;

    free(json);
    err_free(error);
}

TEST_F(JSONOutputTest, FormatErrorWithHelp) {
    SourceLocation loc = { .file = "test.ls", .line = 5, .column = 1 };
    LambdaError* error = err_create(ERR_UNDEFINED_VARIABLE, "variable 'x' not defined", &loc);
    err_add_help(error, "Did you mean 'y'?");

    char* json = err_format_json(error);
    ASSERT_NE(json, nullptr);

    EXPECT_NE(strstr(json, "\"help\": \"Did you mean 'y'?\""), nullptr)
        << "Should contain help text\n" << json;

    free(json);
    err_free(error);
}

TEST_F(JSONOutputTest, FormatErrorArray) {
    SourceLocation loc1 = { .file = "test.ls", .line = 5, .column = 1 };
    SourceLocation loc2 = { .file = "test.ls", .line = 10, .column = 8 };

    LambdaError* errors[2];
    errors[0] = err_create(ERR_SYNTAX_ERROR, "unexpected token", &loc1);
    errors[1] = err_create(ERR_TYPE_MISMATCH, "type mismatch", &loc2);

    char* json = err_format_json_array(errors, 2);
    ASSERT_NE(json, nullptr);

    // check structure
    EXPECT_NE(strstr(json, "\"errors\":"), nullptr) << "Should contain errors array\n" << json;
    EXPECT_NE(strstr(json, "\"errorCount\": 2"), nullptr) << "Should contain count\n" << json;
    EXPECT_NE(strstr(json, "SYNTAX_ERROR"), nullptr) << "Should contain first error\n" << json;
    EXPECT_NE(strstr(json, "TYPE_MISMATCH"), nullptr) << "Should contain second error\n" << json;

    free(json);
    err_free(errors[0]);
    err_free(errors[1]);
}

TEST_F(JSONOutputTest, EscapeSpecialCharacters) {
    SourceLocation loc = { .file = "path/to/file.ls", .line = 1, .column = 1 };
    LambdaError* error = err_create(ERR_SYNTAX_ERROR, "unexpected \"quote\" and \\backslash", &loc);

    char* json = err_format_json(error);
    ASSERT_NE(json, nullptr);

    // special chars should be escaped
    EXPECT_NE(strstr(json, "\\\"quote\\\""), nullptr)
        << "Quotes should be escaped\n" << json;
    EXPECT_NE(strstr(json, "\\\\backslash"), nullptr)
        << "Backslash should be escaped\n" << json;

    free(json);
    err_free(error);
}

//==============================================================================
// Stack Trace Tests (basic - full test requires runtime context)
//==============================================================================

class StackTraceTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(StackTraceTest, CaptureStackTraceWithoutDebugInfo) {
    // Capture stack trace without debug info table
    StackFrame* trace = err_capture_stack_trace(nullptr, 10);

    // Should return something (or NULL if not supported)
    // The frames might have unknown function names
    if (trace) {
        // Verify the linked list structure
        int count = 0;
        StackFrame* frame = trace;
        while (frame && count < 20) {
            count++;
            frame = frame->next;
        }
        EXPECT_GT(count, 0);

        err_free_stack_trace(trace);
    }
}

//==============================================================================
// Error Chaining Tests
//==============================================================================

class ErrorChainingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorChainingTest, ChainedErrors) {
    SourceLocation loc1 = { .file = "main.ls", .line = 50 };
    SourceLocation loc2 = { .file = "util.ls", .line = 20 };

    LambdaError* cause = err_create(ERR_FILE_NOT_FOUND, "Config file missing", &loc2);
    LambdaError* error = err_create(ERR_IO_ERROR, "Failed to initialize", &loc1);
    error->cause = cause;

    EXPECT_NE(error->cause, nullptr);
    EXPECT_EQ(error->cause->code, ERR_FILE_NOT_FOUND);

    // Format should include both errors
    char* formatted = err_format_with_context(error, 0);
    EXPECT_NE(strstr(formatted, "Failed to initialize"), nullptr);
    EXPECT_NE(strstr(formatted, "Caused by"), nullptr);
    EXPECT_NE(strstr(formatted, "Config file missing"), nullptr);

    free(formatted);
    err_free(error);  // should also free cause
}

//==============================================================================
// Negative Test Helpers
//==============================================================================

// Helper to run Lambda script and capture output
struct ScriptResult {
    int exit_code;
    std::string output;
    std::string error_output;
};

ScriptResult run_lambda_script(const char* script_path, bool procedural = false) {
    ScriptResult result;
    const char* direct_args[] = {LAMBDA_EXE, "--no-log", script_path, NULL};
    const char* procedural_args[] = {LAMBDA_EXE, "run", "--no-log", script_path, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    // Negative paths are untrusted test data and must not be interpolated into a shell command.
    ShellResult shell_result = shell_exec(LAMBDA_EXE,
        procedural ? procedural_args : direct_args, &options);
    if (shell_result.stdout_buf) {
        result.output.assign(shell_result.stdout_buf, shell_result.stdout_len);
    }
    result.exit_code = shell_result.exit_code;
    shell_result_free(&shell_result);
    return result;
}

//==============================================================================
// Negative Script Tests - Verify proper error reporting
//==============================================================================

class NegativeScriptTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

    void ExpectErrorWithoutCrash(const char* script_path) {
        ScriptResult result = run_lambda_script(script_path);

        // Should NOT crash
        EXPECT_EQ(result.output.find("Segmentation fault"), std::string::npos)
            << "Script crashed: " << script_path;
        EXPECT_EQ(result.output.find("SIGABRT"), std::string::npos)
            << "Script aborted: " << script_path;
        EXPECT_EQ(result.output.find("core dumped"), std::string::npos)
            << "Script core dumped: " << script_path;
    }

    void ExpectErrorCode(const char* script_path, const char* expected_error_indicator) {
        ScriptResult result = run_lambda_script(script_path);

        // Should contain error indicator
        bool has_error = result.output.find(expected_error_indicator) != std::string::npos ||
                        result.output.find("[ERR!]") != std::string::npos ||
                        result.output.find("error") != std::string::npos;

        EXPECT_TRUE(has_error) << "Expected error for: " << script_path
                               << "\nOutput: " << result.output;
    }

    void ExpectErrorMessage(const char* script_path, const char* expected_message) {
        ScriptResult result = run_lambda_script(script_path);

        EXPECT_NE(result.exit_code, 0) << "Expected script to fail: " << script_path;
        EXPECT_NE(strstr(result.output.c_str(), expected_message), nullptr)
            << "Expected diagnostic text for: " << script_path
            << "\nExpected: " << expected_message
            << "\nOutput: " << result.output;
    }

    void ExpectRuntimeErrorMessage(const char* script_path, const char* expected_message) {
        ScriptResult result = run_lambda_script(script_path, true);

        EXPECT_NE(result.exit_code, 0) << "Expected runtime script to fail: " << script_path;
        EXPECT_NE(strstr(result.output.c_str(), expected_message), nullptr)
            << "Expected runtime diagnostic text for: " << script_path
            << "\nExpected: " << expected_message
            << "\nOutput: " << result.output;
    }
};

// Syntax error tests
TEST_F(NegativeScriptTest, SyntaxErrorMalformedRange) {
    ExpectErrorWithoutCrash("test/lambda/negative/test_syntax_errors.ls");
}

TEST_F(NegativeScriptTest, OldBareStringPatternSyntaxIsRejected) {
    ExpectErrorMessage("test/lambda/negative/semantic/string_pattern_old_bare.ls",
        "Unexpected syntax near");
}

TEST_F(NegativeScriptTest, SymbolLiteralInsidePatternReportsDomainDiagnostic) {
    ExpectErrorMessage("test/lambda/negative/semantic/string_pattern_symbol_literal.ls",
        "pattern bodies are content-only; use \\symbol(...) for the symbol domain");
}

TEST_F(NegativeScriptTest, PatternClassBindingCollisionReportsReservedName) {
    ExpectErrorMessage("test/lambda/negative/semantic/string_pattern_reserved_class.ls",
        "pattern class 'd' is reserved inside pattern islands");
}

// CW31/S9.2.4 exclusivity face 4: overlapping mutable views of one base
// conflict through their shared view base at whole-base granularity.
TEST_F(NegativeScriptTest, VarViewOverlapSharedBaseIsRejected) {
    ExpectErrorMessage("test/lambda/negative/semantic/var_view_overlap.ls",
        "overlaps another `var` parameter through their shared view base");
}

// Exclusivity face 3: a `var` place argument rooted at another `var`
// argument's base is a path-prefix overlap, rejected at whole-base
// granularity.
TEST_F(NegativeScriptTest, VarPathPrefixOverlapIsRejected) {
    ExpectErrorMessage("test/lambda/negative/semantic/var_path_prefix_overlap.ls",
        "overlaps another `var` parameter");
}

// Type error tests
TEST_F(NegativeScriptTest, TypeErrorFuncParam) {
    ExpectErrorWithoutCrash("test/lambda/negative/func_param_negative.ls");
}

// Undefined reference tests
TEST_F(NegativeScriptTest, UndefinedFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/undefined_function.ls");
}

TEST_F(NegativeScriptTest, CallNonFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/call_non_function.ls");
}

TEST_F(NegativeScriptTest, InvalidTypeAnnotation) {
    ExpectErrorWithoutCrash("test/lambda/negative/invalid_type_annotation.ls");
}

TEST_F(NegativeScriptTest, ConceptualTypeNamesSuggestDefinedSyntax) {
    // `int64` is a concept spelling, not annotation syntax; the diagnostic must
    // point at `i64` and must not cascade an E201 "of type error" message.
    const char* script = "test/lambda/negative/semantic/type_alias_suggestion.ls";
    ScriptResult result = run_lambda_script(script);
    EXPECT_NE(result.exit_code, 0) << "Expected script to fail: " << script;
    EXPECT_NE(strstr(result.output.c_str(),
        "unknown type 'int64'; did you mean 'i64'?"), nullptr)
        << "Expected alias suggestion for: " << script
        << "\nOutput: " << result.output;
    // assert on the diagnostic code, not prose — source-context lines echo the
    // script text, so a prose substring can false-match a comment.
    EXPECT_EQ(strstr(result.output.c_str(), "error[E201]"), nullptr)
        << "Unresolved annotation must not cascade an E201 boundary error"
        << "\nOutput: " << result.output;
}

TEST_F(NegativeScriptTest, StaticWarningFlagDowngradesSemanticErrorsAndRuns) {
    // --static-warning (relaxed mode, SI3v2/TI6): the same script that is a
    // static error by default must run to completion, with the diagnostic
    // reported as warning[E…] instead of error[E…].
    const char* script = "test/lambda/negative/semantic/type_alias_suggestion.ls";
    ScriptResult result;
    const char* args[] = {LAMBDA_EXE, "run", "--no-log", "--static-warning",
        script, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.stdout_buf) {
        result.output.assign(shell_result.stdout_buf, shell_result.stdout_len);
    }
    result.exit_code = shell_result.exit_code;
    shell_result_free(&shell_result);

    EXPECT_EQ(result.exit_code, 0)
        << "Relaxed mode must run the script\nOutput: " << result.output;
    EXPECT_NE(strstr(result.output.c_str(),
        "warning[E204]: unknown type 'int64'; did you mean 'i64'?"), nullptr)
        << "Diagnostic must appear as a warning\nOutput: " << result.output;
    EXPECT_EQ(strstr(result.output.c_str(), "error[E204]"), nullptr)
        << "Diagnostic must not appear as an error\nOutput: " << result.output;
    EXPECT_NE(strstr(result.output.c_str(), "1"), nullptr)
        << "Script body must have produced its result\nOutput: " << result.output;
}

TEST_F(NegativeScriptTest, StaticAnnotatedDeclarationsRejectKnownMismatches) {
    ExpectErrorMessage("test/lambda/negative/type_enforcement_declaration.ls",
        "cannot initialize 'wrong_scalar' of type int with string");
}

TEST_F(NegativeScriptTest, StaticUnionContractsDisplayTheirFullExpectedType) {
    ExpectErrorMessage("test/lambda/negative/semantic/type_enforcement_union_diagnostic.ls",
        "cannot initialize 'wrong_union' of type int | string with bool");
}

TEST_F(NegativeScriptTest, ProceduralStatementsOutsidePnReportE224WithoutCascade) {
    const char* script = "test/lambda/negative/semantic/proc_stam_outside_pn.ls";
    ScriptResult result = run_lambda_script(script);

    EXPECT_NE(result.exit_code, 0) << "Expected script to fail: " << script;

    // every procedural-only statement at module scope reports, and reports as E224
    for (const char* subject : {"`var`", "assignment", "`while`",
                                "`break`", "`continue`", "`return`"}) {
        std::string expected = std::string("error[E224]: ") + subject
            + " is only allowed inside a procedure (pn)";
        EXPECT_NE(result.output.find(expected), std::string::npos)
            << "Missing diagnostic: " << expected << "\nOutput: " << result.output;
    }

    // the guards must record a semantic error, not just log one: with error_count still 0 the
    // build looked clean and MIR ran against the AST hole left by the refused binding, inventing
    // follow-on errors about the very name the guard declined to create.
    EXPECT_EQ(result.output.find("undefined variable"), std::string::npos)
        << "Cascade from holey AST reached MIR:\n" << result.output;
}

TEST_F(NegativeScriptTest, TypeValuedOrExplainsTheUnionOperator) {
    ExpectErrorMessage("test/lambda/negative/semantic/type_valued_or.ls",
        "operator `or` cannot combine type values; use `|` to form a union type");
}

TEST_F(NegativeScriptTest, DynamicFractionalDecimalRejectsIntegerBoundary) {
    ScriptResult result = run_lambda_script(
        "test/lambda/negative/runtime/type_enforcement_dynamic_declaration.ls", true);
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(strstr(result.output.c_str(),
        "type check at declaration 'value' failed: expected int, got decimal"), nullptr)
        << result.output;
}

TEST_F(NegativeScriptTest, TypeEnforcementRuntimeNegativeGoldensPinDiagnostics) {
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/computed_key_non_name.ls",
        "error[E201]: computed map key must evaluate to string or symbol");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_array_write.ls",
        "error[E201]: type check at typed array element assignment failed: expected int, got string 'not an integer'");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_dynamic_arity.ls",
        "error[E206]: fn_call_into: function 'add' expects 2 arguments, got 1");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_dynamic_declaration.ls",
        "error[E201]: type check at declaration 'value' failed: expected int, got decimal");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_dynamic_map.ls",
        "error[E201]: type check at declaration 'person' failed: expected Person, got map; validator at .age: Expected type 'int', but got 'string'");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_dynamic_parameter.ls",
        "error[E201]: type check at argument 1 of _accept_0 failed: expected int, got string 'not an integer'");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_dynamic_return.ls",
        "error[E201]: type check at function return failed: expected int, got string 'not an integer'");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_map_write.ls",
        "error[E201]: type check at typed map member assignment failed: expected int, got string 'very old'");
}

TEST_F(NegativeScriptTest, InputSchemaUsesTheSharedTypedBoundary) {
    ScriptResult result = run_lambda_script(
        "test/lambda/negative/runtime/type_enforce_input_schema.ls", true);
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(strstr(result.output.c_str(),
        "type check at input schema failed: expected Person, got map"), nullptr)
        << result.output;
    EXPECT_NE(strstr(result.output.c_str(), "validator at .age"), nullptr)
        << result.output;
}

TEST_F(NegativeScriptTest, StaticNamedMapLiteralFieldsAreCheckedBeforeLayoutAdoption) {
    ExpectErrorMessage("test/lambda/negative/type_enforcement_declaration.ls",
        "field 'age' of 'wrong_field' expects int, but got string");
}

TEST_F(NegativeScriptTest, StaticAnnotatedDeclarationsRejectNull) {
    ExpectErrorMessage("test/lambda/negative/type_enforcement_declaration.ls",
        "cannot initialize 'wrong_null' of type int with null");
}

TEST_F(NegativeScriptTest, StaticDeclaredReturnsRejectKnownMismatches) {
    ExpectErrorMessage("test/lambda/negative/type_enforcement_declaration.ls",
        "function 'wrong_return' body returns type string, declared return type int");
}

TEST_F(NegativeScriptTest, StaticTypedMapWritesRejectKnownMismatches) {
    ExpectErrorMessage("test/lambda/negative/type_enforcement_declaration.ls",
        "cannot assign string to typed map member of type int");
}

TEST_F(NegativeScriptTest, StaticBracketTypedMapWritesRejectKnownMismatches) {
    ExpectErrorMessage("test/lambda/negative/semantic/type_enforcement_bracket_map_write.ls",
        "cannot assign string to typed map member of type int");
}

TEST_F(NegativeScriptTest, StaticArityMismatchIsRejected) {
    ExpectErrorMessage("test/std/negative/wrong_arg_count.ls",
        "function expects 2 arguments, got 1");
}

// S12.3.6 makes optional parameters the sanctioned alternative to overloading,
// so the accepted arity is a range whenever one exists. Reporting only the
// required count understated it in both directions.
// A map key is a symbol, not a string. The brace resolver reads `{"k": 1}` by
// interior and used to fall through to a block, failing at the `:` with a bare
// "expected an expression".
TEST_F(NegativeScriptTest, DoubleQuotedMapKeyNamesTheRule) {
    ExpectErrorMessage("test/std/negative/map_key_double_quoted.ls",
        "a map key is a symbol, not a string");
}

// S16.9.3: `;` separates content items, so it cannot open element content.
// The generic "expected an expression" sent a real user to conclude the grammar
// was whitespace-sensitive; the diagnostic must name the rule.
TEST_F(NegativeScriptTest, ElementSemicolonCannotOpenContent) {
    ExpectErrorMessage("test/std/negative/element_semicolon_opens_content.ls",
        "';' cannot open element content");
}

// LR02-9: a `&`/`!` contract must be rejected on a non-conforming value AND
// named in the diagnostic — it used to print the bare word "type".
TEST_F(NegativeScriptTest, TypeSetOperatorContractIsNamed) {
    ExpectErrorMessage("test/std/negative/type_set_operator_mismatch.ls",
        "cannot initialize 'a' of type int & string with int");
}

TEST_F(NegativeScriptTest, OptionalParamArityReportsARange) {
    ExpectErrorMessage("test/std/negative/wrong_arg_count_optional.ls",
        "function expects 1 to 2 arguments, got 3");
}

TEST_F(NegativeScriptTest, ImportParseErrorBlocksExecution) {
    ScriptResult result = run_lambda_script("test/lambda/negative/import_parse_error_driver.ls");

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(strstr(result.output.c_str(), "error[E217]"), nullptr)
        << "Expected import failure diagnostic.\nOutput: " << result.output;
    EXPECT_EQ(strstr(result.output.c_str(), "\"DRIVER_RAN\""), nullptr)
        << "Importer executed after imported module parse failure.\nOutput: " << result.output;
}

//==============================================================================
// Categorized Negative Tests - Organized by error category
//==============================================================================

// --- Syntax Error Tests (1xx) ---

TEST_F(NegativeScriptTest, SyntaxError_UnterminatedString) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/unterminated_string.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_MissingParen) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/missing_paren.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_MissingBrace) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/missing_brace.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_InvalidNumber) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/invalid_number.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_OversizedIntegerLiteral) {
    ExpectErrorCode("test/lambda/negative/syntax/oversized_integer_literal.ls", "error[E108]");
}

TEST_F(NegativeScriptTest, SyntaxError_RetiredDecimalSuffix) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/retired_decimal_suffix.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_UnexpectedToken) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/unexpected_token.ls");
}

TEST_F(NegativeScriptTest, SyntaxError_StatementComparisonAmbiguousWithElement) {
    ExpectErrorMessage("test/lambda/negative/syntax/statement_comparison_ambiguous.ls",
        "'<' and '>' are ambiguous with element syntax at statement level");
}

TEST_F(NegativeScriptTest, SyntaxError_UnexpectedEOF) {
    ExpectErrorWithoutCrash("test/lambda/negative/syntax/unexpected_eof.ls");
}

// --- Semantic Error Tests (2xx) ---

TEST_F(NegativeScriptTest, SemanticError_UndefinedVariable) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/undefined_variable.ls");
}

TEST_F(NegativeScriptTest, SemanticError_UndefinedFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/undefined_function.ls");
}

TEST_F(NegativeScriptTest, SemanticError_TypeMismatch) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/type_mismatch.ls");
}

TEST_F(NegativeScriptTest, SemanticError_ImplicitFnReturnMustContainError) {
    ExpectErrorMessage("test/lambda/negative/semantic/implicit_fn_error_return.ls",
        "may return error from call to 'may_fail'");
}

TEST_F(NegativeScriptTest, SemanticError_EnforcingCallNeedsImmediateAcknowledgment) {
    ExpectErrorMessage("test/lambda/negative/semantic/unhandled_error_expression.ls",
        "handle with 'risky(...) ^ { ... }'");
}

TEST_F(NegativeScriptTest, SemanticError_DynamicProcedureCallFromFunction) {
    ExpectErrorMessage("test/lambda/negative/semantic/dynamic_call_proc_in_fn.ls",
        "call: cannot call a procedure (pn) from a function (fn)");
}

TEST_F(NegativeScriptTest, SemanticError_ArityMismatch) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/arity_mismatch.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateParam) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_param.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateVariable) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_variable.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateType) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_type.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_function.ls");
}

TEST_F(NegativeScriptTest, SemanticError_DuplicateMixed) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/duplicate_mixed.ls");
}

TEST_F(NegativeScriptTest, SemanticError_ImmutableAssignment) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/immutable_assignment.ls");
}

TEST_F(NegativeScriptTest, SemanticError_ImmutableInteriorAssignment) {
    ExpectErrorMessage("test/lambda/negative/semantic/immutable_interior_assignment.ls",
        "cannot mutate through immutable binding");
}

TEST_F(NegativeScriptTest, SemanticError_ProcMethodRequiresMutableReceiver) {
    ExpectErrorMessage("test/lambda/negative/semantic/proc_method_let_receiver.ls",
        "mutating method 'increment' needs a `var` binding receiver");
}

TEST_F(NegativeScriptTest, SemanticError_CaptureMutation) {
    ExpectErrorMessage("test/lambda/negative/semantic/capture_mutation.ls",
                       "changed invisibly by a previous call");
}

TEST_F(NegativeScriptTest, SemanticError_StartOutsideProcedure) {
    ExpectErrorMessage("test/lambda/negative/semantic/start_outside_pn.ls",
        "`start` is only allowed inside a procedure (pn)");
}

TEST_F(NegativeScriptTest, SemanticError_StartRequiresProcedureCall) {
    ExpectErrorMessage("test/lambda/negative/semantic/start_non_pn.ls",
        "`start` first argument must resolve to a procedure (pn)");
}

TEST_F(NegativeScriptTest, SemanticError_StartRejectsMutableCapture) {
    ExpectErrorMessage("test/lambda/negative/semantic/start_mutable_capture.ls",
        "`start` cannot capture mutable var 'value'");
}

TEST_F(NegativeScriptTest, SemanticError_StartRejectsUnsupportedMode) {
    ExpectErrorMessage("test/lambda/negative/semantic/start_unsupported_mode.ls",
        "`start` mode 'thread' is not implemented yet; use 'task'");
}

TEST_F(NegativeScriptTest, SemanticError_VarTypeMismatch) {
    ExpectErrorWithoutCrash("test/lambda/negative/semantic/var_type_mismatch.ls");
}

TEST_F(NegativeScriptTest, SemanticError_SizedIntegerOverflow) {
    ExpectErrorCode("test/lambda/negative/semantic/sized_integer_overflow.ls", "error[E108]");
}

TEST_F(NegativeScriptTest, SemanticError_SizedConstantConversionOverflow) {
    ExpectErrorCode("test/lambda/negative/semantic/sized_constant_conversion_overflow.ls", "error[E108]");
}

TEST_F(NegativeScriptTest, SemanticError_FunctionArgumentLimit) {
    ExpectErrorMessage("test/lambda/negative/semantic/function_argument_limit.ls",
        "function formal count 17 exceeds Core Lambda limit 16");
}

TEST_F(NegativeScriptTest, SemanticError_IntegralLiteralZero) {
    ExpectErrorMessage("test/lambda/negative/semantic/integral_literal_zero.ls",
        "integral division or remainder by literal zero");
}

TEST_F(NegativeScriptTest, SemanticError_ReservedLastKeyword) {
    ExpectErrorMessage("test/lambda/negative/semantic/reserved_last_keyword.ls",
        "reserved keyword");
}

TEST_F(NegativeScriptTest, SemanticError_OperatorComparabilitySymbol) {
    ExpectErrorMessage("test/lambda/negative/semantic/operator_comparability_symbol.ls",
        "no magnitude");
}

// --- Runtime Error Tests (3xx) ---

TEST_F(NegativeScriptTest, RuntimeError_NullReference) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/null_reference.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_DivisionByZero) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/division_by_zero.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_IndexOutOfBounds) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/index_out_of_bounds.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_InvalidOperation) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/invalid_operation.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_OperatorComparabilityDynamic) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/operator_comparability_dynamic.ls");
}

// Stack overflow test - uses Phase 2 signal-based handler (sigaltstack/SEH)
// for graceful recovery instead of crashing with SIGSEGV
TEST_F(NegativeScriptTest, RuntimeError_StackOverflow) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/stack_overflow.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_CallNonFunction) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/test_call_nonfunc.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_ClosureCallStack) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/test_closure_call_stack.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_DeepCallStack) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/test_deep_call_stack.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_DivByZero) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/test_div_zero.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_TooManyArgs) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/test_too_many_args.ls");
}

TEST_F(NegativeScriptTest, RuntimeError_TypeError) {
    ExpectErrorWithoutCrash("test/lambda/negative/runtime/test_type_error.ls");
}

// --- Fuzzy Crash Regression Tests ---

TEST_F(NegativeScriptTest, FuzzyCrash_EmptyParenthesizedExpr) {
    ExpectErrorWithoutCrash("test/lambda/negative/fuzzy_crashes/empty_parenthesized_expr.ls");
}

TEST_F(NegativeScriptTest, FuzzyCrash_ClosureJitCorruption) {
    ExpectErrorWithoutCrash("test/lambda/negative/fuzzy_crashes/closure_jit_corruption.ls");
}

TEST_F(NegativeScriptTest, FuzzyCrash_TypeValidationGenericMapArray) {
    ExpectErrorWithoutCrash("test/lambda/negative/fuzzy_crashes/type_validation_generic_map_array.ls");
}

// --- I/O Error Tests (4xx) ---

TEST_F(NegativeScriptTest, IOError_FileNotFound) {
    ExpectErrorWithoutCrash("test/lambda/negative/io/file_not_found.ls");
}

TEST_F(NegativeScriptTest, IOError_ParseError) {
    ExpectErrorWithoutCrash("test/lambda/negative/io/parse_error.ls");
}

//==============================================================================
// Keyword-as-name rulings (S16.10) and sys-func shadowing (S12.3.7)
//==============================================================================

// S16.10.1v2: construct-leading words are rejected as binding names at the
// DECLARATION site. Before this, `let type = 1` parsed and `type` then read
// the base type — a silent wrong answer.
TEST_F(NegativeScriptTest, KeywordBarredAsBindingName) {
    ExpectErrorMessage("test/lambda/negative/semantic/keyword_binding_barred.ls",
                       "is a reserved keyword and cannot be used as a name");
}

// An import alias is a binding, so it takes the same bar; the old behaviour
// accepted the import and failed at every use.
TEST_F(NegativeScriptTest, KeywordImportAliasRejected) {
    ExpectErrorMessage("test/lambda/negative/semantic/keyword_import_alias.ls",
                       "an import alias must be a plain identifier");
}

// There is no quoted escape: `import 'edit':` previously created a binding
// that no use site could reach (symbols never implicitly read bindings).
TEST_F(NegativeScriptTest, KeywordImportAliasQuotedRejected) {
    ExpectErrorMessage("test/lambda/negative/semantic/keyword_import_alias_quoted.ls",
                       "an import alias must be a plain identifier");
}

// S12.3.7: a non-callable shadow raises the ordinary not-callable error and
// must never fall back to the shadowed builtin (this returned 3 before).
TEST_F(NegativeScriptTest, SysFuncShadowNonCallableDoesNotFallBack) {
    // The runtime's "call target is not a function" goes to the log, which
    // this harness does not capture, so assert the claim itself: the call
    // fails, and it does NOT quietly yield the builtin `sum([1,2])` == 3.
    ScriptResult result = run_lambda_script(
        "test/lambda/negative/semantic/sysfunc_shadow_not_callable.ls");
    EXPECT_NE(result.exit_code, 0) << "expected the non-callable shadow to fail";
    EXPECT_EQ(result.output.find("3"), std::string::npos)
        << "a non-callable shadow must not fall back to the builtin\nOutput: "
        << result.output;
}

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
