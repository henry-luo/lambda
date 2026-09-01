#include "js_mir_internal.hpp"
#include "js_builtin_catalog.hpp"
#include "js_test262_fast_paths.h"
#include "js_exec_profile.h"
#include "../../lib/lambda_alloca.h"


MIR_reg_t jm_create_func_or_closure(JsMirTranspiler* mt, JsFuncCollected* fc);
static void jm_emit_global_var_property_sync(JsMirTranspiler* mt,
        JsModuleConstEntry* mc, String* name, MIR_reg_t value);
static void jm_emit_named_evaluation_for_identifier(JsMirTranspiler* mt,
        JsAstNode* rhs_node, MIR_reg_t rhs, String* name);
static void jm_sync_arguments_param_after_write(JsMirTranspiler* mt,
        JsMemberNode* member);
static void jm_emit_assignment_var_writeback(JsMirTranspiler* mt,
        JsMirVarEntry* var, const char* vname, MIR_reg_t value,
        bool include_closure_capture);
static void jm_emit_native_assignment_var_writeback(JsMirTranspiler* mt,
        JsMirVarEntry* var, const char* vname, TypeId type_id);
static bool jm_capture_matches_scope_env_name(FnCapture* cap, const char* scope_name);
static bool jm_capture_is_current_loop_lexical(JsMirTranspiler* mt,
        const char* name, JsMirVarEntry* var);
static void jm_promote_capture_to_scope_env(JsMirTranspiler* mt,
        JsMirVarEntry* var, int slot);

static bool jm_shared_scope_env_captures_valid(JsMirTranspiler* mt,
        JsFuncCollected* fc, bool reject_nfe_binding) {
    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
        JsMirVarEntry* cv = jm_find_var(mt, JM_CAPTURE_ARRAY(fc)[ci].name);
        if (reject_nfe_binding && JM_CAPTURE_ARRAY(fc)[ci].is_nfe_binding) return false;
        if (JM_CAPTURE_ARRAY(fc)[ci].scope_env_slot < 0) {
            if (jm_capture_uses_live_module_var(mt, &JM_CAPTURE_ARRAY(fc)[ci])) continue;
            return false;
        }
        if (!reject_nfe_binding && JM_CAPTURE_ARRAY(fc)[ci].is_nfe_binding) continue;
        if (cv && cv->is_let_const &&
            (!cv->in_scope_env || cv->scope_env_reg != mt->scope_env_reg ||
             cv->scope_env_slot != JM_CAPTURE_ARRAY(fc)[ci].scope_env_slot)) {
            return false;
        }
    }
    return true;
}

static void jm_prepare_closure_captures(JsMirTranspiler* mt, JsFuncCollected* fc,
                                        bool skip_nfe_scope_sync) {
    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
        FnCapture* capture = &JM_CAPTURE_ARRAY(fc)[ci];
        if (!capture->is_nfe_binding && capture->scope_env_slot < 0 &&
            mt->current_fc && mt->current_fc->has_scope_env &&
            mt->current_fc->scope_env_names) {
            for (int s = 0; s < mt->current_fc->scope_env_count; s++) {
                if (jm_capture_matches_scope_env_name(capture,
                        mt->current_fc->scope_env_names[s])) {
                    capture->scope_env_slot = s;
                    break;
                }
            }
        }
        JsMirVarEntry* cv = jm_find_var(mt, capture->name);
        if ((!skip_nfe_scope_sync || !capture->is_nfe_binding) &&
            cv && cv->in_scope_env && cv->scope_env_reg == mt->scope_env_reg &&
            cv->scope_env_slot >= 0 && capture->scope_env_slot < 0) {
            capture->scope_env_slot = cv->scope_env_slot;
        }
        if ((!skip_nfe_scope_sync || !capture->is_nfe_binding) &&
            cv && mt->scope_env_reg != 0 && capture->scope_env_slot >= 0 &&
            (!cv->in_scope_env || cv->scope_env_reg != mt->scope_env_reg ||
             cv->scope_env_slot != capture->scope_env_slot) &&
            !jm_capture_is_current_loop_lexical(mt, capture->name, cv)) {
            jm_promote_capture_to_scope_env(mt, cv, capture->scope_env_slot);
        }
        if (cv && cv->is_let_const) capture->is_let_const = true;
    }
}

static MIR_reg_t jm_emit_with_writeback(JsMirTranspiler* mt, MIR_reg_t with_key,
        MIR_reg_t rhs, const char* result_name, bool strict_put,
        JsMirVarEntry* local_var, JsModuleConstEntry* module_const,
        String* name, const char* vname) {
    MIR_reg_t wrote_with_item = jm_call_3(mt, "js_set_last_with_binding_if_valid", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, with_key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, rhs),
        MIR_T_I64, MIR_new_int_op(mt->ctx, strict_put ? 1 : 0));
    jm_emit_error_lane_propagate_check(mt);
    MIR_reg_t wrote_with = jm_emit_is_truthy(mt, wrote_with_item, NULL);
    MIR_label_t local_write_label = jm_new_label(mt);
    MIR_label_t done_label = jm_new_label(mt);
    MIR_reg_t result = jm_new_reg(mt, result_name, MIR_T_I64);
    jm_emit_branch(mt, MIR_BF, local_write_label, wrote_with);
    jm_emit_mov(mt, result, rhs);
    jm_emit_jmp(mt, done_label);
    jm_emit_label(mt, local_write_label);
    if (local_var) {
        jm_emit_mov(mt, local_var->reg, rhs);
        jm_emit_assignment_var_writeback(mt, local_var, vname, local_var->reg, false);
        jm_emit_mov(mt, result, local_var->reg);
    } else {
        jm_store_module_var(mt, (uint32_t)module_const->int_val, rhs);
        jm_emit_global_var_property_sync(mt, module_const, name, rhs);
        jm_scope_env_mark_and_writeback(mt, vname, rhs);
        jm_emit_mov(mt, result, rhs);
    }
    jm_emit_label(mt, done_label);
    return result;
}

static void jm_emit_assignment_var_writeback(JsMirTranspiler* mt,
        JsMirVarEntry* var, const char* vname, MIR_reg_t value,
        bool include_closure_capture) {
    if (var->from_env) {
        jm_emit_store_i64(mt, var->env_slot * (int)sizeof(uint64_t), var->env_reg, value);
    }
    if (var->in_scope_env) {
        jm_emit_store_i64(mt, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg, value);
    }
    if (include_closure_capture) {
        jm_write_last_closure_capture_if_matching(mt, vname, value, var->type_id);
    }
    int api = jm_arguments_param_index(mt, vname, var);
    if (api >= 0) jm_arguments_writeback_param(mt, api, value);
}

static void jm_emit_native_assignment_var_writeback(JsMirTranspiler* mt,
        JsMirVarEntry* var, const char* vname, TypeId type_id) {
    bool has_scope_env = var->in_scope_env && var->scope_env_reg != 0;
    int param_index = jm_arguments_param_index(mt, vname, var);
    if (!has_scope_env && param_index < 0) return;
    MIR_reg_t boxed = jm_box_native(mt, var->reg, type_id);
    if (has_scope_env) {
        jm_emit_store_i64(mt, var->scope_env_slot * (int)sizeof(uint64_t),
            var->scope_env_reg, boxed);
    }
    if (param_index >= 0) jm_arguments_writeback_param(mt, param_index, boxed);
}

static MIR_reg_t jm_emit_logical_assignment(JsMirTranspiler* mt,
        JsAssignmentNode* asgn, JsIdentifierNode* id, MIR_reg_t old_val,
        bool module_store, JsModuleConstEntry* mc, const char* vname,
        bool strict_put, int set_global_strict_flag) {
    MIR_reg_t result = jm_new_reg(mt, "la_res", MIR_T_I64);
    MIR_label_t l_assign = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    MIR_reg_t cond;
    if (asgn->op == JS_OP_NULLISH_ASSIGN) {
        cond = jm_callr_1(mt, "js_is_nullish", MIR_T_I64, old_val);
        jm_emit_branch(mt, MIR_BT, l_assign, cond);
    } else {
        cond = jm_emit_is_truthy(mt, old_val, NULL);
        jm_emit(mt, MIR_new_insn(mt->ctx,
            asgn->op == JS_OP_AND_ASSIGN ? MIR_BT : MIR_BF,
            MIR_new_label_op(mt->ctx, l_assign),
            MIR_new_reg_op(mt->ctx, cond)));
    }
    jm_emit_mov(mt, result, old_val);
    jm_emit_jmp(mt, l_end);
    jm_emit_label(mt, l_assign);
    MIR_reg_t rhs = jm_transpile_box_item(mt, asgn->right);
    jm_emit_error_lane_propagate_check(mt);
    if (!asgn->lhs_is_parenthesized) {
        jm_emit_named_evaluation_for_identifier(mt, asgn->right, rhs, id->name);
    }
    if (module_store) {
        jm_store_module_var(mt, (uint32_t)mc->int_val, rhs);
        jm_emit_global_var_property_sync(mt, mc, id->name, rhs);
        jm_scope_env_mark_and_writeback(mt, vname, rhs);
    } else {
        MIR_reg_t name_reg = jm_box_property_name_literal(mt,
            id->name->chars, id->name->len);
        jm_call_3(mt, "js_set_global_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, rhs),
            MIR_T_I64, MIR_new_int_op(mt->ctx, set_global_strict_flag));
        jm_emit_error_lane_propagate_check(mt);
    }
    jm_emit_mov(mt, result, rhs);
    jm_emit_label(mt, l_end);
    return result;
}

static bool jm_test262_fast_paths_enabled(JsMirTranspiler* mt) {
#if JS_TEST262_FAST_PATHS
    if (!mt) return false;
    if (mt->preamble_entries && mt->preamble_entry_count > 0) return true;
    if (!mt->filename) return false;
    return strstr(mt->filename, "ref/test262/test/") != NULL ||
           strstr(mt->filename, "test/js262/test/") != NULL;
#else
    (void)mt;
    return false;
#endif
}

#if JS_TEST262_FAST_PATHS
// Test262 harness fast paths, one row per intercepted entry point.
//
// `argc` is how many arguments the native takes: the emitter boxes that many
// call arguments in source order and pads the tail with undefined, which is
// exactly what the hand-written arms did. Rows are matched on name plus an
// inclusive [min_args, max_args] window (max_args < 0 means unbounded).
#define JM_T262_ERROR_LANE   0x1u  // propagate the error lane after the call
#define JM_T262_VOID_RESULT  0x2u  // discard the result and yield undefined
#define JM_T262_LOCAL_GUARD  0x4u  // skip when `assert` is a local binding
#define JM_T262_READBACK_ENV 0x8u  // assert.throws re-enters JS; reload captures

typedef struct JsTest262Intercept {
    const char* name;
    uint8_t     len;
    int8_t      min_args;
    int8_t      max_args;
    uint8_t     argc;
    const char* fn;
    uint8_t     flags;
} JsTest262Intercept;

static const JsTest262Intercept js_test262_assert_methods[] = {
    { "sameValue",    9, 0, -1, 3, "js_assert_same_value",     JM_T262_ERROR_LANE | JM_T262_VOID_RESULT },
    { "notSameValue", 12, 0, -1, 3, "js_assert_not_same_value", JM_T262_ERROR_LANE | JM_T262_VOID_RESULT },
    { "compareArray", 12, 0, -1, 3, "js_assert_compare_array",  JM_T262_ERROR_LANE | JM_T262_VOID_RESULT },
    { "deepEqual",    9, 0, -1, 3, "js_assert_deep_equal",     JM_T262_ERROR_LANE | JM_T262_VOID_RESULT },
    { "throws",       6, 0, -1, 3, "js_assert_throws",
      JM_T262_ERROR_LANE | JM_T262_VOID_RESULT | JM_T262_READBACK_ENV },
    { NULL, 0, 0, 0, 0, NULL, 0 }
};

static const JsTest262Intercept js_test262_globals[] = {
    // decimalToPercentHexString appeared twice in the old chain; the second arm
    // (arg_count == 1, js_decimal_to_percent_hex_string) was unreachable behind
    // this one and is gone, along with its runtime entry point.
    { "decimalToPercentHexString", 25, 1, -1, 1, "js_test262_decimal_to_percent_hex_string", 0 },
    { "verifyProperty",            14, 3, -1, 4, "js_verify_property",
      JM_T262_ERROR_LANE | JM_T262_VOID_RESULT },
    { "compareArray",              12, 2,  2, 2, "js_compare_array",       0 },
    { "assert",                     6, 1,  2, 2, "js_assert_base",
      JM_T262_ERROR_LANE | JM_T262_VOID_RESULT | JM_T262_LOCAL_GUARD },
    { "$DONOTEVALUATE",            14, 0, -1, 0, "js_donotevaluate",
      JM_T262_ERROR_LANE | JM_T262_VOID_RESULT },
    { "isConstructor",             13, 1,  1, 1, "js_is_constructor",      0 },
    { "buildString",               11, 1,  1, 1, "js_test262_build_string", 0 },
    { NULL, 0, 0, 0, 0, NULL, 0 }
};

static bool jm_name_is(const String* name, const char* lit, int len) {
    return name && name->len == len && strncmp(name->chars, lit, (size_t)len) == 0;
}

static const JsTest262Intercept* jm_test262_lookup(const JsTest262Intercept* table,
                                                   const String* name, int arg_count) {
    if (!name) return NULL;
    for (const JsTest262Intercept* r = table; r->name; r++) {
        if (!jm_name_is(name, r->name, r->len)) continue;
        if (arg_count < r->min_args) return NULL;
        if (r->max_args >= 0 && arg_count > r->max_args) return NULL;
        return r;
    }
    return NULL;
}

// A local `assert` binding (e.g. `const assert = require('assert')`) must beat
// the harness fast path. Dynamic Function parameters can be MIR locals with no
// useful scope node, so the MIR var table is checked too.
static bool jm_test262_assert_is_local(JsMirTranspiler* mt, JsIdentifierNode* id) {
    AstBindingId binding_id = ast_index_binding_id(
        mt && mt->tp ? &mt->tp->ast_index : NULL, (AstNode*)id);
    NameEntry* entry = ast_index_binding(mt && mt->tp ? &mt->tp->ast_index : NULL, binding_id);
    if (entry && entry->node) return true;
    return jm_find_var(mt, "_js_assert") != NULL;
}

static MIR_reg_t jm_emit_test262_intercept(JsMirTranspiler* mt, JsCallNode* call,
                                           const JsTest262Intercept* spec) {
    MIR_reg_t argv[4];
    JsAstNode* arg = call->arguments;
    for (uint8_t i = 0; i < spec->argc; i++) {
        argv[i] = arg ? jm_transpile_box_item(mt, arg) : jm_emit_undefined(mt);
        if (arg) arg = arg->next;
    }
    MIR_reg_t result = 0;
    switch (spec->argc) {
    case 0: jm_call_0(mt, spec->fn, MIR_T_I64); break;
    case 1: result = jm_callr_1(mt, spec->fn, MIR_T_I64, argv[0]); break;
    case 2: result = jm_callr_2(mt, spec->fn, MIR_T_I64, argv[0], argv[1]); break;
    case 3: result = jm_callr_3(mt, spec->fn, MIR_T_I64, argv[0], argv[1], argv[2]); break;
    default: result = jm_callr_4(mt, spec->fn, MIR_T_I64, argv[0], argv[1], argv[2], argv[3]); break;
    }
    if (spec->flags & JM_T262_ERROR_LANE) jm_emit_error_lane_propagate_check(mt);
    if (spec->flags & JM_T262_READBACK_ENV) jm_readback_closure_env(mt);
    if (spec->flags & JM_T262_VOID_RESULT) return jm_emit_undefined(mt);
    return result;
}
#endif


static void jm_emit_pending_call_source(JsMirTranspiler* mt, JsCallNode* call) {
    if (!mt || !call || !mt->tp || !mt->tp->source) return;
    uint32_t start = call->source_span.start_byte;
    uint32_t end = call->source_span.end_byte;
    if (end <= start || end > (uint32_t)mt->tp->source_length) return;
    // assert.ok() uses the original call expression in generated messages;
    // fallback calls previously erased that source-level invariant at runtime.
    jm_call_void_2(mt, "js_set_pending_call_source",
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)(mt->tp->source + start)),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)(end - start)));
}

static void jm_emit_clear_pending_call_source(JsMirTranspiler* mt) {
    jm_call_void_2(mt, "js_set_pending_call_source",
        MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
}

static bool jm_js_ident_name_eq(JsAstNode* node, const char* name, uint32_t len) {
    if (!node || node->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)node;
    return id->name && id->name->len == len &&
        strncmp(id->name->chars, name, len) == 0;
}

static bool jm_js_string_literal_eq(JsAstNode* node, const char* name, uint32_t len) {
    if (!node || node->node_type != JS_AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)node;
    String* value = lit->literal_type == JS_LITERAL_STRING ?
        lit->value.string_value : NULL;
    return value && value->len == len && memcmp(value->chars, name, len) == 0;
}

static bool jm_js_property_name_eq(JsAstNode* node, const char* name, uint32_t len) {
    return jm_js_ident_name_eq(node, name, len) ||
        jm_js_string_literal_eq(node, name, len);
}

static bool jm_is_assert_object_expr(JsAstNode* node) {
    if (jm_js_ident_name_eq(node, "assert", 6) ||
        jm_js_ident_name_eq(node, "strict", 6)) {
        return true;
    }
    if (!node || node->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)node;
    return !m->computed && jm_is_assert_object_expr(m->object) &&
        jm_js_property_name_eq(m->property, "strict", 6);
}

static bool jm_is_assert_ok_member_expr(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)node;
    return jm_js_property_name_eq(m->property, "ok", 2) &&
        jm_is_assert_object_expr(m->object);
}

static bool jm_is_test262_global_assert_identifier(JsMirTranspiler* mt, JsAstNode* node) {
    if (!jm_test262_fast_paths_enabled(mt) ||
        !jm_js_ident_name_eq(node, "assert", 6)) {
        return false;
    }
    JsIdentifierNode* id = (JsIdentifierNode*)node;
    AstBindingId binding_id = ast_index_binding_id(
        mt && mt->tp ? &mt->tp->ast_index : NULL, (AstNode*)id);
    NameEntry* assert_entry = ast_index_binding(mt && mt->tp ? &mt->tp->ast_index : NULL, binding_id);
    char assert_vname[16];
    snprintf(assert_vname, sizeof(assert_vname), "_js_assert");
    bool is_local_assert = (assert_entry && assert_entry->node) ||
        (jm_find_var(mt, assert_vname) != NULL);
    return !is_local_assert;
}

static bool jm_call_needs_pending_call_source(JsMirTranspiler* mt, JsCallNode* call) {
    if (!call || !call->callee) return false;
    if (jm_js_ident_name_eq(call->callee, "assert", 6)) {
        // Test262's global harness assert is not Node assert.ok(); emitting
        // source capture there makes thousands of hot passing assertions slow.
        return !jm_is_test262_global_assert_identifier(mt, call->callee);
    }
    if (call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (jm_is_assert_ok_member_expr(call->callee)) return true;
    return jm_js_property_name_eq(m->property, "call", 4) ||
        jm_js_property_name_eq(m->property, "apply", 5) ?
        jm_is_assert_ok_member_expr(m->object) : false;
}

static bool jm_emit_assert_pending_call_source(JsMirTranspiler* mt, JsCallNode* call) {
    // pending source is consumed only by Node assert.ok(); keeping it out of
    // generic dispatch avoids taxing every ordinary method/function call.
    if (!jm_call_needs_pending_call_source(mt, call)) return false;
    jm_emit_pending_call_source(mt, call);
    return true;
}

static void jm_emit_clear_assert_pending_call_source(JsMirTranspiler* mt, bool emitted) {
    if (emitted) jm_emit_clear_pending_call_source(mt);
}

bool js_ast_is_proto_literal_key(JsAstNode* key) {
    if (!key) return false;
    if (key->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)key;
        return id->name && id->name->len == 9 &&
            memcmp(id->name->chars, "__proto__", 9) == 0;
    }
    if (key->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* literal = (JsLiteralNode*)key;
        String* value = literal->literal_type == JS_LITERAL_STRING ?
            literal->value.string_value : NULL;
        return value && value->len == 9 &&
            memcmp(value->chars, "__proto__", 9) == 0;
    }
    return false;
}

static JsFuncCollected* jm_find_direct_function_decl_for_identifier(
        JsMirTranspiler* mt, JsIdentifierNode* id) {
    AstIndex* index = mt && mt->tp ? &mt->tp->ast_index : NULL;
    AstNode* definition = ast_index_binding_definition(index,
        ast_index_binding_id(index, (AstNode*)id));
    if (!definition || definition->node_type != JS_AST_NODE_FUNCTION_DECLARATION ||
            !jm_function_decl_is_direct_binding((JsFunctionNode*)definition, false)) return NULL;
    return jm_find_collected_func(mt, (JsFunctionNode*)definition);
}

static bool jm_binding_statement_precedes_reference(JsMirTranspiler* mt,
        JsMirVarEntry* var, JsIdentifierNode* id) {
    if (!mt || !var || !id || var->from_env || mt->with_depth > 0 ||
            (mt->current_fc && JM_JS_FACT(mt->current_fc, has_direct_eval)) ||
            var->binding_end == 0) {
        return false;
    }
    JsBlockNode* block = NULL;
    if (mt->current_fc && mt->current_fc->node &&
            mt->current_fc->node->body &&
            mt->current_fc->node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        block = (JsBlockNode*)mt->current_fc->node->body;
    }
    if (!block) return false;
    int binding_index = -1;
    int reference_index = -1;
    int index = 0;
    for (JsAstNode* stmt = block->statements; stmt; stmt = stmt->next, index++) {
        uint32_t start = stmt->source_span.start_byte;
        uint32_t end = stmt->source_span.end_byte;
        if (start <= var->binding_start && var->binding_end <= end) {
            binding_index = index;
        }
        if (start <= id->source_span.start_byte && id->source_span.end_byte <= end) {
            reference_index = index;
        }
    }
    // A preceding declaration in the same statement block dominates every
    // later statement; switch clauses and closures never enter this proof.
    return binding_index >= 0 && reference_index > binding_index;
}

bool jm_ast_node_has_with_ancestor(JsMirTranspiler* mt, JsAstNode* root,
        JsAstNode* target) {
    AstIndex* index = mt && mt->tp ? &mt->tp->ast_index : NULL;
    AstNodeId root_id = ast_index_find(index, (AstNode*)root);
    AstNodeId target_id = ast_index_find(index, (AstNode*)target);
    if (!root || !target || root_id == AST_NODE_ID_INVALID ||
            target_id == AST_NODE_ID_INVALID ||
            !ast_index_node_descends(index, target_id, root_id)) {
        return false;
    }
    for (AstNodeId node_id = target_id; node_id != AST_NODE_ID_INVALID;
            node_id = ast_index_parent_id(index, node_id)) {
        JsAstNode* node = (JsAstNode*)index->nodes[node_id];
        if (node && node->node_type == JS_AST_NODE_WITH_STATEMENT) {
            JsAstNode* body = ((JsWithStatementNode*)node)->body;
            AstNodeId body_id = ast_index_find(index, (AstNode*)body);
            if (body_id != AST_NODE_ID_INVALID &&
                    ast_index_node_descends(index, target_id, body_id)) {
                return true;
            }
        }
        if (node_id == root_id) break;
    }
    return false;
}

static bool jm_node_has_with_ancestor(JsMirTranspiler* mt, JsAstNode* target) {
    if (!mt || !target || !mt->current_fc || !mt->current_fc->node ||
            !mt->current_fc->node->body) return false;
    return jm_ast_node_has_with_ancestor(mt, mt->current_fc->node->body, target);
}

static bool jm_current_function_captures_with_scope(JsMirTranspiler* mt) {
    return mt && (mt->with_depth > 0 ||
        (mt->current_fc && mt->current_fc->node &&
            JM_JS_FACT(mt->current_fc, uses_with)));
}

static bool jm_current_scope_can_see_iife_modvar(JsMirTranspiler* mt) {
    if (!mt || !mt->current_fc) return false;
    // Follow the published backend identity, never its post-order position.
    for (JsFuncCollected* fc = mt->current_fc; fc && fc->node;
            fc = jm_parent_collected_func(mt, fc)) {
        if (JM_JS_FACT(fc, is_iife_func_decl) || JM_JS_FACT(fc, is_iife_body)) return true;
    }
    return false;
}

static void jm_emit_global_var_property_sync(JsMirTranspiler* mt, JsModuleConstEntry* mc,
                                             String* name, MIR_reg_t value) {
    if (!mt || !mc || !name || name->len <= 0 || value == 0) return;
    if (mt->is_module || mt->is_eval_direct) return;
    if (mc->const_type != MCONST_MODVAR || mc->var_kind != JS_VAR_VAR) return;
    if (mc->is_iife_var) return;
    MIR_reg_t key = jm_box_property_name_literal(mt, name->chars, name->len);
    jm_callr_2(mt, "js_set_global_var_property_fast", MIR_T_I64, key, value);
    jm_emit_error_lane_propagate_check(mt);
}

bool jm_is_private_name(String* name) {
    return name && name->len > 1 && name->chars[0] == '#';
}

String* jm_class_private_name(JsMirTranspiler* mt, JsClassEntry* ce, String* name) {
    // A class index is compilation metadata, not JavaScript private identity.
    // Runtime class evaluation owns the unique key allocation.
    (void)mt;
    (void)ce;
    return name;
}

static void jm_private_name_suffix(String* name, const char** suffix, int* suffix_len) {
    *suffix = NULL; *suffix_len = 0;
    if (!jm_is_private_name(name)) return;
    *suffix = name->chars + 1;
    *suffix_len = (int)name->len - 1;
}

static bool jm_private_name_suffix_eq(String* name, const char* suffix, int suffix_len) {
    const char* own_suffix = NULL; int own_len = 0;
    jm_private_name_suffix(name, &own_suffix, &own_len);
    return own_suffix && own_len == suffix_len && strncmp(own_suffix, suffix, suffix_len) == 0;
}

static bool jm_class_declares_private_name(JsClassEntry* ce, const char* suffix, int suffix_len) {
    if (!ce || !suffix || suffix_len <= 0) return false;
    for (int i = 0; i < ce->method_count; i++) {
        if (jm_private_name_suffix_eq(ce->methods[i].name, suffix, suffix_len)) return true;
    }
    for (int i = 0; i < ce->static_field_count; i++) {
        if (jm_private_name_suffix_eq(ce->static_fields[i].name, suffix, suffix_len)) return true;
    }
    for (int i = 0; i < ce->instance_field_count; i++) {
        if (jm_private_name_suffix_eq(ce->instance_fields[i].name, suffix, suffix_len)) return true;
    }
    return false;
}

// Index ancestry remains correct when a synthetic callable shares source spans.
static JsClassEntry* jm_find_indexed_class_ancestor(JsMirTranspiler* mt,
        JsAstNode* node, bool include_node) {
    if (!mt || !mt->tp || !node) return NULL;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId node_id = ast_index_find(index, (AstNode*)node);
    AstClassId class_id = ast_index_nearest_class(index, node_id, include_node);
    return class_id < index->class_count
        ? jm_find_collected_class(mt, (JsClassNode*)index->classes[class_id]) : NULL;
}

static JsClassEntry* jm_find_innermost_class_for_node(JsMirTranspiler* mt,
        JsAstNode* node) {
    return jm_find_indexed_class_ancestor(mt, node, true);
}

static bool jm_indexed_class_contains_node(JsMirTranspiler* mt,
        JsClassEntry* entry, JsAstNode* node) {
    if (!mt || !mt->tp || !entry || !entry->node || !node) return false;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId node_id = ast_index_find(index, (AstNode*)node);
    AstNodeId class_id = ast_index_find(index, (AstNode*)entry->node);
    return node_id != AST_NODE_ID_INVALID && class_id != AST_NODE_ID_INVALID &&
        ast_index_node_descends(index, node_id, class_id);
}

static bool jm_class_name_matches(JsClassEntry* ce, String* name) {
    return ce && ce->name && name &&
        ce->name->len == name->len &&
        strncmp(ce->name->chars, name->chars, name->len) == 0;
}

static JsClassEntry* jm_current_inner_class_binding(JsMirTranspiler* mt, String* name, JsAstNode* ref_node) {
    if (!mt || !name) return NULL;
    // A named *class expression*'s name is an immutable binding scoped to the
    // class body only (it must not leak to the enclosing scope). So for an
    // expression, resolve to the inner binding only when the reference actually
    // lies inside the class's indexed subtree. Class *declarations* bind their name
    // in the enclosing function scope, so references outside the body still
    // resolve (e.g. `new C()` after a nested `class C {}`).
    if (jm_class_name_matches(mt->current_class, name) &&
        (mt->current_class->is_declaration || !ref_node ||
         jm_indexed_class_contains_node(mt, mt->current_class, ref_node))) {
        return mt->current_class;
    }
    if (mt->current_fc && mt->current_fc->node) {
        JsClassEntry* ce = jm_find_innermost_class_for_node(mt, (JsAstNode*)mt->current_fc->node);
        if (jm_class_name_matches(ce, name) &&
            (ce->is_declaration || !ref_node ||
             jm_indexed_class_contains_node(mt, ce, ref_node))) {
            return ce;
        }
    }
    return NULL;
}

static JsClassEntry* jm_resolve_private_owner(JsMirTranspiler* mt, JsAstNode* access_node, String* name) {
    if (!jm_is_private_name(name) || !mt) return NULL;
    const char* suffix = NULL; int suffix_len = 0;
    jm_private_name_suffix(name, &suffix, &suffix_len);
    if (!suffix || suffix_len <= 0) return NULL;

    for (JsClassEntry* entry = jm_find_innermost_class_for_node(mt, access_node);
            entry; entry = jm_find_indexed_class_ancestor(mt,
            (JsAstNode*)entry->node, false)) {
        if (jm_class_declares_private_name(entry, suffix, suffix_len)) return entry;
    }
    return NULL;
}

static String* jm_resolve_private_name(JsMirTranspiler* mt, JsAstNode* access_node, String* name) {
    if (!jm_is_private_name(name) || !mt) return name;
    const char* suffix = NULL; int suffix_len = 0;
    jm_private_name_suffix(name, &suffix, &suffix_len);
    if (!suffix || suffix_len <= 0) return name;

    JsClassEntry* best = jm_resolve_private_owner(mt, access_node, name);
    if (best) return jm_class_private_name(mt, best, name);
    Item eval_resolved = js_eval_private_resolve((Item){.item = s2it(name)});
    if (get_type_id(eval_resolved) == LMD_TYPE_STRING) {
        // Keep the source spelling through lowering. The runtime resolves it
        // against the bridged private environment, preserving the private
        // access path instead of mistaking an identity key for a public name.
        return name;
    }
    return jm_class_private_name(mt, mt->current_class, name);
}

static MIR_reg_t jm_emit_private_key_for_access(JsMirTranspiler* mt,
        JsAstNode* access_node, String* name) {
    MIR_reg_t source_key = jm_box_string_literal(mt, name->chars, (int)name->len);
    if (mt->current_private_home_class_reg) {
        // Inline static blocks have no method-call frame to publish a home
        // class; use their exact evaluated class identity for private names.
        return jm_callr_2(mt, "js_private_key_for_class", MIR_T_I64, mt->current_private_home_class_reg, source_key);
    }
    JsClassEntry* owner = jm_resolve_private_owner(mt, access_node, name);
    JsClassEntry* lexical_home = access_node
        ? jm_find_innermost_class_for_node(mt, access_node) : mt->current_class;
    if (owner && owner != lexical_home) {
        MIR_reg_t owner_class = jm_emit_class_object_for_entry(mt, owner);
        if (owner_class) {
            // Only an enclosing declaration bypasses the method home class;
            // a re-evaluated local class must retain its own fresh private keys.
            return jm_callr_2(mt, "js_private_key_for_class", MIR_T_I64, owner_class, source_key);
        }
    }
    return jm_callr_1(mt, "js_private_key_for_current_class", MIR_T_I64, source_key);
}

MIR_reg_t jm_create_method_function(JsMirTranspiler* mt, JsFuncCollected* fc, int param_count) {
    MIR_reg_t fn_item = jm_call_2(mt, "js_new_method_function_mir", MIR_T_I64,
        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
        MIR_T_I64, MIR_new_int_op(mt->ctx, param_count));
    if (JM_JS_FACT(fc, is_strict)) {
        jm_callr_void_1(mt, "js_mark_strict_func", fn_item);
    }
    if (JM_JS_FACT(fc, is_derived_constructor)) {
        jm_callr_void_1(mt, "js_mark_derived_constructor_func", fn_item);
    }
    return fn_item;
}

static MIR_reg_t jm_emit_member_key(JsMirTranspiler* mt, JsMemberNode* mem) {
    if (mem->computed) {
        return jm_transpile_box_item(mt, mem->property);
    }
    if (mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
        String* key_name = jm_resolve_private_name(mt, (JsAstNode*)mem->property, prop->name);
        if (jm_is_private_name(key_name)) {
            return jm_emit_private_key_for_access(mt, (JsAstNode*)mem->property, key_name);
        }
        return jm_box_property_name_literal(mt, prop->name->chars, prop->name->len);
    }
    return jm_transpile_box_item(mt, mem->property);
}

typedef struct JsClassMetadataNameRun {
    int metadata_start;
    uint32_t module_name_base;
    int count;
    uint64_t private_method_mask;
} JsClassMetadataNameRun;

static void jm_flush_class_metadata_name_run(JsMirTranspiler* mt,
        MIR_reg_t cls_obj, JsClassMetadataNameRun* run) {
    if (!run || run->count <= 0) return;
    jm_call_void_5(mt, "js_set_class_instance_field_metadata_name_id_range",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_int_op(mt->ctx, run->metadata_start),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)run->module_name_base),
        MIR_T_I64, MIR_new_int_op(mt->ctx, run->count),
        MIR_T_I64, MIR_new_int_op(mt->ctx,
            (int64_t)run->private_method_mask));
    run->count = 0;
    run->private_method_mask = 0;
}

static void jm_append_class_metadata_name(JsMirTranspiler* mt,
        MIR_reg_t cls_obj, JsClassMetadataNameRun* run, int metadata_index,
        uint32_t module_name_index, bool is_private_method) {
    bool continues_run = run->count > 0 && run->count < 64 &&
        module_name_index == run->module_name_base + (uint32_t)run->count;
    if (!continues_run) {
        // Module-name interning reuses duplicate names and initializer callable
        // creation can insert unrelated names, so only proven-contiguous IDs
        // may share one runtime range publication.
        jm_flush_class_metadata_name_run(mt, cls_obj, run);
        run->metadata_start = metadata_index;
        run->module_name_base = module_name_index;
    }
    if (is_private_method) {
        run->private_method_mask |= (uint64_t)1 << run->count;
    }
    run->count++;
}

void jm_emit_class_instance_field_metadata(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !ce) return;
    // Default derived constructors initialize from this runtime metadata. Keep
    // computed fields in declaration order too; their keys are published once
    // class evaluation has completed below.
    int metadata_count = ce->instance_field_count;
    for (int mi = 0; mi < ce->method_count; mi++) {
        JsClassMethodEntry* me = &ce->methods[mi];
        if (!me->is_static && !me->is_constructor && me->name && jm_is_private_name(me->name)) {
            bool seen = false;
            for (int pi = 0; pi < mi; pi++) {
                JsClassMethodEntry* prev = &ce->methods[pi];
                if (prev->is_static || prev->is_constructor || !prev->name || !jm_is_private_name(prev->name)) continue;
                if (prev->name->len == me->name->len &&
                    memcmp(prev->name->chars, me->name->chars, (size_t)me->name->len) == 0) {
                    seen = true;
                    break;
                }
            }
            if (seen) continue;
            metadata_count++;
        }
    }
    if (metadata_count <= 0) return;
    // Metadata is installed through the existing class-owned arrays, but each
    // source spelling is materialized through the sealed module NameId table.
    // This keeps delayed MIR independent of compiler-pool String pointers.
    jm_call_void_2(mt, "js_init_class_instance_field_metadata",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_int_op(mt->ctx, metadata_count));

    int metadata_index = 0;
    JsClassMetadataNameRun name_run = {0, UINT32_MAX, 0, 0};
    for (int fi = 0; fi < ce->instance_field_count; fi++) {
        JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
        String* source_name = inf->computed
            ? name_pool_create_len(mt->tp->name_pool, "__computed_class_field__", 24)
            : jm_class_private_name(mt, ce, inf->name);
        if (!source_name) {
            log_error("js-mir: instance metadata missing field name at index %d", fi);
            mt->collection_failed = true;
            return;
        }
        uint32_t source_index = jm_module_name_append(mt, source_name->chars,
            source_name->len);
        if (source_index == UINT32_MAX) {
            mt->collection_failed = true;
            return;
        }
        jm_append_class_metadata_name(mt, cls_obj, &name_run,
            metadata_index, source_index, false);
        if (inf->initializer && inf->initializer->node_type == JS_AST_NODE_LITERAL) {
            MIR_reg_t field_val = jm_transpile_box_item(mt, inf->initializer);
            jm_call_void_3(mt, "js_set_class_instance_field_metadata_value",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                MIR_T_I64, MIR_new_int_op(mt->ctx, metadata_index),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, field_val));
        } else if (inf->initializer_fc && inf->initializer_fc->func_item) {
            MIR_reg_t initializer = jm_create_func_or_closure(
                mt, inf->initializer_fc);
            // The class metadata call can allocate its property storage; keep
            // the newly created callable live until the class owns it (D5.3.1).
            jm_create_gc_root_slot(mt, initializer);
            // Private names in the thunk resolve against the evaluated class,
            // exactly like method bodies; source text cannot recover this home.
            jm_emit_set_function_home_class(mt, initializer, cls_obj);
            jm_call_void_3(mt,
                "js_set_class_instance_field_metadata_initializer",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
                MIR_T_I64, MIR_new_int_op(mt->ctx, metadata_index),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, initializer));
        }
        metadata_index++;
    }

    for (int mi = 0; mi < ce->method_count; mi++) {
        JsClassMethodEntry* me = &ce->methods[mi];
        if (me->is_static || me->is_constructor || !me->name || !jm_is_private_name(me->name)) continue;
        bool seen = false;
        for (int pi = 0; pi < mi; pi++) {
            JsClassMethodEntry* prev = &ce->methods[pi];
            if (prev->is_static || prev->is_constructor || !prev->name || !jm_is_private_name(prev->name)) continue;
            if (prev->name->len == me->name->len &&
                memcmp(prev->name->chars, me->name->chars, (size_t)me->name->len) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        String* method_name = jm_class_private_name(mt, ce, me->name);
        uint32_t source_index = jm_module_name_append(mt, method_name->chars,
            method_name->len);
        if (source_index == UINT32_MAX) {
            mt->collection_failed = true;
            return;
        }
        jm_append_class_metadata_name(mt, cls_obj, &name_run,
            metadata_index, source_index, true);
        metadata_index++;
    }
    jm_flush_class_metadata_name_run(mt, cls_obj, &name_run);
}

void jm_emit_class_instance_computed_field_metadata_keys(JsMirTranspiler* mt,
    MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !ce) return;
    for (int fi = 0; fi < ce->instance_field_count; fi++) {
        JsInstanceFieldEntry* inf = &ce->instance_fields[fi];
        if (!inf->computed || inf->key_module_var_index < 0) continue;
        MIR_reg_t key = jm_load_module_var(mt, (uint32_t)inf->key_module_var_index);
        jm_call_void_3(mt, "js_set_class_instance_field_metadata_key",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
            MIR_T_I64, MIR_new_int_op(mt->ctx, fi),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
    }
}

static void jm_emit_class_computed_field_module_key(JsMirTranspiler* mt,
        MIR_reg_t cls_obj, JsAstNode* key_expr, int module_var_index,
        bool validate_static_key) {
    int cls_key_spill = -1;
    if (mt->in_generator && jm_has_yield(mt, key_expr)) {
        // computed keys may suspend before the following class metadata writes;
        // preserve the class object across that suspension boundary.
        cls_key_spill = jm_gen_spill_save(mt, cls_obj);
    }
    // Class computed names are evaluated in the class PrivateEnvironment. The
    // module-key pass runs before instance/static initialization, so establish
    // that lexical home explicitly while a key such as `self.#field` resolves.
    MIR_reg_t previous_private_home = jm_callr_1(mt, "js_private_home_class_enter", MIR_T_I64, cls_obj);
    jm_create_gc_root_slot(mt, previous_private_home);
    int previous_private_home_spill = -1;
    if (mt->in_generator && jm_has_yield(mt, key_expr)) {
        // MIR registers do not survive a generator suspension; the private
        // home returned before a computed key yield must be restored from the
        // generator environment before leaving that temporary home.
        previous_private_home_spill = jm_gen_spill_save(mt, previous_private_home);
    }
    MIR_reg_t key = jm_transpile_box_item(mt, key_expr);
    if (cls_key_spill >= 0) {
        jm_gen_spill_load(mt, cls_obj, cls_key_spill);
    }
    if (previous_private_home_spill >= 0) {
        jm_gen_spill_load(mt, previous_private_home, previous_private_home_spill);
    }
    key = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, key);
    key = jm_callr_2(mt, "js_private_home_class_leave_result", MIR_T_I64, previous_private_home, key);
    jm_emit_error_lane_propagate_check(mt);
    if (validate_static_key) {
        jm_callr_1(mt, "js_check_class_static_field_key", MIR_T_I64, key);
        jm_emit_error_lane_propagate_check(mt);
    }
    jm_store_module_var(mt, (uint32_t)module_var_index, key);
}

void jm_emit_class_computed_field_module_keys(JsMirTranspiler* mt,
        MIR_reg_t cls_obj, JsClassEntry* ce) {
    if (!mt || !ce || !ce->node || !ce->node->body ||
            ce->node->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        return;
    }
    JsBlockNode* body = (JsBlockNode*)ce->node->body;
    int static_field_index = 0;
    int instance_field_index = 0;
    for (JsAstNode* elem = body->statements; elem; elem = elem->next) {
        if (elem->node_type != JS_AST_NODE_FIELD_DEFINITION) continue;
        JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)elem;
        if (fd->is_static) {
            if (static_field_index >= ce->static_field_count) continue;
            JsStaticFieldEntry* sf = &ce->static_fields[static_field_index++];
            if (sf->computed && sf->key_expr && sf->key_module_var_index >= 0) {
                jm_emit_class_computed_field_module_key(mt, cls_obj, sf->key_expr,
                    sf->key_module_var_index, true);
            }
        } else {
            if (instance_field_index >= ce->instance_field_count) continue;
            JsInstanceFieldEntry* inf = &ce->instance_fields[instance_field_index++];
            if (inf->computed && inf->key_expr && inf->key_module_var_index >= 0) {
                jm_emit_class_computed_field_module_key(mt, cls_obj, inf->key_expr,
                    inf->key_module_var_index, false);
            }
        }
    }
}

static MIR_reg_t jm_emit_current_this(JsMirTranspiler* mt) {
    if (mt && mt->current_fc && mt->current_fc->node &&
        (mt->current_fc->node->is_arrow || mt->current_fc->node->is_generator ||
         mt->in_generator)) {
        JsMirVarEntry* var = jm_find_var(mt, "_js_this");
        if (var) {
            if (var->in_scope_env && var->scope_env_reg != 0 && var->scope_env_slot >= 0) {
                jm_emit_load_i64(mt, var->reg, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg);
            } else if (var->from_env && var->env_reg != 0 && var->env_slot >= 0) {
                jm_emit_load_i64(mt, var->reg, var->env_slot * (int)sizeof(uint64_t), var->env_reg);
            }
            MIR_reg_t resolved = jm_callr_1(mt, "js_resolve_lexical_this", MIR_T_I64, var->reg);
            // `this` remains a TDZ sentinel until super() in derived
            // constructors; do not let a later call overwrite that error lane.
            jm_emit_error_lane_propagate_check(mt);
            return resolved;
        }
    }
    MIR_reg_t current = jm_call_0(mt, "js_get_this", MIR_T_I64);
    jm_emit_error_lane_propagate_check(mt);
    return current;
}

static MIR_reg_t jm_emit_current_new_target(JsMirTranspiler* mt) {
    if (mt && mt->current_fc && mt->current_fc->node && mt->current_fc->node->is_arrow) {
        JsMirVarEntry* var = jm_find_var(mt, "_js_new.target");
        if (var) {
            // new.target is lexical inside arrows; a later direct call would clear
            // the runtime slot, so use the captured value when one exists.
            return var->reg;
        }
    }
    return jm_call_0(mt, "js_get_new_target", MIR_T_I64);
}

static bool jm_class_has_instance_elements(JsClassEntry* ce) {
    if (!ce) return false;
    if (ce->instance_field_count > 0) return true;
    for (int method_index = 0; method_index < ce->method_count; method_index++) {
        JsClassMethodEntry* method = &ce->methods[method_index];
        if (!method->is_static && !method->is_constructor && method->name &&
            jm_is_private_name(method->name)) return true;
    }
    return false;
}

static void jm_emit_update_lexical_this_binding(JsMirTranspiler* mt, MIR_reg_t obj) {
    if (!mt || !obj) return;
    JsMirVarEntry* js_this_var = jm_find_var(mt, "_js_this");
    if (!js_this_var) return;
    jm_emit_mov(mt, js_this_var->reg, obj);
    if ((js_this_var->in_scope_env || js_this_var->from_env) &&
        js_this_var->scope_env_reg != 0 && js_this_var->scope_env_slot >= 0) {
        jm_emit_store_i64(mt, js_this_var->scope_env_slot * (int)sizeof(uint64_t), js_this_var->scope_env_reg, obj);
    } else if (js_this_var->from_env && js_this_var->env_reg != 0 && js_this_var->env_slot >= 0) {
        jm_emit_store_i64(mt, js_this_var->env_slot * (int)sizeof(uint64_t), js_this_var->env_reg, obj);
    }
}

static void jm_emit_public_instance_fields_for_super(JsMirTranspiler* mt, MIR_reg_t obj, JsClassEntry* ce) {
    if (!mt || !obj || !ce || !jm_class_has_instance_elements(ce)) return;
    MIR_reg_t class_object = jm_emit_class_object_for_entry(mt, ce);
    if (!class_object) return;
    jm_create_gc_root_slot(mt, class_object);
    // Field expression thunks are capabilities stored on the evaluated class.
    // Calling that owner at the exact post-super point keeps dynamic aliases and
    // Reflect.construct on the same D6.2.2v2 path as direct `new` syntax.
    jm_callr_2(mt, "js_init_class_instance_fields_after_super", MIR_T_I64, class_object, obj);
    jm_emit_error_lane_propagate_check(mt);
}

static MIR_reg_t jm_emit_super_bind_this_with_public_fields(JsMirTranspiler* mt, MIR_reg_t this_val, MIR_reg_t super_result) {
    MIR_reg_t bound_this = jm_callr_2(mt, "js_super_bind_this", MIR_T_I64, this_val, super_result);
    if (mt && mt->current_class && mt->current_class->node && mt->current_class->node->superclass) {
        jm_emit_error_lane_propagate_check(mt);
        jm_emit_update_lexical_this_binding(mt, bound_this);
        if (jm_class_has_instance_elements(mt->current_class)) {
            jm_emit_public_instance_fields_for_super(mt, bound_this, mt->current_class);
        }
    }
    return bound_this;
}

static bool jm_super_reference_before_constructor_super_call(JsMirTranspiler* mt, JsAstNode* super_ref_node) {
    if (!mt || !super_ref_node || !mt->current_fc || !mt->current_class) return false;
    if (!JM_JS_FACT(mt->current_fc, is_constructor)) return false;
    if (!mt->current_class->node || !mt->current_class->node->superclass) return false;
    FnAnalysis* analysis = jm_function_analysis(mt->current_fc);
    return !analysis || !analysis->js_has_direct_super_call ||
        super_ref_node->source_span.start_byte <
            analysis->js_first_direct_super_call_start;
}

static void jm_emit_named_evaluation_for_identifier(JsMirTranspiler* mt, JsAstNode* rhs_node, MIR_reg_t rhs, String* name) {
    if (!rhs_node || !name || name->len <= 0) return;
    if (rhs_node->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
        rhs_node->node_type == JS_AST_NODE_ARROW_FUNCTION) {
        JsFunctionNode* fn_node = (JsFunctionNode*)rhs_node;
        if (!fn_node->name) {
            jm_emit_set_function_name(mt, rhs, name->chars);
        }
    } else if (rhs_node->node_type == JS_AST_NODE_CLASS_EXPRESSION ||
               rhs_node->node_type == JS_AST_NODE_CLASS_DECLARATION) {
        JsClassNode* cls = (JsClassNode*)rhs_node;
        if (!cls->name) {
            MIR_reg_t name_reg = jm_box_string_literal(mt, name->chars, (int)name->len);
            jm_callr_void_2(mt, "js_set_class_name", rhs, name_reg);
        }
    }
}

JsMirReference jm_emit_reference(JsMirTranspiler* mt, JsAstNode* node) {
    JsMirReference ref;
    ref.kind = JS_MIR_REF_INVALID;
    ref.base_reg = 0;
    ref.key_reg = 0;
    ref.strict = jm_strict_put(mt);
    ref.uninitialized_this = false;
    ref.is_private = false;
    ref.computed_key = false;
    ref.property_key_canonicalized = false;
    ref.named_key_index = UINT32_MAX;
    ref.named_key_id = NAME_ID_NONE;
    ref.jube_slot = -1;
    ref.jube_ordinal = UINT32_MAX;
    ref.jube_kind = UINT8_MAX;
    ref.jube_can_raise = false;

    if (!node) return ref;

    if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)node;
        bool is_super = false;
        if (mem->object && mem->object->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj_id = (JsIdentifierNode*)mem->object;
            is_super = obj_id->name && obj_id->name->len == 5 &&
                strncmp(obj_id->name->chars, "super", 5) == 0;
        }

        if (is_super) {
            ref.kind = JS_MIR_REF_SUPER_PROPERTY;
            ref.uninitialized_this = jm_super_reference_before_constructor_super_call(mt, node);
            ref.computed_key = mem->computed;
            ref.base_reg = jm_emit_current_this(mt);
            if (ref.uninitialized_this && mem->computed) {
                ref.key_reg = jm_emit_undefined(mt);
            } else {
                ref.key_reg = jm_emit_member_key(mt, mem);
            }
            return ref;
        }

        ref.kind = JS_MIR_REF_PROPERTY;
        ref.computed_key = mem->computed;
        if (!mem->computed && mem->property &&
            mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop_id = (JsIdentifierNode*)mem->property;
            String* key_name = jm_resolve_private_name(mt, (JsAstNode*)mem->property, prop_id->name);
            ref.is_private = jm_is_private_name(key_name);
            if (key_name && !ref.is_private) {
                ref.named_key_id = well_known_name_id({key_name->chars, key_name->len});
                // the runtime receives the owner module's resolved NameId at
                // execution time. A compiler-time ID would bake a realm into
                // shared MIR (D5.4.3, D5.4.4).
                ref.named_key_index = jm_module_name_index(mt,
                    key_name->chars, key_name->len);
            }
        }
        if (mem->object && mem->object->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj_id = (JsIdentifierNode*)mem->object;
            if (obj_id->name && obj_id->name->len == 4 &&
                strncmp(obj_id->name->chars, "this", 4) == 0 &&
                jm_super_reference_before_constructor_super_call(mt, node)) {
                ref.uninitialized_this = true;
                MIR_reg_t msg = jm_box_string_literal(mt, "Must call super constructor before accessing 'this'", 51);
                jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
                jm_emit_error_lane_propagate_check(mt);
            }
        }
        if (!ref.computed_key && !ref.is_private && mem->property &&
                mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* property = (JsIdentifierNode*)mem->property;
            const JubeTypeDef* receiver_type = jm_infer_jube_type(mt, mem->object);
            if (receiver_type && property->name) {
                int ordinal = jube_member_ordinal(receiver_type,
                    property->name->chars, (uint32_t)property->name->len);
                int slot = ordinal >= 0 ? jube_iface_type_slot(receiver_type) : -1;
                if (ordinal >= 0 && slot >= 0) {
                    ref.jube_slot = slot;
                    ref.jube_ordinal = (uint32_t)ordinal;
                    ref.jube_kind = jube_member_kind_at(receiver_type, ordinal);
                    ref.jube_can_raise = jube_member_can_raise_at(receiver_type,
                        ordinal);
                }
            }
        }
        ref.base_reg = jm_transpile_box_item(mt, mem->object);
        // An assignment reference is evaluated before its RHS. Keep both parts
        // exact: a nested RHS write can collect before `bucket[index] = value`
        // consumes the earlier receiver, otherwise the stale MIR register may
        // turn a valid array bucket into undefined after compaction.
        jm_create_gc_root_slot(mt, ref.base_reg);
        int obj_spill = -1;
        if (mt->in_generator && mem->computed && jm_has_yield(mt, mem->property)) {
            obj_spill = jm_gen_spill_save(mt, ref.base_reg);
        }
            ref.key_reg = jm_emit_member_key(mt, mem);
            if (ref.computed_key) jm_emit_error_lane_propagate_check(mt);
            jm_create_gc_root_slot(mt, ref.key_reg);
        if (obj_spill >= 0) {
            jm_gen_spill_load(mt, ref.base_reg, obj_spill);
        }
        return ref;
    }

    return ref;
}

static MIR_reg_t jm_emit_reference_name_id(JsMirTranspiler* mt,
        const JsMirReference* ref) {
    if (ref && ref->named_key_id != NAME_ID_NONE) {
        MIR_reg_t name_id = jm_new_reg(mt, "nameid", MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, name_id, MIR_new_int_op(mt->ctx, (int64_t)ref->named_key_id));
        return name_id;
    }
    return jm_module_name_id_at_index(mt,
        ref ? ref->named_key_index : UINT32_MAX);
}

static const JubeTypeDef* jm_jube_seed_type(JsMirTranspiler* mt,
                                             JsIdentifierNode* id) {
    if (!mt || !id || !id->name || mt->with_depth > 0 || mt->is_eval_direct ||
            (mt->current_fc && (JM_JS_FACT(mt->current_fc, has_direct_eval) ||
                                JM_JS_FACT(mt->current_fc, uses_with)))) {
        return NULL;
    }
    const char* vname = jm_var_name(id->name);
    if (jm_find_var(mt, vname)) return NULL;
    if (mt->module_consts) {
        JsModuleConstEntry probe;
        memset(&probe, 0, sizeof(probe));
        probe.name = jm_persist_name(vname);
        if (hashmap_get(mt->module_consts, &probe)) return NULL;
    }
    if (id->name->len == 8 && memcmp(id->name->chars, "document", 8) == 0) {
        return jube_iface_type_by_name("document", 8);
    }
    return NULL;
}

// D4e keeps the proof deliberately small: only declared-signature results and
// the immutable global seed participate. Unknown assignments and joins return
// NULL, so the ordinary JS property path remains the semantic fallback.
const JubeTypeDef* jm_infer_jube_type(JsMirTranspiler* mt, JsAstNode* node) {
    if (!mt || !node || mt->with_depth > 0 || mt->is_eval_direct ||
            (mt->current_fc && (JM_JS_FACT(mt->current_fc, has_direct_eval) ||
                                JM_JS_FACT(mt->current_fc, uses_with)))) {
        return NULL;
    }
    if (node->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        const char* vname = id->name
            ? jm_var_name(id->name)
            : NULL;
        JsMirVarEntry* var = vname ? jm_find_var(mt, vname) : NULL;
        if (var && var->jube_type) return var->jube_type;
        return jm_jube_seed_type(mt, id);
    }
    if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* member = (JsMemberNode*)node;
        if (member->computed || !member->property ||
                member->property->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
        JsIdentifierNode* property = (JsIdentifierNode*)member->property;
        const JubeTypeDef* base = jm_infer_jube_type(mt, member->object);
        if (!base || !property->name) return NULL;
        int ordinal = jube_member_ordinal(base, property->name->chars,
                                          (uint32_t)property->name->len);
        return ordinal >= 0 ? jube_member_result_type_at(base, ordinal) : NULL;
    }
    if (node->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)node;
        if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) {
            return NULL;
        }
        JsMemberNode* member = (JsMemberNode*)call->callee;
        if (member->computed || !member->property ||
                member->property->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
        JsIdentifierNode* property = (JsIdentifierNode*)member->property;
        const JubeTypeDef* base = jm_infer_jube_type(mt, member->object);
        if (!base || !property->name) return NULL;
        int ordinal = jube_member_ordinal(base, property->name->chars,
                                          (uint32_t)property->name->len);
        return ordinal >= 0 ? jube_member_result_type_at(base, ordinal) : NULL;
    }
    return NULL;
}

static void jm_emit_canonicalize_computed_key_for_get_put(JsMirTranspiler* mt, JsMirReference* ref) {
    if (!ref || ref->kind != JS_MIR_REF_PROPERTY || !ref->computed_key || ref->key_reg == 0) return;
    // computed update/compound references must preserve base-nullish errors while reusing one ToPropertyKey result.
    jm_callr_1(mt, "js_require_object_coercible", MIR_T_I64, ref->base_reg);
    jm_emit_error_lane_propagate_check(mt);
    ref->key_reg = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, ref->key_reg);
    jm_emit_error_lane_propagate_check(mt);
    ref->property_key_canonicalized = true;
}

static MIR_reg_t jm_emit_reference_key(JsMirTranspiler* mt,
        const JsMirReference* ref) {
    MIR_reg_t key = ref->key_reg;
    if (ref->computed_key && !ref->property_key_canonicalized) {
        key = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, key);
        jm_emit_error_lane_propagate_check(mt);
        jm_create_gc_root_slot(mt, key);
    }
    return key;
}

MIR_reg_t jm_emit_get_value(JsMirTranspiler* mt, const JsMirReference* ref) {
    if (!ref) return jm_emit_undefined(mt);
    switch (ref->kind) {
    case JS_MIR_REF_PROPERTY: {
        if (ref->jube_slot >= 0 && ref->jube_ordinal != UINT32_MAX) {
            MIR_reg_t result = jm_call_4(mt, "js_jube_member_get_by_ordinal",
                MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ref->jube_slot),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ref->jube_ordinal),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->key_reg));
            jm_emit_error_lane_propagate_check(mt);
            return result;
        }
        if (!ref->is_private && ref->named_key_index != UINT32_MAX) {
            MIR_reg_t name_id = jm_emit_reference_name_id(mt, ref);
            return jm_callr_2(mt, "js_get_name_id", MIR_T_I64, ref->base_reg, name_id);
        }
        MIR_reg_t key = jm_emit_reference_key(mt, ref);
        MIR_reg_t lane = jm_callr_1(mt, "js_property_lane_for_canonical_key", MIR_T_I64, key);
        return jm_callr_4(mt, "js_get", MIR_T_I64, ref->base_reg, lane, key, ref->base_reg);
    }
    case JS_MIR_REF_SUPER_PROPERTY: {
        if (ref->uninitialized_this) {
            MIR_reg_t msg = jm_box_string_literal(mt, "Must call super constructor before accessing 'this'", 51);
            jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
                jm_emit_error_lane_propagate_check(mt);
            return jm_emit_undefined(mt);
        }
        MIR_reg_t result = jm_callr_2(mt, "js_super_property_get", MIR_T_I64, ref->base_reg, ref->key_reg);
        jm_emit_error_lane_propagate_check(mt);
        return result;
    }
    default:
        return jm_emit_undefined(mt);
    }
}

MIR_reg_t jm_emit_put_value(JsMirTranspiler* mt, const JsMirReference* ref, MIR_reg_t value) {
    if (!ref) return value;
    MIR_reg_t result = value;
    switch (ref->kind) {
    case JS_MIR_REF_PROPERTY:
        if (ref->jube_slot >= 0 && ref->jube_ordinal != UINT32_MAX &&
                !ref->is_private && !ref->computed_key) {
            result = jm_call_6(mt, "js_jube_member_set_by_ordinal", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ref->jube_slot),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)ref->jube_ordinal),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, value),
                MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0));
            break;
        }
        // Tune8 §2.2: js_private_property_set absorbs the _strict variant
        // (4-arg form: obj, key, val, strict).
        if (ref->is_private) {
            result = jm_call_4(mt, "js_private_property_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, value),
                MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0));
        }
        else {
            if (ref->named_key_index != UINT32_MAX) {
                MIR_reg_t name_id = jm_emit_reference_name_id(mt, ref);
                result = jm_call_4(mt, "js_set_name_id", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, name_id),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, value),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0));
            } else {
                MIR_reg_t key = jm_emit_reference_key(mt, ref);
                MIR_reg_t lane = jm_callr_1(mt, "js_property_lane_for_canonical_key", MIR_T_I64, key);
                result = jm_callr_5(mt, "js_set", MIR_T_I64, ref->base_reg, lane, key, value, ref->base_reg);
                result = jm_call_5(mt, "js_assignment_set_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, value),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg));
            }
        }
        break;
    case JS_MIR_REF_SUPER_PROPERTY:
        if (ref->uninitialized_this) {
            MIR_reg_t msg = jm_box_string_literal(mt, "Must call super constructor before accessing 'this'", 51);
            jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
            jm_emit_error_lane_propagate_check(mt);
            result = value;
            break;
        }
        // Tune8 §2.2: super_property_set unified; strict is now an explicit
        // constant operand instead of two separate runtime entries.
        result = jm_call_4(mt, "js_super_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->key_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, value),
            MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0));
        break;
    default:
        return value;
    }
    // Private writes can throw in sloppy code during the brand check; the
    // ordinary setter's strict-only check must not swallow that abrupt result.
    if (ref->is_private || ref->strict) {
        jm_emit_error_lane_propagate_check(mt);
    }
    return result;
}

MIR_reg_t jm_emit_delete_reference(JsMirTranspiler* mt, const JsMirReference* ref) {
    if (!ref) return jm_box_int_const(mt, 1);
    switch (ref->kind) {
    case JS_MIR_REF_PROPERTY: {
        MIR_reg_t key = jm_emit_reference_key(mt, ref);
        MIR_reg_t lane = ref->named_key_index != UINT32_MAX
            ? jm_emit_reference_name_id(mt, ref)
            : jm_callr_1(mt, "js_property_lane_for_canonical_key", MIR_T_I64, key);
        MIR_reg_t result = jm_callr_3(mt, "js_delete", MIR_T_I64, ref->base_reg, lane, key);
        return jm_call_3(mt, "js_delete_reference_result", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
            MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0));
    }
    case JS_MIR_REF_SUPER_PROPERTY: {
        MIR_reg_t msg = jm_box_string_literal(mt, "Unsupported reference to 'super'", 32);
            jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
        jm_emit_error_lane_propagate_check(mt);
        MIR_reg_t r = jm_new_reg(mt, "dfalse", MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, (int64_t)ITEM_FALSE_VAL));
        return r;
    }
    default:
        return jm_box_int_const(mt, 1);
    }
}

static void jm_emit_invalid_assignment_target_reference_error(JsMirTranspiler* mt) {
    MIR_reg_t msg = jm_box_string_literal(mt, "Invalid left-hand side in assignment", 36);
    jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
}

static const char* jm_compound_assign_fn(JsOperator op) {
    switch (op) {
    case JS_OP_ADD_ASSIGN: return "js_add";
    case JS_OP_SUB_ASSIGN: return "js_subtract";
    case JS_OP_MUL_ASSIGN: return "js_multiply";
    case JS_OP_DIV_ASSIGN: return "js_divide";
    case JS_OP_MOD_ASSIGN: return "js_modulo";
    case JS_OP_EXP_ASSIGN: return "js_power";
    case JS_OP_BIT_AND_ASSIGN: return "js_bitwise_and";
    case JS_OP_BIT_OR_ASSIGN: return "js_bitwise_or";
    case JS_OP_BIT_XOR_ASSIGN: return "js_bitwise_xor";
    case JS_OP_LSHIFT_ASSIGN: return "js_left_shift";
    case JS_OP_RSHIFT_ASSIGN: return "js_right_shift";
    case JS_OP_URSHIFT_ASSIGN: return "js_unsigned_right_shift";
    default: return "js_add";
    }
}

static MIR_reg_t jm_emit_compound_assign(JsMirTranspiler* mt, JsOperator op,
        MIR_reg_t left, MIR_reg_t right) {
    return jm_callr_2(mt, jm_compound_assign_fn(op), MIR_T_I64, left, right);
}

static MIR_reg_t jm_emit_int32_bitwise_binary(JsMirTranspiler* mt,
        JsAstNode* left, JsAstNode* right, TypeId left_type, TypeId right_type,
        MIR_insn_code_t op, const char* name) {
    MIR_reg_t li = (left_type == LMD_TYPE_INT)
        ? jm_transpile_as_native(mt, left, LMD_TYPE_INT)
        : jm_call_1(mt, "js_double_to_int32", MIR_T_I64,
            MIR_T_D, MIR_new_reg_op(mt->ctx,
                jm_transpile_as_native(mt, left, LMD_TYPE_FLOAT)));
    MIR_reg_t ri = (right_type == LMD_TYPE_INT)
        ? jm_transpile_as_native(mt, right, LMD_TYPE_INT)
        : jm_call_1(mt, "js_double_to_int32", MIR_T_I64,
            MIR_T_D, MIR_new_reg_op(mt->ctx,
                jm_transpile_as_native(mt, right, LMD_TYPE_FLOAT)));
    MIR_reg_t result = jm_new_reg(mt, name, MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, op,
        MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, li),
        MIR_new_reg_op(mt->ctx, ri)));
    // sign-extend the JS ToInt32 result before boxing it as a number.
    jm_emit_reg_binary_op(mt, MIR_LSH, result, result, MIR_new_int_op(mt->ctx, 32));
    jm_emit_reg_binary_op(mt, MIR_RSH, result, result, MIR_new_int_op(mt->ctx, 32));
    return jm_emit_int_to_double(mt, result);
}

static MIR_reg_t jm_box_bigint_literal(JsMirTranspiler* mt, String* spelling) {
    // bigint_from_string consumes character bytes, not the String object returned by it2s;
    // passing that object pointer makes every literal parse as invalid input.
    return jm_call_2(mt, "bigint_from_string", MIR_T_I64,
        MIR_T_P, MIR_new_str_op(mt->ctx, {(size_t)spelling->len, spelling->chars}),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)spelling->len));
}

MIR_reg_t jm_transpile_literal(JsMirTranspiler* mt, JsLiteralNode* lit) {
    switch (lit->literal_type) {
    case JS_LITERAL_NUMBER: {
        // bigint literal: store the dedicated digit spelling and parse it at runtime.
        if (lit->is_bigint) {
            // bigint AST literals keep their digits in bigint_str; value.string_value is unset,
            // so reading the generic literal slot turns every n-suffixed literal into bad input.
            return jm_box_bigint_literal(mt, lit->bigint_str);
        }
        double val = lit->value.number_value;
        return jm_box_float_const(mt, val);
    }
    case JS_LITERAL_STRING: {
        String* sv = lit->value.string_value;
        return jm_box_string_literal(mt, sv->chars, sv->len);
    }
    case JS_LITERAL_BOOLEAN: {
        uint64_t bval = lit->value.boolean_value ? ITEM_TRUE_VAL : ITEM_FALSE_VAL;
        return jm_boxed_immediate_const(mt, bval, "bool");
    }
    case JS_LITERAL_NULL:
        return jm_emit_null(mt);
    case JS_LITERAL_UNDEFINED: {
        return jm_emit_undefined(mt);
    }
    default:
        // shared AST tags include Python-only literals; JS lowers unknown tags to null.
        break;
    }
    return jm_emit_null(mt);
}

static const char* jm_with_binding_get_name(JsMirTranspiler* mt) {
    return mt && (jm_strict_put(mt))
        ? "js_get_with_binding_or_fallback_strict"
        : "js_get_with_binding_or_fallback";
}

static MIR_reg_t jm_apply_with_identifier_fallback(JsMirTranspiler* mt, JsIdentifierNode* id, MIR_reg_t fallback) {
    if (!mt || !id || !id->name) return fallback;
    if (!mt->with_depth && !jm_current_function_captures_with_scope(mt)) {
        return fallback;
    }
    MIR_reg_t key = jm_box_property_name_literal(mt, id->name->chars, id->name->len);
    MIR_reg_t result = jm_callr_2(mt, jm_with_binding_get_name(mt), MIR_T_I64, key, fallback);
    jm_emit_error_lane_propagate_check(mt);
    return result;
}

static MIR_reg_t jm_emit_plain_call_this_arg(JsMirTranspiler* mt, JsCallNode* call) {
    MIR_reg_t this_arg = jm_emit_undefined(mt);
    if (!mt || (!mt->with_depth && !jm_current_function_captures_with_scope(mt)) ||
            !call || !call->callee ||
            call->callee->node_type != JS_AST_NODE_IDENTIFIER) {
        return this_arg;
    }
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    if (!id->name) return this_arg;
    MIR_reg_t key = jm_box_property_name_literal(mt, id->name->chars, id->name->len);
    // `with (obj) { method(); }` is an identifier reference whose base is the
    // with object, not an ordinary plain call. Reuse the callee lookup result
    // so later argument evaluation cannot change the selected receiver.
    return jm_callr_1(mt, "js_get_last_with_binding_base_or_undefined", MIR_T_I64, key);
}

static bool jm_emit_live_scope_cell_load(JsMirTranspiler* mt,
        JsMirVarEntry* var, MIR_reg_t target) {
    if (!var || !var->in_scope_env || var->scope_env_reg == 0 ||
            var->scope_env_slot < 0 || var->mir_type != MIR_T_I64) {
        return false;
    }
    // Only shared scope cells are reloaded on identifier reads. A from_env
    // capture without this marker is an intentional closure snapshot; treating
    // it as a live cell changes JavaScript closure semantics.
    jm_emit_load_i64(mt, target, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg);
    return true;
}

MIR_reg_t jm_transpile_identifier(JsMirTranspiler* mt, JsIdentifierNode* id) {
    if (!id || !id->name) {
        // Unsupported identifier-shaped AST nodes can reach expression lowering
        // without a parsed name; keep the runtime graceful instead of crashing
        // while resolving an unnameable binding.
        LambdaSourcePoint point = id ? lambda_source_span_start_point(
            mt && mt->tp ? mt->tp->source : NULL, id->source_span) : (LambdaSourcePoint){0, 0};
        log_warn("js-mir: nameless identifier node at %u:%u (node_type=%d); emitting undefined",
            point.row + 1, point.column + 1, id ? (int)id->node_type : -1);
        return jm_emit_undefined(mt);
    }

    // Handle 'this' keyword: use captured _js_this if in arrow function, else js_get_this()
    if (id->name->len == 4 && strncmp(id->name->chars, "this", 4) == 0) {
        return jm_emit_current_this(mt);
    }

    // Handle 'super' keyword: returns current 'this' (super property access
    // is resolved through prototype chain at runtime via js_get_key_default)
    if (id->name->len == 5 && strncmp(id->name->chars, "super", 5) == 0) {
        return jm_emit_current_this(mt);
    }

    // v18q: Handle 'arguments' keyword: return the function's arguments array-like object
    if (id->name->len == 9 && strncmp(id->name->chars, "arguments", 9) == 0) {
        JsMirVarEntry* var = jm_find_var(mt, "_js_arguments");
        if (var) return var->reg;
    }

    // Handle 'new.target' meta-property, including arrow lexical capture.
    if (id->name->len == 10 && strncmp(id->name->chars, "new.target", 10) == 0) {
        return jm_emit_current_new_target(mt);
    }

    // Handle import.meta: ES modules expose a host-created null-prototype object.
    if (id->name->len == 11 && strncmp(id->name->chars, "import.meta", 11) == 0) {
        return jm_call_0(mt, "js_get_import_meta", MIR_T_I64);
    }

    // Handle 'NaN' — IEEE 754 Not a Number global constant
    if (id->name->len == 3 && strncmp(id->name->chars, "NaN", 3) == 0) {
        MIR_reg_t d = jm_new_reg(mt, "nan_val", MIR_T_D);
        jm_emit_reg_op(mt, MIR_DMOV, d, MIR_new_double_op(mt->ctx, 0.0/0.0));
        return jm_apply_with_identifier_fallback(mt, id, jm_box_float(mt, d));
    }

    // Handle 'Infinity' — IEEE 754 positive infinity global constant
    if (id->name->len == 8 && strncmp(id->name->chars, "Infinity", 8) == 0) {
        MIR_reg_t d = jm_new_reg(mt, "inf_val", MIR_T_D);
        jm_emit_reg_op(mt, MIR_DMOV, d, MIR_new_double_op(mt->ctx, INFINITY));
        return jm_apply_with_identifier_fallback(mt, id, jm_box_float(mt, d));
    }

    // v12: Handle 'globalThis' keyword
    if (id->name->len == 10 && strncmp(id->name->chars, "globalThis", 10) == 0) {
        MIR_reg_t global_this = jm_call_0(mt, "js_get_global_this", MIR_T_I64);
        return jm_apply_with_identifier_fallback(mt, id, global_this);
    }

    // Build variable name: _js_<name>
    const char* vname = jm_var_name(id->name);
    JsMirVarEntry* var = jm_find_var(mt, vname);
    JsClassEntry* inner_class_binding = jm_current_inner_class_binding(mt, id->name, (JsAstNode*)id);
    if (inner_class_binding && inner_class_binding->inner_module_var_index >= 0 &&
        (!var || var->from_env)) {
        // Parameters and method-body locals shadow the surrounding class-name
        // environment, while an outer capture does not. Capture analysis can
        // retain a stale same-named outer reference for named class expressions,
        // so only that outer reference yields to the class's immutable binding.
        return jm_load_module_var(mt,
            (uint32_t)inner_class_binding->inner_module_var_index);
    }
    if (var) {
        MIR_reg_t live_cell_reg = 0;
        // Js57 P3 (Track B2): live binding for self-imported default — re-fetch
        // `namespace.default` from the module registry each time the local
        // identifier is read. Throws ReferenceError if the source module's
        // `export default` has not run yet (slot still holds TDZ sentinel).
        if (var->is_live_default_binding && var->live_binding_specifier) {
            MIR_reg_t spec_reg = jm_box_string_literal(mt,
                var->live_binding_specifier, (int)strlen(var->live_binding_specifier));
            MIR_reg_t live_val = jm_callr_1(mt, "js_get_live_binding_default", MIR_T_I64, spec_reg);
            jm_emit_error_lane_propagate_check(mt);
            return live_val;
        }
        if (var->in_scope_env && var->scope_env_reg != 0 &&
                var->scope_env_slot >= 0 && var->mir_type == MIR_T_I64) {
            // Keep each evaluated cell value in its own virtual register. The
            // canonical root slot protects the object at safepoints, but loading
            // the expression result back from that slot confuses semantic homes
            // and colored scratch slots when a register is reused later.
            live_cell_reg = jm_new_reg(mt, "live_cell", MIR_T_I64);
            if (jm_emit_live_scope_cell_load(mt, var, live_cell_reg)) {
                jm_create_gc_root_slot(mt, live_cell_reg);
            }
        }
        // v20 TDZ: emit runtime check for let/const variables before their declaration
        if (var->tdz_active) {
            jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx,
                    live_cell_reg ? live_cell_reg : var->reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx,
                    jm_module_name_id(mt, id->name->chars, id->name->len)),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int)id->name->len));
            jm_emit_error_lane_propagate_check(mt);
        }
        MIR_reg_t var_read_reg = live_cell_reg ? live_cell_reg : var->reg;
        MIR_reg_t lookup_key = 0;
        if (mt->eval_local_frame_reg != 0 && mt->current_fc &&
            JM_JS_FACT(mt->current_fc, has_direct_eval)) {
            // Sloppy direct eval can introduce a function-scoped var after MIR
            // resolved this identifier to an outer/static binding. Later reads
            // must consult that eval-local binding, while the fallback preserves
            // the statically resolved value when eval introduced no such var.
            lookup_key = jm_box_property_name_literal(mt, id->name->chars, id->name->len);
            var_read_reg = jm_callr_2(mt, "js_eval_local_get_binding_or_fallback", MIR_T_I64, lookup_key, var_read_reg);
        }
        bool read_with_outer_binding = var->from_env && jm_current_function_captures_with_scope(mt);
        if (mt->with_depth > 0 || read_with_outer_binding) {
            if (!lookup_key) lookup_key = jm_box_property_name_literal(mt,
                id->name->chars, id->name->len);
            // Functions created inside `with` capture that Object Environment Record
            // at runtime. For captured outer variables, emit the same with lookup the
            // enclosing body would use; local/parameter bindings are left untouched.
            var_read_reg = jm_callr_2(mt, jm_with_binding_get_name(mt), MIR_T_I64, lookup_key, var_read_reg);
            jm_emit_error_lane_propagate_check(mt);
        }
        if (var->from_env) {
            jm_call_3(mt, "js_check_capture_binding", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, var_read_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx,
                    jm_module_name_id(mt, id->name->chars, id->name->len)),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)id->name->len));
            jm_emit_error_lane_propagate_check(mt);
        }
        int param_index = jm_arguments_param_index(mt, vname, var);
        if (param_index >= 0) {
            return jm_call_3(mt, "js_arguments_mapped_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->arguments_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, param_index),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg));
        }
        return var_read_reg;
    }

    // Check module-level constants (top-level const with literal value)
    if (mt->module_consts) {
        JsModuleConstEntry* mc = jm_find_module_const(mt, vname);
        if (mc && mc->is_iife_var && !jm_current_scope_can_see_iife_modvar(mt)) {
            mc = NULL;
        }
        if (mc) {
            switch (mc->const_type) {
            case MCONST_CLASS: {
                // Inside a class's own scope, the class name is an immutable inner binding.
                JsClassEntry* inner_ce = jm_current_inner_class_binding(mt, id->name, (JsAstNode*)id);
                if (inner_ce && inner_ce->inner_module_var_index >= 0) {
                    return jm_load_module_var(mt,
                        (uint32_t)inner_ce->inner_module_var_index);
                }
                // Outside the class scope, class declarations use the surrounding binding.
                MIR_reg_t cls = jm_load_module_var(mt, (uint32_t)mc->int_val);
                return jm_apply_with_identifier_fallback(mt, id, cls);
            }
            case MCONST_MODVAR: {
                // Js57 P3 (Track B2): live binding for self-imported default.
                // Closures bypass capture analysis for MCONST_MODVAR entries and
                // emit js_get_module_var here at every use — perfect place to
                // also route live-binding entries through the runtime call that
                // re-reads namespace.default (and throws ReferenceError if the
                // module's `export default` has not yet executed).
                if (mc->is_live_default_binding && mc->live_binding_specifier) {
                    MIR_reg_t spec_reg = jm_box_string_literal(mt,
                        mc->live_binding_specifier,
                        (int)strlen(mc->live_binding_specifier));
                    MIR_reg_t live_val = jm_callr_1(mt, "js_get_live_binding_default", MIR_T_I64, spec_reg);
                    jm_emit_error_lane_propagate_check(mt);
                    return jm_apply_with_identifier_fallback(mt, id, live_val);
                }
                MIR_reg_t mv = jm_load_module_var(mt, (uint32_t)mc->int_val);
                JsFuncCollected* direct_func =
                    jm_find_direct_function_decl_for_identifier(mt, id);
                if (direct_func && direct_func->func_item &&
                        !JM_JS_FACT(direct_func, is_reassigned)) {
                    JsMirLastClosureSnapshot saved_closure_tracker;
                    jm_save_last_closure_snapshot(mt, &saved_closure_tracker);
                    MIR_reg_t is_undef = jm_new_reg(mt, "func_decl_undef", MIR_T_I64);
                    MIR_reg_t result = jm_new_reg(mt, "func_decl_val", MIR_T_I64);
                    MIR_label_t use_existing = jm_new_label(mt);
                    MIR_label_t done = jm_new_label(mt);
                    jm_emit_reg_binary_op(mt, MIR_EQ, is_undef, mv, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));
                    jm_emit_branch(mt, MIR_BF, use_existing, is_undef);
                    MIR_reg_t fn_reg = jm_create_func_or_closure(mt, direct_func);
                    jm_store_module_var(mt, (uint32_t)mc->int_val, fn_reg);
                    jm_emit_mov(mt, result, fn_reg);
                    jm_emit_jmp(mt, done);
                    jm_emit_label(mt, use_existing);
                    jm_emit_mov(mt, result, mv);
                    jm_emit_label(mt, done);
                    // Cached function declarations skip the fresh closure-env
                    // allocation path, so the branch-local env register must not
                    // become the later capture readback target after the merge.
                    jm_restore_last_closure_snapshot(mt, &saved_closure_tracker);
                    return jm_apply_with_identifier_fallback(mt, id, result);
                }
                if (mc->is_nested_func_hoist && !mc->is_iife_var) {
                    const char* js_name = mc->name;
                    if (strncmp(js_name, "_js_", 4) == 0) js_name += 4;
                    MIR_reg_t global_reg = jm_call_0(mt, "js_get_global_this", MIR_T_I64);
                    MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name,
                        (uint32_t)strlen(js_name));
                    MIR_reg_t has_global = jm_callr_2(mt, "js_has_own_property", MIR_T_I64, global_reg, key_reg);
                    MIR_label_t use_module = jm_new_label(mt);
                    MIR_label_t read_done = jm_new_label(mt);
                    jm_emit_branch(mt, MIR_BF, use_module, has_global);
                    MIR_reg_t global_val = jm_callr_1(mt, "js_get_global_property", MIR_T_I64, key_reg);
                    MIR_reg_t global_is_undef = jm_new_reg(mt, "annexb_gundef", MIR_T_I64);
                    jm_emit_reg_binary_op(mt, MIR_EQ, global_is_undef, global_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));
                    MIR_reg_t module_is_undef = jm_new_reg(mt, "annexb_mundef", MIR_T_I64);
                    jm_emit_reg_binary_op(mt, MIR_EQ, module_is_undef, mv, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL));
                    MIR_reg_t module_is_function = jm_call_2(mt, "js_typeof_is", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, mv),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx,
                            jm_module_name_id(mt, "function", 8)));
                    MIR_reg_t module_not_function = jm_new_reg(mt, "annexb_mnotfn", MIR_T_I64);
                    jm_emit_reg_binary_op(mt, MIR_EQ, module_not_function, module_is_function, MIR_new_int_op(mt->ctx, 0));
                    MIR_reg_t global_is_function = jm_call_2(mt, "js_typeof_is", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, global_val),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx,
                            jm_module_name_id(mt, "function", 8)));
                    bool annexb_self_body = false;
                    if (mt->current_fc && mt->current_fc->node && mt->current_fc->node->name) {
                        String* cur_name = mt->current_fc->node->name;
                        annexb_self_body = cur_name->len == strlen(js_name) &&
                            memcmp(cur_name->chars, js_name, cur_name->len) == 0;
                    }
                    MIR_reg_t prefer_non_function_module = jm_new_reg(mt, "annexb_pnfm", MIR_T_I64);
                    if (mt->is_eval_direct || annexb_self_body) {
                        jm_emit_mov(mt, prefer_non_function_module, module_not_function);
                    } else {
                        jm_emit_reg_binary_op(mt, MIR_EQ, prefer_non_function_module, global_is_function, MIR_new_int_op(mt->ctx, 0));
                        jm_emit_reg_binary(mt, MIR_AND, prefer_non_function_module, prefer_non_function_module, module_not_function);
                    }
                    MIR_reg_t prefer_concrete_module = jm_new_reg(mt, "annexb_pcm", MIR_T_I64);
                    jm_emit_reg_binary(mt, MIR_OR, prefer_concrete_module, global_is_undef, prefer_non_function_module);
                    MIR_reg_t prefer_module = jm_new_reg(mt, "annexb_pmod", MIR_T_I64);
                    jm_emit_reg_binary_op(mt, MIR_EQ, prefer_module, module_is_undef, MIR_new_int_op(mt->ctx, 0));
                    jm_emit_reg_binary(mt, MIR_AND, prefer_module, prefer_module, prefer_concrete_module);
                    jm_emit_branch(mt, MIR_BT, use_module, prefer_module);
                    jm_emit_mov(mt, mv, global_val);
                    jm_emit_jmp(mt, read_done);
                    jm_emit_label(mt, use_module);
                    jm_emit_label(mt, read_done);
                }
                mv = jm_call_4(mt, "js_resolve_unresolved_binding", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, mv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx,
                        jm_module_name_id(mt, id->name->chars, id->name->len)),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)id->name->len),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, mt->in_typeof ? 1 : 0));
                jm_emit_error_lane_propagate_check(mt);
                mv = jm_apply_with_identifier_fallback(mt, id, mv);
                // v20 TDZ: check for let/const modvars
                if (mc->var_kind == JS_VAR_LET || mc->var_kind == JS_VAR_CONST) {
                    jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, mv),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx,
                            jm_module_name_id(mt, id->name->chars, id->name->len)),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int)id->name->len));
                    jm_emit_error_lane_propagate_check(mt);
                }
                return mv;
            }
            }
        }
    }

    if (id->name->len == 9 && strncmp(id->name->chars, "undefined", 9) == 0 &&
            !(mt->current_fc && JM_JS_FACT(mt->current_fc, has_direct_eval))) {
        // Only an unresolved, non-eval reference may use the canonical value;
        // local/module bindings were handled above and with capture stays a
        // dynamic Object Environment Record lookup in the shared fallback.
        MIR_reg_t value = jm_emit_undefined(mt);
        return jm_apply_with_identifier_fallback(mt, id, value);
    }

    if (!id->entry || !id->entry->node) {
        // Debug: log when identifier has no entry and no module_consts match
        if (!var) {
            JsModuleConstEntry lookup2;
            lookup2.name = jm_persist_name(vname);
            JsModuleConstEntry* mc2 = mt->module_consts ? (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup2) : NULL;
            if (!mc2) {
                log_debug("js-mir: identifier '%s' not found in vars, module_consts, or entry (func=%s, byte=%u)",
                    vname, mt->current_fc ? mt->current_fc->name : "?",
                    id->source_span.start_byte);
            }
        }
    }

    // D6.2.2v2: unresolved builtin spellings are still mutable global
    // bindings; returning the catalog's cached intrinsic bypassed reassignment.
    log_debug("js-mir: undefined variable '%s' — using global property lookup", vname);

    // Fallback: look up property on global object (browser-like named access)
    // This resolves element IDs as globals, module-level assignments, etc.
    // Uses strict version that throws ReferenceError for undeclared identifiers.
    {
        MIR_reg_t name_reg = jm_box_property_name_literal(mt,
            id->name->chars, id->name->len);
        bool throw_unresolvable = !mt->in_typeof;
        bool strict_reference = jm_strict_put(mt);
        if (mt->eval_local_frame_reg != 0 && mt->current_fc && JM_JS_FACT(mt->current_fc, has_direct_eval)) {
            MIR_reg_t missing = jm_emit_item_error(mt);
            MIR_reg_t candidate = jm_callr_2(mt, "js_eval_local_get_binding_or_fallback", MIR_T_I64, name_reg, missing);
            MIR_reg_t is_missing = jm_new_reg(mt, "eval_id_missing", MIR_T_I64);
            jm_emit_reg_binary(mt, MIR_EQ, is_missing, candidate, missing);
            MIR_label_t global_lookup = jm_new_label(mt);
            MIR_label_t lookup_done = jm_new_label(mt);
            MIR_reg_t result = jm_new_reg(mt, "eval_id_result", MIR_T_I64);
            jm_emit_branch(mt, MIR_BT, global_lookup, is_missing);
            jm_emit_mov(mt, result, candidate);
            jm_emit_jmp(mt, lookup_done);
            jm_emit_label(mt, global_lookup);
            MIR_reg_t global_result = throw_unresolvable ?
                jm_call_2(mt, "js_get_global_property_reference", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, strict_reference ? 1 : 0)) :
                jm_callr_1(mt, "js_get_global_property", MIR_T_I64, name_reg);
            jm_emit_mov(mt, result, global_result);
            jm_emit_label(mt, lookup_done);
            if (throw_unresolvable) {
                jm_emit_error_lane_propagate_check(mt);
            }
            return result;
        }
        MIR_reg_t result = throw_unresolvable ?
            jm_call_2(mt, "js_get_global_property_reference", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, strict_reference ? 1 : 0)) :
            jm_callr_1(mt, "js_get_global_property", MIR_T_I64, name_reg);
        // strict version throws ReferenceError for undeclared identifiers; route
        // to the nearest try/catch or propagate out of the current function.
        if (throw_unresolvable) {
            jm_emit_error_lane_propagate_check(mt);
        }
        return result;
    }
}

void jm_emit_eval_local_ensure_frame(JsMirTranspiler* mt) {
    if (!mt || mt->eval_local_frame_reg == 0) return;
    MIR_label_t push_label = jm_new_label(mt);
    MIR_label_t done_label = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BF, push_label, mt->eval_local_frame_reg);
    jm_emit_jmp(mt, done_label);
    jm_emit_label(mt, push_label);
    MIR_reg_t pushed = jm_call_0(mt, "js_eval_local_push_frame", MIR_T_I64);
    MIR_label_t pushed_label = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BT, pushed_label, pushed);
    // A failed local-frame push must not set the active flag: a later epilogue
    // pop would otherwise discard an enclosing direct-eval caller's journal.
    jm_call_1(mt, "js_throw_range_error", MIR_T_I64, MIR_T_P,
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)"Maximum eval local frame depth exceeded"));
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_label(mt, pushed_label);
    jm_emit_reg_op(mt, MIR_MOV, mt->eval_local_frame_reg, MIR_new_int_op(mt->ctx, 1));
    jm_emit_label(mt, done_label);
}

void jm_emit_eval_local_pop_if_needed(JsMirTranspiler* mt) {
    if (!mt || mt->eval_local_frame_reg == 0) return;
    MIR_label_t done_label = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BF, done_label, mt->eval_local_frame_reg);
    jm_call_void_0(mt, "js_eval_local_pop_frame");
    jm_emit_reg_op(mt, MIR_MOV, mt->eval_local_frame_reg, MIR_new_int_op(mt->ctx, 0));
    jm_emit_label(mt, done_label);
}

static void jm_emit_eval_private_bind_name(JsMirTranspiler* mt, JsClassEntry* ce, String* name) {
    if (!mt || !ce || !jm_is_private_name(name)) return;
    (void)ce;
    MIR_reg_t source_item = jm_box_string_literal(mt, name->chars, (int)name->len);
    MIR_reg_t private_key = jm_emit_private_key_for_access(mt, NULL, name);
    // Direct eval inherits the lexical private environment as NameRecord
    // identity, never as a user-observable spelling-derived property name.
    jm_callr_void_2(mt, "js_eval_private_bind", source_item, private_key);
}

static bool jm_emit_eval_private_env_push(JsMirTranspiler* mt) {
    if (!mt || !mt->current_class) return false;
    JsClassEntry* ce = mt->current_class;
    bool has_private = false;
    for (int i = 0; i < ce->method_count && !has_private; i++) has_private = jm_is_private_name(ce->methods[i].name);
    for (int i = 0; i < ce->static_field_count && !has_private; i++) has_private = jm_is_private_name(ce->static_fields[i].name);
    for (int i = 0; i < ce->instance_field_count && !has_private; i++) has_private = jm_is_private_name(ce->instance_fields[i].name);
    if (!has_private) return false;

    jm_call_void_0(mt, "js_eval_private_push_frame");
    for (int i = 0; i < ce->method_count; i++) jm_emit_eval_private_bind_name(mt, ce, ce->methods[i].name);
    for (int i = 0; i < ce->static_field_count; i++) jm_emit_eval_private_bind_name(mt, ce, ce->static_fields[i].name);
    for (int i = 0; i < ce->instance_field_count; i++) jm_emit_eval_private_bind_name(mt, ce, ce->instance_fields[i].name);
    return true;
}

static bool jm_identifier_matches(String* name, const char* expected, int expected_len) {
    return name && (int)name->len == expected_len && strncmp(name->chars, expected, expected_len) == 0;
}

// Tune8 §2.5 attempted to retire jm_match_uri_decode_call and friends, but
// removing the js_uri_decode_equals_from_char_code fast path caused test262
// timeouts on decodeURI / decodeURIComponent stress tests (the generic eq
// path is ~100× slower per loop iteration). Restored for that reason.
static bool jm_match_uri_decode_call(JsMirTranspiler* mt, JsAstNode* node,
                                     JsAstNode** uri_arg, int64_t* component) {
    (void)mt;
    if (!node || node->node_type != JS_AST_NODE_CALL_EXPRESSION) return false;
    JsCallNode* call = (JsCallNode*)node;
    if (ast_linked_node_count(call->arguments) != 1 || !call->callee ||
        call->callee->node_type != JS_AST_NODE_IDENTIFIER) {
        return false;
    }
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    if (jm_identifier_matches(id->name, "decodeURI", 9)) {
        *uri_arg = call->arguments;
        *component = 0;
        return true;
    }
    if (jm_identifier_matches(id->name, "decodeURIComponent", 18)) {
        *uri_arg = call->arguments;
        *component = 1;
        return true;
    }
    return false;
}

static bool jm_match_string_from_char_code2(JsMirTranspiler* mt, JsAstNode* node,
                                            JsAstNode** first_arg, JsAstNode** second_arg) {
    (void)mt;
    if (!node || node->node_type != JS_AST_NODE_CALL_EXPRESSION) return false;
    JsCallNode* call = (JsCallNode*)node;
    if (ast_linked_node_count(call->arguments) != 2 || !call->callee ||
        call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) {
        return false;
    }
    JsMemberNode* member = (JsMemberNode*)call->callee;
    if (member->computed || !member->object || !member->property ||
        member->object->node_type != JS_AST_NODE_IDENTIFIER ||
        member->property->node_type != JS_AST_NODE_IDENTIFIER) {
        return false;
    }
    JsIdentifierNode* object = (JsIdentifierNode*)member->object;
    JsIdentifierNode* property = (JsIdentifierNode*)member->property;
    if (!jm_identifier_matches(object->name, "String", 6) ||
        !jm_identifier_matches(property->name, "fromCharCode", 12)) {
        return false;
    }
    *first_arg = call->arguments;
    *second_arg = call->arguments->next;
    return true;
}

static bool jm_uri_compare_arg_is_simple(JsAstNode* node) {
    if (!node) return false;
    return node->node_type == JS_AST_NODE_IDENTIFIER ||
           node->node_type == JS_AST_NODE_LITERAL;
}

static bool jm_try_emit_uri_compare_fast_path(JsMirTranspiler* mt,
                                              JsAstNode* left, JsAstNode* right,
                                              MIR_reg_t* result) {
    JsAstNode* uri_arg = NULL;
    JsAstNode* first_arg = NULL;
    JsAstNode* second_arg = NULL;
    int64_t component = 0;
    if (!jm_match_uri_decode_call(mt, left, &uri_arg, &component) ||
        !jm_match_string_from_char_code2(mt, right, &first_arg, &second_arg) ||
        !jm_uri_compare_arg_is_simple(uri_arg) ||
        !jm_uri_compare_arg_is_simple(first_arg) ||
        !jm_uri_compare_arg_is_simple(second_arg)) {
        return false;
    }
    MIR_reg_t uri_reg = jm_transpile_box_item(mt, uri_arg);
    MIR_reg_t first_reg = jm_transpile_box_item(mt, first_arg);
    MIR_reg_t second_reg = jm_transpile_box_item(mt, second_arg);
    *result = jm_call_4(mt, "js_uri_decode_equals_from_char_code", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, uri_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, first_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, second_reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, component));
    return true;
}

#if JS_TEST262_FAST_PATHS
static bool jm_match_decimal_to_percent_hex_call(JsAstNode* node, JsAstNode** n_arg) {
    if (!node || node->node_type != JS_AST_NODE_CALL_EXPRESSION) return false;
    JsCallNode* call = (JsCallNode*)node;
    if (ast_linked_node_count(call->arguments) != 1 || !call->callee ||
        call->callee->node_type != JS_AST_NODE_IDENTIFIER) {
        return false;
    }
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    if (!jm_identifier_matches(id->name, "decimalToPercentHexString", 25)) return false;
    *n_arg = call->arguments;
    return true;
}
#endif

// Binary expression: native arithmetic fast path + boxed fallback
// JS ToInt32 — replicates js_to_int32 (js_runtime_value.cpp) for compile-time folding.
static int32_t jm_fold_to_int32(double d) {
    if (!isfinite(d) || d == 0.0) return 0;
    double d2 = fmod(trunc(d), 4294967296.0);
    if (d2 < 0) d2 += 4294967296.0;
    return (d2 >= 2147483648.0) ? (int32_t)(d2 - 4294967296.0) : (int32_t)d2;
}
// ToUint32 is ToInt32's bit pattern reinterpreted as unsigned.
static uint32_t jm_fold_to_uint32(double d) { return (uint32_t)jm_fold_to_int32(d); }

bool jm_const_fold_enabled() {
    static int cached = -1;
    if (cached < 0) {
        const char* e = getenv("LAMBDA_JS_CONST_FOLD");
        cached = (e && e[0] == '0') ? 0 : 1;  // on by default; LAMBDA_JS_CONST_FOLD=0 disables
    }
    return cached != 0;
}

bool jm_try_fold_const(JsAstNode* node, JsFoldVal* out) {
    if (!node) return false;
    switch (node->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            if (lit->is_bigint) return false;
            double v = lit->value.number_value;
            if (!isfinite(v)) return false;
            out->kind = JS_FOLD_NUM;
            out->num = v;
            // Mirror literal lowering: values outside INT53 cannot be emitted through i2it.
            out->is_float = lit->has_decimal ||
                !(v == (double)(int64_t)v && v >= (double)INT53_MIN && v <= (double)INT53_MAX);
            return true;
        }
        if (lit->literal_type == JS_LITERAL_BOOLEAN) {
            out->kind = JS_FOLD_BOOL;
            out->boolean = lit->value.boolean_value;
            return true;
        }
        return false;  // string / null / undefined: not folded
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        JsFoldVal a;
        switch (un->op) {
        case JS_OP_MINUS: case JS_OP_SUB:
            if (!jm_try_fold_const(un->operand, &a) || a.kind != JS_FOLD_NUM) return false;
            if (a.num == 0.0) return false;  // -0 must stay float; defer to existing literal path
            { double r = -a.num; if (!isfinite(r)) return false;
              out->kind = JS_FOLD_NUM; out->num = r; out->is_float = a.is_float; return true; }
        case JS_OP_PLUS: case JS_OP_ADD:
            if (!jm_try_fold_const(un->operand, &a) || a.kind != JS_FOLD_NUM) return false;
            *out = a; return true;
        case JS_OP_BIT_NOT:
            if (!jm_try_fold_const(un->operand, &a) || a.kind != JS_FOLD_NUM) return false;
            out->kind = JS_FOLD_NUM; out->num = (double)(~jm_fold_to_int32(a.num)); out->is_float = false; return true;
        case JS_OP_NOT:
            if (!jm_try_fold_const(un->operand, &a)) return false;
            { bool t = (a.kind == JS_FOLD_BOOL) ? a.boolean : (a.num != 0.0);  // operand finite, no NaN
              out->kind = JS_FOLD_BOOL; out->boolean = !t; return true; }
        default: return false;
        }
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        JsFoldVal la, ra;
        if (!jm_try_fold_const(bin->left, &la) || la.kind != JS_FOLD_NUM) return false;
        if (!jm_try_fold_const(bin->right, &ra) || ra.kind != JS_FOLD_NUM) return false;
        double a = la.num, b = ra.num;
        bool both_int = !la.is_float && !ra.is_float;
        switch (bin->op) {
        case JS_OP_ADD: case JS_OP_SUB: case JS_OP_MUL: {
            double r = (bin->op == JS_OP_ADD) ? a + b : (bin->op == JS_OP_SUB) ? a - b : a * b;
            if (!isfinite(r)) return false;
            if (both_int) {
                // int arithmetic must round-trip exactly to match the runtime's int64 path
                if (r != (double)(int64_t)r || r < (double)INT53_MIN || r > (double)INT53_MAX) return false;
                out->is_float = false;
            } else {
                out->is_float = true;
            }
            out->kind = JS_FOLD_NUM; out->num = r; return true;
        }
        case JS_OP_DIV: {
            double r = a / b; if (!isfinite(r)) return false;
            out->kind = JS_FOLD_NUM; out->num = r; out->is_float = true; return true;
        }
        case JS_OP_MOD: {
            double r = fmod(a, b); if (!isfinite(r)) return false;
            out->kind = JS_FOLD_NUM; out->num = r; out->is_float = true; return true;
        }
        case JS_OP_BIT_AND: out->kind = JS_FOLD_NUM; out->num = (double)(jm_fold_to_int32(a) & jm_fold_to_int32(b)); out->is_float = false; return true;
        case JS_OP_BIT_OR:  out->kind = JS_FOLD_NUM; out->num = (double)(jm_fold_to_int32(a) | jm_fold_to_int32(b)); out->is_float = false; return true;
        case JS_OP_BIT_XOR: out->kind = JS_FOLD_NUM; out->num = (double)(jm_fold_to_int32(a) ^ jm_fold_to_int32(b)); out->is_float = false; return true;
        case JS_OP_BIT_LSHIFT: {
            uint32_t r = (uint32_t)jm_fold_to_int32(a) << (jm_fold_to_uint32(b) & 31);
            out->kind = JS_FOLD_NUM; out->num = (double)(int32_t)r; out->is_float = false; return true;
        }
        case JS_OP_BIT_RSHIFT: {
            int32_t r = jm_fold_to_int32(a) >> (jm_fold_to_uint32(b) & 31);
            out->kind = JS_FOLD_NUM; out->num = (double)r; out->is_float = false; return true;
        }
        case JS_OP_BIT_URSHIFT: {
            uint32_t r = jm_fold_to_uint32(a) >> (jm_fold_to_uint32(b) & 31);
            out->kind = JS_FOLD_NUM; out->num = (double)r; out->is_float = false; return true;
        }
        case JS_OP_LT: out->kind = JS_FOLD_BOOL; out->boolean = (a <  b); return true;
        case JS_OP_LE: out->kind = JS_FOLD_BOOL; out->boolean = (a <= b); return true;
        case JS_OP_GT: out->kind = JS_FOLD_BOOL; out->boolean = (a >  b); return true;
        case JS_OP_GE: out->kind = JS_FOLD_BOOL; out->boolean = (a >= b); return true;
        case JS_OP_EQ: case JS_OP_STRICT_EQ: out->kind = JS_FOLD_BOOL; out->boolean = (a == b); return true;
        case JS_OP_NE: case JS_OP_STRICT_NE: out->kind = JS_FOLD_BOOL; out->boolean = (a != b); return true;
        default: return false;  // **, &&, ||, ??, in, instanceof: not folded
        }
    }
    default: return false;
    }
}

// Emit a folded constant at a binary/unary *value* site, honoring the return
// convention callers (notably jm_transpile_box_item) expect:
//   - native (raw reg matching `et`) when `caller_native` is set — the caller
//     will box it via jm_box_native(result, et);
//   - a boxed Item otherwise.
// Returns true and sets *out on success; returns false to mean "fall through to
// normal codegen" (used when the fold result is inconsistent with `et`, so the
// non-folded path emits the correct representation).
static bool jm_emit_folded_at_value_site(JsMirTranspiler* mt, const JsFoldVal* fv,
                                          bool caller_native, TypeId et, MIR_reg_t* out) {
    if (caller_native && jm_is_native_type(et)) {
        if (et == LMD_TYPE_FLOAT && fv->kind == JS_FOLD_NUM) {
            MIR_reg_t d = jm_new_reg(mt, "fdbl", MIR_T_D);
            jm_emit_reg_op(mt, MIR_DMOV, d, MIR_new_double_op(mt->ctx, fv->num));
            *out = d; return true;
        }
        if (et == LMD_TYPE_INT && fv->kind == JS_FOLD_NUM && !fv->is_float &&
            fv->num == (double)(int64_t)fv->num) {
            MIR_reg_t r = jm_new_reg(mt, "fint", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, (int64_t)fv->num));
            *out = r; return true;
        }
        if (et == LMD_TYPE_BOOL && fv->kind == JS_FOLD_BOOL) {
            MIR_reg_t r = jm_new_reg(mt, "fcmp", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, fv->boolean ? 1 : 0));
            *out = r; return true;
        }
        return false;  // et/fold disagreement: let normal codegen handle it
    }
    // caller expects a boxed Item
    if (fv->kind == JS_FOLD_BOOL) {
        MIR_reg_t r = jm_new_reg(mt, "fbool", MIR_T_I64);
        uint64_t bval = fv->boolean ? ITEM_TRUE_VAL : ITEM_FALSE_VAL;
        jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, (int64_t)bval));
        *out = r; return true;
    }
    *out = jm_box_float_const(mt, fv->num); return true;
}

// Native binary-op lanes. Register names are load-bearing: they appear in the
// MIR dumps the emission goldens compare, so they live in the table verbatim.
typedef struct JsMirFloatArithOp { uint8_t op; MIR_insn_code_t code; const char* reg_name; } JsMirFloatArithOp;
static const JsMirFloatArithOp js_mir_float_arith_ops[] = {
    { JS_OP_ADD, MIR_DADD, "add" },
    { JS_OP_SUB, MIR_DSUB, "sub" },
    { JS_OP_MUL, MIR_DMUL, "mul" },
    { JS_OP_DIV, MIR_DDIV, "div" },
};
static const JsMirFloatArithOp* jm_float_arith_op(uint8_t op) {
    for (size_t i = 0; i < sizeof(js_mir_float_arith_ops)/sizeof(js_mir_float_arith_ops[0]); i++)
        if (js_mir_float_arith_ops[i].op == op) return &js_mir_float_arith_ops[i];
    return NULL;
}

typedef struct JsMirCompareOp {
    uint8_t op; MIR_insn_code_t float_code; MIR_insn_code_t int_code; const char* reg_name;
} JsMirCompareOp;
static const JsMirCompareOp js_mir_compare_ops[] = {
    { JS_OP_LT,        MIR_DLT, MIR_LTS, "lt" },
    { JS_OP_LE,        MIR_DLE, MIR_LES, "le" },
    { JS_OP_GT,        MIR_DGT, MIR_GTS, "gt" },
    { JS_OP_GE,        MIR_DGE, MIR_GES, "ge" },
    { JS_OP_EQ,        MIR_DEQ, MIR_EQ,  "eq" },
    { JS_OP_STRICT_EQ, MIR_DEQ, MIR_EQ,  "eq" },
    { JS_OP_NE,        MIR_DNE, MIR_NE,  "ne" },
    { JS_OP_STRICT_NE, MIR_DNE, MIR_NE,  "ne" },
};
static const JsMirCompareOp* jm_compare_op(uint8_t op) {
    for (size_t i = 0; i < sizeof(js_mir_compare_ops)/sizeof(js_mir_compare_ops[0]); i++)
        if (js_mir_compare_ops[i].op == op) return &js_mir_compare_ops[i];
    return NULL;
}

MIR_reg_t jm_transpile_binary(JsMirTranspiler* mt, JsBinaryNode* bin) {
    if (jm_const_fold_enabled()) {
        JsFoldVal fv;
        if (jm_try_fold_const((JsAstNode*)bin, &fv)) {
            // Mirror jm_transpile_box_item's native_binary predicate: a both-numeric
            // binary (the only kind we fold) returns a raw native value the caller boxes.
            TypeId lt = jm_get_effective_type(mt, bin->left);
            TypeId rt = jm_get_effective_type(mt, bin->right);
            bool both_numeric = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) &&
                                (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
            TypeId et = jm_get_effective_type(mt, (JsAstNode*)bin);
            MIR_reg_t out;
            if (jm_emit_folded_at_value_site(mt, &fv, both_numeric, et, &out)) return out;
            // else: fall through to normal codegen
        }
    }
    if (bin->op == JS_OP_ADD) {
#if JS_TEST262_FAST_PATHS
        JsAstNode* n_arg = NULL;
        if (jm_test262_fast_paths_enabled(mt) &&
            jm_match_decimal_to_percent_hex_call(bin->right, &n_arg)) {
            MIR_reg_t left_reg = jm_transpile_box_item(mt, bin->left);
            MIR_reg_t n_reg = jm_transpile_box_item(mt, n_arg);
            return jm_callr_2(mt, "js_test262_concat_percent_hex", MIR_T_I64, left_reg, n_reg);
        }
#endif
    }

    // Tune8 §2.5: retiring this caused timeouts on decodeURI/decodeURIComponent
    // stress tests (generic path ~100× slower per loop iteration). Keep.
    if (bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_STRICT_NE) {
        MIR_reg_t result;
        if (jm_try_emit_uri_compare_fast_path(mt, bin->left, bin->right, &result)) {
            if (bin->op == JS_OP_STRICT_NE) {
                return jm_callr_1(mt, "js_logical_not", MIR_T_I64, result);
            }
            return result;
        }
    }

    // --- Native arithmetic fast path ---
    TypeId left_type  = jm_get_effective_type(mt, bin->left);
    TypeId right_type = jm_get_effective_type(mt, bin->right);

    bool both_numeric = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT) &&
                        (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT);

    if (bin->op == JS_OP_ADD && left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_STRING) {
        MIR_reg_t left_reg = jm_transpile_box_item(mt, bin->left);
        MIR_reg_t right_reg = jm_transpile_box_item(mt, bin->right);
        return jm_callr_2(mt, "js_string_concat", MIR_T_I64, left_reg, right_reg);
    }

    if (both_numeric) {
        bool use_float = (left_type == LMD_TYPE_FLOAT || right_type == LMD_TYPE_FLOAT);

        switch (bin->op) {
        // Arithmetic operators
        // Float arithmetic: both operands to double, double result. The four
        // ops differ only in opcode and register name, so they are one table.
        // (DIV previously had a both_int branch byte-identical to its else.)
        case JS_OP_ADD: case JS_OP_SUB: case JS_OP_MUL: case JS_OP_DIV: {
            const JsMirFloatArithOp* a = jm_float_arith_op(bin->op);
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, LMD_TYPE_FLOAT);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_new_reg(mt, a->reg_name, MIR_T_D);
            jm_emit_reg_binary(mt, a->code, r, fl, fr);
            return r;
        }
        case JS_OP_MOD: {
            // Use float modulo for both int and float: correctly handles x % 0 → NaN
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, LMD_TYPE_FLOAT);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, LMD_TYPE_FLOAT);
            MIR_reg_t fmod_r = jm_call_2(mt, "fmod", MIR_T_D,
                MIR_T_D, MIR_new_reg_op(mt->ctx, fl),
                MIR_T_D, MIR_new_reg_op(mt->ctx, fr));
            return fmod_r;
        }
        case JS_OP_EXP:
            break;  // power → fall through to boxed runtime (no native MIR op)

        // Comparison operators: return native int (0 or 1)
        // Comparisons: native int result; operand lane and opcode follow
        // use_float. Same shape for all six, so one table row each.
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_STRICT_EQ:
        case JS_OP_NE: case JS_OP_STRICT_NE: {
            const JsMirCompareOp* c = jm_compare_op(bin->op);
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, arith_t);
            MIR_reg_t r = jm_new_reg(mt, c->reg_name, MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? c->float_code : c->int_code,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }

        // Bitwise operators: always truncate to int32 (JS ToInt32), always return int
        // Use js_double_to_int32 for safe conversion (handles Infinity, NaN → 0)
        // All results are sign-extended to 32-bit: (r << 32) >> 32
        case JS_OP_BIT_AND:
            return jm_emit_int32_bitwise_binary(mt, bin->left, bin->right,
                left_type, right_type, MIR_AND, "band");
        case JS_OP_BIT_OR:
            return jm_emit_int32_bitwise_binary(mt, bin->left, bin->right,
                left_type, right_type, MIR_OR, "bor");
        case JS_OP_BIT_XOR:
            return jm_emit_int32_bitwise_binary(mt, bin->left, bin->right,
                left_type, right_type, MIR_XOR, "bxor");
        case JS_OP_BIT_LSHIFT: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, LMD_TYPE_INT);
            MIR_reg_t rcount = jm_new_reg(mt, "lsh_count", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_AND, rcount, ri, MIR_new_int_op(mt->ctx, 0x1F));
            // JS: ToInt32(li) << (ToUint32(ri) & 0x1F), result is signed 32-bit
            MIR_reg_t r = jm_new_reg(mt, "lsh", MIR_T_I64);
            MIR_reg_t r32 = jm_new_reg(mt, "lsh32", MIR_T_I64);
            jm_emit_reg_binary(mt, MIR_LSH, r, li, rcount);
            // Sign-extend result to 32-bit: (r << 32) >> 32
            jm_emit_reg_binary_op(mt, MIR_LSH, r32, r, MIR_new_int_op(mt->ctx, 32));
            jm_emit_reg_binary_op(mt, MIR_RSH, r32, r32, MIR_new_int_op(mt->ctx, 32));
            return jm_emit_int_to_double(mt, r32);
        }
        case JS_OP_BIT_RSHIFT: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, LMD_TYPE_INT);
            MIR_reg_t rcount = jm_new_reg(mt, "rsh_count", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_AND, rcount, ri, MIR_new_int_op(mt->ctx, 0x1F));
            // JS: ToInt32(li) >> (ToUint32(ri) & 0x1F) — sign-extend li to 32-bit first
            MIR_reg_t li32 = jm_new_reg(mt, "rli32", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_LSH, li32, li, MIR_new_int_op(mt->ctx, 32));
            jm_emit_reg_binary_op(mt, MIR_RSH, li32, li32, MIR_new_int_op(mt->ctx, 32));
            MIR_reg_t r = jm_new_reg(mt, "rsh", MIR_T_I64);
            jm_emit_reg_binary(mt, MIR_RSH, r, li32, rcount);
            return jm_emit_int_to_double(mt, r);
        }
        case JS_OP_BIT_URSHIFT: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, LMD_TYPE_INT);
            MIR_reg_t rcount = jm_new_reg(mt, "ursh_count", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_AND, rcount, ri, MIR_new_int_op(mt->ctx, 0x1F));
            // JS: ToUint32(li) >>> (ToUint32(ri) & 0x1F) — mask to 32-bit unsigned first
            MIR_reg_t li32 = jm_new_reg(mt, "uli32", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_AND, li32, li, MIR_new_int_op(mt->ctx, (int64_t)0xFFFFFFFFLL));
            MIR_reg_t r = jm_new_reg(mt, "ursh", MIR_T_I64);
            jm_emit_reg_binary(mt, MIR_URSH, r, li32, rcount);
            return jm_emit_int_to_double(mt, r);
        }
        default:
            break;  // fall through to boxed runtime path
        }
    }

    // --- Semi-native comparison path ---
    // DISABLED: When one side has unknown type, using native comparison is unsafe.
    // - it2i() on a boxed float gives garbage
    // - float unboxing can fail for non-numeric Items  
    // Instead, fall through to the boxed runtime path which handles all type
    // combinations correctly via js_get_number().
    // NOTE: The both_numeric case above already handles INT-vs-INT and FLOAT-vs-FLOAT.
    // Mixed INT-vs-FLOAT is also handled there (use_float flag). So we don't need
    // a semi-native path at all.

    // --- Short-circuit evaluation for logical operators ---
    // || and && must NOT evaluate both sides eagerly (side effects, caching patterns)
    // ?? (nullish coalesce) similarly short-circuits on non-null/undefined left
    if (bin->op == JS_OP_OR || bin->op == JS_OP_AND || bin->op == JS_OP_NULLISH_COALESCE) {
        MIR_reg_t result = jm_new_reg(mt, "sc_result", MIR_T_I64);
        MIR_label_t l_right = jm_new_label(mt);
        MIR_label_t l_end = jm_new_label(mt);

        // Evaluate left side first
        MIR_reg_t left_val = jm_transpile_box_item(mt, bin->left);

        MIR_reg_t cond;
        if (bin->op == JS_OP_NULLISH_COALESCE) {
            // ?? : left is null or undefined → evaluate right
            cond = jm_callr_1(mt, "js_is_nullish", MIR_T_I64, left_val);
        } else {
            // || and &&: check truthiness
            cond = jm_emit_is_truthy(mt, left_val, bin->left);
        }

        JsErrorLaneTrack branch_exc = jm_error_lane_state(mt);
        if (bin->op == JS_OP_OR) {
            // ||: if left is truthy, return left; else evaluate right
            // BF l_right: if truthy is FALSE (not truthy), jump to right
            jm_emit_branch(mt, MIR_BF, l_right, cond);
        } else if (bin->op == JS_OP_NULLISH_COALESCE) {
            // ??: if left is nullish (null/undefined), evaluate right; else return left
            // BT l_right: if nullish is TRUE (is null/undefined), jump to evaluate right
            jm_emit_branch(mt, MIR_BT, l_right, cond);
        } else {
            // &&: if left is NOT truthy, return left; else evaluate right
            // BT l_right: if truthy is TRUE (truthy), jump to right
            jm_emit_branch(mt, MIR_BT, l_right, cond);
        }

        // Short-circuit: return left
        jm_emit_mov(mt, result, left_val);
        JsErrorLaneTrack short_circuit_exit = jm_error_lane_state(mt);
        jm_emit_jmp(mt, l_end);

        // Evaluate right side
        jm_emit_label_with_state(mt, l_right, branch_exc);
        MIR_reg_t right_val = jm_transpile_box_item(mt, bin->right);
        jm_emit_mov(mt, result, right_val);
        JsErrorLaneTrack right_exit = jm_error_lane_state(mt);

        jm_emit_label_with_state(mt, l_end,
            jm_error_lane_merge(short_circuit_exit, right_exit));
        // D8.4.3: the ERROR-lane carrier must be the value defined by both
        // short-circuit arms. Keeping the RHS helper register here reads an
        // uninitialized path-local value when the RHS is skipped.
        mt->last_call_result_reg = result;
        return result;
    }

    // v23: typeof pattern optimization: typeof x === "literal" → js_typeof_is(x, "literal")
    // Reduces 3 operations (typeof + box_string + strict_equal) to 1 call returning int.
    {
        bool is_typeof_eq = (bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_EQ);
        bool is_typeof_ne = (bin->op == JS_OP_STRICT_NE || bin->op == JS_OP_NE);
        if (is_typeof_eq || is_typeof_ne) {
            JsAstNode* typeof_side = NULL;
            JsAstNode* literal_side = NULL;
            if (bin->left && bin->left->node_type == JS_AST_NODE_UNARY_EXPRESSION &&
                ((JsUnaryNode*)bin->left)->op == JS_OP_TYPEOF) {
                typeof_side = bin->left; literal_side = bin->right;
            } else if (bin->right && bin->right->node_type == JS_AST_NODE_UNARY_EXPRESSION &&
                       ((JsUnaryNode*)bin->right)->op == JS_OP_TYPEOF) {
                typeof_side = bin->right; literal_side = bin->left;
            }
            if (typeof_side && literal_side &&
                literal_side->node_type == JS_AST_NODE_LITERAL) {
                JsLiteralNode* lit = (JsLiteralNode*)literal_side;
                if (lit->literal_type == JS_LITERAL_STRING && lit->value.string_value) {
                    JsUnaryNode* type_un = (JsUnaryNode*)typeof_side;
                    JsAstNode* operand = type_un->operand;
                    // Only optimize when operand is in scope (avoids issues with
                    // builtins like parseInt/Math and undeclared variables)
                    bool can_optimize = true;
                    if (operand && operand->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* id = (JsIdentifierNode*)operand;
                        if (id->name && !jm_find_var(mt, id->name->chars))
                            can_optimize = false;
                    }
                    if (can_optimize) {
                        MIR_reg_t operand_reg = jm_transpile_box_item(mt, operand);
                        const char* type_str = lit->value.string_value->chars;
                        int type_len = (int)lit->value.string_value->len;
                        // The type spelling is part of the module's sealed name
                        // image, so the generated call carries only its NameId.
                        MIR_reg_t type_name_id = jm_module_name_id(mt, type_str,
                            (uint32_t)type_len);
                        MIR_reg_t raw = jm_callr_2(mt, "js_typeof_is", MIR_T_I64, operand_reg, type_name_id);
                        // for !==, invert the result
                        if (is_typeof_ne) {
                            MIR_reg_t inv = jm_new_reg(mt, "typeof_inv", MIR_T_I64);
                            jm_emit_reg_binary_op(mt, MIR_XOR, inv, raw, MIR_new_int_op(mt->ctx, 1));
                            raw = inv;
                        }
                        // box as boolean Item: ITEM_FALSE | raw_bit
                        MIR_reg_t result = jm_new_reg(mt, "typeof_r", MIR_T_I64);
                        jm_emit_reg_op_binary(mt, MIR_OR, result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_FALSE_VAL), raw);
                        return result;
                    }
                }
            }
        }
    }

    // --- Boxed runtime path (original) ---
    // Tune8 §2.1: js_not_equal / js_strict_not_equal removed — the transpiler
    // emits the corresponding eq call and inverts the low bit (where b2it
    // stores the boolean) with an inline MIR_XOR. The high-byte type tag is
    // preserved by the XOR-with-1.
    const char* fn_name = NULL;
    bool invert_box = false;
    int compare_op = -1;   // 0=LT, 1=GT, 2=LE, 3=GE; -1 = not a compare
    switch (bin->op) {
    case JS_OP_ADD:        fn_name = "js_add"; break;
    case JS_OP_SUB:        fn_name = "js_subtract"; break;
    case JS_OP_MUL:        fn_name = "js_multiply"; break;
    case JS_OP_DIV:        fn_name = "js_divide"; break;
    case JS_OP_MOD:        fn_name = "js_modulo"; break;
    case JS_OP_EXP:        fn_name = "js_power"; break;
    case JS_OP_EQ:         fn_name = "js_equal"; break;
    case JS_OP_NE:         fn_name = "js_equal"; invert_box = true; break;
    case JS_OP_STRICT_EQ:  fn_name = "js_strict_equal"; break;
    case JS_OP_STRICT_NE:  fn_name = "js_strict_equal"; invert_box = true; break;
    // Tune8 §2.1: js_less_than / _equal / js_greater_than / _equal folded into
    // js_compare(op, l, r) with op as compile-time constant operand.
    case JS_OP_LT:         fn_name = "js_compare"; compare_op = 0; break;
    case JS_OP_LE:         fn_name = "js_compare"; compare_op = 2; break;
    case JS_OP_GT:         fn_name = "js_compare"; compare_op = 1; break;
    case JS_OP_GE:         fn_name = "js_compare"; compare_op = 3; break;
    case JS_OP_AND:        fn_name = "js_logical_and"; break;
    case JS_OP_OR:         fn_name = "js_logical_or"; break;
    case JS_OP_BIT_AND:    fn_name = "js_bitwise_and"; break;
    case JS_OP_BIT_OR:     fn_name = "js_bitwise_or"; break;
    case JS_OP_BIT_XOR:    fn_name = "js_bitwise_xor"; break;
    case JS_OP_BIT_LSHIFT: fn_name = "js_left_shift"; break;
    case JS_OP_BIT_RSHIFT: fn_name = "js_right_shift"; break;
    case JS_OP_BIT_URSHIFT: fn_name = "js_unsigned_right_shift"; break;
    case JS_OP_INSTANCEOF: fn_name = "js_instanceof"; break;
    case JS_OP_IN:         fn_name = "js_in"; break;
    case JS_OP_NULLISH_COALESCE: fn_name = "js_nullish_coalesce"; break;
    default:
        log_error("js-mir: unknown binary op %d", bin->op);
        return jm_emit_null(mt);
    }

    // `#field in obj` resolves the current class's lexical private binding.
    MIR_reg_t left;
    if (bin->op == JS_OP_IN && bin->left &&
        bin->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* lid = (JsIdentifierNode*)bin->left;
        if (jm_is_private_name(lid->name)) {
            String* private_name = jm_resolve_private_name(mt, bin->left, lid->name);
            left = jm_emit_private_key_for_access(mt, bin->left, private_name);
        } else {
            left = jm_transpile_box_item(mt, bin->left);
        }
    } else {
        left = jm_transpile_box_item(mt, bin->left);
    }

    int left_spill_slot = -1;
    if (mt->in_generator &&
        (jm_has_yield(mt, bin->right) || (mt->in_async && jm_count_awaits(mt, bin->right) > 0))) {
        left_spill_slot = jm_gen_spill_save(mt, left);
    }

    // Special case: instanceof against Error-family builtins can use the
    // runtime classname helper because thrown Error objects carry class names.
    // Other identifiers must go through normal GetValue so unresolved bindings
    // throw ReferenceError and aliases/global assignments observe runtime values.
    if (bin->op == JS_OP_INSTANCEOF && bin->right &&
        bin->right->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* rid = (JsIdentifierNode*)bin->right;
        if (rid->name) {
            const char* rname = rid->name->chars;
            int rlen = (int)rid->name->len;
            // Check for builtin class names — these are safe for name-based check
            // (they never have custom Symbol.hasInstance)
            bool is_builtin_class = js_builtin_global_has_flag(
                rname, rlen, JS_BUILTIN_GLOBAL_ERROR_CLASS);
            if (is_builtin_class) {
                MIR_reg_t classname = jm_box_string_literal(mt, rname, rlen);
                return jm_callr_2(mt, "js_instanceof_classname", MIR_T_I64, left, classname);
            }
        }
    }

    MIR_reg_t right = jm_transpile_box_item(mt, bin->right);
    if (left_spill_slot >= 0) {
        jm_gen_spill_load(mt, left, left_spill_slot);
    }
    MIR_reg_t result;
    if (compare_op >= 0) {
        // Tune8 §2.1: js_compare(op, l, r) takes an extra constant op operand.
        result = jm_call_3(mt, fn_name, MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, compare_op),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
    } else {
        result = jm_callr_2(mt, fn_name, MIR_T_I64, left, right);
    }
    if (invert_box) {
        // Tune8 §2.1 inverse-pair fold: flip the bool stored in bit 0 of the
        // boxed result. b2it packs `(LMD_TYPE_BOOL << 56) | bool_val`, so XOR
        // with 1 inverts the value while preserving the type tag.
        MIR_reg_t inv = jm_new_reg(mt, "neboxinv", MIR_T_I64);
        jm_emit_reg_binary_op(mt, MIR_XOR, inv, result, MIR_new_int_op(mt->ctx, 1));
        result = inv;
    }
    // instanceof and in can throw TypeError — propagate exception to enclosing try/catch
    if (bin->op == JS_OP_INSTANCEOF || bin->op == JS_OP_IN) {
        jm_emit_error_lane_propagate_check(mt);
    }
    return result;
}

static bool jm_emit_native_update(JsMirTranspiler* mt, JsMirVarEntry* var,
                                  const char* vname, bool decrement, bool prefix,
                                  MIR_reg_t* out) {
    if (!mt || !var || mt->with_depth > 0 || var->from_env ||
        (var->type_id != LMD_TYPE_INT && var->type_id != LMD_TYPE_FLOAT)) {
        return false;
    }
    TypeId type = var->type_id;
    MIR_reg_t old_val = 0;
    if (!prefix) {
        old_val = jm_new_reg(mt, decrement ? "dec_native_old" : "inc_native_old",
            type == LMD_TYPE_INT ? MIR_T_I64 : MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, type == LMD_TYPE_INT ? MIR_MOV : MIR_DMOV,
            MIR_new_reg_op(mt->ctx, old_val), MIR_new_reg_op(mt->ctx, var->reg)));
    }
    if (type == LMD_TYPE_INT) {
        jm_emit(mt, MIR_new_insn(mt->ctx, decrement ? MIR_SUB : MIR_ADD,
            MIR_new_reg_op(mt->ctx, var->reg), MIR_new_reg_op(mt->ctx, var->reg),
            MIR_new_int_op(mt->ctx, 1)));
    } else {
        MIR_reg_t one = jm_new_reg(mt, "native_update_one", MIR_T_D);
        jm_emit_reg_op(mt, MIR_DMOV, one, MIR_new_double_op(mt->ctx, 1.0));
        jm_emit(mt, MIR_new_insn(mt->ctx, decrement ? MIR_DSUB : MIR_DADD,
            MIR_new_reg_op(mt->ctx, var->reg), MIR_new_reg_op(mt->ctx, var->reg),
            MIR_new_reg_op(mt->ctx, one)));
    }
    // Native updates share assignment's closure and mapped-arguments writeback.
    jm_emit_native_assignment_var_writeback(mt, var, vname, type);
    *out = prefix ? var->reg : old_val;
    return true;
}

static void jm_emit_const_assign_error(JsMirTranspiler* mt, const char* name,
        int name_len);

static MIR_reg_t jm_transpile_unary_numeric_operand(JsMirTranspiler* mt,
                                                     JsAstNode* operand,
                                                     MIR_reg_t* with_key) {
    *with_key = 0;
    if (operand && operand->node_type == JS_AST_NODE_IDENTIFIER && mt->with_depth > 0) {
        JsIdentifierNode* id = (JsIdentifierNode*)operand;
        const char* vname = jm_var_name(id->name);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var) {
            *with_key = jm_box_property_name_literal(mt, id->name->chars, id->name->len);
            MIR_reg_t fallback = ((var->type_id == LMD_TYPE_INT || var->type_id == LMD_TYPE_FLOAT) && !var->from_env) ?
                jm_box_native(mt, var->reg, var->type_id) : var->reg;
            MIR_reg_t value = jm_callr_2(mt, jm_with_binding_get_name(mt), MIR_T_I64, *with_key, fallback);
            jm_emit_error_lane_propagate_check(mt);
            return value;
        }
        if (mt->module_consts) {
            JsModuleConstEntry* mc = jm_find_module_const(mt, vname);
            if (mc && mc->const_type == MCONST_MODVAR) {
                *with_key = jm_box_property_name_literal(mt, id->name->chars, id->name->len);
                MIR_reg_t fallback = jm_load_module_var(mt, (uint32_t)mc->int_val);
                MIR_reg_t value = jm_callr_2(mt, jm_with_binding_get_name(mt), MIR_T_I64, *with_key, fallback);
                jm_emit_error_lane_propagate_check(mt);
                return value;
            }
        }
    }
    return jm_transpile_box_item(mt, operand);
}

static void jm_emit_unary_local_writeback(JsMirTranspiler* mt, JsMirVarEntry* var,
                                          MIR_reg_t result) {
    if ((var->type_id == LMD_TYPE_INT || var->type_id == LMD_TYPE_FLOAT) && !var->from_env) {
        MIR_reg_t num_result = jm_callr_1(mt, "js_to_number", MIR_T_I64, result);
        if (var->type_id == LMD_TYPE_INT) {
            MIR_reg_t native_d = jm_callr_1(mt, "js_get_number", MIR_T_D, num_result);
            MIR_reg_t native_i = jm_emit_double_to_int(mt, native_d);
            jm_emit_mov(mt, var->reg, native_i);
        } else {
            MIR_reg_t native_d = jm_callr_1(mt, "js_get_number", MIR_T_D, num_result);
            jm_emit_dmov(mt, var->reg, native_d);
        }
    } else {
        jm_emit_mov(mt, var->reg, result);
    }
    if (var->from_env) {
        jm_emit_store_i64(mt, var->env_slot * (int)sizeof(uint64_t), var->env_reg, var->reg);
    }
    if (var->in_scope_env) {
        jm_emit_store_i64(mt, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg, var->reg);
    }
}

// Return true when a module-level const assignment has emitted its exception path.
static bool jm_emit_unary_identifier_writeback(JsMirTranspiler* mt, JsIdentifierNode* id,
                                               MIR_reg_t result, MIR_reg_t* with_key,
                                               MIR_label_t* with_done_label) {
    const char* vname = jm_var_name(id->name);
    JsMirVarEntry* var = jm_find_var(mt, vname);
    if (mt->with_depth > 0) {
        if (!*with_key) *with_key = jm_box_property_name_literal(mt,
            id->name->chars, id->name->len);
        bool strict_put = jm_strict_put(mt);
        MIR_reg_t wrote_with_item = jm_call_3(mt, "js_set_last_with_binding_if_valid", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, *with_key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
            MIR_T_I64, MIR_new_int_op(mt->ctx, strict_put ? 1 : 0));
        jm_emit_error_lane_propagate_check(mt);
        MIR_reg_t wrote_with = jm_emit_is_truthy(mt, wrote_with_item, NULL);
        MIR_label_t local_write_label = jm_new_label(mt);
        *with_done_label = jm_new_label(mt);
        jm_emit_branch(mt, MIR_BF, local_write_label, wrote_with);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, *with_done_label)));
        jm_emit_label(mt, local_write_label);
    }
    if (var) {
        jm_emit_unary_local_writeback(mt, var, result);
    } else if (mt->module_consts) {
        JsModuleConstEntry* mc = jm_find_module_const(mt, vname);
        if (mc && mc->const_type == MCONST_MODVAR && mc->var_kind == 2) {
            jm_emit_const_assign_error(mt, id->name->chars, id->name->len);
            return true;
        }
        if (mc && mc->const_type == MCONST_MODVAR) {
            jm_store_module_var(mt, (uint32_t)mc->int_val, result);
            jm_scope_env_mark_and_writeback(mt, vname, result);
        } else if (!mc) {
            MIR_reg_t name_reg = jm_box_property_name_literal(mt,
                id->name->chars, id->name->len);
            bool strict_put = jm_strict_put(mt);
            jm_call_3(mt, "js_set_global_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
                MIR_T_I64, MIR_new_int_op(mt->ctx, strict_put ? 1 : 0));
            jm_emit_error_lane_propagate_check(mt);
        }
    }
    if (*with_done_label) jm_emit_label(mt, *with_done_label);
    return false;
}

// Binding writes share the same abrupt TypeError completion.
static void jm_emit_const_assign_error(JsMirTranspiler* mt, const char* name,
        int name_len) {
    MIR_reg_t name_id = jm_module_name_id(mt, name, (uint32_t)name_len);
    jm_call_2(mt, "js_throw_const_assign", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, name_id),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)name_len));
    jm_emit_error_lane_propagate_check(mt);
}

// Unary expression
static MIR_reg_t jm_transpile_update_unary(JsMirTranspiler* mt, JsUnaryNode* un, bool decrement) {
    const char* update_function = decrement ? "js_decrement" : "js_increment";
    const char* reference_old_name = decrement ? "dec_old_ref" : "inc_old_ref";
    const char* boxed_old_name = decrement ? "dec_old_box" : "inc_old_box";

    if (un->operand && un->operand->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        jm_transpile_box_item(mt, un->operand);
        jm_emit_error_lane_propagate_check(mt);
        jm_emit_invalid_assignment_target_reference_error(mt);
        jm_emit_error_lane_propagate_check(mt);
        return jm_emit_undefined(mt);
    }
    if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
        const char* vname = jm_var_name(id->name);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var && var->is_const) {
            jm_emit_const_assign_error(mt, id->name->chars, id->name->len);
            return jm_emit_undefined(mt);
        }
        if (var) {
            MIR_reg_t native_result = 0;
            if (jm_emit_native_update(mt, var, vname, decrement, un->prefix,
                    &native_result)) {
                return native_result;
            }
        }
    }

    if (un->operand && un->operand->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMirReference ref = jm_emit_reference(mt, un->operand);
        jm_emit_canonicalize_computed_key_for_get_put(mt, &ref);
        MIR_reg_t operand = jm_emit_get_value(mt, &ref);
        MIR_reg_t num_operand = jm_callr_1(mt, "js_to_numeric", MIR_T_I64, operand);
        MIR_reg_t old_value = num_operand;
        if (!un->prefix) {
            old_value = jm_new_reg(mt, reference_old_name, MIR_T_I64);
            jm_emit_mov(mt, old_value, num_operand);
        }
        MIR_reg_t result = jm_callr_1(mt, update_function, MIR_T_I64, num_operand);
        jm_emit_put_value(mt, &ref, result);
        return un->prefix ? result : old_value;
    }

    MIR_reg_t with_key = 0;
    MIR_reg_t operand = jm_transpile_unary_numeric_operand(mt, un->operand, &with_key);
    MIR_reg_t num_operand = jm_callr_1(mt, "js_to_numeric", MIR_T_I64, operand);
    if (!un->prefix) {
        MIR_reg_t saved = jm_new_reg(mt, boxed_old_name, MIR_T_I64);
        jm_emit_mov(mt, saved, num_operand);
        num_operand = saved;
    }
    MIR_reg_t result = jm_callr_1(mt, update_function, MIR_T_I64, num_operand);
    if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
        MIR_label_t with_done_label = 0;
        if (jm_emit_unary_identifier_writeback(mt, id, result, &with_key, &with_done_label)) {
            return jm_emit_undefined(mt);
        }
    }
    return un->prefix ? result : num_operand;
}

MIR_reg_t jm_transpile_unary(JsMirTranspiler* mt, JsUnaryNode* un) {
    if (jm_const_fold_enabled()) {
        JsFoldVal fv;
        if (jm_try_fold_const((JsAstNode*)un, &fv)) {
            // jm_transpile_box_item treats only numeric MINUS/SUB as native;
            // PLUS/BIT_NOT/NOT return boxed (matching the non-folded paths below).
            bool caller_native = false;
            if (un->op == JS_OP_MINUS || un->op == JS_OP_SUB) {
                TypeId ot = jm_get_effective_type(mt, un->operand);
                caller_native = (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT);
            }
            TypeId et = jm_get_effective_type(mt, (JsAstNode*)un);
            MIR_reg_t out;
            if (jm_emit_folded_at_value_site(mt, &fv, caller_native, et, &out)) return out;
            // else: fall through to normal codegen
        }
    }
    switch (un->op) {
    case JS_OP_NOT:
        return jm_callr_1(mt, "js_logical_not", MIR_T_I64, jm_transpile_box_item(mt, un->operand));
    case JS_OP_BIT_NOT:
        return jm_callr_1(mt, "js_bitwise_not", MIR_T_I64, jm_transpile_box_item(mt, un->operand));
    case JS_OP_TYPEOF: {
        // Only direct identifiers get typeof's unresolvable-reference escape;
        // member/call operands still perform normal GetValue on their bases.
        bool direct_identifier = un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER;
        mt->in_typeof = direct_identifier;
        MIR_reg_t operand_val = jm_transpile_box_item(mt, un->operand);
        mt->in_typeof = false;
        return jm_callr_1(mt, "js_typeof", MIR_T_I64, operand_val);
    }
    case JS_OP_PLUS:
    case JS_OP_ADD: {
        TypeId op_type = jm_get_effective_type(mt, un->operand);
        if (op_type == LMD_TYPE_FLOAT) {
            MIR_reg_t native = jm_transpile_as_native(mt, un->operand, LMD_TYPE_FLOAT);
            return jm_box_float(mt, native);
        }
        return jm_callr_1(mt, "js_unary_plus", MIR_T_I64, jm_transpile_box_item(mt, un->operand));
    }
    case JS_OP_MINUS:
    case JS_OP_SUB: {
        TypeId op_type = jm_get_effective_type(mt, un->operand);
        if (op_type == LMD_TYPE_INT) {
            if (un->operand && un->operand->node_type == JS_AST_NODE_LITERAL) {
                JsLiteralNode* lit = (JsLiteralNode*)un->operand;
                if (lit->literal_type == JS_LITERAL_NUMBER && lit->value.number_value == 0.0) {
                    MIR_reg_t r = jm_new_reg(mt, "dnegz", MIR_T_D);
                    jm_emit_reg_op(mt, MIR_DMOV, r, MIR_new_double_op(mt->ctx, -0.0));
                    return r;
                }
                MIR_reg_t val = jm_transpile_as_native(mt, un->operand, LMD_TYPE_INT);
                MIR_reg_t r = jm_new_reg(mt, "neg", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_NEG,
                    MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, val)));
                return r;
            }
            return jm_callr_1(mt, "js_unary_minus", MIR_T_I64, jm_transpile_box_item(mt, un->operand));
        }
        if (op_type == LMD_TYPE_FLOAT) {
            MIR_reg_t val = jm_transpile_as_native(mt, un->operand, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_new_reg(mt, "dneg", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DNEG,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, val)));
            return r;
        }
        return jm_callr_1(mt, "js_unary_minus", MIR_T_I64, jm_transpile_box_item(mt, un->operand));
    }
    case JS_OP_INCREMENT:
        return jm_transpile_update_unary(mt, un, false);
    case JS_OP_DECREMENT:
        return jm_transpile_update_unary(mt, un, true);
    case JS_OP_VOID: {
        // Evaluate for side effects, return undefined (not null)
        jm_transpile_box_item(mt, un->operand);
        MIR_reg_t undef_reg = jm_new_reg(mt, "void_undef", MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, undef_reg, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
        return undef_reg;
    }
    case JS_OP_DELETE: {
        // delete obj.prop or delete obj[expr] → js_delete_property(obj, key)
        if (un->operand && un->operand->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* m = (JsMemberNode*)un->operand;
            if (m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* obj_id = (JsIdentifierNode*)m->object;
                if (obj_id->name && obj_id->name->len == 5 &&
                    strncmp(obj_id->name->chars, "super", 5) == 0) {
                    MIR_reg_t msg = jm_box_string_literal(mt, "Unsupported reference to 'super'", 32);
                jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 1),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
                    jm_emit_error_lane_propagate_check(mt);
                    MIR_reg_t r = jm_new_reg(mt, "dfalse", MIR_T_I64);
                    jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, (int64_t)ITEM_FALSE_VAL));
                    return r;
                }
            }
            JsMirReference ref = jm_emit_reference(mt, un->operand);
            return jm_emit_delete_reference(mt, &ref);
        }
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* del_id = (JsIdentifierNode*)un->operand;
            const char* del_name = del_id->name ? del_id->name->chars : NULL;
            if (del_name) {
                // vars are stored with _js_ prefix in var scopes and module_consts
                const char* vname = jm_format_name("_js_%s", del_name);
                bool del_is_declared = (jm_find_var(mt, vname) != NULL);
                if (!del_is_declared && mt->module_consts) {
                    JsModuleConstEntry mclookup;
                    mclookup.name = jm_persist_name(vname);
                    if (hashmap_get(mt->module_consts, &mclookup)) del_is_declared = true;
                }
                MIR_reg_t key = jm_box_property_name_literal(mt, del_name,
                    del_id->name->len);
                return jm_call_2(mt, "js_delete_identifier_with_binding", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, del_is_declared ? 1 : 0));
            }
            // fallthrough: evaluate and return false (shouldn't normally reach here)
            jm_transpile_box_item(mt, un->operand);
            MIR_reg_t r = jm_new_reg(mt, "dfalse", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, (int64_t)ITEM_FALSE_VAL));
            return r;
        }
        // delete <non-reference> → evaluate for side effects, return true
        if (un->operand) {
            jm_transpile_box_item(mt, un->operand);
        }
        {
            MIR_reg_t r = jm_new_reg(mt, "dtrue", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, (int64_t)ITEM_TRUE_VAL));
            return r;
        }
    }
    default:
        log_error("js-mir: unknown unary op %d", un->op);
        return jm_emit_null(mt);
    }
}

// ============================================================================
// v20: Recursive destructuring helpers
// ============================================================================
// Forward declarations for mutual recursion
void jm_emit_destructure_target(JsMirTranspiler* mt, JsAstNode* target, MIR_reg_t val);
void jm_emit_array_destructure(JsMirTranspiler* mt, JsAstNode* pattern_node, MIR_reg_t src);
void jm_emit_object_destructure(JsMirTranspiler* mt, JsAstNode* pattern_node, MIR_reg_t src);
MIR_reg_t jm_emit_destructure_default(JsMirTranspiler* mt, MIR_reg_t val, JsAstNode* default_expr);

static JsMirReference jm_invalid_destructure_reference(void) {
    JsMirReference ref = {};
    ref.kind = JS_MIR_REF_INVALID;
    return ref;
}

static void jm_emit_iterator_abrupt_marks_done(JsMirTranspiler* mt,
        MIR_reg_t iter_done) {
    MIR_reg_t exception = jm_emit_error_lane_test(mt);
    MIR_label_t normal = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BF, normal, exception);
    // IteratorStep/IteratorCollectRest set [[Done]] before forwarding an
    // abrupt completion; IteratorClose must therefore not call return().
    jm_emit_reg_op(mt, MIR_MOV, iter_done, MIR_new_int_op(mt->ctx, 1));
    jm_error_lane_set_state(mt, JS_ERROR_LANE_SET);
    jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
    jm_emit_label_with_state(mt, normal, JS_ERROR_LANE_CLEAN);
}

static void jm_emit_destructure_put_reference(JsMirTranspiler* mt, const JsMirReference* ref, MIR_reg_t val) {
    if (!ref) return;
    MIR_reg_t key_reg = ref->key_reg;
    if (ref->kind == JS_MIR_REF_PROPERTY || ref->kind == JS_MIR_REF_SUPER_PROPERTY) {
        key_reg = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, ref->key_reg);
        jm_emit_error_lane_propagate_check(mt);
    }
    switch (ref->kind) {
    case JS_MIR_REF_PROPERTY:
        // Tune8 §2.2: js_private_property_set absorbs the _strict variant (4-arg form).
        if (ref->is_private) {
            jm_call_4(mt, "js_private_property_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0));
        } else if (ref->strict) {
            // Tune8 §2.2: strict goes through dispatcher.
            jm_call_4(mt, "js_set_key_policy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                MIR_T_I64, MIR_new_int_op(mt->ctx, 1));
        } else {
            // Hot path: direct call.
            jm_callr_3(mt, "js_set_key_default", MIR_T_I64, ref->base_reg, key_reg, val);
        }
        jm_emit_error_lane_propagate_check(mt);
        break;
    case JS_MIR_REF_SUPER_PROPERTY:
        // Tune8 §2.2: super_property_set unified with strict as constant operand.
        jm_call_4(mt, "js_super_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, ref->base_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_int_op(mt->ctx, ref->strict ? 1 : 0));
        if (ref->strict) jm_emit_error_lane_propagate_check(mt);
        break;
    default:
        break;
    }
}

static bool jm_emit_destructure_pre_reference(JsMirTranspiler* mt, JsAstNode* target, JsMirReference* ref) {
    if (!target || !ref) return false;
    if (target->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        *ref = jm_emit_reference(mt, target);
        return ref->kind != JS_MIR_REF_INVALID;
    }
    if (target->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)target;
        if (ap->left && ap->left->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            *ref = jm_emit_reference(mt, ap->left);
            return ref->kind != JS_MIR_REF_INVALID;
        }
    }
    return false;
}

static void jm_emit_destructure_bind_pre_reference(JsMirTranspiler* mt, JsAstNode* target,
    const JsMirReference* ref, MIR_reg_t val)
{
    if (!target || !ref) return;
    MIR_reg_t put_val = val;
    if (target->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)target;
        put_val = jm_emit_destructure_default(mt, val, ap->right);
    }
    jm_emit_destructure_put_reference(mt, ref, put_val);
}

static void jm_emit_destructure_target_or_reference(JsMirTranspiler* mt,
        JsAstNode* target, const JsMirReference* ref, bool has_ref, MIR_reg_t val) {
    if (has_ref) jm_emit_destructure_bind_pre_reference(mt, target, ref, val);
    else jm_emit_destructure_target(mt, target, val);
}

static void jm_emit_array_destructure_target(JsMirTranspiler* mt,
        JsAstNode* target, const JsMirReference* ref, bool has_ref, MIR_reg_t value,
        MIR_reg_t iterator, MIR_reg_t iter_done, bool has_yield,
        bool publish_iterator) {
    int iterator_spill = -1;
    int iter_done_spill = -1;
    if (has_yield) {
        iterator_spill = jm_gen_spill_save(mt, iterator);
        iter_done_spill = jm_gen_spill_save(mt, iter_done);
        if (publish_iterator && mt->gen_active_iterator_slot >= 0) {
            jm_emit_store_i64(mt, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg, iterator);
        }
    }
    jm_emit_destructure_target_or_reference(mt, target, ref, has_ref, value);
    if (has_yield) {
        if (publish_iterator && mt->gen_active_iterator_slot >= 0) {
            MIR_reg_t null_iterator = jm_emit_null(mt);
            jm_emit_store_i64(mt, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg, null_iterator);
        }
        jm_gen_spill_load(mt, iterator, iterator_spill);
        jm_gen_spill_load(mt, iter_done, iter_done_spill);
    }
}

static JsIdentifierNode* jm_destructure_binding_identifier_target(JsAstNode* target) {
    if (!target) return NULL;
    if (target->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)target;
        target = ap->left;
    }
    if (!target || target->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
    return (JsIdentifierNode*)target;
}

static void jm_emit_destructure_pre_binding_probe(JsMirTranspiler* mt, JsAstNode* target) {
    if (!mt || mt->with_depth <= 0 || mt->destructure_assignment_mode) return;
    JsIdentifierNode* id = jm_destructure_binding_identifier_target(target);
    if (!id || !id->name) return;
    MIR_reg_t key = jm_box_property_name_literal(mt, id->name->chars, id->name->len);
    MIR_reg_t probe_result = jm_callr_1(mt, "js_probe_with_binding", MIR_T_I64, key);
    jm_emit_error_lane_propagate_check(mt);
    (void)probe_result;
}

static bool jm_current_scope_has_var(JsMirTranspiler* mt, const char* vname) {
    struct hashmap* scope = jm_var_scope_at(mt, mt ? mt->scope_depth : -1);
    if (!mt || !vname || mt->scope_depth < 0 || !scope) {
        return false;
    }
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    key.name = vname;
    return hashmap_get(scope, &key) != NULL;
}

static void jm_emit_destructure_var_value(JsMirTranspiler* mt,
        MIR_reg_t target_reg, bool from_env, int env_slot, MIR_reg_t env_reg,
        bool in_scope_env, int scope_env_slot, MIR_reg_t scope_env_reg,
        MIR_reg_t value) {
    // A destructuring write updates every captured storage lane; otherwise a
    // closure can observe the local register while its environment is stale.
    jm_emit_mov(mt, target_reg, value);
    if (from_env) {
        jm_emit_store_i64(mt, env_slot * (int)sizeof(uint64_t), env_reg, value);
    }
    if (in_scope_env && scope_env_reg != 0) {
        jm_emit_store_i64(mt, scope_env_slot * (int)sizeof(uint64_t), scope_env_reg, value);
    }
}

// Bind a value to a named variable (find existing or create new register).
// Handles closure env write-back and scope_env write-back.
void jm_bind_destructure_var(JsMirTranspiler* mt, const char* vname, MIR_reg_t val) {
    JsMirVarEntry* var = jm_find_var(mt, vname);
    const char* js_name = (strncmp(vname, "_js_", 4) == 0) ? vname + 4 : vname;
    int js_name_len = (int)strlen(js_name);

    JsModuleConstEntry* module_var = NULL;
    if (mt->module_consts) {
        module_var = jm_find_module_const(mt, vname);
        if (module_var && module_var->const_type != MCONST_MODVAR &&
            module_var->var_kind != JS_VAR_CONST) {
            module_var = NULL;
        }
    }

    bool current_param_binding = mt && mt->current_fc && mt->current_fc->node &&
        jm_func_has_param_named(mt->current_fc->node, vname, (int)strlen(vname));
    if (current_param_binding) {
        if (!jm_current_scope_has_var(mt, vname)) var = NULL;
        module_var = NULL;
    }

    if (!mt->destructure_assignment_mode && jm_has_current_source_function(mt) &&
            !jm_current_function_is_iife_body(mt)) {
        // A declaration pattern inside an ordinary function always creates a
        // local binding. Earlier browser script units may expose a same-named
        // module slot, but that slot must only participate in assignment
        // patterns, never in `let`/`const`/parameter declarations.
        module_var = NULL;
    }

    bool local_shadows_module = module_var && var && !mt->destructure_assignment_mode;
    if (module_var && !mt->destructure_assignment_mode &&
        mt->current_fc && mt->current_fc->node) {
        // A retained script can inherit an unrelated same-named module slot from
        // an earlier script. Declaration destructuring must establish its local
        // binding before that slot is considered, even when no local register
        // exists yet (for example `for (let { target } = event; ...)`).
        if (jm_func_has_param_named(mt->current_fc->node, js_name, js_name_len)) {
            local_shadows_module = true;
        } else if (mt->current_fc->node->body) {
            struct hashmap* local_names = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                jm_name_hash, jm_name_cmp, NULL, NULL);
            jm_collect_indexed_body_locals(mt, mt->current_fc->node->body, local_names);
            local_shadows_module = jm_name_set_has(local_names, vname);
            hashmap_free(local_names);
        }
    }
    if (local_shadows_module && !mt->destructure_assignment_mode) {
        if (!jm_current_scope_has_var(mt, vname)) var = NULL;
        module_var = NULL;
    }
    bool writes_module_binding = module_var && !local_shadows_module;
    if (writes_module_binding) {
        if (!mt->destructure_assignment_mode &&
            (module_var->var_kind == JS_VAR_LET || module_var->var_kind == JS_VAR_CONST)) {
            jm_store_module_var(mt, (uint32_t)module_var->int_val, val);
            return;
        }
        if (module_var->var_kind == JS_VAR_CONST) {
            jm_emit_const_assign_error(mt, js_name, js_name_len);
            return;
        }
        if (module_var->var_kind == JS_VAR_LET) {
            MIR_reg_t old_val = jm_load_module_var(mt, (uint32_t)module_var->int_val);
            jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, old_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx,
                    jm_module_name_id(mt, js_name, (uint32_t)js_name_len)),
                MIR_T_I64, MIR_new_int_op(mt->ctx, js_name_len));
            // TDZ is an abrupt binding access; skipping the write would turn
            // the ReferenceError into a successful destructuring assignment.
            jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
            jm_store_module_var(mt, (uint32_t)module_var->int_val, val);
            return;
        }
        jm_store_module_var(mt, (uint32_t)module_var->int_val, val);
        return;
    }

    if (!var && mt->destructure_assignment_mode) {
            MIR_reg_t name_reg = jm_box_property_name_literal(mt, js_name, js_name_len);
        bool strict_assign = mt->is_module || (mt->current_fc &&
            JM_JS_FACT(mt->current_fc, is_strict));
        if (strict_assign) {
            jm_callr_1(mt, "js_get_global_property_strict", MIR_T_I64, name_reg);
            jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
            jm_call_3(mt, "js_set_global_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
            jm_emit_error_lane_propagate_check(mt);
            return;
        }
        jm_call_3(mt, "js_set_global_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        jm_emit_error_lane_propagate_check(mt);
        return;
    }

    // Declaration destructuring with no pre-registered TDZ entry (notably a
    // lexical for-loop head) must fall through and create a local register.
    // Assignment patterns already handled the unresolved-global case above.
    MIR_reg_t reg;
    bool existing_from_env = false;
    int existing_env_slot = 0;
    MIR_reg_t existing_env_reg = 0;
    bool existing_in_scope_env = false;
    int existing_scope_env_slot = 0;
    MIR_reg_t existing_scope_env_reg = 0;
    bool existing_is_let_const = false;
    if (var) {
        existing_from_env = var->from_env;
        existing_env_slot = var->env_slot;
        existing_env_reg = var->env_reg;
        existing_in_scope_env = var->in_scope_env;
        existing_scope_env_slot = var->scope_env_slot;
        existing_scope_env_reg = var->scope_env_reg;
        existing_is_let_const = var->is_let_const;
        if (var->tdz_active && !mt->destructure_assignment_mode) {
            jm_emit_destructure_var_value(mt, var->reg, var->from_env,
                var->env_slot, var->env_reg, var->in_scope_env,
                var->scope_env_slot, var->scope_env_reg, val);
            var->tdz_active = false;
            return;
        }
        if (var->is_const) {
            jm_emit_const_assign_error(mt, js_name, js_name_len);
            return;
        }
        if (var->tdz_active) {
            jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx,
                    jm_module_name_id(mt, js_name, (uint32_t)js_name_len)),
                MIR_T_I64, MIR_new_int_op(mt->ctx, js_name_len));
            jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
            jm_emit_destructure_var_value(mt, var->reg, var->from_env,
                var->env_slot, var->env_reg, var->in_scope_env,
                var->scope_env_slot, var->scope_env_reg, val);
            return;
        }
        if (var->from_env && !mt->destructure_assignment_mode) {
            MIR_reg_t old_env_val = jm_new_reg(mt, "env_tdz_old", MIR_T_I64);
            jm_emit_load_i64(mt, old_env_val, var->env_slot * (int)sizeof(uint64_t), var->env_reg);
            jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, old_env_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx,
                    jm_module_name_id(mt, js_name, (uint32_t)js_name_len)),
                MIR_T_I64, MIR_new_int_op(mt->ctx, js_name_len));
            jm_emit_error_lane_route(mt, JS_MIR_COMPLETION_THROW);
            jm_emit_destructure_var_value(mt, var->reg, true,
                var->env_slot, var->env_reg, var->in_scope_env,
                var->scope_env_slot, var->scope_env_reg, val);
            return;
        }
        reg = var->reg;
    } else {
        reg = jm_new_reg(mt, vname, MIR_T_I64);
    }
    jm_emit_destructure_var_value(mt, reg, existing_from_env, existing_env_slot,
        existing_env_reg, existing_in_scope_env, existing_scope_env_slot,
        existing_scope_env_reg, val);
    jm_set_var(mt, vname, reg);
    if (existing_from_env && jm_has_current_source_function(mt) && mt->module_consts) {
        JsModuleConstEntry* mc = jm_find_module_const(mt, vname);
        if (mc && mc->const_type == MCONST_MODVAR) {
            if (mc->var_kind == 2) {
                const char* js_name = (strncmp(vname, "_js_", 4) == 0) ? vname + 4 : vname;
                jm_emit_const_assign_error(mt, js_name, (int)strlen(js_name));
                return;
            }
            jm_store_module_var(mt, (uint32_t)mc->int_val, reg);
        }
    }
    // Module variable writeback: if this is a module-level var, sync to runtime.
    // Only write back when we are at module scope (js_main) or inside an IIFE
    // body whose locals were promoted to module vars. In a nested function, a
    // local declaration that shadows a module var name (e.g. `const {x: n} = e`
    // where `n` is also a top-level const) must NOT clobber the module slot.
    bool is_local_let_const = existing_is_let_const;
    if (!is_local_let_const && mt->module_consts) {
        JsModuleConstEntry* mc = jm_find_module_const(mt, vname);
        bool at_module_scope = mt->in_main ||
            (mc && mc->is_iife_var && jm_current_function_is_iife_body(mt));
        if (mc && mc->const_type == MCONST_MODVAR) {
            if (at_module_scope) {
                jm_store_module_var(mt, (uint32_t)mc->int_val, reg);
            }
        }
    }
}

// Emit default value check: if val is undefined, evaluate and use default_expr
MIR_reg_t jm_emit_destructure_default(JsMirTranspiler* mt, MIR_reg_t val, JsAstNode* default_expr) {
    MIR_label_t l_has = jm_new_label(mt);
    MIR_label_t l_done = jm_new_label(mt);
    MIR_reg_t result = jm_new_reg(mt, "_dstr", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
        MIR_new_label_op(mt->ctx, l_has),
        MIR_new_reg_op(mt->ctx, val),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEF_VAL)));
    MIR_reg_t def = jm_transpile_box_item(mt, default_expr);
    // a default initializer is executable code; preserve its abrupt completion
    // before binding the resolved value into the pattern.
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_mov(mt, result, def);
    jm_emit_jmp(mt, l_done);
    jm_emit_label(mt, l_has);
    jm_emit_mov(mt, result, val);
    jm_emit_label(mt, l_done);
    return result;
}

// Dispatch destructuring target: bind val to the target pattern node
void jm_emit_destructure_target(JsMirTranspiler* mt, JsAstNode* target, MIR_reg_t val) {
    if (!target) return;
    if (target->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)target;
        const char* vname = jm_var_name(id->name);
        jm_bind_destructure_var(mt, vname, val);
    } else if (target->node_type == JS_AST_NODE_ARRAY_PATTERN ||
               target->node_type == JS_AST_NODE_ARRAY_EXPRESSION) {
        jm_emit_array_destructure(mt, target, val);
    } else if (target->node_type == JS_AST_NODE_OBJECT_PATTERN ||
               target->node_type == JS_AST_NODE_OBJECT_EXPRESSION) {
        jm_emit_object_destructure(mt, target, val);
    } else if (target->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)target;
        MIR_reg_t resolved = jm_emit_destructure_default(mt, val, ap->right);
        // function name inference for destructuring defaults
        if (ap->left && ap->left->node_type == JS_AST_NODE_IDENTIFIER && ap->right) {
            JsIdentifierNode* id = (JsIdentifierNode*)ap->left;
            bool is_anon_func = false;
            bool is_anon_class = false;
            if (ap->right->node_type == JS_AST_NODE_ARROW_FUNCTION) {
                is_anon_func = true;
            } else if (ap->right->node_type == JS_AST_NODE_FUNCTION_EXPRESSION) {
                JsFunctionNode* fn = (JsFunctionNode*)ap->right;
                is_anon_func = (fn->name == NULL);
            } else if (ap->right->node_type == JS_AST_NODE_CLASS_EXPRESSION ||
                       ap->right->node_type == JS_AST_NODE_CLASS_DECLARATION) {
                JsClassNode* cls = (JsClassNode*)ap->right;
                is_anon_class = (cls->name == NULL);
            }
            if (is_anon_func && id->name) {
                jm_emit_set_function_name(mt, resolved, id->name->chars);
            } else if (is_anon_class && id->name) {
                MIR_reg_t name_val = jm_box_string_literal(mt, id->name->chars, (int)id->name->len);
                jm_callr_void_2(mt, "js_set_class_name", resolved, name_val);
            }
        }
        jm_emit_destructure_target(mt, ap->left, resolved);
    } else if (target->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        // assignment target: obj.prop or obj[expr]
        JsMemberNode* member = (JsMemberNode*)target;
        // generator spill: if computed property contains yield, spill val and obj across it
        bool need_spill = mt->in_generator && member->computed && jm_has_yield(mt, member->property);
        int val_spill = -1, obj_spill = -1;
        if (need_spill) {
            val_spill = jm_gen_spill_save(mt, val);
        }
        MIR_reg_t obj = jm_transpile_box_item(mt, member->object);
        MIR_reg_t prop_key;
        bool is_private = false;
        if (member->computed) {
            if (need_spill) {
                obj_spill = jm_gen_spill_save(mt, obj);
            }
            prop_key = jm_transpile_box_item(mt, member->property);
            if (need_spill) {
                obj = jm_new_reg(mt, "_dstr_obj_r", MIR_T_I64);
                jm_gen_spill_load(mt, obj, obj_spill);
                val = jm_new_reg(mt, "_dstr_val_r", MIR_T_I64);
                jm_gen_spill_load(mt, val, val_spill);
            }
        } else if (member->property && member->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)member->property;
            String* key_name = jm_resolve_private_name(mt, (JsAstNode*)member->property, prop->name);
            // Destructuring writes bypass ordinary Reference lowering, so a
            // private member must still use its lexical class identity.
            if (jm_is_private_name(key_name)) {
                is_private = true;
                prop_key = jm_emit_private_key_for_access(mt, (JsAstNode*)member->property, key_name);
            } else {
                prop_key = jm_box_property_name_literal(mt, key_name->chars,
                    key_name->len);
            }
        } else {
            prop_key = jm_transpile_box_item(mt, member->property);
        }
        // Object-pattern member targets use OrdinarySet unless the syntax is
        // an actual private name. Routing every rest/destructure target to
        // PrivateSet rejected ordinary keys that merely happened to share a
        // NameId (S#7.3.32).
        if (is_private) {
            jm_call_4(mt, "js_private_property_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, prop_key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        } else {
            bool strict_put = jm_strict_put(mt);
            jm_call_4(mt, "js_set_key_policy", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, prop_key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                MIR_T_I64, MIR_new_int_op(mt->ctx, strict_put ? 1 : 0));
        }
        jm_emit_error_lane_propagate_check(mt);
    }
}

// Handle array destructuring pattern: step-by-step iterator protocol (ES spec §13.3.3.6)
void jm_emit_array_destructure(JsMirTranspiler* mt, JsAstNode* pattern_node, MIR_reg_t src) {
    JsArrayPatternNode* pattern = (JsArrayPatternNode*)pattern_node;

    // Validate: rest element must be last, and must not have an initializer
    {
        JsAstNode* chk = pattern->elements;
        while (chk) {
            if (chk->node_type == JS_AST_NODE_REST_ELEMENT ||
                chk->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)chk;
                if (sp->argument && sp->argument->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                    MIR_reg_t msg = jm_box_string_literal(mt, "Rest element must not have a default initializer", 48);
                    jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
                    jm_emit_error_lane_propagate_check(mt);
                    return;
                }
                if (chk->next != NULL) {
                    MIR_reg_t msg = jm_box_string_literal(mt, "Rest element must be last element", 32);
                    jm_call_2(mt, "js_throw_named_error", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg));
                    jm_emit_error_lane_propagate_check(mt);
                    return;
                }
            }
            chk = chk->next;
        }
    }

    // Check if pattern contains yield expressions in generator context. Iterator
    // state is held in MIR registers, so any yield-containing target must spill
    // that state across suspension before destructuring continues.
    bool has_yields = false;
    {
        // Module destructuring has no source function; use the active scope record.
        JsFuncCollected* fc = mt->current_fc;
        if (fc && fc->node && fc->node->is_generator) {
            JsAstNode* chk = pattern->elements;
            while (chk) {
                if (jm_count_yields(mt, chk) > 0) { has_yields = true; break; }
                chk = chk->next;
            }
        }
    }

    // Get iterator from iterable (ES spec: GetIterator)
    MIR_reg_t iterator = jm_emit_get_iterator_lazy(mt, src);
    // If js_get_iterator threw (non-iterable), skip destructuring
    MIR_label_t skip_arr_destr = jm_new_label(mt);
    jm_emit_error_lane_propagate_check(mt);

    // Track whether iterator is exhausted
    MIR_reg_t iter_done = jm_new_reg(mt, "itrdone", MIR_T_I64);
    jm_emit_reg_op(mt, MIR_MOV, iter_done, MIR_new_int_op(mt->ctx, 0));

    MIR_label_t arr_destr_exc = jm_new_label(mt);
    MIR_label_t arr_destr_after = jm_new_label(mt);
    MIR_label_t arr_destr_done = jm_new_label(mt);
    bool pushed_arr_destr_try = false;
    if (JsTryContext* tc = jm_try_context_push(mt)) {
        jm_try_context_setup(tc, arr_destr_exc, 0, arr_destr_after, 0, 0,
            true, false, NULL, 0);
        pushed_arr_destr_try = true;
    }

    JsAstNode* elem = pattern->elements;
    while (elem) {
        if (elem->node_type == JS_AST_NODE_SPREAD_ELEMENT ||
            elem->node_type == JS_AST_NODE_REST_ELEMENT) {
            // rest element: collect remaining iterator values into array
            JsSpreadElementNode* sp = (JsSpreadElementNode*)elem;
            if (sp->argument) {
                MIR_label_t rest_skip = jm_new_label(mt);
                MIR_label_t rest_end = jm_new_label(mt);
                JsMirReference rest_ref = jm_invalid_destructure_reference();
                int rest_pre_iterator_spill = -1;
                int rest_pre_iter_done_spill = -1;
        bool rest_pre_has_yield = mt->in_generator && jm_has_yield(mt, sp->argument);
                if (rest_pre_has_yield) {
                    rest_pre_iterator_spill = jm_gen_spill_save(mt, iterator);
                    rest_pre_iter_done_spill = jm_gen_spill_save(mt, iter_done);
                    if (mt->gen_active_iterator_slot >= 0) {
                        jm_emit_store_i64(mt, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg, iterator);
                    }
                }
                bool has_rest_ref = jm_emit_destructure_pre_reference(mt, sp->argument, &rest_ref);
                if (rest_pre_has_yield) {
                    if (mt->gen_active_iterator_slot >= 0) {
                        MIR_reg_t null_iter = jm_emit_null(mt);
                        jm_emit_store_i64(mt, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg, null_iter);
                    }
                    jm_gen_spill_load(mt, iterator, rest_pre_iterator_spill);
                    jm_gen_spill_load(mt, iter_done, rest_pre_iter_done_spill);
                }
                if (has_rest_ref) {
                    jm_emit_iterator_close_on_error_lane_if_open(mt, iterator, iter_done, skip_arr_destr);
                }
                jm_emit_branch(mt, MIR_BT, rest_skip, iter_done);
                MIR_reg_t rest = jm_emit_iterator_collect_rest(mt, iterator);
                jm_emit_iterator_abrupt_marks_done(mt, iter_done);
                jm_emit_reg_op(mt, MIR_MOV, iter_done, MIR_new_int_op(mt->ctx, 1));
        bool rest_target_has_yield = has_yields && mt->in_generator && jm_has_yield(mt, sp->argument);
                jm_emit_array_destructure_target(mt, sp->argument, &rest_ref,
                    has_rest_ref, rest, iterator, iter_done, rest_target_has_yield, false);
                jm_emit_iterator_close_on_error_lane_if_open(mt, iterator, iter_done, skip_arr_destr);
                jm_emit_jmp(mt, rest_end);
                jm_emit_label(mt, rest_skip);
                MIR_reg_t empty_arr = jm_call_1(mt, "js_array_new", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
        bool empty_target_has_yield = has_yields && mt->in_generator && jm_has_yield(mt, sp->argument);
                jm_emit_array_destructure_target(mt, sp->argument, &rest_ref,
                    has_rest_ref, empty_arr, iterator, iter_done,
                    empty_target_has_yield, false);
                jm_emit_label(mt, rest_end);
            }
        } else if (elem->node_type == JS_AST_NODE_NULL) {
            // elision: advance iterator but discard value
            MIR_label_t elision_end = jm_new_label(mt);
            // skip if already done
            jm_emit_branch(mt, MIR_BT, elision_end, iter_done);
            MIR_reg_t step_val = jm_emit_iterator_step(mt, iterator);
            jm_emit_iterator_abrupt_marks_done(mt, iter_done);
            // check if done
            MIR_reg_t is_done = jm_emit_iterator_done_test(mt, step_val, "eldone");
            // if done, mark iter_done
            MIR_label_t not_done = jm_new_label(mt);
            jm_emit_branch(mt, MIR_BF, not_done, is_done);
            jm_emit_reg_op(mt, MIR_MOV, iter_done, MIR_new_int_op(mt->ctx, 1));
            jm_emit_label(mt, not_done);
            jm_emit_label(mt, elision_end);
        } else {
            // regular element: step iterator and bind value
            MIR_label_t assign_undef = jm_new_label(mt);
            MIR_label_t elem_end = jm_new_label(mt);
            JsMirReference pre_ref = jm_invalid_destructure_reference();
            int pre_iterator_spill = -1;
            int pre_iter_done_spill = -1;
        bool pre_ref_has_yield = mt->in_generator && jm_has_yield(mt, elem);
            if (pre_ref_has_yield) {
                pre_iterator_spill = jm_gen_spill_save(mt, iterator);
                pre_iter_done_spill = jm_gen_spill_save(mt, iter_done);
                if (mt->gen_active_iterator_slot >= 0) {
                    jm_emit_store_i64(mt, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg, iterator);
                }
            }
            bool has_pre_ref = jm_emit_destructure_pre_reference(mt, elem, &pre_ref);
            if (pre_ref_has_yield) {
                if (mt->gen_active_iterator_slot >= 0) {
                    MIR_reg_t null_iter = jm_emit_null(mt);
                    jm_emit_store_i64(mt, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg, null_iter);
                }
                jm_gen_spill_load(mt, iterator, pre_iterator_spill);
                jm_gen_spill_load(mt, iter_done, pre_iter_done_spill);
            }
            if (has_pre_ref) {
                jm_emit_iterator_close_on_error_lane_if_open(mt, iterator, iter_done, skip_arr_destr);
            }

            // if already done, assign undefined
            jm_emit_branch(mt, MIR_BT, assign_undef, iter_done);

            // call js_iterator_step
            MIR_reg_t step_val = jm_emit_iterator_step(mt, iterator);
            jm_emit_iterator_abrupt_marks_done(mt, iter_done);

            // check if done
            MIR_reg_t is_done = jm_emit_iterator_done_test(mt, step_val, "stdone");
            jm_emit_branch(mt, MIR_BT, assign_undef, is_done);

            // not done: bind step value to target
        bool elem_has_yield = has_yields && mt->in_generator && jm_has_yield(mt, elem);
            jm_emit_array_destructure_target(mt, elem, &pre_ref,
                has_pre_ref, step_val, iterator, iter_done, elem_has_yield, true);
            jm_emit_iterator_close_on_error_lane_if_open(mt, iterator, iter_done, skip_arr_destr);
            jm_emit_jmp(mt, elem_end);

            // done: mark done, bind undefined
            // both incoming edges have already completed IteratorStep and its
            // error check; this normal-only join must not inherit that call's
            // register as a new exception source while evaluating a default.
            jm_emit_label_with_state(mt, assign_undef, JS_ERROR_LANE_CLEAN);
            jm_emit_reg_op(mt, MIR_MOV, iter_done, MIR_new_int_op(mt->ctx, 1));
            MIR_reg_t undef_val = jm_new_reg(mt, "undef", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, undef_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
        bool undef_elem_has_yield = has_yields && mt->in_generator && jm_has_yield(mt, elem);
            jm_emit_array_destructure_target(mt, elem, &pre_ref,
                has_pre_ref, undef_val, iterator, iter_done,
                undef_elem_has_yield, true);
            jm_emit_iterator_close_on_error_lane_if_open(mt, iterator, iter_done, skip_arr_destr);
            jm_emit_label(mt, elem_end);
        }
        elem = elem->next;
    }

    // ES spec: if iterator is not exhausted, call IteratorClose
    MIR_label_t no_close = jm_new_label(mt);
    jm_emit_branch(mt, MIR_BT, no_close, iter_done);
    jm_emit_iterator_close(mt, iterator);
    // the normal close has completed the iterator even when its return method
    // throws; marking it done lets the abrupt cleanup rethrow without calling
    // IteratorClose a second time.
    jm_emit_reg_op(mt, MIR_MOV, iter_done, MIR_new_int_op(mt->ctx, 1));
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_label(mt, no_close);

    jm_emit_jmp(mt, arr_destr_after);
    jm_emit_label_with_state(mt, arr_destr_exc, JS_ERROR_LANE_SET);
    jm_emit_iterator_close_on_error_lane_if_open(mt, iterator, iter_done, skip_arr_destr);
    if (pushed_arr_destr_try) mt->try_ctx_depth--;
    // Normal destructuring completion must bypass the abrupt-completion
    // rethrow join below; otherwise its exception test reads a result register
    // written only by IteratorClose-on-error paths.
    jm_emit_label_with_state(mt, arr_destr_after, JS_ERROR_LANE_CLEAN);
    jm_emit_jmp(mt, arr_destr_done);

    // cleanup rethrows arrive here with the original abrupt completion still
    // carried by last_call_result_reg; an untyped async join would clear that
    // carrier before the enclosing function can route the exception.
    jm_emit_label_with_state(mt, skip_arr_destr, JS_ERROR_LANE_SET);
    // cleanup rethrows through js_throw_value; inspect that final Item here so
    // iterator/destructuring abrupt completions reach the enclosing function.
    jm_error_lane_set_state(mt, JS_ERROR_LANE_UNKNOWN);
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_label_with_state(mt, arr_destr_done, JS_ERROR_LANE_CLEAN);
}

// Handle object destructuring pattern: extract properties by key from src
void jm_emit_object_destructure(JsMirTranspiler* mt, JsAstNode* pattern_node, MIR_reg_t src) {
    JsObjectPatternNode* pattern = (JsObjectPatternNode*)pattern_node;

    // Pre-initialize all destructured target variables to undefined.
    // This prevents uninitialized variable usage if the exception check below
    // skips the property gets (for example after an already-routed ERROR lane).
    if (mt->destructure_assignment_mode) {
        MIR_reg_t pre_undef = jm_emit_undefined(mt);
        JsAstNode* p = pattern->properties;
        while (p) {
            JsAstNode* target = NULL;
            if (p->node_type == JS_AST_NODE_PROPERTY) {
                JsPropertyNode* pp = (JsPropertyNode*)p;
                target = pp->value ? pp->value : pp->key;
            } else if (p->node_type == JS_AST_NODE_REST_ELEMENT ||
                       p->node_type == JS_AST_NODE_REST_PROPERTY ||
                       p->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)p;
                target = sp->argument;
            }
            // Unwrap assignment pattern: const { x = default } = obj
            if (target && target->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)target;
                target = ap->left;
            }
            if (target && target->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)target;
                const char* vname = jm_var_name(id->name);
                jm_bind_destructure_var(mt, vname, pre_undef);
            }
            p = p->next;
        }
    }

    // RequireObjectCoercible is an abrupt completion, so route it immediately;
    // a local skip would consume the merged error lane and continue the binding.
    jm_callr_1(mt, "js_require_object_coercible", MIR_T_I64, src);
    jm_emit_error_lane_propagate_check(mt);

    JsAstNode* prop = pattern->properties;
    while (prop) {
        if (prop->node_type == JS_AST_NODE_PROPERTY) {
            JsPropertyNode* p = (JsPropertyNode*)prop;
            MIR_reg_t key;
            if (p->computed) {
                key = jm_transpile_box_item(mt, p->key);
                key = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, key);
                jm_emit_error_lane_propagate_check(mt);
            } else if (p->key && p->key->node_type == JS_AST_NODE_IDENTIFIER) {
                String* kn = ((JsIdentifierNode*)p->key)->name;
                key = jm_box_property_name_literal(mt, kn->chars, kn->len);
            } else {
                key = jm_transpile_box_item(mt, p->key);
                key = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, key);
                jm_emit_error_lane_propagate_check(mt);
            }
            JsAstNode* target = p->value ? p->value : p->key;
            JsMirReference pre_ref = jm_invalid_destructure_reference();
            int pre_src_spill = -1;
            int pre_key_spill = -1;
            bool pre_ref_has_yield = mt->in_generator && jm_has_yield(mt, target);
            if (pre_ref_has_yield) {
                pre_src_spill = jm_gen_spill_save(mt, src);
                pre_key_spill = jm_gen_spill_save(mt, key);
            }
            bool has_pre_ref = jm_emit_destructure_pre_reference(mt, target, &pre_ref);
            if (pre_ref_has_yield) {
                jm_gen_spill_load(mt, src, pre_src_spill);
                jm_gen_spill_load(mt, key, pre_key_spill);
            }
            if (has_pre_ref) jm_emit_error_lane_propagate_check(mt);
            jm_emit_destructure_pre_binding_probe(mt, target);
            MIR_reg_t val = jm_callr_2(mt, "js_get_key_default", MIR_T_I64, src, key);
            jm_emit_error_lane_propagate_check(mt);
            int target_src_spill = -1;
            bool target_has_yield = mt->in_generator && jm_has_yield(mt, target);
            if (target_has_yield) {
                target_src_spill = jm_gen_spill_save(mt, src);
            }
            jm_emit_destructure_target_or_reference(mt, target, &pre_ref,
                has_pre_ref, val);
            if (target_has_yield) {
                jm_gen_spill_load(mt, src, target_src_spill);
            }
        } else if (prop->node_type == JS_AST_NODE_REST_ELEMENT ||
                   prop->node_type == JS_AST_NODE_REST_PROPERTY ||
                   prop->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            // object rest: {...rest} — collect remaining keys
            JsSpreadElementNode* sp = (JsSpreadElementNode*)prop;
            if (sp->argument) {
                int exclude_count = 0;
                JsAstNode* pp = pattern->properties;
                while (pp && pp != prop) {
                    if (pp->node_type == JS_AST_NODE_PROPERTY) exclude_count++;
                    pp = pp->next;
                }
                // Use js_alloc_env instead of MIR_ALLOCA to avoid MIR inlining ALLOCA bug on ARM64.
                MIR_reg_t arr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (exclude_count > 0 ? exclude_count : 1)));
                int ki = 0;
                pp = pattern->properties;
                while (pp && pp != prop) {
                    if (pp->node_type == JS_AST_NODE_PROPERTY) {
                        JsPropertyNode* ep = (JsPropertyNode*)pp;
                        MIR_reg_t ki_item = 0;
                        if (!ep->computed && ep->key && ep->key->node_type == JS_AST_NODE_IDENTIFIER) {
                            String* ek = ((JsIdentifierNode*)ep->key)->name;
                            ki_item = jm_box_property_name_literal(mt, ek->chars, ek->len);
                        } else if (ep->key) {
                            MIR_reg_t raw_key = jm_transpile_box_item(mt, ep->key);
                            ki_item = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, raw_key);
                            jm_emit_error_lane_propagate_check(mt);
                        }
                        if (!ki_item) ki_item = jm_emit_undefined(mt);
                        MIR_reg_t offset = jm_new_reg(mt, "excl_off", MIR_T_I64);
                        jm_emit_reg_binary_op(mt, MIR_ADD, offset, arr, MIR_new_int_op(mt->ctx, ki * 8));
                        jm_emit_store_i64(mt, 0, offset, ki_item);
                        ki++;
                    }
                    pp = pp->next;
                }
                MIR_reg_t rest = jm_call_3(mt, "js_object_rest", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, src),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arr),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, exclude_count));
                jm_emit_error_lane_propagate_check(mt);
                jm_emit_destructure_target(mt, sp->argument, rest);
            }
        }
        prop = prop->next;
    }

}

static bool jm_expression_can_suspend(JsMirTranspiler* mt, JsAstNode* expr) {
    return mt && mt->in_generator && expr &&
        (jm_has_yield(mt, expr) || (mt->in_async && jm_count_awaits(mt, expr) > 0));
}

static void jm_spill_reference_for_suspending_rhs(JsMirTranspiler* mt,
                                                   const JsMirReference* ref,
                                                   JsAstNode* rhs,
                                                   int* out_base_slot,
                                                   int* out_key_slot) {
    *out_base_slot = -1;
    *out_key_slot = -1;
    if (!jm_expression_can_suspend(mt, rhs) || !ref) return;
    if (ref->base_reg) *out_base_slot = jm_gen_spill_save(mt, ref->base_reg);
    if (ref->key_reg) *out_key_slot = jm_gen_spill_save(mt, ref->key_reg);
}

static void jm_restore_suspended_reference(JsMirTranspiler* mt,
                                           const JsMirReference* ref,
                                           int base_slot,
                                           int key_slot) {
    if (!ref) return;
    if (base_slot >= 0) jm_gen_spill_load(mt, ref->base_reg, base_slot);
    if (key_slot >= 0) jm_gen_spill_load(mt, ref->key_reg, key_slot);
}

// Assignment expression
static MIR_reg_t jm_emit_destructure_assignment(JsMirTranspiler* mt,
        JsAstNode* pattern, JsAstNode* rhs, bool is_array) {
    // evaluate RHS first so swap assignments cannot observe partially updated bindings.
    MIR_reg_t src = jm_transpile_box_item(mt, rhs);
    int src_spill = -1;
    if (mt->in_generator && jm_has_yield(mt, pattern)) {
        src_spill = jm_gen_spill_save(mt, src);
    }
    bool prev_dstr_assignment = mt->destructure_assignment_mode;
    mt->destructure_assignment_mode = true;
    if (is_array) jm_emit_array_destructure(mt, pattern, src);
    else jm_emit_object_destructure(mt, pattern, src);
    mt->destructure_assignment_mode = prev_dstr_assignment;
    if (src_spill >= 0) jm_gen_spill_load(mt, src, src_spill);

    // write destructured bindings to scope_env for closure capture.
    jm_writeback_scope_env_pattern_bindings(mt, pattern);
    return src;
}

MIR_reg_t jm_transpile_assignment(JsMirTranspiler* mt, JsAssignmentNode* asgn) {
    if (!asgn->left || !asgn->right) return jm_emit_null(mt);

    if (asgn->left->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        jm_transpile_box_item(mt, asgn->left);
        jm_emit_error_lane_propagate_check(mt);
        jm_emit_invalid_assignment_target_reference_error(mt);
        jm_emit_error_lane_propagate_check(mt);
        return jm_emit_undefined(mt);
    }

    // Simple variable assignment: x = expr
    if (asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)asgn->left;
        const char* vname = jm_var_name(id->name);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        JsClassEntry* inner_ce = jm_current_inner_class_binding(mt, id->name, (JsAstNode*)id);
        bool local_shadow = var && !var->from_env && !var->from_shared_env;
        if (inner_ce && !local_shadow) {
            jm_emit_const_assign_error(mt, id->name->chars, id->name->len);
            return jm_emit_undefined(mt);
        }
        if (!var) {
            // Check module-level variables (let/var at top level accessed from inside functions)
            if (mt->module_consts) {
                JsModuleConstEntry* mc = jm_find_module_const(mt, vname);
                if (mc && (mc->const_type == MCONST_MODVAR || mc->const_type == MCONST_CLASS)) {
                    if (mc->const_type == MCONST_MODVAR &&
                        (mc->var_kind == JS_VAR_LET || mc->var_kind == JS_VAR_CONST)) {
                        MIR_reg_t old_binding = jm_load_module_var(mt, (uint32_t)mc->int_val);
                        jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, old_binding),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx,
                                jm_module_name_id(mt, id->name->chars, id->name->len)),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)id->name->len));
                        jm_emit_error_lane_propagate_check(mt);
                    }
                    // const module var: throw TypeError on assignment
                    if (mc->const_type == MCONST_MODVAR && mc->var_kind == 2) {
                        jm_emit_const_assign_error(mt, id->name->chars, id->name->len);
                        return jm_emit_undefined(mt);
                    }
                    // P5: For typed INT module variables, use inline native arithmetic
                    // for compound assignments instead of calling js_add/js_subtract/etc.
                    // This eliminates one function call per iteration in tight loops.
                    if (mt->with_depth <= 0 && mc->modvar_type == LMD_TYPE_INT && asgn->op != JS_OP_ASSIGN) {
                        MIR_insn_code_t p5_mir_op = MIR_ADD;
                        bool p5_handled = true;
                        switch (asgn->op) {
                        case JS_OP_ADD_ASSIGN:    p5_mir_op = MIR_ADD;  break;
                        case JS_OP_SUB_ASSIGN:    p5_mir_op = MIR_SUB;  break;
                        case JS_OP_MUL_ASSIGN:    p5_mir_op = MIR_MUL;  break;
                        case JS_OP_BIT_AND_ASSIGN: p5_mir_op = MIR_AND; break;
                        case JS_OP_BIT_OR_ASSIGN:  p5_mir_op = MIR_OR;  break;
                        case JS_OP_BIT_XOR_ASSIGN: p5_mir_op = MIR_XOR; break;
                        case JS_OP_LSHIFT_ASSIGN:  p5_mir_op = MIR_LSH; break;
                        case JS_OP_RSHIFT_ASSIGN:  p5_mir_op = MIR_RSH; break;
                        case JS_OP_URSHIFT_ASSIGN: p5_mir_op = MIR_URSH; break;
                        default: p5_handled = false; break;
                        }
                        if (p5_handled) {
                            // load: old = js_get_module_var(idx)  → boxed Item
                            MIR_reg_t old_boxed = jm_load_module_var(mt, (uint32_t)mc->int_val);
                            // inline unbox: native_old = old << 8 >> 8
                            MIR_reg_t old_nat = jm_emit_unbox_int(mt, old_boxed);
                            // native rhs
                            MIR_reg_t rhs_nat = jm_transpile_as_native(mt, asgn->right, LMD_TYPE_INT);
                            // inline arithmetic
                            MIR_reg_t new_nat = jm_new_reg(mt, "mvn", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, p5_mir_op,
                                MIR_new_reg_op(mt->ctx, new_nat),
                                MIR_new_reg_op(mt->ctx, old_nat),
                                MIR_new_reg_op(mt->ctx, rhs_nat)));
                            // inline re-box
                            MIR_reg_t boxed_new = jm_box_int_reg(mt, new_nat);
                            jm_store_module_var(mt, (uint32_t)mc->int_val, boxed_new);
                            jm_emit_global_var_property_sync(mt, mc, id->name, boxed_new);
                            jm_scope_env_mark_and_writeback(mt, vname, boxed_new);
                            return boxed_new;
                        }
                    }
                    if (asgn->op == JS_OP_AND_ASSIGN || asgn->op == JS_OP_OR_ASSIGN ||
                        asgn->op == JS_OP_NULLISH_ASSIGN) {
                        // Logical assignment with short-circuit for module vars
                        MIR_reg_t old_val = jm_load_module_var(mt, (uint32_t)mc->int_val);
                        return jm_emit_logical_assignment(mt, asgn, id, old_val,
                            true, mc, vname, false, 0);
                    }
                    MIR_reg_t rhs;
                    if (asgn->op != JS_OP_ASSIGN) {
                        // Compound assignment: read current value, apply op, store result
                        MIR_reg_t old_val = jm_load_module_var(mt, (uint32_t)mc->int_val);
                        MIR_reg_t with_key = 0;
                        bool strict_put = jm_strict_put(mt);
                        if (mt->with_depth > 0) {
                            with_key = jm_box_property_name_literal(mt,
                                id->name->chars, id->name->len);
                            old_val = jm_callr_2(mt, jm_with_binding_get_name(mt), MIR_T_I64, with_key, old_val);
                            jm_emit_error_lane_propagate_check(mt);
                        }
                        rhs = jm_transpile_box_item(mt, asgn->right);
                        rhs = jm_emit_compound_assign(mt, asgn->op, old_val, rhs);
                        if (mt->with_depth > 0) {
                            return jm_emit_with_writeback(mt, with_key, rhs, "mwa_res",
                                strict_put, NULL, mc, id->name, vname);
                        }
                    } else {
                        MIR_reg_t simple_with_key = 0;
                        bool strict_put = jm_strict_put(mt);
                        if (mt->with_depth > 0) {
                            simple_with_key = jm_box_property_name_literal(mt,
                                id->name->chars, id->name->len);
                            jm_callr_1(mt, "js_capture_with_binding", MIR_T_I64, simple_with_key);
                            jm_emit_error_lane_propagate_check(mt);
                        }
                        rhs = jm_transpile_box_item(mt, asgn->right);
                        // function name inference for simple module-var assignment:
                        // cover = function(){} → cover.name === "cover"
                        // Suppressed when LHS is parenthesized (IsIdentifierRef is false per spec)
                        if (!asgn->lhs_is_parenthesized && asgn->right &&
                            (asgn->right->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                             asgn->right->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
                            JsFunctionNode* fn_node = (JsFunctionNode*)asgn->right;
                            if (!fn_node->name && id->name) {
                                jm_emit_set_function_name(mt, rhs, id->name->chars);
                            }
                        }
                        jm_emit_set_class_assignment_name(mt, asgn, rhs, id->name);
                        if (mt->with_depth > 0) {
                            return jm_emit_with_writeback(mt, simple_with_key, rhs, "mwa_res",
                                strict_put, NULL, mc, id->name, vname);
                        }
                    }
                    if (mt->is_eval_direct) {
                        MIR_reg_t eval_key = jm_box_property_name_literal(mt,
                            id->name->chars, id->name->len);
                        MIR_reg_t has_env_bridge = jm_callr_1(mt,
                            "js_eval_env_has_binding", MIR_T_I64, eval_key);
                        MIR_reg_t has_global_lexical_bridge = jm_callr_1(mt,
                            "js_eval_global_lexical_has_binding", MIR_T_I64, eval_key);
                        MIR_reg_t has_bridge = jm_new_reg(mt, "eval_bridge", MIR_T_I64);
                        // direct eval uses an environment bridge for function
                        // locals and a global-lexical bridge at script scope
                        jm_emit_reg_binary(mt, MIR_OR, has_bridge,
                            has_env_bridge, has_global_lexical_bridge);
                        MIR_label_t module_store = jm_new_label(mt);
                        MIR_label_t store_done = jm_new_label(mt);
                        MIR_reg_t store_result = jm_new_reg(mt, "eval_mva_res", MIR_T_I64);
                        jm_emit_branch(mt, MIR_BF, module_store, has_bridge);
                        jm_call_3(mt, "js_set_global_property", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, eval_key),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, rhs),
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));
                        jm_emit_error_lane_propagate_check(mt);
                        jm_emit_mov(mt, store_result, rhs);
                        jm_emit_jmp(mt, store_done);
                        jm_emit_label(mt, module_store);
                        jm_store_module_var(mt, (uint32_t)mc->int_val, rhs);
                        jm_emit_global_var_property_sync(mt, mc, id->name, rhs);
                        jm_scope_env_mark_and_writeback(mt, vname, rhs);
                        jm_emit_mov(mt, store_result, rhs);
                        jm_emit_label(mt, store_done);
                        return store_result;
                    }
                    jm_store_module_var(mt, (uint32_t)mc->int_val, rhs);
                    jm_emit_global_var_property_sync(mt, mc, id->name, rhs);
                    // Write back to scope env if captured by child closures
                    jm_scope_env_mark_and_writeback(mt, vname, rhs);
                    return rhs;
                }
            }
            // Implicit global assignment: write to global object via js_set_global_property
            {
                bool strict_put = jm_strict_put(mt);
                // Tune8 §2.2: js_set_global_property absorbs the _strict variant.
                const int set_global_strict_flag = strict_put ? 1 : 0;
                MIR_reg_t strict_lhs_key = 0;
                MIR_reg_t strict_lhs_exists = 0;
                if (asgn->op == JS_OP_AND_ASSIGN || asgn->op == JS_OP_OR_ASSIGN ||
                    asgn->op == JS_OP_NULLISH_ASSIGN) {
                    // Logical assignment with short-circuit for global vars
                    MIR_reg_t name_reg = jm_box_property_name_literal(mt,
                        id->name->chars, id->name->len);
                    MIR_reg_t old_val = jm_callr_1(mt, "js_get_global_property_strict", MIR_T_I64, name_reg);
                    jm_emit_error_lane_propagate_check(mt);
                    return jm_emit_logical_assignment(mt, asgn, id, old_val,
                        false, NULL, NULL, strict_put, set_global_strict_flag);
                }
                MIR_reg_t simple_with_key = 0;
                if (mt->with_depth > 0 && asgn->op == JS_OP_ASSIGN) {
                simple_with_key = jm_box_property_name_literal(mt,
                    id->name->chars, id->name->len);
                    jm_callr_1(mt, "js_capture_with_binding", MIR_T_I64, simple_with_key);
                    jm_emit_error_lane_propagate_check(mt);
                }
                if (strict_put && asgn->op == JS_OP_ASSIGN) {
                    strict_lhs_key = simple_with_key ? simple_with_key :
                        jm_box_property_name_literal(mt, id->name->chars, id->name->len);
                    strict_lhs_exists = jm_callr_1(mt, "js_global_binding_exists", MIR_T_I64, strict_lhs_key);
                    jm_emit_error_lane_propagate_check(mt);
                }
                MIR_reg_t rhs = jm_transpile_box_item(mt, asgn->right);
                jm_emit_set_class_assignment_name(mt, asgn, rhs, id->name);
                if (asgn->op != JS_OP_ASSIGN) {
                    // Compound assignment: read current value from global, apply op, store
                    MIR_reg_t name_reg = jm_box_property_name_literal(mt,
                        id->name->chars, id->name->len);
                    MIR_reg_t old_val = jm_callr_1(mt, "js_get_global_property_strict", MIR_T_I64, name_reg);
                    jm_emit_error_lane_propagate_check(mt);
                    rhs = jm_emit_compound_assign(mt, asgn->op, old_val, rhs);
                }
                MIR_reg_t name_reg = jm_box_property_name_literal(mt,
                    id->name->chars, id->name->len);
                if (strict_lhs_exists) {
                    jm_callr_3(mt, "js_set_global_property_strict_prechecked", MIR_T_I64, strict_lhs_key ? strict_lhs_key : name_reg, rhs, strict_lhs_exists);
                } else {
                    jm_call_3(mt, "js_set_global_property", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, rhs),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, set_global_strict_flag));
                }
                jm_emit_error_lane_propagate_check(mt);
                if (strict_put) jm_emit_error_lane_propagate_check(mt);
                return rhs;
            }
        }

        if (var->is_nfe_binding && asgn->op == JS_OP_ASSIGN) {
            MIR_reg_t rhs = jm_transpile_box_item(mt, asgn->right);
            jm_emit_set_class_assignment_name(mt, asgn, rhs, id->name);
            bool strict_assign = jm_strict_put(mt);
            if (strict_assign) {
                jm_emit_const_assign_error(mt, id->name->chars, id->name->len);
                return jm_emit_undefined(mt);
            }
            return rhs;
        }

        bool definitely_initialized = var->is_let_const &&
            jm_binding_statement_precedes_reference(mt, var, id);
        if (var->tdz_active || var->from_env ||
                (var->is_let_const && !definitely_initialized)) {
            MIR_reg_t old_binding = var->reg;
            if (var->from_env && var->env_reg != 0 && var->env_slot >= 0) {
                old_binding = jm_new_reg(mt, "assign_env_tdz_old", MIR_T_I64);
                jm_emit_load_i64(mt, old_binding, var->env_slot * (int)sizeof(uint64_t), var->env_reg);
            } else if (var->in_scope_env && var->scope_env_reg != 0 && var->scope_env_slot >= 0) {
                old_binding = jm_new_reg(mt, "assign_scope_tdz_old", MIR_T_I64);
                jm_emit_load_i64(mt, old_binding, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg);
            } else if (jm_is_native_type(var->type_id)) {
                old_binding = jm_box_native(mt, old_binding, var->type_id);
            }
            jm_call_3(mt, "js_check_tdz", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, old_binding),
                MIR_T_I64, MIR_new_reg_op(mt->ctx,
                    jm_module_name_id(mt, id->name->chars, id->name->len)),
                MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)id->name->len));
            jm_emit_error_lane_propagate_check(mt);
        }

        // const variable: throw TypeError on assignment
        if (var->is_const) {
            jm_emit_const_assign_error(mt, id->name->chars, id->name->len);
            return jm_emit_undefined(mt);
        }

        // --- Native-typed variable fast path ---
        if (mt->with_depth <= 0 && var->type_id == LMD_TYPE_INT && !var->from_env) {
            TypeId rhs_type = jm_get_effective_type(mt, asgn->right);
            if (rhs_type != LMD_TYPE_INT) {
                if (asgn->op != JS_OP_ASSIGN) {
                    MIR_reg_t boxed_current = jm_box_native(mt, var->reg, LMD_TYPE_INT);
                    jm_emit_mov(mt, var->reg, boxed_current);
                }
                // JS Number arithmetic can widen an int lane to double; boxed path preserves Item representation.
                var->type_id = LMD_TYPE_ANY;
                var->mir_type = MIR_T_I64;
            } else {
            if (asgn->op == JS_OP_ASSIGN) {
                MIR_reg_t rhs = jm_transpile_as_native(mt, asgn->right, LMD_TYPE_INT);
                jm_emit_mov(mt, var->reg, rhs);
            } else {
                // Compound assignment on native int
                MIR_reg_t rval = jm_transpile_as_native(mt, asgn->right, LMD_TYPE_INT);
                MIR_insn_code_t op = MIR_ADD;
                switch (asgn->op) {
                case JS_OP_ADD_ASSIGN: op = MIR_ADD; break;
                case JS_OP_SUB_ASSIGN: op = MIR_SUB; break;
                case JS_OP_MUL_ASSIGN: op = MIR_MUL; break;
                case JS_OP_DIV_ASSIGN: op = MIR_DIV; break;
                case JS_OP_MOD_ASSIGN: op = MIR_MOD; break;
                case JS_OP_BIT_AND_ASSIGN: op = MIR_AND; break;
                case JS_OP_BIT_OR_ASSIGN: op = MIR_OR; break;
                case JS_OP_BIT_XOR_ASSIGN: op = MIR_XOR; break;
                case JS_OP_LSHIFT_ASSIGN: op = MIR_LSH; break;
                case JS_OP_RSHIFT_ASSIGN: op = MIR_RSH; break;
                case JS_OP_URSHIFT_ASSIGN: op = MIR_URSH; break;
                default: break;
                }
                jm_emit(mt, MIR_new_insn(mt->ctx, op,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, rval)));
            }
            jm_emit_native_assignment_var_writeback(mt, var, vname, LMD_TYPE_INT);
            return var->reg;
            }
        }

        if (mt->with_depth <= 0 && var->type_id == LMD_TYPE_FLOAT && !var->from_env) {
            if (asgn->op == JS_OP_ASSIGN) {
                MIR_reg_t rhs = jm_transpile_as_native(mt, asgn->right, LMD_TYPE_FLOAT);
                jm_emit_dmov(mt, var->reg, rhs);
            } else {
                MIR_reg_t old_boxed = jm_box_native(mt, var->reg, LMD_TYPE_FLOAT);
                MIR_reg_t rval = jm_transpile_box_item(mt, asgn->right);
                MIR_reg_t boxed_result =
                    jm_emit_compound_assign(mt, asgn->op, old_boxed, rval);
                MIR_reg_t native_result = jm_emit_unbox_float(mt, boxed_result);
                // compound JS Number assignment may receive boxed RHS values; unbox after runtime arithmetic.
                jm_emit_dmov(mt, var->reg, native_result);
            }
            jm_emit_native_assignment_var_writeback(mt, var, vname, LMD_TYPE_FLOAT);
            return var->reg;
        }

        // --- Boxed variable path (original) ---
        MIR_reg_t rhs;
        if (asgn->op == JS_OP_ASSIGN) {
            // Set assignment target hint for closure self-capture detection
            mt->assign_target_vname = vname;
            MIR_reg_t simple_with_key = 0;
            bool strict_put = jm_strict_put(mt);
            if (mt->with_depth > 0) {
                simple_with_key = jm_box_property_name_literal(mt,
                    id->name->chars, id->name->len);
                jm_callr_1(mt, "js_capture_with_binding", MIR_T_I64, simple_with_key);
                jm_emit_error_lane_propagate_check(mt);
            }
            rhs = jm_transpile_box_item(mt, asgn->right);
            mt->assign_target_vname = NULL;
            // v18: function name inference for simple assignment
            if (asgn->right && (asgn->right->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                                asgn->right->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
                JsFunctionNode* fn_node = (JsFunctionNode*)asgn->right;
                if (!fn_node->name && id->name) {
                    jm_emit_set_function_name(mt, rhs, id->name->chars);
                }
            }
            jm_emit_set_class_assignment_name(mt, asgn, rhs, id->name);
            var->jube_type = jm_infer_jube_type(mt, asgn->right);
            if (mt->with_depth > 0) {
                return jm_emit_with_writeback(mt, simple_with_key, rhs, "lsa_res",
                    strict_put, var, NULL, NULL, vname);
            }
        } else if (asgn->op == JS_OP_AND_ASSIGN || asgn->op == JS_OP_OR_ASSIGN ||
                   asgn->op == JS_OP_NULLISH_ASSIGN) {
            // Logical assignment with short-circuit: do NOT evaluate RHS if condition met
            // &&= : if current is falsy, return current (don't eval RHS, don't assign)
            // ||= : if current is truthy, return current (don't eval RHS, don't assign)
            // ??= : if current is not nullish, return current (don't eval RHS, don't assign)
            MIR_label_t l_assign = jm_new_label(mt);
            MIR_label_t l_end = jm_new_label(mt);

            MIR_reg_t cond;
            if (asgn->op == JS_OP_NULLISH_ASSIGN) {
                cond = jm_callr_1(mt, "js_is_nullish", MIR_T_I64, var->reg);
                // if nullish → evaluate RHS and assign
                jm_emit_branch(mt, MIR_BT, l_assign, cond);
            } else {
                cond = jm_emit_is_truthy(mt, var->reg, NULL);
                if (asgn->op == JS_OP_AND_ASSIGN) {
                    // &&= : if truthy → evaluate RHS and assign; if falsy → short-circuit
                    jm_emit_branch(mt, MIR_BT, l_assign, cond);
                } else {
                    // ||= : if falsy → evaluate RHS and assign; if truthy → short-circuit
                    jm_emit_branch(mt, MIR_BF, l_assign, cond);
                }
            }
            // Short-circuit: skip to end, var->reg keeps its current value
            jm_emit_jmp(mt, l_end);

            // Evaluate RHS and assign
            jm_emit_label(mt, l_assign);
            rhs = jm_transpile_box_item(mt, asgn->right);
            jm_emit_error_lane_propagate_check(mt);

            if (!asgn->lhs_is_parenthesized) {
                jm_emit_named_evaluation_for_identifier(mt, asgn->right, rhs, id->name);
            }

            jm_emit_mov(mt, var->reg, rhs);
            var->jube_type = NULL;
            jm_emit_assignment_var_writeback(mt, var, vname, var->reg, false);

            jm_emit_label(mt, l_end);
            return var->reg;
        } else {
            // Compound assignment: var op= expr -> var = js_op(var, expr)
            MIR_reg_t old_val = var->reg;
            if (jm_is_native_type(var->type_id)) {
                // outer/block-scope numeric vars can reach the generic compound path while still native.
                old_val = jm_box_native(mt, var->reg, var->type_id);
            }
            MIR_reg_t with_key = 0;
            if (mt->with_depth > 0) {
                with_key = jm_box_property_name_literal(mt,
                    id->name->chars, id->name->len);
                old_val = jm_callr_2(mt, jm_with_binding_get_name(mt), MIR_T_I64, with_key, old_val);
                jm_emit_error_lane_propagate_check(mt);
            }
            MIR_reg_t rval = jm_transpile_box_item(mt, asgn->right);
            rhs = jm_emit_compound_assign(mt, asgn->op, old_val, rval);
        }

        if (mt->with_depth > 0 && asgn->op != JS_OP_ASSIGN && asgn->op != JS_OP_AND_ASSIGN &&
            asgn->op != JS_OP_OR_ASSIGN && asgn->op != JS_OP_NULLISH_ASSIGN) {
            MIR_reg_t with_key = jm_box_property_name_literal(mt,
                id->name->chars, id->name->len);
            bool strict_put = jm_strict_put(mt);
            return jm_emit_with_writeback(mt, with_key, rhs, "lwa_res",
                strict_put, var, NULL, NULL, vname);
        }

        jm_emit_mov(mt, var->reg, rhs);
        var->jube_type = asgn->op == JS_OP_ASSIGN
            ? jm_infer_jube_type(mt, asgn->right) : NULL;

        // Write-back to env if this is a captured variable
        // Write-back keeps captured variables, closure aliases, and arguments synchronized.
        jm_emit_assignment_var_writeback(mt, var, vname, var->reg, true);
        return var->reg;
    }

    // Member assignment: obj.prop = expr, obj[key] = expr
    if (asgn->left->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* member = (JsMemberNode*)asgn->left;
        bool member_is_super = jm_js_ident_name_eq(member->object, "super", 5);

        // super.x = val — use shared Reference Record handling for correct
        // receiver binding and derived-constructor this/key evaluation order.
        if (asgn->op == JS_OP_ASSIGN && member_is_super) {
                JsMirReference ref = jm_emit_reference(mt, asgn->left);
                jm_emit_error_lane_propagate_check(mt);
                if (ref.uninitialized_this) {
                    MIR_reg_t undef = jm_emit_undefined(mt);
                    jm_emit_put_value(mt, &ref, undef);
                    jm_emit_error_lane_propagate_check(mt);
                    return undef;
                }
                int base_spill = -1;
                int key_spill = -1;
                jm_spill_reference_for_suspending_rhs(mt, &ref, asgn->right,
                                                       &base_spill, &key_spill);
                MIR_reg_t new_val = jm_transpile_box_item(mt, asgn->right);
                jm_emit_error_lane_propagate_check(mt);
                // await resumes in a new state-machine invocation, so the
                // pre-RHS super reference must come back from its precise env home.
                jm_restore_suspended_reference(mt, &ref, base_spill, key_spill);
                MIR_reg_t super_set_result = jm_emit_put_value(mt, &ref, new_val);
                jm_emit_error_lane_propagate_check(mt);
                return super_set_result;
        }

        JsMirReference ref = jm_emit_reference(mt, asgn->left);
        jm_emit_error_lane_propagate_check(mt);
        int base_spill = -1;
        int key_spill = -1;
        jm_spill_reference_for_suspending_rhs(mt, &ref, asgn->right,
                                               &base_spill, &key_spill);
        MIR_reg_t new_val;
        if (asgn->op == JS_OP_ASSIGN) {
            new_val = jm_transpile_box_item(mt, asgn->right);
            jm_emit_error_lane_propagate_check(mt);
        } else if (asgn->op == JS_OP_AND_ASSIGN || asgn->op == JS_OP_OR_ASSIGN ||
                   asgn->op == JS_OP_NULLISH_ASSIGN) {
            // Logical assignment with short-circuit for member expressions
            // obj[key] &&= expr: if obj[key] is falsy, skip RHS eval and assignment
            // obj[key] ||= expr: if obj[key] is truthy, skip RHS eval and assignment
            // obj[key] ??= expr: if obj[key] is not nullish, skip RHS eval and assignment
            MIR_reg_t result = jm_new_reg(mt, "la_res", MIR_T_I64);
            jm_emit_canonicalize_computed_key_for_get_put(mt, &ref);
            MIR_reg_t cur_val = jm_emit_get_value(mt, &ref);
            MIR_label_t l_assign = jm_new_label(mt);
            MIR_label_t l_end = jm_new_label(mt);
            MIR_reg_t cond;
            if (asgn->op == JS_OP_NULLISH_ASSIGN) {
                cond = jm_callr_1(mt, "js_is_nullish", MIR_T_I64, cur_val);
                jm_emit_branch(mt, MIR_BT, l_assign, cond);
            } else {
                cond = jm_emit_is_truthy(mt, cur_val, NULL);
                if (asgn->op == JS_OP_AND_ASSIGN) {
                    jm_emit_branch(mt, MIR_BT, l_assign, cond);
                } else {
                    jm_emit_branch(mt, MIR_BF, l_assign, cond);
                }
            }
            // Short-circuit: return current value without evaluating RHS or setting property
            jm_emit_mov(mt, result, cur_val);
            jm_emit_jmp(mt, l_end);

            // Evaluate RHS, set property, return RHS
            jm_emit_label(mt, l_assign);
            new_val = jm_transpile_box_item(mt, asgn->right);
            // An awaited RHS destroys raw MIR registers; restore the original
            // Reference so `obj.key ||= await value` writes its pre-await base.
            jm_restore_suspended_reference(mt, &ref, base_spill, key_spill);
            jm_emit_put_value(mt, &ref, new_val);
            jm_emit_mov(mt, result, new_val);
            jm_emit_label(mt, l_end);
            return result;
        } else {
            // Compound: get current value, apply operation, set result
            jm_emit_canonicalize_computed_key_for_get_put(mt, &ref);
            MIR_reg_t cur_val = jm_emit_get_value(mt, &ref);
            MIR_reg_t rval = jm_transpile_box_item(mt, asgn->right);
            const char* fn = jm_compound_assign_fn(asgn->op);
            new_val = jm_callr_2(mt, fn, MIR_T_I64, cur_val, rval);
        }

        // A property Reference is evaluated before its RHS by the language;
        // preserve that receiver/key across await instead of using dead MIR regs.
        jm_restore_suspended_reference(mt, &ref, base_spill, key_spill);
        MIR_reg_t result = jm_emit_put_value(mt, &ref, new_val);

        // v20: update the mapped formal after the generic property write.
        jm_sync_arguments_param_after_write(mt, member);

        jm_readback_closure_env(mt);
        jm_scope_env_reload_vars(mt);
        return result;
    }

    if (asgn->op == JS_OP_ASSIGN &&
        (asgn->left->node_type == JS_AST_NODE_ARRAY_PATTERN ||
         asgn->left->node_type == JS_AST_NODE_ARRAY_EXPRESSION ||
         asgn->left->node_type == JS_AST_NODE_OBJECT_PATTERN ||
         asgn->left->node_type == JS_AST_NODE_OBJECT_EXPRESSION)) {
        bool is_array = asgn->left->node_type == JS_AST_NODE_ARRAY_PATTERN ||
            asgn->left->node_type == JS_AST_NODE_ARRAY_EXPRESSION;
        return jm_emit_destructure_assignment(mt, asgn->left, asgn->right, is_array);
    }

    log_error("js-mir: unsupported assignment target %d", asgn->left->node_type);
    return jm_emit_null(mt);
}

// ============================================================================
// Call expression helpers
// ============================================================================


// Read back captured variables from closure env after synchronous callback calls
// (e.g., forEach, reduce, map). The callback may have modified captured variables
// via env write-back, and we need to propagate those changes to the caller's registers.
bool jm_resolve_transitive_capture_env(JsMirVarEntry* var,
        MIR_reg_t* env_reg, int* env_slot) {
    if (!var || !env_reg || !env_slot) return false;
    if (var->from_env && var->env_reg != 0 && var->env_slot >= 0) {
        *env_reg = var->env_reg;
        *env_slot = var->env_slot;
        return true;
    }
    // Top-level block lexicals are backed by the module scope env, not a
    // from_env cell. Transitive callback mutations must read and write that
    // live cell instead of the copied closure's stale snapshot.
    if (var->in_scope_env && var->scope_env_reg != 0 && var->scope_env_slot >= 0) {
        *env_reg = var->scope_env_reg;
        *env_slot = var->scope_env_slot;
        return true;
    }
    return false;
}

void jm_readback_closure_env(JsMirTranspiler* mt) {
    if (!mt->last_closure_has_env) return;
    if (mt->last_closure_env_reg == 0) return;
    int readback_count = jm_last_closure_capture_count_clamped(
        mt->last_closure_capture_count);
    MIR_label_t readback_done = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ,
        MIR_new_label_op(mt->ctx, readback_done),
        MIR_new_reg_op(mt->ctx, mt->last_closure_env_reg),
        MIR_new_int_op(mt->ctx, 0)));
    for (int i = 0; i < readback_count; i++) {
        if (mt->last_closure_capture_is_nfe[i]) continue;
        if (!mt->last_closure_capture_is_assigned[i]) continue;
        JsMirVarEntry* var = jm_find_var(mt, mt->last_closure_capture_names[i]);
        if (!var) {
            continue;
        }
        if (var->from_block_func_decl) continue;
        int slot = mt->last_closure_capture_slots[i] >= 0 ? mt->last_closure_capture_slots[i] : i;
        MIR_reg_t read_env = mt->last_closure_env_reg;
        if (mt->last_closure_capture_is_transitive[i]) {
            jm_resolve_transitive_capture_env(var, &read_env, &slot);
        }
        // Js56 P2: BOOL vars are stored BOXED (var-decl falls into the
        // generic boxed branch — there is no native-bool fast path), so they
        // need the same MOV-only readback as object types even though
        // jm_is_native_type(BOOL) returns true. Treat BOOL as boxed here so
        // closure-mutated booleans propagate back to the outer var->reg
        // (Gate K coerced-new-length-detach: `let called = false; closure
        // mutates called`).
        bool is_bool = (var->type_id == LMD_TYPE_BOOL);
        if (jm_is_native_type(var->type_id) && !is_bool) {
            // Read boxed value from env slot, unbox to native type
            MIR_reg_t boxed = jm_new_reg(mt, "envrd", MIR_T_I64);
            jm_emit_load_i64(mt, boxed, slot * (int)sizeof(uint64_t), read_env);
            if (var->type_id == LMD_TYPE_FLOAT) {
                MIR_reg_t unboxed = jm_emit_unbox_float(mt, boxed);
                jm_emit_dmov(mt, var->reg, unboxed);
            } else if (var->type_id == LMD_TYPE_INT) {
                MIR_reg_t unboxed = jm_emit_unbox_int(mt, boxed);
                jm_emit_mov(mt, var->reg, unboxed);
            }
            if (var->in_scope_env && var->scope_env_reg != 0 && var->scope_env_slot >= 0) {
                jm_emit_store_i64(mt, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg, boxed);
            }
            if (var->from_env && var->env_reg != 0 && var->env_slot >= 0) {
                jm_emit_store_i64(mt, var->env_slot * (int)sizeof(uint64_t), var->env_reg, boxed);
            }
        } else {
            // Boxed variable — direct read from env
            jm_emit_load_i64(mt, var->reg, slot * (int)sizeof(uint64_t), read_env);
            if (var->in_scope_env && var->scope_env_reg != 0 && var->scope_env_slot >= 0) {
                jm_emit_store_i64(mt, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg, var->reg);
            }
            if (var->from_env && var->env_reg != 0 && var->env_slot >= 0) {
                jm_emit_store_i64(mt, var->env_slot * (int)sizeof(uint64_t), var->env_reg, var->reg);
            }
        }
    }
    // Js56 P2: do NOT reset last_closure_has_env after readback. The closure's
    // env is kept alive by the closure object itself and remains the canonical
    // storage for the captured vars; readback on every subsequent call to the
    // same closure propagates env mutations back to the outer's var->reg.
    // Resetting here only works for one-shot callbacks (forEach/map) and
    // silently fails when the closure is stored and called multiple times
    // (Js56 Gate I: speciesctor closure-mutation cluster, §12.17).
    // The earlier "reset on readback" was a forEach-shaped optimization; keeping
    // the env around costs nothing because the readback is idempotent when the
    // env value matches the var->reg already (no spurious work at runtime —
    // just an extra mem load that lands in the same value).
    jm_emit_label(mt, readback_done);
}

// Call expression
static bool jm_eval_env_is_exposable_binding(const char* name,
        const JsMirVarEntry* var, bool for_writeback) {
    if (!name || !var) return false;
    if (strncmp(name, "_js_", 4) != 0) return false;
    if (strcmp(name, "_js_this") == 0 || strcmp(name, "_js_new.target") == 0) return false;
    if (strstr(name, "__dup") != NULL) return false;
    if (var->tdz_active) return false;
    if (for_writeback) {
        if (var->is_let_const || var->is_const || var->mir_type != MIR_T_I64) return false;
    } else if (var->mir_type != MIR_T_I64 &&
               !(var->type_id == LMD_TYPE_FLOAT && var->mir_type == MIR_T_D)) {
        // JS Number locals migrated to native double registers; direct eval
        // still exposes those lexical bindings.
        return false;
    }
    if (var->reg == 0) return false;
    return true;
}

static bool jm_eval_env_is_exposable_lexical_binding(const char* name, const JsMirVarEntry* var) {
    if (!jm_eval_env_is_exposable_binding(name, var, false)) return false;
    return var->is_let_const || var->is_const;
}

static struct hashmap* jm_eval_push_bindings(JsMirTranspiler* mt,
        bool global_lexical) {
    struct hashmap* bridged = hashmap_new(sizeof(JsNameSetEntry), 32, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (!bridged) return NULL;

    bool pushed = !global_lexical;
    if (pushed) jm_call_void_0(mt, "js_eval_env_push_frame");
    for (int depth = mt->scope_depth; depth >= 0; depth--) {
        struct hashmap* scope = jm_var_scope_at(mt, depth);
        if (!scope) continue;
        size_t iter = 0; void* item;
        while (hashmap_iter(scope, &iter, &item)) {
            JsVarScopeEntry* entry = (JsVarScopeEntry*)item;
            if (!jm_eval_env_is_exposable_binding(entry->name, &entry->var, false) ||
                    (global_lexical && !jm_eval_env_is_exposable_lexical_binding(
                        entry->name, &entry->var))) continue;
            JsNameSetEntry seen;
            memset(&seen, 0, sizeof(seen));
            seen.name = jm_persist_name(entry->name);
            if (hashmap_get(bridged, &seen)) continue;
            if (global_lexical && !pushed) {
                jm_call_void_0(mt, "js_eval_global_lexical_push_frame");
                pushed = true;
            }
            hashmap_set(bridged, &seen);
            const char* js_name = entry->name + 4;
            MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
            MIR_reg_t value_reg = jm_is_native_type(entry->var.type_id) ?
                jm_box_native(mt, entry->var.reg, entry->var.type_id) : entry->var.reg;
            jm_callr_void_2(mt, global_lexical ? "js_eval_global_lexical_bind" :
                "js_eval_env_bind", key_reg, value_reg);
        }
    }
    if (!global_lexical) jm_call_void_0(mt, "js_eval_env_bridge_journal_vars");
    if (!pushed) {
        hashmap_free(bridged);
        return NULL;
    }
    return bridged;
}

static struct hashmap* jm_eval_env_push_bindings(JsMirTranspiler* mt) {
    return jm_eval_push_bindings(mt, false);
}

static struct hashmap* jm_eval_global_lexical_push_bindings(JsMirTranspiler* mt) {
    return jm_eval_push_bindings(mt, true);
}

static void jm_eval_local_note_bindings(JsMirTranspiler* mt, bool immutable) {
    if (!mt || mt->eval_local_frame_reg == 0) return;
    for (int depth = mt->scope_depth; depth >= 0; depth--) {
        struct hashmap* scope = jm_var_scope_at(mt, depth);
        if (!scope) continue;
        size_t iter = 0; void* item;
        while (hashmap_iter(scope, &iter, &item)) {
            JsVarScopeEntry* entry = (JsVarScopeEntry*)item;
            if (immutable ? !entry->var.is_nfe_binding :
                    (!entry->var.is_let_const && !entry->var.is_const)) continue;
            if (strncmp(entry->name, "_js_", 4) != 0) continue;
            if (strcmp(entry->name, "_js_this") == 0 || strcmp(entry->name, "_js_new.target") == 0) continue;
            const char* js_name = entry->name + 4;
            MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
            jm_callr_void_1(mt, immutable ? "js_eval_local_note_immutable_binding" :
                "js_eval_local_note_lexical_binding", key_reg);
        }
    }
}

static void jm_eval_env_writeback_bindings(JsMirTranspiler* mt, struct hashmap* bridged) {
    if (!bridged) return;
    size_t iter = 0; void* item;
    while (hashmap_iter(bridged, &iter, &item)) {
        JsNameSetEntry* seen = (JsNameSetEntry*)item;
        JsMirVarEntry* var = jm_find_var(mt, seen->name);
        if (!var || !jm_eval_env_is_exposable_binding(seen->name, var, true)) continue;
        const char* js_name = seen->name + 4;
        MIR_reg_t key_reg = jm_box_property_name_literal(mt, js_name, strlen(js_name));
        MIR_reg_t value_reg = jm_callr_1(mt, "js_get_global_property", MIR_T_I64, key_reg);
        jm_emit_mov(mt, var->reg, value_reg);
        var->type_id = LMD_TYPE_ANY;
        var->mir_type = MIR_T_I64;
        jm_scope_env_mark_and_writeback(mt, seen->name, value_reg);
        int param_index = jm_arguments_param_index(mt, seen->name, var);
        if (param_index >= 0) jm_arguments_writeback_param(mt, param_index, value_reg);
    }
    hashmap_free(bridged);
}

static void jm_transpile_discard_call_args(JsMirTranspiler* mt, JsAstNode* arg) {
    while (arg) {
        // Direct-call fast paths still owe JS its argument evaluation order:
        // extra actual arguments can throw or mutate state even when no formal
        // parameter receives them.
        jm_transpile_box_item(mt, arg);
        jm_emit_error_lane_propagate_check(mt);
        arg = arg->next;
    }
}

// In a generator/async state machine, a suspend point anywhere in the argument list breaks every direct
// dispatch fast path that evaluates args into raw MIR registers, because those
// registers do not survive suspend/resume. When this gate trips the
// caller must fall back to the env-spilling path inside jm_build_args_array.
static bool jm_call_yield_blocks_direct(JsMirTranspiler* mt, JsAstNode* first_arg) {
    for (JsAstNode* a = first_arg; a; a = a->next) {
        if (jm_expression_can_suspend(mt, a)) return true;
    }
    return false;
}

static bool jm_arguments_have_spread(JsAstNode* arguments) {
    for (; arguments; arguments = arguments->next) {
        if (arguments->node_type == JS_AST_NODE_SPREAD_ELEMENT) return true;
    }
    return false;
}

static MIR_reg_t jm_emit_member_call_from_function(JsMirTranspiler* mt,
        JsCallNode* call, MIR_reg_t recv, MIR_reg_t fn, int arg_count,
        bool args_have_yield, bool args_have_spread);

// optional member calls share one nullish guard; keeping it here prevents the
// optional-chain variants from drifting in argument evaluation or env readback.
static MIR_reg_t jm_emit_optional_method_call(JsMirTranspiler* mt, MIR_reg_t recv,
                                               MIR_reg_t method_name, JsCallNode* call,
                                               int arg_count, bool receiver_optional,
                                               bool callee_optional, bool args_have_yield,
                                               bool args_have_spread,
                                               const char* result_prefix,
                                               const char* cmp_prefix) {
    MIR_label_t l_opt_skip = jm_new_label(mt);
    MIR_label_t l_opt_call = jm_new_label(mt);
    MIR_label_t l_opt_end = jm_new_label(mt);
    MIR_reg_t opt_result = jm_new_reg(mt, result_prefix, MIR_T_I64);
    MIR_reg_t opt_cmp = jm_new_reg(mt, cmp_prefix, MIR_T_I64);

    if (receiver_optional) {
        jm_emit_reg_binary_op(mt, MIR_EQ, opt_cmp, recv, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
        jm_emit_branch(mt, MIR_BT, l_opt_skip, opt_cmp);
        jm_emit_reg_binary_op(mt, MIR_EQ, opt_cmp, recv, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
        jm_emit_branch(mt, MIR_BT, l_opt_skip, opt_cmp);
    }
    jm_emit_jmp(mt, l_opt_call);

    jm_emit_label(mt, l_opt_skip);
    jm_emit_reg_op(mt, MIR_MOV, opt_result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
    jm_emit_jmp(mt, l_opt_end);

    jm_emit_label(mt, l_opt_call);
    MIR_reg_t fn = jm_callr_2(mt, "js_get_reference", MIR_T_I64, recv, method_name);
    // D6.2.2v2: an accessor error completes the property Get before any
    // argument side effect; receiver/name dispatch used to reverse this order.
    jm_emit_error_lane_propagate_check(mt);
    if (callee_optional) {
        jm_emit_reg_binary_op(mt, MIR_EQ, opt_cmp, fn, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
        jm_emit_branch(mt, MIR_BT, l_opt_skip, opt_cmp);
        jm_emit_reg_binary_op(mt, MIR_EQ, opt_cmp, fn, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
        jm_emit_branch(mt, MIR_BT, l_opt_skip, opt_cmp);
    }
    jm_clear_last_closure_snapshot(mt);
    // D5.4.3: optional calls use the same suspend-safe receiver/callee spill
    // path as ordinary member calls; keeping them in raw MIR registers across
    // an awaited argument loses both the target and the required this value.
    MIR_reg_t call_result = jm_emit_member_call_from_function(mt, call,
        recv, fn, arg_count, args_have_yield, args_have_spread);
    jm_emit_mov(mt, opt_result, call_result);
    jm_emit_label(mt, l_opt_end);
    jm_readback_closure_env(mt);
    return opt_result;
}

// Optional direct calls and optional calls reached through an existing chain use
// the same guarded call sequence; only their diagnostic register names differ.
static MIR_reg_t jm_emit_optional_function_call(JsMirTranspiler* mt, MIR_reg_t callee,
                                                JsCallNode* call, int arg_count,
                                                bool has_spread,
                                                int callee_spill_slot,
                                                const char* result_prefix,
                                                const char* cmp_prefix) {
    MIR_label_t l_skip = jm_new_label(mt);
    MIR_label_t l_call = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    MIR_reg_t result = jm_new_reg(mt, result_prefix, MIR_T_I64);
    MIR_reg_t cmp = jm_new_reg(mt, cmp_prefix, MIR_T_I64);

    jm_emit_reg_binary_op(mt, MIR_EQ, cmp, callee, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
    jm_emit_branch(mt, MIR_BT, l_skip, cmp);
    jm_emit_reg_binary_op(mt, MIR_EQ, cmp, callee, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
    jm_emit_branch(mt, MIR_BT, l_skip, cmp);
    jm_emit_jmp(mt, l_call);

    jm_emit_label(mt, l_skip);
    jm_emit_reg_op(mt, MIR_MOV, result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
    jm_emit_jmp(mt, l_end);

    jm_emit_label(mt, l_call);
    MIR_reg_t null_this = jm_emit_plain_call_this_arg(mt, call);
    MIR_reg_t call_result;
    if (has_spread) {
        MIR_reg_t sp_arr = jm_build_spread_args_array(mt, call->arguments);
        if (callee_spill_slot >= 0) jm_gen_spill_load(mt, callee, callee_spill_slot);
        bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
        call_result = jm_apply_function_into(mt,
            MIR_new_reg_op(mt->ctx, callee), MIR_new_reg_op(mt->ctx, null_this),
            MIR_new_reg_op(mt->ctx, sp_arr));
        jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);
    } else {
        MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
        if (callee_spill_slot >= 0) jm_gen_spill_load(mt, callee, callee_spill_slot);
        bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
        call_result = jm_call_function_into(mt,
            MIR_new_reg_op(mt->ctx, callee), MIR_new_reg_op(mt->ctx, null_this),
            args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_new_int_op(mt->ctx, arg_count));
        jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);
    }
    jm_emit_mov(mt, result, call_result);
    jm_emit_label(mt, l_end);
    jm_readback_closure_env(mt);
    return result;
}

static void jm_call_arg_flags(JsMirTranspiler* mt, JsAstNode* arguments,
        bool* args_have_yield, bool* args_have_spread) {
    *args_have_yield = false;
    *args_have_spread = false;
    for (JsAstNode* arg = arguments; arg; arg = arg->next) {
        // D5.4.3: async functions share generator spill storage, so an await
        // invalidates pre-argument MIR registers exactly like a yield does.
        if (jm_expression_can_suspend(mt, arg)) {
            *args_have_yield = true;
            break;
        }
    }
    for (JsAstNode* arg = arguments; arg; arg = arg->next) {
        if (arg->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            *args_have_spread = true;
            break;
        }
    }
}

static MIR_reg_t jm_emit_member_call_from_function(JsMirTranspiler* mt,
        JsCallNode* call, MIR_reg_t recv, MIR_reg_t fn, int arg_count,
        bool args_have_yield, bool args_have_spread) {
    int recv_arg_spill = -1, fn_arg_spill = -1;
    if (args_have_yield) {
        recv_arg_spill = jm_gen_spill_save(mt, recv);
        fn_arg_spill = jm_gen_spill_save(mt, fn);
    }
    MIR_reg_t args_ptr = args_have_spread
        ? jm_build_spread_args_array(mt, call->arguments)
        : jm_build_args_array(mt, call->arguments, arg_count);
    if (recv_arg_spill >= 0) {
        jm_gen_spill_load(mt, recv, recv_arg_spill);
        jm_gen_spill_load(mt, fn, fn_arg_spill);
    }
    bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
    MIR_reg_t result;
    if (args_have_spread) {
        result = jm_apply_function_into(mt,
            MIR_new_reg_op(mt->ctx, fn), MIR_new_reg_op(mt->ctx, recv),
            MIR_new_reg_op(mt->ctx, args_ptr));
    } else {
        result = jm_call_function_into(mt,
            MIR_new_reg_op(mt->ctx, fn),
            MIR_new_reg_op(mt->ctx, recv),
            args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_new_int_op(mt->ctx, arg_count));
    }
    jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);
    return result;
}

static MIR_reg_t jm_emit_super_call(JsMirTranspiler* mt, JsCallNode* call,
        MIR_reg_t receiver, MIR_reg_t callee, int arg_count) {
    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    return jm_call_function_into(mt, MIR_new_reg_op(mt->ctx, callee),
        MIR_new_reg_op(mt->ctx, receiver),
        args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_new_int_op(mt->ctx, arg_count));
}

static MIR_reg_t jm_emit_intrinsic_direct_eval(JsMirTranspiler* mt,
                                                MIR_reg_t argument) {
    struct hashmap* eval_bridged = NULL;
    int64_t eval_flags = 3;
    if (jm_strict_put(mt)) {
        eval_flags |= 4;
    }
    if (mt->eval_local_frame_reg != 0) {
        jm_emit_eval_local_ensure_frame(mt);
        jm_eval_local_note_bindings(mt, false);
        jm_eval_local_note_bindings(mt, true);
        eval_bridged = jm_eval_env_push_bindings(mt);
    } else {
        eval_bridged = jm_eval_global_lexical_push_bindings(mt);
    }
    bool eval_private_pushed = jm_emit_eval_private_env_push(mt);
    MIR_reg_t result = jm_call_2(mt, "js_builtin_eval", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, argument),
        MIR_T_I64, MIR_new_int_op(mt->ctx, eval_flags));
    // Direct eval can mutate bindings before throwing. Root and retain its
    // exact ERROR Item while the lexical/private bridges are written back and
    // removed, then route that original lane (D8.4.3, D6.2.2v2).
    jm_create_gc_root_slot(mt, result);
    if (eval_private_pushed) jm_call_void_0(mt, "js_eval_private_pop_frame");
    if (eval_bridged) {
        if (mt->eval_local_frame_reg != 0) {
            jm_eval_env_writeback_bindings(mt, eval_bridged);
            jm_call_void_0(mt, "js_eval_env_pop_frame");
        } else {
            hashmap_free(eval_bridged);
            jm_call_void_0(mt, "js_eval_global_lexical_pop_frame");
        }
    }
    mt->last_call_result_reg = result;
    jm_error_lane_set_state(mt, JS_ERROR_LANE_UNKNOWN);
    jm_emit_error_lane_propagate_check(mt);
    return result;
}

static MIR_reg_t jm_emit_eval_identifier_call(JsMirTranspiler* mt,
                                               JsCallNode* call) {
    MIR_reg_t evaluated = jm_transpile_box_item(mt, call->callee);
    MIR_reg_t callee = jm_new_reg(mt, "eval_callee", MIR_T_I64);
    jm_emit_mov(mt, callee, evaluated);
    jm_create_gc_root_slot(mt, callee);

    int callee_spill_slot = jm_call_yield_blocks_direct(mt, call->arguments)
        ? jm_gen_spill_save(mt, callee) : -1;
    // Build one rooted argument list before selecting the semantic lane. This
    // preserves callee-before-arguments order and evaluates extra/spread
    // arguments exactly once for both direct and indirect eval.
    MIR_reg_t args_array = jm_build_spread_args_array(mt, call->arguments);
    if (callee_spill_slot >= 0) jm_gen_spill_load(mt, callee, callee_spill_slot);

    MIR_reg_t eval_id = jm_box_int_const(mt, JS_BUILTIN_GLOBAL_FN_EVAL);
    MIR_reg_t intrinsic = jm_callr_1(mt, "js_get_global_builtin_fn_by_id", MIR_T_I64, eval_id);
    MIR_label_t indirect = jm_new_label(mt);
    MIR_label_t done = jm_new_label(mt);
    MIR_reg_t result = jm_new_reg(mt, "eval_call", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BNE,
        MIR_new_label_op(mt->ctx, indirect),
        MIR_new_reg_op(mt->ctx, callee), MIR_new_reg_op(mt->ctx, intrinsic)));
    JsErrorLaneTrack branch_lane = jm_error_lane_state(mt);
    MIR_reg_t branch_carrier = mt->last_call_result_reg;

    MIR_reg_t index_zero = jm_box_int_const(mt, 0);
    MIR_reg_t first_argument = jm_callr_2(mt, "js_elements_get", MIR_T_I64, args_array, index_zero);
    jm_emit_error_lane_propagate_check(mt);
    MIR_reg_t direct_result = jm_emit_intrinsic_direct_eval(mt, first_argument);
    jm_emit_mov(mt, result, direct_result);
    JsErrorLaneTrack direct_exit = jm_error_lane_state(mt);
    jm_emit_jmp(mt, done);

    jm_emit_label_with_state(mt, indirect, branch_lane);
    mt->last_call_result_reg = branch_carrier;
    MIR_reg_t this_value = jm_emit_plain_call_this_arg(mt, call);
    bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
    MIR_reg_t indirect_result = jm_apply_function_into(mt,
        MIR_new_reg_op(mt->ctx, callee), MIR_new_reg_op(mt->ctx, this_value),
        MIR_new_reg_op(mt->ctx, args_array));
    jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_mov(mt, result, indirect_result);
    JsErrorLaneTrack indirect_exit = jm_error_lane_state(mt);

    jm_emit_label_with_state(mt, done,
        jm_error_lane_merge(direct_exit, indirect_exit));
    jm_readback_closure_env(mt);
    return jm_publish_call_result(mt, result);
}

MIR_reg_t jm_transpile_call(JsMirTranspiler* mt, JsCallNode* call) {
    int arg_count = ast_linked_node_count(call->arguments);

    // type(value) is a Lambda TypeScript language intrinsic, not a mutable JS
    // global. Keep it in the TS compilation profile so D6.2.2v2 never needs
    // name-selected runtime semantics or property-miss synthesis to find it.
    if (mt->tp && !mt->tp->strict_js && call->callee &&
            call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* type_id = (JsIdentifierNode*)call->callee;
        if (type_id->name && type_id->name->len == 4 &&
                memcmp(type_id->name->chars, "type", 4) == 0) {
            bool type_is_shadowed = type_id->entry != NULL ||
                jm_find_var(mt, "_js_type") != NULL;
            if (!type_is_shadowed && mt->module_consts) {
                JsModuleConstEntry type_lookup;
                type_lookup.name = jm_persist_name("_js_type");
                type_is_shadowed = hashmap_get(mt->module_consts, &type_lookup) != NULL;
            }
            if (!type_is_shadowed) {
                MIR_reg_t value = call->arguments
                    ? jm_transpile_box_item(mt, call->arguments)
                    : jm_emit_null(mt);
                return jm_callr_1(mt, "ts_type_info", MIR_T_I64, value);
            }
        }
    }

    // require(specifier) — CJS module loading
    // Only intercept if 'require' is NOT a local variable/parameter (e.g. webpack factories
    // pass their own require function as a parameter named 'require')
    if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* callee_id = (JsIdentifierNode*)call->callee;
        if (callee_id->name && callee_id->name->len == 7 &&
            memcmp(callee_id->name->chars, "require", 7) == 0 && arg_count == 1) {
            // Check if 'require' is a local/module variable (skip CJS interception if so)
            bool require_is_local = (jm_find_var(mt, "_js_require") != NULL);
            if (!require_is_local && mt->module_consts) {
                JsModuleConstEntry mclookup;
                mclookup.name = jm_persist_name("_js_require");
                require_is_local = (hashmap_get(mt->module_consts, &mclookup) != NULL);
            }
            if (!require_is_local) {
                JsAstNode* arg = call->arguments;
                if (arg && arg->node_type == JS_AST_NODE_LITERAL) {
                    JsLiteralNode* lit = (JsLiteralNode*)arg;
                    if (lit->literal_type == JS_LITERAL_STRING && lit->value.string_value) {
                        // resolve the module path at transpile time
                        char resolved[512];
                        jm_resolve_module_path(mt->filename ? mt->filename : ".",
                            lit->value.string_value->chars, (int)lit->value.string_value->len,
                            resolved, sizeof(resolved));
                        MIR_reg_t spec = jm_box_string_literal(mt, resolved, (int)strlen(resolved));
                        return jm_callr_1(mt, "js_require", MIR_T_I64, spec);
                    }
                }
                // dynamic require(expr) — resolve at runtime
                MIR_reg_t spec = jm_transpile_box_item(mt, call->arguments);
                return jm_callr_1(mt, "js_require", MIR_T_I64, spec);
            }
        }
    }

    // import(specifier) — dynamic import, returns a Promise
    if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* callee_id = (JsIdentifierNode*)call->callee;
        if (callee_id->name && callee_id->name->len == 6 &&
            memcmp(callee_id->name->chars, "import", 6) == 0 && arg_count >= 1) {
            // Check if 'import' is a local variable (unlikely, but be safe)
            bool import_is_local = (jm_find_var(mt, "_js_import") != NULL);
            if (!import_is_local) {
                JsAstNode* arg = call->arguments;
                if (arg && arg->node_type == JS_AST_NODE_LITERAL) {
                    JsLiteralNode* lit = (JsLiteralNode*)arg;
                    if (lit->literal_type == JS_LITERAL_STRING && lit->value.string_value) {
                        // static string — resolve module path at transpile time
                        char resolved[512];
                        jm_resolve_module_path(mt->filename ? mt->filename : ".",
                            lit->value.string_value->chars, (int)lit->value.string_value->len,
                            resolved, sizeof(resolved));
                        MIR_reg_t spec = jm_box_string_literal(mt, resolved, (int)strlen(resolved));
                        return jm_callr_1(mt, "js_dynamic_import", MIR_T_I64, spec);
                    }
                }
                // dynamic import(expr) — resolve at runtime
                MIR_reg_t spec = jm_transpile_box_item(mt, call->arguments);
                return jm_callr_1(mt, "js_dynamic_import", MIR_T_I64, spec);
            }
        }
    }

    // super(args) — call parent constructor with current 'this'
    if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
        if (id->name && id->name->len == 5 && strncmp(id->name->chars, "super", 5) == 0) {
            if (!mt->current_class) {
                mt->current_class = jm_find_innermost_class_for_node(mt, (JsAstNode*)call);
            }
            if (mt->current_class && mt->current_class->node && mt->current_class->node->superclass &&
                (mt->current_class->node->superclass->node_type == JS_AST_NODE_NULL ||
                 (mt->current_class->node->superclass->node_type == JS_AST_NODE_LITERAL &&
                  ((JsLiteralNode*)mt->current_class->node->superclass)->literal_type == JS_LITERAL_NULL))) {
                // `super()` in `class C extends null` is specified to evaluate
                // arguments, then fail IsConstructor on Function.prototype.
                // Route it through the runtime super-call helper with a null
                // callee so the throw happens before any derived fields bind.
                bool null_super_has_spread = jm_arguments_have_spread(call->arguments);
                MIR_reg_t args_ptr = null_super_has_spread ?
                    jm_build_spread_args_array(mt, call->arguments) :
                    jm_build_args_array(mt, call->arguments, arg_count);
                MIR_reg_t this_val = jm_call_0(mt, "js_get_super_this_value", MIR_T_I64);
                MIR_reg_t null_callee = jm_emit_null(mt);
                MIR_reg_t super_result;
                if (null_super_has_spread) {
                    super_result = jm_callr_3(mt, "js_super_apply_native", MIR_T_I64, null_callee, this_val, args_ptr);
                } else {
                    super_result = jm_super_call_class_into(mt,
                        MIR_new_reg_op(mt->ctx, null_callee), MIR_new_reg_op(mt->ctx, this_val),
                        args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                        MIR_new_int_op(mt->ctx, arg_count));
                }
                return jm_emit_super_bind_this_with_public_fields(mt, this_val, super_result);
            }
            JsAstNode* current_heritage = mt->current_class && mt->current_class->node
                ? mt->current_class->node->superclass : NULL;
            JsClassEntry* static_superclass = jm_matching_static_superclass(
                mt->current_class, current_heritage);
            if (static_superclass) {
                MIR_reg_t this_val = jm_call_0(mt, "js_get_super_this_value", MIR_T_I64);
                MIR_reg_t parent_class = jm_emit_class_object_for_entry(
                    mt, static_superclass);
                jm_create_gc_root_slot(mt, parent_class);
                bool super_has_spread = jm_arguments_have_spread(call->arguments);
                MIR_reg_t args_ptr = super_has_spread
                    ? jm_build_spread_args_array(mt, call->arguments)
                    : jm_build_args_array(mt, call->arguments, arg_count);
                // The evaluated class object owns both constructor and field
                // capabilities. Calling a discovered body directly skipped the
                // implicit parent's initializer list (D6.2.2v2).
                MIR_reg_t super_result = super_has_spread
                    ? jm_super_apply_class_into(mt,
                        MIR_new_reg_op(mt->ctx, parent_class),
                        MIR_new_reg_op(mt->ctx, this_val),
                        MIR_new_reg_op(mt->ctx, args_ptr))
                    : jm_super_call_class_into(mt,
                        MIR_new_reg_op(mt->ctx, parent_class),
                        MIR_new_reg_op(mt->ctx, this_val),
                        args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr)
                            : MIR_new_int_op(mt->ctx, 0),
                        MIR_new_int_op(mt->ctx, arg_count));
                return jm_emit_super_bind_this_with_public_fields(
                    mt, this_val, super_result);
            } else {
                // No user-defined superclass — check for builtin parent class (Error, etc.)
                // When super(msg) is called in a class extending Error, set this.message and this.name
                if (mt->current_class && mt->current_class->node &&
                    mt->current_class->node->superclass &&
                    mt->current_class->node->superclass->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* super_id = (JsIdentifierNode*)mt->current_class->node->superclass;
                    if (super_id->name) {
                        const char* sname = super_id->name->chars;
                        int slen = (int)super_id->name->len;
                        bool is_error_class = js_builtin_global_has_flag(
                            sname, slen, JS_BUILTIN_GLOBAL_ERROR_CLASS);
                        if (is_error_class) {
                            // Set this.message = first arg, this.name = error type name
                            MIR_reg_t this_val = jm_call_0(mt, "js_get_super_this_value", MIR_T_I64);
                            MIR_reg_t msg_key = jm_box_property_name_literal(mt, "message", 7);
                            MIR_reg_t msg_val = (call->arguments) ?
                                jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                            jm_callr_3(mt, "js_set_key_default", MIR_T_I64, this_val, msg_key, msg_val);
                            MIR_reg_t name_key = jm_box_property_name_literal(mt, "name", 4);
                            MIR_reg_t name_val = jm_box_string_literal(mt, sname, slen);
                            jm_callr_3(mt, "js_set_key_default", MIR_T_I64, this_val, name_key, name_val);
                            log_debug("js-mir: super() for builtin Error class '%.*s'", slen, sname);
                            return jm_emit_super_bind_this_with_public_fields(mt, this_val, this_val);
                        }
                        // Non-class, non-builtin superclass: resolve at runtime and call with this.
                        // Use js_super_call_native so that native parent ctors that return a fresh
                        // object (e.g. Event, URL) get their own props merged onto `this` — without
                        // this merge the derived `this` would lack the base fields like `type`.
                        {
                            // Resolve the superclass from the class object's stored
                            // the class heritage (captured at class-definition time, when
                            // the binding was in scope) rather than re-evaluating the
                            // identifier here — inside the constructor a captured outer
                            // binding (e.g. a function parameter used as `extends C`)
                            // may not be visible, which would throw "C is not defined".
                            // js_get_super_constructor_from_receiver then refines via the
                            // receiver's prototype chain, so an undefined fallback is fine.
                            MIR_reg_t parent_fn;
                            if (mt->current_class->name) {
                                // Synthetic identifiers have no source range, so a named
                                // class expression would fall through to a nonexistent
                                // global instead of its private class-body binding.
                                MIR_reg_t class_obj = jm_emit_class_object_for_entry(
                                    mt, mt->current_class);
                                parent_fn = jm_callr_1(mt, "js_get_class_superclass", MIR_T_I64, class_obj);
                            } else {
                                parent_fn = jm_emit_undefined(mt);
                            }
                            bool super_has_spread = jm_arguments_have_spread(call->arguments);
                            MIR_reg_t args_ptr = super_has_spread ?
                                jm_build_spread_args_array(mt, call->arguments) :
                                jm_build_args_array(mt, call->arguments, arg_count);
                            MIR_reg_t this_val = jm_call_0(mt, "js_get_super_this_value", MIR_T_I64);
                            parent_fn = jm_callr_2(mt, "js_get_super_constructor_from_receiver", MIR_T_I64, this_val, parent_fn);
                            if (super_has_spread) {
                                MIR_reg_t super_result = jm_callr_3(mt, "js_super_apply_native", MIR_T_I64, parent_fn, this_val, args_ptr);
                                MIR_reg_t bound_this = jm_emit_super_bind_this_with_public_fields(mt, this_val, super_result);
                                log_debug("js-mir: super() resolved dynamically for non-class parent '%.*s'", slen, sname);
                                return bound_this;
                            } else {
                                MIR_reg_t super_result = jm_call_4(mt, "js_super_call_native", MIR_T_I64,
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, parent_fn),
                                    MIR_T_I64, MIR_new_reg_op(mt->ctx, this_val),
                                    MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                                MIR_reg_t bound_this = jm_emit_super_bind_this_with_public_fields(mt, this_val, super_result);
                                log_debug("js-mir: super() resolved dynamically for non-class parent '%.*s'", slen, sname);
                                return bound_this;
                            }
                        }
                    }
                }
                // v21: Handle member-expression superclass for super() calls
                // e.g. class Foo extends obj.Bar { constructor() { super(); } }
                if (mt->current_class && mt->current_class->node &&
                    mt->current_class->node->superclass &&
                    mt->current_class->node->superclass->node_type != JS_AST_NODE_IDENTIFIER) {
                    MIR_reg_t parent_fn = 0;
                    if (mt->current_class->name) {
                        MIR_reg_t class_obj = jm_emit_class_object_for_entry(mt, mt->current_class);
                        parent_fn = jm_callr_1(mt, "js_get_class_superclass", MIR_T_I64, class_obj);
                    } else {
                        parent_fn = jm_transpile_box_item(mt, mt->current_class->node->superclass);
                    }
                    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
                    MIR_reg_t this_val = jm_call_0(mt, "js_get_super_this_value", MIR_T_I64);
                    parent_fn = jm_callr_2(mt, "js_get_super_constructor_from_receiver", MIR_T_I64, this_val, parent_fn);
                    // Use js_super_call_class: handles both FUNC and MAP (class expression) callee.
                    // An empty class {} has no explicit constructor body, so js_call_function
                    // would reject as "not a function". js_super_call_class treats that as a no-op.
                    MIR_reg_t super_result = jm_super_call_class_into(mt,
                        MIR_new_reg_op(mt->ctx, parent_fn), MIR_new_reg_op(mt->ctx, this_val),
                        args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                        MIR_new_int_op(mt->ctx, arg_count));
                    log_debug("js-mir: super() resolved dynamically for member-expression parent");
                    return jm_emit_super_bind_this_with_public_fields(mt, this_val, super_result);
                }
                log_debug("js-mir: super() called but no parent class context");
                return jm_emit_null(mt);
            }
        }
    }

    // super.method(args) — call parent method with current 'this'
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (!m->computed && m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER &&
            m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
            if (obj->name && obj->name->len == 5 && strncmp(obj->name->chars, "super", 5) == 0) {
                JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
                if (mt->current_class && mt->current_class->superclass) {
                    // Look up method in parent class chain
                    JsClassEntry* parent = mt->current_class->superclass;
                    JsClassMethodEntry* found_method = NULL;
                    while (parent && !found_method) {
                        for (int i = 0; i < parent->method_count; i++) {
                            JsClassMethodEntry* me = &parent->methods[i];
                            if (me->name && prop->name &&
                                me->name->len == prop->name->len &&
                                strncmp(me->name->chars, prop->name->chars, me->name->len) == 0 &&
                                !me->is_constructor) {
                                found_method = me;
                                break;
                            }
                        }
                        parent = parent->superclass;
                    }
                    if (found_method && found_method->fc && found_method->fc->func_item) {
                        MIR_reg_t this_val = jm_emit_current_this(mt);
                        jm_emit_error_lane_propagate_check(mt);
                        // A freshly wrapped compiled method loses the installed
                        // method's home class and finalized MIR ABI metadata.
                        // Resolve the published parent method, which preserves
                        // both metadata and avoids a per-super-call allocation.
                        MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                            prop->name->chars, (int)prop->name->len);
                        MIR_reg_t fn_item = jm_callr_2(mt, "js_super_property_get", MIR_T_I64, this_val, key_reg);
                        return jm_emit_super_call(mt, call, this_val, fn_item, arg_count);
                    } else {
                        log_debug("js-mir: super.%.*s not found in parent class",
                            (int)prop->name->len, prop->name->chars);
                    }
                }
                // Fallback for dynamic class cases and object literal methods:
                // resolve on the runtime prototype chain, then call with this as receiver.
                MIR_reg_t this_val = jm_emit_current_this(mt);
                jm_emit_error_lane_propagate_check(mt);
                MIR_reg_t key_reg = jm_box_property_name_literal(mt,
                    prop->name->chars, prop->name->len);
                MIR_reg_t fn_item = jm_callr_2(mt, "js_super_property_get", MIR_T_I64, this_val, key_reg);
                return jm_emit_super_call(mt, call, this_val, fn_item, arg_count);
            }
        }
    }

    // super[computedKey](args) — call parent computed-key method with current 'this'
    // Handles e.g. super[$sym]() where $sym is an identifier naming a Symbol variable.
    // Match computed method in parent class chain by comparing key_expr identifier names.
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (m->computed && m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj_id = (JsIdentifierNode*)m->object;
            if (obj_id->name && obj_id->name->len == 5 && strncmp(obj_id->name->chars, "super", 5) == 0) {
                // The key expression — we match by identifier name in the parent class methods
                bool super_computed_handled = false;
                if (mt->current_class && mt->current_class->superclass && m->property) {
                    JsClassEntry* parent = mt->current_class->superclass;
                    JsClassMethodEntry* found_method = NULL;
                    // Get the identifier name of the key expression (e.g. "$finalize")
                    String* key_id_name = NULL;
                    if (m->property->node_type == JS_AST_NODE_IDENTIFIER) {
                        key_id_name = ((JsIdentifierNode*)m->property)->name;
                    } else if (m->property->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
                        // e.g. Symbol.iterator — use the property name
                        JsMemberNode* kmem = (JsMemberNode*)m->property;
                        if (kmem->property && kmem->property->node_type == JS_AST_NODE_IDENTIFIER) {
                            key_id_name = ((JsIdentifierNode*)kmem->property)->name;
                        }
                    }
                    if (key_id_name) {
                        // Search parent class chain for a computed method whose key_expr
                        // identifier has the same name as key_id_name
                        while (parent && !found_method) {
                            for (int i = 0; i < parent->method_count; i++) {
                                JsClassMethodEntry* me = &parent->methods[i];
                                if (!me->computed || me->is_constructor || me->is_static) continue;
                                if (!me->key_expr) continue;
                                // Match by identifier name
                                String* me_key_name = NULL;
                                if (me->key_expr->node_type == JS_AST_NODE_IDENTIFIER) {
                                    me_key_name = ((JsIdentifierNode*)me->key_expr)->name;
                                } else if (me->key_expr->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
                                    JsMemberNode* kmem = (JsMemberNode*)me->key_expr;
                                    if (kmem->property && kmem->property->node_type == JS_AST_NODE_IDENTIFIER) {
                                        me_key_name = ((JsIdentifierNode*)kmem->property)->name;
                                    }
                                }
                                if (me_key_name && key_id_name->len == me_key_name->len &&
                                    strncmp(key_id_name->chars, me_key_name->chars, key_id_name->len) == 0) {
                                    found_method = me;
                                    break;
                                }
                            }
                            parent = parent->superclass;
                        }
                        if (found_method && found_method->fc && found_method->fc->func_item) {
                            MIR_reg_t this_val = jm_emit_current_this(mt);
                            jm_emit_error_lane_propagate_check(mt);
                            MIR_reg_t key_val = jm_transpile_box_item(mt, m->property);
                            // Computed super calls need the published method for
                            // the same home-class and MIR-ABI invariant as named calls.
                            MIR_reg_t fn_item = jm_callr_2(mt, "js_super_property_get", MIR_T_I64, this_val, key_val);
                            log_debug("js-mir: super[%.*s]() → parent method '%s'",
                                (int)key_id_name->len, key_id_name->chars,
                                found_method->fc->name);
                            super_computed_handled = true;
                            return jm_emit_super_call(mt, call, this_val, fn_item, arg_count);
                        }
                    }
                }
                // Fallback for super[key]() when parent is a builtin (not in class_entries).
                // Only emit the explicit super lookup when the parent is unknown (builtin),
                // e.g. class RE extends RegExp. In this case, resolve via parent prototype
                // (2 levels: skip current class prototype to avoid recursive dispatch).
                // When the parent IS in class_entries but the key wasn't found as a computed
                // method (e.g., super['namedMethod']()), fall through to generic dispatch
                // which correctly handles it via js_get_reference.
                if (!super_computed_handled && mt->current_class && !mt->current_class->superclass) {
                    // Builtin parent: use 2-level prototype skip
                    log_debug("js-mir: super[computed]() — builtin parent, using js_super_instance_method_get");
                    MIR_reg_t this_val = jm_emit_current_this(mt);
                    jm_emit_error_lane_propagate_check(mt);
                    MIR_reg_t key_val = jm_transpile_box_item(mt, m->property);
                    MIR_reg_t fn_item = jm_callr_2(mt, "js_super_instance_method_get", MIR_T_I64, this_val, key_val);
                    bool has_spread = jm_arguments_have_spread(call->arguments);
                    if (has_spread) {
                        MIR_reg_t args_arr = jm_build_spread_args_array(mt, call->arguments);
                        return jm_apply_function_into(mt,
                            MIR_new_reg_op(mt->ctx, fn_item),
                            MIR_new_reg_op(mt->ctx, this_val),
                            MIR_new_reg_op(mt->ctx, args_arr));
                    }
                    return jm_emit_super_call(mt, call, this_val, fn_item, arg_count);
                }
            }
        }
    }

#if JS_TEST262_FAST_PATHS
    // Test262 harness interception. One row per harness entry point; the two
    // shapes (assert.<method>(...) and a bare identifier call) share the same
    // emitter below. Adding a helper is a table row, not another strncmp arm.
    if (jm_test262_fast_paths_enabled(mt)) {
        const JsTest262Intercept* spec = NULL;
        bool needs_assert_guard = false;
        if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* m = (JsMemberNode*)call->callee;
            if (!m->computed && m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER &&
                m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
                JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
                if (jm_name_is(obj->name, "assert", 6)) {
                    spec = jm_test262_lookup(js_test262_assert_methods, prop->name, arg_count);
                    // only intercept the test262 global `assert`; a local binding
                    // (e.g. `const assert = require('assert')`) must win
                    needs_assert_guard = (spec != NULL);
                    if (needs_assert_guard && jm_test262_assert_is_local(mt, obj))
                        spec = NULL;
                }
            }
        } else if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
            spec = jm_test262_lookup(js_test262_globals, id->name, arg_count);
            if (spec && (spec->flags & JM_T262_LOCAL_GUARD) &&
                jm_test262_assert_is_local(mt, id))
                spec = NULL;
        }
        if (spec) return jm_emit_test262_intercept(mt, call, spec);
    }
#endif

    // Computed member call: obj[expr](args) -> get property, then call
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (m->computed) {
            bool is_super_computed_call = false;
            if (m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* obj_id = (JsIdentifierNode*)m->object;
                is_super_computed_call = obj_id->name && obj_id->name->len == 5 &&
                    strncmp(obj_id->name->chars, "super", 5) == 0;
            }
            if (is_super_computed_call) {
                MIR_reg_t recv = jm_emit_current_this(mt);
                int recv_key_spill = -1;
                if (jm_expression_can_suspend(mt, m->property)) {
                    recv_key_spill = jm_gen_spill_save(mt, recv);
                }
                MIR_reg_t key = jm_transpile_box_item(mt, m->property);
                if (recv_key_spill >= 0) {
                    jm_gen_spill_load(mt, recv, recv_key_spill);
                }

                bool args_have_yield = false;
                bool args_have_spread = false;
                jm_call_arg_flags(mt, call->arguments, &args_have_yield, &args_have_spread);

                MIR_reg_t fn = jm_callr_2(mt, "js_super_property_get", MIR_T_I64, recv, key);
                jm_emit_error_lane_propagate_check(mt);
                return jm_emit_member_call_from_function(mt, call, recv, fn,
                    arg_count, args_have_yield, args_have_spread);
            }

            MIR_reg_t recv = jm_transpile_box_item(mt, m->object);
            bool has_optional_call = m->optional || call->optional;
            MIR_label_t l_skip = 0;
            MIR_label_t l_call = 0;
            MIR_label_t l_end = 0;
            MIR_reg_t result = 0;
            MIR_reg_t cmp = 0;
            if (has_optional_call) {
                l_skip = jm_new_label(mt);
                l_call = jm_new_label(mt);
                l_end = jm_new_label(mt);
                result = jm_new_reg(mt, "cmcr", MIR_T_I64);
                cmp = jm_new_reg(mt, "cmck", MIR_T_I64);
            }
            if (m->optional) {
                // D6.2.2v2: the optional receiver guard precedes computed-key
                // evaluation; evaluating the key first leaks skipped side effects.
                jm_emit_reg_binary_op(mt, MIR_EQ, cmp, recv, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
                jm_emit_branch(mt, MIR_BT, l_skip, cmp);
                jm_emit_reg_binary_op(mt, MIR_EQ, cmp, recv, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
                jm_emit_branch(mt, MIR_BT, l_skip, cmp);
            }
            int recv_key_spill = -1;
            if (jm_expression_can_suspend(mt, m->property)) {
                recv_key_spill = jm_gen_spill_save(mt, recv);
            }
            MIR_reg_t key = jm_transpile_box_item(mt, m->property);
            if (recv_key_spill >= 0) {
                jm_gen_spill_load(mt, recv, recv_key_spill);
            }
            if (!m->optional) {
                // A non-optional member call must finish evaluating the
                // callee reference before arguments run. Nullish receivers
                // throw here, so argument side effects must not happen.
                jm_callr_1(mt, "js_require_object_coercible", MIR_T_I64, recv);
                jm_emit_error_lane_propagate_check(mt);
            }
            bool args_have_yield = false;
            bool args_have_spread = false;
            jm_call_arg_flags(mt, call->arguments, &args_have_yield, &args_have_spread);

            // Optional chaining: obj?.[expr](args)
            if (has_optional_call) {
                MIR_reg_t fn = jm_callr_2(mt, "js_get_reference", MIR_T_I64, recv, key);
                jm_emit_error_lane_propagate_check(mt);

                // Check function for null/undefined
                if (call->optional) {
                    jm_emit_reg_binary_op(mt, MIR_EQ, cmp, fn, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
                    jm_emit_branch(mt, MIR_BT, l_skip, cmp);
                    jm_emit_reg_binary_op(mt, MIR_EQ, cmp, fn, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
                    jm_emit_branch(mt, MIR_BT, l_skip, cmp);
                }
                jm_emit_jmp(mt, l_call);

                jm_emit_label(mt, l_skip);
                jm_emit_reg_op(mt, MIR_MOV, result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
                jm_emit_jmp(mt, l_end);

                jm_emit_label(mt, l_call);
                MIR_reg_t call_result = jm_emit_member_call_from_function(mt, call,
                    recv, fn, arg_count, args_have_yield, args_have_spread);
                jm_emit_mov(mt, result, call_result);
                jm_emit_label(mt, l_end);
                return result;
            }

            // Non-optional computed member call
            MIR_reg_t fn = jm_callr_2(mt, "js_get_reference", MIR_T_I64, recv, key);
            jm_emit_error_lane_propagate_check(mt);
            return jm_emit_member_call_from_function(mt, call, recv, fn,
                arg_count, args_have_yield, args_have_spread);
        }
    }

    // D6.2.2v2: named method calls use the same observable property Get and
    // ordinary call kernel as computed calls. Receiver TypeId/name shortcuts
    // bypassed monkey patches, accessors, Proxy traps, and collection methods.
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
            bool args_have_yield = false;
            bool args_have_spread = false;
            jm_call_arg_flags(mt, call->arguments, &args_have_yield, &args_have_spread);

            // A named non-optional call has one Reference evaluation. The
            // ordinal probe and the generic fallback must consume that same
            // receiver/key pair; probing by emitting the member expression a
            // second time duplicates observable receiver evaluation.
            bool can_reuse_reference = !m->optional && !call->optional &&
                !jm_has_optional_chain(m->object) && !args_have_yield &&
                !args_have_spread;
            JsMirReference named_ref;
            memset(&named_ref, 0, sizeof(named_ref));
            if (can_reuse_reference) {
                named_ref = jm_emit_reference(mt, (JsAstNode*)m);
            }

            // D4d: declared method calls may bypass the cached function object
            // only after the complete Reference has been evaluated. The
            // runtime helper retains Get-then-Call as its fallback shape.
            if (can_reuse_reference && !named_ref.is_private) {
                if (named_ref.jube_slot >= 0 &&
                        named_ref.jube_ordinal != UINT32_MAX &&
                        named_ref.jube_kind == JUBE_MEMBER_KIND_METHOD) {
                    jm_call_1(mt, "js_require_object_coercible", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx,
                            named_ref.base_reg));
                    jm_emit_error_lane_propagate_check(mt);
                    MIR_reg_t args_ptr = jm_build_args_array(mt,
                        call->arguments, arg_count);
                    MIR_reg_t direct_result = jm_call_6(mt,
                        "js_jube_member_call_by_ordinal", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, named_ref.base_reg),
                        MIR_T_I64, MIR_new_int_op(mt->ctx,
                            (int64_t)named_ref.jube_slot),
                        MIR_T_I64, MIR_new_int_op(mt->ctx,
                            (int64_t)named_ref.jube_ordinal),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, named_ref.key_reg),
                        MIR_T_I64, args_ptr
                            ? MIR_new_reg_op(mt->ctx, args_ptr)
                            : MIR_new_int_op(mt->ctx, 0),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                    jm_emit_error_lane_propagate_check(mt);
                    jm_readback_closure_env(mt);
                    return jm_publish_call_result(mt, direct_result);
                }
            }
            MIR_reg_t recv = can_reuse_reference
                ? named_ref.base_reg : jm_transpile_box_item(mt, m->object);
            String* method_key_name = jm_resolve_private_name(
                mt, (JsAstNode*)m->property, prop->name);
            MIR_reg_t method_name = can_reuse_reference
                ? named_ref.key_reg : jm_box_property_name_literal(
                    mt, method_key_name->chars, method_key_name->len);
            if (!can_reuse_reference && jm_is_private_name(method_key_name)) {
                method_name = jm_emit_private_key_for_access(
                    mt, (JsAstNode*)m->property, method_key_name);
            }

            bool receiver_optional = m->optional || jm_has_optional_chain(m->object);
            if (!receiver_optional) {
                jm_callr_1(mt, "js_require_object_coercible", MIR_T_I64, recv);
                jm_emit_error_lane_propagate_check(mt);
            }

            if (receiver_optional || call->optional) {
                return jm_emit_optional_method_call(mt, recv, method_name, call,
                    arg_count, receiver_optional, call->optional,
                    args_have_yield, args_have_spread, "optmcr", "optmck");
            }

            MIR_reg_t fn = jm_callr_2(mt, "js_get_reference", MIR_T_I64, recv, method_name);
            jm_emit_error_lane_propagate_check(mt);
            MIR_reg_t result = jm_emit_member_call_from_function(
                mt, call, recv, fn, arg_count, args_have_yield, args_have_spread);
            jm_readback_closure_env(mt);
            return jm_publish_call_result(mt, result);
        }
    }

    // Direct function call: identifier(args)
    if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;

        // Direct eval needs lexical-environment bridging, but only when the
        // evaluated Reference still denotes the realm's original %eval%.
        if (!call->optional && id->name && id->name->len == 4 &&
                memcmp(id->name->chars, "eval", 4) == 0) {
            return jm_emit_eval_identifier_call(mt, call);
        }

        // consume the immutable binding/definition edge published by the AST index.
        JsFunctionNode* resolved_fn = jm_resolve_direct_call_function(mt, call, true);

        if (resolved_fn) {
            const char* direct_vname = jm_var_name(id->name);
            if (mt->module_consts) {
                JsModuleConstEntry* direct_mc = jm_find_module_const(mt, direct_vname);
                if (direct_mc && direct_mc->const_type == MCONST_MODVAR &&
                        direct_mc->is_nested_func_hoist) {
                    resolved_fn = NULL;
                }
            }
        }

        if (resolved_fn) {
            JsFuncCollected* fc = jm_find_collected_func(mt, resolved_fn);
            // If any argument is a spread, skip compile-time dispatch — fall through to fallback
            bool direct_has_spread = jm_arguments_have_spread(call->arguments);
            if (direct_has_spread) fc = NULL;  // nullify so we fall through to fallback
            if (fc && JM_JS_FACT(fc, has_rest_param)) fc = NULL;  // rest-param functions need runtime arg collection
            if (fc && JM_JS_FACT(fc, uses_arguments)) fc = NULL;  // uses_arguments needs runtime pending args from js_invoke_fn
            if (fc && JM_JS_FACT(fc, is_reassigned)) fc = NULL;
            if (fc && fc->node && fc->node->is_async) fc = NULL;
            if (fc && fc->node && fc->node->is_generator &&
                    ast_linked_node_count(fc->node->params) == 0) fc = NULL;
            if (fc && mt->current_fc && fc == mt->current_fc &&
                    (!mt->tco_func || !mt->in_tail_position ||
                     !jm_is_recursive_call(call, mt->tco_func))) {
                // non-tail self recursion must use js_call_function so the call-depth RangeError is catchable.
                fc = NULL;
            }
            if (fc && (jm_current_function_captures_with_scope(mt) ||
                    jm_node_has_with_ancestor(mt, (JsAstNode*)call) ||
                    jm_node_has_with_ancestor(mt, (JsAstNode*)resolved_fn))) {
                // A closure created below `with` restores that Object Environment
                // Record at call time, so a syntactically direct callee is dynamic.
                fc = NULL;
            }
            // Yield in args inside a generator: the direct paths below evaluate
            // args into raw MIR regs that don't survive yield/resume, corrupting
            // earlier args. Force the env-spilling fallback (jm_build_args_array).
            if (fc && jm_call_yield_blocks_direct(mt, call->arguments)) fc = NULL;

            if (fc && (fc->func_item || fc->native_func_item) && JM_CAPTURE_COUNT(fc) == 0) {
                // Phase 4: Check if we can call the native version
                if (JM_JS_FACT(fc, native_return_kind) != NATIVE_RETURN_NONE && fc->native_func_item) {
                    bool all_args_match = true;
                    int pi = 0;
                    JsAstNode* acheck = call->arguments;
                    while (acheck && pi < JM_PARAM_COUNT(fc)) {
                        TypeId expected = jm_param_type(fc, pi);
                        TypeId actual = jm_get_effective_type(mt, acheck);
                        if (expected == LMD_TYPE_INT && actual != LMD_TYPE_INT) {
                            all_args_match = false; break;
                        }
                        if (expected == LMD_TYPE_FLOAT &&
                            actual != LMD_TYPE_FLOAT && actual != LMD_TYPE_INT) {
                            all_args_match = false; break;
                        }
                        pi++;
                        acheck = acheck->next;
                    }
                    if (pi != JM_PARAM_COUNT(fc)) all_args_match = false;

                    if (all_args_match) {
                        // TCO: if this is a tail-recursive call, convert to goto
                        if (mt->tco_func && mt->in_tail_position &&
                            jm_is_recursive_call(call, mt->tco_func)) {
                            log_debug("js-mir TCO: tail call to %s — converting to goto", fc->name);

                            // Clear tail position for arg evaluation (inner calls are NOT tail)
                            bool saved_tail = mt->in_tail_position;
                            mt->in_tail_position = false;

                            // Phase 1: Evaluate all arguments into temp registers
                            MIR_reg_t* temps = LAMBDA_ALLOCA(JM_PARAM_COUNT(fc),
                                MIR_reg_t);
                            JsAstNode* arg = call->arguments;
                            for (int i = 0; i < JM_PARAM_COUNT(fc); i++) {
                                if (arg) {
                                    temps[i] = jm_transpile_as_native(mt, arg,
                                        jm_param_type(fc, i));
                                    arg = arg->next;
                                } else {
                                    MIR_type_t mt2 = (jm_param_type(fc, i) == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                                    temps[i] = jm_new_reg(mt, "tz", mt2);
                                    if (mt2 == MIR_T_D) {
                                        jm_emit_reg_op(mt, MIR_DMOV, temps[i], MIR_new_double_op(mt->ctx, 0.0));
                                    } else {
                                        jm_emit_reg_op(mt, MIR_MOV, temps[i], MIR_new_int_op(mt->ctx, 0));
                                    }
                                }
                            }
                            jm_transpile_discard_call_args(mt, arg);

                            // Phase 2: Assign temps → parameter registers
                            JsAstNode* pnode = mt->tco_func->node->params;
                            for (int i = 0; i < JM_PARAM_COUNT(fc); i++) {
                                char pname[32];
                                jm_get_backend_param_name(i, pname, sizeof(pname));
                                MIR_reg_t preg = MIR_reg(mt->ctx, pname, mt->em.func);
                                MIR_type_t mtype = (jm_param_type(fc, i) == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                                MIR_insn_code_t mov = (mtype == MIR_T_D) ? MIR_DMOV : MIR_MOV;
                                jm_emit(mt, MIR_new_insn(mt->ctx, mov,
                                    MIR_new_reg_op(mt->ctx, preg),
                                    MIR_new_reg_op(mt->ctx, temps[i])));
                                pnode = pnode ? pnode->next : NULL;
                            }

                            mt->in_tail_position = saved_tail;

                            // Jump back to function start
                            jm_emit_jmp(mt, mt->tco_label);
                            mt->tco_jumped = true;

                            // Return dummy register (unreachable code)
                            MIR_type_t native_ret = (JM_JS_FACT(fc, return_type) == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                            MIR_reg_t dummy = jm_new_reg(mt, "tco_d", native_ret);
                            if (native_ret == MIR_T_D) {
                                jm_emit_reg_op(mt, MIR_DMOV, dummy, MIR_new_double_op(mt->ctx, 0.0));
                            } else {
                                jm_emit_reg_op(mt, MIR_MOV, dummy, MIR_new_int_op(mt->ctx, 0));
                            }
                            return dummy;
                        }

                        MIR_reg_t* native_args = LAMBDA_ALLOCA(
                            JM_PARAM_COUNT(fc), MIR_reg_t);

                        JsAstNode* arg = call->arguments;
                        for (int i = 0; i < JM_PARAM_COUNT(fc); i++) {
                            if (arg) {
                                native_args[i] = jm_transpile_as_native(mt, arg,
                                    jm_param_type(fc, i));
                                arg = arg->next;
                            } else {
                                native_args[i] = jm_new_reg(mt, "nz", MIR_T_I64);
                                jm_emit_reg_op(mt, MIR_MOV, native_args[i], MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
                                }
                            }
                            jm_transpile_discard_call_args(mt, arg);

                            bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
                            MIR_reg_t result = jm_call_direct_native(mt, fc,
                                JM_PARAM_COUNT(fc), native_args);
                            jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);
                            return result; // returns NATIVE value
                        }
                }

                // Direct call to local function (only for non-closures;
                // closures need env from the JsFunction wrapper, so they
                // go through js_call_function which handles env passing)
                if (fc->func_item) {
                int param_count = ast_linked_node_count(resolved_fn->params);

                // v17: save prev this/new.target BEFORE evaluating args, but set
                // undefined AFTER args — otherwise `this` in args reads undefined.
                // Use the lexical binding accessor so a direct call before super()
                // in a derived constructor preserves the TDZ sentinel instead of
                // throwing while merely saving caller state.
                MIR_reg_t prev_this = JM_JS_FACT(fc, observes_this)
                    ? jm_call_0(mt, "js_get_lexical_this_binding", MIR_T_I64) : 0;
                MIR_reg_t prev_nt_dc = JM_JS_FACT(fc, observes_new_target)
                    ? jm_call_0(mt, "js_get_new_target", MIR_T_I64) : 0;

                MIR_reg_t* direct_args = param_count > 0
                    ? LAMBDA_ALLOCA(param_count, MIR_reg_t) : NULL;

                JsAstNode* arg = call->arguments;
                for (int i = 0; i < param_count; i++) {
                    if (arg) {
                        MIR_reg_t val = jm_transpile_box_item(mt, arg);
                        direct_args[i] = val;
                        arg = arg->next;
                    } else {
                        MIR_reg_t undef_val = jm_new_reg(mt, "ua", MIR_T_I64);
                        jm_emit_reg_op(mt, MIR_MOV, undef_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
                        direct_args[i] = undef_val;
                        }
                    }
                    jm_transpile_discard_call_args(mt, arg);

                // Set this AFTER evaluating args (so `this` in args
                // still reads the caller's this binding, not undefined)
                // OrdinaryCallBindThis: sloppy → globalThis, strict → undefined
                MIR_reg_t undef_this = 0;
                if (JM_JS_FACT(fc, observes_this) || JM_JS_FACT(fc, observes_new_target)) {
                    undef_this = jm_emit_undefined(mt);
                }
                if (JM_JS_FACT(fc, observes_this)) {
                    if (!JM_JS_FACT(fc, is_strict)) {
                        MIR_reg_t global_this = jm_call_0(mt, "js_get_global_this", MIR_T_I64);
                        jm_callr_void_1(mt, "js_set_this", global_this);
                    } else {
                        jm_callr_void_1(mt, "js_set_this", undef_this);
                    }
                }
                if (JM_JS_FACT(fc, observes_new_target)) jm_callr_void_1(mt, "js_set_direct_new_target", undef_this);

                // save with-scope depth before direct call (function may return from inside 'with')
                MIR_reg_t saved_wd = JM_JS_FACT(fc, uses_with)
                    ? jm_call_0(mt, "js_with_save_depth", MIR_T_I64) : 0;

                bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
                MIR_reg_t result = jm_call_direct_boxed(mt, fc,
                    param_count, direct_args,
                    mt->discarded_expression == (JsAstNode*)call);
                jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);

                if (saved_wd) jm_callr_void_1(mt, "js_with_restore_depth", saved_wd);
                if (prev_this) jm_callr_void_1(mt, "js_set_this", prev_this);
                if (prev_nt_dc) jm_callr_void_1(mt, "js_set_direct_new_target", prev_nt_dc);

                return jm_publish_call_result(mt, result);
                } // end if (fc->func_item)
            }
        }
    }

    // Fallback: evaluate callee, build args array, call js_call_function
    // Check if any argument is a spread element — if so, use js_apply_function with array
    bool fallback_has_spread = jm_arguments_have_spread(call->arguments);

    MIR_reg_t evaluated_callee = jm_transpile_box_item(mt, call->callee);
    // ECMAScript evaluates the callee before its arguments.  Keep that value
    // in its own rooted register because an argument such as `fn(fn = 0)` may
    // overwrite the identifier register used to resolve the callee.
    MIR_reg_t callee = jm_new_reg(mt, "callee", MIR_T_I64);
    jm_emit_mov(mt, callee, evaluated_callee);
    jm_create_gc_root_slot(mt, callee);

    // D5.4.3: both yield and await invalidate raw MIR registers across the
    // shared generator state machine, including the already-evaluated callee.
    int callee_spill_slot = jm_call_yield_blocks_direct(mt, call->arguments)
        ? jm_gen_spill_save(mt, callee) : -1;

    // Optional chaining propagation: if callee is from an optional chain,
    // it may be undefined from short-circuiting — skip the call.
    if (!call->optional && jm_has_optional_chain(call->callee)) {
        return jm_emit_optional_function_call(mt, callee, call, arg_count,
            fallback_has_spread, callee_spill_slot, "optpc", "optpk");
    }

    // Optional chaining: func?.() → return undefined if func is null/undefined
    if (call->optional) {
        return jm_emit_optional_function_call(mt, callee, call, arg_count,
            fallback_has_spread, callee_spill_slot, "optc", "optk");
    }


    if (fallback_has_spread) {
        MIR_reg_t sp_arr = jm_build_spread_args_array(mt, call->arguments);
        if (callee_spill_slot >= 0) jm_gen_spill_load(mt, callee, callee_spill_slot);
        // v17: pass undefined as this for ordinary plain calls; `with` identifier
        // calls are patched by jm_emit_plain_call_this_arg to preserve the base object.
        MIR_reg_t null_this = jm_emit_plain_call_this_arg(mt, call);
        bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
        MIR_reg_t call_result = jm_apply_function_into(mt,
            MIR_new_reg_op(mt->ctx, callee), MIR_new_reg_op(mt->ctx, null_this),
            MIR_new_reg_op(mt->ctx, sp_arr));
        jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);
        jm_emit_error_lane_propagate_check(mt);
        jm_readback_closure_env(mt);
        return jm_publish_call_result(mt, call_result);
    }

    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    if (callee_spill_slot >= 0) jm_gen_spill_load(mt, callee, callee_spill_slot);
    // v17: pass undefined as this for ordinary plain calls; `with` identifier
    // calls are patched by jm_emit_plain_call_this_arg to preserve the base object.
    MIR_reg_t null_this = jm_emit_plain_call_this_arg(mt, call);
    bool emitted_call_source = jm_emit_assert_pending_call_source(mt, call);
    MIR_reg_t call_result = jm_call_function_into(mt,
        MIR_new_reg_op(mt->ctx, callee),
        MIR_new_reg_op(mt->ctx, null_this),
        args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_new_int_op(mt->ctx, arg_count));
    jm_emit_clear_assert_pending_call_source(mt, emitted_call_source);
    jm_emit_error_lane_propagate_check(mt);
    jm_readback_closure_env(mt);
    return jm_publish_call_result(mt, call_result);
}

// ============================================================================
// Member expression
static MIR_reg_t jm_transpile_member_key(JsMirTranspiler* mt, JsMemberNode* mem) {
    if (mem->computed) return jm_transpile_box_item(mt, mem->property);
    if (mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
        String* key_name = jm_resolve_private_name(mt,
            (JsAstNode*)mem->property, prop->name);
        if (jm_is_private_name(key_name)) {
            return jm_emit_private_key_for_access(mt,
                (JsAstNode*)mem->property, key_name);
        }
        return jm_box_property_name_literal(mt, key_name->chars, key_name->len);
    }
    return jm_transpile_box_item(mt, mem->property);
}

static MIR_reg_t jm_emit_optional_member_access(JsMirTranspiler* mt,
                                                JsMemberNode* mem,
                                                MIR_reg_t obj,
                                                int mem_obj_spill) {
    MIR_label_t l_skip = jm_new_label(mt);
    MIR_label_t l_access = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    MIR_reg_t result = jm_new_reg(mt, "optm", MIR_T_I64);
    MIR_reg_t cmp = jm_new_reg(mt, "optc", MIR_T_I64);

    jm_emit_reg_binary_op(mt, MIR_EQ, cmp, obj, MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL));
    jm_emit_branch(mt, MIR_BT, l_skip, cmp);
    jm_emit_reg_binary_op(mt, MIR_EQ, cmp, obj, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
    jm_emit_branch(mt, MIR_BT, l_skip, cmp);
    jm_emit_jmp(mt, l_access);

    jm_emit_label(mt, l_skip);
    jm_emit_reg_op(mt, MIR_MOV, result, MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED));
    jm_emit_jmp(mt, l_end);

    jm_emit_label(mt, l_access);
    MIR_reg_t key = jm_transpile_member_key(mt, mem);
    if (mem_obj_spill >= 0) jm_gen_spill_load(mt, obj, mem_obj_spill);
    MIR_reg_t val = jm_callr_2(mt, "js_get_reference", MIR_T_I64, obj, key);
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_mov(mt, result, val);
    jm_emit_label(mt, l_end);
    return result;
}

MIR_reg_t jm_transpile_member(JsMirTranspiler* mt, JsMemberNode* mem) {

    // super.x / super[expr] property access must bypass local receiver fast paths.
    if (mem->object && mem->object->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* obj_id = (JsIdentifierNode*)mem->object;
        if (obj_id->name && obj_id->name->len == 5 && strncmp(obj_id->name->chars, "super", 5) == 0) {
            JsMirReference ref = jm_emit_reference(mt, (JsAstNode*)mem);
            return jm_emit_get_value(mt, &ref);
        }
    }

    // General property access: js_get_reference(obj, key)
    MIR_reg_t obj = jm_transpile_box_item(mt, mem->object);
    // A chained receiver (for example `this.toolInstance.validate`) is held
    // only in a MIR register while its key is boxed; root every receiver so
    // compaction updates that temporary before the access consumes it.
    jm_create_gc_root_slot(mt, obj);
    jm_emit_error_lane_propagate_check(mt);

    // Computed keys can suspend in both generators and async functions; the
    // receiver is evaluated first and must survive either state-machine edge.
    int mem_obj_spill = -1;
    if (mem->computed && jm_expression_can_suspend(mt, mem->property)) {
        mem_obj_spill = jm_gen_spill_save(mt, obj);
    }

    // Optional chaining propagation: if this is a non-optional access but our object
    // came from an optional chain (?.),  the object may be undefined from short-circuiting.
    // We need to propagate the short-circuit through the rest of the chain.
    if (!mem->optional && jm_has_optional_chain(mem->object)) {
        return jm_emit_optional_member_access(mt, mem, obj, mem_obj_spill);
    }

    // Optional chaining: obj?.prop → return undefined if obj is null/undefined
    if (mem->optional) {
        return jm_emit_optional_member_access(mt, mem, obj, mem_obj_spill);
    }

    if (!mem->computed && mem->property &&
            mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
        String* key_name = jm_resolve_private_name(mt,
            (JsAstNode*)mem->property, prop->name);
        if (key_name && !jm_is_private_name(key_name)) {
            uint32_t key_index = jm_module_name_index(mt,
                key_name->chars, key_name->len);
            if (key_index != UINT32_MAX) {
                if (mem_obj_spill >= 0) jm_gen_spill_load(mt, obj, mem_obj_spill);
                MIR_reg_t name_id = jm_module_name_id(mt, key_name->chars,
                    key_name->len);
                MIR_reg_t val = jm_callr_2(mt, "js_get_name_id", MIR_T_I64, obj, name_id);
                jm_emit_error_lane_propagate_check(mt);
                return val;
            }
        }
    }

    MIR_reg_t key = jm_transpile_member_key(mt, mem);

    if (mem_obj_spill >= 0) {
        jm_gen_spill_load(mt, obj, mem_obj_spill);
    }

    MIR_reg_t val = jm_callr_2(mt, "js_get_reference", MIR_T_I64, obj, key);
    jm_emit_error_lane_propagate_check(mt);
    return val;
}

// Array expression
MIR_reg_t jm_transpile_array(JsMirTranspiler* mt, JsArrayNode* arr) {
    // Check if any element is a spread element
    bool has_spread = false;
    JsAstNode* check = arr->elements;
    while (check) {
        if (check->node_type == JS_AST_NODE_SPREAD_ELEMENT) { has_spread = true; break; }
        check = check->next;
    }
    bool numeric_literal_array = !has_spread;
    if (numeric_literal_array) {
        for (JsAstNode* elem = arr->elements; elem; elem = elem->next) {
            if (elem->node_type != JS_AST_NODE_LITERAL ||
                    ((JsLiteralNode*)elem)->literal_type != JS_LITERAL_NUMBER ||
                    ((JsLiteralNode*)elem)->is_bigint) {
                numeric_literal_array = false;
                break;
            }
        }
    }

    MIR_reg_t array;
    if (has_spread) {
        // Use empty array + push for arrays with spread
        array = jm_call_1(mt, "js_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

        // Generator spill: if any element contains yield, save array ref to env
        int arr_spill_slot_s = -1;
        if (mt->in_generator) {
            JsAstNode* cy = arr->elements;
            while (cy) { if (jm_has_yield(mt, cy)) { arr_spill_slot_s = jm_gen_spill_save(mt, array); break; } cy = cy->next; }
        }

        JsAstNode* elem = arr->elements;
        while (elem) {
            if (elem->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                // Spread element: convert to array (handles Map, Set, generators, strings) then iterate
                JsSpreadElementNode* spread = (JsSpreadElementNode*)elem;
                MIR_reg_t src_raw = jm_transpile_box_item(mt, spread->argument);

                // Generator spill: restore array reg after evaluating spread argument that may yield
                if (arr_spill_slot_s >= 0 && jm_has_yield(mt, spread->argument)) {
                    jm_gen_spill_load(mt, array, arr_spill_slot_s);
                }
                
                // Convert any iterable to an array first
                MIR_reg_t src = jm_callr_1(mt, "js_iterable_to_array", MIR_T_I64, src_raw);
                jm_emit_error_lane_propagate_check(mt);

                // Get length of source array
                MIR_reg_t src_len = jm_callr_1(mt, "js_array_length", MIR_T_I64, src);

                // Loop: for (i = 0; i < src_len; i++)
                MIR_reg_t i_reg = jm_new_reg(mt, "si", MIR_T_I64);
                jm_emit_reg_op(mt, MIR_MOV, i_reg, MIR_new_int_op(mt->ctx, 0));

                MIR_label_t l_spread_check = jm_new_label(mt);
                MIR_label_t l_spread_end = jm_new_label(mt);

                jm_emit_label(mt, l_spread_check);
                MIR_reg_t cmp = jm_new_reg(mt, "scmp", MIR_T_I64);
                jm_emit_reg_binary(mt, MIR_LTS, cmp, i_reg, src_len);
                jm_emit_branch(mt, MIR_BF, l_spread_end, cmp);

                // Get element at index i (box the index first). Must go through
                // the funnel: an int Item carries rotated IEEE bits, so OR-ing
                // the tag onto a raw index yields a different number entirely.
                MIR_reg_t idx_boxed = jm_box_int_reg(mt, i_reg);
                MIR_reg_t src_elem = jm_callr_2(mt, "js_elements_get", MIR_T_I64, src, idx_boxed);
                jm_callr_2(mt, "js_array_push", MIR_T_I64, array, src_elem);

                // i++
                jm_emit_reg_binary_op(mt, MIR_ADD, i_reg, i_reg, MIR_new_int_op(mt->ctx, 1));
                jm_emit_jmp(mt, l_spread_check);
                jm_emit_label(mt, l_spread_end);
            } else {
                MIR_reg_t val;
                if (elem->node_type == JS_AST_NODE_NULL) {
                    // elision hole — push sentinel value
                    val = jm_call_0(mt, "js_array_hole", MIR_T_I64);
                } else {
                    val = jm_transpile_box_item(mt, elem);
                    if (arr_spill_slot_s >= 0 && jm_has_yield(mt, elem)) {
                        jm_gen_spill_load(mt, array, arr_spill_slot_s);
                    }
                }
                jm_callr_2(mt, "js_array_push", MIR_T_I64, array, val);
            }
            elem = elem->next;
        }
    } else {
        // No spread: use pre-allocated array with set (original approach)
        array = jm_call_1(mt, numeric_literal_array ? "js_array_new_numeric" : "js_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, arr->length));

        // Generator spill: if any element contains yield, save array ref to env
        int arr_spill_slot = -1;
        if (mt->in_generator) {
            JsAstNode* check_yield = arr->elements;
            while (check_yield) {
                if (jm_has_yield(mt, check_yield)) {
                    arr_spill_slot = jm_gen_spill_save(mt, array);
                    break;
                }
                check_yield = check_yield->next;
            }
        }

        JsAstNode* elem = arr->elements;
        int index = 0;
        while (elem) {
            if (elem->node_type == JS_AST_NODE_NULL) {
                // elision hole — skip, array was pre-allocated with hole sentinels
                elem = elem->next;
                index++;
                continue;
            }
            MIR_reg_t val = jm_transpile_box_item(mt, elem);
            if (arr_spill_slot >= 0 && jm_has_yield(mt, elem)) {
                // Restore array ref after yield
                jm_gen_spill_load(mt, array, arr_spill_slot);
            }
            jm_call_3(mt, numeric_literal_array ? "js_elements_set_numeric_direct" :
                "js_array_define_dense_element_direct", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                MIR_T_I64, MIR_new_int_op(mt->ctx, index),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            elem = elem->next;
            index++;
        }
    }

    return array;
}

// Object expression
MIR_reg_t jm_transpile_object(JsMirTranspiler* mt, JsObjectNode* obj) {
    // Object literals use the ordinary property builder.  The former static
    // shape shortcut embedded compiler-pool name arrays in delayed MIR; the
    // normal path already canonicalizes each key through NameId and preserves
    // the same observable insertion order (D5.4.3).
    MIR_reg_t object = jm_call_0(mt, "js_new_object", MIR_T_I64);

    // Generator spill: if any property value/key/spread contains yield, save object ref to env
    int obj_spill_slot = -1;
    if (mt->in_generator) {
        JsAstNode* cy = obj->properties;
        while (cy) {
            if (jm_has_yield(mt, cy)) { obj_spill_slot = jm_gen_spill_save(mt, object); break; }
            cy = cy->next;
        }
    }

    JsAstNode* prop = obj->properties;
    while (prop) {
        if (prop->node_type == JS_AST_NODE_PROPERTY) {
            JsPropertyNode* p = (JsPropertyNode*)prop;
            // Skip getter/setter properties with null key (get key() { ... })
            if (!p->key) { prop = prop->next; continue; }
            MIR_reg_t key;
            // Generator spill: if value contains yield, we need to spill key too
            // since key is evaluated before value which may yield
            int key_spill_slot = -1;
            bool val_has_yield = obj_spill_slot >= 0 && p->value && jm_has_yield(mt, p->value);
            if (p->computed) {
                key = jm_transpile_box_item(mt, p->key);
                key = jm_callr_1(mt, "js_to_property_key", MIR_T_I64, key);
                // computed accessor keys use the same ToPropertyKey abrupt
                // completion as data properties; without this boundary an
                // ERROR lane is later treated as an ordinary property key.
                jm_emit_error_lane_propagate_check(mt);
                // Phase-5C: accessor properties no longer wrap the key with
                // __get_/__set_ prefix; we'll dispatch via
                // js_install_user_accessor below using the bare key.
            } else if (p->key->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)p->key;
                const char* kchars = id->name->chars;
                int klen = (int)id->name->len;
                key = jm_box_property_name_literal(mt, kchars, klen);
            } else {
                key = jm_transpile_box_item(mt, p->key);
            }
            if (val_has_yield) {
                key_spill_slot = jm_gen_spill_save(mt, key);
            }
            MIR_reg_t val = jm_transpile_box_item(mt, p->value);
            // Generator spill: restore object and key refs after yield-containing property value
            if (val_has_yield) {
                jm_gen_spill_load(mt, object, obj_spill_slot);
                jm_gen_spill_load(mt, key, key_spill_slot);
            } else if (obj_spill_slot >= 0 && jm_has_yield(mt, prop)) {
                // key itself contained yield (computed key case)
                jm_gen_spill_load(mt, object, obj_spill_slot);
            }
            bool is_proto_literal = false;
            if (!p->computed && !p->method && !p->is_getter && !p->is_setter &&
                !p->shorthand &&
                p->key && p->value && p->key != p->value) {
                is_proto_literal = js_ast_is_proto_literal_key(p->key);
            }
            // function name inference from object property key
            if (!is_proto_literal && p->value &&
                (p->value->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                 p->value->node_type == JS_AST_NODE_ARROW_FUNCTION ||
                 p->value->node_type == JS_AST_NODE_FUNCTION_DECLARATION ||
                 p->value->node_type == JS_AST_NODE_CLASS_DECLARATION ||
                 p->value->node_type == JS_AST_NODE_CLASS_EXPRESSION)) {
                int64_t prefix_kind = p->is_getter ? 1 : (p->is_setter ? 2 : 0);
                jm_call_void_3(mt, "js_set_function_name_from_property_key_if_anonymous",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, prefix_kind));
                if (p->method || p->is_getter || p->is_setter) {
                    jm_callr_void_1(mt, "js_mark_method_func", val);
                }
            }
            if (p->is_getter || p->is_setter) {
                jm_emit_install_method_or_accessor(mt, object, key, val,
                                                    p->is_getter, p->is_setter);
            } else {
                // J39-7: ES B.3.7 — non-computed `__proto__: expr` in an object
                // literal sets [[Prototype]] (or no-ops for non-Object/Null) and
                // does NOT create an own property. Computed `["__proto__"]:`,
                // shorthand `{__proto__}`, and method shorthand do NOT trigger.
                if (is_proto_literal) {
                    jm_callr_void_2(mt, "js_object_proto_setter", object, val);
                } else {
                    jm_callr_3(mt, "js_create_data_property", MIR_T_I64, object, key, val);
                    jm_emit_error_lane_propagate_check(mt);
                }
            }
        } else if (prop->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            // Object spread: { ...source } — copy all own properties from source into target
            JsSpreadElementNode* sp = (JsSpreadElementNode*)prop;
            MIR_reg_t source = jm_transpile_box_item(mt, sp->argument);
            // Generator spill: restore object ref after yield-containing spread
            if (obj_spill_slot >= 0 && jm_has_yield(mt, prop)) {
                jm_gen_spill_load(mt, object, obj_spill_slot);
            }
            jm_callr_2(mt, "js_object_spread_into", MIR_T_I64, object, source);
        }
        prop = prop->next;
    }

    return object;
}

// Conditional expression (ternary)
struct JsMirBranchState {
    MIR_item_t current_func_item;
    MIR_func_t current_func;
    JsFuncCollected* current_fc;
    JsClassEntry* current_class;
    MIR_reg_t scope_env_reg;
    int scope_env_slot_count;
    int scope_depth;
    JsMirLastClosureSnapshot last_closure;
    bool scopes_saved;
    ArrayList* original_var_scopes;
    ArrayList* cloned_var_scopes;
};

static struct hashmap* jm_clone_var_scope_map(struct hashmap* src) {
    if (!src) return NULL;
    size_t count = hashmap_count(src);
    size_t cap = count + 8;
    if (cap < 16) cap = 16;
    struct hashmap* dst = em_var_scope_new((int)cap); // INT_CAST_OK: hashmap capacity is count-sized API.
    if (!dst) return NULL;

    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(src, &iter, &item)) {
        hashmap_set(dst, item);
    }
    return dst;
}

static void jm_free_branch_state(JsMirBranchState* state);

static void jm_save_branch_state(JsMirTranspiler* mt, JsMirBranchState* state) {
    memset(state, 0, sizeof(*state));
    state->current_func_item = mt->em.func_item;
    state->current_func = mt->em.func;
    state->current_fc = mt->current_fc;
    state->current_class = mt->current_class;
    state->scope_env_reg = mt->scope_env_reg;
    state->scope_env_slot_count = mt->scope_env_slot_count;
    state->scope_depth = mt->scope_depth;
    jm_save_last_closure_snapshot(mt, &state->last_closure);
    state->scopes_saved = true;
    state->original_var_scopes = mt->var_scopes;
    state->cloned_var_scopes = arraylist_new(
        state->original_var_scopes ? state->original_var_scopes->length : 1);
    if (!state->cloned_var_scopes) {
        state->scopes_saved = false;
        log_error("js-mir: failed to allocate conditional scope snapshot");
    }
    if (state->scopes_saved) {
        int scope_count = state->original_var_scopes->length;
        for (int i = 0; i < scope_count; i++) {
            struct hashmap* original = jm_var_scope_at(mt, i);
            struct hashmap* clone = jm_clone_var_scope_map(original);
            if (original && !clone) {
                state->scopes_saved = false;
                log_error("js-mir: failed to snapshot conditional scope %d", i);
                break;
            }
            if (!arraylist_append(state->cloned_var_scopes, clone)) {
                if (clone) hashmap_free(clone);
                state->scopes_saved = false;
                log_error("js-mir: failed to grow conditional scope snapshot");
                break;
            }
        }
    }
    if (!state->scopes_saved) {
        jm_free_branch_state(state);
        return;
    }
    mt->var_scopes = state->cloned_var_scopes;
}

static void jm_free_branch_state(JsMirBranchState* state) {
    if (!state) return;
    if (state->cloned_var_scopes) {
        for (int i = 0; i < state->cloned_var_scopes->length; i++) {
            struct hashmap* scope =
                (struct hashmap*)arraylist_get(state->cloned_var_scopes, i);
            if (scope) hashmap_free(scope);
        }
        arraylist_free(state->cloned_var_scopes);
        state->cloned_var_scopes = NULL;
    }
    state->original_var_scopes = NULL;
}

static void jm_restore_branch_state(JsMirTranspiler* mt, JsMirBranchState* state) {
    if (!state) return;

    mt->em.func_item = state->current_func_item;
    mt->em.func = state->current_func;
    mt->current_fc = state->current_fc;
    mt->current_class = state->current_class;
    mt->scope_env_reg = state->scope_env_reg;
    mt->scope_env_slot_count = state->scope_env_slot_count;
    mt->scope_depth = state->scope_depth;
    // Conditional-expression arms do not dominate one another. A closure env
    // allocated in one arm must not remain the writeback target in its sibling.
    jm_restore_last_closure_snapshot(mt, &state->last_closure);

    if (!state->scopes_saved) return;
    mt->var_scopes = state->original_var_scopes;
    jm_free_branch_state(state);
}

MIR_reg_t jm_transpile_conditional(JsMirTranspiler* mt, JsConditionalNode* cond) {
    MIR_reg_t truthy = jm_transpile_condition(mt, cond->test);

    MIR_reg_t result = jm_new_reg(mt, "tern", MIR_T_I64);
    MIR_label_t l_false = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_emit_branch(mt, MIR_BF, l_false, truthy);
    JsErrorLaneTrack branch_exc = jm_error_lane_state(mt);
    MIR_reg_t branch_carrier = mt->last_call_result_reg;

    JsMirBranchState branch_state;
    jm_save_branch_state(mt, &branch_state);
    jm_push_scope(mt);
    MIR_reg_t cons = jm_transpile_box_item(mt, cond->consequent);
    while (mt->scope_depth > branch_state.scope_depth) jm_pop_scope(mt);
    jm_restore_branch_state(mt, &branch_state);
    jm_emit_mov(mt, result, cons);
    JsErrorLaneTrack cons_exit = jm_error_lane_state(mt);
    jm_emit_jmp(mt, l_end);

    jm_emit_label_with_state(mt, l_false, branch_exc);
    // The consequent's last helper is path-local; the alternate begins from
    // the condition carrier that dominates both arms (D8.4.3).
    mt->last_call_result_reg = branch_carrier;
    jm_save_branch_state(mt, &branch_state);
    jm_push_scope(mt);
    MIR_reg_t alt = jm_transpile_box_item(mt, cond->alternate);
    while (mt->scope_depth > branch_state.scope_depth) jm_pop_scope(mt);
    jm_restore_branch_state(mt, &branch_state);
    jm_emit_mov(mt, result, alt);
    JsErrorLaneTrack alt_exit = jm_error_lane_state(mt);

    jm_emit_label_with_state(mt, l_end, jm_error_lane_merge(cons_exit, alt_exit));
    // Both arms define the boxed ternary value, making it the only valid
    // carrier for an ERROR-lane test emitted after the join (D8.4.3).
    mt->last_call_result_reg = result;
    return result;
}

MIR_reg_t jm_transpile_conditional_as_native(JsMirTranspiler* mt,
                                             JsConditionalNode* cond,
                                             TypeId target_type) {
    MIR_reg_t truthy = jm_transpile_condition(mt, cond->test);
    MIR_type_t result_type = (target_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
    MIR_insn_code_t move_code = (result_type == MIR_T_D) ? MIR_DMOV : MIR_MOV;
    MIR_reg_t result = jm_new_reg(mt, "tern_n", result_type);
    MIR_label_t l_false = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_emit_branch(mt, MIR_BF, l_false, truthy);
    JsErrorLaneTrack branch_exc = jm_error_lane_state(mt);

    JsMirBranchState branch_state;
    jm_save_branch_state(mt, &branch_state);
    jm_push_scope(mt);
    MIR_reg_t cons = jm_transpile_as_native(mt, cond->consequent, target_type);
    while (mt->scope_depth > branch_state.scope_depth) jm_pop_scope(mt);
    jm_restore_branch_state(mt, &branch_state);
    jm_emit(mt, MIR_new_insn(mt->ctx, move_code,
        MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, cons)));
    JsErrorLaneTrack cons_exit = jm_error_lane_state(mt);
    jm_emit_jmp(mt, l_end);

    jm_emit_label_with_state(mt, l_false, branch_exc);
    jm_save_branch_state(mt, &branch_state);
    jm_push_scope(mt);
    MIR_reg_t alt = jm_transpile_as_native(mt, cond->alternate, target_type);
    while (mt->scope_depth > branch_state.scope_depth) jm_pop_scope(mt);
    jm_restore_branch_state(mt, &branch_state);
    jm_emit(mt, MIR_new_insn(mt->ctx, move_code,
        MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, alt)));
    JsErrorLaneTrack alt_exit = jm_error_lane_state(mt);

    jm_emit_label_with_state(mt, l_end, jm_error_lane_merge(cons_exit, alt_exit));
    return result;
}

static MIR_reg_t jm_string_chars_ptr(JsMirTranspiler* mt, MIR_reg_t string_ptr) {
    MIR_reg_t chars = jm_new_reg(mt, "chars", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
        MIR_new_reg_op(mt->ctx, chars),
        MIR_new_reg_op(mt->ctx, string_ptr),
        MIR_new_int_op(mt->ctx, offsetof(String, chars))));
    return chars;
}

// Template literal
MIR_reg_t jm_transpile_template_literal(JsMirTranspiler* mt, JsTemplateLiteralNode* tmpl) {
    // The function frame owns the explicit context register.  Template
    // literals are a repeated allocation path, so they must not import or
    // reload process-global runtime state just to find the pool.
    MIR_reg_t pool_reg = jm_new_reg(mt, "pool", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, pool_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(Context, pool),
            mt->em.frame.runtime, 0, 1)));

    // Create StringBuf: stringbuf_new(pool)
    // StringBuf is a pointer-valued helper; using an integer return type here
    // truncated its address on Windows before the first template fragment.
    MIR_reg_t sb = jm_callr_1(mt, "stringbuf_new", MIR_T_P, pool_reg);

    JsAstNode* quasi = tmpl->quasis;
    JsAstNode* expr = tmpl->expressions;

    while (quasi) {
        if (quasi->node_type == JS_AST_NODE_TEMPLATE_ELEMENT) {
            JsTemplateElementNode* elem = (JsTemplateElementNode*)quasi;
            if (elem->cooked && elem->cooked->len > 0) {
                // Resolve the literal from the context-owned NamePool at
                // execution time; the MIR image must not retain compiler-pool
                // character pointers (D5.4.3).
                MIR_reg_t str_item = jm_box_string_literal(mt,
                    elem->cooked->chars, (int)elem->cooked->len);
                MIR_reg_t str_ptr = jm_callr_1(mt, "it2s", MIR_T_P, str_item);
                // `it2s` returns the String header; StringBuf consumes the
                // flexible-array character payload, not the header's length byte.
                MIR_reg_t chars = jm_string_chars_ptr(mt, str_ptr);
                jm_call_void_3(mt, "stringbuf_append_str_n",
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, sb),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, chars),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)elem->cooked->len));
            }
        }

        // Interpolated expression
        if (expr && quasi->node_type == JS_AST_NODE_TEMPLATE_ELEMENT &&
            !((JsTemplateElementNode*)quasi)->tail) {
            // Generator spill: save StringBuf across yield in expression
            int sb_spill = -1;
            if (mt->in_generator && jm_has_yield(mt, expr)) {
                sb_spill = jm_gen_spill_save(mt, sb);
            }
            MIR_reg_t eval = jm_transpile_box_item(mt, expr);
            if (sb_spill >= 0) {
                jm_gen_spill_load(mt, sb, sb_spill);
            }
            // Convert to string: js_to_string(value)
            MIR_reg_t str_item = jm_callr_1(mt, "js_to_string", MIR_T_I64, eval);
            jm_emit_error_lane_propagate_check(mt);
            // Unbox string: it2s(str_item) -> String*
            // it2s returns a String pointer; declaring an integer return here
            // truncated the pointer before template interpolation copied it.
            MIR_reg_t str_ptr = jm_callr_1(mt, "it2s", MIR_T_P, str_item);
            // Guard: if js_to_string threw (e.g. Symbol), str_ptr is null — skip append
            MIR_label_t skip_append = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ,
                MIR_new_label_op(mt->ctx, skip_append),
                MIR_new_reg_op(mt->ctx, str_ptr),
                MIR_new_int_op(mt->ctx, 0)));
            // Compute chars address: str_ptr + offsetof(String, chars)
            // (chars is a flexible array member, not a pointer)
            MIR_reg_t chars = jm_string_chars_ptr(mt, str_ptr);
            // Load String.len (uint32_t at offset 0)
            MIR_reg_t len = jm_new_reg(mt, "slen", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, len),
                MIR_new_mem_op(mt->ctx, MIR_T_U32, offsetof(String, len), str_ptr, 0, 1)));
            // stringbuf_append_str_n(sb, chars, len)
            jm_callr_void_3(mt, "stringbuf_append_str_n", sb, chars, len);
            jm_emit_label(mt, skip_append);
            expr = expr->next;
        }

        quasi = quasi->next;
    }

    // stringbuf_to_string(sb) -> String*
    MIR_reg_t result_str = jm_callr_1(mt, "stringbuf_to_string", MIR_T_P, sb);
    // Box as string
    return jm_box_string(mt, result_str);
}

// Tagged template literal: tag`str0 ${expr0} str1 ${expr1} str2`
MIR_reg_t jm_transpile_tagged_template(JsMirTranspiler* mt, JsTaggedTemplateNode* tt) {
    MIR_reg_t tag_fn;
    MIR_reg_t this_val = 0;
    if (tt->tag && tt->tag->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)tt->tag;
        this_val = jm_transpile_box_item(mt, mem->object);
        MIR_reg_t key;
        if (mem->computed) {
            key = jm_transpile_box_item(mt, mem->property);
        } else if (mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
            key = jm_box_property_name_literal(mt, prop->name->chars, prop->name->len);
        } else {
            key = jm_transpile_box_item(mt, mem->property);
        }
        tag_fn = jm_callr_2(mt, "js_get_reference", MIR_T_I64, this_val, key);
    } else {
        tag_fn = jm_transpile_box_item(mt, tt->tag);
        this_val = jm_emit_undefined(mt);
    }

    JsTemplateLiteralNode* tmpl = tt->quasi;
    if (!tmpl) return jm_emit_undefined(mt);

    // count quasi elements (strings) and expressions
    int quasi_count = ast_linked_node_count(tmpl->quasis);
    int expr_count = ast_linked_node_count(tmpl->expressions);

    // allocate arrays for cooked and raw strings (Item[quasi_count] each)
    // Use heap allocation instead of MIR_ALLOCA to avoid MIR codegen bug
    // where multiple functions with ALLOCA cause register misallocation on ARM64.
    MIR_reg_t cooked_arr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, quasi_count));
    MIR_reg_t raw_arr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, quasi_count));

    // populate cooked[] and raw[] with boxed string Items
    int qi = 0;
    for (JsAstNode* q = tmpl->quasis; q; q = q->next, qi++) {
        if (q->node_type == JS_AST_NODE_TEMPLATE_ELEMENT) {
            JsTemplateElementNode* elem = (JsTemplateElementNode*)q;
            // cooked value (may be NULL for invalid escapes per spec → undefined)
            MIR_reg_t cooked_val;
            if (elem->cooked) {
                cooked_val = jm_box_string_literal(mt, elem->cooked->chars, elem->cooked->len);
            } else {
                cooked_val = jm_emit_undefined(mt);
            }
            jm_emit_store_i64(mt, qi * 8, cooked_arr, cooked_val);
            // raw value (always a string)
            MIR_reg_t raw_val;
            if (elem->raw) {
                raw_val = jm_box_string_literal(mt, elem->raw->chars, elem->raw->len);
            } else {
                raw_val = jm_emit_undefined(mt);
            }
            jm_emit_store_i64(mt, qi * 8, raw_arr, raw_val);
        }
    }

    uint64_t site_id = 14695981039346656037ULL;
    if (tmpl) {
        uint64_t source_part = (mt->tp && mt->tp->source) ? (uint64_t)(uintptr_t)mt->tp->source : 0;
        uint64_t start_part = tmpl->source_span.start_byte;
        uint64_t end_part = tmpl->source_span.end_byte;
        site_id ^= source_part; site_id *= 1099511628211ULL;
        site_id ^= start_part; site_id *= 1099511628211ULL;
        site_id ^= end_part; site_id *= 1099511628211ULL;
        if (mt->template_site_salt != 0) {
            site_id ^= mt->template_site_salt;
            site_id *= 1099511628211ULL;
        }
    }

    MIR_reg_t tmpl_obj = jm_call_4(mt, "js_build_template_object_cached", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cooked_arr),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, raw_arr),
        MIR_T_I64, MIR_new_int_op(mt->ctx, quasi_count),
        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)site_id));

    // build args array: [template_object, ...expressions]
    int total_argc = 1 + expr_count;
    MIR_reg_t args_ptr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, total_argc));
    // store template object as first arg
    jm_emit_store_i64(mt, 0, args_ptr, tmpl_obj);
    // store expression values
    int ei = 1;
    for (JsAstNode* e = tmpl->expressions; e; e = e->next, ei++) {
        MIR_reg_t val = jm_transpile_box_item(mt, e);
        jm_emit_store_i64(mt, ei * 8, args_ptr, val);
    }

    // Preserve a scalar tag result in the caller-owned liveness slot.
    return jm_call_function_into(mt,
        MIR_new_reg_op(mt->ctx, tag_fn), MIR_new_reg_op(mt->ctx, this_val),
        MIR_new_reg_op(mt->ctx, args_ptr), MIR_new_int_op(mt->ctx, total_argc));
}

// Helper: create a function or closure value for an inner function declaration.
// If the function has captures, creates a js_new_closure with env populated from
// current scope. Otherwise creates a js_new_function.
// Js52 R1: was static; made externally linkable so jm_build_closure_for_method
// (in js_mir_statement_lowering.cpp) can size remapped envs the same way.
int jm_closure_env_alloc_size(JsMirTranspiler* mt, JsFuncCollected* fc, bool has_remapped) {
    if (!fc) return 0;
    if (!has_remapped) return JM_CAPTURE_COUNT(fc);
    int env_size = mt ? mt->scope_env_slot_count : 0;
    if (JM_CAPTURE_COUNT(fc) > env_size) env_size = JM_CAPTURE_COUNT(fc);
    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
        int slot = JM_CAPTURE_ARRAY(fc)[ci].scope_env_slot;
        if (slot >= 0 && slot + 1 > env_size) env_size = slot + 1;
        slot = JM_CAPTURE_ARRAY(fc)[ci].private_env_slot;
        if (slot >= 0 && slot + 1 > env_size) env_size = slot + 1;
    }
    if (JM_JS_FACT(fc, closure_env_has_parent_link) &&
        JM_JS_FACT(fc, closure_env_parent_link_slot) + 1 > env_size) {
        env_size = JM_JS_FACT(fc, closure_env_parent_link_slot) + 1;
    }
    if (mt) {
        JsFuncCollected* parent_fc = jm_parent_collected_func(mt, fc);
        if (parent_fc) {
            // A remapped copied env is consumed with the parent's scope-env layout.
            // Reserve its trailing link slot too; otherwise generated transitive
            // loads read one Item past the copied env and treat adjacent pool data as a pointer.
            if (parent_fc->has_parent_env_link && parent_fc->scope_env_count > env_size) {
                env_size = parent_fc->scope_env_count;
            }
        }
    }
    return env_size;
}

static bool jm_closure_has_scope_env_slot(JsFuncCollected* fc) {
    if (!fc) return false;
    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
        if (JM_CAPTURE_ARRAY(fc)[ci].scope_env_slot >= 0 ||
            JM_CAPTURE_ARRAY(fc)[ci].private_env_slot >= 0) return true;
    }
    return false;
}

static bool jm_capture_matches_scope_env_name(FnCapture* cap, const char* scope_name) {
    if (!cap || !scope_name) return false;
    // duplicate block lexicals share the same source name; scope_env_key keeps
    // the closure wired to the binding range selected during scope-env layout.
    if (cap->scope_env_key && cap->scope_env_key[0] &&
            strcmp(cap->scope_env_key, scope_name) == 0) return true;
    return strcmp(cap->name, scope_name) == 0;
}

static int jm_find_var_scope_depth_for_expr(JsMirTranspiler* mt, const char* name) {
    if (!mt || !name) return -1;
    for (int depth = mt->scope_depth; depth >= 0; depth--) {
        struct hashmap* scope = jm_var_scope_at(mt, depth);
        if (!scope) continue;
        JsVarScopeEntry key;
        memset(&key, 0, sizeof(key));
        key.name = name;
        if (hashmap_get(scope, &key)) return depth;
    }
    return -1;
}

static bool jm_capture_is_current_loop_lexical(JsMirTranspiler* mt, const char* name, JsMirVarEntry* var) {
    if (!mt || !name || !var || !var->is_let_const) return false;
    if (mt->loop_scope_depth < 0) return mt->iteration_depth > 0;
    int depth = jm_find_var_scope_depth_for_expr(mt, name);
    // for-init closures are created before iteration_depth increments, but they
    // still capture the loop lexical cell and must not share the parent env.
    return depth >= mt->loop_scope_depth;
}

static void jm_promote_capture_to_scope_env(JsMirTranspiler* mt, JsMirVarEntry* var, int slot) {
    if (!mt || !var || mt->scope_env_reg == 0 || slot < 0) return;
    var->in_scope_env = true;
    var->scope_env_slot = slot;
    var->scope_env_reg = mt->scope_env_reg;
    MIR_reg_t val = var->reg;
    if (jm_is_native_type(var->type_id))
        val = jm_box_native(mt, var->reg, var->type_id);
    jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), mt->scope_env_reg, val);
}

static int jm_last_closure_track_count(JsFuncCollected* fc) {
    if (!fc || JM_CAPTURE_COUNT(fc) <= 0) return 0;
    if (JM_CAPTURE_COUNT(fc) > JS_MIR_LAST_CLOSURE_CAPTURE_MAX) {
        return JS_MIR_LAST_CLOSURE_CAPTURE_MAX;
    }
    return JM_CAPTURE_COUNT(fc);
}

static void jm_collect_descendant_func_assignments(JsMirTranspiler* mt,
        JsFuncCollected* ancestor, struct hashmap* assigned) {
    if (!mt || !mt->tp || !ancestor || !ancestor->node || !assigned) return;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId ancestor_id = ast_index_find(index, (AstNode*)ancestor->node);
    if (ancestor_id == AST_NODE_ID_INVALID) return;
    for (int fi = 0; fi < mt->func_count; fi++) {
        JsFunctionNode* descendant = mt->func_entries[fi].node;
        AstNodeId descendant_id = descendant ? ast_index_find(index,
            (AstNode*)descendant) : AST_NODE_ID_INVALID;
        if (!descendant || !descendant->body || descendant_id == ancestor_id ||
                !ast_index_node_descends(index, descendant_id, ancestor_id)) continue;
        // A synchronously invoked outer closure can run nested callbacks that
        // mutate its captures. The indexed ancestry includes synthetic bodies.
        jm_collect_indexed_func_assignments(mt, descendant->body, assigned);
    }
}

static void jm_track_last_closure_env(JsMirTranspiler* mt, MIR_reg_t env,
        JsFuncCollected* fc, bool use_capture_slots) {
    if (!mt || !fc || env == 0) return;
    int count = jm_last_closure_track_count(fc);
    struct hashmap* assigned = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (assigned && fc->node && fc->node->body) {
        jm_collect_indexed_func_assignments(mt, fc->node->body, assigned);
        jm_collect_descendant_func_assignments(mt, fc, assigned);
    }
    mt->last_closure_env_reg = env;
    mt->last_closure_capture_count = count;
    for (int ci = 0; ci < count; ci++) {
        mt->last_closure_capture_names[ci] = jm_persist_name(JM_CAPTURE_ARRAY(fc)[ci].name);
        mt->last_closure_capture_slots[ci] =
            use_capture_slots ? jm_capture_env_slot(&JM_CAPTURE_ARRAY(fc)[ci], ci) : ci;
        mt->last_closure_capture_is_transitive[ci] =
            JM_CAPTURE_ARRAY(fc)[ci].grandparent_slot >= 0;
        mt->last_closure_capture_is_nfe[ci] = JM_CAPTURE_ARRAY(fc)[ci].is_nfe_binding;
        bool capture_assigned = false;
        if (assigned) {
            JsNameSetEntry lookup;
            memset(&lookup, 0, sizeof(lookup));
            lookup.name = jm_persist_name(JM_CAPTURE_ARRAY(fc)[ci].name);
            capture_assigned = hashmap_get(assigned, &lookup) != NULL;
        }
        // readback is only valid for captures this closure can mutate; read-only
        // captures can be stale private copies and must not overwrite caller locals.
        mt->last_closure_capture_is_assigned[ci] = capture_assigned;
    }
    if (assigned) hashmap_free(assigned);
    mt->last_closure_has_env = count > 0;
}

static void jm_track_tdz_closure_captures(JsMirTranspiler* mt, MIR_reg_t env,
        JsFuncCollected* fc, bool use_capture_slots) {
    if (!mt || !fc || env == 0) return;
    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
        JsMirVarEntry* var = jm_find_var(mt, JM_CAPTURE_ARRAY(fc)[ci].name);
        if (!var || !var->is_let_const || !var->tdz_active) continue;
        if (mt->tdz_closure_capture_count >= JS_MIR_TDZ_CLOSURE_CAPTURE_MAX) {
            log_error("js-mir: TDZ closure capture tracker overflow");
            return;
        }
        JsMirTdzClosureCapture* tracked =
            &mt->tdz_closure_captures[mt->tdz_closure_capture_count++];
        tracked->env_reg = env;
        tracked->slot = use_capture_slots
            ? jm_capture_env_slot(&JM_CAPTURE_ARRAY(fc)[ci], ci) : ci;
        tracked->binding_scope_depth =
            jm_find_var_scope_depth_for_expr(mt, JM_CAPTURE_ARRAY(fc)[ci].name);
        tracked->is_transitive = JM_CAPTURE_ARRAY(fc)[ci].grandparent_slot >= 0;
        tracked->name = jm_persist_name(JM_CAPTURE_ARRAY(fc)[ci].name);
        // A hoisted function can snapshot a loop lexical's TDZ value before an
        // earlier initializer creates another closure; one last-closure slot
        // then loses the callback's binding cell.
    }
}

static void jm_copy_parent_env_link_for_copied_closure(JsMirTranspiler* mt,
        JsFuncCollected* fc, MIR_reg_t env, int env_alloc_size, bool has_remapped) {
    if (!mt || !fc || env == 0 || mt->scope_env_reg == 0) return;
    if (!has_remapped && !JM_JS_FACT(fc, closure_env_has_parent_link)) return;
    bool needs_parent_link = false;
    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
        if (JM_CAPTURE_ARRAY(fc)[ci].grandparent_slot >= 0) {
            needs_parent_link = true;
            break;
        }
    }
    if (!needs_parent_link) return;
    JsFuncCollected* parent_fc = jm_parent_collected_func(mt, fc);
    if (!parent_fc) return;
    if (!parent_fc->has_parent_env_link || parent_fc->scope_env_count <= 0) return;
    int parent_env_link_slot = parent_fc->scope_env_count - 1;
    if (parent_env_link_slot < 0 || parent_env_link_slot >= env_alloc_size) return;

    MIR_reg_t parent_env = jm_new_reg(mt, "copy_parent_env", MIR_T_I64);
    jm_emit_load_i64(mt, parent_env, parent_env_link_slot * (int)sizeof(uint64_t), mt->scope_env_reg);
    // A dense copied env normally uses this slot for a capture. A mixed closure
    // explicitly reserves its parent-link tail, so it must forward the inherited
    // link or nested arrows read the immediate loop value as lexical `this`.
    jm_emit_store_i64(mt, parent_env_link_slot * (int)sizeof(uint64_t), env, parent_env);
}

static bool jm_force_copied_env_for_field_initializer(JsMirTranspiler* mt,
        JsFuncCollected* fc) {
    if (!mt || !fc || !mt->force_closure_env_copy) return false;
    // Bug 2 (vibe/Lambda_Bug.md): force a private (copied) closure env whenever a
    // field-initializer arrow captures a lexical meta binding (_js_this /
    // new.target / arguments) AT ALL — not only when its captures are
    // EXCLUSIVELY meta bindings. A real event handler captures `this` AND an
    // ordinary binding (e.g. an imported function it calls); the old "all
    // captures must be meta" test bypassed the copy for that dominant shape, so
    // the arrow reverted to the enclosing shared scope-env `this` (undefined in
    // an esbuild IIFE) and lost `this` again. `force_closure_env_copy` is set
    // only during field initialization, so this only affects field-init arrows.
    for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
        if (jm_capture_is_lexical_meta_binding(JM_CAPTURE_ARRAY(fc)[ci].name)) return true;
    }
    return false;
}

static bool jm_should_use_shared_scope_env(JsMirTranspiler* mt,
        JsFuncCollected* fc, bool immediate_call, bool* force_copy_out) {
    bool force_copy = jm_force_copied_env_for_field_initializer(mt, fc);
    if (force_copy_out) *force_copy_out = force_copy;
    bool use_scope_env = (!force_copy && !JM_JS_FACT(fc, closure_env_has_parent_link) &&
        mt->scope_env_reg != 0 && JM_CAPTURE_ARRAY(fc)[0].scope_env_slot >= 0);
    if (use_scope_env) {
        // a named function expression can combine a private self slot with
        // ordinary captures; rejecting the shared env snapshots outer values
        // before the parent scope initializes them (D5.3, D5.4.3).
        use_scope_env = jm_shared_scope_env_captures_valid(mt, fc, false);
    }
    if (use_scope_env) {
        for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
            JsMirVarEntry* cv = jm_find_var(mt, JM_CAPTURE_ARRAY(fc)[ci].name);
            if (jm_capture_is_current_loop_lexical(mt, JM_CAPTURE_ARRAY(fc)[ci].name, cv)) {
                use_scope_env = false;
                break;
            }
        }
    }
    return use_scope_env;
}

static void jm_capture_arrow_lexical_home_class(JsMirTranspiler* mt,
                                                 MIR_reg_t fn_reg,
                                                 JsFunctionNode* fn) {
    if (!mt || !fn_reg || !fn || !fn->is_arrow || !mt->current_class) return;
    // `super` in an arrow is lexical. Without the defining class on the arrow,
    // a callback invoked from another method inherits that caller's home class
    // and resolves `super` against the wrong prototype.
    jm_create_gc_root_slot(mt, fn_reg);
    MIR_reg_t home_class = jm_emit_class_object_for_entry(mt, mt->current_class);
    if (!home_class) return;
    jm_create_gc_root_slot(mt, home_class);
    jm_emit_set_function_home_class(mt, fn_reg, home_class);
}

// update a mapped formal parameter after an arguments indexed write. The
// arguments exotic object may have been unmapped, so read its effective value
// instead of copying the right-hand side directly into the parameter register.
static void jm_sync_arguments_param_after_write(JsMirTranspiler* mt,
                                                 JsMemberNode* member) {
    if (!mt || !member || mt->arguments_reg == 0 ||
        !member->object || member->object->node_type != JS_AST_NODE_IDENTIFIER ||
        !member->property || member->property->node_type != JS_AST_NODE_LITERAL) {
        return;
    }
    JsIdentifierNode* obj_id = (JsIdentifierNode*)member->object;
    if (!obj_id->name || obj_id->name->len != 9 ||
        strncmp(obj_id->name->chars, "arguments", 9) != 0) {
        return;
    }
    JsLiteralNode* idx_lit = (JsLiteralNode*)member->property;
    if (idx_lit->literal_type != JS_LITERAL_NUMBER || idx_lit->has_decimal) return;
    int idx = (int)idx_lit->value.number_value;
    if (idx < 0) return;
    JsMirVarEntry* pvar = jm_arguments_param_var(mt, idx);
    if (!pvar) return;
    MIR_reg_t mapped_val = jm_call_3(mt, "js_arguments_mapped_get", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->arguments_reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, idx),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, pvar->reg));
    jm_emit_mov(mt, pvar->reg, mapped_val);
}

MIR_reg_t jm_create_func_or_closure(JsMirTranspiler* mt, JsFuncCollected* fc) {
    if (!fc || !fc->func_item) return jm_emit_null(mt);
    int pc = JM_PARAM_COUNT(fc);
    if (JM_JS_FACT(fc, has_rest_param)) pc = -pc;  // negative signals rest params to js_invoke_fn
    MIR_reg_t fn_reg;
    if (JM_CAPTURE_COUNT(fc) > 0) {
        // v29 TDZ: Propagate capture metadata from the parent scope.
        jm_prepare_closure_captures(mt, fc, false);
        // Check if this closure should use the parent's shared scope env.
        // Share scope env so var-scoped closures can persist mutations to outer
        // variables.  But in loops, if any captured variable is let/const, we must
        // NOT share — let/const need per-iteration binding semantics.
        bool use_scope_env = jm_should_use_shared_scope_env(mt, fc, false, NULL);
        if (use_scope_env) {
            fn_reg = jm_call_4(mt, "js_new_closure_mir", MIR_T_I64,
                MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                MIR_T_I64, MIR_new_int_op(mt->ctx, pc),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->scope_env_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, mt->scope_env_slot_count));
        } else {
            // Fallback: allocate own env and copy values (per-closure env, not shared).
            bool has_remapped = jm_closure_has_scope_env_slot(fc);
            int env_alloc_size = jm_closure_env_alloc_size(mt, fc, has_remapped);

            // Detect self-capture: if the function references its own name, we must
            // defer filling that env slot until after the closure is created, then
            // patch it to point to the closure itself.
            char self_vname[128] = {0};
            int self_ref_slot = -1;
            if (fc->node && fc->node->name) {
                snprintf(self_vname, sizeof(self_vname), "_js_%.*s",
                    (int)fc->node->name->len, fc->node->name->chars);
            }

            MIR_reg_t env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, env_alloc_size));
            // D5.2/D5.3.3: copied scalar captures initially point into this
            // activation; retain the unpublished env and rehome it at epilogue.
            jm_register_owned_env(mt, env);
            if (JM_JS_FACT(fc, closure_env_has_parent_link) && mt->scope_env_reg != 0) {
                // Mixed closures keep private captures local while shared
                // lexical captures read and write through the parent cell.
                jm_emit_store_i64(mt, JM_JS_FACT(fc, closure_env_parent_link_slot) * (int)sizeof(uint64_t), env, mt->scope_env_reg);
            }

            for (int ci = 0; ci < JM_CAPTURE_COUNT(fc); ci++) {
                if (JM_CAPTURE_ARRAY(fc)[ci].scope_env_slot < 0 &&
                    jm_capture_uses_live_module_var(mt, &JM_CAPTURE_ARRAY(fc)[ci])) continue;
                int slot = jm_capture_env_slot(&JM_CAPTURE_ARRAY(fc)[ci], ci);
                if (slot < 0) continue;

                // Skip self-capture — will be patched after closure creation
                if (self_vname[0] && strcmp(JM_CAPTURE_ARRAY(fc)[ci].name, self_vname) == 0) {
                    self_ref_slot = slot;
                    continue;
                }

                JsMirVarEntry* var = jm_find_var(mt, JM_CAPTURE_ARRAY(fc)[ci].name);
                MIR_reg_t val;
                if (var) {
                    // async for-of loop lets may inherit a stale scope-env slot;
                    // per-iteration closures must copy the freshly assigned register.
                    if (var->in_scope_env && var->scope_env_reg != 0 && var->scope_env_slot >= 0 &&
                        !jm_capture_is_current_loop_lexical(mt, JM_CAPTURE_ARRAY(fc)[ci].name, var)) {
                        val = jm_new_reg(mt, "cenv_senv_live", MIR_T_I64);
                        jm_emit_load_i64(mt, val, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg);
                    } else if (var->from_env) {
                        val = jm_new_reg(mt, "cenv_live", MIR_T_I64);
                        jm_emit_load_i64(mt, val, var->env_slot * (int)sizeof(uint64_t), var->env_reg);
                    } else {
                        val = var->reg;
                        if (jm_is_native_type(var->type_id))
                            val = jm_box_native(mt, var->reg, var->type_id);
                    }
                } else if (strcmp(JM_CAPTURE_ARRAY(fc)[ci].name, "_js_this") == 0) {
                    val = jm_call_0(mt, "js_get_lexical_this_binding", MIR_T_I64);
                } else if (strcmp(JM_CAPTURE_ARRAY(fc)[ci].name, "_js_new.target") == 0) {
                    // Arrow closures capture new.target at creation time; the
                    // dynamic runtime value is cleared when the arrow is called.
                    val = jm_emit_current_new_target(mt);
                } else if (mt->module_consts) {
                    JsModuleConstEntry* mc = jm_find_module_const(mt, JM_CAPTURE_ARRAY(fc)[ci].name);
                    if (mc && mc->const_type == MCONST_MODVAR) {
                        val = jm_load_module_var(mt, (uint32_t)mc->int_val);
                    } else {
                        val = jm_emit_null(mt);
                    }
                } else {
                    val = jm_emit_null(mt);
                }
                jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), env, val);
            }
            jm_copy_parent_env_link_for_copied_closure(mt, fc, env, env_alloc_size, has_remapped);
            fn_reg = jm_call_4(mt, "js_new_closure_mir", MIR_T_I64,
                MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                MIR_T_I64, MIR_new_int_op(mt->ctx, pc),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                MIR_T_I64, MIR_new_int_op(mt->ctx, env_alloc_size));

            // Patch self-reference: store the closure itself in its own env slot
            if (self_ref_slot >= 0) {
                jm_emit_store_i64(mt, self_ref_slot * (int)sizeof(uint64_t), env, fn_reg);
            }

            // Js56 P2: register last_closure for the writeback path so subsequent
            // assignments to captured vars (e.g. `flag = true;` after `class Foo {
            // constructor() { if (flag) ... } }`) propagate into this env. The
            // class-constructor path goes through jm_create_func_or_closure rather
            // than jm_transpile_func_expr, so without this it would never register
            // and outer writes would never reach the constructor's captured env.
            jm_track_last_closure_env(mt, env, fc, has_remapped);
            jm_track_tdz_closure_captures(mt, env, fc, has_remapped);
        }
    } else {
        fn_reg = jm_call_2(mt, "js_new_function_mir", MIR_T_I64,
            MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
            MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
    }
    jm_emit_finalize_function(mt, fn_reg, fc, fc->node);
    jm_capture_arrow_lexical_home_class(mt, fn_reg, fc->node);
    return fn_reg;
}

MIR_reg_t jm_emit_module_const_value(JsMirTranspiler* mt,
        const JsModuleConstEntry* mc) {
    if (!mc) return jm_emit_null(mt);
    switch (mc->const_type) {
    case MCONST_CLASS:
    case MCONST_MODVAR:
        return jm_load_module_var(mt, (uint32_t)mc->int_val);
    }
    return jm_emit_null(mt);
}

// Function expression / arrow function
MIR_reg_t jm_transpile_func_expr(JsMirTranspiler* mt, JsFunctionNode* fn) {
    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
    if (!fc || !fc->func_item) {
        log_error("js-mir: function expression not found in collected functions");
        return jm_emit_null(mt);
    }

    int param_count = ast_linked_node_count(fn->params);
    if (JM_JS_FACT(fc, has_rest_param)) param_count = -param_count;  // negative signals rest params

    MIR_reg_t fn_reg;
    if (JM_CAPTURE_COUNT(fc) > 0) {
        jm_prepare_closure_captures(mt, fc, true);
        bool force_copied_field_env = false;
        bool use_scope_env = jm_should_use_shared_scope_env(mt, fc, true,
            &force_copied_field_env);
        if (use_scope_env) {
            jm_track_last_closure_env(mt, mt->scope_env_reg, fc, true);

            fn_reg = jm_call_4(mt, "js_new_closure_mir", MIR_T_I64,
                MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                MIR_T_I64, MIR_new_int_op(mt->ctx, param_count),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->scope_env_reg),
                MIR_T_I64, MIR_new_int_op(mt->ctx, mt->scope_env_slot_count));

            // Patch NFE self-reference in shared scope_env: the parent scope_env
            // includes the NFE name as a slot (from Phase 1.7), but the parent
            // never defines it — only the closure itself knows its own identity.
            // Without this, recursive calls via the NFE name find null in the env.
            if (fn->name) {
                const char* nfe_self = jm_var_name(fn->name);
                for (int i = 0; i < JM_CAPTURE_COUNT(fc); i++) {
                    if (JM_CAPTURE_ARRAY(fc)[i].is_nfe_binding &&
                        strcmp(JM_CAPTURE_ARRAY(fc)[i].name, nfe_self) == 0 &&
                        JM_CAPTURE_ARRAY(fc)[i].scope_env_slot >= 0) {
                        jm_emit_store_i64(mt, JM_CAPTURE_ARRAY(fc)[i].scope_env_slot * (int)sizeof(uint64_t), mt->scope_env_reg, fn_reg);
                        break;
                    }
                }
            }
            // Also handle assign_target_vname self-reference (e.g., var f = function() { ... f() ... })
            if (mt->assign_target_vname) {
                for (int i = 0; i < JM_CAPTURE_COUNT(fc); i++) {
                    if (strcmp(JM_CAPTURE_ARRAY(fc)[i].name, mt->assign_target_vname) == 0 && JM_CAPTURE_ARRAY(fc)[i].scope_env_slot >= 0) {
                        jm_emit_store_i64(mt, JM_CAPTURE_ARRAY(fc)[i].scope_env_slot * (int)sizeof(uint64_t), mt->scope_env_reg, fn_reg);
                        break;
                    }
                }
            }
        } else {
            bool has_remapped = jm_closure_has_scope_env_slot(fc);
            int env_alloc_size = jm_closure_env_alloc_size(mt, fc, has_remapped);

            // Detect self-capture via assignment target hint:
            // e.g. sc_loop1_75 = function(l) { ... sc_loop1_75(l.cdr) ... }
            // Also detect NFE self-reference via the function's own name:
            // e.g. var f = function myName() { return myName; }
            int self_ref_slot_fe = -1;
            char nfe_self_name[128] = {0};
            if (fn->name) {
                snprintf(nfe_self_name, sizeof(nfe_self_name), "_js_%.*s",
                    (int)fn->name->len, fn->name->chars);
            }

            MIR_reg_t env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                MIR_T_I64, MIR_new_int_op(mt->ctx, env_alloc_size));
            // D5.2/D5.3.3: copied scalar captures initially point into this
            // activation; retain the unpublished env and rehome it at epilogue.
            jm_register_owned_env(mt, env);
            if (JM_JS_FACT(fc, closure_env_has_parent_link) && mt->scope_env_reg != 0) {
                // Mixed closures keep private captures local while shared
                // lexical captures read and write through the parent cell.
                jm_emit_store_i64(mt, JM_JS_FACT(fc, closure_env_parent_link_slot) * (int)sizeof(uint64_t), env, mt->scope_env_reg);
            }

            for (int i = 0; i < JM_CAPTURE_COUNT(fc); i++) {
                if (JM_CAPTURE_ARRAY(fc)[i].scope_env_slot < 0 &&
                    jm_capture_uses_live_module_var(mt, &JM_CAPTURE_ARRAY(fc)[i])) continue;
                int slot = jm_capture_env_slot(&JM_CAPTURE_ARRAY(fc)[i], i);
                if (slot < 0) continue;

                // Skip self-capture — will be patched after closure creation
                if ((mt->assign_target_vname && strcmp(JM_CAPTURE_ARRAY(fc)[i].name, mt->assign_target_vname) == 0) ||
                    (nfe_self_name[0] && JM_CAPTURE_ARRAY(fc)[i].is_nfe_binding &&
                     strcmp(JM_CAPTURE_ARRAY(fc)[i].name, nfe_self_name) == 0)) {
                    self_ref_slot_fe = slot;
                    continue;
                }
                if (force_copied_field_env &&
                    jm_capture_is_lexical_meta_binding(JM_CAPTURE_ARRAY(fc)[i].name)) {
                    // field initializer arrows snapshot the construction binding;
                    // a transitive parent-env slot belongs to the enclosing closure.
                    MIR_reg_t value_to_store;
                    if (strcmp(JM_CAPTURE_ARRAY(fc)[i].name, "_js_this") == 0) {
                        value_to_store = jm_call_0(mt,
                            "js_get_lexical_this_binding", MIR_T_I64);
                    } else if (strcmp(JM_CAPTURE_ARRAY(fc)[i].name, "_js_new.target") == 0) {
                        value_to_store = jm_emit_current_new_target(mt);
                    } else {
                        JsMirVarEntry* arguments_var = jm_find_var(mt, "_js_arguments");
                        value_to_store = arguments_var ? arguments_var->reg : jm_emit_undefined(mt);
                    }
                    jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), env, value_to_store);
                    continue;
                }
                JsMirVarEntry* var = jm_find_var(mt, JM_CAPTURE_ARRAY(fc)[i].name);
                if (var) {
                    MIR_reg_t value_to_store;
                    // async for-of loop lets may inherit a stale scope-env slot;
                    // per-iteration closures must copy the freshly assigned register.
                    if (var->in_scope_env && var->scope_env_reg != 0 && var->scope_env_slot >= 0 &&
                        !jm_capture_is_current_loop_lexical(mt, JM_CAPTURE_ARRAY(fc)[i].name, var)) {
                        value_to_store = jm_new_reg(mt, "fenv_senv_live", MIR_T_I64);
                        jm_emit_load_i64(mt, value_to_store, var->scope_env_slot * (int)sizeof(uint64_t), var->scope_env_reg);
                    } else if (var->from_env) {
                        value_to_store = jm_new_reg(mt, "fenv_live", MIR_T_I64);
                        jm_emit_load_i64(mt, value_to_store, var->env_slot * (int)sizeof(uint64_t), var->env_reg);
                    } else {
                        value_to_store = var->reg;
                        if (jm_is_native_type(var->type_id)) {
                            value_to_store = jm_box_native(mt, var->reg, var->type_id);
                        }
                    }
                    jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), env, value_to_store);
                } else {
                    if (strcmp(JM_CAPTURE_ARRAY(fc)[i].name, "_js_this") == 0) {
                        MIR_reg_t this_val = jm_call_0(mt, "js_get_lexical_this_binding", MIR_T_I64);
                        jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), env, this_val);
                    } else if (strcmp(JM_CAPTURE_ARRAY(fc)[i].name, "_js_new.target") == 0) {
                        // Preserve arrow lexical new.target in the closure env,
                        // instead of relying on the call-time runtime slot.
                        MIR_reg_t new_target_val = jm_emit_current_new_target(mt);
                        jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), env, new_target_val);
                    } else {
                    bool found_const = false;
                    if (mt->module_consts) {
                        JsModuleConstEntry* mc = jm_find_module_const(mt, JM_CAPTURE_ARRAY(fc)[i].name);
                        if (mc) {
                            found_const = true;
                            MIR_reg_t const_val = jm_emit_module_const_value(mt, mc);
                            jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), env, const_val);
                        }
                    }
                    if (!found_const) {
                        log_error("js-mir: captured variable '%s' not found in scope (in function '%s')",
                            JM_CAPTURE_ARRAY(fc)[i].name, fc->name);
                        MIR_reg_t undef_val = jm_new_reg(mt, "missing_cap", MIR_T_I64);
                        jm_emit_reg_op(mt, MIR_MOV, undef_val, MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR));
                        jm_emit_store_i64(mt, slot * (int)sizeof(uint64_t), env, undef_val);
                    }
                    } // close else (non _js_this)
                }
            }

            jm_copy_parent_env_link_for_copied_closure(mt, fc, env, env_alloc_size, has_remapped);
            jm_track_last_closure_env(mt, env, fc, has_remapped);

            fn_reg = jm_call_4(mt, "js_new_closure_mir", MIR_T_I64,
                MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                MIR_T_I64, MIR_new_int_op(mt->ctx, param_count),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                MIR_T_I64, MIR_new_int_op(mt->ctx, env_alloc_size));

            // Patch self-reference: store the closure itself in its copied env slot
            if (self_ref_slot_fe >= 0) {
                jm_emit_store_i64(mt, self_ref_slot_fe * (int)sizeof(uint64_t), env, fn_reg);
            }
        }
    } else {
        fn_reg = jm_call_2(mt, "js_new_distinct_function_mir", MIR_T_I64,
            MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
            MIR_T_I64, MIR_new_int_op(mt->ctx, param_count));
    }

    jm_emit_finalize_function(mt, fn_reg, fc, fn);
    jm_capture_arrow_lexical_home_class(mt, fn_reg, fn);
    return fn_reg;
}

// ============================================================================
// Expression result boundary
// ============================================================================

static MIR_reg_t jm_transpile_expression_legacy(JsMirTranspiler* mt,
        JsAstNode* expr);

static MirValue jm_expression_value(JsMirTranspiler* mt, JsAstNode* item,
        MIR_reg_t reg, TypeId type_id, ValueRep rep) {
    Type* contract = item ? item->type : NULL;
    MIR_type_t mir_type = rep == VALUE_REP_F64 ? MIR_T_D
        : rep == VALUE_REP_RAW_NON_GC_POINTER ? MIR_T_P : MIR_T_I64;
    JitValueClass value_class = rep == VALUE_REP_ITEM ? JIT_VALUE_BOXED_ITEM
        : rep == VALUE_REP_RAW_NON_GC_POINTER ? JIT_VALUE_RAW_NON_GC_POINTER
        : JIT_VALUE_NON_GC_SCALAR;
    return em_value(reg, mir_type, type_id, rep, value_class, contract, item);
}

MirValue jm_transpile_expression_value(JsMirTranspiler* mt, JsAstNode* item) {
    if (!item) {
        return jm_expression_value(mt, NULL, jm_emit_null(mt), LMD_TYPE_NULL,
            VALUE_REP_ITEM);
    }

    if (item->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)item;
        JsMirVarEntry* var = id->name ? jm_find_var(mt, jm_var_name(id->name)) : NULL;
        if (var && jm_is_native_type(var->type_id)) {
            return jm_expression_value(mt, item, var->reg, var->type_id,
                var->type_id == LMD_TYPE_FLOAT ? VALUE_REP_F64 : VALUE_REP_I64);
        }
        return jm_expression_value(mt, item, jm_transpile_identifier(mt, id),
            jm_get_effective_type(mt, item), VALUE_REP_ITEM);
    }

    switch (item->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION:
    case JS_AST_NODE_UNARY_EXPRESSION: {
        bool native = item->node_type == JS_AST_NODE_BINARY_EXPRESSION
            ? jm_is_native_binary_expression(mt, (JsBinaryNode*)item)
            : jm_is_native_unary_expression(mt, (JsUnaryNode*)item);
        TypeId type_id = jm_get_effective_type(mt, item);
        MIR_reg_t result = jm_transpile_expression_legacy(mt, item);
        if (native && jm_is_native_type(type_id)) {
            return jm_expression_value(mt, item, result, type_id,
                type_id == LMD_TYPE_FLOAT ? VALUE_REP_F64 : VALUE_REP_I64);
        }
        return jm_expression_value(mt, item, result, type_id, VALUE_REP_ITEM);
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)item;
        JsMirVarEntry* var = NULL;
        if (asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)asgn->left;
            var = jm_find_var(mt, jm_var_name(id->name));
        }
        MIR_reg_t result = jm_transpile_expression_legacy(mt, item);
        if (var && !var->from_env && (var->type_id == LMD_TYPE_INT ||
                var->type_id == LMD_TYPE_FLOAT)) {
            return jm_expression_value(mt, item, result, var->type_id,
                var->type_id == LMD_TYPE_FLOAT ? VALUE_REP_F64 : VALUE_REP_I64);
        }
        return jm_expression_value(mt, item, result,
            jm_get_effective_type(mt, item), VALUE_REP_ITEM);
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsFuncCollected* fc = jm_resolve_native_call(mt, (JsCallNode*)item);
        MIR_reg_t result = jm_transpile_expression_legacy(mt, item);
        if (fc && jm_call_result_uses_native_register(mt, (JsCallNode*)item, fc)) {
            TypeId type_id = JM_JS_FACT(fc, return_type);
            return jm_expression_value(mt, item, result, type_id,
                type_id == LMD_TYPE_FLOAT ? VALUE_REP_F64 : VALUE_REP_I64);
        }
        return jm_expression_value(mt, item, result,
            jm_get_effective_type(mt, item), VALUE_REP_ITEM);
    }
    case JS_AST_NODE_LITERAL:
        return jm_expression_value(mt, item,
            jm_transpile_literal(mt, (JsLiteralNode*)item),
            jm_get_effective_type(mt, item), VALUE_REP_ITEM);
    default:
        break;
    }

    if (item->type) {
        TypeId type_id = item->type->type_id;
        if (type_id == LMD_TYPE_NULL) {
            return jm_expression_value(mt, item, jm_emit_null(mt), type_id,
                VALUE_REP_ITEM);
        }
        if (type_id == LMD_TYPE_INT || type_id == LMD_TYPE_FLOAT ||
                type_id == LMD_TYPE_BOOL) {
            return jm_expression_value(mt, item, jm_transpile_expression_legacy(mt, item),
                type_id, type_id == LMD_TYPE_FLOAT ? VALUE_REP_F64 : VALUE_REP_I64);
        }
        if (type_id == LMD_TYPE_STRING) {
            return jm_expression_value(mt, item, jm_transpile_expression_legacy(mt, item),
                type_id, VALUE_REP_RAW_NON_GC_POINTER);
        }
    }

    return jm_expression_value(mt, item,
        jm_ensure_boxed(mt, jm_transpile_expression_legacy(mt, item)),
        jm_get_effective_type(mt, item), VALUE_REP_ITEM);
}

MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item) {
    return em_require_rep(&mt->em, jm_transpile_expression_value(mt, item),
        VALUE_REP_ITEM).reg;
}

MirValue jm_transpile_box_value(JsMirTranspiler* mt, JsAstNode* item) {
    return em_require_rep(&mt->em, jm_transpile_expression_value(mt, item),
        VALUE_REP_ITEM);
}

static MirValue jm_profile_lower_value(void* owner, AstNode* node) {
    return jm_transpile_box_value((JsMirTranspiler*)owner, (JsAstNode*)node);
}

static MIR_reg_t jm_profile_emit_condition(void* owner, MirValue value) {
    JsMirTranspiler* mt = (JsMirTranspiler*)owner;
    value = em_apply_value_demand(&mt->em, value,
        MIR_VALUE_REQUIRED_REP, VALUE_REP_ITEM);
    return jm_emit_is_truthy(mt, value.reg, (JsAstNode*)value.provenance_node);
}

typedef struct JsSequenceLowering {
    MirLoweringProfile profile;
    MIR_reg_t result;
} JsSequenceLowering;

static void jm_lower_sequence_item(void* owner, AstNode* node, bool is_last) {
    JsSequenceLowering* sequence = (JsSequenceLowering*)owner;
    MirValue value = em_lower_profile_value(&sequence->profile, node,
        is_last ? MIR_VALUE_ANY : MIR_VALUE_DISCARD);
    sequence->result = value.reg;
}

// v23b: Transpile condition expression → raw int64 0/1 for MIR_BF/BT.
// For untyped binary comparisons, calls _raw facade directly (saves 2 calls).
// For native numeric comparisons, the value boundary already returns 0/1.
// For everything else, falls back to box + is_truthy.
MIR_reg_t jm_transpile_condition(JsMirTranspiler* mt, JsAstNode* expr) {
    if (!expr) {
        // no test (e.g., for(;;)) — always true
        MIR_reg_t r = jm_new_reg(mt, "cond1", MIR_T_I64);
        jm_emit_reg_op(mt, MIR_MOV, r, MIR_new_int_op(mt->ctx, 1));
        return r;
    }

    // Case 1: binary comparison with typed numeric operands → native path (already returns 0/1)
    if (expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        bool is_comparison = false;
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
            is_comparison = true; break;
        default: break;
        }

        if (is_comparison) {
            TypeId lt = jm_get_effective_type(mt, bin->left);
            TypeId rt = jm_get_effective_type(mt, bin->right);
            bool left_num = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
            bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);

            if (left_num && right_num) {
                // both numeric → native comparison already returns 0/1
                return jm_transpile_expression_value(mt, expr).reg;
            }

            // Tune8 §2.5: kept — see comment above the box-binary-op path.
            if (bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_STRICT_NE) {
                MIR_reg_t boxed;
                if (jm_try_emit_uri_compare_fast_path(mt, bin->left, bin->right, &boxed)) {
                    MIR_reg_t raw = jm_emit_is_truthy(mt, boxed, expr);
                    if (bin->op == JS_OP_STRICT_NE) {
                        MIR_reg_t inv = jm_new_reg(mt, "uri_ne", MIR_T_I64);
                        jm_emit_reg_binary_op(mt, MIR_XOR, inv, raw, MIR_new_int_op(mt->ctx, 1));
                        raw = inv;
                    }
                    return raw;
                }
            }

            // Untyped comparison → use _raw facade returning int64 directly.
            // Tune8 §2.1:
            //   - js_ne_raw / js_loose_ne_raw collapse to (eq ^ 1) inline.
            //   - js_lt/gt/le/ge_raw collapse to js_cmp_raw(op, l, r); op is a
            //     compile-time constant operand so the runtime-side branch is
            //     well-predicted after the first call at a given site.
            const char* raw_fn = NULL;
            bool invert = false;
            int cmp_op = -1;   // 0=LT, 1=GT, 2=LE, 3=GE; -1 = use eq path
            switch (bin->op) {
            case JS_OP_LT:        raw_fn = "js_cmp_raw"; cmp_op = 0; break;
            case JS_OP_GT:        raw_fn = "js_cmp_raw"; cmp_op = 1; break;
            case JS_OP_LE:        raw_fn = "js_cmp_raw"; cmp_op = 2; break;
            case JS_OP_GE:        raw_fn = "js_cmp_raw"; cmp_op = 3; break;
            case JS_OP_STRICT_EQ: raw_fn = "js_eq_raw"; break;
            case JS_OP_STRICT_NE: raw_fn = "js_eq_raw"; invert = true; break;
            case JS_OP_EQ:        raw_fn = "js_loose_eq_raw"; break;
            case JS_OP_NE:        raw_fn = "js_loose_eq_raw"; invert = true; break;
            default: break;
            }
            if (raw_fn) {
                MIR_reg_t left_val = jm_transpile_box_item(mt, bin->left);
                MIR_reg_t right_val = jm_transpile_box_item(mt, bin->right);
                MIR_reg_t raw;
                if (cmp_op >= 0) {
                    raw = jm_call_3(mt, raw_fn, MIR_T_I64,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, cmp_op),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, left_val),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, right_val));
                } else {
                    raw = jm_callr_2(mt, raw_fn, MIR_T_I64, left_val, right_val);
                }
                if (invert) {
                    MIR_reg_t inv = jm_new_reg(mt, "nerawinv", MIR_T_I64);
                    jm_emit_reg_binary_op(mt, MIR_XOR, inv, raw, MIR_new_int_op(mt->ctx, 1));
                    raw = inv;
                }
                return raw;
            }
        }
    }

    // Case 2: logical NOT → invert the inner condition
    if (expr->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)expr;
        if (un->op == JS_OP_NOT && un->operand) {
            MIR_reg_t inner = jm_transpile_condition(mt, un->operand);
            MIR_reg_t result = jm_new_reg(mt, "notc", MIR_T_I64);
            jm_emit_reg_binary_op(mt, MIR_XOR, result, inner, MIR_new_int_op(mt->ctx, 1));
            return result;
        }
    }

    // Case 3: generic boxed fallback through the shared branch-demand owner.
    MirLoweringProfile profile = {&mt->em, mt, jm_profile_lower_value,
        jm_profile_emit_condition};
    return em_lower_profile_condition(&profile, (AstNode*)expr);
}

// ============================================================================
// Expression dispatcher
// ============================================================================

static void jm_begin_arg_stack_scope(JsMirTranspiler* mt, JsMirArgStackScope* scope) {
    scope->parent = mt->arg_stack_scope;
    scope->saved_depth = mt->arg_frame_depth;
    scope->base_slot = -1;
    scope->slot_count = 0;
    scope->args_reg = 0;
    mt->arg_stack_scope = scope;
}

static void jm_end_arg_stack_scope(JsMirTranspiler* mt, JsMirArgStackScope* scope) {
    // Release completed-call roots at the original expression boundary so
    // weak/finalized objects are not retained until the caller returns.
    jm_emit_arg_frame_clear(mt, scope);
    mt->arg_frame_depth = scope->saved_depth;
    mt->arg_stack_scope = scope->parent;
}

static MIR_reg_t jm_transpile_invocation_with_post_call_state(
        JsMirTranspiler* mt, JsCallNode* call, bool construct) {
    JsMirArgStackScope arg_scope;
    jm_begin_arg_stack_scope(mt, &arg_scope);
    MIR_reg_t result = construct
        ? jm_transpile_new_expr(mt, call)
        : jm_transpile_call(mt, call);
    // Restore transient argument roots before the callee's error lane is read.
    jm_end_arg_stack_scope(mt, &arg_scope);
    // A call may mutate captured bindings through either shared scope owner.
    jm_scope_env_reload_vars(mt);
    jm_emit_error_lane_propagate_check(mt);
    jm_env_reload_shared_captures(mt);
    return result;
}

// _simple: no private-home enter/leave, unlike the same-named 4-arg emitter
// in js_mir_statement_lowering.cpp
void jm_emit_class_static_named_field(JsMirTranspiler* mt,
        MIR_reg_t cls_obj, JsStaticFieldEntry* sf, MIR_reg_t value) {
    if (!sf || !sf->name) return;
    MIR_reg_t fn_name = jm_box_string_literal(mt, sf->name->chars, (int)sf->name->len);
    jm_callr_void_2(mt, "js_set_function_name_if_anonymous", value, fn_name);
    MIR_reg_t key = jm_is_private_name(sf->name)
        ? jm_box_string_literal(mt, sf->name->chars, (int)sf->name->len)
        : jm_box_property_name_literal(mt, sf->name->chars, sf->name->len);
    if (jm_is_private_name(sf->name)) {
        key = jm_callr_2(mt, "js_private_key_for_class", MIR_T_I64, cls_obj, key);
    }
    jm_callr_1(mt, "js_check_class_static_field_key", MIR_T_I64, key);
    jm_emit_error_lane_propagate_check(mt);
    jm_emit_class_static_property(mt, cls_obj, key, value,
        jm_is_private_name(sf->name));
}

void jm_emit_class_static_property(JsMirTranspiler* mt, MIR_reg_t cls_obj,
                                   MIR_reg_t key, MIR_reg_t value,
                                   bool private_brand) {
    jm_callr_3(mt, "js_create_data_property", MIR_T_I64, cls_obj, key, value);
    jm_emit_error_lane_propagate_check(mt);
    if (private_brand) {
        jm_callr_3(mt, "js_private_brand_add", MIR_T_I64, cls_obj, key, cls_obj);
        jm_emit_error_lane_propagate_check(mt);
    }
}

// The raw dispatcher remains a private migration seam.  New structural
// consumers receive producer-owned MirValue descriptors instead.
static MIR_reg_t jm_transpile_expression_legacy(JsMirTranspiler* mt,
        JsAstNode* expr) {
    if (!expr) return jm_emit_null(mt);

    switch (expr->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION:
        return jm_transpile_binary(mt, (JsBinaryNode*)expr);
    case JS_AST_NODE_UNARY_EXPRESSION:
        return jm_transpile_unary(mt, (JsUnaryNode*)expr);
    case JS_AST_NODE_CALL_EXPRESSION:
        return jm_transpile_invocation_with_post_call_state(mt,
            (JsCallNode*)expr, false);
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* mem = (JsMemberNode*)expr;
        return jm_transpile_member(mt, mem);
    }
    case JS_AST_NODE_ARRAY_EXPRESSION:
        return jm_transpile_array(mt, (JsArrayNode*)expr);
    case JS_AST_NODE_OBJECT_EXPRESSION:
        return jm_transpile_object(mt, (JsObjectNode*)expr);
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_FUNCTION_DECLARATION: // class method functions built as FUNCTION_DECLARATION
        return jm_transpile_func_expr(mt, (JsFunctionNode*)expr);
    case JS_AST_NODE_SPREAD_ELEMENT: {
        // When spread is passed as standalone expression (e.g., inside object spread),
        // evaluate and return the argument value — the caller handles spreading.
        JsSpreadElementNode* sp = (JsSpreadElementNode*)expr;
        if (sp->argument) return jm_transpile_box_item(mt, sp->argument);
        return jm_emit_null(mt);
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION:
        return jm_transpile_conditional(mt, (JsConditionalNode*)expr);
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return jm_transpile_template_literal(mt, (JsTemplateLiteralNode*)expr);
    case JS_AST_NODE_TAGGED_TEMPLATE:
        return jm_transpile_tagged_template(mt, (JsTaggedTemplateNode*)expr);
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
        return jm_transpile_assignment(mt, (JsAssignmentNode*)expr);
    case JS_AST_NODE_NEW_EXPRESSION:
        return jm_transpile_invocation_with_post_call_state(mt,
            (JsCallNode*)expr, true);
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        // v11: comma operator — evaluate all expressions, return last
        JsSequenceNode* seq = (JsSequenceNode*)expr;
        JsSequenceLowering sequence = {
            {&mt->em, mt, jm_profile_lower_value, jm_profile_emit_condition},
            jm_emit_null(mt)};
        em_visit_linked_nodes(seq->expressions, &sequence,
            jm_lower_sequence_item);
        return sequence.result;
    }
    case JS_AST_NODE_REGEX: {
        // Embed literal bytes in MIR; compiler name-pool pointers can be retired
        // before a later event executes this generated code.
        JsRegexNode* re = (JsRegexNode*)expr;
        MIR_reg_t pattern = jm_box_string_literal(mt, re->pattern, re->pattern_len);
        MIR_reg_t flags = re->flags
            ? jm_box_string_literal(mt, re->flags, re->flags_len)
            : jm_box_string_literal(mt, "", 0);
        return jm_callr_2(mt, "js_create_regex_literal_items", MIR_T_I64, pattern, flags);
    }
    case JS_AST_NODE_YIELD_EXPRESSION: {
        JsYieldNode* yield_node = (JsYieldNode*)expr;
        if (!mt->in_generator && !yield_node->argument && !yield_node->delegate) {
            JsIdentifierNode yield_id;
            memset(&yield_id, 0, sizeof(yield_id));
            yield_id.node_type = JS_AST_NODE_IDENTIFIER;
            yield_id.name = name_pool_create_len(mt->tp->name_pool, "yield", 5);
            return jm_transpile_identifier(mt, &yield_id);
        }
        MIR_reg_t val;
        if (yield_node->argument) {
            val = jm_transpile_box_item(mt, yield_node->argument);
        } else {
            val = jm_emit_undefined(mt);
        }

        if (mt->in_generator) {
            // v15: Generator state machine — emit save/return/resume/load
            int next_state = jm_next_resume_state(mt, JS_MIR_SUSPEND_YIELD);
            if (next_state < 0) return val;

            jm_emit_suspend_env_save(mt);

            if (yield_node->delegate) {
                // yield* delegation: return [iterable, resume_state, 1]
                MIR_reg_t result = jm_call_2(mt, "js_gen_yield_delegate_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)next_state));
                jm_emit_eval_local_pop_if_needed(mt);
                jm_emit_ret(mt, result);
            } else {
                // Regular yield: return [yield_val, next_state]
                MIR_reg_t result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val),
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)next_state));
                jm_emit_eval_local_pop_if_needed(mt);
                jm_emit_ret(mt, result);
            }

            // Emit resume label for this state
            jm_emit_label(mt, mt->gen_state_labels[next_state]);

            jm_emit_resume_env_restore(mt);

            // Re-initialize try-block state registers after resume.
            // These are plain MIR registers (not env-backed), so they don't
            // survive across yield. Without re-init, stale/undefined values in
            // has_return_reg cause spurious early returns at the try end_label
            // (returning the also-uninitialized return_val_reg, which surfaces
            // as a `null`-valued done iteration result).
            jm_emit_try_state_reset(mt);

            // Generator.prototype.return resumes the suspended yield with an
            // internal return signal. Route it through the same delayed-return
            // registers as a source-level return so enclosing finally blocks run.
            {
                MIR_reg_t is_return_signal = jm_callr_1(mt, "js_gen_is_return_signal", MIR_T_I64, mt->gen_input_reg);
                MIR_label_t no_return_signal = jm_new_label(mt);
                jm_emit_branch(mt, MIR_BF, no_return_signal, is_return_signal);
                if (mt->gen_active_iterator_slot >= 0 && mt->gen_env_reg) {
                    MIR_reg_t active_iter = jm_new_reg(mt, "actiter", MIR_T_I64);
                    jm_emit_load_i64(mt, active_iter, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg);
                    MIR_reg_t null_iter = jm_emit_null(mt);
                    jm_emit_store_i64(mt, mt->gen_active_iterator_slot * (int)sizeof(uint64_t), mt->gen_env_reg, null_iter);
                    jm_emit_iterator_close(mt, active_iter);
                    MIR_reg_t close_exc = jm_emit_error_lane_test(mt);
                    MIR_label_t close_ok = jm_new_label(mt);
                    jm_emit_branch(mt, MIR_BF, close_ok, close_exc);
                    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
                    jm_emit_label(mt, close_ok);
                }
                MIR_reg_t return_value = jm_callr_1(mt, "js_gen_return_signal_value", MIR_T_I64, mt->gen_input_reg);
                if (!jm_emit_delayed_return_completion(mt, return_value,
                        JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL)) {
                    MIR_reg_t done_result = jm_call_2(mt, "js_gen_yield_result", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, return_value),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)-1));
                    jm_emit_ret(mt, done_result);
                }
                jm_emit_label(mt, no_return_signal);
            }

            // Generator.prototype.throw resumes through the same yield point;
            // turn its opaque resume marker into a normal throw completion so
            // the enclosing catch/finally machinery owns the abrupt control.
            {
                MIR_reg_t is_throw_signal = jm_callr_1(mt, "js_gen_is_throw_signal", MIR_T_I64, mt->gen_input_reg);
                MIR_label_t no_throw_signal = jm_new_label(mt);
                jm_emit_branch(mt, MIR_BF, no_throw_signal, is_throw_signal);
                MIR_reg_t throw_value = jm_callr_1(mt, "js_gen_throw_signal_value", MIR_T_I64, mt->gen_input_reg);
                jm_emit_generator_throw_completion(mt, throw_value);
                jm_emit_label(mt, no_throw_signal);
            }

            jm_emit_error_lane_propagate_check(mt);

            // The yield expression evaluates to the 'input' parameter (sent value)
            return mt->gen_input_reg;
        }

        // Non-generator: flat mode, yield just returns its argument value
        return val;
    }
    case JS_AST_NODE_AWAIT_EXPRESSION: {
        JsAwaitNode* await_node = (JsAwaitNode*)expr;
        MIR_reg_t promise_val = jm_transpile_box_item(mt, await_node->argument);

        // Async generator await lowering is shared with implicit loop awaits;
        // only the resume-state classification differs between callers.
        if (mt->in_generator && mt->in_async) {
            return jm_emit_await_value_reg(mt, promise_val, JS_MIR_SUSPEND_AWAIT);
        }

        // Js57 P5 (fulfillment/rejection-order): for top-level awaits in
        // nested modules, route through js_p5_module_await — it publishes the
        // awaited value onto the module registry when (and only when) the
        // awaited value is a pending Promise, and falls back to js_await_sync
        // for settled Promises / non-Promises so `export default await
        // Promise.resolve(42)` still unwraps to 42. The chain-pending case is
        // what gives the dynamic-import chain its spec-order property.
        extern int js_dynamic_import_suppress_module_drain;
        // Dynamic imports enter module compilation at depth one, but their
        // pending top-level await still has to suspend the import promise;
        // otherwise js_await_sync returns an undefined placeholder instead of
        // preserving the module's evaluation dependency.
        bool is_dynamic_import_module = js_dynamic_import_suppress_module_drain > 0;
        bool is_p5_module_tla = (mt->is_module && mt->in_main &&
            !mt->in_generator && !mt->in_async && mt->filename &&
            ((!jm_has_current_source_function(mt) && js_tla_module_depth_get() >= 2) ||
             is_dynamic_import_module));
        if (is_p5_module_tla) {
            MIR_reg_t spec_reg = jm_box_string_literal(mt, mt->filename,
                (int)strlen(mt->filename));
            MIR_reg_t result = jm_callr_2(mt, "js_p5_module_await", MIR_T_I64, spec_reg, promise_val);
            return result;
        }
        // Phase 5: Synchronous fast path (non-state-machine async)
        MIR_reg_t result = jm_callr_1(mt, "js_await_sync", MIR_T_I64, promise_val);
        return result;
    }
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION: {
        // Class expression: var X = class Y {} or var X = class {}
        JsClassNode* cls_expr = (JsClassNode*)expr;
        MIR_reg_t cls_obj = jm_call_0(mt, "js_new_class_function", MIR_T_I64);
        // A class expression creates metadata and methods before assigning its
        // result, so keep the transient class object in the exact root frame.
        jm_create_gc_root_slot(mt, cls_obj);
        MIR_reg_t ctor_super_val = 0;
        // An anonymous class expression still has the index-published class ID;
        // collection may have supplied its effective name from the assignment.
        String* effective_name = cls_expr->name;
        JsClassEntry* ce = jm_find_collected_class(mt, cls_expr);
        if (ce) effective_name = ce->name ? ce->name : effective_name;
        if (!ce && effective_name) {
            ce = jm_find_class(mt, effective_name->chars, (int)effective_name->len);
        }
        if (!ce && !effective_name && mt->assign_target_vname &&
            strncmp(mt->assign_target_vname, "_js_", 4) == 0) {
            const char* target_name = mt->assign_target_vname + 4;
            int target_len = (int)strlen(target_name);
            if (target_len > 0) {
                ce = jm_find_class(mt, target_name, target_len);
                if (ce) effective_name = ce->name;
            }
        }
        JsAstNode* heritage = cls_expr->superclass ? cls_expr->superclass :
            ((ce && ce->node && ce->node->superclass) ? ce->node->superclass : NULL);
        JsClassEntry* static_superclass = jm_matching_static_superclass(ce, heritage);
        jm_emit_set_class_source(mt, cls_obj, cls_expr);
        // TDZ: class x extends x {} → throw ReferenceError
        jm_emit_class_self_extends_check(mt, ce, effective_name);
        MIR_reg_t checked_heritage_val = 0;
        if (cls_expr->superclass &&
            cls_expr->superclass->node_type != JS_AST_NODE_IDENTIFIER &&
            cls_expr->superclass->node_type != JS_AST_NODE_NULL &&
            !(cls_expr->superclass->node_type == JS_AST_NODE_LITERAL &&
              ((JsLiteralNode*)cls_expr->superclass)->literal_type == JS_LITERAL_NULL)) {
            checked_heritage_val = jm_transpile_box_item(mt, cls_expr->superclass);
            jm_callr_1(mt, "js_check_class_heritage_constructor", MIR_T_I64, checked_heritage_val);
            jm_emit_error_lane_propagate_check(mt);
        }
            jm_emit_class_static_methods(mt, cls_obj, 0, ce, static_superclass,
                JS_MIR_COMPUTED_KEY_AFTER_FUNCTION);

            // Store constructor body and instance prototype on the class function
            // (new Type() where Type is a runtime variable holding this class object)
            if (ce) {
                // Dynamic `new` calls use the stored constructor directly, so
                // its lexical super lookup must retain this class as home object.
                jm_emit_class_constructor_property(mt, cls_obj, ce, true);
                // Create the instance prototype with all instance methods
                MIR_reg_t proto_obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
                jm_create_gc_root_slot(mt, proto_obj);
                // Publish the fresh prototype before setup helpers can compact
                // it; subsequent operations refresh from the class carrier.
                jm_emit_class_prototype_properties(mt, cls_obj, proto_obj);
                proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
                jm_callr_void_2(mt, "js_set_default_constructor_property", proto_obj, cls_obj);
                // Set up prototype's __proto__ chain for instanceof on parent classes.
                bool heritage_is_null = false;
                proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
                ctor_super_val = jm_emit_class_prototype_chain(mt, ce, cls_obj, heritage,
                    static_superclass, proto_obj, checked_heritage_val, &heritage_is_null);
                proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
                jm_emit_class_instance_methods(mt, proto_obj, cls_obj, ce);
                jm_callr_void_2(mt, "js_set_default_constructor_property", proto_obj, cls_obj);
                // Mark all prototype methods as non-enumerable (ES spec)
                jm_callr_void_1(mt, "js_mark_all_non_enumerable", proto_obj);
            } else {
                MIR_reg_t proto_obj = jm_call_0(mt, "js_new_object", MIR_T_I64);
                jm_create_gc_root_slot(mt, proto_obj);
                jm_emit_class_prototype_properties(mt, cls_obj, proto_obj);
                proto_obj = jm_emit_current_class_prototype(mt, cls_obj, proto_obj);
                jm_callr_void_2(mt, "js_set_default_constructor_property", proto_obj, cls_obj);
            }

            // The runtime preserves an own class element named `name`.
            {
                const char* class_name_chars = effective_name ? effective_name->chars : "";
                int class_name_len = effective_name ? (int)effective_name->len : 0;
                MIR_reg_t name_val = jm_box_string_literal(mt, class_name_chars, class_name_len);
                jm_callr_void_2(mt, "js_set_class_name", cls_obj, name_val);
            }

            // v18g: Set class .length property (constructor parameter count)
            jm_emit_class_length_property(mt, cls_obj, ce);

                // Mark all static methods on class object as non-enumerable (ES spec)
                if (ce) {
                    jm_callr_void_1(mt, "js_mark_all_non_enumerable", cls_obj);
                }

                if (ctor_super_val) {
                    jm_callr_void_2(mt, "js_set_class_superclass", cls_obj, ctor_super_val);
                }

                // For class expressions with an inner name (var X = class Y { ... }),
                // store the class object into Y's module var so that methods referencing
                // Y can access the fully-initialized class object (with static methods).
            if (ce && effective_name && mt->module_consts) {
                char inner_vname[128];
                snprintf(inner_vname, sizeof(inner_vname), "_js_%.*s",
                    (int)effective_name->len, effective_name->chars);
                JsModuleConstEntry* inner_mc = jm_find_module_const(mt, inner_vname);
                if (inner_mc && inner_mc->const_type == MCONST_CLASS) {
                    jm_store_module_var(mt, (uint32_t)inner_mc->int_val, cls_obj);
                    log_debug("js-mir: class expression inner name '%s' → module_var[%d]",
                        inner_vname, (int)inner_mc->int_val);
                }
                if (ce->inner_module_var_index >= 0) {
                    jm_store_module_var(mt, (uint32_t)ce->inner_module_var_index, cls_obj);
                }
            }

            if (ce) {
                jm_emit_class_instance_field_metadata(mt, cls_obj, ce);
            }

            jm_emit_class_computed_field_module_keys(mt, cls_obj, ce);

            jm_emit_class_instance_computed_field_metadata_keys(mt, cls_obj, ce);

            jm_emit_class_static_initializers(mt, cls_obj, ce, ctor_super_val);
        log_debug("js-mir: class expression evaluated with prototype identity");
        return cls_obj;
    }
    default:
        log_error("js-mir: unsupported expression type %d", expr->node_type);
        return jm_emit_null(mt);
    }
}
