#pragma once

#include "ast.hpp"
#include "ast_build.hpp"

static inline bool mir_flow_nested_function(AstNode* node, void* data) {
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) {
        *(bool*)data = true;
        return false;
    }
    return !*(bool*)data;
}

static inline bool mir_body_has_nested_function(AstNode* body) {
    bool nested = false;
    walk_lambda_ast(body, mir_flow_nested_function, &nested, false);
    return nested;
}

static inline AstNode* mir_index_use_parent(const AstIndex* index, AstNode** use) {
    AstNodeId id = ast_index_find(index, *use);
    AstNode* parent = id == AST_NODE_ID_INVALID ? NULL : index->parents[id];
    while (parent && parent->node_type == AST_NODE_PRIMARY) {
        *use = parent;
        id = ast_index_find(index, parent);
        if (id == AST_NODE_ID_INVALID) return NULL;
        parent = index->parents[id];
    }
    return parent;
}

// The only whole-value transfers allowed are to the same recursive formal.
// A proven scalar key cannot create a view; every other alias/escape keeps COW.
static inline bool mir_confined_scalar_snapshot(const AstIndex* index, AstFuncNode* function,
        AstNamedNode* param, int position, TypeId element,
        bool (*scalar_key)(AstNode*, void*), void* context) {
    if (element != LMD_TYPE_ANY && element != LMD_TYPE_INT && element != LMD_TYPE_BOOL)
        return false;
    TypeParam* contract = param ? lambda_type_param(param->type) : NULL;
    NameEntry* binding = param ? param->entry : NULL;
    if (!index || !function || function->captures || function->is_async ||
            function->is_generator || function->is_loop_continuation ||
            !binding || !binding->cow_param_mutated || binding->is_var_param ||
            binding->view_base || (contract && (contract->is_var_param || contract->default_value)) ||
            mir_body_has_nested_function(function->body)) return false;
    AstBindingId id = ast_index_find_binding_entry(index, binding);
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index, id, &count);
    if (!uses || !count) return false;
    bool recursive_transfer = false;
    for (uint32_t i = 0; i < count; i++) {
        AstNode* original = index->nodes[uses[i]];
        if (original == binding->node) continue;
        AstNode* use = original;
        AstNode* parent = mir_index_use_parent(index, &use);
        if (parent == binding->node) continue;
        AstNode* key = NULL;
        if (parent && parent->node_type == AST_NODE_INDEX_EXPR &&
                ast_unwrap_primary(((AstFieldNode*)parent)->object) == original) {
            key = ((AstFieldNode*)parent)->field;
        } else if (parent && parent->node_type == AST_NODE_INDEX_ASSIGN_STAM &&
                ast_unwrap_primary(((AstCompoundAssignNode*)parent)->object) == original) {
            key = ((AstCompoundAssignNode*)parent)->key;
        }
        if (key && !key->next) {
            if (scalar_key(key, context)) continue;
        }
        if (!parent || parent->node_type != AST_NODE_CALL_EXPR) return false;
        AstCallNode* call = (AstCallNode*)parent;
        AstNode* callee = ast_unwrap_primary(call->function);
        if (callee && callee->node_type == AST_NODE_SYS_FUNC &&
                ((AstSysFuncNode*)callee)->fn_info &&
                ((AstSysFuncNode*)callee)->fn_info->fn == SYSFUNC_LEN &&
                call->argument == use && !use->next) continue;
        if (ast_direct_call_function(call) != function) return false;
        AstNode* arguments[LAMBDA_MAX_FUNCTION_ARGS] = {};
        ast_resolve_call_arguments(call, function, ast_linked_node_count(call->argument), arguments);
        if (arguments[position] != use) return false;
        recursive_transfer = true;
    }
    struct Rebind { NameEntry* binding; bool valid; } rebind = {binding, true};
    walk_lambda_ast(function->body, [](AstNode* node, void* data) -> bool {
        Rebind* rebind = (Rebind*)data;
        if (node->node_type == AST_NODE_ASSIGN_STAM &&
                ((AstAssignStamNode*)node)->target_entry == rebind->binding) rebind->valid = false;
        return rebind->valid;
    }, &rebind, false);
    // Nonrecursive activations retain the existing lazy first-write snapshot.
    // The extra header/copy pays for avoiding repeated recursive detachments.
    return recursive_transfer && rebind.valid;
}
