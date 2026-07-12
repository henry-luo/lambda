#pragma once

#include <mir.h>
#include <stdio.h>
#include <string.h>

#define MIR_SHARED_MAX_CALL_ARGS 6

static inline MIR_type_t mir_reg_type_for_alloc(MIR_type_t type,
                                                bool coerce_float32) {
    return (type == MIR_T_P || (coerce_float32 && type == MIR_T_F)) ? MIR_T_I64 : type;
}

static inline MIR_reg_t mir_new_numbered_reg(MIR_context_t ctx, MIR_func_t func,
                                             int* reg_counter,
                                             const char* prefix,
                                             MIR_type_t type,
                                             bool coerce_float32 = false) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", prefix, (*reg_counter)++);
    return MIR_new_func_reg(ctx, func, mir_reg_type_for_alloc(type, coerce_float32), name);
}

static inline MIR_label_t mir_new_emit_label(MIR_context_t ctx) {
    return MIR_new_label(ctx);
}

static inline void mir_append_emit_insn(MIR_context_t ctx,
                                        MIR_item_t func_item,
                                        MIR_insn_t insn) {
    MIR_append_insn(ctx, func_item, insn);
}

static inline void mir_append_emit_label(MIR_context_t ctx,
                                         MIR_item_t func_item,
                                         MIR_label_t label) {
    MIR_append_insn(ctx, func_item, label);
}

static inline void mir_emit_i64_const_to_reg(MIR_context_t ctx,
                                             MIR_item_t func_item,
                                             MIR_reg_t reg,
                                             int64_t value) {
    mir_append_emit_insn(ctx, func_item,
        MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, reg),
            MIR_new_int_op(ctx, value)));
}

static inline void mir_prepare_call_args(MIR_var_t* args,
                                         MIR_type_t* arg_types,
                                         int nargs) {
    static const char* ARG_NAMES[MIR_SHARED_MAX_CALL_ARGS] = {"a", "b", "c", "d", "e", "f"};
    for (int i = 0; i < nargs; i++) {
        args[i].type = arg_types[i];
        args[i].name = ARG_NAMES[i];
        args[i].size = 0;
    }
}

static inline MIR_insn_t mir_new_call_with_args(MIR_context_t ctx,
                                                MIR_item_t proto,
                                                MIR_item_t import,
                                                MIR_reg_t result,
                                                int nargs,
                                                MIR_op_t* arg_ops) {
    MIR_op_t ops[3 + MIR_SHARED_MAX_CALL_ARGS];
    int oi = 0;
    ops[oi++] = MIR_new_ref_op(ctx, proto);
    ops[oi++] = MIR_new_ref_op(ctx, import);
    if (result) {
        ops[oi++] = MIR_new_reg_op(ctx, result);
    }
    for (int i = 0; i < nargs; i++) {
        ops[oi++] = arg_ops[i];
    }
    return MIR_new_insn_arr(ctx, MIR_CALL, oi, ops);
}

static inline void mir_format_import_key(char* out,
                                         size_t out_size,
                                         const char* name,
                                         MIR_type_t ret_type,
                                         int nargs,
                                         MIR_var_t* args,
                                         int nres,
                                         bool include_signature) {
    if (!out || out_size == 0) return;
    if (!include_signature) {
        snprintf(out, out_size, "%s", name);
        return;
    }

    int key_len = snprintf(out, out_size, "%s#r%d#n%d#a%d",
        name, (int)ret_type, nres, nargs);
    for (int i = 0; i < nargs && key_len > 0 && key_len < (int)out_size; i++) {
        key_len += snprintf(out + key_len, out_size - (size_t)key_len,
            "#%d", (int)args[i].type);
    }
}

static inline void mir_format_import_proto_name(char* out,
                                                size_t out_size,
                                                const char* name,
                                                MIR_type_t ret_type,
                                                int nargs,
                                                int nres,
                                                bool include_signature) {
    if (!out || out_size == 0) return;
    if (include_signature) {
        snprintf(out, out_size, "%s_p_r%d_n%d_a%d",
            name, (int)ret_type, nres, nargs);
    } else {
        snprintf(out, out_size, "%s_p", name);
    }
}

static inline void mir_create_import_proto_pair(MIR_context_t ctx,
                                                const char* name,
                                                MIR_type_t ret_type,
                                                int nargs,
                                                MIR_var_t* args,
                                                int nres,
                                                bool include_signature,
                                                MIR_item_t* out_proto,
                                                MIR_item_t* out_import) {
    char proto_name[140];
    mir_format_import_proto_name(proto_name, sizeof(proto_name), name,
        ret_type, nargs, nres, include_signature);

    MIR_type_t res_types[1] = { ret_type };
    if (out_proto) {
        *out_proto = MIR_new_proto_arr(ctx, proto_name, nres, res_types, nargs, args);
    }
    if (out_import) {
        *out_import = MIR_new_import(ctx, name);
    }
}
