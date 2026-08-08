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
        struct hashmap* scope = jm_var_scope_at(mt, sd);
        if (!scope) continue;
        size_t iter = 0;
        void* item;
        while (hashmap_iter(scope, &iter, &item)) {
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
        struct hashmap* scope = jm_var_scope_at(mt, sd);
        if (!scope) continue;
        size_t iter = 0;
        void* item;
        while (hashmap_iter(scope, &iter, &item)) {
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
        JsTryContext* context = jm_try_context_at(mt, td);
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
        if (context->saved_error_lane_flag_reg) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->saved_error_lane_flag_reg),
                MIR_new_int_op(mt->ctx, 0)));
        }
        if (context->saved_error_lane_val_reg) {
            MIR_reg_t null_value = jm_emit_null(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->saved_error_lane_val_reg),
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
        JsTryContext* context = jm_try_context_at(mt, depth);
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
        // An await rejection follows the ordinary abrupt path: a catch handles
        // it first, while try/finally without catch must enter finally so that
        // the saved rejection remains pending until cleanup completes.
        // This keeps the suspended continuation from being marked clean before
        // its pending rejection has reached the enclosing completion handler.
        return context->has_catch ? context->catch_label :
            (context->has_finally ? context->finally_label : 0);
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

JsErrorLaneTrack jm_error_lane_state(JsMirTranspiler* mt) {
    return mt ? mt->error_lane_track : JS_ERROR_LANE_UNKNOWN;
}

void jm_error_lane_set_state(JsMirTranspiler* mt, JsErrorLaneTrack state) {
    if (!mt) return;
    mt->error_lane_track = state;
}

JsErrorLaneTrack jm_error_lane_merge(JsErrorLaneTrack a, JsErrorLaneTrack b) {
    if (a == JS_ERROR_LANE_UNREACHABLE) return b;
    if (b == JS_ERROR_LANE_UNREACHABLE) return a;
    if (a == b && (a == JS_ERROR_LANE_CLEAN || a == JS_ERROR_LANE_SET)) return a;
    return JS_ERROR_LANE_UNKNOWN;
}

void jm_error_lane_note_call(JsMirTranspiler* mt, JitExceptionEffect effect) {
    if (!mt || mt->error_lane_track == JS_ERROR_LANE_UNREACHABLE) return;
    switch (effect) {
    case JIT_EXCEPTION_PRESERVES:
        return;
    case JIT_EXCEPTION_CLEARS:
        mt->error_lane_track = JS_ERROR_LANE_CLEAN;
        return;
    case JIT_EXCEPTION_SETS:
        mt->error_lane_track = JS_ERROR_LANE_SET;
        return;
    case JIT_EXCEPTION_MAY_SET:
    default:
        mt->error_lane_track = JS_ERROR_LANE_UNKNOWN;
        return;
    }
}

static MIR_label_t jm_error_lane_route_target(JsMirTranspiler* mt,
        JsMirCompletionKind kind, bool include_end_label) {
    if (!mt) return 0;
    JsTryContext* context = jm_find_completion_context(mt, kind);
    MIR_label_t target = jm_completion_target(context, kind, include_end_label);
    if (!target && (kind == JS_MIR_COMPLETION_THROW ||
            kind == JS_MIR_COMPLETION_AWAIT_REJECTION) && !context) {
        // A rejected await in a generator with no local try handler still
        // needs the function-level error lane; otherwise its resume label is
        // incorrectly marked clean while the rejection is pending.
        if (!mt->func_error_lane_label) mt->func_error_lane_label = jm_new_label(mt);
        target = mt->func_error_lane_label;
    }
    return target;
}

static MIR_reg_t jm_emit_error_lane_const(JsMirTranspiler* mt, int64_t value,
        const char* name) {
    MIR_reg_t result = jm_new_reg(mt, name, MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, value)));
    return result;
}

static void jm_capture_routed_error_lane(JsMirTranspiler* mt, JsTryContext* context) {
    if (!mt) return;
    MIR_reg_t value = mt->last_call_result_reg;
    if (!context) {
        if (!value) {
            // Every fallible call publishes its merged lane. Reaching a
            // function exit without one is a lowering invariant violation,
            // but still preserve a valid error lane for the caller.
            value = em_call_1(&mt->em, "js_throw_value", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_emit_null(mt)), true);
        }
        if (!mt->func_error_lane_value_reg) {
            mt->func_error_lane_value_reg = jm_new_reg(mt, "_func_error_lane", MIR_T_I64);
        }
        // function-level exits are emitted after normal-path cleanup, so the
        // result must live in a dedicated register rather than the transient
        // last-call slot.
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, mt->func_error_lane_value_reg),
            MIR_new_reg_op(mt->ctx, value)));
        return;
    }
    if (value) {
        if (!context->incoming_error_lane_val_reg) {
            context->incoming_error_lane_val_reg = jm_new_reg(mt, "_try_exc", MIR_T_I64);
        }
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, context->incoming_error_lane_val_reg),
            MIR_new_reg_op(mt->ctx, value)));
        return;
    }
    // Every fallible call publishes its merged lane in the result register;
    // reaching this edge without one is a lowering invariant violation.
    MIR_reg_t fallback = em_call_1(&mt->em, "js_throw_value",
        MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_emit_null(mt)), true);
    if (!context->incoming_error_lane_val_reg) {
        context->incoming_error_lane_val_reg = jm_new_reg(mt, "_try_exc", MIR_T_I64);
    }
    // keep the context register stable: catch lowering retains the original
    // register, so a fallback value must be written into it rather than
    // replacing the context field after catch code has been emitted.
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, context->incoming_error_lane_val_reg),
        MIR_new_reg_op(mt->ctx, fallback)));
}

MIR_reg_t jm_emit_error_lane_return(JsMirTranspiler* mt) {
    if (!mt) return 0;
    if (mt->try_ctx_depth == 0 && mt->func_error_lane_value_reg) {
        return mt->func_error_lane_value_reg;
    }
    // A routed try edge records the exact ERROR Item before cleanup emits any
    // further calls; rethrow that carrier instead of whichever call happened
    // to be emitted last while closing the iterator.
    for (int depth = mt->try_ctx_depth - 1; depth >= 0; depth--) {
        JsTryContext* context = jm_try_context_at(mt, depth);
        if (context && context->incoming_error_lane_val_reg) {
            return context->incoming_error_lane_val_reg;
        }
    }
    if (mt->last_call_result_reg) return mt->last_call_result_reg;
    MIR_reg_t null_value = jm_emit_null(mt);
    // Exception exits preserve the last boxed helper result.  The fallback is
    // only for an impossible hand-written lowering edge and creates the same
    // valid LambdaError lane instead of manufacturing a null-plus-flag state.
    return em_call_1(&mt->em, "js_throw_value", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, null_value), true);
}

MIR_reg_t jm_arg_frame_base(JsMirTranspiler* mt) {
    if (!mt || !mt->em.frame.active || !mt->em.frame.root_base) {
        log_error("js-mir arg-frame invariant: base without active root frame");
        abort();
    }
    if (mt->arg_frame_base) return mt->arg_frame_base;
    mt->arg_frame_base = jm_new_reg(mt, "js_arg_frame", MIR_T_I64);
    mt->arg_frame_base_add = MIR_new_insn(mt->ctx, MIR_ADD,
        MIR_new_reg_op(mt->ctx, mt->arg_frame_base),
        MIR_new_reg_op(mt->ctx, mt->em.frame.root_base),
        MIR_new_int_op(mt->ctx, 0));
    // The semantic-root count is known only after liveness coloring. Keep one
    // entry add and patch its displacement when the complete frame is fixed.
    MIR_insert_insn_after(mt->ctx, mt->em.func_item,
        mt->em.frame.anchor, mt->arg_frame_base_add);
    return mt->arg_frame_base;
}

void jm_emit_arg_frame_clear(JsMirTranspiler* mt, JsMirArgStackScope* scope) {
    if (!mt || !scope || scope->base_slot < 0 || scope->slot_count <= 0) return;
    MIR_reg_t base = jm_arg_frame_base(mt);
    for (int i = 0; i < scope->slot_count; i++) {
        int slot = scope->base_slot + i;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                slot * (int)sizeof(uint64_t), base, 0, 1),
            MIR_new_int_op(mt->ctx, 0)));
    }
}

static bool jm_has_active_arg_frame(JsMirTranspiler* mt) {
    for (JsMirArgStackScope* scope = mt ? mt->arg_stack_scope : NULL;
            scope; scope = scope->parent) {
        if (scope->base_slot >= 0 && scope->slot_count > 0) return true;
    }
    return false;
}

static void jm_clear_active_arg_frames(JsMirTranspiler* mt) {
    for (JsMirArgStackScope* scope = mt ? mt->arg_stack_scope : NULL;
            scope; scope = scope->parent) {
        jm_emit_arg_frame_clear(mt, scope);
    }
}

MIR_reg_t jm_emit_error_lane_test(JsMirTranspiler* mt) {
    if (!mt) return 0;
    switch (jm_error_lane_state(mt)) {
    case JS_ERROR_LANE_CLEAN:
        return jm_emit_error_lane_const(mt, 0, "exc_clean");
    case JS_ERROR_LANE_SET:
        return jm_emit_error_lane_const(mt, 1, "exc_set");
    case JS_ERROR_LANE_UNREACHABLE:
        return jm_emit_error_lane_const(mt, 0, "exc_dead");
    case JS_ERROR_LANE_UNKNOWN:
    default:
        if (mt->last_call_result_reg) {
            MIR_reg_t tag = jm_new_reg(mt, "exc_tag", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_URSH,
                MIR_new_reg_op(mt->ctx, tag),
                MIR_new_reg_op(mt->ctx, mt->last_call_result_reg),
                MIR_new_int_op(mt->ctx, 56)));
            MIR_reg_t is_error = jm_new_reg(mt, "exc_inband", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                MIR_new_reg_op(mt->ctx, is_error),
                MIR_new_reg_op(mt->ctx, tag),
                MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));
            return is_error;
        }
        // Void fallible helpers are forbidden by the Tune1 catalog; an
        // unknown edge with no result is therefore clean after the sweep.
        return jm_emit_error_lane_const(mt, 0, "exc_no_result");
    }
}

void jm_emit_error_lane_route(JsMirTranspiler* mt, JsMirCompletionKind kind) {
    if (!mt) return;
    JsTryContext* route_context = jm_find_completion_context(mt, kind);
    MIR_label_t target = jm_error_lane_route_target(mt, kind, false);
    if (!target) return;
    switch (jm_error_lane_state(mt)) {
    case JS_ERROR_LANE_CLEAN:
        return;
    case JS_ERROR_LANE_UNREACHABLE:
        return;
    case JS_ERROR_LANE_SET: {
        jm_capture_routed_error_lane(mt, route_context);
        jm_clear_active_arg_frames(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, target)));
        return;
    }
    case JS_ERROR_LANE_UNKNOWN:
    default:
        break;
    }
    MIR_reg_t exception = jm_emit_error_lane_test(mt);
    if (jm_has_active_arg_frame(mt)) {
        MIR_label_t clean_path = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, clean_path),
            MIR_new_reg_op(mt->ctx, exception)));
        // Capture only on the exceptional edge. Emitting the carrier creation
        // before this branch would allocate a synthetic null exception on
        // every normal call and contaminate the merged lane.
        jm_capture_routed_error_lane(mt, route_context);
        // Fixed argument slots stay inside the function frame, but their
        // call-expression lifetime still ends on a caught exceptional edge.
        jm_clear_active_arg_frames(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, target)));
        jm_emit_label_with_state(mt, clean_path, JS_ERROR_LANE_CLEAN);
    } else {
        MIR_label_t clean_path = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, clean_path),
            MIR_new_reg_op(mt->ctx, exception)));
        // Capture only after the tag test proves this edge exceptional; the
        // normal path must not manufacture a discarded ERROR carrier.
        jm_capture_routed_error_lane(mt, route_context);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, target)));
        jm_emit_label_with_state(mt, clean_path, JS_ERROR_LANE_CLEAN);
    }
}

void jm_emit_error_lane_guard(JsMirTranspiler* mt, MIR_label_t target) {
    if (!mt || !target) return;
    switch (jm_error_lane_state(mt)) {
    case JS_ERROR_LANE_CLEAN:
        return;
    case JS_ERROR_LANE_UNREACHABLE:
        return;
    case JS_ERROR_LANE_SET:
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, target)));
        return;
    case JS_ERROR_LANE_UNKNOWN:
    default:
        break;
    }
    MIR_reg_t exception = jm_emit_error_lane_test(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, target),
        MIR_new_reg_op(mt->ctx, exception)));
    jm_error_lane_set_state(mt, JS_ERROR_LANE_CLEAN);
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

static void jm_emit_throw_completion_impl(JsMirTranspiler* mt, MIR_reg_t value,
        JsTryContext* forced_context, bool force_finally) {
    if (!mt) return;
    MIR_reg_t thrown = jm_call_1(mt, "js_throw_value", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
    JsTryContext* context = forced_context ? forced_context :
        jm_find_completion_context(mt, JS_MIR_COMPLETION_THROW);
    MIR_label_t target;
    if (force_finally) {
        // A generator resume must enter the same enclosing completion lane as
        // a source throw: finally first when present, otherwise the catch
        // handler.  Selecting only finally made injected throws escape
        // try/catch blocks that had no finally clause.
        target = context ? (context->has_finally ? context->finally_label :
            context->catch_label) : 0;
    } else {
        target = jm_completion_target(context, JS_MIR_COMPLETION_THROW, true);
    }
    if (target) {
        jm_capture_routed_error_lane(mt, context);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, target)));
        return;
    }
    MIR_reg_t native_value = jm_native_return_reg(mt, thrown);
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, native_value)));
}

void jm_emit_throw_completion(JsMirTranspiler* mt, MIR_reg_t value) {
    jm_emit_throw_completion_impl(mt, value, NULL, false);
}

void jm_emit_generator_throw_completion(JsMirTranspiler* mt, MIR_reg_t value) {
    // The resume edge is emitted while the source try stack is being lowered,
    // but the state-machine body later executes after that stack is unwound.
    // Capture the enclosing finally now so an injected throw follows the same
    // completion path as a source throw at the suspended yield.
    JsTryContext* context = jm_find_completion_context(mt, JS_MIR_COMPLETION_THROW);
    jm_emit_throw_completion_impl(mt, value, context, true);
}

void jm_emit_error_lane_exit(JsMirTranspiler* mt) {
    if (!mt) return;
    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
}

void jm_emit_abrupt_jump_cleanup(JsMirTranspiler* mt) {
    for (int t = mt->try_ctx_depth - 1; t >= 0; t--) {
        JsTryContext* tc = jm_try_context_at(mt, t);
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
        JsLoopLabels* loop = jm_loop_label_at(mt, i);
        if (loop && loop->iterator_to_close) {
            jm_emit_iterator_close(mt, loop->iterator_to_close);
        }
    }
}

void jm_emit_break_completion(JsMirTranspiler* mt, JsBreakContinueNode* brk) {
    jm_emit_abrupt_jump_cleanup(mt);
    if (brk->label && brk->label_len > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            JsLoopLabels* loop = jm_loop_label_at(mt, i);
            if (loop && loop->label_name &&
                loop->label_name_len == brk->label_len &&
                memcmp(loop->label_name, brk->label, brk->label_len) == 0) {
                jm_emit_close_intervening_iterators(mt, i);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, loop->break_label)));
                break;
            }
        }
    } else if (mt->loop_depth > 0) {
        JsLoopLabels* loop = jm_loop_label_at(mt, mt->loop_depth - 1);
        if (loop) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, loop->break_label)));
        }
    }
}

void jm_emit_continue_completion(JsMirTranspiler* mt, JsBreakContinueNode* cont) {
    jm_emit_abrupt_jump_cleanup(mt);
    if (cont->label && cont->label_len > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            JsLoopLabels* loop = jm_loop_label_at(mt, i);
            if (loop && loop->label_name &&
                loop->label_name_len == cont->label_len &&
                memcmp(loop->label_name, cont->label, cont->label_len) == 0) {
                if (loop->continue_label) {
                    jm_emit_close_intervening_iterators(mt, i);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                        MIR_new_label_op(mt->ctx, loop->continue_label)));
                }
                break;
            }
        }
    } else if (mt->loop_depth > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            JsLoopLabels* loop = jm_loop_label_at(mt, i);
            if (loop && loop->continue_label) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, loop->continue_label)));
                break;
            }
        }
    }
}
