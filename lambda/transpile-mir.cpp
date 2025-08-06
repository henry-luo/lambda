#include "transpiler.hpp"
#include <mir.h>
#include <mir-gen.h>

extern Type TYPE_ANY, TYPE_INT;
void transpile_mir_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstNode *expr_node, MIR_reg_t *result_reg);

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
    printf("Creating register '%s' of type %d\n", reg_name, type);
    return MIR_new_func_reg(ctx, func, type, reg_name);
}

static void transpile_mir_primary_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstPrimaryNode *pri_node, MIR_reg_t *result_reg) {
    printf("transpile MIR primary expr: pri_node=%p, pri_node->expr=%p\n", pri_node, pri_node->expr);
    
    if (pri_node->expr) {
        printf("Primary node has expr, recursing...\n");
        transpile_mir_expr(ctx, func_item, func, pri_node->expr, result_reg);
    } else { // const
        printf("Primary node is const\n");
        
        // Check if type pointer is valid
        if (!((AstNode*)pri_node)->type) {
            printf("ERROR: Type pointer is null!\n");
            *result_reg = new_reg(ctx, func, "null_type", MIR_T_I64);
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
                MIR_new_reg_op(ctx, *result_reg), 
                MIR_new_int_op(ctx, 42)));
            return;
        }
        
        printf("Type valid, type_id=%d, is_literal=%d\n", ((AstNode*)pri_node)->type->type_id, ((AstNode*)pri_node)->type->is_literal);
        
        if (((AstNode*)pri_node)->type->is_literal) {  // literal
            printf("Processing literal value\n");
            if (((AstNode*)pri_node)->type->type_id == LMD_TYPE_INT || ((AstNode*)pri_node)->type->type_id == LMD_TYPE_INT64) {
                printf("Creating integer literal\n");
                *result_reg = new_reg(ctx, func, "int_const", MIR_T_I64);
                printf("Created register for integer: %u\n", *result_reg);
                // For now, extract some constant value - in real implementation we'd parse from the source
                long value = 42; // Default test value
                printf("Creating MIR instruction with value %ld...\n", value);
                
                // Create the instruction parts separately for debugging
                MIR_op_t reg_op = MIR_new_reg_op(ctx, *result_reg);
                printf("Created register operand\n");
                MIR_op_t int_op = MIR_new_int_op(ctx, value);
                printf("Created integer operand\n");
                MIR_insn_t insn = MIR_new_insn(ctx, MIR_MOV, reg_op, int_op);
                printf("Created MIR MOV instruction\n");
                
                printf("About to append instruction to function...\n");
                MIR_append_insn(ctx, func_item, insn);
                printf("Integer literal instruction created\n");
            }
            else if (((AstNode*)pri_node)->type->type_id == LMD_TYPE_FLOAT) {
                printf("Creating float literal\n");
                *result_reg = new_reg(ctx, func, "float_const", MIR_T_D);
                double value = 3.14; // Default test value
                MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_DMOV, 
                    MIR_new_reg_op(ctx, *result_reg), 
                    MIR_new_double_op(ctx, value)));
                printf("Float literal instruction created\n");
            }
            else if (((AstNode*)pri_node)->type->type_id == LMD_TYPE_BOOL) {
                printf("Creating boolean literal\n");
                *result_reg = new_reg(ctx, func, "bool_const", MIR_T_I64);
                MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
                    MIR_new_reg_op(ctx, *result_reg), 
                    MIR_new_int_op(ctx, 1))); // true
                printf("Boolean literal instruction created\n");
            }
            else { // null, other types
                printf("Creating other literal type\n");
                *result_reg = new_reg(ctx, func, "null_const", MIR_T_I64);
                MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
                    MIR_new_reg_op(ctx, *result_reg), 
                    MIR_new_int_op(ctx, 0)));
                printf("Other literal instruction created\n");
            }
        } else {
            printf("Processing non-literal constant\n");
            *result_reg = new_reg(ctx, func, "const_val", get_mir_type(((AstNode*)pri_node)->type));
            MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
                MIR_new_reg_op(ctx, *result_reg), 
                MIR_new_int_op(ctx, 0)));
            printf("Non-literal constant instruction created\n");
        }
        printf("Primary expression handling completed\n");
    }
}

static void transpile_mir_binary_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstBinaryNode *bi_node, MIR_reg_t *result_reg) {
    printf("transpile MIR binary expr\n");
    
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
    printf("transpile MIR unary expr\n");
    
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
        
    case OPERATOR_POS:
    default:
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
            MIR_new_reg_op(ctx, *result_reg),
            MIR_new_reg_op(ctx, operand_reg)));
        break;
    }
}

static void transpile_mir_ident_expr(MIR_context_t ctx, MIR_item_t func_item, MIR_func_t func, AstIdentNode *ident_node, MIR_reg_t *result_reg) {
    printf("transpile MIR identifier: %.*s\n", (int)ident_node->name.length, ident_node->name.str);
    
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
        printf("ERROR: Maximum recursion depth exceeded in transpile_mir_expr\n");
        *result_reg = new_reg(ctx, func, "error_expr", MIR_T_I64);
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
            MIR_new_reg_op(ctx, *result_reg), 
            MIR_new_int_op(ctx, 0)));
        return;
    }
    
    recursion_depth++;
    
    if (!expr_node) {
        printf("missing expression node\n");
        *result_reg = new_reg(ctx, func, "null_expr", MIR_T_I64);
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
            MIR_new_reg_op(ctx, *result_reg), 
            MIR_new_int_op(ctx, 0)));
        recursion_depth--;
        return;
    }
    
    printf("transpile_mir_expr: node_type=%d, recursion_depth=%d\n", expr_node->node_type, recursion_depth);
    
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
        printf("unsupported expression type: %d\n", expr_node->node_type);
        *result_reg = new_reg(ctx, func, "unknown_expr", MIR_T_I64);
        MIR_append_insn(ctx, func_item, MIR_new_insn(ctx, MIR_MOV, 
            MIR_new_reg_op(ctx, *result_reg), 
            MIR_new_int_op(ctx, 0)));
        break;
    }
    
    recursion_depth--;
}

void transpile_mir_ast(MIR_context_t ctx, AstScript *script) {
    printf("transpile AST to MIR\n");
    
    MIR_module_t module = MIR_new_module(ctx, "lambda_script");
    
    // Create main function
    MIR_var_t main_vars[] = {
        {MIR_T_P, "rt", 0} // Context *rt parameter
    };
    MIR_type_t main_ret_type = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(ctx, "main", 1, &main_ret_type, 1, main_vars);
    MIR_func_t main_func = MIR_get_item_func(ctx, main_item);
    
    printf("Created MIR function\n");
    
    // Initialize result register
    MIR_reg_t main_result = new_reg(ctx, main_func, "main_result", MIR_T_I64);
    
    // Check if the script has valid content
    if (script && script->child) {
        printf("Transpiling script with child node\n");
        // Transpile the child expression
        transpile_mir_expr(ctx, main_item, main_func, script->child, &main_result);
    } else {
        printf("Empty script or no child - using default value\n");
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
Item run_script_mir(Runtime *runtime, const char* source, char* script_path) {
    printf("Running script with MIR JIT compilation\n");
    
    Script* script;
    if (source) {
        // Parse and build AST from source string
        script = load_script(runtime, script_path, source);
    } else {
        // Load script from file - pass script_path as both path and source (load_script will read file)
        script = load_script(runtime, script_path, NULL);
    }
    
    if (!script || !script->ast_root) {
        printf("Failed to parse script\n");
        return ItemError;
    }
    
    // Initialize MIR context
    MIR_context_t ctx = jit_init();
    
    // Transpile to MIR
    transpile_mir_ast(ctx, (AstScript*)script->ast_root);
    
    // Generate machine code and execute
    MIR_gen_init(ctx);
    MIR_gen_set_optimize_level(ctx, 2);
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);
    
    // Find and execute main function
    main_func_t main_fn = (main_func_t)find_func(ctx, "main");
    if (!main_fn) {
        printf("Failed to find main function\n");
        jit_cleanup(ctx);
        return ItemError;
    }
    
    // Create a simple context for execution
    Context exec_ctx = {0};
    Item result = main_fn(&exec_ctx);
    
    jit_cleanup(ctx);
    return result;
}
