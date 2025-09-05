#include "transpiler.hpp"
#include "../lib/log.h"

extern Type TYPE_ANY, TYPE_INT;
void transpile_expr(Transpiler* tp, AstNode *expr_node);
void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer);
Type* build_lit_string(Transpiler* tp, TSNode node, TSSymbol symbol);
Type* build_lit_datetime(Transpiler* tp, TSNode node, TSSymbol symbol);

void write_node_source(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    const char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    strbuf_append_char(strbuf, '_');
    if (fn_node->name && fn_node->name->chars) {
        strbuf_append_str_n(strbuf, fn_node->name->chars, fn_node->name->len);
    } else {
        strbuf_append_char(strbuf, 'f');
    }
    // char offset ensures the fn name is unique across the script
    strbuf_append_int(strbuf, ts_node_start_byte(fn_node->node));
    // no need to add param cnt
    // strbuf_append_char(tp->code_buf, '_');
    // strbuf_append_int(tp->code_buf, ((TypeFunc*)fn_node->type)->param_count);    
}

void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    // user var name starts with '_'
    strbuf_append_char(strbuf, '_');
    strbuf_append_str_n(strbuf, asn_node->name->chars, asn_node->name->len);
}

// Helper function to determine if an expression produces an Item result
bool expr_produces_item(AstNode* node) {
    if (!node) return false;
    
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bin_node = (AstBinaryNode*)node;
        // Division operations always produce Item results
        if (bin_node->op == OPERATOR_DIV || bin_node->op == OPERATOR_IDIV || 
            bin_node->op == OPERATOR_MOD || bin_node->op == OPERATOR_POW) {
            return true;
        }
        // If any operand produces an Item, the whole expression needs to use runtime functions
        if (expr_produces_item(bin_node->left) || expr_produces_item(bin_node->right)) {
            return true;
        }
        // Mixed type operations often produce Items
        if (bin_node->left->type && bin_node->right->type) {
            bool left_numeric = (LMD_TYPE_INT <= bin_node->left->type->type_id && bin_node->left->type->type_id <= LMD_TYPE_FLOAT);
            bool right_numeric = (LMD_TYPE_INT <= bin_node->right->type->type_id && bin_node->right->type->type_id <= LMD_TYPE_FLOAT);
            if (left_numeric && right_numeric && bin_node->left->type->type_id != bin_node->right->type->type_id) {
                return true; // Mixed int/float operations
            }
        }
    }
    else if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri_node = (AstPrimaryNode*)node;
        if (pri_node->expr) {
            return expr_produces_item(pri_node->expr);
        }
    }
    
    return false;
}

void transpile_box_item(Transpiler* tp, AstNode *item) {
    if (!item->type) {
        log_debug("transpile box item: NULL type, node_type: %d", item->node_type);
        return;
    }
    log_debug("transpile box item: %d", item->type->type_id);
        
    // Debug: if this is a primary node with an identifier, show more info
    if (item->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)item;
        if (pri->expr && pri->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident = (AstIdentNode*)pri->expr;
            log_debug("transpile_box_item: identifier found: %.*s", 
                (int)ident->name->len, ident->name->chars);
            if (ident->entry && ident->entry->node) {
                log_debug("transpile_box_item: identifier entry type: %d", 
                    ident->entry->node->type->type_id);
            }
        }
    }
    switch (item->type->type_id) {
    case LMD_TYPE_NULL:
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "b2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_INT: {
        // Check if this is a variable that was declared as Item due to type coercion
        if (item->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)item;
            if (pri->expr && pri->expr->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
                if (ident_node->entry && ident_node->entry->node && 
                    ident_node->entry->node->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* assign_node = (AstNamedNode*)ident_node->entry->node;
                    
                    // Check if this assignment expression needs boxing (same logic as in transpile_assign_expr)
                    bool expression_needs_boxing = false;
                    if (assign_node->as && assign_node->as->node_type == AST_NODE_IF_EXPR) {
                        AstIfNode *if_node = (AstIfNode*)assign_node->as;
                        Type* then_type = if_node->then ? if_node->then->type : nullptr;
                        Type* else_type = if_node->otherwise ? if_node->otherwise->type : nullptr;
                        
                        if (then_type && else_type) {
                            bool then_is_null = (then_type->type_id == LMD_TYPE_NULL);
                            bool else_is_null = (else_type->type_id == LMD_TYPE_NULL);
                            bool then_is_int = (then_type->type_id == LMD_TYPE_INT);
                            bool else_is_int = (else_type->type_id == LMD_TYPE_INT);  
                            bool then_is_string = (then_type->type_id == LMD_TYPE_STRING);
                            bool else_is_string = (else_type->type_id == LMD_TYPE_STRING);
                            
                            Type *expr_type = assign_node->as->type;
                            if ((then_is_null && (else_is_string || else_is_int)) ||
                                (else_is_null && (then_is_string || then_is_int)) ||
                                (then_is_int && else_is_string) ||
                                (then_is_string && else_is_int) ||
                                (expr_type && expr_type->type_id == LMD_TYPE_ANY)) {
                                expression_needs_boxing = true;
                            }
                        }
                    }
                    
                    if (expression_needs_boxing) {
                        // This variable was declared as Item, so just output the variable name without boxing
                        log_debug("transpile_box_item: int variable declared as Item, no boxing needed");
                        transpile_expr(tp, item);
                        break;
                    }
                }
            }
        }
        
        // Special case: if this is a binary expression with OPERATOR_POW, OPERATOR_DIV, OPERATOR_IDIV, or OPERATOR_MOD,
        // don't wrap with i2it because these functions already return an Item
        if (item->node_type == AST_NODE_BINARY) {
            AstBinaryNode* bin_node = (AstBinaryNode*)item;
            if (bin_node->op == OPERATOR_POW || bin_node->op == OPERATOR_DIV || 
                bin_node->op == OPERATOR_IDIV || bin_node->op == OPERATOR_MOD) {
                transpile_expr(tp, item);
                break;
            }
        }
        
        strbuf_append_str(tp->code_buf, "i2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    }
    case LMD_TYPE_INT64: 
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_l2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            log_enter();
            log_debug("transpile_box_item: push_l");
            strbuf_append_str(tp->code_buf, "push_l(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
            log_leave();
        }
        break;
    case LMD_TYPE_FLOAT: 
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_d2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            // Special case: if this is a binary expression that uses runtime functions,
            // don't wrap with push_d because these functions already return an Item
            bool skip_push_d = false;
            if (item->node_type == AST_NODE_BINARY) {
                AstBinaryNode* bin_node = (AstBinaryNode*)item;
                if (bin_node->op == OPERATOR_POW || bin_node->op == OPERATOR_DIV || 
                    bin_node->op == OPERATOR_IDIV || bin_node->op == OPERATOR_MOD) {
                    skip_push_d = true;
                }
                // Also skip for ADD/SUB/MUL when they use runtime functions (mixed types or complex expressions)
                else if ((bin_node->op == OPERATOR_ADD || bin_node->op == OPERATOR_SUB || 
                          bin_node->op == OPERATOR_MUL) &&
                         (expr_produces_item(bin_node->left) || expr_produces_item(bin_node->right))) {
                    skip_push_d = true;
                }
            }
            // Also check if this is a primary expression containing operations that use runtime functions
            else if (item->node_type == AST_NODE_PRIMARY) {
                AstPrimaryNode* pri_node = (AstPrimaryNode*)item;
                if (pri_node->expr && pri_node->expr->node_type == AST_NODE_BINARY) {
                    AstBinaryNode* bin_node = (AstBinaryNode*)pri_node->expr;
                    if (bin_node->op == OPERATOR_POW || bin_node->op == OPERATOR_DIV || 
                        bin_node->op == OPERATOR_IDIV || bin_node->op == OPERATOR_MOD) {
                        skip_push_d = true;
                    }
                    // Also skip for ADD/SUB/MUL when they use runtime functions
                    else if ((bin_node->op == OPERATOR_ADD || bin_node->op == OPERATOR_SUB || 
                              bin_node->op == OPERATOR_MUL) &&
                             (expr_produces_item(bin_node->left) || expr_produces_item(bin_node->right))) {
                        skip_push_d = true;
                    }
                }
            }
            
            if (skip_push_d) {
                transpile_expr(tp, item);
            } else {
                strbuf_append_str(tp->code_buf, "push_d(");
                transpile_expr(tp, item);
                strbuf_append_char(tp->code_buf, ')');
            }
        }
        break;
    case LMD_TYPE_DTIME: 
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_k2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            // non-literal datetime expression
            strbuf_append_str(tp->code_buf, "push_k(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');            
        }
        break;
    case LMD_TYPE_DECIMAL: 
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_c2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            // non-literal decimal expression
            strbuf_append_str(tp->code_buf, "c2it(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    case LMD_TYPE_NUMBER:
        // NUMBER type is a union of int/float - transpile the expression directly
        transpile_expr(tp, item);
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        char t = item->type->type_id == LMD_TYPE_STRING ? 's' :
            item->type->type_id == LMD_TYPE_SYMBOL ? 'y' : 
            item->type->type_id == LMD_TYPE_BINARY ? 'x':'k';
        if (item->type->is_literal) {
            strbuf_append_format(tp->code_buf, "const_%c2it(", t);
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            // Check if this is actually a null literal that got mistyped as string
            if (item->node_type == AST_NODE_PRIMARY) {
                AstPrimaryNode* pri = (AstPrimaryNode*)item;
                if (!pri->expr && pri->type->is_literal) {
                    // This is a literal - check if it's actually null
                    StrView source = ts_node_source(tp, pri->node);
                    if (source.length == 4 && strncmp(source.str, "null", 4) == 0) {
                        // This is a null literal mistyped as string - use ITEM_NULL instead
                        strbuf_append_str(tp->code_buf, "ITEM_NULL");
                        break;
                    }
                }
                
                // Check if this is a variable that was declared as Item due to type coercion
                if (pri->expr && pri->expr->node_type == AST_NODE_IDENT) {
                    AstIdentNode* ident_node = (AstIdentNode*)pri->expr;
                    if (ident_node->entry && ident_node->entry->node && 
                        ident_node->entry->node->node_type == AST_NODE_ASSIGN) {
                        AstNamedNode* assign_node = (AstNamedNode*)ident_node->entry->node;
                        
                        // Check if this assignment expression needs boxing (same logic as in transpile_assign_expr)
                        bool expression_needs_boxing = false;
                        if (assign_node->as && assign_node->as->node_type == AST_NODE_IF_EXPR) {
                            AstIfNode *if_node = (AstIfNode*)assign_node->as;
                            Type* then_type = if_node->then ? if_node->then->type : nullptr;
                            Type* else_type = if_node->otherwise ? if_node->otherwise->type : nullptr;
                            
                            if (then_type && else_type) {
                                bool then_is_null = (then_type->type_id == LMD_TYPE_NULL);
                                bool else_is_null = (else_type->type_id == LMD_TYPE_NULL);
                                bool then_is_int = (then_type->type_id == LMD_TYPE_INT);
                                bool else_is_int = (else_type->type_id == LMD_TYPE_INT);  
                                bool then_is_string = (then_type->type_id == LMD_TYPE_STRING);
                                bool else_is_string = (else_type->type_id == LMD_TYPE_STRING);
                                
                                Type *expr_type = assign_node->as->type;
                                if ((then_is_null && (else_is_string || else_is_int)) ||
                                    (else_is_null && (then_is_string || then_is_int)) ||
                                    (then_is_int && else_is_string) ||
                                    (then_is_string && else_is_int) ||
                                    (expr_type && expr_type->type_id == LMD_TYPE_ANY)) {
                                    expression_needs_boxing = true;
                                }
                            }
                        }
                        
                        if (expression_needs_boxing) {
                            // This variable was declared as Item, so just output the variable name without boxing
                            log_debug("transpile_box_item: variable declared as Item, no boxing needed");
                            transpile_expr(tp, item);
                            break;
                        }
                    }
                }
            }
            
            strbuf_append_format(tp->code_buf, "%c2it(", t);
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    }
    case LMD_TYPE_LIST:  case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_TYPE:
        transpile_expr(tp, item);  // raw pointer
        break;
    case LMD_TYPE_FUNC:
        strbuf_append_str(tp->code_buf, "to_fn(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_ANY:
        transpile_expr(tp, item);  // no boxing needed
        break;
    default:
        log_debug("unknown box item type: %d", item->type->type_id);
    }
}

void push_list_items(Transpiler* tp, AstNode *item, bool is_elmt) {
    while (item) {
        // skip let declaration
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || 
            item->node_type == AST_NODE_FUNC) {
            item = item->next;  continue;
        }
        strbuf_append_format(tp->code_buf, " list_push(%s, ", is_elmt ? "el" : "ls");
        transpile_box_item(tp, item);
        strbuf_append_str(tp->code_buf, ");\n");
        item = item->next;
    }
    strbuf_append_format(tp->code_buf, " list_end(%s);})", is_elmt ? "el" : "ls");
}

void transpile_primary_expr(Transpiler* tp, AstPrimaryNode *pri_node) {
    if (pri_node->expr) {
        if (pri_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri_node->expr;
            log_debug("transpile_primary_expr: identifier %.*s, type: %d", 
                (int)ident_node->name->len, ident_node->name->chars, pri_node->type->type_id);
                
            if (ident_node->entry && ident_node->entry->node && 
                ident_node->entry->node->node_type == AST_NODE_FUNC) {
                write_fn_name(tp->code_buf, (AstFuncNode*)ident_node->entry->node, 
                    (AstImportNode*)ident_node->entry->import);
            }
            else if (ident_node->entry && ident_node->entry->node) {
                log_debug("transpile_primary_expr: writing var name for %.*s, entry type: %d",
                    (int)ident_node->name->len, ident_node->name->chars,
                    ident_node->entry->node->type->type_id);
                
                // For decimal identifiers, we need to convert the pointer to an Item
                if (ident_node->entry->node->type->type_id == LMD_TYPE_DECIMAL) {
                    strbuf_append_str(tp->code_buf, "c2it(");
                    write_var_name(tp->code_buf, (AstNamedNode*)ident_node->entry->node, 
                        (AstImportNode*)ident_node->entry->import);
                    strbuf_append_char(tp->code_buf, ')');
                } else {
                    write_var_name(tp->code_buf, (AstNamedNode*)ident_node->entry->node, 
                        (AstImportNode*)ident_node->entry->import);
                }
            }
            else {
                log_warn("Warning: ident_node->entry is null or entry->node is null");
                // Handle the case where entry is null - perhaps write the identifier directly
                write_node_source(tp, ident_node->node);
            }
        } else { 
            transpile_expr(tp, pri_node->expr);
        }
    } else { // const
        log_debug("transpile_primary_expr: const");
        if (pri_node->type->is_literal) {  // literal
            if (pri_node->type->type_id == LMD_TYPE_STRING || pri_node->type->type_id == LMD_TYPE_SYMBOL ||
                pri_node->type->type_id == LMD_TYPE_BINARY) {
                // loads the const string without boxing
                strbuf_append_str(tp->code_buf, "const_s(");
                TypeString *str_type = (TypeString*)pri_node->type;
                strbuf_append_int(tp->code_buf, str_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_DTIME) {
                // loads the const datetime without boxing
                strbuf_append_str(tp->code_buf, "const_k(");
                TypeDateTime *dt_type = (TypeDateTime*)pri_node->type;
                strbuf_append_int(tp->code_buf, dt_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_INT) {
                write_node_source(tp, pri_node->node);
                // int32 literals don't use 'L' suffix
            }
            else if (pri_node->type->type_id == LMD_TYPE_INT64) {
                write_node_source(tp, pri_node->node);
                strbuf_append_char(tp->code_buf, 'L');  // add 'L' to ensure it is a long
            }
            else if (pri_node->type->type_id == LMD_TYPE_DECIMAL) {
                // loads the const decimal without boxing
                strbuf_append_str(tp->code_buf, "const_c2it(");
                TypeDecimal *dec_type = (TypeDecimal*)pri_node->type;
                strbuf_append_int(tp->code_buf, dec_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else { // bool, null, float
                TSNode child = ts_node_named_child(pri_node->node, 0);
                TSSymbol symbol = ts_node_symbol(child);
                write_node_source(tp, pri_node->node);
            }
        } else {
            write_node_source(tp, pri_node->node);
        }
    }
}

void transpile_unary_expr(Transpiler* tp, AstUnaryNode *unary_node) {
    log_debug("transpile unary expr");
    if (unary_node->op == OPERATOR_NOT) {
        TypeId operand_type = unary_node->operand->type->type_id;
        if (operand_type == LMD_TYPE_BOOL) {
            // direct C negation for boolean values
            strbuf_append_str(tp->code_buf, "!");
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            strbuf_append_str(tp->code_buf, "fn_not(");
            transpile_box_item(tp, unary_node->operand);  
            strbuf_append_str(tp->code_buf, ")");
        }
    }
    else if (unary_node->op == OPERATOR_POS || unary_node->op == OPERATOR_NEG) {
        TypeId operand_type = unary_node->operand->type->type_id;
        
        // Fast path for numeric types that can be handled directly by C
        if (operand_type == LMD_TYPE_INT || operand_type == LMD_TYPE_INT64 || 
            operand_type == LMD_TYPE_FLOAT) {
            // Direct C operator application for primitive numeric types
            if (unary_node->op == OPERATOR_POS) {
                // Unary + can be optimized away for numeric types
                strbuf_append_char(tp->code_buf, '(');
                transpile_expr(tp, unary_node->operand);
                strbuf_append_char(tp->code_buf, ')');
            } else { // OPERATOR_NEG
                // two brackets to prevent '-' joining into '--'
                strbuf_append_str(tp->code_buf, "(-(");
                transpile_expr(tp, unary_node->operand);
                strbuf_append_str(tp->code_buf, "))");
            }
        }
        else {
            // Runtime function call for other types (ANY, DECIMAL, etc.)
            if (unary_node->op == OPERATOR_POS) {
                strbuf_append_str(tp->code_buf, "fn_pos(");
            } else { // OPERATOR_NEG
                strbuf_append_str(tp->code_buf, "fn_neg(");
            }
            transpile_box_item(tp, unary_node->operand);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else {
        // Fallback for unknown operators (should not happen with proper AST)
        log_error("Error: transpile_unary_expr unknown operator %d", unary_node->op);
        strbuf_append_str(tp->code_buf, "null");
    }
}

void transpile_binary_expr(Transpiler* tp, AstBinaryNode *bi_node) {
    if (bi_node->op == OPERATOR_AND || bi_node->op == OPERATOR_OR) {
        // Check if we need type error checking for mixed types
        TypeId left_type = bi_node->left->type->type_id;
        TypeId right_type = bi_node->right->type->type_id;        
        if (left_type != LMD_TYPE_BOOL || right_type != LMD_TYPE_BOOL) {
            if (bi_node->op == OPERATOR_AND) {
                strbuf_append_str(tp->code_buf, "fn_and(");
            } else {
                strbuf_append_str(tp->code_buf, "fn_or(");
            }
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // slightly faster path
            if (bi_node->op == OPERATOR_AND) {
                strbuf_append_str(tp->code_buf, "op_and(");
            } else {
                strbuf_append_str(tp->code_buf, "op_or(");
            }
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');            
        }
    }
    else if (bi_node->op == OPERATOR_POW) {
        strbuf_append_str(tp->code_buf, "fn_pow(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_ADD) {
        // Check if any operand produces an Item result - if so, use runtime function
        if (expr_produces_item(bi_node->left) || expr_produces_item(bi_node->right)) {
            strbuf_append_str(tp->code_buf, "fn_add(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        
        if (bi_node->left->type->type_id == bi_node->right->type->type_id) {
            if (bi_node->left->type->type_id == LMD_TYPE_STRING) {
                strbuf_append_str(tp->code_buf, "fn_strcat(");
                transpile_expr(tp, bi_node->left);
                strbuf_append_char(tp->code_buf, ',');
                transpile_expr(tp, bi_node->right);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }
            else if (bi_node->left->type->type_id == LMD_TYPE_INT || 
                bi_node->left->type->type_id == LMD_TYPE_INT64 ||
                bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "(");
                transpile_expr(tp, bi_node->left);
                strbuf_append_char(tp->code_buf, '+');
                transpile_expr(tp, bi_node->right);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }
            // else error
        }
        else if (LMD_TYPE_INT <= bi_node->left->type->type_id && bi_node->left->type->type_id <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= bi_node->right->type->type_id && bi_node->right->type->type_id <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '+');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_add()
        strbuf_append_str(tp->code_buf, "fn_add(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_SUB) {
        // Check if any operand produces an Item result - if so, use runtime function
        if (expr_produces_item(bi_node->left) || expr_produces_item(bi_node->right)) {
            strbuf_append_str(tp->code_buf, "fn_sub(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        
        if (bi_node->left->type->type_id == bi_node->right->type->type_id) {
            if (bi_node->left->type->type_id == LMD_TYPE_INT || 
                bi_node->left->type->type_id == LMD_TYPE_INT64 ||
                bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "(");
                transpile_expr(tp, bi_node->left);
                strbuf_append_char(tp->code_buf, '-');
                transpile_expr(tp, bi_node->right);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }
            // else error
        }
        else if (LMD_TYPE_INT <= bi_node->left->type->type_id && bi_node->left->type->type_id <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= bi_node->right->type->type_id && bi_node->right->type->type_id <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '-');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime fn_sub()
        strbuf_append_str(tp->code_buf, "fn_sub(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_MUL) {
        // Check if both operands are simple (primary expressions, not complex expressions)
        bool left_is_simple = (bi_node->left->node_type == AST_NODE_PRIMARY);
        bool right_is_simple = (bi_node->right->node_type == AST_NODE_PRIMARY);
        
        if (left_is_simple && right_is_simple) {
            if (bi_node->left->type->type_id == bi_node->right->type->type_id) {
                if (bi_node->left->type->type_id == LMD_TYPE_INT || 
                    bi_node->left->type->type_id == LMD_TYPE_INT64 ||
                    bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
                    strbuf_append_str(tp->code_buf, "(");
                    transpile_expr(tp, bi_node->left);
                    strbuf_append_char(tp->code_buf, '*');
                    transpile_expr(tp, bi_node->right);
                    strbuf_append_char(tp->code_buf, ')');
                    return;
                }
            }
            else if (LMD_TYPE_INT <= bi_node->left->type->type_id && bi_node->left->type->type_id <= LMD_TYPE_FLOAT &&
                LMD_TYPE_INT <= bi_node->right->type->type_id && bi_node->right->type->type_id <= LMD_TYPE_FLOAT) {
                strbuf_append_char(tp->code_buf, '(');
                transpile_expr(tp, bi_node->left);
                strbuf_append_char(tp->code_buf, '*');
                transpile_expr(tp, bi_node->right);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }
        }
        // call runtime fn_mul()
        strbuf_append_str(tp->code_buf, "fn_mul(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_MOD) {
        // Always call runtime fn_mod() for proper error handling (division by zero, type checking)
        strbuf_append_str(tp->code_buf, "fn_mod(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_DIV) {
        if (bi_node->left->type->type_id == bi_node->right->type->type_id) {
            if (bi_node->left->type->type_id == LMD_TYPE_INT || 
                bi_node->left->type->type_id == LMD_TYPE_INT64 ||
                bi_node->left->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "push_d(((double)(");
                transpile_expr(tp, bi_node->left);
                strbuf_append_str(tp->code_buf, ")/(double)(");
                transpile_expr(tp, bi_node->right);
                strbuf_append_str(tp->code_buf, ")))");
                return;
            }
            // else error
        }
        else if (LMD_TYPE_INT <= bi_node->left->type->type_id && bi_node->left->type->type_id <= LMD_TYPE_FLOAT &&
            LMD_TYPE_INT <= bi_node->right->type->type_id && bi_node->right->type->type_id <= LMD_TYPE_FLOAT) {
            strbuf_append_str(tp->code_buf, "push_d(((double)(");
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, ")/(double)(");
            transpile_expr(tp, bi_node->right);
            strbuf_append_str(tp->code_buf, ")))");
            return;
        }
        // call runtime fn_div()
        strbuf_append_str(tp->code_buf, "fn_div(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_IDIV) {
        // Always call runtime fn_idiv() for proper error handling
        strbuf_append_str(tp->code_buf, "fn_idiv(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_IS) {
        strbuf_append_str(tp->code_buf, "fn_is(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');   
    }
    else if (bi_node->op == OPERATOR_IN) {
        strbuf_append_str(tp->code_buf, "fn_in(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');   
    }
    else if (bi_node->op == OPERATOR_TO) {
        strbuf_append_str(tp->code_buf, "fn_to(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');   
    }
    else if (bi_node->op == OPERATOR_EQ) {
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_eq(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            // todo: need to split into cases, and handle diff type differently
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " == ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_NE) {
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);        
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_ne(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible non-string types
            // todo: need to split into cases, and handle diff type differently
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " != ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_LT) {
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_lt(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " < ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_LE) {
        // Check if we need to use the runtime comparison function for type checking
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_le(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " <= ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_GT) {
        // Check if we need to use the runtime comparison function for type checking
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_gt(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " > ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (bi_node->op == OPERATOR_GE) {
        // Check if we need to use the runtime comparison function for type checking
        // TypeId left_type = bi_node->left->type->type_id;
        // TypeId right_type = bi_node->right->type->type_id;
        // because of encoding of error value, it is very hard to have a fast path
        bool has_fast_path = false; // (left_type == right_type && left_type != LMD_TYPE_STRING);
        if (!has_fast_path) {
            strbuf_append_str(tp->code_buf, "fn_ge(");
            transpile_box_item(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use direct C comparison for compatible types
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_str(tp->code_buf, " >= ");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
    }    
    else {
        log_error("Error: unknown binary operator %d", bi_node->op);
        strbuf_append_str(tp->code_buf, "null");  // should be error
    }
}

// for both if_expr and if_stam
void transpile_if(Transpiler* tp, AstIfNode *if_node) {
    log_debug("transpile if expr");
    
    // Check types for proper conditional expression handling
    Type* if_type = if_node->type;
    Type* then_type = if_node->then ? if_node->then->type : nullptr;
    Type* else_type = if_node->otherwise ? if_node->otherwise->type : nullptr;
    
    strbuf_append_str(tp->code_buf, "(");
    if (if_node->cond->type && if_node->cond->type->type_id == LMD_TYPE_BOOL) {
        transpile_expr(tp, if_node->cond);
    }
    else {
        strbuf_append_str(tp->code_buf, "is_truthy(");
        transpile_box_item(tp, if_node->cond);
        strbuf_append_char(tp->code_buf, ')');
    }
    strbuf_append_str(tp->code_buf, " ? ");

    // Determine if branches have incompatible types that need coercion
    bool need_boxing = true;
    if (then_type && else_type && (then_type->type_id == else_type->type_id)) {
        need_boxing = false;
    }
    if (need_boxing) {
        log_debug("transpile if expr with boxing");
        if (if_node->then) {
            transpile_box_item(tp, if_node->then);
        } else {
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        strbuf_append_str(tp->code_buf, " : ");
        if (if_node->otherwise) {
            transpile_box_item(tp, if_node->otherwise);
        } else {
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        strbuf_append_str(tp->code_buf, ")");
    } else {
        // fast path without boxing
        log_debug("transpile if expr without boxing");
        transpile_expr(tp, if_node->then);
        strbuf_append_char(tp->code_buf, ':');
        if (if_node->otherwise) {
            transpile_expr(tp, if_node->otherwise);
        } else {
            log_warn("Warning: if_stam missing else clause");
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        strbuf_append_char(tp->code_buf, ')');
    }
    log_debug("end if expr");
}

void transpile_assign_expr(Transpiler* tp, AstNamedNode *asn_node) {
        log_debug("transpile assign expr");
    // Defensive validation: ensure all required pointers and components are valid
    if (!asn_node) {
        log_error("Error: transpile_assign_expr called with null assign node");
        strbuf_append_str(tp->code_buf, "\n /* invalid assignment */");
        return;
    }
    if (!asn_node->type) {
        log_error("Error: transpile_assign_expr missing type information");
        strbuf_append_str(tp->code_buf, "\n /* assignment missing type */");
        return;
    }
    if (!asn_node->as) {
        log_error("Error: transpile_assign_expr missing assignment expression");
        strbuf_append_str(tp->code_buf, "\n /* assignment missing expression */");
        return;
    }
    
    strbuf_append_str(tp->code_buf, "\n ");
    
    // Check if the assigned expression requires type coercion (will be boxed to Item)
    bool expression_needs_boxing = false;
    Type *expr_type = asn_node->as->type;
    
    // Check if this is a conditional expression that needs coercion
    if (asn_node->as->node_type == AST_NODE_IF_EXPR) {
        AstIfNode *if_node = (AstIfNode*)asn_node->as;
        Type* then_type = if_node->then ? if_node->then->type : nullptr;
        Type* else_type = if_node->otherwise ? if_node->otherwise->type : nullptr;
        
        if (then_type && else_type) {
            bool then_is_null = (then_type->type_id == LMD_TYPE_NULL);
            bool else_is_null = (else_type->type_id == LMD_TYPE_NULL);
            bool then_is_int = (then_type->type_id == LMD_TYPE_INT);
            bool else_is_int = (else_type->type_id == LMD_TYPE_INT);  
            bool then_is_string = (then_type->type_id == LMD_TYPE_STRING);
            bool else_is_string = (else_type->type_id == LMD_TYPE_STRING);
            
            // These combinations cause type coercion to Item:
            if ((then_is_null && (else_is_string || else_is_int)) ||
                (else_is_null && (then_is_string || then_is_int)) ||
                (then_is_int && else_is_string) ||
                (then_is_string && else_is_int) ||
                (expr_type && expr_type->type_id == LMD_TYPE_ANY)) {
                expression_needs_boxing = true;
            }
        }
    }
    
    // Declare the variable type - use Item if expression needs boxing, original type otherwise
    Type *var_type = asn_node->type;
    if (expression_needs_boxing) {
        // Override the variable type to Item when the expression will be boxed
        strbuf_append_str(tp->code_buf, "Item");
        log_debug("transpile_assign_expr: using Item type due to expression boxing");
    } else {
        write_type(tp->code_buf, var_type);
    }
    
    strbuf_append_char(tp->code_buf, ' ');
    write_var_name(tp->code_buf, asn_node, NULL);
    strbuf_append_char(tp->code_buf, '=');

    transpile_expr(tp, asn_node->as);
    strbuf_append_char(tp->code_buf, ';');
}

void transpile_let_stam(Transpiler* tp, AstLetNode *let_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!let_node) {
        log_error("Error: transpile_let_stam called with null let node");
        return;
    }
    
    AstNode *declare = let_node->declare;
    while (declare) {
        // Additional validation for each declaration node
        if (declare->node_type != AST_NODE_ASSIGN) {
        log_error("Error: transpile_let_stam found non-assign node in declare chain");
            // Skip this node and continue - defensive recovery
            declare = declare->next;
            continue;
        }
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        declare = declare->next;
    }
}

void transpile_loop_expr(Transpiler* tp, AstNamedNode *loop_node, AstNode* then) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!loop_node) {
        log_error("Error: transpile_loop_expr called with null loop node");
        return;
    }
    if (!loop_node->as || !loop_node->as->type) {
        log_error("Error: transpile_loop_expr missing iterable expression or type");
        return;
    }
    if (!then) {
        log_error("Error: transpile_loop_expr missing then expression");
        return;
    }
    
    Type * expr_type = loop_node->as->type;
    Type *item_type = nullptr;
    
    // Safely determine item type based on expression type
    if (expr_type->type_id == LMD_TYPE_ARRAY) {
        TypeArray* array_type = (TypeArray*)expr_type;
        // Validate that the cast is safe by checking the nested pointer
        if (array_type && array_type->nested && (uintptr_t)array_type->nested > 0x1000) {
            item_type = array_type->nested;
        } else {
        log_warn("Warning: Invalid nested type in array, using TYPE_ANY");
            item_type = &TYPE_ANY;
        }
    } else if (expr_type->type_id == LMD_TYPE_RANGE) {
        item_type = &TYPE_INT;
    } else {
        item_type = &TYPE_ANY;
    }
        
    // Validate that we have a proper type for item_type
    if (!item_type) {
        log_error("Error: transpile_loop_expr failed to determine item type");
        item_type = &TYPE_ANY; // fallback to ANY type for safety
    }
        
    // Defensive check: verify item_type is valid before accessing type_id
    if (!item_type || (uintptr_t)item_type < 0x1000 || (uintptr_t)item_type > 0x7FFFFFFFFFFF) {
        log_warn("Warning: item_type pointer is invalid (%p), using TYPE_ANY", item_type);
        item_type = &TYPE_ANY;
    }
    
    strbuf_append_str(tp->code_buf, 
        expr_type->type_id == LMD_TYPE_RANGE ? " Range *rng=" :
        (item_type->type_id == LMD_TYPE_INT) ? " ArrayInt *arr=" : " Array *arr=");
    transpile_expr(tp, loop_node->as);
    strbuf_append_str(tp->code_buf, expr_type->type_id == LMD_TYPE_RANGE ? 
        ";\n for (long i=rng->start; i<=rng->end; i++) {\n " : 
        ";\n for (int i=0; i<arr->length; i++) {\n ");
    write_type(tp->code_buf, item_type);
    strbuf_append_str(tp->code_buf, " _");
    strbuf_append_str_n(tp->code_buf, loop_node->name->chars, loop_node->name->len);
    if (expr_type->type_id == LMD_TYPE_RANGE) {
        strbuf_append_str(tp->code_buf, "=i;\n");
    } else if (item_type->type_id == LMD_TYPE_STRING) {
        strbuf_append_str(tp->code_buf, "=fn_string(arr->items[i]);\n");
    } else {
        strbuf_append_str(tp->code_buf, "=arr->items[i];\n");
    }
    AstNode *next_loop = loop_node->next;
    if (next_loop) {
        log_debug("transpile nested loop");
        transpile_loop_expr(tp, (AstNamedNode*)next_loop, then);
    } else {
        Type *then_type = then->type;
        strbuf_append_str(tp->code_buf, " list_push(ls,");
        transpile_box_item(tp, then);
        strbuf_append_str(tp->code_buf, ");");
    }
    strbuf_append_str(tp->code_buf, " }\n");
}

void transpile_for_expr(Transpiler* tp, AstForNode *for_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!for_node) {
        log_error("Error: transpile_for_expr called with null for node");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    if (!for_node->then) {
        log_error("Error: transpile_for_expr missing then expression");
        strbuf_append_str(tp->code_buf, "({\n List* ls=list();\n ls;})");
        return;
    }
    if (!for_node->then->type) {
        log_error("Error: transpile_for_expr then expression missing type information");
        strbuf_append_str(tp->code_buf, "({\n List* ls=list();\n ls;})");
        return;
    }
    
    Type *then_type = for_node->then->type;
    // init a list
    strbuf_append_str(tp->code_buf, "({\n List* ls=list(); \n");
    AstNode *loop = for_node->loop;
    if (loop) {
        transpile_loop_expr(tp, (AstNamedNode*)loop, for_node->then);
    }
    // return the list
    strbuf_append_str(tp->code_buf, " ls;})");
}

void transpile_items(Transpiler* tp, AstNode *item) {
    bool is_first = true;
    while (item) {
        // skip let declaration
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM || 
            item->node_type == AST_NODE_FUNC) {
            item = item->next;  continue;
        }

        if (is_first) { is_first = false; } 
        else { strbuf_append_str(tp->code_buf, ", "); }

        transpile_box_item(tp, item);
        item = item->next;
    }
}

void transpile_array_expr(Transpiler* tp, AstArrayNode *array_node) {
    TypeArray *type = (TypeArray*)array_node->type;
    bool is_int_array = type->nested && type->nested->type_id == LMD_TYPE_INT;
    bool is_int64_array = type->nested && type->nested->type_id == LMD_TYPE_INT64;
    bool is_float_array = type->nested && type->nested->type_id == LMD_TYPE_FLOAT;
    
    if (is_int_array) {
        strbuf_append_str(tp->code_buf, "({ArrayInt* arr = array_int(); array_int_fill(arr,");
    } else if (is_int64_array) {
        strbuf_append_str(tp->code_buf, "({ArrayInt64* arr = array_int64(); array_int64_fill(arr,");
    } else if (is_float_array) {
        strbuf_append_str(tp->code_buf, "({ArrayFloat* arr = array_float(); array_float_fill(arr,");
    } else {
        strbuf_append_str(tp->code_buf, "({Array* arr = array(); array_fill(arr,");
    }
    
    strbuf_append_int(tp->code_buf, type->length);
    // only add comma if there are items to follow
    if (array_node->item) {
        strbuf_append_char(tp->code_buf, ',');
    }
    
    if (is_int_array || is_int64_array || is_float_array) {
        AstNode *item = array_node->item;
        while (item) {
            // transpile as unboxed items
            transpile_expr(tp, item);
            if (item->next) {
                strbuf_append_char(tp->code_buf, ',');
            }
            item = item->next;
        }
    } else {
        // transpile as boxed items
        transpile_items(tp, array_node->item);
    }
    strbuf_append_str(tp->code_buf, "); })");
}

void transpile_list_expr(Transpiler* tp, AstListNode *list_node) {
    log_debug("transpile list expr: dec - %p, itm - %p", list_node->declare, list_node->item);
    // Defensive validation: ensure all required pointers and components are valid
    if (!list_node) {
        log_error("Error: transpile_list_expr called with null list node");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    if (!list_node->type) {
        log_error("Error: transpile_list_expr missing type information");
        strbuf_append_str(tp->code_buf, "({\n List* ls = list();\n ls;})");
        return;
    }
    
    TypeArray *type = (TypeArray*)list_node->type;
    log_debug("transpile_list_expr: type->length = %ld", type->length);
    // create list before the declarations, to contain all the allocations
    strbuf_append_str(tp->code_buf, "({\n List* ls = list();\n");
    // let declare first
    AstNode *declare = list_node->declare;
    while (declare) {
        if (declare->node_type != AST_NODE_ASSIGN) {
            log_error("Error: transpile_list_expr found non-assign node in declare chain");
            // Skip invalid node - defensive recovery
            declare = declare->next;
            continue;
        }
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        strbuf_append_char(tp->code_buf, ' ');
        declare = declare->next;
    }
    if (type->length == 0) {
        log_debug("transpile_list_expr: type->length is 0, outputting null");
        strbuf_append_str(tp->code_buf, "null;})");
        return;
    }
    if (type->length < 10) {
        strbuf_append_str(tp->code_buf, " list_fill(ls,");
        strbuf_append_int(tp->code_buf, type->length);
        strbuf_append_char(tp->code_buf, ',');
        transpile_items(tp, list_node->item);
        strbuf_append_str(tp->code_buf, ");})");
    } 
    else {
        push_list_items(tp, list_node->item, false);
    }
}

void transpile_content_expr(Transpiler* tp, AstListNode *list_node) {
    log_debug("transpile content expr");
    TypeArray *type = (TypeArray*)list_node->type;
    // create list before the declarations, to contain all the allocations
    strbuf_append_str(tp->code_buf, "({\n List* ls = list();");
    // let declare first
    AstNode *item = list_node->item;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM) {
            type->length--;
            transpile_let_stam(tp, (AstLetNode*)item);
        }
        else if (item->node_type == AST_NODE_FUNC) {
            type->length--;
            // already transpiled
        }
        item = item->next;
    }
    if (type->length == 0) {
        strbuf_append_str(tp->code_buf, "null;})");
        return;
    }
    push_list_items(tp, list_node->item, false);
}

void transpile_map_expr(Transpiler* tp, AstMapNode *map_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!map_node) {
        log_error("Error: transpile_map_expr called with null map node");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    if (!map_node->type) {
        log_error("Error: transpile_map_expr missing type information");
        strbuf_append_str(tp->code_buf, "({Map* m = map(0); m;})");
        return;
    }
    
    strbuf_append_str(tp->code_buf, "({Map* m = map(");
    strbuf_append_int(tp->code_buf, ((TypeMap*)map_node->type)->type_index);
    strbuf_append_str(tp->code_buf, ");");
    AstNode *item = map_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n map_fill(m,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    transpile_expr(tp, key_expr->as);
                } else {
        log_error("Error: transpile_map_expr key expression missing assignment");
                    strbuf_append_str(tp->code_buf, "ITEM_NULL");
                }
            } else {
                transpile_box_item(tp, item);
            }
            if (item->next) { strbuf_append_char(tp->code_buf, ','); }
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, ");");
    }
    else {
        strbuf_append_str(tp->code_buf, "m;");
    }
    strbuf_append_str(tp->code_buf, "})");
}

void transpile_element(Transpiler* tp, AstElementNode *elmt_node) {
    // Defensive validation: ensure all required pointers and components are valid
    if (!elmt_node) {
        log_error("Error: transpile_element called with null element node");
        // todo: raise tranpiling error
        return;
    }
    if (!elmt_node->type) {
        log_error("Error: transpile_element missing type information");
        // todo: raise tranpiling error
        return;
    }
    
    strbuf_append_str(tp->code_buf, "\n({Element* el=elmt(");
    TypeElmt* type = (TypeElmt*)elmt_node->type;
    strbuf_append_int(tp->code_buf, type->type_index);
    strbuf_append_str(tp->code_buf, ");");

    // transpile the attributes
    AstNode *item = elmt_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n elmt_fill(el,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    transpile_expr(tp, key_expr->as);
                } else {
                    log_error("Error: transpile_element key expression missing assignment");
                    strbuf_append_str(tp->code_buf, "ITEM_NULL");
                }
            } else {
                transpile_box_item(tp, item);
            }
            if (item->next) { strbuf_append_char(tp->code_buf, ','); }
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, ");");
    }

    // transpile the content items
    if (type->content_length) {
        if (type->content_length < 10) {
            strbuf_append_str(tp->code_buf, "\n list_fill(el,");
            strbuf_append_int(tp->code_buf, type->content_length);
            strbuf_append_char(tp->code_buf, ',');
            if (elmt_node->content) {
                transpile_items(tp, ((AstListNode*)elmt_node->content)->item);
            } else {
                log_error("Error: transpile_element content missing despite content_length > 0");
            }
            strbuf_append_str(tp->code_buf, ");})");
        } else {
            if (elmt_node->content) {
                push_list_items(tp, ((AstListNode*)elmt_node->content)->item, true);
            } else {
                log_error("Error: transpile_element content missing despite content_length > 0");
            }
        }
    }
    else { // no content
        if (elmt_node->item) {
            strbuf_append_str(tp->code_buf, " list_end(el);})"); 
        }
        else { // and no attr, thus no frame_end
            strbuf_append_str(tp->code_buf, " el;})"); 
        }  
    }
}

void transpile_call_expr(Transpiler* tp, AstCallNode *call_node) {
        log_debug("transpile call expr");
    // Defensive validation: ensure all required pointers and components are valid
    if (!call_node) {
        log_error("Error: transpile_call_expr called with null call node");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    if (!call_node->function) {
        log_error("Error: transpile_call_expr missing function");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    if (!call_node->function->type) {
        log_error("Error: transpile_call_expr function missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    
    // write the function name/ptr
    TypeFunc *fn_type = NULL;
    if (call_node->function->node_type == AST_NODE_SYS_FUNC) {
        StrView fn = ts_node_source(tp, call_node->function->node);
        strbuf_append_str(tp->code_buf, "fn_");
        strbuf_append_str_n(tp->code_buf, fn.str, fn.length);
    }
    else {
        if (call_node->function->type->type_id == LMD_TYPE_FUNC) {
            fn_type = (TypeFunc*)call_node->function->type;
            AstPrimaryNode *fn_node = call_node->function->node_type == AST_NODE_PRIMARY ? 
                (AstPrimaryNode*)call_node->function:null;
            if (fn_node && fn_node->expr && fn_node->expr->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident_node = (AstIdentNode*)fn_node->expr;
                if (ident_node->entry && ident_node->entry->node && 
                    ident_node->entry->node->node_type == AST_NODE_FUNC) {
                    write_fn_name(tp->code_buf, (AstFuncNode*)ident_node->entry->node, 
                        (AstImportNode*)ident_node->entry->import);
                } else { // variable
                    strbuf_append_char(tp->code_buf, '_');
                    write_node_source(tp, fn_node->expr->node);
                }
            }
            else {
                transpile_expr(tp, call_node->function);
            }
            if (fn_type->is_anonymous) {
                strbuf_append_str(tp->code_buf, "->ptr");
            }
        } else { // handle Item
        log_debug("call function type is not func");
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
            return;
        }       
    }

    // write the params
    strbuf_append_str(tp->code_buf, "(");
    AstNode* arg = call_node->argument;  TypeParam *param_type = fn_type ? fn_type->param : NULL;
    while (arg) {
        log_debug("transpile_call_expr: processing arg type %d, node_type %d", 
            arg->type->type_id, arg->node_type);
        
        // For system functions, box DateTime arguments
        if (call_node->function->node_type == AST_NODE_SYS_FUNC && arg->type->type_id == LMD_TYPE_DTIME) {
        log_debug("transpile_call_expr: BOXING DateTime for sys func");
            strbuf_append_str(tp->code_buf, "k2it(");
            transpile_expr(tp, arg);
            strbuf_append_str(tp->code_buf, ")");
        }
        // boxing based on arg type and fn definition type
        else if (param_type) {
            if (param_type->type_id == arg->type->type_id) {
                transpile_expr(tp, arg);
            }
            else if (param_type->type_id == LMD_TYPE_FLOAT) {
                if ((arg->type->type_id == LMD_TYPE_INT || arg->type->type_id == LMD_TYPE_INT64 || 
                    arg->type->type_id == LMD_TYPE_FLOAT)) {
                    transpile_expr(tp, arg);
                }
                else if (arg->type->type_id == LMD_TYPE_ANY) {
                    strbuf_append_str(tp->code_buf, "it2d(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                }
                else {
                    // todo: raise error
                    strbuf_append_str(tp->code_buf, "null");
                }
            }
            else if (param_type->type_id == LMD_TYPE_INT64) {
                if (arg->type->type_id == LMD_TYPE_INT || arg->type->type_id == LMD_TYPE_INT64) {
                    transpile_expr(tp, arg);
                }
                else if (arg->type->type_id == LMD_TYPE_FLOAT) {
                    strbuf_append_str(tp->code_buf, "((long)");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                }
                else if (arg->type->type_id == LMD_TYPE_ANY) {
                    strbuf_append_str(tp->code_buf, "it2l(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                }
                else {
                    // todo: raise error
                    strbuf_append_str(tp->code_buf, "null");
                }
            }
            else transpile_box_item(tp, arg);
        }
        else transpile_box_item(tp, arg);
        if (arg->next) {
            strbuf_append_char(tp->code_buf, ',');
        }
        arg = arg->next;  param_type = param_type ? param_type->next : NULL;
    }
    
    // Special handling for format function - add default second argument if only one provided
    if (call_node->function->node_type == AST_NODE_SYS_FUNC) {
        StrView fn = ts_node_source(tp, call_node->function->node);
        if (fn.length == 6 && strncmp(fn.str, "format", 6) == 0) {
            // Count arguments
            int arg_count = 0;
            AstNode* count_arg = call_node->argument;
            while (count_arg) {
                arg_count++;
                count_arg = count_arg->next;
            }
            
            // If only one argument, add default null second argument
            if (arg_count == 1) {
                strbuf_append_str(tp->code_buf, ", ITEM_NULL");
            }
        }
        // Special handling for min/max functions - add default second argument for array operations
        else if ((fn.length == 3 && (strncmp(fn.str, "min", 3) == 0 || strncmp(fn.str, "max", 3) == 0))) {
            // Count arguments
            int arg_count = 0;
            AstNode* count_arg = call_node->argument;
            while (count_arg) {
                arg_count++;
                count_arg = count_arg->next;
            }
            
            // If only one argument (array case), add null second argument
            if (arg_count == 1) {
                strbuf_append_str(tp->code_buf, ", ITEM_NULL");
            }
        }
    }
    
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_index_expr(Transpiler* tp, AstFieldNode *field_node) {
    // Defensive validation: ensure all required pointers and types are valid
    if (!field_node) {
        log_error("Error: transpile_index_expr called with null field_node");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    if (!field_node->object || !field_node->field) {
        log_error("Error: transpile_index_expr missing object or field");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    if (!field_node->object->type || !field_node->field->type) {
        log_error("Error: transpile_index_expr missing type information");
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        return;
    }
    
    TypeId object_type = field_node->object->type->type_id;
    TypeId field_type = field_node->field->type->type_id;
    
    // Check if field type is numeric (addressing the TODO comment)
    if (field_type != LMD_TYPE_INT && field_type != LMD_TYPE_INT64 && field_type != LMD_TYPE_FLOAT) {
        // Non-numeric index, must use generic fn_index
        strbuf_append_str(tp->code_buf, "fn_index(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    
    // Fast path optimizations for specific type combinations
    if (object_type == LMD_TYPE_ARRAY_INT && field_type == LMD_TYPE_INT) {
        // transpile_expr(tp, field_node->object);
        // strbuf_append_str(tp->code_buf, "->items[");
        // transpile_expr(tp, field_node->field);
        // strbuf_append_str(tp->code_buf, "]");
        // for safety, we have to call array_int_get
        strbuf_append_str(tp->code_buf, "array_int_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_ARRAY_INT64 && field_type == LMD_TYPE_INT) {
        // for safety, we have to call array_int64_get
        strbuf_append_str(tp->code_buf, "array_int64_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_ARRAY_FLOAT && field_type == LMD_TYPE_INT) {
        // for safety, we have to call array_float_get
        strbuf_append_str(tp->code_buf, "array_float_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }       
    else if (object_type == LMD_TYPE_ARRAY && field_type == LMD_TYPE_INT) {
        TypeArray* arr_type = (TypeArray*)field_node->object->type;
        if (arr_type->nested) {
            switch (arr_type->nested->type_id) {
                case LMD_TYPE_INT:
                    strbuf_append_str(tp->code_buf, "array_int_get(");  break;
                case LMD_TYPE_INT64:
                    strbuf_append_str(tp->code_buf, "array_int64_get(");  break;
                case LMD_TYPE_FLOAT:
                    strbuf_append_str(tp->code_buf, "array_float_get(");  break;
                default:
                    strbuf_append_str(tp->code_buf, "array_get(");
            }
        }
        else { 
            strbuf_append_str(tp->code_buf, "array_get(");
        }
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }
    else if (object_type == LMD_TYPE_LIST && field_type == LMD_TYPE_INT) {
        strbuf_append_str(tp->code_buf, "list_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;
    }    
    else {
        // Generic fallback for all other cases
        strbuf_append_str(tp->code_buf, "fn_index(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, field_node->field);
        strbuf_append_char(tp->code_buf, ')');
        return;    
    }
}

void transpile_member_expr(Transpiler* tp, AstFieldNode *field_node) {
    if (field_node->object->type->type_id == LMD_TYPE_MAP) {
        strbuf_append_str(tp->code_buf, "map_get(");
        transpile_expr(tp, field_node->object);
    } 
    else if (field_node->object->type->type_id == LMD_TYPE_ELEMENT) {
        strbuf_append_str(tp->code_buf, "elmt_get(");
        transpile_expr(tp, field_node->object);
    }
    else {
        strbuf_append_str(tp->code_buf, "fn_member(");
        transpile_expr(tp, field_node->object);
    }
    strbuf_append_char(tp->code_buf, ',');
    if (field_node->field->node_type == AST_NODE_IDENT) {
        TSSymbol symbol = ts_node_symbol(field_node->field->node);
        TypeString* type = (TypeString*)build_lit_string(tp, field_node->field->node, symbol);
        strbuf_append_format(tp->code_buf, "const_s2it(%d)", type->const_index);
    } 
    else {
        transpile_box_item(tp, field_node->field);
    }
    strbuf_append_char(tp->code_buf, ')');    
}

void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer) {
    strbuf_append_char(tp->code_buf, '\n');
    // use function body type as the return type for the time being
    Type *ret_type = fn_node->body->type;
    write_type(tp->code_buf, ret_type);

    // write the function name, with a prefix '_', so as to diff from built-in functions
    strbuf_append_str(tp->code_buf, as_pointer ? " (*" :" ");
    write_fn_name(tp->code_buf, fn_node, NULL);
    if (as_pointer) strbuf_append_char(tp->code_buf, ')');

    // write the params
    strbuf_append_char(tp->code_buf, '(');
    AstNamedNode *param = fn_node->param;
    while (param) {
        if (param != fn_node->param) strbuf_append_str(tp->code_buf, ",");
        write_type(tp->code_buf, param->type);
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        param = (AstNamedNode*)param->next;
    }
    if (as_pointer) {
        strbuf_append_str(tp->code_buf, ");\n");  return;
    }
    // write fn body
    strbuf_append_str(tp->code_buf, "){\n return ");
    transpile_expr(tp, fn_node->body);
    strbuf_append_str(tp->code_buf, ";\n}\n");
}

void transpile_fn_expr(Transpiler* tp, AstFuncNode *fn_node) {
    strbuf_append_format(tp->code_buf, "to_fn(_f%d)", ts_node_start_byte(fn_node->node));
}

void transpile_base_type(Transpiler* tp, AstTypeNode* type_node) {
    strbuf_append_format(tp->code_buf, "base_type(%d)", ((TypeType*)type_node->type)->type->type_id);
}

void transpile_binary_type(Transpiler* tp, AstBinaryNode* bin_node) {
    TypeBinary* binary_type = (TypeBinary*)((TypeType*)bin_node->type)->type;
    strbuf_append_format(tp->code_buf, "const_type(%d)", binary_type->type_index);
}

void transpile_expr(Transpiler* tp, AstNode *expr_node) {
    if (!expr_node) { log_error("missing expression node"); return; }
    // get the function name
    switch (expr_node->node_type) {
    case AST_NODE_PRIMARY:
        transpile_primary_expr(tp, (AstPrimaryNode*)expr_node);
        break;
    case AST_NODE_UNARY:
        transpile_unary_expr(tp, (AstUnaryNode*)expr_node);
        break;
    case AST_NODE_BINARY:
        transpile_binary_expr(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_IF_EXPR:
        transpile_if(tp, (AstIfNode*)expr_node);
        break;
    case AST_NODE_IF_STAM:
        transpile_if(tp, (AstIfNode*)expr_node);
        break;
    case AST_NODE_FOR_EXPR:
        transpile_for_expr(tp, (AstForNode*)expr_node);
        break;        
    case AST_NODE_FOR_STAM:
        transpile_for_expr(tp, (AstForNode*)expr_node);
        break;        
    case AST_NODE_ASSIGN:
        transpile_assign_expr(tp, (AstNamedNode*)expr_node);
        break;
    case AST_NODE_ARRAY:
        transpile_array_expr(tp, (AstArrayNode*)expr_node);
        break;
    case AST_NODE_LIST:
        transpile_list_expr(tp, (AstListNode*)expr_node);
        break;
    case AST_NODE_CONTENT:
        transpile_content_expr(tp, (AstListNode*)expr_node);
        break;
    case AST_NODE_MAP:
        transpile_map_expr(tp, (AstMapNode*)expr_node);
        break;
    case AST_NODE_ELEMENT:
        transpile_element(tp, (AstElementNode*)expr_node);
        break;
    case AST_NODE_MEMBER_EXPR:
        transpile_member_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_INDEX_EXPR:
        transpile_index_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_CALL_EXPR:
        transpile_call_expr(tp, (AstCallNode*)expr_node);
        break;
    case AST_NODE_FUNC:  case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:
        // already transpiled
        break;
    case AST_NODE_FUNC_EXPR:
        transpile_fn_expr(tp, (AstFuncNode*)expr_node);
        break;
    case AST_NODE_TYPE:
        transpile_base_type(tp, (AstTypeNode*)expr_node);
        break;
    case AST_NODE_LIST_TYPE: {
        TypeType* list_type = (TypeType*)((AstListNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeList*)list_type->type)->type_index);
        break;
    }
    case AST_NODE_ARRAY_TYPE: {
        TypeType* array_type = (TypeType*)((AstArrayNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeArray*)array_type->type)->type_index);
        break;
    }
    case AST_NODE_MAP_TYPE: {
        TypeType* map_type = (TypeType*)((AstMapNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeMap*)map_type->type)->type_index);
        break;
    }
    case AST_NODE_ELMT_TYPE: {
        TypeType* elmt_type = (TypeType*)((AstElementNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeElmt*)elmt_type->type)->type_index);
        break;
    }
    case AST_NODE_FUNC_TYPE: {
        TypeType* fn_type = (TypeType*)((AstFuncNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeFunc*)fn_type->type)->type_index);
        break;
    }
    case AST_NODE_BINARY_TYPE:
        transpile_binary_type(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_IMPORT:
        log_debug("import module");
        break;
    default:
        log_debug("unknown expression type: %d!!!", expr_node->node_type);
        break;
    }
}

void define_module_import(Transpiler* tp, AstImportNode *import_node) {
    log_debug("define import module");
    // import module
    if (!import_node->script) { log_error("Error: missing script for import");  return; }
    log_debug("script reference: %s", import_node->script->reference);
    // loop through the public functions in the module
    AstNode *node = import_node->script->ast_root;
    if (!node) { log_debug("missing root node");  return; }
    assert(node->node_type == AST_SCRIPT);
    node = ((AstScript*)node)->child;
    log_debug("finding content node");
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) break;
        node = node->next;
    }
    log_debug("missing content node");
    strbuf_append_format(tp->code_buf, "struct Mod%d {\n", import_node->script->index);
    node = ((AstListNode*)node)->item;
    while (node) {
        if (node->node_type == AST_NODE_FUNC) {
            AstFuncNode *func_node = (AstFuncNode*)node;
            log_debug("got fn: %.*s, is_public: %d", (int)func_node->name->len, func_node->name->chars,
                ((TypeFunc*)func_node->type)->is_public);
            if (((TypeFunc*)func_node->type)->is_public) {
                define_func(tp, func_node, true);
            }
        } 
        else if (node->node_type == AST_NODE_PUB_STAM) {
            // let declaration
            AstNode *declare = ((AstLetNode*)node)->declare;
            while (declare) {
                AstNamedNode *asn_node = (AstNamedNode*)declare;
                // declare the type
                Type *type = asn_node->type;
                write_type(tp->code_buf, type);
                strbuf_append_char(tp->code_buf, ' ');
                write_var_name(tp->code_buf, asn_node, NULL);
                strbuf_append_str(tp->code_buf, ";\n");
                declare = declare->next;
            }
        }
        node = node->next;
    }
    strbuf_append_format(tp->code_buf, "} m%d;\n", import_node->script->index);
}

void define_ast_node(Transpiler* tp, AstNode *node) {
    // get the function name
    switch(node->node_type) {
    case AST_NODE_IDENT:  case AST_NODE_PARAM:
        break;
    case AST_NODE_PRIMARY:
        if (((AstPrimaryNode*)node)->expr) {
            define_ast_node(tp, ((AstPrimaryNode*)node)->expr);
        }
        break;
    case AST_NODE_UNARY:
        define_ast_node(tp, ((AstUnaryNode*)node)->operand);
        break;
    case AST_NODE_BINARY:
        define_ast_node(tp, ((AstBinaryNode*)node)->left);
        define_ast_node(tp, ((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_IF_EXPR: {
        AstIfNode* if_node = (AstIfNode*)node;
        define_ast_node(tp, if_node->cond);
        define_ast_node(tp, if_node->then);
        if (if_node->otherwise) {
            define_ast_node(tp, if_node->otherwise);
        }
        break;
    }
    case AST_NODE_IF_STAM: {
        AstIfNode* if_node = (AstIfNode*)node;
        define_ast_node(tp, if_node->cond);
        define_ast_node(tp, if_node->then);
        if (if_node->otherwise) {
            define_ast_node(tp, if_node->otherwise);
        }
        break;
    }
    case AST_NODE_LET_STAM: {
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            define_ast_node(tp, declare);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_PUB_STAM: {
        // pub vars need to be brought to global scope in C, to allow them to be exported
        AstNode *decl = ((AstLetNode*)node)->declare;
        while (decl) {
            transpile_assign_expr(tp, (AstNamedNode*)decl);
            decl = decl->next;
        }
        break;
    }
    case AST_NODE_FOR_EXPR: {
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            define_ast_node(tp, loop);
            loop = loop->next;
        }
        define_ast_node(tp, ((AstForNode*)node)->then);
        break;
    }
    case AST_NODE_FOR_STAM: {
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            define_ast_node(tp, loop);
            loop = loop->next;
        }
        define_ast_node(tp, ((AstForNode*)node)->then);
        break;
    }
    case AST_NODE_ASSIGN: {
        AstNamedNode* assign = (AstNamedNode*)node;
        define_ast_node(tp, assign->as);
        break;
    }
    case AST_NODE_KEY_EXPR: {
        AstNamedNode* key = (AstNamedNode*)node;
        define_ast_node(tp, key->as);
        break;
    }
    case AST_NODE_LOOP:
        define_ast_node(tp, ((AstNamedNode*)node)->as);
        break;
    case AST_NODE_ARRAY: {
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            define_ast_node(tp, item);
            item = item->next;
        }        
        break;
    }
    case AST_NODE_LIST:  case AST_NODE_CONTENT: {
        AstNode *ld = ((AstListNode*)node)->declare;
        while (ld) {
            define_ast_node(tp, ld);
            ld = ld->next;
        }        
        AstNode *li = ((AstListNode*)node)->item;
        while (li) {
            define_ast_node(tp, li);
            li = li->next;
        }        
        break; 
    }
    case AST_NODE_MAP:  case AST_NODE_ELEMENT: {
        AstNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            define_ast_node(tp, nm_item);
            nm_item = nm_item->next;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR:  case AST_NODE_INDEX_EXPR:
        define_ast_node(tp, ((AstFieldNode*)node)->object);
        define_ast_node(tp, ((AstFieldNode*)node)->field);
        break;
    case AST_NODE_CALL_EXPR: {
        define_ast_node(tp, ((AstCallNode*)node)->function);
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            define_ast_node(tp, arg);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR: {
        define_func(tp, (AstFuncNode*)node, false);
        AstFuncNode* func = (AstFuncNode*)node;
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            define_ast_node(tp, fn_param);
            fn_param = fn_param->next;
        }        
        define_ast_node(tp, func->body);
        break;
    }
    case AST_NODE_IMPORT:
        define_module_import(tp, (AstImportNode*)node);
        break;
    case AST_NODE_SYS_FUNC:
        // should define its params
        break;
    default:
        log_debug("unknown expression type: %d", node->node_type);
        break;
    }
}

#include "lambda-embed.h"

void transpile_ast(Transpiler* tp, AstScript *script) {
    strbuf_append_str_n(tp->code_buf, (const char*)lambda_lambda_h, lambda_lambda_h_len);
    // all (nested) function definitions need to be hoisted to global level
        log_debug("define_ast_node...");
    strbuf_append_str(tp->code_buf, "\n\nContext *rt;\n");  // defines global runtime context
    AstNode* child = script->child;
    while (child) {
        define_ast_node(tp, child);
        child = child->next;
    }    

    // global evaluation, wrapped inside main()
        log_debug("transpile_ast_node...");
    strbuf_append_str(tp->code_buf, "\nItem main(Context *runtime){\n rt = runtime;\n return ");
    child = script->child;
    bool has_content = false;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:
        case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:
            break;  // skip defintion nodes
        default:
            // AST_NODE_CONTENT, AST_NODE_PRIMARY, AST_NODE_BINARY, etc.
            transpile_box_item(tp, child);
            has_content = true;            
        }
        child = child->next;
    }
    if (!has_content) { strbuf_append_str(tp->code_buf, "ITEM_NULL"); }
    strbuf_append_str(tp->code_buf, ";\n}\n");
}


