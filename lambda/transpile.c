#include "transpiler.h"

extern Type TYPE_ANY, TYPE_INT;
void transpile_expr(Transpiler* tp, AstNode *expr_node);
void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer);

void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import) {
    if (import) {
        strbuf_append_format(strbuf, "m%d.", import->script->index);
    }
    strbuf_append_char(strbuf, '_');
    if (fn_node->name.str) {
        strbuf_append_str_n(strbuf, fn_node->name.str, fn_node->name.length);
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
    strbuf_append_str_n(strbuf, asn_node->name.str, asn_node->name.length);
}

void transpile_box_item(Transpiler* tp, AstNode *item) {
    printf("transpile box item: %d\n", item->type->type_id);
    switch (item->type->type_id) {
    case LMD_TYPE_NULL:
        strbuf_append_str(tp->code_buf, "ITEM_NULL");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "b2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_INT:
        strbuf_append_str(tp->code_buf, "i2it(");
        transpile_expr(tp, item);
        strbuf_append_char(tp->code_buf, ')');
        break;
    case LMD_TYPE_INT64: 
        if (item->type->is_literal) {
            strbuf_append_str(tp->code_buf, "const_l2it(");
            TypeConst *item_type = (TypeConst*)item->type;
            strbuf_append_int(tp->code_buf, item_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }
        else {
            strbuf_append_str(tp->code_buf, "push_l(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
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
            strbuf_append_str(tp->code_buf, "push_d(");
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
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY:
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
            strbuf_append_format(tp->code_buf, "%c2it(", t);
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        break;
    case LMD_TYPE_LIST:  case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
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
        printf("unknown box item type: %d\n", item->type->type_id);
    }
}

void transpile_primary_expr(Transpiler* tp, AstPrimaryNode *pri_node) {
    printf("transpile primary expr\n");
    if (pri_node->expr) {
        if (pri_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)pri_node->expr;
            if (ident_node->entry->node->node_type == AST_NODE_FUNC) {
                write_fn_name(tp->code_buf, (AstFuncNode*)ident_node->entry->node, 
                    (AstImportNode*)ident_node->entry->import);
            }
            else {
                write_var_name(tp->code_buf, (AstNamedNode*)ident_node->entry->node, 
                    (AstImportNode*)ident_node->entry->import);
            }
        } else { 
            transpile_expr(tp, pri_node->expr);
        }
    } else { // const
        if (pri_node->type->is_literal) {  // literal
            if (pri_node->type->type_id == LMD_TYPE_STRING || pri_node->type->type_id == LMD_TYPE_SYMBOL ||
                pri_node->type->type_id == LMD_TYPE_DTIME || pri_node->type->type_id == LMD_TYPE_BINARY) {
                // loads the const string without boxing
                strbuf_append_str(tp->code_buf, "const_s(");
                TypeString *str_type = (TypeString*)pri_node->type;
                strbuf_append_int(tp->code_buf, str_type->const_index);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (pri_node->type->type_id == LMD_TYPE_INT || pri_node->type->type_id == LMD_TYPE_INT64) {
                writeNodeSource(tp, pri_node->node);
                strbuf_append_char(tp->code_buf, 'L');  // add 'L' to ensure it is a long
            }
            else { // bool, null, float
                TSNode child = ts_node_named_child(pri_node->node, 0);
                TSSymbol symbol = ts_node_symbol(child);
                if (symbol == SYM_INF) {
                    strbuf_append_str(tp->code_buf, "infinity");
                }
                else if (symbol == SYM_NAN) {
                    strbuf_append_str(tp->code_buf, "not_a_number");
                }
                else {
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
    else strbuf_append_str_n(tp->code_buf, unary_node->op_str.str, unary_node->op_str.length);
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
            else if (bi_node->left->type->type_id == LMD_TYPE_INT || bi_node->left->type->type_id == LMD_TYPE_INT64 ||
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
        // call runtime add()
        strbuf_append_str(tp->code_buf, "add(");
        transpile_box_item(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_box_item(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');        
    }
    else if (bi_node->op == OPERATOR_DIV && bi_node->left->type->type_id == LMD_TYPE_INT && 
        bi_node->right->type->type_id == LMD_TYPE_INT) {
        // division is always carried out in double
        strbuf_append_str(tp->code_buf, "((double)");
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, '/');
        transpile_expr(tp, bi_node->right);
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
    else {
        strbuf_append_char(tp->code_buf, '(');
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ' ');
        if (bi_node->op == OPERATOR_IDIV) strbuf_append_str(tp->code_buf, "/");
        else strbuf_append_str_n(tp->code_buf, bi_node->op_str.str, bi_node->op_str.length);
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
    strbuf_append_str(tp->code_buf, "\n ");
    // declare the type
    Type *type = asn_node->type;
    writeType(tp, type);
    strbuf_append_char(tp->code_buf, ' ');
    write_var_name(tp->code_buf, asn_node, NULL);
    strbuf_append_char(tp->code_buf, '=');

    transpile_expr(tp, asn_node->as);
    strbuf_append_char(tp->code_buf, ';');
}

void transpile_let_stam(Transpiler* tp, AstLetNode *let_node) {
    AstNode *declare = let_node->declare;
    while (declare) {
        assert(declare->node_type == AST_NODE_ASSIGN);
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        declare = declare->next;
    }
}

void transpile_loop_expr(Transpiler* tp, AstNamedNode *loop_node, AstNode* then) {
    Type * expr_type = loop_node->as->type;
    Type *item_type = 
        expr_type->type_id == LMD_TYPE_ARRAY ? ((TypeArray*)expr_type)->nested : 
        expr_type->type_id == LMD_TYPE_RANGE ? &TYPE_INT : &TYPE_ANY;
    strbuf_append_str(tp->code_buf, 
        expr_type->type_id == LMD_TYPE_RANGE ? " Range *rng=" :
        (item_type->type_id == LMD_TYPE_INT) ? " ArrayLong *arr=" : " Array *arr=");
    transpile_expr(tp, loop_node->as);
    strbuf_append_str(tp->code_buf, expr_type->type_id == LMD_TYPE_RANGE ? 
        ";\n for (long i=rng->start; i<=rng->end; i++) {\n " : 
        ";\n for (int i=0; i<arr->length; i++) {\n ");
    writeType(tp, item_type);
    strbuf_append_str(tp->code_buf, " _");
    strbuf_append_str_n(tp->code_buf, loop_node->name.str, loop_node->name.length);
    strbuf_append_str(tp->code_buf, expr_type->type_id == LMD_TYPE_RANGE ? 
        "=i;\n" : "=arr->items[i];\n");
    AstNode *next_loop = loop_node->next;
    if (next_loop) {
        printf("transpile nested loop\n");
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
    strbuf_append_str(tp->code_buf, is_int_array ? "array_long_new(" : 
        "({Array* arr = array(); array_fill(arr,");
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
        transpile_items(tp, array_node->item);
        strbuf_append_str(tp->code_buf, ");}");
    }
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_list_expr(Transpiler* tp, AstListNode *list_node) {
    printf("transpile list expr: dec - %p, itm - %p\n", list_node->declare, list_node->item);
    TypeArray *type = (TypeArray*)list_node->type;
    // create list before the declarations, to contain all the allocations
    strbuf_append_str(tp->code_buf, "({\n List* ls = list();\n");
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
    strbuf_append_str(tp->code_buf, " list_fill(ls,");
    strbuf_append_int(tp->code_buf, type->length);
    strbuf_append_char(tp->code_buf, ',');
    transpile_items(tp, list_node->item);
    strbuf_append_str(tp->code_buf, ");})");
}

void transpile_content_expr(Transpiler* tp, AstListNode *list_node) {
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
    strbuf_append_str(tp->code_buf, "\n list_fill(ls,");
    strbuf_append_int(tp->code_buf, type->length);
    strbuf_append_char(tp->code_buf, ',');
    transpile_items(tp, list_node->item);
    strbuf_append_str(tp->code_buf, ");})");
}

void transpile_map_expr(Transpiler* tp, AstMapNode *map_node) {
    strbuf_append_str(tp->code_buf, "({Map* m = map(");
    strbuf_append_int(tp->code_buf, ((TypeMap*)map_node->type)->type_index);
    strbuf_append_str(tp->code_buf, ");");
    AstNode *item = map_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n map_fill(m,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                transpile_expr(tp, ((AstNamedNode*)item)->as);
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
    strbuf_append_str(tp->code_buf, "({Element* el=elmt(");
    TypeElmt* type = (TypeElmt*)elmt_node->type;
    strbuf_append_int(tp->code_buf, type->type_index);
    strbuf_append_str(tp->code_buf, ");");
    AstNode *item = elmt_node->item;
    if (item) {
        strbuf_append_str(tp->code_buf, "\n elmt_fill(el,");
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                transpile_expr(tp, ((AstNamedNode*)item)->as);
            } else {
                transpile_box_item(tp, item);
            }
            if (item->next) { strbuf_append_char(tp->code_buf, ','); }
            item = item->next;
        }
        strbuf_append_str(tp->code_buf, ");");
    }
    if (type->content_length) {
        strbuf_append_str(tp->code_buf, "\n list_fill(el,");
        strbuf_append_int(tp->code_buf, type->content_length);
        strbuf_append_char(tp->code_buf, ',');
        transpile_items(tp, ((AstListNode*)elmt_node->content)->item);
        strbuf_append_str(tp->code_buf, ");");
    }
    else if (!item) {
        strbuf_append_str(tp->code_buf, "el;");
    }
    strbuf_append_str(tp->code_buf, "})");
}

void transpile_call_expr(Transpiler* tp, AstCallNode *call_node) {
    printf("transpile call expr\n");
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
            if (fn_node && fn_node->expr->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident_node = (AstIdentNode*)fn_node->expr;
                if (ident_node->entry->node->node_type == AST_NODE_FUNC) {
                    write_fn_name(tp->code_buf, (AstFuncNode*)ident_node->entry->node, 
                        (AstImportNode*)ident_node->entry->import);
                } else { // variable
                    strbuf_append_char(tp->code_buf, '_');
                    writeNodeSource(tp, fn_node->expr->node);
                }
            }
            else {
                transpile_expr(tp, call_node->function);
            }
            if (fn_type->is_anonymous) {
                strbuf_append_str(tp->code_buf, "->ptr");
            }
        } else { // handle Item
            printf("call function type is not func\n");
            assert(0);
        }       
    }

    // write the params
    strbuf_append_str(tp->code_buf, "(");
    AstNode* arg = call_node->argument;  TypeParam *param_type = fn_type ? fn_type->param : NULL;
    while (arg) {
        // boxing based on arg type and fn definition type
        if (param_type) {
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
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_field_expr(Transpiler* tp, AstFieldNode *field_node) {
    printf("transpile field expr\n");
    if (field_node->object->type->type_id == LMD_TYPE_MAP) {
        strbuf_append_str(tp->code_buf, "map_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        writeNodeSource(tp, field_node->field->node);
        strbuf_append_char(tp->code_buf, ')');
    } 
    else if (field_node->object->type->type_id == LMD_TYPE_ARRAY_INT) {
        transpile_expr(tp, field_node->object);
        strbuf_append_str(tp->code_buf, "->items[");
        writeNodeSource(tp, field_node->field->node);
        strbuf_append_char(tp->code_buf, ']');
    }
    else if (field_node->object->type->type_id == LMD_TYPE_ARRAY) {
        strbuf_append_str(tp->code_buf, "array_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        writeNodeSource(tp, field_node->field->node);
        strbuf_append_char(tp->code_buf, ')');
    }    
    else if (field_node->object->type->type_id == LMD_TYPE_LIST) {
        strbuf_append_str(tp->code_buf, "list_get(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        writeNodeSource(tp, field_node->field->node);
        strbuf_append_char(tp->code_buf, ')');
    }    
    else {
        strbuf_append_str(tp->code_buf, "fn_field(");
        transpile_expr(tp, field_node->object);
        strbuf_append_char(tp->code_buf, ',');
        writeNodeSource(tp, field_node->field->node);
        strbuf_append_char(tp->code_buf, ')');
    }
}

void define_func(Transpiler* tp, AstFuncNode *fn_node, bool as_pointer) {
    // use function body type as the return type for the time being
    Type *ret_type = fn_node->body->type;
    writeType(tp, ret_type);

    // write the function name, with a prefix '_', so as to diff from built-in functions
    strbuf_append_str(tp->code_buf, as_pointer ? " (*" :" ");
    write_fn_name(tp->code_buf, fn_node, NULL);
    if (as_pointer) strbuf_append_char(tp->code_buf, ')');

    // write the params
    strbuf_append_char(tp->code_buf, '(');
    AstNamedNode *param = fn_node->param;
    while (param) {
        if (param != fn_node->param) strbuf_append_str(tp->code_buf, ",");
        writeType(tp, param->type);
        strbuf_append_str(tp->code_buf, " _");
        strbuf_append_str_n(tp->code_buf, param->name.str, param->name.length);
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
    printf("transpile binary type\n");
    TypeBinary* binary_type = (TypeBinary*)((TypeType*)bin_node->type)->type;
    strbuf_append_format(tp->code_buf, "const_type(%d)", binary_type->type_index);
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
    case AST_NODE_ELEMENT:
        transpile_element(tp, (AstElementNode*)expr_node);
        break;
    case AST_NODE_FIELD_EXPR:
        transpile_field_expr(tp, (AstFieldNode*)expr_node);
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
    case AST_NODE_LIST_TYPE:
        TypeType* list_type = (TypeType*)((AstListNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeList*)list_type->type)->type_index);
        break;
    case AST_NODE_ARRAY_TYPE:
        TypeType* array_type = (TypeType*)((AstArrayNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeArray*)array_type->type)->type_index);
        break;
    case AST_NODE_MAP_TYPE:
        TypeType* map_type = (TypeType*)((AstMapNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeMap*)map_type->type)->type_index);
        break;
    case AST_NODE_ELMT_TYPE:
        TypeType* elmt_type = (TypeType*)((AstElementNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeElmt*)elmt_type->type)->type_index);
        break;
    case AST_NODE_FUNC_TYPE:
        TypeType* fn_type = (TypeType*)((AstFuncNode*)expr_node)->type;
        strbuf_append_format(tp->code_buf, "const_type(%d)", 
            ((TypeFunc*)fn_type->type)->type_index);
        break;
    case AST_NODE_BINARY_TYPE:
        transpile_binary_type(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_IMPORT:
        printf("import module\n");
        break;
    default:
        printf("unknown expression type!!!\n");
        break;
    }
}

void define_module_import(Transpiler* tp, AstImportNode *import_node) {
    printf("define import module\n");
    // import module
    if (!import_node->script) { printf("misssing script\n");  return; }
    printf("script reference: %s\n", import_node->script->reference);
    // loop through the public functions in the module
    AstNode *node = import_node->script->ast_root;
    if (!node) { printf("misssing root node\n");  return; }
    assert(node->node_type == AST_SCRIPT);
    node = ((AstScript*)node)->child;
    printf("finding content node\n");
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) break;
        node = node->next;
    }
    if (!node) { printf("misssing content node\n");  return; }
    strbuf_append_format(tp->code_buf, "struct Mod%d {\n", import_node->script->index);
    node = ((AstListNode*)node)->item;
    while (node) {
        if (node->node_type == AST_NODE_FUNC) {
            AstFuncNode *func_node = (AstFuncNode*)node;
            printf("got fn: %.*s, is_public: %d\n", (int)func_node->name.length, func_node->name.str, 
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
                writeType(tp, type);
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
    case AST_NODE_IF_EXPR:
        AstIfExprNode* if_node = (AstIfExprNode*)node;
        define_ast_node(tp, if_node->cond);
        define_ast_node(tp, if_node->then);
        if (if_node->otherwise) {
            define_ast_node(tp, if_node->otherwise);
        }
        break;
    case AST_NODE_LET_STAM:  
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            define_ast_node(tp, declare);
            declare = declare->next;
        }
        break;
    case AST_NODE_PUB_STAM:
        // pub vars need to be brought to global scope in C, to allow them to be exported
        AstNode *decl = ((AstLetNode*)node)->declare;
        while (decl) {
            transpile_assign_expr(tp, (AstNamedNode*)decl);
            decl = decl->next;
        }
        break;    
    case AST_NODE_FOR_EXPR:
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            define_ast_node(tp, loop);
            loop = loop->next;
        }
        if (node->node_type == AST_NODE_FOR_EXPR) {
            define_ast_node(tp, ((AstForNode*)node)->then);
        }
        break;
    case AST_NODE_ASSIGN:
        AstNamedNode* assign = (AstNamedNode*)node;
        define_ast_node(tp, assign->as);
        break;
    case AST_NODE_KEY_EXPR:
        AstNamedNode* key = (AstNamedNode*)node;
        define_ast_node(tp, key->as);
        break;
    case AST_NODE_LOOP:
        define_ast_node(tp, ((AstNamedNode*)node)->as);
        break;
    case AST_NODE_ARRAY:
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            define_ast_node(tp, item);
            item = item->next;
        }        
        break;
    case AST_NODE_LIST:  case AST_NODE_CONTENT:
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
    case AST_NODE_MAP:  case AST_NODE_ELEMENT:
        AstNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            define_ast_node(tp, nm_item);
            nm_item = nm_item->next;
        }
        break;
    case AST_NODE_FIELD_EXPR:
        define_ast_node(tp, ((AstFieldNode*)node)->object);
        define_ast_node(tp, ((AstFieldNode*)node)->field);
        break;
    case AST_NODE_CALL_EXPR:
        define_ast_node(tp, ((AstCallNode*)node)->function);
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            define_ast_node(tp, arg);
            arg = arg->next;
        }
        break;
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:
        define_func(tp, (AstFuncNode*)node, false);
        AstFuncNode* func = (AstFuncNode*)node;
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            define_ast_node(tp, fn_param);
            fn_param = fn_param->next;
        }        
        define_ast_node(tp, func->body);
        break;
    case AST_NODE_IMPORT:
        define_module_import(tp, (AstImportNode*)node);
        break;
    case AST_NODE_SYS_FUNC:
        break;
    default:
        printf("unknown expression type: %d\n", node->node_type);
        break;
    }
}

void transpile_ast(Transpiler* tp, AstScript *script) {
    strbuf_append_str(tp->code_buf, "#include \"lambda/lambda.h\"\n");
    // all (nested) function definitions need to be hoisted to global level
    AstNode* child = script->child;
    while (child) {
        define_ast_node(tp, child);
        child = child->next;
    }    

    // global evaluation, wrapped inside main()
    strbuf_append_str(tp->code_buf, "\nItem main(Context *rt){\n return ");
    child = script->child;
    bool has_content = false;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT || child->node_type == AST_NODE_PRIMARY) {
            transpile_box_item(tp, child);
            has_content = true;
        }
        child = child->next;
    }
    if (!has_content) { strbuf_append_str(tp->code_buf, "ITEM_NULL"); }
    strbuf_append_str(tp->code_buf, ";\n}\n");
}


