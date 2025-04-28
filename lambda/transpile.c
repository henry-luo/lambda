
#include "transpiler.h"

void transpile_expr(Transpiler* tp, AstNode *expr_node);

void writeNodeSource(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    const char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

void writeType(Transpiler* tp, LambdaType *type) {
    TypeId type_id = type->type_id;
    switch (type_id) {
    case LMD_TYPE_NULL:
        strbuf_append_str(tp->code_buf, "void*");
        break;
    case LMD_TYPE_ANY:
        strbuf_append_str(tp->code_buf, "Item");
        break;
    case LMD_TYPE_ERROR:
        strbuf_append_str(tp->code_buf, "Item");
        break;        
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "bool");
        break;        
    case LMD_TYPE_INT:
        strbuf_append_str(tp->code_buf, "int");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(tp->code_buf, "float");
        break;
    case LMD_TYPE_DOUBLE:
        strbuf_append_str(tp->code_buf, "double");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(tp->code_buf, "char*");
        break;
    case LMD_TYPE_ARRAY:
        LambdaTypeArray *array_type = (LambdaTypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_INT) {
            strbuf_append_str(tp->code_buf, "ArrayInt*");
        } else {
            strbuf_append_str(tp->code_buf, "Array*");
        }
        break;
    case LMD_TYPE_MAP:
        strbuf_append_str(tp->code_buf, "Map*");
        break;
    default:
        printf("unknown type %d\n", type_id);
    }
}

void transpile_primary_expr(Transpiler* tp, AstPrimaryNode *pri_node) {
    printf("transpile primary expr\n");
    if (pri_node->expr) {
        if (pri_node->expr->node_type == AST_NODE_IDENT) {
            // user var name starts with '_'
            strbuf_append_char(tp->code_buf, '_');
            writeNodeSource(tp, pri_node->node);
        } else { 
            transpile_expr(tp, pri_node->expr);
        }
    } else { // literals
        if (pri_node->type->is_literal && (pri_node->type->type_id == LMD_TYPE_STRING || pri_node->type->type_id == LMD_TYPE_SYMBOL)) {
            // loads the const string without boxing
            strbuf_append_str(tp->code_buf, "const_s(");
            LambdaTypeString *str_type = (LambdaTypeString*)pri_node->type;
            strbuf_append_int(tp->code_buf, str_type->const_index);
            strbuf_append_char(tp->code_buf, ')');
        } else {
            writeNodeSource(tp, pri_node->node);
        }
    }
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
    else if (bi_node->op == OPERATOR_ADD && bi_node->left->type->type_id == LMD_TYPE_STRING) {
        strbuf_append_str(tp->code_buf, "str_cat(");
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');
    }
    else if (bi_node->op == OPERATOR_SUB && bi_node->left->type->type_id == LMD_TYPE_STRING) {
        strbuf_append_str(tp->code_buf, "str_sub(");
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ',');
        transpile_expr(tp, bi_node->right);
        strbuf_append_char(tp->code_buf, ')');

    }
    else if (bi_node->op == OPERATOR_DIV && bi_node->left->type->type_id == LMD_TYPE_INT && 
        bi_node->right->type->type_id == LMD_TYPE_INT) {
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
    strbuf_append_str(tp->code_buf, "(");
    transpile_expr(tp, if_node->cond);
    strbuf_append_str(tp->code_buf, ")?(");
    transpile_expr(tp, if_node->then);
    strbuf_append_str(tp->code_buf, "):(");
    transpile_expr(tp, if_node->otherwise);
    strbuf_append_str(tp->code_buf, ")");
    printf("end if expr\n");
}

void transpile_assign_expr(Transpiler* tp, AstNamedNode *asn_node) {
    printf("transpile assign expr\n");
    // declare the type
    LambdaType *type = asn_node->then->type;
    writeType(tp, type);
    // user var name starts with '_'
    strbuf_append_str(tp->code_buf, " _");
    // declare the variable
    strbuf_append_str_n(tp->code_buf, asn_node->name.str, asn_node->name.length);
    strbuf_append_char(tp->code_buf, '=');

    transpile_expr(tp, asn_node->then);
    strbuf_append_str(tp->code_buf, ";\n");
}

void transpile_let_expr(Transpiler* tp, AstLetNode *let_node) {
    printf("transpile let expr\n");
    strbuf_append_str(tp->code_buf, " ({");
    AstNode *declare = let_node->declare;
    while (declare) {
        assert(declare->node_type == AST_NODE_ASSIGN);
        transpile_assign_expr(tp, (AstNamedNode*)declare);
        strbuf_append_char(tp->code_buf, ' ');
        declare = declare->next;
    }
    if (let_node->then) {
        printf("transpile let then\n");
        transpile_expr(tp, let_node->then);
    }
    strbuf_append_str(tp->code_buf, ";})");
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

void transpile_loop_expr(Transpiler* tp, AstNamedNode *loop_node, AstNode* for_then) {
    printf("transpile loop expr\n");
    // todo: prefix var name with '_'
    strbuf_append_str(tp->code_buf, " ArrayInt *arr=");
    transpile_expr(tp, loop_node->then);
    strbuf_append_str(tp->code_buf, ";\n for (int i=0; i<arr->length; i++){\n int _");
    strbuf_append_str_n(tp->code_buf, loop_node->name.str, loop_node->name.length);
    strbuf_append_str(tp->code_buf, "=arr->items[i];\n");
    AstNode *next_loop = loop_node->next;
    if (next_loop) {
        printf("transpile nested loop\n");
        transpile_loop_expr(tp, (AstNamedNode*)next_loop, for_then);
    }
    else {
        strbuf_append_str(tp->code_buf, " list_int_push(ls,");
        transpile_expr(tp, for_then);
        strbuf_append_str(tp->code_buf, ");");
    }
    strbuf_append_str(tp->code_buf, " }\n");
}

void transpile_for_expr(Transpiler* tp, AstForNode *for_node) {
    printf("transpile for expr\n");
    // init a list
    strbuf_append_str(tp->code_buf, "({ListInt* ls=list_int();\n");
    AstNode *loop = for_node->loop;
    if (loop) {
        printf("transpile for loop\n");
        transpile_loop_expr(tp, (AstNamedNode*)loop, for_node->then);
    }
    // return the list
    strbuf_append_str(tp->code_buf, " ls;})");
}

void transpile_array_expr(Transpiler* tp, AstArrayNode *array_node) {
    printf("transpile array expr\n");
    LambdaTypeArray *type = (LambdaTypeArray*)array_node->type;
    strbuf_append_str(tp->code_buf, (type->nested && type->nested->type_id == LMD_TYPE_INT) 
        ? "array_int_new(" : "array_new(");
    strbuf_append_int(tp->code_buf, type->length);
    strbuf_append_char(tp->code_buf, ',');
    AstNode *item = array_node->item;
    while (item) {
        transpile_expr(tp, item);
        if (item->next) {
            strbuf_append_char(tp->code_buf, ',');
        }
        item = item->next;
    }
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_list_expr(Transpiler* tp, AstArrayNode *array_node) {
    printf("transpile list expr\n");
    LambdaTypeArray *type = (LambdaTypeArray*)array_node->type;
    strbuf_append_str(tp->code_buf, "({");
    // let declare first
    AstNode *item = array_node->item;
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
    item = array_node->item;  bool is_first = true;
    while (item) {
        if (item->node_type == AST_NODE_LET_STAM) { item = item->next; continue; }
        if (is_first) { is_first = false; } 
        else { strbuf_append_char(tp->code_buf, ','); }
        if (item->type->type_id == LMD_TYPE_INT) {
            strbuf_append_str(tp->code_buf, "i2it(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        else if (item->type->type_id == LMD_TYPE_NULL) {
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        else if (item->type->type_id == LMD_TYPE_BOOL) {
            strbuf_append_str(tp->code_buf, "b2it(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        else if (item->type->type_id == LMD_TYPE_DOUBLE) {
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
            item->type->type_id == LMD_TYPE_DTIME) {
            char t = item->type->type_id == LMD_TYPE_STRING ? 's' :
                (item->type->type_id == LMD_TYPE_SYMBOL ? 'y' : 'k');
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
        item = item->next;
    }
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
        transpile_expr(tp, item->then);
        if (item->next) { strbuf_append_char(tp->code_buf, ','); }
        item = (AstNamedNode*)item->next;
    }
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_call_expr(Transpiler* tp, AstCallNode *call_node) {
    printf("transpile call expr\n");
    transpile_expr(tp, call_node->function);
    strbuf_append_str(tp->code_buf, "(rt,");
    AstNode* arg = call_node->argument;
    while (arg) {
        transpile_expr(tp, arg);
        if (arg->next) {
            strbuf_append_char(tp->code_buf, ',');
        }
        arg = arg->next;
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

void transpile_fn(Transpiler* tp, AstFuncNode *fn_node) {
    // use function body type as the return type for the time being
    LambdaType *ret_type = fn_node->body->type;
    writeType(tp, ret_type);
    // write the function name, with a prefix '_', so as to diff from built-in functions
    strbuf_append_str(tp->code_buf, " _");
    writeNodeSource(tp, fn_node->name);
    strbuf_append_str(tp->code_buf, "(Context *rt");
    AstNamedNode *param = fn_node->param;
    while (param) {
        strbuf_append_str(tp->code_buf, ", ");
        writeType(tp, param->type);
        strbuf_append_str(tp->code_buf, " _");
        writeNodeSource(tp, param->node);
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
    case AST_NODE_BINARY:
        transpile_binary_expr(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_PRIMARY:
        transpile_primary_expr(tp, (AstPrimaryNode*)expr_node);
        break;
    case AST_NODE_IF_EXPR:
        transpile_if_expr(tp, (AstIfExprNode*)expr_node);
        break;        
    case AST_NODE_LET_EXPR:
        transpile_let_expr(tp, (AstLetNode*)expr_node);
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
        transpile_list_expr(tp, (AstArrayNode*)expr_node);
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
        transpile_fn(tp, (AstFuncNode*)expr_node);
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
            transpile_fn(tp, (AstFuncNode*)node);
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


