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

TEST(ValueRepresentationTest, NullableWideArraysUseDestinationOwnedTypedItems) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    Type* contracts[] = {
        lambda_type_nullable_normalized(pool, &TYPE_INT64),
        lambda_type_nullable_normalized(pool, &TYPE_UINT64),
    };
    for (Type* contract : contracts) {
        LaneStorageDesc scalar = lambda_lane_storage_desc_for(contract);
        EXPECT_EQ((int)scalar.kind, (int)LANE_STORAGE_ITEM);

        LaneStorageDesc array = {};
        ASSERT_TRUE(lambda_type_array_lane_storage_desc(contract, &array));
        EXPECT_EQ((int)array.kind, (int)LANE_STORAGE_TYPED_ITEM);
        EXPECT_EQ((int)array.byte_size, (int)sizeof(TypedItem));
        EXPECT_EQ((int)array.nullable, 1);
        EXPECT_EQ((int)array.native, 1);

        ShapeEntry map_field = {};
        shape_entry_set_type(&map_field, contract);
        const LaneStorageDesc* map = shape_entry_storage(&map_field);
        EXPECT_EQ((int)map->kind, (int)LANE_STORAGE_TYPED_ITEM);
        EXPECT_EQ((int)map->byte_size, (int)sizeof(TypedItem));
        EXPECT_EQ((int)map->nullable, 1);
        EXPECT_EQ((int)map->native, 1);

        Array storage = {};
        storage.type_id = LMD_TYPE_ARRAY;
        array_native_lane_configure(&storage, &array);
        EXPECT_TRUE(array_native_lane_matches_desc(&storage, &array));
    }

    pool_destroy(pool);
}

static TypeUnary array_contract_for_test(Type* element) {
    TypeUnary type = {};
    type.type_id = LMD_TYPE_TYPE;
    type.kind = TYPE_KIND_UNARY;
    type.op = OPERATOR_ARRAY;
    type.operand = element;
    return type;
}

TEST(ValueRepresentationTest, ArrayCertificateIdentityStillChecksTheLiveCarrier) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Type* elements[] = {&TYPE_INT, &TYPE_BOOL, &TYPE_FLOAT};
    ArrayNumElemType lanes[] = {ELEM_INT, ELEM_BOOL, ELEM_FLOAT64};
    for (int i = 0; i < 3; i++) {
        TypeUnary contract = array_contract_for_test(elements[i]);
        TypeUnary equivalent = array_contract_for_test(elements[i]);
        TypeUnary nested = array_contract_for_test((Type*)&contract);
        ArrayRepCert* cert = lambda_array_rep_cert_create(pool, (Type*)&contract);
        ASSERT_NE(cert, nullptr);
        EXPECT_TRUE(cert->has_array_num_lane);
        EXPECT_EQ(cert->array_num_elem, lanes[i]);
        ArrayNum array = {};
        array.type_id = LMD_TYPE_ARRAY_NUM;
        array.set_elem_type(lanes[i]);
        array.rep_cert = cert;
        Item value = {.array_num = &array};
        EXPECT_TRUE(lambda_array_rep_proves_cert(value, cert, true));
        EXPECT_TRUE(lambda_array_rep_proves(value, (Type*)&equivalent, true));
        EXPECT_FALSE(lambda_array_rep_proves(value, (Type*)&nested, true));
        array.set_elem_type(lanes[(i + 1) % 3]);
        EXPECT_FALSE(lambda_array_rep_proves_cert(value, cert, true));
        EXPECT_FALSE(lambda_array_rep_proves(value, (Type*)&contract, true));
        array.set_elem_type(lanes[i]);
        lambda_array_clear_rep_cert(value);
        EXPECT_FALSE(lambda_array_rep_proves_cert(value, cert, true));
    }
    EXPECT_FALSE(lambda_array_contract_compatible(&TYPE_INT, &TYPE_INT, true));
    pool_destroy(pool);
}

TEST(ValueRepresentationTest, OwnedNumericTensorProvesNestedPrimitiveContract) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    TypeUnary row = array_contract_for_test(&TYPE_INT);
    TypeUnary matrix = array_contract_for_test((Type*)&row);
    ArrayRepCert* cert = lambda_array_rep_cert_create(pool, (Type*)&matrix);
    ASSERT_NE(cert, nullptr);
    EXPECT_EQ(cert->rank, 2);
    EXPECT_TRUE(cert->has_array_num_lane);
    EXPECT_EQ(cert->array_num_elem, ELEM_INT);

    alignas(ArrayNumShape) uint8_t shape_storage[
        sizeof(ArrayNumShape) + 4 * sizeof(int64_t)] = {};
    ArrayNumShape* shape = (ArrayNumShape*)shape_storage;
    shape->ndim = 2;
    shape->backing_kind = ARRAY_NUM_BACKING_GC_OWNED;

    ArrayNum array = {};
    array.type_id = LMD_TYPE_ARRAY_NUM;
    array.set_elem_type(ELEM_INT);
    array.is_ndim = 1;
    array.extra = (int64_t)(uintptr_t)shape;
    array.rep_cert = cert;
    Item value = {.array_num = &array};
    EXPECT_TRUE(lambda_array_num_representation_proves_primitive_contract(
        value, (Type*)&matrix));
    EXPECT_TRUE(lambda_array_rep_proves_cert(value, cert, true));

    shape->ndim = 3;
    EXPECT_FALSE(lambda_array_num_representation_proves_primitive_contract(
        value, (Type*)&matrix));
    EXPECT_FALSE(lambda_array_rep_proves_cert(value, cert, true));
    shape->ndim = 2;
    array.is_view = 1;
    EXPECT_FALSE(lambda_array_num_representation_proves_primitive_contract(
        value, (Type*)&matrix));
    EXPECT_FALSE(lambda_array_rep_proves_cert(value, cert, true));
    array.is_view = 0;
    array.set_elem_type(ELEM_FLOAT64);
    EXPECT_FALSE(lambda_array_num_representation_proves_primitive_contract(
        value, (Type*)&matrix));
    EXPECT_FALSE(lambda_array_rep_proves_cert(value, cert, true));
    pool_destroy(pool);
}

TEST(ValueRepresentationTest, ArrayCertificateKeepsNullablePointerStorageDistinct) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    TypeUnary strings = array_contract_for_test(&TYPE_STRING);
    Type* nullable = lambda_type_nullable_normalized(pool, &TYPE_STRING);
    TypeUnary optionals = array_contract_for_test(nullable);
    ArrayRepCert* cert = lambda_array_rep_cert_create(pool, (Type*)&strings);
    ASSERT_NE(cert, nullptr);
    EXPECT_FALSE(cert->has_array_num_lane);
    Array array = {};
    array.type_id = LMD_TYPE_ARRAY;
    array.rep_cert = cert;
    Item value = {.array = &array};
    // A certificate on boxed slots never authorizes native String* loads.
    EXPECT_FALSE(lambda_array_rep_proves_cert(value, cert, true));
    array_native_lane_configure(&array, &cert->leaf_lane);
    EXPECT_TRUE(lambda_array_rep_proves_cert(value, cert, true));
    EXPECT_FALSE(lambda_array_rep_proves(value, (Type*)&optionals, true));
    LaneStorageDesc optional_lane = lambda_lane_storage_desc_for(nullable);
    array_native_lane_configure(&array, &optional_lane);
    EXPECT_FALSE(lambda_array_rep_proves_cert(value, cert, true));
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

TEST(TypeContractMetadataTest, AstDumpViewStateBindingsCarryNoParameterContract) {
    // A view `state` binding is an AST_NODE_PARAM whose TypeParam carries no
    // contract. It once held its initializer's plain Type, and dumping a
    // handler's assignment to it read a contract past the end of that Type.
    const char* args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/view_state.ls", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);

    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    // the census trailer follows the tree, so the whole dump was emitted
    EXPECT_NE(strstr(result.stdout_buf, "(any_census "), nullptr);
    EXPECT_NE(strstr(result.stdout_buf, "(AST_NODE_PARAM (name \"flag\")))"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf, "(AST_NODE_PARAM (name \"cursor\")))"), nullptr);
    // f16 initializers: the width NUM_FLOAT16 equals TYPE_KIND_PARAM, so a
    // plain Type here was taken for a TypeParam and still crashed
    EXPECT_NE(strstr(result.stdout_buf, "(AST_NODE_PARAM (name \"level\")))"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf, "(AST_NODE_PARAM (name \"trim\")))"), nullptr);
    // a handler's own parameter is still a TypeParam with its contract
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_PARAM (name \"e\") (contract \"any \\\\ error\") "
        "(contract_explicit false))"), nullptr);

    shell_result_free(&result);
}

TEST(TypeContractMetadataTest, SysFuncRelationsInstantiateSelectedArguments) {
    const char* args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/sysfunc_type_relations.ls", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);

    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    // S11.4.9/D3.3.5: fill binds from argument 1, and its collection/text
    // consumers retain that relation rather than falling back to `any`.
    EXPECT_NE(strstr(result.stdout_buf,
        "(value_type_element_type \"int\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(value_type_element_type \"string\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_SYS_FUNC (name \"fill\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_CALL_EXPR (value_type \"any\") (value_may_error true)\n"
        "                (function\n"
        "                  (AST_NODE_SYS_FUNC (name \"fill\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_SYS_FUNC (name \"slice\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_SYS_FUNC (name \"sort\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_CALL_EXPR (value_type \"array\") (value_may_error false)\n"
        "                    (function\n"
        "                      (AST_NODE_SYS_FUNC (name \"take\")"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(AST_NODE_CALL_EXPR (value_type \"string\") (value_may_error false)\n"
        "                    (function\n"
        "                      (AST_NODE_SYS_FUNC (name \"replace\")"), nullptr);

    shell_result_free(&result);
}

TEST(TypeBinderTest, AstDumpPreservesBinderSitesAndDependentReferences) {
    const char* args[] = {LAMBDA_EXE, "--emit-ast-dump",
        "test/lambda/type_binder.ls", NULL};
    ShellOptions options = {0};
    options.timeout_ms = 10000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);

    ASSERT_EQ(result.exit_code, 0) << (result.stderr_buf ? result.stderr_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    // S11.4.8/D3.1.1v4: slots make binder sites and dependent uses distinct.
    EXPECT_NE(strstr(result.stdout_buf,
        "(TypeBinder (name \"U\") (slot 0) (bound \"number\"))"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf,
        "(TypeBoundRef (slot 0) (bound \"number\"))"), nullptr);

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
    LambdaSyntaxUnit* syntax = nullptr;
    ASSERT_EQ(lambda_rd_parse_syntax(&tp, source, sizeof(source) - 1,
        &syntax, &parse_error), LAMBDA_PARSE_OK);
    ASSERT_NE(syntax, nullptr);
    // The sized-literal copy is the resolve pass's first tracked allocation,
    // and its failure is what this test covers.
    memtrack_fault_inject(0);
    LambdaParseStatus status = lambda_rd_resolve_syntax(&tp, syntax, &root,
        &parse_error);
    if (status == LAMBDA_PARSE_OK && root) lambda_ast_finalize_script(&tp, root);
    memtrack_fault_clear();
    lambda_rd_destroy_syntax(syntax);

    // D8.2.5: a failed resolve pass cannot publish a partial AST.
    EXPECT_EQ(status, LAMBDA_PARSE_ERROR);
    EXPECT_EQ(root, nullptr);
    EXPECT_GT(tp.error_count, 0);

    arraylist_free(tp.const_list);
    pool_destroy(pool);  // releases the Input too (D4.2.6)
}

TEST(AstBuildReductionTest, SyntaxPhaseDefersBindingUntilResolve) {
    const char source[] =
        "fn outer(value: int) => inner(value)\n"
        "fn inner(value: int) => value + 1\n";
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

    LambdaSyntaxUnit* syntax = nullptr;
    LambdaParseError parse_error = {};
    ASSERT_EQ(lambda_rd_parse_syntax(&tp, source, sizeof(source) - 1,
        &syntax, &parse_error), LAMBDA_PARSE_OK);
    ASSERT_NE(syntax, nullptr);
    // D8.2.5: the syntax phase builds nodes but cannot publish bindings.
    EXPECT_EQ(tp.current_scope->first, nullptr);

    AstScript* root = nullptr;
    ASSERT_EQ(lambda_rd_resolve_syntax(&tp, syntax, &root, &parse_error),
        LAMBDA_PARSE_OK);
    EXPECT_NE(root, nullptr);
    EXPECT_NE(tp.current_scope->first, nullptr);

    lambda_rd_destroy_syntax(syntax);
    arraylist_free(tp.const_list);
    pool_destroy(pool);  // releases the Input too (D4.2.6)
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

TEST_F(StackTraceTest, RawStackTraceUsesSharedDefaultDepth) {
    RawStackTrace* trace = err_capture_raw_stack_trace(nullptr, 0);
    ASSERT_NE(trace, nullptr);
    EXPECT_EQ(trace->max_frames, LAMBDA_ERROR_STACK_TRACE_DEFAULT_MAX_FRAMES);
    err_free_raw_stack_trace(trace);
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

ScriptResult run_lambda_script(const char* script_path, bool procedural = false,
                               const char* tier = NULL) {
    ScriptResult result;
    const char* direct_args[] = {LAMBDA_EXE, "--no-log", script_path, NULL};
    const char* procedural_args[] = {LAMBDA_EXE, "run", "--no-log", script_path, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    const ShellEnvEntry env[] = {{"LAMBDA_TIER", tier}, {NULL, NULL}};
    if (tier) options.env = env;
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

    // A boundary defect on one tier hides behind another tier's golden, so
    // each tier must reject the script with `expected_message` and never
    // reach its `bound:` line.
    void ExpectRejectedOnEveryTier(const char* script_path, bool procedural,
            const char* expected_message) {
        static const char* const tiers[] = {"interp", "jit", "auto"};
        for (const char* tier : tiers) {
            ScriptResult result = run_lambda_script(script_path, procedural, tier);
            EXPECT_NE(result.exit_code, 0) << script_path << " tier=" << tier;
            EXPECT_NE(strstr(result.output.c_str(), expected_message), nullptr)
                << script_path << " tier=" << tier << "\n" << result.output;
            EXPECT_EQ(strstr(result.output.c_str(), "bound:"), nullptr)
                << script_path << " tier=" << tier << "\n" << result.output;
        }
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

// S11.1.6v2/S16.8.6v3: a count on a *run* is the occurrence family, spelled as
// in regex. The bracket forms it replaced name their replacement rather than
// changing meaning under the same spelling.
TEST_F(NegativeScriptTest, RetiredUnboundedOccurrenceNamesItsReplacement) {
    ExpectErrorMessage("test/lambda/negative/semantic/occurrence_retired_plus.ls",
        "are retired: write `T{n+}` or `T{n,m}` for a run of T");
}

TEST_F(NegativeScriptTest, RetiredRangedOccurrenceNamesItsReplacement) {
    ExpectErrorMessage("test/lambda/negative/semantic/occurrence_retired_range.ls",
        "`[T{n,m}]` for an array of one");
}

// S11.1.1v3: an array suffix may follow an array suffix (`int[2][3]`), and a
// retired count in that position still names its replacement.
TEST_F(NegativeScriptTest, RetiredCountAfterArraySuffixNamesItsReplacement) {
    ExpectErrorMessage("test/lambda/negative/semantic/array_rank_retired_count.ls",
        "are retired: write `T{n+}` or `T{n,m}` for a run of T");
}

// S12.3.2 / D6.2.2v2 (LR07-16): a dynamic call has no declaration to bind
// names against; both tiers had silently passed named arguments by position.
TEST_F(NegativeScriptTest, NamedArgumentsNeedStaticallyKnownCallee) {
    ExpectErrorMessage("test/lambda/negative/semantic/named_arg_dynamic_call.ls",
        "error[E212]");
}

// LR07-19: named arguments cannot leave out a required parameter, on a direct
// call or a method call; the tiers had split on the direct call, and the
// method call had bound its arguments by position.
TEST_F(NegativeScriptTest, NamedArgumentsCannotSkipRequiredParameter) {
    ExpectErrorMessage("test/lambda/negative/semantic/named_arg_skips_required.ls",
        "the named arguments leave out required parameter 'b'");
    ExpectErrorMessage("test/lambda/negative/semantic/named_arg_method_skips_required.ls",
        "the named arguments leave out required parameter 'b'");
}

// S16.8.6v3: the open count is `T{n+}`, not regex's trailing comma. A habit
// that writes `{n,}` is told, rather than reading as an exact count.
TEST_F(NegativeScriptTest, RegexOpenCountNamesTheOpenSpelling) {
    ExpectErrorMessage("test/lambda/negative/semantic/occurrence_regex_open_count.ls",
        "write `T{n+}` for a run of n or more");
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

// S11.1.5 / S12.1.4v2(3): a known wrong colour is rejected at compile time;
// a colour known only at run time is rejected at the parameter boundary.
TEST_F(NegativeScriptTest, FunctionColourMismatchIsRejectedStatically) {
    ExpectErrorMessage("test/lambda/negative/semantic/fn_pn_colour_mismatch.ls",
        "error[E207]: argument 1 expected fn, got pn");
    ExpectErrorMessage("test/lambda/negative/semantic/fn_pn_colour_mismatch.ls",
        "error[E207]: argument 1 expected pn, got fn");
}

TEST_F(NegativeScriptTest, FunctionColourMismatchIsRejectedAtRuntime) {
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/fn_pn_colour_mismatch.ls",
        "expected fn, got pn logsq");
}

// S12.1.4v2: a `function` call that is statically a `pn` call is rejected in
// `fn` context, and a `function` body is checked as an `fn` body.
TEST_F(NegativeScriptTest, FunctionDeclarationColourIsCheckedStatically) {
    ExpectErrorMessage("test/lambda/negative/semantic/function_colour_static.ls",
        "error[E224]: passing a procedure (pn) to parameter 'f' makes this call of "
        "'apply_all' a pn call, which a function (fn) cannot make");
    ExpectErrorMessage("test/lambda/negative/semantic/function_body_is_fn.ls",
        "error[E224]: 'logsq' is a procedure (pn) and cannot be called from a function (fn)");
    ExpectErrorMessage("test/lambda/negative/semantic/function_body_var.ls",
        "error[E224]: `var` is only allowed inside a procedure (pn)");
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
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_nested_array_write.ls",
        "error[E201]: type check at typed nested array assignment failed: expected Variable, got map; validator at .value: Required field 'value' is missing from object");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_ndim_array_write.ls",
        "error[E201]: type check at typed multi-dimensional array assignment failed: expected int, got string 'not an integer'");
    ExpectRuntimeErrorMessage("test/lambda/negative/runtime/type_enforcement_mask_array_write.ls",
        "error[E201]: type check at typed array mask assignment failed: expected u8, got int 300");
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

TEST_F(NegativeScriptTest, RequiredLiteralFieldRejectsNullOnEveryTier) {
    // T29-2 (D3.2.6, S11.4.10): the JIT's shaped-literal path accepted null in
    // a required field while the interpreter rejected it, so every tier is
    // pinned here. The site label differs by tier (field vs declaration); the
    // outcome may not: E201, and the binding is never established.
    const char* scripts[] = {
        "test/lambda/negative/runtime/typed_literal_required_array_null.ls",
        "test/lambda/negative/runtime/typed_literal_required_dynamic_null.ls",
        // Tune29 §19.1 item 2: the field-carrier admission keeps null rejected
        "test/lambda/negative/runtime/typed_array_field_missing_carrier.ls",
    };
    for (const char* script : scripts) {
        ExpectRejectedOnEveryTier(script, true, "error[E201]");
    }
}

// S11.4.1v3 (J1/J2): a crossing the checker only deferred is decided by the
// runtime check on every tier. The JIT read a literal as `null` against a
// nullable contract and skipped a scalar crossing `int[]?`, binding `5` in
// both scripts while T0 raised E201.
TEST_F(NegativeScriptTest, DeferredBoundariesRejectOnEveryTier) {
    const char* scripts[] = {
        "test/lambda/negative/runtime/deferred_boundary_literal_union.ls",
        "test/lambda/negative/runtime/deferred_boundary_scalar_opt_array.ls",
    };
    for (const char* script : scripts) {
        ExpectRejectedOnEveryTier(script, false,
            "error[E201]: type check at declaration 'w' failed");
    }
}

// S7.1.1v3/S7.10.5v3 + S7.7.4: `math.sqrt(null)` is null, so the sum is null,
// and the declared `float` accumulator rejects it on every tier, naming the
// binding. The JIT's native libm call read the null lane as NaN and the
// interpreter reported the binding only as "declared assignment binding".
TEST_F(NegativeScriptTest, NullNumericReassignmentRejectsOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/null_numeric_reassign_float.ls",
        true, "error[E201]: type check at assignment to 'total' failed: expected float, got null");
}

// S11.1.1v3: rank is part of an array type. `int[]` admitted a 2-D reshape
// because its flat leaf lane holds ints; the runtime check now presents its
// rows, on every tier.
TEST_F(NegativeScriptTest, FlatArrayContractRejectsNdArrayOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/array_rank_flat_contract.ls",
        false, "error[E201]: type check at declaration 'e' failed: expected int[]");
}

// S11.1.1v3 on the element-wise path: a contract with no exact packed lane now
// admits a view or an N-D array by presenting its elements, and a matrix's
// elements are its rows -- so `number[]` still rejects one, on every tier.
TEST_F(NegativeScriptTest, NonLaneArrayContractRejectsNdArrayOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/array_rank_view_contract.ls",
        false, "error[E201]: type check at declaration 'e' failed: expected number[]");
}

// S11.1.1v3: `T[n]` is `T[]` with a fixed length, per axis (`int[2][3]` is
// three arrays of two). Once S11.1.6v2 made a counted bracket an array layer,
// the admission fast paths proved it from lane and rank alone, so a wrong
// length on any axis -- flat, a nested row, a packed shape -- was admitted.
// S11.4.5 / S11.4.1v3 (LR03-11): a sized-int boundary admits by value on every
// tier. T0 coerced (-1 into `u8` was 255, 300 into `i8` was 44); the JIT's
// rejections named the contract "num_sized", and its u32 lane gave none.
TEST_F(NegativeScriptTest, SizedIntParameterAdmitsByValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/sized_admission_param.ls",
        false, "failed: expected u8, got int -1");
}

TEST_F(NegativeScriptTest, SizedIntDeclarationAdmitsByValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/sized_admission_declaration.ls",
        false, "error[E201]: type check at declaration 'x' failed: expected i8, got int 300");
}

TEST_F(NegativeScriptTest, NativeU32LaneRejectionReportsOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/sized_admission_u32_lane.ls",
        false, "error[E201]: type check at declaration 'x' failed: expected u32, got int -1");
}

TEST_F(NegativeScriptTest, U64DeclarationAdmitsByValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/sized_admission_u64.ls",
        false, "error[E201]: type check at declaration 'a' failed: expected u64, got int -1");
}

// S11.2.1 (LR03-11): a literal type admits its one value. An integer literal
// union admitted any int on both tiers, and the static relation proved a
// string argument against `"a"` by TypeId, so the JIT dropped its check.
TEST_F(NegativeScriptTest, IntegerLiteralUnionAdmitsItsValuesOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/literal_admission_union.ls",
        false, "failed: expected 1 | 2, got int 3");
}

TEST_F(NegativeScriptTest, StringLiteralParameterKeepsItsCheckOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/literal_admission_string_param.ls",
        false, "failed: expected \"a\", got string 'c'");
}

// S11.2.1 / S11.4.1v3 (LR07-38): a one-value numeric literal contract names a
// value on the int or float carrier. The JIT took a native lane as its proof,
// so declarations and arguments bound the wrong number there.
TEST_F(NegativeScriptTest, IntLiteralDeclarationChecksItsValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/literal_admission_int_declaration.ls",
        false, "failed: expected 1, got int 2");
}

TEST_F(NegativeScriptTest, FloatLiteralDeclarationChecksItsValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/literal_admission_float_declaration.ls",
        false, "failed: expected 2.5, got float 1.5");
}

TEST_F(NegativeScriptTest, IntLiteralParameterChecksItsValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/literal_admission_int_param.ls",
        false, "failed: expected 1, got int 2");
}

// S11.2.1 (LR03-31): `true` in type position is the singleton {true}.
TEST_F(NegativeScriptTest, BoolLiteralDeclarationChecksItsValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/literal_admission_bool_declaration.ls",
        false, "failed: expected true, got bool false");
}

// S11.1.3 (LR03-14, LR03-18): a range type admits its members only. It wore
// the range VALUE tag (D3.1.1v4), so a range-typed parameter rejected every
// int statically while the JIT admitted a range value, and a range-typed map
// field read its int as a pointer.
TEST_F(NegativeScriptTest, RangeParameterRejectsNonMemberOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/range_admission_param.ls",
        false, "failed: expected 1 to 5, got int 9");
}

TEST_F(NegativeScriptTest, RangeParameterRejectsRangeValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/range_admission_range_value.ls",
        false, "failed: expected 1 to 5, got range");
}

TEST_F(NegativeScriptTest, RangeMapFieldRejectsNonMemberOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/range_admission_field.ls",
        false, "validator at .a: Expected type '1 to 5', but got 'int'");
}

TEST_F(NegativeScriptTest, CharacterRangeRejectsNonMemberOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/range_admission_char.ls",
        false, "failed: expected \"a\" to \"e\", got string 'z'");
}

TEST_F(NegativeScriptTest, RangeValueArgumentIsStaticError) {
    ExpectErrorMessage("test/lambda/negative/semantic/range_argument_static.ls",
        "argument 1 expected 1 to 5, got range");
}

// S11.4.10, S11.4.1v3 (LR03-20): an object literal admits each field against
// its declared contract. Construction stored any value unchecked, so a string's
// pointer read back from an int field and a range field took any int.
TEST_F(NegativeScriptTest, ObjectRangeFieldRejectsNonMemberOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/object_field_range.ls",
        false, "type check at field 'a' of Obj failed: expected 1 to 5, got int 9");
}

TEST_F(NegativeScriptTest, ObjectFieldChecksDynamicValueOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/object_field_dynamic.ls",
        false, "type check at field 'a' of Obj failed: expected int, got string 'x'");
}

TEST_F(NegativeScriptTest, ObjectSpreadFieldIsAdmittedOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/object_field_spread.ls",
        false, "type check at field 'a' of Pair failed: expected int, got string 'no'");
}

TEST_F(NegativeScriptTest, ObjectFieldMismatchIsStaticError) {
    ExpectErrorMessage("test/lambda/negative/semantic/object_field_static.ls",
        "field 'a' of object 'Obj' expects int, but got string");
}

TEST_F(NegativeScriptTest, ObjectMissingRequiredFieldIsStaticError) {
    ExpectErrorMessage("test/lambda/negative/semantic/object_field_missing.ls",
        "object 'Obj' is missing required field 'a'");
}

TEST_F(NegativeScriptTest, CountedArrayContractRejectsWrongLengthOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/array_count_flat_contract.ls",
        false, "error[E201]: type check at declaration 'a' failed: expected int[3]");
}

TEST_F(NegativeScriptTest, CountedArrayContractRejectsWrongInnerAxisOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/array_count_nested_contract.ls",
        false, "error[E201]: type check at declaration 'g' failed: expected int[2][3]");
}

TEST_F(NegativeScriptTest, CountedArrayContractRejectsWrongNdShapeOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/array_count_ndim_contract.ls",
        false, "error[E201]: type check at declaration 'm' failed: expected int[2][3]");
}

// D3.3.3v3: a push changes a counted array's length without a boundary, so the
// next `int[3]` boundary re-checks it. The JIT elided the check for a binding
// declared with that contract, and the interned certificate stayed valid.
TEST_F(NegativeScriptTest, CountedArrayContractRechecksResizedBindingOnEveryTier) {
    ExpectRejectedOnEveryTier("test/lambda/negative/runtime/array_count_resized_binding.ls",
        true, "failed: expected int[3], got array[num]; validator: Array has 4 elements");
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

// S11.1.7: `none` admits no value, so a known value never crosses into it.
// An unknown one is left to the runtime check (test/lambda/type_none.ls).
TEST_F(NegativeScriptTest, StaticNoneDeclarationRejectsKnownValue) {
    ExpectErrorMessage("test/lambda/negative/semantic/none_type_rejections.ls",
        "cannot initialize 'wrong_none' of type none with int");
}

TEST_F(NegativeScriptTest, StaticNoneReturnRejectsKnownBody) {
    ExpectErrorMessage("test/lambda/negative/semantic/none_type_rejections.ls",
        "function 'none_return' body returns type string, declared return type none");
}

// S11.1.7 + S16.10.1v2: `none` is a base-type word, barred as a binding name.
TEST_F(NegativeScriptTest, NoneIsBarredAsBindingName) {
    ExpectErrorMessage("test/lambda/negative/semantic/none_binding_barred.ls",
        "'none' is a reserved keyword and cannot be used as a name");
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

// D7.2.2 (LR07-17): a module whose init ends in an ordinary error never
// becomes importable. The JIT had dropped the init result and run the importer
// against the module's half-set slots.
TEST_F(NegativeScriptTest, ImportInitErrorBlocksExecution) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (const char* tier : tiers) {
        ScriptResult result = run_lambda_script(
            "test/lambda/negative/import_init_error_driver.ls", false, tier);
        EXPECT_NE(result.exit_code, 0) << "tier=" << tier;
        EXPECT_NE(strstr(result.output.c_str(), "error[E201]"), nullptr)
            << "tier=" << tier << "\nOutput: " << result.output;
        EXPECT_EQ(strstr(result.output.c_str(), "DRIVER_RAN"), nullptr)
            << "Importer executed after its module's init failed, tier=" << tier
            << "\nOutput: " << result.output;
    }
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

TEST_F(NegativeScriptTest, SyntaxError_PathDotBeforeIndex) {
    ExpectErrorCode("test/lambda/negative/syntax/path_dot_before_index.ls", "error[E100]");
}

TEST_F(NegativeScriptTest, SyntaxError_PathRelativeDotBeforeIndex) {
    ExpectErrorCode("test/lambda/negative/syntax/path_rel_dot_before_index.ls", "error[E100]");
}

TEST_F(NegativeScriptTest, SyntaxError_PathTrailingDot) {
    ExpectErrorCode("test/lambda/negative/syntax/path_trailing_dot.ls", "error[E100]");
}

TEST_F(NegativeScriptTest, SyntaxError_PathRetiredRooted) {
    ExpectErrorCode("test/lambda/negative/syntax/path_retired_rooted.ls", "error[E100]");
}

TEST_F(NegativeScriptTest, SyntaxError_LetOutsideList) {
    ExpectErrorCode("test/lambda/negative/syntax/let_outside_list.ls", "error[E100]");
    ExpectErrorMessage("test/lambda/negative/syntax/let_outside_list.ls",
        "'let' binds as an expression only inside a parenthesized list");
}

TEST_F(NegativeScriptTest, SyntaxError_WhereFilterNamesPipeFilter) {
    // S10.3.1v3: the retired infix `where` points at the filter stage `|:`
    ExpectErrorMessage("test/lambda/negative/syntax/where_filter_retired.ls",
        "error[E100]: 'where' is not a filter operator; write '|:'");
}

TEST_F(NegativeScriptTest, SyntaxError_PipeFilterGluedToArmColon) {
    // S10.1.6: longest match lexes `|:` in `case int |: …`, which stays an error
    ExpectErrorMessage("test/lambda/negative/syntax/pipe_filter_glued_case.ls",
        "error[E100]: expected ':' or '{' after match arm");
}

TEST_F(NegativeScriptTest, SemanticError_FilterBodyWithoutCurrentItem) {
    // S10.1.6: a `|:` body must mention `~`; `|>` would read it as application
    ExpectErrorMessage("test/lambda/negative/semantic/filter_body_no_current.ls",
        "error[E238]: filter body must mention `~`");
}

TEST_F(NegativeScriptTest, SyntaxError_SignatureReturnLineStart) {
    ExpectErrorCode("test/lambda/negative/syntax/fn_signature_return_line_start.ls",
        "error[E100]");
    ExpectErrorMessage("test/lambda/negative/syntax/fn_signature_return_line_start.ls",
        "a function type's return type starts on the line of its ')'");
}

TEST_F(NegativeScriptTest, SyntaxError_ImportSlashSeparator) {
    ExpectErrorMessage("test/lambda/negative/syntax/import_slash_separator.ls",
        "import paths separate names with '.'");
}

TEST_F(NegativeScriptTest, SyntaxError_PathIntegerKeyRange) {
    ExpectErrorCode("test/lambda/negative/syntax/path_integer_key_range.ls", "error[E103]");
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

// S12.1.1v2 (LR03-24): every `that` predicate is `fn` context -- a declared
// type, a field and an object-level constraint, and an inline arm in a `pn`.
// The colour walk never entered a type expression, so the JIT ran the effect
// inside `is` while T0's allow-list answered `false`.
TEST_F(NegativeScriptTest, SemanticError_PredicateIsFnContext) {
    static const char* const sites[] = {"7:21", "8:24", "8:42", "11:19"};
    for (const char* site : sites) {
        char expected[160];
        snprintf(expected, sizeof(expected), "predicate_calls_pn.ls:%s: error[E224]: "
            "'eff' is a procedure (pn) and cannot be called from a function (fn)", site);
        ExpectErrorMessage("test/lambda/negative/semantic/predicate_calls_pn.ls", expected);
    }
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

TEST_F(NegativeScriptTest, SemanticError_ProcMethodCannotBeTakenAsValue) {
    ExpectErrorMessage("test/lambda/negative/semantic/proc_method_reference.ls",
        "procedure method 'increment' cannot be used as a value; call it directly");
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

// S7.11.1v2: JIT calls bypass T0's depth counter, so this must exercise the
// native entry guard rather than relying on AUTO's interpreter fallback.
TEST_F(NegativeScriptTest, RuntimeError_StackOverflowJit) {
    const char* script = "test/lambda/negative/runtime/stack_overflow.ls";
    ScriptResult result = run_lambda_script(script, false, "jit");
    EXPECT_NE(result.exit_code, 0) << "Expected JIT script to fail: " << script;
    EXPECT_NE(strstr(result.output.c_str(), "error[E308]: Stack overflow"), nullptr)
        << "Expected recoverable JIT stack overflow:\n" << result.output;
    EXPECT_EQ(result.output.find("Segmentation fault"), std::string::npos)
        << "JIT script crashed:\n" << result.output;
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

TEST_F(NegativeScriptTest, FuzzyCrash_ErrorNumericMember) {
    ExpectErrorCode("test/lambda/negative/fuzzy_crashes/error_numeric_member.ls",
                    "Script execution failed");
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

TEST_F(NegativeScriptTest, TypeBinderDiagnosticsRemainSpecific) {
    // S11.4.8: these are semantic binder diagnostics, not generic parse errors.
    ExpectErrorMessage("test/lambda/negative/semantic/type_binder_collision.ls",
        "binder 'int' conflicts with a base type");
    ExpectErrorMessage("test/lambda/negative/semantic/type_binder_bound_mismatch.ls",
        "binder sites for 'T' must share one bound");
    ExpectErrorMessage("test/lambda/negative/semantic/type_binder_forward_ref.ls",
        "binder 'T' must be introduced before it is referenced");
    ExpectErrorMessage("test/lambda/negative/semantic/type_binder_return.ls",
        "a binder is not allowed in a return type");
    ExpectErrorMessage("test/lambda/negative/semantic/type_binder_trailing_that.ls",
        "a binder must follow the complete parameter contract");
}

TEST_F(NegativeScriptTest, TypeSubtypeDiagnosticsRemainSpecific) {
    // S11.1.4: `<:` is a type relation, with function variance deferred by TGO14(b).
    ExpectErrorMessage("test/lambda/negative/semantic/type_subtype_non_type.ls",
        "operator `<:` requires type values");
    ExpectErrorMessage("test/lambda/negative/semantic/type_subtype_function.ls",
        "operator `<:` does not support function types until variance is specified");
}

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
