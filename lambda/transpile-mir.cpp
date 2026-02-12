#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/url.h"
#include "validator/validator.hpp"
#include <mir.h>
#include <mir-gen.h>

extern Type TYPE_ANY, TYPE_INT;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern void heap_destroy();
extern void frame_start();
extern void frame_end();
extern Url* get_current_dir();
void transpile_mir_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstNode *expr_node, MIR_reg_t *result_reg);

// Forward declare Runner helper functions from runner.cpp
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void runner_cleanup(Runner* runner);

// Forward declare import resolver from mir.c
extern "C" {
    void *import_resolver(const char *name);
}

// MIR type mappings
static MIR_type_t get_mir_type(Type *type) {
    switch (type->type_id) {
    case LMD_TYPE_BOOL:
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
        return MIR_T_I64;
    case LMD_TYPE_FLOAT:
        return MIR_T_D;
    default:
        return MIR_T_P; // pointer for complex types
    }
}

// Create a new register
static MIR_reg_t new_reg(MIR_context_t ctx, MIR_func_t func, const char *name, MIR_type_t type) {
    static int reg_counter = 0;
    char reg_name[64];
    snprintf(reg_name, sizeof(reg_name), "%s_%d", name ? name : "tmp", reg_counter++);
    log_debug("Creating register '%s' of type %d", reg_name, type);
    return MIR_new_func_reg(ctx, func, type, reg_name);
}

static void transpile_mir_primary_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstPrimaryNode *pri_node, MIR_reg_t *result_reg) {
    log_debug("transpile MIR primary expr: pri_node=%p, pri_node->expr=%p", pri_node, pri_node->expr);

    if (pri_node->expr) {
        log_debug("Primary node has expr, recursing...");
        transpile_mir_expr(ctx, func_item, func, pri_node->expr, result_reg);
    } else { // const
        log_debug("Primary node is const");

        // Check if type pointer is valid
        if (!((AstNode*)pri_node)->type) {
            log_error("Type pointer is null!");
            *result_reg = new_reg(ctx, func, "null_type", MIR_T_I64);
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_int_op(ctx, 42)));
            return;
        }

        log_debug("Type valid, type_id=%d, is_literal=%d", ((AstNode*)pri_node)->type->type_id, ((AstNode*)pri_node)->type->is_literal);

        if (((AstNode*)pri_node)->type->is_literal) {  // literal
            log_debug("Processing literal value");
            if (((AstNode*)pri_node)->type->type_id == LMD_TYPE_INT || ((AstNode*)pri_node)->type->type_id == LMD_TYPE_INT64) {
                log_debug("Creating integer literal");
                *result_reg = new_reg(ctx, func, "int_const", MIR_T_I64);
                log_debug("Created register for integer: %u", *result_reg);
                // For now, extract some constant value - in real implementation we'd parse from the source
                long value = 42; // Default test value
                log_debug("Creating MIR instruction with value %ld...", value);

                // Create the instruction parts separately for debugging
                MIR_op_t reg_op = MIR_new_reg_op(ctx, *result_reg);
                log_debug("Created register operand");
                MIR_op_t int_op = MIR_new_int_op(ctx, value);
                log_debug("Created integer operand");
                MIR_insn_t insn = MIR_new_insn(ctx, MIR_MOV, reg_op, int_op);
                log_debug("Created MIR MOV instruction");

                log_debug("About to append instruction to function...");
                MIR_append_insn(ctx, func_item, insn);
                log_debug("Integer literal instruction created");
            }
            else if (((AstNode*)pri_node)->type->type_id == LMD_TYPE_FLOAT) {
                log_debug("Creating float literal");
                *result_reg = new_reg(ctx, func, "float_const", MIR_T_D);
                double value = 3.14; // Default test value
                MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DMOV,
                    MIR_new_reg_op(ctx, *result_reg),
                    MIR_new_double_op(ctx, value)));
                log_debug("Float literal instruction created");
            }
            else if (((AstNode*)pri_node)->type->type_id == LMD_TYPE_BOOL) {
                log_debug("Creating boolean literal");
                *result_reg = new_reg(ctx, func, "bool_const", MIR_T_I64);
                MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
                    MIR_new_reg_op(ctx, *result_reg),
                    MIR_new_int_op(ctx, 1))); // true
                log_debug("Boolean literal instruction created");
            }
            else { // null, other types
                log_debug("Creating other literal type");
                *result_reg = new_reg(ctx, func, "null_const", MIR_T_I64);
                MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
                    MIR_new_reg_op(ctx, *result_reg),
                    MIR_new_int_op(ctx, 0)));
                log_debug("Other literal instruction created");
            }
        } else {
            log_debug("Processing non-literal constant");
            *result_reg = new_reg(ctx, func, "const_val", get_mir_type(((AstNode*)pri_node)->type));
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_int_op(ctx, 0)));
            log_debug("Non-literal constant instruction created");
        }
        log_debug("Primary expression handling completed");
    }
}

static void transpile_mir_binary_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstBinaryNode *bi_node, MIR_reg_t *result_reg) {
    log_debug("transpile MIR binary expr");

    MIR_reg_t left_reg = new_reg(ctx, func, "left", get_mir_type(bi_node->left->type));
    MIR_reg_t right_reg = new_reg(ctx, func, "right", get_mir_type(bi_node->right->type));

    transpile_mir_expr(ctx, func_item, func, bi_node->left, &left_reg);
    transpile_mir_expr(ctx, func_item, func, bi_node->right, &right_reg);

    *result_reg = new_reg(ctx, func, "binary_result", get_mir_type(((AstNode*)bi_node)->type));

    switch (bi_node->op) {
    case OPERATOR_ADD:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DADD,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_ADD,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    case OPERATOR_SUB:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DSUB,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_SUB,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    case OPERATOR_MUL:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DMUL,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MUL,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    case OPERATOR_DIV:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DDIV,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DIV,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    case OPERATOR_EQ:
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_EQ,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_reg_op(ctx, left_reg),
            MIR_new_reg_op(ctx, right_reg)));
        break;

    case OPERATOR_NE:
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_NE,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_reg_op(ctx, left_reg),
            MIR_new_reg_op(ctx, right_reg)));
        break;

    case OPERATOR_LT:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DLT,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_LT,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    case OPERATOR_LE:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DLE,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_LE,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    case OPERATOR_GT:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DGT,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_GT,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    case OPERATOR_GE:
        if (bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DGE,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_GE,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, left_reg),
                MIR_new_reg_op(ctx, right_reg)));
        }
        break;

    default:
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_reg_op(ctx, left_reg)));
        break;
    }
}

static void transpile_mir_unary_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstUnaryNode *un_node, MIR_reg_t *result_reg) {
    log_debug("transpile MIR unary expr");

    MIR_reg_t operand_reg = new_reg(ctx, func, "operand", get_mir_type(un_node->operand->type));
    transpile_mir_expr(ctx, func_item, func, un_node->operand, &operand_reg);

    *result_reg = new_reg(ctx, func, "unary_result", get_mir_type(((AstNode*)un_node)->type));

    switch (un_node->op) {
    case OPERATOR_NEG:
        if (un_node->operand->type->type_id == LMD_TYPE_FLOAT) {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DNEG,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, operand_reg)));
        } else {
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_NEG,
                MIR_new_reg_op(ctx, *result_reg),
                MIR_new_reg_op(ctx, operand_reg)));
        }
        break;

    case OPERATOR_NOT:
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_EQ,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_reg_op(ctx, operand_reg),
            MIR_new_int_op(ctx, 0)));
        break;

    case OPERATOR_IS_ERROR: {
        // ^expr: check if item type is LMD_TYPE_ERROR (type tag in upper 8 bits)
        MIR_reg_t type_reg = new_reg(ctx, func, "type_id", MIR_T_I64);
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_RSH,
            MIR_new_reg_op(ctx, type_reg),
            MIR_new_reg_op(ctx, operand_reg),
            MIR_new_int_op(ctx, 56)));
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_EQ,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_reg_op(ctx, type_reg),
            MIR_new_int_op(ctx, LMD_TYPE_ERROR)));
        break;
    }

    case OPERATOR_POS:
    default:
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_reg_op(ctx, operand_reg)));
        break;
    }
}

static void transpile_mir_ident_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstIdentNode *ident_node, MIR_reg_t *result_reg) {
    log_debug("transpile MIR identifier: %.*s", (int)ident_node->name->len, ident_node->name->chars);

    // For now, just return a placeholder value
    // In a real implementation, we'd look up the variable in a symbol table
    *result_reg = new_reg(ctx, func, "ident_val", get_mir_type(((AstNode*)ident_node)->type));
    MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_reg_op(ctx, *result_reg),
        MIR_new_int_op(ctx, 10))); // Default identifier value
}

void transpile_mir_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstNode *expr_node, MIR_reg_t *result_reg) {
    static int recursion_depth = 0;

    if (recursion_depth > 100) {
        log_error("Maximum recursion depth exceeded in transpile_mir_expr");
        *result_reg = new_reg(ctx, func, "error_expr", MIR_T_I64);
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_int_op(ctx, 0)));
        return;
    }

    recursion_depth++;

    if (!expr_node) {
        log_error("missing expression node");
        *result_reg = new_reg(ctx, func, "null_expr", MIR_T_I64);
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_int_op(ctx, 0)));
        recursion_depth--;
        return;
    }

    log_debug("transpile_mir_expr: node_type=%d, recursion_depth=%d", expr_node->node_type, recursion_depth);

    switch (expr_node->node_type) {
    case AST_NODE_PRIMARY:
        transpile_mir_primary_expr(ctx, func_item, func, (AstPrimaryNode*)expr_node, result_reg);
        break;
    case AST_NODE_BINARY:
        transpile_mir_binary_expr(ctx, func_item, func, (AstBinaryNode*)expr_node, result_reg);
        break;
    case AST_NODE_UNARY:
        transpile_mir_unary_expr(ctx, func_item, func, (AstUnaryNode*)expr_node, result_reg);
        break;
    case AST_NODE_IDENT:
        transpile_mir_ident_expr(ctx, func_item, func, (AstIdentNode*)expr_node, result_reg);
        break;
    default:
        log_error("unsupported expression type: %d", expr_node->node_type);
        *result_reg = new_reg(ctx, func, "unknown_expr", MIR_T_I64);
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV,
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_int_op(ctx, 0)));
        break;
    }

    recursion_depth--;
}

void transpile_mir_ast(MIR_context_t ctx, AstScript *script) {
    log_notice("transpile AST to MIR");

    MIR_module_t module = MIR_new_module(ctx, "lambda_script");

    // Create main function
    MIR_var_t main_vars[] = {
        {MIR_T_P, "rt", 0} // Context *rt parameter
    };
    MIR_type_t main_ret_type = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(ctx, "main", 1, &main_ret_type, 1, main_vars);
    MIR_func_t main_func = MIR_get_item_func(ctx, main_item);

    log_debug("Created MIR function");

    // Initialize result register
    MIR_reg_t main_result = new_reg(ctx, main_func, "main_result", MIR_T_I64);

    // Check if the script has valid content
    if (script && script->child) {
        log_debug("Transpiling script with child node");
        // Transpile the child expression
        transpile_mir_expr(ctx, main_item, main_func, script->child, &main_result);
    } else {
        log_debug("Empty script or no child - using default value");
        // Empty script or no child - return default test value
        MIR_append_insn(ctx, main_item, MIR_new_insn(ctx, MIR_MOV,
            MIR_new_reg_op(ctx, main_result),
            MIR_new_int_op(ctx, 42)));
    }

    MIR_append_insn(ctx, main_item, MIR_new_ret_insn(ctx, 1, MIR_new_reg_op(ctx, main_result)));

    MIR_finish_func(ctx);
    MIR_finish_module(ctx);
}

// Main entry point for MIR compilation
Input* run_script_mir(Runtime *runtime, const char* source, char* script_path, bool run_main) {
    log_notice("Running script with MIR JIT compilation");

    // Initialize runner with the same pattern as run_script()
    Runner runner;
    runner_init(runtime, &runner);

    // Load and parse script
    if (source) {
        // Parse and build AST from source string
        runner.script = load_script(runtime, script_path, source, false);
    } else {
        // Load script from file - pass script_path as both path and source (load_script will read file)
        runner.script = load_script(runtime, script_path, NULL, false);
    }

    if (!runner.script || !runner.script->ast_root) {
        log_error("Failed to parse script");
        // Return Input with error item instead of nullptr
        Pool* error_pool = pool_create();
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            return nullptr;
        }
        output->root = ItemError;
        return output;
    }

    // Initialize MIR context with optimization level from runtime
    unsigned int opt_level = runtime ? runtime->optimize_level : 2;
    MIR_context_t ctx = jit_init(opt_level);

    // Transpile to MIR
    transpile_mir_ast(ctx, (AstScript*)runner.script->ast_root);

    // Generate machine code and link
    MIR_gen_init(ctx);
    // Note: optimize_level is already set in jit_init
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // Find the main function and store it in the Script
    runner.script->main_func = (main_func_t)find_func(ctx, "main");
    if (!runner.script->main_func) {
        log_error("Failed to find main function");
        jit_cleanup(ctx);
        // Return Input with error item
        Pool* error_pool = pool_create();
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            return nullptr;
        }
        output->root = ItemError;
        return output;
    }

    // Now we can use the common execution path
    Input* output = execute_script_and_create_output(&runner, run_main);

    // Cleanup MIR context
    jit_cleanup(ctx);

    // Note: Script must remain alive - it shares the pool with output
    // The caller will eventually destroy the pool, cleaning up both Script and output

    return output;
}
