#pragma once

#include "ast.hpp"
#include "mir_emitter_shared.hpp"

// the shared AST proves a store-back region; runtime ancestor sharing decides
// whether its initial child can be borrowed or needs a snapshot (D5.4.4).
static inline MIR_reg_t em_bind_rmw_handle(MirEmitter* em, MIR_reg_t owner,
        MIR_reg_t child, int links, const MIR_reg_t* keys, bool specialize_root = false) {
    // the deeper helper can materialize indexed ancestors; only a proven
    // zero-link call selects the audited noncollecting entry.
    if (links == 0 && specialize_root) return em_call_2(em, "cow_bind_rmw_root", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, owner),
        MIR_T_I64, MIR_new_reg_op(em->ctx, child), true);
    return em_call_5(em, "cow_bind_rmw_handle", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, owner),
        MIR_T_I64, MIR_new_reg_op(em->ctx, child),
        MIR_T_I64, MIR_new_int_op(em->ctx, links),
        MIR_T_I64, MIR_new_reg_op(em->ctx, keys[0]),
        MIR_T_I64, MIR_new_reg_op(em->ctx, keys[1]), true);
}

// static path syntax has no evaluated operands; both emitters use the same
// constructors and the activation's owning pool (D2.3.5, D5.1.2).
static inline MIR_reg_t em_literal_path(MirEmitter* em, AstPathNode* node) {
    MIR_reg_t pool = em_load_at(em, em->frame.runtime, offsetof(Context, pool), MIR_T_P, "path_pool");
    MIR_reg_t path = node->authority
        ? em_call_3(em, "path_new_authority", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, pool),
            MIR_T_I64, MIR_new_int_op(em->ctx, node->scheme), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)node->authority->chars), true)
        : em_call_2(em, "path_new", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, pool),
            MIR_T_I64, MIR_new_int_op(em->ctx, node->scheme), true);
    for (int i = 0; i < node->segment_count; i++) {
        AstPathSegment* part = &node->segments[i];
        const char* unary = part->type == LPATH_SEG_WILDCARD ? "path_wildcard" :
            part->type == LPATH_SEG_WILDCARD_REC ? "path_wildcard_recursive" :
            part->type == LPATH_SEG_PARENT ? "path_select_parent" :
            part->type == LPATH_SEG_ROOT ? "path_select_root" : NULL;
        if (unary) path = em_call_2(em, unary, MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, pool),
            MIR_T_P, MIR_new_reg_op(em->ctx, path), true);
        else path = em_call_3(em, part->type == LPATH_SEG_INT ? "path_extend_int" : "path_extend", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(em->ctx, pool), MIR_T_P, MIR_new_reg_op(em->ctx, path),
            part->type == LPATH_SEG_INT ? MIR_T_I64 : MIR_T_P,
            MIR_new_int_op(em->ctx, part->type == LPATH_SEG_INT ? part->int_value : (intptr_t)(part->name ? part->name->chars : "")), true);
    }
    return path;
}
