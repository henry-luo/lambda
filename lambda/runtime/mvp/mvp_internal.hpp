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
};

bool mvp_reject(LambdaMvpModule* module, AstNode* node, const char* reason);
ValueRep mvp_rep(Type* type);
Type* mvp_contract(AstNode* declaration);
MvpFunction* mvp_function(LambdaMvpModule* module, AstFuncNode* ast);
MvpBinding* mvp_binding(LambdaMvpModule* module, NameEntry* entry);
bool mvp_proc_has_final_return(AstFuncNode* function);
Type* mvp_value_contract(LambdaMvpModule* module, AstNode* node, int depth = 0);
MvpIntRange mvp_int_range(LambdaMvpModule* module, AstNode* node, int depth = 0);
bool mvp_emit_function(LambdaMvpModule* module, MvpFunction* function);

