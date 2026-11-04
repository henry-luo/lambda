#include "mvp_internal.hpp"
#include "../interp.hpp"
#include "../type_contract.hpp"
#include "../mir_loop_analysis.hpp"
#include "../mir_cow_analysis.hpp"
#include "../re2_wrapper.hpp"
#include "../../../lib/host_paths.h"

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

ValueRep mvp_rep(Type* type) {
    if (!type) return VALUE_REP_ITEM;
    // an ordinary error alternative needs Item transport until discharge;
    // its type-expression pointer is not a runtime value (S7.8.1, D2.4.3).
    if (lambda_type_accepts_error(type)) return VALUE_REP_ITEM;
    Type* plain = type_field_unwrap_simple_decl(type);
    if (plain && plain->type_id == LMD_TYPE_TYPE && plain->kind == TYPE_KIND_UNARY &&
            (((TypeUnary*)plain)->op == OPERATOR_ZERO_MORE || ((TypeUnary*)plain)->op == OPERATOR_ONE_MORE ||
             ((TypeUnary*)plain)->op == OPERATOR_REPEAT)) return VALUE_REP_ITEM;
    // sized arithmetic uses the existing boxed numeric helpers until a native
    // lane proof is available; its subtype remains part of the contract.
    if (type_field_unwrap_simple_decl(type)->type_id == LMD_TYPE_NUM_SIZED) return VALUE_REP_ITEM;
    // full contracts, including nullable and union types, select their carrier.
    return lambda_canonical_rep(type);
}

Type* mvp_contract(AstNode* node) {
    if (!node) return &TYPE_ANY;
    if (node->node_type == AST_NODE_PARAM) {
        TypeParam* param = lambda_type_param(node->type);
        return param && param->contract_type ? param->contract_type : node->type;
    }
    if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* decl = (AstDeclaratorNode*)node;
        if (decl->declared_type) return decl->declared_type;
        // an initializer's literal type is not a restriction on an unannotated var (S12.2.1).
        if (decl->entry && decl->entry->is_mutable && node->type && node->type->is_literal) {
            Type* base = lambda_canonical_type_for_id(node->type->type_id);
            if (base) return base;
        }
    }
    return node->type ? node->type : &TYPE_ANY;
}

MvpFunction* mvp_function(LambdaMvpModule* m, AstFuncNode* ast) {
    for (uint32_t i = 0; i < m->function_count; i++)
        if (m->functions[i].ast == ast) return &m->functions[i];
    for (int i = 0; m->imported_functions && i < m->imported_functions->length; i++) {
        MvpFunction* function = (MvpFunction*)m->imported_functions->data[i];
        if (function->ast == ast) return function;
    }
    return NULL;
}

MvpBinding* mvp_binding(LambdaMvpModule* m, NameEntry* entry) {
    // imported binding slots belong to the declaring Script's index/slab.
    if (entry && (entry->import || entry->import_owner)) return NULL;
    AstBindingId id = ast_index_find_binding_entry(&m->tp->ast_index, entry);
    return id < m->binding_count ? &m->bindings[id] : NULL;
}

MvpBinding* mvp_local_binding(LambdaMvpModule* m, AstNode* node) {
    node = ast_unwrap_primary_to_leaf(node);
    return node && node->node_type == AST_NODE_IDENT ? mvp_binding(m, ((AstIdentNode*)node)->entry) : NULL;
}

bool mvp_array_contract(Type* contract) {
    LambdaArrayContractInfo info = {};
    Type* plain = type_field_unwrap_simple_decl(contract);
    return plain && (plain->type_id == LMD_TYPE_ARRAY || plain->type_id == LMD_TYPE_ARRAY_NUM ||
        lambda_array_contract_info(contract, &info));
}

Type* mvp_array_element(Type* contract) {
    LambdaArrayContractInfo info = {};
    if (!lambda_array_contract_info(contract, &info) || info.rank != 1) return NULL;
    Type* element = type_field_unwrap_simple_decl(info.immediate_element);
    return element == &TYPE_INT || element == &TYPE_BOOL || element == &TYPE_FLOAT ? element : NULL;
}

ShapeEntry* mvp_member_field(LambdaMvpModule* m, AstFieldNode* node, TypeMap** shape) {
    Type* owner = lambda_type_nonnull_map_contract(mvp_value_contract(m, node->object));
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
    return type && !type->is_literal && (type == &TYPE_ERROR ||
        (type != &TYPE_TYPE && type == lambda_canonical_type_for_id(type->type_id))) ? type : NULL;
}

bool mvp_boundary_proven(Type* source, Type* destination) {
    // invariant primitive arrays retain their admitted contract even with Item transport.
    return source && (lambda_static_boundary_relation(source, destination, false) == STATIC_BOUNDARY_PROVEN ||
        (mvp_array_element(source) && mvp_array_element(destination) &&
            lambda_array_contract_compatible(source, destination, true)));
}

bool mvp_pure_key(AstNode* node, int depth, bool allow_pure_calls) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 32) return false;
    if (node->node_type == AST_NODE_IDENT || node->node_type == AST_NODE_PRIMARY) return true;
    if (node->node_type == AST_NODE_BINARY) return mvp_pure_key(((AstBinaryNode*)node)->left, depth + 1, allow_pure_calls) &&
        mvp_pure_key(((AstBinaryNode*)node)->right, depth + 1, allow_pure_calls);
    if (allow_pure_calls && node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* target = ast_unwrap_primary_to_leaf(call->function);
        if (!target || target->node_type != AST_NODE_SYS_FUNC || ((AstSysFuncNode*)target)->fn_info->is_proc) return false;
        for (AstNode* arg = call->argument; arg; arg = arg->next)
            if (!mvp_pure_key(arg, depth + 1, true)) return false;
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
    TypeMap* record = binding ? mvp_record_contract(binding->contract, writes) : NULL;
    if (!binding || (!mvp_array_element(binding->contract) && !record) ||
            binding->owner >= m->function_count) return false;
    // sibling-borrow snapshots are a call-site alias obligation, not an
    // escape effect. Direct calls capture aliases; dense callers guard COW
    // exclusivity before eliding that snapshot (S9.1.3v2).
    // storage/escape proofs apply to both callable colours (S9.1.2, S12.1.2v3).
    if (forwarding && !writes && !binding->entry->cow_param_retained && !binding->entry->cow_param_mutated) return true;
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
        AstNode* definition = binding->entry->node;
        if (use == definition || (definition && definition->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                use == ((AstDeclaratorNode*)definition)->id)) continue;
        if (use->node_type != AST_NODE_IDENT || index->owner_functions[uses[i]] != binding->owner) return false;
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
        for (AstNode* arg = call->argument; arg; arg = arg->next, formal = (AstNamedNode*)formal->next) {
            if (mvp_local_binding(m, arg) != binding) continue;
            // a cached record layout survives forwarding only when the
            // callee enforces the identical shape, not a compatible retag.
            if (record && mvp_record_contract(mvp_contract(formal), false) != record) return false;
            bool child_writes = lambda_type_param(formal->type)->is_var_param;
            if (writes && !child_writes) {
                // match direct_call's read-only borrow rule: an effectful
                // sibling or simultaneous var alias forces a snapshot capture.
                AstNamedNode* sibling = target->ast->param;
                for (AstNode* a = call->argument; a; a = a->next, sibling = (AstNamedNode*)sibling->next)
                    if (!mvp_pure_key(a, 0, true) || (lambda_type_param(sibling->type)->is_var_param &&
                            mvp_local_binding(m, a) == binding)) return false;
            }
            if ((child_writes && !writes) || !parameter_storage_closed(m, formal, child_writes, active, depth)) return false;
        }
    }
    return true;
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

bool mvp_proc_has_final_return(AstFuncNode* fn) {
    AstNode* node = fn->body;
    while (node && (node->node_type == AST_NODE_CONTENT || node->node_type == AST_NODE_BLOCK)) {
        node = node->node_type == AST_NODE_CONTENT ? ((AstListNode*)node)->item : ((AstBlockNode*)node)->statements;
        while (node && node->next) node = node->next;
    }
    return node && node->node_type == AST_NODE_RETURN_STAM;
}

AstFieldNode* mvp_string_index(LambdaMvpModule* m, AstNode* node, int depth) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 32 || node->node_type != AST_NODE_INDEX_EXPR) return NULL;
    AstFieldNode* index = (AstFieldNode*)node;
    return mvp_value_contract(m, index->object, depth + 1)->type_id == LMD_TYPE_STRING &&
        mvp_value_contract(m, index->field, depth + 1)->type_id == LMD_TYPE_INT ? index : NULL;
}

static bool binding_var_borrow(LambdaMvpModule* m, MvpBinding* binding) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId i = 0; i < index->count; i++) {
        if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[i];
        AstFuncNode* function = ast_direct_call_function(call);
        AstNamedNode* param = function ? function->param : NULL;
        for (AstNode* arg = call->argument; arg && param; arg = arg->next, param = (AstNamedNode*)param->next)
            if (lambda_type_param(param->type)->is_var_param && mvp_local_binding(m, arg) == binding) return true;
    }
    return false;
}

static bool binding_reassigned(LambdaMvpModule* m, MvpBinding* binding, bool elements, bool whole = true) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (whole && node->node_type == AST_NODE_ASSIGN_STAM && ((AstAssignStamNode*)node)->target_entry == binding->entry) return true;
        if (elements && (node->node_type == AST_NODE_INDEX_ASSIGN_STAM || node->node_type == AST_NODE_MEMBER_ASSIGN_STAM) &&
                mvp_local_binding(m, ((AstAssignStamNode*)node)->object) == binding) return true;
    }
    return false;
}

bool mvp_array_index_present(LambdaMvpModule* m, MvpBinding* b, AstNode* key) {
    if (!b || !mvp_array_element(b->contract) || !b->entry || !b->entry->node ||
            b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) return false;
    MvpIntRange offset = mvp_int_range(m, key);
    if (!offset.finite || offset.lower < 0) return false;
    AstNode* init = ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)b->entry->node)->init);
    int64_t length = 0;
    if (init && init->node_type == AST_NODE_ARRAY) length = ast_linked_node_count(((AstArrayNode*)init)->item);
    else if (init && init->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)init;
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
                ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_FILL) return false;
        MvpIntRange count = mvp_int_range(m, call->argument);
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
    AstFunctionId declared_owner = index->owner_functions[ast_index_find(index, declaration)];
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
        AstFunctionId owner = index->owner_functions[use];
        if (owner >= m->function_count || owner != declared_owner) return NULL;
        Type* result = m->functions[owner].result_contract;
        if (target && target != result) return NULL;
        target = result;
    }
    return target;
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
        AstFunctionId owner = index->owner_functions[parent];
        if (owner < m->function_count) target = m->functions[owner].result_contract;
    }
    return target;
}

static TypeMap* contextual_map_contract(LambdaMvpModule* m, AstNode* literal) {
    TypeMap* shape = (TypeMap*)lambda_type_nonnull_map_contract(mvp_contextual_contract(m, literal));
    return shape && shape->is_trusted_contract && ast_map_literal_keys_follow_contract((AstMapNode*)literal, shape)
        ? shape : NULL;
}

// query transient scalar producer proofs; the shared AST remains the sole type graph (D8.2.5v3).
Type* mvp_value_contract(LambdaMvpModule* m, AstNode* source, int depth, bool dense_arrays) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n) return &TYPE_NULL;
    if (depth > 32) return &TYPE_ANY;
    if (n->node_type == AST_NODE_MAP) {
        TypeMap* target = contextual_map_contract(m, n);
        if (target) {
            ShapeEntry* field = target->shape;
            for (AstNode* item = ((AstMapNode*)n)->item; item; item = item->next, field = typemap_next_field(target, field)) {
                AstNode* input = item->node_type == AST_NODE_KEY_EXPR ? ((AstNamedNode*)item)->as : item;
                if (!mvp_boundary_proven(mvp_value_contract(m, input, depth + 1, dense_arrays), field->type)) return &TYPE_ANY;
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
        ShapeEntry* field = mvp_member_field(m, (AstFieldNode*)n);
        if (!field) return &TYPE_ANY;
        bool optional = false;
        lambda_type_nullable_lane_base(mvp_value_contract(m, ((AstFieldNode*)n)->object, depth + 1), &optional);
        return optional || ((AstFieldNode*)n)->optional
            ? lambda_type_nullable_normalized(m->tp->pool, field->type) : type_field_unwrap_simple_decl(field->type);
    }
    // an indexed read can be absent even under a non-null element contract (S7.1.1v3).
    if (n->node_type == AST_NODE_INDEX_EXPR) {
        MvpBinding* b = mvp_local_binding(m, ((AstFieldNode*)n)->object);
        Type* element = b ? mvp_array_element(b->contract) : NULL;
        bool optional_key = false;
        Type* key = lambda_type_nullable_lane_base(
            mvp_value_contract(m, ((AstFieldNode*)n)->field, depth + 1, dense_arrays), &optional_key);
        // an absent integer key yields absence too, not an arbitrary value.
        // Keep that domain through nested typed indexing (S7.1.1v3).
        if (!key || key->type_id != LMD_TYPE_INT) return &TYPE_ANY;
        Type* owner_contract = b ? b->contract :
            mvp_value_contract(m, ((AstFieldNode*)n)->object, depth + 1, dense_arrays);
        Type* pointer_element = lambda_array_contract_element(owner_contract);
        // an admitted pointer-element array retains its complete field/element
        // contract; an integer miss still yields absence (S7.1.1v3).
        if (!element && pointer_element && (pointer_element == &TYPE_STRING ||
                lambda_type_nonnull_map_contract(pointer_element) || mvp_array_contract(pointer_element)))
            return lambda_type_nullable_normalized(m->tp->pool, pointer_element);
        return element ? dense_arrays || b->array_present || mvp_array_index_present(m, b, ((AstFieldNode*)n)->field)
            ? element : mvp_nullable_contract(m, element) : &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_binding(m, ((AstIdentNode*)n)->entry);
        return b ? b->contract : &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)n;
        MvpFunction* f = mvp_function(m, ast_direct_call_function(call));
        if (f) {
            if (call->propagate) {
                // a discharged call retains only its success contract, just
                // like the unary caret form (S7.7.2v2).
                Type* success = lambda_type_remove_error(m->tp->pool, f->result_contract);
                return success == &TYPE_ANY_NO_ERROR ? n->type : success;
            }
            if (f->abi.effects.may_return_error) return &TYPE_ANY;
            AstNamedNode* p = f->ast->param;
            for (AstNode* a = call->argument; a && p; a = a->next, p = (AstNamedNode*)p->next)
                if (!mvp_boundary_proven(mvp_value_contract(m, a, depth + 1, dense_arrays), mvp_contract(p))) return &TYPE_ANY;
            return f->result_contract;
        }
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return &TYPE_ANY;
        SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
        if (call->propagate && info->fn == SYSFUNC_FILL) {
            Type* destination = mvp_contextual_contract(m, n);
            Type* element = mvp_array_element(destination);
            if (element && lambda_array_contract_is_plain(destination) && !lambda_type_counts_array_length(destination) &&
                    mvp_boundary_proven(mvp_value_contract(m, call->argument->next, depth + 1, dense_arrays), element))
                return destination;
        }
        if (info->fn == SYSPROC_CLOCK) return &TYPE_FLOAT;
        if (info->fn == SYSPROC_PRINT) return &TYPE_NULL;
        if (info->fn == SYSFUNC_ORD && mvp_string_index(m, call->argument, depth + 1)) return m->nullable_int;
        Type* arg_contract = mvp_value_contract(m, call->argument, depth + 1, dense_arrays);
        if (info->fn == SYSFUNC_STRING) return lambda_type_accepts_error(arg_contract) ? &TYPE_ANY : &TYPE_STRING;
        TypeId arg = arg_contract->type_id;
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(arg_contract, &optional);
        if (base == &TYPE_INT && sysfunc_native_math_type_preserving(info->fn, NULL)) return arg_contract;
        if (info->fn == SYSFUNC_LEN && (arg == LMD_TYPE_STRING ||
                mvp_array_contract(mvp_value_contract(m, call->argument, depth + 1, dense_arrays)))) return &TYPE_INT;
        if (mvp_native_math(info, arg)) return &TYPE_FLOAT;
        if ((info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) &&
                (arg == LMD_TYPE_FLOAT || arg == LMD_TYPE_INT))
            return info->fn == SYSFUNC_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
        if (info->fn == SYSFUNC_SHR && mvp_int_range(m, n).finite) return &TYPE_INT;
        // discharge retains producer proofs; a generic AST success type must
        // not hide a closed native conversion from the guarded-body proof.
        return call->propagate ? n->type : &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_BINARY) {
        AstBinaryNode* b = (AstBinaryNode*)n;
        if (b->op == OPERATOR_IS) return &TYPE_BOOL;
        if ((b->op == OPERATOR_EQ || b->op == OPERATOR_NE) &&
                mvp_string_index(m, b->left, depth + 1) && mvp_string_index(m, b->right, depth + 1)) return &TYPE_BOOL;
        bool left_null = false, right_null = false;
        Type* left_base = lambda_type_nullable_lane_base(mvp_value_contract(m, b->left, depth + 1, dense_arrays), &left_null);
        Type* right_base = lambda_type_nullable_lane_base(mvp_value_contract(m, b->right, depth + 1, dense_arrays), &right_null);
        TypeId left = left_base ? left_base->type_id : LMD_TYPE_ANY;
        TypeId right = right_base ? right_base->type_id : LMD_TYPE_ANY;
        if ((b->op == OPERATOR_EQ || b->op == OPERATOR_NE) &&
                ((left == LMD_TYPE_NULL && (right == LMD_TYPE_MAP || right == LMD_TYPE_NULL)) ||
                 (right == LMD_TYPE_NULL && left == LMD_TYPE_MAP))) return &TYPE_BOOL;
        if (b->op == OPERATOR_JOIN && left == LMD_TYPE_STRING && right == LMD_TYPE_STRING)
            return left_null && right_null ? mvp_nullable_contract(m, &TYPE_STRING) : &TYPE_STRING;
        bool numbers = (left == LMD_TYPE_INT || left == LMD_TYPE_FLOAT) &&
            (right == LMD_TYPE_INT || right == LMD_TYPE_FLOAT);
        if (numbers) {
            MirNumericOpPlan plan = {};
            if (em_numeric_op_plan(b->op, &plan, true)) {
                Type* result = plan.is_comparison ? &TYPE_BOOL :
                    b->op == OPERATOR_DIV || left == LMD_TYPE_FLOAT || right == LMD_TYPE_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
                return (left_null || right_null) && b->op != OPERATOR_EQ && b->op != OPERATOR_NE
                    ? mvp_nullable_contract(m, result) : result;
            }
            if (b->op == OPERATOR_IDIV && left == LMD_TYPE_INT && right == LMD_TYPE_INT) return &TYPE_INT;
        }
        if ((b->op == OPERATOR_AND || b->op == OPERATOR_OR || b->op == OPERATOR_EQ || b->op == OPERATOR_NE) &&
                left == LMD_TYPE_BOOL && right == LMD_TYPE_BOOL)
            return (b->op == OPERATOR_AND || b->op == OPERATOR_OR) && (left_null || right_null)
                ? m->nullable_bool : &TYPE_BOOL;
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
        if (u->op == OPERATOR_NOT && base && base->type_id == LMD_TYPE_BOOL) return &TYPE_BOOL;
        if ((u->op == OPERATOR_POS || u->op == OPERATOR_NEG) && base &&
                (base->type_id == LMD_TYPE_INT || base->type_id == LMD_TYPE_FLOAT)) return operand;
        return &TYPE_ANY;
    }
    return n->type ? type_field_unwrap_simple_decl(n->type) : &TYPE_ANY;
}

LambdaMvpModule* lambda_mvp_create(Transpiler* tp, MIR_context_t ctx) {
    LambdaMvpModule* m = (LambdaMvpModule*)mem_calloc(1, sizeof(*m), MEM_CAT_TEMP);
    if (m) { m->tp = tp; m->ctx = ctx; }
    return m;
}

// Demand-only interval query over the shared AST and immutable binding plans;
// it never publishes a second node-type/fact table (D8.2.5v3).
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
    Operator op = test->op;
    if (reversed) {
        if (op == OPERATOR_LT) op = OPERATOR_GT;
        else if (op == OPERATOR_LE) op = OPERATOR_GE;
        else if (op == OPERATOR_GT) op = OPERATOR_LT;
        else if (op == OPERATOR_GE) op = OPERATOR_LE;
    }
    if (!taken) {
        if (op == OPERATOR_LT) op = OPERATOR_GE;
        else if (op == OPERATOR_LE) op = OPERATOR_GT;
        else if (op == OPERATOR_GT) op = OPERATOR_LE;
        else if (op == OPERATOR_GE) op = OPERATOR_LT;
        else if (op == OPERATOR_EQ) op = OPERATOR_NE;
        else if (op == OPERATOR_NE) op = OPERATOR_EQ;
    }
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
        MvpBinding* owner = mvp_local_binding(m, index->object);
        // absence cannot enter a finite range proof, even for an immutable table.
        if (owner && owner->readonly_literal && mvp_array_index_present(m, owner, index->field))
            return array_element_range(m, owner, depth + 1);
        return {};
    }
    if (node->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_binding(m, ((AstIdentNode*)node)->entry);
        MvpIntRange range = b ? b->range : MvpIntRange{};
        if (!range.finite) return range;
        AstIndex* index = &m->tp->ast_index;
        AstNodeId id = ast_index_find(index, node);
        while ((id = ast_index_parent_id(index, id)) < index->count) {
            AstNode* parent = index->nodes[id];
            if ((parent->node_type == AST_NODE_CONTENT || parent->node_type == AST_NODE_BLOCK) &&
                    b->entry->node->node_type == AST_NODE_PARAM) {
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
        if (fn == SYSFUNC_LEN && mvp_value_contract(m, call->argument, depth + 1)->type_id == LMD_TYPE_STRING)
            return {true, 0, UINT32_MAX};
        // an already integral expression keeps its interval through an explicit int conversion.
        if (fn == SYSFUNC_INT) return mvp_int_range(m, call->argument, depth + 1);
        if (fn != SYSFUNC_SHR) return {};
        MvpIntRange a = mvp_int_range(m, call->argument, depth + 1);
        MvpIntRange b = mvp_int_range(m, call->argument->next, depth + 1);
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
    if (plan->follower && ast_index_node_descends(index, ast_index_find(index, plan->follower->entry->node), root)) return false;
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
    if (!plan->loop || !plan->follower || plan->counter->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            plan->follower->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            plan->limit->entry->node->node_type != AST_NODE_PARAM) return false;
    plan->first = mvp_local_binding(m, ((AstDeclaratorNode*)plan->counter->entry->node)->init);
    if (!plan->first || plan->first->entry->node->node_type != AST_NODE_PARAM ||
            plan->first != mvp_local_binding(m, ((AstDeclaratorNode*)plan->follower->entry->node)->init)) return false;
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

bool mvp_numeric_inline_plan(LambdaMvpModule* m, MvpFunction* function, MvpNumericLoopPlan* plan) {
    if (function->abi.effects.may_gc || function->abi.effects.may_return_error) return false;
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
                    b->entry->node->node_type != AST_NODE_PARAM) return false;
            break;
        }
        case AST_NODE_PRIMARY: case AST_NODE_UNARY: case AST_NODE_CONTENT:
        case AST_NODE_BLOCK: case AST_NODE_RETURN_STAM: case AST_NODE_CALL_EXPR:
        case AST_NODE_SYS_FUNC: break;
        default: return false;
        }
    }
    if (arithmetic < 3) return false;
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
        for (AstNode* arg = call->argument; arg && p; arg = arg->next, p = (AstNamedNode*)p->next)
            if (lambda_type_param(p->type)->is_var_param && mvp_local_binding(m, arg) == binding) return true;
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
    AstNodeId declaration = ast_index_find(index, binding->entry->node), outer = AST_NODE_ID_INVALID;
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
            node = ast_unwrap_primary_to_leaf(cast->argument);
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
        int position, bool self_edges, MvpIntRange* range) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId n = 0; n < index->count; n++) {
        if (index->nodes[n]->node_type != AST_NODE_CALL_EXPR ||
                (index->owner_functions[n] == function->id) != self_edges) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[n];
        if (ast_direct_call_function(call) != function->ast) continue;
        AstNode* argument = call->argument;
        for (int i = 0; argument && i < position; i++) argument = argument->next;
        MvpIntRange incoming = mvp_int_range(m, argument);
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
            if (b->range.finite || b->rep != VALUE_REP_INT_LANE || !b->entry || !b->entry->node) continue;
            if (b->entry->node->node_type == AST_NODE_PARAM) {
                MvpFunction* function = b->owner < m->function_count ? &m->functions[b->owner] : NULL;
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
            if (b->entry->node->node_type == AST_NODE_FOR_CLAUSE) {
                AstNode* source = ast_unwrap_primary_to_leaf(((AstLoopNode*)b->entry->node)->as);
                if (!source || source->node_type != AST_NODE_BINARY || ((AstBinaryNode*)source)->op != OPERATOR_TO) continue;
                MvpIntRange first = mvp_int_range(m, ((AstBinaryNode*)source)->left);
                MvpIntRange last = mvp_int_range(m, ((AstBinaryNode*)source)->right);
                if (first.finite && last.finite && first.lower <= last.upper) {
                    b->range = {true, first.lower, last.upper}; changed = true;
                }
                continue;
            }
            if (b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
            AstDeclaratorNode* declaration = (AstDeclaratorNode*)b->entry->node;
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
        if (b->rep != VALUE_REP_F64 || !b->entry || !b->entry->node || b->entry->node->node_type != AST_NODE_PARAM) continue;
        MvpFunction* f = &m->functions[b->owner];
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
            AstNode* a = call->argument;
            for (int p = 0; a && p < position; p++) a = a->next;
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

static Type* inferred_array_element(LambdaMvpModule* m, AstNode* initializer) {
    AstNode* source = ast_unwrap_primary_to_leaf(initializer);
    if (!source) return NULL;
    AstNode* items = NULL;
    if (source->node_type == AST_NODE_ARRAY) items = ((AstArrayNode*)source)->item;
    else if (source->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)source;
        AstNode* target = ast_unwrap_primary_to_leaf(call->function);
        if (!target || target->node_type != AST_NODE_SYS_FUNC ||
                ((AstSysFuncNode*)target)->fn_info->fn != SYSFUNC_FILL) return NULL;
        MvpIntRange count = mvp_int_range(m, call->argument);
        // a failed fill remains an ordinary inferred error value; it cannot
        // acquire an internal typed-declaration failure path (S11.4.1v5).
        if (!count.finite || count.lower < 0) return NULL;
        items = call->argument->next;
    }
    Type* element = NULL;
    for (AstNode* item = items; item; item = item->next) {
        Type* contract = mvp_value_contract(m, item);
        Type* kind = lambda_canonical_type_for_id(contract->type_id);
        if ((kind != &TYPE_INT && kind != &TYPE_FLOAT && kind != &TYPE_BOOL) ||
                (element && element != kind)) return NULL;
        element = kind;
    }
    return element;
}

static bool widen_inferred_binding(LambdaMvpModule* m, MvpBinding* b, Type* source) {
    if (!b || b->contract == &TYPE_ANY || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            ((AstDeclaratorNode*)b->entry->node)->declared_type) return false;
    if (b->inferred_array) {
        // every initializer and indexed write is checked at the same effect
        // fixed point; this is a private proof, never a new user contract.
        Type* element = inferred_array_element(m, ((AstDeclaratorNode*)b->entry->node)->init);
        if (element == mvp_array_element(b->contract)) return false;
        b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM;
        return true;
    }
    Type* joined = b->contract;
    bool source_null = false, binding_null = false;
    Type* from = lambda_type_nullable_lane_base(source, &source_null);
    Type* to = lambda_type_nullable_lane_base(b->contract, &binding_null);
    if (source == &TYPE_ANY) joined = &TYPE_ANY;
    else if (source == &TYPE_NULL && to) joined = mvp_nullable_contract(m, to);
    else if (from && to && from->type_id == to->type_id && source_null && !binding_null)
        joined = mvp_nullable_contract(m, to);
    else if (!from || !to || from->type_id != to->type_id) joined = &TYPE_ANY;
    // a private record cursor keeps only the same enforced shape across all
    // writes; a different inferred record must remain unrestricted (S12.2.1v3).
    if (joined != &TYPE_ANY && from && to && to->type_id == LMD_TYPE_MAP &&
            source != &TYPE_NULL && from != to) joined = &TYPE_ANY;
    if (joined == b->contract) return false;
    b->contract = joined; b->rep = mvp_rep(joined);
    return true;
}

static bool admitted_contract(Type* type, Pool* pool) {
    if (!type) return true;
    type = type_field_unwrap_simple_decl(type);
    if (type == &TYPE_ERROR || type == &TYPE_ANY_NO_ERROR || mvp_boundary_proven(type, &TYPE_ERROR)) return true;
    if (lambda_type_is_union(type) && lambda_type_accepts_error(type)) {
        Type* success = lambda_type_remove_error(pool, type);
        if (success != type) return admitted_contract(success, pool);
    }
    if (lambda_type_is_union(type)) {
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
        type->type_id == LMD_TYPE_ARRAY || type->type_id == LMD_TYPE_MAP || type->type_id == LMD_TYPE_DECIMAL ||
        type->type_id == LMD_TYPE_NULL || type->type_id == LMD_TYPE_ERROR || type->type_id == LMD_TYPE_ANY);
}

static MvpIntRange array_element_range(LambdaMvpModule* m, MvpBinding* binding, int depth) {
    if (depth > 16 || !binding || mvp_array_element(binding->contract) != &TYPE_INT || binding_var_borrow(m, binding)) return {};
    if (binding->entry->node->node_type == AST_NODE_PARAM) {
        AstNamedNode* parameter = (AstNamedNode*)binding->entry->node;
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
            AstNode* actual = call->argument;
            for (int p = 0; actual && p < position; p++) actual = actual->next;
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
    if (binding->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) return {};
    AstNode* initial = ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)binding->entry->node)->init);
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
    MvpIntRange range = mvp_int_range(m, fill->argument->next);
    if (!range.finite) return {};
    AstIndex* index = &m->tp->ast_index;
    // value copies detach before writing; only direct writes/borrows of this
    // root can change its element domain (S9.1.2, D4.4.6).
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
    if (!sum || sum == counter || sum == bound || sum->rep != VALUE_REP_INT_LANE ||
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
    external |= mvp_boxed_entry(f->ast);
    ValueRep rep = external ? VALUE_REP_ITEM : mvp_rep(type);
    bool native = rep != VALUE_REP_ITEM;
    f->result_contract = type;
    f->abi.entry = {native ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY,
        true, false, false, external};
    f->abi.result.normal = {type ? type->type_id : LMD_TYPE_ANY, rep,
        native ? SCALAR_RETURN_NONE : SCALAR_RETURN_DYNAMIC};
    f->abi.result.error = {LMD_TYPE_ERROR, VALUE_REP_ITEM, SCALAR_RETURN_NONE};
    bool errors = f->abi.effects.may_return_error ||
        (f->declared_result && lambda_type_accepts_error(f->declared_result));
    f->abi.result.shape = native ? (errors ? RETURN_SHAPE_NATIVE_ERROR : RETURN_SHAPE_NATIVE)
        : RETURN_SHAPE_ITEM_SCALAR;
    f->abi.result.companion = em_companion_transport(f->abi.result.shape, external);
    f->abi.result.error_lane = FN_ERROR_LANE_NONE;
}

static bool admitted_call(LambdaMvpModule* m, AstCallNode* call) {
    if (call->interp_has_named_args || call->interp_has_spread_args || call->pipe_inject)
        return mvp_reject(m, call, "named, spread and pipe arguments are not admitted yet");
    AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
    AstFuncNode* target = ast_direct_call_function(call);
    int count = ast_linked_node_count(call->argument);
    if (target) {
        MvpFunction* f = mvp_function(m, target);
        if (!f && callee && callee->node_type == AST_NODE_IDENT) {
            NameEntry* entry = ((AstIdentNode*)callee)->entry;
            Script* owner = entry ? entry->import_owner ? entry->import_owner : entry->import ? entry->import->script : NULL : NULL;
            if (owner && ((TypeFunc*)target->type)->is_public) {
                f = (MvpFunction*)mem_calloc(1, sizeof(MvpFunction), MEM_CAT_TEMP);
                f->ast = target; f->imported = true; f->id = AST_FUNCTION_ID_INVALID;
                StrBuf* name = strbuf_new_cap(64);
                write_fn_name_for_script_ex(name, target, owner, "_b");
                f->name = name_pool_create_name(m->tp->name_pool, name->str)->chars;
                strbuf_free(name);
                f->abi.param_count = ast_linked_node_count(target->param);
                f->abi.params = (FnParamAnalysis*)mem_calloc(f->abi.param_count, sizeof(FnParamAnalysis), MEM_CAT_TEMP);
                for (int i = 0; i < f->abi.param_count; i++) f->abi.params[i] = {LMD_TYPE_ANY, VALUE_REP_ITEM, 0};
                f->abi.effects.may_gc = f->abi.effects.may_reenter = f->abi.effects.may_return_error = true;
                f->declared_result = &TYPE_ANY;
                plan_return(f, &TYPE_ANY, true);
                if (!m->imported_functions) m->imported_functions = arraylist_new(8);
                arraylist_append(m->imported_functions, f);
            }
        }
        if (!f || count < ((TypeFunc*)target->type)->required_param_count || count > f->abi.param_count)
            return mvp_reject(m, call, "a direct call requires an admitted fixed-arity definition");
        AstNamedNode* param = f->ast->param;
        for (AstNode* arg = call->argument; arg; arg = arg->next, param = (AstNamedNode*)param->next) {
            TypeParam* p = lambda_type_param(param->type);
            if (p && p->is_var_param) {
                MvpBinding* b = mvp_local_binding(m, arg);
                Type* contract = mvp_contract(param);
                bool invariant = b && (lambda_type_repr_equal(b->contract, contract) ||
                    lambda_type_repr_equal(b->admission_contract, contract) ||
                    b->contract == &TYPE_ANY ||
                    (mvp_array_contract(b->contract) && lambda_array_contract_compatible(b->contract, contract, true)));
                if (!b) {
                    AstCowPath path = {};
                    MvpBinding* root = ast_collect_cow_path(&path, arg) ? mvp_local_binding(m, path.root) : NULL;
                    int64_t index_mask = 0;
                    int last = path.count - 1;
                    if (last >= 0) path.count = last;
                    Type* leaf = root && last >= 0 ? ast_map_path_leaf_contract(root->admission_contract, &path,
                        path.segment[last], path.is_member[last], &index_mask) : NULL;
                    invariant = root && last >= 0 && (lambda_type_repr_equal(leaf, contract) ||
                        lambda_array_contract_compatible(leaf, contract, true) || leaf == &TYPE_ANY);
                }
                if (!invariant)
                    return mvp_reject(m, arg, "var arguments require an invariant local binding");
            }
        }
        return true;
    }
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC)
        return mvp_reject(m, call, "indirect calls are not admitted yet");
    SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
    if (!info || count > LAMBDA_MAX_FUNCTION_ARGS)
        return mvp_reject(m, call, "unsupported builtin argument count");
    switch (info->fn) {
    case SYSPROC_PRINT:
        return count == 1 || mvp_reject(m, call, "print currently requires one argument");
    case SYSPROC_CLOCK: case SYSFUNC_ABS: case SYSFUNC_FLOOR: case SYSFUNC_SQRT: case SYSFUNC_SIN: case SYSFUNC_COS:
    case SYSFUNC_ROUND:
    case SYSFUNC_INT: case SYSFUNC_FLOAT: case SYSFUNC_STRING:
    case SYSFUNC_LEN: case SYSFUNC_BOR: case SYSFUNC_BXOR: case SYSFUNC_SHL: case SYSFUNC_SHR:
    case SYSFUNC_FILL: case SYSFUNC_ORD: case SYSFUNC_CHR: case SYSFUNC_SLICE:
        return count == info->arg_count || mvp_reject(m, call, "builtin arity mismatch");
    default: {
        // fixed pure builtins use the registry's physical ABI and shared roots.
        // Owner-mutating procedures require explicit place lowering below.
        JitImportMetadata metadata = {};
        bool mutation = info->fn == SYSPROC_PUSH || info->fn == SYSPROC_SPLICE;
        bool io = info->fn == SYSPROC_IO_READ || info->fn == SYSPROC_OUTPUT2 || info->fn == SYSPROC_OUTPUT3;
        if ((!info->is_proc || mutation || io) && (!info->is_async || io) && info->arg_count >= 0 &&
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
            b->entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR &&
            b->entry->node->source_span.start_byte < n->source_span.start_byte &&
            module_scalar_initializer(m, ((AstDeclaratorNode*)b->entry->node)->init, depth + 1);
    }
    if (n->node_type == AST_NODE_BINARY) return
        module_scalar_initializer(m, ((AstBinaryNode*)n)->left, depth + 1) &&
        module_scalar_initializer(m, ((AstBinaryNode*)n)->right, depth + 1);
    return n->node_type == AST_NODE_UNARY &&
        module_scalar_initializer(m, ((AstUnaryNode*)n)->operand, depth + 1);
}

static bool readonly_literal_binding(LambdaMvpModule* m, MvpBinding* binding) {
    return !binding_reassigned(m, binding, true) && !binding_var_borrow(m, binding);
}

bool lambda_mvp_plan(LambdaMvpModule* m) {
    if (!m || !m->tp->ast_index.graph_published) return false;
    if (lambda_tier_selected() != LAMBDA_TIER_JIT)
        return mvp_reject(m, NULL, "select LAMBDA_EXEC_BACKEND=jit explicitly");
    AstIndex* index = &m->tp->ast_index;
    bool context_globals = false;
    for (AstNodeId i = 0; i < index->count; i++)
        context_globals |= index->owner_functions[i] == AST_FUNCTION_ID_INVALID &&
            (index->nodes[i]->node_type == AST_NODE_CALL_EXPR || index->nodes[i]->node_type == AST_NODE_PUB_STAM);
    if (!compile_script_pattern_definitions(m->tp->pool, m->tp->type_list, ((AstScript*)m->tp->ast_root)->child))
        return mvp_reject(m, m->tp->ast_root, "pattern compilation failed");
    m->nullable_int = lambda_type_nullable_normalized(m->tp->pool, &TYPE_INT);
    m->nullable_bool = lambda_type_nullable_normalized(m->tp->pool, &TYPE_BOOL);
    m->nullable_float = lambda_type_nullable_normalized(m->tp->pool, &TYPE_FLOAT);
    m->function_count = index->function_count;
    m->binding_count = index->binding_count;
    m->functions = (MvpFunction*)mem_calloc(m->function_count, sizeof(MvpFunction), MEM_CAT_TEMP);
    m->bindings = (MvpBinding*)mem_calloc(m->binding_count, sizeof(MvpBinding), MEM_CAT_TEMP);
    if ((m->function_count && !m->functions) || (m->binding_count && !m->bindings))
        return mvp_reject(m, NULL, "compiler allocation failed");
    for (uint32_t i = 0; i < m->function_count; i++) {
        MvpFunction* f = &m->functions[i];
        f->ast = (AstFuncNode*)index->functions[i].node;
        f->id = i;
        if (!f->ast || (f->ast->node_type != AST_NODE_FUNC && f->ast->node_type != AST_NODE_PROC &&
                f->ast->node_type != AST_NODE_FUNC_EXPR) ||
                (index->functions[i].parent != AST_FUNCTION_ID_INVALID && f->ast->node_type != AST_NODE_FUNC_EXPR))
            return mvp_reject(m, f->ast, "only top-level fn/pn definitions are admitted");
        TypeFunc* signature = (TypeFunc*)f->ast->type;
        bool dependent = lambda_type_contract_has_binder(signature->return_contract, true);
        for (TypeParam* parameter = signature->param; parameter; parameter = parameter->next)
            dependent |= parameter->contract_type && parameter->contract_type->type_id == LMD_TYPE_TYPE &&
                parameter->contract_type->kind == TYPE_KIND_BOUND_REF;
        if (signature->is_variadic || (signature->binder_count && dependent) ||
                f->ast->captures || f->ast->is_async)
            return mvp_reject(m, f->ast, "exported, variadic, captured or suspending definitions are not admitted");
        char name[64];
        snprintf(name, sizeof(name), "mvp_%u_%s", i, f->ast->name ? f->ast->name->chars : "fn");
        f->name = name_pool_create_name(m->tp->name_pool, name)->chars;
        if (mvp_boxed_entry(f->ast)) {
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
            Type* contract = mvp_contract(param);
            if (!admitted_contract(contract, m->tp->pool) || (p && (
                (p->is_var_param && mvp_rep(contract) != VALUE_REP_ITEM &&
                 !mvp_array_contract(contract) && !ast_declared_type_is_map(contract)))))
                return mvp_reject(m, param, "parameter contract is outside the MVP subset");
            f->abi.params[n++] = {contract->type_id, mvp_boxed_entry(f->ast) || (p && p->is_optional) ? VALUE_REP_ITEM : mvp_rep(contract), 0};
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
        // successful scalar lane (S11.4.2v3, D2.4.3).
        plan_return(f, success == &TYPE_ANY_NO_ERROR || success == &TYPE_NONE ||
            mvp_boundary_proven(f->declared_result, &TYPE_ERROR) ? f->declared_result : success, false);
        // open scalar contracts can reach checked admission and contained errors.
        for (int p = 0; p < f->abi.param_count; p++)
            if (f->abi.params[p].canonical_rep == VALUE_REP_ITEM) {
                f->abi.effects.may_gc = true; f->abi.effects.may_return_error = true;
            }
        if (f->ast->name && strcmp(f->ast->name->chars, "main") == 0) {
            if (f->abi.param_count) return mvp_reject(m, f->ast, "main arguments are not admitted yet");
            m->main = f;
        }
    }
    m->entry.name = "main";
    m->entry.id = AST_FUNCTION_ID_INVALID;
    plan_return(&m->entry, &TYPE_ANY, true);
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        b->entry = index->bindings[i];
        b->contract = mvp_contract(b->entry ? b->entry->node : NULL);
        b->admission_contract = b->contract;
        if (b->entry && b->entry->admission_may_fail)
            b->contract = lambda_type_union_normalized(m->tp->pool, b->contract, &TYPE_ERROR);
        if (b->entry && b->entry->node && b->entry->node->node_type == AST_NODE_FOR_CLAUSE)
        {
            AstLoopNode* loop = (AstLoopNode*)b->entry->node;
            AstNode* source = ast_unwrap_primary_to_leaf(loop->as);
            bool range = source && source->node_type == AST_NODE_BINARY && ((AstBinaryNode*)source)->op == OPERATOR_TO;
            b->contract = range || (loop->key_only && loop->key_filter == LOOP_KEY_INT) ? &TYPE_INT : &TYPE_ANY;
        }
        b->rep = mvp_rep(b->contract);
        if (mvp_array_element(b->admission_contract) && lambda_array_contract_is_plain(b->admission_contract))
            b->array_cert = lambda_array_rep_cert_create(m->tp->pool, b->admission_contract);
        AstNodeId node_id = ast_index_find(index, b->entry ? b->entry->node : NULL);
        b->owner = node_id < index->count ? index->owner_functions[node_id] : AST_FUNCTION_ID_INVALID;
        if (b->entry && b->entry->node && b->entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                !((AstDeclaratorNode*)b->entry->node)->declared_type) {
            // inferred container contents are observations, not write contracts
            // (S12.2.1). Closed literal tables are refined separately below.
            if (mvp_array_contract(b->contract)) { b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM; }
            else if (b->entry->is_mutable && b->contract->type_id == LMD_TYPE_MAP) {
                b->contract = &TYPE_MAP; b->rep = mvp_rep(b->contract);
            }
        }
    }
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !b->entry->node || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)b->entry->node;
        AstNode* source = ast_unwrap_primary_to_leaf(d->init);
        if (d->declared_type || !source ||
                binding_reassigned(m, b, false) || binding_var_borrow(m, b)) continue;
        Type* element = inferred_array_element(m, d->init);
        if (!element) continue;
        // keep a private primitive domain only while the fixed point proves
        // every write. The public inferred type remains unrestricted.
        TypeArray* contract = (TypeArray*)alloc_type(m->tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
        contract->nested = element; contract->length = -1; contract->type_index = -1;
        b->contract = contract; b->rep = mvp_rep(contract); b->inferred_array = true;
        b->readonly_literal = source->node_type == AST_NODE_ARRAY && readonly_literal_binding(m, b);
        b->array_cert = lambda_array_rep_cert_create(m->tp->pool, contract);
    }
    // seed producer domains, then widen monotonically as assignment/call effects converge.
    for (AstNodeId i = 0; i < index->count; i++) {
        if (index->nodes[i]->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)index->nodes[i];
        if (d->declared_type || (d->entry && d->entry->admission_may_fail)) continue;
        MvpBinding* b = mvp_binding(m, d->entry);
        Type* source = mvp_value_contract(m, d->init);
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(source, &optional);
        if (!b || !base) continue;
        TypeMap* record = (TypeMap*)lambda_type_nonnull_map_contract(source);
        bool closed_record = record && record->is_trusted_contract &&
            !binding_reassigned(m, b, true, false) && !binding_var_borrow(m, b);
        if (!closed_record && base->type_id != LMD_TYPE_FLOAT && base->type_id != LMD_TYPE_INT &&
                base->type_id != LMD_TYPE_BOOL && base->type_id != LMD_TYPE_STRING) continue;
        base = closed_record ? (Type*)record : lambda_canonical_type_for_id(base->type_id);
        b->contract = optional ? mvp_nullable_contract(m, base) : base;
        b->rep = mvp_rep(b->contract);
    }
    // admission consumes the shared index, including bodies that are never called.
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        AstFunctionId owner = index->owner_functions[i];
        if (owner < m->function_count) m->functions[owner].inline_cost++;
        bool procedural = owner < m->function_count && m->functions[owner].ast->node_type == AST_NODE_PROC;
        if (!procedural && (node->node_type == AST_SCRIPT || node->node_type == AST_NODE_CONTENT || node->node_type == AST_NODE_BLOCK)) {
            AstNode* item = node->node_type == AST_SCRIPT ? ((AstScript*)node)->child :
                node->node_type == AST_NODE_CONTENT ? ((AstListNode*)node)->item : ((AstBlockNode*)node)->statements;
            int values = 0;
            for (; item; item = item->next)
                // fn-local control contributes no value, just like local writes.
                if (item->node_type != AST_NODE_IMPORT && !is_declaration_node(item) && !ast_block_item_is_statement(item, false) && ++values > 1)
                    return mvp_reject(m, node, "multi-value functional content is not admitted yet");
        }
        switch (node->node_type) {
        case AST_SCRIPT: case AST_NODE_CONTENT: case AST_NODE_BLOCK:
        case AST_NODE_EXPR_STMT: case AST_NODE_PRIMARY: case AST_NODE_IDENT:
        case AST_NODE_TYPE: case AST_NODE_SYS_FUNC: case AST_NODE_FUNC: case AST_NODE_PROC: case AST_NODE_FUNC_EXPR:
        case AST_NODE_PARAM: case AST_NODE_RETURN_STAM: case AST_NODE_IF_EXPR:
        case AST_NODE_LET_STAM: case AST_NODE_VAR_STAM:
        case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_FOR_CLAUSE: case AST_NODE_FOR_INDEX:
        case AST_NODE_TYPE_STAM: case AST_NODE_MAP_TYPE: case AST_NODE_ARRAY_TYPE:
        case AST_NODE_UNARY_TYPE: case AST_NODE_BINARY_TYPE: case AST_NODE_STRING_PATTERN:
        case AST_NODE_PATTERN_CHAR_CLASS: case AST_NODE_PATTERN_SEQ: case AST_NODE_PATTERN_RANGE: case AST_NODE_LIST_TYPE:
        case AST_NODE_PATH_EXPR: case AST_NODE_RAISE_EXPR: case AST_NODE_CURRENT_ERROR:
        case AST_NODE_IMPORT: case AST_NODE_PUB_STAM:
        case AST_NODE_GROUP_CLAUSE: case AST_NODE_GROUP_KEY: case AST_NODE_ORDER_SPEC:
        case AST_NODE_KEY_EXPR: case AST_NODE_MEMBER_EXPR:
        case AST_NODE_MATCH_ARM:
            break;
        case AST_NODE_MATCH_EXPR:
            for (AstMatchArm* arm = ((AstMatchNode*)node)->first_arm; arm; arm = (AstMatchArm*)arm->next)
                if (arm->pattern && !mvp_match_type(arm->pattern))
                    return mvp_reject(m, arm, "match currently requires base-type cases");
            break;
        case AST_NODE_HANDLER_EXPR: case AST_NODE_HANDLER_STAM:
            if (((AstHandlerNode*)node)->filter)
                return mvp_reject(m, node, "filtered handlers are not admitted yet");
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
            AstAssignStamNode* a = (AstAssignStamNode*)node;
            if (a->object && a->key) {
                AstCowPath path = {};
                MvpBinding* b = ast_collect_cow_path(&path, a->object) ? mvp_local_binding(m, path.root) : NULL;
                if (!b || (!mvp_array_element(b->contract) && b->rep != VALUE_REP_ITEM &&
                        !mvp_array_contract(b->contract) && !ast_declared_type_is_map(b->contract)) || b->owner != owner) {
                    char reason[160];
                    snprintf(reason, sizeof(reason), "indexed write place: binding=%s type=%d kind=%d rep=%d owner=%u function=%u",
                        b ? b->entry->name->chars : "missing", b ? b->contract->type_id : -1,
                        b ? b->contract->kind : -1, b ? b->rep : -1, b ? b->owner : AST_FUNCTION_ID_INVALID, owner);
                    return mvp_reject(m, node, reason);
                }
            } else if (!a->target_entry || a->object || a->key)
                return mvp_reject(m, node, "assignment requires a resolved local binding");
            break;
        }
        case AST_NODE_INDEX_EXPR:
            break;
        case AST_NODE_MAP: break;
        case AST_NODE_ARRAY: {
            break;
        }
        case AST_NODE_FOR_EXPR: {
            AstForNode* f = (AstForNode*)node;
            AstLoopNode* loop = (AstLoopNode*)f->loop;
            AstNode* source = loop ? ast_unwrap_primary_to_leaf(loop->as) : NULL;
            if (!loop || loop->next || loop->on || loop->optional || f->let_clause || f->limit || f->offset || !source)
                return mvp_reject(m, node, "for requires one source without grouping or ordering");
            if (MvpBinding* key = mvp_binding(m, loop->index_entry)) {
                key->owner = owner; key->contract = key->admission_contract = &TYPE_ANY;
                key->rep = VALUE_REP_ITEM; key->iteration_binding = true;
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
            if (!admitted_call(m, (AstCallNode*)node)) return false;
            break;
        case AST_NODE_BINARY: {
            Operator op = ((AstBinaryNode*)node)->op;
            if (op == OPERATOR_TO) {
                AstNodeId parent = ast_index_parent_id(index, i);
                while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
                    parent = ast_index_parent_id(index, parent);
                if (parent < index->count && index->nodes[parent]->node_type == AST_NODE_FOR_CLAUSE) break;
            }
            if (op != OPERATOR_ADD && op != OPERATOR_SUB && op != OPERATOR_MUL &&
                op != OPERATOR_DIV && op != OPERATOR_IDIV && op != OPERATOR_MOD &&
                op != OPERATOR_EQ && op != OPERATOR_NE && op != OPERATOR_LT &&
                op != OPERATOR_LE && op != OPERATOR_GT && op != OPERATOR_GE &&
                op != OPERATOR_AND && op != OPERATOR_OR && op != OPERATOR_JOIN && op != OPERATOR_IS &&
                op != OPERATOR_AT && op != OPERATOR_IN && op != OPERATOR_OF && op != OPERATOR_TO && op != OPERATOR_IS_NAN)
                return mvp_reject(m, node, "binary operator is not admitted yet");
            break;
        }
        case AST_NODE_UNARY: {
            Operator op = ((AstUnaryNode*)node)->op;
            if (op != OPERATOR_NEG && op != OPERATOR_NOT && op != OPERATOR_POS && op != OPERATOR_PROPAGATE)
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
            b->scalar_snapshot = element && mir_confined_scalar_snapshot(index, f->ast, param, position,
                element ? element->type_id : LMD_TYPE_ANY, [](AstNode* key, void* data) -> bool {
                    LambdaMvpModule* m = (LambdaMvpModule*)data;
                    bool optional = false;
                    Type* type = lambda_type_nullable_lane_base(
                        lambda_type_remove_error(m->tp->pool, mvp_value_contract(m, key)), &optional);
                    return type && (type->type_id == LMD_TYPE_INT || type->type_id == LMD_TYPE_FLOAT ||
                        type->type_id == LMD_TYPE_INT64);
                }, m);
            // the confined snapshot allocates a precisely rooted header and can fail.
            if (b->scalar_snapshot) f->abi.effects.may_gc = f->abi.effects.may_return_error = true;
        }
    }
    // a direct-call fixed point keeps scalar-only recursion free of GC ceremony.
    bool changed;
    do {
        changed = false;
        for (AstNodeId i = 0; i < index->count; i++) {
            AstNode* node = index->nodes[i];
            AstFunctionId owner = index->owner_functions[i];
            MvpFunction* f = owner < m->function_count ? &m->functions[owner] : &m->entry;
            bool collects = false, fails = false, reenters = false;
            if (node->node_type == AST_NODE_IDENT) {
                NameEntry* entry = ((AstIdentNode*)node)->entry;
                MvpBinding* binding = mvp_binding(m, entry);
                // context-slab reads and their return admission can collect;
                // determine this before the storage descriptors are emitted.
                if (entry && entry->node && entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                        !((AstDeclaratorNode*)entry->node)->is_type_definition &&
                        (entry->import || entry->import_owner ||
                         (context_globals && binding && binding->owner == AST_FUNCTION_ID_INVALID)))
                    collects = fails = true;
            }
            if (node->node_type == AST_NODE_ARRAY ||
                    node->node_type == AST_NODE_MATCH_EXPR ||
                    node->node_type == AST_NODE_MEMBER_ASSIGN_STAM || node->node_type == AST_NODE_INDEX_ASSIGN_STAM)
                collects = fails = true;
            if (node->node_type == AST_NODE_BINARY && ((AstBinaryNode*)node)->op == OPERATOR_IS)
                collects = true;
            if (node->node_type == AST_NODE_MAP) {
                collects = true;
                fails = contextual_map_contract(m, node) && mvp_value_contract(m, node) == &TYPE_ANY;
            }
            if (node->node_type == AST_NODE_MEMBER_EXPR) {
                collects = true;
                fails = mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
            }
            if (node->node_type == AST_NODE_INDEX_EXPR) {
                // a primitive integer-key read returns its member or absence;
                // only a consuming typed boundary can reject absence (S7.1.1v3).
                collects = true;
                fails = mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
            }
            if (node->node_type == AST_NODE_FOR_EXPR) {
                AstLoopNode* loop = (AstLoopNode*)((AstForNode*)node)->loop;
                if (loop->key_only) collects = true;
                else {
                    AstNode* source = ast_unwrap_primary_to_leaf(loop->as);
                    bool range_source = source && source->node_type == AST_NODE_BINARY && ((AstBinaryNode*)source)->op == OPERATOR_TO;
                    AstBinaryNode* range = range_source ? (AstBinaryNode*)source : NULL;
                    collects = fails = !range || !((AstForNode*)node)->discard_result || ((AstForNode*)node)->group || ((AstForNode*)node)->order ||
                        !mvp_int_range(m, range->left).finite || !mvp_int_range(m, range->right).finite;
                }
            }
            if (node->node_type == AST_NODE_CALL_EXPR) {
                AstCallNode* call = (AstCallNode*)node;
                MvpFunction* callee = mvp_function(m, ast_direct_call_function(call));
                if (callee) {
                    collects = callee->abi.effects.may_gc;
                    fails = callee->abi.effects.may_return_error;
                    reenters = callee->abi.effects.may_reenter;
                    AstNamedNode* param = callee->ast->param;
                    for (AstNode* arg = call->argument; arg; arg = arg->next, param = (AstNamedNode*)param->next)
                        fails |= !mvp_boundary_proven(mvp_value_contract(m, arg), mvp_contract(param));
                }
                else {
                    AstNode* target = ast_unwrap_primary_to_leaf(call->function);
                    SysFuncInfo* info = ((AstSysFuncNode*)target)->fn_info;
                    JitImportMetadata metadata = {};
                    TypeId input = mvp_value_contract(m, call->argument)->type_id;
                    bool native_math = mvp_native_math(info, input);
                    bool native_cast = (info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) &&
                        (input == LMD_TYPE_INT || input == LMD_TYPE_FLOAT);
                    bool known = jit_import_get_metadata(native_math ? info->native_c_name : info->c_func_name, &metadata);
                    collects = !known || metadata.gc_effect != JIT_EFFECT_NO_GC;
                    reenters = !known || metadata.reentry_effect != JIT_REENTRY_NO;
                    bool optional = false;
                    Type* base = lambda_type_nullable_lane_base(mvp_value_contract(m, call->argument), &optional);
                    bool native_int_math = base == &TYPE_INT && sysfunc_native_math_type_preserving(info->fn, NULL);
                    if (native_cast || native_int_math) collects = reenters = false;
                    fails = mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
                    fails |= call->propagate && call->propagated_error_type &&
                        lambda_type_accepts_error(call->propagated_error_type);
                }
            } else if (node->node_type == AST_NODE_BINARY &&
                    ((AstBinaryNode*)node)->op == OPERATOR_JOIN) {
                collects = true; fails = mvp_value_contract(m, node) == &TYPE_ANY;
            }
            // unresolved boundary admission may allocate an error even on a native function.
            if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
                AstDeclaratorNode* d = (AstDeclaratorNode*)node;
                MvpBinding* b = mvp_binding(m, d->entry);
                Type* source = mvp_value_contract(m, d->init);
                changed |= widen_inferred_binding(m, b, source);
                fails |= d->declared_type && !mvp_boundary_proven(source, d->declared_type);
            }
            if (node->node_type == AST_NODE_ASSIGN_STAM) {
                AstAssignStamNode* a = (AstAssignStamNode*)node;
                MvpBinding* b = mvp_binding(m, a->target_entry);
                Type* source = mvp_value_contract(m, a->value);
                changed |= widen_inferred_binding(m, b, source);
                fails |= !b || !mvp_boundary_proven(source, b->contract);
            }
            if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
                AstAssignStamNode* a = (AstAssignStamNode*)node;
                MvpBinding* b = mvp_local_binding(m, a->object);
                if (b && b->inferred_array && b->contract != &TYPE_ANY &&
                        !mvp_boundary_proven(mvp_value_contract(m, a->value), mvp_array_element(b->contract))) {
                    b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM; changed = true;
                }
            }
            if (node->node_type == AST_NODE_RETURN_STAM) {
                AstNode* v = ((AstReturnNode*)node)->value;
                fails |= !mvp_boundary_proven(mvp_value_contract(m, v), f->result_contract);
            }
            if ((node->node_type == AST_NODE_BINARY && ((AstBinaryNode*)node)->op != OPERATOR_TO) ||
                    node->node_type == AST_NODE_UNARY)
                fails |= mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
            if (node->node_type == AST_NODE_UNARY && ((AstUnaryNode*)node)->op == OPERATOR_PROPAGATE)
                fails |= lambda_type_accepts_error(mvp_value_contract(m, ((AstUnaryNode*)node)->operand));
            collects |= fails;
            if (collects && !f->abi.effects.may_gc) { f->abi.effects.may_gc = true; changed = true; }
            if (fails && !f->abi.effects.may_return_error) { f->abi.effects.may_return_error = true; changed = true; }
            if (reenters && !f->abi.effects.may_reenter) { f->abi.effects.may_reenter = true; changed = true; }
        }
    } while (changed);
    // a discharged typed constructor and a closed mutation graph cannot
    // publish an error into this owner. Keep that private clean domain while
    // leaving the shared AST's conservative acknowledgment facts unchanged.
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !b->entry->admission_may_fail || b->owner >= m->function_count ||
                !b->entry->node || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)b->entry->node;
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
        if (!b->entry || !b->entry->node || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                binding_reassigned(m, b, true) || binding_var_borrow(m, b)) continue;
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)b->entry->node;
        AstNode* source = ast_unwrap_primary_to_leaf(declaration->init);
        MvpFunction* producer = source && source->node_type == AST_NODE_CALL_EXPR
            ? mvp_function(m, ast_direct_call_function((AstCallNode*)source)) : NULL;
        Type* success = b->contract != &TYPE_ANY
            ? lambda_type_remove_error(m->tp->pool, b->contract)
            : producer ? producer->result_contract : mvp_value_contract(m, source);
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(success, &optional);
        // this is a successful-producer fact, not a new inferred write contract.
        // The unchanged local may still hold Error; consumers must classify it.
        if (base && (mvp_array_contract(base) || lambda_type_nonnull_map_contract(base) ||
                base->type_id == LMD_TYPE_FLOAT || base->type_id == LMD_TYPE_INT ||
                base->type_id == LMD_TYPE_BOOL || base->type_id == LMD_TYPE_DECIMAL))
            b->producer_success_contract = success;
    }
    if (m->main) {
        m->entry.abi.effects.may_gc |= m->main->abi.effects.may_gc;
        m->entry.abi.effects.may_reenter |= m->main->abi.effects.may_reenter;
        m->entry.abi.effects.may_return_error |= m->main->abi.effects.may_return_error;
    }
    // publish one final return ABI after recursive error effects have converged (D8.4.2v2).
    for (uint32_t i = 0; i < m->function_count; i++)
        plan_return(&m->functions[i], m->functions[i].result_contract, false);
    plan_integer_ranges(m);
    plan_float_parameters(m);
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        AstNode* object = NULL; AstNode* key = NULL;
        if (node->node_type == AST_NODE_INDEX_EXPR) {
            object = ((AstFieldNode*)node)->object; key = ((AstFieldNode*)node)->field;
        } else if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            object = ((AstAssignStamNode*)node)->object; key = ((AstAssignStamNode*)node)->key;
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
        for (AstNodeId i = 0; i < index->count; i++) {
            if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
            AstCallNode* call = (AstCallNode*)index->nodes[i];
            AstFuncNode* target = ast_direct_call_function(call);
            AstNamedNode* p = target ? target->param : NULL;
            for (AstNode* a = call->argument; a && p; a = a->next, p = (AstNamedNode*)p->next) {
                MvpBinding* source = mvp_local_binding(m, a), *formal = mvp_binding(m, p->entry);
                if (source && formal && mvp_array_element(source->contract) && formal->array_extent > source->array_extent) {
                    source->array_extent = formal->array_extent; changed = true;
                }
            }
        }
    } while (changed);
    for (AstNodeId i = 0; i < index->count; i++) {
        if (index->nodes[i]->node_type != AST_NODE_MEMBER_ASSIGN_STAM) continue;
        MvpBinding* binding = mvp_local_binding(m, ((AstAssignStamNode*)index->nodes[i])->object);
        if (binding) binding->record_written = true;
    }
    bool module_scalars = false;
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !b->entry->node || b->owner != AST_FUNCTION_ID_INVALID ||
                b->entry->is_mutable || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)b->entry->node;
        uint32_t use_count = 0;
        const AstNodeId* uses = ast_index_binding_uses(&m->tp->ast_index, i, &use_count);
        bool cross_function = false;
        for (uint32_t use = 0; use < use_count; use++)
            cross_function |= m->tp->ast_index.owner_functions[uses[use]] != AST_FUNCTION_ID_INVALID;
        if (!cross_function) continue;
        AstNode* literal = ast_unwrap_primary_to_leaf(d->init);
        if (literal && (literal->node_type == AST_NODE_ARRAY ||
                (literal->node_type == AST_NODE_PRIMARY && literal->type && literal->type->type_id == LMD_TYPE_STRING)))
            module_scalars = true;
        if (!module_scalar_initializer(m, d->init) ||
                (d->declared_type && lambda_static_boundary_relation(mvp_value_contract(m, d->init), b->contract, false) != STATIC_BOUNDARY_PROVEN) ||
                (b->rep != VALUE_REP_F64 && b->rep != VALUE_REP_INT_LANE && b->rep != VALUE_REP_I64)) continue;
        b->module_scalar = true;
        module_scalars = true;
    }
    if (context_globals) for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !b->entry->node || b->entry->import || b->entry->import_owner ||
                b->owner != AST_FUNCTION_ID_INVALID || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                ((AstDeclaratorNode*)b->entry->node)->is_type_definition) continue;
        // allocating module initializers publish Items into the context slab,
        // so later calls retain precise roots and source initialization order.
        b->module_managed = true; b->module_scalar = false;
    }
    (void)module_scalars;
    return !m->failed;
}

bool lambda_mvp_lower(LambdaMvpModule* m) {
    if (!m || m->failed) return false;
    m->module = MIR_new_module(m->ctx, "lambda_mvp");
    m->imports = em_import_cache_new(32);
    m->consts = MIR_new_bss(m->ctx, "_mod_consts_ptr", sizeof(void*));
    MIR_new_bss(m->ctx, "_mod_layout", sizeof(LambdaModuleLayout));
    MIR_new_bss(m->ctx, "_mod_type_list_ptr", sizeof(void*));
    for (uint32_t i = 0; i < m->binding_count; i++) if (m->bindings[i].module_scalar) {
        char name[40]; snprintf(name, sizeof(name), "mvp_module_scalar_%u", i);
        m->bindings[i].module_slot = MIR_new_bss(m->ctx, name, sizeof(uint64_t));
    }
    for (uint32_t i = 0; i < m->binding_count; i++) if (m->bindings[i].module_managed) {
        StrBuf* name = strbuf_new_cap(64);
        strbuf_append_str(name, "_gvar_"); strbuf_append_str(name, m->bindings[i].entry->name->chars);
        m->bindings[i].module_ref = MIR_new_bss(m->ctx, name->str, sizeof(LambdaModuleVarRef));
        strbuf_free(name);
    }
    for (uint32_t i = 0; i < m->function_count; i++)
        m->functions[i].target = MIR_new_forward(m->ctx, m->functions[i].name);
    for (int i = 0; m->imported_functions && i < m->imported_functions->length; i++) {
        MvpFunction* function = (MvpFunction*)m->imported_functions->data[i];
        function->target = MIR_new_import(m->ctx, function->name);
    }
    for (uint32_t i = 0; i < m->function_count && !m->failed; i++)
        mvp_emit_function(m, &m->functions[i]);
    if (!m->failed) mvp_emit_function(m, &m->entry);
    MIR_finish_module(m->ctx);
    return !m->failed;
}

bool lambda_mvp_finalize(LambdaMvpModule* m, LambdaMvpStats* stats) {
    if (!m || m->failed || !m->module) return false;
    mir_dump_finalized(m->ctx, host_path_dir(HOST_PATH_TEMP, "mir_dump.txt"), false);
    MIR_load_module(m->ctx, m->module);
    if (stats) *stats = m->stats;
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
    mem_free(m->bindings);
    if (m->imports) hashmap_free(m->imports);
    mem_free(m);
}
