#include "js_mir_internal.hpp"

// ============================================================================
// Completion-style MIR helpers
// ============================================================================

static const char* jm_suspend_kind_name(JsMirSuspendKind kind) {
    switch (kind) {
    case JS_MIR_SUSPEND_YIELD: return "yield";
    case JS_MIR_SUSPEND_AWAIT: return "await";
    case JS_MIR_SUSPEND_IMPLICIT_AWAIT: return "implicit await";
    }
    return "suspend";
}

int jm_next_resume_state(JsMirTranspiler* mt, JsMirSuspendKind kind) {
    if (!mt) return -1;
    int next_state = ++mt->gen_yield_index;
    if (next_state > mt->gen_yield_count || next_state >= 64 ||
        !mt->gen_state_labels[next_state]) {
        log_error("js-mir resume-state: %s index %d exceeds allocated labels (%d)",
            jm_suspend_kind_name(kind), next_state, mt->gen_yield_count);
        return -1;
    }
    return next_state;
}

void jm_emit_suspend_env_save(JsMirTranspiler* mt) {
    if (!mt || !mt->gen_env_reg) return;
    for (int sd = 1; sd <= mt->scope_depth; sd++) {
        if (!mt->var_scopes[sd]) continue;
        size_t iter = 0;
        void* item;
        while (hashmap_iter(mt->var_scopes[sd], &iter, &item)) {
            JsVarScopeEntry* entry = (JsVarScopeEntry*)item;
            if (!entry->var.from_env && entry->var.mir_type == MIR_T_I64) {
                if (mt->gen_local_slot_count >= mt->gen_dynamic_slot_limit) {
                    log_error("js-mir suspend env: dynamic binding slots exhausted before spill region");
                    continue;
                }
                // The name prepass cannot distinguish lexical shadows; give the
                // exact active binding its own suspend home before first yield.
                entry->var.from_env = true;
                entry->var.env_slot = mt->gen_local_slot_count++;
                entry->var.env_reg = mt->gen_env_reg;
            }
            if (entry->var.env_slot < 0 || !entry->var.from_env) continue;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    entry->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, entry->var.reg)));
        }
    }
}

void jm_emit_resume_env_restore(JsMirTranspiler* mt) {
    if (!mt || !mt->gen_env_reg) return;
    for (int sd = 1; sd <= mt->scope_depth; sd++) {
        if (!mt->var_scopes[sd]) continue;
        size_t iter = 0;
        void* item;
        while (hashmap_iter(mt->var_scopes[sd], &iter, &item)) {
            JsVarScopeEntry* entry = (JsVarScopeEntry*)item;
            if (entry->var.env_slot < 0 || !entry->var.from_env) continue;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, entry->var.reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    entry->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
        }
    }
}

void jm_emit_try_state_reset(JsMirTranspiler* mt) {
    if (!mt) return;
    for (int td = 0; td < mt->try_ctx_depth; td++) {
        JsTryContext* context = &mt->try_ctx_stack[td];
        if (context->has_return_reg) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->has_return_reg),
                MIR_new_int_op(mt->ctx, 0)));
        }
        if (context->return_val_reg) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->return_val_reg),
                MIR_new_int_op(mt->ctx, 0)));
        }
        if (context->saved_exc_flag_reg) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->saved_exc_flag_reg),
                MIR_new_int_op(mt->ctx, 0)));
        }
        if (context->saved_exc_val_reg) {
            MIR_reg_t null_value = jm_emit_null(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->saved_exc_val_reg),
                MIR_new_reg_op(mt->ctx, null_value)));
        }
    }
}

void jm_emit_async_resume_refresh(JsMirTranspiler* mt) {
    if (!mt) return;
    jm_scope_env_reload_vars(mt);
    jm_env_reload_shared_captures(mt);
}

JsTryContext* jm_find_completion_context(JsMirTranspiler* mt, JsMirCompletionKind kind) {
    if (!mt) return NULL;
    for (int depth = mt->try_ctx_depth - 1; depth >= 0; depth--) {
        JsTryContext* context = &mt->try_ctx_stack[depth];
        if (context->yield_state_only) continue;
        if (kind == JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL && !context->has_finally) {
            continue;
        }
        return context;
    }
    return NULL;
}

static MIR_label_t jm_completion_target(JsTryContext* context, JsMirCompletionKind kind,
        bool include_end_label) {
    if (!context) return 0;
    switch (kind) {
    case JS_MIR_COMPLETION_AWAIT_REJECTION:
        return context->catch_label;
    case JS_MIR_COMPLETION_RETURN:
    case JS_MIR_COMPLETION_RETURN_THROUGH_CLEANUP:
        return context->has_finally ? context->finally_label : context->end_label;
    case JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL:
        return context->finally_label;
    case JS_MIR_COMPLETION_THROW:
        if (context->has_catch) return context->catch_label;
        if (context->has_finally) return context->finally_label;
        return include_end_label ? context->end_label : 0;
    }
    return 0;
}

void jm_emit_pending_exception_check(JsMirTranspiler* mt, JsMirCompletionKind kind) {
    if (!mt) return;
    JsTryContext* context = jm_find_completion_context(mt, kind);
    MIR_label_t target = jm_completion_target(context, kind, false);
    if (!target && kind == JS_MIR_COMPLETION_THROW && !context) {
        if (!mt->func_except_label) mt->func_except_label = jm_new_label(mt);
        target = mt->func_except_label;
    }
    if (!target) return;
    MIR_reg_t exception = jm_call_0(mt, "js_check_exception", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, target),
        MIR_new_reg_op(mt->ctx, exception)));
}

bool jm_emit_delayed_return_completion(JsMirTranspiler* mt, MIR_reg_t value,
        JsMirCompletionKind kind) {
    if (!mt || (kind != JS_MIR_COMPLETION_RETURN &&
        kind != JS_MIR_COMPLETION_RETURN_THROUGH_CLEANUP &&
        kind != JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL)) {
        return false;
    }
    JsTryContext* context = jm_find_completion_context(mt, kind);
    if (!context || !context->return_val_reg || !context->has_return_reg) return false;
    if (kind == JS_MIR_COMPLETION_RETURN && mt->in_generator && !context->has_finally) {
        return false;
    }
    MIR_label_t target = jm_completion_target(context, kind, true);
    if (!target) return false;
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, context->return_val_reg),
        MIR_new_reg_op(mt->ctx, value)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, context->has_return_reg),
        MIR_new_int_op(mt->ctx, 1)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, target)));
    return true;
}

MIR_reg_t jm_native_return_reg(JsMirTranspiler* mt, MIR_reg_t value) {
    if (!mt || !mt->in_native_func || !mt->current_fc) return value;
    if (mt->current_fc->return_type != LMD_TYPE_FLOAT) return value;
    MIR_type_t value_type = MIR_reg_type(mt->ctx, value, mt->em.func);
    if (value_type == MIR_T_D) return value;
    // Delayed completions use boxed I64 slots, so native float returns must unbox here.
    return jm_emit_unbox_float(mt, value);
}

void jm_emit_throw_completion(JsMirTranspiler* mt, MIR_reg_t value) {
    if (!mt) return;
    jm_call_void_1(mt, "js_throw_value", MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
    JsTryContext* context = jm_find_completion_context(mt, JS_MIR_COMPLETION_THROW);
    MIR_label_t target = jm_completion_target(context, JS_MIR_COMPLETION_THROW, true);
    if (target) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, target)));
        return;
    }
    MIR_reg_t null_value = jm_emit_null(mt);
    MIR_reg_t native_value = jm_native_return_reg(mt, null_value);
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, native_value)));
}

void jm_emit_pending_exception_exit(JsMirTranspiler* mt) {
    if (!mt) return;
    MIR_reg_t pending = jm_call_0(mt, "js_check_exception", MIR_T_I64);
    MIR_label_t no_exception = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, no_exception), MIR_new_reg_op(mt->ctx, pending)));
    JsTryContext* context = jm_find_completion_context(mt, JS_MIR_COMPLETION_THROW);
    MIR_label_t target = jm_completion_target(context, JS_MIR_COMPLETION_THROW, true);
    if (target) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, target)));
    } else {
        MIR_reg_t null_value = jm_emit_null(mt);
        MIR_reg_t native_value = jm_native_return_reg(mt, null_value);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, native_value)));
    }
    jm_emit_label(mt, no_exception);
}

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
