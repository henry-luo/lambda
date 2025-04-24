
#include "transpiler.h"

char* read_text_file(const char *filename);
void write_text_file(const char *filename, const char *content);
TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
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
    if (pri_node->expr && pri_node->expr->node_type != AST_NODE_IDENT) {
        transpile_expr(tp, pri_node->expr);
    } else {
        if (pri_node->expr && pri_node->expr->node_type == AST_NODE_IDENT) {
            // user var name starts with '_'
            strbuf_append_char(tp->code_buf, '_');
        }
        writeNodeSource(tp, pri_node->node);
    }
}

void transpile_binary_expr(Transpiler* tp, AstBinaryNode *bi_node) {
    printf("transpile binary expr\n");
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
    // transpile as C conditional expr
    strbuf_append_str(tp->code_buf, "(");
    transpile_expr(tp, if_node->cond);
    strbuf_append_str(tp->code_buf, ")?(");
    transpile_expr(tp, if_node->then);
    strbuf_append_str(tp->code_buf, "):(");
    transpile_expr(tp, if_node->otherwise);
    strbuf_append_str(tp->code_buf, ")");
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
    strbuf_append_str(tp->code_buf, "list_new(rt,");
    strbuf_append_int(tp->code_buf, type->length);
    strbuf_append_char(tp->code_buf, ',');
    AstNode *item = array_node->item;
    while (item) {
        if (item->type->type_id == LMD_TYPE_INT) {
            strbuf_append_str(tp->code_buf, "i2x(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        else if (item->type->type_id == LMD_TYPE_NULL) {
            strbuf_append_str(tp->code_buf, "ITEM_NULL");
        }
        else if (item->type->type_id == LMD_TYPE_BOOL) {
            strbuf_append_str(tp->code_buf, "b2x(");
            transpile_expr(tp, item);
            strbuf_append_char(tp->code_buf, ')');
        }
        else if (item->type->type_id == LMD_TYPE_DOUBLE) {
            if (item->type->is_literal) {
                strbuf_append_str(tp->code_buf, "const_d2x(");
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
        else if (item->type->type_id == LMD_TYPE_STRING) {
            if (item->type->is_literal) {
                strbuf_append_str(tp->code_buf, "const_s2x(");
                LambdaTypeItem *item_type = (LambdaTypeItem*)item->type;
                strbuf_append_int(tp->code_buf, item_type->const_index);
                strbuf_append_str(tp->code_buf, ")");
            }
            else {
                strbuf_append_str(tp->code_buf, "s2x(");
                transpile_expr(tp, item);
                strbuf_append_char(tp->code_buf, ')');
            }
        }
        else if (item->type->type_id == LMD_TYPE_SYMBOL) {
            strbuf_append_str(tp->code_buf, "const_y2x(");
            LambdaTypeSymbol *sym_type = (LambdaTypeSymbol*)item->type;
            strbuf_append_int(tp->code_buf, sym_type->const_index);
            strbuf_append_str(tp->code_buf, ")");
        }        
        item = item->next;
        if (item) strbuf_append_char(tp->code_buf, ',');
    }
    strbuf_append_str(tp->code_buf, ")");
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
    strbuf_append_str(tp->code_buf, ";\n return v2x(ls);\n}\n");
}

typedef Item (*main_func_t)(Context*);

main_func_t transpile_script(Transpiler *tp, char* source) {
    if (!source) { 
        printf("Error: Source code is NULL\n");
        return NULL; 
    }
    printf("Starting transpiler...\n");
    // create a parser.
    tp->parser = lambda_parser();
    if (tp->parser == NULL) { return NULL; }
    // read the source and parse it
    tp->source = source;
    tp->syntax_tree = lambda_parse_source(tp->parser, tp->source);
    if (tp->syntax_tree == NULL) {
        printf("Error: Failed to parse the source code.\n");
        return NULL;
    }

    // print the syntax tree as an s-expr.
    printf("Syntax tree: ---------\n");
    TSNode root_node = ts_tree_root_node(tp->syntax_tree);
    print_ts_node(root_node, 0);
    
    // todo: verify the source tree, report errors if any
    // we'll transpile functions without error, and ignore the rest

    // build the AST from the syntax tree
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    printf("init AST node pool\n");
    pool_variable_init(&tp->ast_pool, grow_size, tolerance_percent);
    tp->type_list = arraylist_new(16);
    tp->const_list = arraylist_new(16);

    if (ts_node_is_null(root_node) || strcmp(ts_node_type(root_node), "document") != 0) {
        printf("Error: The tree has no valid root node.\n");
        return NULL;
    }
    // build the AST
    tp->ast_root = build_script(tp, root_node);
    // print the AST for debugging
    printf("AST: ---------\n");
    print_ast_node(tp->ast_root, 0);

    // transpile the AST to C code
    printf("transpiling...\n");
    tp->code_buf = strbuf_new_cap(1024);
    transpile_ast_script(tp, (AstScript*)tp->ast_root);

    // JIT compile the C code
    tp->jit_context = jit_init();
    // compile user code to MIR
    write_text_file("_transpiled.c", tp->code_buf->str);
    printf("transpiled code:\n----------------\n%s\n", tp->code_buf->str);    
    jit_compile(tp->jit_context, tp->code_buf->str, tp->code_buf->length, "main.c");
    strbuf_free(tp->code_buf);
    
    // generate the native code and return the function
    main_func_t main_func = jit_gen_func(tp->jit_context, "main");
    return main_func;
}

void runner_init(Runner* runner) {
    memset(runner, 0, sizeof(Runner));
    runner->transpiler = (Transpiler*)malloc(sizeof(Transpiler));
    memset(runner->transpiler, 0, sizeof(Transpiler));
}

void runner_cleanup(Runner* runner) {
    printf("runner cleanup\n");
    if (runner->heap) heap_destroy(runner->heap);
    if (runner->stack) pack_free(runner->stack);
    Transpiler *tp = runner->transpiler;
    if (tp) {
        jit_cleanup(tp->jit_context);
        pool_variable_destroy(tp->ast_pool);
        if (tp->type_list) arraylist_free(tp->type_list);
        if (tp->syntax_tree) ts_tree_delete(tp->syntax_tree);
        if (tp->parser) ts_parser_delete(tp->parser);    
        free(tp);
    }
}

Item run_script(Runner *runner, char* source) {
    main_func_t main_func = transpile_script(runner->transpiler, source);
    // execute the function
    if (!main_func) { 
        printf("Error: Failed to compile the function.\n"); 
        return ITEM_NULL;
    } else {
        printf("Executing JIT compiled code...\n");
        runner->heap = heap_init(4096 * 16);  // 64k
        runner->stack = pack_init(256);
        Context runtime_context = {.ast_pool = runner->transpiler->ast_pool, 
            .type_list = runner->transpiler->type_list, 
            .consts = runner->transpiler->const_list->data, 
            .heap = runner->heap, .stack = runner->stack,
        };
        Item ret = main_func(&runtime_context);
        printf("JIT compiled code returned: %llu\n", ret);
        return ret;
    }
}

Item run_script_at(Runner *runner, char* script_path) {
    char* source = read_text_file(script_path);
    return run_script(runner, source);
}

void print_item(StrBuf *strbuf, Item item) {
    printf("print item: %llu\n", item);
    LambdaItem ld_item = {.item = item};
    if (ld_item.type_id) { // packed value
        TypeId type_id = ld_item.type_id;
        printf("packed value: %d\n", type_id);
        if (type_id == LMD_TYPE_NULL) {
            strbuf_append_str(strbuf, "null");
        } 
        else if (type_id == LMD_TYPE_BOOL) {
            strbuf_append_str(strbuf, ld_item.bool_val ? "true" : "false");
        }
        else if (type_id == LMD_TYPE_INT) {
            int int_val = (int32_t)ld_item.int_val;
            strbuf_append_format(strbuf, "%d", int_val);
        }
        else if (type_id == LMD_TYPE_DOUBLE) {
            strbuf_append_format(strbuf, "%g", *(double*)ld_item.pointer);
        }
        else if (type_id == LMD_TYPE_STRING) {
            LambdaTypeString *str_type = (LambdaTypeString*)ld_item.pointer;
            // todo: escape the string
            strbuf_append_format(strbuf, "\"%s\"", str_type->str);
        }
        else if (type_id == LMD_TYPE_SYMBOL) {
            LambdaTypeSymbol *str_type = (LambdaTypeSymbol*)ld_item.pointer;
            // todo: escape the string
            strbuf_append_format(strbuf, "'%s'", str_type->str);
        }        
        else {
            strbuf_append_format(strbuf, "unknown type: %d", type_id);
        }        
    }
    else { // pointer types
        printf("pointer: %llu\n", item);
        TypeId type_id = *((uint8_t*)item);
        printf("pointer type: %d\n", type_id);
        if (type_id == LMD_TYPE_LIST) {
            List *list = (List*)item;
            printf("print list: %p, length: %d\n", list, list->length);
            strbuf_append_char(strbuf, '(');
            for (int i = 0; i < list->length; i++) {
                if (i) strbuf_append_char(strbuf, ',');
                print_item(strbuf, list->items[i]);
            }
            strbuf_append_char(strbuf, ')');
        }
        else if (type_id == LMD_TYPE_ARRAY) {
            strbuf_append_str(strbuf, "Array");
        }
        else if (type_id == LMD_TYPE_MAP) {
            strbuf_append_str(strbuf, "Map");
        }
        // else if (type_id == LMD_TYPE_STRING) {
        //     strbuf_append_format(strbuf, "\"%s\"", ld_item.string_val);
        // }        
        else {
            strbuf_append_format(strbuf, "unknown type: %d", type_id);
        }
    }
}

