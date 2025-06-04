#include "transpiler.h"
#include <time.h>

char* read_text_file(const char *filename);
void write_text_file(const char *filename, const char *content);
TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
void transpile_ast_to_c(Transpiler* tp, AstScript *script);
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

void transpile_script(Transpiler *tp, const char* source, char* script_path) {
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
    printf("AST: ---------\n");
    print_ast_node(tp->ast_root, 0);

    // transpile the AST to C code
    printf("transpiling...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    tp->code_buf = strbuf_new_cap(1024);
    transpile_ast_to_c(tp, (AstScript*)tp->ast_root);
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
    printf("JIT compiled %s", script_path);
    print_time_elapsed(":", start, end);
}

Script* load_script(Runtime *runtime, char* script_path) {
    // find the script in the list of scripts
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script *script = (Script*)runtime->scripts->data[i];
        if (strcmp(script->reference, script_path) == 0) {
            printf("Script %s is already loaded.\n", script_path);
            return script;
        }
    }
    // script not found, create a new one
    Script *new_script = (Script*)calloc(1, sizeof(Script));
    new_script->reference = strdup(script_path);
    new_script->source = read_text_file(script_path);
    arraylist_append(runtime->scripts, new_script);

    Transpiler *transpiler = (Transpiler*)calloc(1, sizeof(Transpiler));
    memcpy(transpiler, new_script, sizeof(Script));
    transpiler->parser = runtime->parser;
    transpile_script(transpiler, new_script->source, script_path);
    memcpy(new_script, transpiler, sizeof(Script));

    return new_script;
}

void runner_init(Runtime *runtime, Runner* runner) {
    memset(runner, 0, sizeof(Runner));
    runner->transpiler = (Transpiler*)malloc(sizeof(Transpiler));
    memset(runner->transpiler, 0, sizeof(Transpiler));
    runner->transpiler->parser = runtime->parser;
}

void runner_setup_context(Runner* runner) {
    printf("runner setup exec context\n");
    runner->context.ast_pool = runner->transpiler->ast_pool;
    runner->context.type_list = runner->transpiler->type_list;
    runner->context.consts = runner->transpiler->const_list->data;
    runner->context.stack = pack_init(16);
    runner->context.result = ITEM_NULL;  // exec result
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
    Transpiler *tp = runner->transpiler;
    if (tp) {
        if (tp->jit_context) jit_cleanup(tp->jit_context);
        if (tp->ast_pool) pool_variable_destroy(tp->ast_pool);
        if (tp->type_list) arraylist_free(tp->type_list);
        if (tp->syntax_tree) ts_tree_delete(tp->syntax_tree);
        free(tp);
    }
}

Item run_script(Runner *runner, const char* source, char* script_path) {
    transpile_script(runner->transpiler, source, script_path);
    // execute the function
    Item result;
    if (!runner->transpiler->main_func) { 
        printf("Error: Failed to compile the function.\n"); 
        result = ITEM_NULL;
    } else {
        printf("Executing JIT compiled code...\n");
        runner_setup_context(runner);
        result = context->result = runner->transpiler->main_func(context);
    }
    return result;
}

Item run_script_at(Runner *runner, char* script_path) {
    const char* source = (const char*)read_text_file(script_path);
    return run_script(runner, source, script_path);
}

void runtime_init(Runtime* runtime) {
    memset(runtime, 0, sizeof(Runtime));
    runtime->parser = lambda_parser();
}

void runtime_cleanup(Runtime* runtime) {
    if (runtime->parser) ts_parser_delete(runtime->parser);
    if (runtime->scripts) {
        for (int i = 0; i < runtime->scripts->length; i++) {
            Script *script = (Script*)runtime->scripts->data[i];
            if (script->source) free((void*)script->source);
            free(script);
        }
        arraylist_free(runtime->scripts);
    }
}
