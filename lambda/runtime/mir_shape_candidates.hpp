#pragma once

#include "ast.hpp"

// Candidates select guards, never prove a receiver's layout (D8.4.1v2).
// Frontends supply their literal/parameter/callee facts; binding traversal is
// shared because both profiles consume the builder's resolved AST identities.
struct MirShapeCandidateProfile {
    void* owner;
    void* (*direct)(void* owner, AstNode* node);
    void* (*binding)(void* owner, AstNode* definition);
    void* (*call)(void* owner, AstCallNode* call);
};

static inline void* mir_shape_candidate(const MirShapeCandidateProfile& profile,
        AstNode* expression, int depth) {
    if (!expression || depth <= 0) return NULL;
    AstNode* node = ast_unwrap_primary(expression);
    if (!node) return NULL;
    if (void* direct = profile.direct(profile.owner, node)) return direct;
    if (node->node_type == AST_NODE_IDENT) {
        NameEntry* entry = ((AstIdentNode*)node)->entry;
        AstNode* definition = entry ? entry->node : NULL;
        if (!definition) return NULL;
        if (definition->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            return mir_shape_candidate(profile,
                ((AstDeclaratorNode*)definition)->init, depth - 1);
        }
        return profile.binding(profile.owner, definition);
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        return profile.call(profile.owner, (AstCallNode*)node);
    }
    return NULL;
}
