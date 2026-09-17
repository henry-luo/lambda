#pragma once

#include "js_ast.hpp"
#include "parser/js_parser.h"
#include "../runtime/transpiler.hpp"
#include "../input/input-script-cache.h"
#include "../../lib/strbuf.h"

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
typedef enum JsInterpModuleBindingKind {
    JS_INTERP_MODULE_BINDING_IMPORT,
    JS_INTERP_MODULE_BINDING_EXPORT,
} JsInterpModuleBindingKind;

typedef struct JsInterpModuleBinding {
    String* local_name;
    String* source;
    String* export_name;
    // Direct scope attaches the source binding after parsing. The AST tier
    // uses it before ordinary environment lookup so imports remain live; it
    // is NULL for export plans.
    NameEntry* entry;
    JsInterpModuleBindingKind kind;
    // `export * as ns from source` exposes source's namespace object itself,
    // rather than one of its named live bindings.
    bool namespace_binding;
    // Star entries are synthesized only after the target namespace has been
    // linked. This distinguishes them from explicit named re-exports.
    bool star_export;
    struct JsInterpModuleBinding* next;
} JsInterpModuleBinding;

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
    // Cache identity records parse policy, not mutable execution mode.
    bool ast_cache_requested_strict;
    bool ast_cache_typescript_profile;
    // Eval code uses configurable global var bindings and inherits any
    // pre-existing global property during declaration instantiation.
    bool is_eval_script;
    // Module top levels use a private module slab. CJS remains non-strict,
    // while ES modules set both this bit and strict_mode.
    bool is_module;
    bool is_es_module;
    // A Test262 source admitted by the runner's conservative native-harness
    // gate receives realm-local native assert helpers before evaluation.
    bool test262_native_harness;
    // The AST Test262 source can link this exhaustive helper directly while
    // retaining its ordinary writable global binding for test code.
    bool test262_native_build_string;
    // ES declaration instantiation happens before recursive dependency
    // evaluation so circular imports observe hoisted function exports.
    bool es_module_scope_initialized;
    bool strict_js;                 // true = reject TS syntax (pure JS mode)
    struct hashmap* type_registry; // TS name → Type* facts for this JS/TS unit
    JsInterpModuleBinding* interp_imports;
    JsInterpModuleBinding* interp_exports;
    // A common AST template never receives execution-local synthetic nodes or
    // callable facts. Reused instances retain those in this overlay.
    Pool* ast_overlay_pool;
    NamePool* ast_overlay_name_pool;
    bool ast_index_overlay;
    HashMap* ast_callables;
    HashMap* field_initializers;
};

// JsTranspiler is an ephemeral builder extending the retained JsScript prefix.
// Its tail contains only parser, diagnostics, and current-build state; adoption
// moves the prefix into a runtime-owned JsScript before destroying this tail.
struct JsTranspiler : JsScript {
    // Current-build state
    // ANY-census [Type_Infer TI3]: per-reason counts of expressions whose
    // static type fell back to `any`. Diagnostic only — shares the Lambda
    // reason catalog so both lanes report against one vocabulary.
    int any_census[ANY_REASON_COUNT];
    CompilerPassManager pass_manager;

    // Error handling
    bool has_errors;                // Error flag
    int binding_error_count;        // direct-scope diagnostics before validation
    StrBuf* error_buf;              // Error messages
    bool parse_error_valid;
    int64_t parse_error_row;
    int64_t parse_error_col;
    char parse_error_message[128];
    
    // Runtime integration
    Runtime* runtime;               // builder's borrowed runtime

    // RC-J7v2: this unit's literal pool, acquired lazily at the first interned
    // literal. The pool is context-owned, not transpiler-owned: a unit's code
    // outlives its builder in preamble and hot-reload batch mode.
    uint32_t const_unit_id;
    LambdaConstPool* const_pool;
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
// Define in an already-selected scope. Annex B companions use the enclosing
// var scope rather than a simple catch parameter's legacy var target.
NameEntry* js_scope_define_in_scope(JsTranspiler* tp, JsScope* scope,
    String* name, JsAstNode* node, JsVarKind kind);
void js_record_interp_import(JsTranspiler* tp, String* local, String* source,
    String* export_name, bool namespace_import);
void js_record_interp_export(JsTranspiler* tp, String* local,
    String* export_name, String* source, bool namespace_export,
    bool star_export);

// Shared direct-parser AST facts.
void js_report_any_census(JsTranspiler* tp);
int js_check_early_errors(JsTranspiler* tp, JsAstNode* ast);

// AST utility functions shared by direct JS and TypeScript reductions.
JsOperator js_operator_from_string(const char* op_str, size_t len);
bool js_rebuild_direct_scope_graph(JsTranspiler* tp, JsAstNode* ast);

bool js_transpiler_parse_c(JsTranspiler* tp, const char* source, size_t length,
                           JsParseMode mode);

// Error handling functions
void js_error(JsTranspiler* tp, SourceSpan span, const char* format, ...);
void js_syntax_error(JsTranspiler* tp, SourceSpan span, const char* message);

// Transpiler lifecycle functions
JsTranspiler* js_transpiler_create(Runtime* runtime);
void js_transpiler_destroy(JsTranspiler* tp);
JsFunctionNode* js_script_field_initializer_ensure(JsScript* script, JsFieldDefinitionNode* field);
Pool* js_script_execution_pool(JsScript* script);
NamePool* js_script_execution_name_pool(JsScript* script);
int js_transpiler_parse_error_get(const JsTranspiler* tp, int64_t* out_row,
                                  int64_t* out_col, char* out_message,
                                  int64_t out_message_size);
JsScript* js_script_adopt_transpiler(JsTranspiler* tp, Runtime* runtime,
                                     const char* reference);
// Common-cache AST adapter. Only ASTs with no lazy, per-realm compiler facts
// are admitted; execution state remains local to the receiving Runtime.
JsScript* js_common_ast_cache_lookup(Runtime* runtime, const char* source,
                                     size_t source_length, const char* reference,
                                     bool strict, bool typescript_profile);
InputScriptBuildClaim js_common_ast_cache_begin_build(InputScriptBuildScope* build,
    const char* source, size_t source_length, const char* reference,
    bool strict, bool typescript_profile);
void js_common_ast_cache_complete_build(InputScriptBuildScope* build,
    bool published, bool poison);
bool js_common_ast_cache_admit(Runtime* runtime, JsScript* script, const char* source,
                               size_t source_length, const char* reference,
                               bool strict, bool typescript_profile);
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
// Execute an admitted Test262 source with the AST tier's realm-local native
// harness. MIR callers retain their existing lowering interception path.
Item transpile_js_to_mir_test262_native_len(Runtime* runtime, const char* js_source,
                                            size_t js_source_len, const char* filename,
                                            uint64_t* result_home);
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

// A synchronous ES module has no inherited declaration slab, but it still
// needs a sealed property-name image and module-const snapshot to enter a
// fresh realm. Its static dependency paths are immutable compiler metadata;
// every hit reloads those modules into the receiving realm before execution
// (D8.5.1v2).
struct JsModuleMirArtifact {
    JsPreambleState image;
    char** static_dependency_paths;
    int static_dependency_count;
};

// Radiant owns only bounded InputManager artifact leases; it does not own a
// second JavaScript MIR cache.
struct JsMirLeaseSession;

struct JsMirLeaseSessionStats {
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t compiles;
    uint64_t instantiations;
    size_t retained_entries;
    size_t retained_metadata_bytes;
};

JsMirLeaseSession* js_mir_lease_session_create(void);
void js_mir_lease_session_close(JsMirLeaseSession* session);
const JsPreambleState* js_mir_lease_session_lookup(
    JsMirLeaseSession* session, bool preamble_mode,
    const char* source, size_t source_len, const char* filename,
    const JsPreambleState* preamble);
const JsPreambleState* js_mir_lease_session_adopt(
    JsMirLeaseSession* session, bool preamble_mode,
    const char* source, size_t source_len, const char* filename,
    const JsPreambleState* preamble, JsPreambleState* compiled_state);
InputScriptBuildClaim js_mir_lease_session_begin_build(
    JsMirLeaseSession* session, bool preamble_mode,
    const char* source, size_t source_len, const char* filename,
    const JsPreambleState* preamble, InputScriptBuildScope* build);
const JsPreambleState* js_mir_lease_session_adopt_build(
    JsMirLeaseSession* session, InputScriptBuildScope* build,
    JsPreambleState* compiled_state);
void js_mir_lease_session_complete_build(InputScriptBuildScope* build,
    bool published, bool poison);
void js_mir_lease_session_record_instantiation(JsMirLeaseSession* session);

// Direct module artifacts use the same manager-owned opaque MIR slot as
// document scripts. The returned scope pins a live module image until its
// Runtime has destroyed the realm that may hold pointers into that code.
const JsModuleMirArtifact* js_module_mir_cache_lookup(const char* source,
    size_t source_len, const char* filename, struct InputCacheScope** out_scope);
const JsModuleMirArtifact* js_module_mir_cache_adopt(const char* source,
    size_t source_len, const char* filename, JsModuleMirArtifact* compiled,
    struct InputCacheScope** out_scope);
InputScriptBuildClaim js_module_mir_cache_begin_build(const char* source,
    size_t source_len, const char* filename, InputScriptBuildScope* build);
const JsModuleMirArtifact* js_module_mir_cache_adopt_build(
    InputScriptBuildScope* build, JsModuleMirArtifact* compiled,
    struct InputCacheScope** out_scope);
void js_module_mir_cache_complete_build(InputScriptBuildScope* build,
    bool published, bool poison);
// Link exact module source generations so a changed static dependency retires
// every importer before its cached MIR can be reused (D8.5.1v2).
bool js_module_mir_cache_record_dependency(const char* importer_source,
    size_t importer_source_len, const char* importer_filename,
    const char* dependency_source, size_t dependency_source_len,
    const char* dependency_filename);
void js_module_mir_artifact_destroy(JsModuleMirArtifact* artifact);
Item load_js_module(Runtime* runtime, const char* js_path);

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
