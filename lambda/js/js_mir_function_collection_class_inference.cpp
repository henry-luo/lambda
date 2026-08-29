#include "js_mir_internal.hpp"
#include <limits.h>

static bool jm_function_inside_class_syntax(JsFunctionNode* fn) {
    return fn && fn->node_type == JS_AST_NODE_METHOD_DEFINITION;
}

// ============================================================================
// Phase 4: Native call resolution
// ============================================================================

JsFunctionNode* jm_resolve_direct_call_function(JsMirTranspiler* mt,
        JsCallNode* call, bool stable) {
    if (!call->callee || call->callee->node_type != JS_AST_NODE_IDENTIFIER) return NULL;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    // consume the binding resolved by the AST builder; MIR must not rebuild
    // compiler scope state after the indexed unit is sealed.
    AstIndex* index = mt && mt->tp ? &mt->tp->ast_index : NULL;
    AstBindingId binding_id = ast_index_binding_id(index, (AstNode*)id);
    NameEntry* entry = ast_index_binding(index, binding_id);
    AstNode* definition = ast_index_binding_definition(index, binding_id);
    if (!entry || !definition) return NULL;

    JsFunctionNode* fn = NULL;
    JsAstNodeType ntype = ((JsAstNode*)definition)->node_type;
    if (ntype == JS_AST_NODE_FUNCTION_DECLARATION) {
        fn = (JsFunctionNode*)definition;
    } else if (ntype == JS_AST_NODE_VARIABLE_DECLARATOR) {
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)definition;
        if (decl->init && (decl->init->node_type == JS_AST_NODE_FUNCTION_EXPRESSION
            || decl->init->node_type == JS_AST_NODE_ARROW_FUNCTION)) {
            fn = (JsFunctionNode*)decl->init;
        }
    }
    if (stable && ntype == JS_AST_NODE_FUNCTION_DECLARATION &&
            !jm_function_decl_is_direct_binding((JsFunctionNode*)definition, false)) return NULL;
    if (stable && ntype == JS_AST_NODE_VARIABLE_DECLARATOR &&
            (!entry->is_const || !fn ||
             ((JsVariableDeclaratorNode*)definition)->init->source_span.end_byte >
                call->source_span.start_byte)) return NULL;
    return fn;
}

// Phase 3.5: find the collected entry for a direct call without checking native eligibility.
// Used to propagate return types from any known function, even non-native ones.
JsFuncCollected* jm_find_collected_func_for_call(JsMirTranspiler* mt, JsCallNode* call) {
    JsFunctionNode* fn = jm_resolve_direct_call_function(mt, call);
    if (!fn) return NULL;
    return jm_find_collected_func(mt, fn);
}

// Check if a call expression should use the native version of a function.
// Returns the JsFuncCollected* if native call is possible, NULL otherwise.
JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call) {
    // D6.2.2v2: a receiver's inferred class plus a mutable property spelling
    // does not prove callee identity; member calls must observe Get before Call.
    if (call->callee && call->callee->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        return NULL;
    }

    JsFunctionNode* fn = jm_resolve_direct_call_function(mt, call);
    if (!fn) return NULL;
    if (fn->is_async) return NULL;

    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
    if (!fc || !fc->has_native_version || !fc->native_func_item) return NULL;

    // Check if all argument types at this call site match the inferred param types
    JsAstNode* arg = call->arguments;
    for (int i = 0; i < JM_PARAM_COUNT(fc); i++) {
        TypeId expected = jm_param_type(fc, i);
        TypeId actual = arg ? jm_get_effective_type(mt, arg) : LMD_TYPE_ANY;
        if (expected == LMD_TYPE_INT) {
            if (actual != LMD_TYPE_INT && actual != LMD_TYPE_BOOL) return NULL;
        } else if (expected == LMD_TYPE_FLOAT) {
            if (actual != LMD_TYPE_FLOAT && actual != LMD_TYPE_INT) return NULL;
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
    // A known native body is not enough: an unmatched direct call is lowered
    // through the boxed entry, whose slow lane can return any JavaScript value.
    // Reporting the inferred raw return here would make its caller unbox an
    // already boxed string/object result.
    return fc->has_native_version && fc->native_func_item &&
        jm_resolve_native_call(mt, call) == fc;
}

// Indexed return ownership excludes nested functions from tail-call evidence.
// A tail call is `return f(...)` where f is the function itself.
bool jm_has_tail_call(JsMirTranspiler* mt, JsFuncCollected* fc) {
    if (!mt || !fc || !fc->node || !mt->tp) return false;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId fn_node_id = ast_index_find(index, (AstNode*)fc->node);
    AstFunctionId owner = fn_node_id == AST_NODE_ID_INVALID ?
        AST_FUNCTION_ID_INVALID : index->owner_functions[fn_node_id];
    if (owner == AST_FUNCTION_ID_INVALID) return false;
    for (uint32_t i = 0; i < index->count; i++) {
        if (index->owner_functions[i] != owner ||
                index->nodes[i]->node_type != JS_AST_NODE_RETURN_STATEMENT) continue;
        JsReturnNode* ret = (JsReturnNode*)index->nodes[i];
        if (ret->argument && ret->argument->node_type == JS_AST_NODE_CALL_EXPRESSION &&
                jm_is_recursive_call((JsCallNode*)ret->argument, fc)) return true;
    }
    return false;
}

// ============================================================================
// Local function management
// ============================================================================

void jm_register_local_func(JsMirTranspiler* mt, const char* name, MIR_item_t func_item) {
    JsLocalFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.name = mir_em_persist_cstr(&mt->em, name).str;
    entry.func_item = func_item;
    hashmap_set(mt->local_funcs, &entry);
}

// ============================================================================
// Function name generation
// ============================================================================

const char* jm_make_fn_name(JsFunctionNode* fn, JsMirTranspiler* mt) {
    StrBuf* sb = strbuf_new_cap(64);
    strbuf_append_str(sb, "_js_");
    if (fn->name) {
        strbuf_append_str_n(sb, fn->name->chars, fn->name->len);
    } else {
        strbuf_append_str(sb, "anon");
        strbuf_append_int(sb, mt->em.label_counter++);
    }
    strbuf_append_char(sb, '_');
    strbuf_append_int(sb, fn->source_span.start_byte);
    const char* name = jm_persist_name(sb->str);
    strbuf_free(sb);
    return name;
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

// Return the identifier bound by a parameter pattern, when it has one.
JsIdentifierNode* jm_get_param_identifier(JsAstNode* param_node) {
    if (!param_node) return NULL;
    if (param_node->node_type == JS_AST_NODE_IDENTIFIER) {
        return (JsIdentifierNode*)param_node;
    }
    if (param_node->node_type == (int)TS_AST_NODE_PARAMETER) {
        // TsParameterNode: delegate to the wrapped pattern
        TsParameterNode* tsp = (TsParameterNode*)param_node;
        return jm_get_param_identifier(tsp->pattern);
    }
    if (param_node->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        JsAssignmentPatternNode* ap = (JsAssignmentPatternNode*)param_node;
        return jm_get_param_identifier(ap->left);
    }
    if (param_node->node_type == JS_AST_NODE_REST_ELEMENT ||
        param_node->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
        JsSpreadElementNode* sp = (JsSpreadElementNode*)param_node;
        return jm_get_param_identifier(sp->argument);
    }
    return NULL;
}

// Extract the semantic binding name for a function parameter.  MIR formals use
// jm_get_backend_param_name; this spelling remains for JS scope semantics.
const char* jm_get_param_name(JsAstNode* param_node, int index) {
    JsIdentifierNode* pid = jm_get_param_identifier(param_node);
    if (pid && pid->name) {
        return jm_var_name(pid->name);
    }
    return jm_format_name("_js_p%d", index);
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

void jm_collect_functions(JsMirTranspiler* mt, JsAstNode* node);

static String* jm_class_member_source_name(JsMirTranspiler* mt,
        JsClassEntry* owner, JsAstNode* key) {
    if (!mt || !key) return NULL;
    if (key->node_type == JS_AST_NODE_IDENTIFIER) {
        return jm_class_private_name(mt, owner,
            ((JsIdentifierNode*)key)->name);
    }
    if (key->node_type != JS_AST_NODE_LITERAL) return NULL;
    JsLiteralNode* literal = (JsLiteralNode*)key;
    if (literal->literal_type == JS_LITERAL_STRING) {
        return literal->value.string_value;
    }
    if (literal->literal_type == JS_LITERAL_NUMBER) {
        char number_name[64];
        js_double_to_string(literal->value.number_value, number_name,
            sizeof(number_name));
        return name_pool_create_len(mt->tp->name_pool, number_name,
            (int)strlen(number_name));
    }
    return NULL;
}

static JsFuncCollected* jm_collect_class_field_initializer(JsMirTranspiler* mt,
        JsFieldDefinitionNode* field) {
    if (!mt || !field || !field->value ||
        field->value->node_type == JS_AST_NODE_LITERAL) return NULL;
    int children_start = mt->func_count;
    jm_collect_functions(mt, field->value);
    int children_end = mt->func_count;
    if (mt->collection_count_only) {
        if (mt->func_count == INT_MAX) {
            log_error("js-mir: function count overflow in class field initializer");
            mt->collection_failed = true;
            return NULL;
        }
        mt->func_count++;
        return NULL;
    }
    if (mt->func_count >= mt->func_capacity) {
        log_error("js-mir: class field initializer count/fill mismatch at %d of %d",
            mt->func_count, mt->func_capacity);
        mt->collection_failed = true;
        return NULL;
    }

    JsFunctionNode* function = (JsFunctionNode*)pool_calloc(
        mt->tp->pool, sizeof(JsFunctionNode));
    JsBlockNode* body = (JsBlockNode*)pool_calloc(
        mt->tp->pool, sizeof(JsBlockNode));
    JsReturnNode* result = (JsReturnNode*)pool_calloc(
        mt->tp->pool, sizeof(JsReturnNode));
    if (!function || !body || !result) {
        log_error("js-mir: failed to allocate class field initializer AST");
        mt->collection_failed = true;
        return NULL;
    }
    // D6.2.2v2 requires dynamic construction to follow stored capabilities.
    // A synthetic ordinary function preserves the definition environment while
    // receiving the constructed object as `this`; evaluating the expression at
    // class definition would permanently capture the wrong receiver.
    function->node_type = JS_AST_NODE_FUNCTION_EXPRESSION;
    function->source_span = field->source_span;
    function->body = (JsAstNode*)body;
    body->node_type = JS_AST_NODE_BLOCK_STATEMENT;
    body->source_span = field->source_span;
    body->statements = (JsAstNode*)result;
    result->node_type = JS_AST_NODE_RETURN_STATEMENT;
    result->source_span = field->source_span;
    result->argument = field->value;

    // index synthetic capability alongside source functions.
    if (mt->tp && !ast_index_append_profile(&mt->tp->ast_index,
            (AstNode*)function, (AstNode*)field, mt->tp->profile)) {
        log_error("js-mir: failed to index class field initializer");
        mt->collection_failed = true;
        return NULL;
    }

    int function_index = mt->func_count;
    JsFuncCollected* collected = &mt->func_entries[function_index];
    memset(collected, 0, sizeof(JsFuncCollected));
    collected->node = function;
    collected->name = jm_format_name("class_field_initializer_%d_%u",
        function_index, field->source_span.start_byte);
    collected->parent_index = -1;
    collected->is_strict = true;
    collected->is_class_field_initializer = true;
    mt->func_count++;
    for (int child_index = children_start; child_index < children_end;
            child_index++) {
        if (mt->func_entries[child_index].parent_index == -1) {
            mt->func_entries[child_index].parent_index = function_index;
        }
        mt->func_entries[child_index].is_strict = true;
    }
    return collected;
}

// Adapter so the shared child-table visitor can drive the collection walk.
static void jm_collect_functions_child(JsAstNode* child, void* ctx) {
    jm_collect_functions((JsMirTranspiler*)ctx, child);
}

void jm_collect_functions(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node || mt->collection_failed) return;

    switch (node->node_type) {
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
            e->name = jm_make_fn_name(fn, mt);
            e->func_item = NULL; // set during creation
            e->parent_index = -1; // top-level until set by parent
            e->is_strict = mt->is_global_strict || mt->is_module ||
                jm_function_inside_class_syntax(fn) ||
                jm_has_use_strict_directive(fn);
            mt->func_count++;
            // Set parent_index for direct children; strictness covers every
            // descendant collected from this function's body range.
            for (int ci = children_start; ci < children_end; ci++) {
                if (mt->func_entries[ci].parent_index == -1) {
                    mt->func_entries[ci].parent_index = my_index;
                }
                if (e->is_strict) mt->func_entries[ci].is_strict = true;
            }
            // A5: Scan for this.prop = expr patterns (constructor shape pre-alloc)
            jm_scan_ctor_props(mt, e);
        } else {
            // The count/fill walker must agree before pointers into exact storage are published.
            log_error("js-mir: function count/fill mismatch at %d of %d",
                mt->func_count, mt->func_capacity);
            mt->collection_failed = true;
        }
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
            n->init && (n->init->node_type == JS_AST_NODE_CLASS_DECLARATION ||
                n->init->node_type == JS_AST_NODE_CLASS_EXPRESSION) &&
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
                // Minified bundles reuse inner class names; bind this alias to
                // the exact AST entry instead of another same-named class.
                JsClassEntry* ce = NULL;
                for (int i = mt->class_count - 1; i >= 0; i--) {
                    if (mt->class_entries[i].node == cls) {
                        ce = &mt->class_entries[i];
                        break;
                    }
                }
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
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* n = (JsAssignmentNode*)node;
        jm_collect_functions(mt, n->left);
        jm_collect_functions(mt, n->right);
        if (!mt->collection_count_only &&
            n->right && (n->right->node_type == JS_AST_NODE_CLASS_DECLARATION ||
                n->right->node_type == JS_AST_NODE_CLASS_EXPRESSION) &&
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
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION: {
        JsClassNode* cls = (JsClassNode*)node;
        int superclass_functions_start = mt->func_count;
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
                    if (fd->key && fd->value) {
                        if (fd->is_static) jm_collect_functions(mt, fd->value);
                        else jm_collect_class_field_initializer(mt, fd);
                    }
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
            // Class heritage expressions execute in the class's strict realm;
            // carry that fact into every function collected from the expression.
            for (int fi = superclass_functions_start; fi < mt->func_count; fi++) {
                mt->func_entries[fi].is_strict = true;
            }
            JsClassEntry* ce = &mt->class_entries[mt->class_count];
            mt->class_count++; // reserve slot before recursion into methods/fields
            memset(ce, 0, sizeof(JsClassEntry));
            ce->node = cls;
            ce->name = cls->name;
            ce->alias_name = NULL;
            ce->method_count = 0;
            ce->constructor = NULL;
            {
                ce->is_declaration = cls->node_type == JS_AST_NODE_CLASS_DECLARATION;
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
                mt->tp->pool, (size_t)ce->method_capacity * sizeof(JsClassMethodEntry));
            ce->static_fields = (JsStaticFieldEntry*)pool_calloc(
                mt->tp->pool, (size_t)ce->static_field_capacity * sizeof(JsStaticFieldEntry));
            ce->instance_fields = (JsInstanceFieldEntry*)pool_calloc(
                mt->tp->pool, (size_t)ce->instance_field_capacity * sizeof(JsInstanceFieldEntry));
            ce->static_blocks = (JsAstNode**)pool_calloc(
                mt->tp->pool, (size_t)ce->static_block_capacity * sizeof(JsAstNode*));
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
                        // Class field metadata carries the semantic property
                        // spelling; dropping literal keys made `"x";` vanish
                        // during instance initialization (D6.2.2v2).
                        sf->name = !fd->computed
                            ? jm_class_member_source_name(mt, ce, fd->key) : NULL;
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
                        // Instance field source names retain # until class evaluation allocates identity.
                        JsInstanceFieldEntry* inf = &ce->instance_fields[ce->instance_field_count];
                        inf->computed = fd->computed;
                        inf->key_expr = fd->key;
                        inf->name = !fd->computed
                            ? jm_class_member_source_name(mt, ce, fd->key) : NULL;
                        inf->initializer = fd->value;
                        inf->initializer_fc = fd->value
                            ? jm_collect_class_field_initializer(mt, fd) : NULL;
                        inf->key_module_var_index = -1;
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
                            String* method_name = jm_class_member_source_name(
                                mt, ce, md->key);
                            if (!method_name && md->key &&
                                    md->key->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
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
                                fc->name = jm_format_name("%.*s_%s%.*s_%d",
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
                                fc->name = jm_format_name("anon%d_%s%.*s",
                                    mt->func_count, gs_prefix,
                                    (int)method_name->len, method_name->chars);
                            } else {
                                // Use func_count as a unique ID for unnamed computed methods.
                                fc->name = jm_format_name("class_method_%d_%d",
                                    mt->class_count, mt->func_count);
                            }
                            fc->func_item = NULL;
                            fc->is_class_method = true;
                            fc->is_strict = true;
                            mt->func_count++;

                            // Set parent_index for inner functions collected during method body
                            // This ensures capture propagation correctly identifies the method
                            // as the parent, not a grandparent IIFE/function.
                            for (int ci = method_children_start; ci < method_children_end; ci++) {
                                if (mt->func_entries[ci].parent_index == -1) {
                                    mt->func_entries[ci].parent_index = method_index;
                                }
                                mt->func_entries[ci].is_strict = true;
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
                                    jm_scan_ctor_props(mt, fc);
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

    // These kinds are plain traversals whose child order matches
    // JS_AST_CHILDREN, so one shared visitor replaces 35 hand-written
    // bodies. They are listed explicitly rather than folded into `default:`
    // because the original walker did NOT descend into kinds it had no case
    // for (CLASS_EXPRESSION, FIELD/METHOD_DEFINITION, REST_PROPERTY,
    // STATIC_BLOCK); delegating the default would silently start descending
    // into them. Keeping the list explicit makes this refactor behaviour-neutral.
    case JS_AST_NODE_PROGRAM:
    case JS_AST_NODE_BLOCK_STATEMENT:
    case JS_AST_NODE_IF_STATEMENT:
    case AST_NODE_LOOP:
    case JS_AST_NODE_EXPRESSION_STATEMENT:
    case JS_AST_NODE_VARIABLE_DECLARATION:
    case JS_AST_NODE_RETURN_STATEMENT:
    case JS_AST_NODE_CALL_EXPRESSION:
    case JS_AST_NODE_BINARY_EXPRESSION:
    case JS_AST_NODE_UNARY_EXPRESSION:
    case JS_AST_NODE_MEMBER_EXPRESSION:
    case JS_AST_NODE_ARRAY_EXPRESSION:
    case JS_AST_NODE_OBJECT_EXPRESSION:
    case JS_AST_NODE_PROPERTY:
    case JS_AST_NODE_CONDITIONAL_EXPRESSION:
    case JS_AST_NODE_TRY_STATEMENT:
    case JS_AST_NODE_CATCH_CLAUSE:
    case JS_AST_NODE_THROW_STATEMENT:
    case JS_AST_NODE_NEW_EXPRESSION:
    case JS_AST_NODE_SWITCH_STATEMENT:
    case JS_AST_NODE_SWITCH_CASE:
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT:
    case JS_AST_NODE_YIELD_EXPRESSION:
    case JS_AST_NODE_AWAIT_EXPRESSION:
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_SEQUENCE_EXPRESSION:
    case JS_AST_NODE_LABELED_STATEMENT:
    case JS_AST_NODE_WITH_STATEMENT:
    case JS_AST_NODE_ARRAY_PATTERN:
    case JS_AST_NODE_OBJECT_PATTERN:
        js_ast_visit_children(node, jm_collect_functions_child, mt);
        break;
    default:
        break; // leaf nodes, identifiers, literals — unchanged
    }
}

// ============================================================================
// Find collected function entry through the shared AST identity index.
// ============================================================================

JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn) {
    if (mt && fn && mt->tp && mt->tp->ast_index.count && mt->func_by_id) {
        AstNodeId node_id = ast_index_find(&mt->tp->ast_index, (AstNode*)fn);
        if (node_id != AST_NODE_ID_INVALID) {
            AstFunctionId function_id = mt->tp->ast_index.owner_functions[node_id];
            if (function_id < mt->tp->ast_index.function_count && mt->func_by_id[function_id]) {
                return mt->func_by_id[function_id];
            }
        }
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

static bool jm_ctor_indexed_expression_path(AstIndex* index, AstNodeId node_id,
        AstNodeId body_id) {
    while (index && node_id != AST_NODE_ID_INVALID) {
        AstNode* parent = index->parents[node_id];
        if (!parent) return false;
        AstNodeId parent_id = ast_index_find(index, parent);
        if (parent_id == AST_NODE_ID_INVALID) return false;
        switch (parent->node_type) {
        case JS_AST_NODE_SEQUENCE_EXPRESSION:
        case JS_AST_NODE_BINARY_EXPRESSION:
            node_id = parent_id;
            continue;
        case JS_AST_NODE_EXPRESSION_STATEMENT:
            parent = index->parents[parent_id];
            return parent && ast_index_find(index, parent) == body_id;
        default:
            return false;
        }
    }
    return false;
}

// A5: scan constructor assignments from the indexed owner graph. The old
// expression recursion only admitted direct body expression statements and
// sequence/binary wrappers; the path filter preserves that shape exactly.
void jm_scan_ctor_props(JsMirTranspiler* mt, JsFuncCollected* fc) {
    if (!mt || !fc || !fc->node || !fc->node->body ||
            fc->node->body->node_type != JS_AST_NODE_BLOCK_STATEMENT || !mt->tp) return;
    memset(fc->ctor_prop_param_idx, -1, sizeof(fc->ctor_prop_param_idx));
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId fn_id = ast_index_find(index, (AstNode*)fc->node);
    AstNodeId body_id = ast_index_find(index, (AstNode*)fc->node->body);
    if (fn_id == AST_NODE_ID_INVALID || body_id == AST_NODE_ID_INVALID) return;
    AstFunctionId owner = index->owner_functions[fn_id];
    uint32_t terminal_start = UINT32_MAX;
    JsBlockNode* body = (JsBlockNode*)fc->node->body;
    for (JsAstNode* stmt = body->statements; stmt; stmt = stmt->next) {
        if (stmt->node_type != JS_AST_NODE_RETURN_STATEMENT &&
                stmt->node_type != JS_AST_NODE_THROW_STATEMENT) continue;
        if (stmt->source_span.start_byte < terminal_start)
            terminal_start = stmt->source_span.start_byte;
    }
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!node || index->owner_functions[i] != owner ||
                node->node_type != JS_AST_NODE_ASSIGNMENT_EXPRESSION ||
                !jm_index_node_descends(index, i, body_id) ||
                node->source_span.start_byte >= terminal_start ||
                !jm_ctor_indexed_expression_path(index, i, body_id)) continue;
        jm_scan_ctor_prop_assignment(fc, (JsAssignmentNode*)node);
    }
    if (fc->ctor_prop_count > 0) {
        log_debug("A5: constructor '%s' has %d this.prop assignments", fc->name,
            fc->ctor_prop_count);
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
// binding_names: AST-owned parameter or alias names used for evidence lookup
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

static int jm_infer_find_param(JsAstNode* node,
        const String* const binding_names[], int param_count) {
    if (!node || node->node_type != JS_AST_NODE_IDENTIFIER) return -1;
    JsIdentifierNode* id = (JsIdentifierNode*)node;
    for (int i = 0; i < param_count; i++) {
        if (jm_js_name_equal(id->name, binding_names[i])) return i;
    }
    return -1;
}

static bool jm_infer_is_int_literal(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)node;
    if (lit->literal_type != JS_LITERAL_NUMBER || lit->is_bigint || lit->has_decimal) return false;
    double value = lit->value.number_value;
    return value == (double)(int64_t)value;
}

static bool jm_infer_is_float_literal(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)node;
    if (lit->literal_type != JS_LITERAL_NUMBER || lit->is_bigint) return false;
    return lit->has_decimal ||
        lit->value.number_value != (double)(int64_t)lit->value.number_value;
}

static bool jm_infer_is_non_numeric_literal(JsAstNode* node) {
    if (!node || node->node_type != JS_AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)node;
    return lit->literal_type == JS_LITERAL_UNDEFINED ||
        lit->literal_type == JS_LITERAL_NULL || lit->literal_type == JS_LITERAL_BOOLEAN;
}

// Consume one indexed node. Child expressions are visited independently by
// AstIndex, so inference no longer needs a second recursive tree walk.
static void jm_infer_indexed_node(JsAstNode* node,
        const String* const binding_names[], FnParamEvidence* evidence,
        int param_count, const char* self_name) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        int li = jm_infer_find_param(bin->left, binding_names, param_count);
        int ri = jm_infer_find_param(bin->right, binding_names, param_count);
        bool arithmetic = bin->op == JS_OP_SUB || bin->op == JS_OP_MUL ||
            bin->op == JS_OP_DIV || bin->op == JS_OP_MOD || bin->op == JS_OP_EXP;
        bool comparison = bin->op == JS_OP_LT || bin->op == JS_OP_LE ||
            bin->op == JS_OP_GT || bin->op == JS_OP_GE || bin->op == JS_OP_EQ ||
            bin->op == JS_OP_NE || bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_STRICT_NE;
        bool bitwise = bin->op == JS_OP_BIT_AND || bin->op == JS_OP_BIT_OR ||
            bin->op == JS_OP_BIT_XOR || bin->op == JS_OP_BIT_LSHIFT ||
            bin->op == JS_OP_BIT_RSHIFT || bin->op == JS_OP_BIT_URSHIFT;
        if (arithmetic || (bin->op == JS_OP_ADD && self_name && li >= 0 && ri >= 0)) {
            if (li >= 0 && jm_expr_has_bigint_literal(bin->right)) evidence[li].compared_with_non_numeric = true;
            if (ri >= 0 && jm_expr_has_bigint_literal(bin->left)) evidence[ri].compared_with_non_numeric = true;
            if (li >= 0 && jm_infer_is_int_literal(bin->right)) evidence[li].int_evidence++;
            if (ri >= 0 && jm_infer_is_int_literal(bin->left)) evidence[ri].int_evidence++;
            if (li >= 0 && jm_infer_is_float_literal(bin->right)) evidence[li].float_evidence++;
            if (ri >= 0 && jm_infer_is_float_literal(bin->left)) evidence[ri].float_evidence++;
            if (li >= 0 && ri >= 0) { evidence[li].int_evidence++; evidence[ri].int_evidence++; }
        }
        if (bitwise) {
            if (li >= 0) evidence[li].int_evidence++;
            if (ri >= 0) evidence[ri].int_evidence++;
        }
        if (comparison) {
            if (li >= 0 && jm_infer_is_non_numeric_literal(bin->right)) evidence[li].compared_with_non_numeric = true;
            if (ri >= 0 && jm_infer_is_non_numeric_literal(bin->left)) evidence[ri].compared_with_non_numeric = true;
            if (li >= 0 && !jm_infer_is_non_numeric_literal(bin->right)) {
                if (jm_infer_is_int_literal(bin->right)) evidence[li].int_evidence++;
                else if (jm_infer_is_float_literal(bin->right)) evidence[li].float_evidence++;
            }
            if (ri >= 0 && !jm_infer_is_non_numeric_literal(bin->left)) {
                if (jm_infer_is_int_literal(bin->left)) evidence[ri].int_evidence++;
                else if (jm_infer_is_float_literal(bin->left)) evidence[ri].float_evidence++;
            }
        }
        if (bin->op == JS_OP_NULLISH_COALESCE && li >= 0)
            evidence[li].compared_with_non_numeric = true;
        break;
    }
    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* unary = (JsUnaryNode*)node;
        int index = jm_infer_find_param(unary->operand, binding_names, param_count);
        if (index < 0) break;
        switch (unary->op) {
        case JS_OP_PLUS: case JS_OP_ADD: case JS_OP_MINUS: case JS_OP_SUB:
        case JS_OP_INCREMENT: case JS_OP_DECREMENT: case JS_OP_BIT_NOT:
            evidence[index].int_evidence++; break;
        case JS_OP_TYPEOF:
            evidence[index].compared_with_non_numeric = true; break;
        default: break;
        }
        break;
    }
    case JS_AST_NODE_CALL_EXPRESSION: {
        JsCallNode* call = (JsCallNode*)node;
        if (self_name && call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            const char* callee_name = jm_var_name(((JsIdentifierNode*)call->callee)->name);
            if (strncmp(callee_name, self_name, strlen(self_name)) == 0) {
                JsAstNode* arg = call->arguments;
                for (int pi = 0; pi < param_count && arg; pi++, arg = arg->next) {
                    int index = jm_infer_find_param(arg, binding_names, param_count);
                    if (index >= 0) {
                        if (evidence[index].int_evidence > 0) evidence[index].int_evidence++;
                        if (evidence[index].float_evidence > 0) evidence[index].float_evidence++;
                    }
                }
            }
        }
        break;
    }
    case JS_AST_NODE_MEMBER_EXPRESSION: {
        JsMemberNode* member = (JsMemberNode*)node;
        if (member->computed) {
            int index = jm_infer_find_param(member->object, binding_names, param_count);
            if (index >= 0) evidence[index].used_as_container = true;
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        int left = jm_infer_find_param(assignment->left, binding_names, param_count);
        if (assignment->op == JS_OP_ASSIGN) {
            if (left >= 0) evidence[left].param_reassigned = true;
            break;
        }
        bool compound_arith = assignment->op == JS_OP_ADD_ASSIGN || assignment->op == JS_OP_SUB_ASSIGN ||
            assignment->op == JS_OP_MUL_ASSIGN || assignment->op == JS_OP_DIV_ASSIGN ||
            assignment->op == JS_OP_MOD_ASSIGN || assignment->op == JS_OP_EXP_ASSIGN;
        bool compound_bit = assignment->op == JS_OP_BIT_AND_ASSIGN || assignment->op == JS_OP_BIT_OR_ASSIGN ||
            assignment->op == JS_OP_BIT_XOR_ASSIGN || assignment->op == JS_OP_LSHIFT_ASSIGN ||
            assignment->op == JS_OP_RSHIFT_ASSIGN || assignment->op == JS_OP_URSHIFT_ASSIGN;
        if (!compound_arith && !compound_bit) break;
        int right = jm_infer_find_param(assignment->right, binding_names, param_count);
        if (right >= 0) {
            if (compound_bit) evidence[right].int_evidence++;
            else if (jm_infer_is_float_literal(assignment->left)) evidence[right].float_evidence++;
            else evidence[right].int_evidence++;
        }
        if (left >= 0) {
            if (compound_bit) evidence[left].int_evidence++;
            else if (jm_infer_is_float_literal(assignment->right)) evidence[left].float_evidence++;
            else if (jm_infer_is_int_literal(assignment->right)) evidence[left].int_evidence++;
        }
        break;
    }
    default: break;
    }
}

static void jm_infer_indexed(JsMirTranspiler* mt, JsFunctionNode* fn,
        const String* const binding_names[], FnParamEvidence* evidence,
        int param_count, const char* self_name) {
    if (!mt || !mt->tp || !fn) return;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId fn_id = ast_index_find(index, (AstNode*)fn);
    AstFunctionId owner = fn_id == AST_NODE_ID_INVALID
        ? AST_FUNCTION_ID_INVALID : index->owner_functions[fn_id];
    if (owner == AST_FUNCTION_ID_INVALID) return;
    for (uint32_t i = 0; i < index->count; i++) {
        if (index->owner_functions[i] == owner)
            jm_infer_indexed_node((JsAstNode*)index->nodes[i], binding_names,
                evidence, param_count, self_name);
    }
}

// Infer parameter types for a collected function from body usage patterns.
void jm_infer_param_types(JsMirTranspiler* mt, JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    int pc = jm_count_params(fn);
    JM_PARAM_COUNT(fc) = pc;
    JM_JS_FACT(fc, formal_length) = jm_formal_length(fn);
    FnAnalysis* analysis = jm_function_analysis(fc);
    if (analysis->param_types) {
        mem_free(analysis->param_types);
        analysis->param_types = NULL; analysis->param_count = 0;
    }
    analysis->param_count = pc;
    if (pc > 0) {
        analysis->param_types = (FnParamTypeInfo*)mem_calloc((size_t)pc,
            sizeof(FnParamTypeInfo), MEM_CAT_JS_RUNTIME);
        if (!analysis->param_types) {
            analysis->param_count = 0;
            log_error("js-mir: parameter metadata allocation failed for %d formals", pc);
            return;
        }
        for (int i = 0; i < pc; i++) {
            jm_set_param_type(fc, i, LMD_TYPE_ANY);
        }
    }

    // detect rest params (...rest as last parameter)
    JM_JS_FACT(fc, has_rest_param) = false;
    JM_JS_FACT(fc, has_non_simple_params) = false;
    if (pc > 0) {
        JsAstNode* last_p = fn->params;
        while (last_p && last_p->next) last_p = last_p->next;
        if (last_p && (last_p->node_type == JS_AST_NODE_REST_ELEMENT ||
                       last_p->node_type == JS_AST_NODE_SPREAD_ELEMENT)) {
            JM_JS_FACT(fc, has_rest_param) = true;
            JM_JS_FACT(fc, has_non_simple_params) = true;
        }
        // v20: detect non-simple params (default, destructuring, rest, or non-identifier)
        JsAstNode* check_p = fn->params;
        while (check_p) {
            if (check_p->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN ||
                check_p->node_type == JS_AST_NODE_ARRAY_PATTERN ||
                check_p->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                check_p->node_type == JS_AST_NODE_REST_ELEMENT ||
                check_p->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JM_JS_FACT(fc, has_non_simple_params) = true;
                break;
            }
            // Also detect params that are not identifiers (e.g. corrupted rest params
            // where the AST builder produces a LITERAL node instead of REST_ELEMENT)
            if (check_p->node_type != JS_AST_NODE_IDENTIFIER &&
                check_p->node_type != (int)TS_AST_NODE_PARAMETER) {
                JM_JS_FACT(fc, has_non_simple_params) = true;
                break;
            }
            check_p = check_p->next;
        }
    }

    if (pc == 0) return;
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
                        jm_set_param_type(fc, i, tid);
                    } else {
                        jm_set_param_type(fc, i, LMD_TYPE_ANY);
                    }
                } else if (p->node_type == (int)TS_AST_NODE_PARAMETER) {
                    jm_set_param_type(fc, i, LMD_TYPE_ANY);
                } else {
                    // not a TsParameterNode — use body-scan for this param
                    jm_set_param_type(fc, i, LMD_TYPE_ANY);
                }
            }
            log_debug("js-mir P3.4: annotation-based param types for %s: [%s%s%s%s]",
                fn->name ? fn->name->chars : "(anon)",
                pc > 0 ? (jm_param_type(fc, 0) == LMD_TYPE_INT ? "INT" : jm_param_type(fc, 0) == LMD_TYPE_FLOAT ? "FLOAT" : "ANY") : "",
                pc > 1 ? (jm_param_type(fc, 1) == LMD_TYPE_INT ? ",INT" : jm_param_type(fc, 1) == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
                pc > 2 ? (jm_param_type(fc, 2) == LMD_TYPE_INT ? ",INT" : jm_param_type(fc, 2) == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
                pc > 3 ? ",..." : "");
        }
    }

    if (use_annotations) return;  // annotations took priority

    // Keep formal bindings as AST-owned names; copying them into fixed C
    // buffers made inference sensitive to source-name length.
    const String** param_bindings = (const String**)mem_calloc((size_t)pc,
        sizeof(*param_bindings), MEM_CAT_JS_RUNTIME);
    FnParamEvidence* evidence = (FnParamEvidence*)mem_calloc((size_t)pc,
        sizeof(*evidence), MEM_CAT_JS_RUNTIME);
    if (!param_bindings || !evidence) {
        if (param_bindings) mem_free(param_bindings);
        if (evidence) mem_free(evidence);
        log_error("js-mir: inference scratch allocation failed for %d formals", pc);
        return;
    }
    JsAstNode* p = fn->params;
    for (int i = 0; i < pc && p; i++, p = p->next) {
        param_bindings[i] = jm_param_binding_name(p);
    }

    if (jm_expr_has_bigint_literal(fn->body)) {
        for (int i = 0; i < pc; i++) {
            jm_set_param_type(fc, i, LMD_TYPE_ANY);
        }
        log_debug("js-mir P4: boxed params for %s because body uses BigInt literals", fc->name);
        mem_free(param_bindings);
        mem_free(evidence);
        return;
    }

    // Build self-name for recursive call detection
    const char* self_name = NULL;
    if (fn->name) {
        self_name = jm_var_name(fn->name);
    }

    // Accumulate evidence
    jm_infer_indexed(mt, fn, param_bindings, evidence, pc,
                     self_name && self_name[0] ? self_name : NULL);

    // P6: Alias tracking — if `let x = param` appears in the function body,
    // re-walk with `x` added as an alias for that param so evidence on `x`
    // (e.g., x >= 0, x * 3 + 1) flows back to the original parameter.
    {
        // Scan top-level statements of function body for `let/var/const x = param`
        const String** alias_bindings = (const String**)mem_calloc((size_t)pc,
            sizeof(*alias_bindings), MEM_CAT_JS_RUNTIME);
        int* alias_map = (int*)mem_calloc((size_t)pc, sizeof(*alias_map),
            MEM_CAT_JS_RUNTIME);  // alias_map[i] = param index that alias i maps to
        int alias_count = 0;
        JsBlockNode* body_blk = (fn->body && fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT)
            ? (JsBlockNode*)fn->body : NULL;
        if (body_blk) {
            JsAstNode* stmt = body_blk->statements;
            while (stmt && alias_count < pc) {
                if (stmt->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                    JsVariableDeclarationNode* vd = (JsVariableDeclarationNode*)stmt;
                    JsAstNode* decl = vd->declarations;
                    while (decl && alias_count < pc) {
                        if (decl->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
                            JsVariableDeclaratorNode* d = (JsVariableDeclaratorNode*)decl;
                            if (d->id && d->id->node_type == JS_AST_NODE_IDENTIFIER &&
                                d->init && d->init->node_type == JS_AST_NODE_IDENTIFIER) {
                                JsIdentifierNode* init_id = (JsIdentifierNode*)d->init;
                                // Check if init is one of the params
                                for (int pi = 0; pi < pc; pi++) {
                                    if (jm_js_name_equal(init_id->name, param_bindings[pi])) {
                                        JsIdentifierNode* alias_id = (JsIdentifierNode*)d->id;
                                        alias_bindings[alias_count] = alias_id->name;
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
        if (alias_count > 0 && alias_bindings && alias_map) {
            FnParamEvidence* alias_evidence = (FnParamEvidence*)mem_calloc(
                (size_t)alias_count, sizeof(*alias_evidence), MEM_CAT_JS_RUNTIME);
            if (alias_evidence) {
                jm_infer_indexed(mt, fn, alias_bindings, alias_evidence, alias_count,
                                 self_name && self_name[0] ? self_name : NULL);
                // merge alias evidence back to original params
                for (int ai = 0; ai < alias_count; ai++) {
                    int pi = alias_map[ai];
                    evidence[pi].int_evidence += alias_evidence[ai].int_evidence;
                    evidence[pi].float_evidence += alias_evidence[ai].float_evidence;
                    evidence[pi].string_evidence += alias_evidence[ai].string_evidence;
                    if (alias_evidence[ai].used_as_container) evidence[pi].used_as_container = true;
                    if (alias_evidence[ai].compared_with_non_numeric) evidence[pi].compared_with_non_numeric = true;
                }
                mem_free(alias_evidence);
            }
            log_debug("js-mir P6: alias tracking for %s: %d aliases found", fc->name, alias_count);
        }
        if (alias_bindings) mem_free(alias_bindings);
        if (alias_map) mem_free(alias_map);
    }

    // Resolve numeric evidence to FLOAT because JS Number uses binary64 even
    // when every observed argument is integer-looking.
    //          otherwise → ANY
    for (int i = 0; i < pc; i++) {
        if (evidence[i].used_as_container || evidence[i].compared_with_non_numeric) {
            // parameter used as arr[i] object — must remain boxed Item (not unboxed as int/float)
            // OR: parameter compared with undefined/null/boolean — native unboxing would
            // lose the type distinction (e.g., undefined → 0 looks the same as actual 0)
            jm_set_param_type(fc, i, LMD_TYPE_ANY);
        } else if (evidence[i].param_reassigned) {
            // parameter is reassigned (e = expr) — the initial call-site value may be
            // a different type (e.g., string passed to IIFE, reassigned via parseInt).
            // Native version assumes param starts as inferred type, which is unsafe.
            jm_set_param_type(fc, i, LMD_TYPE_ANY);
        } else if (evidence[i].float_evidence > 0) {
            jm_set_param_type(fc, i, LMD_TYPE_FLOAT);
        } else if (evidence[i].int_evidence > 0 && evidence[i].string_evidence == 0) {
            jm_set_param_type(fc, i, LMD_TYPE_FLOAT);
        } else {
            jm_set_param_type(fc, i, LMD_TYPE_ANY);
        }
    }

    log_debug("js-mir P4: inferred param types for %s: [%s%s%s%s]",
        fc->name,
        pc > 0 ? (jm_param_type(fc, 0) == LMD_TYPE_INT ? "INT" : jm_param_type(fc, 0) == LMD_TYPE_FLOAT ? "FLOAT" : "ANY") : "",
        pc > 1 ? (jm_param_type(fc, 1) == LMD_TYPE_INT ? ",INT" : jm_param_type(fc, 1) == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
        pc > 2 ? (jm_param_type(fc, 2) == LMD_TYPE_INT ? ",INT" : jm_param_type(fc, 2) == LMD_TYPE_FLOAT ? ",FLOAT" : ",ANY") : "",
        pc > 3 ? ",..." : "");
    mem_free(param_bindings);
    mem_free(evidence);
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

// Classify one return node. The indexed owner filter below excludes nested
// functions, so this helper has no recursive AST traversal of its own.
static void jm_collect_return_type(JsAstNode* node, const char* self_name,
        JsFuncCollected* fc, TypeId* collected, int* count, int max_count) {
    if (!node || node->node_type != JS_AST_NODE_RETURN_STATEMENT ||
            !count || *count >= max_count) return;
    JsReturnNode* ret = (JsReturnNode*)node;
    if (!ret->argument) {
        collected[(*count)++] = LMD_TYPE_NULL;
        return;
    }
    JsAstNode* expr = ret->argument;
    TypeId t = LMD_TYPE_ANY;
    if (expr->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            t = lit->is_bigint ? LMD_TYPE_DECIMAL : LMD_TYPE_FLOAT;
        } else if (lit->literal_type == JS_LITERAL_BOOLEAN) {
            t = LMD_TYPE_BOOL;
        } else if (lit->literal_type == JS_LITERAL_STRING) {
            t = LMD_TYPE_STRING;
        }
    } else if (expr->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)expr;
        JsAstNode* param = fc && fc->node ? fc->node->params : NULL;
        for (int pi = 0; param && fc && pi < JM_PARAM_COUNT(fc);
                pi++, param = param->next) {
            if (jm_js_name_equal(id->name, jm_param_binding_name(param))) {
                TypeId param_type = jm_param_type(fc, pi);
                if (param_type == LMD_TYPE_INT || param_type == LMD_TYPE_FLOAT) t = param_type;
                break;
            }
        }
    } else if (expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
            t = LMD_TYPE_BOOL; break;
        case JS_OP_ADD:
            if (!jm_expr_has_bigint_literal(expr) &&
                    (jm_add_chain_has_string(bin->left) || jm_add_chain_has_string(bin->right)))
                t = LMD_TYPE_STRING;
            break;
        case JS_OP_SUB: case JS_OP_MUL: case JS_OP_MOD: case JS_OP_DIV: case JS_OP_EXP:
            t = jm_expr_has_bigint_literal(expr) ? LMD_TYPE_ANY : LMD_TYPE_FLOAT;
            break;
        default: break;
        }
    } else if (expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)expr;
        if (self_name && call->callee && call->callee->node_type == JS_AST_NODE_IDENTIFIER) {
            const char* cn = jm_var_name(((JsIdentifierNode*)call->callee)->name);
            if (strncmp(cn, self_name, strlen(self_name)) == 0) t = LMD_TYPE_FLOAT;
        }
    }
    collected[(*count)++] = t;
}

void jm_infer_return_type(JsMirTranspiler* mt, JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    JM_JS_FACT(fc, return_type) = LMD_TYPE_ANY;

    // Phase 3.4: check for explicit TS return type annotation
    if (fn->ts_return_type) {
        TsTypeAnnotationNode* ann = fn->ts_return_type;
        if (ann->type_expr && ann->type_expr->node_type == (int)TS_AST_NODE_PREDEFINED_TYPE) {
            TsPredefinedTypeNode* pt = (TsPredefinedTypeNode*)ann->type_expr;
            JM_JS_FACT(fc, return_type) = pt->predefined_id;
            log_debug("js-mir P3.4: annotation-based return type for %s: %s",
                fn->name ? fn->name->chars : "(anon)",
                JM_JS_FACT(fc, return_type) == LMD_TYPE_INT ? "INT" : JM_JS_FACT(fc, return_type) == LMD_TYPE_FLOAT ? "FLOAT" : "ANY");
            return;
        }
    }

    if (jm_expr_has_bigint_literal(fn->body)) {
        JM_JS_FACT(fc, return_type) = LMD_TYPE_ANY;
        log_debug("js-mir P4: boxed return for %s because body uses BigInt literals", fc->name);
        return;
    }

    const char* self_name = NULL;
    if (fn->name) {
        self_name = jm_var_name(fn->name);
    }

    // For expression-body arrow functions: infer from the expression directly
    if (fn->body && fn->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        // Arrow function with expression body
        if (fn->body->node_type == JS_AST_NODE_LITERAL) {
            JsLiteralNode* lit = (JsLiteralNode*)fn->body;
            if (lit->literal_type == JS_LITERAL_NUMBER) {
                if (lit->is_bigint) {
                    JM_JS_FACT(fc, return_type) = LMD_TYPE_DECIMAL;
                    return;
                }
                JM_JS_FACT(fc, return_type) = LMD_TYPE_FLOAT;
            }
        }
        return;
    }

    TypeId collected[32];
    int count = 0;
    const char* return_self_name = self_name && self_name[0] ? self_name : NULL;
    if (mt && mt->tp) {
        AstIndex* index = &mt->tp->ast_index;
        AstNodeId fn_node_id = ast_index_find(index, (AstNode*)fn);
        AstFunctionId function_id = fn_node_id == AST_NODE_ID_INVALID
            ? AST_FUNCTION_ID_INVALID : index->owner_functions[fn_node_id];
        for (uint32_t i = 0; i < index->count && count < 32; i++) {
            if (index->owner_functions[i] != function_id) continue;
            jm_collect_return_type(index->nodes[i], return_self_name, fc,
                collected, &count, 32);
        }
    }

    if (count == 0) {
        JM_JS_FACT(fc, return_type) = LMD_TYPE_NULL; // no return statements → returns undefined
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
                JM_JS_FACT(fc, return_type) = LMD_TYPE_ANY;
                return;
            }
        }
    }

    if (has_concrete && !has_any) {
        JM_JS_FACT(fc, return_type) = unified;
    }

    log_debug("js-mir P4: inferred return type for %s: %s", fc->name,
        JM_JS_FACT(fc, return_type) == LMD_TYPE_INT ? "INT" :
        JM_JS_FACT(fc, return_type) == LMD_TYPE_FLOAT ? "FLOAT" : "ANY");
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

ScalarReturnClass jm_infer_boxed_return_scalar_class(JsMirTranspiler* mt,
        JsFuncCollected* fc) {
    if (!mt || !fc || !fc->node || !mt->tp) return SCALAR_RETURN_DYNAMIC;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId fn_id = ast_index_find(index, (AstNode*)fc->node);
    if (fn_id == AST_NODE_ID_INVALID) return SCALAR_RETURN_DYNAMIC;
    AstFunctionId owner = index->owner_functions[fn_id];
    bool needs_home = false;
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!node || index->owner_functions[i] != owner ||
                node->node_type != JS_AST_NODE_RETURN_STATEMENT ||
                !jm_index_node_descends(index, i, fn_id)) continue;
        if (jm_return_expr_needs_scalar_home(((JsReturnNode*)node)->argument)) {
            needs_home = true;
            break;
        }
    }
    if (!needs_home) return SCALAR_RETURN_NONE;
    return em_scalar_return_class_for_type(JM_JS_FACT(fc, return_type));
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
    key.name = jm_persist_name(name);
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
            const char* name = jm_format_name("%.*s",
                (int)obj->name->len, obj->name->chars);
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
            if (should_widen) {
                const char* name = jm_format_name("%.*s",
                    (int)dbg_id->name->len, dbg_id->name->chars);
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
    case AST_NODE_LOOP: {
        AstLoopControlNode* loop = (AstLoopControlNode*)node;
        jm_prescan_widen_walk(loop->body, float_arrays, widen_vars);
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
                        const char* name = jm_format_name("%.*s",
                            (int)id->name->len, id->name->chars);
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
                                const char* name = jm_format_name("%.*s",
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
    key.name = jm_persist_name(bare);
    return hashmap_get(mt->widen_to_float, &key) != NULL;
}

JsClassEntry* jm_matching_static_superclass(JsClassEntry* ce, JsAstNode* heritage) {
    if (!ce || !ce->superclass || !ce->superclass->name || !heritage ||
        heritage->node_type != JS_AST_NODE_IDENTIFIER) {
        return NULL;
    }
    JsIdentifierNode* heritage_id = (JsIdentifierNode*)heritage;
    if (!heritage_id->name) return NULL;
    bool matches_name = heritage_id->name->len == ce->superclass->name->len &&
        strncmp(heritage_id->name->chars, ce->superclass->name->chars,
            heritage_id->name->len) == 0;
    bool matches_alias = ce->superclass->alias_name &&
        heritage_id->name->len == ce->superclass->alias_name->len &&
        strncmp(heritage_id->name->chars, ce->superclass->alias_name->chars,
            heritage_id->name->len) == 0;
    if (!matches_name && !matches_alias) {
        // Only the class declaration name and the collector-recorded alias
        // identify this exact class binding. Any other identifier must retain
        // its lexical runtime lookup because it can be shadowed.
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
            if (jm_has_yield(mt, chk) || (mt->in_async && jm_count_awaits(mt, chk) > 0)) {
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
            jm_emit_error_lane_propagate_check(mt);
            jm_emit_store_i64(mt, (base_spill + i) * (int)sizeof(uint64_t), mt->gen_env_reg, val);
            arg = arg->next;
        }

        // Now all args are safely in env. Copy to heap alloc for the call.
        // Use js_alloc_env instead of MIR_ALLOCA to avoid MIR inlining ALLOCA bug on ARM64.
        MIR_reg_t args_ptr = jm_call_1(mt, "js_alloc_env", MIR_T_I64,
            MIR_T_I64, MIR_new_int_op(mt->ctx, arg_count));
        for (int i = 0; i < arg_count; i++) {
            MIR_reg_t tmp = jm_new_reg(mt, "arl", MIR_T_I64);
            jm_emit_load_i64(mt, tmp, (base_spill + i) * (int)sizeof(uint64_t), mt->gen_env_reg);
            jm_emit_store_i64(mt, i * 8, args_ptr, tmp);
        }
        return args_ptr;
    }

    // Args occupy fixed slots in the generated function's canonical root
    // frame. Nested call expressions use disjoint higher slots, while sibling
    // calls reuse the same bounded extent.
    if (!mt->arg_stack_scope) {
        log_error("js-mir arg-frame invariant: args without call/new scope");
        abort();
    }
    JsMirArgStackScope* scope = mt->arg_stack_scope;
    if (scope->base_slot < 0) {
        scope->base_slot = mt->arg_frame_depth;
        scope->slot_count = arg_count;
        mt->arg_frame_depth += arg_count;
        if (mt->arg_frame_depth > mt->arg_frame_slot_count) {
            mt->arg_frame_slot_count = mt->arg_frame_depth;
        }
    } else if (scope->slot_count != arg_count) {
        log_error("js-mir arg-frame invariant: scope arity changed");
        abort();
    }
    MIR_reg_t args_ptr = jm_new_reg(mt, "js_args_ptr", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD,
        MIR_new_reg_op(mt->ctx, args_ptr),
        MIR_new_reg_op(mt->ctx, jm_arg_frame_base(mt)),
        MIR_new_int_op(mt->ctx,
            (int64_t)scope->base_slot * (int64_t)sizeof(uint64_t))));
    // The prerooted ABI is valid only for this exact frame-relative register;
    // another same-arity buffer may have unrelated lifetime ownership.
    scope->args_reg = args_ptr;

    // Evaluate and store each argument
    JsAstNode* arg = first_arg;
    for (int i = 0; i < arg_count && arg; i++) {
        MIR_reg_t val = jm_transpile_box_item(mt, arg);
        jm_emit_error_lane_propagate_check(mt);
        jm_emit_store_i64(mt, i * 8, args_ptr, val);
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
        while (cy) { if (jm_has_yield(mt, cy)) { arr_spill_slot = jm_gen_spill_save(mt, array); break; } cy = cy->next; }
    }

    JsAstNode* arg = first_arg;
    while (arg) {
        if (arg->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            JsSpreadElementNode* spread = (JsSpreadElementNode*)arg;
            MIR_reg_t src_raw = jm_transpile_box_item(mt, spread->argument);
            jm_emit_error_lane_propagate_check(mt);
            // Generator spill: restore array after yield in spread argument
            if (arr_spill_slot >= 0 && jm_has_yield(mt, spread->argument)) {
                jm_gen_spill_load(mt, array, arr_spill_slot);
            }
            // Convert any iterable to array first
            MIR_reg_t src = jm_callr_1(mt, "js_iterable_to_array", MIR_T_I64, src_raw);
            jm_emit_error_lane_propagate_check(mt);
            // Get length
            MIR_reg_t src_len = jm_callr_1(mt, "js_array_length", MIR_T_I64, src);
            jm_emit_error_lane_propagate_check(mt);
            // Loop: push each element
            MIR_reg_t i_reg = jm_new_reg(mt, "spai", MIR_T_I64);
            jm_emit_reg_op(mt, MIR_MOV, i_reg, MIR_new_int_op(mt->ctx, 0));
            MIR_label_t l_check = jm_new_label(mt);
            MIR_label_t l_end = jm_new_label(mt);
            jm_emit_label(mt, l_check);
            MIR_reg_t cmp = jm_new_reg(mt, "spacmp", MIR_T_I64);
            jm_emit_reg_binary(mt, MIR_LTS, cmp, i_reg, src_len);
            jm_emit_branch(mt, MIR_BF, l_end, cmp);
            // Box through the funnel: an int Item is not a tagged payload, so
            // OR-ing the tag onto a raw index no longer produces that index.
            MIR_reg_t idx_boxed = jm_box_int_reg(mt, i_reg);
            MIR_reg_t elem = jm_callr_2(mt, "js_elements_get", MIR_T_I64, src, idx_boxed);
            jm_emit_error_lane_propagate_check(mt);
            jm_callr_2(mt, "js_array_push", MIR_T_I64, array, elem);
            jm_emit_error_lane_propagate_check(mt);
            jm_emit_reg_binary_op(mt, MIR_ADD, i_reg, i_reg, MIR_new_int_op(mt->ctx, 1));
            jm_emit_jmp(mt, l_check);
            jm_emit_label(mt, l_end);
        } else {
            MIR_reg_t val = jm_transpile_box_item(mt, arg);
            jm_emit_error_lane_propagate_check(mt);
            // Generator spill: restore array after yield in argument
            if (arr_spill_slot >= 0 && jm_has_yield(mt, arg)) {
                jm_gen_spill_load(mt, array, arr_spill_slot);
            }
            jm_callr_2(mt, "js_array_push", MIR_T_I64, array, val);
            jm_emit_error_lane_propagate_check(mt);
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
void jm_emit_error_lane_propagate_check(JsMirTranspiler* mt);

// v30: Helper to create a class method function (non-closure) and mark it strict
