#pragma once

#include "../lib/log.h"

#undef LAMBDA_STATIC
#include "lambda-data.hpp"

#include "ast.hpp"

typedef struct Heap {
    Pool *pool;  // memory pool alias (points to gc->pool for compatibility)
    struct gc_heap *gc;  // GC heap with object tracking (replaces entries ArrayList)
} Heap;

void heap_init();
void* heap_alloc(int size, TypeId type_id);
extern "C" void* heap_calloc(size_t size, TypeId type_id);  // callable from C code (path.c)
extern "C" String* heap_strcpy(char* src, int64_t len);  // callable from C code (path.c)
extern "C" void heap_gc_collect(void);                // trigger GC collection from runtime
extern "C" void heap_register_gc_root(uint64_t* slot);   // register BSS global as GC root
extern "C" void heap_unregister_gc_root(uint64_t* slot);  // unregister BSS global
extern "C" void heap_register_gc_root_range(uint64_t* base, int count);  // register env array as GC roots
String* heap_create_name(const char* name, size_t len);
String* heap_create_name(const char* name);
Symbol* heap_create_symbol(const char* symbol, size_t len);
Symbol* heap_create_symbol(const char* symbol);
void heap_destroy();
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
    Runtime* runtime;    // back-pointer to owning Runtime (for heap reuse)
    Script* script;
    EvalContext context;  // execution context
} Runner;

struct Runtime {
    ArrayList* scripts;  // list of (loaded) scripts
    TSParser* parser;
    char* current_dir;
    int max_errors;      // error threshold for type checking (default: 10, 0 = unlimited)
    unsigned int optimize_level;  // MIR JIT optimization level (0-2, default: 2)
    const char* transpile_dir;   // directory for transpiled C output files (NULL = current dir)
    bool dry_run;        // dry-run mode: IO functions return fabricated results instead of real IO
    void* dom_doc;       // DomDocument* for JS DOM API (NULL when no document loaded)
    const char* import_base_dir; // override import base directory for main script (NULL = use script's directory)
    bool use_mir_direct; // if true, all modules (main + imports) compiled via MIR Direct instead of C2MIR

    // Retained execution state (persistent across script evaluations).
    // The GC heap, nursery, and name_pool are created on first evaluation
    // and reused for subsequent evaluations / event handler invocations.
    // Destroyed by runtime_cleanup().
    Heap* heap;
    gc_nursery_t* nursery;
    NamePool* name_pool;
};

// global dry-run flag (set from Runtime, accessible from C code via lambda.h)
extern bool g_dry_run;

// Lambda home: directory containing runtime assets (package/, input/).
// Dev default: "./lambda"  Release: "./lmd"  Override: LAMBDA_HOME env var.
extern const char* g_lambda_home;
void lambda_home_init(void);    // call once at startup (reads LAMBDA_HOME env var)
char* lambda_home_path(const char* rel); // returns malloc'd "<g_lambda_home>/<rel>"; caller frees

#define ts_node_source(transpiler, node)  {.str = (transpiler)->source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

void* alloc_const(Transpiler* tp, size_t size);
AstNode* build_map(Transpiler* tp, TSNode map_node);
AstNode* build_elmt(Transpiler* tp, TSNode element_node);
AstNode* build_for_stam(Transpiler* tp, TSNode for_node);
AstNode* build_expr(Transpiler* tp, TSNode expr_node);
AstNode* build_content(Transpiler* tp, TSNode list_node, bool flattern, bool is_global);
AstNode* build_script(Transpiler* tp, TSNode script_node);
void print_ast_root(Script *script);
void print_ts_root(const char *source, TSTree* syntax_tree);
void print_tree(TSNode node, int depth);

void write_node_source(Transpiler* tp, TSNode node);
void write_type(StrBuf* code_buf, Type *type);
NameEntry *lookup_name(Transpiler* tp, StrView var_name);
void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import);
void write_fn_name_ex(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import, const char* suffix);
void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import);
bool needs_fn_call_wrapper(AstFuncNode* fn_node);

// Transpiler shared functions (used by transpile.cpp and transpile-call.cpp)
void transpile_expr(Transpiler* tp, AstNode *expr_node);
void transpile_box_item(Transpiler* tp, AstNode *node);
void transpile_call_expr(Transpiler* tp, AstCallNode *call_node);
bool callee_returns_retitem(AstCallNode* call_node);
bool current_func_returns_retitem(Transpiler* tp);
bool emit_zero_value(Transpiler* tp, TypeId tid);
bool value_emits_native_type(Transpiler* tp, AstNode* value, TypeId target_type);
const char* get_container_unbox_fn(TypeId type_id);
bool can_use_unboxed_call(AstCallNode* call_node, AstFuncNode* fn_node);
bool has_typed_params(AstFuncNode* fn_node);
Type* resolve_native_ret_type(AstFuncNode* fn_node);

extern"C" {
MIR_context_t jit_init(unsigned int optimize_level);
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name);
void* jit_gen_func(MIR_context_t ctx, char *func_name);
MIR_item_t find_import(MIR_context_t ctx, const char *mod_name);
void* find_func(MIR_context_t ctx, const char *fn_name);
void* find_data(MIR_context_t ctx, const char *data_name);
void jit_cleanup(MIR_context_t ctx);
void register_dynamic_import(const char *name, void *addr);
void clear_dynamic_imports(void);
}

// MIR transpiler functions
Input* run_script_mir(Runtime *runtime, const char* source, char* script_path, bool run_main = false);
void compile_script_as_mir_direct(Transpiler* tp, Script* script, const char* script_path,
                                   double* out_jit_init_ms = nullptr,
                                   double* out_transpile_ms = nullptr,
                                   double* out_mir_gen_ms = nullptr);

Script* load_script(Runtime *runtime, const char* script_path, const char* source, bool is_import = false);
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
Input* execute_script_and_create_output(Runner* runner, bool run_main);
Input* run_script(Runtime *runtime, const char* source, char* script_path, bool transpile_only = false);
Input* run_script_at(Runtime *runtime, char* script_path, bool transpile_only = false);
Input* run_script_with_run_main(Runtime *runtime, char* script_path, bool transpile_only, bool run_main);

void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);
void runtime_reset_heap(Runtime* runtime);  // reset heap between independent evaluations

// JavaScript transpiler integration
Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename);

// Compile a JS file as a module and return the namespace object.
// Used for cross-language imports (Lambda → JS).
Item load_js_module(Runtime* runtime, const char* js_path);

// Compile a Python file as a module and return the namespace object.
// Used for cross-language imports (Lambda/JS/Python → Python).
Item load_py_module(Runtime* runtime, const char* py_path);
