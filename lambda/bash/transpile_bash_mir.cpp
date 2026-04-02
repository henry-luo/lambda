/**
 * Bash → MIR Direct Transpiler
 *
 * Translates a Bash AST into MIR instructions for JIT compilation.
 *
 * 4-phase compilation:
 *   1. Parse: source → Tree-sitter CST
 *   2. Build: CST → Bash AST (build_bash_ast.cpp)
 *   3. Emit:  AST → MIR IR (this file)
 *   4. JIT:   MIR → native → execute
 *
 * All runtime calls go through bash_runtime.h (extern "C").
 * Variables are stored as Items in a module variable table (like Python).
 */
#include "bash_transpiler.hpp"
#include "bash_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/strbuf.h"
#include "../../lib/file.h"
#include <tree_sitter/tree-sitter-bash.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// external MIR functions
extern "C" {
    MIR_context_t jit_init(unsigned int optimize_level);
    void* import_resolver(const char *name);
    void* find_func(MIR_context_t ctx, const char *fn_name);
}

// forward declarations for context and heap init
extern __thread EvalContext* context;
extern void* _lambda_rt;

// ============================================================================
// MIR transpiler state
// ============================================================================

typedef struct BashMirVar {
    const char* name;
    int index;           // module variable table index
} BashMirVar;

static uint64_t bash_var_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const BashMirVar* v = (const BashMirVar*)item;
    return hashmap_sip(v->name, strlen(v->name), seed0, seed1);
}

static int bash_var_cmp(const void *a, const void *b, void *udata) {
    const BashMirVar* va = (const BashMirVar*)a;
    const BashMirVar* vb = (const BashMirVar*)b;
    (void)udata;
    return strcmp(va->name, vb->name);
}

// MIR transpiler context
typedef struct BashMirImportEntry {
    char name[128];
    MIR_item_t proto;
    MIR_item_t import;
} BashMirImportEntry;

static uint64_t bm_import_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const BashMirImportEntry* e = (const BashMirImportEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

static int bm_import_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((const BashMirImportEntry*)a)->name, ((const BashMirImportEntry*)b)->name);
}

typedef struct BashMirTranspiler {
    BashTranspiler* tp;
    MIR_context_t ctx;
    MIR_module_t module;

    // current function being built
    MIR_item_t current_func_item;
    MIR_func_t current_func;
    MIR_reg_t result_reg;       // current expression result

    // variable table
    struct hashmap* vars;
    int var_count;

    // function registry
    struct hashmap* functions;
    int func_count;

    // user-defined function table (name → MIR func item)
    struct hashmap* user_funcs;

    // import cache (proto + import items)
    struct hashmap* import_cache;

    // label counters
    int label_counter;

    // loop control labels
    MIR_label_t loop_break_label;
    MIR_label_t loop_continue_label;
    int loop_depth;

    // subshell exit label: if non-NULL, errexit should jump here instead of RET
    MIR_label_t subshell_exit_label;
} BashMirTranspiler;

// user-defined function entry
typedef struct BashMirUserFunc {
    char name[128];
    MIR_item_t func_item;
    BashFunctionDefNode* ast_node;
} BashMirUserFunc;

static uint64_t bm_user_func_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const BashMirUserFunc* f = (const BashMirUserFunc*)item;
    return hashmap_sip(f->name, strlen(f->name), seed0, seed1);
}

static int bm_user_func_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((const BashMirUserFunc*)a)->name, ((const BashMirUserFunc*)b)->name);
}

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_op_t bm_transpile_node(BashMirTranspiler* mt, BashAstNode* node);
static void bm_transpile_statement(BashMirTranspiler* mt, BashAstNode* node);
static MIR_op_t bm_transpile_word(BashMirTranspiler* mt, BashAstNode* node, bool suppress_tilde = false);
static MIR_op_t bm_transpile_cmd_arg(BashMirTranspiler* mt, BashAstNode* node);
static MIR_op_t bm_transpile_command(BashMirTranspiler* mt, BashCommandNode* cmd);
static void bm_transpile_if(BashMirTranspiler* mt, BashIfNode* node);
static void bm_transpile_for(BashMirTranspiler* mt, BashForNode* node);
static void bm_transpile_for_arith(BashMirTranspiler* mt, BashForArithNode* node);
static void bm_transpile_while(BashMirTranspiler* mt, BashWhileNode* node);
static void bm_transpile_case(BashMirTranspiler* mt, BashCaseNode* node);
static void bm_transpile_assignment(BashMirTranspiler* mt, BashAssignmentNode* node);
static bool node_has_command_sub(BashAstNode* node);
static MIR_op_t bm_transpile_arith(BashMirTranspiler* mt, BashAstNode* node);
static void bm_transpile_function_def(BashMirTranspiler* mt, BashFunctionDefNode* node);
static MIR_op_t bm_transpile_expansion(BashMirTranspiler* mt, BashAstNode* node);
static MIR_op_t bm_transpile_varref(BashMirTranspiler* mt, BashVarRefNode* node);
static MIR_op_t bm_emit_string_literal(BashMirTranspiler* mt, const char* text, size_t len);
static MIR_op_t bm_transpile_string(BashMirTranspiler* mt, BashStringNode* node);
static MIR_op_t bm_transpile_concat(BashMirTranspiler* mt, BashConcatNode* node);
static MIR_label_t bm_new_label(BashMirTranspiler* mt);
static MIR_reg_t bm_emit_call_0(BashMirTranspiler* mt, const char* fn_name);
static void bm_emit_call_void_1(BashMirTranspiler* mt, const char* fn_name, MIR_op_t arg1);
static bool bm_statement_needs_debug_trap(BashAstNodeType node_type);
static bool bm_emit_statement_debug_prelude(BashMirTranspiler* mt, BashAstNode* node, MIR_label_t* skip_label);
static bool arg_is_at_splat(BashAstNode* node);

// emit errexit check: call bash_check_errexit(), if true, exit scope
static void bm_emit_errexit_check(BashMirTranspiler* mt) {
    MIR_reg_t should_exit = bm_emit_call_0(mt, "bash_check_errexit");
    MIR_label_t skip = bm_new_label(mt);
    // if should_exit == 0 (false), skip the return
    // NOTE: bash_check_errexit returns plain C int (0 or 1), NOT a tagged Item
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, skip),
                     MIR_new_reg_op(mt->ctx, should_exit),
                     MIR_new_int_op(mt->ctx, 0)));
    if (mt->subshell_exit_label) {
        // inside subshell: jump to subshell exit instead of returning
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_JMP,
                         MIR_new_label_op(mt->ctx, mt->subshell_exit_label)));
    } else {
        // top-level or function: return exit code
        MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, ec)));
    }
    MIR_append_insn(mt->ctx, mt->current_func_item, skip);
}

// ============================================================================
// Helpers
// ============================================================================

// check if a variable was declared as an associative array (compile-time)
static bool bm_is_assoc_var(BashMirTranspiler* mt, const char* name) {
    String key;
    key.len = (uint32_t)strlen(name);
    key.is_ascii = 1;
    // stack-allocate a String-like object for lookup (point to same chars)
    // we need to do a scope lookup — create a temp String
    String* lookup = name_pool_create_len(mt->tp->name_pool, name, key.len);
    NameEntry* entry = bash_scope_lookup(mt->tp, lookup);
    if (entry && entry->node) {
        BashAstNode* ast = (BashAstNode*)entry->node;
        if (ast->node_type == BASH_AST_NODE_ASSIGNMENT) {
            BashAssignmentNode* assign = (BashAssignmentNode*)ast;
            return (assign->declare_flags & BASH_ATTR_ASSOC_ARRAY) != 0;
        }
    }
    return false;
}

static MIR_reg_t bm_new_temp(BashMirTranspiler* mt) {
    char name[32];
    snprintf(name, sizeof(name), "_bt%d", mt->tp->temp_var_counter++);
    return MIR_new_func_reg(mt->ctx, mt->current_func, MIR_T_I64, name);
}

static bool bm_statement_needs_debug_trap(BashAstNodeType node_type) {
    switch (node_type) {
    case BASH_AST_NODE_PROGRAM:
    case BASH_AST_NODE_FUNCTION_DEF:
    case BASH_AST_NODE_PIPELINE:
    case BASH_AST_NODE_LIST:
    case BASH_AST_NODE_BLOCK:
    case BASH_AST_NODE_COMPOUND_STATEMENT:
    case BASH_AST_NODE_REDIRECTED:
    // if/while/until: debug trap fires for their condition command, not the compound itself
    case BASH_AST_NODE_IF:
    case BASH_AST_NODE_WHILE:
    case BASH_AST_NODE_UNTIL:
    // for loops: debug trap is emitted per-iteration inside bm_transpile_for
    case BASH_AST_NODE_FOR:
    case BASH_AST_NODE_FOR_ARITHMETIC:
        return false;
    default:
        return true;
    }
}

static bool bm_emit_statement_debug_prelude(BashMirTranspiler* mt, BashAstNode* node, MIR_label_t* skip_label) {
    if (!node) return false;

    TSPoint start = ts_node_start_point(node->node);
    bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, (int)start.row + 1));

    if (!bm_statement_needs_debug_trap(node->node_type)) return false;

    *skip_label = bm_new_label(mt);
    MIR_reg_t debug_result = bm_emit_call_0(mt, "bash_run_debug_trap");
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_BEQ,
                     MIR_new_label_op(mt->ctx, *skip_label),
                     MIR_new_reg_op(mt->ctx, debug_result),
                     MIR_new_uint_op(mt->ctx, i2it(2))));
    return true;
}

static MIR_label_t bm_new_label(BashMirTranspiler* mt) {
    return MIR_new_label(mt->ctx);
}

// ensure a proto+import pair exists for a given function name, caching for reuse
static BashMirImportEntry* bm_ensure_import(BashMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    BashMirImportEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", fn_name);

    BashMirImportEntry* found = (BashMirImportEntry*)hashmap_get(mt->import_cache, &key);
    if (found) return found;

    char proto_name[140];
    snprintf(proto_name, sizeof(proto_name), "%s_p", fn_name);

    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, nres, res_types, nargs, args);
    MIR_item_t imp = MIR_new_import(mt->ctx, fn_name);

    BashMirImportEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", fn_name);
    new_entry.proto = proto;
    new_entry.import = imp;
    hashmap_set(mt->import_cache, &new_entry);

    return (BashMirImportEntry*)hashmap_get(mt->import_cache, &key);
}

// convenience wrappers
static BashMirImportEntry* bm_ensure_import_v(BashMirTranspiler* mt, const char* fn_name) {
    return bm_ensure_import(mt, fn_name, MIR_T_I64, 0, NULL, 0);
}

static BashMirImportEntry* bm_ensure_import_i(BashMirTranspiler* mt, const char* fn_name) {
    return bm_ensure_import(mt, fn_name, MIR_T_I64, 0, NULL, 1);
}

static BashMirImportEntry* bm_ensure_import_i_i(BashMirTranspiler* mt, const char* fn_name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return bm_ensure_import(mt, fn_name, MIR_T_I64, 1, args, 1);
}

static BashMirImportEntry* bm_ensure_import_ii_i(BashMirTranspiler* mt, const char* fn_name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return bm_ensure_import(mt, fn_name, MIR_T_I64, 2, args, 1);
}

static BashMirImportEntry* bm_ensure_import_ii_v(BashMirTranspiler* mt, const char* fn_name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return bm_ensure_import(mt, fn_name, MIR_T_I64, 2, args, 0);
}

static BashMirImportEntry* bm_ensure_import_iii_i(BashMirTranspiler* mt, const char* fn_name) {
    MIR_var_t args[3] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}, {MIR_T_I64, "c", 0}};
    return bm_ensure_import(mt, fn_name, MIR_T_I64, 3, args, 1);
}

// high-level emit helpers: emit a call and return the result register
static MIR_reg_t bm_emit_call_0(BashMirTranspiler* mt, const char* fn_name) {
    BashMirImportEntry* ie = bm_ensure_import_i(mt, fn_name);
    MIR_reg_t result = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 3,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            MIR_new_reg_op(mt->ctx, result)));
    return result;
}

static MIR_reg_t bm_emit_call_1(BashMirTranspiler* mt, const char* fn_name, MIR_op_t arg1) {
    BashMirImportEntry* ie = bm_ensure_import_i_i(mt, fn_name);
    MIR_reg_t result = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 4,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            MIR_new_reg_op(mt->ctx, result),
            arg1));
    return result;
}

static MIR_reg_t bm_emit_call_2(BashMirTranspiler* mt, const char* fn_name, MIR_op_t arg1, MIR_op_t arg2) {
    BashMirImportEntry* ie = bm_ensure_import_ii_i(mt, fn_name);
    MIR_reg_t result = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 5,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            MIR_new_reg_op(mt->ctx, result),
            arg1,
            arg2));
    return result;
}

static MIR_reg_t bm_emit_call_3(BashMirTranspiler* mt, const char* fn_name,
                                  MIR_op_t arg1, MIR_op_t arg2, MIR_op_t arg3) {
    BashMirImportEntry* ie = bm_ensure_import_iii_i(mt, fn_name);
    MIR_reg_t result = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 6,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            MIR_new_reg_op(mt->ctx, result),
            arg1,
            arg2,
            arg3));
    return result;
}

static void bm_emit_call_void_0(BashMirTranspiler* mt, const char* fn_name) {
    BashMirImportEntry* ie = bm_ensure_import_v(mt, fn_name);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 2,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import)));
}

static void bm_emit_call_void_1(BashMirTranspiler* mt, const char* fn_name, MIR_op_t arg1) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    BashMirImportEntry* ie = bm_ensure_import(mt, fn_name, MIR_T_I64, 1, args, 0);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 3,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            arg1));
}

static void bm_emit_call_void_2(BashMirTranspiler* mt, const char* fn_name, MIR_op_t arg1, MIR_op_t arg2) {
    BashMirImportEntry* ie = bm_ensure_import_ii_v(mt, fn_name);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 4,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            arg1,
            arg2));
}

static void bm_emit_call_void_4(BashMirTranspiler* mt, const char* fn_name,
                                 MIR_op_t arg1, MIR_op_t arg2, MIR_op_t arg3, MIR_op_t arg4) {
    MIR_var_t args[4] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0},
                          {MIR_T_I64, "c", 0}, {MIR_T_I64, "d", 0}};
    BashMirImportEntry* ie = bm_ensure_import(mt, fn_name, MIR_T_I64, 4, args, 0);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 6,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import),
            arg1, arg2, arg3, arg4));
}

// emit a varargs builtin call: func(Item* args, int argc) -> Item
// builds args buffer, fills it, and calls the function

// check if a single arg node needs IFS word splitting (unquoted expansion)
static bool arg_needs_ifs_split(BashAstNode* arg) {
    if (arg->node_type == BASH_AST_NODE_VARIABLE_REF) return true;
    if (arg->node_type == BASH_AST_NODE_EXPANSION) return true;
    if (arg->node_type == BASH_AST_NODE_COMMAND_SUB) return true;
    if (arg->node_type == BASH_AST_NODE_SPECIAL_VARIABLE) {
        BashSpecialVarNode* sv = (BashSpecialVarNode*)arg;
        // positional params $1-$9 need IFS splitting; $@/$* have own handling
        if (sv->special_id >= BASH_SPECIAL_POS_1) return true;
    }
    return false;
}

// check if any command arg is an unquoted expansion that needs IFS word splitting
static bool cmd_has_unquoted_expansion(BashCommandNode* cmd) {
    BashAstNode* arg = cmd->args;
    while (arg) {
        if (arg_needs_ifs_split(arg)) return true;
        arg = arg->next;
    }
    return false;
}

// emit command dispatch via IFS-split array: builds array with bash_ifs_split_into
// for unquoted expansions, then calls bash_exec_cmd_with_array(cmd_name, arr)
static MIR_op_t bm_emit_ifs_split_cmd(BashMirTranspiler* mt, const char* cmd_name,
                                        int cmd_len, BashCommandNode* cmd) {
    // build array FIRST using current IFS (before prefix assignments)
    MIR_reg_t arr_reg = bm_emit_call_0(mt, "bash_array_new");
    MIR_op_t arr_val = MIR_new_reg_op(mt->ctx, arr_reg);
    BashAstNode* p = cmd->args;
    while (p) {
        bool needs_ifs_split = arg_needs_ifs_split(p);
        MIR_op_t val = bm_transpile_cmd_arg(mt, p);
        if (needs_ifs_split) {
            MIR_reg_t new_arr = bm_emit_call_2(mt, "bash_ifs_split_into", arr_val, val);
            arr_val = MIR_new_reg_op(mt->ctx, new_arr);
        } else {
            MIR_reg_t new_arr = bm_emit_call_2(mt, "bash_array_append", arr_val, val);
            arr_val = MIR_new_reg_op(mt->ctx, new_arr);
        }
        p = p->next;
    }

    // THEN handle prefix assignments (e.g., IFS=: echo $x) — save, set, run, restore
    // In bash, prefix assignments take effect for the command execution but NOT for
    // word splitting of the command's own arguments
    int n_prefix = 0;
    MIR_reg_t saved_vals[8];
    String* saved_names[8];
    BashAstNode* pa = cmd->assignments;
    while (pa && n_prefix < 8) {
        BashAssignmentNode* a = (BashAssignmentNode*)pa;
        if (a->name) {
            saved_names[n_prefix] = a->name;
            MIR_op_t name_op = bm_emit_string_literal(mt, a->name->chars, a->name->len);
            saved_vals[n_prefix] = bm_emit_call_1(mt, "bash_get_var", name_op);
            MIR_op_t val_op = a->value ? bm_transpile_node(mt, a->value)
                                       : bm_emit_string_literal(mt, "", 0);
            bm_emit_call_void_2(mt, "bash_set_var", name_op, val_op);
            n_prefix++;
        }
        pa = pa->next;
    }

    MIR_op_t cmd_name_op = bm_emit_string_literal(mt, cmd_name, cmd_len);
    MIR_reg_t result = bm_emit_call_2(mt, "bash_exec_cmd_with_array", cmd_name_op, arr_val);

    // restore prefix assignments
    for (int i = 0; i < n_prefix; i++) {
        MIR_op_t name_op = bm_emit_string_literal(mt, saved_names[i]->chars, saved_names[i]->len);
        bm_emit_call_void_2(mt, "bash_set_var", name_op, MIR_new_reg_op(mt->ctx, saved_vals[i]));
    }

    return MIR_new_reg_op(mt->ctx, result);
}

static MIR_op_t bm_emit_varargs_builtin(BashMirTranspiler* mt, const char* fn_name,
                                          BashCommandNode* cmd) {
    int argc = cmd->arg_count;

    // check if any arg needs $@ splatting
    bool needs_splat = false;
    {
        BashAstNode* a = cmd->args;
        while (a) {
            if (arg_is_at_splat(a)) { needs_splat = true; break; }
            a = a->next;
        }
    }

    MIR_reg_t args_ptr = bm_new_temp(mt);
    MIR_reg_t argc_reg = bm_new_temp(mt);

    if (needs_splat) {
        bm_emit_call_void_0(mt, "bash_arg_builder_start");
        BashAstNode* arg = cmd->args;
        while (arg) {
            if (arg_is_at_splat(arg)) {
                bm_emit_call_void_0(mt, "bash_arg_builder_push_at");
            } else {
                MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                bm_emit_call_void_1(mt, "bash_arg_builder_push", arg_val);
            }
            arg = arg->next;
        }
        MIR_reg_t ptr_r = bm_emit_call_0(mt, "bash_arg_builder_get_ptr");
        MIR_reg_t cnt_r = bm_emit_call_0(mt, "bash_arg_builder_get_count");
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                         MIR_new_reg_op(mt->ctx, ptr_r)));
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                         MIR_new_reg_op(mt->ctx, cnt_r)));
    } else if (argc > 0) {
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                         MIR_new_uint_op(mt->ctx, (uint64_t)(uintptr_t)malloc(argc * sizeof(Item)))));
        int i = 0;
        BashAstNode* arg = cmd->args;
        while (arg && i < argc) {
            MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                   args_ptr, 0, 1),
                    arg_val));
            arg = arg->next;
            i++;
        }
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                         MIR_new_int_op(mt->ctx, argc)));
    } else {
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                         MIR_new_uint_op(mt->ctx, 0)));
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                         MIR_new_int_op(mt->ctx, 0)));
    }

    MIR_reg_t result = bm_emit_call_2(mt, fn_name,
        MIR_new_reg_op(mt->ctx, args_ptr), MIR_new_reg_op(mt->ctx, argc_reg));
    return MIR_new_reg_op(mt->ctx, result);
}

// get or create a variable slot index
static int bm_get_var_index(BashMirTranspiler* mt, const char* name) {
    BashMirVar key = {.name = name, .index = 0};
    const BashMirVar* found = (const BashMirVar*)hashmap_get(mt->vars, &key);
    if (found) return found->index;

    BashMirVar new_var = {.name = name, .index = mt->var_count++};
    hashmap_set(mt->vars, &new_var);
    return new_var.index;
}

// emit a call to bash_set_var(name_item, value_item)
static void bm_emit_set_var(BashMirTranspiler* mt, const char* name, MIR_op_t value) {
    String* name_str = heap_create_name(name);
    MIR_reg_t name_reg = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, name_reg),
                     MIR_new_uint_op(mt->ctx, s2it(name_str))));
    bm_emit_call_void_2(mt, "bash_set_var", MIR_new_reg_op(mt->ctx, name_reg), value);
}

// emit a call to bash_set_local_var(name_item, value_item)
static void bm_emit_set_local_var(BashMirTranspiler* mt, const char* name, MIR_op_t value) {
    String* name_str = heap_create_name(name);
    MIR_reg_t name_reg = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, name_reg),
                     MIR_new_uint_op(mt->ctx, s2it(name_str))));
    // in POSIX mode local is treated as a global assignment
    const char* fn = bash_get_posix_mode() ? "bash_set_var" : "bash_set_local_var";
    bm_emit_call_void_2(mt, fn, MIR_new_reg_op(mt->ctx, name_reg), value);
}

// emit a call to bash_get_var(name_item) → Item
static MIR_op_t bm_emit_get_var(BashMirTranspiler* mt, const char* name) {
    String* name_str = heap_create_name(name);
    MIR_reg_t name_reg = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, name_reg),
                     MIR_new_uint_op(mt->ctx, s2it(name_str))));
    MIR_reg_t result = bm_emit_call_1(mt, "bash_get_var", MIR_new_reg_op(mt->ctx, name_reg));
    return MIR_new_reg_op(mt->ctx, result);
}

static MIR_op_t bm_emit_get_var_n(BashMirTranspiler* mt, const char* name, int len) {
    String* name_str = heap_create_name(name, len);
    MIR_reg_t name_reg = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, name_reg),
                     MIR_new_uint_op(mt->ctx, s2it(name_str))));
    MIR_reg_t result = bm_emit_call_1(mt, "bash_get_var", MIR_new_reg_op(mt->ctx, name_reg));
    return MIR_new_reg_op(mt->ctx, result);
}

// emit a string literal → Item
static MIR_op_t bm_emit_string_literal(BashMirTranspiler* mt, const char* text, size_t len) {
    String* str = heap_create_name(text, len);
    MIR_reg_t reg = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, reg),
                     MIR_new_uint_op(mt->ctx, s2it(str))));
    return MIR_new_reg_op(mt->ctx, reg);
}

// emit an int literal → Item
static MIR_op_t bm_emit_int_literal(BashMirTranspiler* mt, int64_t value) {
    MIR_reg_t reg = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, reg),
                     MIR_new_uint_op(mt->ctx, i2it(value))));
    return MIR_new_reg_op(mt->ctx, reg);
}

// ============================================================================
// Statement transpilation
// ============================================================================

static void bm_transpile_statement(BashMirTranspiler* mt, BashAstNode* node) {
    if (!node) return;

    MIR_label_t debug_skip = NULL;
    bool has_debug_skip = bm_emit_statement_debug_prelude(mt, node, &debug_skip);

    switch (node->node_type) {
    case BASH_AST_NODE_COMMAND: {
        BashCommandNode* cmd = (BashCommandNode*)node;
        if (cmd->redirects) {
            // analyze redirects to determine what wrapping we need
            bool has_stdout_redir = false;
            bool has_stdin_redir = false;
            BashRedirectNode* stdout_redir = NULL;
            BashRedirectNode* stdin_redir = NULL;

            BashAstNode* redir = cmd->redirects;
            while (redir) {
                if (redir->node_type == BASH_AST_NODE_REDIRECT) {
                    BashRedirectNode* r = (BashRedirectNode*)redir;
                    if ((r->fd == 1 || r->fd == -1) &&
                        (r->mode == BASH_REDIR_WRITE || r->mode == BASH_REDIR_APPEND)) {
                        has_stdout_redir = true;
                        stdout_redir = r;
                    } else if (r->fd == 0 && r->mode == BASH_REDIR_READ) {
                        has_stdin_redir = true;
                        stdin_redir = r;
                    } else if (r->fd == 2 &&
                               (r->mode == BASH_REDIR_WRITE || r->mode == BASH_REDIR_APPEND)) {
                        // stderr redirect (2>/dev/null etc.) — no-op for now
                    } else if (r->mode == BASH_REDIR_DUP) {
                        // fd duplication (>&2, 2>&1) — no-op for now
                    }
                }
                redir = redir->next;
            }

            // before command: set up stdin from file
            if (has_stdin_redir && stdin_redir->target) {
                MIR_op_t filename = bm_transpile_node(mt, stdin_redir->target);
                MIR_reg_t content = bm_emit_call_1(mt, "bash_redirect_read", filename);
                bm_emit_call_void_1(mt, "bash_set_stdin_item",
                    MIR_new_reg_op(mt->ctx, content));
            }

            // before command: start capture for stdout redirect
            if (has_stdout_redir) {
                bm_emit_call_void_0(mt, "bash_begin_capture");
            }

            // execute command normally
            bm_transpile_command(mt, cmd);

            // after command: handle stdout redirect
            if (has_stdout_redir && stdout_redir->target) {
                MIR_reg_t captured = bm_emit_call_0(mt, "bash_end_capture_raw");
                MIR_op_t filename = bm_transpile_node(mt, stdout_redir->target);
                const char* func = (stdout_redir->mode == BASH_REDIR_APPEND)
                    ? "bash_redirect_append" : "bash_redirect_write";
                bm_emit_call_2(mt, func, filename, MIR_new_reg_op(mt->ctx, captured));
            }

            // after command: clean up stdin
            if (has_stdin_redir) {
                bm_emit_call_void_0(mt, "bash_clear_stdin_item");
            }
        } else {
            bm_transpile_command(mt, cmd);
        }
        break;
    }
    case BASH_AST_NODE_REDIRECTED: {
        // redirected wrapper: a pipeline/subshell/compound with outer file redirects
        BashRedirectedNode* red = (BashRedirectedNode*)node;
        bool has_stdout_redir = false;
        bool has_stdin_redir = false;
        bool has_herestring_redir = false;
        bool has_heredoc_redir = false;
        BashRedirectNode* stdout_redir = NULL;
        BashRedirectNode* stdin_redir = NULL;
        BashRedirectNode* herestring_redir = NULL;
        BashRedirectNode* heredoc_redir = NULL;

        BashAstNode* redir = red->redirects;
        while (redir) {
            if (redir->node_type == BASH_AST_NODE_REDIRECT) {
                BashRedirectNode* r = (BashRedirectNode*)redir;
                if ((r->fd == 1 || r->fd == -1) &&
                    (r->mode == BASH_REDIR_WRITE || r->mode == BASH_REDIR_APPEND)) {
                    has_stdout_redir = true;
                    stdout_redir = r;
                } else if (r->fd == 0 && r->mode == BASH_REDIR_READ) {
                    has_stdin_redir = true;
                    stdin_redir = r;
                } else if (r->mode == BASH_REDIR_HERESTRING) {
                    has_herestring_redir = true;
                    herestring_redir = r;
                } else if (r->mode == BASH_REDIR_HEREDOC) {
                    has_heredoc_redir = true;
                    heredoc_redir = r;
                }
                // stderr redirect and fd dup (2>&1) are treated as no-ops for now
            }
            redir = redir->next;
        }

        if (has_stdin_redir && stdin_redir->target) {
            MIR_op_t filename = bm_transpile_node(mt, stdin_redir->target);
            MIR_reg_t content = bm_emit_call_1(mt, "bash_redirect_read", filename);
            bm_emit_call_void_1(mt, "bash_set_stdin_item",
                MIR_new_reg_op(mt->ctx, content));
        }

        // herestring redirect: evaluate body and set as stdin
        if (has_herestring_redir && herestring_redir->target) {
            MIR_op_t body_val = bm_transpile_node(mt, herestring_redir->target);
            bm_emit_call_void_1(mt, "bash_set_stdin_item", body_val);
        }

        // heredoc redirect on non-cat command: evaluate body and set as stdin
        if (has_heredoc_redir && heredoc_redir->target) {
            MIR_op_t body_val = bm_transpile_node(mt, heredoc_redir->target);
            bm_emit_call_void_1(mt, "bash_set_stdin_item", body_val);
        }

        if (has_stdout_redir) {
            bm_emit_call_void_0(mt, "bash_begin_capture");
        }

        bm_transpile_statement(mt, red->inner);

        if (has_stdout_redir && stdout_redir->target) {
            // save exit code from inner statement before redirect_write overwrites it
            MIR_reg_t saved_ec = bm_emit_call_0(mt, "bash_save_exit_code");
            MIR_reg_t captured = bm_emit_call_0(mt, "bash_end_capture_raw");
            MIR_op_t filename = bm_transpile_node(mt, stdout_redir->target);
            const char* func = (stdout_redir->mode == BASH_REDIR_APPEND)
                ? "bash_redirect_append" : "bash_redirect_write";
            bm_emit_call_2(mt, func, filename, MIR_new_reg_op(mt->ctx, captured));
            // restore exit code so the redirected statement's exit code is preserved
            bm_emit_call_void_1(mt, "bash_restore_exit_code", MIR_new_reg_op(mt->ctx, saved_ec));
        }

        if (has_stdin_redir || has_herestring_redir || has_heredoc_redir) {
            bm_emit_call_void_0(mt, "bash_clear_stdin_item");
        }
        break;
    }
    case BASH_AST_NODE_ASSIGNMENT: {
        BashAssignmentNode* assign = (BashAssignmentNode*)node;
        bm_transpile_assignment(mt, assign);
        // in bash, standalone assignment sets $? to 0
        // UNLESS the RHS contains a command substitution — then $? is the comsub's exit code
        if (!node_has_command_sub(assign->value)) {
            bm_emit_call_void_1(mt, "bash_set_exit_code", MIR_new_int_op(mt->ctx, 0));
        }
        break;
    }
    case BASH_AST_NODE_IF:
        bm_transpile_if(mt, (BashIfNode*)node);
        break;
    case BASH_AST_NODE_FOR:
        bm_transpile_for(mt, (BashForNode*)node);
        break;
    case BASH_AST_NODE_FOR_ARITHMETIC:
        bm_transpile_for_arith(mt, (BashForArithNode*)node);
        break;
    case BASH_AST_NODE_WHILE:
    case BASH_AST_NODE_UNTIL:
        bm_transpile_while(mt, (BashWhileNode*)node);
        break;
    case BASH_AST_NODE_CASE:
        bm_transpile_case(mt, (BashCaseNode*)node);
        break;
    case BASH_AST_NODE_FUNCTION_DEF:
        // function defs are compiled in pass 1; skip in pass 2
        break;
    case BASH_AST_NODE_PIPELINE: {
        BashPipelineNode* pipeline = (BashPipelineNode*)node;
        BashAstNode* cmd = pipeline->commands;

        if (pipeline->command_count <= 1) {
            // single command, no pipe — just execute directly
            while (cmd) {
                bm_transpile_statement(mt, cmd);
                cmd = cmd->next;
            }
        } else {
            // multi-command pipeline: capture-and-pass chain
            // for each stage except the last: begin_capture, execute, end_capture, set_stdin_item
            // last stage: set_stdin_item from prev, execute normally (output goes to stdout/capture)
            int stage = 0;
            while (cmd) {
                bool is_last = (cmd->next == NULL);

                if (!is_last) {
                    // capture this stage's output (raw — preserve trailing newlines for pipes)
                    bm_emit_call_void_0(mt, "bash_begin_capture");
                    bm_transpile_statement(mt, cmd);
                    MIR_reg_t captured = bm_emit_call_0(mt, "bash_end_capture_raw");
                    // pass captured output as stdin to next stage
                    bm_emit_call_void_1(mt, "bash_set_stdin_item",
                        MIR_new_reg_op(mt->ctx, captured));
                } else {
                    // last stage: just execute (reads stdin_item if needed)
                    bm_transpile_statement(mt, cmd);
                    // clear stdin item after pipeline completes
                    bm_emit_call_void_0(mt, "bash_clear_stdin_item");
                }
                cmd = cmd->next;
                stage++;
            }
        }

        // handle negated pipeline (! pipeline)
        if (pipeline->negated) {
            bm_emit_call_void_0(mt, "bash_negate_exit_code");
        }
        break;
    }
    case BASH_AST_NODE_LIST: {
        BashListNode* list = (BashListNode*)node;

        if (list->op == BASH_LIST_AND) {
            // && : suppress errexit for LEFT side only (it's being tested)
            bm_emit_call_void_0(mt, "bash_errexit_push");
            bm_transpile_statement(mt, list->left);
            bm_emit_call_void_0(mt, "bash_errexit_pop");
            MIR_label_t skip = bm_new_label(mt);
            MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");
            // if exit code != 0, skip right side
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, skip),
                             MIR_new_reg_op(mt->ctx, ec),
                             MIR_new_uint_op(mt->ctx, i2it(0))));
            bm_transpile_statement(mt, list->right);
            // check errexit after right side (not suppressed)
            bm_emit_errexit_check(mt);
            MIR_append_insn(mt->ctx, mt->current_func_item, skip);
        } else if (list->op == BASH_LIST_OR) {
            // || : suppress errexit for LEFT side only
            bm_emit_call_void_0(mt, "bash_errexit_push");
            bm_transpile_statement(mt, list->left);
            bm_emit_call_void_0(mt, "bash_errexit_pop");
            MIR_label_t skip = bm_new_label(mt);
            MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, skip),
                             MIR_new_reg_op(mt->ctx, ec),
                             MIR_new_uint_op(mt->ctx, i2it(0))));
            bm_transpile_statement(mt, list->right);
            bm_emit_errexit_check(mt);
            MIR_append_insn(mt->ctx, mt->current_func_item, skip);
        } else {
            // ; or & : execute sequentially
            bm_transpile_statement(mt, list->left);
            bm_transpile_statement(mt, list->right);
        }
        break;
    }
    case BASH_AST_NODE_BLOCK: {
        BashBlockNode* block = (BashBlockNode*)node;
        BashAstNode* stmt = block->statements;
        while (stmt) {
            bm_transpile_statement(mt, stmt);
            // check set -e after each statement (skip for LIST nodes - they handle it internally)
            if (stmt->node_type != BASH_AST_NODE_LIST) {
                bm_emit_errexit_check(mt);
            }
            stmt = stmt->next;
        }
        break;
    }
    case BASH_AST_NODE_COMPOUND_STATEMENT: {
        BashCompoundNode* compound = (BashCompoundNode*)node;
        bm_transpile_statement(mt, compound->body);
        break;
    }
    case BASH_AST_NODE_SUBSHELL: {
        BashSubshellNode* subshell = (BashSubshellNode*)node;
        // set up subshell exit label for errexit within subshell
        MIR_label_t sub_exit = bm_new_label(mt);
        MIR_label_t old_sub_exit = mt->subshell_exit_label;
        mt->subshell_exit_label = sub_exit;
        // push subshell scope
        bm_emit_call_void_0(mt, "bash_scope_push_subshell");
        bm_transpile_statement(mt, subshell->body);
        // subshell exit landing: pop scope
        MIR_append_insn(mt->ctx, mt->current_func_item, sub_exit);
        bm_emit_call_void_0(mt, "bash_scope_pop_subshell");
        mt->subshell_exit_label = old_sub_exit;
        break;
    }
    case BASH_AST_NODE_RETURN: {
        BashControlNode* ctrl = (BashControlNode*)node;
        MIR_op_t ret_val;
        if (ctrl->value) {
            MIR_op_t val = bm_transpile_node(mt, ctrl->value);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_return_with_code", val);
            ret_val = MIR_new_reg_op(mt->ctx, result);
        } else {
            ret_val = MIR_new_uint_op(mt->ctx, i2it(0));
        }
        // pop scope frame before leaving the function
        bm_emit_call_void_0(mt, "bash_run_return_trap");
        bm_emit_call_void_0(mt, "bash_pop_bash_argv");
        bm_emit_call_void_0(mt, "bash_pop_funcname");
        bm_emit_call_void_0(mt, "bash_scope_pop");
        bm_emit_call_void_0(mt, "bash_pop_positional");
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_ret_insn(mt->ctx, 1, ret_val));
        break;
    }
    case BASH_AST_NODE_EXIT: {
        BashControlNode* ctrl = (BashControlNode*)node;
        if (ctrl->value) {
            MIR_op_t val = bm_transpile_node(mt, ctrl->value);
            bm_emit_call_1(mt, "bash_return_with_code", val);
        }
        // run EXIT trap before terminating
        bm_emit_call_void_0(mt, "bash_trap_run_exit");
        bm_emit_call_void_0(mt, "bash_pop_bash_argv");
        bm_emit_call_void_0(mt, "bash_pop_funcname");
        MIR_reg_t final_ec = bm_emit_call_0(mt, "bash_get_exit_code");
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, final_ec)));
        break;
    }
    case BASH_AST_NODE_BREAK: {
        if (mt->loop_break_label) {
            // break has exit status 0 (POSIX)
            bm_emit_call_void_1(mt, "bash_set_exit_code",
                MIR_new_int_op(mt->ctx, 0));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP,
                             MIR_new_label_op(mt->ctx, mt->loop_break_label)));
        }
        break;
    }
    case BASH_AST_NODE_CONTINUE: {
        if (mt->loop_continue_label) {
            // continue has exit status 0 (POSIX)
            bm_emit_call_void_1(mt, "bash_set_exit_code",
                MIR_new_int_op(mt->ctx, 0));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP,
                             MIR_new_label_op(mt->ctx, mt->loop_continue_label)));
        }
        break;
    }
    case BASH_AST_NODE_ARITHMETIC_EXPR: {
        // (( expr )) as a statement: evaluate and set exit code
        // exit code = 0 if result is non-zero, 1 if result is zero
        
        // set arithmetic expression context for error messages (e.g., division by zero)
        BashArithExprNode* arith_node = (BashArithExprNode*)node;
        if (arith_node->expression && mt->tp->source) {
            TSNode inner = arith_node->expression->node;
            uint32_t sb = ts_node_start_byte(inner);
            uint32_t eb = ts_node_end_byte(inner);
            if (eb > sb && eb <= (uint32_t)mt->tp->source_length) {
                MIR_op_t expr_str = bm_emit_string_literal(mt,
                    mt->tp->source + sb, (int)(eb - sb));
                bm_emit_call_void_1(mt, "bash_set_arith_context", expr_str);
            }
        }
        
        MIR_op_t result = bm_transpile_arith(mt, node);
        MIR_reg_t result_int = bm_emit_call_1(mt, "bash_to_int", result);
        // compare result to 0 (as tagged Items)
        MIR_reg_t ec_val = bm_new_temp(mt);
        MIR_label_t arith_zero = bm_new_label(mt);
        MIR_label_t arith_done = bm_new_label(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, arith_zero),
                         MIR_new_reg_op(mt->ctx, result_int),
                         MIR_new_uint_op(mt->ctx, i2it(0))));
        // non-zero: exit code = 0 (success)
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ec_val),
                         MIR_new_int_op(mt->ctx, 0)));
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, arith_done)));
        // zero: exit code = 1 (failure)
        MIR_append_insn(mt->ctx, mt->current_func_item, arith_zero);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ec_val),
                         MIR_new_int_op(mt->ctx, 1)));
        MIR_append_insn(mt->ctx, mt->current_func_item, arith_done);
        bm_emit_call_void_1(mt, "bash_set_exit_code", MIR_new_reg_op(mt->ctx, ec_val));
        break;
    }
    case BASH_AST_NODE_TEST_COMMAND:
    case BASH_AST_NODE_EXTENDED_TEST: {
        BashTestCommandNode* test = (BashTestCommandNode*)node;
        if (test->expression) {
            bm_transpile_node(mt, test->expression);
        }
        break;
    }
    case BASH_AST_NODE_HEREDOC:
    case BASH_AST_NODE_HERESTRING: {
        BashHeredocNode* heredoc = (BashHeredocNode*)node;
        if (heredoc->body) {
            MIR_op_t body_val = bm_transpile_node(mt, heredoc->body);
            bm_emit_call_void_2(mt, "bash_write_heredoc",
                body_val, MIR_new_int_op(mt->ctx, node->node_type == BASH_AST_NODE_HERESTRING ? 1 : 0));
        }
        break;
    }
    default:
        log_debug("bash-mir: unhandled statement type %d", node->node_type);
        break;
    }

    if (has_debug_skip) {
        MIR_append_insn(mt->ctx, mt->current_func_item, debug_skip);
    }
}

// ============================================================================
// Expression transpilation (returns MIR operand with result Item value)
// ============================================================================

static MIR_op_t bm_transpile_node(BashMirTranspiler* mt, BashAstNode* node) {
    if (!node) {
        MIR_reg_t reg = bm_new_temp(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, reg),
                         MIR_new_uint_op(mt->ctx, ITEM_NULL)));
        return MIR_new_reg_op(mt->ctx, reg);
    }

    switch (node->node_type) {
    case BASH_AST_NODE_WORD:
        return bm_transpile_word(mt, node);
    case BASH_AST_NODE_STRING:
        return bm_transpile_string(mt, (BashStringNode*)node);
    case BASH_AST_NODE_RAW_STRING: {
        BashRawStringNode* raw = (BashRawStringNode*)node;
        if (raw->text) return bm_emit_string_literal(mt, raw->text->chars, raw->text->len);
        return bm_emit_string_literal(mt, "", 0);
    }
    case BASH_AST_NODE_CONCATENATION:
        return bm_transpile_concat(mt, (BashConcatNode*)node);
    case BASH_AST_NODE_VARIABLE_REF:
        return bm_transpile_varref(mt, (BashVarRefNode*)node);
    case BASH_AST_NODE_EXPANSION:
        return bm_transpile_expansion(mt, node);
    case BASH_AST_NODE_SPECIAL_VARIABLE: {
        BashSpecialVarNode* sv = (BashSpecialVarNode*)node;
        if (sv->special_id == BASH_SPECIAL_QUESTION) {
            // $? → bash_get_exit_code()
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_exit_code");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (sv->special_id == BASH_SPECIAL_HASH) {
            // $# → bash_get_arg_count()
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_arg_count");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (sv->special_id == BASH_SPECIAL_AT || sv->special_id == BASH_SPECIAL_STAR) {
            // $@ / $* → bash_get_all_args_string()
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_all_args_string");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (sv->special_id >= BASH_SPECIAL_POS_1 && sv->special_id <= BASH_SPECIAL_POS_9) {
            // $1-$9 → bash_get_positional(n)
            int pos = sv->special_id - BASH_SPECIAL_POS_1 + 1;
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_positional",
                MIR_new_int_op(mt->ctx, pos));
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (sv->special_id == BASH_SPECIAL_ZERO) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_script_name");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (sv->special_id == BASH_SPECIAL_DOLLAR) {
            // $$ — process ID (as string)
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_pid");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (sv->special_id == BASH_SPECIAL_BANG) {
            // $! — last background process ID
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_last_bg_pid");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (sv->special_id == BASH_SPECIAL_DASH) {
            // $- — current shell flags (himBHs)
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_shell_flags");
            return MIR_new_reg_op(mt->ctx, result);
        }
        return bm_emit_string_literal(mt, "", 0);
    }
    case BASH_AST_NODE_ARITHMETIC_EXPR:
        return bm_transpile_arith(mt, node);
    case BASH_AST_NODE_COMMAND_SUB: {
        BashCommandSubNode* cmd_sub = (BashCommandSubNode*)node;
        // begin capture → execute body → end capture (returns captured string)
        // cmd_sub_enter/exit: suppress debug trap inside $() when functrace is off
        bm_emit_call_void_0(mt, "bash_cmd_sub_enter");
        bm_emit_call_void_0(mt, "bash_begin_capture");
        BashAstNode* stmt = cmd_sub->body;
        while (stmt) {
            bm_transpile_statement(mt, stmt);
            stmt = stmt->next;
        }
        MIR_reg_t result = bm_emit_call_0(mt, "bash_end_capture");
        bm_emit_call_void_0(mt, "bash_cmd_sub_exit");
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_COMMAND: {
        MIR_op_t result = bm_transpile_command(mt, (BashCommandNode*)node);
        return result;
    }
    case BASH_AST_NODE_TEST_COMMAND:
    case BASH_AST_NODE_EXTENDED_TEST: {
        BashTestCommandNode* test = (BashTestCommandNode*)node;
        if (test->expression) return bm_transpile_node(mt, test->expression);
        return bm_emit_int_literal(mt, 0);
    }
    case BASH_AST_NODE_TEST_BINARY: {
        BashTestBinaryNode* bin = (BashTestBinaryNode*)node;

        // logical operators need short-circuit evaluation
        if (bin->op == BASH_TEST_AND || bin->op == BASH_TEST_OR) {
            // evaluate left side (sets exit code)
            bm_transpile_node(mt, bin->left);
            MIR_label_t skip = bm_new_label(mt);
            MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");
            if (bin->op == BASH_TEST_AND) {
                // &&: skip right if left failed (exit code != 0)
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, skip),
                                 MIR_new_reg_op(mt->ctx, ec),
                                 MIR_new_uint_op(mt->ctx, i2it(0))));
            } else {
                // ||: skip right if left succeeded (exit code == 0)
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, skip),
                                 MIR_new_reg_op(mt->ctx, ec),
                                 MIR_new_uint_op(mt->ctx, i2it(0))));
            }
            // evaluate right side (sets exit code)
            bm_transpile_node(mt, bin->right);
            MIR_append_insn(mt->ctx, mt->current_func_item, skip);
            // return dummy value; if-statement uses exit code
            return bm_emit_int_literal(mt, 0);
        }

        MIR_op_t left = bm_transpile_node(mt, bin->left);
        MIR_op_t right = bm_transpile_node(mt, bin->right);
        const char* func = NULL;
        switch (bin->op) {
        case BASH_TEST_EQ:     func = "bash_test_eq"; break;
        case BASH_TEST_NE:     func = "bash_test_ne"; break;
        case BASH_TEST_GT:     func = "bash_test_gt"; break;
        case BASH_TEST_GE:     func = "bash_test_ge"; break;
        case BASH_TEST_LT:     func = "bash_test_lt"; break;
        case BASH_TEST_LE:     func = "bash_test_le"; break;
        case BASH_TEST_STR_EQ: func = "bash_test_str_eq"; break;
        case BASH_TEST_STR_NE: func = "bash_test_str_ne"; break;
        case BASH_TEST_STR_LT: func = "bash_test_str_lt"; break;
        case BASH_TEST_STR_GT: func = "bash_test_str_gt"; break;
        case BASH_TEST_STR_MATCH: func = "bash_test_regex"; break;
        case BASH_TEST_STR_GLOB: func = "bash_test_glob"; break;
        default: func = "bash_test_str_eq"; break;
        }
        MIR_reg_t result = bm_emit_call_2(mt, func, left, right);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_TEST_UNARY: {
        BashTestUnaryNode* unary = (BashTestUnaryNode*)node;
        MIR_op_t operand = bm_transpile_node(mt, unary->operand);
        const char* func = NULL;
        switch (unary->op) {
        case BASH_TEST_Z: func = "bash_test_z"; break;
        case BASH_TEST_N: func = "bash_test_n"; break;
        case BASH_TEST_F: func = "bash_test_f"; break;
        case BASH_TEST_D: func = "bash_test_d"; break;
        case BASH_TEST_E: func = "bash_test_e"; break;
        case BASH_TEST_R: func = "bash_test_r"; break;
        case BASH_TEST_W: func = "bash_test_w"; break;
        case BASH_TEST_X: func = "bash_test_x"; break;
        case BASH_TEST_S: func = "bash_test_s"; break;
        case BASH_TEST_L: func = "bash_test_l"; break;
        case BASH_TEST_NOT: {
            // ! — negate the exit code set by the operand expression
            // the operand was already transpiled (setting exit code)
            // now flip it: exit_code = (exit_code == 0) ? 1 : 0
            bm_emit_call_void_0(mt, "bash_negate_exit_code");
            return operand;
        }
        default: func = "bash_test_n"; break;
        }
        MIR_reg_t result = bm_emit_call_1(mt, func, operand);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_ACCESS: {
        BashArrayAccessNode* access = (BashArrayAccessNode*)node;
        if (access->name && access->name->len == 8 && memcmp(access->name->chars, "FUNCNAME", 8) == 0) {
            MIR_op_t idx_val = bm_transpile_node(mt, access->index);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_funcname", idx_val);
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (access->name && access->name->len == 11 && memcmp(access->name->chars, "BASH_SOURCE", 11) == 0) {
            MIR_op_t idx_val = bm_transpile_node(mt, access->index);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_bash_source", idx_val);
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (access->name && access->name->len == 11 && memcmp(access->name->chars, "BASH_LINENO", 11) == 0) {
            MIR_op_t idx_val = bm_transpile_node(mt, access->index);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_bash_lineno", idx_val);
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (access->name && access->name->len == 9 && memcmp(access->name->chars, "BASH_ARGV", 9) == 0) {
            MIR_op_t idx_val = bm_transpile_node(mt, access->index);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_bash_argv", idx_val);
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (access->name && access->name->len == 9 && memcmp(access->name->chars, "BASH_ARGC", 9) == 0) {
            MIR_op_t idx_val = bm_transpile_node(mt, access->index);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_bash_argc", idx_val);
            return MIR_new_reg_op(mt->ctx, result);
        }
        MIR_op_t arr_val = bm_emit_get_var(mt, access->name->chars);
        // array subscripts are arithmetic contexts in bash
        bool is_assoc = bm_is_assoc_var(mt, access->name->chars);
        MIR_op_t idx_val;
        if (is_assoc) {
            idx_val = bm_transpile_node(mt, access->index);
        } else {
            // for indexed arrays, evaluate subscript as arithmetic expression
            MIR_op_t raw_idx = bm_transpile_node(mt, access->index);
            MIR_reg_t arith_idx = bm_emit_call_1(mt, "bash_arith_eval_value", raw_idx);
            idx_val = MIR_new_reg_op(mt->ctx, arith_idx);
        }
        const char* fn = is_assoc ? "bash_assoc_get" : "bash_array_get";
        MIR_reg_t result = bm_emit_call_2(mt, fn, arr_val, idx_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_ALL: {
        BashArrayAllNode* all_node = (BashArrayAllNode*)node;
        if (all_node->name && all_node->name->len == 8 && memcmp(all_node->name->chars, "FUNCNAME", 8) == 0) {
            // ${FUNCNAME[@]} — build from funcname stack
            // For now, return all funcnames as a list
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_funcname_all");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (all_node->name && all_node->name->len == 9 && memcmp(all_node->name->chars, "BASH_ARGV", 9) == 0) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_bash_argv_all");
            return MIR_new_reg_op(mt->ctx, result);
        }
        MIR_op_t arr_val = bm_emit_get_var(mt, all_node->name->chars);
        const char* fn = bm_is_assoc_var(mt, all_node->name->chars)
                         ? "bash_assoc_values" : "bash_array_all";
        MIR_reg_t result = bm_emit_call_1(mt, fn, arr_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_KEYS: {
        BashArrayKeysNode* keys_node = (BashArrayKeysNode*)node;
        MIR_op_t arr_val = bm_emit_get_var(mt, keys_node->name->chars);
        MIR_reg_t result = bm_emit_call_1(mt, "bash_assoc_keys", arr_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_LENGTH: {
        BashArrayLengthNode* len_node = (BashArrayLengthNode*)node;
        if (len_node->name && len_node->name->len == 8 && memcmp(len_node->name->chars, "FUNCNAME", 8) == 0) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_funcname_count");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (len_node->name && len_node->name->len == 11 && memcmp(len_node->name->chars, "BASH_SOURCE", 11) == 0) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_bash_source_count");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (len_node->name && len_node->name->len == 11 && memcmp(len_node->name->chars, "BASH_LINENO", 11) == 0) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_bash_lineno_count");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (len_node->name && len_node->name->len == 9 && memcmp(len_node->name->chars, "BASH_ARGV", 9) == 0) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_bash_argv_count");
            return MIR_new_reg_op(mt->ctx, result);
        }
        if (len_node->name && len_node->name->len == 9 && memcmp(len_node->name->chars, "BASH_ARGC", 9) == 0) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_bash_argc_count");
            return MIR_new_reg_op(mt->ctx, result);
        }
        MIR_op_t arr_val = bm_emit_get_var(mt, len_node->name->chars);
        const char* fn = bm_is_assoc_var(mt, len_node->name->chars)
                         ? "bash_assoc_length" : "bash_array_length";
        MIR_reg_t result = bm_emit_call_1(mt, fn, arr_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_SLICE: {
        BashArraySliceNode* slice = (BashArraySliceNode*)node;
        MIR_op_t arr_val = bm_emit_get_var(mt, slice->name->chars);
        MIR_op_t off_val = bm_transpile_node(mt, slice->offset);
        MIR_op_t len_val = slice->length ? bm_transpile_node(mt, slice->length)
                                         : MIR_new_int_op(mt->ctx, -1);
        MIR_reg_t result = bm_emit_call_3(mt, "bash_array_slice", arr_val, off_val, len_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_LITERAL: {
        BashArrayLiteralNode* arr = (BashArrayLiteralNode*)node;
        MIR_reg_t arr_reg = bm_emit_call_0(mt, "bash_array_new");
        BashAstNode* elem = arr->elements;
        while (elem) {
            MIR_op_t elem_val = bm_transpile_node(mt, elem);
            bm_emit_call_2(mt, "bash_array_append",
                MIR_new_reg_op(mt->ctx, arr_reg), elem_val);
            elem = elem->next;
        }
        return MIR_new_reg_op(mt->ctx, arr_reg);
    }
    default:
        log_debug("bash-mir: unhandled expression type %d", node->node_type);
        return bm_emit_string_literal(mt, "", 0);
    }
}

// helper: check if a word contains unquoted glob characters (*, ?, [)
static bool word_has_glob(const char* s, int len) {
    for (int i = 0; i < len; i++) {
        if (s[i] == '*' || s[i] == '?') return true;
        if (s[i] == '[') {
            // look for closing ]
            for (int j = i + 1; j < len; j++) {
                if (s[j] == ']') return true;
            }
        }
    }
    return false;
}

// helper: check if a word is a brace expansion {a,b,c} or {1..5}
static bool word_is_brace(const char* s, int len) {
    if (len < 3) return false;
    // look for an unquoted, unescaped '{' that has a matching '}' with a comma or '..' in between
    int depth = 0;
    bool found_comma_or_dotdot = false;
    for (int i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len) { i++; continue; } // skip escaped chars
        if (s[i] == '$' && i + 1 < len && s[i + 1] == '{') { // skip ${...}
            int d = 1;
            i += 2;
            while (i < len && d > 0) {
                if (s[i] == '{') d++;
                else if (s[i] == '}') d--;
                i++;
            }
            i--; // will be incremented by for loop
            continue;
        }
        if (s[i] == '{') {
            depth++;
        } else if (s[i] == '}') {
            depth--;
            if (depth == 0 && found_comma_or_dotdot) return true;
        } else if (depth == 1) {
            if (s[i] == ',') found_comma_or_dotdot = true;
            if (s[i] == '.' && i + 1 < len && s[i + 1] == '.') found_comma_or_dotdot = true;
        }
    }
    return false;
}

static MIR_op_t bm_transpile_word(BashMirTranspiler* mt, BashAstNode* node, bool suppress_tilde) {
    BashWordNode* word = (BashWordNode*)node;
    if (word->text) {
        const char* chars = word->text->chars;
        int len = word->text->len;

        // check if tilde expansion is possible (original word starts with literal ~)
        // tilde expansion is disabled in double-quoted strings (no_backslash_escape=true)
        bool tilde_eligible = (!suppress_tilde && !word->no_backslash_escape && len >= 1 && chars[0] == '~');

        // process backslash escapes: \x → x (removes the escape backslash)
        // only for unquoted words (string fragments set no_backslash_escape=true)
        // check if there's any backslash first (fast path)
        bool has_backslash = false;
        if (!word->no_backslash_escape) {
            for (int i = 0; i < len - 1; i++) {
                if (chars[i] == '\\') { has_backslash = true; break; }
            }
        }
        if (has_backslash) {
            // build unescaped copy
            char* buf = (char*)alloca(len + 1);
            int out = 0;
            for (int i = 0; i < len; i++) {
                if (chars[i] == '\\' && i + 1 < len) {
                    i++; // skip backslash, take next char literally
                    buf[out++] = chars[i];
                } else {
                    buf[out++] = chars[i];
                }
            }
            chars = buf;
            len = out;
        }

        // tilde expansion: ~ → $HOME, ~/path → $HOME/path, ~user → user home, ~-/~+ → OLDPWD/PWD
        // only when the original ~ was literal (not escaped with \~)
        if (tilde_eligible) {
            MIR_op_t word_val = bm_emit_string_literal(mt, chars, len);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_expand_tilde", word_val);
            return MIR_new_reg_op(mt->ctx, result);
        }

        if (has_backslash) {
            return bm_emit_string_literal(mt, chars, len);
        }

        // brace and glob expansion are handled at command arg level only
        // (not here, to avoid expanding patterns in ${var#pat} contexts)
        return bm_emit_string_literal(mt, chars, len);
    }
    return bm_emit_string_literal(mt, "", 0);
}

// check if an argument node is $@ (bare or quoted) that needs splatting
static bool arg_is_at_splat(BashAstNode* node) {
    if (node->node_type == BASH_AST_NODE_SPECIAL_VARIABLE) {
        BashSpecialVarNode* sv = (BashSpecialVarNode*)node;
        return sv->special_id == BASH_SPECIAL_AT;
    }
    // "$@" — string containing only $@
    if (node->node_type == BASH_AST_NODE_STRING) {
        BashStringNode* sn = (BashStringNode*)node;
        if (sn->parts && !sn->parts->next &&
            sn->parts->node_type == BASH_AST_NODE_SPECIAL_VARIABLE) {
            BashSpecialVarNode* sv = (BashSpecialVarNode*)sn->parts;
            return sv->special_id == BASH_SPECIAL_AT;
        }
    }
    return false;
}

// transpile a word with command-argument-level expansions (brace, glob)
static MIR_op_t bm_transpile_cmd_arg(BashMirTranspiler* mt, BashAstNode* node) {
    if (node->node_type == BASH_AST_NODE_WORD) {
        BashWordNode* word = (BashWordNode*)node;
        if (word->text) {
            const char* chars = word->text->chars;
            int len = word->text->len;
            // brace expansion: {a,b,c} → "a b c", {1..5} → "1 2 3 4 5"
            if (word_is_brace(chars, len)) {
                MIR_op_t word_val = bm_emit_string_literal(mt, chars, len);
                MIR_reg_t result = bm_emit_call_1(mt, "bash_expand_brace", word_val);
                return MIR_new_reg_op(mt->ctx, result);
            }
            // glob expansion: *.txt etc.
            if (word_has_glob(chars, len)) {
                MIR_op_t word_val = bm_emit_string_literal(mt, chars, len);
                MIR_reg_t result = bm_emit_call_1(mt, "bash_glob_expand", word_val);
                return MIR_new_reg_op(mt->ctx, result);
            }
            // tilde-assign expansion for assignment-like args (FOO=~/bin)
            // only for unquoted words containing '~' with '=' (non-posix mode)
            bool has_tilde = false;
            bool has_eq = false;
            for (int ci = 0; ci < len; ci++) {
                if (chars[ci] == '~') has_tilde = true;
                if (chars[ci] == '=') has_eq = true;
            }
            if (has_tilde && has_eq && !word->no_backslash_escape) {
                MIR_op_t val = bm_transpile_node(mt, node);
                MIR_reg_t tilde_res = bm_emit_call_1(mt, "bash_expand_tilde_assign_arg", val);
                return MIR_new_reg_op(mt->ctx, tilde_res);
            }
        }
    }
    // unquoted command substitution: apply IFS word-splitting to result
    if (node->node_type == BASH_AST_NODE_COMMAND_SUB) {
        MIR_op_t raw = bm_transpile_node(mt, node);
        MIR_reg_t split = bm_emit_call_1(mt, "bash_cmd_sub_word_split", raw);
        return MIR_new_reg_op(mt->ctx, split);
    }
    return bm_transpile_node(mt, node);
}

static MIR_op_t bm_transpile_varref(BashMirTranspiler* mt, BashVarRefNode* node) {
    if (node->name) {
        if (node->name->len == 6 && memcmp(node->name->chars, "LINENO", 6) == 0) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_get_lineno");
            return MIR_new_reg_op(mt->ctx, result);
        }
        // $FUNCNAME without index → FUNCNAME[0]
        if (node->name->len == 8 && memcmp(node->name->chars, "FUNCNAME", 8) == 0) {
            MIR_op_t idx = MIR_new_uint_op(mt->ctx, i2it(0));
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_funcname", idx);
            return MIR_new_reg_op(mt->ctx, result);
        }
        return bm_emit_get_var(mt, node->name->chars);
    }
    return bm_emit_string_literal(mt, "", 0);
}

static MIR_op_t bm_transpile_string(BashMirTranspiler* mt, BashStringNode* node) {
    // concatenate all parts
    BashAstNode* part = node->parts;
    if (!part) return bm_emit_string_literal(mt, "", 0);
    if (!part->next) return bm_transpile_node(mt, part);

    // multiple parts: concatenate
    MIR_op_t result = bm_transpile_node(mt, part);
    part = part->next;
    while (part) {
        MIR_op_t next_val = bm_transpile_node(mt, part);
        MIR_reg_t concat_result = bm_emit_call_2(mt, "bash_string_concat", result, next_val);
        result = MIR_new_reg_op(mt->ctx, concat_result);
        part = part->next;
    }
    return result;
}

static MIR_op_t bm_transpile_concat(BashMirTranspiler* mt, BashConcatNode* node) {
    BashAstNode* part = node->parts;
    if (!part) return bm_emit_string_literal(mt, "", 0);
    if (!part->next) return bm_transpile_node(mt, part);

    // check if first part is a tilde-only word being concatenated with a non-slash part
    // e.g. ~$USER → the ~ must NOT be tilde-expanded (bash evaluates ~ before $USER,
    // yielding ~root rather than /home/user + root)
    bool tilde_first_suppressed = false;
    MIR_op_t result;
    if (part->node_type == BASH_AST_NODE_WORD) {
        BashWordNode* w = (BashWordNode*)part;
        if (w->text && w->text->len == 1 && w->text->chars[0] == '~') {
            // check next part: if it's not a word starting with '/', suppress tilde expand
            BashAstNode* nxt = part->next;
            bool next_is_slash = false;
            if (nxt && nxt->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* nw = (BashWordNode*)nxt;
                if (nw->text && nw->text->len > 0 && nw->text->chars[0] == '/') {
                    next_is_slash = true;
                }
            }
            if (!next_is_slash) {
                // emit ~ literally (no tilde expansion)
                result = bm_emit_string_literal(mt, "~", 1);
                tilde_first_suppressed = true;
            }
        }
    }
    if (!tilde_first_suppressed) {
        result = bm_transpile_node(mt, part);
    }
    part = part->next;
    while (part) {
        // suppress tilde expansion for non-first concat parts: ~user in middle of word
        // is not a valid tilde prefix. bash_expand_tilde_assign handles :~ in assignments.
        MIR_op_t next_val;
        if (part->node_type == BASH_AST_NODE_WORD) {
            next_val = bm_transpile_word(mt, part, /*suppress_tilde=*/true);
        } else {
            next_val = bm_transpile_node(mt, part);
        }
        MIR_reg_t concat_result = bm_emit_call_2(mt, "bash_string_concat", result, next_val);
        result = MIR_new_reg_op(mt->ctx, concat_result);
        part = part->next;
    }
    return result;
}

static MIR_op_t bm_transpile_expansion(BashMirTranspiler* mt, BashAstNode* node) {
    BashExpansionNode* exp = (BashExpansionNode*)node;
    if (!exp->variable && !exp->inner_expr) return bm_emit_string_literal(mt, "", 0);

    // check if variable is a positional parameter (digits only → $1-$9, ${10}, etc.)
    MIR_op_t var_val;
    if (exp->inner_expr) {
        // inner_expr overrides variable lookup (e.g. ${arr[idx]:-default})
        var_val = bm_transpile_node(mt, exp->inner_expr);
    } else if (exp->variable->len == 1 && (exp->variable->chars[0] == '*' || exp->variable->chars[0] == '@')) {
        // ${*} / ${@} / ${*-default} / ${#*} etc. — get all positional args
        MIR_reg_t all_args = bm_emit_call_0(mt, "bash_get_all_args_string");
        var_val = MIR_new_reg_op(mt->ctx, all_args);
    } else if (exp->variable->len >= 1 && exp->variable->chars[0] >= '1' && exp->variable->chars[0] <= '9') {
        // check if ALL chars are digits (positional param)
        bool all_digits = true;
        for (int i = 0; i < exp->variable->len; i++) {
            if (exp->variable->chars[i] < '0' || exp->variable->chars[i] > '9') {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            int pos = 0;
            for (int i = 0; i < exp->variable->len; i++) {
                pos = pos * 10 + (exp->variable->chars[i] - '0');
            }
            MIR_reg_t positional = bm_emit_call_1(mt, "bash_get_positional",
                MIR_new_int_op(mt->ctx, pos));
            var_val = MIR_new_reg_op(mt->ctx, positional);
        } else {
            var_val = bm_emit_get_var(mt, exp->variable->chars);
        }
    } else {
        var_val = bm_emit_get_var(mt, exp->variable->chars);
    }

    switch (exp->expand_type) {
    case BASH_EXPAND_LENGTH: {
        MIR_reg_t result = bm_emit_call_1(mt, "bash_string_length", var_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_DEFAULT: {
        MIR_op_t def_val = exp->argument ? bm_transpile_node(mt, exp->argument)
                                         : bm_emit_string_literal(mt, "", 0);
        const char* func = exp->has_colon ? "bash_expand_default" : "bash_expand_default_nocolon";
        MIR_reg_t result = bm_emit_call_2(mt, func, var_val, def_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_ASSIGN_DEFAULT: {
        MIR_op_t def_val = exp->argument ? bm_transpile_node(mt, exp->argument)
                                         : bm_emit_string_literal(mt, "", 0);
        MIR_op_t var_name = bm_emit_string_literal(mt, exp->variable->chars, exp->variable->len);
        const char* func = exp->has_colon ? "bash_expand_assign_default" : "bash_expand_assign_default_nocolon";
        MIR_reg_t result = bm_emit_call_3(mt, func,
            var_name, var_val, def_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_ALT: {
        MIR_op_t alt_val = exp->argument ? bm_transpile_node(mt, exp->argument)
                                         : bm_emit_string_literal(mt, "", 0);
        const char* func = exp->has_colon ? "bash_expand_alt" : "bash_expand_alt_nocolon";
        MIR_reg_t result = bm_emit_call_2(mt, func, var_val, alt_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_TRIM_PREFIX:
    case BASH_EXPAND_TRIM_PREFIX_LONG:
    case BASH_EXPAND_TRIM_SUFFIX:
    case BASH_EXPAND_TRIM_SUFFIX_LONG: {
        MIR_op_t pat = exp->argument ? bm_transpile_node(mt, exp->argument)
                                     : bm_emit_string_literal(mt, "", 0);
        const char* func = NULL;
        switch (exp->expand_type) {
        case BASH_EXPAND_TRIM_PREFIX:      func = "bash_expand_trim_prefix"; break;
        case BASH_EXPAND_TRIM_PREFIX_LONG: func = "bash_expand_trim_prefix_long"; break;
        case BASH_EXPAND_TRIM_SUFFIX:      func = "bash_expand_trim_suffix"; break;
        case BASH_EXPAND_TRIM_SUFFIX_LONG: func = "bash_expand_trim_suffix_long"; break;
        default: break;
        }
        MIR_reg_t result = bm_emit_call_2(mt, func, var_val, pat);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_REPLACE:
    case BASH_EXPAND_REPLACE_ALL: {
        MIR_op_t pat = exp->argument ? bm_transpile_node(mt, exp->argument)
                                     : bm_emit_string_literal(mt, "", 0);
        MIR_op_t repl = exp->replacement ? bm_transpile_node(mt, exp->replacement)
                                         : bm_emit_string_literal(mt, "", 0);
        const char* func = (exp->expand_type == BASH_EXPAND_REPLACE_ALL)
                           ? "bash_expand_replace_all" : "bash_expand_replace";
        MIR_reg_t result = bm_emit_call_3(mt, func, var_val, pat, repl);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_SUBSTRING: {
        MIR_op_t offset = exp->argument ? bm_transpile_node(mt, exp->argument)
                                        : bm_emit_int_literal(mt, 0);
        MIR_op_t length = exp->replacement ? bm_transpile_node(mt, exp->replacement)
                                           : bm_emit_int_literal(mt, -1);
        MIR_reg_t result = bm_emit_call_3(mt, "bash_expand_substring", var_val, offset, length);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_UPPER_FIRST:
    case BASH_EXPAND_UPPER_ALL:
    case BASH_EXPAND_LOWER_FIRST:
    case BASH_EXPAND_LOWER_ALL: {
        const char* func = NULL;
        switch (exp->expand_type) {
        case BASH_EXPAND_UPPER_FIRST: func = "bash_expand_upper_first"; break;
        case BASH_EXPAND_UPPER_ALL:   func = "bash_expand_upper_all"; break;
        case BASH_EXPAND_LOWER_FIRST: func = "bash_expand_lower_first"; break;
        case BASH_EXPAND_LOWER_ALL:   func = "bash_expand_lower_all"; break;
        default: break;
        }
        MIR_reg_t result = bm_emit_call_1(mt, func, var_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    default:
        return var_val;
    }
}

// ============================================================================
// Command transpilation
// ============================================================================

static MIR_op_t bm_transpile_command(BashMirTranspiler* mt, BashCommandNode* cmd) {
    if (!cmd->name) {
        // prefix assignments only (no command)
        BashAstNode* assign = cmd->assignments;
        while (assign) {
            BashAssignmentNode* a = (BashAssignmentNode*)assign;
            bm_transpile_statement(mt, assign);
            assign = assign->next;
        }
        return bm_emit_int_literal(mt, 0);
    }

    // detect: a=1 b=2 parsed as command with prefix-assign a=1 and name "b=2"
    // this happens due to grammar ambiguity resolved in favor of 'command'
    // check if command_name is a word that looks like VAR=VALUE
    BashWordNode* name_node = (BashWordNode*)cmd->name;
    if (cmd->name->node_type == BASH_AST_NODE_WORD && name_node->text) {
        const char* nm = name_node->text->chars;
        int nm_len = (int)name_node->text->len;
        int eq_pos = -1;
        int valid = 1;
        for (int i = 0; i < nm_len; i++) {
            if (nm[i] == '=') { eq_pos = i; break; }
            if (!((nm[i] >= 'a' && nm[i] <= 'z') || (nm[i] >= 'A' && nm[i] <= 'Z') ||
                  (nm[i] >= '0' && nm[i] <= '9') || nm[i] == '_')) { valid = 0; break; }
        }
        if (valid && eq_pos > 0 && !cmd->args) {
            // looks like VAR=VALUE with no further arguments — treat as variable assignments
            // run any prefix assignments (stored reversed — iterate as-is)
            BashAstNode* assign = cmd->assignments;
            while (assign) {
                bm_transpile_statement(mt, assign);
                assign = assign->next;
            }
            // emit assignment for the "name=value" text
            const char* val_str = nm + eq_pos + 1;
            int val_len = nm_len - eq_pos - 1;
            MIR_op_t name_item = bm_emit_string_literal(mt, nm, (size_t)eq_pos);
            MIR_op_t val_item = bm_emit_string_literal(mt, val_str, (size_t)val_len);
            bm_emit_call_void_2(mt, "bash_set_var", name_item, val_item);
            return bm_emit_int_literal(mt, 0);
        }
    }

    // get command name
    if (cmd->name->node_type != BASH_AST_NODE_WORD || !name_node->text) {
        // dynamic command name (variable expansion, concatenation, etc.)
        // evaluate the expression, then dispatch via runtime function + external fallback
        MIR_op_t dyn_name = bm_transpile_node(mt, cmd->name);

        int argc = cmd->arg_count;
        MIR_reg_t dyn_args_ptr = bm_new_temp(mt);
        if (argc > 0) {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_ALLOCA, MIR_new_reg_op(mt->ctx, dyn_args_ptr),
                             MIR_new_int_op(mt->ctx, argc * (int)sizeof(Item))));
            int i = 0;
            BashAstNode* arg = cmd->args;
            while (arg && i < argc) {
                MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       dyn_args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
        } else {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, dyn_args_ptr),
                             MIR_new_uint_op(mt->ctx, 0)));
        }

        // try runtime function first
        MIR_reg_t dyn_result = bm_new_temp(mt);
        MIR_reg_t rt_res = bm_emit_call_3(mt, "bash_call_rt_func",
            dyn_name,
            MIR_new_reg_op(mt->ctx, dyn_args_ptr),
            MIR_new_int_op(mt->ctx, argc));
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, dyn_result),
                         MIR_new_reg_op(mt->ctx, rt_res)));

        // if exit code == 127, function not found → try external
        MIR_label_t dyn_try_ext = bm_new_label(mt);
        MIR_label_t dyn_done = bm_new_label(mt);
        MIR_reg_t dyn_ec = bm_emit_call_0(mt, "bash_get_exit_code");
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BEQ,
                MIR_new_label_op(mt->ctx, dyn_try_ext),
                MIR_new_reg_op(mt->ctx, dyn_ec),
                MIR_new_uint_op(mt->ctx, i2it(127))));
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, dyn_done)));

        // external fallback
        MIR_append_insn(mt->ctx, mt->current_func_item, dyn_try_ext);
        {
            int total_argc = 1 + argc;
            MIR_reg_t ext_args = bm_new_temp(mt);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_ALLOCA, MIR_new_reg_op(mt->ctx, ext_args),
                             MIR_new_int_op(mt->ctx, total_argc * (int)sizeof(Item))));
            // argv[0] = dynamic name
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, ext_args, 0, 1),
                    dyn_name));
            // argv[1..] = arguments
            for (int i = 0; i < argc; i++) {
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, (i + 1) * (int)sizeof(Item),
                                       ext_args, 0, 1),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       dyn_args_ptr, 0, 1)));
            }
            MIR_reg_t ext_res = bm_emit_call_2(mt, "bash_exec_external",
                MIR_new_reg_op(mt->ctx, ext_args),
                MIR_new_int_op(mt->ctx, total_argc));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, dyn_result),
                             MIR_new_reg_op(mt->ctx, ext_res)));
        }

        MIR_append_insn(mt->ctx, mt->current_func_item, dyn_done);
        return MIR_new_reg_op(mt->ctx, dyn_result);
    }

    const char* cmd_name = name_node->text->chars;
    int cmd_len = name_node->text->len;

    // set BASH_COMMAND to the source text of this command
    if (mt->tp->source && cmd->base.node.id) {
        uint32_t sb = ts_node_start_byte(cmd->base.node);
        uint32_t eb = ts_node_end_byte(cmd->base.node);
        if (eb > sb && eb <= (uint32_t)mt->tp->source_length) {
            MIR_op_t cmd_text = bm_emit_string_literal(mt, mt->tp->source + sb, (size_t)(eb - sb));
            bm_emit_call_void_1(mt, "bash_set_command", cmd_text);
        }
    }

    // IFS word splitting: if any arg is an unquoted expansion AND the command is
    // known to bash_exec_cmd_with_array, use array-based dispatch so that $var
    // gets split by IFS before being passed as separate arguments.
    // Commands handled: echo, printf, test, read, caller, cat, wc, head, tail,
    // grep, sort, tr, cut, let, type, command, pushd, popd, dirs, getopts,
    // plus user-defined functions and external commands.
    // Commands NOT handled here: :, eval, [, source, export, local, declare,
    // return, exit, shift, cd, pwd — these have special transpiler-level handling.
    if (cmd->arg_count > 0 && cmd_has_unquoted_expansion(cmd)) {
        // skip IFS dispatch for commands with special transpiler handling
        bool skip_ifs_dispatch = false;
        if (cmd_len == 1 && (cmd_name[0] == ':' || cmd_name[0] == '[' || cmd_name[0] == '.')) skip_ifs_dispatch = true;
        else if (cmd_len == 4 && memcmp(cmd_name, "eval", 4) == 0) skip_ifs_dispatch = true;
        else if (cmd_len == 3 && memcmp(cmd_name, "set", 3) == 0) skip_ifs_dispatch = true;
        else if (cmd_len == 5 && (memcmp(cmd_name, "local", 5) == 0 || memcmp(cmd_name, "shift", 5) == 0)) skip_ifs_dispatch = true;
        else if (cmd_len == 5 && memcmp(cmd_name, "unset", 5) == 0) skip_ifs_dispatch = true;
        else if (cmd_len == 6 && (memcmp(cmd_name, "export", 6) == 0 || memcmp(cmd_name, "return", 6) == 0 || memcmp(cmd_name, "source", 6) == 0)) skip_ifs_dispatch = true;
        else if (cmd_len == 4 && memcmp(cmd_name, "exit", 4) == 0) skip_ifs_dispatch = true;
        else if (cmd_len == 7 && (memcmp(cmd_name, "declare", 7) == 0 || memcmp(cmd_name, "typeset", 7) == 0)) skip_ifs_dispatch = true;
        else if (cmd_len == 2 && (memcmp(cmd_name, "cd", 2) == 0 || memcmp(cmd_name, "bg", 2) == 0 || memcmp(cmd_name, "fg", 2) == 0)) skip_ifs_dispatch = true;
        else if (cmd_len == 3 && memcmp(cmd_name, "pwd", 3) == 0) skip_ifs_dispatch = true;
        else if (cmd_len == 8 && memcmp(cmd_name, "readonly", 8) == 0) skip_ifs_dispatch = true;
        if (!skip_ifs_dispatch) {
            return bm_emit_ifs_split_cmd(mt, cmd_name, cmd_len, cmd);
        }
    }

    // handle built-in commands
    if (cmd_len == 4 && memcmp(cmd_name, "echo", 4) == 0) {
        int argc = cmd->arg_count;
        if (argc == 0) {
            MIR_reg_t result = bm_emit_call_2(mt, "bash_builtin_echo",
                MIR_new_uint_op(mt->ctx, 0), MIR_new_int_op(mt->ctx, 0));
            return MIR_new_reg_op(mt->ctx, result);
        }

        // check if any arg needs $@ splatting
        bool needs_splat = false;
        {
            BashAstNode* a = cmd->args;
            while (a) {
                if (arg_is_at_splat(a)) { needs_splat = true; break; }
                a = a->next;
            }
        }

        MIR_reg_t args_ptr = bm_new_temp(mt);
        MIR_reg_t argc_reg = bm_new_temp(mt);

        if (needs_splat) {
            bm_emit_call_void_0(mt, "bash_arg_builder_start");
            BashAstNode* arg = cmd->args;
            while (arg) {
                if (arg_is_at_splat(arg)) {
                    bm_emit_call_void_0(mt, "bash_arg_builder_push_at");
                } else {
                    MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                    bm_emit_call_void_1(mt, "bash_arg_builder_push", arg_val);
                }
                arg = arg->next;
            }
            MIR_reg_t ptr_r = bm_emit_call_0(mt, "bash_arg_builder_get_ptr");
            MIR_reg_t cnt_r = bm_emit_call_0(mt, "bash_arg_builder_get_count");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_reg_op(mt->ctx, ptr_r)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                             MIR_new_reg_op(mt->ctx, cnt_r)));
        } else {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_uint_op(mt->ctx, (uint64_t)(uintptr_t)malloc(argc * sizeof(Item)))));

            int i = 0;
            BashAstNode* arg = cmd->args;
            while (arg && i < argc) {
                MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                             MIR_new_int_op(mt->ctx, argc)));
        }

        MIR_reg_t result = bm_emit_call_2(mt, "bash_builtin_echo",
            MIR_new_reg_op(mt->ctx, args_ptr), MIR_new_reg_op(mt->ctx, argc_reg));
        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 6 && memcmp(cmd_name, "printf", 6) == 0) {
        // printf [-v var] format [args...]
        if (cmd->arg_count == 0) return bm_emit_int_literal(mt, 0);

        // check if any arg needs $@ splatting
        bool needs_splat = false;
        {
            BashAstNode* a = cmd->args;
            while (a) {
                if (arg_is_at_splat(a)) { needs_splat = true; break; }
                a = a->next;
            }
        }

        MIR_reg_t args_ptr = bm_new_temp(mt);
        MIR_reg_t argc_reg = bm_new_temp(mt);

        if (needs_splat) {
            bm_emit_call_void_0(mt, "bash_arg_builder_start");
            BashAstNode* arg = cmd->args;
            while (arg) {
                if (arg_is_at_splat(arg)) {
                    bm_emit_call_void_0(mt, "bash_arg_builder_push_at");
                } else {
                    MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                    bm_emit_call_void_1(mt, "bash_arg_builder_push", arg_val);
                }
                arg = arg->next;
            }
            MIR_reg_t ptr_r = bm_emit_call_0(mt, "bash_arg_builder_get_ptr");
            MIR_reg_t cnt_r = bm_emit_call_0(mt, "bash_arg_builder_get_count");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_reg_op(mt->ctx, ptr_r)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                             MIR_new_reg_op(mt->ctx, cnt_r)));
        } else {
            int total_argc = cmd->arg_count;
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_uint_op(mt->ctx, (uint64_t)(uintptr_t)malloc(total_argc * sizeof(Item)))));
            int i = 0;
            BashAstNode* arg = cmd->args;
            while (arg && i < total_argc) {
                MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                             MIR_new_int_op(mt->ctx, total_argc)));
        }

        MIR_reg_t result = bm_emit_call_2(mt, "bash_builtin_printf",
            MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_new_reg_op(mt->ctx, argc_reg));
        return MIR_new_reg_op(mt->ctx, result);
    }

    // : (colon) — no-op, always returns 0
    if (cmd_len == 1 && cmd_name[0] == ':') {
        // evaluate args (for side effects like ${var:=default}) but discard
        BashAstNode* arg = cmd->args;
        while (arg) {
            bm_transpile_cmd_arg(mt, arg);
            arg = arg->next;
        }
        MIR_reg_t result = bm_emit_call_0(mt, "bash_builtin_true");
        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 4 && memcmp(cmd_name, "true", 4) == 0) {
        MIR_reg_t result = bm_emit_call_0(mt, "bash_builtin_true");
        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 5 && memcmp(cmd_name, "false", 5) == 0) {
        MIR_reg_t result = bm_emit_call_0(mt, "bash_builtin_false");
        return MIR_new_reg_op(mt->ctx, result);
    }

    // eval — concatenate args with spaces and evaluate as bash code
    if (cmd_len == 4 && memcmp(cmd_name, "eval", 4) == 0) {
        if (!cmd->args) {
            MIR_reg_t result = bm_emit_call_0(mt, "bash_builtin_true");
            return MIR_new_reg_op(mt->ctx, result);
        }
        // build concatenated string from args
        MIR_op_t combined = bm_transpile_cmd_arg(mt, cmd->args);
        BashAstNode* arg = cmd->args->next;
        while (arg) {
            // add space separator
            MIR_op_t space = bm_emit_string_literal(mt, " ", 1);
            MIR_reg_t with_space = bm_emit_call_2(mt, "bash_string_concat", combined, space);
            MIR_op_t next_val = bm_transpile_cmd_arg(mt, arg);
            MIR_reg_t concatenated = bm_emit_call_2(mt, "bash_string_concat",
                MIR_new_reg_op(mt->ctx, with_space), next_val);
            combined = MIR_new_reg_op(mt->ctx, concatenated);
            arg = arg->next;
        }

        // handle prefix assignments (e.g., IFS=: eval echo \$x)
        int n_pa_eval = 0;
        MIR_reg_t pa_eval_saved[8];
        String* pa_eval_names[8];
        BashAstNode* pa_node = cmd->assignments;
        while (pa_node && n_pa_eval < 8) {
            BashAssignmentNode* a = (BashAssignmentNode*)pa_node;
            if (a->name) {
                pa_eval_names[n_pa_eval] = a->name;
                MIR_op_t nop = bm_emit_string_literal(mt, a->name->chars, a->name->len);
                pa_eval_saved[n_pa_eval] = bm_emit_call_1(mt, "bash_get_var", nop);
                MIR_op_t vop = a->value ? bm_transpile_node(mt, a->value)
                                        : bm_emit_string_literal(mt, "", 0);
                bm_emit_call_void_2(mt, "bash_set_var", nop, vop);
                n_pa_eval++;
            }
            pa_node = pa_node->next;
        }

        MIR_reg_t result = bm_emit_call_1(mt, "bash_eval_string", combined);

        // restore prefix assignments
        for (int i = 0; i < n_pa_eval; i++) {
            MIR_op_t nop = bm_emit_string_literal(mt, pa_eval_names[i]->chars, pa_eval_names[i]->len);
            bm_emit_call_void_2(mt, "bash_set_var", nop, MIR_new_reg_op(mt->ctx, pa_eval_saved[i]));
        }

        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 5 && memcmp(cmd_name, "shift", 5) == 0) {
        if (cmd->args) {
            // shift N — evaluate argument at runtime (could be $var, $((...)), etc.)
            MIR_op_t arg_val = bm_transpile_cmd_arg(mt, cmd->args);
            MIR_reg_t n_reg = bm_emit_call_1(mt, "bash_to_int_val", arg_val);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_shift_args",
                MIR_new_reg_op(mt->ctx, n_reg));
            return MIR_new_reg_op(mt->ctx, result);
        } else {
            MIR_reg_t result = bm_emit_call_1(mt, "bash_shift_args",
                MIR_new_int_op(mt->ctx, 1));
            return MIR_new_reg_op(mt->ctx, result);
        }
    }

    if (cmd_len == 6 && memcmp(cmd_name, "caller", 6) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_caller", cmd);
    }

    if (cmd_len == 5 && memcmp(cmd_name, "local", 5) == 0) {
        // local var=value — write to current function scope frame via bash_set_local_var
        BashAstNode* arg = cmd->args;
        while (arg) {
            if (arg->node_type == BASH_AST_NODE_ASSIGNMENT) {
                BashAssignmentNode* assign = (BashAssignmentNode*)arg;
                MIR_op_t val;
                if (assign->value) {
                    val = bm_transpile_node(mt, assign->value);
                } else {
                    val = bm_emit_string_literal(mt, "", 0);
                }
                bm_emit_set_local_var(mt, assign->name->chars, val);
            } else if (arg->node_type == BASH_AST_NODE_WORD) {
                // local varname (no value) — declare as empty in current scope
                BashWordNode* w = (BashWordNode*)arg;
                if (w->text) {
                    MIR_op_t val = bm_emit_string_literal(mt, "", 0);
                    bm_emit_set_local_var(mt, w->text->chars, val);
                }
            }
            arg = arg->next;
        }
        return bm_emit_int_literal(mt, 0);
    }

    if (cmd_len == 2 && memcmp(cmd_name, "cd", 2) == 0) {
        MIR_op_t dir_arg;
        if (cmd->args) {
            dir_arg = bm_transpile_cmd_arg(mt, cmd->args);
        } else {
            dir_arg = bm_emit_string_literal(mt, "", 0);
        }
        MIR_reg_t result = bm_emit_call_1(mt, "bash_builtin_cd", dir_arg);
        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 5 && memcmp(cmd_name, "unset", 5) == 0) {
        if (!cmd->args) return bm_emit_int_literal(mt, 0);

        BashAstNode* arg = cmd->args;

        // check for -f flag (unset function)
        bool is_func_unset = false;
        if (arg->node_type == BASH_AST_NODE_RAW_STRING) {
            BashRawStringNode* rs = (BashRawStringNode*)arg;
            if (rs->text && rs->text->len == 2 && memcmp(rs->text->chars, "-f", 2) == 0) {
                is_func_unset = true;
                arg = arg->next; // skip -f
            }
        }

        if (is_func_unset) {
            // unset -f funcname — remove from user_funcs so future calls fall through
            while (arg) {
                const char* text = NULL; int text_len = 0;
                if (arg->node_type == BASH_AST_NODE_WORD) {
                    text = ((BashWordNode*)arg)->text->chars;
                    text_len = ((BashWordNode*)arg)->text->len;
                } else if (arg->node_type == BASH_AST_NODE_RAW_STRING) {
                    text = ((BashRawStringNode*)arg)->text->chars;
                    text_len = ((BashRawStringNode*)arg)->text->len;
                }
                if (text && text_len > 0) {
                    BashMirUserFunc del_key;
                    memset(&del_key, 0, sizeof(del_key));
                    int copy = text_len < (int)(sizeof(del_key.name) - 1) ? text_len : (int)(sizeof(del_key.name) - 1);
                    memcpy(del_key.name, text, copy);
                    del_key.name[copy] = '\0';
                    hashmap_delete(mt->user_funcs, &del_key);
                }
                arg = arg->next;
            }
            return bm_emit_int_literal(mt, 0);
        }

        // unset 'arr[idx]' or unset var — process all args
        while (arg) {
            // skip -v, -n flags
            if (arg->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* w = (BashWordNode*)arg;
                if (w->text && w->text->len == 2 &&
                    (memcmp(w->text->chars, "-v", 2) == 0 || memcmp(w->text->chars, "-n", 2) == 0)) {
                    arg = arg->next;
                    continue;
                }
            }
            if (arg->node_type == BASH_AST_NODE_WORD || arg->node_type == BASH_AST_NODE_RAW_STRING) {
                // parse "arr[idx]" from the raw string
                const char* text;
                int text_len;
                if (arg->node_type == BASH_AST_NODE_WORD) {
                    BashWordNode* w = (BashWordNode*)arg;
                    text = w->text->chars;
                    text_len = w->text->len;
                } else {
                    BashRawStringNode* rs = (BashRawStringNode*)arg;
                    text = rs->text->chars;
                    text_len = rs->text->len;
                }
                // find '[' in text
                const char* bracket = NULL;
                for (int i = 0; i < text_len; i++) {
                    if (text[i] == '[') { bracket = text + i; break; }
                }
                if (bracket) {
                    int name_len = (int)(bracket - text);
                    MIR_op_t arr_val = bm_emit_get_var_n(mt, text, name_len);
                    // extract index between [ and ]
                    const char* idx_start = bracket + 1;
                    const char* idx_end = text + text_len;
                    while (idx_end > idx_start && *(idx_end - 1) == ']') idx_end--;
                    int idx_len = (int)(idx_end - idx_start);
                    MIR_op_t idx_val = bm_emit_string_literal(mt, idx_start, idx_len);
                    // check name for compile-time assoc type
                    char name_buf[128];
                    int copy_len = name_len < 127 ? name_len : 127;
                    memcpy(name_buf, text, copy_len);
                    name_buf[copy_len] = '\0';
                    const char* fn = bm_is_assoc_var(mt, name_buf)
                                     ? "bash_assoc_unset" : "bash_array_unset";
                    bm_emit_call_2(mt, fn, arr_val, idx_val);
                } else {
                    // unset plain variable
                    MIR_op_t name_op = bm_emit_string_literal(mt, text, text_len);
                    bm_emit_call_void_1(mt, "bash_unset_var", name_op);
                }
            }
            arg = arg->next;
        }
        return bm_emit_int_literal(mt, 0);
    }

    // pipeline builtins (read from stdin item)
    if (cmd_len == 3 && memcmp(cmd_name, "cat", 3) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_cat", cmd);
    }
    if (cmd_len == 4 && memcmp(cmd_name, "read", 4) == 0) {
        // handle prefix assignments (e.g., IFS=: read x y)
        int n_prefix = 0;
        MIR_reg_t saved_vals[8]; // max 8 prefix assignments
        String* saved_names[8];
        BashAstNode* pa = cmd->assignments;
        while (pa && n_prefix < 8) {
            BashAssignmentNode* a = (BashAssignmentNode*)pa;
            if (a->name) {
                saved_names[n_prefix] = a->name;
                // save current value
                MIR_op_t name_op = bm_emit_string_literal(mt, a->name->chars, a->name->len);
                saved_vals[n_prefix] = bm_emit_call_1(mt, "bash_get_var", name_op);
                // set new value
                MIR_op_t val_op = a->value ? bm_transpile_node(mt, a->value)
                                           : bm_emit_string_literal(mt, "", 0);
                bm_emit_call_void_2(mt, "bash_set_var", name_op, val_op);
                n_prefix++;
            }
            pa = pa->next;
        }
        MIR_op_t result = bm_emit_varargs_builtin(mt, "bash_builtin_read", cmd);
        // restore prefix assignments
        for (int i = 0; i < n_prefix; i++) {
            MIR_op_t name_op = bm_emit_string_literal(mt, saved_names[i]->chars, saved_names[i]->len);
            bm_emit_call_void_2(mt, "bash_set_var", name_op, MIR_new_reg_op(mt->ctx, saved_vals[i]));
        }
        return result;
    }
    if (cmd_len == 2 && memcmp(cmd_name, "wc", 2) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_wc", cmd);
    }
    if (cmd_len == 4 && memcmp(cmd_name, "head", 4) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_head", cmd);
    }
    if (cmd_len == 4 && memcmp(cmd_name, "tail", 4) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_tail", cmd);
    }
    if (cmd_len == 4 && memcmp(cmd_name, "grep", 4) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_grep", cmd);
    }
    if (cmd_len == 4 && memcmp(cmd_name, "sort", 4) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_sort", cmd);
    }
    if (cmd_len == 2 && memcmp(cmd_name, "tr", 2) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_tr", cmd);
    }
    if (cmd_len == 3 && memcmp(cmd_name, "cut", 3) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_cut", cmd);
    }

    // source / . — execute file in current scope
    if ((cmd_len == 6 && memcmp(cmd_name, "source", 6) == 0) ||
        (cmd_len == 1 && cmd_name[0] == '.')) {
        if (cmd->args) {
            MIR_op_t filename = bm_transpile_cmd_arg(mt, cmd->args);
            MIR_reg_t result = bm_emit_call_1(mt, "bash_source_file", filename);
            return MIR_new_reg_op(mt->ctx, result);
        }
        return bm_emit_int_literal(mt, 1);
    }

    // trap — register signal/event handlers
    if (cmd_len == 4 && memcmp(cmd_name, "trap", 4) == 0) {
        if (cmd->args) {
            MIR_op_t handler_val = bm_transpile_cmd_arg(mt, cmd->args);
            BashAstNode* sig_arg = cmd->args->next;
            if (!sig_arg) {
                // trap with only handler (no signal) — treat as reset all or print; no-op
            } else {
                while (sig_arg) {
                    MIR_op_t sig_val = bm_transpile_cmd_arg(mt, sig_arg);
                    bm_emit_call_void_2(mt, "bash_trap_set", handler_val, sig_val);
                    sig_arg = sig_arg->next;
                }
            }
        }
        return bm_emit_int_literal(mt, 0);
    }

    // set — shell options
    if (cmd_len == 3 && memcmp(cmd_name, "set", 3) == 0) {
        BashAstNode* arg = cmd->args;

        // check for set -- arg1 arg2 ... (set positional parameters)
        // or set x arg1 arg2 ... (where x is first positional)
        bool is_set_positional = false;
        BashAstNode* pos_args = NULL;
        if (arg && arg->node_type == BASH_AST_NODE_WORD) {
            BashWordNode* w = (BashWordNode*)arg;
            if (w->text && w->text->len == 2 && memcmp(w->text->chars, "--", 2) == 0) {
                is_set_positional = true;
                pos_args = arg->next;
            }
        }
        // also handle: set x y z (non-flag first arg sets positionals)
        if (!is_set_positional && arg && arg->node_type == BASH_AST_NODE_WORD) {
            BashWordNode* w = (BashWordNode*)arg;
            if (w->text && w->text->len > 0 &&
                w->text->chars[0] != '-' && w->text->chars[0] != '+') {
                is_set_positional = true;
                pos_args = arg; // include the first arg
            }
        }
        // handle set $'\177' etc: first arg is a non-WORD node (raw_string, string, expansion, etc.)
        if (!is_set_positional && arg &&
            arg->node_type != BASH_AST_NODE_WORD) {
            is_set_positional = true;
            pos_args = arg;
        }

        if (is_set_positional) {
            // Use array-based approach with IFS splitting for unquoted expansions.
            // This supports: set x $var (splits $var by IFS into positional params)
            MIR_reg_t arr_reg = bm_emit_call_0(mt, "bash_array_new");
            MIR_op_t arr_val = MIR_new_reg_op(mt->ctx, arr_reg);
            BashAstNode* p = pos_args;
            while (p) {
                // check if this arg is an unquoted variable reference or expansion
                // (not a bare word, not a quoted string)
                bool needs_ifs_split = false;
                if (p->node_type == BASH_AST_NODE_VARIABLE_REF) {
                    needs_ifs_split = true;
                } else if (p->node_type == BASH_AST_NODE_EXPANSION) {
                    needs_ifs_split = true;
                } else if (p->node_type == BASH_AST_NODE_COMMAND_SUB) {
                    needs_ifs_split = true;
                }
                MIR_op_t val = bm_transpile_cmd_arg(mt, p);
                if (needs_ifs_split) {
                    MIR_reg_t new_arr = bm_emit_call_2(mt, "bash_ifs_split_into", arr_val, val);
                    arr_val = MIR_new_reg_op(mt->ctx, new_arr);
                } else {
                    MIR_reg_t new_arr = bm_emit_call_2(mt, "bash_array_append", arr_val, val);
                    arr_val = MIR_new_reg_op(mt->ctx, new_arr);
                }
                p = p->next;
            }
            bm_emit_call_void_1(mt, "bash_set_positional_from_array", arr_val);
            return bm_emit_int_literal(mt, 0);
        }

        while (arg) {
            if (arg->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* w = (BashWordNode*)arg;
                if (w->text && w->text->len >= 2) {
                    const char* flag = w->text->chars;
                    int flen = w->text->len;
                    if (flag[0] == '-' && flag[1] == 'o') {
                        // set -o optname (next arg is the option name)
                        arg = arg->next;
                        if (arg && arg->node_type == BASH_AST_NODE_WORD) {
                            BashWordNode* opt = (BashWordNode*)arg;
                            if (opt->text) {
                                MIR_op_t opt_val = bm_emit_string_literal(mt, opt->text->chars, opt->text->len);
                                bm_emit_call_void_2(mt, "bash_set_option", opt_val,
                                    MIR_new_uint_op(mt->ctx, 1));
                            }
                        }
                    } else if (flag[0] == '+' && flag[1] == 'o') {
                        // set +o optname (disable)
                        arg = arg->next;
                        if (arg && arg->node_type == BASH_AST_NODE_WORD) {
                            BashWordNode* opt = (BashWordNode*)arg;
                            if (opt->text) {
                                MIR_op_t opt_val = bm_emit_string_literal(mt, opt->text->chars, opt->text->len);
                                bm_emit_call_void_2(mt, "bash_set_option", opt_val,
                                    MIR_new_uint_op(mt->ctx, 0));
                            }
                        }
                    } else if (flag[0] == '-') {
                        // set -eux etc. — each char is a flag
                        for (int i = 1; i < flen; i++) {
                            char opt_ch[2] = {flag[i], '\0'};
                            MIR_op_t opt_val = bm_emit_string_literal(mt, opt_ch, 1);
                            bm_emit_call_void_2(mt, "bash_set_option", opt_val,
                                MIR_new_uint_op(mt->ctx, 1));
                        }
                    } else if (flag[0] == '+') {
                        // set +eux etc. — disable each flag
                        for (int i = 1; i < flen; i++) {
                            char opt_ch[2] = {flag[i], '\0'};
                            MIR_op_t opt_val = bm_emit_string_literal(mt, opt_ch, 1);
                            bm_emit_call_void_2(mt, "bash_set_option", opt_val,
                                MIR_new_uint_op(mt->ctx, 0));
                        }
                    }
                }
            }
            if (arg) arg = arg->next;
        }
        return bm_emit_int_literal(mt, 0);
    }

    // shopt -s extdebug / shopt -u extdebug
    if (cmd_len == 5 && memcmp(cmd_name, "shopt", 5) == 0) {
        BashAstNode* first = cmd->args;
        BashAstNode* second = first ? first->next : NULL;
        if (first && second && first->node_type == BASH_AST_NODE_WORD && second->node_type == BASH_AST_NODE_WORD) {
            BashWordNode* op = (BashWordNode*)first;
            BashWordNode* opt = (BashWordNode*)second;
            if (op->text && opt->text && opt->text->len == 8 && memcmp(opt->text->chars, "extdebug", 8) == 0) {
                bool enable = (op->text->len == 2 && memcmp(op->text->chars, "-s", 2) == 0);
                bool disable = (op->text->len == 2 && memcmp(op->text->chars, "-u", 2) == 0);
                if (enable || disable) {
                    MIR_op_t opt_val = bm_emit_string_literal(mt, "extdebug", 8);
                    bm_emit_call_void_2(mt, "bash_set_option", opt_val,
                        MIR_new_int_op(mt->ctx, enable ? 1 : 0));
                    return bm_emit_int_literal(mt, 0);
                }
            }
        }
        return bm_emit_int_literal(mt, 0);
    }

    // let — evaluate arithmetic expressions
    if (cmd_len == 3 && memcmp(cmd_name, "let", 3) == 0) {
        int argc = cmd->arg_count;
        if (argc == 0) return bm_emit_int_literal(mt, 0);

        MIR_reg_t args_ptr = bm_new_temp(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_ALLOCA, MIR_new_reg_op(mt->ctx, args_ptr),
                         MIR_new_int_op(mt->ctx, argc * (int)sizeof(Item))));
        int i = 0;
        BashAstNode* arg = cmd->args;
        while (arg && i < argc) {
            MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                   args_ptr, 0, 1),
                    arg_val));
            arg = arg->next;
            i++;
        }
        MIR_reg_t result = bm_emit_call_2(mt, "bash_builtin_let",
            MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_new_int_op(mt->ctx, argc));
        return MIR_new_reg_op(mt->ctx, result);
    }

    // type — describe command type
    if (cmd_len == 4 && memcmp(cmd_name, "type", 4) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_type", cmd);
    }

    // command -v / -V / plain
    if (cmd_len == 7 && memcmp(cmd_name, "command", 7) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_command", cmd);
    }

    // pushd / popd / dirs
    if (cmd_len == 5 && memcmp(cmd_name, "pushd", 5) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_pushd", cmd);
    }
    if (cmd_len == 4 && memcmp(cmd_name, "popd", 4) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_popd", cmd);
    }
    if (cmd_len == 4 && memcmp(cmd_name, "dirs", 4) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_dirs", cmd);
    }

    // getopts optstring name [args...]
    // if extra args are $@/$*, remove them so we use positional params directly
    if (cmd_len == 7 && memcmp(cmd_name, "getopts", 7) == 0) {
        if (cmd->arg_count > 2) {
            // check if all extra args (beyond optstring and name) are $@/$*
            bool only_all_args = true;
            BashAstNode* arg = cmd->args;
            int arg_idx = 0;
            while (arg) {
                if (arg_idx >= 2) {
                    bool is_at_star = false;
                    if (arg->node_type == BASH_AST_NODE_SPECIAL_VARIABLE) {
                        BashSpecialVarNode* sv = (BashSpecialVarNode*)arg;
                        if (sv->special_id == BASH_SPECIAL_AT || sv->special_id == BASH_SPECIAL_STAR)
                            is_at_star = true;
                    }
                    // also handle "$@" wrapped in a quoted string
                    if (arg->node_type == BASH_AST_NODE_STRING) {
                        BashStringNode* sn = (BashStringNode*)arg;
                        if (sn->parts && !sn->parts->next &&
                            sn->parts->node_type == BASH_AST_NODE_SPECIAL_VARIABLE) {
                            BashSpecialVarNode* sv = (BashSpecialVarNode*)sn->parts;
                            if (sv->special_id == BASH_SPECIAL_AT || sv->special_id == BASH_SPECIAL_STAR)
                                is_at_star = true;
                        }
                    }
                    if (!is_at_star) { only_all_args = false; break; }
                }
                arg = arg->next;
                arg_idx++;
            }
            if (only_all_args) {
                // use only optstring + name args (2 args) → uses positional params
                // temporarily unlink the 3rd arg
                BashAstNode* second_arg = cmd->args ? cmd->args->next : NULL;
                BashAstNode* third_arg = second_arg ? second_arg->next : NULL;
                if (second_arg) second_arg->next = NULL;
                int saved_count = cmd->arg_count;
                cmd->arg_count = 2;
                MIR_op_t result = bm_emit_varargs_builtin(mt, "bash_builtin_getopts", cmd);
                // restore
                cmd->arg_count = saved_count;
                if (second_arg) second_arg->next = third_arg;
                return result;
            }
        }
        return bm_emit_varargs_builtin(mt, "bash_builtin_getopts", cmd);
    }

    // builtin keyword — just pass through to the actual builtin
    // e.g. "builtin echo hello" → treat as "echo hello"
    if (cmd_len == 7 && memcmp(cmd_name, "builtin", 7) == 0) {
        if (cmd->args && cmd->args->node_type == BASH_AST_NODE_WORD) {
            // rewrite: shift name to first arg, first arg becomes new command name
            BashCommandNode* inner_cmd = cmd;
            inner_cmd->name = inner_cmd->args;
            inner_cmd->args = inner_cmd->args->next;
            inner_cmd->name->next = NULL;
            inner_cmd->arg_count--;
            return bm_transpile_command(mt, inner_cmd);
        }
        return bm_emit_int_literal(mt, 0);
    }

    // export/readonly as a command (e.g., after prefix assignment: IFS=: export x)
    if ((cmd_len == 6 && memcmp(cmd_name, "export", 6) == 0) ||
        (cmd_len == 8 && memcmp(cmd_name, "readonly", 8) == 0)) {
        bool is_readonly = (cmd_len == 8);

        // handle prefix assignments — in posix mode they persist, otherwise temporary
        BashAstNode* pa_n = cmd->assignments;
        while (pa_n) {
            BashAssignmentNode* a = (BashAssignmentNode*)pa_n;
            if (a->name) {
                MIR_op_t nop = bm_emit_string_literal(mt, a->name->chars, a->name->len);
                MIR_op_t vop = a->value ? bm_transpile_node(mt, a->value)
                                        : bm_emit_string_literal(mt, "", 0);
                bm_emit_call_void_2(mt, "bash_set_var", nop, vop);
                // apply readonly/export to the assigned variable
                if (is_readonly) {
                    bm_emit_call_void_2(mt, "bash_declare_var", nop,
                        MIR_new_int_op(mt->ctx, BASH_ATTR_READONLY));
                } else {
                    bm_emit_call_void_1(mt, "bash_export_var", nop);
                }
            }
            pa_n = pa_n->next;
        }

        BashAstNode* arg = cmd->args;
        while (arg) {
            // skip flags like -p, -n, -f
            if (arg->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* w = (BashWordNode*)arg;
                if (w->text && w->text->len >= 2 && w->text->chars[0] == '-') {
                    arg = arg->next;
                    continue;
                }
            }
            if (arg->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* w = (BashWordNode*)arg;
                if (w->text) {
                    MIR_op_t name_op = bm_emit_string_literal(mt, w->text->chars, w->text->len);
                    if (is_readonly) {
                        bm_emit_call_void_2(mt, "bash_declare_var", name_op,
                            MIR_new_int_op(mt->ctx, BASH_ATTR_READONLY));
                    } else {
                        bm_emit_call_void_1(mt, "bash_export_var", name_op);
                    }
                }
            }
            arg = arg->next;
        }
        return bm_emit_int_literal(mt, 0);
    }

    // return builtin — set exit code and return
    if (cmd_len == 6 && memcmp(cmd_name, "return", 6) == 0) {
        MIR_op_t ret_val;
        if (cmd->args) {
            ret_val = bm_transpile_cmd_arg(mt, cmd->args);
        } else {
            ret_val = bm_emit_int_literal(mt, 0);
        }
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_ret_insn(mt->ctx, 1, ret_val));
        return ret_val;
    }

    // check for user-defined function call
    BashMirUserFunc key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%.*s", cmd_len, cmd_name);
    const BashMirUserFunc* uf = (const BashMirUserFunc*)hashmap_get(mt->user_funcs, &key);
    if (uf) {
        // build a function call with arguments
        int argc = cmd->arg_count;

        // check if any arg needs $@ splatting (dynamic arg count)
        bool needs_splat = false;
        {
            BashAstNode* a = cmd->args;
            while (a) {
                if (arg_is_at_splat(a)) { needs_splat = true; break; }
                a = a->next;
            }
        }

        MIR_reg_t args_ptr = bm_new_temp(mt);
        MIR_reg_t argc_reg = bm_new_temp(mt);

        if (needs_splat) {
            // use dynamic arg builder to handle $@ expansion
            bm_emit_call_void_0(mt, "bash_arg_builder_start");
            BashAstNode* arg = cmd->args;
            while (arg) {
                if (arg_is_at_splat(arg)) {
                    bm_emit_call_void_0(mt, "bash_arg_builder_push_at");
                } else {
                    MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                    bm_emit_call_void_1(mt, "bash_arg_builder_push", arg_val);
                }
                arg = arg->next;
            }
            MIR_reg_t ptr_r = bm_emit_call_0(mt, "bash_arg_builder_get_ptr");
            MIR_reg_t cnt_r = bm_emit_call_0(mt, "bash_arg_builder_get_count");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_reg_op(mt->ctx, ptr_r)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                             MIR_new_reg_op(mt->ctx, cnt_r)));
        } else if (argc > 0) {
            // static arg count: allocate args buffer on stack
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_ALLOCA, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_int_op(mt->ctx, argc * (int)sizeof(Item))));
            int i = 0;
            BashAstNode* arg = cmd->args;
            while (arg && i < argc) {
                MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                             MIR_new_int_op(mt->ctx, argc)));
        } else {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_uint_op(mt->ctx, 0)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, argc_reg),
                             MIR_new_int_op(mt->ctx, 0)));
        }

        // handle prefix assignments (e.g., IFS=: ff args)
        int n_pa = 0;
        MIR_reg_t pa_saved[8];
        String* pa_names[8];
        BashAstNode* pa_node = cmd->assignments;
        while (pa_node && n_pa < 8) {
            BashAssignmentNode* a = (BashAssignmentNode*)pa_node;
            if (a->name) {
                pa_names[n_pa] = a->name;
                MIR_op_t nop = bm_emit_string_literal(mt, a->name->chars, a->name->len);
                pa_saved[n_pa] = bm_emit_call_1(mt, "bash_get_var", nop);
                MIR_op_t vop = a->value ? bm_transpile_node(mt, a->value)
                                        : bm_emit_string_literal(mt, "", 0);
                bm_emit_call_void_2(mt, "bash_set_var", nop, vop);
                n_pa++;
            }
            pa_node = pa_node->next;
        }

        // call the user function: bash_uf_XXX(args_ptr, argc) -> Item
        char mir_name[140];
        snprintf(mir_name, sizeof(mir_name), "bash_uf_%.*s", cmd_len, cmd_name);

        // create proto + forward reference
        char proto_name[160];
        snprintf(proto_name, sizeof(proto_name), "p_%s_call_%d", mir_name, mt->label_counter++);
        MIR_type_t res_type = MIR_T_I64;
        MIR_var_t call_params[2] = {
            {MIR_T_I64, "a", 0},
            {MIR_T_I64, "b", 0}
        };
        MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, &res_type, 2, call_params);

        bm_emit_call_void_0(mt, "bash_push_call_frame");

        MIR_reg_t result = bm_new_temp(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_call_insn(mt->ctx, 5,
                MIR_new_ref_op(mt->ctx, proto),
                MIR_new_ref_op(mt->ctx, uf->func_item),
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_reg_op(mt->ctx, argc_reg)));

            bm_emit_call_void_0(mt, "bash_pop_call_frame");

        // restore prefix assignments
        for (int i = 0; i < n_pa; i++) {
            MIR_op_t nop = bm_emit_string_literal(mt, pa_names[i]->chars, pa_names[i]->len);
            bm_emit_call_void_2(mt, "bash_set_var", nop, MIR_new_reg_op(mt->ctx, pa_saved[i]));
        }

        return MIR_new_reg_op(mt->ctx, result);
    }

    // handle prefix assignments for runtime/external commands (e.g., IFS=: cmd args)
    int n_pa_ext = 0;
    MIR_reg_t pa_ext_saved[8];
    String* pa_ext_names[8];
    {
        BashAstNode* pa_n = cmd->assignments;
        while (pa_n && n_pa_ext < 8) {
            BashAssignmentNode* a = (BashAssignmentNode*)pa_n;
            if (a->name) {
                pa_ext_names[n_pa_ext] = a->name;
                MIR_op_t nop = bm_emit_string_literal(mt, a->name->chars, a->name->len);
                pa_ext_saved[n_pa_ext] = bm_emit_call_1(mt, "bash_get_var", nop);
                MIR_op_t vop = a->value ? bm_transpile_node(mt, a->value)
                                        : bm_emit_string_literal(mt, "", 0);
                bm_emit_call_void_2(mt, "bash_set_var", nop, vop);
                n_pa_ext++;
            }
            pa_n = pa_n->next;
        }
    }

    // before trying external: check runtime function registry (for functions from sourced files)
    // use a temporary result register; if exit code 127 afterward, fall through to external
    MIR_reg_t rt_result_reg = bm_new_temp(mt);
    MIR_label_t try_external_label = bm_new_label(mt);
    MIR_label_t done_label_rt = bm_new_label(mt);

    {
        int argc = cmd->arg_count;
        MIR_op_t rt_name_val = bm_emit_string_literal(mt, cmd_name, cmd_len);

        // check if any arg needs $@ splatting
        bool needs_splat = false;
        {
            BashAstNode* a = cmd->args;
            while (a) {
                if (arg_is_at_splat(a)) { needs_splat = true; break; }
                a = a->next;
            }
        }

        MIR_reg_t rt_args_ptr = bm_new_temp(mt);
        MIR_reg_t rt_argc_reg = bm_new_temp(mt);

        if (needs_splat) {
            bm_emit_call_void_0(mt, "bash_arg_builder_start");
            BashAstNode* arg = cmd->args;
            while (arg) {
                if (arg_is_at_splat(arg)) {
                    bm_emit_call_void_0(mt, "bash_arg_builder_push_at");
                } else {
                    MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                    bm_emit_call_void_1(mt, "bash_arg_builder_push", arg_val);
                }
                arg = arg->next;
            }
            MIR_reg_t ptr_r = bm_emit_call_0(mt, "bash_arg_builder_get_ptr");
            MIR_reg_t cnt_r = bm_emit_call_0(mt, "bash_arg_builder_get_count");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_args_ptr),
                             MIR_new_reg_op(mt->ctx, ptr_r)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_argc_reg),
                             MIR_new_reg_op(mt->ctx, cnt_r)));
        } else if (argc > 0) {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_ALLOCA, MIR_new_reg_op(mt->ctx, rt_args_ptr),
                             MIR_new_int_op(mt->ctx, argc * (int)sizeof(Item))));
            int i = 0;
            BashAstNode* arg = cmd->args;
            while (arg && i < argc) {
                MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       rt_args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_argc_reg),
                             MIR_new_int_op(mt->ctx, argc)));
        } else {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_args_ptr),
                             MIR_new_uint_op(mt->ctx, 0)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_argc_reg),
                             MIR_new_int_op(mt->ctx, 0)));
        }

        MIR_reg_t rt_res = bm_emit_call_3(mt, "bash_call_rt_func",
            rt_name_val,
            MIR_new_reg_op(mt->ctx, rt_args_ptr),
            MIR_new_reg_op(mt->ctx, rt_argc_reg));
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_result_reg),
                         MIR_new_reg_op(mt->ctx, rt_res)));
        // if exit code == 127, function not found → try external
        MIR_reg_t rt_ec = bm_emit_call_0(mt, "bash_get_exit_code");
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BEQ,
                MIR_new_label_op(mt->ctx, try_external_label),
                MIR_new_reg_op(mt->ctx, rt_ec),
                MIR_new_uint_op(mt->ctx, i2it(127))));
        // runtime function ran — jump to done
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, done_label_rt)));
    }

    // label: try external command execution via posix_spawn
    MIR_append_insn(mt->ctx, mt->current_func_item, try_external_label);
    // fallback: external command execution via posix_spawn
    // build argv[] with command name as argv[0] + all arguments
    {
        // check if any arg needs $@ splatting
        bool needs_splat = false;
        {
            BashAstNode* a = cmd->args;
            while (a) {
                if (arg_is_at_splat(a)) { needs_splat = true; break; }
                a = a->next;
            }
        }

        MIR_reg_t ext_args_ptr = bm_new_temp(mt);
        MIR_reg_t ext_argc_reg = bm_new_temp(mt);
        MIR_op_t name_val = bm_emit_string_literal(mt, cmd_name, cmd_len);

        if (needs_splat) {
            bm_emit_call_void_0(mt, "bash_arg_builder_start");
            // argv[0] = command name
            bm_emit_call_void_1(mt, "bash_arg_builder_push", name_val);
            BashAstNode* arg = cmd->args;
            while (arg) {
                if (arg_is_at_splat(arg)) {
                    bm_emit_call_void_0(mt, "bash_arg_builder_push_at");
                } else {
                    MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                    bm_emit_call_void_1(mt, "bash_arg_builder_push", arg_val);
                }
                arg = arg->next;
            }
            MIR_reg_t ptr_r = bm_emit_call_0(mt, "bash_arg_builder_get_ptr");
            MIR_reg_t cnt_r = bm_emit_call_0(mt, "bash_arg_builder_get_count");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ext_args_ptr),
                             MIR_new_reg_op(mt->ctx, ptr_r)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ext_argc_reg),
                             MIR_new_reg_op(mt->ctx, cnt_r)));
        } else {
            int total_argc = 1 + cmd->arg_count;
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_ALLOCA, MIR_new_reg_op(mt->ctx, ext_args_ptr),
                             MIR_new_int_op(mt->ctx, total_argc * (int)sizeof(Item))));

            // argv[0] = command name
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, ext_args_ptr, 0, 1),
                    name_val));

            // argv[1..] = arguments
            int i = 1;
            BashAstNode* arg = cmd->args;
            while (arg && i < total_argc) {
                MIR_op_t arg_val = bm_transpile_cmd_arg(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       ext_args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ext_argc_reg),
                             MIR_new_int_op(mt->ctx, total_argc)));
        }

        MIR_reg_t ext_result = bm_emit_call_2(mt, "bash_exec_external",
            MIR_new_reg_op(mt->ctx, ext_args_ptr),
            MIR_new_reg_op(mt->ctx, ext_argc_reg));
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_result_reg),
                         MIR_new_reg_op(mt->ctx, ext_result)));
    }

    // done: rt_result_reg holds the final result
    MIR_append_insn(mt->ctx, mt->current_func_item, done_label_rt);

    // restore prefix assignments for runtime/external commands
    for (int i = 0; i < n_pa_ext; i++) {
        MIR_op_t nop = bm_emit_string_literal(mt, pa_ext_names[i]->chars, pa_ext_names[i]->len);
        bm_emit_call_void_2(mt, "bash_set_var", nop, MIR_new_reg_op(mt->ctx, pa_ext_saved[i]));
    }

    return MIR_new_reg_op(mt->ctx, rt_result_reg);
}

// ============================================================================
// Assignment
// ============================================================================

// check if an AST node contains a command substitution (shallow check for common patterns)
static bool node_has_command_sub(BashAstNode* node) {
    if (!node) return false;
    if (node->node_type == BASH_AST_NODE_COMMAND_SUB) return true;
    // string with parts: check each part
    if (node->node_type == BASH_AST_NODE_STRING) {
        BashStringNode* str = (BashStringNode*)node;
        for (BashAstNode* p = str->parts; p; p = p->next) {
            if (p->node_type == BASH_AST_NODE_COMMAND_SUB) return true;
        }
    }
    // concatenation: check each part
    if (node->node_type == BASH_AST_NODE_CONCATENATION) {
        BashConcatNode* cat = (BashConcatNode*)node;
        for (BashAstNode* p = cat->parts; p; p = p->next) {
            if (node_has_command_sub(p)) return true;
        }
    }
    return false;
}

static void bm_transpile_assignment(BashMirTranspiler* mt, BashAssignmentNode* node) {
    if (!node->name) return;

    // handle declare -p: print variable attributes and value
    if (node->declare_flags & BASH_ATTR_PRINT) {
        MIR_op_t name_op = bm_emit_string_literal(mt, node->name->chars, node->name->len);
        bm_emit_call_void_1(mt, "bash_declare_print_var", name_op);
        return;
    }

    // handle declare flags: declare -A creates assoc array, declare -i/-r/-l/-u sets attrs
    if (node->declare_flags) {
        // nameref (-n) is not fully supported; silently skip the assignment
        if (node->declare_flags & BASH_ATTR_NAMEREF) return;

        MIR_op_t name_op = bm_emit_string_literal(mt, node->name->chars, node->name->len);
        // use local-scope declare for local variables
        const char* declare_fn = node->is_local ? "bash_declare_local_var" : "bash_declare_var";

        // for declare-with-value, set non-readonly attrs first (for -i/-l/-u coercions),
        // then assign value, then add readonly after (so initial assignment isn't blocked)
        bool has_value = (node->value != NULL) || (node->index != NULL);
        bool has_readonly = (node->declare_flags & BASH_ATTR_READONLY) != 0;

        if (has_value) {
            int attrs_first = node->declare_flags & ~BASH_ATTR_READONLY;
            if (attrs_first) {
                bm_emit_call_void_2(mt, declare_fn, name_op,
                    MIR_new_int_op(mt->ctx, attrs_first));
            }
            // readonly will be added after value assignment (see below)
        } else {
            // no value: set all attrs including readonly
            bm_emit_call_void_2(mt, declare_fn, name_op,
                MIR_new_int_op(mt->ctx, node->declare_flags));
        }

        // declare -A map → create assoc array if no value assigned
        if ((node->declare_flags & BASH_ATTR_ASSOC_ARRAY) && !node->value) {
            MIR_reg_t assoc_reg = bm_emit_call_0(mt, "bash_assoc_new");
            bm_emit_set_var(mt, node->name->chars, MIR_new_reg_op(mt->ctx, assoc_reg));
            return;
        }
        // declare -a arr → create indexed array if no value assigned
        if ((node->declare_flags & BASH_ATTR_INDEXED_ARRAY) && !node->value) {
            MIR_reg_t arr_reg = bm_emit_call_0(mt, "bash_array_new");
            bm_emit_set_var(mt, node->name->chars, MIR_new_reg_op(mt->ctx, arr_reg));
            return;
        }
        // declare without value and non-array flags: just set attrs, done
        if (!node->value && !node->index) return;

        // fall through to value assignment, then add readonly after
    }

    MIR_op_t value;
    if (node->value) {
        value = bm_transpile_node(mt, node->value);
        // apply tilde expansion in assignment values (e.g., path=~/bin or path=/usr:~/bin)
        // but NOT when the value is double-quoted (e.g., path="~/bin") or an array literal
        if (node->value->node_type != BASH_AST_NODE_STRING &&
            node->value->node_type != BASH_AST_NODE_ARRAY_LITERAL) {
            MIR_reg_t tilde_res = bm_emit_call_1(mt, "bash_expand_tilde_assign", value);
            value = MIR_new_reg_op(mt->ctx, tilde_res);
        }
    } else if (node->is_export) {
        // export var (no value) — just mark as exported, don't overwrite existing value
        MIR_op_t name_op = bm_emit_string_literal(mt, node->name->chars, node->name->len);
        bm_emit_call_void_1(mt, "bash_export_var", name_op);
        return;
    } else {
        value = bm_emit_string_literal(mt, "", 0);
    }

    if (node->index) {
        // arr[idx]=val or map[key]=val (or arr[idx]+=val)
        // ensure variable holds an array (auto-create if needed)
        MIR_op_t name_op = bm_emit_string_literal(mt, node->name->chars, node->name->len);
        bool is_assoc = bm_is_assoc_var(mt, node->name->chars);
        MIR_reg_t arr_reg;
        if (is_assoc) {
            arr_reg = bm_emit_call_1(mt, "bash_ensure_assoc", name_op);
        } else {
            arr_reg = bm_emit_call_1(mt, "bash_ensure_array", name_op);
        }
        MIR_op_t idx_val = bm_transpile_node(mt, node->index);
        if (node->is_append && !is_assoc) {
            // arr[idx]+=val → single runtime call handles get+append+set
            bm_emit_call_void_4(mt, "bash_array_elem_append",
                MIR_new_reg_op(mt->ctx, arr_reg), idx_val, value, name_op);
        } else {
            const char* fn = is_assoc ? "bash_assoc_set" : "bash_array_set";
            bm_emit_call_3(mt, fn, MIR_new_reg_op(mt->ctx, arr_reg), idx_val, value);
        }
    } else if (node->is_append && node->value &&
               node->value->node_type == BASH_AST_NODE_ARRAY_LITERAL) {
        // arr+=(elem1 elem2 ...) → bash_array_append for each element
        BashArrayLiteralNode* arr_lit = (BashArrayLiteralNode*)node->value;
        MIR_op_t arr_val = bm_emit_get_var(mt, node->name->chars);
        BashAstNode* elem = arr_lit->elements;
        while (elem) {
            MIR_op_t elem_val = bm_transpile_node(mt, elem);
            bm_emit_call_2(mt, "bash_array_append", arr_val, elem_val);
            elem = elem->next;
        }
    } else if (node->is_append) {
        // var+=val → integer-aware append (arithmetic add if integer attr, else string concat)
        MIR_op_t old_val = bm_emit_get_var(mt, node->name->chars);
        MIR_op_t name_op = bm_emit_string_literal(mt, node->name->chars, node->name->len);
        MIR_reg_t result = bm_emit_call_3(mt, "bash_var_append", name_op, old_val, value);
        if (node->is_local)
            bm_emit_set_local_var(mt, node->name->chars, MIR_new_reg_op(mt->ctx, result));
        else
            bm_emit_set_var(mt, node->name->chars, MIR_new_reg_op(mt->ctx, result));
    } else {
        if (node->is_local)
            bm_emit_set_local_var(mt, node->name->chars, value);
        else
            bm_emit_set_var(mt, node->name->chars, value);
    }

    // if variable is declared with `export`, also call bash_export_var
    if (node->is_export) {
        MIR_op_t name_op = bm_emit_string_literal(mt, node->name->chars, node->name->len);
        bm_emit_call_void_1(mt, "bash_export_var", name_op);
    }

    // add readonly AFTER value assignment (so initial declare -r var=val works)
    if (node->declare_flags && (node->declare_flags & BASH_ATTR_READONLY)) {
        MIR_op_t name_op = bm_emit_string_literal(mt, node->name->chars, node->name->len);
        const char* readonly_fn = node->is_local ? "bash_declare_local_var" : "bash_declare_var";
        bm_emit_call_void_2(mt, readonly_fn, name_op,
            MIR_new_int_op(mt->ctx, BASH_ATTR_READONLY));
    }
}

// ============================================================================
// If statement
// ============================================================================

static void bm_transpile_if(BashMirTranspiler* mt, BashIfNode* node) {
    MIR_label_t else_label = bm_new_label(mt);
    MIR_label_t end_label = bm_new_label(mt);
    // separate label for "no branch taken" path to reset exit code
    bool need_no_branch = (!node->else_body && !node->elif_clauses);
    MIR_label_t no_branch_label = need_no_branch ? bm_new_label(mt) : NULL;

    // suppress errexit in condition
    bm_emit_call_void_0(mt, "bash_errexit_push");
    bm_transpile_statement(mt, node->condition);
    bm_emit_call_void_0(mt, "bash_errexit_pop");

    // check exit code
    MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");

    // branch: if exit code != 0, go to else/elif/no_branch
    MIR_label_t false_target;
    if (node->elif_clauses)
        false_target = bm_new_label(mt);  // first elif
    else if (node->else_body)
        false_target = else_label;
    else
        false_target = no_branch_label;   // no branch → reset exit code

    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_BNE,
                     MIR_new_label_op(mt->ctx, false_target),
                     MIR_new_reg_op(mt->ctx, ec),
                     MIR_new_uint_op(mt->ctx, i2it(0))));

    // then body
    bm_transpile_statement(mt, node->then_body);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, end_label)));

    // elif clauses
    if (node->elif_clauses) {
        MIR_append_insn(mt->ctx, mt->current_func_item, false_target);

        BashAstNode* elif = node->elif_clauses;
        while (elif) {
            BashElifNode* elif_node = (BashElifNode*)elif;
            MIR_label_t next_elif;
            if (elif->next)
                next_elif = bm_new_label(mt);
            else if (node->else_body)
                next_elif = else_label;
            else
                next_elif = end_label;

            // suppress errexit in elif condition
            bm_emit_call_void_0(mt, "bash_errexit_push");
            bm_transpile_statement(mt, elif_node->condition);
            bm_emit_call_void_0(mt, "bash_errexit_pop");

            MIR_reg_t elif_ec = bm_emit_call_0(mt, "bash_get_exit_code");

            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BNE,
                             MIR_new_label_op(mt->ctx, next_elif),
                             MIR_new_reg_op(mt->ctx, elif_ec),
                             MIR_new_uint_op(mt->ctx, i2it(0))));

            bm_transpile_statement(mt, elif_node->body);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, end_label)));

            if (elif->next) {
                MIR_append_insn(mt->ctx, mt->current_func_item, next_elif);
            }
            elif = elif->next;
        }
    }

    // else body
    if (node->else_body) {
        MIR_append_insn(mt->ctx, mt->current_func_item, else_label);
        bm_transpile_statement(mt, node->else_body);
    }

    // no-branch path: reset exit code to 0 (condition's exit code shouldn't leak)
    if (need_no_branch) {
        MIR_append_insn(mt->ctx, mt->current_func_item, no_branch_label);
        bm_emit_call_void_1(mt, "bash_set_exit_code", MIR_new_int_op(mt->ctx, 0));
    }

    MIR_append_insn(mt->ctx, mt->current_func_item, end_label);
}

// ============================================================================
// For loop
// ============================================================================

static void bm_transpile_for(BashMirTranspiler* mt, BashForNode* node) {
    if (!node->variable || !node->words) return;

    MIR_label_t loop_start = bm_new_label(mt);
    MIR_label_t loop_end = bm_new_label(mt);
    MIR_label_t loop_continue = bm_new_label(mt);

    // save old loop labels
    MIR_label_t old_break = mt->loop_break_label;
    MIR_label_t old_continue = mt->loop_continue_label;
    mt->loop_break_label = loop_end;
    mt->loop_continue_label = loop_continue;
    mt->loop_depth++;

    // check if iterating over an array: single word that is ARRAY_ALL, ARRAY_KEYS, or string wrapping them
    BashAstNode* word = node->words;
    String* array_name = NULL;
    bool is_keys_iter = false;
    bool is_assoc_iter = false;
    bool is_at_iter = false;
    if (word && !word->next) {
        if (arg_is_at_splat(word)) {
            is_at_iter = true;
        } else if (word->node_type == BASH_AST_NODE_ARRAY_ALL) {
            array_name = ((BashArrayAllNode*)word)->name;
            is_assoc_iter = bm_is_assoc_var(mt, array_name->chars);
        } else if (word->node_type == BASH_AST_NODE_ARRAY_KEYS) {
            array_name = ((BashArrayKeysNode*)word)->name;
            is_keys_iter = true;
        } else if (word->node_type == BASH_AST_NODE_STRING) {
            BashStringNode* str = (BashStringNode*)word;
            if (str->parts && !str->parts->next) {
                if (str->parts->node_type == BASH_AST_NODE_ARRAY_ALL) {
                    array_name = ((BashArrayAllNode*)str->parts)->name;
                    is_assoc_iter = bm_is_assoc_var(mt, array_name->chars);
                } else if (str->parts->node_type == BASH_AST_NODE_ARRAY_KEYS) {
                    array_name = ((BashArrayKeysNode*)str->parts)->name;
                    is_keys_iter = true;
                }
            }
        }
    }

    if (array_name || is_at_iter) {
        // POSIX: for loop exit code is 0 if body never runs, else body's last exit code
        bm_emit_call_void_1(mt, "bash_set_exit_code",
            MIR_new_int_op(mt->ctx, 0));

        // for assoc arrays, first materialize values/keys as an indexed array
        MIR_op_t arr_val;
        bool is_special_array = false;
        if (is_at_iter) {
            // for var in "$@" → iterate over positional parameters
            MIR_reg_t r = bm_emit_call_0(mt, "bash_get_all_args");
            arr_val = MIR_new_reg_op(mt->ctx, r);
            is_special_array = true;
        } else if (is_keys_iter) {
            MIR_op_t map_val = bm_emit_get_var(mt, array_name->chars);
            MIR_reg_t keys_reg = bm_emit_call_1(mt, "bash_assoc_keys", map_val);
            arr_val = MIR_new_reg_op(mt->ctx, keys_reg);
        } else if (is_assoc_iter) {
            MIR_op_t map_val = bm_emit_get_var(mt, array_name->chars);
            MIR_reg_t vals_reg = bm_emit_call_1(mt, "bash_assoc_values", map_val);
            arr_val = MIR_new_reg_op(mt->ctx, vals_reg);
        } else if (array_name->len == 9 && memcmp(array_name->chars, "BASH_ARGV", 9) == 0) {
            MIR_reg_t r = bm_emit_call_0(mt, "bash_get_bash_argv_all");
            arr_val = MIR_new_reg_op(mt->ctx, r);
            is_special_array = true;
        } else if (array_name->len == 8 && memcmp(array_name->chars, "FUNCNAME", 8) == 0) {
            MIR_reg_t r = bm_emit_call_0(mt, "bash_get_funcname_all");
            arr_val = MIR_new_reg_op(mt->ctx, r);
            is_special_array = true;
        } else {
            arr_val = bm_emit_get_var(mt, array_name->chars);
        }

        // for assoc/keys/special, store materialized array in temp var
        const char* iter_arr_name = NULL;
        char temp_name[64];
        if (is_keys_iter || is_assoc_iter || is_special_array) {
            snprintf(temp_name, sizeof(temp_name), "__iter_arr_%d", mt->label_counter++);
            iter_arr_name = temp_name;
            bm_emit_set_var(mt, iter_arr_name, arr_val);
            arr_val = bm_emit_get_var(mt, iter_arr_name);
        }

        // runtime array iteration: for var in "${arr[@]}"

        // get raw count (int64, not Item)
        char count_proto_name[64];
        snprintf(count_proto_name, sizeof(count_proto_name), "p_array_count_%d", mt->label_counter++);
        MIR_type_t count_res_type = MIR_T_I64;
        MIR_var_t count_params[1] = {{MIR_T_I64, "a", 0}};
        MIR_item_t count_proto = MIR_new_proto_arr(mt->ctx, count_proto_name, 1, &count_res_type, 1, count_params);
        MIR_item_t count_import = MIR_new_import(mt->ctx, "bash_array_count");
        MIR_reg_t count_reg = bm_new_temp(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_call_insn(mt->ctx, 4,
                MIR_new_ref_op(mt->ctx, count_proto),
                MIR_new_ref_op(mt->ctx, count_import),
                MIR_new_reg_op(mt->ctx, count_reg),
                arr_val));

        MIR_reg_t idx_reg = bm_new_temp(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx_reg),
                         MIR_new_int_op(mt->ctx, 0)));

        // loop_start:
        MIR_append_insn(mt->ctx, mt->current_func_item, loop_start);

        // if idx >= count goto loop_end
        // bash_array_length returns Item (i2it), so compare as uint64
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BGE,
                         MIR_new_label_op(mt->ctx, loop_end),
                         MIR_new_reg_op(mt->ctx, idx_reg),
                         MIR_new_reg_op(mt->ctx, count_reg)));

        // debug trap: fires at for statement line before each iteration
        {
            TSPoint start = ts_node_start_point(node->base.node);
            bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, (int)start.row + 1));
            MIR_label_t skip_iter = bm_new_label(mt);
            MIR_reg_t dbg_res = bm_emit_call_0(mt, "bash_run_debug_trap");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BEQ,
                             MIR_new_label_op(mt->ctx, loop_continue),
                             MIR_new_reg_op(mt->ctx, dbg_res),
                             MIR_new_uint_op(mt->ctx, i2it(2))));
            (void)skip_iter;
        }

        // elem = bash_array_get(arr, i2it(idx))
        MIR_reg_t idx_item_reg = bm_emit_call_1(mt, "bash_int_to_item",
            MIR_new_reg_op(mt->ctx, idx_reg));
        // re-fetch arr each iteration (in case it changes)
        const char* refetch_name = iter_arr_name ? iter_arr_name : array_name->chars;
        MIR_op_t arr_val2 = bm_emit_get_var(mt, refetch_name);
        MIR_reg_t elem_reg = bm_emit_call_2(mt, "bash_array_get",
            arr_val2, MIR_new_reg_op(mt->ctx, idx_item_reg));
        bm_emit_set_var(mt, node->variable->chars, MIR_new_reg_op(mt->ctx, elem_reg));

        // body
        bm_transpile_statement(mt, node->body);

        // continue label
        MIR_append_insn(mt->ctx, mt->current_func_item, loop_continue);

        // idx++
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx_reg),
                         MIR_new_reg_op(mt->ctx, idx_reg),
                         MIR_new_int_op(mt->ctx, 1)));

        // goto loop_start
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, loop_start)));

        MIR_append_insn(mt->ctx, mt->current_func_item, loop_end);
    } else {
        // POSIX: for loop exit code is 0 if body never runs, else body's last exit code
        bm_emit_call_void_1(mt, "bash_set_exit_code",
            MIR_new_int_op(mt->ctx, 0));

        // compile-time unrolled iteration over word list
        // but if any word requires brace/glob expansion or IFS splitting
        // (which produces multiple words), build a runtime array first.
        word = node->words;
        bool needs_runtime_array = false;
        for (BashAstNode* w = word; w; w = w->next) {
            if (arg_is_at_splat(w) || arg_needs_ifs_split(w)) {
                needs_runtime_array = true;
                break;
            }
            if (w->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* wn = (BashWordNode*)w;
                if (wn->text) {
                    const char* ch = wn->text->chars;
                    int wlen = wn->text->len;
                    if (word_is_brace(ch, wlen) || word_has_glob(ch, wlen)) {
                        needs_runtime_array = true;
                        break;
                    }
                }
            }
        }

        if (needs_runtime_array) {
            // build a runtime array of all expanded words, then do array iteration
            char arr_var[64];
            snprintf(arr_var, sizeof(arr_var), "__for_words_%d", mt->label_counter++);

            MIR_reg_t new_arr_reg = bm_emit_call_0(mt, "bash_array_new");
            MIR_op_t arr_val = MIR_new_reg_op(mt->ctx, new_arr_reg);
            bm_emit_set_var(mt, arr_var, arr_val);

            for (BashAstNode* w = word; w; w = w->next) {
                MIR_op_t cur_arr = bm_emit_get_var(mt, arr_var);
                // handle $@ / "$@" — append each positional param individually
                if (arg_is_at_splat(w)) {
                    MIR_reg_t at_arr = bm_emit_call_0(mt, "bash_get_all_args");
                    bm_emit_call_2(mt, "bash_array_concat", cur_arr, MIR_new_reg_op(mt->ctx, at_arr));
                    continue;
                }
                bool is_expand_word = false;
                bool is_brace = false;
                if (w->node_type == BASH_AST_NODE_WORD) {
                    BashWordNode* wn = (BashWordNode*)w;
                    if (wn->text) {
                        const char* ch = wn->text->chars;
                        int wlen = wn->text->len;
                        if (word_is_brace(ch, wlen)) { is_expand_word = true; is_brace = true; }
                        else if (word_has_glob(ch, wlen)) { is_expand_word = true; }
                    }
                }
                if (is_expand_word) {
                    BashWordNode* wn = (BashWordNode*)w;
                    MIR_op_t pat_val = bm_emit_string_literal(mt, wn->text->chars, wn->text->len);
                    MIR_reg_t expanded_reg;
                    if (is_brace) {
                        expanded_reg = bm_emit_call_1(mt, "bash_expand_brace", pat_val);
                    } else {
                        expanded_reg = bm_emit_call_1(mt, "bash_glob_expand", pat_val);
                    }
                    bm_emit_call_2(mt, "bash_words_split_into", cur_arr,
                        MIR_new_reg_op(mt->ctx, expanded_reg));
                } else {
                    MIR_op_t wval = bm_transpile_cmd_arg(mt, w);
                    if (arg_needs_ifs_split(w)) {
                        MIR_reg_t new_arr = bm_emit_call_2(mt, "bash_ifs_split_into", cur_arr, wval);
                        (void)new_arr;
                    } else {
                        bm_emit_call_2(mt, "bash_array_append", cur_arr, wval);
                    }
                }
            }

            // now iterate the runtime array (same as the array_name=set path)
            MIR_op_t arr_val2 = bm_emit_get_var(mt, arr_var);

            char count_proto_name2[64];
            snprintf(count_proto_name2, sizeof(count_proto_name2), "p_array_count_%d", mt->label_counter++);
            MIR_type_t count_res_type2 = MIR_T_I64;
            MIR_var_t count_params2[1] = {{MIR_T_I64, "a", 0}};
            MIR_item_t count_proto2 = MIR_new_proto_arr(mt->ctx, count_proto_name2, 1, &count_res_type2, 1, count_params2);
            MIR_item_t count_import2 = MIR_new_import(mt->ctx, "bash_array_count");
            MIR_reg_t count_reg2 = bm_new_temp(mt);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_call_insn(mt->ctx, 4,
                    MIR_new_ref_op(mt->ctx, count_proto2),
                    MIR_new_ref_op(mt->ctx, count_import2),
                    MIR_new_reg_op(mt->ctx, count_reg2),
                    arr_val2));

            MIR_reg_t idx_reg2 = bm_new_temp(mt);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx_reg2),
                             MIR_new_int_op(mt->ctx, 0)));

            MIR_append_insn(mt->ctx, mt->current_func_item, loop_start);

            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BGE,
                             MIR_new_label_op(mt->ctx, loop_end),
                             MIR_new_reg_op(mt->ctx, idx_reg2),
                             MIR_new_reg_op(mt->ctx, count_reg2)));

            // debug trap
            {
                TSPoint start = ts_node_start_point(node->base.node);
                bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, (int)start.row + 1));
                bm_emit_call_0(mt, "bash_run_debug_trap");
            }

            MIR_reg_t idx_item_reg2 = bm_emit_call_1(mt, "bash_int_to_item",
                MIR_new_reg_op(mt->ctx, idx_reg2));
            MIR_op_t arr_val3 = bm_emit_get_var(mt, arr_var);
            MIR_reg_t elem_reg2 = bm_emit_call_2(mt, "bash_array_get",
                arr_val3, MIR_new_reg_op(mt->ctx, idx_item_reg2));
            bm_emit_set_var(mt, node->variable->chars, MIR_new_reg_op(mt->ctx, elem_reg2));

            bm_transpile_statement(mt, node->body);

            MIR_append_insn(mt->ctx, mt->current_func_item, loop_continue);

            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx_reg2),
                             MIR_new_reg_op(mt->ctx, idx_reg2),
                             MIR_new_int_op(mt->ctx, 1)));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, loop_start)));

        } else {
        word = node->words;
        while (word) {
            // debug trap: fires at for statement line before each iteration
            {
                TSPoint start = ts_node_start_point(node->base.node);
                bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, (int)start.row + 1));
                MIR_label_t iter_skip = bm_new_label(mt);
                MIR_reg_t dbg_res = bm_emit_call_0(mt, "bash_run_debug_trap");
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_BEQ,
                                 MIR_new_label_op(mt->ctx, loop_end),
                                 MIR_new_reg_op(mt->ctx, dbg_res),
                                 MIR_new_uint_op(mt->ctx, i2it(2))));
                (void)iter_skip;
            }

            MIR_op_t word_val = bm_transpile_node(mt, word);
            bm_emit_set_var(mt, node->variable->chars, word_val);

            MIR_label_t iter_continue = bm_new_label(mt);
            mt->loop_continue_label = iter_continue;

            bm_transpile_statement(mt, node->body);

            MIR_append_insn(mt->ctx, mt->current_func_item, iter_continue);

            word = word->next;
        }
        } // end needs_runtime_array else

        MIR_append_insn(mt->ctx, mt->current_func_item, loop_end);
    }

    // restore old loop labels
    mt->loop_break_label = old_break;
    mt->loop_continue_label = old_continue;
    mt->loop_depth--;
}

static void bm_transpile_for_arith(BashMirTranspiler* mt, BashForArithNode* node) {
    MIR_label_t loop_start = bm_new_label(mt);
    MIR_label_t loop_end = bm_new_label(mt);
    MIR_label_t loop_continue = bm_new_label(mt);

    MIR_label_t old_break = mt->loop_break_label;
    MIR_label_t old_continue = mt->loop_continue_label;
    mt->loop_break_label = loop_end;
    mt->loop_continue_label = loop_continue;
    mt->loop_depth++;

    // line number of the for statement for debug preludes
    int for_line = (int)ts_node_start_point(node->base.node).row + 1;

    // debug prelude before init (for the for statement itself)
    bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, for_line));
    bm_emit_call_0(mt, "bash_run_debug_trap");

    // POSIX: for loop exit code is 0 if body never runs, else body's last exit code
    bm_emit_call_void_1(mt, "bash_set_exit_code",
        MIR_new_int_op(mt->ctx, 0));

    // init
    if (node->init) bm_transpile_arith(mt, node->init);

    // loop start
    MIR_append_insn(mt->ctx, mt->current_func_item, loop_start);

    // condition — emit debug prelude for condition check
    if (node->condition) {
        bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, for_line));
        bm_emit_call_0(mt, "bash_run_debug_trap");

        MIR_op_t cond = bm_transpile_arith(mt, node->condition);
        // if condition is 0, exit loop
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BEQ,
                         MIR_new_label_op(mt->ctx, loop_end),
                         cond,
                         MIR_new_uint_op(mt->ctx, i2it(0))));
    }

    // body
    bm_transpile_statement(mt, node->body);

    // continue point
    MIR_append_insn(mt->ctx, mt->current_func_item, loop_continue);

    // step — emit debug prelude for step expression
    if (node->step) {
        bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, for_line));
        bm_emit_call_0(mt, "bash_run_debug_trap");
        bm_transpile_arith(mt, node->step);
    }

    // jump back
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, loop_start)));

    MIR_append_insn(mt->ctx, mt->current_func_item, loop_end);

    mt->loop_break_label = old_break;
    mt->loop_continue_label = old_continue;
    mt->loop_depth--;
}

// ============================================================================
// While loop
// ============================================================================

static void bm_transpile_while(BashMirTranspiler* mt, BashWhileNode* node) {
    MIR_label_t loop_start = bm_new_label(mt);
    MIR_label_t loop_end = bm_new_label(mt);
    MIR_label_t loop_continue = bm_new_label(mt);
    MIR_label_t condition_exit = bm_new_label(mt);

    MIR_label_t old_break = mt->loop_break_label;
    MIR_label_t old_continue = mt->loop_continue_label;
    mt->loop_break_label = loop_end;
    mt->loop_continue_label = loop_continue;
    mt->loop_depth++;

    bool is_until = (node->base.node_type == BASH_AST_NODE_UNTIL);

    // POSIX: while/until exit code is body's last command, or 0 if body never runs
    MIR_reg_t body_ec = bm_new_temp(mt);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, body_ec),
                     MIR_new_int_op(mt->ctx, 0)));

    // loop start
    MIR_append_insn(mt->ctx, mt->current_func_item, loop_start);

    // check for pending signal traps at each iteration
    bm_emit_call_void_0(mt, "bash_trap_check");

    // condition (suppress errexit)
    bm_emit_call_void_0(mt, "bash_errexit_push");
    bm_transpile_statement(mt, node->condition);
    bm_emit_call_void_0(mt, "bash_errexit_pop");

    MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");

    if (is_until) {
        // until: exit when condition succeeds (exit code 0)
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BEQ,
                         MIR_new_label_op(mt->ctx, condition_exit),
                         MIR_new_reg_op(mt->ctx, ec),
                         MIR_new_uint_op(mt->ctx, i2it(0))));
    } else {
        // while: exit when condition fails (exit code != 0)
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BNE,
                         MIR_new_label_op(mt->ctx, condition_exit),
                         MIR_new_reg_op(mt->ctx, ec),
                         MIR_new_uint_op(mt->ctx, i2it(0))));
    }

    // body
    bm_transpile_statement(mt, node->body);

    // save body's exit code for when condition terminates the loop
    MIR_reg_t body_last_ec = bm_emit_call_0(mt, "bash_get_exit_code");
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, body_ec),
                     MIR_new_reg_op(mt->ctx, body_last_ec)));

    // continue point
    MIR_append_insn(mt->ctx, mt->current_func_item, loop_continue);

    // jump back
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, loop_start)));

    // condition_exit: restore body's exit code when condition terminates loop
    MIR_append_insn(mt->ctx, mt->current_func_item, condition_exit);
    bm_emit_call_void_1(mt, "bash_set_exit_code",
        MIR_new_reg_op(mt->ctx, body_ec));

    MIR_append_insn(mt->ctx, mt->current_func_item, loop_end);

    mt->loop_break_label = old_break;
    mt->loop_continue_label = old_continue;
    mt->loop_depth--;
}

// ============================================================================
// Case statement
// ============================================================================

static void bm_transpile_case(BashMirTranspiler* mt, BashCaseNode* node) {
    if (!node->word) return;

    MIR_op_t value = bm_transpile_node(mt, node->word);
    MIR_label_t end_label = bm_new_label(mt);

    // first pass: create one label per case item (for fallthrough targets)
    // count items
    int item_count = 0;
    { BashAstNode* it = node->items; while (it) { item_count++; it = it->next; } }

    // allocate body labels (one per item) for ;& fallthrough
    MIR_label_t* body_labels = (MIR_label_t*)alloca(item_count * sizeof(MIR_label_t));
    MIR_label_t* next_labels = (MIR_label_t*)alloca(item_count * sizeof(MIR_label_t));
    for (int k = 0; k < item_count; k++) {
        body_labels[k] = bm_new_label(mt);
        next_labels[k] = bm_new_label(mt);
    }

    BashAstNode* item = node->items;
    int idx = 0;
    while (item) {
        BashCaseItemNode* case_item = (BashCaseItemNode*)item;

        // check each pattern
        BashAstNode* pattern = case_item->patterns;
        bool has_wildcard = false;

        while (pattern) {
            // only check for literal wildcard '*' if the pattern is a plain word
            if (pattern->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* pat_word = (BashWordNode*)pattern;
                if (pat_word->text && pat_word->text->len == 1 && pat_word->text->chars[0] == '*') {
                    has_wildcard = true;
                    pattern = pattern->next;
                    continue;
                }
            }

            MIR_op_t pat_val = bm_transpile_node(mt, pattern);
            // choose match function based on pattern type:
            // - quoted patterns ("..." or '...'): literal comparison
            // - word patterns (already backslash-processed): glob match with FNM_NOESCAPE
            // - variable/expansion patterns: glob match with backslash escapes
            const char* match_fn;
            if (pattern->node_type == BASH_AST_NODE_STRING
                || pattern->node_type == BASH_AST_NODE_RAW_STRING) {
                match_fn = "bash_str_eq";
            } else if (pattern->node_type == BASH_AST_NODE_WORD) {
                match_fn = "bash_test_str_eq_noescape";
            } else {
                match_fn = "bash_test_str_eq";
            }
            MIR_reg_t cmp_result = bm_emit_call_2(mt, match_fn, value, pat_val);

            // if match, jump to match body
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BNE,
                             MIR_new_label_op(mt->ctx, body_labels[idx]),
                             MIR_new_reg_op(mt->ctx, cmp_result),
                             MIR_new_uint_op(mt->ctx, b2it(false))));

            pattern = pattern->next;
        }

        if (!has_wildcard) {
            // no match — skip to next item
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, next_labels[idx])));
        }

        // match body
        MIR_append_insn(mt->ctx, mt->current_func_item, body_labels[idx]);
        {
            BashAstNode* body_stmt = case_item->body;
            while (body_stmt) {
                bm_transpile_statement(mt, body_stmt);
                body_stmt = body_stmt->next;
            }
        }

        // handle terminator
        if (case_item->terminator == 1) {
            // ;& — fall through to next item's body directly (skip pattern check)
            if (item->next) {
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, body_labels[idx + 1])));
            } else {
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, end_label)));
            }
        } else if (case_item->terminator == 2) {
            // ;;& — continue testing next patterns
            if (item->next) {
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, next_labels[idx])));
            } else {
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, end_label)));
            }
        } else {
            // ;; — done, jump to end
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, end_label)));
        }

        MIR_append_insn(mt->ctx, mt->current_func_item, next_labels[idx]);
        item = item->next;
        idx++;
    }

    MIR_append_insn(mt->ctx, mt->current_func_item, end_label);
}

// ============================================================================
// Function definition
// ============================================================================

static void bm_transpile_function_def(BashMirTranspiler* mt, BashFunctionDefNode* node) {
    if (!node->name) return;

    // if this function name already exists, use a versioned MIR name to avoid
    // "Repeated item declaration" — calls after this point will use the new version
    char func_name[160];
    BashMirUserFunc lookup_key;
    memset(&lookup_key, 0, sizeof(lookup_key));
    snprintf(lookup_key.name, sizeof(lookup_key.name), "%.*s", (int)node->name->len, node->name->chars);
    if (hashmap_get(mt->user_funcs, &lookup_key)) {
        snprintf(func_name, sizeof(func_name), "bash_uf_%.*s_r%d",
                 (int)node->name->len, node->name->chars, mt->label_counter++);
    } else {
        snprintf(func_name, sizeof(func_name), "bash_uf_%.*s", (int)node->name->len, node->name->chars);
    }

    // save current function context
    MIR_item_t saved_func_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    struct hashmap* saved_vars = mt->vars;
    int saved_var_count = mt->var_count;
    MIR_label_t saved_break = mt->loop_break_label;
    MIR_label_t saved_continue = mt->loop_continue_label;
    int saved_loop_depth = mt->loop_depth;

    // new variable scope for function
    mt->vars = hashmap_new(sizeof(BashMirVar), 16, 0, 0,
                           bash_var_hash, bash_var_cmp, NULL, NULL);
    mt->var_count = 0;
    mt->loop_break_label = NULL;
    mt->loop_continue_label = NULL;
    mt->loop_depth = 0;

    // create MIR function: func(args_ptr: i64, argc: i64) -> i64
    MIR_type_t func_ret = MIR_T_I64;
    MIR_var_t params[2] = {
        {MIR_T_I64, "args_ptr", 0},
        {MIR_T_I64, "argc", 0}
    };
    mt->current_func_item = MIR_new_func_arr(mt->ctx, func_name, 1, &func_ret, 2, params);
    mt->current_func = mt->current_func_item->u.func;

    // register in user function table BEFORE transpiling body (enables recursion)
    BashMirUserFunc entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%.*s", (int)node->name->len, node->name->chars);
    entry.func_item = mt->current_func_item;
    entry.ast_node = node;
    hashmap_set(mt->user_funcs, &entry);

    // emit: bash_set_positional(args_ptr, argc)
    MIR_reg_t args_reg = MIR_reg(mt->ctx, "args_ptr", mt->current_func);
    MIR_reg_t argc_reg = MIR_reg(mt->ctx, "argc", mt->current_func);
    bm_emit_call_void_2(mt, "bash_push_positional",
        MIR_new_reg_op(mt->ctx, args_reg), MIR_new_reg_op(mt->ctx, argc_reg));
    // push a new dynamic scope frame for local variables
    bm_emit_call_void_0(mt, "bash_scope_push");
    MIR_op_t func_name_val = bm_emit_string_literal(mt, node->name->chars, node->name->len);
    bm_emit_call_void_1(mt, "bash_push_funcname", func_name_val);

    // push function arguments onto BASH_ARGV stack (in reverse order, per bash spec)
    bm_emit_call_void_2(mt, "bash_push_argv_frame",
        MIR_new_reg_op(mt->ctx, args_reg), MIR_new_reg_op(mt->ctx, argc_reg));

    // function-entry debug trap: fire with LINENO = function def start line
    int func_start_line = (int)ts_node_start_point(node->base.node).row + 1;
    bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, func_start_line));
    bm_emit_call_0(mt, "bash_run_debug_trap");

    // transpile function body
    bm_transpile_statement(mt, node->body);

    // function-exit debug trap: fire with LINENO = function def start line
    bm_emit_call_void_1(mt, "bash_set_lineno", MIR_new_int_op(mt->ctx, func_start_line));
    bm_emit_call_0(mt, "bash_run_debug_trap");

    // pop scope frame and restore positional params
    bm_emit_call_void_0(mt, "bash_run_return_trap");
    bm_emit_call_void_0(mt, "bash_pop_bash_argv");
    bm_emit_call_void_0(mt, "bash_pop_funcname");
    bm_emit_call_void_0(mt, "bash_scope_pop");
    bm_emit_call_void_0(mt, "bash_pop_positional");

    // default return 0
    MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, ec)));
    MIR_finish_func(mt->ctx);

    log_debug("bash-mir: compiled function '%.*s'", (int)node->name->len, node->name->chars);

    // restore parent context
    hashmap_free(mt->vars);
    mt->vars = saved_vars;
    mt->var_count = saved_var_count;
    mt->current_func_item = saved_func_item;
    mt->current_func = saved_func;
    mt->loop_break_label = saved_break;
    mt->loop_continue_label = saved_continue;
    mt->loop_depth = saved_loop_depth;
}

// ============================================================================
// Arithmetic expression transpilation
// ============================================================================

static MIR_op_t bm_transpile_arith(BashMirTranspiler* mt, BashAstNode* node) {
    if (!node) return bm_emit_int_literal(mt, 0);

    switch (node->node_type) {
    case BASH_AST_NODE_ARITHMETIC_EXPR: {
        BashArithExprNode* arith = (BashArithExprNode*)node;
        return bm_transpile_arith(mt, arith->expression);
    }
    case BASH_AST_NODE_ARITH_NUMBER: {
        BashArithNumberNode* num = (BashArithNumberNode*)node;
        return bm_emit_int_literal(mt, num->value);
    }
    case BASH_AST_NODE_ARITH_VARIABLE: {
        BashArithVariableNode* var = (BashArithVariableNode*)node;
        if (var->name) return bm_emit_get_var(mt, var->name->chars);
        return bm_emit_int_literal(mt, 0);
    }
    case BASH_AST_NODE_ARITH_BINARY: {
        BashArithBinaryNode* bin = (BashArithBinaryNode*)node;

        // short-circuit for logical && and ||
        if (bin->op == BASH_OP_LOGICAL_AND || bin->op == BASH_OP_LOGICAL_OR) {
            // result register
            MIR_reg_t result_reg = bm_new_temp(mt);
            MIR_label_t skip_label = bm_new_label(mt);
            MIR_label_t end_label = bm_new_label(mt);

            // evaluate left
            MIR_op_t left = bm_transpile_arith(mt, bin->left);
            MIR_reg_t left_int = bm_emit_call_1(mt, "bash_to_int", left);
            // zero value for comparison
            MIR_op_t zero = MIR_new_uint_op(mt->ctx, i2it(0));

            if (bin->op == BASH_OP_LOGICAL_AND) {
                // &&: if left == 0, result is 0 (skip right)
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result_reg), zero));
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, end_label),
                                 MIR_new_reg_op(mt->ctx, left_int), zero));
            } else {
                // ||: if left != 0, result is 1 (skip right)
                MIR_op_t one = MIR_new_uint_op(mt->ctx, i2it(1));
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result_reg), one));
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, end_label),
                                 MIR_new_reg_op(mt->ctx, left_int), zero));
            }

            // evaluate right
            MIR_op_t right = bm_transpile_arith(mt, bin->right);
            MIR_reg_t right_int = bm_emit_call_1(mt, "bash_to_int", right);
            // result = (right != 0) ? 1 : 0
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result_reg), zero));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, end_label),
                             MIR_new_reg_op(mt->ctx, right_int), zero));
            MIR_op_t one = MIR_new_uint_op(mt->ctx, i2it(1));
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result_reg), one));

            MIR_append_insn(mt->ctx, mt->current_func_item, end_label);
            return MIR_new_reg_op(mt->ctx, result_reg);
        }

        MIR_op_t left = bm_transpile_arith(mt, bin->left);
        MIR_op_t right = bm_transpile_arith(mt, bin->right);

        // map operator to runtime function
        const char* func_name = NULL;
        switch (bin->op) {
        case BASH_OP_ADD: func_name = "bash_add"; break;
        case BASH_OP_SUB: func_name = "bash_subtract"; break;
        case BASH_OP_MUL: func_name = "bash_multiply"; break;
        case BASH_OP_DIV: func_name = "bash_divide"; break;
        case BASH_OP_MOD: func_name = "bash_modulo"; break;
        case BASH_OP_POW: func_name = "bash_power"; break;
        case BASH_OP_LSHIFT: func_name = "bash_lshift"; break;
        case BASH_OP_RSHIFT: func_name = "bash_rshift"; break;
        case BASH_OP_BIT_AND: func_name = "bash_bit_and"; break;
        case BASH_OP_BIT_OR: func_name = "bash_bit_or"; break;
        case BASH_OP_BIT_XOR: func_name = "bash_bit_xor"; break;
        case BASH_OP_EQ: func_name = "bash_arith_eq"; break;
        case BASH_OP_NE: func_name = "bash_arith_ne"; break;
        case BASH_OP_LT: func_name = "bash_arith_lt"; break;
        case BASH_OP_LE: func_name = "bash_arith_le"; break;
        case BASH_OP_GT: func_name = "bash_arith_gt"; break;
        case BASH_OP_GE: func_name = "bash_arith_ge"; break;
        default: func_name = "bash_add"; break;
        }

        MIR_reg_t result = bm_emit_call_2(mt, func_name, left, right);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARITH_UNARY: {
        BashArithUnaryNode* unary = (BashArithUnaryNode*)node;
        MIR_op_t operand = bm_transpile_arith(mt, unary->operand);

        if (unary->op == BASH_OP_NEGATE || unary->op == BASH_OP_SUB) {
            MIR_reg_t result = bm_emit_call_1(mt, "bash_negate", operand);
            return MIR_new_reg_op(mt->ctx, result);
        }
        return operand;
    }
    case BASH_AST_NODE_ARITH_ASSIGN: {
        BashArithAssignNode* assign = (BashArithAssignNode*)node;
        if (!assign->name) return bm_emit_int_literal(mt, 0);

        if (assign->op == BASH_OP_INC || assign->op == BASH_OP_DEC) {
            // i++ or i--: get current value, add/sub 1, store back
            MIR_op_t cur = bm_emit_get_var(mt, assign->name->chars);
            MIR_op_t one = bm_emit_int_literal(mt, 1);
            const char* fn = (assign->op == BASH_OP_INC) ? "bash_add" : "bash_subtract";
            MIR_reg_t result = bm_emit_call_2(mt, fn, cur, one);
            bm_emit_set_var(mt, assign->name->chars, MIR_new_reg_op(mt->ctx, result));
            return cur; // postfix: return old value
        }

        // i=0, i+=1, etc.
        if (assign->op == BASH_OP_ASSIGN) {
            MIR_op_t val = assign->value ? bm_transpile_arith(mt, assign->value)
                                         : bm_emit_int_literal(mt, 0);
            bm_emit_set_var(mt, assign->name->chars, val);
            return val;
        }

        // compound assignment: +=, -=, *=, /=, %=
        MIR_op_t cur = bm_emit_get_var(mt, assign->name->chars);
        MIR_op_t rhs = assign->value ? bm_transpile_arith(mt, assign->value)
                                     : bm_emit_int_literal(mt, 0);
        const char* fn = "bash_add";
        switch (assign->op) {
        case BASH_OP_ADD_ASSIGN: fn = "bash_add"; break;
        case BASH_OP_SUB_ASSIGN: fn = "bash_subtract"; break;
        case BASH_OP_MUL_ASSIGN: fn = "bash_multiply"; break;
        case BASH_OP_DIV_ASSIGN: fn = "bash_divide"; break;
        case BASH_OP_MOD_ASSIGN: fn = "bash_modulo"; break;
        default: break;
        }
        MIR_reg_t result = bm_emit_call_2(mt, fn, cur, rhs);
        bm_emit_set_var(mt, assign->name->chars, MIR_new_reg_op(mt->ctx, result));
        return MIR_new_reg_op(mt->ctx, result);
    }
    default:
        return bm_transpile_node(mt, node);
    }
}

// ============================================================================
// Script sourcing — bash_source_file (runtime-callable from JIT)
// ============================================================================

static Runtime* bash_source_runtime = NULL;

// forward declarations for preprocessor passes
static const char* preprocess_dollar_dollar(const char* src, size_t src_len, StrBuf** out_buf);
static const char* preprocess_bash_source(const char* src, size_t src_len, StrBuf** out_buf);

// list of MIR contexts from sourced files — kept alive for function pointer validity
#define BASH_SOURCE_CTX_MAX 32
static MIR_context_t bash_source_ctx_list[BASH_SOURCE_CTX_MAX];
static int bash_source_ctx_count = 0;

extern "C" Item bash_source_file(Item filename) {
    String* s = it2s(filename);
    if (!s || s->len == 0) {
        log_error("bash: source: missing filename");
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    char path_buf[1024];
    snprintf(path_buf, sizeof(path_buf), "%.*s", s->len, s->chars);

    char* source_text = read_text_file(path_buf);
    if (!source_text) {
        log_error("bash: source: cannot open '%s'", path_buf);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    if (!bash_source_runtime) {
        log_error("bash: source: no runtime context");
        free(source_text);
        return (Item){.item = i2it(1)};
    }

    log_debug("bash: sourcing file '%s'", path_buf);

    BashTranspiler* tp = bash_transpiler_create(bash_source_runtime);
    if (!tp) {
        log_error("bash: source: failed to create transpiler");
        free(source_text);
        return (Item){.item = i2it(1)};
    }

    tp->source = source_text;
    tp->source_length = strlen(source_text);

    // preprocess: fix $$ at end-of-word and multi-assignment lines
    StrBuf* dd_buf = NULL;
    const char* dd_src = preprocess_dollar_dollar(tp->source, tp->source_length, &dd_buf);
    if (dd_src != tp->source) {
        tp->source = dd_src;
        tp->source_length = strlen(dd_src);
    }
    StrBuf* preproc_buf = NULL;
    const char* pp_src = preprocess_bash_source(tp->source, tp->source_length, &preproc_buf);
    if (pp_src != tp->source) {
        tp->source = pp_src;
        tp->source_length = strlen(pp_src);
    }

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_bash());
    TSTree* tree = ts_parser_parse_string(parser, NULL, tp->source, (uint32_t)tp->source_length);
    if (!tree) {
        log_error("bash: source: parse failed for '%s'", path_buf);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        free(source_text);
        return (Item){.item = i2it(1)};
    }

    TSNode root = ts_tree_root_node(tree);
    BashAstNode* ast = build_bash_ast(tp, root);
    if (!ast) {
        log_error("bash: source: AST build failed for '%s'", path_buf);
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        free(source_text);
        return (Item){.item = i2it(1)};
    }

    MIR_context_t ctx = jit_init(bash_source_runtime->optimize_level);
    if (!ctx) {
        log_error("bash: source: MIR init failed");
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        free(source_text);
        return (Item){.item = i2it(1)};
    }

    BashMirTranspiler* mt = (BashMirTranspiler*)malloc(sizeof(BashMirTranspiler));
    memset(mt, 0, sizeof(BashMirTranspiler));
    mt->tp = tp;
    mt->ctx = ctx;
    mt->vars = hashmap_new(sizeof(BashMirVar), 64, 0, 0,
                           bash_var_hash, bash_var_cmp, NULL, NULL);
    mt->import_cache = hashmap_new(sizeof(BashMirImportEntry), 64, 0, 0,
                                   bm_import_hash, bm_import_cmp, NULL, NULL);
    mt->user_funcs = hashmap_new(sizeof(BashMirUserFunc), 16, 0, 0,
                                 bm_user_func_hash, bm_user_func_cmp, NULL, NULL);
    mt->var_count = 0;
    mt->label_counter = 0;
    mt->loop_depth = 0;

    mt->module = MIR_new_module(ctx, "bash_source");

    // Pass 1: function definitions
    BashProgramNode* program = (BashProgramNode*)ast;
    BashAstNode* stmt = program->body;
    while (stmt) {
        if (stmt->node_type == BASH_AST_NODE_FUNCTION_DEF) {
            bm_transpile_function_def(mt, (BashFunctionDefNode*)stmt);
        }
        stmt = stmt->next;
    }

    // Pass 2: main body
    MIR_type_t ret_type = MIR_T_I64;
    mt->current_func_item = MIR_new_func(ctx, "bash_source_entry", 1, &ret_type, 0);
    mt->current_func = mt->current_func_item->u.func;
    mt->result_reg = bm_new_temp(mt);

    stmt = program->body;
    while (stmt) {
        bm_transpile_statement(mt, stmt);
        if (stmt->node_type != BASH_AST_NODE_LIST) {
            bm_emit_errexit_check(mt);
        }
        stmt = stmt->next;
    }

    MIR_reg_t final_ec = bm_emit_call_0(mt, "bash_get_exit_code");
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, final_ec)));

    MIR_finish_func(ctx);
    MIR_finish_module(ctx);
    MIR_load_module(ctx, mt->module);
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    typedef Item (*source_entry_t)(void);
    source_entry_t entry = (source_entry_t)find_func(ctx, "bash_source_entry");

    Item result = {.item = i2it(0)};
    if (entry) {
        bash_push_call_frame();
        // push source BEFORE funcname so that BASH_SOURCE for "source" entry = sourced file
        String* src_path = heap_create_name(path_buf, (int)strlen(path_buf));
        bash_push_source((Item){.item = s2it(src_path)});
        String* source_name = heap_create_name("source", 6);
        bash_push_funcname((Item){.item = s2it(source_name)});
        result = entry();
        // restore lineno to the call site, pop source context, then run traps in caller context
        bash_restore_call_frame_lineno();
        bash_pop_funcname();
        bash_pop_source();
        // source exit debug trap only fires with functrace on (set -T)
        if (bash_is_functrace()) bash_run_debug_trap();
        bash_run_return_trap();
        bash_pop_call_frame();
    } else {
        log_error("bash: source: failed to find bash_source_entry");
    }

    // register user functions so caller can dispatch them by name at runtime
    // iterate all user funcs compiled in this module and register their JIT pointers
    size_t iter = 0;
    void* item_ptr;
    while (hashmap_iter(mt->user_funcs, &iter, &item_ptr)) {
        BashMirUserFunc* uf = (BashMirUserFunc*)item_ptr;
        char full_name[160];
        snprintf(full_name, sizeof(full_name), "bash_uf_%s", uf->name);
        BashRtFuncPtr func_ptr = (BashRtFuncPtr)find_func(ctx, full_name);
        if (func_ptr) {
            bash_register_rt_func(uf->name, func_ptr);
            log_debug("bash: source: registered function '%s'", uf->name);
        }
    }

    // save ctx — don't free yet (function pointers remain valid until cleanup)
    if (bash_source_ctx_count < BASH_SOURCE_CTX_MAX) {
        bash_source_ctx_list[bash_source_ctx_count++] = ctx;
    } else {
        // too many sourced files — must free (function pointers become invalid)
        MIR_finish(ctx);
    }

    // cleanup everything except ctx
    hashmap_free(mt->vars);
    hashmap_free(mt->import_cache);
    hashmap_free(mt->user_funcs);
    free(mt);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    bash_transpiler_destroy(tp);
    if (preproc_buf) strbuf_free(preproc_buf);
    if (dd_buf) strbuf_free(dd_buf);
    free(source_text);

    return result;
}

// ============================================================================
// bash_eval_string — evaluate a bash code string in the current runtime scope
// (runtime-callable from JIT; also called from bash_trap_run_exit / bash_trap_check)
// ============================================================================

static int bash_eval_counter = 0;

extern "C" Item bash_eval_string(Item code) {
    String* s = it2s(code);
    if (!s || s->len == 0) {
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    if (!bash_source_runtime) {
        log_error("bash: eval: no runtime context");
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    // make a null-terminated copy of the code
    char* source_text = (char*)malloc(s->len + 1);
    memcpy(source_text, s->chars, s->len);
    source_text[s->len] = '\0';

    log_debug("bash: eval: executing '%.*s'", s->len < 40 ? s->len : 40, s->chars);

    BashTranspiler* tp = bash_transpiler_create(bash_source_runtime);
    if (!tp) {
        log_error("bash: eval: failed to create transpiler");
        free(source_text);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    tp->source = source_text;
    tp->source_length = (int)s->len;

    // preprocess: fix $$ at end-of-word and multi-assignment lines
    StrBuf* dd_buf = NULL;
    const char* dd_src = preprocess_dollar_dollar(tp->source, tp->source_length, &dd_buf);
    if (dd_src != tp->source) {
        tp->source = dd_src;
        tp->source_length = strlen(dd_src);
    }
    StrBuf* preproc_buf = NULL;
    const char* pp_src = preprocess_bash_source(tp->source, tp->source_length, &preproc_buf);
    if (pp_src != tp->source) {
        tp->source = pp_src;
        tp->source_length = strlen(pp_src);
    }

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_bash());
    TSTree* tree = ts_parser_parse_string(parser, NULL, tp->source, (uint32_t)tp->source_length);
    if (!tree) {
        log_error("bash: eval: parse failed");
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        free(source_text);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    TSNode root = ts_tree_root_node(tree);
    BashAstNode* ast = build_bash_ast(tp, root);
    if (!ast) {
        log_error("bash: eval: AST build failed");
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        free(source_text);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    MIR_context_t ctx = jit_init(bash_source_runtime->optimize_level);
    if (!ctx) {
        log_error("bash: eval: MIR init failed");
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        free(source_text);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    BashMirTranspiler* mt = (BashMirTranspiler*)malloc(sizeof(BashMirTranspiler));
    memset(mt, 0, sizeof(BashMirTranspiler));
    mt->tp = tp;
    mt->ctx = ctx;
    mt->vars = hashmap_new(sizeof(BashMirVar), 32, 0, 0,
                           bash_var_hash, bash_var_cmp, NULL, NULL);
    mt->import_cache = hashmap_new(sizeof(BashMirImportEntry), 32, 0, 0,
                                   bm_import_hash, bm_import_cmp, NULL, NULL);
    mt->user_funcs = hashmap_new(sizeof(BashMirUserFunc), 8, 0, 0,
                                 bm_user_func_hash, bm_user_func_cmp, NULL, NULL);
    mt->var_count = 0;
    mt->label_counter = 0;
    mt->loop_depth = 0;

    // give each eval module a unique name
    char module_name[48];
    snprintf(module_name, sizeof(module_name), "bash_eval_%d", bash_eval_counter++);
    mt->module = MIR_new_module(ctx, module_name);

    // Pass 1: function defs
    BashProgramNode* program = (BashProgramNode*)ast;
    BashAstNode* stmt = program->body;
    while (stmt) {
        if (stmt->node_type == BASH_AST_NODE_FUNCTION_DEF) {
            bm_transpile_function_def(mt, (BashFunctionDefNode*)stmt);
        }
        stmt = stmt->next;
    }

    // Pass 2: main body
    char func_name[48];
    snprintf(func_name, sizeof(func_name), "bash_eval_entry_%d", bash_eval_counter - 1);
    MIR_type_t ret_type = MIR_T_I64;
    mt->current_func_item = MIR_new_func(ctx, func_name, 1, &ret_type, 0);
    mt->current_func = mt->current_func_item->u.func;
    mt->result_reg = bm_new_temp(mt);

    stmt = program->body;
    while (stmt) {
        bm_transpile_statement(mt, stmt);
        if (stmt->node_type != BASH_AST_NODE_LIST) {
            bm_emit_errexit_check(mt);
        }
        stmt = stmt->next;
    }

    MIR_reg_t final_ec = bm_emit_call_0(mt, "bash_get_exit_code");
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, final_ec)));

    MIR_finish_func(ctx);
    MIR_finish_module(ctx);
    MIR_load_module(ctx, mt->module);
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // register eval-local user functions before executing entry so trap/eval code
    // can call functions defined earlier in the same snippet
    size_t iter = 0;
    void* item_ptr;
    while (hashmap_iter(mt->user_funcs, &iter, &item_ptr)) {
        BashMirUserFunc* uf = (BashMirUserFunc*)item_ptr;
        char full_name[160];
        snprintf(full_name, sizeof(full_name), "bash_uf_%s", uf->name);
        BashRtFuncPtr func_ptr = (BashRtFuncPtr)find_func(ctx, full_name);
        if (func_ptr) {
            bash_register_rt_func(uf->name, func_ptr);
        }
    }

    typedef Item (*eval_entry_t)(void);
    eval_entry_t entry = (eval_entry_t)find_func(ctx, func_name);

    Item result = {.item = i2it(0)};
    if (entry) {
        result = entry();
    } else {
        log_error("bash: eval: failed to find entry function '%s'", func_name);
    }

    // only keep the MIR context alive if it registered user functions whose
    // pointers must remain valid.  Trap-handler evals (the vast majority) have
    // no user functions and can be freed immediately, preventing exhaustion of
    // the limited bash_source_ctx_list slots.
    if (hashmap_count(mt->user_funcs) > 0) {
        if (bash_source_ctx_count < BASH_SOURCE_CTX_MAX) {
            bash_source_ctx_list[bash_source_ctx_count++] = ctx;
        } else {
            MIR_finish(ctx);
        }
    } else {
        MIR_finish(ctx);
    }

    hashmap_free(mt->vars);
    hashmap_free(mt->import_cache);
    hashmap_free(mt->user_funcs);
    free(mt);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    bash_transpiler_destroy(tp);
    if (preproc_buf) strbuf_free(preproc_buf);
    if (dd_buf) strbuf_free(dd_buf);
    free(source_text);

    return result;
}

// ============================================================================
// Source preprocessing: fix tree-sitter parsing issues
// ============================================================================

// Pass 1: Replace $$ with ${$} outside single quotes.
// tree-sitter-bash fails to recognize $$ as a special variable when it appears
// at the end of a word (e.g., "test-$$" parses as a single word, losing the expansion).
// ${$} is semantically identical and always parsed correctly.
static const char* preprocess_dollar_dollar(const char* src, size_t src_len, StrBuf** out_buf) {
    // quick scan: is there any $$ in the source?
    bool has_dd = false;
    for (size_t i = 0; i + 1 < src_len; i++) {
        if (src[i] == '$' && src[i+1] == '$') {
            // check it's not $$( which is $( after PID — actually $$( is $$ then (
            // and not $${ which is already covered, and not $$$
            has_dd = true;
            break;
        }
    }
    if (!has_dd) return src;

    StrBuf* buf = strbuf_new_cap(src_len + 64);
    *out_buf = buf;
    bool in_single_quote = false;
    bool in_double_quote = false;
    for (size_t i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == '\\' && !in_single_quote && i + 1 < src_len) {
            // escaped char — copy both
            strbuf_append_char(buf, c);
            i++;
            strbuf_append_char(buf, src[i]);
            continue;
        }
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            strbuf_append_char(buf, c);
            continue;
        }
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            strbuf_append_char(buf, c);
            continue;
        }
        if (!in_single_quote && c == '$' && i + 1 < src_len && src[i+1] == '$') {
            // check that the next-next char is NOT [a-zA-Z_0-9] (which would make $$var, not $$)
            // and not another $ (which would be $$$)
            bool is_dd = true;
            if (i + 2 < src_len) {
                char next2 = src[i+2];
                if ((next2 >= 'a' && next2 <= 'z') || (next2 >= 'A' && next2 <= 'Z') ||
                    next2 == '_' || next2 == '$') {
                    is_dd = false;
                }
            }
            if (is_dd) {
                strbuf_append_str(buf, "${$}");
                i++; // skip second $
                continue;
            }
        }
        strbuf_append_char(buf, c);
    }
    return buf->str;
}

// Pass 2: Fix multi-assignment lines for tree-sitter.
// tree-sitter-bash fails to parse "a=1 b=2\nfor ..." inside compound statements:
// it drops the "for" line entirely as a parse error. This preprocessor converts
// lines that are purely multiple space-separated assignments (e.g. "a=1 b=2 c=3")
// into semicolon-separated form ("a=1; b=2; c=3") so each assignment is a separate
// statement and the following control-flow line is parsed correctly.
//
// A line qualifies if every token is of the form: IDENTIFIER= followed by a value
// that ends at whitespace or end-of-line (not followed by a keyword or argument).
static const char* preprocess_bash_source(const char* src, size_t src_len, StrBuf** out_buf) {
    // return src directly if no multi-assignment lines are possible
    // quick check: does any line have ' [a-zA-Z_][a-zA-Z0-9_]*=' ?
    bool has_multi_assign = false;
    for (size_t i = 0; i + 2 < src_len; i++) {
        if (src[i] == ' ' && (i == 0 || src[i-1] != '\n')) {
            // look for IDENTIFIER= pattern after space
            size_t j = i + 1;
            while (j < src_len) {
                char c = src[j];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                    (c >= '0' && c <= '9' && j > i+1)) {
                    j++;
                } else {
                    break;
                }
            }
            if (j > i+1 && j < src_len && src[j] == '=') {
                has_multi_assign = true;
                break;
            }
        }
    }
    if (!has_multi_assign) return src;

    StrBuf* buf = strbuf_new_cap(src_len + 64);
    *out_buf = buf;
    size_t i = 0;
    while (i < src_len) {
        // find the start of the current line (skip leading whitespace for detection)
        size_t line_start = i;
        // collect leading whitespace
        while (i < src_len && (src[i] == ' ' || src[i] == '\t')) i++;
        size_t content_start = i;

        // check if this line is a sequence of variable assignments
        // a variable assignment token: [a-zA-Z_][a-zA-Z0-9_]*=<value>
        // <value> ends at the next unquoted space or end-of-line
        if (i < src_len && src[i] != '\n' && src[i] != '\r' && src[i] != '#') {
            // parse first token: must be IDENTIFIER=
            size_t j = i;
            bool first_is_assign = false;
            while (j < src_len) {
                char c = src[j];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                    (c >= '0' && c <= '9' && j > i)) {
                    j++;
                } else {
                    break;
                }
            }
            if (j > i && j < src_len && src[j] == '=') {
                first_is_assign = true;
            }

            if (first_is_assign) {
                // scan the full line to detect if it has multiple assignments
                // emit each assignment as a separate statement separated by '; '
                // we must handle: single-quoted, double-quoted, and unquoted values
                // position: we are at 'i' (start of first assignment token)
                // emit leading whitespace first
                for (size_t k = line_start; k < content_start; k++) {
                    strbuf_append_char(buf, src[k]);
                }

                bool any_extra = false;
                size_t pos = i;
                while (pos < src_len && src[pos] != '\n' && src[pos] != '\r') {
                    // try to match IDENTIFIER=
                    size_t name_start = pos;
                    size_t name_end = pos;
                    while (name_end < src_len) {
                        char c = src[name_end];
                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                            (c >= '0' && c <= '9' && name_end > name_start)) {
                            name_end++;
                        } else {
                            break;
                        }
                    }
                    if (name_end > name_start && name_end < src_len && src[name_end] == '=') {
                        // emit this assignment
                        pos = name_end + 1; // skip past '='
                        // emit NAME=
                        for (size_t k = name_start; k <= name_end; k++) {
                            strbuf_append_char(buf, src[k]);
                        }
                        // emit value until next unquoted space, newline, `;`, `|`, `&`, `>`
                        while (pos < src_len) {
                            char c = src[pos];
                            if (c == '(' ) {
                                // array value: copy until matching )
                                strbuf_append_char(buf, c); pos++;
                                int depth = 1;
                                while (pos < src_len && depth > 0) {
                                    if (src[pos] == '(') depth++;
                                    else if (src[pos] == ')') depth--;
                                    if (depth > 0 || src[pos] == ')') {
                                        strbuf_append_char(buf, src[pos]);
                                    }
                                    pos++;
                                }
                            } else if (c == '\'' ) {
                                // single-quoted: copy verbatim until closing '
                                strbuf_append_char(buf, c); pos++;
                                while (pos < src_len && src[pos] != '\'') {
                                    strbuf_append_char(buf, src[pos]); pos++;
                                }
                                if (pos < src_len) { strbuf_append_char(buf, src[pos]); pos++; }
                            } else if (c == '"') {
                                // double-quoted: copy until closing "
                                strbuf_append_char(buf, c); pos++;
                                while (pos < src_len && src[pos] != '"') {
                                    if (src[pos] == '\\' && pos+1 < src_len) {
                                        strbuf_append_char(buf, src[pos]); pos++;
                                    }
                                    strbuf_append_char(buf, src[pos]); pos++;
                                }
                                if (pos < src_len) { strbuf_append_char(buf, src[pos]); pos++; }
                            } else if (c == '$' && pos+1 < src_len && src[pos+1] == '(') {
                                // command substitution: copy until matching )
                                strbuf_append_char(buf, c); pos++;
                                strbuf_append_char(buf, src[pos]); pos++;
                                int depth = 1;
                                while (pos < src_len && depth > 0) {
                                    if (src[pos] == '(') depth++;
                                    else if (src[pos] == ')') depth--;
                                    strbuf_append_char(buf, src[pos]); pos++;
                                }
                            } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                                       c == ';' || c == '|' || c == '&' || c == '>' || c == '<') {
                                break;
                            } else {
                                strbuf_append_char(buf, c); pos++;
                            }
                        }
                        // skip spaces/tabs to check for next assignment
                        size_t ws_start = pos;
                        while (pos < src_len && (src[pos] == ' ' || src[pos] == '\t')) pos++;

                        // check if next token is IDENTIFIER=
                        size_t next_name_start = pos;
                        size_t next_name_end = pos;
                        while (next_name_end < src_len) {
                            char c = src[next_name_end];
                            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                                (c >= '0' && c <= '9' && next_name_end > next_name_start)) {
                                next_name_end++;
                            } else {
                                break;
                            }
                        }
                        bool next_is_assign = (next_name_end > next_name_start &&
                                               next_name_end < src_len &&
                                               src[next_name_end] == '=');
                        if (next_is_assign) {
                            // emit semicolon to separate assignments (preserve line count for LINENO)
                            strbuf_append_str(buf, "; ");
                            any_extra = true;
                        } else {
                            // preserve whitespace before non-assignment token (e.g., comment)
                            for (size_t k = ws_start; k < pos; k++) {
                                strbuf_append_char(buf, src[k]);
                            }
                        }
                        // continue to next assignment (or end of line)
                    } else {
                        // not an assignment: emit remaining line verbatim
                        while (pos < src_len && src[pos] != '\n' && src[pos] != '\r') {
                            strbuf_append_char(buf, src[pos]); pos++;
                        }
                        break;
                    }
                }

                // emit line ending
                if (i < src_len && (src[pos] == '\n' || src[pos] == '\r')) {
                    strbuf_append_char(buf, src[pos]);
                    if (src[pos] == '\r' && pos+1 < src_len && src[pos+1] == '\n') {
                        pos++;
                        strbuf_append_char(buf, src[pos]);
                    }
                    pos++;
                }
                i = pos;
                continue;
            }
        }

        // normal line: emit verbatim until end of line
        i = content_start;
        // re-emit leading whitespace
        for (size_t k = line_start; k < i; k++) {
            strbuf_append_char(buf, src[k]);
        }
        while (i < src_len && src[i] != '\n' && src[i] != '\r') {
            strbuf_append_char(buf, src[i]); i++;
        }
        if (i < src_len) {
            strbuf_append_char(buf, src[i]); // '\n' or '\r'
            if (src[i] == '\r' && i+1 < src_len && src[i+1] == '\n') {
                i++;
                strbuf_append_char(buf, src[i]);
            }
            i++;
        }
    }
    strbuf_append_char(buf, '\0'); // null terminate
    return buf->str;
}

// ============================================================================
// Main entry point: transpile_bash_to_mir
// ============================================================================

Item transpile_bash_to_mir(Runtime* runtime, const char* bash_source, const char* filename) {
    log_debug("bash-mir: starting transpilation for '%s'", filename ? filename : "<string>");

    // store runtime for bash_source_file() to use
    bash_source_runtime = runtime;

    // create Bash transpiler
    BashTranspiler* tp = bash_transpiler_create(runtime);
    if (!tp) {
        log_error("bash-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    tp->source = bash_source;
    tp->source_length = strlen(bash_source);

    // preprocess pass 1: fix $$ at end-of-word (tree-sitter doesn't recognize it)
    StrBuf* dd_buf = NULL;
    const char* dd_source = preprocess_dollar_dollar(bash_source, tp->source_length, &dd_buf);
    if (dd_source != bash_source) {
        tp->source = dd_source;
        tp->source_length = strlen(dd_source);
    }

    // preprocess pass 2: split multi-assignment lines (fixes tree-sitter error recovery)
    StrBuf* preproc_buf = NULL;
    const char* actual_source = preprocess_bash_source(tp->source, tp->source_length, &preproc_buf);
    if (actual_source != tp->source) {
        tp->source = actual_source;
        tp->source_length = strlen(actual_source);
        log_debug("bash-mir: preprocessed source (%zu -> %zu bytes)", strlen(bash_source), tp->source_length);
    }

    // parse source with tree-sitter-bash
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_bash());

    TSTree* tree = ts_parser_parse_string(parser, NULL, tp->source, (uint32_t)tp->source_length);
    if (!tree) {
        log_error("bash-mir: parse failed");
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tree);

    // build AST
    BashAstNode* ast = build_bash_ast(tp, root);
    if (!ast) {
        log_error("bash-mir: AST build failed");
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        return (Item){.item = ITEM_ERROR};
    }

    // set up evaluation context
    EvalContext bash_context;
    memset(&bash_context, 0, sizeof(EvalContext));
    EvalContext* old_context = context;
    bool reusing_context = false;

    if (old_context && old_context->heap) {
        context = old_context;
        reusing_context = true;
        if (!context->nursery) {
            context->nursery = gc_nursery_create(0);
        }
    } else {
        bash_context.nursery = gc_nursery_create(0);
        context = &bash_context;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
        context->type_list = arraylist_new(64);
    }

    _lambda_rt = (Context*)context;

    // init Bash runtime
    bash_runtime_init();

    // apply any deferred positional args (set before heap was initialized)
    bash_apply_pending_args();

    // init MIR context
    MIR_context_t ctx = jit_init(runtime->optimize_level);
    if (!ctx) {
        log_error("bash-mir: MIR context init failed");
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
        if (preproc_buf) strbuf_free(preproc_buf);
        if (dd_buf) strbuf_free(dd_buf);
        return (Item){.item = ITEM_ERROR};
    }

    // allocate MIR transpiler
    BashMirTranspiler* mt = (BashMirTranspiler*)malloc(sizeof(BashMirTranspiler));
    memset(mt, 0, sizeof(BashMirTranspiler));
    mt->tp = tp;
    mt->ctx = ctx;
    mt->vars = hashmap_new(sizeof(BashMirVar), 64, 0, 0,
                           bash_var_hash, bash_var_cmp, NULL, NULL);
    mt->import_cache = hashmap_new(sizeof(BashMirImportEntry), 64, 0, 0,
                                   bm_import_hash, bm_import_cmp, NULL, NULL);
    mt->user_funcs = hashmap_new(sizeof(BashMirUserFunc), 16, 0, 0,
                                 bm_user_func_hash, bm_user_func_cmp, NULL, NULL);
    mt->var_count = 0;
    mt->label_counter = 0;
    mt->loop_depth = 0;

    // create module
    mt->module = MIR_new_module(ctx, "bash_script");

    // === Pass 1: Compile all function definitions first ===
    BashProgramNode* program = (BashProgramNode*)ast;
    BashAstNode* stmt = program->body;
    while (stmt) {
        if (stmt->node_type == BASH_AST_NODE_FUNCTION_DEF) {
            bm_transpile_function_def(mt, (BashFunctionDefNode*)stmt);
        }
        stmt = stmt->next;
    }

    // === Pass 2: Compile bash_main (function defs become no-ops) ===
    MIR_type_t ret_type = MIR_T_I64;
    mt->current_func_item = MIR_new_func(ctx, "bash_main", 1, &ret_type, 0);
    mt->current_func = mt->current_func_item->u.func;
    mt->result_reg = bm_new_temp(mt);
    MIR_op_t script_name_str = bm_emit_string_literal(mt, filename ? filename : "", filename ? (int)strlen(filename) : 0);
    bm_emit_call_void_1(mt, "bash_push_source", script_name_str);
    bm_emit_call_void_1(mt, "bash_set_script_name", script_name_str);
    // push "main" as funcname — needed for ${FUNCNAME[@]} inside functions
    bm_emit_call_void_1(mt, "bash_push_funcname",
        bm_emit_string_literal(mt, "main", 4));

    stmt = program->body;
    while (stmt) {
        bm_transpile_statement(mt, stmt);
        if (stmt->node_type != BASH_AST_NODE_LIST) {
            bm_emit_errexit_check(mt);
        }
        stmt = stmt->next;
    }

    // run EXIT trap before returning from main script
    bm_emit_call_void_0(mt, "bash_trap_run_exit");
    bm_emit_call_void_0(mt, "bash_pop_funcname");
    bm_emit_call_void_0(mt, "bash_pop_source");

    // return exit code
    MIR_reg_t final_ec = bm_emit_call_0(mt, "bash_get_exit_code");

    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, final_ec)));

    MIR_finish_func(ctx);
    MIR_finish_module(ctx);

    // load and link
    MIR_load_module(ctx, mt->module);

#ifndef NDEBUG
    FILE* mir_dump = fopen("temp/bash_mir_dump.txt", "w");
    if (mir_dump) {
        MIR_output(ctx, mir_dump);
        fclose(mir_dump);
    }
#endif

    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // register top-level user functions before executing main so eval/trap code
    // can resolve functions defined in the script body
    size_t iter = 0;
    void* item_ptr;
    while (hashmap_iter(mt->user_funcs, &iter, &item_ptr)) {
        BashMirUserFunc* uf = (BashMirUserFunc*)item_ptr;
        char full_name[160];
        snprintf(full_name, sizeof(full_name), "bash_uf_%s", uf->name);
        BashRtFuncPtr func_ptr = (BashRtFuncPtr)find_func(ctx, full_name);
        if (func_ptr) {
            bash_register_rt_func(uf->name, func_ptr);
        }
    }

    // find and execute bash_main
    typedef Item (*bash_main_func_t)(void);
    bash_main_func_t bash_main = (bash_main_func_t)find_func(ctx, "bash_main");

    Item result = {.item = ITEM_ERROR};
    if (bash_main) {
        log_debug("bash-mir: executing JIT compiled Bash script");
        result = bash_main();
    } else {
        log_error("bash-mir: failed to find bash_main");
    }

    // cleanup
    bash_source_runtime = NULL;
    bash_runtime_cleanup();
    hashmap_free(mt->vars);
    hashmap_free(mt->import_cache);
    free(mt);

    MIR_finish(ctx);

    // free any sourced contexts now that execution is complete
    for (int i = 0; i < bash_source_ctx_count; i++) {
        MIR_finish(bash_source_ctx_list[i]);
    }
    bash_source_ctx_count = 0;
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    bash_transpiler_destroy(tp);
    if (preproc_buf) strbuf_free(preproc_buf);
    if (dd_buf) strbuf_free(dd_buf);

    if (!reusing_context) {
        context = old_context;
    }

    return result;
}
