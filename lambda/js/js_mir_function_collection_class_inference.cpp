#include "js_mir_internal.hpp"
#include "../../lib/sort.h"
#include <limits.h>

// ============================================================================
// Phase 4: Native call resolution
// ============================================================================

JsFunctionNode* jm_resolve_direct_call_function(JsMirTranspiler* mt,
        JsCallNode* call, bool stable) {
    if (!call->callee || call->callee->node_type != AST_NODE_IDENT) return NULL;
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
    if (ntype == AST_NODE_FUNC) {
        fn = (JsFunctionNode*)definition;
    } else if (ntype == AST_NODE_VARIABLE_DECLARATOR) {
        JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)definition;
        if (decl->init && (decl->init->node_type == AST_NODE_FUNC_EXPR
            || decl->init->node_type == AST_NODE_ARROW_FUNC)) {
            fn = (JsFunctionNode*)decl->init;
        }
    }
    if (stable && ntype == AST_NODE_FUNC &&
            !jm_function_decl_is_direct_binding((JsFunctionNode*)definition, false)) return NULL;
    if (stable && ntype == AST_NODE_VARIABLE_DECLARATOR &&
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
    if (call->callee && call->callee->node_type == AST_NODE_MEMBER_EXPR) {
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

bool jm_call_result_uses_native_register(JsMirTranspiler* mt, JsCallNode* call, JsFuncCollected* fc) {
    if (!mt || !call || !fc) return false;
    // A known native body is not enough: an unmatched direct call is lowered
    // through the boxed entry, whose slow lane can return any JavaScript value.
    // Reporting the inferred raw return here would make its caller unbox an
    // already boxed string/object result.
    return JM_JS_FACT(fc, native_return_kind) != NATIVE_RETURN_NONE &&
        JM_JS_FACT(fc, native_return_kind) != NATIVE_RETURN_ITEM && fc->native_func_item &&
        jm_resolve_native_call(mt, call) == fc;
}

// ============================================================================
// Local function management
// ============================================================================

void jm_register_local_func(JsMirTranspiler* mt, const char* name, MIR_item_t func_item) {
    JsLocalFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.name = mir_em_persist_cstr(&mt->func_em->em, name).str;
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
        strbuf_append_int(sb, mt->func_em->em.label_counter++);
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
    if (key->node_type == AST_NODE_IDENT) {
        return jm_class_private_name(mt, owner,
            ((JsIdentifierNode*)key)->name);
    }
    if (key->node_type != AST_NODE_LITERAL) return NULL;
    JsLiteralNode* literal = (JsLiteralNode*)key;
    if (literal->literal_type == AST_LITERAL_STRING) {
        return literal->value.string_value;
    }
    if (literal->literal_type == AST_LITERAL_NUMBER) {
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
        if (!node || node->node_type != AST_NODE_FIELD) continue;
        JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)node;
        if (field->is_static || !field->key || !field->value ||
                field->value->node_type == AST_NODE_LITERAL) continue;
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
        if (node->node_type == AST_NODE_METHOD ||
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
    if (definition->node_type == AST_NODE_CLASS ||
            definition->node_type == AST_NODE_CLASS_EXPR) {
        return jm_find_collected_class(mt, (JsClassNode*)definition);
    }
    if (definition->node_type == AST_NODE_VAR_STAM) {
        JsVariableDeclarationNode* declaration =
            (JsVariableDeclarationNode*)definition;
        for (JsAstNode* item = declaration->declarations; item; item = item->next) {
            if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* candidate =
                (JsVariableDeclaratorNode*)item;
            if (candidate->entry == binding) {
                definition = (JsAstNode*)candidate;
                break;
            }
        }
    }
    if (definition->node_type != AST_NODE_VARIABLE_DECLARATOR) return NULL;
    JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)definition;
    if (!declarator->init) return NULL;
    if (declarator->init->node_type == AST_NODE_CLASS ||
            declarator->init->node_type == AST_NODE_CLASS_EXPR) {
        return jm_find_collected_class(mt, (JsClassNode*)declarator->init);
    }
    if (declarator->init->node_type == AST_NODE_IDENT) {
        return jm_find_class_for_binding_impl(mt,
            ((JsIdentifierNode*)declarator->init)->entry, depth + 1);
    }
    return NULL;
}

JsClassEntry* jm_find_class_for_binding(JsMirTranspiler* mt,
        NameEntry* binding) {
    return jm_find_class_for_binding_impl(mt, binding, 0);
}

static bool jm_publish_collected_artifact(JsMirTranspiler* mt,
        JsFuncCollected* collected) {
    // FunctionId is the only source-to-backend edge (D8.2.4); the post-order
    // collection array is not a node-owned identity cache.
    if (!mt || !collected || !collected->node ||
            collected->function_id == AST_FUNCTION_ID_INVALID ||
            collected->function_id >= (AstFunctionId)mt->func_capacity ||
            !mt->func_entries_by_id) return false;
    if (mt->func_entries_by_id[collected->function_id] &&
            mt->func_entries_by_id[collected->function_id] != collected) {
        log_error("js-mir: duplicate FunctionId artifact %u",
            collected->function_id);
        return false;
    }
    mt->func_entries_by_id[collected->function_id] = collected;
    if (!collected->node->analysis) collected->node->analysis =
        (FnAnalysis*)pool_calloc(mt->tp->pool, sizeof(FnAnalysis));
    if (!collected->node->analysis) {
        log_error("js-mir: failed to allocate shared function analysis");
        mt->collection_failed = true;
        return false;
    }
    memset(collected->node->analysis, 0, sizeof(FnAnalysis));
    return true;
}

static JsFuncCollected* jm_collect_class_field_initializer(JsMirTranspiler* mt,
        JsFieldDefinitionNode* field) {
    if (!mt || !field || !field->value ||
        field->value->node_type == AST_NODE_LITERAL) return NULL;
    if (mt->func_count >= mt->func_capacity) {
        log_error("js-mir: class field initializer exceeds indexed capacity at %d of %d",
            mt->func_count, mt->func_capacity);
        mt->collection_failed = true;
        return NULL;
    }

    JsFunctionNode* function = js_script_field_initializer_ensure(mt->tp, field);
    if (!function) {
        log_error("js-mir: failed to retain indexed class field initializer");
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
    if (!jm_publish_collected_artifact(mt, collected)) {
        log_error("js-mir: failed to publish synthetic FunctionId artifact");
        mt->collection_failed = true;
        return NULL;
    }
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
            method->key->node_type != AST_NODE_MEMBER_EXPR) return name;
    JsMemberNode* member = (JsMemberNode*)method->key;
    return member->property && member->property->node_type == AST_NODE_IDENT
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
            class_node->body->node_type != AST_NODE_BLOCK) {
        log_error("js-mir: indexed class is missing its block body");
        return false;
    }
    JsBlockNode* body = (JsBlockNode*)class_node->body;
    for (JsAstNode* member = body->statements; member; member = member->next) {
        if (member->node_type == AST_NODE_METHOD &&
                ((JsMethodDefinitionNode*)member)->body) {
            entry->member_capacity++;
        } else if (member->node_type == AST_NODE_FIELD) {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)member;
            if (field->key) entry->member_capacity++;
        } else if (member->node_type == JS_AST_NODE_STATIC_BLOCK &&
                ((JsStaticBlockNode*)member)->body) {
            entry->member_capacity++;
        }
    }
    entry->members = (JsClassMember*)pool_calloc(mt->tp->pool,
        (size_t)entry->member_capacity * sizeof(JsClassMember));
    if (entry->member_capacity && !entry->members) {
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
        if (member->node_type == AST_NODE_FIELD) {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)member;
            if (!field->key) continue;
            if (entry->member_count >= entry->member_capacity) return false;
            JsClassMember* field_member = &entry->members[entry->member_count++];
            field_member->kind = field->is_static ? JS_CLASS_MEMBER_STATIC_FIELD
                : JS_CLASS_MEMBER_INSTANCE_FIELD;
            field_member->computed = field->computed;
            field_member->key_expr = field->key;
            // D6.2.2v2: retain literal member spellings for initialization.
            field_member->name = !field->computed
                ? jm_class_member_source_name(mt, entry, field->key) : NULL;
            field_member->initializer = field->value;
            field_member->key_module_var_index = -1;
            if (field->is_static) {
                field_member->module_var_index = -1;
            } else {
                field_member->initializer_fc = jm_collect_class_field_initializer(mt, field);
                if (mt->collection_failed) return false;
            }
            log_debug("js-mir: class '%.*s' %s field %s'%.*s'",
                class_node->name ? (int)class_node->name->len : 5,
                class_node->name ? class_node->name->chars : "anon?",
                field->is_static ? "static" : "instance",
                field->computed ? "[computed] " : "",
                field_member->name ? (int)field_member->name->len : 0,
                field_member->name ? field_member->name->chars : "");
            continue;
        }
        if (member->node_type == JS_AST_NODE_STATIC_BLOCK) {
            JsStaticBlockNode* block = (JsStaticBlockNode*)member;
            if (block->body) {
                if (entry->member_count >= entry->member_capacity) return false;
                JsClassMember* class_member = &entry->members[entry->member_count++];
                class_member->kind = JS_CLASS_MEMBER_STATIC_BLOCK;
                class_member->static_block = block->body;
                log_debug("js-mir: class '%.*s' static block #%d",
                    class_node->name ? (int)class_node->name->len : 5,
                    class_node->name ? class_node->name->chars : "anon?",
                    entry->member_count);
            }
            continue;
        }
        if (member->node_type != AST_NODE_METHOD) continue;
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)member;
        if (!method->body) continue;
        if (entry->member_count >= entry->member_capacity) return false;
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

        JsClassMember* class_member = &entry->members[entry->member_count++];
        class_member->kind = JS_CLASS_MEMBER_METHOD;
        JsClassMember* method_entry = class_member;
        method_entry->name = method_name;
        method_entry->fc = collected;
        method_entry->param_count = ast_linked_node_count(
            ((JsFunctionNode*)method)->params);
        JsAstNode* last_param = NULL;
        for (JsAstNode* param = ((JsFunctionNode*)method)->params; param;
                param = param->next) last_param = param;
        if (last_param && (last_param->node_type == AST_NODE_REST_ELEMENT ||
                last_param->node_type == AST_NODE_SPREAD)) {
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
        if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)node;
            if (!decl->id || decl->id->node_type != AST_NODE_IDENT || !decl->init) continue;
            JsIdentifierNode* binding = (JsIdentifierNode*)decl->id;
            if (!binding->name) continue;
            if (decl->init->node_type == AST_NODE_CLASS ||
                    decl->init->node_type == AST_NODE_CLASS_EXPR) {
                JsClassEntry* entry = jm_find_collected_class(mt, (JsClassNode*)decl->init);
                if (!entry) continue;
                if (!entry->name) entry->name = binding->name;
                else if (entry->name->len != binding->name->len ||
                        strncmp(entry->name->chars, binding->name->chars,
                            entry->name->len) != 0) entry->alias_name = binding->name;
                continue;
            }
            if (decl->init->node_type != AST_NODE_IDENT) continue;
            JsIdentifierNode* source = (JsIdentifierNode*)decl->init;
            JsClassEntry* entry = jm_find_class_for_binding(mt, source->entry);
            if (entry && !entry->alias_name) entry->alias_name = binding->name;
        } else if (node->node_type == AST_NODE_ASSIGN) {
            JsAssignmentNode* assignment = (JsAssignmentNode*)node;
            if (!assignment->left || assignment->left->node_type != AST_NODE_IDENT ||
                    !assignment->right || (assignment->right->node_type !=
                    AST_NODE_CLASS && assignment->right->node_type !=
                    AST_NODE_CLASS_EXPR)) continue;
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
        if (!jm_publish_collected_artifact(mt, function)) {
            log_error("js-mir: failed to publish FunctionId artifact");
            mt->collection_failed = true;
            return;
        }
        JM_JS_FACT(function, is_strict) = jm_indexed_function_is_strict(mt,
            function->node);
    }
    mt->class_count = mt->class_capacity;
    for (int class_index = 0; class_index < mt->class_count; class_index++) {
        JsClassEntry* entry = &mt->class_entries[class_index];
        memset(entry, 0, sizeof(JsClassEntry));
        entry->node = (JsClassNode*)index->classes[class_index];
        entry->name = entry->node->name;
        entry->is_declaration = entry->node->node_type == AST_NODE_CLASS;
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

JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn) {
    return jm_collected_func_by_id(mt, jm_function_id_for_node(mt, fn));
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
    if (!node || node->node_type != AST_NODE_LITERAL) return true;
    JsLiteralNode* literal = (JsLiteralNode*)node;
    scan->found = literal->literal_type == AST_LITERAL_NUMBER && literal->is_bigint;
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
    bool is_alias;
} JmParamInferenceBinding;

static int jm_infer_find_param(JsAstNode* node,
        const JmParamInferenceBinding bindings[], int binding_count) {
    if (!node || node->node_type != AST_NODE_IDENT) return -1;
    JsIdentifierNode* id = (JsIdentifierNode*)node;
    if (!id->entry) return -1;
    for (int i = 0; i < binding_count; i++) {
        if (bindings[i].entry == id->entry) return bindings[i].param_index;
    }
    return -1;
}

static bool jm_infer_param_binding_is_alias(JsAstNode* node,
        const JmParamInferenceBinding bindings[], int binding_count) {
    if (!node || node->node_type != AST_NODE_IDENT) return false;
    NameEntry* entry = ((JsIdentifierNode*)node)->entry;
    for (int i = 0; entry && i < binding_count; i++) {
        if (bindings[i].entry == entry) return bindings[i].is_alias;
    }
    return false;
}

// Direct-body alias evidence is a source-ordered current-binding relation.
// A later non-alias write clears the relation before indexed uses are scored,
// so a stale local value cannot strengthen a parameter candidate.
static void jm_infer_forget_direct_alias(JmParamInferenceBinding bindings[],
        int* binding_count, int param_count, NameEntry* entry) {
    if (!bindings || !binding_count || !entry) return;
    for (int index = param_count; index < *binding_count; index++) {
        if (bindings[index].entry != entry) continue;
        for (int next = index + 1; next < *binding_count; next++) {
            bindings[next - 1] = bindings[next];
        }
        (*binding_count)--;
        return;
    }
}

static bool jm_infer_is_direct_local_binding(JsFunctionNode* function,
        NameEntry* entry) {
    if (!function || !entry || entry->is_parameter || !entry->node ||
            !jm_entry_is_owned_by_function(function, entry)) {
        return false;
    }
    JsAstNode* definition = (JsAstNode*)entry->node;
    return definition->node_type == AST_NODE_VARIABLE_DECLARATOR ||
        definition->node_type == AST_NODE_VAR_STAM;
}

static void jm_infer_rebind_direct_alias(JsFunctionNode* function,
        JsAstNode* left, JsAstNode* right, JmParamInferenceBinding bindings[],
        int* binding_count, int binding_capacity, int param_count) {
    if (!function || !left || left->node_type != AST_NODE_IDENT || !bindings ||
            !binding_count) {
        return;
    }
    JsIdentifierNode* target = (JsIdentifierNode*)left;
    if (!jm_infer_is_direct_local_binding(function, target->entry)) return;

    jm_infer_forget_direct_alias(bindings, binding_count, param_count,
        target->entry);
    int param_index = jm_infer_find_param(right, bindings, *binding_count);
    if (param_index < 0 || *binding_count >= binding_capacity) return;
    bindings[(*binding_count)++] = {target->entry, param_index, true};
}

static int jm_infer_direct_alias_capacity(JsBlockNode* body) {
    int capacity = 0;
    for (JsAstNode* statement = body ? body->statements : NULL; statement;
            statement = statement->next) {
        if (statement->node_type == AST_NODE_VAR_STAM) {
            JsVariableDeclarationNode* declaration =
                (JsVariableDeclarationNode*)statement;
            for (JsAstNode* item = declaration->declarations; item;
                    item = item->next) {
                if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
                JsVariableDeclaratorNode* declarator =
                    (JsVariableDeclaratorNode*)item;
                if (declarator->id && declarator->id->node_type == AST_NODE_IDENT) {
                    capacity++;
                }
            }
            continue;
        }
        if (statement->node_type != AST_NODE_EXPR_STMT) continue;
        JsAstNode* expression = ((JsExpressionStatementNode*)statement)->expression;
        if (expression && expression->node_type == AST_NODE_ASSIGN &&
                ((JsAssignmentNode*)expression)->op == OPERATOR_ASSIGN &&
                ((JsAssignmentNode*)expression)->left &&
                ((JsAssignmentNode*)expression)->left->node_type == AST_NODE_IDENT) {
            capacity++;
        }
    }
    return capacity;
}

static void jm_infer_collect_direct_aliases(JsFunctionNode* function,
        JsBlockNode* body, JmParamInferenceBinding bindings[], int* binding_count,
        int binding_capacity, int param_count) {
    if (!function || !body || !bindings || !binding_count) return;
    for (JsAstNode* statement = body->statements; statement;
            statement = statement->next) {
        if (statement->node_type == AST_NODE_VAR_STAM) {
            JsVariableDeclarationNode* declaration =
                (JsVariableDeclarationNode*)statement;
            for (JsAstNode* item = declaration->declarations; item;
                    item = item->next) {
                if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
                JsVariableDeclaratorNode* declarator =
                    (JsVariableDeclaratorNode*)item;
                jm_infer_rebind_direct_alias(function, declarator->id,
                    declarator->init, bindings, binding_count, binding_capacity,
                    param_count);
            }
            continue;
        }
        if (statement->node_type != AST_NODE_EXPR_STMT) continue;
        JsAstNode* expression = ((JsExpressionStatementNode*)statement)->expression;
        if (!expression || expression->node_type != AST_NODE_ASSIGN) continue;
        JsAssignmentNode* assignment = (JsAssignmentNode*)expression;
        if (assignment->op != OPERATOR_ASSIGN) continue;
        jm_infer_rebind_direct_alias(function, assignment->left, assignment->right,
            bindings, binding_count, binding_capacity, param_count);
    }
}

static bool jm_infer_is_int_literal(JsAstNode* node) {
    if (!node || node->node_type != AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)node;
    if (lit->literal_type != AST_LITERAL_NUMBER || lit->is_bigint || lit->has_decimal) return false;
    double value = lit->value.number_value;
    return value == (double)(int64_t)value;
}

static bool jm_infer_is_float_literal(JsAstNode* node) {
    if (!node || node->node_type != AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)node;
    if (lit->literal_type != AST_LITERAL_NUMBER || lit->is_bigint) return false;
    return lit->has_decimal ||
        lit->value.number_value != (double)(int64_t)lit->value.number_value;
}

static bool jm_infer_is_non_numeric_literal(JsAstNode* node) {
    if (!node || node->node_type != AST_NODE_LITERAL) return false;
    JsLiteralNode* lit = (JsLiteralNode*)node;
    return lit->literal_type == AST_LITERAL_UNDEFINED ||
        lit->literal_type == AST_LITERAL_NULL || lit->literal_type == AST_LITERAL_BOOLEAN;
}

static bool jm_infer_identifier_has_numeric_literal_initializer(JsAstNode* node,
        bool* is_float) {
    if (is_float) *is_float = false;
    if (!node || node->node_type != AST_NODE_IDENT) return false;
    NameEntry* entry = ((JsIdentifierNode*)node)->entry;
    if (!entry || !entry->node) return false;
    JsAstNode* definition = (JsAstNode*)entry->node;
    JsVariableDeclaratorNode* declaration = NULL;
    if (definition->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        declaration = (JsVariableDeclaratorNode*)definition;
    } else if (definition->node_type == AST_NODE_VAR_STAM) {
        JsVariableDeclarationNode* statement =
            (JsVariableDeclarationNode*)definition;
        for (JsAstNode* item = statement->declarations; item; item = item->next) {
            if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
            JsVariableDeclaratorNode* candidate =
                (JsVariableDeclaratorNode*)item;
            if (candidate->entry == entry || (candidate->id &&
                    candidate->id->node_type == AST_NODE_IDENT &&
                    ((JsIdentifierNode*)candidate->id)->entry == entry)) {
                declaration = candidate;
                break;
            }
        }
    }
    if (!declaration || !declaration->init) return false;
    if (jm_infer_is_int_literal(declaration->init)) return true;
    if (!jm_infer_is_float_literal(declaration->init)) return false;
    if (is_float) *is_float = true;
    return true;
}

// Consume one indexed node. Child expressions are visited independently by
// AstIndex, so inference no longer needs a second recursive tree walk.
static void jm_infer_indexed_node(JsMirTranspiler* mt, JsAstNode* node,
        const JmParamInferenceBinding bindings[], FnParamEvidence* evidence,
        int binding_count, int param_count, const char* self_name) {
    if (!node) return;
    switch (node->node_type) {
    case AST_NODE_BINARY: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        int li = jm_infer_find_param(bin->left, bindings, binding_count);
        int ri = jm_infer_find_param(bin->right, bindings, binding_count);
        bool arithmetic = bin->op == OPERATOR_SUB || bin->op == OPERATOR_MUL ||
            bin->op == OPERATOR_DIV || bin->op == OPERATOR_MOD || bin->op == OPERATOR_JS_EXP;
        bool comparison = bin->op == OPERATOR_LT || bin->op == OPERATOR_LE ||
            bin->op == OPERATOR_GT || bin->op == OPERATOR_GE || bin->op == OPERATOR_EQ ||
            bin->op == OPERATOR_NE || bin->op == OPERATOR_JS_STRICT_EQ || bin->op == OPERATOR_JS_STRICT_NE;
        bool bitwise = bin->op == OPERATOR_JS_BIT_AND || bin->op == OPERATOR_JS_BIT_OR ||
            bin->op == OPERATOR_JS_BIT_XOR || bin->op == OPERATOR_JS_LSHIFT ||
            bin->op == OPERATOR_JS_RSHIFT || bin->op == OPERATOR_JS_URSHIFT;
        if (arithmetic || (bin->op == OPERATOR_ADD && self_name && li >= 0 && ri >= 0)) {
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
            bool left_float = false;
            bool right_float = false;
            bool left_numeric_local =
                jm_infer_identifier_has_numeric_literal_initializer(bin->left,
                    &left_float);
            bool right_numeric_local =
                jm_infer_identifier_has_numeric_literal_initializer(bin->right,
                    &right_float);
            if (li >= 0 && jm_infer_is_non_numeric_literal(bin->right)) evidence[li].compared_with_non_numeric = true;
            if (ri >= 0 && jm_infer_is_non_numeric_literal(bin->left)) evidence[ri].compared_with_non_numeric = true;
            if (li >= 0 && !jm_infer_is_non_numeric_literal(bin->right)) {
                if (jm_infer_is_int_literal(bin->right)) evidence[li].int_evidence++;
                else if (jm_infer_is_float_literal(bin->right)) evidence[li].float_evidence++;
                else if (right_numeric_local) {
                    if (right_float) evidence[li].float_evidence++;
                    else evidence[li].int_evidence++;
                }
            }
            if (ri >= 0 && !jm_infer_is_non_numeric_literal(bin->left)) {
                if (jm_infer_is_int_literal(bin->left)) evidence[ri].int_evidence++;
                else if (jm_infer_is_float_literal(bin->left)) evidence[ri].float_evidence++;
                else if (left_numeric_local) {
                    if (left_float) evidence[ri].float_evidence++;
                    else evidence[ri].int_evidence++;
                }
            }
        }
        if (bin->op == OPERATOR_JS_NULLISH_COALESCE && li >= 0)
            evidence[li].compared_with_non_numeric = true;
        break;
    }
    case AST_NODE_UNARY: {
        JsUnaryNode* unary = (JsUnaryNode*)node;
        int index = jm_infer_find_param(unary->operand, bindings, binding_count);
        if (index < 0) break;
        switch (unary->op) {
        case OPERATOR_POS: case OPERATOR_ADD: case OPERATOR_NEG: case OPERATOR_SUB:
        case OPERATOR_JS_INCREMENT: case OPERATOR_JS_DECREMENT: case OPERATOR_JS_BIT_NOT:
            evidence[index].int_evidence++; break;
        case OPERATOR_JS_TYPEOF:
            evidence[index].compared_with_non_numeric = true; break;
        default: break;
        }
        break;
    }
    case AST_NODE_CALL_EXPR: {
        JsCallNode* call = (JsCallNode*)node;
        if (self_name && call->callee && call->callee->node_type == AST_NODE_IDENT) {
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
    case AST_NODE_MEMBER_EXPR: {
        JsMemberNode* member = (JsMemberNode*)node;
        if (member->computed) {
            int index = jm_infer_find_param(member->object, bindings, binding_count);
            if (index >= 0) evidence[index].used_as_container = true;
        }
        break;
    }
    case AST_NODE_ASSIGN: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        int left = jm_infer_find_param(assignment->left, bindings, binding_count);
        if (assignment->op == OPERATOR_ASSIGN) {
            // A local alias can be a numeric loop cursor without changing the
            // guarded parameter's entry value. Only the formal itself revokes
            // its Number entry shape.
            if (left >= 0 && !jm_infer_param_binding_is_alias(assignment->left,
                    bindings, binding_count)) {
                evidence[left].param_reassigned = true;
            }
            break;
        }
        bool compound_arith = assignment->op == OPERATOR_JS_ADD_ASSIGN || assignment->op == OPERATOR_JS_SUB_ASSIGN ||
            assignment->op == OPERATOR_JS_MUL_ASSIGN || assignment->op == OPERATOR_JS_DIV_ASSIGN ||
            assignment->op == OPERATOR_JS_MOD_ASSIGN || assignment->op == OPERATOR_JS_EXP_ASSIGN;
        bool compound_bit = assignment->op == OPERATOR_JS_BIT_AND_ASSIGN || assignment->op == OPERATOR_JS_BIT_OR_ASSIGN ||
            assignment->op == OPERATOR_JS_BIT_XOR_ASSIGN || assignment->op == OPERATOR_JS_LSHIFT_ASSIGN ||
            assignment->op == OPERATOR_JS_RSHIFT_ASSIGN || assignment->op == OPERATOR_JS_URSHIFT_ASSIGN;
        if (!compound_arith && !compound_bit) break;
        int right = jm_infer_find_param(assignment->right, bindings, binding_count);
        if (compound_arith && left >= 0 && right >= 0) {
            // A resolved alias on the LHS and a formal RHS form one numeric
            // candidate edge. The native entry still guards both Number inputs;
            // BigInt and coercive calls retain the generic assignment path.
            evidence[left].int_evidence++;
            evidence[right].int_evidence++;
        }
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
// Used only by debug traces, which release builds compile out.
[[maybe_unused]] static const char* jm_param_type_label(JsFuncCollected* fc, int index) {
    TypeId type = jm_param_type(fc, index);
    return type == LMD_TYPE_INT ? "INT" : type == LMD_TYPE_FLOAT ? "FLOAT" : "ANY";
}

// Debug trace of the first three inferred formal types.
static void jm_log_param_types(const char* phase, const char* name,
        JsFuncCollected* fc, int pc) {
    log_debug("js-mir %s param types for %s: [%s%s%s%s%s%s]", phase, name,
        pc > 0 ? jm_param_type_label(fc, 0) : "", pc > 1 ? "," : "",
        pc > 1 ? jm_param_type_label(fc, 1) : "", pc > 2 ? "," : "",
        pc > 2 ? jm_param_type_label(fc, 2) : "", pc > 3 ? ",..." : "");
}

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
            jm_log_param_types("P3.4: annotation-based",
                fn->name ? fn->name->chars : "(anon)", fc, pc);
        }
    }

    if (use_annotations) return;  // annotations took priority

    // One indexed inference pass covers formals and their direct-body aliases.
    // The prior spelling-based alias re-walk confused shadowed bindings and
    // retained a second full body scan after identity publication.
    // Direct-body alias chains are keyed by resolved bindings. Count their
    // possible declarations before allocating the compiler-owned worklist so
    // `cursor = first` can reuse an earlier `first = param` edge without a
    // fixed small alias limit.
    JsBlockNode* body_blk = fn->body &&
        fn->body->node_type == AST_NODE_BLOCK
        ? (JsBlockNode*)fn->body : NULL;
    int direct_alias_capacity = jm_infer_direct_alias_capacity(body_blk);
    int inference_binding_capacity = pc + direct_alias_capacity;
    JmParamInferenceBinding* inference_bindings =
        (JmParamInferenceBinding*)mem_calloc((size_t)inference_binding_capacity,
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
        inference_bindings[i] = {binding ? binding->entry : NULL, i, false};
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

    jm_infer_collect_direct_aliases(fn, body_blk, inference_bindings,
        &inference_binding_count, inference_binding_capacity, pc);

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

    jm_log_param_types("P4: inferred", fc->name, fc, pc);
    mem_free(inference_bindings);
    mem_free(evidence);
}

// check if a + expression chain contains an operand known to produce a string
bool jm_add_chain_has_string(JsAstNode* expr) {
    if (!expr) return false;
    if (expr->node_type == AST_NODE_LITERAL)
        return ((JsLiteralNode*)expr)->literal_type == AST_LITERAL_STRING;
    if (expr->node_type == JS_AST_NODE_TEMPLATE_LITERAL) return true;
    if (expr->node_type == AST_NODE_CALL_EXPR) {
        JsCallNode* call = (JsCallNode*)expr;
        if (call->callee && call->callee->node_type == AST_NODE_IDENT) {
            JsIdentifierNode* id = (JsIdentifierNode*)call->callee;
            if (id->name && id->name->len == 6 && strncmp(id->name->chars, "String", 6) == 0)
                return true;
        }
    }
    if (expr->node_type == AST_NODE_BINARY) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        if (bin->op == OPERATOR_ADD)
            return jm_add_chain_has_string(bin->left) || jm_add_chain_has_string(bin->right);
    }
    return false;
}

// Classify one return node. The indexed owner filter below excludes nested
// functions, so this helper has no recursive AST traversal of its own.
static Type* jm_type_from_effective_id(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT: return &TYPE_INT;
    case LMD_TYPE_FLOAT: return &TYPE_FLOAT;
    case LMD_TYPE_BOOL: return &TYPE_BOOL;
    case LMD_TYPE_STRING: return &TYPE_STRING;
    case LMD_TYPE_NULL: return &TYPE_NULL;
    case LMD_TYPE_DECIMAL: return &TYPE_DECIMAL;
    default: return &TYPE_ANY;
    }
}

static Type* jm_return_expression_contract(JsMirTranspiler* mt,
        JsFuncCollected* fc, JsAstNode* expression) {
    if (!expression) return &TYPE_NULL;
    if (expression->type && expression->type->type_id != LMD_TYPE_ANY) {
        return expression->type;
    }
    if (expression->node_type == AST_NODE_IDENT && fc && fc->node) {
        JsIdentifierNode* identifier = (JsIdentifierNode*)expression;
        JsAstNode* param = fc->node->params;
        for (int index = 0; param && index < JM_PARAM_COUNT(fc);
                index++, param = param->next) {
            JsIdentifierNode* binding = js_ast_parameter_binding_identifier(param);
            if (identifier->entry && binding && identifier->entry == binding->entry) {
                return jm_type_from_effective_id(jm_param_type(fc, index));
            }
        }
    }
    return jm_type_from_effective_id(jm_get_effective_type(mt, expression));
}

// Return contracts are joined through Lambda type operations. The native TypeId
// is derived afterwards, so no JS-only TypeId join becomes a second authority.
static void jm_collect_return_contract(JsMirTranspiler* mt, JsAstNode* node,
        JsFuncCollected* fc, Type** collected, int* count, int max_count) {
    if (!node || node->node_type != AST_NODE_RETURN_STAM ||
            !count || *count >= max_count) return;
    JsReturnNode* returned = (JsReturnNode*)node;
    collected[(*count)++] = jm_return_expression_contract(mt, fc,
        returned->argument);
}

static TypeId jm_native_return_type_from_contract(Type* contract) {
    if (!contract) return LMD_TYPE_ANY;
    switch (contract->type_id) {
    case LMD_TYPE_INT:
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_BOOL:
    case LMD_TYPE_STRING:
    case LMD_TYPE_NULL:
        return contract->type_id;
    default:
        return LMD_TYPE_ANY;
    }
}

static void jm_publish_return_contract(JsFuncCollected* fc, Type* contract) {
    FnAnalysis* analysis = jm_function_analysis(fc);
    analysis->js_return_contract = contract ? contract : &TYPE_ANY;
    analysis->js_return_type = jm_native_return_type_from_contract(
        analysis->js_return_contract);
}

void jm_infer_return_type(JsMirTranspiler* mt, JsFuncCollected* fc) {
    if (!fc || !fc->node) return;
    JsFunctionNode* fn = fc->node;
    jm_publish_return_contract(fc, &TYPE_ANY);

    // A declared contract is already a shared Type owner. It remains visible
    // even if it cannot select a raw JS native lane (D8.2.4-D8.2.6).
    if (fn->declared_return_type) {
        jm_publish_return_contract(fc, fn->declared_return_type);
        log_debug("js-mir return contract: annotation for %s", fc->name);
        return;
    }

    if (fn->body && fn->body->node_type != AST_NODE_BLOCK) {
        jm_publish_return_contract(fc, jm_return_expression_contract(mt, fc,
            fn->body));
        return;
    }

    Type* collected[32] = {};
    int count = 0;
    if (mt && mt->tp) {
        AstIndex* index = &mt->tp->ast_index;
        AstNodeId function_node_id = ast_index_find(index, (AstNode*)fn);
        AstFunctionId function_id = function_node_id == AST_NODE_ID_INVALID
            ? AST_FUNCTION_ID_INVALID : index->owner_functions[function_node_id];
        for (uint32_t index_id = 0; index_id < index->count && count < 32;
                index_id++) {
            if (index->owner_functions[index_id] != function_id) continue;
            jm_collect_return_contract(mt, index->nodes[index_id], fc, collected,
                &count, 32);
        }
    }

    if (count == 0) {
        jm_publish_return_contract(fc, &TYPE_NULL);
        return;
    }

    Type* joined = NULL;
    Pool* pool = mt && mt->tp ? mt->tp->pool : NULL;
    for (int index = 0; index < count; index++) {
        Type* next = collected[index] ? collected[index] : &TYPE_ANY;
        if (!joined) {
            joined = next;
        } else if (joined->type_id == LMD_TYPE_ANY ||
                next->type_id == LMD_TYPE_ANY || !pool) {
            joined = &TYPE_ANY;
        } else {
            joined = lambda_type_union_normalized(pool, joined, next);
            if (!joined) joined = &TYPE_ANY;
        }
    }
    jm_publish_return_contract(fc, joined);
    log_debug("js-mir return contract: inferred %s", fc->name);
}

// T12-2 keeps this proof local to return inference. It recognizes only Number
// expressions that a guarded native entry can reproduce without coercion.
enum JmNumericReturnFact {
    JM_NUMERIC_RETURN_INVALID = 0,
    JM_NUMERIC_RETURN_NUMBER,
    JM_NUMERIC_RETURN_PENDING,
};

enum {
    JM_NUMERIC_RETURN_MAX_BINDINGS = 32,
    JM_NUMERIC_RETURN_MAX_DEPENDENCIES = 32,
    JM_NUMERIC_RETURN_MAX_FIXPOINT_PASSES = 64,
};

struct JmNumericReturnCandidate {
    bool valid;
    bool has_number_base;
    bool proven;
    JsFuncCollected* dependencies[JM_NUMERIC_RETURN_MAX_DEPENDENCIES];
    int dependency_count;
};

struct JmNumericReturnContext {
    JsMirTranspiler* mt;
    JsFuncCollected* fc;
    JmNumericReturnCandidate* candidate;
    NameEntry* active_bindings[JM_NUMERIC_RETURN_MAX_BINDINGS];
    JmNumericReturnFact active_facts[JM_NUMERIC_RETURN_MAX_BINDINGS];
    int active_count;
};

static bool jm_numeric_return_is_number(JmNumericReturnFact fact) {
    return fact == JM_NUMERIC_RETURN_NUMBER || fact == JM_NUMERIC_RETURN_PENDING;
}

static JmNumericReturnFact jm_numeric_return_merge(JmNumericReturnFact left,
        JmNumericReturnFact right) {
    if (left == JM_NUMERIC_RETURN_INVALID || right == JM_NUMERIC_RETURN_INVALID) {
        return JM_NUMERIC_RETURN_INVALID;
    }
    return left == JM_NUMERIC_RETURN_PENDING || right == JM_NUMERIC_RETURN_PENDING
        ? JM_NUMERIC_RETURN_PENDING : JM_NUMERIC_RETURN_NUMBER;
}

static bool jm_numeric_return_is_arithmetic_operator(Operator op) {
    switch (op) {
    case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
    case OPERATOR_DIV: case OPERATOR_MOD: case OPERATOR_JS_EXP:
    case OPERATOR_JS_BIT_AND: case OPERATOR_JS_BIT_OR:
    case OPERATOR_JS_BIT_XOR: case OPERATOR_JS_LSHIFT:
    case OPERATOR_JS_RSHIFT: case OPERATOR_JS_URSHIFT:
        return true;
    default:
        return false;
    }
}

static bool jm_numeric_return_is_compound_operator(Operator op) {
    switch (op) {
    case OPERATOR_JS_ADD_ASSIGN: case OPERATOR_JS_SUB_ASSIGN:
    case OPERATOR_JS_MUL_ASSIGN: case OPERATOR_JS_DIV_ASSIGN:
    case OPERATOR_JS_MOD_ASSIGN: case OPERATOR_JS_EXP_ASSIGN:
    case OPERATOR_JS_BIT_AND_ASSIGN: case OPERATOR_JS_BIT_OR_ASSIGN:
    case OPERATOR_JS_BIT_XOR_ASSIGN: case OPERATOR_JS_LSHIFT_ASSIGN:
    case OPERATOR_JS_RSHIFT_ASSIGN: case OPERATOR_JS_URSHIFT_ASSIGN:
        return true;
    default:
        return false;
    }
}

static void jm_numeric_return_add_dependency(JmNumericReturnContext* context,
        JsFuncCollected* dependency) {
    if (!context || !context->candidate || !dependency) return;
    JmNumericReturnCandidate* candidate = context->candidate;
    for (int i = 0; i < candidate->dependency_count; i++) {
        if (candidate->dependencies[i] == dependency) return;
    }
    if (candidate->dependency_count >= JM_NUMERIC_RETURN_MAX_DEPENDENCIES) {
        candidate->valid = false;
        log_debug("js-mir T12-2: native return refused for %s: dependency bound exhausted",
            context->fc ? context->fc->name : "(unknown)");
        return;
    }
    candidate->dependencies[candidate->dependency_count++] = dependency;
}

static JsVariableDeclaratorNode* jm_numeric_return_binding_declarator(
        NameEntry* binding) {
    if (!binding || !binding->node) return NULL;
    JsAstNode* definition = (JsAstNode*)binding->node;
    if (definition->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        return (JsVariableDeclaratorNode*)definition;
    }
    if (definition->node_type != AST_NODE_VAR_STAM) return NULL;
    JsVariableDeclarationNode* declaration =
        (JsVariableDeclarationNode*)definition;
    for (JsAstNode* item = declaration->declarations; item; item = item->next) {
        if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)item;
        if (declarator->entry == binding) return declarator;
        if (declarator->id && declarator->id->node_type == AST_NODE_IDENT &&
                ((JsIdentifierNode*)declarator->id)->entry == binding) {
            return declarator;
        }
    }
    return NULL;
}

static JmNumericReturnFact jm_numeric_return_expression(
        JmNumericReturnContext* context, JsAstNode* expression);

static JmNumericReturnFact jm_numeric_return_parameter_fact(
        JsFuncCollected* fc, NameEntry* binding) {
    if (!fc || !binding) return JM_NUMERIC_RETURN_INVALID;
    JsAstNode* parameter = fc->node ? fc->node->params : NULL;
    for (int index = 0; parameter && index < JM_PARAM_COUNT(fc);
            index++, parameter = parameter->next) {
        JsIdentifierNode* parameter_id =
            js_ast_parameter_binding_identifier(parameter);
        if (!parameter_id || parameter_id->entry != binding) continue;
        TypeId type = jm_param_type(fc, index);
        return type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT
            ? JM_NUMERIC_RETURN_NUMBER : JM_NUMERIC_RETURN_INVALID;
    }
    return JM_NUMERIC_RETURN_INVALID;
}

static JmNumericReturnFact jm_numeric_return_binding_fact(
        JmNumericReturnContext* context, NameEntry* binding) {
    if (!context || !context->fc || !binding) return JM_NUMERIC_RETURN_INVALID;
    JmNumericReturnFact parameter = jm_numeric_return_parameter_fact(context->fc,
        binding);
    if (parameter != JM_NUMERIC_RETURN_INVALID || binding->is_parameter) {
        return parameter;
    }
    if (!jm_entry_is_owned_by_function(context->fc->node, binding)) {
        return JM_NUMERIC_RETURN_INVALID;
    }
    for (int i = context->active_count - 1; i >= 0; i--) {
        if (context->active_bindings[i] == binding) return context->active_facts[i];
    }
    if (context->active_count >= JM_NUMERIC_RETURN_MAX_BINDINGS) {
        context->candidate->valid = false;
        log_debug("js-mir T12-2: native return refused for %s: binding bound exhausted",
            context->fc->name);
        return JM_NUMERIC_RETURN_INVALID;
    }
    JsVariableDeclaratorNode* declarator =
        jm_numeric_return_binding_declarator(binding);
    if (!declarator || !declarator->init) return JM_NUMERIC_RETURN_INVALID;

    int active_index = context->active_count++;
    context->active_bindings[active_index] = binding;
    context->active_facts[active_index] = JM_NUMERIC_RETURN_INVALID;
    JmNumericReturnFact fact = jm_numeric_return_expression(context,
        declarator->init);
    context->active_facts[active_index] = fact;
    if (!jm_numeric_return_is_number(fact)) {
        context->active_count--;
        return JM_NUMERIC_RETURN_INVALID;
    }

    AstIndex* index = context->mt && context->mt->tp
        ? &context->mt->tp->ast_index : NULL;
    if (!index) {
        context->active_count--;
        return JM_NUMERIC_RETURN_INVALID;
    }
    // Every direct write must preserve Number. This is the conservative join
    // across branches and loop backedges; a non-Number write widens the fact.
    for (uint32_t node_id = 0; node_id < index->count; node_id++) {
        if (index->owner_functions[node_id] != context->fc->function_id) continue;
        JsAstNode* node = (JsAstNode*)index->nodes[node_id];
        if (!node) continue;
        if (node->node_type == AST_NODE_ASSIGN) {
            JsAssignmentNode* assignment = (JsAssignmentNode*)node;
            if (!assignment->left || assignment->left->node_type != AST_NODE_IDENT ||
                    ((JsIdentifierNode*)assignment->left)->entry != binding) continue;
            if (binding->is_const) {
                fact = JM_NUMERIC_RETURN_INVALID;
                break;
            }
            JmNumericReturnFact written = jm_numeric_return_expression(context,
                assignment->right);
            if (assignment->op == OPERATOR_ASSIGN) {
                fact = written;
            } else if (jm_numeric_return_is_compound_operator(assignment->op) &&
                    jm_numeric_return_is_number(fact) &&
                    jm_numeric_return_is_number(written)) {
                fact = jm_numeric_return_merge(fact, written);
            } else {
                fact = JM_NUMERIC_RETURN_INVALID;
            }
        } else if (node->node_type == AST_NODE_UNARY) {
            JsUnaryNode* unary = (JsUnaryNode*)node;
            if (!unary->operand || unary->operand->node_type != AST_NODE_IDENT ||
                    ((JsIdentifierNode*)unary->operand)->entry != binding) continue;
            if (binding->is_const || (unary->op != OPERATOR_JS_INCREMENT &&
                    unary->op != OPERATOR_JS_DECREMENT)) {
                fact = JM_NUMERIC_RETURN_INVALID;
            }
        }
        context->active_facts[active_index] = fact;
        if (!jm_numeric_return_is_number(fact)) break;
    }
    context->active_count--;
    return fact;
}

static JmNumericReturnFact jm_numeric_return_direct_call(
        JmNumericReturnContext* context, JsCallNode* call) {
    if (!context || !call) return JM_NUMERIC_RETURN_INVALID;
    JsFunctionNode* function = jm_resolve_direct_call_function(context->mt, call,
        true);
    JsFuncCollected* callee = function
        ? jm_find_collected_func(context->mt, function) : NULL;
    if (!callee || JM_JS_FACT(callee, is_reassigned) || callee->node->is_async ||
            callee->node->is_generator) {
        return JM_NUMERIC_RETURN_INVALID;
    }
    int argument_count = 0;
    for (JsAstNode* argument = call->arguments; argument; argument = argument->next) {
        if (!jm_numeric_return_is_number(jm_numeric_return_expression(context,
                argument))) {
            return JM_NUMERIC_RETURN_INVALID;
        }
        argument_count++;
    }
    if (argument_count != JM_PARAM_COUNT(callee)) return JM_NUMERIC_RETURN_INVALID;
    for (int index = 0; index < JM_PARAM_COUNT(callee); index++) {
        TypeId parameter_type = jm_param_type(callee, index);
        if (parameter_type != LMD_TYPE_INT && parameter_type != LMD_TYPE_FLOAT) {
            return JM_NUMERIC_RETURN_INVALID;
        }
    }
    jm_numeric_return_add_dependency(context, callee);
    return context->candidate->valid ? JM_NUMERIC_RETURN_PENDING
        : JM_NUMERIC_RETURN_INVALID;
}

static JmNumericReturnFact jm_numeric_return_expression(
        JmNumericReturnContext* context, JsAstNode* expression) {
    if (!context || !expression) return JM_NUMERIC_RETURN_INVALID;
    switch (expression->node_type) {
    case AST_NODE_LITERAL: {
        JsLiteralNode* literal = (JsLiteralNode*)expression;
        return literal->literal_type == AST_LITERAL_NUMBER && !literal->is_bigint
            ? JM_NUMERIC_RETURN_NUMBER : JM_NUMERIC_RETURN_INVALID;
    }
    case AST_NODE_IDENT:
        return jm_numeric_return_binding_fact(context,
            ((JsIdentifierNode*)expression)->entry);
    case AST_NODE_BINARY: {
        JsBinaryNode* binary = (JsBinaryNode*)expression;
        if (!jm_numeric_return_is_arithmetic_operator(binary->op)) {
            return JM_NUMERIC_RETURN_INVALID;
        }
        return jm_numeric_return_merge(jm_numeric_return_expression(context,
                binary->left), jm_numeric_return_expression(context, binary->right));
    }
    case AST_NODE_UNARY: {
        JsUnaryNode* unary = (JsUnaryNode*)expression;
        switch (unary->op) {
        case OPERATOR_POS: case OPERATOR_NEG: case OPERATOR_JS_BIT_NOT:
        case OPERATOR_JS_INCREMENT: case OPERATOR_JS_DECREMENT:
            return jm_numeric_return_expression(context, unary->operand);
        default:
            return JM_NUMERIC_RETURN_INVALID;
        }
    }
    case AST_NODE_ASSIGN: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)expression;
        if (assignment->op == OPERATOR_ASSIGN) {
            return jm_numeric_return_expression(context, assignment->right);
        }
        if (!jm_numeric_return_is_compound_operator(assignment->op) ||
                !assignment->left || assignment->left->node_type != AST_NODE_IDENT) {
            return JM_NUMERIC_RETURN_INVALID;
        }
        return jm_numeric_return_merge(jm_numeric_return_binding_fact(context,
                ((JsIdentifierNode*)assignment->left)->entry),
            jm_numeric_return_expression(context, assignment->right));
    }
    case AST_NODE_CONDITIONAL_EXPR: {
        JsConditionalNode* conditional = (JsConditionalNode*)expression;
        return jm_numeric_return_merge(jm_numeric_return_expression(context,
                conditional->consequent), jm_numeric_return_expression(context,
                conditional->alternate));
    }
    case AST_NODE_SEQ: {
        JsSequenceNode* sequence = (JsSequenceNode*)expression;
        JsAstNode* last = sequence->expressions;
        if (!last) return JM_NUMERIC_RETURN_INVALID;
        while (last->next) last = last->next;
        return jm_numeric_return_expression(context, last);
    }
    case AST_NODE_CALL_EXPR:
        return jm_numeric_return_direct_call(context, (JsCallNode*)expression);
    default:
        return JM_NUMERIC_RETURN_INVALID;
    }
}

static void jm_numeric_return_collect_candidate(JmNumericReturnContext* context) {
    if (!context || !context->mt || !context->fc || !context->candidate) return;
    JmNumericReturnCandidate* candidate = context->candidate;
    candidate->valid = true;
    JsFunctionNode* function = context->fc->node;
    if (function->body && function->body->node_type != AST_NODE_BLOCK) {
        JmNumericReturnFact fact = jm_numeric_return_expression(context,
            function->body);
        candidate->has_number_base = fact == JM_NUMERIC_RETURN_NUMBER;
        candidate->valid = jm_numeric_return_is_number(fact) && candidate->valid;
        return;
    }
    AstIndex* index = &context->mt->tp->ast_index;
    bool has_return = false;
    for (uint32_t node_id = 0; node_id < index->count; node_id++) {
        if (index->owner_functions[node_id] != context->fc->function_id ||
                index->nodes[node_id]->node_type != AST_NODE_RETURN_STAM) continue;
        has_return = true;
        JsReturnNode* returned = (JsReturnNode*)index->nodes[node_id];
        JmNumericReturnFact fact = returned->argument
            ? jm_numeric_return_expression(context, returned->argument)
            : JM_NUMERIC_RETURN_INVALID;
        if (!jm_numeric_return_is_number(fact)) candidate->valid = false;
        if (fact == JM_NUMERIC_RETURN_NUMBER) candidate->has_number_base = true;
    }
    candidate->valid = candidate->valid && has_return;
}

static bool jm_function_has_numeric_local_facts(JsMirTranspiler* mt,
        JsFuncCollected* fc) {
    if (!mt || !fc || !mt->tp) return false;
    AstIndex* index = &mt->tp->ast_index;
    for (uint32_t node_id = 0; node_id < index->count; node_id++) {
        if (index->owner_functions[node_id] != fc->function_id ||
                !index->nodes[node_id] || index->nodes[node_id]->node_type !=
                AST_NODE_VARIABLE_DECLARATOR) {
            continue;
        }
        JsVariableDeclaratorNode* declarator =
            (JsVariableDeclaratorNode*)index->nodes[node_id];
        if (!declarator->id || declarator->id->node_type != AST_NODE_IDENT) continue;
        JmNumericReturnCandidate candidate = {};
        candidate.valid = true;
        JmNumericReturnContext context = {mt, fc, &candidate};
        if (jm_numeric_return_binding_fact(&context,
                ((JsIdentifierNode*)declarator->id)->entry) ==
                JM_NUMERIC_RETURN_NUMBER && candidate.valid) {
            return true;
        }
    }
    return false;
}

void jm_infer_native_numeric_returns(JsMirTranspiler* mt) {
    if (!mt || mt->func_count <= 0) return;
    JmNumericReturnCandidate* candidates = (JmNumericReturnCandidate*)mem_calloc(
        (size_t)mt->func_count, sizeof(*candidates), MEM_CAT_TEMP);
    if (!candidates) {
        log_error("js-mir T12-2: native return analysis allocation failed");
        return;
    }
    for (int index = 0; index < mt->func_count; index++) {
        JsFuncCollected* fc = &mt->func_entries[index];
        JM_JS_FACT(fc, native_numeric_proven) = false;
        JM_JS_FACT(fc, has_numeric_local_facts) = false;
        JmNumericReturnContext context = {mt, fc, &candidates[index]};
        jm_numeric_return_collect_candidate(&context);
    }

    int bound = mt->func_count < JM_NUMERIC_RETURN_MAX_FIXPOINT_PASSES
        ? mt->func_count : JM_NUMERIC_RETURN_MAX_FIXPOINT_PASSES;
    bool changed = false;
    int pass = 0;
    for (; pass < bound; pass++) {
        changed = false;
        for (int index = 0; index < mt->func_count; index++) {
            JmNumericReturnCandidate* candidate = &candidates[index];
            if (!candidate->valid) continue;
            for (int dependency = 0; dependency < candidate->dependency_count;
                    dependency++) {
                int dependency_index = (int)(candidate->dependencies[dependency] -
                    mt->func_entries);
                if (dependency_index < 0 || dependency_index >= mt->func_count ||
                        !candidates[dependency_index].valid) {
                    candidate->valid = false;
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) break;
    }
    if (changed) {
        log_debug("js-mir T12-2: native return refusal: dependency fixed-point bound exhausted");
        for (int index = 0; index < mt->func_count; index++) candidates[index].valid = false;
    }

    for (pass = 0; pass < bound; pass++) {
        changed = false;
        for (int index = 0; index < mt->func_count; index++) {
            JmNumericReturnCandidate* candidate = &candidates[index];
            if (!candidate->valid || candidate->proven) continue;
            bool ready = candidate->has_number_base;
            if (!ready && candidate->dependency_count > 0) {
                ready = true;
                for (int dependency = 0; dependency < candidate->dependency_count;
                        dependency++) {
                    int dependency_index = (int)(candidate->dependencies[dependency] -
                        mt->func_entries);
                    if (dependency_index < 0 || dependency_index >= mt->func_count ||
                            !candidates[dependency_index].proven) {
                        ready = false;
                        break;
                    }
                }
            }
            if (ready) {
                candidate->proven = true;
                changed = true;
            }
        }
        if (!changed) break;
    }
    if (changed) {
        log_debug("js-mir T12-2: native return refusal: proof fixed-point bound exhausted");
        for (int index = 0; index < mt->func_count; index++) candidates[index].proven = false;
    }
    for (int index = 0; index < mt->func_count; index++) {
        JsFuncCollected* fc = &mt->func_entries[index];
        JM_JS_FACT(fc, native_numeric_proven) = candidates[index].proven;
        JM_JS_FACT(fc, has_numeric_local_facts) =
            jm_function_has_numeric_local_facts(mt, fc);
        if (candidates[index].proven) {
            JM_JS_FACT(fc, return_type) = LMD_TYPE_FLOAT;
            log_debug("js-mir T12-2: native Number return proven for %s",
                fc->name);
        }
    }
    mem_free(candidates);
}

void jm_populate_numeric_binding_facts(JsMirTranspiler* mt,
        JsFuncCollected* fc, FnVariantAnalysis* body) {
    // Local Number facts describe the guarded native entry, whose parameter
    // admission has already established Number inputs. A boxed body retains
    // JavaScript's complete value domain and cannot reuse those F64 facts.
    if (!mt || !fc || !body) return;
    AstIndex* index = &mt->tp->ast_index;
    int capacity = 0;
    for (uint32_t node_id = 0; node_id < index->count; node_id++) {
        if (index->owner_functions[node_id] == fc->function_id &&
                index->nodes[node_id] &&
                index->nodes[node_id]->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            capacity++;
        }
    }
    if (capacity == 0) return;
    body->bindings = (FnBindingAnalysis*)pool_calloc(mt->tp->pool,
        sizeof(*body->bindings) * (size_t)capacity);
    if (!body->bindings) {
        log_error("js-mir T12-P2-3: numeric binding facts allocation failed for %s",
            fc->name);
        return;
    }
    for (uint32_t node_id = 0; node_id < index->count; node_id++) {
        if (index->owner_functions[node_id] != fc->function_id ||
                !index->nodes[node_id] || index->nodes[node_id]->node_type !=
                AST_NODE_VARIABLE_DECLARATOR) {
            continue;
        }
        JsVariableDeclaratorNode* declarator =
            (JsVariableDeclaratorNode*)index->nodes[node_id];
        if (!declarator->id || declarator->id->node_type != AST_NODE_IDENT) continue;
        JsIdentifierNode* identifier = (JsIdentifierNode*)declarator->id;
        JmNumericReturnCandidate candidate = {};
        candidate.valid = true;
        JmNumericReturnContext context = {mt, fc, &candidate};
        if (jm_numeric_return_binding_fact(&context, identifier->entry) !=
                JM_NUMERIC_RETURN_NUMBER || !candidate.valid) {
            continue;
        }
        FnBindingAnalysis* fact = &body->bindings[body->binding_count++];
        *fact = {identifier->entry, LMD_TYPE_FLOAT, VALUE_REP_F64,
            JIT_VALUE_NON_GC_SCALAR, BINDING_STORAGE_REGISTER, 0};
    }
}

TypeId jm_numeric_binding_type(JsMirTranspiler* mt, NameEntry* binding) {
    if (!mt || !mt->current_fc || !binding) return LMD_TYPE_ANY;
    FnAnalysis* analysis = jm_function_analysis(mt->current_fc);
    FnVariantAnalysis* body = fn_analysis_variant(analysis,
        mt->in_native_func ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY);
    if (body) {
        for (int index = 0; index < body->binding_count; index++) {
            if (body->bindings[index].name == binding) {
                return body->bindings[index].semantic_type;
            }
        }
    }
    // Native entries may reuse the Number facts recorded for their guarded
    // sibling when an older analysis record lacks the native slot.
    if (mt->in_native_func) {
        body = fn_analysis_variant(analysis, FN_ENTRY_BOXED_BODY);
        for (int index = 0; body && index < body->binding_count; index++) {
            if (body->bindings[index].name == binding) {
                return body->bindings[index].semantic_type;
            }
        }
    }
    return LMD_TYPE_ANY;
}

// Return expressions whose values always fit directly in Item bits or are
// managed objects never borrow a number-stack cell from the activation.
static bool jm_return_expr_needs_scalar_home(JsAstNode* expr);

static bool jm_const_identifier_has_stable_return_value(JsIdentifierNode* id) {
    if (!id || !id->entry || !id->entry->is_const || !id->entry->node ||
            id->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) {
        return false;
    }
    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)id->entry->node;
    return decl->init && !jm_return_expr_needs_scalar_home(decl->init);
}

static bool jm_return_expr_needs_scalar_home(JsAstNode* expr) {
    if (!expr) return false;
    switch (expr->node_type) {
    case AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type != AST_LITERAL_NUMBER || lit->is_bigint) return false;
        return !jm_float_const_is_inline(lit->value.number_value);
    }
    case AST_NODE_ARRAY:
    case AST_NODE_MAP:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_ARROW_FUNC:
    case AST_NODE_CLASS_EXPR:
    case AST_NODE_NEW_EXPR:
        return false;
    case AST_NODE_CONDITIONAL_EXPR: {
        JsConditionalNode* cond = (JsConditionalNode*)expr;
        return jm_return_expr_needs_scalar_home(cond->consequent) ||
            jm_return_expr_needs_scalar_home(cond->alternate);
    }
    case AST_NODE_SEQ: {
        JsSequenceNode* seq = (JsSequenceNode*)expr;
        JsAstNode* last = seq->expressions;
        if (!last) return false;
        while (last->next) last = last->next;
        return jm_return_expr_needs_scalar_home(last);
    }
    case AST_NODE_UNARY: {
        JsUnaryNode* unary = (JsUnaryNode*)expr;
        return unary->op != OPERATOR_NOT && unary->op != OPERATOR_JS_TYPEOF &&
            unary->op != OPERATOR_JS_VOID;
    }
    case AST_NODE_IDENT:
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
                node->node_type != AST_NODE_RETURN_STAM ||
                !ast_index_node_descends(index, i, fn_id)) continue;
        if (jm_return_expr_needs_scalar_home(((JsReturnNode*)node)->argument)) {
            needs_home = true;
            break;
        }
    }
    if (!needs_home) return SCALAR_RETURN_NONE;
    return jit_scalar_return_class_for_type(JM_JS_FACT(fc, return_type));
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
                (parent->node_type != AST_NODE_BINARY &&
                 parent->node_type != AST_NODE_UNARY)) return false;
        node_id = parent_id;
    }
    return true;
}

static bool jm_prescan_node_has_float_hint(JsAstNode* node) {
    if (!node) return false;
    if (node->node_type == AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        if (lit->literal_type != AST_LITERAL_NUMBER) return false;
        return lit->has_decimal || lit->value.number_value !=
            (double)(long long)lit->value.number_value;
    }
    if (node->node_type == AST_NODE_BINARY) {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        return bin->op == OPERATOR_DIV || bin->op == OPERATOR_MOD;
    }
    if (node->node_type != AST_NODE_IDENT) return false;
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
        if (!node || node->node_type != AST_NODE_MEMBER_EXPR ||
                !jm_prescan_expression_path_is_numeric(index, i, root_id, owner)) continue;
        JsMemberNode* member = (JsMemberNode*)node;
        if (!member->computed || !member->object ||
                member->object->node_type != AST_NODE_IDENT) continue;
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
        bool reaches_child = parent->node_type == AST_NODE_BLOCK ||
            (parent->node_type == AST_NODE_LOOP &&
                ((AstLoopControlNode*)parent)->body == child) ||
            (parent->node_type == AST_NODE_IF_EXPR &&
                (((JsIfNode*)parent)->consequent == child ||
                 ((JsIfNode*)parent)->alternate == child)) ||
            (parent->node_type == AST_NODE_EXPR_STMT &&
                ((JsExpressionStatementNode*)parent)->expression == child) ||
            (parent->node_type == AST_NODE_ASSIGN &&
                ((JsAssignmentNode*)parent)->right == child &&
                child->node_type == AST_NODE_ASSIGN);
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
    if (!decl || !decl->id || decl->id->node_type != AST_NODE_IDENT ||
            !decl->init || decl->init->node_type != AST_NODE_NEW_EXPR) return false;
    JsCallNode* construct = (JsCallNode*)decl->init;
    if (!construct->callee || construct->callee->node_type != AST_NODE_IDENT) return false;
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
                node->node_type != AST_NODE_VAR_STAM) continue;
        for (JsAstNode* item = ((JsVariableDeclarationNode*)node)->declarations;
                item; item = item->next) {
            if (item->node_type != AST_NODE_VARIABLE_DECLARATOR ||
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
            if (node->node_type == AST_NODE_ASSIGN) {
                JsAssignmentNode* assignment = (JsAssignmentNode*)node;
                if (!assignment->left ||
                        assignment->left->node_type != AST_NODE_IDENT) continue;
                bool float_array_access = jm_prescan_expression_has_float_array_access(
                    mt, assignment->right, float_arrays);
                bool float_hint = jm_prescan_expression_has_float_hint(mt,
                    assignment->right);
                bool compound_float_hint = assignment->op == OPERATOR_JS_ADD_ASSIGN ||
                    assignment->op == OPERATOR_JS_SUB_ASSIGN ||
                    assignment->op == OPERATOR_JS_MUL_ASSIGN;
                if (float_array_access || assignment->op == OPERATOR_JS_DIV_ASSIGN ||
                        ((assignment->op == OPERATOR_ASSIGN || compound_float_hint) &&
                         float_hint)) {
                    jm_prescan_add_widened_name(mt->widen_to_float,
                        (JsIdentifierNode*)assignment->left, "");
                }
            } else if (node->node_type == AST_NODE_VAR_STAM) {
                for (JsAstNode* item = ((JsVariableDeclarationNode*)node)->declarations;
                        item; item = item->next) {
                    if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
                    JsVariableDeclaratorNode* decl = (JsVariableDeclaratorNode*)item;
                    if (!decl->id || decl->id->node_type != AST_NODE_IDENT ||
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
        heritage->node_type != AST_NODE_IDENT) {
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
            if (jm_can_suspend(mt, chk)) {
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
        while (cy) { if (jm_can_suspend(mt, cy)) { arr_spill_slot = jm_gen_spill_save(mt, array); break; } cy = cy->next; }
    }

    JsAstNode* arg = first_arg;
    while (arg) {
        if (arg->node_type == AST_NODE_SPREAD) {
            JsSpreadElementNode* spread = (JsSpreadElementNode*)arg;
            MIR_reg_t src_raw = jm_transpile_box_item(mt, spread->argument);
            jm_emit_error_lane_propagate_check(mt);
            // Generator spill: restore array after yield in spread argument
            if (arr_spill_slot >= 0 && jm_can_suspend(mt, spread->argument)) {
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
            if (arr_spill_slot >= 0 && jm_can_suspend(mt, arg)) {
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
