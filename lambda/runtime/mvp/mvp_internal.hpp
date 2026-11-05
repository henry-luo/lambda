#pragma once

#include "mvp.hpp"
#include "../mir_emitter_shared.hpp"
#include "../lambda-error.h"

static inline bool mvp_boxed_entry(AstFuncNode* function) {
    return function && (((TypeFunc*)function->type)->is_public || function->node_type == AST_NODE_FUNC_EXPR);
}

static inline bool mvp_parameter_receiver(AstFuncNode* function) {
    if (mvp_boxed_entry(function)) return true;
    for (AstNamedNode* node = function ? function->param : NULL; node; node = (AstNamedNode*)node->next) {
        TypeParam* parameter = lambda_type_param(node->type);
        if (parameter && parameter->is_optional) return true;
    }
    return false;
}

struct MvpFunction {
    AstFuncNode* ast;
    const char* name;
    MIR_item_t target;
    FnVariantAnalysis abi;
    Type* declared_result;
    Type* result_contract;
    AstFunctionId id;
    uint32_t inline_cost;
    bool imported;
};

struct MvpErrorTarget {
    MIR_label_t done;
    MIR_reg_t error;
};
struct MvpScope {
    MvpScope* parent;
    MvpErrorTarget failure;
};
struct MvpIntRange {
    bool finite;
    int64_t lower;
    int64_t upper;
};
struct MvpBinding {
    NameEntry* entry;
    Type* admission_contract;
    Type* producer_success_contract;
    Type* contract;
    ValueRep rep;
    MIR_reg_t reg;
    int scalar_home;
    MvpScope* scope;
    AstFunctionId owner;
    MvpIntRange range;
    MIR_reg_t var_home;
    MIR_reg_t array_native;
    MIR_reg_t array_float_native;
    MIR_reg_t record_writable;
    MIR_reg_t record_readable;
    MIR_reg_t array_writable;
    MIR_reg_t array_length;
    int64_t array_extent;
    MIR_item_t module_slot;
    MIR_item_t module_ref;
    bool module_scalar;
    bool module_managed;
    bool iteration_binding;
    bool array_written;
    bool coordinate_written;
    bool scalar_snapshot;
    bool array_accessed;
    ArrayRepCert* array_cert;
    MIR_reg_t dense_items;
    bool constant_float;
    double float_value;
    Item constant_item;
    bool constant_ready;
    bool readonly_literal;
    bool inferred_array;
    bool array_present;
    bool record_written, record_accessed;
    bool string_buffer_owned;
};

struct LambdaMvpModule {
    Transpiler* tp;
    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t consts;
    MIR_item_t layout;
    ArrayList* property_keys;
    HashMap* imports;
    MvpFunction* functions;
    uint32_t function_count;
    ArrayList* imported_functions;
    MvpBinding* bindings;
    uint32_t binding_count;
    MvpFunction entry;
    MvpFunction* main;
    LambdaMvpStats stats;
    int label_counter;
    bool failed;
    Type* nullable_int;
    Type* nullable_bool;
    Type* nullable_float;
};

struct MvpControl {
    MvpControl* parent;
    MIR_label_t next;
    MIR_label_t done;
};

struct MvpInline {
    MvpInline* parent;
    MvpFunction* function;
    MIR_reg_t result;
    MIR_label_t done;
    MIR_reg_t error;
};

struct MvpFloatConstant {
    MvpFloatConstant* next;
    uint64_t bits;
    MIR_reg_t reg;
};

struct MvpStringByte {
    MvpStringByte* next;
    MvpBinding* owner;
    AstNode* key;
    MIR_reg_t byte;
};

struct MvpModuleLocation {
    MvpModuleLocation* next;
    MIR_item_t reference;
    MIR_reg_t descriptor, state, slot;
};

struct MvpCompiler {
    LambdaMvpModule* module;
    MvpFunction* function;
    MirEmitter em;
    MvpControl* control;
    MvpScope* scope;
    MvpInline* inlining;
    MvpErrorTarget* condition_failure;
    AstNode* consuming_array_join;
    MIR_reg_t current_error;
    uint32_t inline_cost;
    MvpFloatConstant* float_constants;
    MvpStringByte* string_bytes;
    MvpModuleLocation* module_locations;
    MIR_insn_t constant_cursor;
    MIR_label_t tail_entry;
    MIR_reg_t argument_span;
    MIR_insn_t argument_fixup;
    int argument_count;
    int argument_active;
    MvpBinding* loop_array;
    MIR_reg_t loop_array_items;
    bool dense_arrays;
    bool guarded_allocating;
    MvpFunction* dense_callee;
    bool dense_calls;
    bool checked_bitwise;
    bool checked_float_compare;
};

struct MvpArraySumPlan {
    MvpBinding* array;
    MvpBinding* counter;
    MvpBinding* sum;
    AstNode* limit;
    int64_t max_count;
    bool inclusive;
};

struct MvpArrayCopyPlan {
    MvpBinding* source;
    MvpBinding* destination;
    MvpBinding* read_index;
    MvpBinding* write_index;
    MvpBinding* counter;
    AstNode* limit;
    int64_t read_offset, write_offset;
};

struct MvpArrayReversePlan {
    MvpBinding* array;
    MvpBinding* first;
    MvpBinding* last;
};

struct MvpNumericLoopPlan {
    MvpBinding** guards;
    uint32_t count;
    int64_t radius;
    unsigned iterations;
};

struct MvpScaledInteger {
    MvpIntRange numerator;
    unsigned fraction_bits;
};

struct MvpIndexLoopPlan {
    MvpBinding* array;
    MvpBinding* counter;
    MvpBinding* follower;
    MvpBinding* limit;
    MvpBinding* first;
    AstLoopControlNode* loop;
};

bool mvp_reject(LambdaMvpModule* module, AstNode* node, const char* reason);
ValueRep mvp_rep(Type* type);
Type* mvp_contract(AstNode* declaration);
MvpFunction* mvp_function(LambdaMvpModule* module, AstFuncNode* ast);
MvpBinding* mvp_binding(LambdaMvpModule* module, NameEntry* entry);
Type* mvp_binding_return_contract(LambdaMvpModule* module, NameEntry* entry);
bool mvp_proc_has_final_return(AstFuncNode* function);
AstNode* mvp_single_statement(AstNode* node);
AstNode* mvp_function_result_expression(MvpFunction* function);
bool mvp_prefers_packed_tensor(LambdaMvpModule* module, AstNode* literal);
Type* mvp_value_contract(LambdaMvpModule* module, AstNode* node, int depth = 0, bool dense_arrays = false);
MvpIntRange mvp_int_range(LambdaMvpModule* module, AstNode* node, int depth = 0);
bool mvp_same_integer_expression(LambdaMvpModule* module, AstNode* left, AstNode* right, int depth = 0);
MvpScaledInteger mvp_scaled_integer(LambdaMvpModule* module, AstNode* node, int depth = 0);
Type* mvp_array_element(Type* contract);
bool mvp_array_contract(Type* contract);
bool mvp_consuming_array_join(LambdaMvpModule* m, MvpBinding* binding, AstNode* value);
bool mvp_binary_bitwise(SysFunc fn);
bool mvp_needs_number_home(Type* contract);
Type* mvp_scalar_success_contract(LambdaMvpModule* module, Type* contract);
MvpBinding* mvp_null_condition(LambdaMvpModule* module, AstNode* condition,
    Type** present, bool* null_when_true);
ShapeEntry* mvp_member_field(LambdaMvpModule* module, AstFieldNode* node,
    TypeMap** shape = NULL, bool success_only = false);
Type* mvp_match_type(AstNode* pattern);
bool mvp_boundary_proven(Type* source, Type* destination);
bool mvp_parameter_borrowable(LambdaMvpModule* module, AstNamedNode* parameter, bool forwarding = true);
bool mvp_pure_key(AstNode* node, int depth = 0, bool allow_pure_calls = false);
bool mvp_index_key(LambdaMvpModule* module, AstNode* node, int depth = 0);
bool mvp_parameter_storage_stable(LambdaMvpModule* module, AstNamedNode* parameter);
TypeMap* mvp_record_contract(Type* contract, bool scalar_only);
Type* mvp_contextual_contract(LambdaMvpModule* module, AstNode* node);
TypeMap* mvp_contextual_map_contract(LambdaMvpModule* module, AstNode* node);
TypeMap* mvp_member_candidate(LambdaMvpModule* module, NameRef name);
bool mvp_native_math(const SysFuncInfo* info, TypeId input);
Type* mvp_nullable_contract(LambdaMvpModule* module, Type* base);
AstFieldNode* mvp_string_index(LambdaMvpModule* module, AstNode* node, int depth = 0);
bool mvp_array_index_present(LambdaMvpModule* module, MvpBinding* binding, AstNode* key);
MvpBinding* mvp_local_binding(LambdaMvpModule* module, AstNode* node);
MvpBinding* mvp_place_root(LambdaMvpModule* module, AstNode* node);
bool mvp_array_sum_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpArraySumPlan* plan);
bool mvp_array_copy_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpArrayCopyPlan* plan);
bool mvp_array_reverse_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpArrayReversePlan* plan);
bool mvp_numeric_loop_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpNumericLoopPlan* plan);
bool mvp_numeric_region_advance(LambdaMvpModule* module, AstNode* region, MvpNumericLoopPlan* plan);
bool mvp_numeric_inline_plan(LambdaMvpModule* module, MvpFunction* function, MvpNumericLoopPlan* plan);
bool mvp_index_loop_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpIndexLoopPlan* plan);
bool mvp_index_function_plan(LambdaMvpModule* module, MvpFunction* function, MvpIndexLoopPlan* plan);
bool mvp_emit_function(LambdaMvpModule* module, MvpFunction* function);

