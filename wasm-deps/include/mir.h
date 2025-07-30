/* Minimal MIR stub for WASM builds */
#ifndef MIR_H_WASM_STUB
#define MIR_H_WASM_STUB

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic MIR types as stubs */
typedef void* MIR_context_t;
typedef void* MIR_item_t;
typedef void* MIR_func_t;
typedef void* MIR_insn_t;
typedef void* MIR_op_t;
typedef int MIR_reg_t;

typedef enum {
    MIR_T_I8, MIR_T_U8, MIR_T_I16, MIR_T_U16, 
    MIR_T_I32, MIR_T_U32, MIR_T_I64, MIR_T_U64,
    MIR_T_F, MIR_T_D, MIR_T_LD, MIR_T_P,
    MIR_T_BOUND
} MIR_type_t;

typedef enum {
    MIR_MOV, MIR_FMOV, MIR_DMOV, MIR_LDMOV,
    MIR_ADD, MIR_SUB, MIR_MUL, MIR_DIV,
    MIR_MOD, MIR_AND, MIR_OR, MIR_XOR,
    MIR_LSH, MIR_RSH, MIR_URSH,
    MIR_NEG, MIR_NOT,
    MIR_EQ, MIR_NE, MIR_LT, MIR_LE, MIR_GT, MIR_GE,
    MIR_ULT, MIR_ULE, MIR_UGT, MIR_UGE,
    MIR_FEQ, MIR_FNE, MIR_FLT, MIR_FLE, MIR_FGT, MIR_FGE,
    MIR_DEQ, MIR_DNE, MIR_DLT, MIR_DLE, MIR_DGT, MIR_DGE,
    MIR_LDEQ, MIR_LDNE, MIR_LDLT, MIR_LDLE, MIR_LDGT, MIR_LDGE,
    MIR_JMP, MIR_BT, MIR_BF,
    MIR_BEQ, MIR_BNE, MIR_BLT, MIR_BLE, MIR_BGT, MIR_BGE,
    MIR_UBLT, MIR_UBLE, MIR_UBGT, MIR_UBGE,
    MIR_FBLT, MIR_FBLE, MIR_FBGT, MIR_FBGE,
    MIR_DBLT, MIR_DBLE, MIR_DBGT, MIR_DBGE,
    MIR_LDBLT, MIR_LDBLE, MIR_LDBGT, MIR_LDBGE,
    MIR_CALL, MIR_INLINE, MIR_SWITCH,
    MIR_RET,
    MIR_ALLOCA, MIR_BSTART, MIR_BEND, MIR_VA_ARG, MIR_VA_BLOCK_ARG,
    MIR_LABEL,
    MIR_INVALID_INSN
} MIR_insn_code_t;

/* Stub function declarations */
static inline MIR_context_t MIR_init(void) { return NULL; }
static inline void MIR_finish(MIR_context_t ctx) { (void)ctx; }
static inline void *MIR_gen(MIR_context_t ctx, MIR_func_t *func) { (void)ctx; (void)func; return NULL; }

static inline MIR_item_t MIR_new_func(MIR_context_t ctx, const char *name, size_t nargs, MIR_type_t *arg_types, size_t nlocals, ...) {
    (void)ctx; (void)name; (void)nargs; (void)arg_types; (void)nlocals; return NULL;
}

static inline void MIR_finish_func(MIR_context_t ctx) { (void)ctx; }

static inline MIR_reg_t MIR_new_func_reg(MIR_context_t ctx, MIR_func_t func, MIR_type_t type, const char *name) {
    (void)ctx; (void)func; (void)type; (void)name; return 0;
}

static inline MIR_insn_t MIR_new_insn(MIR_context_t ctx, MIR_insn_code_t code, ...) {
    (void)ctx; (void)code; return NULL;
}

static inline void MIR_append_insn(MIR_context_t ctx, MIR_item_t func_item, MIR_insn_t insn) {
    (void)ctx; (void)func_item; (void)insn;
}

static inline MIR_op_t MIR_new_reg_op(MIR_context_t ctx, MIR_reg_t reg) {
    (void)ctx; (void)reg; return NULL;
}

static inline MIR_op_t MIR_new_int_op(MIR_context_t ctx, int64_t v) {
    (void)ctx; (void)v; return NULL;
}

static inline MIR_op_t MIR_new_float_op(MIR_context_t ctx, float v) {
    (void)ctx; (void)v; return NULL;
}

static inline MIR_op_t MIR_new_double_op(MIR_context_t ctx, double v) {
    (void)ctx; (void)v; return NULL;
}

static inline void MIR_load_module(MIR_context_t ctx, void *addr) {
    (void)ctx; (void)addr;
}

static inline void MIR_link(MIR_context_t ctx, void (*set_interface)(MIR_context_t)) {
    (void)ctx; (void)set_interface;
}

#ifdef __cplusplus
}
#endif

#endif /* MIR_H_WASM_STUB */
