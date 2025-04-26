#include "transpiler.h"
#include <math.h>

char* read_text_file(const char *filename);
void write_text_file(const char *filename, const char *content);
TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
void transpile_ast_script(Transpiler* tp, AstScript *script);

void find_errors(TSNode node) {
    if (ts_node_is_error(node)) {
        TSPoint point = ts_node_start_point(node);
        printf("Syntax error at Ln %u, Col %u\n", point.row + 1, point.column + 1);
    }
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        find_errors(ts_node_child(node, i));
    }
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
    
    // check if the syntax tree is valid
    if (ts_node_has_error(root_node)) {
        printf("Syntax tree has errors.\n");
        // find and print syntax errors
        find_errors(root_node);
        return NULL;
    }

    // build the AST from the syntax tree
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    pool_variable_init(&tp->ast_pool, grow_size, tolerance_percent);
    tp->type_list = arraylist_new(16);
    tp->const_list = arraylist_new(16);

    if (strcmp(ts_node_type(root_node), "document") != 0) {
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
        if (tp->jit_context) jit_cleanup(tp->jit_context);
        if (tp->ast_pool) pool_variable_destroy(tp->ast_pool);
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
    LambdaItem ld_item = {.item = item};
    if (ld_item.type_id) { // packed value
        TypeId type_id = ld_item.type_id;
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
            double num = *(double*)ld_item.pointer;
            int exponent;
            double mantissa = frexp(num, &exponent);
            if (-20 < exponent && exponent < 30) {
                printf("num f: %.10f, g: %g, exponent: %d\n", num, num, exponent);
                strbuf_append_format(strbuf, "%.10f", num);
                // trim trailing zeros
                char *end = strbuf->str + strbuf->length - 1;
                while (*end == '0' && end > strbuf->str) { *end-- = '\0'; }
                // if it ends with a dot, remove that too
                if (*end == '.') { *end-- = '\0'; }
                strbuf->length = end - strbuf->str + 1;
            }
            else if (-30 < exponent && exponent <= -20) {
                printf("num g: %g, exp: %d\n", num, exponent);
                strbuf_append_format(strbuf, "%.g", num);
                // remove the zero in exponent, like 'e07'
                char *end = strbuf->str + strbuf->length - 1;
                if (*(end-1) == '0' && *(end-2) == '-' && *(end-3) == 'e') { 
                    *(end-1) = *end;  *end = '\0';
                    strbuf->length = end - strbuf->str; 
                }
            }
            else {
                printf("num g: %g, exponent: %d\n", num, exponent);
                strbuf_append_format(strbuf, "%g", num);
            }
        }
        else if (type_id == LMD_TYPE_STRING) {
            String *string = (String*)ld_item.pointer;
            // todo: escape the string
            strbuf_append_format(strbuf, "\"%s\"", string->str);
        }
        else if (type_id == LMD_TYPE_SYMBOL) {
            String *string = (String*)ld_item.pointer;
            // todo: escape the string
            strbuf_append_format(strbuf, "'%s'", string->str);
        } 
        else if (type_id == LMD_TYPE_DTIME) {
            String *string = (String*)ld_item.pointer;
            // todo: escape the string
            strbuf_append_format(strbuf, "t'%s'", string->str);
        }        
        else {
            strbuf_append_format(strbuf, "unknown type: %d", type_id);
        }        
    }
    else { // pointer types
        TypeId type_id = *((uint8_t*)item);
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
        else {
            strbuf_append_format(strbuf, "unknown type: %d", type_id);
        }
    }
}
