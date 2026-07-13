#pragma once

// js_mir_internal.hpp - shared declarations for the split JS MIR transpiler.

#include "js_mir_context.hpp"

extern "C" void log_mem_stage(const char* stage);
extern "C" Context* _lambda_rt;
extern "C" void *import_resolver(const char *name);
extern __thread EvalContext* context;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();
extern "C" void js_reset_module_vars();
extern "C" Item* js_alloc_module_vars(void);
extern "C" Item* js_get_active_module_vars(void);
extern "C" void js_set_active_module_vars(Item* vars);
extern void js_double_to_string(double d, char* out, int out_size);
extern "C" void js_process_emit_before_exit(int code);
extern "C" void js_process_emit_exit(int code);
extern MIR_error_func_t g_batch_mir_error_handler;
extern unsigned int g_js_mir_optimize_level;
extern int g_js_force_document_interp;
#define JM_LARGE_FUNC_INSN_THRESHOLD 10000
#define JM_LARGE_MODULE_INSN_THRESHOLD 100000
// Tune6: in a document/Radiant context (cold vendor JS), use the MIR interpreter
// for modules above this (moderate) insn count — see Transpile_Js_Tune6_AST.md §0.2d.
#define JM_RADIANT_INTERP_INSN_THRESHOLD 20000
extern "C" int g_mir_interp_mode;
extern "C" const TSLanguage* tree_sitter_typescript(void);
extern "C" const TSLanguage* tree_sitter_javascript(void);
extern "C" void ensure_jit_imports_initialized(void);

extern JsModuleConstEntry* g_eval_preamble_entries;
extern int g_eval_preamble_entry_count;
extern int g_eval_preamble_var_count;
extern bool g_jm_preamble_mode;
extern JsPreambleState* g_jm_preamble_out;
extern const JsPreambleState* g_jm_preamble_in;
extern Runtime* js_source_runtime;
extern int js_dynamic_func_counter;
extern MIR_context_t g_active_mir_ctx;
extern JsTranspiler* g_active_js_transpiler;
extern JsMirTranspiler* g_active_mir_transpiler;
extern char* g_active_js_owned_source;
extern MIR_context_t module_mir_contexts[];
extern NamePool* module_mir_name_pools[];
extern Pool* module_mir_ast_pools[];
extern char* module_mir_source_buffers[];
extern int module_mir_context_count;
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
    const char* named_key;
    int named_key_len;
    uint64_t named_key_item;
    const char* profile_label;
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

// internal function declarations
int js_var_scope_cmp(const void *a, const void *b, void *udata);
uint64_t js_var_scope_hash(const void *item, uint64_t seed0, uint64_t seed1);
int js_local_func_cmp(const void *a, const void *b, void *udata);
uint64_t js_local_func_hash(const void *item, uint64_t seed0, uint64_t seed1);
int js_module_const_cmp(const void *a, const void *b, void *udata);
uint64_t js_module_const_hash(const void *item, uint64_t seed0, uint64_t seed1);
JsMirTranspiler* jm_create_mir_transpiler(
    JsTranspiler* tp, MIR_context_t ctx, const char* filename, bool is_module,
    int import_capacity, int local_func_capacity, int var_scope_capacity,
    const char* log_prefix);
void jm_destroy_mir_transpiler(JsMirTranspiler* mt);
MIR_reg_t jm_new_reg(JsMirTranspiler* mt, const char* prefix, MIR_type_t type);
MIR_label_t jm_new_label(JsMirTranspiler* mt);
void jm_emit(JsMirTranspiler* mt, MIR_insn_t insn);
// Tune6 §3.3: per-opcode emission histogram (env JS_MIR_OPCODE_HIST)
void jm_opcode_hist_set_enabled(int enabled);
void jm_opcode_hist_reset(void);
void jm_opcode_hist_dump(MIR_context_t ctx, const char* label);
void jm_emit_label(JsMirTranspiler* mt, MIR_label_t label);
JsMirReference jm_emit_reference(JsMirTranspiler* mt, JsAstNode* node);
MIR_reg_t jm_emit_get_value(JsMirTranspiler* mt, const JsMirReference* ref);
MIR_reg_t jm_emit_put_value(JsMirTranspiler* mt, const JsMirReference* ref, MIR_reg_t value);
MIR_reg_t jm_emit_delete_reference(JsMirTranspiler* mt, const JsMirReference* ref);
bool jm_is_private_name(String* name);
String* jm_class_private_name(JsMirTranspiler* mt, JsClassEntry* ce, String* name);
void jm_eval_cptn_reset(JsMirTranspiler* mt);
void jm_push_loop_labels(JsMirTranspiler* mt, MIR_label_t continue_label, MIR_label_t break_label);
MIR_reg_t jm_emit_get_iterator(JsMirTranspiler* mt, MIR_reg_t iterable);
MIR_reg_t jm_emit_get_iterator_lazy(JsMirTranspiler* mt, MIR_reg_t iterable);
MIR_reg_t jm_emit_iterator_step(JsMirTranspiler* mt, MIR_reg_t iterator);
MIR_reg_t jm_emit_iterator_done_test(JsMirTranspiler* mt, MIR_reg_t step_result, const char* prefix);
MIR_reg_t jm_emit_iterator_collect_rest(JsMirTranspiler* mt, MIR_reg_t iterator);
void jm_emit_iterator_close(JsMirTranspiler* mt, MIR_reg_t iterator);
void jm_emit_iterator_close_on_exception(JsMirTranspiler* mt, MIR_reg_t iterator, MIR_label_t target);
void jm_emit_iterator_close_on_exception_if_open(JsMirTranspiler* mt, MIR_reg_t iterator,
    MIR_reg_t iter_done, MIR_label_t target);
void jm_emit_abrupt_jump_cleanup(JsMirTranspiler* mt);
void jm_emit_break_completion(JsMirTranspiler* mt, JsBreakContinueNode* brk);
void jm_emit_continue_completion(JsMirTranspiler* mt, JsBreakContinueNode* cont);
MIR_reg_t jm_emit_uext8(JsMirTranspiler* mt, MIR_reg_t r);
void jm_push_scope(JsMirTranspiler* mt);
int jm_arguments_param_index(JsMirTranspiler* mt, const char* vname);
bool jm_has_use_strict_directive(JsFunctionNode* fn);
void jm_pop_scope(JsMirTranspiler* mt);
void jm_set_var(JsMirTranspiler* mt, const char* name, MIR_reg_t reg,
                       MIR_type_t mir_type = MIR_T_I64, TypeId type_id = LMD_TYPE_ANY);
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
void jm_collect_param_default_refs(JsAstNode* params, struct hashmap* refs);
void jm_analyze_captures(JsFuncCollected* fc, struct hashmap* outer_scope_names,
                                struct hashmap* module_consts,
                                struct hashmap* ancestor_func_locals);
JsMirImportEntry* jm_ensure_import(JsMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres);
JsMirImportEntry* jm_ensure_import_ii_i(JsMirTranspiler* mt, const char* name);
JsMirImportEntry* jm_ensure_import_i_i(JsMirTranspiler* mt, const char* name);
JsMirImportEntry* jm_ensure_import_v_i(JsMirTranspiler* mt, const char* name);
MIR_reg_t jm_call_0(JsMirTranspiler* mt, const char* fn_name, MIR_type_t ret_type);
MIR_reg_t jm_call_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1);
MIR_reg_t jm_call_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2);
MIR_reg_t jm_call_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3);
MIR_reg_t jm_call_4(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4);
MIR_reg_t jm_call_5(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5);
MIR_reg_t jm_call_6(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5,
    MIR_type_t a6t, MIR_op_t a6);
void jm_call_void_0(JsMirTranspiler* mt, const char* fn_name);
void jm_call_void_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1);
void jm_call_void_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2);
void jm_call_void_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3);
void jm_call_void_4(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3, MIR_type_t a4t, MIR_op_t a4);
void jm_call_void_5(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3, MIR_type_t a4t, MIR_op_t a4,
    MIR_type_t a5t, MIR_op_t a5);
MIR_reg_t jm_emit_null(JsMirTranspiler* mt);
MIR_reg_t jm_emit_undefined(JsMirTranspiler* mt);
MIR_reg_t jm_emit_item_error(JsMirTranspiler* mt);
MIR_reg_t jm_box_int_const(JsMirTranspiler* mt, int64_t value);
void jm_arguments_writeback_param(JsMirTranspiler* mt, int param_index, MIR_reg_t val_reg);
MIR_reg_t jm_box_int_reg(JsMirTranspiler* mt, MIR_reg_t val);
MIR_reg_t jm_box_float(JsMirTranspiler* mt, MIR_reg_t d_reg);
MIR_reg_t jm_box_string(JsMirTranspiler* mt, MIR_reg_t ptr_reg);
MIR_reg_t jm_box_string_literal(JsMirTranspiler* mt, const char* str, int len);
void jm_emit_install_method_or_accessor(JsMirTranspiler* mt,
    MIR_reg_t obj, MIR_reg_t key, MIR_reg_t fn_item,
    bool is_getter, bool is_setter);
void jm_emit_set_function_name(JsMirTranspiler* mt, MIR_reg_t fn_reg, const char* name, int formal_length = -1);
void jm_emit_set_class_assignment_name(JsMirTranspiler* mt, JsAssignmentNode* asgn, MIR_reg_t rhs, String* name);
void jm_emit_set_function_source(JsMirTranspiler* mt, MIR_reg_t fn_reg, JsFunctionNode* fn_node);
void jm_emit_set_class_source(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassNode* cls_node);
void jm_emit_begin_lexical_this_rebind(JsMirTranspiler* mt, MIR_reg_t value,
    JsMirLexicalThisRebind* state, bool restore_binding);
void jm_emit_end_lexical_this_rebind(JsMirTranspiler* mt,
    const JsMirLexicalThisRebind* state);
void jm_emit_class_ctor_shape_metadata(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce);
void jm_emit_formal_length(JsMirTranspiler* mt, MIR_reg_t fn_reg, int formal_length);
MIR_reg_t jm_build_error_stack_string(JsMirTranspiler* mt, const char* error_type);
MIR_reg_t jm_emit_unbox_int(JsMirTranspiler* mt, MIR_reg_t item);
MIR_reg_t jm_emit_unbox_float(JsMirTranspiler* mt, MIR_reg_t item);
MIR_reg_t jm_emit_int_to_double(JsMirTranspiler* mt, MIR_reg_t int_reg);
MIR_reg_t jm_emit_double_to_int(JsMirTranspiler* mt, MIR_reg_t d_reg);
MIR_reg_t jm_ensure_native_int(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type);
MIR_reg_t jm_ensure_native_float(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type);
MIR_reg_t jm_box_native(JsMirTranspiler* mt, MIR_reg_t reg, TypeId type_id);
MIR_reg_t jm_ensure_boxed(JsMirTranspiler* mt, MIR_reg_t reg);
MIR_reg_t jm_transpile_math_native(JsMirTranspiler* mt, JsCallNode* call,
                                            String* method, TypeId target_type);;
MIR_reg_t jm_transpile_typed_array_get(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                               MIR_reg_t idx_native, int ta_type,
                                               MIR_reg_t h_data = 0, MIR_reg_t h_len = 0);;
MIR_reg_t jm_transpile_typed_array_get_native(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                                      MIR_reg_t idx_native, int ta_type,
                                                      TypeId target_type,
                                                      MIR_reg_t h_data = 0);;
MIR_reg_t jm_transpile_typed_array_set(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                               MIR_reg_t idx_native, MIR_reg_t val_boxed,
                                               int ta_type,
                                               MIR_reg_t h_data = 0, MIR_reg_t h_len = 0);;
MIR_reg_t jm_transpile_array_get_inline(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                                MIR_reg_t idx_native,
                                                MIR_reg_t h_items = 0, MIR_reg_t h_len = 0);;
TypeId jm_get_effective_type(JsMirTranspiler* mt, JsAstNode* node);
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
MIR_item_t jm_find_local_func(JsMirTranspiler* mt, const char* name);
void jm_register_local_func(JsMirTranspiler* mt, const char* name, MIR_item_t func_item);
void jm_make_fn_name(char* buf, int bufsize, JsFunctionNode* fn, JsMirTranspiler* mt);
int jm_count_params(JsFunctionNode* fn);
int jm_formal_length(JsFunctionNode* fn);
void jm_get_param_name(JsAstNode* param_node, int index, char* out, int out_size);
void jm_resolve_module_path(const char* base_file, const char* specifier, int spec_len,
                                   char* out, int out_size);;
void jm_collect_functions(JsMirTranspiler* mt, JsAstNode* node);
JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn);
bool jm_func_has_param_named(JsFunctionNode* fn, const char* name, int name_len);
int jm_ctor_prop_slot(JsFuncCollected* fc, const char* prop_name, int prop_len);
void jm_collect_var_fields_walk(JsAstNode* node, const char* varname, int varlen,
                                       char fields[][64], int* count, int max_fields);
JsClassEntry* jm_match_class_from_fields(JsMirTranspiler* mt,
                                                  char fields[][64], int field_count);
void jm_scan_subscript_arrays(JsAstNode* node, char names[][64], bool unsafe[],
                                      int* count, int max_names);
int jm_detect_typed_array_new(JsAstNode* rhs);
int jm_class_field_ta_type(JsClassEntry* ce, const char* prop_name, int prop_len);
TypeId jm_detect_ctor_field_type(JsAstNode* rhs);
void jm_scan_ctor_props(JsFuncCollected* fc, JsAstNode* body);
JsClassEntry* jm_find_class(JsMirTranspiler* mt, const char* name, int name_len);
void jm_infer_walk(JsAstNode* node, const char param_names[][128],
                          FnParamEvidence* evidence, int param_count,
                          const char* self_name);
void jm_infer_param_types(JsFuncCollected* fc);
bool jm_add_chain_has_string(JsAstNode* expr);
void jm_infer_return_type_walk(JsAstNode* node, const char* self_name,
                                       TypeId* collected, int* count, int max_count);
void jm_infer_return_type(JsFuncCollected* fc);
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
bool jm_is_console_log(JsCallNode* call);
bool jm_is_math_call(JsCallNode* call);
MIR_reg_t jm_transpile_math_call(JsMirTranspiler* mt, JsCallNode* call, String* method);
TypeId jm_math_return_type(String* method, JsMirTranspiler* mt, JsAstNode* arg0);
MIR_reg_t jm_transpile_math_native(JsMirTranspiler* mt, JsCallNode* call,
                                            String* method, TypeId target_type);
String* jm_get_math_method(JsCallNode* call);
void jm_readback_closure_env(JsMirTranspiler* mt);
void jm_write_last_closure_capture_if_matching(JsMirTranspiler* mt,
        const char* name, MIR_reg_t val_reg, TypeId type_id = LMD_TYPE_ANY);
bool jm_should_inline(JsFuncCollected* fc);
MIR_reg_t jm_transpile_inline_native(JsMirTranspiler* mt, JsCallNode* call, JsFuncCollected* fc);
MIR_reg_t jm_transpile_call(JsMirTranspiler* mt, JsCallNode* call);
int jm_typed_array_elem_shift(int ta_type);
int jm_typed_array_elem_size(int ta_type);
JsMirVarEntry* jm_get_typed_array_var(JsMirTranspiler* mt, JsAstNode* obj_node);
JsMirVarEntry* jm_get_js_array_var(JsMirTranspiler* mt, JsAstNode* obj_node);
MIR_reg_t jm_transpile_array_get_inline(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                                 MIR_reg_t idx_native,
                                                 MIR_reg_t h_items, MIR_reg_t h_len);
bool jm_typed_array_is_int(int ta_type);
MIR_reg_t jm_transpile_typed_array_get(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                               MIR_reg_t idx_native, int ta_type,
                                               MIR_reg_t h_data, MIR_reg_t h_len);
MIR_reg_t jm_transpile_typed_array_get_native(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                                      MIR_reg_t idx_native, int ta_type,
                                                      TypeId target_type,
                                                      MIR_reg_t h_data);
MIR_reg_t jm_transpile_typed_array_set(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                               MIR_reg_t idx_native, MIR_reg_t val_boxed,
                                               int ta_type,
                                               MIR_reg_t h_data, MIR_reg_t h_len);
MIR_reg_t jm_transpile_typed_array_length(JsMirTranspiler* mt, MIR_reg_t arr_reg);
MIR_reg_t jm_transpile_member(JsMirTranspiler* mt, JsMemberNode* mem);
MIR_reg_t jm_transpile_array(JsMirTranspiler* mt, JsArrayNode* arr);
MIR_reg_t jm_transpile_object(JsMirTranspiler* mt, JsObjectNode* obj);
MIR_reg_t jm_transpile_conditional(JsMirTranspiler* mt, JsConditionalNode* cond);
MIR_reg_t jm_transpile_template_literal(JsMirTranspiler* mt, JsTemplateLiteralNode* tmpl);
MIR_reg_t jm_transpile_tagged_template(JsMirTranspiler* mt, JsTaggedTemplateNode* tt);
MIR_reg_t jm_create_func_or_closure(JsMirTranspiler* mt, JsFuncCollected* fc);
MIR_reg_t jm_transpile_func_expr(JsMirTranspiler* mt, JsFunctionNode* fn);
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
void jm_emit_exc_propagate_check(JsMirTranspiler* mt);
void jm_emit_class_static_field(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce, JsStaticFieldEntry* sf);
void jm_emit_class_static_block(JsMirTranspiler* mt, JsClassEntry* ce, JsAstNode* block);
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
void jm_cleanup_deferred_mir();
void* jm_get_last_deferred_mir_ctx();
void jm_finish_last_deferred_mir();
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
TypeId jm_p6_expr_type(JsAstNode* expr,
                               const char param_names[][128], TypeId* param_types, int param_count,
                               const char local_names[][128], TypeId* local_types, int local_count);
void jm_p6_collect_locals(JsAstNode* body,
                                  const char param_names[][128], TypeId* param_types, int param_count,
                                  char local_names[][128], TypeId* local_types, int* local_count, int max_locals);
void jm_p6_return_walk(JsAstNode* node,
                               const char param_names[][128], TypeId* param_types, int param_count,
                               const char local_names[][128], TypeId* local_types, int local_count,
                               TypeId* collected, int* count, int max_count);
void jm_p6_reinfer_return_type(JsFuncCollected* fc);
TypeId jm_p6_static_arg_type(JsMirTranspiler* mt, JsAstNode* arg);
void jm_p4b_ctor_walk(JsMirTranspiler* mt, JsAstNode* node,
                              P4bCtorEvidence* evidence);
void jm_p6_narrow_walk(JsMirTranspiler* mt, JsAstNode* node,
                               FnParamEvidence evidence[][16]);
void jm_callsite_scan_node(JsMirTranspiler* mt, JsAstNode* node);
void jm_callsite_propagate(JsMirTranspiler* mt, JsAstNode* program_body);
void jm_emit_eval_local_ensure_frame(JsMirTranspiler* mt);
void jm_emit_eval_local_pop_if_needed(JsMirTranspiler* mt);
void transpile_js_mir_ast(JsMirTranspiler* mt, JsAstNode* root);
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
Item transpile_js_module_to_mir(Runtime* runtime, const char* js_source, const char* filename);
void jm_load_imports(Runtime* runtime, JsAstNode* ast, const char* filename);
extern "C" Item js_new_function_from_string(Item* args, int argc);
char* eval_try_insert_return(const char* code, size_t len);
extern "C" Item js_builtin_eval(Item code_item, int64_t is_global_scope);
Item transpile_js_ast_to_mir(Runtime* runtime, JsTranspiler* tp, JsAstNode* ast, const char* filename);
void js_normalize_path_separators(char* path);
Item transpile_js_to_mir_core(Runtime* runtime, const char* js_source, const char* filename);
Item transpile_js_to_mir_core_len(Runtime* runtime, const char* js_source, size_t js_source_len, const char* filename);
Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename);
Item transpile_js_to_mir_len(Runtime* runtime, const char* js_source, size_t js_source_len, const char* filename);
Item transpile_js_to_mir_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                   JsPreambleState* out_state);
Item transpile_js_to_mir_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                      const char* filename, JsPreambleState* out_state);
Item transpile_js_to_mir_with_preamble(Runtime* runtime, const char* js_source, const char* filename,
                                        const JsPreambleState* preamble);
Item transpile_js_to_mir_with_preamble_len(Runtime* runtime, const char* js_source, size_t js_source_len,
                                           const char* filename, const JsPreambleState* preamble);
void preamble_state_destroy(JsPreambleState* state);
Item load_js_module(Runtime* runtime, const char* js_path);
bool js_is_cjs_file(const char* path);
char* js_wrap_cjs_source(const char* source, const char* filename);
extern "C" Item js_require(Item specifier);
extern "C" Item js_dynamic_import(Item specifier);
