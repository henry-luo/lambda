#pragma once

#include "mvp.hpp"
#include "../mir_emitter_shared.hpp"
#include "../lambda-error.h"

struct MvpFunction {
    AstFuncNode* ast;
    const char* name;
    MIR_item_t target;
    FnVariantAnalysis abi;
    Type* result_contract;
    AstFunctionId id;
    uint32_t inline_cost;
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
    Type* contract;
    ValueRep rep;
    MIR_reg_t reg;
    int scalar_home;
    MvpScope* scope;
    AstFunctionId owner;
    MvpIntRange range;
    MIR_reg_t var_home;
    MIR_reg_t array_native;
    MIR_reg_t array_writable;
    MIR_reg_t array_length;
    int64_t array_extent;
    MIR_item_t module_slot;
    bool module_scalar;
    bool array_written;
    bool array_accessed;
    ArrayRepCert* array_cert;
    MIR_reg_t dense_items;
    bool constant_float;
    double float_value;
};

struct LambdaMvpModule {
    Transpiler* tp;
    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t consts;
    HashMap* imports;
    MvpFunction* functions;
    uint32_t function_count;
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

struct MvpCompiler {
    LambdaMvpModule* module;
    MvpFunction* function;
    MirEmitter em;
    MvpControl* control;
    MvpScope* scope;
    MvpInline* inlining;
    uint32_t inline_cost;
    MvpFloatConstant* float_constants;
    MIR_insn_t constant_cursor;
    MIR_label_t tail_entry;
    MIR_reg_t argument_span;
    MIR_insn_t argument_fixup;
    int argument_count;
    int argument_active;
    MvpBinding* loop_array;
    MIR_reg_t loop_array_items;
    bool dense_arrays;
    MvpFunction* dense_callee;
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
bool mvp_proc_has_final_return(AstFuncNode* function);
Type* mvp_value_contract(LambdaMvpModule* module, AstNode* node, int depth = 0, bool dense_arrays = false);
MvpIntRange mvp_int_range(LambdaMvpModule* module, AstNode* node, int depth = 0);
bool mvp_same_integer_expression(LambdaMvpModule* module, AstNode* left, AstNode* right, int depth = 0);
MvpScaledInteger mvp_scaled_integer(LambdaMvpModule* module, AstNode* node, int depth = 0);
Type* mvp_array_element(Type* contract);
bool mvp_native_math(const SysFuncInfo* info, TypeId input);
Type* mvp_nullable_contract(LambdaMvpModule* module, Type* base);
AstFieldNode* mvp_string_index(LambdaMvpModule* module, AstNode* node, int depth = 0);
bool mvp_array_index_present(LambdaMvpModule* module, MvpBinding* binding, AstNode* key);
MvpBinding* mvp_local_binding(LambdaMvpModule* module, AstNode* node);
bool mvp_array_sum_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpArraySumPlan* plan);
bool mvp_array_copy_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpArrayCopyPlan* plan);
bool mvp_numeric_loop_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpNumericLoopPlan* plan);
bool mvp_numeric_loop_advance(LambdaMvpModule* module, AstLoopControlNode* loop, MvpNumericLoopPlan* plan);
bool mvp_index_loop_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpIndexLoopPlan* plan);
bool mvp_index_function_plan(LambdaMvpModule* module, MvpFunction* function, MvpIndexLoopPlan* plan);
bool mvp_emit_function(LambdaMvpModule* module, MvpFunction* function);

