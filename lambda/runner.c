#include "transpiler.h"
#include <time.h>

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

main_func_t transpile_script(Transpiler *tp, char* source, char* script_path) {
    if (!source) {
        printf("Error: Source code is NULL\n");
        return NULL; 
    }
    printf("Start transpiling %s...\n", script_path);
    struct timespec start, end;
    
    // create a parser
    clock_gettime(CLOCK_MONOTONIC, &start);
    tp->parser = lambda_parser();
    if (tp->parser == NULL) { return NULL; }
    // read the source and parse it
    tp->source = source;
    tp->syntax_tree = lambda_parse_source(tp->parser, tp->source);
    if (tp->syntax_tree == NULL) {
        printf("Error: Failed to parse the source code.\n");
        return NULL;
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
        return NULL;
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
        return NULL;
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
    transpile_ast_script(tp, (AstScript*)tp->ast_root);
    clock_gettime(CLOCK_MONOTONIC, &end);
    print_time_elapsed("transpiling", start, end);

    // JIT compile the C code
    clock_gettime(CLOCK_MONOTONIC, &start);
    tp->jit_context = jit_init();
    // compile user code to MIR
    write_text_file("_transpiled.c", tp->code_buf->str);
    printf("transpiled code:\n----------------\n%s\n", tp->code_buf->str);    
    jit_compile_to_mir(tp->jit_context, tp->code_buf->str, tp->code_buf->length, script_path);
    strbuf_free(tp->code_buf);
    // generate the native code and return the function
    main_func_t main_func = jit_gen_func(tp->jit_context, "main");
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("JIT compiled %s", script_path);
    print_time_elapsed(":", start, end);  

    return main_func;
}

// thread-specific runtime context
__thread Context* context = NULL;

int dataowner_compare(const void *a, const void *b, void *udata) {
    const DataOwner *da = a;
    const DataOwner *db = b;
    return da->data == db->data;
}

uint64_t dataowner_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const DataOwner *dataowner = item;
    return hashmap_xxhash3(dataowner->data, sizeof(dataowner->data), seed0, seed1);
}

void heap_init() {
    printf("heap init: %p\n", context);
    context->heap = (Heap*)calloc(1, sizeof(Heap));
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    pool_variable_init(&context->heap->pool, grow_size, tolerance_percent);
    context->heap->entries = arraylist_new(1024);
}

void* heap_alloc(size_t size, TypeId type_id) {
    Heap *heap = context->heap;
    void *data;
    pool_variable_alloc(heap->pool, size, (void**)&data);
    if (!data) {
        printf("Error: Failed to allocate memory for heap entry.\n");
        return NULL;
    }
    // scalar pointers needs to be tagged
    arraylist_append(heap->entries, type_id < LMD_TYPE_ARRAY ?
        ((((uint64_t)type_id)<<56) | (uint64_t)(data)) : data);
    return data;
}

void* heap_calloc(size_t size, TypeId type_id) {
    void* ptr = heap_alloc(size, type_id);
    memset(ptr, 0, size);
    return ptr;
}

// void heap_free(HeapEntry* entry) {
    // HeapEntry *entry = (HeapEntry*)((uint8_t*)ptr - sizeof(HeapEntry));
    // if (entry == heap->first) {
    //     heap->first = entry->next;
    //     if (!heap->first) { heap->last = NULL; }
    // } else {
    //     HeapEntry *prev = heap->first;
    //     while (prev && prev->next != entry) { prev = prev->next; }
    //     if (prev) { prev->next = entry->next; }
    //     if (!prev->next) { heap->last = prev; }
    // }
// }

void heap_destroy() {
    if (context->heap) {
        if (context->heap->pool) pool_variable_destroy(context->heap->pool);
        free(context->heap);
    }
}

void runner_init(Runner* runner) {
    memset(runner, 0, sizeof(Runner));
    runner->transpiler = (Transpiler*)malloc(sizeof(Transpiler));
    memset(runner->transpiler, 0, sizeof(Transpiler));
}

void runner_setup_context(Runner* runner) {
    printf("runner setup exec context\n");
    runner->context.ast_pool = runner->transpiler->ast_pool;
    runner->context.type_list = runner->transpiler->type_list;
    runner->context.consts = runner->transpiler->const_list->data;
    runner->context.stack = pack_init(256);
    runner->context.data_owners = hashmap_new(sizeof(DataOwner), 16, 0, 0, 
        dataowner_hash, dataowner_compare, NULL, NULL);
    context = &runner->context;
    heap_init();
}

void runner_cleanup(Runner* runner) {
    printf("runner cleanup\n");
    Transpiler *tp = runner->transpiler;
    if (tp) {
        if (tp->jit_context) jit_cleanup(tp->jit_context);
        if (tp->ast_pool) pool_variable_destroy(tp->ast_pool);
        if (tp->type_list) arraylist_free(tp->type_list);
        if (tp->syntax_tree) ts_tree_delete(tp->syntax_tree);
        if (tp->parser) ts_parser_delete(tp->parser);    
        free(tp);
    }
    heap_destroy();
    if (runner->context.stack) pack_free(runner->context.stack);
    if (runner->context.data_owners) hashmap_free(runner->context.data_owners);
}

Item run_script(Runner *runner, char* source, char* script_path) {
    main_func_t main_func = transpile_script(runner->transpiler, source, script_path);
    // execute the function
    if (!main_func) { 
        printf("Error: Failed to compile the function.\n"); 
        return ITEM_NULL;
    } else {
        printf("Executing JIT compiled code...\n");
        runner_setup_context(runner);
        Item ret = main_func(context);
        return ret;
    }
}

Item run_script_at(Runner *runner, char* script_path) {
    char* source = read_text_file(script_path);
    return run_script(runner, source, script_path);
}

