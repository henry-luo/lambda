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
    MIR_reg_t argument_span;
    MIR_insn_t argument_fixup;
    int argument_count;
    MvpBinding* loop_array;
    MIR_reg_t loop_array_items;
};

struct MvpArraySumPlan {
    MvpBinding* array;
    MvpBinding* counter;
    MvpBinding* sum;
    AstNode* limit;
    int64_t max_count;
};

bool mvp_reject(LambdaMvpModule* module, AstNode* node, const char* reason);
ValueRep mvp_rep(Type* type);
Type* mvp_contract(AstNode* declaration);
MvpFunction* mvp_function(LambdaMvpModule* module, AstFuncNode* ast);
MvpBinding* mvp_binding(LambdaMvpModule* module, NameEntry* entry);
bool mvp_proc_has_final_return(AstFuncNode* function);
Type* mvp_value_contract(LambdaMvpModule* module, AstNode* node, int depth = 0);
MvpIntRange mvp_int_range(LambdaMvpModule* module, AstNode* node, int depth = 0);
Type* mvp_array_element(Type* contract);
MvpBinding* mvp_local_binding(LambdaMvpModule* module, AstNode* node);
bool mvp_array_sum_plan(LambdaMvpModule* module, AstLoopControlNode* loop, MvpArraySumPlan* plan);
bool mvp_emit_function(LambdaMvpModule* module, MvpFunction* function);

