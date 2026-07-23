#include "js_mir_internal.hpp"
#include <limits.h>

static bool jm_function_inside_class_syntax(JsFunctionNode* fn) {
    if (!fn || ts_node_is_null(fn->node)) return false;
    TSNode node = ts_node_parent(fn->node);
    while (!ts_node_is_null(node)) {
        const char* node_type = ts_node_type(node);
        if (node_type &&
            (strncmp(node_type, "class", 5) == 0 ||
             strcmp(node_type, "class_body") == 0)) {
            return true;
        }
        node = ts_node_parent(node);
    }
    return false;
}

static bool jm_node_has_direct_eval_call(JsAstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
            if (id->name && id->name->len == 4 && strncmp(id->name->chars, "eval", 4) == 0) return true;
        }
        for (JsAstNode* arg = call->arguments; arg; arg = arg->next) {
            if (jm_node_has_direct_eval_call(arg)) return true;
        }
        return false;
    }
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION:
        return false;
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        for (JsAstNode* stmt = blk->statements; stmt; stmt = stmt->next) {
            if (jm_node_has_direct_eval_call(stmt)) return true;
        }
        return false;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT:
        return jm_node_has_direct_eval_call(((JsExpressionStatementNode*)node)->expression);
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* decl = (JsVariableDeclarationNode*)node;
        for (JsAstNode* d = decl->declarations; d; d = d->next) {
            if (jm_node_has_direct_eval_call(d)) return true;
        }
        return false;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR:
        return jm_node_has_direct_eval_call(((JsVariableDeclaratorNode*)node)->init);
    case JS_AST_NODE_RETURN_STATEMENT:
        return jm_node_has_direct_eval_call(((JsReturnNode*)node)->argument);
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        return jm_node_has_direct_eval_call(n->test) || jm_node_has_direct_eval_call(n->consequent) ||
            jm_node_has_direct_eval_call(n->alternate);
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* n = (JsBinaryNode*)node;
        return jm_node_has_direct_eval_call(n->left) || jm_node_has_direct_eval_call(n->right);
    }
    case JS_AST_NODE_UNARY_EXPRESSION:
        return jm_node_has_direct_eval_call(((JsUnaryNode*)node)->operand);
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* n = (JsAssignmentNode*)node;
        return jm_node_has_direct_eval_call(n->left) || jm_node_has_direct_eval_call(n->right);
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* n = (JsMemberNode*)node;
        return jm_node_has_direct_eval_call(n->object) || (n->computed && jm_node_has_direct_eval_call(n->property));
    }
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        for (JsAstNode* arg = call->arguments; arg; arg = arg->next) {
            if (jm_node_has_direct_eval_call(arg)) return true;
        }
        return false;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)node;
        for (JsAstNode* expr = seq->expressions; expr; expr = expr->next) {
            if (jm_node_has_direct_eval_call(expr)) return true;
        }
        return false;
    }
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT:
        return jm_node_has_direct_eval_call(((JsSpreadElementNode*)node)->argument);
    case JS_AST_NODE_YIELD_EXPRESSION:
        return jm_node_has_direct_eval_call(((JsYieldNode*)node)->argument);
    case JS_AST_NODE_AWAIT_EXPRESSION:
        return jm_node_has_direct_eval_call(((JsAwaitNode*)node)->argument);
    default:
        return false;
    }
}

// ============================================================================
// Phase 4: Native call resolution
// ============================================================================

// Phase 3.5: find the collected entry for a direct call without checking native eligibility.
// Used to propagate return types from any known function, even non-native ones.
JsFuncCollected* jm_find_collected_func_for_call(JsMirTranspiler* mt, JsCallNode* call) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    NameEntry* entry = js_scope_lookup(mt->tp, id->name);
    if (!entry) entry = id->entry;
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
    return jm_find_collected_func(mt, fn);
}

// Check if a call expression should use the native version of a function.
// Returns the JsFuncCollected* if native call is possible, NULL otherwise.
JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call) {
    // P7: obj.method(args) — typed class instance with known native method
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)call->callee;
        if (!mem->computed && mem->object && mem->property &&
            mem->object->node_type == JS_AST_NODE_IDENTIFIER &&
            mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj_id  = (JsIdentifierNode*)mem->object;
            JsIdentifierNode* prop_id = (JsIdentifierNode*)mem->property;
            char vname[128];
            snprintf(vname, sizeof(vname), "_js_%.*s", (int)obj_id->name->len, obj_id->name->chars);
            JsMirVarEntry* obj_var = jm_find_var(mt, vname);
            // P7: also check module_consts for top-level vars (is_modvar path has no local entry)
            JsClassEntry* p7_ce = obj_var ? obj_var->class_entry : NULL;
            if (!p7_ce && mt->module_consts) {
                JsModuleConstEntry p7_mclookup;
                memset(&p7_mclookup, 0, sizeof(p7_mclookup));
                snprintf(p7_mclookup.name, sizeof(p7_mclookup.name), "%s", vname);
                JsModuleConstEntry* p7_mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &p7_mclookup);
                if (p7_mc) p7_ce = p7_mc->class_entry;
            }
            if (p7_ce) {
                JsClassEntry* ce = p7_ce;
                for (int i = 0; i < ce->method_count; i++) {
                    JsClassMethodEntry* me = &ce->methods[i];
                    if (me->is_constructor || me->is_static) continue;
                    if (!me->fc || !me->fc->has_native_version || !me->fc->native_func_item) continue;
                    if (!me->name) continue;
                    if (me->name->len != (size_t)prop_id->name->len ||
                        strncmp(me->name->chars, prop_id->name->chars, me->name->len) != 0) continue;
                    // found matching method — validate arg types
                    JsAstNode* arg = call->arguments;
                    bool ok = true;
                    for (int p = 0; p < me->fc->param_count && ok; p++) {
                        TypeId expected = me->fc->param_types[p];
                        TypeId actual   = arg ? jm_get_effective_type(mt, arg) : LMD_TYPE_ANY;
                        if (expected == LMD_TYPE_INT) {
                            if (actual != LMD_TYPE_INT && actual != LMD_TYPE_BOOL) ok = false;
                        } else if (expected == LMD_TYPE_FLOAT) {
                            if (actual != LMD_TYPE_FLOAT && actual != LMD_TYPE_INT) ok = false;
                        } else {
                            ok = false; // ANY param — cannot optimize
                        }
                        if (arg) arg = arg->next;
                    }
                    if (ok) {
                        log_debug("P7: resolved native method %.*s.%.*s → %s",
                            (int)obj_id->name->len, obj_id->name->chars,
                            (int)prop_id->name->len, prop_id->name->chars,
                            me->fc->name);
                        return me->fc;
                    }
                }
            }
        }
        return NULL; // MEMBER_EXPRESSION but not P7-eligible
    }

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
    if (fn->is_async) return NULL;

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
bool jm_is_recursive_call(JsCallNode* call, JsFuncCollected* fc) {
    if (!call || !call->callee || !fc || !fc->node || !fc->node->name) return false;
    if (call->callee->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    if (!id->name) return false;
    // Compare callee name against the original function name (before mangling)
    String* fn_name = fc->node->name;
    return (id->name->len == fn_name->len &&
            memcmp(id->name->chars, fn_name->chars, fn_name->len) == 0);
}

bool jm_call_result_uses_native_register(JsMirTranspiler* mt, JsCallNode* call, JsFuncCollected* fc) {
    if (!mt || !call || !fc) return false;
    // non-tail self recursion is deliberately routed through js_call_function, so
    // the MIR result is a boxed Item even when the function has a native body.
    if (mt->current_fc && fc == mt->current_fc &&
        (!mt->tco_func || !mt->in_tail_position || !jm_is_recursive_call(call, mt->tco_func))) {
        return false;
    }
    return true;
}

// Walk JS AST to find if there's at least one tail-recursive call.
// A tail call is: return f(...) where f is the function itself.
bool jm_has_tail_call(JsAstNode* node, JsFuncCollected* fc) {
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

MIR_item_t jm_find_local_func(JsMirTranspiler* mt, const char* name) {
    JsLocalFuncEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    JsLocalFuncEntry* found = (JsLocalFuncEntry*)hashmap_get(mt->local_funcs, &key);
    return found ? found->func_item : NULL;
}

void jm_register_local_func(JsMirTranspiler* mt, const char* name, MIR_item_t func_item) {
    JsLocalFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.func_item = func_item;
    hashmap_set(mt->local_funcs, &entry);
}

// ============================================================================
// Function name generation
// ============================================================================

void jm_make_fn_name(char* buf, int bufsize, JsFunctionNode* fn, JsMirTranspiler* mt) {
    StrBuf* sb = strbuf_new_cap(64);
    strbuf_append_str(sb, "_js_");
    if (fn->name) {
        strbuf_append_str_n(sb, fn->name->chars, fn->name->len);
    } else {
        strbuf_append_str(sb, "anon");
        strbuf_append_int(sb, mt->em.label_counter++);
    }
    strbuf_append_char(sb, '_');
    strbuf_append_int(sb, ts_node_start_byte(fn->node));
    snprintf(buf, bufsize, "%s", sb->str);
    strbuf_free(sb);
}

int jm_count_params(JsFunctionNode* fn) {
    int count = 0;
    JsAstNode* p = fn->params;
    while (p) { count++; p = p->next; }
    return count;
}

// Compute ES spec .length: number of params before first default/rest/destructuring-with-default.
// Returns -1 if same as param_count (no defaults, no rest) meaning no correction needed.
int jm_formal_length(JsFunctionNode* fn) {
    int count = 0;
    bool needs_correction = false;
    JsAstNode* p = fn->params;
    while (p) {
        if (p->node_type == JS_AST_NODE_REST_ELEMENT || p->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            needs_correction = true;
            break; // rest param doesn't count
        }
        if (p->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
            needs_correction = true;
            break; // stop at first default
        }
        if (p->node_type == (int)TS_AST_NODE_PARAMETER) {
            TsParameterNode* tsp = (TsParameterNode*)p;
            if (tsp->default_value) {
                needs_correction = true;
                break;
            }
        }
        count++;
        p = p->next;
    }
    return needs_correction ? count : -1;
}

// Extract the MIR param name for a function parameter node.
// For IDENTIFIER: "_js_<name>"; for ASSIGNMENT_PATTERN: extract left identifier name;
// for REST_ELEMENT: extract argument identifier name; fallback: "_js_p<i>".
void jm_get_param_name(JsAstNode* param_node, int index, char* out, int out_size) {
    if (!param_node) {
        snprintf(out, out_size, "_js_p%d", index);
        return;
    }
    if (param_node->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* pid = (JsIdentifierNode*)param_node;
        snprintf(out, out_size, "_js_%.*s", (int)pid->name->len, pid->name->chars);
    } else if (param_node->node_type == (int)TS_AST_NODE_PARAMETER) {
        // TsParameterNode: delegate to the wrapped pattern
        TsParameterNode* tsp = (TsParameterNode*)param_node;
        jm_get_param_name(tsp->pattern, index, out, out_size);
    } else if (param_node->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)param_node;
        jm_get_param_name(ap->left, index, out, out_size);
    } else if (param_node->node_type == JS_AST_NODE_REST_ELEMENT ||
               param_node->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)param_node;
        jm_get_param_name(sp->argument, index, out, out_size);
    } else {
        snprintf(out, out_size, "_js_p%d", index);
    }
}

// ============================================================================
// Forward declarations
// ============================================================================

MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item);
void jm_transpile_statement(JsMirTranspiler* mt, JsAstNode* stmt);
void jm_resolve_module_path(const char* base_file, const char* specifier, int spec_len,
                                   char* out, int out_size);

// ============================================================================
// Function collection (pre-pass) - post-order to get innermost first
// ============================================================================

JsClassEntry* jm_find_class(JsMirTranspiler* mt, const char* name, int name_len);

void jm_collect_functions(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node || mt->collection_failed) return;

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
        // Record how many functions exist before recursion — those are NOT our children
        int children_start = mt->func_count;
        // recurse into parameters first — default values may contain class/function expressions
        // e.g. ([cls = class {}]) => {} or function f(x = function(){}) {}
        {
            JsAstNode* param = fn->params;
            while (param) { jm_collect_functions(mt, param); param = param->next; }
        }
        // recurse into body (post-order)
        if (fn->body) jm_collect_functions(mt, fn->body);
        int children_end = mt->func_count;
        if (mt->collection_count_only) {
            if (mt->func_count == INT_MAX) {
                log_error("js-mir: function count overflow");
                mt->collection_failed = true;
                break;
            }
            mt->func_count++;
            break;
        }
        // add this function
        if (mt->func_count < mt->func_capacity) {
            int my_index = mt->func_count;
            JsFuncCollected* e = &mt->func_entries[my_index];
            memset(e, 0, sizeof(JsFuncCollected));
            e->node = fn;
            jm_make_fn_name(e->name, sizeof(e->name), fn, mt);
            e->func_item = NULL; // set during creation
            e->parent_index = -1; // top-level until set by parent
            e->is_strict = jm_function_inside_class_syntax(fn);
            e->has_direct_eval = jm_node_has_direct_eval_call(fn->body);
            mt->func_count++;
            // Set parent_index for DIRECT children: functions collected during our body
            // recursion that don't already have a parent assigned by a deeper enclosing
            // function. Direct children still have parent_index == -1.
            for (int ci = children_start; ci < children_end; ci++) {
                if (mt->func_entries[ci].parent_index == -1) {
                    mt->func_entries[ci].parent_index = my_index;
                }
            }
            // A5: Scan for this.prop = expr patterns (constructor shape pre-alloc)
            if (fn->body) jm_scan_ctor_props(e, fn->body);
        } else {
            // The count/fill walker must agree before pointers into exact storage are published.
            log_error("js-mir: function count/fill mismatch at %d of %d",
                mt->func_count, mt->func_capacity);
            mt->collection_failed = true;
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
        // Destructuring patterns may contain default values with functions:
        // e.g. var { fn = function(){} } = obj;
        if (n->id) jm_collect_functions(mt, n->id);
        // For var X = class Y { ... } or var X = class { ... }
        if (!mt->collection_count_only &&
            n->init && n->init->node_type == JS_AST_NODE_CLASS_DECLARATION &&
            n->id && n->id->node_type == JS_AST_NODE_IDENTIFIER) {
            JsClassNode* cls = (JsClassNode*)n->init;
            JsIdentifierNode* var_id = (JsIdentifierNode*)n->id;
            if (!cls->name && var_id->name) {
                // Anonymous class: set its name from the variable
                // Find the just-collected anonymous class entry (name == NULL)
                for (int i = mt->class_count - 1; i >= 0; i--) {
                    if (mt->class_entries[i].node == cls && mt->class_entries[i].name == NULL) {
                        mt->class_entries[i].name = var_id->name;
                        log_debug("js-mir: anonymous class named as '%.*s'",
                            (int)var_id->name->len, var_id->name->chars);
                        break;
                    }
                }
            } else if (cls->name && var_id->name &&
                (cls->name->len != var_id->name->len ||
                 strncmp(cls->name->chars, var_id->name->chars, cls->name->len) != 0)) {
                // Names differ — find the just-collected class and set alias
                JsClassEntry* ce = jm_find_class(mt, cls->name->chars, (int)cls->name->len);
                if (ce) {
                    ce->alias_name = var_id->name;
                    log_debug("js-mir: class '%.*s' aliased as '%.*s'",
                        (int)cls->name->len, cls->name->chars,
                        (int)var_id->name->len, var_id->name->chars);
                }
            }
        }
        // For var X = Y where Y is a known class name — register X as alias
        // Handles esbuild pattern: var PostScriptStack = _PostScriptStack;
        if (!mt->collection_count_only &&
            n->init && n->init->node_type == JS_AST_NODE_IDENTIFIER &&
            n->id && n->id->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* var_id = (JsIdentifierNode*)n->id;
            JsIdentifierNode* init_id = (JsIdentifierNode*)n->init;
            if (var_id->name && init_id->name) {
                JsClassEntry* ce = jm_find_class(mt, init_id->name->chars, (int)init_id->name->len);
                if (ce && !ce->alias_name) {
                    ce->alias_name = var_id->name;
                    if (ce->name) {
                        log_debug("js-mir: class '%.*s' aliased via variable as '%.*s'",
                            (int)ce->name->len, ce->name->chars,
                            (int)var_id->name->len, var_id->name->chars);
                    }
                }
            }
        }
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
        if (!mt->collection_count_only &&
            n->right && n->right->node_type == JS_AST_NODE_CLASS_DECLARATION &&
            n->left && n->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsClassNode* cls = (JsClassNode*)n->right;
            JsIdentifierNode* lhs_id = (JsIdentifierNode*)n->left;
            if (lhs_id->name) {
                for (int i = mt->class_count - 1; i >= 0; i--) {
                    JsClassEntry* ce = &mt->class_entries[i];
                    if (ce->node != cls) continue;
                    if (!ce->alias_name) ce->alias_name = lhs_id->name;
                    break;
                }
            }
        }
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
        if (n->computed) jm_collect_functions(mt, n->key);
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
    case JS_AST_NODE_TAGGED_TEMPLATE: {
        JsTaggedTemplateNode* tt = (JsTaggedTemplateNode*)node;
        jm_collect_functions(mt, tt->tag);
        if (tt->quasi) { JsAstNode* e = tt->quasi->expressions; while (e) { jm_collect_functions(mt, e); e = e->next; } }
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
        jm_collect_functions(mt, n->param);
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
        if (cls->superclass) jm_collect_functions(mt, cls->superclass);
        if (cls->body && cls->body->node_type == JS_AST_NODE_BLOCK_STATEMENT &&
            mt->collection_count_only) {
            if (mt->class_count == INT_MAX) {
                log_error("js-mir: class count overflow");
                mt->collection_failed = true;
                break;
            }
            mt->class_count++;
            JsBlockNode* count_body = (JsBlockNode*)cls->body;
            for (JsAstNode* member = count_body->statements; member; member = member->next) {
                if (member->node_type == JS_AST_NODE_FIELD_DEFINITION) {
                    JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)member;
                    if (fd->computed && fd->key) jm_collect_functions(mt, fd->key);
                    if (fd->key && fd->value) jm_collect_functions(mt, fd->value);
                } else if (member->node_type == JS_AST_NODE_STATIC_BLOCK) {
                    JsStaticBlockNode* sb = (JsStaticBlockNode*)member;
                    if (sb->body) jm_collect_functions(mt, sb->body);
                } else if (member->node_type == JS_AST_NODE_METHOD_DEFINITION) {
                    JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)member;
                    if (md->computed && md->key) jm_collect_functions(mt, md->key);
                    if (md->body) {
                        JsFunctionNode* fn = (JsFunctionNode*)md;
                        for (JsAstNode* param = fn->params; param; param = param->next) {
                            jm_collect_functions(mt, param);
                        }
                        if (fn->body) jm_collect_functions(mt, fn->body);
                        if (mt->func_count == INT_MAX) {
                            log_error("js-mir: function count overflow");
                            mt->collection_failed = true;
                            break;
                        }
                        mt->func_count++;
                    }
                }
            }
            break;
        }
        if (cls->body && cls->body->node_type == JS_AST_NODE_BLOCK_STATEMENT &&
            mt->class_count < mt->class_capacity) {
            JsClassEntry* ce = &mt->class_entries[mt->class_count];
            mt->class_count++; // reserve slot before recursion into methods/fields
            memset(ce, 0, sizeof(JsClassEntry));
            ce->node = cls;
            ce->name = cls->name;
            ce->alias_name = NULL;
            ce->method_count = 0;
            ce->constructor = NULL;
            {
                const char* class_node_type = ts_node_type(cls->node);
                ce->is_declaration = class_node_type && strcmp(class_node_type, "class_declaration") == 0;
            }
            ce->inner_module_var_index = -1;
            int class_body_functions_start = mt->func_count;

            JsBlockNode* body = (JsBlockNode*)cls->body;
            for (JsAstNode* member = body->statements; member; member = member->next) {
                if (member->node_type == JS_AST_NODE_METHOD_DEFINITION &&
                    ((JsMethodDefinitionNode*)member)->body) {
                    ce->method_capacity++;
                } else if (member->node_type == JS_AST_NODE_FIELD_DEFINITION) {
                    JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)member;
                    if (fd->key && fd->is_static) ce->static_field_capacity++;
                    else if (fd->key) ce->instance_field_capacity++;
                } else if (member->node_type == JS_AST_NODE_STATIC_BLOCK &&
                    ((JsStaticBlockNode*)member)->body) {
                    ce->static_block_capacity++;
                }
            }
            // Class metadata retains AST pointers, so give both the same
            // compile/runtime lifetime instead of a separate native owner.
            ce->methods = (JsClassMethodEntry*)pool_calloc(
                mt->tp->ast_pool, (size_t)ce->method_capacity * sizeof(JsClassMethodEntry));
            ce->static_fields = (JsStaticFieldEntry*)pool_calloc(
                mt->tp->ast_pool, (size_t)ce->static_field_capacity * sizeof(JsStaticFieldEntry));
            ce->instance_fields = (JsInstanceFieldEntry*)pool_calloc(
                mt->tp->ast_pool, (size_t)ce->instance_field_capacity * sizeof(JsInstanceFieldEntry));
            ce->static_blocks = (JsAstNode**)pool_calloc(
                mt->tp->ast_pool, (size_t)ce->static_block_capacity * sizeof(JsAstNode*));
            if ((ce->method_capacity && !ce->methods) ||
                (ce->static_field_capacity && !ce->static_fields) ||
                (ce->instance_field_capacity && !ce->instance_fields) ||
                (ce->static_block_capacity && !ce->static_blocks)) {
                // Exact member storage is required because later phases retain pointers into it.
                log_error("js-mir: failed to allocate class member metadata");
                mt->collection_failed = true;
                break;
            }
            JsAstNode* m = body->statements;
            ce->static_field_count = 0;
            ce->instance_field_count = 0;
            ce->static_block_count = 0;
            while (m) {
                if (m->node_type == JS_AST_NODE_FIELD_DEFINITION) {
                    JsFieldDefinitionNode* fd = (JsFieldDefinitionNode*)m;
                    if (fd->computed && fd->key) jm_collect_functions(mt, fd->key);
                    if (fd->is_static && fd->key &&
                        ce->static_field_count < ce->static_field_capacity) {
                        JsStaticFieldEntry* sf = &ce->static_fields[ce->static_field_count];
                        sf->computed = fd->computed;
                        sf->key_expr = fd->key;
                        if (!fd->computed && fd->key->node_type == JS_AST_NODE_IDENTIFIER) {
                            sf->name = jm_class_private_name(mt, ce, ((JsIdentifierNode*)fd->key)->name);
                        } else {
                            sf->name = NULL;
                        }
                        sf->initializer = fd->value;
                        sf->module_var_index = -1; // assigned later in Phase 1.1 (only for non-computed)
                        sf->key_module_var_index = -1;
                        // if the initializer contains functions, collect them
                        if (fd->value) jm_collect_functions(mt, fd->value);
                        ce->static_field_count++;
                        log_debug("js-mir: class '%.*s' static field %s'%.*s'",
                            cls->name ? (int)cls->name->len : 5, cls->name ? cls->name->chars : "anon?",
                            fd->computed ? "[computed] " : "",
                            sf->name ? (int)sf->name->len : 0, sf->name ? sf->name->chars : "");
                    } else if (!fd->is_static && fd->key &&
                        ce->instance_field_count < ce->instance_field_capacity) {
                        // Instance field (public or private — private already renamed to __private_)
                        JsInstanceFieldEntry* inf = &ce->instance_fields[ce->instance_field_count];
                        inf->computed = fd->computed;
                        inf->key_expr = fd->key;
                        if (!fd->computed && fd->key->node_type == JS_AST_NODE_IDENTIFIER) {
                            inf->name = jm_class_private_name(mt, ce, ((JsIdentifierNode*)fd->key)->name);
                        } else {
                            inf->name = NULL;
                        }
                        inf->initializer = fd->value;
                        inf->key_module_var_index = -1;
                        if (fd->value) jm_collect_functions(mt, fd->value);
                        ce->instance_field_count++;
                        log_debug("js-mir: class '%.*s' instance field %s'%.*s'",
                            cls->name ? (int)cls->name->len : 5, cls->name ? cls->name->chars : "anon?",
                            fd->computed ? "[computed] " : "",
                            inf->name ? (int)inf->name->len : 0, inf->name ? inf->name->chars : "");
                    }
                } else if (m->node_type == JS_AST_NODE_STATIC_BLOCK) {
                    // class static block: static { ... }
                    JsStaticBlockNode* sb = (JsStaticBlockNode*)m;
                    if (sb->body && ce->static_block_count < ce->static_block_capacity) {
                        ce->static_blocks[ce->static_block_count++] = sb->body;
                        jm_collect_functions(mt, sb->body);
                        log_debug("js-mir: class '%.*s' static block #%d",
                            cls->name ? (int)cls->name->len : 5, cls->name ? cls->name->chars : "anon?",
                            ce->static_block_count);
                    }
                } else if (m->node_type == JS_AST_NODE_METHOD_DEFINITION) {
                    JsMethodDefinitionNode* md = (JsMethodDefinitionNode*)m;
                    if (md->computed && md->key) jm_collect_functions(mt, md->key);
                    if (md->body) {
                        JsFunctionNode* fn = (JsFunctionNode*)md;
                        // Track inner functions before recursion
                        int method_children_start = mt->func_count;
                        // Recurse into parameters first — default values may contain function expressions
                        // e.g. method(fn = () => {}) {} or method([cls = class {}]) {}
                        {
                            JsAstNode* param = fn->params;
                            while (param) { jm_collect_functions(mt, param); param = param->next; }
                        }
                        // Recurse into method body
                        if (fn->body) jm_collect_functions(mt, fn->body);
                        int method_children_end = mt->func_count;
                        // Add as collected function
                        if (mt->func_count < mt->func_capacity) {
                            int method_index = mt->func_count;
                            JsFuncCollected* fc = &mt->func_entries[mt->func_count];
                            memset(fc, 0, sizeof(JsFuncCollected));
                            fc->node = fn;
                            fc->parent_index = -1; // class methods are at top level
                            // Name: ClassName_methodName
                            String* method_name = NULL;
                            if (md->key && md->key->node_type == JS_AST_NODE_IDENTIFIER) {
                                method_name = jm_class_private_name(mt, ce, ((JsIdentifierNode*)md->key)->name);
                            } else if (md->key && md->key->node_type == JS_AST_NODE_LITERAL) {
                                JsLiteralNode* lit = (JsLiteralNode*)md->key;
                                if (lit->literal_type == JS_LITERAL_STRING) {
                                    method_name = lit->value.string_value;
                                } else if (lit->literal_type == JS_LITERAL_NUMBER) {
                                    char nbuf[64];
                                    js_double_to_string(lit->value.number_value, nbuf, sizeof(nbuf));
                                    int nlen = (int)strlen(nbuf);
                                    String* ns = name_pool_create_len(mt->tp->name_pool, nbuf, nlen);
                                    method_name = ns;
                                }
                            } else if (md->key && md->key->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
                                // computed key like [Symbol.iterator] — use the property name
                                JsMemberNode* mem = (JsMemberNode*)md->key;
                                if (mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
                                    method_name = ((JsIdentifierNode*)mem->property)->name;
                                }
                            }
                            if (method_name && cls->name) {
                                const char* gs_prefix = "";
                                if (md->kind == JsMethodDefinitionNode::JS_METHOD_GET) gs_prefix = "get_";
                                else if (md->kind == JsMethodDefinitionNode::JS_METHOD_SET) gs_prefix = "set_";
                                else if (md->static_method) gs_prefix = "s_";
                                snprintf(fc->name, sizeof(fc->name), "%.*s_%s%.*s_%d",
                                    (int)cls->name->len, cls->name->chars,
                                    gs_prefix,
                                    (int)method_name->len, method_name->chars,
                                    mt->func_count);
                            } else if (method_name) {
                                const char* gs_prefix = "";
                                if (md->kind == JsMethodDefinitionNode::JS_METHOD_GET) gs_prefix = "get_";
                                else if (md->kind == JsMethodDefinitionNode::JS_METHOD_SET) gs_prefix = "set_";
                                else if (md->static_method) gs_prefix = "s_";
                                // Anonymous class: use func_count to disambiguate
                                snprintf(fc->name, sizeof(fc->name), "anon%d_%s%.*s",
                                    mt->func_count, gs_prefix,
                                    (int)method_name->len, method_name->chars);
                            } else {
                                // Use func_count as a unique ID for unnamed computed methods.
                                snprintf(fc->name, sizeof(fc->name), "class_method_%d_%d",
                                    mt->class_count, mt->func_count);
                            }
                            fc->func_item = NULL;
                            fc->capture_count = 0;
                            fc->is_strict = true;
                            fc->has_direct_eval = jm_node_has_direct_eval_call(fn->body);
                            mt->func_count++;

                            // Set parent_index for inner functions collected during method body
                            // This ensures capture propagation correctly identifies the method
                            // as the parent, not a grandparent IIFE/function.
                            for (int ci = method_children_start; ci < method_children_end; ci++) {
                                if (mt->func_entries[ci].parent_index == -1) {
                                    mt->func_entries[ci].parent_index = method_index;
                                }
                            }

                            // Add to class entry
                            if (ce->method_count < ce->method_capacity) {
                                JsClassMethodEntry* me = &ce->methods[ce->method_count];
                                me->name = method_name;
                                me->fc = fc;
                                me->param_count = jm_count_params(fn);
                                // negate param_count if last param is ...rest (signals rest to js_invoke_fn)
                                {
                                    JsAstNode* last_p = NULL;
                                    JsAstNode* pp = fn->params;
                                    while (pp) { last_p = pp; pp = pp->next; }
                                    if (last_p && (last_p->node_type == JS_AST_NODE_REST_ELEMENT ||
                                                   last_p->node_type == JS_AST_NODE_SPREAD_ELEMENT)) {
                                        me->param_count = -me->param_count;
                                    }
                                }
                                me->is_static = md->static_method;
                                me->is_getter = (md->kind == JsMethodDefinitionNode::JS_METHOD_GET);
                                me->is_setter = (md->kind == JsMethodDefinitionNode::JS_METHOD_SET);
                                me->computed = md->computed;
                                me->key_expr = md->key;
                                // Detect constructor by name
                                me->is_constructor = (!me->is_static && !me->computed && method_name &&
                                    method_name->len == 11 &&
                                    strncmp(method_name->chars, "constructor", 11) == 0);
                                if (me->is_constructor) {
                                    ce->constructor = me;
                                    fc->is_constructor = true;  // P3: mark fc for direct slot stores
                                    fc->is_derived_constructor = (cls->superclass != NULL);
                                    // A5: Scan constructor for this.prop = expr
                                    if (fn->body) jm_scan_ctor_props(fc, fn->body);
                                }
                                fc->is_class_static_method = me->is_static;
                                ce->method_count++;
                            }
                        } else {
                            // A mismatch would otherwise publish incomplete class/function metadata.
                            log_error("js-mir: class method count/fill mismatch at %d of %d",
                                mt->func_count, mt->func_capacity);
                            mt->collection_failed = true;
                        }
                    }
                }
                m = m->next;
            }

            // The private name of a named class is visible throughout field,
            // static-block, method, and nested-function bodies. Preserve an
            // already assigned inner class so nested classes keep precedence.
            for (int fi = class_body_functions_start; fi < mt->func_count; fi++) {
                if (!mt->func_entries[fi].owner_class) {
                    mt->func_entries[fi].owner_class = ce;
                }
            }

            jm_disable_ctor_shape_for_method_overwrite(ce);

            // When instance fields are present, disable A5/P3/P4 shaped slot optimization.
            // Field inits run before the constructor body via js_property_set, which manages
            // the shape dynamically. P3 slot indices would conflict with the dynamic shape.
            if (ce->constructor && ce->constructor->fc && ce->instance_field_count > 0) {
                ce->constructor->fc->ctor_prop_count = 0;
            }

            // §7: Allocate per-class shape cache slot for inline shape guard
            if (ce->constructor && ce->constructor->fc &&
                ce->constructor->fc->ctor_prop_count > 0) {
                ce->shape_cache_ptr = jm_alloc_shape_cache_slot(mt);
            }
        } else if (cls->body && cls->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            // A mismatch would otherwise make owner_class and superclass pointers incomplete.
            log_error("js-mir: class count/fill mismatch at %d of %d",
                mt->class_count, mt->class_capacity);
            mt->collection_failed = true;
        }
        break;
    }
    case JS_AST_NODE_EXPORT_DECLARATION: {
        // v14: recurse into exported declaration to collect functions
        JsExportNode* exp = (JsExportNode*)node;
        if (exp->declaration) jm_collect_functions(mt, exp->declaration);
        break;
    }
    case JS_AST_NODE_IMPORT_DECLARATION:
        // v14: imports don't contain function declarations to collect
        break;
    case JS_AST_NODE_YIELD_EXPRESSION: {
        // v14: recurse into yield argument
        JsYieldNode* y = (JsYieldNode*)node;
        if (y->argument) jm_collect_functions(mt, y->argument);
        break;
    }
    case JS_AST_NODE_AWAIT_EXPRESSION: {
        // v14: recurse into await argument
        JsAwaitNode* a = (JsAwaitNode*)node;
        if (a->argument) jm_collect_functions(mt, a->argument);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        // default param: (x = expr) — recurse into default value in case it's a function
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)node;
        if (ap->left) jm_collect_functions(mt, ap->left);
        if (ap->right) jm_collect_functions(mt, ap->right);
        break;
    }
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT: {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)node;
        if (sp->argument) jm_collect_functions(mt, sp->argument);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        // v24: comma operator — recurse into all sub-expressions
        JsSequenceNode* seq = (JsSequenceNode*)node;
        JsAstNode* e = seq->expressions;
        while (e) { jm_collect_functions(mt, e); e = e->next; }
        break;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        // v24: labeled statement — recurse into body
        JsLabeledStatementNode* ls = (JsLabeledStatementNode*)node;
        if (ls->body) jm_collect_functions(mt, ls->body);
        break;
    }
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* ws = (JsWithStatementNode*)node;
        if (ws->object) jm_collect_functions(mt, ws->object);
        if (ws->body) jm_collect_functions(mt, ws->body);
        break;
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        // v24: destructuring array pattern — elements may contain default values with functions
        JsArrayPatternNode* ap = (JsArrayPatternNode*)node;
        JsAstNode* e = ap->elements;
        while (e) { jm_collect_functions(mt, e); e = e->next; }
        break;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        // v24: destructuring object pattern — properties may contain default values with functions
        JsObjectPatternNode* op = (JsObjectPatternNode*)node;
        JsAstNode* p = op->properties;
        while (p) { jm_collect_functions(mt, p); p = p->next; }
        break;
    }
    default:
        break; // leaf nodes, identifiers, literals
    }
}

// ============================================================================
// Find collected function entry by node pointer
// ============================================================================

JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn) {
    for (int i = 0; i < mt->func_count; i++) {
        if (mt->func_entries[i].node == fn) return &mt->func_entries[i];
    }
    return NULL;
}

// Annex B §B.3.3.1: Check if enclosing function has a parameter whose name
// matches the given identifier.  When it does, the block-scoped function
// declaration must NOT overwrite the parameter binding.
bool jm_func_has_param_named(JsFunctionNode* fn, const char* name, int name_len) {
    if (!fn || !fn->params) return false;
    JsAstNode* p = fn->params;
    while (p) {
        JsIdentifierNode* pid = NULL;
        if (p->node_type == JS_AST_NODE_IDENTIFIER) {
            pid = (JsIdentifierNode*)p;
        } else if (p->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
            // default parameter: (x = defaultVal) — check left side
            JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)p;
            if (ap->left && ap->left->node_type == JS_AST_NODE_IDENTIFIER)
                pid = (JsIdentifierNode*)ap->left;
        } else if (p->node_type == JS_AST_NODE_REST_ELEMENT) {
            JsSpreadElementNode* rest = (JsSpreadElementNode*)p;
            if (rest->argument && rest->argument->node_type == JS_AST_NODE_IDENTIFIER)
                pid = (JsIdentifierNode*)rest->argument;
        }
        if (pid && pid->name &&
            (int)pid->name->len == name_len &&
            memcmp(pid->name->chars, name, name_len) == 0) {
            return true;
        }
        p = p->next;
    }
    return false;
}

// P3: Find property slot index in constructor's ctor_prop_ptrs[]. Returns -1 if not found.
int jm_ctor_prop_slot(JsFuncCollected* fc, const char* prop_name, int prop_len) {
    for (int i = 0; i < fc->ctor_prop_count; i++) {
        if (fc->ctor_prop_lens[i] == prop_len &&
            strncmp(fc->ctor_prop_ptrs[i], prop_name, prop_len) == 0)
            return i;
    }
    return -1;
}

// ============================================================================
// P4b: Infer class type for variables assigned from subscript access (arr[i]).
// Walks the function body AST to collect field names accessed on the variable,
// then finds the unique class whose constructor has ALL those fields.
// ============================================================================

// Recursive AST walker: collect unique field names from `varname.field` accesses.
void jm_collect_var_fields_walk(JsAstNode* node, const char* varname, int varlen,
                                       char fields[][64], int* count, int max_fields) {
    if (!node || *count >= max_fields) return;

    // check for varname.field pattern
    if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)node;
        if (!mem->computed && mem->object && mem->property &&
            mem->object->node_type == JS_AST_NODE_IDENTIFIER &&
            mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj = (JsIdentifierNode*)mem->object;
            JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
            if (obj->name && (int)obj->name->len == varlen &&
                strncmp(obj->name->chars, varname, varlen) == 0 && prop->name) {
                bool dup = false;
                for (int i = 0; i < *count; i++) {
                    if ((int)strlen(fields[i]) == (int)prop->name->len &&
                        strncmp(fields[i], prop->name->chars, prop->name->len) == 0) {
                        dup = true; break;
                    }
                }
                if (!dup && *count < max_fields) {
                    int len = (int)prop->name->len < 63 ? (int)prop->name->len : 63;
                    memcpy(fields[*count], prop->name->chars, len);
                    fields[*count][len] = 0;
                    (*count)++;
                }
            }
        }
        // recurse into object (for chained access like iBody.mass.toString())
        jm_collect_var_fields_walk(mem->object, varname, varlen, fields, count, max_fields);
        return;
    }

    // recurse into children based on node type
    switch (node->node_type) {
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        for (JsAstNode* s = blk->statements; s; s = s->next)
            jm_collect_var_fields_walk(s, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_collect_var_fields_walk(n->init, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->test, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->update, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->body, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* n = (JsExpressionStatementNode*)node;
        jm_collect_var_fields_walk(n->expression, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* n = (JsAssignmentNode*)node;
        jm_collect_var_fields_walk(n->left, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->right, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* n = (JsBinaryNode*)node;
        jm_collect_var_fields_walk(n->left, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->right, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* n = (JsCallNode*)node;
        jm_collect_var_fields_walk(n->callee, varname, varlen, fields, count, max_fields);
        for (JsAstNode* a = n->arguments; a; a = a->next)
            jm_collect_var_fields_walk(a, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* n = (JsVariableDeclarationNode*)node;
        for (JsAstNode* d = n->declarations; d; d = d->next)
            jm_collect_var_fields_walk(d, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* n = (JsVariableDeclaratorNode*)node;
        jm_collect_var_fields_walk(n->init, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* n = (JsReturnNode*)node;
        jm_collect_var_fields_walk(n->argument, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_collect_var_fields_walk(n->test, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->consequent, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->alternate, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_collect_var_fields_walk(n->test, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->body, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_collect_var_fields_walk(n->body, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->test, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* n = (JsConditionalNode*)node;
        jm_collect_var_fields_walk(n->test, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->consequent, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->alternate, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* n = (JsUnaryNode*)node;
        jm_collect_var_fields_walk(n->operand, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* n = (JsSequenceNode*)node;
        for (JsAstNode* e = n->expressions; e; e = e->next)
            jm_collect_var_fields_walk(e, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_SWITCH_STATEMENT: {
        JsSwitchNode* n = (JsSwitchNode*)node;
        jm_collect_var_fields_walk(n->discriminant, varname, varlen, fields, count, max_fields);
        for (JsAstNode* c = n->cases; c; c = c->next) {
            JsSwitchCaseNode* sc = (JsSwitchCaseNode*)c;
            jm_collect_var_fields_walk(sc->test, varname, varlen, fields, count, max_fields);
            for (JsAstNode* s = sc->consequent; s; s = s->next)
                jm_collect_var_fields_walk(s, varname, varlen, fields, count, max_fields);
        }
        break;
    }
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* n = (JsForOfNode*)node;
        jm_collect_var_fields_walk(n->left, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->right, varname, varlen, fields, count, max_fields);
        jm_collect_var_fields_walk(n->body, varname, varlen, fields, count, max_fields);
        break;
    }
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* n = (JsTryNode*)node;
        jm_collect_var_fields_walk(n->block, varname, varlen, fields, count, max_fields);
        if (n->handler) {
            JsCatchNode* ch = (JsCatchNode*)n->handler;
            jm_collect_var_fields_walk(ch->body, varname, varlen, fields, count, max_fields);
        }
        jm_collect_var_fields_walk(n->finalizer, varname, varlen, fields, count, max_fields);
        break;
    }
    // skip function/class bodies (different scope)
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_CLASS_DECLARATION:
        break;
    default:
        break;
    }
}

// Match collected field names against all known classes.
// Returns the unique class whose constructor has ALL field names, or NULL.
JsClassEntry* jm_match_class_from_fields(JsMirTranspiler* mt,
                                                  char fields[][64], int field_count) {
    if (field_count < 2) return NULL; // require at least 2 fields for reliable inference
    JsClassEntry* match = NULL;
    int match_count = 0;
    for (int c = 0; c < mt->class_count; c++) {
        JsClassEntry* ce = &mt->class_entries[c];
        if (!ce->constructor || !ce->constructor->fc) continue;
        JsFuncCollected* fc = ce->constructor->fc;
        if (fc->ctor_prop_count < field_count) continue; // class has fewer props than accessed fields
        bool all_match = true;
        for (int f = 0; f < field_count; f++) {
            if (jm_ctor_prop_slot(fc, fields[f], (int)strlen(fields[f])) < 0) {
                all_match = false;
                break;
            }
        }
        if (all_match) {
            match = ce;
            match_count++;
            if (match_count > 1) return NULL; // ambiguous — multiple classes match
        }
    }
    return (match_count == 1) ? match : NULL;
}

// P4h: Scan loop body AST for subscript array accesses (arr[idx]).
// Collects unique identifier names used as array objects in computed member expressions.
// Also tracks whether any collected name appears as an assignment target (unsafe for hoisting).
// mark identifier targets of a destructuring assignment LHS ([a,b]=…, {x}=…) as
// unsafe for the P4h typed-array pointer hoist: such a target is rebound each time,
// so a hoisted data pointer for it would go stale (e.g. kostya/levenshtein's
// [prev,curr]=[curr,prev] swap). find-or-add so the result is order-independent.
static void jm_mark_destructure_targets_unsafe(JsAstNode* node, char names[][64],
                                               bool unsafe[], int* count, int max_names) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        if (!id->name) return;
        for (int i = 0; i < *count; i++) {
            if ((int)strlen(names[i]) == (int)id->name->len &&
                strncmp(names[i], id->name->chars, id->name->len) == 0) {
                unsafe[i] = true;
                return;
            }
        }
        // not yet tracked: record it as unsafe so a later subscript use stays unsafe
        if (*count < max_names) {
            int len = (int)id->name->len < 63 ? (int)id->name->len : 63;
            memcpy(names[*count], id->name->chars, len);
            names[*count][len] = 0;
            unsafe[*count] = true;
            (*count)++;
        }
        return;
    }
    case JS_AST_NODE_ARRAY_PATTERN:
        for (JsAstNode* e = ((JsArrayPatternNode*)node)->elements; e; e = e->next)
            jm_mark_destructure_targets_unsafe(e, names, unsafe, count, max_names);
        return;
    case JS_AST_NODE_ARRAY_EXPRESSION:
        for (JsAstNode* e = ((JsArrayNode*)node)->elements; e; e = e->next)
            jm_mark_destructure_targets_unsafe(e, names, unsafe, count, max_names);
        return;
    case JS_AST_NODE_OBJECT_PATTERN:
        for (JsAstNode* p = ((JsObjectPatternNode*)node)->properties; p; p = p->next)
            jm_mark_destructure_targets_unsafe(p, names, unsafe, count, max_names);
        return;
    case JS_AST_NODE_OBJECT_EXPRESSION:
        for (JsAstNode* p = ((JsObjectNode*)node)->properties; p; p = p->next)
            jm_mark_destructure_targets_unsafe(p, names, unsafe, count, max_names);
        return;
    case JS_AST_NODE_PROPERTY:
        // the value side is the binding target ({ key: target })
        jm_mark_destructure_targets_unsafe(((JsPropertyNode*)node)->value, names, unsafe, count, max_names);
        return;
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        // default: target = default; only the left is a binding target
        jm_mark_destructure_targets_unsafe(((JsAssignmentPatternNode*)node)->left, names, unsafe, count, max_names);
        return;
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
        jm_mark_destructure_targets_unsafe(((JsSpreadElementNode*)node)->argument, names, unsafe, count, max_names);
        return;
    default:
        // member-expression target (a[i]=) does not rebind the variable; ignore
        return;
    }
}

void jm_scan_subscript_arrays(JsAstNode* node, char names[][64], bool unsafe[],
                                      int* count, int max_names) {
    if (!node || *count >= max_names) return;

    if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)node;
        if (mem->computed && mem->object &&
            mem->object->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)mem->object;
            if (id->name && id->name->len > 0) {
                // add unique
                bool found = false;
                for (int i = 0; i < *count; i++) {
                    if ((int)strlen(names[i]) == (int)id->name->len &&
                        strncmp(names[i], id->name->chars, id->name->len) == 0) {
                        found = true; break;
                    }
                }
                if (!found && *count < max_names) {
                    int len = (int)id->name->len < 63 ? (int)id->name->len : 63;
                    memcpy(names[*count], id->name->chars, len);
                    names[*count][len] = 0;
                    // unsafe[max_names] records a call seen earlier in this scan;
                    // later-discovered arrays must inherit the same invalidation.
                    unsafe[*count] = unsafe[max_names];
                    (*count)++;
                }
            }
        }
        // recurse into both sides
        jm_scan_subscript_arrays(mem->object, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(mem->property, names, unsafe, count, max_names);
        return;
    }

    // check for assignment to an identifier (marks it unsafe for hoisting)
    if (node->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        if (asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)asgn->left;
            if (id->name) {
                for (int i = 0; i < *count; i++) {
                    if ((int)strlen(names[i]) == (int)id->name->len &&
                        strncmp(names[i], id->name->chars, id->name->len) == 0) {
                        unsafe[i] = true;
                    }
                }
            }
        } else if (asgn->left) {
            // destructuring/pattern LHS ([a,b]=…, {x}=…): every bound identifier is rebound
            jm_mark_destructure_targets_unsafe(asgn->left, names, unsafe, count, max_names);
        }
        jm_scan_subscript_arrays(asgn->left, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(asgn->right, names, unsafe, count, max_names);
        return;
    }

    // recurse into children
    switch (node->node_type) {
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        for (JsAstNode* s = blk->statements; s; s = s->next)
            jm_scan_subscript_arrays(s, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_scan_subscript_arrays(n->init, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->test, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->update, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->body, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* n = (JsExpressionStatementNode*)node;
        jm_scan_subscript_arrays(n->expression, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* n = (JsBinaryNode*)node;
        jm_scan_subscript_arrays(n->left, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->right, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION: {
        JsCallNode* n = (JsCallNode*)node;
        // an arbitrary call can resize, transfer, or detach a backing buffer,
        // invalidating every loop-hoisted typed-array pointer and length
        unsafe[max_names] = true;
        for (int i = 0; i < *count; i++) unsafe[i] = true;
        jm_scan_subscript_arrays(n->callee, names, unsafe, count, max_names);
        for (JsAstNode* a = n->arguments; a; a = a->next)
            jm_scan_subscript_arrays(a, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* n = (JsVariableDeclarationNode*)node;
        for (JsAstNode* d = n->declarations; d; d = d->next)
            jm_scan_subscript_arrays(d, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* n = (JsVariableDeclaratorNode*)node;
        jm_scan_subscript_arrays(n->init, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* n = (JsReturnNode*)node;
        jm_scan_subscript_arrays(n->argument, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_scan_subscript_arrays(n->test, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->consequent, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->alternate, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_scan_subscript_arrays(n->test, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->body, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_DO_WHILE_STATEMENT: {
        JsDoWhileNode* n = (JsDoWhileNode*)node;
        jm_scan_subscript_arrays(n->body, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->test, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* n = (JsConditionalNode*)node;
        jm_scan_subscript_arrays(n->test, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->consequent, names, unsafe, count, max_names);
        jm_scan_subscript_arrays(n->alternate, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* n = (JsUnaryNode*)node;
        jm_scan_subscript_arrays(n->operand, names, unsafe, count, max_names);
        break;
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* n = (JsSequenceNode*)node;
        for (JsAstNode* e = n->expressions; e; e = e->next)
            jm_scan_subscript_arrays(e, names, unsafe, count, max_names);
        break;
    }
    // skip function/class bodies (different scope)
    case JS_AST_NODE_FUNCTION_DECLARATION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_CLASS_DECLARATION:
        break;
    default:
        break;
    }
}

// detect typed array constructor type from a new expression
// also detects chained method calls like new Uint8Array(n).map(fn)
int jm_detect_typed_array_new(JsAstNode* rhs) {
    if (!rhs) return -1;
    // direct: new TypedArray(...)
    if (rhs->node_type == JS_AST_NODE_NEW_EXPRESSION) {
        JsCallNode* new_call = (JsCallNode*)rhs;
        if (!new_call->callee || new_call->callee->node_type != JS_AST_NODE_IDENTIFIER) return -1;
        JsIdentifierNode* ctor = (JsIdentifierNode*)new_call->callee;
        if (ctor->name->len == 10 && strncmp(ctor->name->chars, "Uint8Array", 10) == 0) return JS_TYPED_UINT8;
        if (ctor->name->len == 10 && strncmp(ctor->name->chars, "Int32Array", 10) == 0) return JS_TYPED_INT32;
        if (ctor->name->len == 10 && strncmp(ctor->name->chars, "Int16Array", 10) == 0) return JS_TYPED_INT16;
        if (ctor->name->len == 9 && strncmp(ctor->name->chars, "Int8Array", 9) == 0) return JS_TYPED_INT8;
        if (ctor->name->len == 11 && strncmp(ctor->name->chars, "Uint32Array", 11) == 0) return JS_TYPED_UINT32;
        if (ctor->name->len == 11 && strncmp(ctor->name->chars, "Uint16Array", 11) == 0) return JS_TYPED_UINT16;
        if (ctor->name->len == 17 && strncmp(ctor->name->chars, "Uint8ClampedArray", 17) == 0) return JS_TYPED_UINT8_CLAMPED;
        if (ctor->name->len == 12 && strncmp(ctor->name->chars, "Float16Array", 12) == 0) return JS_TYPED_FLOAT16;
        if (ctor->name->len == 12 && strncmp(ctor->name->chars, "Float64Array", 12) == 0) return JS_TYPED_FLOAT64;
        if (ctor->name->len == 12 && strncmp(ctor->name->chars, "Float32Array", 12) == 0) return JS_TYPED_FLOAT32;
        return -1;
    }
    // chained: new TypedArray(n).map(fn) / .filter(fn) / .slice(...) etc.
    // TypedArray methods that preserve type: map, filter, slice, sort, reverse, subarray
    if (rhs->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)rhs;
        if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
            JsMemberNode* cm = (JsMemberNode*)call->callee;
            if (!cm->computed && cm->property &&
                cm->property->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* method = (JsIdentifierNode*)cm->property;
                bool preserves_type = false;
                if (method->name->len == 3 && strncmp(method->name->chars, "map", 3) == 0) preserves_type = true;
                else if (method->name->len == 6 && strncmp(method->name->chars, "filter", 6) == 0) preserves_type = true;
                else if (method->name->len == 5 && strncmp(method->name->chars, "slice", 5) == 0) preserves_type = true;
                else if (method->name->len == 4 && strncmp(method->name->chars, "sort", 4) == 0) preserves_type = true;
                else if (method->name->len == 7 && strncmp(method->name->chars, "reverse", 7) == 0) preserves_type = true;
                else if (method->name->len == 8 && strncmp(method->name->chars, "subarray", 8) == 0) preserves_type = true;
                if (preserves_type) {
                    return jm_detect_typed_array_new(cm->object);
                }
            }
        }
    }
    return -1;
}

// look up typed array type for a class instance field by name.
// checks the class and its superclass chain for instance fields with typed array initializers.
int jm_class_field_ta_type(JsClassEntry* ce, const char* prop_name, int prop_len) {
    while (ce) {
        for (int i = 0; i < ce->instance_field_count; i++) {
            JsInstanceFieldEntry* f = &ce->instance_fields[i];
            if (f->name && (int)f->name->len == prop_len &&
                strncmp(f->name->chars, prop_name, prop_len) == 0) {
                return jm_detect_typed_array_new(f->initializer);
            }
        }
        // also check ctor_prop_ta_types for constructor body assignments
        if (ce->constructor && ce->constructor->fc) {
            JsFuncCollected* fc = ce->constructor->fc;
            for (int i = 0; i < fc->ctor_prop_count; i++) {
                if (fc->ctor_prop_lens[i] == prop_len &&
                    strncmp(fc->ctor_prop_ptrs[i], prop_name, prop_len) == 0) {
                    return fc->ctor_prop_ta_types[i];
                }
            }
        }
        ce = ce->superclass;
    }
    return -1;
}

// P1: Detect field type from constructor init expression (this.x ).
// Returns LMD_TYPE_INT, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_STRING for literals,
// or LMD_TYPE_NULL (unknown) for complex expressions.
// For binary arithmetic, returns FLOAT since JS numbers are all IEEE-754 doubles.
TypeId jm_detect_ctor_field_type(JsAstNode* rhs) {
    if (!rhs) return LMD_TYPE_NULL;
    if (rhs->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)rhs;
        switch (lit->literal_type) {
        case JS_LITERAL_NUMBER:
            return lit->is_bigint ? LMD_TYPE_DECIMAL : LMD_TYPE_FLOAT;
        case JS_LITERAL_BOOLEAN: return LMD_TYPE_BOOL;
        case JS_LITERAL_STRING: return LMD_TYPE_STRING;
        case JS_LITERAL_NULL: return LMD_TYPE_NULL;
        default: return LMD_TYPE_NULL;
        }
    }
    // Unary minus on JS Number keeps the binary64 lane.
    if (rhs->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)rhs;
        if ((un->op == JS_OP_MINUS || un->op == JS_OP_SUB) && un->operand) {
            TypeId inner = jm_detect_ctor_field_type(un->operand);
            if (inner == LMD_TYPE_INT || inner == LMD_TYPE_FLOAT) return inner;
        }
    }
    // Binary arithmetic (+, -, *, /, %) → FLOAT.
    // In JS, all arithmetic produces IEEE-754 doubles. If the expression involves
    // arithmetic, the result slot will hold a float. This catches patterns like
    // `this.vx = vx * DAYS_PER_YER` in nbody.
    if (rhs->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)rhs;
        switch (bin->op) {
        case JS_OP_ADD: case JS_OP_SUB: case JS_OP_MUL:
        case JS_OP_DIV: case JS_OP_MOD:
            return LMD_TYPE_FLOAT;
        default: break;
        }
    }
    // new TypedArray() → treat as array (not typed for native access)
    // Complex expressions (new Foo(), function calls, etc.) → unknown
    return LMD_TYPE_NULL;
}

static bool jm_ident_name_is(String* name, const char* text, int text_len) {
    return name && text && text_len >= 0 && (int)name->len == text_len &&
        strncmp(name->chars, text, (size_t)text_len) == 0;
}

static bool jm_ctor_call_first_arg_is_this(JsCallNode* call) {
    if (!call || !call->arguments) return false;
    JsAstNode* arg = call->arguments;
    if (arg->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)arg;
    return jm_ident_name_is(id->name, "this", 4);
}

static bool jm_ctor_super_prop_name(String* name) {
    return jm_ident_name_is(name, "superConstructor", 16) ||
        jm_ident_name_is(name, "super_", 6);
}

static void jm_note_ctor_super_call(JsFuncCollected* fc, bool via_self_prop,
        String* name) {
    if (!fc || !name || name->len <= 0) return;
    if (fc->ctor_has_super_call) {
        if (fc->ctor_super_via_self_prop != via_self_prop ||
                fc->ctor_super_name_len != (int)name->len ||
                strncmp(fc->ctor_super_name_ptr, name->chars, (size_t)name->len) != 0) {
            fc->ctor_has_dynamic_this_call = true;
        }
        return;
    }
    fc->ctor_has_super_call = true;
    fc->ctor_super_via_self_prop = via_self_prop;
    fc->ctor_super_name_ptr = name->chars;
    fc->ctor_super_name_len = (int)name->len;
}

static void jm_scan_ctor_super_call(JsFuncCollected* fc, JsCallNode* call) {
    if (!fc || !call || !jm_ctor_call_first_arg_is_this(call)) return;
    if (!call->callee || call->callee->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return;
    JsMemberNode* call_member = (JsMemberNode*)call->callee;
    if (call_member->computed || !call_member->property ||
            call_member->property->node_type != JS_AST_NODE_IDENTIFIER) {
        fc->ctor_has_dynamic_this_call = true;
        return;
    }
    JsIdentifierNode* call_prop = (JsIdentifierNode*)call_member->property;
    if (!jm_ident_name_is(call_prop->name, "call", 4)) return;

    if (call_member->object &&
            call_member->object->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* base = (JsIdentifierNode*)call_member->object;
        jm_note_ctor_super_call(fc, false, base->name);
        return;
    }

    if (call_member->object &&
            call_member->object->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* super_member = (JsMemberNode*)call_member->object;
        if (!super_member->computed && super_member->object &&
                super_member->object->node_type == JS_AST_NODE_IDENTIFIER &&
                super_member->property &&
                super_member->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* owner = (JsIdentifierNode*)super_member->object;
            JsIdentifierNode* prop = (JsIdentifierNode*)super_member->property;
            if (jm_ctor_super_prop_name(prop->name)) {
                jm_note_ctor_super_call(fc, true, owner->name);
                return;
            }
        }
    }

    fc->ctor_has_dynamic_this_call = true;
}

static void jm_scan_ctor_prop_assignment(JsFuncCollected* fc, JsAssignmentNode* asgn) {
    if (!fc || !asgn || asgn->op != JS_OP_ASSIGN || !asgn->left ||
        asgn->left->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return;
    if (fc->ctor_shape_overflow) return;
    JsMemberNode* mem = (JsMemberNode*)asgn->left;
    if (!mem->object || mem->object->node_type != JS_AST_NODE_IDENTIFIER ||
        mem->computed || !mem->property ||
        mem->property->node_type != JS_AST_NODE_IDENTIFIER) return;
    JsIdentifierNode* obj_id = (JsIdentifierNode*)mem->object;
    if (obj_id->name->len != 4 ||
        strncmp(obj_id->name->chars, "this", 4) != 0) return;

    JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
    int idx = -1;
    for (int existing = 0; existing < fc->ctor_prop_count; existing++) {
        if (fc->ctor_prop_lens[existing] == (int)prop->name->len &&
            strncmp(fc->ctor_prop_ptrs[existing], prop->name->chars,
                (int)prop->name->len) == 0) {
            idx = existing;
            break;
        }
    }
    if (idx < 0 && fc->ctor_prop_count >= 16) {
        // Dropping the 17th field would build a semantically incomplete optimized shape.
        fc->ctor_prop_count = 0;
        fc->ctor_shape_overflow = true;
        return;
    }

    bool is_new_prop = idx < 0;
    if (is_new_prop) idx = fc->ctor_prop_count;
    fc->ctor_prop_ptrs[idx] = prop->name->chars;
    fc->ctor_prop_lens[idx] = (int)prop->name->len;
    int ta_type = jm_detect_typed_array_new(asgn->right);
    if (ta_type >= 0 || is_new_prop) fc->ctor_prop_ta_types[idx] = ta_type;
    TypeId detected_type = jm_detect_ctor_field_type(asgn->right);
    if (detected_type != LMD_TYPE_NULL || is_new_prop) {
        fc->ctor_prop_types[idx] = detected_type;
    }
    if (is_new_prop) fc->ctor_prop_param_idx[idx] = -1;
    if (asgn->right && asgn->right->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* rhs_id = (JsIdentifierNode*)asgn->right;
        JsAstNode* param = fc->node->params;
        for (int pi = 0; param; pi++, param = param->next) {
            const char* pname = NULL;
            int plen = 0;
            if (param->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* pid = (JsIdentifierNode*)param;
                pname = pid->name->chars;
                plen = (int)pid->name->len;
            } else if (param->node_type == (int)TS_AST_NODE_PARAMETER) {
                TsParameterNode* tsp = (TsParameterNode*)param;
                if (tsp->pattern && tsp->pattern->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* pid = (JsIdentifierNode*)tsp->pattern;
                    pname = pid->name->chars;
                    plen = (int)pid->name->len;
                }
            }
            if (pname && plen == (int)rhs_id->name->len &&
                strncmp(pname, rhs_id->name->chars, plen) == 0) {
                fc->ctor_prop_param_idx[idx] = pi;
                break;
            }
        }
    } else if (!is_new_prop && detected_type != LMD_TYPE_NULL) {
        fc->ctor_prop_param_idx[idx] = -1;
    }
    if (is_new_prop) fc->ctor_prop_count++;
}

static void jm_scan_ctor_expression(JsFuncCollected* fc, JsAstNode* expression) {
    if (!expression) return;
    if (expression->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        jm_scan_ctor_super_call(fc, (JsCallNode*)expression);
        return;
    }
    if (expression->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
        jm_scan_ctor_prop_assignment(fc, (JsAssignmentNode*)expression);
        return;
    }
    if (expression->node_type == JS_AST_NODE_SEQUENCE_EXPRESSION) {
        // Minifiers commonly collapse constructor initialization into a comma
        // sequence; every member still belongs to the fixed instance shape.
        JsSequenceNode* sequence = (JsSequenceNode*)expression;
        for (JsAstNode* child = sequence->expressions; child; child = child->next) {
            jm_scan_ctor_expression(fc, child);
        }
        return;
    }
    if (expression->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        // Short-circuit tails such as `(this.next = next) && ...` are still
        // unconditional shape declarations even when their RHS is not run.
        JsBinaryNode* binary = (JsBinaryNode*)expression;
        jm_scan_ctor_expression(fc, binary->left);
        jm_scan_ctor_expression(fc, binary->right);
    }
}

// A5: Scan constructor body for this.property = expr assignment patterns.
// Records property names in order so we can pre-build the object shape.
void jm_scan_ctor_props(JsFuncCollected* fc, JsAstNode* body) {
    if (!body || body->node_type != JS_AST_NODE_BLOCK_STATEMENT) return;
    memset(fc->ctor_prop_param_idx, -1, sizeof(fc->ctor_prop_param_idx));
    JsBlockNode* blk = (JsBlockNode*)body;
    for (JsAstNode* stmt = blk->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type == JS_AST_NODE_RETURN_STATEMENT ||
            stmt->node_type == JS_AST_NODE_THROW_STATEMENT) break;
        if (stmt->node_type == JS_AST_NODE_EXPRESSION_STATEMENT) {
            jm_scan_ctor_expression(fc,
                ((JsExpressionStatementNode*)stmt)->expression);
        }
    }
    if (fc->ctor_prop_count > 0) {
        log_debug("A5: constructor '%s' has %d this.prop assignments", fc->name,
            fc->ctor_prop_count);
    }
}

void jm_disable_ctor_shape_for_method_overwrite(JsClassEntry* ce) {
    if (!ce || !ce->constructor || !ce->constructor->fc) return;
    JsFuncCollected* fc = ce->constructor->fc;
    for (int prop_index = 0; prop_index < fc->ctor_prop_count; prop_index++) {
        for (JsClassEntry* owner = ce; owner; owner = owner->superclass) {
            for (int method_index = 0; method_index < owner->method_count; method_index++) {
                JsClassMethodEntry* method = &owner->methods[method_index];
                if (method->is_static || method->computed || !method->name) continue;
                if ((int)method->name->len != fc->ctor_prop_lens[prop_index] ||
                        memcmp(method->name->chars, fc->ctor_prop_ptrs[prop_index],
                            method->name->len) != 0) continue;
                // Shape metadata also drives compile-time class access. A
                // prototype method that becomes own data changes dispatch
                // semantics mid-constructor, so the whole optimization must
                // bail out instead of deleting one field from the metadata.
                fc->ctor_prop_count = 0;
                return;
            }
        }
    }
}

// Find class entry by name
JsClassEntry* jm_find_class(JsMirTranspiler* mt, const char* name, int name_len) {
    for (int i = 0; i < mt->class_count; i++) {
        JsClassEntry* ce = &mt->class_entries[i];
        if (ce->name && (int)ce->name->len == name_len &&
            strncmp(ce->name->chars, name, name_len) == 0) {
            return ce;
        }
        // Check alias_name for class expressions: var X = class Y {}
        if (ce->alias_name && (int)ce->alias_name->len == name_len &&
            strncmp(ce->alias_name->chars, name, name_len) == 0) {
            return ce;
        }
    }
    return NULL;
}

// ============================================================================
// Phase 4: Parameter and return type inference
// ============================================================================

// Walk an AST subtree and accumulate type evidence for parameters.
// param_names: array of parameter name strings (with _js_ prefix)
// evidence: array of evidence counters, one per parameter
// param_count: number of parameters
// self_name: function's own name for detecting recursive calls (NULL if none)
static bool jm_expr_has_bigint_literal(JsAstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        return lit->literal_type == JS_LITERAL_NUMBER && lit->is_bigint;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        return jm_expr_has_bigint_literal(bin->left) || jm_expr_has_bigint_literal(bin->right);
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        return jm_expr_has_bigint_literal(un->operand);
    }
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        return jm_expr_has_bigint_literal(cond->test) ||
               jm_expr_has_bigint_literal(cond->consequent) ||
               jm_expr_has_bigint_literal(cond->alternate);
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        if (jm_expr_has_bigint_literal(call->callee)) return true;
        JsAstNode* arg = call->arguments;
        while (arg) {
            if (jm_expr_has_bigint_literal(arg)) return true;
            arg = arg->next;
        }
        return false;
    }
    case JS_AST_NODE_RETURN_STATEMENT: {
        JsReturnNode* ret = (JsReturnNode*)node;
        return jm_expr_has_bigint_literal(ret->argument);
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        JsAstNode* decl = vd->declarations;
        while (decl) {
            if (jm_expr_has_bigint_literal(decl)) return true;
            decl = decl->next;
        }
        return false;
    }
    case JS_AST_NODE_VARIABLE_DECLARATOR: {
        JsVariableDeclaratorNode* vd = (JsVariableDeclaratorNode*)node;
        return jm_expr_has_bigint_literal(vd->init);
    }
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        return jm_expr_has_bigint_literal(es->expression);
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* ifn = (JsIfNode*)node;
        return jm_expr_has_bigint_literal(ifn->test) ||
               jm_expr_has_bigint_literal(ifn->consequent) ||
               jm_expr_has_bigint_literal(ifn->alternate);
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* stmt = blk->statements;
        while (stmt) {
            if (jm_expr_has_bigint_literal(stmt)) return true;
            stmt = stmt->next;
        }
        return false;
    }
    default:
        return false;
    }
}

void jm_infer_walk(JsAstNode* node, const char param_names[][128],
                          FnParamEvidence* evidence, int param_count,
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
        if (lit->is_bigint) return false;
        if (lit->has_decimal) return false;  // 999999.0 is NOT an int literal
        double val = lit->value.number_value;
        return val == (double)(int64_t)val;
    };
    auto is_float_literal = [](JsAstNode* n) -> bool {
        if (!n || n->node_type != JS_AST_NODE_LITERAL) return false;
        JsLiteralNode* lit = (JsLiteralNode*)n;
        if (lit->literal_type != JS_LITERAL_NUMBER) return false;
        if (lit->is_bigint) return false;
        if (lit->has_decimal) return true;  // 999999.0 IS a float literal
        return lit->value.number_value != (double)(int64_t)lit->value.number_value;
    };

    switch (node->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        int li = find_param(bin->left);
        int ri = find_param(bin->right);
        bool is_arith = (bin->op == JS_OP_SUB ||
                         bin->op == JS_OP_MUL || bin->op == JS_OP_DIV ||
                         bin->op == JS_OP_MOD || bin->op == JS_OP_EXP);
        bool is_cmp = (bin->op == JS_OP_LT || bin->op == JS_OP_LE ||
                        bin->op == JS_OP_GT || bin->op == JS_OP_GE ||
                        bin->op == JS_OP_EQ || bin->op == JS_OP_NE ||
                        bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_STRICT_NE);
        bool is_bitwise = (bin->op == JS_OP_BIT_AND || bin->op == JS_OP_BIT_OR ||
                           bin->op == JS_OP_BIT_XOR || bin->op == JS_OP_BIT_LSHIFT ||
                           bin->op == JS_OP_BIT_RSHIFT || bin->op == JS_OP_BIT_URSHIFT);

        if (is_arith) {
            if (li >= 0 && jm_expr_has_bigint_literal(bin->right)) evidence[li].compared_with_non_numeric = true;
            if (ri >= 0 && jm_expr_has_bigint_literal(bin->left))  evidence[ri].compared_with_non_numeric = true;
            // Parameter used in arithmetic with int literal → int evidence
            // NOTE: comparisons (< > == etc.) do NOT contribute type evidence,
            // because JS is dynamically typed — e.g. (x < 0) works for both int and float x.
            if (li >= 0 && is_int_literal(bin->right)) evidence[li].int_evidence++;
            if (ri >= 0 && is_int_literal(bin->left))  evidence[ri].int_evidence++;
            // Parameter used in arithmetic with float literal → float evidence
            if (li >= 0 && is_float_literal(bin->right)) evidence[li].float_evidence++;
            if (ri >= 0 && is_float_literal(bin->left))  evidence[ri].float_evidence++;
            // Two params in arithmetic together → both are numeric, but could be
            // int or float.
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
        // Detect param compared with undefined/null/boolean — these values cannot
        // survive native INT unboxing, so the param must stay boxed (ANY).
        if (is_cmp) {
            auto is_non_numeric_literal = [](JsAstNode* n) -> bool {
                if (!n || n->node_type != JS_AST_NODE_LITERAL) return false;
                JsLiteralNode* l = (JsLiteralNode*)n;
                return l->literal_type == JS_LITERAL_UNDEFINED ||
                       l->literal_type == JS_LITERAL_NULL ||
                       l->literal_type == JS_LITERAL_BOOLEAN;
            };
            if (li >= 0 && is_non_numeric_literal(bin->right))
                evidence[li].compared_with_non_numeric = true;
            if (ri >= 0 && is_non_numeric_literal(bin->left))
                evidence[ri].compared_with_non_numeric = true;
            // P6: Comparison with numeric literal → numeric evidence.
            // e.g., n >= 0, n < limit, n !== 1 → param is numeric.
            if (li >= 0 && !is_non_numeric_literal(bin->right)) {
                if (is_int_literal(bin->right)) evidence[li].int_evidence++;
                else if (is_float_literal(bin->right)) evidence[li].float_evidence++;
            }
            if (ri >= 0 && !is_non_numeric_literal(bin->left)) {
                if (is_int_literal(bin->left)) evidence[ri].int_evidence++;
                else if (is_float_literal(bin->left)) evidence[ri].float_evidence++;
            }
        }
        // Detect param in nullish coalescing (param ?? default) — the ?? operator
        // checks for null/undefined which cannot survive native INT unboxing.
        if (bin->op == JS_OP_NULLISH_COALESCE) {
            if (li >= 0) evidence[li].compared_with_non_numeric = true;
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
            case JS_OP_TYPEOF:
                // typeof param — code may check if param is undefined/string/etc.
                // Native INT unboxing loses type info, so param must stay boxed.
                evidence[oi].compared_with_non_numeric = true;
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
                // Recursive call: pass-through params are type-consistent
                // but only reinforce if there's already arithmetic evidence
                JsAstNode* arg = call->arguments;
                for (int pi = 0; pi < param_count && arg; pi++, arg = arg->next) {
                    int ai = find_param(arg);
                    if (ai >= 0) {
                        if (evidence[ai].int_evidence > 0) evidence[ai].int_evidence++;
                        if (evidence[ai].float_evidence > 0) evidence[ai].float_evidence++;
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
        // A computed key is a full JavaScript PropertyKey, not evidence of an
        // array index. Treating target[key] as numeric miscompiled generic
        // setters used by libraries when key held a CSS property string.
        if (mem->computed) {
            int oi = find_param(mem->object);
            if (oi >= 0) evidence[oi].used_as_container = true;
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
        // P6: Plain assignment to a parameter (e = expr) means the initial call-site
        // type may differ from the post-assignment type. The native version assumes
        // the parameter starts as the inferred type, so reassignment is unsafe.
        if (n->op == JS_OP_ASSIGN) {
            int li = find_param(n->left);
            if (li >= 0) evidence[li].param_reassigned = true;
        }
        // P6: Compound assignments (r -= y, x += 1) → operands are numeric
        if (n->op != JS_OP_ASSIGN) {
            bool is_compound_arith = (n->op == JS_OP_ADD_ASSIGN || n->op == JS_OP_SUB_ASSIGN ||
                                      n->op == JS_OP_MUL_ASSIGN || n->op == JS_OP_DIV_ASSIGN ||
                                      n->op == JS_OP_MOD_ASSIGN || n->op == JS_OP_EXP_ASSIGN);
            bool is_compound_bit = (n->op == JS_OP_BIT_AND_ASSIGN || n->op == JS_OP_BIT_OR_ASSIGN ||
                                    n->op == JS_OP_BIT_XOR_ASSIGN || n->op == JS_OP_LSHIFT_ASSIGN ||
                                    n->op == JS_OP_RSHIFT_ASSIGN || n->op == JS_OP_URSHIFT_ASSIGN);
            if (is_compound_arith || is_compound_bit) {
                int ri = find_param(n->right);
                int li = find_param(n->left);
                if (ri >= 0) {
                    if (is_compound_bit) evidence[ri].int_evidence++;
                    else if (is_float_literal(n->left)) evidence[ri].float_evidence++;
                    else evidence[ri].int_evidence++;
                }
                if (li >= 0) {
                    if (is_compound_bit) evidence[li].int_evidence++;
                    else if (is_float_literal(n->right)) evidence[li].float_evidence++;
                    else if (is_int_literal(n->right)) evidence[li].int_evidence++;
                }
            }
        }
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
void jm_infer_param_types(JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    int pc = jm_count_params(fn);
    fc->param_count = pc;
    fc->formal_length = jm_formal_length(fn);

    // detect rest params (...rest as last parameter)
    fc->has_rest_param = false;
    fc->has_non_simple_params = false;
    if (pc > 0) {
        JsAstNode* last_p = fn->params;
        while (last_p && last_p->next) last_p = last_p->next;
        if (last_p && (last_p->node_type == JS_AST_NODE_REST_ELEMENT ||
                       last_p->node_type == JS_AST_NODE_SPREAD_ELEMENT)) {
            fc->has_rest_param = true;
            fc->has_non_simple_params = true;
        }
        // v20: detect non-simple params (default, destructuring, rest, or non-identifier)
        JsAstNode* check_p = fn->params;
        while (check_p) {
            if (check_p->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN ||
                check_p->node_type == JS_AST_NODE_ARRAY_PATTERN ||
                check_p->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                check_p->node_type == JS_AST_NODE_REST_ELEMENT ||
                check_p->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                fc->has_non_simple_params = true;
                break;
            }
            // Also detect params that are not identifiers (e.g. corrupted rest params
            // where the AST builder produces a LITERAL node instead of REST_ELEMENT)
            if (check_p->node_type != JS_AST_NODE_IDENTIFIER &&
                check_p->node_type != (int)TS_AST_NODE_PARAMETER) {
                fc->has_non_simple_params = true;
                break;
            }
            check_p = check_p->next;
        }
    }

    if (pc == 0 || pc > 16) return;

    // Phase 3.4: Check for TS type annotations on parameters first
    // If ALL params have annotations, use them. Otherwise fall through to body-scan.
    bool use_annotations = false;
    {
        int ann_count = 0;
        JsAstNode* p = fn->params;
        while (p) {
            if (p->node_type == (int)TS_AST_NODE_PARAMETER) {
                TsParameterNode* tsp = (TsParameterNode*)p;
                if (tsp->ts_type) ann_count++;
            }
            p = p->next;
        }
        if (ann_count > 0) {
            // use annotations for annotated params, ANY for unannotated
            use_annotations = true;
            p = fn->params;
            for (int i = 0; i < pc && p; i++, p = p->next) {
                if (p->node_type == (int)TS_AST_NODE_PARAMETER) {
                    TsParameterNode* tsp = (TsParameterNode*)p;
                    if (tsp->ts_type && tsp->ts_type->type_expr && !tsp->optional) {
                        TypeId tid = ts_predefined_name_to_type_id(NULL, 0);  // default
                        // resolve from the predefined_type or type_expr
                        TsTypeNode* tex = tsp->ts_type->type_expr;
                        if (tex->node_type == (int)TS_AST_NODE_PREDEFINED_TYPE) {
                            TsPredefinedTypeNode* pt = (TsPredefinedTypeNode*)tex;
                            tid = pt->predefined_id;
                        } else {
                            // fallback: resolve via ts_resolve_type
                            tid = LMD_TYPE_ANY;
                        }
                        fc->param_types[i] = tid;
                    } else {
                        fc->param_types[i] = LMD_TYPE_ANY;
                    }
                } else if (p->node_type == (int)TS_AST_NODE_PARAMETER) {
                    fc->param_types[i] = LMD_TYPE_ANY;
                } else {
                    // not a TsParameterNode — use body-scan for this param
                    fc->param_types[i] = LMD_TYPE_ANY;
                }
            }
            log_debug("js-mir P3.4: annotation-based param types for %s: [%s%s%s%s]",
                fn->name ? fn->name->chars : "(anon)",
                pc > 0 ? (fc->param_types[0] == LMD_TYPE_INT ? "INT" : fc->param_types[0] == LMD_TYPE_FLOAT ? "FLOAT" : "ANY") : "",
                pc > 1 ? (fc->param_types[1] == LMD_TYPE_INT ? ",INT" : fc->param_types[1] == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
                pc > 2 ? (fc->param_types[2] == LMD_TYPE_INT ? ",INT" : fc->param_types[2] == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
                pc > 3 ? ",..." : "");
        }
    }

    if (use_annotations) return;  // annotations took priority

    // Build parameter name array
    char param_names[16][128];
    JsAstNode* p = fn->params;
    for (int i = 0; i < pc && p; i++, p = p->next) {
        jm_get_param_name(p, i, param_names[i], 128);
    }

    if (jm_expr_has_bigint_literal(fn->body)) {
        for (int i = 0; i < pc; i++) {
            fc->param_types[i] = LMD_TYPE_ANY;
        }
        log_debug("js-mir P4: boxed params for %s because body uses BigInt literals", fc->name);
        return;
    }

    // Build self-name for recursive call detection
    char self_name[128] = {0};
    if (fn->name) {
        snprintf(self_name, sizeof(self_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
    }

    // Accumulate evidence
    FnParamEvidence evidence[16] = {};
    jm_infer_walk(fn->body, param_names, evidence, pc,
                  self_name[0] ? self_name : NULL);

    // P6: Alias tracking — if `let x = param` appears in the function body,
    // re-walk with `x` added as an alias for that param so evidence on `x`
    // (e.g., x >= 0, x * 3 + 1) flows back to the original parameter.
    {
        // Scan top-level statements of function body for `let/var/const x = param`
        char alias_names[16][128];
        int alias_map[16];  // alias_map[i] = param index that alias i maps to
        int alias_count = 0;
        JsBlockNode* body_blk = (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT)
            ? (JsBlockNode*)fn->body : NULL;
        if (body_blk) {
            JsAstNode* stmt = body_blk->statements;
            while (stmt && alias_count < 16) {
                if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
                    JsAstNode* decl = vd->declarations;
                    while (decl && alias_count < 16) {
                        if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
                            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER &&
                                d->init && d->init->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* init_id = (JsIdentifierNode*)d->init;
                                char init_name[128];
                                snprintf(init_name, sizeof(init_name), "_js_%.*s",
                                    (int)init_id->name->len, init_id->name->chars);
                                // Check if init is one of the params
                                for (int pi = 0; pi < pc; pi++) {
                                    if (strcmp(init_name, param_names[pi]) == 0) {
                                        JsIdentifierNode* alias_id = (JsIdentifierNode*)d->id;
                                        snprintf(alias_names[alias_count], 128, "_js_%.*s",
                                            (int)alias_id->name->len, alias_id->name->chars);
                                        alias_map[alias_count] = pi;
                                        alias_count++;
                                        break;
                                    }
                                }
                            }
                        }
                        decl = decl->next;
                    }
                }
                stmt = stmt->next;
            }
        }
        if (alias_count > 0) {
            FnParamEvidence alias_evidence[16] = {};
            jm_infer_walk(fn->body, alias_names, alias_evidence, alias_count,
                          self_name[0] ? self_name : NULL);
            // Merge alias evidence back to original params
            for (int ai = 0; ai < alias_count; ai++) {
                int pi = alias_map[ai];
                evidence[pi].int_evidence += alias_evidence[ai].int_evidence;
                evidence[pi].float_evidence += alias_evidence[ai].float_evidence;
                evidence[pi].string_evidence += alias_evidence[ai].string_evidence;
                if (alias_evidence[ai].used_as_container) evidence[pi].used_as_container = true;
                if (alias_evidence[ai].compared_with_non_numeric) evidence[pi].compared_with_non_numeric = true;
            }
            log_debug("js-mir P6: alias tracking for %s: %d aliases found", fc->name, alias_count);
        }
    }

    // Resolve numeric evidence to FLOAT because JS Number uses binary64 even
    // when every observed argument is integer-looking.
    //          otherwise → ANY
    for (int i = 0; i < pc; i++) {
        if (evidence[i].used_as_container || evidence[i].compared_with_non_numeric) {
            // parameter used as arr[i] object — must remain boxed Item (not unboxed as int/float)
            // OR: parameter compared with undefined/null/boolean — native unboxing would
            // lose the type distinction (e.g., undefined → 0 looks the same as actual 0)
            fc->param_types[i] = LMD_TYPE_ANY;
        } else if (evidence[i].param_reassigned) {
            // parameter is reassigned (e = expr) — the initial call-site value may be
            // a different type (e.g., string passed to IIFE, reassigned via parseInt).
            // Native version assumes param starts as inferred type, which is unsafe.
            fc->param_types[i] = LMD_TYPE_ANY;
        } else if (evidence[i].float_evidence > 0) {
            fc->param_types[i] = LMD_TYPE_FLOAT;
        } else if (evidence[i].int_evidence > 0 && evidence[i].string_evidence == 0) {
            fc->param_types[i] = LMD_TYPE_FLOAT;
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

// check if a + expression chain contains an operand known to produce a string
bool jm_add_chain_has_string(JsAstNode* expr) {
    if (!expr) return false;
    if (expr->node_type == JS_AST_NODE_LITERAL)
        return ((JsLiteralNode*)expr)->literal_type == JS_LITERAL_STRING;
    if (expr->node_type == JS_AST_NODE_TEMPLATE_LITERAL) return true;
    if (expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)expr;
        if (call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
            if (id->name && id->name->len == 6 && strncmp(id->name->chars, "String", 6) == 0)
                return true;
        }
    }
    if (expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        if (bin->op == JS_OP_ADD)
            return jm_add_chain_has_string(bin->left) || jm_add_chain_has_string(bin->right);
    }
    return false;
}

// Infer return type by collecting types from all return statements.
// For recursive calls to self, assume result type matches the base-case return type.
void jm_infer_return_type_walk(JsAstNode* node, const char* self_name,
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
                if (lit->is_bigint) {
                    t = LMD_TYPE_DECIMAL;
                    break;
                }
                t = LMD_TYPE_FLOAT;
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
            case JS_OP_ADD:
                // plus is string concat when any operand is a string; otherwise
                // unknown operands must stay boxed because param + param can
                // still concatenate at runtime.
                if (jm_expr_has_bigint_literal(expr))
                    t = LMD_TYPE_ANY;
                else if (jm_add_chain_has_string(bin->left) || jm_add_chain_has_string(bin->right))
                    t = LMD_TYPE_STRING;
                else
                    t = LMD_TYPE_ANY;
                break;
            case JS_OP_SUB: case JS_OP_MUL: case JS_OP_MOD:
                t = jm_expr_has_bigint_literal(expr) ? LMD_TYPE_ANY : LMD_TYPE_FLOAT;
                break;
            case JS_OP_DIV: case JS_OP_EXP:
                t = jm_expr_has_bigint_literal(expr) ? LMD_TYPE_ANY : LMD_TYPE_FLOAT;
                break;
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
                    t = LMD_TYPE_FLOAT; // recursive JS Number calls return binary64
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

void jm_infer_return_type(JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    fc->return_type = LMD_TYPE_ANY;

    // Phase 3.4: check for explicit TS return type annotation
    if (fn->ts_return_type) {
        TsTypeAnnotationNode* ann = fn->ts_return_type;
        if (ann->type_expr && ann->type_expr->node_type == (int)TS_AST_NODE_PREDEFINED_TYPE) {
            TsPredefinedTypeNode* pt = (TsPredefinedTypeNode*)ann->type_expr;
            fc->return_type = pt->predefined_id;
            log_debug("js-mir P3.4: annotation-based return type for %s: %s",
                fn->name ? fn->name->chars : "(anon)",
                fc->return_type == LMD_TYPE_INT ? "INT" : fc->return_type == LMD_TYPE_FLOAT ? "FLOAT" : "ANY");
            return;
        }
    }

    if (jm_expr_has_bigint_literal(fn->body)) {
        fc->return_type = LMD_TYPE_ANY;
        log_debug("js-mir P4: boxed return for %s because body uses BigInt literals", fc->name);
        return;
    }

    char self_name[128] = {0};
    if (fn->name) {
        snprintf(self_name, sizeof(self_name), "_js_%.*s", (int)fn->name->len, fn->name->chars);
    }

    // For expression-body arrow functions: infer from the expression directly
    if (fn->body && fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        // Arrow function with expression body
        if (fn->body->node_type == JS_AST_NODE_LITERAL) {
            JsLiteralNode* lit = (JsLiteralNode*)fn->body;
            if (lit->literal_type == JS_LITERAL_NUMBER) {
                if (lit->is_bigint) {
                    fc->return_type = LMD_TYPE_DECIMAL;
                    return;
                }
                fc->return_type = LMD_TYPE_FLOAT;
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

    // Unify: all concrete types must agree. If ANY is present (unresolvable
    // expressions like function calls), the return type must stay ANY —
    // we can't assume what the call returns at runtime.
    TypeId unified = LMD_TYPE_ANY;
    bool has_concrete = false;
    bool has_any = false;
    for (int i = 0; i < count; i++) {
        if (collected[i] == LMD_TYPE_ANY) { has_any = true; continue; }
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

    if (has_concrete && !has_any) {
        fc->return_type = unified;
    }

    log_debug("js-mir P4: inferred return type for %s: %s", fc->name,
        fc->return_type == LMD_TYPE_INT ? "INT" :
        fc->return_type == LMD_TYPE_FLOAT ? "FLOAT" : "ANY");
}

// Return expressions whose values always fit directly in Item bits or are
// managed objects never borrow a number-stack cell from the activation.
static bool jm_return_expr_needs_scalar_home(JsAstNode* expr);

static bool jm_const_identifier_has_stable_return_value(JsIdentifierNode* id) {
    if (!id || !id->entry || !id->entry->is_const || !id->entry->node ||
            id->entry->node->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) {
        return false;
    }
    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)id->entry->node;
    return decl->init && !jm_return_expr_needs_scalar_home(decl->init);
}

static bool jm_return_expr_needs_scalar_home(JsAstNode* expr) {
    if (!expr) return false;
    switch (expr->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type != JS_LITERAL_NUMBER || lit->is_bigint) return false;
        return !jm_float_const_is_inline(lit->value.number_value);
    }
    case JS_AST_NODE_ARRAY_EXPRESSION:
    case JS_AST_NODE_OBJECT_EXPRESSION:
    case JS_AST_NODE_FUNCTION_EXPRESSION:
    case JS_AST_NODE_ARROW_FUNCTION:
    case JS_AST_NODE_CLASS_EXPRESSION:
    case JS_AST_NODE_NEW_EXPRESSION:
        return false;
    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)expr;
        return jm_return_expr_needs_scalar_home(cond->consequent) ||
            jm_return_expr_needs_scalar_home(cond->alternate);
    }
    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        JsSequenceNode* seq = (JsSequenceNode*)expr;
        JsAstNode* last = seq->expressions;
        if (!last) return false;
        while (last->next) last = last->next;
        return jm_return_expr_needs_scalar_home(last);
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* unary = (JsUnaryNode*)expr;
        return unary->op != JS_OP_NOT && unary->op != JS_OP_TYPEOF &&
            unary->op != JS_OP_VOID;
    }
    case JS_AST_NODE_IDENTIFIER:
        // A const initializer cannot be rebound. Reuse the initializer's
        // proven lifetime so a BigInt/Date/object Item does not get a dead
        // caller scalar home merely because the return expression is a name.
        return !jm_const_identifier_has_stable_return_value((JsIdentifierNode*)expr);
    default:
        return true;
    }
}

static bool jm_return_walk_needs_scalar_home(JsAstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case JS_AST_NODE_RETURN_STATEMENT:
        return jm_return_expr_needs_scalar_home(((JsReturnNode*)node)->argument);
    case JS_AST_NODE_BLOCK_STATEMENT:
        for (JsAstNode* stmt = ((JsBlockNode*)node)->statements; stmt;
                stmt = stmt->next) {
            if (jm_return_walk_needs_scalar_home(stmt)) return true;
        }
        return false;
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* branch = (JsIfNode*)node;
        return jm_return_walk_needs_scalar_home(branch->consequent) ||
            jm_return_walk_needs_scalar_home(branch->alternate);
    }
    case JS_AST_NODE_WHILE_STATEMENT:
        return jm_return_walk_needs_scalar_home(((JsWhileNode*)node)->body);
    case JS_AST_NODE_FOR_STATEMENT:
        return jm_return_walk_needs_scalar_home(((JsForNode*)node)->body);
    case JS_AST_NODE_TRY_STATEMENT: {
        JsTryNode* attempt = (JsTryNode*)node;
        return jm_return_walk_needs_scalar_home(attempt->block) ||
            jm_return_walk_needs_scalar_home(attempt->handler) ||
            jm_return_walk_needs_scalar_home(attempt->finalizer);
    }
    case JS_AST_NODE_CATCH_CLAUSE:
        return jm_return_walk_needs_scalar_home(((JsCatchNode*)node)->body);
    case JS_AST_NODE_SWITCH_STATEMENT:
        for (JsAstNode* item = ((JsSwitchNode*)node)->cases; item;
                item = item->next) {
            if (jm_return_walk_needs_scalar_home(item)) return true;
        }
        return false;
    case JS_AST_NODE_SWITCH_CASE:
        for (JsAstNode* stmt = ((JsSwitchCaseNode*)node)->consequent; stmt;
                stmt = stmt->next) {
            if (jm_return_walk_needs_scalar_home(stmt)) return true;
        }
        return false;
    default:
        // Nested function bodies are intentionally not traversed.
        return false;
    }
}

ScalarReturnClass jm_infer_boxed_return_scalar_class(JsFuncCollected* fc) {
    if (!fc || !fc->node) return SCALAR_RETURN_DYNAMIC;
    JsAstNode* body = fc->node->body;
    bool needs_home = body && body->node_type == JS_AST_NODE_BLOCK_STATEMENT
        ? jm_return_walk_needs_scalar_home(body)
        : jm_return_expr_needs_scalar_home(body);
    if (!needs_home) return SCALAR_RETURN_NONE;
    return em_scalar_return_class_for_type(fc->return_type);
}

// ============================================================================
// P9: Variable type widening pre-scan
// ============================================================================
//
// Pre-scan a function body to identify INT variables that will be assigned
// FLOAT values (e.g., from Float64Array element access). These variables
// should be created as FLOAT from the start to avoid type mismatch in loops.

// Check if an expression contains evidence that it will evaluate to float
// (float literals or division operators)
bool jm_expression_has_float_hint(JsAstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            if (lit->has_decimal) return true;  // 999999.0 is a float hint
            double v = lit->value.number_value;
            if (v != (double)(long long)v) return true;
        }
        return false;
    }
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        if (bin->op == JS_OP_DIV || bin->op == JS_OP_MOD) return true;
        return jm_expression_has_float_hint(bin->left) || jm_expression_has_float_hint(bin->right);
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        return jm_expression_has_float_hint(un->operand);
    }
    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        if (id->name->len == 3 && strncmp(id->name->chars, "NaN", 3) == 0) return true;
        if (id->name->len == 8 && strncmp(id->name->chars, "Infinity", 8) == 0) return true;
        return false;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION:
        // A named property can contain any JS value. Float-array element
        // reads are recognized separately with container-specific evidence.
        return false;
    default:
        return false;
    }
}

// Check if a variable name is a float typed array, given a set of known float-array vars
bool jm_prescan_is_float_array(struct hashmap* float_arrays, const char* name) {
    JsNameSetEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);
    return hashmap_get(float_arrays, &key) != NULL;
}

// Check if an expression involves a float typed array element access
bool jm_prescan_has_float_array_access(JsAstNode* node, struct hashmap* float_arrays) {
    if (!node) return false;
    // arr[i] where arr is a float typed array
    if (node->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)node;
        if (mem->computed && mem->object && mem->object->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* obj = (JsIdentifierNode*)mem->object;
            char name[128];
            snprintf(name, sizeof(name), "%.*s", (int)obj->name->len, obj->name->chars);
            if (jm_prescan_is_float_array(float_arrays, name)) return true;
        }
    }
    // Check sub-expressions
    if (node->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        return jm_prescan_has_float_array_access(bin->left, float_arrays) ||
               jm_prescan_has_float_array_access(bin->right, float_arrays);
    }
    if (node->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)node;
        return jm_prescan_has_float_array_access(un->operand, float_arrays);
    }
    return false;
}

// Walk AST to find assignments that need float widening
void jm_prescan_widen_walk(JsAstNode* node, struct hashmap* float_arrays,
                                   struct hashmap* widen_vars) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_EXPRESSION_STATEMENT: {
        JsExpressionStatementNode* es = (JsExpressionStatementNode*)node;
        jm_prescan_widen_walk(es->expression, float_arrays, widen_vars);
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        if (asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* dbg_id = (JsIdentifierNode*)asgn->left;
            bool should_widen = false;
            // Widen if RHS accesses a float typed array
            if (jm_prescan_has_float_array_access(asgn->right, float_arrays)) {
                should_widen = true;
            }
            // Widen if /= (always produces float)
            if (asgn->op == JS_OP_DIV_ASSIGN) {
                should_widen = true;
            }
            // Widen if plain assignment = with float evidence in RHS
            // (float literals, division, or property access that may be float)
            if (asgn->op == JS_OP_ASSIGN &&
                jm_expression_has_float_hint(asgn->right)) {
                should_widen = true;
            }
            // Widen if compound assignment with float evidence in RHS
            if ((asgn->op == JS_OP_ADD_ASSIGN || asgn->op == JS_OP_SUB_ASSIGN ||
                 asgn->op == JS_OP_MUL_ASSIGN) &&
                jm_expression_has_float_hint(asgn->right)) {
                should_widen = true;
            }
            log_debug("P9-DBG: prescan assignment '%.*s' op=%d rhs_type=%d float_hint=%d should_widen=%d",
                (int)dbg_id->name->len, dbg_id->name->chars, asgn->op,
                asgn->right ? asgn->right->node_type : -1,
                asgn->right ? jm_expression_has_float_hint(asgn->right) : 0, should_widen);
            if (should_widen) {
                char name[128];
                snprintf(name, sizeof(name), "%.*s", (int)dbg_id->name->len, dbg_id->name->chars);
                jm_name_set_add(widen_vars, name);
                log_debug("P9: prescan widen '%s' to FLOAT", name);
            }
        }
        // Recurse into nested assignments (e.g., P = J = 0 → check J = 0 too)
        if (asgn->right && asgn->right->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
            jm_prescan_widen_walk(asgn->right, float_arrays, widen_vars);
        }
        break;
    }
    case JS_AST_NODE_BLOCK_STATEMENT: {
        JsBlockNode* blk = (JsBlockNode*)node;
        JsAstNode* s = blk->statements;
        while (s) { jm_prescan_widen_walk(s, float_arrays, widen_vars); s = s->next; }
        break;
    }
    case JS_AST_NODE_FOR_STATEMENT: {
        JsForNode* n = (JsForNode*)node;
        jm_prescan_widen_walk(n->body, float_arrays, widen_vars);
        break;
    }
    case JS_AST_NODE_WHILE_STATEMENT: {
        JsWhileNode* n = (JsWhileNode*)node;
        jm_prescan_widen_walk(n->body, float_arrays, widen_vars);
        break;
    }
    case JS_AST_NODE_IF_STATEMENT: {
        JsIfNode* n = (JsIfNode*)node;
        jm_prescan_widen_walk(n->consequent, float_arrays, widen_vars);
        jm_prescan_widen_walk(n->alternate, float_arrays, widen_vars);
        break;
    }
    case JS_AST_NODE_VARIABLE_DECLARATION: {
        // Widen variables declared with float-hinting initializers: let x = a / b
        JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)node;
        JsAstNode* decl = vd->declarations;
        while (decl) {
            if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
                if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER && d->init) {
                    bool should_widen = false;
                    if (jm_prescan_has_float_array_access(d->init, float_arrays))
                        should_widen = true;
                    if (jm_expression_has_float_hint(d->init))
                        should_widen = true;
                    if (should_widen) {
                        JsIdentifierNode* id = (JsIdentifierNode*)d->id;
                        char name[128];
                        snprintf(name, sizeof(name), "%.*s", (int)id->name->len, id->name->chars);
                        jm_name_set_add(widen_vars, name);
                        log_debug("P9: prescan widen '%s' to FLOAT (var decl)", name);
                    }
                }
            }
            decl = decl->next;
        }
        break;
    }
    default: break;
    }
}

// Pre-scan a function body: find float typed arrays and variables needing widening
void jm_prescan_float_widening(JsMirTranspiler* mt, JsAstNode* body) {
    if (!body) return;

    // Step 1: Find all Float32Array/Float64Array variable names
    struct hashmap* float_arrays = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);

    // Walk all var declarations looking for new Float32Array/Float64Array
    // (simplified: only handles top-level and direct block var decls)
    JsAstNode* stmt = NULL;
    if (body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        stmt = ((JsBlockNode*)body)->statements;
    }
    while (stmt) {
        if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
            JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
            JsAstNode* decl = vd->declarations;
            while (decl) {
                if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                    JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
                    if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER &&
                        d->init && d->init->node_type == JS_AST_NODE_NEW_EXPRESSION) {
                        JsCallNode* ne = (JsCallNode*)d->init;
                        if (ne->callee && ne->callee->node_type == JS_AST_NODE_IDENTIFIER) {
                            JsIdentifierNode* ctor = (JsIdentifierNode*)ne->callee;
                            bool is_float_array = false;
                            if (ctor->name->len == 12 &&
                                (strncmp(ctor->name->chars, "Float16Array", 12) == 0 ||
                                 strncmp(ctor->name->chars, "Float64Array", 12) == 0 ||
                                 strncmp(ctor->name->chars, "Float32Array", 12) == 0)) {
                                is_float_array = true;
                            }
                            if (is_float_array) {
                                JsIdentifierNode* vid = (JsIdentifierNode*)d->id;
                                char name[128];
                                snprintf(name, sizeof(name), "%.*s",
                                    (int)vid->name->len, vid->name->chars);
                                jm_name_set_add(float_arrays, name);
                                log_debug("P9: prescan found float typed array '%s'", name);
                            }
                        }
                    }
                }
                decl = decl->next;
            }
        }
        stmt = stmt->next;
    }

    // Step 2: Walk body to find assignments involving float typed array elements
    if (!mt->widen_to_float) {
        mt->widen_to_float = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
    }
    jm_prescan_widen_walk(body, float_arrays, mt->widen_to_float);

    hashmap_free(float_arrays);
}

// Check if a variable name should be widened from INT to FLOAT
bool jm_should_widen_to_float(JsMirTranspiler* mt, const char* vname) {
    if (!mt->widen_to_float) return false;
    // Strip the _js_ prefix to match the prescan names
    const char* bare = vname;
    if (strncmp(vname, "_js_", 4) == 0) bare = vname + 4;
    JsNameSetEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", bare);
    return hashmap_get(mt->widen_to_float, &key) != NULL;
}

JsClassEntry* jm_matching_static_superclass(JsClassEntry* ce, JsAstNode* heritage) {
    if (!ce || !ce->superclass || !ce->superclass->name || !heritage ||
        heritage->node_type != JS_AST_NODE_IDENTIFIER) {
        return NULL;
    }
    JsIdentifierNode* heritage_id = (JsIdentifierNode*)heritage;
    if (!heritage_id->name || heritage_id->name->len != ce->superclass->name->len ||
        strncmp(heritage_id->name->chars, ce->superclass->name->chars,
            heritage_id->name->len) != 0) {
        // Static alias inference is optimization metadata; a differently named
        // heritage binding must still be evaluated in its lexical environment.
        return NULL;
    }
    return ce->superclass;
}

// ============================================================================
// Argument array allocation helper
// ============================================================================

// Allocates stack space for an Item[] args array, stores evaluated args,
// returns register pointing to the array. If arg_count == 0, returns 0.
MIR_reg_t jm_build_args_array(JsMirTranspiler* mt, JsAstNode* first_arg, int arg_count) {
    if (arg_count == 0) return 0;

    // Generator/async mode: if any argument contains a suspend point, we cannot
    // keep the call argument buffer in raw registers across suspend/resume.
    // Instead, spill each evaluated arg to an env slot, then copy to ALLOCA after all done.
    bool has_yield_in_args = false;
    if (mt->in_generator) {
        JsAstNode* chk = first_arg;
        while (chk) {
            if (jm_has_yield(chk) || (mt->in_async && jm_count_awaits(chk) > 0)) {
                has_yield_in_args = true;
                break;
            }
            chk = chk->next;
        }
    }

    if (has_yield_in_args) {
        // Allocate env spill slots for each argument
        int base_spill = mt->gen_spill_slot_next;
        mt->gen_spill_slot_next += arg_count;

        // Evaluate each argument and store to env
        JsAstNode* arg = first_arg;
        for (int i = 0; i < arg_count && arg; i++) {
            MIR_reg_t val = jm_transpile_box_item(mt, arg);
            jm_emit_exc_propagate_check(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    (base_spill + i) * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, val)));
            arg = arg->next;
        }

        // Now all args are safely in env. Copy to heap alloc for the call.
        // Use js_alloc_env instead of MIR_ALLOCA to avoid MIR inlining ALLOCA bug on ARM64.
        MIR_reg_t args_ptr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
        for (int i = 0; i < arg_count; i++) {
            MIR_reg_t tmp = jm_new_reg(mt, "arl", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, tmp),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    (base_spill + i) * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, i * 8, args_ptr, 0, 1),
                MIR_new_reg_op(mt->ctx, tmp)));
        }
        return args_ptr;
    }

    // Args live on the transient call-argument stack (js_args_push), which is
    // registered with the GC once and popped after the call returns. The
    // enclosing call/new scope emits its mark lazily at the first push, so
    // direct and zero-argument calls carry no argument-stack protocol. This
    // avoids the per-call permanent GC root range
    // that made js_alloc_env-based calls O(n^2) in call-heavy loops. We use a
    // runtime stack (not MIR_ALLOCA) to avoid the MIR inlining ALLOCA bug on
    // ARM64 where top-alloca consolidation assigns wrong offsets.
    if (!mt->arg_stack_scope) {
        // Every transient buffer must be bounded by its owning call/new
        // expression; an unscoped push would leak stack roots across calls.
        log_error("js-mir arg-stack invariant: push without call/new scope");
        abort();
    }
    if (!mt->arg_stack_scope->mark) {
        mt->arg_stack_scope->mark = jm_call_0(mt, "js_args_save", MIR_T_I64);
    }
    MIR_reg_t args_ptr = jm_call_1(mt, "js_args_push", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));

    // Evaluate and store each argument
    JsAstNode* arg = first_arg;
    for (int i = 0; i < arg_count && arg; i++) {
        MIR_reg_t val = jm_transpile_box_item(mt, arg);
        jm_emit_exc_propagate_check(mt);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_mem_op(mt->ctx, MIR_T_I64, i * 8, args_ptr, 0, 1),
            MIR_new_reg_op(mt->ctx, val)));
        arg = arg->next;
    }

    return args_ptr;
}

// Build args as a GC-heap JS array, expanding spread elements.
// Returns MIR_reg_t for a boxed JS array Item (LMD_TYPE_ARRAY).
MIR_reg_t jm_build_spread_args_array(JsMirTranspiler* mt, JsAstNode* first_arg) {
    MIR_reg_t array = jm_call_1(mt, "js_array_new", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(mt->ctx, 0));

    // Generator spill: if any argument contains yield, save array ref to env
    int arr_spill_slot = -1;
    if (mt->in_generator) {
        JsAstNode* cy = first_arg;
        while (cy) { if (jm_has_yield(cy)) { arr_spill_slot = jm_gen_spill_save(mt, array); break; } cy = cy->next; }
    }

    JsAstNode* arg = first_arg;
    while (arg) {
        if (arg->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            JsSpreadElementNode* spread = (JsSpreadElementNode*)arg;
            MIR_reg_t src_raw = jm_transpile_box_item(mt, spread->argument);
            jm_emit_exc_propagate_check(mt);
            // Generator spill: restore array after yield in spread argument
            if (arr_spill_slot >= 0 && jm_has_yield(spread->argument)) {
                jm_gen_spill_load(mt, array, arr_spill_slot);
            }
            // Convert any iterable to array first
            MIR_reg_t src = jm_call_1(mt, "js_iterable_to_array", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, src_raw));
            jm_emit_exc_propagate_check(mt);
            // Get length
            MIR_reg_t src_len = jm_call_1(mt, "js_array_length", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, src));
            jm_emit_exc_propagate_check(mt);
            // Loop: push each element
            MIR_reg_t i_reg = jm_new_reg(mt, "spai", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, i_reg), MIR_new_int_op(mt->ctx, 0)));
            MIR_label_t l_check = jm_new_label(mt);
            MIR_label_t l_end = jm_new_label(mt);
            jm_emit_label(mt, l_check);
            MIR_reg_t cmp = jm_new_reg(mt, "spacmp", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LTS, MIR_new_reg_op(mt->ctx, cmp),
                MIR_new_reg_op(mt->ctx, i_reg), MIR_new_reg_op(mt->ctx, src_len)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, MIR_new_label_op(mt->ctx, l_end),
                MIR_new_reg_op(mt->ctx, cmp)));
            MIR_reg_t idx_boxed = jm_new_reg(mt, "spaidx", MIR_T_I64);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, idx_boxed),
                MIR_new_reg_op(mt->ctx, i_reg), MIR_new_uint_op(mt->ctx, ITEM_INT_TAG)));
            MIR_reg_t elem = jm_call_2(mt, "js_array_get", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, src),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_boxed));
            jm_emit_exc_propagate_check(mt);
            jm_call_2(mt, "js_array_push", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, elem));
            jm_emit_exc_propagate_check(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD, MIR_new_reg_op(mt->ctx, i_reg),
                MIR_new_reg_op(mt->ctx, i_reg), MIR_new_int_op(mt->ctx, 1)));
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_check)));
            jm_emit_label(mt, l_end);
        } else {
            MIR_reg_t val = jm_transpile_box_item(mt, arg);
            jm_emit_exc_propagate_check(mt);
            // Generator spill: restore array after yield in argument
            if (arr_spill_slot >= 0 && jm_has_yield(arg)) {
                jm_gen_spill_load(mt, array, arr_spill_slot);
            }
            jm_call_2(mt, "js_array_push", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(mt->ctx, array),
                MIR_T_I64, MIR_new_reg_op(mt->ctx, val));
            jm_emit_exc_propagate_check(mt);
        }
        arg = arg->next;
    }

    return array;
}

int jm_count_args(JsAstNode* arg) {
    int count = 0;
    while (arg) { count++; arg = arg->next; }
    return count;
}

// ============================================================================
// Expression transpilers - each returns MIR_reg_t holding boxed Item result
// ============================================================================

// Forward declarations for transpiler functions defined later
MIR_reg_t jm_transpile_new_expr(JsMirTranspiler* mt, JsCallNode* call);
MIR_reg_t jm_build_closure_for_method(JsMirTranspiler* mt, JsFuncCollected* fc, int param_count);
void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw);
void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw);
void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo);
void jm_scope_env_reload_vars(JsMirTranspiler* mt);
void jm_env_reload_shared_captures(JsMirTranspiler* mt);
void jm_emit_exc_propagate_check(JsMirTranspiler* mt);

// v30: Helper to create a class method function (non-closure) and mark it strict
