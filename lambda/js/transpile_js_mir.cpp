// transpile_js_mir.cpp - Direct MIR generation for JavaScript
//
// Replaces the C codegen path (transpile_js.cpp -> C2MIR) with direct MIR IR
// emission. Mirrors the Lambda MIR transpiler architecture (transpile-mir.cpp).
//
// Design: All JS values are boxed Items (MIR_T_I64). JS runtime functions
// (js_add, js_subtract, etc.) take and return Items. Boxing is only needed
// for literals; expression results are already boxed.

#include "js_transpiler.hpp"
#include "js_dom.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include "../../lib/file_utils.h"
#include "../transpiler.hpp"
#include <mir.h>
#include <mir-gen.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <alloca.h>

// External reference to Lambda runtime context pointer (defined in mir.c)
extern "C" Context* _lambda_rt;
extern "C" {
    void *import_resolver(const char *name);
}

// External from runner.cpp
extern __thread EvalContext* context;
extern void* heap_alloc(int size, TypeId type_id);
extern void heap_init();

// ============================================================================
// JsMirTranspiler Context
// ============================================================================

struct JsMirImportEntry {
    MIR_item_t proto;
    MIR_item_t import;
};

struct JsMirVarEntry {
    MIR_reg_t reg;
    MIR_type_t mir_type;     // MIR_T_I64 for int/boxed, MIR_T_D for native double
    TypeId type_id;          // LMD_TYPE_INT, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_ANY (boxed)
    bool from_env;           // true if loaded from closure env
    int env_slot;            // slot index in env array
    MIR_reg_t env_reg;       // register holding env pointer (for write-back)
};

// Loop label pair for break/continue
struct JsLoopLabels {
    MIR_label_t continue_label;
    MIR_label_t break_label;
};

// Capture entry for closure analysis
struct JsCaptureEntry {
    char name[128];      // variable name (with _js_ prefix)
};

// Function entry for pre-pass collection
struct JsFuncCollected {
    JsFunctionNode* node;
    char name[128];
    MIR_item_t func_item;   // set after creation (boxed version)
    // Capture info
    JsCaptureEntry captures[32];
    int capture_count;
    // Phase 4: Type inference results
    TypeId param_types[16];         // inferred parameter types
    TypeId return_type;             // inferred return type
    int param_count;                // cached param count
    MIR_item_t native_func_item;    // native version (NULL if not generated)
    bool has_native_version;        // whether native version was generated
    // TCO:
    bool is_tco_eligible;           // has tail-recursive calls → loop transform
};

// Class method info for transpiler
struct JsClassMethodEntry {
    String* name;                   // method name
    JsFuncCollected* fc;            // collected function entry
    int param_count;
    bool is_constructor;
};

// Class info for transpiler
struct JsClassEntry {
    JsClassNode* node;
    String* name;
    JsClassMethodEntry methods[32];
    int method_count;
    JsClassMethodEntry* constructor;     // points into methods[] or NULL
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

    // Loop label stack
    JsLoopLabels loop_stack[32];
    int loop_depth;

    int reg_counter;
    int label_counter;

    // Collected functions (pre-pass)
    JsFuncCollected func_entries[256];
    int func_count;

    // Collected classes
    JsClassEntry class_entries[32];
    int class_count;

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
};

// ============================================================================
// Hashmap helpers
// ============================================================================

struct JsImportCacheEntry {
    char name[128];
    JsMirImportEntry entry;
};

static int js_import_cache_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsImportCacheEntry*)a)->name, ((JsImportCacheEntry*)b)->name);
}
static uint64_t js_import_cache_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsImportCacheEntry*)item)->name,
        strlen(((JsImportCacheEntry*)item)->name), seed0, seed1);
}

struct JsVarScopeEntry {
    char name[128];
    JsMirVarEntry var;
};

static int js_var_scope_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsVarScopeEntry*)a)->name, ((JsVarScopeEntry*)b)->name);
}
static uint64_t js_var_scope_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsVarScopeEntry*)item)->name,
        strlen(((JsVarScopeEntry*)item)->name), seed0, seed1);
}

struct JsLocalFuncEntry {
    char name[128];
    MIR_item_t func_item;
};

static int js_local_func_cmp(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(((JsLocalFuncEntry*)a)->name, ((JsLocalFuncEntry*)b)->name);
}
static uint64_t js_local_func_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(((JsLocalFuncEntry*)item)->name,
        strlen(((JsLocalFuncEntry*)item)->name), seed0, seed1);
}

// ============================================================================
// Basic MIR helpers
// ============================================================================

static MIR_reg_t jm_new_reg(JsMirTranspiler* mt, const char* prefix, MIR_type_t type) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", prefix, mt->reg_counter++);
    MIR_type_t rtype = (type == MIR_T_P || type == MIR_T_F) ? MIR_T_I64 : type;
    return MIR_new_func_reg(mt->ctx, mt->current_func, rtype, name);
}

static MIR_label_t jm_new_label(JsMirTranspiler* mt) {
    return MIR_new_label(mt->ctx);
}

static void jm_emit(JsMirTranspiler* mt, MIR_insn_t insn) {
    MIR_append_insn(mt->ctx, mt->current_func_item, insn);
}

static void jm_emit_label(JsMirTranspiler* mt, MIR_label_t label) {
    MIR_append_insn(mt->ctx, mt->current_func_item, label);
}

// ============================================================================
// Scope management
// ============================================================================

static void jm_push_scope(JsMirTranspiler* mt) {
    if (mt->scope_depth >= 63) { log_error("js-mir: scope overflow"); return; }
    mt->scope_depth++;
    mt->var_scopes[mt->scope_depth] = hashmap_new(sizeof(JsVarScopeEntry), 16, 0, 0,
        js_var_scope_hash, js_var_scope_cmp, NULL, NULL);
}

static void jm_pop_scope(JsMirTranspiler* mt) {
    if (mt->scope_depth <= 0) { log_error("js-mir: scope underflow"); return; }
    hashmap_free(mt->var_scopes[mt->scope_depth]);
    mt->var_scopes[mt->scope_depth] = NULL;
    mt->scope_depth--;
}

static void jm_set_var(JsMirTranspiler* mt, const char* name, MIR_reg_t reg,
                       MIR_type_t mir_type = MIR_T_I64, TypeId type_id = LMD_TYPE_ANY) {
    JsVarScopeEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.var.reg = reg;
    entry.var.mir_type = mir_type;
    entry.var.type_id = type_id;
    hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
}

static JsMirVarEntry* jm_find_var(JsMirTranspiler* mt, const char* name) {
    JsVarScopeEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    for (int i = mt->scope_depth; i >= 0; i--) {
        if (!mt->var_scopes[i]) continue;
        JsVarScopeEntry* found = (JsVarScopeEntry*)hashmap_get(mt->var_scopes[i], &key);
        if (found) return &found->var;
    }
    return NULL;
}

// ============================================================================
// Capture analysis for closures
// ============================================================================

// Simple string set using hashmap
struct JsNameSetEntry {
    char name[128];
};

static uint64_t jm_name_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsNameSetEntry* e = (const JsNameSetEntry*)item;
    return hashmap_sip(e->name, strlen(e->name), seed0, seed1);
}

static int jm_name_cmp(const void* a, const void* b, void* udata) {
    return strcmp(((const JsNameSetEntry*)a)->name, ((const JsNameSetEntry*)b)->name);
}

static void jm_name_set_add(struct hashmap* set, const char* name) {
    JsNameSetEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "%s", name);
    hashmap_set(set, &e);
}

static bool jm_name_set_has(struct hashmap* set, const char* name) {
    JsNameSetEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return hashmap_get(set, &key) != NULL;
}

// Forward declare
static void jm_collect_body_refs(JsAstNode* node, struct hashmap* refs);
static void jm_collect_body_locals(JsAstNode* node, struct hashmap* locals);

// Collect all identifier references in a function body (excluding nested function bodies)
static void jm_collect_body_refs(JsAstNode* node, struct hashmap* refs) {
    if (!node) return;

    switch (node->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        if (id->name) {
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
            jm_name_set_add(refs, name);
        }
        break;
    }
    // Don't recurse into nested function bodies (they have their own scope)
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_FUNCTION_DECLARATION:
        break;

    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_body_refs(s, refs); s = s->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        JsAstNode* d = v->declarations;
        while (d) { jm_collect_body_refs(d, refs); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        // Recurse into init (may reference outer vars)
        if (d->init) jm_collect_body_refs(d->init, refs);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        jm_collect_body_refs(bin->left, refs);
        jm_collect_body_refs(bin->right, refs);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        jm_collect_body_refs(un->operand, refs);
        break;
    }
    // Note: JS_AST_NODE_UNARY_EXPRESSION covers both unary ops and update (++/--)
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* a = (JsAssignmentNode*)node;
        jm_collect_body_refs(a->left, refs);
        jm_collect_body_refs(a->right, refs);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* c = (JsCallNode*)node;
        jm_collect_body_refs(c->callee, refs);
        JsAstNode* arg = c->arguments;
        while (arg) { jm_collect_body_refs(arg, refs); arg = arg->next; }
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* m = (JsMemberNode*)node;
        jm_collect_body_refs(m->object, refs);
        if (m->computed) jm_collect_body_refs(m->property, refs);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* r = (JsReturnNode*)node;
        jm_collect_body_refs(r->argument, refs);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        jm_collect_body_refs(es->expression, refs);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_collect_body_refs(ifn->test, refs);
        jm_collect_body_refs(ifn->consequent, refs);
        jm_collect_body_refs(ifn->alternate, refs);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_collect_body_refs(f->init, refs);
        jm_collect_body_refs(f->test, refs);
        jm_collect_body_refs(f->update, refs);
        jm_collect_body_refs(f->body, refs);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_collect_body_refs(w->test, refs);
        jm_collect_body_refs(w->body, refs);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        jm_collect_body_refs(cond->test, refs);
        jm_collect_body_refs(cond->consequent, refs);
        jm_collect_body_refs(cond->alternate, refs);
        break;
    }
    // Note: logical expressions use JS_AST_NODE_BINARY_EXPRESSION (already handled above)
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* arr = (JsArrayNode*)node;
        JsAstNode* el = arr->elements;
        while (el) { jm_collect_body_refs(el, refs); el = el->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* obj = (JsObjectNode*)node;
        JsAstNode* prop = obj->properties;
        while (prop) { jm_collect_body_refs(prop, refs); prop = prop->next; }
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* p = (JsPropertyNode*)node;
        jm_collect_body_refs(p->value, refs);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* tl = (JsTemplateLiteralNode*)node;
        JsAstNode* expr = tl->expressions;
        while (expr) { jm_collect_body_refs(expr, refs); expr = expr->next; }
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT: {
        JsSpreadElementNode* spread = (JsSpreadElementNode*)node;
        jm_collect_body_refs(spread->argument, refs);
        break;
    }
    default:
        // For unhandled node types, we may miss some references
        // but that's OK — we'll just not capture those variables
        break;
    }
}

// Collect all locally declared variable names in a function body
static void jm_collect_body_locals(JsAstNode* node, struct hashmap* locals) {
    if (!node) return;

    switch (node->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)node;
        JsAstNode* d = v->declarations;
        while (d) { jm_collect_body_locals(d, locals); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)node;
        if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)d->id;
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
            jm_name_set_add(locals, name);
        }
        break;
    }
    // Don't recurse into nested functions
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_FUNCTION_DECLARATION:
        break;

    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_body_locals(s, locals); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        jm_collect_body_locals(ifn->consequent, locals);
        jm_collect_body_locals(ifn->alternate, locals);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* f = (JsForNode*)node;
        jm_collect_body_locals(f->init, locals);
        jm_collect_body_locals(f->body, locals);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* w = (JsWhileNode*)node;
        jm_collect_body_locals(w->body, locals);
        break;
    }
    default:
        break;
    }
}

// Analyze captures for a function: find identifiers referenced but not locally declared
static void jm_analyze_captures(JsFuncCollected* fc, struct hashmap* outer_scope_names) {
    JsFunctionNode* fn = fc->node;
    fc->capture_count = 0;

    // Collect parameter names
    struct hashmap* params = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    JsAstNode* param = fn->params;
    while (param) {
        if (param->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)param;
            char name[128];
            snprintf(name, sizeof(name), "_js_%.*s", (int)id->name->len, id->name->chars);
            jm_name_set_add(params, name);
        }
        param = param->next;
    }

    // Collect local variable declarations
    struct hashmap* locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (fn->body) jm_collect_body_locals(fn->body, locals);

    // Collect all identifier references in the body
    struct hashmap* refs = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (fn->body) jm_collect_body_refs(fn->body, refs);

    // Find captures: referenced identifiers that are not params/locals but ARE in outer scope
    // Skip self-references (function calling itself is NOT a capture)
    char self_name[128] = {0};
    if (fn->name && fn->name->chars) {
        snprintf(self_name, sizeof(self_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
    }

    size_t iter = 0;
    void* item;
    while (hashmap_iter(refs, &iter, &item)) {
        JsNameSetEntry* ref = (JsNameSetEntry*)item;
        if (jm_name_set_has(params, ref->name)) continue;    // local param
        if (jm_name_set_has(locals, ref->name)) continue;    // local var
        if (!jm_name_set_has(outer_scope_names, ref->name)) continue;  // not in outer scope
        if (self_name[0] && strcmp(ref->name, self_name) == 0) continue; // self-reference
        // This is a capture
        if (fc->capture_count < 32) {
            snprintf(fc->captures[fc->capture_count].name, 128, "%s", ref->name);
            fc->capture_count++;
            log_debug("js-mir: capture '%s' in function '%s'", ref->name, fc->name);
        }
    }

    hashmap_free(params);
    hashmap_free(locals);
    hashmap_free(refs);
}

// ============================================================================
// Import management
// ============================================================================

static JsMirImportEntry* jm_ensure_import(JsMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    JsImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);

    JsImportCacheEntry* found = (JsImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    if (found) return &found->entry;

    char proto_name[140];
    snprintf(proto_name, sizeof(proto_name), "%s_p", name);

    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, nres, res_types, nargs, args);
    MIR_item_t imp = MIR_new_import(mt->ctx, name);

    JsImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", name);
    new_entry.entry.proto = proto;
    new_entry.entry.import = imp;
    hashmap_set(mt->import_cache, &new_entry);

    found = (JsImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    return &found->entry;
}

// Item(Item, Item)
static JsMirImportEntry* jm_ensure_import_ii_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 2, args, 1);
}

// Item(Item)
static JsMirImportEntry* jm_ensure_import_i_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Item(void)
static JsMirImportEntry* jm_ensure_import_v_i(JsMirTranspiler* mt, const char* name) {
    return jm_ensure_import(mt, name, MIR_T_I64, 0, NULL, 1);
}

// ============================================================================
// Emit call helpers
// ============================================================================

static MIR_reg_t jm_call_0(JsMirTranspiler* mt, const char* fn_name, MIR_type_t ret_type) {
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 0, NULL, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res)));
    return res;
}

static MIR_reg_t jm_call_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 1, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1));
    return res;
}

static MIR_reg_t jm_call_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 2, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2));
    return res;
}

static MIR_reg_t jm_call_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 3, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 6,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3));
    return res;
}

static MIR_reg_t jm_call_4(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4) {
    MIR_var_t args[4] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 4, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 7,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4));
    return res;
}

static void jm_call_void_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 1, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1));
}

static void jm_call_void_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 2, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2));
}

static void jm_call_void_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 3, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2, a3));
}

// ============================================================================
// Constants
// ============================================================================

static const uint64_t ITEM_NULL_VAL  = (uint64_t)LMD_TYPE_NULL << 56;
static const uint64_t ITEM_TRUE_VAL  = ((uint64_t)LMD_TYPE_BOOL << 56) | 1;
static const uint64_t ITEM_FALSE_VAL = ((uint64_t)LMD_TYPE_BOOL << 56) | 0;
static const uint64_t ITEM_INT_TAG   = (uint64_t)LMD_TYPE_INT << 56;
static const uint64_t STR_TAG        = (uint64_t)LMD_TYPE_STRING << 56;
static const uint64_t MASK56         = 0x00FFFFFFFFFFFFFFULL;

static MIR_reg_t jm_emit_null(JsMirTranspiler* mt) {
    MIR_reg_t r = jm_new_reg(mt, "null", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    return r;
}

// ============================================================================
// Boxing helpers
// ============================================================================

// Box int64 constant -> Item
static MIR_reg_t jm_box_int_const(JsMirTranspiler* mt, int64_t value) {
    // Inline i2it: result = ITEM_INT_TAG | (value & MASK56)
    MIR_reg_t r = jm_new_reg(mt, "boxi", MIR_T_I64);
    uint64_t tagged = ITEM_INT_TAG | ((uint64_t)value & MASK56);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)tagged)));
    return r;
}

// Box int64 register -> Item (runtime range check)
static MIR_reg_t jm_box_int_reg(JsMirTranspiler* mt, MIR_reg_t val) {
    int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
    int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;

    MIR_reg_t result = jm_new_reg(mt, "boxi", MIR_T_I64);
    MIR_reg_t masked = jm_new_reg(mt, "mask", MIR_T_I64);
    MIR_reg_t tagged = jm_new_reg(mt, "tag", MIR_T_I64);
    MIR_reg_t le_max = jm_new_reg(mt, "le", MIR_T_I64);
    MIR_reg_t ge_min = jm_new_reg(mt, "ge", MIR_T_I64);
    MIR_reg_t in_range = jm_new_reg(mt, "rng", MIR_T_I64);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LE, MIR_new_reg_op(mt->ctx, le_max),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, INT56_MAX_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, ge_min),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, INT56_MIN_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, in_range),
        MIR_new_reg_op(mt->ctx, le_max), MIR_new_reg_op(mt->ctx, ge_min)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, (int64_t)MASK56)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, tagged),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_INT_TAG), MIR_new_reg_op(mt->ctx, masked)));

    MIR_label_t l_ok = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_ok),
        MIR_new_reg_op(mt->ctx, in_range)));
    uint64_t ITEM_ERROR_VAL = (uint64_t)LMD_TYPE_ERROR << 56;
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_ERROR_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    jm_emit_label(mt, l_ok);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, tagged)));
    jm_emit_label(mt, l_end);
    return result;
}

// Box double -> Item via push_d
static MIR_reg_t jm_box_float(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    return jm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, d_reg));
}

// Box string via s2it tagging: result = ptr ? (STR_TAG | ptr) : ITEM_NULL
static MIR_reg_t jm_box_string(JsMirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = jm_new_reg(mt, "boxs", MIR_T_I64);
    MIR_label_t l_nn = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    jm_emit_label(mt, l_nn);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)STR_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    jm_emit_label(mt, l_end);
    return result;
}

// Create a boxed string Item from a C string literal
// Calls heap_create_name(chars) -> String*, then boxes with s2it
static MIR_reg_t jm_box_string_literal(JsMirTranspiler* mt, const char* str, int len) {
    // Intern so pointer is valid at JIT runtime
    String* interned = name_pool_create_len(mt->tp->name_pool, str, len);
    MIR_reg_t ptr = jm_new_reg(mt, "cs", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, ptr),
        MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)interned->chars)));
    MIR_reg_t name_reg = jm_call_1(mt, "heap_create_name", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, ptr));
    return jm_box_string(mt, name_reg);
}

// ============================================================================
// Inline unboxing helpers (MIR instructions, no function calls)
// ============================================================================

// Unbox Item → native int64_t: sign-extend lower 56 bits
static MIR_reg_t jm_emit_unbox_int(JsMirTranspiler* mt, MIR_reg_t item) {
    MIR_reg_t result = jm_new_reg(mt, "ubi", MIR_T_I64);
    // shift left 8, arithmetic shift right 8 for sign extension
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, item), MIR_new_int_op(mt->ctx, 8)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_RSH, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, result), MIR_new_int_op(mt->ctx, 8)));
    return result;
}

// Unbox Item → native double via it2d runtime function
static MIR_reg_t jm_emit_unbox_float(JsMirTranspiler* mt, MIR_reg_t item) {
    return jm_call_1(mt, "it2d", MIR_T_D, MIR_T_I64, MIR_new_reg_op(mt->ctx, item));
}

// Convert native int64_t → native double
static MIR_reg_t jm_emit_int_to_double(JsMirTranspiler* mt, MIR_reg_t int_reg) {
    MIR_reg_t result = jm_new_reg(mt, "i2d", MIR_T_D);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, int_reg)));
    return result;
}

// Convert native double → native int64_t (truncate)
static MIR_reg_t jm_emit_double_to_int(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    MIR_reg_t result = jm_new_reg(mt, "d2i", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_D2I, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, d_reg)));
    return result;
}

// Ensure a register is native int64_t, converting from boxed if needed
static MIR_reg_t jm_ensure_native_int(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type) {
    if (src_type == LMD_TYPE_INT) return reg;  // already native int
    if (src_type == LMD_TYPE_FLOAT) return jm_emit_double_to_int(mt, reg);
    // boxed Item → unbox
    return jm_emit_unbox_int(mt, reg);
}

// Ensure a register is native double, converting from int or boxed if needed
static MIR_reg_t jm_ensure_native_float(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type) {
    if (src_type == LMD_TYPE_FLOAT) return reg;  // already native double
    if (src_type == LMD_TYPE_INT) return jm_emit_int_to_double(mt, reg);
    // boxed Item → unbox
    return jm_emit_unbox_float(mt, reg);
}

// Box a native value into an Item based on its type
static MIR_reg_t jm_box_native(JsMirTranspiler* mt, MIR_reg_t reg, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:   return jm_box_int_reg(mt, reg);
    case LMD_TYPE_FLOAT: return jm_box_float(mt, reg);
    case LMD_TYPE_BOOL: {
        MIR_reg_t result = jm_new_reg(mt, "boxb", MIR_T_I64);
        uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG), MIR_new_reg_op(mt->ctx, reg)));
        return result;
    }
    default: return reg;  // already boxed
    }
}

// ============================================================================
// Type inference for expressions (jm_get_effective_type)
// ============================================================================

// Forward declarations
static JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call);
static JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn);
// Phase 5 forward declarations
static String* jm_get_math_method(JsCallNode* call);
static TypeId jm_math_return_type(String* method, JsMirTranspiler* mt, JsAstNode* arg0);
static MIR_reg_t jm_transpile_math_native(JsMirTranspiler* mt, JsCallNode* call,
                                            String* method, TypeId target_type);
static MIR_reg_t jm_transpile_math_call(JsMirTranspiler* mt, JsCallNode* call, String* method);

// Returns the inferred TypeId for a JS AST expression node.
// LMD_TYPE_INT, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_STRING → known type
// LMD_TYPE_ANY → unknown (must use boxed path)
static TypeId jm_get_effective_type(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return LMD_TYPE_ANY;

    switch (node->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        switch (lit->literal_type) {
        case JS_LITERAL_NUMBER: {
            double val = lit->value.number_value;
            if (val == (double)(int64_t)val && val >= -36028797018963968.0 && val <= 36028797018963967.0)
                return LMD_TYPE_INT;
            return LMD_TYPE_FLOAT;
        }
        case JS_LITERAL_BOOLEAN:  return LMD_TYPE_BOOL;
        case JS_LITERAL_STRING:   return LMD_TYPE_STRING;
        case JS_LITERAL_NULL:
        case JS_LITERAL_UNDEFINED: return LMD_TYPE_NULL;
        }
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var) return var->type_id;
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        // comparison operators always return bool
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
        case JS_OP_INSTANCEOF: case JS_OP_IN:
            return LMD_TYPE_BOOL;
        default: break;
        }
        // arithmetic operators: propagate types
        TypeId left_t  = jm_get_effective_type(mt, bin->left);
        TypeId right_t = jm_get_effective_type(mt, bin->right);
        switch (bin->op) {
        case JS_OP_ADD:
            // if either is string, result is string (JS concatenation)
            if (left_t == LMD_TYPE_STRING || right_t == LMD_TYPE_STRING)
                return LMD_TYPE_STRING;
            // if either is float, result is float
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            // both int → int
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        case JS_OP_SUB: case JS_OP_MUL:
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        case JS_OP_EXP:
            // pow() always returns double
            if ((left_t == LMD_TYPE_INT || left_t == LMD_TYPE_FLOAT) &&
                (right_t == LMD_TYPE_INT || right_t == LMD_TYPE_FLOAT))
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_DIV:
            // JS division always produces float (7/2 == 3.5)
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_FLOAT;
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_MOD:
            // modulo: int%int → int; float mod handled by runtime (no native DMOD)
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        case JS_OP_BIT_AND: case JS_OP_BIT_OR: case JS_OP_BIT_XOR:
        case JS_OP_BIT_LSHIFT: case JS_OP_BIT_RSHIFT: case JS_OP_BIT_URSHIFT:
            return LMD_TYPE_INT;  // bitwise ops always produce int32
        case JS_OP_AND: case JS_OP_OR:
            return LMD_TYPE_ANY;  // logical AND/OR return one of the operands
        default:
            return LMD_TYPE_ANY;
        }
    }

    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        switch (un->op) {
        case JS_OP_NOT:    return LMD_TYPE_BOOL;
        case JS_OP_TYPEOF: return LMD_TYPE_STRING;
        case JS_OP_BIT_NOT: return LMD_TYPE_INT;
        case JS_OP_PLUS: case JS_OP_ADD: {
            TypeId t = jm_get_effective_type(mt, un->operand);
            return (t == LMD_TYPE_FLOAT) ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
        }
        case JS_OP_MINUS: case JS_OP_SUB: {
            TypeId t = jm_get_effective_type(mt, un->operand);
            if (t == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
            if (t == LMD_TYPE_INT)   return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        }
        case JS_OP_INCREMENT: case JS_OP_DECREMENT: {
            if (!un->operand) return LMD_TYPE_ANY;
            TypeId t = jm_get_effective_type(mt, un->operand);
            if (t == LMD_TYPE_INT || t == LMD_TYPE_FLOAT) return t;
            return LMD_TYPE_ANY;
        }
        default: return LMD_TYPE_ANY;
        }
    }

    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        if (asgn->op == JS_OP_ASSIGN)
            return jm_get_effective_type(mt, asgn->right);
        // compound assignment: depends on operator and operand types
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        TypeId t1 = jm_get_effective_type(mt, cond->consequent);
        TypeId t2 = jm_get_effective_type(mt, cond->alternate);
        if (t1 == t2) return t1;
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_CALL_EXPRESSION: {
        // Phase 4: If callee resolves to a function with a native version
        // and all arg types match, the call returns the function's return type
        JsCallNode* call = (JsCallNode*)node;
        JsFuncCollected* fc = jm_resolve_native_call(mt, call);
        if (fc) return fc->return_type;
        // Phase 5: If callee is Math.xxx(), resolve return type at compile time
        String* math_method = jm_get_math_method(call);
        if (math_method) return jm_math_return_type(math_method, mt, call->arguments);
        return LMD_TYPE_ANY;
    }

    default:
        return LMD_TYPE_ANY;
    }
}

// Check if a type is native (not boxed)
static bool jm_is_native_type(TypeId tid) {
    return tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL;
}

// Forward declarations for native expression transpilation
static MIR_reg_t jm_transpile_expression(JsMirTranspiler* mt, JsAstNode* expr);
static MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item);

// Transpile an expression returning a native register of the target type.
// Handles literals inline (no boxing), identifiers from typed vars, and
// recursive expressions. Falls back to unbox from boxed Item when needed.
static MIR_reg_t jm_transpile_as_native(JsMirTranspiler* mt, JsAstNode* expr,
                                         TypeId expr_type, TypeId target_type) {
    // Literals: emit native constant directly (bypass boxing)
    if (expr && expr->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            if (target_type == LMD_TYPE_FLOAT) {
                MIR_reg_t r = jm_new_reg(mt, "dlit", MIR_T_D);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, r),
                    MIR_new_double_op(mt->ctx, lit->value.number_value)));
                return r;
            } else {
                MIR_reg_t r = jm_new_reg(mt, "ilit", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)lit->value.number_value)));
                return r;
            }
        }
    }

    // Identifiers: use native register directly if variable is typed
    if (expr && expr->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)expr;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var && jm_is_native_type(var->type_id)) {
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, var->reg, var->type_id);
            else
                return jm_ensure_native_int(mt, var->reg, var->type_id);
        }
        // boxed variable: unbox
        MIR_reg_t boxed = var ? var->reg : jm_emit_null(mt);
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, boxed);
        else
            return jm_emit_unbox_int(mt, boxed);
    }

    // Other expressions: determine if jm_transpile_expression returns native.
    // Must check specifically whether the native path is actually taken.
    if (expr && expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        TypeId lt = jm_get_effective_type(mt, bin->left);
        TypeId rt = jm_get_effective_type(mt, bin->right);
        bool left_num  = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
        bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
        bool both_numeric = left_num && right_num;
        // Determine if the native path is actually taken
        bool native_binary = both_numeric &&
            bin->op != JS_OP_EXP && bin->op != JS_OP_AND && bin->op != JS_OP_OR;
        if (native_binary && bin->op == JS_OP_MOD) {
            native_binary = (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT);
        }
        // Semi-native comparisons: at least one side typed numeric
        if (!native_binary && (left_num || right_num)) {
            switch (bin->op) {
            case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
            case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
                native_binary = true;
                break;
            default: break;
            }
        }
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_binary) {
            // Native path was taken: result is native int or double
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, expr_type);
            else
                return jm_ensure_native_int(mt, result, expr_type);
        }
        // Boxed path was taken: result is boxed Item, need to unbox
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, result);
        else {
            // Use it2d + D2I for robust int extraction (handles INT, FLOAT, etc.)
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    if (expr && expr->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)expr;
        // Check if unary op takes the native path
        bool native_unary = false;
        if (un->operand) {
            TypeId op_type = jm_get_effective_type(mt, un->operand);
            bool op_numeric = (op_type == LMD_TYPE_INT || op_type == LMD_TYPE_FLOAT);
            switch (un->op) {
            case JS_OP_PLUS: case JS_OP_ADD:
            case JS_OP_MINUS: case JS_OP_SUB:
                native_unary = op_numeric;
                break;
            case JS_OP_INCREMENT: case JS_OP_DECREMENT:
                // Only native if operand is a typed identifier
                if (un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* uid = (JsIdentifierNode*)un->operand;
                    char uvname[128];
                    snprintf(uvname, sizeof(uvname), "_js_%.*s", (int)uid->name->len, uid->name->chars);
                    JsMirVarEntry* uvar = jm_find_var(mt, uvname);
                    native_unary = uvar && (uvar->type_id == LMD_TYPE_INT || uvar->type_id == LMD_TYPE_FLOAT);
                }
                break;
            default:
                break;
            }
        }
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_unary) {
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, expr_type);
            else
                return jm_ensure_native_int(mt, result, expr_type);
        }
        // Boxed result: unbox
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, result);
        else {
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    if (expr && expr->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
        JsAssignmentNode* asgn = (JsAssignmentNode*)expr;
        bool native_assign = false;
        if (asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* aid = (JsIdentifierNode*)asgn->left;
            char avname[128];
            snprintf(avname, sizeof(avname), "_js_%.*s", (int)aid->name->len, aid->name->chars);
            JsMirVarEntry* avar = jm_find_var(mt, avname);
            native_assign = avar && !avar->from_env &&
                            (avar->type_id == LMD_TYPE_INT || avar->type_id == LMD_TYPE_FLOAT);
        }
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_assign) {
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, expr_type);
            else
                return jm_ensure_native_int(mt, result, expr_type);
        }
        // Boxed result: unbox
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, result);
        else {
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    // Phase 4: Call expressions — if native call, result is already native
    if (expr && expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)expr;

        // Phase 5: Math.xxx() calls — use native C math when arg types known
        String* math_method = jm_get_math_method(call);
        if (math_method) {
            return jm_transpile_math_native(mt, call, math_method, target_type);
        }

        JsFuncCollected* fc = jm_resolve_native_call(mt, call);
        if (fc) {
            // jm_transpile_expression → jm_transpile_call → native call → native result
            MIR_reg_t result = jm_transpile_expression(mt, expr);
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, fc->return_type);
            else
                return jm_ensure_native_int(mt, result, fc->return_type);
        }
        // Non-native call: result is boxed → unbox
        MIR_reg_t boxed = jm_transpile_expression(mt, expr);
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, boxed);
        else
            return jm_emit_unbox_int(mt, boxed);
    }

    // All other expressions: get boxed value and unbox to target type
    MIR_reg_t boxed = jm_transpile_box_item(mt, expr);
    if (target_type == LMD_TYPE_FLOAT)
        return jm_emit_unbox_float(mt, boxed);
    else
        return jm_emit_unbox_int(mt, boxed);
}

// ============================================================================
// Phase 4: Native call resolution
// ============================================================================

// Check if a call expression should use the native version of a function.
// Returns the JsFuncCollected* if native call is possible, NULL otherwise.
static JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;

    // Resolve to a function declaration or expression
    NameEntry* entry = js_scope_lookup(mt->tp, id->name);
    if (!entry) entry = id->entry; // fallback to AST-resolved entry
    if (!entry || !entry->node) return NULL;

    JsFunctionNode* fn = NULL;
    JsAstNodeType ntype = ((JsAstNode*)entry->node)->node_type;
    if (ntype == JS_AST_NODE_FUNCTION_DECLARATION) {
        fn = (JsFunctionNode*)entry->node;
    } else if (ntype == JS_AST_NODE_VARIABLE_DECLARATOR) {
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)entry->node;
        if (decl->init && (decl->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION
            || decl->init->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
            fn = (JsFunctionNode*)decl->init;
        }
    }
    if (!fn) return NULL;

    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
    if (!fc || !fc->has_native_version || !fc->native_func_item) return NULL;

    // Check if all argument types at this call site match the inferred param types
    JsAstNode* arg = call->arguments;
    for (int i = 0; i < fc->param_count; i++) {
        TypeId expected = fc->param_types[i];
        TypeId actual = arg ? jm_get_effective_type(mt, arg) : LMD_TYPE_ANY;
        if (expected == LMD_TYPE_INT) {
            if (actual != LMD_TYPE_INT && actual != LMD_TYPE_BOOL) return NULL;
        } else if (expected == LMD_TYPE_FLOAT) {
            if (actual != LMD_TYPE_FLOAT && actual != LMD_TYPE_INT) return NULL;
        } else {
            return NULL; // ANY param — can't optimize
        }
        if (arg) arg = arg->next;
    }

    return fc;
}

// ============================================================================
// TCO: Tail-call detection
// ============================================================================

// Check if a JS call expression is a recursive call to the given function
static bool jm_is_recursive_call(JsCallNode* call, JsFuncCollected* fc) {
    if (!call || !call->callee || !fc || !fc->node || !fc->node->name) return false;
    if (call->callee->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    if (!id->name) return false;
    // Compare callee name against the original function name (before mangling)
    String* fn_name = fc->node->name;
    return (id->name->len == fn_name->len &&
            memcmp(id->name->chars, fn_name->chars, fn_name->len) == 0);
}

// Walk JS AST to find if there's at least one tail-recursive call.
// A tail call is: return f(...) where f is the function itself.
static bool jm_has_tail_call(JsAstNode* node, JsFuncCollected* fc) {
    if (!node || !fc) return false;
    switch (node->node_type) {
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        if (ret->argument && ret->argument->node_type == JS_AST_NODE_CALL_EXPRESSION) {
            if (jm_is_recursive_call((JsCallNode*)ret->argument, fc)) return true;
        }
        return false;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) {
            if (jm_has_tail_call(s, fc)) return true;
            s = s->next;
        }
        return false;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        return jm_has_tail_call(n->consequent, fc) || jm_has_tail_call(n->alternate, fc);
    }
    default:
        return false;
    }
}

// ============================================================================
// Local function management
// ============================================================================

static MIR_item_t jm_find_local_func(JsMirTranspiler* mt, const char* name) {
    JsLocalFuncEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    JsLocalFuncEntry* found = (JsLocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
    return found ? found->func_item : NULL;
}

static void jm_register_local_func(JsMirTranspiler* mt, const char* name, MIR_item_t func_item) {
    JsLocalFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.func_item = func_item;
    hashmap_set(mt->local_funcs, &entry);
}

// ============================================================================
// Function name generation
// ============================================================================

static void jm_make_fn_name(char* buf, int bufsize, JsFunctionNode* fn, JsMirTranspiler* mt) {
    StrBuf* sb = strbuf_new_cap(64);
    strbuf_append_str(sb, "_js_");
    if (fn->name && fn->name->chars) {
        strbuf_append_str_n(sb, fn->name->chars, fn->name->len);
    } else {
        strbuf_append_str(sb, "anon");
        strbuf_append_int(sb, mt->label_counter++);
    }
    strbuf_append_char(sb, '_');
    strbuf_append_int(sb, ts_node_start_byte(fn->base.node));
    snprintf(buf, bufsize, "%s", sb->str);
    strbuf_free(sb);
}

static int jm_count_params(JsFunctionNode* fn) {
    int count = 0;
    JsAstNode* p = fn->params;
    while (p) { count++; p = p->next; }
    return count;
}

// ============================================================================
// Forward declarations
// ============================================================================

static MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item);
static void jm_transpile_statement(JsMirTranspiler* mt, JsAstNode* stmt);

// ============================================================================
// Function collection (pre-pass) - post-order to get innermost first
// ============================================================================

static void jm_collect_functions(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return;

    switch (node->node_type) {
    case JS_AST_NODE_PROGRAM: {
        JsProgramNode* prog = (JsProgramNode*)node;
        JsAstNode* s = prog->body;
        while (s) { jm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION: {
        JsFunctionNode* fn = (JsFunctionNode*)node;
        // recurse into body first (post-order)
        if (fn->body) jm_collect_functions(mt, fn->body);
        // add this function
        if (mt->func_count < 256) {
            JsFuncCollected* e = &mt->func_entries[mt->func_count];
            e->node = fn;
            jm_make_fn_name(e->name, sizeof(e->name), fn, mt);
            e->func_item = NULL; // set during creation
            mt->func_count++;
        }
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->consequent);
        jm_collect_functions(mt, n->alternate);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->body);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_collect_functions(mt, n->init);
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->update);
        jm_collect_functions(mt, n->body);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* n = (JsExpressionStatementNode*)node;
        jm_collect_functions(mt, n->expression);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* n = (JsVariableDeclarationNode*)node;
        JsAstNode* d = n->declarations;
        while (d) { jm_collect_functions(mt, d); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* n = (JsVariableDeclaratorNode*)node;
        jm_collect_functions(mt, n->init);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* n = (JsReturnNode*)node;
        jm_collect_functions(mt, n->argument);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* n = (JsCallNode*)node;
        jm_collect_functions(mt, n->callee);
        JsAstNode* a = n->arguments;
        while (a) { jm_collect_functions(mt, a); a = a->next; }
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* n = (JsBinaryNode*)node;
        jm_collect_functions(mt, n->left);
        jm_collect_functions(mt, n->right);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* n = (JsUnaryNode*)node;
        jm_collect_functions(mt, n->operand);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* n = (JsAssignmentNode*)node;
        jm_collect_functions(mt, n->left);
        jm_collect_functions(mt, n->right);
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* n = (JsMemberNode*)node;
        jm_collect_functions(mt, n->object);
        jm_collect_functions(mt, n->property);
        break;
    }
    case JS_AST_NODE_ARRAY_EXPRESSION: {
        JsArrayNode* n = (JsArrayNode*)node;
        JsAstNode* e = n->elements;
        while (e) { jm_collect_functions(mt, e); e = e->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_EXPRESSION: {
        JsObjectNode* n = (JsObjectNode*)node;
        JsAstNode* p = n->properties;
        while (p) { jm_collect_functions(mt, p); p = p->next; }
        break;
    }
    case JS_AST_NODE_PROPERTY: {
        JsPropertyNode* n = (JsPropertyNode*)node;
        jm_collect_functions(mt, n->value);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* n = (JsConditionalNode*)node;
        jm_collect_functions(mt, n->test);
        jm_collect_functions(mt, n->consequent);
        jm_collect_functions(mt, n->alternate);
        break;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* n = (JsTemplateLiteralNode*)node;
        JsAstNode* e = n->expressions;
        while (e) { jm_collect_functions(mt, e); e = e->next; }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_collect_functions(mt, n->block);
        jm_collect_functions(mt, n->handler);
        jm_collect_functions(mt, n->finalizer);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* n = (JsCatchNode*)node;
        jm_collect_functions(mt, n->body);
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* n = (JsThrowNode*)node;
        jm_collect_functions(mt, n->argument);
        break;
    }
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* n = (JsCallNode*)node;
        jm_collect_functions(mt, n->callee);
        JsAstNode* a = n->arguments;
        while (a) { jm_collect_functions(mt, a); a = a->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        jm_collect_functions(mt, n->discriminant);
        JsAstNode* c = n->cases;
        while (c) { jm_collect_functions(mt, c); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* n = (JsSwitchCaseNode*)node;
        jm_collect_functions(mt, n->test);
        JsAstNode* s = n->consequent;
        while (s) { jm_collect_functions(mt, s); s = s->next; }
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_collect_functions(mt, n->body);
        jm_collect_functions(mt, n->test);
        break;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* n = (JsForOfNode*)node;
        jm_collect_functions(mt, n->left);
        jm_collect_functions(mt, n->right);
        jm_collect_functions(mt, n->body);
        break;
    }
    case JS_AST_NODE_CLASS_DECLARATION: {
        JsClassNode* cls = (JsClassNode*)node;
        if (cls->body && cls->body->node_type == JS_AST_NODE_BLOCK_STATEMENT && mt->class_count < 32) {
            JsClassEntry* ce = &mt->class_entries[mt->class_count];
            ce->node = cls;
            ce->name = cls->name;
            ce->method_count = 0;
            ce->constructor = NULL;

            JsBlockNode* body = (JsBlockNode*)cls->body;
            JsAstNode* m = body->statements;
            while (m) {
                if (m->node_type == JS_AST_NODE_METHOD_DEFINITION) {
                    JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)m;
                    if (md->value && (md->value->node_type == JS_AST_NODE_FUNCTION_EXPRESSION ||
                                      md->value->node_type == JS_AST_NODE_FUNCTION_DECLARATION)) {
                        JsFunctionNode* fn = (JsFunctionNode*)md->value;
                        // Recurse into method body first
                        if (fn->body) jm_collect_functions(mt, fn->body);
                        // Add as collected function
                        if (mt->func_count < 256) {
                            JsFuncCollected* fc = &mt->func_entries[mt->func_count];
                            fc->node = fn;
                            // Name: ClassName_methodName
                            String* method_name = NULL;
                            if (md->key && md->key->node_type == JS_AST_NODE_IDENTIFIER) {
                                method_name = ((JsIdentifierNode*)md->key)->name;
                            }
                            if (method_name && cls->name) {
                                snprintf(fc->name, sizeof(fc->name), "%.*s_%.*s",
                                    (int)cls->name->len, cls->name->chars,
                                    (int)method_name->len, method_name->chars);
                            } else {
                                snprintf(fc->name, sizeof(fc->name), "class_method_%d_%d",
                                    mt->class_count, ce->method_count);
                            }
                            fc->func_item = NULL;
                            fc->capture_count = 0;
                            mt->func_count++;

                            // Add to class entry
                            if (ce->method_count < 32) {
                                JsClassMethodEntry* me = &ce->methods[ce->method_count];
                                me->name = method_name;
                                me->fc = fc;
                                me->param_count = jm_count_params(fn);
                                // Detect constructor by name
                                me->is_constructor = (method_name &&
                                    method_name->len == 11 &&
                                    strncmp(method_name->chars, "constructor", 11) == 0);
                                if (me->is_constructor) {
                                    ce->constructor = me;
                                }
                                ce->method_count++;
                            }
                        }
                    }
                }
                m = m->next;
            }
            mt->class_count++;
        }
        break;
    }
    default:
        break; // leaf nodes, identifiers, literals
    }
}

// ============================================================================
// Find collected function entry by node pointer
// ============================================================================

static JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn) {
    for (int i = 0; i < mt->func_count; i++) {
        if (mt->func_entries[i].node == fn) return &mt->func_entries[i];
    }
    return NULL;
}

// Find class entry by name
static JsClassEntry* jm_find_class(JsMirTranspiler* mt, const char* name, int name_len) {
    for (int i = 0; i < mt->class_count; i++) {
        JsClassEntry* ce = &mt->class_entries[i];
        if (ce->name && (int)ce->name->len == name_len &&
            strncmp(ce->name->chars, name, name_len) == 0) {
            return ce;
        }
    }
    return NULL;
}

// ============================================================================
// Phase 4: Parameter and return type inference
// ============================================================================

// Evidence counters for parameter type inference
struct JsParamEvidence {
    int int_evidence;
    int float_evidence;
    int string_evidence;
};

// Walk an AST subtree and accumulate type evidence for parameters.
// param_names: array of parameter name strings (with _js_ prefix)
// evidence: array of evidence counters, one per parameter
// param_count: number of parameters
// self_name: function's own name for detecting recursive calls (NULL if none)
static void jm_infer_walk(JsAstNode* node, const char param_names[][128],
                          JsParamEvidence* evidence, int param_count,
                          const char* self_name) {
    if (!node) return;

    // Helper: check if an identifier is a tracked parameter and return its index
    auto find_param = [&](JsAstNode* n) -> int {
        if (!n || n->node_type != JS_AST_NODE_IDENTIFIER) return -1;
        JsIdentifierNode* id = (JsIdentifierNode*)n;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        for (int i = 0; i < param_count; i++) {
            if (strcmp(vname, param_names[i]) == 0) return i;
        }
        return -1;
    };

    // Helper: check if an expression is a numeric literal
    auto is_int_literal = [](JsAstNode* n) -> bool {
        if (!n || n->node_type != JS_AST_NODE_LITERAL) return false;
        JsLiteralNode* lit = (JsLiteralNode*)n;
        if (lit->literal_type != JS_LITERAL_NUMBER) return false;
        double val = lit->value.number_value;
        return val == (double)(int64_t)val;
    };
    auto is_float_literal = [](JsAstNode* n) -> bool {
        if (!n || n->node_type != JS_AST_NODE_LITERAL) return false;
        JsLiteralNode* lit = (JsLiteralNode*)n;
        return lit->literal_type == JS_LITERAL_NUMBER && lit->value.number_value != (double)(int64_t)lit->value.number_value;
    };

    switch (node->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        int li = find_param(bin->left);
        int ri = find_param(bin->right);
        bool is_arith = (bin->op == JS_OP_ADD || bin->op == JS_OP_SUB ||
                         bin->op == JS_OP_MUL || bin->op == JS_OP_DIV ||
                         bin->op == JS_OP_MOD || bin->op == JS_OP_EXP);
        bool is_cmp = (bin->op == JS_OP_LT || bin->op == JS_OP_LE ||
                        bin->op == JS_OP_GT || bin->op == JS_OP_GE ||
                        bin->op == JS_OP_EQ || bin->op == JS_OP_NE ||
                        bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_STRICT_NE);
        bool is_bitwise = (bin->op == JS_OP_BIT_AND || bin->op == JS_OP_BIT_OR ||
                           bin->op == JS_OP_BIT_XOR || bin->op == JS_OP_BIT_LSHIFT ||
                           bin->op == JS_OP_BIT_RSHIFT || bin->op == JS_OP_BIT_URSHIFT);

        if (is_arith || is_cmp) {
            // Parameter used in arithmetic/comparison with int literal → int evidence
            if (li >= 0 && is_int_literal(bin->right)) evidence[li].int_evidence++;
            if (ri >= 0 && is_int_literal(bin->left))  evidence[ri].int_evidence++;
            // Parameter used in arithmetic/comparison with float literal → float evidence
            if (li >= 0 && is_float_literal(bin->right)) evidence[li].float_evidence++;
            if (ri >= 0 && is_float_literal(bin->left))  evidence[ri].float_evidence++;
            // Two params in arithmetic together → evidence for both based on context
            // (if either has int evidence, the other gets int evidence too)
            if (li >= 0 && ri >= 0) {
                evidence[li].int_evidence++;
                evidence[ri].int_evidence++;
            }
        }
        if (is_bitwise) {
            // Bitwise ops always produce/expect int
            if (li >= 0) evidence[li].int_evidence++;
            if (ri >= 0) evidence[ri].int_evidence++;
        }
        jm_infer_walk(bin->left, param_names, evidence, param_count, self_name);
        jm_infer_walk(bin->right, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        int oi = find_param(un->operand);
        if (oi >= 0) {
            switch (un->op) {
            case JS_OP_PLUS: case JS_OP_ADD:
            case JS_OP_MINUS: case JS_OP_SUB:
            case JS_OP_INCREMENT: case JS_OP_DECREMENT:
            case JS_OP_BIT_NOT:
                evidence[oi].int_evidence++;
                break;
            default: break;
            }
        }
        jm_infer_walk(un->operand, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        // Check if this is a recursive call — args passed to self propagate evidence
        if (self_name && call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* cid = (JsIdentifierNode*)call->callee;
            char cname[128];
            snprintf(cname, sizeof(cname), "_js_%.*s", (int)cid->name->len, cid->name->chars);
            if (strncmp(cname, self_name, strlen(self_name)) == 0) {
                // Recursive call: each argument position inherits the param's evidence
                JsAstNode* arg = call->arguments;
                for (int pi = 0; pi < param_count && arg; pi++, arg = arg->next) {
                    int ai = find_param(arg);
                    if (ai >= 0) {
                        // param passed directly to same position → reinforce
                        evidence[ai].int_evidence++;
                    }
                }
            }
        }
        jm_infer_walk(call->callee, param_names, evidence, param_count, self_name);
        JsAstNode* a = call->arguments;
        while (a) { jm_infer_walk(a, param_names, evidence, param_count, self_name); a = a->next; }
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* mem = (JsMemberNode*)node;
        // arr[param] → param is likely int (used as index)
        if (mem->computed) {
            int pi = find_param(mem->property);
            if (pi >= 0) evidence[pi].int_evidence++;
        }
        jm_infer_walk(mem->object, param_names, evidence, param_count, self_name);
        jm_infer_walk(mem->property, param_names, evidence, param_count, self_name);
        break;
    }
    // Recurse into sub-expressions
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_infer_walk(s, param_names, evidence, param_count, self_name); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_infer_walk(n->test, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->consequent, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->alternate, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_infer_walk(n->test, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->body, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_infer_walk(n->init, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->test, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->update, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->body, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* n = (JsReturnNode*)node;
        jm_infer_walk(n->argument, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* n = (JsVariableDeclarationNode*)node;
        JsAstNode* d = n->declarations;
        while (d) { jm_infer_walk(d, param_names, evidence, param_count, self_name); d = d->next; }
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* n = (JsVariableDeclaratorNode*)node;
        jm_infer_walk(n->init, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* n = (JsExpressionStatementNode*)node;
        jm_infer_walk(n->expression, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* n = (JsAssignmentNode*)node;
        jm_infer_walk(n->left, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->right, param_names, evidence, param_count, self_name);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* n = (JsConditionalNode*)node;
        jm_infer_walk(n->test, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->consequent, param_names, evidence, param_count, self_name);
        jm_infer_walk(n->alternate, param_names, evidence, param_count, self_name);
        break;
    }
    default: break;
    }
}

// Infer parameter types for a collected function from body usage patterns.
static void jm_infer_param_types(JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    int pc = jm_count_params(fn);
    fc->param_count = pc;
    if (pc == 0 || pc > 16) return;

    // Build parameter name array
    char param_names[16][128];
    JsAstNode* p = fn->params;
    for (int i = 0; i < pc && p; i++, p = p->next) {
        if (p->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* pid = (JsIdentifierNode*)p;
            snprintf(param_names[i], 128, "_js_%.*s", (int)pid->name->len, pid->name->chars);
        } else {
            snprintf(param_names[i], 128, "_js_p%d", i);
        }
    }

    // Build self-name for recursive call detection
    char self_name[128] = {0};
    if (fn->name && fn->name->chars) {
        snprintf(self_name, sizeof(self_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
    }

    // Accumulate evidence
    JsParamEvidence evidence[16] = {};
    jm_infer_walk(fn->body, param_names, evidence, pc,
                  self_name[0] ? self_name : NULL);

    // Resolve: int_evidence > 0 && float_evidence == 0 → INT
    //          float_evidence > 0 → FLOAT
    //          otherwise → ANY
    for (int i = 0; i < pc; i++) {
        if (evidence[i].float_evidence > 0) {
            fc->param_types[i] = LMD_TYPE_FLOAT;
        } else if (evidence[i].int_evidence > 0 && evidence[i].string_evidence == 0) {
            fc->param_types[i] = LMD_TYPE_INT;
        } else {
            fc->param_types[i] = LMD_TYPE_ANY;
        }
    }

    log_debug("js-mir P4: inferred param types for %s: [%s%s%s%s]",
        fc->name,
        pc > 0 ? (fc->param_types[0] == LMD_TYPE_INT ? "INT" : fc->param_types[0] == LMD_TYPE_FLOAT ? "FLOAT" : "ANY") : "",
        pc > 1 ? (fc->param_types[1] == LMD_TYPE_INT ? ",INT" : fc->param_types[1] == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
        pc > 2 ? (fc->param_types[2] == LMD_TYPE_INT ? ",INT" : fc->param_types[2] == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
        pc > 3 ? ",..." : "");
}

// Infer return type by collecting types from all return statements.
// For recursive calls to self, assume result type matches the base-case return type.
static void jm_infer_return_type_walk(JsAstNode* node, const char* self_name,
                                       TypeId* collected, int* count, int max_count) {
    if (!node || *count >= max_count) return;

    switch (node->node_type) {
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        if (!ret->argument) {
            collected[(*count)++] = LMD_TYPE_NULL;
            return;
        }
        // Determine expression type statically
        JsAstNode* expr = ret->argument;
        TypeId t = LMD_TYPE_ANY;

        if (expr->node_type == JS_AST_NODE_LITERAL) {
            JsLiteralNode* lit = (JsLiteralNode*)expr;
            switch (lit->literal_type) {
            case JS_LITERAL_NUMBER: {
                double val = lit->value.number_value;
                t = (val == (double)(int64_t)val) ? LMD_TYPE_INT : LMD_TYPE_FLOAT;
                break;
            }
            case JS_LITERAL_BOOLEAN: t = LMD_TYPE_BOOL; break;
            case JS_LITERAL_STRING:  t = LMD_TYPE_STRING; break;
            default: break;
            }
        } else if (expr->node_type == JS_AST_NODE_IDENTIFIER) {
            // Can't resolve variable type without scope — use ANY
            // But for parameter names that will be typed, we'll treat them specially
            t = LMD_TYPE_ANY; // will be refined later
        } else if (expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
            JsBinaryNode* bin = (JsBinaryNode*)expr;
            switch (bin->op) {
            case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
            case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
                t = LMD_TYPE_BOOL; break;
            case JS_OP_ADD: case JS_OP_SUB: case JS_OP_MUL: case JS_OP_MOD:
                t = LMD_TYPE_INT; break; // approximate — may be float
            case JS_OP_DIV: case JS_OP_EXP:
                t = LMD_TYPE_FLOAT; break;
            default: break;
            }
        } else if (expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
            // Recursive call: assume same return type (will be validated)
            JsCallNode* call = (JsCallNode*)expr;
            if (self_name && call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* cid = (JsIdentifierNode*)call->callee;
                char cn[128];
                snprintf(cn, sizeof(cn), "_js_%.*s", (int)cid->name->len, cid->name->chars);
                if (strncmp(cn, self_name, strlen(self_name)) == 0) {
                    t = LMD_TYPE_INT; // provisional: recursive calls return INT
                }
            }
        }
        collected[(*count)++] = t;
        return;
    }
    // Recurse into sub-statements (but NOT into nested function bodies)
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_infer_return_type_walk(s, self_name, collected, count, max_count); s = s->next; }
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_infer_return_type_walk(n->consequent, self_name, collected, count, max_count);
        jm_infer_return_type_walk(n->alternate, self_name, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_infer_return_type_walk(n->body, self_name, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_infer_return_type_walk(n->body, self_name, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_infer_return_type_walk(n->block, self_name, collected, count, max_count);
        jm_infer_return_type_walk(n->handler, self_name, collected, count, max_count);
        jm_infer_return_type_walk(n->finalizer, self_name, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* n = (JsCatchNode*)node;
        jm_infer_return_type_walk(n->body, self_name, collected, count, max_count);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        JsAstNode* c = n->cases;
        while (c) { jm_infer_return_type_walk(c, self_name, collected, count, max_count); c = c->next; }
        break;
    }
    case JS_AST_NODE_SWITCH_CASE: {
        JsSwitchCaseNode* n = (JsSwitchCaseNode*)node;
        JsAstNode* s = n->consequent;
        while (s) { jm_infer_return_type_walk(s, self_name, collected, count, max_count); s = s->next; }
        break;
    }
    default: break;
    }
}

static void jm_infer_return_type(JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    fc->return_type = LMD_TYPE_ANY;

    char self_name[128] = {0};
    if (fn->name && fn->name->chars) {
        snprintf(self_name, sizeof(self_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
    }

    // For expression-body arrow functions: infer from the expression directly
    if (fn->body && fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        // Arrow function with expression body
        if (fn->body->node_type == JS_AST_NODE_LITERAL) {
            JsLiteralNode* lit = (JsLiteralNode*)fn->body;
            if (lit->literal_type == JS_LITERAL_NUMBER) {
                double val = lit->value.number_value;
                fc->return_type = (val == (double)(int64_t)val) ? LMD_TYPE_INT : LMD_TYPE_FLOAT;
            }
        }
        return;
    }

    TypeId collected[32];
    int count = 0;
    jm_infer_return_type_walk(fn->body, self_name[0] ? self_name : NULL,
                               collected, &count, 32);

    if (count == 0) {
        fc->return_type = LMD_TYPE_NULL; // no return statements → returns undefined
        return;
    }

    // Unify: all must agree (ignore ANY from expressions we couldn't resolve)
    TypeId unified = LMD_TYPE_ANY;
    bool has_concrete = false;
    for (int i = 0; i < count; i++) {
        if (collected[i] == LMD_TYPE_ANY) continue;
        if (collected[i] == LMD_TYPE_NULL) continue; // undefined returns are compatible
        if (!has_concrete) {
            unified = collected[i];
            has_concrete = true;
        } else if (collected[i] != unified) {
            // Conflicting types
            if ((unified == LMD_TYPE_INT && collected[i] == LMD_TYPE_FLOAT) ||
                (unified == LMD_TYPE_FLOAT && collected[i] == LMD_TYPE_INT)) {
                unified = LMD_TYPE_FLOAT; // int + float → float
            } else {
                fc->return_type = LMD_TYPE_ANY;
                return;
            }
        }
    }

    if (has_concrete) {
        fc->return_type = unified;
    }

    log_debug("js-mir P4: inferred return type for %s: %s", fc->name,
        fc->return_type == LMD_TYPE_INT ? "INT" :
        fc->return_type == LMD_TYPE_FLOAT ? "FLOAT" : "ANY");
}

// ============================================================================
// Argument array allocation helper
// ============================================================================

// Allocates stack space for an Item[] args array, stores evaluated args,
// returns register pointing to the array. If arg_count == 0, returns 0.
static MIR_reg_t jm_build_args_array(JsMirTranspiler* mt, JsAstNode* first_arg, int arg_count) {
    if (arg_count == 0) return 0;

    // Allocate stack space: ALLOCA(arg_count * 8)
    MIR_reg_t args_ptr = jm_new_reg(mt, "args", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ALLOCA,
        MIR_new_reg_op(mt->ctx, args_ptr),
        MIR_new_int_op(mt->ctx, arg_count * 8)));

    // Evaluate and store each argument
    JsAstNode* arg = first_arg;
    for (int i = 0; i < arg_count && arg; i++) {
        MIR_reg_t val = jm_transpile_box_item(mt, arg);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, i * 8, args_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        arg = arg->next;
    }

    return args_ptr;
}

static int jm_count_args(JsAstNode* arg) {
    int count = 0;
    while (arg) { count++; arg = arg->next; }
    return count;
}

// ============================================================================
// Expression transpilers - each returns MIR_reg_t holding boxed Item result
// ============================================================================

// Forward declarations for transpiler functions defined later
static MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call);
static void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw);
static void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw);
static void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo);

static MIR_reg_t jm_transpile_literal(JsMirTranspiler* mt, JsLiteralNode* lit) {
    switch (lit->literal_type) {
    case JS_LITERAL_NUMBER: {
        double val = lit->value.number_value;
        // check if value is an integer
        if (val == (double)(int64_t)val && val >= -36028797018963968.0 && val <= 36028797018963967.0) {
            return jm_box_int_const(mt, (int64_t)val);
        } else {
            MIR_reg_t d = jm_new_reg(mt, "dbl", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                MIR_new_reg_op(mt->ctx, d),
                MIR_new_double_op(mt->ctx, val)));
            return jm_box_float(mt, d);
        }
    }
    case JS_LITERAL_STRING: {
        String* sv = lit->value.string_value;
        return jm_box_string_literal(mt, sv->chars, sv->len);
    }
    case JS_LITERAL_BOOLEAN: {
        MIR_reg_t r = jm_new_reg(mt, "bool", MIR_T_I64);
        uint64_t bval = lit->value.boolean_value ? ITEM_TRUE_VAL : ITEM_FALSE_VAL;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, r), MIR_new_int_op(mt->ctx, (int64_t)bval)));
        return r;
    }
    case JS_LITERAL_NULL:
    case JS_LITERAL_UNDEFINED:
        return jm_emit_null(mt);
    }
    return jm_emit_null(mt);
}

static MIR_reg_t jm_transpile_identifier(JsMirTranspiler* mt, JsIdentifierNode* id) {
    // Handle 'this' keyword: call js_get_this()
    if (id->name->len == 4 && strncmp(id->name->chars, "this", 4) == 0) {
        return jm_call_0(mt, "js_get_this", MIR_T_I64);
    }

    // Build variable name: _js_<name>
    char vname[128];
    snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
    JsMirVarEntry* var = jm_find_var(mt, vname);
    if (var) return var->reg;

    log_error("js-mir: undefined variable '%s'", vname);
    return jm_emit_null(mt);
}

// Binary expression: native arithmetic fast path + boxed fallback
static MIR_reg_t jm_transpile_binary(JsMirTranspiler* mt, JsBinaryNode* bin) {
    // --- Native arithmetic fast path ---
    TypeId left_type  = jm_get_effective_type(mt, bin->left);
    TypeId right_type = jm_get_effective_type(mt, bin->right);

    bool both_numeric = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT) &&
                        (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT);

    if (both_numeric) {
        bool use_float = (left_type == LMD_TYPE_FLOAT || right_type == LMD_TYPE_FLOAT);
        bool both_int  = (left_type == LMD_TYPE_INT && right_type == LMD_TYPE_INT);

        switch (bin->op) {
        // Arithmetic operators
        case JS_OP_ADD: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "add", use_float ? MIR_T_D : MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DADD : MIR_ADD,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_SUB: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "sub", use_float ? MIR_T_D : MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DSUB : MIR_SUB,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_MUL: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "mul", use_float ? MIR_T_D : MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DMUL : MIR_MUL,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_DIV: {
            // JS division always produces float (7/2 === 3.5)
            if (both_int) {
                MIR_reg_t dl = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_FLOAT);
                MIR_reg_t dr = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_FLOAT);
                MIR_reg_t r = jm_new_reg(mt, "div", MIR_T_D);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DDIV,
                    MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, dl), MIR_new_reg_op(mt->ctx, dr)));
                return r;
            } else {
                MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_FLOAT);
                MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_FLOAT);
                MIR_reg_t r = jm_new_reg(mt, "div", MIR_T_D);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DDIV,
                    MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
                return r;
            }
        }
        case JS_OP_MOD: {
            if (both_int) {
                MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_INT);
                MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_INT);
                MIR_reg_t r = jm_new_reg(mt, "mod", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOD,
                    MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
                return r;
            }
            break;  // float modulo → fall through to boxed runtime
        }
        case JS_OP_EXP:
            break;  // power → fall through to boxed runtime (no native MIR op)

        // Comparison operators: return native int (0 or 1)
        case JS_OP_LT: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "lt", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DLT : MIR_LTS,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_LE: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "le", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DLE : MIR_LES,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_GT: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "gt", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DGT : MIR_GTS,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_GE: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "ge", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DGE : MIR_GES,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_EQ:
        case JS_OP_STRICT_EQ: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "eq", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DEQ : MIR_EQ,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
        case JS_OP_NE:
        case JS_OP_STRICT_NE: {
            TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "ne", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, use_float ? MIR_DNE : MIR_NE,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }

        // Bitwise operators: always truncate to int, always return int
        case JS_OP_BIT_AND: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_INT);
            MIR_reg_t r = jm_new_reg(mt, "band", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, li), MIR_new_reg_op(mt->ctx, ri)));
            return r;
        }
        case JS_OP_BIT_OR: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_INT);
            MIR_reg_t r = jm_new_reg(mt, "bor", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, li), MIR_new_reg_op(mt->ctx, ri)));
            return r;
        }
        case JS_OP_BIT_XOR: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_INT);
            MIR_reg_t r = jm_new_reg(mt, "bxor", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_XOR,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, li), MIR_new_reg_op(mt->ctx, ri)));
            return r;
        }
        case JS_OP_BIT_LSHIFT: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_INT);
            // JS: ToInt32(li) << (ToUint32(ri) & 0x1F), result is signed 32-bit
            MIR_reg_t r = jm_new_reg(mt, "lsh", MIR_T_I64);
            MIR_reg_t r32 = jm_new_reg(mt, "lsh32", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LSH,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, li), MIR_new_reg_op(mt->ctx, ri)));
            // Sign-extend result to 32-bit: (r << 32) >> 32
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LSH,
                MIR_new_reg_op(mt->ctx, r32), MIR_new_reg_op(mt->ctx, r), MIR_new_int_op(mt->ctx, 32)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_RSH,
                MIR_new_reg_op(mt->ctx, r32), MIR_new_reg_op(mt->ctx, r32), MIR_new_int_op(mt->ctx, 32)));
            return r32;
        }
        case JS_OP_BIT_RSHIFT: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_INT);
            // JS: ToInt32(li) >> (ToUint32(ri) & 0x1F) — sign-extend li to 32-bit first
            MIR_reg_t li32 = jm_new_reg(mt, "rli32", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LSH,
                MIR_new_reg_op(mt->ctx, li32), MIR_new_reg_op(mt->ctx, li), MIR_new_int_op(mt->ctx, 32)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_RSH,
                MIR_new_reg_op(mt->ctx, li32), MIR_new_reg_op(mt->ctx, li32), MIR_new_int_op(mt->ctx, 32)));
            MIR_reg_t r = jm_new_reg(mt, "rsh", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_RSH,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, li32), MIR_new_reg_op(mt->ctx, ri)));
            return r;
        }
        case JS_OP_BIT_URSHIFT: {
            MIR_reg_t li = jm_transpile_as_native(mt, bin->left, left_type, LMD_TYPE_INT);
            MIR_reg_t ri = jm_transpile_as_native(mt, bin->right, right_type, LMD_TYPE_INT);
            // JS: ToUint32(li) >>> (ToUint32(ri) & 0x1F) — mask to 32-bit unsigned first
            MIR_reg_t li32 = jm_new_reg(mt, "uli32", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND,
                MIR_new_reg_op(mt->ctx, li32), MIR_new_reg_op(mt->ctx, li),
                MIR_new_int_op(mt->ctx, (int64_t)0xFFFFFFFFLL)));
            MIR_reg_t r = jm_new_reg(mt, "ursh", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_URSH,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, li32), MIR_new_reg_op(mt->ctx, ri)));
            return r;
        }
        default:
            break;  // fall through to boxed runtime path
        }
    }

    // --- Semi-native comparison path ---
    // When at least one operand is typed numeric, we can unbox the other side
    // and emit a native comparison. This avoids boxing the typed side + 2 runtime calls.
    bool any_numeric = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT) ||
                       (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT);
    if (!both_numeric && any_numeric) {
        bool use_float = (left_type == LMD_TYPE_FLOAT || right_type == LMD_TYPE_FLOAT);
        TypeId arith_t = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
        MIR_insn_code_t cmp_insn = (MIR_insn_code_t)0;

        switch (bin->op) {
        case JS_OP_LT:        cmp_insn = use_float ? MIR_DLT : MIR_LTS; break;
        case JS_OP_LE:        cmp_insn = use_float ? MIR_DLE : MIR_LES; break;
        case JS_OP_GT:        cmp_insn = use_float ? MIR_DGT : MIR_GTS; break;
        case JS_OP_GE:        cmp_insn = use_float ? MIR_DGE : MIR_GES; break;
        case JS_OP_EQ:
        case JS_OP_STRICT_EQ: cmp_insn = use_float ? MIR_DEQ : MIR_EQ;  break;
        case JS_OP_NE:
        case JS_OP_STRICT_NE: cmp_insn = use_float ? MIR_DNE : MIR_NE;  break;
        default: break;
        }

        if (cmp_insn) {
            MIR_reg_t fl = jm_transpile_as_native(mt, bin->left, left_type, arith_t);
            MIR_reg_t fr = jm_transpile_as_native(mt, bin->right, right_type, arith_t);
            MIR_reg_t r = jm_new_reg(mt, "scmp", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, cmp_insn,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, fl), MIR_new_reg_op(mt->ctx, fr)));
            return r;
        }
    }

    // --- Boxed runtime path (original) ---
    const char* fn_name = NULL;
    switch (bin->op) {
    case JS_OP_ADD:        fn_name = "js_add"; break;
    case JS_OP_SUB:        fn_name = "js_subtract"; break;
    case JS_OP_MUL:        fn_name = "js_multiply"; break;
    case JS_OP_DIV:        fn_name = "js_divide"; break;
    case JS_OP_MOD:        fn_name = "js_modulo"; break;
    case JS_OP_EXP:        fn_name = "js_power"; break;
    case JS_OP_EQ:         fn_name = "js_equal"; break;
    case JS_OP_NE:         fn_name = "js_not_equal"; break;
    case JS_OP_STRICT_EQ:  fn_name = "js_strict_equal"; break;
    case JS_OP_STRICT_NE:  fn_name = "js_strict_not_equal"; break;
    case JS_OP_LT:         fn_name = "js_less_than"; break;
    case JS_OP_LE:         fn_name = "js_less_equal"; break;
    case JS_OP_GT:         fn_name = "js_greater_than"; break;
    case JS_OP_GE:         fn_name = "js_greater_equal"; break;
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
    MIR_reg_t left = jm_transpile_box_item(mt, bin->left);
    MIR_reg_t right = jm_transpile_box_item(mt, bin->right);
    return jm_call_2(mt, fn_name, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, left),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, right));
}

// Unary expression
static MIR_reg_t jm_transpile_unary(JsMirTranspiler* mt, JsUnaryNode* un) {
    switch (un->op) {
    case JS_OP_NOT:
        return jm_call_1(mt, "js_logical_not", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_BIT_NOT:
        return jm_call_1(mt, "js_bitwise_not", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_TYPEOF:
        return jm_call_1(mt, "js_typeof", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    case JS_OP_PLUS:
    case JS_OP_ADD: {
        TypeId op_type = jm_get_effective_type(mt, un->operand);
        if (op_type == LMD_TYPE_INT) {
            return jm_transpile_as_native(mt, un->operand, op_type, LMD_TYPE_INT);
        }
        if (op_type == LMD_TYPE_FLOAT) {
            return jm_transpile_as_native(mt, un->operand, op_type, LMD_TYPE_FLOAT);
        }
        return jm_call_1(mt, "js_unary_plus", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    }
    case JS_OP_MINUS:
    case JS_OP_SUB: {
        TypeId op_type = jm_get_effective_type(mt, un->operand);
        if (op_type == LMD_TYPE_INT) {
            MIR_reg_t val = jm_transpile_as_native(mt, un->operand, op_type, LMD_TYPE_INT);
            MIR_reg_t r = jm_new_reg(mt, "neg", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_NEG,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, val)));
            return r;
        }
        if (op_type == LMD_TYPE_FLOAT) {
            MIR_reg_t val = jm_transpile_as_native(mt, un->operand, op_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_new_reg(mt, "dneg", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DNEG,
                MIR_new_reg_op(mt->ctx, r), MIR_new_reg_op(mt->ctx, val)));
            return r;
        }
        return jm_call_1(mt, "js_unary_minus", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, jm_transpile_box_item(mt, un->operand)));
    }
    case JS_OP_INCREMENT: {
        // ++var or var++ — native fast path for typed variables
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (var && var->type_id == LMD_TYPE_INT) {
                // native int: var = var + 1
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_int_op(mt->ctx, 1)));
                return var->reg;
            }
            if (var && var->type_id == LMD_TYPE_FLOAT) {
                // native float: var = var + 1.0
                MIR_reg_t one = jm_new_reg(mt, "one", MIR_T_D);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, one),
                    MIR_new_double_op(mt->ctx, 1.0)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DADD,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, one)));
                return var->reg;
            }
        }
        // boxed fallback
        MIR_reg_t operand = jm_transpile_box_item(mt, un->operand);
        MIR_reg_t one = jm_box_int_const(mt, 1);
        MIR_reg_t result = jm_call_2(mt, "js_add", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, operand),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, one));
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (var) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, result)));
                if (var->from_env) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, var->env_slot * (int)sizeof(uint64_t), var->env_reg, 0, 1),
                        MIR_new_reg_op(mt->ctx, var->reg)));
                }
            }
        }
        return result;
    }
    case JS_OP_DECREMENT: {
        // --var or var-- — native fast path for typed variables
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (var && var->type_id == LMD_TYPE_INT) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_SUB,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_int_op(mt->ctx, 1)));
                return var->reg;
            }
            if (var && var->type_id == LMD_TYPE_FLOAT) {
                MIR_reg_t one = jm_new_reg(mt, "one", MIR_T_D);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, one),
                    MIR_new_double_op(mt->ctx, 1.0)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DSUB,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, one)));
                return var->reg;
            }
        }
        // boxed fallback
        MIR_reg_t operand = jm_transpile_box_item(mt, un->operand);
        MIR_reg_t one = jm_box_int_const(mt, 1);
        MIR_reg_t result = jm_call_2(mt, "js_subtract", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, operand),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, one));
        if (un->operand && un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)un->operand;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
            JsMirVarEntry* var = jm_find_var(mt, vname);
            if (var) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, result)));
                if (var->from_env) {
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_mem_op(mt->ctx, MIR_T_I64, var->env_slot * (int)sizeof(uint64_t), var->env_reg, 0, 1),
                        MIR_new_reg_op(mt->ctx, var->reg)));
                }
            }
        }
        return result;
    }
    case JS_OP_VOID: {
        // Evaluate for side effects, return null
        jm_transpile_box_item(mt, un->operand);
        return jm_emit_null(mt);
    }
    case JS_OP_DELETE:
        return jm_box_int_const(mt, 1); // simplified: always true
    default:
        log_error("js-mir: unknown unary op %d", un->op);
        return jm_emit_null(mt);
    }
}

// Assignment expression
static MIR_reg_t jm_transpile_assignment(JsMirTranspiler* mt, JsAssignmentNode* asgn) {
    if (!asgn->left || !asgn->right) return jm_emit_null(mt);

    // Simple variable assignment: x = expr
    if (asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)asgn->left;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (!var) {
            log_error("js-mir: assignment to undefined var '%s'", vname);
            return jm_emit_null(mt);
        }

        // --- Native-typed variable fast path ---
        if (var->type_id == LMD_TYPE_INT && !var->from_env) {
            if (asgn->op == JS_OP_ASSIGN) {
                TypeId rhs_type = jm_get_effective_type(mt, asgn->right);
                MIR_reg_t rhs = jm_transpile_as_native(mt, asgn->right, rhs_type, LMD_TYPE_INT);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, var->reg), MIR_new_reg_op(mt->ctx, rhs)));
            } else {
                // Compound assignment on native int
                TypeId rhs_type = jm_get_effective_type(mt, asgn->right);
                MIR_reg_t rval = jm_transpile_as_native(mt, asgn->right, rhs_type, LMD_TYPE_INT);
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
            return var->reg;
        }

        if (var->type_id == LMD_TYPE_FLOAT && !var->from_env) {
            if (asgn->op == JS_OP_ASSIGN) {
                TypeId rhs_type = jm_get_effective_type(mt, asgn->right);
                MIR_reg_t rhs = jm_transpile_as_native(mt, asgn->right, rhs_type, LMD_TYPE_FLOAT);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, var->reg), MIR_new_reg_op(mt->ctx, rhs)));
            } else {
                TypeId rhs_type = jm_get_effective_type(mt, asgn->right);
                MIR_reg_t rval = jm_transpile_as_native(mt, asgn->right, rhs_type, LMD_TYPE_FLOAT);
                MIR_insn_code_t op = MIR_DADD;
                switch (asgn->op) {
                case JS_OP_ADD_ASSIGN: op = MIR_DADD; break;
                case JS_OP_SUB_ASSIGN: op = MIR_DSUB; break;
                case JS_OP_MUL_ASSIGN: op = MIR_DMUL; break;
                case JS_OP_DIV_ASSIGN: op = MIR_DDIV; break;
                default: break;
                }
                jm_emit(mt, MIR_new_insn(mt->ctx, op,
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, var->reg),
                    MIR_new_reg_op(mt->ctx, rval)));
            }
            return var->reg;
        }

        // --- Boxed variable path (original) ---
        MIR_reg_t rhs;
        if (asgn->op == JS_OP_ASSIGN) {
            rhs = jm_transpile_box_item(mt, asgn->right);
        } else {
            // Compound assignment: var op= expr -> var = js_op(var, expr)
            MIR_reg_t rval = jm_transpile_box_item(mt, asgn->right);
            const char* fn = NULL;
            switch (asgn->op) {
            case JS_OP_ADD_ASSIGN: fn = "js_add"; break;
            case JS_OP_SUB_ASSIGN: fn = "js_subtract"; break;
            case JS_OP_MUL_ASSIGN: fn = "js_multiply"; break;
            case JS_OP_DIV_ASSIGN: fn = "js_divide"; break;
            case JS_OP_MOD_ASSIGN: fn = "js_modulo"; break;
            case JS_OP_EXP_ASSIGN: fn = "js_power"; break;
            case JS_OP_BIT_AND_ASSIGN: fn = "js_bitwise_and"; break;
            case JS_OP_BIT_OR_ASSIGN: fn = "js_bitwise_or"; break;
            case JS_OP_BIT_XOR_ASSIGN: fn = "js_bitwise_xor"; break;
            case JS_OP_LSHIFT_ASSIGN: fn = "js_left_shift"; break;
            case JS_OP_RSHIFT_ASSIGN: fn = "js_right_shift"; break;
            case JS_OP_URSHIFT_ASSIGN: fn = "js_unsigned_right_shift"; break;
            default: fn = "js_add"; break;
            }
            rhs = jm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, var->reg),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, rval));
        }

        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, var->reg),
            MIR_new_reg_op(mt->ctx, rhs)));

        // Write-back to env if this is a captured variable
        if (var->from_env) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, var->env_slot * (int)sizeof(uint64_t), var->env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, var->reg)));
        }

        return var->reg;
    }

    // Member assignment: obj.prop = expr, obj[key] = expr
    if (asgn->left->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* member = (JsMemberNode*)asgn->left;

        // Detect chained member: obj.style.prop = val -> js_dom_set_style_property
        if (!member->computed && member->object &&
            member->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* outer = (JsMemberNode*)member->object;
            if (!outer->computed && outer->property &&
                outer->property->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* mid_prop = (JsIdentifierNode*)outer->property;
                if (mid_prop->name && mid_prop->name->len == 5 &&
                    strncmp(mid_prop->name->chars, "style", 5) == 0 &&
                    member->property &&
                    member->property->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* style_prop = (JsIdentifierNode*)member->property;
                    MIR_reg_t obj = jm_transpile_box_item(mt, outer->object);
                    MIR_reg_t key = jm_box_string_literal(mt, style_prop->name->chars, style_prop->name->len);
                    MIR_reg_t val = jm_transpile_box_item(mt, asgn->right);
                    return jm_call_3(mt, "js_dom_set_style_property", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
                }
            }
        }

        // General member assignment
        MIR_reg_t obj = jm_transpile_box_item(mt, member->object);
        MIR_reg_t key;
        if (member->computed) {
            key = jm_transpile_box_item(mt, member->property);
        } else if (member->property && member->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)member->property;
            key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
        } else {
            key = jm_transpile_box_item(mt, member->property);
        }

        MIR_reg_t new_val;
        if (asgn->op == JS_OP_ASSIGN) {
            new_val = jm_transpile_box_item(mt, asgn->right);
        } else {
            // Compound: get current value, apply operation, set result
            MIR_reg_t cur_val = jm_call_2(mt, "js_property_access", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            MIR_reg_t rval = jm_transpile_box_item(mt, asgn->right);
            const char* fn = NULL;
            switch (asgn->op) {
            case JS_OP_ADD_ASSIGN: fn = "js_add"; break;
            case JS_OP_SUB_ASSIGN: fn = "js_subtract"; break;
            case JS_OP_MUL_ASSIGN: fn = "js_multiply"; break;
            case JS_OP_DIV_ASSIGN: fn = "js_divide"; break;
            case JS_OP_MOD_ASSIGN: fn = "js_modulo"; break;
            case JS_OP_EXP_ASSIGN: fn = "js_power"; break;
            case JS_OP_BIT_AND_ASSIGN: fn = "js_bitwise_and"; break;
            case JS_OP_BIT_OR_ASSIGN: fn = "js_bitwise_or"; break;
            case JS_OP_BIT_XOR_ASSIGN: fn = "js_bitwise_xor"; break;
            case JS_OP_LSHIFT_ASSIGN: fn = "js_left_shift"; break;
            case JS_OP_RSHIFT_ASSIGN: fn = "js_right_shift"; break;
            case JS_OP_URSHIFT_ASSIGN: fn = "js_unsigned_right_shift"; break;
            default: fn = "js_add"; break;
            }
            new_val = jm_call_2(mt, fn, MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, cur_val),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, rval));
        }

        return jm_call_3(mt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, new_val));
    }

    log_error("js-mir: unsupported assignment target %d", asgn->left->node_type);
    return jm_emit_null(mt);
}

// ============================================================================
// Call expression helpers
// ============================================================================

static bool jm_is_console_log(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    if (!m->property || m->property->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
    return obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "console", 7) == 0 &&
           prop->name && prop->name->len == 3 && strncmp(prop->name->chars, "log", 3) == 0;
}

static bool jm_is_math_call(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    return obj->name && obj->name->len == 4 && strncmp(obj->name->chars, "Math", 4) == 0;
}

// ============================================================================
// Phase 5: Compile-time Math method resolution
// ============================================================================
// Instead of boxing method name string + building args array + calling js_math_method
// (which does sequential strncmp), resolve the method at compile time and call the
// specific Lambda function directly. Eliminates string boxing, args array allocation,
// and runtime string matching per call.

// Check if name matches a known string (helper macro for readability)
#define MATH_MATCH(s, slen) (ml == (slen) && strncmp(m, (s), (slen)) == 0)

// Compile-time Math method resolution for boxed path.
// Returns boxed Item result.
static MIR_reg_t jm_transpile_math_call(JsMirTranspiler* mt, JsCallNode* call, String* method) {
    int argc = jm_count_args(call->arguments);
    const char* m = method->chars;
    int ml = (int)method->len;

    // Transpile first argument and convert to number
    JsAstNode* arg0 = call->arguments;
    JsAstNode* arg1 = arg0 ? arg0->next : NULL;

    // --- 1-argument Math functions ---
    if (argc >= 1) {
        // Math.abs(x) → fn_abs(js_to_number(x))
        if (MATH_MATCH("abs", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_abs", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.floor(x) → fn_floor(js_to_number(x))
        if (MATH_MATCH("floor", 5)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_floor", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.ceil(x) → fn_ceil(js_to_number(x))
        if (MATH_MATCH("ceil", 4)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_ceil", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.round(x) → fn_round(js_to_number(x))
        if (MATH_MATCH("round", 5)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_round", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.sqrt(x) → fn_math_sqrt(js_to_number(x))
        if (MATH_MATCH("sqrt", 4)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_math_sqrt", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.log(x) → fn_math_log(js_to_number(x))
        if (MATH_MATCH("log", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_math_log", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.log10(x) → fn_math_log10(js_to_number(x))
        if (MATH_MATCH("log10", 5)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_math_log10", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.exp(x) → fn_math_exp(js_to_number(x))
        if (MATH_MATCH("exp", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_math_exp", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.sin(x) → fn_math_sin(js_to_number(x))
        if (MATH_MATCH("sin", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_math_sin", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.cos(x) → fn_math_cos(js_to_number(x))
        if (MATH_MATCH("cos", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_math_cos", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.tan(x) → fn_math_tan(js_to_number(x))
        if (MATH_MATCH("tan", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_math_tan", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.sign(x) → fn_sign(js_to_number(x))
        if (MATH_MATCH("sign", 4)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_sign", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
        // Math.trunc(x) → fn_int(js_to_number(x))
        if (MATH_MATCH("trunc", 5)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t num = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            return jm_call_1(mt, "fn_int", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, num));
        }
    }

    // --- 2-argument Math functions ---
    if (argc >= 2 && arg1) {
        // Math.pow(x, y) → fn_pow(js_to_number(x), js_to_number(y))
        if (MATH_MATCH("pow", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t b = jm_transpile_box_item(mt, arg1);
            MIR_reg_t na = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            MIR_reg_t nb = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, b));
            return jm_call_2(mt, "fn_pow", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, na),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, nb));
        }
        // Math.min(x, y) → fn_min2(js_to_number(x), js_to_number(y))
        if (MATH_MATCH("min", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t na = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            MIR_reg_t result = na;
            JsAstNode* arg = arg1;
            while (arg) {
                MIR_reg_t b = jm_transpile_box_item(mt, arg);
                MIR_reg_t nb = jm_call_1(mt, "js_to_number", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, b));
                result = jm_call_2(mt, "fn_min2", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, nb));
                arg = arg->next;
            }
            return result;
        }
        // Math.max(x, y) → fn_max2(js_to_number(x), js_to_number(y))
        if (MATH_MATCH("max", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t na = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            MIR_reg_t result = na;
            JsAstNode* arg = arg1;
            while (arg) {
                MIR_reg_t b = jm_transpile_box_item(mt, arg);
                MIR_reg_t nb = jm_call_1(mt, "js_to_number", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, b));
                result = jm_call_2(mt, "fn_max2", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, result),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, nb));
                arg = arg->next;
            }
            return result;
        }
        // Math.atan2(y, x) → fn_math_atan2(js_to_number(y), js_to_number(x))
        if (MATH_MATCH("atan2", 5)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            MIR_reg_t b = jm_transpile_box_item(mt, arg1);
            MIR_reg_t na = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
            MIR_reg_t nb = jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, b));
            return jm_call_2(mt, "fn_math_atan2", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, na),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, nb));
        }
    }

    // --- 0 or 1-arg special cases ---
    // Math.min() → Infinity, Math.max() → -Infinity (0 args)
    if (argc == 0) {
        if (MATH_MATCH("min", 3)) {
            MIR_reg_t r = jm_new_reg(mt, "inf", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_double_op(mt->ctx, INFINITY)));
            return jm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, r));
        }
        if (MATH_MATCH("max", 3)) {
            MIR_reg_t r = jm_new_reg(mt, "ninf", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV, MIR_new_reg_op(mt->ctx, r),
                MIR_new_double_op(mt->ctx, -INFINITY)));
            return jm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, r));
        }
    }
    // Math.min(x) / Math.max(x) with 1 arg → js_to_number(x)
    if (argc == 1) {
        if (MATH_MATCH("min", 3) || MATH_MATCH("max", 3)) {
            MIR_reg_t a = jm_transpile_box_item(mt, arg0);
            return jm_call_1(mt, "js_to_number", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, a));
        }
    }

    // --- Fallback: unknown Math method → dispatch via js_math_method ---
    log_debug("phase5: unresolved Math.%.*s, using runtime dispatch", ml, m);
    MIR_reg_t method_str = jm_box_string_literal(mt, method->chars, method->len);
    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, argc);
    return jm_call_3(mt, "js_math_method", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, method_str),
        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, argc));
}

// Phase 5: Resolve Math method return type at compile time
static TypeId jm_math_return_type(String* method, JsMirTranspiler* mt, JsAstNode* arg0) {
    if (!method) return LMD_TYPE_ANY;
    const char* m = method->chars;
    int ml = (int)method->len;

    // Always-float methods: sqrt, sin, cos, tan, log, log10, exp, pow, random, atan2
    if (MATH_MATCH("sqrt", 4) || MATH_MATCH("sin", 3) || MATH_MATCH("cos", 3) ||
        MATH_MATCH("tan", 3) || MATH_MATCH("log", 3) || MATH_MATCH("log10", 5) ||
        MATH_MATCH("exp", 3) || MATH_MATCH("pow", 3) || MATH_MATCH("random", 6) ||
        MATH_MATCH("atan2", 5))
        return LMD_TYPE_FLOAT;

    // Always-int methods: trunc, sign
    if (MATH_MATCH("trunc", 5) || MATH_MATCH("sign", 4))
        return LMD_TYPE_INT;

    // Type-preserving: abs, floor, ceil, round, min, max
    if (MATH_MATCH("abs", 3)) {
        if (arg0) {
            TypeId at = jm_get_effective_type(mt, arg0);
            if (at == LMD_TYPE_INT) return LMD_TYPE_INT;
            if (at == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
        }
        return LMD_TYPE_ANY;
    }
    // floor/ceil/round: always return int in Lambda (matches JS Math semantics for integers)
    if (MATH_MATCH("floor", 5) || MATH_MATCH("ceil", 4) || MATH_MATCH("round", 5))
        return LMD_TYPE_INT;

    // min/max: preserve type if both args same type
    if (MATH_MATCH("min", 3) || MATH_MATCH("max", 3)) {
        if (arg0) {
            TypeId at = jm_get_effective_type(mt, arg0);
            if (at == LMD_TYPE_INT || at == LMD_TYPE_FLOAT) return at;
        }
        return LMD_TYPE_ANY;
    }

    return LMD_TYPE_ANY;
}

// Phase 5: Native Math for known argument types.
// When called inside jm_transpile_as_native(), emits native C math function calls
// bypassing all Item boxing. Returns native-typed register (MIR_T_D or MIR_T_I64).
static MIR_reg_t jm_transpile_math_native(JsMirTranspiler* mt, JsCallNode* call,
                                            String* method, TypeId target_type) {
    int argc = jm_count_args(call->arguments);
    if (argc < 1) {
        // Math.random() → call push_d(rand()/RAND_MAX) then unbox. Not worth native path.
        MIR_reg_t boxed = jm_transpile_math_call(mt, call, method);
        return target_type == LMD_TYPE_FLOAT ? jm_emit_unbox_float(mt, boxed) : jm_emit_unbox_int(mt, boxed);
    }

    const char* m = method->chars;
    int ml = (int)method->len;
    JsAstNode* arg0 = call->arguments;
    TypeId arg_type = jm_get_effective_type(mt, arg0);
    bool arg_numeric = (arg_type == LMD_TYPE_INT || arg_type == LMD_TYPE_FLOAT);

    // For numeric arguments, use native C math functions directly
    if (arg_numeric) {
        // 1-arg double→double functions: sqrt, sin, cos, tan, log, log10, exp
        if (MATH_MATCH("sqrt", 4)) {
            MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_call_1(mt, "sqrt", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
            if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
            return r;
        }
        if (MATH_MATCH("sin", 3)) {
            MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_call_1(mt, "sin", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
            if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
            return r;
        }
        if (MATH_MATCH("cos", 3)) {
            MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_call_1(mt, "cos", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
            if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
            return r;
        }
        if (MATH_MATCH("tan", 3)) {
            MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_call_1(mt, "tan", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
            if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
            return r;
        }
        if (MATH_MATCH("log", 3)) {
            MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_call_1(mt, "log", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
            if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
            return r;
        }
        if (MATH_MATCH("log10", 5)) {
            MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_call_1(mt, "log10", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
            if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
            return r;
        }
        if (MATH_MATCH("exp", 3)) {
            MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
            MIR_reg_t r = jm_call_1(mt, "exp", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
            if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
            return r;
        }
        // Math.abs: int→int, float→float
        if (MATH_MATCH("abs", 3)) {
            if (arg_type == LMD_TYPE_INT) {
                MIR_reg_t i = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_INT);
                MIR_reg_t r = jm_call_1(mt, "fn_abs_i", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, i));
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, r);
                return r;
            } else {
                MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t r = jm_call_1(mt, "fabs", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
                if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
                return r;
            }
        }
        // Math.floor: int→int (identity), float→int via C floor()
        if (MATH_MATCH("floor", 5)) {
            if (arg_type == LMD_TYPE_INT) {
                MIR_reg_t i = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_INT);
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, i);
                return i;
            } else {
                MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t fd = jm_call_1(mt, "floor", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
                if (target_type == LMD_TYPE_FLOAT) return fd;
                return jm_emit_double_to_int(mt, fd);
            }
        }
        // Math.ceil: int→int (identity), float→int via C ceil()
        if (MATH_MATCH("ceil", 4)) {
            if (arg_type == LMD_TYPE_INT) {
                MIR_reg_t i = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_INT);
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, i);
                return i;
            } else {
                MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t cd = jm_call_1(mt, "ceil", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
                if (target_type == LMD_TYPE_FLOAT) return cd;
                return jm_emit_double_to_int(mt, cd);
            }
        }
        // Math.round: int→int (identity), float→int via C round()
        if (MATH_MATCH("round", 5)) {
            if (arg_type == LMD_TYPE_INT) {
                MIR_reg_t i = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_INT);
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, i);
                return i;
            } else {
                MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t rd = jm_call_1(mt, "round", MIR_T_D, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
                if (target_type == LMD_TYPE_FLOAT) return rd;
                return jm_emit_double_to_int(mt, rd);
            }
        }
        // Math.trunc: int→int (identity), float→int via D2I
        if (MATH_MATCH("trunc", 5)) {
            if (arg_type == LMD_TYPE_INT) {
                MIR_reg_t i = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_INT);
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, i);
                return i;
            } else {
                MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t i = jm_emit_double_to_int(mt, d);
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, i);
                return i;
            }
        }
        // Math.sign: returns i32, but we need target type
        if (MATH_MATCH("sign", 4)) {
            if (arg_type == LMD_TYPE_INT) {
                MIR_reg_t i = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_INT);
                MIR_reg_t r = jm_call_1(mt, "fn_sign_i", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, i));
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, r);
                return r;
            } else {
                MIR_reg_t d = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t r = jm_call_1(mt, "fn_sign_f", MIR_T_I64,
                    MIR_T_D, MIR_new_reg_op(mt->ctx, d));
                if (target_type == LMD_TYPE_FLOAT) return jm_emit_int_to_double(mt, r);
                return r;
            }
        }
        // Math.pow(x, y): both args → double, call fn_pow_u
        if (MATH_MATCH("pow", 3) && argc >= 2) {
            JsAstNode* arg1_n = arg0->next;
            TypeId a1t = jm_get_effective_type(mt, arg1_n);
            if (a1t == LMD_TYPE_INT || a1t == LMD_TYPE_FLOAT) {
                MIR_reg_t da = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t db = jm_transpile_as_native(mt, arg1_n, a1t, LMD_TYPE_FLOAT);
                MIR_reg_t r = jm_call_2(mt, "fn_pow_u", MIR_T_D,
                    MIR_T_D, MIR_new_reg_op(mt->ctx, da),
                    MIR_T_D, MIR_new_reg_op(mt->ctx, db));
                if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
                return r;
            }
        }
        // Math.min/max with 2 args: both → double, call fn_min2_u/fn_max2_u
        if (MATH_MATCH("min", 3) && argc >= 2) {
            JsAstNode* arg1_n = arg0->next;
            TypeId a1t = jm_get_effective_type(mt, arg1_n);
            if (a1t == LMD_TYPE_INT || a1t == LMD_TYPE_FLOAT) {
                MIR_reg_t da = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t db = jm_transpile_as_native(mt, arg1_n, a1t, LMD_TYPE_FLOAT);
                MIR_reg_t r = jm_call_2(mt, "fn_min2_u", MIR_T_D,
                    MIR_T_D, MIR_new_reg_op(mt->ctx, da),
                    MIR_T_D, MIR_new_reg_op(mt->ctx, db));
                if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
                return r;
            }
        }
        if (MATH_MATCH("max", 3) && argc >= 2) {
            JsAstNode* arg1_n = arg0->next;
            TypeId a1t = jm_get_effective_type(mt, arg1_n);
            if (a1t == LMD_TYPE_INT || a1t == LMD_TYPE_FLOAT) {
                MIR_reg_t da = jm_transpile_as_native(mt, arg0, arg_type, LMD_TYPE_FLOAT);
                MIR_reg_t db = jm_transpile_as_native(mt, arg1_n, a1t, LMD_TYPE_FLOAT);
                MIR_reg_t r = jm_call_2(mt, "fn_max2_u", MIR_T_D,
                    MIR_T_D, MIR_new_reg_op(mt->ctx, da),
                    MIR_T_D, MIR_new_reg_op(mt->ctx, db));
                if (target_type == LMD_TYPE_INT) return jm_emit_double_to_int(mt, r);
                return r;
            }
        }
    }

    // Non-numeric args or unhandled method: use boxed path, then unbox
    MIR_reg_t boxed = jm_transpile_math_call(mt, call, method);
    return target_type == LMD_TYPE_FLOAT ? jm_emit_unbox_float(mt, boxed) : jm_emit_unbox_int(mt, boxed);
}

#undef MATH_MATCH

// Helper: check if a CALL_EXPRESSION is a Math.xxx() call and extract the method name
static String* jm_get_math_method(JsCallNode* call) {
    if (!jm_is_math_call(call)) return NULL;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->property || m->property->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
    JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
    return prop->name;
}

static bool jm_is_document_call(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    return obj->name && obj->name->len == 8 && strncmp(obj->name->chars, "document", 8) == 0;
}

static bool jm_is_window_getComputedStyle(JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return false;
    JsMemberNode* m = (JsMemberNode*)call->callee;
    if (!m->object || m->object->node_type != JS_AST_NODE_IDENTIFIER) return false;
    if (!m->property || m->property->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
    JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
    return obj->name && obj->name->len == 6 && strncmp(obj->name->chars, "window", 6) == 0 &&
           prop->name && prop->name->len == 16 && strncmp(prop->name->chars, "getComputedStyle", 16) == 0;
}

// Call expression
static MIR_reg_t jm_transpile_call(JsMirTranspiler* mt, JsCallNode* call) {
    int arg_count = jm_count_args(call->arguments);

    // console.log(args...)
    if (jm_is_console_log(call)) {
        JsAstNode* arg = call->arguments;
        if (arg_count > 1) {
            // Multi-arg: space-separated output
            MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
            jm_call_void_2(mt, "js_console_log_multi",
                MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
        } else if (arg) {
            MIR_reg_t val = jm_transpile_box_item(mt, arg);
            jm_call_void_1(mt, "js_console_log",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        } else {
            MIR_reg_t null_val = jm_emit_null(mt);
            jm_call_void_1(mt, "js_console_log",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, null_val));
        }
        return jm_emit_null(mt);
    }

    // process.stdout.write(str)
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* outer = (JsMemberNode*)call->callee;
        if (!outer->computed && outer->property &&
            outer->property->node_type == JS_AST_NODE_IDENTIFIER &&
            outer->object && outer->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* inner = (JsMemberNode*)outer->object;
            JsIdentifierNode* prop = (JsIdentifierNode*)outer->property;
            if (!inner->computed && inner->object &&
                inner->object->node_type == JS_AST_NODE_IDENTIFIER &&
                inner->property && inner->property->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* obj = (JsIdentifierNode*)inner->object;
                JsIdentifierNode* mid = (JsIdentifierNode*)inner->property;
                // process.stdout.write
                if (obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "process", 7) == 0 &&
                    mid->name && mid->name->len == 6 && strncmp(mid->name->chars, "stdout", 6) == 0 &&
                    prop->name && prop->name->len == 5 && strncmp(prop->name->chars, "write", 5) == 0) {
                    MIR_reg_t arg_val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                    jm_call_void_1(mt, "js_process_stdout_write",
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, arg_val));
                    return jm_emit_null(mt);
                }
                // process.hrtime.bigint()
                if (obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "process", 7) == 0 &&
                    mid->name && mid->name->len == 6 && strncmp(mid->name->chars, "hrtime", 6) == 0 &&
                    prop->name && prop->name->len == 6 && strncmp(prop->name->chars, "bigint", 6) == 0) {
                    return jm_call_0(mt, "js_process_hrtime_bigint", MIR_T_I64);
                }
            }
        }
    }

    // document.<method>(args...)
    if (jm_is_document_call(call)) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
        MIR_reg_t method_str = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
        MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
        return jm_call_3(mt, "js_document_method", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, method_str),
            MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    }

    // Math.<method>(args...) → Phase 5: compile-time resolution
    if (jm_is_math_call(call)) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
        return jm_transpile_math_call(mt, call, prop->name);
    }

    // Object.keys(obj) -> js_object_keys(obj)
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (!m->computed && m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER &&
            m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
            JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
            if (obj->name && obj->name->len == 6 && strncmp(obj->name->chars, "Object", 6) == 0 &&
                prop->name && prop->name->len == 4 && strncmp(prop->name->chars, "keys", 4) == 0) {
                MIR_reg_t arg = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_object_keys", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, arg));
            }
        }
    }

    // window.getComputedStyle(elem, pseudo)
    if (jm_is_window_getComputedStyle(call)) {
        JsAstNode* arg = call->arguments;
        MIR_reg_t elem = arg ? jm_transpile_box_item(mt, arg) : jm_emit_null(mt);
        MIR_reg_t pseudo = (arg && arg->next) ? jm_transpile_box_item(mt, arg->next) : jm_emit_null(mt);
        return jm_call_2(mt, "js_get_computed_style", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, elem),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, pseudo));
    }

    // String.fromCharCode(code)
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (!m->computed && m->object && m->object->node_type == JS_AST_NODE_IDENTIFIER &&
            m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj = (JsIdentifierNode*)m->object;
            JsIdentifierNode* prop = (JsIdentifierNode*)m->property;
            if (obj->name && obj->name->len == 6 && strncmp(obj->name->chars, "String", 6) == 0 &&
                prop->name && prop->name->len == 12 && strncmp(prop->name->chars, "fromCharCode", 12) == 0) {
                MIR_reg_t code = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_box_int_const(mt, 0);
                return jm_call_1(mt, "js_string_fromCharCode", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, code));
            }
        }
    }

    // Generic method call: obj.method(args) -> dispatch by receiver type
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* m = (JsMemberNode*)call->callee;
        if (m->property && m->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)m->property;

            MIR_reg_t recv = jm_transpile_box_item(mt, m->object);
            MIR_reg_t method_name = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
            MIR_op_t args_op = args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0);

            // Phase 5: Type-aware method dispatch — when receiver type is known
            // at compile time, skip the runtime type cascade and call directly.
            TypeId recv_type = jm_get_effective_type(mt, m->object);
            if (recv_type == LMD_TYPE_STRING) {
                return jm_call_4(mt, "js_string_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
            }
            if (recv_type == LMD_TYPE_ARRAY) {
                return jm_call_4(mt, "js_array_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
            }
            if (recv_type == LMD_TYPE_INT || recv_type == LMD_TYPE_FLOAT) {
                return jm_call_4(mt, "js_number_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
            }

            // Runtime type dispatch cascade (when receiver type unknown)
            MIR_reg_t rtype = jm_call_1(mt, "item_type_id", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));

            MIR_reg_t result = jm_new_reg(mt, "mcall", MIR_T_I64);
            MIR_label_t l_string = jm_new_label(mt);
            MIR_label_t l_array = jm_new_label(mt);
            MIR_label_t l_dom = jm_new_label(mt);
            MIR_label_t l_fallback = jm_new_label(mt);
            MIR_label_t l_end = jm_new_label(mt);

            // if type == STRING
            MIR_reg_t is_str = jm_new_reg(mt, "isstr", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_str),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_STRING)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_string),
                MIR_new_reg_op(mt->ctx, is_str)));

            // if type == ARRAY
            MIR_reg_t is_arr = jm_new_reg(mt, "isarr", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_arr),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_ARRAY)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_array),
                MIR_new_reg_op(mt->ctx, is_arr)));

            // if type == INT or FLOAT -> number method
            MIR_label_t l_number = jm_new_label(mt);
            MIR_reg_t is_int = jm_new_reg(mt, "isint", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_int),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_INT)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_number),
                MIR_new_reg_op(mt->ctx, is_int)));
            MIR_reg_t is_float = jm_new_reg(mt, "isfloat", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_float),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_FLOAT)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_number),
                MIR_new_reg_op(mt->ctx, is_float)));

            // if type == MAP: check typed array -> array path, dom -> dom path, else fallback
            MIR_reg_t is_map = jm_new_reg(mt, "ismap", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ, MIR_new_reg_op(mt->ctx, is_map),
                MIR_new_reg_op(mt->ctx, rtype), MIR_new_int_op(mt->ctx, LMD_TYPE_MAP)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_fallback),
                MIR_new_reg_op(mt->ctx, is_map)));
            // Check if this is a typed array (Map with sentinel marker) -> use array method dispatch
            MIR_reg_t is_ta = jm_call_1(mt, "js_is_typed_array", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_array),
                MIR_new_reg_op(mt->ctx, is_ta)));
            MIR_reg_t is_dom = jm_call_1(mt, "js_is_dom_node", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, recv));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_dom),
                MIR_new_reg_op(mt->ctx, is_dom)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_fallback)));

            // STRING path
            jm_emit_label(mt, l_string);
            {
                MIR_reg_t r = jm_call_4(mt, "js_string_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // ARRAY path
            jm_emit_label(mt, l_array);
            {
                MIR_reg_t r = jm_call_4(mt, "js_array_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // DOM path
            jm_emit_label(mt, l_dom);
            {
                MIR_reg_t r = jm_call_4(mt, "js_dom_element_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // NUMBER path (INT or FLOAT): dispatch to js_number_method
            jm_emit_label(mt, l_number);
            {
                // For now, dispatch number methods: toFixed, toString, etc.
                MIR_reg_t r = jm_call_4(mt, "js_number_method", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

            // Fallback: property access + js_call_function
            jm_emit_label(mt, l_fallback);
            {
                MIR_reg_t fn = jm_call_2(mt, "js_property_access", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, method_name));
                MIR_reg_t r = jm_call_4(mt, "js_call_function", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, fn),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, recv),
                    MIR_T_I64, args_op,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, r)));
            }
            jm_emit_label(mt, l_end);
            return result;
        }
    }

    // Direct function call: identifier(args)
    if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;

        // Global builtin functions
        if (id->name) {
            const char* n = id->name->chars;
            int nl = (int)id->name->len;

            // parseInt(str, radix?)
            if (nl == 8 && strncmp(n, "parseInt", 8) == 0) {
                MIR_reg_t str = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                MIR_reg_t radix = (call->arguments && call->arguments->next)
                    ? jm_transpile_box_item(mt, call->arguments->next)
                    : jm_box_int_const(mt, 10);
                return jm_call_2(mt, "js_parseInt", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, str),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, radix));
            }
            // parseFloat(str)
            if (nl == 10 && strncmp(n, "parseFloat", 10) == 0) {
                MIR_reg_t str = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_parseFloat", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, str));
            }
            // isNaN(val)
            if (nl == 5 && strncmp(n, "isNaN", 5) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_isNaN", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // isFinite(val)
            if (nl == 8 && strncmp(n, "isFinite", 8) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_isFinite", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // Number(val)
            if (nl == 6 && strncmp(n, "Number", 6) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_unary_plus", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // String(val) — toString
            if (nl == 6 && strncmp(n, "String", 6) == 0) {
                MIR_reg_t val = call->arguments ? jm_transpile_box_item(mt, call->arguments) : jm_emit_null(mt);
                return jm_call_1(mt, "js_to_string_val", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            // String.fromCharCode(code)
            if (nl == 6 && strncmp(n, "String", 6) == 0) {
                // Already handled above; this is a member expression path
            }
        }

        NameEntry* entry = js_scope_lookup(mt->tp, id->name);
        // Fallback: use pre-resolved entry from AST building if scope lookup fails
        if (!entry && id->entry) entry = id->entry;

        // Resolve to a JsFunctionNode for direct call:
        // - directly a FUNCTION_DECLARATION, or
        // - a VARIABLE_DECLARATOR whose init is a function expression/arrow
        JsFunctionNode* resolved_fn = NULL;
        if (entry && entry->node) {
            JsAstNodeType ntype = ((JsAstNode*)entry->node)->node_type;
            if (ntype == JS_AST_NODE_FUNCTION_DECLARATION) {
                resolved_fn = (JsFunctionNode*)entry->node;
            } else if (ntype == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)entry->node;
                if (decl->init && (decl->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION
                    || decl->init->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
                    resolved_fn = (JsFunctionNode*)decl->init;
                }
            }
        }

        if (resolved_fn) {
            JsFuncCollected* fc = jm_find_collected_func(mt, resolved_fn);
            if (fc && (fc->func_item || fc->native_func_item) && fc->capture_count == 0) {
                // Phase 4: Check if we can call the native version
                if (fc->has_native_version && fc->native_func_item) {
                    bool all_args_match = true;
                    int pi = 0;
                    JsAstNode* acheck = call->arguments;
                    while (acheck && pi < fc->param_count) {
                        TypeId expected = fc->param_types[pi];
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
                    if (pi != fc->param_count) all_args_match = false;

                    if (all_args_match) {
                        // TCO: if this is a tail-recursive call, convert to goto
                        if (mt->tco_func && mt->in_tail_position &&
                            jm_is_recursive_call(call, mt->tco_func)) {
                            log_debug("js-mir TCO: tail call to %s — converting to goto", fc->name);

                            // Clear tail position for arg evaluation (inner calls are NOT tail)
                            bool saved_tail = mt->in_tail_position;
                            mt->in_tail_position = false;

                            // Phase 1: Evaluate all arguments into temp registers
                            MIR_reg_t temps[16];
                            JsAstNode* arg = call->arguments;
                            for (int i = 0; i < fc->param_count; i++) {
                                if (arg) {
                                    temps[i] = jm_transpile_as_native(mt, arg,
                                        jm_get_effective_type(mt, arg), fc->param_types[i]);
                                    arg = arg->next;
                                } else {
                                    MIR_type_t mt2 = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                                    temps[i] = jm_new_reg(mt, "tz", mt2);
                                    if (mt2 == MIR_T_D) {
                                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                            MIR_new_reg_op(mt->ctx, temps[i]),
                                            MIR_new_double_op(mt->ctx, 0.0)));
                                    } else {
                                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                            MIR_new_reg_op(mt->ctx, temps[i]),
                                            MIR_new_int_op(mt->ctx, 0)));
                                    }
                                }
                            }

                            // Phase 2: Assign temps → parameter registers
                            JsAstNode* pnode = mt->tco_func->node->params;
                            for (int i = 0; i < fc->param_count; i++) {
                                char pname[128];
                                if (pnode && pnode->node_type == JS_AST_NODE_IDENTIFIER) {
                                    JsIdentifierNode* pid = (JsIdentifierNode*)pnode;
                                    snprintf(pname, sizeof(pname), "_js_%.*s",
                                        (int)pid->name->len, pid->name->chars);
                                } else {
                                    snprintf(pname, sizeof(pname), "_js_p%d", i);
                                }
                                MIR_reg_t preg = MIR_reg(mt->ctx, pname, mt->current_func);
                                MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                                MIR_insn_code_t mov = (mtype == MIR_T_D) ? MIR_DMOV : MIR_MOV;
                                jm_emit(mt, MIR_new_insn(mt->ctx, mov,
                                    MIR_new_reg_op(mt->ctx, preg),
                                    MIR_new_reg_op(mt->ctx, temps[i])));
                                pnode = pnode ? pnode->next : NULL;
                            }

                            mt->in_tail_position = saved_tail;

                            // Jump back to function start
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                                MIR_new_label_op(mt->ctx, mt->tco_label)));
                            mt->tco_jumped = true;

                            // Return dummy register (unreachable code)
                            MIR_type_t native_ret = (fc->return_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                            MIR_reg_t dummy = jm_new_reg(mt, "tco_d", native_ret);
                            if (native_ret == MIR_T_D) {
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                    MIR_new_reg_op(mt->ctx, dummy),
                                    MIR_new_double_op(mt->ctx, 0.0)));
                            } else {
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, dummy),
                                    MIR_new_int_op(mt->ctx, 0)));
                            }
                            return dummy;
                        }

                        // Native direct call
                        char p_name[160];
                        snprintf(p_name, sizeof(p_name), "%s_n_cp%d", fc->name, mt->label_counter++);
                        MIR_var_t* p_args = (MIR_var_t*)alloca(fc->param_count * sizeof(MIR_var_t));
                        for (int i = 0; i < fc->param_count; i++) {
                            MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                            p_args[i] = {mtype, "a", 0};
                        }
                        MIR_type_t native_ret = (fc->return_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                        MIR_item_t proto = MIR_new_proto_arr(mt->ctx, p_name, 1, &native_ret,
                            fc->param_count, p_args);

                        int nops = 3 + fc->param_count;
                        MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
                        int oi = 0;
                        ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
                        ops[oi++] = MIR_new_ref_op(mt->ctx, fc->native_func_item);
                        MIR_reg_t result = jm_new_reg(mt, "ncall", native_ret);
                        ops[oi++] = MIR_new_reg_op(mt->ctx, result);

                        JsAstNode* arg = call->arguments;
                        for (int i = 0; i < fc->param_count; i++) {
                            if (arg) {
                                MIR_reg_t val = jm_transpile_as_native(mt, arg,
                                    jm_get_effective_type(mt, arg), fc->param_types[i]);
                                ops[oi++] = MIR_new_reg_op(mt->ctx, val);
                                arg = arg->next;
                            } else {
                                MIR_reg_t zero = jm_new_reg(mt, "nz", MIR_T_I64);
                                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                    MIR_new_reg_op(mt->ctx, zero), MIR_new_int_op(mt->ctx, 0)));
                                ops[oi++] = MIR_new_reg_op(mt->ctx, zero);
                            }
                        }

                        jm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
                        return result; // returns NATIVE value
                    }
                }

                // Direct call to local function (only for non-closures;
                // closures need env from the JsFunction wrapper, so they
                // go through js_call_function which handles env passing)
                if (fc->func_item) {
                int param_count = jm_count_params(resolved_fn);

                // Build proto for this call site
                char p_name[160];
                snprintf(p_name, sizeof(p_name), "%s_cp%d", fc->name, mt->label_counter++);
                MIR_var_t* p_args = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
                for (int i = 0; i < param_count; i++) {
                    p_args[i] = {MIR_T_I64, "a", 0};
                }
                MIR_type_t res_types[1] = {MIR_T_I64};
                MIR_item_t proto = MIR_new_proto_arr(mt->ctx, p_name, 1, res_types, param_count, p_args);

                // Build call operands: proto, func_ref, result, args...
                int nops = 3 + param_count;
                MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
                int oi = 0;
                ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
                ops[oi++] = MIR_new_ref_op(mt->ctx, fc->func_item);
                MIR_reg_t result = jm_new_reg(mt, "dcall", MIR_T_I64);
                ops[oi++] = MIR_new_reg_op(mt->ctx, result);

                JsAstNode* arg = call->arguments;
                for (int i = 0; i < param_count; i++) {
                    if (arg) {
                        MIR_reg_t val = jm_transpile_box_item(mt, arg);
                        ops[oi++] = MIR_new_reg_op(mt->ctx, val);
                        arg = arg->next;
                    } else {
                        MIR_reg_t null_val = jm_emit_null(mt);
                        ops[oi++] = MIR_new_reg_op(mt->ctx, null_val);
                    }
                }

                jm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));
                return result;
                } // end if (fc->func_item)
            }
        }
    }

    // Fallback: evaluate callee, build args array, call js_call_function
    MIR_reg_t callee = jm_transpile_box_item(mt, call->callee);
    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    MIR_reg_t null_this = jm_emit_null(mt);
    return jm_call_4(mt, "js_call_function", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, null_this),
        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
}

// Member expression
static MIR_reg_t jm_transpile_member(JsMirTranspiler* mt, JsMemberNode* mem) {
    // document.property
    if (!mem->computed && mem->object &&
        mem->object->node_type == JS_AST_NODE_IDENTIFIER &&
        mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* obj = (JsIdentifierNode*)mem->object;
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;

        // document.<prop>
        if (obj->name && obj->name->len == 8 && strncmp(obj->name->chars, "document", 8) == 0) {
            MIR_reg_t key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            return jm_call_1(mt, "js_document_get_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        }

        // Math.<prop>
        if (obj->name && obj->name->len == 4 && strncmp(obj->name->chars, "Math", 4) == 0) {
            MIR_reg_t key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            return jm_call_1(mt, "js_math_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        }

        // process.argv
        if (obj->name && obj->name->len == 7 && strncmp(obj->name->chars, "process", 7) == 0 &&
            prop->name && prop->name->len == 4 && strncmp(prop->name->chars, "argv", 4) == 0) {
            return jm_call_0(mt, "js_get_process_argv", MIR_T_I64);
        }

        // Number.MAX_SAFE_INTEGER, Number.MIN_SAFE_INTEGER, etc. → js_number_property
        if (obj->name && obj->name->len == 6 && strncmp(obj->name->chars, "Number", 6) == 0) {
            MIR_reg_t key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
            return jm_call_1(mt, "js_number_property", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
        }
    }

    // obj.style.X -> js_dom_get_style_property(obj, "X")
    if (!mem->computed && mem->object &&
        mem->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION &&
        mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsMemberNode* outer = (JsMemberNode*)mem->object;
        if (!outer->computed && outer->property &&
            outer->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* mid = (JsIdentifierNode*)outer->property;
            if (mid->name && mid->name->len == 5 && strncmp(mid->name->chars, "style", 5) == 0) {
                JsIdentifierNode* sp = (JsIdentifierNode*)mem->property;
                MIR_reg_t obj = jm_transpile_box_item(mt, outer->object);
                MIR_reg_t key = jm_box_string_literal(mt, sp->name->chars, sp->name->len);
                return jm_call_2(mt, "js_dom_get_style_property", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
            }
        }
    }

    // .length -> i2it(js_get_length(obj))
    // Uses js_get_length which handles typed arrays (Maps) as well as all Lambda container types
    if (!mem->computed && mem->property &&
        mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
        if (prop->name && prop->name->len == 6 && strncmp(prop->name->chars, "length", 6) == 0) {
            MIR_reg_t obj = jm_transpile_box_item(mt, mem->object);
            MIR_reg_t len = jm_call_1(mt, "js_get_length", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj));
            return jm_box_int_reg(mt, len);
        }
    }

    // General property access: js_property_access(obj, key)
    MIR_reg_t obj = jm_transpile_box_item(mt, mem->object);
    MIR_reg_t key;
    if (mem->computed) {
        key = jm_transpile_box_item(mt, mem->property);
    } else if (mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
        key = jm_box_string_literal(mt, prop->name->chars, prop->name->len);
    } else {
        key = jm_transpile_box_item(mt, mem->property);
    }

    return jm_call_2(mt, "js_property_access", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
}

// Array expression
static MIR_reg_t jm_transpile_array(JsMirTranspiler* mt, JsArrayNode* arr) {
    // Check if any element is a spread element
    bool has_spread = false;
    JsAstNode* check = arr->elements;
    while (check) {
        if (check->node_type == JS_AST_NODE_SPREAD_ELEMENT) { has_spread = true; break; }
        check = check->next;
    }

    MIR_reg_t array;
    if (has_spread) {
        // Use empty array + push for arrays with spread
        array = jm_call_1(mt, "js_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

        JsAstNode* elem = arr->elements;
        while (elem) {
            if (elem->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                // Spread element: iterate source array and push each element
                JsSpreadElementNode* spread = (JsSpreadElementNode*)elem;
                MIR_reg_t src = jm_transpile_box_item(mt, spread->argument);

                // Get length of source array
                MIR_reg_t src_len = jm_call_1(mt, "js_array_length", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, src));

                // Loop: for (i = 0; i < src_len; i++)
                MIR_reg_t i_reg = jm_new_reg(mt, "si", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, i_reg), MIR_new_int_op(mt->ctx, 0)));

                MIR_label_t l_spread_check = jm_new_label(mt);
                MIR_label_t l_spread_end = jm_new_label(mt);

                jm_emit_label(mt, l_spread_check);
                MIR_reg_t cmp = jm_new_reg(mt, "scmp", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, cmp),
                    MIR_new_reg_op(mt->ctx, i_reg), MIR_new_reg_op(mt->ctx, src_len)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_spread_end),
                    MIR_new_reg_op(mt->ctx, cmp)));

                // Get element at index i (box the index first)
                MIR_reg_t idx_boxed = jm_new_reg(mt, "sidx", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, idx_boxed),
                    MIR_new_reg_op(mt->ctx, i_reg), MIR_new_uint_op(mt->ctx, ITEM_INT_TAG)));
                MIR_reg_t src_elem = jm_call_2(mt, "js_array_get", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, src),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_boxed));
                jm_call_2(mt, "js_array_push", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, src_elem));

                // i++
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, i_reg),
                    MIR_new_reg_op(mt->ctx, i_reg), MIR_new_int_op(mt->ctx, 1)));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_spread_check)));
                jm_emit_label(mt, l_spread_end);
            } else {
                MIR_reg_t val = jm_transpile_box_item(mt, elem);
                jm_call_2(mt, "js_array_push", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            }
            elem = elem->next;
        }
    } else {
        // No spread: use pre-allocated array with set (original approach)
        array = jm_call_1(mt, "js_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, arr->length));

        JsAstNode* elem = arr->elements;
        int index = 0;
        while (elem) {
            MIR_reg_t idx = jm_box_int_const(mt, index);
            MIR_reg_t val = jm_transpile_box_item(mt, elem);
            jm_call_3(mt, "js_array_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            elem = elem->next;
            index++;
        }
    }

    return array;
}

// Object expression
static MIR_reg_t jm_transpile_object(JsMirTranspiler* mt, JsObjectNode* obj) {
    MIR_reg_t object = jm_call_0(mt, "js_new_object", MIR_T_I64);

    JsAstNode* prop = obj->properties;
    while (prop) {
        if (prop->node_type == JS_AST_NODE_PROPERTY) {
            JsPropertyNode* p = (JsPropertyNode*)prop;
            MIR_reg_t key;
            if (p->computed) {
                key = jm_transpile_box_item(mt, p->key);
            } else if (p->key->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)p->key;
                key = jm_box_string_literal(mt, id->name->chars, id->name->len);
            } else {
                key = jm_transpile_box_item(mt, p->key);
            }
            MIR_reg_t val = jm_transpile_box_item(mt, p->value);
            jm_call_3(mt, "js_property_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, object),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
        }
        prop = prop->next;
    }

    return object;
}

// Conditional expression (ternary)
static MIR_reg_t jm_transpile_conditional(JsMirTranspiler* mt, JsConditionalNode* cond) {
    MIR_reg_t test = jm_transpile_box_item(mt, cond->test);
    MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, test));

    MIR_reg_t result = jm_new_reg(mt, "tern", MIR_T_I64);
    MIR_label_t l_false = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_false),
        MIR_new_reg_op(mt->ctx, truthy)));

    MIR_reg_t cons = jm_transpile_box_item(mt, cond->consequent);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, cons)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    jm_emit_label(mt, l_false);
    MIR_reg_t alt = jm_transpile_box_item(mt, cond->alternate);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, result), MIR_new_reg_op(mt->ctx, alt)));

    jm_emit_label(mt, l_end);
    return result;
}

// Template literal
static MIR_reg_t jm_transpile_template_literal(JsMirTranspiler* mt, JsTemplateLiteralNode* tmpl) {
    // Get pool pointer from _lambda_rt for StringBuf allocation
    // Load _lambda_rt import
    JsMirImportEntry* rt_ie = jm_ensure_import(mt, "_lambda_rt", MIR_T_I64, 0, NULL, 0);
    MIR_reg_t rt_addr = jm_new_reg(mt, "rt_addr", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, rt_addr),
        MIR_new_ref_op(mt->ctx, rt_ie->import)));
    // Load _lambda_rt pointer: Context* rt = *(Context**)rt_addr
    MIR_reg_t rt_ptr = jm_new_reg(mt, "rt_ptr", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, rt_ptr),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, rt_addr, 0, 1)));
    // Load rt->pool (offset = offsetof(Context, pool))
    MIR_reg_t pool_reg = jm_new_reg(mt, "pool", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, pool_reg),
        MIR_new_mem_op(mt->ctx, MIR_T_I64, offsetof(Context, pool), rt_ptr, 0, 1)));

    // Create StringBuf: stringbuf_new(pool)
    MIR_reg_t sb = jm_call_1(mt, "stringbuf_new", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, pool_reg));

    JsAstNode* quasi = tmpl->quasis;
    JsAstNode* expr = tmpl->expressions;

    while (quasi) {
        if (quasi->node_type == JS_AST_NODE_TEMPLATE_ELEMENT) {
            JsTemplateElementNode* elem = (JsTemplateElementNode*)quasi;
            if (elem->cooked && elem->cooked->len > 0) {
                // Intern the template text
                String* interned = name_pool_create_len(mt->tp->name_pool,
                    elem->cooked->chars, elem->cooked->len);
                MIR_reg_t str_ptr = jm_new_reg(mt, "tstr", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, str_ptr),
                    MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)interned->chars)));
                // stringbuf_append_str(sb, str)
                MIR_var_t app_args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
                JsMirImportEntry* app_ie = jm_ensure_import(mt, "stringbuf_append_str", MIR_T_I64, 2, app_args, 0);
                jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
                    MIR_new_ref_op(mt->ctx, app_ie->proto),
                    MIR_new_ref_op(mt->ctx, app_ie->import),
                    MIR_new_reg_op(mt->ctx, sb),
                    MIR_new_reg_op(mt->ctx, str_ptr)));
            }
        }

        // Interpolated expression
        if (expr && quasi->node_type == JS_AST_NODE_TEMPLATE_ELEMENT &&
            !((JsTemplateElementNode*)quasi)->tail) {
            MIR_reg_t eval = jm_transpile_box_item(mt, expr);
            // Convert to string: js_to_string(value)
            MIR_reg_t str_item = jm_call_1(mt, "js_to_string", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, eval));
            // Unbox string: it2s(str_item) -> String*
            MIR_reg_t str_ptr = jm_call_1(mt, "it2s", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, str_item));
            // Compute chars address: str_ptr + offsetof(String, chars)
            // (chars is a flexible array member, not a pointer)
            MIR_reg_t chars = jm_new_reg(mt, "chars", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                MIR_new_reg_op(mt->ctx, chars),
                MIR_new_reg_op(mt->ctx, str_ptr),
                MIR_new_int_op(mt->ctx, offsetof(String, chars))));
            // Load String.len (uint32_t at offset 0)
            MIR_reg_t len = jm_new_reg(mt, "slen", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, len),
                MIR_new_mem_op(mt->ctx, MIR_T_U32, offsetof(String, len), str_ptr, 0, 1)));
            // stringbuf_append_str_n(sb, chars, len)
            jm_call_void_3(mt, "stringbuf_append_str_n",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, sb),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, chars),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, len));
            expr = expr->next;
        }

        quasi = quasi->next;
    }

    // stringbuf_to_string(sb) -> String*
    MIR_reg_t result_str = jm_call_1(mt, "stringbuf_to_string", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, sb));
    // Box as string
    return jm_box_string(mt, result_str);
}

// Function expression / arrow function
static MIR_reg_t jm_transpile_func_expr(JsMirTranspiler* mt, JsFunctionNode* fn) {
    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
    if (!fc || !fc->func_item) {
        log_error("js-mir: function expression not found in collected functions");
        return jm_emit_null(mt);
    }

    int param_count = jm_count_params(fn);

    if (fc->capture_count > 0) {
        // Closure: allocate env, populate with captured variable values
        // env = js_alloc_env(capture_count)
        MIR_reg_t env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, fc->capture_count));

        // Store each captured variable's current value into env slots
        for (int i = 0; i < fc->capture_count; i++) {
            JsMirVarEntry* var = jm_find_var(mt, fc->captures[i].name);
            if (var) {
                // env[i] = current value of captured variable
                // Box native-typed variables before storing in env (closures read boxed Items)
                MIR_reg_t value_to_store = var->reg;
                if (jm_is_native_type(var->type_id)) {
                    value_to_store = jm_box_native(mt, var->reg, var->type_id);
                }
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(uint64_t), env, 0, 1),
                    MIR_new_reg_op(mt->ctx, value_to_store)));
            } else {
                log_error("js-mir: captured variable '%s' not found in scope", fc->captures[i].name);
            }
        }

        // js_new_closure(func_ptr, param_count, env, capture_count)
        return jm_call_4(mt, "js_new_closure", MIR_T_I64,
            MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
            MIR_T_I64, MIR_new_int_op(mt->ctx, param_count),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
            MIR_T_I64, MIR_new_int_op(mt->ctx, fc->capture_count));
    }

    // Regular function (no captures)
    // js_new_function((void*)func_item, param_count)
    return jm_call_2(mt, "js_new_function", MIR_T_I64,
        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
        MIR_T_I64, MIR_new_int_op(mt->ctx, param_count));
}

// ============================================================================
// Box item dispatcher: returns register containing boxed Item
// ============================================================================

static MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item) {
    if (!item) return jm_emit_null(mt);

    // Identifiers: handle native-typed variables (need boxing)
    if (item->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)item;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var && jm_is_native_type(var->type_id)) {
            return jm_box_native(mt, var->reg, var->type_id);
        }
        return jm_transpile_identifier(mt, id);
    }

    // Expressions that may return native registers — check type and box if needed
    switch (item->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION: {
        // Only treat as native if the binary op actually takes the native path
        // (both operands must be typed numeric)
        JsBinaryNode* bin = (JsBinaryNode*)item;
        TypeId lt = jm_get_effective_type(mt, bin->left);
        TypeId rt = jm_get_effective_type(mt, bin->right);
        bool left_num  = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
        bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
        bool both_numeric = left_num && right_num;
        // Native path is only taken if both_numeric AND the op is handled natively
        // (EXP, AND, OR, float MOD fall through to boxed runtime)
        bool native_binary = both_numeric &&
            bin->op != JS_OP_EXP && bin->op != JS_OP_AND && bin->op != JS_OP_OR;
        if (native_binary && bin->op == JS_OP_MOD) {
            // Float modulo falls through to boxed
            native_binary = (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT);
        }
        // Semi-native: comparisons with at least one typed numeric operand
        if (!native_binary && (left_num || right_num)) {
            switch (bin->op) {
            case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
            case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
                native_binary = true;
                break;
            default: break;
            }
        }
        TypeId etype = jm_get_effective_type(mt, item);
        MIR_reg_t result = jm_transpile_expression(mt, item);
        if (native_binary && jm_is_native_type(etype)) {
            return jm_box_native(mt, result, etype);
        }
        return result;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)item;
        // Check if the native path is actually taken for this unary op
        bool native_unary = false;
        if (un->operand) {
            TypeId op_type = jm_get_effective_type(mt, un->operand);
            bool op_numeric = (op_type == LMD_TYPE_INT || op_type == LMD_TYPE_FLOAT);
            switch (un->op) {
            case JS_OP_PLUS: case JS_OP_ADD:
            case JS_OP_MINUS: case JS_OP_SUB:
                native_unary = op_numeric;
                break;
            case JS_OP_INCREMENT: case JS_OP_DECREMENT:
                if (un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* uid = (JsIdentifierNode*)un->operand;
                    char uvname[128];
                    snprintf(uvname, sizeof(uvname), "_js_%.*s", (int)uid->name->len, uid->name->chars);
                    JsMirVarEntry* uvar = jm_find_var(mt, uvname);
                    native_unary = uvar && (uvar->type_id == LMD_TYPE_INT || uvar->type_id == LMD_TYPE_FLOAT);
                }
                break;
            default: break;
            }
        }
        TypeId etype = jm_get_effective_type(mt, item);
        MIR_reg_t result = jm_transpile_expression(mt, item);
        if (native_unary && jm_is_native_type(etype)) {
            return jm_box_native(mt, result, etype);
        }
        return result;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)item;
        bool native_assign = false;
        if (asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* aid = (JsIdentifierNode*)asgn->left;
            char avname[128];
            snprintf(avname, sizeof(avname), "_js_%.*s", (int)aid->name->len, aid->name->chars);
            JsMirVarEntry* avar = jm_find_var(mt, avname);
            native_assign = avar && !avar->from_env &&
                            (avar->type_id == LMD_TYPE_INT || avar->type_id == LMD_TYPE_FLOAT);
        }
        TypeId etype = jm_get_effective_type(mt, item);
        MIR_reg_t result = jm_transpile_expression(mt, item);
        if (native_assign && jm_is_native_type(etype)) {
            return jm_box_native(mt, result, etype);
        }
        return result;
    }
    // These always return boxed Items — transpile directly
    case JS_AST_NODE_CONDITIONAL_EXPRESSION:
    case JS_AST_NODE_MEMBER_EXPRESSION:
    case JS_AST_NODE_ARRAY_EXPRESSION:
    case JS_AST_NODE_OBJECT_EXPRESSION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return jm_transpile_expression(mt, item);
    case JS_AST_NODE_CALL_EXPRESSION: {
        // Phase 4: Only native user-defined functions return native registers.
        // Math calls and other built-in calls return boxed Items.
        JsCallNode* call_item = (JsCallNode*)item;
        JsFuncCollected* fc = jm_resolve_native_call(mt, call_item);
        if (fc) {
            MIR_reg_t result = jm_transpile_expression(mt, item);
            return jm_box_native(mt, result, fc->return_type);
        }
        // Non-native call: always returns boxed Item
        return jm_transpile_expression(mt, item);
    }
    default:
        break;
    }

    // Type-based boxing for literals
    if (item->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)item;
        switch (lit->literal_type) {
        case JS_LITERAL_NUMBER: {
            double val = lit->value.number_value;
            if (val == (double)(int64_t)val && val >= -36028797018963968.0 && val <= 36028797018963967.0) {
                return jm_box_int_const(mt, (int64_t)val);
            }
            MIR_reg_t d = jm_new_reg(mt, "dbl", MIR_T_D);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                MIR_new_reg_op(mt->ctx, d),
                MIR_new_double_op(mt->ctx, val)));
            return jm_box_float(mt, d);
        }
        case JS_LITERAL_STRING: {
            return jm_box_string_literal(mt, lit->value.string_value->chars,
                lit->value.string_value->len);
        }
        case JS_LITERAL_BOOLEAN: {
            MIR_reg_t r = jm_new_reg(mt, "bool", MIR_T_I64);
            uint64_t bval = lit->value.boolean_value ? ITEM_TRUE_VAL : ITEM_FALSE_VAL;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, r), MIR_new_int_op(mt->ctx, (int64_t)bval)));
            return r;
        }
        case JS_LITERAL_NULL:
        case JS_LITERAL_UNDEFINED:
            return jm_emit_null(mt);
        }
    }

    // If type info available, box based on type
    if (item->type) {
        switch (item->type->type_id) {
        case LMD_TYPE_NULL:
            return jm_emit_null(mt);
        case LMD_TYPE_INT: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            return jm_box_int_reg(mt, raw);
        }
        case LMD_TYPE_FLOAT: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            return jm_box_float(mt, raw);
        }
        case LMD_TYPE_BOOL: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            MIR_reg_t result = jm_new_reg(mt, "boxb", MIR_T_I64);
            uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR,
                MIR_new_reg_op(mt->ctx, result),
                MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG),
                MIR_new_reg_op(mt->ctx, raw)));
            return result;
        }
        case LMD_TYPE_STRING: {
            MIR_reg_t raw = jm_transpile_expression(mt, item);
            return jm_box_string(mt, raw);
        }
        default:
            // Already boxed or unknown - just transpile as-is
            return jm_transpile_expression(mt, item);
        }
    }

    // Fallback
    return jm_transpile_expression(mt, item);
}

// ============================================================================
// Expression dispatcher
// ============================================================================

static MIR_reg_t jm_transpile_expression(JsMirTranspiler* mt, JsAstNode* expr) {
    if (!expr) return jm_emit_null(mt);

    switch (expr->node_type) {
    case JS_AST_NODE_LITERAL:
        return jm_transpile_literal(mt, (JsLiteralNode*)expr);
    case JS_AST_NODE_IDENTIFIER:
        return jm_transpile_identifier(mt, (JsIdentifierNode*)expr);
    case JS_AST_NODE_BINARY_EXPRESSION:
        return jm_transpile_binary(mt, (JsBinaryNode*)expr);
    case JS_AST_NODE_UNARY_EXPRESSION:
        return jm_transpile_unary(mt, (JsUnaryNode*)expr);
    case JS_AST_NODE_CALL_EXPRESSION:
        return jm_transpile_call(mt, (JsCallNode*)expr);
    case JS_AST_NODE_MEMBER_EXPRESSION:
        return jm_transpile_member(mt, (JsMemberNode*)expr);
    case JS_AST_NODE_ARRAY_EXPRESSION:
        return jm_transpile_array(mt, (JsArrayNode*)expr);
    case JS_AST_NODE_OBJECT_EXPRESSION:
        return jm_transpile_object(mt, (JsObjectNode*)expr);
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
        return jm_transpile_func_expr(mt, (JsFunctionNode*)expr);
    case JS_AST_NODE_CONDITIONAL_EXPRESSION:
        return jm_transpile_conditional(mt, (JsConditionalNode*)expr);
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return jm_transpile_template_literal(mt, (JsTemplateLiteralNode*)expr);
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION:
        return jm_transpile_assignment(mt, (JsAssignmentNode*)expr);
    case JS_AST_NODE_NEW_EXPRESSION:
        return jm_transpile_new_expr(mt, (JsCallNode*)expr);
    default:
        log_error("js-mir: unsupported expression type %d", expr->node_type);
        return jm_emit_null(mt);
    }
}

// ============================================================================
// Statement transpilers
// ============================================================================

static void jm_transpile_var_decl(JsMirTranspiler* mt, JsVariableDeclarationNode* var) {
    JsAstNode* decl = var->declarations;
    while (decl) {
        if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                char vname[128];
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);

                if (d->init) {
                    log_debug("var-decl: '%s' init node_type=%d", vname, d->init->node_type);
                    TypeId init_type = jm_get_effective_type(mt, d->init);

                    if (init_type == LMD_TYPE_INT) {
                        // native int variable
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                        MIR_reg_t native_val = jm_transpile_as_native(mt, d->init, init_type, LMD_TYPE_INT);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, native_val)));
                        jm_set_var(mt, vname, reg, MIR_T_I64, LMD_TYPE_INT);
                    } else if (init_type == LMD_TYPE_FLOAT) {
                        // native double variable
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_D);
                        MIR_reg_t native_val = jm_transpile_as_native(mt, d->init, init_type, LMD_TYPE_FLOAT);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, native_val)));
                        jm_set_var(mt, vname, reg, MIR_T_D, LMD_TYPE_FLOAT);
                    } else {
                        // boxed (string, object, array, any, etc.)
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                        MIR_reg_t val = jm_transpile_box_item(mt, d->init);
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, val)));
                        jm_set_var(mt, vname, reg, MIR_T_I64, init_type);
                    }
                } else {
                    MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, reg),
                        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
                    jm_set_var(mt, vname, reg);
                }
            } else if (d->id && d->id->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                // array destructuring: const [a, b, ...rest] = expr
                JsArrayPatternNode* pattern = (JsArrayPatternNode*)d->id;
                MIR_reg_t src = d->init ? jm_transpile_box_item(mt, d->init) : jm_emit_null(mt);
                int idx = 0;
                JsAstNode* elem = pattern->elements;
                while (elem) {
                    if (elem->node_type == JS_AST_NODE_IDENTIFIER) {
                        // simple: a = src[idx]
                        JsIdentifierNode* id = (JsIdentifierNode*)elem;
                        char vname[128];
                        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
                        MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                        MIR_reg_t key = jm_box_int_const(mt, idx);
                        MIR_reg_t val = jm_call_2(mt, "js_property_access", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, src),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, reg),
                            MIR_new_reg_op(mt->ctx, val)));
                        jm_set_var(mt, vname, reg);
                    } else if (elem->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                        // rest: ...rest = src.slice(idx)
                        JsSpreadElementNode* spread = (JsSpreadElementNode*)elem;
                        if (spread->argument && spread->argument->node_type == JS_AST_NODE_IDENTIFIER) {
                            JsIdentifierNode* id = (JsIdentifierNode*)spread->argument;
                            char vname[128];
                            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
                            MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                            MIR_reg_t start = jm_box_int_const(mt, idx);
                            MIR_reg_t val = jm_call_2(mt, "js_array_slice_from", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, src),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, start));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, reg),
                                MIR_new_reg_op(mt->ctx, val)));
                            jm_set_var(mt, vname, reg);
                        }
                    } else if (elem->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
                        // default: a = defaultVal -> a = src[idx], if null/undef use default
                        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)elem;
                        if (ap->left && ap->left->node_type == JS_AST_NODE_IDENTIFIER) {
                            JsIdentifierNode* id = (JsIdentifierNode*)ap->left;
                            char vname[128];
                            snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
                            MIR_reg_t reg = jm_new_reg(mt, vname, MIR_T_I64);
                            MIR_reg_t key = jm_box_int_const(mt, idx);
                            MIR_reg_t val = jm_call_2(mt, "js_property_access", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, src),
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, key));
                            // check if null/undefined, use default
                            MIR_label_t l_has_val = jm_new_label(mt);
                            MIR_reg_t cmp = jm_new_reg(mt, "destr_cmp", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, reg),
                                MIR_new_reg_op(mt->ctx, val)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_NES,
                                MIR_new_reg_op(mt->ctx, cmp),
                                MIR_new_reg_op(mt->ctx, val),
                                MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                                MIR_new_label_op(mt->ctx, l_has_val),
                                MIR_new_reg_op(mt->ctx, cmp)));
                            // null/undefined -> use default
                            MIR_reg_t def_val = jm_transpile_box_item(mt, ap->right);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, reg),
                                MIR_new_reg_op(mt->ctx, def_val)));
                            jm_emit_label(mt, l_has_val);
                            jm_set_var(mt, vname, reg);
                        }
                    }
                    elem = elem->next;
                    idx++;
                }
            }
        }
        decl = decl->next;
    }
}

static void jm_transpile_if(JsMirTranspiler* mt, JsIfNode* if_node) {
    // Determine if we can use native comparison for the test
    bool native_test = false;
    if (if_node->test && if_node->test->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* test_bin = (JsBinaryNode*)if_node->test;
        TypeId lt = jm_get_effective_type(mt, test_bin->left);
        TypeId rt = jm_get_effective_type(mt, test_bin->right);
        bool left_num  = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
        bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
        if (left_num || right_num) {
            switch (test_bin->op) {
            case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
            case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
                native_test = true; break;
            default: break;
            }
        }
        if (left_num && right_num) native_test = true;
    }

    MIR_reg_t test_val;
    if (native_test) {
        test_val = jm_transpile_expression(mt, if_node->test);
    } else {
        MIR_reg_t test = jm_transpile_box_item(mt, if_node->test);
        test_val = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
    }

    MIR_label_t l_else = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_else),
        MIR_new_reg_op(mt->ctx, test_val)));

    // Consequent
    if (if_node->consequent) {
        if (if_node->consequent->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)if_node->consequent;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, if_node->consequent);
        }
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // Alternate
    jm_emit_label(mt, l_else);
    if (if_node->alternate) {
        if (if_node->alternate->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)if_node->alternate;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, if_node->alternate);
        }
    }
    jm_emit_label(mt, l_end);
}

static void jm_transpile_while(JsMirTranspiler* mt, JsWhileNode* wh) {
    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Push loop labels
    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_test;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    jm_emit_label(mt, l_test);

    // Determine if we can use native comparison for the test
    bool native_test = false;
    if (wh->test && wh->test->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* test_bin = (JsBinaryNode*)wh->test;
        TypeId lt = jm_get_effective_type(mt, test_bin->left);
        TypeId rt = jm_get_effective_type(mt, test_bin->right);
        bool left_num  = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
        bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
        // Native test when at least one side is typed numeric (comparisons)
        if (left_num || right_num) {
            switch (test_bin->op) {
            case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
            case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
                native_test = true; break;
            default: break;
            }
        }
        // Also native for all operations when both sides typed
        if (left_num && right_num) native_test = true;
    }

    if (native_test) {
        MIR_reg_t test = jm_transpile_expression(mt, wh->test);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
            MIR_new_reg_op(mt->ctx, test)));
    } else {
        MIR_reg_t test = jm_transpile_box_item(mt, wh->test);
        MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // Body
    if (wh->body) {
        if (wh->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)wh->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, wh->body);
        }
    }

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
}

static void jm_transpile_for(JsMirTranspiler* mt, JsForNode* for_node) {
    jm_push_scope(mt);

    // Init
    if (for_node->init) {
        jm_transpile_statement(mt, for_node->init);
    }

    // --- For-loop specialization: detect and cache loop bound ---
    // Three tiers of test optimization:
    //   1. full_native:  both sides typed numeric → native compare + branch (existing)
    //   2. semi_native:  one side typed, other untyped but identifier/literal →
    //                    cache bound before loop, native compare each iteration (new)
    //   3. boxed:        no type info → boxed runtime comparison (fallback)
    bool semi_native_test = false;
    MIR_reg_t cached_bound = 0;
    MIR_insn_code_t cached_cmp_insn = MIR_LTS;
    bool cached_bound_on_right = true;
    JsAstNode* cached_counter_node = NULL;
    TypeId cached_cmp_target = LMD_TYPE_INT;

    if (for_node->test && for_node->test->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* test_bin = (JsBinaryNode*)for_node->test;
        TypeId lt = jm_get_effective_type(mt, test_bin->left);
        TypeId rt = jm_get_effective_type(mt, test_bin->right);
        bool left_num  = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
        bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
        bool full_native = left_num && right_num;

        // Only consider semi-native when one side is typed, other isn't
        if (!full_native && (left_num || right_num)) {
            bool is_cmp = false;
            switch (test_bin->op) {
            case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
            case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
                is_cmp = true; break;
            default: break;
            }

            if (is_cmp) {
                JsAstNode* bound_expr;
                TypeId bound_type;
                bool use_float;

                if (left_num) {
                    // Pattern: typed_counter CMP untyped_bound  (e.g. i < n)
                    cached_counter_node = test_bin->left;
                    bound_expr = test_bin->right;
                    bound_type = rt;
                    use_float = (lt == LMD_TYPE_FLOAT);
                    cached_bound_on_right = true;
                } else {
                    // Pattern: untyped_bound CMP typed_counter  (e.g. 0 <= i)
                    cached_counter_node = test_bin->right;
                    bound_expr = test_bin->left;
                    bound_type = lt;
                    use_float = (rt == LMD_TYPE_FLOAT);
                    cached_bound_on_right = false;
                }

                cached_cmp_target = use_float ? LMD_TYPE_FLOAT : LMD_TYPE_INT;

                switch (test_bin->op) {
                case JS_OP_LT:        cached_cmp_insn = use_float ? MIR_DLT : MIR_LTS; break;
                case JS_OP_LE:        cached_cmp_insn = use_float ? MIR_DLE : MIR_LES; break;
                case JS_OP_GT:        cached_cmp_insn = use_float ? MIR_DGT : MIR_GTS; break;
                case JS_OP_GE:        cached_cmp_insn = use_float ? MIR_DGE : MIR_GES; break;
                case JS_OP_EQ:
                case JS_OP_STRICT_EQ: cached_cmp_insn = use_float ? MIR_DEQ : MIR_EQ;  break;
                case JS_OP_NE:
                case JS_OP_STRICT_NE: cached_cmp_insn = use_float ? MIR_DNE : MIR_NE;  break;
                default: break;
                }

                // Cache the bound ONCE before the loop.
                // For identifiers: safe assuming bound is loop-invariant (common for function params).
                // For other expressions: evaluated once, result cached.
                cached_bound = jm_transpile_as_native(mt, bound_expr, bound_type, cached_cmp_target);
                semi_native_test = true;
            }
        }
    }

    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_update = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Push loop labels
    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_update;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    jm_emit_label(mt, l_test);

    // Test
    if (for_node->test) {
        if (semi_native_test) {
            // Semi-native: read counter as native, compare with cached bound
            TypeId ct = jm_get_effective_type(mt, cached_counter_node);
            MIR_reg_t counter_reg = jm_transpile_as_native(mt, cached_counter_node, ct, cached_cmp_target);

            MIR_reg_t left_cmp  = cached_bound_on_right ? counter_reg  : cached_bound;
            MIR_reg_t right_cmp = cached_bound_on_right ? cached_bound : counter_reg;

            MIR_reg_t test_r = jm_new_reg(mt, "fltest", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, cached_cmp_insn,
                MIR_new_reg_op(mt->ctx, test_r),
                MIR_new_reg_op(mt->ctx, left_cmp),
                MIR_new_reg_op(mt->ctx, right_cmp)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
                MIR_new_reg_op(mt->ctx, test_r)));
        } else {
            // Full-native or boxed path (existing logic)
            bool native_test = false;
            if (for_node->test->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
                JsBinaryNode* test_bin = (JsBinaryNode*)for_node->test;
                TypeId lt = jm_get_effective_type(mt, test_bin->left);
                TypeId rt = jm_get_effective_type(mt, test_bin->right);
                native_test = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) &&
                              (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
            }

            if (native_test) {
                MIR_reg_t test = jm_transpile_expression(mt, for_node->test);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
                    MIR_new_reg_op(mt->ctx, test)));
            } else {
                MIR_reg_t test = jm_transpile_box_item(mt, for_node->test);
                MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
                    MIR_new_reg_op(mt->ctx, truthy)));
            }
        }
    }

    // Body
    if (for_node->body) {
        if (for_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)for_node->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, for_node->body);
        }
    }

    // Update — use native path for typed increment/assignment
    jm_emit_label(mt, l_update);
    if (for_node->update) {
        TypeId upd_type = jm_get_effective_type(mt, for_node->update);
        if (jm_is_native_type(upd_type)) {
            jm_transpile_expression(mt, for_node->update);
        } else {
            jm_transpile_box_item(mt, for_node->update);
        }
    }

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));
    jm_emit_label(mt, l_end);

    if (mt->loop_depth > 0) mt->loop_depth--;
    jm_pop_scope(mt);
}

// new expression: new TypedArray(len), new Array(len), new Object()
static MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call) {
    if (!call->callee) return jm_emit_null(mt);

    const char* ctor_name = NULL;
    int ctor_len = 0;
    if (call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
        ctor_name = id->name->chars;
        ctor_len = (int)id->name->len;
    }

    if (!ctor_name) {
        log_error("js-mir: new expression with non-identifier constructor");
        return jm_emit_null(mt);
    }

    // Evaluate first argument (length for typed arrays / Array)
    MIR_reg_t first_arg = 0;
    int arg_count = jm_count_args(call->arguments);
    if (call->arguments) {
        first_arg = jm_transpile_box_item(mt, call->arguments);
    }

    // Typed arrays: Int8Array, Uint8Array, Int16Array, Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array
    int typed_array_type = -1;
    if (ctor_len == 10 && strncmp(ctor_name, "Int32Array", 10) == 0) typed_array_type = 4; // JS_TYPED_INT32
    else if (ctor_len == 10 && strncmp(ctor_name, "Int16Array", 10) == 0) typed_array_type = 2; // JS_TYPED_INT16
    else if (ctor_len == 9 && strncmp(ctor_name, "Int8Array", 9) == 0) typed_array_type = 0; // JS_TYPED_INT8
    else if (ctor_len == 11 && strncmp(ctor_name, "Uint32Array", 11) == 0) typed_array_type = 5; // JS_TYPED_UINT32
    else if (ctor_len == 11 && strncmp(ctor_name, "Uint16Array", 11) == 0) typed_array_type = 3; // JS_TYPED_UINT16
    else if (ctor_len == 10 && strncmp(ctor_name, "Uint8Array", 10) == 0) typed_array_type = 1; // JS_TYPED_UINT8
    else if (ctor_len == 12 && strncmp(ctor_name, "Float64Array", 12) == 0) typed_array_type = 7; // JS_TYPED_FLOAT64
    else if (ctor_len == 12 && strncmp(ctor_name, "Float32Array", 12) == 0) typed_array_type = 6; // JS_TYPED_FLOAT32

    if (typed_array_type >= 0) {
        MIR_reg_t len_arg = first_arg ? first_arg : jm_box_int_const(mt, 0);
        return jm_call_2(mt, "js_typed_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, typed_array_type),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, len_arg));
    }

    // new Array(len)
    if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) {
        MIR_reg_t len_arg = first_arg ? first_arg : jm_box_int_const(mt, 0);
        return jm_call_1(mt, "js_array_new", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, len_arg));
    }

    // new Object()
    if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) {
        return jm_call_0(mt, "js_new_object", MIR_T_I64);
    }

    // new Error(message) — built-in Error constructor
    if (ctor_len == 5 && strncmp(ctor_name, "Error", 5) == 0) {
        MIR_reg_t msg_arg = first_arg ? first_arg : jm_emit_null(mt);
        return jm_call_1(mt, "js_new_error", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, msg_arg));
    }

    // User-defined class instantiation: new ClassName(args)
    JsClassEntry* ce = jm_find_class(mt, ctor_name, ctor_len);
    if (ce) {
        // Create new empty object
        MIR_reg_t obj = jm_call_0(mt, "js_new_object", MIR_T_I64);

        // Add all non-constructor methods as properties
        for (int i = 0; i < ce->method_count; i++) {
            JsClassMethodEntry* me = &ce->methods[i];
            if (me->is_constructor) continue;
            if (!me->fc || !me->fc->func_item || !me->name) continue;

            MIR_reg_t fn_item = jm_call_2(mt, "js_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_ref_op(mt->ctx, me->fc->func_item),
                MIR_T_I64, MIR_new_int_op(mt->ctx, me->param_count));
            MIR_reg_t key = jm_box_string_literal(mt, me->name->chars, me->name->len);
            jm_call_3(mt, "js_property_set", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
        }

        // Call constructor with this = obj
        if (ce->constructor && ce->constructor->fc && ce->constructor->fc->func_item) {
            MIR_reg_t ctor_fn = jm_call_2(mt, "js_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_ref_op(mt->ctx, ce->constructor->fc->func_item),
                MIR_T_I64, MIR_new_int_op(mt->ctx, ce->constructor->param_count));
            MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
            jm_call_4(mt, "js_call_function", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, ctor_fn),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
                MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
                MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
        }

        return obj;
    }

    // Fallback: user-defined constructor function (new FunctionName(args))
    // 1. Create empty object
    // 2. Look up constructor function via scope resolution (same as regular calls)
    // 3. Call constructor with this = object
    // 4. Return object
    log_info("js-mir: new %.*s — treating as constructor function", ctor_len, ctor_name);
    MIR_reg_t obj = jm_call_0(mt, "js_new_object", MIR_T_I64);

    // Use scope-based resolution (same pattern as jm_transpile_call)
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    NameEntry* entry = js_scope_lookup(mt->tp, id->name);

    JsFunctionNode* resolved_fn = NULL;
    if (entry && entry->node) {
        JsAstNodeType ntype = ((JsAstNode*)entry->node)->node_type;
        if (ntype == JS_AST_NODE_FUNCTION_DECLARATION) {
            resolved_fn = (JsFunctionNode*)entry->node;
        } else if (ntype == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)entry->node;
            if (decl->init && (decl->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION
                || decl->init->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
                resolved_fn = (JsFunctionNode*)decl->init;
            }
        }
    }

    MIR_reg_t callee;
    if (resolved_fn) {
        JsFuncCollected* fc = jm_find_collected_func(mt, resolved_fn);
        if (fc && fc->func_item && fc->capture_count == 0) {
            int pc = jm_count_params(resolved_fn);
            callee = jm_call_2(mt, "js_new_function", MIR_T_I64,
                MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
        } else {
            // Resolved but not collected — fall back to transpiling callee
            callee = jm_transpile_box_item(mt, call->callee);
        }
    } else {
        // Fall back to transpiling the callee expression (for variables holding functions)
        callee = jm_transpile_box_item(mt, call->callee);
    }

    MIR_reg_t args_ptr = jm_build_args_array(mt, call->arguments, arg_count);
    jm_call_4(mt, "js_call_function", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, callee),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
        MIR_T_I64, args_ptr ? MIR_new_reg_op(mt->ctx, args_ptr) : MIR_new_int_op(mt->ctx, 0),
        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
    return obj;
}

// switch statement
static void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw) {
    jm_push_scope(mt);

    MIR_reg_t discriminant = jm_transpile_box_item(mt, sw->discriminant);
    MIR_label_t l_end = jm_new_label(mt);

    // Push break label for the switch (break exits the switch)
    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = 0; // no continue in switch
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    // Collect case labels and default
    int case_count = 0;
    JsSwitchCaseNode* cases[128];
    JsAstNode* c = sw->cases;
    while (c && case_count < 128) {
        cases[case_count++] = (JsSwitchCaseNode*)c;
        c = c->next;
    }

    // Generate labels for each case body
    MIR_label_t case_labels[128];
    for (int i = 0; i < case_count; i++) {
        case_labels[i] = jm_new_label(mt);
    }

    // Test phase: for each non-default case, compare discriminant with test value
    // and branch to the corresponding case body label
    int default_idx = -1;
    for (int i = 0; i < case_count; i++) {
        if (!cases[i]->test) {
            default_idx = i;
            continue;
        }
        MIR_reg_t test_val = jm_transpile_box_item(mt, cases[i]->test);
        MIR_reg_t eq = jm_call_2(mt, "js_strict_equal", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, discriminant),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test_val));
        MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, eq));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, case_labels[i]),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    // If no case matched, jump to default or end
    if (default_idx >= 0) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, case_labels[default_idx])));
    } else {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    }

    // Body phase: emit each case body with fall-through semantics
    for (int i = 0; i < case_count; i++) {
        jm_emit_label(mt, case_labels[i]);
        JsAstNode* s = cases[i]->consequent;
        while (s) {
            jm_transpile_statement(mt, s);
            s = s->next;
        }
        // Fall through to next case (break will jump to l_end)
    }

    jm_emit_label(mt, l_end);
    if (mt->loop_depth > 0) mt->loop_depth--;
    jm_pop_scope(mt);
}

// do-while statement
static void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw) {
    MIR_label_t l_body = jm_new_label(mt);
    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_test;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    // Body first
    jm_emit_label(mt, l_body);
    if (dw->body) {
        if (dw->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)dw->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, dw->body);
        }
    }

    // Test
    jm_emit_label(mt, l_test);
    if (dw->test) {
        MIR_reg_t test = jm_transpile_box_item(mt, dw->test);
        MIR_reg_t truthy = jm_call_1(mt, "js_is_truthy", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, test));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_body),
            MIR_new_reg_op(mt->ctx, truthy)));
    }

    jm_emit_label(mt, l_end);
    if (mt->loop_depth > 0) mt->loop_depth--;
}

// for-of / for-in statement
// Uses fn_len + js_property_access for arrays, or js_object_keys for objects
static void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo) {
    jm_push_scope(mt);

    // Check if the loop variable is a destructuring pattern
    JsArrayPatternNode* destr_pattern = NULL;
    const char* var_name = NULL;
    int var_len = 0;

    if (fo->left && fo->left->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)fo->left;
        if (decl->declarations && decl->declarations->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl->declarations;
            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                var_name = id->name->chars;
                var_len = (int)id->name->len;
            } else if (d->id && d->id->node_type == JS_AST_NODE_ARRAY_PATTERN) {
                destr_pattern = (JsArrayPatternNode*)d->id;
            }
        }
    } else if (fo->left && fo->left->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)fo->left;
        var_name = id->name->chars;
        var_len = (int)id->name->len;
    } else if (fo->left && fo->left->node_type == JS_AST_NODE_ARRAY_PATTERN) {
        // for (const [a, b] of arr) — left is array_pattern directly
        destr_pattern = (JsArrayPatternNode*)fo->left;
    }

    if (!var_name && !destr_pattern) {
        log_error("js-mir: for-of/for-in missing loop variable");
        jm_pop_scope(mt);
        return;
    }

    // Create loop variable (for simple case) or temp var (for destructuring)
    MIR_reg_t loop_var;
    if (var_name) {
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", var_len, var_name);
        loop_var = jm_new_reg(mt, vname, MIR_T_I64);
        jm_set_var(mt, vname, loop_var);
    } else {
        loop_var = jm_new_reg(mt, "_destr_elem", MIR_T_I64);
    }
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, loop_var),
        MIR_new_int_op(mt->ctx, ITEM_NULL_VAL)));

    // Pre-create destructuring variable registers
    if (destr_pattern) {
        JsAstNode* pe = destr_pattern->elements;
        while (pe) {
            if (pe->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)pe;
                char pvname[128];
                snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                MIR_reg_t preg = jm_new_reg(mt, pvname, MIR_T_I64);
                jm_set_var(mt, pvname, preg);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
                    MIR_new_int_op(mt->ctx, ITEM_NULL_VAL)));
            } else if (pe->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)pe;
                if (sp->argument && sp->argument->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* pid = (JsIdentifierNode*)sp->argument;
                    char pvname[128];
                    snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                    MIR_reg_t preg = jm_new_reg(mt, pvname, MIR_T_I64);
                    jm_set_var(mt, pvname, preg);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, preg),
                        MIR_new_int_op(mt->ctx, ITEM_NULL_VAL)));
                }
            }
            pe = pe->next;
        }
    }

    // Evaluate right-hand side (the iterable)
    MIR_reg_t iterable = jm_transpile_box_item(mt, fo->right);

    // For for-in: get keys array first
    bool is_for_in = (fo->base.node_type == JS_AST_NODE_FOR_IN_STATEMENT);
    MIR_reg_t collection = iterable;
    if (is_for_in) {
        collection = jm_call_1(mt, "js_object_keys", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, iterable));
    }

    // Get length (as raw int, not boxed Item)
    MIR_reg_t len = jm_call_1(mt, "js_array_length", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, collection));

    // Index counter
    MIR_reg_t idx = jm_new_reg(mt, "foridx", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_int_op(mt->ctx, 0)));

    MIR_label_t l_test = jm_new_label(mt);
    MIR_label_t l_update = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    if (mt->loop_depth < 32) {
        mt->loop_stack[mt->loop_depth].continue_label = l_update;
        mt->loop_stack[mt->loop_depth].break_label = l_end;
        mt->loop_depth++;
    }

    // Test: idx < len
    jm_emit_label(mt, l_test);
    MIR_reg_t cmp = jm_new_reg(mt, "foricmp", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, cmp),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_reg_op(mt->ctx, len)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
        MIR_new_reg_op(mt->ctx, cmp)));

    // Get current element: collection[idx]
    MIR_reg_t idx_item = jm_box_int_reg(mt, idx);
    MIR_reg_t elem = jm_call_2(mt, "js_property_access", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, collection),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_item));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, loop_var), MIR_new_reg_op(mt->ctx, elem)));

    // Destructure element into individual variables if array pattern
    if (destr_pattern) {
        int di = 0;
        JsAstNode* pe = destr_pattern->elements;
        while (pe) {
            if (pe->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)pe;
                char pvname[128];
                snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                JsMirVarEntry* pvar = jm_find_var(mt, pvname);
                if (pvar) {
                    MIR_reg_t dkey = jm_box_int_const(mt, di);
                    MIR_reg_t dval = jm_call_2(mt, "js_property_access", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, dkey));
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, pvar->reg),
                        MIR_new_reg_op(mt->ctx, dval)));
                }
            } else if (pe->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* sp = (JsSpreadElementNode*)pe;
                if (sp->argument && sp->argument->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* pid = (JsIdentifierNode*)sp->argument;
                    char pvname[128];
                    snprintf(pvname, sizeof(pvname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                    JsMirVarEntry* pvar = jm_find_var(mt, pvname);
                    if (pvar) {
                        MIR_reg_t dstart = jm_box_int_const(mt, di);
                        MIR_reg_t dval = jm_call_2(mt, "js_array_slice_from", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, loop_var),
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, dstart));
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, pvar->reg),
                            MIR_new_reg_op(mt->ctx, dval)));
                    }
                }
            }
            pe = pe->next;
            di++;
        }
    }

    // Body
    if (fo->body) {
        if (fo->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)fo->body;
            JsAstNode* s = blk->statements;
            while (s) { jm_transpile_statement(mt, s); s = s->next; }
        } else {
            jm_transpile_statement(mt, fo->body);
        }
    }

    // Update: idx++
    jm_emit_label(mt, l_update);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, idx),
        MIR_new_reg_op(mt->ctx, idx), MIR_new_int_op(mt->ctx, 1)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_test)));

    jm_emit_label(mt, l_end);
    if (mt->loop_depth > 0) mt->loop_depth--;
    jm_pop_scope(mt);
}

static void jm_transpile_return(JsMirTranspiler* mt, JsReturnNode* ret) {
    MIR_reg_t val;

    // Phase 4: In native function, return native value directly
    if (mt->in_native_func && mt->current_fc) {
        TypeId ret_type = mt->current_fc->return_type;

        // TCO: set tail position so recursive calls in the argument can be converted to goto
        bool saved_tail = mt->in_tail_position;
        if (mt->tco_func) {
            mt->in_tail_position = true;
            mt->tco_jumped = false;
        }

        if (ret->argument) {
            TypeId expr_type = jm_get_effective_type(mt, ret->argument);
            if (jm_is_native_type(expr_type)) {
                // Expression already returns native — convert to target type
                val = jm_transpile_as_native(mt, ret->argument, expr_type, ret_type);
            } else {
                // Expression returns boxed — unbox to native
                MIR_reg_t boxed = jm_transpile_box_item(mt, ret->argument);
                if (ret_type == LMD_TYPE_FLOAT) {
                    val = jm_emit_unbox_float(mt, boxed);
                } else {
                    val = jm_emit_unbox_int(mt, boxed);
                }
            }
        } else {
            // return; → return 0 (native)
            MIR_type_t mtype = (ret_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
            val = jm_new_reg(mt, "ret0", mtype);
            if (ret_type == LMD_TYPE_FLOAT) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, val), MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, 0)));
            }
        }

        mt->in_tail_position = saved_tail;

        // If TCO converted the call to a goto, skip the ret — it's dead code
        if (mt->tco_jumped) {
            mt->tco_jumped = false;
            return;
        }

        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
        return;
    }

    if (ret->argument) {
        val = jm_transpile_box_item(mt, ret->argument);
    } else {
        val = jm_emit_null(mt);
    }

    // If inside a try block, delay the return and jump to finally/end
    if (mt->try_ctx_depth > 0) {
        JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth - 1];
        // Store the return value and set the return flag
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, tc->return_val_reg),
            MIR_new_reg_op(mt->ctx, val)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, tc->has_return_reg),
            MIR_new_int_op(mt->ctx, 1)));
        // Jump to finally (or end if no finally)
        MIR_label_t target = tc->has_finally ? tc->finally_label : tc->end_label;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, target)));
        return;
    }

    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
}

// Statement dispatcher
static void jm_transpile_statement(JsMirTranspiler* mt, JsAstNode* stmt) {
    if (!stmt) return;

    switch (stmt->node_type) {
    case JS_AST_NODE_VARIABLE_DECLARATION:
        jm_transpile_var_decl(mt, (JsVariableDeclarationNode*)stmt);
        break;
    case JS_AST_NODE_FUNCTION_DECLARATION:
        // Already handled in pre-pass; skip
        break;
    case JS_AST_NODE_CLASS_DECLARATION:
        // Already handled in pre-pass (class collection); skip
        break;
    case JS_AST_NODE_IF_STATEMENT:
        jm_transpile_if(mt, (JsIfNode*)stmt);
        break;
    case JS_AST_NODE_WHILE_STATEMENT:
        jm_transpile_while(mt, (JsWhileNode*)stmt);
        break;
    case JS_AST_NODE_FOR_STATEMENT:
        jm_transpile_for(mt, (JsForNode*)stmt);
        break;
    case JS_AST_NODE_DO_WHILE_STATEMENT:
        jm_transpile_do_while(mt, (JsDoWhileNode*)stmt);
        break;
    case JS_AST_NODE_SWITCH_STATEMENT:
        jm_transpile_switch(mt, (JsSwitchNode*)stmt);
        break;
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT:
        jm_transpile_for_of(mt, (JsForOfNode*)stmt);
        break;
    case JS_AST_NODE_RETURN_STATEMENT:
        jm_transpile_return(mt, (JsReturnNode*)stmt);
        break;
    case JS_AST_NODE_BREAK_STATEMENT:
        if (mt->loop_depth > 0) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
        }
        break;
    case JS_AST_NODE_CONTINUE_STATEMENT:
        if (mt->loop_depth > 0) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].continue_label)));
        }
        break;
    case JS_AST_NODE_BLOCK_STATEMENT: {
        jm_push_scope(mt);
        JsBlockNode* blk = (JsBlockNode*)stmt;
        JsAstNode* s = blk->statements;
        while (s) { jm_transpile_statement(mt, s); s = s->next; }
        jm_pop_scope(mt);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)stmt;
        if (es->expression) {
            jm_transpile_box_item(mt, es->expression);
        }
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* try_node = (JsTryNode*)stmt;
        bool has_catch = (try_node->handler != NULL);
        bool has_finally = (try_node->finalizer != NULL);

        // Create labels
        MIR_label_t catch_label = has_catch ? jm_new_label(mt) : 0;
        MIR_label_t finally_label = has_finally ? jm_new_label(mt) : 0;
        MIR_label_t end_label = jm_new_label(mt);

        // Create registers for delayed return handling
        MIR_reg_t return_val_reg = jm_new_reg(mt, "_try_ret", MIR_T_I64);
        MIR_reg_t has_return_reg = jm_new_reg(mt, "_try_has_ret", MIR_T_I64);

        // Initialize return tracking
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, return_val_reg),
            MIR_new_int_op(mt->ctx, 0)));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, has_return_reg),
            MIR_new_int_op(mt->ctx, 0)));

        // Push try context
        if (mt->try_ctx_depth < 16) {
            JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
            tc->catch_label = catch_label;
            tc->finally_label = finally_label;
            tc->end_label = end_label;
            tc->return_val_reg = return_val_reg;
            tc->has_return_reg = has_return_reg;
            tc->has_catch = has_catch;
            tc->has_finally = has_finally;
        }

        // === Try body ===
        if (try_node->block && try_node->block->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            JsBlockNode* blk = (JsBlockNode*)try_node->block;
            JsAstNode* s = blk->statements;
            while (s) {
                jm_transpile_statement(mt, s);
                // After each statement, check if an exception was thrown
                // (from a called function that set the flag and returned)
                if (has_catch) {
                    MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                        MIR_new_label_op(mt->ctx, catch_label),
                        MIR_new_reg_op(mt->ctx, exc_check)));
                } else if (has_finally) {
                    // try/finally without catch: check and jump to finally, then propagate
                    MIR_reg_t exc_check = jm_call_0(mt, "js_check_exception", MIR_T_I64);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                        MIR_new_label_op(mt->ctx, finally_label),
                        MIR_new_reg_op(mt->ctx, exc_check)));
                }
                s = s->next;
            }
        }

        // Normal exit from try: jump to finally (or end)
        // Pop the try context so throws in catch propagate to outer handler
        if (mt->try_ctx_depth > 0) mt->try_ctx_depth--;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, has_finally ? finally_label : end_label)));

        // === Catch block ===
        if (has_catch) {
            jm_emit_label(mt, catch_label);
            JsCatchNode* catch_node = (JsCatchNode*)try_node->handler;

            // Clear exception and get thrown value
            MIR_reg_t thrown_val = jm_call_0(mt, "js_clear_exception", MIR_T_I64);

            // If there's a finally block, push a context for return-in-catch
            // (so return in catch still goes through finally)
            // This context has no catch_label, so throws propagate outward
            if (has_finally && mt->try_ctx_depth < 16) {
                JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth++];
                tc->catch_label = 0;
                tc->finally_label = finally_label;
                tc->end_label = end_label;
                tc->return_val_reg = return_val_reg;
                tc->has_return_reg = has_return_reg;
                tc->has_catch = false;
                tc->has_finally = true;
            }

            // Bind the catch parameter
            jm_push_scope(mt);
            if (catch_node->param && catch_node->param->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* param_id = (JsIdentifierNode*)catch_node->param;
                char vname[128];
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)param_id->name->len, param_id->name->chars);
                jm_set_var(mt, vname, thrown_val);
            }

            // Transpile catch body
            if (catch_node->body && catch_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)catch_node->body;
                JsAstNode* s = blk->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
            }
            jm_pop_scope(mt);

            // Pop catch-finally context if we pushed one
            if (has_finally && mt->try_ctx_depth > 0) mt->try_ctx_depth--;

            // Jump to finally (or end)
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, has_finally ? finally_label : end_label)));
        }

        // === Finally block ===
        if (has_finally) {
            jm_emit_label(mt, finally_label);
            if (try_node->finalizer->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* fin = (JsBlockNode*)try_node->finalizer;
                JsAstNode* s = fin->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
            }

            // After finally: check if we had a delayed return
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                MIR_new_label_op(mt->ctx, end_label),
                MIR_new_reg_op(mt->ctx, has_return_reg)));
        }

        // End label: check for delayed return
        jm_emit_label(mt, end_label);

        // If has_return_reg is set, issue the actual return
        MIR_label_t no_ret_label = jm_new_label(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, no_ret_label),
            MIR_new_reg_op(mt->ctx, has_return_reg)));
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
            MIR_new_reg_op(mt->ctx, return_val_reg)));
        jm_emit_label(mt, no_ret_label);

        // If exception is still pending (try/finally without catch, or re-throw),
        // propagate by returning ItemNull
        if (!has_catch || has_finally) {
            MIR_reg_t still_exc = jm_call_0(mt, "js_check_exception", MIR_T_I64);
            MIR_label_t no_exc_label = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
                MIR_new_label_op(mt->ctx, no_exc_label),
                MIR_new_reg_op(mt->ctx, still_exc)));
            MIR_reg_t null_ret = jm_emit_null(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_reg_op(mt->ctx, null_ret)));
            jm_emit_label(mt, no_exc_label);
        }
        break;
    }
    case JS_AST_NODE_THROW_STATEMENT: {
        JsThrowNode* throw_node = (JsThrowNode*)stmt;
        MIR_reg_t thrown_val = jm_emit_null(mt);
        if (throw_node->argument) {
            thrown_val = jm_transpile_box_item(mt, throw_node->argument);
        }

        // If inside a try block, set the flag and jump to catch/finally
        if (mt->try_ctx_depth > 0) {
            JsTryContext* tc = &mt->try_ctx_stack[mt->try_ctx_depth - 1];
            // Store the thrown value in the global exception state
            jm_call_void_1(mt, "js_throw_value",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, thrown_val));
            // Jump to catch (or finally if no catch)
            MIR_label_t target = tc->has_catch ? tc->catch_label
                               : (tc->has_finally ? tc->finally_label : tc->end_label);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                MIR_new_label_op(mt->ctx, target)));
        } else {
            // Not in a try block: set flag and return from function
            // (the caller's try block will check js_check_exception)
            jm_call_void_1(mt, "js_throw_value",
                MIR_T_I64, MIR_new_reg_op(mt->ctx, thrown_val));
            MIR_reg_t null_ret = jm_emit_null(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1,
                MIR_new_reg_op(mt->ctx, null_ret)));
        }
        break;
    }
    default:
        log_error("js-mir: unsupported statement type %d", stmt->node_type);
        break;
    }
}

// ============================================================================
// Function definition transpiler
// ============================================================================

static void jm_define_function(JsMirTranspiler* mt, JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    int param_count = jm_count_params(fn);
    bool has_captures = (fc->capture_count > 0);

    // Phase 4: Check if this function qualifies for a native version.
    // Requirements: no captures, all params inferred as INT or FLOAT,
    //               return type is INT or FLOAT, param_count <= 16.
    bool generate_native = false;
    if (!has_captures && param_count > 0 && param_count <= 16 &&
        (fc->return_type == LMD_TYPE_INT || fc->return_type == LMD_TYPE_FLOAT)) {
        generate_native = true;
        for (int i = 0; i < param_count; i++) {
            if (fc->param_types[i] != LMD_TYPE_INT && fc->param_types[i] != LMD_TYPE_FLOAT) {
                generate_native = false;
                break;
            }
        }
    }

    // --- Generate native version if eligible ---
    if (generate_native) {
        // Create native function: <name>_n(native_p1, native_p2, ...) → native_ret
        char native_name[140];
        snprintf(native_name, sizeof(native_name), "%s_n", fc->name);

        MIR_var_t* n_params = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
        char** n_param_names = (char**)alloca(param_count * sizeof(char*));
        JsAstNode* param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            n_param_names[i] = (char*)alloca(128);
            if (param_node && param_node->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
                snprintf(n_param_names[i], 128, "_js_%.*s", (int)pid->name->len, pid->name->chars);
            } else {
                snprintf(n_param_names[i], 128, "_js_p%d", i);
            }
            MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
            n_params[i] = {mtype, n_param_names[i], 0};
            param_node = param_node ? param_node->next : NULL;
        }

        MIR_type_t native_ret_type = (fc->return_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
        MIR_item_t native_item = MIR_new_func_arr(mt->ctx, native_name, 1, &native_ret_type, param_count, n_params);
        MIR_func_t native_func = MIR_get_item_func(mt->ctx, native_item);

        fc->native_func_item = native_item;
        fc->has_native_version = true;
        jm_register_local_func(mt, native_name, native_item);

        // Save transpiler state
        MIR_item_t saved_item = mt->current_func_item;
        MIR_func_t saved_func = mt->current_func;
        int saved_scope_depth = mt->scope_depth;
        int saved_loop_depth = mt->loop_depth;
        bool saved_in_native = mt->in_native_func;
        JsFuncCollected* saved_fc = mt->current_fc;
        // Save TCO state
        JsFuncCollected* saved_tco_func = mt->tco_func;
        MIR_label_t saved_tco_label = mt->tco_label;
        MIR_reg_t saved_tco_count = mt->tco_count_reg;
        bool saved_tail_pos = mt->in_tail_position;

        mt->current_func_item = native_item;
        mt->current_func = native_func;
        mt->loop_depth = 0;
        mt->in_native_func = true;
        mt->current_fc = fc;
        mt->tco_func = NULL;
        mt->in_tail_position = false;
        mt->tco_jumped = false;

        jm_push_scope(mt);

        // Register parameters with their inferred native types
        param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            if (param_node && param_node->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
                char vname[128];
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                MIR_reg_t preg = MIR_reg(mt->ctx, vname, native_func);
                MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
                jm_set_var(mt, vname, preg, mtype, fc->param_types[i]);
            }
            param_node = param_node ? param_node->next : NULL;
        }

        // TCO setup: wrap body in a loop if this function has tail-recursive calls
        if (fc->is_tco_eligible) {
            log_debug("js-mir TCO: enabling loop transform for %s", fc->name);
            mt->tco_func = fc;

            // Create iteration counter, init to 0
            mt->tco_count_reg = jm_new_reg(mt, "tco_count", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_int_op(mt->ctx, 0)));

            // TCO loop label
            mt->tco_label = jm_new_label(mt);
            jm_emit_label(mt, mt->tco_label);

            // Increment: tco_count += 1
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_int_op(mt->ctx, 1)));

            // Guard: if (tco_count <= 1000000) goto ok
            MIR_label_t ok_label = jm_new_label(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BLE, MIR_new_label_op(mt->ctx, ok_label),
                MIR_new_reg_op(mt->ctx, mt->tco_count_reg),
                MIR_new_int_op(mt->ctx, 1000000)));

            // Overflow: return 0 (safety net — should never trigger for correct code)
            if (native_ret_type == MIR_T_D) {
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_int_op(mt->ctx, 0)));
            }

            jm_emit_label(mt, ok_label);
        }

        // Transpile body (same as original, but params are native-typed)
        if (fn->body) {
            if (fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)fn->body;
                JsAstNode* s = blk->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
            } else {
                // Expression body (arrow function): return native value
                MIR_reg_t val = jm_transpile_as_native(mt, fn->body,
                    jm_get_effective_type(mt, fn->body), fc->return_type);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
                goto finish_native;
            }
        }

        // Implicit return 0 (native)
        {
            MIR_reg_t zero = jm_new_reg(mt, "ret0", native_ret_type);
            if (native_ret_type == MIR_T_D) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, zero), MIR_new_double_op(mt->ctx, 0.0)));
            } else {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, zero), MIR_new_int_op(mt->ctx, 0)));
            }
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, zero)));
        }

    finish_native:
        jm_pop_scope(mt);
        MIR_finish_func(mt->ctx);

        // Restore state
        mt->current_func_item = saved_item;
        mt->current_func = saved_func;
        mt->scope_depth = saved_scope_depth;
        mt->loop_depth = saved_loop_depth;
        mt->in_native_func = saved_in_native;
        mt->current_fc = saved_fc;
        mt->tco_func = saved_tco_func;
        mt->tco_label = saved_tco_label;
        mt->tco_count_reg = saved_tco_count;
        mt->in_tail_position = saved_tail_pos;
        mt->tco_jumped = false;

        log_debug("js-mir P4: generated native version %s (params: %d, ret: %s%s)",
            native_name, param_count,
            fc->return_type == LMD_TYPE_INT ? "INT" : "FLOAT",
            fc->is_tco_eligible ? ", TCO" : "");
    }

    // --- Generate boxed version (original or wrapper) ---
    int total_params = param_count + (has_captures ? 1 : 0);
    MIR_var_t* params = (MIR_var_t*)alloca(total_params * sizeof(MIR_var_t));
    char** param_names_arr = (char**)alloca(total_params * sizeof(char*));

    int pi = 0;
    if (has_captures) {
        param_names_arr[pi] = (char*)alloca(128);
        snprintf(param_names_arr[pi], 128, "_js_env");
        params[pi] = {MIR_T_I64, param_names_arr[pi], 0};
        pi++;
    }

    JsAstNode* param_node = fn->params;
    for (int i = 0; i < param_count; i++) {
        param_names_arr[pi] = (char*)alloca(128);
        if (param_node && param_node->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
            snprintf(param_names_arr[pi], 128, "_js_%.*s", (int)pid->name->len, pid->name->chars);
        } else {
            snprintf(param_names_arr[pi], 128, "_js_p%d", i);
        }
        params[pi] = {MIR_T_I64, param_names_arr[pi], 0};
        param_node = param_node ? param_node->next : NULL;
        pi++;
    }

    MIR_type_t ret_type = MIR_T_I64;
    MIR_item_t func_item = MIR_new_func_arr(mt->ctx, fc->name, 1, &ret_type, total_params, params);
    MIR_func_t func = MIR_get_item_func(mt->ctx, func_item);

    fc->func_item = func_item;
    jm_register_local_func(mt, fc->name, func_item);

    // Save transpiler state
    MIR_item_t saved_item = mt->current_func_item;
    MIR_func_t saved_func = mt->current_func;
    int saved_scope_depth = mt->scope_depth;
    int saved_loop_depth = mt->loop_depth;
    bool saved_in_native = mt->in_native_func;
    JsFuncCollected* saved_fc = mt->current_fc;

    mt->current_func_item = func_item;
    mt->current_func = func;
    mt->loop_depth = 0;
    mt->in_native_func = false;
    mt->current_fc = fc;

    jm_push_scope(mt);

    // If we have a native version, the boxed version becomes a thin wrapper:
    // unbox params → call native → box result
    if (generate_native) {
        // Build native call: result = native_func(unboxed_p1, unboxed_p2, ...)
        char proto_name[160];
        snprintf(proto_name, sizeof(proto_name), "%s_n_wp%d", fc->name, mt->label_counter++);

        MIR_var_t* np_args = (MIR_var_t*)alloca(param_count * sizeof(MIR_var_t));
        for (int i = 0; i < param_count; i++) {
            MIR_type_t mtype = (fc->param_types[i] == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
            np_args[i] = {mtype, "a", 0};
        }
        MIR_type_t native_ret_type = (fc->return_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
        MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, 1, &native_ret_type, param_count, np_args);

        int nops = 3 + param_count;
        MIR_op_t* ops = (MIR_op_t*)alloca(nops * sizeof(MIR_op_t));
        int oi = 0;
        ops[oi++] = MIR_new_ref_op(mt->ctx, proto);
        ops[oi++] = MIR_new_ref_op(mt->ctx, fc->native_func_item);
        MIR_reg_t native_result = jm_new_reg(mt, "nret", native_ret_type);
        ops[oi++] = MIR_new_reg_op(mt->ctx, native_result);

        // Unbox each parameter
        param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            char vname[128] = "_js_p";
            if (param_node && param_node->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
            }
            MIR_reg_t preg = MIR_reg(mt->ctx, vname, func);

            if (fc->param_types[i] == LMD_TYPE_FLOAT) {
                MIR_reg_t unboxed = jm_emit_unbox_float(mt, preg);
                ops[oi++] = MIR_new_reg_op(mt->ctx, unboxed);
            } else {
                MIR_reg_t unboxed = jm_emit_unbox_int(mt, preg);
                ops[oi++] = MIR_new_reg_op(mt->ctx, unboxed);
            }
            param_node = param_node ? param_node->next : NULL;
        }

        jm_emit(mt, MIR_new_insn_arr(mt->ctx, MIR_CALL, nops, ops));

        // Box the result and return
        MIR_reg_t boxed_result = jm_box_native(mt, native_result, fc->return_type);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, boxed_result)));

        goto finish_boxed;
    }

    // --- Full boxed version (no native available) ---

    // For closures: get env register and load captured variables from env slots
    {
        MIR_reg_t env_reg = 0;
        if (has_captures) {
            env_reg = MIR_reg(mt->ctx, "_js_env", func);
            for (int i = 0; i < fc->capture_count; i++) {
                MIR_reg_t cap_reg = jm_new_reg(mt, fc->captures[i].name, MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, cap_reg),
                    MIR_new_mem_op(mt->ctx, MIR_T_I64, i * (int)sizeof(uint64_t), env_reg, 0, 1)));
                JsVarScopeEntry entry;
                memset(&entry, 0, sizeof(entry));
                snprintf(entry.name, sizeof(entry.name), "%s", fc->captures[i].name);
                entry.var.reg = cap_reg;
                entry.var.from_env = true;
                entry.var.env_slot = i;
                entry.var.env_reg = env_reg;
                hashmap_set(mt->var_scopes[mt->scope_depth], &entry);
            }
        }

        // Register parameter variables
        param_node = fn->params;
        for (int i = 0; i < param_count; i++) {
            if (param_node && param_node->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
                char vname[128];
                snprintf(vname, sizeof(vname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                MIR_reg_t preg = MIR_reg(mt->ctx, vname, func);
                jm_set_var(mt, vname, preg);
            }
            param_node = param_node ? param_node->next : NULL;
        }

        // Transpile body
        if (fn->body) {
            if (fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
                JsBlockNode* blk = (JsBlockNode*)fn->body;
                JsAstNode* s = blk->statements;
                while (s) { jm_transpile_statement(mt, s); s = s->next; }
            } else {
                MIR_reg_t val = jm_transpile_box_item(mt, fn->body);
                jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, val)));
                goto finish_boxed;
            }
        }

        // Implicit return ITEM_NULL
        {
            MIR_reg_t null_val = jm_emit_null(mt);
            jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, null_val)));
        }
    }

finish_boxed:
    jm_pop_scope(mt);
    MIR_finish_func(mt->ctx);

    // Restore state
    mt->current_func_item = saved_item;
    mt->current_func = saved_func;
    mt->scope_depth = saved_scope_depth;
    mt->loop_depth = saved_loop_depth;
    mt->in_native_func = saved_in_native;
    mt->current_fc = saved_fc;
}

// ============================================================================
// AST root transpilation
// ============================================================================

void transpile_js_mir_ast(JsMirTranspiler* mt, JsAstNode* root) {
    if (!root || root->node_type != JS_AST_NODE_PROGRAM) {
        log_error("js-mir: expected program node");
        return;
    }

    JsProgramNode* program = (JsProgramNode*)root;

    // Phase 1: Collect all functions (post-order: innermost first)
    jm_collect_functions(mt, root);
    log_debug("js-mir: collected %d functions, %d classes", mt->func_count, mt->class_count);
    for (int i = 0; i < mt->class_count; i++) {
        JsClassEntry* ce = &mt->class_entries[i];
        log_debug("js-mir: class '%.*s' with %d methods, ctor=%p",
            ce->name ? (int)ce->name->len : 0, ce->name ? ce->name->chars : "",
            ce->method_count, (void*)ce->constructor);
    }

    // Phase 1.5: Capture analysis
    // For each function, determine which variables it captures from outer scopes.
    // We build an outer_scope_names set from: top-level variable declarations,
    // function declaration names, and each function's parameters and locals.
    // Then we analyze each function expression/arrow for captures.
    {
        // Build set of all variable names visible at the top level and in enclosing functions
        struct hashmap* all_names = hashmap_new(sizeof(JsNameSetEntry), 64, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);

        // Add top-level variable declarations and function names from program body
        JsAstNode* s = program->body;
        while (s) {
            if (s->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
                JsFunctionNode* fnd = (JsFunctionNode*)s;
                if (fnd->name) {
                    char name[128];
                    snprintf(name, sizeof(name), "_js_%.*s", (int)fnd->name->len, fnd->name->chars);
                    jm_name_set_add(all_names, name);
                }
                // Add function's params and locals as visible names (for nested closures)
                JsAstNode* p = fnd->params;
                while (p) {
                    if (p->node_type == JS_AST_NODE_IDENTIFIER) {
                        JsIdentifierNode* pid = (JsIdentifierNode*)p;
                        char pname[128];
                        snprintf(pname, sizeof(pname), "_js_%.*s", (int)pid->name->len, pid->name->chars);
                        jm_name_set_add(all_names, pname);
                    }
                    p = p->next;
                }
                // Collect locals from function body
                struct hashmap* fn_locals = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
                    jm_name_hash, jm_name_cmp, NULL, NULL);
                if (fnd->body) jm_collect_body_locals(fnd->body, fn_locals);
                size_t iter2 = 0; void* item2;
                while (hashmap_iter(fn_locals, &iter2, &item2)) {
                    JsNameSetEntry* e = (JsNameSetEntry*)item2;
                    jm_name_set_add(all_names, e->name);
                }
                hashmap_free(fn_locals);
            }
            if (s->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                JsVariableDeclarationNode* v = (JsVariableDeclarationNode*)s;
                JsAstNode* d = v->declarations;
                while (d) {
                    if (d->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)d;
                        if (vd->id && vd->id->node_type == JS_AST_NODE_IDENTIFIER) {
                            JsIdentifierNode* vid = (JsIdentifierNode*)vd->id;
                            char vname[128];
                            snprintf(vname, sizeof(vname), "_js_%.*s", (int)vid->name->len, vid->name->chars);
                            jm_name_set_add(all_names, vname);
                        }
                    }
                    d = d->next;
                }
            }
            s = s->next;
        }

        // Analyze each collected function for captures
        for (int i = 0; i < mt->func_count; i++) {
            JsFuncCollected* fc = &mt->func_entries[i];
            // Analyze all functions (declarations, expressions, and arrows) for captures
            jm_analyze_captures(fc, all_names);
        }

        hashmap_free(all_names);
    }

    // Phase 1.75: Infer parameter and return types for each function
    for (int i = 0; i < mt->func_count; i++) {
        JsFuncCollected* fc = &mt->func_entries[i];
        jm_infer_param_types(fc);
        jm_infer_return_type(fc);
        // Check native eligibility (preview only — actual flag set in jm_define_function)
        bool eligible = (fc->capture_count == 0 && fc->param_count > 0 &&
                         fc->param_count <= 16 &&
                         (fc->return_type == LMD_TYPE_INT || fc->return_type == LMD_TYPE_FLOAT));
        if (eligible) {
            for (int j = 0; j < fc->param_count; j++) {
                if (fc->param_types[j] != LMD_TYPE_INT && fc->param_types[j] != LMD_TYPE_FLOAT) {
                    eligible = false;
                    break;
                }
            }
        }
        if (eligible) {
            log_debug("js-mir P4: %s eligible for native version (params: %d, ret: %s)",
                fc->name, fc->param_count,
                fc->return_type == LMD_TYPE_INT ? "INT" : "FLOAT");
        }

        // TCO eligibility: native-eligible function with at least one tail-recursive call
        fc->is_tco_eligible = false;
        if (eligible && jm_has_tail_call(fc->node->body, fc)) {
            fc->is_tco_eligible = true;
            log_debug("js-mir TCO: %s eligible for tail-call optimization", fc->name);
        }
    }

    // Phase 2: Define all collected functions (innermost first)
    for (int i = 0; i < mt->func_count; i++) {
        jm_define_function(mt, &mt->func_entries[i]);
    }

    // Phase 3: Create js_main(Context* ctx) -> Item
    MIR_var_t main_vars[] = {{MIR_T_P, "ctx", 0}};
    MIR_type_t main_ret = MIR_T_I64;
    MIR_item_t main_item = MIR_new_func_arr(mt->ctx, "js_main", 1, &main_ret, 1, main_vars);
    MIR_func_t main_func = MIR_get_item_func(mt->ctx, main_item);
    mt->current_func_item = main_item;
    mt->current_func = main_func;

    jm_push_scope(mt);

    // Initialize result register
    MIR_reg_t result = jm_new_reg(mt, "result", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));

    // Emit variable bindings for named function declarations (so they can be
    // used as first-class values, e.g., passed as callbacks).
    // Non-capturing function declarations are hoisted (bound before any statements).
    // Capturing function declarations are deferred to their source position
    // (bound inline with statements, after preceding const/let are in scope).
    JsAstNode* stmt = program->body;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (fn->name && fn->name->chars) {
                JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                if (fc && fc->func_item && fc->capture_count == 0) {
                    // Non-capturing: hoist normally
                    int pc = jm_count_params(fn);
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t fn_item = jm_call_2(mt, "js_new_function", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc));
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, fn_item)));
                    jm_set_var(mt, vname, var_reg);
                }
            }
        }
        stmt = stmt->next;
    }

    // Transpile top-level statements in source order.
    // Function declarations with captures are bound at their source position.
    stmt = program->body;
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_FUNCTION_DECLARATION) {
            JsFunctionNode* fn = (JsFunctionNode*)stmt;
            if (fn->name && fn->name->chars) {
                JsFuncCollected* fc = jm_find_collected_func(mt, fn);
                if (fc && fc->func_item && fc->capture_count > 0) {
                    // Capturing function declaration: bind as closure at this position
                    int pc = jm_count_params(fn);
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)fn->name->len, fn->name->chars);
                    MIR_reg_t var_reg = jm_new_reg(mt, vname, MIR_T_I64);
                    MIR_reg_t env = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
                        MIR_T_I64, MIR_new_int_op(mt->ctx, fc->capture_count));

                    // Track which env slot is the self-reference (for recursive fn decls)
                    int self_ref_slot = -1;

                    for (int ci = 0; ci < fc->capture_count; ci++) {
                        // Check if this capture is the function's own name (self-reference)
                        if (strcmp(fc->captures[ci].name, vname) == 0) {
                            self_ref_slot = ci;
                            // Will be filled after closure creation below
                            continue;
                        }
                        JsMirVarEntry* var = jm_find_var(mt, fc->captures[ci].name);
                        if (var) {
                            // Box native-typed variables before storing in env
                            MIR_reg_t value_to_store = var->reg;
                            if (jm_is_native_type(var->type_id)) {
                                value_to_store = jm_box_native(mt, var->reg, var->type_id);
                            }
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, ci * (int)sizeof(uint64_t), env, 0, 1),
                                MIR_new_reg_op(mt->ctx, value_to_store)));
                        } else {
                            log_error("js-mir: captured var '%s' not found for fn decl '%.*s'",
                                fc->captures[ci].name, (int)fn->name->len, fn->name->chars);
                        }
                    }
                    MIR_reg_t fn_item = jm_call_4(mt, "js_new_closure", MIR_T_I64,
                        MIR_T_I64, MIR_new_ref_op(mt->ctx, fc->func_item),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, pc),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, env),
                        MIR_T_I64, MIR_new_int_op(mt->ctx, fc->capture_count));
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                        MIR_new_reg_op(mt->ctx, var_reg),
                        MIR_new_reg_op(mt->ctx, fn_item)));
                    jm_set_var(mt, vname, var_reg);

                    // Patch self-reference: update env slot to point to the closure itself
                    if (self_ref_slot >= 0) {
                        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_mem_op(mt->ctx, MIR_T_I64, self_ref_slot * (int)sizeof(uint64_t), env, 0, 1),
                            MIR_new_reg_op(mt->ctx, var_reg)));
                    }
                }
            }
            // Non-capturing function declarations already handled above
            stmt = stmt->next;
            continue;
        }

        if (stmt->node_type == JS_AST_NODE_CLASS_DECLARATION) {
            // Already handled in class collection phase
            stmt = stmt->next;
            continue;
        }

        if (stmt->node_type == JS_AST_NODE_EXPRESSION_STATEMENT) {
            JsExpressionStatementNode* es = (JsExpressionStatementNode*)stmt;
            if (es->expression) {
                MIR_reg_t val = jm_transpile_box_item(mt, es->expression);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, result),
                    MIR_new_reg_op(mt->ctx, val)));
            }
        } else {
            jm_transpile_statement(mt, stmt);
        }

        stmt = stmt->next;
    }

    // Return result
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, result)));

    jm_pop_scope(mt);
    MIR_finish_func(mt->ctx);
    MIR_finish_module(mt->ctx);

    // Load module for linking
    MIR_load_module(mt->ctx, mt->module);
}

// ============================================================================
// Public entry point for JS transpilation via direct MIR generation
// ============================================================================

Item transpile_js_to_mir(Runtime* runtime, const char* js_source, const char* filename) {
    log_debug("js-mir: starting direct MIR transpilation for '%s'", filename ? filename : "<string>");

    // Create JS transpiler (for parsing and AST building)
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("js-mir: failed to create transpiler");
        return (Item){.item = ITEM_ERROR};
    }

    // Parse JavaScript source
    if (!js_transpiler_parse(tp, js_source, strlen(js_source))) {
        log_error("js-mir: parse failed");
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // Build JavaScript AST
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("js-mir: AST build failed");
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // Initialize MIR context
    MIR_context_t ctx = jit_init(2);
    if (!ctx) {
        log_error("js-mir: MIR context init failed");
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // Set up MIR transpiler
    JsMirTranspiler mt;
    memset(&mt, 0, sizeof(mt));
    mt.tp = tp;
    mt.ctx = ctx;
    mt.import_cache = hashmap_new(sizeof(JsImportCacheEntry), 64, 0, 0,
        js_import_cache_hash, js_import_cache_cmp, NULL, NULL);
    mt.local_funcs = hashmap_new(sizeof(JsLocalFuncEntry), 32, 0, 0,
        js_local_func_hash, js_local_func_cmp, NULL, NULL);
    mt.var_scopes[0] = hashmap_new(sizeof(JsVarScopeEntry), 16, 0, 0,
        js_var_scope_hash, js_var_scope_cmp, NULL, NULL);
    mt.scope_depth = 0;

    // Create module
    mt.module = MIR_new_module(ctx, "js_script");

    // Transpile AST to MIR
    transpile_js_mir_ast(&mt, js_ast);

    // Dump MIR for debugging
    create_dir_recursive("temp");
    FILE* mir_dump = fopen("temp/js_mir_dump.txt", "w");
    if (mir_dump) {
        MIR_output(ctx, mir_dump);
        fclose(mir_dump);
    }

    // Link and generate
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);

    // Find js_main
    typedef Item (*js_main_func_t)(Context*);
    // Use find_func which is declared in transpiler.hpp
    js_main_func_t js_main = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main) {
        log_error("js-mir: failed to find js_main");
        hashmap_free(mt.import_cache);
        hashmap_free(mt.local_funcs);
        for (int i = 0; i <= mt.scope_depth; i++) {
            if (mt.var_scopes[i]) hashmap_free(mt.var_scopes[i]);
        }
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    // Set up evaluation context (same logic as js_transpiler_compile in js_scope.cpp)
    EvalContext js_context;
    memset(&js_context, 0, sizeof(EvalContext));
    EvalContext* old_context = context;
    bool reusing_context = false;

    if (old_context && old_context->heap) {
        context = old_context;
        reusing_context = true;
        if (!context->nursery) {
            context->nursery = gc_nursery_create(0);
        }
    } else {
        js_context.nursery = gc_nursery_create(0);
        context = &js_context;
        heap_init();
        context->pool = context->heap->pool;
        context->name_pool = name_pool_create(context->pool, nullptr);
    }

    _lambda_rt = (Context*)context;

    // Create Input context for JS runtime map_put operations
    Input* js_input = Input::create(context->pool);
    js_runtime_set_input(js_input);

    // Set up DOM document context if available
    if (runtime->dom_doc) {
        js_dom_set_document(runtime->dom_doc);
    }

    // Execute
    log_notice("js-mir: executing JIT compiled code");
    Item result = js_main((Context*)context);

    // Handle result (same logic as js_transpiler_compile)
    Item final_result;
    TypeId type_id = get_type_id(result);

    if (reusing_context) {
        final_result = result;
    } else {
        if (type_id == LMD_TYPE_FLOAT) {
            double value = it2d(result);
            if (value == (double)(int64_t)value && value >= INT32_MIN && value <= INT32_MAX) {
                final_result = (Item){.item = i2it((int64_t)value)};
            } else {
                double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *ptr = value;
                final_result = (Item){.item = d2it(ptr)};
            }
        } else {
            final_result = result;
        }
    }

    // Convert JS HashMap objects to VMap for proper printing (before context restore)
    // (no longer needed — JS objects are now Lambda Maps)

    context = old_context;

    // Cleanup
    hashmap_free(mt.import_cache);
    hashmap_free(mt.local_funcs);
    for (int i = 0; i <= mt.scope_depth; i++) {
        if (mt.var_scopes[i]) hashmap_free(mt.var_scopes[i]);
    }
    MIR_finish(ctx);
    js_transpiler_destroy(tp);

    log_debug("js-mir: transpilation completed");
    return final_result;
}
