#pragma once

#include "mir_emitter_shared.hpp"
#include "recovery_frame.h"

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

static inline MIR_reg_t em_function_value(MirEmitter* em, MIR_reg_t address,
        int arity, const char* name, Type* type, bool procedure, bool boxed, uint32_t shape,
        bool system = false, MIR_reg_t environment = 0, int capture_count = 0) {
    MIR_reg_t object;
    if (environment) {
        object = name && *name
            ? em_call_4(em, "to_closure_named", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(em->ctx, address), MIR_T_I64, MIR_new_int_op(em->ctx, arity),
                MIR_T_P, MIR_new_reg_op(em->ctx, environment), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name), true)
            : em_call_3(em, "to_closure", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(em->ctx, address), MIR_T_I64, MIR_new_int_op(em->ctx, arity),
                MIR_T_P, MIR_new_reg_op(em->ctx, environment), true);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
            MIR_new_mem_op(em->ctx, MIR_T_U8, offsetof(Function, closure_field_count), object, 0, 1),
            MIR_new_int_op(em->ctx, capture_count)));
    } else {
        object = name && *name
            ? em_call_3(em, system ? "to_sys_fn_named" : "to_fn_named", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, address),
                MIR_T_I64, MIR_new_int_op(em->ctx, arity), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name), true)
            : em_call_2(em, "to_fn_n", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, address),
                MIR_T_I64, MIR_new_int_op(em->ctx, arity), true);
    }
    if (boxed) em_mark_boxed_function(em, object, procedure, shape);
    if (type) em_call_void_2(em, "lambda_function_set_type", MIR_T_P, MIR_new_reg_op(em->ctx, object),
        MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)type), true);
    return object;
}

typedef MIR_reg_t (*MirCaptureReader)(void* owner, FnCapture* capture);

// each flat environment owns its Item payloads; frontends supply lexical reads
// and capture permissions, while this loop owns layout and precise roots.
static inline MIR_reg_t em_capture_environment(MirEmitter* em, FnCapture* captures,
        MirCaptureReader read_capture) {
    int count = ast_capture_count(captures);
    if (!count) return 0;
    // the collector must see initialized slots before a Function owns the env.
    MIR_reg_t environment = em_call_1(em, "heap_calloc_closure_env", MIR_T_P,
        MIR_T_I64, MIR_new_int_op(em->ctx, count * sizeof(Item)), true);
    if (em->root_call_value) em->root_call_value(em->call_owner, environment, JIT_VALUE_RAW_GC_POINTER);
    int index = 0;
    for (FnCapture* capture = captures; capture; capture = capture->next, index++) {
        MIR_reg_t item = read_capture(em->call_owner, capture);
        if (em->root_call_value) em->root_call_value(em->call_owner, item, JIT_VALUE_BOXED_ITEM);
        em_call_void_4(em, "owned_item_slot_store",
            MIR_T_P, MIR_new_reg_op(em->ctx, environment), MIR_T_I64, MIR_new_int_op(em->ctx, count),
            MIR_T_I64, MIR_new_int_op(em->ctx, index), MIR_T_I64, MIR_new_reg_op(em->ctx, item), true);
    }
    return environment;
}

static inline MIR_reg_t em_capture_slot_read(MirEmitter* em, MIR_reg_t environment,
        int count, int index) {
    return em_call_4(em, "owned_item_slot_read", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, environment), MIR_T_I64, MIR_new_int_op(em->ctx, count),
        MIR_T_I64, MIR_new_int_op(em->ctx, index), MIR_T_I64, MIR_new_int_op(em->ctx, 0), true);
}

// task entries use boxed functions and a boxed-value argument array (S13.1.1v3).
static inline MIR_reg_t em_start_task(MirEmitter* em, AstStartNode* start,
        MirResultCheck check_arguments) {
    AstCallNode* call = start->call;
    MirValue function = em_require_rep(em,
        em_lower_value(em, call->function, MIR_VALUE_ANY), VALUE_REP_ITEM);
    if (em->root_call_value) em->root_call_value(em->call_owner, function.reg, JIT_VALUE_BOXED_ITEM);
    MIR_reg_t arguments;
    if (call->argument) {
        MirValue source = em_require_rep(em,
            em_lower_value(em, call->argument, MIR_VALUE_ANY), VALUE_REP_ITEM);
        arguments = em_call_2(em, "ensure_typed_array", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, source.reg),
            MIR_T_I64, MIR_new_int_op(em->ctx, LMD_TYPE_ANY), true);
        check_arguments(em->call_owner, arguments);
    } else arguments = em_call_0(em, "list", MIR_T_P, true);
    if (em->root_call_value) em->root_call_value(em->call_owner, arguments, JIT_VALUE_RAW_GC_POINTER);
    if (start->document_owner)
        return em_call_2(em, "lambda_task_start_document", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, function.reg),
            MIR_T_P, MIR_new_reg_op(em->ctx, arguments), true);
    return em_call_3(em, "lambda_task_start_function_scoped", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, function.reg),
        MIR_T_P, MIR_new_reg_op(em->ctx, arguments),
        MIR_T_I64, MIR_new_int_op(em->ctx, start->escapes ? 1 : 0), true);
}

// both frontends retain their lexical exit destinations; the runtime joins or
// cancels the crossed task scopes while the activation's roots remain live.
static inline void em_task_scope_finish(MirEmitter* em, MIR_reg_t scope,
        MIR_op_t error_exit, bool unwind = false) {
    (void)em_call_2(em, unwind ? "lambda_task_scope_unwind" : "lambda_task_scope_leave", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, scope), MIR_T_I64, error_exit, true);
}

typedef MIR_reg_t (*MirFaultOperandReader)(void* owner, AstNode* node, MIR_reg_t frame);

// S7.11.1v2/S7.11.4: the generated activation owns the platform checkpoint;
// a returned C wrapper cannot be a valid non-local recovery target.
static inline MIR_reg_t em_local_fault_expression(MirEmitter* em, AstNode* node,
        MirFaultOperandReader read_operand) {
    MIR_reg_t result = em_new_reg(em, "local_fault_result", MIR_T_I64);
    MIR_reg_t scope_mark = em_call_0(em, "lambda_task_scope_current", MIR_T_P, true);
    MIR_reg_t frame = em_call_2(em, "lambda_recovery_frame_begin_for", MIR_T_P,
        MIR_T_P, MIR_new_reg_op(em->ctx, em->frame.runtime),
        MIR_T_I64, MIR_new_int_op(em->ctx, LAMBDA_RECOVERY_CAP_LOCAL_FAULT), true);
    MIR_label_t have_frame = em_new_label(em), landing = em_new_label(em);
    MIR_label_t setup_failed = em_new_label(em), restored_landing = em_new_label(em), done = em_new_label(em);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, have_frame),
        MIR_new_reg_op(em->ctx, frame), MIR_new_int_op(em->ctx, 0)));
    // setup failure must raise to the enclosing frame rather than evaluate
    // the protected operand without a live target.
    (void)em_call_2(em, "lambda_recovery_frame_raise_fault", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(em->ctx, LAMBDA_FAULT_OUT_OF_MEMORY),
        MIR_T_I64, MIR_new_int_op(em->ctx, ERR_OK), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result),
        MIR_new_int_op(em->ctx, ITEM_ERROR)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
    em_emit_label(em, have_frame);
    MIR_reg_t jump_buffer = em_new_reg(em, "local_fault_jmp", MIR_T_P);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD, MIR_new_reg_op(em->ctx, jump_buffer),
        MIR_new_reg_op(em->ctx, frame), MIR_new_int_op(em->ctx, LAMBDA_RECOVERY_FRAME_JUMP_BUFFER_OFFSET)));
    MIR_var_t checkpoint_args[2] = {
        {MIR_T_P, "jump_buffer", 0},
#if defined(_WIN32)
        {MIR_T_P, "frame", 0},
#else
        {MIR_T_I64, "save_mask", 0},
#endif
    };
    MirImportEntry* checkpoint_import = em_ensure_import(em,
        LAMBDA_RECOVERY_FRAME_MIR_CHECKPOINT_NAME, MIR_T_I64, 2, checkpoint_args, 1, true);
    MIR_reg_t checkpoint = em_new_reg(em, "local_fault_checkpoint", MIR_T_I64);
    em_emit_insn(em, MIR_new_call_insn(em->ctx, 5,
        MIR_new_ref_op(em->ctx, checkpoint_import->proto), MIR_new_ref_op(em->ctx, checkpoint_import->import),
        MIR_new_reg_op(em->ctx, checkpoint), MIR_new_reg_op(em->ctx, jump_buffer),
#if defined(_WIN32)
        // mingw requires a null frame pointer, not the POSIX save-mask flag.
        MIR_new_int_op(em->ctx, 0)
#else
        MIR_new_int_op(em->ctx, 1)
#endif
        ));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, landing),
        MIR_new_reg_op(em->ctx, checkpoint), MIR_new_int_op(em->ctx, 0)));
    MIR_reg_t armed = em_call_1(em, "lambda_recovery_frame_arm", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UEXT8, MIR_new_reg_op(em->ctx, armed), MIR_new_reg_op(em->ctx, armed)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF, MIR_new_label_op(em->ctx, setup_failed), MIR_new_reg_op(em->ctx, armed)));
    MIR_reg_t produced = read_operand(em->call_owner, node, frame);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, produced)));
    (void)em_call_1(em, "lambda_recovery_frame_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
    em_emit_label(em, landing);
    MIR_reg_t restored = em_call_1(em, "lambda_recovery_frame_restore_landing", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UEXT8, MIR_new_reg_op(em->ctx, restored), MIR_new_reg_op(em->ctx, restored)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, restored_landing), MIR_new_reg_op(em->ctx, restored)));
    em_emit_label(em, setup_failed);
    (void)em_call_1(em, "lambda_recovery_frame_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, ITEM_ERROR)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
    em_emit_label(em, restored_landing);
    // restore the precise side-stack checkpoint before publishing the durable
    // fault; callees abandoned by the jump cannot run their own epilogues.
    MIR_reg_t fault = em_call_2(em, "lambda_recovery_frame_fault_item", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, em->frame.runtime), MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, fault)));
    (void)em_call_1(em, "lambda_recovery_frame_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_task_scope_finish(em, scope_mark, MIR_new_int_op(em->ctx, 1), true);
    em_emit_label(em, done);
    return result;
}
