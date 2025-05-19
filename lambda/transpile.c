
#include "transpiler.h"

extern LambdaType TYPE_ANY;
void transpile_expr(Transpiler* tp, AstNode *expr_node);

void transpile_box_item(Transpiler* tp, AstNode *item) {
    if (item->type->type_id == LMD_TYPE_NULL) {
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
    }
    else if (item->type->type_id == LMD_TYPE_BOOL) {
        strbuf_append_str(tp->code_buf, "b2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (item->type->type_id == LMD_TYPE_IMP_INT) {
        strbuf_append_str(tp->code_buf, "i2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
    }    
    else if (item->type->type_id == LMD_TYPE_INT) {
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_l2it(");
            LambdaTypeItem *item_type = (LambdaTypeItem*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            strbuf_append_str(tp->code_buf, "push_l(rt,");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
    }    
    else if (item->type->type_id == LMD_TYPE_FLOAT) {
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_d2it(");
            LambdaTypeItem *item_type = (LambdaTypeItem*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            strbuf_append_str(tp->code_buf, "push_d(rt,");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (item->type->type_id == LMD_TYPE_STRING || item->type->type_id == LMD_TYPE_SYMBOL || 
        item->type->type_id == LMD_TYPE_DTIME || item->type->type_id == LMD_TYPE_BINARY) {
        char t = item->type->type_id == LMD_TYPE_STRING ? 's' :
            item->type->type_id == LMD_TYPE_SYMBOL ? 'y' : 
            item->type->type_id == LMD_TYPE_BINARY ? 'x':'k';
        if (item->type->is_literal) {
            strbuf_append_format(tp->code_buf, "const_%c2it(", t);
            LambdaTypeItem *item_type = (LambdaTypeItem*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            strbuf_append_format(tp->code_buf, "%c2it(", t);
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
    }
    else if (item->type->type_id == LMD_TYPE_LIST || item->type->type_id == LMD_TYPE_ARRAY || 
        item->type->type_id == LMD_TYPE_MAP) {
        transpile_expr(tp, item);  // raw pointer
    }
    else if (item->type->type_id == LMD_TYPE_ANY) {
        transpile_expr(tp, item);  // no boxing needed
    }
    else {
        printf("unknown box item type: %d\n", item->type->type_id);
    }
}

void transpile_primary_expr(Transpiler* tp, AstPrimaryNode *pri_node) {
    printf("transpile primary expr\n");
    if (pri_node->expr) {
        printf("transpile_expr\n");
        if (pri_node->expr->node_type == AST_NODE_IDENT) {
            // user var name starts with '_'
            strbuf_append_char(tp->code_buf, '_');
            writeNodeSource(tp, pri_node->node);
        } else { 
            transpile_expr(tp, pri_node->expr);
        }
    } else { // const
        if (pri_node->type->is_literal) {  // literal
            printf("transpile_literal: %d\n", pri_node->type->type_id);
            if (pri_node->type->type_id == LMD_TYPE_STRING || pri_node->type->type_id == LMD_TYPE_SYMBOL ||
                pri_node->type->type_id == LMD_TYPE_DTIME || pri_node->type->type_id == LMD_TYPE_BINARY) {
                // loads the const string without boxing
                strbuf_append_str(tp->code_buf, "const_s(");
                LambdaTypeString *str_type = (LambdaTypeString*)pri_node->type;
                strbuf_append_int(tp->code_buf, str_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_IMP_INT || pri_node->type->type_id == LMD_TYPE_INT) {
                writeNodeSource(tp, pri_node->node);
                strbuf_append_char(tp->code_buf, 'L');  // add 'L' to ensure it is a long
            }
            else { // bool, null, float
                TSNode child = ts_node_named_child(pri_node->node, 0);
                TSSymbol symbol = ts_node_symbol(child);
                printf("literal symbol: %d\n", symbol);
                if (symbol == SYM_INF) {
                    strbuf_append_str(tp->code_buf, "infinity");
                }
                else if (symbol == SYM_NAN) {
                    strbuf_append_str(tp->code_buf, "not_a_number");
                }
                else {
                    printf("literal type: %d\n", pri_node->type->type_id);
                    writeNodeSource(tp, pri_node->node);
                }
            }
        } else {
            writeNodeSource(tp, pri_node->node);
        }
    }
}

void transpile_unary_expr(Transpiler* tp, AstUnaryNode *unary_node) {
    printf("transpile unary expr\n");
    if (unary_node->op == OPERATOR_NOT) { strbuf_append_str(tp->code_buf, "!"); }
    else strbuf_append_str_n(tp->code_buf, unary_node->operator.str, unary_node->operator.length);
    strbuf_append_char(tp->code_buf, '(');
    transpile_expr(tp, unary_node->operand);
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_binary_expr(Transpiler* tp, AstBinaryNode *bi_node) {
    if (bi_node->op == OPERATOR_AND || bi_node->op == OPERATOR_OR) {
        strbuf_append_char(tp->code_buf, '(');
        // left operand
        if (bi_node->left->type->type_id == LMD_TYPE_ANY) {
            strbuf_append_str(tp->code_buf, "item_true(");
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            transpile_expr(tp, bi_node->left);
        }
        // operator
        if (bi_node->op == OPERATOR_OR) {
            strbuf_append_str(tp->code_buf, "||");
        } else {
            strbuf_append_str(tp->code_buf, "&&");
        }
        // right operand
        if (bi_node->right->type->type_id == LMD_TYPE_ANY) {
            strbuf_append_str(tp->code_buf, "item_true(");
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
        }
        else {
            transpile_expr(tp, bi_node->right);
        }
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_POW) {
        strbuf_append_str(tp->code_buf, "pow(");
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_ADD) {
        if (bi_node->left->type->type_id == bi_node->right->type->type_id) {
            if (bi_node->left->type->type_id == LMD_TYPE_STRING) {
                strbuf_append_str(tp->code_buf, "str_cat(");
                transpile_expr(tp, bi_node->left);
                strbuf_append_char(tp->code_buf, ',');
                transpile_expr(tp, bi_node->right);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }
            else if (bi_node->left->type->type_id == LMD_TYPE_IMP_INT || bi_node->left->type->type_id == LMD_TYPE_INT ||
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
        else if (LMD_TYPE_IMP_INT <= bi_node->left->type->type_id && bi_node->left->type->type_id <= LMD_TYPE_FLOAT &&
            LMD_TYPE_IMP_INT <= bi_node->right->type->type_id && bi_node->right->type->type_id <= LMD_TYPE_FLOAT) {
            strbuf_append_char(tp->code_buf, '(');
            transpile_expr(tp, bi_node->left);
            strbuf_append_char(tp->code_buf, '+');
            transpile_expr(tp, bi_node->right);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }
        // call runtime add()
        strbuf_append_str(tp->code_buf, "add(rt,");  // not able to handle yet
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_DIV && bi_node->left->type->type_id == LMD_TYPE_IMP_INT && 
        bi_node->right->type->type_id == LMD_TYPE_IMP_INT) {
        // division is always carried out in double
        strbuf_append_str(tp->code_buf, "((double)");
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, '/');
        transpile_expr(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else {
        strbuf_append_char(tp->code_buf, '(');
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ' ');
        if (bi_node->op == OPERATOR_IDIV) strbuf_append_str(tp->code_buf, "/");
        else strbuf_append_str_n(tp->code_buf, bi_node->operator.str, bi_node->operator.length);        
        strbuf_append_char(tp->code_buf, ' ');
        transpile_expr(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
}

void transpile_if_expr(Transpiler* tp, AstIfExprNode *if_node) {
    printf("transpile if expr\n");
    // transpile as C conditional expr
    strbuf_append_char(tp->code_buf, '(');
    transpile_expr(tp, if_node->cond);
    strbuf_append_char(tp->code_buf, '?');
    transpile_expr(tp, if_node->then);
    strbuf_append_char(tp->code_buf, ':');
    if (if_node->otherwise) transpile_expr(tp, if_node->otherwise);
    else strbuf_append_str(tp->code_buf, "null");
    strbuf_append_char(tp->code_buf, ')');
    printf("end if expr\n");
}

void transpile_assign_expr(Transpiler* tp, AstNamedNode *asn_node) {
    printf("transpile assign expr\n");
    // declare the type
    LambdaType *type = asn_node->type;
    writeType(tp, type);
    // user var name starts with '_'
    strbuf_append_str(tp->code_buf, " _");
    // declare the variable
    strbuf_append_str_n(tp->code_buf, asn_node->name.str, asn_node->name.length);
    strbuf_append_char(tp->code_buf, '=');

    transpile_expr(tp, asn_node->as);
    strbuf_append_str(tp->code_buf, ";\n");
}

void transpile_let_stam(Transpiler* tp, AstLetNode *let_node) {
    printf("transpile let stam\n");
    AstNode *declare = let_node->declare;
    while (declare) {
        assert(declare->node_type == AST_NODE_ASSIGN);
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        declare = declare->next;
    }
}

void transpile_loop_expr(Transpiler* tp, AstNamedNode *loop_node, AstNode* then) {
    printf("transpile loop expr\n");
    // todo: prefix var name with '_'
    LambdaType *item_type = loop_node->as->type->type_id == LMD_TYPE_ARRAY ? 
        ((LambdaTypeArray*)loop_node->as->type)->nested : &TYPE_ANY;
    strbuf_append_str(tp->code_buf, (item_type->type_id == LMD_TYPE_IMP_INT) ? " ArrayLong *arr=" : " Array *arr=");
    transpile_expr(tp, loop_node->as);
    strbuf_append_str(tp->code_buf, ";\n for (int i=0; i<arr->length; i++) {\n ");
    writeType(tp, item_type);
    strbuf_append_str(tp->code_buf, " _");
    strbuf_append_str_n(tp->code_buf, loop_node->name.str, loop_node->name.length);
    strbuf_append_str(tp->code_buf, "=arr->items[i];\n");
    AstNode *next_loop = loop_node->next;
    if (next_loop) {
        printf("transpile nested loop\n");
        transpile_loop_expr(tp, (AstNamedNode*)next_loop, then);
    } else {
        LambdaType *then_type = then->type;
        if (then_type->type_id == LMD_TYPE_IMP_INT || then_type->type_id == LMD_TYPE_INT) {
            strbuf_append_str(tp->code_buf, " list_long_push(ls,");
            transpile_expr(tp, then);            
        }
        else {
            strbuf_append_str(tp->code_buf, " list_push(ls,");
            transpile_box_item(tp, then);
        }
        strbuf_append_str(tp->code_buf, ");");
    }
    strbuf_append_str(tp->code_buf, " }\n");
}

void transpile_for_expr(Transpiler* tp, AstForNode *for_node) {
    printf("transpile for expr\n");
    LambdaType *then_type = for_node->then->type;
    // init a list
    strbuf_append_str(tp->code_buf,
        (then_type->type_id == LMD_TYPE_IMP_INT || then_type->type_id == LMD_TYPE_INT) ?
        "({\n ListLong* ls=list_long();\n" : "({\n List* ls=list(); \n");
    AstNode *loop = for_node->loop;
    if (loop) {
        printf("transpile for loop\n");
        transpile_loop_expr(tp, (AstNamedNode*)loop, for_node->then);
    }
    // return the list
    strbuf_append_str(tp->code_buf, " ls;})");
}

void transpile_items(Transpiler* tp, AstArrayNode *array_node) {
    AstNode *item = array_node->item;  bool is_first = true;
    while (item) {
        // skip let declaration
        if (item->node_type == AST_NODE_LET_STAM) { item = item->next; continue; }

        if (is_first) { is_first = false; } 
        else { strbuf_append_char(tp->code_buf, ','); }

        printf("list item type:%d\n", item->type->type_id);
        transpile_box_item(tp, item);
        item = item->next;
    }
}

void transpile_array_expr(Transpiler* tp, AstArrayNode *array_node) {
    printf("transpile array expr\n");
    LambdaTypeArray *type = (LambdaTypeArray*)array_node->type;
    bool is_int_array = type->nested && type->nested->type_id == LMD_TYPE_IMP_INT;
    strbuf_append_str(tp->code_buf, is_int_array ? "array_long_new(" : "array_new(");
    strbuf_append_int(tp->code_buf, type->length);
    strbuf_append_char(tp->code_buf, ',');
    if (is_int_array) {
        AstNode *item = array_node->item;
        while (item) {
            transpile_expr(tp, item);
            if (item->next) {
                strbuf_append_char(tp->code_buf, ',');
            }
            item = item->next;
        }        
    } else {
        transpile_items(tp, array_node);
    }
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_list_expr(Transpiler* tp, AstListNode *list_node) {
    printf("transpile list expr: dec - %p, itm - %p\n", list_node->declare, list_node->item);
    LambdaTypeArray *type = (LambdaTypeArray*)list_node->type;
    strbuf_append_str(tp->code_buf, "({");
    // let declare first
    AstNode *declare = list_node->declare;
    while (declare) {
        assert(declare->node_type == AST_NODE_ASSIGN);
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        strbuf_append_char(tp->code_buf, ' ');
        declare = declare->next;
    }
    if (type->length == 0) {
        strbuf_append_str(tp->code_buf, "null;})");
        return;
    }    
    strbuf_append_str(tp->code_buf, "list_new(rt,");
    strbuf_append_int(tp->code_buf, type->length);
    strbuf_append_char(tp->code_buf, ',');
    transpile_items(tp, (AstArrayNode*)list_node);
    strbuf_append_str(tp->code_buf, ");})");
}

void transpile_content_expr(Transpiler* tp, AstListNode *list_node) {
    printf("transpile content expr\n");
    LambdaTypeArray *type = (LambdaTypeArray*)list_node->type;
    strbuf_append_str(tp->code_buf, "({");
    // let declare first
    AstNode *item = list_node->item;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM) {
            type->length--;
            transpile_let_stam(tp, (AstLetNode*)item);
        }
        item = item->next;
    }
    if (type->length == 0) {
        strbuf_append_str(tp->code_buf, "null;})");
        return;
    }
    strbuf_append_str(tp->code_buf, "list_new(rt,");
    strbuf_append_int(tp->code_buf, type->length);
    strbuf_append_char(tp->code_buf, ',');
    transpile_items(tp, (AstArrayNode*)list_node);
    strbuf_append_str(tp->code_buf, ");})");
}

void transpile_map_expr(Transpiler* tp, AstMapNode *map_node) {
    printf("transpile map expr\n");
    strbuf_append_str(tp->code_buf, "map_new(rt,");
    strbuf_append_int(tp->code_buf, ((LambdaTypeMap*)map_node->type)->type_index);
    strbuf_append_char(tp->code_buf, ',');
    AstNamedNode *item = map_node->item;
    while (item) {
        // strbuf_append_char(tp->code_buf, '"');
        // strbuf_append_str_n(tp->code_buf, item->name.str, item->name.length);
        // strbuf_append_str(tp->code_buf, "\",");
        transpile_expr(tp, item->as);
        if (item->next) { strbuf_append_char(tp->code_buf, ','); }
        item = (AstNamedNode*)item->next;
    }
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_call_expr(Transpiler* tp, AstCallNode *call_node) {
    printf("transpile call expr\n");
    transpile_expr(tp, call_node->function);

    // write the params
    LambdaTypeFunc *fn_type = NULL;
    if (call_node->function->type->type_id == LMD_TYPE_FUNC) {
        fn_type = (LambdaTypeFunc*)call_node->function->type;
    } 

    strbuf_append_str(tp->code_buf, "(rt,");
    AstNode* arg = call_node->argument;  LambdaTypeParam *param_type = fn_type ? fn_type->param : NULL;
    while (arg) {
        // boxing based on arg type and fn definition type
        if (param_type) {
            if (param_type->type_id == arg->type->type_id) {
                transpile_expr(tp, arg);
            }
            else if (param_type->type_id == LMD_TYPE_FLOAT) {
                if ((arg->type->type_id == LMD_TYPE_IMP_INT || arg->type->type_id == LMD_TYPE_INT || 
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
            else if (param_type->type_id == LMD_TYPE_INT) {
                if (arg->type->type_id == LMD_TYPE_IMP_INT || arg->type->type_id == LMD_TYPE_INT) {
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
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_field_expr(Transpiler* tp, AstFieldNode *field_node) {
    printf("transpile field expr\n");
    if (field_node->object->type->type_id == LMD_TYPE_MAP) {
        strbuf_append_str(tp->code_buf, "map_get(rt,");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        if (field_node->field && field_node->field->node_type == AST_NODE_IDENT) {
            strbuf_append_char(tp->code_buf, '"');
            writeNodeSource(tp, field_node->field->node);
            strbuf_append_char(tp->code_buf, '"');
        }
        else {
            writeNodeSource(tp, field_node->field->node);
        }
        strbuf_append_char(tp->code_buf, ')');
    } 
    else if (field_node->object->type->type_id == LMD_TYPE_ARRAY) {
        transpile_expr(tp, field_node->object);
        strbuf_append_str(tp->code_buf, "->items[");
        writeNodeSource(tp, field_node->field->node);
        strbuf_append_char(tp->code_buf, ']');
    } 
    else {
        strbuf_append_str(tp->code_buf, "field(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        writeNodeSource(tp, field_node->field->node);
        strbuf_append_char(tp->code_buf, ')');
    }
}

void transpile_func(Transpiler* tp, AstFuncNode *fn_node) {
    // use function body type as the return type for the time being
    LambdaType *ret_type = fn_node->body->type;
    writeType(tp, ret_type);
    // write the function name, with a prefix '_', so as to diff from built-in functions
    strbuf_append_str(tp->code_buf, " _");
    strbuf_append_str_n(tp->code_buf, fn_node->name.str, fn_node->name.length);
    strbuf_append_str(tp->code_buf, "(Context *rt");
    AstNamedNode *param = fn_node->param;
    while (param) {
        strbuf_append_str(tp->code_buf, ", ");
        writeType(tp, param->type);
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name.str, param->name.length);
        param = (AstNamedNode*)param->next;
    }
    strbuf_append_str(tp->code_buf, "){\n return ");
    transpile_expr(tp, fn_node->body);
    strbuf_append_str(tp->code_buf, ";\n}\n");
}

void transpile_expr(Transpiler* tp, AstNode *expr_node) {
    if (!expr_node) {
        printf("missing expression node\n");  return;
    }
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
        transpile_if_expr(tp, (AstIfExprNode*)expr_node);
        break;
    case AST_NODE_LET_STAM:
        transpile_let_stam(tp, (AstLetNode*)expr_node);
        break;
    case AST_NODE_FOR_EXPR:
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
    case AST_NODE_FIELD_EXPR:
        transpile_field_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_CALL_EXPR:
        transpile_call_expr(tp, (AstCallNode*)expr_node);
        break;
    case AST_NODE_FUNC:
        transpile_func(tp, (AstFuncNode*)expr_node);
        break;
    default:
        printf("unknown expression type\n");
        break;
    }
}

void transpile_ast_script(Transpiler* tp, AstScript *script) {
    strbuf_append_str(tp->code_buf, "#include \"lambda/lambda.h\"\n");

    AstNode *node = script->child;
    // global declarations
    while (node) {
        // const stam
        if (node->node_type == AST_NODE_LET_STAM) {
            transpile_let_stam(tp, (AstLetNode*)node);
        }
        else if (node->node_type == AST_NODE_FUNC) {
            transpile_func(tp, (AstFuncNode*)node);
        }
        node = node->next;
    }
    // global evaluation, wrapped inside main()
    strbuf_append_str(tp->code_buf, "Item main(Context *rt){\n List *ls=");
    node = script->child;
    while (node) {
        if (node->node_type != AST_NODE_LET_STAM && node->node_type != AST_NODE_FUNC) {
            transpile_expr(tp, node);
        }
        node = node->next;
    }
    strbuf_append_str(tp->code_buf, ";\n return v2it(ls);\n}\n");
}


