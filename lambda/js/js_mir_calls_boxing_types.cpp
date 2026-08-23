#include "js_mir_internal.hpp"

static MIR_reg_t jm_normalize_numeric_result(JsMirTranspiler* mt, MIR_reg_t result,
        TypeId target_type, TypeId result_type, bool native_result) {
    if (native_result) {
        if (target_type == LMD_TYPE_FLOAT)
            return jm_ensure_native_float(mt, result, result_type);
        return jm_ensure_native_int(mt, result, result_type);
    }
    if (target_type == LMD_TYPE_FLOAT) return jm_emit_unbox_float(mt, result);
    MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
    return jm_emit_double_to_int(mt, as_dbl);
}

bool jm_is_native_binary_expression(JsMirTranspiler* mt, JsBinaryNode* bin) {
    if (!bin) return false;
    TypeId lt = jm_get_effective_type(mt, bin->left);
    TypeId rt = jm_get_effective_type(mt, bin->right);
    bool both_numeric = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
    return both_numeric && bin->op != JS_OP_EXP &&
        bin->op != JS_OP_AND && bin->op != JS_OP_OR;
}

bool jm_is_native_unary_expression(JsMirTranspiler* mt, JsUnaryNode* un) {
    if (!un || !un->operand) return false;
    TypeId op_type = jm_get_effective_type(mt, un->operand);
    bool op_numeric = op_type == LMD_TYPE_INT || op_type == LMD_TYPE_FLOAT;
    switch (un->op) {
    case JS_OP_MINUS: case JS_OP_SUB:
        return op_numeric;
    case JS_OP_INCREMENT: case JS_OP_DECREMENT:
        if (un->operand->node_type != JS_AST_NODE_IDENTIFIER) return false;
        {
            JsIdentifierNode* uid = (JsIdentifierNode*)un->operand;
            const char* uvname = jm_var_name(uid->name);
            JsMirVarEntry* uvar = jm_find_var(mt, uvname);
            return uvar && (uvar->type_id == LMD_TYPE_INT ||
                uvar->type_id == LMD_TYPE_FLOAT) && !uvar->from_env;
        }
    default:
        return false;
    }
}
#include "js_exec_profile.h"
#include "../../lib/lambda_alloca.h"

MIR_reg_t jm_box_float(JsMirTranspiler* mt, MIR_reg_t d_reg);

MIR_reg_t jm_call_1_or_inline(JsMirTranspiler* mt, const char* fn_name,
        MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1) {
    // The module-var slab can grow while a generated function is running.
    // Resolve it in the native helper so MIR cannot retain a pointer to the
    // retired exact allocation across a nested eval or compilation.
    jm_preserve_error_lane_carrier(mt, fn_name, true);
    MIR_reg_t result = em_call_1(&mt->em, fn_name, ret_type, a1t, a1, true);
    return jm_publish_call_result(mt, result, fn_name);
}

void jm_call_void_2_or_inline(JsMirTranspiler* mt, const char* fn_name,
        MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    jm_preserve_error_lane_carrier(mt, fn_name, false);
    em_call_void_2(&mt->em, fn_name, a1t, a1, a2t, a2, true);
}

struct JsMirScalarResultHome {
    int id;
    MIR_reg_t reg;
};

static JsMirScalarResultHome jm_new_scalar_result_home(JsMirTranspiler* mt,
        const char* operation) {
    JsMirScalarResultHome result = {-1, 0};
    if (!mt || !mt->em.frame.active) return result;
    result.id = em_scalar_home_new(&mt->em);
    result.reg = em_materialize_frame_ref(&mt->em,
        em_scalar_home_ref(&mt->em, result.id));
    if (!result.reg) {
        // every dynamic Item result needs a caller-owned scalar destination.
        log_error("js-mir scalar-home: failed to materialize %s result home",
            operation);
        abort();
    }
    return result;
}

static MIR_reg_t jm_finish_scalar_result_home(JsMirTranspiler* mt,
        JsMirScalarResultHome home, MIR_reg_t result) {
    em_scalar_home_bind(&mt->em, home.id, result);
    return result;
}

static MIR_reg_t jm_adopt_direct_scalar_result(JsMirTranspiler* mt,
        const FnVariantAnalysis* body, MIR_reg_t result) {
    if (!mt || !result || !mt->em.frame.active) return result;
    MirScalarReturnMode mode = body
        ? em_scalar_return_mode_for_class(body->result.normal.scalar_class)
        : MIR_SCALAR_RETURN_DYNAMIC;
    if (mode == MIR_SCALAR_RETURN_NONE) return result;
    int home_id = em_scalar_home_new(&mt->em);
    MIR_reg_t home = em_materialize_frame_ref(&mt->em,
        em_scalar_home_ref(&mt->em, home_id));
    if (!home) {
        log_error("js-mir scalar-home: failed to materialize direct-call result home");
        abort();
    }
    // D5.3: a direct JS call may return a number-home pointer from its
    // transient activation; adopt it before a local or closure can retain it.
    // The no-GC adopter already classifies packed values, so keep that check in
    // one runtime helper instead of expanding a tag branch at every call site.
    MIR_reg_t adopted = em_call_2(&mt->em, "lambda_item_adopt_scalar_home",
        MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
        MIR_T_P, MIR_new_reg_op(mt->ctx, home), true);
    em_scalar_home_bind(&mt->em, home_id, adopted);
    return adopted;
}

MIR_reg_t jm_call_direct_boxed(JsMirTranspiler* mt, JsFuncCollected* callee,
        int arg_count, MIR_reg_t* arg_regs, bool discard_result) {
    if (!mt || !callee || !callee->body_func_item || arg_count < 0) return 0;
    MIR_type_t* types = arg_count > 0 ? LAMBDA_ALLOCA(
        arg_count, MIR_type_t) : NULL;
    MIR_op_t* ops = arg_count > 0 ? LAMBDA_ALLOCA(
        arg_count, MIR_op_t) : NULL;
    for (int i = 0; i < arg_count; i++) {
        types[i] = MIR_T_I64;
        ops[i] = MIR_new_reg_op(mt->ctx, arg_regs[i]);
    }
    // A direct compiled call receives the caller's context register.  This is
    // immutable activation input, not a load from a shared runtime cell.
    // A discarded expression still has to transport an ERROR Item: the merged
    // lane makes the callee's returned Item the only exception signal, so
    // suppressing the normal result also suppresses throws from direct calls.
    (void)discard_result;
    MirCallOptions options = {true, false, 0};
    FnVariantAnalysis* body = fn_analysis_variant(&callee->analysis,
        FN_ENTRY_BOXED_BODY);
    MirCallResult direct = em_call_direct(&mt->em, callee->body_name,
        callee->body_func_item, body, arg_count, types, ops,
        &options);
    if (direct.normal.maybe_pending) {
        // direct boxed bodies share the lazy shape-2 transport with Lambda;
        // publish only after resolving the companion so JS cannot execute a
        // pending tag as an ordinary Item (D5.2.1v3, D5.2.2v3).
        direct.normal = em_materialize_pending_value(&mt->em, direct.normal);
    }
    MIR_reg_t result = jm_adopt_direct_scalar_result(mt, body, direct.normal.reg);
    return jm_publish_call_result(mt, result);
}

static bool jm_args_are_prerooted(JsMirTranspiler* mt, MIR_op_t args,
        MIR_op_t arg_count);

MIR_reg_t jm_call_function_into(JsMirTranspiler* mt, MIR_op_t func,
        MIR_op_t this_value, MIR_op_t args, MIR_op_t arg_count) {
    JsMirScalarResultHome home = jm_new_scalar_result_home(mt, "dynamic-call");
    if (!home.reg) return 0;
    bool prerooted_args = jm_args_are_prerooted(mt, args, arg_count);
    // The active scope is the emitter's provenance proof: its frame-relative
    // extent owns exactly this argument span until expression completion.
    MIR_reg_t result = jm_call_5(mt, prerooted_args
            ? "js_call_function_prerooted_args_into" : "js_call_function_into", MIR_T_I64,
        MIR_T_I64, func,
        MIR_T_I64, this_value,
        MIR_T_I64, args,
        MIR_T_I64, arg_count,
        MIR_T_P, MIR_new_reg_op(mt->ctx, home.reg));
    // The callee writes scalar payloads directly into this logical home; bind
    // the returned Item so subsequent calls share the liveness-coloured slot.
    return jm_finish_scalar_result_home(mt, home, result);
}

static bool jm_args_are_prerooted(JsMirTranspiler* mt, MIR_op_t args,
        MIR_op_t arg_count) {
    return mt && !mt->in_generator && mt->arg_stack_scope &&
        mt->arg_stack_scope->base_slot >= 0 && args.mode == MIR_OP_REG &&
        arg_count.mode == MIR_OP_INT &&
        args.u.reg == mt->arg_stack_scope->args_reg &&
        arg_count.u.i == mt->arg_stack_scope->slot_count &&
        mt->arg_stack_scope->slot_count > 0;
}

MIR_reg_t jm_construct_value_into(JsMirTranspiler* mt, MIR_op_t callee,
        MIR_op_t args, MIR_op_t arg_count, MIR_op_t new_target) {
    JsMirScalarResultHome home = jm_new_scalar_result_home(mt, "construct");
    if (!home.reg) return 0;
    bool prerooted_args = jm_args_are_prerooted(mt, args, arg_count);
    MIR_reg_t result = jm_call_6(mt, "js_construct_value", MIR_T_I64,
        MIR_T_I64, callee, MIR_T_I64, args, MIR_T_I64, arg_count,
        MIR_T_I64, new_target,
        MIR_T_P, MIR_new_reg_op(mt->ctx, home.reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, prerooted_args ? 1 : 0));
    return jm_finish_scalar_result_home(mt, home, result);
}

MIR_reg_t jm_apply_function_into(JsMirTranspiler* mt, MIR_op_t func,
        MIR_op_t this_value, MIR_op_t args) {
    JsMirScalarResultHome home = jm_new_scalar_result_home(mt, "apply");
    if (!home.reg) return 0;
    MIR_reg_t result = jm_call_4(mt, "js_apply_function_into", MIR_T_I64,
        MIR_T_I64, func, MIR_T_I64, this_value, MIR_T_I64, args,
        MIR_T_P, MIR_new_reg_op(mt->ctx, home.reg));
    return jm_finish_scalar_result_home(mt, home, result);
}

MIR_reg_t jm_super_call_class_into(JsMirTranspiler* mt, MIR_op_t callee,
        MIR_op_t this_value, MIR_op_t args, MIR_op_t arg_count) {
    JsMirScalarResultHome home = jm_new_scalar_result_home(mt, "super-call");
    if (!home.reg) return 0;
    MIR_reg_t result = jm_call_5(mt, "js_super_call_class_into", MIR_T_I64,
        MIR_T_I64, callee, MIR_T_I64, this_value, MIR_T_I64, args,
        MIR_T_I64, arg_count, MIR_T_P, MIR_new_reg_op(mt->ctx, home.reg));
    return jm_finish_scalar_result_home(mt, home, result);
}

MIR_reg_t jm_super_apply_class_into(JsMirTranspiler* mt, MIR_op_t callee,
        MIR_op_t this_value, MIR_op_t args) {
    JsMirScalarResultHome home = jm_new_scalar_result_home(mt, "super-apply");
    if (!home.reg) return 0;
    MIR_reg_t result = jm_call_4(mt, "js_super_apply_class_into", MIR_T_I64,
        MIR_T_I64, callee, MIR_T_I64, this_value, MIR_T_I64, args,
        MIR_T_P, MIR_new_reg_op(mt->ctx, home.reg));
    return jm_finish_scalar_result_home(mt, home, result);
}

MIR_reg_t jm_call_direct_native(JsMirTranspiler* mt, JsFuncCollected* callee,
        int arg_count, MIR_reg_t* arg_regs) {
    if (!mt || !callee || !callee->native_func_item || arg_count < 0) return 0;
    MIR_type_t* types = arg_count > 0
        ? LAMBDA_ALLOCA(arg_count, MIR_type_t) : NULL;
    MIR_op_t* ops = arg_count > 0
        ? LAMBDA_ALLOCA(arg_count, MIR_op_t) : NULL;
    for (int i = 0; i < arg_count; i++) {
        types[i] = jm_param_type(callee, i) == LMD_TYPE_FLOAT
            ? MIR_T_D : MIR_T_I64;
        ops[i] = MIR_new_reg_op(mt->ctx, arg_regs[i]);
    }
    FnVariantAnalysis* native = fn_analysis_variant(&callee->analysis,
        FN_ENTRY_NATIVE_BODY);
    MirCallOptions options = {true, false, 0};
    MirCallResult direct = em_call_direct(&mt->em, callee->name,
        callee->native_func_item, native, arg_count, types, ops,
        &options);
    if (direct.normal.maybe_pending) {
        // native entries may still use the boxed shape for a dynamic result;
        // resolve that pair before handing the register to later lowering.
        direct.normal = em_materialize_pending_value(&mt->em, direct.normal);
    }
    MIR_reg_t result = direct.normal.reg;
    mt->last_call_result_reg = 0;
    return result;
}

JsMirImportEntry* jm_ensure_import(JsMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    JsMirImportEntry* entry = em_ensure_import(&mt->em, name, ret_type, nargs, args, nres, true);
    return entry;
}

// Item(Item, Item)
JsMirImportEntry* jm_ensure_import_ii_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 2, args, 1);
}

// Item(Item)
JsMirImportEntry* jm_ensure_import_i_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Item(void)
JsMirImportEntry* jm_ensure_import_v_i(JsMirTranspiler* mt, const char* name) {
    return jm_ensure_import(mt, name, MIR_T_I64, 0, NULL, 1);
}

// Immediate Item constants are non-GC values, but each use gets its own MIR
// register because a value defined in a sibling control-flow block does not
// dominate the current use.
MIR_reg_t jm_boxed_immediate_const(JsMirTranspiler* mt, uint64_t item,
        const char* prefix) {
    if (!mt || !mt->ctx) return 0;
    MIR_reg_t result = jm_new_reg(mt, prefix, MIR_T_I64);
    jm_emit_reg_op(mt, MIR_MOV, result, MIR_new_int_op(mt->ctx, (int64_t)item));
    return result;
}

MIR_reg_t jm_emit_null(JsMirTranspiler* mt) {
    return jm_boxed_immediate_const(mt, ITEM_NULL_VAL, "null");
}

// v17: emit JS undefined value (for strict mode this coercion)
MIR_reg_t jm_emit_undefined(JsMirTranspiler* mt) {
    return jm_boxed_immediate_const(mt, ITEM_JS_UNDEFINED, "undef");
}

MIR_reg_t jm_emit_item_error(JsMirTranspiler* mt) {
    MIR_reg_t r = jm_new_reg(mt, "item_error", MIR_T_I64);
    mir_emit_i64_const_to_reg(mt->ctx, mt->em.func_item, r,
        (int64_t)(((uint64_t)LMD_TYPE_ERROR) << 56));
    return r;
}

// ============================================================================
// Boxing helpers
// ============================================================================

static MIR_reg_t jm_emit_double_bits(JsMirTranspiler* mt, MIR_reg_t d_reg);

// JavaScript has one number domain, including integral values. Keep an i64 only
// while it is an unboxed working register; once it becomes an Item, it must use
// the float representation so an ordinary JS number cannot alias an INT symbol.
MIR_reg_t jm_box_int_double(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    return jm_box_float(mt, d_reg);
}

// Box an integral JavaScript constant as a JS number Item.
MIR_reg_t jm_box_int_const(JsMirTranspiler* mt, int64_t value) {
    return jm_box_float_const(mt, (double)value);
}

// v20: Emit writeback from param register to arguments[param_index]
void jm_arguments_writeback_param(JsMirTranspiler* mt, int param_index, MIR_reg_t val_reg) {
    if (mt->arguments_reg == 0) return;
    jm_call_3(mt, "js_arguments_mapped_param_writeback", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->arguments_reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, param_index),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box a native JS-number register into its float Item representation.
MIR_reg_t jm_box_int_reg(JsMirTranspiler* mt, MIR_reg_t val) {
    MIR_reg_t as_double = jm_new_reg(mt, "boxid", MIR_T_D);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_I2D,
        MIR_new_reg_op(mt->ctx, as_double), MIR_new_reg_op(mt->ctx, val)));
    return jm_box_int_double(mt, as_double);
}

static MIR_reg_t jm_emit_double_bits(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    // Result15: the call edge to the 2-insn helper made numeric loops
    // placement-sensitive; reinterpret inline via the shared per-function
    // scratch slot (see em_emit_double_bits for the full rationale).
    return em_emit_double_bits(&mt->em, d_reg);
}

static MIR_reg_t jm_emit_bits_double(JsMirTranspiler* mt, MIR_reg_t bits_reg) {
    return em_emit_bits_double(&mt->em, bits_reg);
}

// Box double -> Item; the hot in-band arm is inline.
MIR_reg_t jm_box_float(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    MIR_reg_t bits = jm_emit_double_bits(mt, d_reg);
    MIR_reg_t in_band = jm_new_reg(mt, "jfdmask", MIR_T_I64);
    jm_emit_reg_binary_op(mt, MIR_AND, in_band, bits, MIR_new_int_op(mt->ctx, (int64_t)ITEM_DBL_MASK));

    MIR_reg_t result = jm_new_reg(mt, "boxf", MIR_T_I64);
    MIR_label_t l_in_band = jm_new_label(mt);
    MIR_label_t l_zero = jm_new_label(mt);
    MIR_label_t l_cold = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_emit_branch(mt, MIR_BT, l_in_band, in_band);

    MIR_reg_t is_zero = jm_new_reg(mt, "jfzero", MIR_T_I64);
    jm_emit_reg_binary_op(mt, MIR_DEQ, is_zero, d_reg, MIR_new_double_op(mt->ctx, 0.0));
    jm_emit_branch(mt, MIR_BT, l_zero, is_zero);
    jm_emit_jmp(mt, l_cold);

    jm_emit_label(mt, l_in_band);
    jm_emit_mov(mt, result, bits);
    jm_emit_jmp(mt, l_end);

    jm_emit_label(mt, l_zero);
    MIR_reg_t sign = jm_new_reg(mt, "jfsign", MIR_T_I64);
    jm_emit_reg_binary_op(mt, MIR_URSH, sign, bits, MIR_new_int_op(mt->ctx, 63));
    jm_emit_reg_op_binary(mt, MIR_OR, result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_FLOAT_P0), sign);
    jm_emit_jmp(mt, l_end);

    jm_emit_label(mt, l_cold);
    MIR_reg_t boxed = jm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D,
        MIR_new_reg_op(mt->ctx, d_reg));
    jm_emit_mov(mt, result, boxed);

    jm_emit_label(mt, l_end);
    return result;
}

bool jm_float_const_is_inline(double value) {
    uint64_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return value == 0.0 || (bits & ITEM_DBL_MASK) != 0;
}

MIR_reg_t jm_box_float_const(JsMirTranspiler* mt, double value) {
    uint64_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    uint64_t item = 0;
    if (value == 0.0) {
        item = ITEM_FLOAT_P0 | (bits >> 63);
    } else if (bits & ITEM_DBL_MASK) {
        item = bits;
    } else {
        // Out-of-band IEEE patterns require a managed numeric cell; retain the
        // dynamic boxing path only for constants that cannot live in Item bits.
        MIR_reg_t d = jm_new_reg(mt, "dbl", MIR_T_D);
        jm_emit_reg_op(mt, MIR_DMOV, d, MIR_new_double_op(mt->ctx, value));
        return jm_box_float(mt, d);
    }

    return jm_boxed_immediate_const(mt, item, "boxfc");
}

// Box string via s2it tagging: result = ptr ? (STR_TAG | ptr) : ITEM_NULL
MIR_reg_t jm_box_string(JsMirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = jm_new_reg(mt, "boxs", MIR_T_I64);
    MIR_label_t l_nn = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BT, l_nn, ptr_reg);
    jm_emit_reg_op(mt, MIR_MOV, result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
    jm_emit_jmp(mt, l_end);
    jm_emit_label(mt, l_nn);
    jm_emit_reg_op_binary(mt, MIR_OR, result, MIR_new_int_op(mt->ctx, (int64_t)STR_TAG), ptr_reg);
    jm_emit_label(mt, l_end);
    return result;
}

uint32_t jm_module_name_index(JsMirTranspiler* mt, const char* chars, uint32_t length) {
    if (!mt || !mt->tp || !mt->tp->name_pool || !chars) return UINT32_MAX;
    if (!mt->module_name_specs) {
        mt->module_name_specs = arraylist_new(16);
        if (!mt->module_name_specs) return UINT32_MAX;
    }
    for (int i = 0; i < mt->module_name_specs->length; i++) {
        NameRef spec = (NameRef)arraylist_get(mt->module_name_specs, i);
        if (spec && spec->len == length && memcmp(spec->chars, chars, length) == 0) {
            return mt->module_name_base + (uint32_t)i;
        }
    }
    String* stable = name_pool_create_len(mt->tp->name_pool, chars, length);
    if (!stable) return UINT32_MAX;
    // The compiler list is transient; the sealed PropertyKeySpec image is
    // built before the transpiler is released, so generated MIR never retains
    // this pool-owned spelling pointer (D5.4.3, D5.4.4).
    if (!arraylist_append(mt->module_name_specs, stable)) return UINT32_MAX;
    return mt->module_name_base + (uint32_t)(mt->module_name_specs->length - 1);
}

uint32_t jm_module_name_append(JsMirTranspiler* mt, const char* chars, uint32_t length) {
    if (!mt || !mt->tp || !mt->tp->name_pool || !chars) return UINT32_MAX;
    if (!mt->module_name_specs) {
        mt->module_name_specs = arraylist_new(16);
        if (!mt->module_name_specs) return UINT32_MAX;
    }
    String* stable = name_pool_create_len(mt->tp->name_pool, chars, length);
    if (!stable || !arraylist_append(mt->module_name_specs, stable)) return UINT32_MAX;
    // Range metadata deliberately preserves duplicate spellings: the table
    // position is the safe transport for per-entry kind bits (D4.6.2v2).
    return mt->module_name_base + (uint32_t)(mt->module_name_specs->length - 1);
}

bool jm_build_property_key_image(const PropertyKeySpec* inherited,
        uint32_t inherited_count, uint32_t inherited_bytes_size,
        const ArrayList* local_names, PropertyKeySpec** out_specs,
        uint32_t* out_count, uint32_t* out_bytes_size) {
    if (!out_specs || !out_count || !out_bytes_size) return false;
    *out_specs = NULL;
    *out_count = 0;
    *out_bytes_size = 0;
    uint32_t local_count = local_names ? (uint32_t)local_names->length : 0;
    if (inherited_count > UINT32_MAX - local_count) return false;
    uint32_t total = inherited_count + local_count;
    if (total == 0) return true;
    size_t spec_bytes = (size_t)total * sizeof(PropertyKeySpec);
    if (spec_bytes > UINT32_MAX) return false;
    if (inherited_count > 0 && (!inherited ||
            inherited_bytes_size < (size_t)inherited_count * sizeof(PropertyKeySpec))) {
        return false;
    }
    size_t total_bytes = spec_bytes;
    for (uint32_t i = 0; i < inherited_count; i++) {
        const PropertyKeySpec* spec = &inherited[i];
        if (spec->predefined_id != NAME_ID_NONE) continue;
        if (spec->name_offset < (size_t)inherited_count * sizeof(PropertyKeySpec) ||
                spec->name_offset > inherited_bytes_size ||
                spec->name_length >= inherited_bytes_size - spec->name_offset) return false;
        const uint8_t* inherited_bytes = (const uint8_t*)inherited;
        if (inherited_bytes[spec->name_offset + spec->name_length] != '\0') return false;
        if (total_bytes >= UINT32_MAX - spec->name_length) return false;
        total_bytes += spec->name_length + 1;
    }
    for (uint32_t i = 0; i < local_count; i++) {
        NameRef name = (NameRef)arraylist_get((ArrayList*)local_names, (int)i);
        if (!name || name->len >= UINT32_MAX - total_bytes) return false;
        NameId id = property_key_kind(name) == NAME_KEY_STRING
            ? well_known_name_id({name->chars, name->len}) : NAME_ID_NONE;
        if (id == NAME_ID_NONE) {
            total_bytes += name->len + 1;
        }
    }
    if (total_bytes > UINT32_MAX) return false;
    PropertyKeySpec* image = (PropertyKeySpec*)mem_calloc(1, total_bytes,
        MEM_CAT_JS_RUNTIME);
    if (!image) return false;
    uint8_t* image_bytes = (uint8_t*)image;
    uint32_t cursor = (uint32_t)spec_bytes;
    for (uint32_t i = 0; i < inherited_count; i++) {
        const PropertyKeySpec* source = &inherited[i];
        PropertyKeySpec* target = &image[i];
        *target = *source;
        if (source->predefined_id != NAME_ID_NONE) {
            target->name_offset = 0;
            target->name_length = 0;
            continue;
        }
        target->name_offset = cursor;
        const uint8_t* source_bytes = (const uint8_t*)inherited;
        memcpy(image_bytes + cursor, source_bytes + source->name_offset,
            source->name_length);
        cursor += source->name_length;
        image_bytes[cursor++] = '\0';
    }
    for (uint32_t i = 0; i < local_count; i++) {
        NameRef name = (NameRef)arraylist_get((ArrayList*)local_names, (int)i);
        PropertyKeySpec* target = &image[inherited_count + i];
        NameId id = property_key_kind(name) == NAME_KEY_STRING
            ? well_known_name_id({name->chars, name->len}) : NAME_ID_NONE;
        if (id != NAME_ID_NONE) {
            target->predefined_id = id;
            continue;
        }
        target->name_offset = cursor;
        target->name_length = name->len;
        memcpy(image_bytes + cursor, name->chars, name->len);
        cursor += name->len;
        image_bytes[cursor++] = '\0';
    }
    *out_specs = image;
    *out_count = total;
    *out_bytes_size = cursor;
    return true;
}

MIR_reg_t jm_module_name_id_at_index(JsMirTranspiler* mt, uint32_t index) {
    if (!mt) return 0;
    if (mt->em.func_item != mt->module_name_id_cache_func) {
        mt->module_name_id_cache_func = mt->em.func_item;
        mt->module_name_id_cache_count = 0;
    }
    for (int i = 0; i < mt->module_name_id_cache_count; i++) {
        if (mt->module_name_id_cache[i].module_name_index == index &&
                mt->module_name_id_cache[i].direct_name_id == NAME_ID_NONE) {
            return mt->module_name_id_cache[i].reg;
        }
    }
    MIR_reg_t result = jm_call_1(mt, "js_active_module_name_id", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)index));
    if (result && mt->module_name_id_cache_count <
            (int)(sizeof(mt->module_name_id_cache) /
                  sizeof(mt->module_name_id_cache[0]))) {
        int slot = mt->module_name_id_cache_count++;
        mt->module_name_id_cache[slot].module_name_index = index;
        mt->module_name_id_cache[slot].direct_name_id = NAME_ID_NONE;
        mt->module_name_id_cache[slot].reg = result;
    }
    return result;
}

MIR_reg_t jm_module_name_id(JsMirTranspiler* mt,
        const char* chars, uint32_t length) {
    if (!mt) return 0;
    // Generated catalog names are stable across modules, so keep their NameId
    // as an immediate. Only arbitrary sealed spellings need the active module
    // table lookup (D4.6.1v2, D4.6.2v2).
    NameId generated_id = well_known_name_id({chars, length});
    if (generated_id != NAME_ID_NONE) {
        if (mt->em.func_item != mt->module_name_id_cache_func) {
            mt->module_name_id_cache_func = mt->em.func_item;
            mt->module_name_id_cache_count = 0;
        }
        for (int i = 0; i < mt->module_name_id_cache_count; i++) {
            if (mt->module_name_id_cache[i].module_name_index == UINT32_MAX &&
                    mt->module_name_id_cache[i].direct_name_id == generated_id) {
                return mt->module_name_id_cache[i].reg;
            }
        }
        MIR_reg_t id = jm_new_reg(mt, "nameid", MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, id, MIR_new_int_op(mt->ctx, (int64_t)generated_id));
        if (mt->module_name_id_cache_count <
                (int)(sizeof(mt->module_name_id_cache) /
                      sizeof(mt->module_name_id_cache[0]))) {
            int slot = mt->module_name_id_cache_count++;
            mt->module_name_id_cache[slot].module_name_index = UINT32_MAX;
            mt->module_name_id_cache[slot].direct_name_id = generated_id;
            mt->module_name_id_cache[slot].reg = id;
        }
        return id;
    }
    uint32_t index = jm_module_name_index(mt, chars, length);
    return jm_module_name_id_at_index(mt, index);
}

MIR_reg_t jm_box_property_name_literal(JsMirTranspiler* mt,
        const char* chars, uint32_t length) {
    if (!mt) return 0;
    NameId direct_name_id = well_known_name_id({chars, length});
    uint32_t module_name_index = direct_name_id == NAME_ID_NONE
        ? jm_module_name_index(mt, chars, length) : UINT32_MAX;
    if (mt->em.func_item != mt->property_name_cache_func) {
        mt->property_name_cache_func = mt->em.func_item;
        mt->property_name_cache_count = 0;
    }
    for (int i = 0; i < mt->property_name_cache_count; i++) {
        if (mt->property_name_cache[i].module_name_index == module_name_index &&
                mt->property_name_cache[i].direct_name_id == direct_name_id) {
            return mt->property_name_cache[i].reg;
        }
    }
    MIR_reg_t result = jm_call_2(mt, "js_active_module_name_item", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)module_name_index),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)direct_name_id));
    if (result && mt->property_name_cache_count <
            (int)(sizeof(mt->property_name_cache) /
                  sizeof(mt->property_name_cache[0]))) {
        int slot = mt->property_name_cache_count++;
        mt->property_name_cache[slot].module_name_index = module_name_index;
        mt->property_name_cache[slot].direct_name_id = direct_name_id;
        mt->property_name_cache[slot].reg = result;
    }
    return result;
}

MIR_reg_t jm_string_literal_chars(JsMirTranspiler* mt, const char* str, int len) {
    if (!mt || len < 0 || (!str && len > 0)) return 0;
    MIR_reg_t chars = jm_new_reg(mt, "strlit", MIR_T_P);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, chars),
        MIR_new_str_op(mt->ctx, {(size_t)len, str ? str : ""})));
    return chars;
}

MIR_reg_t jm_box_string_literal(JsMirTranspiler* mt, const char* str, int len) {
    if (!mt || !str || len < 0) return jm_emit_null(mt);
    // String values are not property identities. Keep their bytes in the MIR
    // artifact and allocate an ordinary GC String at evaluation time so source
    // text, diagnostics, and literals never consume permanent NameIds.
    MIR_reg_t chars = jm_string_literal_chars(mt, str, len);
    return jm_call_2(mt, "js_make_string_len", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, chars),
        MIR_T_I64, MIR_new_int_op(mt->ctx, len));
}

// Phase-5C: emit either `js_create_data_property(obj, key, fn)` for regular methods or
// `js_install_user_accessor(obj, key, fn, is_setter)` for getter/setter
// accessors. Replaces the legacy pattern of writing to a `__get_X`/`__set_X`
// magic-key marker that was caught by the property-set intercept.
void jm_emit_install_method_or_accessor(JsMirTranspiler* mt,
    MIR_reg_t obj, MIR_reg_t key, MIR_reg_t fn_item,
    bool is_getter, bool is_setter) {
    key = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, key);
    jm_emit_error_lane_propagate_check(mt);
    int64_t prefix_kind = is_getter ? 1 : (is_setter ? 2 : 0);
    jm_call_void_3(mt, "js_set_function_name_from_property_key_if_anonymous",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
        MIR_T_I64, MIR_new_int_op(mt->ctx, prefix_kind));
    jm_callr_void_2(mt, "js_set_method_home_from_target", obj, fn_item);
    if (is_getter || is_setter) {
        MIR_reg_t is_set = jm_new_reg(mt, "is_set", MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, is_set, MIR_new_int_op(mt->ctx, is_setter ? 1 : 0));
        jm_callr_4(mt, "js_install_user_accessor", MIR_T_I64, obj, key, fn_item, is_set);
        jm_emit_error_lane_propagate_check(mt);
    } else {
        jm_call_void_0(mt, "js_private_field_init_begin");
        jm_callr_3(mt, "js_create_data_property", MIR_T_I64, obj, key, fn_item);
        jm_emit_error_lane_propagate_check(mt);
        jm_call_void_0(mt, "js_private_field_init_end");
        jm_callr_void_2(mt, "js_mark_private_method_non_writable", obj, key);
    }
}

static const char* jm_private_display_suffix_from_name(const char* name) {
    return name && name[0] == '#' ? name + 1 : name;
}

static const char* jm_function_display_name(const char* name, char* buffer,
        size_t buffer_size) {
    if (!name || !name[0]) return NULL;
    const char* display_name = name;
    if (name[0] == '#') {
        snprintf(buffer, buffer_size, "#%s", jm_private_display_suffix_from_name(name));
        display_name = buffer;
    } else if (strncmp(name, "get #", 5) == 0) {
        snprintf(buffer, buffer_size, "get #%s", jm_private_display_suffix_from_name(name + 4));
        display_name = buffer;
    } else if (strncmp(name, "set #", 5) == 0) {
        snprintf(buffer, buffer_size, "set #%s", jm_private_display_suffix_from_name(name + 4));
        display_name = buffer;
    }
    return display_name;
}

// Helper: emit js_set_function_name call if name is non-empty, and formal_length if needed
void jm_emit_set_function_name(JsMirTranspiler* mt, MIR_reg_t fn_reg, const char* name, int formal_length ) {
    if (name && name[0]) {
        char priv_buf[256];
        const char* display_name = jm_function_display_name(name, priv_buf,
            sizeof(priv_buf));
        MIR_reg_t name_reg = jm_box_string_literal(mt, display_name, strlen(display_name));
        jm_callr_void_2(mt, "js_set_function_name", fn_reg, name_reg);
    }
    if (formal_length >= 0) {
        jm_call_void_2(mt, "js_set_formal_length",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, formal_length));
    }
}

void jm_emit_set_class_assignment_name(JsMirTranspiler* mt, JsAssignmentNode* asgn, MIR_reg_t rhs, String* name) {
    if (!asgn || asgn->lhs_is_parenthesized || !asgn->right || !name) return;
    if (asgn->op != JS_OP_ASSIGN) return;
    if (asgn->right->node_type != JS_AST_NODE_CLASS_EXPRESSION &&
        asgn->right->node_type != JS_AST_NODE_CLASS_DECLARATION) return;
    JsClassNode* cls = (JsClassNode*)asgn->right;
    if (cls->name) return;
    MIR_reg_t name_reg = jm_box_string_literal(mt, name->chars, (int)name->len);
    jm_callr_void_2(mt, "js_set_class_name", rhs, name_reg);
}

static bool jm_function_source_span(JsMirTranspiler* mt,
        JsFunctionNode* fn_node, const char** text_out, uint32_t* len_out) {
    if (!mt || !fn_node || !text_out || !len_out || !mt->tp ||
            !mt->tp->source) return false;
    uint32_t start = fn_node->source_span.start_byte;
    uint32_t end = fn_node->source_span.end_byte;
    if (end <= start || end > mt->tp->source_length) return false;
    const char* text = mt->tp->source + start;
    uint32_t len = end - start;
    // Cap source text to avoid overly large string literals in MIR
    if (len > 65536) return false;
    // Skip leading whitespace and comments, then strip "static" keyword if present.
    // Class static methods span from (optional comment + "static" keyword) through body.
    // We want source text to start at "get"/"set"/function-keyword, not "static".
    // Helper lambda: skip whitespace in-place
    auto skip_ws = [](const char* p, uint32_t& rem) {
        while (rem > 0 && (p[0] == ' ' || p[0] == '\t' || p[0] == '\n' || p[0] == '\r')) { p++; rem--; }
        return p;
    };
    (void)skip_ws; // suppress unused warning
    // Step 1: trim leading whitespace
    while (len > 0 && (text[0] == ' ' || text[0] == '\t' || text[0] == '\n' || text[0] == '\r')) { text++; len--; }
    // Step 2: skip leading block comments (/* ... */) and then check for "static"
    {
        const char* scan = text;
        uint32_t slen = len;
        // skip any leading block/line comments
        bool advanced = true;
        while (advanced && slen > 0) {
            advanced = false;
            while (slen > 0 && (scan[0] == ' ' || scan[0] == '\t' || scan[0] == '\n' || scan[0] == '\r')) { scan++; slen--; advanced = true; }
            if (slen >= 2 && scan[0] == '/' && scan[1] == '*') {
                // skip block comment
                scan += 2; slen -= 2;
                while (slen >= 2 && !(scan[0] == '*' && scan[1] == '/')) { scan++; slen--; }
                if (slen >= 2) { scan += 2; slen -= 2; }
                advanced = true;
            } else if (slen >= 2 && scan[0] == '/' && scan[1] == '/') {
                // skip line comment
                scan += 2; slen -= 2;
                while (slen > 0 && scan[0] != '\n') { scan++; slen--; }
                advanced = true;
            }
        }
        // Now scan points past any leading comments. Check if next token is "static".
        if (slen >= 7 && strncmp(scan, "static", 6) == 0 &&
            (scan[6] == ' ' || scan[6] == '\t' || scan[6] == '\n' || scan[6] == '/' )) {
            // Static method: advance scan past "static" and then skip whitespace/comments again
            scan += 6; slen -= 6;
            // skip whitespace/comments after "static"
            bool adv2 = true;
            while (adv2 && slen > 0) {
                adv2 = false;
                while (slen > 0 && (scan[0] == ' ' || scan[0] == '\t' || scan[0] == '\n' || scan[0] == '\r')) { scan++; slen--; adv2 = true; }
                if (slen >= 2 && scan[0] == '/' && scan[1] == '*') {
                    scan += 2; slen -= 2;
                    while (slen >= 2 && !(scan[0] == '*' && scan[1] == '/')) { scan++; slen--; }
                    if (slen >= 2) { scan += 2; slen -= 2; }
                    adv2 = true;
                }
            }
            text = scan;
            len = slen;
        }
    }
    // Tree-sitter may extend node ranges to include trailing comments.
    // Trim back to the actual closing '}' for block-bodied functions.
    if (len > 0 && fn_node->body && fn_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        while (len > 1 && text[len - 1] != '}') len--;
    }
    *text_out = text;
    *len_out = len;
    return true;
}

// Helper: emit js_set_function_source call to store original source text for toString
void jm_emit_set_function_source(JsMirTranspiler* mt, MIR_reg_t fn_reg, JsFunctionNode* fn_node) {
    if (!fn_node) return;
    const char* text = NULL;
    uint32_t len = 0;
    if (!jm_function_source_span(mt, fn_node, &text, &len)) return;
    MIR_reg_t src_reg = jm_box_string_literal(mt, text, len);
    jm_callr_void_2(mt, "js_set_function_source", fn_reg, src_reg);
}

void jm_emit_finalize_function(JsMirTranspiler* mt, MIR_reg_t fn_reg,
        JsFuncCollected* fc, JsFunctionNode* fn_node) {
    if (!mt || !fn_reg || !fc || !fn_node) return;
    char display_buffer[256];
    const char* source_name = fn_node->name ? fn_node->name->chars : NULL;
    const char* display_name = jm_function_display_name(source_name,
        display_buffer, sizeof(display_buffer));
    const char* source_text = NULL;
    uint32_t source_len = 0;
    if (!jm_function_source_span(mt, fn_node, &source_text, &source_len)) {
        source_text = NULL;
        source_len = 0;
    }
    uint32_t name_len = display_name ? (uint32_t)strlen(display_name) : 0;
    MIR_reg_t name_chars = display_name
        ? jm_string_literal_chars(mt, display_name, (int)name_len) : 0;
    MIR_reg_t source_chars = source_text
        ? jm_string_literal_chars(mt, source_text, (int)source_len) : 0;
    uint64_t span_lengths = (uint64_t)name_len | ((uint64_t)source_len << 32);
    int flags = 0;
    if (fn_node->is_generator && fn_node->is_async) {
        flags |= JS_FUNC_INIT_ASYNC_GENERATOR;
    } else if (fn_node->is_generator) {
        flags |= JS_FUNC_INIT_GENERATOR;
    } else if (fn_node->is_async) {
        flags |= JS_FUNC_INIT_ASYNC;
    }
    if (fn_node->is_arrow) flags |= JS_FUNC_INIT_ARROW;
    if (fc->is_strict) flags |= JS_FUNC_INIT_STRICT;
    if (fc->uses_with) flags |= JS_FUNC_INIT_USES_WITH;
    flags |= JS_FUNC_INIT_ANALYSIS_KNOWN;
    if (fc->observes_this) flags |= JS_FUNC_INIT_READS_THIS;
    if (fc->observes_new_target) flags |= JS_FUNC_INIT_READS_NEW_TARGET;
    if (fc->is_class_field_initializer) {
        flags |= JS_FUNC_INIT_CLASS_FIELD_INITIALIZER;
    }
    // Compiled public wrappers take Context and publish a wide return payload
    // through its companion slot; native callbacks keep their explicit result
    // homes because those are ownership transfers, not generated ABI lanes.
    flags |= JS_FUNC_INIT_MIR_PUBLIC_ABI;
    flags |= JS_FUNC_INIT_MIR_CONTEXT_ABI;
    // D5.4.3: no allocating operation separates callable creation from this
    // GC-aware transaction. The runtime roots the fresh callable before it
    // materializes either string or publishes metadata and capabilities.
    jm_call_void_6(mt, "js_finalize_function",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
        MIR_T_P, name_chars ? MIR_new_reg_op(mt->ctx, name_chars)
                            : MIR_new_int_op(mt->ctx, 0),
        MIR_T_P, source_chars ? MIR_new_reg_op(mt->ctx, source_chars)
                              : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)span_lengths),
        MIR_T_I64, MIR_new_int_op(mt->ctx, fc->formal_length),
        MIR_T_I64, MIR_new_int_op(mt->ctx, flags));
}

// Publish a class's source in the callable carrier so Function.prototype
// toString does not need an observable backing property or a name-based probe.
void jm_emit_set_class_source(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassNode* cls_node) {
    if (!cls_node || !mt->tp || !mt->tp->source) return;
    uint32_t start = cls_node->source_span.start_byte;
    uint32_t end = cls_node->source_span.end_byte;
    if (end <= start || end > mt->tp->source_length) return;
    const char* text = mt->tp->source + start;
    uint32_t len = end - start;
    if (len > 65536) return;
    // Trim leading whitespace
    while (len > 0 && (text[0] == ' ' || text[0] == '\t' || text[0] == '\n' || text[0] == '\r')) { text++; len--; }
    // Tree-sitter may extend the node end past trailing comments; trim to closing '}'
    while (len > 1 && text[len - 1] != '}') len--;
    MIR_reg_t src_reg = jm_box_string_literal(mt, text, len);
    jm_callr_void_2(mt, "js_set_function_source", cls_obj, src_reg);
}

// ============================================================================
// Inline unboxing helpers (MIR instructions, no function calls)
// ============================================================================

// Unbox Item → native int64_t: sign-extend lower 56 bits
MIR_reg_t jm_emit_unbox_int(JsMirTranspiler* mt, MIR_reg_t item) {
    MIR_reg_t result = jm_new_reg(mt, "ubi", MIR_T_I64);
    // shift left 8, arithmetic shift right 8 for sign extension
    jm_emit_reg_binary_op(mt, MIR_LSH, result, item, MIR_new_int_op(mt->ctx, 8));
    jm_emit_reg_binary_op(mt, MIR_RSH, result, result, MIR_new_int_op(mt->ctx, 8));
    return result;
}

// Unbox Item → native double; inline the raw-bit arm for self-tagged floats.
MIR_reg_t jm_emit_unbox_float(JsMirTranspiler* mt, MIR_reg_t item) {
    // Safety: if item is already a native double, return it directly
    MIR_type_t rt = MIR_reg_type(mt->ctx, item, mt->em.func);
    if (rt == MIR_T_D) return item;
    if (rt == MIR_T_F) {
        MIR_reg_t d = jm_new_reg(mt, "f2d_ub", MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_F2D,
            MIR_new_reg_op(mt->ctx, d), MIR_new_reg_op(mt->ctx, item)));
        return d;
    }
    MIR_reg_t in_band = jm_new_reg(mt, "jfumask", MIR_T_I64);
    jm_emit_reg_binary_op(mt, MIR_AND, in_band, item, MIR_new_int_op(mt->ctx, (int64_t)ITEM_DBL_MASK));
    MIR_reg_t result = jm_new_reg(mt, "junboxf", MIR_T_D);
    MIR_label_t l_inline = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BT, l_inline, in_band);
    MIR_reg_t cold = jm_callr_1(mt, "it2d", MIR_T_D, item);
    jm_emit_dmov(mt, result, cold);
    jm_emit_jmp(mt, l_end);
    jm_emit_label(mt, l_inline);
    MIR_reg_t inline_d = jm_emit_bits_double(mt, item);
    jm_emit_dmov(mt, result, inline_d);
    jm_emit_label(mt, l_end);
    return result;
}

// Convert native int64_t → native double
MIR_reg_t jm_emit_int_to_double(JsMirTranspiler* mt, MIR_reg_t int_reg) {
    MIR_reg_t result = jm_new_reg(mt, "i2d", MIR_T_D);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, int_reg)));
    return result;
}

// Convert native double → native int64_t (truncate)
MIR_reg_t jm_emit_double_to_int(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    MIR_reg_t result = jm_new_reg(mt, "d2i", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_D2I, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, d_reg)));
    return result;
}

// Ensure a register is native int64_t, converting from boxed if needed
MIR_reg_t jm_ensure_native_int(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type) {
    MIR_type_t rt = MIR_reg_type(mt->ctx, reg, mt->em.func);
    if (rt == MIR_T_I64 && src_type == LMD_TYPE_INT) return reg;
    if (rt == MIR_T_D) return jm_emit_double_to_int(mt, reg);
    if (rt == MIR_T_F) {
        MIR_reg_t d = jm_new_reg(mt, "f2d_i", MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_F2D,
            MIR_new_reg_op(mt->ctx, d), MIR_new_reg_op(mt->ctx, reg)));
        return jm_emit_double_to_int(mt, d);
    }
    if (src_type == LMD_TYPE_INT) return reg;  // already native int
    if (src_type == LMD_TYPE_FLOAT) {
        // Boxed JS Number values also live in I64 lanes; unbox before integer conversion.
        MIR_reg_t as_dbl = jm_emit_unbox_float(mt, reg);
        return jm_emit_double_to_int(mt, as_dbl);
    }
    // boxed Item of unknown type → call it2i for safe conversion
    // (handles INT, FLOAT, INT64, BOOL items correctly)
    return jm_callr_1(mt, "it2i", MIR_T_I64, reg);
}

// Ensure a register is native double, converting from int or boxed if needed
MIR_reg_t jm_ensure_native_float(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type) {
    MIR_type_t rt = MIR_reg_type(mt->ctx, reg, mt->em.func);
    if (rt == MIR_T_D) return reg;
    if (rt == MIR_T_F) {
        MIR_reg_t d = jm_new_reg(mt, "f2d_f", MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_F2D,
            MIR_new_reg_op(mt->ctx, d), MIR_new_reg_op(mt->ctx, reg)));
        return d;
    }
    if (rt == MIR_T_I64 && src_type == LMD_TYPE_INT)
        return jm_emit_int_to_double(mt, reg);
    if (rt == MIR_T_I64 && src_type == LMD_TYPE_FLOAT) {
        // Boxed JS Number values also live in I64 lanes; converting the tagged
        // Item bits as an integer corrupts nested native-call arithmetic.
        return jm_emit_unbox_float(mt, reg);
    }
    if (src_type == LMD_TYPE_FLOAT) return reg;  // already native double
    if (src_type == LMD_TYPE_INT) return jm_emit_int_to_double(mt, reg);
    // boxed Item → unbox
    return jm_emit_unbox_float(mt, reg);
}

// Box a native value into an Item based on its type
static MIR_reg_t jm_box_native_impl(JsMirTranspiler* mt, MIR_reg_t reg,
        TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:   return jm_box_int_reg(mt, reg);
    case LMD_TYPE_FLOAT: {
        // P6 inlining can preserve a widened FLOAT semantic type while the
        // emitted native value is still an integer register; push_d requires D.
        MIR_reg_t d = jm_ensure_native_float(mt, reg, type_id);
        return jm_box_float(mt, d);
    }
    case LMD_TYPE_BOOL: {
        MIR_reg_t result = jm_new_reg(mt, "boxb", MIR_T_I64);
        uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
        jm_emit_reg_op_binary(mt, MIR_OR, result, MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG), reg);
        return result;
    }
    default: return reg;  // already boxed
    }
}

MirValue jm_convert_rep(void* owner, MirValue value,
        ValueRep required) {
    JsMirTranspiler* mt = (JsMirTranspiler*)owner;
    MIR_reg_t reg = 0;
    if (required == VALUE_REP_ITEM) {
        reg = jm_box_native_impl(mt, value.reg, value.semantic_type);
    } else if (value.rep == VALUE_REP_ITEM) {
        reg = required == VALUE_REP_F64
            ? jm_emit_unbox_float(mt, value.reg)
            : jm_emit_unbox_int(mt, value.reg);
    }
    if (!reg) return value;
    MIR_type_t mir_type = required == VALUE_REP_F64 ? MIR_T_D : MIR_T_I64;
    MirValue converted = em_value(reg, mir_type, value.semantic_type,
        required, required == VALUE_REP_ITEM ? JIT_VALUE_BOXED_ITEM
            : JIT_VALUE_NON_GC_SCALAR);
    converted.scalar_home_id = em_scalar_home_for_reg(&mt->em, reg);
    converted.scalar_provenance = converted.scalar_home_id
        ? SCALAR_PROVENANCE_ACTIVATION_HOME : SCALAR_PROVENANCE_NONE;
    return converted;
}

MIR_reg_t jm_box_native(JsMirTranspiler* mt, MIR_reg_t reg,
        TypeId type_id) {
    ValueRep actual = type_id == LMD_TYPE_FLOAT ? VALUE_REP_F64
        : type_id == LMD_TYPE_INT || type_id == LMD_TYPE_BOOL
            ? VALUE_REP_I64 : VALUE_REP_ITEM;
    MIR_type_t mir_type = actual == VALUE_REP_F64 ? MIR_T_D : MIR_T_I64;
    MirValue value = em_value(reg, mir_type, type_id, actual,
        actual == VALUE_REP_ITEM ? JIT_VALUE_BOXED_ITEM
            : JIT_VALUE_NON_GC_SCALAR);
    return em_require_rep(&mt->em, value, VALUE_REP_ITEM).reg;
}

// Safety net: ensure a register holds a boxed Item (I64).
// If it's a native double/float, box it; otherwise return as-is.
MIR_reg_t jm_ensure_boxed(JsMirTranspiler* mt, MIR_reg_t reg) {
    MIR_type_t rtype = MIR_reg_type(mt->ctx, reg, mt->em.func);
    if (rtype == MIR_T_D) return jm_box_float(mt, reg);
    if (rtype == MIR_T_F) {
        MIR_reg_t d = jm_new_reg(mt, "f2d_box", MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_F2D,
            MIR_new_reg_op(mt->ctx, d), MIR_new_reg_op(mt->ctx, reg)));
        return jm_box_float(mt, d);
    }
    return reg;
}

// ============================================================================
// Type inference for expressions (jm_get_effective_type)
// ============================================================================

// Forward declarations
JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call);
JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn);
// A5 forward declaration
void jm_scan_ctor_props(JsFuncCollected* fc, JsAstNode* body);

// Returns the inferred TypeId for a JS AST expression node.
// LMD_TYPE_INT, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_STRING → known type
// LMD_TYPE_ANY → unknown (must use boxed path)
TypeId jm_get_effective_type(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return LMD_TYPE_ANY;

    switch (node->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        switch (lit->literal_type) {
        case JS_LITERAL_NUMBER: {
            if (lit->is_bigint) return LMD_TYPE_DECIMAL;
            return LMD_TYPE_FLOAT;
        }
        case JS_LITERAL_BOOLEAN:  return LMD_TYPE_BOOL;
        case JS_LITERAL_STRING:   return LMD_TYPE_STRING;
        case JS_LITERAL_NULL:     return LMD_TYPE_NULL;
        case JS_LITERAL_UNDEFINED: return LMD_TYPE_UNDEFINED;
        default:
            // shared AST tags include frontend-specific Python literals; JS treats unknown tags as dynamic.
            return LMD_TYPE_ANY;
        }
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        const char* vname = jm_var_name(id->name);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var) return var->type_id;
        // P5: Check module-level variable type for arithmetic type inference.
        // When a MODVAR was initialized with a numeric literal, modvar_type is set
        // to LMD_TYPE_INT or LMD_TYPE_FLOAT; this enables native arithmetic paths.
        if (mt->module_consts) {
            JsModuleConstEntry* mv_mc = jm_find_module_const(mt, vname);
            if (mv_mc && mv_mc->const_type == MCONST_MODVAR &&
                (mv_mc->modvar_type == LMD_TYPE_INT || mv_mc->modvar_type == LMD_TYPE_FLOAT))
                return mv_mc->modvar_type;
        }
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        // comparison operators always return bool
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
        case JS_OP_INSTANCEOF: case JS_OP_IN:
            return LMD_TYPE_BOOL;
        default: break;
        }
        // arithmetic operators: propagate types
        TypeId left_t  = jm_get_effective_type(mt, bin->left);
        TypeId right_t = jm_get_effective_type(mt, bin->right);
        switch (bin->op) {
        case JS_OP_ADD:
            // if either is string, result is string (JS concatenation)
            if (left_t == LMD_TYPE_STRING || right_t == LMD_TYPE_STRING)
                return LMD_TYPE_STRING;
            // Unknown operands can become strings through ToPrimitive, so `+`
            // is numeric only when both sides are statically numeric.
            if ((left_t == LMD_TYPE_INT || left_t == LMD_TYPE_FLOAT) &&
                (right_t == LMD_TYPE_INT || right_t == LMD_TYPE_FLOAT))
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_SUB: case JS_OP_MUL:
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_EXP:
            // pow() always returns double
            if ((left_t == LMD_TYPE_INT || left_t == LMD_TYPE_FLOAT) &&
                (right_t == LMD_TYPE_INT || right_t == LMD_TYPE_FLOAT))
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_DIV:
            // JS division always produces float (7/2 == 3.5)
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_FLOAT;
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_MOD:
            // modulo uses fmod() → always returns float (handles x%0 → NaN)
            if ((left_t == LMD_TYPE_INT || left_t == LMD_TYPE_FLOAT) &&
                (right_t == LMD_TYPE_INT || right_t == LMD_TYPE_FLOAT))
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_BIT_AND: case JS_OP_BIT_OR: case JS_OP_BIT_XOR:
        case JS_OP_BIT_LSHIFT: case JS_OP_BIT_RSHIFT: case JS_OP_BIT_URSHIFT:
            // bigint bitwise/shift operators return BigInt, so only Number-proven operands may use the Number result type.
            if ((left_t == LMD_TYPE_INT || left_t == LMD_TYPE_FLOAT) &&
                (right_t == LMD_TYPE_INT || right_t == LMD_TYPE_FLOAT))
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_AND: case JS_OP_OR:
            return LMD_TYPE_ANY;  // logical AND/OR return one of the operands
        default:
            return LMD_TYPE_ANY;
        }
    }

    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        switch (un->op) {
        case JS_OP_NOT:    return LMD_TYPE_BOOL;
        case JS_OP_TYPEOF: return LMD_TYPE_STRING;
        case JS_OP_BIT_NOT: return LMD_TYPE_FLOAT;
        case JS_OP_PLUS: case JS_OP_ADD: {
            TypeId t = jm_get_effective_type(mt, un->operand);
            if (t == LMD_TYPE_FLOAT || t == LMD_TYPE_INT) return t;
            return LMD_TYPE_ANY;
        }
        case JS_OP_MINUS: case JS_OP_SUB: {
            TypeId t = jm_get_effective_type(mt, un->operand);
            if (t == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
            if (t == LMD_TYPE_INT) {
                if (un->operand && un->operand->node_type == JS_AST_NODE_LITERAL) {
                    JsLiteralNode* lit = (JsLiteralNode*)un->operand;
                    if (lit->literal_type == JS_LITERAL_NUMBER && lit->value.number_value == 0.0)
                        return LMD_TYPE_FLOAT;
                    return LMD_TYPE_INT;
                }
                return LMD_TYPE_ANY;
            }
            return LMD_TYPE_ANY;
        }
        case JS_OP_INCREMENT: case JS_OP_DECREMENT: {
            if (!un->operand) return LMD_TYPE_ANY;
            TypeId t = jm_get_effective_type(mt, un->operand);
            if (t == LMD_TYPE_INT || t == LMD_TYPE_FLOAT) return t;
            return LMD_TYPE_ANY;
        }
        default: return LMD_TYPE_ANY;
        }
    }

    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        if (asgn->op == JS_OP_ASSIGN)
            return jm_get_effective_type(mt, asgn->right);
        // compound assignment: depends on operator and operand types
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        TypeId t1 = jm_get_effective_type(mt, cond->consequent);
        TypeId t2 = jm_get_effective_type(mt, cond->alternate);
        if (t1 == t2) return t1;
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        // v11: comma operator returns type of last expression
        JsSequenceNode* seq = (JsSequenceNode*)node;
        JsAstNode* child = seq->expressions;
        JsAstNode* last = NULL;
        while (child) { last = child; child = child->next; }
        return last ? jm_get_effective_type(mt, last) : LMD_TYPE_ANY;
    }

    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
            if (id->name && id->name->len == 6 && strncmp(id->name->chars, "String", 6) == 0)
                return LMD_TYPE_STRING;
        }
        // Phase 4: If callee resolves to a function with a native version
        // and all arg types match, the call returns the function's return type
        JsFuncCollected* fc = jm_resolve_native_call(mt, call);
        if (fc && jm_call_result_uses_native_register(mt, call, fc)) return fc->return_type;
        // Phase 3.5: return type from any collected function (not just native-eligible)
        // Skip generators — they return iterator objects, not the inferred return type
        {
            JsFuncCollected* any_fc = jm_find_collected_func_for_call(mt, call);
            if (any_fc && any_fc->return_type != LMD_TYPE_ANY
                && jm_call_result_uses_native_register(mt, call, any_fc)
                && any_fc->node && !any_fc->node->is_generator && !any_fc->node->is_async)
                return any_fc->return_type;
        }
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* mem = (JsMemberNode*)node;
        // .length returns INT only for known arrays, strings, and functions
        if (!mem->computed && mem->property &&
            mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
            if (prop->name && prop->name->len == 6 && strncmp(prop->name->chars, "length", 6) == 0) {
                // Only infer INT for known types where .length is guaranteed numeric
                TypeId obj_t = jm_get_effective_type(mt, mem->object);
                if (obj_t == LMD_TYPE_ARRAY || obj_t == LMD_TYPE_STRING)
                    return LMD_TYPE_INT;
                // For functions, .length is param_count (INT)
                if (obj_t == LMD_TYPE_FUNC)
                    return LMD_TYPE_INT;
                // Unknown object type — .length can be anything (e.g., {length: {toString: fn}})
                return LMD_TYPE_ANY;
            }

            // P3.4: TypeMap shape lookup — if the object has a full_type (TypeMap from TS interface),
            // look up the property name in the ShapeEntry chain to find the field type.
            Type* obj_full = jm_get_full_type(mt, mem->object);
            if (obj_full && obj_full->type_id == LMD_TYPE_MAP) {
                TypeMap* tm = (TypeMap*)obj_full;
                for (ShapeEntry* se = tm->shape; se; se = se->next) {
                    if (se->name && se->name->str && se->name->length == prop->name->len &&
                        memcmp(se->name->str, prop->name->chars, prop->name->len) == 0) {
                        if (se->type) return se->type->type_id;
                    }
                }
            }

        }
        return LMD_TYPE_ANY;
    }

    default:
        return LMD_TYPE_ANY;
    }
}

// Returns the full Type* for an expression (richer than just TypeId).
// Checks variable scope for Type* carried from TS annotations.
// Returns NULL for unknown or non-compound types.
Type* jm_get_full_type(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return NULL;
    if (node->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        const char* vname = jm_var_name(id->name);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var) return var->full_type;
    }
    return NULL;
}

// Check if a type is native (not boxed)
bool jm_is_native_type(TypeId tid) {
    return tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL;
}

static int jm_find_var_scope_depth_for_name(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return -1;
    for (int depth = mt->scope_depth; depth >= 0; depth--) {
        struct hashmap* scope = jm_var_scope_at(mt, depth);
        if (!scope) continue;
        JsVarScopeEntry key;
        memset(&key, 0, sizeof(key));
        key.name = name;
        JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(scope, &key);
        if (found) return depth;
    }
    return -1;
}

static bool jm_has_outer_binding_before_depth(JsMirTranspiler* mt, const char* name, int inner_depth) {
    if (!mt || !name || inner_depth <= 0) return false;
    for (int depth = inner_depth - 1; depth >= 0; depth--) {
        struct hashmap* scope = jm_var_scope_at(mt, depth);
        if (!scope) continue;
        JsVarScopeEntry key;
        memset(&key, 0, sizeof(key));
        key.name = name;
        if (hashmap_get(scope, &key)) return true;
    }
    return false;
}

static JsFuncCollected* jm_current_scope_env_func(JsMirTranspiler* mt) {
    if (!mt) return NULL;
    int fi = mt->current_func_index;
    if (fi < 0) {
        if (!mt->module_scope_env_active) return NULL;
        return &mt->module_fc;
    }
    if (fi >= mt->func_count) return NULL;
    return &mt->func_entries[fi];
}

// Helper: if a variable is in the current function's scope env, mark it and write-back.
// Called after jm_set_var or assignment to propagate value to shared scope env.
void jm_scope_env_mark_and_writeback(JsMirTranspiler* mt, const char* name, MIR_reg_t val_reg, TypeId type_id) {
    if (mt->scope_env_reg == 0) return;
    JsMirVarEntry* active_var = jm_find_var(mt, name);
    if (active_var && active_var->in_scope_env && active_var->scope_env_reg == mt->scope_env_reg &&
        active_var->scope_env_slot >= 0) {
        MIR_reg_t val = val_reg;
        if (jm_is_native_type(type_id))
            val = jm_box_native(mt, val_reg, type_id);
        jm_emit_store_i64(mt, active_var->scope_env_slot * (int)sizeof(uint64_t), active_var->scope_env_reg, val);
        return;
    }
    // Check if this var name is in the current function's scope env.
    // Js57 Track A: in module body (current_func_index == -1), use the synthetic
    // module_fc set up at js_main entry so top-level closures share lexical state.
    JsFuncCollected* fc = jm_current_scope_env_func(mt);
    if (!fc) return;
    if (!fc->has_scope_env) return;
    for (int s = 0; s < fc->scope_env_count; s++) {
        if (strcmp(name, fc->scope_env_names[s]) == 0) {
            JsMirVarEntry* var = jm_find_var(mt, name);
            int bind_depth = jm_find_var_scope_depth_for_name(mt, name);
            if (var && var->is_let_const && bind_depth > 1 &&
                jm_has_outer_binding_before_depth(mt, name, bind_depth)) {
                return;
            }
            // Determine the actual slot: when reusing parent env, use the
            // remapped slot (from the var's env_slot), not the local index.
            int slot = s;
            if (fc->reuse_parent_env) {
                JsMirVarEntry* var = jm_find_var(mt, name);
                if (var && var->in_scope_env) {
                    // Use preserved scope_env_slot (set during scope env setup)
                    slot = var->scope_env_slot;
                } else if (var && var->from_env) {
                    slot = var->env_slot;
                } else {
                    // Fallback: look up the correct slot from captures
                    for (int c = 0; c < fc->capture_count; c++) {
                        if (strcmp(name, fc->captures[c].name) == 0) {
                            int cap_slot = fc->captures[c].scope_env_slot;
                            if (cap_slot >= 0) slot = cap_slot;
                            break;
                        }
                    }
                }
            }
            // Mark the variable entry
            if (var) {
                var->in_scope_env = true;
                var->scope_env_slot = slot;
                var->scope_env_reg = mt->scope_env_reg;
            }
            // Write current value to scope env
            MIR_reg_t val = val_reg;
            if (jm_is_native_type(type_id))
                val = jm_box_native(mt, val_reg, type_id);
            jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), mt->scope_env_reg, val);

            return;
        }
    }
}

void jm_scope_env_mark_and_writeback_binding(JsMirTranspiler* mt, const char* name,
        JsAstNode* binding_node, MIR_reg_t val_reg, TypeId type_id) {
    if (!mt || mt->scope_env_reg == 0) return;
    JsMirVarEntry* active_var = jm_find_var(mt, name);
    if (active_var && active_var->in_scope_env && active_var->scope_env_reg == mt->scope_env_reg &&
        active_var->scope_env_slot >= 0) {
        jm_scope_env_mark_and_writeback(mt, name, val_reg, type_id);
        return;
    }
    JsFuncCollected* fc = jm_current_scope_env_func(mt);
    if (!fc || !fc->has_scope_env) return;
    for (int s = 0; s < fc->scope_env_count; s++) {
        if (!jm_scope_env_name_matches_binding(fc->scope_env_names[s], name, binding_node)) continue;
        JsMirVarEntry* var = jm_find_var(mt, name);
        if (var) {
            var->in_scope_env = true;
            var->scope_env_slot = s;
            var->scope_env_reg = mt->scope_env_reg;
        }
        MIR_reg_t val = val_reg;
        if (jm_is_native_type(type_id))
            val = jm_box_native(mt, val_reg, type_id);
        jm_emit_store_i64(mt, s * (int)sizeof(uint64_t), mt->scope_env_reg, val);
        return;
    }
    jm_scope_env_mark_and_writeback(mt, name, val_reg, type_id);
}

// v23: truthiness check with inline fast-path for known boolean Items.
// For known-BOOL expressions (comparisons, !expr), extracts the low bit directly
// instead of calling js_is_truthy (saves a function call).
MIR_reg_t jm_emit_is_truthy(JsMirTranspiler* mt, MIR_reg_t val, JsAstNode* expr) {
    TypeId expr_type = expr ? jm_get_effective_type(mt, expr) : LMD_TYPE_ANY;
    if (expr_type == LMD_TYPE_BOOL) {
        MIR_reg_t result = jm_new_reg(mt, "trthy", MIR_T_I64);
        jm_emit_reg_binary_op(mt, MIR_AND, result, val, MIR_new_int_op(mt->ctx, 1));
        return result;
    }
    if (expr_type == LMD_TYPE_FLOAT && MIR_reg_type(mt->ctx, val, mt->em.func) == MIR_T_D) {
        MIR_reg_t nonzero = jm_new_reg(mt, "dtruth_nz", MIR_T_I64);
        jm_emit_reg_binary_op(mt, MIR_DNE, nonzero, val, MIR_new_double_op(mt->ctx, 0.0));
        MIR_reg_t notnan = jm_new_reg(mt, "dtruth_nn", MIR_T_I64);
        jm_emit_reg_binary(mt, MIR_DEQ, notnan, val, val);
        MIR_reg_t result = jm_new_reg(mt, "dtruth", MIR_T_I64);
        // JS Number truthiness treats +0, -0, and NaN as false; all other doubles are true.
        jm_emit_reg_binary(mt, MIR_AND, result, nonzero, notnan);
        return result;
    }
    return jm_emit_uext8(mt, jm_callr_1(mt, "js_is_truthy", MIR_T_I64, val));
}

// v23b: transpile an expression for use as a branch condition (if/while/for/ternary).
// Returns raw int64 0/1 directly usable in MIR_BF/BT.
// For untyped binary comparisons, calls _raw facades to avoid box→unbox cycle.
// For everything else, falls back to jm_transpile_box_item + jm_emit_is_truthy.
MIR_reg_t jm_transpile_condition(JsMirTranspiler* mt, JsAstNode* expr);

// Forward declarations for native expression transpilation
MIR_reg_t jm_transpile_expression(JsMirTranspiler* mt, JsAstNode* expr);
MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item);

// Transpile an expression returning a native register of the target type.
// Handles literals inline (no boxing), identifiers from typed vars, and
// recursive expressions. Falls back to unbox from boxed Item when needed.
MIR_reg_t jm_transpile_as_native(JsMirTranspiler* mt, JsAstNode* expr,
                                         TypeId expr_type, TypeId target_type) {
    if (target_type == LMD_TYPE_ANY) {
        // A mixed native entry carries this formal as an Item. Reusing the
        // numeric fallback below would coerce an arbitrary JS value merely to
        // satisfy an ABI register type, defeating the boxed slow semantics.
        return jm_transpile_box_item(mt, expr);
    }

    // Literals: emit native constant directly (bypass boxing)
    if (expr && expr->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            if (target_type == LMD_TYPE_FLOAT) {
                MIR_reg_t r = jm_new_reg(mt, "dlit", MIR_T_D);
                jm_emit_reg_op(mt, MIR_DMOV, r, MIR_new_double_op(mt->ctx, lit->value.number_value));
                return r;
            } else {
                MIR_reg_t r = jm_new_reg(mt, "ilit", MIR_T_I64);
                jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, (int64_t)lit->value.number_value));
                return r;
            }
        }
        if (lit->literal_type == JS_LITERAL_BOOLEAN) {
            // Native conditional joins use numeric lanes for inferred boolean
            // returns; unboxing the boxed Item here turned both arms into 0.
            if (target_type == LMD_TYPE_FLOAT) {
                MIR_reg_t r = jm_new_reg(mt, "dbool", MIR_T_D);
                jm_emit_reg_op(mt, MIR_DMOV, r, MIR_new_double_op(mt->ctx, lit->value.boolean_value ? 1.0 : 0.0));
                return r;
            }
            MIR_reg_t r = jm_new_reg(mt, "ibool", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, lit->value.boolean_value ? 1 : 0));
            return r;
        }
    }

    // Identifiers: use native register directly if variable is typed
    if (expr && expr->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)expr;
        const char* vname = jm_var_name(id->name);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var && jm_is_native_type(var->type_id)) {
            if (var->tdz_active) {
                MIR_reg_t boxed = jm_box_native(mt, var->reg, var->type_id);
                jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx,
                        jm_module_name_id(mt, id->name->chars, id->name->len)),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int)id->name->len));
                jm_emit_error_lane_propagate_check(mt);
            }
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, var->reg, var->type_id);
            else
                return jm_ensure_native_int(mt, var->reg, var->type_id);
        }
        // boxed variable: unbox
        MIR_reg_t boxed;
        if (var) {
            boxed = var->reg;
        } else if (mt->module_consts) {
            // check module-level variables (e.g. top-level let/var accessed from for-loop update)
            JsModuleConstEntry* mc = jm_find_module_const(mt, vname);
            if (mc && mc->const_type == MCONST_MODVAR) {
                boxed = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
                if (mc->var_kind == JS_VAR_LET || mc->var_kind == JS_VAR_CONST) {
                    jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx,
                            jm_module_name_id(mt, id->name->chars, id->name->len)),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int)id->name->len));
                    jm_emit_error_lane_propagate_check(mt);
                }
            } else if (mc && mc->const_type == MCONST_INT) {
                // constant int: emit directly as native
                MIR_reg_t r = jm_new_reg(mt, "mcint", MIR_T_I64);
                jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, mc->int_val));
                if (target_type == LMD_TYPE_FLOAT)
                    return jm_ensure_native_float(mt, r, LMD_TYPE_INT);
                return r;
            } else if (mc && mc->const_type == MCONST_FLOAT) {
                MIR_reg_t r = jm_new_reg(mt, "mcflt", MIR_T_D);
                jm_emit_reg_op(mt, MIR_DMOV, r, MIR_new_double_op(mt->ctx, mc->float_val));
                if (target_type == LMD_TYPE_INT)
                    return jm_ensure_native_int(mt, r, LMD_TYPE_FLOAT);
                return r;
            } else {
                boxed = jm_emit_null(mt);
            }
        } else {
            boxed = jm_emit_null(mt);
        }
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, boxed);
        else {
            // Use it2d + D2I for robust int extraction (handles INT, FLOAT, any numeric)
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, boxed);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    // Other expressions: determine if jm_transpile_expression returns native.
    // Must check specifically whether the native path is actually taken.
    if (expr && expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        // Determine if the native path is actually taken
        bool native_binary = jm_is_native_binary_expression(mt, bin);
        // Comparisons return native 0/1 only when BOTH sides are typed numeric.
        // With one untyped side, the comparison falls through to boxed runtime
        // and returns a boxed boolean Item, not a native value.
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_binary) {
            // Native path was taken: result is native int or double
            return jm_normalize_numeric_result(mt, result, target_type, expr_type, true);
        }
        // Boxed path was taken: result is boxed Item, need to unbox
        return jm_normalize_numeric_result(mt, result, target_type, expr_type, false);
    }

    if (expr && expr->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)expr;
        // Check if unary op takes the native path
        bool native_unary = jm_is_native_unary_expression(mt, un);
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_unary) {
            return jm_normalize_numeric_result(mt, result, target_type, expr_type, true);
        }
        // Boxed result: unbox
        return jm_normalize_numeric_result(mt, result, target_type, expr_type, false);
    }

    if (expr && expr->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
        JsAssignmentNode* asgn = (JsAssignmentNode*)expr;
        bool native_assign = false;
        TypeId assign_var_type = LMD_TYPE_ANY;
        if (asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* aid = (JsIdentifierNode*)asgn->left;
            const char* avname = jm_var_name(aid->name);
            JsMirVarEntry* avar = jm_find_var(mt, avname);
            native_assign = avar && !avar->from_env &&
                            (avar->type_id == LMD_TYPE_INT || avar->type_id == LMD_TYPE_FLOAT);
            if (native_assign) assign_var_type = avar->type_id;
        }
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_assign) {
            // use the variable's actual type (not expr_type from get_effective_type)
            // because P9 widening may have changed the var to FLOAT while
            // get_effective_type still reports the RHS type (e.g. INT for `J = 0`)
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, assign_var_type);
            else
                return jm_ensure_native_int(mt, result, assign_var_type);
        }
        // Boxed result: unbox
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, result);
        else {
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    if (expr && expr->node_type == JS_AST_NODE_CONDITIONAL_EXPRESSION) {
        // Ternary lowering normally joins boxed Item arms; native returns need
        // each arm lowered to the target MIR mode before the branch join.
        return jm_transpile_conditional_as_native(mt, (JsConditionalNode*)expr, target_type);
    }

    // Phase 4: Call expressions — if native call, result is already native
    if (expr && expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)expr;

        JsFuncCollected* fc = jm_resolve_native_call(mt, call);
        if (fc && jm_call_result_uses_native_register(mt, call, fc)) {
            // jm_transpile_expression → jm_transpile_call → native call → native result
            MIR_reg_t result = jm_transpile_expression(mt, expr);
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, fc->return_type);
            else
                return jm_ensure_native_int(mt, result, fc->return_type);
        }
        // Non-native call: result is boxed → unbox
        MIR_reg_t boxed = jm_transpile_expression(mt, expr);
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, boxed);
        else {
            // Use it2d + D2I for robust int extraction (handles INT, FLOAT, any numeric)
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, boxed);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    // All other expressions: get boxed value and unbox to target type
    MIR_reg_t boxed = jm_transpile_box_item(mt, expr);
    if (target_type == LMD_TYPE_FLOAT)
        return jm_emit_unbox_float(mt, boxed);
    else {
        // Use it2d + D2I for robust int extraction (handles INT, FLOAT, any numeric)
        MIR_reg_t as_dbl = jm_emit_unbox_float(mt, boxed);
        return jm_emit_double_to_int(mt, as_dbl);
    }
}
