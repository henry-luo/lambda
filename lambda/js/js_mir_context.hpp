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
#include "../core/lambda-decimal.hpp"
#include "../runtime/mir_emitter_shared.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include "../../lib/file_utils.h"
#include "../../lib/file.h"
#include "../runtime/transpiler.hpp"
#include "../runtime/module_registry.h"
#include <mir.h>
#include <mir-gen.h>
#include <cstring>
#include <cstdio>
#include "../../lib/mem.h"
#include "../runtime/lambda-stack.h"
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

typedef MirRootBinding JsMirRootBinding;
typedef MirEnvBinding JsMirEnvBinding;

// Native bodies state their ABI result independently of the JS-inferred
// return type. BOXED/VOID remain unavailable until their complete lowering and
// direct-call contracts exist; treating either as an integer changes JS.
enum NativeReturnKind : uint8_t {
    NATIVE_RETURN_NONE = 0,
    NATIVE_RETURN_INT,
    NATIVE_RETURN_FLOAT,
    NATIVE_RETURN_BOXED,
    NATIVE_RETURN_VOID,
};
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
    const char* name;   // NamePool-owned semantic binding name
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
    bool is_iife_func_decl; // true if direct sync-IIFE function decl promoted to module var for escaping closures
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

struct JsImportGraphNode {
    char* path;            // resolved file path (owned)
    char* source;          // source text (owned)
    int* deps;             // indices of dependency nodes (owned)
    int dep_count;
    int dep_cap;
    int depth;             // topological depth (-1 = uncomputed)
    MIR_context_t mir_ctx;
    void* js_main_func;    // typed as js_main_func_t at call sites
    uint32_t module_var_count;
    PropertyKeySpec* module_property_specs;
    uint32_t module_property_count;
    uint32_t module_property_bytes_size;
    uint32_t ic_count;
    bool compiled;
};

struct JsNameSetEntry {
    const char* name;   // NamePool-owned semantic binding name
    int var_kind;  // v20 TDZ: 0=var, 1=let, 2=const (mirrors JsVarKind)
    bool from_func_decl;  // true if this name came from a nested function declaration
    uint32_t binding_start; // source range of the resolved defining binding, if known
    uint32_t binding_end;
    NameEntry* entry; // AST binding identity, when this record came from an identifier
};

static const uint64_t ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t ITEM_JS_UNDEF_VAL = (uint64_t)LMD_TYPE_UNDEFINED << 56;
static const uint64_t ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56) | 1;
static const uint64_t ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56) | 0;
static const uint64_t STR_TAG        = (uint64_t)LMD_TYPE_STRING << 56;

static const int JS_MIR_LAST_CLOSURE_CAPTURE_MAX = 512;
static const int JS_MIR_TDZ_CLOSURE_CAPTURE_MAX = 512;

typedef MirImportEntry JsMirImportEntry;

struct JsLocalFuncEntry {
    // function lookup is semantic metadata; keep the AST spelling in the
    // transpiler's NamePool instead of truncating it in a MIR-sized buffer.
    const char* name;
    MIR_item_t func_item;
};

typedef struct JsMirTdzClosureCapture {
    MIR_reg_t env_reg;
    int slot;
    int binding_scope_depth;
    bool is_transitive;
    const char* name;   // NamePool-owned binding name
} JsMirTdzClosureCapture;

typedef enum JsErrorLaneTrack {
    // D8.4.3: compiler dataflow for the last returned Item, never ambient
    // runtime exception state.
    JS_ERROR_LANE_UNKNOWN = 0,
    JS_ERROR_LANE_CLEAN,
    JS_ERROR_LANE_SET,
    JS_ERROR_LANE_UNREACHABLE,
} JsErrorLaneTrack;

// Loop label pair for break/continue
struct JsLoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
    MIR_reg_t iterator_to_close;   // nonzero for for-of entries that need IteratorClose on outer abrupt jumps
    const char* label_name;       // v11: named label (NULL if anonymous)
    int label_name_len;           // v11: length of label name
};

// A dynamically sized iterator-cleanup entry. Iterator registers are MIR
// values rather than pointers, so they are stored in a stack-owned record when
// held by the Lambda ArrayList.
struct JsMirIteratorFrame {
    MIR_reg_t iterator;
};

// Function entry for pre-pass collection
struct JsFuncCollected {
    JsFunctionNode* node;
    const char* name;       // NamePool-owned semantic/function identity
    const char* body_name;  // NamePool-owned backend body symbol
    MIR_item_t func_item;        // public checked wrapper
    MIR_item_t body_func_item;   // internal boxed implementation body
    // Capture info
    FnCapture* captures;          // dynamically allocated capture array
    int captures_capacity;        // allocated size
    int capture_count;
    FnAnalysis analysis;
    int parent_index;       // index of parent function in func_entries (-1 = top level)
    // Scope env: shared closure environment for all child closures
    bool has_scope_env;              // true if this func allocates a scope env
    int scope_env_count;             // number of vars in scope env
    int scope_env_normal_count;      // number of normal vars (excluding NFE extra slots and parent env link)
    const char** scope_env_names;    // NamePool-owned scope binding keys
    bool reuse_parent_env;           // v16: true if scope_env reuses parent env (all vars transitive captures)
    int reuse_env_slot_count;        // v16: slot count when reusing parent env
    bool has_parent_env_link;        // v29: scope env slot 0 stores parent env pointer (for mixed transitive)
    bool parent_env_link_uses_grandparent; // parent link should store parent's parent env
    bool has_immediate_parent_env_link; // compact env also links direct parent-local cells
    int immediate_parent_env_link_slot;
    bool closure_env_has_parent_link; // copied closure env carries direct parent scope_env for mixed loop captures
    int closure_env_parent_link_slot; // slot in copied closure env holding direct parent scope_env
    // phase 4: type inference results. Per-formal type records live in the
    // shared FnAnalysis metadata and are sized from the JS AST parameter list.
    TypeId return_type;             // inferred return type
    ScalarReturnClass boxed_return_scalar_class; // caller-home need for boxed body
    int param_count;                // cached param count
    int formal_length;              // ES spec .length: params before first default/rest (-1 = same as param_count)
    MIR_item_t native_func_item;    // native version (NULL if not generated)
    bool has_native_version;        // whether native version was generated
    NativeReturnKind native_return_kind; // ABI result for the native entry
    // TCO:
    bool is_tco_eligible;           // has tail-recursive calls → loop transform
    bool is_iife_body;              // true if this function is a top-level IIFE body
    bool is_iife_func_decl;         // true if declared directly inside a promoted IIFE body
    // P3: Constructor flag (set for class constructor methods only)
    bool is_constructor;            // true if this function is a class constructor
    bool is_derived_constructor;    // true if class constructor has [[ConstructorKind]] derived
    bool is_class_method;           // true for any class method/accessor/constructor
    bool is_class_static_method;    // true for static class methods/accessors
    bool is_class_field_initializer; // synthetic per-instance field capability
    JsClassEntry* owner_class;       // innermost class whose lexical body contains this function
    bool has_rest_param;            // true if last param is ...rest
    bool uses_arguments;            // v18q: true if function body references 'arguments'
    bool has_non_simple_params;      // v20: true if function has default/rest/destructuring params (no arguments aliasing)
    bool is_reassigned;              // function name is an assignment target somewhere in the module
    bool is_strict;                  // v30: true if function is strict mode (own directive, inherits, or class method)
    bool has_direct_eval;            // true if own function body contains syntactic eval(...)
    bool observes_this;              // own body or lexical arrows read this
    bool observes_new_target;        // own body or lexical arrows read new.target
    bool uses_with;                  // own body contains a with statement
    // A5: Constructor shape pre-allocation
    int ctor_prop_count;            // number of this.xxx = yyy properties found
    bool ctor_shape_overflow;       // optimization disabled after exceeding shape metadata capacity
    const char* ctor_prop_ptrs[16]; // pointers to pool-stable property name strings
    int ctor_prop_lens[16];         // lengths of each property name
    int ctor_prop_ta_types[16];     // typed array type for each prop (-1 = not a typed array)
    TypeId ctor_prop_types[16];     // P1: detected field type from constructor init (LMD_TYPE_NULL = unknown)
    int ctor_prop_param_idx[16];    // P4b: maps property → constructor param index (-1 = not a param)
    // immutable post-collection scope facts; cache AST walks shared by capture,
    // propagation, and scope-environment planning.
    struct hashmap* cached_var_locals;
    struct hashmap* cached_all_locals;
    struct hashmap* cached_direct_lexicals;
    struct hashmap* cached_annexb_suppressed;
    bool cached_annexb_suppressed_ready;
};

static inline FnParamTypeInfo* jm_param_info(JsFuncCollected* fc, int index) {
    if (!fc || index < 0 || index >= fc->analysis.param_count ||
            !fc->analysis.param_types) return NULL;
    return &fc->analysis.param_types[index];
}

static inline const FnParamTypeInfo* jm_param_info_const(const JsFuncCollected* fc,
        int index) {
    if (!fc || index < 0 || index >= fc->analysis.param_count ||
            !fc->analysis.param_types) return NULL;
    return &fc->analysis.param_types[index];
}

static inline TypeId jm_param_type(const JsFuncCollected* fc, int index) {
    const FnParamTypeInfo* info = jm_param_info_const(fc, index);
    return info ? info->semantic_type : LMD_TYPE_ANY;
}

static inline void jm_set_param_type(JsFuncCollected* fc, int index, TypeId type) {
    FnParamTypeInfo* info = jm_param_info(fc, index);
    if (info) info->semantic_type = type;
}

// Free dynamically allocated scope_env_names for all func_entries
static void jm_free_scope_env_names(JsFuncCollected* func_entries, int func_count) {
    for (int i = 0; i < func_count; i++) {
        if (func_entries[i].cached_var_locals) {
            hashmap_free(func_entries[i].cached_var_locals);
            func_entries[i].cached_var_locals = NULL;
        }
        if (func_entries[i].cached_all_locals) {
            hashmap_free(func_entries[i].cached_all_locals);
            func_entries[i].cached_all_locals = NULL;
        }
        if (func_entries[i].cached_direct_lexicals) {
            hashmap_free(func_entries[i].cached_direct_lexicals);
            func_entries[i].cached_direct_lexicals = NULL;
        }
        if (func_entries[i].cached_annexb_suppressed) {
            hashmap_free(func_entries[i].cached_annexb_suppressed);
            func_entries[i].cached_annexb_suppressed = NULL;
        }
        if (func_entries[i].scope_env_names) {
            mem_free(func_entries[i].scope_env_names);
            func_entries[i].scope_env_names = NULL;
        }
        if (func_entries[i].captures) {
            mem_free(func_entries[i].captures);
            func_entries[i].captures = NULL;
        }
        if (func_entries[i].analysis.param_types) {
            mem_free(func_entries[i].analysis.param_types);
            func_entries[i].analysis.param_types = NULL;
            func_entries[i].analysis.param_count = 0;
        }
        // shape cache slots are pool-owned because generated MIR embeds their addresses.
    }
}

// Ensure captures array has room for at least one more entry
static void __attribute__((unused)) jm_ensure_captures_capacity(JsFuncCollected* fc) {
    if (fc->capture_count >= fc->captures_capacity) {
        int new_cap = fc->captures_capacity == 0 ? 16 : fc->captures_capacity * 2;
        FnCapture* new_arr = (FnCapture*)mem_calloc(new_cap, sizeof(FnCapture), MEM_CAT_JS_RUNTIME);
        if (fc->captures && fc->capture_count > 0) {
            memcpy(new_arr, fc->captures, fc->capture_count * sizeof(FnCapture));
        }
        mem_free(fc->captures);
        fc->captures = new_arr;
        fc->analysis.captures = fc->captures;
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
    String* name;                   // source field name (#name if private, NULL if computed)
    JsAstNode* key_expr;            // key expression for computed fields
    JsAstNode* initializer;         // initializer expression (NULL if no initializer)
    JsFuncCollected* initializer_fc; // internal per-instance initializer capability
    int key_module_var_index;       // class-evaluation computed key slot (-1 if not computed)
    bool computed;                  // whether this is a computed property name
};

// Class info for transpiler
struct JsClassEntry {
    JsClassNode* node;
    String* name;
    String* alias_name;                  // variable name for class expressions (var X = class Y {})
    JsClassMethodEntry* methods;          // exact-sized, stable for the compile lifetime
    int method_capacity;
    int method_count;
    JsClassMethodEntry* constructor;     // points into methods[] or NULL
    JsClassEntry* superclass;            // resolved parent class entry or NULL
    bool has_self_extends;               // class x extends x {} — TDZ violation
    bool is_declaration;                 // true for class declarations, false for class expressions
    int inner_module_var_index;          // immutable class-name binding inside class scope
    JsStaticFieldEntry* static_fields;    // exact-sized, stable for the compile lifetime
    int static_field_capacity;
    int static_field_count;
    JsInstanceFieldEntry* instance_fields; // exact-sized, stable for the compile lifetime
    int instance_field_capacity;
    int instance_field_count;
    JsAstNode** static_blocks;               // exact-sized, stable for the compile lifetime
    int static_block_capacity;
    int static_block_count;
};

// Try/catch context for handling return-in-try and exception flow
struct JsTryContext {
    MIR_label_t catch_label;     // jump here on exception (NULL if no catch)
    MIR_label_t finally_label;   // jump here for finally or normal exit
    MIR_label_t end_label;       // end of entire try statement
    MIR_reg_t return_val_reg;    // stores delayed return value
    MIR_reg_t has_return_reg;    // flag: 1 if return encountered in try/catch
    bool end_label_has_edge;     // compiler-only: an emitted completion targets end_label
    JsErrorLaneTrack end_label_error_lane_state; // merged proof for end_label predecessors
    bool has_catch;
    bool has_finally;
    bool inlining_finally;       // re-entrance guard for finally block inlining
    bool yield_state_only;       // synthetic ctx solely for yield-resume re-init of state regs;
                                 // invisible to throw/return routing (skip in stack walks)
    JsAstNode* finally_body;     // v18: AST of finally block for inlining before break/continue
    MIR_reg_t saved_error_lane_flag_reg; // generator finally: routed ERROR tag saved before finalizer
    MIR_reg_t saved_error_lane_val_reg;  // generator finally: routed ERROR Item saved before finalizer
    MIR_reg_t incoming_error_lane_val_reg; // routed ERROR Item handed to catch/finally
};

// A call/new expression owns one fixed argument-root extent inside its
// generated function frame. Nested calls use higher slots while sibling calls
// reuse the same slots after their lexical extent ends.
struct JsMirArgStackScope {
    JsMirArgStackScope* parent;
    int saved_depth;
    int base_slot;
    int slot_count;
    MIR_reg_t args_reg;
};

struct JsMirTranspiler {
    JsTranspiler* tp;        // access to AST, name_pool, scopes

    MIR_context_t ctx;
    MIR_module_t module;
    MirEmitter em;

    // Local function items: name -> MIR_item_t
    struct hashmap* local_funcs;

    // Variable scopes: dynamic stack of hashmaps, name -> JsMirVarEntry.
    // Each map carries pass-local MIR registers, type, TDZ, root, and
    // environment state for one lexical scope.
    ArrayList* var_scopes;
    int scope_depth;
    int var_hoist_depth;  // >=0: redirect jm_set_var to this depth for 'var' hoisting; -1 = normal

    // Loop label stack. Entries are JsLoopLabels* owned by the ArrayList.
    ArrayList* loop_stack;
    int loop_depth;
    int iteration_depth;
    int loop_scope_depth;

    // Active for-of iterator stack for return cleanup. Entries are
    // JsMirIteratorFrame* owned by the ArrayList.
    ArrayList* for_of_iterators;
    int for_of_depth;

    // v11: pending label for next loop push
    const char* pending_label_name;
    int pending_label_len;

    // Collected functions (pre-pass)
    JsAstNode* root_node;
    JsFuncCollected* func_entries;      // exact-sized after the shared count pass
    int func_capacity;
    int func_count;
    JsFunctionNode** func_index_nodes;  // pointer-keyed identity index, built once after collection
    int* func_index_ids;
    int func_index_capacity;

    // Collected classes
    JsClassEntry* class_entries;        // exact-sized after the shared count pass
    int class_capacity;
    int class_count;
    bool collection_count_only;
    bool collection_failed;

    // Current class being transpiled (for super resolution)
    JsClassEntry* current_class;
    MIR_reg_t current_private_home_class_reg; // exact evaluated class during inline initialization

    // Try/catch context stack (for return-in-try and exception flow). Entries
    // are JsTryContext* owned by the ArrayList.
    ArrayList* try_ctx_stack;
    int try_ctx_depth;

    // Phase 4: Native function generation state
    bool in_native_func;            // currently transpiling native version?
    JsFuncCollected* current_fc;    // current function being transpiled
    JsAstNode* discarded_expression; // outer expression whose value is unobserved

    struct {
        uint32_t module_name_index;
        NameId direct_name_id;
        MIR_reg_t reg;
    } property_name_cache[32];
    int property_name_cache_count;
    MIR_item_t property_name_cache_func;
    struct {
        uint32_t module_name_index;
        NameId direct_name_id;
        MIR_reg_t reg;
    } module_name_id_cache[32];
    int module_name_id_cache_count;
    MIR_item_t module_name_id_cache_func;
    struct {
        uint32_t index;
        MIR_reg_t reg;
    } module_ic_cache[32];
    int module_ic_cache_count;
    MIR_item_t module_ic_cache_func;

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

    // Property-name literals are emitted as module-table lookups. The table
    // is linked to the active NamePool after the static compilation root is
    // sealed, so generated MIR never embeds a compiler-owned String*.
    ArrayList* module_name_specs;
    uint32_t module_name_base;
    uint32_t ic_count;
    uint32_t module_ic_base;

    bool in_main;                    // true when transpiling Phase 3 (js_main)

    // Closure env read-back for mutable captures (forEach, reduce, etc.)
    MIR_reg_t last_closure_env_reg;
    int last_closure_capture_count;
    const char* last_closure_capture_names[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    int last_closure_capture_slots[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool last_closure_capture_is_transitive[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool last_closure_capture_is_nfe[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool last_closure_capture_is_assigned[JS_MIR_LAST_CLOSURE_CAPTURE_MAX];
    bool last_closure_has_env;
    // Hoisted closures can be created while a later lexical binding is still
    // TDZ. Retain every such cell until that binding initializes.
    JsMirTdzClosureCapture tdz_closure_captures[JS_MIR_TDZ_CLOSURE_CAPTURE_MAX];
    int tdz_closure_capture_count;
    bool allow_loop_let_scope_env_for_immediate_call;
    bool preserve_last_closure_env_after_readback;
    bool force_closure_env_copy;    // class field initializers need a stable lexical this cell

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
    int cascade_debug_site_counter;  // compile-local diagnostic site IDs
    int fallback_debug_site_counter;
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
    int gen_dynamic_slot_limit;      // first spill slot; lexical homes stay below it
    int gen_capture_offset;          // start of captures in env
    int gen_param_offset;            // start of params in env
    int gen_local_offset;            // start of locals in env
    int gen_spill_slot_next;         // next available spill slot in env (for temporaries across yields)
    int gen_active_iterator_slot;    // iterator to close if generator.return interrupts destructuring

    // D8.4.3: route a returned ERROR Item outside a lexical try to this
    // lazily-created function exit; no flag is polled or cleared.
    MIR_label_t func_error_lane_label;   // 0 if not yet created for current function
    JsErrorLaneTrack error_lane_track;
    // The transition emitter remembers the most recent boxed call result so
    // in-band mode can test its ERROR tag without issuing a separate poll.
    MIR_reg_t last_call_result_reg;
    // A function-level exceptional edge must retain the exact Item that
    // triggered the branch; later cleanup emitted on the normal path may
    // legitimately replace last_call_result_reg before the landing pad.
    MIR_reg_t func_error_lane_value_reg;

    JsMirArgStackScope* arg_stack_scope; // active call/new argument extent, if any
    MIR_reg_t arg_frame_base;
    MIR_insn_t arg_frame_base_add;
    int arg_frame_depth;
    int arg_frame_slot_count;

    // v20: arguments aliasing state
    MIR_reg_t arguments_reg;         // register holding 'arguments' object (0 if not active)
    // The AST owns this linked list for the full compilation.  Copying formal
    // names here capped mapped `arguments` semantics at 16 parameters.
    JsAstNode* arguments_params;     // simple formal parameters mapped to arguments, or NULL
    int arguments_param_scope_depth; // lexical scope containing those formal bindings

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
    if (mt->em.import_cache) {
        hashmap_free(mt->em.import_cache);
        mt->em.import_cache = NULL;
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
    if (mt->var_scopes) {
        for (int i = 0; i < mt->var_scopes->length; i++) {
            struct hashmap* scope =
                (struct hashmap*)arraylist_get(mt->var_scopes, i);
            if (scope) hashmap_free(scope);
        }
        arraylist_free(mt->var_scopes);
        mt->var_scopes = NULL;
    }
    if (mt->loop_stack) {
        for (int i = 0; i < mt->loop_stack->length; i++) {
            JsLoopLabels* labels =
                (JsLoopLabels*)arraylist_get(mt->loop_stack, i);
            if (labels) mem_free(labels);
        }
        arraylist_free(mt->loop_stack);
        mt->loop_stack = NULL;
    }
    if (mt->for_of_iterators) {
        for (int i = 0; i < mt->for_of_iterators->length; i++) {
            JsMirIteratorFrame* frame =
                (JsMirIteratorFrame*)arraylist_get(mt->for_of_iterators, i);
            if (frame) mem_free(frame);
        }
        arraylist_free(mt->for_of_iterators);
        mt->for_of_iterators = NULL;
    }
    if (mt->try_ctx_stack) {
        for (int i = 0; i < mt->try_ctx_stack->length; i++) {
            JsTryContext* try_context =
                (JsTryContext*)arraylist_get(mt->try_ctx_stack, i);
            if (try_context) mem_free(try_context);
        }
        arraylist_free(mt->try_ctx_stack);
        mt->try_ctx_stack = NULL;
    }
    if (mt->module_name_specs) {
        arraylist_free(mt->module_name_specs);
        mt->module_name_specs = NULL;
    }
    if (mt->func_entries) jm_free_scope_env_names(mt->func_entries, mt->func_count);
    mt->func_entries = NULL;
    mt->class_entries = NULL;
    if (mt->module_fc.scope_env_names) {
        mem_free(mt->module_fc.scope_env_names);
        mt->module_fc.scope_env_names = NULL;
    }
    if (mt->module_fc.captures) {
        mem_free(mt->module_fc.captures);
        mt->module_fc.captures = NULL;
    }
    if (mt->em.frame.root_bindings) {
        mem_free(mt->em.frame.root_bindings);
        mt->em.frame.root_bindings = NULL;
    }
    if (mt->em.frame.root_binding_by_reg) {
        mem_free(mt->em.frame.root_binding_by_reg);
        mt->em.frame.root_binding_by_reg = NULL;
    }
    if (mt->em.frame.root_binding_by_home) {
        mem_free(mt->em.frame.root_binding_by_home);
        mt->em.frame.root_binding_by_home = NULL;
    }
    if (mt->em.frame.gc_candidates) {
        mem_free(mt->em.frame.gc_candidates);
        mt->em.frame.gc_candidates = NULL;
    }
    if (mt->em.frame.gc_candidate_by_reg) {
        mem_free(mt->em.frame.gc_candidate_by_reg);
        mt->em.frame.gc_candidate_by_reg = NULL;
    }
    if (mt->em.frame.gc_call_sites) {
        mem_free(mt->em.frame.gc_call_sites);
        mt->em.frame.gc_call_sites = NULL;
    }
    if (mt->em.frame.env_bindings) {
        mem_free(mt->em.frame.env_bindings);
        mt->em.frame.env_bindings = NULL;
    }
}
