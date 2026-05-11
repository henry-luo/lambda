#include "js_mir_internal.hpp"

// ============================================================================
// Completion-style MIR helpers
// ============================================================================

void jm_emit_abrupt_jump_cleanup(JsMirTranspiler* mt) {
    for (int t = mt->try_ctx_depth - 1; t >= 0; t--) {
        JsTryContext* tc = &mt->try_ctx_stack[t];
        if (tc->has_finally && tc->finally_body && !tc->inlining_finally &&
            tc->finally_body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            tc->inlining_finally = true;
            JsBlockNode* fin = (JsBlockNode*)tc->finally_body;
            JsAstNode* fs = fin->statements;
            while (fs) {
                jm_transpile_statement(mt, fs);
                fs = fs->next;
            }
            tc->inlining_finally = false;
        }
    }

    for (int w = 0; w < mt->with_depth; w++) {
        jm_call_void_0(mt, "js_with_pop");
    }
}



static void jm_emit_close_intervening_iterators(JsMirTranspiler* mt, int target_index) {
    for (int i = mt->loop_depth - 1; i > target_index; i--) {
        if (mt->loop_stack[i].iterator_to_close) {
            jm_emit_iterator_close(mt, mt->loop_stack[i].iterator_to_close);
        }
    }
}

void jm_emit_break_completion(JsMirTranspiler* mt, JsBreakContinueNode* brk) {
    jm_emit_abrupt_jump_cleanup(mt);
    if (brk->label && brk->label_len > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            if (mt->loop_stack[i].label_name &&
                mt->loop_stack[i].label_name_len == brk->label_len &&
                memcmp(mt->loop_stack[i].label_name, brk->label, brk->label_len) == 0) {
                jm_emit_close_intervening_iterators(mt, i);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->loop_stack[i].break_label)));
                break;
            }
        }
    } else if (mt->loop_depth > 0) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
    }
}

void jm_emit_continue_completion(JsMirTranspiler* mt, JsBreakContinueNode* cont) {
    jm_emit_abrupt_jump_cleanup(mt);
    if (cont->label && cont->label_len > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            if (mt->loop_stack[i].label_name &&
                mt->loop_stack[i].label_name_len == cont->label_len &&
                memcmp(mt->loop_stack[i].label_name, cont->label, cont->label_len) == 0) {
                if (mt->loop_stack[i].continue_label) {
                    jm_emit_close_intervening_iterators(mt, i);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                        MIR_new_label_op(mt->ctx, mt->loop_stack[i].continue_label)));
                }
                break;
            }
        }
    } else if (mt->loop_depth > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            if (mt->loop_stack[i].continue_label) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->loop_stack[i].continue_label)));
                break;
            }
        }
    }
}