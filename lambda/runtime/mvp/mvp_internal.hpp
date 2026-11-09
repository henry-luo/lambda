#pragma once

#include "mvp.hpp"
#include "../mir_emitter_shared.hpp"
#include "../lambda-error.h"
#include "../type_contract.hpp"

// all incoming-edge proofs use the receiver's formal position (S10.1.2v4).
static inline int mvp_call_argument_count(AstCallNode* call) {
    if (!call) return 0;
    AstFuncNode* target = ast_call_has_named_args(call) ? ast_direct_call_function(call) : NULL;
    int supplied = ast_linked_node_count(call->argument) + (call->pipe_inject ? 1 : 0);
    if (!target) return supplied;
    int fixed = ast_linked_node_count(target->param);
    return ((TypeFunc*)target->type)->is_variadic && supplied > fixed ? supplied : fixed;
}

static inline AstNode* mvp_call_argument(AstCallNode* call, int position) {
    if (!call) return NULL;
    if (ast_call_has_named_args(call)) {
        AstNode* resolved[LAMBDA_MAX_FUNCTION_ARGS] = {};
        ast_resolve_call_arguments(call, ast_direct_call_function(call), ast_linked_node_count(call->argument), resolved);
        return position >= 0 && position < LAMBDA_MAX_FUNCTION_ARGS ? resolved[position] : NULL;
    }
    return ast_positional_call_argument(call->argument, call->pipe_inject ? call->pipe_receiver : NULL, position);
}

static inline bool mvp_function_expression(const AstNode* node) {
    return node && (node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC) &&
        !((const AstFuncNode*)node)->name;
}

static inline bool mvp_type_has_environment(Type* type) {
    type = type_field_unwrap_simple_decl(type);
    for (TypeNominal* nominal = type_nominal_record(type); nominal; nominal = nominal->base)
        for (TypeMethod* method = nominal->methods; method; method = method->next)
            if (method->ast_def && method->ast_def->captures) return true;
    return false;
}

static inline bool mvp_dynamic_type_binding(NameEntry* entry) {
    return entry && entry->node && ast_definition_denotes_type(entry->node) &&
        mvp_type_has_environment(entry->node->type);
}

static inline bool mvp_static_type_capture(const FnCapture* capture) {
    return capture && capture->entry && ast_definition_denotes_type(capture->entry->node) &&
        !mvp_dynamic_type_binding(capture->entry);
}

static inline bool mvp_open_value_contract(Type* contract) {
    return contract == &TYPE_ANY || contract == &TYPE_ANY_NO_ERROR;
}

static inline bool mvp_exact_membership_domain(Type* type) {
    return type == &TYPE_NULL || type == &TYPE_ERROR || type == &TYPE_BOOL || type == &TYPE_STRING ||
        type == &TYPE_SYMBOL || type == &TYPE_BINARY || type == &TYPE_DTIME || type == &TYPE_MAP;
}

static inline bool mvp_membership_proven(Type* source, Type* expected) {
    // leave schema-top predicate behavior with the shared kernel while the
    // contract/schema uniformity question remains open (SO7).
    if (!source || !expected || expected == &TYPE_ANY) return false;
    return lambda_type_contract_is_subtype(source, expected);
}

static inline bool mvp_direct_callee_node(const AstIndex* index, AstNodeId id) {
    AstNode* node = id < index->count ? index->nodes[id] : NULL;
    AstNodeId parent = ast_index_parent_id(index, id);
    while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
        parent = ast_index_parent_id(index, parent);
    AstNode* consumer = parent < index->count ? index->nodes[parent] : NULL;
    return consumer && consumer->node_type == AST_NODE_CALL_EXPR &&
        !ast_index_call_is_task_launch(index, parent) &&
        ast_unwrap_primary(((AstCallNode*)consumer)->function) == node;
}

static inline AstFuncNode* mvp_function_value_reference(const AstIndex* index, AstNodeId id) {
    AstNode* node = id < index->count ? index->nodes[id] : NULL;
    if (!node || node->node_type != AST_NODE_IDENT) return NULL;
    NameEntry* entry = ((AstIdentNode*)node)->entry;
    AstNode* definition = entry ? entry->node : NULL;
    if (!definition || !ast_index_node_is_function(definition) || mvp_direct_callee_node(index, id)) return NULL;
    return (AstFuncNode*)definition;
}

struct MvpFunction {
    AstFuncNode* ast;
    AstViewNode* view;
    AstEventHandler* handler;
    const char* template_ref;
    TypeObject* method_owner;
    const char* name;
    const char* js_export_name;
    Script* js_export_owner;
    MIR_item_t target;
    MIR_item_t js_export_ref;
    FnVariantAnalysis abi;
    Type* declared_result;
    Type* result_contract;
    Type* scalar_result_fallback;
    Type** captured_type_references;
    NameEntry** captured_type_entries;
    uint16_t captured_type_count;
    AstFunctionId id;
    AstFunctionId parent;
    uint32_t inline_cost;
    bool imported;
    bool boxed_entry;
    bool no_value_result;
    bool owns_open_scopes;
};

static inline bool mvp_edit_model_place(MvpFunction* function, AstNode* object) {
    if (!function || !function->handler || !function->view->is_edit) return false;
    object = ast_unwrap_primary_to_leaf(object);
    while (object && (object->node_type == AST_NODE_MEMBER_EXPR || object->node_type == AST_NODE_INDEX_EXPR))
        object = ast_unwrap_primary_to_leaf(((AstFieldNode*)object)->object);
    return object && object->node_type == AST_NODE_CURRENT_ITEM;
}

static inline bool mvp_variadic_function(const MvpFunction* function) {
    return function && function->ast && ((TypeFunc*)function->ast->type)->is_variadic;
}

static inline uint16_t mvp_function_binder_count(const MvpFunction* function) {
    return function && function->ast ? ((TypeFunc*)function->ast->type)->binder_count : 0;
}

static inline ShapeEntry* mvp_method_field(MvpFunction* function, NameEntry* entry) {
    TypeObject* owner = function ? function->method_owner : NULL;
    for (ShapeEntry* field = owner ? owner->shape : NULL; field; field = typemap_next_field(owner, field))
        if (field->binding == entry) return field;
    return NULL;
}

static inline FnCapture* mvp_function_capture(MvpFunction* function, NameEntry* entry) {
    for (FnCapture* capture = function && function->ast ? function->ast->captures : NULL;
            capture; capture = capture->next)
        if (capture->entry == entry) return capture;
    return NULL;
}

static inline bool mvp_parameter_receiver(MvpFunction* function) {
    if (function && function->boxed_entry) return true;
    for (AstNamedNode* node = function && function->ast ? function->ast->param : NULL;
            node; node = (AstNamedNode*)node->next) {
        TypeParam* parameter = lambda_type_param(node->type);
        if (parameter && parameter->is_optional) return true;
    }
    return false;
}

enum MvpCleanupKind { MVP_CLEANUP_TASK, MVP_CLEANUP_TRANSACTION, MVP_CLEANUP_RECOVERY, MVP_CLEANUP_KEYS };
struct MvpCleanup {
    MvpCleanup* parent;
    MIR_reg_t record;
    MvpCleanupKind kind;
};

struct MvpErrorTarget {
    MIR_label_t done;
    MIR_reg_t error;
    MvpCleanup* cleanup_base;
    bool unwinds;
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
    const char* state_name;
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
    MIR_reg_t capture_home;
    MIR_reg_t method_field_present;
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
    bool capture_binding;
    bool array_written;
    bool tensor_required;
    bool scalar_snapshot;
    bool array_accessed;
    ArrayRepCert* array_cert;
    MIR_reg_t dense_items;
    bool constant_float;
    bool closed_parameter;
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
    MvpCleanup* cleanup_base;
    MvpCleanup* break_cleanup_base;
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
    MIR_reg_t environment;
    MIR_reg_t vargs_roots;
    MIR_reg_t binder_env;
    MIR_reg_t captured_types;
    MIR_reg_t method_receiver;
    MIR_reg_t method_environment;
    MIR_reg_t admission_status;
    MvpControl* control;
    MvpScope* scope;
    MvpCleanup* cleanups;
    ArrayList* shared_bindings;
    MvpInline* inlining;
    MvpErrorTarget* condition_failure;
    MvpErrorTarget* output_failure;
    unsigned output_inline_depth;
    AstNode* consuming_array_join;
    MIR_reg_t current_error;
    MIR_reg_t current_item;
    MIR_reg_t current_index;
    MIR_reg_t current_parent;
    MIR_reg_t current_root;
    MIR_reg_t view_model;
    MIR_reg_t last_index_item;
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
Type* mvp_inferred_scalar_contract(Type* contract);
Type* mvp_join_value_contracts(LambdaMvpModule* module, Type* left, Type* right);
Type* mvp_bound_array_contract(Type* contract);
Type* mvp_contract(AstNode* declaration);
MvpFunction* mvp_function(LambdaMvpModule* module, AstFuncNode* ast);
AstFunctionId mvp_node_owner(LambdaMvpModule* module, AstNodeId id);
MvpFunction* mvp_require_function_reference(LambdaMvpModule* module, AstFuncNode* target,
    AstNode* callee, Script* owner = NULL);
MvpBinding* mvp_binding(LambdaMvpModule* module, NameEntry* entry);
Type* mvp_binding_return_contract(LambdaMvpModule* module, NameEntry* entry);
bool mvp_proc_has_final_return(AstFuncNode* function);
AstNode* mvp_single_statement(AstNode* node);
AstNode* mvp_function_result_expression(MvpFunction* function);
bool mvp_prefers_packed_tensor(LambdaMvpModule* module, AstNode* literal);
bool mvp_native_range_source(AstNode* source);
Type* mvp_value_contract(LambdaMvpModule* module, AstNode* node, int depth = 0, bool dense_arrays = false);
MvpIntRange mvp_int_range(LambdaMvpModule* module, AstNode* node, int depth = 0);
bool mvp_same_integer_expression(LambdaMvpModule* module, AstNode* left, AstNode* right, int depth = 0);
MvpScaledInteger mvp_scaled_integer(LambdaMvpModule* module, AstNode* node, int depth = 0);
Type* mvp_array_element(Type* contract);
Type* mvp_module_array_literal_element(LambdaMvpModule* module, MvpBinding* binding, AstNode* source);
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
bool mvp_match_arm_covers(Type* subject, AstMatchArm* arm);
bool mvp_boundary_proven(Type* source, Type* destination);
bool mvp_admission_proven(LambdaMvpModule* module, Type* source, Type* destination);
// private array observations require the exact primitive element domain;
// numeric subtyping cannot turn int elements into float elements (D4.3.3).
static inline bool mvp_private_domain_proven(Type* source, Type* destination) {
    Type* element = mvp_array_element(destination);
    return source && destination && source->type_id == destination->type_id &&
        (!element || (mvp_array_element(source) == element &&
            lambda_array_contract_is_plain(source))) && mvp_boundary_proven(source, destination);
}
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
Type* mvp_handler_pass_contract(LambdaMvpModule* module, AstHandlerNode* handler, Type* operand);
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

