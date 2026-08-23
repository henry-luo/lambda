#pragma once

#include "../../lib/log.h"
#include "../io/input-allocation-context.h"

#include "../lambda-data.hpp"

#include "ast.hpp"
#include "compiler_timing.hpp"

// Shared range materialization helper for runtime sequence consumers.
Item fn_chr(Item codepoint);

typedef struct LambdaRegion LambdaRegion;
typedef struct LambdaRegionBlock LambdaRegionBlock;

// Runtime map admissions repeat the same candidate/contract shape pair across
// recursive calls. The entries are context-owned so a cache cannot leak a
// TypeMap relation between independent EvalContexts (D3.2.2).
#define LAMBDA_MAP_CONTRACT_CACHE_CAPACITY 16
typedef struct LambdaMapContractCacheEntry {
    const TypeMap* candidate;
    const TypeMap* expected;
    uint8_t relation;
} LambdaMapContractCacheEntry;

typedef struct Heap {
    Pool *pool;  // runtime owner group for non-GC semantic allocations
    struct gc_heap *gc;  // GC heap with object tracking (replaces entries ArrayList)
    uint64_t result_root;  // stable GC root slot for the current script result
    // Per-runtime caches for compiler-proven temporary-object regions.  These
    // blocks never enter gc->all_objects and are recycled only after a region
    // ends, so an ordinary heap object can never retain a region pointer.
    LambdaRegion* region_free;
    LambdaRegionBlock* region_free_blocks;
    LambdaMapContractCacheEntry map_contract_cache[LAMBDA_MAP_CONTRACT_CACHE_CAPACITY];
    uint32_t map_contract_cache_next;
} Heap;

void heap_init();
// Emergency recovery only: release the GC/pool generation without invoking
// object finalizers after a signal interrupted normal runtime execution.
void heap_discard_unfinalized();
extern "C" void heap_finalize_gc_objects(struct gc_heap* gc);
void* heap_alloc(int size, TypeId type_id);
extern "C" void* heap_calloc(size_t size, TypeId type_id);  // callable from C code (path.c)
extern "C" String* heap_strcpy(const char* src, int64_t len);  // callable from C code (path.c)
extern "C" void heap_gc_collect(void);                // trigger GC collection from runtime
extern "C" void heap_register_gc_root(uint64_t* slot);   // register BSS global as GC root
extern "C" void heap_unregister_gc_root(uint64_t* slot);  // unregister BSS global
extern "C" void heap_register_gc_root_range(uint64_t* base, int count);  // register env array as GC roots
extern "C" void heap_unregister_gc_root_range(uint64_t* base);  // unregister env array root range by base
LambdaRegion* lambda_region_begin(void);
void lambda_region_end(LambdaRegion* region);
void* lambda_region_calloc(LambdaRegion* region, size_t size, TypeId type_id);
String* heap_create_name(const char* name, size_t len);
String* heap_create_name(const char* name);
Symbol* heap_create_symbol(const char* symbol, size_t len);
Symbol* heap_create_symbol(const char* symbol);
void heap_destroy();
void free_item(Item item, bool clear_entry);
void expand_list(List *list, Arena* arena = nullptr);

extern "C" {
#ifndef WASM_BUILD
#include <mir.h>
#include <mir-gen.h>
#else
#include "../../wasm-deps/include/mir.h"
#include "../../wasm-deps/include/mir-gen.h"
#endif
}

typedef struct Runner {
    Runtime* runtime;    // back-pointer to owning Runtime (for heap reuse)
    Script* script;
    EvalContext* context;  // runtime-owned execution context
} Runner;

struct ModuleRegistry;

struct Runtime {
    ArrayList* scripts;  // list of (loaded) scripts
    uint32_t next_module_state_id;  // allocator shared by every language's sealed modules
    struct hashmap* script_index;  // canonical script path -> Script*
    ModuleRegistry* module_registry; // runtime-owned cross-language module definitions
    TSParser* parser;
    char* current_dir;
    int max_errors;      // error threshold for type checking (default: 10, 0 = unlimited)
    bool static_warning; // --static-warning: report semantic type errors as warnings and keep compiling (Lambda relaxed mode)
    unsigned int optimize_level;  // MIR optimization level (0-3, default: 2)
    bool dry_run;        // dry-run mode: IO functions return fabricated results instead of real IO
    void* dom_doc;       // DomDocument* for JS DOM API (NULL when no document loaded)
    void* dom_ui_context; // UiContext* borrowed by the document execution realm (NULL outside DOM sessions)
    const char* import_base_dir; // override import base directory for main script (NULL = use script's directory)
    bool use_mir_direct; // all executable Lambda paths use MIR Direct

    // Retained execution state (persistent across script evaluations).
    // The GC heap and name_pool are created on first evaluation
    // and reused for subsequent evaluations / event handler invocations.
    // Destroyed by runtime_cleanup().
    Heap* heap;
    NamePool* name_pool;
    ArrayList* type_list;

    // Phase 5: unified DOM — when ui_mode is true, elmt()/list_push()/elmt_fill()
    // allocate fat DomElement/DomText on result_arena instead of the GC heap.
    bool ui_mode;
    Arena* result_arena;

    // level 1 MIR cache counters. Phase 1 records index hits/misses while module
    // retention stays disabled until cone-based initialization lands.
    bool mir_cache_disabled;
    int mir_cache_hits;
    int mir_cache_misses;
    int mir_cache_compiles;
    int mir_cache_invalidations;
    LambdaScheduler* scheduler;
    // Canonical runtime-owned execution state.  Runners, callbacks, and guest
    // bridges bind this stable object through TLS; none embeds it on a stack.
    EvalContext* eval_context;
    EvalContext* js_bootstrap_context;
    bool js_runtime_used;
    bool no_task_drain;
};

// Complete a guest MIR activation that either reused its caller's heap or
// created a standalone heap which must remain owned until runtime_cleanup().
void mir_guest_finish_context(Runtime* runtime, bool reusing_context);

// global dry-run flag (set from Runtime, accessible from C code via lambda.h)
#include "runtime-state.h"

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
void print_ts_root(const char *source, TSTree* syntax_tree);
void print_tree(TSNode node, int depth);

void write_node_source(Transpiler* tp, TSNode node);
NameEntry *lookup_name(Transpiler* tp, StrView var_name);
void write_fn_name(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import);
void write_fn_name_ex(StrBuf *strbuf, AstFuncNode* fn_node, AstImportNode* import, const char* suffix);
void write_var_name(StrBuf *strbuf, AstNamedNode *asn_node, AstImportNode* import);
bool needs_fn_call_wrapper(AstFuncNode* fn_node);

// Shared AST/MIR helpers.
bool has_typed_params(AstFuncNode* fn_node);
ShapeEntry* find_shape_field_by_name(TypeMap* map_type, const char* name, int name_len);
// Object literals carry only the supplied named fields.  Construction must
// align each value with its declared ShapeEntry so omitted fields can run their
// declared defaults instead of being shifted into a later storage lane.
AstNode* ast_object_literal_value_for_shape(const AstObjectLiteralNode* literal,
    const ShapeEntry* shape);
// The unkeyed item in an object literal is the `*:source` spread. It must be
// evaluated once so omitted fields inherit the source value before typed-field
// storage coercion runs.
AstNode* ast_object_literal_spread_value(const AstObjectLiteralNode* literal);
bool has_fixed_shape(TypeMap* map_type);
bool is_direct_access_type(TypeId type_id);
bool static_literal_item_from_type(Type* type, Item* out);

// Shape/type-graph helpers shared by the Lambda and JS AST builders.
// `is_global_simple_type` answers whether a Type* is one of the compact global
// singletons (which carry only the Type prefix and must never be read as a
// TypeMap/TypeArray); `unwrap_simple_type_type` peels compiler-built TypeType
// wrappers off a recorded field type.
bool is_global_simple_type(const Type* type);
Type* unwrap_simple_type_type(Type* type);

// ANY-census recorders [Type_Infer TI3]. Every `any` fallback in the builders
// goes through one of these so the reason is counted; never bare-assign
// `&TYPE_ANY` to an expression node's type.
Type* set_type_any(Transpiler* tp, AnyReason reason);
Type* set_lit_type_any(Transpiler* tp, AnyReason reason);
TypeId census_any_type_id(Transpiler* tp, AnyReason reason);
TypeId resolve_field_type_id(ShapeEntry* field, bool unwrap_type_type);
int detect_ndim_literal(AstNode* node, int64_t* shape_out, int max_ndim,
                        ArrayNumElemType* elem_type_out, bool disqualify_assign = false);

extern"C" {
MIR_context_t jit_init(unsigned int optimize_level);
void* jit_gen_func(MIR_context_t ctx, const char *func_name);
size_t jit_release_generated_ir(MIR_context_t ctx);
MIR_item_t find_import(MIR_context_t ctx, const char *mod_name);
void* find_func(MIR_context_t ctx, const char *fn_name);
void* find_func_prefix(MIR_context_t ctx, const char *prefix);
void* find_data(MIR_context_t ctx, const char *data_name);
void jit_cleanup(MIR_context_t ctx);
void jit_cleanup_mode(MIR_context_t ctx, int generator_initialized);
void register_dynamic_import(const char *name, void *addr);
void clear_dynamic_imports(void);
}

// MIR transpiler functions
// compile_only stops after compilation (and therefore after MIR emission),
// skipping execution; it backs --transpile-only for the MIR Direct pipeline.
Input* run_script_mir(Runtime *runtime, const char* source, char* script_path, bool run_main = false,
                      bool compile_only = false);
void compile_script_as_mir_direct(Transpiler* tp, Script* script, const char* script_path,
                                   double* out_jit_init_ms = nullptr,
                                   double* out_transpile_ms = nullptr,
                                   double* out_mir_gen_ms = nullptr,
                                   uint64_t* out_mir_module_count = nullptr,
                                   uint64_t* out_mir_function_count = nullptr,
                                   uint64_t* out_mir_instruction_count = nullptr);

// P2: compile one T0-supported definition into a Script-owned MIR satellite.
// Its module bindings are read from the existing T0 slab; the resulting
// address has the normal generated boxed ABI.
bool compile_ast_function_satellite(Runtime* runtime, Script* script,
                                    const AstFuncNode* fn, void** out_boxed_entry);

// Transfers the Script-sized prefix of a finished Transpiler onto its Script.
// Shared by the MIR Direct handoff and the T0 plan-only load path.
void script_adopt_transpiler(Script* script, Transpiler* tp);

Script* load_script(Runtime *runtime, const char* script_path, const char* source, bool is_import = false);
Script* load_script_mir_direct(Runtime *runtime, const char* script_path,
                               const char* source, bool is_import = false);
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void preserve_context_last_error(Item result);
Input* execute_script_and_create_output(Runner* runner, bool run_main);
void runtime_init(Runtime* runtime);
void runtime_cleanup(Runtime* runtime);
void runtime_reset_heap(Runtime* runtime);  // reset heap between independent evaluations
EvalContext* runtime_get_eval_context(Runtime* runtime);
void runtime_register_script(Runtime* runtime, Script* script);
void runtime_free_script(Runtime* runtime, Script* script, bool remove_index);
void runtime_teardown_batch_scripts(Runtime* runtime);
void runtime_log_mir_cache_summary(Runtime* runtime);
void path_reset(void);  // reset path scheme roots (must call after runtime_reset_heap in batch)

// JavaScript transpiler integration
Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename,
                          uint64_t* result_home);
Item transpile_js_to_mir_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                             const char* filename, uint64_t* result_home);

// Batch mode preamble support (two-module MIR split)
struct JsPreambleState;
Item transpile_js_to_mir_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                   JsPreambleState* out_state, uint64_t* result_home);
Item transpile_js_to_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                      const char* filename, JsPreambleState* out_state,
                                      uint64_t* result_home);
Item transpile_js_to_mir_with_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                        const JsPreambleState* preamble, uint64_t* result_home);
Item transpile_js_to_mir_with_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                           const char* filename, const JsPreambleState* preamble,
                                           uint64_t* result_home);
void preamble_state_destroy(JsPreambleState* state);
void jm_cleanup_deferred_mir();

// Compile a JS file as a module and return the namespace object.
// Used for cross-language imports (Lambda → JS).
Item load_js_module(Runtime* runtime, const char* js_path);
