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
