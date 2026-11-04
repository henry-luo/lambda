#pragma once

#include "ast.hpp"
#include "mir_emitter_shared.hpp"

// static path syntax has no evaluated operands; both emitters use the same
// constructors and the activation's owning pool (D2.4.3, D4.1.2).
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
