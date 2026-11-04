#pragma once

#include "mir_emitter_shared.hpp"

static inline void em_mark_boxed_function(MirEmitter* em, MIR_reg_t object,
        bool procedure, uint32_t shape) {
    em_call_void_1(em, "lambda_function_mark_mir_context_abi", MIR_T_P, MIR_new_reg_op(em->ctx, object), true);
    em_call_void_1(em, procedure ? "lambda_function_mark_lambda_boxed_procedure" :
        "lambda_function_mark_lambda_boxed_function", MIR_T_P, MIR_new_reg_op(em->ctx, object), true);
    // keep hot function-value creation on the existing inline descriptor store.
    MIR_reg_t flags = em_load_at(em, object, offsetof(Function, flags), MIR_T_U32, "fn_flags");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR, MIR_new_reg_op(em->ctx, flags),
        MIR_new_reg_op(em->ctx, flags), MIR_new_uint_op(em->ctx, shape << LAMBDA_MIR_PUBLIC_RETURN_SHAPE_SHIFT)));
    em_store_at(em, object, offsetof(Function, flags), MIR_T_U32, flags);
}

static inline MIR_reg_t em_plain_function_value(MirEmitter* em, MIR_reg_t address,
        int arity, const char* name, Type* type, bool procedure, bool boxed, uint32_t shape) {
    MIR_reg_t object = name && *name
        ? em_call_3(em, "to_fn_named", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, address),
            MIR_T_I64, MIR_new_int_op(em->ctx, arity), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name), true)
        : em_call_2(em, "to_fn_n", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, address),
            MIR_T_I64, MIR_new_int_op(em->ctx, arity), true);
    if (boxed) em_mark_boxed_function(em, object, procedure, shape);
    if (type) em_call_void_2(em, "lambda_function_set_type", MIR_T_P, MIR_new_reg_op(em->ctx, object),
        MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)type), true);
    return object;
}
