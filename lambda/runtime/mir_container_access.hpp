#pragma once
#include "mir_emitter_shared.hpp"

// Physical ArrayNum guards shared by both Lambda MIR lowerings. The caller
// already owns an admitted container; views and alternate lanes use its fallback.
static inline void em_array_num_elem_guard(MirEmitter* em, MIR_reg_t arr_ptr,
        uint8_t expected_elem, MIR_label_t l_bail) {
    MIR_reg_t tid_byte = em_new_reg(em, "gtid", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, tid_byte),
        MIR_new_mem_op(em->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));
    MIR_reg_t tid_ok = em_new_reg(em, "gtidok", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ, MIR_new_reg_op(em->ctx, tid_ok),
        MIR_new_reg_op(em->ctx, tid_byte), MIR_new_int_op(em->ctx, LMD_TYPE_ARRAY_NUM)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF, MIR_new_label_op(em->ctx, l_bail),
        MIR_new_reg_op(em->ctx, tid_ok)));

    MIR_reg_t layout = em_new_reg(em, "glay", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, layout),
        MIR_new_mem_op(em->ctx, MIR_T_U8, 2, arr_ptr, 0, 1)));
    MIR_reg_t shaped = em_new_reg(em, "gshp", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, shaped),
        MIR_new_reg_op(em->ctx, layout), MIR_new_int_op(em->ctx, 0x03)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, l_bail),
        MIR_new_reg_op(em->ctx, shaped)));

    MIR_reg_t elem_byte = em_new_reg(em, "gelm", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, elem_byte),
        MIR_new_mem_op(em->ctx, MIR_T_U8, 3, arr_ptr, 0, 1)));
    MIR_reg_t elem_ok = em_new_reg(em, "gelmok", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ, MIR_new_reg_op(em->ctx, elem_ok),
        MIR_new_reg_op(em->ctx, elem_byte), MIR_new_int_op(em->ctx, expected_elem)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF, MIR_new_label_op(em->ctx, l_bail),
        MIR_new_reg_op(em->ctx, elem_ok)));
}

static inline void em_array_num_bounds_check(MirEmitter* em, MIR_reg_t arr_ptr,
        MIR_reg_t idx_int, MIR_label_t oob_label, bool index_nonnegative = false) {
    // indices are 64-bit: MIR's S suffix truncates to 32 bits, admitting 2^32.
    MIR_reg_t arr_len = em_new_reg(em, "alen", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, arr_len),
        MIR_new_mem_op(em->ctx, MIR_T_I64, LAMBDA_GC_OFF_LIST_LENGTH, arr_ptr, 0, 1)));
    if (!index_nonnegative) {
        MIR_reg_t neg_check = em_new_reg(em, "negc", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_LT, MIR_new_reg_op(em->ctx, neg_check),
            MIR_new_reg_op(em->ctx, idx_int), MIR_new_int_op(em->ctx, 0)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, oob_label),
            MIR_new_reg_op(em->ctx, neg_check)));
    }
    MIR_reg_t ge_check = em_new_reg(em, "gec", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_GE, MIR_new_reg_op(em->ctx, ge_check),
        MIR_new_reg_op(em->ctx, idx_int), MIR_new_reg_op(em->ctx, arr_len)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, oob_label),
        MIR_new_reg_op(em->ctx, ge_check)));
}

// Consume one caller-published precise home before any nested call (S9.1.3).
static inline MIR_reg_t em_take_var_home(MirEmitter* em, MIR_reg_t runtime, int position) {
    MIR_disp_t cell = offsetof(Context, mir_var_homes) + position * sizeof(uint64_t*);
    // production consumes these cells before installing MirFrameState.
    MIR_reg_t home = em_load_at(em, runtime, cell, MIR_T_I64, "var_home");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_mem_op(em->ctx, MIR_T_I64, cell, runtime, 0, 1),
        MIR_new_int_op(em->ctx, 0)));
    return home;
}
static inline void em_publish_var_home(MirEmitter* em, int position, MIR_reg_t home) {
    em_store_at(em, em->frame.runtime,
        offsetof(Context, mir_var_homes) + position * sizeof(uint64_t*), MIR_T_I64, home);
}

// shared physical string reads; the caller admits the pointer, the miss retains Unicode/absence.
static inline MIR_reg_t em_ascii_string_byte_at_pointer(MirEmitter* em,
        MIR_reg_t ptr, MIR_reg_t index) {
    MIR_reg_t address = em_new_reg(em, "char_addr", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD,
        MIR_new_reg_op(em->ctx, address), MIR_new_reg_op(em->ctx, ptr),
        MIR_new_reg_op(em->ctx, index)));
    MIR_reg_t byte = em_new_reg(em, "char_byte", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, byte),
        MIR_new_mem_op(em->ctx, MIR_T_U8, offsetof(String, chars), address, 0, 1)));
    return byte;
}


static inline MIR_reg_t em_ascii_string_byte(MirEmitter* em, MIR_reg_t ptr,
        MIR_reg_t index, MIR_label_t slow) {
    MIR_reg_t flags = em_new_reg(em, "char_flags", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, flags),
        MIR_new_mem_op(em->ctx, MIR_T_U8, offsetof(String, flags), ptr, 0, 1)));
    MIR_reg_t ascii = em_new_reg(em, "char_ascii", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, ascii), MIR_new_reg_op(em->ctx, flags),
        MIR_new_int_op(em->ctx, 1)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF,
        MIR_new_label_op(em->ctx, slow), MIR_new_reg_op(em->ctx, ascii)));
    MIR_reg_t length = em_new_reg(em, "char_len", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, length),
        MIR_new_mem_op(em->ctx, MIR_T_U32, offsetof(String, len), ptr, 0, 1)));
    MIR_reg_t in_range = em_new_reg(em, "char_in_range", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ULT,
        MIR_new_reg_op(em->ctx, in_range), MIR_new_reg_op(em->ctx, index),
        MIR_new_reg_op(em->ctx, length)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF,
        MIR_new_label_op(em->ctx, slow), MIR_new_reg_op(em->ctx, in_range)));
    return em_ascii_string_byte_at_pointer(em, ptr, index);
}
