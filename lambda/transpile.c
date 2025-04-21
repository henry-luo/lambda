
#include "transpiler.h"
#include "lambda.h"

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
        strbuf_append_str(tp->code_buf, "long");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(tp->code_buf, "double");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(tp->code_buf, "char*");
        break;
    case LMD_TYPE_ARRAY:
        LambdaTypeArray *array_type = (LambdaTypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_INT) {
            strbuf_append_str(tp->code_buf, "ArrayLong*");
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
    strbuf_append_char(tp->code_buf, '(');
    
    TSNode op_node = ts_node_child_by_field_name(bi_node->node, "operator", 8);
    StrView op = ts_node_source(tp, op_node);
    printf("op: %.*s\n", (int)op.length, op.str);
    if (strncmp(op.str, "and", 3) == 0 || strncmp(op.str, "or", 2) == 0) {
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
        if (strncmp(op.str, "or", 2) == 0) {
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
    }
    else {
        transpile_expr(tp, bi_node->left);
        strbuf_append_char(tp->code_buf, ' ');
        strbuf_append_str_n(tp->code_buf, op.str, op.length);
        strbuf_append_char(tp->code_buf, ' ');
        transpile_expr(tp, bi_node->right);
    }
    strbuf_append_char(tp->code_buf, ')');
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
    if (tp->phase == TP_DECLARE) {
        AstNode *declare = let_node->declare;
        while (declare) {
            assert(declare->node_type == AST_NODE_ASSIGN);
            strbuf_append_char(tp->code_buf, ' ');
            transpile_assign_expr(tp, (AstNamedNode*)declare);
            declare = declare->next;
        }
    }
    else if (tp->phase == TP_COMPOSE) {
        printf("transpile let then\n");
        transpile_expr(tp, let_node->then);
    }
}

void transpile_let_stam(Transpiler* tp, AstLetNode *let_node) {
    printf("transpile let stam\n");
    if (tp->phase == TP_DECLARE) {
        AstNode *declare = let_node->declare;
        while (declare) {
            assert(declare->node_type == AST_NODE_ASSIGN);
            transpile_assign_expr(tp, (AstNamedNode*)declare);
            declare = declare->next;
        }
    }
    else if (tp->phase == TP_COMPOSE) {
        // let stam does not have then clause
    }
}

void transpile_loop_expr(Transpiler* tp, AstNamedNode *loop_node, AstNode* for_then) {
    printf("transpile loop expr\n");
    // todo: prefix var name with '_'
    strbuf_append_str(tp->code_buf, " ArrayLong *arr=");
    transpile_expr(tp, loop_node->then);
    strbuf_append_str(tp->code_buf, ";\n for (int i=0; i<arr->length; i++){\n long _");
    strbuf_append_str_n(tp->code_buf, loop_node->name.str, loop_node->name.length);
    strbuf_append_str(tp->code_buf, "=arr->items[i];\n");
    AstNode *next_loop = loop_node->next;
    if (next_loop) {
        printf("transpile nested loop\n");
        transpile_loop_expr(tp, (AstNamedNode*)next_loop, for_then);
    }
    else {
        strbuf_append_str(tp->code_buf, " list_long_push(ls,");
        transpile_expr(tp, for_then);
        strbuf_append_str(tp->code_buf, ");");
    }
    strbuf_append_str(tp->code_buf, " }\n");
}

void transpile_for_expr(Transpiler* tp, AstForNode *for_node) {
    printf("transpile for expr\n");
    // init a list
    strbuf_append_str(tp->code_buf, "({ListLong* ls=list_long();\n");
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
        ? "array_long_new(" : "array_new(");
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
    case AST_NODE_MAP:
        transpile_map_expr(tp, (AstMapNode*)expr_node);
        break;        
    case AST_NODE_FIELD_EXPR:
        transpile_field_expr(tp, (AstFieldNode*)expr_node);
        break;
    case AST_NODE_CALL_EXPR:
        transpile_call_expr(tp, (AstCallNode*)expr_node);
        break;     
    default:
        printf("unknown expression type\n");
        break;
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
    strbuf_append_str(tp->code_buf, "){\n");
   
    // get the function body
    tp->phase = TP_DECLARE;
    AstNode *body = fn_node->body;
    if (body->node_type == AST_NODE_LET_EXPR) {
        transpile_let_expr(tp, (AstLetNode*)body);
    }
    
    tp->phase = TP_COMPOSE;
    strbuf_append_char(tp->code_buf, ' ');
    writeType(tp, ret_type);
    strbuf_append_str(tp->code_buf, " ret=");
    transpile_expr(tp, body);
    strbuf_append_str(tp->code_buf, ";\n return ret;\n}\n");
}

void transpile_ast_script(Transpiler* tp, AstScript *script) {
    strbuf_append_str(tp->code_buf, "#include \"lambda/lambda.h\"\n");

    AstNode *node = script->child;
    tp->phase = TP_DECLARE;
    while (node) {
        if (node->node_type == AST_NODE_LET_STAM) {
            transpile_let_stam(tp, (AstLetNode*)node);
        }
        node = node->next;
    }
    tp->phase = TP_COMPOSE;
    node = script->child;
    while (node) {
        if (node->node_type == AST_NODE_FUNC) {
            transpile_fn(tp, (AstFuncNode*)node);
        }
        node = node->next;
    }
    strbuf_append_str(tp->code_buf, "int main(Context *rt) {\n"
        " void* ret=_main(rt); printf(\"%s\\n\", (char*)ret);\n"
        " return 0;\n}\n");
}

typedef int (*main_func_t)(Context*);

main_func_t transpile_script(Transpiler *tp, char* script_path) {
    printf("Starting transpiler...\n");
    // create a parser.
    tp->parser = lambda_parser();
    if (tp->parser == NULL) { return NULL; }
    // read the source and parse it
    tp->source = read_text_file(script_path);
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
    write_text_file("hello-world.c", tp->code_buf->str);
    printf("transpiled code:\n----------------\n%s\n", tp->code_buf->str);    
    jit_compile(tp->jit_context, tp->code_buf->str, tp->code_buf->length, "main.c");
    strbuf_free(tp->code_buf);
    
    // generate the native code and return the function
    
    main_func_t main_func = jit_gen_func(tp->jit_context, "main");
    return main_func;
}

void run_script(char* script_path) {
    Runner runner;
    memset(&runner, 0, sizeof(Runner));
    runner.transpiler = (Transpiler*)malloc(sizeof(Transpiler));
    memset(runner.transpiler, 0, sizeof(Transpiler));

    main_func_t main_func = transpile_script(runner.transpiler, script_path);
    // execute the function
    if (!main_func) { printf("Error: Failed to compile the function.\n"); }
    else {
        printf("Executing JIT compiled code...\n");
        runner.heap = heap_init(4096 * 16);  // 64k
        Context runtime_context = {.ast_pool = runner.transpiler->ast_pool, 
            .type_list = runner.transpiler->type_list, .heap = runner.heap};
        int ret = main_func(&runtime_context);
        printf("JIT compiled code returned: %d\n", ret);
        heap_destroy(runner.heap);
    }

    // cleanup
    Transpiler *tp = runner.transpiler;
    if (tp) {
        jit_cleanup(tp->jit_context);
        free((void*)tp->source);
        pool_variable_destroy(tp->ast_pool);
        arraylist_free(tp->type_list);
        if (tp->syntax_tree) ts_tree_delete(tp->syntax_tree);
        if (tp->parser) ts_parser_delete(tp->parser);    
        free(tp);
    }
}

int main(void) {
    run_script("test/hello-world.ls");  
    return 0;
}