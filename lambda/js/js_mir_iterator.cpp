#include "js_mir_internal.hpp"

// ============================================================================
// IteratorRecord-style MIR helpers
// ============================================================================

MIR_reg_t jm_emit_get_iterator(JsMirTranspiler* mt, MIR_reg_t iterable) {
    return jm_call_1(mt, "js_get_iterator", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));
}

MIR_reg_t jm_emit_get_iterator_lazy(JsMirTranspiler* mt, MIR_reg_t iterable) {
    return jm_call_1(mt, "js_get_iterator_lazy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));
}


MIR_reg_t jm_emit_iterator_step(JsMirTranspiler* mt, MIR_reg_t iterator) {
    return jm_call_1(mt, "js_iterator_step", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterator));
}

MIR_reg_t jm_emit_iterator_done_test(JsMirTranspiler* mt, MIR_reg_t step_result, const char* prefix) {
    MIR_reg_t is_done = jm_new_reg(mt, prefix ? prefix : "itdone", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ,
        MIR_new_reg_op(mt->ctx, is_done),
        MIR_new_reg_op(mt->ctx, step_result),
        MIR_new_int_op(mt->ctx, (int64_t)JS_ITER_DONE_SENTINEL)));
    return is_done;
}

MIR_reg_t jm_emit_iterator_collect_rest(JsMirTranspiler* mt, MIR_reg_t iterator) {
    return jm_call_1(mt, "js_iterator_collect_rest", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterator));
}

void jm_emit_iterator_close(JsMirTranspiler* mt, MIR_reg_t iterator) {
    jm_call_1(mt, "js_iterator_close", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, iterator));
}

void jm_emit_iterator_close_on_exception(JsMirTranspiler* mt, MIR_reg_t iterator, MIR_label_t target) {
    MIR_reg_t exc = jm_emit_exception_test(mt);
    MIR_label_t no_exc = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, no_exc),
        MIR_new_reg_op(mt->ctx, exc)));

    MIR_reg_t saved_exc = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
    jm_emit_iterator_close(mt, iterator);
    jm_call_0(mt, "js_clear_exception", MIR_T_I64);
    jm_call_void_1(mt, "js_throw_value",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_exc));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, target)));
    jm_emit_label(mt, no_exc);
}


void jm_emit_iterator_close_on_exception_if_open(JsMirTranspiler* mt, MIR_reg_t iterator,
    MIR_reg_t iter_done, MIR_label_t target)
{
    MIR_reg_t exc = jm_emit_exception_test(mt);
    MIR_label_t no_exc = jm_new_label(mt);
    MIR_label_t rethrow_only = jm_new_label(mt);
    MIR_label_t after_close = jm_new_label(mt);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, no_exc),
        MIR_new_reg_op(mt->ctx, exc)));

    MIR_reg_t saved_exc = jm_call_0(mt, "js_clear_exception", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, rethrow_only),
        MIR_new_reg_op(mt->ctx, iter_done)));
    jm_emit_iterator_close(mt, iterator);
    jm_call_0(mt, "js_clear_exception", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, after_close)));

    jm_emit_label(mt, rethrow_only);
    jm_emit_label(mt, after_close);
    jm_call_void_1(mt, "js_throw_value",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, saved_exc));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, target)));
    jm_emit_label(mt, no_exc);
}
