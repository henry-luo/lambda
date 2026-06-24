#pragma once

// js_mir_context.hpp - shared context for the split JS MIR transpiler.
// Included by transpile_js_mir.cpp during the J41-1 mechanical split.

// for literals; expression results are already boxed.

#include "js_transpiler.hpp"
#include "../ts/ts_ast.hpp"
#include "../ts/ts_transpiler.hpp"
#include "js_dom.h"
#include "js_runtime.h"
#include "js_typed_array.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../lambda-decimal.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include "../../lib/file_utils.h"
#include "../../lib/file.h"
#include "../transpiler.hpp"
#include "../module_registry.h"
#include "../npm/npm_resolve_module.h"
#include <mir.h>
#include <mir-gen.h>
#include <cstring>
#include <cstdio>
#include "../../lib/mem.h"
#include "../lambda-stack.h"
#ifdef _WIN32
#include <malloc.h>  // alloca on Windows
#include <direct.h>  // _getcwd
#define getcwd _getcwd
#define realpath(p, r) (_fullpath((r), (p), _MAX_PATH))
#else
#include <alloca.h>
#include <unistd.h>  // getcwd
#endif

struct JsClassEntry;  // forward declaration for JsMirVarEntry.class_entry
// ============================================================================

// Module-scope constants: variables, functions, classes declared at top level.
enum JsModuleConstType {
    MCONST_INT,
    MCONST_FLOAT,
    MCONST_NULL,
    MCONST_UNDEFINED,
    MCONST_BOOL,
    MCONST_CLASS,   // class name: int_val = module var index for the class object
    MCONST_FUNC,    // function declaration: int_val = index into func_entries
    MCONST_MODVAR,  // runtime module variable: int_val = index into js_module_vars[]
};

struct JsModuleConstEntry {
    char name[128];     // e.g. "_js_INITIAL_SIZE"
    JsModuleConstType const_type;
    int64_t int_val;    // for MCONST_INT and MCONST_BOOL (0/1)
    double float_val;   // for MCONST_FLOAT
    bool is_int;        // legacy compat: true for int, false for float
    bool is_iife_var;   // true if promoted from IIFE scope (write-through always)
    TypeId modvar_type; // P5: for MCONST_MODVAR, the known initial type
    JsClassEntry* class_entry;  // P7: non-NULL if module var is a known class instance
    int var_kind;       // v20 TDZ: 0=var, 1=let, 2=const (for MCONST_MODVAR)
    bool is_implicit_global; // true if registered as implicit global (not explicitly declared)
    bool is_nested_func_hoist; // true if from nested function decl name (Annex B candidate, not a real var)
    bool annexb_suppressed;    // AnnexB B.3.3.3: true if propagation suppressed (let/const collision, catch param, etc.)
    // Js57 P3 (Track B2): live binding for self-imported default. When set,
    // identifier reads emit js_get_live_binding_default(specifier) instead of
    // js_get_module_var(int_val); the inner read sees TDZ until `export default`
    // overwrites namespace.default at the module's source position. Live
    // binding entries are also skipped during snapshot publication so closures
    // do not capture a pre-initialised undefined.
    bool is_live_default_binding;
    const char* live_binding_specifier; // resolved module path, NamePool-owned
};

// Evidence counters for parameter type inference.
struct JsParamEvidence {
    int int_evidence;
    int float_evidence;
    int string_evidence;
    bool used_as_container;  // param used as object in arr[i] expression
    bool compared_with_non_numeric;  // param compared with undefined/null/bool via ===
    bool param_reassigned;  // param is target of plain assignment; call-site type may differ
};

struct P4bCtorEvidence {
    int int_count;
    int float_count;
    int other_count;
};

struct P6NarrowEvidence {
    int int_count;
    int float_count;
    int other_count;
};

struct JsImportGraphNode {
    char* path;            // resolved file path (owned)
    char* source;          // source text (owned)
    int* deps;             // indices of dependency nodes (owned)
    int dep_count;
    int dep_cap;
    int depth;             // topological depth (-1 = uncomputed)
    MIR_context_t mir_ctx;
    void* js_main_func;    // typed as js_main_func_t at call sites
    bool compiled;
};

struct JsNameSetEntry {
    char name[128];
    int var_kind;  // v20 TDZ: 0=var, 1=let, 2=const (mirrors JsVarKind)
    bool from_func_decl;  // true if this name came from a nested function declaration
    uint32_t binding_start; // source range of the resolved defining binding, if known
    uint32_t binding_end;
};

static const uint64_t ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t ITEM_JS_UNDEF_VAL = (uint64_t)LMD_TYPE_UNDEFINED << 56;
static const uint64_t ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56) | 1;
static const uint64_t ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56) | 0;
static const uint64_t ITEM_INT_TAG   = (uint64_t)LMD_TYPE_INT << 56;
static const uint64_t STR_TAG        = (uint64_t)LMD_TYPE_STRING << 56;
static const uint64_t MASK56         = 0x00FFFFFFFFFFFFFFULL;

static const int JS_MIR_MAX_COLLECTED_FUNCTIONS = 32768;
static const int JS_MIR_MAX_COLLECTED_CLASSES = 4096;
static const int JS_MIR_LAST_CLOSURE_CAPTURE_MAX = 512;

struct JsMirImportEntry {
    MIR_item_t proto;
    MIR_item_t import;
};

struct JsImportCacheEntry {
    char name[128];
    JsMirImportEntry entry;
};

struct JsMirVarEntry {
    MIR_reg_t reg;
    MIR_type_t mir_type;     // MIR_T_I64 for int/boxed, MIR_T_D for native double
    TypeId type_id;          // LMD_TYPE_INT, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_ANY (boxed)
    bool from_env;           // true if loaded from closure env
    int env_slot;            // slot index in env array
    MIR_reg_t env_reg;       // register holding env pointer (for write-back)
    bool from_shared_env;    // true if captured from a shared scope_env (reload after calls)
    bool in_scope_env;       // true if this var is in parent func's scope env (write-back on assign)
    int scope_env_slot;      // slot in scope env
    MIR_reg_t scope_env_reg; // register holding scope env pointer
    int typed_array_type;    // P9: JsTypedArrayType enum value, -1 if not a typed array
    bool is_js_array;        // A2: true if variable is known to hold a regular JS array
    JsClassEntry* class_entry;  // P4: non-NULL if variable is a known class instance
    Type* full_type;         // P3.4: full Type* (e.g. TypeMap for interface vars; NULL otherwise)
    bool is_let_const;       // v20: true if declared with let/const (TDZ enforcement)
    bool is_const;           // true if declared with const (prevents reassignment)
    bool is_nfe_binding;     // true for named function expression self-binding
    bool from_block_func_decl; // true for lexical block function declaration binding
    bool from_catch_param;   // true for simple catch parameter binding
    bool tdz_active;         // v20: true if still in temporal dead zone (before declaration)
    MIR_reg_t hoisted_data_reg;  // P4h: hoisted items/data pointer for loop optimization (0 = not active)
    MIR_reg_t hoisted_len_reg;   // P4h: hoisted length register for loop optimization (0 = not active)
    bool from_hoist;             // v50: true if created by var-hoisting (not a parameter)
    // Js57 P3 (Track B2): live binding to another module's default export.
    // Used for self-imports (`import self from "./self.js"` inside self.js)
    // so reads see the current state of `namespace.default` rather than the
    // import-time snapshot (which is TDZ before `export default` runs).
    bool is_live_default_binding;
    const char* live_binding_specifier; // resolved module path, NamePool-owned
};

struct JsVarScopeEntry {
    char name[128];
    JsMirVarEntry var;
};

struct JsLocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
};

// Loop label pair for break/continue
struct JsLoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
    MIR_reg_t iterator_to_close;   // nonzero for for-of entries that need IteratorClose on outer abrupt jumps
    const char* label_name;       // v11: named label (NULL if anonymous)
    int label_name_len;           // v11: length of label name
};

// Capture entry for closure analysis
struct JsCaptureEntry {
    char name[128];      // variable name (with _js_ prefix)
    char scope_env_key[128]; // binding identity for shared env slots; name when not needed
    int scope_env_slot;  // slot in parent's scope env (-1 if not remapped)
    int grandparent_slot; // v29: for transitive captures in mixed scope envs, read from
                          // grandparent env (stored in parent env slot 0). -1 if not transitive.
    bool is_let_const;   // v29 TDZ: true if captured variable is let/const (needs TDZ check)
    bool is_const;       // true if captured variable is const (assignment throws)
    bool is_nfe_binding; // named function expression self-binding
    bool force_env_capture; // true when a lexical loop head shadows a module var
};

// Function entry for pre-pass collection
struct JsFuncCollected {
    JsFunctionNode* node;
    char name[128];
    MIR_item_t func_item;   // set after creation (boxed version)
    // Capture info
    JsCaptureEntry* captures;     // dynamically allocated capture array
    int captures_capacity;        // allocated size
    int capture_count;
    int parent_index;       // index of parent function in func_entries (-1 = top level)
    // Scope env: shared closure environment for all child closures
    bool has_scope_env;              // true if this func allocates a scope env
    int scope_env_count;             // number of vars in scope env
    int scope_env_normal_count;      // number of normal vars (excluding NFE extra slots and parent env link)
    char (*scope_env_names)[64];     // dynamically allocated: scope_env_count entries of 64 chars each
    bool reuse_parent_env;           // v16: true if scope_env reuses parent env (all vars transitive captures)
    int reuse_env_slot_count;        // v16: slot count when reusing parent env
    bool has_parent_env_link;        // v29: scope env slot 0 stores parent env pointer (for mixed transitive)
    // Phase 4: Type inference results
    TypeId param_types[16];         // inferred parameter types
    TypeId return_type;             // inferred return type
    int param_count;                // cached param count
    int formal_length;              // ES spec .length: params before first default/rest (-1 = same as param_count)
    MIR_item_t native_func_item;    // native version (NULL if not generated)
    bool has_native_version;        // whether native version was generated
    // TCO:
    bool is_tco_eligible;           // has tail-recursive calls → loop transform
    bool is_iife_body;              // true if this function is a top-level IIFE body
    // P3: Constructor flag (set for class constructor methods only)
    bool is_constructor;            // true if this function is a class constructor
    bool is_derived_constructor;    // true if class constructor has [[ConstructorKind]] derived
    bool is_class_method;           // true for any class method/accessor/constructor
    bool is_class_static_method;    // true for static class methods/accessors
    bool has_rest_param;            // true if last param is ...rest
    bool uses_arguments;            // v18q: true if function body references 'arguments'
    bool has_non_simple_params;      // v20: true if function has default/rest/destructuring params (no arguments aliasing)
    bool is_reassigned;              // function name is an assignment target somewhere in the module
    bool is_strict;                  // v30: true if function is strict mode (own directive, inherits, or class method)
    bool has_direct_eval;            // true if own function body contains syntactic eval(...)
    // A5: Constructor shape pre-allocation
    int ctor_prop_count;            // number of this.xxx = yyy properties found
    const char* ctor_prop_ptrs[16]; // pointers to pool-stable property name strings
    int ctor_prop_lens[16];         // lengths of each property name
    int ctor_prop_ta_types[16];     // typed array type for each prop (-1 = not a typed array)
    TypeId ctor_prop_types[16];     // P1: detected field type from constructor init (LMD_TYPE_NULL = unknown)
    int ctor_prop_param_idx[16];    // P4b: maps property → constructor param index (-1 = not a param)
};

// Free dynamically allocated scope_env_names for all func_entries
static void jm_free_scope_env_names(JsFuncCollected* func_entries, int func_count) {
    for (int i = 0; i < func_count; i++) {
        if (func_entries[i].scope_env_names) {
            mem_free(func_entries[i].scope_env_names);
            func_entries[i].scope_env_names = NULL;
        }
        if (func_entries[i].captures) {
            mem_free(func_entries[i].captures);
            func_entries[i].captures = NULL;
        }
    }
}

// Ensure captures array has room for at least one more entry
static void __attribute__((unused)) jm_ensure_captures_capacity(JsFuncCollected* fc) {
    if (fc->capture_count >= fc->captures_capacity) {
        int new_cap = fc->captures_capacity == 0 ? 16 : fc->captures_capacity * 2;
        JsCaptureEntry* new_arr = (JsCaptureEntry*)mem_calloc(new_cap, sizeof(JsCaptureEntry), MEM_CAT_JS_RUNTIME);
        if (fc->captures && fc->capture_count > 0) {
            memcpy(new_arr, fc->captures, fc->capture_count * sizeof(JsCaptureEntry));
        }
        mem_free(fc->captures);
        fc->captures = new_arr;
        fc->captures_capacity = new_cap;
    }
}

// Class method info for transpiler
struct JsClassMethodEntry {
    String* name;                   // method name
    JsFuncCollected* fc;            // collected function entry
    int param_count;
    bool is_constructor;
    bool is_static;
    bool is_getter;                 // getter method (get size() { ... })
    bool is_setter;                 // setter method (set value(v) { ... })
    bool computed;                  // computed property name ([expr])
    JsAstNode* key_expr;            // original key AST node (for computed keys)
};

// Static field entry for class
struct JsStaticFieldEntry {
    String* name;                   // field name (NULL if computed)
    JsAstNode* key_expr;            // key expression for computed fields
    JsAstNode* initializer;         // initializer expression
    int module_var_index;           // index into js_module_vars[] (-1 for computed)
    int key_module_var_index;       // class-evaluation computed key slot (-1 if not computed)
    bool computed;                  // whether this is a computed property name
};

// Instance field entry for class (non-static field initializers)
struct JsInstanceFieldEntry {
    String* name;                   // field name (already __private_ prefixed if private, NULL if computed)
    JsAstNode* key_expr;            // key expression for computed fields
    JsAstNode* initializer;         // initializer expression (NULL if no initializer)
    int key_module_var_index;       // class-evaluation computed key slot (-1 if not computed)
    bool computed;                  // whether this is a computed property name
};

// Class info for transpiler
struct JsClassEntry {
    JsClassNode* node;
    String* name;
    String* alias_name;                  // variable name for class expressions (var X = class Y {})
    JsClassMethodEntry methods[128];
    int method_count;
    JsClassMethodEntry* constructor;     // points into methods[] or NULL
    JsClassEntry* superclass;            // resolved parent class entry or NULL
    bool has_self_extends;               // class x extends x {} — TDZ violation
    bool is_declaration;                 // true for class declarations, false for class expressions
    int inner_module_var_index;          // immutable class-name binding inside class scope
    JsStaticFieldEntry static_fields[16]; // static field definitions
    int static_field_count;
    JsInstanceFieldEntry instance_fields[32]; // instance field definitions
    int instance_field_count;
    JsAstNode* static_blocks[8];            // static { ... } block bodies
    int static_block_count;
    void** shape_cache_ptr;                 // §7: per-class shape cache slot (NULL until allocated)
    bool ctor_shape_composed;               // Tune11 P5: inherited ctor fields merged
    bool ctor_shape_compose_failed;         // Tune11 P5: fell back to dynamic parent writes
};

// Try/catch context for handling return-in-try and exception flow
struct JsTryContext {
    MIR_label_t catch_label;     // jump here on exception (NULL if no catch)
    MIR_label_t finally_label;   // jump here for finally or normal exit
    MIR_label_t end_label;       // end of entire try statement
    MIR_reg_t return_val_reg;    // stores delayed return value
    MIR_reg_t has_return_reg;    // flag: 1 if return encountered in try/catch
    bool has_catch;
    bool has_finally;
    bool inlining_finally;       // re-entrance guard for finally block inlining
    bool yield_state_only;       // synthetic ctx solely for yield-resume re-init of state regs;
                                 // invisible to throw/return routing (skip in stack walks)
    JsAstNode* finally_body;     // v18: AST of finally block for inlining before break/continue
    MIR_reg_t saved_exc_flag_reg; // generator finally: pending-exception flag saved before finalizer
    MIR_reg_t saved_exc_val_reg;  // generator finally: pending-exception value saved before finalizer
};

struct JsMirTranspiler {
    JsTranspiler* tp;        // access to AST, name_pool, scopes

    MIR_context_t ctx;
    MIR_module_t module;
    MIR_item_t current_func_item;
    MIR_func_t current_func;

    // Import cache: name -> JsMirImportEntry
    struct hashmap* import_cache;

    // Local function items: name -> MIR_item_t
    struct hashmap* local_funcs;

    // Variable scopes: array of hashmaps, name -> JsMirVarEntry
    struct hashmap* var_scopes[64];
    int scope_depth;
    int var_hoist_depth;  // >=0: redirect jm_set_var to this depth for 'var' hoisting; -1 = normal

    // Loop label stack
    JsLoopLabels loop_stack[32];
    int loop_depth;
    int iteration_depth;
    int loop_scope_depth;

    // Active for-of iterator stack for return cleanup
    MIR_reg_t for_of_iterators[32];
    int for_of_depth;

    // v11: pending label for next loop push
    const char* pending_label_name;
    int pending_label_len;

    int reg_counter;
    int label_counter;

    // Collected functions (pre-pass)
    JsAstNode* root_node;
    JsFuncCollected func_entries[JS_MIR_MAX_COLLECTED_FUNCTIONS];
    int func_count;
    bool func_collection_overflow_logged;

    // Collected classes
    JsClassEntry class_entries[JS_MIR_MAX_COLLECTED_CLASSES];
    int class_count;
    bool class_collection_overflow_logged;

    // Current class being transpiled (for super resolution)
    JsClassEntry* current_class;

    // Try/catch context stack (for return-in-try and exception flow)
    JsTryContext try_ctx_stack[16];
    int try_ctx_depth;

    // Phase 4: Native function generation state
    bool in_native_func;            // currently transpiling native version?
    JsFuncCollected* current_fc;    // current function being transpiled

    // TCO state
    JsFuncCollected* tco_func;      // function being TCO'd (NULL if not active)
    MIR_label_t tco_label;          // loop-back label for tail calls
    MIR_reg_t tco_count_reg;        // iteration counter for overflow guard
    bool in_tail_position;          // current expression is in tail position
    bool tco_jumped;                // set when a tail call was converted to goto

    // P9: Variable widening from INT→FLOAT (pre-scan)
    struct hashmap* widen_to_float;  // set of variable names that should be FLOAT
    struct hashmap* force_boxed;     // set of variable names that must use boxed Item (non-numeric assignments)

    // Module-level constants: name -> value (for top-level const with literal init)
    struct hashmap* module_consts;   // name -> JsModuleConstEntry
    int module_var_count;            // next index for js_module_vars[]

    bool in_main;                    // true when transpiling Phase 3 (js_main)

    // Closure env read-back for mutable captures (forEach, reduce, etc.)
    MIR_reg_t last_closure_env_reg;
    int last_closure_capture_count;
    char last_closure_capture_names[JS_MIR_LAST_CLOSURE_CAPTURE_MAX][128];
    int last_closure_capture_slots[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool last_closure_capture_is_transitive[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool last_closure_capture_is_nfe[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool last_closure_has_env;
    bool allow_loop_let_scope_env_for_immediate_call;
    bool preserve_last_closure_env_after_readback;

    // Assignment target hint for closure self-capture detection in copy-env path
    const char* assign_target_vname;  // set before RHS eval, NULL otherwise

    // Phase 1: parent function tracking during collection
    int collect_parent_func_index;   // current parent func index (-1 = top level)

    // Scope env: shared closure environment for all child closures in current function
    MIR_reg_t scope_env_reg;         // register holding current func's scope env (0 if none)
    int scope_env_slot_count;        // number of slots in current scope env
    int current_func_index;          // index of current func in func_entries (-1 if not set)

    // ES module support
    bool is_module;                  // true when compiling an ES module (not main script)
    bool is_global_strict;           // v20: true when top-level "use strict" directive present
    bool is_eval_direct;             // true when compiling eval code as direct script (sloppy-mode var export)
    uint64_t template_site_salt;      // non-zero for eval compilations; separates eval template sites
    MIR_reg_t namespace_reg;         // register holding module namespace object (when is_module)
    const char* filename;            // path of current file being compiled

    // v15: Generator state machine
    bool in_generator;               // currently emitting a generator state machine body
    bool in_async;                   // currently emitting an async function body (Phase 5)
    MIR_reg_t gen_env_reg;           // register for env parameter (Item*)
    MIR_reg_t gen_input_reg;         // register for input parameter (Item)
    MIR_reg_t gen_state_reg;         // register for state parameter (int64_t)
    int gen_yield_index;             // counter for next yield state assignment
    int gen_yield_count;             // total yield count (from pre-scan)
    MIR_label_t gen_state_labels[64];  // labels for each resume state (1..yield_count)
    MIR_label_t gen_done_label;      // label for done state (function end)
    // Generator variable-to-env-slot mapping
    int gen_local_slot_count;        // total env slots (captures + params + locals)
    int gen_capture_offset;          // start of captures in env
    int gen_param_offset;            // start of params in env
    int gen_local_offset;            // start of locals in env
    int gen_spill_slot_next;         // next available spill slot in env (for temporaries across yields)
    int gen_active_iterator_slot;    // iterator to close if generator.return interrupts destructuring

    // Exception propagation: label to jump to when an exception is detected outside a try block.
    // Lazily created when first needed within a function body. Jumps to this label → return null.
    MIR_label_t func_except_label;   // 0 if not yet created for current function

    // v20: arguments aliasing state
    MIR_reg_t arguments_reg;         // register holding 'arguments' object (0 if not active)
    int arguments_param_count;       // number of formal params mapped to arguments
    char arguments_param_names[16][128]; // formal param var names (_js_xxx)

    // Batch preamble mode: compile harness (sta.js + assert.js) so func decls persist as module vars
    bool preamble_mode;
    // With-preamble mode: pre-seed module_consts from harness compilation
    JsModuleConstEntry* preamble_entries;   // array of entries to pre-seed (owned by caller)
    int preamble_entry_count;
    int preamble_var_count;                 // starting module_var_count from preamble

    // Eval completion value: when set, expression statements store their value into this register.
    // Used by js_main to capture the result of the last evaluated expression (even inside
    // control flow statements like for/while/if/switch), implementing ES spec §13.5.1.
    MIR_reg_t eval_completion_reg;           // 0 if not tracking completion values
    MIR_reg_t eval_local_frame_reg;           // non-zero when direct eval pushed a caller-local frame
    bool in_typeof;                          // true when transpiling operand of typeof
    int with_depth;                           // nesting depth of 'with' statements (for break/continue/return cleanup)
    bool destructure_assignment_mode;         // true for assignment-pattern destructuring targets

    // Js57 Track A: synthetic module-level scope env. Captures of top-level closures
    // (parent_index == -1) that reference block-lets at module scope land here. The
    // env is allocated at js_main entry and shared across all top-level child closures
    // so mutations propagate (matches spec lexical-env semantics). For-init lets are
    // excluded so per-iteration semantics still works.
    JsFuncCollected module_fc;
    bool module_scope_env_active;             // true if module_fc has been initialised and scope_env is live
};

static void __attribute__((unused)) jm_cleanup_mir_transpiler_state(JsMirTranspiler* mt) {
    if (!mt) return;
    if (mt->import_cache) {
        hashmap_free(mt->import_cache);
        mt->import_cache = NULL;
    }
    if (mt->local_funcs) {
        hashmap_free(mt->local_funcs);
        mt->local_funcs = NULL;
    }
    if (mt->widen_to_float) {
        hashmap_free(mt->widen_to_float);
        mt->widen_to_float = NULL;
    }
    if (mt->module_consts) {
        hashmap_free(mt->module_consts);
        mt->module_consts = NULL;
    }
    for (int i = 0; i <= mt->scope_depth && i < 64; i++) {
        if (mt->var_scopes[i]) {
            hashmap_free(mt->var_scopes[i]);
            mt->var_scopes[i] = NULL;
        }
    }
    for (int i = 0; i < mt->class_count && i < JS_MIR_MAX_COLLECTED_CLASSES; i++) {
        if (mt->class_entries[i].shape_cache_ptr) {
            mem_free(mt->class_entries[i].shape_cache_ptr);
            mt->class_entries[i].shape_cache_ptr = NULL;
        }
    }
    jm_free_scope_env_names(mt->func_entries, mt->func_count);
    if (mt->module_fc.scope_env_names) {
        mem_free(mt->module_fc.scope_env_names);
        mt->module_fc.scope_env_names = NULL;
    }
    if (mt->module_fc.captures) {
        mem_free(mt->module_fc.captures);
        mt->module_fc.captures = NULL;
    }
}
