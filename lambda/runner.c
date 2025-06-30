#include "transpiler.h"
#include <time.h>

char* read_text_file(const char *filename);
void write_text_file(const char *filename, const char *content);
TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
void transpile_ast(Transpiler* tp, AstScript *script);
void check_memory_leak();
void print_heap_entries();
int dataowner_compare(const void *a, const void *b, void *udata);
uint64_t dataowner_hash(const void *item, uint64_t seed0, uint64_t seed1);

// thread-specific runtime context
__thread Context* context = NULL;

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

void print_time_elapsed(char* label, struct timespec start, struct timespec end) {
    // Calculate elapsed time in milliseconds
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000;
    }
    double elapsed_ms = seconds * 1000.0 + nanoseconds / 1e6;
    printf("%s took %.3f ms\n", label, elapsed_ms);
}

void find_script_func() {

}
void init_module_import(Transpiler *tp, AstScript *script) {
    printf("init imports of script\n");
    AstNode* child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_IMPORT) {
            AstImportNode* import = (AstImportNode*)child;
            printf("init import: %.*s\n", (int)(import->module.length), import->module.str);
            // find the module bss item
            char buf[256];
            snprintf(buf, sizeof(buf), "m%d", import->script->index);
            MIR_item_t imp = find_import(tp->jit_context, buf);
            printf("imported item: %p\n", imp);
            if (!imp) {
                printf("Error: Failed to find import item for module %.*s\n", 
                    (int)(import->module.length), import->module.str);
                return;
            }
            uint8_t* mod_def = imp->addr;
            // loop through the public functions in the module
            AstNode *node = import->script->ast_root;
            assert(node->node_type == AST_SCRIPT);
            node = ((AstScript*)node)->child;
            printf("finding content node\n");
            while (node) {
                if (node->node_type == AST_NODE_CONTENT) break;
                node = node->next;
            }
            if (!node) { printf("misssing content node\n");  return; }
            node = ((AstListNode*)node)->item;
            while (node) {
                if (node->node_type == AST_NODE_FUNC) {
                    AstFuncNode *func_node = (AstFuncNode*)node;
                    if (((TypeFunc*)func_node->type)->is_public) {
                        // get func addr
                        StrBuf *func_name = strbuf_new();
                        write_fn_name(func_name, func_node, NULL);
                        printf("loading fn addr: %s from script: %s\n", func_name->str, import->script->reference);
                        void* fn_ptr = find_func(import->script->jit_context, func_name->str);
                        printf("got fn: %s, func_ptr: %p\n", func_name->str, fn_ptr);
                        strbuf_free(func_name);
                        if (!fn_ptr) { printf("misssing content node\n");  return; }
                        *(main_func_t*) mod_def = (main_func_t)fn_ptr;
                        mod_def += sizeof(main_func_t);
                    }
                }
                else if (node->node_type == AST_NODE_PUB_STAM) {
                    AstLetNode *pub_node = (AstLetNode*)node;
                    // loop through the declarations
                    AstNode *declare = pub_node->declare;
                    while (declare) {
                        AstNamedNode *dec_node = (AstNamedNode*)declare;
                        // write the variable name
                        StrBuf *var_name = strbuf_new();
                        write_var_name(var_name, dec_node, NULL);
                        printf("loading pub var: %s from script: %s\n", var_name->str, import->script->reference);
                        void* data_ptr = find_data(import->script->jit_context, var_name->str);
                        printf("got pub var addr: %s, %p\n", var_name->str, data_ptr);
                        // copy the data
                        int bytes = type_info[dec_node->type->type_id].byte_size;
                        memcpy(mod_def, data_ptr, bytes);
                        mod_def += bytes;
                        strbuf_free(var_name);
                        declare = declare->next;
                    }
                }
                node = node->next;
            }
        }
        child = child->next;
    }    
}

void transpile_script(Transpiler *tp, const char* source, const char* script_path) {
    if (!source) {
        printf("Error: Source code is NULL\n");
        return; 
    }
    printf("Start transpiling %s...\n", script_path);
    struct timespec start, end;
    
    // create a parser
    clock_gettime(CLOCK_MONOTONIC, &start);
    // parse the source
    tp->source = source;
    tp->syntax_tree = lambda_parse_source(tp->parser, tp->source);
    if (tp->syntax_tree == NULL) {
        printf("Error: Failed to parse the source code.\n");
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    print_time_elapsed("parsing", start, end);

    // print the syntax tree as an s-expr.
    printf("Syntax tree: ---------\n");
    TSNode root_node = ts_tree_root_node(tp->syntax_tree);
    print_ts_node(tp->source, root_node, 0);
    
    // check if the syntax tree is valid
    if (ts_node_has_error(root_node)) {
        printf("Syntax tree has errors.\n");
        // find and print syntax errors
        find_errors(root_node);
        return;
    }

    // build the AST from the syntax tree
    clock_gettime(CLOCK_MONOTONIC, &start);
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    pool_variable_init(&tp->ast_pool, grow_size, tolerance_percent);
    tp->type_list = arraylist_new(16);
    tp->const_list = arraylist_new(16);
    if (strcmp(ts_node_type(root_node), "document") != 0) {
        printf("Error: The tree has no valid root node.\n");
        return;
    }
    // build the AST
    tp->ast_root = build_script(tp, root_node);
    clock_gettime(CLOCK_MONOTONIC, &end);
    print_time_elapsed("building AST", start, end);
    // print the AST for debugging
    printf("AST: %s ---------\n", tp->reference);
    print_ast_node(tp->ast_root, 0);

    // transpile the AST to C code
    printf("transpiling...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    tp->code_buf = strbuf_new_cap(1024);
    transpile_ast(tp, (AstScript*)tp->ast_root);
    clock_gettime(CLOCK_MONOTONIC, &end);
    print_time_elapsed("transpiling", start, end);

    // JIT compile the C code
    clock_gettime(CLOCK_MONOTONIC, &start);
    tp->jit_context = jit_init();
    // compile user code to MIR
    write_text_file("_transpiled.c", tp->code_buf->str);
    printf("transpiled code:\n----------------\n%s\n", tp->code_buf->str);    
    jit_compile_to_mir(tp->jit_context, tp->code_buf->str, tp->code_buf->length, script_path);
    strbuf_free(tp->code_buf);  tp->code_buf = NULL;
    // generate native code and return the function
    tp->main_func = jit_gen_func(tp->jit_context, "main");
    clock_gettime(CLOCK_MONOTONIC, &end);
    // init lambda imports
    init_module_import(tp, (AstScript*)tp->ast_root);

    printf("JIT compiled %s\n", script_path);
    printf("jit_context: %p, main_func: %p\n", tp->jit_context, tp->main_func);
    print_time_elapsed(":", start, end);
}

Script* load_script(Runtime *runtime, const char* script_path, const char* source) {
    printf("loading script: %s\n", script_path);
    // find the script in the list of scripts
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script *script = (Script*)runtime->scripts->data[i];
        if (strcmp(script->reference, script_path) == 0) {
            printf("Script %s is already loaded.\n", script_path);
            return script;
        }
    }
    // script not found, create a new one
    printf("Script %s not found, loading...\n", script_path);
    Script *new_script = (Script*)calloc(1, sizeof(Script));
    new_script->reference = strdup(script_path);
    new_script->source = source ? source : read_text_file(script_path);
    arraylist_append(runtime->scripts, new_script);
    new_script->index = runtime->scripts->length - 1;

    Transpiler transpiler;  memset(&transpiler, 0, sizeof(Transpiler));
    memcpy(&transpiler, new_script, sizeof(Script));
    transpiler.parser = runtime->parser;  transpiler.runtime = runtime;
    transpile_script(&transpiler, new_script->source, script_path);
    memcpy(new_script, &transpiler, sizeof(Script));
    new_script->main_func = transpiler.main_func;
    printf("loaded main func: %p\n", new_script->main_func);
    return new_script;
}

void runner_init(Runtime *runtime, Runner* runner) {
    memset(runner, 0, sizeof(Runner));
}

#include <lexbor/url/url.h>
lxb_url_t* get_current_dir();

void runner_setup_context(Runner* runner) {
    printf("runner setup exec context\n");
    runner->context.ast_pool = runner->script->ast_pool;
    runner->context.type_list = runner->script->type_list;
    runner->context.type_info = type_info;
    runner->context.consts = runner->script->const_list->data;
    runner->context.stack = pack_init(16);
    runner->context.result = ITEM_NULL;  // exec result
    runner->context.cwd = get_current_dir();
    context = &runner->context;
    heap_init();
    frame_start();
}

void runner_cleanup(Runner* runner) {
    printf("runner cleanup\n");
    // free final result
    if (runner->context.heap) {
        print_heap_entries();
        printf("freeing final result -----------------\n");
        if (runner->context.result) free_item(runner->context.result, true);
        // check memory leaks
        check_memory_leak();
        heap_destroy();
        if (runner->context.stack) pack_free(runner->context.stack);
    }
}

Item run_script(Runtime *runtime, const char* source, char* script_path) {
    Runner runner;
    runner_init(runtime, &runner);
    runner.script = load_script(runtime, script_path, source);
    // execute the function
    Item result;
    if (!runner.script || !runner.script->main_func) { 
        printf("Error: Failed to compile the function.\n"); 
        result = ITEM_NULL;
    } else {
        printf("Executing JIT compiled code...\n");
        runner_setup_context(&runner);
        printf("exec main func\n");
        result = context->result = runner.script->main_func(context);
        printf("after main func\n");
        // runner_cleanup() later
    }
    return result;
}

Item run_script_at(Runtime *runtime, char* script_path) {
    return run_script(runtime, NULL, script_path);
}

void runtime_init(Runtime* runtime) {
    memset(runtime, 0, sizeof(Runtime));
    runtime->parser = lambda_parser();
    runtime->scripts = arraylist_new(16);
}

void runtime_cleanup(Runtime* runtime) {
    if (runtime->parser) ts_parser_delete(runtime->parser);
    if (runtime->scripts) {
        for (int i = 0; i < runtime->scripts->length; i++) {
            Script *script = (Script*)runtime->scripts->data[i];
            if (script->source) free((void*)script->source);
            if (script->syntax_tree) ts_tree_delete(script->syntax_tree);
            if (script->ast_pool) pool_variable_destroy(script->ast_pool);
            if (script->type_list) arraylist_free(script->type_list);
            if (script->jit_context) jit_cleanup(script->jit_context);
            free(script);
        }
        arraylist_free(runtime->scripts);
    }
}
