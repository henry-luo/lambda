#include "transpiler.hpp"
#include "re2_wrapper.hpp"
#include "mark_builder.hpp"
#include "safety_analyzer.hpp"
#include "module_registry.h"
#include "template_registry.h"
#include "mir_emitter_shared.hpp"
#include "js/js_runtime.h"
#include "../lib/log.h"
#include "../lib/lambda_alloca.h"
#include "../lib/memtrack.h"
#include "../lib/mem_factory.h"
#include "../lib/url.h"
#include "../lib/hashmap.h"
#include "../lib/hashmap_helpers.h"
#include "../lib/gc/gc_heap.h"
#include "validator/validator.hpp"
#include "lambda-stack.h"
#include <mir.h>
#include <mir-gen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <malloc.h>  // alloca on Windows
#else
#include <alloca.h>
#endif
#include <time.h>

extern Type TYPE_ANY, TYPE_INT;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern void heap_destroy();

extern "C" {
    int gc_object_zone_class_index(size_t size);
}

// Profiling infrastructure (defined in runner.cpp)
#define PROFILE_MAX_SCRIPTS 64
typedef struct PhaseProfile {
    const char* script_path;
    double parse_ms;
    double ast_ms;
    double transpile_ms;
    double jit_init_ms;
    double file_write_ms;
    double c2mir_ms;
    double mir_gen_ms;
    int code_len;
} PhaseProfile;
extern bool is_profile_enabled();
extern PhaseProfile profile_data[];
extern int profile_count;
#ifdef _WIN32
#include <windows.h>
typedef LARGE_INTEGER profile_time_t;
#else
typedef struct timespec profile_time_t;
#endif
extern void profile_get_time(profile_time_t* t);
extern double elapsed_ms_val(profile_time_t t0, profile_time_t t1);
extern void profile_dump_to_file();
extern Url* get_current_dir();

// Forward declare Runner helper functions from runner.cpp
void runner_init(Runtime *runtime, Runner* runner);
void runner_setup_context(Runner* runner);
void clear_persistent_last_error();
extern __thread LambdaError* persistent_last_error;
extern __thread EvalContext* context;

// Ensure MIR inline allocation offsets stay correct if Context struct changes.
// EvalContext extends Context via single non-virtual inheritance; the layout is
// well-defined in practice. Suppress -Winvalid-offsetof for this file's offsetof uses.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(EvalContext, heap) == sizeof(Context),
    "EvalContext.heap offset changed — update MIR inline bump allocation code");
#pragma clang diagnostic pop

// Forward declare has_current_item_ref from build_ast.cpp
bool has_current_item_ref(AstNode* node);

// Forward declare resolve_sys_paths_recursive from runner.cpp
void resolve_sys_paths_recursive(Item item);

// Forward declare import resolver from mir.c
extern "C" {
    void *import_resolver(const char *name);
    void register_bss_gc_roots(void* mir_ctx);
    void reset_and_register_bss_gc_roots(void* mir_ctx);
    extern int g_mir_interp_mode;
}

// ============================================================================
// MIR Transpiler Context
// ============================================================================

// Loop label pair for break/continue
struct LoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
    MIR_reg_t task_scope_base;
};

typedef struct AsyncRegSpill {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    int slot;
} AsyncRegSpill;

struct MirTranspiler {
    // Input
    AstScript* script;
    const char* source;
    Runtime* runtime;
    bool is_main;
    int script_index;

    // Pattern type list (shared with Script's type_list for const_pattern access)
    ArrayList* type_list;
    ArrayList* const_list;
    Pool* script_pool;  // pool for pattern compilation

    // MIR context
    MIR_context_t ctx;
    MIR_module_t module;

    // Shared emit substrate (Phase 0 / P0.1): owns the per-function emit cursor
    // (func_item/func), the register + label counters, and the import cache.
    // em.ctx mirrors `ctx` above (immutable after init). Access via em.func /
    // em.func_item / em.reg_counter / em.label_counter / em.import_cache.
    MirEmitter em;

    // Local function items: name -> MIR_item_t
    struct hashmap* local_funcs;

    // Global variables: name -> GlobalVarEntry (BSS-backed module-level let bindings)
    struct hashmap* global_vars;

    // Variable scopes: array of hashmaps, each mapping name -> MirVarEntry
    struct hashmap* var_scopes[64];
    int scope_depth;

    // Loop label stack
    LoopLabels loop_stack[32];
    int loop_depth;

    // Runtime pointer register (loaded at function entry)
    MIR_reg_t rt_reg;

    // GC heap pointer register (loaded at function entry for inline alloc)
    MIR_reg_t gc_reg;

    // Consts pointer register
    MIR_reg_t consts_reg;

    // Per-module BSS item that holds this module's const_list pointer.
    // Stored here during transpile_mir_ast so all functions in the module use it
    // instead of context->consts, enabling correct cross-module function calls.
    MIR_item_t consts_bss;

    // Type-list pointer register: holds this module's type_list ArrayList* so that
    // cross-module function calls use the right type_list for map/elmt/object allocation.
    MIR_reg_t type_list_reg;

    // Per-module BSS item that holds this module's type_list pointer.
    MIR_item_t type_list_bss;

    // Current pipe context
    MIR_reg_t pipe_item_reg;
    MIR_reg_t pipe_index_reg;
    bool in_pipe;
    AstNode* last_index_object;

    // TCO
    AstFuncNode* tco_func;
    MIR_label_t tco_label;
    MIR_reg_t tco_count_reg;   // iteration counter for TCO guard
    bool in_tail_position;     // current expression is in tail position

    // Closure
    AstFuncNode* current_closure;
    MIR_reg_t env_reg;

    // Object method context: set when transpiling a method body inside an object type
    TypeObject* method_owner;
    MIR_reg_t self_reg;  // register holding the boxed self Item (_self parameter)

    // Whether we're inside a user-defined function (not the main function)
    bool in_user_func;

    // Whether we're inside a proc function body (pn)
    bool in_proc;
    bool preserve_proc_if_result;
    bool current_func_can_raise;

    // Lambda concurrency state-machine context. Only procedures in the
    // analyzer's task-context closure use these fields.
    bool in_async_proc;
    bool emitting_async_call;
    int async_call_state;
    int async_call_spill_count;
    bool async_call_resume_emitted;
    AstCallNode* async_call_target;
    MIR_reg_t async_frame_reg;
    MIR_reg_t async_scope_base_reg;
    MIR_label_t* async_state_labels;
    int async_state_count;
    int async_next_state;
    int async_next_slot;
    AsyncRegSpill* async_spills;
    int async_spill_count;
    int async_spill_capacity;
    bool async_tracking;
    bool async_tracking_suppressed;

    // P4-3.2: Current function/script body for mutation analysis
    // Set to script->child at module level, fn_node->body inside functions
    AstNode* func_body;

    // Set when a return/break/continue emits a terminal instruction.
    // Used to skip dead-code boxing that causes MIR type mismatches.
    bool block_returned;

    // Phase 4: Native function info map — tracks which functions have native
    // (unboxed) versions and their parameter/return types.
    // name -> NativeFuncInfo
    struct hashmap* native_func_info;

    // P4-3.4: Native return type for the current function being compiled.
    // Set when a function (fn or pn) has a native return type (INT, FLOAT, BOOL).
    // Used by transpile_return() to emit unboxed return values in procs.
    TypeId native_return_tid;
    Type* current_return_type;

    // Variadic function body context: when true, return/raise must emit restore_vargs
    bool in_variadic_body;

    // Infer cache: AstFuncNode* -> InferCacheEntry (cached param type inference results)
    struct hashmap* infer_cache;

    // View/edit template counter for generating unique function names
    int view_counter;

    // View/edit handler context: set when transpiling view body or handler
    bool in_view_context;          // true in view body or handler
    bool in_view_handler;          // true specifically in event handler
    bool view_is_edit;             // true when the view/edit template is 'edit' (Phase 4)
    MIR_reg_t view_model_reg;      // register holding model Item for state ops
    const char* view_template_ref; // template ref string for state store keying
    int view_handler_counter;      // counter for generating handler function names

    // Name pool for interning template_ref strings (shared with Transpiler/Input)
    NamePool* name_pool;
};

// ============================================================================
// Hashmap helpers for import_cache and var_scopes
// ============================================================================

struct LocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
    const FnVariantAnalysis* variant;
};
HASHMAP_DEFINE_STRKEY(local_func, struct LocalFuncEntry, name)

// Native function info: tracks parameter types and return type for functions
// that have a dual native+boxed version (Phase 4 optimization).
struct NativeFuncInfo {
    char name[128];           // mangled function name (native version)
    TypeId param_types[16];   // resolved (declared or inferred) TypeId per param
    MIR_type_t param_mir[16]; // MIR type per param
    int param_count;          // number of user params (excluding _env_ptr, _self, _vargs)
    TypeId return_type;       // resolved return TypeId (LMD_TYPE_ANY if unknown)
    MIR_type_t return_mir;    // MIR return type
    bool has_native;          // true if a native version was generated
};
HASHMAP_DEFINE_STRKEY(native_func, struct NativeFuncInfo, name)

// Global variable entry (module-level let bindings stored in BSS)
struct GlobalVarEntry {
    char name[128];
    MIR_item_t bss_item;
    TypeId type_id;
    MIR_type_t mir_type;
};
HASHMAP_DEFINE_STRKEY(global_var, struct GlobalVarEntry, name)

// Infer cache entry: caches inferred parameter types per function to avoid
// redundant body walks between prepass_forward_declare and transpile_func_def.
// Keyed by AstFuncNode pointer (stable across the compilation).
struct InferCacheEntry {
    AstFuncNode* fn;
    TypeId param_types[16];
    int param_count;
};
HASHMAP_DEFINE_PTRKEY(infer_cache, struct InferCacheEntry, fn)

// ============================================================================
// Helpers
// ============================================================================

static MIR_type_t type_to_mir(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_FLOAT:
        return MIR_T_D;
    default:
        // MIR registers only support I64, F, D, LD - use I64 for all non-float types
        // (including pointers, which are stored as I64 in MIR registers)
        return MIR_T_I64;
    }
}

static bool mir_type_needs_mutable_clone(TypeId type_id) {
    return type_id == LMD_TYPE_ANY || type_id == LMD_TYPE_ARRAY ||
           type_id == LMD_TYPE_ARRAY_NUM || type_id == LMD_TYPE_MAP ||
           type_id == LMD_TYPE_ELEMENT || type_id == LMD_TYPE_OBJECT;
}

static AstNode* mir_unwrap_primary(AstNode* node);

static bool mir_expr_may_return_container(AstNode* expr, TypeId expr_tid, TypeId target_tid) {
    if (mir_type_needs_mutable_clone(expr_tid) && expr_tid != LMD_TYPE_ANY) return true;
    if (expr_tid != LMD_TYPE_ANY && target_tid != LMD_TYPE_ANY) return false;

    AstNode* root_expr = mir_unwrap_primary(expr);
    if (!root_expr) return target_tid == LMD_TYPE_ANY;
    switch (root_expr->node_type) {
    case AST_NODE_ARRAY:
    case AST_NODE_MAP:
    case AST_NODE_ELEMENT:
    case AST_NODE_LIST:
    case AST_NODE_IDENT:
    case AST_NODE_INDEX_EXPR:
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_CALL_EXPR:
    case AST_NODE_IF_EXPR:
    case AST_NODE_MATCH_EXPR:
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM:
        return true;
    default:
        // `any` arithmetic/comparison paths are scalar in practice; cloning
        // their boxed result turns tight integer loops into runtime calls.
        return false;
    }
}

static AstNode* mir_unwrap_primary(AstNode* node) {
    while (node && node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* primary = (AstPrimaryNode*)node;
        node = primary->expr;
    }
    return node;
}

static AstIdentNode* mir_compound_root_ident(AstNode* node) {
    node = mir_unwrap_primary(node);
    if (!node) return NULL;
    if (node->node_type == AST_NODE_IDENT) return (AstIdentNode*)node;
    if (node->node_type == AST_NODE_INDEX_EXPR || node->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* field = (AstFieldNode*)node;
        return mir_compound_root_ident(field->object);
    }
    return NULL;
}

static bool mir_call_has_var_param(AstCallNode* call) {
    if (!call) return false;
    AstNode* fn_expr = mir_unwrap_primary(call->function);
    if (!fn_expr || fn_expr->node_type != AST_NODE_IDENT) return false;
    AstIdentNode* ident = (AstIdentNode*)fn_expr;
    AstNode* fn_node_raw = ident->entry ? ident->entry->node : NULL;
    if (!fn_node_raw || (fn_node_raw->node_type != AST_NODE_FUNC &&
        fn_node_raw->node_type != AST_NODE_FUNC_EXPR &&
        fn_node_raw->node_type != AST_NODE_PROC)) {
        return false;
    }
    if (fn_node_raw->node_type == AST_NODE_PROC) {
        // Procedural helpers are the mutable layer: constructors and accessors
        // often return freshly-owned or borrowed containers. Re-cloning their
        // result at every `var` binding makes graph algorithms pathological.
        return true;
    }
    AstFuncNode* fn_node = (AstFuncNode*)fn_node_raw;
    if (!fn_node->type || fn_node->type->type_id != LMD_TYPE_FUNC) return false;
    TypeParam* param = ((TypeFunc*)fn_node->type)->param;
    while (param) {
        if (param->is_var_param) return true;
        param = param->next;
    }
    return false;
}

static bool mir_var_rhs_keeps_mutable_alias(AstNode* rhs) {
    AstNode* root_expr = mir_unwrap_primary(rhs);
    if (!root_expr) {
        return false;
    }
    if (root_expr->node_type == AST_NODE_CALL_EXPR) {
        // Calls that accept explicit `var` parameters are already in the
        // borrowed-mutation channel; their result may intentionally expose a
        // mutable subvalue, so cloning here breaks that alias contract.
        return mir_call_has_var_param((AstCallNode*)root_expr);
    }
    if (root_expr->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)root_expr;
        return ident->entry && ident->entry->is_mutable;
    }
    if (root_expr->node_type != AST_NODE_INDEX_EXPR &&
        root_expr->node_type != AST_NODE_MEMBER_EXPR) return false;
    AstIdentNode* root = mir_compound_root_ident(root_expr);
    return root && root->entry && root->entry->is_mutable;
}

// Thin wrappers over the shared emitter primitives (P0.1). These delegate to
// the em_* functions in mir_emitter_shared.hpp so the register/label/insn
// emit logic lives once and JsMirTranspiler can adopt the same primitives.
static void async_track_reg(MirTranspiler* mt, MIR_reg_t reg, MIR_type_t type) {
    if (mt->async_tracking && !mt->async_tracking_suppressed) {
        if (mt->async_spill_count >= mt->async_spill_capacity) {
            int next_capacity = mt->async_spill_capacity > 0
                ? mt->async_spill_capacity * 2 : 64;
            AsyncRegSpill* resized = (AsyncRegSpill*)mem_realloc(mt->async_spills,
                sizeof(AsyncRegSpill) * (size_t)next_capacity, MEM_CAT_EVAL);
            if (resized) {
                mt->async_spills = resized;
                mt->async_spill_capacity = next_capacity;
            }
        }
        if (mt->async_spill_count < mt->async_spill_capacity) {
            AsyncRegSpill* spill = &mt->async_spills[mt->async_spill_count++];
            spill->reg = reg;
            spill->mir_type = type == MIR_T_D ? MIR_T_D : MIR_T_I64;
            spill->slot = mt->async_next_slot++;
        }
    }
}

static MIR_reg_t new_reg(MirTranspiler* mt, const char* prefix, MIR_type_t type) {
    MIR_reg_t reg = em_new_reg(&mt->em, prefix, type);
    async_track_reg(mt, reg, type);
    return reg;
}

static MIR_label_t new_label(MirTranspiler* mt) {
    return em_new_label(&mt->em);
}

static void emit_insn(MirTranspiler* mt, MIR_insn_t insn) {
    em_emit_insn(&mt->em, insn);
}

static void emit_label(MirTranspiler* mt, MIR_label_t label) {
    em_emit_label(&mt->em, label);
}

// emit a register holding the boxed null Item. Used as the "value" of statements that
// produce no result (e.g. an index-assignment `arr[i] = v`) so callers that consume the
// result — a function return, a for-loop body push, `let x = (arr[i] = v)` — never see
// the invalid-reg sentinel (reg 0), which would crash MIR with "undeclared reg 0".
static MIR_reg_t emit_null_item_reg(MirTranspiler* mt) {
    MIR_reg_t r = new_reg(mt, "stmt_null", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    mir_emit_i64_const_to_reg(mt->ctx, mt->em.func_item, r, (int64_t)NULL_VAL);
    return r;
}

#define emit_call_0(mt, fn, ret) em_call_0(&(mt)->em, fn, ret, false)
#define emit_call_1(mt, fn, ret, ...) em_call_1(&(mt)->em, fn, ret, __VA_ARGS__, false)
#define emit_call_2(mt, fn, ret, ...) em_call_2(&(mt)->em, fn, ret, __VA_ARGS__, false)
#define emit_call_3(mt, fn, ret, ...) em_call_3(&(mt)->em, fn, ret, __VA_ARGS__, false)
#define emit_call_4(mt, fn, ret, ...) em_call_4(&(mt)->em, fn, ret, __VA_ARGS__, false)
#define emit_call_5(mt, fn, ret, ...) em_call_5(&(mt)->em, fn, ret, __VA_ARGS__, false)
#define emit_call_8(mt, fn, ret, ...) em_call_8(&(mt)->em, fn, ret, __VA_ARGS__, false)
#define emit_call_void_1(mt, fn, ...) em_call_void_1(&(mt)->em, fn, __VA_ARGS__, false)
#define emit_call_void_2(mt, fn, ...) em_call_void_2(&(mt)->em, fn, __VA_ARGS__, false)
#define emit_call_void_3(mt, fn, ...) em_call_void_3(&(mt)->em, fn, __VA_ARGS__, false)
#define emit_call_void_4(mt, fn, ...) em_call_void_4(&(mt)->em, fn, __VA_ARGS__, false)
static MIR_reg_t emit_box(MirTranspiler* mt, MIR_reg_t val_reg, TypeId type_id);
static void async_store_var(MirTranspiler* mt, MirVarEntry* var);
static void transpile_task_scope_unwind(MirTranspiler* mt, bool error_exit);

static bool should_gc_root_var(MIR_type_t mir_type, TypeId type_id) {
    if (mir_type == MIR_T_P) return true;
    switch (type_id) {
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_STRING:
    case LMD_TYPE_BINARY:
    case LMD_TYPE_PATH:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_MAP:
    case LMD_TYPE_VMAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:
    case LMD_TYPE_TYPE:
    case LMD_TYPE_FUNC:
    case LMD_TYPE_ANY:
    case LMD_TYPE_ERROR:
        return true;
    default:
        return false;
    }
}

static void emit_jit_root_frame_enter(MirTranspiler* mt) {
    mt->em.frame.root_slot_count = 0;
    mt->em.frame.root_base = new_reg(mt, "root_frame", MIR_T_I64);
    mt->em.frame.anchor = new_label(mt);
    emit_label(mt, mt->em.frame.anchor);
}

static void emit_jit_root_frame_exit(MirTranspiler* mt) {
    em_store_frame_top(&mt->em, mt->em.frame.runtime,
        offsetof(Context, side_root_top), mt->em.frame.root_base);
}

enum FunctionReturnLaneKind {
    RETURN_LANE_NONE = 0,
    RETURN_LANE_SCALAR = 1,
    RETURN_LANE_ERROR = 2,
};

static void begin_function_epilogue(MirTranspiler* mt, MIR_type_t return_type,
                                    int lane_kind = RETURN_LANE_NONE,
                                    MirScalarReturnMode scalar_mode =
                                        MIR_SCALAR_RETURN_DYNAMIC) {
    mt->em.frame.return_label = new_label(mt);
    mt->em.frame.return_type = return_type;
    mt->em.frame.return_reg = new_reg(mt, "return_value", return_type);
    mt->em.frame.return_lane_kind = lane_kind;
    mt->em.frame.scalar_return_mode = scalar_mode;
    mt->em.frame.error_return_reg = lane_kind == RETURN_LANE_ERROR
        ? new_reg(mt, "return_error", MIR_T_I64)
        : 0;
    mt->em.frame.plan.entry_kind = FN_ENTRY_PUBLIC_WRAPPER;
    mt->em.frame.plan.entry_mode = MIR_ENTRY_CHECKED;
    mt->em.frame.active = true;
}

static void emit_function_return(MirTranspiler* mt, MIR_op_t value) {
    MIR_insn_code_t move = mt->em.frame.return_type == MIR_T_D ? MIR_DMOV : MIR_MOV;
    emit_insn(mt, MIR_new_insn(mt->ctx, move,
        MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg), value));
    if (mt->em.frame.return_lane_kind == RETURN_LANE_ERROR) {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, mt->em.frame.error_return_reg),
            MIR_new_int_op(mt->ctx, 0)));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, mt->em.frame.return_label)));
}

static void emit_function_error_return(MirTranspiler* mt, MIR_reg_t error_item) {
    if (mt->em.frame.return_lane_kind != RETURN_LANE_ERROR) {
        emit_function_return(mt, MIR_new_reg_op(mt->ctx, error_item));
        return;
    }
    MIR_insn_code_t move = mt->em.frame.return_type == MIR_T_D ? MIR_DMOV : MIR_MOV;
    MIR_op_t zero = mt->em.frame.return_type == MIR_T_D
        ? MIR_new_double_op(mt->ctx, 0.0) : MIR_new_int_op(mt->ctx, 0);
    emit_insn(mt, MIR_new_insn(mt->ctx, move,
        MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg), zero));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, mt->em.frame.error_return_reg),
        MIR_new_reg_op(mt->ctx, error_item)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, mt->em.frame.return_label)));
}

static void emit_number_frame_enter(MirTranspiler* mt) {
    mt->em.frame.number_base = new_reg(mt, "number_frame", MIR_T_I64);
    mt->em.frame.fixed_number_slots =
        mt->em.frame.return_lane_kind == RETURN_LANE_ERROR ? 1 : 0;
    mt->em.frame.plan.fixed_number_scratch_slots =
        mt->em.frame.fixed_number_slots;
    mt->em.frame.number_active = true;
}

static void emit_number_frame_exit(MirTranspiler* mt) {
    if (!mt->em.frame.number_active) return;
    em_store_frame_top(&mt->em, mt->em.frame.runtime,
        offsetof(Context, side_number_top), mt->em.frame.number_base);
}

static void finish_function_epilogue(MirTranspiler* mt) {
    emit_label(mt, mt->em.frame.return_label);
    MIR_reg_t error_scratch = 0;
    if (mt->em.frame.return_lane_kind == RETURN_LANE_ERROR) {
        error_scratch = em_materialize_frame_ref(&mt->em,
            em_fixed_number_scratch_ref(0));
        MIR_insn_code_t first_store = mt->em.frame.return_type == MIR_T_D
            ? MIR_DMOV : MIR_MOV;
        MIR_type_t first_type = mt->em.frame.return_type == MIR_T_D
            ? MIR_T_D : MIR_T_I64;
        // Preserve ARM64's nested-call first result before caller-home handoff.
        emit_insn(mt, MIR_new_insn(mt->ctx, first_store,
            MIR_new_mem_op(mt->ctx, first_type, 0,
                error_scratch, 0, 1),
            MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg)));
    }
    // Cleanup is emitted before each branch here; restoring the root watermark
    // last keeps the in-flight return value live across cleanup calls that may collect.
    if (mt->em.frame.root_slot_count > 0) emit_jit_root_frame_exit(mt);
    if (mt->em.frame.return_lane_kind == RETURN_LANE_SCALAR) {
        // Preserve scalar payloads in the caller home before extent teardown.
        MIR_reg_t rehomed = em_adopt_scalar_item(&mt->em,
            mt->em.frame.scalar_return_mode, mt->em.frame.return_reg,
            mt->em.frame.runtime, offsetof(Context, side_number_top),
            mt->em.frame.number_base, mt->em.frame.incoming_scalar_home);
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_reg_op(mt->ctx, rehomed)));
    } else if (mt->em.frame.return_lane_kind == RETURN_LANE_ERROR) {
        MIR_reg_t adopted_error = em_adopt_scalar_item(&mt->em,
            MIR_SCALAR_RETURN_DYNAMIC, mt->em.frame.error_return_reg,
            mt->em.frame.runtime, offsetof(Context, side_number_top),
            mt->em.frame.number_base, mt->em.frame.incoming_scalar_home);
        MIR_reg_t first = new_reg(mt, "return_first_final", mt->em.frame.return_type);
        MIR_insn_code_t first_load = mt->em.frame.return_type == MIR_T_D
            ? MIR_DMOV : MIR_MOV;
        MIR_type_t first_type = mt->em.frame.return_type == MIR_T_D
            ? MIR_T_D : MIR_T_I64;
        emit_insn(mt, MIR_new_insn(mt->ctx, first_load,
            MIR_new_reg_op(mt->ctx, first),
            MIR_new_mem_op(mt->ctx, first_type, 0,
                error_scratch, 0, 1)));
        em_store_frame_top(&mt->em, mt->em.frame.runtime,
            offsetof(Context, mir_return_lane),
            adopted_error);
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_reg_op(mt->ctx, first)));
    } else {
        emit_number_frame_exit(mt);
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_reg_op(mt->ctx, mt->em.frame.return_reg)));
    }
    mt->em.frame.active = false;
    mt->em.frame.number_active = false;
}

static void finalize_side_root_frame(MirTranspiler* mt) {
    em_finalize_scalar_homes(&mt->em);
    em_finalize_frame_prologue(&mt->em, mt->em.frame.plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit));
    emit_call_void_1(mt, "lambda_stack_overflow_error", MIR_T_P,
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)"side-stack"));
    if (mt->em.frame.return_lane_kind == RETURN_LANE_ERROR) {
        MIR_op_t first = mt->em.frame.return_type == MIR_T_D
            ? MIR_new_double_op(mt->ctx, 0.0)
            : MIR_new_uint_op(mt->ctx, ITEM_ERROR);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                offsetof(Context, mir_return_lane),
                mt->em.frame.runtime, 0, 1),
            MIR_new_uint_op(mt->ctx, ITEM_ERROR)));
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1, first));
    } else if (mt->em.frame.return_type == MIR_T_D) {
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_double_op(mt->ctx, 0.0)));
    } else {
        emit_insn(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_uint_op(mt->ctx, ITEM_ERROR)));
    }
    em_finalize_function_metadata(&mt->em);
}

static void finalize_gc_root_publication(MirTranspiler* mt,
        const char* function_name) {
    if (!mt || mt->em.frame.root_slot_count <= 0) return;
    MirRootWriteBackResult result;
    if (em_finalize_recorded_roots(&mt->em, mt->em.frame.root_base,
            mt->em.frame.anchor, &result, function_name)) {
        mt->em.frame.root_slot_count = result.stable_slots + result.scratch_slots;
    }
}

static const MIR_reg_t MIR_ROOT_MEMORY_AUTHORITATIVE = (MIR_reg_t)-1;

static void note_gc_root_candidate(MirTranspiler* mt, MIR_reg_t value,
        int root_slot) {
    if (!em_root_note_candidate(&mt->em.frame.gc_candidates,
            &mt->em.frame.gc_candidate_count, &mt->em.frame.gc_candidate_capacity,
            &mt->em.frame.gc_candidate_by_reg,
            &mt->em.frame.gc_candidate_by_reg_capacity, value,
            JIT_VALUE_UNKNOWN, root_slot + 1)) {
        log_error("mir-root-candidates: unable to record reg=%u",
            (unsigned)value);
        // Candidate identity is unavailable to every later correctness
        // fallback, so compilation must fail closed.
        abort();
    }
}

static void record_pending_gc_root_store(MirTranspiler* mt, int root_slot,
        MIR_reg_t value) {
    if (mt->em.frame.pending_root_store_count >=
            mt->em.frame.pending_root_store_capacity) {
        int next_capacity = mt->em.frame.pending_root_store_capacity
            ? mt->em.frame.pending_root_store_capacity * 2 : 32;
        MirPendingRootStore* next = (MirPendingRootStore*)mem_realloc(
            mt->em.frame.pending_root_stores,
            (size_t)next_capacity * sizeof(MirPendingRootStore), MEM_CAT_EVAL);
        if (!next) {
            log_error("mir-root-pending-stores: unable to grow to %d",
                next_capacity);
            abort();
        }
        mt->em.frame.pending_root_stores = next;
        mt->em.frame.pending_root_store_capacity = next_capacity;
    }
    MirPendingRootStore* pending =
        &mt->em.frame.pending_root_stores[mt->em.frame.pending_root_store_count++];
    pending->slot = root_slot;
    pending->value = value;
    pending->definition = DLIST_TAIL(MIR_insn_t, mt->em.func->insns);
}

static void materialize_pending_gc_root_stores(MirTranspiler* mt,
        int root_slot) {
    for (int i = 0; i < mt->em.frame.pending_root_store_count; i++) {
        MirPendingRootStore* pending = &mt->em.frame.pending_root_stores[i];
        if (pending->slot != root_slot) continue;
        MIR_type_t value_type = MIR_reg_type(mt->ctx, pending->value,
            mt->em.func);
        if (value_type != MIR_T_P) value_type = MIR_T_I64;
        MIR_insn_t store = MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, value_type,
                (MIR_disp_t)root_slot * (MIR_disp_t)sizeof(uint64_t),
                mt->em.frame.root_base, 0, 1),
            MIR_new_reg_op(mt->ctx, pending->value));
        if (pending->definition) {
            MIR_insert_insn_after(mt->ctx, mt->em.func_item,
                pending->definition, store);
        } else {
            MIR_append_insn(mt->ctx, mt->em.func_item, store);
        }
        pending->slot = -1;
    }
}

static void store_gc_root_slot(MirTranspiler* mt, int root_slot, MIR_reg_t value) {
    if (root_slot < 0) return;
    if (root_slot >= mt->em.frame.root_latest_capacity) {
        int next_capacity = mt->em.frame.root_latest_capacity
            ? mt->em.frame.root_latest_capacity * 2 : 32;
        while (next_capacity <= root_slot) next_capacity *= 2;
        MIR_reg_t* next = (MIR_reg_t*)mem_realloc(mt->em.frame.root_latest,
            (size_t)next_capacity * sizeof(MIR_reg_t), MEM_CAT_EVAL);
        if (!next) {
            log_error("mir-root-latest: failed to grow slot map to %d",
                next_capacity);
            return;
        }
        memset(next + mt->em.frame.root_latest_capacity, 0,
            (size_t)(next_capacity - mt->em.frame.root_latest_capacity) *
                sizeof(MIR_reg_t));
        mt->em.frame.root_latest = next;
        mt->em.frame.root_latest_capacity = next_capacity;
    }
    MIR_reg_t previous = mt->em.frame.root_latest[root_slot];
    MIR_type_t value_type = MIR_reg_type(mt->ctx, value, mt->em.func);
    if (value_type != MIR_T_P) value_type = MIR_T_I64;
    note_gc_root_candidate(mt, value, root_slot);
    if (previous != MIR_ROOT_MEMORY_AUTHORITATIVE) {
        // Buffer definitions so only real control-flow merges acquire homes.
        record_pending_gc_root_store(mt, root_slot, value);
        if (previous && previous != value) {
            mt->em.frame.root_latest[root_slot] =
                MIR_ROOT_MEMORY_AUTHORITATIVE;
            materialize_pending_gc_root_stores(mt, root_slot);
        } else {
            mt->em.frame.root_latest[root_slot] = value;
        }
        return;
    }
    em_store_frame_slot_typed(&mt->em, mt->em.frame.root_base, root_slot,
        value, value_type);
}

static int create_binding_gc_root_slot(MirTranspiler* mt, MIR_reg_t value) {
    int root_slot = mt->em.frame.root_slot_count++;
    store_gc_root_slot(mt, root_slot, value);
    return root_slot;
}

static int create_gc_root_slot(MirTranspiler* mt, MIR_reg_t value) {
    int root_slot = mt->em.frame.root_slot_count++;
    note_gc_root_candidate(mt, value, root_slot);
    if (root_slot >= mt->em.frame.root_latest_capacity) {
        int next_capacity = mt->em.frame.root_latest_capacity
            ? mt->em.frame.root_latest_capacity * 2 : 32;
        while (next_capacity <= root_slot) next_capacity *= 2;
        MIR_reg_t* next = (MIR_reg_t*)mem_realloc(mt->em.frame.root_latest,
            (size_t)next_capacity * sizeof(MIR_reg_t), MEM_CAT_EVAL);
        if (!next) {
            log_error("mir-root-latest: failed to grow direct candidate map to %d",
                next_capacity);
            abort();
        }
        memset(next + mt->em.frame.root_latest_capacity, 0,
            (size_t)(next_capacity - mt->em.frame.root_latest_capacity) *
                sizeof(MIR_reg_t));
        mt->em.frame.root_latest = next;
        mt->em.frame.root_latest_capacity = next_capacity;
    }
    mt->em.frame.root_latest[root_slot] = value;
    return root_slot;
}

static void lambda_call_root_value(void* owner, MIR_reg_t reg) {
    MirTranspiler* mt = (MirTranspiler*)owner;
    if (mt && mt->em.frame.active && mt->em.frame.root_base && reg) {
        create_gc_root_slot(mt, reg);
    }
}

static void lambda_after_call_result(void* owner, MIR_reg_t reg,
        MIR_type_t type) {
    MirTranspiler* mt = (MirTranspiler*)owner;
    if (mt && reg) async_track_reg(mt, reg, type);
}

static int create_pointer_gc_root_slot(MirTranspiler* mt, MIR_reg_t ptr) {
    return create_gc_root_slot(mt, ptr);
}

static MIR_reg_t load_gc_root_slot(MirTranspiler* mt, int root_slot, const char* prefix) {
    if (root_slot < 0) {
        MIR_reg_t zero = new_reg(mt, prefix, MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, zero),
            MIR_new_int_op(mt->ctx, 0)));
        return zero;
    }
    if (root_slot < mt->em.frame.root_latest_capacity &&
            mt->em.frame.root_latest[root_slot] &&
            mt->em.frame.root_latest[root_slot] != MIR_ROOT_MEMORY_AUTHORITATIVE) {
        // Write-back makes registers authoritative between safepoints. Reading
        // the eager oracle slot after its stores are deleted would return stale
        // data, so consumers use the latest semantic value directly.
        return mt->em.frame.root_latest[root_slot];
    }
    MIR_reg_t value = new_reg(mt, prefix, MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, value),
        MIR_new_mem_op(mt->ctx, MIR_T_I64,
            (MIR_disp_t)root_slot * (MIR_disp_t)sizeof(uint64_t),
            mt->em.frame.root_base, 0, 1)));
    return value;
}

static void update_gc_root_slot(MirTranspiler* mt, MirVarEntry* var) {
    if (!var) return;
    async_store_var(mt, var);
    // The async frame's registered Item range owns suspended locals; a second
    // side-root slot would duplicate the same liveness edge on every write.
    if (var->async_slot >= 0) return;
    if (var->root_slot < 0 && !should_gc_root_var(var->mir_type, var->type_id)) return;
    if (var->root_slot < 0) {
        var->root_slot = create_binding_gc_root_slot(mt, var->reg);
        return;
    }
    store_gc_root_slot(mt, var->root_slot, var->reg);
}

static MIR_reg_t root_gc_result_if_needed(MirTranspiler* mt, MIR_reg_t result,
    MIR_type_t mir_type, TypeId type_id, const char* prefix) {
    if (!should_gc_root_var(mir_type, type_id)) return result;
    int root_slot = create_gc_root_slot(mt, result);
    return load_gc_root_slot(mt, root_slot, prefix);
}

// ============================================================================
// Scope management
// ============================================================================

static void push_scope(MirTranspiler* mt) {
    if (mt->scope_depth >= 63) { log_error("mir: scope overflow"); return; }
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = em_var_scope_new(16);
}

static void pop_scope(MirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("mir: scope underflow"); return; }
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

static void set_var(MirTranspiler* mt, const char* name, MIR_reg_t reg, MIR_type_t mir_type, TypeId type_id) {
    VarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.root_slot = -1;
    entry.var.async_slot = -1;
    entry.var.mir_type = mir_type;
    entry.var.type_id = type_id;
    entry.var.elem_type = LMD_TYPE_ANY;
    entry.var.num_type = NUM_INT8;
    entry.var.env_offset = -1;  // not a captured variable by default
    entry.var.is_state_var = false;
    if (mt->in_async_proc) entry.var.async_slot = mt->async_next_slot++;
    if (entry.var.async_slot < 0 && should_gc_root_var(mir_type, type_id)) {
        entry.var.root_slot = create_binding_gc_root_slot(mt, reg);
    }
    async_store_var(mt, &entry.var);
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

// set a state variable (writes go to template state store)
static void set_state_var(MirTranspiler* mt, const char* name, MIR_reg_t reg,
                          MIR_type_t mir_type, TypeId type_id,
                          const char* interned_name_ptr) {
    VarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.root_slot = -1;
    entry.var.async_slot = -1;
    entry.var.mir_type = mir_type;
    entry.var.type_id = type_id;
    entry.var.elem_type = LMD_TYPE_ANY;
    entry.var.num_type = NUM_INT8;
    entry.var.env_offset = -1;
    entry.var.is_state_var = true;
    entry.var.state_name_ptr = interned_name_ptr;
    if (mt->in_async_proc) entry.var.async_slot = mt->async_next_slot++;
    if (entry.var.async_slot < 0 && should_gc_root_var(mir_type, type_id)) {
        entry.var.root_slot = create_binding_gc_root_slot(mt, reg);
    }
    async_store_var(mt, &entry.var);
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static MirVarEntry* find_var(MirTranspiler* mt, const char* name) {
    VarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        VarScopeEntry* found = (VarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) {
            return &found->var;
        }
    }
    return NULL;
}

// Look up a global (module-level) variable
static GlobalVarEntry* find_global_var(MirTranspiler* mt, const char* name) {
    if (!mt->global_vars) return NULL;
    GlobalVarEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return (GlobalVarEntry*)hashmap_get(mt->global_vars, &key);
}

// Forward declarations (defined after emit_box/emit_unbox)
static MIR_reg_t load_global_var(MirTranspiler* mt, GlobalVarEntry* gvar);
static void store_global_var(MirTranspiler* mt, GlobalVarEntry* gvar, MIR_reg_t val, TypeId val_tid);

// ============================================================================
// Import management (lazy proto + import creation)
// ============================================================================

// Look up a locally defined function
static LocalFuncEntry* find_local_func_entry(MirTranspiler* mt,
        const char* name) {
    LocalFuncEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return (LocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
}

static MIR_item_t find_local_func(MirTranspiler* mt, const char* name) {
    LocalFuncEntry* found = find_local_func_entry(mt, name);
    return found ? found->func_item : NULL;
}

static void register_local_func_contract(MirTranspiler* mt, const char* name,
        MIR_item_t func_item, const FnVariantAnalysis* variant = NULL) {
    LocalFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.func_item = func_item;
    entry.variant = variant;
    hashmap_set(mt->local_funcs, &entry);
}

static void register_local_func(MirTranspiler* mt, const char* name,
        MIR_item_t func_item) {
    register_local_func_contract(mt, name, func_item);
}

// Look up native function info for dual-version functions
static NativeFuncInfo* find_native_func_info(MirTranspiler* mt, const char* name) {
    if (!mt->native_func_info) return NULL;
    NativeFuncInfo key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return (NativeFuncInfo*)hashmap_get(mt->native_func_info, &key);
}

static void register_native_func_info(MirTranspiler* mt, const char* name, NativeFuncInfo* info) {
    if (!mt->native_func_info) {
        mt->native_func_info = native_func_new(16);
    }
    snprintf(info->name, sizeof(info->name), "%s", name);
    hashmap_set(mt->native_func_info, info);
}

// Check if a parameter type qualifies for native (unboxed) representation.
// Only types that map to a different native type than boxed Item are worth unboxing.
static bool mir_is_native_param_type(TypeId tid) {
    return is_native_param_type_id(tid);
}

static bool mir_is_native_scalar_value_type(TypeId tid) {
    // Generated calls return an Item so scalar-home adoption can preserve the
    // payload. INT64 callers immediately recover their raw MIR value; otherwise
    // later boxing would treat the tagged Item bits as an integer payload.
    return tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 ||
        tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL;
}

static TypeId resolve_declared_param_type(AstNamedNode* param, TypeParam* type_param) {
    if (!param) return LMD_TYPE_ANY;
    TypeId tid = param->type ? param->type->type_id : LMD_TYPE_ANY;
    bool may_be_null = type_param && type_param->is_optional && !type_param->default_value;

    // Typed array annotations are stored as TypeParam/TypeUnary wrappers. The
    // MIR native-call metadata must keep them as boxed containers; treating
    // `float[]` as scalar FLOAT makes callers pass an array where the proto
    // expects a double.
    if (param->type && param->type->kind == TYPE_KIND_UNARY) {
        TypeParam* tp_cast = (TypeParam*)param->type;
        Type* full = tp_cast->full_type;
        if (full && full->kind == TYPE_KIND_UNARY) {
            TypeUnary* unary = (TypeUnary*)full;
            Type* operand = unary->operand;
            if (operand && operand->type_id == LMD_TYPE_TYPE && operand->kind == TYPE_KIND_SIMPLE) {
                operand = ((TypeType*)operand)->type;
            }
            if (operand) {
                switch (operand->type_id) {
                case LMD_TYPE_FLOAT:
                case LMD_TYPE_INT:
                case LMD_TYPE_INT64:
                    tid = LMD_TYPE_ARRAY_NUM;
                    break;
                default: break;
                }
            }
        }
    }

    if (may_be_null) tid = LMD_TYPE_ANY;
    return tid;
}

// Get or create import + proto for a runtime function
static MirImportEntry* ensure_import(MirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    return em_ensure_import(&mt->em, name, ret_type, nargs, args, nres, false);
}

// ============================================================================
// Variadic call helpers for map_fill, elmt_fill, list_fill
// ============================================================================

// Emit a variadic call: fn_name(mandatory_arg, vararg1, vararg2, ...)
// mandatory_args + varargs are passed as ops. proto is created with vararg flag.
static MIR_reg_t emit_vararg_call(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, int nres,
    MIR_type_t mandatory_type, MIR_op_t mandatory_op,
    int n_varargs, MIR_op_t* vararg_ops) {
    // Create unique vararg proto name per call site
    char proto_name[160];
    snprintf(proto_name, sizeof(proto_name), "%s_vp%d", fn_name, mt->em.label_counter++);

    // Mandatory arg
    MIR_var_t mandatory_var = {mandatory_type, "m", 0};
    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_vararg_proto_arr(mt->ctx, proto_name, nres, res_types, 1, &mandatory_var);

    // Import
    MirImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", fn_name);
    MirImportCacheEntry* found = (MirImportCacheEntry*)hashmap_get(mt->em.import_cache, &key);
    MIR_item_t imp;
    if (found) {
        imp = found->entry.import;
    } else {
        imp = MIR_new_import(mt->ctx, fn_name);
        // Cache only the import, not proto (proto is unique per call)
        MirImportCacheEntry new_entry;
        memset(&new_entry, 0, sizeof(new_entry));
        snprintf(new_entry.name, sizeof(new_entry.name), "%s", fn_name);
        new_entry.entry.import = imp;
        new_entry.entry.proto = proto; // store last proto
        hashmap_set(mt->em.import_cache, &new_entry);
    }

    // Build call: proto, import, [result], mandatory, varargs...
    int nops = (nres > 0 ? 3 : 2) + 1 + n_varargs; // +1 for mandatory arg
    MIR_op_t* ops = LAMBDA_ALLOCA(nops, MIR_op_t);
    int oi = 0;
    ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
    ops[oi++] = MIR_new_ref_op(mt->ctx, imp);
    MIR_reg_t result = 0;
    if (nres > 0) {
        result = new_reg(mt, fn_name, ret_type);
        ops[oi++] = MIR_new_reg_op(mt->ctx, result);
    }
    ops[oi++] = mandatory_op;
    for (int i = 0; i < n_varargs; i++) {
        ops[oi++] = vararg_ops[i];
    }

    emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, oi, ops));
    return result;
}

// Emit vararg call with 2 mandatory args (for list_fill: List*, int count, ...)
static MIR_reg_t emit_vararg_call_2(MirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, int nres,
    MIR_type_t m1_type, MIR_op_t m1_op,
    MIR_type_t m2_type, MIR_op_t m2_op,
    int n_varargs, MIR_op_t* vararg_ops) {
    char proto_name[160];
    snprintf(proto_name, sizeof(proto_name), "%s_vp%d", fn_name, mt->em.label_counter++);

    MIR_var_t mandatory_vars[2] = {{m1_type, "m1", 0}, {m2_type, "m2", 0}};
    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_vararg_proto_arr(mt->ctx, proto_name, nres, res_types, 2, mandatory_vars);

    MirImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", fn_name);
    MirImportCacheEntry* found = (MirImportCacheEntry*)hashmap_get(mt->em.import_cache, &key);
    MIR_item_t imp;
    if (found) {
        imp = found->entry.import;
    } else {
        imp = MIR_new_import(mt->ctx, fn_name);
        MirImportCacheEntry new_entry;
        memset(&new_entry, 0, sizeof(new_entry));
        snprintf(new_entry.name, sizeof(new_entry.name), "%s", fn_name);
        new_entry.entry.import = imp;
        new_entry.entry.proto = proto;
        hashmap_set(mt->em.import_cache, &new_entry);
    }

    int nops = (nres > 0 ? 3 : 2) + 2 + n_varargs; // +2 for m1 and m2
    MIR_op_t* ops = LAMBDA_ALLOCA(nops, MIR_op_t);
    int oi = 0;
    ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
    ops[oi++] = MIR_new_ref_op(mt->ctx, imp);
    MIR_reg_t result = 0;
    if (nres > 0) {
        result = new_reg(mt, fn_name, ret_type);
        ops[oi++] = MIR_new_reg_op(mt->ctx, result);
    }
    ops[oi++] = m1_op;
    ops[oi++] = m2_op;
    for (int i = 0; i < n_varargs; i++) {
        ops[oi++] = vararg_ops[i];
    }

    emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, oi, ops));
    return result;
}

// ============================================================================
// Emit runtime function calls
// ============================================================================

// ============================================================================
// Boxing/Unboxing Helpers (emit inline MIR instructions)
// ============================================================================

// Box int64_t -> Item (inline i2it macro equivalent)
// i2it(v) = (v <= INT56_MAX && v >= INT56_MIN) ? (ITEM_INT | (v & MASK56)) : ITEM_ERROR
static MIR_reg_t emit_box_int(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxi", MIR_T_I64);
    MIR_reg_t masked = new_reg(mt, "mask", MIR_T_I64);
    MIR_reg_t tagged = new_reg(mt, "tag", MIR_T_I64);
    MIR_reg_t in_range = new_reg(mt, "rng", MIR_T_I64);
    MIR_reg_t le_max = new_reg(mt, "le", MIR_T_I64);
    MIR_reg_t ge_min = new_reg(mt, "ge", MIR_T_I64);

    int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
    int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;
    uint64_t MASK56 = 0x00FFFFFFFFFFFFFFULL;
    uint64_t ITEM_INT_TAG = (uint64_t)LMD_TYPE_INT << 56;

    // le_max = val <= INT56_MAX
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LE, MIR_new_reg_op(mt->ctx, le_max),
        MIR_new_reg_op(mt->ctx, val_reg), MIR_new_int_op(mt->ctx, INT56_MAX_VAL)));
    // ge_min = val >= INT56_MIN
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, ge_min),
        MIR_new_reg_op(mt->ctx, val_reg), MIR_new_int_op(mt->ctx, INT56_MIN_VAL)));
    // in_range = le_max & ge_min
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, in_range),
        MIR_new_reg_op(mt->ctx, le_max), MIR_new_reg_op(mt->ctx, ge_min)));
    // masked = val & MASK56
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
        MIR_new_reg_op(mt->ctx, val_reg), MIR_new_int_op(mt->ctx, (int64_t)MASK56)));
    // tagged = ITEM_INT | masked
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, tagged),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_INT_TAG), MIR_new_reg_op(mt->ctx, masked)));

    // result = in_range ? tagged : ITEM_ERROR
    MIR_label_t l_ok = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_ok),
        MIR_new_reg_op(mt->ctx, in_range)));
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_ok);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, tagged)));
    emit_label(mt, l_end);
    return result;
}

// Zero-extend uint8_t Bool return from a runtime function call.
// MIR reads full i64 from the return register but C functions returning uint8_t
// may leave garbage in the upper 56 bits. This masks to the low 8 bits.
static MIR_reg_t emit_uext8(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t ext = new_reg(mt, "bext", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_UEXT8, MIR_new_reg_op(mt->ctx, ext),
        MIR_new_reg_op(mt->ctx, val_reg)));
    return ext;
}

// Box bool -> Item (inline b2it)
static MIR_reg_t emit_box_bool(MirTranspiler* mt, MIR_reg_t val_reg) {
    // Bool can be BOOL_FALSE(0), BOOL_TRUE(1), or BOOL_ERROR(2)
    // Zero-extend from 8 bits: runtime functions return uint8_t Bool, but MIR
    // reads full i64 from the return register — upper bits may contain garbage.
    MIR_reg_t masked = new_reg(mt, "bmask", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_UEXT8, MIR_new_reg_op(mt->ctx, masked),
        MIR_new_reg_op(mt->ctx, val_reg)));
    // If val >= BOOL_ERROR (2), return ITEM_ERROR instead of boxing as bool
    MIR_reg_t is_error = new_reg(mt, "berr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, is_error),
        MIR_new_reg_op(mt->ctx, masked), MIR_new_int_op(mt->ctx, 2)));
    MIR_label_t l_error = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    MIR_reg_t result = new_reg(mt, "boxb", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_error),
        MIR_new_reg_op(mt->ctx, is_error)));
    // Normal bool: box as (BOOL_TAG | val)
    uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG), MIR_new_reg_op(mt->ctx, masked)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    // Error case: return ITEM_ERROR
    emit_label(mt, l_error);
    uint64_t ERROR_TAG = (uint64_t)LMD_TYPE_ERROR << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ERROR_TAG)));
    emit_label(mt, l_end);
    return result;
}

static void emit_return_item_error_if_zero(MirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t is_zero = new_reg(mt, "is_null_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_zero),
        MIR_new_reg_op(mt->ctx, ptr_reg), MIR_new_int_op(mt->ctx, 0)));
    MIR_label_t l_ok = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_ok),
        MIR_new_reg_op(mt->ctx, is_zero)));
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    MIR_reg_t err = new_reg(mt, "coerce_err", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, err),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    emit_function_error_return(mt, err);
    emit_label(mt, l_ok);
}

static void emit_return_if_item_error(MirTranspiler* mt, MIR_reg_t item_reg) {
    MIR_reg_t type_id = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg)));
    MIR_reg_t is_error = new_reg(mt, "stmt_err", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_error),
        MIR_new_reg_op(mt->ctx, type_id), MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));
    MIR_label_t l_ok = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_ok),
        MIR_new_reg_op(mt->ctx, is_error)));
    transpile_task_scope_unwind(mt, true);
    emit_function_error_return(mt, item_reg);
    emit_label(mt, l_ok);
}

static MIR_reg_t emit_double_bits(MirTranspiler* mt, MIR_reg_t d_reg) {
    // MIR_ALLOCA is dynamic, so using it as a bitcast scratch slot inside hot
    // loops grows the stack per box/unbox. Keep the Item encoding decisions
    // inline, but route the raw bit reinterpretation through a leaf helper.
    return emit_call_1(mt, "lambda_mir_double_bits", MIR_T_I64, MIR_T_D,
        MIR_new_reg_op(mt->ctx, d_reg));
}

static MIR_reg_t emit_bits_double(MirTranspiler* mt, MIR_reg_t bits_reg) {
    // Inline-float Items carry raw double bits; the helper reinterprets them
    // without per-iteration stack allocation in JIT or interpreter mode.
    return emit_call_1(mt, "lambda_mir_bits_double", MIR_T_D, MIR_T_I64,
        MIR_new_reg_op(mt->ctx, bits_reg));
}

// Box float (double) -> Item; the hot in-band arm is inline.
static MIR_reg_t emit_box_float(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t bits = emit_double_bits(mt, val_reg);
    MIR_reg_t in_band = new_reg(mt, "fdmask", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND,
        MIR_new_reg_op(mt->ctx, in_band),
        MIR_new_reg_op(mt->ctx, bits),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_DBL_MASK)));

    MIR_reg_t result = new_reg(mt, "boxf", MIR_T_I64);
    MIR_label_t l_in_band = new_label(mt);
    MIR_label_t l_zero = new_label(mt);
    MIR_label_t l_cold = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, l_in_band),
        MIR_new_reg_op(mt->ctx, in_band)));

    MIR_reg_t is_zero = new_reg(mt, "fzero", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DEQ,
        MIR_new_reg_op(mt->ctx, is_zero),
        MIR_new_reg_op(mt->ctx, val_reg),
        MIR_new_double_op(mt->ctx, 0.0)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
        MIR_new_label_op(mt->ctx, l_zero),
        MIR_new_reg_op(mt->ctx, is_zero)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_cold)));

    emit_label(mt, l_in_band);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, bits)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    emit_label(mt, l_zero);
    MIR_reg_t sign = new_reg(mt, "fsign", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_URSH,
        MIR_new_reg_op(mt->ctx, sign),
        MIR_new_reg_op(mt->ctx, bits),
        MIR_new_int_op(mt->ctx, 63)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR,
        MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_FLOAT_P0),
        MIR_new_reg_op(mt->ctx, sign)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    emit_label(mt, l_cold);
    MIR_reg_t boxed = emit_call_1(mt, "push_d", MIR_T_I64, MIR_T_D,
        MIR_new_reg_op(mt->ctx, val_reg));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, boxed)));

    emit_label(mt, l_end);
    return result;
}

// Load a C string literal pointer into a register.
static MIR_reg_t emit_load_string_literal(MirTranspiler* mt, const char* str) {
    // Generated code outlives temporary StrBuf and stack storage used while lowering.
    // Intern every literal so the embedded host pointer remains valid for the module lifetime.
    const char* stable = name_pool_create_len(mt->name_pool, str, strlen(str))->chars;
    MIR_reg_t r = new_reg(mt, "strp", MIR_T_P);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)stable)));
    return r;
}

// Box int64 -> Item through the full-domain number-stack API.
static MIR_reg_t emit_box_int64(MirTranspiler* mt, MIR_reg_t val_reg) {
    return emit_call_1(mt, "box_int64_value", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box a raw DateTime pointer by copying its value into a GC DateTime object.
// Native literal storage belongs to the compiled module, never to an Item.
static MIR_reg_t emit_box_dtime(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxk", MIR_T_I64);
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    MIR_label_t l_nn = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, val_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_nn);
    MIR_reg_t raw = new_reg(mt, "dtime", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, val_reg, 0, 1)));
    MIR_reg_t boxed = emit_call_1(mt, "push_k", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, raw));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, boxed)));
    emit_label(mt, l_end);
    return result;
}

// Box raw DateTime VALUE -> a GC-owned Item via push_k.
// val_reg is the raw DateTime uint64_t value (NOT a pointer).
static MIR_reg_t emit_box_dtime_value(MirTranspiler* mt, MIR_reg_t val_reg) {
    return emit_call_1(mt, "push_k", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box string pointer -> Item (inline s2it)
// s2it(ptr) = ptr ? (STRING_TAG | (uint64_t)ptr) : ITEM_NULL
static MIR_reg_t emit_box_string(MirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = new_reg(mt, "boxs", MIR_T_I64);
    uint64_t STR_TAG = (uint64_t)LMD_TYPE_STRING << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;

    MIR_label_t l_notnull = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_notnull),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_notnull);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)STR_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_label(mt, l_end);
    return result;
}

// Box symbol pointer -> Item (inline y2it)
static MIR_reg_t emit_box_symbol(MirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = new_reg(mt, "boxy", MIR_T_I64);
    uint64_t SYM_TAG = (uint64_t)LMD_TYPE_SYMBOL << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;

    MIR_label_t l_notnull = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_notnull),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_notnull);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)SYM_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    emit_label(mt, l_end);
    return result;
}

// Box a container pointer (Array*, List*, Map*, Element*, etc.) -> Item
// Containers have their type_id in their first struct field, so just cast ptr to Item
static MIR_reg_t emit_box_container(MirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = new_reg(mt, "boxc", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    return result;
}

// Box Decimal* pointer -> Item (inline dc2it)
// Cannot use push_dc because it takes Decimal struct by value.
static MIR_reg_t emit_box_decimal(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxdc", MIR_T_I64);
    uint64_t DEC_TAG = (uint64_t)LMD_TYPE_DECIMAL << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    MIR_label_t l_nn = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, val_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_nn);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)DEC_TAG), MIR_new_reg_op(mt->ctx, val_reg)));
    emit_label(mt, l_end);
    return result;
}

// Box Binary* pointer -> Item (inline)
static MIR_reg_t emit_box_binary(MirTranspiler* mt, MIR_reg_t val_reg) {
    MIR_reg_t result = new_reg(mt, "boxx", MIR_T_I64);
    uint64_t BIN_TAG = (uint64_t)LMD_TYPE_BINARY << 56;
    uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    MIR_label_t l_nn = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, val_reg)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    emit_label(mt, l_nn);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)BIN_TAG), MIR_new_reg_op(mt->ctx, val_reg)));
    emit_label(mt, l_end);
    return result;
}

// Generic box: given a value register and its TypeId, emit the appropriate boxing
static MIR_reg_t emit_box_impl(MirTranspiler* mt, MIR_reg_t val_reg,
        TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:
        return emit_box_int(mt, val_reg);
    case LMD_TYPE_FLOAT:
        return emit_box_float(mt, val_reg);
    case LMD_TYPE_BOOL:
        return emit_box_bool(mt, val_reg);
    case LMD_TYPE_INT64:
        return emit_box_int64(mt, val_reg);
    case LMD_TYPE_DTIME:
        return val_reg;
    case LMD_TYPE_STRING:
        return emit_box_string(mt, val_reg);
    case LMD_TYPE_SYMBOL:
        return emit_box_symbol(mt, val_reg);
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: case LMD_TYPE_RANGE: case LMD_TYPE_FUNC:
    case LMD_TYPE_TYPE: case LMD_TYPE_PATH: case LMD_TYPE_VMAP:
        return emit_box_container(mt, val_reg);
    case LMD_TYPE_DECIMAL:
        return emit_box_decimal(mt, val_reg);
    case LMD_TYPE_BINARY:
        return emit_box_binary(mt, val_reg);
    case LMD_TYPE_NULL:
    case LMD_TYPE_ANY:
    case LMD_TYPE_ERROR:
    case LMD_TYPE_NUM_SIZED:  // already packed as Item (type_id + sub_type + value)
    case LMD_TYPE_UINT64:     // tagged pointer, already boxed
    default:
        // Already boxed Item or NULL
        return val_reg;
    }
}

static Type* mir_unwrap_decl_type(Type* type) {
    if (type && type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_SIMPLE) {
        TypeType* tt = (TypeType*)type;
        if (tt->type) return tt->type;
    }
    return type;
}

static TypeId mir_decl_type_id(Type* type) {
    Type* unwrapped = mir_unwrap_decl_type(type);
    return unwrapped ? unwrapped->type_id : LMD_TYPE_ANY;
}

static MIR_reg_t emit_coerce_boxed_to_declared(MirTranspiler* mt, MIR_reg_t boxed, Type* declared_type) {
    Type* target = mir_unwrap_decl_type(declared_type);
    TypeId target_tid = target ? target->type_id : LMD_TYPE_ANY;
    if (target_tid == LMD_TYPE_NUM_SIZED) {
        NumSizedType num_type = type_num_sized_kind(target);
        return emit_call_2(mt, "coerce_num_sized", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)num_type));
    }
    if (target_tid == LMD_TYPE_UINT64) {
        return emit_call_1(mt, "coerce_uint64", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    return boxed;
}

static MIR_reg_t emit_coerce_value_to_declared(MirTranspiler* mt, MIR_reg_t val,
                                               TypeId val_tid, Type* declared_type) {
    TypeId target_tid = mir_decl_type_id(declared_type);
    if (target_tid == LMD_TYPE_NUM_SIZED || target_tid == LMD_TYPE_UINT64) {
        MIR_reg_t boxed = emit_box(mt, val, val_tid);
        return emit_coerce_boxed_to_declared(mt, boxed, declared_type);
    }
    return val;
}

static MIR_reg_t emit_bitwise_i64_arg(MirTranspiler* mt, MIR_reg_t val, TypeId tid) {
    if (is_integer_type_id(tid)) return val;
    MIR_reg_t boxed = emit_box(mt, val, tid);
    return emit_call_1(mt, "_barg", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
}

// Unbox boxed Item -> container pointer by stripping the type tag (upper 8 bits)
// Container types embed their TypeId in the struct's first field, so the tag bits
// from boxing must be cleared to recover the raw pointer.
static MIR_reg_t emit_unbox_container(MirTranspiler* mt, MIR_reg_t item_reg) {
    MIR_reg_t ptr = new_reg(mt, "unboxc", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, ptr),
        MIR_new_reg_op(mt->ctx, item_reg),
        MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
    return ptr;
}

// Unbox boxed Item -> raw int64_t by stripping the type tag (upper 8 bits)
static MIR_reg_t emit_unbox_int_mask(MirTranspiler* mt, MIR_reg_t item_reg) {
    MIR_reg_t val = new_reg(mt, "unboxi", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, val),
        MIR_new_reg_op(mt->ctx, item_reg),
        MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
    return val;
}

// Unbox Item -> native type
static MIR_reg_t emit_unbox(MirTranspiler* mt, MIR_reg_t item_reg, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:
        return emit_call_1(mt, "it2i", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_FLOAT:
        {
            MIR_reg_t in_band = new_reg(mt, "fumask", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND,
                MIR_new_reg_op(mt->ctx, in_band),
                MIR_new_reg_op(mt->ctx, item_reg),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_DBL_MASK)));
            MIR_reg_t result = new_reg(mt, "unboxf", MIR_T_D);
            MIR_label_t l_inline = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, l_inline),
                MIR_new_reg_op(mt->ctx, in_band)));
            MIR_reg_t cold = emit_call_1(mt, "it2d", MIR_T_D, MIR_T_I64,
                MIR_new_reg_op(mt->ctx, item_reg));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, cold)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
            emit_label(mt, l_inline);
            MIR_reg_t inline_d = emit_bits_double(mt, item_reg);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, inline_d)));
            emit_label(mt, l_end);
            return result;
        }
    case LMD_TYPE_BOOL:
        return emit_uext8(mt, emit_call_1(mt, "it2b", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg)));
    case LMD_TYPE_STRING:
        return emit_call_1(mt, "it2s", MIR_T_P, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    case LMD_TYPE_INT64:
        return emit_call_1(mt, "it2l", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, item_reg));
    // Container types: strip upper 8 tag bits to recover raw pointer
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: case LMD_TYPE_RANGE:
    case LMD_TYPE_FUNC: case LMD_TYPE_TYPE: case LMD_TYPE_PATH: case LMD_TYPE_VMAP:
        return emit_unbox_container(mt, item_reg);
    default:
        return item_reg;
    }
}

static ValueRep lambda_value_rep(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_FLOAT:
        return VALUE_REP_F64;
    case LMD_TYPE_INT:
    case LMD_TYPE_BOOL:
    case LMD_TYPE_INT64:
        return VALUE_REP_I64;
    case LMD_TYPE_DTIME:
        return VALUE_REP_ITEM;
    case LMD_TYPE_STRING:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_BINARY:
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_PATH:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_MAP:
    case LMD_TYPE_VMAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:
    case LMD_TYPE_TYPE:
    case LMD_TYPE_FUNC:
        // These I64 carriers hold raw managed pointers and still need boxing.
        return VALUE_REP_RAW_GC_POINTER;
    default:
        return VALUE_REP_ITEM;
    }
}

static MirValue lambda_convert_rep(void* owner, MirValue value,
        ValueRep required) {
    MirTranspiler* mt = (MirTranspiler*)owner;
    MIR_reg_t reg = 0;
    if (required == VALUE_REP_ITEM) {
        reg = emit_box_impl(mt, value.reg, value.semantic_type);
    } else if (value.rep == VALUE_REP_ITEM) {
        reg = emit_unbox(mt, value.reg, value.semantic_type);
    }
    if (!reg) return value;
    MIR_type_t mir_type = required == VALUE_REP_F64 ? MIR_T_D
        : required == VALUE_REP_RAW_GC_POINTER ||
            required == VALUE_REP_RAW_NON_GC_POINTER ? MIR_T_P : MIR_T_I64;
    JitValueClass value_class = required == VALUE_REP_ITEM
        ? JIT_VALUE_BOXED_ITEM
        : required == VALUE_REP_RAW_GC_POINTER
            ? JIT_VALUE_RAW_GC_POINTER : JIT_VALUE_NON_GC_SCALAR;
    MirValue converted = em_value(reg, mir_type, value.semantic_type,
        required, value_class);
    converted.scalar_home_id = em_scalar_home_for_reg(&mt->em, reg);
    converted.scalar_provenance = converted.scalar_home_id
        ? SCALAR_PROVENANCE_ACTIVATION_HOME : SCALAR_PROVENANCE_NONE;
    return converted;
}

static MIR_reg_t emit_box(MirTranspiler* mt, MIR_reg_t val_reg,
        TypeId type_id) {
    ValueRep actual = lambda_value_rep(type_id);
    MIR_type_t mir_type = actual == VALUE_REP_F64 ? MIR_T_D
        : actual == VALUE_REP_RAW_GC_POINTER ? MIR_T_P : MIR_T_I64;
    MirValue value = em_value(val_reg, mir_type, type_id, actual,
        actual == VALUE_REP_ITEM ? JIT_VALUE_BOXED_ITEM
        : actual == VALUE_REP_RAW_GC_POINTER ? JIT_VALUE_RAW_GC_POINTER
        : JIT_VALUE_NON_GC_SCALAR);
    return em_require_rep(&mt->em, value, VALUE_REP_ITEM).reg;
}

static void async_store_var(MirTranspiler* mt, MirVarEntry* var) {
    if (!mt || !var || !mt->in_async_proc || !mt->async_frame_reg ||
            var->async_slot < 0) return;
    bool saved_suppressed = mt->async_tracking_suppressed;
    mt->async_tracking_suppressed = true;
    MIR_reg_t boxed = emit_box(mt, var->reg, var->type_id);
    emit_call_void_3(mt, "lambda_async_frame_set",
        MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, var->async_slot),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    mt->async_tracking_suppressed = saved_suppressed;
}

static void async_restore_vars(MirTranspiler* mt) {
    if (!mt || !mt->in_async_proc || !mt->async_frame_reg) return;
    bool saved_suppressed = mt->async_tracking_suppressed;
    mt->async_tracking_suppressed = true;
    for (int scope = 0; scope <= mt->scope_depth; scope++) {
        if (!mt->var_scopes[scope]) continue;
        size_t iter = 0;
        void* item = NULL;
        while (hashmap_iter(mt->var_scopes[scope], &iter, &item)) {
            VarScopeEntry* entry = (VarScopeEntry*)item;
            MirVarEntry* var = &entry->var;
            if (var->async_slot < 0) continue;
            MIR_reg_t saved = emit_call_2(mt, "lambda_async_frame_get", MIR_T_I64,
                MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, var->async_slot));
            MIR_reg_t restored = var->type_id == LMD_TYPE_ANY ||
                var->type_id == LMD_TYPE_NULL || var->type_id == LMD_TYPE_ERROR ||
                var->type_id == LMD_TYPE_NUM_SIZED || var->type_id == LMD_TYPE_UINT64
                ? saved : emit_unbox(mt, saved, var->type_id);
            emit_insn(mt, MIR_new_insn(mt->ctx,
                var->mir_type == MIR_T_D ? MIR_DMOV : MIR_MOV,
                MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, restored)));
            update_gc_root_slot(mt, var);
        }
    }
    mt->async_tracking_suppressed = saved_suppressed;
}

static void async_complete_frame(MirTranspiler* mt) {
    if (!mt || !mt->in_async_proc || !mt->async_frame_reg) return;
    emit_call_void_1(mt, "lambda_async_frame_complete",
        MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg));
}

static void finalize_async_frame_enter(MirTranspiler* mt) {
    if (!mt || !mt->in_async_proc || !mt->async_frame_reg) return;
    MIR_var_t arg = {MIR_T_I64, "slots", 0};
    MirImportEntry* imported = ensure_import(mt, "lambda_async_frame_enter_current",
        MIR_T_P, 1, &arg, 1);
    MIR_insn_t enter = MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, imported->proto),
        MIR_new_ref_op(mt->ctx, imported->import),
        MIR_new_reg_op(mt->ctx, mt->async_frame_reg),
        MIR_new_int_op(mt->ctx, mt->async_next_slot));
    // async_next_slot is final only after lowering. Insert the exact-size frame
    // allocation into the prologue once every named local and temporary is known.
    MIR_insert_insn_before(mt->ctx, mt->em.func_item, mt->em.frame.anchor, enter);
}

static void async_save_spills(MirTranspiler* mt, int count) {
    if (!mt || !mt->in_async_proc || !mt->async_frame_reg) return;
    if (count > mt->async_spill_count) count = mt->async_spill_count;
    bool saved_suppressed = mt->async_tracking_suppressed;
    mt->async_tracking_suppressed = true;
    for (int i = 0; i < count; i++) {
        AsyncRegSpill* spill = &mt->async_spills[i];
        MIR_reg_t bits = spill->reg;
        const char* setter = "lambda_async_frame_set_raw";
        if (spill->mir_type == MIR_T_D) {
            bits = emit_box_float(mt, spill->reg);
            setter = "lambda_async_frame_set";
        }
        emit_call_void_3(mt, setter,
            MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, spill->slot),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, bits));
    }
    mt->async_tracking_suppressed = saved_suppressed;
}

static void async_restore_spills(MirTranspiler* mt, int count) {
    if (!mt || !mt->in_async_proc || !mt->async_frame_reg) return;
    if (count > mt->async_spill_count) count = mt->async_spill_count;
    bool saved_suppressed = mt->async_tracking_suppressed;
    mt->async_tracking_suppressed = true;
    for (int i = 0; i < count; i++) {
        AsyncRegSpill* spill = &mt->async_spills[i];
        const char* getter = spill->mir_type == MIR_T_D
            ? "lambda_async_frame_get" : "lambda_async_frame_get_raw";
        MIR_reg_t saved = emit_call_2(mt, getter, MIR_T_I64,
            MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, spill->slot));
        MIR_reg_t value = spill->mir_type == MIR_T_D
            ? emit_unbox(mt, saved, LMD_TYPE_FLOAT) : saved;
        emit_insn(mt, MIR_new_insn(mt->ctx,
            spill->mir_type == MIR_T_D ? MIR_DMOV : MIR_MOV,
            MIR_new_reg_op(mt->ctx, spill->reg),
            MIR_new_reg_op(mt->ctx, value)));
    }
    mt->async_tracking_suppressed = saved_suppressed;
}

static void async_emit_suspended_return(
    MirTranspiler* mt, MIR_reg_t result, int state, int spill_count) {
    MIR_reg_t suspended = new_reg(mt, "async_suspended", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ,
        MIR_new_reg_op(mt->ctx, suspended),
        MIR_new_reg_op(mt->ctx, result),
        MIR_new_uint_op(mt->ctx, ITEM_TASK_SUSPENDED)));
    MIR_label_t ready = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, ready), MIR_new_reg_op(mt->ctx, suspended)));
    async_save_spills(mt, spill_count);
    emit_call_void_2(mt, "lambda_async_frame_set_state",
        MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, state));
    log_debug("concurrency MIR: emitted park/resume state %d", state);
    emit_function_return(mt, MIR_new_uint_op(mt->ctx, ITEM_TASK_SUSPENDED));
    emit_label(mt, ready);
}

static void async_emit_invoke_resume_point(MirTranspiler* mt, AstCallNode* call) {
    if (!mt || call != mt->async_call_target || mt->async_call_state <= 0 ||
            mt->async_call_resume_emitted) return;
    int state = mt->async_call_state;
    if (!mt->async_state_labels || state > mt->async_state_count) return;

    // The resume label belongs after callee and argument evaluation. Saving the
    // prepared registers here prevents side effects in those expressions from
    // running again when the awaited invocation is retried.
    mt->async_call_spill_count = mt->async_spill_count;
    MIR_label_t invoke = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, invoke)));
    emit_label(mt, mt->async_state_labels[state]);
    async_restore_spills(mt, mt->async_call_spill_count);
    async_restore_vars(mt);
    emit_label(mt, invoke);
    mt->async_call_resume_emitted = true;
}

static void transpile_task_scope_unwind_to(
    MirTranspiler* mt, MIR_reg_t scope_base, bool error_exit) {
    if (!mt->in_async_proc || !scope_base) return;
    int state = ++mt->async_next_state;
    if (state > mt->async_state_count || !mt->async_state_labels) {
        log_error("concurrency MIR: unwind-state count mismatch at state %d/%d",
            state, mt->async_state_count);
        return;
    }
    int spill_count = mt->async_spill_count;
    MIR_label_t invoke = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, invoke)));
    emit_label(mt, mt->async_state_labels[state]);
    async_restore_spills(mt, spill_count);
    async_restore_vars(mt);
    emit_label(mt, invoke);
    MIR_reg_t result = emit_call_2(mt, "lambda_task_scope_unwind", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, scope_base),
        MIR_T_I64, MIR_new_int_op(mt->ctx, error_exit ? 1 : 0));
    async_emit_suspended_return(mt, result, state, spill_count);
}

static void transpile_task_scope_unwind(MirTranspiler* mt, bool error_exit) {
    transpile_task_scope_unwind_to(mt, mt->async_scope_base_reg, error_exit);
}

// ============================================================================
// Phase 3: Direct Map/Struct Field Access helpers
// ============================================================================

// Check if a type is a container type whose runtime storage format is a raw pointer
// (not a tagged Item). The runtime's map_field_store stores raw Container* for these
// types, so MIR direct read must re-tag and direct write must strip the tag.
static bool mir_is_container_field_type(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE: case LMD_TYPE_TYPE: case LMD_TYPE_FUNC:
    case LMD_TYPE_PATH:
        return true;
    default:
        return false;
    }
}

// ============================================================================
// Global variable BSS access helpers
// ============================================================================

// Load a global variable from BSS storage into a register
static MIR_reg_t load_global_var(MirTranspiler* mt, GlobalVarEntry* gvar) {
    // Get BSS address
    MIR_reg_t addr = new_reg(mt, "gv_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, addr),
        MIR_new_ref_op(mt->ctx, gvar->bss_item)));
    // Load boxed Item from BSS
    MIR_reg_t boxed = new_reg(mt, "gv_val", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, boxed),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1)));
    // Unbox to native type if needed
    TypeId tid = gvar->type_id;
    if (is_native_numeric_or_bool_type_id(tid) || tid == LMD_TYPE_STRING) {
        return emit_unbox(mt, boxed, tid);
    }
    return boxed;
}

// Store a value to a global variable's BSS storage
static void store_global_var(MirTranspiler* mt, GlobalVarEntry* gvar, MIR_reg_t val, TypeId val_tid) {
    // Box the value first
    MIR_reg_t boxed = emit_box(mt, val, val_tid);
    // Get BSS address
    MIR_reg_t addr = new_reg(mt, "gv_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, addr),
        MIR_new_ref_op(mt->ctx, gvar->bss_item)));
    // Store boxed Item to BSS
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr, 0, 1),
        MIR_new_reg_op(mt->ctx, boxed)));
}

// ============================================================================
// Literal value extraction from source text
// ============================================================================

static int64_t parse_int_literal(const char* source, TSNode node) {
    int start = ts_node_start_byte(node);
    int end = ts_node_end_byte(node);
    const char* text = source + start;
    int len = end - start;

    // Copy to null-terminated buffer
    char buf[128];
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';

    // Handle hex (0x), octal (0o), binary (0b)
    if (len > 2 && buf[0] == '0') {
        if (buf[1] == 'x' || buf[1] == 'X') return strtoll(buf, NULL, 16);
        if (buf[1] == 'o' || buf[1] == 'O') return strtoll(buf + 2, NULL, 8);
        if (buf[1] == 'b' || buf[1] == 'B') return strtoll(buf + 2, NULL, 2);
    }

    // Remove underscores (1_000_000 -> 1000000)
    char clean[128];
    int ci = 0;
    for (int i = 0; i < len && ci < (int)sizeof(clean) - 1; i++) {
        if (buf[i] != '_') clean[ci++] = buf[i];
    }
    clean[ci] = '\0';

    return strtoll(clean, NULL, 10);
}

static bool parse_bool_literal(const char* source, TSNode node) {
    int start = ts_node_start_byte(node);
    return source[start] == 't';
}

// ============================================================================
// Load constant from rt->consts[index]
// ============================================================================

static MIR_reg_t emit_load_const(MirTranspiler* mt, int const_index, MIR_type_t as_type) {
    mt->em.consts_reg = mt->consts_reg;
    MIR_reg_t ptr = em_load_const(&mt->em, const_index, as_type);
    mt->consts_reg = mt->em.consts_reg;
    return ptr;
}

static MIR_reg_t emit_load_module_type_list(MirTranspiler* mt) {
    MIR_reg_t type_list = new_reg(mt, "type_list", MIR_T_P);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, type_list),
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)mt->type_list)));
    return type_list;
}

static MIR_reg_t emit_load_module_consts(MirTranspiler* mt) {
    mt->em.consts_bss = mt->consts_bss;
    MIR_reg_t consts = em_load_consts_from_bss(&mt->em);
    mt->consts_reg = consts;
    return consts;
}

// Load const and box as Item based on TypeId
static MIR_reg_t emit_load_const_boxed(MirTranspiler* mt, int const_index, TypeId type_id) {
    MIR_reg_t ptr = emit_load_const(mt, const_index, MIR_T_P);
    switch (type_id) {
    case LMD_TYPE_STRING:
        return emit_box_string(mt, ptr);
    case LMD_TYPE_SYMBOL:
        return emit_box_symbol(mt, ptr);
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        // d2it(ptr) = ptr ? (FLOAT_TAG | (uint64_t)ptr) : ITEM_NULL
        MIR_reg_t result = new_reg(mt, "boxd", MIR_T_I64);
        // f64 is a source-level alias; any legacy path that reaches here still
        // emits the canonical runtime float tag.
        uint64_t FLOAT_TAG = (uint64_t)LMD_TYPE_FLOAT << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)FLOAT_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    case LMD_TYPE_INT64: {
        // l2it(ptr) = ptr ? (INT64_TAG | (uint64_t)ptr) : ITEM_NULL
        MIR_reg_t result = new_reg(mt, "boxl", MIR_T_I64);
        uint64_t INT64_TAG = (uint64_t)LMD_TYPE_INT64 << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)INT64_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    case LMD_TYPE_DTIME:
        return emit_box_dtime(mt, ptr);
    case LMD_TYPE_DECIMAL: {
        MIR_reg_t result = new_reg(mt, "boxdc", MIR_T_I64);
        uint64_t DEC_TAG = (uint64_t)LMD_TYPE_DECIMAL << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)DEC_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    case LMD_TYPE_BINARY: {
        MIR_reg_t result = new_reg(mt, "boxx", MIR_T_I64);
        uint64_t BIN_TAG = (uint64_t)LMD_TYPE_BINARY << 56;
        uint64_t ITEM_NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_label_t l_nn = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
            MIR_new_reg_op(mt->ctx, ptr)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_nn);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)BIN_TAG), MIR_new_reg_op(mt->ctx, ptr)));
        emit_label(mt, l_end);
        return result;
    }
    default:
        // Direct cast for containers
        return ptr;
    }
}

static bool static_const_item_from_node(MirTranspiler* mt, AstNode* node, Item* out);

static int append_static_container_const(MirTranspiler* mt, void* ptr) {
    mt->em.const_list = mt->const_list;
    return em_add_const(&mt->em, ptr);
}

static bool static_store_field_value(void* field_ptr, TypeId field_type, Item value) {
    if (!field_ptr) return false;
    TypeId value_type = get_type_id(value);
    switch (field_type) {
    case LMD_TYPE_NULL:
        if (value_type == LMD_TYPE_NULL) { *(void**)field_ptr = NULL; return true; }
        *(Item*)field_ptr = value; return true;
    case LMD_TYPE_BOOL:
        if (value_type != LMD_TYPE_BOOL) return false;
        *(bool*)field_ptr = value.bool_val; return true;
    case LMD_TYPE_INT:
        if (value_type != LMD_TYPE_INT) return false;
        *(int64_t*)field_ptr = value.get_int56(); return true;
    case LMD_TYPE_INT64:
        if (value_type == LMD_TYPE_INT) { *(int64_t*)field_ptr = value.get_int56(); return true; }
        if (value_type == LMD_TYPE_INT64) { *(int64_t*)field_ptr = value.get_int64(); return true; }
        return false;
    case LMD_TYPE_FLOAT:
        if (value_type == LMD_TYPE_FLOAT) { *(double*)field_ptr = value.get_double(); return true; }
        if (value_type == LMD_TYPE_INT) { *(double*)field_ptr = (double)value.get_int56(); return true; }
        if (value_type == LMD_TYPE_INT64) { *(double*)field_ptr = (double)value.get_int64(); return true; }
        return false;
    case LMD_TYPE_DTIME:
        if (value_type != LMD_TYPE_DTIME) return false;
        *(DateTime**)field_ptr = value.get_datetime_ptr(); return true;
    case LMD_TYPE_STRING:
        if (value_type != LMD_TYPE_STRING) return false;
        *(String**)field_ptr = value.get_safe_string(); return true;
    case LMD_TYPE_SYMBOL:
        if (value_type != LMD_TYPE_SYMBOL) return false;
        *(Symbol**)field_ptr = value.get_safe_symbol(); return true;
    case LMD_TYPE_BINARY:
        if (value_type != LMD_TYPE_BINARY) return false;
        *(Binary**)field_ptr = value.get_safe_binary(); return true;
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:
        if (value_type < LMD_TYPE_RANGE || value_type > LMD_TYPE_OBJECT) return false;
        *(Container**)field_ptr = value.container; return true;
    case LMD_TYPE_DECIMAL:
        if (value_type != LMD_TYPE_DECIMAL) return false;
        *(Decimal**)field_ptr = value.get_decimal();
        return true;
    case LMD_TYPE_TYPE:
        if (value_type != LMD_TYPE_TYPE) return false;
        *(Type**)field_ptr = value.type;
        return true;
    case LMD_TYPE_FUNC:
        if (value_type != LMD_TYPE_FUNC) return false;
        *(Function**)field_ptr = value.function;
        return true;
    case LMD_TYPE_PATH:
        if (value_type != LMD_TYPE_PATH) return false;
        *(Path**)field_ptr = value.path;
        return true;
    case LMD_TYPE_UINT64:
        if (field_type != value_type) return false;
        *(uint64_t*)field_ptr = value.get_uint64();
        return true;
    case LMD_TYPE_NUM_SIZED:
        if (value_type != LMD_TYPE_NUM_SIZED) return false;
        *(uint64_t*)field_ptr = value.item; return true;
    case LMD_TYPE_ANY: {
        TypedItem titem;
        memset(&titem, 0, sizeof(titem));
        titem.type_id = value_type;
        titem.item = value.item;
        switch (value_type) {
        case LMD_TYPE_NULL: case LMD_TYPE_UNDEFINED: case LMD_TYPE_ERROR: break;
        case LMD_TYPE_BOOL: titem.bool_val = value.bool_val; break;
        case LMD_TYPE_INT: titem.int_val = value.int_val; break;
        case LMD_TYPE_INT64: titem.long_val = value.get_int64(); break;
        case LMD_TYPE_FLOAT: titem.double_val = value.get_double(); break;
        case LMD_TYPE_DTIME: titem.datetime_ptr = value.get_datetime_ptr(); break;
        case LMD_TYPE_UINT64: titem.uint64_val = value.get_uint64(); break;
        case LMD_TYPE_STRING: titem.string = value.get_safe_string(); break;
        case LMD_TYPE_SYMBOL: titem.symbol = value.get_safe_symbol(); break;
        case LMD_TYPE_BINARY: titem.binary = value.get_safe_binary(); break;
        case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_RANGE:
        case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
            titem.container = value.container; break;
        case LMD_TYPE_DECIMAL: titem.decimal = value.get_decimal(); break;
        case LMD_TYPE_TYPE: titem.type = value.type; break;
        case LMD_TYPE_FUNC: titem.function = value.function; break;
        case LMD_TYPE_PATH: titem.path = value.path; break;
        default: return false;
        }
        *(TypedItem*)field_ptr = titem;
        return true;
    }
    default:
        return false;
    }
}

static bool static_const_array_from_node(MirTranspiler* mt, AstArrayNode* arr_node, Item* out) {
    if (!mt || !arr_node || !out || !mt->script_pool) return false;
    TypeArray* arr_type = (TypeArray*)arr_node->type;
    if (arr_type && arr_type->nested && arr_type->nested->type_id == LMD_TYPE_BOOL) return false;
    int64_t count = 0;
    for (AstNode* item = arr_node->item; item; item = item->next) {
        if (item->node_type == AST_NODE_FOR_EXPR || item->node_type == AST_NODE_SPREAD ||
            item->node_type == AST_NODE_PIPE || item->node_type == AST_NODE_ASSIGN) return false;
        AstNode* value = item;
        if (value->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)value;
            if (pri->expr) value = pri->expr;
        }
        if (value->node_type == AST_NODE_ARRAY) return false;
        count++;
    }
    Array* arr = (Array*)pool_calloc(mt->script_pool, sizeof(Array));
    if (!arr) return false;
    arr->type_id = LMD_TYPE_ARRAY;
    arr->is_static = 1;
    arr->is_immortal = 1;
    arr->length = count;
    arr->capacity = count;
    if (count > 0) {
        arr->items = (Item*)pool_calloc(mt->script_pool, sizeof(Item) * count);
        if (!arr->items) return false;
    }
    int64_t index = 0;
    for (AstNode* item = arr_node->item; item; item = item->next) {
        Item value = ItemNull;
        if (!static_const_item_from_node(mt, item, &value)) return false;
        arr->items[index++] = value;
    }
    out->item = (uint64_t)(uintptr_t)arr;
    return true;
}

static bool static_const_map_from_node(MirTranspiler* mt, AstMapNode* map_node, Item* out) {
    if (!mt || !map_node || !map_node->type || !out || !mt->script_pool) return false;
    TypeMap* map_type = (TypeMap*)map_node->type;
    Map* map = (Map*)pool_calloc(mt->script_pool, sizeof(Map));
    if (!map) return false;
    map->type_id = LMD_TYPE_MAP;
    map->is_static = 1;
    map->is_immortal = 1;
    map->map_kind = MAP_KIND_PLAIN;
    map->type = map_type;
    map->data_cap = (int)map_type->byte_size;
    if (map_type->byte_size > 0) {
        map->data = pool_calloc(mt->script_pool, (size_t)map_type->byte_size);
        if (!map->data) return false;
    }
    AstNode* item = map_node->item;
    ShapeEntry* field = map_type->shape;
    while (item && field) {
        if (item->node_type != AST_NODE_KEY_EXPR || !field->name) return false;
        AstNamedNode* key_expr = (AstNamedNode*)item;
        Item value = ItemNull;
        if (key_expr->as && !static_const_item_from_node(mt, key_expr->as, &value)) return false;
        if (!static_store_field_value((char*)map->data + field->byte_offset,
                field->type->type_id, value)) return false;
        item = item->next;
        field = field->next;
    }
    if (item || field) return false;
    out->item = (uint64_t)(uintptr_t)map;
    return true;
}

static bool static_const_item_from_node(MirTranspiler* mt, AstNode* node, Item* out) {
    if (!mt || !node || !out) return false;
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) return static_const_item_from_node(mt, pri->expr, out);
        if (!node->type || !node->type->is_literal) return false;
        switch (node->type->type_id) {
        case LMD_TYPE_NULL: *out = ItemNull; return true;
        case LMD_TYPE_BOOL: out->item = b2it(parse_bool_literal(mt->source, node->node)); return true;
        case LMD_TYPE_INT: out->item = i2it(parse_int_literal(mt->source, node->node)); return true;
        case LMD_TYPE_INT64: { TypeInt64* t = (TypeInt64*)node->type; out->item = l2it(&t->int64_val); return true; }
        case LMD_TYPE_FLOAT: { TypeFloat* t = (TypeFloat*)node->type; *out = lambda_float_ptr_to_item(&t->double_val); return true; }
        case LMD_TYPE_FLOAT64: { TypeFloat* t = (TypeFloat*)node->type; *out = lambda_float_ptr_to_item(&t->double_val); return true; }
        case LMD_TYPE_DTIME: return false;
        case LMD_TYPE_DECIMAL: { TypeDecimal* t = (TypeDecimal*)node->type; out->item = c2it(t->decimal); return true; }
        case LMD_TYPE_STRING: { TypeString* t = (TypeString*)node->type; out->item = s2it(t->string); return true; }
        case LMD_TYPE_SYMBOL: { TypeString* t = (TypeString*)node->type; out->item = y2it((Symbol*)t->string); return true; }
        case LMD_TYPE_BINARY: { TypeBinaryConst* t = (TypeBinaryConst*)node->type; out->item = x2it(t->binary); return true; }
        case LMD_TYPE_NUM_SIZED: { TypeNumSized* t = (TypeNumSized*)node->type; out->item = NUM_SIZED_PACK(t->num_type, t->raw_bits); return true; }
        case LMD_TYPE_UINT64: { TypeUint64* t = (TypeUint64*)node->type; out->item = u2it(&t->uint64_val); return true; }
        default: return false;
        }
    }
    if (node->node_type == AST_NODE_ARRAY) return static_const_array_from_node(mt, (AstArrayNode*)node, out);
    if (node->node_type == AST_NODE_MAP) return static_const_map_from_node(mt, (AstMapNode*)node, out);
    return false;
}

static bool emit_static_collection_const(MirTranspiler* mt, AstNode* node, MIR_reg_t* out_reg) {
    if (!mt || !node || !out_reg) return false;
    if (mt->in_proc) return false;
    if (node->node_type != AST_NODE_ARRAY && node->node_type != AST_NODE_MAP) return false;
    Item value = ItemNull;
    if (!static_const_item_from_node(mt, node, &value)) return false;
    TypeId tid = get_type_id(value);
    if (tid != LMD_TYPE_ARRAY && tid != LMD_TYPE_MAP) return false;
    int const_index = append_static_container_const(mt, value.container);
    if (const_index < 0) return false;
    *out_reg = emit_load_const(mt, const_index, MIR_T_P);
    return true;
}

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node);
static MIR_reg_t transpile_box_item(MirTranspiler* mt, AstNode* node);
static MIR_reg_t transpile_const_type(MirTranspiler* mt, int type_index);
static void transpile_let_stam(MirTranspiler* mt, AstLetNode* let_node);
static void transpile_func_def(MirTranspiler* mt, AstFuncNode* fn_node);
static bool has_index_mutation(const char* var_name, AstNode* node);  // P4-3.2

// ============================================================================
// Expression transpilation
// ============================================================================

static MIR_reg_t transpile_primary(MirTranspiler* mt, AstPrimaryNode* pri) {
    if (pri->expr) {
        return transpile_expr(mt, pri->expr);
    }

    AstNode* node = (AstNode*)pri;
    if (!node->type) {
        log_error("mir: primary node has null type");
        MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }

    TypeId tid = node->type->type_id;

    if (node->type->is_literal) {
        switch (tid) {
        case LMD_TYPE_INT: {
            int64_t val;
            if (node->type == &LIT_INT || ts_node_symbol(node->node) == SYM_INT) {
                val = parse_int_literal(mt->source, node->node);
            } else {
                TypeConst* tc = (TypeConst*)node->type;
                MIR_reg_t ptr = emit_load_const(mt, tc->const_index, MIR_T_P);
                MIR_reg_t r = new_reg(mt, "intc", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, ptr, 0, 1)));
                return r;
            }
            MIR_reg_t r = new_reg(mt, "int", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, val)));
            return r;
        }
        case LMD_TYPE_FLOAT: {
            // Float literals are stored in const_list, load from there
            TypeConst* tc = (TypeConst*)node->type;
            MIR_reg_t ptr = emit_load_const(mt, tc->const_index, MIR_T_P);
            // Dereference the double* to get the actual double value
            MIR_reg_t r = new_reg(mt, "flt", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_mem_op(mt->ctx, MIR_T_D, 0, ptr, 0, 1)));
            return r;
        }
        case LMD_TYPE_BOOL: {
            bool val = parse_bool_literal(mt->source, node->node);
            MIR_reg_t r = new_reg(mt, "bool", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, val ? 1 : 0)));
            return r;
        }
        case LMD_TYPE_NULL: {
            MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
        case LMD_TYPE_STRING: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_SYMBOL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_INT64: {
            TypeConst* tc = (TypeConst*)node->type;
            MIR_reg_t ptr = emit_load_const(mt, tc->const_index, MIR_T_P);
            // Dereference to get int64_t value
            MIR_reg_t r = new_reg(mt, "i64", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, ptr, 0, 1)));
            return r;
        }
        case LMD_TYPE_DTIME: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_box_dtime(mt, emit_load_const(mt, tc->const_index, MIR_T_P));
        }
        case LMD_TYPE_DECIMAL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_BINARY: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const(mt, tc->const_index, MIR_T_P);
        }
        case LMD_TYPE_NUM_SIZED: {
            // sized numeric: pack inline as a 64-bit immediate
            TypeNumSized* ns = (TypeNumSized*)node->type;
            uint64_t packed = NUM_SIZED_PACK(ns->num_type, ns->raw_bits);
            MIR_reg_t r = new_reg(mt, "nsz", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)packed)));
            return r;
        }
        case LMD_TYPE_UINT64: {
            // A literal must get a transient number home; const-pool storage is
            // neither a stack home nor a destination-owned scalar slot.
            TypeConst* tc = (TypeConst*)node->type;
            MIR_reg_t ptr = emit_load_const(mt, tc->const_index, MIR_T_P);
            MIR_reg_t raw = new_reg(mt, "u64", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, ptr, 0, 1)));
            return emit_call_1(mt, "box_uint64_value", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, raw));
        }
        default: {
            log_error("mir: unhandled literal type %d", tid);
            MIR_reg_t r = new_reg(mt, "unk", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
        }
    }

    // Non-literal primary (shouldn't happen - primaries are either literal or have expr)
    log_error("mir: non-literal primary without expr, type %d", tid);
    MIR_reg_t r = new_reg(mt, "unk", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, 0)));
    return r;
}

static MIR_reg_t transpile_ident(MirTranspiler* mt, AstIdentNode* ident) {
    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)ident->name->len, ident->name->chars);

    // Check if the AST name resolution says this is a function reference FIRST.
    // This must come before variable/global lookup because a local fn declaration
    // should shadow a global variable with the same name (but fn declarations
    // don't create set_var entries — they're compiled as MIR functions).
    if (ident->entry && ident->entry->node) {
        AstNode* entry_node = ident->entry->node;
        if (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_PROC ||
            entry_node->node_type == AST_NODE_FUNC_EXPR) {
            // Only use the function reference path if the function is NOT stored
            // as a captured variable (closures store captured functions as vars)
            MirVarEntry* cap_var = find_var(mt, name_buf);
            if (!cap_var) {
                goto function_reference;
            }
        }
    }

    {
    MirVarEntry* var = find_var(mt, name_buf);
    if (var) {
        if (var->root_slot >= 0 && var->mir_type == MIR_T_I64) {
            return load_gc_root_slot(mt, var->root_slot, "var_live");
        }
        return var->reg;
    }
    }

    // Check global (module-level) variables stored in BSS
    {
    GlobalVarEntry* gvar = find_global_var(mt, name_buf);
    if (gvar) {
        return load_global_var(mt, gvar);
    }
    }

    // Check if this is an imported variable (pub var from another module)
    if (ident->entry && ident->entry->import) {
        AstNode* entry_node = ident->entry->node;
        // Imported pub variables
        if (entry_node && (entry_node->node_type == AST_NODE_ASSIGN ||
            entry_node->node_type == AST_NODE_PARAM)) {
            AstNamedNode* named = (AstNamedNode*)entry_node;
            StrBuf* var_name = strbuf_new_cap(64);
            write_var_name(var_name, named, NULL);

            TypeId var_tid = entry_node->type ? entry_node->type->type_id : LMD_TYPE_ANY;
            log_debug("mir: loading imported variable '%s' type_id=%d", var_name->str, var_tid);

            // MIR Direct BSS always stores a boxed Item (uint64_t), matching store_global_var.
            // Load as int64 then unbox — same pattern as load_global_var.
            MIR_item_t imp_item = MIR_new_import(mt->ctx, var_name->str);
            MIR_reg_t addr_reg = new_reg(mt, "impvar_addr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, addr_reg),
                MIR_new_ref_op(mt->ctx, imp_item)));
            // Load boxed Item (uint64) from BSS
            MIR_reg_t boxed_reg = new_reg(mt, "impvar_boxed", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, boxed_reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, addr_reg, 0, 1)));
            // Unbox to native type if the declared type is a simple scalar
            MIR_reg_t val_reg;
            if (is_native_numeric_or_bool_type_id(var_tid) || var_tid == LMD_TYPE_STRING) {
                val_reg = emit_unbox(mt, boxed_reg, var_tid);
            } else {
                val_reg = boxed_reg;
            }

            strbuf_free(var_name);
            return val_reg;
        }
        // Imported pub object types — resolve via const_type(type_index)
        if (entry_node && entry_node->node_type == AST_NODE_OBJECT_TYPE) {
            AstObjectTypeNode* obj_node = (AstObjectTypeNode*)entry_node;
            if (obj_node->type && obj_node->type->type_id == LMD_TYPE_TYPE) {
                TypeObject* ot = (TypeObject*)((TypeType*)obj_node->type)->type;
                log_debug("mir: resolving imported object type '%.*s' via const_type(%d)",
                    (int)obj_node->name->len, obj_node->name->chars, ot->type_index);
                return transpile_const_type(mt, ot->type_index);
            }
        }
    }

    // Check if this is a reference to a function (for first-class function usage)
    function_reference:
    if (ident->entry && ident->entry->node) {
        AstNode* entry_node = ident->entry->node;
        if (entry_node->node_type == AST_NODE_FUNC || entry_node->node_type == AST_NODE_PROC ||
            entry_node->node_type == AST_NODE_FUNC_EXPR) {
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;

            // Count arity from param list
            int arity = 0;
            AstNamedNode* p = fn_node->param;
            while (p) { arity++; p = (AstNamedNode*)p->next; }

            // Handle imported function references
            if (ident->entry->import) {
                // Get the function name (use boxed wrapper for typed params or inferred native params)
                // Include module index prefix to prevent name collisions across modules
                StrBuf* fn_import_name = strbuf_new_cap(64);
                // Cross-language exports already expose the boxed host ABI;
                // only Lambda-compiled dependencies publish generated _b symbols.
                bool use_wrapper = !ident->entry->import->is_cross_lang;
                if (use_wrapper) {
                    write_fn_name_ex(fn_import_name, fn_node, ident->entry->import, "_b");
                } else {
                    write_fn_name(fn_import_name, fn_node, ident->entry->import);
                }
                log_debug("mir: imported function reference '%s'", fn_import_name->str);

                // Get the imported function address via MIR import
                MIR_item_t imp_item = MIR_new_import(mt->ctx, fn_import_name->str);
                MIR_reg_t fn_addr = new_reg(mt, "imp_fnaddr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, fn_addr),
                    MIR_new_ref_op(mt->ctx, imp_item)));

                // Create Function* via to_fn_named
                const char* fn_display_name = fn_node->name ? fn_node->name->chars : nullptr;
                MIR_reg_t fn_obj;
                if (fn_display_name) {
                    MIR_reg_t name_reg = emit_load_string_literal(mt, fn_display_name);
                    MIR_var_t cv[3] = {{MIR_T_P,"f",0},{MIR_T_I64,"a",0},{MIR_T_P,"n",0}};
                    MirImportEntry* ie = ensure_import(mt, "to_fn_named", MIR_T_P, 3, cv, 1);
                    fn_obj = new_reg(mt, "imp_fn", MIR_T_I64);
                    emit_insn(mt, MIR_new_call_insn(mt->ctx, 6,
                        MIR_new_ref_op(mt->ctx, ie->proto),
                        MIR_new_ref_op(mt->ctx, ie->import),
                        MIR_new_reg_op(mt->ctx, fn_obj),
                        MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_new_int_op(mt->ctx, arity),
                        MIR_new_reg_op(mt->ctx, name_reg)));
                } else {
                    fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arity));
                }
                strbuf_free(fn_import_name);
                return fn_obj;
            }

            // Look up the MIR function item
            StrBuf* nm_buf = strbuf_new_cap(64);
            write_fn_name(nm_buf, fn_node, ident->entry->import);
            MIR_item_t func_item = find_local_func(mt, nm_buf->str);
            if (!func_item) func_item = find_local_func(mt, name_buf);

            StrBuf* wrapper_buf = strbuf_new_cap(64);
            write_fn_name_ex(wrapper_buf, fn_node, ident->entry->import, "_b");
            MIR_item_t wrapper_item = find_local_func(mt, wrapper_buf->str);
            if (wrapper_item) {
                log_debug("mir: fn ref '%s' → using ABI wrapper '%s'",
                    nm_buf->str, wrapper_buf->str);
                func_item = wrapper_item;
            }
            strbuf_free(wrapper_buf);

            if (func_item) {
                // Get function address via MIR ref
                MIR_reg_t fn_addr = new_reg(mt, "fnaddr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_addr),
                    MIR_new_ref_op(mt->ctx, func_item)));

                if (fn_node->captures) {
                    // Closure: allocate and populate env
                    int cap_count = 0;
                    FnCapture* cap = fn_node->captures;
                    while (cap) { cap_count++; cap = cap->next; }

                    MIR_reg_t env_reg = emit_call_2(mt, "heap_calloc", MIR_T_P,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(cap_count * 16)),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

                    int cap_idx = 0;
                    cap = fn_node->captures;
                    while (cap) {
                        char cap_name[128];
                        snprintf(cap_name, sizeof(cap_name), "%s", cap->name);

                        MIR_reg_t cap_val = 0;
                        MirVarEntry* cvar = find_var(mt, cap_name);
                        if (cvar) {
                            cap_val = emit_box(mt, cvar->reg, cvar->type_id);
                        } else {
                            GlobalVarEntry* gvar = find_global_var(mt, cap_name);
                            if (gvar) {
                                cap_val = load_global_var(mt, gvar);
                            } else {
                                log_error("mir: closure capture '%s' not found in scope", cap_name);
                                cap_val = new_reg(mt, "capnull", MIR_T_I64);
                                uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, cap_val),
                                    MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                            }
                        }

                        emit_call_void_4(mt, "owned_item_slot_store",
                            MIR_T_P, MIR_new_reg_op(mt->ctx, env_reg),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, cap_count),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, cap_idx),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, cap_val));

                        cap_idx++;
                        cap = cap->next;
                    }

                    // Create closure: to_closure_named(fn_ptr, arity, env, name)
                    const char* closure_name = fn_node->name ? fn_node->name->chars : nullptr;
                    MIR_reg_t fn_obj;
                    if (closure_name) {
                        MIR_reg_t cn_reg = emit_load_string_literal(mt, closure_name);
                        MIR_var_t cv[4] = {{MIR_T_P,"f",0},{MIR_T_I64,"a",0},{MIR_T_P,"e",0},{MIR_T_P,"n",0}};
                        MirImportEntry* ie = ensure_import(mt, "to_closure_named", MIR_T_P, 4, cv, 1);
                        fn_obj = new_reg(mt, "closure", MIR_T_I64);
                        emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                            MIR_new_ref_op(mt->ctx, ie->proto),
                            MIR_new_ref_op(mt->ctx, ie->import),
                            MIR_new_reg_op(mt->ctx, fn_obj),
                            MIR_new_reg_op(mt->ctx, fn_addr),
                            MIR_new_int_op(mt->ctx, arity),
                            MIR_new_reg_op(mt->ctx, env_reg),
                            MIR_new_reg_op(mt->ctx, cn_reg)));
                    } else {
                        fn_obj = emit_call_3(mt, "to_closure", MIR_T_P,
                            MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, arity),
                            MIR_T_P, MIR_new_reg_op(mt->ctx, env_reg));
                    }

                    strbuf_free(nm_buf);

                    // set closure_field_count (offset 2 in Function struct)
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_U8, 2, fn_obj, 0, 1),
                        MIR_new_int_op(mt->ctx, cap_count)));

                    return fn_obj;
                } else {
                    MIR_reg_t fn_obj;
                    if (fn_node->name && fn_node->name->len > 0) {
                        // identifier-style first-class refs must carry the declared name for name(fn).
                        MIR_reg_t name_reg = emit_load_string_literal(mt, fn_node->name->chars);
                        fn_obj = emit_call_3(mt, "to_fn_named", MIR_T_P,
                            MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, arity),
                            MIR_T_P, MIR_new_reg_op(mt->ctx, name_reg));
                    } else {
                        // plain anonymous function: create Function* via to_fn_n(fn_ptr, arity)
                        fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                            MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, arity));
                    }

                    strbuf_free(nm_buf);
                    return fn_obj;  // Function* is a container
                }
            }
            strbuf_free(nm_buf);
        }
        // Check if this is a reference to a compiled pattern
        else if (entry_node->node_type == AST_NODE_STRING_PATTERN ||
                 entry_node->node_type == AST_NODE_SYMBOL_PATTERN) {
            AstPatternDefNode* pattern_def = (AstPatternDefNode*)entry_node;
            TypePattern* pattern_type = (TypePattern*)pattern_def->type;
            log_debug("mir: pattern reference '%s', index=%d", name_buf, pattern_type->pattern_index);

            // emit const_pattern_with_tl(pattern_index, type_list_ptr) call
            // Use the module-local type_list (loaded from BSS in prologue) so cross-module
            // pattern references use the correct type_list.
            MIR_reg_t pat_reg = emit_call_2(mt, "const_pattern_with_tl", MIR_T_P,
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)pattern_type->pattern_index),
                MIR_T_P, MIR_new_reg_op(mt->ctx, emit_load_module_type_list(mt)));
            return pat_reg;  // TypePattern* used as Item by fn_is etc.
        }
        // Check if this is a local object type reference (e.g., `Point` in `p is Point`)
        else if (entry_node->node_type == AST_NODE_OBJECT_TYPE) {
            AstObjectTypeNode* obj_node = (AstObjectTypeNode*)entry_node;
            if (obj_node->type && obj_node->type->type_id == LMD_TYPE_TYPE) {
                TypeObject* ot = (TypeObject*)((TypeType*)obj_node->type)->type;
                log_debug("mir: resolving local object type '%s' via const_type(%d)",
                    name_buf, ot->type_index);
                return transpile_const_type(mt, ot->type_index);
            }
        }
    }

    log_error("mir: undefined variable '%s'", name_buf);
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    MIR_reg_t r = new_reg(mt, "undef", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    return r;
}

// ============================================================================
// Binary expressions
// ============================================================================

static inline bool is_elementwise_comparison_op(int op) {
    return op >= OPERATOR_ELEM_EQ && op <= OPERATOR_ELEM_GE;
}

static inline int elementwise_cmp_code(int op) {
    return op - OPERATOR_ELEM_EQ;
}

// True iff a keyword comparison `lt OP rt` should produce an ELEM_BOOL mask.
static inline bool comparison_vectorizes(int op, TypeId lt, TypeId rt) {
    if (!is_elementwise_comparison_op(op)) return false;
    bool l_arr = (lt == LMD_TYPE_ARRAY_NUM || lt == LMD_TYPE_ARRAY);
    bool r_arr = (rt == LMD_TYPE_ARRAY_NUM || rt == LMD_TYPE_ARRAY);
    return l_arr || r_arr;
}

static inline bool eq_is_numeric_type(TypeId tid) {
    return is_numeric_type_id(tid);
}

static inline bool eq_is_sequence_type(TypeId tid) {
    return tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_ARRAY_NUM || tid == LMD_TYPE_RANGE;
}

static bool eq_known_cross_family_false(TypeId left_tid, TypeId right_tid) {
    if (left_tid == right_tid) return false;
    if (left_tid == LMD_TYPE_ANY || right_tid == LMD_TYPE_ANY) return false;
    if (left_tid == LMD_TYPE_RAW_POINTER || right_tid == LMD_TYPE_RAW_POINTER) return false;
    if (left_tid == LMD_TYPE_TYPE || right_tid == LMD_TYPE_TYPE) return false;
    if (eq_is_numeric_type(left_tid) && eq_is_numeric_type(right_tid)) return false;
    if (eq_is_sequence_type(left_tid) && eq_is_sequence_type(right_tid)) return false;
    return true;
}

static MIR_reg_t emit_bool_const(MirTranspiler* mt, bool value) {
    MIR_reg_t result = new_reg(mt, value ? "true" : "false", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, value ? 1 : 0)));
    return result;
}

// Get effective runtime type for a node. If the node is an identifier whose
// variable is stored as boxed (ANY) — e.g. optional params — return ANY instead
// of the AST-declared type. This prevents native op dispatch on boxed values.
static TypeId get_effective_type(MirTranspiler* mt, AstNode* node) {
    if (!node) return LMD_TYPE_ANY;
    // Unwrap PRIMARY nodes (parenthesized expressions)
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) return get_effective_type(mt, pri->expr);
    }
    // transpile_match always boxes each arm's body into the result slot, so a match
    // result is a boxed Item regardless of its declared (arm-union) type. Report ANY
    // so callers treat it as a boxed Item (box/unbox correctly) instead of assuming a
    // raw native register — otherwise a match returning ints gets double-boxed.
    if (node->node_type == AST_NODE_MATCH_EXPR) return LMD_TYPE_ANY;
    TypeId tid = node->type ? node->type->type_id : LMD_TYPE_ANY;
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        Type* target_type = call->function ? call->function->type : NULL;
        bool can_raise = target_type && target_type->type_id == LMD_TYPE_FUNC &&
            ((TypeFunc*)target_type)->can_raise;
        if (!can_raise && call->function &&
                call->function->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident = (AstIdentNode*)call->function;
            AstNode* target = ident->entry ? ident->entry->node : NULL;
            if (target && (target->node_type == AST_NODE_FUNC ||
                    target->node_type == AST_NODE_FUNC_EXPR ||
                    target->node_type == AST_NODE_PROC) && target->type) {
                can_raise = ((TypeFunc*)target->type)->can_raise;
            }
        }
        if (can_raise) {
            // A can-raise call must carry either its value or its error in one
            // Item register at expression boundaries, even when its success
            // lane is native. Reporting the declared scalar type here would
            // make let/destructure code box the already-boxed merged result.
            return LMD_TYPE_ANY;
        }
    }
    if (node->node_type == AST_NODE_ARRAY &&
        (tid == LMD_TYPE_NULL || tid == LMD_TYPE_RAW_POINTER || tid == LMD_TYPE_ANY)) {
        return LMD_TYPE_ARRAY;
    }
    if (node->node_type == AST_NODE_LIST) {
        AstListNode* list = (AstListNode*)node;
        if (list->declare) {
            AstNode* scan = list->item;
            AstNode* last_value = NULL;
            int value_count = 0;
            while (scan) {
                value_count++;
                last_value = scan;
                scan = scan->next;
            }
            if (value_count == 1 && last_value) {
                AstNode* unwrapped = last_value;
                while (unwrapped && unwrapped->node_type == AST_NODE_PRIMARY) {
                    unwrapped = ((AstPrimaryNode*)unwrapped)->expr;
                }
                if (unwrapped && unwrapped->node_type == AST_NODE_IDENT) {
                    AstIdentNode* ident = (AstIdentNode*)unwrapped;
                    AstNode* declare = list->declare;
                    while (declare) {
                        if (declare->node_type == AST_NODE_ASSIGN) {
                            AstNamedNode* asn = (AstNamedNode*)declare;
                            if (asn->as && ident->name && asn->name &&
                                ident->name->len == asn->name->len &&
                                memcmp(ident->name->chars, asn->name->chars, ident->name->len) == 0) {
                                // final local identifiers are not in MIR scope yet
                                // during type probing; resolve them through this block's declarations.
                                return get_effective_type(mt, asn->as);
                            }
                        }
                        declare = declare->next;
                    }
                }
                // let-blocks return the last expression directly; callers must
                // use that expression's actual representation, not the stale block type.
                return get_effective_type(mt, last_value);
            }
        }
    }
    // P4-3.1: For index expressions (subscripts), check if the object variable
    // has a known element type from fill() narrowing. This enables native bool
    // paths in AND/OR/NOT for bool array elements.
    // P4-3.2: Check MirVarEntry elem_type and ARRAY_INT var type for INDEX_EXPR.
    // elem_type is set by fill() narrowing (P4-3.1) or pre-pass mutation analysis (P4-3.2).
    // Only returns native types when provably safe (explicitly set elem_type).
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* fn = (AstFieldNode*)node;
        // Guard: if the index is a TYPE expression (child query like arr[int]),
        // the result is a spreadable array, NOT a single element. Skip elem_type opt.
        AstNode* idx_unwrapped = fn->field;
        while (idx_unwrapped && idx_unwrapped->node_type == AST_NODE_PRIMARY)
            idx_unwrapped = ((AstPrimaryNode*)idx_unwrapped)->expr;
        bool idx_is_type = false;
        if (idx_unwrapped) {
            if (idx_unwrapped->node_type == AST_NODE_TYPE ||
                idx_unwrapped->node_type == AST_NODE_ARRAY_TYPE ||
                idx_unwrapped->node_type == AST_NODE_LIST_TYPE ||
                idx_unwrapped->node_type == AST_NODE_MAP_TYPE ||
                idx_unwrapped->node_type == AST_NODE_ELMT_TYPE ||
                idx_unwrapped->node_type == AST_NODE_FUNC_TYPE ||
                idx_unwrapped->node_type == AST_NODE_BINARY_TYPE ||
                idx_unwrapped->node_type == AST_NODE_UNARY_TYPE ||
                idx_unwrapped->node_type == AST_NODE_CONTENT_TYPE)
                idx_is_type = true;
            if (!idx_is_type && idx_unwrapped->type && idx_unwrapped->type->type_id == LMD_TYPE_TYPE)
                idx_is_type = true;
        }
        // Boolean mask index a[mask] returns an ARRAY, not a scalar element — skip
        // the elem_type optimization. (A multi-dim a[i,j] has an integer first
        // index, so an ARRAY_NUM index uniquely identifies a mask.)
        TypeId field_eff = fn->field ? get_effective_type(mt, fn->field) : LMD_TYPE_ANY;
        bool idx_is_mask = (field_eff == LMD_TYPE_ARRAY_NUM || field_eff == LMD_TYPE_ARRAY);
        if (!idx_is_type && !idx_is_mask) {
            // Unwrap PRIMARY around the object to find the IDENT
            AstNode* obj_unwrapped = fn->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov && ov->elem_type != LMD_TYPE_ANY) return ov->elem_type;
            }
        }
    }
    // For identifiers: reconcile AST type with actual variable storage.
    // Two cases: (1) AST says typed but var stored as ANY → downgrade to ANY
    //            (2) AST says ANY but var narrowed by inference → upgrade to narrowed type
    if (node->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)node;
        char name[128];
        snprintf(name, sizeof(name), "%.*s", (int)ident->name->len, ident->name->chars);
        MirVarEntry* v = find_var(mt, name);
        if (v) {
            if (v->type_id == LMD_TYPE_ANY) return LMD_TYPE_ANY;
            // Narrowed: var has concrete type (from param inference or typed init)
            if (tid == LMD_TYPE_ANY && v->type_id != LMD_TYPE_ANY) return v->type_id;
            // Type annotation meta-type (e.g. `arr: int[]` produces LMD_TYPE_TYPE
            // on the AST node).  The variable's tracked type_id is the actual
            // runtime representation type, so prefer it.
            if (tid == LMD_TYPE_TYPE && v->type_id != LMD_TYPE_ANY) return v->type_id;
        }
    }
    // Proc/function call AST types can be stale when one return path is null but
    // another returns a heap value, e.g. `vec_remove_first()` returning null for
    // empty arrays and a map otherwise. MIR calls return boxed Items in that case;
    // treating the call as NULL leaves the result unrooted until a later assignment,
    // where growing the root frame can allocate before the value is protected.
    if (node->node_type == AST_NODE_CALL_EXPR && tid == LMD_TYPE_NULL) {
        return LMD_TYPE_ANY;
    }
    // For binary nodes: if either operand has effective type ANY, the runtime
    // will use the boxed fallback path → result is a boxed Item (ANY)
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bi = (AstBinaryNode*)node;

        // IDIV/MOD ALWAYS return boxed Items from runtime (fn_idiv/fn_mod)
        // UNLESS both operands are INT — then we use native fn_idiv_i/fn_mod_i
        // which return int64_t (with INT64_ERROR for div-by-zero).
        // Note: INT64 excluded — transpile_expr returns inconsistent values for INT64
        // (raw int64 from literals but boxed Items from generic binary fallback).
        if (bi->op == OPERATOR_IDIV || bi->op == OPERATOR_MOD) {
            TypeId idiv_lt = get_effective_type(mt, bi->left);
            TypeId idiv_rt = get_effective_type(mt, bi->right);
            if (idiv_lt == LMD_TYPE_ANY || idiv_rt == LMD_TYPE_ANY)
                return LMD_TYPE_ANY;
            bool both_int_idiv = (idiv_lt == LMD_TYPE_INT) && (idiv_rt == LMD_TYPE_INT);
            if (both_int_idiv)
                return LMD_TYPE_INT;
            // Float MOD uses fmod() which returns double
            if (bi->op == OPERATOR_MOD) {
                bool has_float = (idiv_lt == LMD_TYPE_FLOAT || idiv_rt == LMD_TYPE_FLOAT);
                bool other_numeric = (idiv_lt == LMD_TYPE_INT || idiv_lt == LMD_TYPE_FLOAT) &&
                                     (idiv_rt == LMD_TYPE_INT || idiv_rt == LMD_TYPE_FLOAT);
                if (has_float && other_numeric)
                    return LMD_TYPE_FLOAT;
            }
            return LMD_TYPE_ANY;
        }

        TypeId lt = get_effective_type(mt, bi->left);
        TypeId rt = get_effective_type(mt, bi->right);

        if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT &&
            (bi->op == OPERATOR_ADD || bi->op == OPERATOR_SUB || bi->op == OPERATOR_MUL)) {
            // The AST's int result type is too narrow after 53-bit overflow
            // promotion; MIR must treat the runtime result as a boxed Item.
            return LMD_TYPE_ANY;
        }

        // Comparison result type — must mirror transpile_binary's native-vs-fallback
        // decision (which uses these same lt/rt), so consumers read the result with
        // the right representation:
        //   • vectorized (array operand)  → ARRAY_NUM mask
        //   • fn_eq/fn_ne (still Bool)     → BOOL (raw uint8)
        //   • LT-GE, both native numeric   → BOOL (native MIR comparison → raw 0/1)
        //   • LT-GE, otherwise             → ANY  (fn_lt/gt/le/ge fallback → boxed bool / mask Item)
        // EQ/NE/IS/IN stay Bool. (Returning ANY for a raw-0/1 native bool would be
        // unsafe — it would be boxed then dereferenced as a pointer — hence the
        // native cases keep BOOL.)
        {
            int op_ck = bi->op;
            if (is_elementwise_comparison_op(op_ck)) {
                if (comparison_vectorizes(op_ck, lt, rt)) return LMD_TYPE_ARRAY_NUM;
                return LMD_TYPE_ANY;
            }
            if (op_ck == OPERATOR_EQ || op_ck == OPERATOR_NE ||
                op_ck == OPERATOR_IS || op_ck == OPERATOR_IS_NAN ||
                op_ck == OPERATOR_IN || op_ck == OPERATOR_AT)
                return LMD_TYPE_BOOL;
            if (op_ck >= OPERATOR_LT && op_ck <= OPERATOR_GE) {
                bool l_native = (is_native_numeric_type_id(lt));
                bool r_native = (is_native_numeric_type_id(rt));
                return (l_native && r_native) ? LMD_TYPE_BOOL : LMD_TYPE_ANY;
            }
        }

        if (lt == LMD_TYPE_ANY || rt == LMD_TYPE_ANY) return LMD_TYPE_ANY;

        // INT64 arithmetic (with INT or INT64) goes through boxed fn_add/fn_sub/fn_mul
        // runtime path (transpile_binary skips native arithmetic for INT64 due to
        // inconsistent representation — raw int64 from some paths, tagged Items from
        // others). The boxed runtime returns tagged Items, so effective type is ANY.
        // Without this, the AST type (often INT) would be returned, causing
        // transpile_assign_stam to treat the tagged Item as a raw int → infinite loops.
        {
            bool has_int64 = (lt == LMD_TYPE_INT64 || rt == LMD_TYPE_INT64);
            bool other_int = is_integer_type_id(lt) && is_integer_type_id(rt);
            if (has_int64 && other_int) {
                int op64 = bi->op;
                if (op64 == OPERATOR_ADD || op64 == OPERATOR_SUB || op64 == OPERATOR_MUL ||
                    op64 == OPERATOR_DIV || op64 == OPERATOR_POW)
                    return LMD_TYPE_ANY;
            }
        }

        // Infer result type from operand types when AST type is not set.
        // This ensures transpile_assign_stam correctly identifies native values
        // returned by transpile_binary (e.g. unboxed INT from fn_mod with int ops).
        if (tid == LMD_TYPE_ANY || tid == LMD_TYPE_NULL) {
            int op = bi->op;
            bool both_int = (lt == LMD_TYPE_INT) && (rt == LMD_TYPE_INT);
            bool both_float = (lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_FLOAT);
            bool int_float = (lt == LMD_TYPE_INT && rt == LMD_TYPE_FLOAT) ||
                             (lt == LMD_TYPE_FLOAT && rt == LMD_TYPE_INT);
            bool both_bool = (lt == LMD_TYPE_BOOL) && (rt == LMD_TYPE_BOOL);

            if (both_int) {
                if (op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL)
                    return LMD_TYPE_ANY;
                if (op == OPERATOR_DIV)
                    return LMD_TYPE_FLOAT;
                // IDIV/MOD with both_int handled above via native fn_idiv_i/fn_mod_i
            }
            if (both_float || int_float) {
                if (op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL ||
                    op == OPERATOR_DIV)
                    return LMD_TYPE_FLOAT;
            }
            // Scalar symbolic comparisons always return bool.
            if (op >= OPERATOR_EQ && op <= OPERATOR_GE)
                return LMD_TYPE_BOOL;
            if (is_elementwise_comparison_op(op))
                return LMD_TYPE_ANY;
            if (op == OPERATOR_IS || op == OPERATOR_IS_NAN ||
                op == OPERATOR_IN || op == OPERATOR_AT)
                return LMD_TYPE_BOOL;
            // AND/OR with both_bool return bool
            if ((op == OPERATOR_AND || op == OPERATOR_OR) && both_bool)
                return LMD_TYPE_BOOL;
        }
    }
    // For ASSIGN_STAM: transpile_assign_stam returns the RHS value, so
    // effective type is the RHS type. This ensures callers (e.g. transpile_if)
    // know the actual MIR register type when an assignment appears as a
    // branch result (e.g. if (cond) { x = x / 2 } → RHS is FLOAT).
    if (node->node_type == AST_NODE_ASSIGN_STAM) {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        if (assign->value) return get_effective_type(mt, assign->value);
    }
    // For INDEX nodes: if object is known ArrayNum and index is INT, derive element type
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* fn = (AstFieldNode*)node;
        TypeId obj_eff = get_effective_type(mt, fn->object);
        TypeId idx_eff = get_effective_type(mt, fn->field);
        if (obj_eff == LMD_TYPE_ARRAY_NUM && is_integer_type_id(idx_eff)) {
            // try to determine element type from the object variable
            AstNode* obj_unwrapped = fn->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov && ov->elem_type != LMD_TYPE_ANY) return ov->elem_type;
            }
            return LMD_TYPE_INT;  // default for ARRAY_NUM without known elem_type
        }
        // ARRAY with nested=INT + INT index: return type is ANY (not INT)
        // because the array may have been converted from ArrayInt to generic
        // Array by fn_array_set (e.g., arr[1] = 3.14 on an int array).
        // The transpile_index runtime check handles this correctly.
    }
    // For if nodes: use effective type of branches
    if (node->node_type == AST_NODE_IF_EXPR) {
        AstIfNode* if_node = (AstIfNode*)node;
        TypeId then_eff = if_node->then ? get_effective_type(mt, if_node->then) : LMD_TYPE_ANY;
        TypeId else_eff = if_node->otherwise ? get_effective_type(mt, if_node->otherwise) : LMD_TYPE_ANY;
        if (then_eff == LMD_TYPE_ANY || else_eff == LMD_TYPE_ANY) return LMD_TYPE_ANY;
    }
    // For UNARY nodes: infer result type from operand's effective type.
    // transpile_unary returns native values for NEG(INT) → INT, NEG(FLOAT) → FLOAT,
    // NOT(BOOL) → BOOL, POS(numeric) → same type. Without this, Phase 4 native
    // functions whose parameter types are inferred (not in AST) would have wrong
    // effective types for unary expressions, causing type mismatches in assignments.
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* un = (AstUnaryNode*)node;
        TypeId operand_eff = get_effective_type(mt, un->operand);
        if (un->op == OPERATOR_NEG) {
            if (operand_eff == LMD_TYPE_NUM_SIZED || operand_eff == LMD_TYPE_UINT64) return operand_eff;
            if (operand_eff == LMD_TYPE_INT) return LMD_TYPE_INT;
            if (operand_eff == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
        } else if (un->op == OPERATOR_NOT) {
            return LMD_TYPE_BOOL;
        } else if (un->op == OPERATOR_POS) {
            if (is_integer_type_id(operand_eff) ||
                operand_eff == LMD_TYPE_NUM_SIZED || operand_eff == LMD_TYPE_UINT64 ||
                operand_eff == LMD_TYPE_FLOAT)
                return operand_eff;
        } else if (un->op == OPERATOR_IS_ERROR) {
            return LMD_TYPE_BOOL;
        }
    }
    return tid;
}

static MIR_reg_t transpile_binary(MirTranspiler* mt, AstBinaryNode* bi) {
    // TCO: binary expression operands are NEVER in tail position.
    // e.g., `1 + f(n-1)` — the call is NOT tail because addition follows.
    mt->in_tail_position = false;

    TypeId left_tid = get_effective_type(mt, bi->left);
    TypeId right_tid = get_effective_type(mt, bi->right);

    if ((bi->op == OPERATOR_EQ || bi->op == OPERATOR_NE) &&
        eq_known_cross_family_false(left_tid, right_tid)) {
        // statically-known cross-family equality is total and never calls fn_eq.
        return emit_bool_const(mt, bi->op == OPERATOR_NE);
    }

    // Keyword comparisons are the only vectorized comparison syntax; symbolic
    // < <= > >= remain scalar so masks are never implicit truth values.
    if (is_elementwise_comparison_op(bi->op)) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        return emit_call_3(mt, "vec_cmp", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)elementwise_cmp_code(bi->op)));
    }

    // IDIV and MOD: native fast paths when operand types are known
    // Note: INT64 excluded from native path — transpile_expr returns inconsistent
    // values for INT64 (raw vs boxed). All INT64 ops use generic boxed fallback.
    if (bi->op == OPERATOR_IDIV || bi->op == OPERATOR_MOD) {
        bool both_int = (left_tid == LMD_TYPE_INT) && (right_tid == LMD_TYPE_INT);
        if (both_int) {
            // Native integer path: fn_idiv_i / fn_mod_i (returns int64_t, INT64_ERROR on div-by-zero)
            MIR_reg_t left = transpile_expr(mt, bi->left);
            MIR_reg_t right = transpile_expr(mt, bi->right);
            const char* fn_name = (bi->op == OPERATOR_IDIV) ? "fn_idiv_i" : "fn_mod_i";
            return emit_call_2(mt, fn_name, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
        }
        // Float MOD: use fmod(double, double) -> double
        // Requires at least one FLOAT operand and the other being INT or FLOAT
        if (bi->op == OPERATOR_MOD) {
            bool has_float = (left_tid == LMD_TYPE_FLOAT || right_tid == LMD_TYPE_FLOAT);
            bool both_numeric = has_float &&
                                (left_tid == LMD_TYPE_INT || left_tid == LMD_TYPE_FLOAT) &&
                                (right_tid == LMD_TYPE_INT || right_tid == LMD_TYPE_FLOAT);
            if (both_numeric) {
                MIR_reg_t left = transpile_expr(mt, bi->left);
                MIR_reg_t right = transpile_expr(mt, bi->right);
                // Convert to double if needed
                MIR_reg_t dl = left, dr = right;
                if (left_tid == LMD_TYPE_INT) {
                    dl = new_reg(mt, "i2d_ml", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dl),
                        MIR_new_reg_op(mt->ctx, left)));
                }
                if (right_tid == LMD_TYPE_INT) {
                    dr = new_reg(mt, "i2d_mr", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dr),
                        MIR_new_reg_op(mt->ctx, right)));
                }
                return emit_call_2(mt, "fmod", MIR_T_D,
                    MIR_T_D, MIR_new_reg_op(mt->ctx, dl),
                    MIR_T_D, MIR_new_reg_op(mt->ctx, dr));
            }
        }
        // Fallback: boxed runtime for unknown/mixed types
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        const char* fn_name = (bi->op == OPERATOR_IDIV) ? "fn_idiv" : "fn_mod";
        MIR_reg_t result = emit_call_2(mt, fn_name, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
        return result;
    }

    // Type-dispatch: if both sides are native types, use native MIR ops
    bool left_int = (left_tid == LMD_TYPE_INT);
    bool right_int = (right_tid == LMD_TYPE_INT);
    bool left_float = (left_tid == LMD_TYPE_FLOAT);
    bool right_float = (right_tid == LMD_TYPE_FLOAT);
    bool left_int64 = (left_tid == LMD_TYPE_INT64);
    bool right_int64 = (right_tid == LMD_TYPE_INT64);
    bool both_int = left_int && right_int;
    bool both_float = left_float && right_float;
    bool int_float = (left_int && right_float) || (left_float && right_int);
    // INT64 + INT or INT64 + INT64: both stored as native int64 in MIR registers.
    // Safe for COMPARISONS only (EQ/NE/LT/LE/GT/GE) — no risk of overflow or
    // representation mismatch. Arithmetic still goes through boxed path since
    // transpile_expr may return inconsistent values for INT64.
    bool int_or_int64 = (left_int || left_int64) && (right_int || right_int64);

    // Note: INT64 arithmetic is NOT handled natively because transpile_expr
    // returns inconsistent values for INT64 (raw int64 from literals/fn_int64,
    // but boxed Items from generic binary fallback). All INT64 ops go through
    // the generic boxed path, whose result is preserved by transpile_box_item.

    if (both_int && (bi->op == OPERATOR_ADD || bi->op == OPERATOR_SUB || bi->op == OPERATOR_MUL)) {
        // Compact-int arithmetic can promote to boxed float at the 53-bit boundary;
        // the native MIR op would silently create an out-of-model tagged int.
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        const char* fn_name = (bi->op == OPERATOR_ADD) ? "fn_add" :
                              (bi->op == OPERATOR_SUB) ? "fn_sub" : "fn_mul";
        return emit_call_2(mt, fn_name, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

    // Arithmetic ops with native types
    if (both_int || both_float || int_float) {
        MIR_reg_t left = transpile_expr(mt, bi->left);
        MIR_reg_t right = transpile_expr(mt, bi->right);

        bool use_float = both_float || int_float;
        MIR_reg_t fl = left, fr = right;

        // Convert int to float if needed
        if (int_float) {
            if (left_int && right_float) {
                fl = new_reg(mt, "i2d", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, fl),
                    MIR_new_reg_op(mt->ctx, left)));
                fr = right;
            } else {
                fl = left;
                fr = new_reg(mt, "i2d", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, fr),
                    MIR_new_reg_op(mt->ctx, right)));
            }
        }

        MIR_type_t rtype = use_float ? MIR_T_D : MIR_T_I64;

        switch (bi->op) {
        case OPERATOR_ADD: {
            MIR_reg_t r = new_reg(mt, "add", rtype);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DADD : MIR_ADD,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_SUB: {
            MIR_reg_t r = new_reg(mt, "sub", rtype);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DSUB : MIR_SUB,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_MUL: {
            MIR_reg_t r = new_reg(mt, "mul", rtype);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DMUL : MIR_MUL,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_DIV: {
            // int/int division promotes to float
            if (both_int) {
                MIR_reg_t dl = new_reg(mt, "i2d_l", MIR_T_D);
                MIR_reg_t dr = new_reg(mt, "i2d_r", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dl),
                    MIR_new_reg_op(mt->ctx, left)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dr),
                    MIR_new_reg_op(mt->ctx, right)));
                MIR_reg_t r = new_reg(mt, "div", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DDIV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, dl), MIR_new_reg_op(mt->ctx, dr)));
                return r;
            } else {
                MIR_reg_t r = new_reg(mt, "div", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DDIV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
                return r;
            }
        }
        case OPERATOR_POW:
            // POW has AST type ANY (not FLOAT/INT), so native handling would
            // return MIR_T_D which conflicts with ANY expectation of boxed Item.
            // Let all POW fall through to boxed runtime path.
            break;
        // Comparison operators
        case OPERATOR_EQ: {
            MIR_reg_t r = new_reg(mt, "eq", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DEQ : MIR_EQ,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_NE: {
            MIR_reg_t r = new_reg(mt, "ne", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DNE : MIR_NE,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_LT: {
            MIR_reg_t r = new_reg(mt, "lt", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DLT : MIR_LT,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_LE: {
            MIR_reg_t r = new_reg(mt, "le", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DLE : MIR_LE,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_GT: {
            MIR_reg_t r = new_reg(mt, "gt", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DGT : MIR_GT,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case OPERATOR_GE: {
            MIR_reg_t r = new_reg(mt, "ge", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DGE : MIR_GE,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        default:
            break;  // fall through to boxed path
        }
    }

    // Native INT64 vs INT comparisons: both are int64 in MIR registers.
    // Only for comparison operators (safe, no overflow risk).
    // NOTE: Arithmetic (ADD/SUB/MUL) intentionally NOT handled natively for INT64
    // because transpile_expr returns inconsistent values for INT64 (raw int64 from
    // some paths, tagged Items from others). All INT64 arithmetic goes through the
    // boxed runtime path. The assignment unbox path (INT64 in transpile_assign_stam)
    // ensures the boxed result is correctly converted back to raw int64.
    if (int_or_int64 && !both_int) {
        int op = bi->op;
        if (op >= OPERATOR_EQ && op <= OPERATOR_GE) {
            MIR_reg_t left = transpile_expr(mt, bi->left);
            MIR_reg_t right = transpile_expr(mt, bi->right);
            MIR_insn_code_t mir_op;
            const char* name;
            switch (op) {
            case OPERATOR_EQ: mir_op = MIR_EQ; name = "eq"; break;
            case OPERATOR_NE: mir_op = MIR_NE; name = "ne"; break;
            case OPERATOR_LT: mir_op = MIR_LT; name = "lt"; break;
            case OPERATOR_LE: mir_op = MIR_LE; name = "le"; break;
            case OPERATOR_GT: mir_op = MIR_GT; name = "gt"; break;
            case OPERATOR_GE: mir_op = MIR_GE; name = "ge"; break;
            default: mir_op = MIR_EQ; name = "eq"; break;
            }
            MIR_reg_t r = new_reg(mt, name, MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, mir_op,
                MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, left),
                MIR_new_reg_op(mt->ctx, right)));
            return r;
        }
    }

    // String/symbol concatenation: fn_join(Item, Item) -> Item
    if (bi->op == OPERATOR_JOIN) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        int left_root = create_gc_root_slot(mt, boxl);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        int right_root = create_gc_root_slot(mt, boxr);
        boxl = load_gc_root_slot(mt, left_root, "join_l");
        boxr = load_gc_root_slot(mt, right_root, "join_r");
        MIR_reg_t join_result = emit_call_2(mt, "fn_join", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
        int result_root = create_gc_root_slot(mt, join_result);
        return load_gc_root_slot(mt, result_root, "join_rv");
    }

    // Short-circuit AND/OR
    if (bi->op == OPERATOR_AND) {
        MIR_reg_t result = new_reg(mt, "and", MIR_T_I64);

        if (left_tid == LMD_TYPE_BOOL && right_tid == LMD_TYPE_BOOL) {
            // native bool AND
            MIR_reg_t left_val = transpile_expr(mt, bi->left);
            MIR_label_t l_false = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
                MIR_new_reg_op(mt->ctx, left_val)));
            MIR_reg_t right_val = transpile_expr(mt, bi->right);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, right_val)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
            emit_label(mt, l_false);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));
            emit_label(mt, l_end);
            return result;
        }
        // Boxed AND: use is_truthy + branch with short-circuit
        // Use transpile_box_item to ensure sub-expressions are properly boxed
        // (transpile_expr + emit_box(val, ANY) fails when inner expr returns native values)
        MIR_reg_t boxed_left = transpile_box_item(mt, bi->left);
        MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left)));
        MIR_label_t l_false = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
            MIR_new_reg_op(mt->ctx, truthy)));
        MIR_reg_t boxed_right = transpile_box_item(mt, bi->right);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_right)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_false);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_left)));
        emit_label(mt, l_end);
        return result;
    }

    if (bi->op == OPERATOR_OR) {
        MIR_reg_t result = new_reg(mt, "or", MIR_T_I64);

        if (left_tid == LMD_TYPE_BOOL && right_tid == LMD_TYPE_BOOL) {
            MIR_reg_t left_val = transpile_expr(mt, bi->left);
            MIR_label_t l_true = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_true),
                MIR_new_reg_op(mt->ctx, left_val)));
            MIR_reg_t right_val = transpile_expr(mt, bi->right);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, right_val)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
            emit_label(mt, l_true);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 1)));
            emit_label(mt, l_end);
            return result;
        }
        // Boxed OR: use is_truthy + branch with short-circuit
        MIR_reg_t boxed_left = transpile_box_item(mt, bi->left);
        MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left)));
        MIR_label_t l_true = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_true),
            MIR_new_reg_op(mt->ctx, truthy)));
        MIR_reg_t boxed_right = transpile_box_item(mt, bi->right);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_right)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        emit_label(mt, l_true);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, boxed_left)));
        emit_label(mt, l_end);
        return result;
    }

    // Range operator (a to b)
    if (bi->op == OPERATOR_TO) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        // fn_to(start, end) returns Range* (a container pointer)
        return emit_call_2(mt, "fn_to", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

    // IEEE NaN check: expr is nan
    if (bi->op == OPERATOR_IS_NAN) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        return emit_call_1(mt, "fn_is_nan", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl));
    }

    // Type operators
    if (bi->op == OPERATOR_IS) {
        // Check if right operand is a constrained type for inline constraint evaluation
        AstNode* right_node = bi->right;
        // Unwrap primary node
        if (right_node->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)right_node;
            if (pri->expr) right_node = pri->expr;
        }

        AstConstrainedTypeNode* constrained_node = nullptr;
        if (right_node->node_type == AST_NODE_CONSTRAINED_TYPE) {
            constrained_node = (AstConstrainedTypeNode*)right_node;
        } else if (right_node->type && right_node->type->kind == TYPE_KIND_CONSTRAINED) {
            if (right_node->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident = (AstIdentNode*)right_node;
                if (ident->entry && ident->entry->node && ident->entry->node->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* type_def = (AstNamedNode*)ident->entry->node;
                    if (type_def->as && type_def->as->node_type == AST_NODE_CONSTRAINED_TYPE) {
                        constrained_node = (AstConstrainedTypeNode*)type_def->as;
                    }
                }
            }
        } else if (right_node->type && right_node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* type_type = (TypeType*)right_node->type;
            if (type_type->type && type_type->type->kind == TYPE_KIND_CONSTRAINED) {
                if (right_node->node_type == AST_NODE_IDENT) {
                    AstIdentNode* ident = (AstIdentNode*)right_node;
                    if (ident->entry && ident->entry->node && ident->entry->node->node_type == AST_NODE_ASSIGN) {
                        AstNamedNode* type_def = (AstNamedNode*)ident->entry->node;
                        if (type_def->as && type_def->as->node_type == AST_NODE_CONSTRAINED_TYPE) {
                            constrained_node = (AstConstrainedTypeNode*)type_def->as;
                        }
                    }
                }
            }
        }

        if (constrained_node) {
            // Inline constrained type check: base_type_check && constraint_check
            TypeConstrained* constrained = (TypeConstrained*)constrained_node->type;

            MIR_reg_t ct_value = transpile_box_item(mt, bi->left);
            MIR_reg_t result = new_reg(mt, "ct_res", MIR_T_I64);

            // Check base type: item_type_id(ct_value) == constrained->base->type_id
            MIR_reg_t tid_reg = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ct_value)));
            MIR_reg_t base_match = new_reg(mt, "base_eq", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, base_match),
                MIR_new_reg_op(mt->ctx, tid_reg),
                MIR_new_int_op(mt->ctx, constrained->base->type_id)));

            // If base doesn't match, result = false
            MIR_label_t lbl_check = new_label(mt);
            MIR_label_t lbl_end = new_label(mt);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, 0)));  // default false
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, lbl_check),
                MIR_new_reg_op(mt->ctx, base_match)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, lbl_end)));

            // Base type matches — evaluate constraint with ~ bound to ct_value
            emit_label(mt, lbl_check);
            MIR_reg_t old_pipe_item = mt->pipe_item_reg;
            bool old_in_pipe = mt->in_pipe;
            mt->pipe_item_reg = ct_value;
            mt->in_pipe = true;

            MIR_reg_t constraint_val = transpile_box_item(mt, constrained_node->constraint);
            MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, constraint_val)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, truthy)));

            mt->pipe_item_reg = old_pipe_item;
            mt->in_pipe = old_in_pipe;

            emit_label(mt, lbl_end);
            return result;
        }

        // Standard fn_is call for non-constrained types
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        return emit_call_2(mt, "fn_is", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }
    if (bi->op == OPERATOR_IN) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        return emit_call_2(mt, "fn_in", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }
    if (bi->op == OPERATOR_AT) {
        MIR_reg_t boxl = transpile_box_item(mt, bi->left);
        MIR_reg_t boxr = transpile_box_item(mt, bi->right);
        return emit_call_2(mt, "fn_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));
    }

    // ======================================================================
    // Inline string/symbol comparison: avoid boxing + type dispatch
    // Fast path: pointer identity (single MIR_EQ, no function call)
    // Slow path: lightweight helper (no boxing, no type dispatch)
    // ======================================================================
    {
        bool both_string = (left_tid == LMD_TYPE_STRING && right_tid == LMD_TYPE_STRING);
        bool both_symbol = (left_tid == LMD_TYPE_SYMBOL && right_tid == LMD_TYPE_SYMBOL);
        if ((both_string || both_symbol) &&
            (bi->op == OPERATOR_EQ || bi->op == OPERATOR_NE)) {
            MIR_reg_t left = transpile_expr(mt, bi->left);
            MIR_reg_t right = transpile_expr(mt, bi->right);
            MIR_reg_t result = new_reg(mt, "seq", MIR_T_I64);
            MIR_label_t l_end = new_label(mt);

            // Fast path: pointer identity
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, left),
                MIR_new_reg_op(mt->ctx, right)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, l_end),
                MIR_new_reg_op(mt->ctx, result)));

            // Slow path: content comparison via lightweight helper
            const char* helper = both_string ? "fn_str_eq_ptr" : "fn_sym_eq_ptr";
            MIR_reg_t cmp = emit_call_2(mt, helper, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, cmp),
                MIR_new_int_op(mt->ctx, 0xFF)));

            emit_label(mt, l_end);

            // For NE: invert the result
            if (bi->op == OPERATOR_NE) {
                MIR_reg_t inv = new_reg(mt, "sne", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                    MIR_new_reg_op(mt->ctx, inv),
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
                return inv;
            }
            return result;
        }
    }

    // Fallback: box both sides and call runtime function
    // Use transpile_box_item to correctly box sub-expressions that may return native values
    MIR_reg_t boxl = transpile_box_item(mt, bi->left);
    MIR_reg_t boxr = transpile_box_item(mt, bi->right);

    const char* fn_name = NULL;
    switch (bi->op) {
    case OPERATOR_ADD: fn_name = "fn_add"; break;
    // OPERATOR_JOIN handled above via fn_join
    case OPERATOR_SUB: fn_name = "fn_sub"; break;
    case OPERATOR_MUL: fn_name = "fn_mul"; break;
    case OPERATOR_DIV: fn_name = "fn_div"; break;
    case OPERATOR_IDIV: fn_name = "fn_idiv"; break;
    case OPERATOR_MOD: fn_name = "fn_mod"; break;
    case OPERATOR_POW: fn_name = "fn_pow"; break;
    case OPERATOR_UNION: fn_name = "fn_union"; break;
    case OPERATOR_EQ: fn_name = "fn_eq"; break;
    case OPERATOR_NE: fn_name = "fn_ne"; break;
    case OPERATOR_LT: fn_name = "fn_lt"; break;
    case OPERATOR_LE: fn_name = "fn_le"; break;
    case OPERATOR_GT: fn_name = "fn_gt"; break;
    case OPERATOR_GE: fn_name = "fn_ge"; break;
    default:
        log_error("mir: unhandled binary op %d", bi->op);
        {
            // Return ItemNull for unimplemented operators (e.g. set intersection/exclusion)
            MIR_reg_t null_r = new_reg(mt, "uop", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, null_r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return null_r;
        }
    }

    MIR_reg_t result = emit_call_2(mt, fn_name, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxl),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxr));

    // fn_eq/fn_ne still return Bool (uint8_t) but we declare MIR_T_I64 as the
    // return type; on some ABIs the upper bytes may be garbage, so mask to 0xFF
    // for a clean native bool.  Ordered comparisons (fn_lt/fn_gt/fn_le/fn_ge) now
    // return an Item (boxed bool or element-wise mask) — leave it unmasked.
    if (bi->op == OPERATOR_EQ || bi->op == OPERATOR_NE) {
        MIR_reg_t clean = new_reg(mt, "cmask", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, clean),
            MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, 0xFF)));
        return clean;
    }

    // NOTE: The general fallback is only reached for non-native type combinations
    // or POW (which breaks out of the native block). ADD/SUB/MUL/DIV with native
    // types are handled above, IDIV/MOD are intercepted at the top of this function.
    // POW with int operands can return float (negative exponent), so we do NOT
    // unbox POW results — they remain boxed Items from the runtime.
    return result;
}

// ============================================================================
// Unary expressions
// ============================================================================

static MIR_reg_t transpile_unary(MirTranspiler* mt, AstUnaryNode* un) {
    TypeId operand_tid = get_effective_type(mt, un->operand);

    switch (un->op) {
    case OPERATOR_NEG: {
        if (operand_tid == LMD_TYPE_INT) {
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            MIR_reg_t r = new_reg(mt, "neg", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_NEG, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand)));
            return r;
        }
        if (operand_tid == LMD_TYPE_FLOAT) {
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            MIR_reg_t r = new_reg(mt, "neg", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DNEG, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand)));
            return r;
        }
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
        return emit_call_1(mt, "fn_neg", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    case OPERATOR_NOT: {
        if (operand_tid == LMD_TYPE_BOOL) {
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            MIR_reg_t r = new_reg(mt, "not", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, r),
                MIR_new_reg_op(mt->ctx, operand), MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
        MIR_reg_t bool_result = emit_call_1(mt, "fn_not", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        // fn_not returns uint8_t Bool — zero-extend to native i64 (0 or 1).
        // Must return native bool (not boxed Item) because get_effective_type
        // advertises OPERATOR_NOT as LMD_TYPE_BOOL, and transpile_if uses the
        // value directly with MIR_BF (branch-if-false) without calling is_truthy.
        // A boxed bool (e.g. 0x0200000000000000 for false) is always non-zero,
        // which would make MIR_BF never branch — breaking `if not x` for non-bool x.
        return emit_uext8(mt, bool_result);
    }
    case OPERATOR_POS: {
        if (is_native_numeric_type_id(operand_tid)) {
            // No-op for numeric types
            MIR_reg_t operand = transpile_expr(mt, un->operand);
            return operand;
        }
        // Runtime fn_pos for string-to-number casting etc.
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
        return emit_call_1(mt, "fn_pos", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    case OPERATOR_IS_ERROR: {
        // ^expr: check if Item type is LMD_TYPE_ERROR
        MIR_reg_t boxed = transpile_box_item(mt, un->operand);
        MIR_reg_t type_reg = new_reg(mt, "tid", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_RSH, MIR_new_reg_op(mt->ctx, type_reg),
            MIR_new_reg_op(mt->ctx, boxed), MIR_new_int_op(mt->ctx, 56)));
        MIR_reg_t r = new_reg(mt, "iserr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, r),
            MIR_new_reg_op(mt->ctx, type_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));
        return r;
    }
    default:
        log_error("mir: unhandled unary op %d", un->op);
        MIR_reg_t operand = transpile_expr(mt, un->operand);
        return operand;
    }
}

// ============================================================================
// Spread expression
// ============================================================================

static MIR_reg_t transpile_spread(MirTranspiler* mt, AstUnaryNode* spread) {
    MIR_reg_t boxed = transpile_box_item(mt, spread->operand);
    return emit_call_1(mt, "item_spread", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
}

// ============================================================================
// If/else expressions
// ============================================================================

static MIR_reg_t transpile_if(MirTranspiler* mt, AstIfNode* if_node) {
    TypeId cond_tid = get_effective_type(mt, if_node->cond);

    // TCO: condition is NOT in tail position
    bool saved_tail = mt->in_tail_position;
    mt->in_tail_position = false;

    MIR_reg_t cond = transpile_expr(mt, if_node->cond);

    // Restore tail position for branches
    mt->in_tail_position = saved_tail;

    // For non-bool condition, check truthiness via runtime
    // Lambda semantics: only null, error, and false are falsy.
    // Note: even when type says INT/FLOAT/STRING, runtime value could be null
    // (e.g. optional parameters), so always use is_truthy for safety.
    MIR_reg_t cond_val = cond;
    if (cond_tid != LMD_TYPE_BOOL) {
        MIR_reg_t boxed = emit_box(mt, cond, cond_tid);
        cond_val = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed)));
    }

    // Determine result type - check if branches could produce different MIR types
    // Use effective types to account for optional params and mixed runtime paths
    TypeId if_tid = get_effective_type(mt, (AstNode*)if_node);
    TypeId then_tid = if_node->then ? get_effective_type(mt, if_node->then) : LMD_TYPE_ANY;
    TypeId else_tid = if_node->otherwise ? get_effective_type(mt, if_node->otherwise) : LMD_TYPE_ANY;
    MIR_type_t then_mir = type_to_mir(then_tid);
    MIR_type_t else_mir = type_to_mir(else_tid);

    log_debug("mir: transpile_if if_tid=%d then_tid=%d else_tid=%d then_mir=%d else_mir=%d",
             if_tid, then_tid, else_tid, then_mir, else_mir);

    // If branches have different MIR types or the node type is ANY, always box to Item (I64)
    bool need_boxing = (if_tid == LMD_TYPE_ANY ||
                        then_mir != else_mir ||
                        then_mir != type_to_mir(if_tid));

    // In proc statement context the if/else result is unused, but a final
    // pn-body if/else is the implicit return value and must keep branch values.
    bool proc_discard = mt->in_proc && need_boxing && !mt->preserve_proc_if_result;

    MIR_type_t result_type = need_boxing ? MIR_T_I64 : type_to_mir(if_tid);
    MIR_reg_t result = new_reg(mt, "if_res", result_type);
    MIR_label_t l_else = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, cond_val)));

    // Then branch
    if (if_node->then) {
        mt->in_tail_position = saved_tail;  // ensure correct before then branch
        push_scope(mt);  // isolate branch variables
        MIR_reg_t then_val = transpile_expr(mt, if_node->then);
        if (mt->block_returned) {
            // Branch contains a terminal statement (return/break/continue).
            // Code after MIR_RET/TCO is unreachable, but MIR still validates
            // operand modes, so the dummy must match the if-result register.
            if (result_type == MIR_T_D) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
            }
            mt->block_returned = false;
        } else if (proc_discard) {
            // Proc context: result unused, skip expensive boxing.
            // Just assign a dummy null Item to satisfy MIR type validation.
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        } else if (need_boxing) {
            // transpile_content always returns I64 (boxed Item from list_end
            // or transpile_box_item). The AST-inferred then_tid may be stale
            // (e.g. FLOAT for a side-effect-only block). Use ANY for CONTENT
            // branches to avoid type mismatches like emit_box_float(I64_reg).
            TypeId box_tid = then_tid;
            if (if_node->then->node_type == AST_NODE_CONTENT ||
                if_node->then->node_type == AST_NODE_LIST) {
                box_tid = LMD_TYPE_ANY;
            }
            MIR_reg_t boxed = emit_box(mt, then_val, box_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed)));
        } else if (result_type == MIR_T_D) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, then_val)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, then_val)));
        }
        pop_scope(mt);
    } else {
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // Else branch
    emit_label(mt, l_else);
    mt->in_tail_position = saved_tail;  // restore for else branch
    if (if_node->otherwise) {
        push_scope(mt);  // isolate branch variables
        MIR_reg_t else_val = transpile_expr(mt, if_node->otherwise);
        if (mt->block_returned) {
            // Same as above: terminal in else branch still needs a type-shaped dummy.
            if (result_type == MIR_T_D) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
            }
            mt->block_returned = false;
        } else if (proc_discard) {
            // Proc context: result unused, skip expensive boxing.
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        } else if (need_boxing) {
            TypeId box_tid = else_tid;
            if (if_node->otherwise->node_type == AST_NODE_CONTENT ||
                if_node->otherwise->node_type == AST_NODE_LIST) {
                box_tid = LMD_TYPE_ANY;
            }
            MIR_reg_t boxed = emit_box(mt, else_val, box_tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, boxed)));
        } else if (result_type == MIR_T_D) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, else_val)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, else_val)));
        }
        pop_scope(mt);
    } else {
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    }

    emit_label(mt, l_end);
    return result;
}

// ============================================================================
// Match expression
// ============================================================================

// Emit a single pattern test (fn_is/fn_in/fn_eq) and branch to l_fail on mismatch
static void emit_single_pattern_test(MirTranspiler* mt, AstNode* pattern, MIR_reg_t boxed_scrut, MIR_label_t l_fail) {
    // Handle constrained type patterns: case int that (~ > 0)
    if (pattern->node_type == AST_NODE_CONSTRAINED_TYPE) {
        AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)pattern;
        TypeConstrained* constrained = (TypeConstrained*)ct->type;
        if (constrained) {
            // Check base type
            MIR_reg_t tid_reg = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut)));
            MIR_reg_t base_match = new_reg(mt, "base_eq", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, base_match),
                MIR_new_reg_op(mt->ctx, tid_reg),
                MIR_new_int_op(mt->ctx, constrained->base->type_id)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fail),
                MIR_new_reg_op(mt->ctx, base_match)));

            // Base type matches — evaluate constraint with ~ bound to scrutinee
            MIR_reg_t old_pipe_item = mt->pipe_item_reg;
            bool old_in_pipe = mt->in_pipe;
            mt->pipe_item_reg = boxed_scrut;
            mt->in_pipe = true;

            MIR_reg_t constraint_val = transpile_box_item(mt, ct->constraint);
            MIR_reg_t truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, constraint_val)));

            mt->pipe_item_reg = old_pipe_item;
            mt->in_pipe = old_in_pipe;

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fail),
                MIR_new_reg_op(mt->ctx, truthy)));
            return;
        }
    }

    MIR_reg_t pat = transpile_expr(mt, pattern);
    TypeId pat_tid = get_effective_type(mt, pattern);
    MIR_reg_t boxed_pat = emit_box(mt, pat, pat_tid);

    MIR_reg_t match_test;
    if (pat_tid == LMD_TYPE_TYPE) {
        match_test = emit_uext8(mt, emit_call_2(mt, "fn_is", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat)));
    } else if (pat_tid == LMD_TYPE_RANGE) {
        match_test = emit_uext8(mt, emit_call_2(mt, "fn_in", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat)));
    } else {
        match_test = emit_uext8(mt, emit_call_2(mt, "fn_eq", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_scrut),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_pat)));
    }

    MIR_reg_t is_match = new_reg(mt, "ismatch", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_match),
        MIR_new_reg_op(mt->ctx, match_test), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fail),
        MIR_new_reg_op(mt->ctx, is_match)));
}

// Emit pattern test handling union (or-) patterns: branches to l_fail only if ALL alternatives fail
static void emit_match_pattern_test(MirTranspiler* mt, AstNode* pattern, MIR_reg_t boxed_scrut, MIR_label_t l_fail) {
    // Handle union patterns (A | B): try left, if matches jump to success, else try right
    if (pattern->node_type == AST_NODE_BINARY_TYPE) {
        AstBinaryNode* bi = (AstBinaryNode*)pattern;
        if (bi->op == OPERATOR_UNION) {
            MIR_label_t l_success = new_label(mt);
            MIR_label_t l_try_right = new_label(mt);

            // Try left alternative
            emit_match_pattern_test(mt, bi->left, boxed_scrut, l_try_right);
            // Left matched - jump to success
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_success)));

            // Try right alternative
            emit_label(mt, l_try_right);
            emit_match_pattern_test(mt, bi->right, boxed_scrut, l_fail);
            // Right matched - fall through to success

            emit_label(mt, l_success);
            return;
        }
    }

    // Simple pattern test
    emit_single_pattern_test(mt, pattern, boxed_scrut, l_fail);
}

static MIR_reg_t transpile_match(MirTranspiler* mt, AstMatchNode* match_node) {
    // TCO: scrutinee is NOT in tail position
    bool saved_tail = mt->in_tail_position;
    mt->in_tail_position = false;

    MIR_reg_t scrutinee = transpile_expr(mt, match_node->scrutinee);

    // Restore tail position for arm bodies
    mt->in_tail_position = saved_tail;
    TypeId scrut_tid = get_effective_type(mt, match_node->scrutinee);
    MIR_reg_t boxed_scrut = emit_box(mt, scrutinee, scrut_tid);

    // Set ~ (current item) to the scrutinee value for match bodies
    bool old_in_pipe = mt->in_pipe;
    MIR_reg_t old_pipe_item = mt->pipe_item_reg;
    MIR_reg_t old_pipe_index = mt->pipe_index_reg;
    mt->in_pipe = true;
    mt->pipe_item_reg = boxed_scrut;

    MIR_reg_t result = new_reg(mt, "match", MIR_T_I64);
    MIR_label_t l_end = new_label(mt);

    AstNode* arm = (AstNode*)match_node->first_arm;
    while (arm) {
        AstMatchArm* match_arm = (AstMatchArm*)arm;
        if (match_arm->pattern) {
            MIR_label_t l_next = new_label(mt);

            // Emit pattern test (handles union/or-patterns recursively)
            emit_match_pattern_test(mt, match_arm->pattern, boxed_scrut, l_next);

            // Body (restore tail position before each arm body)
            mt->in_tail_position = saved_tail;
            MIR_reg_t body = transpile_box_item(mt, match_arm->body);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, body)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            emit_label(mt, l_next);
        } else {
            // Default arm (restore tail position)
            mt->in_tail_position = saved_tail;
            MIR_reg_t body = transpile_box_item(mt, match_arm->body);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, body)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
        }
        arm = arm->next;
    }

    // No match - return ITEM_NULL
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));

    emit_label(mt, l_end);

    // Restore old pipe state
    mt->in_pipe = old_in_pipe;
    mt->pipe_item_reg = old_pipe_item;
    mt->pipe_index_reg = old_pipe_index;

    return result;
}

// ============================================================================
// For expressions
// ============================================================================

static bool mir_for_has_join_clause(AstForNode* for_node) {
    for (AstNode* cur = for_node ? for_node->loop : NULL; cur; cur = cur->next) {
        AstLoopNode* loop = (AstLoopNode*)cur;
        if (loop->on || loop->optional) return true;
    }
    return false;
}

static bool mir_validate_join_sources(AstLoopNode* first) {
    if (!first || !first->next) {
        log_error("mir: join on requires at least two sources");
        return false;
    }
    if (first->on || first->optional) {
        log_error("mir: first for source cannot have a join on clause");
        return false;
    }
    for (AstLoopNode* cur = first; cur; cur = (AstLoopNode*)cur->next) {
        // Sources without `on` stay cross-product stages; sources with `on` hash-join.
        if (cur != first && cur->on && cur->join_key_count <= 0) {
            log_error("mir: join source has no valid equality keys");
            return false;
        }
    }
    return true;
}

// Index/key bindings surface as int (LOOP_KEY_INT) or an opaque symbol/position item otherwise.
static Type* mir_join_index_type(AstLoopNode* loop) {
    return (loop->key_filter == LOOP_KEY_INT) ? &TYPE_INT : &TYPE_ANY;
}

// Emit the index/key binding name as an item register (or a null item when absent).
static MIR_reg_t mir_join_idx_name_item(MirTranspiler* mt, AstLoopNode* loop);

static MIR_reg_t mir_join_name_item(MirTranspiler* mt, String* name) {
    MIR_reg_t name_ptr = emit_load_string_literal(mt, name->chars);
    MIR_reg_t str_ptr = emit_call_1(mt, "heap_create_name", MIR_T_P,
        MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr));
    return emit_box_string(mt, str_ptr);
}

static MIR_reg_t mir_join_idx_name_item(MirTranspiler* mt, AstLoopNode* loop) {
    if (loop->index_name) return mir_join_name_item(mt, loop->index_name);
    return emit_null_item_reg(mt);
}

static void mir_join_bind_item_var(MirTranspiler* mt, String* name, Type* item_type, MIR_reg_t item_reg) {
    TypeId tid = item_type ? item_type->type_id : LMD_TYPE_ANY;
    MIR_type_t mtype = MIR_T_I64;
    MIR_reg_t val_reg = item_reg;
    if (tid == LMD_TYPE_FLOAT) {
        val_reg = emit_unbox(mt, item_reg, LMD_TYPE_FLOAT);
        mtype = MIR_T_D;
    } else if (tid == LMD_TYPE_INT) {
        val_reg = emit_unbox(mt, item_reg, LMD_TYPE_INT);
    } else if (tid == LMD_TYPE_INT64) {
        val_reg = emit_unbox(mt, item_reg, LMD_TYPE_INT64);
    } else if (tid == LMD_TYPE_STRING) {
        val_reg = emit_unbox(mt, item_reg, LMD_TYPE_STRING);
        mtype = MIR_T_P;
    }
    char var_name[128];
    snprintf(var_name, sizeof(var_name), "%.*s", (int)name->len, name->chars);
    set_var(mt, var_name, val_reg, mtype, tid);
}

static void mir_join_bind_tuple_vars(MirTranspiler* mt, AstLoopNode* first, AstLoopNode* stop_before,
        MIR_reg_t tuple_item) {
    for (AstLoopNode* cur = first; cur && cur != stop_before; cur = (AstLoopNode*)cur->next) {
        MIR_reg_t key_ptr = emit_load_string_literal(mt, cur->name->chars);
        MIR_reg_t attr = emit_call_2(mt, "item_attr", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, tuple_item),
            MIR_T_P, MIR_new_reg_op(mt->ctx, key_ptr));
        mir_join_bind_item_var(mt, cur->name, cur->type, attr);
        if (cur->index_name) {
            MIR_reg_t ikey_ptr = emit_load_string_literal(mt, cur->index_name->chars);
            MIR_reg_t iattr = emit_call_2(mt, "item_attr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, tuple_item),
                MIR_T_P, MIR_new_reg_op(mt->ctx, ikey_ptr));
            mir_join_bind_item_var(mt, cur->index_name, mir_join_index_type(cur), iattr);
        }
    }
}

static MIR_reg_t mir_join_key_item(MirTranspiler* mt, AstLoopNode* loop, bool use_new_side) {
    if (!loop || loop->join_key_count <= 0) return emit_null_item_reg(mt);
    if (loop->join_key_count == 1) {
        AstJoinKey* key = loop->join_keys;
        AstNode* expr = use_new_side ? key->new_expr : key->prior_expr;
        MIR_reg_t val = transpile_expr(mt, expr);
        TypeId tid = get_effective_type(mt, expr);
        return emit_box(mt, val, tid);
    }
    MIR_reg_t tuple = emit_call_0(mt, "array_plain", MIR_T_P);
    for (AstJoinKey* key = loop->join_keys; key; key = (AstJoinKey*)key->next) {
        AstNode* expr = use_new_side ? key->new_expr : key->prior_expr;
        MIR_reg_t val = transpile_expr(mt, expr);
        TypeId tid = get_effective_type(mt, expr);
        MIR_reg_t boxed = emit_box(mt, val, tid);
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, tuple),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
    }
    return emit_box_container(mt, tuple);
}

static void mir_join_collect_source(MirTranspiler* mt, AstLoopNode* loop, MIR_reg_t rows,
        MIR_reg_t row_keys, bool collect_keys, MIR_reg_t idx_vals) {
    int key_filter = (int)loop->key_filter;
    MIR_reg_t collection = transpile_expr(mt, loop->as);
    TypeId coll_tid = get_effective_type(mt, loop->as);
    MIR_reg_t boxed_coll = emit_box(mt, collection, coll_tid);
    MIR_reg_t keys_al = emit_call_1(mt, "item_keys", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));
    MIR_reg_t len = emit_call_3(mt, "iter_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));

    MIR_reg_t idx = new_reg(mt, "jidx", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));
    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    emit_label(mt, l_loop);
    MIR_reg_t cmp = new_reg(mt, "jcmp", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));

    // key-only sources (`k at map`) bind the key as the value; others bind the value.
    MIR_reg_t row = emit_call_4(mt, loop->key_only ? "iter_key_at" : "iter_val_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));
    mir_join_bind_item_var(mt, loop->name, loop->type, row);
    if (loop->index_name) {
        MIR_reg_t idx_item = emit_call_4(mt, "iter_key_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
            MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
            MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));
        mir_join_bind_item_var(mt, loop->index_name, mir_join_index_type(loop), idx_item);
        if (idx_vals) {
            emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, idx_vals),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_item));
        }
    }
    emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, rows),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, row));
    if (collect_keys) {
        MIR_reg_t key_item = mir_join_key_item(mt, loop, true);
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, row_keys),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_item));
    }

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));
    emit_label(mt, l_end);
    emit_call_void_1(mt, "symbol_key_list_free", MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al));
}

static MIR_reg_t mir_finalize_for_output(MirTranspiler* mt, AstForNode* for_node,
        MIR_reg_t output, MIR_reg_t keys_arr) {
    bool has_order = for_node->order != NULL;
    bool has_limit = for_node->limit != NULL;
    bool has_offset = for_node->offset != NULL;
    MIR_reg_t final_reg;
    if (has_order) {
        AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
        emit_call_void_3(mt, "fn_sort_by_keys",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, output),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, keys_arr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, first_spec->descending ? 1 : 0));
    }
    if (has_order && (has_offset || has_limit)) {
        if (has_offset) {
            MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
            TypeId off_tid = get_effective_type(mt, for_node->offset);
            MIR_reg_t off_raw = is_integer_type_id(off_tid)
                ? off_val : emit_unbox(mt, emit_box(mt, off_val, off_tid), LMD_TYPE_INT);
            MIR_reg_t off_masked = emit_unbox_int_mask(mt, off_raw);
            emit_call_void_2(mt, "array_drop_inplace", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, off_masked));
        }
        if (has_limit) {
            MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
            TypeId lim_tid = get_effective_type(mt, for_node->limit);
            MIR_reg_t lim_raw = is_integer_type_id(lim_tid)
                ? lim_val : emit_unbox(mt, emit_box(mt, lim_val, lim_tid), LMD_TYPE_INT);
            MIR_reg_t lim_masked = emit_unbox_int_mask(mt, lim_raw);
            emit_call_void_2(mt, for_node->limit_from_end ? "array_limit_last_inplace" : "array_limit_inplace",
                MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, lim_masked));
        }
        final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
    } else if (has_offset || has_limit) {
        MIR_reg_t cur_result = emit_box_container(mt, output);
        if (has_offset) {
            MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
            TypeId off_tid = get_effective_type(mt, for_node->offset);
            cur_result = emit_call_2(mt, "fn_drop", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box(mt, off_val, off_tid)));
        }
        if (has_limit) {
            MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
            TypeId lim_tid = get_effective_type(mt, for_node->limit);
            cur_result = emit_call_2(mt, for_node->limit_from_end ? "fn_take_last" : "fn_take", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box(mt, lim_val, lim_tid)));
        }
        final_reg = cur_result;
    } else {
        final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
    }
    return final_reg;
}

static MIR_reg_t transpile_for_join(MirTranspiler* mt, AstForNode* for_node, AstLoopNode* first) {
    if (!mir_validate_join_sources(first)) {
        MIR_reg_t r = new_reg(mt, "joinerr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR)));
        return r;
    }

    MIR_reg_t output = emit_call_0(mt, "array_spreadable", MIR_T_P);
    bool has_order = for_node->order != NULL;
    MIR_reg_t keys_arr = has_order ? emit_call_0(mt, "array_plain", MIR_T_P) : 0;

    MIR_reg_t seed_rows = emit_call_0(mt, "array_plain", MIR_T_P);
    MIR_reg_t seed_idx = first->index_name ? emit_call_0(mt, "array_plain", MIR_T_P) : 0;
    mir_join_collect_source(mt, first, seed_rows, 0, false, seed_idx);
    MIR_reg_t seed_idx_item = seed_idx ? emit_box_container(mt, seed_idx) : emit_null_item_reg(mt);
    MIR_reg_t tuples = emit_call_4(mt, "fn_join_seed_tuples", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, seed_rows)),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, mir_join_name_item(mt, first->name)),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, mir_join_idx_name_item(mt, first)),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, seed_idx_item));

    for (AstLoopNode* cur = (AstLoopNode*)first->next; cur; cur = (AstLoopNode*)cur->next) {
        MIR_reg_t rows = emit_call_0(mt, "array_plain", MIR_T_P);
        MIR_reg_t cur_idx = cur->index_name ? emit_call_0(mt, "array_plain", MIR_T_P) : 0;

        if (!cur->on) {
            // cross-product stage: expand every prior tuple by every row of this source.
            mir_join_collect_source(mt, cur, rows, 0, false, cur_idx);
            MIR_reg_t cur_idx_item = cur_idx ? emit_box_container(mt, cur_idx) : emit_null_item_reg(mt);
            tuples = emit_call_5(mt, "fn_cross_join_tuples", MIR_T_P,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, tuples)),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, rows)),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mir_join_name_item(mt, cur->name)),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mir_join_idx_name_item(mt, cur)),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_idx_item));
            continue;
        }

        // hash-join stage.
        MIR_reg_t row_keys = emit_call_0(mt, "array_plain", MIR_T_P);
        mir_join_collect_source(mt, cur, rows, row_keys, true, cur_idx);

        MIR_reg_t prior_keys = emit_call_0(mt, "array_plain", MIR_T_P);
        MIR_reg_t tuple_stream_item = emit_box_container(mt, tuples);
        MIR_reg_t tuple_len = emit_call_1(mt, "fn_len", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, tuple_stream_item));
        MIR_reg_t pidx = new_reg(mt, "jpidx", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pidx),
            MIR_new_int_op(mt->ctx, 0)));
        MIR_label_t l_prior = new_label(mt);
        MIR_label_t l_prior_end = new_label(mt);
        emit_label(mt, l_prior);
        MIR_reg_t pcmp = new_reg(mt, "jpcmp", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, pcmp),
            MIR_new_reg_op(mt->ctx, pidx), MIR_new_reg_op(mt->ctx, tuple_len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_prior_end),
            MIR_new_reg_op(mt->ctx, pcmp)));
        MIR_reg_t tuple_item = emit_call_2(mt, "item_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, tuple_stream_item),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, pidx));
        mir_join_bind_tuple_vars(mt, first, cur, tuple_item);
        MIR_reg_t prior_key = mir_join_key_item(mt, cur, false);
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, prior_keys),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, prior_key));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, pidx),
            MIR_new_reg_op(mt->ctx, pidx), MIR_new_int_op(mt->ctx, 1)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_prior)));
        emit_label(mt, l_prior_end);

        MIR_reg_t cur_idx_item = cur_idx ? emit_box_container(mt, cur_idx) : emit_null_item_reg(mt);
        tuples = emit_call_8(mt, "fn_hash_join_tuples", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, tuples)),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, prior_keys)),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, rows)),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, row_keys)),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, mir_join_name_item(mt, cur->name)),
            MIR_T_I64, MIR_new_int_op(mt->ctx, cur->optional ? 1 : 0),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, mir_join_idx_name_item(mt, cur)),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_idx_item));
    }

    MIR_reg_t final_stream_item = emit_box_container(mt, tuples);
    MIR_reg_t final_len = emit_call_1(mt, "fn_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, final_stream_item));
    MIR_reg_t idx = new_reg(mt, "joutidx", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));
    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_continue = new_label(mt);
    MIR_label_t l_end = new_label(mt);
    if (mt->loop_depth < 31) {
        mt->loop_stack[mt->loop_depth].continue_label = l_continue;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_stack[mt->loop_depth].task_scope_base = mt->in_async_proc
            ? emit_call_0(mt, "lambda_task_scope_current", MIR_T_P) : 0;
        mt->loop_depth++;
    }

    emit_label(mt, l_loop);
    MIR_reg_t cmp = new_reg(mt, "joutcmp", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, final_len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));
    MIR_reg_t tuple_item = emit_call_2(mt, "item_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, final_stream_item),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    mir_join_bind_tuple_vars(mt, first, NULL, tuple_item);

    if (for_node->let_clause) {
        AstNode* lc = for_node->let_clause;
        while (lc) {
            AstNamedNode* let_node = (AstNamedNode*)lc;
            if (let_node->as) {
                MIR_reg_t val = transpile_expr(mt, let_node->as);
                char lc_name[128];
                snprintf(lc_name, sizeof(lc_name), "%.*s", (int)let_node->name->len, let_node->name->chars);
                TypeId lc_tid = get_effective_type(mt, let_node->as);
                MIR_type_t lc_mtype = type_to_mir(lc_tid);
                set_var(mt, lc_name, val, lc_mtype, lc_tid);
            }
            lc = lc->next;
        }
    }

    if (for_node->where) {
        MIR_reg_t where_val = transpile_expr(mt, for_node->where);
        TypeId where_tid = get_effective_type(mt, for_node->where);
        MIR_reg_t where_test = where_val;
        if (where_tid != LMD_TYPE_BOOL) {
            MIR_reg_t boxw = emit_box(mt, where_val, where_tid);
            where_test = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxw)));
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_continue),
            MIR_new_reg_op(mt->ctx, where_test)));
    }

    MIR_reg_t body_result = transpile_expr(mt, for_node->then);
    TypeId body_tid = get_effective_type(mt, for_node->then);
    MIR_reg_t boxed_result = body_result ? emit_box(mt, body_result, body_tid) : emit_null_item_reg(mt);
    emit_call_void_2(mt, "array_push_spread", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_result));

    if (has_order) {
        AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
        MIR_reg_t key_val = transpile_expr(mt, first_spec->expr);
        TypeId key_tid = get_effective_type(mt, first_spec->expr);
        MIR_reg_t boxed_key = emit_box(mt, key_val, key_tid);
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, keys_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_key));
    }

    emit_label(mt, l_continue);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));
    emit_label(mt, l_end);
    if (mt->loop_depth > 0) mt->loop_depth--;

    return mir_finalize_for_output(mt, for_node, output, keys_arr);
}

static MIR_reg_t transpile_for(MirTranspiler* mt, AstForNode* for_node) {
    push_scope(mt);

    AstLoopNode* loop = (AstLoopNode*)for_node->loop;
    if (!loop) {
        log_error("mir: for without loop");
        pop_scope(mt);
        MIR_reg_t r = new_reg(mt, "fornull", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }

    if (mir_for_has_join_clause(for_node)) {
        MIR_reg_t result = transpile_for_join(mt, for_node, loop);
        pop_scope(mt);
        return result;
    }

    if (for_node->group) {
        if (loop->next) {
            log_error("mir: group by currently supports one source; joined tuple grouping ships with join on");
            pop_scope(mt);
            MIR_reg_t r = new_reg(mt, "forgerr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR)));
            return r;
        }

        int key_filter = (int)loop->key_filter;
        bool key_only = loop->key_only;
        MIR_reg_t collection = transpile_expr(mt, loop->as);
        TypeId coll_tid = get_effective_type(mt, loop->as);
        MIR_reg_t boxed_coll = emit_box(mt, collection, coll_tid);
        MIR_reg_t keys_al = emit_call_1(mt, "item_keys", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));
        MIR_reg_t len = emit_call_3(mt, "iter_len", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
            MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
            MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));

        MIR_reg_t output = emit_call_0(mt, "array_spreadable", MIR_T_P);
        bool has_order = for_node->order != NULL;
        bool has_limit = for_node->limit != NULL;
        bool has_offset = for_node->offset != NULL;
        MIR_reg_t keys_arr = 0;
        if (has_order) keys_arr = emit_call_0(mt, "array_plain", MIR_T_P);
        MIR_reg_t group_rows = emit_call_0(mt, "array_plain", MIR_T_P);
        MIR_reg_t group_keys = emit_call_0(mt, "array_plain", MIR_T_P);

        MIR_reg_t idx = new_reg(mt, "gidx", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
            MIR_new_int_op(mt->ctx, 0)));

        MIR_label_t l_loop = new_label(mt);
        MIR_label_t l_continue = new_label(mt);
        MIR_label_t l_end = new_label(mt);
        emit_label(mt, l_loop);
        MIR_reg_t cmp = new_reg(mt, "gcmp", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, cmp),
            MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
            MIR_new_reg_op(mt->ctx, cmp)));

        MIR_reg_t current_item = emit_call_4(mt, key_only ? "iter_key_at" : "iter_val_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
            MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
            MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));

        TypeId val_tid = loop->type ? loop->type->type_id : LMD_TYPE_ANY;
        MIR_type_t val_mtype = MIR_T_I64;
        MIR_reg_t val_reg = current_item;
        if (val_tid == LMD_TYPE_FLOAT) {
            val_reg = emit_unbox(mt, current_item, LMD_TYPE_FLOAT);
            val_mtype = MIR_T_D;
        } else if (val_tid == LMD_TYPE_INT) {
            val_reg = emit_unbox(mt, current_item, LMD_TYPE_INT);
        } else if (val_tid == LMD_TYPE_INT64) {
            val_reg = emit_unbox(mt, current_item, LMD_TYPE_INT64);
        } else if (val_tid == LMD_TYPE_STRING) {
            val_reg = emit_unbox(mt, current_item, LMD_TYPE_STRING);
            val_mtype = MIR_T_P;
        }
        char var_name[128];
        snprintf(var_name, sizeof(var_name), "%.*s", (int)loop->name->len, loop->name->chars);
        set_var(mt, var_name, val_reg, val_mtype, val_tid);

        if (loop->index_name) {
            char idx_name[128];
            snprintf(idx_name, sizeof(idx_name), "%.*s", (int)loop->index_name->len, loop->index_name->chars);
            MIR_reg_t key_item = emit_call_4(mt, "iter_key_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
                MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
                MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));
            set_var(mt, idx_name, key_item, MIR_T_I64, LMD_TYPE_ANY);
        }

        if (for_node->let_clause) {
            AstNode* lc = for_node->let_clause;
            while (lc) {
                AstNamedNode* let_node = (AstNamedNode*)lc;
                if (let_node->as) {
                    MIR_reg_t val = transpile_expr(mt, let_node->as);
                    char lc_name[128];
                    snprintf(lc_name, sizeof(lc_name), "%.*s", (int)let_node->name->len, let_node->name->chars);
                    TypeId lc_tid = get_effective_type(mt, let_node->as);
                    MIR_type_t lc_mtype = type_to_mir(lc_tid);
                    set_var(mt, lc_name, val, lc_mtype, lc_tid);
                }
                lc = lc->next;
            }
        }

        if (for_node->where) {
            MIR_reg_t where_val = transpile_expr(mt, for_node->where);
            TypeId where_tid = get_effective_type(mt, for_node->where);
            MIR_reg_t where_test = where_val;
            if (where_tid != LMD_TYPE_BOOL) {
                MIR_reg_t boxw = emit_box(mt, where_val, where_tid);
                where_test = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxw)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_continue),
                MIR_new_reg_op(mt->ctx, where_test)));
        }

        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, group_rows),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, current_item));

        int key_count = 0;
        for (AstGroupKey* gk = for_node->group->keys; gk; gk = (AstGroupKey*)gk->next) key_count++;
        MIR_reg_t group_key_item = 0;
        if (key_count <= 1) {
            AstGroupKey* gk = for_node->group->keys;
            MIR_reg_t key_val = gk && gk->expr ? transpile_expr(mt, gk->expr) : 0;
            TypeId key_tid = gk && gk->expr ? get_effective_type(mt, gk->expr) : LMD_TYPE_NULL;
            group_key_item = key_val ? emit_box(mt, key_val, key_tid) : emit_null_item_reg(mt);
        } else {
            MIR_reg_t tuple = emit_call_0(mt, "array_plain", MIR_T_P);
            for (AstGroupKey* gk = for_node->group->keys; gk; gk = (AstGroupKey*)gk->next) {
                MIR_reg_t key_val = transpile_expr(mt, gk->expr);
                TypeId key_tid = get_effective_type(mt, gk->expr);
                MIR_reg_t boxed_key = emit_box(mt, key_val, key_tid);
                emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, tuple),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_key));
            }
            group_key_item = emit_box_container(mt, tuple);
        }
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, group_keys),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, group_key_item));

        emit_label(mt, l_continue);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
            MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));
        emit_label(mt, l_end);
        emit_call_void_1(mt, "symbol_key_list_free", MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al));

        MIR_reg_t aliases_arr = emit_call_0(mt, "array_plain", MIR_T_P);
        for (AstGroupKey* gk = for_node->group->keys; gk; gk = (AstGroupKey*)gk->next) {
            const char* alias_chars = gk->alias ? gk->alias->chars : "";
            MIR_reg_t alias_ptr = emit_load_string_literal(mt, alias_chars);
            MIR_reg_t alias_str = emit_call_1(mt, "heap_create_name", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(mt->ctx, alias_ptr));
            MIR_reg_t alias_item = emit_box_string(mt, alias_str);
            emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, aliases_arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, alias_item));
        }

        MIR_reg_t groups = emit_call_3(mt, "fn_group_by_keys_items", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, group_rows)),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, group_keys)),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box_container(mt, aliases_arr)));

        // The row-collection scope ends at group materialization; post-group code may only see `into`.
        pop_scope(mt);
        push_scope(mt);

        MIR_reg_t out_idx = new_reg(mt, "goutidx", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, out_idx),
            MIR_new_int_op(mt->ctx, 0)));
        MIR_reg_t groups_item = emit_box_container(mt, groups);
        MIR_reg_t groups_len = emit_call_1(mt, "fn_len", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, groups_item));
        MIR_label_t l_out_loop = new_label(mt);
        MIR_label_t l_out_end = new_label(mt);
        emit_label(mt, l_out_loop);
        MIR_reg_t out_cmp = new_reg(mt, "goutcmp", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, out_cmp),
            MIR_new_reg_op(mt->ctx, out_idx), MIR_new_reg_op(mt->ctx, groups_len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_out_end),
            MIR_new_reg_op(mt->ctx, out_cmp)));

        MIR_reg_t group_item = emit_call_2(mt, "item_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, groups_item),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, out_idx));
        MIR_reg_t group_el = emit_unbox(mt, group_item, LMD_TYPE_ELEMENT);
        char group_name[128];
        snprintf(group_name, sizeof(group_name), "%.*s", (int)for_node->group->name->len, for_node->group->name->chars);
        set_var(mt, group_name, group_el, MIR_T_P, LMD_TYPE_ELEMENT);

        MIR_reg_t body_result = transpile_expr(mt, for_node->then);
        TypeId body_tid = get_effective_type(mt, for_node->then);
        MIR_reg_t boxed_result = body_result ? emit_box(mt, body_result, body_tid) : emit_null_item_reg(mt);
        emit_call_void_2(mt, "array_push_spread", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_result));

        if (has_order) {
            AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
            MIR_reg_t key_val = transpile_expr(mt, first_spec->expr);
            TypeId key_tid = get_effective_type(mt, first_spec->expr);
            MIR_reg_t boxed_key = emit_box(mt, key_val, key_tid);
            emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, keys_arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_key));
        }

        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, out_idx),
            MIR_new_reg_op(mt->ctx, out_idx), MIR_new_int_op(mt->ctx, 1)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_out_loop)));
        emit_label(mt, l_out_end);

        MIR_reg_t final_reg;
        if (has_order) {
            AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
            emit_call_void_3(mt, "fn_sort_by_keys",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, output),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, keys_arr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, first_spec->descending ? 1 : 0));
        }
        if (has_order && (has_offset || has_limit)) {
            if (has_offset) {
                MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
                TypeId off_tid = get_effective_type(mt, for_node->offset);
                MIR_reg_t off_raw = is_integer_type_id(off_tid)
                    ? off_val : emit_unbox(mt, emit_box(mt, off_val, off_tid), LMD_TYPE_INT);
                MIR_reg_t off_masked = emit_unbox_int_mask(mt, off_raw);
                emit_call_void_2(mt, "array_drop_inplace", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, off_masked));
            }
            if (has_limit) {
                MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
                TypeId lim_tid = get_effective_type(mt, for_node->limit);
                MIR_reg_t lim_raw = is_integer_type_id(lim_tid)
                    ? lim_val : emit_unbox(mt, emit_box(mt, lim_val, lim_tid), LMD_TYPE_INT);
                MIR_reg_t lim_masked = emit_unbox_int_mask(mt, lim_raw);
                emit_call_void_2(mt, for_node->limit_from_end ? "array_limit_last_inplace" : "array_limit_inplace",
                    MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, lim_masked));
            }
            final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
        } else if (has_offset || has_limit) {
            MIR_reg_t cur_result = emit_box_container(mt, output);
            if (has_offset) {
                MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
                TypeId off_tid = get_effective_type(mt, for_node->offset);
                cur_result = emit_call_2(mt, "fn_drop", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box(mt, off_val, off_tid)));
            }
            if (has_limit) {
                MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
                TypeId lim_tid = get_effective_type(mt, for_node->limit);
                cur_result = emit_call_2(mt, for_node->limit_from_end ? "fn_take_last" : "fn_take", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, emit_box(mt, lim_val, lim_tid)));
            }
            final_reg = cur_result;
        } else {
            final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
        }
        pop_scope(mt);
        return final_reg;
    }

    int key_filter = (int)loop->key_filter;  // 0=ALL, 1=INT, 2=SYMBOL
    bool key_only = loop->key_only;

    // Evaluate collection
    MIR_reg_t collection = transpile_expr(mt, loop->as);
    TypeId coll_tid = get_effective_type(mt, loop->as);

    // Box the collection
    MIR_reg_t boxed_coll = emit_box(mt, collection, coll_tid);

    // Get keys (for maps/elements/objects - returns ArrayList* or NULL for arrays)
    MIR_reg_t keys_al = emit_call_1(mt, "item_keys", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll));

    // Get unified iteration length via iter_len(data, keys, key_filter)
    MIR_reg_t len = emit_call_3(mt, "iter_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));

    // Create spreadable output array
    MIR_reg_t output = emit_call_0(mt, "array_spreadable", MIR_T_P);

    // If order by is present, allocate a parallel keys array
    bool has_order = for_node->order != NULL;
    bool has_limit = for_node->limit != NULL;
    bool has_offset = for_node->offset != NULL;
    MIR_reg_t keys_arr = 0;
    if (has_order) {
        keys_arr = emit_call_0(mt, "array_plain", MIR_T_P);
    }

    // Index counter
    MIR_reg_t idx = new_reg(mt, "idx", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_continue = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    // Push loop labels for break/continue
    if (mt->loop_depth < 31) {
        mt->loop_stack[mt->loop_depth].continue_label = l_continue;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_stack[mt->loop_depth].task_scope_base = mt->in_async_proc
            ? emit_call_0(mt, "lambda_task_scope_current", MIR_T_P) : 0;
        mt->loop_depth++;
    }

    emit_label(mt, l_loop);
    // Exit when idx >= len
    MIR_reg_t cmp = new_reg(mt, "cmp", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));

    // Get current loop item; `for k at item` binds keys, while `in` binds values.
    MIR_reg_t current_item = emit_call_4(mt, key_only ? "iter_key_at" : "iter_val_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));

    // Determine the proper type for the loop variable from AST
    TypeId val_tid = loop->type ? loop->type->type_id : LMD_TYPE_ANY;
    MIR_type_t val_mtype = MIR_T_I64;
    MIR_reg_t val_reg = current_item;

    // For known element types, unbox to the proper MIR type
    if (val_tid == LMD_TYPE_FLOAT) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_FLOAT);
        val_mtype = MIR_T_D;
    } else if (val_tid == LMD_TYPE_INT) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_INT);
        val_mtype = MIR_T_I64;
    } else if (val_tid == LMD_TYPE_INT64) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_INT64);
        val_mtype = MIR_T_I64;
    } else if (val_tid == LMD_TYPE_STRING) {
        val_reg = emit_unbox(mt, current_item, LMD_TYPE_STRING);
        val_mtype = MIR_T_P;
    }

    // Bind loop value variable
    char var_name[128];
    snprintf(var_name, sizeof(var_name), "%.*s", (int)loop->name->len, loop->name->chars);
    set_var(mt, var_name, val_reg, val_mtype, val_tid);

    // Bind index/key variable if present
    if (loop->index_name) {
        char idx_name[128];
        snprintf(idx_name, sizeof(idx_name), "%.*s", (int)loop->index_name->len, loop->index_name->chars);

        // Get key via iter_key_at(data, keys, idx, key_filter)
        MIR_reg_t key_item = emit_call_4(mt, "iter_key_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_coll),
            MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
            MIR_T_I64, MIR_new_int_op(mt->ctx, key_filter));
        set_var(mt, idx_name, key_item, MIR_T_I64, LMD_TYPE_ANY);
    }

    // Handle nested loops (multi-variable for: for (a in X, b in Y, ...))
    // Each additional loop variable creates a nested inner loop (cross product)
    struct NestedLoopInfo { MIR_reg_t nidx; MIR_reg_t nlen; MIR_label_t nloop; MIR_label_t ncont; MIR_label_t nend; };
    NestedLoopInfo nested_loops[8];
    int nested_count = 0;

    AstNode* next_loop = loop->next;
    while (next_loop && nested_count < 8) {
        AstLoopNode* nl = (AstLoopNode*)next_loop;

        // Evaluate and box the nested collection
        MIR_reg_t nl_src = transpile_expr(mt, nl->as);
        TypeId nl_tid = get_effective_type(mt, nl->as);
        MIR_reg_t nl_boxed = emit_box(mt, nl_src, nl_tid);

        // Get length
        MIR_reg_t nl_len = emit_call_1(mt, "fn_len", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, nl_boxed));

        // Inner index counter
        MIR_reg_t nidx = new_reg(mt, "nidx", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nidx),
            MIR_new_int_op(mt->ctx, 0)));

        MIR_label_t nl_loop = new_label(mt);
        MIR_label_t nl_cont = new_label(mt);
        MIR_label_t nl_end = new_label(mt);

        emit_label(mt, nl_loop);
        // Exit when nidx >= nl_len
        MIR_reg_t nl_cmp = new_reg(mt, "nlcmp", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, nl_cmp),
            MIR_new_reg_op(mt->ctx, nidx), MIR_new_reg_op(mt->ctx, nl_len)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, nl_end),
            MIR_new_reg_op(mt->ctx, nl_cmp)));

        // Get current item: item_at(nl_src, nidx)
        MIR_reg_t nl_item = emit_call_2(mt, "item_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, nl_boxed),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, nidx));

        // Bind nested loop variable
        char nl_name[128];
        snprintf(nl_name, sizeof(nl_name), "%.*s", (int)nl->name->len, nl->name->chars);
        set_var(mt, nl_name, nl_item, MIR_T_I64, LMD_TYPE_ANY);

        // Bind index variable if present
        if (nl->index_name) {
            char nl_idx_name[128];
            snprintf(nl_idx_name, sizeof(nl_idx_name), "%.*s", (int)nl->index_name->len, nl->index_name->chars);
            set_var(mt, nl_idx_name, nidx, MIR_T_I64, LMD_TYPE_INT);
        }

        nested_loops[nested_count++] = {nidx, nl_len, nl_loop, nl_cont, nl_end};
        next_loop = next_loop->next;
    }

    // Determine the innermost continue label for where clause
    MIR_label_t innermost_continue = (nested_count > 0)
        ? nested_loops[nested_count - 1].ncont : l_continue;

    // Process let clauses (additional variable bindings)
    if (for_node->let_clause) {
        AstNode* lc = for_node->let_clause;
        while (lc) {
            AstNamedNode* let_node = (AstNamedNode*)lc;
            if (let_node->as) {
                MIR_reg_t val = transpile_expr(mt, let_node->as);
                char lc_name[128];
                snprintf(lc_name, sizeof(lc_name), "%.*s", (int)let_node->name->len, let_node->name->chars);
                // for-let values may be boxed by numeric overflow-safe helpers;
                // track the emitted representation so later uses do not unbox the wrong shape.
                TypeId lc_tid = get_effective_type(mt, let_node->as);
                MIR_type_t lc_mtype = type_to_mir(lc_tid);
                set_var(mt, lc_name, val, lc_mtype, lc_tid);
            }
            lc = lc->next;
        }
    }

    // Where clause — skip to innermost continue on failure
    if (for_node->where) {
        MIR_reg_t where_val = transpile_expr(mt, for_node->where);
        TypeId where_tid = get_effective_type(mt, for_node->where);
        MIR_reg_t where_test = where_val;
        if (where_tid != LMD_TYPE_BOOL) {
            MIR_reg_t boxw = emit_box(mt, where_val, where_tid);
            where_test = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxw)));
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, innermost_continue),
            MIR_new_reg_op(mt->ctx, where_test)));
    }

    // Body expression
    MIR_reg_t body_result = transpile_expr(mt, for_node->then);
    TypeId body_tid = get_effective_type(mt, for_node->then);
    // A pure-statement body (e.g. `arr[i] = v` in a `for i in a to b { ... }`
    // statement) produces no value — transpile returns reg 0 (the invalid-reg
    // sentinel). Boxing it would emit an undeclared register. Substitute a null
    // Item instead; the for-statement's collected spreadable result is discarded
    // in procedural context anyway.
    MIR_reg_t boxed_result;
    if (body_result == 0) {
        boxed_result = new_reg(mt, "for_void_null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, boxed_result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    } else {
        boxed_result = emit_box(mt, body_result, body_tid);
    }

    // Push to output (use spread to flatten nested spreadable arrays)
    emit_call_void_2(mt, "array_push_spread", MIR_T_P, MIR_new_reg_op(mt->ctx, output),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_result));

    // If order by is present, also push the sort key value
    if (has_order) {
        AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
        MIR_reg_t key_val = transpile_expr(mt, first_spec->expr);
        TypeId key_tid = get_effective_type(mt, first_spec->expr);
        MIR_reg_t boxed_key = emit_box(mt, key_val, key_tid);
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, keys_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_key));
    }

    // Close nested loops (in reverse order, innermost first)
    for (int ni = nested_count - 1; ni >= 0; ni--) {
        emit_label(mt, nested_loops[ni].ncont);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, nested_loops[ni].nidx),
            MIR_new_reg_op(mt->ctx, nested_loops[ni].nidx), MIR_new_int_op(mt->ctx, 1)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, nested_loops[ni].nloop)));
        emit_label(mt, nested_loops[ni].nend);
    }

    // Continue: increment outer index
    emit_label(mt, l_continue);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));

    emit_label(mt, l_end);
    emit_call_void_1(mt, "symbol_key_list_free",
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al));

    // Post-processing: ORDER BY, then OFFSET, then LIMIT
    MIR_reg_t final_reg;

    if (has_order) {
        // Sort arr_out in-place by the collected keys, with ascending/descending flag
        AstOrderSpec* first_spec = (AstOrderSpec*)for_node->order;
        int64_t desc_flag = first_spec->descending ? 1 : 0;
        emit_call_void_3(mt, "fn_sort_by_keys",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, output),  // cast Array* to Item
            MIR_T_I64, MIR_new_reg_op(mt->ctx, keys_arr), // cast Array* to Item
            MIR_T_I64, MIR_new_int_op(mt->ctx, desc_flag));
    }

    if (has_order && (has_offset || has_limit)) {
        // When order by + offset/limit, apply them in-place on arr_out
        if (has_offset) {
            MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
            TypeId off_tid = get_effective_type(mt, for_node->offset);
            MIR_reg_t off_raw = off_val;
            if (!is_integer_type_id(off_tid)) {
                off_raw = emit_unbox(mt, emit_box(mt, off_val, off_tid), LMD_TYPE_INT);
            }
            // Strip tag bits from int value
            MIR_reg_t off_masked = emit_unbox_int_mask(mt, off_raw);
            emit_call_void_2(mt, "array_drop_inplace",
                MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, off_masked));
        }
        if (has_limit) {
            MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
            TypeId lim_tid = get_effective_type(mt, for_node->limit);
            MIR_reg_t lim_raw = lim_val;
            if (!is_integer_type_id(lim_tid)) {
                lim_raw = emit_unbox(mt, emit_box(mt, lim_val, lim_tid), LMD_TYPE_INT);
            }
            MIR_reg_t lim_masked = emit_unbox_int_mask(mt, lim_raw);
            emit_call_void_2(mt, for_node->limit_from_end
                    ? "array_limit_last_inplace"
                    : "array_limit_inplace",
                MIR_T_P, MIR_new_reg_op(mt->ctx, output),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, lim_masked));
        }
        // Finalize via array_end
        final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
    } else if (has_offset || has_limit) {
        // Without order by, use fn_drop/fn_take which return spreadable results

        MIR_reg_t cur_result = emit_box_container(mt, output); // cast Array* → Item
        if (has_offset) {
            MIR_reg_t off_val = transpile_expr(mt, for_node->offset);
            TypeId off_tid = get_effective_type(mt, for_node->offset);
            MIR_reg_t off_boxed = emit_box(mt, off_val, off_tid);
            cur_result = emit_call_2(mt, "fn_drop", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, off_boxed));
        }
        if (has_limit) {
            MIR_reg_t lim_val = transpile_expr(mt, for_node->limit);
            TypeId lim_tid = get_effective_type(mt, for_node->limit);
            MIR_reg_t lim_boxed = emit_box(mt, lim_val, lim_tid);
            cur_result = emit_call_2(mt, for_node->limit_from_end ? "fn_take_last" : "fn_take", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_result),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, lim_boxed));
        }
        final_reg = cur_result;
    } else {
        // No post-processing — just finalize via array_end
        final_reg = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, output));
    }

    if (mt->loop_depth > 0) mt->loop_depth--;
    pop_scope(mt);

    // If array_end returned ITEM_NULL_SPREADABLE (empty for-expr),
    // convert to a proper empty Array* so [for (x in []) x] returns []
    uint64_t NULL_SPREAD = (uint64_t)LMD_TYPE_NULL << 56 | 1;
    MIR_reg_t is_spread = new_reg(mt, "sns", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_spread),
        MIR_new_reg_op(mt->ctx, final_reg), MIR_new_int_op(mt->ctx, (int64_t)NULL_SPREAD)));
    MIR_label_t l_not_spread = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_not_spread),
        MIR_new_reg_op(mt->ctx, is_spread)));
    // Use the Array* pointer directly — it's a valid empty container
    MIR_reg_t empty_arr = emit_box_container(mt, output);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, final_reg),
        MIR_new_reg_op(mt->ctx, empty_arr)));
    emit_label(mt, l_not_spread);

    return final_reg;
}

// ============================================================================
// While statement
// ============================================================================

static MIR_reg_t transpile_while(MirTranspiler* mt, AstWhileNode* while_node) {
    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    if (mt->loop_depth < 31) {
        mt->loop_stack[mt->loop_depth].continue_label = l_loop;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_stack[mt->loop_depth].task_scope_base = mt->in_async_proc
            ? emit_call_0(mt, "lambda_task_scope_current", MIR_T_P) : 0;
        mt->loop_depth++;
    }

    push_scope(mt);

    emit_label(mt, l_loop);

    // Condition — use get_effective_type to detect boxed values from runtime
    // calls (e.g., fn_gt returns boxed bool when operands are ANY)
    MIR_reg_t cond = transpile_expr(mt, while_node->cond);
    TypeId cond_tid = get_effective_type(mt, while_node->cond);
    MIR_reg_t cond_val = cond;
    if (cond_tid != LMD_TYPE_BOOL) {
        MIR_reg_t boxed = emit_box(mt, cond, cond_tid);
        cond_val = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed)));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cond_val)));

    // Body
    transpile_expr(mt, while_node->body);

    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));
    emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
    pop_scope(mt);

    MIR_reg_t r = new_reg(mt, "while_null", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    return r;
}

// ============================================================================
// Let/pub statements
// ============================================================================

static void transpile_let_stam(MirTranspiler* mt, AstLetNode* let_node) {
    AstNode* declare = let_node->declare;
    while (declare) {
        if (declare->node_type == AST_NODE_ASSIGN) {
            AstNamedNode* asn = (AstNamedNode*)declare;
            if (asn->as) {
                bool saved_preserve_proc_if_result = mt->preserve_proc_if_result;
                if (mt->in_proc && asn->as->node_type == AST_NODE_IF_EXPR) {
                    // let/var initializers consume the if-expression value; without
                    // this, proc-mode branch boxing treats it like a discarded statement.
                    mt->preserve_proc_if_result = true;
                }
                MIR_reg_t val = transpile_expr(mt, asn->as);
                mt->preserve_proc_if_result = saved_preserve_proc_if_result;
                char name_buf[128];
                snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
                TypeId expr_tid = get_effective_type(mt, asn->as);
                bool has_type_annotation = asn->entry && asn->entry->has_type_annotation;
                if (!has_type_annotation && declare->type && asn->as &&
                    declare->type != asn->as->type) {
                    TypeId declared_tid = mir_decl_type_id(declare->type);
                    has_type_annotation = (declared_tid == LMD_TYPE_INT ||
                                           declared_tid == LMD_TYPE_INT64 ||
                                           declared_tid == LMD_TYPE_FLOAT ||
                                           declared_tid == LMD_TYPE_NUM_SIZED ||
                                           declared_tid == LMD_TYPE_UINT64);
                }
                Type* declared_value_type = has_type_annotation ? mir_unwrap_decl_type(declare->type) : NULL;
                // Use the variable's declared type if available, otherwise the expression type.
                // But if the expression is boxed ANY (e.g. captured variable), the declared
                // type from AST is stale — use ANY to match the actual runtime value.
                // Also: when declared type is ANY (untyped var) but expression type is concrete,
                // prefer the expression type so type narrowing propagates through assignments.
                TypeId var_tid = declared_value_type ? declared_value_type->type_id : expr_tid;
                if (expr_tid == LMD_TYPE_ANY) var_tid = LMD_TYPE_ANY;
                if (var_tid == LMD_TYPE_ANY && expr_tid != LMD_TYPE_ANY) var_tid = expr_tid;
                // mutable null placeholders often receive boxed heap Items later
                // (`var out = null; out = c.v2`).  Creating a GC root slot only at
                // that later assignment is unsafe because growing the root frame can
                // allocate before the new heap value has been registered.  The AST
                // type field already contains inferred NULL here, so use the source
                // annotation flag to distinguish `var x = null` from `var x: int`.
                if (let_node->node_type == AST_NODE_VAR_STAM &&
                    (!asn->entry || !asn->entry->has_type_annotation) &&
                    expr_tid == LMD_TYPE_NULL) {
                    var_tid = LMD_TYPE_ANY;
                }

                // Detect fill(n, int_val) → narrows variable to ARRAY_INT for inline access.
                // fill() with an int fill value always creates ArrayInt at runtime.
                // The variable still stores a tagged Item (fill returns boxed container);
                // ARRAY_INT type_id tells downstream code what kind of container it is.
                TypeId fill_elem_type = LMD_TYPE_ANY;  // P4-3.1: track fill element type
                {
                    AstNode* rhs = asn->as;
                    // Unwrap PRIMARY wrapper nodes (parenthesized expressions)
                    while (rhs && rhs->node_type == AST_NODE_PRIMARY) {
                        AstPrimaryNode* pri = (AstPrimaryNode*)rhs;
                        if (pri->expr) rhs = pri->expr; else break;
                    }
                    if (var_tid == LMD_TYPE_ANY && rhs && rhs->node_type == AST_NODE_CALL_EXPR) {
                        AstCallNode* call = (AstCallNode*)rhs;
                        if (call->function && call->function->node_type == AST_NODE_SYS_FUNC) {
                            AstSysFuncNode* sys = (AstSysFuncNode*)call->function;
                            if (sys->fn_info && sys->fn_info->fn == SYSFUNC_FILL) {
                                AstNode* arg2 = call->argument ? call->argument->next : nullptr;
                                if (arg2) {
                                    TypeId fill_val_tid = get_effective_type(mt, arg2);
                                    if (is_integer_type_id(fill_val_tid)) {
                                        var_tid = LMD_TYPE_ARRAY_NUM;
                                        fill_elem_type = LMD_TYPE_INT;
                                    } else if (fill_val_tid == LMD_TYPE_FLOAT) {
                                        var_tid = LMD_TYPE_ARRAY_NUM;
                                        fill_elem_type = LMD_TYPE_FLOAT;
                                    } else if (fill_val_tid == LMD_TYPE_BOOL) {
                                        // P4-3.1: fill(n, bool) → generic Array of bools
                                        var_tid = LMD_TYPE_ARRAY;
                                        fill_elem_type = LMD_TYPE_BOOL;
                                    }
                                }
                            }
                        }
                    }
                }

                log_debug("mir: let/var '%s' declare_type=%d expr_tid=%d var_tid=%d",
                    name_buf, declare->type ? (int)declare->type->type_id : -1, expr_tid, var_tid);

                // Runtime typed array coercion for occurrence annotations (int[], float[], etc.)
                // When declared type is TypeUnary and RHS is dynamic or a mismatched typed array,
                // call ensure_typed_array to convert at runtime.
                if (declare->type && declare->type->kind == TYPE_KIND_UNARY) {
                    bool needs_coerce = (expr_tid == LMD_TYPE_ANY || expr_tid == LMD_TYPE_NULL ||
                                         expr_tid == LMD_TYPE_ARRAY ||
                                         expr_tid == LMD_TYPE_ARRAY_NUM);
                    if (needs_coerce) {
                        // extract element type from TypeUnary operand
                        TypeUnary* unary = (TypeUnary*)declare->type;
                        Type* operand = unary->operand;
                        if (operand && operand->type_id == LMD_TYPE_TYPE && operand->kind == TYPE_KIND_SIMPLE) {
                            operand = ((TypeType*)operand)->type;
                        }
                        TypeId elem_tid = operand ? operand->type_id : LMD_TYPE_ANY;
                        // box the expression value to Item if not already boxed
                        MIR_reg_t boxed = emit_box(mt, val, expr_tid);
                        if (elem_tid == LMD_TYPE_NUM_SIZED && operand) {
                            // compact sized array: use ensure_sized_array(item, ArrayNumElemType)
                            ArrayNumElemType et = num_sized_to_elem_type(type_num_sized_kind(operand));
                            val = emit_call_2(mt, "ensure_sized_array", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)et));
                            // failed coercions must halt before a null pointer is stored as an array value.
                            emit_return_item_error_if_zero(mt, val);
                        } else {
                            // call ensure_typed_array(item, element_type_id) → void* (pointer)
                            val = emit_call_2(mt, "ensure_typed_array", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, elem_tid));
                            // failed coercions must halt before a null pointer is stored as an array value.
                            emit_return_item_error_if_zero(mt, val);
                        }
                        // result is a pointer (stored as I64), treat as ANY
                        var_tid = LMD_TYPE_ANY;
                    }
                }

                // Convert value if expression type differs from variable type
                if (declared_value_type && declared_value_type->type_id == LMD_TYPE_NUM_SIZED) {
                    val = emit_coerce_value_to_declared(mt, val, expr_tid, declared_value_type);
                    var_tid = LMD_TYPE_NUM_SIZED;
                    expr_tid = LMD_TYPE_NUM_SIZED;
                } else if (declared_value_type && declared_value_type->type_id == LMD_TYPE_UINT64) {
                    val = emit_coerce_value_to_declared(mt, val, expr_tid, declared_value_type);
                    var_tid = LMD_TYPE_UINT64;
                    expr_tid = LMD_TYPE_UINT64;
                } else if (var_tid == LMD_TYPE_FLOAT && expr_tid == LMD_TYPE_INT) {
                    // int -> float conversion
                    MIR_reg_t fval = new_reg(mt, "i2d", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                        MIR_new_reg_op(mt->ctx, fval), MIR_new_reg_op(mt->ctx, val)));
                    val = fval;
                } else if (var_tid == LMD_TYPE_INT && expr_tid == LMD_TYPE_FLOAT) {
                    // float -> int conversion
                    MIR_reg_t ival = new_reg(mt, "d2i", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_D2I,
                        MIR_new_reg_op(mt->ctx, ival), MIR_new_reg_op(mt->ctx, val)));
                    val = ival;
                }

                bool keep_mutable_alias = let_node->node_type == AST_NODE_VAR_STAM &&
                    mir_var_rhs_keeps_mutable_alias(asn->as);
                if (let_node->node_type == AST_NODE_VAR_STAM &&
                    mir_expr_may_return_container(asn->as, expr_tid, var_tid) &&
                    !keep_mutable_alias) {
                    // Phase 5 COW anchor: a var binding receives its own mutable
                    // container so later interior writes cannot mutate a let alias.
                    // Field/index aliases from mutable roots keep their source
                    // identity; detaching them would break intentional write-back.
                    MIR_reg_t boxed = emit_box(mt, val, var_tid);
                    val = emit_call_1(mt, "fn_mutable_value", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
                    // fn_mutable_value returns a boxed Item; keep the tracked
                    // storage type aligned so later reads/calls use the Item ABI.
                    var_tid = LMD_TYPE_ANY;
                    expr_tid = LMD_TYPE_ANY;
                }

                MIR_type_t mtype = type_to_mir(var_tid);

                // Copy value to a new register so the let binding has its own
                // independent copy. Without this, `let tmp = a` shares a's
                // register, so subsequent mutations to a also affect tmp.
                MIR_reg_t copy = new_reg(mt, "letv", mtype);
                if (mtype == MIR_T_D) {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                        MIR_new_reg_op(mt->ctx, copy), MIR_new_reg_op(mt->ctx, val)));
                } else {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, copy), MIR_new_reg_op(mt->ctx, val)));
                }

                // Store in current scope
                set_var(mt, name_buf, copy, mtype, var_tid);
                if (var_tid == LMD_TYPE_NUM_SIZED && declared_value_type) {
                    MirVarEntry* v = find_var(mt, name_buf);
                    if (v) v->num_type = type_num_sized_kind(declared_value_type);
                }

                // P4-3.1: Set element type for container variables (from fill() narrowing)
                // Only set when var_tid is still ARRAY (not overridden by typed array coercion)
                if (fill_elem_type != LMD_TYPE_ANY && (var_tid == LMD_TYPE_ARRAY || var_tid == LMD_TYPE_ARRAY_NUM)) {
                    MirVarEntry* v = find_var(mt, name_buf);
                    if (v) v->elem_type = fill_elem_type;
                }

                // P4-3.2: Set elem_type for array variables with known nested type,
                // ONLY when the variable is never index-mutated in the current scope.
                // This enables NATIVE return from FAST PATH 1b/1d array indexing.
                // Must check after fill narrowing (which handles fill(n, bool) → BOOL).
                if (var_tid == LMD_TYPE_ARRAY && fill_elem_type == LMD_TYPE_ANY) {
                    // Check AST nested type from array literal or typed array assignment
                    AstNode* rhs = asn->as;
                    while (rhs && rhs->node_type == AST_NODE_PRIMARY)
                        rhs = ((AstPrimaryNode*)rhs)->expr;
                    Type* rhs_type = rhs ? rhs->type : nullptr;
                    if (rhs_type && rhs_type->type_id == LMD_TYPE_ARRAY) {
                        TypeArray* arr_type = (TypeArray*)rhs_type;
                        if (arr_type->nested &&
                            (arr_type->nested->type_id == LMD_TYPE_INT ||
                             arr_type->nested->type_id == LMD_TYPE_BOOL)) {
                            // Check mutation: scan current scope for arr[i] = val
                            if (mt->func_body && !has_index_mutation(name_buf, mt->func_body)) {
                                MirVarEntry* v = find_var(mt, name_buf);
                                if (v && v->elem_type == LMD_TYPE_ANY) {
                                    v->elem_type = arr_type->nested->type_id;
                                }
                            }
                        }
                    }
                }

                // If this is a module-level variable (not inside a user function),
                // store to BSS so other functions can access it
                if (!mt->in_user_func) {
                    GlobalVarEntry* gvar = find_global_var(mt, name_buf);
                    if (gvar) {
                        store_global_var(mt, gvar, val, var_tid);
                    }
                }

                // Handle error destructuring: let a^err = expr
                if (asn->error_name) {
                    // Box the value for error checking
                    MIR_reg_t boxed = emit_box(mt, val, var_tid);
                    // Check if result is an error: item_type_id(boxed) == LMD_TYPE_ERROR
                    // item_type_id returns uint8_t TypeId — zero-extend for clean comparison
                    MIR_reg_t type_id = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed)));
                    MIR_reg_t is_error = new_reg(mt, "iserr", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_error),
                        MIR_new_reg_op(mt->ctx, type_id),
                        MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));

                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    MIR_label_t l_is_err = new_label(mt);
                    MIR_label_t l_end = new_label(mt);

                    // error_var = is_error ? boxed : ITEM_NULL
                    // value_var = is_error ? ITEM_NULL : boxed
                    MIR_reg_t err_result = new_reg(mt, "errv", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, err_result),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_is_err),
                        MIR_new_reg_op(mt->ctx, is_error)));
                    // Non-error path: val = boxed (convert native to boxed Item)
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, val),
                        MIR_new_reg_op(mt->ctx, boxed)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
                    emit_label(mt, l_is_err);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, err_result),
                        MIR_new_reg_op(mt->ctx, boxed)));
                    // If error: value_var = ITEM_NULL
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, val),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    emit_label(mt, l_end);
                    // After both paths, val is a boxed Item (either boxed value or NULL)
                    set_var(mt, name_buf, val, MIR_T_I64, LMD_TYPE_ANY);

                    // Update BSS global if module-level (val may have changed to NULL on error)
                    if (!mt->in_user_func) {
                        GlobalVarEntry* gvar = find_global_var(mt, name_buf);
                        if (gvar) {
                            store_global_var(mt, gvar, val, LMD_TYPE_ANY);
                        }
                    }

                    char err_name_buf[128];
                    snprintf(err_name_buf, sizeof(err_name_buf), "%.*s",
                        (int)asn->error_name->len, asn->error_name->chars);
                    set_var(mt, err_name_buf, err_result, MIR_T_I64, LMD_TYPE_ANY);

                    // Store error var to BSS global at module level
                    if (!mt->in_user_func) {
                        GlobalVarEntry* err_gvar = find_global_var(mt, err_name_buf);
                        if (err_gvar) {
                            store_global_var(mt, err_gvar, err_result, LMD_TYPE_ANY);
                        }
                    }
                }
            }
        } else if (declare->node_type == AST_NODE_DECOMPOSE) {
            AstDecomposeNode* dec = (AstDecomposeNode*)declare;
            if (dec->as && dec->name_count > 0) {
                // Evaluate source expression and box it
                MIR_reg_t src_val = transpile_expr(mt, dec->as);
                TypeId src_tid = get_effective_type(mt, dec->as);
                MIR_reg_t boxed_src = emit_box(mt, src_val, src_tid);

                for (int i = 0; i < dec->name_count; i++) {
                    String* name = dec->names[i];
                    char name_buf[128];
                    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)name->len, name->chars);

                    MIR_reg_t extracted;
                    if (dec->is_named) {
                        // Named decomposition: item_attr(src, "field_name")
                        // item_attr takes (Item, const char*) → Item
                        // Use name->chars directly (persistent name pool pointer)
                        MIR_reg_t name_str = emit_load_string_literal(mt, name->chars);
                        extracted = emit_call_2(mt, "item_attr", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_src),
                            MIR_T_P, MIR_new_reg_op(mt->ctx, name_str));
                    } else {
                        // Positional decomposition: item_at(src, index)
                        extracted = emit_call_2(mt, "item_at", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_src),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, i));
                    }
                    set_var(mt, name_buf, extracted, MIR_T_I64, LMD_TYPE_ANY);

                    // Store to BSS if this is a module-level decomposed variable
                    if (!mt->in_user_func) {
                        GlobalVarEntry* gvar = find_global_var(mt, name_buf);
                        if (gvar) {
                            store_global_var(mt, gvar, extracted, LMD_TYPE_ANY);
                        }
                    }
                }
            }
        } else if (declare->node_type == AST_NODE_FUNC || declare->node_type == AST_NODE_FUNC_EXPR ||
                   declare->node_type == AST_NODE_PROC) {
            // Function definitions are handled in the module-level pre-pass
            // (MIR requires all functions to be defined before generating code)
        } else if (declare->node_type == AST_NODE_OBJECT_TYPE) {
            // pub type Counter { ... } → emit method registration code
            log_debug("mir: transpile_let_stam handling OBJECT_TYPE declare");
            transpile_expr(mt, declare);
        }
        declare = declare->next;
    }
}

// ============================================================================
// Array expressions
// ============================================================================

// Emit MIR that walks nested literal leaves in row-major order, calling
// array_num_set_item for each leaf at sequential flat indices.
static void mir_emit_ndim_leaves(MirTranspiler* mt, AstNode* node, MIR_reg_t arr_reg, int* flat_idx_io) {
    // Unwrap AST_NODE_PRIMARY to reach the inner array node
    while (node && node->node_type == AST_NODE_PRIMARY) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    if (!node || node->node_type != AST_NODE_ARRAY) return;
    AstArrayNode* arr = (AstArrayNode*)node;
    TypeArray* type = (TypeArray*)arr->type;
    bool inner_is_array = type && type->nested && type->nested->type_id == LMD_TYPE_ARRAY;
    AstNode* item = arr->item;
    while (item) {
        if (inner_is_array) {
            mir_emit_ndim_leaves(mt, item, arr_reg, flat_idx_io);
        } else {
            MIR_reg_t val = transpile_expr(mt, item);
            TypeId val_tid = get_effective_type(mt, item);
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            emit_call_void_3(mt, "array_num_set_item",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(*flat_idx_io)),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
            (*flat_idx_io)++;
        }
        item = item->next;
    }
}

static MIR_reg_t transpile_array(MirTranspiler* mt, AstArrayNode* arr_node) {
    // Check if any child is a for-expression, spread, or let binding
    bool has_spreadable = false;
    bool has_pipe_spread = false;  // pipe expressions spread their array results in array literals
    bool has_let = false;
    AstNode* scan = arr_node->item;
    while (scan) {
        if (scan->node_type == AST_NODE_FOR_EXPR || scan->node_type == AST_NODE_SPREAD) {
            has_spreadable = true;
        }
        if (scan->node_type == AST_NODE_PIPE) {
            has_pipe_spread = true;
        }
        if (scan->node_type == AST_NODE_ASSIGN) {
            has_let = true;
        }
        scan = scan->next;
    }
    bool any_spread = has_spreadable || has_pipe_spread;

    // push scope to contain let bindings within the array
    if (has_let) push_scope(mt);

    // Static N-D detection: nested numeric literals like [[1,2],[3,4]] become a
    // single N-D ArrayNum with shape metadata, not an Array of ArrayNums.
    // Detection recurses through inner arrays; all must have same shape & elem_type.
    if (!any_spread && !has_let) {
        int64_t shape[32];
        ArrayNumElemType n_etype;
        int ndim = detect_ndim_literal((AstNode*)arr_node, shape, 32, &n_etype, true);
        if (ndim >= 2) {
            int64_t total = 1;
            for (int i = 0; i < ndim; i++) total *= shape[i];

            // Allocate a heap_data buffer for the dims array (ndim * int64_t).
            int64_t dims_bytes = (int64_t)ndim * (int64_t)sizeof(int64_t);
            MIR_reg_t dims_ptr = emit_call_1(mt, "heap_data_calloc", MIR_T_P,
                MIR_T_I64, MIR_new_int_op(mt->ctx, dims_bytes));
            // Store each dim into dims_ptr[i]
            for (int i = 0; i < ndim; i++) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, (MIR_disp_t)(i * (int)sizeof(int64_t)),
                                   dims_ptr, 0, 1),
                    MIR_new_int_op(mt->ctx, shape[i])));
            }
            // Call helper to build the N-D ArrayNum with shape metadata.
            MIR_reg_t arr = emit_call_4(mt, "array_num_new_ndim", MIR_T_P,
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)n_etype),
                MIR_T_I64, MIR_new_int_op(mt->ctx, total),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ndim),
                MIR_T_P,   MIR_new_reg_op(mt->ctx, dims_ptr));
            // Fill leaves in row-major flat order.
            int flat_idx = 0;
            mir_emit_ndim_leaves(mt, (AstNode*)arr_node, arr, &flat_idx);
            if (has_let) pop_scope(mt);
            return arr;
        }
    }

    // Detect homogeneous typed arrays (ArrayInt, ArrayFloat) to match C transpiler behavior.
    // Vector functions (concat, take, drop, reverse, etc.) dispatch based on runtime type_id,
    // so creating the correct specialized array type is essential.
    TypeArray* arr_type = (TypeArray*)arr_node->type;
    bool is_int_array = arr_type && arr_type->nested && arr_type->nested->type_id == LMD_TYPE_INT;
    bool is_float_array = arr_type && arr_type->nested && arr_type->nested->type_id == LMD_TYPE_FLOAT;
    bool is_sized_array = arr_type && arr_type->nested && arr_type->nested->type_id == LMD_TYPE_NUM_SIZED;
    ArrayNumElemType sized_elem_type = ELEM_INT;  // default, overwritten below
    if (is_sized_array) {
        sized_elem_type = num_sized_to_elem_type(type_num_sized_kind(arr_type->nested));
    }

    // Specialized ArrayFloat path: array_float_new(count) + array_float_set(arr, i, val)
    // Skip specialized paths when array has let bindings (let nodes are transparent)
    if (is_float_array && !any_spread && !has_let && arr_node->item) {
        int count = (int)arr_type->length;
        MIR_reg_t arr = emit_call_1(mt, "array_float_new", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, count));
        int idx = 0;
        AstNode* item = arr_node->item;
        while (item) {
            MIR_reg_t val = transpile_expr(mt, item);
            // val is native double (MIR_T_D) for float literals/expressions,
            // but may be a boxed Item for captured variables (LMD_TYPE_ANY)
            TypeId val_tid = get_effective_type(mt, item);
            if (val_tid == LMD_TYPE_ANY) {
                // unbox Item to double
                val = emit_unbox(mt, val, LMD_TYPE_FLOAT);
            } else if (val_tid != LMD_TYPE_FLOAT) {
                // coerce non-float to double via i2d or unbox
                if (val_tid == LMD_TYPE_INT) {
                    MIR_reg_t dval = new_reg(mt, "i2d", MIR_T_D);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, dval),
                        MIR_new_reg_op(mt->ctx, val)));
                    val = dval;
                }
            }
            emit_call_void_3(mt, "array_float_set",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                MIR_T_D, MIR_new_reg_op(mt->ctx, val));
            idx++;
            item = item->next;
        }
        if (has_let) pop_scope(mt);
        return arr;  // ArrayFloat* is a container pointer
    }

    // Specialized ArrayInt path: array_int_new(count) + array_int_set(arr, i, val)
    // Skip specialized paths when array has let bindings (let nodes are transparent)
    if (is_int_array && !any_spread && !has_let && arr_node->item) {
        int count = (int)arr_type->length;
        MIR_reg_t arr = emit_call_1(mt, "array_int_new", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, count));
        int idx = 0;
        AstNode* item = arr_node->item;
        while (item) {
            MIR_reg_t val = transpile_expr(mt, item);
            // val is native int64 for int literals/expressions,
            // but may be a boxed Item for captured variables (LMD_TYPE_ANY)
            TypeId val_tid = get_effective_type(mt, item);
            if (val_tid == LMD_TYPE_ANY) {
                val = emit_unbox(mt, val, LMD_TYPE_INT);
            }
            emit_call_void_3(mt, "array_int_set",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            idx++;
            item = item->next;
        }
        if (has_let) pop_scope(mt);
        return arr;  // ArrayInt* is a container pointer
    }

    // Specialized compact sized array path: array_num_new(elem_type, count) + array_num_set_item(arr, i, boxed)
    if (is_sized_array && !any_spread && !has_let && arr_node->item) {
        int count = (int)arr_type->length;
        MIR_reg_t arr = emit_call_2(mt, "array_num_new", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sized_elem_type),
            MIR_T_I64, MIR_new_int_op(mt->ctx, count));
        int idx = 0;
        AstNode* item = arr_node->item;
        while (item) {
            MIR_reg_t val = transpile_expr(mt, item);
            TypeId val_tid = get_effective_type(mt, item);
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            emit_call_void_3(mt, "array_num_set_item",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
            idx++;
            item = item->next;
        }
        if (has_let) pop_scope(mt);
        return arr;  // ArrayNum* compact container pointer
    }

    MIR_reg_t static_reg = 0;
    if (emit_static_collection_const(mt, (AstNode*)arr_node, &static_reg)) {
        if (has_let) pop_scope(mt);
        return static_reg;
    }

    // Generic array path
    MIR_reg_t arr = emit_call_0(mt, "array", MIR_T_P);
    int arr_root = create_pointer_gc_root_slot(mt, arr);

    // Handle empty arrays without array_end() which returns ITEM_NULL_SPREADABLE
    if (!arr_node->item) {
        // Array* pointer is already a valid Item (container pointer)
        if (has_let) pop_scope(mt);
        return load_gc_root_slot(mt, arr_root, "arrb");
    }

    AstNode* item = arr_node->item;
    while (item) {
        MIR_reg_t val = transpile_expr(mt, item);

        // let bindings are transparent - evaluate for side effect but don't push to array
        if (item->node_type == AST_NODE_ASSIGN) {
            item = item->next;
            continue;
        }

        TypeId val_tid = get_effective_type(mt, item);

        if (item->node_type == AST_NODE_PIPE) {
            // pipe/that/where exprs in array literals spread their array results
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            int item_root = create_gc_root_slot(mt, boxed);
            boxed = load_gc_root_slot(mt, item_root, "arr_item");
            arr = load_gc_root_slot(mt, arr_root, "arrb");
            emit_call_void_2(mt, "array_push_spread_all",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        } else if (has_spreadable) {
            // When any child is spreadable, use array_push_spread for ALL children
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            int item_root = create_gc_root_slot(mt, boxed);
            boxed = load_gc_root_slot(mt, item_root, "arr_item");
            arr = load_gc_root_slot(mt, arr_root, "arrb");
            emit_call_void_2(mt, "array_push_spread",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        } else if (item->node_type == AST_NODE_SPREAD) {
            // Spread: use array_push_spread
            int item_root = create_gc_root_slot(mt, val);
            val = load_gc_root_slot(mt, item_root, "arr_item");
            arr = load_gc_root_slot(mt, arr_root, "arrb");
            emit_call_void_2(mt, "array_push_spread",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        } else {
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            int item_root = create_gc_root_slot(mt, boxed);
            boxed = load_gc_root_slot(mt, item_root, "arr_item");
            arr = load_gc_root_slot(mt, arr_root, "arrb");
            emit_call_void_2(mt, "array_push",
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        }
        item = item->next;
    }

    arr = load_gc_root_slot(mt, arr_root, "arrb");
    MIR_reg_t arr_result = emit_call_1(mt, "array_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, arr));

    // If array contains spreadable children (for-expressions) and all produce
    // empty results, array_end returns ITEM_NULL_SPREADABLE. For top-level
    // array literals [for ...], convert to proper empty array.
    if (any_spread) {
        uint64_t NULL_SPREAD = (uint64_t)LMD_TYPE_NULL << 56 | 1;
        MIR_reg_t is_sn = new_reg(mt, "sn2", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_sn),
            MIR_new_reg_op(mt->ctx, arr_result), MIR_new_int_op(mt->ctx, (int64_t)NULL_SPREAD)));
        MIR_label_t l_not_sn = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_not_sn),
            MIR_new_reg_op(mt->ctx, is_sn)));
        MIR_reg_t empty_a = emit_box_container(mt, arr);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_result),
            MIR_new_reg_op(mt->ctx, empty_a)));
        emit_label(mt, l_not_sn);
    }

    if (has_let) pop_scope(mt);
    return arr_result;
}

// ============================================================================
// List/content expressions
// ============================================================================

static MIR_reg_t transpile_list(MirTranspiler* mt, AstListNode* list_node) {
    // Check for block expression optimization: 1 value item + declarations
    // In this case, emit declarations then return the single value directly
    AstNode* declare = list_node->declare;
    if (declare) {
        // Count value items
        AstNode* scan = list_node->item;
        int val_count = 0;
        AstNode* last_value = nullptr;
        while (scan) {
            val_count++;
            last_value = scan;
            scan = scan->next;
        }

        if (val_count == 1 && last_value) {
            // Block expression: (let x = 10, x) → process declares, return value
            push_scope(mt);
            while (declare) {
                if (declare->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* asn = (AstNamedNode*)declare;
                    if (asn->as) {
                        MIR_reg_t val = transpile_expr(mt, asn->as);
                        char name_buf[128];
                        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
                        // Use get_effective_type to match actual MIR register type
                        // (raw AST type may say FLOAT while boxed runtime returns I64)
                        TypeId expr_tid = get_effective_type(mt, asn->as);
                        TypeId var_tid = declare->type ? declare->type->type_id : expr_tid;
                        if (expr_tid == LMD_TYPE_ANY) var_tid = LMD_TYPE_ANY;
                        if (var_tid == LMD_TYPE_ANY && expr_tid != LMD_TYPE_ANY) var_tid = expr_tid;
                        // Convert value if expression type differs from variable type
                        if (var_tid == LMD_TYPE_FLOAT && expr_tid == LMD_TYPE_INT) {
                            MIR_reg_t fval = new_reg(mt, "i2d", MIR_T_D);
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                                MIR_new_reg_op(mt->ctx, fval), MIR_new_reg_op(mt->ctx, val)));
                            val = fval;
                        } else if (var_tid == LMD_TYPE_INT && expr_tid == LMD_TYPE_FLOAT) {
                            MIR_reg_t ival = new_reg(mt, "d2i", MIR_T_I64);
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_D2I,
                                MIR_new_reg_op(mt->ctx, ival), MIR_new_reg_op(mt->ctx, val)));
                            val = ival;
                        }
                        set_var(mt, name_buf, val, type_to_mir(var_tid), var_tid);
                    }
                }
                declare = declare->next;
            }
            MIR_reg_t result = transpile_expr(mt, last_value);
            pop_scope(mt);
            return result;
        }

        // Multiple values with declarations: process declares then build list
        push_scope(mt);
        while (declare) {
            if (declare->node_type == AST_NODE_ASSIGN) {
                AstNamedNode* asn = (AstNamedNode*)declare;
                if (asn->as) {
                    MIR_reg_t val = transpile_expr(mt, asn->as);
                    char name_buf[128];
                    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
                    // Use get_effective_type to match actual MIR register type
                    TypeId expr_tid = get_effective_type(mt, asn->as);
                    TypeId var_tid = declare->type ? declare->type->type_id : expr_tid;
                    if (expr_tid == LMD_TYPE_ANY) var_tid = LMD_TYPE_ANY;
                    if (var_tid == LMD_TYPE_ANY && expr_tid != LMD_TYPE_ANY) var_tid = expr_tid;
                    if (var_tid == LMD_TYPE_FLOAT && expr_tid == LMD_TYPE_INT) {
                        MIR_reg_t fval = new_reg(mt, "i2d", MIR_T_D);
                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                            MIR_new_reg_op(mt->ctx, fval), MIR_new_reg_op(mt->ctx, val)));
                        val = fval;
                    } else if (var_tid == LMD_TYPE_INT && expr_tid == LMD_TYPE_FLOAT) {
                        MIR_reg_t ival = new_reg(mt, "d2i", MIR_T_I64);
                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_D2I,
                            MIR_new_reg_op(mt->ctx, ival), MIR_new_reg_op(mt->ctx, val)));
                        val = ival;
                    }
                    set_var(mt, name_buf, val, type_to_mir(var_tid), var_tid);
                }
            }
            declare = declare->next;
        }

        MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
        AstNode* item = list_node->item;
        while (item) {
            MIR_reg_t val = transpile_box_item(mt, item);
            emit_call_void_2(mt, "list_push_spread",
                MIR_T_P, MIR_new_reg_op(mt->ctx, ls),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            item = item->next;
        }
        MIR_reg_t result = emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
        pop_scope(mt);
        return result;
    }

    // No declarations - simple list
    MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);

    AstNode* item = list_node->item;
    while (item) {
        // Skip declarations
        if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
            item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_OBJECT_TYPE ||
            item->node_type == AST_NODE_FUNC ||
            item->node_type == AST_NODE_FUNC_EXPR || item->node_type == AST_NODE_PROC ||
            item->node_type == AST_NODE_STRING_PATTERN || item->node_type == AST_NODE_SYMBOL_PATTERN ||
            item->node_type == AST_NODE_VIEW) {
            item = item->next;
            continue;
        }
        // Use transpile_box_item for proper type-aware boxing
        MIR_reg_t boxed = transpile_box_item(mt, item);
        emit_call_void_2(mt, "list_push_spread",
            MIR_T_P, MIR_new_reg_op(mt->ctx, ls),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        item = item->next;
    }

    return emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
}

// Check if a node type is a declaration (processed separately, not a value)
static bool is_declaration_node(int node_type) {
    switch (node_type) {
    case AST_NODE_LET_STAM: case AST_NODE_PUB_STAM:
    case AST_NODE_TYPE_STAM: case AST_NODE_VAR_STAM:
    case AST_NODE_OBJECT_TYPE:
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_PROC:
    case AST_NODE_STRING_PATTERN: case AST_NODE_SYMBOL_PATTERN:
    case AST_NODE_VIEW:
        return true;
    default:
        return false;
    }
}

// Check if a node type is a procedural side-effect statement.
// These execute for side effects but do NOT produce output values.
// NOTE: IF_EXPR (with block form), WHILE_STAM, FOR_STAM can appear in functional code
// and produce values, so they are NOT included here.
static bool is_side_effect_stam(int node_type) {
    switch (node_type) {
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
    case AST_NODE_RETURN_STAM:
    case AST_NODE_RAISE_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_PIPE_FILE_STAM:
        return true;
    default:
        return false;
    }
}

static bool is_proc_flow_side_effect_node(AstNode* node, AstNode* last_value) {
    return node && node != last_value &&
           (node->node_type == AST_NODE_IF_EXPR ||
            node->node_type == AST_NODE_WHILE_STAM ||
            node->node_type == AST_NODE_FOR_STAM);
}

static bool side_effect_result_can_error(int node_type) {
    switch (node_type) {
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_PIPE_FILE_STAM:
        return true;
    default:
        return false;
    }
}

static void transpile_proc_side_effect(MirTranspiler* mt, AstNode* item) {
    MIR_reg_t stmt_result = transpile_expr(mt, item);
    if (mt->current_func_can_raise && side_effect_result_can_error(item->node_type)) {
        // can-raise procs must not discard failed mutation/helper statements as side effects.
        emit_return_if_item_error(mt, stmt_result);
    }
}

static MIR_reg_t transpile_task_scope_leave(
    MirTranspiler* mt, MIR_reg_t scope, MIR_reg_t block_result, bool error_exit) {
    if (!mt->in_async_proc || !scope) return block_result;
    int state = ++mt->async_next_state;
    if (state > mt->async_state_count || !mt->async_state_labels) {
        log_error("concurrency MIR: scope-state count mismatch at state %d/%d",
            state, mt->async_state_count);
        return block_result;
    }

    int spill_count = mt->async_spill_count;
    MIR_label_t invoke = new_label(mt);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, invoke)));
    emit_label(mt, mt->async_state_labels[state]);
    async_restore_spills(mt, spill_count);
    async_restore_vars(mt);
    emit_label(mt, invoke);
    MIR_reg_t leave_result = emit_call_2(mt, "lambda_task_scope_leave", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, scope),
        MIR_T_I64, MIR_new_int_op(mt->ctx, error_exit ? 1 : 0));
    async_emit_suspended_return(mt, leave_result, state, spill_count);
    return block_result;
}

static MIR_reg_t transpile_content(MirTranspiler* mt, AstListNode* list_node) {
    // TCO: content block items are NOT in tail position. Only return statements
    // (which set in_tail_position=true for their value) should be considered tail.
    mt->in_tail_position = false;
    MIR_reg_t task_scope = mt->in_async_proc
        ? emit_call_0(mt, "lambda_task_scope_enter", MIR_T_P) : 0;

    // Detect proc context: either we're inside a pn function (mt->in_proc),
    // or the content block contains proc-only nodes like VAR_STAM.
    // In proc context, loops are side-effect statements, and only the LAST
    // value expression contributes to the result.
    bool is_proc = mt->in_proc;
    if (!is_proc) {
        AstNode* scan = list_node->item;
        while (scan) {
            if (scan->node_type == AST_NODE_VAR_STAM) { is_proc = true; break; }
            scan = scan->next;
        }
    }

    // Count: declarations, side-effect statements, and value expressions
    AstNode* scan = list_node->item;
    int decl_count = 0, stam_count = 0, value_count = 0;
    AstNode* last_value = nullptr;
    while (scan) {
        if (is_declaration_node(scan->node_type)) {
            decl_count++;
        } else if (is_side_effect_stam(scan->node_type)) {
            stam_count++;
        } else {
            value_count++;
            last_value = scan;
        }
        scan = scan->next;
    }

    if (is_proc) {
        // Non-final proc control blocks are statements; preserving their null
        // result used to be masked by empty-string-as-null concat behavior.
        decl_count = 0; stam_count = 0; value_count = 0; last_value = nullptr;
        scan = list_node->item;
        AstNode* last_executable = nullptr;
        while (scan) {
            if (!is_declaration_node(scan->node_type)) {
                last_executable = scan;
            }
            scan = scan->next;
        }
        scan = list_node->item;
        while (scan) {
            if (is_declaration_node(scan->node_type)) {
                decl_count++;
            } else if (is_side_effect_stam(scan->node_type) ||
                       is_proc_flow_side_effect_node(scan, last_executable)) {
                stam_count++;
            } else if (scan->node_type == AST_NODE_WHILE_STAM ||
                       scan->node_type == AST_NODE_FOR_STAM) {
                stam_count++;
            } else {
                value_count++;
                last_value = scan;
            }
            scan = scan->next;
        }
    }

    // In proc context with multiple values, only the LAST value expression
    // contributes to the result. Preceding value expressions are side effects.
    // Treat as single-value block expression.
    if (is_proc && value_count > 1) {
        push_scope(mt);
        MIR_reg_t result = 0;
        AstNode* item = list_node->item;
        while (item) {
            if (is_declaration_node(item->node_type)) {
                if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                    item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                    transpile_let_stam(mt, (AstLetNode*)item);
                } else if (item->node_type == AST_NODE_OBJECT_TYPE) {
                    transpile_expr(mt, item); // emit method registration
                }
            } else if (is_side_effect_stam(item->node_type)) {
                transpile_proc_side_effect(mt, item);
            } else if (is_proc_flow_side_effect_node(item, last_value)) {
                transpile_expr(mt, item);
            } else if (item == last_value) {
                // Last value expression: this is the return value
                result = transpile_box_item(mt, item);
            } else if (item->node_type == AST_NODE_IF_EXPR ||
                       item->node_type == AST_NODE_WHILE_STAM ||
                       item->node_type == AST_NODE_FOR_STAM) {
                transpile_expr(mt, item);
            } else {
                // Non-last value expression in proc: side effect only
                transpile_expr(mt, item);
            }
            item = item->next;
        }
        result = transpile_task_scope_leave(mt, task_scope, result, false);
        pop_scope(mt);
        return result;
    }

    // Single value with declarations/statements: block expression
    // Process all items in order, return the single value expression result.
    // Exclude FOR_EXPR/FOR_STAM — spreadable results need list_push_spread
    if (value_count == 1 && last_value && (decl_count > 0 || stam_count > 0)
        && last_value->node_type != AST_NODE_FOR_EXPR
        && last_value->node_type != AST_NODE_FOR_STAM) {
        push_scope(mt);
        MIR_reg_t result = 0;
        AstNode* item = list_node->item;
        while (item) {
            if (is_declaration_node(item->node_type)) {
                if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                    item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                    transpile_let_stam(mt, (AstLetNode*)item);
                } else if (item->node_type == AST_NODE_OBJECT_TYPE) {
                    transpile_expr(mt, item); // emit method registration
                }
                // Func defs handled in module-level pre-pass
            } else if (is_side_effect_stam(item->node_type)) {
                transpile_proc_side_effect(mt, item); // execute for side effects
            } else if (is_proc_flow_side_effect_node(item, last_value)) {
                transpile_expr(mt, item);
            } else if (item == last_value) {
                // This is the single value expression
                result = transpile_box_item(mt, item);
            } else if (is_proc &&
                       (item->node_type == AST_NODE_WHILE_STAM ||
                        item->node_type == AST_NODE_FOR_STAM)) {
                transpile_expr(mt, item); // proc context side effect
            }
            item = item->next;
        }
        result = transpile_task_scope_leave(mt, task_scope, result, false);
        pop_scope(mt);
        return result;
    }

    // Single value without declarations: just return it boxed
    if (value_count == 1 && last_value && decl_count == 0 && stam_count == 0) {
        MIR_reg_t result = transpile_box_item(mt, last_value);
        return transpile_task_scope_leave(mt, task_scope, result, false);
    }

    // No value items: execute side-effect statements and return null
    if (value_count == 0) {
        push_scope(mt);
        AstNode* item = list_node->item;
        while (item) {
            if (is_declaration_node(item->node_type)) {
                if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                    item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                    transpile_let_stam(mt, (AstLetNode*)item);
                } else if (item->node_type == AST_NODE_OBJECT_TYPE) {
                    transpile_expr(mt, item); // emit method registration
                }
            } else if (is_side_effect_stam(item->node_type)) {
                transpile_proc_side_effect(mt, item);
            } else if (is_proc_flow_side_effect_node(item, last_value)) {
                transpile_expr(mt, item);
            } else if (is_proc &&
                       (item->node_type == AST_NODE_WHILE_STAM ||
                        item->node_type == AST_NODE_FOR_STAM)) {
                transpile_expr(mt, item); // proc context side effect
            }
            item = item->next;
        }
        // In proc context, the result of a side-effect-only block is never used.
        // Skip the expensive list()/list_end() allocation and return a null Item.
        if (is_proc) {
            MIR_reg_t result = new_reg(mt, "null_block", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            result = transpile_task_scope_leave(mt, task_scope, result, false);
            pop_scope(mt);
            return result;
        }
        MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
        MIR_reg_t result = emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
        result = transpile_task_scope_leave(mt, task_scope, result, false);
        pop_scope(mt);
        return result;
    }

    // Multiple values: build a list (functional context)
    push_scope(mt);

    // Process all items in order
    MIR_reg_t ls = emit_call_0(mt, "list", MIR_T_P);
    AstNode* item = list_node->item;
    while (item) {
        if (is_declaration_node(item->node_type)) {
            if (item->node_type == AST_NODE_LET_STAM || item->node_type == AST_NODE_PUB_STAM ||
                item->node_type == AST_NODE_TYPE_STAM || item->node_type == AST_NODE_VAR_STAM) {
                transpile_let_stam(mt, (AstLetNode*)item);
            } else if (item->node_type == AST_NODE_OBJECT_TYPE) {
                transpile_expr(mt, item); // emit method registration
            }
            item = item->next;
            continue;
        }
        if (is_side_effect_stam(item->node_type)) {
            transpile_proc_side_effect(mt, item); // execute for side effects, don't push to list
            item = item->next;
            continue;
        }
        MIR_reg_t val = transpile_box_item(mt, item);
        emit_call_void_2(mt, "list_push_spread",
            MIR_T_P, MIR_new_reg_op(mt->ctx, ls),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        item = item->next;
    }

    MIR_reg_t result = emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, ls));
    result = transpile_task_scope_leave(mt, task_scope, result, false);
    pop_scope(mt);
    return result;
}

// ============================================================================
// Map expressions
// ============================================================================

static MIR_reg_t transpile_map(MirTranspiler* mt, AstMapNode* map_node) {
    MIR_reg_t static_reg = 0;
    if (emit_static_collection_const(mt, (AstNode*)map_node, &static_reg)) {
        return static_reg;
    }

    TypeMap* map_type = (TypeMap*)map_node->type;
    int type_index = map_type->type_index;

    // Create map with inline data: Map* m = map_with_data(type_index)
    MIR_reg_t m = 0; // allocated below based on path

    // Count value items
    AstNode* item = map_node->item;
    int val_count = 0;
    while (item) { val_count++; item = item->next; }

    // =========================================================================
    // Optimization: Direct field stores for typed maps with known shapes.
    // Additionally, map_with_data() is inlined: we call heap_calloc directly
    // and set Map header fields with immediate constants, eliminating:
    //   - map_with_data() function call overhead
    //   - type_list[type_index] runtime lookup (2 pointer chases)
    //   - C varargs marshaling/unmarshaling overhead
    //   - set_fields() per-field type switch dispatch
    //   - runtime type checking for each field
    // =========================================================================
    if (false && (has_fixed_shape(map_type) || map_type->has_named_shape) &&
        map_type->byte_size > 0 && val_count > 0 && val_count == (int)map_type->length) {
        // Check all fields: named, typed, 8-byte aligned offsets, supported types
        bool all_direct = true;
        ShapeEntry* check = map_type->shape;
        while (check) {
            if (!check->name || !check->type ||
                check->byte_offset % sizeof(void*) != 0) {
                all_direct = false;
                break;
            }
            TypeId ft = resolve_field_type_id(check, true);
            if (ft == LMD_TYPE_ANY) {
                all_direct = false;
                break;
            }
            if (!is_native_numeric_or_bool_type_id(ft) &&
                ft != LMD_TYPE_STRING && ft != LMD_TYPE_NULL &&
                !mir_is_container_field_type(ft)) {
                all_direct = false;
                break;
            }
            check = check->next;
        }

        if (all_direct) {
            log_debug("mir: inline alloc + direct field store for typed map '%s' (%d fields)",
                map_type->struct_name, val_count);

            // Inline map_with_data: call heap_calloc directly with compile-time constants
            int64_t byte_size = map_type->byte_size;
            int64_t total_size = (int64_t)sizeof(Map) + byte_size;

            // Compute size class at JIT compile time to skip runtime class_index lookup.
            // Size classes: {16, 32, 48, 64, 96, 128, 256} — find smallest that fits.
            int alloc_class = gc_object_zone_class_index((size_t)total_size);
            int64_t class_size = (alloc_class >= 0) ? (int64_t)gc_object_zone_class_size(alloc_class) : total_size;
            int64_t slot_size = (int64_t)sizeof(gc_header_t) + class_size;

            // ================================================================
            // INLINE BUMP-POINTER ALLOCATION (#10 + #11)
            // Fast path: bump gc->bump_cursor by slot_size.
            // Slow path: call heap_calloc_class (handles free list + new block).
            // gc_reg is loaded at function entry: context->heap->gc
            // ================================================================
            if (alloc_class >= 0 && mt->gc_reg != 0) {
                // gc_heap_t offsets: bump_cursor=16, bump_end=24, all_objects=8
                MIR_reg_t cursor = new_reg(mt, "bcur", MIR_T_I64);
                MIR_reg_t new_cursor = new_reg(mt, "bnew", MIR_T_I64);
                MIR_reg_t bend = new_reg(mt, "bend", MIR_T_I64);

                // Load bump_cursor and bump_end from gc_heap_t
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, cursor),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, mt->gc_reg, 0, 1)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, new_cursor),
                    MIR_new_reg_op(mt->ctx, cursor),
                    MIR_new_int_op(mt->ctx, slot_size)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, bend),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 24, mt->gc_reg, 0, 1)));

                // Branch: if new_cursor > bump_end → slow path
                MIR_label_t slow_label = MIR_new_label(mt->ctx);
                MIR_label_t done_label = MIR_new_label(mt->ctx);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_UBGT,
                    MIR_new_label_op(mt->ctx, slow_label),
                    MIR_new_reg_op(mt->ctx, new_cursor),
                    MIR_new_reg_op(mt->ctx, bend)));

                // ---- Fast path: bump succeeded ----
                // Store updated cursor back to gc->bump_cursor
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, mt->gc_reg, 0, 1),
                    MIR_new_reg_op(mt->ctx, new_cursor)));

                // m = cursor + sizeof(gc_header_t) — user pointer past header
                m = new_reg(mt, "mptr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, m),
                    MIR_new_reg_op(mt->ctx, cursor),
                    MIR_new_int_op(mt->ctx, (int64_t)sizeof(gc_header_t))));

                // Initialize gc_header_t at cursor (memory is pre-zeroed):
                //   header->next = gc->all_objects  (offset 0 from cursor)
                //   header->type_tag = LMD_TYPE_MAP (offset 8, uint16)
                //   header->alloc_size = total_size (offset 12, uint32)
                //   gc_flags=0 and marked=0 are already zero from block memset
                MIR_reg_t old_head = new_reg(mt, "oldh", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, old_head),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, mt->gc_reg, 0, 1))); // gc->all_objects
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, cursor, 0, 1),          // header->next
                    MIR_new_reg_op(mt->ctx, old_head)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, mt->gc_reg, 0, 1),    // gc->all_objects = cursor
                    MIR_new_reg_op(mt->ctx, cursor)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U16, 8, cursor, 0, 1),          // header->type_tag
                    MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U32, 12, cursor, 0, 1),         // header->alloc_size
                    MIR_new_int_op(mt->ctx, total_size)));

                // Set Container.is_heap = 1 (flags byte at user+1, bit 2)
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U8, 1, m, 0, 1),
                    MIR_new_int_op(mt->ctx, 0x04)));

                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, done_label)));

                // ---- Slow path: call heap_calloc_class ----
                emit_insn(mt, slow_label);
                MIR_reg_t slow_m = emit_call_3(mt, "heap_calloc_class", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, total_size),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)alloc_class));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, m),
                    MIR_new_reg_op(mt->ctx, slow_m)));

                // Reload gc->bump_cursor after slow path (may have changed block)
                // Not needed for m, but needed for future allocations in this function.
                // gc_reg is stable (gc_heap_t* doesn't change).

                emit_insn(mt, done_label);
            } else if (alloc_class >= 0) {
                m = emit_call_3(mt, "heap_calloc_class", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, total_size),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)alloc_class));
            } else {
                // Fallback for very large maps (unlikely)
                m = emit_call_2(mt, "heap_calloc", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, total_size),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP));
            }

            // Set Map header fields
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, m, 0, 1),
                MIR_new_int_op(mt->ctx, (int64_t)LMD_TYPE_MAP)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, m, 0, 1),
                MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)map_type)));

            // Compute and store data pointer = m + sizeof(Map)
            MIR_reg_t data_ptr = new_reg(mt, "mdata", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                MIR_new_reg_op(mt->ctx, data_ptr),
                MIR_new_reg_op(mt->ctx, m),
                MIR_new_int_op(mt->ctx, (int64_t)sizeof(Map))));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, m, 0, 1),
                MIR_new_reg_op(mt->ctx, data_ptr)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I32, 24, m, 0, 1),
                MIR_new_int_op(mt->ctx, byte_size)));

            MIR_reg_t map_root_slot = create_pointer_gc_root_slot(mt, m);

            // Store each field directly at known byte offset
            item = map_node->item;
            ShapeEntry* field = map_type->shape;
            while (item && field) {
                TypeId field_type = resolve_field_type_id(field, true);
                int64_t offset = field->byte_offset;

                // Extract value AST node from key-value pair
                AstNode* value_node = nullptr;
                bool is_null_literal = false;
                if (item->node_type == AST_NODE_KEY_EXPR) {
                    AstNamedNode* key_expr = (AstNamedNode*)item;
                    value_node = key_expr->as;
                    is_null_literal = (value_node == nullptr);
                } else {
                    value_node = item;
                }

                if (is_null_literal || field_type == LMD_TYPE_NULL) {
                    // Data buffer is zero-initialized by heap_calloc — skip store
                } else if (field_type == LMD_TYPE_FLOAT) {
                    // Store native double at data + offset
                    TypeId val_tid = get_effective_type(mt, value_node);
                    MIR_reg_t val;
                    if (val_tid == LMD_TYPE_FLOAT) {
                        val = transpile_expr(mt, value_node);
                    } else if (is_integer_type_id(val_tid)) {
                        MIR_reg_t int_val = transpile_expr(mt, value_node);
                        val = new_reg(mt, "i2d", MIR_T_D);
                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                            MIR_new_reg_op(mt->ctx, val),
                            MIR_new_reg_op(mt->ctx, int_val)));
                    } else {
                        MIR_reg_t boxed = transpile_box_item(mt, value_node);
                        val = emit_unbox(mt, boxed, LMD_TYPE_FLOAT);
                    }
                    MIR_reg_t live_m = load_gc_root_slot(mt, map_root_slot, "map_live");
                    MIR_reg_t live_data = new_reg(mt, "mdata_live", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, live_data),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, live_m, 0, 1)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, live_data, 0, 1),
                        MIR_new_reg_op(mt->ctx, val)));
                } else if (is_integer_type_id(field_type) || field_type == LMD_TYPE_BOOL) {
                    // Store native int/bool at data + offset
                    TypeId val_tid = get_effective_type(mt, value_node);
                    MIR_reg_t val;
                    if (is_integer_type_id(val_tid) || val_tid == LMD_TYPE_BOOL) {
                        val = transpile_expr(mt, value_node);
                    } else {
                        MIR_reg_t boxed = transpile_box_item(mt, value_node);
                        val = emit_unbox(mt, boxed, field_type);
                    }
                    MIR_reg_t live_m = load_gc_root_slot(mt, map_root_slot, "map_live");
                    MIR_reg_t live_data = new_reg(mt, "mdata_live", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, live_data),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, live_m, 0, 1)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, live_data, 0, 1),
                        MIR_new_reg_op(mt->ctx, val)));
                } else if (field_type == LMD_TYPE_STRING) {
                    // Store raw String* pointer at data + offset
                    TypeId val_tid = get_effective_type(mt, value_node);
                    MIR_reg_t val;
                    if (val_tid == LMD_TYPE_STRING) {
                        val = transpile_expr(mt, value_node);
                    } else {
                        MIR_reg_t boxed = transpile_box_item(mt, value_node);
                        val = emit_unbox(mt, boxed, LMD_TYPE_STRING);
                    }
                    MIR_reg_t live_m = load_gc_root_slot(mt, map_root_slot, "map_live");
                    MIR_reg_t live_data = new_reg(mt, "mdata_live", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, live_data),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, live_m, 0, 1)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, live_data, 0, 1),
                        MIR_new_reg_op(mt->ctx, val)));
                } else if (mir_is_container_field_type(field_type)) {
                    // Container types: get boxed Item, strip tag → raw Container*
                    // For null Items: AND mask strips to 0 (matching nullptr)
                    MIR_reg_t boxed = transpile_box_item(mt, value_node);
                    MIR_reg_t raw = emit_unbox_container(mt, boxed);
                    MIR_reg_t live_m = load_gc_root_slot(mt, map_root_slot, "map_live");
                    MIR_reg_t live_data = new_reg(mt, "mdata_live", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, live_data),
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, live_m, 0, 1)));
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, live_data, 0, 1),
                        MIR_new_reg_op(mt->ctx, raw)));
                }

                item = item->next;
                field = field->next;
            }

            return load_gc_root_slot(mt, map_root_slot, "map_live");
        }
    }

    // Fallback: evaluate all values as boxed Items first, then allocate and fill the map.
    // This avoids keeping a newly allocated map/data pointer live across value evaluation
    // calls that may trigger GC or clobber JIT temporaries.
    if (val_count == 0) {
        return emit_call_2(mt, "map_with_tl", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, type_index),
            MIR_T_P, MIR_new_reg_op(mt->ctx, emit_load_module_type_list(mt)));
    }

    MIR_op_t* val_ops = LAMBDA_ALLOCA(val_count, MIR_op_t);
    int* val_root_slots = LAMBDA_ALLOCA(val_count, int);
    item = map_node->item;
    int vi = 0;
    while (item) {
        if (item->node_type == AST_NODE_KEY_EXPR) {
            AstNamedNode* key_expr = (AstNamedNode*)item;
            if (key_expr->as) {
                MIR_reg_t val = transpile_box_item(mt, key_expr->as);
                val_root_slots[vi] = create_gc_root_slot(mt, val);
                val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
            } else {
                MIR_reg_t nul = new_reg(mt, "mapnull", MIR_T_I64);
                uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nul),
                    MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                val_root_slots[vi] = -1;
                val_ops[vi++] = MIR_new_reg_op(mt->ctx, nul);
            }
        } else {
            MIR_reg_t val = transpile_box_item(mt, item);
            val_root_slots[vi] = create_gc_root_slot(mt, val);
            val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
        }
        item = item->next;
    }

    // Allocate map via map_with_tl() (saves/restores context->type_list so cross-module
    // calls always use this module's own type_list).
    m = emit_call_2(mt, "map_with_tl", MIR_T_P,
        MIR_T_I64, MIR_new_int_op(mt->ctx, type_index),
        MIR_T_P, MIR_new_reg_op(mt->ctx, emit_load_module_type_list(mt)));
    create_pointer_gc_root_slot(mt, m);

    for (int ri = 0; ri < vi; ri++) {
        if (val_root_slots[ri] >= 0) {
            MIR_reg_t live_val = load_gc_root_slot(mt, val_root_slots[ri], "map_val");
            val_ops[ri] = MIR_new_reg_op(mt->ctx, live_val);
        }
    }

    // Call map_fill(m, val1, val2, ...) — variadic
    MIR_reg_t filled = emit_vararg_call(mt, "map_fill", MIR_T_P, 1,
        MIR_T_P, MIR_new_reg_op(mt->ctx, m), vi, val_ops);

    return filled;
}

// ============================================================================
// Element expressions
// ============================================================================

static MIR_reg_t transpile_element(MirTranspiler* mt, AstElementNode* elmt_node) {
    if (!elmt_node || !elmt_node->type) {
        log_error("mir: transpile_element with null node or type");
        MIR_reg_t r = new_reg(mt, "elmt_err", MIR_T_I64);
        uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
        return r;
    }

    TypeElmt* type = (TypeElmt*)elmt_node->type;

    // Create element: Element* el = elmt_with_tl(type_index, type_list_ptr)
    MIR_reg_t el = emit_call_2(mt, "elmt_with_tl", MIR_T_P,
        MIR_T_I64, MIR_new_int_op(mt->ctx, type->type_index),
        MIR_T_P, MIR_new_reg_op(mt->ctx, emit_load_module_type_list(mt)));
    int el_root = create_pointer_gc_root_slot(mt, el);

    // Fill attributes if present
    AstNode* item = elmt_node->item;
    if (item) {
        // Count attribute values
        int attr_count = 0;
        AstNode* scan = item;
        while (scan) { attr_count++; scan = scan->next; }

        // Evaluate attribute values
        MIR_op_t* attr_ops = LAMBDA_ALLOCA(attr_count, MIR_op_t);
        int* attr_roots = LAMBDA_ALLOCA(attr_count, int);
        for (int i = 0; i < attr_count; i++) attr_roots[i] = -1;
        int ai = 0;
        scan = item;
        while (scan) {
            if (scan->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)scan;
                if (key_expr->as) {
                    MIR_reg_t val = transpile_box_item(mt, key_expr->as);
                    attr_roots[ai] = create_gc_root_slot(mt, val);
                    attr_ops[ai++] = MIR_new_reg_op(mt->ctx, val);
                } else {
                    MIR_reg_t nul = new_reg(mt, "atnull", MIR_T_I64);
                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nul),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    attr_ops[ai++] = MIR_new_reg_op(mt->ctx, nul);
                }
            } else {
                MIR_reg_t val = transpile_box_item(mt, scan);
                attr_roots[ai] = create_gc_root_slot(mt, val);
                attr_ops[ai++] = MIR_new_reg_op(mt->ctx, val);
            }
            scan = scan->next;
        }
        for (int i = 0; i < ai; i++) {
            if (attr_roots[i] >= 0) {
                MIR_reg_t live_attr = load_gc_root_slot(mt, attr_roots[i], "el_attr");
                attr_ops[i] = MIR_new_reg_op(mt->ctx, live_attr);
            }
        }
        el = load_gc_root_slot(mt, el_root, "el_live");

        // Call elmt_fill(el, val1, val2, ...) — variadic
        emit_vararg_call(mt, "elmt_fill", MIR_T_P, 1,
            MIR_T_P, MIR_new_reg_op(mt->ctx, el), ai, attr_ops);
    }

    // Fill content if present
    if (type->content_length) {
        if (elmt_node->content) {
            AstListNode* content_list = (AstListNode*)elmt_node->content;
            AstNode* content_item = content_list->item;

            if (type->content_length < 10) {
                // Use list_fill(el, count, val1, val2, ...) for small content
                // Count content items
                int content_count = 0;
                AstNode* cscan = content_item;
                while (cscan) { content_count++; cscan = cscan->next; }

                MIR_op_t* content_ops = LAMBDA_ALLOCA(content_count, MIR_op_t);
                int* content_roots = LAMBDA_ALLOCA(content_count, int);
                for (int i = 0; i < content_count; i++) content_roots[i] = -1;
                int ci = 0;
                cscan = content_item;
                while (cscan) {
                    MIR_reg_t val = transpile_box_item(mt, cscan);
                    content_roots[ci] = create_gc_root_slot(mt, val);
                    content_ops[ci++] = MIR_new_reg_op(mt->ctx, val);
                    cscan = cscan->next;
                }
                for (int i = 0; i < ci; i++) {
                    if (content_roots[i] >= 0) {
                        MIR_reg_t live_content = load_gc_root_slot(mt, content_roots[i], "el_content");
                        content_ops[i] = MIR_new_reg_op(mt->ctx, live_content);
                    }
                }
                el = load_gc_root_slot(mt, el_root, "el_live");

                // list_fill(el, count, items...) — returns Item
                emit_vararg_call_2(mt, "list_fill", MIR_T_I64, 1,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, el),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, content_count),
                    ci, content_ops);
            } else {
                // Use list_push_spread for each content item, then list_end
                while (content_item) {
                    MIR_reg_t val = transpile_box_item(mt, content_item);
                    int content_root = create_gc_root_slot(mt, val);
                    val = load_gc_root_slot(mt, content_root, "el_content");
                    el = load_gc_root_slot(mt, el_root, "el_live");
                    emit_call_void_2(mt, "list_push_spread",
                        MIR_T_P, MIR_new_reg_op(mt->ctx, el),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                    content_item = content_item->next;
                }
                el = load_gc_root_slot(mt, el_root, "el_live");
                emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, el));
            }
        } else {
            // content_length but no content node — just list_end
            el = load_gc_root_slot(mt, el_root, "el_live");
            emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, el));
        }
    } else {
        // No content
        if (elmt_node->item) {
            // Has attributes but no content — call list_end to finalize frame
            el = load_gc_root_slot(mt, el_root, "el_live");
            emit_call_1(mt, "list_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, el));
        }
        // else: no attrs and no content — element is just a bare pointer,
    }

    return load_gc_root_slot(mt, el_root, "el_rv");
}

// ============================================================================
// Member/Index access
// ============================================================================

// Emit direct field READ from a packed map/object data struct.
// Map/Object layout: [TypeId(1) flags(1) pad(6)] [void* type @8] [void* data @16] [int data_cap @24]
// data points to packed struct where each field is 8-byte aligned.
// Returns native value for scalars (INT/FLOAT/BOOL/STRING) or tagged Item for containers.
// obj_boxed: pre-computed tagged Item for the object (avoids double-evaluation).
// skip_null_guard: when true, omit the null check branch (caller guarantees non-null).
static MIR_reg_t emit_mir_direct_field_read(MirTranspiler* mt, MIR_reg_t obj_boxed,
    ShapeEntry* field, bool skip_null_guard) {
    TypeId type_id = resolve_field_type_id(field, true);
    int64_t offset = field->byte_offset;

    // strip tag from boxed Item → raw Map*/Object*
    MIR_reg_t map_ptr = emit_unbox_container(mt, obj_boxed);

    // Null guard: if map_ptr == 0 (object is null), return default value.
    // Skipped when the caller can prove the object is non-null (e.g. typed
    // local variable or parameter — the type annotation is a non-null contract).
    MIR_label_t l_not_null = 0;
    MIR_label_t l_done = 0;
    if (!skip_null_guard) {
        l_not_null = new_label(mt);
        l_done = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BNE, MIR_new_label_op(mt->ctx, l_not_null),
            MIR_new_reg_op(mt->ctx, map_ptr), MIR_new_int_op(mt->ctx, 0)));
    }

    // null path: produce default value
    if (type_id == LMD_TYPE_FLOAT) {
        MIR_reg_t result = new_reg(mt, "dfld", MIR_T_D);
        if (!skip_null_guard) {
            // null → 0.0
            MIR_reg_t zero_i = new_reg(mt, "zi", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, zero_i),
                MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, zero_i)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_done)));
            // non-null path
            emit_label(mt, l_not_null);
        }

        MIR_reg_t data_ptr = new_reg(mt, "dptr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));
        if (skip_null_guard) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, data_ptr, 0, 1)));
        } else {
            MIR_reg_t nn_result = new_reg(mt, "dfld", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, nn_result),
                MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, data_ptr, 0, 1)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, nn_result)));
            emit_label(mt, l_done);
        }
        return result;
    }

    if (mir_is_container_field_type(type_id)) {
        // Container fields store raw Container* pointers (no type tag in high bits).
        // The Lambda runtime identifies container types by reading the first byte of
        // the struct (Container.type_id), NOT from Item tag bits. So we must return
        // the raw pointer as-is. For null fields (ptr==0), return ItemNull.
        MIR_reg_t result = new_reg(mt, "cfraw", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        if (!skip_null_guard) {
            // null object → ItemNull
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_done)));
            // non-null path
            emit_label(mt, l_not_null);
        }

        // load raw Container* from data buffer
        MIR_reg_t data_ptr = new_reg(mt, "dptr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));

        MIR_reg_t raw = new_reg(mt, "cfptr", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, raw),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1)));

        // if raw == 0, field is null → produce ItemNull; else return raw pointer
        MIR_label_t null_field_lbl = new_label(mt);
        MIR_label_t done_field_lbl = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, null_field_lbl),
            MIR_new_reg_op(mt->ctx, raw), MIR_new_int_op(mt->ctx, 0)));
        // non-null: return raw pointer as-is (Container* is a valid Item with _type_id=0;
        // get_type_id() reads Container.type_id from the struct's first byte)
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, raw)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, done_field_lbl)));
        // null field: produce ItemNull
        emit_label(mt, null_field_lbl);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        emit_label(mt, done_field_lbl);
        if (!skip_null_guard) {
            emit_label(mt, l_done);
        }
        return result;
    }

    // Scalar types: load 8 bytes as int64
    // INT/INT64: native int64 value
    // BOOL: bool (1 byte) in 8-byte zero-padded slot → safe to read 8 bytes
    // STRING/SYMBOL/BINARY: raw pointer (String*/Symbol*/Binary*)
    MIR_reg_t result = new_reg(mt, "mfld", MIR_T_I64);
    if (!skip_null_guard) {
        // null → 0 (default for scalars)
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, 0)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_done)));
        // non-null path
        emit_label(mt, l_not_null);
    }

    MIR_reg_t data_ptr = new_reg(mt, "dptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));
    if (skip_null_guard) {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1)));
    } else {
        MIR_reg_t nn_val = new_reg(mt, "mfld", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nn_val),
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, nn_val)));
        emit_label(mt, l_done);
    }
    return result;
}

// Emit direct field WRITE to a packed map/object data struct.
// Stores value at data + byte_offset. Value must be native type for scalars,
// or tagged Item for containers.
static void emit_mir_direct_field_write(MirTranspiler* mt, AstNode* object,
    ShapeEntry* field, AstNode* value, bool skip_null_guard) {
    TypeId type_id = resolve_field_type_id(field, true);
    int64_t offset = field->byte_offset;

    // get tagged Item for the object, strip tag → raw Map*/Object*
    MIR_reg_t obj_item = transpile_expr(mt, object);
    MIR_reg_t map_ptr = emit_unbox_container(mt, obj_item);

    // Null guard: if map_ptr == 0 (object is null), skip the write entirely.
    // Skipped when caller proves object is non-null (typed IDENT variable/param).
    MIR_label_t l_skip_write = 0;
    if (!skip_null_guard) {
        l_skip_write = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, l_skip_write),
            MIR_new_reg_op(mt->ctx, map_ptr), MIR_new_int_op(mt->ctx, 0)));
    }

    // load data pointer from Map* + 16
    MIR_reg_t data_ptr = new_reg(mt, "dwptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, data_ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, map_ptr, 0, 1)));

    if (type_id == LMD_TYPE_FLOAT) {
        // value should be native double from transpile_expr for FLOAT-typed expressions
        TypeId val_tid = get_effective_type(mt, value);
        MIR_reg_t val;
        if (val_tid == LMD_TYPE_FLOAT) {
            val = transpile_expr(mt, value);
        } else if (is_integer_type_id(val_tid)) {
            // int → float promotion
            MIR_reg_t int_val = transpile_expr(mt, value);
            val = new_reg(mt, "i2d", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, val),
                MIR_new_reg_op(mt->ctx, int_val)));
        } else {
            // fallback: box then unbox to double
            MIR_reg_t boxed = transpile_box_item(mt, value);
            val = emit_unbox(mt, boxed, LMD_TYPE_FLOAT);
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
            MIR_new_mem_op(mt->ctx, MIR_T_D, (int)offset, data_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        if (!skip_null_guard) emit_label(mt, l_skip_write);
        return;
    }

    if (is_integer_type_id(type_id) || type_id == LMD_TYPE_BOOL) {
        // store native int64 value
        TypeId val_tid = get_effective_type(mt, value);
        MIR_reg_t val;
        if (is_integer_type_id(val_tid) || val_tid == LMD_TYPE_BOOL) {
            val = transpile_expr(mt, value);
        } else {
            // fallback: box then unbox
            MIR_reg_t boxed = transpile_box_item(mt, value);
            val = emit_unbox(mt, boxed, type_id);
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        if (!skip_null_guard) emit_label(mt, l_skip_write);
        return;
    }

    if (type_id == LMD_TYPE_STRING) {
        // store native String* pointer
        TypeId val_tid = get_effective_type(mt, value);
        MIR_reg_t val;
        if (val_tid == LMD_TYPE_STRING) {
            val = transpile_expr(mt, value);  // native String*
        } else {
            // fallback: box then unbox to String*
            MIR_reg_t boxed = transpile_box_item(mt, value);
            val = emit_unbox(mt, boxed, LMD_TYPE_STRING);
        }
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        if (!skip_null_guard) emit_label(mt, l_skip_write);
        return;
    }

    // container and other pointer types: strip tag, store raw pointer
    // (runtime's map_field_store stores raw Container*/pointer, not tagged Items)
    MIR_reg_t val = transpile_box_item(mt, value);
    MIR_reg_t raw = emit_unbox_container(mt, val);  // strip tag → raw ptr (0 for null)
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)offset, data_ptr, 0, 1),
        MIR_new_reg_op(mt->ctx, raw)));
    if (!skip_null_guard) emit_label(mt, l_skip_write);
}

static MIR_reg_t transpile_member(MirTranspiler* mt, AstFieldNode* field_node) {
    // Use transpile_box_item for the object to ensure proper boxing.
    // This is critical for types like DTIME/INT64/DECIMAL where transpile_expr
    // returns raw unboxed values from transpile_primary, but variables may hold
    // values that were previously boxed via transpile_box_item. Using
    // transpile_box_item here ensures consistent boxing at the call site.
    MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);

    // For Path objects with known property fields, use item_attr(item, "key")
    // instead of fn_member(item, key_item). fn_member doesn't auto-load metadata,
    // but item_attr (used by C transpiler) does. This matches C transpiler behavior.
    TypeId obj_tid = get_effective_type(mt, field_node->object);
    if (obj_tid == LMD_TYPE_PATH && field_node->field->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)field_node->field;
        const char* k = ident->name->chars;
        bool is_property = (strcmp(k, "name") == 0 || strcmp(k, "is_dir") == 0 ||
                           strcmp(k, "is_file") == 0 || strcmp(k, "is_link") == 0 ||
                           strcmp(k, "size") == 0 || strcmp(k, "modified") == 0 ||
                           strcmp(k, "path") == 0 || strcmp(k, "extension") == 0 ||
                           strcmp(k, "scheme") == 0 || strcmp(k, "depth") == 0 ||
                           strcmp(k, "parent") == 0 || strcmp(k, "mode") == 0);
        if (is_property) {
            MIR_reg_t key_ptr = emit_load_string_literal(mt, k);
            return emit_call_2(mt, "item_attr", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_P, MIR_new_reg_op(mt->ctx, key_ptr));
        }
        // Non-property field on path: use fn_member which extends the path
    }

    // ==================================================================
    // Phase 3: Direct map/object field access optimization
    // For typed maps/objects with fixed shape, bypass fn_member runtime call
    // and load field directly from packed data struct.
    // Use AST type (not get_effective_type) because parameters stored as
    // boxed ANY still have correct AST type from type annotations.
    // ==================================================================
    TypeId ast_obj_tid = field_node->object->type ? field_node->object->type->type_id : LMD_TYPE_ANY;
    if (false && (ast_obj_tid == LMD_TYPE_MAP || ast_obj_tid == LMD_TYPE_OBJECT) &&
        field_node->field->node_type == AST_NODE_IDENT &&
        field_node->object->type) {
        TypeMap* map_type = (TypeMap*)field_node->object->type;
        if (has_fixed_shape(map_type)) {
            AstIdentNode* ident = (AstIdentNode*)field_node->field;
            ShapeEntry* se = find_shape_field_by_name(map_type,
                ident->name->chars, ident->name->len);
            if (se && se->type && is_direct_access_type(resolve_field_type_id(se, true))) {
                log_debug("mir: direct field read: %.*s (type=%d offset=%lld)",
                    (int)ident->name->len, ident->name->chars,
                    resolve_field_type_id(se, true), (long long)se->byte_offset);
                bool skip_null_guard = false; // typed variables can still hold null
                return emit_mir_direct_field_read(mt, boxed_obj, se, skip_null_guard);
            }
        }
    }

    // Field name for member access: extract name from ident and create boxed string Item
    AstNode* field = field_node->field;
    MIR_reg_t boxed_field;
    if (field->node_type == AST_NODE_IDENT) {
        // IDENT node has no type info for member fields — use name pool String* directly
        AstIdentNode* ident = (AstIdentNode*)field;
        String* str = ident->name; // persistent name pool pointer
        uint64_t str_item = ((uint64_t)LMD_TYPE_STRING << 56) | (uint64_t)(uintptr_t)str;
        boxed_field = new_reg(mt, "fld", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, boxed_field),
            MIR_new_int_op(mt->ctx, (int64_t)str_item)));
    } else {
        // Non-ident field (computed member): transpile as expression
        boxed_field = transpile_box_item(mt, field);
    }

    int obj_root = create_gc_root_slot(mt, boxed_obj);
    int field_root = create_gc_root_slot(mt, boxed_field);
    boxed_obj = load_gc_root_slot(mt, obj_root, "member_obj");
    boxed_field = load_gc_root_slot(mt, field_root, "member_key");
    MIR_reg_t result = emit_call_2(mt, "fn_member", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_field));
    int result_root = create_gc_root_slot(mt, result);
    result = load_gc_root_slot(mt, result_root, "member_res");

    // when the member expression has a resolved field type, unbox the fn_member
    // result so that transpile_expr returns a native value matching the type
    TypeId mem_tid = ((AstNode*)field_node)->type ? ((AstNode*)field_node)->type->type_id : LMD_TYPE_ANY;
    if (is_native_numeric_or_bool_type_id(mem_tid) || mem_tid == LMD_TYPE_STRING) {
        result = emit_unbox(mt, result, mem_tid);
    }
    return result;
}

typedef enum MirIndexStorageKind {
    MIR_INDEX_STORAGE_ARRAY_NUM,
    MIR_INDEX_STORAGE_ARRAY_FLOAT,
    MIR_INDEX_STORAGE_BOXED_ITEMS,
} MirIndexStorageKind;

typedef enum MirIndexResultKind {
    MIR_INDEX_RESULT_NATIVE_INT,
    MIR_INDEX_RESULT_NATIVE_FLOAT,
    MIR_INDEX_RESULT_BOXED_INT,
    MIR_INDEX_RESULT_NATIVE_BOOL,
} MirIndexResultKind;

typedef enum MirIndexGuardKind {
    MIR_INDEX_GUARD_NONE,
    MIR_INDEX_GUARD_CONTAINER_TYPE,
    MIR_INDEX_GUARD_BOXED_TYPE,
} MirIndexGuardKind;

typedef enum MirIndexOobKind {
    MIR_INDEX_OOB_ITEM_NULL,
    MIR_INDEX_OOB_FLOAT_ZERO,
} MirIndexOobKind;

typedef enum MirIndexSlowKind {
    MIR_INDEX_SLOW_NONE,
    MIR_INDEX_SLOW_ITEM_AT,
    MIR_INDEX_SLOW_FN_INDEX,
} MirIndexSlowKind;

typedef struct MirIndexLoadPolicy {
    MirIndexStorageKind storage_kind;
    MIR_type_t element_type;
    int element_width;
    MirIndexResultKind result_kind;
    MirIndexGuardKind guard_kind;
    TypeId expected_type;
    MirIndexOobKind oob_kind;
    MirIndexSlowKind slow_kind;
} MirIndexLoadPolicy;

static void emit_index_result_move(MirTranspiler* mt, MIR_reg_t result,
        MIR_reg_t loaded, MirIndexResultKind result_kind, bool loaded_is_boxed) {
    switch (result_kind) {
    case MIR_INDEX_RESULT_NATIVE_INT:
        if (loaded_is_boxed) loaded = emit_unbox(mt, loaded, LMD_TYPE_INT);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, loaded)));
        break;
    case MIR_INDEX_RESULT_NATIVE_FLOAT:
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
            MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, loaded)));
        break;
    case MIR_INDEX_RESULT_BOXED_INT:
        if (loaded_is_boxed) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, loaded)));
        } else {
            MIR_reg_t masked = new_reg(mt, "idx_mask", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND,
                MIR_new_reg_op(mt->ctx, masked), MIR_new_reg_op(mt->ctx, loaded),
                MIR_new_int_op(mt->ctx, (int64_t)0x00FFFFFFFFFFFFFFLL)));
            uint64_t int_tag = (uint64_t)LMD_TYPE_INT << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR,
                MIR_new_reg_op(mt->ctx, result), MIR_new_int_op(mt->ctx, (int64_t)int_tag),
                MIR_new_reg_op(mt->ctx, masked)));
        }
        break;
    case MIR_INDEX_RESULT_NATIVE_BOOL:
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND,
            MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, loaded),
            MIR_new_int_op(mt->ctx, 1)));
        break;
    }
}

// Typed index paths share this layout, but their guards, representations, and
// fallbacks differ after mutation; keep those semantics explicit in the policy.
static MIR_reg_t emit_checked_index_load(MirTranspiler* mt, MIR_reg_t arr_ptr,
        MIR_reg_t boxed_obj, MIR_reg_t idx_native, MirIndexLoadPolicy policy) {
    MIR_type_t result_type = policy.result_kind == MIR_INDEX_RESULT_NATIVE_FLOAT
        ? MIR_T_D : MIR_T_I64;
    MIR_reg_t result = new_reg(mt, "idx_result", result_type);
    MIR_label_t l_fast = new_label(mt);
    MIR_label_t l_slow = new_label(mt);
    MIR_label_t l_oob = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    if (policy.guard_kind != MIR_INDEX_GUARD_NONE) {
        MIR_reg_t runtime_type = new_reg(mt, "idx_type", MIR_T_I64);
        if (policy.guard_kind == MIR_INDEX_GUARD_CONTAINER_TYPE) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, runtime_type),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_RSH,
                MIR_new_reg_op(mt->ctx, runtime_type), MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_new_int_op(mt->ctx, 56)));
        }
        MIR_reg_t type_matches = new_reg(mt, "idx_type_ok", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ,
            MIR_new_reg_op(mt->ctx, type_matches), MIR_new_reg_op(mt->ctx, runtime_type),
            MIR_new_int_op(mt->ctx, policy.expected_type)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
            MIR_new_reg_op(mt->ctx, type_matches)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));
        emit_label(mt, l_fast);
    }

    MIR_reg_t arr_len = new_reg(mt, "idx_len", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));
    MIR_reg_t negative = new_reg(mt, "idx_negative", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, negative),
        MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, 0)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
        MIR_new_reg_op(mt->ctx, negative)));
    MIR_reg_t past_end = new_reg(mt, "idx_past_end", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, past_end),
        MIR_new_reg_op(mt->ctx, idx_native), MIR_new_reg_op(mt->ctx, arr_len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
        MIR_new_reg_op(mt->ctx, past_end)));

    const char* items_name = policy.storage_kind == MIR_INDEX_STORAGE_ARRAY_FLOAT
        ? "float_items" : (policy.storage_kind == MIR_INDEX_STORAGE_BOXED_ITEMS
            ? "boxed_items" : "int_items");
    MIR_reg_t items_ptr = new_reg(mt, items_name, MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));
    MIR_reg_t byte_offset = new_reg(mt, "idx_byte_offset", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MUL, MIR_new_reg_op(mt->ctx, byte_offset),
        MIR_new_reg_op(mt->ctx, idx_native), MIR_new_int_op(mt->ctx, policy.element_width)));
    MIR_reg_t element_addr = new_reg(mt, "idx_element", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, element_addr),
        MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_offset)));
    MIR_reg_t loaded = new_reg(mt, "idx_loaded", policy.element_type);
    MIR_insn_code_t load_code = policy.element_type == MIR_T_D ? MIR_DMOV : MIR_MOV;
    emit_insn(mt, MIR_new_insn(mt->ctx, load_code, MIR_new_reg_op(mt->ctx, loaded),
        MIR_new_mem_op(mt->ctx, policy.element_type, 0, element_addr, 0, 1)));
    emit_index_result_move(mt, result, loaded, policy.result_kind, false);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    emit_label(mt, l_oob);
    if (policy.oob_kind == MIR_INDEX_OOB_FLOAT_ZERO) {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_double_op(mt->ctx, 0.0)));
    } else {
        uint64_t null_value = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)null_value)));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    if (policy.guard_kind != MIR_INDEX_GUARD_NONE) {
        emit_label(mt, l_slow);
        MIR_reg_t slow_result;
        if (policy.slow_kind == MIR_INDEX_SLOW_FN_INDEX) {
            MIR_reg_t boxed_idx = emit_box_int(mt, idx_native);
            slow_result = emit_call_2(mt, "fn_index", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
        } else {
            slow_result = emit_call_2(mt, "item_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
        }
        emit_index_result_move(mt, result, slow_result, policy.result_kind, true);
    }

    emit_label(mt, l_end);
    return result;
}

static MIR_reg_t transpile_index(MirTranspiler* mt, AstFieldNode* field_node) {
    mt->last_index_object = field_node->object;
    // Multi-dim path: arr[i, j, k] — when there's more than one index, dispatch
    // to the runtime array_num_at_nd helper.  Indices are stored into a small
    // heap-data buffer and the helper computes the stride-walking offset.
    if (field_node->field && field_node->field->next) {
        // Count indices
        int ndim = 0;
        for (AstNode* it = field_node->field; it; it = it->next) ndim++;
        // Allocate a heap_data buffer for ndim * int64_t
        int64_t buf_bytes = (int64_t)ndim * (int64_t)sizeof(int64_t);
        MIR_reg_t idx_buf = emit_call_1(mt, "heap_data_calloc", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, buf_bytes));
        // Transpile each index and store into idx_buf
        int slot = 0;
        for (AstNode* it = field_node->field; it; it = it->next) {
            MIR_reg_t val = transpile_expr(mt, it);
            TypeId vt = get_effective_type(mt, it);
            if (!is_integer_type_id(vt)) {
                // unbox boxed Item to int64
                val = emit_unbox(mt, val, LMD_TYPE_INT);
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                               (MIR_disp_t)(slot * (int)sizeof(int64_t)),
                               idx_buf, 0, 1),
                MIR_new_reg_op(mt->ctx, val)));
            slot++;
        }
        // Get the object as ArrayNum* (unbox the container Item)
        MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
        MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);
        // Call array_num_at_nd(arr, ndim, indices) → Item
        return emit_call_3(mt, "array_num_at_nd", MIR_T_I64,
            MIR_T_P,   MIR_new_reg_op(mt->ctx, arr_ptr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ndim),
            MIR_T_P,   MIR_new_reg_op(mt->ctx, idx_buf));
    }

    TypeId idx_tid = get_effective_type(mt, field_node->field);
    TypeId obj_tid = get_effective_type(mt, field_node->object);

    // P4-3.2: Guard — if the index is a TYPE expression (child query like arr[int]),
    // skip all fast paths and fall through to fn_index which handles type-based dispatch.
    AstNode* idx_node = field_node->field;
    while (idx_node && idx_node->node_type == AST_NODE_PRIMARY)
        idx_node = ((AstPrimaryNode*)idx_node)->expr;
    bool is_type_index = false;
    if (idx_node) {
        // Direct type node (int, string, float, etc.)
        if (idx_node->node_type == AST_NODE_TYPE ||
            idx_node->node_type == AST_NODE_ARRAY_TYPE ||
            idx_node->node_type == AST_NODE_LIST_TYPE ||
            idx_node->node_type == AST_NODE_MAP_TYPE ||
            idx_node->node_type == AST_NODE_ELMT_TYPE ||
            idx_node->node_type == AST_NODE_FUNC_TYPE ||
            idx_node->node_type == AST_NODE_BINARY_TYPE ||
            idx_node->node_type == AST_NODE_UNARY_TYPE ||
            idx_node->node_type == AST_NODE_CONTENT_TYPE) {
            is_type_index = true;
        }
        // Node whose expression type is LMD_TYPE_TYPE (type-valued expression)
        if (!is_type_index && idx_node->type && idx_node->type->type_id == LMD_TYPE_TYPE) {
            is_type_index = true;
        }
    }
    if (is_type_index) {
        MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
        MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);
        return emit_call_2(mt, "fn_index", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
    }

    bool is_range_index = false;
    if (idx_node && idx_node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bi = (AstBinaryNode*)idx_node;
        is_range_index = (bi->op == OPERATOR_TO);
    }
    if (is_range_index) {
        // range indexes are slices; never coerce the range pointer through integer fast paths.
        MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
        MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);
        return emit_call_2(mt, "fn_index", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
    }

    // Boolean mask index: arr[mask] — when the index is a typed array, defer to
    // fn_index which routes ELEM_BOOL masks to the gather path. (Int fast paths
    // below would otherwise try to unbox the array as a scalar index.)
    if (idx_tid == LMD_TYPE_ARRAY_NUM) {
        MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
        MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);
        return emit_call_2(mt, "fn_index", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
    }

    if (!is_integer_type_id(idx_tid) && idx_tid != LMD_TYPE_ANY) {
        // range/string/type indexes have runtime semantics in fn_index; numeric fast paths corrupt them.
        MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
        MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);
        return emit_call_2(mt, "fn_index", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
    }

    // Extract elem_type for ARRAY_NUM objects (used by fast paths below)
    TypeId obj_elem_type = LMD_TYPE_ANY;
    if (obj_tid == LMD_TYPE_ARRAY_NUM) {
        AstNode* obj_unwrapped = field_node->object;
        while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
            obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
        if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
            AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
            char oname[128];
            snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
            MirVarEntry* ov = find_var(mt, oname);
            if (ov) obj_elem_type = ov->elem_type;
        }
    }

    // ======================================================================
    // FAST PATH 1: Compile-time known ArrayNum with int elements (from fill() narrowing)
    // No runtime type check needed — we KNOW it's ArrayNum with int items.
    // Returns NATIVE INT (not boxed), enabling native arithmetic downstream.
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY_NUM &&
        is_integer_type_id(obj_elem_type) &&
        (is_integer_type_id(idx_tid) || idx_tid == LMD_TYPE_ANY)) {
        MIR_reg_t idx_native;
        if (idx_tid == LMD_TYPE_ANY) {
            MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);
            idx_native = emit_unbox(mt, boxed_idx, LMD_TYPE_INT);
        } else {
            idx_native = transpile_expr(mt, field_node->field);
        }
        MIR_reg_t obj_item = transpile_expr(mt, field_node->object);  // tagged container Item
        MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);       // strip tag → raw ArrayNum*
        MirIndexLoadPolicy policy = {
            MIR_INDEX_STORAGE_ARRAY_NUM, MIR_T_I64, 8,
            MIR_INDEX_RESULT_NATIVE_INT, MIR_INDEX_GUARD_NONE, LMD_TYPE_ARRAY_NUM,
            MIR_INDEX_OOB_ITEM_NULL, MIR_INDEX_SLOW_NONE,
        };
        return emit_checked_index_load(mt, arr_ptr, 0, idx_native, policy);
    }

    // ======================================================================
    // FAST PATH 1a: Compile-time known ArrayNum with float elements (from fill() narrowing)
    // No runtime type check needed — we KNOW it's ArrayNum with double items.
    // Returns NATIVE FLOAT (double), enabling native FP arithmetic downstream.
    // ArrayNum stores raw doubles in float_items[], same struct layout:
    //   offset 0: type_id (uint8_t), offset 1: elem_type, offset 8: double* float_items, offset 16: int64_t length
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY_NUM &&
        obj_elem_type == LMD_TYPE_FLOAT &&
        (is_integer_type_id(idx_tid) || idx_tid == LMD_TYPE_ANY)) {
        MIR_reg_t idx_native;
        if (idx_tid == LMD_TYPE_ANY) {
            MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);
            idx_native = emit_unbox(mt, boxed_idx, LMD_TYPE_INT);
        } else {
            idx_native = transpile_expr(mt, field_node->field);
        }
        MIR_reg_t obj_item = transpile_expr(mt, field_node->object);  // tagged container Item
        MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);       // strip tag → raw ArrayFloat*
        MirIndexLoadPolicy policy = {
            MIR_INDEX_STORAGE_ARRAY_FLOAT, MIR_T_D, 8,
            MIR_INDEX_RESULT_NATIVE_FLOAT, MIR_INDEX_GUARD_NONE, LMD_TYPE_ARRAY_NUM,
            MIR_INDEX_OOB_FLOAT_ZERO, MIR_INDEX_SLOW_NONE,
        };
        return emit_checked_index_load(mt, arr_ptr, 0, idx_native, policy);
    }

    // ======================================================================
    // FAST PATH 1b: ARRAY type + INT index — compile-time typed inline read
    // The compile-time type is generic ARRAY, but when the AST records a
    // nested element type (e.g., nested=INT for [0,4,2,...]), we know the
    // runtime object is a typed array created by transpile_array.
    // Runtime type check needed because fn_array_set may convert ArrayInt
    // to generic Array in-place (e.g., arr[1] = 3.14 on an int array).
    // P4-3.2: nested_int now returns NATIVE INT (matching get_effective_type).
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY && is_integer_type_id(idx_tid)) {
        // Check if AST nested type suggests this was originally an int array
        Type* obj_type = field_node->object ? field_node->object->type : nullptr;
        bool nested_int = false;
        bool nested_bool = false;
        if (obj_type && obj_type->type_id == LMD_TYPE_ARRAY) {
            TypeArray* arr_type = (TypeArray*)obj_type;
            nested_int = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_INT;
            nested_bool = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_BOOL;
        }

        // P4-3.1: Check MirVarEntry elem_type (from fill() narrowing), unwrap PRIMARY
        if (!nested_bool) {
            AstNode* obj_unwrapped = field_node->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov && ov->elem_type == LMD_TYPE_BOOL) nested_bool = true;
            }
        }

        if (nested_int) {
            // P4-3.2: Likely ArrayInt — inline read with runtime type check.
            // Return type depends on whether elem_type is provably INT:
            //   safe_native_int=true  → NATIVE INT (matching get_effective_type=INT)
            //   safe_native_int=false → BOXED Item (AST nested can be wrong after mutation)
            bool safe_native_int = false;
            {
                AstNode* obj_unwrapped = field_node->object;
                while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                    obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
                if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                    AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                    char oname[128];
                    snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                    MirVarEntry* ov = find_var(mt, oname);
                    if (ov && ov->elem_type == LMD_TYPE_INT) safe_native_int = true;
                }
            }
            MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);

            MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
            MirIndexLoadPolicy policy = {
                MIR_INDEX_STORAGE_ARRAY_NUM, MIR_T_I64, 8,
                safe_native_int ? MIR_INDEX_RESULT_NATIVE_INT : MIR_INDEX_RESULT_BOXED_INT,
                MIR_INDEX_GUARD_CONTAINER_TYPE, LMD_TYPE_ARRAY_NUM,
                MIR_INDEX_OOB_ITEM_NULL, MIR_INDEX_SLOW_ITEM_AT,
            };
            return emit_checked_index_load(mt, arr_ptr, boxed_obj, idx_native, policy);

        } else if (nested_bool) {
            // ==============================================================
            // P4-3.1: Bool arrays store boxed Items, so the common load keeps
            // the generic-array guard and extracts the native low-bit result.
            MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);
            MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
            MirIndexLoadPolicy policy = {
                MIR_INDEX_STORAGE_BOXED_ITEMS, MIR_T_I64, 8,
                MIR_INDEX_RESULT_NATIVE_BOOL, MIR_INDEX_GUARD_CONTAINER_TYPE, LMD_TYPE_ARRAY,
                MIR_INDEX_OOB_ITEM_NULL, MIR_INDEX_SLOW_ITEM_AT,
            };
            return emit_checked_index_load(mt, arr_ptr, boxed_obj, idx_native, policy);

        } else {
            // Generic ARRAY — use item_at(boxed_obj, native_idx) to skip index dispatch
            MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
            MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
            return emit_call_2(mt, "item_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
        }
    }

    // ======================================================================
    // FAST PATH 1d: ARRAY + ANY index — unbox index, typed dispatch
    // Object is generic ARRAY, index type unknown. Unbox field with it2i().
    // P4-3.2: nested=INT returns NATIVE INT only when elem_type proven safe;
    // otherwise returns BOXED Item (backward-compatible with pre-P4-3.2).
    // For nested=BOOL (P4-3.1): inline items[idx], extract native bool.
    // Otherwise: item_at(boxed_obj, idx). Returns BOXED Item.
    // ======================================================================
    if (obj_tid == LMD_TYPE_ARRAY && idx_tid == LMD_TYPE_ANY) {
        Type* obj_type = field_node->object ? field_node->object->type : nullptr;
        bool nested_int = false;
        bool nested_bool = false;
        if (obj_type && obj_type->type_id == LMD_TYPE_ARRAY) {
            TypeArray* arr_type = (TypeArray*)obj_type;
            nested_int = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_INT;
            nested_bool = arr_type->nested && arr_type->nested->type_id == LMD_TYPE_BOOL;
        }

        // P4-3.1: Check MirVarEntry elem_type (from fill() narrowing), unwrap PRIMARY
        if (!nested_bool) {
            AstNode* obj_unwrapped = field_node->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov && ov->elem_type == LMD_TYPE_BOOL) nested_bool = true;
            }
        }

        MIR_reg_t boxed_field = transpile_box_item(mt, field_node->field);
        MIR_reg_t idx_native = emit_unbox(mt, boxed_field, LMD_TYPE_INT);  // it2i

        if (nested_int) {
            // P4-3.2: Likely ArrayInt — inline read with runtime type check.
            // Return NATIVE INT only when elem_type proven (safe_native_int);
            // otherwise return BOXED Item (AST nested may be wrong after mutation).
            bool safe_native_int = false;
            {
                AstNode* obj_unwrapped = field_node->object;
                while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                    obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
                if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                    AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                    char oname[128];
                    snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                    MirVarEntry* ov = find_var(mt, oname);
                    if (ov && ov->elem_type == LMD_TYPE_INT) safe_native_int = true;
                }
            }
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);

            MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
            MirIndexLoadPolicy policy = {
                MIR_INDEX_STORAGE_ARRAY_NUM, MIR_T_I64, 8,
                safe_native_int ? MIR_INDEX_RESULT_NATIVE_INT : MIR_INDEX_RESULT_BOXED_INT,
                MIR_INDEX_GUARD_CONTAINER_TYPE, LMD_TYPE_ARRAY_NUM,
                MIR_INDEX_OOB_ITEM_NULL, MIR_INDEX_SLOW_ITEM_AT,
            };
            return emit_checked_index_load(mt, arr_ptr, boxed_obj, idx_native, policy);

        } else if (nested_bool) {
            // ==============================================================
            // P4-3.1: ANY indexes are unboxed above; preserve the same boxed
            // storage, runtime guard, native-bool, and ItemNull policies.
            MIR_reg_t obj_item = transpile_expr(mt, field_node->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);
            MIR_reg_t boxed_obj = emit_box_container(mt, obj_item);
            MirIndexLoadPolicy policy = {
                MIR_INDEX_STORAGE_BOXED_ITEMS, MIR_T_I64, 8,
                MIR_INDEX_RESULT_NATIVE_BOOL, MIR_INDEX_GUARD_CONTAINER_TYPE, LMD_TYPE_ARRAY,
                MIR_INDEX_OOB_ITEM_NULL, MIR_INDEX_SLOW_ITEM_AT,
            };
            return emit_checked_index_load(mt, arr_ptr, boxed_obj, idx_native, policy);

        } else {
            // Generic ARRAY, ANY index — item_at(boxed_obj, it2i(field))
            MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
            return emit_call_2(mt, "item_at", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
        }
    }

    // ======================================================================
    // FAST PATH 2: Runtime type check for unknown containers with INT index
    // Object might be ArrayNum at runtime — check type tag.
    // Returns BOXED Item (type is unknown at compile time).
    // ======================================================================
    if (idx_tid == LMD_TYPE_INT && obj_tid == LMD_TYPE_ANY) {
        MIR_reg_t idx_native = transpile_expr(mt, field_node->field);
        MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);

        MIR_reg_t arr_ptr = emit_unbox_container(mt, boxed_obj);
        MirIndexLoadPolicy policy = {
            MIR_INDEX_STORAGE_ARRAY_NUM, MIR_T_I64, 8,
            MIR_INDEX_RESULT_BOXED_INT, MIR_INDEX_GUARD_BOXED_TYPE, LMD_TYPE_ARRAY_NUM,
            MIR_INDEX_OOB_ITEM_NULL, MIR_INDEX_SLOW_FN_INDEX,
        };
        return emit_checked_index_load(mt, arr_ptr, boxed_obj, idx_native, policy);

    }

    // ======================================================================
    // FAST PATH 3: ANY + ANY with inner INDEX_EXPR detection (Level 3)
    // When object is ANY and field is also ANY, check if the field expression
    // is an INDEX_EXPR into a typed int array. If so, the field produces an
    // integer Item and we can use item_at(obj, it2i(field)) instead of the
    // expensive fn_index double dispatch. Mirrors C2MIR D1 Level 3.
    // ======================================================================
    if (obj_tid == LMD_TYPE_ANY && idx_tid == LMD_TYPE_ANY) {
        bool field_known_int = false;
        AstNode* field_expr = field_node->field;
        // Unwrap PRIMARY wrapper
        if (field_expr && field_expr->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)field_expr;
            if (pri->expr) field_expr = pri->expr;
        }
        if (field_expr && field_expr->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* inner_idx = (AstFieldNode*)field_expr;
            // Check inner object's effective type (handles fill() narrowing)
            TypeId inner_obj_eff = get_effective_type(mt, inner_idx->object);
            if (inner_obj_eff == LMD_TYPE_ARRAY_NUM) {
                field_known_int = true;
            } else if (inner_obj_eff == LMD_TYPE_ARRAY) {
                // Check AST nested type
                Type* inner_type = inner_idx->object ? inner_idx->object->type : nullptr;
                if (inner_type && inner_type->type_id == LMD_TYPE_ARRAY) {
                    TypeArray* arr_type = (TypeArray*)inner_type;
                    if (arr_type->nested && (arr_type->nested->type_id == LMD_TYPE_INT
                        || arr_type->nested->type_id == LMD_TYPE_INT64)) {
                        field_known_int = true;
                    }
                }
            }
        }
        if (field_known_int) {
            // Field produces an integer Item, but unknown obj may still be VMap/map.
            MIR_reg_t boxed_field = transpile_box_item(mt, field_node->field);
            MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
            return emit_call_2(mt, "fn_index", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_field));
        }
    }

    // ======================================================================
    // DEFAULT: box both operands and call fn_index
    // ======================================================================
    MIR_reg_t boxed_obj = transpile_box_item(mt, field_node->object);
    MIR_reg_t boxed_idx = transpile_box_item(mt, field_node->field);

    return emit_call_2(mt, "fn_index", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_idx));
}

// ============================================================================
// Call expressions
// ============================================================================

static bool mir_call_may_suspend(AstCallNode* call_node) {
    if (!call_node) return false;
    AstNode* function = mir_unwrap_primary(call_node->function);
    if (function && function->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* info = ((AstSysFuncNode*)function)->fn_info;
        return info && info->is_async;
    }
    if (!function || !function->type || function->type->type_id != LMD_TYPE_FUNC ||
            !((TypeFunc*)function->type)->is_proc) return false;
    if (function->node_type != AST_NODE_IDENT) return true;
    NameEntry* entry = ((AstIdentNode*)function)->entry;
    AstNode* target = entry ? entry->node : NULL;
    if (!target || target->node_type != AST_NODE_PROC) return true;
    AstFuncNode* callee = (AstFuncNode*)target;
    return callee->analysis && callee->analysis->may_await;
}

static MIR_reg_t transpile_call_raw(MirTranspiler* mt, AstCallNode* call_node) {
    AstNode* fn_expr = call_node->function;

    AstNode* only_arg = call_node->argument;
    if (only_arg && !only_arg->next) {
        AstNode* target_expr = fn_expr;
        while (target_expr && target_expr->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* primary = (AstPrimaryNode*)target_expr;
            if (!primary->expr) break;
            target_expr = primary->expr;
        }
        bool callee_is_type_value = target_expr && target_expr->node_type == AST_NODE_TYPE;
        Type* target_type = callee_is_type_value ? ((AstNode*)call_node)->type : NULL;
        if (target_type && target_type->type_id == LMD_TYPE_NUM_SIZED) {
            MIR_reg_t boxed = transpile_box_item(mt, only_arg);
            NumSizedType num_type = type_num_sized_kind(target_type);
            return emit_call_2(mt, "coerce_num_sized", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)num_type));
        }
        if (target_type && target_type->type_id == LMD_TYPE_UINT64) {
            MIR_reg_t boxed = transpile_box_item(mt, only_arg);
            return emit_call_1(mt, "coerce_uint64", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
        }
    }

    // Check for system function calls
    if (fn_expr->node_type == AST_NODE_SYS_FUNC) {
        AstSysFuncNode* sys = (AstSysFuncNode*)fn_expr;
        SysFuncInfo* info = sys->fn_info;

        // Count arguments
        AstNode* arg = call_node->argument;
        int arg_count = 0;
        while (arg) { arg_count++; arg = arg->next; }

        if (info->fn == SYSPROC_PRINT) {
            arg = call_node->argument;
            bool first_print_arg = true;
            while (arg) {
                if (!first_print_arg) {
                    // Multi-arg print is one logical print call; keep the
                    // separator explicit so future config can replace it here.
                    MIR_reg_t sep_ptr = emit_call_2(mt, "heap_strcpy", MIR_T_P,
                        MIR_T_P, MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)" "),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, 1));
                    MIR_reg_t sep_item = emit_box_string(mt, sep_ptr);
                    emit_call_1(mt, "pn_print", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, sep_item));
                }
                MIR_reg_t boxed_arg = transpile_box_item(mt, arg);
                emit_call_1(mt, "pn_print", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_arg));
                first_print_arg = false;
                arg = arg->next;
            }
            return emit_null_item_reg(mt);
        }

        if (info->fn == SYSPROC_SELECT) {
            MIR_reg_t handles = emit_call_0(mt, "array", MIR_T_P);
            int handles_root = create_pointer_gc_root_slot(mt, handles);
            MIR_reg_t timeout = new_reg(mt, "select_timeout", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, timeout),
                MIR_new_int_op(mt->ctx, (int64_t)((uint64_t)LMD_TYPE_INT << 56))));
            for (arg = call_node->argument; arg; arg = arg->next) {
                if (arg->node_type == AST_NODE_NAMED_ARG) {
                    AstNamedNode* named = (AstNamedNode*)arg;
                    if (named->name && named->name->len == 7 &&
                            memcmp(named->name->chars, "timeout", 7) == 0) {
                        timeout = transpile_box_item(mt, named->as);
                        continue;
                    }
                }
                MIR_reg_t handle = transpile_box_item(mt, arg);
                int handle_root = create_gc_root_slot(mt, handle);
                handles = load_gc_root_slot(mt, handles_root, "select_handles");
                handle = load_gc_root_slot(mt, handle_root, "select_handle");
                emit_call_void_2(mt, "array_push",
                    MIR_T_P, MIR_new_reg_op(mt->ctx, handles),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, handle));
            }
            handles = load_gc_root_slot(mt, handles_root, "select_handles");
            MIR_reg_t handles_item = emit_call_1(mt, "array_end", MIR_T_I64,
                MIR_T_P, MIR_new_reg_op(mt->ctx, handles));
            async_emit_invoke_resume_point(mt, call_node);
            return emit_call_2(mt, "pn_select_mir", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, handles_item),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, timeout));
        }

        // ==== VMap: map() and map(arr) ====
        if (info->fn == SYSFUNC_VMAP_NEW) {
            if (arg_count == 0) {
                // map() -> vmap_new()
                return emit_call_0(mt, "vmap_new", MIR_T_I64);
            } else {
                // map([k1, v1, ...]) -> vmap_from_array(arr)
                arg = call_node->argument;
                MIR_reg_t a1 = transpile_expr(mt, arg);
                TypeId a1_tid = get_effective_type(mt, arg);
                MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);
                return emit_call_1(mt, "vmap_from_array", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
            }
        }
        // ==== VMap: m.set(k, v) -> vmap_set(m, k, v) ====
        if (info->fn == SYSPROC_VMAP_SET) {
            arg = call_node->argument;
            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = get_effective_type(mt, arg);
            MIR_reg_t boxed_a1 = emit_box(mt, a1, a1_tid);

            arg = arg->next;
            MIR_reg_t a2 = transpile_expr(mt, arg);
            TypeId a2_tid = get_effective_type(mt, arg);
            MIR_reg_t boxed_a2 = emit_box(mt, a2, a2_tid);

            arg = arg->next;
            MIR_reg_t a3 = transpile_expr(mt, arg);
            TypeId a3_tid = get_effective_type(mt, arg);
            MIR_reg_t boxed_a3 = emit_box(mt, a3, a3_tid);

            return emit_call_3(mt, "vmap_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3));
        }

        // ==== Native len() for typed collections/strings ====
        // When the argument type is known, dispatch to type-specific fn_len_* variants
        // that take native pointers and return int64_t directly (no boxing overhead).
        if (info->fn == SYSFUNC_LEN && arg_count == 1) {
            arg = call_node->argument;
            // State variables and other boxed locals may retain a narrower AST
            // type; native len_* helpers require the actual MIR raw-pointer form.
            TypeId arg_tid = get_effective_type(mt, arg);
            if (arg_tid == LMD_TYPE_ARRAY) {
                MIR_reg_t a1 = emit_unbox_container(mt, transpile_expr(mt, arg));
                return emit_call_1(mt, "fn_len_l", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            if (arg_tid == LMD_TYPE_ARRAY || arg_tid == LMD_TYPE_ARRAY_NUM) {
                MIR_reg_t a1 = emit_unbox_container(mt, transpile_expr(mt, arg));
                return emit_call_1(mt, "fn_len_a", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            if (is_text_type_id(arg_tid)) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                return emit_call_1(mt, "fn_len_s", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            if (arg_tid == LMD_TYPE_ELEMENT) {
                MIR_reg_t a1 = emit_unbox_container(mt, transpile_expr(mt, arg));
                return emit_call_1(mt, "fn_len_e", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
            // Fallback: use generic fn_len(Item) for unknown types (handled below)
        }

        // ==== Native starts_with/ends_with for typed strings ====
        // When both args are statically known as String*, dispatch to native fn_*_str variants
        // that take String* pointers and return Bool directly (no Item unboxing overhead).
        if (info->fn == SYSFUNC_STARTS_WITH && arg_count == 2) {
            arg = call_node->argument;
            AstNode* arg2 = arg->next;
            TypeId a1_tid = get_effective_type(mt, arg);
            TypeId a2_tid = arg2 ? get_effective_type(mt, arg2) : LMD_TYPE_ANY;
            if (a1_tid == LMD_TYPE_STRING && a2_tid == LMD_TYPE_STRING) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                MIR_reg_t a2 = transpile_expr(mt, arg2);
                return emit_call_2(mt, "fn_starts_with_str", MIR_T_I64,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, a1),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, a2));
            }
        }
        if (info->fn == SYSFUNC_ENDS_WITH && arg_count == 2) {
            arg = call_node->argument;
            AstNode* arg2 = arg->next;
            TypeId a1_tid = get_effective_type(mt, arg);
            TypeId a2_tid = arg2 ? get_effective_type(mt, arg2) : LMD_TYPE_ANY;
            if (a1_tid == LMD_TYPE_STRING && a2_tid == LMD_TYPE_STRING) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                MIR_reg_t a2 = transpile_expr(mt, arg2);
                return emit_call_2(mt, "fn_ends_with_str", MIR_T_I64,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, a1),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, a2));
            }
        }

        // ==== Native ord() for typed strings ====
        if (info->fn == SYSFUNC_ORD && arg_count == 1) {
            arg = call_node->argument;
            TypeId a1_tid = get_effective_type(mt, arg);
            if (a1_tid == LMD_TYPE_STRING) {
                MIR_reg_t a1 = transpile_expr(mt, arg);
                return emit_call_1(mt, "fn_ord_str", MIR_T_I64, MIR_T_P, MIR_new_reg_op(mt->ctx, a1));
            }
        }

        // ==== Bitwise functions: inline as native MIR instructions ====
        // band/bor/bxor → MIR_AND/MIR_OR/MIR_XOR (single instruction, no function call)
        // shl/shr → guarded: negative count is error; b >= 64 yields 0
        // bnot → MIR_XOR with -1 (equivalent to ~a)
        if (info->fn == SYSFUNC_USHR) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
            arg = arg->next;
            MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);
            return emit_call_2(mt, "fn_ushr_item", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2));
        }
        if (info->fn == SYSFUNC_BAND || info->fn == SYSFUNC_BOR ||
            info->fn == SYSFUNC_BXOR || info->fn == SYSFUNC_SHL ||
            info->fn == SYSFUNC_SHR) {
            arg = call_node->argument;
            TypeId call_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
            if (call_tid == LMD_TYPE_NUM_SIZED || call_tid == LMD_TYPE_UINT64) {
                const char* boxed_fn = NULL;
                switch (info->fn) {
                    case SYSFUNC_BAND: boxed_fn = "fn_band_item"; break;
                    case SYSFUNC_BOR:  boxed_fn = "fn_bor_item"; break;
                    case SYSFUNC_BXOR: boxed_fn = "fn_bxor_item"; break;
                    case SYSFUNC_SHL:  boxed_fn = "fn_shl_item"; break;
                    case SYSFUNC_SHR:  boxed_fn = "fn_shr_item"; break;
                    default: break;
                }
                MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
                arg = arg->next;
                MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);
                return emit_call_2(mt, boxed_fn, MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2));
            }

            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = get_effective_type(mt, arg);
            a1 = emit_bitwise_i64_arg(mt, a1, a1_tid);
            arg = arg->next;
            MIR_reg_t a2 = transpile_expr(mt, arg);
            TypeId a2_tid = get_effective_type(mt, arg);
            a2 = emit_bitwise_i64_arg(mt, a2, a2_tid);

            // band/bor/bxor: single MIR instruction, always safe
            MIR_insn_code_t mir_op = (MIR_insn_code_t)0;
            switch (info->fn) {
                case SYSFUNC_BAND: mir_op = MIR_AND; break;
                case SYSFUNC_BOR:  mir_op = MIR_OR;  break;
                case SYSFUNC_BXOR: mir_op = MIR_XOR; break;
                default: break;
            }
            if (mir_op) {
                MIR_reg_t result = new_reg(mt, "bw", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, mir_op,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, a1),
                    MIR_new_reg_op(mt->ctx, a2)));
                return result;
            }

            // shl/shr: guarded shift — negative is INT64_ERROR, b >= 64 is 0.
            {
                MIR_insn_code_t shift_op = (info->fn == SYSFUNC_SHL) ? MIR_LSH : MIR_RSH;
                MIR_reg_t result = new_reg(mt, "shft", MIR_T_I64);
                MIR_label_t l_ok = new_label(mt);
                MIR_label_t l_err = new_label(mt);
                MIR_label_t l_zero = new_label(mt);
                MIR_label_t l_end = new_label(mt);

                // check b >= 0
                MIR_reg_t neg_chk = new_reg(mt, "sneg", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_chk),
                    MIR_new_reg_op(mt->ctx, a2), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_err),
                    MIR_new_reg_op(mt->ctx, neg_chk)));

                // check b < 64
                MIR_reg_t ge_chk = new_reg(mt, "sge", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_chk),
                    MIR_new_reg_op(mt->ctx, a2), MIR_new_int_op(mt->ctx, 64)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_zero),
                    MIR_new_reg_op(mt->ctx, ge_chk)));

                // in range: do the shift
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

                emit_label(mt, l_err);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, INT64_ERROR)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

                emit_label(mt, l_zero);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
                    MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

                emit_label(mt, l_ok);
                emit_insn(mt, MIR_new_insn(mt->ctx, shift_op,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, a1),
                    MIR_new_reg_op(mt->ctx, a2)));

                emit_label(mt, l_end);
                return result;
            }
        }
        if (info->fn == SYSFUNC_BNOT) {
            arg = call_node->argument;
            TypeId call_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
            if (call_tid == LMD_TYPE_NUM_SIZED || call_tid == LMD_TYPE_UINT64) {
                MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
                return emit_call_1(mt, "fn_bnot_item", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
            }

            MIR_reg_t a1 = transpile_expr(mt, arg);
            TypeId a1_tid = get_effective_type(mt, arg);
            a1 = emit_bitwise_i64_arg(mt, a1, a1_tid);
            // ~a == a XOR -1
            MIR_reg_t result = new_reg(mt, "bnot", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_XOR,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_reg_op(mt->ctx, a1),
                MIR_new_int_op(mt->ctx, (int64_t)-1)));
            return result;
        }

        // Build runtime function name: "fn_" or "pn_" + name + optional arg count for overloaded
        char sys_fn_name[128];
        if (info->c_func_name && info->c_func_name[0]) {
            // descriptor-backed functions carry the exact lowered C symbol.
            snprintf(sys_fn_name, sizeof(sys_fn_name), "%s", info->c_func_name);
        } else if (info->is_overloaded) {
            snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s%d",
                info->is_proc ? "pn_" : "fn_", info->name, arg_count);
        } else {
            snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s",
                info->is_proc ? "pn_" : "fn_", info->name);
        }

        // For C_RET_RETITEM functions (returning 16-byte struct), use MIR wrapper
        // that returns Item. RetItem has ABI issues with MIR's i64 return type.
        if (info->c_ret_type == C_RET_RETITEM) {
            char mir_name[140];
            snprintf(mir_name, sizeof(mir_name), "%s_mir", sys_fn_name);
            memcpy(sys_fn_name, mir_name, sizeof(sys_fn_name));
        }

        // Determine the actual C return type based on the SysFunc enum value.
        // The return_type in SysFuncInfo is the Lambda-level semantic type, NOT the C type.
        // Some funcs with same Lambda return type (e.g., TYPE_STRING) return String* in C
        // while others return Item. We must use the enum to distinguish.
        TypeId c_ret_tid = LMD_TYPE_ANY;  // default: C function returns Item
        switch (info->fn) {
        // C functions returning int64_t
        case SYSFUNC_LEN: case SYSFUNC_INT64:
        case SYSFUNC_INDEX_OF: case SYSFUNC_LAST_INDEX_OF:
        case SYSFUNC_BAND: case SYSFUNC_BOR: case SYSFUNC_BXOR:
        case SYSFUNC_BNOT: case SYSFUNC_SHL: case SYSFUNC_SHR:
        case SYSFUNC_ORD:
            c_ret_tid = LMD_TYPE_INT; break;
        // C functions returning Bool (uint8_t)
        case SYSFUNC_CONTAINS: case SYSFUNC_STARTS_WITH: case SYSFUNC_ENDS_WITH:
        case SYSFUNC_EXISTS:
            c_ret_tid = LMD_TYPE_BOOL; break;
        // C functions returning String*
        case SYSFUNC_STRING: case SYSFUNC_FORMAT1: case SYSFUNC_FORMAT2:
            c_ret_tid = LMD_TYPE_STRING; break;
        // C functions returning Symbol*
        case SYSFUNC_NAME: case SYSFUNC_SYMBOL:
            c_ret_tid = LMD_TYPE_SYMBOL; break;
        // C functions returning Type*
        case SYSFUNC_TYPE:
            c_ret_tid = LMD_TYPE_TYPE; break;
        // C functions returning DateTime (uint64_t)
        case SYSFUNC_DATETIME: case SYSFUNC_DATETIME0:
        case SYSFUNC_DATE: case SYSFUNC_DATE0: case SYSFUNC_DATE3:
        case SYSFUNC_TIME: case SYSFUNC_TIME0: case SYSFUNC_TIME3:
        case SYSFUNC_JUSTNOW:
            c_ret_tid = LMD_TYPE_DTIME; break;
        // C functions returning double
        case SYSPROC_CLOCK:
            c_ret_tid = LMD_TYPE_FLOAT; break;
        default: break;  // returns Item, no boxing needed
        }
        MIR_type_t mir_ret_type = (c_ret_tid == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;

        // Helper lambda: post-process DateTime returns via push_k so variables store
        // DateTime* pointers (consistent with literals from emit_load_const).
        // DateTime C functions return raw uint64_t values, not pointers.
        #define POST_PROCESS_DTIME(result) \
            if (c_ret_tid == LMD_TYPE_DTIME) { result = emit_box_dtime_value(mt, result); }

        // Helper: when a sys func returns a boxed Item (c_ret_tid=ANY) but the
        // call expression has a specific native type, unbox to native format.
        // This ensures consistency with local/dynamic calls which also unbox
        // Items to native types (FLOAT→double, INT→int64, BOOL→int64, INT64→int64,
        // STRING→String*, SYMBOL→Symbol*).
        TypeId call_expr_tid = ((AstNode*)call_node)->type ? ((AstNode*)call_node)->type->type_id : LMD_TYPE_ANY;
        #define POST_PROCESS_UNBOX(result) \
            if (!mt->emitting_async_call && c_ret_tid == LMD_TYPE_ANY && \
                (is_native_numeric_or_bool_type_id(call_expr_tid) || \
                 is_text_type_id(call_expr_tid))) { \
                result = emit_unbox(mt, result, call_expr_tid); \
            }

        // For 0-arg functions like datetime(), date() etc
        if (arg_count == 0) {
            async_emit_invoke_resume_point(mt, call_node);
            MIR_reg_t result = emit_call_0(mt, sys_fn_name, mir_ret_type);
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // For 1-arg system functions
        if (arg_count == 1) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
            async_emit_invoke_resume_point(mt, call_node);
            MIR_reg_t result = emit_call_1(mt, sys_fn_name, mir_ret_type, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // For 2-arg system functions
        if (arg_count == 2) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);

            async_emit_invoke_resume_point(mt, call_node);
            MIR_reg_t result = emit_call_2(mt, sys_fn_name, mir_ret_type,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // 3-arg system functions
        if (arg_count == 3) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a3 = transpile_box_item(mt, arg);

            async_emit_invoke_resume_point(mt, call_node);
            MIR_reg_t result = emit_call_3(mt, sys_fn_name, mir_ret_type,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // 4-arg system functions use fixed C ABIs; routing them through the vararg fallback shifts arguments.
        if (arg_count == 4) {
            arg = call_node->argument;
            MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a3 = transpile_box_item(mt, arg);

            arg = arg->next;
            MIR_reg_t boxed_a4 = transpile_box_item(mt, arg);

            async_emit_invoke_resume_point(mt, call_node);
            MIR_reg_t result = emit_call_4(mt, sys_fn_name, mir_ret_type,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a3),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a4));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }

        // Fallback for more args: use vararg call
        {
            arg = call_node->argument;
            MIR_op_t arg_ops[16];
            int ai = 0;
            while (arg && ai < 16) {
                MIR_reg_t boxed = transpile_box_item(mt, arg);
                arg_ops[ai++] = MIR_new_reg_op(mt->ctx, boxed);
                arg = arg->next;
            }

            // Create unique vararg proto name per call site
            char proto_name[160];
            snprintf(proto_name, sizeof(proto_name), "%s_sp%d", sys_fn_name, mt->em.label_counter++);

            // First arg is mandatory, rest are varargs
            MIR_var_t first_arg = {MIR_T_I64, "a", 0};
            MIR_type_t res_types[1] = { mir_ret_type };
            MIR_item_t proto = MIR_new_vararg_proto_arr(mt->ctx, proto_name, 1, res_types, 1, &first_arg);
            MIR_item_t imp = MIR_new_import(mt->ctx, sys_fn_name);

            int nops = 3 + ai;
            MIR_op_t* ops = LAMBDA_ALLOCA(nops, MIR_op_t);
            ops[0] = MIR_new_ref_op(mt->ctx, proto);
            ops[1] = MIR_new_ref_op(mt->ctx, imp);
            MIR_reg_t result = new_reg(mt, "sys", mir_ret_type);
            ops[2] = MIR_new_reg_op(mt->ctx, result);
            for (int i = 0; i < ai; i++) ops[3 + i] = arg_ops[i];

            async_emit_invoke_resume_point(mt, call_node);
            emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
            POST_PROCESS_DTIME(result);
            POST_PROCESS_UNBOX(result);
            return result;
        }
    }

    // User-defined function call
    if (fn_expr->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)fn_expr;
        if (pri->expr) fn_expr = pri->expr;
    }

    if (fn_expr->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)fn_expr;
        AstNode* entry_node = ident->entry ? ident->entry->node : nullptr;

        // Handle imported function calls (from another module compiled via C transpiler)
        if (ident->entry && ident->entry->import && entry_node &&
            (entry_node->node_type == AST_NODE_FUNC ||
             entry_node->node_type == AST_NODE_FUNC_EXPR ||
             entry_node->node_type == AST_NODE_PROC)) {
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;

            // Determine the import function name:
            // Use boxed wrapper (_b) for typed-param functions, direct for untyped.
            // Also use _b when the function has inferred native params (e.g. i in arr[i]).
            // Include module index prefix to disambiguate functions with same name+offset
            // across different modules (e.g. fraction.ls and style.ls both have _render_574).
            StrBuf* fn_import_name = strbuf_new_cap(64);
            // JS/Python module exports are registered under their direct boxed
            // ABI name and never have a Lambda-generated _b companion.
            bool use_wrapper = !ident->entry->import->is_cross_lang;
            if (use_wrapper) {
                write_fn_name_ex(fn_import_name, fn_node, ident->entry->import, "_b");
            } else {
                write_fn_name(fn_import_name, fn_node, ident->entry->import);
            }
            log_debug("mir: imported function call '%s' (wrapper=%d)", fn_import_name->str, use_wrapper);

            // Count and box arguments
            AstNode* arg = call_node->argument;
            int arg_count = 0;
            while (arg) { arg_count++; arg = arg->next; }

            // Build resolved_args with named arg reordering if needed
            AstNode* resolved_args[16] = {0};
            bool has_named_args = false;
            arg = call_node->argument;
            while (arg) {
                if (arg->node_type == AST_NODE_NAMED_ARG) has_named_args = true;
                arg = arg->next;
            }

            if (has_named_args) {
                int positional_idx = 0;
                arg = call_node->argument;
                while (arg) {
                    if (arg->node_type == AST_NODE_NAMED_ARG) {
                        AstNamedNode* named_arg = (AstNamedNode*)arg;
                        int param_idx = 0;
                        AstNamedNode* param = fn_node->param;
                        while (param) {
                            if (param->name && named_arg->name &&
                                param->name->len == named_arg->name->len &&
                                memcmp(param->name->chars, named_arg->name->chars, param->name->len) == 0) {
                                if (param_idx < 16) resolved_args[param_idx] = named_arg->as;
                                break;
                            }
                            param_idx++;
                            param = (AstNamedNode*)((AstNode*)param)->next;
                        }
                    } else {
                        while (positional_idx < 16 && resolved_args[positional_idx]) positional_idx++;
                        if (positional_idx < 16) resolved_args[positional_idx++] = arg;
                    }
                    arg = arg->next;
                }
            } else {
                arg = call_node->argument;
                for (int i = 0; i < arg_count && i < 16; i++) {
                    resolved_args[i] = arg;
                    arg = arg->next;
                }
            }

            // Get expected param count
            int expected_params = arg_count;
            TypeFunc* call_fn_type = fn_node->type ? (TypeFunc*)fn_node->type : nullptr;
            if (call_fn_type && call_fn_type->type_id == LMD_TYPE_FUNC) {
                expected_params = call_fn_type->param_count;
            }

            // Box all args to Items
            MIR_op_t arg_ops[17];
            MIR_var_t arg_vars[17];
            int arg_root_slots[17];
            for (int i = 0; i < 17; i++) arg_root_slots[i] = -1;
            int ai = 0;
            for (int i = 0; i < expected_params && i < 16; i++) {
                if (resolved_args[i]) {
                    MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                    arg_root_slots[i] = create_gc_root_slot(mt, val);
                    arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                } else {
                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    MIR_reg_t null_reg = new_reg(mt, "pad", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, null_reg),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    arg_ops[i] = MIR_new_reg_op(mt->ctx, null_reg);
                }
                arg_vars[i] = {MIR_T_I64, "p", 0};
            }
            ai = expected_params;

            // Handle variadic args
            bool call_is_variadic = call_fn_type && call_fn_type->is_variadic;
            if (call_is_variadic && ai < 16) {
                int extra_count = arg_count - expected_params;
                if (extra_count < 0) extra_count = 0;
                MIR_reg_t vargs_reg;
                if (extra_count == 0) {
                    vargs_reg = new_reg(mt, "vargs", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vargs_reg),
                        MIR_new_int_op(mt->ctx, 0)));
                } else {
                    vargs_reg = emit_call_0(mt, "list", MIR_T_I64);
                    int vargs_root = create_gc_root_slot(mt, vargs_reg);
                    for (int i = expected_params; i < arg_count && i < 16; i++) {
                        if (resolved_args[i]) {
                            MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                            int val_root = create_gc_root_slot(mt, val);
                            val = load_gc_root_slot(mt, val_root, "varg_val");
                            vargs_reg = load_gc_root_slot(mt, vargs_root, "vargs_live");
                            emit_call_void_2(mt, "list_push",
                                MIR_T_P, MIR_new_reg_op(mt->ctx, vargs_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                        }
                    }
                    vargs_reg = load_gc_root_slot(mt, vargs_root, "vargs_live");
                }
                arg_ops[ai] = MIR_new_reg_op(mt->ctx, vargs_reg);
                arg_root_slots[ai] = create_gc_root_slot(mt, vargs_reg);
                arg_vars[ai] = {MIR_T_P, "va", 0};
                ai++;
            }

            for (int i = 0; i < ai; i++) {
                if (arg_root_slots[i] >= 0) {
                    MIR_reg_t live_arg = load_gc_root_slot(mt, arg_root_slots[i], "call_arg");
                    arg_ops[i] = MIR_new_reg_op(mt->ctx, live_arg);
                }
            }

            // Create proto and import for the cross-module call
            char proto_name[160];
            snprintf(proto_name, sizeof(proto_name), "%s_ip%d", fn_import_name->str, mt->em.label_counter++);

            // Import the target function
            MIR_item_t imp_item = MIR_new_import(mt->ctx, fn_import_name->str);

            MIR_reg_t result;
            if (use_wrapper && ai <= 8) {
                // _b wrappers return RetItem (16 bytes). On Windows x64, structs > 8 bytes
                // use hidden pointer return, which MIR cannot handle directly.
                // Route through fn_call_boxed_N trampoline: passes function pointer + args
                // to C code that calls the _b wrapper with correct ABI.
                char trampoline_name[32];
                snprintf(trampoline_name, sizeof(trampoline_name), "fn_call_boxed_%d", ai);

                // Load _b function address into a register
                MIR_reg_t fp_reg = new_reg(mt, "bfp", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, fp_reg),
                    MIR_new_ref_op(mt->ctx, imp_item)));

                // Build trampoline call: fn_call_boxed_N(fp, arg0, arg1, ...)
                MIR_var_t tramp_vars[17];
                tramp_vars[0] = {MIR_T_P, "fp", 0};
                for (int i = 0; i < ai; i++) tramp_vars[1 + i] = arg_vars[i];

                MirImportEntry* tramp_ie = ensure_import(mt, trampoline_name,
                    MIR_T_I64, 1 + ai, tramp_vars, 1);

                int nops = 3 + 1 + ai;  // proto + import + result + fp + args
                MIR_op_t ops[20];
                ops[0] = MIR_new_ref_op(mt->ctx, tramp_ie->proto);
                ops[1] = MIR_new_ref_op(mt->ctx, tramp_ie->import);
                result = new_reg(mt, "impcall", MIR_T_I64);
                ops[2] = MIR_new_reg_op(mt->ctx, result);
                ops[3] = MIR_new_reg_op(mt->ctx, fp_reg);
                for (int i = 0; i < ai; i++) ops[4 + i] = arg_ops[i];

                async_emit_invoke_resume_point(mt, call_node);
                emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
            } else {
                // Non-wrapper calls: function returns Item directly (MIR_T_I64)
                MIR_type_t res_types[1] = { MIR_T_I64 };
                MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, res_types, ai, arg_vars);

                int nops = 3 + ai;
                MIR_op_t ops[19];
                ops[0] = MIR_new_ref_op(mt->ctx, proto);
                ops[1] = MIR_new_ref_op(mt->ctx, imp_item);
                result = new_reg(mt, "impcall", MIR_T_I64);
                ops[2] = MIR_new_reg_op(mt->ctx, result);
                for (int i = 0; i < ai; i++) ops[3 + i] = arg_ops[i];

                async_emit_invoke_resume_point(mt, call_node);
                emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
            }

            // Import calls return boxed Items. Unbox to native type to match
            // local/system call behavior, so callers can re-box consistently.
            TypeId call_tid = get_effective_type(mt, (AstNode*)call_node);
            if (!mt->emitting_async_call && mir_is_native_scalar_value_type(call_tid)) {
                result = emit_unbox(mt, result, call_tid);
            }

            strbuf_free(fn_import_name);
            result = root_gc_result_if_needed(mt, result, MIR_T_I64, call_tid, "impcall_rv");
            return result;
        }

        // Try entry-based lookup first, then raw name fallback
        const char* fn_mangled = nullptr;
        StrBuf* name_buf = nullptr;
        MIR_item_t local_func = nullptr;

        // Guard: if the identifier is a variable/parameter (has a MIR var registered),
        // it holds a function value at runtime — must use dynamic dispatch, not direct call.
        // This prevents treating `render_fn(args)` as a direct call to the function
        // that was propagated to the parameter via AST type inference.
        char ident_name_str[128];
        snprintf(ident_name_str, sizeof(ident_name_str), "%.*s",
            (int)ident->name->len, ident->name->chars);
        MirVarEntry* ident_var = find_var(mt, ident_name_str);
        bool is_fn_variable = (ident_var != nullptr);

        if (!is_fn_variable && entry_node && (entry_node->node_type == AST_NODE_FUNC ||
            entry_node->node_type == AST_NODE_FUNC_EXPR ||
            entry_node->node_type == AST_NODE_PROC)) {
            AstFuncNode* fn_node = (AstFuncNode*)entry_node;
            name_buf = strbuf_new_cap(64);
            write_fn_name(name_buf, fn_node, ident->entry->import);
            fn_mangled = name_buf->str;
            local_func = find_local_func(mt, fn_mangled);
        }

        // Fallback: try raw name lookup ONLY when entry is NULL (unresolved) or mangled name not found
        // Do NOT use raw name fallback when entry_node exists but is not a function —
        // that means it's a variable (e.g. "let adder = ...") which should go through dynamic call
        char raw_name[128];
        if (!local_func && !entry_node) {
            snprintf(raw_name, sizeof(raw_name), "%.*s", (int)ident->name->len, ident->name->chars);
            local_func = find_local_func(mt, raw_name);
            if (local_func) fn_mangled = raw_name;
        }

        if (fn_mangled && (local_func || entry_node)) {
            log_debug("mir: DIRECT call '%s' local_func=%p entry_type=%d",
                fn_mangled, (void*)local_func,
                entry_node ? entry_node->node_type : -1);

            // ======== TCO interception ========
            // If this is a tail-recursive call to the current TCO function,
            // transform into: evaluate args → assign to params → goto tco_label
            if (mt->tco_func && mt->in_tail_position && is_recursive_call(call_node, mt->tco_func)) {
                log_debug("mir: TCO tail call to '%.*s' — converting to goto",
                    (int)mt->tco_func->name->len, mt->tco_func->name->chars);

                // Arguments are NOT in tail position
                bool saved_tail = mt->in_tail_position;
                mt->in_tail_position = false;

                // Phase 1: Evaluate all arguments into temporaries
                // This prevents aliasing when args reference parameters being overwritten (e.g., f(b, a))
                AstNode* arg = call_node->argument;
                AstNamedNode* param = mt->tco_func->param;
                MIR_reg_t temps[16];
                int arg_idx = 0;
                while (arg && param && arg_idx < 16) {
                    MIR_reg_t val = transpile_expr(mt, arg);
                    TypeId val_tid = get_effective_type(mt, arg);

                    // TCO rewrites run after parameter binding; use the resolved MIR
                    // local type, not the raw TypeParam wrapper, or typed float params
                    // can be treated as int when recursive args are assigned back.
                    char pname[64];
                    snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);
                    MirVarEntry* pvar = find_var(mt, pname);
                    TypeId param_tid = pvar ? pvar->type_id : LMD_TYPE_ANY;

                    // Convert to match parameter's representation
                    if (mir_is_native_param_type(param_tid)) {
                        if (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL) {
                            val = emit_unbox(mt, val, param_tid);
                        } else if (val_tid != param_tid) {
                            MIR_reg_t boxed = emit_box(mt, val, val_tid);
                            val = emit_unbox(mt, boxed, param_tid);
                        }
                    } else {
                        val = emit_box(mt, val, val_tid);
                    }

                    temps[arg_idx] = val;
                    arg_idx++;
                    arg = arg->next;
                    param = (AstNamedNode*)((AstNode*)param)->next;
                }

                // Phase 2: Assign temporaries to parameter registers
                param = mt->tco_func->param;
                for (int i = 0; i < arg_idx; i++) {
                    if (!param) break;
                    char pname[64];
                    snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);
                    MirVarEntry* pvar = find_var(mt, pname);
                    if (pvar) {
                        if (pvar->mir_type == MIR_T_D) {
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                MIR_new_reg_op(mt->ctx, pvar->reg),
                                MIR_new_reg_op(mt->ctx, temps[i])));
                        } else {
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, pvar->reg),
                                MIR_new_reg_op(mt->ctx, temps[i])));
                        }
                        update_gc_root_slot(mt, pvar);
                    }
                    param = (AstNamedNode*)((AstNode*)param)->next;
                }

                mt->in_tail_position = saved_tail;

                // Jump back to function start
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->tco_label)));
                mt->block_returned = true;

                // Return a type-shaped dummy register. The jump makes this value
                // unreachable, but enclosing expressions still use its MIR type.
                TypeId tail_tid = get_effective_type(mt, (AstNode*)call_node);
                MIR_type_t dummy_type = type_to_mir(tail_tid);
                MIR_reg_t dummy = new_reg(mt, "tco_dummy", dummy_type);
                if (dummy_type == MIR_T_D) {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                        MIR_new_reg_op(mt->ctx, dummy),
                        MIR_new_double_op(mt->ctx, 0.0)));
                } else {
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, dummy),
                        MIR_new_int_op(mt->ctx, 0)));
                }
                if (name_buf) strbuf_free(name_buf);
                return dummy;
            }

            // Get function definition for param info (defaults, named args)
            AstFuncNode* fn_def = NULL;
            if (entry_node && (entry_node->node_type == AST_NODE_FUNC ||
                entry_node->node_type == AST_NODE_FUNC_EXPR ||
                entry_node->node_type == AST_NODE_PROC)) {
                fn_def = (AstFuncNode*)entry_node;
            }

            // Get expected param count and variadic info
            int expected_params = 0;
            TypeFunc* call_fn_type = NULL;
            if (fn_def && fn_def->type && fn_def->type->type_id == LMD_TYPE_FUNC) {
                call_fn_type = (TypeFunc*)fn_def->type;
                expected_params = call_fn_type->param_count;
            }
            bool call_is_variadic = call_fn_type && call_fn_type->is_variadic;

            // Check if any arguments are named
            bool has_named_args = false;
            AstNode* arg = call_node->argument;
            int arg_count = 0;
            while (arg) {
                if (arg->node_type == AST_NODE_NAMED_ARG) has_named_args = true;
                arg_count++;
                arg = arg->next;
            }
            // Only bump expected_params to arg_count when we don't know the function signature
            // and the function is NOT variadic (variadic funcs collect extras into vargs)
            if (expected_params == 0 && !call_is_variadic) expected_params = arg_count;

            // Build resolved_args array: maps each parameter position to its argument expression
            AstNode* resolved_args[16] = {0};

            if (has_named_args && fn_def) {
                // Reorder: named args go to their param positions, positional fill sequentially
                int positional_idx = 0;
                arg = call_node->argument;
                while (arg) {
                    if (arg->node_type == AST_NODE_NAMED_ARG) {
                        AstNamedNode* named_arg = (AstNamedNode*)arg;
                        // Find param index by name
                        int param_idx = 0;
                        AstNamedNode* param = fn_def->param;
                        while (param) {
                            if (param->name && named_arg->name &&
                                param->name->len == named_arg->name->len &&
                                memcmp(param->name->chars, named_arg->name->chars, param->name->len) == 0) {
                                if (param_idx < 16) resolved_args[param_idx] = named_arg->as;
                                break;
                            }
                            param_idx++;
                            param = (AstNamedNode*)((AstNode*)param)->next;
                        }
                    } else {
                        // Skip positional slots already filled by named args
                        while (positional_idx < 16 && resolved_args[positional_idx]) positional_idx++;
                        if (positional_idx < 16) resolved_args[positional_idx++] = arg;
                    }
                    arg = arg->next;
                }
            } else {
                // Simple positional: fill in order
                arg = call_node->argument;
                for (int i = 0; i < arg_count && i < 16; i++) {
                    resolved_args[i] = arg;
                    arg = arg->next;
                }
            }

            // Phase 4: Check if target function has a native version for optimized calling
            NativeFuncInfo* call_nfi = fn_mangled ? find_native_func_info(mt, fn_mangled) : nullptr;
            bool native_call = (call_nfi && call_nfi->has_native && local_func);

            // Emit args in parameter order, filling defaults for missing slots
            MIR_op_t arg_ops[16];
            MIR_var_t arg_vars[16];
            int arg_root_slots[16];
            for (int i = 0; i < 16; i++) arg_root_slots[i] = -1;
            int ai = 0;
            AstNamedNode* param_iter = fn_def ? fn_def->param : NULL;
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            for (int i = 0; i < expected_params && i < 16; i++) {
                if (native_call && i < call_nfi->param_count &&
                    mir_is_native_param_type(call_nfi->param_types[i])) {
                    // Phase 4: Native param — pass native value directly (skip boxing)
                    TypeId param_tid = call_nfi->param_types[i];
                    if (resolved_args[i]) {
                        MIR_reg_t val = transpile_expr(mt, resolved_args[i]);
                        TypeId val_tid = get_effective_type(mt, resolved_args[i]);
                        if (val_tid == param_tid) {
                            // Direct native match — pass through
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                        } else if (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL) {
                            // Boxed Item → unbox to expected native type
                            MIR_reg_t unboxed = emit_unbox(mt, val, param_tid);
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, unboxed);
                        } else {
                            // Type mismatch: box → unbox (handles int→float etc.)
                            MIR_reg_t boxed = emit_box(mt, val, val_tid);
                            MIR_reg_t unboxed = emit_unbox(mt, boxed, param_tid);
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, unboxed);
                        }
                    } else {
                        // Default value for native param
                        TypeParam* tp = param_iter ? (TypeParam*)((AstNode*)param_iter)->type : NULL;
                        if (tp && tp->default_value) {
                            MIR_reg_t val = transpile_expr(mt, tp->default_value);
                            TypeId val_tid = get_effective_type(mt, tp->default_value);
                            if (val_tid == param_tid) {
                                arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                            } else {
                                MIR_reg_t boxed = emit_box(mt, val, val_tid);
                                MIR_reg_t unboxed = emit_unbox(mt, boxed, param_tid);
                                arg_ops[i] = MIR_new_reg_op(mt->ctx, unboxed);
                            }
                        } else {
                            // No value: pass 0 as native default
                            MIR_reg_t zero = new_reg(mt, "npad", MIR_T_I64);
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, zero),
                                MIR_new_int_op(mt->ctx, 0)));
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, zero);
                        }
                    }
                    arg_vars[i] = {call_nfi->param_mir[i], "p", 0};
                } else {
                    // Non-native param: standard boxed Item ABI
                    if (resolved_args[i]) {
                        MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                        arg_root_slots[i] = create_gc_root_slot(mt, val);
                        arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                    } else {
                        // Check for default value in TypeParam
                        TypeParam* tp = param_iter ? (TypeParam*)((AstNode*)param_iter)->type : NULL;
                        if (tp && tp->default_value) {
                            MIR_reg_t val = transpile_box_item(mt, tp->default_value);
                            arg_root_slots[i] = create_gc_root_slot(mt, val);
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, val);
                        } else {
                            MIR_reg_t null_reg = new_reg(mt, "pad", MIR_T_I64);
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, null_reg),
                                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                            arg_ops[i] = MIR_new_reg_op(mt->ctx, null_reg);
                        }
                    }
                    arg_vars[i] = {MIR_T_I64, "p", 0};
                }
                if (param_iter) param_iter = (AstNamedNode*)((AstNode*)param_iter)->next;
            }
            ai = expected_params;

            // Handle variadic arguments: collect extra args into a List*
            if (call_is_variadic && ai < 16) {
                int extra_count = arg_count - expected_params;
                if (extra_count < 0) extra_count = 0;

                MIR_reg_t vargs_reg;
                if (extra_count == 0) {
                    // No variadic args: pass null pointer
                    vargs_reg = new_reg(mt, "vargs", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, vargs_reg),
                        MIR_new_int_op(mt->ctx, 0)));
                } else {
                    // Create list and push extra args
                    vargs_reg = emit_call_0(mt, "list", MIR_T_I64);
                    int vargs_root = create_gc_root_slot(mt, vargs_reg);
                    for (int i = expected_params; i < arg_count && i < 16; i++) {
                        if (resolved_args[i]) {
                            MIR_reg_t val = transpile_box_item(mt, resolved_args[i]);
                            int val_root = create_gc_root_slot(mt, val);
                            val = load_gc_root_slot(mt, val_root, "varg_val");
                            vargs_reg = load_gc_root_slot(mt, vargs_root, "vargs_live");
                            emit_call_void_2(mt, "list_push",
                                MIR_T_P, MIR_new_reg_op(mt->ctx, vargs_reg),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                        }
                    }
                    vargs_reg = load_gc_root_slot(mt, vargs_root, "vargs_live");
                }
                arg_ops[ai] = MIR_new_reg_op(mt->ctx, vargs_reg);
                arg_root_slots[ai] = create_gc_root_slot(mt, vargs_reg);
                arg_vars[ai] = {MIR_T_P, "va", 0};
                ai++;
            }

            // P4-3.3: Return type — use native type when calling native version
            bool call_native_return = (native_call && call_nfi->return_type != LMD_TYPE_ANY);
            MIR_type_t ret_type = call_native_return ? call_nfi->return_mir : MIR_T_I64;
            bool call_error_lane = call_native_return && call_fn_type && call_fn_type->can_raise;
            LocalFuncEntry* local_entry = local_func && fn_mangled
                ? find_local_func_entry(mt, fn_mangled) : NULL;
            int scalar_home_id = 0;
            MIR_type_t call_types[16];
            MIR_op_t call_ops[16];
            for (int i = 0; i < ai; i++) {
                call_types[i] = arg_vars[i].type;
                call_ops[i] = arg_root_slots[i] >= 0
                    ? MIR_new_reg_op(mt->ctx, load_gc_root_slot(mt,
                        arg_root_slots[i], "call_arg")) : arg_ops[i];
            }

            async_emit_invoke_resume_point(mt, call_node);
            MIR_reg_t result = 0;
            if (local_func && local_entry && local_entry->variant) {
                MirCallOptions options = {{MIR_FRAME_REF_NONE, 0},
                    (uint8_t)(FN_RETURN_HOME_NORMAL | FN_RETURN_HOME_ERROR),
                    false};
                MirCallResult direct = em_call_direct(&mt->em, fn_mangled,
                    local_func, local_entry->variant, ai, call_types,
                    call_ops, &options);
                result = direct.normal.reg;
                scalar_home_id = direct.normal.scalar_home_id
                    ? direct.normal.scalar_home_id
                    : direct.error.scalar_home_id;
            } else {
                char proto_name[160];
                snprintf(proto_name, sizeof(proto_name), "%s_cp%d",
                    fn_mangled, mt->em.label_counter++);
                MIR_type_t res_types[1] = {ret_type};
                MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name,
                    1, res_types, ai, arg_vars);
                MIR_item_t target = local_func ? local_func
                    : MIR_new_import(mt->ctx, fn_mangled);
                MIR_op_t ops[19];
                ops[0] = MIR_new_ref_op(mt->ctx, proto);
                ops[1] = MIR_new_ref_op(mt->ctx, target);
                result = new_reg(mt, "call", ret_type);
                ops[2] = MIR_new_reg_op(mt->ctx, result);
                for (int i = 0; i < ai; i++) ops[3 + i] = call_ops[i];
                emit_insn(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL,
                    3 + ai, ops));
            }
            MIR_reg_t second_result = 0;
            if (call_error_lane) {
                second_result = new_reg(mt, "call_error", MIR_T_I64);
            }
            if (second_result) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, second_result),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                        offsetof(Context, mir_return_lane),
                        mt->em.frame.runtime, 0, 1)));
            }

            bool result_is_boxed = !call_native_return;
            if (call_error_lane) {
                MIR_reg_t boxed_native = emit_box(mt, result, call_nfi->return_type);
                MIR_reg_t merged = new_reg(mt, "call_value_or_error", MIR_T_I64);
                MIR_label_t use_error = new_label(mt);
                MIR_label_t merged_done = new_label(mt);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
                    MIR_new_label_op(mt->ctx, use_error),
                    MIR_new_reg_op(mt->ctx, second_result)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, merged),
                    MIR_new_reg_op(mt->ctx, boxed_native)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, merged_done)));
                emit_label(mt, use_error);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, merged),
                    MIR_new_reg_op(mt->ctx, second_result)));
                emit_label(mt, merged_done);
                result = merged;
                result_is_boxed = true;
            }
            if (scalar_home_id && result_is_boxed) {
                // Preserve caller-home identity across copies and lane joins.
                em_scalar_home_bind(&mt->em, scalar_home_id, result);
            }

            // P4-3.3: Post-call type handling for return values
            TypeId call_tid = get_effective_type(mt, (AstNode*)call_node);
            if (call_native_return && !result_is_boxed) {
                // Native function returns native value directly
                if (call_tid == call_nfi->return_type) {
                    // Perfect match — no conversion needed
                } else if (mir_is_native_scalar_value_type(call_tid)) {
                    // Caller expects different native type — box then unbox
                    MIR_reg_t boxed = emit_box(mt, result, call_nfi->return_type);
                    result = emit_unbox(mt, boxed, call_tid);
                } else {
                    // Caller expects boxed Item — box the native return
                    result = emit_box(mt, result, call_nfi->return_type);
                }
            } else {
                // Standard boxed return — unbox if caller expects native type
                if (!call_error_lane && !mt->emitting_async_call &&
                        mir_is_native_scalar_value_type(call_tid)) {
                    result = emit_unbox(mt, result, call_tid);
                }
            }

            if (name_buf) strbuf_free(name_buf);
            result = root_gc_result_if_needed(mt, result, ret_type, call_tid, "call_rv");
            return result;
        }
    }

    // Dynamic call via fn_call
    log_debug("mir: dynamic call - using fn_call");
    MIR_reg_t fn_val = transpile_expr(mt, call_node->function);
    TypeId fn_tid = get_effective_type(mt, call_node->function);
    MIR_reg_t boxed_fn = emit_box(mt, fn_val, fn_tid);

    // Count and box args
    AstNode* arg = call_node->argument;
    int arg_count = 0;
    while (arg) { arg_count++; arg = arg->next; }

    const char* call_fn = NULL;
    switch (arg_count) {
    case 0: call_fn = "fn_call0"; break;
    case 1: call_fn = "fn_call1"; break;
    case 2: call_fn = "fn_call2"; break;
    case 3: call_fn = "fn_call3"; break;
    default: call_fn = "fn_call"; break;
    }

    MIR_reg_t dyn_result;

    if (arg_count == 0) {
        async_emit_invoke_resume_point(mt, call_node);
        dyn_result = emit_call_1(mt, call_fn, MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn));
    } else if (arg_count <= 3) {
        MIR_reg_t args[3];
        arg = call_node->argument;
        for (int i = 0; i < arg_count; i++) {
            // Use transpile_box_item to correctly handle BINARY/UNARY with native returns
            args[i] = transpile_box_item(mt, arg);
            arg = arg->next;
        }
        async_emit_invoke_resume_point(mt, call_node);
        if (arg_count == 1) {
            dyn_result = emit_call_2(mt, call_fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[0]));
        } else if (arg_count == 2) {
            dyn_result = emit_call_3(mt, call_fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[0]),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, args[1]));
        } else {
            // 3 args
            MIR_var_t avars[4] = {{MIR_T_I64,"f",0},{MIR_T_I64,"a",0},{MIR_T_I64,"b",0},{MIR_T_I64,"c",0}};
            MirImportEntry* ie = ensure_import(mt, call_fn, MIR_T_I64, 4, avars, 1);
            dyn_result = new_reg(mt, "call3", MIR_T_I64);
            emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                MIR_new_ref_op(mt->ctx, ie->proto),
                MIR_new_ref_op(mt->ctx, ie->import),
                MIR_new_reg_op(mt->ctx, dyn_result),
                MIR_new_reg_op(mt->ctx, boxed_fn),
                MIR_new_reg_op(mt->ctx, args[0]),
                MIR_new_reg_op(mt->ctx, args[1]),
                MIR_new_reg_op(mt->ctx, args[2])));
        }
    } else {
        // More than 3 args: use fn_call with list
        log_error("mir: calls with >3 args not yet fully supported");
        dyn_result = boxed_fn;
    }

    // Dynamic calls (fn_call0/1/2/3) return Item (already boxed).
    // Unbox to native type to match direct call behavior, so callers
    // can re-box consistently based on AST type.
    TypeId call_tid = get_effective_type(mt, (AstNode*)call_node);
    if (!mt->emitting_async_call && mir_is_native_scalar_value_type(call_tid)) {
        dyn_result = emit_unbox(mt, dyn_result, call_tid);
    }
    return dyn_result;
}

static MIR_reg_t transpile_call(MirTranspiler* mt, AstCallNode* call_node) {
    bool split = mt->in_async_proc && mir_call_may_suspend(call_node);
    if (!split) return transpile_call_raw(mt, call_node);
    int state = ++mt->async_next_state;
    if (state > mt->async_state_count || !mt->async_state_labels) {
        log_error("concurrency MIR: await-state count mismatch at state %d/%d",
            state, mt->async_state_count);
        return transpile_call_raw(mt, call_node);
    }

    int saved_call_state = mt->async_call_state;
    int saved_call_spill_count = mt->async_call_spill_count;
    bool saved_resume_emitted = mt->async_call_resume_emitted;
    AstCallNode* saved_call_target = mt->async_call_target;
    mt->async_call_state = state;
    mt->async_call_spill_count = -1;
    mt->async_call_resume_emitted = false;
    mt->async_call_target = call_node;
    bool saved_async_call = mt->emitting_async_call;
    mt->emitting_async_call = true;
    MIR_reg_t result = transpile_call_raw(mt, call_node);
    mt->emitting_async_call = saved_async_call;
    int spill_count = mt->async_call_spill_count;
    bool resume_emitted = mt->async_call_resume_emitted;
    mt->async_call_state = saved_call_state;
    mt->async_call_spill_count = saved_call_spill_count;
    mt->async_call_resume_emitted = saved_resume_emitted;
    mt->async_call_target = saved_call_target;
    if (!resume_emitted || spill_count < 0) {
        log_error("concurrency MIR: missing replay-safe invoke boundary at state %d", state);
        return result;
    }

    async_emit_suspended_return(mt, result, state, spill_count);

    TypeId call_tid = get_effective_type(mt, (AstNode*)call_node);
    if (mir_is_native_scalar_value_type(call_tid) || is_text_type_id(call_tid)) {
        result = emit_unbox(mt, result, call_tid);
    }
    return result;
}

static MIR_reg_t transpile_start(MirTranspiler* mt, AstStartNode* start_node) {
    AstCallNode* call = start_node ? start_node->call : NULL;
    if (!call) return emit_null_item_reg(mt);
    MIR_reg_t function = transpile_box_item(mt, call->function);
    int function_root = create_gc_root_slot(mt, function);
    MIR_reg_t args = emit_call_0(mt, "list", MIR_T_P);
    int args_root = create_pointer_gc_root_slot(mt, args);
    for (AstNode* arg = call->argument; arg; arg = arg->next) {
        MIR_reg_t value = transpile_box_item(mt,
            arg->node_type == AST_NODE_NAMED_ARG ? ((AstNamedNode*)arg)->as : arg);
        int value_root = create_gc_root_slot(mt, value);
        function = load_gc_root_slot(mt, function_root, "start_fn");
        (void)function;
        args = load_gc_root_slot(mt, args_root, "start_args");
        value = load_gc_root_slot(mt, value_root, "start_arg");
        emit_call_void_2(mt, "list_push",
            MIR_T_P, MIR_new_reg_op(mt->ctx, args),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
    }
    function = load_gc_root_slot(mt, function_root, "start_fn");
    args = load_gc_root_slot(mt, args_root, "start_args");
    return emit_call_3(mt, "lambda_task_start_function_scoped", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, function),
        MIR_T_P, MIR_new_reg_op(mt->ctx, args),
        MIR_T_I64, MIR_new_int_op(mt->ctx, start_node->escapes ? 1 : 0));
}

// ============================================================================
// Pipe expressions
// ============================================================================

static MIR_reg_t transpile_pipe(MirTranspiler* mt, AstPipeNode* pipe_node) {
    MIR_reg_t left = transpile_expr(mt, pipe_node->left);
    TypeId left_tid = get_effective_type(mt, pipe_node->left);
    MIR_reg_t boxed_left = emit_box(mt, left, left_tid);

    bool uses_current_item = has_current_item_ref(pipe_node->right);

    // Simple aggregate pipe without ~ : data | func -> func(data)
    if (!uses_current_item && pipe_node->op == OPERATOR_PIPE) {
        // Check for pipe call injection: data | func(args) -> func(data, args)
        // This avoids generating fn_name1 for overloaded sys functions that only have fn_name2
        AstNode* right_node = pipe_node->right;
        if (right_node && right_node->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)right_node;
            if (pri->expr) right_node = pri->expr;
        }
        if (right_node && right_node->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call_node = (AstCallNode*)right_node;
            AstNode* fn_expr = call_node->function;
            if (fn_expr && fn_expr->node_type == AST_NODE_SYS_FUNC) {
                AstSysFuncNode* sys = (AstSysFuncNode*)fn_expr;
                SysFuncInfo* info = sys->fn_info;
                if (info) {
                    // build the correct function name using the fn_info (already resolved to N+1 version)
                    char sys_fn_name[128];
                    if (info->c_func_name && info->c_func_name[0]) {
                        // descriptor-backed functions carry the exact lowered C symbol.
                        snprintf(sys_fn_name, sizeof(sys_fn_name), "%s", info->c_func_name);
                    } else if (info->is_overloaded) {
                        snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s%d",
                            info->is_proc ? "pn_" : "fn_", info->name, info->arg_count);
                    } else {
                        snprintf(sys_fn_name, sizeof(sys_fn_name), "%s%s",
                            info->is_proc ? "pn_" : "fn_", info->name);
                    }

                    // count call arguments
                    AstNode* arg = call_node->argument;
                    int arg_count = 0;
                    while (arg) { arg_count++; arg = arg->next; }

                    // emit call with left injected as first argument
                    if (arg_count == 0) {
                        return emit_call_1(mt, sys_fn_name, MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
                    } else if (arg_count == 1) {
                        arg = call_node->argument;
                        MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
                        return emit_call_2(mt, sys_fn_name, MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1));
                    } else if (arg_count == 2) {
                        arg = call_node->argument;
                        MIR_reg_t boxed_a1 = transpile_box_item(mt, arg);
                        arg = arg->next;
                        MIR_reg_t boxed_a2 = transpile_box_item(mt, arg);
                        return emit_call_3(mt, sys_fn_name, MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a1),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_a2));
                    }
                    // fall through for more args — use fn_pipe_call
                }
            }
        }

        MIR_reg_t right = transpile_expr(mt, pipe_node->right);
        TypeId right_tid = get_effective_type(mt, pipe_node->right);
        MIR_reg_t boxed_right = emit_box(mt, right, right_tid);
        return emit_call_2(mt, "fn_pipe_call", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
    }

    // Iterating pipe (uses ~ or WHERE/THAT) - generate inline loop
    // Create result array
    MIR_reg_t result_arr = emit_call_0(mt, "array", MIR_T_P);

    // Check if collection is a MAP or VMAP (maps need key-based iteration)
    MIR_reg_t type_id_reg = emit_uext8(mt, emit_call_1(mt, "item_type_id", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left)));

    MIR_reg_t is_map = new_reg(mt, "is_map", MIR_T_I64);
    MIR_reg_t is_map_t2 = new_reg(mt, "is_map_t2", MIR_T_I64);
    MIR_reg_t is_vmap_t2 = new_reg(mt, "is_vmap_t2", MIR_T_I64);
    MIR_reg_t is_obj_t2 = new_reg(mt, "is_obj_t2", MIR_T_I64);
    MIR_reg_t is_element_pipe = new_reg(mt, "is_element_pipe", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_map_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_MAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_vmap_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_VMAP)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_obj_t2),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_OBJECT)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_element_pipe),
        MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_ELEMENT)));
    MIR_reg_t is_map_or_vmap2 = new_reg(mt, "is_map_or_vmap2", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_map_or_vmap2),
        MIR_new_reg_op(mt->ctx, is_map_t2), MIR_new_reg_op(mt->ctx, is_vmap_t2)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_map),
        MIR_new_reg_op(mt->ctx, is_map_or_vmap2), MIR_new_reg_op(mt->ctx, is_obj_t2)));

    // Pre-declare len, keys_al registers
    MIR_reg_t len = new_reg(mt, "pipe_len", MIR_T_I64);
    MIR_reg_t keys_al = new_reg(mt, "pipe_keys", MIR_T_P);

    // Initialize keys_al to NULL
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_non_map = new_label(mt);
    MIR_label_t l_start_loop = new_label(mt);

    // Branch: if not map, use normal fn_len path
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_non_map),
        MIR_new_reg_op(mt->ctx, is_map)));

    // MAP path: get keys and length from keys
    MIR_reg_t keys_result = emit_call_1(mt, "item_keys", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_new_reg_op(mt->ctx, keys_result)));
    MIR_reg_t map_len = emit_call_1(mt, "pipe_map_len", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
        MIR_new_reg_op(mt->ctx, map_len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_start_loop)));

    // NON-MAP path: use fn_len
    emit_label(mt, l_non_map);
    MIR_reg_t arr_len = emit_call_1(mt, "fn_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
        MIR_new_reg_op(mt->ctx, arr_len)));

    // Scalar pipe handling: if fn_len returns 0 and left is not a collection type,
    // treat as single-element iteration (e.g., 42 | ~ * 2 => [84])
    MIR_reg_t is_scalar = new_reg(mt, "is_scalar", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, is_scalar),
        MIR_new_int_op(mt->ctx, 0)));
    {
        MIR_label_t l_not_scalar = new_label(mt);
        // Only check for scalar if len == 0
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_not_scalar),
            MIR_new_reg_op(mt->ctx, arr_len)));
        // Check if type_id is a collection type — if so, it's truly empty (not scalar)
        MIR_reg_t is_arr = new_reg(mt, "is_arr", MIR_T_I64);
        MIR_reg_t is_aint = new_reg(mt, "is_aint", MIR_T_I64);
        MIR_reg_t is_range = new_reg(mt, "is_range", MIR_T_I64);
        MIR_reg_t is_elmt = new_reg(mt, "is_elmt", MIR_T_I64);
        MIR_reg_t is_str = new_reg(mt, "is_str", MIR_T_I64);
        MIR_reg_t is_null = new_reg(mt, "is_null", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_arr),
            MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_aint),
            MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY_NUM)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_range),
            MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_RANGE)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_elmt),
            MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_ELEMENT)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_str),
            MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_STRING)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_null),
            MIR_new_reg_op(mt->ctx, type_id_reg), MIR_new_int_op(mt->ctx, LMD_TYPE_NULL)));
        MIR_reg_t is_coll = new_reg(mt, "is_coll", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_coll),
            MIR_new_reg_op(mt->ctx, is_arr), MIR_new_reg_op(mt->ctx, is_aint)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_coll),
            MIR_new_reg_op(mt->ctx, is_coll), MIR_new_reg_op(mt->ctx, is_range)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_coll),
            MIR_new_reg_op(mt->ctx, is_coll), MIR_new_reg_op(mt->ctx, is_elmt)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_coll),
            MIR_new_reg_op(mt->ctx, is_coll), MIR_new_reg_op(mt->ctx, is_str)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, is_coll),
            MIR_new_reg_op(mt->ctx, is_coll), MIR_new_reg_op(mt->ctx, is_null)));
        // If it IS a collection type, skip (it's truly empty)
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_not_scalar),
            MIR_new_reg_op(mt->ctx, is_coll)));
        // It's a scalar: set len=1, is_scalar=1
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, len),
            MIR_new_int_op(mt->ctx, 1)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, is_scalar),
            MIR_new_int_op(mt->ctx, 1)));
        emit_label(mt, l_not_scalar);
    }

    emit_label(mt, l_start_loop);

    // Index counter
    MIR_reg_t idx = new_reg(mt, "pipe_i", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_loop = new_label(mt);
    MIR_label_t l_continue = new_label(mt);
    MIR_label_t l_end = new_label(mt);

    emit_label(mt, l_loop);
    // Exit when idx >= len
    MIR_reg_t cmp = new_reg(mt, "pipe_cmp", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));

    // Get pipe_item and pipe_index based on collection type
    MIR_reg_t pipe_item = new_reg(mt, "pipe_item", MIR_T_I64);
    MIR_reg_t pipe_index = new_reg(mt, "pipe_idx_item", MIR_T_I64);

    MIR_label_t l_arr_item = new_label(mt);
    MIR_label_t l_got_item = new_label(mt);

    // Branch: if not map, use item_at
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_arr_item),
        MIR_new_reg_op(mt->ctx, is_map)));

    // MAP path: get value and key from map using keys ArrayList
    MIR_reg_t map_val = emit_call_3(mt, "pipe_map_val", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_item),
        MIR_new_reg_op(mt->ctx, map_val)));
    MIR_reg_t map_key = emit_call_2(mt, "pipe_map_key", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_index),
        MIR_new_reg_op(mt->ctx, map_key)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_got_item)));

    // ARRAY/LIST/RANGE path: use item_at (or scalar directly)
    emit_label(mt, l_arr_item);
    MIR_label_t l_scalar_item = new_label(mt);
    MIR_label_t l_arr_done = new_label(mt);
    // If is_scalar, use boxed_left directly as pipe_item
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_scalar_item),
        MIR_new_reg_op(mt->ctx, is_scalar)));
    {
        MIR_label_t l_indexed_item = new_label(mt);
        MIR_label_t l_child_item_done = new_label(mt);
        // Element pipes iterate children only; attributes are aggregate keys, not stream rows.
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_indexed_item),
            MIR_new_reg_op(mt->ctx, is_element_pipe)));
        MIR_reg_t child_item = emit_call_4(mt, "iter_val_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
            MIR_T_P, MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_item),
            MIR_new_reg_op(mt->ctx, child_item)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_child_item_done)));

        emit_label(mt, l_indexed_item);
        MIR_reg_t arr_item = emit_call_2(mt, "item_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_left),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, idx));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_item),
            MIR_new_reg_op(mt->ctx, arr_item)));
        emit_label(mt, l_child_item_done);
    }
    {
        MIR_reg_t arr_idx = emit_box_int(mt, idx);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_index),
            MIR_new_reg_op(mt->ctx, arr_idx)));
    }
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_arr_done)));
    // Scalar path: use boxed_left directly as the item
    emit_label(mt, l_scalar_item);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_item),
        MIR_new_reg_op(mt->ctx, boxed_left)));
    {
        MIR_reg_t scalar_idx = emit_box_int(mt, idx);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, pipe_index),
            MIR_new_reg_op(mt->ctx, scalar_idx)));
    }
    emit_label(mt, l_arr_done);

    emit_label(mt, l_got_item);

    // Save old pipe state and set new
    bool old_in_pipe = mt->in_pipe;
    MIR_reg_t old_pipe_item = mt->pipe_item_reg;
    MIR_reg_t old_pipe_index = mt->pipe_index_reg;
    mt->in_pipe = true;
    mt->pipe_item_reg = pipe_item;
    mt->pipe_index_reg = pipe_index;

    // Evaluate right-side expression
    MIR_reg_t right_val = transpile_expr(mt, pipe_node->right);
    // Use get_effective_type to account for expressions where AST type (e.g. BOOL
    // from chained comparison transformation) doesn't match runtime behavior
    // (e.g. boxed AND returning Item when ~ has type ANY)
    TypeId right_tid = get_effective_type(mt, (AstNode*)pipe_node->right);
    MIR_reg_t boxed_right = emit_box(mt, right_val, right_tid);

    // Restore old pipe state
    mt->in_pipe = old_in_pipe;
    mt->pipe_item_reg = old_pipe_item;
    mt->pipe_index_reg = old_pipe_index;

    if (pipe_node->op == OPERATOR_WHERE) {
        // Filter: only push original item if predicate is truthy
        MIR_reg_t truthy;
        if (right_tid == LMD_TYPE_BOOL) {
            // right_val is already native Bool (0/1), use directly
            truthy = right_val;
        } else {
            // Need to box and check truthiness
            truthy = emit_uext8(mt, emit_call_1(mt, "is_truthy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right)));
        }
        MIR_label_t l_skip = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_skip),
            MIR_new_reg_op(mt->ctx, truthy)));
        // Push original item
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, result_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, pipe_item));
        emit_label(mt, l_skip);
    } else {
        // Map: push transformed result
        emit_call_void_2(mt, "array_push", MIR_T_P, MIR_new_reg_op(mt->ctx, result_arr),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_right));
    }

    // Continue: increment index
    emit_label(mt, l_continue);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_loop)));

    emit_label(mt, l_end);
    emit_call_void_1(mt, "symbol_key_list_free",
        MIR_T_P, MIR_new_reg_op(mt->ctx, keys_al));

    // Finalize array — array_end returns Item
    MIR_reg_t final = emit_call_1(mt, "array_end", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(mt->ctx, result_arr));
    return final;
}

// ============================================================================
// Raise expressions
// ============================================================================

static MIR_reg_t transpile_raise(MirTranspiler* mt, AstRaiseNode* raise_node) {
    if (raise_node->value) {
        // Evaluate the raise value and return it directly (like C transpiler)
        // The value is typically already an error from error() call
        MIR_reg_t val = transpile_expr(mt, raise_node->value);
        TypeId val_tid = get_effective_type(mt, raise_node->value);
        MIR_reg_t boxed = emit_box(mt, val, val_tid);
        // Return the error value from the function
        transpile_task_scope_unwind(mt, true);
        async_complete_frame(mt);
        emit_function_error_return(mt, boxed);
        // Dummy register for unreachable code after return
        MIR_reg_t r = new_reg(mt, "raise_dummy", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    MIR_reg_t r = new_reg(mt, "err", MIR_T_I64);
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    transpile_task_scope_unwind(mt, true);
    async_complete_frame(mt);
    emit_function_error_return(mt, r);
    return r;
}

// ============================================================================
// Return statement
// ============================================================================

static MIR_reg_t transpile_return(MirTranspiler* mt, AstReturnNode* ret_node) {
    // TCO: return value is in tail position — if this is a recursive call,
    // it can be converted to a goto jump instead of a function call.
    bool saved_tail = mt->in_tail_position;
    if (mt->tco_func) {
        mt->in_tail_position = true;
    }

    // P4-3.4: Check if current function has native return type
    TypeId native_ret = mt->native_return_tid;

    // Helper: emit restore_vargs before returning from variadic function
    auto emit_vargs_restore = [&]() {
        if (mt->in_variadic_body) {
            MIR_reg_t saved_vargs = MIR_reg(mt->ctx, "_saved_vargs", mt->em.func);
            emit_call_void_1(mt, "restore_vargs", MIR_T_P, MIR_new_reg_op(mt->ctx, saved_vargs));
        }
    };

    if (ret_node->value) {
        MIR_reg_t val = transpile_expr(mt, ret_node->value);
        // If TCO converted the inner call to a goto, block_returned is already set.
        // Skip the boxing/ret — it's dead code and would emit after a JMP.
        if (mt->block_returned) {
            mt->in_tail_position = saved_tail;
            return val;
        }

        // Use get_effective_type instead of AST type to match the actual
        // register type returned by transpile_expr. AST may say FLOAT but
        // the variable is stored as ANY (boxed Item / I64) when params are
        // untyped, causing emit_box_float to receive an I64 register → MIR
        // type validation error "Got 'int', expected 'double'".
        TypeId val_tid = get_effective_type(mt, ret_node->value);

        if (native_ret != LMD_TYPE_ANY) {
            // P4-3.4: Native return — emit unboxed value matching function's return type
            MIR_reg_t native_val;
            if (val_tid == native_ret) {
                // Perfect match — use value directly
                native_val = val;
            } else if (val_tid == LMD_TYPE_ANY || val_tid == LMD_TYPE_NULL) {
                // Boxed Item → unbox to native type
                native_val = emit_unbox(mt, val, native_ret);
            } else {
                // Different native type — box then unbox
                MIR_reg_t boxed = emit_box(mt, val, val_tid);
                native_val = emit_unbox(mt, boxed, native_ret);
            }
            emit_vargs_restore();
            transpile_task_scope_unwind(mt, false);
            async_complete_frame(mt);
            emit_function_return(mt, MIR_new_reg_op(mt->ctx, native_val));
        } else {
            // Standard: box and return
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            boxed = emit_coerce_boxed_to_declared(mt, boxed, mt->current_return_type);
            emit_vargs_restore();
            transpile_task_scope_unwind(mt, false);
            async_complete_frame(mt);
            emit_function_return(mt, MIR_new_reg_op(mt->ctx, boxed));
        }
    } else {
        // Return with no value
        emit_vargs_restore();
        transpile_task_scope_unwind(mt, false);
        async_complete_frame(mt);
        if (native_ret == LMD_TYPE_FLOAT) {
            emit_function_return(mt, MIR_new_double_op(mt->ctx, 0.0));
        } else if (native_ret != LMD_TYPE_ANY) {
            emit_function_return(mt, MIR_new_int_op(mt->ctx, 0));
        } else {
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_function_return(mt, MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL));
        }
    }

    mt->in_tail_position = saved_tail;

    MIR_type_t dummy_type = (native_ret == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
    MIR_reg_t r = new_reg(mt, "ret_dummy", dummy_type);
    if (dummy_type == MIR_T_D) {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_double_op(mt->ctx, 0.0)));
    } else {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
    }
    mt->block_returned = true;
    return r;
}

// ============================================================================
// Assignment statement (procedural)
// ============================================================================

static MIR_reg_t transpile_assign_stam(MirTranspiler* mt, AstAssignStamNode* assign) {
    MIR_reg_t val = transpile_expr(mt, assign->value);
    TypeId val_tid = get_effective_type(mt, assign->value);

    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)assign->target->len, assign->target->chars);

    MirVarEntry* var = find_var(mt, name_buf);
    if (var) {
        TypeId var_tid = var->type_id;

        bool keep_mutable_alias = mir_var_rhs_keeps_mutable_alias(assign->value);
        if (mir_expr_may_return_container(assign->value, val_tid, var_tid) && !keep_mutable_alias) {
            // Phase 5 COW anchor for reassignment: storing a container into a
            // var slot must detach it before later interior writes. Direct
            // aliases from mutable roots and borrowed-var calls keep identity.
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            val = emit_call_1(mt, "fn_mutable_value", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
            // fn_mutable_value returns a boxed Item; assignment widening and
            // follow-up boxing must see the actual register representation.
            val_tid = LMD_TYPE_ANY;
        }

        // Check if the new value type matches the variable's tracked type.
        // INT and INT64 are both raw int64 in MIR registers (MIR_T_I64) —
        // treat them as compatible for direct move. This prevents re-boxing
        // when e.g. len() (INT64) result participates in INT arithmetic.
        bool same_int_family = is_integer_type_id(var_tid) && is_integer_type_id(val_tid);
        if (var_tid == LMD_TYPE_NUM_SIZED) {
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            MIR_reg_t coerced = emit_call_2(mt, "coerce_num_sized", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)var->num_type));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, coerced)));
        } else if (var_tid == LMD_TYPE_UINT64) {
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            MIR_reg_t coerced = emit_call_1(mt, "coerce_uint64", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, coerced)));
        } else if (var_tid == val_tid || same_int_family ||
            (var_tid == LMD_TYPE_ANY && val_tid == LMD_TYPE_ANY)) {
            // Same type (or INT/INT64 compatible): direct move
            if (var->mir_type == MIR_T_D) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, val)));
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, val)));
            }
        } else if (var_tid == LMD_TYPE_ANY && val_tid != LMD_TYPE_ANY) {
            // Variable is boxed (ANY), value is typed: box the value and store
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            if (var->mir_type != MIR_T_I64) {
                MIR_reg_t any_reg = new_reg(mt, "anyv", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, any_reg),
                    MIR_new_reg_op(mt->ctx, boxed)));
                var->reg = any_reg;
                var->mir_type = MIR_T_I64;
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, boxed)));
            }
        } else if (var_tid == LMD_TYPE_FLOAT && val_tid == LMD_TYPE_INT) {
            // var is float, assigning int: convert int -> float
            MIR_reg_t fval = new_reg(mt, "i2d", MIR_T_D);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_I2D,
                MIR_new_reg_op(mt->ctx, fval), MIR_new_reg_op(mt->ctx, val)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_new_reg_op(mt->ctx, fval)));
        } else if (var_tid == LMD_TYPE_INT && val_tid == LMD_TYPE_FLOAT) {
            // var is int, assigning float value.
            // Inside loops: MIR registers are fixed-type; the while condition
            // code was already emitted for INT, so we must truncate to preserve
            // type consistency. Outside loops: safe to widen by boxing to ANY.
            if (mt->loop_depth > 0) {
                MIR_reg_t ival = new_reg(mt, "d2i", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_D2I,
                    MIR_new_reg_op(mt->ctx, ival), MIR_new_reg_op(mt->ctx, val)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, ival)));
            } else {
                // Outside loops: box and widen to ANY to preserve float value
                MIR_reg_t boxed = emit_box(mt, val, val_tid);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, boxed)));
                var->type_id = LMD_TYPE_ANY;
                var->mir_type = MIR_T_I64;
            }
        } else if (val_tid == LMD_TYPE_ANY &&
                   (is_native_numeric_or_bool_type_id(var_tid) ||
                    var_tid == LMD_TYPE_STRING)) {
            // Value is boxed (e.g., from IDIV/MOD runtime call) but variable
            // is native. Unbox to maintain type consistency — critical for loops
            // where the condition code is emitted once with native types.
            // INT64 included: len() and similar functions return raw int64 values
            // stored in INT64 variables; boxed fallback results must be unboxed.
            // String included: boxed fn_join results in branch assignments must
            // not widen empty-string accumulators to branch-local ANY registers.
            // Error items (div-by-zero) get silently converted to 0/0.0/false.
            MIR_reg_t unboxed = emit_unbox(mt, val, var_tid);
            if (var->mir_type == MIR_T_D) {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, unboxed)));
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, unboxed)));
            }
            // Variable stays at its native type
        } else {
            // Type mismatch (e.g. null->int, int->string): box the value and
            // switch to ANY type so the variable always holds a valid boxed Item
            MIR_reg_t boxed = emit_box(mt, val, val_tid);
            if (var->mir_type != MIR_T_I64) {
                MIR_reg_t any_reg = new_reg(mt, "anyv", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, any_reg),
                    MIR_new_reg_op(mt->ctx, boxed)));
                var->reg = any_reg;
                var->mir_type = MIR_T_I64;
                var->type_id = LMD_TYPE_ANY;
            } else {
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, boxed)));
                var->type_id = LMD_TYPE_ANY;
                var->mir_type = MIR_T_I64;
            }
        }

        // Write-back for captured variables: if this variable came from a
        // closure env, store the updated value back so subsequent calls
        // to the closure see the mutations (persistent mutable state).
        if (var->env_offset >= 0 && mt->env_reg) {
            // Box the current value to store back as Item
            MIR_reg_t boxed_wb = emit_box(mt, var->reg, var->type_id);
            int env_count = 0;
            for (FnCapture* cap = mt->current_closure ? mt->current_closure->captures : NULL;
                    cap; cap = cap->next) env_count++;
            emit_call_void_4(mt, "owned_item_slot_store",
                MIR_T_P, MIR_new_reg_op(mt->ctx, mt->env_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, env_count),
                MIR_T_I64, MIR_new_int_op(mt->ctx, var->env_offset / 8),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_wb));
        }

        update_gc_root_slot(mt, var);

        // Write-back for state variables: persist the updated value to the
        // central template state store so it survives across re-renders.
        if (var->is_state_var && mt->in_view_context && var->state_name_ptr) {
            MIR_reg_t boxed_sv = emit_box(mt, var->reg, var->type_id);
            // call tmpl_state_set(model_item, template_ref, state_name, value)
            // emit as void call with 4 args: (Item, ptr, ptr, Item)
            MIR_var_t sv_args[4] = {
                {MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0},
                {MIR_T_I64, "c", 0}, {MIR_T_I64, "d", 0}
            };
            MirImportEntry* sv_ie = ensure_import(mt, "tmpl_state_set", MIR_T_I64, 4, sv_args, 0);
            emit_insn(mt, MIR_new_call_insn(mt->ctx, 6,
                MIR_new_ref_op(mt->ctx, sv_ie->proto),
                MIR_new_ref_op(mt->ctx, sv_ie->import),
                MIR_new_reg_op(mt->ctx, mt->view_model_reg),
                MIR_new_int_op(mt->ctx, (int64_t)mt->view_template_ref),
                MIR_new_int_op(mt->ctx, (int64_t)var->state_name_ptr),
                MIR_new_reg_op(mt->ctx, boxed_sv)));
            log_debug("mir: state write-back for '%s'", name_buf);
        }
    } else {
        log_error("mir: assignment to undefined variable '%s'", name_buf);
    }

    return val;
}

// ============================================================================
// Box item: emit boxing for an expression node (produces Item from any type)
// ============================================================================

static MIR_reg_t transpile_box_item(MirTranspiler* mt, AstNode* node) {
    if (!node) {
        MIR_reg_t null_r = new_reg(mt, "null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, null_r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return null_r;
    }

    // ASSIGN_STAM nodes have NULL type but return the expression value.
    // After assignment, look up the variable's tracked type to box correctly.
    if (node->node_type == AST_NODE_ASSIGN_STAM) {
        AstAssignStamNode* assign = (AstAssignStamNode*)node;
        transpile_expr(mt, node); // execute the assignment (side effect)
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)assign->target->len, assign->target->chars);
        MirVarEntry* var = find_var(mt, name_buf);
        if (var) {
            return emit_box(mt, var->reg, var->type_id);
        }
        // Fallback: return null Item
        MIR_reg_t null_r = new_reg(mt, "null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, null_r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return null_r;
    }

    if (!node->type) {
        log_debug("mir: box_item: node_type=%d has NULL type, skipping boxing",
            node->node_type);
        MIR_reg_t val = transpile_expr(mt, node);
        return val;
    }

    // Use effective type, which accounts for variables stored differently
    // than their AST type (e.g. optional params stored as boxed ANY)
    TypeId tid = get_effective_type(mt, node);
    log_debug("mir: box_item: node_type=%d, type_id=%d, is_literal=%d",
        node->node_type, tid, node->type->is_literal);

    // PRIMARY nodes are transparent wrappers (parenthesized expressions).
    // Always delegate to the inner expression for correct type-aware boxing.
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) {
            return transpile_box_item(mt, pri->expr);
        }
    }

    // For literals of constant types (string, symbol, etc), use const boxing.
    // Only apply to PRIMARY nodes which are actual literals - variables (IDENT)
    // may have is_literal=true on their type from type inference propagation
    // but should NOT load from the const pool.
    if (node->type->is_literal && node->node_type == AST_NODE_PRIMARY) {
        switch (tid) {
        case LMD_TYPE_NULL: {
            MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
        case LMD_TYPE_BOOL: {
            MIR_reg_t val = transpile_expr(mt, node);
            return emit_box_bool(mt, val);
        }
        case LMD_TYPE_INT: {
            MIR_reg_t val = transpile_expr(mt, node);
            return emit_box_int(mt, val);
        }
        case LMD_TYPE_INT64: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_INT64);
        }
        case LMD_TYPE_FLOAT: {
            TypeFloat* t = (TypeFloat*)node->type;
            Item encoded = lambda_float_ptr_to_item(&t->double_val);
            MIR_reg_t reg = new_reg(mt, "fconst", MIR_T_I64);
            // Literal floats already have a stable TypeFloat slot, so emit the
            // canonical Item bits directly instead of re-tagging the const pointer.
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, reg),
                MIR_new_int_op(mt->ctx, (int64_t)encoded.item)));
            return reg;
        }
        case LMD_TYPE_FLOAT64: {
            TypeFloat* t = (TypeFloat*)node->type;
            Item encoded = lambda_float_ptr_to_item(&t->double_val);
            MIR_reg_t reg = new_reg(mt, "fconst", MIR_T_I64);
            // f64 is an alias for runtime float Items; keep its literal encoding
            // identical to float so equality and unboxing see one canonical shape.
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, reg),
                MIR_new_int_op(mt->ctx, (int64_t)encoded.item)));
            return reg;
        }
        case LMD_TYPE_STRING: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_STRING);
        }
        case LMD_TYPE_SYMBOL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_SYMBOL);
        }
        case LMD_TYPE_DTIME: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_box_dtime(mt, emit_load_const(mt, tc->const_index, MIR_T_P));
        }
        case LMD_TYPE_DECIMAL: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_DECIMAL);
        }
        case LMD_TYPE_BINARY: {
            TypeConst* tc = (TypeConst*)node->type;
            return emit_load_const_boxed(mt, tc->const_index, LMD_TYPE_BINARY);
        }
        default:
            break;
        }
    }

    // Evaluate expression then box. A final pn-body if/else reaches here as
    // the implicit return value; preserve branch results instead of using the
    // proc statement discard path.
    bool saved_preserve_proc_if_result = mt->preserve_proc_if_result;
    if (node->node_type == AST_NODE_IF_EXPR) {
        mt->preserve_proc_if_result = true;
    }
    MIR_reg_t val = transpile_expr(mt, node);
    mt->preserve_proc_if_result = saved_preserve_proc_if_result;

    // If the expression already emitted a return (e.g. RETURN_STAM in a proc),
    // the val is a dummy register and any further boxing would be dead code.
    // Skip boxing to avoid type mismatches (e.g. trying to box an I64 dummy as FLOAT).
    if (mt->block_returned) return val;

    // Boxed/unknown expressions already produce Item registers. Do this before
    // binary/unary heuristics so mixed boxed-native arithmetic is not reboxed
    // using a stale scalar expectation.
    if (tid == LMD_TYPE_ANY || tid == LMD_TYPE_ERROR || tid == LMD_TYPE_NULL) {
        return val;
    }

    // For BINARY/UNARY nodes: check if the result is already a boxed Item
    // from the runtime fallback path (not native handling).
    // transpile_binary returns NATIVE values only for specific type+op combos;
    // everything else goes through boxed runtime and returns an already-boxed Item.
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* bi = (AstBinaryNode*)node;
        TypeId lt = get_effective_type(mt, bi->left);
        TypeId rt = get_effective_type(mt, bi->right);
        bool both_int = (lt == LMD_TYPE_INT) && (rt == LMD_TYPE_INT);
        bool both_float = (lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_FLOAT);
        bool int_float = (lt == LMD_TYPE_INT && rt == LMD_TYPE_FLOAT) ||
                         (lt == LMD_TYPE_FLOAT && rt == LMD_TYPE_INT);
        bool both_bool = (lt == LMD_TYPE_BOOL) && (rt == LMD_TYPE_BOOL);
        bool both_string = (lt == LMD_TYPE_STRING) && (rt == LMD_TYPE_STRING);
        int op = bi->op;

        // Enumerate ALL cases where transpile_binary returns native values:

        // 1. Comparison results. Must mirror transpile_binary's native-vs-fallback
        // decision (same lt/rt):
        //   • vectorized (array operand)  → already a boxed ELEM_BOOL mask Item
        //   • EQ/NE                        → native Bool (uint8) → box it
        //   • LT-GE, both native numeric   → native MIR comparison → box it
        //   • LT-GE, otherwise             → fn_lt/gt/le/ge already returned a boxed
        //                                    Item (boxed bool or mask) → return as-is
        bool is_cmp = (op >= OPERATOR_EQ && op <= OPERATOR_GE) || is_elementwise_comparison_op(op);
        if (is_cmp) {
            if (comparison_vectorizes(op, lt, rt)) {
                return val;  // already a boxed mask Item from vec_cmp
            }
            if (is_elementwise_comparison_op(op)) return val;  // scalar keyword compare returns boxed bool
            if (op == OPERATOR_EQ || op == OPERATOR_NE) return emit_box_bool(mt, val);
            bool l_native = (is_native_numeric_type_id(lt));
            bool r_native = (is_native_numeric_type_id(rt));
            if (l_native && r_native) return emit_box_bool(mt, val);
            return val;  // fn_lt/gt/le/ge fallback already returned a boxed Item
        }

        // 1b. IS, IS_NAN, IN, and AT also return native Bool from runtime helpers
        if (op == OPERATOR_IS || op == OPERATOR_IS_NAN ||
            op == OPERATOR_IN || op == OPERATOR_AT) {
            return emit_box_bool(mt, val);
        }

        // 2. AND/OR with both_bool → native bool
        if ((op == OPERATOR_AND || op == OPERATOR_OR) && both_bool) {
            return emit_box_bool(mt, val);
        }

        // 3. both_int arithmetic: ADD/SUB/MUL now use boxed helpers so compact-int
        // overflow can promote safely; IDIV/MOD remain native int, DIV native float.
        //    POW falls through to boxed runtime (AST type is ANY)
        if (both_int) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
                return val;
            case OPERATOR_IDIV: case OPERATOR_MOD:
                return emit_box_int(mt, val);
            case OPERATOR_DIV:
                return emit_box_float(mt, val);
            default:
                return val; // POW, JOIN, etc. → boxed fallback
            }
        }

        // 3b. INT64 binary results: all INT64 ops go through the boxed fallback
        //     (no native INT64 handling), so results are already boxed Items.
        //     Comparisons already handled by is_cmp above.
        bool left_int64_b = (lt == LMD_TYPE_INT64);
        bool right_int64_b = (rt == LMD_TYPE_INT64);
        bool has_int64_b = left_int64_b || right_int64_b;
        if (has_int64_b) {
            return val; // already a boxed Item from the generic fallback
        }

        // 4. both_float: ADD,SUB,MUL,DIV,MOD → native float
        //    IDIV,POW fall through to boxed runtime
        if (both_float) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
            case OPERATOR_DIV: case OPERATOR_MOD:
                return emit_box_float(mt, val);
            default:
                return val; // IDIV,POW with float → boxed fallback
            }
        }

        // 5. int_float: ADD,SUB,MUL,DIV,MOD → native float
        //    IDIV,POW fall through to boxed runtime
        if (int_float) {
            switch (op) {
            case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
            case OPERATOR_DIV: case OPERATOR_MOD:
                return emit_box_float(mt, val);
            default:
                return val; // IDIV,POW with int_float → boxed fallback
            }
        }

        // 6. JOIN with both_string → native String*
        if (op == OPERATOR_JOIN && both_string) {
            return emit_box_string(mt, val);
        }

        // Everything else: transpile_binary used boxed runtime → result is already boxed Item
        return val;
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* un = (AstUnaryNode*)node;
        TypeId ct = get_effective_type(mt, un->operand);
        int uop = un->op;

        // NEG: native for INT (→int), FLOAT (→float); otherwise boxed
        if (uop == OPERATOR_NEG) {
            if (ct == LMD_TYPE_INT) return emit_box_int(mt, val);
            if (ct == LMD_TYPE_FLOAT) return emit_box_float(mt, val);
            return val; // fn_neg → boxed
        }
        // NOT: always returns native bool (MIR_EQ for BOOL operand, fn_not returns Bool/uint8_t)
        if (uop == OPERATOR_NOT) {
            return emit_box_bool(mt, val);
        }
        // POS: for native numeric types, box as that type; otherwise fn_pos already returns boxed Item
        if (uop == OPERATOR_POS) {
            if (ct == LMD_TYPE_INT) return emit_box_int(mt, val);
            if (ct == LMD_TYPE_INT64) return emit_box_int64(mt, val);
            if (ct == LMD_TYPE_FLOAT) return emit_box_float(mt, val);
            return val; // fn_pos returns boxed Item for non-numeric operands
        }
        // IS_ERROR: always returns native bool regardless of operand type
        if (uop == OPERATOR_IS_ERROR) {
            return emit_box_bool(mt, val);
        }
        // Default: assume boxed
        return val;
    }

    // For LIST type, list_end already returns Item - return as-is
    if (tid == LMD_TYPE_ARRAY && node->node_type == AST_NODE_CONTENT) {
        return val;
    }

    // INDEX_EXPR fast paths may return ITEM_NULL for out-of-bounds access.
    // Check at runtime to preserve NULL (suppressed in output) instead of
    // re-boxing it as a native 0 value (e.g. emit_box_int(ITEM_NULL) → boxed INT 0).
    // Only for integer-typed results — float fast paths return 0.0 in a double register
    // which can't be compared with MIR_BNE against an int operand.
    if (node->node_type == AST_NODE_INDEX_EXPR &&
        tid != LMD_TYPE_FLOAT) {
        MIR_label_t l_not_null = MIR_new_label(mt->ctx);
        MIR_label_t l_done = MIR_new_label(mt->ctx);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        MIR_reg_t boxed = new_reg(mt, "idx_box", MIR_T_I64);

        // if val != ITEM_NULL → goto normal boxing
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BNE,
            MIR_new_label_op(mt->ctx, l_not_null),
            MIR_new_reg_op(mt->ctx, val),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        // OOB null: preserve as-is
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, boxed),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, l_done)));
        // Normal: box the value
        emit_label(mt, l_not_null);
        {
            MIR_reg_t box_result = emit_box(mt, val, tid);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, boxed),
                MIR_new_reg_op(mt->ctx, box_result)));
        }
        emit_label(mt, l_done);
        return boxed;
    }

    if (node->node_type == AST_NODE_CALL_EXPR && tid == LMD_TYPE_INT64) {
        AstCallNode* call = (AstCallNode*)node;
        if (call->function && call->function->node_type == AST_NODE_SYS_FUNC) {
            // Legacy INT64-valued runtime helpers still use INT64_ERROR as
            // their error channel. Preserve that adapter at the helper ABI;
            // language-level two-lane returns use the full-domain boxer.
            return emit_call_1(mt, "box_int64_result_or_error", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }
    }

    return emit_box(mt, val, tid);
}

// ============================================================================
// Base type expressions (for match patterns, type checks)
// ============================================================================

static MIR_reg_t transpile_base_type(MirTranspiler* mt, AstTypeNode* type_node) {
    // base_type(type_id) returns a Type* for runtime type checking
    TypeId tid = type_node->type ? type_node->type->type_id : LMD_TYPE_ANY;

    // If this is a TypeType, get the actual type and check for special cases
    if (type_node->type && type_node->type->type_id == LMD_TYPE_TYPE) {
        TypeType* tt = (TypeType*)type_node->type;
        if (tt->type) {
            // For datetime sub-types (date, time), load the specific LIT_TYPE_DATE/TIME pointer
            // because date/time/dtime share the same type_id (LMD_TYPE_DTIME)
            extern Type TYPE_DATE, TYPE_TIME;
            extern TypeType LIT_TYPE_DATE, LIT_TYPE_TIME;
            if (tt->type == &TYPE_DATE) {
                MIR_reg_t r = new_reg(mt, "tdate", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_DATE)));
                return r;
            }
            if (tt->type == &TYPE_TIME) {
                MIR_reg_t r = new_reg(mt, "ttime", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_TIME)));
                return r;
            }
            // For 'list' bare keyword: emit &LIT_TYPE_LIST directly so fn_is returns BOOL_FALSE
            // (LMD_TYPE_LIST no longer exists; 'list' type never matches at runtime)
            extern Type TYPE_LIST;
            extern TypeType LIT_TYPE_LIST;
            if (tt->type == &TYPE_LIST) {
                MIR_reg_t r = new_reg(mt, "tlist", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_LIST)));
                return r;
            }
            // `number` has no runtime TypeId; keep its TypeType singleton instead of base_type(LMD_TYPE_TYPE).
            extern Type TYPE_NUMBER;
            extern TypeType LIT_TYPE_NUMBER;
            if (tt->type == &TYPE_NUMBER) {
                MIR_reg_t r = new_reg(mt, "tnumber", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_NUMBER)));
                return r;
            }
            // `integer` is an abstract numeric domain like `number`; preserve its singleton.
            extern Type TYPE_INTEGER;
            extern TypeType LIT_TYPE_INTEGER;
            if (tt->type == &TYPE_INTEGER) {
                MIR_reg_t r = new_reg(mt, "tinteger", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)&LIT_TYPE_INTEGER)));
                return r;
            }
            // For NUM_SIZED sub-types (i8..f32), load the specific LIT_TYPE_Xxx pointer
            // so fn_is can distinguish between different sized type checks
            if (tt->type->type_id == LMD_TYPE_NUM_SIZED) {
                extern TypeType LIT_TYPE_I8, LIT_TYPE_I16, LIT_TYPE_I32;
                extern TypeType LIT_TYPE_U8, LIT_TYPE_U16, LIT_TYPE_U32;
                extern TypeType LIT_TYPE_F16, LIT_TYPE_F32;
                TypeType* sized_lit = nullptr;
                switch ((NumSizedType)tt->type->kind) {
                    case NUM_INT8:    sized_lit = &LIT_TYPE_I8;  break;
                    case NUM_INT16:   sized_lit = &LIT_TYPE_I16; break;
                    case NUM_INT32:   sized_lit = &LIT_TYPE_I32; break;
                    case NUM_UINT8:   sized_lit = &LIT_TYPE_U8;  break;
                    case NUM_UINT16:  sized_lit = &LIT_TYPE_U16; break;
                    case NUM_UINT32:  sized_lit = &LIT_TYPE_U32; break;
                    case NUM_FLOAT16: sized_lit = &LIT_TYPE_F16; break;
                    case NUM_FLOAT32: sized_lit = &LIT_TYPE_F32; break;
                    default: break;
                }
                if (sized_lit) {
                    MIR_reg_t r = new_reg(mt, "tsized", MIR_T_I64);
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)sized_lit)));
                    return r;
                }
            }
            tid = tt->type->type_id;
        }
    }

    return emit_call_1(mt, "base_type", MIR_T_P, MIR_T_I64, MIR_new_int_op(mt->ctx, tid));
}

// Emit const_type_with_tl(type_index, type_list_ptr) for complex types that need
// runtime type_index lookup. Uses module-local type_list to support cross-module calls.
static MIR_reg_t transpile_const_type(MirTranspiler* mt, int type_index) {
    return emit_call_2(mt, "const_type_with_tl", MIR_T_P,
        MIR_T_I64, MIR_new_int_op(mt->ctx, type_index),
        MIR_T_P, MIR_new_reg_op(mt->ctx, emit_load_module_type_list(mt)));
}

// ============================================================================
// Main expression dispatcher
// ============================================================================

static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node) {
    if (!node) {
        log_error("mir: null expression node");
        MIR_reg_t r = new_reg(mt, "null", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }

    switch (node->node_type) {
    case AST_NODE_PRIMARY:
        return transpile_primary(mt, (AstPrimaryNode*)node);
    case AST_NODE_IDENT:
        return transpile_ident(mt, (AstIdentNode*)node);
    case AST_NODE_BINARY:
        return transpile_binary(mt, (AstBinaryNode*)node);
    case AST_NODE_UNARY:
        return transpile_unary(mt, (AstUnaryNode*)node);
    case AST_NODE_SPREAD:
        return transpile_spread(mt, (AstUnaryNode*)node);
    case AST_NODE_IF_EXPR:
        return transpile_if(mt, (AstIfNode*)node);
    case AST_NODE_MATCH_EXPR:
        return transpile_match(mt, (AstMatchNode*)node);
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM:
        return transpile_for(mt, (AstForNode*)node);
    case AST_NODE_WHILE_STAM:
        return transpile_while(mt, (AstWhileNode*)node);
    case AST_NODE_BREAK_STAM: {
        if (mt->loop_depth > 0) {
            // A loop jump bypasses the lexical block tail, so unwind its task
            // scopes here before transferring control out of the body.
            transpile_task_scope_unwind_to(mt,
                mt->loop_stack[mt->loop_depth - 1].task_scope_base, false);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
        }
        MIR_reg_t r = new_reg(mt, "brk", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_CONTINUE_STAM: {
        if (mt->loop_depth > 0) {
            // Continue starts a fresh body iteration and must not retain tasks
            // owned by the lexical scopes from the previous iteration.
            transpile_task_scope_unwind_to(mt,
                mt->loop_stack[mt->loop_depth - 1].task_scope_base, false);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].continue_label)));
        }
        MIR_reg_t r = new_reg(mt, "cont", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_RETURN_STAM:
        return transpile_return(mt, (AstReturnNode*)node);
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR:
        return transpile_raise(mt, (AstRaiseNode*)node);
    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM:
    case AST_NODE_TYPE_STAM:
        transpile_let_stam(mt, (AstLetNode*)node);
        {
            MIR_reg_t r = new_reg(mt, "let_null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
    case AST_NODE_VAR_STAM:
        transpile_let_stam(mt, (AstLetNode*)node);
        {
            MIR_reg_t r = new_reg(mt, "var_null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }
    case AST_NODE_ASSIGN_STAM:
        return transpile_assign_stam(mt, (AstAssignStamNode*)node);
    case AST_NODE_INDEX_ASSIGN_STAM: {
        // arr[i] = val → inline store for ArrayInt, or fn_array_set fallback
        // arr[i, j, k] = val → dispatch to array_num_set_nd runtime helper
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;

        // Multi-dim path: more than one chained index
        if (ca->key && ca->key->next) {
            int ndim = 0;
            for (AstNode* it = ca->key; it; it = it->next) ndim++;
            int64_t buf_bytes = (int64_t)ndim * (int64_t)sizeof(int64_t);
            MIR_reg_t idx_buf = emit_call_1(mt, "heap_data_calloc", MIR_T_P,
                MIR_T_I64, MIR_new_int_op(mt->ctx, buf_bytes));
            int slot = 0;
            for (AstNode* it = ca->key; it; it = it->next) {
                MIR_reg_t val = transpile_expr(mt, it);
                TypeId vt = get_effective_type(mt, it);
                if (!is_integer_type_id(vt)) {
                    val = emit_unbox(mt, val, LMD_TYPE_INT);
                }
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64,
                                   (MIR_disp_t)(slot * (int)sizeof(int64_t)),
                                   idx_buf, 0, 1),
                    MIR_new_reg_op(mt->ctx, val)));
                slot++;
            }
            MIR_reg_t obj_item = transpile_expr(mt, ca->object);
            MIR_reg_t arr_ptr = emit_unbox_container(mt, obj_item);
            MIR_reg_t boxed_val = transpile_box_item(mt, ca->value);
            // object_type_set_method returns an Item used by the surrounding expression.
            (void)emit_call_4(mt, "array_num_set_nd", MIR_T_I64,
                MIR_T_P,   MIR_new_reg_op(mt->ctx, arr_ptr),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ndim),
                MIR_T_P,   MIR_new_reg_op(mt->ctx, idx_buf),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
            return emit_null_item_reg(mt);
        }

        // Masked / slice assignment: arr[mask] = v (mask is a typed bool array),
        // arr[a to b] = v (range), or a dynamically-typed (ANY) index — e.g. a
        // comparison-on-view mask held with static type ANY.  Route to the runtime
        // helper, which dispatches on the index's actual type (int / range / mask).
        if (ca->key && !ca->key->next) {
            TypeId key_tid = get_effective_type(mt, ca->key);
            if (key_tid == LMD_TYPE_ARRAY_NUM || key_tid == LMD_TYPE_RANGE || key_tid == LMD_TYPE_ANY) {
                MIR_reg_t arr_item = transpile_box_item(mt, ca->object);
                MIR_reg_t key_item = transpile_box_item(mt, ca->key);
                MIR_reg_t val_item = transpile_box_item(mt, ca->value);
                (void)emit_call_3(mt, "fn_index_assign", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arr_item),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key_item),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val_item));
                return emit_null_item_reg(mt);
            }
        }

        // ==================================================================
        // Phase 4: Edit bridge — route through MarkEditor in edit handlers
        // arr[i] = val → edit_array_set(arr, index, val) or
        // element[i] = val → edit_elmt_replace_child(element, index, val)
        // ==================================================================
        if (mt->in_view_handler && mt->view_is_edit) {
            MIR_reg_t obj = transpile_box_item(mt, ca->object);
            MIR_reg_t idx = transpile_expr(mt, ca->key);
            TypeId idx_tid = get_effective_type(mt, ca->key);
            MIR_reg_t idx_int;
            if (is_integer_type_id(idx_tid)) {
                idx_int = idx;
            } else {
                idx_int = emit_unbox(mt, idx, LMD_TYPE_INT);
            }
            MIR_reg_t val_boxed = transpile_box_item(mt, ca->value);

            TypeId obj_tid = get_effective_type(mt, ca->object);
            const char* bridge_fn;
            if (obj_tid == LMD_TYPE_ELEMENT) {
                bridge_fn = "edit_elmt_replace_child";
            } else {
                bridge_fn = "edit_array_set";
            }
            // call bridge: Item edit_xxx(Item obj, int64_t index, Item value)
            MIR_reg_t result = emit_call_3(mt, bridge_fn,
                MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_boxed));
            log_debug("mir: edit bridge index assign via %s", bridge_fn);
            return result;
        }

        MIR_reg_t obj = transpile_expr(mt, ca->object);
        TypeId obj_tid = get_effective_type(mt, ca->object);
        // Object must be a pointer (Array*), unbox if boxed
        MIR_reg_t arr_ptr;
        if (obj_tid == LMD_TYPE_ANY || obj_tid == LMD_TYPE_NULL) {
            arr_ptr = emit_unbox_container(mt, obj);
        } else if (obj_tid == LMD_TYPE_ARRAY_NUM ||
                   obj_tid == LMD_TYPE_ARRAY) {
            // Container variable: stored as tagged Item, strip tag to get pointer
            arr_ptr = emit_unbox_container(mt, obj);
        } else {
            arr_ptr = obj;
        }
        MIR_reg_t idx = transpile_expr(mt, ca->key);
        TypeId idx_tid = get_effective_type(mt, ca->key);
        MIR_reg_t idx_int;
        if (is_integer_type_id(idx_tid)) {
            idx_int = idx;
        } else {
            idx_int = emit_unbox(mt, idx, LMD_TYPE_INT);
        }

        TypeId val_tid = get_effective_type(mt, ca->value);
        MIR_reg_t assign_result = emit_null_item_reg(mt);

        // Extract elem_type for ARRAY_NUM objects (used by fast paths below)
        TypeId assign_obj_elem = LMD_TYPE_ANY;
        if (obj_tid == LMD_TYPE_ARRAY_NUM) {
            AstNode* obj_unwrapped = ca->object;
            while (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_PRIMARY)
                obj_unwrapped = ((AstPrimaryNode*)obj_unwrapped)->expr;
            if (obj_unwrapped && obj_unwrapped->node_type == AST_NODE_IDENT) {
                AstIdentNode* obj_ident = (AstIdentNode*)obj_unwrapped;
                char oname[128];
                snprintf(oname, sizeof(oname), "%.*s", (int)obj_ident->name->len, obj_ident->name->chars);
                MirVarEntry* ov = find_var(mt, oname);
                if (ov) assign_obj_elem = ov->elem_type;
            }
        }

        // ==================================================================
        // FAST PATH: Compile-time known ArrayNum (int) + native INT index + native INT value
        // No runtime type check — direct inline store to items[idx].
        // ==================================================================
        if (obj_tid == LMD_TYPE_ARRAY_NUM &&
            is_integer_type_id(assign_obj_elem) &&
            is_integer_type_id(idx_tid) &&
            val_tid == LMD_TYPE_INT) {
            MIR_reg_t val_native = transpile_expr(mt, ca->value);

            MIR_label_t l_ok = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_label_t l_end = new_label(mt);

            // Bounds check
            MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

            MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, neg_check)));

            MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_reg_op(mt->ctx, arr_len)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, ge_check)));

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

            // OOB: fallback to fn_array_set
            emit_label(mt, l_oob);
            {
                MIR_reg_t boxed_val = emit_box_int(mt, val_native);
                MIR_reg_t call_result = emit_call_3(mt, "fn_array_set", MIR_T_I64,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                // assignment status is a real MIR value; branch-local C++ register selection loses OOB errors.
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, assign_result),
                    MIR_new_reg_op(mt->ctx, call_result)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // In bounds: direct store items[idx] = val
            emit_label(mt, l_ok);
            {
                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, val_native)));
            }

            emit_label(mt, l_end);
        }
        // ==================================================================
        // Runtime type-check path: INT index + INT value, unknown container type
        // ==================================================================
        else if (is_integer_type_id(idx_tid) &&
                 val_tid == LMD_TYPE_INT) {
            MIR_reg_t val_native = transpile_expr(mt, ca->value);

            MIR_reg_t tid_byte = new_reg(mt, "tidb", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, tid_byte),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 0, arr_ptr, 0, 1)));

            MIR_reg_t is_aint = new_reg(mt, "isai", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_aint),
                MIR_new_reg_op(mt->ctx, tid_byte),
                MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY_NUM)));

            MIR_label_t l_fast = new_label(mt);
            MIR_label_t l_slow = new_label(mt);
            MIR_label_t l_end = new_label(mt);
            MIR_label_t l_oob = new_label(mt);

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_fast),
                MIR_new_reg_op(mt->ctx, is_aint)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_slow)));

            // Inline ArrayNum int store (only for ELEM_INT / ELEM_INT64, not ELEM_FLOAT64)
            emit_label(mt, l_fast);
            {
                // Reject views: fall back to fn_array_set which logs the error.
                MIR_reg_t flags_byte = new_reg(mt, "flgb", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, flags_byte),
                    MIR_new_mem_op(mt->ctx, MIR_T_U8, 2, arr_ptr, 0, 1)));
                MIR_reg_t is_view_bit = new_reg(mt, "isvw", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, is_view_bit),
                    MIR_new_reg_op(mt->ctx, flags_byte), MIR_new_int_op(mt->ctx, 0x02)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_slow),
                    MIR_new_reg_op(mt->ctx, is_view_bit)));
                // elem_type lives in map_kind byte at offset 3.
                // ELEM_FLOAT64=0x10 → fall back to fn_array_set; ELEM_INT=0x00 and ELEM_INT64=0x50 take fast path
                MIR_reg_t etype = new_reg(mt, "etyp", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, etype),
                    MIR_new_mem_op(mt->ctx, MIR_T_U8, 3, arr_ptr, 0, 1)));
                MIR_reg_t is_float = new_reg(mt, "isfl", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_float),
                    MIR_new_reg_op(mt->ctx, etype), MIR_new_int_op(mt->ctx, 0x10)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_slow),
                    MIR_new_reg_op(mt->ctx, is_float)));

                MIR_reg_t arr_len = new_reg(mt, "alen", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

                MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 0)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, neg_check)));

                MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_reg_op(mt->ctx, arr_len)));
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                    MIR_new_reg_op(mt->ctx, ge_check)));

                MIR_reg_t items_ptr = new_reg(mt, "itms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "boff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "eadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, elem_addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, val_native)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // OOB
            emit_label(mt, l_oob);
            {
                MIR_reg_t boxed_val = emit_box_int(mt, val_native);
                MIR_reg_t call_result = emit_call_3(mt, "fn_array_set", MIR_T_I64,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                // assignment status is a real MIR value; branch-local C++ register selection loses OOB errors.
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, assign_result),
                    MIR_new_reg_op(mt->ctx, call_result)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Non-ArrayInt fallback
            emit_label(mt, l_slow);
            {
                MIR_reg_t boxed_val = emit_box_int(mt, val_native);
                MIR_reg_t call_result = emit_call_3(mt, "fn_array_set", MIR_T_I64,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                // assignment status is a real MIR value; branch-local C++ register selection loses helper errors.
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, assign_result),
                    MIR_new_reg_op(mt->ctx, call_result)));
            }

            emit_label(mt, l_end);
        }
        // ==================================================================
        // FAST PATH: Compile-time known ArrayNum (float) + INT index + FLOAT value
        // Direct inline store of native double to float_items[idx].
        // ==================================================================
        else if (obj_tid == LMD_TYPE_ARRAY_NUM &&
                 assign_obj_elem == LMD_TYPE_FLOAT &&
                 is_integer_type_id(idx_tid) &&
                 val_tid == LMD_TYPE_FLOAT) {
            MIR_reg_t val_native = transpile_expr(mt, ca->value);

            MIR_label_t l_ok = new_label(mt);
            MIR_label_t l_oob = new_label(mt);
            MIR_label_t l_end = new_label(mt);

            // Reject views: fall back to fn_array_set
            MIR_reg_t fflags = new_reg(mt, "fflg", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fflags),
                MIR_new_mem_op(mt->ctx, MIR_T_U8, 2, arr_ptr, 0, 1)));
            MIR_reg_t fview = new_reg(mt, "fvw", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, fview),
                MIR_new_reg_op(mt->ctx, fflags), MIR_new_int_op(mt->ctx, 0x02)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, fview)));

            // Bounds check
            MIR_reg_t arr_len = new_reg(mt, "aflen", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, arr_len),
                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, arr_ptr, 0, 1)));

            MIR_reg_t neg_check = new_reg(mt, "negc", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, neg_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 0)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, neg_check)));

            MIR_reg_t ge_check = new_reg(mt, "gec", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_GES, MIR_new_reg_op(mt->ctx, ge_check),
                MIR_new_reg_op(mt->ctx, idx_int), MIR_new_reg_op(mt->ctx, arr_len)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_oob),
                MIR_new_reg_op(mt->ctx, ge_check)));

            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_ok)));

            // OOB: fallback to fn_array_set with boxed float
            emit_label(mt, l_oob);
            {
                MIR_reg_t boxed_val = emit_box_float(mt, val_native);
                MIR_reg_t call_result = emit_call_3(mt, "fn_array_set", MIR_T_I64,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                // assignment status is a real MIR value; branch-local C++ register selection loses OOB errors.
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, assign_result),
                    MIR_new_reg_op(mt->ctx, call_result)));
            }
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // In bounds: direct store items[idx] = double_val
            emit_label(mt, l_ok);
            {
                MIR_reg_t items_ptr = new_reg(mt, "fitms", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, items_ptr),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, arr_ptr, 0, 1)));

                MIR_reg_t byte_off = new_reg(mt, "fboff", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, byte_off),
                    MIR_new_reg_op(mt->ctx, idx_int), MIR_new_int_op(mt->ctx, 3)));

                MIR_reg_t elem_addr = new_reg(mt, "feadr", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, elem_addr),
                    MIR_new_reg_op(mt->ctx, items_ptr), MIR_new_reg_op(mt->ctx, byte_off)));

                // Store native double directly
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_D, 0, elem_addr, 0, 1),
                    MIR_new_reg_op(mt->ctx, val_native)));
            }

            emit_label(mt, l_end);
        }
        // ==================================================================
        // DEFAULT: box value, call fn_array_set
        // ==================================================================
        else {
            MIR_reg_t val = transpile_box_item(mt, ca->value);
            MIR_reg_t call_result = emit_call_3(mt, "fn_array_set", MIR_T_I64,
                MIR_T_P, MIR_new_reg_op(mt->ctx, arr_ptr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_int),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, assign_result),
                MIR_new_reg_op(mt->ctx, call_result)));
        }

        // Return helper status so OOB write failures participate in T^ propagation.
        return assign_result;
    }
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        // obj.field = val → fn_map_set(boxed_obj, boxed_key, boxed_val)
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;

        // ==================================================================
        // Phase 4: Edit bridge — route through MarkEditor in edit handlers
        // When in an edit handler (is_edit=true), member assignments on the
        // model are routed through edit_map_update / edit_elmt_update_attr
        // instead of direct in-place mutation.
        // ==================================================================
        if (mt->in_view_handler && mt->view_is_edit &&
            ca->key->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident = (AstIdentNode*)ca->key;
            MIR_reg_t obj = transpile_box_item(mt, ca->object);
            MIR_reg_t val_boxed = transpile_box_item(mt, ca->value);
            // load key string pointer
            MIR_reg_t key_ptr = emit_load_string_literal(mt, ident->name->chars);

            // determine whether object is element or map and call appropriate bridge
            TypeId obj_tid = get_effective_type(mt, ca->object);
            const char* bridge_fn;
            if (obj_tid == LMD_TYPE_ELEMENT) {
                bridge_fn = "edit_elmt_update_attr";
            } else {
                bridge_fn = "edit_map_update";
            }
            // call bridge: Item edit_xxx(Item obj, const char* key, Item value)
            MIR_reg_t result = emit_call_3(mt, bridge_fn,
                MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_ptr),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val_boxed));
            log_debug("mir: edit bridge member assign via %s for '%.*s'",
                      bridge_fn, (int)ident->name->len, ident->name->chars);
            return result;
        }

        // ==================================================================
        // Phase 3: Direct field write optimization for typed maps/objects
        // ==================================================================
        if (false && ca->object->type && ca->key->node_type == AST_NODE_IDENT) {
            TypeId obj_type_id = ca->object->type->type_id;
            if (obj_type_id == LMD_TYPE_MAP || obj_type_id == LMD_TYPE_OBJECT) {
                TypeMap* map_type = (TypeMap*)ca->object->type;
                if (has_fixed_shape(map_type)) {
                    AstIdentNode* ident = (AstIdentNode*)ca->key;
                    ShapeEntry* se = find_shape_field_by_name(map_type,
                        ident->name->chars, ident->name->len);
                    if (se && se->type && is_direct_access_type(resolve_field_type_id(se, true))) {
                        log_debug("mir: direct field write: %.*s (type=%d offset=%lld)",
                            (int)ident->name->len, ident->name->chars,
                            resolve_field_type_id(se, true), (long long)se->byte_offset);
                        bool skip_null_guard = false; // typed variables can still hold null
                        emit_mir_direct_field_write(mt, ca->object, se, ca->value, skip_null_guard);
                        // Return null (void statement)
                        MIR_reg_t r = new_reg(mt, "void", MIR_T_I64);
                        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                        return r;
                    }
                }
            }
        }

        // Fallback: runtime fn_map_set
        MIR_reg_t obj = transpile_box_item(mt, ca->object);
        // Key: if it's an ident, emit as string constant; otherwise box it
        MIR_reg_t key;
        if (ca->key->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident = (AstIdentNode*)ca->key;
            MIR_reg_t name_ptr = emit_load_string_literal(mt, ident->name->chars);
            MIR_reg_t str_obj = emit_call_1(mt, "heap_create_name", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr));
            key = emit_box_string(mt, str_obj);
        } else {
            key = transpile_box_item(mt, ca->key);
        }
        MIR_reg_t val = transpile_box_item(mt, ca->value);
        emit_call_void_3(mt, "fn_map_set",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        // Return null (void statement)
        MIR_reg_t r = new_reg(mt, "void", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_PIPE_FILE_STAM: {
        // data |> file → pn_output2(boxed_data, boxed_file)
        // data |>> file → pn_output_append(boxed_data, boxed_file)
        AstBinaryNode* pipe = (AstBinaryNode*)node;
        MIR_reg_t data = transpile_box_item(mt, pipe->left);
        MIR_reg_t target = transpile_box_item(mt, pipe->right);
        const char* fn_name = (pipe->op == OPERATOR_PIPE_APPEND) ? "pn_output_append_mir" : "pn_output2_mir";
        return emit_call_2(mt, fn_name, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, data),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, target));
    }
    case AST_NODE_ASSIGN: {
        AstNamedNode* asn = (AstNamedNode*)node;
        if (asn->as) {
            MIR_reg_t val = transpile_expr(mt, asn->as);
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)asn->name->len, asn->name->chars);
            TypeId tid = asn->as->type ? asn->as->type->type_id : LMD_TYPE_ANY;
            set_var(mt, name_buf, val, type_to_mir(tid), tid);
            return val;
        }
        MIR_reg_t r = new_reg(mt, "asn", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_ARRAY:
        return transpile_array(mt, (AstArrayNode*)node);
    case AST_NODE_LIST:
        return transpile_list(mt, (AstListNode*)node);
    case AST_NODE_CONTENT:
        return transpile_content(mt, (AstListNode*)node);
    case AST_NODE_MAP:
        return transpile_map(mt, (AstMapNode*)node);
    case AST_NODE_OBJECT_LITERAL: {
        // object literal: {TypeName key: value, ...}
        AstObjectLiteralNode* obj_lit = (AstObjectLiteralNode*)node;
        TypeObject* obj_type = (TypeObject*)obj_lit->type;
        int type_index = obj_type->type_index;

        // create object with inline data via object_with_tl (saves/restores context->type_list)
        MIR_reg_t o = emit_call_2(mt, "object_with_tl", MIR_T_P,
            MIR_T_I64, MIR_new_int_op(mt->ctx, type_index),
            MIR_T_P, MIR_new_reg_op(mt->ctx, emit_load_module_type_list(mt)));

        // count and evaluate field values
        AstNode* item = obj_lit->item;
        int val_count = 0;
        while (item) { val_count++; item = item->next; }

        if (val_count == 0) {
            // no fields - return empty object
            return o;
        }

        MIR_op_t* val_ops = LAMBDA_ALLOCA(val_count, MIR_op_t);
        item = obj_lit->item;
        int vi = 0;
        while (item) {
            if (item->node_type == AST_NODE_KEY_EXPR) {
                AstNamedNode* key_expr = (AstNamedNode*)item;
                if (key_expr->as) {
                    MIR_reg_t val = transpile_box_item(mt, key_expr->as);
                    val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
                } else {
                    MIR_reg_t nul = new_reg(mt, "objnull", MIR_T_I64);
                    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, nul),
                        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                    val_ops[vi++] = MIR_new_reg_op(mt->ctx, nul);
                }
            } else {
                // bare expression (wrapping source object) — not yet handled
                MIR_reg_t val = transpile_box_item(mt, item);
                val_ops[vi++] = MIR_new_reg_op(mt->ctx, val);
            }
            item = item->next;
        }

        // call object_fill(o, val1, val2, ...) — variadic
        MIR_reg_t filled = emit_vararg_call(mt, "object_fill", MIR_T_P, 1,
            MIR_T_P, MIR_new_reg_op(mt->ctx, o), vi, val_ops);
        return filled;
    }
    case AST_NODE_OBJECT_TYPE: {
        // object type definition — type is already registered, emit const_type for type value
        AstObjectTypeNode* obj_type_node = (AstObjectTypeNode*)node;
        if (obj_type_node->type && obj_type_node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)obj_type_node->type;
            if (tt->type) {
                TypeObject* ot = (TypeObject*)tt->type;
                MIR_reg_t type_val = transpile_const_type(mt, ot->type_index);

                // Register method function pointers on the TypeObject
                AstNode* method_node = obj_type_node->methods;
                while (method_node) {
                    if (method_node->node_type == AST_NODE_FUNC ||
                        method_node->node_type == AST_NODE_PROC ||
                        method_node->node_type == AST_NODE_FUNC_EXPR) {
                        AstFuncNode* fn_method = (AstFuncNode*)method_node;
                        // Get the mangled function name
                        StrBuf* fn_name_buf = strbuf_new_cap(64);
                        write_fn_name_ex(fn_name_buf, fn_method, NULL, "_b");
                        // Find the compiled MIR function
                        MIR_item_t method_func = find_local_func(mt, fn_name_buf->str);
                        log_debug("mir: AST_NODE_OBJECT_TYPE: looking for method '%s' → %s",
                            fn_name_buf->str, method_func ? "FOUND" : "NOT FOUND");
                        if (method_func) {
                            // Get function address
                            MIR_reg_t fn_addr = new_reg(mt, "mthaddr", MIR_T_I64);
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, fn_addr),
                                MIR_new_ref_op(mt->ctx, method_func)));
                            // Get method name as string
                            MIR_reg_t mth_name = emit_load_string_literal(mt,
                                fn_method->name->chars);
                            // Count user-visible arity (without hidden _self)
                            int arity = 0;
                            AstNamedNode* p = fn_method->param;
                            while (p) { arity++; p = (AstNamedNode*)p->next; }
                            // Emit: object_type_set_method(type_index, name, fn_ptr, arity, is_proc)
                            MIR_var_t args[5] = {
                                {MIR_T_I64, "ti", 0}, {MIR_T_P, "n", 0},
                                {MIR_T_P, "f", 0}, {MIR_T_I64, "a", 0},
                                {MIR_T_I64, "p", 0}
                            };
                            MirImportEntry* ie = ensure_import(mt, "object_type_set_method",
                                MIR_T_I64, 5, args, 0);
                            emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                                MIR_new_ref_op(mt->ctx, ie->proto),
                                MIR_new_ref_op(mt->ctx, ie->import),
                                MIR_new_int_op(mt->ctx, (int64_t)ot->type_index),
                                MIR_new_reg_op(mt->ctx, mth_name),
                                MIR_new_reg_op(mt->ctx, fn_addr),
                                MIR_new_int_op(mt->ctx, (int64_t)arity),
                                MIR_new_int_op(mt->ctx, method_node->node_type == AST_NODE_PROC ? 1 : 0)));
                            log_debug("mir: registered method ABI wrapper '%s' on type '%.*s'",
                                fn_name_buf->str, (int)ot->type_name.length, ot->type_name.str);
                        }
                        strbuf_free(fn_name_buf);
                    }
                    method_node = method_node->next;
                }

                return type_val;
            }
        }
        // fallback: return null
        MIR_reg_t r = new_reg(mt, "objtype_null", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_ELEMENT:
        return transpile_element(mt, (AstElementNode*)node);
    case AST_NODE_MEMBER_EXPR:
        return transpile_member(mt, (AstFieldNode*)node);
    case AST_NODE_INDEX_EXPR:
        return transpile_index(mt, (AstFieldNode*)node);
    case AST_NODE_START:
        return transpile_start(mt, (AstStartNode*)node);
    case AST_NODE_CALL_EXPR: {
        AstCallNode* cn = (AstCallNode*)node;
        MIR_reg_t call_result = transpile_call(mt, cn);
        if (cn->propagate) {
            // '^' error propagation: check if result is error, early-return if so.
            // can_raise calls return boxed Item (LMD_TYPE_ANY), so result is raw i64.
            // extract type tag from high byte: result >> 56
            MIR_reg_t type_tag = new_reg(mt, "ptag", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_RSH, MIR_new_reg_op(mt->ctx, type_tag),
                MIR_new_reg_op(mt->ctx, call_result), MIR_new_int_op(mt->ctx, 56)));
            MIR_reg_t is_err = new_reg(mt, "perr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_err),
                MIR_new_reg_op(mt->ctx, type_tag), MIR_new_int_op(mt->ctx, LMD_TYPE_ERROR)));
            MIR_label_t l_ok = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_ok),
                MIR_new_reg_op(mt->ctx, is_err)));
            // error path: return error from enclosing function
            transpile_task_scope_unwind(mt, true);
            async_complete_frame(mt);
            emit_function_error_return(mt, call_result);
            // non-error path: continue with result
            emit_label(mt, l_ok);
        }
        return call_result;
    }
    case AST_NODE_QUERY_EXPR: {
        // query expression: expr?T or expr.?T → fn_query(data, type_val, direct)
        AstQueryNode* query = (AstQueryNode*)node;
        MIR_reg_t obj = transpile_expr(mt, query->object);
        TypeId obj_tid = get_effective_type(mt, query->object);
        MIR_reg_t boxed_obj = emit_box(mt, obj, obj_tid);
        MIR_reg_t type_reg = transpile_expr(mt, query->query);
        TypeId type_tid = get_effective_type(mt, query->query);
        MIR_reg_t boxed_type = emit_box(mt, type_reg, type_tid);
        return emit_call_3(mt, "fn_query", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_type),
            MIR_T_I64, MIR_new_int_op(mt->ctx, query->direct ? 1 : 0));
    }
    case AST_NODE_PIPE:
        return transpile_pipe(mt, (AstPipeNode*)node);
    case AST_NODE_CURRENT_ITEM: {
        if (mt->in_pipe) return mt->pipe_item_reg;
        // in view/edit template context, ~ resolves to the model parameter
        if (mt->in_view_context) return mt->view_model_reg;
        MIR_reg_t r = new_reg(mt, "pipe_item", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_CURRENT_INDEX: {
        if (mt->in_pipe) return mt->pipe_index_reg;
        MIR_reg_t r = new_reg(mt, "pipe_idx", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, 0)));
        return r;
    }
    case AST_NODE_LAST_INDEX: {
        MIR_reg_t r = new_reg(mt, "last_idx", MIR_T_I64);
        if (!mt->last_index_object) {
            log_error("mir: `last` used outside subscript");
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
        MIR_reg_t boxed_obj = transpile_box_item(mt, mt->last_index_object);
        MIR_reg_t len = emit_call_1(mt, "fn_len", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_obj));
        // C15 defines `last` as len(container) - 1; empty containers naturally produce -1.
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_SUB, MIR_new_reg_op(mt->ctx, r),
            MIR_new_reg_op(mt->ctx, len), MIR_new_int_op(mt->ctx, 1)));
        return r;
    }
    case AST_NODE_TYPE:
        return transpile_base_type(mt, (AstTypeNode*)node);
    case AST_NODE_KEY_EXPR: {
        // Key expression - transpile the value part
        AstNamedNode* key = (AstNamedNode*)node;
        if (key->as) return transpile_expr(mt, key->as);
        MIR_reg_t r = new_reg(mt, "key", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_BINARY_TYPE: {
        // Union type in match pattern: const_type_with_tl(type_index, type_list_ptr)
        AstBinaryNode* bin = (AstBinaryNode*)node;
        if (bin->type && bin->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)bin->type;
            if (tt->type) {
                TypeBinary* bt = (TypeBinary*)tt->type;
                return emit_call_2(mt, "const_type_with_tl", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, bt->type_index),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, emit_load_module_type_list(mt)));
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_UNARY_TYPE: {
        // Unary type (e.g. int?, int+, int[2]): use const_type(type_index)
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeUnary* ut = (TypeUnary*)tt->type;
                return transpile_const_type(mt, ut->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_CONTENT_TYPE:
    case AST_NODE_LIST_TYPE: {
        // List type: use const_type(type_index) for structured types; fall back to base_type otherwise
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                extern Type TYPE_LIST;
                // bare 'list' keyword: TYPE_LIST is a plain Type (no type_index), use base_type
                if (tt->type == &TYPE_LIST) {
                    return transpile_base_type(mt, (AstTypeNode*)node);
                }
                TypeList* lt = (TypeList*)tt->type;
                return transpile_const_type(mt, lt->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_ARRAY_TYPE: {
        // Array type (e.g. [int], int[2]): use const_type(type_index)
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeArray* at = (TypeArray*)tt->type;
                return transpile_const_type(mt, at->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_MAP_TYPE: {
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeMap* mp = (TypeMap*)tt->type;
                return transpile_const_type(mt, mp->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_ELMT_TYPE: {
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeElmt* et = (TypeElmt*)tt->type;
                return transpile_const_type(mt, et->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_FUNC_TYPE: {
        if (node->type && node->type->type_id == LMD_TYPE_TYPE) {
            TypeType* tt = (TypeType*)node->type;
            if (tt->type) {
                TypeFunc* ft = (TypeFunc*)tt->type;
                return transpile_const_type(mt, ft->type_index);
            }
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_CONSTRAINED_TYPE: {
        // Constrained type (e.g. int where (~ > 0)): use const_type(type_index)
        AstConstrainedTypeNode* ct = (AstConstrainedTypeNode*)node;
        TypeConstrained* constrained = (TypeConstrained*)ct->type;
        if (constrained) {
            return transpile_const_type(mt, constrained->type_index);
        }
        return transpile_base_type(mt, (AstTypeNode*)node);
    }
    case AST_NODE_NAMED_ARG: {
        // Named argument: transpile the value expression
        AstNamedNode* named = (AstNamedNode*)node;
        if (named->as) return transpile_expr(mt, named->as);
        MIR_reg_t r = new_reg(mt, "narg", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_PATH_EXPR: {
        // Path expression: build path using path_new + chained path_extend calls
        AstPathNode* path_node = (AstPathNode*)node;

        // Get runtime pool pointer
        MIR_reg_t pool = emit_call_0(mt, "get_runtime_pool", MIR_T_P);

        // Create base path: path_new(pool, scheme)
        MIR_reg_t path = emit_call_2(mt, "path_new", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int)path_node->scheme));

        // Extend with each segment
        for (int i = 0; i < path_node->segment_count; i++) {
            AstPathSegment* seg = &path_node->segments[i];
            if (seg->type == LPATH_SEG_WILDCARD) {
                path = emit_call_2(mt, "path_wildcard", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, path));
            } else if (seg->type == LPATH_SEG_WILDCARD_REC) {
                path = emit_call_2(mt, "path_wildcard_recursive", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, path));
            } else {
                // Normal segment: pass C string name
                const char* seg_name = seg->name ? seg->name->chars : "";
                MIR_reg_t name_ptr = emit_load_string_literal(mt, seg_name);
                path = emit_call_3(mt, "path_extend", MIR_T_P,
                    MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, path),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr));
            }
        }
        // Path* is a container pointer, return as-is
        return path;
    }
    case AST_NODE_PATH_INDEX_EXPR: {
        // Path index: path[expr] → path_extend(pool, base, fn_to_cstr(expr))
        AstPathIndexNode* pix = (AstPathIndexNode*)node;
        MIR_reg_t pool = emit_call_0(mt, "get_runtime_pool", MIR_T_P);
        MIR_reg_t base = transpile_expr(mt, pix->base_path);
        MIR_reg_t seg_val = transpile_expr(mt, pix->segment_expr);
        TypeId seg_tid = get_effective_type(mt, pix->segment_expr);
        MIR_reg_t boxed_seg = emit_box(mt, seg_val, seg_tid);
        MIR_reg_t seg_cstr = emit_call_1(mt, "fn_to_cstr", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_seg));
        MIR_reg_t result = emit_call_3(mt, "path_extend", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(mt->ctx, pool),
            MIR_T_P, MIR_new_reg_op(mt->ctx, base),
            MIR_T_P, MIR_new_reg_op(mt->ctx, seg_cstr));
        return result;
    }
    case AST_NODE_PARENT_EXPR: {
        // Parent access: expr.. → fn_member(expr, "parent") repeated for depth levels
        AstParentNode* parent = (AstParentNode*)node;
        if (parent->object) {
            MIR_reg_t obj = transpile_expr(mt, parent->object);
            TypeId obj_tid = get_effective_type(mt, parent->object);
            MIR_reg_t current = emit_box(mt, obj, obj_tid);

            // Create a "parent" string at runtime via heap_create_name
            MIR_reg_t name_ptr = emit_load_string_literal(mt, "parent");
            MIR_reg_t parent_str = emit_call_1(mt, "heap_create_name", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr));
            MIR_reg_t parent_key = emit_box_string(mt, parent_str);

            for (int i = 0; i < parent->depth; i++) {
                current = emit_call_2(mt, "fn_member", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, current),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, parent_key));
            }
            return current;
        }
        MIR_reg_t r = new_reg(mt, "parent", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        // Look up the MIR function created in pre-pass and create a runtime Function object
        AstFuncNode* fn_node = (AstFuncNode*)node;
        StrBuf* name_buf = strbuf_new_cap(64);
        write_fn_name(name_buf, fn_node, NULL);

        MIR_item_t func_item = find_local_func(mt, name_buf->str);

        StrBuf* wrapper_buf = strbuf_new_cap(64);
        write_fn_name_ex(wrapper_buf, fn_node, NULL, "_b");
        MIR_item_t wrapper_item = find_local_func(mt, wrapper_buf->str);
        if (wrapper_item) {
            log_debug("mir: fn expr '%s' → using ABI wrapper '%s'",
                name_buf->str, wrapper_buf->str);
            func_item = wrapper_item;
        }
        strbuf_free(wrapper_buf);

        if (func_item) {
            // Count arity from AST param list
            int arity = 0;
            AstNamedNode* p = fn_node->param;
            while (p) { arity++; p = (AstNamedNode*)p->next; }

            // Get function's native code address via MIR ref
            MIR_reg_t fn_addr = new_reg(mt, "fnaddr", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_addr),
                MIR_new_ref_op(mt->ctx, func_item)));

            if (fn_node->captures) {
                // Closure: allocate and populate env, then create closure object
                int cap_count = 0;
                FnCapture* cap = fn_node->captures;
                while (cap) { cap_count++; cap = cap->next; }

                // Each capture owns one Item slot and one raw scalar tail slot.
                MIR_reg_t env_reg = emit_call_2(mt, "heap_calloc", MIR_T_P,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(cap_count * 16)),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

                // Populate env with captured variable values
                int cap_idx = 0;
                cap = fn_node->captures;
                while (cap) {
                    char cap_name[128];
                    snprintf(cap_name, sizeof(cap_name), "%s", cap->name);

                    // Look up the captured variable value
                    MIR_reg_t cap_val = 0;
                    MirVarEntry* var = find_var(mt, cap_name);
                    if (var) {
                        // Box the variable value if needed
                        cap_val = emit_box(mt, var->reg, var->type_id);
                    } else {
                        GlobalVarEntry* gvar = find_global_var(mt, cap_name);
                        if (gvar) {
                            cap_val = load_global_var(mt, gvar);
                        } else {
                            // Variable not found - use null
                            log_error("mir: closure capture '%s' not found in scope", cap_name);
                            cap_val = new_reg(mt, "capnull", MIR_T_I64);
                            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
                            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, cap_val),
                                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
                        }
                    }

                    emit_call_void_4(mt, "owned_item_slot_store",
                        MIR_T_P, MIR_new_reg_op(mt->ctx, env_reg),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, cap_count),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, cap_idx),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, cap_val));

                    log_debug("mir: closure '%s' - captured '%s' at env offset %d",
                        name_buf->str, cap_name, cap_idx * 8);

                    cap_idx++;
                    cap = cap->next;
                }

                // Get closure name for stack traces
                const char* closure_name = fn_node->name ? fn_node->name->chars : nullptr;

                // Create closure: to_closure_named(fn_ptr, arity, env, name)
                MIR_reg_t fn_obj;
                if (closure_name) {
                    MIR_reg_t name_reg = emit_load_string_literal(mt, closure_name);
                    MIR_var_t cv[4] = {{MIR_T_P,"f",0},{MIR_T_I64,"a",0},{MIR_T_P,"e",0},{MIR_T_P,"n",0}};
                    MirImportEntry* ie = ensure_import(mt, "to_closure_named", MIR_T_P, 4, cv, 1);
                    fn_obj = new_reg(mt, "closure", MIR_T_I64);
                    emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
                        MIR_new_ref_op(mt->ctx, ie->proto),
                        MIR_new_ref_op(mt->ctx, ie->import),
                        MIR_new_reg_op(mt->ctx, fn_obj),
                        MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_new_int_op(mt->ctx, arity),
                        MIR_new_reg_op(mt->ctx, env_reg),
                        MIR_new_reg_op(mt->ctx, name_reg)));
                } else {
                    fn_obj = emit_call_3(mt, "to_closure", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arity),
                        MIR_T_P, MIR_new_reg_op(mt->ctx, env_reg));
                }

                strbuf_free(name_buf);

                // set closure_field_count (offset 2 in Function struct)
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_U8, 2, fn_obj, 0, 1),
                    MIR_new_int_op(mt->ctx, cap_count)));

                return fn_obj;
            } else {
                MIR_reg_t fn_obj;
                if (fn_node->name && fn_node->name->len > 0) {
                    // first-class declarations must carry their source name so name(fn) is stable.
                    MIR_reg_t name_reg = emit_load_string_literal(mt, fn_node->name->chars);
                    fn_obj = emit_call_3(mt, "to_fn_named", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arity),
                        MIR_T_P, MIR_new_reg_op(mt->ctx, name_reg));
                } else {
                    // plain anonymous function: call to_fn_n(fn_ptr, arity) -> Function*
                    fn_obj = emit_call_2(mt, "to_fn_n", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arity));
                }

                strbuf_free(name_buf);
                return fn_obj;  // Function* is a container - type_id in first byte
            }
        }

        strbuf_free(name_buf);
        // Function not found in pre-pass - return null
        log_error("mir: function not found in pre-pass: %s", name_buf->str);
        MIR_reg_t r = new_reg(mt, "def", MIR_T_I64);
        uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        return r;
    }
    case AST_NODE_SYS_FUNC: {
        AstSysFuncNode* sys = (AstSysFuncNode*)node;
        SysFuncInfo* info = sys->fn_info;
        if (!info || !info->func_ptr) {
            MIR_reg_t r = new_reg(mt, "sysfn_null", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
            return r;
        }

        // Bare sys funcs have no compiled MIR item, so use registry identity.
        MIR_reg_t fn_addr = new_reg(mt, "sysfnaddr", MIR_T_P);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, fn_addr),
            MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)info->func_ptr)));
        MIR_reg_t name_reg = emit_load_string_literal(mt, info->name);
        return emit_call_3(mt, "to_sys_fn_named", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(mt->ctx, fn_addr),
            MIR_T_I64, MIR_new_int_op(mt->ctx, info->arg_count),
            MIR_T_P, MIR_new_reg_op(mt->ctx, name_reg));
    }
    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN:
    case AST_NODE_VIEW:
    case AST_NODE_IMPORT: {
        // Definitions handled in root pass - return null as placeholder
        MIR_reg_t r = new_reg(mt, "defp", MIR_T_I64);
        uint64_t NULL_VAL2 = (uint64_t)LMD_TYPE_NULL << 56;
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
            MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL2)));
        return r;
    }
    default:
        log_error("mir: unhandled node type %d", node->node_type);
        {
            MIR_reg_t r = new_reg(mt, "unk", MIR_T_I64);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_int_op(mt->ctx, 0)));
            return r;
        }
    }
}

// ============================================================================
// User-defined function transpilation
// ============================================================================

// ============================================================================
// Phase 2: Parameter Type Inference from Usage Context
// ============================================================================
// For untyped function parameters (type_id == ANY), scan the function body to
// infer the most likely type from how the parameter is used. This allows
// speculative unboxing at function entry, enabling native arithmetic ops
// instead of slow boxed runtime calls (e.g., fn_add, fn_sub).
//
// Evidence rules:
//   +INT: param used in binary op with int literal or int-typed expr
//   +FLOAT: param used in binary op with float literal or float-typed expr
//   +NUMERIC_USE: param used in arithmetic/comparison (no literal type info on other side)
//   +STOP: param used in function call, member access, index, or non-numeric context
// If all evidence is INT → infer INT.  FLOAT present (no STOP) → infer FLOAT.
// NUMERIC_USE only (no INT/FLOAT/STOP) → infer INT (default numeric type).
// Any STOP evidence → keep as ANY.

#define INFER_INT         1
#define INFER_FLOAT       2
#define INFER_STOP        4
#define INFER_NUMERIC_USE 8
#define INFER_FLOAT_CONTEXT 16  // function body contains float literals (guards NUMERIC_USE→INT)
#define INFER_ARITH_USE   32  // param used in arithmetic (+,-,*,/,%,**), not just comparisons

// Get the AST-declared type_id for a node, handling common cases
static TypeId node_type_id(AstNode* node) {
    if (!node || !node->type) return LMD_TYPE_ANY;
    return node->type->type_id;
}

// Check if a node is a reference to ANY tracked name (param or alias)
static bool is_tracked_ref(AstNode* node, FnParamEvidence* ctx) {
    if (!node) return false;
    if (node->node_type == AST_NODE_IDENT) {
        AstIdentNode* ident = (AstIdentNode*)node;
        if (!ident->name) return false;
        for (int i = 0; i < ctx->name_count; i++) {
            if ((int)ident->name->len == ctx->name_lens[i] &&
                memcmp(ident->name->chars, ctx->names[i], ctx->name_lens[i]) == 0)
                return true;
        }
    }
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        return is_tracked_ref(pri->expr, ctx);
    }
    return false;
}

// Add alias name to tracking context
static void add_alias(FnParamEvidence* ctx, const char* name, int name_len) {
    if (ctx->name_count >= FN_PARAM_MAX_ALIASES || name_len >= 64) return;
    // Check if already tracked
    for (int i = 0; i < ctx->name_count; i++) {
        if (ctx->name_lens[i] == name_len &&
            memcmp(ctx->names[i], name, name_len) == 0) return;
    }
    memcpy(ctx->names[ctx->name_count], name, name_len);
    ctx->names[ctx->name_count][name_len] = '\0';
    ctx->name_lens[ctx->name_count] = name_len;
    ctx->name_count++;
}

// Classify the "other side" type for a binary op involving our param
static int classify_other_type(TypeId tid) {
    if (tid == LMD_TYPE_INT) return INFER_INT;
    if (tid == LMD_TYPE_FLOAT) return INFER_FLOAT;
    if (tid == LMD_TYPE_ANY) return 0;  // unknown other — no evidence
    // BOOL comparison is NOT evidence of INT — e.g. `param == false` means
    // the param is boolean, not integer. Treating BOOL as INFER_INT would
    // cause boolean params to be stored as INT when boxed, breaking type-
    // strict equality comparisons later (INT 1 != BOOL true).
    if (tid == LMD_TYPE_BOOL) return 0;
    return INFER_STOP;  // string, map, list, etc.
}

// ============================================================================
// Batched parameter type inference: infer types for ALL untyped params
// in a single body walk instead of walking once per parameter.
// ============================================================================

// Multi-context alias finding: walks body once checking all FnParamEvidence contexts
static void find_aliases_multi(AstNode* node, FnParamEvidence* ctxs, int ctx_count) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            find_aliases_multi(list->declare, ctxs, ctx_count);
            find_aliases_multi(list->item, ctxs, ctx_count);
            break;
        }
        case AST_NODE_VAR_STAM:
        case AST_NODE_LET_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            find_aliases_multi(let_node->declare, ctxs, ctx_count);
            break;
        }
        case AST_NODE_ASSIGN: {
            AstNamedNode* named = (AstNamedNode*)node;
            if (named->as && named->name) {
                for (int c = 0; c < ctx_count; c++) {
                    if (is_tracked_ref(named->as, &ctxs[c])) {
                        add_alias(&ctxs[c], named->name->chars, (int)named->name->len);
                    }
                }
            }
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* ifn = (AstIfNode*)node;
            find_aliases_multi(ifn->then, ctxs, ctx_count);
            find_aliases_multi(ifn->otherwise, ctxs, ctx_count);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            find_aliases_multi(wh->body, ctxs, ctx_count);
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
}

// Multi-context evidence gathering: walks body once checking all FnParamEvidence contexts
static void gather_evidence_multi(AstNode* node, FnParamEvidence* ctxs, int ctx_count) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_BINARY: {
            AstBinaryNode* bi = (AstBinaryNode*)node;
            int op = bi->op;
            bool is_arith = (op == OPERATOR_ADD || op == OPERATOR_SUB || op == OPERATOR_MUL ||
                             op == OPERATOR_DIV || op == OPERATOR_IDIV || op == OPERATOR_MOD ||
                             op == OPERATOR_POW);
            bool is_cmp = (op >= OPERATOR_EQ && op <= OPERATOR_GE) || is_elementwise_comparison_op(op);

            if (is_arith || is_cmp) {
                for (int c = 0; c < ctx_count; c++) {
                    bool left_is_tracked = is_tracked_ref(bi->left, &ctxs[c]);
                    bool right_is_tracked = is_tracked_ref(bi->right, &ctxs[c]);
                    if (is_arith && left_is_tracked && bi->right && bi->right->type && bi->right->type->is_literal) {
                        TypeId rtid = node_type_id(bi->right);
                        ctxs[c].evidence |= classify_other_type(rtid);
                    }
                    if (is_arith && right_is_tracked && bi->left && bi->left->type && bi->left->type->is_literal) {
                        TypeId ltid = node_type_id(bi->left);
                        ctxs[c].evidence |= classify_other_type(ltid);
                    }
                    if (left_is_tracked || right_is_tracked) {
                        ctxs[c].evidence |= INFER_NUMERIC_USE;
                        if (is_arith) ctxs[c].evidence |= INFER_ARITH_USE;
                        // true division (`/`) always yields float in Lambda (e.g. 4/2 → 2.0),
                        // so a param used as a `/` operand is float-natured. Mark float context
                        // to suppress the speculative pn INT inference, which would otherwise
                        // truncate float args at the call site (e.g. a timestep dt = 0.01 → 0).
                        if (op == OPERATOR_DIV) ctxs[c].evidence |= INFER_FLOAT_CONTEXT;
                    }
                    if ((left_is_tracked || right_is_tracked) &&
                        !(ctxs[c].evidence & (INFER_INT | INFER_FLOAT))) {
                        TypeId bi_tid = node_type_id((AstNode*)bi);
                        if (bi_tid == LMD_TYPE_INT) ctxs[c].evidence |= INFER_INT;
                        else if (bi_tid == LMD_TYPE_FLOAT) ctxs[c].evidence |= INFER_FLOAT;
                    }
                }
            }

            gather_evidence_multi(bi->left, ctxs, ctx_count);
            gather_evidence_multi(bi->right, ctxs, ctx_count);
            break;
        }
        case AST_NODE_UNARY: {
            AstUnaryNode* un = (AstUnaryNode*)node;
            if (un->op == OPERATOR_NEG) {
                for (int c = 0; c < ctx_count; c++) {
                    if (is_tracked_ref(un->operand, &ctxs[c])) {
                        ctxs[c].evidence |= INFER_NUMERIC_USE;
                    }
                }
            }
            gather_evidence_multi(un->operand, ctxs, ctx_count);
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* ifn = (AstIfNode*)node;
            gather_evidence_multi(ifn->cond, ctxs, ctx_count);
            gather_evidence_multi(ifn->then, ctxs, ctx_count);
            gather_evidence_multi(ifn->otherwise, ctxs, ctx_count);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            gather_evidence_multi(wh->cond, ctxs, ctx_count);
            gather_evidence_multi(wh->body, ctxs, ctx_count);
            break;
        }
        case AST_NODE_RETURN_STAM: {
            AstReturnNode* ret = (AstReturnNode*)node;
            gather_evidence_multi(ret->value, ctxs, ctx_count);
            break;
        }
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* asn = (AstAssignStamNode*)node;
            gather_evidence_multi(asn->value, ctxs, ctx_count);
            break;
        }
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            gather_evidence_multi(list->declare, ctxs, ctx_count);
            gather_evidence_multi(list->item, ctxs, ctx_count);
            break;
        }
        case AST_NODE_VAR_STAM:
        case AST_NODE_LET_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            gather_evidence_multi(let_node->declare, ctxs, ctx_count);
            break;
        }
        case AST_NODE_ASSIGN: {
            AstNamedNode* named = (AstNamedNode*)node;
            gather_evidence_multi(named->as, ctxs, ctx_count);
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            gather_evidence_multi(call->function, ctxs, ctx_count);
            gather_evidence_multi(call->argument, ctxs, ctx_count);
            break;
        }
        case AST_NODE_INDEX_EXPR: {
            AstBinaryNode* idx = (AstBinaryNode*)node;
            for (int c = 0; c < ctx_count; c++) {
                if (is_tracked_ref(idx->right, &ctxs[c])) {
                    TypeId left_tid = (idx->left && idx->left->type)
                        ? idx->left->type->type_id : LMD_TYPE_ANY;
                    bool is_typed_array = (left_tid == LMD_TYPE_ARRAY_NUM);
                    if (is_typed_array) ctxs[c].evidence |= INFER_INT;
                }
            }
            gather_evidence_multi(idx->left, ctxs, ctx_count);
            gather_evidence_multi(idx->right, ctxs, ctx_count);
            break;
        }
        case AST_NODE_PRIMARY: {
            AstPrimaryNode* pri = (AstPrimaryNode*)node;
            if (node->type && node->type->is_literal && node->type->type_id == LMD_TYPE_FLOAT) {
                for (int c = 0; c < ctx_count; c++) {
                    ctxs[c].evidence |= INFER_FLOAT_CONTEXT;
                }
            }
            gather_evidence_multi(pri->expr, ctxs, ctx_count);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            gather_evidence_multi(match->scrutinee, ctxs, ctx_count);
            AstMatchArm* arm = match->first_arm;
            while (arm) {
                gather_evidence_multi(arm->body, ctxs, ctx_count);
                arm = (AstMatchArm*)arm->next;
            }
            break;
        }
        default:
            break;
        }

        node = node->next;
    }
}

// Resolve evidence flags to a TypeId (same logic as infer_param_type)
static TypeId resolve_inferred_type(FnParamEvidence* ctx, bool is_proc) {
    if (ctx->evidence & INFER_STOP) return LMD_TYPE_ANY;
    if ((ctx->evidence & INFER_INT) && !(ctx->evidence & INFER_FLOAT)) return LMD_TYPE_INT;
    if (ctx->evidence & INFER_FLOAT) return LMD_TYPE_FLOAT;
    // Only weak arithmetic evidence (no int/float literal, no typed-array index) → keep ANY.
    // We no longer SPECULATE INT here: that guess truncated float args at the call boundary
    // (cd.ls positions/denominators). See infer_param_type() for the full rationale.
    return LMD_TYPE_ANY;
}

// Infer parameter types for ALL untyped params in a single body traversal.
// out_types must be pre-filled with declared types; untyped params (LMD_TYPE_ANY)
// will be overwritten with inferred types. Skips optional-without-default params.
static void infer_param_types_batched(AstFuncNode* fn_node, bool is_proc,
                                       TypeId* out_types, int param_count) {
    if (!fn_node->body || param_count == 0) return;

    AstNode* fn_as = (AstNode*)fn_node;
    TypeFunc* ft = (fn_as->type && fn_as->type->type_id == LMD_TYPE_FUNC)
        ? (TypeFunc*)fn_as->type : nullptr;

    // Collect FnParamEvidence for each untyped param
    FnParamEvidence ctxs[16];
    int ctx_indices[16];  // maps ctx index → param index
    int ctx_count = 0;

    TypeParam* tp = ft ? ft->param : nullptr;
    AstNamedNode* p = fn_node->param;
    for (int i = 0; i < param_count && p; i++) {
        if (out_types[i] != LMD_TYPE_ANY || !p->name) {
            p = (AstNamedNode*)p->next;
            tp = tp ? tp->next : nullptr;
            continue;
        }
        // Skip optional params without defaults (may_be_null — must remain ANY)
        bool may_be_null = tp && tp->is_optional && !tp->default_value;
        if (may_be_null) {
            p = (AstNamedNode*)p->next;
            tp = tp ? tp->next : nullptr;
            continue;
        }

        FnParamEvidence* ctx = &ctxs[ctx_count];
        memset(ctx, 0, sizeof(FnParamEvidence));
        int len = (int)p->name->len;
        if (len >= 64) len = 63;
        memcpy(ctx->names[0], p->name->chars, len);
        ctx->names[0][len] = '\0';
        ctx->name_lens[0] = len;
        ctx->name_count = 1;
        ctx_indices[ctx_count] = i;
        ctx_count++;

        p = (AstNamedNode*)p->next;
        tp = tp ? tp->next : nullptr;
    }

    if (ctx_count == 0) return;

    // Alias finding: iterate until convergence (single walk per round for ALL params)
    bool changed = true;
    while (changed) {
        changed = false;
        int prev_counts[16];
        for (int c = 0; c < ctx_count; c++) prev_counts[c] = ctxs[c].name_count;
        find_aliases_multi(fn_node->body, ctxs, ctx_count);
        for (int c = 0; c < ctx_count; c++) {
            if (ctxs[c].name_count != prev_counts[c]) changed = true;
        }
    }

    // Single body walk gathers evidence for ALL params
    gather_evidence_multi(fn_node->body, ctxs, ctx_count);

    // Resolve each param's type from evidence
    for (int c = 0; c < ctx_count; c++) {
        int pi = ctx_indices[c];
        TypeId inferred = resolve_inferred_type(&ctxs[c], is_proc);
        log_debug("mir: infer_param_types_batched('%s') aliases=%d evidence=%d → %d",
            ctxs[c].names[0], ctxs[c].name_count - 1, ctxs[c].evidence, inferred);
        if (inferred != LMD_TYPE_ANY) {
            out_types[pi] = inferred;
        }
    }
}

// ============================================================================
// Phase 4 (P4-3.3): Infer function return type from declaration and body.
// Returns a native TypeId (INT, FLOAT, BOOL) or LMD_TYPE_ANY if unknown.
// Only returns native types for simple cases where the return path is singular.
// ============================================================================
static TypeId infer_return_type(AstFuncNode* fn_node) {
    AstNode* fn_as_node = (AstNode*)fn_node;
    bool is_proc = (fn_as_node->node_type == AST_NODE_PROC);

    // 1. Check declared return type from TypeFunc::returned
    if (fn_as_node->type && fn_as_node->type->type_id == LMD_TYPE_FUNC) {
        TypeFunc* ft = (TypeFunc*)fn_as_node->type;
        if (ft->returned) {
            TypeId ret_tid = ft->returned->type_id;
            // Only accept simple native types for now
            if (mir_is_native_scalar_value_type(ret_tid)) {
                log_debug("mir: infer_return_type - declared type_id=%d (proc=%d)", ret_tid, is_proc);
                return ret_tid;
            }
            return LMD_TYPE_ANY;
        }
    }

    // 2. For fn (not pn): Check the body expression's type (fn body IS the return value)
    // Procs have multiple return paths via explicit `return` statements,
    // so body type inference is unreliable — only declared types are used.
    if (!is_proc && fn_node->body && fn_node->body->type) {
        TypeId body_tid = fn_node->body->type->type_id;
        // Only accept simple native scalar types
        if (mir_is_native_scalar_value_type(body_tid)) {
            log_debug("mir: infer_return_type - body type_id=%d", body_tid);
            return body_tid;
        }
    }

    return LMD_TYPE_ANY;
}

static MirScalarReturnMode infer_boxed_return_mode(MirTranspiler* mt,
        AstFuncNode* fn_node) {
    AstNode* fn_as_node = (AstNode*)fn_node;
    bool is_proc = fn_as_node->node_type == AST_NODE_PROC;
    TypeId return_type = LMD_TYPE_ANY;
    if (fn_as_node->type && fn_as_node->type->type_id == LMD_TYPE_FUNC) {
        TypeFunc* ft = (TypeFunc*)fn_as_node->type;
        if (ft->returned) return_type = ft->returned->type_id;
    }
    if (return_type == LMD_TYPE_ANY && !is_proc && fn_node->body) {
        return_type = get_effective_type(mt, fn_node->body);
    }
    return em_scalar_return_mode_for_type(return_type);
}

static FnVariantAnalysis* analyze_lambda_mir_variants(AstFuncNode* fn,
        NativeFuncInfo* native_info, MirScalarReturnMode scalar_mode,
        uint8_t lane_mask) {
    if (!fn || !fn->analysis) return NULL;
    FnAnalysis* analysis = fn->analysis;
    analysis->variant_count = 0;
    TypeFunc* type = fn->type && fn->type->type_id == LMD_TYPE_FUNC
        ? (TypeFunc*)fn->type : NULL;
    int param_count = type ? type->param_count : 0;
    FnVariantAnalysis* public_entry =
        &analysis->variants[analysis->variant_count++];
    memset(public_entry, 0, sizeof(*public_entry));
    public_entry->entry = {FN_ENTRY_PUBLIC_WRAPPER, false, false,
        false, true};
    public_entry->effects = {true, true, false,
        type && type->can_raise, analysis->may_await, true};
    public_entry->result.normal = {LMD_TYPE_ANY, VALUE_REP_ITEM,
        SCALAR_RETURN_NONE, false};
    public_entry->param_count = param_count;

    FnVariantAnalysis* body =
        &analysis->variants[analysis->variant_count++];
    memset(body, 0, sizeof(*body));
    bool native = native_info && native_info->return_type != LMD_TYPE_ANY;
    body->entry = {native ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY,
        true, false, false, false};
    body->effects = public_entry->effects;
    TypeId return_type = native ? native_info->return_type : LMD_TYPE_ANY;
    ValueRep return_rep = native
        ? native_info->return_mir == MIR_T_D ? VALUE_REP_F64 : VALUE_REP_I64
        : VALUE_REP_ITEM;
    ScalarReturnClass scalar_class = em_scalar_return_class_for_type(
        return_type);
    if (!native) scalar_class = scalar_mode == MIR_SCALAR_RETURN_DYNAMIC
        ? SCALAR_RETURN_DYNAMIC : scalar_class;
    body->result.normal = {return_type, return_rep,
        lane_mask & FN_RETURN_HOME_NORMAL ? scalar_class : SCALAR_RETURN_NONE,
        (lane_mask & FN_RETURN_HOME_NORMAL) != 0};
    if (type && type->can_raise && native) {
        body->result.error_lane = FN_ERROR_LANE_CONTEXT_ITEM;
        body->result.error = {LMD_TYPE_ERROR, VALUE_REP_ITEM,
            SCALAR_RETURN_DYNAMIC, (lane_mask & FN_RETURN_HOME_ERROR) != 0};
    }
    body->result.scalar_home_lane_mask = lane_mask;
    body->param_count = param_count;
    return body;
}

// ============================================================================
// Phase 4: Boxed wrapper for dual-version functions
// Generates a thin "_b" suffixed function with all-Item ABI that unboxes params,
// calls the native version, and returns the (already boxed) result.
// Used for dynamic dispatch (Function*) and cross-module calls.
// ============================================================================
static void emit_boxed_abi_wrapper(MirTranspiler* mt, const char* raw_name,
    AstFuncNode* fn_node, NativeFuncInfo* nfi, bool is_method)
{
    StrBuf* wrapper_name = strbuf_new_cap(64);
    write_fn_name_ex(wrapper_name, fn_node, NULL, "_b");
    log_debug("mir: generating ABI wrapper '%s' for '%s'", wrapper_name->str, raw_name);

    AstNode* fn_as = (AstNode*)fn_node;
    TypeFunc* fn_type = fn_as->type && fn_as->type->type_id == LMD_TYPE_FUNC
        ? (TypeFunc*)fn_as->type : NULL;
    bool is_closure = fn_node->captures && !is_method;
    bool is_variadic = fn_type && fn_type->is_variadic;

    // The wrapper preserves hidden env/self/vargs parameters but exposes boxed
    // Items for every user parameter.
    MIR_var_t params[33];
    char* param_name_copies[33];
    int param_count = 0;
    if (is_closure) {
        params[param_count] = {MIR_T_P, raw_strdup("_env_ptr"), 0}; // RAWALLOC_OK: MIR owns a copy
        param_name_copies[param_count] = (char*)params[param_count].name;
        param_count++;
    } else if (is_method) {
        params[param_count] = {MIR_T_P, raw_strdup("_self"), 0}; // RAWALLOC_OK: MIR owns a copy
        param_name_copies[param_count] = (char*)params[param_count].name;
        param_count++;
    }
    AstNamedNode* param = fn_node->param;
    while (param && param_count < 31) {
        char pname[64];
        snprintf(pname, sizeof(pname), "_%.*s", (int)param->name->len, param->name->chars);
        params[param_count] = {MIR_T_I64, raw_strdup(pname), 0}; // RAWALLOC_OK: MIR manages param name lifetime
        param_name_copies[param_count] = (char*)params[param_count].name;
        param_count++;
        param = (AstNamedNode*)param->next;
    }
    if (is_variadic && param_count < 32) {
        params[param_count] = {MIR_T_P, raw_strdup("_vargs"), 0}; // RAWALLOC_OK: MIR owns a copy
        param_name_copies[param_count] = (char*)params[param_count].name;
        param_count++;
    }

    // Save outer function context
    MIR_item_t saved_func_item = mt->em.func_item;
    MIR_func_t saved_func = mt->em.func;
    MIR_reg_t saved_consts = mt->consts_reg;
    MirFrameState saved_frame = em_frame_suspend(&mt->em);

    // Create wrapper function
    MIR_type_t ret_type = MIR_T_I64;
    MIR_item_t wrapper_item = MIR_new_func_arr(mt->ctx, wrapper_name->str, 1, &ret_type, param_count, params);
    MIR_func_t wrapper_func = MIR_get_item_func(mt->ctx, wrapper_item);
    mt->em.func_item = wrapper_item;
    mt->em.func = wrapper_func;

    // Free strdup copies
    for (int i = 0; i < param_count; i++) raw_free(param_name_copies[i]);

    MIR_item_t rt_import = MIR_new_import(mt->ctx, "_lambda_rt");
    MIR_reg_t rt_addr = new_reg(mt, "wrapper_rt_addr", MIR_T_I64);
    MIR_reg_t runtime = new_reg(mt, "wrapper_runtime", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, rt_addr), MIR_new_ref_op(mt->ctx, rt_import)));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, runtime),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, rt_addr, 0, 1)));
    mt->em.frame.runtime = runtime;
    mt->em.frame.return_type = ret_type;
    emit_jit_root_frame_enter(mt);
    begin_function_epilogue(mt, ret_type, RETURN_LANE_NONE,
        MIR_SCALAR_RETURN_NONE);
    mt->em.frame.plan.debug_name = wrapper_name->str;
    emit_number_frame_enter(mt);

    MIR_op_t call_args[33];
    MIR_var_t call_vars[33];
    int call_arg_count = 0;
    if (is_closure) {
        MIR_reg_t env = MIR_reg(mt->ctx, "_env_ptr", wrapper_func);
        call_args[call_arg_count] = MIR_new_reg_op(mt->ctx, env);
        call_vars[call_arg_count++] = {MIR_T_P, "env", 0};
    } else if (is_method) {
        MIR_reg_t self = MIR_reg(mt->ctx, "_self", wrapper_func);
        call_args[call_arg_count] = MIR_new_reg_op(mt->ctx, self);
        call_vars[call_arg_count++] = {MIR_T_P, "self", 0};
    }
    param = fn_node->param;
    int user_index = 0;
    while (param && call_arg_count < 31) {
        char prefixed[68];
        snprintf(prefixed, sizeof(prefixed), "_%.*s", (int)param->name->len, param->name->chars);
        MIR_reg_t preg = MIR_reg(mt->ctx, prefixed, wrapper_func);

        if (nfi && user_index < nfi->param_count &&
                mir_is_native_param_type(nfi->param_types[user_index])) {
            MIR_reg_t unboxed = emit_unbox(mt, preg, nfi->param_types[user_index]);
            call_args[call_arg_count] = MIR_new_reg_op(mt->ctx, unboxed);
            call_vars[call_arg_count] = {nfi->param_mir[user_index], "p", 0};
        } else {
            call_args[call_arg_count] = MIR_new_reg_op(mt->ctx, preg);
            call_vars[call_arg_count] = {MIR_T_I64, "p", 0};
        }
        call_arg_count++;
        user_index++;
        param = (AstNamedNode*)param->next;
    }
    if (is_variadic) {
        MIR_reg_t vargs = MIR_reg(mt->ctx, "_vargs", wrapper_func);
        call_args[call_arg_count] = MIR_new_reg_op(mt->ctx, vargs);
        call_vars[call_arg_count++] = {MIR_T_P, "vargs", 0};
    }

    MIR_item_t raw_func = find_local_func(mt, raw_name);
    LocalFuncEntry* raw_entry = find_local_func_entry(mt, raw_name);
    if (!raw_func || !raw_entry || !raw_entry->variant) {
        log_error("mir: ABI wrapper - missing body contract for '%s'", raw_name);
        abort();
    }
    bool native_return = nfi && nfi->return_type != LMD_TYPE_ANY;
    int raw_lane_kind = native_return
        ? ((fn_type && fn_type->can_raise) ? RETURN_LANE_ERROR : RETURN_LANE_NONE)
        : RETURN_LANE_SCALAR;
    MIR_type_t call_types[33];
    for (int i = 0; i < call_arg_count; i++) {
        call_types[i] = call_vars[i].type;
    }
    MirCallOptions options = {{MIR_FRAME_REF_NONE, 0},
        (uint8_t)(FN_RETURN_HOME_NORMAL | FN_RETURN_HOME_ERROR), false};
    MirCallResult direct = em_call_direct(&mt->em, raw_name, raw_func,
        raw_entry ? raw_entry->variant : NULL, call_arg_count,
        call_types, call_args, &options);
    MIR_reg_t result = direct.normal.reg;
    int scalar_home_id = direct.normal.scalar_home_id
        ? direct.normal.scalar_home_id : direct.error.scalar_home_id;
    MIR_reg_t second_result = 0;
    if (raw_lane_kind == RETURN_LANE_ERROR) {
        second_result = new_reg(mt, "werr", MIR_T_I64);
    }
    if (second_result) {
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, second_result),
            MIR_new_mem_op(mt->ctx, MIR_T_I64,
                offsetof(Context, mir_return_lane), runtime, 0, 1)));
    }

    MIR_reg_t boxed_result = native_return
        ? emit_box(mt, result, nfi->return_type) : result;
    if (native_return) {
        if (raw_lane_kind == RETURN_LANE_ERROR) {
            MIR_reg_t selected = new_reg(mt, "wrapper_lane", MIR_T_I64);
            MIR_label_t error = new_label(mt);
            MIR_label_t selected_done = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, error),
                MIR_new_reg_op(mt->ctx, second_result)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, selected),
                MIR_new_reg_op(mt->ctx, boxed_result)));
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, selected_done)));
            emit_label(mt, error);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, selected),
                MIR_new_reg_op(mt->ctx, second_result)));
            emit_label(mt, selected_done);
            boxed_result = selected;
        }
    }
    if (scalar_home_id) {
        em_scalar_home_bind(&mt->em, scalar_home_id, boxed_result);
    }
    // Public results must not retain this wrapper's activation home.
    boxed_result = emit_call_1(mt, "lambda_item_heap_rehome", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_result));
    emit_function_return(mt, MIR_new_reg_op(mt->ctx, boxed_result));
    finish_function_epilogue(mt);
    finalize_gc_root_publication(mt, wrapper_name->str);
    finalize_side_root_frame(mt);

    MIR_finish_func(mt->ctx);

    // Register wrapper as local function with _b name
    register_local_func_contract(mt, wrapper_name->str, wrapper_item,
        fn_analysis_variant(fn_node->analysis, FN_ENTRY_PUBLIC_WRAPPER));

    log_debug("mir: ABI wrapper '%s' generated successfully", wrapper_name->str);

    // Restore outer function context
    mt->em.func_item = saved_func_item;
    mt->em.func = saved_func;
    mt->consts_reg = saved_consts;
    em_frame_dispose(&mt->em);
    em_frame_restore(&mt->em, saved_frame);

    strbuf_free(wrapper_name);
}

static void transpile_func_def(MirTranspiler* mt, AstFuncNode* fn_node) {
    // Build function name
    StrBuf* name_buf = strbuf_new_cap(64);
    write_fn_name(name_buf, fn_node, NULL);

    log_debug("mir: transpile_func_def '%s'", name_buf->str);

    // Check if this is a method inside an object type definition
    bool is_method = (mt->method_owner != nullptr);

    // Check if this function is a closure (has captured variables)
    // Methods are NOT treated as closures: field captures are loaded from _self, not a closure env.
    // This matches the C transpiler which clears fn_node->captures for methods.
    bool is_closure = (fn_node->captures != nullptr) && !is_method;

    AstNode* fn_as_node = (AstNode*)fn_node;
    TypeFunc* fn_type = (fn_as_node->type && fn_as_node->type->type_id == LMD_TYPE_FUNC)
        ? (TypeFunc*)fn_as_node->type : NULL;
    bool is_variadic = fn_type && fn_type->is_variadic;
    bool is_proc_fn = (fn_as_node->node_type == AST_NODE_PROC);
    bool needs_task_context = is_proc_fn && fn_node->analysis &&
        fn_node->analysis->needs_task_context;
    bool is_async_proc = is_proc_fn && fn_node->analysis &&
        fn_node->analysis->may_await;

    // ===== Phase 4: Pre-resolve all parameter types (declared + inferred) =====
    // We resolve types BEFORE creating the MIR function so we can decide whether
    // to generate a native-typed version or the traditional all-Item version.
    // Uses infer_cache from prepass_forward_declare to avoid redundant body walks.
    TypeId resolved_param_types[16];
    int user_param_count = 0;

    // First pass: collect declared types and TypeUnary array resolution
    {
        TypeParam* tp_iter = fn_type ? fn_type->param : NULL;
        AstNamedNode* p = fn_node->param;
        while (p && user_param_count < 16) {
            TypeId tid = resolve_declared_param_type(p, tp_iter);
            if (tid == LMD_TYPE_ARRAY_NUM) {
                log_debug("mir: param '%.*s' resolved typed array annotation -> type_id=%d",
                    (int)p->name->len, p->name->chars, tid);
            }

            resolved_param_types[user_param_count] = tid;
            user_param_count++;
            tp_iter = tp_iter ? tp_iter->next : NULL;
            p = (AstNamedNode*)p->next;
        }
    }

    // Infer types for untyped params: use cache from prepass_forward_declare, or batch-infer
    if (fn_node->body) {
        InferCacheEntry cache_key;
        cache_key.fn = fn_node;
        InferCacheEntry* cached = (InferCacheEntry*)hashmap_get(mt->infer_cache, &cache_key);
        if (cached && cached->param_count == user_param_count) {
            // Apply cached inference results from prepass_forward_declare
            for (int i = 0; i < user_param_count; i++) {
                if (resolved_param_types[i] == LMD_TYPE_ANY &&
                    cached->param_types[i] != LMD_TYPE_ANY) {
                    log_debug("mir: param[%d] inferred type_id=%d (cached)", i, cached->param_types[i]);
                    resolved_param_types[i] = cached->param_types[i];
                }
            }
        } else {
            // No cache hit (closure/variadic/method): batch-infer in single body walk
            infer_param_types_batched(fn_node, is_proc_fn, resolved_param_types, user_param_count);
        }
    }

    // Determine if this function qualifies for native (unboxed) dual version.
    // Eligible: not a closure, not a method, not variadic, has at least one
    // param with a native type (INT, FLOAT, BOOL, STRING, INT64), OR has a
    // declared native return type (P4-3.4: return-type-only optimization).
    bool generate_native = false;
    bool has_typed_array_param = false;
    for (int i = 0; i < user_param_count; i++) {
        if (resolved_param_types[i] == LMD_TYPE_ARRAY_NUM) {
            has_typed_array_param = true;
            break;
        }
    }
    if (!needs_task_context && !is_closure && !is_method && !is_variadic &&
            !has_typed_array_param) {
        for (int i = 0; i < user_param_count; i++) {
            if (mir_is_native_param_type(resolved_param_types[i])) {
                generate_native = true;
                break;
            }
        }
        // P4-3.4: Also enable native version when return type alone is native.
        // This allows procs with untyped params but declared return type to
        // avoid boxing/unboxing return values at call sites.
        if (!generate_native) {
            TypeId ret_tid = infer_return_type(fn_node);
            if (mir_is_native_scalar_value_type(ret_tid)) {
                generate_native = true;
            }
        }
    }

    // Register NativeFuncInfo so call sites know to use the native version
    if (generate_native) {
        NativeFuncInfo nfi;
        memset(&nfi, 0, sizeof(nfi));
        nfi.param_count = user_param_count;
        nfi.has_native = true;
        for (int i = 0; i < user_param_count; i++) {
            nfi.param_types[i] = resolved_param_types[i];
            nfi.param_mir[i] = type_to_mir(resolved_param_types[i]);
        }
        // P4-3.3: Infer return type for native version
        TypeId ret_tid = infer_return_type(fn_node);
        nfi.return_type = ret_tid;
        nfi.return_mir = (ret_tid != LMD_TYPE_ANY) ? type_to_mir(ret_tid) : MIR_T_I64;
        register_native_func_info(mt, name_buf->str, &nfi);
        log_debug("mir: dual version - native '%s' with %d params, return=%d",
            name_buf->str, user_param_count, ret_tid);
    }

    // ===== Build MIR parameter list =====
    // For native version: use resolved native MIR types for params
    // For boxed version (non-native): all params are MIR_T_I64 (Item)
    MIR_var_t params[33];
    int param_count = 0;

    // Hidden leading params (_env_ptr for closures, _self for methods)
    if (is_closure) {
        params[param_count] = {MIR_T_P, raw_strdup("_env_ptr"), 0}; // RAWALLOC_OK: MIR manages param name lifetime
        param_count++;
    }
    if (is_method && !is_closure) {
        params[param_count] = {MIR_T_P, raw_strdup("_self"), 0}; // RAWALLOC_OK: MIR manages param name lifetime
        param_count++;
    }

    // User params: use native types for native version, MIR_T_I64 for boxed
    AstNamedNode* param = fn_node->param;
    int pi_build = 0;
    while (param && param_count < 31) {
        char pname[64];
        snprintf(pname, sizeof(pname), "_%.*s", (int)param->name->len, param->name->chars);

        MIR_type_t mir_ptype = MIR_T_I64;  // default: boxed Item
        if (generate_native && pi_build < user_param_count) {
            mir_ptype = type_to_mir(resolved_param_types[pi_build]);
        }
        params[param_count] = {mir_ptype, raw_strdup(pname), 0}; // RAWALLOC_OK: MIR manages param name lifetime
        param_count++;
        pi_build++;
        param = (AstNamedNode*)param->next;
    }

    // Hidden trailing params (_vargs)
    if (is_variadic && param_count < 32) {
        params[param_count] = {MIR_T_P, raw_strdup("_vargs"), 0}; // RAWALLOC_OK: MIR manages param name lifetime
        param_count++;
    }

    // Determine return type
    // P4-3.3: Use native return type when inferred (INT/BOOL → I64, FLOAT → D)
    MIR_type_t ret_type = MIR_T_I64;
    bool native_return = false;
    if (generate_native) {
        NativeFuncInfo* nfi_ret = find_native_func_info(mt, name_buf->str);
        if (nfi_ret && nfi_ret->return_type != LMD_TYPE_ANY) {
            ret_type = nfi_ret->return_mir;
            native_return = true;
        }
    }

    int return_lane_kind = native_return
        ? ((fn_type && fn_type->can_raise) ? RETURN_LANE_ERROR : RETURN_LANE_NONE)
        : RETURN_LANE_SCALAR;
    MirScalarReturnMode body_scalar_mode = return_lane_kind == RETURN_LANE_ERROR
        ? MIR_SCALAR_RETURN_DYNAMIC
        : return_lane_kind == RETURN_LANE_SCALAR
            ? infer_boxed_return_mode(mt, fn_node)
            : MIR_SCALAR_RETURN_NONE;
    uint8_t scalar_home_lane_mask = body_scalar_mode != MIR_SCALAR_RETURN_NONE
        ? (return_lane_kind == RETURN_LANE_ERROR
            ? FN_RETURN_HOME_ERROR : FN_RETURN_HOME_NORMAL)
        : 0;
    if (scalar_home_lane_mask && param_count < 33) {
        // Only generated bodies expose the trailing caller-owned scalar home.
        params[param_count] = {MIR_T_P, raw_strdup("_scalar_home"), 0};
        param_count++;
    }

    // Save current function context
    MIR_item_t saved_func_item = mt->em.func_item;
    MIR_func_t saved_func = mt->em.func;
    MIR_reg_t saved_consts_reg = mt->consts_reg;
    MIR_reg_t saved_gc_reg = mt->gc_reg;
    MirFrameState saved_frame = em_frame_suspend(&mt->em);
    bool saved_in_user_func = mt->in_user_func;
    AstNode* saved_func_body = mt->func_body;
    bool saved_in_async_proc = mt->in_async_proc;
    bool saved_emitting_async_call = mt->emitting_async_call;
    int saved_async_call_state = mt->async_call_state;
    int saved_async_call_spill_count = mt->async_call_spill_count;
    bool saved_async_call_resume_emitted = mt->async_call_resume_emitted;
    AstCallNode* saved_async_call_target = mt->async_call_target;
    MIR_reg_t saved_async_frame_reg = mt->async_frame_reg;
    MIR_reg_t saved_async_scope_base_reg = mt->async_scope_base_reg;
    MIR_label_t* saved_async_state_labels = mt->async_state_labels;
    int saved_async_state_count = mt->async_state_count;
    int saved_async_next_state = mt->async_next_state;
    int saved_async_next_slot = mt->async_next_slot;
    AsyncRegSpill* saved_async_spills = mt->async_spills;
    int saved_async_spill_count = mt->async_spill_count;
    int saved_async_spill_capacity = mt->async_spill_capacity;
    bool saved_async_tracking = mt->async_tracking;
    bool saved_async_tracking_suppressed = mt->async_tracking_suppressed;
    mt->in_user_func = true;
    mt->func_body = fn_node->body;  // P4-3.2: for mutation analysis
    mt->in_async_proc = is_async_proc;
    mt->emitting_async_call = false;
    mt->async_call_state = 0;
    mt->async_call_spill_count = -1;
    mt->async_call_resume_emitted = false;
    mt->async_call_target = NULL;
    mt->async_frame_reg = 0;
    mt->async_scope_base_reg = 0;
    mt->async_state_count = is_async_proc && fn_node->analysis
        ? fn_node->analysis->await_point_count : 0;
    mt->async_next_state = 0;
    mt->async_next_slot = 0;
    mt->async_spills = NULL;
    mt->async_spill_count = 0;
    mt->async_spill_capacity = 0;
    mt->async_tracking = false;
    mt->async_tracking_suppressed = false;
    mt->async_state_labels = NULL;
    if (mt->async_state_count > 0) {
        mt->async_state_labels = (MIR_label_t*)mem_calloc(
            (size_t)(mt->async_state_count + 1), sizeof(MIR_label_t), MEM_CAT_EVAL);
    }

    // Save original strdup pointers before MIR overwrites them
    char* param_name_copies[33];
    for (int i = 0; i < param_count; i++) param_name_copies[i] = (char*)params[i].name;

    // Create function (MIR replaces params[i].name with internal copies)
    MIR_type_t return_types[1] = {ret_type};
    int return_count = 1;
    MIR_item_t func_item = MIR_new_func_arr(mt->ctx, name_buf->str,
        return_count, return_types, param_count, params);
    MIR_func_t func = MIR_get_item_func(mt->ctx, func_item);
    mt->em.func_item = func_item;
    mt->em.func = func;

    // Free our strdup copies (MIR made its own)
    for (int i = 0; i < param_count; i++) raw_free(param_name_copies[i]);

    // Load runtime context from _lambda_rt before setting up module-local state.
    MIR_item_t rt_import_fn = MIR_new_import(mt->ctx, "_lambda_rt");
    MIR_reg_t rt_addr_fn = new_reg(mt, "rt_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_addr_fn),
        MIR_new_ref_op(mt->ctx, rt_import_fn)));
    // Dereference: runtime = *(&_lambda_rt)
    MIR_reg_t runtime_fn = new_reg(mt, "runtime", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, runtime_fn),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, rt_addr_fn, 0, 1)));
    emit_load_module_consts(mt);

    // Load this module's type_list from per-module BSS _mod_type_list_ptr. Used by
    // map_with_tl/elmt_with_tl/etc. to pass the module's own type_list to those calls.
    MIR_reg_t mod_tl_bss_addr_fn = new_reg(mt, "mod_tl_bss", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mod_tl_bss_addr_fn),
        MIR_new_ref_op(mt->ctx, mt->type_list_bss)));
    mt->type_list_reg = new_reg(mt, "type_list", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->type_list_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, mod_tl_bss_addr_fn, 0, 1)));

    // Load gc_heap_t* for inline bump allocation: context->heap->gc
    MIR_reg_t heap_reg_fn = new_reg(mt, "heap_ptr", MIR_T_I64);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, heap_reg_fn),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(EvalContext, heap), runtime_fn, 0, 1)));  // context->heap
#pragma clang diagnostic pop
    mt->gc_reg = new_reg(mt, "gc_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->gc_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, heap_reg_fn, 0, 1)));  // heap->gc

    mt->em.frame.runtime = runtime_fn;
    mt->em.frame.return_type = ret_type;
    emit_jit_root_frame_enter(mt);
    begin_function_epilogue(mt, ret_type, return_lane_kind, body_scalar_mode);
    mt->em.frame.plan.entry_kind = generate_native
        ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY;
    mt->em.frame.plan.entry_mode = MIR_ENTRY_BOUND_INTERNAL;
    mt->em.frame.plan.debug_name = name_buf->str;
    mt->em.frame.plan.scalar_return_mode = body_scalar_mode;
    mt->em.frame.plan.scalar_home_lane_mask = scalar_home_lane_mask;
    mt->em.frame.plan.accepts_caller_scalar_home = scalar_home_lane_mask != 0;
    mt->em.frame.incoming_scalar_home = scalar_home_lane_mask
        ? MIR_reg(mt->ctx, "_scalar_home", func) : 0;
    emit_number_frame_enter(mt);

    FnVariantAnalysis* body_variant = analyze_lambda_mir_variants(fn_node,
        generate_native ? find_native_func_info(mt, name_buf->str) : NULL,
        body_scalar_mode, scalar_home_lane_mask);

    // Register as local function early (before body transpilation for recursion)
    register_local_func_contract(mt, name_buf->str, func_item, body_variant);

    // Also register by raw name for fallback lookup when ident->entry is NULL
    if (fn_node->name) {
        char raw_name[128];
        snprintf(raw_name, sizeof(raw_name), "%.*s", (int)fn_node->name->len, fn_node->name->chars);
        if (!find_local_func(mt, raw_name)) {
            register_local_func_contract(mt, raw_name, func_item, body_variant);
        }
    }

    // Set up parameter scope
    push_scope(mt);

    if (needs_task_context) {
        MIR_reg_t has_task = emit_call_0(mt, "lambda_task_has_current", MIR_T_I64);
        MIR_label_t in_task = new_label(mt);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BT,
            MIR_new_label_op(mt->ctx, in_task), MIR_new_reg_op(mt->ctx, has_task)));

        MIR_reg_t launch_args = emit_call_0(mt, "list", MIR_T_P);
        int launch_args_root = create_pointer_gc_root_slot(mt, launch_args);
        AstNamedNode* launch_param = fn_node->param;
        while (launch_param) {
            char launch_name[68];
            snprintf(launch_name, sizeof(launch_name), "_%.*s",
                (int)launch_param->name->len, launch_param->name->chars);
            MIR_reg_t launch_value = MIR_reg(mt->ctx, launch_name, func);
            launch_args = load_gc_root_slot(mt, launch_args_root, "launch_args");
            emit_call_void_2(mt, "list_push",
                MIR_T_P, MIR_new_reg_op(mt->ctx, launch_args),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, launch_value));
            launch_param = (AstNamedNode*)launch_param->next;
        }
        launch_args = load_gc_root_slot(mt, launch_args_root, "launch_args");

        MIR_reg_t function_ptr = new_reg(mt, "async_root_fn", MIR_T_P);
        StrBuf* task_wrapper_name = strbuf_new_cap(64);
        write_fn_name_ex(task_wrapper_name, fn_node, NULL, "_b");
        MIR_item_t task_wrapper = find_local_func(mt, task_wrapper_name->str);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, function_ptr),
            MIR_new_ref_op(mt->ctx, task_wrapper ? task_wrapper : func_item)));
        strbuf_free(task_wrapper_name);
        MIR_reg_t launch_env = new_reg(mt, "async_root_env", MIR_T_P);
        if (is_closure) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, launch_env),
                MIR_new_reg_op(mt->ctx, MIR_reg(mt->ctx, "_env_ptr", func))));
        } else if (is_method) {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, launch_env),
                MIR_new_reg_op(mt->ctx, MIR_reg(mt->ctx, "_self", func))));
        } else {
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, launch_env), MIR_new_int_op(mt->ctx, 0)));
        }
        MIR_var_t launch_vars[4] = {
            {MIR_T_P, "fn", 0}, {MIR_T_P, "env", 0},
            {MIR_T_I64, "env_count", 0}, {MIR_T_P, "args", 0}
        };
        MirImportEntry* launch_import = ensure_import(mt, "lambda_task_run_root_raw",
            MIR_T_I64, 4, launch_vars, 1);
        MIR_reg_t launch_result = new_reg(mt, "async_root_result", MIR_T_I64);
        emit_insn(mt, MIR_new_call_insn(mt->ctx, 7,
            MIR_new_ref_op(mt->ctx, launch_import->proto),
            MIR_new_ref_op(mt->ctx, launch_import->import),
            MIR_new_reg_op(mt->ctx, launch_result),
            MIR_new_reg_op(mt->ctx, function_ptr),
            MIR_new_reg_op(mt->ctx, launch_env),
            MIR_new_int_op(mt->ctx, fn_node->analysis ? fn_node->analysis->capture_count : 0),
            MIR_new_reg_op(mt->ctx, launch_args)));
        emit_function_return(mt, MIR_new_reg_op(mt->ctx, launch_result));
        emit_label(mt, in_task);
    }

    if (is_async_proc) {
        mt->async_frame_reg = new_reg(mt, "async_frame", MIR_T_P);
        mt->async_scope_base_reg = emit_call_1(mt, "lambda_async_frame_scope_base", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg));
        MIR_reg_t frame_state = emit_call_1(mt, "lambda_async_frame_state", MIR_T_I64,
            MIR_T_P, MIR_new_reg_op(mt->ctx, mt->async_frame_reg));
        for (int state = 1; state <= mt->async_state_count; state++) {
            mt->async_state_labels[state] = new_label(mt);
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BEQ,
                MIR_new_label_op(mt->ctx, mt->async_state_labels[state]),
                MIR_new_reg_op(mt->ctx, frame_state),
                MIR_new_int_op(mt->ctx, state)));
        }
        mt->async_tracking = true;
    }

    // Save and set closure context
    AstFuncNode* saved_closure = mt->current_closure;
    MIR_reg_t saved_env_reg = mt->env_reg;
    bool saved_in_pipe = mt->in_pipe;
    MIR_reg_t saved_pipe_item_reg = mt->pipe_item_reg;
    MIR_reg_t saved_self_reg = mt->self_reg;
    if (is_closure) {
        mt->current_closure = fn_node;
        // Get the _env_ptr parameter register
        MIR_reg_t env_ptr_reg = MIR_reg(mt->ctx, "_env_ptr", func);
        mt->env_reg = env_ptr_reg;

        // Load captured variables from env into local scope
        // env is a flat array of Items (8 bytes each)
        int cap_count = 0;
        for (FnCapture* count_cap = fn_node->captures; count_cap; count_cap = count_cap->next) {
            cap_count++;
        }
        int cap_index = 0;
        FnCapture* cap = fn_node->captures;
        while (cap) {
            char cap_name[128];
            snprintf(cap_name, sizeof(cap_name), "%s", cap->name);

            MIR_reg_t cap_val = emit_call_4(mt, "owned_item_slot_read", MIR_T_I64,
                MIR_T_P, MIR_new_reg_op(mt->ctx, env_ptr_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, cap_count),
                MIR_T_I64, MIR_new_int_op(mt->ctx, cap_index),
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

            // Store as local variable (already boxed as Item)
            set_var(mt, cap_name, cap_val, MIR_T_I64, LMD_TYPE_ANY);

            // Mark as captured variable with env offset for write-back on mutation
            MirVarEntry* cap_var = find_var(mt, cap_name);
            if (cap_var) {
                cap_var->env_offset = cap_index * 8;
            }

            log_debug("mir: closure '%s' - loaded capture '%s' at env offset %d",
                name_buf->str, cap_name, cap_index * 8);

            cap_index++;
            cap = cap->next;
        }
    }

    // Method body setup: load object fields from _self into local scope.
    // _self receives the boxed self Item (passed as void* via closure mechanism).
    // Fields are accessed via item_attr(self_item, "fieldname") for each shape entry.
    if (is_method) {
        // _self param is (void*)(uintptr_t)boxed_self_item — move to I64 register
        MIR_reg_t self_ptr_reg = MIR_reg(mt->ctx, "_self", func);
        MIR_reg_t self_item_reg = new_reg(mt, "self_it", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, self_item_reg),
            MIR_new_reg_op(mt->ctx, self_ptr_reg)));

        // The receiver is a managed Item and field access may collect. Publish
        // it once for the whole method instead of rebuilding an unrooted copy.
        create_gc_root_slot(mt, self_item_reg);
        mt->self_reg = self_item_reg;

        // make '~' (current item / self) refer to self inside method body
        mt->in_pipe = true;
        mt->pipe_item_reg = self_item_reg;

        // Load each field of the object type as a local variable
        TypeObject* obj_type = mt->method_owner;
        ShapeEntry* se = obj_type->shape;
        while (se) {
            if (se->name) {
                char field_name[128];
                snprintf(field_name, sizeof(field_name), "%.*s",
                    (int)se->name->length, se->name->str);

                // Load: field_val = item_attr(self_item, "fieldname")
                MIR_reg_t name_str = emit_load_string_literal(mt, se->name->str);
                MIR_reg_t field_val = emit_call_2(mt, "item_attr", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, self_item_reg),
                    MIR_T_P, MIR_new_reg_op(mt->ctx, name_str));

                // Unbox to native type if known
                TypeId ftid = se->type ? se->type->type_id : LMD_TYPE_ANY;
                if (ftid == LMD_TYPE_TYPE && se->type) {
                    TypeType* field_type = (TypeType*)se->type;
                    if (field_type->type) ftid = field_type->type->type_id;
                }
                if (is_integer_type_id(ftid) || ftid == LMD_TYPE_BOOL) {
                    MIR_reg_t unboxed = emit_unbox(mt, field_val, ftid);
                    set_var(mt, field_name, unboxed, type_to_mir(ftid), ftid);
                } else if (ftid == LMD_TYPE_FLOAT) {
                    MIR_reg_t unboxed = emit_unbox(mt, field_val, ftid);
                    set_var(mt, field_name, unboxed, MIR_T_D, ftid);
                } else {
                    set_var(mt, field_name, field_val, MIR_T_I64, LMD_TYPE_ANY);
                }

                log_debug("mir: method '%s' - loaded field '%s' (type=%d) from self",
                    name_buf->str, field_name, ftid);
            }
            se = se->next;
        }
    }

    // Set vargs for variadic functions (save previous for nesting)
    if (is_variadic) {
        MIR_reg_t vargs_reg = MIR_reg(mt->ctx, "_vargs", func);
        MIR_reg_t saved_reg = emit_call_1(mt, "set_vargs", MIR_T_P, MIR_T_P,
            MIR_new_reg_op(mt->ctx, vargs_reg));
        // Store in a named register so we can reference it at function exit
        MIR_reg_t named_saved = MIR_new_func_reg(mt->ctx, mt->em.func, MIR_T_I64, "_saved_vargs");
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, named_saved),
            MIR_new_reg_op(mt->ctx, saved_reg)));
    }

    // Bind parameters as local variables
    // Uses pre-resolved types from the Phase 4 pre-resolve step above.
    log_debug("mir: binding params for '%s', fn_type=%p, native=%d",
        name_buf->str, (void*)fn_type, generate_native);
    param = fn_node->param;
    int pi = 0;
    while (param && pi < user_param_count) {
        char pname[64];
        snprintf(pname, sizeof(pname), "%.*s", (int)param->name->len, param->name->chars);

        // Function parameter register is already created by MIR with the prefixed name
        char prefixed[68];
        snprintf(prefixed, sizeof(prefixed), "_%s", pname);
        MIR_reg_t preg = MIR_reg(mt->ctx, prefixed, func);

        TypeId tid = resolved_param_types[pi];

        log_debug("mir: param '%s' type_id=%d has_annotation=%d native=%d", pname, tid,
            param->type ? param->type->type_id : -1, generate_native);

        if (generate_native && mir_is_native_param_type(tid)) {
            // Phase 4 native version: param arrives as native type, no unboxing needed.
            // Register directly with the native MIR type.
            MIR_type_t mtype = type_to_mir(tid);
            set_var(mt, pname, preg, mtype, tid);
            log_debug("mir: native param '%s' registered directly, mir_type=%d", pname, mtype);
        } else if (!generate_native && mir_is_native_param_type(tid)) {
            // Boxed version: params arrive as boxed Items, unbox to native type
            MIR_reg_t unboxed = emit_unbox(mt, preg, tid);
            MIR_type_t mtype = type_to_mir(tid);
            set_var(mt, pname, unboxed, mtype, tid);
        } else {
            // Untyped, nullable optional, or complex type: keep as boxed Item.
            // Preserve known container type (e.g. ARRAY_NUM from annotation) so
            // the transpiler can generate fast paths for array access.
            Type* param_decl_type = mir_unwrap_decl_type((Type*)param->type);
            TypeId param_decl_tid = param_decl_type ? param_decl_type->type_id : tid;
            TypeId var_type = (tid == LMD_TYPE_ARRAY_NUM || param_decl_tid == LMD_TYPE_NUM_SIZED ||
                               param_decl_tid == LMD_TYPE_UINT64) ? param_decl_tid : LMD_TYPE_ANY;

            MIR_reg_t final_reg = preg;
            // For typed array params: ensure the incoming array is the correct
            // typed representation.  The caller may pass a generic Array that
            // needs to be converted to ArrayNum so that the FAST PATH inline
            // reads operate on the correct memory layout.
            TypeId param_elem_tid = LMD_TYPE_ANY;
            if (var_type == LMD_TYPE_ARRAY_NUM) {
                // Re-derive element type from the param's type annotation
                param_elem_tid = LMD_TYPE_INT;  // default
                if (param->type && param->type->kind == TYPE_KIND_UNARY) {
                    TypeParam* tp_cast = (TypeParam*)param->type;
                    Type* full = tp_cast->full_type;
                    if (full && full->kind == TYPE_KIND_UNARY) {
                        TypeUnary* unary = (TypeUnary*)full;
                        Type* operand = unary->operand;
                        if (operand && operand->type_id == LMD_TYPE_TYPE && operand->kind == TYPE_KIND_SIMPLE)
                            operand = ((TypeType*)operand)->type;
                        if (operand) param_elem_tid = operand->type_id;
                    }
                }
                final_reg = emit_call_2(mt, "ensure_typed_array", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, preg),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, param_elem_tid));
                // failed parameter coercions must exit before fast array paths see a null pointer.
                emit_return_item_error_if_zero(mt, final_reg);
                log_debug("mir: param '%s' — inserted ensure_typed_array (elem=%d)", pname, param_elem_tid);
            } else if (var_type == LMD_TYPE_NUM_SIZED) {
                final_reg = emit_coerce_boxed_to_declared(mt, preg, param_decl_type);
                log_debug("mir: param '%s' — inserted coerce_num_sized", pname);
            } else if (var_type == LMD_TYPE_UINT64) {
                final_reg = emit_coerce_boxed_to_declared(mt, preg, param_decl_type);
                log_debug("mir: param '%s' — inserted coerce_uint64", pname);
            }

            set_var(mt, pname, final_reg, MIR_T_I64, var_type);
            if (var_type == LMD_TYPE_NUM_SIZED) {
                MirVarEntry* v = find_var(mt, pname);
                if (v) v->num_type = type_num_sized_kind(param_decl_type);
            }

            // Set elem_type on the variable entry for fast path dispatch
            if (var_type == LMD_TYPE_ARRAY_NUM && param_elem_tid != LMD_TYPE_ANY) {
                MirVarEntry* v = find_var(mt, pname);
                if (v) v->elem_type = param_elem_tid;
            }
        }

        pi++;
        param = (AstNamedNode*)param->next;
    }

    // Set proc flag based on function type
    bool saved_in_proc = mt->in_proc;
    bool saved_current_func_can_raise = mt->current_func_can_raise;

    // Set variadic body flag for restore_vargs in return/raise paths
    bool saved_in_variadic_body = mt->in_variadic_body;
    mt->in_variadic_body = is_variadic;
    mt->in_proc = (fn_as_node->node_type == AST_NODE_PROC);
    mt->current_func_can_raise = fn_type && fn_type->can_raise;

    // P4-3.4: Set native return type for transpile_return() to use
    TypeId saved_native_return_tid = mt->native_return_tid;
    Type* saved_current_return_type = mt->current_return_type;
    mt->current_return_type = fn_type ? fn_type->returned : NULL;
    mt->native_return_tid = LMD_TYPE_ANY;
    if (native_return) {
        NativeFuncInfo* nfi_ctx = find_native_func_info(mt, name_buf->str);
        if (nfi_ctx && nfi_ctx->return_type != LMD_TYPE_ANY) {
            mt->native_return_tid = nfi_ctx->return_type;
        }
    }

    log_debug("mir: params bound, transpiling body of '%s'", name_buf->str);

    // Reset block_returned for this function's compilation scope.
    // Prevents the flag from leaking from one function's body into the next.
    bool saved_block_returned = mt->block_returned;
    mt->block_returned = false;

    // Save the method owner for the surrounding compilation context.
    TypeObject* saved_method_owner = mt->method_owner;

    // ===== TCO Setup =====
    // Check if this function is eligible for tail call optimization.
    // If so, wrap the body in a loop: tco_label -> body -> (tail calls jump back)
    // with an iteration counter to prevent infinite loops.
    bool use_tco = should_use_tco(fn_node);
    AstFuncNode* saved_tco_func = mt->tco_func;
    MIR_label_t saved_tco_label = mt->tco_label;
    MIR_reg_t saved_tco_count_reg = mt->tco_count_reg;
    bool saved_tail_position = mt->in_tail_position;

    if (use_tco) {
        log_debug("mir: TCO enabled for '%s'", name_buf->str);
        mt->tco_func = fn_node;

        // Create iteration counter register, init to 0
        mt->tco_count_reg = new_reg(mt, "tco_count", MIR_T_I64);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->ctx, 0)));

        // Create the TCO loop label
        mt->tco_label = MIR_new_label(mt->ctx);
        emit_insn(mt, mt->tco_label);

        // Increment counter: tco_count = tco_count + 1
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD,
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->ctx, 1)));

        // Guard: if tco_count > LAMBDA_TCO_MAX_ITERATIONS, call overflow error
        MIR_label_t ok_label = MIR_new_label(mt->ctx);
        emit_insn(mt, MIR_new_insn(mt->ctx, MIR_BLE, MIR_new_label_op(mt->ctx, ok_label),
            MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
            MIR_new_int_op(mt->ctx, LAMBDA_TCO_MAX_ITERATIONS)));

        // Overflow path: call lambda_stack_overflow_error(func_name)
        MIR_reg_t fn_name_ptr = emit_load_string_literal(mt, name_buf->str);
        emit_call_void_1(mt, "lambda_stack_overflow_error",
            MIR_T_P, MIR_new_reg_op(mt->ctx, fn_name_ptr));

        // Emit a ret after overflow call (unreachable but needed for MIR validation)
        if (ret_type == MIR_T_D) {
            emit_function_return(mt, MIR_new_double_op(mt->ctx, 0.0));
        } else {
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_function_return(mt, MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL));
        }

        // Continue label for normal execution
        emit_insn(mt, ok_label);

        // Body is in tail position for TCO
        mt->in_tail_position = true;
    }

    // Transpile body
    MIR_reg_t body_result;
    if (native_return) {
        // P4-3.3: Native return — produce unboxed native value
        NativeFuncInfo* nfi_body = find_native_func_info(mt, name_buf->str);
        body_result = transpile_expr(mt, fn_node->body);
        // P4-3.4: For procs, explicit `return` statements already emit native
        // MIR_new_ret_insn via transpile_return(). The body_result here is just
        // the ret_dummy value and may have an incompatible MIR type. Skip the
        // post-body unbox/box when block_returned is set.
        if (!mt->block_returned) {
            TypeId body_tid = get_effective_type(mt, fn_node->body);
            if (body_tid != nfi_body->return_type) {
                if (body_tid == LMD_TYPE_ANY || body_tid == LMD_TYPE_NULL) {
                    // Body returned boxed Item, unbox to native
                    body_result = emit_unbox(mt, body_result, nfi_body->return_type);
                } else {
                    // Different native type — box then unbox
                    MIR_reg_t boxed = emit_box(mt, body_result, body_tid);
                    body_result = emit_unbox(mt, boxed, nfi_body->return_type);
                }
            }
        } else {
            // Proc already returned — emit type-correct dummy for trailing ret
            if (nfi_body->return_mir == MIR_T_D) {
                body_result = new_reg(mt, "body_fallthrough", MIR_T_D);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, body_result),
                    MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                body_result = new_reg(mt, "body_fallthrough", MIR_T_I64);
                emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, body_result),
                    MIR_new_int_op(mt->ctx, 0)));
            }
        }
    } else {
        // Standard: box result to Item
        body_result = transpile_box_item(mt, fn_node->body);
        if (!mt->block_returned) {
            body_result = emit_coerce_boxed_to_declared(mt, body_result, mt->current_return_type);
        }
    }

    // Return result
    // Always emit the trailing ret — MIR needs a ret on every code path.
    // For TCO functions, this serves as the non-tail-call exit path.
    // For functions where body already returned, it's dead code (MIR removes it).
    // Restore vargs before returning from variadic function
    if (is_variadic) {
        MIR_reg_t saved_vargs = MIR_reg(mt->ctx, "_saved_vargs", func);
        emit_call_void_1(mt, "restore_vargs", MIR_T_P, MIR_new_reg_op(mt->ctx, saved_vargs));
    }
    if (is_method && !is_closure && mt->self_reg) {
        TypeObject* owner = mt->method_owner;
        ShapeEntry* field = owner ? owner->shape : NULL;
        while (field) {
            if (field->name) {
                char field_name[128];
                snprintf(field_name, sizeof(field_name), "%.*s",
                    (int)field->name->length, field->name->str);
                MirVarEntry* var = find_var(mt, field_name);
                if (var) {
                    MIR_reg_t name_ptr = emit_load_string_literal(mt, field->name->str);
                    MIR_reg_t sym_ptr = emit_call_2(mt, "heap_create_symbol", MIR_T_P,
                        MIR_T_P, MIR_new_reg_op(mt->ctx, name_ptr),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)field->name->length));
                    MIR_reg_t key_boxed = emit_box_symbol(mt, sym_ptr);
                    MIR_reg_t boxed_val = emit_box(mt, var->reg, var->type_id);
                    // method field locals are snapshots; write them back before returning to preserve mutations.
                    emit_call_void_3(mt, "fn_map_set",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->self_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key_boxed),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, boxed_val));
                }
            }
            field = field->next;
        }
    }
    // Conservative exit-edge analysis may reserve states for branches whose
    // lowering proves they cannot suspend. MIR still requires every dispatcher
    // target to be present, even though those states can never be stored.
    for (int state = mt->async_next_state + 1; state <= mt->async_state_count; state++) {
        emit_label(mt, mt->async_state_labels[state]);
    }
    async_complete_frame(mt);
    emit_function_return(mt, MIR_new_reg_op(mt->ctx, body_result));
    finish_function_epilogue(mt);
    finalize_gc_root_publication(mt, name_buf->str);
    finalize_side_root_frame(mt);
    finalize_async_frame_enter(mt);

    pop_scope(mt);

    MIR_finish_func(mt->ctx);
    mt->current_return_type = saved_current_return_type;

    // Every raw Lambda function uses the widened JIT ABI. Publish one boxed,
    // single-lane wrapper for dynamic calls, methods, imports, and host bridges.
    NativeFuncInfo* wrapper_nfi = generate_native
        ? find_native_func_info(mt, name_buf->str) : NULL;
    emit_boxed_abi_wrapper(mt, name_buf->str, fn_node, wrapper_nfi, is_method);

    // Restore function context
    mt->em.func_item = saved_func_item;
    mt->em.func = saved_func;
    mt->consts_reg = saved_consts_reg;
    mt->gc_reg = saved_gc_reg;
    em_frame_restore(&mt->em, saved_frame);
    mt->in_user_func = saved_in_user_func;
    mt->func_body = saved_func_body;
    mem_free(mt->async_state_labels);
    mem_free(mt->async_spills);
    mt->in_async_proc = saved_in_async_proc;
    mt->emitting_async_call = saved_emitting_async_call;
    mt->async_call_state = saved_async_call_state;
    mt->async_call_spill_count = saved_async_call_spill_count;
    mt->async_call_resume_emitted = saved_async_call_resume_emitted;
    mt->async_call_target = saved_async_call_target;
    mt->async_frame_reg = saved_async_frame_reg;
    mt->async_scope_base_reg = saved_async_scope_base_reg;
    mt->async_state_labels = saved_async_state_labels;
    mt->async_state_count = saved_async_state_count;
    mt->async_next_state = saved_async_next_state;
    mt->async_next_slot = saved_async_next_slot;
    mt->async_spills = saved_async_spills;
    mt->async_spill_count = saved_async_spill_count;
    mt->async_spill_capacity = saved_async_spill_capacity;
    mt->async_tracking = saved_async_tracking;
    mt->async_tracking_suppressed = saved_async_tracking_suppressed;
    mt->current_closure = saved_closure;
    mt->env_reg = saved_env_reg;
    mt->in_proc = saved_in_proc;
    mt->current_func_can_raise = saved_current_func_can_raise;
    mt->in_variadic_body = saved_in_variadic_body;
    mt->native_return_tid = saved_native_return_tid;
    mt->block_returned = saved_block_returned;
    mt->method_owner = saved_method_owner;
    mt->self_reg = saved_self_reg;
    mt->in_pipe = saved_in_pipe;
    mt->pipe_item_reg = saved_pipe_item_reg;
    mt->tco_func = saved_tco_func;
    mt->tco_label = saved_tco_label;
    mt->tco_count_reg = saved_tco_count_reg;
    mt->in_tail_position = saved_tail_position;

    strbuf_free(name_buf);
}

// ============================================================================
// P4-3.2: Mutation analysis — check if a variable is ever index-assigned
// in a given AST subtree. Used to determine if elem_type can be safely set.
// Returns true if arr[i] = val is found where arr matches var_name.
// Does NOT recurse into nested function/procedure definitions (separate scope).
// ============================================================================
static bool has_index_mutation(const char* var_name, AstNode* node) {
    while (node) {
        // Check if this node is an index-assignment targeting our variable
        if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
            AstNode* obj = ca->object;
            while (obj && obj->node_type == AST_NODE_PRIMARY)
                obj = ((AstPrimaryNode*)obj)->expr;
            if (obj && obj->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident = (AstIdentNode*)obj;
                if (ident->name && ident->name->len > 0 &&
                    strncmp(var_name, ident->name->chars, ident->name->len) == 0 &&
                    var_name[ident->name->len] == '\0') {
                    return true;
                }
            }
        }

        // Recurse into child nodes that can contain statements
        switch (node->node_type) {
        case AST_NODE_IF_EXPR: {
            AstIfNode* if_node = (AstIfNode*)node;
            if (if_node->then && has_index_mutation(var_name, if_node->then)) return true;
            if (if_node->otherwise && has_index_mutation(var_name, if_node->otherwise)) return true;
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            if (wh->body && has_index_mutation(var_name, wh->body)) return true;
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* for_node = (AstForNode*)node;
            if (for_node->then && has_index_mutation(var_name, for_node->then)) return true;
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            AstMatchArm* arm = match->first_arm;
            while (arm) {
                if (arm->body && has_index_mutation(var_name, arm->body)) return true;
                arm = (AstMatchArm*)arm->next;
            }
            break;
        }
        case AST_NODE_CONTENT:
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare && has_index_mutation(var_name, list->declare)) return true;
            if (list->item && has_index_mutation(var_name, list->item)) return true;
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            if (let_node->declare && has_index_mutation(var_name, let_node->declare)) return true;
            break;
        }
        // Recurse into function bodies (module-level vars can be mutated from within)
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn = (AstFuncNode*)node;
            if (fn->body && has_index_mutation(var_name, fn->body)) return true;
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
    return false;
}

// ============================================================================
// Forward-declaration pass: create MIR forward declarations for ALL functions
// BEFORE any function bodies are transpiled. This allows forward references
// between functions (e.g., first() calling second() defined later).
// ============================================================================

static void prepass_forward_declare(MirTranspiler* mt, AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            StrBuf* name_buf = strbuf_new_cap(64);
            write_fn_name(name_buf, fn_node, NULL);

            // Create a forward declaration at module level
            MIR_item_t fwd = MIR_new_forward(mt->ctx, name_buf->str);
            register_local_func(mt, name_buf->str, fwd);

            StrBuf* wrapper_fwd_name = strbuf_new_cap(64);
            write_fn_name_ex(wrapper_fwd_name, fn_node, NULL, "_b");
            MIR_item_t wrapper_fwd = MIR_new_forward(mt->ctx, wrapper_fwd_name->str);
            register_local_func(mt, wrapper_fwd_name->str, wrapper_fwd);
            log_debug("mir: forward-declared ABI wrapper '%s'", wrapper_fwd_name->str);
            strbuf_free(wrapper_fwd_name);

            // Also register by raw name for fallback lookup
            if (fn_node->name) {
                char raw_name[128];
                snprintf(raw_name, sizeof(raw_name), "%.*s",
                    (int)fn_node->name->len, fn_node->name->chars);
                if (!find_local_func(mt, raw_name)) {
                    register_local_func(mt, raw_name, fwd);
                }
            }

            // Phase 4: Forward-declare the boxed wrapper (_b) for functions that
            // will generate dual versions. Also pre-register NativeFuncInfo so that
            // call sites in other functions can use the native calling convention
            // even when the target function hasn't been transpiled yet.
            {
                bool is_closure = (fn_node->captures != nullptr);
                bool is_method = false; // in prepass, method_owner is not set
                AstNode* fn_as = (AstNode*)fn_node;
                TypeFunc* ft = (fn_as->type && fn_as->type->type_id == LMD_TYPE_FUNC)
                    ? (TypeFunc*)fn_as->type : NULL;
                bool is_variadic = ft && ft->is_variadic;
                bool needs_task_context = fn_as->node_type == AST_NODE_PROC &&
                    fn_node->analysis && fn_node->analysis->needs_task_context;
                if (!needs_task_context && !is_closure && !is_variadic && !is_method) {
                    // Resolve all param types (matching transpile_func_def logic)
                    TypeId fwd_param_types[16];
                    int fwd_param_count = 0;
                    bool has_native = false;
                    bool is_proc = (fn_as->node_type == AST_NODE_PROC);
                    {
                        TypeParam* tp = ft ? ft->param : NULL;
                        AstNamedNode* p = fn_node->param;
                        while (p && fwd_param_count < 16) {
                            TypeId tid = resolve_declared_param_type(p, tp);
                            fwd_param_types[fwd_param_count] = tid;
                            fwd_param_count++;
                            tp = tp ? tp->next : NULL;
                            p = (AstNamedNode*)p->next;
                        }
                    }
                    // Batch-infer types for all untyped params in a single body walk
                    infer_param_types_batched(fn_node, is_proc, fwd_param_types, fwd_param_count);
                    bool has_typed_array_param = false;
                    for (int i = 0; i < fwd_param_count; i++) {
                        if (fwd_param_types[i] == LMD_TYPE_ARRAY_NUM) has_typed_array_param = true;
                        if (mir_is_native_param_type(fwd_param_types[i])) has_native = true;
                    }
                    if (has_typed_array_param) has_native = false;
                    // Cache inferred param types so transpile_func_def can skip re-inference
                    {
                        InferCacheEntry ice;
                        memset(&ice, 0, sizeof(ice));
                        ice.fn = fn_node;
                        ice.param_count = fwd_param_count;
                        for (int i = 0; i < fwd_param_count; i++) {
                            ice.param_types[i] = fwd_param_types[i];
                        }
                        hashmap_set(mt->infer_cache, &ice);
                    }
                    if (has_native) {
                        // Pre-register NativeFuncInfo so call sites can use native ABI
                        NativeFuncInfo nfi;
                        memset(&nfi, 0, sizeof(nfi));
                        nfi.param_count = fwd_param_count;
                        nfi.has_native = true;
                        for (int i = 0; i < fwd_param_count; i++) {
                            nfi.param_types[i] = fwd_param_types[i];
                            nfi.param_mir[i] = type_to_mir(fwd_param_types[i]);
                        }
                        // P4-3.3: Infer return type
                        TypeId fwd_ret_tid = infer_return_type(fn_node);
                        nfi.return_type = fwd_ret_tid;
                        nfi.return_mir = (fwd_ret_tid != LMD_TYPE_ANY) ? type_to_mir(fwd_ret_tid) : MIR_T_I64;
                        register_native_func_info(mt, name_buf->str, &nfi);
                        log_debug("mir: pre-registered NativeFuncInfo for '%s', return=%d",
                            name_buf->str, fwd_ret_tid);
                    }
                    // P4-3.4: Also enable native version when return type alone is native
                    if (!has_native && !has_typed_array_param) {
                        TypeId fwd_ret_tid = infer_return_type(fn_node);
                        if (mir_is_native_scalar_value_type(fwd_ret_tid)) {
                            NativeFuncInfo nfi;
                            memset(&nfi, 0, sizeof(nfi));
                            nfi.param_count = fwd_param_count;
                            nfi.has_native = true;
                            for (int i = 0; i < fwd_param_count; i++) {
                                nfi.param_types[i] = fwd_param_types[i];
                                nfi.param_mir[i] = type_to_mir(fwd_param_types[i]);
                            }
                            nfi.return_type = fwd_ret_tid;
                            nfi.return_mir = type_to_mir(fwd_ret_tid);
                            register_native_func_info(mt, name_buf->str, &nfi);
                            log_debug("mir: pre-registered NativeFuncInfo for '%s' (return-only), return=%d",
                                name_buf->str, fwd_ret_tid);
                        }
                    }
                }
                NativeFuncInfo* fwd_nfi = find_native_func_info(mt,
                    name_buf->str);
                bool native_return = fwd_nfi &&
                    fwd_nfi->return_type != LMD_TYPE_ANY;
                int lane_kind = native_return
                    ? ((ft && ft->can_raise)
                        ? RETURN_LANE_ERROR : RETURN_LANE_NONE)
                    : RETURN_LANE_SCALAR;
                MirScalarReturnMode scalar_mode = lane_kind == RETURN_LANE_ERROR
                    ? MIR_SCALAR_RETURN_DYNAMIC
                    : lane_kind == RETURN_LANE_SCALAR
                        ? infer_boxed_return_mode(mt, fn_node)
                        : MIR_SCALAR_RETURN_NONE;
                uint8_t lane_mask = scalar_mode != MIR_SCALAR_RETURN_NONE
                    ? (lane_kind == RETURN_LANE_ERROR
                        ? FN_RETURN_HOME_ERROR : FN_RETURN_HOME_NORMAL)
                    : 0;
                FnVariantAnalysis* variant = analyze_lambda_mir_variants(
                    fn_node, fwd_nfi, scalar_mode, lane_mask);
                register_local_func_contract(mt, name_buf->str, fwd, variant);
                if (fn_node->name) {
                    char raw_name[128];
                    snprintf(raw_name, sizeof(raw_name), "%.*s",
                        (int)fn_node->name->len, fn_node->name->chars);
                    LocalFuncEntry* raw_entry = find_local_func_entry(mt,
                        raw_name);
                    if (raw_entry && raw_entry->func_item == fwd) {
                        register_local_func_contract(mt, raw_name, fwd, variant);
                    }
                }
            }

            strbuf_free(name_buf);

            // Recurse into function body to find nested function definitions
            if (fn_node->body) prepass_forward_declare(mt, fn_node->body);
            break;
        }
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_forward_declare(mt, list->declare);
            prepass_forward_declare(mt, list->item);
            break;
        }
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_forward_declare(mt, list->declare);
            prepass_forward_declare(mt, list->item);
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            prepass_forward_declare(mt, let_node->declare);
            break;
        }
        case AST_NODE_OBJECT_TYPE: {
            // forward-declare methods inside the object type
            AstObjectTypeNode* obj = (AstObjectTypeNode*)node;
            if (obj->methods) prepass_forward_declare(mt, obj->methods);
            break;
        }
        case AST_NODE_ASSIGN:
        case AST_NODE_KEY_EXPR:
        case AST_NODE_PARAM:
        case AST_NODE_NAMED_ARG: {
            AstNamedNode* named = (AstNamedNode*)node;
            if (named->as) prepass_forward_declare(mt, named->as);
            break;
        }
        case AST_NODE_PRIMARY: {
            AstPrimaryNode* pri = (AstPrimaryNode*)node;
            if (pri->expr) prepass_forward_declare(mt, pri->expr);
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            if (call->function) prepass_forward_declare(mt, call->function);
            if (call->argument) prepass_forward_declare(mt, call->argument);
            break;
        }
        case AST_NODE_BINARY:
        case AST_NODE_PIPE: {
            AstBinaryNode* bi = (AstBinaryNode*)node;
            if (bi->left) prepass_forward_declare(mt, bi->left);
            if (bi->right) prepass_forward_declare(mt, bi->right);
            break;
        }
        case AST_NODE_UNARY:
        case AST_NODE_SPREAD: {
            AstUnaryNode* un = (AstUnaryNode*)node;
            if (un->operand) prepass_forward_declare(mt, un->operand);
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* if_node = (AstIfNode*)node;
            if (if_node->cond) prepass_forward_declare(mt, if_node->cond);
            if (if_node->then) prepass_forward_declare(mt, if_node->then);
            if (if_node->otherwise) prepass_forward_declare(mt, if_node->otherwise);
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* for_node = (AstForNode*)node;
            if (for_node->loop) prepass_forward_declare(mt, for_node->loop);
            if (for_node->let_clause) prepass_forward_declare(mt, for_node->let_clause);
            if (for_node->where) prepass_forward_declare(mt, for_node->where);
            if (for_node->then) prepass_forward_declare(mt, for_node->then);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            if (match->scrutinee) prepass_forward_declare(mt, match->scrutinee);
            if (match->first_arm) prepass_forward_declare(mt, (AstNode*)match->first_arm);
            break;
        }
        case AST_NODE_MATCH_ARM: {
            AstMatchArm* arm = (AstMatchArm*)node;
            if (arm->body) prepass_forward_declare(mt, arm->body);
            break;
        }
        case AST_NODE_LOOP: {
            AstLoopNode* loop = (AstLoopNode*)node;
            if (loop->as) prepass_forward_declare(mt, loop->as);
            break;
        }
        case AST_NODE_DECOMPOSE: {
            AstDecomposeNode* dec = (AstDecomposeNode*)node;
            if (dec->as) prepass_forward_declare(mt, dec->as);
            break;
        }
        case AST_NODE_RAISE_STAM:
        case AST_NODE_RAISE_EXPR: {
            AstRaiseNode* raise = (AstRaiseNode*)node;
            if (raise->value) prepass_forward_declare(mt, raise->value);
            break;
        }
        case AST_NODE_RETURN_STAM: {
            AstReturnNode* ret = (AstReturnNode*)node;
            if (ret->value) prepass_forward_declare(mt, ret->value);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            if (wh->cond) prepass_forward_declare(mt, wh->cond);
            if (wh->body) prepass_forward_declare(mt, wh->body);
            break;
        }
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* assign = (AstAssignStamNode*)node;
            if (assign->value) prepass_forward_declare(mt, assign->value);
            break;
        }
        case AST_NODE_INDEX_ASSIGN_STAM:
        case AST_NODE_MEMBER_ASSIGN_STAM: {
            AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
            if (ca->object) prepass_forward_declare(mt, ca->object);
            if (ca->key) prepass_forward_declare(mt, ca->key);
            if (ca->value) prepass_forward_declare(mt, ca->value);
            break;
        }
        case AST_NODE_INDEX_EXPR:
        case AST_NODE_MEMBER_EXPR: {
            AstFieldNode* field = (AstFieldNode*)node;
            if (field->object) prepass_forward_declare(mt, field->object);
            if (field->field) prepass_forward_declare(mt, field->field);
            break;
        }
        case AST_NODE_ARRAY: {
            AstArrayNode* arr = (AstArrayNode*)node;
            if (arr->item) prepass_forward_declare(mt, arr->item);
            break;
        }
        case AST_NODE_MAP: {
            AstMapNode* map = (AstMapNode*)node;
            if (map->item) prepass_forward_declare(mt, map->item);
            break;
        }
        case AST_NODE_ELEMENT: {
            AstElementNode* elmt = (AstElementNode*)node;
            if (elmt->item) prepass_forward_declare(mt, elmt->item);
            if (elmt->content) prepass_forward_declare(mt, elmt->content);
            break;
        }
        case AST_NODE_VIEW: {
            AstViewNode* view = (AstViewNode*)node;
            if (view->body) prepass_forward_declare(mt, view->body);
            AstEventHandler* handler = view->handler;
            while (handler) {
                if (handler->body) prepass_forward_declare(mt, handler->body);
                handler = handler->next_handler;
            }
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
}

// ============================================================================
// Pre-pass: create BSS items for module-level let bindings
// This allows user-defined functions to access module-level variables
// through BSS memory instead of MIR registers (which are function-local).
// Only scans TOP-LEVEL declarations (not nested in functions).
// ============================================================================

static void prepass_create_global_vars(MirTranspiler* mt, AstNode* node) {
    while (node) {
        if (node->node_type == AST_NODE_LET_STAM || node->node_type == AST_NODE_PUB_STAM ||
            node->node_type == AST_NODE_VAR_STAM) {
            AstLetNode* let_node = (AstLetNode*)node;
            AstNode* decl = let_node->declare;
            while (decl) {
                if (decl->node_type == AST_NODE_ASSIGN) {
                    AstNamedNode* asn = (AstNamedNode*)decl;
                    char name[128];
                    snprintf(name, sizeof(name), "%.*s", (int)asn->name->len, asn->name->chars);

                    // Create BSS for this variable (8 bytes for boxed Item)
                    char bss_name[140];
                    snprintf(bss_name, sizeof(bss_name), "_gvar_%s", name);
                    MIR_item_t bss = MIR_new_bss(mt->ctx, bss_name, 8);

                    TypeId tid = decl->type ? decl->type->type_id : LMD_TYPE_ANY;
                    if (tid == LMD_TYPE_ANY && asn->as && asn->as->type) {
                        tid = asn->as->type->type_id;
                    }

                    GlobalVarEntry entry;
                    memset(&entry, 0, sizeof(entry));
                    snprintf(entry.name, sizeof(entry.name), "%s", name);
                    entry.bss_item = bss;
                    entry.type_id = tid;
                    entry.mir_type = type_to_mir(tid);
                    hashmap_set(mt->global_vars, &entry);

                    // Also create BSS for the error variable (e.g. pub val^err = ...)
                    if (asn->error_name) {
                        char err_name[128];
                        snprintf(err_name, sizeof(err_name), "%.*s",
                            (int)asn->error_name->len, asn->error_name->chars);
                        char err_bss_name[140];
                        snprintf(err_bss_name, sizeof(err_bss_name), "_gvar_%s", err_name);
                        MIR_item_t err_bss = MIR_new_bss(mt->ctx, err_bss_name, 8);
                        GlobalVarEntry err_entry;
                        memset(&err_entry, 0, sizeof(err_entry));
                        snprintf(err_entry.name, sizeof(err_entry.name), "%s", err_name);
                        err_entry.bss_item = err_bss;
                        err_entry.type_id = LMD_TYPE_ANY;
                        err_entry.mir_type = MIR_T_I64;
                        hashmap_set(mt->global_vars, &err_entry);
                    }
                } else if (decl->node_type == AST_NODE_DECOMPOSE) {
                    AstDecomposeNode* dec = (AstDecomposeNode*)decl;
                    for (int i = 0; i < dec->name_count; i++) {
                        String* name_str = dec->names[i];
                        char name[128];
                        snprintf(name, sizeof(name), "%.*s", (int)name_str->len, name_str->chars);
                        char bss_name[140];
                        snprintf(bss_name, sizeof(bss_name), "_gvar_%s", name);
                        MIR_item_t bss = MIR_new_bss(mt->ctx, bss_name, 8);

                        GlobalVarEntry entry;
                        memset(&entry, 0, sizeof(entry));
                        snprintf(entry.name, sizeof(entry.name), "%s", name);
                        entry.bss_item = bss;
                        entry.type_id = LMD_TYPE_ANY;
                        entry.mir_type = MIR_T_I64;
                        hashmap_set(mt->global_vars, &entry);
                    }
                }
                decl = decl->next;
            }
        } else if (node->node_type == AST_NODE_CONTENT) {
            // Recurse into content blocks to find nested let statements
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_create_global_vars(mt, list->declare);
            prepass_create_global_vars(mt, list->item);
        }
        node = node->next;
    }
}

// ============================================================================
// Pre-pass: compile string/symbol pattern definitions
// Walks AST to find pattern defs and compiles them to RE2 regex,
// storing results in the script's type_list for runtime const_pattern() access.
// ============================================================================

static void prepass_compile_patterns(MirTranspiler* mt, AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_STRING_PATTERN:
        case AST_NODE_SYMBOL_PATTERN: {
            AstPatternDefNode* pattern_def = (AstPatternDefNode*)node;
            TypePattern* pattern_type = (TypePattern*)pattern_def->type;

            // compile pattern to regex if not already compiled
            if (pattern_type->re2 == nullptr && pattern_def->as != nullptr) {
                const char* error_msg = nullptr;
                TypePattern* compiled = compile_pattern_ast(mt->script_pool, pattern_def->as,
                    pattern_def->is_symbol, &error_msg);
                if (compiled) {
                    // copy compiled info to existing type
                    pattern_type->re2 = compiled->re2;
                    pattern_type->source = compiled->source;
                    // add to type_list for runtime access via const_pattern()
                    arraylist_append(mt->type_list, pattern_type);
                    pattern_type->pattern_index = mt->type_list->length - 1;
                    log_debug("mir: compiled pattern '%.*s' to regex, index=%d",
                        (int)pattern_def->name->len, pattern_def->name->chars, pattern_type->pattern_index);
                } else {
                    log_error("mir: failed to compile pattern '%.*s': %s",
                        (int)pattern_def->name->len, pattern_def->name->chars,
                        error_msg ? error_msg : "unknown error");
                }
            }
            break;
        }
        case AST_NODE_CONTENT:
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_compile_patterns(mt, list->declare);
            prepass_compile_patterns(mt, list->item);
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            prepass_compile_patterns(mt, let_node->declare);
            break;
        }
        case AST_NODE_OBJECT_TYPE: {
            // recurse into methods for pattern compilation
            AstObjectTypeNode* obj = (AstObjectTypeNode*)node;
            if (obj->methods) prepass_compile_patterns(mt, obj->methods);
            break;
        }
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            if (fn_node->body) prepass_compile_patterns(mt, fn_node->body);
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
}

// ============================================================================
// View/Edit template body compilation
// Compiles a view/edit body as a MIR function: Item _view_N(Item model) -> Item
// ============================================================================

static void transpile_view_def(MirTranspiler* mt, AstViewNode* view) {
    // generate unique function name
    char name_buf[64];
    snprintf(name_buf, sizeof(name_buf), "_view_%d", mt->view_counter++);

    log_debug("mir: transpile_view_def '%s' (is_edit=%d)", name_buf, view->is_edit);

    // save outer function context
    MIR_item_t saved_func_item = mt->em.func_item;
    MIR_func_t saved_func = mt->em.func;
    MIR_reg_t saved_consts_reg = mt->consts_reg;
    MIR_reg_t saved_gc_reg = mt->gc_reg;
    MIR_reg_t saved_tl_reg = mt->type_list_reg;
    MirFrameState saved_frame = em_frame_suspend(&mt->em);
    bool saved_in_user_func = mt->in_user_func;
    AstNode* saved_func_body = mt->func_body;
    bool saved_block_returned = mt->block_returned;

    mt->in_user_func = true;
    mt->block_returned = false;
    mt->func_body = view->body;

    // create MIR function: Item _view_N(Item _model) -> Item (i64)
    MIR_type_t ret_type = MIR_T_I64;
    MIR_var_t params[1] = {{MIR_T_I64, raw_strdup("_model"), 0}}; // RAWALLOC_OK: MIR manages param name lifetime
    char* param_name_copy = (char*)params[0].name;

    MIR_item_t func_item = MIR_new_func_arr(mt->ctx, name_buf, 1, &ret_type, 1, params);
    MIR_func_t func = MIR_get_item_func(mt->ctx, func_item);
    mt->em.func_item = func_item;
    mt->em.func = func;
    raw_free(param_name_copy);

    // load runtime context from _lambda_rt
    MIR_item_t rt_import = MIR_new_import(mt->ctx, "_lambda_rt");
    MIR_reg_t rt_addr = new_reg(mt, "rt_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_addr),
        MIR_new_ref_op(mt->ctx, rt_import)));
    MIR_reg_t runtime_reg = new_reg(mt, "runtime", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, runtime_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, rt_addr, 0, 1)));

    emit_load_module_consts(mt);

    // load type_list from per-module BSS
    MIR_reg_t mod_tl_bss_addr = new_reg(mt, "mod_tl_bss", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mod_tl_bss_addr),
        MIR_new_ref_op(mt->ctx, mt->type_list_bss)));
    mt->type_list_reg = new_reg(mt, "type_list", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->type_list_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, mod_tl_bss_addr, 0, 1)));

    // load gc heap pointer
    MIR_reg_t heap_reg = new_reg(mt, "heap_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, heap_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 64, runtime_reg, 0, 1)));
    mt->gc_reg = new_reg(mt, "gc_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->gc_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, heap_reg, 0, 1)));

    mt->em.frame.runtime = runtime_reg;
    mt->em.frame.return_type = ret_type;
    emit_jit_root_frame_enter(mt);
    begin_function_epilogue(mt, ret_type);
    mt->em.frame.plan.debug_name = name_buf;
    emit_number_frame_enter(mt);

    // register as local function
    register_local_func(mt, name_buf, func_item);

    // determine template reference string for state store keying
    // use template name if available, otherwise intern the generated function name
    // via name_pool so the pointer persists after this stack frame returns
    const char* tmpl_ref = view->name ? view->name->chars
        : name_pool_create_len(mt->name_pool, name_buf, strlen(name_buf))->chars;

    // save outer view context
    bool saved_in_view_context = mt->in_view_context;
    bool saved_in_view_handler = mt->in_view_handler;
    bool saved_view_is_edit = mt->view_is_edit;
    MIR_reg_t saved_view_model_reg = mt->view_model_reg;
    const char* saved_view_template_ref = mt->view_template_ref;

    mt->in_view_context = true;
    mt->in_view_handler = false;
    mt->view_is_edit = view->is_edit;
    mt->view_template_ref = tmpl_ref;

    // set up parameter scope: ~ resolves to the model parameter
    push_scope(mt);
    MIR_reg_t model_reg = MIR_reg(mt->ctx, "_model", func);
    mt->view_model_reg = model_reg;

    // initialize state variables from the central state store
    // For each state declaration, call tmpl_state_get_or_init(model, tmpl_ref, name, default)
    // and bind the result to a local register as a state var.
    for (AstStateEntry* se = view->state; se; se = se->next_state) {
        if (!se->name) continue;
        const char* state_name = se->name->chars;
        log_debug("mir: view state init: %s", state_name);

        // transpile default value expression
        MIR_reg_t default_val;
        if (se->value) {
            default_val = transpile_box_item(mt, se->value);
        } else {
            // no default: use ItemNull
            default_val = new_reg(mt, "null_default", MIR_T_I64);
            uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
            emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, default_val),
                MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
        }

        // call tmpl_state_get_or_init(model_item, template_ref, state_name, default)
        // signature: Item(Item, const char*, const char*, Item)
        MIR_reg_t state_val = emit_call_4(mt, "tmpl_state_get_or_init",
            MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, model_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)tmpl_ref),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)state_name),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, default_val));

        // bind as state variable (boxed Item, type ANY)
        set_state_var(mt, state_name, state_val, MIR_T_I64, LMD_TYPE_ANY, state_name);
    }

    // transpile the body expression
    MIR_reg_t body_result = transpile_box_item(mt, view->body);

    // emit return
    emit_function_return(mt, MIR_new_reg_op(mt->ctx, body_result));
    finish_function_epilogue(mt);
    finalize_gc_root_publication(mt, name_buf);
    finalize_side_root_frame(mt);

    pop_scope(mt);
    MIR_finish_func(mt->ctx);

    // restore outer view context
    mt->in_view_context = saved_in_view_context;
    mt->in_view_handler = saved_in_view_handler;
    mt->view_is_edit = saved_view_is_edit;
    mt->view_model_reg = saved_view_model_reg;
    mt->view_template_ref = saved_view_template_ref;

    // restore outer function context
    mt->em.func_item = saved_func_item;
    mt->em.func = saved_func;
    mt->consts_reg = saved_consts_reg;
    mt->gc_reg = saved_gc_reg;
    mt->type_list_reg = saved_tl_reg;
    em_frame_restore(&mt->em, saved_frame);
    mt->in_user_func = saved_in_user_func;
    mt->func_body = saved_func_body;
    mt->block_returned = saved_block_returned;
}

// ============================================================================
// Event handler compilation
// Compiles an event handler as a MIR function: Item _handler_N_M(Item model, Item event)
// Handlers use procedural semantics. State variables are loaded from the
// central state store at function entry and written back on assignment.
// ============================================================================

static void transpile_handler_def(MirTranspiler* mt, AstEventHandler* handler,
                                  AstViewNode* view, const char* view_func_name,
                                  int handler_idx) {
    // generate unique handler function name
    char handler_name[64];
    snprintf(handler_name, sizeof(handler_name), "%s_h%d", view_func_name, handler_idx);

    const char* event_name = handler->event ? handler->event->chars : "unknown";
    log_debug("mir: transpile_handler_def '%s' event='%s'", handler_name, event_name);

    // save outer function context
    MIR_item_t saved_func_item = mt->em.func_item;
    MIR_func_t saved_func = mt->em.func;
    MIR_reg_t saved_consts_reg = mt->consts_reg;
    MIR_reg_t saved_gc_reg = mt->gc_reg;
    MIR_reg_t saved_tl_reg = mt->type_list_reg;
    MirFrameState saved_frame = em_frame_suspend(&mt->em);
    bool saved_in_user_func = mt->in_user_func;
    bool saved_in_proc = mt->in_proc;
    AstNode* saved_func_body = mt->func_body;
    bool saved_block_returned = mt->block_returned;
    bool saved_in_view_context = mt->in_view_context;
    bool saved_in_view_handler = mt->in_view_handler;
    bool saved_view_is_edit = mt->view_is_edit;
    MIR_reg_t saved_view_model_reg = mt->view_model_reg;
    const char* saved_view_template_ref = mt->view_template_ref;

    mt->in_user_func = true;
    mt->in_proc = true;  // handlers are procedural
    mt->block_returned = false;
    mt->func_body = handler->body;

    // create MIR function: Item _handler_N_M(Item _model, Item _event) -> Item (i64)
    MIR_type_t ret_type = MIR_T_I64;
    MIR_var_t params[2] = {{MIR_T_I64, raw_strdup("_model"), 0}, {MIR_T_I64, raw_strdup("_event"), 0}}; // RAWALLOC_OK: MIR manages param name lifetime
    char* param_name_copy = (char*)params[0].name;
    char* event_name_copy = (char*)params[1].name;

    MIR_item_t func_item = MIR_new_func_arr(mt->ctx, handler_name, 1, &ret_type, 2, params);
    MIR_func_t func = MIR_get_item_func(mt->ctx, func_item);
    mt->em.func_item = func_item;
    mt->em.func = func;
    raw_free(param_name_copy);
    raw_free(event_name_copy);

    // load runtime context from _lambda_rt
    MIR_item_t rt_import = MIR_new_import(mt->ctx, "_lambda_rt");
    MIR_reg_t rt_addr = new_reg(mt, "rt_addr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, rt_addr),
        MIR_new_ref_op(mt->ctx, rt_import)));
    MIR_reg_t runtime_reg = new_reg(mt, "runtime", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, runtime_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, rt_addr, 0, 1)));

    emit_load_module_consts(mt);

    // load type_list from per-module BSS
    MIR_reg_t mod_tl_bss_addr = new_reg(mt, "mod_tl_bss", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mod_tl_bss_addr),
        MIR_new_ref_op(mt->ctx, mt->type_list_bss)));
    mt->type_list_reg = new_reg(mt, "type_list", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->type_list_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, mod_tl_bss_addr, 0, 1)));

    // load gc heap pointer
    MIR_reg_t heap_reg = new_reg(mt, "heap_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, heap_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 64, runtime_reg, 0, 1)));
    mt->gc_reg = new_reg(mt, "gc_ptr", MIR_T_I64);
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, mt->gc_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, heap_reg, 0, 1)));

    mt->em.frame.runtime = runtime_reg;
    mt->em.frame.return_type = ret_type;
    emit_jit_root_frame_enter(mt);
    begin_function_epilogue(mt, ret_type);
    mt->em.frame.plan.debug_name = handler_name;
    emit_number_frame_enter(mt);

    // register as local function
    register_local_func(mt, handler_name, func_item);

    // determine template reference
    // view_func_name is already interned via name_pool by the caller
    const char* tmpl_ref = view->name ? view->name->chars : view_func_name;

    mt->in_view_context = true;
    mt->in_view_handler = true;
    mt->view_is_edit = view->is_edit;
    mt->view_template_ref = tmpl_ref;

    // set up handler scope: ~ resolves to the model parameter
    push_scope(mt);
    MIR_reg_t model_reg = MIR_reg(mt->ctx, "_model", func);
    mt->view_model_reg = model_reg;

    // bind event parameter if declared: on click(evt) { evt.target_class ... }
    if (handler->param && handler->param->name) {
        MIR_reg_t event_reg = MIR_reg(mt->ctx, "_event", func);
        set_var(mt, handler->param->name->chars, event_reg, MIR_T_I64, LMD_TYPE_ANY);
    }

    // load state variables from the central state store
    for (AstStateEntry* se = view->state; se; se = se->next_state) {
        if (!se->name) continue;
        const char* sname = se->name->chars;

        // call tmpl_state_get(model_item, template_ref, state_name) -> Item
        MIR_reg_t state_val = emit_call_3(mt, "tmpl_state_get",
            MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, model_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)tmpl_ref),
            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)sname));

        set_state_var(mt, sname, state_val, MIR_T_I64, LMD_TYPE_ANY, sname);
    }

    // re-emit view body let declarations so handlers can reference them
    // (the AST scope says handler inherits template scope which includes body lets)
    if (view->body && view->body->node_type == AST_NODE_CONTENT) {
        AstListNode* body_list = (AstListNode*)view->body;
        AstNode* body_item = body_list->item;
        while (body_item) {
            if (body_item->node_type == AST_NODE_LET_STAM ||
                body_item->node_type == AST_NODE_PUB_STAM ||
                body_item->node_type == AST_NODE_VAR_STAM) {
                transpile_let_stam(mt, (AstLetNode*)body_item);
            }
            body_item = body_item->next;
        }
    }

    // transpile handler body (procedural)
    if (handler->body) {
        transpile_expr(mt, handler->body);
    }

    // Phase 4: auto-commit after edit handler body completes
    if (view->is_edit) {
        // call edit_commit(NULL) to commit the changes as a version
        emit_call_1(mt, "edit_commit", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        log_debug("mir: edit handler auto-commit for '%s'", handler_name);
    }

    // emit return ItemNull (handlers are void-like)
    MIR_reg_t null_r = new_reg(mt, "null_ret", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, null_r),
        MIR_new_int_op(mt->ctx, (int64_t)NULL_VAL)));
    emit_function_return(mt, MIR_new_reg_op(mt->ctx, null_r));
    finish_function_epilogue(mt);
    finalize_gc_root_publication(mt, handler_name);
    finalize_side_root_frame(mt);

    pop_scope(mt);
    MIR_finish_func(mt->ctx);

    // restore all saved context
    mt->in_view_context = saved_in_view_context;
    mt->in_view_handler = saved_in_view_handler;
    mt->view_is_edit = saved_view_is_edit;
    mt->view_model_reg = saved_view_model_reg;
    mt->view_template_ref = saved_view_template_ref;
    mt->em.func_item = saved_func_item;
    mt->em.func = saved_func;
    mt->consts_reg = saved_consts_reg;
    mt->gc_reg = saved_gc_reg;
    mt->type_list_reg = saved_tl_reg;
    em_frame_restore(&mt->em, saved_frame);
    mt->in_user_func = saved_in_user_func;
    mt->in_proc = saved_in_proc;
    mt->func_body = saved_func_body;
    mt->block_returned = saved_block_returned;
}

// ============================================================================
// Pre-pass: recursively find and define all function definitions
// MIR requires all functions to be created at module level before we start
// generating code for main. This scanner descends into content blocks,
// let statements, and other constructs to find nested function definitions.
// ============================================================================

static void prepass_define_functions(MirTranspiler* mt, AstNode* node) {
    while (node) {
        switch (node->node_type) {
        case AST_NODE_FUNC:
        case AST_NODE_PROC:
        case AST_NODE_FUNC_EXPR: {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            // First define any nested functions inside this function's body
            // so they exist before the outer function body references them
            if (fn_node->body) prepass_define_functions(mt, fn_node->body);
            transpile_func_def(mt, fn_node);
            break;
        }
        case AST_NODE_CONTENT: {
            AstListNode* list = (AstListNode*)node;
            if (list->declare) prepass_define_functions(mt, list->declare);
            prepass_define_functions(mt, list->item);
            break;
        }
        case AST_NODE_LIST: {
            AstListNode* list = (AstListNode*)node;
            // Check declare chain
            if (list->declare) prepass_define_functions(mt, list->declare);
            prepass_define_functions(mt, list->item);
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM: {
            AstLetNode* let_node = (AstLetNode*)node;
            prepass_define_functions(mt, let_node->declare);
            break;
        }
        case AST_NODE_OBJECT_TYPE: {
            // recurse into methods for function definition, with method_owner context
            AstObjectTypeNode* obj = (AstObjectTypeNode*)node;
            if (obj->methods) {
                TypeObject* saved_owner = mt->method_owner;
                TypeType* tt = (TypeType*)obj->type;
                mt->method_owner = (TypeObject*)tt->type;
                prepass_define_functions(mt, obj->methods);
                mt->method_owner = saved_owner;
            }
            break;
        }
        case AST_NODE_ASSIGN:
        case AST_NODE_KEY_EXPR:
        case AST_NODE_PARAM:
        case AST_NODE_NAMED_ARG: {
            AstNamedNode* named = (AstNamedNode*)node;
            if (named->as) prepass_define_functions(mt, named->as);
            break;
        }
        case AST_NODE_PRIMARY: {
            AstPrimaryNode* pri = (AstPrimaryNode*)node;
            if (pri->expr) prepass_define_functions(mt, pri->expr);
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            if (call->function) prepass_define_functions(mt, call->function);
            if (call->argument) prepass_define_functions(mt, call->argument);
            break;
        }
        case AST_NODE_BINARY:
        case AST_NODE_PIPE: {
            AstBinaryNode* bi = (AstBinaryNode*)node;
            if (bi->left) prepass_define_functions(mt, bi->left);
            if (bi->right) prepass_define_functions(mt, bi->right);
            break;
        }
        case AST_NODE_UNARY:
        case AST_NODE_SPREAD: {
            AstUnaryNode* un = (AstUnaryNode*)node;
            if (un->operand) prepass_define_functions(mt, un->operand);
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* if_node = (AstIfNode*)node;
            if (if_node->cond) prepass_define_functions(mt, if_node->cond);
            if (if_node->then) prepass_define_functions(mt, if_node->then);
            if (if_node->otherwise) prepass_define_functions(mt, if_node->otherwise);
            break;
        }
        case AST_NODE_FOR_EXPR:
        case AST_NODE_FOR_STAM: {
            AstForNode* for_node = (AstForNode*)node;
            if (for_node->loop) prepass_define_functions(mt, for_node->loop);
            if (for_node->let_clause) prepass_define_functions(mt, for_node->let_clause);
            if (for_node->where) prepass_define_functions(mt, for_node->where);
            if (for_node->then) prepass_define_functions(mt, for_node->then);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* match = (AstMatchNode*)node;
            if (match->scrutinee) prepass_define_functions(mt, match->scrutinee);
            if (match->first_arm) prepass_define_functions(mt, (AstNode*)match->first_arm);
            break;
        }
        case AST_NODE_MATCH_ARM: {
            AstMatchArm* arm = (AstMatchArm*)node;
            if (arm->body) prepass_define_functions(mt, arm->body);
            break;
        }
        case AST_NODE_LOOP: {
            AstLoopNode* loop = (AstLoopNode*)node;
            if (loop->as) prepass_define_functions(mt, loop->as);
            break;
        }
        case AST_NODE_DECOMPOSE: {
            AstDecomposeNode* dec = (AstDecomposeNode*)node;
            if (dec->as) prepass_define_functions(mt, dec->as);
            break;
        }
        case AST_NODE_RAISE_STAM:
        case AST_NODE_RAISE_EXPR: {
            AstRaiseNode* raise = (AstRaiseNode*)node;
            if (raise->value) prepass_define_functions(mt, raise->value);
            break;
        }
        case AST_NODE_RETURN_STAM: {
            AstReturnNode* ret = (AstReturnNode*)node;
            if (ret->value) prepass_define_functions(mt, ret->value);
            break;
        }
        case AST_NODE_WHILE_STAM: {
            AstWhileNode* wh = (AstWhileNode*)node;
            if (wh->cond) prepass_define_functions(mt, wh->cond);
            if (wh->body) prepass_define_functions(mt, wh->body);
            break;
        }
        case AST_NODE_ASSIGN_STAM: {
            AstAssignStamNode* assign = (AstAssignStamNode*)node;
            if (assign->value) prepass_define_functions(mt, assign->value);
            break;
        }
        case AST_NODE_INDEX_ASSIGN_STAM:
        case AST_NODE_MEMBER_ASSIGN_STAM: {
            AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
            if (ca->object) prepass_define_functions(mt, ca->object);
            if (ca->key) prepass_define_functions(mt, ca->key);
            if (ca->value) prepass_define_functions(mt, ca->value);
            break;
        }
        case AST_NODE_INDEX_EXPR:
        case AST_NODE_MEMBER_EXPR: {
            AstFieldNode* field = (AstFieldNode*)node;
            if (field->object) prepass_define_functions(mt, field->object);
            if (field->field) prepass_define_functions(mt, field->field);
            break;
        }
        case AST_NODE_ARRAY: {
            AstArrayNode* arr = (AstArrayNode*)node;
            if (arr->item) prepass_define_functions(mt, arr->item);
            break;
        }
        case AST_NODE_MAP: {
            AstMapNode* map = (AstMapNode*)node;
            if (map->item) prepass_define_functions(mt, map->item);
            break;
        }
        case AST_NODE_ELEMENT: {
            AstElementNode* elmt = (AstElementNode*)node;
            if (elmt->item) prepass_define_functions(mt, elmt->item);
            if (elmt->content) prepass_define_functions(mt, elmt->content);
            break;
        }
        case AST_NODE_VIEW: {
            AstViewNode* view = (AstViewNode*)node;
            // compile any nested functions inside the view body first
            if (view->body) prepass_define_functions(mt, view->body);
            // compile any nested functions inside event handler bodies
            for (AstEventHandler* h = view->handler; h; h = h->next_handler) {
                if (h->body) prepass_define_functions(mt, h->body);
            }
            // compile the view body as a MIR function
            transpile_view_def(mt, view);
            // compile event handlers as separate MIR functions
            {
                // derive the view function name for handler naming
                // (view_counter was already incremented by transpile_view_def)
                char vname[64];
                snprintf(vname, sizeof(vname), "_view_%d", mt->view_counter - 1);
                // intern the name so handlers get a persistent pointer matching
                // the body function's template_ref (name_pool deduplicates)
                const char* view_ref = view->name ? view->name->chars
                    : name_pool_create_len(mt->name_pool, vname, strlen(vname))->chars;
                int hidx = 0;
                for (AstEventHandler* h = view->handler; h; h = h->next_handler) {
                    transpile_handler_def(mt, h, view, view_ref, hidx++);
                }
            }
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
}

// ============================================================================
// AST root transpilation
// ============================================================================

void transpile_mir_ast(MIR_context_t ctx, AstScript *script, const char* source,
                       ArrayList* type_list, ArrayList* const_list,
                       Pool* script_pool, NamePool* name_pool) {
    log_notice("transpile AST to MIR (direct)");

    MirTranspiler mt;
    memset(&mt, 0, sizeof(mt));
    mt.ctx = ctx;
    mt.em.ctx = ctx;  // emitter caches the immutable MIR context handle
    mt.em.call_owner = &mt;
    mt.em.root_call_value = lambda_call_root_value;
    mt.em.after_call_result = lambda_after_call_result;
    mt.em.convert_rep = lambda_convert_rep;
    mt.script = script;
    mt.source = source;
    mt.is_main = true;
    mt.type_list = type_list;
    mt.const_list = const_list;
    mt.script_pool = script_pool;
    mt.name_pool = name_pool;
    mt.native_return_tid = LMD_TYPE_ANY;  // P4-3.4: no native return at module level

    // Init import cache
    mt.em.import_cache = em_import_cache_new(128);
    mt.em.const_list = const_list;
    mt.local_funcs  = local_func_new(32);
    mt.global_vars  = global_var_new(32);
    mt.infer_cache  = infer_cache_new(32);

    // Create module
    mt.module = MIR_new_module(ctx, "lambda_script");

    // Per-module BSS: stores this module's const_list pointer so that all functions
    // (including those called cross-module) load their own module's consts.
    mt.consts_bss = MIR_new_bss(ctx, "_mod_consts_ptr", 8);
    mt.em.consts_bss = mt.consts_bss;

    // Per-module BSS: stores this module's type_list pointer so that cross-module
    // map/element/object allocations use the right type_list.
    mt.type_list_bss = MIR_new_bss(ctx, "_mod_type_list_ptr", 8);

    // Import _lambda_rt (shared context pointer)
    MIR_item_t rt_import = MIR_new_import(ctx, "_lambda_rt");

    // Pre-pass: compile string/symbol patterns
    prepass_compile_patterns(&mt, script->child);

    // Pre-pass: create BSS items for module-level variables
    prepass_create_global_vars(&mt, script->child);

    // Forward-declare ALL functions first (handles forward references between functions)
    prepass_forward_declare(&mt, script->child);

    // Define ALL functions at module level (transpiles bodies)
    prepass_define_functions(&mt, script->child);

    // Create main function: Item main(Context* runtime)
    em_frame_dispose(&mt.em);
    MIR_var_t main_vars[] = {{MIR_T_P, "runtime", 0}};
    MIR_type_t main_ret = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(ctx, "main", 1, &main_ret, 1, main_vars);
    MIR_func_t main_func = MIR_get_item_func(ctx, main_item);
    mt.em.func_item = main_item;
    mt.em.func = main_func;

    // Get the runtime parameter register
    MIR_reg_t runtime_reg = MIR_reg(ctx, "runtime", main_func);

    // Store runtime to _lambda_rt: *(&_lambda_rt) = runtime
    // import_resolver("_lambda_rt") returns &_lambda_rt (a Context**)
    // We load this address into a register, then store runtime through it
    mt.rt_reg = runtime_reg;
    MIR_reg_t rt_addr = new_reg(&mt, "rt_addr", MIR_T_I64);
    // Load the address that the import resolves to (= &_lambda_rt)
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, rt_addr),
        MIR_new_ref_op(ctx, rt_import)));
    // Store runtime pointer at that address: *(&_lambda_rt) = runtime
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_mem_op(ctx, MIR_T_I64, 0, rt_addr, 0, 1),
        MIR_new_reg_op(ctx, runtime_reg)));

    emit_load_module_consts(&mt);

    // Load type_list from per-module BSS _mod_type_list_ptr so all map/elmt/object
    // allocations in this module use the module's own type_list via the _with_tl wrappers.
    MIR_reg_t mod_tl_bss_addr = new_reg(&mt, "mod_tl_bss", MIR_T_I64);
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, mod_tl_bss_addr),
        MIR_new_ref_op(ctx, mt.type_list_bss)));
    mt.type_list_reg = new_reg(&mt, "type_list", MIR_T_I64);
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, mt.type_list_reg),
        MIR_new_mem_op(ctx, MIR_T_I64, 0, mod_tl_bss_addr, 0, 1)));

    // Load gc_heap_t* for inline bump allocation: context->heap->gc
    // EvalContext.heap is at offsetof(EvalContext, heap), Heap.gc is at offset 8
    MIR_reg_t heap_reg = new_reg(&mt, "heap_ptr", MIR_T_I64);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, heap_reg),
        MIR_new_mem_op(ctx, MIR_T_I64, offsetof(EvalContext, heap), runtime_reg, 0, 1)));  // context->heap
#pragma clang diagnostic pop
    mt.gc_reg = new_reg(&mt, "gc_ptr", MIR_T_I64);
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, mt.gc_reg),
        MIR_new_mem_op(ctx, MIR_T_I64, 8, heap_reg, 0, 1)));  // heap->gc

    mt.em.frame.runtime = runtime_reg;
    mt.em.frame.return_type = main_ret;
    emit_jit_root_frame_enter(&mt);
    begin_function_epilogue(&mt, main_ret);
    mt.em.frame.plan.debug_name = "main";
    emit_number_frame_enter(&mt);

    // Set up variable scope for main body
    push_scope(&mt);

    // P4-3.2: Set func_body for mutation analysis at module level
    mt.func_body = script->child;

    // Transpile body: walk children, emit content
    MIR_reg_t result = new_reg(&mt, "result", MIR_T_I64);
    uint64_t NULL_VAL = (uint64_t)LMD_TYPE_NULL << 56;
    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
        MIR_new_int_op(ctx, (int64_t)NULL_VAL)));

    AstNode* child = script->child;
    while (child) {
        switch (child->node_type) {
        case AST_NODE_CONTENT: {
            MIR_reg_t content_val = transpile_content(&mt, (AstListNode*)child);
            emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
                MIR_new_reg_op(ctx, content_val)));
            break;
        }
        case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM:
        case AST_NODE_TYPE_STAM:
        case AST_NODE_VAR_STAM:
            transpile_let_stam(&mt, (AstLetNode*)child);
            break;
        case AST_NODE_OBJECT_TYPE: {
            // Object type definition — methods compiled in pre-pass
            // Emit method registration code (transpile_expr handles this)
            transpile_expr(&mt, child);
            break;
        }
        case AST_NODE_IMPORT:
        case AST_NODE_FUNC:
        case AST_NODE_FUNC_EXPR:
        case AST_NODE_PROC:
        case AST_NODE_STRING_PATTERN:
        case AST_NODE_SYMBOL_PATTERN:
        case AST_NODE_VIEW:
            // Skip - handled in pre-pass
            break;
        default: {
            // Expression: box it as the result
            MIR_reg_t val = transpile_box_item(&mt, child);
            emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
                MIR_new_reg_op(ctx, val)));
            break;
        }
        }
        child = child->next;
    }

    // Emit invocation of user-defined main() procedure if present
    // Equivalent to C transpiler's: if (rt->run_main) result = _main0();
    child = script->child;
    while (child) {
        if (child->node_type == AST_NODE_CONTENT) {
            child = ((AstListNode*)child)->item;
            continue;
        }
        if (child->node_type == AST_NODE_PROC) {
            AstFuncNode* proc_node = (AstFuncNode*)child;
            if (proc_node->name && strcmp(proc_node->name->chars, "main") == 0) {
                // Load rt->run_main flag
                MIR_reg_t run_main_val = new_reg(&mt, "run_main", MIR_T_I64);
                emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, run_main_val),
                    MIR_new_mem_op(ctx, MIR_T_U8, offsetof(Context, run_main), runtime_reg, 0, 1)));

                MIR_label_t skip_main = MIR_new_label(ctx);
                emit_insn(&mt, MIR_new_insn(ctx, MIR_BF, MIR_new_label_op(ctx, skip_main),
                    MIR_new_reg_op(ctx, run_main_val)));

                // Call the user-defined main procedure: _main0()
                StrBuf* main_name = strbuf_new_cap(64);
                write_fn_name_ex(main_name, proc_node, NULL, "_b");

                MIR_item_t main_func = find_local_func(&mt, main_name->str);
                if (main_func) {
                    LocalFuncEntry* entry = find_local_func_entry(&mt,
                        main_name->str);
                    MirCallResult call = em_call_direct(&mt.em,
                        main_name->str, main_func,
                        entry ? entry->variant : NULL, 0, NULL, NULL);
                    MIR_reg_t main_result = call.normal.reg;
                    emit_insn(&mt, MIR_new_insn(ctx, MIR_MOV, MIR_new_reg_op(ctx, result),
                        MIR_new_reg_op(ctx, main_result)));
                } else {
                    log_error("mir: could not find local function '%s' for run_main", main_name->str);
                }
                strbuf_free(main_name);

                emit_insn(&mt, skip_main);
                break;  // only one main proc
            }
        }
        child = child->next;
    }

    pop_scope(&mt);

    // Return result
    emit_function_return(&mt, MIR_new_reg_op(ctx, result));
    finish_function_epilogue(&mt);
    finalize_gc_root_publication(&mt, "main");
    finalize_side_root_frame(&mt);
    // Main releases shared frame state after all late finalizers consume it.
    em_frame_dispose(&mt.em);

#ifndef NDEBUG
    // Dump MIR text for debugging (before finish_func to capture state on error).
    // The dump is a debug artifact like log.txt, so --no-log suppresses it too:
    // log_disable_all() clears the default category's enabled flag, checked here.
    // The path is overridable via LAMBDA_MIR_DUMP_PATH: every debug lambda.exe run
    // truncates the fixed default path, so tests that inspect the dump while other
    // lambda.exe processes run concurrently must redirect it to a private file.
    if (log_default_category && log_default_category->enabled) {
        const char* mir_dump_path = getenv("LAMBDA_MIR_DUMP_PATH");
        if (!mir_dump_path || !*mir_dump_path) { mir_dump_path = "temp/mir_dump.txt"; }
        FILE* mir_dump = fopen(mir_dump_path, "w");
        if (mir_dump) {
            MIR_output(ctx, mir_dump);
            fclose(mir_dump);
        } else {
            log_warn("mir dump: failed to open '%s' for writing", mir_dump_path);
        }
    }
#endif

    MIR_finish_func(ctx);
    MIR_finish_module(ctx);

    // Load module for linking (required before MIR_link)
    MIR_load_module(ctx, mt.module);

    // Cleanup
    hashmap_free(mt.em.import_cache);
    hashmap_free(mt.local_funcs);
    hashmap_free(mt.global_vars);
    hashmap_free(mt.infer_cache);
}

// ============================================================================
// Per-module MIR Direct compilation (used when runtime->use_mir_direct is set)
// ============================================================================

// Registers all pub functions and pub variable BSS addresses from imp's already-compiled
// jit_context into the global dynamic_import_table so import_resolver can resolve them
// when MIR_link() is called for the module that imports this one.
static void register_module_pub_fns(AstImportNode* imp) {
    if (!imp->script || !imp->script->jit_context) {
        log_error("mir cache: import '%.*s' has no compiled dependency context",
                  (int)imp->module.length, imp->module.str);
        assert(false && "MIR Direct import dependency must be compiled before importer");
        return;
    }
    AstNode* mod_node = imp->script->ast_root;
    if (!mod_node || mod_node->node_type != AST_SCRIPT) return;

    log_info("mir: registering pub symbols from module '%.*s' (index=%d)",
        (int)imp->module.length, imp->module.str, imp->script->index);

    AstNode* mod_child = ((AstScript*)mod_node)->child;
    while (mod_child) {
        if (mod_child->node_type == AST_NODE_CONTENT) {
            mod_child = ((AstListNode*)mod_child)->item;
            continue;
        }
        if (mod_child->node_type == AST_NODE_FUNC ||
            mod_child->node_type == AST_NODE_FUNC_EXPR ||
            mod_child->node_type == AST_NODE_PROC) {
            AstFuncNode* fn_node = (AstFuncNode*)mod_child;
            TypeFunc* fn_type = (TypeFunc*)fn_node->type;
            if (fn_type && fn_type->is_public) {
                // Register boxed wrapper (_b suffix) if the function needs one
                // (due to declared typed params OR inferred native params)
                // Use module-local name to find the function in its own MIR context,
                // then register with module-prefixed name for cross-module resolution.
                // This prevents name collisions when different modules have functions
                // with the same name at the same byte offset (e.g. fraction.ls and
                // style.ls both have pub fn render at byte 574 → _render_574).
                StrBuf* fn_name = strbuf_new_cap(64);
                write_fn_name_ex(fn_name, fn_node, NULL, "_b");
                void* fn_ptr = find_func(imp->script->jit_context, fn_name->str);
                if (fn_ptr) {
                    StrBuf* reg_name = strbuf_new_cap(64);
                    write_fn_name_ex(reg_name, fn_node, imp, "_b");
                    register_dynamic_import(raw_strdup(reg_name->str), fn_ptr); // RAWALLOC_OK: MIR manages param name lifetime
                    log_debug("mir: registered import wrapper fn: %s -> %p", reg_name->str, fn_ptr);
                    strbuf_free(reg_name);
                }
                strbuf_free(fn_name);
            }
        } else if (mod_child->node_type == AST_NODE_PUB_STAM) {
            // Register pub variable BSS addresses.
            // MIR Direct names BSS items "_gvar_<name>" (see prepass_create_global_vars).
            // C2MIR names them the same as write_var_name: "_<name>".
            // The consuming module always imports via write_var_name ("_<name>"),
            // so we register the lookup result under that key regardless of format.
            AstNode* declare = ((AstLetNode*)mod_child)->declare;
            while (declare) {
                AstNamedNode* named = (AstNamedNode*)declare;
                if (named->name) {
                    // Import key (what the consumer's MIR_new_import uses)
                    StrBuf* import_key = strbuf_new_cap(64);
                    write_var_name(import_key, named, NULL);  // e.g. "_pi"
                    // MIR Direct BSS name: "_gvar_<rawname>"
                    char gvar[200];
                    snprintf(gvar, sizeof(gvar), "_gvar_%.*s", (int)named->name->len, named->name->chars);
                    MIR_item_t bss_item = find_import(imp->script->jit_context, gvar);
                    if (!bss_item || !bss_item->addr) {
                        // fall back for C2MIR-compiled modules (BSS == import_key)
                        bss_item = find_import(imp->script->jit_context, import_key->str);
                    }
                    if (bss_item && bss_item->addr) {
                        register_dynamic_import(raw_strdup(import_key->str), bss_item->addr); // RAWALLOC_OK: MIR manages param name lifetime
                        log_debug("mir: registered import var BSS: %s -> %p", import_key->str, bss_item->addr);
                    }
                    strbuf_free(import_key);
                }
                if (named->error_name) {
                    // Same dual-lookup for error variable BSS
                    StrBuf* err_key = strbuf_new_cap(64);
                    strbuf_append_char(err_key, '_');
                    strbuf_append_str_n(err_key, named->error_name->chars, named->error_name->len);
                    char gvar_err[200];
                    snprintf(gvar_err, sizeof(gvar_err), "_gvar_%.*s",
                             (int)named->error_name->len, named->error_name->chars);
                    MIR_item_t err_bss = find_import(imp->script->jit_context, gvar_err);
                    if (!err_bss || !err_bss->addr) {
                        err_bss = find_import(imp->script->jit_context, err_key->str);
                    }
                    if (err_bss && err_bss->addr) {
                        register_dynamic_import(raw_strdup(err_key->str), err_bss->addr); // RAWALLOC_OK: MIR manages param name lifetime
                        log_debug("mir: registered import error var BSS: %s -> %p", err_key->str, err_bss->addr);
                    }
                    strbuf_free(err_key);
                }
                declare = declare->next;
            }
        }
        mod_child = mod_child->next;
    }
}

// Register cross-language (e.g., JS) module exports for MIR import resolution.
// Unlike register_module_pub_fns which looks up compiled MIR functions from a jit_context,
// this reads native function pointers directly from the JS namespace object stored in the
// unified module registry, then registers them with module-prefixed names so that
// import_resolver() can resolve them during MIR_link().
static void register_cross_lang_pub_fns(AstImportNode* imp) {
    if (!imp->script || !imp->script->reference) return;
    AstNode* mod_node = imp->script->ast_root;
    if (!mod_node || mod_node->node_type != AST_SCRIPT) return;

    ModuleDescriptor* desc = module_get(imp->script->reference);
    if (!desc) {
        log_error("mir: cross-lang module '%s' not found in registry", imp->script->reference);
        return;
    }
    Item ns = desc->namespace_obj;

    log_debug("mir: registering cross-lang pub symbols from '%.*s' (index=%d)",
        (int)imp->module.length, imp->module.str, imp->script->index);

    AstNode* child = ((AstScript*)mod_node)->child;
    while (child) {
        if (child->node_type == AST_NODE_FUNC) {
            AstFuncNode* fn_node = (AstFuncNode*)child;
            TypeFunc* fn_type = (TypeFunc*)fn_node->type;
            if (fn_type && fn_type->is_public) {
                // Look up the function in the JS namespace
                char name_buf[256];
                snprintf(name_buf, sizeof(name_buf), "%.*s",
                    (int)fn_node->name->len, fn_node->name->chars);
                Item key = {.item = s2it(heap_create_name(name_buf, strlen(name_buf)))};
                Item fn_item = js_property_get(ns, key);
                if (get_type_id(fn_item) == LMD_TYPE_FUNC) {
                    void* fn_ptr = js_function_get_ptr(fn_item);
                    if (fn_ptr) {
                        // Register with module-prefixed name (e.g., "m2._add_1000000")
                        StrBuf* reg_name = strbuf_new_cap(64);
                        write_fn_name(reg_name, fn_node, imp);
                        register_dynamic_import(raw_strdup(reg_name->str), fn_ptr); // RAWALLOC_OK: MIR manages param name lifetime
                        strbuf_free(reg_name);
                    } else {
                        log_error("mir: cross-lang fn '%s' has null ptr", name_buf);
                    }
                } else {
                    log_error("mir: cross-lang fn '%s' not found in namespace (type=%d)",
                        name_buf, get_type_id(fn_item));
                }
            }
        }
        child = child->next;
    }
}

// Compile a module whose AST is already built (tp->ast_root set) via MIR Direct.
// Called from transpile_script() when runtime->use_mir_direct is true, for both
// imported modules and the main script.  Depth-first import loading guarantees that
// all of this module's sub-imports are already compiled before we are called.
void compile_script_as_mir_direct(Transpiler* tp, Script* script, const char* script_path,
                                   double* out_jit_init_ms,
                                   double* out_transpile_ms,
                                   double* out_mir_gen_ms) {
    log_notice("MIR Direct: compiling module '%s'", script_path ? script_path : "<unknown>");

    // Ensure template registry exists for post-JIT template registration
    if (!g_template_registry) {
        g_template_registry = template_registry_new();
    }

    bool timing = out_jit_init_ms || out_transpile_ms || out_mir_gen_ms;

    // Clear the dynamic-import table and populate it with this module's direct imports only.
    // Because imports are compiled depth-first, every sub-import's symbols were already
    // registered by their own compile_script_as_mir_direct() call; we clear here so that
    // only THIS module's direct dependencies are visible to import_resolver during MIR_link().
    clear_dynamic_imports();

    AstScript* ast_root = (AstScript*)tp->ast_root;
    if (tp->direct_imports) {
        arraylist_free(tp->direct_imports);
        tp->direct_imports = NULL;
    }
    tp->cache_cross_lang_tainted = false;
    AstNode* child = ast_root->child;
    while (child) {
        if (child->node_type == AST_NODE_IMPORT) {
            AstImportNode* imp = (AstImportNode*)child;
            if (imp->is_cross_lang) {
                tp->cache_cross_lang_tainted = true;
                register_cross_lang_pub_fns(imp);
            } else {
                if (!tp->direct_imports) tp->direct_imports = arraylist_new(4);
                if (imp->script) {
                    arraylist_append(tp->direct_imports, imp->script);
                    if (imp->script->cache_cross_lang_tainted) {
                        tp->cache_cross_lang_tainted = true;
                    }
                }
                register_module_pub_fns(imp);
            }
        }
        child = child->next;
    }

    // Profiling timestamps (only when profiling is requested)
#ifdef _WIN32
    LARGE_INTEGER pt0, pt1, pt2, pt3, pfreq;
    if (timing) { QueryPerformanceFrequency(&pfreq); QueryPerformanceCounter(&pt0); }
#else
    struct timespec pt0, pt1, pt2, pt3;
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt0);
#endif

    // Transpile the AST directly to MIR (no C code generated)
    unsigned int opt_level = tp->runtime ? tp->runtime->optimize_level : 2;
    MIR_context_t ctx = jit_init(opt_level);

#ifdef _WIN32
    if (timing) QueryPerformanceCounter(&pt1);
#else
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt1);
#endif

    transpile_mir_ast(ctx, ast_root, tp->source, tp->type_list, tp->const_list, tp->pool, tp->name_pool);
    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

#ifdef _WIN32
    if (timing) QueryPerformanceCounter(&pt2);
#else
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt2);
#endif

    // Store results in the transpiler and propagate back to script
    tp->jit_context = ctx;
    tp->main_func = (main_func_t)(g_mir_interp_mode ? find_func(ctx, "main") : jit_gen_func(ctx, "main"));

#ifdef _WIN32
    if (timing) QueryPerformanceCounter(&pt3);
#else
    if (timing) clock_gettime(CLOCK_MONOTONIC, &pt3);
#endif

    // Set per-module consts BSS AFTER jit_gen_func: jit_gen_func re-invokes MIR_link
    // which reallocates BSS memory, so any earlier write would be overwritten.
    // After jit_gen_func, BSS addresses are final.
    MIR_item_t consts_bss_item = find_import(ctx, "_mod_consts_ptr");
    if (consts_bss_item && consts_bss_item->addr) {
        *(void**)(consts_bss_item->addr) = tp->const_list ? tp->const_list->data : nullptr;
    } else {
        log_error("MIR Direct: _mod_consts_ptr BSS not found or no addr for '%s' (addr=%p)",
                  script_path ? script_path : "<unknown>",
                  consts_bss_item ? consts_bss_item->addr : nullptr);
    }

    // Set per-module type_list BSS (same timing: must be after jit_gen_func).
    MIR_item_t tl_bss_item = find_import(ctx, "_mod_type_list_ptr");
    if (tl_bss_item && tl_bss_item->addr) {
        *(void**)(tl_bss_item->addr) = tp->type_list;
    } else {
        log_error("MIR Direct: _mod_type_list_ptr BSS not found or no addr for '%s'",
                  script_path ? script_path : "<unknown>");
    }

    // Register view/edit templates: walk AST, look up compiled body functions,
    // and add them to the global template registry.
    if (g_template_registry && tp->main_func) {
        // Walk through content block to find view/edit nodes
        AstNode* first_child = ast_root->child;
        AstNode* tmpl_child = first_child;
        // If the first child is a content block, walk its items
        if (tmpl_child && tmpl_child->node_type == AST_NODE_CONTENT) {
            tmpl_child = ((AstListNode*)tmpl_child)->item;
        }
        int view_idx = 0;
        while (tmpl_child) {
            AstNode* view_child = tmpl_child;
            AstNode* next_tmpl_child = tmpl_child->next;
            if (tmpl_child->node_type == AST_NODE_CONTENT) {
                view_child = ((AstListNode*)tmpl_child)->item;
            }
            while (view_child) {
                if (view_child->node_type == AST_NODE_VIEW) {
                    AstViewNode* view = (AstViewNode*)view_child;

                    // build the function name used during transpilation
                    char func_name[64];
                    snprintf(func_name, sizeof(func_name), "_view_%d", view_idx++);

                    void* func_ptr = find_func(ctx, func_name);
                    if (func_ptr) {
                        // determine specificity from the pattern
                        TemplateSpecificity spec = TMPL_SPEC_CATCHALL;
                        TypeId match_type = LMD_TYPE_ANY;
                        const char* match_tag = NULL;
                        int match_tag_len = 0;

                        if (view->pattern) {
                            AstNode* pat = view->pattern;
                            if (pat->type) {
                                TypeId tid = pat->type->type_id;
                                if (tid == LMD_TYPE_TYPE) {
                                    // unwrap TypeType to get the actual matched type
                                    TypeType* tt = (TypeType*)pat->type;
                                    if (tt->type && tt->type->type_id != LMD_TYPE_ANY) {
                                        match_type = tt->type->type_id;
                                        spec = TMPL_SPEC_SIMPLE_TYPE;
                                        // extract element tag for element patterns
                                        if (match_type == LMD_TYPE_ELEMENT) {
                                            TypeElmt* elmt_type = (TypeElmt*)tt->type;
                                            if (elmt_type->name.str && elmt_type->name.length > 0) {
                                                match_tag = elmt_type->name.str;
                                                match_tag_len = (int)elmt_type->name.length;
                                                spec = elmt_type->length > 0
                                                    ? TMPL_SPEC_ELMT_ATTR : TMPL_SPEC_ELMT_TAG;
                                            }
                                        }
                                    }
                                } else if (tid != LMD_TYPE_ANY) {
                                    match_type = tid;
                                    spec = TMPL_SPEC_SIMPLE_TYPE;
                                }
                            }
                        }

                        // named templates have highest specificity
                        if (view->name) spec = TMPL_SPEC_NAMED;

                        const char* tmpl_name = view->name ? view->name->chars : NULL;
                        template_registry_add(g_template_registry,
                            tmpl_name, view->is_edit,
                            (fn_ptr)func_ptr, spec,
                            match_type, match_tag, match_tag_len,
                            0, 0);

                        // get the just-added entry (it's the last one)
                        TemplateEntry* tmpl_entry = g_template_registry->last;

                        // set template_ref for state store keying
                        // func_name is stack-local, so we need a persistent copy
                        if (tmpl_name) {
                            tmpl_entry->template_ref = tmpl_name;
                        } else {
                            tmpl_entry->template_ref = name_pool_create_len(tp->name_pool, func_name, strlen(func_name))->chars;
                        }

                        // register event handlers on the template entry
                        int hidx = 0;
                        for (AstEventHandler* h = view->handler; h; h = h->next_handler) {
                            char hname[64];
                            snprintf(hname, sizeof(hname), "%s_h%d", func_name, hidx++);
                            void* hptr = find_func(ctx, hname);
                            if (hptr) {
                                const char* ename = h->event ? h->event->chars : "unknown";
                                template_entry_add_handler(tmpl_entry, ename, (fn_ptr)hptr);
                            } else {
                                log_error("MIR Direct: handler function '%s' not found", hname);
                            }
                        }

                        log_debug("registered template '%s' func=%s spec=%d type=%d handlers=%d",
                            tmpl_name ? tmpl_name : "(anonymous)", func_name,
                            (int)spec, (int)match_type, hidx);
                    } else {
                        log_error("MIR Direct: view function '%s' not found after JIT", func_name);
                    }
                }
                view_child = (tmpl_child->node_type == AST_NODE_CONTENT) ? view_child->next : NULL;
            }
            tmpl_child = next_tmpl_child;
        }
    }

    if (!tp->main_func) {
        log_error("MIR Direct: failed to generate 'main' for '%s'",
                  script_path ? script_path : "<unknown>");
        jit_cleanup(ctx);
        tp->jit_context = NULL;
    }

    // Copy Script-sized portion of Transpiler back to the Script object
    memcpy(script, tp, sizeof(Script));
    script->jit_context = tp->jit_context;
    script->main_func   = tp->main_func;

    // Write out profiling results
    if (timing) {
#ifdef _WIN32
        double f = 1000.0 / (double)pfreq.QuadPart;
        if (out_jit_init_ms)  *out_jit_init_ms  = (double)(pt1.QuadPart - pt0.QuadPart) * f;
        if (out_transpile_ms) *out_transpile_ms = (double)(pt2.QuadPart - pt1.QuadPart) * f;
        if (out_mir_gen_ms)   *out_mir_gen_ms   = (double)(pt3.QuadPart - pt2.QuadPart) * f;
#else
        auto elapsed = [](struct timespec a, struct timespec b) -> double {
            long sec = b.tv_sec - a.tv_sec;
            long nsec = b.tv_nsec - a.tv_nsec;
            if (nsec < 0) { sec--; nsec += 1000000000L; }
            return sec * 1000.0 + nsec / 1e6;
        };
        if (out_jit_init_ms)  *out_jit_init_ms  = elapsed(pt0, pt1);
        if (out_transpile_ms) *out_transpile_ms = elapsed(pt1, pt2);
        if (out_mir_gen_ms)   *out_mir_gen_ms   = elapsed(pt2, pt3);
#endif
    }

    if (tp->main_func) {
        log_info("MIR Direct: compiled '%s' ctx=%p main=%p",
                 script_path ? script_path : "<unknown>", ctx, tp->main_func);
    }
}

static bool script_ptr_list_contains(ArrayList* list, Script* script) {
    if (!list || !script) return false;
    for (int i = 0; i < list->length; i++) {
        if ((Script*)list->data[i] == script) return true;
    }
    return false;
}

static void collect_import_cone_postorder(Script* script, ArrayList* cone, ArrayList* visited) {
    if (!script || script_ptr_list_contains(visited, script)) return;
    arraylist_append(visited, script);

    if (script->direct_imports) {
        for (int i = 0; i < script->direct_imports->length; i++) {
            Script* dep = (Script*)script->direct_imports->data[i];
            collect_import_cone_postorder(dep, cone, visited);
        }
    }

    arraylist_append(cone, script);
}

static ArrayList* collect_import_cone(Script* main_script) {
    ArrayList* cone_with_main = arraylist_new(8);
    ArrayList* visited = arraylist_new(8);
    collect_import_cone_postorder(main_script, cone_with_main, visited);
    arraylist_free(visited);

    ArrayList* cone = arraylist_new(cone_with_main->length > 1 ? cone_with_main->length - 1 : 1);
    for (int i = 0; i < cone_with_main->length; i++) {
        Script* script = (Script*)cone_with_main->data[i];
        if (script != main_script) arraylist_append(cone, script);
    }
    arraylist_free(cone_with_main);
    return cone;
}

// ============================================================================
// Main entry point for MIR compilation
// ============================================================================

Input* run_script_mir(Runtime *runtime, const char* source, char* script_path, bool run_main) {
    log_notice("Running script with MIR JIT compilation (direct)");

    // Initialize runner
    Runner runner;
    runner_init(runtime, &runner);

    // Enable MIR Direct mode: load_script will call compile_script_as_mir_direct() for every
    // module (main + all imports) instead of going through the C transpiler (C2MIR) path.
    // Each compile_script_as_mir_direct() call clears dynamic_imports, registers the module's
    // direct sub-imports' pub symbols, then does transpile_mir_ast() + MIR_link().
    // Imports are compiled depth-first, so by the time any module is linked its dependencies
    // are already in dynamic_imports.
    bool was_mir_direct = runtime->use_mir_direct;
    runtime->use_mir_direct = true;

    if (source) {
        runner.script = load_script(runtime, script_path, source, false);
    } else {
        runner.script = load_script(runtime, script_path, NULL, false);
    }

    runtime->use_mir_direct = was_mir_direct;  // restore for any nested/reentrant calls

    if (!runner.script || !runner.script->ast_root) {
        log_error("Failed to parse script");
        Pool* error_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(error_pool, nullptr);
        if (!output) {
            log_error("Failed to create error output Input");
            if (error_pool) pool_destroy(error_pool);
            return nullptr;
        }
        output->root = ItemError;
        return output;
    }

    if (!runner.script->main_func) {
        log_error("MIR Direct: 'main' missing after compilation of '%s'", script_path);
        Pool* error_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(error_pool, nullptr);
        if (output) output->root = ItemError;
        return output;
    }

    ArrayList* import_cone = collect_import_cone(runner.script);
    if (import_cone && import_cone->length > 0) {
        // Set up context for module initialization
        runner_setup_context(&runner);
        runner.context.run_main = run_main;

        // cached modules may hold heap Items from a previous batch run; zero BSS before rooting
        for (int i = 0; i < import_cone->length; i++) {
            Script* imp_script = (Script*)import_cone->data[i];
            if (imp_script && imp_script->jit_context) {
                reset_and_register_bss_gc_roots((void*)imp_script->jit_context);
            }
        }
        reset_and_register_bss_gc_roots((void*)runner.script->jit_context);

        // run only the current main script's import cone, deps before dependents
        for (int i = 0; i < import_cone->length; i++) {
            Script* imp_script = (Script*)import_cone->data[i];
            if (!imp_script || !imp_script->main_func) continue;
            log_info("mir cache: running imported module main index=%d", imp_script->index);
            runner.context.consts = imp_script->const_list ? imp_script->const_list->data : nullptr;
            runner.context.type_list = imp_script->type_list;
            imp_script->main_func(&runner.context);
        }
        // Restore context for main script execution
        runner.context.consts = runner.script->const_list ? runner.script->const_list->data : nullptr;
        runner.context.type_list = runner.script->type_list;

        // Execute main script using already-initialized context
        log_notice("Executing JIT compiled code...");
        runner.context.run_main = run_main;
        Item result;
        LambdaRecoveryCheckpoint recovery_checkpoint =
            lambda_recovery_checkpoint_capture(&runner.context);
        #if defined(__APPLE__) || defined(__linux__)
        if (sigsetjmp(_lambda_recovery_point, 1)) {
        #elif defined(_WIN32)
        if (setjmp(_lambda_recovery_point)) {
        #else
        if (0) {
        #endif
            _lambda_recovery_armed = 0;   // recovery consumed; disarm
            log_error("exec: recovered from stack overflow via signal handler");
            _lambda_stack_overflow_flag = false;
            lambda_recovery_checkpoint_restore(&recovery_checkpoint);
            lambda_stack_overflow_error("<signal>");
            result = runner.context.result = ItemError;
        } else {
            _lambda_recovery_armed = 1;    // arm only for the duration of user code
            result = runner.context.result = runner.script->main_func(&runner.context);
            _lambda_recovery_armed = 0;
            lambda_recovery_checkpoint_disarm(&recovery_checkpoint);
        }
        if (runner.context.heap) {
            runner.context.heap->result_root = runner.context.result.item;
        }
        preserve_context_last_error(&runner.context, result);

        // Create output
        Pool* output_pool = mem_pool_create(NULL, MEM_ROLE_AST, "script.result");
        Input* output = Input::create(output_pool, nullptr);
        if (!output) {
            log_error("Failed to create output Input");
            if (output_pool) pool_destroy(output_pool);
            if (runner.context.cwd) {
                url_destroy((Url*)runner.context.cwd);
                runner.context.cwd = NULL;
            }
            arraylist_free(import_cone);
            return nullptr;
        }
        resolve_sys_paths_recursive(result);
        if (runner.context.cwd) {
            url_destroy((Url*)runner.context.cwd);
            runner.context.cwd = NULL;
        }

        // Return result directly on the GC heap — no deep_copy needed.
        // With GC-managed memory the heap is retained across the session;
        // the caller is responsible for calling runtime_cleanup() when done.
        output->root = result;
        arraylist_free(import_cone);
        return output;
    }
    if (import_cone) arraylist_free(import_cone);

    // Execute (no imports — use standard path)
    Input* output = execute_script_and_create_output(&runner, run_main);

    return output;
}
