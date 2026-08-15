#pragma once

// js_mir_internal.hpp - shared declarations for the split JS MIR transpiler.

#include "js_mir_context.hpp"
#include "js_runtime_state.hpp"
#include "../runtime/heap_api.h"
#include "../runtime/mir_policy.hpp"
#include "../jube/jube_interface.h"

extern "C" void *import_resolver(const char *name);
extern __thread EvalContext* context;
extern "C" void js_reset_module_vars();
extern "C" uint32_t js_alloc_module_state(uint32_t var_count);
extern "C" bool js_activate_module_state(uint32_t var_count);
extern "C" uint32_t js_get_active_module_state_id(void);
extern "C" bool js_set_active_module_state_id(uint32_t module_state_id);
extern "C" bool js_module_state_is_available(uint32_t module_state_id);
extern void js_double_to_string(double d, char* out, int out_size);
extern "C" Item js_process_emit_before_exit(int code);
extern "C" void js_process_emit_exit(int code);
extern MIR_error_func_t g_batch_mir_error_handler;
extern unsigned int g_js_mir_optimize_level;
extern int g_js_force_document_interp;
#define JM_LARGE_FUNC_INSN_THRESHOLD 10000
#define JM_LARGE_MODULE_INSN_THRESHOLD MIR_LARGE_MODULE_INSN_THRESHOLD
// Tune6: in a document/Radiant context (cold vendor JS), use the MIR interpreter
// for modules above this (moderate) insn count — see Transpile_Js_Tune6_AST.md §0.2d.
#define JM_RADIANT_INTERP_INSN_THRESHOLD MIR_RADIANT_INTERP_INSN_THRESHOLD
extern "C" int g_mir_interp_mode;
extern "C" const TSLanguage* tree_sitter_typescript(void);
extern "C" const TSLanguage* tree_sitter_javascript(void);
extern "C" void ensure_jit_imports_initialized(void);

bool jm_float_const_is_inline(double value);
MIR_reg_t jm_box_float_const(JsMirTranspiler* mt, double value);

extern JsModuleConstEntry* g_eval_preamble_entries;
extern int g_eval_preamble_entry_count;
extern int g_eval_preamble_var_count;
bool js_preamble_entry_copy(const JsModuleConstEntry* source,
                            JsModuleConstEntry* target);
bool js_preamble_entries_copy(const JsModuleConstEntry* source, int count,
                              JsModuleConstEntry** out_entries);
void js_preamble_entries_free(JsModuleConstEntry* entries, int count);
void js_eval_preamble_entries_free(void);
extern __thread NamePool* g_js_mir_name_pool_override;
void jm_set_name_pool_override(NamePool* pool);
extern bool g_jm_preamble_mode;
extern bool g_jm_preamble_compile_only;
extern JsPreambleState* g_jm_preamble_out;
extern const JsPreambleState* g_jm_preamble_in;
// Dynamic JS compilation follows the bound EvalContext. This direct TLS read
// keeps runtime selection context-local without adding a hot-path check.
static inline Runtime* js_current_runtime(void) {
    return context ? context->runtime : NULL;
}
#define js_dynamic_func_counter (js_runtime_state.dynamic_func_counter)

#define JS_ACTIVE_TRANSPILE_MAX 32
typedef struct ActiveJsTranspileOwner {
    JsTranspiler* tp;
    JsMirTranspiler* mt;
    char* owned_source;
} ActiveJsTranspileOwner;

// This is only touched at compilation and signal-recovery boundaries.  It is
// deliberately outside generated JS dispatch so realm isolation adds no hot
// path synchronization or readiness probe.
typedef struct JsMirCompileRecoveryState {
    MIR_context_t active_mir_ctx;
    JsTranspiler* active_js_transpiler;
    JsMirTranspiler* active_mir_transpiler;
    char* active_js_owned_source;
    ActiveJsTranspileOwner stack[JS_ACTIVE_TRANSPILE_MAX];
    int count;
} JsMirCompileRecoveryState;

JsMirCompileRecoveryState* jm_compile_recovery_state_ensure(void);
JsMirCompileRecoveryState* jm_compile_recovery_state_current(void);
void jm_compile_recovery_state_destroy_context(JsRuntimeState* runtime_state);

#define g_active_mir_ctx (jm_compile_recovery_state_ensure()->active_mir_ctx)
#define g_active_js_transpiler (jm_compile_recovery_state_ensure()->active_js_transpiler)
#define g_active_mir_transpiler (jm_compile_recovery_state_ensure()->active_mir_transpiler)
#define g_active_js_owned_source (jm_compile_recovery_state_ensure()->active_js_owned_source)
#define module_mir_contexts ((MIR_context_t*)js_runtime_state.deferred_mir.contexts)
#define module_mir_source_buffers (js_runtime_state.deferred_mir.source_buffers)
#define module_mir_context_count (js_runtime_state.deferred_mir.count)
void* jm_build_js_debug_info(JsMirTranspiler* mt, const char* filename);

typedef enum JsMirReferenceKind {
    JS_MIR_REF_INVALID = 0,
    JS_MIR_REF_PROPERTY,
    JS_MIR_REF_SUPER_PROPERTY
} JsMirReferenceKind;

typedef struct JsMirReference {
    JsMirReferenceKind kind;
    MIR_reg_t base_reg;
    MIR_reg_t key_reg;
    bool strict;
    bool uninitialized_this;
    bool is_private;
    bool computed_key;
    bool property_key_canonicalized;
    uint32_t named_key_index;
    NameId named_key_id;
    int jube_slot;
    uint32_t jube_ordinal;
    uint8_t jube_kind;
    bool jube_can_raise;
} JsMirReference;

typedef struct JsMirLexicalThisRebind {
    bool saved_force_closure_env_copy;
    bool restore_binding;
    MIR_reg_t var_reg;
    MIR_reg_t saved_var_reg;
    MIR_reg_t scope_env_reg;
    MIR_reg_t saved_scope_env_value_reg;
    int scope_env_slot;
} JsMirLexicalThisRebind;

typedef enum JsMirSuspendKind {
    JS_MIR_SUSPEND_YIELD = 0,
    JS_MIR_SUSPEND_AWAIT,
    JS_MIR_SUSPEND_IMPLICIT_AWAIT
} JsMirSuspendKind;

typedef enum JsMirCompletionKind {
    JS_MIR_COMPLETION_THROW = 0,
    JS_MIR_COMPLETION_AWAIT_REJECTION,
    JS_MIR_COMPLETION_RETURN,
    JS_MIR_COMPLETION_RETURN_THROUGH_CLEANUP,
    JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL
} JsMirCompletionKind;

typedef enum JsMirClassMethodInstallMode {
    JS_MIR_CLASS_METHOD_INHERITED_STATIC = 0,
    JS_MIR_CLASS_METHOD_OWN_STATIC,
    JS_MIR_CLASS_METHOD_OWN_INSTANCE
} JsMirClassMethodInstallMode;

typedef enum JsMirComputedKeyOrder {
    JS_MIR_COMPUTED_KEY_AFTER_FUNCTION = 0,
    JS_MIR_COMPUTED_KEY_BEFORE_FUNCTION
} JsMirComputedKeyOrder;

typedef struct JsMirClassMethodInstallPolicy {
    MIR_reg_t destination;
    MIR_reg_t home_class;
    MIR_reg_t preserve_reg;
    JsClassEntry* owner_class;
    int method_index;
    JsMirClassMethodInstallMode mode;
    JsMirComputedKeyOrder computed_key_order;
} JsMirClassMethodInstallPolicy;

// internal function declarations
int js_local_func_cmp(const void *a, const void *b, void *udata);
uint64_t js_local_func_hash(const void *item, uint64_t seed0, uint64_t seed1);
int js_module_const_cmp(const void *a, const void *b, void *udata);
uint64_t js_module_const_hash(const void *item, uint64_t seed0, uint64_t seed1);
const char* jm_persist_name(const char* name);
const char* jm_format_name(const char* format, ...);
JsMirTranspiler* jm_create_mir_transpiler(
    JsTranspiler* tp, MIR_context_t ctx, const char* filename, bool is_module,
    int import_capacity, int local_func_capacity, int var_scope_capacity,
    const char* log_prefix);
void jm_destroy_mir_transpiler(JsMirTranspiler* mt);
bool js_link_compiled_name_table(const JsMirTranspiler* mt);
bool js_append_compiled_name_table(const JsMirTranspiler* mt);
bool js_capture_compiled_name_table(const JsMirTranspiler* mt, JsPreambleState* state);
bool jm_build_property_key_image(const PropertyKeySpec* inherited,
    uint32_t inherited_count, uint32_t inherited_bytes_size,
    const ArrayList* local_names, PropertyKeySpec** out_specs,
    uint32_t* out_count, uint32_t* out_bytes_size);
// Strictness of the code currently being lowered: the top-level directive, an
// ES module (always strict), or the enclosing function's own directive.
static inline bool jm_strict_put(JsMirTranspiler* mt) {
    return mt && (mt->is_global_strict || mt->is_module ||
        (mt->current_fc && mt->current_fc->is_strict));
}
MIR_reg_t jm_new_reg(JsMirTranspiler* mt, const char* prefix, MIR_type_t type);
MIR_label_t jm_new_label(JsMirTranspiler* mt);
void jm_emit(JsMirTranspiler* mt, MIR_insn_t insn);
static inline void jm_emit_mov(JsMirTranspiler* mt, MIR_reg_t target, MIR_reg_t source) {
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, target), MIR_new_reg_op(mt->ctx, source)));
}
static inline void jm_emit_dmov(JsMirTranspiler* mt, MIR_reg_t target, MIR_reg_t source) {
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
        MIR_new_reg_op(mt->ctx, target), MIR_new_reg_op(mt->ctx, source)));
}
static inline void jm_emit_load_i64(JsMirTranspiler* mt, MIR_reg_t target,
        int64_t offset, MIR_reg_t base) {
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, target),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offset, base, 0, 1)));
}
static inline void jm_emit_store_i64(JsMirTranspiler* mt, int64_t offset,
        MIR_reg_t base, MIR_reg_t source) {
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offset, base, 0, 1),
        MIR_new_reg_op(mt->ctx, source)));
}
static inline void jm_emit_reg_binary(JsMirTranspiler* mt, MIR_insn_code_t opcode,
        MIR_reg_t target, MIR_reg_t left, MIR_reg_t right) {
    jm_emit(mt, MIR_new_insn(mt->ctx, opcode,
        MIR_new_reg_op(mt->ctx, target), MIR_new_reg_op(mt->ctx, left),
        MIR_new_reg_op(mt->ctx, right)));
}
static inline void jm_emit_reg_op(JsMirTranspiler* mt, MIR_insn_code_t opcode,
        MIR_reg_t target, MIR_op_t source) {
    jm_emit(mt, MIR_new_insn(mt->ctx, opcode,
        MIR_new_reg_op(mt->ctx, target), source));
}
static inline void jm_emit_reg_binary_op(JsMirTranspiler* mt, MIR_insn_code_t opcode,
        MIR_reg_t target, MIR_reg_t left, MIR_op_t right) {
    jm_emit(mt, MIR_new_insn(mt->ctx, opcode,
        MIR_new_reg_op(mt->ctx, target), MIR_new_reg_op(mt->ctx, left), right));
}
static inline void jm_emit_reg_op_binary(JsMirTranspiler* mt, MIR_insn_code_t opcode,
        MIR_reg_t target, MIR_op_t left, MIR_reg_t right) {
    jm_emit(mt, MIR_new_insn(mt->ctx, opcode,
        MIR_new_reg_op(mt->ctx, target), left, MIR_new_reg_op(mt->ctx, right)));
}
// conditional branch to `label` on a single register operand (MIR_BT/MIR_BF)
static inline void jm_emit_branch(JsMirTranspiler* mt, MIR_insn_code_t code,
        MIR_label_t label, MIR_reg_t reg) {
    jm_emit(mt, MIR_new_insn(mt->ctx, code,
        MIR_new_label_op(mt->ctx, label), MIR_new_reg_op(mt->ctx, reg)));
}
static inline void jm_emit_jmp(JsMirTranspiler* mt, MIR_label_t label) {
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, label)));
}
static inline void jm_emit_ret(JsMirTranspiler* mt, MIR_reg_t reg) {
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, reg)));
}
void jm_emit_label(JsMirTranspiler* mt, MIR_label_t label);
void jm_emit_label_with_state(JsMirTranspiler* mt, MIR_label_t label, JsErrorLaneTrack state);
void jm_begin_function_frame(JsMirTranspiler* mt, MIR_type_t return_type,
    bool item_return, MirScalarReturnMode scalar_return_mode,
    MIR_reg_t runtime_reg, bool clean_error_lane_entry);
void jm_finish_function_frame(JsMirTranspiler* mt, const char* function_name);
int jm_create_gc_root_slot(JsMirTranspiler* mt, MIR_reg_t value);
void jm_update_gc_root_slot(JsMirTranspiler* mt, JsMirVarEntry* var);
void jm_register_owned_env(JsMirTranspiler* mt, MIR_reg_t reg);
void jm_emit_loop_backedge_frame_reload(JsMirTranspiler* mt);
JsMirReference jm_emit_reference(JsMirTranspiler* mt, JsAstNode* node);
MIR_reg_t jm_emit_get_value(JsMirTranspiler* mt, const JsMirReference* ref);
MIR_reg_t jm_emit_put_value(JsMirTranspiler* mt, const JsMirReference* ref, MIR_reg_t value);
MIR_reg_t jm_emit_delete_reference(JsMirTranspiler* mt, const JsMirReference* ref);
const JubeTypeDef* jm_infer_jube_type(JsMirTranspiler* mt, JsAstNode* node);
bool jm_is_private_name(String* name);
String* jm_class_private_name(JsMirTranspiler* mt, JsClassEntry* ce, String* name);
bool jm_class_or_ancestor_has_private_members(JsClassEntry* ce);
void jm_eval_cptn_reset(JsMirTranspiler* mt);
void jm_push_loop_labels(JsMirTranspiler* mt, MIR_label_t continue_label, MIR_label_t break_label);
MIR_reg_t jm_emit_get_iterator(JsMirTranspiler* mt, MIR_reg_t iterable);
MIR_reg_t jm_emit_get_iterator_lazy(JsMirTranspiler* mt, MIR_reg_t iterable);
MIR_reg_t jm_emit_iterator_step(JsMirTranspiler* mt, MIR_reg_t iterator);
MIR_reg_t jm_emit_iterator_done_test(JsMirTranspiler* mt, MIR_reg_t step_result, const char* prefix);
MIR_reg_t jm_emit_iterator_collect_rest(JsMirTranspiler* mt, MIR_reg_t iterator);
void jm_emit_iterator_close(JsMirTranspiler* mt, MIR_reg_t iterator);
void jm_emit_iterator_close_checked(JsMirTranspiler* mt, MIR_reg_t iterator);
void jm_emit_iterator_close_on_error_lane_if_open(JsMirTranspiler* mt, MIR_reg_t iterator,
    MIR_reg_t iter_done, MIR_label_t target);
void jm_emit_abrupt_jump_cleanup(JsMirTranspiler* mt);
void jm_emit_break_completion(JsMirTranspiler* mt, JsBreakContinueNode* brk);
void jm_emit_continue_completion(JsMirTranspiler* mt, JsBreakContinueNode* cont);
int jm_next_resume_state(JsMirTranspiler* mt, JsMirSuspendKind kind);
MIR_reg_t jm_emit_await_value_reg(JsMirTranspiler* mt, MIR_reg_t promise_val,
    JsMirSuspendKind kind);
void jm_emit_suspend_env_save(JsMirTranspiler* mt);
void jm_emit_resume_env_restore(JsMirTranspiler* mt);
void jm_emit_try_state_reset(JsMirTranspiler* mt);
void jm_emit_async_resume_refresh(JsMirTranspiler* mt);
JsTryContext* jm_find_completion_context(JsMirTranspiler* mt, JsMirCompletionKind kind);
JsErrorLaneTrack jm_error_lane_state(JsMirTranspiler* mt);
JsErrorLaneTrack jm_error_lane_merge(JsErrorLaneTrack a, JsErrorLaneTrack b);
void jm_error_lane_set_state(JsMirTranspiler* mt, JsErrorLaneTrack state);
void jm_error_lane_note_call(JsMirTranspiler* mt, JitExceptionEffect effect);
MIR_reg_t jm_emit_error_lane_test(JsMirTranspiler* mt);
void jm_emit_error_lane_route(JsMirTranspiler* mt, JsMirCompletionKind kind);
void jm_emit_error_lane_guard(JsMirTranspiler* mt, MIR_label_t target);
MIR_reg_t jm_arg_frame_base(JsMirTranspiler* mt);
void jm_emit_arg_frame_clear(JsMirTranspiler* mt, JsMirArgStackScope* scope);
bool jm_emit_delayed_return_completion(JsMirTranspiler* mt, MIR_reg_t value,
    JsMirCompletionKind kind);
void jm_emit_throw_completion(JsMirTranspiler* mt, MIR_reg_t value);
void jm_emit_generator_throw_completion(JsMirTranspiler* mt, MIR_reg_t value);
void jm_emit_error_lane_exit(JsMirTranspiler* mt);
MIR_reg_t jm_native_return_reg(JsMirTranspiler* mt, MIR_reg_t value);
MIR_reg_t jm_emit_uext8(JsMirTranspiler* mt, MIR_reg_t r);
struct hashmap* jm_var_scope_at(JsMirTranspiler* mt, int depth);
bool jm_var_scope_set(JsMirTranspiler* mt, int depth, struct hashmap* scope);
JsLoopLabels* jm_loop_label_at(JsMirTranspiler* mt, int index);
JsMirIteratorFrame* jm_for_of_iterator_at(JsMirTranspiler* mt, int index);
JsTryContext* jm_try_context_at(JsMirTranspiler* mt, int index);
JsTryContext* jm_try_context_push(JsMirTranspiler* mt);
void jm_push_scope(JsMirTranspiler* mt);
int jm_arguments_param_index(JsMirTranspiler* mt, const char* vname,
    JsMirVarEntry* resolved_var);
JsMirVarEntry* jm_arguments_param_var(JsMirTranspiler* mt, int param_index);
bool jm_has_use_strict_directive(JsFunctionNode* fn);
bool jm_function_decl_is_direct_binding(JsFunctionNode* fn, bool arrow_body_is_direct);
void jm_pop_scope(JsMirTranspiler* mt);
void jm_set_var(JsMirTranspiler* mt, const char* name, MIR_reg_t reg,
                       MIR_type_t mir_type = MIR_T_I64, TypeId type_id = LMD_TYPE_ANY);
JsMirVarEntry* jm_install_fresh_var_entry(JsMirTranspiler* mt, int depth,
    JsVarScopeEntry* entry);
JsMirVarEntry* jm_find_var(JsMirTranspiler* mt, const char* name);
uint64_t jm_name_hash(const void* item, uint64_t seed0, uint64_t seed1);
int jm_name_cmp(const void* a, const void* b, void* udata);
void jm_name_set_add(struct hashmap* set, const char* name);
void jm_name_set_add_kind(struct hashmap* set, const char* name, int kind);
bool jm_name_set_has(struct hashmap* set, const char* name);
int jm_count_yields(JsAstNode* node);
int jm_gen_spill_save(JsMirTranspiler* mt, MIR_reg_t reg);
void jm_gen_spill_load(JsMirTranspiler* mt, MIR_reg_t reg, int slot);
bool jm_has_yield(JsAstNode* node);
bool jm_has_optional_chain(JsAstNode* node);
int jm_count_awaits(JsAstNode* node);
void jm_collect_func_assignments(JsAstNode* node, struct hashmap* names);
void jm_collect_arrow_lexical_refs(JsAstNode* node, struct hashmap* refs);
void jm_collect_body_refs(JsAstNode* node, struct hashmap* refs);
void jm_collect_body_locals(JsAstNode* node, struct hashmap* locals, bool var_only = false);
void jm_collect_let_const_names(JsAstNode* block, struct hashmap* names);
void jm_collect_switch_lexical_names(JsAstNode* switch_node, struct hashmap* names);
void jm_collect_all_let_const_names_recursive(JsAstNode* node, struct hashmap* names);
void jm_init_block_tdz(JsMirTranspiler* mt, JsAstNode* block);
void jm_init_switch_tdz(JsMirTranspiler* mt, JsAstNode* switch_node);
void jm_collect_pattern_names(JsAstNode* pat, struct hashmap* names);
void jm_collect_pattern_names_kind(JsAstNode* pat, struct hashmap* names, int var_kind);
void jm_writeback_scope_env_pattern_bindings(JsMirTranspiler* mt, JsAstNode* pattern);
void jm_collect_param_default_refs(JsAstNode* params, struct hashmap* refs);
void jm_analyze_captures(JsFuncCollected* fc, struct hashmap* outer_scope_names,
                                struct hashmap* module_consts,
                                struct hashmap* ancestor_func_locals);
JsMirImportEntry* jm_ensure_import(JsMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres);
JsMirImportEntry* jm_ensure_import_ii_i(JsMirTranspiler* mt, const char* name);
JsMirImportEntry* jm_ensure_import_i_i(JsMirTranspiler* mt, const char* name);
JsMirImportEntry* jm_ensure_import_v_i(JsMirTranspiler* mt, const char* name);
MIR_reg_t jm_call_1_or_inline(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1);
void jm_call_void_2_or_inline(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2);
#define jm_call_0(mt, fn, ret) \
    (jm_preserve_error_lane_carrier((mt), fn, true), \
     jm_publish_call_result((mt), em_call_0(&(mt)->em, fn, ret, true), fn))
#define jm_call_1(mt, fn, ret, ...) jm_call_1_or_inline(mt, fn, ret, __VA_ARGS__)
#define jm_call_2(mt, fn, ret, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, true), \
     jm_publish_call_result((mt), em_call_2(&(mt)->em, fn, ret, __VA_ARGS__, true), fn))
#define jm_call_3(mt, fn, ret, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, true), \
     jm_publish_call_result((mt), em_call_3(&(mt)->em, fn, ret, __VA_ARGS__, true), fn))
#define jm_call_4(mt, fn, ret, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, true), \
     jm_publish_call_result((mt), em_call_4(&(mt)->em, fn, ret, __VA_ARGS__, true), fn))
#define jm_call_5(mt, fn, ret, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, true), \
     jm_publish_call_result((mt), em_call_5(&(mt)->em, fn, ret, __VA_ARGS__, true), fn))
#define jm_call_6(mt, fn, ret, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, true), \
     jm_publish_call_result((mt), em_call_6(&(mt)->em, fn, ret, __VA_ARGS__, true), fn))
static inline MIR_reg_t jm_publish_call_result(JsMirTranspiler* mt,
                                                MIR_reg_t result,
                                                const char* helper_name = NULL) {
    // A MIR I64 can be either an Item or a native scalar.  The old I64-only
    // gate published raw booleans/comparisons into D8.4.3's Item lane and
    // emitted an unreachable ERROR-tag branch; use the catalog's value class.
    if (mt) {
        MIR_type_t result_type = result
            ? MIR_reg_type(mt->ctx, result, mt->em.func) : MIR_T_UNDEF;
        JitImportMetadata metadata;
        bool cataloged = helper_name && jit_import_get_metadata(helper_name, &metadata);
        bool boxed_result = !cataloged || metadata.ret_class == JIT_VALUE_UNKNOWN
            ? result_type == MIR_T_I64
            : metadata.ret_class == JIT_VALUE_BOXED_ITEM;
        if (boxed_result && result_type == MIR_T_I64) {
            mt->last_call_result_reg = result;
        } else if (!cataloged || metadata.exception_effect != JIT_EXCEPTION_PRESERVES) {
            // A PRESERVES scalar cannot replace an earlier Item carrier.  Clearing
            // it here made a known SET lane rethrow null after a scalar helper.
            mt->last_call_result_reg = 0;
        }
    }
    return result;
}
static inline void jm_preserve_error_lane_carrier(JsMirTranspiler* mt,
                                                   const char* helper_name,
                                                   bool helper_has_result) {
    if (!mt) return;
    JitImportMetadata metadata;
    bool preserves = helper_name && jit_import_get_metadata(helper_name, &metadata) &&
        metadata.exception_effect == JIT_EXCEPTION_PRESERVES;
    if (!preserves || !mt->last_call_result_reg ||
            mt->error_lane_track == JS_ERROR_LANE_CLEAN ||
            mt->error_lane_track == JS_ERROR_LANE_UNREACHABLE) return;
    if (metadata.gc_effect != JIT_EFFECT_NO_GC) {
        // A collecting PRESERVES import has no replacement Item. Keep the
        // existing carrier alive in its exact side-root slot for D8.4.3.
        jm_create_gc_root_slot(mt, mt->last_call_result_reg);
        return;
    }
    if (!helper_has_result || metadata.ret_class != JIT_VALUE_NON_GC_SCALAR) return;
    // A no-GC raw result still occupies MIR's call destination. Preserve the
    // earlier Item in a distinct register so that destination cannot alias a
    // later D8.4.3 ERROR-tag test.
    MIR_reg_t carrier = jm_new_reg(mt, "exc_carrier", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, carrier),
        MIR_new_reg_op(mt->ctx, mt->last_call_result_reg)));
    mt->last_call_result_reg = carrier;
}
MIR_reg_t jm_call_direct_boxed(JsMirTranspiler* mt, JsFuncCollected* callee,
        int arg_count, MIR_reg_t* arg_regs, bool discard_result = false);
MIR_reg_t jm_module_name_id_at_index(JsMirTranspiler* mt, uint32_t index);
MIR_reg_t jm_call_function_into(JsMirTranspiler* mt, MIR_op_t func,
        MIR_op_t this_value, MIR_op_t args, MIR_op_t arg_count);
MIR_reg_t jm_call_function_discard(JsMirTranspiler* mt, MIR_op_t func,
        MIR_op_t this_value, MIR_op_t args, MIR_op_t arg_count);
MIR_reg_t jm_apply_function_discard(JsMirTranspiler* mt, MIR_op_t func,
        MIR_op_t this_value, MIR_op_t args);
MIR_reg_t jm_construct_value_into(JsMirTranspiler* mt, MIR_op_t callee,
        MIR_op_t args, MIR_op_t arg_count, MIR_op_t new_target);
MIR_reg_t jm_apply_function_into(JsMirTranspiler* mt, MIR_op_t func,
        MIR_op_t this_value, MIR_op_t args);
MIR_reg_t jm_super_call_class_into(JsMirTranspiler* mt, MIR_op_t callee,
        MIR_op_t this_value, MIR_op_t args, MIR_op_t arg_count);
MIR_reg_t jm_super_apply_class_into(JsMirTranspiler* mt, MIR_op_t callee,
        MIR_op_t this_value, MIR_op_t args);
MIR_reg_t jm_call_direct_native(JsMirTranspiler* mt, JsFuncCollected* callee,
        int arg_count, MIR_reg_t* arg_regs);
MirValue jm_convert_rep(void* owner, MirValue value, ValueRep required);
#define jm_call_void_0(mt, fn) \
    (jm_preserve_error_lane_carrier((mt), fn, false), em_call_void_0(&(mt)->em, fn, true))
#define jm_call_void_1(mt, fn, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, false), em_call_void_1(&(mt)->em, fn, __VA_ARGS__, true))
#define jm_call_void_2(mt, fn, ...) jm_call_void_2_or_inline(mt, fn, __VA_ARGS__)
#define jm_call_void_3(mt, fn, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, false), em_call_void_3(&(mt)->em, fn, __VA_ARGS__, true))
#define jm_call_void_4(mt, fn, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, false), em_call_void_4(&(mt)->em, fn, __VA_ARGS__, true))
#define jm_call_void_5(mt, fn, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, false), em_call_void_5(&(mt)->em, fn, __VA_ARGS__, true))
#define jm_call_void_6(mt, fn, ...) \
    (jm_preserve_error_lane_carrier((mt), fn, false), em_call_void_6(&(mt)->em, fn, __VA_ARGS__, true))

// Register-operand call shorthand. The overwhelming majority of helper calls
// pass every argument as an I64 register; jm_callr_N spells that directly
// instead of repeating `MIR_T_I64, MIR_new_reg_op(mt->ctx, x)` per argument.
// Mixed sites keep the explicit jm_call_N form.
#define JM_REG(mt, r) MIR_T_I64, MIR_new_reg_op((mt)->ctx, (r))
#define jm_callr_1(mt, fn, ret, a1) jm_call_1(mt, fn, ret, JM_REG(mt, a1))
#define jm_callr_2(mt, fn, ret, a1, a2) \
    jm_call_2(mt, fn, ret, JM_REG(mt, a1), JM_REG(mt, a2))
#define jm_callr_3(mt, fn, ret, a1, a2, a3) \
    jm_call_3(mt, fn, ret, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3))
#define jm_callr_4(mt, fn, ret, a1, a2, a3, a4) \
    jm_call_4(mt, fn, ret, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3), \
        JM_REG(mt, a4))
#define jm_callr_5(mt, fn, ret, a1, a2, a3, a4, a5) \
    jm_call_5(mt, fn, ret, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3), \
        JM_REG(mt, a4), JM_REG(mt, a5))
#define jm_callr_6(mt, fn, ret, a1, a2, a3, a4, a5, a6) \
    jm_call_6(mt, fn, ret, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3), \
        JM_REG(mt, a4), JM_REG(mt, a5), JM_REG(mt, a6))
#define jm_callr_void_1(mt, fn, a1) jm_call_void_1(mt, fn, JM_REG(mt, a1))
#define jm_callr_void_2(mt, fn, a1, a2) \
    jm_call_void_2(mt, fn, JM_REG(mt, a1), JM_REG(mt, a2))
#define jm_callr_void_3(mt, fn, a1, a2, a3) \
    jm_call_void_3(mt, fn, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3))
#define jm_callr_void_4(mt, fn, a1, a2, a3, a4) \
    jm_call_void_4(mt, fn, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3), \
        JM_REG(mt, a4))
#define jm_callr_void_5(mt, fn, a1, a2, a3, a4, a5) \
    jm_call_void_5(mt, fn, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3), \
        JM_REG(mt, a4), JM_REG(mt, a5))
#define jm_callr_void_6(mt, fn, a1, a2, a3, a4, a5, a6) \
    jm_call_void_6(mt, fn, JM_REG(mt, a1), JM_REG(mt, a2), JM_REG(mt, a3), \
        JM_REG(mt, a4), JM_REG(mt, a5), JM_REG(mt, a6))
MIR_reg_t jm_emit_null(JsMirTranspiler* mt);
MIR_reg_t jm_emit_undefined(JsMirTranspiler* mt);
MIR_reg_t jm_boxed_immediate_const(JsMirTranspiler* mt, uint64_t item,
        const char* prefix);
MIR_reg_t jm_emit_item_error(JsMirTranspiler* mt);
MIR_reg_t jm_emit_error_lane_return(JsMirTranspiler* mt);
bool jm_is_native_binary_expression(JsMirTranspiler* mt, JsBinaryNode* bin);
bool jm_is_native_unary_expression(JsMirTranspiler* mt, JsUnaryNode* un);
MIR_reg_t jm_box_int_const(JsMirTranspiler* mt, int64_t value);
void jm_arguments_writeback_param(JsMirTranspiler* mt, int param_index, MIR_reg_t val_reg);
MIR_reg_t jm_box_int_reg(JsMirTranspiler* mt, MIR_reg_t val);
MIR_reg_t jm_box_int_double(JsMirTranspiler* mt, MIR_reg_t d_reg);
MIR_reg_t jm_box_float(JsMirTranspiler* mt, MIR_reg_t d_reg);
MIR_reg_t jm_box_string(JsMirTranspiler* mt, MIR_reg_t ptr_reg);
MIR_reg_t jm_string_literal_chars(JsMirTranspiler* mt, const char* str, int len);
uint32_t jm_module_name_index(JsMirTranspiler* mt, const char* chars, uint32_t length);
uint32_t jm_module_name_append(JsMirTranspiler* mt, const char* chars, uint32_t length);
MIR_reg_t jm_module_name_id(JsMirTranspiler* mt, const char* chars, uint32_t length);
MIR_reg_t jm_box_property_name_literal(JsMirTranspiler* mt,
        const char* chars, uint32_t length);
MIR_reg_t jm_box_string_literal(JsMirTranspiler* mt, const char* str, int len);
void jm_emit_install_method_or_accessor(JsMirTranspiler* mt,
    MIR_reg_t obj, MIR_reg_t key, MIR_reg_t fn_item,
    bool is_getter, bool is_setter);
void jm_emit_set_function_name(JsMirTranspiler* mt, MIR_reg_t fn_reg, const char* name, int formal_length = -1);
void jm_emit_set_class_assignment_name(JsMirTranspiler* mt, JsAssignmentNode* asgn, MIR_reg_t rhs, String* name);
void jm_emit_set_function_source(JsMirTranspiler* mt, MIR_reg_t fn_reg, JsFunctionNode* fn_node);
void jm_emit_set_class_source(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassNode* cls_node);
MIR_reg_t jm_emit_class_object_for_entry(JsMirTranspiler* mt, JsClassEntry* ce);
MIR_reg_t jm_link_static_super_prototype(JsMirTranspiler* mt,
        MIR_reg_t cls_obj, MIR_reg_t proto_obj, JsClassEntry* static_superclass);
MIR_reg_t jm_emit_current_class_prototype(JsMirTranspiler* mt, MIR_reg_t cls_obj,
        MIR_reg_t fallback_proto);
void jm_emit_set_private_class_index(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce);
void jm_emit_class_instance_field_metadata(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce);
void jm_emit_class_instance_computed_field_metadata_keys(JsMirTranspiler* mt,
    MIR_reg_t cls_obj, JsClassEntry* ce);
void jm_emit_class_computed_field_module_keys(JsMirTranspiler* mt,
    MIR_reg_t cls_obj, JsClassEntry* ce);
void jm_emit_set_function_home_class(JsMirTranspiler* mt, MIR_reg_t fn_item, MIR_reg_t cls_obj);
bool jm_emit_class_method_install(JsMirTranspiler* mt,
    const JsMirClassMethodInstallPolicy* policy);
void jm_emit_class_constructor_property(JsMirTranspiler* mt, MIR_reg_t cls_obj,
    JsClassEntry* ce, bool set_home_class);
void jm_emit_class_self_extends_check(JsMirTranspiler* mt, JsClassEntry* ce,
    String* class_name);
MIR_reg_t jm_emit_class_prototype_chain(JsMirTranspiler* mt, JsClassEntry* ce,
    MIR_reg_t cls_obj, JsAstNode* heritage, JsClassEntry* static_superclass, MIR_reg_t proto_obj,
    MIR_reg_t checked_heritage_val, bool* heritage_is_null_out);
void jm_emit_class_length_property(JsMirTranspiler* mt, MIR_reg_t cls_obj,
    JsClassEntry* ce);
void jm_emit_begin_lexical_this_rebind(JsMirTranspiler* mt, MIR_reg_t value,
    JsMirLexicalThisRebind* state, bool restore_binding);
void jm_emit_end_lexical_this_rebind(JsMirTranspiler* mt,
    const JsMirLexicalThisRebind* state);
MIR_reg_t jm_emit_unbox_int(JsMirTranspiler* mt, MIR_reg_t item);
MIR_reg_t jm_emit_unbox_float(JsMirTranspiler* mt, MIR_reg_t item);
MIR_reg_t jm_emit_int_to_double(JsMirTranspiler* mt, MIR_reg_t int_reg);
MIR_reg_t jm_emit_double_to_int(JsMirTranspiler* mt, MIR_reg_t d_reg);
MIR_reg_t jm_ensure_native_int(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type);
MIR_reg_t jm_ensure_native_float(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type);
MIR_reg_t jm_box_native(JsMirTranspiler* mt, MIR_reg_t reg, TypeId type_id);
MIR_reg_t jm_ensure_boxed(JsMirTranspiler* mt, MIR_reg_t reg);
TypeId jm_get_effective_type(JsMirTranspiler* mt, JsAstNode* node);
JsClassEntry* jm_matching_static_superclass(JsClassEntry* ce, JsAstNode* heritage);
Type* jm_get_full_type(JsMirTranspiler* mt, JsAstNode* node);

// --- AST constant folding (Tune3 §3) ---
// Compile-time folding of subtrees that reduce to a numeric or boolean constant
// (e.g. `-1 << 16`, `(a) !== b` with literal a/b). Results are bit-identical to
// the runtime arithmetic; folding bails (returns false) on any case that could
// diverge (non-finite results, bigint, int overflow past 2^53, unsupported ops).
enum JsFoldKind { JS_FOLD_NUM, JS_FOLD_BOOL };
struct JsFoldVal {
    JsFoldKind kind;
    double num;     // valid when kind == JS_FOLD_NUM
    bool boolean;   // valid when kind == JS_FOLD_BOOL
    bool is_float;  // when kind == JS_FOLD_NUM: emit as float (vs int) — matches runtime type
};
bool jm_const_fold_enabled();
bool jm_try_fold_const(JsAstNode* node, JsFoldVal* out);
bool jm_is_native_type(TypeId tid);
void jm_scope_env_mark_and_writeback(JsMirTranspiler* mt, const char* name, MIR_reg_t val_reg, TypeId type_id = LMD_TYPE_ANY);
void jm_scope_env_mark_and_writeback_binding(JsMirTranspiler* mt, const char* name,
    JsAstNode* binding_node, MIR_reg_t val_reg, TypeId type_id = LMD_TYPE_ANY);
MIR_reg_t jm_emit_is_truthy(JsMirTranspiler* mt, MIR_reg_t val, JsAstNode* expr);
MIR_reg_t jm_transpile_as_native(JsMirTranspiler* mt, JsAstNode* expr,
                                         TypeId expr_type, TypeId target_type);
MIR_reg_t jm_transpile_conditional_as_native(JsMirTranspiler* mt,
                                             JsConditionalNode* cond,
                                             TypeId target_type);
JsFuncCollected* jm_find_collected_func_for_call(JsMirTranspiler* mt, JsCallNode* call);
JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call);
bool jm_is_recursive_call(JsCallNode* call, JsFuncCollected* fc);
bool jm_call_result_uses_native_register(JsMirTranspiler* mt, JsCallNode* call, JsFuncCollected* fc);
bool jm_has_tail_call(JsAstNode* node, JsFuncCollected* fc);
void jm_register_local_func(JsMirTranspiler* mt, const char* name, MIR_item_t func_item);
const char* jm_make_fn_name(JsFunctionNode* fn, JsMirTranspiler* mt);
int jm_count_params(JsFunctionNode* fn);
int jm_formal_length(JsFunctionNode* fn);
JsIdentifierNode* jm_get_param_identifier(JsAstNode* param_node);
const char* jm_get_param_name(JsAstNode* param_node, int index);
// Backend-only formal names are compiler-owned and independent of source
// spelling; semantic JS binding names continue to use jm_get_param_name.
static inline void jm_get_backend_param_name(int index, char* out, int out_size) {
    mir_format_backend_name(out, (size_t)out_size, 'p', (uint64_t)index);
}
static inline bool jm_js_name_equal(const String* left, const String* right) {
    return left && right && left->len == right->len &&
        memcmp(left->chars, right->chars, left->len) == 0;
}
static inline const String* jm_param_binding_name(JsAstNode* param_node) {
    JsIdentifierNode* identifier = jm_get_param_identifier(param_node);
    return identifier ? identifier->name : NULL;
}
void jm_collect_functions(JsMirTranspiler* mt, JsAstNode* node);
JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn);
bool jm_func_has_param_named(JsFunctionNode* fn, const char* name, int name_len);
TypeId jm_detect_ctor_field_type(JsAstNode* rhs);
void jm_scan_ctor_props(JsFuncCollected* fc, JsAstNode* body);
JsClassEntry* jm_find_class(JsMirTranspiler* mt, const char* name, int name_len);
void jm_infer_walk(JsAstNode* node, const String* const binding_names[],
                          FnParamEvidence* evidence, int param_count,
                          const char* self_name);
void jm_infer_param_types(JsFuncCollected* fc);
bool jm_add_chain_has_string(JsAstNode* expr);
void jm_infer_return_type_walk(JsAstNode* node, const char* self_name,
                                       TypeId* collected, int* count, int max_count);
void jm_infer_return_type(JsFuncCollected* fc);
ScalarReturnClass jm_infer_boxed_return_scalar_class(JsFuncCollected* fc);
void jm_emit_finalize_function(JsMirTranspiler* mt, MIR_reg_t fn_reg,
    JsFuncCollected* fc, JsFunctionNode* fn_node);
bool jm_expression_has_float_hint(JsAstNode* node);
bool jm_prescan_is_float_array(struct hashmap* float_arrays, const char* name);
bool jm_prescan_has_float_array_access(JsAstNode* node, struct hashmap* float_arrays);
void jm_prescan_widen_walk(JsAstNode* node, struct hashmap* float_arrays,
                                   struct hashmap* widen_vars);
void jm_prescan_float_widening(JsMirTranspiler* mt, JsAstNode* body);
bool jm_should_widen_to_float(JsMirTranspiler* mt, const char* vname);
MIR_reg_t jm_build_args_array(JsMirTranspiler* mt, JsAstNode* first_arg, int arg_count);
MIR_reg_t jm_build_spread_args_array(JsMirTranspiler* mt, JsAstNode* first_arg);
int jm_count_args(JsAstNode* arg);
MIR_reg_t jm_create_method_function(JsMirTranspiler* mt, JsFuncCollected* fc, int param_count);
MIR_reg_t jm_transpile_literal(JsMirTranspiler* mt, JsLiteralNode* lit);
MIR_reg_t jm_transpile_identifier(JsMirTranspiler* mt, JsIdentifierNode* id);
MIR_reg_t jm_transpile_binary(JsMirTranspiler* mt, JsBinaryNode* bin);
MIR_reg_t jm_transpile_unary(JsMirTranspiler* mt, JsUnaryNode* un);
void jm_bind_destructure_var(JsMirTranspiler* mt, const char* vname, MIR_reg_t val);
MIR_reg_t jm_emit_destructure_default(JsMirTranspiler* mt, MIR_reg_t val, JsAstNode* default_expr);
void jm_emit_destructure_target(JsMirTranspiler* mt, JsAstNode* target, MIR_reg_t val);
void jm_emit_array_destructure(JsMirTranspiler* mt, JsAstNode* pattern_node, MIR_reg_t src);
void jm_emit_object_destructure(JsMirTranspiler* mt, JsAstNode* pattern_node, MIR_reg_t src);
MIR_reg_t jm_transpile_assignment(JsMirTranspiler* mt, JsAssignmentNode* asgn);
void jm_readback_closure_env(JsMirTranspiler* mt);
bool jm_resolve_transitive_capture_env(JsMirVarEntry* var,
    MIR_reg_t* env_reg, int* env_slot);
void jm_write_last_closure_capture_if_matching(JsMirTranspiler* mt,
        const char* name, MIR_reg_t val_reg, TypeId type_id = LMD_TYPE_ANY);
MIR_reg_t jm_transpile_call(JsMirTranspiler* mt, JsCallNode* call);
MIR_reg_t jm_transpile_member(JsMirTranspiler* mt, JsMemberNode* mem);
MIR_reg_t jm_transpile_array(JsMirTranspiler* mt, JsArrayNode* arr);
MIR_reg_t jm_transpile_object(JsMirTranspiler* mt, JsObjectNode* obj);
MIR_reg_t jm_transpile_conditional(JsMirTranspiler* mt, JsConditionalNode* cond);
MIR_reg_t jm_transpile_template_literal(JsMirTranspiler* mt, JsTemplateLiteralNode* tmpl);
MIR_reg_t jm_transpile_tagged_template(JsMirTranspiler* mt, JsTaggedTemplateNode* tt);
MIR_reg_t jm_create_func_or_closure(JsMirTranspiler* mt, JsFuncCollected* fc);
MIR_reg_t jm_emit_module_const_value(JsMirTranspiler* mt,
    const JsModuleConstEntry* mc);
bool jm_capture_uses_live_module_var(JsMirTranspiler* mt, FnCapture* capture);
bool jm_capture_is_lexical_meta_binding(const char* name);
int jm_capture_env_slot(FnCapture* capture, int dense_slot);
MIR_reg_t jm_transpile_func_expr(JsMirTranspiler* mt, JsFunctionNode* fn);
void jm_emit_class_static_property(JsMirTranspiler* mt, MIR_reg_t cls_obj,
    MIR_reg_t key, MIR_reg_t value, bool private_brand);
MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item);
MIR_reg_t jm_transpile_condition(JsMirTranspiler* mt, JsAstNode* expr);
MIR_reg_t jm_transpile_expression(JsMirTranspiler* mt, JsAstNode* expr);
void jm_transpile_var_decl(JsMirTranspiler* mt, JsVariableDeclarationNode* var);
JsIdentifierNode* jm_detect_typeof_pattern(JsAstNode* test,
                                                    TypeId* narrowed_type, bool* negate);
bool jm_push_typeof_narrow(JsMirTranspiler* mt, JsIdentifierNode* id, TypeId narrowed_type);
void jm_transpile_if(JsMirTranspiler* mt, JsIfNode* if_node);
void jm_scope_env_reload_vars(JsMirTranspiler* mt);
void jm_env_reload_shared_captures(JsMirTranspiler* mt);
void jm_emit_error_lane_propagate_check(JsMirTranspiler* mt);
void jm_emit_class_static_field(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce, JsStaticFieldEntry* sf);
void jm_emit_class_static_block(JsMirTranspiler* mt, MIR_reg_t cls_obj,
    JsClassEntry* ce, JsAstNode* block);
bool jm_emit_class_static_source_order(JsMirTranspiler* mt, MIR_reg_t cls_obj,
    JsClassEntry* ce);
void jm_emit_class_static_initializers(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce,
    MIR_reg_t ctor_super_val);
typedef struct JsMirClassSetup {
    MIR_reg_t ctor_super_val;
    MIR_reg_t class_proto_obj;
    JsAstNode* heritage;
    JsClassEntry* static_superclass;
} JsMirClassSetup;
void jm_emit_class_setup(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce,
    JsAstNode* class_node, bool computed_key_before_function, JsMirClassSetup* setup);
void jm_emit_class_instance_setup_tail(JsMirTranspiler* mt, MIR_reg_t cls_obj,
    JsClassEntry* ce, MIR_reg_t proto_obj, MIR_reg_t ctor_super_val, bool heritage_is_null);
void jm_transpile_while(JsMirTranspiler* mt, JsWhileNode* wh);
void jm_transpile_for(JsMirTranspiler* mt, JsForNode* for_node);
MIR_reg_t jm_build_closure_for_method(JsMirTranspiler* mt, JsFuncCollected* fc, int param_count);
MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call);
void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw);
void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw);
void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo);
void jm_transpile_return(JsMirTranspiler* mt, JsReturnNode* ret);
void jm_transpile_statement(JsMirTranspiler* mt, JsAstNode* stmt);
void jm_transpile_statement_list_with_using(JsMirTranspiler* mt, JsAstNode* first);
void jm_define_function(JsMirTranspiler* mt, JsFuncCollected* fc);
bool jm_try_eval_const_expr(JsMirTranspiler* mt, JsAstNode* node, double* result);
void jm_track_active_js_transpile(JsTranspiler* tp, JsMirTranspiler* mt, char* owned_source);
void jm_clear_active_js_transpile(JsTranspiler* tp, JsMirTranspiler* mt, char* owned_source);
void jm_cleanup_active_mir(void);
void jm_abandon_active_mir_after_signal(void);
void jm_defer_mir_cleanup(MIR_context_t ctx);
void jm_resolve_module_path(const char* base_file, const char* specifier, int spec_len,
                                   char* out, int out_size);
void jm_emit_module_export(JsMirTranspiler* mt, const char* name, int name_len,
                                  bool is_default);
// Js52 P1: aliased export — resolve via local_name, publish under export_name.
void jm_emit_module_export_aliased(JsMirTranspiler* mt,
                                          const char* local_name, int local_len,
                                          const char* export_name, int export_len);
// Js52 R1: closure env size accounting for remapped scope_env_slot captures.
int jm_closure_env_alloc_size(JsMirTranspiler* mt, JsFuncCollected* fc, bool has_remapped);
void jm_callsite_scan_node(JsMirTranspiler* mt, JsAstNode* node);
void jm_callsite_propagate(JsMirTranspiler* mt, JsAstNode* program_body);
void jm_emit_eval_local_ensure_frame(JsMirTranspiler* mt);
void jm_emit_eval_local_pop_if_needed(JsMirTranspiler* mt);
bool jm_scope_env_name_matches_binding(const char* scope_name, const char* name,
    JsAstNode* binding_node);
bool transpile_js_mir_ast(JsMirTranspiler* mt, JsAstNode* root);
uint64_t js_path_index_hash(const void* item, uint64_t seed0, uint64_t seed1);
int js_path_index_compare(const void* a, const void* b, void* udata);
void jm_add_dep(JsImportGraphNode* nodes, int parent_idx, int dep_idx);
void jm_discover_js_imports_recursive(
    TSParser* parser, int parent_idx,
    JsImportGraphNode** nodes, int* count, int* capacity,
    struct hashmap* path_map);
int jm_compute_depth(JsImportGraphNode* nodes, int idx);
bool jm_validate_mir_labels(MIR_context_t ctx);
bool jm_compile_js_module(Runtime* runtime, JsImportGraphNode* node);
void* jm_compile_js_worker(void* arg);
int jm_precompile_js_imports(Runtime* runtime, const char* js_source, const char* filename);
bool js_activate_runtime_name_pool(void);
bool js_prelink_compiled_name_table(const JsMirTranspiler* mt);
Item transpile_js_module_to_mir(Runtime* runtime, const char* js_source, const char* filename);
void jm_load_imports(Runtime* runtime, JsAstNode* ast, const char* filename);
extern "C" Item js_new_function_from_string(Item* args, int argc);
char* eval_try_insert_return(const char* code, size_t len);
extern "C" Item js_builtin_eval(Item code_item, int64_t is_global_scope);
void js_normalize_path_separators(char* path);
Item transpile_js_to_mir_core(Runtime* runtime, const char* js_source, const char* filename,
                              uint64_t* result_home);
Item transpile_js_to_mir_core_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                  const char* filename, uint64_t* result_home);
Item load_js_module(Runtime* runtime, const char* js_path);
bool js_is_cjs_file(const char* path);
char* js_wrap_cjs_source(const char* source, const char* filename);
extern "C" Item js_require(Item specifier);
extern "C" Item js_dynamic_import(Item specifier);
