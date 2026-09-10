#pragma once

#include "transpiler.hpp"

// plans retain layout owners, never a runtime receiver or a speculative type.
// JS resolves a module recipe; Lambda retains its already selected TypeMap.
struct MirConstructionPlan {
    TypeMap* static_layout;
    uint32_t recipe_id;
    uint32_t field_count;
};

struct MirFieldAccessPlan {
    MirConstructionPlan construction;
    ShapeEntry* static_field;
    int64_t byte_offset;
    int32_t slot;
};

static inline MirConstructionPlan mir_plan_static_construction(TypeMap* shape) {
    return {shape, UINT32_MAX, shape ? (uint32_t)shape->length : 0};
}

static inline bool mir_plan_static_field(TypeMap* shape, String* name,
        MirFieldAccessPlan* plan) {
    if (!shape || !name || !plan) return false;
    ShapeEntry* field = find_shape_field_by_name(shape, name->chars, name->len);
    if (!field) return false;
    *plan = {mir_plan_static_construction(shape), field, field->byte_offset, -1};
    return true;
}

// source candidates have priority; a missing field can use the module's unique
// construction candidate. Both lowerings keep their semantic admission below.
template<typename Resolve, typename Fallback>
static inline bool mir_plan_field_access(void* candidate, Resolve resolve,
        Fallback fallback, MirFieldAccessPlan* plan) {
    *plan = {};
    plan->slot = -1;
    if (candidate && resolve(candidate, plan)) return true;
    void* alternative = fallback();
    return alternative && alternative != candidate && resolve(alternative, plan);
}

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
