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
    if (next_state > mt->gen_yield_count ||
        next_state >= mt->gen_state_label_capacity ||
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
            jm_emit_store_i64(mt, entry->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, entry->var.reg);
        }
    }
    jm_emit_try_state_save(mt);
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
            jm_emit_load_i64(mt, entry->var.reg, entry->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg);
        }
    }
}

// A suspension returns from the state machine, so the delayed-return registers
// of every enclosing try are gone on resume. Park them in env slots first; a
// `return` that is waiting for a `finally` which itself yields would otherwise
// be forgotten and the generator would finish with no value.
void jm_emit_try_state_save(JsMirTranspiler* mt) {
    if (!mt || !mt->gen_env_reg) return;
    for (int td = 0; td < mt->try_ctx_depth; td++) {
        JsTryContext* context = jm_try_context_at(mt, td);
        if (!context->has_return_reg || !context->return_val_reg) continue;
        if (context->has_return_spill < 0 && context->return_val_spill < 0) {
            int has_slot = jm_gen_spill_reserve(mt);
            int val_slot = has_slot >= 0 ? jm_gen_spill_reserve(mt) : -1;
            // Both or neither: a half-saved pair would restore a stale value
            // against a live flag. Without slots the old reset applies.
            if (val_slot < 0) continue;
            context->has_return_spill = has_slot;
            context->return_val_spill = val_slot;
        }
        if (context->has_return_spill < 0) continue;
        jm_emit_store_i64(mt, context->has_return_spill * (int)sizeof(uint64_t),
            mt->gen_env_reg, context->has_return_reg);
        jm_emit_store_i64(mt, context->return_val_spill * (int)sizeof(uint64_t),
            mt->gen_env_reg, context->return_val_reg);
    }
}

void jm_emit_try_state_restore(JsMirTranspiler* mt) {
    if (!mt) return;
    for (int td = 0; td < mt->try_ctx_depth; td++) {
        JsTryContext* context = jm_try_context_at(mt, td);
        bool restored = context->has_return_spill >= 0 && mt->gen_env_reg;
        if (restored) {
            jm_emit_load_i64(mt, context->has_return_reg,
                context->has_return_spill * (int)sizeof(uint64_t), mt->gen_env_reg);
            jm_emit_load_i64(mt, context->return_val_reg,
                context->return_val_spill * (int)sizeof(uint64_t), mt->gen_env_reg);
        }
        if (!restored && context->has_return_reg) {
            jm_emit_reg_op(mt, MIR_MOV, context->has_return_reg, MIR_new_int_op(mt->ctx, 0));
        }
        if (!restored && context->return_val_reg) {
            jm_emit_reg_op(mt, MIR_MOV, context->return_val_reg, MIR_new_int_op(mt->ctx, 0));
        }
        if (context->saved_error_lane_flag_reg) {
            jm_emit_reg_op(mt, MIR_MOV, context->saved_error_lane_flag_reg, MIR_new_int_op(mt->ctx, 0));
        }
        if (context->saved_error_lane_val_reg) {
            MIR_reg_t null_value = jm_emit_null(mt);
            jm_emit_mov(mt, context->saved_error_lane_val_reg, null_value);
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
    jm_emit_reg_op(mt, MIR_MOV, result, MIR_new_int_op(mt->ctx, value));
    return result;
}

static void jm_capture_routed_error_lane(JsMirTranspiler* mt, JsTryContext* context) {
    if (!mt) return;
    MIR_reg_t value = mt->func_em->last_call_result.reg;
    if (!context) {
        if (!value) {
            // Every fallible call publishes its merged lane. Reaching a
            // function exit without one is a lowering invariant violation,
            // but still preserve a valid error lane for the caller.
            value = em_call_1(&mt->func_em->em, "js_throw_value", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_emit_null(mt)), true);
        }
        if (!mt->func_error_lane_value_reg) {
            mt->func_error_lane_value_reg = jm_new_reg(mt, "_func_error_lane", MIR_T_I64);
        }
        // function-level exits are emitted after normal-path cleanup, so the
        // result must live in a dedicated register rather than the transient
        // last-call slot.
        jm_emit_mov(mt, mt->func_error_lane_value_reg, value);
        return;
    }
    if (value) {
        if (!context->incoming_error_lane_val_reg) {
            context->incoming_error_lane_val_reg = jm_new_reg(mt, "_try_exc", MIR_T_I64);
        }
        jm_emit_mov(mt, context->incoming_error_lane_val_reg, value);
        return;
    }
    // Every fallible call publishes its merged lane in the result register;
    // reaching this edge without one is a lowering invariant violation.
    MIR_reg_t fallback = em_call_1(&mt->func_em->em, "js_throw_value",
        MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_emit_null(mt)), true);
    if (!context->incoming_error_lane_val_reg) {
        context->incoming_error_lane_val_reg = jm_new_reg(mt, "_try_exc", MIR_T_I64);
    }
    // keep the context register stable: catch lowering retains the original
    // register, so a fallback value must be written into it rather than
    // replacing the context field after catch code has been emitted.
    jm_emit_mov(mt, context->incoming_error_lane_val_reg, fallback);
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
    if (mt->func_em->last_call_result.reg) return mt->func_em->last_call_result.reg;
    MIR_reg_t null_value = jm_emit_null(mt);
    // Exception exits preserve the last boxed helper result.  The fallback is
    // only for an impossible hand-written lowering edge and creates the same
    // valid LambdaError lane instead of manufacturing a null-plus-flag state.
    return em_call_1(&mt->func_em->em, "js_throw_value", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, null_value), true);
}

MIR_reg_t jm_arg_frame_base(JsMirTranspiler* mt) {
    if (!mt || !mt->func_em->em.frame.active || !mt->func_em->em.frame.root_base) {
        log_error("js-mir arg-frame invariant: base without active root frame");
        abort();
    }
    if (mt->arg_frame_base) return mt->arg_frame_base;
    mt->arg_frame_base = jm_new_reg(mt, "js_arg_frame", MIR_T_I64);
    mt->arg_frame_base_add = MIR_new_insn(mt->ctx, MIR_ADD,
        MIR_new_reg_op(mt->ctx, mt->arg_frame_base),
        MIR_new_reg_op(mt->ctx, mt->func_em->em.frame.root_base),
        MIR_new_int_op(mt->ctx, 0));
    // The semantic-root count is known only after liveness coloring. Keep one
    // entry add and patch its displacement when the complete frame is fixed.
    MIR_insert_insn_after(mt->ctx, mt->func_em->em.func_item,
        mt->func_em->em.frame.anchor, mt->arg_frame_base_add);
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
        if (mt->func_em->last_call_result.reg) {
            MIR_reg_t tag = jm_new_reg(mt, "exc_tag", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_URSH, tag, mt->func_em->last_call_result.reg,
                MIR_new_int_op(mt->ctx, 56));
            MIR_reg_t is_error = jm_new_reg(mt, "exc_inband", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_EQ, is_error, tag, MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR));
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
        jm_emit_jmp(mt, target);
        // An unconditional ERROR-lane jump leaves no fallthrough path. Keep
        // later lowering unreachable until the handler label supplies its
        // explicit lane state; otherwise dead statements after `throw` can
        // corrupt the following join (D8.4.3).
        jm_error_lane_set_state(mt, JS_ERROR_LANE_UNREACHABLE);
        return;
    }
    case JS_ERROR_LANE_UNKNOWN:
    default:
        break;
    }
    MIR_reg_t exception = jm_emit_error_lane_test(mt);
    if (jm_has_active_arg_frame(mt)) {
        MIR_label_t clean_path = jm_new_label(mt);
        jm_emit_branch(mt, MIR_BF, clean_path, exception);
        // Capture only on the exceptional edge. Emitting the carrier creation
        // before this branch would allocate a synthetic null exception on
        // every normal call and contaminate the merged lane.
        jm_capture_routed_error_lane(mt, route_context);
        // Fixed argument slots stay inside the function frame, but their
        // call-expression lifetime still ends on a caught exceptional edge.
        jm_clear_active_arg_frames(mt);
        jm_emit_jmp(mt, target);
        jm_emit_label_with_state(mt, clean_path, JS_ERROR_LANE_CLEAN);
    } else {
        MIR_label_t clean_path = jm_new_label(mt);
        jm_emit_branch(mt, MIR_BF, clean_path, exception);
        // Capture only after the tag test proves this edge exceptional; the
        // normal path must not manufacture a discarded ERROR carrier.
        jm_capture_routed_error_lane(mt, route_context);
        jm_emit_jmp(mt, target);
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
        jm_emit_jmp(mt, target);
        // the unconditional completion edge consumes the current path; the
        // target label restores the handler's explicit lane state (D8.4.3).
        jm_error_lane_set_state(mt, JS_ERROR_LANE_UNREACHABLE);
        return;
    case JS_ERROR_LANE_UNKNOWN:
    default:
        break;
    }
    MIR_reg_t exception = jm_emit_error_lane_test(mt);
    jm_emit_branch(mt, MIR_BT, target, exception);
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
    if (target == context->end_label) {
        // A delayed return is an actual end-label predecessor.  Preserve its
        // lane proof so an otherwise dead join cannot revive a D8.4.3 tag test.
        context->end_label_has_edge = true;
        context->end_label_error_lane_state = jm_error_lane_merge(
            context->end_label_error_lane_state, jm_error_lane_state(mt));
    }
    jm_emit_mov(mt, context->return_val_reg, value);
    jm_emit_reg_op(mt, MIR_MOV, context->has_return_reg, MIR_new_int_op(mt->ctx, 1));
    jm_emit_jmp(mt, target);
    return true;
}

MIR_reg_t jm_native_return_reg(JsMirTranspiler* mt, MirValue value) {
    if (!mt || !mt->in_native_func || !mt->current_fc) return value.reg;
    if (JM_JS_FACT(mt->current_fc, return_type) != LMD_TYPE_FLOAT) return value.reg;
    // Delayed completions publish an Item lane. Requesting the native return
    // carrier from its descriptor avoids recovering that fact from MIR.
    return em_require_rep(&mt->func_em->em, value, VALUE_REP_F64).reg;
}

// native errors share the planned companion lane and ownership epilogue (D8.4.3v2).
bool jm_emit_native_throw_exit(JsMirTranspiler* mt, MIR_reg_t lane) {
    if (!mt || !mt->in_native_func || !mt->current_fc || !lane) return false;
    MIR_op_t placeholder = mt->func_em->em.frame.return_type == MIR_T_D
        ? MIR_new_double_op(mt->ctx, 0.0) : MIR_new_int_op(mt->ctx, 0);
    em_stage_function_return(&mt->func_em->em, placeholder, lane);
    jm_error_lane_set_state(mt, JS_ERROR_LANE_UNREACHABLE);
    return true;
}

static void jm_emit_throw_completion_impl(JsMirTranspiler* mt, MIR_reg_t value,
        JsTryContext* forced_context, bool force_finally) {
    if (!mt) return;
    MIR_reg_t thrown = jm_callr_1(mt, "js_throw_value", MIR_T_I64, value);
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
        jm_emit_jmp(mt, target);
        return;
    }
    if (jm_emit_native_throw_exit(mt, thrown)) return;
    MIR_reg_t native_value = jm_native_return_reg(mt, jm_item_value(thrown));
    jm_emit_ret(mt, native_value);
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

// `target_loop_index` is the break-target stack entry the jump lands on, or -1
// when none was found. Only a try entered *inside* that target is unwound: a
// `break` out of a `switch` (or an inner loop) that sits inside a try's block
// leaves the block still running, so its finally must not run here — and would
// otherwise run a second time when the block completes normally.
void jm_emit_abrupt_jump_cleanup(JsMirTranspiler* mt, int target_loop_index) {
    for (int t = mt->try_ctx_depth - 1; t >= 0; t--) {
        JsTryContext* tc = jm_try_context_at(mt, t);
        if (tc->loop_depth_at_push <= target_loop_index) continue;
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

// Which break-target stack entry this jump lands on; -1 when unresolved. The
// finally-unwinding depth is decided from it, so it has to be known first.
static int jm_jump_target_index(JsMirTranspiler* mt, JsBreakContinueNode* jump,
        bool is_continue) {
    if (!mt || !jump) return -1;
    if (jump->label && jump->label_len > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            JsLoopLabels* loop = jm_loop_label_at(mt, i);
            if (loop && loop->label_name &&
                loop->label_name_len == jump->label_len &&
                memcmp(loop->label_name, jump->label, jump->label_len) == 0) {
                return i;
            }
        }
        return -1;
    }
    if (!is_continue) return mt->loop_depth > 0 ? mt->loop_depth - 1 : -1;
    // `continue` skips break-only targets such as an enclosing switch.
    for (int i = mt->loop_depth - 1; i >= 0; i--) {
        JsLoopLabels* loop = jm_loop_label_at(mt, i);
        if (loop && loop->continue_label) return i;
    }
    return -1;
}

void jm_emit_break_completion(JsMirTranspiler* mt, JsBreakContinueNode* brk) {
    int target = jm_jump_target_index(mt, brk, false);
    jm_emit_abrupt_jump_cleanup(mt, target);
    if (target < 0) return;
    JsLoopLabels* loop = jm_loop_label_at(mt, target);
    if (!loop) return;
    if (brk->label && brk->label_len > 0) {
        jm_emit_close_intervening_iterators(mt, target);
    }
    jm_emit_jmp(mt, loop->break_label);
}

void jm_emit_continue_completion(JsMirTranspiler* mt, JsBreakContinueNode* cont) {
    int target = jm_jump_target_index(mt, cont, true);
    jm_emit_abrupt_jump_cleanup(mt, target);
    if (target < 0) return;
    JsLoopLabels* loop = jm_loop_label_at(mt, target);
    if (!loop || !loop->continue_label) return;
    if (cont->label && cont->label_len > 0) {
        jm_emit_close_intervening_iterators(mt, target);
    }
    jm_emit_jmp(mt, loop->continue_label);
}
