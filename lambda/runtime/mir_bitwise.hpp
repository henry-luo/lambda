#pragma once

#include "mir_emitter_shared.hpp"

static inline MIR_insn_code_t em_integer_bitwise_opcode(SysFunc fn) {
    return fn == SYSFUNC_BAND ? MIR_AND : fn == SYSFUNC_BOR ? MIR_OR :
        fn == SYSFUNC_BXOR ? MIR_XOR : fn == SYSFUNC_SHL ? MIR_LSH : MIR_RSH;
}

static inline const char* em_integer_bitwise_helper(SysFunc fn) {
    return fn == SYSFUNC_BAND ? "fn_band_item" : fn == SYSFUNC_BOR ? "fn_bor_item" :
        fn == SYSFUNC_BXOR ? "fn_bxor_item" : fn == SYSFUNC_BNOT ? "fn_bnot_item" :
        fn == SYSFUNC_SHL ? "fn_shl_item" : fn == SYSFUNC_SHR ? "fn_shr_item" : "fn_ushr_item";
}

static inline MIR_reg_t em_pack_u32_payload(MirEmitter* em, MIR_reg_t raw, MIR_reg_t result = 0) {
    if (!result) result = em_new_reg(em, "u32", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR, MIR_new_reg_op(em->ctx, result),
        MIR_new_uint_op(em->ctx, ((uint64_t)LMD_TYPE_NUM_SIZED << 56) | ((uint64_t)NUM_UINT32 << 48)),
        MIR_new_reg_op(em->ctx, raw)));
    return result;
}

// an unsigned interval test also rejects negative and nullable/poison lanes;
// the caller retains its ordinary E201 diagnostic on a miss (S4.5.3).
static inline MIR_reg_t em_admit_int_lane_u32(MirEmitter* em, MIR_reg_t value,
        MIR_label_t miss) {
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UBGT, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, value), MIR_new_uint_op(em->ctx, UINT32_MAX)));
    return em_pack_u32_payload(em, value);
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

// shared packed u32 operations preserve guards before payload masking (S4.4, D2.3.5).
static inline void em_guard_u32_item(MirEmitter* em, MIR_reg_t item,
        MIR_label_t fallback) {
    MIR_reg_t upper = em_new_reg(em, "u32_operand_upper", MIR_T_I64);
    const uint64_t prefix = ((uint64_t)LMD_TYPE_NUM_SIZED << 56) |
        ((uint64_t)NUM_UINT32 << 48);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, upper), MIR_new_reg_op(em->ctx, item),
        MIR_new_int_op(em->ctx, (int64_t)~UINT64_C(0xffffffff))));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE,
        MIR_new_label_op(em->ctx, fallback), MIR_new_reg_op(em->ctx, upper),
        MIR_new_int_op(em->ctx, (int64_t)prefix)));
}

static inline MIR_reg_t em_u32_binary(MirEmitter* em, Operator operation,
        MIR_reg_t left_item, MIR_reg_t right_item, bool guard_left, bool guard_right) {
    MIR_reg_t left = em_new_reg(em, "u32_l", MIR_T_I64);
    MIR_reg_t right = em_new_reg(em, "u32_r", MIR_T_I64);
    MIR_reg_t raw = em_new_reg(em, "u32_raw", MIR_T_I64);
    MIR_reg_t result = em_new_reg(em, "u32", MIR_T_I64);
    MIR_label_t fallback = em_new_label(em);
    MIR_label_t done = em_new_label(em);
    bool guarded = guard_left || guard_right;
    const uint64_t value_mask = UINT32_MAX;

    // A nested fallible producer can publish ItemError even when inference
    // selects u32 arithmetic. Keep the boxed operator for that cold join
    // before masking either tag away (S4.4.1, D2.3.4–D2.3.5).
    if (guard_left) em_guard_u32_item(em, left_item, fallback);
    if (guard_right) em_guard_u32_item(em, right_item, fallback);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, left), MIR_new_reg_op(em->ctx, left_item),
        MIR_new_int_op(em->ctx, (int64_t)value_mask)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, right), MIR_new_reg_op(em->ctx, right_item),
        MIR_new_int_op(em->ctx, (int64_t)value_mask)));
    MIR_insn_code_t op = operation == OPERATOR_ADD ? MIR_ADD :
        operation == OPERATOR_SUB ? MIR_SUB : MIR_MUL;
    em_emit_insn(em, MIR_new_insn(em->ctx, op, MIR_new_reg_op(em->ctx, raw),
        MIR_new_reg_op(em->ctx, left), MIR_new_reg_op(em->ctx, right)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, raw), MIR_new_reg_op(em->ctx, raw),
        MIR_new_int_op(em->ctx, (int64_t)value_mask)));
    // u32 arithmetic is defined to wrap; rebuilding the tagged payload keeps
    // the exact same self-describing Item that coerce_num_sized would return.
    em_pack_u32_payload(em, raw, result);
    if (guarded) {
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
            MIR_new_label_op(em->ctx, done)));
        em_emit_label(em, fallback);
        const char* boxed_fn = operation == OPERATOR_ADD ? "fn_add" :
            operation == OPERATOR_SUB ? "fn_sub" : "fn_mul";
        MIR_reg_t boxed_result = em_call_2(em, boxed_fn, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, left_item),
            MIR_T_I64, MIR_new_reg_op(em->ctx, right_item), false);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
            MIR_new_reg_op(em->ctx, result),
            MIR_new_reg_op(em->ctx, boxed_result)));
        em_emit_label(em, done);
    }
    return result;
}

static inline MIR_reg_t em_u32_bitwise(MirEmitter* em, SysFunc function,
        MIR_reg_t left_item, MIR_reg_t right_item, bool left_sized, bool right_sized,
        bool guard_left, bool guard_right) {
    MIR_reg_t left = em_new_reg(em, "u32_l", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, left), MIR_new_reg_op(em->ctx, left_item),
        MIR_new_int_op(em->ctx, (int64_t)UINT32_MAX)));
    MIR_reg_t right = right_item;
    if (right_sized) {
        right = em_new_reg(em, "u32_r", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
            MIR_new_reg_op(em->ctx, right), MIR_new_reg_op(em->ctx, right_item),
            MIR_new_int_op(em->ctx, (int64_t)UINT32_MAX)));
    }

    MIR_insn_code_t op = (MIR_insn_code_t)0;
    if (function == SYSFUNC_BAND) op = MIR_AND;
    else if (function == SYSFUNC_BOR) op = MIR_OR;
    else if (function == SYSFUNC_BXOR) op = MIR_XOR;
    else if (function == SYSFUNC_SHL) op = MIR_LSH;
    else op = MIR_URSH;

    MIR_reg_t raw = em_new_reg(em, "u32_raw", MIR_T_I64);
    MIR_reg_t result = em_new_reg(em, "u32_result", MIR_T_I64);
    MIR_reg_t out = em_new_reg(em, "u32_out", MIR_T_I64);
    MIR_label_t l_error = em_new_label(em);
    MIR_label_t l_zero = em_new_label(em);
    MIR_label_t l_compute = em_new_label(em);
    MIR_label_t l_pack = em_new_label(em);
    MIR_label_t l_fallback = em_new_label(em);
    MIR_label_t l_end = em_new_label(em);
    bool guarded = guard_left || guard_right;
    // A nested shift can return ItemError despite its inferred u32 result.
    // Validate uncertain operands before their payload masks erase that tag;
    // the existing boxed helper retains the slow/error semantics (S4.4.4,
    // D2.3.4–D2.3.5).
    if (guard_left) em_guard_u32_item(em, left_item, l_fallback);
    if (guard_right) em_guard_u32_item(em, right_item, l_fallback);
    bool is_shift = function == SYSFUNC_SHL ||
        function == SYSFUNC_SHR || function == SYSFUNC_USHR;
    if (is_shift && !right_sized) {
        // counts are int53 lanes; truncating to 32 bits changes large counts.
        MIR_reg_t negative = em_new_reg(em, "u32_shift_neg", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_LT,
            MIR_new_reg_op(em->ctx, negative), MIR_new_reg_op(em->ctx, right),
            MIR_new_int_op(em->ctx, 0)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, l_error), MIR_new_reg_op(em->ctx, negative)));
    }
    if (is_shift) {
        MIR_reg_t exhausted = em_new_reg(em, "u32_shift_exhausted", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_GE,
            MIR_new_reg_op(em->ctx, exhausted), MIR_new_reg_op(em->ctx, right),
            MIR_new_int_op(em->ctx, 32)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, l_zero), MIR_new_reg_op(em->ctx, exhausted)));
    }
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
        MIR_new_label_op(em->ctx, l_compute)));
    em_emit_label(em, l_error);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, out), MIR_new_int_op(em->ctx,
            (int64_t)((uint64_t)LMD_TYPE_ERROR << 56))));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
        MIR_new_label_op(em->ctx, l_end)));
    em_emit_label(em, l_zero);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, raw), MIR_new_int_op(em->ctx, 0)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
        MIR_new_label_op(em->ctx, l_pack)));
    em_emit_label(em, l_compute);
    if (is_shift) {
        MIR_reg_t computed = em_new_reg(em, "u32_shift", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, op,
            MIR_new_reg_op(em->ctx, computed), MIR_new_reg_op(em->ctx, left),
            MIR_new_reg_op(em->ctx, right)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
            MIR_new_reg_op(em->ctx, raw), MIR_new_reg_op(em->ctx, computed),
            MIR_new_int_op(em->ctx, (int64_t)UINT32_MAX)));
    } else {
        em_emit_insn(em, MIR_new_insn(em->ctx, op,
            MIR_new_reg_op(em->ctx, raw), MIR_new_reg_op(em->ctx, left),
            MIR_new_reg_op(em->ctx, right)));
    }
    // A count outside the u32 width must bypass the machine shift entirely;
    // falling through recomputed the zero arm with a host-width shift count.
    em_emit_label(em, l_pack);
    em_pack_u32_payload(em, raw, result);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, out), MIR_new_reg_op(em->ctx, result)));
    if (guarded) {
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP,
            MIR_new_label_op(em->ctx, l_end)));
        em_emit_label(em, l_fallback);
        const char* boxed_fn = em_integer_bitwise_helper(function);
        MIR_reg_t boxed_left = left_sized
            ? left_item : em_require_rep(em, em_value_for_rep(left_item, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT), VALUE_REP_ITEM).reg;
        MIR_reg_t boxed_right = right_sized
            ? right_item : em_require_rep(em, em_value_for_rep(right_item, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT), VALUE_REP_ITEM).reg;
        MIR_reg_t boxed_result = em_call_2(em, boxed_fn, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, boxed_left),
            MIR_T_I64, MIR_new_reg_op(em->ctx, boxed_right), false);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
            MIR_new_reg_op(em->ctx, out), MIR_new_reg_op(em->ctx, boxed_result)));
    }
    em_emit_label(em, l_end);
    return out;
}
