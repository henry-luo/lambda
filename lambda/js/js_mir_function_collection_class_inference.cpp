#include "js_mir_internal.hpp"
#include "../../lib/sort.h"
#include <limits.h>

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

    // Native lowering uses only the stable direct-call path. A mutable `var`
    // function expression remains a boxed runtime call, so it cannot publish
    // a native result descriptor at this site.
    JsFunctionNode* fn = jm_resolve_direct_call_function(mt, call, true);
    if (!fn) return NULL;
    if (fn->is_async) return NULL;

    JsFuncCollected* fc = jm_find_collected_func(mt, fn);
    if (!fc || JM_JS_FACT(fc, native_return_kind) == NATIVE_RETURN_NONE || !fc->native_func_item) return NULL;

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

// Tail recursion is a binding relation: same-spelled local shadows must retain
// their ordinary Call semantics instead of targeting the enclosing function.
bool jm_is_recursive_call(JsCallNode* call, JsFuncCollected* fc) {
    if (!call || !call->callee || !fc || !fc->node || !fc->node->entry) return false;
    if (call->callee->node_type != JS_AST_NODE_IDENTIFIER) return false;
    JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
    return id->entry && id->entry == fc->node->entry;
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
    return JM_JS_FACT(fc, native_return_kind) != NATIVE_RETURN_NONE && fc->native_func_item &&
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

// Extract the semantic binding name for a function parameter.  MIR formals use
// jm_get_backend_param_name; this spelling remains for JS scope semantics.
const char* jm_get_param_name(JsAstNode* param_node, int index) {
    JsIdentifierNode* pid = js_ast_parameter_binding_identifier(param_node);
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
// Function/class collection from the sealed shared AST index
// ============================================================================

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

int jm_indexed_synthetic_field_initializer_count(const AstIndex* index) {
    if (!index) return -1;
    int count = 0;
    for (uint32_t node_id = 0; node_id < index->count; node_id++) {
        AstNode* node = index->nodes[node_id];
        if (!node || node->node_type != JS_AST_NODE_FIELD_DEFINITION) continue;
        JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)node;
        if (field->is_static || !field->key || !field->value ||
                field->value->node_type == JS_AST_NODE_LITERAL) continue;
        if (count == INT_MAX) return -1;
        count++;
    }
    return count;
}

static bool jm_indexed_function_is_strict(JsMirTranspiler* mt,
        JsFunctionNode* function) {
    if (mt->is_global_strict || mt->is_module) return true;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId node_id = ast_index_find(index, (AstNode*)function);
    if (node_id == AST_NODE_ID_INVALID) return false;
    for (AstFunctionId id = index->owner_functions[node_id];
            id != AST_FUNCTION_ID_INVALID; id = index->functions[id].parent) {
        JsFunctionNode* node = (JsFunctionNode*)index->functions[id].node;
        if (node->node_type == JS_AST_NODE_METHOD_DEFINITION ||
                jm_has_use_strict_directive(node)) return true;
    }
    return false;
}

static bool jm_indexed_function_is_direct_field_child(JsMirTranspiler* mt,
        JsFunctionNode* function, JsFieldDefinitionNode* field) {
    AstIndex* index = &mt->tp->ast_index;
    for (AstNode* parent = ast_index_parent(index, (AstNode*)function); parent;
            parent = ast_index_parent(index, parent)) {
        if (parent == (AstNode*)field) return true;
        if (ast_index_node_is_function(parent)) return false;
    }
    return false;
}

JsClassEntry* jm_find_collected_class(JsMirTranspiler* mt,
        JsClassNode* class_node) {
    if (!mt || !class_node || class_node->class_id == AST_CLASS_ID_INVALID ||
            class_node->class_id >= (AstClassId)mt->class_count) return NULL;
    JsClassEntry* entry = &mt->class_entries[class_node->class_id];
    return entry->node == class_node ? entry : NULL;
}

static JsClassEntry* jm_find_class_for_binding_impl(JsMirTranspiler* mt,
        NameEntry* binding, int depth) {
    if (!mt || !binding || !binding->node || depth > 8) return NULL;
    JsAstNode* definition = (JsAstNode*)binding->node;
    if (definition->node_type == JS_AST_NODE_CLASS_DECLARATION ||
            definition->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        return jm_find_collected_class(mt, (JsClassNode*)definition);
    }
    if (definition->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
        JsVariableDeclarationNode* declaration =
            (JsVariableDeclarationNode*)definition;
        for (JsAstNode* item = declaration->declarations; item; item = item->next) {
            if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* candidate =
                (JsVariableDeclaratorNode*)item;
            if (candidate->entry == binding) {
                definition = (JsAstNode*)candidate;
                break;
            }
        }
    }
    if (definition->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) return NULL;
    JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)definition;
    if (!declarator->init) return NULL;
    if (declarator->init->node_type == JS_AST_NODE_CLASS_DECLARATION ||
            declarator->init->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
        return jm_find_collected_class(mt, (JsClassNode*)declarator->init);
    }
    if (declarator->init->node_type == JS_AST_NODE_IDENTIFIER) {
        return jm_find_class_for_binding_impl(mt,
            ((JsIdentifierNode*)declarator->init)->entry, depth + 1);
    }
    return NULL;
}

JsClassEntry* jm_find_class_for_binding(JsMirTranspiler* mt,
        NameEntry* binding) {
    return jm_find_class_for_binding_impl(mt, binding, 0);
}

static bool jm_publish_collected_backend(JsMirTranspiler* mt,
        JsFuncCollected* collected) {
    // Parent/class collection needs this identity before backend lowering (D8.2.4).
    if (!collected || !collected->node) return false;
    if (!collected->node->analysis) collected->node->analysis =
        (FnAnalysis*)pool_calloc(mt->tp->pool, sizeof(FnAnalysis));
    if (!collected->node->analysis) {
        log_error("js-mir: failed to allocate shared function analysis");
        mt->collection_failed = true;
        return false;
    }
    memset(collected->node->analysis, 0, sizeof(FnAnalysis));
    collected->node->analysis->js_mir_backend = collected;
    return true;
}

static JsFuncCollected* jm_collect_class_field_initializer(JsMirTranspiler* mt,
        JsFieldDefinitionNode* field) {
    if (!mt || !field || !field->value ||
        field->value->node_type == JS_AST_NODE_LITERAL) return NULL;
    if (mt->func_count >= mt->func_capacity) {
        log_error("js-mir: class field initializer exceeds indexed capacity at %d of %d",
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

    if (!ast_index_append_profile(&mt->tp->ast_index, (AstNode*)function,
            (AstNode*)field, mt->tp->profile)) {
        log_error("js-mir: failed to index class field initializer");
        mt->collection_failed = true;
        return NULL;
    }

    int function_index = mt->func_count;
    JsFuncCollected* collected = &mt->func_entries[function_index];
    memset(collected, 0, sizeof(JsFuncCollected));
    AstNodeId node_id = ast_index_find(&mt->tp->ast_index, (AstNode*)function);
    collected->function_id = node_id == AST_NODE_ID_INVALID ? AST_FUNCTION_ID_INVALID :
        mt->tp->ast_index.owner_functions[node_id];
    if (collected->function_id == AST_FUNCTION_ID_INVALID) {
        log_error("js-mir: synthetic class field initializer has no function identity");
        mt->collection_failed = true;
        return NULL;
    }
    collected->node = function;
    collected->name = jm_format_name("class_field_initializer_%d_%u",
        function_index, field->source_span.start_byte);
    if (!jm_publish_collected_backend(mt, collected)) return NULL;
    JM_JS_FACT(collected, is_class_field_initializer) = true;
    JM_JS_FACT(collected, is_strict) = true;
    mt->func_count++;

    // Class-field source descendants adopt the synthetic callable parent.
    for (int i = 0; i < function_index; i++) {
        JsFuncCollected* child = &mt->func_entries[i];
        if (!jm_indexed_function_is_direct_field_child(mt, child->node, field)) continue;
        mt->tp->ast_index.functions[child->function_id].parent =
            collected->function_id;
        JM_JS_FACT(child, is_strict) = true;
    }
    return collected;
}

static String* jm_class_method_source_name(JsMirTranspiler* mt,
        JsClassEntry* entry, JsMethodDefinitionNode* method) {
    String* name = jm_class_member_source_name(mt, entry, method->key);
    if (name || !method->key ||
            method->key->node_type != JS_AST_NODE_MEMBER_EXPRESSION) return name;
    JsMemberNode* member = (JsMemberNode*)method->key;
    return member->property && member->property->node_type == JS_AST_NODE_IDENTIFIER
        ? ((JsIdentifierNode*)member->property)->name : NULL;
}

static const char* jm_class_method_backend_name(JsMirTranspiler* mt,
        JsClassEntry* entry, JsMethodDefinitionNode* method, String* method_name,
        int function_index) {
    const char* prefix = method->kind == JsMethodDefinitionNode::JS_METHOD_GET ? "get_"
        : method->kind == JsMethodDefinitionNode::JS_METHOD_SET ? "set_"
        : method->static_method ? "s_" : "";
    JsClassNode* class_node = (JsClassNode*)entry->node;
    if (method_name && class_node->name) {
        return jm_format_name("%.*s_%s%.*s_%d", (int)class_node->name->len,
            class_node->name->chars, prefix, (int)method_name->len,
            method_name->chars, function_index);
    }
    if (method_name) {
        return jm_format_name("anon%d_%s%.*s", function_index, prefix,
            (int)method_name->len, method_name->chars);
    }
    return jm_format_name("class_method_%d_%d",
        (int)(entry - mt->class_entries) + 1, function_index);
}

static bool jm_prepare_indexed_class(JsMirTranspiler* mt, JsClassEntry* entry) {
    JsClassNode* class_node = (JsClassNode*)entry->node;
    if (!class_node->body ||
            class_node->body->node_type != JS_AST_NODE_BLOCK_STATEMENT) {
        log_error("js-mir: indexed class is missing its block body");
        return false;
    }
    JsBlockNode* body = (JsBlockNode*)class_node->body;
    for (JsAstNode* member = body->statements; member; member = member->next) {
        if (member->node_type == JS_AST_NODE_METHOD_DEFINITION &&
                ((JsMethodDefinitionNode*)member)->body) {
            entry->method_capacity++;
        } else if (member->node_type == JS_AST_NODE_FIELD_DEFINITION) {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)member;
            if (field->key && field->is_static) entry->static_field_capacity++;
            else if (field->key) entry->instance_field_capacity++;
        } else if (member->node_type == JS_AST_NODE_STATIC_BLOCK &&
                ((JsStaticBlockNode*)member)->body) {
            entry->static_block_capacity++;
        }
    }
    entry->methods = (JsClassMethodEntry*)pool_calloc(mt->tp->pool,
        (size_t)entry->method_capacity * sizeof(JsClassMethodEntry));
    entry->static_fields = (JsStaticFieldEntry*)pool_calloc(mt->tp->pool,
        (size_t)entry->static_field_capacity * sizeof(JsStaticFieldEntry));
    entry->instance_fields = (JsInstanceFieldEntry*)pool_calloc(mt->tp->pool,
        (size_t)entry->instance_field_capacity * sizeof(JsInstanceFieldEntry));
    entry->static_blocks = (JsAstNode**)pool_calloc(mt->tp->pool,
        (size_t)entry->static_block_capacity * sizeof(JsAstNode*));
    if ((entry->method_capacity && !entry->methods) ||
            (entry->static_field_capacity && !entry->static_fields) ||
            (entry->instance_field_capacity && !entry->instance_fields) ||
            (entry->static_block_capacity && !entry->static_blocks)) {
        log_error("js-mir: failed to allocate class member metadata");
        return false;
    }
    return true;
}

static bool jm_collect_indexed_class_members(JsMirTranspiler* mt,
        JsClassEntry* entry) {
    JsClassNode* class_node = (JsClassNode*)entry->node;
    JsBlockNode* body = (JsBlockNode*)class_node->body;
    for (JsAstNode* member = body->statements; member; member = member->next) {
        if (member->node_type == JS_AST_NODE_FIELD_DEFINITION) {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)member;
            if (field->is_static && field->key) {
                if (entry->static_field_count >= entry->static_field_capacity) return false;
                JsStaticFieldEntry* static_field =
                    &entry->static_fields[entry->static_field_count++];
                static_field->computed = field->computed;
                static_field->key_expr = field->key;
                // D6.2.2v2: retain literal member spellings for initialization.
                static_field->name = !field->computed
                    ? jm_class_member_source_name(mt, entry, field->key) : NULL;
                static_field->initializer = field->value;
                static_field->module_var_index = -1;
                static_field->key_module_var_index = -1;
                log_debug("js-mir: class '%.*s' static field %s'%.*s'",
                    class_node->name ? (int)class_node->name->len : 5,
                    class_node->name ? class_node->name->chars : "anon?",
                    field->computed ? "[computed] " : "",
                    static_field->name ? (int)static_field->name->len : 0,
                    static_field->name ? static_field->name->chars : "");
            } else if (!field->is_static && field->key) {
                if (entry->instance_field_count >= entry->instance_field_capacity) return false;
                JsInstanceFieldEntry* instance_field =
                    &entry->instance_fields[entry->instance_field_count++];
                instance_field->computed = field->computed;
                instance_field->key_expr = field->key;
                instance_field->name = !field->computed
                    ? jm_class_member_source_name(mt, entry, field->key) : NULL;
                instance_field->initializer = field->value;
                instance_field->initializer_fc = jm_collect_class_field_initializer(mt, field);
                instance_field->key_module_var_index = -1;
                if (mt->collection_failed) return false;
                log_debug("js-mir: class '%.*s' instance field %s'%.*s'",
                    class_node->name ? (int)class_node->name->len : 5,
                    class_node->name ? class_node->name->chars : "anon?",
                    field->computed ? "[computed] " : "",
                    instance_field->name ? (int)instance_field->name->len : 0,
                    instance_field->name ? instance_field->name->chars : "");
            }
            continue;
        }
        if (member->node_type == JS_AST_NODE_STATIC_BLOCK) {
            JsStaticBlockNode* block = (JsStaticBlockNode*)member;
            if (block->body && entry->static_block_count < entry->static_block_capacity) {
                entry->static_blocks[entry->static_block_count++] = block->body;
                log_debug("js-mir: class '%.*s' static block #%d",
                    class_node->name ? (int)class_node->name->len : 5,
                    class_node->name ? class_node->name->chars : "anon?",
                    entry->static_block_count);
            }
            continue;
        }
        if (member->node_type != JS_AST_NODE_METHOD_DEFINITION) continue;
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)member;
        if (!method->body || entry->method_count >= entry->method_capacity) continue;
        JsFuncCollected* collected = jm_find_collected_func(mt,
            (JsFunctionNode*)method);
        if (!collected) {
            log_error("js-mir: indexed class method has no callable entry");
            return false;
        }
        int function_index = (int)(collected - mt->func_entries);
        String* method_name = jm_class_method_source_name(mt, entry, method);
        collected->name = jm_class_method_backend_name(mt, entry, method,
            method_name, function_index);
        JM_JS_FACT(collected, is_class_method) = true;

        JsClassMethodEntry* method_entry = &entry->methods[entry->method_count++];
        method_entry->name = method_name;
        method_entry->fc = collected;
        method_entry->param_count = ast_linked_node_count(
            ((JsFunctionNode*)method)->params);
        JsAstNode* last_param = NULL;
        for (JsAstNode* param = ((JsFunctionNode*)method)->params; param;
                param = param->next) last_param = param;
        if (last_param && (last_param->node_type == JS_AST_NODE_REST_ELEMENT ||
                last_param->node_type == JS_AST_NODE_SPREAD_ELEMENT)) {
            method_entry->param_count = -method_entry->param_count;
        }
        method_entry->is_static = method->static_method;
        method_entry->is_getter = method->kind == JsMethodDefinitionNode::JS_METHOD_GET;
        method_entry->is_setter = method->kind == JsMethodDefinitionNode::JS_METHOD_SET;
        method_entry->computed = method->computed;
        method_entry->key_expr = method->key;
        method_entry->is_constructor = !method_entry->is_static &&
            !method_entry->computed && method_name && method_name->len == 11 &&
            strncmp(method_name->chars, "constructor", 11) == 0;
        if (method_entry->is_constructor) {
            entry->constructor = method_entry;
            JM_JS_FACT(collected, is_constructor) = true;
            JM_JS_FACT(collected, is_derived_constructor) = class_node->superclass != NULL;
        }
    }
    return true;
}

static void jm_collect_indexed_class_aliases(JsMirTranspiler* mt) {
    AstIndex* index = &mt->tp->ast_index;
    for (uint32_t node_id = 0; node_id < index->count; node_id++) {
        JsAstNode* node = (JsAstNode*)index->nodes[node_id];
        if (!node) continue;
        if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)node;
            if (!decl->id || decl->id->node_type != JS_AST_NODE_IDENTIFIER || !decl->init) continue;
            JsIdentifierNode* binding = (JsIdentifierNode*)decl->id;
            if (!binding->name) continue;
            if (decl->init->node_type == JS_AST_NODE_CLASS_DECLARATION ||
                    decl->init->node_type == JS_AST_NODE_CLASS_EXPRESSION) {
                JsClassEntry* entry = jm_find_collected_class(mt, (JsClassNode*)decl->init);
                if (!entry) continue;
                if (!entry->name) entry->name = binding->name;
                else if (entry->name->len != binding->name->len ||
                        strncmp(entry->name->chars, binding->name->chars,
                            entry->name->len) != 0) entry->alias_name = binding->name;
                continue;
            }
            if (decl->init->node_type != JS_AST_NODE_IDENTIFIER) continue;
            JsIdentifierNode* source = (JsIdentifierNode*)decl->init;
            JsClassEntry* entry = jm_find_class_for_binding(mt, source->entry);
            if (entry && !entry->alias_name) entry->alias_name = binding->name;
        } else if (node->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
            JsAssignmentNode* assignment = (JsAssignmentNode*)node;
            if (!assignment->left || assignment->left->node_type != JS_AST_NODE_IDENTIFIER ||
                    !assignment->right || (assignment->right->node_type !=
                    JS_AST_NODE_CLASS_DECLARATION && assignment->right->node_type !=
                    JS_AST_NODE_CLASS_EXPRESSION)) continue;
            JsIdentifierNode* binding = (JsIdentifierNode*)assignment->left;
            JsClassEntry* entry = binding->name
                ? jm_find_collected_class(mt, (JsClassNode*)assignment->right) : NULL;
            if (entry && !entry->alias_name) entry->alias_name = binding->name;
        }
    }
}

static void jm_assign_indexed_class_facts(JsMirTranspiler* mt) {
    AstIndex* index = &mt->tp->ast_index;
    for (int function_index = 0; function_index < mt->func_count; function_index++) {
        JsFuncCollected* function = &mt->func_entries[function_index];
        JM_JS_FACT(function, owner_class_id) = ast_index_nearest_class(index,
            ast_index_find(index, (AstNode*)function->node), false);
    }
    for (int class_index = 0; class_index < mt->class_count; class_index++) {
        JsClassEntry* entry = &mt->class_entries[class_index];
        JsClassNode* class_node = (JsClassNode*)entry->node;
        if (!class_node->superclass) continue;
        // Class heritage expressions execute in the class's strict realm.
        for (int function_index = 0; function_index < mt->func_count; function_index++) {
        JsFuncCollected* function = &mt->func_entries[function_index];
        if (ast_index_node_descends(index,
                ast_index_find(index, (AstNode*)function->node),
                ast_index_find(index, class_node->superclass))) {
            JM_JS_FACT(function, is_strict) = true;
        }
        }
    }
}

static int jm_indexed_function_postorder_cmp(const void* left, const void* right,
        void* opaque) {
    AstIndex* index = (AstIndex*)opaque;
    uint32_t left_id = *(const uint32_t*)left;
    uint32_t right_id = *(const uint32_t*)right;
    AstNode* left_node = index->functions[left_id].node;
    AstNode* right_node = index->functions[right_id].node;
    if (left_node->source_span.end_byte != right_node->source_span.end_byte) {
        return left_node->source_span.end_byte < right_node->source_span.end_byte ? -1 : 1;
    }
    // An equal end position belongs to an enclosing function; visit its
    // shorter nested source range first to retain post-order lowering.
    if (left_node->source_span.start_byte != right_node->source_span.start_byte) {
        return left_node->source_span.start_byte > right_node->source_span.start_byte ? -1 : 1;
    }
    return left_id < right_id ? -1 : left_id > right_id;
}

void jm_collect_indexed_functions(JsMirTranspiler* mt) {
    if (!mt || !mt->tp || mt->collection_failed) return;
    AstIndex* index = &mt->tp->ast_index;
    uint32_t source_function_count = index->function_count;
    if (source_function_count > (uint32_t)mt->func_capacity ||
            index->class_count != (uint32_t)mt->class_capacity) {
        log_error("js-mir: indexed function/class capacity disagreement");
        mt->collection_failed = true;
        return;
    }

    uint32_t* source_order = (uint32_t*)pool_calloc(mt->tp->pool,
        (size_t)source_function_count * sizeof(uint32_t));
    if (source_function_count && !source_order) {
        log_error("js-mir: failed to allocate indexed function order");
        mt->collection_failed = true;
        return;
    }
    for (uint32_t function_id = 0; function_id < source_function_count; function_id++) {
        source_order[function_id] = function_id;
    }
    // FunctionId remains the source identity; source spans reconstruct the
    // existing lexical post-order without a second recursive AST traversal.
    sort_qsort_r(source_order, source_function_count, sizeof(uint32_t),
        jm_indexed_function_postorder_cmp, index);
    for (uint32_t order_index = 0; order_index < source_function_count; order_index++) {
        JsFunctionNode* function = (JsFunctionNode*)
            index->functions[source_order[order_index]].node;
        JsFuncCollected* collected = &mt->func_entries[mt->func_count++];
        memset(collected, 0, sizeof(JsFuncCollected));
        collected->node = function;
        collected->function_id = source_order[order_index];
        collected->name = jm_make_fn_name(function, mt);
    }
    for (int function_index = 0; function_index < mt->func_count; function_index++) {
        JsFuncCollected* function = &mt->func_entries[function_index];
        if (!jm_publish_collected_backend(mt, function)) return;
        JM_JS_FACT(function, is_strict) = jm_indexed_function_is_strict(mt,
            function->node);
    }
    mt->class_count = mt->class_capacity;
    for (int class_index = 0; class_index < mt->class_count; class_index++) {
        JsClassEntry* entry = &mt->class_entries[class_index];
        memset(entry, 0, sizeof(JsClassEntry));
        entry->node = (JsClassNode*)index->classes[class_index];
        entry->name = entry->node->name;
        entry->is_declaration = entry->node->node_type == JS_AST_NODE_CLASS_DECLARATION;
        entry->inner_module_var_index = -1;
        if (!jm_prepare_indexed_class(mt, entry)) {
            mt->collection_failed = true;
            return;
        }
    }
    for (int class_index = 0; class_index < mt->class_count; class_index++) {
        if (!jm_collect_indexed_class_members(mt, &mt->class_entries[class_index])) {
            mt->collection_failed = true;
            return;
        }
    }
    jm_collect_indexed_class_aliases(mt);
    jm_assign_indexed_class_facts(mt);
}

// ============================================================================
// Find collected function entry through the shared AST identity index.
// ============================================================================

JsFuncCollected* jm_find_collected_func(JsMirTranspiler*, JsFunctionNode* fn) {
    return fn && fn->analysis
        ? (JsFuncCollected*)fn->analysis->js_mir_backend : NULL;
}

// Annex B §B.3.3.1: Check if enclosing function has a parameter whose name
// matches the given identifier.  When it does, the block-scoped function
// declaration must NOT overwrite the parameter binding.
bool jm_func_has_param_named(JsFunctionNode* fn, const char* name, int name_len) {
    if (!fn || !fn->params) return false;
    for (JsAstNode* p = fn->params; p; p = p->next) {
        JsIdentifierNode* pid = js_ast_parameter_binding_identifier(p);
        if (pid && pid->name &&
            (int)pid->name->len == name_len &&
            memcmp(pid->name->chars, name, name_len) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Phase 4: Parameter and return type inference
// ============================================================================

typedef struct JmBigIntLiteralScan {
    AstFunctionId owner;
    bool found;
} JmBigIntLiteralScan;

static bool jm_scan_indexed_bigint_literal(const AstIndex* index,
        AstNodeId node_id, void* opaque) {
    JmBigIntLiteralScan* scan = (JmBigIntLiteralScan*)opaque;
    if (index->owner_functions[node_id] != scan->owner) return true;
    JsAstNode* node = (JsAstNode*)index->nodes[node_id];
    if (!node || node->node_type != JS_AST_NODE_LITERAL) return true;
    JsLiteralNode* literal = (JsLiteralNode*)node;
    scan->found = literal->literal_type == JS_LITERAL_NUMBER && literal->is_bigint;
    return !scan->found;
}

// The sealed index owns child traversal and excludes nested function bodies.
// This catches every expression shape, rather than maintaining a second list.
static bool jm_indexed_expr_has_bigint_literal(JsMirTranspiler* mt,
        JsAstNode* root) {
    AstIndex* index = mt && mt->tp ? &mt->tp->ast_index : NULL;
    AstNodeId root_id = index ? ast_index_find(index, (AstNode*)root) :
        AST_NODE_ID_INVALID;
    if (!index || root_id == AST_NODE_ID_INVALID) return false;
    JmBigIntLiteralScan scan = {index->owner_functions[root_id], false};
    ast_index_visit_subtree(index, root_id, jm_scan_indexed_bigint_literal, &scan);
    return scan.found;
}

// Parameter and direct-body alias evidence is keyed by the binding resolved by
// the direct scope pass.  Source spelling is not enough: an inner `let x`
// must not contribute evidence to an outer `x` parameter or alias.
typedef struct JmParamInferenceBinding {
    NameEntry* entry;
    int param_index;
} JmParamInferenceBinding;

static int jm_infer_find_param(JsAstNode* node,
        const JmParamInferenceBinding bindings[], int binding_count) {
    if (!node || node->node_type != JS_AST_NODE_IDENTIFIER) return -1;
    JsIdentifierNode* id = (JsIdentifierNode*)node;
    if (!id->entry) return -1;
    for (int i = 0; i < binding_count; i++) {
        if (bindings[i].entry == id->entry) return bindings[i].param_index;
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
static void jm_infer_indexed_node(JsMirTranspiler* mt, JsAstNode* node,
        const JmParamInferenceBinding bindings[], FnParamEvidence* evidence,
        int binding_count, int param_count, const char* self_name) {
    if (!node) return;
    switch (node->node_type) {
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        int li = jm_infer_find_param(bin->left, bindings, binding_count);
        int ri = jm_infer_find_param(bin->right, bindings, binding_count);
        bool arithmetic = bin->op == JS_OP_SUB || bin->op == JS_OP_MUL ||
            bin->op == JS_OP_DIV || bin->op == JS_OP_MOD || bin->op == JS_OP_EXP;
        bool comparison = bin->op == JS_OP_LT || bin->op == JS_OP_LE ||
            bin->op == JS_OP_GT || bin->op == JS_OP_GE || bin->op == JS_OP_EQ ||
            bin->op == JS_OP_NE || bin->op == JS_OP_STRICT_EQ || bin->op == JS_OP_STRICT_NE;
        bool bitwise = bin->op == JS_OP_BIT_AND || bin->op == JS_OP_BIT_OR ||
            bin->op == JS_OP_BIT_XOR || bin->op == JS_OP_BIT_LSHIFT ||
            bin->op == JS_OP_BIT_RSHIFT || bin->op == JS_OP_BIT_URSHIFT;
        if (arithmetic || (bin->op == JS_OP_ADD && self_name && li >= 0 && ri >= 0)) {
            if (li >= 0 && jm_indexed_expr_has_bigint_literal(mt, bin->right)) evidence[li].compared_with_non_numeric = true;
            if (ri >= 0 && jm_indexed_expr_has_bigint_literal(mt, bin->left)) evidence[ri].compared_with_non_numeric = true;
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
        int index = jm_infer_find_param(unary->operand, bindings, binding_count);
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
                    int index = jm_infer_find_param(arg, bindings, binding_count);
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
            int index = jm_infer_find_param(member->object, bindings, binding_count);
            if (index >= 0) evidence[index].used_as_container = true;
        }
        break;
    }
    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        int left = jm_infer_find_param(assignment->left, bindings, binding_count);
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
        int right = jm_infer_find_param(assignment->right, bindings, binding_count);
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
        const JmParamInferenceBinding bindings[], FnParamEvidence* evidence,
        int binding_count, int param_count, const char* self_name) {
    if (!mt || !mt->tp || !fn) return;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId fn_id = ast_index_find(index, (AstNode*)fn);
    AstFunctionId owner = fn_id == AST_NODE_ID_INVALID
        ? AST_FUNCTION_ID_INVALID : index->owner_functions[fn_id];
    if (owner == AST_FUNCTION_ID_INVALID) return;
    for (uint32_t i = 0; i < index->count; i++) {
        if (index->owner_functions[i] == owner)
            jm_infer_indexed_node(mt, (JsAstNode*)index->nodes[i], bindings,
                evidence, binding_count, param_count, self_name);
    }
}

// Infer parameter types for a collected function from body usage patterns.
void jm_infer_param_types(JsMirTranspiler* mt, JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;
    int pc = ast_linked_node_count(fn->params);
    JM_PARAM_COUNT(fc) = pc;
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

    JsAstParameterFacts parameter_facts =
        js_ast_collect_parameter_facts(fn->params);
    JM_JS_FACT(fc, formal_length) = parameter_facts.formal_length;
    JM_JS_FACT(fc, has_default_params) = parameter_facts.has_default_params;
    JM_JS_FACT(fc, has_duplicate_param_names) =
        parameter_facts.has_duplicate_param_names;
    JM_JS_FACT(fc, has_rest_param) = parameter_facts.has_rest_param;
    JM_JS_FACT(fc, has_non_simple_params) = parameter_facts.has_non_simple_params;

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
                if (tsp->declared_type) ann_count++;
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
                    if (tsp->declared_type && !tsp->optional) {
                        TypeId tid = LMD_TYPE_ANY;
                        TypeId declared = tsp->declared_type->type_id;
                        if (declared == LMD_TYPE_FLOAT || declared == LMD_TYPE_INT ||
                                declared == LMD_TYPE_STRING || declared == LMD_TYPE_BOOL) {
                            tid = declared;
                        }
                        jm_set_param_type(fc, i, tid);
                    } else {
                        jm_set_param_type(fc, i, LMD_TYPE_ANY);
                    }
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

    // One indexed inference pass covers formals and their direct-body aliases.
    // The prior spelling-based alias re-walk confused shadowed bindings and
    // retained a second full body scan after identity publication.
    JmParamInferenceBinding* inference_bindings =
        (JmParamInferenceBinding*)mem_calloc((size_t)pc * 2,
            sizeof(*inference_bindings), MEM_CAT_JS_RUNTIME);
    FnParamEvidence* evidence = (FnParamEvidence*)mem_calloc((size_t)pc,
        sizeof(*evidence), MEM_CAT_JS_RUNTIME);
    if (!inference_bindings || !evidence) {
        if (inference_bindings) mem_free(inference_bindings);
        if (evidence) mem_free(evidence);
        log_error("js-mir: inference scratch allocation failed for %d formals", pc);
        return;
    }
    JsAstNode* p = fn->params;
    for (int i = 0; i < pc && p; i++, p = p->next) {
        JsIdentifierNode* binding = js_ast_parameter_binding_identifier(p);
        inference_bindings[i] = {binding ? binding->entry : NULL, i};
    }
    int inference_binding_count = pc;

    if (jm_indexed_expr_has_bigint_literal(mt, fn->body)) {
        for (int i = 0; i < pc; i++) {
            jm_set_param_type(fc, i, LMD_TYPE_ANY);
        }
        log_debug("js-mir P4: boxed params for %s because body uses BigInt literals", fc->name);
        mem_free(inference_bindings);
        mem_free(evidence);
        return;
    }

    // Build self-name for recursive call detection
    const char* self_name = NULL;
    if (fn->name) {
        self_name = jm_var_name(fn->name);
    }

    JsBlockNode* body_blk = fn->body &&
        fn->body->node_type == JS_AST_NODE_BLOCK_STATEMENT
        ? (JsBlockNode*)fn->body : NULL;
    for (JsAstNode* stmt = body_blk ? body_blk->statements : NULL;
            stmt && inference_binding_count < pc * 2; stmt = stmt->next) {
        if (stmt->node_type != JS_AST_NODE_VARIABLE_DECLARATION) continue;
        for (JsAstNode* decl = ((JsVariableDeclarationNode*)stmt)->declarations;
                decl && inference_binding_count < pc * 2; decl = decl->next) {
            if (decl->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* alias = (JsVariableDeclaratorNode*)decl;
            if (!alias->id || alias->id->node_type != JS_AST_NODE_IDENTIFIER ||
                    !alias->init || alias->init->node_type != JS_AST_NODE_IDENTIFIER) continue;
            int param_index = jm_infer_find_param(alias->init, inference_bindings, pc);
            JsIdentifierNode* alias_id = (JsIdentifierNode*)alias->id;
            if (param_index < 0 || !alias_id->entry) continue;
            inference_bindings[inference_binding_count++] = {alias_id->entry, param_index};
        }
    }

    // Accumulate formal and alias evidence once through the sealed index.
    jm_infer_indexed(mt, fn, inference_bindings, evidence,
        inference_binding_count, pc, self_name && self_name[0] ? self_name : NULL);

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
    mem_free(inference_bindings);
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
static void jm_collect_return_type(JsMirTranspiler* mt, JsAstNode* node, const char* self_name,
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
            if (!jm_indexed_expr_has_bigint_literal(mt, expr) &&
                    (jm_add_chain_has_string(bin->left) || jm_add_chain_has_string(bin->right)))
                t = LMD_TYPE_STRING;
            break;
        case JS_OP_SUB: case JS_OP_MUL: case JS_OP_MOD: case JS_OP_DIV: case JS_OP_EXP:
            t = jm_indexed_expr_has_bigint_literal(mt, expr) ? LMD_TYPE_ANY : LMD_TYPE_FLOAT;
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
    if (fn->declared_return_type) {
        TypeId declared = fn->declared_return_type->type_id;
        if (declared == LMD_TYPE_FLOAT || declared == LMD_TYPE_INT ||
                declared == LMD_TYPE_STRING || declared == LMD_TYPE_BOOL) {
            JM_JS_FACT(fc, return_type) = declared;
            log_debug("js-mir P3.4: annotation-based return type for %s: %s",
                fn->name ? fn->name->chars : "(anon)",
                JM_JS_FACT(fc, return_type) == LMD_TYPE_INT ? "INT" : JM_JS_FACT(fc, return_type) == LMD_TYPE_FLOAT ? "FLOAT" : "ANY");
            return;
        }
    }

    if (jm_indexed_expr_has_bigint_literal(mt, fn->body)) {
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
            jm_collect_return_type(mt, index->nodes[i], return_self_name, fc,
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
                !ast_index_node_descends(index, i, fn_id)) continue;
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

static bool jm_prescan_is_float_array(struct hashmap* float_arrays,
        const char* name) {
    JsNameSetEntry key;
    memset(&key, 0, sizeof(key));
    key.name = jm_persist_name(name);
    return hashmap_get(float_arrays, &key) != NULL;
}

// This prepass used to recursively rediscover only the body shapes it knew
// about. Keep its deliberately narrow binary/unary evidence rules, but read
// the traversal and function boundary from the sealed AstIndex (D8.2.4).
static bool jm_prescan_expression_path_is_numeric(AstIndex* index,
        AstNodeId node_id, AstNodeId root_id, AstFunctionId owner) {
    if (!index || node_id == AST_NODE_ID_INVALID || root_id == AST_NODE_ID_INVALID ||
            index->owner_functions[node_id] != owner) return false;
    while (node_id != root_id) {
        AstNodeId parent_id = ast_index_parent_id(index, node_id);
        AstNode* parent = parent_id < index->count ? index->nodes[parent_id] : NULL;
        if (parent_id == AST_NODE_ID_INVALID ||
                (parent->node_type != JS_AST_NODE_BINARY_EXPRESSION &&
                 parent->node_type != JS_AST_NODE_UNARY_EXPRESSION)) return false;
        node_id = parent_id;
    }
    return true;
}

static bool jm_prescan_node_has_float_hint(JsAstNode* node) {
    if (!node) return false;
    if (node->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        if (lit->literal_type != JS_LITERAL_NUMBER) return false;
        return lit->has_decimal || lit->value.number_value !=
            (double)(long long)lit->value.number_value;
    }
    if (node->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        return bin->op == JS_OP_DIV || bin->op == JS_OP_MOD;
    }
    if (node->node_type != JS_AST_NODE_IDENTIFIER) return false;
    String* name = ((JsIdentifierNode*)node)->name;
    return name && ((name->len == 3 && strncmp(name->chars, "NaN", 3) == 0) ||
        (name->len == 8 && strncmp(name->chars, "Infinity", 8) == 0));
}

static bool jm_prescan_expression_has_float_hint(JsMirTranspiler* mt,
        JsAstNode* root) {
    AstIndex* index = mt && mt->tp ? &mt->tp->ast_index : NULL;
    AstNodeId root_id = index ? ast_index_find(index, (AstNode*)root) :
        AST_NODE_ID_INVALID;
    if (!index || root_id == AST_NODE_ID_INVALID) return false;
    AstFunctionId owner = index->owner_functions[root_id];
    for (uint32_t i = 0; i < index->count; i++) {
        if (jm_prescan_node_has_float_hint((JsAstNode*)index->nodes[i]) &&
                jm_prescan_expression_path_is_numeric(index, i, root_id, owner)) {
            return true;
        }
    }
    return false;
}

static bool jm_prescan_expression_has_float_array_access(JsMirTranspiler* mt,
        JsAstNode* root, struct hashmap* float_arrays) {
    AstIndex* index = mt && mt->tp ? &mt->tp->ast_index : NULL;
    AstNodeId root_id = index ? ast_index_find(index, (AstNode*)root) :
        AST_NODE_ID_INVALID;
    if (!index || root_id == AST_NODE_ID_INVALID) return false;
    AstFunctionId owner = index->owner_functions[root_id];
    for (uint32_t i = 0; i < index->count; i++) {
        JsAstNode* node = (JsAstNode*)index->nodes[i];
        if (!node || node->node_type != JS_AST_NODE_MEMBER_EXPRESSION ||
                !jm_prescan_expression_path_is_numeric(index, i, root_id, owner)) continue;
        JsMemberNode* member = (JsMemberNode*)node;
        if (!member->computed || !member->object ||
                member->object->node_type != JS_AST_NODE_IDENTIFIER) continue;
        JsIdentifierNode* object = (JsIdentifierNode*)member->object;
        const char* name = jm_format_name("%.*s", (int)object->name->len,
            object->name->chars);
        if (jm_prescan_is_float_array(float_arrays, name)) return true;
    }
    return false;
}

static bool jm_prescan_body_path_is_reachable(AstIndex* index,
        AstNodeId node_id, AstNodeId root_id, AstFunctionId owner) {
    if (!index || node_id == AST_NODE_ID_INVALID || root_id == AST_NODE_ID_INVALID ||
            index->owner_functions[node_id] != owner) return false;
    while (node_id != root_id) {
        AstNode* child = index->nodes[node_id];
        AstNodeId parent_id = ast_index_parent_id(index, node_id);
        AstNode* parent = parent_id < index->count ? index->nodes[parent_id] : NULL;
        if (parent_id == AST_NODE_ID_INVALID) return false;
        if (parent_id == root_id) return true;
        bool reaches_child = parent->node_type == JS_AST_NODE_BLOCK_STATEMENT ||
            (parent->node_type == AST_NODE_LOOP &&
                ((AstLoopControlNode*)parent)->body == child) ||
            (parent->node_type == JS_AST_NODE_IF_STATEMENT &&
                (((JsIfNode*)parent)->consequent == child ||
                 ((JsIfNode*)parent)->alternate == child)) ||
            (parent->node_type == JS_AST_NODE_EXPRESSION_STATEMENT &&
                ((JsExpressionStatementNode*)parent)->expression == child) ||
            (parent->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION &&
                ((JsAssignmentNode*)parent)->right == child &&
                child->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION);
        if (!reaches_child) return false;
        node_id = parent_id;
    }
    return true;
}

static void jm_prescan_add_widened_name(struct hashmap* widen_vars,
        JsIdentifierNode* id, const char* suffix) {
    if (!widen_vars || !id || !id->name) return;
    const char* name = jm_format_name("%.*s", (int)id->name->len,
        id->name->chars);
    jm_name_set_add(widen_vars, name);
    log_debug("P9: indexed prescan widen '%s'%s", name, suffix);
}

static bool jm_prescan_is_float_array_declarator(JsVariableDeclaratorNode* decl) {
    if (!decl || !decl->id || decl->id->node_type != JS_AST_NODE_IDENTIFIER ||
            !decl->init || decl->init->node_type != JS_AST_NODE_NEW_EXPRESSION) return false;
    JsCallNode* construct = (JsCallNode*)decl->init;
    if (!construct->callee || construct->callee->node_type != JS_AST_NODE_IDENTIFIER) return false;
    String* name = ((JsIdentifierNode*)construct->callee)->name;
    return name && name->len == 12 &&
        (strncmp(name->chars, "Float16Array", 12) == 0 ||
         strncmp(name->chars, "Float32Array", 12) == 0 ||
         strncmp(name->chars, "Float64Array", 12) == 0);
}

// Pre-scan one indexed body for float typed arrays and variables requiring
// FLOAT storage before native lowering publishes their MIR registers.
void jm_prescan_float_widening(JsMirTranspiler* mt, JsAstNode* body) {
    if (!mt || !mt->tp || !body) return;
    AstIndex* index = &mt->tp->ast_index;
    AstNodeId root_id = ast_index_find(index, (AstNode*)body);
    if (root_id == AST_NODE_ID_INVALID) return;
    AstFunctionId owner = index->owner_functions[root_id];

    // Step 1: Find all Float32Array/Float64Array variable names
    struct hashmap* float_arrays = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
        jm_name_hash, jm_name_cmp, NULL, NULL);
    if (!float_arrays) return;

    // This intentionally remains a direct-body declaration scan. The old
    // pass did not infer aliases or nested declarations, so indexed migration
    // must not broaden the native specialization policy.
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (!node || index->owner_functions[i] != owner ||
                ast_index_parent_id(index, i) != root_id ||
                node->node_type != JS_AST_NODE_VARIABLE_DECLARATION) continue;
        for (JsAstNode* item = ((JsVariableDeclarationNode*)node)->declarations;
                item; item = item->next) {
            if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR ||
                    !jm_prescan_is_float_array_declarator((JsVariableDeclaratorNode*)item)) continue;
            JsIdentifierNode* id = (JsIdentifierNode*)((JsVariableDeclaratorNode*)item)->id;
            const char* name = jm_format_name("%.*s", (int)id->name->len,
                id->name->chars);
            jm_name_set_add(float_arrays, name);
            log_debug("P9: indexed prescan found float typed array '%s'", name);
        }
    }

    // Step 2: consume the same body shapes through indexed parent links.
    if (!mt->widen_to_float) {
        mt->widen_to_float = hashmap_new(sizeof(JsNameSetEntry), 16, 0, 0,
            jm_name_hash, jm_name_cmp, NULL, NULL);
    }
    if (mt->widen_to_float) {
        for (uint32_t i = 0; i < index->count; i++) {
            JsAstNode* node = (JsAstNode*)index->nodes[i];
            if (!node || !jm_prescan_body_path_is_reachable(index, i, root_id, owner)) continue;
            if (node->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
                JsAssignmentNode* assignment = (JsAssignmentNode*)node;
                if (!assignment->left ||
                        assignment->left->node_type != JS_AST_NODE_IDENTIFIER) continue;
                bool float_array_access = jm_prescan_expression_has_float_array_access(
                    mt, assignment->right, float_arrays);
                bool float_hint = jm_prescan_expression_has_float_hint(mt,
                    assignment->right);
                bool compound_float_hint = assignment->op == JS_OP_ADD_ASSIGN ||
                    assignment->op == JS_OP_SUB_ASSIGN ||
                    assignment->op == JS_OP_MUL_ASSIGN;
                if (float_array_access || assignment->op == JS_OP_DIV_ASSIGN ||
                        ((assignment->op == JS_OP_ASSIGN || compound_float_hint) &&
                         float_hint)) {
                    jm_prescan_add_widened_name(mt->widen_to_float,
                        (JsIdentifierNode*)assignment->left, "");
                }
            } else if (node->node_type == JS_AST_NODE_VARIABLE_DECLARATION) {
                for (JsAstNode* item = ((JsVariableDeclarationNode*)node)->declarations;
                        item; item = item->next) {
                    if (item->node_type != JS_AST_NODE_VARIABLE_DECLARATOR) continue;
                    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)item;
                    if (!decl->id || decl->id->node_type != JS_AST_NODE_IDENTIFIER ||
                            !decl->init) continue;
                    if (jm_prescan_expression_has_float_array_access(mt, decl->init,
                            float_arrays) || jm_prescan_expression_has_float_hint(mt,
                            decl->init)) {
                        jm_prescan_add_widened_name(mt->widen_to_float,
                            (JsIdentifierNode*)decl->id, " (var decl)");
                    }
                }
            }
        }
    }

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

// ============================================================================
// Expression transpilers - each returns MIR_reg_t holding boxed Item result
// ============================================================================

// Forward declarations for transpiler functions defined later
MIR_reg_t jm_build_closure_for_method(JsMirTranspiler* mt, JsFuncCollected* fc, int param_count);
void jm_transpile_switch(JsMirTranspiler* mt, JsSwitchNode* sw);
void jm_transpile_do_while(JsMirTranspiler* mt, JsDoWhileNode* dw);
void jm_transpile_for_of(JsMirTranspiler* mt, JsForOfNode* fo);
void jm_scope_env_reload_vars(JsMirTranspiler* mt);
void jm_env_reload_shared_captures(JsMirTranspiler* mt);
void jm_emit_error_lane_propagate_check(JsMirTranspiler* mt);

// v30: Helper to create a class method function (non-closure) and mark it strict
