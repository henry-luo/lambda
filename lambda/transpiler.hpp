#pragma once

#undef LAMBDA_STATIC
#include "lambda-data.hpp"

#include "ast.hpp"

typedef struct Heap {
    VariableMemPool *pool;  // memory pool for the heap
    // HeapEntry *first, *last;  // first and last heap entry
    ArrayList *entries;  // list of allocation entries
} Heap;

void heap_init();
void* heap_alloc(size_t size, TypeId type_id);
void* heap_calloc(size_t size, TypeId type_id);
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
    Context context;  // execution context
} Runner;

struct Runtime {
    ArrayList* scripts;  // list of (loaded) scripts
    TSParser* parser;
    char* current_dir;
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
void print_ts_node(const char *source, TSNode node, uint32_t indent);
void find_errors(TSNode node);
void write_node_source(Transpiler* tp, TSNode node);
void write_type(StrBuf* code_buf, Type *type);
NameEntry *lookup_name(Transpiler* tp, StrView var_name);
void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import);
void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import);

extern"C" {
MIR_context_t jit_init();
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
MIR_item_t find_import(MIR_context_t ctx, const char *mod_name);
void* find_func(MIR_context_t ctx, const char *fn_name);
void* find_data(MIR_context_t ctx, const char *data_name);
void jit_cleanup(MIR_context_t ctx);
}

// MIR transpiler functions
Item run_script_mir(Runtime *runtime, const char* source, char* script_path);

Script* load_script(Runtime *runtime, const char* script_path, const char* source);
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void runner_cleanup(Runner* runner);
Item run_script(Runtime *runtime, const char* source, char* script_path, bool transpile_only = false);
Item run_script_at(Runtime *runtime, char* script_path, bool transpile_only = false);

void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);
