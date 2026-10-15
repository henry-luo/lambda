#pragma once

#include "ast.hpp"

// ordered/windowed producers retain their ordinary materialization boundary.
inline bool ast_output_stream_for(const AstForNode* node) {
    if (!node || node->order || node->group || node->offset || node->limit || node->resume_from) return false;
    for (AstNode* item = node->loop; item; item = item->next) {
        AstLoopNode* loop = (AstLoopNode*)item;
        if (loop->on || loop->join_keys || loop->optional) return false;
    }
    return true;
}

inline bool ast_output_function(const AstFuncNode* node) {
    if (!node || node->node_type != AST_NODE_FUNC || !node->body || node->is_async || node->is_generator) return false;
    TypeFunc* type = (TypeFunc*)node->type;
    if (!type || type->is_proc || type->is_variadic || type->binder_count ||
            type->param_count != type->required_param_count ||
            (type->has_explicit_return_contract && type->return_contract != &TYPE_ANY &&
                type->return_contract != &TYPE_ANY_NO_ERROR)) return false;
    for (AstNamedNode* param = node->param; param; param = (AstNamedNode*)param->next) {
        TypeParam* slot = lambda_type_param(param->type);
        if (!slot || slot->is_var_param || slot->is_optional || slot->default_value || slot->binder) return false;
    }
    return true;
}
