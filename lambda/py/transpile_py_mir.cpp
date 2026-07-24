// transpile_py_mir.cpp - Direct MIR generation for Python
//
// Mirrors the JavaScript MIR transpiler architecture (transpile_js_mir.cpp).
// All Python values are boxed Items (MIR_T_I64). Runtime functions
// (py_add, py_subtract, etc.) take and return Items.

#include "py_transpiler.hpp"
#include "../runtime/mir_dump.h"
#include "py_runtime.h"
#include "py_bigint.h"
#include "py_async.h"
#include "py_stdlib.h"
#include "../lambda-data.hpp"
#define MIR_EMITTER_NO_DIRECT_REGISTER_LOOKUP 1
#include "../runtime/mir_emitter_shared.hpp"
#include "../jube/jube_language.h"
#include "../../lib/log.h"
#include "../../lib/mem_factory.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/mempool.h"
#include "../runtime/transpiler.hpp"
#include "../runtime/heap_api.h"
#include <mir.h>
#include <mir-gen.h>
#include <cstring>
#include <cstdio>
#include "../../lib/mem.h"
#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <sys/stat.h>

extern "C" void py_reset_module_vars();

// ============================================================================
// Constants
// ============================================================================
static const uint64_t PY_ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t PY_ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56) | 1;
static const uint64_t PY_ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56) | 0;
static const uint64_t PY_ITEM_INT_TAG   = (uint64_t)LMD_TYPE_INT << 56;
static const uint64_t PY_MASK56         = 0x00FFFFFFFFFFFFFFULL;

// These fixed compiler tables describe generated MIR state.  Each limit is
// checked at collection/lowering time so a larger program never loses state.
static const int PM_MAX_VAR_SCOPES = 64;
static const int PM_MAX_LOOPS = 32;
static const int PM_MAX_FUNCTIONS = 128;
static const int PM_MAX_GLOBAL_VARS = 64;
static const int PM_MAX_LAMBDAS = 64;
static const int PM_MAX_TRY_HANDLERS = 16;
static const int PM_MAX_GENERATOR_SLOTS = 32;
static const int PM_MAX_MIR_PARAMS = 16;

// directory of the entry-point script, used for absolute import resolution
// in sub-modules (prevents path doubling for package-internal imports)
static char py_entry_script_dir[512] = "";
static const JubeModuleGraphAPI* py_hosted_module_graph_api = NULL;
static const JubeSourceAPI* py_hosted_source_api = NULL;
static const JubeGuestExecutionAPI* py_hosted_execution_api = NULL;
static const JubeRuntimeCatalogAPI* py_hosted_runtime_catalog_api = NULL;
static thread_local MirEmitter* py_mir_argument_emitter = NULL;

void py_set_hosted_module_graph_api(const JubeModuleGraphAPI* module_graph) {
    py_hosted_module_graph_api = module_graph;
}

void py_set_hosted_source_api(const JubeSourceAPI* source_api) {
    py_hosted_source_api = source_api;
}

void py_set_hosted_execution_api(const JubeGuestExecutionAPI* execution_api) {
    py_hosted_execution_api = execution_api;
}

void py_set_hosted_runtime_catalog_api(const JubeRuntimeCatalogAPI* runtime_catalog) {
    py_hosted_runtime_catalog_api = runtime_catalog;
}

static bool pm_lookup_hosted_import_metadata(const char* name,
        JitImportMetadata* out_metadata) {
    if (!name || !out_metadata || !py_hosted_runtime_catalog_api ||
        !py_hosted_runtime_catalog_api->lookup_import_metadata) return false;
    JubeRuntimeImportMetadata hosted = {};
    if (py_hosted_runtime_catalog_api->lookup_import_metadata(name, &hosted) != 0) {
        return false;
    }
    out_metadata->gc_effect = (JitGcEffect)hosted.gc_effect;
    out_metadata->reentry_effect = (JitReentryEffect)hosted.reentry_effect;
    out_metadata->ret_class = (JitValueClass)hosted.result_class;
    out_metadata->arg_classes = hosted.argument_classes;
    out_metadata->flags = hosted.flags;
    out_metadata->exception_effect = (JitExceptionEffect)hosted.exception_effect;
    out_metadata->arg_effects = hosted.argument_effects;
    return true;
}

// Import lowering can allocate while it traverses a Python namespace. The
// Jube root-frame service keeps that namespace live without exposing the
// active host context to normal Python compilation.
class PmHostedItemRoot {
    PyHostedRootFrame frame_;
    uint64_t* slot_;
    Item fallback_;

public:
    PmHostedItemRoot(Item value) : frame_(1), slot_(frame_.take_slot()), fallback_(value) {
        if (slot_) *slot_ = value.item;
    }

    Item get(void) const {
        return slot_ ? (Item){.item = *slot_} : fallback_;
    }
};

static bool pm_link_hosted_module(void* host_execution, MIR_context_t mir_context) {
    return py_hosted_execution_api &&
        py_hosted_execution_api->api_version == JUBE_HOST_SERVICE_API_VERSION &&
        py_hosted_execution_api->struct_size >= JUBE_GUEST_EXECUTION_API_V1_SIZE &&
        py_hosted_execution_api->execution_link_module &&
        py_hosted_execution_api->execution_link_module(host_execution, mir_context) == 0;
}

static MIR_context_t pm_create_hosted_mir_context(void) {
    if (!py_hosted_execution_api || !py_hosted_execution_api->mir_context_create) return NULL;
    return (MIR_context_t)py_hosted_execution_api->mir_context_create(2);
}

static void pm_destroy_hosted_mir_context(MIR_context_t mir_context) {
    if (!mir_context || !py_hosted_execution_api || !py_hosted_execution_api->mir_context_destroy) return;
    py_hosted_execution_api->mir_context_destroy(mir_context);
}

static MIR_module_t pm_create_hosted_mir_module(MIR_context_t mir_context, const char* module_name) {
    if (!mir_context || !py_hosted_execution_api || !py_hosted_execution_api->mir_module_create) return NULL;
    return (MIR_module_t)py_hosted_execution_api->mir_module_create(mir_context, module_name);
}

static bool pm_finalize_and_load_hosted_mir_module(MIR_context_t mir_context, MIR_module_t mir_module) {
    return mir_context && mir_module && py_hosted_execution_api &&
        py_hosted_execution_api->mir_module_finalize_and_load &&
        py_hosted_execution_api->mir_module_finalize_and_load(mir_context, mir_module) == 0;
}

static void* pm_find_hosted_mir_function(MIR_context_t mir_context, const char* function_name) {
    if (!mir_context || !function_name || !py_hosted_execution_api ||
        !py_hosted_execution_api->mir_function_lookup) return NULL;
    return py_hosted_execution_api->mir_function_lookup(mir_context, function_name);
}

static void pm_finish_hosted_mir_function(MIR_context_t mir_context) {
    if (!mir_context || !py_hosted_execution_api ||
        !py_hosted_execution_api->mir_function_finish) return;
    py_hosted_execution_api->mir_function_finish(mir_context);
}

static bool pm_create_hosted_item_function_typed(MIR_context_t mir_context,
        const char* function_name, int parameter_count,
        const char* const* parameter_names, const uint8_t* parameter_kinds,
        MIR_item_t* out_function_item, MIR_func_t* out_function) {
    if (!mir_context || !function_name || parameter_count < 0 || !out_function_item ||
            !out_function || !parameter_kinds || !py_hosted_execution_api ||
            py_hosted_execution_api->struct_size <
                JUBE_GUEST_EXECUTION_API_H7C_TYPED_FUNCTION_CREATE_SIZE ||
            !py_hosted_execution_api->mir_item_function_create_typed) return false;
    void* function_item = NULL;
    void* function = NULL;
    if (py_hosted_execution_api->mir_item_function_create_typed(mir_context,
            function_name, (uint32_t)parameter_count, parameter_names,
            parameter_kinds, &function_item, &function) != 0) return false;
    *out_function_item = (MIR_item_t)function_item;
    *out_function = (MIR_func_t)function;
    return true;
}

static bool pm_create_hosted_function_forward(MIR_context_t mir_context,
        const char* function_name, MIR_item_t* out_function_item) {
    if (!mir_context || !function_name || !out_function_item ||
        !py_hosted_execution_api ||
        !py_hosted_execution_api->mir_function_forward_create) return false;
    void* function_item = NULL;
    if (py_hosted_execution_api->mir_function_forward_create(mir_context, function_name,
            &function_item) != 0) return false;
    *out_function_item = (MIR_item_t)function_item;
    return true;
}

static bool pm_create_hosted_item_function_proto_typed(MIR_context_t mir_context,
        const char* prototype_name, int parameter_count,
        const uint8_t* parameter_kinds, MIR_item_t* out_prototype_item) {
    if (!mir_context || !prototype_name || parameter_count < 0 ||
            !parameter_kinds || !out_prototype_item || !py_hosted_execution_api ||
            py_hosted_execution_api->struct_size <
                JUBE_GUEST_EXECUTION_API_H7C_TYPED_PROTO_CREATE_SIZE ||
            !py_hosted_execution_api->mir_item_function_proto_create_typed) return false;
    void* prototype_item = NULL;
    if (py_hosted_execution_api->mir_item_function_proto_create_typed(mir_context,
            prototype_name, (uint32_t)parameter_count, parameter_kinds,
            &prototype_item) != 0) return false;
    *out_prototype_item = (MIR_item_t)prototype_item;
    return true;
}

static MIR_reg_t pm_lookup_hosted_function_register(MIR_context_t mir_context,
        MIR_func_t function, const char* register_name) {
    if (!mir_context || !function || !register_name || !py_hosted_execution_api ||
        !py_hosted_execution_api->mir_function_register_lookup) return 0;
    uint32_t register_id = 0;
    if (py_hosted_execution_api->mir_function_register_lookup(mir_context, function,
            register_name, &register_id) != 0) return 0;
    MIR_reg_t reg = (MIR_reg_t)register_id;
    // Parameter binding is the only point where the hosted lowerer receives
    // this identity; retain it for entry-root liveness without a raw lookup.
    em_function_argument_register(py_mir_argument_emitter, reg);
    return reg;
}

static bool pm_activate_hosted_guest_execution(void* host_execution, void** out_input) {
    return py_hosted_execution_api && py_hosted_execution_api->execution_activate &&
        py_hosted_execution_api->execution_activate(host_execution, out_input) == 0;
}

static bool pm_activate_hosted_import_execution(void* host_execution, void** out_input,
                                                bool* out_retained_until_heap_cleanup) {
    return py_hosted_execution_api && py_hosted_execution_api->execution_activate_import &&
        py_hosted_execution_api->execution_activate_import(host_execution, out_input,
            out_retained_until_heap_cleanup) == 0;
}

static bool pm_run_hosted_guest_main(void* host_execution, void* entry_function,
                                     Item* out_result) {
    return py_hosted_execution_api &&
        py_hosted_execution_api->struct_size >=
            JUBE_GUEST_EXECUTION_API_H7C_RUN_MAIN_INTO_SIZE &&
        py_hosted_execution_api->execution_run_main_into &&
        py_hosted_execution_api->execution_run_main_into(host_execution,
            entry_function, out_result) == 0;
}

static void pm_finish_hosted_guest_execution(void* host_execution) {
    if (!py_hosted_execution_api || !py_hosted_execution_api->execution_finish_guest) return;
    py_hosted_execution_api->execution_finish_guest(host_execution);
}

static void* pm_hosted_frame_runtime_slot(void* host_execution) {
    if (!py_hosted_execution_api ||
        py_hosted_execution_api->struct_size < JUBE_GUEST_EXECUTION_API_H7C_RUNTIME_SLOT_SIZE ||
        !py_hosted_execution_api->execution_frame_runtime_slot) {
        return NULL;
    }
    return py_hosted_execution_api->execution_frame_runtime_slot(host_execution);
}

static Item pm_loading_module_namespace(void* host_execution, const char* source_path) {
    Item namespace_obj = ItemNull;
    if (!py_hosted_module_graph_api ||
        py_hosted_module_graph_api->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        py_hosted_module_graph_api->struct_size < JUBE_MODULE_GRAPH_API_V1_SIZE ||
        !py_hosted_module_graph_api->loading_namespace ||
        py_hosted_module_graph_api->loading_namespace(host_execution, source_path, &namespace_obj) != 1) {
        return ItemNull;
    }
    return namespace_obj;
}

static Item pm_load_lambda_module(void* host_execution, const char* source_path) {
    Item namespace_obj = ItemNull;
    if (!py_hosted_module_graph_api ||
        py_hosted_module_graph_api->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        py_hosted_module_graph_api->struct_size < JUBE_MODULE_GRAPH_API_V1_SIZE ||
        !py_hosted_module_graph_api->load_lambda_module ||
        py_hosted_module_graph_api->load_lambda_module(host_execution, source_path, NULL,
                                                        &namespace_obj) != 0) {
        return ItemNull;
    }
    return namespace_obj;
}

// ============================================================================
// Transpiler structs
// ============================================================================

// Python uses the shared lowering scope entry.  `type_id` carries its optional
// native-number evidence while the existing env fields preserve closure access.
typedef VarEntry PyMirVarEntry;
typedef VarScopeEntry PyVarScopeEntry;

struct PyLoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
};

struct PyFuncCollected {
    PyFunctionDefNode* node;
    FnAnalysis* analysis;
    PyFnExt* ext;
    char name[128];
    MIR_item_t func_item;
    int param_count;
    int required_count;  // params without defaults
    int default_count;   // params with defaults
    // capture info for closures
    char captures[32][128];  // variable names captured (with _py_ prefix)
    bool capture_is_nonlocal[32]; // true if this capture was declared nonlocal
    int capture_count;
    int parent_index;    // index in func_entries of parent function (-1 if top-level)
    bool is_closure;     // true if this function captures variables
    // shared env for nonlocal children
    char shared_env_names[32][128]; // vars to put in shared env (with _py_ prefix)
    int shared_env_count;           // number of shared env slots
    bool has_nonlocal_children;     // true if any child uses nonlocal
    // global declarations
    char global_names[16][128];     // vars declared global (with _py_ prefix)
    int global_name_count;
    // *args variadic parameter
    bool has_star_args;             // true if function has *args parameter
    char star_args_name[128];       // name of *args parameter (with _py_ prefix)
    int star_args_index;            // index of *args in param list (regular params before it)
    // **kwargs variadic keyword parameter
    bool has_kwargs;                // true if function has **kwargs parameter
    char kwargs_name[128];          // name of **kwargs variable (with _py_ prefix)
    // class method info
    bool is_method;                 // true if this function is a class method
    char class_name[128];           // name of the class this method belongs to (without _py_ prefix)
    int class_depth;                // nesting depth of class within which method is defined (top=0)
    // generator flag
    bool is_generator;              // true if body contains any yield / yield from
    bool is_async;                  // true if declared as async def (Phase D)
};

struct PyLambdaCollected {
    PyLambdaNode* node;
    FnAnalysis* analysis;
    PyFnExt* ext;
    char name[64];
    MIR_item_t func_item;
    int param_count;
    char captures[32][128];
    int capture_count;
    int parent_index;    // index in func_entries of enclosing function (-1 if top-level)
    bool is_closure;
};

struct PyMirTranspiler {
    PyTranspiler* tp;

    MirEmitter em;
    MIR_module_t module;
    // Local function items
    struct hashmap* local_funcs;

    // Variable scopes: array of hashmaps
    struct hashmap* var_scopes[PM_MAX_VAR_SCOPES];
    int scope_depth;

    // Loop label stack
    PyLoopLabels loop_stack[PM_MAX_LOOPS];
    int loop_depth;

    // Collected functions
    PyFuncCollected func_entries[PM_MAX_FUNCTIONS];
    int func_count;

    // Module vars
    int module_var_count;
    // Global variable name → module var index mapping
    char global_var_names[PM_MAX_GLOBAL_VARS][128];  // variable names (with _py_ prefix)
    int global_var_indices[PM_MAX_GLOBAL_VARS];      // module var index for each
    int global_var_count;

    bool in_main;

    // Lambda counter
    int lambda_count;

    // Collected lambdas
    PyLambdaCollected lambda_entries[PM_MAX_LAMBDAS];

    // Scope env for closures
    MIR_reg_t scope_env_reg;
    int scope_env_slot_count;
    int current_func_index;

    // Try/except stack
    int try_depth;
    MIR_label_t try_handler_labels[PM_MAX_TRY_HANDLERS];

    // Source filename for relative import resolution
    const char* filename;

    // Opaque host execution token for cross-language module loading. The
    // lowering layer never needs the Runtime record itself.
    void* host_execution;
    // Host-owned address of the active runtime token. It is valid only while
    // the matching guest activation remains active.
    void* frame_runtime_slot;

    // ---- TCO (Tail Call Optimization) state ----
    PyFuncCollected* tco_func;      // current function being TCO'd (NULL if not TCO)
    MIR_label_t tco_label;          // jump target at top of function body
    MIR_reg_t tco_count_reg;        // iteration counter for overflow guard
    bool in_tail_position;          // true when current expression is in tail position
    bool block_returned;            // set when TCO jump replaces a return

    // ---- Generator compilation state (valid during pm_compile_generator) ----
    bool in_generator;              // true while compiling a generator resume function
    bool in_async;                  // true while compiling an async def (Phase D)
    MIR_reg_t gen_frame_reg;        // MIR reg holding the _gen_frame pointer parameter
    MIR_reg_t gen_sent_reg;         // MIR reg holding the _gen_sent parameter
    int gen_yield_count;            // yield points emitted so far (0-based)
    MIR_label_t gen_yield_labels[PM_MAX_GENERATOR_SLOTS]; // pre-created labels: gen_yield_labels[i] = L_resume_(i+1)
    char gen_local_names[PM_MAX_GENERATOR_SLOTS][128];  // local var names tracked in frame (with _py_ prefix)
    int  gen_local_frame_slots[PM_MAX_GENERATOR_SLOTS]; // frame slot index for each gen_local_names[i]
    int  gen_local_count;           // number of entries
    int  gen_iter_count;            // counter for auto-named iterator vars (_py__git_N)

    // Shared-emitter call-path telemetry, populated while lowering this module.
    int mir_call_count;
    int mir_may_gc_call_count;
    int mir_item_result_count;
    bool has_compile_error;
};

static thread_local PyMirTranspiler* py_mir_telemetry_owner = NULL;

static void pm_select_hosted_function(PyMirTranspiler* mt, MIR_item_t function_item,
        MIR_func_t function) {
    if (!mt) return;
    mt->em.func_item = function_item;
    mt->em.func = function;
    py_mir_argument_emitter = &mt->em;
    em_function_arguments_clear(&mt->em);
}

static void pm_restore_hosted_function(PyMirTranspiler* mt, MIR_item_t function_item,
        MIR_func_t function, MirFunctionArgumentState arguments) {
    if (!mt) return;
    mt->em.func_item = function_item;
    mt->em.func = function;
    py_mir_argument_emitter = &mt->em;
    em_function_arguments_restore(&mt->em, arguments);
}

static bool pm_require_capacity(PyMirTranspiler* mt, int used, int capacity,
        const char* subject) {
    if (used < capacity) return true;
    // a dropped entry changes Python program meaning; abort this lowering instead.
    log_error("py-mir-cap: %s exceeds compiler limit %d", subject, capacity);
    mt->has_compile_error = true;
    return false;
}

static bool pm_require_count(PyMirTranspiler* mt, int count, int maximum,
        const char* subject) {
    return pm_require_capacity(mt, count - 1, maximum, subject);
}

// ============================================================================
// Hashmap helpers
// ============================================================================

struct PyLocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
};
HASHMAP_DEFINE_STRKEY(py_local_func, struct PyLocalFuncEntry, name)

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_reg_t pm_transpile_expression(PyMirTranspiler* mt, PyAstNode* expr);
static void pm_transpile_statement(PyMirTranspiler* mt, PyAstNode* stmt);
static MIR_reg_t pm_call_0(PyMirTranspiler* mt, const char* fn_name,
                           MIR_type_t ret_type);
static void pm_call_void_1(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1);

// ============================================================================
// Basic MIR helpers
// ============================================================================

static uint8_t pm_hosted_value_kind(MIR_type_t type) {
    if (type == MIR_T_I64) return JUBE_COMPILER_VALUE_I64;
    if (type == MIR_T_D) return JUBE_COMPILER_VALUE_F64;
    if (type == MIR_T_P) return JUBE_COMPILER_VALUE_POINTER;
    log_error("py-mir: unsupported hosted compiler value type %d", (int)type);
    abort();
}

static MIR_reg_t pm_new_reg(PyMirTranspiler* mt, const char* prefix, MIR_type_t type) {
    if (!mt || !mt->em.func || !prefix || !*prefix || !py_hosted_execution_api ||
            py_hosted_execution_api->struct_size <
                JUBE_GUEST_EXECUTION_API_H7C_REGISTER_CREATE_SIZE ||
            !py_hosted_execution_api->mir_function_register_create) {
        log_error("py-mir: hosted compiler cannot allocate a register");
        abort();
    }
    uint32_t register_id = 0;
    if (py_hosted_execution_api->mir_function_register_create(mt->em.ctx, mt->em.func,
            &mt->em.reg_counter, prefix, pm_hosted_value_kind(type), &register_id) != 0 ||
            register_id == 0) {
        log_error("py-mir: host rejected register allocation");
        abort();
    }
    return (MIR_reg_t)register_id;
}

static MIR_label_t pm_new_label(PyMirTranspiler* mt) {
    if (!mt || !py_hosted_execution_api ||
            py_hosted_execution_api->struct_size <
                JUBE_GUEST_EXECUTION_API_H7C_LABEL_CREATE_SIZE ||
            !py_hosted_execution_api->mir_label_create) {
        log_error("py-mir: hosted compiler cannot allocate a label");
        abort();
    }
    void* label = NULL;
    if (py_hosted_execution_api->mir_label_create(mt->em.ctx, &label) != 0 || !label) {
        log_error("py-mir: host rejected label allocation");
        abort();
    }
    return (MIR_label_t)label;
}

static void pm_emit_hosted_instruction(PyMirTranspiler* mt,
        const JubeCompilerInstruction* instruction) {
    if (!mt || !mt->em.func_item || !instruction || !py_hosted_execution_api ||
            py_hosted_execution_api->struct_size <
                JUBE_GUEST_EXECUTION_API_H7C_INSTRUCTION_EMIT_SIZE ||
            !py_hosted_execution_api->mir_instruction_emit ||
            py_hosted_execution_api->mir_instruction_emit(mt->em.ctx,
                mt->em.func_item, instruction) != 0) {
        log_error("py-mir: host rejected compiler instruction");
        abort();
    }
}

static void pm_emit_i64_immediate(PyMirTranspiler* mt, MIR_reg_t destination,
        int64_t value) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_MOVE_I64_IMMEDIATE;
    instruction.destination_register = (uint32_t)destination;
    instruction.immediate_i64 = value;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_f64_immediate(PyMirTranspiler* mt, MIR_reg_t destination,
        double value) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_MOVE_F64_IMMEDIATE;
    instruction.destination_register = (uint32_t)destination;
    instruction.immediate_f64 = value;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_i64_register_move(PyMirTranspiler* mt, MIR_reg_t destination,
        MIR_reg_t source) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_MOVE_I64_REGISTER;
    instruction.destination_register = (uint32_t)destination;
    instruction.source_register = (uint32_t)source;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_jump(PyMirTranspiler* mt, MIR_label_t target) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_JUMP;
    instruction.target_label = (void*)target;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_branch_true(PyMirTranspiler* mt, MIR_reg_t condition,
        MIR_label_t target) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_BRANCH_TRUE;
    instruction.source_register = (uint32_t)condition;
    instruction.target_label = (void*)target;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_branch_false(PyMirTranspiler* mt, MIR_reg_t condition,
        MIR_label_t target) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_BRANCH_FALSE;
    instruction.source_register = (uint32_t)condition;
    instruction.target_label = (void*)target;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_branch_not_equal_i64_immediate(PyMirTranspiler* mt,
        MIR_reg_t condition, int64_t value, MIR_label_t target) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_BRANCH_NOT_EQUAL_I64_IMMEDIATE;
    instruction.source_register = (uint32_t)condition;
    instruction.immediate_i64 = value;
    instruction.target_label = (void*)target;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_branch_greater_equal_i64_immediate(PyMirTranspiler* mt,
        MIR_reg_t condition, int64_t value, MIR_label_t target) {
    JubeCompilerInstruction instruction = {};
    instruction.opcode = JUBE_COMPILER_INSN_BRANCH_GREATER_EQUAL_I64_IMMEDIATE;
    instruction.source_register = (uint32_t)condition;
    instruction.immediate_i64 = value;
    instruction.target_label = (void*)target;
    pm_emit_hosted_instruction(mt, &instruction);
}

static void pm_emit_raw(PyMirTranspiler* mt, MIR_insn_t insn) {
    em_emit_insn(&mt->em, insn);
}

static void pm_emit(PyMirTranspiler* mt, MIR_insn_t insn) {
    if (mt->em.frame.active && insn->code == MIR_RET) {
        if (insn->nops != 1) {
            log_error("py-mir-frame: expected one return operand, got %u", insn->nops);
            pm_emit_raw(mt, insn);
            return;
        }
        pm_emit_raw(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, mt->em.frame.return_reg), insn->ops[0]));
        pm_emit_raw(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
            MIR_new_label_op(mt->em.ctx, mt->em.frame.return_label)));
        _MIR_free_insn(mt->em.ctx, insn);
        return;
    }
    em_emit_insn(&mt->em, insn);
}

static void pm_emit_local_direct_call(PyMirTranspiler* mt, const char* function_name,
                                      int operand_count, MIR_op_t* operands) {
    // A resolved local function borrows its argument frame for this synchronous
    // call; treating it as unknown would reject valid caller-owned scalar homes.
    em_emit_borrowed_call(&mt->em, function_name,
        MIR_new_insn_arr(mt->em.ctx, MIR_CALL, operand_count, operands));
}

static void pm_emit_label(PyMirTranspiler* mt, MIR_label_t label) {
    if (!mt || !mt->em.func_item || !label || !py_hosted_execution_api ||
            py_hosted_execution_api->struct_size <
                JUBE_GUEST_EXECUTION_API_H7C_LABEL_EMIT_SIZE ||
            !py_hosted_execution_api->mir_label_emit ||
            py_hosted_execution_api->mir_label_emit(mt->em.ctx,
                mt->em.func_item, (void*)label) != 0) {
        log_error("py-mir: host rejected label emission");
        abort();
    }
}

static MIR_reg_t pm_load_side_stack_runtime(PyMirTranspiler* mt) {
    if (!mt || !mt->frame_runtime_slot || !mt->em.func_item || !mt->em.func ||
            !py_hosted_execution_api ||
            py_hosted_execution_api->struct_size <
                JUBE_GUEST_EXECUTION_API_H7C_FRAME_RUNTIME_LOAD_SIZE ||
            !py_hosted_execution_api->mir_function_frame_runtime_load) {
        log_error("py-mir: hosted compiler cannot load the active frame runtime");
        abort();
    }
    uint32_t runtime_register = 0;
    if (py_hosted_execution_api->mir_function_frame_runtime_load(mt->em.ctx,
            mt->em.func_item, mt->em.func, mt->frame_runtime_slot,
            &runtime_register) != 0 || runtime_register == 0) {
        log_error("py-mir: host rejected the active frame runtime load");
        abort();
    }
    return (MIR_reg_t)runtime_register;
}

static void pm_emit_side_stack_overflow(PyMirTranspiler* mt) {
    if (!mt) return;
    // The shared import cache owns the module-global prototype; hand-written
    // prototypes here would redeclare the same ABI contract per Python frame.
    pm_call_void_1(mt, "lambda_stack_overflow_error", MIR_T_P,
        MIR_new_int_op(mt->em.ctx, (int64_t)(uintptr_t)"py-side-stack"));
}

static void pm_root_call_value(void* owner, MIR_reg_t reg) {
    PyMirTranspiler* mt = (PyMirTranspiler*)owner;
    if (!mt || !mt->em.frame.active || !reg) return;
    if (!em_root_note_candidate(&mt->em.frame.gc_candidates,
            &mt->em.frame.gc_candidate_count, &mt->em.frame.gc_candidate_capacity,
            &mt->em.frame.gc_candidate_by_reg,
            &mt->em.frame.gc_candidate_by_reg_capacity, reg,
            JIT_VALUE_UNKNOWN, 0)) {
        log_error("py-mir-frame: unable to record root candidate reg=%u",
            (unsigned)reg);
        abort();
    }
}

static void pm_note_mir_call(const char* name) {
    (void)name;
    if (py_mir_telemetry_owner) py_mir_telemetry_owner->mir_call_count++;
}

static void pm_before_may_gc_call(void* owner) {
    PyMirTranspiler* mt = (PyMirTranspiler*)owner;
    if (mt) mt->mir_may_gc_call_count++;
}

static void pm_after_call_result(void* owner, MIR_reg_t reg, MIR_type_t type) {
    PyMirTranspiler* mt = (PyMirTranspiler*)owner;
    if (mt && reg && type == MIR_T_I64) mt->mir_item_result_count++;
}

static MIR_reg_t pm_call_1(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1);

// Python Item values have no static type proof at lowering time, so the shared
// liveness pass owns their exact root slots instead of trusting local hints.
static void pm_begin_function_frame(PyMirTranspiler* mt, MIR_reg_t runtime) {
    if (!mt) return;
    em_frame_dispose(&mt->em);
    mt->em.frame.return_type = MIR_T_I64;
    mt->em.frame.item_return = true;
    mt->em.frame.scalar_return_mode = MIR_SCALAR_RETURN_DYNAMIC;
    mt->em.frame.runtime = runtime;
    mt->em.frame.root_base = pm_new_reg(mt, "py_root_frame", MIR_T_I64);
    mt->em.frame.number_base = pm_new_reg(mt, "py_number_frame", MIR_T_I64);
    mt->em.frame.anchor = pm_new_label(mt);
    mt->em.frame.return_label = pm_new_label(mt);
    mt->em.frame.return_reg = pm_new_reg(mt, "py_return_value", MIR_T_I64);
    mt->em.frame.plan.entry_kind = FN_ENTRY_PUBLIC_WRAPPER;
    mt->em.frame.plan.entry_mode = MIR_ENTRY_CHECKED;
    mt->em.frame.active = true;
    pm_emit_label(mt, mt->em.frame.anchor);
}

static void pm_enable_scalar_return_home(PyMirTranspiler* mt,
        const char* home_name) {
    if (!mt || !home_name) return;
    MIR_reg_t home = pm_lookup_hosted_function_register(mt->em.ctx,
        mt->em.func, home_name);
    if (!home) {
        log_error("py-mir: missing required scalar return home '%s'", home_name);
        abort();
    }
    // Public Python frames must hand scalar payloads to their caller before
    // popping the number stack; a fallback GC scalar cell is forbidden.
    mt->em.frame.incoming_scalar_home = home;
    mt->em.frame.plan.scalar_home_lane_mask = FN_RETURN_HOME_NORMAL;
    mt->em.frame.plan.accepts_caller_scalar_home = true;
}

static void pm_finish_function_frame(PyMirTranspiler* mt, const char* name) {
    if (!mt || !mt->em.frame.active) return;
    mt->em.frame.plan.debug_name = name;
    pm_emit_label(mt, mt->em.frame.return_label);

    MirRootWriteBackResult roots = {};
    em_finalize_semantic_root_write_back(&mt->em, mt->em.frame.root_base,
        mt->em.frame.anchor, false, 0, &mt->em.frame.gc_candidates,
        &mt->em.frame.gc_candidate_count, &mt->em.frame.gc_candidate_capacity,
        &mt->em.frame.gc_candidate_by_reg,
        &mt->em.frame.gc_candidate_by_reg_capacity, mt->em.frame.gc_call_sites,
        mt->em.frame.gc_call_site_count, &roots, name);
    mt->em.frame.root_slot_count = roots.stable_slots + roots.scratch_slots;
    mt->em.frame.root_store_count = roots.inserted_stores;
    em_finalize_scalar_homes(&mt->em);

    if (mt->em.frame.item_return) {
        MIR_reg_t returned = mt->em.frame.return_reg;
        if (!mt->em.frame.incoming_scalar_home) {
            log_error("py-mir: public Item return has no scalar home");
            abort();
        }
        returned = em_adopt_scalar_item(&mt->em,
            mt->em.frame.scalar_return_mode, returned,
            mt->em.frame.runtime, offsetof(Context, side_number_top),
            mt->em.frame.number_base, mt->em.frame.incoming_scalar_home);
        if (returned != mt->em.frame.return_reg) {
            pm_emit_i64_register_move(mt, mt->em.frame.return_reg, returned);
        }
    } else {
        em_store_frame_top(&mt->em, mt->em.frame.runtime,
            offsetof(Context, side_number_top), mt->em.frame.number_base);
    }
    if (mt->em.frame.root_slot_count > 0) {
        em_store_frame_top(&mt->em, mt->em.frame.runtime,
            offsetof(Context, side_root_top), mt->em.frame.root_base);
    }
    pm_emit_raw(mt, MIR_new_ret_insn(mt->em.ctx, 1,
        MIR_new_reg_op(mt->em.ctx, mt->em.frame.return_reg)));

    em_finalize_frame_prologue(&mt->em, mt->em.frame.plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit));
    mt->em.frame.active = false;
    pm_emit_side_stack_overflow(mt);
    pm_emit_raw(mt, MIR_new_ret_insn(mt->em.ctx, 1,
        MIR_new_uint_op(mt->em.ctx, PY_ITEM_NULL_VAL)));
    em_finalize_function_metadata(&mt->em);
    em_frame_dispose(&mt->em);
}

// ============================================================================
// Call helpers
// ============================================================================

static MIR_reg_t pm_call_0(PyMirTranspiler* mt, const char* fn_name, MIR_type_t ret_type) {
    return em_call_0(&mt->em, fn_name, ret_type, true);
}

static MIR_reg_t pm_call_1(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1) {
    return em_call_1(&mt->em, fn_name, ret_type, a1t, a1, true);
}

static MIR_reg_t pm_call_2(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2) {
    return em_call_2(&mt->em, fn_name, ret_type, a1t, a1, a2t, a2, true);
}

static MIR_reg_t pm_call_3(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3) {
    return em_call_3(&mt->em, fn_name, ret_type, a1t, a1, a2t, a2, a3t, a3, true);
}

static MIR_reg_t pm_call_4(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4) {
    return em_call_4(&mt->em, fn_name, ret_type,
        a1t, a1, a2t, a2, a3t, a3, a4t, a4, true);
}

static MIR_reg_t pm_call_5(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5) {
    return em_call_5(&mt->em, fn_name, ret_type,
        a1t, a1, a2t, a2, a3t, a3, a4t, a4, a5t, a5, true);
}

static void pm_call_void_1(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1) {
    em_call_void_1(&mt->em, fn_name, a1t, a1, true);
}

static void pm_call_void_2(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    em_call_void_2(&mt->em, fn_name, a1t, a1, a2t, a2, true);
}

static void pm_call_void_3(PyMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3) {
    MIR_type_t types[] = {a1t, a2t, a3t};
    MIR_op_t ops[] = {a1, a2, a3};
    em_call_void_with_args(&mt->em, fn_name, 3, types, ops, true);
}

struct PmArgScope {
    MIR_reg_t mark;
    MIR_reg_t args;
};

static PmArgScope pm_begin_arg_scope(PyMirTranspiler* mt, int count) {
    PmArgScope scope = {};
    scope.mark = pm_call_0(mt, "py_args_save", MIR_T_I64);
    scope.args = pm_call_1(mt, "py_args_push", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, count));
    return scope;
}

static void pm_arg_scope_store(PyMirTranspiler* mt, PmArgScope scope,
        int index, MIR_reg_t value) {
    pm_call_void_3(mt, "py_args_store",
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, scope.args),
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, index),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, value));
}

static void pm_end_arg_scope(PyMirTranspiler* mt, PmArgScope scope) {
    pm_call_void_1(mt, "py_args_restore", MIR_T_I64,
        MIR_new_reg_op(mt->em.ctx, scope.mark));
}

// ============================================================================
// Scope management
// ============================================================================

static void pm_push_scope(PyMirTranspiler* mt) {
    if (!pm_require_capacity(mt, mt->scope_depth + 1, PM_MAX_VAR_SCOPES,
            "nested variable scopes")) return;
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = em_var_scope_new(16);
}

static void pm_pop_scope(PyMirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("py-mir: scope underflow"); return; }
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

static void pm_set_var(PyMirTranspiler* mt, const char* name, MIR_reg_t reg) {
    PyVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.mir_type = MIR_T_I64;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static void pm_set_var_with_type(PyMirTranspiler* mt, const char* name, MIR_reg_t reg, TypeId type_hint) {
    PyVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.mir_type = MIR_T_I64;
    entry.var.type_id = type_hint;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static void pm_set_env_var(PyMirTranspiler* mt, const char* name, MIR_reg_t env_reg, int slot) {
    PyVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.from_env = true;
    entry.var.env_reg = env_reg;
    entry.var.env_slot = slot;
    entry.var.mir_type = MIR_T_I64;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static MIR_reg_t pm_load_env_slot(PyMirTranspiler* mt, MIR_reg_t env_reg, int slot) {
    return pm_call_2(mt, "py_env_load", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, env_reg),
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, slot));
}

static void pm_store_env_slot(PyMirTranspiler* mt, MIR_reg_t env_reg, int slot, MIR_reg_t val_reg) {
    pm_call_void_3(mt, "py_env_store",
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, env_reg),
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, slot),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val_reg));
}

static PyMirVarEntry* pm_find_var(PyMirTranspiler* mt, const char* name) {
    PyVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        PyVarScopeEntry* found = (PyVarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) return &found->var;
    }
    return NULL;
}

// ============================================================================
// Boxing helpers
// ============================================================================

static MIR_reg_t pm_emit_null(PyMirTranspiler* mt) {
    MIR_reg_t r = pm_new_reg(mt, "null", MIR_T_I64);
    pm_emit_i64_immediate(mt, r, (int64_t)PY_ITEM_NULL_VAL);
    return r;
}

static MIR_reg_t pm_box_int_const(PyMirTranspiler* mt, int64_t value) {
    MIR_reg_t r = pm_new_reg(mt, "boxi", MIR_T_I64);
    uint64_t tagged = PY_ITEM_INT_TAG | ((uint64_t)value & PY_MASK56);
    pm_emit_i64_immediate(mt, r, (int64_t)tagged);
    return r;
}

static MIR_reg_t pm_box_float(PyMirTranspiler* mt, MIR_reg_t d_reg) {
    return pm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->em.ctx, d_reg));
}

static MIR_reg_t pm_box_string_literal(PyMirTranspiler* mt, const char* str, int len) {
    // Module JIT code outlives the parser pool, so embedding AST-owned chars
    // leaves imported functions with dangling literal pointers after cleanup.
    String* literal = heap_create_name(str, (size_t)len);
    MIR_reg_t result = pm_new_reg(mt, "str_literal", MIR_T_I64);
    pm_emit_i64_immediate(mt, result, (int64_t)s2it(literal));
    return result;
}

static MIR_reg_t pm_emit_bool(PyMirTranspiler* mt, bool value) {
    MIR_reg_t r = pm_new_reg(mt, "bool", MIR_T_I64);
    uint64_t bval = value ? PY_ITEM_TRUE_VAL : PY_ITEM_FALSE_VAL;
    pm_emit_i64_immediate(mt, r, (int64_t)bval);
    return r;
}

// ============================================================================
// Native type inference and inline arithmetic (Phase 1-3 optimizations)
// ============================================================================

// forward declaration
static MIR_reg_t pm_transpile_expression(PyMirTranspiler* mt, PyAstNode* expr);

// infer the effective type of an AST expression without emitting code
static TypeId pm_get_effective_type(PyMirTranspiler* mt, PyAstNode* expr) {
    if (!expr) return LMD_TYPE_NULL;

    switch (expr->node_type) {
    case PY_AST_NODE_LITERAL: {
        PyLiteralNode* lit = (PyLiteralNode*)expr;
        if (lit->literal_type == PY_LITERAL_INT && !lit->is_bigint_literal) return LMD_TYPE_INT;
        if (lit->literal_type == PY_LITERAL_FLOAT) return LMD_TYPE_FLOAT;
        if (lit->literal_type == PY_LITERAL_BOOLEAN) return LMD_TYPE_BOOL;
        return LMD_TYPE_NULL;
    }
    case PY_AST_NODE_IDENTIFIER: {
        PyIdentifierNode* id = (PyIdentifierNode*)expr;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var && var->type_id) return var->type_id;
        return LMD_TYPE_NULL;
    }
    case PY_AST_NODE_BINARY_OP: {
        PyBinaryNode* bin = (PyBinaryNode*)expr;
        TypeId lt = pm_get_effective_type(mt, bin->left);
        TypeId rt = pm_get_effective_type(mt, bin->right);
        if (bin->op == PY_OP_DIV) return LMD_TYPE_FLOAT; // true division always yields float
        if (lt == LMD_TYPE_FLOAT || rt == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
        if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) return LMD_TYPE_INT;
        return LMD_TYPE_NULL;
    }
    case PY_AST_NODE_UNARY_OP: {
        PyUnaryNode* un = (PyUnaryNode*)expr;
        if (un->op == PY_OP_NEGATE || un->op == PY_OP_POSITIVE || un->op == PY_OP_BIT_NOT)
            return pm_get_effective_type(mt, un->operand);
        return LMD_TYPE_NULL;
    }
    case PY_AST_NODE_CALL: {
        // len() returns int
        PyCallNode* call = (PyCallNode*)expr;
        if (call->function && call->function->node_type == PY_AST_NODE_IDENTIFIER) {
            PyIdentifierNode* fn_id = (PyIdentifierNode*)call->function;
            if (fn_id->name->len == 3 && strncmp(fn_id->name->chars, "len", 3) == 0)
                return LMD_TYPE_INT;
            if (fn_id->name->len == 3 && strncmp(fn_id->name->chars, "int", 3) == 0)
                return LMD_TYPE_INT;
            if (fn_id->name->len == 5 && strncmp(fn_id->name->chars, "float", 5) == 0)
                return LMD_TYPE_FLOAT;
            if (fn_id->name->len == 3 && strncmp(fn_id->name->chars, "abs", 3) == 0)
                return pm_get_effective_type(mt, call->arguments);
        }
        return LMD_TYPE_NULL;
    }
    default:
        return LMD_TYPE_NULL;
    }
}

// emit MIR code to produce an unboxed int64 from a boxed Item register
static MIR_reg_t pm_emit_unbox_int(PyMirTranspiler* mt, MIR_reg_t item) {
    MIR_reg_t result = pm_new_reg(mt, "ubi", MIR_T_I64);
    // shift left 8 to discard tag, arithmetic shift right 8 for sign extension
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_LSH, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, item), MIR_new_int_op(mt->em.ctx, 8)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_RSH, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, result), MIR_new_int_op(mt->em.ctx, 8)));
    return result;
}

// box an unboxed int64 register back to a tagged Item, with INT56 range check
static MIR_reg_t pm_box_int_reg(PyMirTranspiler* mt, MIR_reg_t val) {
    int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
    int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;

    MIR_reg_t result = pm_new_reg(mt, "bxi", MIR_T_I64);
    MIR_reg_t masked = pm_new_reg(mt, "mask", MIR_T_I64);
    MIR_reg_t tagged = pm_new_reg(mt, "tag", MIR_T_I64);
    MIR_reg_t le_max = pm_new_reg(mt, "le", MIR_T_I64);
    MIR_reg_t ge_min = pm_new_reg(mt, "ge", MIR_T_I64);
    MIR_reg_t in_range = pm_new_reg(mt, "rng", MIR_T_I64);

    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_LE, MIR_new_reg_op(mt->em.ctx, le_max),
        MIR_new_reg_op(mt->em.ctx, val), MIR_new_int_op(mt->em.ctx, INT56_MAX_VAL)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_GE, MIR_new_reg_op(mt->em.ctx, ge_min),
        MIR_new_reg_op(mt->em.ctx, val), MIR_new_int_op(mt->em.ctx, INT56_MIN_VAL)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_AND, MIR_new_reg_op(mt->em.ctx, in_range),
        MIR_new_reg_op(mt->em.ctx, le_max), MIR_new_reg_op(mt->em.ctx, ge_min)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_AND, MIR_new_reg_op(mt->em.ctx, masked),
        MIR_new_reg_op(mt->em.ctx, val), MIR_new_int_op(mt->em.ctx, (int64_t)PY_MASK56)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_OR, MIR_new_reg_op(mt->em.ctx, tagged),
        MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_INT_TAG), MIR_new_reg_op(mt->em.ctx, masked)));

    MIR_label_t l_ok = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    pm_emit_branch_true(mt, in_range, l_ok);
    // Native arithmetic reaches here only after producing a valid int64; it
    // must become a Python bigint instead of discarding the computed value.
    MIR_reg_t promoted = pm_call_1(mt, "py_bigint_from_int64", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, promoted)));
    pm_emit_jump(mt, l_end);
    pm_emit_label(mt, l_ok);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, tagged)));
    pm_emit_label(mt, l_end);
    return result;
}

// emit code to produce an unboxed int64 from an expression (literal → direct, var → unbox)
static MIR_reg_t pm_transpile_as_native_int(PyMirTranspiler* mt, PyAstNode* expr) {
    if (!expr) {
        MIR_reg_t z = pm_new_reg(mt, "zero", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, z),
            MIR_new_int_op(mt->em.ctx, 0)));
        return z;
    }
    // integer literal → emit constant directly
    if (expr->node_type == PY_AST_NODE_LITERAL) {
        PyLiteralNode* lit = (PyLiteralNode*)expr;
        if (lit->literal_type == PY_LITERAL_INT && !lit->is_bigint_literal) {
            MIR_reg_t r = pm_new_reg(mt, "ilit", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, r),
                MIR_new_int_op(mt->em.ctx, lit->value.int_value)));
            return r;
        }
        if (lit->literal_type == PY_LITERAL_BOOLEAN) {
            MIR_reg_t r = pm_new_reg(mt, "blit", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, r),
                MIR_new_int_op(mt->em.ctx, lit->value.boolean_value ? 1 : 0)));
            return r;
        }
    }
    // identifier with known INT type → unbox from register
    if (expr->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)expr;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var && var->type_id == LMD_TYPE_INT) {
            return pm_emit_unbox_int(mt, var->reg);
        }
    }
    // binary expression of int op int → recurse natively
    if (expr->node_type == PY_AST_NODE_BINARY_OP) {
        PyBinaryNode* bin = (PyBinaryNode*)expr;
        TypeId lt = pm_get_effective_type(mt, bin->left);
        TypeId rt = pm_get_effective_type(mt, bin->right);
        if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
            MIR_reg_t l = pm_transpile_as_native_int(mt, bin->left);
            MIR_reg_t r = pm_transpile_as_native_int(mt, bin->right);
            MIR_insn_code_t op;
            switch (bin->op) {
            case PY_OP_ADD: op = MIR_ADD; break;
            case PY_OP_SUB: op = MIR_SUB; break;
            case PY_OP_MUL: op = MIR_MUL; break;
            case PY_OP_FLOOR_DIV: op = MIR_DIV; break;
            case PY_OP_MOD: op = MIR_MOD; break;
            case PY_OP_LSHIFT: op = MIR_LSH; break;
            case PY_OP_RSHIFT: op = MIR_RSH; break;
            case PY_OP_BIT_AND: op = MIR_AND; break;
            case PY_OP_BIT_OR: op = MIR_OR; break;
            case PY_OP_BIT_XOR: op = MIR_XOR; break;
            default: goto fallback;
            }
            MIR_reg_t res = pm_new_reg(mt, "ni", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, op,
                MIR_new_reg_op(mt->em.ctx, res),
                MIR_new_reg_op(mt->em.ctx, l),
                MIR_new_reg_op(mt->em.ctx, r)));
            return res;
        }
    }
    // unary negate on int
    if (expr->node_type == PY_AST_NODE_UNARY_OP) {
        PyUnaryNode* un = (PyUnaryNode*)expr;
        TypeId ot = pm_get_effective_type(mt, un->operand);
        if (ot == LMD_TYPE_INT && un->op == PY_OP_NEGATE) {
            MIR_reg_t inner = pm_transpile_as_native_int(mt, un->operand);
            MIR_reg_t res = pm_new_reg(mt, "neg", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_NEG,
                MIR_new_reg_op(mt->em.ctx, res),
                MIR_new_reg_op(mt->em.ctx, inner)));
            return res;
        }
    }
fallback:;
    // fallback: transpile as boxed then unbox
    MIR_reg_t boxed = pm_transpile_expression(mt, expr);
    return pm_emit_unbox_int(mt, boxed);
}

// emit code to produce an unboxed double from an expression
static MIR_reg_t pm_transpile_as_native_float(PyMirTranspiler* mt, PyAstNode* expr) {
    if (!expr) {
        MIR_reg_t z = pm_new_reg(mt, "fzero", MIR_T_D);
        pm_emit_f64_immediate(mt, z, 0.0);
        return z;
    }
    // float literal → emit constant directly
    if (expr->node_type == PY_AST_NODE_LITERAL) {
        PyLiteralNode* lit = (PyLiteralNode*)expr;
        if (lit->literal_type == PY_LITERAL_FLOAT) {
            MIR_reg_t r = pm_new_reg(mt, "dlit", MIR_T_D);
            pm_emit_f64_immediate(mt, r, lit->value.float_value);
            return r;
        }
        // int literal → convert to double
        if (lit->literal_type == PY_LITERAL_INT && !lit->is_bigint_literal) {
            MIR_reg_t i = pm_new_reg(mt, "i2f", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, i),
                MIR_new_int_op(mt->em.ctx, lit->value.int_value)));
            MIR_reg_t r = pm_new_reg(mt, "dlit", MIR_T_D);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_I2D, MIR_new_reg_op(mt->em.ctx, r),
                MIR_new_reg_op(mt->em.ctx, i)));
            return r;
        }
    }
    // identifier with known FLOAT type → unbox
    if (expr->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)expr;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var && var->type_id == LMD_TYPE_FLOAT) {
            return pm_call_1(mt, "it2d", MIR_T_D,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, var->reg));
        }
        // int var used in float context → unbox int then convert
        if (var && var->type_id == LMD_TYPE_INT) {
            MIR_reg_t i = pm_emit_unbox_int(mt, var->reg);
            MIR_reg_t d = pm_new_reg(mt, "i2d", MIR_T_D);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_I2D, MIR_new_reg_op(mt->em.ctx, d),
                MIR_new_reg_op(mt->em.ctx, i)));
            return d;
        }
    }
    // fallback: transpile as boxed then call it2d
    MIR_reg_t boxed = pm_transpile_expression(mt, expr);
    return pm_call_1(mt, "it2d", MIR_T_D,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, boxed));
}

// ============================================================================
// Expression transpilers
// (forward declarations for generator helpers used in expression/statement transpilers)
static void pm_gen_emit_save(PyMirTranspiler* mt);
static void pm_gen_emit_restore(PyMirTranspiler* mt);
static void pm_gen_store_state(PyMirTranspiler* mt, int64_t state_val);
static void pm_transpile_for_gen(PyMirTranspiler* mt, PyForNode* forn);
// ============================================================================

// forward declarations — global variable routing
static bool pm_is_global_in_current_func(PyMirTranspiler* mt, const char* vname);
static int pm_get_global_var_index(PyMirTranspiler* mt, const char* vname);

static MIR_reg_t pm_transpile_literal(PyMirTranspiler* mt, PyLiteralNode* lit) {
    switch (lit->literal_type) {
    case PY_LITERAL_INT:
        if (lit->is_bigint_literal && lit->bigint_literal_str) {
            // large integer literal: call py_bigint_from_cstr at runtime
            String* interned = name_pool_create_len(mt->tp->name_pool,
                lit->bigint_literal_str, (int)strlen(lit->bigint_literal_str));
            MIR_reg_t ptr = pm_new_reg(mt, "biglit", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, ptr),
                MIR_new_int_op(mt->em.ctx, (int64_t)(uintptr_t)interned->chars)));
            return pm_call_1(mt, "py_bigint_from_cstr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, ptr));
        }
        return pm_box_int_const(mt, lit->value.int_value);
    case PY_LITERAL_FLOAT: {
        MIR_reg_t d = pm_new_reg(mt, "dbl", MIR_T_D);
        pm_emit_f64_immediate(mt, d, lit->value.float_value);
        return pm_box_float(mt, d);
    }
    case PY_LITERAL_STRING: {
        String* sv = lit->value.string_value;
        return pm_box_string_literal(mt, sv->chars, sv->len);
    }
    case PY_LITERAL_BOOLEAN:
        return pm_emit_bool(mt, lit->value.boolean_value);
    case PY_LITERAL_NONE:
        return pm_emit_null(mt);
    case AST_LITERAL_NUMBER:
    case AST_LITERAL_UNDEFINED:
        // shared AST values are never valid Python literal payloads; do not reinterpret them.
        return pm_emit_null(mt);
    }
    return pm_emit_null(mt);
}

static MIR_reg_t pm_transpile_identifier(PyMirTranspiler* mt, PyIdentifierNode* id) {
    // Python builtins as names
    if (id->name->len == 4 && strncmp(id->name->chars, "None", 4) == 0) {
        return pm_emit_null(mt);
    }
    if (id->name->len == 4 && strncmp(id->name->chars, "True", 4) == 0) {
        return pm_emit_bool(mt, true);
    }
    if (id->name->len == 5 && strncmp(id->name->chars, "False", 5) == 0) {
        return pm_emit_bool(mt, false);
    }

    char vname[128];
    snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);

    // check if variable is declared global in current function
    if (pm_is_global_in_current_func(mt, vname)) {
        int idx = pm_get_global_var_index(mt, vname);
        if (idx >= 0) {
            return pm_call_1(mt, "py_get_module_var", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, idx));
        }
    }

    PyMirVarEntry* var = pm_find_var(mt, vname);
    if (var) {
        if (var->from_env) return pm_load_env_slot(mt, var->env_reg, var->env_slot);
        return var->reg;
    }

    // fallback: check module-level vars (imports, classes, top-level assignments)
    {
        int midx = pm_get_global_var_index(mt, vname);
        if (midx >= 0) {
            return pm_call_1(mt, "py_get_module_var", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, midx));
        }
    }

    // Check local functions
    if (mt->local_funcs) {
        PyLocalFuncEntry key;
        snprintf(key.name, sizeof(key.name), "%s", vname);
        PyLocalFuncEntry* found = (PyLocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
        if (found) {
            // create a function Item from the MIR function
            for (int i = 0; i < mt->func_count; i++) {
                char fname[128];
                snprintf(fname, sizeof(fname), "_py_%s", mt->func_entries[i].name);
                if (strcmp(fname, vname) == 0) {
                    MIR_reg_t fptr = pm_new_reg(mt, "fptr", MIR_T_I64);
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                        MIR_new_reg_op(mt->em.ctx, fptr),
                        MIR_new_ref_op(mt->em.ctx, found->func_item)));
                    MIR_reg_t fn_item_reg = pm_call_2(mt, "py_new_function", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fptr),
                        MIR_T_I64, MIR_new_int_op(mt->em.ctx, mt->func_entries[i].param_count));
                    if (mt->func_entries[i].has_kwargs) {
                        pm_call_1(mt, "py_set_kwargs_flag", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item_reg));
                    }
                    fn_item_reg = pm_call_1(mt, "py_mark_mir_public_abi", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item_reg));
                    return fn_item_reg;
                }
            }
        }
    }

    log_debug("py-mir: undefined variable '%.*s' — trying builtin class lookup",
        (int)id->name->len, id->name->chars);
    // fallback: look up as a builtin class (ValueError, RuntimeError, etc.)
    MIR_reg_t name_item = pm_box_string_literal(mt, id->name->chars, id->name->len);
    return pm_call_1(mt, "py_resolve_name_item", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, name_item));
}

static const char* pm_binary_op_func(PyOperator op) {
    switch (op) {
    case PY_OP_ADD:        return "py_add";
    case PY_OP_SUB:        return "py_subtract";
    case PY_OP_MUL:        return "py_multiply";
    case PY_OP_DIV:        return "py_divide";
    case PY_OP_FLOOR_DIV:  return "py_floor_divide";
    case PY_OP_MOD:        return "py_modulo";
    case PY_OP_POW:        return "py_power";
    case PY_OP_LSHIFT:     return "py_lshift";
    case PY_OP_RSHIFT:     return "py_rshift";
    case PY_OP_BIT_AND:    return "py_bit_and";
    case PY_OP_BIT_OR:     return "py_bit_or";
    case PY_OP_BIT_XOR:    return "py_bit_xor";
    default:               return "py_add";
    }
}

static const char* pm_compare_op_func(PyOperator op) {
    switch (op) {
    case PY_OP_EQ:         return "py_eq";
    case PY_OP_NE:         return "py_ne";
    case PY_OP_LT:         return "py_lt";
    case PY_OP_LE:         return "py_le";
    case PY_OP_GT:         return "py_gt";
    case PY_OP_GE:         return "py_ge";
    case PY_OP_IS:         return "py_is";
    case PY_OP_IS_NOT:     return "py_is_not";
    case PY_OP_IN:         return "py_contains";
    case PY_OP_NOT_IN:     return NULL; // handled specially
    default:               return "py_eq";
    }
}

static MIR_reg_t pm_transpile_binary(PyMirTranspiler* mt, PyBinaryNode* bin) {
    // Phase 1: native integer arithmetic when both operands are provably INT
    TypeId lt = pm_get_effective_type(mt, bin->left);
    TypeId rt = pm_get_effective_type(mt, bin->right);

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT && bin->op != PY_OP_DIV && bin->op != PY_OP_POW) {
        MIR_insn_code_t op;
        bool can_overflow = false;
        switch (bin->op) {
        case PY_OP_ADD: op = MIR_ADD; can_overflow = true; break;
        case PY_OP_SUB: op = MIR_SUB; can_overflow = true; break;
        case PY_OP_MUL: op = MIR_MUL; can_overflow = true; break;
        case PY_OP_FLOOR_DIV: op = MIR_DIV; break;
        case PY_OP_MOD: op = MIR_MOD; break;
        case PY_OP_LSHIFT: op = MIR_LSH; can_overflow = true; break;
        case PY_OP_RSHIFT: op = MIR_RSH; break;
        case PY_OP_BIT_AND: op = MIR_AND; break;
        case PY_OP_BIT_OR: op = MIR_OR; break;
        case PY_OP_BIT_XOR: op = MIR_XOR; break;
        default: goto boxed_path;
        }
        const char* fn = pm_binary_op_func(bin->op);

        // transpile both as boxed Items first (needed for runtime fallback)
        MIR_reg_t lhs_boxed = pm_transpile_expression(mt, bin->left);
        MIR_reg_t rhs_boxed = pm_transpile_expression(mt, bin->right);

        MIR_reg_t final_result = pm_new_reg(mt, "bxr", MIR_T_I64);
        MIR_label_t l_boxed_call = pm_new_label(mt);
        MIR_label_t l_end = pm_new_label(mt);

        // runtime type guard: verify both operands are actually INT at runtime
        MIR_reg_t l_tag = pm_new_reg(mt, "lt", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_RSH, MIR_new_reg_op(mt->em.ctx, l_tag),
            MIR_new_reg_op(mt->em.ctx, lhs_boxed), MIR_new_int_op(mt->em.ctx, 56)));
        pm_emit_branch_not_equal_i64_immediate(mt, l_tag, (int64_t)LMD_TYPE_INT,
            l_boxed_call);
        MIR_reg_t r_tag = pm_new_reg(mt, "rt", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_RSH, MIR_new_reg_op(mt->em.ctx, r_tag),
            MIR_new_reg_op(mt->em.ctx, rhs_boxed), MIR_new_int_op(mt->em.ctx, 56)));
        pm_emit_branch_not_equal_i64_immediate(mt, r_tag, (int64_t)LMD_TYPE_INT,
            l_boxed_call);

        // native path: unbox and compute
        MIR_reg_t l = pm_emit_unbox_int(mt, lhs_boxed);
        MIR_reg_t r = pm_emit_unbox_int(mt, rhs_boxed);

        // for LSHIFT: CPU masks shift to 6 bits, so shift >= 64 gives wrong results
        if (bin->op == PY_OP_LSHIFT) {
            pm_emit_branch_greater_equal_i64_immediate(mt, r, 63, l_boxed_call);
        }

        MIR_reg_t res = pm_new_reg(mt, "ni", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, op,
            MIR_new_reg_op(mt->em.ctx, res),
            MIR_new_reg_op(mt->em.ctx, l),
            MIR_new_reg_op(mt->em.ctx, r)));

        if (can_overflow) {
            // range check: if result outside INT56, fall back to boxed runtime call
            int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
            int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;
            MIR_reg_t le_max = pm_new_reg(mt, "le", MIR_T_I64);
            MIR_reg_t ge_min = pm_new_reg(mt, "ge", MIR_T_I64);
            MIR_reg_t in_range = pm_new_reg(mt, "rng", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_LE, MIR_new_reg_op(mt->em.ctx, le_max),
                MIR_new_reg_op(mt->em.ctx, res), MIR_new_int_op(mt->em.ctx, INT56_MAX_VAL)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_GE, MIR_new_reg_op(mt->em.ctx, ge_min),
                MIR_new_reg_op(mt->em.ctx, res), MIR_new_int_op(mt->em.ctx, INT56_MIN_VAL)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_AND, MIR_new_reg_op(mt->em.ctx, in_range),
                MIR_new_reg_op(mt->em.ctx, le_max), MIR_new_reg_op(mt->em.ctx, ge_min)));
            MIR_label_t l_ok = pm_new_label(mt);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_ok),
                MIR_new_reg_op(mt->em.ctx, in_range)));
            // overflow → runtime call with boxed operands (handles bigint promotion)
            MIR_reg_t ov_result = pm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, lhs_boxed),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rhs_boxed));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, final_result),
                MIR_new_reg_op(mt->em.ctx, ov_result)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
            // fast path: inline box the native result
            pm_emit_label(mt, l_ok);
            MIR_reg_t masked = pm_new_reg(mt, "mask", MIR_T_I64);
            MIR_reg_t tagged = pm_new_reg(mt, "tag", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_AND, MIR_new_reg_op(mt->em.ctx, masked),
                MIR_new_reg_op(mt->em.ctx, res), MIR_new_int_op(mt->em.ctx, (int64_t)PY_MASK56)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_OR, MIR_new_reg_op(mt->em.ctx, tagged),
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_INT_TAG), MIR_new_reg_op(mt->em.ctx, masked)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, final_result),
                MIR_new_reg_op(mt->em.ctx, tagged)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
        } else {
            MIR_reg_t boxed = pm_box_int_reg(mt, res);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, final_result),
                MIR_new_reg_op(mt->em.ctx, boxed)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
        }

        // boxed fallback: runtime call (wrong type at runtime or overflow)
        pm_emit_label(mt, l_boxed_call);
        MIR_reg_t boxed_res = pm_call_2(mt, fn, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, lhs_boxed),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rhs_boxed));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, final_result),
            MIR_new_reg_op(mt->em.ctx, boxed_res)));
        pm_emit_label(mt, l_end);
        return final_result;
    }

    // native float arithmetic when both operands are numeric and at least one is float
    if ((lt == LMD_TYPE_FLOAT || rt == LMD_TYPE_FLOAT) &&
        (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT)) {
        MIR_insn_code_t op;
        switch (bin->op) {
        case PY_OP_ADD: op = MIR_DADD; break;
        case PY_OP_SUB: op = MIR_DSUB; break;
        case PY_OP_MUL: op = MIR_DMUL; break;
        case PY_OP_DIV: op = MIR_DDIV; break;
        default: goto boxed_path;
        }
        MIR_reg_t l = pm_transpile_as_native_float(mt, bin->left);
        MIR_reg_t r = pm_transpile_as_native_float(mt, bin->right);
        MIR_reg_t res = pm_new_reg(mt, "nf", MIR_T_D);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, op,
            MIR_new_reg_op(mt->em.ctx, res),
            MIR_new_reg_op(mt->em.ctx, l),
            MIR_new_reg_op(mt->em.ctx, r)));
        return pm_box_float(mt, res);
    }

    // true division on int/int yields float
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT && bin->op == PY_OP_DIV) {
        MIR_reg_t l = pm_transpile_as_native_float(mt, bin->left);
        MIR_reg_t r = pm_transpile_as_native_float(mt, bin->right);
        MIR_reg_t res = pm_new_reg(mt, "nf", MIR_T_D);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_DDIV,
            MIR_new_reg_op(mt->em.ctx, res),
            MIR_new_reg_op(mt->em.ctx, l),
            MIR_new_reg_op(mt->em.ctx, r)));
        return pm_box_float(mt, res);
    }

boxed_path:;
    // fallback: boxed runtime call
    MIR_reg_t left = pm_transpile_expression(mt, bin->left);
    MIR_reg_t right = pm_transpile_expression(mt, bin->right);
    const char* fn = pm_binary_op_func(bin->op);
    return pm_call_2(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, right));
}

static MIR_reg_t pm_transpile_unary(PyMirTranspiler* mt, PyUnaryNode* un) {
    MIR_reg_t operand = pm_transpile_expression(mt, un->operand);
    const char* fn;
    switch (un->op) {
    case PY_OP_NEGATE:  fn = "py_negate"; break;
    case PY_OP_POSITIVE: fn = "py_positive"; break;
    case PY_OP_BIT_NOT: fn = "py_bit_not"; break;
    default:            fn = "py_negate"; break;
    }
    return pm_call_1(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, operand));
}

static MIR_reg_t pm_transpile_not(PyMirTranspiler* mt, PyUnaryNode* un) {
    MIR_reg_t operand = pm_transpile_expression(mt, un->operand);
    // call py_is_truthy, negate result, box as bool
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, operand));
    // negate: result = truthy ? False : True
    MIR_reg_t result = pm_new_reg(mt, "not", MIR_T_I64);
    MIR_label_t l_true = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    pm_emit_branch_true(mt, truthy, l_true);
    // truthy was false → return True
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_TRUE_VAL)));
    pm_emit_jump(mt, l_end);
    pm_emit_label(mt, l_true);
    // truthy was true → return False
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_FALSE_VAL)));
    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_transpile_boolean(PyMirTranspiler* mt, PyBooleanNode* bop) {
    // short-circuit: and/or
    MIR_reg_t left = pm_transpile_expression(mt, bop->left);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, left));

    MIR_reg_t result = pm_new_reg(mt, "bop", MIR_T_I64);
    MIR_label_t l_short = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    if (bop->op == PY_OP_AND) {
        // and: if left is falsy, return left
        pm_emit_branch_false(mt, truthy, l_short);
    } else {
        // or: if left is truthy, return left
        pm_emit_branch_true(mt, truthy, l_short);
    }

    // evaluate right
    MIR_reg_t right = pm_transpile_expression(mt, bop->right);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, right)));
    pm_emit_jump(mt, l_end);

    pm_emit_label(mt, l_short);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, left)));
    pm_emit_label(mt, l_end);
    return result;
}

// check if a comparison operator can be done natively on integers
static bool pm_is_native_int_compare_op(PyOperator op) {
    return op == PY_OP_LT || op == PY_OP_LE || op == PY_OP_GT || op == PY_OP_GE ||
           op == PY_OP_EQ || op == PY_OP_NE;
}

static MIR_insn_code_t pm_native_int_compare_insn(PyOperator op) {
    switch (op) {
    case PY_OP_LT: return MIR_LT;
    case PY_OP_LE: return MIR_LE;
    case PY_OP_GT: return MIR_GT;
    case PY_OP_GE: return MIR_GE;
    case PY_OP_EQ: return MIR_EQ;
    case PY_OP_NE: return MIR_NE;
    default: return MIR_EQ;
    }
}

static MIR_insn_code_t pm_native_float_compare_insn(PyOperator op) {
    switch (op) {
    case PY_OP_LT: return MIR_DLT;
    case PY_OP_LE: return MIR_DLE;
    case PY_OP_GT: return MIR_DGT;
    case PY_OP_GE: return MIR_DGE;
    case PY_OP_EQ: return MIR_DEQ;
    case PY_OP_NE: return MIR_DNE;
    default: return MIR_DEQ;
    }
}

static MIR_reg_t pm_transpile_compare(PyMirTranspiler* mt, PyCompareNode* cmp) {
    // chained comparisons: a < b < c → (a < b) and (b < c)
    MIR_reg_t result = pm_new_reg(mt, "cmp", MIR_T_I64);
    MIR_label_t l_false = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    // Phase 2: for simple (non-chained) int comparisons, use native path with runtime type guard
    if (cmp->op_count == 1 && pm_is_native_int_compare_op(cmp->ops[0])) {
        TypeId lt = pm_get_effective_type(mt, cmp->left);
        TypeId rt = pm_get_effective_type(mt, cmp->comparators[0]);

        if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
            // transpile both as boxed first (for fallback)
            MIR_reg_t lhs_boxed = pm_transpile_expression(mt, cmp->left);
            MIR_reg_t rhs_boxed = pm_transpile_expression(mt, cmp->comparators[0]);

            MIR_label_t l_boxed_cmp = pm_new_label(mt);

            // runtime type guard: verify both are actually INT
            MIR_reg_t l_tag = pm_new_reg(mt, "clt", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_RSH, MIR_new_reg_op(mt->em.ctx, l_tag),
                MIR_new_reg_op(mt->em.ctx, lhs_boxed), MIR_new_int_op(mt->em.ctx, 56)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BNE, MIR_new_label_op(mt->em.ctx, l_boxed_cmp),
                MIR_new_reg_op(mt->em.ctx, l_tag), MIR_new_int_op(mt->em.ctx, (int64_t)LMD_TYPE_INT)));
            MIR_reg_t r_tag = pm_new_reg(mt, "crt", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_RSH, MIR_new_reg_op(mt->em.ctx, r_tag),
                MIR_new_reg_op(mt->em.ctx, rhs_boxed), MIR_new_int_op(mt->em.ctx, 56)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BNE, MIR_new_label_op(mt->em.ctx, l_boxed_cmp),
                MIR_new_reg_op(mt->em.ctx, r_tag), MIR_new_int_op(mt->em.ctx, (int64_t)LMD_TYPE_INT)));

            // native int compare
            MIR_reg_t l_val = pm_emit_unbox_int(mt, lhs_boxed);
            MIR_reg_t r_val = pm_emit_unbox_int(mt, rhs_boxed);
            MIR_reg_t cmp_res = pm_new_reg(mt, "ncmp", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, pm_native_int_compare_insn(cmp->ops[0]),
                MIR_new_reg_op(mt->em.ctx, cmp_res),
                MIR_new_reg_op(mt->em.ctx, l_val),
                MIR_new_reg_op(mt->em.ctx, r_val)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_false),
                MIR_new_reg_op(mt->em.ctx, cmp_res)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_TRUE_VAL)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));

            // boxed fallback: call runtime compare
            pm_emit_label(mt, l_boxed_cmp);
            const char* fn = pm_compare_op_func(cmp->ops[0]);
            MIR_reg_t boxed_cmp = pm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, lhs_boxed),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rhs_boxed));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
                MIR_new_reg_op(mt->em.ctx, boxed_cmp)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));

            pm_emit_label(mt, l_false);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_FALSE_VAL)));
            pm_emit_label(mt, l_end);
            return result;
        }

        // native float comparison
        if ((lt == LMD_TYPE_FLOAT || rt == LMD_TYPE_FLOAT) &&
            (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) &&
            (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT)) {
            MIR_reg_t l = pm_transpile_as_native_float(mt, cmp->left);
            MIR_reg_t r = pm_transpile_as_native_float(mt, cmp->comparators[0]);
            MIR_reg_t cmp_res = pm_new_reg(mt, "ncmpf", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, pm_native_float_compare_insn(cmp->ops[0]),
                MIR_new_reg_op(mt->em.ctx, cmp_res),
                MIR_new_reg_op(mt->em.ctx, l),
                MIR_new_reg_op(mt->em.ctx, r)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_false),
                MIR_new_reg_op(mt->em.ctx, cmp_res)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_TRUE_VAL)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
            pm_emit_label(mt, l_false);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_FALSE_VAL)));
            pm_emit_label(mt, l_end);
            return result;
        }
    }

    // general path: boxed comparisons with chaining support
    MIR_reg_t left = pm_transpile_expression(mt, cmp->left);

    for (int i = 0; i < cmp->op_count; i++) {
        MIR_reg_t right = pm_transpile_expression(mt, cmp->comparators[i]);

        const char* fn = pm_compare_op_func(cmp->ops[i]);
        MIR_reg_t cmp_result;

        if (cmp->ops[i] == PY_OP_NOT_IN) {
            // not in → negate contains
            cmp_result = pm_call_2(mt, "py_contains", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, right),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, left));
            // negate
            MIR_reg_t neg = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cmp_result));
            MIR_label_t l_was_true = pm_new_label(mt);
            MIR_label_t l_neg_end = pm_new_label(mt);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_was_true),
                MIR_new_reg_op(mt->em.ctx, neg)));
            cmp_result = pm_new_reg(mt, "neg", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, cmp_result),
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_TRUE_VAL)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_neg_end)));
            pm_emit_label(mt, l_was_true);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, cmp_result),
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_FALSE_VAL)));
            pm_emit_label(mt, l_neg_end);
        } else if (cmp->ops[i] == PY_OP_IN) {
            // in → py_contains(container, value)
            cmp_result = pm_call_2(mt, "py_contains", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, right),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, left));
        } else {
            cmp_result = pm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, left),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, right));
        }

        // check if comparison is false → short-circuit
        MIR_reg_t is_true = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cmp_result));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_false),
            MIR_new_reg_op(mt->em.ctx, is_true)));

        left = right;
    }

    // all comparisons passed
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_TRUE_VAL)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));

    pm_emit_label(mt, l_false);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_FALSE_VAL)));

    pm_emit_label(mt, l_end);
    return result;
}

// ============================================================================
// TCO helpers
// ============================================================================

// Check if a call node directly calls the given function by name.
static bool pm_is_py_recursive_call(PyCallNode* call, PyFuncCollected* fc) {
    if (!call || !fc || !call->function) return false;
    if (call->function->node_type != PY_AST_NODE_IDENTIFIER) return false;
    PyIdentifierNode* fn_id = (PyIdentifierNode*)call->function;
    int name_len = (int)strlen(fc->name);
    return (int)fn_id->name->len == name_len &&
           strncmp(fn_id->name->chars, fc->name, name_len) == 0;
}

// Recursively check if a function body contains a tail-recursive call.
// Tail position: return f(...), or return f(...) inside if/elif/else branches.
static bool pm_has_py_tail_call(PyAstNode* node, PyFuncCollected* fc) {
    if (!node) return false;
    switch (node->node_type) {
    case PY_AST_NODE_BLOCK: {
        // only the last statement can be in tail position
        PyAstNode* s = ((PyBlockNode*)node)->statements;
        PyAstNode* last = NULL;
        while (s) { last = s; s = s->next; }
        return pm_has_py_tail_call(last, fc);
    }
    case PY_AST_NODE_RETURN: {
        PyReturnNode* ret = (PyReturnNode*)node;
        if (!ret->value) return false;
        if (ret->value->node_type == PY_AST_NODE_CALL)
            return pm_is_py_recursive_call((PyCallNode*)ret->value, fc);
        return false;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* ifn = (PyIfNode*)node;
        if (pm_has_py_tail_call(ifn->body, fc)) return true;
        for (PyAstNode* e = ifn->elif_clauses; e; e = e->next)
            if (e->node_type == PY_AST_NODE_ELIF &&
                pm_has_py_tail_call(((PyIfNode*)e)->body, fc)) return true;
        if (pm_has_py_tail_call(ifn->else_body, fc)) return true;
        return false;
    }
    default:
        return false;
    }
}

// Check if a function should use TCO: must be named, non-closure, non-generator,
// and have at least one tail-recursive call in its body.
static bool pm_should_use_tco(PyFuncCollected* fc) {
    if (!fc || !fc->node || !fc->node->name) return false;
    if (fc->is_closure) return false;
    if (fc->is_generator) return false;
    if (fc->has_star_args || fc->has_kwargs) return false;
    return pm_has_py_tail_call(fc->node->body, fc);
}

static MIR_reg_t pm_transpile_call(PyMirTranspiler* mt, PyCallNode* call) {
    // check for builtin calls
    if (call->function && call->function->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* fn_id = (PyIdentifierNode*)call->function;
        const char* name = fn_id->name->chars;
        int name_len = fn_id->name->len;

        // super()
        if (name_len == 5 && strncmp(name, "super", 5) == 0) {
            // Python 3 zero-arg super: super() in a method implicitly uses
            // the enclosing class and self (first parameter)
            int cur = mt->current_func_index;
            if (cur >= 0 && mt->func_entries[cur].is_method) {
                const char* cls_name = mt->func_entries[cur].class_name;
                // load the class variable — look up scope then module vars
                char cls_var[132];
                snprintf(cls_var, sizeof(cls_var), "_py_%s", cls_name);
                MIR_reg_t cls_reg;
                {
                    // try scope first
                    PyMirVarEntry* cv = pm_find_var(mt, cls_var);
                    if (cv) {
                        cls_reg = cv->from_env ? pm_load_env_slot(mt, cv->env_reg, cv->env_slot) : cv->reg;
                    } else {
                        // try module var
                        int midx = pm_get_global_var_index(mt, cls_var);
                        if (midx >= 0) {
                            cls_reg = pm_call_1(mt, "py_get_module_var", MIR_T_I64,
                                MIR_T_I64, MIR_new_int_op(mt->em.ctx, midx));
                        } else {
                            cls_reg = pm_emit_null(mt);
                        }
                    }
                }
                // load self — first param of the method
                MIR_reg_t self_reg;
                PyAstNode* first_param = mt->func_entries[cur].node->params;
                if (first_param) {
                    String* pn = ((PyParamNode*)first_param)->name;
                    char self_var[132];
                    snprintf(self_var, sizeof(self_var), "_py_%.*s", (int)pn->len, pn->chars);
                    PyMirVarEntry* sv2 = pm_find_var(mt, self_var);
                    self_reg = sv2 ? (sv2->from_env ? pm_load_env_slot(mt, sv2->env_reg, sv2->env_slot) : sv2->reg) : pm_emit_null(mt);
                } else {
                    self_reg = pm_emit_null(mt);
                }
                return pm_call_2(mt, "py_super", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cls_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, self_reg));
            }
            return pm_emit_null(mt);
        }

        // print()
        if (name_len == 5 && strncmp(name, "print", 5) == 0) {
            // check for sep= and end= keyword arguments
            MIR_reg_t sep_reg = pm_emit_null(mt);
            MIR_reg_t end_reg = pm_emit_null(mt);
            bool has_kwargs = false;
            {
                PyAstNode* ka = call->arguments;
                while (ka) {
                    if (ka->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                        PyKeywordArgNode* kw = (PyKeywordArgNode*)ka;
                        if (kw->key && kw->key->len == 3 && strncmp(kw->key->chars, "sep", 3) == 0) {
                            sep_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        } else if (kw->key && kw->key->len == 3 && strncmp(kw->key->chars, "end", 3) == 0) {
                            end_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        }
                    }
                    ka = ka->next;
                }
            }

            // collect positional args on stack
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[64];
            while (arg) {
                if (arg->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                    arg = arg->next; continue;
                }
                if (argc >= 64) {
                    log_error("py-mir: print exceeds 64 positional arguments");
                    return pm_emit_null(mt);
                }
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }

            PmArgScope args = pm_begin_arg_scope(mt, argc);
            for (int i = 0; i < argc; i++) pm_arg_scope_store(mt, args, i, arg_regs[i]);

            if (has_kwargs) {
                MIR_reg_t result = pm_call_4(mt, "py_print_ex", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, sep_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, end_reg));
                pm_end_arg_scope(mt, args);
                return result;
            }
            MIR_reg_t result = pm_call_2(mt, "py_print", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));
            pm_end_arg_scope(mt, args);
            return result;
        }

        // len()
        if (name_len == 3 && strncmp(name, "len", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_len", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // type()
        if (name_len == 4 && strncmp(name, "type", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_type", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // int(), float(), str(), bool()
        if (name_len == 3 && strncmp(name, "int", 3) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_box_int_const(mt, 0);
            return pm_call_1(mt, "py_builtin_int", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 5 && strncmp(name, "float", 5) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_box_int_const(mt, 0);
            return pm_call_1(mt, "py_builtin_float", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "str", 3) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_box_string_literal(mt, "", 0);
            return pm_call_1(mt, "py_builtin_str", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 4 && strncmp(name, "bool", 4) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) : pm_emit_bool(mt, false);
            return pm_call_1(mt, "py_builtin_bool", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // abs()
        if (name_len == 3 && strncmp(name, "abs", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_abs", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // range()
        if (name_len == 5 && strncmp(name, "range", 5) == 0) {
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[3];
            while (arg) {
                if (argc >= 3) {
                    log_error("py-mir: range accepts at most 3 arguments");
                    return pm_emit_null(mt);
                }
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }
            PmArgScope args = pm_begin_arg_scope(mt, argc);
            for (int i = 0; i < argc; i++) pm_arg_scope_store(mt, args, i, arg_regs[i]);
            MIR_reg_t result = pm_call_2(mt, "py_builtin_range", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));
            pm_end_arg_scope(mt, args);
            return result;
        }

        // min(), max(), sum()
        if ((name_len == 3 && strncmp(name, "min", 3) == 0) ||
            (name_len == 3 && strncmp(name, "max", 3) == 0)) {
            const char* fn_name = (name[1] == 'i') ? "py_builtin_min" : "py_builtin_max";
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[64];
            while (arg) {
                if (argc >= 64) {
                    log_error("py-mir: min/max argument count exceeds Python argument stack limit");
                    return pm_emit_null(mt);
                }
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }
            PmArgScope args = pm_begin_arg_scope(mt, argc);
            for (int i = 0; i < argc; i++) pm_arg_scope_store(mt, args, i, arg_regs[i]);
            MIR_reg_t result = pm_call_2(mt, fn_name, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));
            pm_end_arg_scope(mt, args);
            return result;
        }

        if (name_len == 3 && strncmp(name, "sum", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_sum", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // sorted(), reversed()
        if (name_len == 6 && strncmp(name, "sorted", 6) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            // check for key= and reverse= kwargs
            MIR_reg_t key_reg = pm_emit_null(mt);
            MIR_reg_t rev_reg = pm_emit_null(mt);
            bool has_kwargs = false;
            {
                PyAstNode* ka = call->arguments;
                while (ka) {
                    if (ka->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                        PyKeywordArgNode* kw = (PyKeywordArgNode*)ka;
                        if (kw->key && kw->key->len == 3 && strncmp(kw->key->chars, "key", 3) == 0) {
                            key_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        } else if (kw->key && kw->key->len == 7 && strncmp(kw->key->chars, "reverse", 7) == 0) {
                            rev_reg = pm_transpile_expression(mt, kw->value);
                            has_kwargs = true;
                        }
                    }
                    ka = ka->next;
                }
            }
            if (has_kwargs) {
                return pm_call_3(mt, "py_builtin_sorted_ex", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rev_reg));
            }
            return pm_call_1(mt, "py_builtin_sorted", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 8 && strncmp(name, "reversed", 8) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_reversed", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // enumerate()
        if (name_len == 9 && strncmp(name, "enumerate", 9) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_enumerate", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // ord(), chr()
        if (name_len == 3 && strncmp(name, "ord", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_ord", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "chr", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_chr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // input()
        if (name_len == 5 && strncmp(name, "input", 5) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_box_string_literal(mt, "", 0);
            return pm_call_1(mt, "py_builtin_input", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // repr()
        if (name_len == 4 && strncmp(name, "repr", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_repr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // map(func, iterable)
        if (name_len == 3 && strncmp(name, "map", 3) == 0 && call->arg_count >= 2) {
            MIR_reg_t func_arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t iter_arg = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_map", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, func_arg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iter_arg));
        }

        // filter(func, iterable)
        if (name_len == 6 && strncmp(name, "filter", 6) == 0 && call->arg_count >= 2) {
            MIR_reg_t func_arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t iter_arg = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_filter", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, func_arg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iter_arg));
        }

        // list()
        if (name_len == 4 && strncmp(name, "list", 4) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_call_1(mt, "py_list_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, 0));
            if (!call->arguments) return arg;
            return pm_call_1(mt, "py_builtin_list", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // round()
        if (name_len == 5 && strncmp(name, "round", 5) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t ndigits = call->arguments->next ?
                pm_transpile_expression(mt, call->arguments->next) : pm_emit_null(mt);
            return pm_call_2(mt, "py_builtin_round", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, ndigits));
        }

        // all()
        if (name_len == 3 && strncmp(name, "all", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_all", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // any()
        if (name_len == 3 && strncmp(name, "any", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_any", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // bin(), oct(), hex()
        if (name_len == 3 && strncmp(name, "bin", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_bin", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "oct", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_oct", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 3 && strncmp(name, "hex", 3) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_hex", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // divmod()
        if (name_len == 6 && strncmp(name, "divmod", 6) == 0 && call->arg_count >= 2) {
            MIR_reg_t a = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t b = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_divmod", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, a),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, b));
        }

        // pow()
        if (name_len == 3 && strncmp(name, "pow", 3) == 0 && call->arg_count >= 2) {
            MIR_reg_t base = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t exp = pm_transpile_expression(mt, call->arguments->next);
            MIR_reg_t mod = (call->arguments->next->next) ?
                pm_transpile_expression(mt, call->arguments->next->next) : pm_emit_null(mt);
            return pm_call_3(mt, "py_builtin_pow", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, base),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exp),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, mod));
        }

        // callable()
        if (name_len == 8 && strncmp(name, "callable", 8) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_callable", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // getattr(obj, name) / getattr(obj, name, default)
        if (name_len == 7 && strncmp(name, "getattr", 7) == 0 && call->arg_count >= 2) {
            MIR_reg_t obj_r = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t key_r = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_getattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj_r),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_r));
        }

        // setattr(obj, name, value)
        if (name_len == 7 && strncmp(name, "setattr", 7) == 0 && call->arg_count >= 3) {
            MIR_reg_t obj_r = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t key_r = pm_transpile_expression(mt, call->arguments->next);
            MIR_reg_t val_r = pm_transpile_expression(mt, call->arguments->next->next);
            return pm_call_3(mt, "py_setattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj_r),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_r),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val_r));
        }

        // hasattr(obj, name)
        if (name_len == 7 && strncmp(name, "hasattr", 7) == 0 && call->arg_count >= 2) {
            MIR_reg_t obj_r = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t key_r = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_hasattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj_r),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_r));
        }

        // delattr(obj, name)
        if (name_len == 7 && strncmp(name, "delattr", 7) == 0 && call->arg_count >= 2) {
            MIR_reg_t obj_r = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t key_r = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_delattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj_r),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_r));
        }

        // property(fget) — creates a property descriptor {__is_property__: True, __get__: fget}
        if (name_len == 8 && strncmp(name, "property", 8) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // isinstance()
        if (name_len == 10 && strncmp(name, "isinstance", 10) == 0 && call->arg_count >= 2) {
            MIR_reg_t obj = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t cls = pm_transpile_expression(mt, call->arguments->next);
            return pm_call_2(mt, "py_builtin_isinstance", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cls));
        }

        // hash(), id()
        if (name_len == 4 && strncmp(name, "hash", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_hash", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 2 && strncmp(name, "id", 2) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_builtin_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // set(), tuple(), dict()
        if (name_len == 3 && strncmp(name, "set", 3) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_call_1(mt, "py_list_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, 0));
            if (!call->arguments) return arg;
            return pm_call_1(mt, "py_builtin_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 5 && strncmp(name, "tuple", 5) == 0) {
            MIR_reg_t arg = call->arguments ?
                pm_transpile_expression(mt, call->arguments) :
                pm_call_1(mt, "py_list_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, 0));
            if (!call->arguments) return arg;
            return pm_call_1(mt, "py_builtin_tuple", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }
        if (name_len == 4 && strncmp(name, "dict", 4) == 0) {
            // dict() with no args → empty dict
            return pm_call_0(mt, "py_dict_new", MIR_T_I64);
        }

        // zip()
        if (name_len == 3 && strncmp(name, "zip", 3) == 0) {
            int argc = 0;
            PyAstNode* arg = call->arguments;
            MIR_reg_t arg_regs[64];
            while (arg) {
                if (argc >= 64) {
                    log_error("py-mir: zip argument count exceeds Python argument stack limit");
                    return pm_emit_null(mt);
                }
                arg_regs[argc++] = pm_transpile_expression(mt, arg);
                arg = arg->next;
            }
            PmArgScope args = pm_begin_arg_scope(mt, argc);
            for (int i = 0; i < argc; i++) pm_arg_scope_store(mt, args, i, arg_regs[i]);
            MIR_reg_t result = pm_call_2(mt, "py_builtin_zip", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));
            pm_end_arg_scope(mt, args);
            return result;
        }

        // iter()
        if (name_len == 4 && strncmp(name, "iter", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_get_iterator", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // next()
        if (name_len == 4 && strncmp(name, "next", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t arg = pm_transpile_expression(mt, call->arguments);
            return pm_call_1(mt, "py_iterator_next", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, arg));
        }

        // open()
        if (name_len == 4 && strncmp(name, "open", 4) == 0 && call->arg_count >= 1) {
            MIR_reg_t path_arg = pm_transpile_expression(mt, call->arguments);
            MIR_reg_t mode_arg = (call->arguments->next) ?
                pm_transpile_expression(mt, call->arguments->next) :
                pm_box_string_literal(mt, "r", 1);
            return pm_call_2(mt, "py_builtin_open", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, path_arg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, mode_arg));
        }
    }

    // method call: obj.method(args)
    if (call->function && call->function->node_type == PY_AST_NODE_ATTRIBUTE) {
        PyAttributeNode* attr = (PyAttributeNode*)call->function;
        MIR_reg_t obj = pm_transpile_expression(mt, attr->object);

        // gen.send(value) — generator send shortcut
        if (attr->attribute->len == 4 && strncmp(attr->attribute->chars, "send", 4) == 0
            && call->arg_count == 1) {
            MIR_reg_t val = pm_transpile_expression(mt, call->arguments);
            return pm_call_2(mt, "py_gen_send", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
        }

        MIR_reg_t method_name = pm_box_string_literal(mt, attr->attribute->chars, attr->attribute->len);

        // collect args
        int argc = 0;
        PyAstNode* arg = call->arguments;
        MIR_reg_t arg_regs[64];
        while (arg) {
            if (arg->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                arg = arg->next; continue;
            }
            if (argc >= 64) {
                log_error("py-mir: method call exceeds Python argument stack limit");
                return pm_emit_null(mt);
            }
            arg_regs[argc++] = pm_transpile_expression(mt, arg);
            arg = arg->next;
        }

        PmArgScope args = pm_begin_arg_scope(mt, argc);
        for (int i = 0; i < argc; i++) pm_arg_scope_store(mt, args, i, arg_regs[i]);

        // dispatch: try string, then list, then dict method
        // simpler approach: call py_string_method first, if returns null try list, then dict
        MIR_reg_t str_result = pm_call_4(mt, "py_string_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, method_name),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));

        // check if string method returned non-null
        MIR_reg_t result = pm_new_reg(mt, "mcall", MIR_T_I64);
        MIR_label_t l_try_list = pm_new_label(mt);
        MIR_label_t l_try_dict = pm_new_label(mt);
        MIR_label_t l_end = pm_new_label(mt);

        // if str_result != NULL_VAL, use it
        MIR_reg_t is_null = pm_new_reg(mt, "isnull", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_EQ, MIR_new_reg_op(mt->em.ctx, is_null),
            MIR_new_reg_op(mt->em.ctx, str_result),
            MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_try_list),
            MIR_new_reg_op(mt->em.ctx, is_null)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
            MIR_new_reg_op(mt->em.ctx, str_result)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));

        pm_emit_label(mt, l_try_list);
        MIR_reg_t list_result = pm_call_4(mt, "py_list_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, method_name),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_EQ, MIR_new_reg_op(mt->em.ctx, is_null),
            MIR_new_reg_op(mt->em.ctx, list_result),
            MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_try_dict),
            MIR_new_reg_op(mt->em.ctx, is_null)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
            MIR_new_reg_op(mt->em.ctx, list_result)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));

        pm_emit_label(mt, l_try_dict);
        MIR_reg_t dict_result = pm_call_4(mt, "py_dict_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, method_name),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_EQ, MIR_new_reg_op(mt->em.ctx, is_null),
            MIR_new_reg_op(mt->em.ctx, dict_result),
            MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL)));
        MIR_label_t l_try_instance = pm_new_label(mt);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_try_instance),
            MIR_new_reg_op(mt->em.ctx, is_null)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
            MIR_new_reg_op(mt->em.ctx, dict_result)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));

        // fallback: user-defined class method — get bound method via py_getattr + call it
        pm_emit_label(mt, l_try_instance);
        MIR_reg_t bound_method = pm_call_2(mt, "py_getattr", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, method_name));
        MIR_reg_t instance_result = pm_call_3(mt, "py_call_function", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, bound_method),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
            MIR_new_reg_op(mt->em.ctx, instance_result)));

        pm_emit_label(mt, l_end);
        pm_end_arg_scope(mt, args);
        return result;
    }

    // direct call to known user function — resolve kwargs at compile time
    if (call->function && call->function->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* fn_id = (PyIdentifierNode*)call->function;
        // look up in func_entries
        PyFuncCollected* target_fc = NULL;
        for (int i = 0; i < mt->func_count; i++) {
            if ((int)fn_id->name->len == (int)strlen(mt->func_entries[i].name) &&
                strncmp(fn_id->name->chars, mt->func_entries[i].name, fn_id->name->len) == 0) {
                target_fc = &mt->func_entries[i];
                break;
            }
        }
        // Phase C: if this function was decorated, its name is now bound to the
        // decorator's return value (stored as a local var).  Use the generic
        // py_call_function path so the decorator takes effect.
        if (target_fc && target_fc->node && target_fc->node->decorators) {
            char vcheck[128];
            snprintf(vcheck, sizeof(vcheck), "_py_%.*s",
                (int)fn_id->name->len, fn_id->name->chars);
            if (pm_find_var(mt, vcheck)) {
                target_fc = NULL;  // force fall-through to generic py_call_function
            }
        }
        if (target_fc && target_fc->is_closure) {
            // A direct MIR call has no Function object to supply the closure's
            // hidden environment argument; preserve the closure ABI via runtime dispatch.
            target_fc = NULL;
        }
        if (target_fc && target_fc->func_item) {
            // ======== TCO interception ========
            // If this is a tail-recursive call to the current TCO function,
            // convert to: evaluate args → assign to params → goto tco_label
            if (mt->tco_func && mt->in_tail_position && target_fc == mt->tco_func) {
                log_debug("py-mir: TCO tail call to '%s' — converting to goto", mt->tco_func->name);

                // arguments are NOT in tail position
                bool saved_tail = mt->in_tail_position;
                mt->in_tail_position = false;

                int total_params = target_fc->param_count;

                // Phase 1: evaluate all arguments into temporaries
                MIR_reg_t temps[16];
                int arg_idx = 0;
                PyAstNode* a = call->arguments;
                // positional args
                while (a && arg_idx < total_params) {
                    if (a->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                        a = a->next; continue;
                    }
                    temps[arg_idx] = pm_transpile_expression(mt, a);
                    arg_idx++;
                    a = a->next;
                }
                // fill remaining with null
                while (arg_idx < total_params) {
                    temps[arg_idx] = pm_emit_null(mt);
                    arg_idx++;
                }

                // Phase 2: assign temporaries to parameter registers
                int offset = target_fc->is_closure ? 1 : 0;
                PyAstNode* pnode = target_fc->node->params;
                for (int i = 0; i < total_params && i < 16; i++) {
                    String* pn = NULL;
                    while (pnode) {
                        if (pnode->node_type == PY_AST_NODE_LIST_SPLAT_PARAMETER ||
                            pnode->node_type == PY_AST_NODE_DICT_SPLAT_PARAMETER) {
                            pnode = pnode->next;
                            continue;
                        }
                        if (pnode->node_type == PY_AST_NODE_PARAMETER ||
                            pnode->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                            pnode->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                            pn = ((PyParamNode*)pnode)->name;
                        }
                        pnode = pnode->next;
                        break;
                    }
                    if (pn) {
                        char pname[128];
                        snprintf(pname, sizeof(pname), "_py_%.*s", (int)pn->len, pn->chars);
                        PyMirVarEntry* pvar = pm_find_var(mt, pname);
                        if (pvar) {
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                                MIR_new_reg_op(mt->em.ctx, pvar->reg),
                                MIR_new_reg_op(mt->em.ctx, temps[i])));
                        }
                    }
                }

                mt->in_tail_position = saved_tail;

                // jump back to function start
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
                    MIR_new_label_op(mt->em.ctx, mt->tco_label)));
                mt->block_returned = true;

                // return dummy register (unreachable but MIR needs a value)
                MIR_reg_t dummy = pm_new_reg(mt, "tco_dummy", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                    MIR_new_reg_op(mt->em.ctx, dummy),
                    MIR_new_int_op(mt->em.ctx, 0)));
                return dummy;
            }

            int total_params = target_fc->param_count;
            // build ordered argument array: init all slots to null
            MIR_reg_t ordered_args[16];
            bool arg_filled[16];
            for (int i = 0; i < total_params && i < 16; i++) {
                ordered_args[i] = pm_emit_null(mt);
                arg_filled[i] = false;
            }

            // first pass: fill positional arguments
            int pos = 0;
            PyAstNode* a = call->arguments;
            while (a && pos < total_params) {
                if (a->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                    a = a->next;
                    continue;
                }
                ordered_args[pos] = pm_transpile_expression(mt, a);
                arg_filled[pos] = true;
                pos++;
                a = a->next;
            }

            // second pass: fill keyword arguments by matching param names
            a = call->arguments;
            while (a) {
                if (a->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                    PyKeywordArgNode* kw = (PyKeywordArgNode*)a;
                    if (kw->key) {
                        // find matching parameter by name
                        PyAstNode* pp = target_fc->node->params;
                        int pi = 0;
                        while (pp && pi < total_params) {
                            String* pname = NULL;
                            if (pp->node_type == PY_AST_NODE_PARAMETER ||
                                pp->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                                pp->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                                pname = ((PyParamNode*)pp)->name;
                            }
                            if (pname && pname->len == kw->key->len &&
                                strncmp(pname->chars, kw->key->chars, pname->len) == 0) {
                                ordered_args[pi] = pm_transpile_expression(mt, kw->value);
                                arg_filled[pi] = true;
                                break;
                            }
                            pp = pp->next;
                            pi++;
                        }
                    }
                }
                a = a->next;
            }

            // emit direct call to local function with ordered args
            char fn_name[140];
            snprintf(fn_name, sizeof(fn_name), "_%s", target_fc->name);

            // determine total MIR params (regular + optional varargs pair + optional kwargs map)
            int mir_total = total_params + (target_fc->has_star_args ? 2 : 0) +
                (target_fc->has_kwargs ? 1 : 0);
            int direct_mir_total = mir_total + 1;
            uint8_t direct_param_kinds[21] = {};
            direct_param_kinds[mir_total] = 1;

            // check import cache for proto (avoid duplicate proto names)
            char proto_key[148];
            snprintf(proto_key, sizeof(proto_key), "%s_local", fn_name);
            MirImportCacheEntry key;
            memset(&key, 0, sizeof(key));
            snprintf(key.name, sizeof(key.name), "%s", proto_key);
            MirImportCacheEntry* cached =
                (MirImportCacheEntry*)hashmap_get(mt->em.import_cache, &key);
            MIR_item_t proto;
            if (cached) {
                proto = cached->entry.proto;
            } else {
                char proto_name[160];
                snprintf(proto_name, sizeof(proto_name), "%s_lp", fn_name);
                if (!pm_create_hosted_item_function_proto_typed(mt->em.ctx,
                        proto_name, direct_mir_total, direct_param_kinds, &proto)) {
                    log_error("py-mir: host could not create direct-call prototype '%s'", proto_name);
                    mt->has_compile_error = true;
                    return pm_emit_null(mt);
                }
                MirImportCacheEntry new_entry;
                memset(&new_entry, 0, sizeof(new_entry));
                snprintf(new_entry.name, sizeof(new_entry.name), "%s", proto_key);
                new_entry.entry.proto = proto;
                new_entry.entry.import = NULL;
                hashmap_set(mt->em.import_cache, &new_entry);
            }

            // build kwargs map for **kwargs-accepting functions
            MIR_reg_t kwargs_map_reg = 0;
            if (target_fc->has_kwargs) {
                kwargs_map_reg = pm_call_0(mt, "py_dict_new", MIR_T_I64);
                // third pass: collect leftover keyword args + **splat into kwargs map
                PyAstNode* ka = call->arguments;
                while (ka) {
                    if (ka->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                        PyKeywordArgNode* kw = (PyKeywordArgNode*)ka;
                        if (!kw->key) {
                            // **expr splat — merge into kwargs map
                            MIR_reg_t splat_val = pm_transpile_expression(mt, kw->value);
                            pm_call_2(mt, "py_dict_merge", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kwargs_map_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, splat_val));
                        } else {
                            // named kwarg — add to map if it doesn't match any named param
                            bool kw_matched = false;
                            if (target_fc->node) {
                                PyAstNode* pp = target_fc->node->params;
                                int pi = 0;
                                while (pp && pi < total_params) {
                                    String* pname = NULL;
                                    if (pp->node_type == PY_AST_NODE_PARAMETER ||
                                        pp->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                                        pp->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                                        pname = ((PyParamNode*)pp)->name;
                                    }
                                    if (pname && pname->len == kw->key->len &&
                                        strncmp(pname->chars, kw->key->chars, pname->len) == 0) {
                                        kw_matched = true;
                                        break;
                                    }
                                    pp = pp->next;
                                    pi++;
                                }
                            }
                            if (!kw_matched) {
                                MIR_reg_t kname_reg = pm_box_string_literal(mt, kw->key->chars, kw->key->len);
                                MIR_reg_t kval_reg = pm_transpile_expression(mt, kw->value);
                                pm_call_3(mt, "py_dict_set", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kwargs_map_reg),
                                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kname_reg),
                                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kval_reg));
                            }
                        }
                    }
                    ka = ka->next;
                }
            }

            MIR_reg_t res = pm_new_reg(mt, "dcall", MIR_T_I64);
            int result_home_id = em_scalar_home_new(&mt->em);
            MIR_reg_t result_home = em_materialize_frame_ref(&mt->em,
                em_scalar_home_ref(&mt->em, result_home_id));
            if (!result_home) {
                log_error("py-mir: direct call has no caller-owned scalar result home");
                abort();
            }

            if (target_fc->has_star_args) {
                // collect all positional args beyond regular params as excess
                int excess_count = 0;
                MIR_reg_t excess_regs[64];
                int positional = 0;
                PyAstNode* pa = call->arguments;
                while (pa) {
                    if (pa->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) { pa = pa->next; continue; }
                    positional++;
                    if (positional > total_params) {
                        // excess positional arg → *args
                        if (excess_count >= 64) {
                            log_error("py-mir: *args exceeds Python argument stack limit");
                            return pm_emit_null(mt);
                        }
                        excess_regs[excess_count++] = pm_transpile_expression(mt, pa);
                    }
                    pa = pa->next;
                }

                PmArgScope varargs = pm_begin_arg_scope(mt, excess_count);
                for (int i = 0; i < excess_count; i++) {
                    pm_arg_scope_store(mt, varargs, i, excess_regs[i]);
                }

                int nops = 3 + direct_mir_total;
                MIR_op_t ops[24];
                ops[0] = MIR_new_ref_op(mt->em.ctx, proto);
                ops[1] = MIR_new_ref_op(mt->em.ctx, target_fc->func_item);
                ops[2] = MIR_new_reg_op(mt->em.ctx, res);
                for (int i = 0; i < total_params && i < 16; i++) {
                    ops[3 + i] = MIR_new_reg_op(mt->em.ctx, ordered_args[i]);
                }
                ops[3 + total_params] = MIR_new_reg_op(mt->em.ctx, varargs.args);
                ops[3 + total_params + 1] = MIR_new_int_op(mt->em.ctx, excess_count);
                if (target_fc->has_kwargs) {
                    ops[3 + total_params + 2] = MIR_new_reg_op(mt->em.ctx, kwargs_map_reg);
                }
                ops[3 + mir_total] = MIR_new_reg_op(mt->em.ctx, result_home);
                pm_emit_local_direct_call(mt, fn_name, nops, ops);
                pm_end_arg_scope(mt, varargs);
            } else {
                int nops = 3 + direct_mir_total;
                MIR_op_t ops[20]; // proto + func_ref + result + up to 16 args + optional kwargs map
                ops[0] = MIR_new_ref_op(mt->em.ctx, proto);
                ops[1] = MIR_new_ref_op(mt->em.ctx, target_fc->func_item);
                ops[2] = MIR_new_reg_op(mt->em.ctx, res);
                for (int i = 0; i < total_params && i < 16; i++) {
                    ops[3 + i] = MIR_new_reg_op(mt->em.ctx, ordered_args[i]);
                }
                if (target_fc->has_kwargs) {
                    ops[3 + total_params] = MIR_new_reg_op(mt->em.ctx, kwargs_map_reg);
                }
                ops[3 + mir_total] = MIR_new_reg_op(mt->em.ctx, result_home);
                pm_emit_local_direct_call(mt, fn_name, nops, ops);
            }
            em_scalar_home_bind(&mt->em, result_home_id, res);
            return res;
        }
    }

    // generic function call: evaluate function, collect args, call py_call_function
    MIR_reg_t func = pm_transpile_expression(mt, call->function);

    int argc = 0;
    PyAstNode* arg = call->arguments;
    MIR_reg_t arg_regs[64];
    while (arg) {
        if (arg->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
            arg = arg->next; continue;
        }
        if (argc >= 64) {
            log_error("py-mir: generic call exceeds 64 positional arguments");
            return pm_emit_null(mt);
        }
        arg_regs[argc++] = pm_transpile_expression(mt, arg);
        arg = arg->next;
    }

    PmArgScope args = pm_begin_arg_scope(mt, argc);
    for (int i = 0; i < argc; i++) pm_arg_scope_store(mt, args, i, arg_regs[i]);

    // check for keyword arguments (**expr splats and named kwargs) in generic call path
    {
        bool has_any_kwarg = false;
        PyAstNode* ka = call->arguments;
        while (ka) {
            if (ka->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) { has_any_kwarg = true; break; }
            ka = ka->next;
        }
        if (has_any_kwarg) {
            // build kwargs map from all named kwargs and **splat
            MIR_reg_t kw_map = pm_call_0(mt, "py_dict_new", MIR_T_I64);
            ka = call->arguments;
            while (ka) {
                if (ka->node_type == PY_AST_NODE_KEYWORD_ARGUMENT) {
                    PyKeywordArgNode* kw = (PyKeywordArgNode*)ka;
                    if (!kw->key) {
                        // **expr splat — merge into kwargs map
                        MIR_reg_t splat_val = pm_transpile_expression(mt, kw->value);
                        pm_call_2(mt, "py_dict_merge", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kw_map),
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, splat_val));
                    } else {
                        // named kwarg — add to map
                        MIR_reg_t kname_reg = pm_box_string_literal(mt, kw->key->chars, kw->key->len);
                        MIR_reg_t kval_reg = pm_transpile_expression(mt, kw->value);
                        pm_call_3(mt, "py_dict_set", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kw_map),
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kname_reg),
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kval_reg));
                    }
                }
                ka = ka->next;
            }
            int keyword_home_id = em_scalar_home_new(&mt->em);
            MIR_reg_t keyword_home = em_materialize_frame_ref(&mt->em,
                em_scalar_home_ref(&mt->em, keyword_home_id));
            if (!keyword_home) {
                log_error("py-mir: keyword call has no caller-owned scalar result home");
                abort();
            }
            MIR_reg_t result = pm_call_5(mt, "py_call_function_kw_into", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, func),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, kw_map),
                MIR_T_P, MIR_new_reg_op(mt->em.ctx, keyword_home));
            em_scalar_home_bind(&mt->em, keyword_home_id, result);
            pm_end_arg_scope(mt, args);
            return result;
        }
    }

    int dynamic_home_id = em_scalar_home_new(&mt->em);
    MIR_reg_t dynamic_home = em_materialize_frame_ref(&mt->em,
        em_scalar_home_ref(&mt->em, dynamic_home_id));
    if (!dynamic_home) {
        log_error("py-mir: dynamic call has no caller-owned scalar result home");
        abort();
    }
    MIR_reg_t result = pm_call_4(mt, "py_call_function_into", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, func),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, argc),
        MIR_T_P, MIR_new_reg_op(mt->em.ctx, dynamic_home));
    em_scalar_home_bind(&mt->em, dynamic_home_id, result);
    pm_end_arg_scope(mt, args);
    return result;
}

static MIR_reg_t pm_transpile_attribute(PyMirTranspiler* mt, PyAttributeNode* attr) {
    MIR_reg_t obj = pm_transpile_expression(mt, attr->object);
    MIR_reg_t name = pm_box_string_literal(mt, attr->attribute->chars, attr->attribute->len);
    return pm_call_2(mt, "py_getattr", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, name));
}

static MIR_reg_t pm_transpile_subscript(PyMirTranspiler* mt, PySubscriptNode* sub) {
    MIR_reg_t obj = pm_transpile_expression(mt, sub->object);

    // check if index is a slice node
    if (sub->index && sub->index->node_type == PY_AST_NODE_SLICE) {
        PySliceNode* slice = (PySliceNode*)sub->index;
        MIR_reg_t start = slice->start ? pm_transpile_expression(mt, slice->start) : pm_emit_null(mt);
        MIR_reg_t stop = slice->stop ? pm_transpile_expression(mt, slice->stop) : pm_emit_null(mt);
        MIR_reg_t step = slice->step ? pm_transpile_expression(mt, slice->step) : pm_emit_null(mt);
        return pm_call_4(mt, "py_slice_get", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, start),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, stop),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, step));
    }

    MIR_reg_t key = pm_transpile_expression(mt, sub->index);
    return pm_call_2(mt, "py_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key));
}

static MIR_reg_t pm_transpile_list(PyMirTranspiler* mt, PySequenceNode* seq) {
    MIR_reg_t list = pm_call_1(mt, "py_list_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, 0));

    PyAstNode* elem = seq->elements;
    while (elem) {
        MIR_reg_t val = pm_transpile_expression(mt, elem);
        pm_call_2(mt, "py_list_append", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, list),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
        elem = elem->next;
    }
    return list;
}

static MIR_reg_t pm_transpile_tuple(PyMirTranspiler* mt, PySequenceNode* seq) {
    int length = seq->length;
    MIR_reg_t tuple = pm_call_1(mt, "py_tuple_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, length));

    PyAstNode* elem = seq->elements;
    int i = 0;
    while (elem) {
        MIR_reg_t val = pm_transpile_expression(mt, elem);
        pm_call_3(mt, "py_tuple_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, tuple),
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, i),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
        elem = elem->next;
        i++;
    }
    return tuple;
}

static MIR_reg_t pm_transpile_dict(PyMirTranspiler* mt, PyDictNode* dict) {
    MIR_reg_t d = pm_call_0(mt, "py_dict_new", MIR_T_I64);

    PyAstNode* pair_node = dict->pairs;
    while (pair_node) {
        if (pair_node->node_type == PY_AST_NODE_PAIR) {
            PyPairNode* pair = (PyPairNode*)pair_node;
            MIR_reg_t key = pm_transpile_expression(mt, pair->key);
            MIR_reg_t val = pm_transpile_expression(mt, pair->value);
            pm_call_3(mt, "py_dict_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, d),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
        }
        pair_node = pair_node->next;
    }
    return d;
}

static MIR_reg_t pm_transpile_conditional(PyMirTranspiler* mt, PyConditionalNode* cond) {
    MIR_reg_t test = pm_transpile_expression(mt, cond->test);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, test));

    MIR_reg_t result = pm_new_reg(mt, "cond", MIR_T_I64);
    MIR_label_t l_else = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    pm_emit_branch_false(mt, truthy, l_else);

    MIR_reg_t then_val = pm_transpile_expression(mt, cond->then);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, then_val)));
    pm_emit_jump(mt, l_end);

    pm_emit_label(mt, l_else);
    MIR_reg_t else_val = pm_transpile_expression(mt, cond->otherwise);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, result),
        MIR_new_reg_op(mt->em.ctx, else_val)));

    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_transpile_fstring(PyMirTranspiler* mt, PyFStringNode* fstr) {
    // f-string: concatenate parts via py_to_str + py_add
    MIR_reg_t result = pm_box_string_literal(mt, "", 0);
    PyAstNode* part = fstr->parts;
    while (part) {
        MIR_reg_t val;
        if (part->node_type == PY_AST_NODE_LITERAL) {
            val = pm_transpile_literal(mt, (PyLiteralNode*)part);
        } else if (part->node_type == PY_AST_NODE_FSTRING_EXPR) {
            // f-string expression with format spec: f"{expr:spec}"
            PyFStringExprNode* fse = (PyFStringExprNode*)part;
            MIR_reg_t expr_val = pm_transpile_expression(mt, fse->expression);
            MIR_reg_t spec_val = pm_box_string_literal(mt, fse->format_spec->chars, fse->format_spec->len);
            val = pm_call_2(mt, "py_format_value", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, expr_val),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, spec_val));
        } else {
            MIR_reg_t expr = pm_transpile_expression(mt, part);
            val = pm_call_1(mt, "py_to_str", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, expr));
        }
        result = pm_call_2(mt, "py_add", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, result),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
        part = part->next;
    }
    return result;
}

// ============================================================================
// Comprehensions
// ============================================================================

// helper: assign a loop element to the target variable(s) in comprehension
static void pm_assign_comp_target(PyMirTranspiler* mt, PyAstNode* target, MIR_reg_t item) {
    if (target->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)target;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var) {
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, var->reg),
                MIR_new_reg_op(mt->em.ctx, item)));
        } else {
            MIR_reg_t reg = pm_new_reg(mt, vname, MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, reg),
                MIR_new_reg_op(mt->em.ctx, item)));
            pm_set_var(mt, vname, reg);
        }
    } else if (target->node_type == PY_AST_NODE_TUPLE ||
               target->node_type == PY_AST_NODE_TUPLE_UNPACK) {
        PySequenceNode* tup = (PySequenceNode*)target;
        PyAstNode* elem = tup->elements;
        int i = 0;
        while (elem) {
            MIR_reg_t sub_item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, item),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pm_box_int_const(mt, i)));
            pm_assign_comp_target(mt, elem, sub_item);
            elem = elem->next;
            i++;
        }
    }
}

static MIR_reg_t pm_transpile_list_comprehension(PyMirTranspiler* mt, PyComprehensionNode* comp) {
    // create result list
    MIR_reg_t result = pm_call_1(mt, "py_list_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, 0));

    // evaluate iterable
    MIR_reg_t iterable = pm_transpile_expression(mt, comp->iter);
    MIR_reg_t length = pm_call_1(mt, "py_builtin_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iterable));

    // loop index
    MIR_reg_t idx = pm_new_reg(mt, "comp_idx", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, idx),
        MIR_new_int_op(mt->em.ctx, (int64_t)(PY_ITEM_INT_TAG | 0))));

    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    MIR_label_t l_continue = pm_new_label(mt);

    pm_emit_label(mt, l_start);

    // check idx < length
    MIR_reg_t cmp = pm_call_2(mt, "py_lt", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, length));
    MIR_reg_t cmp_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cmp));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_end),
        MIR_new_reg_op(mt->em.ctx, cmp_truthy)));

    // get current item and assign to target
    MIR_reg_t item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iterable),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, idx));
    pm_assign_comp_target(mt, comp->target, item);

    // evaluate conditions (if any)
    MIR_label_t l_skip = NULL;
    if (comp->conditions) {
        l_skip = pm_new_label(mt);
        PyAstNode* cond = comp->conditions;
        while (cond) {
            MIR_reg_t cond_val = pm_transpile_expression(mt, cond);
            MIR_reg_t cond_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cond_val));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_skip),
                MIR_new_reg_op(mt->em.ctx, cond_truthy)));
            cond = cond->next;
        }
    }

    // evaluate element expression and append to result
    MIR_reg_t elem = pm_transpile_expression(mt, comp->element);
    pm_call_2(mt, "py_list_append", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, result),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, elem));

    if (l_skip) pm_emit_label(mt, l_skip);
    pm_emit_label(mt, l_continue);

    // increment idx
    MIR_reg_t one = pm_box_int_const(mt, 1);
    MIR_reg_t new_idx = pm_call_2(mt, "py_add", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, one));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
        MIR_new_reg_op(mt->em.ctx, idx),
        MIR_new_reg_op(mt->em.ctx, new_idx)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_start)));

    pm_emit_label(mt, l_end);
    return result;
}

static MIR_reg_t pm_transpile_dict_comprehension(PyMirTranspiler* mt, PyComprehensionNode* comp) {
    // create result dict
    MIR_reg_t result = pm_call_0(mt, "py_dict_new", MIR_T_I64);

    // evaluate iterable
    MIR_reg_t iterable = pm_transpile_expression(mt, comp->iter);
    MIR_reg_t length = pm_call_1(mt, "py_builtin_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iterable));

    MIR_reg_t idx = pm_new_reg(mt, "comp_idx", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, idx),
        MIR_new_int_op(mt->em.ctx, (int64_t)(PY_ITEM_INT_TAG | 0))));

    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);
    MIR_label_t l_continue = pm_new_label(mt);

    pm_emit_label(mt, l_start);

    MIR_reg_t cmp = pm_call_2(mt, "py_lt", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, length));
    MIR_reg_t cmp_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cmp));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_end),
        MIR_new_reg_op(mt->em.ctx, cmp_truthy)));

    MIR_reg_t item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iterable),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, idx));
    pm_assign_comp_target(mt, comp->target, item);

    // evaluate conditions
    MIR_label_t l_skip = NULL;
    if (comp->conditions) {
        l_skip = pm_new_label(mt);
        PyAstNode* cond = comp->conditions;
        while (cond) {
            MIR_reg_t cond_val = pm_transpile_expression(mt, cond);
            MIR_reg_t cond_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cond_val));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_skip),
                MIR_new_reg_op(mt->em.ctx, cond_truthy)));
            cond = cond->next;
        }
    }

    // for dict comprehension, element is a pair node with key and value
    if (comp->element && comp->element->node_type == PY_AST_NODE_PAIR) {
        PyPairNode* pair = (PyPairNode*)comp->element;
        MIR_reg_t key_val = pm_transpile_expression(mt, pair->key);
        MIR_reg_t val_val = pm_transpile_expression(mt, pair->value);
        pm_call_3(mt, "py_dict_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, result),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_val),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val_val));
    }

    if (l_skip) pm_emit_label(mt, l_skip);
    pm_emit_label(mt, l_continue);

    MIR_reg_t one = pm_box_int_const(mt, 1);
    MIR_reg_t new_idx = pm_call_2(mt, "py_add", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, one));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
        MIR_new_reg_op(mt->em.ctx, idx),
        MIR_new_reg_op(mt->em.ctx, new_idx)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_start)));

    pm_emit_label(mt, l_end);
    return result;
}

// ============================================================================
// Lambda expressions
// ============================================================================

static MIR_reg_t pm_transpile_lambda(PyMirTranspiler* mt, PyLambdaNode* lam) {
    // find the pre-compiled lambda entry by matching node pointer
    for (int i = 0; i < mt->lambda_count; i++) {
        if (mt->lambda_entries[i].node == lam) {
            PyLambdaCollected* lc = &mt->lambda_entries[i];
            MIR_reg_t fptr = pm_new_reg(mt, "lam_ptr", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, fptr),
                MIR_new_ref_op(mt->em.ctx, lc->func_item)));

            if (lc->is_closure) {
                MIR_reg_t closure = pm_call_3(mt, "py_new_closure", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fptr),
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, lc->param_count),
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, lc->capture_count));
                MIR_reg_t env = pm_call_1(mt, "py_closure_get_env", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, closure));
                for (int ci = 0; ci < lc->capture_count; ci++) {
                    PyMirVarEntry* cvar = pm_find_var(mt, lc->captures[ci]);
                    if (cvar) {
                        pm_store_env_slot(mt, env, ci, cvar->reg);
                    }
                }
                return pm_call_1(mt, "py_mark_mir_public_abi", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, closure));
            }

            MIR_reg_t function = pm_call_2(mt, "py_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fptr),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, lc->param_count));
            return pm_call_1(mt, "py_mark_mir_public_abi", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, function));
        }
    }
    log_error("py-mir: lambda not found in pre-compiled entries");
    return pm_emit_null(mt);
}

// ============================================================================
// Expression dispatcher
// ============================================================================

static MIR_reg_t pm_transpile_expression(PyMirTranspiler* mt, PyAstNode* expr) {
    if (!expr) return pm_emit_null(mt);

    switch (expr->node_type) {
    case PY_AST_NODE_LITERAL:
        return pm_transpile_literal(mt, (PyLiteralNode*)expr);
    case PY_AST_NODE_IDENTIFIER:
        return pm_transpile_identifier(mt, (PyIdentifierNode*)expr);
    case PY_AST_NODE_BINARY_OP:
        return pm_transpile_binary(mt, (PyBinaryNode*)expr);
    case PY_AST_NODE_UNARY_OP:
        return pm_transpile_unary(mt, (PyUnaryNode*)expr);
    case PY_AST_NODE_NOT:
        return pm_transpile_not(mt, (PyUnaryNode*)expr);
    case PY_AST_NODE_BOOLEAN_OP:
        return pm_transpile_boolean(mt, (PyBooleanNode*)expr);
    case PY_AST_NODE_COMPARE:
        return pm_transpile_compare(mt, (PyCompareNode*)expr);
    case PY_AST_NODE_CALL:
        return pm_transpile_call(mt, (PyCallNode*)expr);
    case PY_AST_NODE_ATTRIBUTE:
        return pm_transpile_attribute(mt, (PyAttributeNode*)expr);
    case PY_AST_NODE_SUBSCRIPT:
        return pm_transpile_subscript(mt, (PySubscriptNode*)expr);
    case PY_AST_NODE_LIST:
        return pm_transpile_list(mt, (PySequenceNode*)expr);
    case PY_AST_NODE_TUPLE:
        return pm_transpile_tuple(mt, (PySequenceNode*)expr);
    case PY_AST_NODE_SET:
        return pm_transpile_list(mt, (PySequenceNode*)expr); // sets as lists for now
    case PY_AST_NODE_DICT:
        return pm_transpile_dict(mt, (PyDictNode*)expr);
    case PY_AST_NODE_CONDITIONAL_EXPR:
        return pm_transpile_conditional(mt, (PyConditionalNode*)expr);
    case PY_AST_NODE_FSTRING:
        return pm_transpile_fstring(mt, (PyFStringNode*)expr);
    case PY_AST_NODE_LIST_COMPREHENSION:
        return pm_transpile_list_comprehension(mt, (PyComprehensionNode*)expr);
    case PY_AST_NODE_DICT_COMPREHENSION:
        return pm_transpile_dict_comprehension(mt, (PyComprehensionNode*)expr);
    case PY_AST_NODE_SET_COMPREHENSION:
        return pm_transpile_list_comprehension(mt, (PyComprehensionNode*)expr); // sets as lists
    case PY_AST_NODE_LAMBDA:
        return pm_transpile_lambda(mt, (PyLambdaNode*)expr);
    case PY_AST_NODE_YIELD: {
        PyYieldNode* yn = (PyYieldNode*)expr;
        if (!mt->in_generator) {
            // yield outside a generator — just evaluate the value (shouldn't happen)
            return yn->value ? pm_transpile_expression(mt, yn->value) : pm_emit_null(mt);
        }
        if (yn->is_from) {
            // yield from: delegate all yields to sub-iterator
            char iter_vname[128];
            snprintf(iter_vname, sizeof(iter_vname), "_py__git_%d", mt->gen_iter_count++);
            // evaluate sub-iterable and create iterator
            MIR_reg_t sub_expr = yn->value ? pm_transpile_expression(mt, yn->value) : pm_emit_null(mt);
            MIR_reg_t sub_iter = pm_call_1(mt, "py_get_iterator", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, sub_expr));
            // store in pre-registered gen_local
            PyMirVarEntry* sub_iter_var = pm_find_var(mt, iter_vname);
            if (sub_iter_var && !sub_iter_var->from_env) {
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                    MIR_new_reg_op(mt->em.ctx, sub_iter_var->reg),
                    MIR_new_reg_op(mt->em.ctx, sub_iter)));
            }
            // save all locals and enter the yield-from loop state
            pm_gen_emit_save(mt);
            int yf_state = mt->gen_yield_count + 1;
            pm_gen_store_state(mt, yf_state);
            // jump to the loop label (also used as the dispatch target on resume)
            MIR_label_t l_yf_loop = mt->gen_yield_labels[mt->gen_yield_count];
            MIR_label_t l_yf_done = pm_new_label(mt);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_yf_loop)));
            // === the loop label (both initial entry and resume landing pad) ===
            pm_emit_label(mt, l_yf_loop);
            mt->gen_yield_count++;
            // restore locals (including sub_iter_var)
            pm_gen_emit_restore(mt);
            MIR_reg_t cur_iter = (sub_iter_var && !sub_iter_var->from_env) ? sub_iter_var->reg : sub_iter;
            MIR_reg_t sub_item = pm_call_1(mt, "py_iterator_next", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cur_iter));
            MIR_reg_t is_stop = pm_call_1(mt, "py_is_stop_iteration", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, sub_item));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                MIR_new_label_op(mt->em.ctx, l_yf_done),
                MIR_new_reg_op(mt->em.ctx, is_stop)));
            // yield sub_item to caller, loop back (same state)
            pm_gen_emit_save(mt);
            pm_gen_store_state(mt, yf_state);
            pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, sub_item)));
            pm_emit_label(mt, l_yf_done);
            // yield from complete — result is ItemNull (sub-generator return value not tracked)
            return pm_emit_null(mt);
        }
        // regular yield: evaluate value, save locals, set next state, return value
        MIR_reg_t yield_val = yn->value ? pm_transpile_expression(mt, yn->value) : pm_emit_null(mt);
        pm_gen_emit_save(mt);
        int next_state = mt->gen_yield_count + 1;
        pm_gen_store_state(mt, next_state);
        pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, yield_val)));
        // resume label: execution continues here on next next()/send() call
        pm_emit_label(mt, mt->gen_yield_labels[mt->gen_yield_count]);
        mt->gen_yield_count++;
        // restore locals from frame
        pm_gen_emit_restore(mt);
        // return the sent value (for use as expression result in: value = yield expr)
        MIR_reg_t sent_result = pm_new_reg(mt, "gen_sent_r", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, sent_result),
            MIR_new_reg_op(mt->em.ctx, mt->gen_sent_reg)));
        return sent_result;
    }
    case PY_AST_NODE_AWAIT: {
        // await expr (Phase D): drive sub-coroutine via yield-from loop, then get return value
        PyAwaitNode* aw = (PyAwaitNode*)expr;
        if (!mt->in_generator) {
            // await outside async context: drive the coroutine directly
            MIR_reg_t coro = aw->value ? pm_transpile_expression(mt, aw->value) : pm_emit_null(mt);
            return pm_call_1(mt, "py_coro_drive", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, coro));
        }
        // inside async def: same as yield from but result = py_coro_get_return()
        char iter_vname[128];
        snprintf(iter_vname, sizeof(iter_vname), "_py__git_%d", mt->gen_iter_count++);
        MIR_reg_t sub_expr = aw->value ? pm_transpile_expression(mt, aw->value) : pm_emit_null(mt);
        MIR_reg_t sub_iter = pm_call_1(mt, "py_get_iterator", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, sub_expr));
        PyMirVarEntry* sub_iter_var = pm_find_var(mt, iter_vname);
        if (sub_iter_var && !sub_iter_var->from_env) {
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, sub_iter_var->reg),
                MIR_new_reg_op(mt->em.ctx, sub_iter)));
        }
        pm_gen_emit_save(mt);
        int yf_state = mt->gen_yield_count + 1;
        pm_gen_store_state(mt, yf_state);
        MIR_label_t l_yf_loop = mt->gen_yield_labels[mt->gen_yield_count];
        MIR_label_t l_yf_done = pm_new_label(mt);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_yf_loop)));
        // === loop label: both initial entry and resume landing ===
        pm_emit_label(mt, l_yf_loop);
        mt->gen_yield_count++;
        pm_gen_emit_restore(mt);
        MIR_reg_t cur_iter = (sub_iter_var && !sub_iter_var->from_env) ? sub_iter_var->reg : sub_iter;
        MIR_reg_t sub_item = pm_call_1(mt, "py_iterator_next", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cur_iter));
        MIR_reg_t is_stop = pm_call_1(mt, "py_is_stop_iteration", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, sub_item));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
            MIR_new_label_op(mt->em.ctx, l_yf_done),
            MIR_new_reg_op(mt->em.ctx, is_stop)));
        pm_gen_emit_save(mt);
        pm_gen_store_state(mt, yf_state);
        pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, sub_item)));
        pm_emit_label(mt, l_yf_done);
        // retrieve the return value stored by the sub-coroutine
        return pm_call_0(mt, "py_coro_get_return", MIR_T_I64);
    }
    default:
        log_error("py-mir: unsupported expression type %d", expr->node_type);
        return pm_emit_null(mt);
    }
}

// ============================================================================
// Statement transpilers
// ============================================================================

static void pm_transpile_assignment(PyMirTranspiler* mt, PyAssignmentNode* assign) {
    // infer type of RHS for type propagation
    TypeId rhs_type = pm_get_effective_type(mt, assign->right);
    MIR_reg_t val = pm_transpile_expression(mt, assign->right);

    // walk targets
    PyAstNode* target = assign->left;
    while (target) {
        if (target->node_type == PY_AST_NODE_IDENTIFIER) {
            PyIdentifierNode* id = (PyIdentifierNode*)target;
            char vname[128];
            snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);

            // check if variable is declared global in current function
            if (pm_is_global_in_current_func(mt, vname)) {
                int idx = pm_get_global_var_index(mt, vname);
                if (idx >= 0) {
                    pm_call_void_2(mt, "py_set_module_var",
                        MIR_T_I64, MIR_new_int_op(mt->em.ctx, idx),
                        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
                    target = target->next;
                    continue;
                }
            }

            PyMirVarEntry* var = pm_find_var(mt, vname);
            if (var) {
                if (var->from_env) {
                    pm_store_env_slot(mt, var->env_reg, var->env_slot, val);
                } else {
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                        MIR_new_reg_op(mt->em.ctx, var->reg),
                        MIR_new_reg_op(mt->em.ctx, val)));
                    // update type hint
                    if (rhs_type == LMD_TYPE_INT || rhs_type == LMD_TYPE_FLOAT)
                        var->type_id = rhs_type;
                    else
                        var->type_id = LMD_TYPE_NULL; // unknown, clear previous hint
                }
            } else {
                // new variable
                MIR_reg_t reg = pm_new_reg(mt, vname, MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                    MIR_new_reg_op(mt->em.ctx, reg),
                    MIR_new_reg_op(mt->em.ctx, val)));
                pm_set_var_with_type(mt, vname, reg, rhs_type);
            }
        } else if (target->node_type == PY_AST_NODE_SUBSCRIPT) {
            PySubscriptNode* sub = (PySubscriptNode*)target;
            MIR_reg_t obj = pm_transpile_expression(mt, sub->object);
            if (sub->index && sub->index->node_type == PY_AST_NODE_SLICE) {
                // slice assignment: obj[start:stop:step] = val
                PySliceNode* slice = (PySliceNode*)sub->index;
                MIR_reg_t start = slice->start ? pm_transpile_expression(mt, slice->start) : pm_emit_null(mt);
                MIR_reg_t stop  = slice->stop  ? pm_transpile_expression(mt, slice->stop)  : pm_emit_null(mt);
                MIR_reg_t step  = slice->step  ? pm_transpile_expression(mt, slice->step)  : pm_emit_null(mt);
                pm_call_5(mt, "py_slice_set", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, start),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, stop),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, step),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
            } else {
                MIR_reg_t key = pm_transpile_expression(mt, sub->index);
                pm_call_3(mt, "py_subscript_set", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
            }
        } else if (target->node_type == PY_AST_NODE_ATTRIBUTE) {
            PyAttributeNode* attr = (PyAttributeNode*)target;
            MIR_reg_t obj = pm_transpile_expression(mt, attr->object);
            MIR_reg_t name = pm_box_string_literal(mt, attr->attribute->chars, attr->attribute->len);
            pm_call_3(mt, "py_setattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, name),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
        } else if (target->node_type == PY_AST_NODE_TUPLE_UNPACK || target->node_type == PY_AST_NODE_TUPLE) {
            // tuple unpacking: a, b = expr
            PySequenceNode* tup = (PySequenceNode*)target;
            PyAstNode* elem = tup->elements;
            int idx = 0;
            while (elem) {
                MIR_reg_t item_val = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pm_box_int_const(mt, idx)));

                if (elem->node_type == PY_AST_NODE_IDENTIFIER) {
                    PyIdentifierNode* eid = (PyIdentifierNode*)elem;
                    char evname[128];
                    snprintf(evname, sizeof(evname), "_py_%.*s", (int)eid->name->len, eid->name->chars);
                    PyMirVarEntry* evar = pm_find_var(mt, evname);
                    if (evar) {
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                            MIR_new_reg_op(mt->em.ctx, evar->reg),
                            MIR_new_reg_op(mt->em.ctx, item_val)));
                    } else {
                        MIR_reg_t reg = pm_new_reg(mt, evname, MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                            MIR_new_reg_op(mt->em.ctx, reg),
                            MIR_new_reg_op(mt->em.ctx, item_val)));
                        pm_set_var(mt, evname, reg);
                    }
                }
                elem = elem->next;
                idx++;
            }
        }
        target = target->next;
    }
}

static void pm_transpile_aug_assignment(PyMirTranspiler* mt, PyAugAssignmentNode* aug) {
    // convert op to binary op
    PyOperator bin_op;
    switch (aug->op) {
    case PY_OP_ADD_ASSIGN:       bin_op = PY_OP_ADD; break;
    case PY_OP_SUB_ASSIGN:       bin_op = PY_OP_SUB; break;
    case PY_OP_MUL_ASSIGN:       bin_op = PY_OP_MUL; break;
    case PY_OP_DIV_ASSIGN:       bin_op = PY_OP_DIV; break;
    case PY_OP_FLOOR_DIV_ASSIGN: bin_op = PY_OP_FLOOR_DIV; break;
    case PY_OP_MOD_ASSIGN:       bin_op = PY_OP_MOD; break;
    case PY_OP_POW_ASSIGN:       bin_op = PY_OP_POW; break;
    case PY_OP_LSHIFT_ASSIGN:    bin_op = PY_OP_LSHIFT; break;
    case PY_OP_RSHIFT_ASSIGN:    bin_op = PY_OP_RSHIFT; break;
    case PY_OP_BIT_AND_ASSIGN:   bin_op = PY_OP_BIT_AND; break;
    case PY_OP_BIT_OR_ASSIGN:    bin_op = PY_OP_BIT_OR; break;
    case PY_OP_BIT_XOR_ASSIGN:   bin_op = PY_OP_BIT_XOR; break;
    default:                     bin_op = PY_OP_ADD; break;
    }

    // Phase 1: native int augmented assignment for simple identifier targets
    if (aug->left->node_type == PY_AST_NODE_IDENTIFIER && bin_op != PY_OP_DIV && bin_op != PY_OP_POW) {
        TypeId lt = pm_get_effective_type(mt, aug->left);
        TypeId rt = pm_get_effective_type(mt, aug->right);
        if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
            MIR_insn_code_t op;
            bool native_ok = true;
            switch (bin_op) {
            case PY_OP_ADD: op = MIR_ADD; break;
            case PY_OP_SUB: op = MIR_SUB; break;
            case PY_OP_MUL: op = MIR_MUL; break;
            case PY_OP_FLOOR_DIV: op = MIR_DIV; break;
            case PY_OP_MOD: op = MIR_MOD; break;
            case PY_OP_LSHIFT: op = MIR_LSH; break;
            case PY_OP_RSHIFT: op = MIR_RSH; break;
            case PY_OP_BIT_AND: op = MIR_AND; break;
            case PY_OP_BIT_OR: op = MIR_OR; break;
            case PY_OP_BIT_XOR: op = MIR_XOR; break;
            default: native_ok = false; break;
            }
            if (native_ok) {
                PyIdentifierNode* id = (PyIdentifierNode*)aug->left;
                char vname[128];
                snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);

                if (!pm_is_global_in_current_func(mt, vname)) {
                    PyMirVarEntry* var = pm_find_var(mt, vname);
                    if (var && !var->from_env && var->type_id == LMD_TYPE_INT) {
                        const char* fn = pm_binary_op_func(bin_op);
                        bool can_overflow = (bin_op == PY_OP_ADD || bin_op == PY_OP_SUB ||
                                             bin_op == PY_OP_MUL || bin_op == PY_OP_LSHIFT);

                        // transpile RHS as boxed Item first (needed for fallback)
                        MIR_reg_t rhs_boxed = pm_transpile_expression(mt, aug->right);

                        MIR_label_t l_boxed_path = pm_new_label(mt);
                        MIR_label_t l_end = pm_new_label(mt);

                        // runtime type guard: verify variable is actually INT
                        MIR_reg_t var_tag = pm_new_reg(mt, "vt", MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_RSH, MIR_new_reg_op(mt->em.ctx, var_tag),
                            MIR_new_reg_op(mt->em.ctx, var->reg), MIR_new_int_op(mt->em.ctx, 56)));
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BNE, MIR_new_label_op(mt->em.ctx, l_boxed_path),
                            MIR_new_reg_op(mt->em.ctx, var_tag), MIR_new_int_op(mt->em.ctx, (int64_t)LMD_TYPE_INT)));
                        // runtime type guard: verify RHS is actually INT
                        MIR_reg_t rhs_tag = pm_new_reg(mt, "rhst", MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_RSH, MIR_new_reg_op(mt->em.ctx, rhs_tag),
                            MIR_new_reg_op(mt->em.ctx, rhs_boxed), MIR_new_int_op(mt->em.ctx, 56)));
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BNE, MIR_new_label_op(mt->em.ctx, l_boxed_path),
                            MIR_new_reg_op(mt->em.ctx, rhs_tag), MIR_new_int_op(mt->em.ctx, (int64_t)LMD_TYPE_INT)));

                        // native path: unbox and compute
                        MIR_reg_t l = pm_emit_unbox_int(mt, var->reg);
                        MIR_reg_t r = pm_emit_unbox_int(mt, rhs_boxed);
                        MIR_reg_t res = pm_new_reg(mt, "naug", MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, op,
                            MIR_new_reg_op(mt->em.ctx, res),
                            MIR_new_reg_op(mt->em.ctx, l),
                            MIR_new_reg_op(mt->em.ctx, r)));

                        if (can_overflow) {
                            int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
                            int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;
                            MIR_reg_t le_max = pm_new_reg(mt, "le", MIR_T_I64);
                            MIR_reg_t ge_min = pm_new_reg(mt, "ge", MIR_T_I64);
                            MIR_reg_t in_range = pm_new_reg(mt, "rng", MIR_T_I64);
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_LE, MIR_new_reg_op(mt->em.ctx, le_max),
                                MIR_new_reg_op(mt->em.ctx, res), MIR_new_int_op(mt->em.ctx, INT56_MAX_VAL)));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_GE, MIR_new_reg_op(mt->em.ctx, ge_min),
                                MIR_new_reg_op(mt->em.ctx, res), MIR_new_int_op(mt->em.ctx, INT56_MIN_VAL)));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_AND, MIR_new_reg_op(mt->em.ctx, in_range),
                                MIR_new_reg_op(mt->em.ctx, le_max), MIR_new_reg_op(mt->em.ctx, ge_min)));
                            MIR_label_t l_ok = pm_new_label(mt);
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_ok),
                                MIR_new_reg_op(mt->em.ctx, in_range)));
                            // overflow → runtime call with boxed operands
                            MIR_reg_t ov_result = pm_call_2(mt, fn, MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, var->reg),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rhs_boxed));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                                MIR_new_reg_op(mt->em.ctx, var->reg),
                                MIR_new_reg_op(mt->em.ctx, ov_result)));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
                            // fast path: inline box
                            pm_emit_label(mt, l_ok);
                            MIR_reg_t masked = pm_new_reg(mt, "mask", MIR_T_I64);
                            MIR_reg_t tagged = pm_new_reg(mt, "tag", MIR_T_I64);
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_AND, MIR_new_reg_op(mt->em.ctx, masked),
                                MIR_new_reg_op(mt->em.ctx, res), MIR_new_int_op(mt->em.ctx, (int64_t)PY_MASK56)));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_OR, MIR_new_reg_op(mt->em.ctx, tagged),
                                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_INT_TAG), MIR_new_reg_op(mt->em.ctx, masked)));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                                MIR_new_reg_op(mt->em.ctx, var->reg),
                                MIR_new_reg_op(mt->em.ctx, tagged)));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
                        } else {
                            MIR_reg_t boxed = pm_box_int_reg(mt, res);
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                                MIR_new_reg_op(mt->em.ctx, var->reg),
                                MIR_new_reg_op(mt->em.ctx, boxed)));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
                        }

                        // boxed fallback: runtime call (wrong type or overflow)
                        pm_emit_label(mt, l_boxed_path);
                        MIR_reg_t boxed_res = pm_call_2(mt, fn, MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, var->reg),
                            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rhs_boxed));
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                            MIR_new_reg_op(mt->em.ctx, var->reg),
                            MIR_new_reg_op(mt->em.ctx, boxed_res)));
                        pm_emit_label(mt, l_end);
                        return;
                    }
                }
            }
        }
    }

    MIR_reg_t left = pm_transpile_expression(mt, aug->left);
    MIR_reg_t right = pm_transpile_expression(mt, aug->right);
    const char* fn = pm_binary_op_func(bin_op);
    MIR_reg_t val = pm_call_2(mt, fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, right));

    // assign back
    if (aug->left->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)aug->left;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);

        // check if variable is declared global in current function
        if (pm_is_global_in_current_func(mt, vname)) {
            int idx = pm_get_global_var_index(mt, vname);
            if (idx >= 0) {
                pm_call_void_2(mt, "py_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, idx),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
                return;
            }
        }

        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var) {
            if (var->from_env) {
                pm_store_env_slot(mt, var->env_reg, var->env_slot, val);
            } else {
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                    MIR_new_reg_op(mt->em.ctx, var->reg),
                    MIR_new_reg_op(mt->em.ctx, val)));
            }
        }
    } else if (aug->left->node_type == PY_AST_NODE_SUBSCRIPT) {
        PySubscriptNode* sub = (PySubscriptNode*)aug->left;
        MIR_reg_t obj = pm_transpile_expression(mt, sub->object);
        MIR_reg_t key = pm_transpile_expression(mt, sub->index);
        pm_call_3(mt, "py_subscript_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
    } else if (aug->left->node_type == PY_AST_NODE_ATTRIBUTE) {
        // self.x += val → py_setattr(self, "x", val)
        PyAttributeNode* attr = (PyAttributeNode*)aug->left;
        MIR_reg_t obj = pm_transpile_expression(mt, attr->object);
        MIR_reg_t attr_name = pm_box_string_literal(mt, attr->attribute->chars, attr->attribute->len);
        pm_call_3(mt, "py_setattr", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, attr_name),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val));
    }
}

static void pm_transpile_if(PyMirTranspiler* mt, PyIfNode* ifn) {
    MIR_reg_t test = pm_transpile_expression(mt, ifn->test);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, test));

    MIR_label_t l_else = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_else),
        MIR_new_reg_op(mt->em.ctx, truthy)));

    // then body
    if (ifn->body) {
        PyAstNode* s;
        if (ifn->body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)ifn->body)->statements;
        } else {
            s = ifn->body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));

    pm_emit_label(mt, l_else);

    // elif clauses
    PyAstNode* elif = ifn->elif_clauses;
    while (elif) {
        if (elif->node_type == PY_AST_NODE_ELIF) {
            PyIfNode* elif_node = (PyIfNode*)elif;
            MIR_reg_t elif_test = pm_transpile_expression(mt, elif_node->test);
            MIR_reg_t elif_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, elif_test));
            MIR_label_t l_next = pm_new_label(mt);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_next),
                MIR_new_reg_op(mt->em.ctx, elif_truthy)));

            if (elif_node->body) {
                PyAstNode* s;
                if (elif_node->body->node_type == PY_AST_NODE_BLOCK) {
                    s = ((PyBlockNode*)elif_node->body)->statements;
                } else {
                    s = elif_node->body;
                }
                while (s) { pm_transpile_statement(mt, s); s = s->next; }
            }
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_end)));
            pm_emit_label(mt, l_next);
        }
        elif = elif->next;
    }

    // else body
    if (ifn->else_body) {
        PyAstNode* s;
        if (ifn->else_body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)ifn->else_body)->statements;
        } else {
            s = ifn->else_body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }

    pm_emit_label(mt, l_end);
}

static void pm_transpile_while(PyMirTranspiler* mt, PyWhileNode* wh) {
    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    if (!pm_require_capacity(mt, mt->loop_depth, PM_MAX_LOOPS, "nested loops")) return;
    mt->loop_stack[mt->loop_depth].continue_label = l_start;
    mt->loop_stack[mt->loop_depth].break_label = l_end;
    mt->loop_depth++;

    pm_emit_label(mt, l_start);

    MIR_reg_t test = pm_transpile_expression(mt, wh->test);
    MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, test));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF, MIR_new_label_op(mt->em.ctx, l_end),
        MIR_new_reg_op(mt->em.ctx, truthy)));

    if (wh->body) {
        PyAstNode* s;
        if (wh->body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)wh->body)->statements;
        } else {
            s = wh->body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }

    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_start)));
    pm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
}

// detect if a call expression is range(...)
static bool pm_is_range_call(PyAstNode* iter) {
    if (!iter || iter->node_type != PY_AST_NODE_CALL) return false;
    PyCallNode* call = (PyCallNode*)iter;
    if (!call->function || call->function->node_type != PY_AST_NODE_IDENTIFIER) return false;
    PyIdentifierNode* fn_id = (PyIdentifierNode*)call->function;
    return fn_id->name->len == 5 && strncmp(fn_id->name->chars, "range", 5) == 0 &&
           call->arg_count >= 1 && call->arg_count <= 3;
}

static void pm_transpile_for(PyMirTranspiler* mt, PyForNode* forn) {
    // in generator context, use iterator protocol to survive yields inside the loop
    if (mt->in_generator) {
        pm_transpile_for_gen(mt, forn);
        return;
    }

    // Phase 3: optimized range-for loop with native counter
    if (forn->left->node_type == PY_AST_NODE_IDENTIFIER && pm_is_range_call(forn->right)) {
        PyCallNode* call = (PyCallNode*)forn->right;
        PyIdentifierNode* target_id = (PyIdentifierNode*)forn->left;

        // extract range arguments: range(stop), range(start, stop), range(start, stop, step)
        PyAstNode* arg1 = call->arguments;
        PyAstNode* arg2 = arg1 ? arg1->next : NULL;
        PyAstNode* arg3 = arg2 ? arg2->next : NULL;

        MIR_reg_t start_reg, stop_reg, step_reg;
        if (call->arg_count == 1) {
            // range(stop): start=0, step=1
            start_reg = pm_new_reg(mt, "rstart", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, start_reg),
                MIR_new_int_op(mt->em.ctx, 0)));
            stop_reg = pm_transpile_as_native_int(mt, arg1);
            step_reg = pm_new_reg(mt, "rstep", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, step_reg),
                MIR_new_int_op(mt->em.ctx, 1)));
        } else if (call->arg_count == 2) {
            // range(start, stop): step=1
            start_reg = pm_transpile_as_native_int(mt, arg1);
            stop_reg = pm_transpile_as_native_int(mt, arg2);
            step_reg = pm_new_reg(mt, "rstep", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, step_reg),
                MIR_new_int_op(mt->em.ctx, 1)));
        } else {
            // range(start, stop, step)
            start_reg = pm_transpile_as_native_int(mt, arg1);
            stop_reg = pm_transpile_as_native_int(mt, arg2);
            step_reg = pm_transpile_as_native_int(mt, arg3);
        }

        // counter register
        MIR_reg_t counter = pm_new_reg(mt, "rcnt", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, counter),
            MIR_new_reg_op(mt->em.ctx, start_reg)));

        // set loop var with INT type hint to enable native arithmetic inside the body
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)target_id->name->len, target_id->name->chars);

        // create or find the loop variable register
        MIR_reg_t var_reg;
        PyMirVarEntry* existing_var = pm_find_var(mt, vname);
        if (existing_var) {
            var_reg = existing_var->reg;
        } else {
            var_reg = pm_new_reg(mt, vname, MIR_T_I64);
        }

        MIR_label_t l_test = pm_new_label(mt);
        MIR_label_t l_continue = pm_new_label(mt);
        MIR_label_t l_end = pm_new_label(mt);

        if (!pm_require_capacity(mt, mt->loop_depth, PM_MAX_LOOPS, "nested loops")) return;
        mt->loop_stack[mt->loop_depth].continue_label = l_continue;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;

        pm_emit_label(mt, l_test);

        // test: counter < stop (for positive step) or counter > stop (for negative step)
        // for simplicity and common case (step=1), use counter >= stop → exit
        // for general case with variable step, check (counter - stop) * sign(step) >= 0
        // but for benchmarks, step is almost always 1, so check counter >= stop
        MIR_reg_t cmp_exit = pm_new_reg(mt, "rcmp", MIR_T_I64);
        if (call->arg_count <= 2) {
            // step is always 1: simple counter >= stop check
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_GE, MIR_new_reg_op(mt->em.ctx, cmp_exit),
                MIR_new_reg_op(mt->em.ctx, counter), MIR_new_reg_op(mt->em.ctx, stop_reg)));
        } else {
            // general step: check if (counter - stop) has same sign as step
            // use: step > 0 ? counter >= stop : counter <= stop
            MIR_reg_t step_pos = pm_new_reg(mt, "spos", MIR_T_I64);
            MIR_reg_t cmp_fwd = pm_new_reg(mt, "cfwd", MIR_T_I64);
            MIR_reg_t cmp_bwd = pm_new_reg(mt, "cbwd", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_GT, MIR_new_reg_op(mt->em.ctx, step_pos),
                MIR_new_reg_op(mt->em.ctx, step_reg), MIR_new_int_op(mt->em.ctx, 0)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_GE, MIR_new_reg_op(mt->em.ctx, cmp_fwd),
                MIR_new_reg_op(mt->em.ctx, counter), MIR_new_reg_op(mt->em.ctx, stop_reg)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_LE, MIR_new_reg_op(mt->em.ctx, cmp_bwd),
                MIR_new_reg_op(mt->em.ctx, counter), MIR_new_reg_op(mt->em.ctx, stop_reg)));
            // cmp_exit = step > 0 ? cmp_fwd : cmp_bwd
            MIR_label_t l_use_fwd = pm_new_label(mt);
            MIR_label_t l_cmp_done = pm_new_label(mt);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_use_fwd),
                MIR_new_reg_op(mt->em.ctx, step_pos)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, cmp_exit),
                MIR_new_reg_op(mt->em.ctx, cmp_bwd)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_cmp_done)));
            pm_emit_label(mt, l_use_fwd);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV, MIR_new_reg_op(mt->em.ctx, cmp_exit),
                MIR_new_reg_op(mt->em.ctx, cmp_fwd)));
            pm_emit_label(mt, l_cmp_done);
        }

        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_end),
            MIR_new_reg_op(mt->em.ctx, cmp_exit)));

        // box counter and assign to loop variable (with INT type hint)
        MIR_reg_t boxed_cnt = pm_box_int_reg(mt, counter);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, var_reg),
            MIR_new_reg_op(mt->em.ctx, boxed_cnt)));
        pm_set_var_with_type(mt, vname, var_reg, LMD_TYPE_INT);

        // body
        if (forn->body) {
            PyAstNode* s;
            if (forn->body->node_type == PY_AST_NODE_BLOCK) {
                s = ((PyBlockNode*)forn->body)->statements;
            } else {
                s = forn->body;
            }
            while (s) { pm_transpile_statement(mt, s); s = s->next; }
        }

        pm_emit_label(mt, l_continue);

        // increment counter natively: counter += step
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_ADD,
            MIR_new_reg_op(mt->em.ctx, counter),
            MIR_new_reg_op(mt->em.ctx, counter),
            MIR_new_reg_op(mt->em.ctx, step_reg)));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_test)));
        pm_emit_label(mt, l_end);

        if (mt->loop_depth > 0) mt->loop_depth--;
        return;
    }

    // general for loop path: evaluate iterable and get an iterator object
    MIR_reg_t iterable = pm_transpile_expression(mt, forn->right);
    MIR_reg_t iter_obj = pm_call_1(mt, "py_get_iterator", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iterable));

    MIR_label_t l_start = pm_new_label(mt);
    MIR_label_t l_continue = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    if (!pm_require_capacity(mt, mt->loop_depth, PM_MAX_LOOPS, "nested loops")) return;
    mt->loop_stack[mt->loop_depth].continue_label = l_continue;
    mt->loop_stack[mt->loop_depth].break_label = l_end;
    mt->loop_depth++;

    pm_emit_label(mt, l_start);

    // get next item; end loop on StopIteration
    MIR_reg_t item = pm_call_1(mt, "py_iterator_next", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iter_obj));
    MIR_reg_t is_stop = pm_call_1(mt, "py_is_stop_iteration", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, item));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
        MIR_new_label_op(mt->em.ctx, l_end),
        MIR_new_reg_op(mt->em.ctx, is_stop)));

    // assign to loop variable
    if (forn->left->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)forn->left;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var) {
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, var->reg),
                MIR_new_reg_op(mt->em.ctx, item)));
        } else {
            MIR_reg_t reg = pm_new_reg(mt, vname, MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, reg),
                MIR_new_reg_op(mt->em.ctx, item)));
            pm_set_var(mt, vname, reg);
        }
    } else if (forn->left->node_type == PY_AST_NODE_TUPLE ||
               forn->left->node_type == PY_AST_NODE_TUPLE_UNPACK) {
        // tuple unpacking in for loop: for a, b in list_of_tuples
        PySequenceNode* tup = (PySequenceNode*)forn->left;
        PyAstNode* elem = tup->elements;
        int i = 0;
        while (elem) {
            MIR_reg_t sub_item = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, item),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pm_box_int_const(mt, i)));
            if (elem->node_type == PY_AST_NODE_IDENTIFIER) {
                PyIdentifierNode* eid = (PyIdentifierNode*)elem;
                char evname[128];
                snprintf(evname, sizeof(evname), "_py_%.*s", (int)eid->name->len, eid->name->chars);
                PyMirVarEntry* evar = pm_find_var(mt, evname);
                if (evar) {
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                        MIR_new_reg_op(mt->em.ctx, evar->reg),
                        MIR_new_reg_op(mt->em.ctx, sub_item)));
                } else {
                    MIR_reg_t reg = pm_new_reg(mt, evname, MIR_T_I64);
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                        MIR_new_reg_op(mt->em.ctx, reg),
                        MIR_new_reg_op(mt->em.ctx, sub_item)));
                    pm_set_var(mt, evname, reg);
                }
            }
            elem = elem->next;
            i++;
        }
    }

    // body
    if (forn->body) {
        PyAstNode* s;
        if (forn->body->node_type == PY_AST_NODE_BLOCK) {
            s = ((PyBlockNode*)forn->body)->statements;
        } else {
            s = forn->body;
        }
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
    }

    pm_emit_label(mt, l_continue);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_start)));
    pm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
}

static void pm_transpile_return(PyMirTranspiler* mt, PyReturnNode* ret) {
    if (mt->in_generator) {
        // return inside a generator/coroutine: mark exhausted and return stop_iteration
        if (mt->in_async && ret->value) {
            // async def return X: store return value via side-channel before exhaustion
            MIR_reg_t rval = pm_transpile_expression(mt, ret->value);
            pm_call_1(mt, "py_coro_set_return", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rval));
        }
        pm_gen_store_state(mt, -1);
        MIR_reg_t si = pm_call_0(mt, "py_stop_iteration", MIR_T_I64);
        pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, si)));
        return;
    }
    // TCO: return value is in tail position
    bool saved_tail = mt->in_tail_position;
    mt->block_returned = false;  // reset per-return so earlier TCO jumps don't suppress this ret
    if (mt->tco_func) {
        mt->in_tail_position = true;
    }
    MIR_reg_t val;
    if (ret->value) {
        val = pm_transpile_expression(mt, ret->value);
    } else {
        val = pm_emit_null(mt);
    }
    mt->in_tail_position = saved_tail;
    // if TCO converted the call to a jump, block_returned is set — skip the ret
    if (mt->block_returned) return;
    pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, val)));
}

static void pm_transpile_block(PyMirTranspiler* mt, PyAstNode* body) {
    if (!body) return;
    PyAstNode* s;
    if (body->node_type == PY_AST_NODE_BLOCK) {
        s = ((PyBlockNode*)body)->statements;
    } else {
        s = body;
    }
    while (s) { pm_transpile_statement(mt, s); s = s->next; }
}

// ============================================================================
// Decorator application
// ============================================================================

// Apply a decorator list (bottom-to-top) to a value and return the final result.
// Decorators are linked via ->next in source order; bottom-to-top means the last
// decorator in the list wraps first (inner-most) and the first wraps last (outer).
static MIR_reg_t pm_apply_decorators(PyMirTranspiler* mt, MIR_reg_t value, PyAstNode* decorators) {
    if (!decorators) return value;

    // collect into small fixed array so we can iterate in reverse
    const int MAX_DECS = 16;
    PyAstNode* dec_list[MAX_DECS];
    int dec_count = 0;
    PyAstNode* d = decorators;
    while (d && dec_count < MAX_DECS) {
        dec_list[dec_count++] = d;
        d = d->next;
    }

    // apply bottom-to-top: last decorator in source order wraps the function first
    for (int i = dec_count - 1; i >= 0; i--) {
        PyDecoratorNode* dn = (PyDecoratorNode*)dec_list[i];

        // Special case: @property — call py_builtin_property(value) directly,
        // because 'property' as a bare identifier has no runtime binding.
        if (dn->expression && dn->expression->node_type == PY_AST_NODE_IDENTIFIER) {
            PyIdentifierNode* id = (PyIdentifierNode*)dn->expression;
            if (id->name->len == 8 && strncmp(id->name->chars, "property", 8) == 0) {
                value = pm_call_1(mt, "py_builtin_property", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, value));
                continue;
            }
        }

        MIR_reg_t dec_val = pm_transpile_expression(mt, dn->expression);

        // decorator values can allocate, so their positional argument must stay rooted.
        PmArgScope args = pm_begin_arg_scope(mt, 1);
        pm_arg_scope_store(mt, args, 0, value);
        value = pm_call_3(mt, "py_call_function", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, dec_val),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, args.args),
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, 1));
        pm_end_arg_scope(mt, args);
    }
    return value;
}

// ============================================================================
// Phase B: match/case codegen
// ============================================================================

// Forward declaration
static void pm_compile_pattern(PyMirTranspiler* mt, MIR_reg_t subj,
                                PyPatternNode* pat, MIR_label_t l_fail);

// Emit code to resolve a dotted value name (e.g., "Status.OK") and return its reg.
// Split on '.' and chain py_getattr calls.
static MIR_reg_t pm_resolve_dotted_name(PyMirTranspiler* mt, const char* dotted, int len) {
    // find first dot
    int dot = -1;
    for (int i = 0; i < len; i++) {
        if (dotted[i] == '.') { dot = i; break; }
    }
    if (dot < 0) {
        // single name: look up as variable
        char vname[130]; snprintf(vname, sizeof(vname), "_py_%.*s", len, dotted);
        // check local/closure scope first
        PyMirVarEntry* v = pm_find_var(mt, vname);
        if (v && !v->from_env) return v->reg;
        if (v && v->from_env) return pm_load_env_slot(mt, v->env_reg, v->env_slot);
        // check module-level vars (includes classes scanned in pm_scan_module_vars)
        int midx = pm_get_global_var_index(mt, vname);
        if (midx >= 0) {
            return pm_call_1(mt, "py_get_module_var", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, midx));
        }
        // fallback: builtin class lookup by name
        return pm_call_1(mt, "py_resolve_name_item", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pm_box_string_literal(mt, dotted, len)));
    }
    MIR_reg_t obj = pm_resolve_dotted_name(mt, dotted, dot);
    const char* rest = dotted + dot + 1;
    int rlen = len - dot - 1;
    MIR_reg_t attr_r = pm_box_string_literal(mt, rest, rlen);
    return pm_call_2(mt, "py_getattr", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, attr_r));
}

// Count elements in a linked list
static int pm_count_nodes(PyAstNode* head) {
    int n = 0;
    for (PyAstNode* s = head; s; s = s->next) n++;
    return n;
}

// Bind a capture variable in the current scope
static void pm_bind_capture(PyMirTranspiler* mt, String* name, MIR_reg_t val_reg) {
    char vname[130];
    snprintf(vname, sizeof(vname), "_py_%.*s", (int)name->len, name->chars);
    MIR_reg_t r = pm_new_reg(mt, vname, MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
        MIR_new_reg_op(mt->em.ctx, r),
        MIR_new_reg_op(mt->em.ctx, val_reg)));
    pm_set_var(mt, vname, r);
}

// Emit code to match subject against a sequence pattern.
// l_fail: jumped to on mismatch.
static void pm_compile_sequence_pattern(PyMirTranspiler* mt, MIR_reg_t subj,
                                         PyPatternNode* pat, MIR_label_t l_fail) {
    // 1. Check it's a list/tuple (not string)
    MIR_reg_t seq_ok = pm_call_1(mt, "py_match_is_sequence", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj));
    MIR_reg_t seq_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, seq_ok));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
        MIR_new_label_op(mt->em.ctx, l_fail),
        MIR_new_reg_op(mt->em.ctx, seq_t)));

    // Count elements
    int n_elem = pm_count_nodes(pat->elements);
    bool has_star = (pat->rest_name != NULL || pat->rest_pos >= 0);
    int star_pos  = pat->rest_pos;  // -1 = no star; >= 0 = star after index star_pos
    if (!has_star) star_pos = -1;
    int n_before = (star_pos >= 0) ? star_pos : n_elem;
    int n_after  = (star_pos >= 0) ? (n_elem - n_before) : 0;

    // 2. Get length
    MIR_reg_t len_reg = pm_call_1(mt, "py_builtin_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj));

    if (!has_star) {
        // exact length check
        MIR_reg_t exp_len = pm_box_int_const(mt, n_elem);
        MIR_reg_t eq_r = pm_call_2(mt, "py_eq", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, len_reg),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exp_len));
        MIR_reg_t eq_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, eq_r));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
            MIR_new_label_op(mt->em.ctx, l_fail),
            MIR_new_reg_op(mt->em.ctx, eq_t)));
    } else {
        // len >= n_before + n_after
        MIR_reg_t min_len = pm_box_int_const(mt, n_before + n_after);
        MIR_reg_t ge_r = pm_call_2(mt, "py_ge", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, len_reg),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, min_len));
        MIR_reg_t ge_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, ge_r));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
            MIR_new_label_op(mt->em.ctx, l_fail),
            MIR_new_reg_op(mt->em.ctx, ge_t)));
    }

    // 3. Match each element
    int i = 0;
    for (PyAstNode* e = pat->elements; e; e = e->next, i++) {
        MIR_reg_t idx_r;
        if (star_pos >= 0 && i >= n_before) {
            // after star: index from end
            int offset = n_after - (i - n_before);
            MIR_reg_t off_r = pm_box_int_const(mt, offset);
            idx_r = pm_call_2(mt, "py_subtract", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, len_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, off_r));
        } else {
            idx_r = pm_box_int_const(mt, i);
        }
        MIR_reg_t elem_r = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, idx_r));
        pm_compile_pattern(mt, elem_r, (PyPatternNode*)e, l_fail);
    }

    // 4. Bind *rest if named
    if (has_star && pat->rest_name) {
        MIR_reg_t start_r = pm_box_int_const(mt, n_before);
        MIR_reg_t off_r   = pm_box_int_const(mt, n_after);
        MIR_reg_t end_r   = pm_call_2(mt, "py_subtract", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, len_reg),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, off_r));
        MIR_reg_t none_r  = pm_emit_null(mt);
        MIR_reg_t rest_r  = pm_call_4(mt, "py_slice_get", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, start_r),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, end_r),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, none_r));
        pm_bind_capture(mt, pat->rest_name, rest_r);
    }
}

// Compile a single pattern against subject register.
// Emits code that falls through on match, jumps to l_fail on mismatch.
// Capture variables are bound as local MIR registers.
static void pm_compile_pattern(PyMirTranspiler* mt, MIR_reg_t subj,
                                PyPatternNode* pat, MIR_label_t l_fail) {
    if (!pat) return;

    switch (pat->kind) {

    case PY_PAT_WILDCARD:
        // always matches, no binding
        break;

    case PY_PAT_CAPTURE:
        // always matches; bind subject to name
        if (pat->name) {
            pm_bind_capture(mt, pat->name, subj);
        }
        break;

    case PY_PAT_LITERAL: {
        MIR_reg_t lit_r;
        if (pat->literal) {
            lit_r = pm_transpile_expression(mt, pat->literal);
        } else {
            lit_r = pm_emit_null(mt);
        }
        if (pat->literal_neg) {
            lit_r = pm_call_1(mt, "py_negate", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, lit_r));
        }
        MIR_reg_t eq_r = pm_call_2(mt, "py_eq", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, lit_r));
        MIR_reg_t ok_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, eq_r));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
            MIR_new_label_op(mt->em.ctx, l_fail),
            MIR_new_reg_op(mt->em.ctx, ok_t)));
        break;
    }

    case PY_PAT_VALUE: {
        // Resolve dotted name as constant value and compare
        if (!pat->name) break;
        MIR_reg_t val_r = pm_resolve_dotted_name(mt, pat->name->chars, (int)pat->name->len);
        MIR_reg_t eq_r = pm_call_2(mt, "py_eq", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val_r));
        MIR_reg_t ok_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, eq_r));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
            MIR_new_label_op(mt->em.ctx, l_fail),
            MIR_new_reg_op(mt->em.ctx, ok_t)));
        break;
    }

    case PY_PAT_OR: {
        // Try each alternative; jump to l_match_end on first success
        MIR_label_t l_end = pm_new_label(mt);
        PyAstNode* alt = pat->elements;
        while (alt) {
            PyAstNode* next = alt->next;
            if (next) {
                // not the last: if this alternative fails, try next
                MIR_label_t l_next = pm_new_label(mt);
                pm_compile_pattern(mt, subj, (PyPatternNode*)alt, l_next);
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
                    MIR_new_label_op(mt->em.ctx, l_end)));
                pm_emit_label(mt, l_next);
            } else {
                // last alternative: fail goes to l_fail
                pm_compile_pattern(mt, subj, (PyPatternNode*)alt, l_fail);
            }
            alt = next;
        }
        pm_emit_label(mt, l_end);
        break;
    }

    case PY_PAT_AS: {
        // Match inner pattern, then bind alias
        if (pat->literal) {
            pm_compile_pattern(mt, subj, (PyPatternNode*)pat->literal, l_fail);
        }
        if (pat->name) {
            pm_bind_capture(mt, pat->name, subj);
        }
        break;
    }

    case PY_PAT_SEQUENCE: {
        pm_compile_sequence_pattern(mt, subj, pat, l_fail);
        break;
    }

    case PY_PAT_MAPPING: {
        // 1. Check subject is a mapping
        MIR_reg_t map_ok = pm_call_1(mt, "py_match_is_mapping", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj));
        MIR_reg_t map_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, map_ok));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
            MIR_new_label_op(mt->em.ctx, l_fail),
            MIR_new_reg_op(mt->em.ctx, map_t)));

        // 2. For each KV pair: check key exists, get value, match sub-pattern
        MIR_reg_t keys_list = pm_call_1(mt, "py_list_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, 0));
        for (PyAstNode* kv = pat->kv_pairs; kv; kv = kv->next) {
            PyPatternKVNode* kvn = (PyPatternKVNode*)kv;
            // key is a literal or value pattern — emit its expression
            MIR_reg_t key_r;
            if (kvn->key_pat) {
                PyPatternNode* kp = (PyPatternNode*)kvn->key_pat;
                if (kp->kind == PY_PAT_LITERAL && kp->literal) {
                    key_r = pm_transpile_expression(mt, kp->literal);
                } else if (kp->kind == PY_PAT_VALUE && kp->name) {
                    key_r = pm_resolve_dotted_name(mt, kp->name->chars, (int)kp->name->len);
                } else if (kp->kind == PY_PAT_CAPTURE && kp->name) {
                    key_r = pm_box_string_literal(mt, kp->name->chars, (int)kp->name->len);
                } else {
                    key_r = pm_emit_null(mt);
                }
            } else {
                key_r = pm_emit_null(mt);
            }
            // check key exists: py_contains(subject, key)
            MIR_reg_t has_r = pm_call_2(mt, "py_contains", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_r));
            MIR_reg_t has_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, has_r));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
                MIR_new_label_op(mt->em.ctx, l_fail),
                MIR_new_reg_op(mt->em.ctx, has_t)));
            // get value and match sub-pattern
            MIR_reg_t val_r = pm_call_2(mt, "py_dict_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_r));
            if (kvn->val_pat) {
                pm_compile_pattern(mt, val_r, (PyPatternNode*)kvn->val_pat, l_fail);
            }
            // track key for **rest
            pm_call_2(mt, "py_list_append", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, keys_list),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, key_r));
        }
        // 3. Bind **rest if present
        if (pat->rest_name) {
            MIR_reg_t rest_r = pm_call_2(mt, "py_match_mapping_rest", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, keys_list));
            pm_bind_capture(mt, pat->rest_name, rest_r);
        }
        break;
    }

    case PY_PAT_CLASS: {
        // 1. Resolve class name and check isinstance
        if (!pat->name) break;
        MIR_reg_t cls_r = pm_resolve_dotted_name(mt, pat->name->chars, (int)pat->name->len);
        MIR_reg_t inst_r = pm_call_2(mt, "py_builtin_isinstance", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cls_r));
        MIR_reg_t inst_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, inst_r));
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
            MIR_new_label_op(mt->em.ctx, l_fail),
            MIR_new_reg_op(mt->em.ctx, inst_t)));

        // 2. Positional patterns: use __match_args__ to get attribute names
        int pos = 0;
        for (PyAstNode* e = pat->elements; e; e = e->next, pos++) {
            // get __match_args__[pos] to find the attribute name
            MIR_reg_t match_args_r = pm_call_2(mt, "py_getattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cls_r),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pm_box_string_literal(mt, "__match_args__", 14)));
            MIR_reg_t pos_r = pm_box_int_const(mt, pos);
            MIR_reg_t attr_name_r = pm_call_2(mt, "py_subscript_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, match_args_r),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pos_r));
            MIR_reg_t attr_val_r = pm_call_2(mt, "py_getattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, attr_name_r));
            pm_compile_pattern(mt, attr_val_r, (PyPatternNode*)e, l_fail);
        }

        // 3. Keyword patterns: each is PAT_CAPTURE with name=attr and literal=sub-pattern
        for (PyAstNode* kw = pat->kv_pairs; kw; kw = kw->next) {
            PyPatternNode* kwp = (PyPatternNode*)kw;
            if (!kwp->name) continue;
            MIR_reg_t attr_r = pm_box_string_literal(mt, kwp->name->chars, (int)kwp->name->len);
            MIR_reg_t val_r = pm_call_2(mt, "py_getattr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, subj),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, attr_r));
            if (kwp->literal) {
                pm_compile_pattern(mt, val_r, (PyPatternNode*)kwp->literal, l_fail);
            } else {
                // bare keyword pattern: case Cls(attr=capture_name) — bind the attribute value
                if (kwp->name) pm_bind_capture(mt, kwp->name, val_r);  // captures attr value
                // Actually for class keyword patterns: the name IS the attribute to read,
                // and for a bare pattern (no sub-pattern), it's a capture. But PyPatternNode
                // stores capture name in `name` and sub-pattern in `literal`. So if literal ==
                // NULL, we just read the attribute to verify it doesn't throw but don't bind.
                // A real capture in class pattern would be: case C(x=y): where y is bound.
                // Actually, the sub-pattern is mandatory in keyword_pattern grammar: name = sub_pat.
                // So literal should always be present here.
            }
        }
        break;
    }

    case PY_PAT_STAR:
        // Should not appear at top level; handled inside SEQUENCE
        break;
    }
}

// Transpile a match statement
static void pm_transpile_match(PyMirTranspiler* mt, PyMatchNode* mn) {
    // evaluate subject
    MIR_reg_t subj = pm_transpile_expression(mt, mn->subject);

    MIR_label_t l_match_end = pm_new_label(mt);

    for (PyAstNode* c = mn->cases; c; c = c->next) {
        PyCaseNode* cn = (PyCaseNode*)c;
        if (!cn->pattern) continue;

        MIR_label_t l_case_fail = pm_new_label(mt);

        // push scope for captures within this case
        pm_push_scope(mt);

        // compile pattern — jumps to l_case_fail on mismatch
        pm_compile_pattern(mt, subj, (PyPatternNode*)cn->pattern, l_case_fail);

        // compile guard if present
        if (cn->guard) {
            MIR_reg_t g_r = pm_transpile_expression(mt, cn->guard);
            MIR_reg_t g_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, g_r));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
                MIR_new_label_op(mt->em.ctx, l_case_fail),
                MIR_new_reg_op(mt->em.ctx, g_t)));
        }

        // compile body
        pm_transpile_block(mt, cn->body);

        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
            MIR_new_label_op(mt->em.ctx, l_match_end)));

        // fail path: pattern or guard did not match
        pm_emit_label(mt, l_case_fail);

        // single pop covers both success and fail paths (scope pushed once per case)
        pm_pop_scope(mt);
    }

    pm_emit_label(mt, l_match_end);
}

// ============================================================================
// Package / module resolution helper (Phase E)
// ============================================================================

// Check if a file exists and is a regular file
static bool pm_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Count leading dots in a module name for relative imports
static int pm_count_leading_dots(const char* name, int len) {
    int count = 0;
    while (count < len && name[count] == '.') count++;
    return count;
}

// Try loading a module from a base path (without extension).
// Tries: base_path.py, base_path/__init__.py, base_path.js, base_path.ls
// Returns the loaded namespace Item or ItemNull.
static Item pm_try_load_module(void* host_execution, const char* base_path) {
    StrBuf* try_buf = strbuf_new();
    Item ns = ItemNull;

    // try base_path.py
    strbuf_append_format(try_buf, "%s.py", base_path);
    if (pm_file_exists(try_buf->str)) {
        // check circular import
        Item loading_namespace = pm_loading_module_namespace(host_execution, try_buf->str);
        if (loading_namespace.item != ItemNull.item) {
            log_info("py-pkg: circular import detected for '%s', returning partial namespace", try_buf->str);
            strbuf_free(try_buf);
            return loading_namespace;
        }
        // Python lowering selects a hosted source by path; the language
        // registry owns descriptor dispatch instead of embedding a Python
        // loader call in every import lowering site.
        jube_load_hosted_module(host_execution, try_buf->str, NULL, &ns);
        if (ns.item != ItemNull.item) {
            strbuf_free(try_buf);
            return ns;
        }
    }

    // try base_path/__init__.py (package directory)
    strbuf_reset(try_buf);
    strbuf_append_format(try_buf, "%s/__init__.py", base_path);
    if (pm_file_exists(try_buf->str)) {
        Item loading_namespace = pm_loading_module_namespace(host_execution, try_buf->str);
        if (loading_namespace.item != ItemNull.item) {
            log_info("py-pkg: circular import detected for '%s', returning partial namespace", try_buf->str);
            strbuf_free(try_buf);
            return loading_namespace;
        }
        jube_load_hosted_module(host_execution, try_buf->str, NULL, &ns);
        if (ns.item != ItemNull.item) {
            strbuf_free(try_buf);
            return ns;
        }
    }

    // Cross-language loading is routed through the hosted-language bridge so
    // Python never reaches into the JavaScript module loader directly.
    strbuf_reset(try_buf);
    strbuf_append_format(try_buf, "%s.js", base_path);
    if (pm_file_exists(try_buf->str)) {
        if (jube_load_language_module(host_execution, try_buf->str, NULL, &ns)) {
            strbuf_free(try_buf);
            return ns;
        }
    }

    // try base_path.ls
    strbuf_reset(try_buf);
    strbuf_append_format(try_buf, "%s.ls", base_path);
    if (pm_file_exists(try_buf->str)) {
        ns = pm_load_lambda_module(host_execution, try_buf->str);
        if (ns.item != ItemNull.item) {
            strbuf_free(try_buf);
            return ns;
        }
    }

    strbuf_free(try_buf);
    return ItemNull;
}

// Resolve a Python module import to a namespace Item.
// Handles built-in modules, single-file imports, package imports (pkg/__init__.py),
// dotted imports (pkg.submod), relative imports (from .x, from ..x), and circular imports.
static Item pm_resolve_py_import(const char* mod_name, int mod_len,
                                  const char* script_filename, void* host_execution) {
    // check built-in modules first
    PyBuiltinModuleInitFn builtin_init = py_stdlib_find_builtin(mod_name, mod_len);
    if (builtin_init) {
        return builtin_init();
    }

    // determine base directory of the current script
    StrBuf* base_dir = strbuf_new();
    const char* last_slash = script_filename ? strrchr(script_filename, '/') : NULL;
    if (last_slash) {
        strbuf_append_format(base_dir, "%.*s", (int)(last_slash - script_filename), script_filename);
    } else {
        strbuf_append_str(base_dir, ".");
    }

    // count leading dots for relative imports
    int dot_count = pm_count_leading_dots(mod_name, mod_len);
    const char* rel_name = mod_name + dot_count;
    int rel_len = mod_len - dot_count;

    // resolve base directory for relative imports (go up directories)
    StrBuf* resolve_dir = strbuf_new();
    if (dot_count > 0) {
        // relative import: start from current script directory
        strbuf_append_str(resolve_dir, base_dir->str);
        // each dot beyond the first goes up one directory level
        for (int d = 1; d < dot_count; d++) {
            const char* up_slash = strrchr(resolve_dir->str, '/');
            if (up_slash && up_slash != resolve_dir->str) {
                int new_len = (int)(up_slash - resolve_dir->str);
                resolve_dir->str[new_len] = '\0';
                resolve_dir->length = new_len;
            }
        }
    } else {
        // absolute import: start from script directory
        strbuf_append_str(resolve_dir, base_dir->str);
    }

    // build the base path: resolve_dir / rel_name (with dots converted to slashes)
    StrBuf* base_path = strbuf_new();
    strbuf_append_str(base_path, resolve_dir->str);

    if (rel_len > 0) {
        strbuf_append_char(base_path, '/');
        for (int i = 0; i < rel_len; i++) {
            strbuf_append_char(base_path, rel_name[i] == '.' ? '/' : rel_name[i]);
        }
    }

    Item ns = ItemNull;

    if (rel_len > 0) {
        // has a module name after dots (or no dots at all)
        const char* first_dot = (const char*)memchr(rel_name, '.', rel_len);
        if (first_dot) {
            // dotted import: e.g. pkg.submod
            // Always use hierarchical loading so that each component gets attached
            // to its parent namespace (e.g. mypkg.utils → mypkg_ns["utils"] = utils_ns)
            {
                int comp_start = 0;
                Item parent_ns = ItemNull;
                StrBuf* comp_path = strbuf_new();
                strbuf_append_str(comp_path, resolve_dir->str);

                for (int i = 0; i <= rel_len; i++) {
                    if (i == rel_len || rel_name[i] == '.') {
                        int comp_len = i - comp_start;
                        if (comp_len > 0) {
                            strbuf_append_char(comp_path, '/');
                            strbuf_append_format(comp_path, "%.*s", comp_len, rel_name + comp_start);

                            Item sub_ns = pm_try_load_module(host_execution, comp_path->str);
                            if (sub_ns.item == ItemNull.item) break;

                            if (parent_ns.item != ItemNull.item) {
                                // attach submodule as attribute of parent package
                                Item key = {.item = s2it(heap_create_name(rel_name + comp_start, comp_len))};
                                py_dict_set(parent_ns, key, sub_ns);
                            }
                            parent_ns = sub_ns;
                            ns = sub_ns;
                        }
                        comp_start = i + 1;
                    }
                }
                strbuf_free(comp_path);
            }
        } else {
            // simple (non-dotted) name
            ns = pm_try_load_module(host_execution, base_path->str);
        }
    } else if (dot_count > 0) {
        // bare relative import: "from . import x" — module_name is just dots
        // load the package directory's __init__.py
        StrBuf* init_path = strbuf_new();
        strbuf_append_format(init_path, "%s/__init__.py", resolve_dir->str);
        if (pm_file_exists(init_path->str)) {
            ns = pm_loading_module_namespace(host_execution, init_path->str);
            if (ns.item == ItemNull.item) {
                jube_load_hosted_module(host_execution, init_path->str, NULL, &ns);
            }
        }
        strbuf_free(init_path);
    }

    // fallback: try relative to entry-point script directory
    if (ns.item == ItemNull.item && py_entry_script_dir[0] && dot_count == 0) {
        StrBuf* entry_path = strbuf_new();
        strbuf_append_str(entry_path, py_entry_script_dir);
        if (rel_len > 0) {
            strbuf_append_char(entry_path, '/');
            for (int i = 0; i < rel_len; i++) {
                strbuf_append_char(entry_path, rel_name[i] == '.' ? '/' : rel_name[i]);
            }
        }
        ns = pm_try_load_module(host_execution, entry_path->str);
        strbuf_free(entry_path);
    }

    strbuf_free(base_dir);
    strbuf_free(resolve_dir);
    strbuf_free(base_path);
    return ns;
}

// ============================================================================
// Statement dispatcher
// ============================================================================

static void pm_transpile_statement(PyMirTranspiler* mt, PyAstNode* stmt) {
    if (!stmt) return;

    switch (stmt->node_type) {
    case PY_AST_NODE_EXPRESSION_STATEMENT: {
        PyExpressionStatementNode* es = (PyExpressionStatementNode*)stmt;
        if (es->expression) {
            pm_transpile_expression(mt, es->expression);
        }
        break;
    }
    case PY_AST_NODE_ASSIGNMENT:
        pm_transpile_assignment(mt, (PyAssignmentNode*)stmt);
        break;
    case PY_AST_NODE_AUGMENTED_ASSIGNMENT:
        pm_transpile_aug_assignment(mt, (PyAugAssignmentNode*)stmt);
        break;
    case PY_AST_NODE_IF:
        pm_transpile_if(mt, (PyIfNode*)stmt);
        break;
    case PY_AST_NODE_WHILE:
        pm_transpile_while(mt, (PyWhileNode*)stmt);
        break;
    case PY_AST_NODE_FOR:
        pm_transpile_for(mt, (PyForNode*)stmt);
        break;
    case PY_AST_NODE_RETURN:
        pm_transpile_return(mt, (PyReturnNode*)stmt);
        break;
    case PY_AST_NODE_BREAK:
        if (mt->loop_depth > 0) {
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
                MIR_new_label_op(mt->em.ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
        }
        break;
    case PY_AST_NODE_CONTINUE:
        if (mt->loop_depth > 0) {
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
                MIR_new_label_op(mt->em.ctx, mt->loop_stack[mt->loop_depth - 1].continue_label)));
        }
        break;
    case PY_AST_NODE_PASS:
        // no-op
        break;
    case PY_AST_NODE_FUNCTION_DEF: {
        // for closures: emit env allocation + closure creation
        // for non-closures in nested context: emit function reference
        PyFunctionDefNode* fdef = (PyFunctionDefNode*)stmt;
        PyFuncCollected* fc_target = NULL;
        for (int fi = 0; fi < mt->func_count; fi++) {
            if (mt->func_entries[fi].node == fdef) {
                fc_target = &mt->func_entries[fi];
                break;
            }
        }
        if (fc_target && fc_target->is_closure && fc_target->func_item) {
            // check if ALL captures are nonlocal — if so, pass parent's shared env directly
            bool all_nonlocal = true;
            for (int ci = 0; ci < fc_target->capture_count; ci++) {
                if (!fc_target->capture_is_nonlocal[ci]) { all_nonlocal = false; break; }
            }

            MIR_reg_t env = 0;
            MIR_reg_t closure = 0;
            if (all_nonlocal && mt->scope_env_reg != 0) {
                // pass the parent's shared env directly — no new alloc needed
                env = mt->scope_env_reg;
            } else {
                // Allocate the Function before its environment so a collection
                // cannot observe a newly allocated environment without an owner.
                MIR_reg_t fptr = pm_new_reg(mt, "cfp", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                    MIR_new_reg_op(mt->em.ctx, fptr),
                    MIR_new_ref_op(mt->em.ctx, fc_target->func_item)));
                closure = pm_call_3(mt, "py_new_closure", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fptr),
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, fc_target->param_count),
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, fc_target->capture_count));
                env = pm_call_1(mt, "py_closure_get_env", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, closure));
                // store each captured variable into env slots
                for (int ci = 0; ci < fc_target->capture_count; ci++) {
                    PyMirVarEntry* cvar = pm_find_var(mt, fc_target->captures[ci]);
                    if (cvar) {
                        if (cvar->from_env) {
                            // load from parent's env first, then store into child env
                            MIR_reg_t loaded = pm_load_env_slot(mt, cvar->env_reg, cvar->env_slot);
                            pm_store_env_slot(mt, env, ci, loaded);
                        } else {
                            pm_store_env_slot(mt, env, ci, cvar->reg);
                        }
                    }
                }
            }
            if (closure == 0) {
                MIR_reg_t fptr = pm_new_reg(mt, "cfp", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                    MIR_new_reg_op(mt->em.ctx, fptr),
                    MIR_new_ref_op(mt->em.ctx, fc_target->func_item)));
                closure = pm_call_4(mt, "py_new_closure_with_env", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fptr),
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, fc_target->param_count),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, env),
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, fc_target->capture_count));
            }
            closure = pm_call_1(mt, "py_mark_mir_public_abi", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, closure));
            // assign to local variable
            char vname[128];
            snprintf(vname, sizeof(vname), "_py_%s", fc_target->name);
            pm_set_var(mt, vname, closure);
            // Phase C: apply decorators to the closure (bottom-to-top)
            if (fdef->decorators) {
                MIR_reg_t dec_result = pm_apply_decorators(mt, closure, fdef->decorators);
                PyMirVarEntry* ventry = pm_find_var(mt, vname);
                if (ventry && !ventry->from_env) {
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                        MIR_new_reg_op(mt->em.ctx, ventry->reg),
                        MIR_new_reg_op(mt->em.ctx, dec_result)));
                }
            }
        } else if (fc_target && !fc_target->is_closure && fc_target->func_item && fdef->decorators) {
            // non-closure function with decorators: build function item, apply decorators, store as var
            char vname[128];
            snprintf(vname, sizeof(vname), "_py_%s", fc_target->name);
            MIR_reg_t fptr = pm_new_reg(mt, "dfp", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, fptr),
                MIR_new_ref_op(mt->em.ctx, fc_target->func_item)));
            MIR_reg_t fn_item = pm_call_2(mt, "py_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fptr),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, fc_target->param_count));
            if (fc_target->has_kwargs) {
                pm_call_1(mt, "py_set_kwargs_flag", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item));
            }
            fn_item = pm_call_1(mt, "py_mark_mir_public_abi", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item));
            MIR_reg_t dec_result = pm_apply_decorators(mt, fn_item, fdef->decorators);
            // store as local var — shadows local_funcs lookup for subsequent references
            MIR_reg_t var_reg = pm_new_reg(mt, vname, MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, var_reg),
                MIR_new_reg_op(mt->em.ctx, dec_result)));
            pm_set_var(mt, vname, var_reg);
            // if there's a module-level slot for this name, also store there
            int midx = pm_get_global_var_index(mt, vname);
            if (midx >= 0) {
                pm_call_void_2(mt, "py_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, midx),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, var_reg));
            }
        }
        // non-closures without decorators are already registered in local_funcs by pm_compile_function
        break;
    }
    case PY_AST_NODE_CLASS_DEF: {
        PyClassDefNode* cls = (PyClassDefNode*)stmt;

        // 1. build name string item
        MIR_reg_t name_reg = pm_box_string_literal(mt,
            cls->name->chars, cls->name->len);

        // 2. build bases list
        MIR_reg_t bases_reg = pm_call_1(mt, "py_list_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, 0));
        PyAstNode* base = cls->bases;
        while (base) {
            MIR_reg_t base_val = pm_transpile_expression(mt, base);
            pm_call_2(mt, "py_list_append", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, bases_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, base_val));
            base = base->next;
        }

        // 3. build methods dict — gather methods belonging to this class
        MIR_reg_t methods_reg = pm_call_0(mt, "py_dict_new", MIR_T_I64);
        for (int fi = 0; fi < mt->func_count; fi++) {
            PyFuncCollected* fc = &mt->func_entries[fi];
            if (!fc->is_method) continue;
            if (strcmp(fc->class_name, cls->name->chars) != 0) continue;
            // only direct methods (parent == -1 or parent is in a different class)
            if (fc->parent_index >= 0 &&
                mt->func_entries[fc->parent_index].is_method &&
                strcmp(mt->func_entries[fc->parent_index].class_name, cls->name->chars) == 0)
                continue;  // nested function inside a method — skip
            if (!fc->func_item) continue;

            MIR_reg_t fptr = pm_new_reg(mt, "mfp", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, fptr),
                MIR_new_ref_op(mt->em.ctx, fc->func_item)));
            MIR_reg_t fn_item_reg = pm_call_2(mt, "py_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fptr),
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, fc->param_count));
            if (fc->has_kwargs) {
                pm_call_1(mt, "py_set_kwargs_flag", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item_reg));
            }
            fn_item_reg = pm_call_1(mt, "py_mark_mir_public_abi", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item_reg));

            // Phase F1: detect @prop.setter / @prop.deleter decorator patterns.
            // When the first decorator expression is an attribute access like `celsius.setter`,
            // augment the existing property in the methods dict instead of creating a new entry.
            bool handled_as_prop_accessor = false;
            if (fc->node && fc->node->decorators) {
                PyDecoratorNode* first_dec = (PyDecoratorNode*)fc->node->decorators;
                if (first_dec->expression &&
                    first_dec->expression->node_type == PY_AST_NODE_ATTRIBUTE) {
                    PyAttributeNode* attr_dec = (PyAttributeNode*)first_dec->expression;
                    if (attr_dec->object &&
                        attr_dec->object->node_type == PY_AST_NODE_IDENTIFIER &&
                        attr_dec->attribute) {
                        const char* attr_kind = attr_dec->attribute->chars;
                        bool is_setter  = (strncmp(attr_kind, "setter",  6) == 0 && attr_dec->attribute->len == 6);
                        bool is_deleter = (strncmp(attr_kind, "deleter", 7) == 0 && attr_dec->attribute->len == 7);
                        if (is_setter || is_deleter) {
                            PyIdentifierNode* prop_id = (PyIdentifierNode*)attr_dec->object;
                            MIR_reg_t pname_reg = pm_box_string_literal(mt,
                                prop_id->name->chars, prop_id->name->len);
                            // get the existing property descriptor from the methods dict
                            MIR_reg_t existing_prop = pm_call_2(mt, "py_dict_get", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, methods_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pname_reg));
                            MIR_reg_t updated_prop;
                            if (is_setter) {
                                updated_prop = pm_call_2(mt, "py_property_setter", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, existing_prop),
                                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item_reg));
                            } else {
                                updated_prop = pm_call_2(mt, "py_property_deleter", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, existing_prop),
                                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item_reg));
                            }
                            pm_call_3(mt, "py_dict_set", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, methods_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, pname_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, updated_prop));
                            handled_as_prop_accessor = true;
                        }
                    }
                }
            }
            if (handled_as_prop_accessor) continue;

            // Phase C: apply method decorators (e.g. @property)
            if (fc->node && fc->node->decorators) {
                fn_item_reg = pm_apply_decorators(mt, fn_item_reg, fc->node->decorators);
            }
            MIR_reg_t mname_reg = pm_box_string_literal(mt,
                fc->name, strlen(fc->name));
            pm_call_3(mt, "py_dict_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, methods_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, mname_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, fn_item_reg));
        }

        // 3b. execute class body non-method statements: assignments to simple names
        // are stored in the methods dict as class attributes (e.g. _registry = [], x = 42).
        {
            PyAstNode* body_stmt = NULL;
            if (cls->body && cls->body->node_type == PY_AST_NODE_BLOCK) {
                body_stmt = ((PyBlockNode*)cls->body)->statements;
            } else {
                body_stmt = cls->body;
            }
            while (body_stmt) {
                if (body_stmt->node_type == PY_AST_NODE_ASSIGNMENT) {
                    PyAssignmentNode* asgn = (PyAssignmentNode*)body_stmt;
                    // only handle simple name assignments (skip dunder methods handled above)
                    if (asgn->left &&
                        asgn->left->node_type == PY_AST_NODE_IDENTIFIER &&
                        asgn->right) {
                        PyIdentifierNode* id_node = (PyIdentifierNode*)asgn->left;
                        // skip assignments to special names that are handled as methods
                        bool is_method = false;
                        for (int fi = 0; fi < mt->func_count && !is_method; fi++) {
                            if (mt->func_entries[fi].is_method &&
                                strcmp(mt->func_entries[fi].class_name, cls->name->chars) == 0 &&
                                (int)strlen(mt->func_entries[fi].name) == id_node->name->len &&
                                strncmp(mt->func_entries[fi].name, id_node->name->chars, id_node->name->len) == 0) {
                                is_method = true;
                            }
                        }
                        if (!is_method) {
                            MIR_reg_t val_reg = pm_transpile_expression(mt, asgn->right);
                            MIR_reg_t aname_reg = pm_box_string_literal(mt,
                                id_node->name->chars, id_node->name->len);
                            pm_call_3(mt, "py_dict_set", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, methods_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, aname_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val_reg));
                        }
                    }
                }
                // skip function defs (already handled), expression-only stmts, pass, etc.
                body_stmt = body_stmt->next;
            }
        }

        // 4. call py_class_new(name, bases, methods) — or metaclass(name, bases, methods)
        MIR_reg_t class_reg;
        if (cls->metaclass) {
            // Phase F5: metaclass specified — call metaclass(name, bases, methods) directly
            MIR_reg_t meta_reg = pm_transpile_expression(mt, cls->metaclass);
            Item args_arr[3];
            (void)args_arr;
            // use py_call_function via a temporary args array: build it on the stack
            // emit: call py_class_new_meta(meta, name, bases, methods)
            class_reg = pm_call_4(mt, "py_class_new_meta", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, meta_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, bases_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, methods_reg));
        } else {
            class_reg = pm_call_3(mt, "py_class_new", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, bases_reg),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, methods_reg));
        }

        // 5. store class as a local scope variable AND as a module var (for super() in methods)
        char class_var_name[132];
        snprintf(class_var_name, sizeof(class_var_name),
            "_py_%.*s", (int)cls->name->len, cls->name->chars);
        MIR_reg_t reg = pm_new_reg(mt, class_var_name, MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, reg),
            MIR_new_reg_op(mt->em.ctx, class_reg)));
        pm_set_var(mt, class_var_name, reg);
        // Phase C: apply class decorators (bottom-to-top)
        if (cls->decorators) {
            MIR_reg_t dec_result = pm_apply_decorators(mt, reg, cls->decorators);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, reg),
                MIR_new_reg_op(mt->em.ctx, dec_result)));
        }
        // also store in module var so methods can retrieve it
        {
            int midx = pm_get_global_var_index(mt, class_var_name);
            if (midx >= 0) {
                pm_call_void_2(mt, "py_set_module_var",
                    MIR_T_I64, MIR_new_int_op(mt->em.ctx, midx),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, reg));
            }
        }
        break;
    }
    case PY_AST_NODE_BLOCK: {
        pm_push_scope(mt);
        PyBlockNode* blk = (PyBlockNode*)stmt;
        PyAstNode* s = blk->statements;
        while (s) { pm_transpile_statement(mt, s); s = s->next; }
        pm_pop_scope(mt);
        break;
    }
    case PY_AST_NODE_ASSERT: {
        PyAssertNode* assert_n = (PyAssertNode*)stmt;
        MIR_reg_t test = pm_transpile_expression(mt, assert_n->test);
        MIR_reg_t truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, test));
        MIR_label_t l_ok = pm_new_label(mt);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT, MIR_new_label_op(mt->em.ctx, l_ok),
            MIR_new_reg_op(mt->em.ctx, truthy)));
        // assert failed → raise
        MIR_reg_t exc_type = pm_box_string_literal(mt, "AssertionError", 14);
        MIR_reg_t exc_msg;
        if (assert_n->message) {
            exc_msg = pm_transpile_expression(mt, assert_n->message);
        } else {
            exc_msg = pm_box_string_literal(mt, "assertion failed", 16);
        }
        MIR_reg_t exc = pm_call_2(mt, "py_new_exception", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_type),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_msg));
        pm_call_void_1(mt, "py_raise",
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc));
        pm_emit_label(mt, l_ok);
        break;
    }
    case PY_AST_NODE_GLOBAL:
    case PY_AST_NODE_NONLOCAL:
        // handled in scope analysis
        break;
    case PY_AST_NODE_DEL: {
        PyDelNode* dn = (PyDelNode*)stmt;
        PyAstNode* target = dn->targets;
        while (target) {
            if (target->node_type == PY_AST_NODE_ATTRIBUTE) {
                // del obj.attr — invoke py_delattr(obj, "attr")
                PyAttributeNode* attr_node = (PyAttributeNode*)target;
                MIR_reg_t obj_reg = pm_transpile_expression(mt, attr_node->object);
                MIR_reg_t attr_name_reg = pm_box_string_literal(mt,
                    attr_node->attribute->chars, attr_node->attribute->len);
                pm_call_2(mt, "py_delattr", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, obj_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, attr_name_reg));
            }
            // del var / del obj[key] — simplified no-op for now
            target = target->next;
        }
        break;
    }
    case PY_AST_NODE_RAISE: {
        PyRaiseNode* rn = (PyRaiseNode*)stmt;
        if (rn->value) {
            MIR_reg_t exc = pm_transpile_expression(mt, rn->value);
            pm_call_void_1(mt, "py_raise",
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc));
        } else {
            pm_call_void_1(mt, "py_raise",
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL));
        }
        // if inside a try block, jump to handler; otherwise return from function
        if (mt->try_depth > 0) {
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
                MIR_new_label_op(mt->em.ctx, mt->try_handler_labels[mt->try_depth - 1])));
        } else {
            pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1,
                MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL)));
        }
        break;
    }
    case PY_AST_NODE_TRY: {
        PyTryNode* tn = (PyTryNode*)stmt;
        MIR_label_t l_handler = pm_new_label(mt);
        MIR_label_t l_end = pm_new_label(mt);
        MIR_label_t l_finally = tn->finally_body ? pm_new_label(mt) : l_end;

        // push try handler label so raise can jump here
        if (!pm_require_capacity(mt, mt->try_depth, PM_MAX_TRY_HANDLERS,
                "nested try handlers")) break;
        mt->try_handler_labels[mt->try_depth] = l_handler;
        mt->try_depth++;

        // emit try body — after each statement, check for exception
        {
            PyAstNode* body = tn->body;
            if (body && body->node_type == PY_AST_NODE_BLOCK) {
                PyAstNode* s = ((PyBlockNode*)body)->statements;
                while (s) {
                    pm_transpile_statement(mt, s);
                    // check exception after each statement
                    MIR_reg_t chk = pm_call_0(mt, "py_check_exception", MIR_T_I64);
                    MIR_reg_t chk_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, chk));
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                        MIR_new_label_op(mt->em.ctx, l_handler),
                        MIR_new_reg_op(mt->em.ctx, chk_t)));
                    s = s->next;
                }
            } else {
                pm_transpile_block(mt, body);
                // single check at the end
                MIR_reg_t chk = pm_call_0(mt, "py_check_exception", MIR_T_I64);
                MIR_reg_t chk_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, chk));
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                    MIR_new_label_op(mt->em.ctx, l_handler),
                    MIR_new_reg_op(mt->em.ctx, chk_t)));
            }
        }

        // pop try depth (we either fell through normally or will jump to handler)
        if (mt->try_depth > 0) mt->try_depth--;

        // no exception → jump to finally/end
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
            MIR_new_label_op(mt->em.ctx, l_finally)));

        // handler label
        pm_emit_label(mt, l_handler);

        if (tn->handlers) {
            // clear exception
            MIR_reg_t exc_val = pm_call_0(mt, "py_clear_exception", MIR_T_I64);

            // handle each except clause
            MIR_label_t l_no_match = pm_new_label(mt); // fallback: re-raise if no handler matches
            PyAstNode* handler = tn->handlers;
            while (handler) {
                if (handler->node_type == PY_AST_NODE_EXCEPT) {
                    PyExceptNode* en = (PyExceptNode*)handler;
                    MIR_label_t l_next_handler = pm_new_label(mt);

                    // if except has a type filter (e.g., except ValueError),
                    // check if exception type matches
                    if (en->type) {
                        // "except Exception" catches all — skip type check
                        bool catch_all = false;
                        if (en->type->node_type == PY_AST_NODE_IDENTIFIER) {
                            PyIdentifierNode* tid = (PyIdentifierNode*)en->type;
                            if (tid->name->len == 9 && strncmp(tid->name->chars, "Exception", 9) == 0) {
                                catch_all = true;
                            }
                        }

                        if (!catch_all) {
                            // get the exception's type string
                            MIR_reg_t exc_type = pm_call_1(mt, "py_exception_get_type", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_val));

                            // get the expected type name as a string
                            MIR_reg_t expected_type;
                            if (en->type->node_type == PY_AST_NODE_IDENTIFIER) {
                                PyIdentifierNode* tid = (PyIdentifierNode*)en->type;
                                expected_type = pm_box_string_literal(mt, tid->name->chars, tid->name->len);
                            } else {
                                expected_type = pm_transpile_expression(mt, en->type);
                            }

                            // compare: py_eq(exc_type, expected_type)
                            MIR_reg_t match = pm_call_2(mt, "py_eq", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_type),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, expected_type));
                            MIR_reg_t match_truthy = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, match));
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BF,
                                MIR_new_label_op(mt->em.ctx, l_next_handler),
                                MIR_new_reg_op(mt->em.ctx, match_truthy)));
                        }
                    }
                    // bind exception to name if present
                    if (en->name) {
                        char vname[128];
                        snprintf(vname, sizeof(vname), "_py_%.*s", (int)en->name->len, en->name->chars);
                        PyMirVarEntry* var = pm_find_var(mt, vname);
                        if (var) {
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                                MIR_new_reg_op(mt->em.ctx, var->reg),
                                MIR_new_reg_op(mt->em.ctx, exc_val)));
                        } else {
                            MIR_reg_t reg = pm_new_reg(mt, vname, MIR_T_I64);
                            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                                MIR_new_reg_op(mt->em.ctx, reg),
                                MIR_new_reg_op(mt->em.ctx, exc_val)));
                            pm_set_var(mt, vname, reg);
                        }
                    }

                    // emit handler body
                    pm_transpile_block(mt, en->body);
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
                        MIR_new_label_op(mt->em.ctx, l_finally)));

                    pm_emit_label(mt, l_next_handler);
                }
                handler = handler->next;
            }

            // no handler matched → re-raise
            pm_call_void_1(mt, "py_raise",
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_val));
            pm_emit_label(mt, l_no_match);
        }

        // finally
        if (tn->finally_body) {
            pm_emit_label(mt, l_finally);
            pm_transpile_block(mt, tn->finally_body);
        }

        pm_emit_label(mt, l_end);
        break;
    }
    case PY_AST_NODE_IMPORT: {
        // bare "import module" — bind the module namespace as _py_<name>
        PyImportNode* imp = (PyImportNode*)stmt;
        if (!imp->module_name) break;

        const char* mod_name = imp->module_name->chars;
        int mod_len = (int)imp->module_name->len;

        // use alias if provided; for dotted names "os.path" bind as "os"
        const char* local_name = mod_name;
        int local_len = mod_len;
        if (imp->alias) {
            local_name = imp->alias->chars;
            local_len = (int)imp->alias->len;
        } else {
            // only use the first component of a dotted name
            const char* dot = (const char*)memchr(mod_name, '.', mod_len);
            if (dot) local_len = (int)(dot - mod_name);
        }

        Item ns = pm_resolve_py_import(mod_name, mod_len, mt->filename, mt->host_execution);

        if (ns.item == ItemNull.item) {
            log_error("py-mir: import '%.*s': module not found", mod_len, mod_name);
            break;
        }

        // For dotted imports without alias (e.g. "import mypkg.utils"), the local
        // binding is the FIRST component (mypkg), not the last (utils).
        // pm_resolve_py_import returns the last component's namespace and attaches
        // each component as an attribute of its parent. Re-resolve the first component.
        if (!imp->alias) {
            const char* dot = (const char*)memchr(mod_name, '.', mod_len);
            if (dot) {
                int first_len = (int)(dot - mod_name);
                Item first_ns = pm_resolve_py_import(mod_name, first_len, mt->filename,
                                                      mt->host_execution);
                if (first_ns.item != ItemNull.item) ns = first_ns;
            }
        }

        // bind namespace map to _py_<localname>
        char vname[132];
        snprintf(vname, sizeof(vname), "_py_%.*s", local_len, local_name);
        MIR_reg_t val_reg = pm_new_reg(mt, "imp_ns", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, val_reg),
            MIR_new_int_op(mt->em.ctx, (int64_t)ns.item)));
        pm_set_var(mt, vname, val_reg);

        // also store as module var so nested functions (generators/coroutines) can access it
        int midx = pm_get_global_var_index(mt, vname);
        if (midx >= 0) {
            pm_call_void_2(mt, "py_set_module_var",
                MIR_T_I64, MIR_new_int_op(mt->em.ctx, midx),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val_reg));
        }

        log_debug("py-mir: import '%.*s' → '%s'", mod_len, mod_name, vname);
        break;
    }
    case PY_AST_NODE_WITH: {
        // with <expr> as <target>: <body>
        // desugars to:
        //   mgr = expr
        //   val = mgr.__enter__()
        //   try: body
        //       mgr.__exit__(None, None, None)
        //   except: if not mgr.__exit__(exc, exc, None): re-raise
        PyWithNode* wn = (PyWithNode*)stmt;

        // 1. evaluate context manager expression
        MIR_reg_t mgr_reg = pm_transpile_expression(mt, wn->items);
        MIR_reg_t mgr_saved = pm_new_reg(mt, "with_mgr", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, mgr_saved),
            MIR_new_reg_op(mt->em.ctx, mgr_reg)));

        // 2. call __enter__, check for exception
        MIR_label_t l_with_exc  = pm_new_label(mt);
        MIR_label_t l_with_end  = pm_new_label(mt);

        MIR_reg_t enter_val = pm_call_1(mt, "py_context_enter", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, mgr_saved));
        {
            MIR_reg_t chk = pm_call_0(mt, "py_check_exception", MIR_T_I64);
            MIR_reg_t chkt = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, chk));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                MIR_new_label_op(mt->em.ctx, l_with_end),
                MIR_new_reg_op(mt->em.ctx, chkt)));
        }

        // 3. bind as-target if present
        if (wn->target) {
            char vname[132];
            snprintf(vname, sizeof(vname), "_py_%.*s",
                (int)wn->target->len, wn->target->chars);
            MIR_reg_t vr = pm_new_reg(mt, vname, MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, vr),
                MIR_new_reg_op(mt->em.ctx, enter_val)));
            pm_set_var(mt, vname, vr);
        }

        // 4. push exception handler
        if (!pm_require_capacity(mt, mt->try_depth, PM_MAX_TRY_HANDLERS,
                "nested try handlers")) break;
        mt->try_handler_labels[mt->try_depth] = l_with_exc;
        mt->try_depth++;

        // 5. compile body with per-statement exception checks
        {
            PyAstNode* body = wn->body;
            if (body && body->node_type == PY_AST_NODE_BLOCK) {
                PyAstNode* s2 = ((PyBlockNode*)body)->statements;
                while (s2) {
                    pm_transpile_statement(mt, s2);
                    MIR_reg_t chk = pm_call_0(mt, "py_check_exception", MIR_T_I64);
                    MIR_reg_t chkt = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, chk));
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                        MIR_new_label_op(mt->em.ctx, l_with_exc),
                        MIR_new_reg_op(mt->em.ctx, chkt)));
                    s2 = s2->next;
                }
            } else {
                pm_transpile_block(mt, body);
                MIR_reg_t chk = pm_call_0(mt, "py_check_exception", MIR_T_I64);
                MIR_reg_t chkt = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->em.ctx, chk));
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                    MIR_new_label_op(mt->em.ctx, l_with_exc),
                    MIR_new_reg_op(mt->em.ctx, chkt)));
            }
        }

        // pop try depth
        if (mt->try_depth > 0) mt->try_depth--;

        // 6. normal exit: __exit__(None, None, None)
        {
            MIR_reg_t n1 = pm_emit_null(mt);
            MIR_reg_t n2 = pm_emit_null(mt);
            MIR_reg_t n3 = pm_emit_null(mt);
            pm_call_4(mt, "py_context_exit", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, mgr_saved),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, n1),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, n2),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, n3));
        }
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP,
            MIR_new_label_op(mt->em.ctx, l_with_end)));

        // 7. exception handler: call __exit__ with exception info
        pm_emit_label(mt, l_with_exc);
        {
            MIR_reg_t exc_val = pm_call_0(mt, "py_clear_exception", MIR_T_I64);
            MIR_reg_t null_tb = pm_emit_null(mt);
            MIR_reg_t suppress = pm_call_4(mt, "py_context_exit", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, mgr_saved),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_val),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_val),
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, null_tb));
            MIR_reg_t supp_t = pm_call_1(mt, "py_is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, suppress));
            // if suppress=true: jump to end (swallow the exception)
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                MIR_new_label_op(mt->em.ctx, l_with_end),
                MIR_new_reg_op(mt->em.ctx, supp_t)));
            // if suppress=false: re-raise
            pm_call_void_1(mt, "py_raise",
                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, exc_val));
        }

        pm_emit_label(mt, l_with_end);
        break;
    }
    case PY_AST_NODE_MATCH: {
        pm_transpile_match(mt, (PyMatchNode*)stmt);
        break;
    }
    case PY_AST_NODE_IMPORT_FROM: {
        // from <module> import name1, name2
        PyImportNode* imp = (PyImportNode*)stmt;
        if (!imp->module_name) break;

        const char* mod_name = imp->module_name->chars;
        int mod_len = (int)imp->module_name->len;

        Item ns = pm_resolve_py_import(mod_name, mod_len, mt->filename, mt->host_execution);

        if (ns.item == ItemNull.item) {
            log_error("py-mir: failed to load module '%.*s'", mod_len, mod_name);
            break;
        }
        // Import lowering allocates names and package paths while reading this
        // heap namespace; keep it exact-rooted across those compiler-side calls.
        PmHostedItemRoot rooted_ns(ns);

        // handle wildcard import: from module import *
        if (imp->names) {
            PyImportNode* first = (PyImportNode*)imp->names;
            if (first->module_name && first->module_name->len == 1 &&
                first->module_name->chars[0] == '*') {
                ns = rooted_ns.get();
                Item keys_arr = py_dict_keys(ns);
                int64_t key_count = py_list_length(keys_arr);
                for (int64_t ki = 0; ki < key_count; ki++) {
                    Item key_item = py_list_get(keys_arr, (Item){.item = i2it(ki)});
                    Item value = py_dict_get(ns, key_item);
                    if (value.item == ItemNull.item) continue;
                    if (get_type_id(key_item) != LMD_TYPE_STRING) continue;
                    String* key_str = it2s(key_item);
                    if (!key_str) continue;
                    // skip dunder names (__name__, __is_class__, etc.)
                    if (key_str->len >= 4 &&
                        key_str->chars[0] == '_' && key_str->chars[1] == '_') continue;
                    char vname[132];
                    snprintf(vname, sizeof(vname), "_py_%.*s",
                        (int)key_str->len, key_str->chars);
                    MIR_reg_t val_reg = pm_new_reg(mt, "star_imp", MIR_T_I64);
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                        MIR_new_reg_op(mt->em.ctx, val_reg),
                        MIR_new_int_op(mt->em.ctx, (int64_t)value.item)));
                    pm_set_var(mt, vname, val_reg);
                    log_debug("py-mir: star import '%.*s' → '%s'",
                        (int)key_str->len, key_str->chars, vname);
                }
                break;
            }
        }

        // determine the package directory for resolving submodule imports
        // (needed for "from . import submod" where submod is a separate .py file,
        // and for "from pkg import submod" where pkg is a package with __init__.py)
        int dot_count = pm_count_leading_dots(mod_name, mod_len);
        StrBuf* pkg_dir = NULL;
        if (dot_count > 0) {
            // relative import: compute the package directory
            const char* last_slash = mt->filename ? strrchr(mt->filename, '/') : NULL;
            pkg_dir = strbuf_new();
            if (last_slash) {
                strbuf_append_format(pkg_dir, "%.*s", (int)(last_slash - mt->filename), mt->filename);
            } else {
                strbuf_append_str(pkg_dir, ".");
            }
            for (int d = 1; d < dot_count; d++) {
                const char* up = strrchr(pkg_dir->str, '/');
                if (up && up != pkg_dir->str) {
                    int new_len = (int)(up - pkg_dir->str);
                    pkg_dir->str[new_len] = '\0';
                    pkg_dir->length = new_len;
                }
            }
            // if there's a module name after dots, append it
            const char* rel_name = mod_name + dot_count;
            int rel_len = mod_len - dot_count;
            if (rel_len > 0) {
                strbuf_append_char(pkg_dir, '/');
                for (int i = 0; i < rel_len; i++) {
                    strbuf_append_char(pkg_dir, rel_name[i] == '.' ? '/' : rel_name[i]);
                }
            }
        } else if (mod_len > 0) {
            // absolute import (e.g. "from mypkg import submod"):
            // compute the package directory by resolving the module name as a directory
            const char* script_dir_end = mt->filename ? strrchr(mt->filename, '/') : NULL;
            StrBuf* candidate_dir = strbuf_new();
            if (script_dir_end) {
                strbuf_append_format(candidate_dir, "%.*s", (int)(script_dir_end - mt->filename), mt->filename);
            } else {
                strbuf_append_str(candidate_dir, ".");
            }
            strbuf_append_char(candidate_dir, '/');
            for (int i = 0; i < mod_len; i++) {
                strbuf_append_char(candidate_dir, mod_name[i] == '.' ? '/' : mod_name[i]);
            }
            // only use it as pkg_dir if it's a package (has __init__.py)
            StrBuf* init_check = strbuf_new();
            strbuf_append_format(init_check, "%s/__init__.py", candidate_dir->str);
            if (pm_file_exists(init_check->str)) {
                pkg_dir = candidate_dir;
            } else {
                strbuf_free(candidate_dir);
                // also try from py_entry_script_dir
                if (py_entry_script_dir[0]) {
                    strbuf_reset(init_check);
                    candidate_dir = strbuf_new();
                    strbuf_append_format(candidate_dir, "%s/", py_entry_script_dir);
                    for (int i = 0; i < mod_len; i++) {
                        strbuf_append_char(candidate_dir, mod_name[i] == '.' ? '/' : mod_name[i]);
                    }
                    strbuf_append_format(init_check, "%s/__init__.py", candidate_dir->str);
                    if (pm_file_exists(init_check->str)) {
                        pkg_dir = candidate_dir;
                    } else {
                        strbuf_free(candidate_dir);
                    }
                }
            }
            strbuf_free(init_check);
        }

        // for each imported name, extract from namespace and store as variable
        PyAstNode* name_node = imp->names;
        while (name_node) {
            if (name_node->node_type == PY_AST_NODE_IMPORT) {
                PyImportNode* name_imp = (PyImportNode*)name_node;
                if (name_imp->module_name) {
                    const char* sym_name = name_imp->module_name->chars;
                    int sym_len = (int)name_imp->module_name->len;

                    // get from namespace
                    ns = rooted_ns.get();
                    Item key = {.item = s2it(heap_create_name(sym_name, sym_len))};
                    ns = rooted_ns.get();
                    // Python module namespaces are Python maps. Keeping lookup
                    // in the Python map API prevents imports from depending on
                    // JS runtime activation or JavaScript missing-key rules.
                    Item value = py_dict_get(ns, key);

                    // if not found in namespace and we have a package dir,
                    // try loading as a submodule (from . import submod)
                    bool not_in_ns = (value.item == ItemNull.item || get_type_id(value) == LMD_TYPE_UNDEFINED);
                    if (not_in_ns && pkg_dir) {
                        StrBuf* submod_path = strbuf_new();
                        strbuf_append_format(submod_path, "%s/%.*s", pkg_dir->str, sym_len, sym_name);
                        value = pm_try_load_module(mt->host_execution, submod_path->str);
                        if (value.item != ItemNull.item) {
                            // also add to parent namespace for future lookups
                            py_dict_set(ns, key, value);
                        }
                        strbuf_free(submod_path);
                    }

                    bool value_valid = (value.item != ItemNull.item && get_type_id(value) != LMD_TYPE_UNDEFINED);
                    if (value_valid) {
                        // use alias if provided, otherwise original name
                        const char* local_name = sym_name;
                        int local_len = sym_len;
                        if (name_imp->alias) {
                            local_name = name_imp->alias->chars;
                            local_len = (int)name_imp->alias->len;
                        }

                        // store as a variable in current scope with _py_ prefix
                        char vname[128];
                        snprintf(vname, sizeof(vname), "_py_%.*s", local_len, local_name);
                        MIR_reg_t val_reg = pm_new_reg(mt, "imp", MIR_T_I64);
                        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                            MIR_new_reg_op(mt->em.ctx, val_reg),
                            MIR_new_int_op(mt->em.ctx, (int64_t)value.item)));
                        pm_set_var(mt, vname, val_reg);

                        // also store as module var for export and nested function access
                        int imp_midx = pm_get_global_var_index(mt, vname);
                        if (imp_midx >= 0) {
                            pm_call_void_2(mt, "py_set_module_var",
                                MIR_T_I64, MIR_new_int_op(mt->em.ctx, imp_midx),
                                MIR_T_I64, MIR_new_reg_op(mt->em.ctx, val_reg));
                        }

                        log_debug("py-mir: imported '%.*s' as '%s'", sym_len, sym_name, vname);
                    } else {
                        log_error("py-mir: name '%.*s' not found in module '%.*s'",
                            sym_len, sym_name, mod_len, mod_name);
                    }
                }
            }
            name_node = name_node->next;
        }
        if (pkg_dir) strbuf_free(pkg_dir);
        break;
    }
    default:
        log_debug("py-mir: unsupported statement type %d", stmt->node_type);
        break;
    }
}

// ============================================================================
// Function collection and compilation
// ============================================================================
// Phase A: Generator helpers
// ============================================================================

// Recursively check if a node (and its descendants) contains a yield expression.
// Does NOT recurse into nested function or class definitions.
static bool pm_has_yield_r(PyAstNode* node) {
    if (!node) return false;
    if (node->node_type == PY_AST_NODE_YIELD) return true;
    if (node->node_type == PY_AST_NODE_FUNCTION_DEF ||
        node->node_type == PY_AST_NODE_CLASS_DEF) return false;
    // recurse into common structural nodes
    switch (node->node_type) {
    case PY_AST_NODE_BLOCK: {
        for (PyAstNode* s = ((PyBlockNode*)node)->statements; s; s = s->next)
            if (pm_has_yield_r(s)) return true;
        return false;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* i = (PyIfNode*)node;
        if (pm_has_yield_r(i->body)) return true;
        for (PyAstNode* e = i->elif_clauses; e; e = e->next)
            if (pm_has_yield_r(e)) return true;
        return pm_has_yield_r(i->else_body);
    }
    case PY_AST_NODE_ELIF:
        return pm_has_yield_r(((PyIfNode*)node)->body);
    case PY_AST_NODE_WHILE:
        return pm_has_yield_r(((PyWhileNode*)node)->body);
    case PY_AST_NODE_FOR:
        return pm_has_yield_r(((PyForNode*)node)->body);
    case PY_AST_NODE_TRY: {
        PyTryNode* t = (PyTryNode*)node;
        if (pm_has_yield_r(t->body)) return true;
        for (PyAstNode* h = t->handlers; h; h = h->next)
            if (pm_has_yield_r(h)) return true;
        return pm_has_yield_r(t->finally_body);
    }
    case PY_AST_NODE_WITH:
        return pm_has_yield_r(((PyWithNode*)node)->body);
    case PY_AST_NODE_ASSIGNMENT:
        return pm_has_yield_r(((PyAssignmentNode*)node)->right);
    case PY_AST_NODE_EXPRESSION_STATEMENT:
        return pm_has_yield_r(((PyExpressionStatementNode*)node)->expression);
    default:
        return false;
    }
}

// Count yield points (each PY_AST_NODE_YIELD = 1, regardless of is_from).
static int pm_count_yield_points_r(PyAstNode* node) {
    if (!node) return 0;
    if (node->node_type == PY_AST_NODE_YIELD) return 1;
    if (node->node_type == PY_AST_NODE_FUNCTION_DEF ||
        node->node_type == PY_AST_NODE_CLASS_DEF) return 0;
    int n = 0;
    switch (node->node_type) {
    case PY_AST_NODE_BLOCK:
        for (PyAstNode* s = ((PyBlockNode*)node)->statements; s; s = s->next)
            n += pm_count_yield_points_r(s);
        break;
    case PY_AST_NODE_IF: {
        PyIfNode* i = (PyIfNode*)node;
        n += pm_count_yield_points_r(i->body);
        for (PyAstNode* e = i->elif_clauses; e; e = e->next)
            n += pm_count_yield_points_r(e);
        n += pm_count_yield_points_r(i->else_body);
        break;
    }
    case PY_AST_NODE_ELIF:
        n += pm_count_yield_points_r(((PyIfNode*)node)->body);
        break;
    case PY_AST_NODE_WHILE:
        n += pm_count_yield_points_r(((PyWhileNode*)node)->body);
        break;
    case PY_AST_NODE_FOR:
        n += pm_count_yield_points_r(((PyForNode*)node)->body);
        break;
    case PY_AST_NODE_TRY: {
        PyTryNode* t = (PyTryNode*)node;
        n += pm_count_yield_points_r(t->body);
        for (PyAstNode* h = t->handlers; h; h = h->next)
            n += pm_count_yield_points_r(h);
        n += pm_count_yield_points_r(t->finally_body);
        break;
    }
    case PY_AST_NODE_WITH:
        n += pm_count_yield_points_r(((PyWithNode*)node)->body);
        break;
    case PY_AST_NODE_ASSIGNMENT:
        n += pm_count_yield_points_r(((PyAssignmentNode*)node)->right);
        break;
    case PY_AST_NODE_EXPRESSION_STATEMENT:
        n += pm_count_yield_points_r(((PyExpressionStatementNode*)node)->expression);
        break;
    default:
        break;
    }
    return n;
}

// Add a local variable name to the generator frame tracking list (idempotent).
static void pm_gen_add_local(PyMirTranspiler* mt, const char* vn) {
    for (int i = 0; i < mt->gen_local_count; i++)
        if (strcmp(mt->gen_local_names[i], vn) == 0) return;
    if (!pm_require_capacity(mt, mt->gen_local_count, PM_MAX_GENERATOR_SLOTS,
            "generator frame locals")) return;
    snprintf(mt->gen_local_names[mt->gen_local_count], 128, "%s", vn);
    mt->gen_local_frame_slots[mt->gen_local_count] = mt->gen_local_count + 1; // slot 0 = state
    mt->gen_local_count++;
}

// Walk the generator body AST to collect all locally assigned variable names,
// as well as auto-named iterator variables for for-loops and yield-from.
static void pm_gen_collect_locals_r(PyMirTranspiler* mt, PyAstNode* node) {
    if (!node) return;
    if (node->node_type == PY_AST_NODE_FUNCTION_DEF ||
        node->node_type == PY_AST_NODE_CLASS_DEF) return;
    switch (node->node_type) {
    case PY_AST_NODE_YIELD: {
        PyYieldNode* yn = (PyYieldNode*)node;
        if (yn->is_from) {
            // yield from needs a sub-iterator variable saved across yields
            char vn[128];
            snprintf(vn, sizeof(vn), "_py__git_%d", mt->gen_iter_count++);
            pm_gen_add_local(mt, vn);
        }
        pm_gen_collect_locals_r(mt, ((PyYieldNode*)node)->value);
        break;
    }
    case PY_AST_NODE_FOR: {
        PyForNode* f = (PyForNode*)node;
        // iterator variable for this for loop
        char ivn[128];
        snprintf(ivn, sizeof(ivn), "_py__git_%d", mt->gen_iter_count++);
        pm_gen_add_local(mt, ivn);
        // loop target variable
        if (f->left && f->left->node_type == PY_AST_NODE_IDENTIFIER) {
            PyIdentifierNode* id = (PyIdentifierNode*)f->left;
            char tvn[128];
            snprintf(tvn, sizeof(tvn), "_py_%.*s", (int)id->name->len, id->name->chars);
            pm_gen_add_local(mt, tvn);
        }
        pm_gen_collect_locals_r(mt, f->body);
        break;
    }
    case PY_AST_NODE_ASSIGNMENT: {
        PyAssignmentNode* a = (PyAssignmentNode*)node;
        for (PyAstNode* t = a->left; t; t = t->next) {
            if (t->node_type == PY_AST_NODE_IDENTIFIER) {
                PyIdentifierNode* id = (PyIdentifierNode*)t;
                char vn[128];
                snprintf(vn, sizeof(vn), "_py_%.*s", (int)id->name->len, id->name->chars);
                pm_gen_add_local(mt, vn);
            }
        }
        pm_gen_collect_locals_r(mt, a->right);
        break;
    }
    case PY_AST_NODE_AUGMENTED_ASSIGNMENT: {
        PyAugAssignmentNode* a = (PyAugAssignmentNode*)node;
        if (a->left && a->left->node_type == PY_AST_NODE_IDENTIFIER) {
            PyIdentifierNode* id = (PyIdentifierNode*)a->left;
            char vn[128];
            snprintf(vn, sizeof(vn), "_py_%.*s", (int)id->name->len, id->name->chars);
            pm_gen_add_local(mt, vn);
        }
        break;
    }
    case PY_AST_NODE_BLOCK: {
        for (PyAstNode* s = ((PyBlockNode*)node)->statements; s; s = s->next)
            pm_gen_collect_locals_r(mt, s);
        break;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* i = (PyIfNode*)node;
        pm_gen_collect_locals_r(mt, i->body);
        for (PyAstNode* e = i->elif_clauses; e; e = e->next)
            pm_gen_collect_locals_r(mt, e);
        pm_gen_collect_locals_r(mt, i->else_body);
        break;
    }
    case PY_AST_NODE_ELIF:
        pm_gen_collect_locals_r(mt, ((PyIfNode*)node)->body);
        break;
    case PY_AST_NODE_WHILE:
        pm_gen_collect_locals_r(mt, ((PyWhileNode*)node)->body);
        break;
    case PY_AST_NODE_TRY: {
        PyTryNode* t = (PyTryNode*)node;
        pm_gen_collect_locals_r(mt, t->body);
        for (PyAstNode* h = t->handlers; h; h = h->next)
            pm_gen_collect_locals_r(mt, h);
        pm_gen_collect_locals_r(mt, t->else_body);
        pm_gen_collect_locals_r(mt, t->finally_body);
        break;
    }
    case PY_AST_NODE_WITH:
        pm_gen_collect_locals_r(mt, ((PyWithNode*)node)->body);
        break;
    case PY_AST_NODE_EXPRESSION_STATEMENT:
        pm_gen_collect_locals_r(mt, ((PyExpressionStatementNode*)node)->expression);
        break;
    default:
        break;
    }
}

// Save all generator locals to the frame (does NOT save state slot 0).
static void pm_gen_emit_save(PyMirTranspiler* mt) {
    for (int i = 0; i < mt->gen_local_count; i++) {
        PyMirVarEntry* v = pm_find_var(mt, mt->gen_local_names[i]);
        if (v && !v->from_env) {
            pm_store_env_slot(mt, mt->gen_frame_reg, mt->gen_local_frame_slots[i], v->reg);
        }
    }
}

// Restore all generator locals from the frame into their pre-registered MIR regs.
static void pm_gen_emit_restore(PyMirTranspiler* mt) {
    for (int i = 0; i < mt->gen_local_count; i++) {
        PyMirVarEntry* v = pm_find_var(mt, mt->gen_local_names[i]);
        if (v && !v->from_env) {
            MIR_reg_t loaded = pm_load_env_slot(mt, mt->gen_frame_reg, mt->gen_local_frame_slots[i]);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, v->reg),
                MIR_new_reg_op(mt->em.ctx, loaded)));
        }
    }
}

// Store a 64-bit integer into a raw pointer's slot (used to set frame[0] = state).
static void pm_gen_store_state(PyMirTranspiler* mt, int64_t state_val) {
    MIR_reg_t addr = pm_new_reg(mt, "gfst", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
        MIR_new_reg_op(mt->em.ctx, addr),
        MIR_new_reg_op(mt->em.ctx, mt->gen_frame_reg)));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
        MIR_new_mem_op(mt->em.ctx, MIR_T_I64, 0, addr, 0, 1),
        MIR_new_int_op(mt->em.ctx, state_val)));
}

// Forward-declare pm_transpile_statement (used by pm_transpile_for_gen).
static void pm_transpile_statement(PyMirTranspiler* mt, PyAstNode* stmt);

// For loop transpilation in generator context: uses py_get_iterator/py_iterator_next
// and stores the iterator in a named gen_local so it survives across yield points.
static void pm_transpile_for_gen(PyMirTranspiler* mt, PyForNode* forn) {
    char iter_vname[128];
    snprintf(iter_vname, sizeof(iter_vname), "_py__git_%d", mt->gen_iter_count++);

    // evaluate iterable
    MIR_reg_t iterable = pm_transpile_expression(mt, forn->right);
    // create iterator object
    MIR_reg_t iter_item = pm_call_1(mt, "py_get_iterator", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, iterable));

    // store in pre-registered gen_local register
    PyMirVarEntry* iter_var = pm_find_var(mt, iter_vname);
    if (iter_var && !iter_var->from_env) {
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, iter_var->reg),
            MIR_new_reg_op(mt->em.ctx, iter_item)));
    }

    MIR_label_t l_start    = pm_new_label(mt);
    MIR_label_t l_continue = pm_new_label(mt);
    MIR_label_t l_end      = pm_new_label(mt);

    if (!pm_require_capacity(mt, mt->loop_depth, PM_MAX_LOOPS, "nested loops")) return;
    mt->loop_stack[mt->loop_depth].continue_label = l_continue;
    mt->loop_stack[mt->loop_depth].break_label = l_end;
    mt->loop_depth++;

    pm_emit_label(mt, l_start);

    // get next item from iterator
    MIR_reg_t cur_iter = (iter_var && !iter_var->from_env) ? iter_var->reg : iter_item;
    MIR_reg_t item = pm_call_1(mt, "py_iterator_next", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, cur_iter));

    // check for stop iteration
    MIR_reg_t is_stop = pm_call_1(mt, "py_is_stop_iteration", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, item));
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
        MIR_new_label_op(mt->em.ctx, l_end),
        MIR_new_reg_op(mt->em.ctx, is_stop)));

    // assign loop target
    if (forn->left && forn->left->node_type == PY_AST_NODE_IDENTIFIER) {
        PyIdentifierNode* id = (PyIdentifierNode*)forn->left;
        char vname[128];
        snprintf(vname, sizeof(vname), "_py_%.*s", (int)id->name->len, id->name->chars);
        PyMirVarEntry* var = pm_find_var(mt, vname);
        if (var && !var->from_env) {
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, var->reg),
                MIR_new_reg_op(mt->em.ctx, item)));
        }
    }

    // body
    pm_transpile_block(mt, forn->body);

    pm_emit_label(mt, l_continue);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_start)));
    pm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
}

// Compile a generator function: emits a resume (state machine) function and a
// thin wrapper function that creates the generator object.
static void pm_compile_generator(PyMirTranspiler* mt, PyFuncCollected* fc);

// ============================================================================

static void pm_collect_functions_r(PyMirTranspiler* mt, PyAstNode* node, int parent_index) {
    if (!node) return;

    switch (node->node_type) {
    case PY_AST_NODE_MODULE: {
        PyModuleNode* mod = (PyModuleNode*)node;
        PyAstNode* s = mod->body;
        while (s) { pm_collect_functions_r(mt, s, parent_index); s = s->next; }
        break;
    }
    case PY_AST_NODE_BLOCK: {
        PyBlockNode* blk = (PyBlockNode*)node;
        PyAstNode* s = blk->statements;
        while (s) { pm_collect_functions_r(mt, s, parent_index); s = s->next; }
        break;
    }
    case PY_AST_NODE_FUNCTION_DEF: {
        PyFunctionDefNode* fn = (PyFunctionDefNode*)node;
        if (!pm_require_capacity(mt, mt->func_count, PM_MAX_FUNCTIONS,
                "function definitions")) return;

        int my_index = mt->func_count;
        PyFuncCollected* fc = &mt->func_entries[my_index];
        memset(fc, 0, sizeof(PyFuncCollected));
        fc->node = fn;
        fc->analysis = fn->analysis;
        fc->ext = (PyFnExt*)fn->ext.ptr;
        fc->parent_index = parent_index;
        snprintf(fc->name, sizeof(fc->name), "%.*s", (int)fn->name->len, fn->name->chars);

        // If the first decorator is @propname.setter or @propname.deleter, suffix the
        // internal name to avoid MIR function name collisions with the getter.
        if (fn->decorators) {
            PyDecoratorNode* first_dec = (PyDecoratorNode*)fn->decorators;
            if (first_dec->expression &&
                first_dec->expression->node_type == PY_AST_NODE_ATTRIBUTE) {
                PyAttributeNode* attr_dec = (PyAttributeNode*)first_dec->expression;
                if (attr_dec->attribute) {
                    if (attr_dec->attribute->len == 6 &&
                        strncmp(attr_dec->attribute->chars, "setter", 6) == 0) {
                        strncat(fc->name, "__setter",
                            sizeof(fc->name) - strlen(fc->name) - 1);
                    } else if (attr_dec->attribute->len == 7 &&
                               strncmp(attr_dec->attribute->chars, "deleter", 7) == 0) {
                        strncat(fc->name, "__deleter",
                            sizeof(fc->name) - strlen(fc->name) - 1);
                    }
                }
            }
        }

        // count params, detect *args
        int pc = 0;
        int dc = 0;
        PyAstNode* p = fn->params;
        while (p) {
            if (p->node_type == PY_AST_NODE_LIST_SPLAT_PARAMETER) {
                // *args parameter — don't count as regular param
                PyParamNode* sp = (PyParamNode*)p;
                fc->has_star_args = true;
                fc->star_args_index = pc;
                if (sp->name) {
                    snprintf(fc->star_args_name, sizeof(fc->star_args_name),
                        "_py_%.*s", (int)sp->name->len, sp->name->chars);
                } else {
                    snprintf(fc->star_args_name, sizeof(fc->star_args_name), "_py_args");
                }
                p = p->next;
                continue;
            }
            if (p->node_type == PY_AST_NODE_DICT_SPLAT_PARAMETER) {
                // **kwargs — record and skip (not counted as a regular param)
                PyParamNode* kp = (PyParamNode*)p;
                fc->has_kwargs = true;
                if (kp->name) {
                    snprintf(fc->kwargs_name, sizeof(fc->kwargs_name),
                        "_py_%.*s", (int)kp->name->len, kp->name->chars);
                } else {
                    snprintf(fc->kwargs_name, sizeof(fc->kwargs_name), "_py_kwargs");
                }
                p = p->next;
                continue;
            }
            if (p->node_type == PY_AST_NODE_DEFAULT_PARAMETER) dc++;
            pc++;
            p = p->next;
        }
        fc->param_count = pc;
        if (!pm_require_count(mt, fc->param_count, PM_MAX_MIR_PARAMS,
                "function parameters")) return;
        fc->required_count = pc - dc;
        fc->default_count = dc;

        // detect generator functions (body contains any yield / yield from)
        fc->is_generator = pm_has_yield_r(fn->body);
        fc->is_async     = fn->is_async;
        if (fc->is_async) fc->is_generator = true;  // async def always compiles as generator
        if (fc->analysis && fc->is_async) {
            fc->analysis->may_await = true;
            fc->analysis->needs_task_context = true;
        }
        if (fc->ext) {
            fc->ext->has_star_args = fc->has_star_args;
            fc->ext->has_kwargs = fc->has_kwargs;
            fc->ext->is_generator = fc->is_generator;
            fc->ext->is_async = fc->is_async;
            fc->ext->required_param_count = fc->required_count;
            fc->ext->default_param_count = fc->default_count;
        }

        mt->func_count++;

        // recurse into body for nested functions — they are children of this func
        pm_collect_functions_r(mt, fn->body, my_index);
        break;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* ifn = (PyIfNode*)node;
        pm_collect_functions_r(mt, ifn->body, parent_index);
        PyAstNode* elif = ifn->elif_clauses;
        while (elif) { pm_collect_functions_r(mt, elif, parent_index); elif = elif->next; }
        pm_collect_functions_r(mt, ifn->else_body, parent_index);
        break;
    }
    case PY_AST_NODE_ELIF: {
        PyIfNode* elif = (PyIfNode*)node;
        pm_collect_functions_r(mt, elif->body, parent_index);
        break;
    }
    case PY_AST_NODE_WHILE: {
        PyWhileNode* wh = (PyWhileNode*)node;
        pm_collect_functions_r(mt, wh->body, parent_index);
        break;
    }
    case PY_AST_NODE_FOR: {
        PyForNode* fn = (PyForNode*)node;
        pm_collect_functions_r(mt, fn->body, parent_index);
        break;
    }
    case PY_AST_NODE_CLASS_DEF: {
        // collect methods inside the class body; mark them as methods
        PyClassDefNode* cls = (PyClassDefNode*)node;
        int before = mt->func_count;
        // recurse: methods are direct children of class body (top-level block)
        PyAstNode* body_stmt = NULL;
        if (cls->body && cls->body->node_type == PY_AST_NODE_BLOCK) {
            body_stmt = ((PyBlockNode*)cls->body)->statements;
        } else {
            body_stmt = cls->body;
        }
        while (body_stmt) {
            pm_collect_functions_r(mt, body_stmt, parent_index);
            body_stmt = body_stmt->next;
        }
        // tag only DIRECT children of the class body as methods (not nested functions inside methods)
        for (int mi = before; mi < mt->func_count; mi++) {
            if (!mt->func_entries[mi].is_method && mt->func_entries[mi].parent_index == parent_index) {
                mt->func_entries[mi].is_method = true;
                snprintf(mt->func_entries[mi].class_name,
                    sizeof(mt->func_entries[mi].class_name),
                    "%.*s", (int)cls->name->len, cls->name->chars);
            }
        }
        break;
    }
    default:
        break;
    }
}

static void pm_collect_functions(PyMirTranspiler* mt, PyAstNode* node) {
    pm_collect_functions_r(mt, node, -1);
}

// ============================================================================
// Capture analysis for closures
// ============================================================================

// collect all identifiers referenced in an AST subtree (not descending into nested functions)
static void pm_collect_referenced_ids(PyAstNode* node, char refs[][128], int* ref_count, int max_refs) {
    if (!node || *ref_count >= max_refs) return;

    switch (node->node_type) {
    case PY_AST_NODE_IDENTIFIER: {
        PyIdentifierNode* id = (PyIdentifierNode*)node;
        char name[128];
        snprintf(name, sizeof(name), "_py_%.*s", (int)id->name->len, id->name->chars);
        // skip builtins
        if (strcmp(name, "_py_None") == 0 || strcmp(name, "_py_True") == 0 ||
            strcmp(name, "_py_False") == 0) break;
        // check if already in refs
        for (int i = 0; i < *ref_count; i++) {
            if (strcmp(refs[i], name) == 0) return;
        }
        if (*ref_count < max_refs) {
            snprintf(refs[*ref_count], 128, "%s", name);
            (*ref_count)++;
        }
        break;
    }
    case PY_AST_NODE_BINARY_OP: {
        PyBinaryNode* b = (PyBinaryNode*)node;
        pm_collect_referenced_ids(b->left, refs, ref_count, max_refs);
        pm_collect_referenced_ids(b->right, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_UNARY_OP:
    case PY_AST_NODE_NOT:
        pm_collect_referenced_ids(((PyUnaryNode*)node)->operand, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_COMPARE: {
        PyCompareNode* c = (PyCompareNode*)node;
        pm_collect_referenced_ids(c->left, refs, ref_count, max_refs);
        for (int i = 0; i < c->op_count; i++)
            pm_collect_referenced_ids(c->comparators[i], refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_BOOLEAN_OP: {
        PyBooleanNode* b = (PyBooleanNode*)node;
        pm_collect_referenced_ids(b->left, refs, ref_count, max_refs);
        pm_collect_referenced_ids(b->right, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_CALL: {
        PyCallNode* c = (PyCallNode*)node;
        pm_collect_referenced_ids(c->function, refs, ref_count, max_refs);
        PyAstNode* arg = c->arguments;
        while (arg) { pm_collect_referenced_ids(arg, refs, ref_count, max_refs); arg = arg->next; }
        break;
    }
    case PY_AST_NODE_KEYWORD_ARGUMENT:
        pm_collect_referenced_ids(((PyKeywordArgNode*)node)->value, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_ATTRIBUTE:
        pm_collect_referenced_ids(((PyAttributeNode*)node)->object, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_SUBSCRIPT: {
        PySubscriptNode* s = (PySubscriptNode*)node;
        pm_collect_referenced_ids(s->object, refs, ref_count, max_refs);
        pm_collect_referenced_ids(s->index, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_LIST:
    case PY_AST_NODE_TUPLE:
    case PY_AST_NODE_SET: {
        PyAstNode* e = ((PySequenceNode*)node)->elements;
        while (e) { pm_collect_referenced_ids(e, refs, ref_count, max_refs); e = e->next; }
        break;
    }
    case PY_AST_NODE_DICT: {
        PyAstNode* p = ((PyDictNode*)node)->pairs;
        while (p) { pm_collect_referenced_ids(p, refs, ref_count, max_refs); p = p->next; }
        break;
    }
    case PY_AST_NODE_PAIR: {
        PyPairNode* p = (PyPairNode*)node;
        pm_collect_referenced_ids(p->key, refs, ref_count, max_refs);
        pm_collect_referenced_ids(p->value, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_CONDITIONAL_EXPR: {
        PyConditionalNode* c = (PyConditionalNode*)node;
        pm_collect_referenced_ids(c->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(c->then, refs, ref_count, max_refs);
        pm_collect_referenced_ids(c->otherwise, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_RETURN:
        pm_collect_referenced_ids(((PyReturnNode*)node)->value, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_EXPRESSION_STATEMENT:
        pm_collect_referenced_ids(((PyExpressionStatementNode*)node)->expression, refs, ref_count, max_refs);
        break;
    case PY_AST_NODE_ASSIGNMENT: {
        PyAssignmentNode* a = (PyAssignmentNode*)node;
        pm_collect_referenced_ids(a->right, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_AUGMENTED_ASSIGNMENT: {
        PyAugAssignmentNode* a = (PyAugAssignmentNode*)node;
        pm_collect_referenced_ids(a->left, refs, ref_count, max_refs);
        pm_collect_referenced_ids(a->right, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_IF: {
        PyIfNode* i = (PyIfNode*)node;
        pm_collect_referenced_ids(i->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(i->body, refs, ref_count, max_refs);
        PyAstNode* e = i->elif_clauses;
        while (e) { pm_collect_referenced_ids(e, refs, ref_count, max_refs); e = e->next; }
        pm_collect_referenced_ids(i->else_body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_ELIF: {
        PyIfNode* e = (PyIfNode*)node;
        pm_collect_referenced_ids(e->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(e->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_WHILE: {
        PyWhileNode* w = (PyWhileNode*)node;
        pm_collect_referenced_ids(w->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(w->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_FOR: {
        PyForNode* f = (PyForNode*)node;
        pm_collect_referenced_ids(f->right, refs, ref_count, max_refs);
        pm_collect_referenced_ids(f->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_BLOCK: {
        PyAstNode* s = ((PyBlockNode*)node)->statements;
        while (s) { pm_collect_referenced_ids(s, refs, ref_count, max_refs); s = s->next; }
        break;
    }
    case PY_AST_NODE_FSTRING: {
        PyAstNode* p = ((PyFStringNode*)node)->parts;
        while (p) { pm_collect_referenced_ids(p, refs, ref_count, max_refs); p = p->next; }
        break;
    }
    case PY_AST_NODE_LAMBDA: {
        // for lambdas inside a function, collect refs from the lambda body too
        PyLambdaNode* lam = (PyLambdaNode*)node;
        pm_collect_referenced_ids(lam->body, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_ASSERT: {
        PyAssertNode* a = (PyAssertNode*)node;
        pm_collect_referenced_ids(a->test, refs, ref_count, max_refs);
        pm_collect_referenced_ids(a->message, refs, ref_count, max_refs);
        break;
    }
    case PY_AST_NODE_FUNCTION_DEF:
        // do NOT descend into nested function defs — they have their own scope
        break;
    default:
        break;
    }
}

// forward declaration — collects nonlocal names from function body
static void pm_collect_nonlocal_names(PyAstNode* body, char names[][128], int* count, int max);

// collect locally defined variable names (params + assignment targets + for targets)
// excludes names declared nonlocal
static void pm_collect_local_defs(PyFunctionDefNode* fn, char locals[][128], int* local_count, int max_locals) {
    // first collect nonlocal names to exclude them
    char nonlocals[32][128];
    int nonlocal_count = 0;
    pm_collect_nonlocal_names(fn->body, nonlocals, &nonlocal_count, 32);

    // params
    PyAstNode* p = fn->params;
    while (p) {
        String* pn = NULL;
        if (p->node_type == PY_AST_NODE_PARAMETER ||
            p->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
            p->node_type == PY_AST_NODE_TYPED_PARAMETER) {
            pn = ((PyParamNode*)p)->name;
        }
        if (pn && *local_count < max_locals) {
            snprintf(locals[*local_count], 128, "_py_%.*s", (int)pn->len, pn->chars);
            (*local_count)++;
        }
        p = p->next;
    }

    // walk the body for assignments and for-loop targets
    PyAstNode* s = fn->body ? (fn->body->node_type == PY_AST_NODE_BLOCK ?
        ((PyBlockNode*)fn->body)->statements : fn->body) : NULL;
    while (s) {
        if (s->node_type == PY_AST_NODE_ASSIGNMENT) {
            PyAssignmentNode* a = (PyAssignmentNode*)s;
            if (a->left && a->left->node_type == PY_AST_NODE_IDENTIFIER) {
                PyIdentifierNode* id = (PyIdentifierNode*)a->left;
                char name[128];
                snprintf(name, sizeof(name), "_py_%.*s", (int)id->name->len, id->name->chars);
                // check if declared nonlocal
                bool is_nl = false;
                for (int ni = 0; ni < nonlocal_count; ni++) {
                    if (strcmp(nonlocals[ni], name) == 0) { is_nl = true; break; }
                }
                if (is_nl) { s = s->next; continue; }
                // check duplicate
                bool dup = false;
                for (int i = 0; i < *local_count; i++) {
                    if (strcmp(locals[i], name) == 0) { dup = true; break; }
                }
                if (!dup && *local_count < max_locals) {
                    snprintf(locals[*local_count], 128, "%s", name);
                    (*local_count)++;
                }
            }
        } else if (s->node_type == PY_AST_NODE_FOR) {
            PyForNode* f = (PyForNode*)s;
            if (f->left && f->left->node_type == PY_AST_NODE_IDENTIFIER) {
                PyIdentifierNode* id = (PyIdentifierNode*)f->left;
                char name[128];
                snprintf(name, sizeof(name), "_py_%.*s", (int)id->name->len, id->name->chars);
                bool dup = false;
                for (int i = 0; i < *local_count; i++) {
                    if (strcmp(locals[i], name) == 0) { dup = true; break; }
                }
                if (!dup && *local_count < max_locals) {
                    snprintf(locals[*local_count], 128, "%s", name);
                    (*local_count)++;
                }
            }
        }
        s = s->next;
    }
}

// analyze captures for all collected functions
// Helper: check if a variable is directly defined (param or local) in a function's scope
static bool pm_var_in_func_scope(PyMirTranspiler* mt, PyFuncCollected* fc, const char* varname) {
    // check params
    PyAstNode* pp = fc->node->params;
    while (pp) {
        String* pn = NULL;
        if (pp->node_type == PY_AST_NODE_PARAMETER ||
            pp->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
            pp->node_type == PY_AST_NODE_TYPED_PARAMETER) {
            pn = ((PyParamNode*)pp)->name;
        }
        if (pn) {
            char pname[128];
            snprintf(pname, sizeof(pname), "_py_%.*s", (int)pn->len, pn->chars);
            if (strcmp(varname, pname) == 0) return true;
        }
        pp = pp->next;
    }
    // check locals
    char locs[64][128];
    int lcount = 0;
    pm_collect_local_defs(fc->node, locs, &lcount, 64);
    for (int i = 0; i < lcount; i++) {
        if (strcmp(varname, locs[i]) == 0) return true;
    }
    return false;
}

// Helper: add a capture to a function if not already present; returns true if added
static bool pm_add_capture_entry(PyMirTranspiler* mt, PyFuncCollected* fc,
        const char* varname) {
    for (int ci = 0; ci < fc->capture_count; ci++) {
        if (strcmp(fc->captures[ci], varname) == 0) return false; // already present
    }
    if (fc->capture_count < 32) {
        snprintf(fc->captures[fc->capture_count], 128, "%s", varname);
        fc->capture_count++;
        fc->is_closure = true;
        if (fc->analysis) {
            FnCapture* capture = (FnCapture*)arena_calloc(mt->tp->ast_arena,
                sizeof(FnCapture));
            snprintf(capture->name, sizeof(capture->name), "%s", varname);
            capture->next = fc->analysis->captures;
            fc->analysis->captures = capture;
            fc->analysis->capture_count++;
        }
        return true;
    }
    return false;
}

static void pm_analyze_captures(PyMirTranspiler* mt) {
    for (int fi = 0; fi < mt->func_count; fi++) {
        PyFuncCollected* fc = &mt->func_entries[fi];
        if (fc->parent_index < 0) continue;  // top-level function, no captures

        // collect all identifiers referenced in this function body
        char refs[128][128];
        int ref_count = 0;
        pm_collect_referenced_ids(fc->node->body, refs, &ref_count, 128);

        // collect locally defined names
        char locals[64][128];
        int local_count = 0;
        pm_collect_local_defs(fc->node, locals, &local_count, 64);

        for (int ri = 0; ri < ref_count; ri++) {
            // skip if locally defined
            bool is_local = false;
            for (int li = 0; li < local_count; li++) {
                if (strcmp(refs[ri], locals[li]) == 0) { is_local = true; break; }
            }
            if (is_local) continue;

            // skip known function names (resolved separately via local_funcs)
            bool is_func = false;
            for (int fi2 = 0; fi2 < mt->func_count; fi2++) {
                char fname[140];
                snprintf(fname, sizeof(fname), "_py_%s", mt->func_entries[fi2].name);
                if (strcmp(refs[ri], fname) == 0) { is_func = true; break; }
            }
            if (is_func) continue;

            // Walk up the ancestor chain to find where this variable is defined.
            // Collect the chain of intermediate ancestors (parent, grandparent, ...)
            // so that we can propagate the capture upward if needed.
            int chain[16];     // ancestor indices, from parent (chain[0]) up
            int chain_len = 0;
            int anc_idx = fc->parent_index;
            bool found = false;

            while (anc_idx >= 0 && chain_len < 16) {
                PyFuncCollected* anc = &mt->func_entries[anc_idx];
                chain[chain_len++] = anc_idx;

                // variable is directly defined (param or local) in this ancestor
                if (pm_var_in_func_scope(mt, anc, refs[ri])) {
                    found = true;
                    break;
                }
                // ancestor already captures it (from its own parent)
                for (int ci = 0; ci < anc->capture_count; ci++) {
                    if (strcmp(anc->captures[ci], refs[ri]) == 0) { found = true; break; }
                }
                if (found) break;

                anc_idx = anc->parent_index;
            }

            if (!found) continue;

            // Add capture to fc itself
            pm_add_capture_entry(mt, fc, refs[ri]);
            log_debug("py-mir: function '%s' captures '%s' (found at depth %d)",
                fc->name, refs[ri], chain_len);

            // Propagate through all intermediate ancestors:
            // chain[0]=direct parent, ..., chain[chain_len-1]=where V was found.
            // The last entry in chain already has V (directly or via its own capture),
            // so only add to chain[0] .. chain[chain_len-2].
            for (int ci = 0; ci < chain_len - 1; ci++) {
                if (pm_add_capture_entry(mt, &mt->func_entries[chain[ci]], refs[ri])) {
                    log_debug("py-mir: propagated capture '%s' to intermediate function '%s'",
                        refs[ri], mt->func_entries[chain[ci]].name);
                }
            }
        }
    }
}
// analyze captures for lambdas — similar to functions
static void pm_analyze_lambda_captures(PyMirTranspiler* mt) {
    for (int li = 0; li < mt->lambda_count; li++) {
        PyLambdaCollected* lc = &mt->lambda_entries[li];
        if (lc->parent_index < 0) continue;

        char refs[128][128];
        int ref_count = 0;
        pm_collect_referenced_ids(lc->node->body, refs, &ref_count, 128);

        // lambda's own params are local
        char locals[32][128];
        int local_count = 0;
        PyAstNode* p = lc->node->params;
        while (p) {
            String* pn = NULL;
            if (p->node_type == PY_AST_NODE_PARAMETER ||
                p->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                p->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)p)->name;
            }
            if (pn && local_count < 32) {
                snprintf(locals[local_count], 128, "_py_%.*s", (int)pn->len, pn->chars);
                local_count++;
            }
            p = p->next;
        }

        for (int ri = 0; ri < ref_count; ri++) {
            bool is_local = false;
            for (int loi = 0; loi < local_count; loi++) {
                if (strcmp(refs[ri], locals[loi]) == 0) { is_local = true; break; }
            }
            if (is_local) continue;

            // check if it's a known function
            bool is_func = false;
            for (int fi = 0; fi < mt->func_count; fi++) {
                char fname[140];
                snprintf(fname, sizeof(fname), "_py_%s", mt->func_entries[fi].name);
                if (strcmp(refs[ri], fname) == 0) { is_func = true; break; }
            }
            if (is_func) continue;

            // check parent function's scope
            PyFuncCollected* parent = &mt->func_entries[lc->parent_index];
            bool in_parent = false;
            PyAstNode* pp = parent->node->params;
            while (pp) {
                String* pn = NULL;
                if (pp->node_type == PY_AST_NODE_PARAMETER ||
                    pp->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                    pp->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                    pn = ((PyParamNode*)pp)->name;
                }
                if (pn) {
                    char pname[128];
                    snprintf(pname, sizeof(pname), "_py_%.*s", (int)pn->len, pn->chars);
                    if (strcmp(refs[ri], pname) == 0) { in_parent = true; break; }
                }
                pp = pp->next;
            }
            if (!in_parent) {
                char plocs[64][128];
                int ploc_count = 0;
                pm_collect_local_defs(parent->node, plocs, &ploc_count, 64);
                for (int pli = 0; pli < ploc_count; pli++) {
                    if (strcmp(refs[ri], plocs[pli]) == 0) { in_parent = true; break; }
                }
            }
            if (!in_parent) continue;

            if (lc->capture_count < 32) {
                snprintf(lc->captures[lc->capture_count], 128, "%s", refs[ri]);
                lc->capture_count++;
                lc->is_closure = true;
                if (lc->analysis) {
                    FnCapture* capture = (FnCapture*)arena_calloc(mt->tp->ast_arena,
                        sizeof(FnCapture));
                    snprintf(capture->name, sizeof(capture->name), "%s", refs[ri]);
                    capture->next = lc->analysis->captures;
                    lc->analysis->captures = capture;
                    lc->analysis->capture_count++;
                }
                log_debug("py-mir: lambda '%s' captures '%s' from parent '%s'",
                    lc->name, refs[ri], parent->name);
            }
        }
    }
}

// collect nonlocal declarations from a function's body (not recursing into nested functions)
static void pm_collect_nonlocal_names(PyAstNode* body, char names[][128], int* count, int max) {
    if (!body) return;
    if (body->node_type == PY_AST_NODE_NONLOCAL) {
        PyGlobalNonlocalNode* nl = (PyGlobalNonlocalNode*)body;
        for (int i = 0; i < nl->name_count && *count < max; i++) {
            snprintf(names[*count], 128, "_py_%.*s", (int)nl->names[i]->len, nl->names[i]->chars);
            (*count)++;
        }
        return;
    }
    if (body->node_type == PY_AST_NODE_BLOCK) {
        PyAstNode* s = ((PyBlockNode*)body)->statements;
        while (s) { pm_collect_nonlocal_names(s, names, count, max); s = s->next; }
        return;
    }
    // check first-level statements in if/while/for bodies, but don't recurse into nested functions
    if (body->node_type == PY_AST_NODE_IF) {
        PyIfNode* ifn = (PyIfNode*)body;
        pm_collect_nonlocal_names(ifn->body, names, count, max);
        pm_collect_nonlocal_names(ifn->else_body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_WHILE) {
        pm_collect_nonlocal_names(((PyWhileNode*)body)->body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_FOR) {
        pm_collect_nonlocal_names(((PyForNode*)body)->body, names, count, max);
        return;
    }
    // don't descend into nested function defs
}

// analyze nonlocal declarations: mark child captures as nonlocal and parent's shared env
static void pm_analyze_nonlocal(PyMirTranspiler* mt) {
    for (int fi = 0; fi < mt->func_count; fi++) {
        PyFuncCollected* fc = &mt->func_entries[fi];
        if (fc->parent_index < 0) continue;

        // collect nonlocal names from this function
        char nl_names[32][128];
        int nl_count = 0;
        pm_collect_nonlocal_names(fc->node->body, nl_names, &nl_count, 32);
        if (nl_count == 0) continue;

        PyFuncCollected* parent = &mt->func_entries[fc->parent_index];

        for (int ni = 0; ni < nl_count; ni++) {
            // mark this capture as nonlocal
            for (int ci = 0; ci < fc->capture_count; ci++) {
                if (strcmp(fc->captures[ci], nl_names[ni]) == 0) {
                    fc->capture_is_nonlocal[ci] = true;
                    for (FnCapture* capture = fc->analysis ? fc->analysis->captures : NULL;
                            capture; capture = capture->next) {
                        if (strcmp(capture->name, nl_names[ni]) == 0) {
                            // A nonlocal write must retain the shared environment cell.
                            capture->force_env_capture = true;
                            break;
                        }
                    }
                    break;
                }
            }
            // add to parent's shared env
            bool already = false;
            for (int si = 0; si < parent->shared_env_count; si++) {
                if (strcmp(parent->shared_env_names[si], nl_names[ni]) == 0) {
                    already = true;
                    break;
                }
            }
            if (!already && parent->shared_env_count < 32) {
                snprintf(parent->shared_env_names[parent->shared_env_count], 128, "%s", nl_names[ni]);
                parent->shared_env_count++;
                parent->has_nonlocal_children = true;
                log_debug("py-mir: parent '%s' shares env var '%s' for nonlocal",
                    parent->name, nl_names[ni]);
            }
        }
    }
}

// collect global declarations from a function's body (not recursing into nested functions)
static void pm_collect_global_names(PyAstNode* body, char names[][128], int* count, int max) {
    if (!body) return;
    if (body->node_type == PY_AST_NODE_GLOBAL) {
        PyGlobalNonlocalNode* gn = (PyGlobalNonlocalNode*)body;
        for (int i = 0; i < gn->name_count && *count < max; i++) {
            snprintf(names[*count], 128, "_py_%.*s", (int)gn->names[i]->len, gn->names[i]->chars);
            (*count)++;
        }
        return;
    }
    if (body->node_type == PY_AST_NODE_BLOCK) {
        PyAstNode* s = ((PyBlockNode*)body)->statements;
        while (s) { pm_collect_global_names(s, names, count, max); s = s->next; }
        return;
    }
    if (body->node_type == PY_AST_NODE_IF) {
        PyIfNode* ifn = (PyIfNode*)body;
        pm_collect_global_names(ifn->body, names, count, max);
        pm_collect_global_names(ifn->else_body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_WHILE) {
        pm_collect_global_names(((PyWhileNode*)body)->body, names, count, max);
        return;
    }
    if (body->node_type == PY_AST_NODE_FOR) {
        pm_collect_global_names(((PyForNode*)body)->body, names, count, max);
        return;
    }
    // don't descend into nested function defs
}

static bool pm_register_global_var(PyMirTranspiler* mt, const char* name,
        const char* category);

// analyze global declarations: collect global names into func_entries and allocate module var indices
static void pm_analyze_globals(PyMirTranspiler* mt) {
    for (int fi = 0; fi < mt->func_count; fi++) {
        PyFuncCollected* fc = &mt->func_entries[fi];
        // collect global names from this function's body
        pm_collect_global_names(fc->node->body, fc->global_names, &fc->global_name_count, 16);
        // allocate module var indices for each global name
        for (int gi = 0; gi < fc->global_name_count; gi++) {
            // check if already allocated
            bool found = false;
            for (int vi = 0; vi < mt->global_var_count; vi++) {
                if (strcmp(mt->global_var_names[vi], fc->global_names[gi]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) pm_register_global_var(mt, fc->global_names[gi], "global var");
        }
    }
}

// check if a variable name is declared global in the current function,
// or if at module level, check if any function declared it global
static bool pm_is_global_in_current_func(PyMirTranspiler* mt, const char* vname) {
    if (mt->current_func_index >= 0) {
        // inside a function: check if this function has a `global` declaration for this name
        PyFuncCollected* fc = &mt->func_entries[mt->current_func_index];
        for (int i = 0; i < fc->global_name_count; i++) {
            if (strcmp(fc->global_names[i], vname) == 0) return true;
        }
        return false;
    }
    // at module level: check if any function declared this variable as global
    for (int i = 0; i < mt->global_var_count; i++) {
        if (strcmp(mt->global_var_names[i], vname) == 0) return true;
    }
    return false;
}

// get the module var index for a global variable name
static int pm_get_global_var_index(PyMirTranspiler* mt, const char* vname) {
    for (int i = 0; i < mt->global_var_count; i++) {
        if (strcmp(mt->global_var_names[i], vname) == 0) return mt->global_var_indices[i];
    }
    return -1;
}

static bool pm_register_global_var(PyMirTranspiler* mt, const char* name,
        const char* category) {
    for (int i = 0; i < mt->global_var_count; i++) {
        if (strcmp(mt->global_var_names[i], name) == 0) return true;
    }
    if (!pm_require_capacity(mt, mt->global_var_count, PM_MAX_GLOBAL_VARS,
            "module global variables")) return false;
    int index = mt->module_var_count++;
    snprintf(mt->global_var_names[mt->global_var_count], 128, "%s", name);
    mt->global_var_indices[mt->global_var_count] = index;
    mt->global_var_count++;
    log_debug("py-mir: %s '%s' → module_var[%d]", category, name, index);
    return true;
}

// walk all AST nodes to find lambda expressions, pre-collect them
static void pm_collect_lambdas_r(PyMirTranspiler* mt, PyAstNode* node, int parent_func_index) {
    if (!node) return;

    if (node->node_type == PY_AST_NODE_LAMBDA) {
        if (pm_require_capacity(mt, mt->lambda_count, PM_MAX_LAMBDAS,
                "lambda expressions")) {
            PyLambdaCollected* lc = &mt->lambda_entries[mt->lambda_count];
            memset(lc, 0, sizeof(PyLambdaCollected));
            lc->node = (PyLambdaNode*)node;
            lc->analysis = lc->node->analysis;
            lc->ext = (PyFnExt*)lc->node->ext.ptr;
            lc->parent_index = parent_func_index;
            snprintf(lc->name, sizeof(lc->name), "__lambda_%d", mt->lambda_count);
            int pc = 0;
            PyAstNode* p = lc->node->params;
            while (p) { pc++; p = p->next; }
            lc->param_count = pc;
            if (pm_require_count(mt, lc->param_count, PM_MAX_MIR_PARAMS,
                    "lambda parameters")) mt->lambda_count++;
        }
        pm_collect_lambdas_r(mt, ((PyLambdaNode*)node)->body, parent_func_index);
        return;
    }

    switch (node->node_type) {
    case PY_AST_NODE_MODULE:
        { PyAstNode* s = ((PyModuleNode*)node)->body; while (s) { pm_collect_lambdas_r(mt, s, parent_func_index); s = s->next; } } break;
    case PY_AST_NODE_BLOCK:
        { PyAstNode* s = ((PyBlockNode*)node)->statements; while (s) { pm_collect_lambdas_r(mt, s, parent_func_index); s = s->next; } } break;
    case PY_AST_NODE_EXPRESSION_STATEMENT:
        pm_collect_lambdas_r(mt, ((PyExpressionStatementNode*)node)->expression, parent_func_index); break;
    case PY_AST_NODE_ASSIGNMENT:
        { PyAssignmentNode* a = (PyAssignmentNode*)node; pm_collect_lambdas_r(mt, a->right, parent_func_index); } break;
    case PY_AST_NODE_AUGMENTED_ASSIGNMENT:
        { PyAugAssignmentNode* a = (PyAugAssignmentNode*)node; pm_collect_lambdas_r(mt, a->right, parent_func_index); } break;
    case PY_AST_NODE_BINARY_OP:
        { PyBinaryNode* b = (PyBinaryNode*)node; pm_collect_lambdas_r(mt, b->left, parent_func_index); pm_collect_lambdas_r(mt, b->right, parent_func_index); } break;
    case PY_AST_NODE_UNARY_OP:
    case PY_AST_NODE_NOT:
        pm_collect_lambdas_r(mt, ((PyUnaryNode*)node)->operand, parent_func_index); break;
    case PY_AST_NODE_COMPARE:
        { PyCompareNode* c = (PyCompareNode*)node; pm_collect_lambdas_r(mt, c->left, parent_func_index);
          for (int i = 0; i < c->op_count; i++) pm_collect_lambdas_r(mt, c->comparators[i], parent_func_index); } break;
    case PY_AST_NODE_BOOLEAN_OP:
        { PyBooleanNode* b = (PyBooleanNode*)node; pm_collect_lambdas_r(mt, b->left, parent_func_index); pm_collect_lambdas_r(mt, b->right, parent_func_index); } break;
    case PY_AST_NODE_CALL:
        { PyCallNode* c = (PyCallNode*)node; pm_collect_lambdas_r(mt, c->function, parent_func_index);
          PyAstNode* arg = c->arguments; while (arg) { pm_collect_lambdas_r(mt, arg, parent_func_index); arg = arg->next; } } break;
    case PY_AST_NODE_KEYWORD_ARGUMENT:
        pm_collect_lambdas_r(mt, ((PyKeywordArgNode*)node)->value, parent_func_index); break;
    case PY_AST_NODE_ATTRIBUTE:
        pm_collect_lambdas_r(mt, ((PyAttributeNode*)node)->object, parent_func_index); break;
    case PY_AST_NODE_SUBSCRIPT:
        { PySubscriptNode* s = (PySubscriptNode*)node; pm_collect_lambdas_r(mt, s->object, parent_func_index); pm_collect_lambdas_r(mt, s->index, parent_func_index); } break;
    case PY_AST_NODE_LIST:
    case PY_AST_NODE_TUPLE:
    case PY_AST_NODE_SET:
        { PyAstNode* e = ((PySequenceNode*)node)->elements; while (e) { pm_collect_lambdas_r(mt, e, parent_func_index); e = e->next; } } break;
    case PY_AST_NODE_DICT:
        { PyAstNode* p = ((PyDictNode*)node)->pairs; while (p) { pm_collect_lambdas_r(mt, p, parent_func_index); p = p->next; } } break;
    case PY_AST_NODE_PAIR:
        { PyPairNode* p = (PyPairNode*)node; pm_collect_lambdas_r(mt, p->key, parent_func_index); pm_collect_lambdas_r(mt, p->value, parent_func_index); } break;
    case PY_AST_NODE_CONDITIONAL_EXPR:
        { PyConditionalNode* c = (PyConditionalNode*)node; pm_collect_lambdas_r(mt, c->test, parent_func_index); pm_collect_lambdas_r(mt, c->then, parent_func_index); pm_collect_lambdas_r(mt, c->otherwise, parent_func_index); } break;
    case PY_AST_NODE_IF:
        { PyIfNode* i = (PyIfNode*)node; pm_collect_lambdas_r(mt, i->test, parent_func_index); pm_collect_lambdas_r(mt, i->body, parent_func_index);
          PyAstNode* e = i->elif_clauses; while (e) { pm_collect_lambdas_r(mt, e, parent_func_index); e = e->next; }
          pm_collect_lambdas_r(mt, i->else_body, parent_func_index); } break;
    case PY_AST_NODE_ELIF:
        { PyIfNode* e = (PyIfNode*)node; pm_collect_lambdas_r(mt, e->test, parent_func_index); pm_collect_lambdas_r(mt, e->body, parent_func_index); } break;
    case PY_AST_NODE_WHILE:
        { PyWhileNode* w = (PyWhileNode*)node; pm_collect_lambdas_r(mt, w->test, parent_func_index); pm_collect_lambdas_r(mt, w->body, parent_func_index); } break;
    case PY_AST_NODE_FOR:
        { PyForNode* f = (PyForNode*)node; pm_collect_lambdas_r(mt, f->right, parent_func_index); pm_collect_lambdas_r(mt, f->body, parent_func_index); } break;
    case PY_AST_NODE_RETURN:
        pm_collect_lambdas_r(mt, ((PyReturnNode*)node)->value, parent_func_index); break;
    case PY_AST_NODE_FUNCTION_DEF: {
        PyFunctionDefNode* fdef = (PyFunctionDefNode*)node;
        int func_idx = parent_func_index;
        for (int ii = 0; ii < mt->func_count; ii++) {
            if (mt->func_entries[ii].node == fdef) { func_idx = ii; break; }
        }
        pm_collect_lambdas_r(mt, fdef->body, func_idx);
        break;
    }
    case PY_AST_NODE_LIST_COMPREHENSION:
    case PY_AST_NODE_DICT_COMPREHENSION:
    case PY_AST_NODE_SET_COMPREHENSION:
        { PyComprehensionNode* c = (PyComprehensionNode*)node;
          pm_collect_lambdas_r(mt, c->element, parent_func_index); pm_collect_lambdas_r(mt, c->iter, parent_func_index);
          pm_collect_lambdas_r(mt, c->conditions, parent_func_index); pm_collect_lambdas_r(mt, c->inner, parent_func_index); } break;
    case PY_AST_NODE_FSTRING:
        { PyAstNode* p = ((PyFStringNode*)node)->parts; while (p) { pm_collect_lambdas_r(mt, p, parent_func_index); p = p->next; } } break;
    case PY_AST_NODE_CLASS_DEF: {
        // recurse into class body so lambdas inside methods are collected
        PyClassDefNode* cls = (PyClassDefNode*)node;
        PyAstNode* body_stmt = NULL;
        if (cls->body && cls->body->node_type == PY_AST_NODE_BLOCK) {
            body_stmt = ((PyBlockNode*)cls->body)->statements;
        } else {
            body_stmt = cls->body;
        }
        while (body_stmt) {
            pm_collect_lambdas_r(mt, body_stmt, parent_func_index);
            body_stmt = body_stmt->next;
        }
        break;
    }
    default:
        break;
    }
}

// ============================================================================
// MIR function name generation — ensures unique names for nested functions
// ============================================================================

static void pm_make_mir_func_name(PyMirTranspiler* mt, PyFuncCollected* fc, char* fn_name, int fn_name_size) {
    if (fc->is_method && fc->class_name[0]) {
        snprintf(fn_name, fn_name_size, "_%s_%s", fc->class_name, fc->name);
    } else if (fc->parent_index >= 0) {
        // nested function — include parent info for uniqueness
        PyFuncCollected* parent = &mt->func_entries[fc->parent_index];
        if (parent->is_method && parent->class_name[0]) {
            snprintf(fn_name, fn_name_size, "_%s_%s__%s", parent->class_name, parent->name, fc->name);
        } else {
            snprintf(fn_name, fn_name_size, "_%s__%s", parent->name, fc->name);
        }
    } else {
        snprintf(fn_name, fn_name_size, "_%s", fc->name);
    }
}

// ============================================================================
// Generator compilation (Phase A)
// ============================================================================

static void pm_compile_generator(PyMirTranspiler* mt, PyFuncCollected* fc) {
    char fn_name[260];
    pm_make_mir_func_name(mt, fc, fn_name, sizeof(fn_name));
    log_debug("py-mir: compiling generator '%s'", fn_name);

    // ---- Step 1: Collect all local variables ----
    mt->gen_local_count = 0;
    mt->gen_iter_count  = 0;

    // params first (slots 1..param_count)
    {
        PyAstNode* pnode = fc->node->params;
        while (pnode) {
            if (pnode->node_type == PY_AST_NODE_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                String* pn = ((PyParamNode*)pnode)->name;
                if (pn) {
                    char vn[130];
                    snprintf(vn, sizeof(vn), "_py_%.*s", (int)pn->len, pn->chars);
                    pm_gen_add_local(mt, vn);
                }
            }
            pnode = pnode->next;
        }
    }
    // other locals from body
    pm_gen_collect_locals_r(mt, fc->node->body);
    int num_locals  = mt->gen_local_count;
    int frame_size  = num_locals + 1;   // slot 0 = state, 1..N = locals

    // ---- Step 2: Count yield points ----
    mt->gen_iter_count = 0;  // reset so that pm_count reuse is clean (not used there)
    int yield_count = pm_count_yield_points_r(fc->node->body);
    if (!pm_require_count(mt, yield_count, PM_MAX_GENERATOR_SLOTS,
            "generator yield points")) return;

    // ---- Step 3: Build param descriptor for both functions ----
    int offset = 0;
    char* param_name_bufs[21];
    uint8_t param_kinds[21] = {};

    PyAstNode* pnode = fc->node->params;
    for (int i = 0; i < fc->param_count && i < 16; i++) {
        param_name_bufs[i + offset] = (char*)alloca(128);
        String* pn = NULL;
        while (pnode) {
            if (pnode->node_type == PY_AST_NODE_LIST_SPLAT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DICT_SPLAT_PARAMETER) {
                pnode = pnode->next;
                continue;
            }
            if (pnode->node_type == PY_AST_NODE_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)pnode)->name;
            }
            pnode = pnode->next;
            break;
        }
        if (pn) snprintf(param_name_bufs[i + offset], 128, "_py_%.*s", (int)pn->len, pn->chars);
        else    snprintf(param_name_bufs[i + offset], 128, "_py_p%d", i);
    }
    int mir_param_count = fc->param_count + offset;

    // ---- Step 4: Save transpiler state ----
    MIR_item_t old_func_item      = mt->em.func_item;
    MIR_func_t old_func           = mt->em.func;
    MirFunctionArgumentState old_argument_registers = em_function_arguments_suspend(&mt->em);
    int        old_scope_depth    = mt->scope_depth;
    int        old_func_index     = mt->current_func_index;
    MIR_reg_t  old_env_reg        = mt->scope_env_reg;
    int        old_env_slot_count = mt->scope_env_slot_count;
    bool       old_in_generator   = mt->in_generator;
    bool       old_in_async       = mt->in_async;
    MIR_reg_t  old_gen_frame_reg  = mt->gen_frame_reg;
    MIR_reg_t  old_gen_sent_reg   = mt->gen_sent_reg;
    int        old_gen_ycount     = mt->gen_yield_count;
    int        old_gen_lcount     = mt->gen_local_count;
    int        old_gen_iter_count = mt->gen_iter_count;

    // ---- Step 5: Compile RESUME function ----
    char resume_name[300];
    snprintf(resume_name, sizeof(resume_name), "_gen_resume%s", fn_name); // e.g. _gen_resume_counter

    char rp0_buf[] = "_gen_frame";
    char rp1_buf[] = "_gen_sent";
    char rp2_buf[] = "_py__scalar_home";
    const char* resume_params[] = {rp0_buf, rp1_buf, rp2_buf};
    const uint8_t resume_param_kinds[] = {1, 0, 1};
    MIR_item_t resume_func_item = NULL;
    MIR_func_t resume_func = NULL;
    if (!pm_create_hosted_item_function_typed(mt->em.ctx, resume_name, 3,
            resume_params, resume_param_kinds, &resume_func_item, &resume_func)) {
        log_error("py-mir: host could not create generator resume function '%s'", resume_name);
        mt->has_compile_error = true;
        return;
    }

    pm_select_hosted_function(mt, resume_func_item, resume_func);
    // A resume activation can allocate between yields just like a normal
    // Python function; it must publish its locals before any helper call.
    pm_begin_function_frame(mt, pm_load_side_stack_runtime(mt));
    pm_enable_scalar_return_home(mt, "_py__scalar_home");
    mt->scope_env_reg      = 0;
    mt->scope_env_slot_count = 0;
    mt->in_generator       = true;
    mt->in_async           = fc->is_async;
    mt->gen_frame_reg      = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "_gen_frame");
    mt->gen_sent_reg       = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "_gen_sent");
    mt->gen_yield_count    = 0;
    mt->gen_iter_count     = 0;

    // find function index for try-depth tracking
    for (int i = 0; i < mt->func_count; i++) {
        if (&mt->func_entries[i] == fc) { mt->current_func_index = i; break; }
    }

    // pre-create yield resume labels
    for (int i = 0; i < yield_count && i < 32; i++) {
        mt->gen_yield_labels[i] = pm_new_label(mt);
    }

    pm_push_scope(mt);

    // pre-create MIR regs for ALL locals and register them in scope
    for (int li = 0; li < num_locals; li++) {
        MIR_reg_t r = pm_new_reg(mt, mt->gen_local_names[li], MIR_T_I64);
        pm_set_var(mt, mt->gen_local_names[li], r);
    }

    // Emit dispatch table: load state, branch to body_start or resume labels
    MIR_label_t l_body_start = pm_new_label(mt);
    MIR_label_t l_exhausted  = pm_new_label(mt);

    // load state = frame[0]
    MIR_reg_t state_reg = pm_new_reg(mt, "gen_state", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
        MIR_new_reg_op(mt->em.ctx, state_reg),
        MIR_new_mem_op(mt->em.ctx, MIR_T_I64, 0, mt->gen_frame_reg, 0, 1)));

    // BEQ state == 0 → l_body_start
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BEQ,
        MIR_new_label_op(mt->em.ctx, l_body_start),
        MIR_new_reg_op(mt->em.ctx, state_reg),
        MIR_new_int_op(mt->em.ctx, 0)));

    // BEQ state == N+1 → gen_yield_labels[N] for each yield point
    for (int i = 0; i < yield_count && i < 32; i++) {
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BEQ,
            MIR_new_label_op(mt->em.ctx, mt->gen_yield_labels[i]),
            MIR_new_reg_op(mt->em.ctx, state_reg),
            MIR_new_int_op(mt->em.ctx, i + 1)));
    }
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_JMP, MIR_new_label_op(mt->em.ctx, l_exhausted)));

    // --- body start: restore all locals (params from frame, others = ItemNull) ---
    pm_emit_label(mt, l_body_start);
    pm_gen_emit_restore(mt);

    // compile function body (yield expressions emit save+ret+label+restore inline)
    pm_transpile_block(mt, fc->node->body);

    // --- exhaustion epilogue ---
    pm_emit_label(mt, l_exhausted);
    pm_gen_store_state(mt, -1);
    if (fc->is_async) {
        // async def with no explicit return: store None as the coroutine return value
        MIR_reg_t none_r = pm_emit_null(mt);
        pm_call_1(mt, "py_coro_set_return", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, none_r));
    }
    MIR_reg_t si_reg = pm_call_0(mt, "py_stop_iteration", MIR_T_I64);
    pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, si_reg)));

    pm_finish_function_frame(mt, resume_name);
    pm_finish_hosted_mir_function(mt->em.ctx);
    pm_pop_scope(mt);

    // restore transpiler state (but keep gen_local_names for wrapper compilation)
    mt->in_generator      = old_in_generator;
    mt->in_async          = old_in_async;
    mt->gen_frame_reg     = old_gen_frame_reg;
    mt->gen_sent_reg      = old_gen_sent_reg;
    mt->gen_yield_count   = old_gen_ycount;
    mt->gen_iter_count    = old_gen_iter_count;
    mt->scope_env_reg     = old_env_reg;
    mt->scope_env_slot_count = old_env_slot_count;
    mt->current_func_index = old_func_index;

    // ---- Step 6: Compile WRAPPER function ----
    // The wrapper: same name/params as original Python function.
    // Body: allocate generator object with resume func ptr + frame, store params, return gen.
    param_name_bufs[mir_param_count] = (char*)alloca(128);
    snprintf(param_name_bufs[mir_param_count], 128, "_py__scalar_home");
    param_kinds[mir_param_count] = 1;
    mir_param_count++;
    MIR_func_t wrapper_func = NULL;
    if (!pm_create_hosted_item_function_typed(mt->em.ctx, fn_name, mir_param_count,
            (const char* const*)param_name_bufs, param_kinds, &fc->func_item,
            &wrapper_func)) {
        log_error("py-mir: host could not create generator wrapper '%s'", fn_name);
        mt->has_compile_error = true;
        return;
    }
    pm_select_hosted_function(mt, fc->func_item, wrapper_func);
    pm_begin_function_frame(mt, pm_load_side_stack_runtime(mt));
    pm_enable_scalar_return_home(mt, "_py__scalar_home");

    // load address of resume function as i64
    MIR_reg_t rptr = pm_new_reg(mt, "genfptr", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
        MIR_new_reg_op(mt->em.ctx, rptr),
        MIR_new_ref_op(mt->em.ctx, resume_func_item)));

    // gen = py_gen_create(rptr, frame_size) or py_coro_create(rptr, frame_size) for async
    const char* create_fn = fc->is_async ? "py_coro_create" : "py_gen_create";
    MIR_reg_t gen_obj = pm_call_2(mt, create_fn, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, rptr),
        MIR_T_I64, MIR_new_int_op(mt->em.ctx, frame_size));

    // frame_ptr = py_gen_get_frame_c(gen_obj) — raw uint64_t* as i64
    MIR_reg_t fptr_reg = pm_call_1(mt, "py_gen_get_frame_c", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->em.ctx, gen_obj));

    // store params into frame slots 1..param_count
    for (int i = 0; i < fc->param_count && i < num_locals; i++) {
        MIR_reg_t preg = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func,
            param_name_bufs[i + offset]);
        pm_store_env_slot(mt, fptr_reg, i + 1, preg);
    }

    pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, gen_obj)));
    pm_finish_function_frame(mt, fn_name);
    pm_finish_hosted_mir_function(mt->em.ctx);

    // restore remaining parent state
    pm_restore_hosted_function(mt, old_func_item, old_func, old_argument_registers);
    mt->scope_depth       = old_scope_depth;
    mt->gen_local_count   = old_gen_lcount;

    // register wrapper in local_funcs so call sites can find it
    if (!fc->is_method) {
        PyLocalFuncEntry lfe;
        memset(&lfe, 0, sizeof(lfe));
        snprintf(lfe.name, sizeof(lfe.name), "%s", fn_name);
        lfe.func_item = fc->func_item;
        hashmap_set(mt->local_funcs, &lfe);
        log_debug("py-mir: generator wrapper '%s' registered (frame_size=%d, yields=%d)",
                  fn_name, frame_size, yield_count);
    }
}

static void pm_compile_lambda(PyMirTranspiler* mt, PyLambdaCollected* lc) {
    PyLambdaNode* lam = lc->node;

    int mir_param_count = lc->param_count + (lc->is_closure ? 1 : 0);
    char* param_name_bufs[18];
    uint8_t param_kinds[18] = {};
    int offset = 0;

    if (lc->is_closure) {
        param_name_bufs[0] = (char*)alloca(128);
        snprintf(param_name_bufs[0], 128, "_py__env");
        offset = 1;
    }

    PyAstNode* p = lam->params;
    for (int i = 0; i < lc->param_count && i < 16; i++) {
        param_name_bufs[i + offset] = (char*)alloca(128);
        String* pn = NULL;
        if (p) {
            if (p->node_type == PY_AST_NODE_PARAMETER ||
                p->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                p->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)p)->name;
            }
            p = p->next;
        }
        if (pn) {
            snprintf(param_name_bufs[i + offset], 128, "_py_%.*s", (int)pn->len, pn->chars);
        } else {
            snprintf(param_name_bufs[i + offset], 128, "_py_p%d", i);
        }
    }
    param_name_bufs[mir_param_count] = (char*)alloca(128);
    snprintf(param_name_bufs[mir_param_count], 128, "_py__scalar_home");
    param_kinds[mir_param_count] = 1;
    mir_param_count++;

    MIR_func_t lambda_func = NULL;
    if (!pm_create_hosted_item_function_typed(mt->em.ctx, lc->name, mir_param_count,
            (const char* const*)param_name_bufs, param_kinds, &lc->func_item,
            &lambda_func)) {
        log_error("py-mir: host could not create lambda '%s'", lc->name);
        mt->has_compile_error = true;
        return;
    }

    MIR_item_t old_func_item = mt->em.func_item;
    MIR_func_t old_func = mt->em.func;
    MirFunctionArgumentState old_argument_registers = em_function_arguments_suspend(&mt->em);
    int old_scope_depth = mt->scope_depth;
    MIR_reg_t old_env_reg = mt->scope_env_reg;
    int old_env_slot_count = mt->scope_env_slot_count;

    pm_select_hosted_function(mt, lc->func_item, lambda_func);
    pm_begin_function_frame(mt, pm_load_side_stack_runtime(mt));
    pm_enable_scalar_return_home(mt, "_py__scalar_home");

    pm_push_scope(mt);

    for (int i = 0; i < lc->param_count && i < 16; i++) {
        MIR_reg_t preg = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func,
            param_name_bufs[i + offset]);
        pm_set_var(mt, param_name_bufs[i + offset], preg);
    }

    // if this is a closure, load captured variables from env
    if (lc->is_closure) {
        MIR_reg_t env_reg = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "_py__env");
        mt->scope_env_reg = env_reg;
        mt->scope_env_slot_count = lc->capture_count;
        for (int i = 0; i < lc->capture_count; i++) {
            MIR_reg_t var_reg = pm_new_reg(mt, "cvar", MIR_T_I64);
            MIR_reg_t addr = pm_new_reg(mt, "eaddr", MIR_T_I64);
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_ADD,
                MIR_new_reg_op(mt->em.ctx, addr),
                MIR_new_reg_op(mt->em.ctx, env_reg),
                MIR_new_int_op(mt->em.ctx, i * 8)));
            pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                MIR_new_reg_op(mt->em.ctx, var_reg),
                MIR_new_mem_op(mt->em.ctx, MIR_T_I64, 0, addr, 0, 1)));
            pm_set_var(mt, lc->captures[i], var_reg);
        }
    } else {
        mt->scope_env_reg = 0;
        mt->scope_env_slot_count = 0;
    }

    MIR_reg_t body_val = pm_transpile_expression(mt, lam->body);
    pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1, MIR_new_reg_op(mt->em.ctx, body_val)));

    pm_finish_function_frame(mt, lc->name);
    pm_finish_hosted_mir_function(mt->em.ctx);

    pm_pop_scope(mt);
    pm_restore_hosted_function(mt, old_func_item, old_func, old_argument_registers);
    mt->scope_depth = old_scope_depth;
    mt->scope_env_reg = old_env_reg;
    mt->scope_env_slot_count = old_env_slot_count;
}

static void pm_compile_function(PyMirTranspiler* mt, PyFuncCollected* fc) {
    // generator functions use a different compilation path
    if (fc->is_generator) {
        pm_compile_generator(mt, fc);
        return;
    }

    char fn_name[260];
    pm_make_mir_func_name(mt, fc, fn_name, sizeof(fn_name));

    // build parameter list; closures get _py__env as hidden first param
    // *args functions get _py__varargs (ptr) + _py__varargc (count) as extra params
    int mir_param_count = fc->param_count + (fc->is_closure ? 1 : 0) + (fc->has_star_args ? 2 : 0) + (fc->has_kwargs ? 1 : 0);
    char* param_name_bufs[21];
    uint8_t param_kinds[21] = {};
    int offset = 0;

    if (fc->is_closure) {
        param_name_bufs[0] = (char*)alloca(128);
        snprintf(param_name_bufs[0], 128, "_py__env");
        offset = 1;
    }

    PyAstNode* pnode = fc->node->params;
    for (int i = 0; i < fc->param_count && i < 16; i++) {
        param_name_bufs[i + offset] = (char*)alloca(128);
        String* pn = NULL;
        while (pnode) {
            if (pnode->node_type == PY_AST_NODE_LIST_SPLAT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DICT_SPLAT_PARAMETER) {
                pnode = pnode->next;
                continue;
            }
            if (pnode->node_type == PY_AST_NODE_PARAMETER ||
                pnode->node_type == PY_AST_NODE_DEFAULT_PARAMETER ||
                pnode->node_type == PY_AST_NODE_TYPED_PARAMETER) {
                pn = ((PyParamNode*)pnode)->name;
            }
            pnode = pnode->next;
            break;
        }
        if (pn) {
            snprintf(param_name_bufs[i + offset], 128, "_py_%.*s", (int)pn->len, pn->chars);
        } else {
            snprintf(param_name_bufs[i + offset], 128, "_py_p%d", i);
        }
    }

    // add *args extra params after regular params
    int varargs_param_offset = fc->param_count + offset;
    if (fc->has_star_args) {
        param_name_bufs[varargs_param_offset] = (char*)alloca(128);
        snprintf(param_name_bufs[varargs_param_offset], 128, "_py__varargs");
        param_name_bufs[varargs_param_offset + 1] = (char*)alloca(128);
        snprintf(param_name_bufs[varargs_param_offset + 1], 128, "_py__varargc");
    }
    // add **kwargs map parameter after *args (if any)
    int kwargs_param_offset = varargs_param_offset + (fc->has_star_args ? 2 : 0);
    if (fc->has_kwargs) {
        param_name_bufs[kwargs_param_offset] = (char*)alloca(128);
        snprintf(param_name_bufs[kwargs_param_offset], 128, "_py__kwargs_map");
    }
    param_name_bufs[mir_param_count] = (char*)alloca(128);
    snprintf(param_name_bufs[mir_param_count], 128, "_py__scalar_home");
    param_kinds[mir_param_count] = 1;
    mir_param_count++;

    MIR_func_t compiled_func = NULL;
    if (!pm_create_hosted_item_function_typed(mt->em.ctx, fn_name, mir_param_count,
            (const char* const*)param_name_bufs, param_kinds, &fc->func_item,
            &compiled_func)) {
        log_error("py-mir: host could not create function '%s'", fn_name);
        mt->has_compile_error = true;
        return;
    }

    // save parent state
    MIR_item_t old_func_item = mt->em.func_item;
    MIR_func_t old_func = mt->em.func;
    MirFunctionArgumentState old_argument_registers = em_function_arguments_suspend(&mt->em);
    int old_scope_depth = mt->scope_depth;
    int old_func_index = mt->current_func_index;
    MIR_reg_t old_env_reg = mt->scope_env_reg;
    int old_env_slot_count = mt->scope_env_slot_count;

    pm_select_hosted_function(mt, fc->func_item, compiled_func);
    pm_begin_function_frame(mt, pm_load_side_stack_runtime(mt));
    pm_enable_scalar_return_home(mt, "_py__scalar_home");

    // find this function's index in func_entries
    for (int i = 0; i < mt->func_count; i++) {
        if (&mt->func_entries[i] == fc) { mt->current_func_index = i; break; }
    }

    // push function scope
    pm_push_scope(mt);

    // register params as variables
    for (int i = 0; i < fc->param_count && i < 16; i++) {
        MIR_reg_t preg = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func,
            param_name_bufs[i + offset]);
        pm_set_var(mt, param_name_bufs[i + offset], preg);
    }

    // if function has *args, build a list from the varargs pointer + count
    if (fc->has_star_args) {
        MIR_reg_t va_ptr = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "_py__varargs");
        MIR_reg_t va_cnt = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "_py__varargc");
        // call py_build_list_from_args(ptr, count) → list Item
        MIR_reg_t star_list = pm_call_2(mt, "py_build_list_from_args", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, va_ptr),
            MIR_T_I64, MIR_new_reg_op(mt->em.ctx, va_cnt));
        MIR_reg_t star_reg = pm_new_reg(mt, fc->star_args_name, MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, star_reg),
            MIR_new_reg_op(mt->em.ctx, star_list)));
        pm_set_var(mt, fc->star_args_name, star_reg);
    }

    // if function has **kwargs, bind the kwargs map param to the user's variable
    if (fc->has_kwargs) {
        MIR_reg_t kw_reg = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "_py__kwargs_map");
        MIR_reg_t kw_var = pm_new_reg(mt, fc->kwargs_name, MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, kw_var),
            MIR_new_reg_op(mt->em.ctx, kw_reg)));
        pm_set_var(mt, fc->kwargs_name, kw_var);
    }

    // if this is a closure, load captured variables from env
    if (fc->is_closure) {
        MIR_reg_t env_reg = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "_py__env");
        mt->scope_env_reg = env_reg;
        mt->scope_env_slot_count = fc->capture_count;
        for (int i = 0; i < fc->capture_count; i++) {
            if (fc->capture_is_nonlocal[i]) {
                // nonlocal: reads and writes go through env slot directly
                pm_set_env_var(mt, fc->captures[i], env_reg, i);
            } else {
                // read-only: load once into a local register
                MIR_reg_t var_reg = pm_new_reg(mt, "cvar", MIR_T_I64);
                MIR_reg_t addr = pm_new_reg(mt, "eaddr", MIR_T_I64);
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_ADD,
                    MIR_new_reg_op(mt->em.ctx, addr),
                    MIR_new_reg_op(mt->em.ctx, env_reg),
                    MIR_new_int_op(mt->em.ctx, i * 8)));
                pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                    MIR_new_reg_op(mt->em.ctx, var_reg),
                    MIR_new_mem_op(mt->em.ctx, MIR_T_I64, 0, addr, 0, 1)));
                pm_set_var(mt, fc->captures[i], var_reg);
            }
        }
    } else {
        mt->scope_env_reg = 0;
        mt->scope_env_slot_count = 0;
    }

    // if this function has nonlocal children, allocate a shared env and route
    // shared vars through it — both this function and its children access the same env
    if (fc->has_nonlocal_children) {
        MIR_reg_t shared_env = pm_call_1(mt, "py_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->em.ctx, fc->shared_env_count));
        mt->scope_env_reg = shared_env;
        mt->scope_env_slot_count = fc->shared_env_count;
        for (int si = 0; si < fc->shared_env_count; si++) {
            // initialize the env slot from current param/local value if it exists
            PyMirVarEntry* existing = pm_find_var(mt, fc->shared_env_names[si]);
            if (existing && !existing->from_env) {
                pm_store_env_slot(mt, shared_env, si, existing->reg);
            }
            // re-register this var as env-backed
            pm_set_env_var(mt, fc->shared_env_names[si], shared_env, si);
        }
    }

    // emit default-value initialization for parameters with defaults:
    // if param == PY_ITEM_NULL_VAL, assign the default expression
    {
        PyAstNode* dp = fc->node->params;
        int pi = 0;
        while (dp && pi < fc->param_count && pi < 16) {
            if (dp->node_type == PY_AST_NODE_DEFAULT_PARAMETER) {
                PyParamNode* pp = (PyParamNode*)dp;
                if (pp->default_value) {
                    MIR_reg_t preg = pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func,
                        param_name_bufs[pi + offset]);
                    MIR_reg_t is_null = pm_new_reg(mt, "dnull", MIR_T_I64);
                    MIR_label_t l_no_default = pm_new_label(mt);
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_NE,
                        MIR_new_reg_op(mt->em.ctx, is_null),
                        MIR_new_reg_op(mt->em.ctx, preg),
                        MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL)));
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BT,
                        MIR_new_label_op(mt->em.ctx, l_no_default),
                        MIR_new_reg_op(mt->em.ctx, is_null)));
                    // param was null → evaluate default expression and assign
                    MIR_reg_t def_val = pm_transpile_expression(mt, pp->default_value);
                    pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
                        MIR_new_reg_op(mt->em.ctx, preg),
                        MIR_new_reg_op(mt->em.ctx, def_val)));
                    pm_emit_label(mt, l_no_default);
                }
            }
            dp = dp->next;
            pi++;
        }
    }

    // ===== TCO Setup =====
    bool use_tco = pm_should_use_tco(fc);
    PyFuncCollected* saved_tco_func = mt->tco_func;
    MIR_label_t saved_tco_label = mt->tco_label;
    MIR_reg_t saved_tco_count_reg = mt->tco_count_reg;
    bool saved_tail_position = mt->in_tail_position;
    bool saved_block_returned = mt->block_returned;

    if (use_tco) {
        log_debug("py-mir: TCO enabled for '%s'", fc->name);
        mt->tco_func = fc;

        // create iteration counter register, init to 0
        mt->tco_count_reg = pm_new_reg(mt, "tco_count", MIR_T_I64);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->em.ctx, 0)));

        // create the TCO loop label
        mt->tco_label = MIR_new_label(mt->em.ctx);
        pm_emit(mt, mt->tco_label);

        // increment counter: tco_count = tco_count + 1
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_ADD,
            MIR_new_reg_op(mt->em.ctx, mt->tco_count_reg),
            MIR_new_reg_op(mt->em.ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->em.ctx, 1)));

        // guard: if tco_count > LAMBDA_TCO_MAX_ITERATIONS, call overflow error
        MIR_label_t ok_label = pm_new_label(mt);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_BLE, MIR_new_label_op(mt->em.ctx, ok_label),
            MIR_new_reg_op(mt->em.ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->em.ctx, LAMBDA_TCO_MAX_ITERATIONS)));

        // overflow path: call lambda_stack_overflow_error(func_name)
        // lambda_stack_overflow_error takes a const char* — pass a raw C string pointer
        String* fn_str = name_pool_create_len(mt->tp->name_pool, fc->name, strlen(fc->name));
        MIR_reg_t fn_name_ptr = pm_new_reg(mt, "tco_fn", MIR_T_P);
        pm_emit(mt, MIR_new_insn(mt->em.ctx, MIR_MOV,
            MIR_new_reg_op(mt->em.ctx, fn_name_ptr),
            MIR_new_int_op(mt->em.ctx, (int64_t)(uintptr_t)fn_str->chars)));
        // Keep the pointer ABI identical to the shared side-stack error call;
        // an I64 variant created a second same-name MIR prototype at finalize.
        pm_call_void_1(mt, "lambda_stack_overflow_error",
            MIR_T_P, MIR_new_reg_op(mt->em.ctx, fn_name_ptr));

        // emit a ret after overflow call (unreachable but needed for MIR validation)
        pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1,
            MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL)));

        pm_emit_label(mt, ok_label);
        mt->in_tail_position = false;
        mt->block_returned = false;
    }

    // compile body
    pm_transpile_block(mt, fc->node->body);

    // ensure function ends with a return
    pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1,
        MIR_new_int_op(mt->em.ctx, (int64_t)PY_ITEM_NULL_VAL)));

    pm_finish_function_frame(mt, fn_name);
    pm_finish_hosted_mir_function(mt->em.ctx);

    // restore TCO state
    mt->tco_func = saved_tco_func;
    mt->tco_label = saved_tco_label;
    mt->tco_count_reg = saved_tco_count_reg;
    mt->in_tail_position = saved_tail_position;
    mt->block_returned = saved_block_returned;

    // restore parent state
    pm_pop_scope(mt);
    pm_restore_hosted_function(mt, old_func_item, old_func, old_argument_registers);
    mt->scope_depth = old_scope_depth;
    mt->current_func_index = old_func_index;
    mt->scope_env_reg = old_env_reg;
    mt->scope_env_slot_count = old_env_slot_count;

    // register in local_funcs (methods are accessed via py_getattr, not by bare name)
    if (!fc->is_method) {
        PyLocalFuncEntry lfe;
        memset(&lfe, 0, sizeof(lfe));
        snprintf(lfe.name, sizeof(lfe.name), "_py_%s", fc->name);
        lfe.func_item = fc->func_item;
        hashmap_set(mt->local_funcs, &lfe);
    }
}

// ============================================================================
// Module-level variable scan
// ============================================================================

static void pm_scan_module_vars(PyMirTranspiler* mt, PyAstNode* root) {
    // register class names and top-level assignment targets as module vars
    // so they are accessible after py_main() runs (for imports and for super())
    if (!root || root->node_type != PY_AST_NODE_MODULE) return;
    PyModuleNode* program = (PyModuleNode*)root;
    PyAstNode* s = program->body;
    while (s) {
        if (s->node_type == PY_AST_NODE_CLASS_DEF) {
            PyClassDefNode* cls = (PyClassDefNode*)s;
            char cls_var[132];
            snprintf(cls_var, sizeof(cls_var), "_py_%.*s", (int)cls->name->len, cls->name->chars);
            pm_register_global_var(mt, cls_var, "class var");
        } else if (s->node_type == PY_AST_NODE_ASSIGNMENT) {
            // register top-level simple name assignments as module vars
            // so they can be exported to importing scripts
            PyAssignmentNode* asgn = (PyAssignmentNode*)s;
            PyAstNode* target = asgn->left;
            while (target) {
                if (target->node_type == PY_AST_NODE_IDENTIFIER) {
                    PyIdentifierNode* id = (PyIdentifierNode*)target;
                    char var_name[132];
                    snprintf(var_name, sizeof(var_name), "_py_%.*s",
                        (int)id->name->len, id->name->chars);
                    pm_register_global_var(mt, var_name, "module-level var");
                }
                target = target->next;
            }
        } else if (s->node_type == PY_AST_NODE_IMPORT) {
            // register top-level imports as module vars so they are accessible
            // from nested functions (generators, coroutines, closures)
            PyImportNode* imp = (PyImportNode*)s;
            if (imp->module_name) {
                const char* local_name = imp->module_name->chars;
                int local_len = (int)imp->module_name->len;
                if (imp->alias) {
                    local_name = imp->alias->chars;
                    local_len = (int)imp->alias->len;
                } else {
                    const char* dot = (const char*)memchr(local_name, '.', local_len);
                    if (dot) local_len = (int)(dot - local_name);
                }
                char var_name[132];
                snprintf(var_name, sizeof(var_name), "_py_%.*s", local_len, local_name);
                pm_register_global_var(mt, var_name, "import var");
            }
        } else if (s->node_type == PY_AST_NODE_IMPORT_FROM) {
            // register from...import names as module vars for export
            PyImportNode* imp = (PyImportNode*)s;
            PyAstNode* name_node = imp->names;
            while (name_node) {
                if (name_node->node_type == PY_AST_NODE_IMPORT) {
                    PyImportNode* name_imp = (PyImportNode*)name_node;
                    if (name_imp->module_name) {
                        const char* local_name = name_imp->module_name->chars;
                        int local_len = (int)name_imp->module_name->len;
                        if (name_imp->alias) {
                            local_name = name_imp->alias->chars;
                            local_len = (int)name_imp->alias->len;
                        }
                        // skip wildcard import
                        if (!(local_len == 1 && local_name[0] == '*')) {
                            char var_name[132];
                            snprintf(var_name, sizeof(var_name), "_py_%.*s", local_len, local_name);
                            pm_register_global_var(mt, var_name, "from-import var");
                        }
                    }
                }
                name_node = name_node->next;
            }
        }
        s = s->next;
    }
}

// ============================================================================
// Main transpilation pipeline
// ============================================================================

static void pm_transpile_ast(PyMirTranspiler* mt, PyAstNode* root) {
    if (!root || root->node_type != PY_AST_NODE_MODULE) {
        log_error("py-mir: expected module node");
        return;
    }

    PyModuleNode* program = (PyModuleNode*)root;

    // Phase 1: collect functions
    pm_collect_functions(mt, root);
    log_debug("py-mir: collected %d functions", mt->func_count);
    if (mt->has_compile_error) return;

    // Phase 1b: collect lambda expressions
    pm_collect_lambdas_r(mt, root, -1);
    log_debug("py-mir: collected %d lambdas", mt->lambda_count);
    if (mt->has_compile_error) return;

    // Phase 1c: analyze captures for closures
    pm_analyze_captures(mt);
    pm_analyze_lambda_captures(mt);
    if (mt->has_compile_error) return;

    // Phase 1d: analyze nonlocal declarations → shared env
    pm_analyze_nonlocal(mt);
    if (mt->has_compile_error) return;

    // Phase 1e: analyze global declarations → module var indices
    pm_analyze_globals(mt);
    if (mt->has_compile_error) return;

    // Phase 2: scan module-level variables
    pm_scan_module_vars(mt, root);
    if (mt->has_compile_error) return;

    // Phase 3a: compile all lambdas first (functions reference lambda func_items)
    for (int i = 0; i < mt->lambda_count; i++) {
        pm_compile_lambda(mt, &mt->lambda_entries[i]);
    }

    // Phase 3a2: forward-declare all module-level functions so that generators/
    // coroutines compiled in reverse order can reference not-yet-compiled functions.
    for (int i = 0; i < mt->func_count; i++) {
        PyFuncCollected* fc = &mt->func_entries[i];
        if (fc->is_method) continue;  // methods are accessed via py_getattr
        char fn_name[260];
        pm_make_mir_func_name(mt, fc, fn_name, sizeof(fn_name));
        MIR_item_t fwd = NULL;
        if (!pm_create_hosted_function_forward(mt->em.ctx, fn_name, &fwd)) {
            log_error("py-mir: host could not declare forward function '%s'", fn_name);
            mt->has_compile_error = true;
            return;
        }
        fc->func_item = fwd;  // direct-call path uses this; overwritten by real func later
        PyLocalFuncEntry lfe;
        memset(&lfe, 0, sizeof(lfe));
        snprintf(lfe.name, sizeof(lfe.name), "_py_%s", fc->name);
        lfe.func_item = fwd;
        hashmap_set(mt->local_funcs, &lfe);
    }

    // Phase 3b: compile all functions (reverse order so nested functions compile first)
    for (int i = mt->func_count - 1; i >= 0; i--) {
        pm_compile_function(mt, &mt->func_entries[i]);
    }

    // Phase 4: create py_main function
    const char* main_params[] = {"ctx", "_py__scalar_home"};
    const uint8_t main_param_kinds[] = {0, 1};
    MIR_item_t main_function_item = NULL;
    MIR_func_t main_function = NULL;
    if (!pm_create_hosted_item_function_typed(mt->em.ctx, "py_main", 2,
            main_params, main_param_kinds,
            &main_function_item, &main_function)) {
        log_error("py-mir: host could not create py_main");
        mt->has_compile_error = true;
        return;
    }
    pm_select_hosted_function(mt, main_function_item, main_function);
    mt->in_main = true;
    pm_begin_function_frame(mt, pm_lookup_hosted_function_register(mt->em.ctx, mt->em.func, "ctx"));
    mt->em.frame.incoming_scalar_home = pm_lookup_hosted_function_register(
        mt->em.ctx, mt->em.func, "_py__scalar_home");
    mt->em.frame.plan.scalar_home_lane_mask = FN_RETURN_HOME_NORMAL;
    mt->em.frame.plan.accepts_caller_scalar_home = true;

    pm_push_scope(mt);

    // transpile top-level statements
    MIR_reg_t last_result = pm_emit_null(mt);
    PyAstNode* s = program->body;
    while (s) {
        if (s->node_type == PY_AST_NODE_FUNCTION_DEF) {
            PyFunctionDefNode* fdef_chk = (PyFunctionDefNode*)s;
            if (!fdef_chk->decorators) {
                // no decorators: skip — already compiled and accessible via local_funcs
                s = s->next;
                continue;
            }
            // has decorators: fall through to pm_transpile_statement to apply them
        }
        if (s->node_type == PY_AST_NODE_CLASS_DEF) {
            // class objects are created at runtime via pm_transpile_statement
            // (NOT skipped — we need to run the class creation at module init time)
        }
        if (s->node_type == PY_AST_NODE_EXPRESSION_STATEMENT) {
            PyExpressionStatementNode* es = (PyExpressionStatementNode*)s;
            if (es->expression) {
                last_result = pm_transpile_expression(mt, es->expression);
            }
        } else {
            pm_transpile_statement(mt, s);
        }
        s = s->next;
    }

    // return last result
    pm_emit(mt, MIR_new_ret_insn(mt->em.ctx, 1,
        MIR_new_reg_op(mt->em.ctx, last_result)));

    pm_finish_function_frame(mt, "py_main");
    pm_finish_hosted_mir_function(mt->em.ctx);
    pm_pop_scope(mt);
    mt->in_main = false;
}

// ============================================================================
// Entry point
// ============================================================================

typedef Item (*PmMainFunc)(Context*);

Item transpile_py_to_mir(void* host_execution, const char* py_source, const char* filename) {
    log_debug("py-mir: starting direct MIR transpilation for '%s'", filename ? filename : "<string>");
    // Python MIR functions publish canonical frame roots, so forcing the old
    // compatibility tier here would hide precise-rooting regressions.

    // create Python transpiler (parsing + AST building)
    PyTranspiler* tp = py_transpiler_create(host_execution);
    if (!tp) {
        log_error("py-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    // parse source
    if (!py_transpiler_parse(tp, py_source, strlen(py_source))) {
        log_error("py-mir: parse failed");
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // build AST
    PyAstNode* ast = build_py_ast(tp, root);
    if (!ast) {
        log_error("py-mir: AST build failed");
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // The host owns TLS activation, heap/input creation, and recovery state.
    // Python receives the input only as an opaque adapter token.
    void* runtime_input = NULL;
    if (!pm_activate_hosted_guest_execution(host_execution, &runtime_input)) {
        log_error("py-mir: host could not activate Python execution");
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }
    py_runtime_set_data_session(runtime_input);

    // init MIR context
    MIR_context_t ctx = pm_create_hosted_mir_context();
    if (!ctx) {
        log_error("py-mir: MIR context init failed");
        py_transpiler_destroy(tp);
        pm_finish_hosted_guest_execution(host_execution);
        return (Item){.item = ITEM_ERROR};
    }

    // allocate transpiler
    PyMirTranspiler* mt = (PyMirTranspiler*)mem_alloc(sizeof(PyMirTranspiler), MEM_CAT_PY_RUNTIME);
    if (!mt) {
        log_error("py-mir: failed to allocate PyMirTranspiler");
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        pm_finish_hosted_guest_execution(host_execution);
        return (Item){.item = ITEM_ERROR};
    }
    memset(mt, 0, sizeof(PyMirTranspiler));
    mt->tp = tp;
    mt->em.ctx = ctx;
    mt->em.call_owner = mt;
    mt->em.note_mir_call = pm_note_mir_call;
    mt->em.before_may_gc_call = pm_before_may_gc_call;
    mt->em.root_call_value = pm_root_call_value;
    mt->em.after_call_result = pm_after_call_result;
    mt->em.lookup_import_metadata = pm_lookup_hosted_import_metadata;
    mt->filename = filename;
    mt->host_execution = host_execution;
    mt->frame_runtime_slot = pm_hosted_frame_runtime_slot(host_execution);
    if (!mt->frame_runtime_slot) {
        log_error("py-mir: host did not provide an active frame runtime slot");
        mem_free(mt);
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        pm_finish_hosted_guest_execution(host_execution);
        return (Item){.item = ITEM_ERROR};
    }

    // store entry-point script directory for sub-module import resolution
    if (filename) {
        const char* last_slash = strrchr(filename, '/');
        if (last_slash) {
            int dir_len = (int)(last_slash - filename);
            if (dir_len < (int)sizeof(py_entry_script_dir) - 1) {
                memcpy(py_entry_script_dir, filename, dir_len);
                py_entry_script_dir[dir_len] = '\0';
            }
        } else {
            py_entry_script_dir[0] = '.';
            py_entry_script_dir[1] = '\0';
        }
    }

    mt->em.import_cache = em_import_cache_new(64);
    mt->local_funcs  = py_local_func_new(32);
    mt->var_scopes[0] = em_var_scope_new(16);
    mt->scope_depth = 0;
    mt->current_func_index = -1;

    // create module
    mt->module = pm_create_hosted_mir_module(ctx, "py_script");
    if (!mt->module) {
        log_error("py-mir: host could not create Python MIR module");
        hashmap_free(mt->em.import_cache);
        hashmap_free(mt->local_funcs);
        for (int i = 0; i <= mt->scope_depth; i++) {
            if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
        }
        mem_free(mt);
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        pm_finish_hosted_guest_execution(host_execution);
        return (Item){.item = ITEM_ERROR};
    }

    // Keep shared-emitter telemetry scoped to this lowering activation.
    PyMirTranspiler* saved_telemetry_owner = py_mir_telemetry_owner;
    py_mir_telemetry_owner = mt;
    pm_transpile_ast(mt, ast);
    py_mir_telemetry_owner = saved_telemetry_owner;

    if (mt->has_compile_error) {
        hashmap_free(mt->em.import_cache);
        hashmap_free(mt->local_funcs);
        for (int i = 0; i <= mt->scope_depth; i++) {
            if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
        }
        mem_free(mt);
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        pm_finish_hosted_guest_execution(host_execution);
        return (Item){.item = ITEM_ERROR};
    }

    // Finalization stays host-owned so guest code cannot depend on the MIR
    // module lifecycle implementation or its load ordering.
    if (!pm_finalize_and_load_hosted_mir_module(mt->em.ctx, mt->module)) {
        log_error("py-mir: host could not finalize Python MIR module");
        hashmap_free(mt->em.import_cache);
        hashmap_free(mt->local_funcs);
        for (int i = 0; i <= mt->scope_depth; i++) {
            if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
        }
        mem_free(mt);
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        pm_finish_hosted_guest_execution(host_execution);
        return (Item){.item = ITEM_ERROR};
    }

#ifndef NDEBUG
    // developer diagnostic; --no-log suppresses it like every other optional
    // MIR artifact. The private-path/release contract is Lambda/JS/TS only.
    if (mir_dump_instrumentation_enabled()) {
        mir_dump_write_context(ctx, "temp/py_mir_dump.txt", false);
    }
#endif

    // link and generate
    if (!pm_link_hosted_module(host_execution, ctx)) {
        log_error("py-mir: host could not link Python MIR module");
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // find py_main
    PmMainFunc py_main = (PmMainFunc)pm_find_hosted_mir_function(ctx, "py_main");

    if (!py_main) {
        log_error("py-mir: failed to find py_main");
        hashmap_free(mt->em.import_cache);
        hashmap_free(mt->local_funcs);
        for (int i = 0; i <= mt->scope_depth; i++) {
            if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
        }
        mem_free(mt);
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        pm_finish_hosted_guest_execution(host_execution);
        return (Item){.item = ITEM_ERROR};
    }

    // execute
    log_notice("py-mir: executing JIT compiled Python code");
    py_reset_module_vars();
    Item result = ItemError;
    if (!pm_run_hosted_guest_main(host_execution, (void*)py_main, &result)) {
        log_error("py-mir: host execution failed for py_main");
    }

    // cleanup
    hashmap_free(mt->em.import_cache);
    hashmap_free(mt->local_funcs);
    for (int i = 0; i <= mt->scope_depth; i++) {
        if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
    }
    mem_free(mt);

    pm_destroy_hosted_mir_context(ctx);
    py_transpiler_destroy(tp);

    pm_finish_hosted_guest_execution(host_execution);

    return result;
}

// ============================================================================
// Deferred MIR context cleanup for Python modules
// ============================================================================

#define PM_MAX_MODULE_CONTEXTS 64
static MIR_context_t pm_module_mir_contexts[PM_MAX_MODULE_CONTEXTS];
static int pm_module_mir_context_count = 0;
static void* pm_module_owned_execution = NULL;

static void pm_defer_mir_cleanup(MIR_context_t ctx) {
    if (pm_module_mir_context_count < PM_MAX_MODULE_CONTEXTS) {
        pm_module_mir_contexts[pm_module_mir_context_count++] = ctx;
    } else {
        log_error("py-mir: exceeded max deferred MIR contexts (%d)", PM_MAX_MODULE_CONTEXTS);
        pm_destroy_hosted_mir_context(ctx);
    }
}

void py_module_heap_cleanup(void* host_heap) {
    (void)host_heap;
    // Imported Python code keeps its MIR contexts alive for exported function
    // pointers. Release them at the host heap boundary before either heap can
    // disappear; leaving them process-global leaked every cross-language run.
    for (int i = 0; i < pm_module_mir_context_count; i++) {
        pm_destroy_hosted_mir_context(pm_module_mir_contexts[i]);
        pm_module_mir_contexts[i] = NULL;
    }
    pm_module_mir_context_count = 0;

    if (pm_module_owned_execution && py_hosted_execution_api &&
        py_hosted_execution_api->execution_destroy) {
        // The host retained this standalone import activation so its TLS and
        // heap ownership are restored at the runtime's single cleanup point.
        py_hosted_execution_api->execution_destroy(pm_module_owned_execution);
    }
    pm_module_owned_execution = NULL;
    py_runtime_set_data_session(NULL);
}

// ============================================================================
// Load Python module for cross-language import
// ============================================================================

static Item pm_module_namespace_create(void) {
    return py_dict_new();
}

static Item pm_module_namespace_get(Item namespace_obj, const char* name) {
    Item key = {.item = s2it(heap_create_name(name))};
    return py_dict_get(namespace_obj, key);
}

static int pm_module_namespace_function_arity(Item function_obj) {
    if (get_type_id(function_obj) != LMD_TYPE_FUNC || !function_obj.function) return 0;
    return function_obj.function->arity;
}

static void* pm_module_namespace_function_ptr(Item function_obj) {
    if (get_type_id(function_obj) != LMD_TYPE_FUNC || !function_obj.function) return NULL;
    return (void*)function_obj.function->ptr;
}

static const JubeModuleNamespaceOps pm_module_namespace_ops = {
    pm_module_namespace_create,
    pm_module_namespace_get,
    pm_module_namespace_function_arity,
    pm_module_namespace_function_ptr,
};

static void pm_module_source_release(JubeHostedSource* source) {
    if (!source || !py_hosted_source_api || !py_hosted_source_api->source_release) return;
    py_hosted_source_api->source_release(source);
}

class PmDataSessionScope {
    void* previous_session_;
    bool restore_;

public:
    PmDataSessionScope() : previous_session_(py_runtime_data_session()), restore_(true) {}

    ~PmDataSessionScope() {
        if (restore_) py_runtime_restore_data_session(previous_session_);
    }

    void retain_current_session() { restore_ = false; }
};

static void pm_module_lowering_cleanup(PyMirTranspiler* mt, MIR_context_t mir_context,
                                       PyTranspiler* transpiler, JubeHostedSource* source,
                                       bool defer_mir_context) {
    if (mt) {
        hashmap_free(mt->em.import_cache);
        hashmap_free(mt->local_funcs);
        for (int i = 0; i <= mt->scope_depth; i++) {
            if (mt->var_scopes[i]) hashmap_free(mt->var_scopes[i]);
        }
        mem_free(mt);
    }
    if (mir_context) {
        if (defer_mir_context) pm_defer_mir_cleanup(mir_context);
        else pm_destroy_hosted_mir_context(mir_context);
    }
    py_transpiler_destroy(transpiler);
    pm_module_source_release(source);
}

Item load_py_module(void* host_execution, const char* py_path) {
    // The language registry provides a short-lived opaque import execution;
    // Python never unwraps its Runtime or active-context ownership.
    log_info("py-mir: loading Python module '%s' for cross-language import", py_path);

    // check if already loaded in module registry
    Item cached_namespace = ItemNull;
    int cached_state = py_hosted_module_graph_api &&
        py_hosted_module_graph_api->module_state
        ? py_hosted_module_graph_api->module_state(host_execution, py_path, &cached_namespace) : -1;
    if (cached_state == 2) {
        log_debug("py-mir: module '%s' already loaded, returning cached namespace", py_path);
        return cached_namespace;
    }
    // check for circular import (module is currently being loaded)
    if (cached_state == 1) {
        log_info("py-mir: circular import detected for '%s', returning partial namespace", py_path);
        return cached_namespace;
    }

    JubeHostedSource source_record = {JUBE_HOSTED_SOURCE_V1_SIZE};
    if (!py_hosted_source_api ||
        py_hosted_source_api->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        py_hosted_source_api->struct_size < JUBE_SOURCE_API_V1_SIZE ||
        !py_hosted_source_api->source_read || !py_hosted_source_api->source_release ||
        py_hosted_source_api->source_read(py_path, &source_record) != 0) {
        log_debug("py-mir: cannot read Python file '%s'", py_path);
        return ItemNull;
    }
    const char* source = source_record.bytes;

    void* import_input = NULL;
    bool retain_import_execution = false;
    if (!pm_activate_hosted_import_execution(host_execution, &import_input,
                                             &retain_import_execution)) {
        log_error("py-mir: host could not activate imported Python module '%s'", py_path);
        pm_module_source_release(&source_record);
        return ItemNull;
    }
    PmDataSessionScope data_session_scope;
    py_runtime_set_data_session(import_input);

    // create Python transpiler (parsing + AST building)
    PyTranspiler* tp = py_transpiler_create(host_execution);
    if (!tp) {
        log_error("py-mir: module: failed to create transpiler for '%s'", py_path);
        pm_module_source_release(&source_record);
        return ItemNull;
    }

    if (!py_transpiler_parse(tp, source, strlen(source))) {
        log_error("py-mir: module: parse failed for '%s'", py_path);
        py_transpiler_destroy(tp);
        pm_module_source_release(&source_record);
        return ItemNull;
    }

    TSNode root = ts_tree_root_node(tp->tree);
    PyAstNode* ast = build_py_ast(tp, root);
    if (!ast) {
        log_error("py-mir: module: AST build failed for '%s'", py_path);
        py_transpiler_destroy(tp);
        pm_module_source_release(&source_record);
        return ItemNull;
    }

    MIR_context_t ctx = pm_create_hosted_mir_context();
    if (!ctx) {
        log_error("py-mir: module: MIR context init failed for '%s'", py_path);
        py_transpiler_destroy(tp);
        pm_module_source_release(&source_record);
        return ItemNull;
    }

    PyMirTranspiler* mt = (PyMirTranspiler*)mem_alloc(sizeof(PyMirTranspiler), MEM_CAT_PY_RUNTIME);
    if (!mt) {
        log_error("py-mir: module: failed to allocate transpiler for '%s'", py_path);
        pm_destroy_hosted_mir_context(ctx);
        py_transpiler_destroy(tp);
        pm_module_source_release(&source_record);
        return ItemNull;
    }
    memset(mt, 0, sizeof(PyMirTranspiler));
    mt->tp = tp;
    mt->em.ctx = ctx;
    mt->em.call_owner = mt;
    mt->em.note_mir_call = pm_note_mir_call;
    mt->em.before_may_gc_call = pm_before_may_gc_call;
    mt->em.root_call_value = pm_root_call_value;
    mt->em.after_call_result = pm_after_call_result;
    mt->em.lookup_import_metadata = pm_lookup_hosted_import_metadata;
    mt->filename = py_path;
    mt->host_execution = host_execution;
    mt->frame_runtime_slot = pm_hosted_frame_runtime_slot(host_execution);
    if (!mt->frame_runtime_slot) {
        log_error("py-mir: module: host did not provide an active frame runtime slot for '%s'", py_path);
        pm_module_lowering_cleanup(mt, ctx, tp, &source_record, false);
        return ItemNull;
    }
    mt->em.import_cache = em_import_cache_new(64);
    mt->local_funcs  = py_local_func_new(32);
    mt->var_scopes[0] = em_var_scope_new(16);
    mt->scope_depth = 0;
    mt->current_func_index = -1;

    mt->module = pm_create_hosted_mir_module(ctx, "py_module");
    if (!mt->module) {
        log_error("py-mir: module: host could not create MIR module for '%s'", py_path);
        pm_module_lowering_cleanup(mt, ctx, tp, &source_record, false);
        return ItemNull;
    }

    // transpile AST to MIR (compiles all functions + py_main)
    PyMirTranspiler* saved_telemetry_owner = py_mir_telemetry_owner;
    py_mir_telemetry_owner = mt;
    pm_transpile_ast(mt, ast);
    py_mir_telemetry_owner = saved_telemetry_owner;

    if (!pm_finalize_and_load_hosted_mir_module(mt->em.ctx, mt->module)) {
        log_error("py-mir: module: host could not finalize '%s'", py_path);
        pm_module_lowering_cleanup(mt, ctx, tp, &source_record, false);
        return ItemNull;
    }
    if (!pm_link_hosted_module(host_execution, ctx)) {
        log_error("py-mir: module: host could not link Python MIR module '%s'", py_path);
        pm_module_lowering_cleanup(mt, ctx, tp, &source_record, false);
        return ItemNull;
    }

    // mark module as loading before execution (circular import detection)
    if (!py_hosted_module_graph_api || !py_hosted_module_graph_api->module_begin_loading ||
        py_hosted_module_graph_api->module_begin_loading(host_execution, py_path, "python",
                                                         &pm_module_namespace_ops) != 0) {
        log_error("py-mir: module graph could not mark '%s' as loading", py_path);
        pm_module_lowering_cleanup(mt, ctx, tp, &source_record, false);
        return ItemNull;
    }

    // execute py_main to run module-level code (assignments, etc.)
    PmMainFunc py_main = (PmMainFunc)pm_find_hosted_mir_function(ctx, "py_main");
    if (py_main) {
        py_reset_module_vars();
        Item ignored_result = ItemError;
        if (!pm_run_hosted_guest_main(host_execution, (void*)py_main, &ignored_result)) {
            log_error("py-mir: host execution failed for imported module '%s'", py_path);
            pm_module_lowering_cleanup(mt, ctx, tp, &source_record, false);
            return ItemNull;
        }
    }

    // build namespace from top-level compiled functions
    Item ns = py_dict_new();
    for (int i = 0; i < mt->func_count; i++) {
        PyFuncCollected* fc = &mt->func_entries[i];
        if (fc->parent_index != -1) continue;  // skip nested functions

        // get the JIT-compiled function pointer by name
        char mir_name[140];
        snprintf(mir_name, sizeof(mir_name), "_%s", fc->name);
        void* func_ptr = pm_find_hosted_mir_function(ctx, mir_name);

        if (func_ptr) {
            // Use to_fn_n to create a Lambda Function* compatible with Python
            // calling convention; a JavaScript function has a different layout.
            Item val = {.function = to_fn_n((fn_ptr)func_ptr, fc->param_count)};
            Item key = {.item = s2it(heap_create_name(fc->name))};
            py_dict_set(ns, key, val);
            log_debug("py-mir: module export fn '%s' arity=%d", fc->name, fc->param_count);
        }
    }

    // also export module-level variables (constants, class objects, etc.)
    for (int i = 0; i < mt->global_var_count; i++) {
        const char* var_name = mt->global_var_names[i];
        int var_idx = mt->global_var_indices[i];
        Item value = py_get_module_var(var_idx);
        log_debug("py-mir: module var[%d] '%s' (slot %d) = 0x%llx type=%d",
            i, var_name, var_idx, (unsigned long long)value.item, (int)get_type_id(value));
        if (value.item == ItemNull.item) continue;
        // strip _py_ prefix for the export key
        const char* export_name = (strncmp(var_name, "_py_", 4) == 0) ? var_name + 4 : var_name;
        Item key = {.item = s2it(heap_create_name(export_name))};
        // functions exported above take priority over module vars
        Item existing = py_dict_get(ns, key);
        bool not_found = (existing.item == ItemNull.item || get_type_id(existing) == LMD_TYPE_UNDEFINED);
        if (not_found) {
            py_dict_set(ns, key, value);
            log_debug("py-mir: module export var '%s' type=%d", export_name, (int)get_type_id(value));
        }
    }

    // Publish through the host-owned graph so Python retains only its export
    // membrane and never accesses registry records or their state layout.
    if (!py_hosted_module_graph_api || !py_hosted_module_graph_api->module_publish ||
        py_hosted_module_graph_api->module_publish(host_execution, py_path, "python", ns, ctx,
                                                   &pm_module_namespace_ops) != 0) {
        log_error("py-mir: module graph could not publish '%s'", py_path);
        pm_module_lowering_cleanup(mt, ctx, tp, &source_record, false);
        return ItemNull;
    }

    // cleanup transpiler state but DEFER MIR context cleanup
    pm_module_lowering_cleanup(mt, ctx, tp, &source_record, true);

    if (retain_import_execution) {
        if (pm_module_owned_execution) {
            log_error("py-mir: multiple standalone Python import activations are unsupported");
            return ItemNull;
        }
        pm_module_owned_execution = host_execution;
        data_session_scope.retain_current_session();
    }

    log_info("py-mir: module '%s' loaded successfully", py_path);
    return ns;
}
