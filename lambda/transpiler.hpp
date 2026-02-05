#pragma once

#include "../lib/log.h"

#undef LAMBDA_STATIC
#include "lambda-data.hpp"

#include "ast.hpp"

typedef struct Heap {
    Pool *pool;  // memory pool for the heap
    // HeapEntry *first, *last;  // first and last heap entry
    ArrayList *entries;  // list of allocation entries
} Heap;

void heap_init();
void* heap_alloc(int size, TypeId type_id);
extern "C" void* heap_calloc(size_t size, TypeId type_id);  // callable from C code (path.c)
extern "C" String* heap_strcpy(char* src, int len);  // callable from C code (path.c)
String* heap_create_name(const char* name, size_t len);
String* heap_create_name(const char* name);
String* heap_create_symbol(const char* symbol, size_t len);
String* heap_create_symbol(const char* symbol);
void heap_destroy();
void frame_start();
void frame_end();
void free_item(Item item, bool clear_entry);
void expand_list(List *list);

extern "C" {
#ifndef WASM_BUILD
#include <mir.h>
#include <mir-gen.h>
#include <c2mir.h>
#else
#include "../wasm-deps/include/mir.h"
#include "../wasm-deps/include/mir-gen.h"
#include "../wasm-deps/include/c2mir.h"
#endif
}

typedef struct Runner {
    Script* script;
    EvalContext context;  // execution context
} Runner;

struct Runtime {
    ArrayList* scripts;  // list of (loaded) scripts
    TSParser* parser;
    char* current_dir;
    int max_errors;      // error threshold for type checking (default: 10, 0 = unlimited)
    unsigned int optimize_level;  // MIR JIT optimization level (0-2, default: 2)
};

#define ts_node_source(transpiler, node)  {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

void* alloc_const(Transpiler* tp, size_t size);
AstNode* build_map(Transpiler* tp, TSNode map_node);
AstNode* build_elmt(Transpiler* tp, TSNode element_node);
AstNode* build_if_stam(Transpiler* tp, TSNode if_node);
AstNode* build_for_stam(Transpiler* tp, TSNode for_node);
AstNode* build_expr(Transpiler* tp, TSNode expr_node);
AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global);
AstNode* build_script(Transpiler* tp, TSNode script_node);
void print_ast_root(Script *script);
void print_ts_root(const char *source, TSTree* syntax_tree);
void print_tree(TSNode node, int depth);
void find_errors(TSNode node);
void write_node_source(Transpiler* tp, TSNode node);
void write_type(StrBuf* code_buf, Type *type);
NameEntry *lookup_name(Transpiler* tp, StrView var_name);
void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import);
void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import);

extern"C" {
MIR_context_t jit_init(unsigned int optimize_level);
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
MIR_item_t find_import(MIR_context_t ctx, const char *mod_name);
void* find_func(MIR_context_t ctx, const char *fn_name);
void* find_data(MIR_context_t ctx, const char *data_name);
void jit_cleanup(MIR_context_t ctx);
}

// MIR transpiler functions
Input* run_script_mir(Runtime *runtime, const char* source, char* script_path, bool run_main = false);

Script* load_script(Runtime *runtime, const char* script_path, const char* source);
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void runner_cleanup(Runner* runner);
Input* execute_script_and_create_output(Runner* runner, bool run_main);
Input* run_script(Runtime *runtime, const char* source, char* script_path, bool transpile_only = false);
Input* run_script_at(Runtime *runtime, char* script_path, bool transpile_only = false);
Input* run_script_with_run_main(Runtime *runtime, char* script_path, bool transpile_only, bool run_main);

void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);

// JavaScript transpiler integration
Item transpile_js_to_c(Runtime* runtime, const char* js_source, const char* filename);
