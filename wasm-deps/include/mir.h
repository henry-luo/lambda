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
typedef void* MIR_func_t;
typedef void* MIR_insn_t;
typedef void* MIR_op_t;
typedef int MIR_reg_t;
typedef void* MIR_module_t;

typedef enum {
    MIR_T_I8, MIR_T_U8, MIR_T_I16, MIR_T_U16, 
    MIR_T_I32, MIR_T_U32, MIR_T_I64, MIR_T_U64,
    MIR_T_F, MIR_T_D, MIR_T_LD, MIR_T_P,
    MIR_T_BOUND
} MIR_type_t;

typedef enum {
    MIR_func_item,
    MIR_proto_item,
    MIR_import_item,
    MIR_export_item,
    MIR_forward_item,
    MIR_data_item,
    MIR_ref_data_item,
    MIR_expr_data_item,
    MIR_bss_item,
    MIR_ITEM_BOUND
} MIR_item_type_t;

typedef enum {
    MIR_MOV, MIR_FMOV, MIR_DMOV, MIR_LDMOV,
    MIR_ADD, MIR_SUB, MIR_MUL, MIR_DIV,
    MIR_MOD, MIR_AND, MIR_OR, MIR_XOR,
    MIR_LSH, MIR_RSH, MIR_URSH,
    MIR_FADD, MIR_FSUB, MIR_FMUL, MIR_FDIV,
    MIR_DADD, MIR_DSUB, MIR_DMUL, MIR_DDIV,
    MIR_FNEG, MIR_DNEG,
    MIR_NEG, MIR_NOT,
    MIR_JMP, MIR_BT, MIR_BF,
    MIR_RET, MIR_CALL,
    MIR_EQ, MIR_NE, MIR_LT, MIR_LE, MIR_GT, MIR_GE,
    MIR_ULT, MIR_ULE, MIR_UGT, MIR_UGE,
    MIR_FEQ, MIR_FNE, MIR_FLT, MIR_FLE, MIR_FGT, MIR_FGE,
    MIR_DEQ, MIR_DNE, MIR_DLT, MIR_DLE, MIR_DGT, MIR_DGE,
    MIR_LDEQ, MIR_LDNE, MIR_LDLT, MIR_LDLE, MIR_LDGT, MIR_LDGE,
    MIR_BEQ, MIR_BNE, MIR_BLT, MIR_BLE, MIR_BGT, MIR_BGE,
    MIR_UBLT, MIR_UBLE, MIR_UBGT, MIR_UBGE,
    MIR_FBLT, MIR_FBLE, MIR_FBGT, MIR_FBGE,
    MIR_DBLT, MIR_DBLE, MIR_DBGT, MIR_DBGE,
    MIR_LDBLT, MIR_LDBLE, MIR_LDBGT, MIR_LDBGE,
    MIR_SWITCH, MIR_LABEL,
    MIR_ALLOCA, MIR_BSTART, MIR_BEND, MIR_VA_ARG, MIR_VA_BLOCK_ARG,
    MIR_INLINE, MIR_INVALID_INSN,
    MIR_INSN_BOUND
} MIR_insn_code_t;

/* MIR_item_t needs to have an addr field for runner.c */
typedef struct mir_item_struct_t {
    void* addr;
    MIR_item_type_t item_type;
    union {
        struct { const char *name; void *call_addr; } *func;
        struct { const char *name; } *proto;
        const char *import_id;
        const char *export_id;
        const char *forward_id;
    } u;
} mir_item_struct_t;
typedef mir_item_struct_t* MIR_item_t;

typedef struct {
    MIR_type_t type;
    const char *name;
} MIR_var_t;

/* Stub function declarations */
static inline MIR_context_t MIR_init(void) { return NULL; }
static inline void MIR_finish(MIR_context_t ctx) { (void)ctx; }
static inline void *MIR_gen(MIR_context_t ctx, MIR_func_t *func) { (void)ctx; (void)func; return NULL; }

static inline MIR_item_t MIR_new_func(MIR_context_t ctx, const char *name, size_t nargs, MIR_type_t *arg_types, size_t nlocals, ...) {
    (void)ctx; (void)name; (void)nargs; (void)arg_types; (void)nlocals; 
    static mir_item_struct_t stub_item = {0};
    return &stub_item;
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

static inline MIR_item_t find_import(MIR_context_t ctx, const char *mod_name) {
    (void)ctx; (void)mod_name;
    static mir_item_struct_t stub_item = {0};
    return &stub_item;
}

static inline void* find_func(MIR_context_t ctx, const char *func_name) {
    (void)ctx; (void)func_name;
    return NULL;
}

static inline void* find_data(MIR_context_t ctx, const char *data_name) {
    (void)ctx; (void)data_name;
    return NULL;
}

static inline MIR_module_t MIR_new_module(MIR_context_t ctx, const char *name) {
    (void)ctx; (void)name;
    return NULL;
}

static inline MIR_item_t MIR_new_func_arr(MIR_context_t ctx, const char *name, size_t nargs, MIR_type_t *ret_type, size_t nlocals, MIR_var_t *vars) {
    (void)ctx; (void)name; (void)nargs; (void)ret_type; (void)nlocals; (void)vars;
    static mir_item_struct_t stub_item = {0};
    return &stub_item;
}

static inline MIR_func_t MIR_get_item_func(MIR_context_t ctx, MIR_item_t item) {
    (void)ctx; (void)item;
    return NULL;
}

static inline MIR_insn_t MIR_new_ret_insn(MIR_context_t ctx, size_t nops, ...) {
    (void)ctx; (void)nops;
    return NULL;
}

static inline void MIR_finish_module(MIR_context_t ctx) {
    (void)ctx;
}

static inline void MIR_gen_set_optimize_level(MIR_context_t ctx, int level) {
    (void)ctx; (void)level;
}

#ifdef __cplusplus
}
#endif

#endif /* MIR_H_WASM_STUB */
