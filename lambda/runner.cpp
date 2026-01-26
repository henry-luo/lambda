#include <time.h>
#include "transpiler.hpp"
#include "mark_builder.hpp"
#include "lambda_error.h"

#if _WIN32
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
void transpile_ast_root(Transpiler* tp, AstScript *script);
void check_memory_leak();
void print_heap_entries();

// thread-specific runtime context
__thread EvalContext* context = NULL;
extern __thread Context* input_context;

// Persistent last error (survives beyond runner lifetime)
// This is needed because context points to runner's stack-allocated EvalContext
__thread LambdaError* persistent_last_error = NULL;

// Accessor for persistent error from other modules
LambdaError* get_persistent_last_error() {
    return persistent_last_error;
}

void clear_persistent_last_error() {
    if (persistent_last_error) {
        err_free(persistent_last_error);
        persistent_last_error = NULL;
    }
}

void find_errors(TSNode node) {
    const char *node_type = ts_node_type(node);
    TSPoint start_point = ts_node_start_point(node);
    TSPoint end_point = ts_node_end_point(node);

    // Check for direct syntax error nodes
    if (ts_node_is_error(node)) {
        printf("PARSE ERROR: Syntax error at Ln %u, Col %u - %u, Col %u: node_type='%s'\n",
               start_point.row + 1, start_point.column + 1,
               end_point.row + 1, end_point.column + 1, node_type);
        // Print child count to see what's inside the error
        uint32_t child_count = ts_node_child_count(node);
        printf("  Error node has %u children\n", child_count);
        for (uint32_t i = 0; i < child_count && i < 5; i++) {
            TSNode child = ts_node_child(node, i);
            printf("    Child %u: %s\n", i, ts_node_type(child));
        }
        log_error("Syntax error at Ln %u, Col %u - %u, Col %u: %s",
               start_point.row + 1, start_point.column + 1,
               end_point.row + 1, end_point.column + 1, node_type);
    }

    // Check for missing nodes (inserted by parser for error recovery)
    if (ts_node_is_missing(node)) {
        printf("PARSE ERROR: Missing node at Ln %u, Col %u: expected '%s'\n",
               start_point.row + 1, start_point.column + 1, node_type);
        log_error("Missing node at Ln %u, Col %u: expected %s",
               start_point.row + 1, start_point.column + 1, node_type);
    }

    // Check for ERROR node type specifically (some parsers use this)
    if (strcmp(node_type, "ERROR") == 0) {
        printf("PARSE ERROR: ERROR node at Ln %u, Col %u - %u, Col %u\n",
               start_point.row + 1, start_point.column + 1,
               end_point.row + 1, end_point.column + 1);
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
    log_enter();
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
                goto RETURN;
            }
            uint8_t* mod_def = (uint8_t*)imp->addr;
            // loop through the public functions in the module
            AstNode *node = import->script->ast_root;
            assert(node->node_type == AST_SCRIPT);
            node = ((AstScript*)node)->child;
            while (node) {
                log_debug("checking node: %d", node->node_type);
                if (node->node_type == AST_NODE_CONTENT) {
                    node = ((AstListNode*)node)->item;  // drill down
                    continue;
                }
                else if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC) {
                    AstFuncNode *func_node = (AstFuncNode*)node;
                    if (((TypeFunc*)func_node->type)->is_public) {
                        // get func addr
                        StrBuf *func_name = strbuf_new();
                        write_fn_name(func_name, func_node, NULL);
                        log_debug("loading fn addr: %s from script: %s", func_name->str, import->script->reference);
                        void* fn_ptr = find_func(import->script->jit_context, func_name->str);
                        log_debug("got imported fn: %s, func_ptr: %p", func_name->str, fn_ptr);
                        strbuf_free(func_name);
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
    RETURN:
    log_leave();
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

    // print the syntax tree as an s-expr
    print_ts_root(tp->source, tp->syntax_tree);

    // check if the syntax tree is valid
    TSNode root_node = ts_tree_root_node(tp->syntax_tree);
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
    
    // Initialize Input base class (Script extends Input)
    Input* input_base = Input::create(pool_create(), nullptr);
    if (!input_base) {
        log_error("Error: Failed to initialize Input base");
        return;
    }
    
    // Copy Input fields to Script (Script extends Input)
    tp->pool = input_base->pool;
    tp->arena = input_base->arena;
    tp->name_pool = input_base->name_pool;
    tp->type_list = input_base->type_list;
    tp->url = input_base->url;
    tp->path = input_base->path;
    tp->root = input_base->root;
    
    // Initialize Script-specific fields
    tp->const_list = arraylist_new(16);

    if (strcmp(ts_node_type(root_node), "document") != 0) {
        log_error("Error: The tree has no valid root node.");
        return;
    }
    // build the AST
    tp->ast_root = build_script(tp, root_node);
    get_time(&end);
    print_elapsed_time("building AST", start, end);
    
    // Check for errors during AST building
    if (tp->error_count > 0) {
        log_error("compiled '%s' with error!!", script_path);
        return;
    }
    
    // print the AST for debugging
    log_debug("AST: %s ---------", tp->reference);
    print_ast_root(tp);

    // transpile the AST to C code
    log_debug("transpiling...");
    get_time(&start);
    tp->code_buf = strbuf_new_cap(1024);
    transpile_ast_root(tp, (AstScript*)tp->ast_root);
    get_time(&end);
    print_elapsed_time("transpiling", start, end);
    
    // Check for errors during transpilation
    if (tp->error_count > 0) {
        log_error("compiled '%s' with error!!", script_path);
        strbuf_free(tp->code_buf);  tp->code_buf = NULL;
        return;
    }

    // JIT compile the C code
    get_time(&start);
    tp->jit_context = jit_init();
    // compile user code to MIR
    log_debug("compiling to MIR...");
    write_text_file("_transpiled.c", tp->code_buf->str);
    char* code = tp->code_buf->str + lambda_lambda_h_len;
    // printf("code len: %d\n", (int)strlen(code));
    log_debug("transpiled code (first 500 chars):\n---------%.500s", code);
    fflush(NULL);  // force flush all open streams for large log
    jit_compile_to_mir(tp->jit_context, tp->code_buf->str, tp->code_buf->length, script_path);
    strbuf_free(tp->code_buf);  tp->code_buf = NULL;
    // generate native code and return the function
    tp->main_func = (main_func_t)jit_gen_func(tp->jit_context, "main");
    get_time(&end);
    
    // Build debug info table for stack traces (after MIR_link has assigned addresses)
    tp->debug_info = (ArrayList*)build_debug_info_table(tp->jit_context);
    
    // init lambda imports
    init_module_import(tp, (AstScript*)tp->ast_root);

    log_info("JIT compiled %s", script_path);
    log_debug("jit_context: %p, main_func: %p, debug_info: %p", tp->jit_context, tp->main_func, tp->debug_info);
    // copy value back to script
    memcpy(script, tp, sizeof(Script));
    script->main_func = tp->main_func;

    print_elapsed_time("JIT compiling", start, end);
}

Script* load_script(Runtime *runtime, const char* script_path, const char* source) {
    log_info("Loading script: %s", script_path);
    // find the script in the list of scripts
    for (int i = 0; i < runtime->scripts->length; i++) {
        Script *script = (Script*)runtime->scripts->data[i];
        if (strcmp(script->reference, script_path) == 0) {
            log_info("Script %s is already loaded.", script_path);
            return script;
        }
    }
    // script not found, create a new one
    const char* script_source =  source ? source : read_text_file(script_path);
    if (!script_source) {
        log_error("Error: Failed to read source code from %s", script_path);
        return NULL;
    }
    Script *new_script = (Script*)calloc(1, sizeof(Script));
    new_script->reference = strdup(script_path);
    new_script->source = script_source;
    log_debug("script source length: %d", (int)strlen(new_script->source));
    arraylist_append(runtime->scripts, new_script);
    new_script->index = runtime->scripts->length - 1;

    // Initialize decimal context
    new_script->decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    mpd_maxcontext(new_script->decimal_ctx);

    Transpiler transpiler;  memset(&transpiler, 0, sizeof(Transpiler));
    memcpy(&transpiler, new_script, sizeof(Script));
    transpiler.parser = runtime->parser;  transpiler.runtime = runtime;
    transpiler.error_count = 0;
    transpiler.max_errors = runtime->max_errors > 0 ? runtime->max_errors : 10;  // use runtime setting or default 10
    transpiler.errors = arraylist_new(8);  // initialize error list for structured errors
    transpile_script(&transpiler, new_script, script_path);
    
    // Print structured errors if any
    if (transpiler.errors && transpiler.errors->length > 0) {
        fprintf(stderr, "\n");
        for (int i = 0; i < transpiler.errors->length; i++) {
            LambdaError* error = (LambdaError*)transpiler.errors->data[i];
            err_print(error);
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "%d error(s) found.\n", transpiler.errors->length);
    }
    
    log_debug("loaded script main func: %s, %p", script_path, new_script->main_func);
    return new_script;
}

void runner_init(Runtime *runtime, Runner* runner) {
    memset(runner, 0, sizeof(Runner));
}

#include "../lib/url.h"
#include "validator/validator.hpp"

void runner_setup_context(Runner* runner) {
    log_debug("runner setup exec context");
    runner->context.pool = runner->script->pool;
    runner->context.type_list = runner->script->type_list;
    
    // Initialize name_pool for runtime-generated names (separate from script's name_pool)
    runner->context.name_pool = name_pool_create(runner->context.pool, nullptr);
    if (!runner->context.name_pool) {
        log_error("Failed to create runtime name_pool");
    }
    
    runner->context.type_info = type_info;
    runner->context.consts = runner->script->const_list->data;
    runner->context.num_stack = num_stack_create(16);
    runner->context.result = ItemNull;  // exec result
    runner->context.cwd = get_current_dir();  // proper URL object for current directory
    // initialize decimal context
    runner->context.decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    runner->context.context_alloc = heap_alloc;
    mpd_defaultcontext(runner->context.decimal_ctx);
    // init AST validator
    runner->context.validator = schema_validator_create(runner->context.pool);
    
    // Initialize error handling and stack trace support
    // Use debug_info from script (built after MIR compilation for address → function mapping)
    runner->context.debug_info = runner->script->debug_info;
    runner->context.current_file = runner->script->reference;  // source file for error reporting
    runner->context.last_error = NULL;
    
    input_context = context = &runner->context;
    heap_init();
    context->pool = context->heap->pool;
    frame_start();
}

void runner_cleanup(Runner* runner) {
    log_debug("runner cleanup start");
    if (!runner) {
        log_debug("runner is NULL, skipping cleanup");
        return;
    }
    
    // Only call frame_end if we set up a heap (which means runner_setup_context was called)
    if (runner->context.heap) {
        log_debug("calling frame_end");
        frame_end();
        log_debug("after frame_end");
    } else {
        log_debug("no heap, skipping frame_end");
    }
    
    // free final result
    if (runner->context.heap) {
        log_debug("cleaning up heap");
        print_heap_entries();
        log_debug("freeing final result -----------------");
        if (runner->context.result.item) free_item(runner->context.result, true);
        // check memory leaks
        check_memory_leak();
        heap_destroy();
        if (runner->context.num_stack) num_stack_destroy((num_stack_t*)runner->context.num_stack);
    }
    // free decimal context
    if (runner->context.decimal_ctx) {
        log_debug("freeing decimal context");
        free(runner->context.decimal_ctx);
        runner->context.decimal_ctx = NULL;
    }
    // free AST validator
    if (runner->context.validator) {
        log_debug("freeing validator");
        schema_validator_destroy(runner->context.validator);
        runner->context.validator = NULL;
    }
    // free last runtime error
    if (runner->context.last_error) {
        log_debug("freeing last error");
        err_free(runner->context.last_error);
        runner->context.last_error = NULL;
    }
    log_debug("runner cleanup end");
}

// Common helper function to execute a compiled script and wrap the result in an Input*
// This handles the execution, result wrapping, and cleanup logic shared between run_script and run_script_with_run_main
// Made non-static so it can be used by MIR execution path in transpile-mir.cpp
Input* execute_script_and_create_output(Runner* runner, bool run_main) {
    if (!runner->script || !runner->script->main_func) {
        log_error("Error: Failed to compile the function.");
        // Return Input with error item instead of nullptr
        Pool* error_pool = pool_create();
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            runner_cleanup(runner);
            return nullptr;
        }
        output->root = ItemError;
        runner_cleanup(runner);
        return output;
    }
    
    log_notice("Executing JIT compiled code...");
    runner_setup_context(runner);

    // set the run_main flag in the execution context
    runner->context.run_main = run_main;
    log_debug("Set context run_main = %s", run_main ? "true" : "false");

    log_debug("exec main func");
    Item result = context->result = runner->script->main_func(context);
    log_debug("after main func, result type_id=%d", get_type_id(result));

    // Preserve runtime error before runner goes out of scope
    // context points to runner's stack-allocated EvalContext, so we need to copy the error
    if (context && context->last_error) {
        clear_persistent_last_error();  // free any previous error
        persistent_last_error = context->last_error;
        context->last_error = NULL;  // transfer ownership
    }

    // Create output Input with its own pool (independent from Script's pool)
    // This allows safe cleanup of the execution context and heap
    log_debug("Creating output Input with independent pool");
    Pool* output_pool = pool_create();
    Input* output = Input::create(output_pool, nullptr);
    if (!output) {
        log_error("Failed to create output Input");
        if (output_pool) pool_destroy(output_pool);
        runner_cleanup(runner);
        return nullptr;
    }
    
    // Use MarkBuilder to deep copy result to output's arena
    // This ensures all data is copied to the output's memory space
    log_debug("Deep copying result using MarkBuilder, result.item=%016lx", result.item);
    MarkBuilder builder(output);
    output->root = builder.deep_copy(result);
    log_debug("Deep copy completed, root type_id: %d", get_type_id(output->root));
    
    // Now we can safely clean up the execution heap since output has its own copy
    log_debug("Cleaning up execution context");
    // runner_cleanup(runner);
    
    log_debug("Script execution completed, returning output Input");
    return output;
}

Input* run_script(Runtime *runtime, const char* source, char* script_path, bool transpile_only) {
    Runner runner;
    runner_init(runtime, &runner);
    runner.script = load_script(runtime, script_path, source);
    if (transpile_only) {
        log_info("Transpiled script %s only, not executing.", script_path);
        // Return Input with null item for transpile-only mode
        Pool* null_pool = pool_create();
        Input* output = Input::create(null_pool, nullptr);
        if (!output) {
            log_error("Failed to create transpile output Input");
            if (null_pool) pool_destroy(null_pool);
            return nullptr;
        }
        output->root = ItemNull;
        return output;
    }
    
    // Use common execution function with run_main=false
    return execute_script_and_create_output(&runner, false);
}

Input* run_script_at(Runtime *runtime, char* script_path, bool transpile_only) {
    return run_script(runtime, NULL, script_path, transpile_only);
}

// Extended function that supports setting run_main context and returns Input*
Input* run_script_with_run_main(Runtime *runtime, char* script_path, bool transpile_only, bool run_main) {
    Runner runner;
    runner_init(runtime, &runner);
    runner.script = load_script(runtime, script_path, NULL);
    
    if (transpile_only) {
        log_info("Transpiled script %s only, not executing.", script_path);
        // Return Input with null item for transpile-only mode
        Pool* null_pool = pool_create();
        Input* output = Input::create(null_pool, nullptr);
        if (!output) {
            log_error("Failed to create transpile output Input");
            if (null_pool) pool_destroy(null_pool);
            return nullptr;
        }
        output->root = ItemNull;
        return output;
    }
    
    // Use common execution function with specified run_main flag
    return execute_script_and_create_output(&runner, run_main);
}

void runtime_init(Runtime* runtime) {
    memset(runtime, 0, sizeof(Runtime));
    runtime->parser = lambda_parser();
    runtime->scripts = arraylist_new(16);
    runtime->max_errors = 10;  // default error threshold
}

void runtime_cleanup(Runtime* runtime) {
    if (runtime->parser) ts_parser_delete(runtime->parser);
    if (runtime->scripts) {
        for (int i = 0; i < runtime->scripts->length; i++) {
            Script *script = (Script*)runtime->scripts->data[i];
            if (script->source) free((void*)script->source);
            if (script->syntax_tree) ts_tree_delete(script->syntax_tree);
            if (script->pool) pool_destroy(script->pool);
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
