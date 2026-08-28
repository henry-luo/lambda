#pragma once

#include "js_ast.hpp"
#include "../runtime/transpiler.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct JsScript JsScript;
typedef struct JsTranspiler JsTranspiler;
typedef NameScope JsScope;
struct hashmap;

// Import/export plans retain only AST/name-pool data. The actual namespace
// Items stay rooted by the single runtime module registry.
typedef struct JsInterpImportBinding {
    String* local_name;
    String* source;
    String* export_name;
    bool namespace_import;
    struct JsInterpImportBinding* next;
} JsInterpImportBinding;

typedef struct JsInterpExportBinding {
    String* local_name;
    String* export_name;
    // Non-null for `export { local as exported } from source` and export-star
    // entries; source writes propagate through the same module registry.
    String* source;
    // `export * as ns from source` exposes source's namespace object itself,
    // rather than one of its named live bindings.
    bool namespace_export;
    // Star entries are synthesized only after the target namespace has been
    // linked. This distinguishes them from explicit named re-exports.
    bool star_export;
    struct JsInterpExportBinding* next;
} JsInterpExportBinding;

// JavaScript variable declaration types
typedef enum JsVarKind {
    JS_VAR_VAR,     // var - function scoped
    JS_VAR_LET,     // let - block scoped
    JS_VAR_CONST    // const - block scoped, immutable
} JsVarKind;

// JavaScript scope types
typedef enum JsScopeType {
    JS_SCOPE_GLOBAL,
    JS_SCOPE_FUNCTION,
    JS_SCOPE_BLOCK,
    JS_SCOPE_MODULE
} JsScopeType;

// JsScript retains the JavaScript-specific semantic facts over the common
// Script owner. The base owns source, Input allocation, profile, AST/index,
// module identity, interpreter plan/slab, imports, debug state, and MIR
// artifacts; do not mirror those fields here.
struct JsScript : Script {
    size_t source_length;           // source byte count; adopted Script owns source bytes
    JsScope* global_scope;          // JS global/module lexical scope root
    bool strict_mode;               // JS script/function strictness default
    // Module top levels use a private module slab. CJS remains non-strict,
    // while ES modules set both this bit and strict_mode.
    bool is_module;
    bool is_es_module;
    // ES declaration instantiation happens before recursive dependency
    // evaluation so circular imports observe hoisted function exports.
    bool es_module_scope_initialized;
    bool strict_js;                 // true = reject TS syntax (pure JS mode)
    bool emit_runtime_checks;       // TS development-mode assertion emission
    struct hashmap* type_registry; // TS name → Type* facts for this JS/TS unit
    JsInterpImportBinding* interp_imports;
    JsInterpExportBinding* interp_exports;
};

// JsTranspiler is an ephemeral builder extending the retained JsScript prefix.
// Its tail contains only parser, diagnostics, and current-build state; adoption
// moves the prefix into a runtime-owned JsScript before destroying this tail.
struct JsTranspiler : JsScript {
    char* normalized_source;        // owned parse buffer when normalization applies

    // Current-build state
    int function_counter;           // Counter for anonymous functions
    int temp_var_counter;           // Counter for temporary variables
    int label_counter;              // Counter for labels
    bool in_expression;             // True when transpiling inside an expression (for function expressions)
    bool in_async_function;         // True while building an async function body/parameters
    
    // ANY-census [Type_Infer TI3]: per-reason counts of expressions whose
    // static type fell back to `any`. Diagnostic only — shares the Lambda
    // reason catalog so both lanes report against one vocabulary.
    int any_census[ANY_REASON_COUNT];

    // Error handling
    bool has_errors;                // Error flag
    StrBuf* error_buf;              // Error messages
    bool parse_error_valid;
    int64_t parse_error_row;
    int64_t parse_error_col;
    char parse_error_message[128];
    
    // Tree-sitter integration
    TSParser* parser;               // Tree-sitter parser
    TSTree* tree;                   // Parse tree

    // Runtime integration
    Runtime* runtime;               // builder's borrowed runtime
};

// JavaScript type mapping functions
Type* js_type_to_lambda_type(JsTranspiler* tp, JsAstNode* node);
TypeId infer_js_expression_type(JsTranspiler* tp, JsAstNode* expr);
bool is_js_truthy_type(TypeId type_id);

// Scope management functions
JsScope* js_scope_create(JsTranspiler* tp, JsScopeType scope_type, JsScope* parent);
void js_scope_push(JsTranspiler* tp, JsScope* scope);
void js_scope_pop(JsTranspiler* tp);
NameEntry* js_scope_lookup(JsTranspiler* tp, String* name);
NameEntry* js_scope_lookup_current(JsTranspiler* tp, String* name);
NameEntry* js_scope_define(JsTranspiler* tp, String* name, JsAstNode* node, JsVarKind kind);

// AST building functions (build_js_ast.cpp)
JsAstNode* build_js_ast(JsTranspiler* tp, TSNode root);
void js_report_any_census(JsTranspiler* tp);
typedef struct JsAstIndexPassContext {
    JsTranspiler* transpiler;
    JsAstNode* root;
    int validation_errors;
} JsAstIndexPassContext;
int js_check_early_errors(JsTranspiler* tp, JsAstNode* ast);
static inline int js_validate_compiler_pass(void* opaque) {
    JsAstIndexPassContext* pass = (JsAstIndexPassContext*)opaque;
    if (!pass || !pass->transpiler || !pass->root) return 0;
    pass->validation_errors = js_check_early_errors(pass->transpiler, pass->root);
    return pass->validation_errors == 0;
}
static inline int js_index_compiler_pass(void* opaque) {
    JsAstIndexPassContext* pass = (JsAstIndexPassContext*)opaque;
    return pass && pass->transpiler && pass->root &&
        ast_index_build_profile(&pass->transpiler->ast_index,
            (AstNode*)pass->root, pass->transpiler->profile);
}
static inline JsAstNode* build_js_ast_indexed(JsTranspiler* tp, TSNode root) {
    JsAstNode* ast = build_js_ast(tp, root);
    if (!ast) return NULL;
    // The shared Script owner is the AST lifetime authority after adoption.
    // Publish the root before post-build passes attach indexed facts to it.
    tp->ast_root = (AstNode*)ast;
    js_report_any_census(tp);
    JsAstIndexPassContext pass_context = {tp, ast, -1};
    CompilerPassManager pass_manager;
    compiler_pass_manager_init(&pass_manager, COMPILER_FACT_AST | COMPILER_FACT_BOUND);
    CompilerPassSpec validate_pass = {"validate", COMPILER_FACT_AST |
        COMPILER_FACT_BOUND, COMPILER_FACT_VALIDATED, js_validate_compiler_pass};
    CompilerPassSpec index_pass = {"index",
        COMPILER_FACT_AST | COMPILER_FACT_BOUND | COMPILER_FACT_VALIDATED,
        COMPILER_FACT_INDEXED, js_index_compiler_pass};
    if (!compiler_pass_manager_add(&pass_manager, &validate_pass) ||
            !compiler_pass_manager_add(&pass_manager, &index_pass) ||
            !compiler_pass_manager_run(&pass_manager, &pass_context)) {
        if (pass_context.validation_errors > 0) return ast;
        log_error("js-ast: failed to run indexed AST pass");
        return NULL;
    }
    return ast;
}
JsAstNode* build_js_program(JsTranspiler* tp, TSNode program_node);
JsAstNode* build_js_statement(JsTranspiler* tp, TSNode stmt_node);
JsAstNode* build_js_expression(JsTranspiler* tp, TSNode expr_node);
JsAstNode* build_js_function(JsTranspiler* tp, TSNode func_node);
JsAstNode* build_js_variable_declaration(JsTranspiler* tp, TSNode var_node);
JsAstNode* build_js_binary_expression(JsTranspiler* tp, TSNode binary_node);
JsAstNode* build_js_unary_expression(JsTranspiler* tp, TSNode unary_node);
JsAstNode* build_js_call_expression(JsTranspiler* tp, TSNode call_node);
JsAstNode* build_js_member_expression(JsTranspiler* tp, TSNode member_node);
JsAstNode* build_js_array_expression(JsTranspiler* tp, TSNode array_node);
JsAstNode* build_js_object_expression(JsTranspiler* tp, TSNode object_node);
JsAstNode* build_js_identifier(JsTranspiler* tp, TSNode id_node);
JsAstNode* build_js_literal(JsTranspiler* tp, TSNode literal_node);
JsAstNode* build_js_block_statement(JsTranspiler* tp, TSNode block_node, JsScopeType scope_type = JS_SCOPE_BLOCK);
JsAstNode* build_js_class_declaration(JsTranspiler* tp, TSNode class_node);
JsAstNode* build_js_class_body(JsTranspiler* tp, TSNode body_node);
JsAstNode* build_js_method_definition(JsTranspiler* tp, TSNode method_node);
JsAstNode* build_js_field_definition(JsTranspiler* tp, TSNode field_node);

// AST utility functions (build_js_ast.cpp)
JsAstNode* alloc_js_ast_node(JsTranspiler* tp, JsAstNodeType node_type, TSNode node, size_t size);
JsOperator js_operator_from_string(const char* op_str, size_t len);

// Error handling functions
void js_error(JsTranspiler* tp, SourceSpan span, const char* format, ...);

// Transpiler lifecycle functions
JsTranspiler* js_transpiler_create(Runtime* runtime);
void js_transpiler_destroy(JsTranspiler* tp);
bool js_transpiler_parse(JsTranspiler* tp, const char* source, size_t length);
int js_transpiler_parse_error_get(const JsTranspiler* tp, int64_t* out_row,
                                  int64_t* out_col, char* out_message,
                                  int64_t out_message_size);
JsScript* js_script_adopt_transpiler(JsTranspiler* tp, Runtime* runtime,
                                     const char* reference);
static inline JsScript* js_script_from_script(Script* script) {
    return script && script->profile == &js_profile ? (JsScript*)script : NULL;
}
#ifdef __cplusplus
}
#endif

// Direct MIR transpilation entry point
Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename,
                          uint64_t* result_home);
Item transpile_js_to_mir_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                             const char* filename, uint64_t* result_home);
// Compile preprocessed TypeScript with the JS parser while retaining the TS
// language-profile intrinsics during MIR lowering.
Item transpile_js_typescript_to_mir_len(Runtime* runtime, const char* js_source,
                                        size_t js_source_len, const char* filename,
                                        uint64_t* result_home);

// Batch mode preamble support (two-module MIR split)
struct JsModuleConstEntry;  // defined in transpile_js_mir.cpp

struct JsPreambleState {
    void* mir_ctx;              // MIR_context_t kept alive for harness function objects
    char* source_buffer;        // source bytes kept alive for retained AST/source ranges
    void* entry_func;           // compiled js_main, reusable with a fresh EvalContext
    int module_var_count;       // number of harness module vars
    uint32_t module_state_id;   // live realm slab that retains those harness vars
    JsModuleConstEntry* entries;// owned snapshot of harness module_consts
    int entry_count;
    PropertyKeySpec* module_property_specs; // sealed spelling image for MIR property names
    uint32_t module_property_count;
    uint32_t module_property_bytes_size;
    bool owns_compiled_state;   // clones share the immutable MIR context
};

enum JsMirCacheMode {
    JS_MIR_CACHE_PREAMBLE = 1,
    JS_MIR_CACHE_LIFECYCLE = 2,
    JS_MIR_CACHE_EXTERNAL_CLASSIC = 3,
    JS_MIR_CACHE_INLINE_CLASSIC = 4,
    JS_MIR_CACHE_MODULE = 5,
};

struct JsMirCache;

struct JsMirCacheStats {
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t compiles;
    uint64_t instantiations;
    uint64_t poisoned;
    size_t retained_entries;
    size_t retained_metadata_bytes;
};

JsMirCache* js_mir_cache_create(void);
void js_mir_cache_destroy(JsMirCache* cache);
const JsPreambleState* js_mir_cache_lookup(
    JsMirCache* cache, JsMirCacheMode mode,
    const char* source, size_t source_len, const char* filename,
    const JsPreambleState* preamble);
const JsPreambleState* js_mir_cache_adopt(
    JsMirCache* cache, JsMirCacheMode mode,
    const char* source, size_t source_len, const char* filename,
    const JsPreambleState* preamble, JsPreambleState* compiled_state);
void js_mir_cache_record_instantiation(JsMirCache* cache);

Item transpile_js_to_mir_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                   JsPreambleState* out_state, uint64_t* result_home);
Item transpile_js_to_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                      const char* filename, JsPreambleState* out_state,
                                      uint64_t* result_home);
Item compile_js_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                 const char* filename, JsPreambleState* out_state);
Item compile_js_mir_with_preamble_len(Runtime* runtime, const char* js_source,
                                      size_t js_source_len, const char* filename,
                                      const JsPreambleState* preamble,
                                      JsPreambleState* out_state);
Item execute_compiled_js_in_current_realm(Runtime* runtime,
                                          const JsPreambleState* base_preamble,
                                          const JsPreambleState* compiled_state,
                                          bool retain_unit_state);
Item transpile_js_to_mir_with_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                        const JsPreambleState* preamble, uint64_t* result_home);
Item transpile_js_to_mir_with_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                           const char* filename, const JsPreambleState* preamble,
                                           uint64_t* result_home);
bool preamble_state_update_from_eval_snapshot(JsPreambleState* state);
bool preamble_state_update_from_compiled(JsPreambleState* state,
                                         const JsPreambleState* compiled_state);
bool clone_js_preamble_state(const JsPreambleState* source, JsPreambleState* out_state);
Item instantiate_js_preamble(Runtime* runtime, const JsPreambleState* cached,
                             JsPreambleState* out_state);
void preamble_state_destroy(JsPreambleState* state);

// Clean up all deferred MIR contexts (call at batch end or after heap_destroy on crash)
void jm_cleanup_deferred_mir();

// Get the most recently deferred MIR context (for function pointer lookup after with_preamble compilation)
void* jm_get_last_deferred_mir_ctx();

// Transpile a pre-built JS AST to MIR (used by TS transpiler)
Item transpile_js_ast_to_mir(Runtime* runtime, JsTranspiler* tp, JsAstNode* ast,
                             const char* filename, uint64_t* result_home);

// JavaScript runtime function declarations (js_runtime.cpp)
#ifdef __cplusplus
extern "C" {
#endif

// JavaScript runtime functions
Item js_typeof(Item value);
Item js_add(Item left, Item right);
Item js_subtract(Item left, Item right);
Item js_multiply(Item left, Item right);
Item js_divide(Item left, Item right);
Item js_modulo(Item left, Item right);
Item js_power(Item left, Item right);

// Comparison operators
Item js_equal(Item left, Item right);
Item js_strict_equal(Item left, Item right);
// Tune8 §2.1: js_not_equal / js_strict_not_equal removed (see js_runtime.h).
Item js_less_than(Item left, Item right);
Item js_greater_than(Item left, Item right);
// Tune8 §2.1: js_less_equal / js_greater_equal removed (see js_runtime.h).
Item js_compare(int64_t op, Item left, Item right);

// Logical operators
Item js_logical_and(Item left, Item right);
Item js_logical_or(Item left, Item right);
Item js_logical_not(Item operand);

// Bitwise operators
Item js_bitwise_and(Item left, Item right);
Item js_bitwise_or(Item left, Item right);
Item js_bitwise_xor(Item left, Item right);
Item js_bitwise_not(Item operand);
Item js_left_shift(Item left, Item right);
Item js_right_shift(Item left, Item right);
Item js_unsigned_right_shift(Item left, Item right);

// Unary operators
Item js_unary_plus(Item operand);
Item js_unary_minus(Item operand);
Item js_increment(Item operand);
Item js_decrement(Item operand);

// Type conversion functions
Item js_to_primitive(Item value, const char* hint);
Item js_to_number(Item value);
Item js_to_string(Item value);
Item js_to_boolean(Item value);
Item js_to_object(Item value);
bool js_is_truthy(Item value);

// Object and property functions
Item js_new_object();
Item js_get_reference(Item object, Item key);
Item js_set_key_default(Item object, Item key, Item value);

// Function call functions
Item js_call_function(Item func, Item this_binding, Item* args, int arg_count);
Item js_call_function_into(Item func, Item this_binding, Item* args,
                           int arg_count, uint64_t* result_home);
Item js_call_constructor_body_into(Item func, Item this_binding, Item* args,
                                   int arg_count, Item new_target,
                                   uint64_t* result_home);
Item js_call_constructor_body_prerooted_args_into(Item func, Item this_binding,
                                                  Item* args, int arg_count,
                                                  Item new_target,
                                                  uint64_t* result_home);
Item js_construct_value(Item callee, Item* args, int arg_count, Item new_target,
                        uint64_t* result_home, bool args_prerooted);
Item js_construct_value_defer_own_fields(Item callee, Item* args, int arg_count,
                                         Item new_target);
Item js_init_class_instance_fields_after_super(Item callee, Item object);

// Array functions
Item js_elements_get(Item array, Item index);
Item js_elements_set(Item array, Item index, Item value);
int64_t js_array_length(Item array);
Item js_array_push(Item array, Item value);

// Math helpers retained for exact namespace operations.

// Prototype and inheritance
Item js_prototype_lookup(Item object, Item property);
Item js_get_prototype(Item object);
void js_set_prototype(Item object, Item prototype);

// Global object and built-ins

#ifdef __cplusplus
}
#endif
