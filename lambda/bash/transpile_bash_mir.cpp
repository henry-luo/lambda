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
static MIR_op_t bm_transpile_word(BashMirTranspiler* mt, BashAstNode* node);
static MIR_op_t bm_transpile_command(BashMirTranspiler* mt, BashCommandNode* cmd);
static void bm_transpile_if(BashMirTranspiler* mt, BashIfNode* node);
static void bm_transpile_for(BashMirTranspiler* mt, BashForNode* node);
static void bm_transpile_for_arith(BashMirTranspiler* mt, BashForArithNode* node);
static void bm_transpile_while(BashMirTranspiler* mt, BashWhileNode* node);
static void bm_transpile_case(BashMirTranspiler* mt, BashCaseNode* node);
static void bm_transpile_assignment(BashMirTranspiler* mt, BashAssignmentNode* node);
static MIR_op_t bm_transpile_arith(BashMirTranspiler* mt, BashAstNode* node);
static void bm_transpile_function_def(BashMirTranspiler* mt, BashFunctionDefNode* node);
static MIR_op_t bm_transpile_expansion(BashMirTranspiler* mt, BashAstNode* node);
static MIR_op_t bm_transpile_varref(BashMirTranspiler* mt, BashVarRefNode* node);
static MIR_op_t bm_transpile_string(BashMirTranspiler* mt, BashStringNode* node);
static MIR_op_t bm_transpile_concat(BashMirTranspiler* mt, BashConcatNode* node);

// ============================================================================
// Helpers
// ============================================================================

static MIR_reg_t bm_new_temp(BashMirTranspiler* mt) {
    char name[32];
    snprintf(name, sizeof(name), "_bt%d", mt->tp->temp_var_counter++);
    return MIR_new_func_reg(mt->ctx, mt->current_func, MIR_T_I64, name);
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

// emit a varargs builtin call: func(Item* args, int argc) -> Item
// builds args buffer, fills it, and calls the function
static MIR_op_t bm_emit_varargs_builtin(BashMirTranspiler* mt, const char* fn_name,
                                          BashCommandNode* cmd) {
    int argc = cmd->arg_count;
    MIR_reg_t args_ptr = bm_new_temp(mt);
    if (argc > 0) {
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                         MIR_new_uint_op(mt->ctx, (uint64_t)(uintptr_t)malloc(argc * sizeof(Item)))));
        int i = 0;
        BashAstNode* arg = cmd->args;
        while (arg && i < argc) {
            MIR_op_t arg_val = bm_transpile_node(mt, arg);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                   args_ptr, 0, 1),
                    arg_val));
            arg = arg->next;
            i++;
        }
    } else {
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                         MIR_new_uint_op(mt->ctx, 0)));
    }

    MIR_reg_t result = bm_emit_call_2(mt, fn_name,
        MIR_new_reg_op(mt->ctx, args_ptr), MIR_new_int_op(mt->ctx, argc));
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
    case BASH_AST_NODE_ASSIGNMENT:
        bm_transpile_assignment(mt, (BashAssignmentNode*)node);
        break;
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
        bm_transpile_statement(mt, list->left);

        if (list->op == BASH_LIST_AND) {
            // && : only execute right if left succeeded (exit code 0)
            MIR_label_t skip = bm_new_label(mt);
            MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");
            // if exit code != 0, skip right side
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, skip),
                             MIR_new_reg_op(mt->ctx, ec),
                             MIR_new_uint_op(mt->ctx, i2it(0))));
            bm_transpile_statement(mt, list->right);
            MIR_append_insn(mt->ctx, mt->current_func_item, skip);
        } else if (list->op == BASH_LIST_OR) {
            // || : only execute right if left failed
            MIR_label_t skip = bm_new_label(mt);
            MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, skip),
                             MIR_new_reg_op(mt->ctx, ec),
                             MIR_new_uint_op(mt->ctx, i2it(0))));
            bm_transpile_statement(mt, list->right);
            MIR_append_insn(mt->ctx, mt->current_func_item, skip);
        } else {
            // ; or & : execute sequentially
            bm_transpile_statement(mt, list->right);
        }
        break;
    }
    case BASH_AST_NODE_BLOCK: {
        BashBlockNode* block = (BashBlockNode*)node;
        BashAstNode* stmt = block->statements;
        while (stmt) {
            bm_transpile_statement(mt, stmt);
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
        // push subshell scope
        bm_emit_call_void_0(mt, "bash_scope_push_subshell");
        bm_transpile_statement(mt, subshell->body);
        // pop subshell scope
        bm_emit_call_void_0(mt, "bash_scope_pop_subshell");
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
        bm_emit_call_void_0(mt, "bash_pop_positional");
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_ret_insn(mt->ctx, 1, ret_val));
        break;
    }
    case BASH_AST_NODE_BREAK: {
        if (mt->loop_break_label) {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP,
                             MIR_new_label_op(mt->ctx, mt->loop_break_label)));
        }
        break;
    }
    case BASH_AST_NODE_CONTINUE: {
        if (mt->loop_continue_label) {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP,
                             MIR_new_label_op(mt->ctx, mt->loop_continue_label)));
        }
        break;
    }
    case BASH_AST_NODE_ARITHMETIC_EXPR:
        bm_transpile_arith(mt, node);
        break;
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
        return bm_emit_string_literal(mt, "", 0);
    }
    case BASH_AST_NODE_ARITHMETIC_EXPR:
        return bm_transpile_arith(mt, node);
    case BASH_AST_NODE_COMMAND_SUB: {
        BashCommandSubNode* cmd_sub = (BashCommandSubNode*)node;
        // begin capture → execute body → end capture (returns captured string)
        bm_emit_call_void_0(mt, "bash_begin_capture");
        BashAstNode* stmt = cmd_sub->body;
        while (stmt) {
            bm_transpile_statement(mt, stmt);
            stmt = stmt->next;
        }
        MIR_reg_t result = bm_emit_call_0(mt, "bash_end_capture");
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
        MIR_op_t arr_val = bm_emit_get_var(mt, access->name->chars);
        MIR_op_t idx_val = bm_transpile_node(mt, access->index);
        MIR_reg_t result = bm_emit_call_2(mt, "bash_array_get", arr_val, idx_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_ALL: {
        BashArrayAllNode* all_node = (BashArrayAllNode*)node;
        MIR_op_t arr_val = bm_emit_get_var(mt, all_node->name->chars);
        MIR_reg_t result = bm_emit_call_1(mt, "bash_array_all", arr_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_AST_NODE_ARRAY_LENGTH: {
        BashArrayLengthNode* len_node = (BashArrayLengthNode*)node;
        MIR_op_t arr_val = bm_emit_get_var(mt, len_node->name->chars);
        MIR_reg_t result = bm_emit_call_1(mt, "bash_array_length", arr_val);
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
            bm_emit_call_void_2(mt, "bash_array_append",
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

static MIR_op_t bm_transpile_word(BashMirTranspiler* mt, BashAstNode* node) {
    BashWordNode* word = (BashWordNode*)node;
    if (word->text) {
        return bm_emit_string_literal(mt, word->text->chars, word->text->len);
    }
    return bm_emit_string_literal(mt, "", 0);
}

static MIR_op_t bm_transpile_varref(BashMirTranspiler* mt, BashVarRefNode* node) {
    if (node->name) {
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

static MIR_op_t bm_transpile_expansion(BashMirTranspiler* mt, BashAstNode* node) {
    BashExpansionNode* exp = (BashExpansionNode*)node;
    if (!exp->variable) return bm_emit_string_literal(mt, "", 0);

    // check if variable is a positional parameter (single digit 1-9)
    MIR_op_t var_val;
    if (exp->variable->len == 1 && exp->variable->chars[0] >= '1' && exp->variable->chars[0] <= '9') {
        int pos = exp->variable->chars[0] - '0';
        MIR_reg_t positional = bm_emit_call_1(mt, "bash_get_positional",
            MIR_new_int_op(mt->ctx, pos));
        var_val = MIR_new_reg_op(mt->ctx, positional);
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
        MIR_reg_t result = bm_emit_call_2(mt, "bash_expand_default", var_val, def_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_ASSIGN_DEFAULT: {
        MIR_op_t def_val = exp->argument ? bm_transpile_node(mt, exp->argument)
                                         : bm_emit_string_literal(mt, "", 0);
        MIR_op_t var_name = bm_emit_string_literal(mt, exp->variable->chars, exp->variable->len);
        MIR_reg_t result = bm_emit_call_3(mt, "bash_expand_assign_default",
            var_name, var_val, def_val);
        return MIR_new_reg_op(mt->ctx, result);
    }
    case BASH_EXPAND_ALT: {
        MIR_op_t alt_val = exp->argument ? bm_transpile_node(mt, exp->argument)
                                         : bm_emit_string_literal(mt, "", 0);
        MIR_reg_t result = bm_emit_call_2(mt, "bash_expand_alt", var_val, alt_val);
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
            bm_transpile_statement(mt, assign);
            assign = assign->next;
        }
        return bm_emit_int_literal(mt, 0);
    }

    // get command name
    BashWordNode* name_node = (BashWordNode*)cmd->name;
    if (cmd->name->node_type != BASH_AST_NODE_WORD || !name_node->text) {
        return bm_emit_int_literal(mt, 0);
    }

    const char* cmd_name = name_node->text->chars;
    int cmd_len = name_node->text->len;

    // handle built-in commands
    if (cmd_len == 4 && memcmp(cmd_name, "echo", 4) == 0) {
        int argc = cmd->arg_count;
        if (argc == 0) {
            MIR_reg_t result = bm_emit_call_2(mt, "bash_builtin_echo",
                MIR_new_uint_op(mt->ctx, 0), MIR_new_int_op(mt->ctx, 0));
            return MIR_new_reg_op(mt->ctx, result);
        }

        // allocate args on heap, fill them, pass pointer + count
        MIR_reg_t args_ptr = bm_new_temp(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                         MIR_new_uint_op(mt->ctx, (uint64_t)(uintptr_t)malloc(argc * sizeof(Item)))));

        int i = 0;
        BashAstNode* arg = cmd->args;
        while (arg && i < argc) {
            MIR_op_t arg_val = bm_transpile_node(mt, arg);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                   args_ptr, 0, 1),
                    arg_val));
            arg = arg->next;
            i++;
        }

        MIR_reg_t result = bm_emit_call_2(mt, "bash_builtin_echo",
            MIR_new_reg_op(mt->ctx, args_ptr), MIR_new_int_op(mt->ctx, argc));
        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 6 && memcmp(cmd_name, "printf", 6) == 0) {
        // printf format [args...]
        if (cmd->arg_count == 0) return bm_emit_int_literal(mt, 0);

        // first arg is format string
        MIR_op_t format_val = bm_transpile_node(mt, cmd->args);
        int extra_argc = cmd->arg_count - 1;

        MIR_reg_t args_ptr = bm_new_temp(mt);
        if (extra_argc > 0) {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_uint_op(mt->ctx, (uint64_t)(uintptr_t)malloc(extra_argc * sizeof(Item)))));
            int i = 0;
            BashAstNode* arg = cmd->args->next;
            while (arg && i < extra_argc) {
                MIR_op_t arg_val = bm_transpile_node(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
        } else {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_uint_op(mt->ctx, 0)));
        }

        MIR_reg_t result = bm_emit_call_3(mt, "bash_builtin_printf",
            format_val, MIR_new_reg_op(mt->ctx, args_ptr),
            MIR_new_int_op(mt->ctx, extra_argc));
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

    if (cmd_len == 5 && memcmp(cmd_name, "shift", 5) == 0) {
        int shift_n = 1;
        if (cmd->args) {
            // shift N — get N from first argument
            BashAstNode* arg = cmd->args;
            if (arg->node_type == BASH_AST_NODE_WORD) {
                BashWordNode* w = (BashWordNode*)arg;
                if (w->text) shift_n = atoi(w->text->chars);
            }
        }
        MIR_reg_t result = bm_emit_call_1(mt, "bash_shift_args",
            MIR_new_int_op(mt->ctx, shift_n));
        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 5 && memcmp(cmd_name, "local", 5) == 0) {
        // local var=value — treated as assignment for now
        BashAstNode* arg = cmd->args;
        while (arg) {
            if (arg->node_type == BASH_AST_NODE_ASSIGNMENT) {
                bm_transpile_statement(mt, arg);
            }
            arg = arg->next;
        }
        return bm_emit_int_literal(mt, 0);
    }

    if (cmd_len == 5 && memcmp(cmd_name, "unset", 5) == 0) {
        // unset 'arr[idx]' or unset var
        if (cmd->args) {
            BashAstNode* arg = cmd->args;
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
                    bm_emit_call_2(mt, "bash_array_unset", arr_val, idx_val);
                } else {
                    // unset plain variable
                    bm_emit_set_var(mt, text, bm_emit_string_literal(mt, "", 0));
                }
            }
        }
        return bm_emit_int_literal(mt, 0);
    }

    // pipeline builtins (read from stdin item)
    if (cmd_len == 3 && memcmp(cmd_name, "cat", 3) == 0) {
        return bm_emit_varargs_builtin(mt, "bash_builtin_cat", cmd);
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

    // check for user-defined function call
    BashMirUserFunc key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%.*s", cmd_len, cmd_name);
    const BashMirUserFunc* uf = (const BashMirUserFunc*)hashmap_get(mt->user_funcs, &key);
    if (uf) {
        // build a function call with arguments
        int argc = cmd->arg_count;

        MIR_reg_t args_ptr = bm_new_temp(mt);
        if (argc > 0) {
            // allocate args buffer on stack (runtime alloca for recursion safety)
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_ALLOCA, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_int_op(mt->ctx, argc * (int)sizeof(Item))));
            int i = 0;
            BashAstNode* arg = cmd->args;
            while (arg && i < argc) {
                MIR_op_t arg_val = bm_transpile_node(mt, arg);
                MIR_append_insn(mt->ctx, mt->current_func_item,
                    MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(Item),
                                       args_ptr, 0, 1),
                        arg_val));
                arg = arg->next;
                i++;
            }
        } else {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, args_ptr),
                             MIR_new_uint_op(mt->ctx, 0)));
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

        MIR_reg_t result = bm_new_temp(mt);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_call_insn(mt->ctx, 5,
                MIR_new_ref_op(mt->ctx, proto),
                MIR_new_ref_op(mt->ctx, uf->func_item),
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, args_ptr),
                MIR_new_int_op(mt->ctx, argc)));

        return MIR_new_reg_op(mt->ctx, result);
    }

    // fallback: unrecognized command — log and return exit code 127
    log_debug("bash-mir: unrecognized command '%s'", cmd_name);
    return bm_emit_int_literal(mt, 127);
}

// ============================================================================
// Assignment
// ============================================================================

static void bm_transpile_assignment(BashMirTranspiler* mt, BashAssignmentNode* node) {
    if (!node->name) return;

    MIR_op_t value;
    if (node->value) {
        value = bm_transpile_node(mt, node->value);
    } else {
        value = bm_emit_string_literal(mt, "", 0);
    }

    if (node->index) {
        // arr[idx]=val → bash_array_set(arr, idx, val)
        MIR_op_t arr_val = bm_emit_get_var(mt, node->name->chars);
        MIR_op_t idx_val = bm_transpile_node(mt, node->index);
        bm_emit_call_3(mt, "bash_array_set", arr_val, idx_val, value);
    } else if (node->is_append && node->value &&
               node->value->node_type == BASH_AST_NODE_ARRAY_LITERAL) {
        // arr+=(elem1 elem2 ...) → bash_array_append for each element
        BashArrayLiteralNode* arr_lit = (BashArrayLiteralNode*)node->value;
        MIR_op_t arr_val = bm_emit_get_var(mt, node->name->chars);
        BashAstNode* elem = arr_lit->elements;
        while (elem) {
            MIR_op_t elem_val = bm_transpile_node(mt, elem);
            bm_emit_call_void_2(mt, "bash_array_append", arr_val, elem_val);
            elem = elem->next;
        }
    } else if (node->is_append) {
        // var+=val → string append
        MIR_op_t old_val = bm_emit_get_var(mt, node->name->chars);
        MIR_reg_t result = bm_emit_call_2(mt, "bash_concat", old_val, value);
        bm_emit_set_var(mt, node->name->chars, MIR_new_reg_op(mt->ctx, result));
    } else {
        bm_emit_set_var(mt, node->name->chars, value);
    }
}

// ============================================================================
// If statement
// ============================================================================

static void bm_transpile_if(BashMirTranspiler* mt, BashIfNode* node) {
    MIR_label_t else_label = bm_new_label(mt);
    MIR_label_t end_label = bm_new_label(mt);

    // transpile condition
    bm_transpile_statement(mt, node->condition);

    // check exit code
    MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");

    // branch: if exit code != 0, go to else/elif
    MIR_label_t first_elif_or_else = node->elif_clauses ?
        bm_new_label(mt) : (node->else_body ? else_label : end_label);

    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_BNE,
                     MIR_new_label_op(mt->ctx, first_elif_or_else),
                     MIR_new_reg_op(mt->ctx, ec),
                     MIR_new_uint_op(mt->ctx, i2it(0))));

    // then body
    bm_transpile_statement(mt, node->then_body);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, end_label)));

    // elif clauses
    if (node->elif_clauses) {
        MIR_append_insn(mt->ctx, mt->current_func_item, first_elif_or_else);

        BashAstNode* elif = node->elif_clauses;
        while (elif) {
            BashElifNode* elif_node = (BashElifNode*)elif;
            MIR_label_t next_elif = elif->next ? bm_new_label(mt) :
                                    (node->else_body ? else_label : end_label);

            bm_transpile_statement(mt, elif_node->condition);

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

    // check if iterating over an array: single word that is ARRAY_ALL or string wrapping ARRAY_ALL
    BashAstNode* word = node->words;
    String* array_name = NULL;
    if (word && !word->next) {
        if (word->node_type == BASH_AST_NODE_ARRAY_ALL) {
            array_name = ((BashArrayAllNode*)word)->name;
        } else if (word->node_type == BASH_AST_NODE_STRING) {
            BashStringNode* str = (BashStringNode*)word;
            if (str->parts && !str->parts->next &&
                str->parts->node_type == BASH_AST_NODE_ARRAY_ALL) {
                array_name = ((BashArrayAllNode*)str->parts)->name;
            }
        }
    }

    if (array_name) {
        // runtime array iteration: for var in "${arr[@]}"
        MIR_op_t arr_val = bm_emit_get_var(mt, array_name->chars);

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

        // elem = bash_array_get(arr, i2it(idx))
        MIR_reg_t idx_item_reg = bm_emit_call_1(mt, "bash_int_to_item",
            MIR_new_reg_op(mt->ctx, idx_reg));
        // re-fetch arr each iteration (in case it changes)
        MIR_op_t arr_val2 = bm_emit_get_var(mt, array_name->chars);
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
        // compile-time unrolled iteration over word list
        word = node->words;
        while (word) {
            MIR_op_t word_val = bm_transpile_node(mt, word);
            bm_emit_set_var(mt, node->variable->chars, word_val);

            MIR_label_t iter_continue = bm_new_label(mt);
            mt->loop_continue_label = iter_continue;

            bm_transpile_statement(mt, node->body);

            MIR_append_insn(mt->ctx, mt->current_func_item, iter_continue);

            word = word->next;
        }

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

    // init
    if (node->init) bm_transpile_arith(mt, node->init);

    // loop start
    MIR_append_insn(mt->ctx, mt->current_func_item, loop_start);

    // condition
    if (node->condition) {
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

    // step
    if (node->step) bm_transpile_arith(mt, node->step);

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

    MIR_label_t old_break = mt->loop_break_label;
    MIR_label_t old_continue = mt->loop_continue_label;
    mt->loop_break_label = loop_end;
    mt->loop_continue_label = loop_continue;
    mt->loop_depth++;

    bool is_until = (node->base.node_type == BASH_AST_NODE_UNTIL);

    // loop start
    MIR_append_insn(mt->ctx, mt->current_func_item, loop_start);

    // condition
    bm_transpile_statement(mt, node->condition);

    MIR_reg_t ec = bm_emit_call_0(mt, "bash_get_exit_code");

    if (is_until) {
        // until: exit when condition succeeds (exit code 0)
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BEQ,
                         MIR_new_label_op(mt->ctx, loop_end),
                         MIR_new_reg_op(mt->ctx, ec),
                         MIR_new_uint_op(mt->ctx, i2it(0))));
    } else {
        // while: exit when condition fails (exit code != 0)
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_BNE,
                         MIR_new_label_op(mt->ctx, loop_end),
                         MIR_new_reg_op(mt->ctx, ec),
                         MIR_new_uint_op(mt->ctx, i2it(0))));
    }

    // body
    bm_transpile_statement(mt, node->body);

    // continue point
    MIR_append_insn(mt->ctx, mt->current_func_item, loop_continue);

    // jump back
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, loop_start)));

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

    BashAstNode* item = node->items;
    while (item) {
        BashCaseItemNode* case_item = (BashCaseItemNode*)item;
        MIR_label_t next_item = bm_new_label(mt);

        // check each pattern
        BashAstNode* pattern = case_item->patterns;
        MIR_label_t match_label = bm_new_label(mt);
        bool has_wildcard = false;

        while (pattern) {
            BashWordNode* pat_word = (BashWordNode*)pattern;
            if (pat_word->text && pat_word->text->len == 1 && pat_word->text->chars[0] == '*') {
                has_wildcard = true;
                pattern = pattern->next;
                continue;
            }

            MIR_op_t pat_val = bm_transpile_node(mt, pattern);
            MIR_reg_t cmp_result = bm_emit_call_2(mt, "bash_test_str_eq", value, pat_val);

            // if match, jump to match body
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_BNE,
                             MIR_new_label_op(mt->ctx, match_label),
                             MIR_new_reg_op(mt->ctx, cmp_result),
                             MIR_new_uint_op(mt->ctx, b2it(false))));

            pattern = pattern->next;
        }

        if (!has_wildcard) {
            // no match — skip to next item
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, next_item)));
        }

        // match body
        MIR_append_insn(mt->ctx, mt->current_func_item, match_label);
        bm_transpile_statement(mt, case_item->body);
        MIR_append_insn(mt->ctx, mt->current_func_item,
            MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, end_label)));

        MIR_append_insn(mt->ctx, mt->current_func_item, next_item);
        item = item->next;
    }

    MIR_append_insn(mt->ctx, mt->current_func_item, end_label);
}

// ============================================================================
// Function definition
// ============================================================================

static void bm_transpile_function_def(BashMirTranspiler* mt, BashFunctionDefNode* node) {
    if (!node->name) return;

    char func_name[140];
    snprintf(func_name, sizeof(func_name), "bash_uf_%.*s", (int)node->name->len, node->name->chars);

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

    // transpile function body
    bm_transpile_statement(mt, node->body);

    // restore positional params
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
        case BASH_OP_LOGICAL_AND: func_name = "bash_arith_eq"; break;  // TODO: short-circuit
        case BASH_OP_LOGICAL_OR: func_name = "bash_arith_eq"; break;   // TODO: short-circuit
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
// Main entry point: transpile_bash_to_mir
// ============================================================================

Item transpile_bash_to_mir(Runtime* runtime, const char* bash_source, const char* filename) {
    log_debug("bash-mir: starting transpilation for '%s'", filename ? filename : "<string>");

    // create Bash transpiler
    BashTranspiler* tp = bash_transpiler_create(runtime);
    if (!tp) {
        log_error("bash-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    tp->source = bash_source;
    tp->source_length = strlen(bash_source);

    // parse source with tree-sitter-bash
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_bash());

    TSTree* tree = ts_parser_parse_string(parser, NULL, bash_source, (uint32_t)tp->source_length);
    if (!tree) {
        log_error("bash-mir: parse failed");
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
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

    // init MIR context
    MIR_context_t ctx = jit_init(runtime->optimize_level);
    if (!ctx) {
        log_error("bash-mir: MIR context init failed");
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        bash_transpiler_destroy(tp);
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

    stmt = program->body;
    while (stmt) {
        bm_transpile_statement(mt, stmt);
        stmt = stmt->next;
    }

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

    // find and execute bash_main
    typedef Item (*bash_main_func_t)(void);
    bash_main_func_t bash_main = (bash_main_func_t)find_func(ctx, "bash_main");

    Item result = {.item = ITEM_ERROR};
    if (bash_main) {
        log_notice("bash-mir: executing JIT compiled Bash script");
        result = bash_main();
    } else {
        log_error("bash-mir: failed to find bash_main");
    }

    // cleanup
    bash_runtime_cleanup();
    hashmap_free(mt->vars);
    hashmap_free(mt->import_cache);
    free(mt);

    MIR_finish(ctx);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    bash_transpiler_destroy(tp);

    if (!reusing_context) {
        context = old_context;
    }

    return result;
}
