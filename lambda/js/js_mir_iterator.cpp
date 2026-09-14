#include "js_mir_internal.hpp"

// ============================================================================
// IteratorRecord-style MIR helpers
// ============================================================================

#define JM_EMIT_ITERATOR_CALL(name, runtime_name, parameter) \
MIR_reg_t name(JsMirTranspiler* mt, MIR_reg_t parameter) { \
    return jm_call_1(mt, runtime_name, MIR_T_I64, \
        MIR_T_I64, MIR_new_reg_op(mt->ctx, parameter)); \
}

JM_EMIT_ITERATOR_CALL(jm_emit_get_iterator, "js_get_iterator", iterable)
JM_EMIT_ITERATOR_CALL(jm_emit_get_iterator_lazy, "js_get_iterator_lazy", iterable)
JM_EMIT_ITERATOR_CALL(jm_emit_iterator_step, "js_iterator_step", iterator)

MIR_reg_t jm_emit_iterator_done_test(JsMirTranspiler* mt, MIR_reg_t step_result, const char* prefix) {
    MIR_reg_t is_done = jm_new_reg(mt, prefix ? prefix : "itdone", MIR_T_I64);
    jm_emit_reg_binary_op(mt, MIR_EQ, is_done, step_result, MIR_new_int_op(mt->ctx, (int64_t)JS_ITER_DONE_SENTINEL));
    return is_done;
}

JM_EMIT_ITERATOR_CALL(jm_emit_iterator_collect_rest, "js_iterator_collect_rest", iterator)

#undef JM_EMIT_ITERATOR_CALL

void jm_emit_iterator_close(JsMirTranspiler* mt, MIR_reg_t iterator) {
    jm_callr_1(mt, "js_iterator_close", MIR_T_I64, iterator);
}

void jm_emit_iterator_close_checked(JsMirTranspiler* mt, MIR_reg_t iterator) {
    // Normal-completion IteratorClose must forward a failing return lookup or
    // call; exception-cleanup callers use the unchecked form to preserve the
    // original abrupt completion while closing.
    jm_callr_1(mt, "js_iterator_close", MIR_T_I64, iterator);
    jm_emit_error_lane_propagate_check(mt);
}

void jm_emit_async_iterator_close_checked(JsMirTranspiler* mt,
        MIR_reg_t iterator) {
    MIR_reg_t raw_close = jm_callr_1(mt, "js_async_iterator_close_result",
        MIR_T_I64, iterator);
    // A lookup/call failure is an ordinary abrupt close completion here and
    // therefore overrides a pending break or return.
    jm_emit_error_lane_propagate_check(mt);

    MIR_reg_t needs_await = jm_emit_uext8(mt, jm_callr_1(mt,
        "js_async_iterator_close_needs_await", MIR_T_I64, raw_close));
    MIR_label_t close_done = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BF, close_done, needs_await);

    MIR_reg_t close_result = jm_emit_await_value_reg(mt, raw_close,
        JS_MIR_SUSPEND_ASYNC_ITERATOR_CLOSE);
    jm_emit_error_lane_propagate_check(mt);

    MIR_reg_t is_object = jm_emit_uext8(mt, jm_callr_1(mt,
        "js_is_object_value", MIR_T_I64, close_result));
    MIR_label_t close_result_object = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BT, close_result_object, is_object);
    MIR_reg_t message = jm_box_string_literal(mt,
        "Iterator result is not an object", 32);
    MIR_reg_t type_name = jm_box_string_literal(mt, "TypeError", 9);
    MIR_reg_t type_error = jm_callr_2(mt, "js_new_error_with_name", MIR_T_I64,
        type_name, message);
    (void)jm_callr_1(mt, "js_throw_value", MIR_T_I64, type_error);
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_label_with_state(mt, close_result_object, JS_ERROR_LANE_CLEAN);
    jm_emit_label_with_state(mt, close_done, JS_ERROR_LANE_CLEAN);
}

void jm_emit_loop_iterator_close_checked(JsMirTranspiler* mt,
        const JsLoopLabels* loop) {
    if (!loop || !loop->iterator_to_close) return;
    if (!loop->is_async_iterator) {
        jm_emit_iterator_close_checked(mt, loop->iterator_to_close);
        return;
    }

    int saved_try_depth = mt->try_ctx_depth;
    if (loop->iterator_cleanup_try_depth >= 0 &&
            loop->iterator_cleanup_try_depth < saved_try_depth) {
        // A close failure belongs outside the loop body's synthetic handler;
        // otherwise it re-enters cleanup and closes the same iterator twice.
        mt->try_ctx_depth = loop->iterator_cleanup_try_depth;
    }
    jm_emit_async_iterator_close_checked(mt, loop->iterator_to_close);
    mt->try_ctx_depth = saved_try_depth;
}

void jm_emit_async_iterator_close_preserving_throw(JsMirTranspiler* mt,
        MIR_reg_t iterator, MIR_reg_t thrown_value) {
    // The source throw remains observable after cleanup, but its payload must
    // stay rooted while `return()` and the awaited close result can collect.
    jm_create_gc_root_slot(mt, thrown_value);
    MIR_reg_t raw_close = jm_callr_1(mt, "js_async_iterator_close_result",
        MIR_T_I64, iterator);
    MIR_label_t rethrow_source = jm_new_label(mt);
    MIR_reg_t close_failed = jm_emit_error_lane_test(mt);
    jm_emit_branch(mt, MIR_BT, rethrow_source, close_failed);

    MIR_reg_t needs_await = jm_emit_uext8(mt, jm_callr_1(mt,
        "js_async_iterator_close_needs_await", MIR_T_I64, raw_close));
    jm_emit_branch(mt, MIR_BF, rethrow_source, needs_await);

    int thrown_spill = -1;
    if (mt->in_generator && mt->gen_env_reg) {
        thrown_spill = jm_gen_spill_save(mt, thrown_value);
    }
    (void)jm_emit_await_value_reg(mt, raw_close,
        JS_MIR_SUSPEND_ASYNC_ITERATOR_CLOSE, false);
    if (thrown_spill >= 0) jm_gen_spill_load(mt, thrown_value, thrown_spill);
    jm_emit_jmp(mt, rethrow_source);

    // Ignore a close rejection or invalid close result: the source throw has
    // precedence on this cleanup edge and is re-routed by the caller.
    jm_emit_label_with_state(mt, rethrow_source, JS_ERROR_LANE_CLEAN);
    (void)jm_callr_1(mt, "js_throw_value", MIR_T_I64, thrown_value);
    jm_error_lane_set_state(mt, JS_ERROR_LANE_SET);
}

void jm_emit_iterator_close_on_error_lane_if_open(JsMirTranspiler* mt, MIR_reg_t iterator,
    MIR_reg_t iter_done, MIR_label_t target)
{
    MIR_reg_t exc = jm_emit_error_lane_test(mt);
    MIR_label_t no_exc = jm_new_label(mt);
    MIR_label_t rethrow_only = jm_new_label(mt);
    MIR_label_t after_close = jm_new_label(mt);

    jm_emit_branch(mt, MIR_BF, no_exc, exc);

    MIR_reg_t saved_exc = jm_emit_error_lane_return(mt);
    jm_emit_branch(mt, MIR_BT, rethrow_only, iter_done);
    jm_emit_iterator_close(mt, iterator);
    // iterator close may set a second error; the saved result remains the
    // abrupt completion that must be rethrown after cleanup.
    jm_emit_jmp(mt, after_close);

    jm_emit_label(mt, rethrow_only);
    jm_emit_label(mt, after_close);
    jm_callr_1(mt, "js_throw_value", MIR_T_I64, saved_exc);
    jm_emit_jmp(mt, target);
    jm_emit_label(mt, no_exc);
    // the fallthrough edge is the normal completion path; leaving the
    // compile-time state as SET makes a later cleanup helper rethrow its
    // uninitialized saved exception slot on iterator exhaustion.
    jm_error_lane_set_state(mt, JS_ERROR_LANE_CLEAN);
}
