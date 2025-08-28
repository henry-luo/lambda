#include "transpiler.hpp"
#include "../lib/log.h"
#include "name_pool.h"
#include <time.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

// Windows-specific timing implementation
typedef struct {
    LARGE_INTEGER counter;
} win_timer;

static void get_time(win_timer* timer) {
    QueryPerformanceCounter(&timer->counter);
}

static void print_elapsed_time(const char* label, win_timer start, win_timer end) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    
    double elapsed_ms = ((double)(end.counter.QuadPart - start.counter.QuadPart) * 1000.0) / frequency.QuadPart;
        log_debug("%s took %.3f ms", label, elapsed_ms);
}

#else
// Unix/Linux/macOS version
typedef struct timespec win_timer;

static void get_time(win_timer* timer) {
    clock_gettime(CLOCK_MONOTONIC, timer);
}

static void print_elapsed_time(const char* label, win_timer start, win_timer end) {
    // Calculate elapsed time in milliseconds
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000;
    }
    double elapsed_ms = seconds * 1000.0 + nanoseconds / 1e6;
        log_debug("%s took %.3f ms", label, elapsed_ms);
}
#endif


extern "C" {
char* read_text_file(const char *filename);
void write_text_file(const char *filename, const char *content);
TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
}
void transpile_ast(Transpiler* tp, AstScript *script);
void check_memory_leak();
void print_heap_entries();
int dataowner_compare(const void *a, const void *b, void *udata);
uint64_t dataowner_hash(const void *item, uint64_t seed0, uint64_t seed1);

// thread-specific runtime context
__thread Context* context = NULL;

void find_errors(TSNode node) {
    const char *node_type = ts_node_type(node);
    TSPoint start_point = ts_node_start_point(node);
    TSPoint end_point = ts_node_end_point(node);
    
    // Check for direct syntax error nodes
    if (ts_node_is_error(node)) {
        log_error("Syntax error at Ln %u, Col %u - %u, Col %u: %s", 
               start_point.row + 1, start_point.column + 1,
               end_point.row + 1, end_point.column + 1, node_type);
    }
    
    // Check for missing nodes (inserted by parser for error recovery)
    if (ts_node_is_missing(node)) {
        log_error("Missing node at Ln %u, Col %u: expected %s", 
               start_point.row + 1, start_point.column + 1, node_type);
    }
    
    // Check for ERROR node type specifically (some parsers use this)
    if (strcmp(node_type, "ERROR") == 0) {
        log_error("ERROR node at Ln %u, Col %u - %u, Col %u", 
               start_point.row + 1, start_point.column + 1,
               end_point.row + 1, end_point.column + 1);
    }
    
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        find_errors(ts_node_child(node, i));
    }
}

void init_module_import(Transpiler *tp, AstScript *script) {
        log_debug("init imports of script");
    AstNode* child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_IMPORT) {
            AstImportNode* import = (AstImportNode*)child;
        log_debug("init import: %.*s", (int)(import->module.length), import->module.str);
            // find the module bss item
            char buf[256];
            snprintf(buf, sizeof(buf), "m%d", import->script->index);
            MIR_item_t imp = find_import(tp->jit_context, buf);
        log_debug("imported item: %p", imp);
            if (!imp) {
                log_error("Error: Failed to find import item for module %.*s", 
                    (int)(import->module.length), import->module.str);
                return;
            }
            uint8_t* mod_def = (uint8_t*)imp->addr;
            // loop through the public functions in the module
            AstNode *node = import->script->ast_root;
            assert(node->node_type == AST_SCRIPT);
            node = ((AstScript*)node)->child;
        log_debug("finding content node");
            while (node) {
                if (node->node_type == AST_NODE_CONTENT) break;
                node = node->next;
            }
        log_error("misssing content node");
            node = ((AstListNode*)node)->item;
            while (node) {
                if (node->node_type == AST_NODE_FUNC) {
                    AstFuncNode *func_node = (AstFuncNode*)node;
                    if (((TypeFunc*)func_node->type)->is_public) {
                        // get func addr
                        StrBuf *func_name = strbuf_new();
                        write_fn_name(func_name, func_node, NULL);
        log_debug("loading fn addr: %s from script: %s", func_name->str, import->script->reference);
                        void* fn_ptr = find_func(import->script->jit_context, func_name->str);
        log_debug("got fn: %s, func_ptr: %p", func_name->str, fn_ptr);
                        strbuf_free(func_name);
        log_error("misssing content node");
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
        log_debug("loading pub var: %s from script: %s", var_name->str, import->script->reference);
                        void* data_ptr = find_data(import->script->jit_context, var_name->str);
        log_debug("got pub var addr: %s, %p", var_name->str, data_ptr);
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

extern unsigned int lambda_lambda_h_len;

void transpile_script(Transpiler *tp, Script* script, const char* script_path) {
    if (!script || !script->source) {
        log_error("Error: Source code is NULL");
        return; 
    }
        log_notice("Start transpiling %s...", script_path);
    win_timer start, end;
    
    // create a parser
    get_time(&start);
    // parse the source
    tp->source = script->source;
    tp->syntax_tree = lambda_parse_source(tp->parser, tp->source);
    if (tp->syntax_tree == NULL) {
        log_error("Error: Failed to parse the source code.");
        return;
    }
    get_time(&end);
    print_elapsed_time("parsing", start, end);

    // print the syntax tree as an s-expr.
    // printf("Syntax tree: ---------\n");
    TSNode root_node = ts_tree_root_node(tp->syntax_tree);
    // print_ts_node(tp->source, root_node, 0);
    
    // check if the syntax tree is valid
    if (ts_node_has_error(root_node)) {
        log_error("Syntax tree has errors.");
        log_debug("Root node type: %s", ts_node_type(root_node));
        log_debug("Root node is_error: %d", ts_node_is_error(root_node));
        log_debug("Root node is_missing: %d", ts_node_is_missing(root_node));
        log_debug("Root node has_error: %d", ts_node_has_error(root_node));
        log_debug("Source pointer: %p", script->source);
        
        // find and print syntax errors
        find_errors(root_node);
        return;
    }

    // build the AST from the syntax tree
    get_time(&start);
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    pool_variable_init(&tp->ast_pool, grow_size, tolerance_percent);
    tp->type_list = arraylist_new(16);
    tp->const_list = arraylist_new(16);
    
    // Initialize name pool
    tp->name_pool = name_pool_create(tp->ast_pool, nullptr);
    if (!tp->name_pool) {
        log_error("Error: Failed to create name pool");
        return;
    }
    
    if (strcmp(ts_node_type(root_node), "document") != 0) {
        log_error("Error: The tree has no valid root node.");
        return;
    }
    // build the AST
    tp->ast_root = build_script(tp, root_node);
    get_time(&end);
    print_elapsed_time("building AST", start, end);
    // print the AST for debugging
    log_debug("AST: %s ---------\n", tp->reference);
    print_ast_node(tp, tp->ast_root, 0);

    // transpile the AST to C code
    log_debug("transpiling...");
    get_time(&start);
    tp->code_buf = strbuf_new_cap(1024);
    transpile_ast(tp, (AstScript*)tp->ast_root);
    get_time(&end);
    print_elapsed_time("transpiling", start, end);

    // JIT compile the C code
    get_time(&start);
    tp->jit_context = jit_init();
    // compile user code to MIR
    log_debug("compiling to MIR...");
    write_text_file("_transpiled.c", tp->code_buf->str);
    char* code = tp->code_buf->str + lambda_lambda_h_len;
    printf("code len: %d\n", (int)strlen(code));
    log_debug("transpiled code:\n----------------\n%s", code);
    jit_compile_to_mir(tp->jit_context, tp->code_buf->str, tp->code_buf->length, script_path);
    strbuf_free(tp->code_buf);  tp->code_buf = NULL;
    // generate native code and return the function
    tp->main_func = (main_func_t)jit_gen_func(tp->jit_context, "main");
    get_time(&end);
    // init lambda imports
    init_module_import(tp, (AstScript*)tp->ast_root);

    log_info("JIT compiled %s", script_path);
    log_debug("jit_context: %p, main_func: %p", tp->jit_context, tp->main_func);
    // copy value back to script
    memcpy(script, tp, sizeof(Script));
    script->main_func = tp->main_func;

    print_elapsed_time(":", start, end);
}

Script* load_script(Runtime *runtime, const char* script_path, const char* source) {
    // printf("loading script: %s\n", script_path);
    // find the script in the list of scripts
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script *script = (Script*)runtime->scripts->data[i];
        if (strcmp(script->reference, script_path) == 0) {
            log_info("Script %s is already loaded.", script_path);
            return script;
        }
    }
    // script not found, create a new one
    // printf("Loading %s ...\n", script_path);
    Script *new_script = (Script*)calloc(1, sizeof(Script));
    new_script->reference = strdup(script_path);
    new_script->source = source ? source : read_text_file(script_path);
    arraylist_append(runtime->scripts, new_script);
    new_script->index = runtime->scripts->length - 1;
    
    // Initialize decimal context
    new_script->decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    mpd_maxcontext(new_script->decimal_ctx);

    Transpiler transpiler;  memset(&transpiler, 0, sizeof(Transpiler));
    memcpy(&transpiler, new_script, sizeof(Script));
    transpiler.parser = runtime->parser;  transpiler.runtime = runtime;
    transpile_script(&transpiler, new_script, script_path);
    log_debug("loaded main func: %p", new_script->main_func);
    return new_script;
}

void runner_init(Runtime *runtime, Runner* runner) {
    memset(runner, 0, sizeof(Runner));
}

#include "../lib/url.h"

void runner_setup_context(Runner* runner) {
    log_debug("runner setup exec context");
    runner->context.ast_pool = runner->script->ast_pool;
    runner->context.type_list = runner->script->type_list;
    runner->context.type_info = type_info;
    runner->context.consts = runner->script->const_list->data;
    runner->context.num_stack = num_stack_create(16);
    runner->context.result = ItemNull;  // exec result
    runner->context.cwd = (char*)"./";  // Simple fallback for current directory
    
    // Initialize decimal context
    runner->context.decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    mpd_defaultcontext(runner->context.decimal_ctx);
    
    context = &runner->context;
    heap_init();
    frame_start();
}

void runner_cleanup(Runner* runner) {
    log_debug("runner cleanup");
    // free final result
    if (runner->context.heap) {
        print_heap_entries();
        log_debug("freeing final result -----------------");
        if (runner->context.result.item) free_item(runner->context.result, true);
        // check memory leaks
        check_memory_leak();
        heap_destroy();
        if (runner->context.num_stack) num_stack_destroy((num_stack_t*)runner->context.num_stack);
    }
    
    // Free decimal context
    if (runner->context.decimal_ctx) {
        free(runner->context.decimal_ctx);
        runner->context.decimal_ctx = NULL;
    }
}

Item run_script(Runtime *runtime, const char* source, char* script_path, bool transpile_only) {
    Runner runner;
    runner_init(runtime, &runner);
    runner.script = load_script(runtime, script_path, source);
    if (transpile_only) {
        log_info("Transpiled script %s only, not executing.", script_path);
        return ItemNull;
    }
    // execute the function
    Item result;
    if (!runner.script || !runner.script->main_func) { 
        log_error("Error: Failed to compile the function."); 
        result = ItemNull;
    } else {
        log_debug("Executing JIT compiled code...");
        runner_setup_context(&runner);
        log_debug("exec main func");
        result = context->result = runner.script->main_func(context);
        log_debug("after main func");
        // runner_cleanup() later
    }
    return result;
}

Item run_script_at(Runtime *runtime, char* script_path, bool transpile_only) {
    return run_script(runtime, NULL, script_path, transpile_only);
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
            if (script->decimal_ctx) {
                free(script->decimal_ctx);
                script->decimal_ctx = NULL;
            }
            free(script);
        }
        arraylist_free(runtime->scripts);
    }
}
