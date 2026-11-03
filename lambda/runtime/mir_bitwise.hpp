#pragma once

#include "mir_emitter_shared.hpp"

static inline MIR_insn_code_t em_integer_bitwise_opcode(SysFunc fn) {
    return fn == SYSFUNC_BAND ? MIR_AND : fn == SYSFUNC_BOR ? MIR_OR :
        fn == SYSFUNC_BXOR ? MIR_XOR : fn == SYSFUNC_SHL ? MIR_LSH : MIR_RSH;
}

// Shared compact-int fast path. The caller chooses the semantic operation and
// fallback; native inputs avoid a box/unbox round trip on the successful arm.
static inline MIR_reg_t em_guarded_int_bitwise(MirEmitter* em, SysFunc fn,
        MirValue left, MirValue right, const char* fallback_name) {
    MIR_context_t ctx = em->ctx;
    MIR_label_t fallback = em_new_label(em), done = em_new_label(em);
    MIR_reg_t result = em_new_reg(em, "bitwise_join", MIR_T_I64);
    MirValue inputs[] = {left, right};
    MIR_reg_t lanes[2];
    // materialize both fallback operands before the first guard can branch.
    for (int i = 0; i < 2; i++)
        if (inputs[i].rep != VALUE_REP_INT_LANE)
            inputs[i] = em_require_rep(em, inputs[i], VALUE_REP_ITEM);
    for (int i = 0; i < 2; i++) lanes[i] = em_guard_finite_int(em, inputs[i], fallback);
    MIR_insn_code_t op = em_integer_bitwise_opcode(fn);
    if (fn == SYSFUNC_SHL || fn == SYSFUNC_SHR)
        em_emit_insn(em, MIR_new_insn(ctx, MIR_UBGE, MIR_new_label_op(ctx, fallback),
            MIR_new_reg_op(ctx, lanes[1]), MIR_new_int_op(ctx, 64)));
    MIR_reg_t raw = em_new_reg(em, "bitwise_word", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(ctx, op, MIR_new_reg_op(ctx, raw),
        MIR_new_reg_op(ctx, lanes[0]), MIR_new_reg_op(ctx, lanes[1])));
    // A machine result can equal a lane sentinel (1 << 63). The helper must
    // saturate that finite word instead of interpreting it as poison (S4.1.2).
    em_branch_int53_outside(em, raw, fallback);
    em_box_finite_int_item(em, raw, result);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, done)));
    em_emit_label(em, fallback);
    left = em_require_rep(em, inputs[0], VALUE_REP_ITEM);
    right = em_require_rep(em, inputs[1], VALUE_REP_ITEM);
    MIR_reg_t slow = em_call_2(em, fallback_name, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(ctx, left.reg), MIR_T_I64, MIR_new_reg_op(ctx, right.reg), false);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result), MIR_new_reg_op(ctx, slow)));
    em_emit_label(em, done);
    return result;
}
