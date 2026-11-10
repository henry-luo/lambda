#include "mvp_internal.hpp"
#include "../interp.hpp"
#include "lambda/io/mark_output_builder.hpp"
#include "../type_contract.hpp"
#include "../mir_loop_analysis.hpp"
#include "../mir_cow_analysis.hpp"
#include "../mir_shape_candidates.hpp"
#include "../lambda-number.hpp"
#include "../lambda-number-types.hpp"
#include "../re2_wrapper.hpp"
#include "lib/host_paths.h"

bool mvp_reject(LambdaMvpModule* m, AstNode* node, const char* reason) {
    if (m->failed) return false;
    m->failed = true;
    LambdaSourcePoint point = lambda_source_point_at(m->tp->source,
        node ? node->source_span.start_byte : 0);
    SourceLocation location = {m->tp->reference, point.row + 1, point.column + 1,
        point.row + 1, point.column + 1, m->tp->source};
    LambdaError* error = err_createf(ERR_NOT_IMPLEMENTED, &location,
        "Lambda MIR MVP: %s", reason);
    if (error) {
        if (m->tp->errors) arraylist_append(m->tp->errors, error);
        else { err_print(error); err_free(error); }
    }
    m->tp->error_count++;
    log_error("lambda-mvp-admission: %s at byte %u", reason,
        node ? node->source_span.start_byte : 0);
    return false;
}

// a removed error-only arm is bottom, not a missing type descriptor.
static Type* normal_domain(LambdaMvpModule* m, Type* contract) {
    Type* normal = lambda_type_remove_error(m->tp->pool, contract);
    return normal ? normal : &TYPE_NONE;
}

ValueRep mvp_rep(Type* type) {
    if (!type) return VALUE_REP_ITEM;
    // dependent contracts describe selected values, not Type* carriers. Type
    // values themselves can also be GC-owned (D2.3.1, D4.1.4).
    if (type == &TYPE_TYPE || lambda_type_contract_has_binder(type, true)) return VALUE_REP_ITEM;
    // an ordinary error alternative needs Item transport until discharge;
    // its type-expression pointer is not a runtime value (S7.8.1, D2.3.5).
    if (lambda_type_accepts_error(type)) return VALUE_REP_ITEM;
    Type* plain = type_field_unwrap_simple_decl(type);
    if (plain && plain->type_id == LMD_TYPE_TYPE && plain->kind == TYPE_KIND_UNARY &&
            (((TypeUnary*)plain)->op == OPERATOR_ZERO_MORE || ((TypeUnary*)plain)->op == OPERATOR_ONE_MORE ||
             ((TypeUnary*)plain)->op == OPERATOR_REPEAT)) return VALUE_REP_ITEM;
    // optional wide integers have no spare absence word. Compact sized
    // values retain their self-describing Item transport.
    bool optional = false;
    Type* scalar = lambda_type_nullable_lane_base(plain, &optional);
    if (scalar && (scalar->type_id == LMD_TYPE_NUM_SIZED || (optional &&
            (scalar->type_id == LMD_TYPE_INT64 || scalar->type_id == LMD_TYPE_UINT64)))) return VALUE_REP_ITEM;
    // full contracts, including nullable and union types, select their carrier.
    return lambda_canonical_rep(type);
}

Type* mvp_inferred_scalar_contract(Type* contract) {
    if (contract && contract->is_literal &&
            (contract->type_id == LMD_TYPE_BOOL || contract->type_id == LMD_TYPE_INT ||
             contract->type_id == LMD_TYPE_FLOAT || contract->type_id == LMD_TYPE_STRING ||
             contract->type_id == LMD_TYPE_DECIMAL))
        return lambda_canonical_type_for_id(contract->type_id);
    return contract;
}

Type* mvp_join_value_contracts(LambdaMvpModule* m, Type* left, Type* right) {
    // homogeneous observed literals share a carrier, not a receiving literal
    // refinement. Mixed numeric tags keep the ordinary union (D4.3.1).
    if (left && right && left->type_id == right->type_id) {
        left = mvp_inferred_scalar_contract(left);
        right = mvp_inferred_scalar_contract(right);
    }
    return lambda_type_union_normalized(m->tp->pool, left, right);
}

Type* mvp_contract(AstNode* node) {
    if (!node) return &TYPE_ANY;
    if (node->node_type == AST_NODE_PARAM) {
        TypeParam* param = lambda_type_param(node->type);
        return param && param->contract_type ? param->contract_type : node->type;
    }
    if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* decl = (AstDeclaratorNode*)node;
        if (decl->is_type_definition) return &TYPE_TYPE;
        if (decl->declared_type) return decl->declared_type;
        // inference describes the value of a type expression, not its members.
        if (ast_is_explicit_type_value(decl->init)) return &TYPE_TYPE;
        // an initializer's literal type is not a restriction on an unannotated var (S12.2.1).
        if (decl->entry && decl->entry->is_mutable && node->type && node->type->is_literal) {
            Type* base = lambda_canonical_type_for_id(node->type->type_id);
            if (base) return base;
        }
    }
    return node->type ? node->type : &TYPE_ANY;
}

MvpFunction* mvp_function(LambdaMvpModule* m, AstFuncNode* ast) {
    if (!ast) return NULL;
    for (uint32_t i = 0; i < m->function_count; i++)
        if (m->functions[i].ast == ast || (m->satellite && m->functions[i].ast &&
                m->functions[i].ast->body == ast->body && m->functions[i].ast->param == ast->param))
            return &m->functions[i];
    for (int i = 0; m->imported_functions && i < m->imported_functions->length; i++) {
        MvpFunction* function = (MvpFunction*)m->imported_functions->data[i];
        if (function->ast == ast) return function;
    }
    return NULL;
}

AstFunctionId mvp_node_owner(LambdaMvpModule* m, AstNodeId id) {
    AstIndex* index = &m->tp->ast_index;
    if (id >= index->count) return AST_FUNCTION_ID_INVALID;
    if (m->function_count == index->function_count) return index->owner_functions[id];
    // templates have native activations but are not source Function values.
    // Derive their physical owners from the shared structural index.
    for (AstNode* node = index->nodes[id]; node; node = ast_index_parent(index, node)) {
        if (ast_index_node_is_function(node))
            return index->owner_functions[ast_index_find(index, node)];
        if (node->node_type != AST_NODE_VIEW && node->node_type != AST_NODE_EVENT_HANDLER) continue;
        for (uint32_t i = index->function_count; i < m->function_count; i++) {
            MvpFunction* function = &m->functions[i];
            if ((node->node_type == AST_NODE_VIEW && !function->handler && function->view == (AstViewNode*)node) ||
                    (node->node_type == AST_NODE_EVENT_HANDLER && function->handler == (AstEventHandler*)node)) return i;
        }
    }
    return index->owner_functions[id];
}

MvpBinding* mvp_binding(LambdaMvpModule* m, NameEntry* entry) {
    // imported binding slots belong to the declaring Script's index/slab.
    if (entry && (entry->import || entry->import_owner)) return NULL;
    AstBindingId id = ast_index_find_binding_entry(&m->tp->ast_index, entry);
    return id < m->binding_count ? &m->bindings[id] : NULL;
}

MvpBinding* mvp_local_binding(LambdaMvpModule* m, AstNode* node) {
    node = ast_unwrap_primary_to_leaf(node);
    NameEntry* entry = node && node->node_type == AST_NODE_IDENT ? ((AstIdentNode*)node)->entry : NULL;
    // definitions denote newly materialized values, not local storage that a
    // caller can borrow or read through its snapshot-elision path.
    if (entry && entry->node && (ast_index_node_is_function(entry->node) ||
            ast_definition_denotes_type(entry->node))) return NULL;
    return mvp_binding(m, entry);
}

MvpBinding* mvp_place_root(LambdaMvpModule* m, AstNode* node) {
    AstCowPath path = {};
    return ast_collect_cow_path(&path, node) ? mvp_local_binding(m, path.root) : NULL;
}

bool mvp_sole_declaration_read(LambdaMvpModule* m, AstDeclaratorNode* declaration, AstNode* read) {
    AstIndex* index = &m->tp->ast_index;
    uint32_t count = 0, reads = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, declaration->entry), &count);
    read = ast_unwrap_primary_to_leaf(read);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use == declaration || use == declaration->id) continue;
        if (use != read) return false;
        reads++;
    }
    return reads == 1;
}

bool mvp_string_append_plan(LambdaMvpModule* m, AstNode* first, MvpStringAppendPlan* plan) {
    if (!first || (first->node_type != AST_NODE_VAR_STAM && first->node_type != AST_NODE_LET_STAM)) return false;
    AstNode* declaration = ((AstVarDeclNode*)first)->declarations;
    AstNode* next = first->next;
    if (!declaration || declaration->next || declaration->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            !next) return false;
    AstDeclaratorNode* admitted = NULL;
    if (next->node_type == AST_NODE_LET_STAM) {
        admitted = (AstDeclaratorNode*)((AstVarDeclNode*)next)->declarations;
        if (!admitted || admitted->node_type != AST_NODE_VARIABLE_DECLARATOR || admitted->next ||
                !admitted->entry || admitted->entry->admission_may_fail ||
                admitted->declared_type != &TYPE_STRING) return false;
        next = next->next;
    }
    if (!next || next->node_type != AST_NODE_ASSIGN_STAM) return false;
    AstDeclaratorNode* d = (AstDeclaratorNode*)declaration;
    AstAssignStamNode* assignment = (AstAssignStamNode*)next;
    MvpBinding* temporary = mvp_binding(m, d->entry);
    MvpBinding* owner = mvp_binding(m, assignment->target_entry);
    if (!temporary || !owner || temporary == owner || temporary->contract != &TYPE_STRING ||
            owner->contract != &TYPE_STRING || temporary->owner != owner->owner) return false;
    bool transfer = !admitted && mvp_local_binding(m, assignment->value) == temporary;
    AstNode* joined_value = transfer ? d->init : admitted ? admitted->init : assignment->value;
    if (admitted && (mvp_local_binding(m, assignment->value) != mvp_binding(m, admitted->entry) ||
            !mvp_sole_declaration_read(m, admitted, assignment->value) ||
            !mvp_admission_proven(m, mvp_value_contract(m, joined_value), &TYPE_STRING))) return false;
    AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS]; int size = 0;
    if (!ast_collect_join_leaves(joined_value, leaves, &size) || size < 2 ||
            mvp_local_binding(m, leaves[0]) != owner || (!transfer &&
            (size != 2 || mvp_local_binding(m, leaves[1]) != temporary))) return false;
    if (!mvp_sole_declaration_read(m, d, transfer ? assignment->value : leaves[1])) return false;
    *plan = {d, assignment, temporary, owner, transfer};
    return true;
}

static bool require_tensor(MvpBinding* binding) {
    if (!binding || binding->tensor_required) return false;
    binding->tensor_required = true;
    return true;
}

bool mvp_array_contract(Type* contract) {
    LambdaArrayContractInfo info = {};
    Type* plain = type_field_unwrap_simple_decl(contract);
    return plain && (plain->type_id == LMD_TYPE_ARRAY || plain->type_id == LMD_TYPE_ARRAY_NUM ||
        lambda_array_contract_info(contract, &info));
}

bool mvp_binary_bitwise(SysFunc fn) {
    return fn == SYSFUNC_BAND || fn == SYSFUNC_BOR || fn == SYSFUNC_BXOR ||
        fn == SYSFUNC_SHL || fn == SYSFUNC_SHR;
}

int mvp_decimal_operation(Operator op) {
    static const struct { Operator op; LambdaNumericOpFamily family; } operations[] = {
        {OPERATOR_ADD, LAMBDA_NUM_OP_ADD}, {OPERATOR_SUB, LAMBDA_NUM_OP_SUB},
        {OPERATOR_MUL, LAMBDA_NUM_OP_MUL}, {OPERATOR_DIV, LAMBDA_NUM_OP_TRUE_DIV},
        {OPERATOR_IDIV, LAMBDA_NUM_OP_IDIV}, {OPERATOR_MOD, LAMBDA_NUM_OP_MOD},
    };
    for (const auto& operation : operations) if (operation.op == op) return operation.family;
    return -1;
}

bool mvp_needs_number_home(Type* contract) {
    bool optional = false;
    Type* scalar = lambda_type_nullable_lane_base(contract, &optional);
    // an array contract is a Type expression, but its values are containers,
    // not possible cold scalar homes (D2.3.5, D6.2.3).
    if (scalar == &TYPE_TYPE || mvp_array_contract(scalar) ||
            lambda_type_nonnull_map_contract(scalar)) return false;
    return !scalar || scalar->type_id == LMD_TYPE_TYPE ||
        lambda_type_id_may_be_wide_scalar(scalar->type_id);
}

Type* mvp_scalar_success_contract(LambdaMvpModule* m, Type* contract) {
    if (!contract) return NULL;
    Type* success = lambda_type_remove_error(m->tp->pool, contract);
    bool optional = false;
    Type* base = lambda_type_nullable_lane_base(success, &optional);
    if (!base) return NULL;
    switch (base->type_id) {
    case LMD_TYPE_INT: case LMD_TYPE_FLOAT: case LMD_TYPE_BOOL:
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_DECIMAL:
    case LMD_TYPE_NUM_SIZED: case LMD_TYPE_INT64: case LMD_TYPE_UINT64:
        return success;
    default:
        return NULL;
    }
}

static bool private_scalar_kind(Type* type) {
    return type && (type->type_id == LMD_TYPE_INT || type->type_id == LMD_TYPE_FLOAT ||
        type->type_id == LMD_TYPE_BOOL || type->type_id == LMD_TYPE_STRING || type->type_id == LMD_TYPE_DECIMAL);
}

Type* mvp_array_element(Type* contract) {
    LambdaArrayContractInfo info = {};
    if (!lambda_array_contract_info(contract, &info) || info.rank != 1) return NULL;
    Type* element = type_field_unwrap_simple_decl(info.immediate_element);
    return element == &TYPE_INT || element == &TYPE_BOOL || element == &TYPE_FLOAT ? element : NULL;
}

Type* mvp_bound_array_contract(Type* contract) {
    LambdaArrayContractInfo bound = {};
    return lambda_array_contract_info(contract, &bound) &&
        !lambda_type_contract_has_binder(bound.array_contract, true) ? bound.array_contract : NULL;
}

TypeArray* mvp_private_array_domain(LambdaMvpModule* m, Type* element) {
    TypeArray* array = (TypeArray*)alloc_type(m->tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
    array->nested = element; array->length = -1; array->type_index = -1;
    return array;
}

MvpBinding* mvp_null_condition(LambdaMvpModule* m, AstNode* condition,
        Type** present, bool* null_when_true) {
    AstNode* node = ast_unwrap_primary_to_leaf(condition);
    if (!node || node->node_type != AST_NODE_BINARY) return NULL;
    AstBinaryNode* compare = (AstBinaryNode*)node;
    if (compare->op != OPERATOR_EQ && compare->op != OPERATOR_NE) return NULL;
    AstNode* empty = ast_unwrap_primary_to_leaf(compare->right);
    MvpBinding* binding = mvp_local_binding(m, compare->left);
    if (!binding) { binding = mvp_local_binding(m, compare->right); empty = ast_unwrap_primary_to_leaf(compare->left); }
    if (!binding || !binding->entry || binding->entry->is_mutable || binding->entry->cow_param_mutated ||
            lambda_type_accepts_error(binding->contract) || !empty ||
            empty->node_type != AST_NODE_PRIMARY || empty->type->type_id != LMD_TYPE_NULL) return NULL;
    bool optional = false;
    *present = lambda_type_nullable_lane_base(binding->contract, &optional);
    *null_when_true = compare->op == OPERATOR_EQ;
    return optional ? binding : NULL;
}

ShapeEntry* mvp_member_field(LambdaMvpModule* m, AstFieldNode* node, TypeMap** shape, bool success_only) {
    Type* contract = mvp_value_contract(m, node->object);
    // emission can guard an admitted record/error receiver; ordinary static
    // queries must keep its error alternative rather than promise a field lane.
    if (success_only) contract = lambda_type_remove_error(m->tp->pool, contract);
    Type* owner = lambda_type_nonnull_map_contract(contract);
    AstNode* key = ast_unwrap_primary_to_leaf(node->field);
    if (!owner || owner == &TYPE_MAP || owner->type_id != LMD_TYPE_MAP ||
            !key || key->node_type != AST_NODE_IDENT) return NULL;
    String* name = ((AstIdentNode*)key)->name;
    if (shape) *shape = (TypeMap*)owner;
    return find_shape_read_field_by_name((TypeMap*)owner, name->chars, name->len);
}

Type* mvp_match_type(AstNode* pattern) {
    pattern = ast_unwrap_primary_to_leaf(pattern);
    if (!pattern || pattern->node_type != AST_NODE_TYPE) return NULL;
    Type* type = type_field_unwrap_simple_decl(pattern->type);
    // the null type node retains its singleton literal descriptor. Unlike
    // other literal patterns it denotes the complete base domain (S11.1).
    if (type && type->type_id == LMD_TYPE_NULL) return &TYPE_NULL;
    return type && !type->is_literal && (type == &TYPE_ERROR ||
        (type != &TYPE_TYPE && type == lambda_canonical_type_for_id(type->type_id))) ? type : NULL;
}

bool mvp_match_arm_covers(Type* subject, AstMatchArm* arm) {
    return !arm->pattern || mvp_membership_proven(subject, mvp_match_type(arm->pattern));
}

bool mvp_boundary_proven(Type* source, Type* destination) {
    if (!source) return false;
    // equality of binder descriptors cannot skip publishing/resolving slots.
    if (lambda_type_contract_has_binder(destination, true)) return false;
    if (lambda_static_boundary_relation(source, destination, false) == STATIC_BOUNDARY_PROVEN) return true;
    if (!lambda_array_contract_compatible(source, destination, true)) return false;
    Type* element = lambda_array_contract_element(source);
    Type* expected = lambda_array_contract_element(destination);
    // an identical admitted element descriptor preserves record reification
    // transitively. Structural similarity alone cannot prove its physical
    // layout; outer constraints retain their validator (D4.2.5, D4.3.3).
    return (mvp_array_element(source) && mvp_array_element(destination)) ||
        (element == &TYPE_STRING && expected == &TYPE_STRING) ||
        (element && element == expected && lambda_array_contract_is_plain(source) &&
            lambda_array_contract_is_plain(destination));
}

bool mvp_admission_proven(LambdaMvpModule* m, Type* source, Type* destination) {
    // push/splice can resize a counted value without a checked binding write;
    // a static descriptor cannot prove its current lengths (D4.3.3).
    if (lambda_type_counts_array_length(destination)) return false;
    if (!mvp_boundary_proven(source, destination)) return false;
    Type* normal = lambda_type_normal_admission_contract(m->tp->pool, destination);
    if (lambda_array_contract_element(normal) &&
            !lambda_array_contract_compatible(source, normal, true)) return false;
    Type* expected = lambda_type_nonnull_map_contract(lambda_constrained_type_base(normal));
    // a literal already carries its own inferred schema; reification belongs
    // to an actual receiving record contract, not this identical observation.
    if (!expected || expected == &TYPE_MAP || expected == &TYPE_OBJECT ||
            (source == expected && !((TypeMap*)expected)->is_trusted_contract) ||
            (source && source->type_id == LMD_TYPE_NULL)) return true;
    Type* candidate = lambda_type_nonnull_map_contract(source);
    if (!candidate || candidate == &TYPE_MAP || candidate == &TYPE_OBJECT ||
            !((TypeMap*)candidate)->is_trusted_contract) return false;
    // structural membership alone cannot authorize another schema's field
    // order or scalar slots. Reuse the runtime's physical relation (D4.2.5).
    MapContractRelation relation = lambda_map_contract_relation((TypeMap*)candidate, (TypeMap*)expected);
    return relation == MAP_CONTRACT_EXACT_TRUSTED || relation == MAP_CONTRACT_STORAGE_COMPATIBLE;
}

bool mvp_pure_key(AstNode* node, int depth, bool allow_pure_calls) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 32) return false;
    if (node->node_type == AST_NODE_IDENT || node->node_type == AST_NODE_PRIMARY) return true;
    if (node->node_type == AST_NODE_BINARY) return mvp_pure_key(((AstBinaryNode*)node)->left, depth + 1, allow_pure_calls) &&
        mvp_pure_key(((AstBinaryNode*)node)->right, depth + 1, allow_pure_calls);
    if (allow_pure_calls && (node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR)) {
        AstFieldNode* field = (AstFieldNode*)node;
        // projections can allocate or fail, but cannot mutate an earlier
        // argument; ordinary index-loop proofs retain the stricter grammar.
        return mvp_pure_key(field->object, depth + 1, true) &&
            (node->node_type == AST_NODE_MEMBER_EXPR || mvp_pure_key(field->field, depth + 1, true));
    }
    if (allow_pure_calls && node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* target = ast_unwrap_primary_to_leaf(call->function);
        if (!target || target->node_type != AST_NODE_SYS_FUNC || ((AstSysFuncNode*)target)->fn_info->is_proc) return false;
        for (int i = 0; i < mvp_call_argument_count(call); i++)
            if (!mvp_pure_key(mvp_call_argument(call, i), depth + 1, true)) return false;
        return true;
    }
    return node->node_type == AST_NODE_UNARY && mvp_pure_key(((AstUnaryNode*)node)->operand, depth + 1, allow_pure_calls);
}

bool mvp_index_key(LambdaMvpModule* m, AstNode* node, int depth) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 16) return false;
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* index = (AstFieldNode*)node;
        MvpBinding* owner = mvp_local_binding(m, index->object);
        return owner && owner->readonly_literal && mvp_array_element(owner->contract) == &TYPE_INT &&
            mvp_index_key(m, index->field, depth + 1);
    }
    if (node->node_type == AST_NODE_BINARY)
        return mvp_index_key(m, ((AstBinaryNode*)node)->left, depth + 1) &&
            mvp_index_key(m, ((AstBinaryNode*)node)->right, depth + 1);
    if (node->node_type == AST_NODE_UNARY) return mvp_index_key(m, ((AstUnaryNode*)node)->operand, depth + 1);
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        MvpFunction* function = mvp_function(m, ast_direct_call_function(call));
        if (!function || function->imported || function->ast->captures || function->method_owner ||
                function->ast->is_async || function->ast->is_generator ||
                lambda_type_func_is_proc(function->ast->type) || mvp_variadic_function(function) ||
                mvp_function_binder_count(function) || mvp_call_argument_count(call) != function->abi.param_count)
            return false;
        // a capture-free pure call with plain supplied arguments cannot write
        // the receiver; freezing it here copies the whole array per read (S1.6/S9.1.2).
        int position = 0;
        for (AstNamedNode* param = function->ast->param; param; param = (AstNamedNode*)param->next, position++) {
            TypeParam* type = lambda_type_param(param->type);
            if (!type || type->is_var_param || type->is_optional ||
                    !mvp_index_key(m, mvp_call_argument(call, position), depth + 1)) return false;
        }
        return true;
    }
    return mvp_pure_key(node);
}

TypeMap* mvp_record_contract(Type* contract, bool scalar_only) {
    TypeMap* record = contract && contract->type_id == LMD_TYPE_MAP
        ? (TypeMap*)lambda_type_nonnull_map_contract(contract) : NULL;
    if (!record || !record->is_trusted_contract || record->has_spread || !record->shape) return NULL;
    for (ShapeEntry* field = record->shape; field; field = typemap_next_field(record, field)) {
        const LaneStorageDesc* lane = shape_entry_storage(field);
        if (!shape_entry_storage_fits_data(field, record->byte_size) ||
                (scalar_only && lane->kind != LANE_STORAGE_INT && lane->kind != LANE_STORAGE_FLOAT64 &&
                 lane->kind != LANE_STORAGE_BOOL)) return NULL;
    }
    return record;
}

static bool parameter_storage_closed(LambdaMvpModule*, AstNamedNode*, bool,
    NameEntry**, unsigned, bool = true);

static bool binding_storage_closed(LambdaMvpModule* m, MvpBinding* binding,
        bool writes, NameEntry** active, unsigned depth, bool forwarding) {
    // layout stability does not require every field to be a scalar: child
    // captures affect child COW state, while whole-owner escapes stay rejected.
    TypeMap* record = binding ? mvp_record_contract(binding->contract, false) : NULL;
    // read-only array borrows do not require a primitive numeric lane. Pointer
    // arrays retain the same indexed-use and closed-forwarding escape checks.
    bool array = binding && (writes ? mvp_array_element(binding->contract) != NULL :
        mvp_array_contract(binding->contract));
    if (!binding || (!array && !record) ||
            binding->owner >= m->function_count) return false;
    // sibling-borrow snapshots are a call-site alias obligation, not an
    // escape effect. Direct calls capture aliases; dense callers guard COW
    // exclusivity before eliding that snapshot (S9.1.3v2).
    // storage/escape proofs apply to both callable colours (S9.1.2, S12.1.2v3).
    // parameter escape flags do not describe local aliases or their mutators.
    if (forwarding && !writes && mvp_binding_node(binding) &&
            mvp_binding_node(binding)->node_type == AST_NODE_PARAM &&
            !binding->entry->cow_param_retained && !binding->entry->cow_param_mutated) return true;
    for (unsigned i = 0; i < depth; i++) if (active[i] == binding->entry) return true;
    if (depth == 32) return false;
    active[depth++] = binding->entry;
    AstIndex* index = &m->tp->ast_index;
    uint32_t count = 0;
    AstBindingId binding_id = ast_index_find_binding_entry(index, binding->entry);
    const AstFunctionReference* references = ast_index_function_references(index, binding->owner, &count);
    // assignment targets contain index-expression children too; the shared
    // write references distinguish those lvalues from ordinary scalar reads.
    for (uint32_t i = 0; i < count; i++) {
        if (references[i].binding_id != binding_id || !(references[i].flags & AST_FUNCTION_REF_WRITE)) continue;
        AstNode* write = index->nodes[references[i].node_id];
        if (!writes || write->node_type != (record ? AST_NODE_MEMBER_ASSIGN_STAM : AST_NODE_INDEX_ASSIGN_STAM)) return false;
        if (record) {
            AstAssignStamNode* assignment = (AstAssignStamNode*)write;
            AstFieldNode field = {}; field.object = assignment->object; field.field = assignment->key;
            if (!mvp_member_field(m, &field)) return false;
        }
    }
    const AstNodeId* uses = ast_index_binding_uses(index,
        binding_id, &count);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        AstNode* definition = mvp_binding_node(binding);
        if (use == definition || (definition && definition->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                use == ((AstDeclaratorNode*)definition)->id)) continue;
        if (use->node_type != AST_NODE_IDENT || mvp_node_owner(m, uses[i]) != binding->owner) return false;
        AstNodeId parent = ast_index_parent_id(index, uses[i]);
        while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
            parent = ast_index_parent_id(index, parent);
        AstNode* consumer = parent < index->count ? index->nodes[parent] : NULL;
        if (consumer && consumer->node_type == AST_NODE_INDEX_EXPR &&
                mvp_local_binding(m, ((AstFieldNode*)consumer)->object) == binding) continue;
        if (record && consumer && consumer->node_type == AST_NODE_MEMBER_EXPR &&
                mvp_local_binding(m, ((AstFieldNode*)consumer)->object) == binding) continue;
        // direct-parser lvalues may expose the assignment as the owner's
        // immediate consumer rather than an intermediate index/member read.
        // The write-reference pass above has already checked this store kind.
        if (writes && consumer && consumer->node_type ==
                (record ? AST_NODE_MEMBER_ASSIGN_STAM : AST_NODE_INDEX_ASSIGN_STAM) &&
                mvp_local_binding(m, ((AstAssignStamNode*)consumer)->object) == binding) continue;
        if (!consumer || consumer->node_type != AST_NODE_CALL_EXPR) return false;
        AstCallNode* call = (AstCallNode*)consumer;
        MvpFunction* target = mvp_function(m, ast_direct_call_function(call));
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        if (!target) {
            if (callee && callee->node_type == AST_NODE_SYS_FUNC &&
                    ((AstSysFuncNode*)callee)->fn_info->fn == SYSFUNC_LEN) continue;
            return false;
        }
        // Input-owned constants cannot acquire activation-owned certificates at a forwarded boundary.
        if (!forwarding) return false;
        AstNamedNode* formal = target->ast->param;
        for (int i = 0; i < mvp_call_argument_count(call) && formal; i++, formal = (AstNamedNode*)formal->next) {
            AstNode* arg = mvp_call_argument(call, i);
            if (mvp_local_binding(m, arg) != binding) continue;
            // a cached record layout survives forwarding only when the
            // callee enforces the identical shape, not a compatible retag.
            if (record && mvp_record_contract(mvp_contract(formal), false) != record) return false;
            bool child_writes = lambda_type_param(formal->type)->is_var_param;
            if (writes && !child_writes) {
                // match direct_call's read-only borrow rule: an effectful
                // sibling or simultaneous var alias forces a snapshot capture.
                AstNamedNode* sibling = target->ast->param;
                for (int j = 0; j < mvp_call_argument_count(call) && sibling; j++, sibling = (AstNamedNode*)sibling->next) {
                    AstNode* a = mvp_call_argument(call, j);
                    if (!mvp_pure_key(a, 0, true) || (lambda_type_param(sibling->type)->is_var_param &&
                            mvp_place_root(m, a) == binding)) return false;
                }
            }
            if ((child_writes && !writes) || !parameter_storage_closed(m, formal, child_writes, active, depth)) return false;
        }
    }
    return true;
}

bool mvp_local_storage_stable(LambdaMvpModule* m, MvpBinding* binding, bool writes) {
    if (!binding || !mvp_binding_node(binding) || mvp_binding_node(binding)->node_type != AST_NODE_VARIABLE_DECLARATOR)
        return false;
    NameEntry* active[32] = {};
    return binding_storage_closed(m, binding, writes, active, 0, false);
}

static bool parameter_storage_closed(LambdaMvpModule* m, AstNamedNode* parameter,
        bool writes, NameEntry** active, unsigned depth, bool forwarding) {
    TypeParam* type = lambda_type_param(parameter->type);
    return type && type->is_var_param == writes &&
        binding_storage_closed(m, mvp_binding(m, parameter->entry), writes, active, depth, forwarding);
}

bool mvp_parameter_borrowable(LambdaMvpModule* m, AstNamedNode* parameter, bool forwarding) {
    // close recursive read-only edges without treating forwarding as an escape;
    // every cycle member still rejects writes, aliases and escaping uses (S9.1.3).
    NameEntry* active[32];
    return parameter_storage_closed(m, parameter, false, active, 0, forwarding);
}

bool mvp_parameter_storage_stable(LambdaMvpModule* m, AstNamedNode* parameter) {
    // indexed/field writes may detach but retain their storage contract;
    // no reference escapes the closed call graph to make a unique owner shared.
    NameEntry* active[32];
    return parameter_storage_closed(m, parameter, true, active, 0);
}

bool mvp_native_math(const SysFuncInfo* info, TypeId input) {
    // admission bounds the builtin set; only a proven float can bypass boxed classification.
    return input == LMD_TYPE_FLOAT && info->native_c_name &&
        info->native_returns_float && info->native_arg_count == 1;
}

Type* mvp_nullable_contract(LambdaMvpModule* m, Type* base) {
    return base == &TYPE_FLOAT ? m->nullable_float : base == &TYPE_INT ? m->nullable_int :
        base == &TYPE_BOOL ? m->nullable_bool : lambda_type_nullable_normalized(m->tp->pool, base);
}

Type* mvp_handler_pass_contract(LambdaMvpModule* m, AstHandlerNode* handler, Type* operand) {
    return handler->filter ? operand : handler->catches_falsy
        ? lambda_type_remove_error_and_null(m->tp->pool, operand)
        : lambda_type_remove_error(m->tp->pool, operand);
}

bool mvp_proc_has_final_return(AstFuncNode* fn) {
    AstNode* node = fn->body;
    while (node && (node->node_type == AST_NODE_CONTENT || node->node_type == AST_NODE_BLOCK)) {
        node = node->node_type == AST_NODE_CONTENT ? ((AstListNode*)node)->item : ((AstBlockNode*)node)->statements;
        while (node && node->next) node = node->next;
    }
    return node && node->node_type == AST_NODE_RETURN_STAM;
}

AstFieldNode* mvp_string_index(LambdaMvpModule* m, AstNode* node, int depth, bool dynamic_key) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 32 || node->node_type != AST_NODE_INDEX_EXPR) return NULL;
    AstFieldNode* index = (AstFieldNode*)node;
    if (index->field && index->field->next) return NULL;
    return mvp_value_contract(m, index->object, depth + 1)->type_id == LMD_TYPE_STRING &&
        (dynamic_key || mvp_value_contract(m, index->field, depth + 1)->type_id == LMD_TYPE_INT) ? index : NULL;
}

static bool binding_var_borrow(LambdaMvpModule* m, MvpBinding* binding,
        Type* preserved = NULL, bool open_only = false) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId i = 0; i < index->count; i++) {
        if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[i];
        AstFuncNode* function = ast_direct_call_function(call);
        AstNamedNode* param = function ? function->param : NULL;
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        // a procedure method has an implicit mutable receiver. Reuse the
        // shared write audit before retaining an initializer's element domain.
        if (call->is_proc_method && ast_body_may_write_entry((AstNode*)call,
                binding->entry, true, call->next)) return true;
        if (preserved && callee && callee->node_type == AST_NODE_SYS_FUNC) {
            SysFunc fn = ((AstSysFuncNode*)callee)->fn_info->fn;
            // erased setters can replace an inferred element domain; no
            // typed-array append certificate covers their key/value writes.
            if (fn == SYSPROC_VMAP_SET &&
                    mvp_place_root(m, mvp_call_argument(call, 0)) == binding) return true;
            if ((fn == SYSPROC_PUSH || fn == SYSPROC_SPLICE) &&
                    mvp_place_root(m, mvp_call_argument(call, 0)) == binding) {
                if (binding->entry->has_type_annotation && mvp_array_element(preserved) &&
                        lambda_array_contract_is_plain(preserved) && !lambda_type_counts_array_length(preserved) &&
                        mvp_boundary_proven(binding->admission_contract, preserved)) continue;
                return true;
            }
        }
        // a projected var argument can invalidate the root's transitive proof.
        for (int j = 0; j < mvp_call_argument_count(call) && param; j++, param = (AstNamedNode*)param->next) {
            AstNode* arg = mvp_call_argument(call, j);
            if (!lambda_type_param(param->type)->is_var_param || mvp_place_root(m, arg) != binding) continue;
            if (open_only && !mvp_open_value_contract(mvp_contract(param))) continue;
            Type* normal = preserved ? lambda_type_remove_error(m->tp->pool, mvp_contract(param)) : NULL;
            // this optional query preserves only a normal value domain across
            // invariant primitive-array borrows. Storage/escape callers omit
            // it and retain the stricter no-borrow proof (D4.3.3, S9.1.3).
            if (preserved && mvp_local_binding(m, arg) == binding &&
                    !lambda_type_accepts_error(mvp_contract(param)) && mvp_array_element(preserved) &&
                    mvp_array_element(normal) == mvp_array_element(preserved) &&
                    lambda_array_contract_is_plain(normal) && mvp_boundary_proven(normal, preserved)) continue;
            return true;
        }
    }
    return false;
}

bool mvp_consuming_array_join(LambdaMvpModule* m, MvpBinding* binding, AstNode* value) {
    AstNode* node = ast_unwrap_primary_to_leaf(value);
    if (!binding || binding->admission_contract != &TYPE_ARRAY || !binding->entry ||
            !binding->entry->is_mutable || binding->entry->lambda_shared_local ||
            !mvp_binding_node(binding) || mvp_binding_node(binding)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            !node || node->node_type != AST_NODE_BINARY || ((AstBinaryNode*)node)->op != OPERATOR_JOIN ||
            !mir_is_direct_binding(((AstBinaryNode*)node)->left, binding->entry) ||
            !mir_join_rhs_does_not_observe_owner(((AstBinaryNode*)node)->right, binding->entry) ||
            binding_var_borrow(m, binding)) return false;
    AstIndex* index = &m->tp->ast_index;
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, binding->entry), &count);
    // a captured home could be replaced by a RHS call without naming it.
    // Other retained aliases are detached by the shared helper (S9.1.2).
    for (uint32_t i = 0; i < count; i++)
        if (mvp_node_owner(m, uses[i]) != binding->owner) return false;
    return true;
}

static bool binding_reassigned(LambdaMvpModule* m, MvpBinding* binding, bool elements, bool whole = true) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (whole && node->node_type == AST_NODE_ASSIGN_STAM && ((AstAssignStamNode*)node)->target_entry == binding->entry) return true;
        // nested writes replace the COW spine and can revoke its record proof.
        if (elements && (node->node_type == AST_NODE_INDEX_ASSIGN_STAM || node->node_type == AST_NODE_MEMBER_ASSIGN_STAM) &&
                mvp_place_root(m, ((AstAssignStamNode*)node)->object) == binding) return true;
    }
    return false;
}

static bool binding_scalar_home_stable(LambdaMvpModule* m, MvpBinding* binding) {
    NameEntry* entry = binding->entry;
    if (!entry || !entry->node || entry->is_mutable || entry->cow_param_mutated ||
            entry->lambda_shared_local || binding->capture_binding ||
            binding->owner >= m->function_count || binding_reassigned(m, binding, false) ||
            binding_var_borrow(m, binding)) return false;
    if (entry->node->node_type == AST_NODE_PARAM) {
        TypeParam* parameter = lambda_type_param(entry->node->type);
        return parameter && !parameter->is_var_param;
    }
    if (entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) return false;
    AstIndex* index = &m->tp->ast_index;
    if (ast_index_find(index, entry->node) >= index->count) return false;
    for (AstNode* parent = ast_index_parent(index, entry->node); parent;
            parent = ast_index_parent(index, parent)) {
        if (parent->node_type == AST_NODE_LOOP || parent->node_type == AST_NODE_FOR_EXPR) return false;
        if (ast_index_node_is_function(parent)) break;
    }
    // declarations re-executed by loops still need independent read snapshots;
    // a one-shot owner can share its cold scalar bytes until activation exit.
    return true;
}

static bool mvp_unit_step(LambdaMvpModule*, AstAssignStamNode*, int64_t);

static Operator normalized_comparison(Operator op, bool reversed, bool taken) {
    if (reversed) op = op == OPERATOR_LT ? OPERATOR_GT : op == OPERATOR_LE ? OPERATOR_GE :
        op == OPERATOR_GT ? OPERATOR_LT : op == OPERATOR_GE ? OPERATOR_LE : op;
    if (!taken) op = op == OPERATOR_LT ? OPERATOR_GE : op == OPERATOR_LE ? OPERATOR_GT :
        op == OPERATOR_GT ? OPERATOR_LE : op == OPERATOR_GE ? OPERATOR_LT :
        op == OPERATOR_EQ ? OPERATOR_NE : op == OPERATOR_NE ? OPERATOR_EQ : op;
    return op;
}

static void index_condition_bounds(LambdaMvpModule* m, AstNode* condition, bool taken,
        MvpBinding* array, MvpBinding* key, bool* lower, bool* upper) {
    condition = ast_unwrap_primary_to_leaf(condition);
    if (!condition || condition->node_type != AST_NODE_BINARY) return;
    AstBinaryNode* test = (AstBinaryNode*)condition;
    if ((taken && test->op == OPERATOR_AND) || (!taken && test->op == OPERATOR_OR)) {
        index_condition_bounds(m, test->left, taken, array, key, lower, upper);
        index_condition_bounds(m, test->right, taken, array, key, lower, upper);
        return;
    }
    bool reversed = mvp_local_binding(m, test->right) == key;
    if (!reversed && mvp_local_binding(m, test->left) != key) return;
    Operator op = normalized_comparison(test->op, reversed, taken);
    AstNode* bound = ast_unwrap_primary_to_leaf(reversed ? test->left : test->right);
    int64_t constant;
    if (mir_decimal_int_literal(m->tp->source, bound, &constant) &&
            ((op == OPERATOR_GE && constant >= 0) || (op == OPERATOR_GT && constant >= -1))) *lower = true;
    if (op != OPERATOR_LT || !bound || bound->node_type != AST_NODE_CALL_EXPR) return;
    AstCallNode* call = (AstCallNode*)bound;
    AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
    if (callee && callee->node_type == AST_NODE_SYS_FUNC &&
            ((AstSysFuncNode*)callee)->fn_info->fn == SYSFUNC_LEN &&
            mvp_local_binding(m, mvp_call_argument(call, 0)) == array) *upper = true;
}

static bool nonnegative_loop_counter(LambdaMvpModule* m, MvpBinding* key, AstLoopControlNode* loop) {
    AstNode* definition = mvp_binding_node(key);
    if (!definition || definition->node_type != AST_NODE_VARIABLE_DECLARATOR) return false;
    MvpIntRange initial = mvp_int_range(m, ((AstDeclaratorNode*)definition)->init);
    if (!initial.finite || initial.lower < 0 || !loop->body || loop->body->node_type != AST_NODE_CONTENT) return false;
    AstNode* last = ((AstListNode*)loop->body)->item;
    while (last && last->next) last = last->next;
    if (!last || last->node_type != AST_NODE_ASSIGN_STAM ||
            ((AstAssignStamNode*)last)->target_entry != key->entry || !mvp_unit_step(m, (AstAssignStamNode*)last, 1)) return false;
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId id = 0; id < index->count; id++) {
        AstNode* node = index->nodes[id];
        if (node->node_type == AST_NODE_ASSIGN_STAM && ((AstAssignStamNode*)node)->target_entry == key->entry && node != last)
            return false;
        if (node->node_type == AST_NODE_CALL_EXPR && ast_body_may_write_entry(node, key->entry, true, node->next)) return false;
    }
    return true;
}

static bool dominated_index_present(LambdaMvpModule* m, MvpBinding* array, AstNode* source) {
    MvpBinding* key = mvp_local_binding(m, source);
    if (!key || key->rep != VALUE_REP_INT_LANE) return false;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId use = ast_index_find(index, ast_unwrap_primary_to_leaf(source));
    bool lower = false, upper = false;
    for (AstNodeId id = ast_index_parent_id(index, use); id < index->count; id = ast_index_parent_id(index, id)) {
        AstNode* node = index->nodes[id];
        if (mvp_node_owner(m, id) != key->owner) break;
        AstNode* condition = NULL; AstNode* scope = NULL; bool taken = true;
        if (node->node_type == AST_NODE_IF_EXPR) {
            AstIfNode* branch = (AstIfNode*)node;
            if (ast_index_node_descends(index, use, ast_index_find(index, branch->then))) scope = branch->then;
            else if (ast_index_node_descends(index, use, ast_index_find(index, branch->otherwise))) {
                scope = branch->otherwise; taken = false;
            }
            if (scope) condition = branch->cond;
        } else if (node->node_type == AST_NODE_BINARY) {
            AstBinaryNode* binary = (AstBinaryNode*)node;
            if ((binary->op == OPERATOR_AND || binary->op == OPERATOR_OR) &&
                    ast_index_node_descends(index, use, ast_index_find(index, binary->right))) {
                condition = binary->left; scope = binary->right; taken = binary->op == OPERATOR_AND;
            }
        } else if (node->node_type == AST_NODE_LOOP) {
            AstLoopControlNode* loop = (AstLoopControlNode*)node;
            if (!ast_body_may_write_entry(loop->body, array->entry, true) && nonnegative_loop_counter(m, key, loop)) {
                lower = true;
                if (ast_index_node_descends(index, use, ast_index_find(index, loop->body)))
                    index_condition_bounds(m, loop->cond, true, array, key, &lower, &upper);
            }
        }
        if (condition && !ast_body_may_write_entry(scope, array->entry, true) &&
                !ast_body_may_write_entry(scope, key->entry, true))
            index_condition_bounds(m, condition, taken, array, key, &lower, &upper);
    }
    return lower && upper;
}

bool mvp_array_index_present(LambdaMvpModule* m, MvpBinding* b, AstNode* key) {
    if (b && (mvp_array_element(b->contract) || b->contract == &TYPE_STRING) &&
            dominated_index_present(m, b, key)) return true;
    if (!b || !mvp_array_element(b->contract) || !b->entry || !mvp_binding_node(b) ||
            mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR) return false;
    MvpIntRange offset = mvp_int_range(m, key);
    if (!offset.finite || offset.lower < 0) return false;
    AstNode* init = ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)mvp_binding_node(b))->init);
    int64_t length = 0;
    if (init && init->node_type == AST_NODE_ARRAY) length = ast_linked_node_count(((AstArrayNode*)init)->item);
    else if (init && init->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)init;
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
                ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_FILL) return false;
        MvpIntRange count = mvp_int_range(m, mvp_call_argument(call, 0));
        if (count.finite) length = count.lower;
    }
    if (offset.upper >= length) return false;
    // indexed writes preserve length, and every path that could replace this owner was excluded.
    return !binding_reassigned(m, b, false) && !binding_var_borrow(m, b);
}

Type* mvp_binding_return_contract(LambdaMvpModule* m, NameEntry* entry) {
    if (!entry || !entry->node || entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) return NULL;
    AstDeclaratorNode* declaration = (AstDeclaratorNode*)entry->node;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId declaration_id = ast_index_find(index, declaration);
    // synthetic decomposition targets have storage IDs but no initializer
    // node from which a contextual constructor proof could be derived.
    if (declaration_id >= index->count) return NULL;
    AstFunctionId declared_owner = mvp_node_owner(m, declaration_id);
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, entry), &count);
    Type* target = NULL;
    for (uint32_t i = 0; i < count; i++) {
        AstNodeId use = uses[i];
        if (index->nodes[use] == declaration || (declaration->id &&
                ast_index_node_descends(index, use, ast_index_find(index, declaration->id)))) continue;
        if (index->nodes[use]->node_type != AST_NODE_IDENT) return NULL;
        use = ast_index_parent_id(index, use);
        while (use < index->count && index->nodes[use]->node_type == AST_NODE_PRIMARY)
            use = ast_index_parent_id(index, use);
        if (use >= index->count || index->nodes[use]->node_type != AST_NODE_RETURN_STAM) return NULL;
        AstFunctionId owner = mvp_node_owner(m, use);
        if (owner >= m->function_count || owner != declared_owner) return NULL;
        Type* result = m->functions[owner].result_contract;
        if (target && target != result) return NULL;
        target = result;
    }
    return target;
}

AstNode* mvp_function_result_expression(MvpFunction* f) {
    AstNode* body = mvp_single_statement(f->ast->body);
    if (body && body->node_type == AST_NODE_RETURN_STAM) body = ((AstReturnNode*)body)->value;
    else if (lambda_type_func_is_proc(f->ast->type)) return NULL;
    return ast_unwrap_primary_to_leaf(body);
}


// mirror enforced constructor destinations; inferred local shapes alone
// never establish an admission contract.
Type* mvp_contextual_contract(LambdaMvpModule* m, AstNode* literal) {
    AstIndex* index = &m->tp->ast_index;
    AstNodeId parent = ast_index_parent_id(index, ast_index_find(index, literal));
    while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
        parent = ast_index_parent_id(index, parent);
    if (parent >= index->count) return NULL;
    AstNode* node = index->nodes[parent];
    Type* target = NULL;
    if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)node;
        target = declaration->declared_type;
        // a producer observed only as a return can adopt that layout once
        // its field proofs succeed, even when declared with `var`.
        if (!target) target = mvp_binding_return_contract(m, declaration->entry);
    } else if (node->node_type == AST_NODE_RETURN_STAM) {
        AstFunctionId owner = mvp_node_owner(m, parent);
        if (owner < m->function_count) target = m->functions[owner].result_contract;
    } else if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        AstFuncNode* callee = ast_direct_call_function(call);
        AstNamedNode* formal = callee ? callee->param : NULL;
        for (int i = 0; i < mvp_call_argument_count(call) && formal; i++, formal = (AstNamedNode*)formal->next)
            if (ast_unwrap_primary_to_leaf(mvp_call_argument(call, i)) == literal) { target = mvp_contract(formal); break; }
    } else if (node->node_type == AST_NODE_KEY_EXPR) {
        AstNamedNode* member = (AstNamedNode*)node;
        AstNodeId parent_map = ast_index_parent_id(index, parent);
        if (member->name && parent_map < index->count && index->nodes[parent_map]->node_type == AST_NODE_MAP) {
            TypeMap* shape = mvp_contextual_map_contract(m, index->nodes[parent_map]);
            ShapeEntry* field = shape ? find_shape_read_field_by_name(shape, member->name->chars, member->name->len) : NULL;
            if (field) target = field->type;
        }
    }
    AstNodeId id = ast_index_find(index, literal);
    AstFunctionId owner = id < index->count ? mvp_node_owner(m, id) : AST_FUNCTION_ID_INVALID;
    // an expression-bodied function enforces the same result boundary as an
    // explicit return; construct its record in that layout (D4.2.5).
    if (!target && owner < m->function_count &&
            mvp_function_result_expression(&m->functions[owner]) == literal)
        target = m->functions[owner].result_contract;
    return target;
}

static bool mvp_literal_map_arm_matches(TypeMap* arm, void* data) {
    return arm->is_trusted_contract && ast_map_contract_storage_fits(arm) &&
        ast_map_literal_keys_follow_contract((AstMapNode*)data, arm);
}

static Type* mvp_addressable_shape(Type* type, bool trusted) {
    TypeMap* shape = (TypeMap*)lambda_type_nonnull_map_contract(type);
    return shape && shape != &TYPE_MAP && shape != &TYPE_OBJECT && shape->is_trusted_contract == trusted &&
        (trusted ? ast_map_contract_storage_valid(shape) : mir_shape_layout_is_addressable(shape)) &&
        !typemap_fixed_slot_prefix_count(shape) ? shape : NULL;
}

struct MvpCandidateContext { LambdaMvpModule* module; MvpCandidateKind kind; unsigned projections; };
static Type* mvp_construction_candidate(MvpCandidateContext* context, AstNode* node, int depth);
static Type* inferred_array_element(LambdaMvpModule* m, AstNode* initializer);

static void* mvp_construction_direct_candidate(void* owner, AstNode* node) {
    MvpCandidateContext* context = (MvpCandidateContext*)owner;
    LambdaMvpModule* m = context->module;
    // a receiving record contract selects the constructed layout; the
    // literal's earlier inferred descriptor is then a permanent guard miss.
    if (context->kind == MVP_CANDIDATE_RECORD) {
        if (node->node_type == AST_NODE_MAP)
            if (TypeMap* shape = mvp_contextual_map_contract(m, node)) return shape;
        if (Type* shape = mvp_addressable_shape(node->type, false)) return shape;
    } else if (context->kind == MVP_CANDIDATE_ARRAY_ELEMENT) {
        Type* element = mvp_array_element(normal_domain(m, mvp_value_contract(m, node)));
        if (!element) element = inferred_array_element(m, node);
        if (element == &TYPE_INT || element == &TYPE_FLOAT || element == &TYPE_BOOL) return element;
    } else {
        bool optional = false;
        Type* type = lambda_type_nullable_lane_base(normal_domain(m, mvp_value_contract(m, node)), &optional);
        if (private_scalar_kind(type) && (!context->projections || type->type_id == LMD_TYPE_STRING))
            return lambda_canonical_type_for_id(type->type_id);
    }
    MvpBinding* binding = mvp_local_binding(m, node);
    if (context->kind == MVP_CANDIDATE_SCALAR && context->projections)
        // this predicts a guarded entry, including nullable/missing reads;
        // it never establishes a source or element admission contract.
        return context->projections == 1 && binding ? binding->candidates[MVP_CANDIDATE_ARRAY_ELEMENT] : NULL;
    return binding ? binding->candidates[context->kind] : NULL;
}

static void* mvp_construction_binding_candidate(void* owner, AstNode* definition) {
    if (definition->node_type != AST_NODE_PARAM) return NULL;
    MvpCandidateContext* context = (MvpCandidateContext*)owner;
    MvpBinding* binding = mvp_binding(context->module, ((AstNamedNode*)definition)->entry);
    if (context->kind == MVP_CANDIDATE_SCALAR && context->projections)
        return context->projections == 1 && binding ? binding->candidates[MVP_CANDIDATE_ARRAY_ELEMENT] : NULL;
    return binding ? binding->candidates[context->kind] : NULL;
}

static void* mvp_construction_call_candidate(void* owner, AstCallNode* call) {
    MvpCandidateContext* context = (MvpCandidateContext*)owner;
    AstFuncNode* target = ast_direct_call_function(call);
    MvpFunction* callee = target ? mvp_function(context->module, target) : NULL;
    Type* candidate = callee ? callee->result_candidates[context->kind] : NULL;
    return context->kind != MVP_CANDIDATE_SCALAR || !context->projections || candidate == &TYPE_STRING
        ? candidate : NULL;
}

static void* mvp_construction_nested_candidate(void* owner, AstNode* node, int depth) {
    MvpCandidateContext* context = (MvpCandidateContext*)owner;
    // an array owner prediction says nothing about a projected child array.
    if (context->kind == MVP_CANDIDATE_ARRAY_ELEMENT) return NULL;
    MvpCandidateContext nested = *context;
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        if (context->kind == MVP_CANDIDATE_SCALAR) nested.projections++;
        return mvp_construction_candidate(&nested, ((AstFieldNode*)node)->object, depth - 1);
    }
    if (node->node_type == AST_NODE_ARRAY) {
        // a scalar element predicts an indexed value, never its array owner.
        if (context->kind == MVP_CANDIDATE_SCALAR && !nested.projections) return NULL;
        if (nested.projections) nested.projections--;
        for (AstNode* child = ((AstArrayNode*)node)->item; child; child = child->next)
            if (Type* candidate = mvp_construction_candidate(&nested, child, depth - 1)) return candidate;
    }
    return NULL;
}

static Type* mvp_construction_candidate(MvpCandidateContext* context, AstNode* node, int depth) {
    MirShapeCandidateProfile profile = {context, mvp_construction_direct_candidate,
        mvp_construction_binding_candidate, mvp_construction_call_candidate, mvp_construction_nested_candidate};
    return (Type*)mir_shape_candidate(profile, node, depth);
}

static bool mvp_note_construction_candidates(LambdaMvpModule* m, Type** targets, AstNode* source) {
    bool changed = false;
    for (int kind = 0; kind < MVP_CANDIDATE_COUNT; kind++) {
        if (targets[kind]) continue;
        MvpCandidateContext context = {m, (MvpCandidateKind)kind, 0};
        targets[kind] = mvp_construction_candidate(&context, source, 8);
        changed |= targets[kind] != NULL;
    }
    return changed;
}

static void plan_shape_candidates(LambdaMvpModule* m) {
    // first construction evidence wins, including through recursive edges.
    // These compile-time hints never alter contracts or prove a load (D7.4.1).
    AstIndex* index = &m->tp->ast_index;
    bool changed;
    do {
        changed = false;
        for (uint32_t i = 0; i < m->function_count; i++) {
            MvpFunction* function = &m->functions[i];
            if (!function->ast) continue;
            AstNode* result = mvp_function_result_expression(function);
            AstNode* body = ast_unwrap_primary_to_leaf(function->ast->body);
            if (!result && body && (body->node_type == AST_NODE_CONTENT || body->node_type == AST_NODE_BLOCK)) {
                int values = 0, declarations = 0, statements = 0; AstNode* terminal = NULL;
                bool procedural = lambda_type_func_is_proc(function->ast->type);
                AstNode* last = ast_block_last_value(body->node_type == AST_NODE_CONTENT
                    ? ((AstListNode*)body)->item : ((AstBlockNode*)body)->statements,
                    procedural, &values, &declarations, &statements, &terminal);
                if ((procedural || values == 1) && (!terminal || terminal->node_type != AST_NODE_RETURN_STAM)) result = last;
            }
            changed |= mvp_note_construction_candidates(m, function->result_candidates, result);
        }
        for (AstNodeId id = 0; id < index->count; id++) {
            AstNode* node = index->nodes[id];
            MvpBinding* binding = NULL;
            AstNode* source = NULL;
            if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
                AstDeclaratorNode* declaration = (AstDeclaratorNode*)node;
                binding = mvp_binding(m, declaration->entry); source = declaration->init;
            } else if (node->node_type == AST_NODE_ASSIGN_STAM) {
                AstAssignStamNode* assignment = (AstAssignStamNode*)node;
                binding = mvp_binding(m, assignment->target_entry); source = assignment->value;
            } else if (node->node_type == AST_NODE_RETURN_STAM) {
                AstFunctionId owner = mvp_node_owner(m, id);
                if (owner < m->function_count)
                    changed |= mvp_note_construction_candidates(m, m->functions[owner].result_candidates,
                        ((AstReturnNode*)node)->value);
            } else if (node->node_type == AST_NODE_CALL_EXPR) {
                AstCallNode* call = (AstCallNode*)node;
                AstFuncNode* callee = ast_direct_call_function(call);
                int position = 0;
                for (AstNamedNode* param = callee ? callee->param : NULL; param;
                        param = (AstNamedNode*)param->next, position++) {
                    MvpBinding* argument = mvp_binding(m, param->entry);
                    if (argument) changed |= mvp_note_construction_candidates(m, argument->candidates,
                        mvp_call_argument(call, position));
                }
            }
            if (binding) changed |= mvp_note_construction_candidates(m, binding->candidates, source);
        }
    } while (changed);
}

TypeMap* mvp_member_candidate(LambdaMvpModule* m, AstNode* object, NameRef name) {
    MvpBinding* binding = mvp_local_binding(m, object);
    AstNode* definition = binding && binding->entry ? mvp_binding_node(binding) : NULL;
    AstNode* initializer = definition && definition->node_type == AST_NODE_VARIABLE_DECLARATOR
        ? ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)definition)->init) : NULL;
    MvpFunction* producer = initializer && initializer->node_type == AST_NODE_CALL_EXPR
        ? mvp_function(m, ast_direct_call_function((AstCallNode*)initializer)) : NULL;
    TypeMap* declared = producer ? (TypeMap*)lambda_type_nonnull_map_contract(producer->declared_result) : NULL;
    if (declared && declared != &TYPE_MAP && declared != &TYPE_OBJECT) {
        // return admission can replace a literal's physical layout. Never
        // predict its pre-admission shape through an explicitly typed call.
        TypeMap* shape = (TypeMap*)mvp_addressable_shape(producer->declared_result, true);
        return shape && find_shape_read_field_by_name(shape, name->chars, name->len) ? shape : NULL;
    }
    MvpCandidateContext context = {m, MVP_CANDIDATE_RECORD, 0};
    TypeMap* source = (TypeMap*)mvp_construction_candidate(&context, object, 8);
    if (source && find_shape_read_field_by_name(source, name->chars, name->len)) return source;
    // whole-unit and satellite plans read only their own immutable type list.
    Type* shape = mir_unique_shape_for_field(m->tp->type_list, name->chars, name->len,
        true, mvp_addressable_shape);
    return (TypeMap*)(shape ? shape : mir_unique_shape_for_field(m->tp->type_list,
        name->chars, name->len, false, mvp_addressable_shape));
}

TypeMap* mvp_next_member_candidate(LambdaMvpModule* m, NameRef name,
        TypeMap* preferred, int64_t* cursor) {
    ArrayList* types = m->tp->type_list;
    if (!preferred || !types || !name) return NULL;
    while (*cursor < types->length) {
        TypeMap* shape = (TypeMap*)mvp_addressable_shape((Type*)types->data[(*cursor)++], false);
        if (!shape || shape == preferred || shape->length != preferred->length ||
                !find_shape_read_field_by_name(shape, name->chars, name->len)) continue;
        bool same_keys = true;
        for (ShapeEntry* field = preferred->shape; field; field = typemap_next_field(preferred, field)) {
            ShapeEntry* match = field->name
                ? find_shape_read_field_by_name(shape, field->name->str, field->name->length) : NULL;
            if (!field->name || field->ns || field->key_kind != NAME_KEY_STRING ||
                    !match || match->ns || match->key_kind != NAME_KEY_STRING) {
                same_keys = false; break;
            }
        }
        // alternate constructions with the same keys can have different lane
        // layouts. Each remains a static guard candidate, never a value fact.
        if (same_keys) return shape;
    }
    return NULL;
}

TypeMap* mvp_contextual_map_contract(LambdaMvpModule* m, AstNode* literal) {
    Type* target = mvp_contextual_contract(m, literal);
    TypeMap* shape = (TypeMap*)lambda_type_nonnull_map_contract(target);
    if (shape) return mvp_literal_map_arm_matches(shape, literal) ? shape : NULL;
    bool ambiguous = false;
    lambda_type_select_map_arm(target, &shape, &ambiguous, mvp_literal_map_arm_matches, literal);
    return ambiguous ? NULL : shape;
}

bool mvp_native_range_source(LambdaMvpModule* m, AstNode* source) {
    source = ast_unwrap_primary_to_leaf(source);
    if (!source || source->node_type != AST_NODE_BINARY ||
            ((AstBinaryNode*)source)->op != OPERATOR_TO) return false;
    AstBinaryNode* range = (AstBinaryNode*)source;
    // the loop counter's lane must hold for every evaluation, not only an inferred initial bound.
    return mvp_boundary_proven(normal_domain(m, mvp_value_contract(m, range->left)), &TYPE_INT) &&
        mvp_boundary_proven(normal_domain(m, mvp_value_contract(m, range->right)), &TYPE_INT);
}

struct MvpRowUse {
    MvpBinding* binding;
    int rank;
};

static bool private_row_binding(LambdaMvpModule*, MvpBinding*, int, MvpRowUse*, unsigned);

static bool private_row_use(LambdaMvpModule* m, AstNode* value, int rank,
        MvpRowUse* active, unsigned depth) {
    if (rank == 0) return true;
    AstIndex* index = &m->tp->ast_index;
    AstNode* consumer = ast_index_parent(index, value);
    while (consumer && consumer->node_type == AST_NODE_PRIMARY) {
        value = consumer;
        consumer = ast_index_parent(index, consumer);
    }
    if (!consumer) return false;
    if (consumer->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* access = (AstFieldNode*)consumer;
        return access->object == value && access->field && !access->field->next &&
            mvp_boundary_proven(mvp_value_contract(m, access->field), &TYPE_INT) &&
            private_row_use(m, consumer, rank - 1, active, depth);
    }
    if (consumer->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* alias = (AstDeclaratorNode*)consumer;
        return alias->init == value && private_row_binding(m,
            mvp_binding(m, alias->entry), rank, active, depth);
    }
    if (consumer->node_type != AST_NODE_CALL_EXPR) return false;
    AstCallNode* call = (AstCallNode*)consumer;
    AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
    if (callee && callee->node_type == AST_NODE_SYS_FUNC)
        return ((AstSysFuncNode*)callee)->fn_info->fn == SYSFUNC_LEN &&
            ast_unwrap_primary_to_leaf(mvp_call_argument(call, 0)) == ast_unwrap_primary_to_leaf(value);
    MvpFunction* target = mvp_function(m, ast_direct_call_function(call));
    if (!target || call->interp_has_spread_args) return false;
    AstNamedNode* formal = target->ast->param;
    bool found = false;
    for (int i = 0; i < mvp_call_argument_count(call) && formal; i++, formal = (AstNamedNode*)formal->next) {
        if (ast_unwrap_primary_to_leaf(mvp_call_argument(call, i)) != ast_unwrap_primary_to_leaf(value)) continue;
        if (lambda_type_param(formal->type)->is_var_param ||
                !private_row_binding(m, mvp_binding(m, formal->entry), rank, active, depth)) return false;
        found = true;
    }
    return found;
}

static bool private_row_binding(LambdaMvpModule* m, MvpBinding* binding,
        int rank, MvpRowUse* active, unsigned depth) {
    if (!binding || !binding->entry || binding->entry->is_mutable || binding->tensor_required ||
            binding->owner >= m->function_count) return false;
    for (unsigned i = 0; i < depth; i++)
        if (active[i].binding == binding) return active[i].rank == rank;
    if (depth == 32) return false;
    active[depth++] = {binding, rank};
    AstIndex* index = &m->tp->ast_index;
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, binding->entry), &count);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        AstNode* definition = mvp_binding_node(binding);
        if (use == definition || (definition && definition->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                use == ((AstDeclaratorNode*)definition)->id)) continue;
        if (use->node_type != AST_NODE_IDENT || mvp_node_owner(m, uses[i]) != binding->owner ||
                !private_row_use(m, use, rank, active, depth)) return false;
    }
    return true;
}

bool mvp_prefers_packed_tensor(LambdaMvpModule* m, AstNode* literal) {
    LambdaArrayContractInfo info = {};
    ArrayNumElemType lane;
    if (lambda_array_contract_info(mvp_contextual_contract(m, literal), &info) && info.rank > 1 &&
            lambda_array_num_elem_type_for_contract(info.leaf_element, &lane)) return true;
    // pointer rows are private only when every use consumes scalar coordinates
    // or length. Opaque producers and shape observers require canonical N-D
    // construction, including each nested child (D3.2.2, D4.3.1).
    if (!lambda_array_contract_info(literal->type, &info) || info.rank < 2) return true;
    MvpRowUse active[32];
    return !private_row_use(m, literal, info.rank, active, 0);
}

static Type* common_union_field_contract(LambdaMvpModule* m, Type* owner,
        String* name, unsigned depth = 0) {
    owner = type_field_unwrap_simple_decl(owner);
    if (!owner || depth > 32) return NULL;
    if (lambda_type_is_union(owner)) {
        TypeBinary* arms = (TypeBinary*)owner;
        Type* left = common_union_field_contract(m, arms->left, name, depth + 1);
        Type* right = common_union_field_contract(m, arms->right, name, depth + 1);
        // distinct spellings of the same nullable field must not become
        // T? | T?, which loses the nullable lane query's single base.
        if (left && right && lambda_type_repr_equal(left, right)) return left;
        return left && right ? lambda_type_union_normalized(m->tp->pool, left, right) : NULL;
    }
    if (owner->type_id != LMD_TYPE_MAP || owner == &TYPE_MAP ||
            !((TypeMap*)owner)->is_trusted_contract) return NULL;
    ShapeEntry* field = find_shape_read_field_by_name((TypeMap*)owner, name->chars, name->len);
    // an open arm that omits the field proves nothing about its extra values.
    // Common required domains do not imply a shared physical layout (D4.2.5).
    if (!field || field->is_optional) return NULL;
    Type* domain = type_field_unwrap_simple_decl(field->type);
    Type* normal = lambda_type_remove_error(m->tp->pool, domain);
    bool optional = false;
    Type* lane = lambda_type_nullable_lane_base(normal, &optional);
    // semantic union admission preserves the source carrier. A named child
    // map or array still needs its own layout/certificate crossing.
    return mvp_scalar_success_contract(m, domain) || lambda_type_is_union(lane ? lane : normal)
        ? domain : NULL;
}

// query transient scalar producer proofs; the shared AST remains the sole type graph (D8.2.5).
Type* mvp_value_contract(LambdaMvpModule* m, AstNode* source, int depth, bool dense_arrays) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n) return &TYPE_NULL;
    if (depth > 32) return &TYPE_ANY;
    if (n->node_type == AST_NODE_NAMED_ARG || n->node_type == AST_NODE_KEY_EXPR)
        return mvp_value_contract(m, ((AstNamedNode*)n)->as, depth + 1, dense_arrays);
    if (n->node_type == AST_NODE_EXPR_STMT)
        return mvp_value_contract(m, ((AstExprStmtNode*)n)->expression, depth + 1, dense_arrays);
    if (ast_is_explicit_type_value(n)) return &TYPE_TYPE;
    if (n->node_type == AST_NODE_ELEMENT && is_file_element_type((TypeElmt*)n->type))
        return lambda_type_union_normalized(m->tp->pool, n->type, &TYPE_ERROR);
    if (n->node_type == AST_NODE_IF_EXPR) {
        AstIfNode* branch = (AstIfNode*)n;
        Type* joined = mvp_join_value_contracts(m,
            mvp_value_contract(m, branch->then, depth + 1, dense_arrays),
            mvp_value_contract(m, branch->otherwise, depth + 1, dense_arrays));
        // a condition's Error is the expression's outcome, independent of
        // the normal arms' container/scalar carrier (S7.9.3).
        return lambda_type_accepts_error(mvp_value_contract(m, branch->cond, depth + 1, dense_arrays))
            ? lambda_type_union_normalized(m->tp->pool, joined, &TYPE_ERROR) : joined;
    }
    if (n->node_type == AST_NODE_MATCH_EXPR) {
        AstMatchNode* match = (AstMatchNode*)n;
        Type* subject = mvp_value_contract(m, match->scrutinee, depth + 1, dense_arrays);
        Type* joined = NULL;
        bool covered = false;
        for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
            joined = mvp_join_value_contracts(m, joined,
                mvp_value_contract(m, arm->body, depth + 1, dense_arrays));
            covered |= mvp_match_arm_covers(subject, arm);
        }
        return covered ? joined : mvp_join_value_contracts(m, joined, &TYPE_NULL);
    }
    if (n->node_type == AST_NODE_HANDLER_EXPR || n->node_type == AST_NODE_HANDLER_STAM) {
        AstHandlerNode* handler = (AstHandlerNode*)n;
        Type* pass = mvp_handler_pass_contract(m, handler,
            mvp_value_contract(m, handler->operand, depth + 1, dense_arrays));
        // the recovery can replace a typed operand with any domain; the
        // operand's old lane is never an inferred recovery restriction.
        AstNode* recovery = mvp_single_statement(handler->body);
        Type* recovered = recovery ? mvp_value_contract(m, recovery, depth + 1, dense_arrays) : &TYPE_ANY;
        return mvp_join_value_contracts(m, pass, recovered);
    }
    if (n->node_type == AST_NODE_PIPE) {
        AstPipeNode* pipe = (AstPipeNode*)n;
        if (pipe->op == OPERATOR_THAT)
            return mvp_nullable_contract(m, mvp_value_contract(m, pipe->left, depth + 1, dense_arrays));
        AstNode* right = ast_unwrap_primary_to_leaf(pipe->right);
        return pipe->op == OPERATOR_PIPE && !has_current_item_ref(pipe->right) &&
                right && right->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)right)->pipe_inject
            ? mvp_value_contract(m, pipe->right, depth + 1, dense_arrays) : &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_MAP) {
        TypeMap* target = mvp_contextual_map_contract(m, n);
        if (target) {
            ShapeEntry* field = target->shape;
            for (AstNode* item = ((AstMapNode*)n)->item; item; item = item->next, field = typemap_next_field(target, field)) {
                AstNode* input = item->node_type == AST_NODE_KEY_EXPR ? ((AstNamedNode*)item)->as : item;
                if (!mvp_boundary_proven(mvp_value_contract(m, input, depth + 1, dense_arrays), field->type))
                    return lambda_type_union_normalized(m->tp->pool, target, &TYPE_ERROR);
            }
            return target;
        }
    }
    if (n->node_type == AST_NODE_ARRAY) {
        Type* target = mvp_contextual_contract(m, n);
        Type* element = mvp_array_element(target);
        if (element && lambda_array_contract_is_plain(target) && !lambda_type_counts_array_length(target)) {
            bool proven = true;
            for (AstNode* item = ((AstArrayNode*)n)->item; proven && item; item = item->next)
                proven = mvp_boundary_proven(mvp_value_contract(m, item, depth + 1, dense_arrays), element);
            if (proven) return target;
        }
    }
    if (n->node_type == AST_NODE_MEMBER_EXPR) {
        Type* owner = mvp_value_contract(m, ((AstFieldNode*)n)->object, depth + 1);
        // shared AST typing already resolves exact Error metadata (S7.4.4).
        // Unlike a mutable map, this receiver cannot change its field domain.
        if (owner && owner->type_id == LMD_TYPE_ERROR) return n->type ? n->type : &TYPE_ANY;
        ShapeEntry* field = mvp_member_field(m, (AstFieldNode*)n, NULL, true);
        bool optional = false;
        Type* receiver = lambda_type_remove_error(m->tp->pool, owner);
        Type* lane = lambda_type_nullable_lane_base(receiver, &optional);
        if (lane) receiver = lane;
        Type* domain = field ? type_field_unwrap_simple_decl(field->type) : NULL;
        if (!domain && lambda_type_is_union(receiver)) {
            MvpBinding* binding = mvp_local_binding(m, ((AstFieldNode*)n)->object);
            AstNode* key = ast_unwrap_primary_to_leaf(((AstFieldNode*)n)->field);
            if (binding && key && key->node_type == AST_NODE_IDENT &&
                    !binding_reassigned(m, binding, true) && !binding_var_borrow(m, binding))
                domain = common_union_field_contract(m, receiver, ((AstIdentNode*)key)->name);
        }
        if (!domain) return &TYPE_ANY;
        Type* normal = optional || ((AstFieldNode*)n)->optional
            ? lambda_type_nullable_normalized(m->tp->pool, domain) : domain;
        // a record/error receiver still proves its field's successful domain;
        // keep the error arm rather than widening every scalar read to any.
        return lambda_type_has_proven_error(owner)
            ? lambda_type_union_normalized(m->tp->pool, normal, &TYPE_ERROR) : normal;
    }
    // an indexed read can be absent even under a non-null element contract (S7.1.1v3).
    if (n->node_type == AST_NODE_INDEX_EXPR) {
        // a multi-axis read has neither the first row's contract nor its range.
        AstFieldNode* index = (AstFieldNode*)n;
        if (index->field && index->field->next) return &TYPE_ANY;
        MvpBinding* b = mvp_local_binding(m, ((AstFieldNode*)n)->object);
        bool optional_key = false;
        Type* key_contract = mvp_value_contract(m, ((AstFieldNode*)n)->field, depth + 1, dense_arrays);
        bool errors = lambda_type_has_proven_error(key_contract);
        Type* key = lambda_type_nullable_lane_base(
            lambda_type_remove_error(m->tp->pool, key_contract), &optional_key);
        // an absent integer key yields absence too, not an arbitrary value.
        // Keep that domain through nested typed indexing (S7.1.1v3).
        if (!key || key->type_id != LMD_TYPE_INT) return &TYPE_ANY;
        Type* owner_contract = b ? b->contract :
            mvp_value_contract(m, ((AstFieldNode*)n)->object, depth + 1, dense_arrays);
        errors |= lambda_type_has_proven_error(owner_contract);
        owner_contract = lambda_type_remove_error(m->tp->pool, owner_contract);
        // a typed projected array has the same element contract as a local
        // owner; error-bearing keys retain that domain on their success arm.
        Type* element = mvp_array_element(owner_contract);
        Type* pointer_element = lambda_array_contract_element(owner_contract);
        // an admitted pointer-element array retains its complete field/element
        // contract; an integer miss still yields absence (S7.1.1v3).
        Type* result = &TYPE_ANY;
        if (!element && pointer_element && (pointer_element == &TYPE_STRING ||
                lambda_type_nonnull_map_contract(pointer_element) || mvp_array_contract(pointer_element)))
            result = lambda_type_nullable_normalized(m->tp->pool, pointer_element);
        else if (element) result = dense_arrays || (b && b->array_present) || mvp_array_index_present(m, b, ((AstFieldNode*)n)->field)
            ? element : mvp_nullable_contract(m, element);
        return errors && result != &TYPE_ANY
            ? lambda_type_union_normalized(m->tp->pool, result, &TYPE_ERROR) : result;
    }
    if (n->node_type == AST_NODE_IDENT) {
        NameEntry* entry = ((AstIdentNode*)n)->entry;
        if (!entry) return &TYPE_ERROR;
        if (entry && entry->is_binder) return &TYPE_TYPE;
        MvpBinding* b = mvp_binding(m, ((AstIdentNode*)n)->entry);
        if (!b) return &TYPE_ANY;
        if (lambda_type_contract_has_binder(b->contract, true)) {
            Type* bound = mvp_bound_array_contract(b->admission_contract);
            if (b->entry && mvp_binding_node(b) && mvp_binding_node(b)->node_type == AST_NODE_PARAM && bound)
                return lambda_type_union_normalized(m->tp->pool, bound, &TYPE_ERROR);
            return &TYPE_ANY;
        }
        // the same immutable null predicate drives effect planning and emitted
        // branch observations; querying a condition itself never narrows it.
        AstIndex* index = &m->tp->ast_index;
        AstNodeId use = ast_index_find(index, n);
        for (AstNodeId parent = ast_index_parent_id(index, use); parent < index->count &&
                mvp_node_owner(m, parent) == b->owner; parent = ast_index_parent_id(index, parent)) {
            if (index->nodes[parent]->node_type != AST_NODE_IF_EXPR) continue;
            AstIfNode* branch = (AstIfNode*)index->nodes[parent];
            bool yes = ast_index_node_descends(index, use, ast_index_find(index, branch->then));
            if (!yes && !ast_index_node_descends(index, use, ast_index_find(index, branch->otherwise))) continue;
            Type* present = NULL; bool null_when_true = false;
            if (mvp_null_condition(m, branch->cond, &present, &null_when_true) == b)
                return yes == null_when_true ? &TYPE_NULL : present;
        }
        return b->contract;
    }
    if (n->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)n;
        MvpFunction* f = mvp_function(m, ast_direct_call_function(call));
        if (f) {
            Type* result = mvp_integer_leaf_result(m, call, depth + 1, dense_arrays);
            if (!result) result = f->result_contract;
            if (call->propagate) {
                // a discharged call retains only its success contract, just
                // like the unary caret form (S7.7.2v2).
                Type* success = lambda_type_remove_error(m->tp->pool, result);
                return success == &TYPE_ANY_NO_ERROR ? n->type : success;
            }
            Type* outcome = lambda_type_union_normalized(m->tp->pool, result, &TYPE_ERROR);
            if (f->abi.effects.may_return_error) return outcome;
            AstNamedNode* p = f->ast->param;
            for (int i = 0; i < mvp_call_argument_count(call) && p; i++, p = (AstNamedNode*)p->next)
                if (!mvp_boundary_proven(mvp_value_contract(m, mvp_call_argument(call, i), depth + 1, dense_arrays), mvp_contract(p))) return outcome;
            return result;
        }
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return &TYPE_ANY;
        SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
        // reuse the registry relation already instantiated by shared AST
        // typing; keep its error alternative on the boxed split result.
        if (info->result_kind == SYS_RESULT_TEXT_SPLIT &&
                lambda_array_contract_element(lambda_type_remove_error(m->tp->pool, n->type)) == &TYPE_STRING)
            return n->type;
        if (info->result_kind == SYS_RESULT_TEXT_SPLIT) {
            Type* source = normal_domain(m, mvp_value_contract(m, mvp_call_argument(call, 0), depth + 1, dense_arrays));
            Type* separator = normal_domain(m, mvp_value_contract(m, mvp_call_argument(call, 1), depth + 1, dense_arrays));
            if (sysfunc_text_split_element_type_for_args(info, source->type_id, separator->type_id) == LMD_TYPE_STRING)
                return call->propagate ? m->text_split_result
                    : lambda_type_union_normalized(m->tp->pool, m->text_split_result, &TYPE_ERROR);
        }
        if (info->fn == SYSFUNC_FILL) {
            Type* destination = mvp_contextual_contract(m, n);
            destination = lambda_type_remove_error(m->tp->pool, destination);
            Type* element = mvp_array_element(destination);
            Type* normal = &TYPE_ARRAY;
            if (element && lambda_array_contract_is_plain(destination) && !lambda_type_counts_array_length(destination) &&
                    mvp_boundary_proven(mvp_value_contract(m, mvp_call_argument(call, 1), depth + 1, dense_arrays), element))
                normal = destination;
            return call->propagate ? normal :
                lambda_type_union_normalized(m->tp->pool, normal, &TYPE_ERROR);
        }
        if (info->fn == SYSPROC_CLOCK) return &TYPE_FLOAT;
        if (info->fn == SYSPROC_PRINT) return mvp_call_argument_count(call) == 1
            ? lambda_type_union_normalized(m->tp->pool, &TYPE_NULL, &TYPE_ERROR) : &TYPE_NULL;
        if (info->fn == SYSFUNC_ORD) {
            AstFieldNode* character = mvp_string_index(m, mvp_call_argument(call, 0), depth + 1);
            if (character) return mvp_array_index_present(m, mvp_local_binding(m, character->object), character->field)
                ? &TYPE_INT : m->nullable_int;
        }
        Type* arg_contract = mvp_value_contract(m, mvp_call_argument(call, 0), depth + 1, dense_arrays);
        if (info->fn == SYSFUNC_STRING) return lambda_type_accepts_error(arg_contract) ? &TYPE_ANY : &TYPE_STRING;
        TypeId arg = arg_contract->type_id;
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(arg_contract, &optional);
        if (base == &TYPE_INT && sysfunc_native_math_type_preserving(info->fn, NULL)) return arg_contract;
        Type* successful_arg = lambda_type_remove_error(m->tp->pool, arg_contract);
        if (info->result_kind == SYS_RESULT_SLICE_OF_ARGUMENT && successful_arg == &TYPE_STRING) {
            // the registry's slice relation preserves text on success; invalid
            // bounds remain checked; an absent offset yields null (S7.1.2).
            Type* sliced = &TYPE_STRING;
            for (int i = 1; i < mvp_call_argument_count(call); i++)
                if (lambda_type_accepts_null(mvp_value_contract(m, mvp_call_argument(call, i), depth + 1, dense_arrays)))
                    sliced = mvp_nullable_contract(m, &TYPE_STRING);
            return call->propagate ? sliced :
                lambda_type_union_normalized(m->tp->pool, sliced, &TYPE_ERROR);
        }
        if (info->fn == SYSFUNC_SHR && mvp_int_range(m, n).finite) return &TYPE_INT;
        if (mvp_binary_bitwise(info->fn) && successful_arg && successful_arg->type_id == LMD_TYPE_INT &&
                normal_domain(m,
                    mvp_value_contract(m, mvp_call_argument(call, 1), depth + 1, dense_arrays))->type_id == LMD_TYPE_INT)
            return lambda_type_union_normalized(m->tp->pool, &TYPE_INT, &TYPE_ERROR);
        if (info->fn == SYSFUNC_LEN && ((successful_arg && successful_arg->type_id == LMD_TYPE_STRING) || mvp_array_element(successful_arg)))
            return lambda_type_accepts_error(arg_contract)
                ? lambda_type_union_normalized(m->tp->pool, &TYPE_INT, &TYPE_ERROR) : &TYPE_INT;
        if (info->fn == SYSFUNC_LEN)
            return call->propagate ? &TYPE_INT :
                lambda_type_union_normalized(m->tp->pool, &TYPE_INT, &TYPE_ERROR);
        if (mvp_native_math(info, arg)) return &TYPE_FLOAT;
        bool u32_cast = (info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) &&
            lambda_numeric_kind_from_type(successful_arg) == LAMBDA_NUM_U32;
        if (u32_cast) {
            // match the emitted u32 converter's scalar success without losing
            // an incoming Error before the call boundary (S7.7.3, D4.3.1).
            Type* converted = info->success_type;
            return lambda_type_accepts_error(arg_contract)
                ? lambda_type_union_normalized(m->tp->pool, converted, &TYPE_ERROR) : converted;
        }
        if ((info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) &&
                (arg == LMD_TYPE_FLOAT || arg == LMD_TYPE_INT))
            return info->fn == SYSFUNC_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
        // discharge retains producer proofs; a generic AST success type must
        // not hide a closed native conversion from the guarded-body proof.
        return call->propagate ? n->type : &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_BINARY) {
        AstBinaryNode* b = (AstBinaryNode*)n;
        if (b->op == OPERATOR_IS) return mvp_match_type(b->right) ? &TYPE_BOOL :
            lambda_type_union_normalized(m->tp->pool, &TYPE_BOOL, &TYPE_ERROR);
        if ((b->op == OPERATOR_EQ || b->op == OPERATOR_NE) &&
                mvp_string_index(m, b->left, depth + 1) && mvp_string_index(m, b->right, depth + 1)) return &TYPE_BOOL;
        bool left_null = false, right_null = false;
        Type* left_contract = mvp_value_contract(m, b->left, depth + 1, dense_arrays);
        Type* right_contract = mvp_value_contract(m, b->right, depth + 1, dense_arrays);
        // short-circuit selection keeps either operand's successful domain.
        if (b->op == OPERATOR_AND || b->op == OPERATOR_OR)
            return mvp_join_value_contracts(m, left_contract, right_contract);
        bool errors = lambda_type_accepts_error(left_contract) || lambda_type_accepts_error(right_contract);
        Type* left_base = lambda_type_nullable_lane_base(lambda_type_remove_error(m->tp->pool, left_contract), &left_null);
        Type* right_base = lambda_type_nullable_lane_base(lambda_type_remove_error(m->tp->pool, right_contract), &right_null);
        TypeId left = left_base ? left_base->type_id : LMD_TYPE_ANY;
        TypeId right = right_base ? right_base->type_id : LMD_TYPE_ANY;
        if (!errors && (b->op == OPERATOR_EQ || b->op == OPERATOR_NE) &&
                ((left == LMD_TYPE_NULL && (right == LMD_TYPE_MAP || right == LMD_TYPE_NULL)) ||
                 (right == LMD_TYPE_NULL && left == LMD_TYPE_MAP))) return &TYPE_BOOL;
        if (!errors && b->op == OPERATOR_JOIN && left == LMD_TYPE_STRING && right == LMD_TYPE_STRING)
            return left_null && right_null ? mvp_nullable_contract(m, &TYPE_STRING) : &TYPE_STRING;
        if (b->op == OPERATOR_JOIN && mvp_array_contract(left_base) && mvp_array_contract(right_base))
            return lambda_type_union_normalized(m->tp->pool, &TYPE_ARRAY, &TYPE_ERROR);
        if (left == LMD_TYPE_DECIMAL && right == LMD_TYPE_DECIMAL && mvp_decimal_operation(b->op) >= 0) {
            // decimal arithmetic retains its carrier, including BigInt and
            // nonfinite results; allocation failure remains an Error outcome.
            Type* result = left_null || right_null ? mvp_nullable_contract(m, &TYPE_DECIMAL) : &TYPE_DECIMAL;
            return lambda_type_union_normalized(m->tp->pool, result, &TYPE_ERROR);
        }
        bool numbers = (left == LMD_TYPE_INT || left == LMD_TYPE_FLOAT) &&
            (right == LMD_TYPE_INT || right == LMD_TYPE_FLOAT);
        if (numbers) {
            MirNumericOpPlan plan = {};
            if (em_numeric_op_plan(b->op, &plan, true)) {
                Type* result = plan.is_comparison ? &TYPE_BOOL :
                    b->op == OPERATOR_DIV || left == LMD_TYPE_FLOAT || right == LMD_TYPE_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
                if ((left_null || right_null) && b->op != OPERATOR_EQ && b->op != OPERATOR_NE)
                    result = mvp_nullable_contract(m, result);
                return errors ? lambda_type_union_normalized(m->tp->pool, result, &TYPE_ERROR) : result;
            }
            if (b->op == OPERATOR_IDIV && left == LMD_TYPE_INT && right == LMD_TYPE_INT)
                return errors ? lambda_type_union_normalized(m->tp->pool, &TYPE_INT, &TYPE_ERROR) : &TYPE_INT;
        }
        if (!errors && (b->op == OPERATOR_AND || b->op == OPERATOR_OR || b->op == OPERATOR_EQ || b->op == OPERATOR_NE) &&
                left == LMD_TYPE_BOOL && right == LMD_TYPE_BOOL)
            return (b->op == OPERATOR_AND || b->op == OPERATOR_OR) && (left_null || right_null)
                ? m->nullable_bool : &TYPE_BOOL;
        // scalar comparisons produce bool/Error (and ordered absence), even
        // when their operands require boxed dispatch (S5.1, S7.9.3).
        if (b->op == OPERATOR_EQ || b->op == OPERATOR_NE || b->op == OPERATOR_LT ||
                b->op == OPERATOR_LE || b->op == OPERATOR_GT || b->op == OPERATOR_GE)
            return lambda_type_union_normalized(m->tp->pool,
                b->op == OPERATOR_EQ || b->op == OPERATOR_NE ? &TYPE_BOOL : m->nullable_bool, &TYPE_ERROR);
        return &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_UNARY) {
        AstUnaryNode* u = (AstUnaryNode*)n;
        if (u->op == OPERATOR_PROPAGATE) {
            Type* success = lambda_type_remove_error(m->tp->pool, mvp_value_contract(m, u->operand, depth + 1, dense_arrays));
            return success == &TYPE_ANY_NO_ERROR ? n->type : success;
        }
        Type* operand = mvp_value_contract(m, u->operand, depth + 1, dense_arrays);
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(operand, &optional);
        if (u->op == OPERATOR_NOT) return lambda_type_accepts_error(operand)
            ? lambda_type_union_normalized(m->tp->pool, &TYPE_BOOL, &TYPE_ERROR) : &TYPE_BOOL;
        if ((u->op == OPERATOR_POS || u->op == OPERATOR_NEG) && base &&
                (base->type_id == LMD_TYPE_INT || base->type_id == LMD_TYPE_FLOAT)) return operand;
        return &TYPE_ANY;
    }
    return n->type ? type_field_unwrap_simple_decl(n->type) : &TYPE_ANY;
}

LambdaMvpModule* lambda_mvp_create(Transpiler* tp, MIR_context_t ctx) {
    LambdaMvpModule* m = (LambdaMvpModule*)mem_calloc(1, sizeof(*m), MEM_CAT_TEMP);
    if (m) { m->tp = tp; m->ctx = ctx; m->names = &MIR_DEFAULT_MODULE_NAMES; }
    return m;
}

Script* mvp_import_owner(LambdaMvpModule* m, NameEntry* entry) {
    if (!entry) return NULL;
    // cached import nodes retain another execution's Script identity; both
    // symbol names and constant pools belong to the current overlay (D8.5.1).
    return entry->import ? lambda_ast_overlay_import_script(m->tp->script_owner, entry->import)
        : entry->import_owner;
}

bool mvp_interp_binding(LambdaMvpModule* m, NameEntry* entry, Script** owner, int* slot) {
    if (!m->tp->compile_against_interp_slab || !entry) return false;
    if (entry->import) return interp_satellite_import_binding(m->tp->script_owner, entry, owner, slot);
    if (!entry->storage_assigned || entry->binding_storage != BINDING_STORAGE_MODULE || entry->slot < 0)
        return false;
    *owner = m->tp->script_owner; *slot = entry->slot;
    return *owner != NULL;
}

// Demand-only interval query over the shared AST and immutable binding plans;
// it never publishes a second node-type/fact table (D8.2.5).
static bool parameter_incoming_range(LambdaMvpModule*, MvpFunction*, int, bool, MvpIntRange*, int = 0);
static MvpIntRange array_element_range(LambdaMvpModule*, MvpBinding*, int depth = 0);
static MvpIntRange counter_bound(LambdaMvpModule*, NameEntry*, AstNode*, int depth = 0);
static MvpIntRange condition_range(LambdaMvpModule* m, NameEntry* entry, AstNode* source,
        bool taken, MvpIntRange range, int depth) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || depth > 32 || n->node_type != AST_NODE_BINARY) return range;
    AstBinaryNode* test = (AstBinaryNode*)n;
    if ((taken && test->op == OPERATOR_AND) || (!taken && test->op == OPERATOR_OR)) {
        range = condition_range(m, entry, test->left, taken, range, depth + 1);
        return condition_range(m, entry, test->right, taken, range, depth + 1);
    }
    MvpBinding* left = mvp_local_binding(m, test->left), *right = mvp_local_binding(m, test->right);
    bool reversed = right && right->entry == entry;
    if ((!left || left->entry != entry) && !reversed) return range;
    MvpIntRange bound = mvp_int_range(m, reversed ? test->left : test->right, depth + 1);
    if (!bound.finite) return range;
    Operator op = normalized_comparison(test->op, reversed, taken);
    int64_t lower = range.lower, upper = range.upper;
    if (op == OPERATOR_EQ) { lower = bound.lower; upper = bound.upper; }
    else if (op == OPERATOR_NE && bound.lower == bound.upper) {
        if (lower == bound.lower) lower++;
        if (upper == bound.upper) upper--;
    }
    else if (op == OPERATOR_LT || op == OPERATOR_LE) upper = bound.upper - (op == OPERATOR_LT);
    else if (op == OPERATOR_GT || op == OPERATOR_GE) lower = bound.lower + (op == OPERATOR_GT);
    if (lower > range.lower) range.lower = lower;
    if (upper < range.upper) range.upper = upper;
    return range;
}

AstNode* mvp_single_statement(AstNode* node) {
    while (node && (node->node_type == AST_NODE_CONTENT || node->node_type == AST_NODE_BLOCK)) {
        node = node->node_type == AST_NODE_CONTENT ? ((AstListNode*)node)->item : ((AstBlockNode*)node)->statements;
        if (node && node->next) return NULL;
    }
    return node;
}

static bool sole_return(AstNode* node) {
    node = mvp_single_statement(node);
    return node && node->node_type == AST_NODE_RETURN_STAM;
}

MvpIntRange mvp_int_range(LambdaMvpModule* m, AstNode* node, int depth) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 32) return {};
    int64_t literal;
    if (mir_decimal_int_literal(m->tp->source, node, &literal) && literal >= INT53_MIN && literal <= INT53_MAX)
        return {true, literal, literal};
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* index = (AstFieldNode*)node;
        if (index->field && index->field->next) return {};
        MvpBinding* owner = mvp_local_binding(m, index->object);
        // absence cannot enter a finite range proof, even for an immutable table.
        if (owner && owner->readonly_literal && mvp_array_index_present(m, owner, index->field))
            return array_element_range(m, owner, depth + 1);
        return {};
    }
    if (node->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_binding(m, ((AstIdentNode*)node)->entry);
        MvpIntRange range = b ? b->range : MvpIntRange{};
        if (!range.finite && b && b->rep == VALUE_REP_INT_LANE && b->entry && mvp_binding_node(b)) {
            AstNode* definition = mvp_binding_node(b);
            // early array seeding precedes interval publication. Immutable
            // initializers and closed incoming calls can be queried directly.
            if (definition->node_type == AST_NODE_VARIABLE_DECLARATOR && !b->entry->is_mutable)
                range = mvp_int_range(m, ((AstDeclaratorNode*)definition)->init, depth + 1);
            else if (definition->node_type == AST_NODE_PARAM && b->owner < m->function_count &&
                    !m->functions[b->owner].boxed_entry && !binding_reassigned(m, b, false) &&
                    !binding_var_borrow(m, b)) {
                MvpFunction* function = &m->functions[b->owner];
                int position = 0;
                for (AstNamedNode* p = function->ast->param; p && p->entry != b->entry;
                        p = (AstNamedNode*)p->next) position++;
                MvpIntRange incoming = {};
                if (parameter_incoming_range(m, function, position, false, &incoming, depth + 1) &&
                        incoming.finite && parameter_incoming_range(m, function, position, true, &incoming, depth + 1))
                    range = incoming;
            }
        }
        if (!range.finite) return range;
        AstIndex* index = &m->tp->ast_index;
        AstNodeId id = ast_index_find(index, node);
        while ((id = ast_index_parent_id(index, id)) < index->count) {
            AstNode* parent = index->nodes[id];
            if ((parent->node_type == AST_NODE_CONTENT || parent->node_type == AST_NODE_BLOCK) &&
                    mvp_binding_node(b)->node_type == AST_NODE_PARAM) {
                uint32_t count = 0;
                const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &count);
                bool written = false;
                for (uint32_t i = 0; i < count; i++) written |= index->nodes[uses[i]]->node_type == AST_NODE_ASSIGN_STAM;
                if (written) continue;
                AstNode* statement = parent->node_type == AST_NODE_CONTENT ? ((AstListNode*)parent)->item :
                    ((AstBlockNode*)parent)->statements;
                for (; statement; statement = statement->next) {
                    if (ast_index_node_descends(index, ast_index_find(index, node), ast_index_find(index, statement))) break;
                    if (statement->node_type != AST_NODE_IF_EXPR) continue;
                    AstIfNode* guard = (AstIfNode*)statement;
                    if (guard->otherwise || !sole_return(guard->then)) continue;
                    // reaching a later statement excludes this returning arm;
                    // immutable parameters keep the predicate valid across intervening code.
                    MvpIntRange refined = condition_range(m, b->entry, guard->cond, false, range, depth + 1);
                    if (refined.lower <= refined.upper) range = refined;
                }
                continue;
            }
            if (parent->node_type == AST_NODE_IF_EXPR) {
                AstIfNode* branch = (AstIfNode*)parent;
                bool taken = ast_index_node_descends(index, ast_index_find(index, node), ast_index_find(index, branch->then));
                AstNodeId arm = ast_index_find(index, taken ? branch->then : branch->otherwise);
                if (!ast_index_node_descends(index, ast_index_find(index, node), arm)) continue;
                uint32_t count = 0;
                const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &count);
                bool written = false;
                for (uint32_t i = 0; i < count; i++)
                    written |= index->nodes[uses[i]]->node_type == AST_NODE_ASSIGN_STAM && ast_index_node_descends(index, uses[i], arm);
                // only unchanged finite lanes inherit a branch predicate; poison and later writes keep their checks.
                if (!written) {
                    MvpIntRange refined = condition_range(m, b->entry, branch->cond, taken, range, depth + 1);
                    if (refined.lower <= refined.upper) range = refined;
                }
                continue;
            }
            if (parent->node_type != AST_NODE_LOOP && parent->node_type != AST_NODE_FOR_EXPR) continue;
            if (parent->node_type != AST_NODE_LOOP) break;
            AstLoopControlNode* loop = (AstLoopControlNode*)parent;
            AstNodeId body = ast_index_find(index, loop->body);
            if (!ast_index_node_descends(index, ast_index_find(index, node), body)) break;
            MvpIntRange bound = counter_bound(m, b->entry, loop->cond, depth + 1);
            // An outer bound survives a nested loop only if that loop never
            // writes this binding, including writes after the current use.
            uint32_t count = 0;
            const AstNodeId* uses = ast_index_binding_uses(index,
                ast_index_find_binding_entry(index, b->entry), &count);
            for (uint32_t i = 0; i < count; i++) {
                AstNode* use = index->nodes[uses[i]];
                if (use->node_type == AST_NODE_ASSIGN_STAM && (!bound.finite || use->source_span.start_byte < node->source_span.start_byte) &&
                        ast_index_node_descends(index, uses[i], body)) return range;
            }
            if (!bound.finite) continue;
            if (bound.finite && bound.upper >= range.lower && bound.upper < range.upper) range.upper = bound.upper;
            break;
        }
        return range;
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        MvpIntRange operand = mvp_int_range(m, unary->operand, depth + 1);
        if (operand.finite && unary->op == OPERATOR_NEG) return {true, -operand.upper, -operand.lower};
        return unary->op == OPERATOR_POS ? operand : MvpIntRange{};
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* target = ast_unwrap_primary_to_leaf(call->function);
        if (!target || target->node_type != AST_NODE_SYS_FUNC) return {};
        SysFunc fn = ((AstSysFuncNode*)target)->fn_info->fn;
        // strings store their byte length in u32; their character count cannot exceed it.
        if (fn == SYSFUNC_LEN && mvp_value_contract(m, mvp_call_argument(call, 0), depth + 1)->type_id == LMD_TYPE_STRING)
            return {true, 0, UINT32_MAX};
        // an already integral expression keeps its interval through an explicit int conversion.
        if (fn == SYSFUNC_INT) return mvp_int_range(m, mvp_call_argument(call, 0), depth + 1);
        if (fn != SYSFUNC_SHR) return {};
        MvpIntRange a = mvp_int_range(m, mvp_call_argument(call, 0), depth + 1);
        MvpIntRange b = mvp_int_range(m, mvp_call_argument(call, 1), depth + 1);
        if (!a.finite || !b.finite || b.lower < 0 || b.upper >= 64) return {};
        // arithmetic right shift contracts a finite lane; poison/count errors
        // remain outside this proof and retain the shared checked helper.
        return {true, a.lower >> (a.lower < 0 ? b.lower : b.upper),
            a.upper >> (a.upper < 0 ? b.upper : b.lower)};
    }
    if (node->node_type != AST_NODE_BINARY) return {};
    AstBinaryNode* binary = (AstBinaryNode*)node;
    MvpIntRange a = mvp_int_range(m, binary->left, depth + 1);
    MvpIntRange b = mvp_int_range(m, binary->right, depth + 1);
    if (!a.finite || !b.finite) return {};
    __int128 low, high;
    if (binary->op == OPERATOR_ADD) { low = (__int128)a.lower + b.lower; high = (__int128)a.upper + b.upper; }
    else if (binary->op == OPERATOR_SUB) { low = (__int128)a.lower - b.upper; high = (__int128)a.upper - b.lower; }
    else if (binary->op == OPERATOR_MUL) {
        __int128 corners[] = {(__int128)a.lower * b.lower, (__int128)a.lower * b.upper,
            (__int128)a.upper * b.lower, (__int128)a.upper * b.upper};
        low = high = corners[0];
        for (int i = 1; i < 4; i++) { if (corners[i] < low) low = corners[i]; if (corners[i] > high) high = corners[i]; }
    } else if (binary->op == OPERATOR_IDIV && (b.lower > 0 || b.upper < 0)) {
        int64_t corners[] = {a.lower / b.lower, a.lower / b.upper, a.upper / b.lower, a.upper / b.upper};
        low = high = corners[0];
        for (int i = 1; i < 4; i++) { if (corners[i] < low) low = corners[i]; if (corners[i] > high) high = corners[i]; }
    } else if (binary->op == OPERATOR_MOD && (b.lower > 0 || b.upper < 0)) {
        int64_t magnitude = b.lower < 0 ? -b.lower : b.upper;
        low = a.lower < 0 ? -(magnitude - 1) : 0;
        high = a.upper > 0 ? magnitude - 1 : 0;
    } else return {};
    if (low < INT53_MIN || high > INT53_MAX) return {};
    return {true, (int64_t)low, (int64_t)high};
}

bool mvp_same_integer_expression(LambdaMvpModule* m, AstNode* a, AstNode* b, int depth) {
    a = ast_unwrap_primary_to_leaf(a); b = ast_unwrap_primary_to_leaf(b);
    if (!a || !b || depth > 32 || a->node_type != b->node_type) return false;
    if (a->node_type == AST_NODE_IDENT) return ((AstIdentNode*)a)->entry == ((AstIdentNode*)b)->entry;
    int64_t x, y;
    if (mir_decimal_int_literal(m->tp->source, a, &x) && mir_decimal_int_literal(m->tp->source, b, &y)) return x == y;
    if (a->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* left = (AstBinaryNode*)a; AstBinaryNode* right = (AstBinaryNode*)b;
    return left->op == right->op && mvp_same_integer_expression(m, left->left, right->left, depth + 1) &&
        mvp_same_integer_expression(m, left->right, right->right, depth + 1);
}

static unsigned integer_zero_bits(LambdaMvpModule* m, AstNode* source, int depth = 0) {
    if (depth > 32) return 0;
    MvpIntRange range = mvp_int_range(m, source);
    if (range.finite && range.lower == range.upper) {
        uint64_t bits = (uint64_t)range.lower;
        unsigned count = 0;
        while (count < 52 && !(bits & 1)) { bits >>= 1; count++; }
        return count;
    }
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || n->node_type != AST_NODE_BINARY) return 0;
    AstBinaryNode* b = (AstBinaryNode*)n;
    unsigned left = integer_zero_bits(m, b->left, depth + 1), right = integer_zero_bits(m, b->right, depth + 1);
    if (b->op == OPERATOR_ADD || b->op == OPERATOR_SUB) return left < right ? left : right;
    if (b->op != OPERATOR_MUL) return 0;
    unsigned count = left + right;
    AstNode* factors[] = {b->left, b->right};
    for (int i = 0; !count && i < 2; i++) {
        AstNode* factor = ast_unwrap_primary_to_leaf(factors[i]);
        if (!factor || factor->node_type != AST_NODE_BINARY) continue;
        AstBinaryNode* sum = (AstBinaryNode*)factor;
        int64_t difference;
        if ((sum->op == OPERATOR_ADD || sum->op == OPERATOR_SUB) &&
                mir_decimal_int_literal(m->tp->source, sum->right, &difference) && (difference & 1) &&
                mvp_same_integer_expression(m, sum->left, factors[1 - i])) count = 1;
    }
    return count > 52 ? 52 : count;
}

MvpScaledInteger mvp_scaled_integer(LambdaMvpModule* m, AstNode* source, int depth) {
    if (depth > 32) return {};
    MvpIntRange integer = mvp_int_range(m, source);
    if (integer.finite) return {integer, 0};
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || n->node_type != AST_NODE_BINARY) return {};
    AstBinaryNode* binary = (AstBinaryNode*)n;
    MvpScaledInteger a = mvp_scaled_integer(m, binary->left, depth + 1);
    if (!a.numerator.finite) return {};
    if (binary->op == OPERATOR_DIV) {
        MvpIntRange divisor = mvp_int_range(m, binary->right);
        if (!divisor.finite || divisor.lower != divisor.upper || divisor.lower <= 0 ||
                (divisor.lower & (divisor.lower - 1))) return {};
        unsigned bits = 0;
        for (int64_t d = divisor.lower; d > 1; d >>= 1) bits++;
        unsigned exact = a.fraction_bits == 0 && mvp_int_range(m, binary->left).finite
            ? integer_zero_bits(m, binary->left) : 0;
        if (exact > bits) exact = bits;
        if (exact) { a.numerator.lower >>= exact; a.numerator.upper >>= exact; }
        a.fraction_bits += bits - exact;
        return a.fraction_bits <= 52 ? a : MvpScaledInteger{};
    }
    if (binary->op != OPERATOR_ADD && binary->op != OPERATOR_SUB) return {};
    MvpScaledInteger b = mvp_scaled_integer(m, binary->right, depth + 1);
    if (!b.numerator.finite) return {};
    unsigned bits = a.fraction_bits > b.fraction_bits ? a.fraction_bits : b.fraction_bits;
    __int128 scale_a = (__int128)1 << (bits - a.fraction_bits);
    __int128 scale_b = (__int128)1 << (bits - b.fraction_bits);
    __int128 al = a.numerator.lower * scale_a, ah = a.numerator.upper * scale_a;
    __int128 bl = b.numerator.lower * scale_b, bh = b.numerator.upper * scale_b;
    __int128 low = binary->op == OPERATOR_ADD ? al + bl : al - bh;
    __int128 high = binary->op == OPERATOR_ADD ? ah + bh : ah - bl;
    // every intermediate is an exactly representable binary fraction. This
    // replaces representation conversions, never rounded float arithmetic (S4.5.2).
    if (al < INT53_MIN || ah > INT53_MAX || bl < INT53_MIN || bh > INT53_MAX ||
            low < INT53_MIN || high > INT53_MAX) return {};
    return {{true, (int64_t)low, (int64_t)high}, bits};
}

static MvpIntRange counter_expression_bound(LambdaMvpModule* m, NameEntry* counter,
        AstNode* source, MvpIntRange bound, int depth) {
    AstNode* left = ast_unwrap_primary_to_leaf(source);
    if (!left || depth > 32) return {};
    if (left->node_type == AST_NODE_IDENT && ((AstIdentNode*)left)->entry == counter) return bound;
    if (left->node_type == AST_NODE_BINARY && ((AstBinaryNode*)left)->op == OPERATOR_ADD) {
        AstBinaryNode* sum = (AstBinaryNode*)left;
        AstNode* terms[] = {sum->left, sum->right};
        // a nonnegative finite offset can only reduce the counter's upper bound;
        // saturation to +inf cannot make the finite upper comparison succeed (S4.1.2).
        for (int i = 0; i < 2; i++) {
            MvpIntRange offset = mvp_int_range(m, terms[1 - i], depth + 1);
            if (!offset.finite || offset.lower < 0 || bound.upper < offset.lower) continue;
            MvpIntRange result = counter_expression_bound(m, counter, terms[i],
                {true, INT53_MIN, bound.upper - offset.lower}, depth + 1);
            if (result.finite) return result;
        }
        return {};
    }
    if (left->node_type != AST_NODE_BINARY || ((AstBinaryNode*)left)->op != OPERATOR_MUL ||
            mvp_value_contract(m, left)->type_id != LMD_TYPE_INT || bound.upper < 0) return {};
    AstBinaryNode* product = (AstBinaryNode*)left;
    MvpBinding* a = mvp_local_binding(m, product->left);
    MvpBinding* b = mvp_local_binding(m, product->right);
    bool first = a && a->entry == counter, second = b && b->entry == counter;
    if (first && second) {
        // a finite square bound excludes poison/overflow. Integer search avoids rounded sqrt bounds.
        int64_t low = 0, high = bound.upper;
        while (low < high) {
            int64_t mid = low + (high - low + 1) / 2;
            if ((__int128)mid * mid <= bound.upper) low = mid;
            else high = mid - 1;
        }
        return {true, -low, low};
    }
    if (!first && !second) return {};
    MvpIntRange factor = mvp_int_range(m, first ? product->right : product->left, depth + 1);
    if (!factor.finite || factor.lower <= 0) return {};
    return {true, INT53_MIN, bound.upper / factor.lower};
}

static MvpIntRange counter_bound(LambdaMvpModule* m, NameEntry* counter, AstNode* condition, int depth) {
    condition = ast_unwrap_primary_to_leaf(condition);
    if (!condition || depth > 32 || condition->node_type != AST_NODE_BINARY) return {};
    AstBinaryNode* binary = (AstBinaryNode*)condition;
    if (binary->op == OPERATOR_AND) {
        MvpIntRange bound = counter_bound(m, counter, binary->left, depth + 1);
        return bound.finite ? bound : counter_bound(m, counter, binary->right, depth + 1);
    }
    if (binary->op != OPERATOR_LT && binary->op != OPERATOR_LE) return {};
    MvpIntRange bound = mvp_int_range(m, binary->right, depth + 1);
    if (bound.finite && binary->op == OPERATOR_LT) bound.upper--;
    return bound.finite ? counter_expression_bound(m, counter, binary->left, bound, depth + 1) : MvpIntRange{};
}

static bool mvp_unit_step(LambdaMvpModule* m, AstAssignStamNode* a, int64_t expected) {
    AstNode* rhs = ast_unwrap_primary_to_leaf(a->value);
    if (!rhs || rhs->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* add = (AstBinaryNode*)rhs;
    if (add->op != OPERATOR_ADD && add->op != OPERATOR_SUB) return false;
    MvpBinding* left = mvp_local_binding(m, add->left);
    int64_t step;
    return left && left->entry == a->target_entry &&
        mir_decimal_int_literal(m->tp->source, add->right, &step) &&
        (add->op == OPERATOR_ADD ? (__int128)step : -(__int128)step) == expected;
}

static bool mvp_unit_increment(LambdaMvpModule* m, AstAssignStamNode* a) {
    return mvp_unit_step(m, a, 1);
}

bool mvp_index_loop_plan(LambdaMvpModule* m, AstLoopControlNode* loop, MvpIndexLoopPlan* plan) {
    plan->loop = loop;
    AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY || ((AstBinaryNode*)condition)->op != OPERATOR_LT ||
            !loop->body || loop->body->node_type != AST_NODE_CONTENT) return false;
    AstBinaryNode* test = (AstBinaryNode*)condition;
    plan->counter = mvp_local_binding(m, test->left); plan->limit = mvp_local_binding(m, test->right);
    if (!plan->counter || !plan->limit || plan->counter == plan->limit ||
            plan->counter->rep != VALUE_REP_INT_LANE || plan->limit->rep != VALUE_REP_INT_LANE) return false;
    AstNode* last = ((AstListNode*)loop->body)->item;
    while (last && last->next) last = last->next;
    if (!last || last->node_type != AST_NODE_ASSIGN_STAM ||
            ((AstAssignStamNode*)last)->target_entry != plan->counter->entry || !mvp_unit_increment(m, (AstAssignStamNode*)last)) return false;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop);
    AstNode* follower_step = NULL;
    for (AstNodeId i = root + 1; i < ast_index_subtree_end(index, root); i++) {
        AstNode* n = index->nodes[i];
        AstNode* object = NULL; AstNode* key = NULL; AstNode* input = NULL;
        Type* destination = NULL;
        if (n->node_type == AST_NODE_INDEX_EXPR) {
            object = ((AstFieldNode*)n)->object; key = ((AstFieldNode*)n)->field;
        } else if (n->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            AstAssignStamNode* a = (AstAssignStamNode*)n;
            object = a->object; key = a->key; input = a->value;
        } else if (n->node_type == AST_NODE_ASSIGN_STAM) {
            AstAssignStamNode* a = (AstAssignStamNode*)n;
            MvpBinding* b = mvp_binding(m, a->target_entry);
            // every tracked induction lane participates in integer bounds guards.
            if (!b || b->rep != VALUE_REP_INT_LANE || b == plan->limit || !mvp_unit_increment(m, a)) return false;
            if (b == plan->counter) { if (n != last) return false; }
            else {
                if (plan->follower && plan->follower != b) return false;
                if (follower_step) return false;
                plan->follower = b; follower_step = n;
            }
        } else if (n->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            MvpBinding* b = mvp_binding(m, ((AstDeclaratorNode*)n)->entry);
            if (!b || mvp_array_element(b->contract)) return false;
            destination = b->contract; input = ((AstDeclaratorNode*)n)->init;
        } else if (n->node_type == AST_NODE_IDENT) {
            MvpBinding* b = mvp_local_binding(m, n);
            if (b && mvp_array_element(b->contract)) {
                AstNodeId p = ast_index_parent_id(index, i);
                while (p < index->count && index->nodes[p]->node_type == AST_NODE_PRIMARY) p = ast_index_parent_id(index, p);
                if (p >= index->count || (index->nodes[p]->node_type != AST_NODE_INDEX_EXPR &&
                        index->nodes[p]->node_type != AST_NODE_INDEX_ASSIGN_STAM)) return false;
            }
        } else if (n->node_type == AST_NODE_CALL_EXPR || n->node_type == AST_NODE_ARRAY || n->node_type == AST_NODE_LOOP ||
                n->node_type == AST_NODE_FOR_EXPR || n->node_type == AST_NODE_RETURN_STAM || n->node_type == AST_NODE_BREAK_STAM ||
                n->node_type == AST_NODE_CONTINUE_STAM) return false;
        if (object) {
            MvpBinding* b = mvp_local_binding(m, object), *k = mvp_local_binding(m, key);
            if (!b || !mvp_array_element(b->contract) || !k || k->rep != VALUE_REP_INT_LANE || k == plan->limit) return false;
            if (plan->array && plan->array != b) return false;
            plan->array = b;
            if (k != plan->counter) {
                if (plan->follower && plan->follower != k) return false;
                plan->follower = k;
            }
            if (input) destination = mvp_array_element(b->contract);
        }
        if (destination && lambda_static_boundary_relation(mvp_value_contract(m, input, 0, true), destination, false)
                != STATIC_BOUNDARY_PROVEN) return false;
        if ((n->node_type == AST_NODE_BINARY || n->node_type == AST_NODE_UNARY || n->node_type == AST_NODE_IF_EXPR) &&
                mvp_value_contract(m, n->node_type == AST_NODE_IF_EXPR ? ((AstIfNode*)n)->cond : n, 0, true)->type_id == LMD_TYPE_ANY) return false;
    }
    if (!plan->array || (plan->follower && !follower_step)) return false;
    if (plan->follower && ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(plan->follower)), root)) return false;
    // the follower may advance once, after its final use. Together with the
    // unconditional counter step this preserves 0 <= follower <= counter.
    if (plan->follower) {
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, plan->follower->entry), &count);
        for (uint32_t i = 0; i < count; i++) {
            AstNode* n = index->nodes[uses[i]];
            if (n->node_type == AST_NODE_IDENT && ast_index_node_descends(index, uses[i], root) &&
                    n->source_span.start_byte >= follower_step->source_span.end_byte) return false;
        }
    }
    return true;
}

bool mvp_index_function_plan(LambdaMvpModule* m, MvpFunction* function, MvpIndexLoopPlan* plan) {
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, function->ast->body);
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        if (index->nodes[i]->node_type != AST_NODE_LOOP) continue;
        if (plan->loop || !mvp_index_loop_plan(m, (AstLoopControlNode*)index->nodes[i], plan)) return false;
    }
    if (!plan->loop || !plan->follower || mvp_binding_node(plan->counter)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            mvp_binding_node(plan->follower)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            mvp_binding_node(plan->limit)->node_type != AST_NODE_PARAM) return false;
    plan->first = mvp_local_binding(m, ((AstDeclaratorNode*)mvp_binding_node(plan->counter))->init);
    if (!plan->first || mvp_binding_node(plan->first)->node_type != AST_NODE_PARAM ||
            plan->first != mvp_local_binding(m, ((AstDeclaratorNode*)mvp_binding_node(plan->follower))->init)) return false;
    MvpBinding* bounds[] = {plan->first, plan->limit, plan->counter, plan->follower};
    AstNodeId loop_id = ast_index_find(index, plan->loop);
    for (unsigned b = 0; b < sizeof(bounds) / sizeof(bounds[0]); b++) {
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, bounds[b]->entry), &count);
        for (uint32_t i = 0; i < count; i++) if (index->nodes[uses[i]]->node_type == AST_NODE_ASSIGN_STAM &&
                (b < 2 || !ast_index_node_descends(index, uses[i], loop_id))) return false;
    }
    return true;
}

static bool exclusive_branches(AstIndex* index, AstNodeId a, AstNodeId b) {
    for (AstNodeId p = ast_index_parent_id(index, a); p < index->count; p = ast_index_parent_id(index, p)) {
        if (index->nodes[p]->node_type != AST_NODE_IF_EXPR) continue;
        AstIfNode* branch = (AstIfNode*)index->nodes[p];
        AstNodeId yes = ast_index_find(index, branch->then), no = ast_index_find(index, branch->otherwise);
        if ((ast_index_node_descends(index, a, yes) && ast_index_node_descends(index, b, no)) ||
                (ast_index_node_descends(index, a, no) && ast_index_node_descends(index, b, yes))) return true;
    }
    return false;
}

static bool numeric_loop_fits(LambdaMvpModule* m, AstNodeId root) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId i = root + 1; i < index->count && ast_index_node_descends(index, i, root); i++) {
        AstNode* n = index->nodes[i];
        if (n->node_type == AST_NODE_ASSIGN_STAM && !mvp_int_range(m, ((AstAssignStamNode*)n)->value).finite) return false;
        if (n->node_type == AST_NODE_CALL_EXPR && !mvp_int_range(m, n).finite) return false;
        if (n->node_type == AST_NODE_BINARY) {
            Operator op = ((AstBinaryNode*)n)->op;
            if ((op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL ||
                    op == OPERATOR_IDIV || op == OPERATOR_MOD) && !mvp_int_range(m, n).finite) return false;
        }
    }
    return true;
}

bool mvp_numeric_region_advance(LambdaMvpModule* m, AstNode* region, MvpNumericLoopPlan* plan) {
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, region);
    if (!numeric_loop_fits(m, root)) return false;
    MvpIntRange* next = (MvpIntRange*)mem_alloc(plan->count * sizeof(MvpIntRange), MEM_CAT_TEMP);
    if (!next) return false;
    for (uint32_t g = 0; g < plan->count; g++) {
        MvpBinding* b = plan->guards[g]; next[g] = b->range;
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &count);
        for (uint32_t i = 0; i < count; i++) {
            if (!ast_index_node_descends(index, uses[i], root) || index->nodes[uses[i]]->node_type != AST_NODE_ASSIGN_STAM) continue;
            MvpIntRange rhs = mvp_int_range(m, ((AstAssignStamNode*)index->nodes[uses[i]])->value);
            if (rhs.lower < next[g].lower) next[g].lower = rhs.lower;
            if (rhs.upper > next[g].upper) next[g].upper = rhs.upper;
        }
    }
    for (uint32_t g = 0; g < plan->count; g++) plan->guards[g]->range = next[g];
    mem_free(next);
    return true;
}

static bool numeric_guard_radius(LambdaMvpModule* m, AstNode* region, MvpNumericLoopPlan* plan) {
    int64_t low = 0, high = INT53_MAX;
    while (low < high) {
        int64_t radius = low + (high - low + 1) / 2;
        for (uint32_t i = 0; i < plan->count; i++) plan->guards[i]->range = {true, -radius, radius};
        bool fits = true;
        for (unsigned i = 0; i < plan->iterations && fits; i++) fits = mvp_numeric_region_advance(m, region, plan);
        if (fits) low = radius; else high = radius - 1;
    }
    for (uint32_t i = 0; i < plan->count; i++) plan->guards[i]->range = {};
    plan->radius = low;
    if (plan->count && low) return true;
    mem_free(plan->guards); *plan = {}; return false;
}

bool mvp_integer_leaf_body(LambdaMvpModule* m, MvpFunction* function, bool allow_calls) {
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, function->ast->body);
    unsigned arithmetic = 0;
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        AstNode* n = index->nodes[i];
        switch (n->node_type) {
        case AST_NODE_BINARY:
            arithmetic++; break;
        case AST_NODE_IDENT: {
            MvpBinding* b = mvp_local_binding(m, n);
            if (!b || b->owner != function->id || b->rep != VALUE_REP_INT_LANE ||
                    mvp_binding_node(b)->node_type != AST_NODE_PARAM) return false;
            break;
        }
        case AST_NODE_CALL_EXPR: case AST_NODE_SYS_FUNC:
            if (!allow_calls) return false;
            break;
        case AST_NODE_PRIMARY: case AST_NODE_UNARY: case AST_NODE_CONTENT:
        case AST_NODE_BLOCK: case AST_NODE_RETURN_STAM: break;
        default: return false;
        }
    }
    return arithmetic >= 3;
}

Type* mvp_integer_leaf_result(LambdaMvpModule* m, AstCallNode* call, int depth, bool dense_arrays) {
    MvpFunction* f = mvp_function(m, ast_direct_call_function(call));
    if (depth > 32 || !f || f->imported || mvp_parameter_receiver(f) || f->ast->captures ||
            f->method_owner || f->owns_open_scopes || mvp_variadic_function(f) || mvp_function_binder_count(f) ||
            f->inline_cost > 96 || !mvp_open_value_contract(f->result_contract) ||
            f->abi.result.normal.abi_rep != VALUE_REP_ITEM ||
            mvp_call_argument_count(call) != f->abi.param_count) return NULL;
    AstNode* body = mvp_function_result_expression(f);
    if (!body) return NULL;
    struct LeafInput { MvpBinding* binding; Type* contract; ValueRep rep; };
    LeafInput inputs[LAMBDA_MAX_FUNCTION_ARGS] = {};
    unsigned count = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next) {
        MvpBinding* b = mvp_binding(m, p->entry);
        TypeParam* param = lambda_type_param(p->type);
        if (!b || !param || param->is_var_param || !b->stable_scalar_home || b->capture_home ||
                (b->rep != VALUE_REP_INT_LANE &&
                    (b->rep != VALUE_REP_ITEM || !mvp_open_value_contract(b->contract))) ||
                !mvp_boundary_proven(mvp_value_contract(m, mvp_call_argument(call, count), depth + 1, dense_arrays), &TYPE_INT))
            return NULL;
        inputs[count++] = {b, b->contract, b->rep};
    }
    // private activation facts match inline_call; receiving contracts and
    // the real call's stack/error protocol remain unchanged (D4.3.1, D7.4.1).
    for (unsigned i = 0; i < count; i++) {
        inputs[i].binding->contract = &TYPE_INT;
        inputs[i].binding->rep = VALUE_REP_INT_LANE;
    }
    Type* result = mvp_integer_leaf_body(m, f, false) ? mvp_value_contract(m, body, depth + 1, dense_arrays) : NULL;
    for (unsigned i = 0; i < count; i++) {
        inputs[i].binding->contract = inputs[i].contract;
        inputs[i].binding->rep = inputs[i].rep;
    }
    return result == &TYPE_INT ? result : NULL;
}

bool mvp_numeric_inline_plan(LambdaMvpModule* m, MvpFunction* function, MvpNumericLoopPlan* plan,
        bool guarded_inputs) {
    if (!guarded_inputs && (function->abi.effects.may_gc || function->abi.effects.may_return_error)) return false;
    if (!mvp_integer_leaf_body(m, function, true)) return false;
    plan->guards = (MvpBinding**)mem_alloc(function->abi.param_count * sizeof(MvpBinding*), MEM_CAT_TEMP);
    if (!plan->guards) return false;
    for (AstNamedNode* p = function->ast->param; p; p = (AstNamedNode*)p->next) {
        MvpBinding* b = mvp_binding(m, p->entry);
        if (b->rep == VALUE_REP_INT_LANE && !b->range.finite) plan->guards[plan->count++] = b;
    }
    plan->iterations = 1;
    return numeric_guard_radius(m, function->ast->body, plan);
}

bool mvp_numeric_loop_plan(LambdaMvpModule* m, AstLoopControlNode* loop, MvpNumericLoopPlan* plan) {
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop);
    // a guarded iteration may change a lane beyond its entry interval, but no
    // later use may observe that write before the next guard. Opposite if arms
    // cannot execute together; all other write/read dependences stay checked.
    for (AstNodeId i = root + 1; i < index->count && ast_index_node_descends(index, i, root); i++) {
        AstNode* n = index->nodes[i];
        switch (n->node_type) {
        case AST_NODE_PRIMARY:
            if (((AstPrimaryNode*)n)->expr) break;
            if (n->type->type_id != LMD_TYPE_INT && n->type->type_id != LMD_TYPE_BOOL) return false;
            break;
        case AST_NODE_IDENT: {
            MvpBinding* b = mvp_local_binding(m, n);
            if (!b || b->rep != VALUE_REP_INT_LANE || !b->reg) return false;
            break;
        }
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* a = (AstAssignStamNode*)n;
            MvpBinding* b = mvp_binding(m, a->target_entry);
            if (!b || b->rep != VALUE_REP_INT_LANE || a->object) return false;
            uint32_t count = 0;
            const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &count);
            for (uint32_t j = 0; j < count; j++) {
                AstNode* use = index->nodes[uses[j]];
                if (use->node_type == AST_NODE_IDENT && use->source_span.start_byte >= n->source_span.end_byte &&
                        ast_index_node_descends(index, uses[j], root) && !exclusive_branches(index, i, uses[j])) return false;
            }
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstNode* callee = ast_unwrap_primary_to_leaf(((AstCallNode*)n)->function);
            if (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
                    ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_SHR) return false;
            break;
        }
        case AST_NODE_BINARY:
            if (((AstBinaryNode*)n)->op == OPERATOR_DIV || ((AstBinaryNode*)n)->op == OPERATOR_JOIN) return false;
            break;
        case AST_NODE_CONTENT: case AST_NODE_BLOCK: case AST_NODE_EXPR_STMT:
        case AST_NODE_IF_EXPR: case AST_NODE_SYS_FUNC:
        case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: break;
        default: return false;
        }
    }
    plan->guards = (MvpBinding**)mem_alloc(m->binding_count * sizeof(MvpBinding*), MEM_CAT_TEMP);
    if (!plan->guards) return false;
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->rep != VALUE_REP_INT_LANE || b->range.finite) continue;
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, i, &count);
        for (uint32_t u = 0; u < count; u++) if (ast_index_node_descends(index, uses[u], root)) {
            plan->guards[plan->count++] = b; break;
        }
    }
    plan->iterations = 4;
    return numeric_guard_radius(m, loop, plan);
}

static bool guards_difference(LambdaMvpModule* m, AstNode* source, AstBinaryNode* difference) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || n->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* test = (AstBinaryNode*)n;
    if (test->op == OPERATOR_AND)
        return guards_difference(m, test->left, difference) || guards_difference(m, test->right, difference);
    return (test->op == OPERATOR_GE || test->op == OPERATOR_GT) &&
        mvp_same_integer_expression(m, test->left, difference->left) &&
        mvp_same_integer_expression(m, test->right, difference->right);
}

static bool contracting_subtraction(LambdaMvpModule* m, MvpBinding* binding, AstAssignStamNode* update) {
    AstNode* value = ast_unwrap_primary_to_leaf(update->value);
    if (!value || value->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* sub = (AstBinaryNode*)value;
    MvpIntRange amount = mvp_int_range(m, sub->right);
    if (sub->op != OPERATOR_SUB || mvp_local_binding(m, sub->left) != binding ||
            !amount.finite || amount.lower < 0) return false;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId body = ast_index_parent_id(index, ast_index_find(index, update));
    if (body >= index->count || index->nodes[body]->node_type != AST_NODE_CONTENT ||
            ((AstListNode*)index->nodes[body])->item != update) return false;
    AstNodeId parent = ast_index_parent_id(index, body);
    if (parent >= index->count || index->nodes[parent]->node_type != AST_NODE_LOOP) return false;
    AstLoopControlNode* loop = (AstLoopControlNode*)index->nodes[parent];
    // the first write sees the unchanged, nonnegative ordered difference.
    return loop->form == LOOP_FORM_WHILE && mvp_pure_key(loop->cond) && guards_difference(m, loop->cond, sub);
}

static bool binding_borrowed_in_region(LambdaMvpModule* m, MvpBinding* binding, AstNodeId root) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId id = root; id < ast_index_subtree_end(index, root); id++) {
        if (index->nodes[id]->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[id];
        AstFuncNode* fn = ast_direct_call_function(call);
        AstNamedNode* p = fn ? fn->param : NULL;
        for (int i = 0; i < mvp_call_argument_count(call) && p; i++, p = (AstNamedNode*)p->next)
            if (lambda_type_param(p->type)->is_var_param && mvp_local_binding(m, mvp_call_argument(call, i)) == binding) return true;
    }
    return false;
}

static int64_t finite_loop_trips(LambdaMvpModule* m, AstNode* node) {
    AstIndex* index = &m->tp->ast_index;
    MvpBinding* counter = NULL;
    AstNode* update = NULL;
    MvpIntRange first = {}, last = {};
    int64_t step = 1;
    if (node->node_type == AST_NODE_FOR_EXPR) {
        AstLoopNode* clause = (AstLoopNode*)((AstForNode*)node)->loop;
        AstNode* source = ast_unwrap_primary_to_leaf(clause->as);
        if (!source || source->node_type != AST_NODE_BINARY || ((AstBinaryNode*)source)->op != OPERATOR_TO) return -1;
        AstBinaryNode* range = (AstBinaryNode*)source;
        first = mvp_int_range(m, range->left); last = mvp_int_range(m, range->right);
        counter = mvp_binding(m, clause->entry);
    } else {
        AstLoopControlNode* loop = (AstLoopControlNode*)node;
        AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
        if (loop->form != LOOP_FORM_WHILE || !condition || condition->node_type != AST_NODE_BINARY ||
                !loop->body || loop->body->node_type != AST_NODE_CONTENT) return -1;
        AstBinaryNode* test = (AstBinaryNode*)condition;
        if (test->op != OPERATOR_LT && test->op != OPERATOR_LE) return -1;
        counter = mvp_local_binding(m, test->left);
        first = counter ? counter->range : MvpIntRange{};
        last = mvp_int_range(m, test->right);
        if (last.finite && test->op == OPERATOR_LT) last.upper--;
        update = ((AstListNode*)loop->body)->item;
        while (update && update->next) update = update->next;
        if (!counter || !update || update->node_type != AST_NODE_ASSIGN_STAM ||
                ((AstAssignStamNode*)update)->target_entry != counter->entry) return -1;
        AstNode* value = ast_unwrap_primary_to_leaf(((AstAssignStamNode*)update)->value);
        if (!value || value->node_type != AST_NODE_BINARY) return -1;
        AstBinaryNode* add = (AstBinaryNode*)value;
        if (add->op != OPERATOR_ADD || mvp_local_binding(m, add->left) != counter ||
                !mir_decimal_int_literal(m->tp->source, add->right, &step) || step <= 0) return -1;
    }
    if (!counter || !first.finite || !last.finite) return -1;
    AstNodeId root = ast_index_find(index, node);
    for (AstNodeId id = root; id < ast_index_subtree_end(index, root); id++) {
        AstNode* n = index->nodes[id];
        if (n->node_type == AST_NODE_CONTINUE_STAM || (n->node_type == AST_NODE_ASSIGN_STAM &&
                n != update && ((AstAssignStamNode*)n)->target_entry == counter->entry)) return -1;
    }
    if (binding_borrowed_in_region(m, counter, root)) return -1;
    __int128 distance = (__int128)last.upper - first.lower;
    __int128 trips = distance < 0 ? 0 : distance / step + 1;
    return trips <= INT53_MAX ? (int64_t)trips : -1;
}

static MvpIntRange counted_increment(LambdaMvpModule* m, MvpBinding* binding,
        AstAssignStamNode* update, MvpIntRange initial, MvpIntRange step) {
    AstIndex* index = &m->tp->ast_index;
    AstNodeId declaration = ast_index_find(index, mvp_binding_node(binding)), outer = AST_NODE_ID_INVALID;
    __int128 trips = 1;
    for (AstNodeId id = ast_index_parent_id(index, ast_index_find(index, update));
            id < index->count; id = ast_index_parent_id(index, id)) {
        AstNode* n = index->nodes[id];
        if (n->node_type != AST_NODE_LOOP && n->node_type != AST_NODE_FOR_EXPR) continue;
        if (ast_index_node_descends(index, declaration, id)) break;
        int64_t count = finite_loop_trips(m, n);
        if (count < 0 || trips * count > INT53_MAX) return {};
        trips *= count; outer = id;
    }
    if (outer == AST_NODE_ID_INVALID || binding_borrowed_in_region(m, binding, outer)) return {};
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, binding->entry), &count);
    for (uint32_t u = 0; u < count; u++) {
        AstNode* n = index->nodes[uses[u]];
        if (n != update && n->node_type == AST_NODE_ASSIGN_STAM && ast_index_node_descends(index, uses[u], outer) &&
                !(initial.lower >= 0 && contracting_subtraction(m, binding, (AstAssignStamNode*)n))) return {};
    }
    // multiply enclosing trip bounds; all other writes must contract toward zero.
    __int128 upper = initial.upper + trips * step.upper;
    return upper <= INT53_MAX ? MvpIntRange{true, initial.lower, (int64_t)upper} : MvpIntRange{};
}

static MvpIntRange bounded_counter(LambdaMvpModule* m, MvpBinding* binding,
        AstAssignStamNode* update, MvpIntRange initial) {
    AstNode* node = ast_unwrap_primary_to_leaf(update->value);
    if (node && node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* cast = (AstCallNode*)node;
        AstNode* target = ast_unwrap_primary_to_leaf(cast->function);
        if (target && target->node_type == AST_NODE_SYS_FUNC && ((AstSysFuncNode*)target)->fn_info->fn == SYSFUNC_INT)
            node = ast_unwrap_primary_to_leaf(mvp_call_argument(cast, 0));
    }
    if (!node || node->node_type != AST_NODE_BINARY) return {};
    AstBinaryNode* add = (AstBinaryNode*)node;
    AstNode* left = ast_unwrap_primary_to_leaf(add->left);
    MvpIntRange step = mvp_int_range(m, add->right);
    if (initial.lower >= 0 && contracting_subtraction(m, binding, update)) return {true, 0, initial.upper};
    if (add->op == OPERATOR_IDIV && left && left->node_type == AST_NODE_IDENT &&
            ((AstIdentNode*)left)->entry == binding->entry && step.finite && step.lower >= 1) {
        // division by a positive integer contracts toward zero, including repeated updates.
        return {true, initial.lower < 0 ? initial.lower : 0, initial.upper > 0 ? initial.upper : 0};
    }
    bool multiply = add->op == OPERATOR_MUL;
    if (!step.finite || step.lower < 0) return {};
    while (add->op == OPERATOR_ADD && left && left->node_type == AST_NODE_BINARY &&
            ((AstBinaryNode*)left)->op == OPERATOR_ADD && step.finite) {
        AstBinaryNode* prefix = (AstBinaryNode*)left;
        MvpIntRange term = mvp_int_range(m, prefix->right);
        if (!term.finite || term.lower < 0 || (__int128)step.upper + term.upper > INT53_MAX) return {};
        step.lower += term.lower; step.upper += term.upper;
        left = ast_unwrap_primary_to_leaf(prefix->left);
    }
    if ((add->op != OPERATOR_ADD && !multiply) || !left || left->node_type != AST_NODE_IDENT ||
            ((AstIdentNode*)left)->entry != binding->entry ||
            !step.finite || step.lower < 0 || (multiply && (step.lower < 1 || initial.lower < 0))) return {};
    AstIndex* index = &m->tp->ast_index;
    if (multiply) {
        // another reset could introduce a negative value and invalidate the nonnegative recurrence.
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, binding->entry), &count);
        for (uint32_t i = 0; i < count; i++)
            if (index->nodes[uses[i]]->node_type == AST_NODE_ASSIGN_STAM && index->nodes[uses[i]] != update) return {};
    }
    if (!multiply) {
        MvpIntRange counted = counted_increment(m, binding, update, initial, step);
        if (counted.finite) return counted;
    }
    AstNodeId id = ast_index_find(index, update);
    while ((id = ast_index_parent_id(index, id)) < index->count) {
        AstNode* parent = index->nodes[id];
        if (parent->node_type == AST_NODE_FOR_EXPR) {
            return {};
        }
        if (parent->node_type != AST_NODE_LOOP) continue;
        // the nearest loop must guard this counter; a nested loop cannot execute the update repeatedly under an outer guard.
        AstLoopControlNode* loop = (AstLoopControlNode*)parent;
        MvpIntRange bound = counter_bound(m, binding->entry, loop->cond);
        if (loop->form != LOOP_FORM_WHILE || !bound.finite ||
                !ast_index_node_descends(index, ast_index_find(index, update), ast_index_find(index, loop->body))) return {};
        __int128 upper = multiply ? (__int128)bound.upper * step.upper : (__int128)bound.upper + step.upper;
        if (upper > INT53_MAX) return {};
        if (upper > initial.upper) initial.upper = (int64_t)upper;
        return initial;
    }
    return {};
}

static bool parameter_incoming_range(LambdaMvpModule* m, MvpFunction* function,
        int position, bool self_edges, MvpIntRange* range, int depth) {
    if (depth > 32) return false;
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId n = 0; n < index->count; n++) {
        if (index->nodes[n]->node_type != AST_NODE_CALL_EXPR ||
                (mvp_node_owner(m, n) == function->id) != self_edges) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[n];
        if (ast_direct_call_function(call) != function->ast) continue;
        AstNode* argument = mvp_call_argument(call, position);
        MvpIntRange incoming = mvp_int_range(m, argument, depth + 1);
        if (!incoming.finite) return false;
        if (!range->finite) *range = incoming;
        else {
            if (incoming.lower < range->lower) range->lower = incoming.lower;
            if (incoming.upper > range->upper) range->upper = incoming.upper;
        }
    }
    return true;
}

static void plan_integer_ranges(LambdaMvpModule* m) {
    AstIndex* index = &m->tp->ast_index;
    bool changed;
    do {
        changed = false;
        for (AstBindingId id = 0; id < m->binding_count; id++) {
            MvpBinding* b = &m->bindings[id];
            if (b->range.finite || b->rep != VALUE_REP_INT_LANE || !b->entry || !mvp_binding_node(b)) continue;
            // a var call can replace this lane without an assignment node in
            // the caller. Its initializer is not a range for later reads.
            if (binding_var_borrow(m, b)) continue;
            if (mvp_binding_node(b)->node_type == AST_NODE_PARAM) {
                MvpFunction* function = b->owner < m->function_count ? &m->functions[b->owner] : NULL;
                if (!function || function->boxed_entry) continue;
                int position = 0;
                AstNamedNode* p = function ? function->ast->param : NULL;
                for (; p && p->entry != b->entry; p = (AstNamedNode*)p->next) position++;
                uint32_t count = 0;
                const AstNodeId* uses = ast_index_binding_uses(index, id, &count);
                bool written = false;
                for (uint32_t u = 0; u < count; u++) written |= index->nodes[uses[u]]->node_type == AST_NODE_ASSIGN_STAM;
                MvpIntRange range = {};
                if (!p || written || !parameter_incoming_range(m, function, position, false, &range) || !range.finite) continue;
                // external calls seed the induction; publish only an interval
                // closed under every self edge, including its dominating return guards.
                for (unsigned attempt = 0; attempt < 64; attempt++) {
                    b->range = range;
                    MvpIntRange next = range;
                    bool known = parameter_incoming_range(m, function, position, true, &next);
                    b->range = {};
                    if (!known) break;
                    if (next.lower == range.lower && next.upper == range.upper) {
                        b->range = range; changed = true; break;
                    }
                    range = next;
                }
                continue;
            }
            if (mvp_binding_node(b)->node_type == AST_NODE_FOR_CLAUSE) {
                AstNode* source = ast_unwrap_primary_to_leaf(((AstLoopNode*)mvp_binding_node(b))->as);
                if (!source || source->node_type != AST_NODE_BINARY || ((AstBinaryNode*)source)->op != OPERATOR_TO) continue;
                MvpIntRange first = mvp_int_range(m, ((AstBinaryNode*)source)->left);
                MvpIntRange last = mvp_int_range(m, ((AstBinaryNode*)source)->right);
                if (first.finite && last.finite && first.lower <= last.upper) {
                    b->range = {true, first.lower, last.upper}; changed = true;
                }
                continue;
            }
            if (mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
            AstDeclaratorNode* declaration = (AstDeclaratorNode*)mvp_binding_node(b);
            MvpIntRange range = mvp_int_range(m, declaration->init);
            AstNode* initial = ast_unwrap_primary_to_leaf(declaration->init);
            if (!range.finite && declaration->declared_type == &TYPE_INT && initial &&
                    initial->node_type == AST_NODE_INDEX_EXPR) {
                // the declaration rejects absence before its uses. Its success
                // can inherit a closed owner's finite element domain (S11.4.1).
                range = array_element_range(m, mvp_local_binding(m, ((AstFieldNode*)initial)->object));
            }
            if (!range.finite) continue;
            uint32_t count = 0;
            const AstNodeId* uses = ast_index_binding_uses(index, id, &count);
            // every write preserves reset values or a guarded nonnegative step;
            // multiple loops can safely reuse one counter.
            for (uint32_t i = 0; i < count && range.finite; i++) {
                AstNode* use = index->nodes[uses[i]];
                if (use->node_type != AST_NODE_ASSIGN_STAM) continue;
                AstAssignStamNode* update = (AstAssignStamNode*)use;
                MvpIntRange rhs = bounded_counter(m, b, update, range);
                if (!rhs.finite) rhs = mvp_int_range(m, update->value);
                if (!rhs.finite) { range.finite = false; break; }
                if (rhs.lower < range.lower) range.lower = rhs.lower;
                if (rhs.upper > range.upper) range.upper = rhs.upper;
            }
            if (range.finite) { b->range = range; changed = true; }
        }
    } while (changed);
}

static void plan_float_parameters(LambdaMvpModule* m) {
    AstIndex* index = &m->tp->ast_index;
    for (AstBindingId id = 0; id < m->binding_count; id++) {
        MvpBinding* b = &m->bindings[id];
        if (b->rep != VALUE_REP_F64 || !b->entry || !mvp_binding_node(b) || mvp_binding_node(b)->node_type != AST_NODE_PARAM) continue;
        MvpFunction* f = &m->functions[b->owner];
        if (f->boxed_entry || binding_var_borrow(m, b)) continue;
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, id, &count);
        bool known = true, seen = false;
        for (uint32_t u = 0; u < count; u++) if (index->nodes[uses[u]]->node_type == AST_NODE_ASSIGN_STAM) known = false;
        int position = 0;
        for (AstNamedNode* p = f->ast->param; p && p->entry != b->entry; p = (AstNamedNode*)p->next) position++;
        for (AstNodeId i = 0; i < index->count && known; i++) {
            if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
            AstCallNode* call = (AstCallNode*)index->nodes[i];
            if (ast_direct_call_function(call) != f->ast) continue;
            AstNode* a = mvp_call_argument(call, position);
            a = ast_unwrap_primary_to_leaf(a);
            if (!a || a->node_type != AST_NODE_PRIMARY || !a->type ||
                    a->type->type_id != LMD_TYPE_FLOAT || !a->type->is_literal) { known = false; break; }
            double literal = ((TypeFloat*)a->type)->double_val;
            // bit equality preserves signed zero and NaN payloads; every private
            // call must supply the same literal, and the parameter is never written.
            if (seen && memcmp(&literal, &b->float_value, sizeof(literal))) { known = false; break; }
            seen = true; b->float_value = literal;
        }
        b->constant_float = known && seen;
    }
}

static bool scalar_snapshot_key(AstNode* key, void* data) {
    LambdaMvpModule* m = (LambdaMvpModule*)data;
    bool optional = false;
    Type* type = lambda_type_nullable_lane_base(
        lambda_type_remove_error(m->tp->pool, mvp_value_contract(m, key)), &optional);
    return type && (type->type_id == LMD_TYPE_INT || type->type_id == LMD_TYPE_FLOAT ||
        type->type_id == LMD_TYPE_INT64);
}

static void widen_closed_parameter(MvpBinding* b) {
    b->closed_parameter = b->scalar_snapshot = false;
    b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM;
}

static void seed_closed_values(LambdaMvpModule* m) {
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !mvp_binding_node(b) || mvp_binding_node(b)->node_type != AST_NODE_PARAM ||
                !mvp_open_value_contract(b->admission_contract) ||
                b->owner >= m->function_count ||
                m->functions[b->owner].boxed_entry) continue;
        TypeParam* p = lambda_type_param(mvp_binding_node(b)->type);
        if (!p || p->is_optional || p->is_var_param || binding_reassigned(m, b, false) ||
                binding_var_borrow(m, b)) continue;
        // closed direct calls may select a scalar lane without adding an
        // admission contract. Every incoming edge must survive widening below.
        b->closed_parameter = true;
        b->contract = &TYPE_INT; b->rep = VALUE_REP_INT_LANE;
    }
    for (uint32_t i = 0; i < m->function_count; i++) {
        MvpFunction* f = &m->functions[i];
        if (f->boxed_entry ||
                !mvp_open_value_contract(f->result_contract) ||
                !mvp_proc_has_final_return(f->ast)) continue;
        // every normal exit is explicit; widening checks all return values,
        // including recursive edges. An explicit top contract remains the
        // admission boundary; it does not require boxed internal transport.
        f->scalar_result_fallback = f->result_contract;
        f->result_contract = &TYPE_INT;
    }
}

static bool update_closed_values(LambdaMvpModule* m, bool choose_domain = false) {
    AstIndex* index = &m->tp->ast_index;
    bool changed = false;
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!choose_domain && (node->node_type == AST_NODE_INDEX_ASSIGN_STAM ||
                node->node_type == AST_NODE_MEMBER_ASSIGN_STAM)) {
            AstAssignStamNode* write = (AstAssignStamNode*)node;
            MvpBinding* b = mvp_local_binding(m, write->object);
            Type* element = b ? mvp_array_element(b->contract) : NULL;
            if (b && b->closed_parameter && (node->node_type != AST_NODE_INDEX_ASSIGN_STAM ||
                    !element || !mvp_private_domain_proven(mvp_value_contract(m, write->value), element))) {
                // mutable private observations survive only exact-domain
                // stores; an inferred element type never constrains a write.
                widen_closed_parameter(b); changed = true;
            }
        }
        if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[i];
        AstFuncNode* target = ast_direct_call_function(call);
        int position = 0;
        for (AstNamedNode* param = target ? target->param : NULL; param;
                param = (AstNamedNode*)param->next, position++) {
            AstNode* arg = mvp_call_argument(call, position);
            MvpBinding* b = mvp_binding(m, param->entry);
            if (!b || !b->closed_parameter) continue;
            Type* incoming = arg ? mvp_value_contract(m, arg) : &TYPE_ANY;
            // implicit parameters short-circuit Error at their existing
            // boundary; an explicit any still admits it as a body value.
            if (b->admission_contract == &TYPE_ANY_NO_ERROR)
                incoming = normal_domain(m, incoming);
            // an error-only actual never enters this implicit parameter.
            // it supplies no normal-domain evidence (S7.7.3, D4.3.1).
            if (incoming == &TYPE_NONE) continue;
            bool errors = lambda_type_accepts_error(incoming);
            if (choose_domain) {
                if (private_scalar_kind(incoming) && incoming->type_id != LMD_TYPE_INT &&
                        !errors && b->contract == &TYPE_INT) {
                    b->contract = lambda_canonical_type_for_id(incoming->type_id);
                    b->rep = mvp_rep(b->contract); changed = true;
                }
                Type* element = mvp_array_element(incoming);
                if (!element && lambda_array_contract_element(incoming) == &TYPE_STRING) element = &TYPE_STRING;
                if (element && !errors && b->contract == &TYPE_INT &&
                        lambda_array_contract_is_plain(incoming)) {
                    // readonly closed callers prove an element domain, not a
                    // new public array contract or a fixed extent. Reuse the
                    // indexed-use audit: an erased child retention flag alone
                    // does not mean that its primitive array owner escapes.
                    Type* previous = b->contract;
                    b->contract = mvp_private_array_domain(m, element);
                    if (mvp_parameter_borrowable(m, param) ||
                            mir_confined_scalar_snapshot(index, target, param, position,
                                element->type_id, scalar_snapshot_key, m)) {
                        b->rep = mvp_rep(b->contract); changed = true;
                    } else b->contract = previous;
                }
                continue;
            }
            if (!errors && mvp_private_domain_proven(incoming, b->contract)) continue;
            // absence, Error and conflicting carriers retain ordinary Item
            // transport; dependent local observations widen in the same pass.
            widen_closed_parameter(b);
            changed = true;
        }
    }
    for (AstNodeId i = 0; i < index->count; i++) {
        AstFunctionId owner = mvp_node_owner(m, i);
        if (index->nodes[i]->node_type != AST_NODE_RETURN_STAM || owner >= m->function_count) continue;
        MvpFunction* f = &m->functions[owner];
        if (!f->scalar_result_fallback) continue;
        Type* incoming = normal_domain(m,
            mvp_value_contract(m, ((AstReturnNode*)index->nodes[i])->value));
        // error-only exits do not constrain the successful native lane.
        if (incoming == &TYPE_NONE) continue;
        if (choose_domain) {
            if (private_scalar_kind(incoming) && incoming->type_id != LMD_TYPE_INT &&
                    f->result_contract == &TYPE_INT) {
                f->result_contract = lambda_canonical_type_for_id(incoming->type_id); changed = true;
            }
            continue;
        }
        if (incoming->type_id == f->result_contract->type_id) continue;
        f->result_contract = f->scalar_result_fallback;
        f->scalar_result_fallback = NULL;
        changed = true;
    }
    return changed;
}

static bool widen_range_bindings(LambdaMvpModule* m) {
    bool changed = false;
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !mvp_binding_node(b) || mvp_binding_node(b)->node_type != AST_NODE_FOR_CLAUSE ||
                b->contract != &TYPE_INT) continue;
        AstLoopNode* loop = (AstLoopNode*)mvp_binding_node(b);
        AstNode* source = ast_unwrap_primary_to_leaf(loop->as);
        if (!source || source->node_type != AST_NODE_BINARY || ((AstBinaryNode*)source)->op != OPERATOR_TO ||
                mvp_native_range_source(m, source)) continue;
        // range counters share the closed-value fixed point: an untyped bound
        // can widen later, and must then retain ordinary iteration (D4.3.1).
        b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM;
        changed = true;
    }
    return changed;
}

static Type* inferred_append_element(LambdaMvpModule* m, AstNode* initializer) {
    AstIndex* index = &m->tp->ast_index;
    AstNode* definition = ast_index_parent(index, initializer);
    while (definition && definition->node_type == AST_NODE_PRIMARY) definition = ast_index_parent(index, definition);
    if (!definition || definition->node_type != AST_NODE_VARIABLE_DECLARATOR) return NULL;
    MvpBinding* binding = mvp_binding(m, ((AstDeclaratorNode*)definition)->entry);
    if (!binding || binding->entry->has_type_annotation || binding->entry->lambda_shared_local ||
            binding_reassigned(m, binding, false)) return NULL;
    uint32_t count = 0; Type* element = NULL; bool appended = false;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, binding->entry), &count);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use == definition || use == ((AstDeclaratorNode*)definition)->id) continue;
        AstNode* parent = ast_index_parent(index, use);
        while (parent && parent->node_type == AST_NODE_PRIMARY) parent = ast_index_parent(index, parent);
        if (!parent) return NULL;
        if (parent->node_type == AST_NODE_RETURN_STAM || parent->node_type == AST_NODE_CONTENT ||
                parent->node_type == AST_NODE_BLOCK) continue;
        if (parent->node_type == AST_NODE_INDEX_EXPR && mvp_local_binding(m, ((AstFieldNode*)parent)->object) == binding) continue;
        if (parent->node_type != AST_NODE_CALL_EXPR) return NULL;
        AstCallNode* call = (AstCallNode*)parent;
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return NULL;
        SysFunc fn = ((AstSysFuncNode*)callee)->fn_info->fn;
        if (fn == SYSFUNC_LEN) continue;
        if (fn != SYSPROC_PUSH || mvp_call_argument_count(call) != 2 ||
                mvp_local_binding(m, mvp_call_argument(call, 0)) != binding) return NULL;
        Type* next = mvp_value_contract(m, mvp_call_argument(call, 1));
        Type* kind = lambda_canonical_type_for_id(next->type_id);
        if ((kind != &TYPE_INT && kind != &TYPE_FLOAT && kind != &TYPE_BOOL) ||
                !mvp_boundary_proven(next, kind) || (element && element != kind)) return NULL;
        element = kind; appended = true;
    }
    // every append has an exact successful scalar domain. Unknown and nullable
    // items keep the open builder; this creates no source annotation (D4.3.1).
    return appended ? element : NULL;
}

static Type* inferred_array_element(LambdaMvpModule* m, AstNode* initializer) {
    AstNode* source = ast_unwrap_primary_to_leaf(initializer);
    if (!source) return NULL;
    AstNode* items = NULL;
    if (source->node_type == AST_NODE_ARRAY) {
        items = ((AstArrayNode*)source)->item;
        if (!items) return inferred_append_element(m, source);
    }
    else if (source->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)source;
        AstNode* target = ast_unwrap_primary_to_leaf(call->function);
        if (!target || target->node_type != AST_NODE_SYS_FUNC ||
                ((AstSysFuncNode*)target)->fn_info->fn != SYSFUNC_FILL) return NULL;
        MvpIntRange count = mvp_int_range(m, mvp_call_argument(call, 0));
        // a failed fill remains an ordinary inferred error value; it cannot
        // acquire an internal typed-declaration failure path (S11.4.1v5).
        if (!count.finite || count.lower < 0) return NULL;
        items = mvp_call_argument(call, 1);
    }
    Type* element = NULL;
    for (AstNode* item = items; item; item = item->next) {
        Type* contract = mvp_value_contract(m, item);
        Type* kind = lambda_canonical_type_for_id(contract->type_id);
        if ((kind != &TYPE_INT && kind != &TYPE_FLOAT && kind != &TYPE_BOOL && kind != &TYPE_STRING) ||
                (element && element != kind)) return NULL;
        element = kind;
    }
    return element;
}

static bool widen_inferred_binding(LambdaMvpModule* m, MvpBinding* b, Type* source) {
    if (!b || b->contract == &TYPE_ANY || mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            ((AstDeclaratorNode*)mvp_binding_node(b))->declared_type ||
            ((AstDeclaratorNode*)mvp_binding_node(b))->is_type_definition) return false;
    if (b->inferred_array) {
        // every initializer and indexed write is checked at the same effect
        // fixed point; this is a private proof, never a new user contract.
        Type* element = inferred_array_element(m, ((AstDeclaratorNode*)mvp_binding_node(b))->init);
        if (element == lambda_array_contract_element(normal_domain(m, b->contract))) return false;
        b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM;
        return true;
    }
    Type* joined = b->contract;
    bool source_null = false, binding_null = false;
    Type* from = lambda_type_nullable_lane_base(lambda_type_remove_error(m->tp->pool, source), &source_null);
    Type* to = lambda_type_nullable_lane_base(lambda_type_remove_error(m->tp->pool, b->contract), &binding_null);
    if (source == &TYPE_ANY) joined = &TYPE_ANY;
    // widening an already nullable domain must be idempotent; allocating an
    // equivalent descriptor on each pass prevents fixed-point convergence.
    else if (source == &TYPE_NULL && to)
        joined = binding_null ? b->contract : mvp_nullable_contract(m, to);
    else if (from && to && from->type_id == to->type_id && source_null && !binding_null)
        joined = mvp_nullable_contract(m, to);
    else if (!from || !to || from->type_id != to->type_id) joined = &TYPE_ANY;
    // a private record cursor keeps only the same enforced shape across all
    // writes; a different inferred record must remain unrestricted (S12.2.1v3).
    if (joined != &TYPE_ANY && from && to && to->type_id == LMD_TYPE_MAP &&
            source != &TYPE_NULL && from != to) joined = &TYPE_ANY;
    if (joined != &TYPE_ANY && (lambda_type_has_proven_error(source) || lambda_type_has_proven_error(b->contract)))
        joined = lambda_type_union_normalized(m->tp->pool, joined, &TYPE_ERROR);
    if (joined == b->contract) return false;
    b->contract = joined; b->rep = mvp_rep(joined);
    return true;
}

Type* mvp_guarded_parameter_candidate(LambdaMvpModule* m, MvpBinding* binding) {
    if (!binding || !binding->entry || !mvp_binding_node(binding) ||
            mvp_binding_node(binding)->node_type != AST_NODE_PARAM ||
            !mvp_open_value_contract(binding->contract) || binding->entry->lambda_shared_local)
        return NULL;
    TypeParam* param = lambda_type_param(mvp_binding_node(binding)->type);
    Type* candidate = binding->candidates[MVP_CANDIDATE_SCALAR];
    if (!param || param->is_var_param || param->is_optional ||
            binding_reassigned(m, binding, true) || binding_var_borrow(m, binding)) return NULL;
    if (private_scalar_kind(candidate)) return candidate;
    AstIndex* index = &m->tp->ast_index;
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, binding->entry), &count);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* operand = index->nodes[uses[i]];
        AstNode* parent = ast_index_parent(index, operand);
        while (parent && parent->node_type == AST_NODE_PRIMARY) {
            operand = parent; parent = ast_index_parent(index, parent);
        }
        if (!parent || parent->node_type != AST_NODE_BINARY) continue;
        AstBinaryNode* binary = (AstBinaryNode*)parent;
        // string is a speculative version of erased concatenation, selected
        // only after all participating parameters pass exact entry guards.
        if (binary->op == OPERATOR_JOIN) return &TYPE_STRING;
        MirNumericOpPlan plan = {};
        if (!em_numeric_op_plan(binary->op, &plan, true)) continue;
        AstNode* sibling = binary->left == operand ? binary->right : binary->left;
        // an integer consumer predicts a useful version, never an admission
        // contract. Exact entry guards retain every other input domain.
        if (mvp_value_contract(m, sibling)->type_id == LMD_TYPE_INT) return &TYPE_INT;
    }
    return NULL;
}

void mvp_refine_guarded_locals(LambdaMvpModule* m, MvpFunction* function) {
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, function->ast->body);
    AstNodeId end = ast_index_subtree_end(index, root);
    for (AstNodeId id = root; id < end; id++) {
        AstNode* node = index->nodes[id];
        if (mvp_node_owner(m, id) != function->id || node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)node;
        MvpBinding* binding = mvp_binding(m, declaration->entry);
        if (!binding || declaration->declared_type || declaration->is_type_definition ||
                binding->entry->lambda_shared_local || binding_reassigned(m, binding, true, false) ||
                binding_var_borrow(m, binding)) continue;
        Type* source = mvp_inferred_scalar_contract(mvp_value_contract(m, declaration->init));
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(normal_domain(m, source), &optional);
        if (!private_scalar_kind(base)) continue;
        if (source != binding->contract) binding->range = {};
        binding->contract = source; binding->rep = mvp_rep(source);
    }
    // a guarded parameter observation does not constrain a later assignment.
    // reuse the ordinary widening fixed point before choosing any local lane.
    bool changed;
    do {
        changed = false;
        for (AstNodeId id = root; id < end; id++) {
            AstNode* node = index->nodes[id];
            if (mvp_node_owner(m, id) != function->id) continue;
            NameEntry* entry = NULL; AstNode* source = NULL;
            if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
                entry = ((AstDeclaratorNode*)node)->entry; source = ((AstDeclaratorNode*)node)->init;
            } else if (node->node_type == AST_NODE_ASSIGN_STAM) {
                entry = ((AstAssignStamNode*)node)->target_entry; source = ((AstAssignStamNode*)node)->value;
            }
            MvpBinding* binding = mvp_binding(m, entry);
            if (binding && widen_inferred_binding(m, binding, mvp_value_contract(m, source))) {
                binding->range = {}; changed = true;
            }
        }
    } while (changed);
}

static bool admitted_contract(Type* type, Pool* pool) {
    if (!type) return true;
    type = type_field_unwrap_simple_decl(type);
    if (type == &TYPE_TYPE) return true;
    if (lambda_member_test_type_domain(type)) return true;
    if (type && type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINDER)
        return admitted_contract(((TypeBinder*)type)->bound, pool);
    if (type && type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BOUND_REF)
        return admitted_contract(((TypeBoundRef*)type)->bound, pool);
    // annotation admission checks only the base; predicates belong to `is`,
    // match and handler type positions (S11.4.6v4).
    if (lambda_type_is_constrained(type)) {
        Type* base = lambda_constrained_type_base(type);
        return base && admitted_contract(base, pool);
    }
    // abstract numeric contracts use the shared Item admission and owned-result
    // protocol; their LMD_TYPE_TYPE tag does not denote a Type* value (D2.3.1).
    if (type == &TYPE_ERROR || type == &TYPE_ANY_NO_ERROR || type_is_global_meta_type(type) ||
            mvp_boundary_proven(type, &TYPE_ERROR)) return true;
    if (lambda_type_is_union(type) && lambda_type_accepts_error(type)) {
        Type* success = lambda_type_remove_error(pool, type);
        if (success != type) return admitted_contract(success, pool);
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        TypeBinary* alternatives = (TypeBinary*)type;
        return admitted_contract(alternatives->left, pool) && admitted_contract(alternatives->right, pool);
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_UNARY &&
            (((TypeUnary*)type)->op == OPERATOR_ZERO_MORE || ((TypeUnary*)type)->op == OPERATOR_ONE_MORE ||
             ((TypeUnary*)type)->op == OPERATOR_REPEAT)) return admitted_contract(((TypeUnary*)type)->operand, pool);
    bool optional = false;
    Type* base = lambda_type_nullable_lane_base(type, &optional);
    if (optional && base && base != type) return admitted_contract(base, pool);
    return type && (mvp_array_contract(type) || type->type_id == LMD_TYPE_INT || type->type_id == LMD_TYPE_FLOAT ||
        type->type_id == LMD_TYPE_BOOL || type->type_id == LMD_TYPE_STRING ||
        type->type_id == LMD_TYPE_SYMBOL || type->type_id == LMD_TYPE_NUM_SIZED ||
        type->type_id == LMD_TYPE_INT64 || type->type_id == LMD_TYPE_UINT64 ||
        type->type_id == LMD_TYPE_ELEMENT || type->type_id == LMD_TYPE_RANGE ||
        type->type_id == LMD_TYPE_DTIME || type->type_id == LMD_TYPE_BINARY || type->type_id == LMD_TYPE_FUNC ||
        type->type_id == LMD_TYPE_PATH || type->type_id == LMD_TYPE_COMPLEX ||
        type->type_id == LMD_TYPE_ARRAY || type->type_id == LMD_TYPE_MAP || type->type_id == LMD_TYPE_DECIMAL ||
        type->type_id == LMD_TYPE_NULL || type->type_id == LMD_TYPE_ERROR || type->type_id == LMD_TYPE_ANY);
}

static MvpIntRange array_element_range(LambdaMvpModule* m, MvpBinding* binding, int depth) {
    if (depth > 16 || !binding || mvp_array_element(binding->contract) != &TYPE_INT || binding_var_borrow(m, binding)) return {};
    if (mvp_binding_node(binding)->node_type == AST_NODE_PARAM) {
        AstNamedNode* parameter = (AstNamedNode*)mvp_binding_node(binding);
        if (!mvp_parameter_borrowable(m, parameter) || binding->owner >= m->function_count) return {};
        AstFuncNode* function = m->functions[binding->owner].ast;
        int position = 0;
        for (AstNamedNode* p = function->param; p && p != parameter; p = (AstNamedNode*)p->next) position++;
        MvpIntRange result = {}; bool seen = false;
        AstIndex* index = &m->tp->ast_index;
        for (AstNodeId i = 0; i < index->count; i++) {
            if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
            AstCallNode* call = (AstCallNode*)index->nodes[i];
            if (ast_direct_call_function(call) != function) continue;
            AstNode* actual = mvp_call_argument(call, position);
            MvpIntRange incoming = array_element_range(m, mvp_local_binding(m, actual), depth + 1);
            if (!incoming.finite) return {};
            if (!seen) result = incoming;
            else {
                if (incoming.lower < result.lower) result.lower = incoming.lower;
                if (incoming.upper > result.upper) result.upper = incoming.upper;
            }
            seen = true;
        }
        return seen ? result : MvpIntRange{};
    }
    if (mvp_binding_node(binding)->node_type != AST_NODE_VARIABLE_DECLARATOR) return {};
    AstNode* initial = ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)mvp_binding_node(binding))->init);
    if (initial && initial->node_type == AST_NODE_ARRAY && binding->readonly_literal) {
        MvpIntRange result = {}; bool seen = false;
        for (AstNode* item = ((AstArrayNode*)initial)->item; item; item = item->next) {
            MvpIntRange member = mvp_int_range(m, item, depth + 1);
            if (!member.finite) return {};
            if (!seen) result = member;
            else {
                if (member.lower < result.lower) result.lower = member.lower;
                if (member.upper > result.upper) result.upper = member.upper;
            }
            seen = true;
        }
        return seen ? result : MvpIntRange{};
    }
    if (!initial || initial->node_type != AST_NODE_CALL_EXPR) return {};
    AstCallNode* fill = (AstCallNode*)initial;
    AstNode* callee = ast_unwrap_primary_to_leaf(fill->function);
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
            ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_FILL) return {};
    MvpIntRange range = mvp_int_range(m, mvp_call_argument(fill, 1));
    if (!range.finite) return {};
    AstIndex* index = &m->tp->ast_index;
    // value copies detach before writing; only direct writes/borrows of this
    // root can change its element domain (S9.1.2, D5.4.5).
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (node->node_type == AST_NODE_ASSIGN_STAM &&
                ((AstAssignStamNode*)node)->target_entry == binding->entry) return {};
        if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM &&
                mvp_local_binding(m, ((AstAssignStamNode*)node)->object) == binding) {
            MvpIntRange rhs = mvp_int_range(m, ((AstAssignStamNode*)node)->value);
            if (!rhs.finite) return {};
            if (rhs.lower < range.lower) range.lower = rhs.lower;
            if (rhs.upper > range.upper) range.upper = rhs.upper;
        }
    }
    return range;
}

bool mvp_array_sum_plan(LambdaMvpModule* m, AstLoopControlNode* loop, MvpArraySumPlan* plan) {
    AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY || !loop->body ||
            loop->body->node_type != AST_NODE_CONTENT) return false;
    AstBinaryNode* compare = (AstBinaryNode*)condition;
    MvpBinding* counter = mvp_local_binding(m, compare->left);
    AstNode* limit = ast_unwrap_primary_to_leaf(compare->right);
    MvpBinding* bound = mvp_local_binding(m, limit);
    if ((compare->op != OPERATOR_LT && compare->op != OPERATOR_LE) || !counter || counter->rep != VALUE_REP_INT_LANE ||
            (!bound && (!limit || limit->node_type != AST_NODE_PRIMARY)) ||
            mvp_value_contract(m, limit)->type_id != LMD_TYPE_INT || bound == counter) return false;
    AstNode* first = ((AstListNode*)loop->body)->item;
    AstNode* second = first ? first->next : NULL;
    if (!first || !second || second->next || second->node_type != AST_NODE_ASSIGN_STAM) return false;
    AstNode* predicate = NULL;
    if (first->node_type == AST_NODE_IF_EXPR) {
        AstIfNode* branch = (AstIfNode*)first;
        if (branch->otherwise || !branch->then || branch->then->node_type != AST_NODE_CONTENT) return false;
        predicate = branch->cond;
        first = ((AstListNode*)branch->then)->item;
        if (!first || first->next) return false;
    }
    if (first->node_type != AST_NODE_ASSIGN_STAM) return false;
    AstAssignStamNode* assignment = (AstAssignStamNode*)first;
    AstAssignStamNode* increment = (AstAssignStamNode*)second;
    MvpBinding* sum = mvp_binding(m, assignment->target_entry);
    AstNode* add_node = ast_unwrap_primary_to_leaf(assignment->value);
    AstNode* step_node = ast_unwrap_primary_to_leaf(increment->value);
    if (!sum || sum == counter || sum == bound ||
            (sum->rep != VALUE_REP_INT_LANE &&
                (sum->rep != VALUE_REP_ITEM || sum->admission_contract != &TYPE_INT)) ||
            increment->target_entry != counter->entry || !add_node || !step_node ||
            add_node->node_type != AST_NODE_BINARY || step_node->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* add = (AstBinaryNode*)add_node;
    AstBinaryNode* step = (AstBinaryNode*)step_node;
    int64_t literal;
    if (add->op != OPERATOR_ADD || step->op != OPERATOR_ADD || mvp_local_binding(m, add->left) != sum ||
            mvp_local_binding(m, step->left) != counter ||
            !mir_decimal_int_literal(m->tp->source, step->right, &literal) || literal != 1) return false;
    if (predicate && (!mir_decimal_int_literal(m->tp->source, add->right, &literal) || literal != 1)) return false;
    AstNode* read = ast_unwrap_primary_to_leaf(predicate ? predicate : add->right);
    if (!read || read->node_type != AST_NODE_INDEX_EXPR) return false;
    AstFieldNode* access = (AstFieldNode*)read;
    MvpBinding* array = mvp_local_binding(m, access->object);
    if (!array || mvp_local_binding(m, access->field) != counter) return false;
    if (predicate && mvp_array_element(array->contract) != &TYPE_BOOL) return false;
    MvpIntRange elements = predicate ? MvpIntRange{true, 0, 1} : array_element_range(m, array);
    if (!elements.finite) return false;
    int64_t magnitude = elements.lower < 0 ? -elements.lower : elements.lower;
    int64_t upper = elements.upper < 0 ? -elements.upper : elements.upper;
    if (upper > magnitude) magnitude = upper;
    *plan = {array, counter, sum, limit, magnitude ? INT53_MAX / magnitude : INT53_MAX, compare->op == OPERATOR_LE};
    return true;
}

static MvpBinding* copy_index(LambdaMvpModule* m, AstNode* source, int64_t* offset) {
    if (MvpBinding* binding = mvp_local_binding(m, source)) return binding;
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (!node || node->node_type != AST_NODE_BINARY) return NULL;
    AstBinaryNode* b = (AstBinaryNode*)node;
    if (b->op != OPERATOR_ADD && b->op != OPERATOR_SUB) return NULL;
    MvpBinding* binding = mvp_local_binding(m, b->left);
    int64_t constant;
    if (!binding || !mir_decimal_int_literal(m->tp->source, b->right, &constant) ||
            constant < INT53_MIN || constant > INT53_MAX) return NULL;
    *offset = b->op == OPERATOR_ADD ? constant : -constant;
    return binding;
}

bool mvp_array_copy_plan(LambdaMvpModule* m, AstLoopControlNode* loop, MvpArrayCopyPlan* plan) {
    AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY || !loop->body ||
            loop->body->node_type != AST_NODE_CONTENT) return false;
    AstBinaryNode* compare = (AstBinaryNode*)condition;
    MvpBinding* counter = mvp_local_binding(m, compare->left);
    MvpBinding* bound = mvp_local_binding(m, compare->right);
    int64_t literal;
    if (compare->op != OPERATOR_LT || !counter || counter->rep != VALUE_REP_INT_LANE ||
            (!bound && !mir_decimal_int_literal(m->tp->source, compare->right, &literal)) ||
            (bound && bound->rep != VALUE_REP_INT_LANE)) return false;
    AstNode* first = ((AstListNode*)loop->body)->item;
    if (!first || first->node_type != AST_NODE_INDEX_ASSIGN_STAM) return false;
    AstAssignStamNode* store = (AstAssignStamNode*)first;
    AstNode* input = ast_unwrap_primary_to_leaf(store->value);
    if (!input || input->node_type != AST_NODE_INDEX_EXPR) return false;
    AstFieldNode* load = (AstFieldNode*)input;
    MvpBinding* source = mvp_local_binding(m, load->object), *destination = mvp_local_binding(m, store->object);
    int64_t read_offset = 0, write_offset = 0;
    MvpBinding* read_index = copy_index(m, load->field, &read_offset);
    MvpBinding* write_index = copy_index(m, store->key, &write_offset);
    if (!source || !destination || !mvp_array_element(source->contract) ||
            mvp_array_element(source->contract) != mvp_array_element(destination->contract) ||
            !read_index || !write_index || read_index->rep != VALUE_REP_INT_LANE ||
            write_index->rep != VALUE_REP_INT_LANE || (counter != read_index && counter != write_index) ||
            bound == read_index || bound == write_index) return false;
    bool read_step = false, write_step = false;
    for (AstNode* n = first->next; n; n = n->next) {
        if (n->node_type != AST_NODE_ASSIGN_STAM || !mvp_unit_increment(m, (AstAssignStamNode*)n)) return false;
        NameEntry* target = ((AstAssignStamNode*)n)->target_entry;
        if ((target != read_index->entry && target != write_index->entry) ||
                (target == read_index->entry && read_step) || (target == write_index->entry && write_step)) return false;
        read_step |= target == read_index->entry; write_step |= target == write_index->entry;
    }
    if (!read_step || !write_step) return false;
    *plan = {source, destination, read_index, write_index, counter, compare->right, read_offset, write_offset};
    return true;
}

bool mvp_array_reverse_plan(LambdaMvpModule* m, AstLoopControlNode* loop, MvpArrayReversePlan* plan) {
    AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY || !loop->body ||
            loop->body->node_type != AST_NODE_CONTENT) return false;
    AstBinaryNode* test = (AstBinaryNode*)condition;
    MvpBinding* first = mvp_local_binding(m, test->left), *last = mvp_local_binding(m, test->right);
    if (test->op != OPERATOR_LT || !first || !last || first == last ||
            first->rep != VALUE_REP_INT_LANE || last->rep != VALUE_REP_INT_LANE) return false;
    AstNode* statements[5]; AstNode* n = ((AstListNode*)loop->body)->item;
    for (int i = 0; i < 5; i++) { if (!n) return false; statements[i] = n; n = n->next; }
    if (n || (statements[0]->node_type != AST_NODE_VAR_STAM && statements[0]->node_type != AST_NODE_LET_STAM) ||
            statements[1]->node_type != AST_NODE_INDEX_ASSIGN_STAM || statements[2]->node_type != AST_NODE_INDEX_ASSIGN_STAM ||
            statements[3]->node_type != AST_NODE_ASSIGN_STAM || statements[4]->node_type != AST_NODE_ASSIGN_STAM) return false;
    AstNode* decl = ((AstVarDeclNode*)statements[0])->declarations;
    if (!decl || decl->next || decl->node_type != AST_NODE_VARIABLE_DECLARATOR) return false;
    AstDeclaratorNode* temporary = (AstDeclaratorNode*)decl;
    AstNode* init = ast_unwrap_primary_to_leaf(temporary->init);
    AstAssignStamNode* left = (AstAssignStamNode*)statements[1], *right = (AstAssignStamNode*)statements[2];
    AstNode* rhs = ast_unwrap_primary_to_leaf(left->value);
    if (!init || init->node_type != AST_NODE_INDEX_EXPR || !rhs || rhs->node_type != AST_NODE_INDEX_EXPR) return false;
    AstFieldNode* read_first = (AstFieldNode*)init, *read_last = (AstFieldNode*)rhs;
    MvpBinding* array = mvp_local_binding(m, read_first->object), *saved = mvp_binding(m, temporary->entry);
    Type* element = array ? mvp_array_element(array->contract) : NULL;
    if (!element || !saved || lambda_static_boundary_relation(element, saved->contract, false) != STATIC_BOUNDARY_PROVEN ||
            mvp_rep(element) != saved->rep || mvp_local_binding(m, right->value) != saved ||
            mvp_local_binding(m, read_first->field) != first || mvp_local_binding(m, read_last->field) != last ||
            mvp_local_binding(m, left->key) != first || mvp_local_binding(m, right->key) != last ||
            mvp_local_binding(m, left->object) != array || mvp_local_binding(m, right->object) != array ||
            mvp_local_binding(m, read_last->object) != array) return false;
    AstAssignStamNode* up = (AstAssignStamNode*)statements[3], *down = (AstAssignStamNode*)statements[4];
    if (up->target_entry != first->entry || down->target_entry != last->entry ||
            !mvp_unit_step(m, up, 1) || !mvp_unit_step(m, down, -1)) return false;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop);
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, saved->entry), &count);
    for (uint32_t i = 0; i < count; i++) if (!ast_index_node_descends(index, uses[i], root)) return false;
    *plan = {array, first, last};
    return true;
}

static void plan_return(MvpFunction* f, Type* type, bool external) {
    // a bound reference belongs to this activation; callers receive an Item
    // whose actual type is established by the callee's return admission.
    if (lambda_type_contract_has_binder(f->declared_result, true) ||
            lambda_type_contract_has_binder(type, true)) type = &TYPE_ANY;
    external |= f->boxed_entry;
    ValueRep rep = external ? VALUE_REP_ITEM : mvp_rep(type);
    bool native = rep != VALUE_REP_ITEM;
    f->result_contract = type;
    f->abi.entry = {native ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY,
        true, false, false, external};
    f->abi.result.normal = {type ? type->type_id : LMD_TYPE_ANY, rep,
        native || !mvp_needs_number_home(type) ? SCALAR_RETURN_NONE : SCALAR_RETURN_DYNAMIC};
    f->abi.result.error = {LMD_TYPE_ERROR, VALUE_REP_ITEM, SCALAR_RETURN_NONE};
    bool errors = f->abi.effects.may_return_error ||
        (f->declared_result && lambda_type_accepts_error(f->declared_result));
    f->abi.result.shape = native ? (errors ? RETURN_SHAPE_NATIVE_ERROR : RETURN_SHAPE_NATIVE)
        : RETURN_SHAPE_ITEM_SCALAR;
    f->abi.result.companion = em_companion_transport(f->abi.result.shape, external);
    f->abi.result.error_lane = FN_ERROR_LANE_NONE;
}

static bool plan_template_function(LambdaMvpModule* m, AstViewNode* view,
        AstEventHandler* handler, const char* name, const char* reference) {
    MvpFunction* function = &m->functions[m->function_count];
    function->id = m->function_count++;
    function->parent = AST_FUNCTION_ID_INVALID;
    function->view = view; function->handler = handler; function->template_ref = reference;
    function->name = name_pool_create_name(m->tp->name_pool, name)->chars;
    // the retained source AST is immutable; this private entry descriptor
    // reuses ordinary MVP frames, returns and precise roots (D6.3, D7.4.1).
    AstFuncNode* ast = (AstFuncNode*)pool_calloc(m->tp->pool, sizeof(AstFuncNode));
    ast->node_type = handler ? AST_NODE_PROC : AST_NODE_FUNC;
    ast->body = handler ? handler->body : view->body;
    ast->source_span = handler ? handler->source_span : view->source_span;
    ast->type = view->signature;
    if (handler) {
        TypeFunc* signature = (TypeFunc*)alloc_type(m->tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
        signature->is_proc = true;
        signature->returned = signature->return_contract = &TYPE_ANY;
        ast->type = signature;
        // handlers are physical procedures outside the source function table;
        // their scoped children still join at each block exit (S13.3.1v2).
        ast->analysis = (FnAnalysis*)pool_calloc(m->tp->pool, sizeof(FnAnalysis));
        if (!lambda_ast_plan_body_concurrency(ast->body, ast->analysis))
            return mvp_reject(m, handler, "handler concurrency planning failed");
    }
    function->ast = ast;
    function->boxed_entry = true;
    function->declared_result = ((TypeFunc*)ast->type)->return_contract;
    function->abi.param_count = handler ? 2 : 1;
    function->abi.params = (FnParamAnalysis*)mem_calloc(function->abi.param_count, sizeof(FnParamAnalysis), MEM_CAT_TEMP);
    for (int i = 0; i < function->abi.param_count; i++) function->abi.params[i] = {LMD_TYPE_ANY, VALUE_REP_ITEM, 0};
    function->abi.effects.may_gc = function->abi.effects.may_reenter = function->abi.effects.may_return_error = true;
    plan_return(function, function->declared_result, true);
    return true;
}

// place invariance consumes the completed inference fixed point, not the
// initializer's provisional carrier (D4.3.1, S12.2.1v3).
static bool admitted_var_arguments(LambdaMvpModule* m, AstCallNode* call, MvpFunction* f) {
    AstNamedNode* param = f->ast->param;
    for (int i = 0; i < mvp_call_argument_count(call) && param; i++, param = (AstNamedNode*)param->next) {
        AstNode* arg = mvp_call_argument(call, i);
        TypeParam* p = lambda_type_param(param->type);
        if (p && p->is_var_param) {
            MvpBinding* b = mvp_local_binding(m, arg);
            Type* contract = mvp_contract(param);
            // an erased var receives the value without a physical restriction;
            // caller publication still validates the destination's leaf contract.
            bool invariant = b && (mvp_open_value_contract(contract) || lambda_type_repr_equal(b->contract, contract) ||
                lambda_type_repr_equal(b->admission_contract, contract) ||
                mvp_boundary_proven(&TYPE_ANY, b->admission_contract) ||
                (mvp_array_contract(b->contract) && lambda_array_contract_compatible(b->contract, contract, true)));
            if (!b) {
                AstCowPath path = {};
                MvpBinding* root = ast_collect_cow_path(&path, arg) ? mvp_local_binding(m, path.root) : NULL;
                int64_t index_mask = 0;
                int last = path.count - 1;
                if (last >= 0) path.count = last;
                Type* leaf = root && last >= 0 ? ast_map_path_leaf_contract(root->admission_contract, &path,
                    path.segment[last], path.is_member[last], &index_mask) : NULL;
                invariant = root && last >= 0 && (mvp_open_value_contract(contract) || root->admission_contract == &TYPE_ANY ||
                    lambda_type_repr_equal(leaf, contract) ||
                    lambda_array_contract_compatible(leaf, contract, true) || leaf == &TYPE_ANY);
            }
            if (!invariant)
                return mvp_reject(m, arg, "var arguments require an invariant local binding");
        }
    }
    return true;
}

MvpFunction* mvp_require_function_reference(LambdaMvpModule* m, AstFuncNode* target,
        AstNode* callee, Script* owner) {
    MvpFunction* f = mvp_function(m, target);
    NameEntry* resident = callee && callee->node_type == AST_NODE_IDENT ? ((AstIdentNode*)callee)->entry : NULL;
    Script* resident_owner = NULL; int resident_slot;
    bool resident_entry = mvp_interp_binding(m, resident, &resident_owner, &resident_slot);
    if (resident_entry) owner = resident_owner;
    if (!owner && callee && callee->node_type == AST_NODE_IDENT) {
        NameEntry* entry = ((AstIdentNode*)callee)->entry;
        owner = mvp_import_owner(m, entry);
    }
    if (!f && owner) {
        if (resident_entry || ((TypeFunc*)target->type)->is_public || target->is_that_predicate) {
            f = (MvpFunction*)mem_calloc(1, sizeof(MvpFunction), MEM_CAT_TEMP);
            f->ast = target; f->imported = f->boxed_entry = true; f->id = AST_FUNCTION_ID_INVALID;
            f->interp_binding = resident_entry ? resident : NULL;
            StrBuf* name = strbuf_new_cap(64);
            bool js_export = owner->profile == &js_profile;
            TypeFunc* signature = (TypeFunc*)target->type;
            if (js_export && signature->param_count > 8) {
                strbuf_free(name); mem_free(f);
                mvp_reject(m, callee, "JavaScript export arity exceeds the shared bridge limit");
                return NULL;
            }
            write_fn_name_for_script_ex(name, target, owner, js_export ? "" : "_b");
            f->name = name_pool_create_name(m->tp->name_pool, name->str)->chars;
            strbuf_free(name);
            if (js_export) {
                f->js_export_name = f->name;
                f->js_export_owner = owner;
                // imported JS declarations have an arity but no Core AST
                // parameters. Their adapter owns a separate activation ID.
                unsigned offset = m->imported_functions ? m->imported_functions->length : 0;
                f->id = m->function_count + offset;
                char adapter[64]; snprintf(adapter, sizeof(adapter), "mvp_js_export_%u", offset);
                f->name = name_pool_create_name(m->tp->name_pool, adapter)->chars;
            }
            f->abi.param_count = js_export ? signature->param_count : ast_linked_node_count(target->param);
            f->abi.params = (FnParamAnalysis*)mem_calloc(f->abi.param_count, sizeof(FnParamAnalysis), MEM_CAT_TEMP);
            for (int i = 0; i < f->abi.param_count; i++) f->abi.params[i] = {LMD_TYPE_ANY, VALUE_REP_ITEM, 0};
            f->abi.effects.may_gc = f->abi.effects.may_reenter = f->abi.effects.may_return_error = true;
            f->declared_result = &TYPE_ANY;
            Type* normal = &TYPE_ANY;
            if (signature->has_explicit_return_contract && signature->return_contract &&
                    !lambda_type_contract_has_binder(signature->return_contract, true)) {
                Type* declared = signature->return_contract;
                // an Error-only export has bottom as its successful domain,
                // not a missing descriptor that a call consumer may dereference.
                Type* success = normal_domain(m, declared);
                if (success != &TYPE_ANY_NO_ERROR && admitted_contract(success, m->tp->pool)) {
                    // the imported boxed entry enforces its declared result
                    // just like a local call. Preserve its normal domain
                    // without changing the external Item ABI (D7.4.2).
                    f->declared_result = declared;
                    normal = success;
                }
            }
            plan_return(f, normal, true);
            if (!m->imported_functions) m->imported_functions = arraylist_new(8);
            arraylist_append(m->imported_functions, f);
        }
    }
    return f;
}

static bool require_constraint_predicates(LambdaMvpModule* m, AstNode* pattern) {
    pattern = ast_unwrap_primary(pattern);
    if (pattern && pattern->node_type == AST_NODE_BINARY_TYPE &&
            ((AstBinaryNode*)pattern)->op == OPERATOR_UNION) {
        AstBinaryNode* alternatives = (AstBinaryNode*)pattern;
        return require_constraint_predicates(m, alternatives->left) &&
            require_constraint_predicates(m, alternatives->right);
    }
    TypeConstrained* constrained = ast_constrained_type(pattern);
    if (!constrained) return true;
    TypeConstrained* layers[LAMBDA_CONSTRAINT_CHAIN_MAX];
    int count = ast_constrained_type_layers(constrained, layers);
    if (count < 0) return mvp_reject(m, pattern, "invalid constrained predicate chain");
    for (int i = 0; i < count; i++) {
        Script* owner = runtime_script_instance(m->tp->runtime, layers[i]->module);
        if (!mvp_require_function_reference(m, layers[i]->predicate_fn, NULL, owner))
            return mvp_reject(m, pattern, "constrained predicate's declaring module is unavailable");
    }
    return true;
}

static bool admitted_call(LambdaMvpModule* m, AstCallNode* call, bool task_launch = false) {
    if (call->interp_has_spread_args)
        return mvp_reject(m, call, "spread arguments are not admitted yet");
    AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
    AstFuncNode* target = ast_direct_call_function(call);
    if (ast_call_has_named_args(call) && !target && !interp_named_sys_args_supported(callee))
        return mvp_reject(m, call, "named arguments require a direct lexical definition");
    int count = mvp_call_argument_count(call);
    if (count > LAMBDA_MAX_FUNCTION_ARGS && (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
            ((AstSysFuncNode*)callee)->fn_info->fn != SYSPROC_PRINT))
        return mvp_reject(m, call, "call exceeds the shared argument limit");
    if (target) {
        MvpFunction* f = mvp_require_function_reference(m, target, callee);
        if (!f || (!task_launch && (count < ((TypeFunc*)target->type)->required_param_count ||
                (!mvp_variadic_function(f) && count > f->abi.param_count))))
            return mvp_reject(m, call, "direct call arguments do not fit the admitted definition");
        return true;
    }
    if (task_launch) return true;
    if (ast_numeric_type_call_target(call)) return true;
    // nonnumeric Type values use the shared checked dynamic-call outcome;
    // only explicit numeric conversions have a separate native entry.
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return true;
    SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
    if (!info || (info->fn != SYSPROC_PRINT && count > LAMBDA_MAX_FUNCTION_ARGS))
        return mvp_reject(m, call, "unsupported builtin argument count");
    switch (info->fn) {
    case SYSPROC_PRINT: case SYSPROC_SELECT:
        return true;
    case SYSPROC_VMAP_SET:
    case SYSPROC_CLOCK: case SYSFUNC_ABS: case SYSFUNC_FLOOR: case SYSFUNC_SQRT: case SYSFUNC_SIN: case SYSFUNC_COS:
    case SYSFUNC_ROUND:
    case SYSFUNC_INT: case SYSFUNC_FLOAT: case SYSFUNC_STRING:
    case SYSFUNC_LEN: case SYSFUNC_BOR: case SYSFUNC_BXOR: case SYSFUNC_SHL: case SYSFUNC_SHR:
    case SYSFUNC_FILL: case SYSFUNC_ORD: case SYSFUNC_CHR: case SYSFUNC_SLICE:
        return count == info->arg_count || mvp_reject(m, call, "builtin arity mismatch");
    default: {
        // colour and parking do not change a fixed native ABI: suspension
        // keeps the activation's stack and precise roots (S13.1.2v2, D7.5.3).
        // place-mutating procedures keep their explicit lowering.
        JitImportMetadata metadata = {};
        if (info->arg_count >= 0 &&
                count == info->arg_count && info->c_func_name &&
                jit_import_get_metadata(info->c_func_name, &metadata)) return true;
        return mvp_reject(m, call, "builtin is outside the MVP subset admission set");
    }
    }
}

// Module scalar slots have no GC payload. Admit only initialization that cannot
// call back into a definition before its immutable dependencies are initialized.
static bool module_scalar_initializer(LambdaMvpModule* m, AstNode* source, int depth = 0) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || depth > 32) return false;
    TypeId type = mvp_value_contract(m, n)->type_id;
    if (type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT && type != LMD_TYPE_BOOL) return false;
    if (n->node_type == AST_NODE_PRIMARY) return true;
    if (n->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_local_binding(m, n);
        return b && b->owner == AST_FUNCTION_ID_INVALID && !b->entry->is_mutable &&
            mvp_binding_node(b)->node_type == AST_NODE_VARIABLE_DECLARATOR &&
            mvp_binding_node(b)->source_span.start_byte < n->source_span.start_byte &&
            module_scalar_initializer(m, ((AstDeclaratorNode*)mvp_binding_node(b))->init, depth + 1);
    }
    if (n->node_type == AST_NODE_BINARY) return
        module_scalar_initializer(m, ((AstBinaryNode*)n)->left, depth + 1) &&
        module_scalar_initializer(m, ((AstBinaryNode*)n)->right, depth + 1);
    return n->node_type == AST_NODE_UNARY &&
        module_scalar_initializer(m, ((AstUnaryNode*)n)->operand, depth + 1);
}

Type* mvp_module_array_literal_element(LambdaMvpModule* m, MvpBinding* binding, AstNode* source) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    LambdaArrayContractInfo info = {};
    if (!node || node->node_type != AST_NODE_ARRAY ||
            !lambda_array_contract_info(binding->contract, &info) || info.rank != 1 || info.counted_axes) return NULL;
    Type* element = type_field_unwrap_simple_decl(info.immediate_element);
    if (!element || (element->type_id != LMD_TYPE_STRING && element->type_id != LMD_TYPE_FLOAT &&
            element->type_id != LMD_TYPE_INT && element->type_id != LMD_TYPE_BOOL)) return NULL;
    for (AstNode* item = ((AstArrayNode*)node)->item; item; item = item->next) {
        AstNode* leaf = ast_unwrap_primary_to_leaf(item);
        if (!leaf || leaf->node_type != AST_NODE_PRIMARY || !leaf->type ||
                leaf->type->type_id != element->type_id || !mvp_boundary_proven(leaf->type, element)) return NULL;
    }
    (void)m;
    return element;
}

static void plan_module_bindings(LambdaMvpModule* m, bool context_globals) {
    AstIndex* index = &m->tp->ast_index;
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* binding = &m->bindings[i];
        NameEntry* entry = binding->entry;
        if (!entry || !entry->node || entry->import || entry->import_owner ||
                entry->scope != ((AstScript*)m->tp->ast_root)->global_vars ||
                binding->owner != AST_FUNCTION_ID_INVALID || entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                ((AstDeclaratorNode*)entry->node)->is_type_definition) continue;
        bool used_in_function = false;
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, i, &count);
        for (uint32_t use = 0; use < count; use++)
            used_in_function |= mvp_node_owner(m, uses[use]) != AST_FUNCTION_ID_INVALID;
        if (!context_globals && !used_in_function) continue;
        AstNode* init = ((AstDeclaratorNode*)entry->node)->init;
        if (!context_globals && !entry->is_mutable) {
            Type* declared = ((AstDeclaratorNode*)entry->node)->declared_type;
            if (module_scalar_initializer(m, init) &&
                    (!declared || lambda_static_boundary_relation(mvp_value_contract(m, init),
                        binding->contract, false) == STATIC_BOUNDARY_PROVEN) && (binding->rep == VALUE_REP_F64 ||
                    binding->rep == VALUE_REP_INT_LANE || binding->rep == VALUE_REP_I64)) {
                binding->module_scalar = true;
                continue;
            }
            AstNode* leaf = ast_unwrap_primary_to_leaf(init);
            if ((leaf && leaf->node_type == AST_NODE_PRIMARY && leaf->type &&
                    leaf->type->type_id == LMD_TYPE_STRING) || mvp_module_array_literal_element(m, binding, init)) continue;
        }
        // private composite/computed globals need the same per-evaluator slab
        // as exports; a module need not contain a top-level call (D6.3.3).
        binding->module_managed = true;
    }
}

static bool readonly_literal_binding(LambdaMvpModule* m, MvpBinding* binding) {
    return !binding_reassigned(m, binding, true) && !binding_var_borrow(m, binding);
}

static bool procedure_has_no_value_result(AstFuncNode* function) {
    if (!lambda_type_func_is_proc(function->type)) return false;
    AstNode* body = function->body;
    if (body && body->node_type == AST_NODE_CONTENT) body = ((AstListNode*)body)->item;
    else if (body && body->node_type == AST_NODE_BLOCK) body = ((AstBlockNode*)body)->statements;
    int values = 0, declarations = 0, statements = 0;
    return !ast_proc_block_last_value(body, &values, &declarations, &statements);
}

static bool admitted_assignment(LambdaMvpModule* m, AstAssignStamNode* assignment,
        AstFunctionId owner, bool final_domains) {
    if (assignment->object && assignment->key) {
        if (owner < m->function_count && mvp_edit_model_place(&m->functions[owner], assignment->object)) return true;
        if (assignment->node_type == AST_NODE_MEMBER_ASSIGN_STAM && owner < m->function_count &&
                mvp_method_member(&m->functions[owner], assignment->object, assignment->key)) return true;
        AstCowPath path = {};
        MvpBinding* b = ast_collect_cow_path(&path, assignment->object)
            ? mvp_local_binding(m, path.root) : NULL;
        Type* plain = b ? lambda_type_remove_error_and_null(m->tp->pool, b->contract) : NULL;
        bool carrier = b && (mvp_array_element(plain) || b->rep == VALUE_REP_ITEM ||
            mvp_array_contract(plain) || ast_declared_type_is_map(plain) ||
            (plain && plain->type_id == LMD_TYPE_ELEMENT));
        FnCapture* capture = b && owner < m->function_count
            ? mvp_function_capture(&m->functions[owner], b->entry) : NULL;
        bool live = capture && capture->lambda_live_local;
        bool state = b && b->state_name && owner < m->function_count && m->functions[owner].handler;
        if (!b || (b->owner != owner && !live && !state) || (final_domains && !carrier)) {
            char reason[160];
            snprintf(reason, sizeof(reason), "indexed write place: binding=%s type=%d kind=%d rep=%d owner=%u function=%u",
                b ? b->entry->name->chars : "missing", b ? b->contract->type_id : -1,
                b ? b->contract->kind : -1, b ? b->rep : -1, b ? b->owner : AST_FUNCTION_ID_INVALID, owner);
            return mvp_reject(m, assignment, reason);
        }
    } else if (!assignment->target_entry || assignment->object || assignment->key)
        return mvp_reject(m, assignment, "assignment requires a resolved local binding");
    return true;
}


Type* mvp_parameter_contract(MvpFunction* function, AstNamedNode* parameter) {
    Type* contract = mvp_contract(parameter);
    return function && function->erase_unused_binders && contract && contract->kind == TYPE_KIND_BINDER
        ? ((TypeBinder*)contract)->bound : contract;
}

static bool unused_array_binders(LambdaMvpModule* m, MvpFunction* function) {
    TypeFunc* signature = (TypeFunc*)function->ast->type;
    if (!signature->binder_count || signature->binder_count > LAMBDA_MAX_FUNCTION_ARGS ||
            function->boxed_entry || signature->is_public || signature->is_variadic ||
            function->ast->captures || function->method_owner || mvp_function_expression(function->ast) ||
            function->ast->is_that_predicate || m->tp->compile_against_interp_slab ||
            lambda_type_contract_has_binder(signature->return_contract, true)) return false;
    unsigned sites[LAMBDA_MAX_FUNCTION_ARGS] = {};
    for (AstNamedNode* parameter = function->ast->param; parameter; parameter = (AstNamedNode*)parameter->next) {
        TypeParam* p = lambda_type_param(parameter->type);
        Type* contract = mvp_contract(parameter);
        if (!p || p->is_optional || p->is_var_param || p->binder) return false;
        if (contract && contract->kind == TYPE_KIND_BINDER) {
            TypeBinder* binder = (TypeBinder*)contract;
            if (binder->slot >= signature->binder_count || ++sites[binder->slot] != 1 ||
                    !lambda_array_contract_is_plain(binder->bound) ||
                    lambda_type_contract_has_binder(binder->bound, true) ||
                    ast_body_may_write_entry(function->ast->body, parameter->entry, true, NULL)) return false;
        } else if (lambda_type_contract_has_binder(contract, true)) return false;
    }
    for (unsigned slot = 0; slot < signature->binder_count; slot++) if (sites[slot] != 1) return false;
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId id = 0; id < index->count; id++) {
        AstNode* node = index->nodes[id];
        if (node->node_type == AST_NODE_IDENT) {
            NameEntry* entry = ((AstIdentNode*)node)->entry;
            if (entry && entry->is_binder && ast_scope_function(entry->scope) == function->ast) return false;
        }
        if (!ast_index_node_is_function(node)) continue;
        NameScope* scope = ((AstFuncNode*)node)->vars;
        for (AstBoundTypeUse* use = scope ? scope->bound_type_uses : NULL; use; use = use->next)
            if (ast_scope_function(use->binder->scope) == function->ast) return false;
    }
    // an unobserved, single-site selection on a read-only array has only its
    // written admission to perform. Keep that bound; omit no referenced binder.
    return true;
}

bool lambda_mvp_plan(LambdaMvpModule* m) {
    if (!m || !m->tp->ast_index.graph_published) return false;
    AstIndex* index = &m->tp->ast_index;
    bool context_globals = false;
    for (AstNodeId i = 0; i < index->count; i++)
        context_globals |= mvp_node_owner(m, i) == AST_FUNCTION_ID_INVALID &&
            (index->nodes[i]->node_type == AST_NODE_CALL_EXPR || index->nodes[i]->node_type == AST_NODE_PUB_STAM);
    if (!m->satellite && !compile_script_pattern_definitions(m->tp->pool, m->tp->type_list, ((AstScript*)m->tp->ast_root)->child))
        return mvp_reject(m, m->tp->ast_root, "pattern compilation failed");
    m->nullable_int = lambda_type_nullable_normalized(m->tp->pool, &TYPE_INT);
    m->nullable_bool = lambda_type_nullable_normalized(m->tp->pool, &TYPE_BOOL);
    m->nullable_float = lambda_type_nullable_normalized(m->tp->pool, &TYPE_FLOAT);
    m->text_split_result = mvp_private_array_domain(m, &TYPE_STRING);
    m->function_count = index->function_count;
    uint32_t capacity = m->function_count;
    for (AstNodeId i = 0; i < index->count; i++) if (index->nodes[i]->node_type == AST_NODE_VIEW) {
        capacity++;
        for (AstEventHandler* handler = ((AstViewNode*)index->nodes[i])->handler; handler; handler = handler->next_handler)
            capacity++;
    }
    m->binding_count = index->binding_count;
    m->functions = (MvpFunction*)mem_calloc(capacity, sizeof(MvpFunction), MEM_CAT_TEMP);
    m->bindings = (MvpBinding*)mem_calloc(m->binding_count, sizeof(MvpBinding), MEM_CAT_TEMP);
    if ((m->function_count && !m->functions) || (m->binding_count && !m->bindings))
        return mvp_reject(m, NULL, "compiler allocation failed");
    // Lambda callable definitions need not publish AstFuncNode::entry. Resolve
    // value uses through the indexed identifier's actual definition identity.
    for (AstNodeId id = 0; id < index->count; id++) {
        AstFuncNode* target = mvp_function_value_reference(index, id);
        AstNodeId definition = target ? ast_index_find(index, target) : AST_NODE_ID_INVALID;
        AstFunctionId owner = definition < index->count ? mvp_node_owner(m, definition) : AST_FUNCTION_ID_INVALID;
        if (owner < m->function_count) m->functions[owner].boxed_entry = true;
    }
    for (uint32_t i = 0; i < m->function_count; i++) {
        MvpFunction* f = &m->functions[i];
        f->ast = (AstFuncNode*)index->functions[i].node;
        f->id = i;
        f->parent = index->functions[i].parent;
        for (AstNode* parent = ast_index_parent(index, f->ast); parent;
                parent = ast_index_parent(index, parent)) {
            if (ast_index_node_is_function(parent)) break;
            if (parent->node_type == AST_NODE_OBJECT_TYPE) {
                Type* type = parent->type;
                if (type && type->type_id == LMD_TYPE_TYPE) f->method_owner = (TypeObject*)((TypeType*)type)->type;
                break;
            }
        }
        if (!f->ast || (f->ast->node_type != AST_NODE_FUNC && f->ast->node_type != AST_NODE_PROC &&
                f->ast->node_type != AST_NODE_FUNC_EXPR))
            return mvp_reject(m, f->ast, "callable definition requires function/procedure lowering");
        TypeFunc* signature = (TypeFunc*)f->ast->type;
        NameScope* scope = f->ast->vars;
        for (AstBoundTypeUse* use = scope ? scope->bound_type_uses : NULL; use; use = use->next)
            if (ast_scope_function(use->binder->scope) != f->ast) f->captured_type_count++;
        if (f->captured_type_count) {
            f->captured_type_references = (Type**)pool_calloc(m->tp->pool,
                f->captured_type_count * sizeof(Type*));
            f->captured_type_entries = (NameEntry**)pool_calloc(m->tp->pool,
                f->captured_type_count * sizeof(NameEntry*));
            uint16_t index = 0;
            for (AstBoundTypeUse* use = scope->bound_type_uses; use; use = use->next) {
                if (ast_scope_function(use->binder->scope) == f->ast) continue;
                f->captured_type_references[index] = (Type*)use->reference;
                f->captured_type_entries[index++] = use->binder;
            }
        }
        f->no_value_result = procedure_has_no_value_result(f->ast);
        f->erase_unused_binders = unused_array_binders(m, f);
        // escaping definitions need checked Item admission; closed direct-only
        // definitions keep their native signature (D7.2.2/D4.3.1).
        f->boxed_entry |= (m->tp->compile_against_interp_slab && index->functions[i].parent == AST_FUNCTION_ID_INVALID) ||
            signature->is_public || mvp_function_expression(f->ast) ||
            f->ast->is_that_predicate || f->ast->captures || f->method_owner || signature->is_variadic || mvp_function_binder_count(f);
        if (signature->binder_count > LAMBDA_MAX_FUNCTION_ARGS)
            return mvp_reject(m, f->ast, "binder slots exceed the shared call-frame limit");
        if (ast_function_capture_count(f->ast) > UINT8_MAX)
            return mvp_reject(m, f->ast, "capture count exceeds the shared closure ABI");
        for (FnCapture* cap = f->ast->captures; cap; cap = cap->next) {
            if (mvp_static_type_capture(cap) || mvp_method_field(f, cap->entry)) continue;
            if (cap->entry && cap->entry->is_binder) continue;
            if (cap->is_mutable && !cap->lambda_live_local)
                return mvp_reject(m, f->ast, "legacy mutable snapshots require environment write-back");
            if (!cap->entry || cap->entry->import || cap->entry->import_owner ||
                    ast_index_find_binding_entry(index, cap->entry) >= m->binding_count ||
                    (cap->entry->node && ast_definition_denotes_type(cap->entry->node) &&
                        !mvp_dynamic_type_binding(cap->entry)))
                return mvp_reject(m, f->ast, "capture requires an indexed lexical value binding");
        }
        // shared owned-slot readers carry the registry's collecting effect.
        f->abi.effects.may_gc = f->ast->captures != NULL || f->method_owner != NULL || mvp_function_binder_count(f);
        f->abi.effects.may_reenter = f->abi.effects.may_return_error = mvp_function_binder_count(f) != 0;
        char name[64];
        snprintf(name, sizeof(name), "mvp_%u_%s", i, f->ast->name ? f->ast->name->chars : "fn");
        f->name = name_pool_create_name(m->tp->name_pool, name)->chars;
        if (m->tp->compile_against_interp_slab || signature->is_public || mvp_function_expression(f->ast) || f->ast->is_that_predicate || f->method_owner) {
            StrBuf* exported = strbuf_new_cap(64);
            write_fn_name_ex(exported, f->ast, NULL, "_b");
            f->name = name_pool_create_name(m->tp->name_pool, exported->str)->chars;
            strbuf_free(exported);
        }
        f->abi.param_count = ast_linked_node_count(f->ast->param);
        if (f->abi.param_count > LAMBDA_MAX_FUNCTION_ARGS)
            return mvp_reject(m, f->ast, "too many native operands");
        f->abi.params = (FnParamAnalysis*)mem_calloc(f->abi.param_count, sizeof(FnParamAnalysis), MEM_CAT_TEMP);
        int n = 0;
        for (AstNamedNode* param = f->ast->param; param; param = (AstNamedNode*)param->next) {
            TypeParam* p = lambda_type_param(param->type);
            Type* contract = mvp_parameter_contract(f, param);
            // var transport owns an Item home independently of the local
            // lane, including float and pointer scalars (D5.4.6, D7.3.2).
            if (!admitted_contract(contract, m->tp->pool))
                return mvp_reject(m, param, "parameter contract is outside the MVP subset");
            f->abi.params[n++] = {contract->type_id, f->boxed_entry || (p && p->is_optional) ? VALUE_REP_ITEM : mvp_rep(contract), 0};
        }
        // the shared signature owns normalized outcome contracts; the AST's
        // legacy declared-return field may still denote a type-value carrier.
        Type* result = signature->return_contract ? signature->return_contract : signature->returned;
        if (!admitted_contract(result, m->tp->pool)) {
            Type* base = type_field_unwrap_simple_decl(result);
            char reason[160];
            snprintf(reason, sizeof(reason), "return contract is outside the MVP subset: %s (kind %u)",
                get_type_name(base ? base->type_id : LMD_TYPE_ANY), base ? (unsigned)base->kind : 0);
            return mvp_reject(m, f->ast, reason);
        }
        f->declared_result = result ? result : &TYPE_ANY;
        Type* success = signature->has_explicit_return_contract
            ? lambda_type_remove_error(m->tp->pool, f->declared_result) : signature->returned;
        if (!success) success = f->declared_result;
        // the ordinary error alternative travels in the companion, never in a
        // successful scalar lane (S11.4.2v3, D2.3.5).
        plan_return(f, success == &TYPE_ANY_NO_ERROR || success == &TYPE_NONE ||
            mvp_boundary_proven(f->declared_result, &TYPE_ERROR) ? f->declared_result : success, false);
        // the CLI entry is a procedure; a pure function named main keeps its
        // ordinary parameters/defaults and is never called implicitly.
        if (!f->method_owner && lambda_type_func_is_proc(f->ast->type) &&
                index->functions[i].parent == AST_FUNCTION_ID_INVALID &&
                f->ast->name && strcmp(f->ast->name->chars, "main") == 0) {
            if (f->abi.param_count) return mvp_reject(m, f->ast, "main arguments are not admitted yet");
            m->main = f;
        }
    }
    int template_index = 0;
    for (AstNodeId i = 0; i < index->count; i++) if (index->nodes[i]->node_type == AST_NODE_VIEW) {
        AstViewNode* view = (AstViewNode*)index->nodes[i];
        char name[64]; snprintf(name, sizeof(name), "_view_%d", template_index++);
        const char* reference = view->name ? view->name->chars : name_pool_create_name(m->tp->name_pool, name)->chars;
        if (!plan_template_function(m, view, NULL, name, reference)) return false;
        int handler_index = 0;
        for (AstEventHandler* handler = view->handler; handler; handler = handler->next_handler) {
            char handler_name[80]; snprintf(handler_name, sizeof(handler_name), "%s_h%d", name, handler_index++);
            if (!plan_template_function(m, view, handler, handler_name, reference)) return false;
        }
    }
    for (uint32_t i = 0; i < index->function_count; i++)
        m->functions[i].parent = mvp_node_owner(m,
            ast_index_find(index, ast_index_parent(index, m->functions[i].ast)));
    m->entry.name = "main";
    m->entry.id = AST_FUNCTION_ID_INVALID;
    plan_return(&m->entry, &TYPE_ANY, true);
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        b->entry = index->bindings[i];
        if (m->satellite) {
            // copied parameters and iterators retain their original binding
            // identity; use this image's declaration without retagging it.
            for (AstNodeId n = 0; n < index->count; n++)
                if ((index->nodes[n]->node_type == AST_NODE_PARAM ||
                        index->nodes[n]->node_type == AST_NODE_FOR_CLAUSE ||
                        index->nodes[n]->node_type == AST_NODE_FOR_INDEX ||
                        index->nodes[n]->node_type == AST_NODE_VARIABLE_DECLARATOR) &&
                        index->node_bindings[n] == i) {
                    b->declaration = index->nodes[n]; break;
                }
        }
        AstNode* declaration = b->entry ? mvp_binding_node(b) : NULL;
        AstNodeId node_id = ast_index_find(index, declaration);
        b->owner = node_id < index->count ? mvp_node_owner(m, node_id) : AST_FUNCTION_ID_INVALID;
        b->contract = declaration && declaration->node_type == AST_NODE_PARAM && b->owner < m->function_count
            ? mvp_parameter_contract(&m->functions[b->owner], (AstNamedNode*)declaration) : mvp_contract(declaration);
        b->admission_contract = b->contract;
        if (mvp_dynamic_type_binding(b->entry)) {
            b->contract = b->admission_contract = &TYPE_TYPE;
            b->capture_binding = true;
        }
        if (b->entry && b->entry->admission_may_fail)
            b->contract = lambda_type_union_normalized(m->tp->pool, b->contract, &TYPE_ERROR);
        b->rep = mvp_rep(b->contract);
        if (mvp_array_element(b->admission_contract) && lambda_array_contract_is_plain(b->admission_contract))
            b->array_cert = lambda_array_rep_cert_create(m->tp->pool, b->admission_contract);
        if (b->entry && ast_index_node_is_function(mvp_binding_node(b))) {
            // a definition owns its body, but its snapshot value belongs to
            // the enclosing declaration activation (S9.1.4v2).
            b->owner = b->owner < m->function_count
                ? m->functions[b->owner].parent : AST_FUNCTION_ID_INVALID;
            b->capture_binding = true;
        }
        if (b->entry && mvp_binding_node(b) && mvp_binding_node(b)->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                !((AstDeclaratorNode*)mvp_binding_node(b))->declared_type) {
            // observations may select private lanes, but cannot constrain later
            // field writes or borrow publication (D4.3.1, S12.2.1v3).
            b->admission_contract = &TYPE_ANY;
            if (binding_var_borrow(m, b, NULL, true)) {
                b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM;
            }
            // inferred container contents are observations, not write contracts
            // (S12.2.1). Closed literal tables are refined separately below.
            if (mvp_array_contract(lambda_type_remove_error(m->tp->pool, b->contract))) {
                b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM;
            }
            else if (b->entry->is_mutable &&
                    lambda_type_nonnull_map_contract(normal_domain(m, b->contract))) {
                // reset the successful record arm too: Record | Error must not
                // retain a physical layout proof across open field writes.
                // Typed calls re-establish their prefix (D2.4.3, S12.2.1v3).
                // an open var parameter can replace the owner with another kind;
                // retain Item transport across write-back, not the initial map lane (S12.2.1v3).
                // nullable records need the same reset; their union tag is
                // not MAP and absence must retain Item transport (S11.4.10).
                b->contract = lambda_type_has_proven_error(b->contract) || lambda_type_accepts_null(b->contract) ||
                    binding_var_borrow(m, b, NULL, true)
                    ? &TYPE_ANY : &TYPE_MAP;
                b->rep = mvp_rep(b->contract);
            }
        }
    }
    for (uint32_t i = index->function_count; i < m->function_count; i++) {
        MvpFunction* function = &m->functions[i];
        if (function->handler) continue;
        for (AstStateEntry* state = function->view->state; state; state = state->next_state) {
            MvpBinding* binding = mvp_binding(m, state->entry);
            if (!binding) continue;
            binding->owner = function->id;
            binding->contract = binding->admission_contract = &TYPE_ANY;
            binding->rep = VALUE_REP_ITEM; binding->capture_binding = true;
            binding->state_name = state->name->chars;
        }
    }
    // canonical decomposition targets are synthetic declarators outside the
    // node walk. Their owning declaration supplies the activation (D8.2.4).
    for (AstNodeId i = 0; i < index->count; i++) {
        if (index->nodes[i]->node_type != AST_NODE_DECOMPOSE) continue;
        AstDecomposeNode* node = (AstDecomposeNode*)index->nodes[i];
        for (int n = 0; n < node->name_count; n++) {
            MvpBinding* b = mvp_binding(m, node->entries ? node->entries[n] : NULL);
            if (!b) return mvp_reject(m, node, "decomposition requires canonical target bindings");
            b->owner = mvp_node_owner(m, i);
            // projection inference adds no contract, including for missing
            // members or targets later rebound to a different kind (D4.3.1).
            b->contract = b->admission_contract = &TYPE_ANY;
            b->rep = VALUE_REP_ITEM;
        }
    }
    // source contracts can reference a later indexed parameter binding. Read
    // them only after every binding has its declaration metadata (D8.2.4).
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !mvp_binding_node(b) || mvp_binding_node(b)->node_type != AST_NODE_FOR_CLAUSE) continue;
        AstLoopNode* loop = (AstLoopNode*)mvp_binding_node(b);
        AstNode* source = ast_unwrap_primary_to_leaf(loop->as);
        bool optional = false;
        Type* sequence = lambda_type_nullable_lane_base(
            normal_domain(m, mvp_value_contract(m, source)), &optional);
        bool integer_keys = loop->key_only &&
            (loop->key_filter == LOOP_KEY_INT || mvp_array_contract(sequence));
        bool range = source && source->node_type == AST_NODE_BINARY && ((AstBinaryNode*)source)->op == OPERATOR_TO;
        b->contract = range || integer_keys ? &TYPE_INT : &TYPE_ANY;
        b->rep = mvp_rep(b->contract);
    }
    seed_closed_values(m);
    while (update_closed_values(m, true)) {}
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !mvp_binding_node(b) || mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)mvp_binding_node(b);
        AstNode* source = ast_unwrap_primary_to_leaf(d->init);
        if (d->declared_type || !source ||
                binding_reassigned(m, b, false) || binding_var_borrow(m, b)) continue;
        Type* element = inferred_array_element(m, d->init);
        if (!element) continue;
        // keep a private primitive domain only while the fixed point proves
        // every write. The public inferred type remains unrestricted.
        TypeArray* contract = mvp_private_array_domain(m, element);
        bool append = source->node_type == AST_NODE_ARRAY && !((AstArrayNode*)source)->item;
        if (!append && binding_var_borrow(m, b, (Type*)contract)) continue;
        b->inferred_append = append;
        // a constructor's ordinary Error remains an inferred binding value,
        // even when its successful array has a private primitive certificate.
        bool outcome = source->node_type == AST_NODE_CALL_EXPR && !((AstCallNode*)source)->propagate;
        b->contract = outcome ? lambda_type_union_normalized(m->tp->pool, contract, &TYPE_ERROR) : contract;
        b->rep = mvp_rep(b->contract); b->inferred_array = true;
        b->readonly_literal = source->node_type == AST_NODE_ARRAY && readonly_literal_binding(m, b);
        b->array_cert = lambda_array_rep_cert_create(m->tp->pool, contract);
    }
    // literal arrays are seeded after scalar callers. Revisit unchanged
    // read-only parameters before the ordinary widening pass (D4.3.1).
    while (update_closed_values(m, true)) {}
    // seed producer domains, then widen monotonically as assignment/call effects converge.
    for (AstNodeId i = 0; i < index->count; i++) {
        if (index->nodes[i]->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)index->nodes[i];
        if (d->declared_type || d->is_type_definition) continue;
        MvpBinding* b = mvp_binding(m, d->entry);
        if (b && binding_var_borrow(m, b, NULL, true)) continue;
        // the primitive-array seed already retains constructor Error. Its
        // write audit below owns widening; scalar seeding must not erase it.
        if (b && b->inferred_array) continue;
        Type* source = mvp_value_contract(m, d->init);
        if (!b) continue;
        Type* success = lambda_type_remove_error(m->tp->pool, source);
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(success, &optional);
        bool scalar = private_scalar_kind(base);
        // a shared-AST acknowledgment on an inferred scalar is not a user
        // annotation. Its private observation still widens over all writes
        // and retains Error; it must not start at absorbing `any` (S12.2.1v3).
        if (!b->inferred_array && mvp_array_contract(success) &&
                !binding_reassigned(m, b, true) && !binding_var_borrow(m, b, success)) {
            Type* previous = b->contract;
            b->contract = success;
            NameEntry* active[32];
            // strings cannot be mutated through a retained child. The local
            // write/borrow audits above preserve this array's element domain
            // even when a plain callee keeps its COW snapshot (S9.1.2).
            bool closed = (lambda_array_contract_element(success) == &TYPE_STRING &&
                !b->entry->lambda_shared_local) || binding_storage_closed(m, b, false, active, 0, true);
            b->contract = closed ? source : previous;
            if (closed) {
                // a closed producer observation is not an inferred write
                // contract. Writes/escapes keep the open domain (S12.2.1v3).
                b->rep = mvp_rep(source);
                continue;
            }
        }
        if (!b || !base) continue;
        bool errors = lambda_type_has_proven_error(source);
        bool borrowed = binding_var_borrow(m, b);
        // scalar/error observations have no mutable container layout. The
        // write fixed point widens every assignment before choosing Item ABI;
        // mutable wide values still retain per-read number-home snapshots.
        TypeMap* record = (TypeMap*)lambda_type_nonnull_map_contract(success);
        bool closed_record = record && record->is_trusted_contract &&
            !binding_reassigned(m, b, true, false) && !binding_var_borrow(m, b);
        if (errors && !closed_record &&
                ((!scalar && (b->entry->is_mutable || binding_reassigned(m, b, true))) || borrowed)) {
            b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM;
            continue;
        }
        if (!closed_record && !scalar) continue;
        base = closed_record ? (Type*)record : lambda_canonical_type_for_id(base->type_id);
        b->contract = optional ? mvp_nullable_contract(m, base) : base;
        if (errors) b->contract = lambda_type_union_normalized(m->tp->pool, b->contract, &TYPE_ERROR);
        b->rep = mvp_rep(b->contract);
    }
    // local producers can identify a float result after parameter selection.
    // Exact carrier equality below forbids an inferred int-to-float conversion.
    while (update_closed_values(m, true)) {}
    while (update_closed_values(m)) {}
    plan_module_bindings(m, context_globals);
    // admission consumes the shared index, including bodies that are never called.
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        AstFunctionId owner = mvp_node_owner(m, i);
        if (owner < m->function_count) m->functions[owner].inline_cost++;
        // an explicit return inside any owned block can publish a scalar;
        // nested definitions have their own owner and cannot change this fact.
        if (owner < m->function_count && node->node_type == AST_NODE_RETURN_STAM)
            m->functions[owner].no_value_result = false;
        switch (node->node_type) {
        case AST_SCRIPT: case AST_NODE_CONTENT: case AST_NODE_BLOCK:
        case AST_NODE_VIEW: case AST_NODE_STATE_ENTRY: case AST_NODE_EVENT_HANDLER:
        case AST_NODE_EXPR_STMT: case AST_NODE_PRIMARY:
        case AST_NODE_TYPE: case AST_NODE_SYS_FUNC: case AST_NODE_FUNC: case AST_NODE_PROC: case AST_NODE_FUNC_EXPR:
        case AST_NODE_PARAM: case AST_NODE_RETURN_STAM: case AST_NODE_IF_EXPR:
        case AST_NODE_LET_STAM: case AST_NODE_VAR_STAM: case AST_NODE_DECOMPOSE:
        case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_FOR_CLAUSE: case AST_NODE_FOR_INDEX:
        case AST_NODE_TYPE_STAM: case AST_NODE_MAP_TYPE: case AST_NODE_ARRAY_TYPE:
        case AST_NODE_CONTENT_TYPE: case AST_NODE_ELMT_TYPE: case AST_NODE_FUNC_TYPE:
        case AST_NODE_UNARY_TYPE: case AST_NODE_BINARY_TYPE: case AST_NODE_CONSTRAINED_TYPE:
        case AST_NODE_STRING_PATTERN: case AST_NODE_SYMBOL_PATTERN:
        case AST_NODE_PATTERN_CHAR_CLASS: case AST_NODE_PATTERN_SEQ: case AST_NODE_PATTERN_RANGE: case AST_NODE_LIST_TYPE:
        case AST_NODE_PATH_EXPR: case AST_NODE_PATH_INDEX_EXPR: case AST_NODE_NAVIGATION_EXPR: case AST_NODE_QUERY_EXPR:
        case AST_NODE_RAISE_EXPR: case AST_NODE_CURRENT_ERROR:
        case AST_NODE_CURRENT_ITEM: case AST_NODE_CURRENT_INDEX: case AST_NODE_LAST_INDEX:
        case AST_NODE_IMPORT: case AST_NODE_PUB_STAM:
        case AST_NODE_GROUP_CLAUSE: case AST_NODE_GROUP_KEY: case AST_NODE_ORDER_SPEC:
        case AST_NODE_KEY_EXPR: case AST_NODE_NAMED_ARG: case AST_NODE_MEMBER_EXPR: case AST_NODE_LIST: case AST_NODE_SPREAD:
            break;
        case AST_NODE_IDENT: {
            NameEntry* entry = ((AstIdentNode*)node)->entry;
            Script* owner; int slot;
            if (mvp_interp_binding(m, entry, &owner, &slot) &&
                    !ast_index_node_is_function(entry->node)) break;
            if (entry && entry->node && ast_index_node_is_function(entry->node) &&
                    !mvp_require_function_reference(m, (AstFuncNode*)entry->node, node))
                return mvp_reject(m, node, "callable value requires an indexed or public imported definition");
            break;
        }
        case AST_NODE_PATTERN_ISLAND: {
            AstPatternIslandNode* island = (AstPatternIslandNode*)node;
            if (m->satellite) break; // T0 already compiled the retained pattern.
            if (!compile_runtime_pattern(m->tp->pool, m->tp->type_list,
                    (TypePattern*)island->type, island->pattern, island->is_symbol))
                return mvp_reject(m, node, "inline pattern compilation failed");
            island->pattern_index = ((TypePattern*)island->type)->pattern_index;
            break;
        }
        case AST_NODE_MATCH_EXPR:
            break;
        case AST_NODE_OBJECT_TYPE:
            break;
        case AST_NODE_OBJECT_LITERAL:
            break;
        case AST_NODE_MATCH_ARM:
            if (!require_constraint_predicates(m, ((AstMatchArm*)node)->pattern)) return false;
            break;
        case AST_NODE_PIPE:
        case AST_NODE_JOIN_KEY:
            break;
        case AST_NODE_HANDLER_EXPR: case AST_NODE_HANDLER_STAM:
            if (!require_constraint_predicates(m, ((AstHandlerNode*)node)->filter)) return false;
            break;
        case AST_NODE_VARIABLE_DECLARATOR: {
            AstDeclaratorNode* d = (AstDeclaratorNode*)node;
            bool optional = false;
            Type* base = lambda_type_nullable_lane_base(d->type, &optional);
            // inferred optional lanes join through Item after producer/effect analysis, like inferred unions.
            if (!d->is_type_definition && (!admitted_contract(mvp_contract(d), m->tp->pool) &&
                    (d->declared_type || !(lambda_type_is_union(d->type) || (optional && admitted_contract(base, m->tp->pool))))))
                return mvp_reject(m, node, "binding contract is outside the MVP subset");
            break;
        }
        case AST_NODE_ASSIGN_STAM: case AST_NODE_INDEX_ASSIGN_STAM: case AST_NODE_MEMBER_ASSIGN_STAM: {
            // provisional scalar producers can widen to open containers;
            // validate their final carrier after the inference fixed point.
            if (!admitted_assignment(m, (AstAssignStamNode*)node, owner, false)) return false;
            break;
        }
        case AST_NODE_INDEX_EXPR:
            break;
        case AST_NODE_MAP: break;
        case AST_NODE_ELEMENT: break;
        case AST_NODE_ARRAY: {
            break;
        }
        case AST_NODE_FOR_EXPR: {
            AstForNode* f = (AstForNode*)node;
            AstLoopNode* loop = (AstLoopNode*)f->loop;
            if (!loop) return mvp_reject(m, node, "for requires a source");
            for (AstLoopNode* source = loop; source; source = (AstLoopNode*)source->next) {
                if (!source->as || (source->on && source->join_key_count <= 0))
                    return mvp_reject(m, source, "query source requires resolved equality keys");
                if (MvpBinding* key = mvp_binding(m, source->index_entry)) {
                    key->owner = owner; key->contract = key->admission_contract = &TYPE_ANY;
                    key->rep = VALUE_REP_ITEM; key->iteration_binding = true;
                }
            }
            if (f->group) {
                auto bind = [&](NameEntry* entry, Type* contract) {
                    MvpBinding* binding = mvp_binding(m, entry);
                    if (!binding) return;
                    binding->owner = owner; binding->contract = binding->admission_contract = contract;
                    binding->rep = mvp_rep(contract); binding->iteration_binding = true;
                };
                bind(f->group->entry, &TYPE_ARRAY);
                for (AstGroupKey* key = f->group->keys; key; key = (AstGroupKey*)key->next) bind(key->entry, &TYPE_ANY);
            }
            break;
        }
        case AST_NODE_LOOP:
            if (((AstLoopControlNode*)node)->form != LOOP_FORM_WHILE)
                return mvp_reject(m, node, "only while loops are admitted in the scalar MVP");
            break;
        case AST_NODE_CALL_EXPR:
            if (!admitted_call(m, (AstCallNode*)node, ast_index_call_is_task_launch(index, i))) return false;
            break;
        case AST_NODE_START: {
            AstStartNode* start = (AstStartNode*)node;
            if (!start->call || start->mode != START_MODE_TASK)
                return mvp_reject(m, node, "task launch requires the implemented shared-context mode");
            break;
        }
        case AST_NODE_CRUD_STAM: break;
        case AST_NODE_OPEN_STAM: {
            if (owner < m->function_count) m->functions[owner].owns_open_scopes = true;
            NameEntry* entry = ast_open_alias_entry((AstOpenNode*)node);
            MvpBinding* alias = mvp_binding(m, entry);
            if (alias) {
                // the empty alias declarator is populated by fn_open_begin.
                alias->contract = alias->admission_contract = &TYPE_ANY;
                alias->rep = VALUE_REP_ITEM;
            }
            break;
        }
        case AST_NODE_BINARY: {
            Operator op = ((AstBinaryNode*)node)->op;
            if (op == OPERATOR_IS && !require_constraint_predicates(m, ((AstBinaryNode*)node)->right)) return false;
            if (op == OPERATOR_TO) {
                AstNodeId parent = ast_index_parent_id(index, i);
                while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
                    parent = ast_index_parent_id(index, parent);
                if (parent < index->count && index->nodes[parent]->node_type == AST_NODE_FOR_CLAUSE) break;
            }
            if (!em_boxed_binary_helper(op) &&
                !(op >= OPERATOR_ELEM_EQ && op <= OPERATOR_ELEM_GE) &&
                op != OPERATOR_AND && op != OPERATOR_OR && op != OPERATOR_IS && op != OPERATOR_IS_NAN)
                return mvp_reject(m, node, "binary operator is not admitted yet");
            break;
        }
        case AST_NODE_UNARY: {
            Operator op = ((AstUnaryNode*)node)->op;
            if (op != OPERATOR_NEG && op != OPERATOR_NOT && op != OPERATOR_POS && op != OPERATOR_PROPAGATE &&
                    op != OPERATOR_SPREAD && !em_reference_unary_helper(op))
                return mvp_reject(m, node, "unary operator is not admitted yet");
            break;
        }
        default: return mvp_reject(m, node, interp_node_kind_name(node->node_type));
        }
    }
    for (uint32_t i = 0; i < m->function_count; i++) {
        MvpFunction* f = &m->functions[i];
        int position = 0;
        for (AstNamedNode* param = f->ast->param; param; param = (AstNamedNode*)param->next, position++) {
            MvpBinding* b = mvp_binding(m, param->entry);
            Type* element = mvp_array_element(b->contract);
            b->scalar_snapshot = (element || b->rep == VALUE_REP_ITEM) &&
                mir_confined_scalar_snapshot(index, f->ast, param, position,
                    element ? element->type_id : LMD_TYPE_ANY, scalar_snapshot_key, m);
            // the confined snapshot allocates a precisely rooted header and can fail.
            if (b->scalar_snapshot) f->abi.effects.may_gc = f->abi.effects.may_return_error = true;
        }
    }
    // a direct-call fixed point keeps scalar-only recursion free of GC ceremony.
    bool changed;
    do {
        changed = update_closed_values(m);
        changed |= widen_range_bindings(m);
        for (AstNodeId i = 0; i < index->count; i++) {
            AstNode* node = index->nodes[i];
            AstFunctionId owner = mvp_node_owner(m, i);
            // a function expression allocates in its enclosing activation, not in the body's owner.
            bool named_snapshot = ast_index_node_is_function(node) &&
                ((AstFuncNode*)node)->captures && ((AstFuncNode*)node)->name;
            if ((mvp_function_expression(node) || named_snapshot) && owner < m->function_count)
                owner = m->functions[owner].parent;
            MvpFunction* f = owner < m->function_count ? &m->functions[owner] : &m->entry;
            bool collects = false, fails = false, reenters = false;
            bool implicit_collects = false, implicit_fails = false;
            if (f->ast && node == f->ast->body) {
                AstNode* result = ast_unwrap_primary_to_leaf(node);
                bool implicit = true;
                Type* incoming = NULL;
                if (result && (result->node_type == AST_NODE_CONTENT || result->node_type == AST_NODE_BLOCK)) {
                    AstNode* first = result->node_type == AST_NODE_CONTENT
                        ? ((AstListNode*)result)->item : ((AstBlockNode*)result)->statements;
                    int values = 0, declarations = 0, statements = 0;
                    AstNode* terminal = NULL;
                    bool procedural = lambda_type_func_is_proc(f->ast->type);
                    result = ast_block_last_value(first, procedural,
                        &values, &declarations, &statements, &terminal);
                    implicit = !terminal || terminal->node_type != AST_NODE_RETURN_STAM;
                    implicit_collects = !procedural && values > 1;
                    incoming = !procedural && values > 1 ? &TYPE_ANY :
                        result ? mvp_value_contract(m, result) : &TYPE_NULL;
                } else incoming = mvp_value_contract(m, result);
                // implicit results cross the same checked return boundary as
                // RETURN_STAM; an omitted effect cannot be repaired in lowering.
                if (implicit) implicit_fails = !mvp_admission_proven(m, incoming, f->result_contract);
            }
            if (node->node_type == AST_NODE_PARAM) {
                MvpBinding* b = mvp_binding(m, ((AstNamedNode*)node)->entry);
                TypeParam* p = lambda_type_param(node->type);
                // defer open-entry effects until private lanes are known;
                // widening reinstates the checked Item receiver's effects.
                collects = fails = f->boxed_entry || (p && p->is_optional) ||
                    (b && b->rep == VALUE_REP_ITEM);
            }
            if (node->node_type == AST_NODE_IDENT) {
                NameEntry* entry = ((AstIdentNode*)node)->entry;
                MvpBinding* binding = mvp_binding(m, entry);
                collects = mvp_function_value_reference(index, i) != NULL;
                collects |= entry && entry->lambda_shared_local;
                // context-slab reads and their return admission can collect;
                // determine this before the storage descriptors are emitted.
                if (entry && entry->node && entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                        !((AstDeclaratorNode*)entry->node)->is_type_definition &&
                        (entry->import || entry->import_owner ||
                         (binding && binding->module_managed)))
                    collects = fails = true;
            }
            if ((node->node_type == AST_NODE_PRIMARY && node->type &&
                    (node->type->type_id == LMD_TYPE_DTIME || node->type->type_id == LMD_TYPE_COMPLEX)) ||
                    node->node_type == AST_NODE_ELEMENT || node->node_type == AST_NODE_OBJECT_LITERAL ||
                    node->node_type == AST_NODE_OBJECT_TYPE ||
                    node->node_type == AST_NODE_PATH_EXPR ||
                    mvp_function_expression(node) || named_snapshot ||
                    node->node_type == AST_NODE_ARRAY || node->node_type == AST_NODE_LIST ||
                    node->node_type == AST_NODE_SPREAD || node->node_type == AST_NODE_DECOMPOSE ||
                    node->node_type == AST_NODE_MATCH_EXPR ||
                    node->node_type == AST_NODE_MEMBER_ASSIGN_STAM || node->node_type == AST_NODE_INDEX_ASSIGN_STAM)
                collects = fails = true;
            if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
                MvpBinding* binding = mvp_binding(m, ((AstDeclaratorNode*)node)->entry);
                if (binding && binding->module_managed) collects = fails = true;
                if (binding && binding->entry->lambda_shared_local) collects = true;
            }
            if (node->node_type == AST_NODE_SYS_FUNC && !mvp_direct_callee_node(index, i)) collects = true;
            // raise can construct and stamp a rich Error from any value;
            // its helper owns the same collecting effect as error() (S7.4.6).
            if (node->node_type == AST_NODE_RAISE_EXPR) collects = fails = true;
            if (node->node_type == AST_NODE_START || node->node_type == AST_NODE_OPEN_STAM ||
                    node->node_type == AST_NODE_CRUD_STAM ||
                    (node->node_type == AST_NODE_MEMBER_EXPR && ast_member_wildcard((AstFieldNode*)node)) ||
                    node->node_type == AST_NODE_QUERY_EXPR || node->node_type == AST_NODE_PATH_INDEX_EXPR ||
                    node->node_type == AST_NODE_NAVIGATION_EXPR || node->node_type == AST_NODE_LAST_INDEX ||
                    node->node_type == AST_NODE_PIPE) {
                // query admission can call predicates; computed keys can
                // resolve through a provider. Keep both effects conservative.
                collects = fails = reenters = true;
            }
            if ((node->node_type == AST_NODE_HANDLER_EXPR || node->node_type == AST_NODE_HANDLER_STAM) &&
                    ((AstHandlerNode*)node)->is_statement && f->ast && lambda_type_func_is_proc(f->ast->type))
                collects = fails = reenters = true;
            if (node->node_type == AST_SCRIPT || node->node_type == AST_NODE_CONTENT ||
                    node->node_type == AST_NODE_BLOCK) {
                AstNode* item = node->node_type == AST_SCRIPT ? ((AstScript*)node)->child :
                    node->node_type == AST_NODE_CONTENT ? ((AstListNode*)node)->item : ((AstBlockNode*)node)->statements;
                if (!f->ast || !lambda_type_func_is_proc(f->ast->type)) {
                    unsigned values = 0;
                    for (; item; item = item->next)
                        if (!is_declaration_node(item) && !ast_block_item_is_statement(item, false)) values++;
                    if (values > 1) collects = reenters = fails = true;
                }
            }
            if (node->node_type == AST_NODE_BINARY && ((AstBinaryNode*)node)->op == OPERATOR_IS) {
                collects = true;
                fails = !mvp_match_type(((AstBinaryNode*)node)->right);
            }
            // predicate calls are implicit AST edges, including recursive
            // tests and imported declaring scopes (S11.4.11).
            if (ast_constrained_type(node)) collects = reenters = true;
            if (node->node_type == AST_NODE_MAP) {
                collects = true;
                // computed keys/spreads use checked builders even when the
                // successful map domain is closed (S7.9.3).
                fails = ((AstMapNode*)node)->has_computed_key ||
                    (mvp_contextual_map_contract(m, node) && lambda_type_accepts_error(mvp_value_contract(m, node)));
            }
            if (node->node_type == AST_NODE_MEMBER_EXPR) {
                collects = true;
                fails = mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
            }
            if (node->node_type == AST_NODE_INDEX_EXPR) {
                // a primitive integer-key read returns its member or absence;
                // only a consuming typed boundary can reject absence (S7.1.1v3).
                AstFieldNode* read = (AstFieldNode*)node;
                Type* owner = mvp_value_contract(m, read->object);
                Type* key = mvp_value_contract(m, read->field);
                collects = read->field->next || !mvp_array_element(owner) ||
                    lambda_type_accepts_error(owner) || mvp_rep(key) != VALUE_REP_INT_LANE;
                fails = mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
            }
            if (node->node_type == AST_NODE_FOR_EXPR) {
                AstLoopNode* loop = (AstLoopNode*)((AstForNode*)node)->loop;
                if (loop->key_only) collects = true;
                else {
                    AstNode* source = ast_unwrap_primary_to_leaf(loop->as);
                    bool range_source = source && source->node_type == AST_NODE_BINARY && ((AstBinaryNode*)source)->op == OPERATOR_TO;
                    AstBinaryNode* range = range_source ? (AstBinaryNode*)source : NULL;
                    AstForNode* query = (AstForNode*)node;
                    collects = fails = loop->next || !range || !query->discard_result || query->group || query->order ||
                        query->let_clause || query->limit || query->offset ||
                        !mvp_int_range(m, range->left).finite || !mvp_int_range(m, range->right).finite;
                }
            }
            if (node->node_type == AST_NODE_CALL_EXPR && !ast_index_call_is_task_launch(index, i)) {
                AstCallNode* call = (AstCallNode*)node;
                MvpFunction* callee = mvp_function(m, ast_direct_call_function(call));
                if (callee) {
                    bool rest = mvp_variadic_function(callee) && mvp_call_argument_count(call) > callee->abi.param_count;
                    collects = callee->abi.effects.may_gc || callee->ast->captures || rest;
                    fails = callee->abi.effects.may_return_error;
                    reenters = callee->abi.effects.may_reenter || callee->ast->captures || rest;
                    AstNamedNode* param = callee->ast->param;
                    for (int a = 0; a < mvp_call_argument_count(call) && param; a++, param = (AstNamedNode*)param->next)
                        fails |= !mvp_admission_proven(m, mvp_value_contract(m, mvp_call_argument(call, a)), mvp_contract(param));
                }
                else {
                    AstNode* target = ast_unwrap_primary_to_leaf(call->function);
                    if (!target || target->node_type != AST_NODE_SYS_FUNC) {
                        collects = fails = reenters = true;
                    } else {
                        SysFuncInfo* info = ((AstSysFuncNode*)target)->fn_info;
                        JitImportMetadata metadata = {};
                        TypeId input = mvp_value_contract(m, mvp_call_argument(call, 0))->type_id;
                        bool native_math = mvp_native_math(info, input);
                        bool native_cast = (info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) &&
                            (input == LMD_TYPE_INT || input == LMD_TYPE_FLOAT);
                        bool native_length = info->fn == SYSFUNC_LEN &&
                            mvp_value_contract(m, node)->type_id == LMD_TYPE_INT &&
                            !lambda_type_accepts_error(mvp_value_contract(m, mvp_call_argument(call, 0)));
                        bool native_ord = info->fn == SYSFUNC_ORD && mvp_string_index(m, mvp_call_argument(call, 0));
                        bool string_identity = info->fn == SYSFUNC_STRING && input == LMD_TYPE_STRING;
                        const char* import_name = native_math ? info->native_c_name : info->c_func_name;
                        MvpBinding* receiver = info->fn == SYSPROC_PUSH ? mvp_local_binding(m, mvp_call_argument(call, 0)) : NULL;
                        // match the emitted open-owner helper. Typed/projected
                        // pushes retain callback effects from element admission.
                        if (receiver && !lambda_array_contract_element(receiver->admission_contract))
                            import_name = "pn_push_cow";
                        bool known = jit_import_get_metadata(import_name, &metadata);
                        collects = !known || metadata.gc_effect != JIT_EFFECT_NO_GC;
                        reenters = !known || metadata.reentry_effect != JIT_REENTRY_NO;
                        // plain builtin numeric lanes have no element constraint
                        // callback, even though the generic certified push can.
                        if (receiver && mvp_array_element(receiver->admission_contract) &&
                                lambda_array_contract_is_plain(receiver->admission_contract)) reenters = false;
                        bool optional = false;
                        Type* base = lambda_type_nullable_lane_base(mvp_value_contract(m, mvp_call_argument(call, 0)), &optional);
                        bool native_int_math = base == &TYPE_INT && sysfunc_native_math_type_preserving(info->fn, NULL);
                        // these admitted operations emit scalar/header reads or
                        // audited noncollecting text helpers, not boxed dispatch.
                        if (native_cast || native_int_math || native_length || native_ord || string_identity)
                            collects = reenters = false;
                        fails = mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
                        if (info->fn == SYSFUNC_LEN)
                            fails |= lambda_type_accepts_error(mvp_value_contract(m, node));
                        fails |= call->propagate && call->propagated_error_type &&
                            lambda_type_accepts_error(call->propagated_error_type);
                    }
                }
            } else if (node->node_type == AST_NODE_BINARY &&
                    ((AstBinaryNode*)node)->op == OPERATOR_JOIN) {
                collects = true; fails = mvp_value_contract(m, node) == &TYPE_ANY;
            }
            // unresolved boundary admission may allocate an error even on a native function.
            if (node->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)node)->fn_colour_guard)
                collects = fails = true;
            if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
                AstDeclaratorNode* d = (AstDeclaratorNode*)node;
                MvpBinding* b = mvp_binding(m, d->entry);
                Type* source = mvp_value_contract(m, d->init);
                changed |= widen_inferred_binding(m, b, source);
                fails |= d->declared_type && !mvp_admission_proven(m, source, d->declared_type);
            }
            if (node->node_type == AST_NODE_ASSIGN_STAM) {
                AstAssignStamNode* a = (AstAssignStamNode*)node;
                MvpBinding* b = mvp_binding(m, a->target_entry);
                Type* source = mvp_value_contract(m, a->value);
                changed |= widen_inferred_binding(m, b, source);
                fails |= !b || !mvp_admission_proven(m, source, b->contract);
            }
            if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
                AstAssignStamNode* a = (AstAssignStamNode*)node;
                MvpBinding* b = mvp_local_binding(m, a->object);
                if (b && b->inferred_array && b->contract != &TYPE_ANY &&
                        !mvp_boundary_proven(mvp_value_contract(m, a->value), mvp_array_element(normal_domain(m, b->contract)))) {
                    b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM; changed = true;
                }
            }
            if (node->node_type == AST_NODE_RETURN_STAM) {
                AstNode* v = ((AstReturnNode*)node)->value;
                fails |= !mvp_admission_proven(m, mvp_value_contract(m, v), f->result_contract);
            }
            if ((node->node_type == AST_NODE_BINARY && ((AstBinaryNode*)node)->op != OPERATOR_TO) ||
                    node->node_type == AST_NODE_UNARY)
                // boxed comparison unions retain Error even with a closed
                // successful bool domain (S7.9.3, D6.3.2).
                fails |= lambda_type_accepts_error(mvp_value_contract(m, node));
            if (node->node_type == AST_NODE_UNARY && ((AstUnaryNode*)node)->op == OPERATOR_PROPAGATE)
                fails |= lambda_type_accepts_error(mvp_value_contract(m, ((AstUnaryNode*)node)->operand));
            if (node->node_type == AST_NODE_UNARY) {
                const char* helper = em_reference_unary_helper(((AstUnaryNode*)node)->op);
                if (helper) {
                    JitImportMetadata metadata = {};
                    bool known = jit_import_get_metadata(helper, &metadata);
                    collects |= !known || metadata.gc_effect != JIT_EFFECT_NO_GC;
                    reenters |= !known || metadata.reentry_effect != JIT_REENTRY_NO;
                }
            }
            // the body's node-specific producer effects cannot replace its
            // independent return admission (for example, a record literal).
            fails |= implicit_fails;
            collects |= fails || implicit_collects;
            if (node->node_type != AST_NODE_PARAM && collects) f->body_may_gc = true;
            if (collects && !f->abi.effects.may_gc) { f->abi.effects.may_gc = true; changed = true; }
            if (fails && !f->abi.effects.may_return_error) { f->abi.effects.may_return_error = true; changed = true; }
            if (reenters && !f->abi.effects.may_reenter) { f->abi.effects.may_reenter = true; changed = true; }
        }
    } while (changed);
    plan_shape_candidates(m);
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNodeType kind = index->nodes[i]->node_type;
        if (kind == AST_NODE_ASSIGN_STAM || kind == AST_NODE_INDEX_ASSIGN_STAM || kind == AST_NODE_MEMBER_ASSIGN_STAM) {
            if (!admitted_assignment(m, (AstAssignStamNode*)index->nodes[i], mvp_node_owner(m, i), true)) return false;
            continue;
        }
        if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR || ast_index_call_is_task_launch(index, i)) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[i];
        MvpFunction* callee = mvp_function(m, ast_direct_call_function(call));
        if (callee && !admitted_var_arguments(m, call, callee)) return false;
    }
    // a discharged typed constructor and a closed mutation graph cannot
    // publish an error into this owner. Keep that private clean domain while
    // leaving the shared AST's conservative acknowledgment facts unchanged.
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !b->entry->admission_may_fail || b->owner >= m->function_count ||
                !mvp_binding_node(b) || mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)mvp_binding_node(b);
        if (!d->declared_type || (!mvp_array_element(b->admission_contract) &&
                !mvp_record_contract(b->admission_contract, true)) ||
                !mvp_boundary_proven(mvp_value_contract(m, d->init), b->admission_contract)) continue;
        Type* previous = b->contract;
        b->contract = b->admission_contract;
        NameEntry* active[32];
        if (binding_storage_closed(m, b, true, active, 0, true)) b->rep = mvp_rep(b->contract);
        else b->contract = previous;
    }
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !mvp_binding_node(b) || mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                binding_reassigned(m, b, true)) continue;
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)mvp_binding_node(b);
        AstNode* source = ast_unwrap_primary_to_leaf(declaration->init);
        MvpFunction* producer = source && source->node_type == AST_NODE_CALL_EXPR
            ? mvp_function(m, ast_direct_call_function((AstCallNode*)source)) : NULL;
        Type* observation = b->contract != &TYPE_ANY
            ? b->contract
            : producer ? producer->result_contract : mvp_value_contract(m, source);
        // an unchanged literal can retain the exact arm it constructed. The
        // generic inferred-map reset still applies to every writable owner.
        TypeMap* constructed = (b->contract == &TYPE_MAP || b->contract == &TYPE_ANY) &&
            source && source->node_type == AST_NODE_MAP ? mvp_contextual_map_contract(m, source) : NULL;
        if (constructed) observation = lambda_type_accepts_error(mvp_value_contract(m, source))
            ? lambda_type_union_normalized(m->tp->pool, constructed, &TYPE_ERROR) : constructed;
        Type* success = lambda_type_remove_error(m->tp->pool, observation);
        if (binding_var_borrow(m, b, success)) continue;
        if (constructed && b->contract == &TYPE_MAP) {
            b->contract = observation;
            b->rep = mvp_rep(observation);
        }
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(success, &optional);
        // this is a successful-producer fact, not a new inferred write contract.
        // The unchanged local may still hold Error; consumers must classify it.
        if (base && (mvp_array_contract(base) || lambda_type_nonnull_map_contract(base) ||
                base->type_id == LMD_TYPE_FLOAT || base->type_id == LMD_TYPE_INT ||
                base->type_id == LMD_TYPE_BOOL || base->type_id == LMD_TYPE_DECIMAL ||
                base->type_id == LMD_TYPE_STRING || base->type_id == LMD_TYPE_SYMBOL ||
                base->type_id == LMD_TYPE_NUM_SIZED))
            b->producer_success_contract = success;
    }
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        AstNode* definition = mvp_binding_node(b);
        if (!b->entry || b->entry->has_type_annotation || b->entry->lambda_shared_local ||
                b->module_slot || b->module_ref || !definition || definition->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                !binding_reassigned(m, b, false) || binding_var_borrow(m, b)) continue;
        Type* success = normal_domain(m, mvp_value_contract(m, ((AstDeclaratorNode*)definition)->init));
        if (!mvp_array_contract(success)) continue;
        bool valid = true;
        for (AstNodeId id = 0; id < index->count && valid; id++) {
            AstNode* node = index->nodes[id];
            if (node->node_type == AST_NODE_ASSIGN_STAM && ((AstAssignStamNode*)node)->target_entry == b->entry) {
                Type* next = normal_domain(m, mvp_value_contract(m, ((AstAssignStamNode*)node)->value));
                valid = next == &TYPE_NONE || mvp_admission_proven(m, next, success);
            } else if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM || node->node_type == AST_NODE_MEMBER_ASSIGN_STAM)
                valid = mvp_place_root(m, ((AstAssignStamNode*)node)->object) != b;
            else if (node->node_type == AST_NODE_CALL_EXPR) {
                AstCallNode* call = (AstCallNode*)node;
                AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
                if (callee && callee->node_type == AST_NODE_SYS_FUNC && ((AstSysFuncNode*)callee)->fn_info->is_proc &&
                        mvp_place_root(m, mvp_call_argument(call, 0)) == b) valid = false;
            }
        }
        // all replacements preserve the same successful array domain. This
        // does not constrain unannotated stores or discard Error (D4.3.1).
        if (valid) b->producer_success_contract = success;
    }
    if (m->main) {
        m->entry.abi.effects.may_gc |= m->main->abi.effects.may_gc;
        m->entry.abi.effects.may_reenter |= m->main->abi.effects.may_reenter;
        m->entry.abi.effects.may_return_error |= m->main->abi.effects.may_return_error;
    }
    // publish one final return ABI after recursive error effects have converged (D7.4.2).
    for (uint32_t i = 0; i < m->function_count; i++) {
        MvpFunction* f = &m->functions[i];
        int position = 0;
        for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, position++) {
            MvpBinding* b = mvp_binding(m, p->entry);
            if (b && b->closed_parameter)
                f->abi.params[position] = {b->contract->type_id, b->rep, 0};
        }
        plan_return(f, f->result_contract, false);
    }
    plan_integer_ranges(m);
    plan_float_parameters(m);
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        b->stable_scalar_home = binding_scalar_home_stable(m, b);
        if (lambda_type_contract_has_binder(b->contract, true)) {
            // the annotation remains the receiving contract. Observers see
            // boxed runtime values, including Error when the selection admits it.
            Type* bound = mvp_bound_array_contract(b->admission_contract);
            bool parameter = b->entry && mvp_binding_node(b) && mvp_binding_node(b)->node_type == AST_NODE_PARAM;
            b->contract = bound && parameter ? bound : &TYPE_ANY;
            if (bound && parameter && (binding_reassigned(m, b, false) || binding_var_borrow(m, b)))
                b->contract = lambda_type_union_normalized(m->tp->pool, bound, &TYPE_ERROR);
            b->rep = VALUE_REP_ITEM;
            b->closed_parameter = b->scalar_snapshot = false;
            // an admitted binder's fixed array bound describes successful
            // reads, without selecting its invocation-specific type (D4.1.4).
            b->producer_success_contract = bound;
            b->array_cert = NULL;
            b->range.finite = b->constant_float = b->constant_ready =
                b->readonly_literal = b->string_buffer_owned = false;
        }
        if (!b->entry || !b->entry->lambda_shared_local) continue;
        // shared homes publish ordinary Items, including nullable/error
        // outcomes; the source annotation still owns admission (S9.1.4v2).
        b->rep = VALUE_REP_ITEM;
        b->range.finite = b->constant_float = b->readonly_literal = b->string_buffer_owned = false;
    }
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        AstNode* object = NULL; AstNode* key = NULL;
        if (node->node_type == AST_NODE_INDEX_EXPR) {
            object = ((AstFieldNode*)node)->object; key = ((AstFieldNode*)node)->field;
            if (key && key->next) require_tensor(mvp_place_root(m, object));
        } else if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            object = ((AstAssignStamNode*)node)->object; key = ((AstAssignStamNode*)node)->key;
            if (key && key->next) require_tensor(mvp_place_root(m, object));
            AstCowPath path = {};
            if (ast_collect_cow_path(&path, object) && path.count >= 1) {
                bool indices = true;
                for (int k = 0; k < path.count; k++) indices &= !path.is_member[k];
                MvpBinding* root = indices ? mvp_local_binding(m, path.root) : NULL;
                require_tensor(root);
            }
        }
        MvpBinding* b = mvp_local_binding(m, object);
        if (!b || (!mvp_array_element(b->contract) && b->rep != VALUE_REP_ITEM)) continue;
        b->array_accessed = true;
        MvpIntRange range = mvp_int_range(m, key);
        if (range.finite && range.lower >= 0 && range.upper < INT53_MAX && range.upper + 1 > b->array_extent)
            b->array_extent = range.upper + 1;
    }
    // extents describe required guards, not array length proofs. Propagate
    // accessor requirements so closed callers can guard forwarded owners too.
    do {
        changed = false;
        // tensor observations follow immutable aliases back to their producer.
        for (uint32_t i = 0; i < m->binding_count; i++) {
            MvpBinding* binding = &m->bindings[i];
            AstNode* definition = binding->entry ? mvp_binding_node(binding) : NULL;
            if (binding->tensor_required && definition && definition->node_type == AST_NODE_VARIABLE_DECLARATOR)
                changed |= require_tensor(mvp_place_root(m, ((AstDeclaratorNode*)definition)->init));
        }
        for (AstNodeId i = 0; i < index->count; i++) {
            if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
            AstCallNode* call = (AstCallNode*)index->nodes[i];
            AstFuncNode* target = ast_direct_call_function(call);
            AstNamedNode* p = target ? target->param : NULL;
            for (int a = 0; a < mvp_call_argument_count(call) && p; a++, p = (AstNamedNode*)p->next) {
                AstNode* actual = mvp_call_argument(call, a);
                MvpBinding* source = mvp_local_binding(m, actual), *formal = mvp_binding(m, p->entry);
                if (formal && formal->tensor_required)
                    changed |= require_tensor(mvp_place_root(m, actual));
                if (source && formal && mvp_array_element(source->contract) && formal->array_extent > source->array_extent) {
                    source->array_extent = formal->array_extent; changed = true;
                }
            }
        }
    } while (changed);
    for (AstNodeId i = 0; i < index->count; i++) {
        MvpStringAppendPlan append = {};
        if (mvp_string_append_plan(m, index->nodes[i], &append)) append.owner->string_buffer_owned = true;
        if (index->nodes[i]->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call = (AstCallNode*)index->nodes[i];
            MvpFunction* function = mvp_function(m, ast_direct_call_function(call));
            if (function && mvp_node_owner(m, i) == function->id && !function->ast->captures) {
                AstNamedNode* last = function->ast->param;
                while (last && last->next) last = (AstNamedNode*)last->next;
                MvpBinding* binding = last ? mvp_binding(m, last->entry) : NULL;
                AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS]; int count = 0;
                bool append = binding && binding->contract == &TYPE_STRING &&
                    ast_collect_join_leaves(mvp_call_argument(call, function->abi.param_count - 1), leaves, &count) &&
                    count > 1 && mvp_local_binding(m, leaves[0]) == binding;
                for (int j = 1; append && j < count; j++)
                    append = mir_join_rhs_does_not_observe_owner(leaves[j], binding->entry);
                // ordinary reads freeze a tail accumulator before a retained
                // observer escapes; only its final append borrows the buffer.
                if (append) binding->string_buffer_owned = true;
            }
        }
        if (index->nodes[i]->node_type == AST_NODE_ASSIGN_STAM) {
            AstAssignStamNode* assignment = (AstAssignStamNode*)index->nodes[i];
            MvpBinding* binding = mvp_binding(m, assignment->target_entry);
            if (binding && ast_is_owned_string_append(index->nodes[i], binding->entry))
                binding->string_buffer_owned = true;
        }
        if (index->nodes[i]->node_type == AST_NODE_MEMBER_EXPR) {
            MvpBinding* receiver = mvp_local_binding(m, ((AstFieldNode*)index->nodes[i])->object);
            if (receiver) receiver->record_accessed = true;
        }
        if (index->nodes[i]->node_type != AST_NODE_MEMBER_ASSIGN_STAM) continue;
        MvpBinding* binding = mvp_local_binding(m, ((AstAssignStamNode*)index->nodes[i])->object);
        if (binding) binding->record_written = true;
    }
    // fault injection reaches the same emitted-effect verifier as ordinary
    // plans; it must reject before any entry is linked or published (D6.3.2).
    const char* no_gc_claim = getenv("LAMBDA_TEST_GC_PLAN_CLAIM");
    for (uint32_t i = 0; no_gc_claim && no_gc_claim[0] && i < m->function_count; i++) {
        MvpFunction* function = &m->functions[i];
        if (function->ast->name && strcmp(function->ast->name->chars, no_gc_claim) == 0)
            function->abi.effects.may_gc = false;
    }
    return !m->failed;
}

bool lambda_mvp_lower(LambdaMvpModule* m) {
    if (!m || m->failed) return false;
    m->module = MIR_new_module(m->ctx, m->satellite ? m->names->module : "lambda_mvp");
    m->imports = em_import_cache_new(32);
    m->consts = MIR_new_bss(m->ctx, m->names->consts_bss, sizeof(void*));
    m->layout = MIR_new_bss(m->ctx, m->names->layout_bss, sizeof(LambdaModuleLayout));
    MIR_new_bss(m->ctx, m->names->type_list_bss, sizeof(void*));
    for (uint32_t i = 0; i < m->binding_count; i++) if (!m->satellite && m->bindings[i].module_scalar) {
        char name[40]; snprintf(name, sizeof(name), "mvp_module_scalar_%u", i);
        m->bindings[i].module_slot = MIR_new_bss(m->ctx, name, sizeof(uint64_t));
    }
    for (uint32_t i = 0; i < m->binding_count; i++) if (!m->satellite && m->bindings[i].module_managed) {
        StrBuf* name = strbuf_new_cap(64);
        strbuf_append_str(name, "_gvar_"); strbuf_append_str(name, m->bindings[i].entry->name->chars);
        m->bindings[i].module_ref = MIR_new_bss(m->ctx, name->str, sizeof(LambdaModuleVarRef));
        strbuf_free(name);
    }
    for (uint32_t i = 0; i < m->function_count; i++)
        m->functions[i].target = MIR_new_forward(m->ctx, m->functions[i].name);
    for (int i = 0; m->imported_functions && i < m->imported_functions->length; i++) {
        MvpFunction* function = (MvpFunction*)m->imported_functions->data[i];
        if (function->js_export_name) {
            function->js_export_ref = MIR_new_import(m->ctx, function->js_export_name);
            function->target = MIR_new_forward(m->ctx, function->name);
        } else if (!function->interp_binding) function->target = MIR_new_import(m->ctx, function->name);
    }
    for (uint32_t i = 0; i < m->function_count && !m->failed; i++)
        mvp_emit_function(m, &m->functions[i]);
    for (int i = 0; m->imported_functions && i < m->imported_functions->length && !m->failed; i++) {
        MvpFunction* function = (MvpFunction*)m->imported_functions->data[i];
        if (function->js_export_name) mvp_emit_function(m, function);
    }
    if (!m->failed && !m->satellite) mvp_emit_function(m, &m->entry);
    if (!m->failed && m->property_keys && m->property_keys->length) {
        uint32_t bytes = 0;
        if (!em_module_property_key_image_size(m->property_keys, &bytes))
            mvp_reject(m, NULL, "module property key image is too large");
        else MIR_new_bss(m->ctx, m->names->property_specs_bss, bytes);
    }
    MIR_finish_module(m->ctx);
    return !m->failed;
}

bool lambda_mvp_finalize(LambdaMvpModule* m, LambdaMvpStats* stats, ArrayList** property_keys) {
    if (!m || m->failed || !m->module) return false;
    mir_dump_finalized(m->ctx, host_path_dir(HOST_PATH_TEMP, "mir_dump.txt"), false);
    MIR_load_module(m->ctx, m->module);
    if (stats) *stats = m->stats;
    if (property_keys) { *property_keys = m->property_keys; m->property_keys = NULL; }
    return true;
}

void lambda_mvp_destroy(LambdaMvpModule* m) {
    if (!m) return;
    for (uint32_t i = 0; m->functions && i < m->function_count; i++) mem_free(m->functions[i].abi.params);
    mem_free(m->functions);
    for (int i = 0; m->imported_functions && i < m->imported_functions->length; i++) {
        MvpFunction* function = (MvpFunction*)m->imported_functions->data[i];
        mem_free(function->abi.params); mem_free(function);
    }
    if (m->imported_functions) arraylist_free(m->imported_functions);
    if (m->property_keys) arraylist_free(m->property_keys);
    mem_free(m->bindings);
    if (m->imports) hashmap_free(m->imports);
    mem_free(m);
}

bool lambda_mvp_compile_satellite(Transpiler* tp, MIR_context_t ctx,
        const MirModuleNames* names, MirModuleArtifacts* artifacts, ArrayList** property_keys) {
    // the cluster root is private; its retained bodies and binding identities
    // stay shared and read-only. No module initializer runs on promotion.
    LangProfile profile = *tp->profile;
    // parser-built indexes include Lambda-only statement edges. A fresh
    // satellite index must visit them too, especially copied for clauses.
    profile.visit_ext_children = [](AstNode* node, AstChildVisitor visit, void* context) {
        struct Children { AstNode* parent; AstChildVisitor visit; void* context; } children = {node, visit, context};
        interp_visit_children(node, [](AstNode* child, void* context) {
            Children* children = (Children*)context;
            children->visit(child, children->parent, children->context);
        }, &children);
    };
    if (!ast_index_build_profile(&tp->ast_index, tp->ast_root, &profile)) return false;
    LambdaMvpModule* m = lambda_mvp_create(tp, ctx);
    if (!m) { ast_index_destroy(&tp->ast_index); return false; }
    MirModuleNames selected = *names;
    StrBuf* module_name = strbuf_new_cap(64);
    strbuf_append_str(module_name, "mvp_"); strbuf_append_str(module_name, names->module);
    selected.module = name_pool_create_name(tp->name_pool, module_name->str)->chars;
    strbuf_free(module_name);
    m->satellite = true; m->names = &selected;
    bool ok = lambda_mvp_plan(m) && lambda_mvp_lower(m) && lambda_mvp_finalize(m, NULL, property_keys);
    if (ok) {
        artifacts->consts_bss = find_import(ctx, names->consts_bss);
        artifacts->layout_bss = find_import(ctx, names->layout_bss);
        artifacts->type_list_bss = find_import(ctx, names->type_list_bss);
        artifacts->property_specs_bss = find_import(ctx, names->property_specs_bss);
    }
    lambda_mvp_destroy(m);
    ast_index_destroy(&tp->ast_index);
    return ok;
}
