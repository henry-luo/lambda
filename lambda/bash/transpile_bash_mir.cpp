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

    // import cache (proto + import items)
    struct hashmap* import_cache;

    // label counters
    int label_counter;

    // loop control labels
    MIR_label_t loop_break_label;
    MIR_label_t loop_continue_label;
    int loop_depth;
} BashMirTranspiler;

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

static void bm_emit_call_void_0(BashMirTranspiler* mt, const char* fn_name) {
    BashMirImportEntry* ie = bm_ensure_import_v(mt, fn_name);
    MIR_append_insn(mt->ctx, mt->current_func_item,
        MIR_new_call_insn(mt->ctx, 2,
            MIR_new_ref_op(mt->ctx, ie->proto),
            MIR_new_ref_op(mt->ctx, ie->import)));
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
    case BASH_AST_NODE_COMMAND:
        bm_transpile_command(mt, (BashCommandNode*)node);
        break;
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
        bm_transpile_function_def(mt, (BashFunctionDefNode*)node);
        break;
    case BASH_AST_NODE_PIPELINE: {
        BashPipelineNode* pipeline = (BashPipelineNode*)node;
        // for non-pipe pipelines (single command), just execute
        BashAstNode* cmd = pipeline->commands;
        while (cmd) {
            bm_transpile_statement(mt, cmd);
            cmd = cmd->next;
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
        if (ctrl->value) {
            MIR_op_t val = bm_transpile_node(mt, ctrl->value);
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_ret_insn(mt->ctx, 1, val));
        } else {
            MIR_append_insn(mt->ctx, mt->current_func_item,
                MIR_new_ret_insn(mt->ctx, 1, MIR_new_uint_op(mt->ctx, i2it(0))));
        }
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
        if (sv->special_id >= BASH_SPECIAL_POS_1 && sv->special_id <= BASH_SPECIAL_POS_9) {
            // $1-$9 → bash_get_positional(n)
            int pos = sv->special_id - BASH_SPECIAL_POS_1 + 1;
            MIR_reg_t result = bm_emit_call_1(mt, "bash_get_positional",
                MIR_new_int_op(mt->ctx, pos));
            return MIR_new_reg_op(mt->ctx, result);
        }
        return bm_emit_string_literal(mt, "", 0);
    }
    case BASH_AST_NODE_ARITHMETIC_EXPR:
        return bm_transpile_arith(mt, node);
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
    if (exp->variable) {
        return bm_emit_get_var(mt, exp->variable->chars);
    }
    return bm_emit_string_literal(mt, "", 0);
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

    if (cmd_len == 4 && memcmp(cmd_name, "true", 4) == 0) {
        MIR_reg_t result = bm_emit_call_0(mt, "bash_builtin_true");
        return MIR_new_reg_op(mt->ctx, result);
    }

    if (cmd_len == 5 && memcmp(cmd_name, "false", 5) == 0) {
        MIR_reg_t result = bm_emit_call_0(mt, "bash_builtin_false");
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

    bm_emit_set_var(mt, node->name->chars, value);
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

    // iterate over words
    BashAstNode* word = node->words;
    int word_count = 0;
    while (word) { word_count++; word = word->next; }
    log_debug("bash-mir: for loop has %d words, variable='%.*s'",
              word_count, (int)node->variable->len, node->variable->chars);
    word = node->words;
    int iter = 0;
    while (word) {
        MIR_op_t word_val = bm_transpile_node(mt, word);
        bm_emit_set_var(mt, node->variable->chars, word_val);

        // use a per-iteration continue label
        MIR_label_t iter_continue = bm_new_label(mt);
        mt->loop_continue_label = iter_continue;

        // body
        bm_transpile_statement(mt, node->body);

        // continue label for this iteration
        MIR_append_insn(mt->ctx, mt->current_func_item, iter_continue);

        word = word->next;
        iter++;
    }

    MIR_append_insn(mt->ctx, mt->current_func_item, loop_end);

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
    // functions in Bash are compiled as separate MIR functions
    // for now, just register the AST node for later invocation
    // TODO: implement function compilation and call dispatch
    if (node->name) {
        log_debug("bash-mir: registered function '%.*s'", (int)node->name->len, node->name->chars);
    }
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
    mt->var_count = 0;
    mt->label_counter = 0;
    mt->loop_depth = 0;

    // create module
    mt->module = MIR_new_module(ctx, "bash_script");

    // create bash_main function: () -> Item (i64)
    MIR_type_t ret_type = MIR_T_I64;
    mt->current_func_item = MIR_new_func(ctx, "bash_main", 1, &ret_type, 0);
    mt->current_func = mt->current_func_item->u.func;
    mt->result_reg = bm_new_temp(mt);

    // transpile AST
    BashProgramNode* program = (BashProgramNode*)ast;
    BashAstNode* stmt = program->body;
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
